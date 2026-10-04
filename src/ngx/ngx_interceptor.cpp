#include "ngx_interceptor.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "luau/vr_bridge.h"
#include "proxy/dxgi_proxy.h"
#include "openxr/openxr_manager.h"
#include <cwctype>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace fs25vr {

namespace {
// The instance whose detours are live. The detour entry points are plain function pointers, so they
// resolve the interceptor that installed them here instead of reaching for the singleton.
std::atomic<NgxInterceptor*> s_featureHost{nullptr};
std::atomic<NgxInterceptor*> s_loadHookHost{nullptr};
} // namespace

NgxInterceptor* NgxInterceptor::FeatureHost() {
    return s_featureHost.load();
}

NgxInterceptor* NgxInterceptor::LoadHookHost() {
    return s_loadHookHost.load();
}

NgxInterceptor::NgxInterceptor() = default;

NgxInterceptor::~NgxInterceptor() {
    if (IsProcessTerminating()) return;
    DetachAll();
}

bool NgxInterceptor::MatchesNgxName(LPCWSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    std::wstring s(lpLibFileName);
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));

    // Explicitly reject child plugin DLLs (Super Resolution, Frame Generation,
    // Ray Reconstruction, Update service) to ensure we hook the core runtime / loader.
    if (s.find(L"dlss") != std::wstring::npos ||
        s.find(L"update") != std::wstring::npos) {
        return false;
    }

    auto pos = s.find_last_of(L"\\/");
    std::wstring filename = (pos == std::wstring::npos) ? s : s.substr(pos + 1);

    // Accept nvngx.dll (standard NGX loader) and _nvngx.dll (DriverStore core NGX implementation).
    return filename == L"nvngx.dll" || filename == L"_nvngx.dll" ||
           filename == L"nvngx" || filename == L"_nvngx";
}

bool NgxInterceptor::MatchesNgxName(LPCSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    std::string s(lpLibFileName);
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    // Explicitly reject child plugin DLLs (Super Resolution, Frame Generation,
    // Ray Reconstruction, Update service) to ensure we hook the core runtime / loader.
    if (s.find("dlss") != std::string::npos ||
        s.find("update") != std::string::npos) {
        return false;
    }

    auto pos = s.find_last_of("\\/");
    std::string filename = (pos == std::string::npos) ? s : s.substr(pos + 1);

    // Accept nvngx.dll (standard NGX loader) and _nvngx.dll (DriverStore core NGX implementation).
    return filename == "nvngx.dll" || filename == "_nvngx.dll" ||
           filename == "nvngx" || filename == "_nvngx";
}

bool NgxInterceptor::AttachDirect(
    PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreate,
    PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnRelease,
    PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluate,
    PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAlloc,
    PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroy,
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCaps
) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return AttachDirectInternal(pfnCreate, pfnRelease, pfnEvaluate, pfnAlloc, pfnDestroy, pfnGetCaps);
}

bool NgxInterceptor::AttachDirectInternal(
    PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreate,
    PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnRelease,
    PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluate,
    PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAlloc,
    PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroy,
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCaps
) {
    if (!pfnCreate || !pfnRelease) {
        LogError("NgxInterceptor: CreateFeature and ReleaseFeature function pointers must be non-null.");
        return false;
    }

    // Detach any previous attachment
    if (m_isAttached) {
        if (m_detourCreateFeature.IsInstalled()) m_detourCreateFeature.Uninstall();
        if (m_detourReleaseFeature.IsInstalled()) m_detourReleaseFeature.Uninstall();
        if (m_detourEvaluateFeature.IsInstalled()) m_detourEvaluateFeature.Uninstall();
        if (m_detourAllocateParameters.IsInstalled()) m_detourAllocateParameters.Uninstall();
        if (m_detourDestroyParameters.IsInstalled()) m_detourDestroyParameters.Uninstall();
        if (m_detourGetCapabilityParameters.IsInstalled()) m_detourGetCapabilityParameters.Uninstall();
        m_isAttached = false;
    }

    // Publish the host before any detour goes live: a game thread can enter a detour the moment it is
    // enabled, and an entry point with no host fails the call.
    NgxInterceptor* previousHost = s_featureHost.exchange(this);

    bool createInstalled = m_detourCreateFeature.Install(
        reinterpret_cast<void*>(pfnCreate),
        reinterpret_cast<void*>(&NgxInterceptor::Intercept_CreateFeature)
    );
    bool releaseInstalled = m_detourReleaseFeature.Install(
        reinterpret_cast<void*>(pfnRelease),
        reinterpret_cast<void*>(&NgxInterceptor::Intercept_ReleaseFeature)
    );

    if (!createInstalled || !releaseInstalled) {
        LogError("NgxInterceptor: Failed to install CreateFeature or ReleaseFeature detours.");
        if (m_detourCreateFeature.IsInstalled()) m_detourCreateFeature.Uninstall();
        if (m_detourReleaseFeature.IsInstalled()) m_detourReleaseFeature.Uninstall();
        NgxInterceptor* expectedHost = this;
        s_featureHost.compare_exchange_strong(expectedHost, previousHost);
        return false;
    }

    if (pfnEvaluate &&
        !m_detourEvaluateFeature.Install(reinterpret_cast<void*>(pfnEvaluate), reinterpret_cast<void*>(&NgxInterceptor::Intercept_EvaluateFeature))) {
        LogError("NgxInterceptor: Failed to install EvaluateFeature detour.");
    }
    if (pfnAlloc) {
        m_detourAllocateParameters.Install(reinterpret_cast<void*>(pfnAlloc), reinterpret_cast<void*>(&NgxInterceptor::Intercept_AllocateParameters));
    }
    if (pfnDestroy) {
        m_detourDestroyParameters.Install(reinterpret_cast<void*>(pfnDestroy), reinterpret_cast<void*>(&NgxInterceptor::Intercept_DestroyParameters));
    }
    if (pfnGetCaps) {
        m_detourGetCapabilityParameters.Install(reinterpret_cast<void*>(pfnGetCaps), reinterpret_cast<void*>(&NgxInterceptor::Intercept_GetCapabilityParameters));
    }

    m_isAttached = true;
    LogInfo("NgxInterceptor: Successfully attached NGX feature lifecycle detours.");
    return true;
}

