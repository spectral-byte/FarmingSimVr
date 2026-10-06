#include "fidelityfx_interceptor.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "luau/vr_bridge.h"
#include "ngx/ngx_interceptor.h"
#include "ffx_mv_conventions.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace fs25vr {

namespace {
// The instance whose detours are live. The detour entry points are plain function pointers, so they
// resolve the interceptor that installed them here instead of reaching for a singleton.
std::atomic<FidelityFxInterceptor*> s_host{nullptr};

constexpr struct { uint32_t bit; const char* name; } kUpscaleCreateFlagNames[] = {
    {FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE, "HDR"},
    {FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS, "DisplayResMV"},
    {FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION, "MVJitterCancellation"},
    {FFX_UPSCALE_ENABLE_DEPTH_INVERTED, "DepthInverted"},
    {FFX_UPSCALE_ENABLE_DEPTH_INFINITE, "DepthInfinite"},
    {FFX_UPSCALE_ENABLE_AUTO_EXPOSURE, "AutoExposure"},
    {FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION, "DynamicResolution"},
    {FFX_UPSCALE_ENABLE_DEBUG_CHECKING, "DebugChecking"},
    {FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE, "NonLinearColorspace"},
    {FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION, "DebugVisualization"},
};

std::string DescribeUpscaleCreateFlags(uint32_t flags) {
    std::string names;
    for (const auto& flag : kUpscaleCreateFlagNames) {
        if (flags & flag.bit) {
            if (!names.empty()) names += "|";
            names += flag.name;
        }
    }
    return names.empty() ? "none" : names;
}

// Jitter offsets closer than this are the same sample position.
constexpr float kJitterMatchTolerance = 1e-4f;
constexpr size_t kMaxListedJitterOffsets = 16;

// Walks the descriptor chain for the first descriptor of the given type.
template <typename Desc>
Desc* FindInChain(ffxApiHeader* head, ffxStructType_t type) {
    for (ffxApiHeader* h = head; h; h = h->pNext) {
        if (h->type == type) return reinterpret_cast<Desc*>(h);
    }
    return nullptr;
}

template <typename Char>
bool IsLoaderFileName(std::basic_string<Char> s) {
    for (auto& c : s) c = static_cast<Char>(towlower(static_cast<wint_t>(c)));
    auto pos = s.find_last_of(std::basic_string<Char>{static_cast<Char>('\\'), static_cast<Char>('/')});
    std::basic_string<Char> filename = (pos == std::basic_string<Char>::npos) ? s : s.substr(pos + 1);
    static constexpr char kName[] = "amd_fidelityfx_loader_dx12";
    std::basic_string<Char> base;
    for (const char* p = kName; *p; ++p) base.push_back(static_cast<Char>(*p));
    std::basic_string<Char> withExt = base;
    for (const char* p = ".dll"; *p; ++p) withExt.push_back(static_cast<Char>(*p));
    return filename == base || filename == withExt;
}
} // namespace

FidelityFxInterceptor* FidelityFxInterceptor::Host() {
    return s_host.load();
}

FidelityFxInterceptor::FidelityFxInterceptor() {
    m_mvPassFactory = [](ID3D12GraphicsCommandList*) { return CreateD3D12MotionVectorPass(); };
}

FidelityFxInterceptor::~FidelityFxInterceptor() {
    if (IsProcessTerminating()) {
        // Releasing GPU objects while the process is going down is not safe; the OS reclaims them.
        (void)m_mvPass.release();
        return;
    }
    DetachAll();
}

void FidelityFxInterceptor::SetMotionVectorPassFactory(MotionVectorPassFactory factory) {
    std::lock_guard<std::mutex> lock(m_mvFixMutex);
    m_mvPass.reset();
    m_mvPassFactory = std::move(factory);
}

std::string FidelityFxInterceptor::GetMvFixDisableReason() const {
    std::lock_guard<std::mutex> lock(m_mvFixMutex);
    return m_mvFixDisableReason;
}

void FidelityFxInterceptor::DisableMvFix(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(m_mvFixMutex);
        if (m_mvFixDisabled.load()) return;
        m_mvFixDisableReason = reason;
    }
    m_mvFixDisabled.store(true);
    LogError("FidelityFxInterceptor: Motion vector pass disabled for this session: " + reason +
             ". FSR3 continues on the engine's motion vectors.");
}

ID3D12Resource* FidelityFxInterceptor::RecordMvPass(const MvPassInputs& inputs, std::vector<MvSelfCheckReport>& reports) {
    std::unique_lock<std::mutex> lock(m_mvFixMutex);
    if (m_mvFixDisabled.load()) return nullptr;
    if (!m_mvPass) {
        if (m_mvPassFactory) m_mvPass = m_mvPassFactory(inputs.cmdList);
        if (!m_mvPass) {
            lock.unlock();
            DisableMvFix("the motion vector pass could not be created");
            return nullptr;
        }
    }
    MvPassResult result = m_mvPass->Record(inputs);
    if (!result.ok || !result.output) {
        lock.unlock();
        DisableMvFix(result.failureReason.empty() ? std::string("the pass reported a failure") : result.failureReason);
        return nullptr;
    }
    reports = m_mvPass->PollSelfCheck();
    return result.output;
}

bool FidelityFxInterceptor::ApplyMvCorrection(ffxDispatchDescUpscale& desc, uint32_t createFlags, uint32_t eye,
                                              uint64_t dispatchIndex, ffxApiResource& savedMv, const char*& state) {
    VRBridge* bridge = GetVRBridge();
    state = "off";
    // The switch is vrDlssMvFix, shared with DLSS.
    std::shared_ptr<NgxInterceptor> ngx = bridge ? bridge->GetNgxInterceptor() : nullptr;
    if (!ngx || !ngx->IsMvFixEnabled() || m_mvFixDisabled.load()) return false;

    if (!desc.commandList || !desc.depth.resource || !desc.motionVectors.resource) {
        state = "missing-inputs";
        return false;
    }
    const FfxMvConventions conventions = ReadFfxMvConventions(createFlags, desc);
    if (conventions.unsupportedReason) {
        state = conventions.unsupportedReason;
        return false;
    }

    const ResolvedCameras resolved = bridge->GetCameraRecords().Resolve(eye);
    mvreproj::CorrectionPlan plan = mvreproj::PlanCorrection(resolved);
    state = mvreproj::CorrectionDecisionName(plan.decision);
    MvCorrectionRequest correctionRequest;
    const MvCorrectionRequest* correction = nullptr;
    if (plan.decision == mvreproj::CorrectionDecision::Correct) {
        if (std::fabs(conventions.mvScaleX) > 1e-6f && std::fabs(conventions.mvScaleY) > 1e-6f) {
            // The dispatch's own camera fields describe the camera that rendered this frame.
            plan.current.infiniteDepth = conventions.depthInfinite;
            if (conventions.hasProjection) {
                plan.current.tanHalfFovY = conventions.tanHalfFovY;
                plan.current.nearClip = conventions.nearClip;
                plan.current.farClip = conventions.farClip;
            }
            correctionRequest.mvScaleX = conventions.mvScaleX;
            correctionRequest.mvScaleY = conventions.mvScaleY;
            correctionRequest.current = plan.current;
            correctionRequest.previousFrame = plan.previousFrame;
            correctionRequest.sameEye = plan.sameEye;
            correctionRequest.toPreviousFrame = plan.toPreviousFrame;
            correctionRequest.toSameEye = plan.toSameEye;
            // AFR: the simulation advances every frame, so only the engine's object motion needs the eye
            // stride (the same eye's history is two frames back). Sequential Dual-Pass replays one time.
            correctionRequest.objectMotionFactor =
                bridge->GetRenderingMode() == VRRenderingMode::AFR ? mvreproj::kAfrEyeStride : 1.0f;
            correction = &correctionRequest;
            state = "correcting";
        } else {
            state = "zero-mv-scale";
        }
    }

    // Self-check (diagnostic window only): score the vectors the upscaler will read against the prediction
    // from depth and the camera records. Measured only.
    MvSelfCheckRequest checkRequest;
    const MvSelfCheckRequest* check = nullptr;
    if (m_diagRemaining.load() > 0) {
        std::string skipReason;
        if (BuildMvSelfCheckRequest(resolved, eye, dispatchIndex, conventions.mvScaleX, conventions.mvScaleY,
                                    correction ? &plan : nullptr, checkRequest, skipReason)) {
            // The dispatch's own camera fields describe the camera that rendered this frame.
            if (correction) checkRequest.current = plan.current;
            checkRequest.current.infiniteDepth = conventions.depthInfinite;
            check = &checkRequest;
        } else {
            LogInfo(FormatMvSelfCheckSkipped("FidelityFxInterceptor", "Dispatch", eye, dispatchIndex, skipReason));
        }
        LogInfo("FidelityFxInterceptor: [Diag] MvCorrection Dispatch #" + std::to_string(dispatchIndex) + " Eye=" +
                std::to_string(eye) + ": " + state);
    }

    MvPassInputs inputs;
    inputs.cmdList = static_cast<ID3D12GraphicsCommandList*>(desc.commandList);
    inputs.depth = static_cast<ID3D12Resource*>(desc.depth.resource);
    inputs.motionVectors = static_cast<ID3D12Resource*>(desc.motionVectors.resource);
    inputs.selfCheck = check;
    inputs.correction = correction;
    std::vector<MvSelfCheckReport> reports;
    ID3D12Resource* corrected = RecordMvPass(inputs, reports);
    for (const MvSelfCheckReport& report : reports) LogInfo(FormatMvSelfCheckReport("FidelityFxInterceptor", "Dispatch", report));
    if (!corrected) {
        state = "failed";
        return false;
    }

    // The pass's texture has the engine's format and size; it is left in the combined shader-read state.
    savedMv = desc.motionVectors;
    desc.motionVectors.resource = corrected;
    desc.motionVectors.state = FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ;
    return true;
}