VRBridge* NgxInterceptor::GetVRBridge() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_vrBridge;
}

void NgxInterceptor::SetVRBridge(VRBridge* bridge) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_vrBridge = bridge;
}

DxgiProxy* NgxInterceptor::GetDxgiProxy() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_dxgiProxy;
}

void NgxInterceptor::SetDxgiProxy(DxgiProxy* proxy) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_dxgiProxy = proxy;
}

OpenXrManager* NgxInterceptor::GetOpenXrManager() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_openXrManager) return m_openXrManager;
    OpenXrManager* proxyManager = m_dxgiProxy ? m_dxgiProxy->GetOpenXrManager() : nullptr;
    return (proxyManager && proxyManager->IsActive()) ? proxyManager : nullptr;
}

void NgxInterceptor::SetOpenXrManager(OpenXrManager* mgr) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_openXrManager = mgr;
}

void NgxInterceptor::GetTargetUpscalingDimensions(uint32_t& outWidth, uint32_t& outHeight) const {
    outWidth = 0;
    outHeight = 0;

    // Pass-Through Mode: an inactive proxy would report the mirror target resolution, not the headset's.
    DxgiProxy* proxy = GetDxgiProxy();
    if (proxy && proxy->IsActive()) {
        proxy->GetDecoupledTargetResolution(outWidth, outHeight);
        if (outWidth > 0 && outHeight > 0) {
            return;
        }
    }

    OpenXrManager* oxr = GetOpenXrManager();
    if (oxr && oxr->IsActive()) {
        if (oxr->HasEyeSwapchains()) {
            const auto& sc = oxr->GetEyeSwapchain(0);
            if (sc.width > 0 && sc.height > 0) {
                outWidth = sc.width;
                outHeight = sc.height;
                return;
            }
        }
        outWidth = oxr->GetRecommendedEyeWidth(0);
        outHeight = oxr->GetRecommendedEyeHeight(0);
        if (outWidth > 0 && outHeight > 0) {
            return;
        }
    }
}

bool NgxInterceptor::AttachModule(HMODULE hNgxModule) {
    if (!hNgxModule) return false;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    char modPath[MAX_PATH] = {0};
    GetModuleFileNameA(hNgxModule, modPath, MAX_PATH);

    auto pfnCreate = reinterpret_cast<PFN_NVSDK_NGX_D3D12_CreateFeature>(
        GetProcAddress(hNgxModule, "NVSDK_NGX_D3D12_CreateFeature"));
    auto pfnRelease = reinterpret_cast<PFN_NVSDK_NGX_D3D12_ReleaseFeature>(
        GetProcAddress(hNgxModule, "NVSDK_NGX_D3D12_ReleaseFeature"));
    auto pfnEval = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(
        GetProcAddress(hNgxModule, "NVSDK_NGX_D3D12_EvaluateFeature"));

    auto pfnAlloc = reinterpret_cast<PFN_NVSDK_NGX_D3D12_AllocateParameters>(
        GetProcAddress(hNgxModule, "NVSDK_NGX_D3D12_AllocateParameters"));
    auto pfnDestroy = reinterpret_cast<PFN_NVSDK_NGX_D3D12_DestroyParameters>(
        GetProcAddress(hNgxModule, "NVSDK_NGX_D3D12_DestroyParameters"));
    auto pfnGetCaps = reinterpret_cast<PFN_NVSDK_NGX_D3D12_GetCapabilityParameters>(
        GetProcAddress(hNgxModule, "NVSDK_NGX_D3D12_GetCapabilityParameters"));

    // Fall back to nvngx.dll if parameter management functions are not exported by this module (e.g. nvngx_dlss.dll)
    if (!pfnAlloc || !pfnDestroy || !pfnGetCaps) {
        HMODULE hNvngx = GetModuleHandleW(L"nvngx.dll");
        if (!hNvngx) {
            hNvngx = LoadLibraryW(L"nvngx.dll");
        }
        if (hNvngx) {
            if (!pfnAlloc) {
                pfnAlloc = reinterpret_cast<PFN_NVSDK_NGX_D3D12_AllocateParameters>(
                    GetProcAddress(hNvngx, "NVSDK_NGX_D3D12_AllocateParameters"));
            }
            if (!pfnDestroy) {
                pfnDestroy = reinterpret_cast<PFN_NVSDK_NGX_D3D12_DestroyParameters>(
                    GetProcAddress(hNvngx, "NVSDK_NGX_D3D12_DestroyParameters"));
            }
            if (!pfnGetCaps) {
                pfnGetCaps = reinterpret_cast<PFN_NVSDK_NGX_D3D12_GetCapabilityParameters>(
                    GetProcAddress(hNvngx, "NVSDK_NGX_D3D12_GetCapabilityParameters"));
            }
        }
    }

    if (m_isAttached) {
        // If already attached to feature functions, but parameter functions were missing, attach parameter detours now
        if (pfnAlloc && !m_detourAllocateParameters.IsInstalled()) {
            if (m_detourAllocateParameters.Install(reinterpret_cast<void*>(pfnAlloc), reinterpret_cast<void*>(&NgxInterceptor::Intercept_AllocateParameters))) {
                LogInfo("NgxInterceptor: Successfully attached deferred AllocateParameters detour.");
            }
        }
        if (pfnDestroy && !m_detourDestroyParameters.IsInstalled()) {
            if (m_detourDestroyParameters.Install(reinterpret_cast<void*>(pfnDestroy), reinterpret_cast<void*>(&NgxInterceptor::Intercept_DestroyParameters))) {
                LogInfo("NgxInterceptor: Successfully attached deferred DestroyParameters detour.");
            }
        }
        if (pfnGetCaps && !m_detourGetCapabilityParameters.IsInstalled()) {
            if (m_detourGetCapabilityParameters.Install(reinterpret_cast<void*>(pfnGetCaps), reinterpret_cast<void*>(&NgxInterceptor::Intercept_GetCapabilityParameters))) {
                LogInfo("NgxInterceptor: Successfully attached deferred GetCapabilityParameters detour.");
            }
        }
        return true;
    }

    if (!pfnCreate || !pfnRelease || !pfnEval) {
        LogWarn(std::string("NgxInterceptor: Module does not export D3D12 CreateFeature, ReleaseFeature, or EvaluateFeature: ") + modPath);
        return false;
    }

    bool ok = AttachDirectInternal(pfnCreate, pfnRelease, pfnEval, pfnAlloc, pfnDestroy, pfnGetCaps);
    if (ok) {
        m_attachedModule = hNgxModule;
        LogInfo(std::string("NgxInterceptor: Successfully attached to NGX module: ") + modPath);
    }
    return ok;
}