bool FidelityFxInterceptor::MatchesLoaderName(LPCWSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    return IsLoaderFileName(std::wstring(lpLibFileName));
}

bool FidelityFxInterceptor::MatchesLoaderName(LPCSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    return IsLoaderFileName(std::string(lpLibFileName));
}

bool FidelityFxInterceptor::AttachDirect(PFN_ffxCreateContext pfnCreate, PFN_ffxDestroyContext pfnDestroy,
                                         PFN_ffxConfigure pfnConfigure, PFN_ffxQuery pfnQuery,
                                         PFN_ffxDispatch pfnDispatch) {
    if (!pfnCreate || !pfnDestroy) {
        LogError("FidelityFxInterceptor: ffxCreateContext and ffxDestroyContext must be non-null.");
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_isAttached) {
        Detach();
    }

    // Publish the host before any detour goes live: a game thread can enter a detour the moment it is
    // enabled, and an entry point with no host fails the call.
    FidelityFxInterceptor* previousHost = s_host.exchange(this);

    bool createInstalled = m_detourCreate.Install(reinterpret_cast<void*>(pfnCreate),
                                                  reinterpret_cast<void*>(&FidelityFxInterceptor::Intercept_CreateContext));
    bool destroyInstalled = m_detourDestroy.Install(reinterpret_cast<void*>(pfnDestroy),
                                                    reinterpret_cast<void*>(&FidelityFxInterceptor::Intercept_DestroyContext));
    if (!createInstalled || !destroyInstalled) {
        LogError("FidelityFxInterceptor: Failed to install ffxCreateContext or ffxDestroyContext detours.");
        if (m_detourCreate.IsInstalled()) m_detourCreate.Uninstall();
        if (m_detourDestroy.IsInstalled()) m_detourDestroy.Uninstall();
        FidelityFxInterceptor* expected = this;
        s_host.compare_exchange_strong(expected, previousHost);
        return false;
    }

    if (pfnConfigure &&
        !m_detourConfigure.Install(reinterpret_cast<void*>(pfnConfigure), reinterpret_cast<void*>(&FidelityFxInterceptor::Intercept_Configure))) {
        LogError("FidelityFxInterceptor: Failed to install ffxConfigure detour.");
    }
    if (pfnQuery &&
        !m_detourQuery.Install(reinterpret_cast<void*>(pfnQuery), reinterpret_cast<void*>(&FidelityFxInterceptor::Intercept_Query))) {
        LogError("FidelityFxInterceptor: Failed to install ffxQuery detour.");
    }
    if (pfnDispatch &&
        !m_detourDispatch.Install(reinterpret_cast<void*>(pfnDispatch), reinterpret_cast<void*>(&FidelityFxInterceptor::Intercept_Dispatch))) {
        LogError("FidelityFxInterceptor: Failed to install ffxDispatch detour.");
    }

    m_isAttached = true;
    LogInfo("FidelityFxInterceptor: Successfully attached FidelityFX context lifecycle detours.");
    return true;
}