void NgxInterceptor::CheckAndAttachLoadedModule() {
    static const wchar_t* const s_candidates[] = {
        L"nvngx.dll",
        L"_nvngx.dll"
    };

    for (const auto* name : s_candidates) {
        HMODULE hMod = GetModuleHandleW(name);
        if (hMod) {
            AttachModule(hMod);
        }
    }
}

bool NgxInterceptor::InstallModuleLoadHooks() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_moduleHooksInstalled) return true;

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32) {
        LogError("NgxInterceptor: Failed to get handle for kernel32.dll");
        return false;
    }

    void* pfnLoadLibraryW = reinterpret_cast<void*>(GetProcAddress(hKernel32, "LoadLibraryW"));
    void* pfnLoadLibraryExW = reinterpret_cast<void*>(GetProcAddress(hKernel32, "LoadLibraryExW"));
    void* pfnLoadLibraryA = reinterpret_cast<void*>(GetProcAddress(hKernel32, "LoadLibraryA"));
    void* pfnLoadLibraryExA = reinterpret_cast<void*>(GetProcAddress(hKernel32, "LoadLibraryExA"));

    // Publish the host before any detour goes live. A LoadLibrary detour with no trampoline calls the
    // hooked LoadLibrary again and recurses until the stack overflows, so the detours must never see a
    // host other than the one whose trampolines they are. LoadLibrary can be hooked only once, so a
    // second interceptor stays out while another owns the hooks.
    NgxInterceptor* noHost = nullptr;
    if (!s_loadHookHost.compare_exchange_strong(noHost, this)) {
        LogError("NgxInterceptor: Another interceptor already owns the module load detours.");
        return false;
    }

    if (pfnLoadLibraryW) {
        m_detourLoadLibraryW.Install(pfnLoadLibraryW, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryW));
    }
    if (pfnLoadLibraryExW) {
        m_detourLoadLibraryExW.Install(pfnLoadLibraryExW, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryExW));
    }
    if (pfnLoadLibraryA) {
        m_detourLoadLibraryA.Install(pfnLoadLibraryA, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryA));
    }
    if (pfnLoadLibraryExA) {
        m_detourLoadLibraryExA.Install(pfnLoadLibraryExA, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryExA));
    }

    m_moduleHooksInstalled = true;
    LogInfo("NgxInterceptor: Module load detours installed.");
    return true;
}

void NgxInterceptor::UninstallModuleLoadHooks() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_moduleHooksInstalled) return;

    if (m_detourLoadLibraryExA.IsInstalled()) m_detourLoadLibraryExA.Uninstall();
    if (m_detourLoadLibraryA.IsInstalled()) m_detourLoadLibraryA.Uninstall();
    if (m_detourLoadLibraryExW.IsInstalled()) m_detourLoadLibraryExW.Uninstall();
    if (m_detourLoadLibraryW.IsInstalled()) m_detourLoadLibraryW.Uninstall();

    m_moduleHooksInstalled = false;
    NgxInterceptor* expectedLoadHost = this;
    s_loadHookHost.compare_exchange_strong(expectedLoadHost, nullptr);
    LogInfo("NgxInterceptor: Module load detours uninstalled.");
}