bool FidelityFxInterceptor::AttachModule(HMODULE hLoaderModule) {
    if (!hLoaderModule) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_isAttached) return true;

    char modPath[MAX_PATH] = {0};
    GetModuleFileNameA(hLoaderModule, modPath, MAX_PATH);

    auto pfnCreate = reinterpret_cast<PFN_ffxCreateContext>(GetProcAddress(hLoaderModule, "ffxCreateContext"));
    auto pfnDestroy = reinterpret_cast<PFN_ffxDestroyContext>(GetProcAddress(hLoaderModule, "ffxDestroyContext"));
    auto pfnConfigure = reinterpret_cast<PFN_ffxConfigure>(GetProcAddress(hLoaderModule, "ffxConfigure"));
    auto pfnQuery = reinterpret_cast<PFN_ffxQuery>(GetProcAddress(hLoaderModule, "ffxQuery"));
    auto pfnDispatch = reinterpret_cast<PFN_ffxDispatch>(GetProcAddress(hLoaderModule, "ffxDispatch"));
    if (!pfnCreate || !pfnDestroy || !pfnDispatch) {
        LogWarn(std::string("FidelityFxInterceptor: Module does not export ffxCreateContext, ffxDestroyContext and ffxDispatch: ") + modPath);
        return false;
    }

    bool ok = AttachDirect(pfnCreate, pfnDestroy, pfnConfigure, pfnQuery, pfnDispatch);
    if (ok) LogInfo(std::string("FidelityFxInterceptor: Successfully attached to FidelityFX loader: ") + modPath);
    return ok;
}

void FidelityFxInterceptor::CheckAndAttachLoadedModule() {
    HMODULE hMod = GetModuleHandleW(L"amd_fidelityfx_loader_dx12.dll");
    if (hMod) AttachModule(hMod);
}

void FidelityFxInterceptor::Detach() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_isAttached) return;

    if (m_detourCreate.IsInstalled()) m_detourCreate.Uninstall();
    if (m_detourDestroy.IsInstalled()) m_detourDestroy.Uninstall();
    if (m_detourConfigure.IsInstalled()) m_detourConfigure.Uninstall();
    if (m_detourQuery.IsInstalled()) m_detourQuery.Uninstall();
    if (m_detourDispatch.IsInstalled()) m_detourDispatch.Uninstall();

    m_isAttached = false;
    FidelityFxInterceptor* expected = this;
    s_host.compare_exchange_strong(expected, nullptr);
    m_lastRenderingMode.store(-1);
    m_internalDiscontinuityPendingLeft.store(false);
    m_internalDiscontinuityPendingRight.store(false);
    LogInfo("FidelityFxInterceptor: FidelityFX detours detached.");
}

void FidelityFxInterceptor::DetachAll() {
    Detach();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_virtualContexts.clear();
    m_vrBridge = nullptr;
    m_lastRenderingMode.store(-1);
    m_internalDiscontinuityPendingLeft.store(false);
    m_internalDiscontinuityPendingRight.store(false);
    m_diagRemaining.store(0);
    {
        std::lock_guard<std::mutex> mvLock(m_mvFixMutex);
        m_mvPass.reset();
        m_mvFixDisableReason.clear();
        m_mvPassFactory = [](ID3D12GraphicsCommandList*) { return CreateD3D12MotionVectorPass(); };
    }
    m_mvFixDisabled.store(false);
}

void FidelityFxInterceptor::DetachLiveHooks() {
    if (FidelityFxInterceptor* host = s_host.load()) host->DetachAll();
}

bool FidelityFxInterceptor::IsAttached() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_isAttached;
}

VRBridge* FidelityFxInterceptor::GetVRBridge() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_vrBridge;
}

void FidelityFxInterceptor::SetVRBridge(VRBridge* bridge) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_vrBridge = bridge;
}