void NgxInterceptor::Detach() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_isAttached) return;

    if (m_detourCreateFeature.IsInstalled()) m_detourCreateFeature.Uninstall();
    if (m_detourReleaseFeature.IsInstalled()) m_detourReleaseFeature.Uninstall();
    if (m_detourEvaluateFeature.IsInstalled()) m_detourEvaluateFeature.Uninstall();
    if (m_detourAllocateParameters.IsInstalled()) m_detourAllocateParameters.Uninstall();
    if (m_detourDestroyParameters.IsInstalled()) m_detourDestroyParameters.Uninstall();
    if (m_detourGetCapabilityParameters.IsInstalled()) m_detourGetCapabilityParameters.Uninstall();

    m_attachedModule = nullptr;
    m_isAttached = false;
    NgxInterceptor* expectedFeatureHost = this;
    s_featureHost.compare_exchange_strong(expectedFeatureHost, nullptr);
    m_lastRenderingMode.store(-1);
    m_internalDiscontinuityPendingLeft.store(false);
    m_internalDiscontinuityPendingRight.store(false);
    LogInfo("NgxInterceptor: Feature detours detached.");
}

void NgxInterceptor::DetachAll() {
    Detach();
    UninstallModuleLoadHooks();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_virtualHandles.clear();
    m_trackedParameters.clear();
    m_vrBridge = nullptr;
    m_dxgiProxy = nullptr;
    m_openXrManager = nullptr;
    m_dlssPresetOverride = kDefaultDlssPresetOverride;
    m_sharpnessOverride = kDefaultSharpnessOverride;
    m_forceSharpnessOverride = kDefaultForceSharpnessOverride;
    m_mvScaleMultiplierX = kDefaultAfrMvScale;
    m_mvScaleMultiplierY = kDefaultAfrMvScale;
    m_lastRenderingMode.store(-1);
    m_internalDiscontinuityPendingLeft.store(false);
    m_internalDiscontinuityPendingRight.store(false);
}

void NgxInterceptor::DetachLiveHooks() {
    if (NgxInterceptor* host = s_featureHost.load()) host->DetachAll();
    if (NgxInterceptor* host = s_loadHookHost.load()) host->DetachAll();
}

bool NgxInterceptor::IsAttached() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_isAttached;
}

bool NgxInterceptor::AreModuleHooksInstalled() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_moduleHooksInstalled;
}

bool NgxInterceptor::IsSingleContextBypassActive() const {
    VRBridge* bridge = GetVRBridge();
    if (!bridge) return false;
    return bridge->Is2DViewActive() || bridge->IsMenuVisible();
}

uint32_t NgxInterceptor::GetActiveDualPassEyeIndex() const {
    VRBridge* bridge = GetVRBridge();
    return bridge ? bridge->GetSubPassIndex() : 0;
}

void NgxInterceptor::TriggerDiscontinuityFlush() {
    VRBridge* bridge = GetVRBridge();
    if (bridge) {
        bridge->SignalDiscontinuity();
    }
    m_internalDiscontinuityPendingLeft.store(true);
    m_internalDiscontinuityPendingRight.store(true);
}

bool NgxInterceptor::HasPendingDiscontinuity(uint32_t contextIndex) const {
    VRBridge* bridge = GetVRBridge();
    if (bridge) {
        return bridge->HasPendingDiscontinuity(contextIndex);
    }
    return (contextIndex == 1) ? m_internalDiscontinuityPendingRight.load()
                               : m_internalDiscontinuityPendingLeft.load();
}

bool NgxInterceptor::ConsumeDiscontinuity(uint32_t contextIndex) {
    VRBridge* bridge = GetVRBridge();
    if (bridge) {
        return bridge->ConsumeDiscontinuity(contextIndex);
    }
    if (contextIndex == 1) {
        return m_internalDiscontinuityPendingRight.exchange(false);
    } else {
        return m_internalDiscontinuityPendingLeft.exchange(false);
    }
}

size_t NgxInterceptor::GetActiveVirtualHandleCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_virtualHandles.size();
}

bool NgxInterceptor::IsVirtualHandle(const NVSDK_NGX_Handle* handle) const {
    if (!handle) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_virtualHandles.find(const_cast<NVSDK_NGX_Handle*>(handle));
    if (it != m_virtualHandles.end() && it->second) {
        return it->second->magic == VIRTUAL_NGX_HANDLE_MAGIC;
    }
    return false;
}

VirtualNgxHandle* NgxInterceptor::GetVirtualHandle(const NVSDK_NGX_Handle* handle) const {
    if (!handle) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_virtualHandles.find(const_cast<NVSDK_NGX_Handle*>(handle));
    if (it != m_virtualHandles.end()) {
        return it->second.get();
    }
    return nullptr;
}

void NgxInterceptor::RegisterVirtualHandle(std::unique_ptr<VirtualNgxHandle> handle) {
    if (!handle) return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    NVSDK_NGX_Handle* rawKey = static_cast<NVSDK_NGX_Handle*>(handle.get());
    m_virtualHandles[rawKey] = std::move(handle);
}

std::unique_ptr<VirtualNgxHandle> NgxInterceptor::UnregisterVirtualHandle(NVSDK_NGX_Handle* handle) {
    if (!handle) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_virtualHandles.find(handle);
    if (it != m_virtualHandles.end()) {
        auto result = std::move(it->second);
        m_virtualHandles.erase(it);
        return result;
    }
    return nullptr;
}

void NgxInterceptor::RegisterTrackedParameter(std::unique_ptr<NgxParameterImpl> param) {
    if (!param) return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    NVSDK_NGX_Parameter* rawKey = param.get();
    m_trackedParameters[rawKey] = std::move(param);
}

bool NgxInterceptor::UnregisterTrackedParameter(NVSDK_NGX_Parameter* param) {
    if (!param) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_trackedParameters.find(param);
    if (it != m_trackedParameters.end()) {
        m_trackedParameters.erase(it);
        return true;
    }
    return false;
}

// Intercept entry point: CreateFeature
NVSDK_NGX_Result NgxInterceptor::Intercept_CreateFeature(
    ID3D12GraphicsCommandList* InCmdList,
    NVSDK_NGX_Feature InFeatureID,
    NVSDK_NGX_Parameter* InParameters,
    NVSDK_NGX_Handle** OutHandle
) {
    if (!OutHandle) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    *OutHandle = nullptr;

    NgxInterceptor* host = FeatureHost();
    if (!host) {
        LogError("NgxInterceptor: No interceptor owns the CreateFeature detour");
        return NVSDK_NGX_Result_Fail;
    }
    auto& interceptor = *host;
    auto origCreate = interceptor.GetOriginalCreateFeature();
    if (!origCreate) {
        LogError("NgxInterceptor: Original CreateFeature is null");
        return NVSDK_NGX_Result_Fail;
    }

    if (!InParameters) {
        LogError("NgxInterceptor: InParameters is null");
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }

    // Step 0: Query decoupled target resolution and derive internal DLSS render resolution
    uint32_t decoupledW = 0, decoupledH = 0;
    interceptor.GetTargetUpscalingDimensions(decoupledW, decoupledH);

    int perfQualityInt = static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality);
    InParameters->Get(NVSDK_NGX_Parameter_PerfQualityValue, &perfQualityInt);
    auto quality = static_cast<NVSDK_NGX_PerfQuality_Value>(perfQualityInt);

    uint32_t effectiveTargetW = decoupledW;
    uint32_t effectiveTargetH = decoupledH;
    if (effectiveTargetW == 0 || effectiveTargetH == 0) {
        unsigned int outW = 0, outH = 0;
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_OutWidth, &outW)) && outW > 0 && outW <= 16384) {
            effectiveTargetW = outW;
        }
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_OutHeight, &outH)) && outH > 0 && outH <= 16384) {
            effectiveTargetH = outH;
        }
    }
    if (effectiveTargetW == 0 || effectiveTargetH == 0) {
        unsigned int pW = 0, pH = 0;
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Width, &pW)) && pW > 0 && pW <= 16384) {
            effectiveTargetW = pW;
        }
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Height, &pH)) && pH > 0 && pH <= 16384) {
            effectiveTargetH = pH;
        }
    }

    DlssRenderResolution renderRes = DeriveDlssRenderResolution(effectiveTargetW, effectiveTargetH, quality);
    if (renderRes.renderWidth == 0 || renderRes.renderHeight == 0) {
        unsigned int inW = 0, inH = 0;
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_InWidth, &inW)) && inW > 0 && inW <= 16384) {
            renderRes.renderWidth = inW;
        }
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_InHeight, &inH)) && inH > 0 && inH <= 16384) {
            renderRes.renderHeight = inH;
        }
    }
    if (renderRes.renderWidth == 0 || renderRes.renderHeight == 0) {
        unsigned int inW = 0, inH = 0;
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Width, &inW)) && inW > 0 && inW <= 16384) {
            renderRes.renderWidth = inW;
        }
        if (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Height, &inH)) && inH > 0 && inH <= 16384) {
            renderRes.renderHeight = inH;
        }
    }

    int presetOverride = interceptor.GetDlssPresetOverride();
    if (presetOverride > 0 && InParameters) {
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, presetOverride);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, presetOverride);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, presetOverride);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, presetOverride);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, presetOverride);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, presetOverride);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, presetOverride);
    }

    // Step 1: Clone parameters for isolated mirroring across both underlying features
    auto clonedParams = std::make_shared<NgxParameterImpl>();
    clonedParams->CopyFrom(InParameters);

    // Step 2: Create Left Feature Handle
    NVSDK_NGX_Handle* hLeft = nullptr;
    NVSDK_NGX_Result resLeft = origCreate(InCmdList, InFeatureID, InParameters, &hLeft);
    if (NVSDK_NGX_FAILED(resLeft) || !hLeft) {
        std::stringstream ss;
        ss << "NgxInterceptor: Failed to create left NGX feature: " << resLeft
           << " (0x" << std::hex << resLeft << std::dec << ") FeatureID=" << InFeatureID
           << " Target=[" << effectiveTargetW << "x" << effectiveTargetH << "]"
           << " Render=[" << renderRes.renderWidth << "x" << renderRes.renderHeight << "]";
        LogError(ss.str());
        return resLeft;
    }

    // Step 3: Create Right Feature Handle with cloned parameters
    NVSDK_NGX_Handle* hRight = nullptr;
    NVSDK_NGX_Parameter* rightParamsToPass = InParameters;
    NVSDK_NGX_Parameter* nativeRightParams = nullptr;
    auto origAlloc = interceptor.GetOriginalAllocateParameters();
    auto origDestroy = interceptor.GetOriginalDestroyParameters();

    if (origAlloc && origDestroy && NVSDK_NGX_SUCCEED(origAlloc(&nativeRightParams)) && nativeRightParams) {
        clonedParams->MirrorTo(nativeRightParams);
        rightParamsToPass = nativeRightParams;
    } else {
        rightParamsToPass = clonedParams.get();
    }

    NVSDK_NGX_Result resRight = origCreate(InCmdList, InFeatureID, rightParamsToPass, &hRight);

    if (nativeRightParams && origDestroy) {
        origDestroy(nativeRightParams);
        nativeRightParams = nullptr;
    }

    // Step 4: Handle Right Feature Creation Failure (Rollback Left to prevent leaks)
    if (NVSDK_NGX_FAILED(resRight) || !hRight) {
        std::stringstream ss;
        ss << "NgxInterceptor: Failed to create right NGX feature: " << resRight
           << " (0x" << std::hex << resRight << std::dec << ") FeatureID=" << InFeatureID
           << ". Rolling back left handle.";
        LogError(ss.str());
        auto origRelease = interceptor.GetOriginalReleaseFeature();
        if (origRelease && hLeft) {
            origRelease(hLeft);
        }
        return resRight;
    }

    // Step 5: Allocate VirtualNgxHandle wrapping both handles
    auto virtualHandle = std::make_unique<VirtualNgxHandle>();
    virtualHandle->Id = interceptor.m_nextVirtualId.fetch_add(1);
    virtualHandle->magic = VIRTUAL_NGX_HANDLE_MAGIC;
    virtualHandle->hLeft = hLeft;
    virtualHandle->hRight = hRight;
    virtualHandle->featureId = InFeatureID;
    virtualHandle->clonedParameters = clonedParams;
    virtualHandle->targetWidth = effectiveTargetW;
    virtualHandle->targetHeight = effectiveTargetH;
    virtualHandle->renderWidth = renderRes.renderWidth;
    virtualHandle->renderHeight = renderRes.renderHeight;
    virtualHandle->perfQuality = quality;

    VirtualNgxHandle* rawVirtual = virtualHandle.get();
    interceptor.RegisterVirtualHandle(std::move(virtualHandle));

    *OutHandle = static_cast<NVSDK_NGX_Handle*>(rawVirtual);
    std::stringstream ss;
    ss << "NgxInterceptor: Successfully virtualized NGX feature (FeatureID=" << InFeatureID
       << ", Left=" << hLeft << ", Right=" << hRight
       << ", Target=" << effectiveTargetW << "x" << effectiveTargetH
       << ", Render=" << renderRes.renderWidth << "x" << renderRes.renderHeight << ")";
    LogInfo(ss.str());
    return NVSDK_NGX_Result_Success;
}