size_t FidelityFxInterceptor::GetActiveVirtualContextCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_virtualContexts.size();
}

VirtualFfxContext* FidelityFxInterceptor::GetVirtualContext(ffxContext context) const {
    if (!context) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_virtualContexts.find(context);
    return it != m_virtualContexts.end() ? it->second.get() : nullptr;
}

bool FidelityFxInterceptor::IsVirtualContext(ffxContext context) const {
    VirtualFfxContext* virtualContext = GetVirtualContext(context);
    return virtualContext && virtualContext->magic == VIRTUAL_FFX_CONTEXT_MAGIC;
}

void FidelityFxInterceptor::ArmDiagnostics(uint32_t dispatches) {
    dispatches = (std::min)(dispatches, kMaxDiagDispatches);
    {
        std::lock_guard<std::mutex> lock(m_diagMutex);
        m_diagSamples[0].clear();
        m_diagSamples[1].clear();
        m_diagLastContext = -1;
        m_diagContextRun = 0;
        m_diagLongestContextRun = 0;
    }
    m_diagRemaining.store(dispatches);
    LogInfo("FidelityFxInterceptor: [Diag] Armed for " + std::to_string(dispatches) + " dispatches.");
}

FidelityFxInterceptor::JitterSummary FidelityFxInterceptor::Summarize(const std::vector<DiagSample>& samples) {
    JitterSummary summary;
    std::vector<std::pair<float, float>> distinct;
    for (const auto& s : samples) {
        if (summary.dispatches == 0) {
            summary.minX = summary.maxX = s.jitterX;
            summary.minY = summary.maxY = s.jitterY;
        }
        summary.dispatches++;
        if (s.reset) summary.resets++;
        summary.minX = (std::min)(summary.minX, s.jitterX);
        summary.maxX = (std::max)(summary.maxX, s.jitterX);
        summary.minY = (std::min)(summary.minY, s.jitterY);
        summary.maxY = (std::max)(summary.maxY, s.jitterY);
        bool seen = std::any_of(distinct.begin(), distinct.end(), [&](const auto& d) {
            return std::fabs(d.first - s.jitterX) < kJitterMatchTolerance &&
                   std::fabs(d.second - s.jitterY) < kJitterMatchTolerance;
        });
        if (!seen) distinct.emplace_back(s.jitterX, s.jitterY);
    }
    summary.distinctOffsets = static_cast<uint32_t>(distinct.size());
    return summary;
}

FidelityFxInterceptor::JitterSummary FidelityFxInterceptor::GetDiagJitterSummary(uint32_t contextIndex) const {
    std::lock_guard<std::mutex> lock(m_diagMutex);
    return Summarize(m_diagSamples[contextIndex == 1 ? 1 : 0]);
}

uint32_t FidelityFxInterceptor::GetDiagLongestContextRun() const {
    std::lock_guard<std::mutex> lock(m_diagMutex);
    return m_diagLongestContextRun;
}

void FidelityFxInterceptor::RecordDiagnostics(uint64_t dispatchIndex, uint32_t contextIndex,
                                              const ffxDispatchDescUpscale& desc, bool reset, bool isDualPass) {
    uint32_t remaining = m_diagRemaining.load();
    while (remaining > 0 && !m_diagRemaining.compare_exchange_weak(remaining, remaining - 1)) {}
    if (remaining == 0) return;

    // The engine may request a reset itself, not only our Discontinuity Flush.
    if (desc.reset) reset = true;

    VRBridge* bridge = GetVRBridge();
    uint32_t ctx = (contextIndex == 1) ? 1 : 0;
    {
        std::lock_guard<std::mutex> lock(m_diagMutex);
        m_diagSamples[ctx].push_back({desc.jitterOffset.x, desc.jitterOffset.y, reset});
        m_diagContextRun = (m_diagLastContext == static_cast<int>(ctx)) ? m_diagContextRun + 1 : 1;
        m_diagLongestContextRun = (std::max)(m_diagLongestContextRun, m_diagContextRun);
        m_diagLastContext = static_cast<int>(ctx);
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(4)
       << "FidelityFxInterceptor: [Diag] Dispatch #" << dispatchIndex << " Ctx=" << ctx
       << " Mode=" << (isDualPass ? "SDP" : "AFR")
       << " SubPass=" << (bridge ? bridge->GetSubPassIndex() : 0)
       << " Eye=" << (bridge ? bridge->GetActiveEye() : 0)
       << " Jitter=(" << desc.jitterOffset.x << ", " << desc.jitterOffset.y << ")"
       << " MVScale=(" << desc.motionVectorScale.x << ", " << desc.motionVectorScale.y << ")"
       << " Reset=" << (reset ? 1 : 0)
       << " Render=" << desc.renderSize.width << "x" << desc.renderSize.height
       << " Thread=" << GetCurrentThreadId();
    LogInfo(ss.str());

    if (remaining == 1) {
        LogDiagSummary();
    }
}

void FidelityFxInterceptor::LogDiagSummary() {
    std::lock_guard<std::mutex> lock(m_diagMutex);
    for (uint32_t ctx = 0; ctx < 2; ++ctx) {
        const auto& samples = m_diagSamples[ctx];
        JitterSummary summary = Summarize(samples);

        std::stringstream ss;
        ss << std::fixed << std::setprecision(4)
           << "FidelityFxInterceptor: [Diag] Summary Ctx=" << ctx << " Dispatches=" << summary.dispatches
           << " Resets=" << summary.resets << " DistinctJitter=" << summary.distinctOffsets
           << " JitterX=[" << summary.minX << ", " << summary.maxX << "]"
           << " JitterY=[" << summary.minY << ", " << summary.maxY << "] Offsets:";
        std::vector<std::pair<float, float>> listed;
        for (const auto& s : samples) {
            if (listed.size() >= kMaxListedJitterOffsets) break;
            bool seen = std::any_of(listed.begin(), listed.end(), [&](const auto& d) {
                return std::fabs(d.first - s.jitterX) < kJitterMatchTolerance &&
                       std::fabs(d.second - s.jitterY) < kJitterMatchTolerance;
            });
            if (!seen) {
                listed.emplace_back(s.jitterX, s.jitterY);
                ss << " (" << s.jitterX << ", " << s.jitterY << ")";
            }
        }
        LogInfo(ss.str());
    }
    LogInfo("FidelityFxInterceptor: [Diag] Longest same-context run=" + std::to_string(m_diagLongestContextRun) +
            " (1 means the contexts strictly alternated).");
}

void FidelityFxInterceptor::TriggerDiscontinuityFlush() {
    VRBridge* bridge = GetVRBridge();
    if (bridge) {
        bridge->SignalDiscontinuity();
    }
    m_internalDiscontinuityPendingLeft.store(true);
    m_internalDiscontinuityPendingRight.store(true);
}

bool FidelityFxInterceptor::ConsumeDiscontinuity(uint32_t contextIndex) {
    VRBridge* bridge = GetVRBridge();
    if (bridge) {
        return bridge->ConsumeDiscontinuity(contextIndex);
    }
    return (contextIndex == 1) ? m_internalDiscontinuityPendingRight.exchange(false)
                               : m_internalDiscontinuityPendingLeft.exchange(false);
}

ffxReturnCode_t FidelityFxInterceptor::Intercept_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                               const ffxAllocationCallbacks* memCb) {
    FidelityFxInterceptor* host = Host();
    if (!host) {
        LogError("FidelityFxInterceptor: No interceptor owns the ffxCreateContext detour");
        return FFX_API_RETURN_ERROR;
    }
    auto origCreate = Original<PFN_ffxCreateContext>(host->m_detourCreate);
    if (!origCreate) {
        LogError("FidelityFxInterceptor: Original ffxCreateContext is null");
        return FFX_API_RETURN_ERROR;
    }

    auto* upscale = (context && desc)
        ? FindInChain<ffxCreateContextDescUpscale>(desc, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE)
        : nullptr;
    if (!upscale) {
        // Frame generation and every other context type: not ours to split.
        return origCreate(context, desc, memCb);
    }

    ffxContext left = nullptr;
    ffxReturnCode_t resLeft = origCreate(&left, desc, memCb);
    if (resLeft != FFX_API_RETURN_OK || !left) {
        LogError("FidelityFxInterceptor: Failed to create left upscaler context: " + std::to_string(resLeft));
        return resLeft != FFX_API_RETURN_OK ? resLeft : FFX_API_RETURN_ERROR;
    }

    ffxContext right = nullptr;
    ffxReturnCode_t resRight = origCreate(&right, desc, memCb);
    if (resRight != FFX_API_RETURN_OK || !right) {
        LogError("FidelityFxInterceptor: Failed to create right upscaler context: " + std::to_string(resRight) +
                 ". Rolling back left context.");
        auto origDestroy = Original<PFN_ffxDestroyContext>(host->m_detourDestroy);
        if (origDestroy) origDestroy(&left, memCb);
        *context = nullptr;
        return resRight != FFX_API_RETURN_OK ? resRight : FFX_API_RETURN_ERROR;
    }

    auto virtualContext = std::make_unique<VirtualFfxContext>();
    virtualContext->id = host->m_nextVirtualId.fetch_add(1);
    virtualContext->left = left;
    virtualContext->right = right;
    virtualContext->flags = upscale->flags;
    virtualContext->maxRenderSize = upscale->maxRenderSize;
    virtualContext->maxUpscaleSize = upscale->maxUpscaleSize;
    ffxContext handle = virtualContext.get();
    {
        std::lock_guard<std::recursive_mutex> lock(host->m_mutex);
        host->m_virtualContexts[handle] = std::move(virtualContext);
    }
    *context = handle;

    std::stringstream ss;
    ss << "FidelityFxInterceptor: Successfully virtualized FidelityFX upscaler context (Left=" << left
       << ", Right=" << right << ", Render=" << upscale->maxRenderSize.width << "x" << upscale->maxRenderSize.height
       << ", Upscale=" << upscale->maxUpscaleSize.width << "x" << upscale->maxUpscaleSize.height << ")";
    LogInfo(ss.str());
    std::stringstream diag;
    diag << "FidelityFxInterceptor: [Diag] Create flags=" << DescribeUpscaleCreateFlags(upscale->flags)
         << " (0x" << std::hex << upscale->flags << std::dec << ")";
    LogInfo(diag.str());
    host->ArmDiagnostics();
    return FFX_API_RETURN_OK;
}