// Intercept entry point: ReleaseFeature
NVSDK_NGX_Result NgxInterceptor::Intercept_ReleaseFeature(NVSDK_NGX_Handle* InHandle) {
    if (!InHandle) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }

    NgxInterceptor* host = FeatureHost();
    if (!host) {
        LogError("NgxInterceptor: No interceptor owns the ReleaseFeature detour");
        return NVSDK_NGX_Result_Fail;
    }
    auto& interceptor = *host;
    auto origRelease = interceptor.GetOriginalReleaseFeature();
    if (!origRelease) {
        LogError("NgxInterceptor: Original ReleaseFeature is null");
        return NVSDK_NGX_Result_Fail;
    }

    // Check if this is a registered virtual handle
    if (interceptor.IsVirtualHandle(InHandle)) {
        auto virtualHandle = interceptor.UnregisterVirtualHandle(InHandle);
        if (!virtualHandle) {
            LogError("NgxInterceptor: Virtual handle not found in registry.");
            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }

        NVSDK_NGX_Result resLeft = NVSDK_NGX_Result_Success;
        NVSDK_NGX_Result resRight = NVSDK_NGX_Result_Success;

        if (virtualHandle->hLeft) {
            resLeft = origRelease(virtualHandle->hLeft);
            virtualHandle->hLeft = nullptr;
        }
        if (virtualHandle->hRight) {
            resRight = origRelease(virtualHandle->hRight);
            virtualHandle->hRight = nullptr;
        }

        LogInfo("NgxInterceptor: Successfully released virtual NGX feature.");
        return (NVSDK_NGX_FAILED(resLeft)) ? resLeft : resRight;
    }

    // Non-virtual handle: forward directly to original
    return origRelease(InHandle);
}

// Intercept entry point: EvaluateFeature
NVSDK_NGX_Result NgxInterceptor::Intercept_EvaluateFeature(
    ID3D12GraphicsCommandList* InCmdList,
    const NVSDK_NGX_Handle* InFeatureHandle,
    const NVSDK_NGX_Parameter* InParameters,
    PFN_NVSDK_NGX_ProgressCallback InCallback
) {
    if (!InFeatureHandle) {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }

    NgxInterceptor* host = FeatureHost();
    if (!host) {
        LogError("NgxInterceptor: No interceptor owns the EvaluateFeature detour");
        return NVSDK_NGX_Result_Fail;
    }
    auto& interceptor = *host;
    auto origEval = interceptor.GetOriginalEvaluateFeature();
    if (!origEval) {
        LogError("NgxInterceptor: Original EvaluateFeature is null");
        return NVSDK_NGX_Result_Fail;
    }

    VRBridge* bridge = interceptor.GetVRBridge();

    // Mode transition tracking & discontinuity latching
    int currentMode = bridge ? static_cast<int>(bridge->GetRenderingMode()) : -1;
    int lastMode = interceptor.m_lastRenderingMode.exchange(currentMode);
    if (lastMode != -1 && lastMode != currentMode) {
        interceptor.TriggerDiscontinuityFlush();
    }

    bool isDualPass = (bridge && bridge->GetRenderingMode() == VRRenderingMode::SequentialDualPass);

    const NVSDK_NGX_Handle* target = nullptr;
    uint32_t contextIndex = 0;
    bool singleContextBypass = false;

    if (interceptor.IsVirtualHandle(InFeatureHandle)) {
        auto* vHandle = interceptor.GetVirtualHandle(InFeatureHandle);
        if (!vHandle) {
            target = InFeatureHandle;
            contextIndex = 0;
        } else if (interceptor.IsSingleContextBypassActive()) {
            target = vHandle->hLeft;
            contextIndex = 0;
            singleContextBypass = true;
        } else {
            uint32_t eyeIndex = isDualPass ? interceptor.GetActiveDualPassEyeIndex()
                                           : (bridge ? bridge->GetFrameParity() : 0);
            target = (eyeIndex == 1 && vHandle->hRight) ? vHandle->hRight : vHandle->hLeft;
            contextIndex = (eyeIndex == 1 && vHandle->hRight) ? 1 : 0;
        }

        if (!target) target = InFeatureHandle;
    } else {
        target = InFeatureHandle;
        contextIndex = 0;
    }

    // Check if a Discontinuity Flush is pending for this context
    bool needReset = interceptor.ConsumeDiscontinuity(contextIndex);

    int prevReset = 0;
    bool hadPrevReset = false;
    auto* mutableParams = const_cast<NVSDK_NGX_Parameter*>(InParameters);

    if (needReset && mutableParams) {
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_Reset, &prevReset))) {
            hadPrevReset = true;
        }
        mutableParams->Set(NVSDK_NGX_Parameter_Reset, 1);
    }

    int prevPreset = 0;
    bool hadPrevPreset = false;
    int presetOverride = interceptor.GetDlssPresetOverride();
    if (presetOverride > 0 && mutableParams) {
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &prevPreset)) && prevPreset > 0) {
            hadPrevPreset = true;
        } else {
            mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, presetOverride);
        }
        mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, presetOverride);
        mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, presetOverride);
        mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, presetOverride);
        mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, presetOverride);
        mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, presetOverride);
        mutableParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, presetOverride);
    }

    float prevSharpness = 0.0f;
    bool hadPrevSharpness = false;
    float sharpnessOverride = interceptor.GetSharpnessOverride();
    if (sharpnessOverride >= 0.0f && mutableParams) {
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_Sharpness, &prevSharpness)) && prevSharpness > 0.0f) {
            hadPrevSharpness = true;
            if (interceptor.IsForceSharpnessOverride()) {
                mutableParams->Set(NVSDK_NGX_Parameter_Sharpness, sharpnessOverride);
            }
        } else {
            mutableParams->Set(NVSDK_NGX_Parameter_Sharpness, sharpnessOverride);
        }
    }

    // Eye-stride compensation only applies when consecutive evaluations of a context
    // are two frames apart (AFR). SDP and the monoscopic single-context bypass are 1.0x.
    bool eyeStride = !isDualPass && !singleContextBypass;
    float prevScaleX = 1.0f, prevScaleY = 1.0f;
    float scaleMulX = eyeStride ? interceptor.GetMvScaleMultiplierX() : 1.0f;
    float scaleMulY = eyeStride ? interceptor.GetMvScaleMultiplierY() : 1.0f;
    bool wroteScale = false;
    if (mutableParams) {
        // An absent MV scale means 1.0 to DLSS, which is also what we restore it to.
        float scale = 1.0f;
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_MV_Scale_X, &scale))) prevScaleX = scale;
        scale = 1.0f;
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &scale))) prevScaleY = scale;
        if (scaleMulX != 1.0f || scaleMulY != 1.0f) {
            mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_X, prevScaleX * scaleMulX);
            mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y, prevScaleY * scaleMulY);
            wroteScale = true;
        }
    }

    uint64_t currentEval = interceptor.m_evalCount.fetch_add(1);
    if ((currentEval == 0 || (currentEval % 300 == 0)) && InParameters) {
        unsigned int inW = 0, inH = 0, outW = 0, outH = 0;
        InParameters->Get(NVSDK_NGX_Parameter_Width, &inW);
        InParameters->Get(NVSDK_NGX_Parameter_Height, &inH);
        InParameters->Get(NVSDK_NGX_Parameter_OutWidth, &outW);
        InParameters->Get(NVSDK_NGX_Parameter_OutHeight, &outH);

        float mvX = 1.0f, mvY = 1.0f, jitX = 0.0f, jitY = 0.0f, sharp = 0.0f;
        int hasMvX = (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvX))) ? 1 : 0;
        int hasMvY = (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvY))) ? 1 : 0;
        int hasJitX = (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitX))) ? 1 : 0;
        int hasJitY = (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitY))) ? 1 : 0;
        int hasSharp = (NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_Sharpness, &sharp))) ? 1 : 0;

        int curPreset = 0;
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, &curPreset);
        if (curPreset == 0) {
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, &curPreset);
        }

        ID3D12Resource* pColor = nullptr;
        ID3D12Resource* pDepth = nullptr;
        ID3D12Resource* pMv = nullptr;
        ID3D12Resource* pOut = nullptr;
        InParameters->Get(NVSDK_NGX_Parameter_Color, &pColor);
        InParameters->Get(NVSDK_NGX_Parameter_Depth, &pDepth);
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &pMv);
        InParameters->Get(NVSDK_NGX_Parameter_Output, &pOut);

        std::stringstream ss;
        ss << "NgxInterceptor: [Eval #" << currentEval << "] Ctx=" << contextIndex
           << " Target=" << target << " Dim=[" << inW << "x" << inH << " -> " << outW << "x" << outH << "]"
           << " MVScale=(" << mvX << ", " << mvY << ") [has=(" << hasMvX << "," << hasMvY << ")]"
           << " Jitter=(" << jitX << ", " << jitY << ") [has=(" << hasJitX << "," << hasJitY << ")]"
           << " Sharp=" << sharp << " [has=" << hasSharp << "] Preset=" << curPreset << " Reset=" << (needReset ? 1 : 0)
           << " Res=[Col=" << pColor << " Depth=" << pDepth << " MV=" << pMv << " Out=" << pOut << "]";
        LogInfo(ss.str());
    }

    NVSDK_NGX_Result res = origEval(InCmdList, target, InParameters, InCallback);

    if (needReset && mutableParams) {
        if (hadPrevReset) {
            mutableParams->Set(NVSDK_NGX_Parameter_Reset, prevReset);
        } else {
            mutableParams->Set(NVSDK_NGX_Parameter_Reset, 0);
        }
    }

    // Always undo our write so a reused parameter block never compounds the multiplier
    if (wroteScale) {
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_X, prevScaleX);
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y, prevScaleY);
    }

    return res;
}