ffxReturnCode_t FidelityFxInterceptor::Intercept_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb) {
    FidelityFxInterceptor* host = Host();
    if (!host) {
        LogError("FidelityFxInterceptor: No interceptor owns the ffxDestroyContext detour");
        return FFX_API_RETURN_ERROR;
    }
    auto origDestroy = Original<PFN_ffxDestroyContext>(host->m_detourDestroy);
    if (!origDestroy) {
        LogError("FidelityFxInterceptor: Original ffxDestroyContext is null");
        return FFX_API_RETURN_ERROR;
    }
    if (!context || !*context) {
        return origDestroy(context, memCb);
    }

    std::unique_ptr<VirtualFfxContext> virtualContext;
    {
        std::lock_guard<std::recursive_mutex> lock(host->m_mutex);
        auto it = host->m_virtualContexts.find(*context);
        if (it != host->m_virtualContexts.end()) {
            virtualContext = std::move(it->second);
            host->m_virtualContexts.erase(it);
        }
    }
    if (!virtualContext) {
        return origDestroy(context, memCb);
    }

    ffxReturnCode_t resLeft = FFX_API_RETURN_OK;
    ffxReturnCode_t resRight = FFX_API_RETURN_OK;
    if (virtualContext->left) resLeft = origDestroy(&virtualContext->left, memCb);
    if (virtualContext->right) resRight = origDestroy(&virtualContext->right, memCb);
    *context = nullptr;
    LogInfo("FidelityFxInterceptor: Successfully released virtual FidelityFX upscaler context.");
    return resLeft != FFX_API_RETURN_OK ? resLeft : resRight;
}

ffxReturnCode_t FidelityFxInterceptor::Intercept_Configure(ffxContext* context, const ffxConfigureDescHeader* desc) {
    FidelityFxInterceptor* host = Host();
    auto orig = host ? Original<PFN_ffxConfigure>(host->m_detourConfigure) : nullptr;
    if (!orig) return FFX_API_RETURN_ERROR;

    VirtualFfxContext* virtualContext = (context && *context) ? host->GetVirtualContext(*context) : nullptr;
    if (!virtualContext) return orig(context, desc);

    // Both eye contexts must hold the same settings.
    ffxContext left = virtualContext->left;
    ffxContext right = virtualContext->right;
    ffxReturnCode_t resLeft = orig(&left, desc);
    ffxReturnCode_t resRight = orig(&right, desc);
    return resLeft != FFX_API_RETURN_OK ? resLeft : resRight;
}