// Intercept entry point: AllocateParameters
NVSDK_NGX_Result NgxInterceptor::Intercept_AllocateParameters(NVSDK_NGX_Parameter** OutParameters) {
    if (!OutParameters) return NVSDK_NGX_Result_FAIL_InvalidParameter;

    NgxInterceptor* host = FeatureHost();
    if (!host) return NVSDK_NGX_Result_Fail;
    auto& interceptor = *host;
    auto origAlloc = interceptor.GetOriginalAllocateParameters();
    if (origAlloc) {
        return origAlloc(OutParameters);
    }

    auto param = std::make_unique<NgxParameterImpl>();
    *OutParameters = param.get();
    interceptor.RegisterTrackedParameter(std::move(param));
    return NVSDK_NGX_Result_Success;
}

// Intercept entry point: GetCapabilityParameters
NVSDK_NGX_Result NgxInterceptor::Intercept_GetCapabilityParameters(NVSDK_NGX_Parameter** OutParameters) {
    if (!OutParameters) return NVSDK_NGX_Result_FAIL_InvalidParameter;

    NgxInterceptor* host = FeatureHost();
    if (!host) return NVSDK_NGX_Result_Fail;
    auto& interceptor = *host;
    auto origGetCaps = interceptor.GetOriginalGetCapabilityParameters();
    if (origGetCaps) {
        return origGetCaps(OutParameters);
    }

    auto param = std::make_unique<NgxParameterImpl>();
    *OutParameters = param.get();
    interceptor.RegisterTrackedParameter(std::move(param));
    return NVSDK_NGX_Result_Success;
}

// Intercept entry point: DestroyParameters
NVSDK_NGX_Result NgxInterceptor::Intercept_DestroyParameters(NVSDK_NGX_Parameter* InParameters) {
    if (!InParameters) return NVSDK_NGX_Result_FAIL_InvalidParameter;

    NgxInterceptor* host = FeatureHost();
    if (!host) return NVSDK_NGX_Result_Fail;
    auto& interceptor = *host;
    if (interceptor.UnregisterTrackedParameter(InParameters)) {
        return NVSDK_NGX_Result_Success;
    }

    auto origDestroy = interceptor.GetOriginalDestroyParameters();
    if (origDestroy) {
        return origDestroy(InParameters);
    }

    return NVSDK_NGX_Result_Success;
}

// Intercept entry point: LoadLibraryW
HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryW(LPCWSTR lpLibFileName) {
    NgxInterceptor* interceptor = LoadHookHost();
    // No trampoline means the hook is already gone, so the plain call cannot re-enter this detour.
    auto orig = interceptor ? Original<decltype(&LoadLibraryW)>(interceptor->m_detourLoadLibraryW) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName) : LoadLibraryW(lpLibFileName);

    if (interceptor && hMod && MatchesNgxName(lpLibFileName)) {
        interceptor->AttachModule(hMod);
    }
    return hMod;
}

// Intercept entry point: LoadLibraryExW
HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryExW)>(interceptor->m_detourLoadLibraryExW) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName, hFile, dwFlags) : LoadLibraryExW(lpLibFileName, hFile, dwFlags);

    if (interceptor && hMod && MatchesNgxName(lpLibFileName)) {
        interceptor->AttachModule(hMod);
    }
    return hMod;
}

// Intercept entry point: LoadLibraryA
HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryA(LPCSTR lpLibFileName) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryA)>(interceptor->m_detourLoadLibraryA) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName) : LoadLibraryA(lpLibFileName);

    if (interceptor && hMod && MatchesNgxName(lpLibFileName)) {
        interceptor->AttachModule(hMod);
    }
    return hMod;
}

// Intercept entry point: LoadLibraryExA
HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryExA)>(interceptor->m_detourLoadLibraryExA) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName, hFile, dwFlags) : LoadLibraryExA(lpLibFileName, hFile, dwFlags);

    if (interceptor && hMod && MatchesNgxName(lpLibFileName)) {
        interceptor->AttachModule(hMod);
    }
    return hMod;
}

} // namespace fs25vr