ffxReturnCode_t FidelityFxInterceptor::Intercept_Query(ffxContext* context, ffxQueryDescHeader* desc) {
    FidelityFxInterceptor* host = Host();
    auto orig = host ? Original<PFN_ffxQuery>(host->m_detourQuery) : nullptr;
    if (!orig) return FFX_API_RETURN_ERROR;

    VirtualFfxContext* virtualContext = (context && *context) ? host->GetVirtualContext(*context) : nullptr;
    if (!virtualContext) return orig(context, desc);

    // Both contexts share one description, so they answer alike.
    ffxContext left = virtualContext->left;
    return orig(&left, desc);
}

ffxReturnCode_t FidelityFxInterceptor::Intercept_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc) {
    FidelityFxInterceptor* host = Host();
    auto orig = host ? Original<PFN_ffxDispatch>(host->m_detourDispatch) : nullptr;
    if (!orig) {
        LogError("FidelityFxInterceptor: No original ffxDispatch");
        return FFX_API_RETURN_ERROR;
    }
    if (!context || !*context || !desc) return orig(context, desc);

    VirtualFfxContext* virtualContext = host->GetVirtualContext(*context);
    if (!virtualContext) return orig(context, desc);

    // Not an upscale dispatch: there is no eye to route and no history to reset.
    if (desc->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {
        ffxContext left = virtualContext->left;
        return orig(&left, desc);
    }

    VRBridge* bridge = host->GetVRBridge();

    // Mode transition tracking & discontinuity latching
    int currentMode = bridge ? static_cast<int>(bridge->GetRenderingMode()) : -1;
    int lastMode = host->m_lastRenderingMode.exchange(currentMode);
    if (lastMode != -1 && lastMode != currentMode) {
        host->TriggerDiscontinuityFlush();
        host->ArmDiagnostics();
    }
    bool isDualPass = bridge && bridge->GetRenderingMode() == VRRenderingMode::SequentialDualPass;

    ffxContext target = virtualContext->left;
    uint32_t contextIndex = 0;
    if (!(bridge && (bridge->Is2DViewActive() || bridge->IsMenuVisible()))) {
        uint32_t eyeIndex = bridge ? bridge->GetActiveEye() : 0;
        if (eyeIndex == 1 && virtualContext->right) {
            target = virtualContext->right;
            contextIndex = 1;
        }
    }
    auto* upscale = const_cast<ffxDispatchDescUpscale*>(reinterpret_cast<const ffxDispatchDescUpscale*>(desc));
    bool needReset = host->ConsumeDiscontinuity(contextIndex);
    bool wroteReset = false;
    if (needReset && !upscale->reset) {
        upscale->reset = true;
        wroteReset = true;
    }

    // The motion vector pass: FSR3 reads the Injector's texture for this one dispatch. Monoscopic presentation
    // (menus, 2D Floating Window Mode) keeps the engine's vectors.
    const bool bypass = bridge && (bridge->Is2DViewActive() || bridge->IsMenuVisible());
    ffxApiResource savedMv{};
    const char* mvState = "off";
    bool swappedMv = false;
    if (!bypass) {
        swappedMv = host->ApplyMvCorrection(*upscale, virtualContext->flags, contextIndex == 1 ? 1u : 0u,
                                            host->m_dispatchCount.load(), savedMv, mvState);
    }

    uint64_t index = host->m_dispatchCount.fetch_add(1);
    host->RecordDiagnostics(index, contextIndex, *upscale, needReset, isDualPass);
    (void)mvState; // named in the [Diag] MvCorrection line while a diagnostic window is armed

    ffxReturnCode_t res = orig(&target, desc);

    // Always undo our writes so a reused descriptor never carries our reset or texture into later dispatches.
    if (wroteReset) upscale->reset = false;
    if (swappedMv) upscale->motionVectors = savedMv;
    return res;
}

} // namespace fs25vr
