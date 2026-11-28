#include "ngx_interceptor.h"
#include "core/logger.h"
#include "core/dll_lifecycle.h"
#include "luau/vr_bridge.h"
#include "proxy/dxgi_proxy.h"
#include "openxr/openxr_manager.h"
#include "fidelityfx/fidelityfx_interceptor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace fs25vr {

namespace {
struct TrackedNgxParamRegistry {
    std::mutex mutex;
    std::unordered_map<const NVSDK_NGX_Parameter*, const NgxParameterImpl*> instances;
};

TrackedNgxParamRegistry& NgxParamRegistry() {
    static auto* registry = new TrackedNgxParamRegistry();
    return *registry;
}

struct KnownNgxKey {
    const char* name;
    NgxParamType type;
};

constexpr KnownNgxKey kKnownNgxKeys[] = {
    {NVSDK_NGX_Parameter_Width, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_Height, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_OutWidth, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_OutHeight, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_PerfQualityValue, NgxParamType::Int},
    {NVSDK_NGX_Parameter_RTXValue, NgxParamType::Int},
    {NVSDK_NGX_Parameter_Reset, NgxParamType::Int},
    {NVSDK_NGX_Parameter_CreationNodeMask, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_VisibilityNodeMask, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_Color, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_Output, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_Depth, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_MotionVectors, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_Jitter_Offset_X, NgxParamType::Float},
    {NVSDK_NGX_Parameter_Jitter_Offset_Y, NgxParamType::Float},
    {NVSDK_NGX_Parameter_MV_Scale_X, NgxParamType::Float},
    {NVSDK_NGX_Parameter_MV_Scale_Y, NgxParamType::Float},
    {NVSDK_NGX_Parameter_MV_Offset_X, NgxParamType::Float},
    {NVSDK_NGX_Parameter_MV_Offset_Y, NgxParamType::Float},
    {NVSDK_NGX_Parameter_Sharpness, NgxParamType::Float},
    {NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, NgxParamType::Int},
    {NVSDK_NGX_Parameter_InWidth, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_InHeight, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Optimal_Render_Width, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Optimal_Render_Height, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_ExposureTexture, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_PreExposure, NgxParamType::Float},
    {NVSDK_NGX_Parameter_DLSS_Exposure_Texture, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_DLSS_Pre_Exposure, NgxParamType::Float},
    {NVSDK_NGX_Parameter_DLSS_Exposure, NgxParamType::Float},
    {NVSDK_NGX_Parameter_TonemapperType, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, NgxParamType::D3D12Resource},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, NgxParamType::Int},
    {NVSDK_NGX_Parameter_Resource_Width, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_Resource_Height, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, NgxParamType::Int},
    {NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, NgxParamType::Uint},
    {NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, NgxParamType::Uint}
};

bool IsDataOrImageResourceLoad(DWORD dwFlags, HMODULE hMod) {
    constexpr DWORD kDataFlags =
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if ((dwFlags & kDataFlags) != 0) return true;
    return (reinterpret_cast<uintptr_t>(hMod) & static_cast<uintptr_t>(3)) != 0;
}

struct SavedPresetKey {
    const char* name = nullptr;
    bool wrote = false;
    bool hadPrev = false;
    int prevVal = 0;
};

struct SavedPresetOverrides {
    SavedPresetKey keys[7] = {
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset},
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA},
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality},
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced},
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance},
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance},
        {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality}
    };

    void Apply(NVSDK_NGX_Parameter* params, int presetOverride, bool preserveExplicitBase) {
        if (presetOverride <= 0 || !params) return;
        for (size_t i = 0; i < std::size(keys); ++i) {
            auto& entry = keys[i];
            int prev = 0;
            if (NVSDK_NGX_SUCCEED(params->Get(entry.name, &prev))) {
                entry.hadPrev = true;
                entry.prevVal = prev;
                if (i == 0 && preserveExplicitBase && prev > 0) {
                    continue;
                }
            }
            params->Set(entry.name, presetOverride);
            entry.wrote = true;
        }
    }

    void Restore(NVSDK_NGX_Parameter* params) const {
        if (!params) return;
        for (const auto& entry : keys) {
            if (entry.wrote) {
                params->Set(entry.name, entry.hadPrev ? entry.prevVal : 0);
            }
        }
    }
};

std::atomic<NgxInterceptor*> s_featureHost{nullptr};
std::atomic<NgxInterceptor*> s_loadHookHost{nullptr};

constexpr struct { int bit; const char* name; } kDlssCreateFlagNames[] = {
    {1 << 0, "IsHDR"},
    {1 << 1, "MVLowRes"},
    {1 << 2, "MVJittered"},
    {1 << 3, "DepthInverted"},
    {1 << 5, "DoSharpening"},
    {1 << 6, "AutoExposure"},
    {1 << 7, "AlphaUpscaling"},
};

std::string DescribeDlssCreateFlags(int flags) {
    std::string names;
    for (const auto& flag : kDlssCreateFlagNames) {
        if (flags & flag.bit) {
            if (!names.empty()) names += "|";
            names += flag.name;
        }
    }
    return names.empty() ? "none" : names;
}

constexpr float kJitterMatchTolerance = 1e-4f;
// Distinct offsets listed per context in the summary; the count covers all of them.
constexpr size_t kMaxListedJitterOffsets = 16;
}

NgxParameterImpl::NgxParameterImpl() {
    auto& registry = NgxParamRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.instances[static_cast<const NVSDK_NGX_Parameter*>(this)] = this;
}

NgxParameterImpl::~NgxParameterImpl() {
    auto& registry = NgxParamRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.instances.erase(static_cast<const NVSDK_NGX_Parameter*>(this));
}

void NgxParameterImpl::CopyFrom(const NVSDK_NGX_Parameter* source) {
    if (!source || source == this) return;

    {
        auto& registry = NgxParamRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        auto it = registry.instances.find(source);
        if (it != registry.instances.end() && it->second) {
            m_params = it->second->m_params;
            return;
        }
    }

    m_params.clear();
    for (const auto& key : kKnownNgxKeys) {
        switch (key.type) {
            case NgxParamType::Float: {
                float fVal = 0.0f;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &fVal))) {
                    Set(key.name, fVal);
                    break;
                }
                double dVal = 0.0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &dVal))) {
                    Set(key.name, dVal);
                }
                break;
            }
            case NgxParamType::Double: {
                double dVal = 0.0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &dVal))) {
                    Set(key.name, dVal);
                    break;
                }
                float fVal = 0.0f;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &fVal))) {
                    Set(key.name, fVal);
                }
                break;
            }
            case NgxParamType::Int: {
                int iVal = 0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &iVal))) {
                    Set(key.name, iVal);
                    break;
                }
                unsigned int uVal = 0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &uVal))) {
                    Set(key.name, uVal);
                }
                break;
            }
            case NgxParamType::Uint: {
                unsigned long long ullVal = 0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &ullVal))) {
                    if (ullVal > (std::numeric_limits<unsigned int>::max)()) {
                        Set(key.name, ullVal);
                    } else {
                        Set(key.name, static_cast<unsigned int>(ullVal));
                    }
                    break;
                }
                unsigned int uVal = 0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &uVal))) {
                    Set(key.name, uVal);
                }
                break;
            }
            case NgxParamType::Ull: {
                unsigned long long ullVal = 0;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &ullVal))) {
                    Set(key.name, ullVal);
                }
                break;
            }
            case NgxParamType::D3D12Resource: {
                ID3D12Resource* res12 = nullptr;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &res12))) {
                    Set(key.name, res12);
                    break;
                }
                ID3D11Resource* res11 = nullptr;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &res11))) {
                    Set(key.name, res11);
                    break;
                }
                void* pVal = nullptr;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &pVal))) {
                    Set(key.name, pVal);
                }
                break;
            }
            case NgxParamType::D3D11Resource: {
                ID3D11Resource* res11 = nullptr;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &res11))) {
                    Set(key.name, res11);
                    break;
                }
                void* pVal = nullptr;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &pVal))) {
                    Set(key.name, pVal);
                }
                break;
            }
            case NgxParamType::VoidPtr: {
                void* pVal = nullptr;
                if (NVSDK_NGX_SUCCEED(source->Get(key.name, &pVal))) {
                    Set(key.name, pVal);
                }
                break;
            }
        }
    }
}

NgxInterceptor* NgxInterceptor::FeatureHost() {
    return s_featureHost.load();
}

NgxInterceptor* NgxInterceptor::LoadHookHost() {
    return s_loadHookHost.load();
}

NgxInterceptor::NgxInterceptor() {
    m_mvPassFactory = [](ID3D12GraphicsCommandList*) { return CreateD3D12MotionVectorPass(); };
}

NgxInterceptor::~NgxInterceptor() {
    if (IsProcessTerminating()) {
        (void)m_mvPass.release();
        return;
    }
    DetachAll();
}

bool NgxInterceptor::MatchesNgxName(LPCWSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    std::wstring s(lpLibFileName);
    auto pos = s.find_last_of(L"\\/");
    std::wstring filename = (pos == std::wstring::npos) ? s : s.substr(pos + 1);
    for (auto& c : filename) c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));

    return filename == L"nvngx.dll" || filename == L"_nvngx.dll" ||
           filename == L"nvngx" || filename == L"_nvngx";
}

bool NgxInterceptor::MatchesNgxName(LPCSTR lpLibFileName) {
    if (!lpLibFileName) return false;
    std::string s(lpLibFileName);
    auto pos = s.find_last_of("\\/");
    std::string filename = (pos == std::string::npos) ? s : s.substr(pos + 1);
    for (auto& c : filename) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

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

    if (m_isAttached) {
        if (m_detourCreateFeature.IsInstalled()) m_detourCreateFeature.Uninstall();
        if (m_detourReleaseFeature.IsInstalled()) m_detourReleaseFeature.Uninstall();
        if (m_detourEvaluateFeature.IsInstalled()) m_detourEvaluateFeature.Uninstall();
        if (m_detourAllocateParameters.IsInstalled()) m_detourAllocateParameters.Uninstall();
        if (m_detourDestroyParameters.IsInstalled()) m_detourDestroyParameters.Uninstall();
        if (m_detourGetCapabilityParameters.IsInstalled()) m_detourGetCapabilityParameters.Uninstall();
        m_isAttached = false;
    }

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
    if (pfnAlloc &&
        !m_detourAllocateParameters.Install(reinterpret_cast<void*>(pfnAlloc), reinterpret_cast<void*>(&NgxInterceptor::Intercept_AllocateParameters))) {
        LogError("NgxInterceptor: Failed to install AllocateParameters detour.");
    }
    if (pfnDestroy &&
        !m_detourDestroyParameters.Install(reinterpret_cast<void*>(pfnDestroy), reinterpret_cast<void*>(&NgxInterceptor::Intercept_DestroyParameters))) {
        LogError("NgxInterceptor: Failed to install DestroyParameters detour.");
    }
    if (pfnGetCaps &&
        !m_detourGetCapabilityParameters.Install(reinterpret_cast<void*>(pfnGetCaps), reinterpret_cast<void*>(&NgxInterceptor::Intercept_GetCapabilityParameters))) {
        LogError("NgxInterceptor: Failed to install GetCapabilityParameters detour.");
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
    if (!hNgxModule || (reinterpret_cast<uintptr_t>(hNgxModule) & static_cast<uintptr_t>(3)) != 0) {
        return false;
    }

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

    if (!pfnAlloc || !pfnDestroy || !pfnGetCaps) {
        HMODULE hNvngx = GetModuleHandleW(L"nvngx.dll");
        if (!hNvngx) {
            decltype(&LoadLibraryW) origLoadW = nullptr;
            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                origLoadW = Original<decltype(&LoadLibraryW)>(m_detourLoadLibraryW);
            }
            hNvngx = origLoadW ? origLoadW(L"nvngx.dll") : LoadLibraryW(L"nvngx.dll");
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

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_isAttached) {
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

FidelityFxInterceptor* NgxInterceptor::GetFidelityFxInterceptor() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_fidelityFx;
}

void NgxInterceptor::SetFidelityFxInterceptor(FidelityFxInterceptor* interceptor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fidelityFx = interceptor;
}

void NgxInterceptor::NotifyModuleLoaded(HMODULE hMod, LPCWSTR name) {
    FidelityFxInterceptor* ffx = GetFidelityFxInterceptor();
    if (ffx && FidelityFxInterceptor::MatchesLoaderName(name)) ffx->AttachModule(hMod);
}

void NgxInterceptor::NotifyModuleLoaded(HMODULE hMod, LPCSTR name) {
    FidelityFxInterceptor* ffx = GetFidelityFxInterceptor();
    if (ffx && FidelityFxInterceptor::MatchesLoaderName(name)) ffx->AttachModule(hMod);
}

void NgxInterceptor::CheckAndAttachLoadedModule() {
    if (FidelityFxInterceptor* ffx = GetFidelityFxInterceptor()) ffx->CheckAndAttachLoadedModule();

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

    if (!pfnLoadLibraryW || !pfnLoadLibraryExW || !pfnLoadLibraryA || !pfnLoadLibraryExA) {
        LogError("NgxInterceptor: Failed to resolve kernel32 LoadLibrary entry points.");
        return false;
    }

    NgxInterceptor* noHost = nullptr;
    if (!s_loadHookHost.compare_exchange_strong(noHost, this)) {
        LogError("NgxInterceptor: Another interceptor already owns the module load detours.");
        return false;
    }

    bool ok =
        m_detourLoadLibraryW.Install(pfnLoadLibraryW, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryW)) &&
        m_detourLoadLibraryExW.Install(pfnLoadLibraryExW, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryExW)) &&
        m_detourLoadLibraryA.Install(pfnLoadLibraryA, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryA)) &&
        m_detourLoadLibraryExA.Install(pfnLoadLibraryExA, reinterpret_cast<void*>(&NgxInterceptor::Intercept_LoadLibraryExA));

    if (!ok) {
        LogError("NgxInterceptor: Failed to install one or more module load detours.");
        if (m_detourLoadLibraryExA.IsInstalled()) m_detourLoadLibraryExA.Uninstall();
        if (m_detourLoadLibraryA.IsInstalled()) m_detourLoadLibraryA.Uninstall();
        if (m_detourLoadLibraryExW.IsInstalled()) m_detourLoadLibraryExW.Uninstall();
        if (m_detourLoadLibraryW.IsInstalled()) m_detourLoadLibraryW.Uninstall();
        NgxInterceptor* expectedLoadHost = this;
        s_loadHookHost.compare_exchange_strong(expectedLoadHost, nullptr);
        return false;
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
    m_sdpMvScaleMultiplierX = 1.0f;
    m_sdpMvScaleMultiplierY = 1.0f;
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
    m_mvFixEnabled.store(kDefaultMvFixEnabled);
    m_mvFixDisabled.store(false);
    m_depthModelInfinite.store(true);
    m_nearClipOverride.store(0.0f);
    ForgetNearEstimate();
}

void NgxInterceptor::ForgetNearEstimate() {
    m_nearEstimator.Reset(m_evalCount.load());
    m_nearCrossEyeEvals.store(0);
    m_nearLastLogged.store(0.0f);
    m_nearLastLogMs.store(INT64_MIN);
}

void NgxInterceptor::OnNearSolution(uint64_t evalIndex, const mvreproj::NearSolution& solution, float recordNear,
                                    uint32_t eye, double gatedFraction) {
    m_nearEstimator.Submit(evalIndex, solution);
    const float estimate = m_nearEstimator.Estimate();
    if (!(estimate > 0.0f)) return;
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t lastMs = m_nearLastLogMs.load();
    const float last = m_nearLastLogged.load();
    const bool due = lastMs == INT64_MIN || nowMs - lastMs >= 2000;
    if (!due || (last > 0.0f && std::fabs(estimate - last) <= 0.05f * last)) return;
    m_nearLastLogMs.store(nowMs);
    m_nearLastLogged.store(estimate);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(3) << "NgxInterceptor: auto near " << estimate << " m (record "
       << recordNear << ", Eye=" << eye << " inliers=" << std::setprecision(1) << solution.inlierFraction * 100.0
       << "% (" << solution.used << " of " << solution.retained << ") gated=" << gatedFraction * 100.0 << "%)";
    LogInfo(ss.str());
}

void NgxInterceptor::SetNearClipOverride(float meters) {
    if (!std::isfinite(meters) || meters < 0.0f) meters = 0.0f;
    if (m_nearClipOverride.exchange(meters) != meters) {
        LogInfo("NgxInterceptor: Near plane (vrDlssNearClip*) " +
                (meters > 0.0f ? std::to_string(meters) + " m" : std::string("auto (estimated, camera record until then)")));
    }
}

void NgxInterceptor::SetDepthModelInfinite(bool infinite) {
    if (m_depthModelInfinite.exchange(infinite) != infinite) {
        LogInfo(std::string("NgxInterceptor: Depth model (vrDlssDepthModel) ") + (infinite ? "infinite" : "finite"));
    }
}

bool NgxInterceptor::SetMvFixEnabled(bool enabled) {
    if (enabled && m_mvFixDisabled.load()) {
        LogWarn("NgxInterceptor: vrDlssMvFix on ignored; the pass is disabled for this session (" + GetMvFixDisableReason() + ")");
        return false;
    }
    bool previous = m_mvFixEnabled.exchange(enabled);
    if (previous != enabled) {
        LogInfo(std::string("NgxInterceptor: Motion vector pass (vrDlssMvFix) ") + (enabled ? "on" : "off"));
    }
    return enabled;
}

std::string NgxInterceptor::GetMvFixDisableReason() const {
    std::lock_guard<std::mutex> lock(m_mvFixMutex);
    return m_mvFixDisableReason;
}

void NgxInterceptor::SetMotionVectorPassFactory(MotionVectorPassFactory factory) {
    std::lock_guard<std::mutex> lock(m_mvFixMutex);
    m_mvPass.reset();
    m_mvPassFactory = std::move(factory);
}

const char* NgxInterceptor::DescribeMvFixState() const {
    if (m_mvFixDisabled.load()) return "failed";
    return m_mvFixEnabled.load() ? "on" : "off";
}

void NgxInterceptor::DisableMvFix(const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(m_mvFixMutex);
        if (m_mvFixDisabled.load()) return;
        m_mvFixDisableReason = reason;
    }
    m_mvFixDisabled.store(true);
    m_mvFixEnabled.store(false);
    LogError("NgxInterceptor: Motion vector pass disabled for this session: " + reason +
             ". DLSS continues on the engine's motion vectors.");
}

ID3D12Resource* NgxInterceptor::RecordMvPass(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth, ID3D12Resource* mv,
                                             const MvSelfCheckRequest* request,
                                             std::vector<MvSelfCheckReport>* reports,
                                             const MvCorrectionRequest* correction) {
    std::unique_lock<std::mutex> lock(m_mvFixMutex);
    if (m_mvFixDisabled.load()) return nullptr;
    if (!m_mvPass) {
        if (m_mvPassFactory) m_mvPass = m_mvPassFactory(cmdList);
        if (!m_mvPass) {
            lock.unlock();
            DisableMvFix("the motion vector pass could not be created");
            return nullptr;
        }
    }
    MvPassInputs inputs;
    inputs.cmdList = cmdList;
    inputs.depth = depth;
    inputs.motionVectors = mv;
    inputs.selfCheck = request;
    inputs.correction = correction;
    MvPassResult result = m_mvPass->Record(inputs);
    if (!result.ok || !result.output) {
        lock.unlock();
        DisableMvFix(result.failureReason.empty() ? std::string("the pass reported a failure") : result.failureReason);
        return nullptr;
    }
    if (reports) *reports = m_mvPass->PollSelfCheck();
    return result.output;
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
    return bridge ? bridge->GetActiveEye() : 0;
}

void NgxInterceptor::ArmDiagnostics(uint32_t evaluations) {
    evaluations = (std::min)(evaluations, kMaxDiagEvaluations);
    {
        std::lock_guard<std::mutex> lock(m_diagMutex);
        m_diagSamples[0].clear();
        m_diagSamples[1].clear();
        m_diagLastContext = -1;
        m_diagContextRun = 0;
        m_diagLongestContextRun = 0;
    }
    m_diagRawRangeLogged.store(false);
    m_diagProjectionLogged.store(false);
    m_diagRemaining.store(evaluations);
    LogInfo("NgxInterceptor: [Diag] Armed for " + std::to_string(evaluations) + " evaluations.");
}

NgxInterceptor::JitterSummary NgxInterceptor::Summarize(const std::vector<DiagSample>& samples) {
    JitterSummary summary;
    std::vector<std::pair<float, float>> distinct;
    for (const auto& s : samples) {
        if (summary.evaluations == 0) {
            summary.minX = summary.maxX = s.jitterX;
            summary.minY = summary.maxY = s.jitterY;
        }
        summary.evaluations++;
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

NgxInterceptor::JitterSummary NgxInterceptor::GetDiagJitterSummary(uint32_t contextIndex) const {
    std::lock_guard<std::mutex> lock(m_diagMutex);
    return Summarize(m_diagSamples[contextIndex == 1 ? 1 : 0]);
}

void NgxInterceptor::RecordDiagnostics(uint64_t evalIndex, uint32_t contextIndex,
                                       const NVSDK_NGX_Parameter* params, bool reset, bool isDualPass,
                                       const ResolvedCameras* resolvedCameras) {
    uint32_t remaining = m_diagRemaining.load();
    while (remaining > 0 && !m_diagRemaining.compare_exchange_weak(remaining, remaining - 1)) {}
    if (remaining == 0) return;

    float jitX = 0.0f, jitY = 0.0f, mvX = 1.0f, mvY = 1.0f;
    unsigned int renderW = 0, renderH = 0;
    if (params) {
        int resetParam = 0;
        if (NVSDK_NGX_SUCCEED(params->Get(NVSDK_NGX_Parameter_Reset, &resetParam)) && resetParam != 0) {
            reset = true;
        }
        params->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitX);
        params->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitY);
        params->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvX);
        params->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvY);
        if (NVSDK_NGX_FAILED(params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &renderW)) ||
            NVSDK_NGX_FAILED(params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &renderH))) {
            params->Get(NVSDK_NGX_Parameter_Width, &renderW);
            params->Get(NVSDK_NGX_Parameter_Height, &renderH);
        }
    }

    VRBridge* bridge = GetVRBridge();
    uint32_t ctx = (contextIndex == 1) ? 1 : 0;
    {
        std::lock_guard<std::mutex> lock(m_diagMutex);
        m_diagSamples[ctx].push_back({jitX, jitY, reset});
        m_diagContextRun = (m_diagLastContext == static_cast<int>(ctx)) ? m_diagContextRun + 1 : 1;
        m_diagLongestContextRun = (std::max)(m_diagLongestContextRun, m_diagContextRun);
        m_diagLastContext = static_cast<int>(ctx);
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(4)
       << "NgxInterceptor: [Diag] Eval #" << evalIndex << " Ctx=" << ctx
       << " Mode=" << (isDualPass ? "SDP" : "AFR")
       << " SubPass=" << (bridge ? bridge->GetSubPassIndex() : 0)
       << " Eye=" << (bridge ? bridge->GetActiveEye() : 0)
       << " Jitter=(" << jitX << ", " << jitY << ")"
       << " MVScale=(" << mvX << ", " << mvY << ")"
       << " MvFix=" << DescribeMvFixState()
       << " Reset=" << (reset ? 1 : 0)
       << " Render=" << renderW << "x" << renderH
       << " Thread=" << GetCurrentThreadId();
    if (bridge) {
        if (resolvedCameras) {
            ss << " " << DescribeResolvedCameras(*resolvedCameras);
        } else {
            ss << " " << DescribeResolvedCameras(bridge->GetCameraRecords().Resolve(bridge->GetActiveEye()));
        }
    }
    LogInfo(ss.str());

    if (remaining == 1) {
        LogDiagSummary();
    }
}

void NgxInterceptor::LogDiagSummary() {
    std::lock_guard<std::mutex> lock(m_diagMutex);
    for (uint32_t ctx = 0; ctx < 2; ++ctx) {
        const auto& samples = m_diagSamples[ctx];
        JitterSummary summary = Summarize(samples);

        std::stringstream ss;
        ss << std::fixed << std::setprecision(4)
           << "NgxInterceptor: [Diag] Summary Ctx=" << ctx << " Evals=" << summary.evaluations
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
    LogInfo("NgxInterceptor: [Diag] Longest same-context run=" + std::to_string(m_diagLongestContextRun) +
            " (1 means the contexts strictly alternated).");
}

void NgxInterceptor::TriggerDiscontinuityFlush() {
    VRBridge* bridge = GetVRBridge();
    if (bridge) {
        bridge->SignalDiscontinuity();
    }
    m_internalDiscontinuityPendingLeft.store(true);
    m_internalDiscontinuityPendingRight.store(true);
    ForgetNearEstimate();
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

InFlightLease<VirtualNgxHandle> NgxInterceptor::AcquireVirtualHandle(const NVSDK_NGX_Handle* handle) const {
    if (!handle) return {};
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_virtualHandles.find(const_cast<NVSDK_NGX_Handle*>(handle));
    if (it != m_virtualHandles.end() && it->second) {
        return InFlightLease<VirtualNgxHandle>(it->second);
    }
    return {};
}

void NgxInterceptor::RegisterVirtualHandle(std::shared_ptr<VirtualNgxHandle> handle) {
    if (!handle) return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    NVSDK_NGX_Handle* rawKey = static_cast<NVSDK_NGX_Handle*>(handle.get());
    m_virtualHandles[rawKey] = std::move(handle);
}

std::shared_ptr<VirtualNgxHandle> NgxInterceptor::UnregisterVirtualHandle(NVSDK_NGX_Handle* handle) {
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

    if (InFeatureID != NVSDK_NGX_Feature_SuperSampling) {
        return origCreate(InCmdList, InFeatureID, InParameters, OutHandle);
    }

    if (!InParameters) {
        LogError("NgxInterceptor: InParameters is null");
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }

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

    SavedPresetOverrides createPresetOverrides;
    createPresetOverrides.Apply(InParameters, interceptor.GetDlssPresetOverride(), false);

    auto clonedParams = std::make_shared<NgxParameterImpl>();
    clonedParams->CopyFrom(InParameters);

    NVSDK_NGX_Handle* hLeft = nullptr;
    NVSDK_NGX_Result resLeft = origCreate(InCmdList, InFeatureID, InParameters, &hLeft);
    createPresetOverrides.Restore(InParameters);
    if (NVSDK_NGX_FAILED(resLeft) || !hLeft) {
        std::stringstream ss;
        ss << "NgxInterceptor: Failed to create left NGX feature: " << resLeft
           << " (0x" << std::hex << resLeft << std::dec << ") FeatureID=" << InFeatureID
           << " Target=[" << effectiveTargetW << "x" << effectiveTargetH << "]"
           << " Render=[" << renderRes.renderWidth << "x" << renderRes.renderHeight << "]";
        LogError(ss.str());
        return resLeft;
    }

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

    auto virtualHandle = std::make_shared<VirtualNgxHandle>();
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

    int createFlags = 0;
    bool hasCreateFlags = NVSDK_NGX_SUCCEED(InParameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &createFlags));
    unsigned int inW = 0, inH = 0, outW = 0, outH = 0;
    InParameters->Get(NVSDK_NGX_Parameter_Width, &inW);
    InParameters->Get(NVSDK_NGX_Parameter_Height, &inH);
    InParameters->Get(NVSDK_NGX_Parameter_OutWidth, &outW);
    InParameters->Get(NVSDK_NGX_Parameter_OutHeight, &outH);
    std::stringstream diag;
    diag << "NgxInterceptor: [Diag] Create flags=" << (hasCreateFlags ? DescribeDlssCreateFlags(createFlags) : "absent")
         << " (0x" << std::hex << createFlags << std::dec << ")"
         << " PerfQuality=" << perfQualityInt
         << " Engine dims=" << inW << "x" << inH << " -> " << outW << "x" << outH
         << " RightCloneHasFlags=" << (clonedParams->HasParam(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags) ? 1 : 0);
    LogInfo(diag.str());
    interceptor.ArmDiagnostics();
    return NVSDK_NGX_Result_Success;
}

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

    if (auto virtualHandle = interceptor.UnregisterVirtualHandle(InHandle)) {
        virtualHandle->callGate.WaitForZero();

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

    return origRelease(InHandle);
}

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

    auto handleLease = interceptor.AcquireVirtualHandle(InFeatureHandle);
    auto* vHandle = handleLease.get();
    if (!vHandle || vHandle->magic != VIRTUAL_NGX_HANDLE_MAGIC) {
        handleLease.Reset();
        return origEval(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    VRBridge* bridge = interceptor.GetVRBridge();

    int currentMode = bridge ? static_cast<int>(bridge->GetRenderingMode()) : -1;
    int lastMode = interceptor.m_lastRenderingMode.exchange(currentMode);
    if (lastMode != -1 && lastMode != currentMode) {
        interceptor.TriggerDiscontinuityFlush();
        interceptor.ArmDiagnostics();
    }

    bool isDualPass = (bridge && bridge->GetRenderingMode() == VRRenderingMode::SequentialDualPass);

    const NVSDK_NGX_Handle* target = nullptr;
    uint32_t contextIndex = 0;
    bool alternatesEyeContexts = false;
    const uint32_t activeEye = bridge ? bridge->GetActiveEye() : 0;

    if (bridge && (bridge->Is2DViewActive() || bridge->IsMenuVisible())) {
        target = vHandle->hLeft;
        contextIndex = 0;
    } else {
        target = (activeEye == 1 && vHandle->hRight) ? vHandle->hRight : vHandle->hLeft;
        contextIndex = (activeEye == 1 && vHandle->hRight) ? 1 : 0;
        alternatesEyeContexts = (vHandle->hRight != nullptr);
    }

    if (!target) target = InFeatureHandle;

    bool needReset = interceptor.ConsumeDiscontinuity(contextIndex);
    if (needReset) interceptor.ForgetNearEstimate();

    int prevReset = 0;
    bool hadPrevReset = false;
    auto* mutableParams = const_cast<NVSDK_NGX_Parameter*>(InParameters);

    if (needReset && mutableParams) {
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_Reset, &prevReset))) {
            hadPrevReset = true;
        }
        mutableParams->Set(NVSDK_NGX_Parameter_Reset, 1);
    }

    SavedPresetOverrides evalPresetOverrides;
    evalPresetOverrides.Apply(mutableParams, interceptor.GetDlssPresetOverride(), true);

    float prevSharpness = 0.0f;
    bool hadPrevSharpness = false;
    bool wroteSharpness = false;
    float sharpnessOverride = interceptor.GetSharpnessOverride();
    if (sharpnessOverride >= 0.0f && mutableParams) {
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_Sharpness, &prevSharpness))) {
            hadPrevSharpness = true;
            if (prevSharpness <= 0.0f || interceptor.IsForceSharpnessOverride()) {
                mutableParams->Set(NVSDK_NGX_Parameter_Sharpness, sharpnessOverride);
                wroteSharpness = true;
            }
        } else {
            mutableParams->Set(NVSDK_NGX_Parameter_Sharpness, sharpnessOverride);
            wroteSharpness = true;
        }
    }

    bool eyeStride = !isDualPass && alternatesEyeContexts && bridge && bridge->IsCompanionActive();
    bool dualPassEyeContexts = isDualPass && alternatesEyeContexts && bridge && bridge->IsCompanionActive();
    float prevScaleX = 1.0f, prevScaleY = 1.0f;
    float scaleMulX = 1.0f, scaleMulY = 1.0f;
    float afrFallbackMulX = 1.0f, afrFallbackMulY = 1.0f;
    if (eyeStride) {
        afrFallbackMulX = interceptor.GetMvScaleMultiplierX();
        afrFallbackMulY = interceptor.GetMvScaleMultiplierY();
    } else if (dualPassEyeContexts) {
        scaleMulX = interceptor.GetSdpMvScaleMultiplierX();
        scaleMulY = interceptor.GetSdpMvScaleMultiplierY();
    }
    bool wroteScale = false;
    if (mutableParams) {
        float scale = 1.0f;
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_MV_Scale_X, &scale))) prevScaleX = scale;
        scale = 1.0f;
        if (NVSDK_NGX_SUCCEED(mutableParams->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &scale))) prevScaleY = scale;
    }
    const bool mvFixWillRun = interceptor.m_mvFixEnabled.load() && !interceptor.m_mvFixDisabled.load();
    if (eyeStride && !mvFixWillRun) {
        scaleMulX = afrFallbackMulX;
        scaleMulY = afrFallbackMulY;
    }
    if (mutableParams && (scaleMulX != 1.0f || scaleMulY != 1.0f)) {
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_X, prevScaleX * scaleMulX);
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y, prevScaleY * scaleMulY);
        wroteScale = true;
    }

    const bool needMvPass = interceptor.m_mvFixEnabled.load() && alternatesEyeContexts && InCmdList && mutableParams;
    std::optional<ResolvedCameras> resolvedCameras;
    if (bridge && (needMvPass || interceptor.m_diagRemaining.load() > 0)) {
        resolvedCameras = bridge->GetCameraRecords().Resolve(activeEye);
    }

    ID3D12Resource* prevMv = nullptr;
    bool swappedMv = false;
    const char* correctionState = "off";
    if (needMvPass) {
        ID3D12Resource* depth = nullptr;
        ID3D12Resource* mv = nullptr;
        mutableParams->Get(NVSDK_NGX_Parameter_Depth, &depth);
        mutableParams->Get(NVSDK_NGX_Parameter_MotionVectors, &mv);
        if (depth && mv) {
            const float effScaleX = prevScaleX * scaleMulX;
            const float effScaleY = prevScaleY * scaleMulY;
            mvreproj::CorrectionPlan plan;
            if (resolvedCameras) plan = mvreproj::PlanCorrection(*resolvedCameras);
            MvCorrectionRequest correctionRequest;
            const MvCorrectionRequest* correction = nullptr;
            correctionState = mvreproj::CorrectionDecisionName(plan.decision);
            const bool infiniteDepth = interceptor.m_depthModelInfinite.load();
            const float nearOverride = interceptor.m_nearClipOverride.load();
            const float autoNear = nearOverride > 0.0f ? 0.0f : interceptor.m_nearEstimator.Estimate();
            const float forcedNear = nearOverride > 0.0f ? nearOverride : autoNear;
            if (plan.decision == mvreproj::CorrectionDecision::Correct) {
                if (std::fabs(effScaleX) > 1e-6f && std::fabs(effScaleY) > 1e-6f) {
                    plan.current.infiniteDepth = infiniteDepth;
                    if (forcedNear > 0.0f) {
                        plan.current.nearClip = plan.previousFrame.nearClip = plan.sameEye.nearClip = forcedNear;
                    }
                    correctionRequest.mvScaleX = effScaleX;
                    correctionRequest.mvScaleY = effScaleY;
                    correctionRequest.current = plan.current;
                    correctionRequest.previousFrame = plan.previousFrame;
                    correctionRequest.sameEye = plan.sameEye;
                    correctionRequest.toPreviousFrame = plan.toPreviousFrame;
                    correctionRequest.toSameEye = plan.toSameEye;
                    correctionRequest.objectMotionFactor = eyeStride ? mvreproj::kAfrEyeStride : 1.0f;
                    correctionRequest.gateNonStatic = true;
                    correction = &correctionRequest;
                } else {
                    correctionState = "zero-mv-scale";
                }
            }
            if (interceptor.m_diagRemaining.load() > 0) {
                LogInfo("NgxInterceptor: [Diag] MvCorrection Eval #" + std::to_string(interceptor.m_evalCount.load()) +
                        " Eye=" + std::to_string(activeEye) + ": " + (correction ? "correcting" : correctionState));
            }

            MvSelfCheckRequest checkRequest;
            const MvSelfCheckRequest* check = nullptr;
            if (interceptor.m_diagRemaining.load() > 0) {
                const uint64_t evalIndex = interceptor.m_evalCount.load();
                std::string skipReason;
                if (resolvedCameras &&
                    BuildMvSelfCheckRequest(*resolvedCameras, activeEye, evalIndex, effScaleX, effScaleY,
                                            correction ? &plan : nullptr, checkRequest, skipReason)) {
                    checkRequest.current.infiniteDepth = infiniteDepth;
                    if (forcedNear > 0.0f) checkRequest.current.nearClip = checkRequest.previous.nearClip = forcedNear;
                    check = &checkRequest;
                    if (!interceptor.m_diagProjectionLogged.exchange(true)) {
                        unsigned int renderW = 0, renderH = 0;
                        mutableParams->Get(NVSDK_NGX_Parameter_Width, &renderW);
                        mutableParams->Get(NVSDK_NGX_Parameter_Height, &renderH);
                        MvProjectionDiag diag;
                        diag.source = nearOverride > 0.0f ? "override" : (autoNear > 0.0f ? "auto" : "camera-record");
                        diag.used = {forcedNear > 0.0f ? forcedNear : resolvedCameras->current->nearClip,
                                     resolvedCameras->current->farClip, resolvedCameras->current->fovY};
                        diag.autoEstimate = autoNear;
                        diag.hasRecordNear = true;
                        diag.recordNear = resolvedCameras->current->nearClip;
                        diag.projOffsetX = resolvedCameras->current->projOffsetX;
                        diag.projOffsetY = resolvedCameras->current->projOffsetY;
                        diag.aspect = renderH ? static_cast<float>(renderW) / static_cast<float>(renderH) : 1.0f;
                        diag.infinite = infiniteDepth;
                        diag.staticGate = true;
                        diag.depthSource = "vrDlssDepthModel";
                        LogInfo(FormatMvProjectionLine("NgxInterceptor", "Eval", activeEye, evalIndex, diag));
                    }
                } else {
                    if (!bridge) skipReason = "no VR bridge";
                    LogInfo(FormatMvSelfCheckSkipped("NgxInterceptor", "Eval", activeEye, evalIndex, skipReason));
                }
            }
            MvSelfCheckRequest nearRequest;
            float recordNear = 0.0f;
            if (resolvedCameras) {
                if (resolvedCameras->current) recordNear = resolvedCameras->current->nearClip;
                if (nearOverride <= 0.0f && resolvedCameras->current && resolvedCameras->previousFrame &&
                    std::fabs(effScaleX) > 1e-6f && std::fabs(effScaleY) > 1e-6f &&
                    interceptor.m_nearCrossEyeEvals.fetch_add(1) % NgxInterceptor::kNearEstimateInterval ==
                        NgxInterceptor::kNearEstimateInterval - 1) {
                    std::string skipReason;
                    const uint64_t nearEvalIndex = interceptor.m_evalCount.load();
                    if (BuildMvSelfCheckRequest(*resolvedCameras, activeEye, nearEvalIndex, effScaleX, effScaleY, nullptr,
                                                nearRequest, skipReason)) {
                        nearRequest.current.infiniteDepth = infiniteDepth;
                        if (forcedNear > 0.0f) nearRequest.current.nearClip = nearRequest.previous.nearClip = forcedNear;
                        nearRequest.nearEstimate = true;
                        check = &nearRequest;
                    }
                }
            }
            std::vector<MvSelfCheckReport> reports;
            ID3D12Resource* corrected = interceptor.RecordMvPass(InCmdList, depth, mv, check, &reports, correction);
            for (const MvSelfCheckReport& report : reports) {
                if (report.nearEstimate) {
                    interceptor.OnNearSolution(report.evalIndex, report.nearSolution, recordNear, report.eye,
                                               report.stats.gatedFraction);
                    if (interceptor.m_diagRemaining.load() > 0) {
                        LogInfo(FormatMvSelfCheckReport("NgxInterceptor", "Eval", report));
                    }
                    continue;
                }
                if (!interceptor.m_diagRawRangeLogged.exchange(true)) {
                    LogInfo(FormatMvRawRangeReport("NgxInterceptor", "Eval", report));
                }
                LogInfo(FormatMvSelfCheckReport("NgxInterceptor", "Eval", report));
            }
            if (corrected) {
                prevMv = mv;
                mutableParams->Set(NVSDK_NGX_Parameter_MotionVectors, corrected);
                swappedMv = true;
            }
        }
    }

    if (eyeStride && mvFixWillRun && !swappedMv && mutableParams) {
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_X, prevScaleX * afrFallbackMulX);
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y, prevScaleY * afrFallbackMulY);
        wroteScale = true;
    }

    uint64_t currentEval = interceptor.m_evalCount.fetch_add(1);
    interceptor.RecordDiagnostics(currentEval, contextIndex, InParameters, needReset, isDualPass,
                                  resolvedCameras ? &*resolvedCameras : nullptr);
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
           << " MvFix=" << interceptor.DescribeMvFixState() << " MvCorrection=" << correctionState
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

    if (swappedMv) {
        mutableParams->Set(NVSDK_NGX_Parameter_MotionVectors, prevMv);
    }

    if (wroteScale) {
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_X, prevScaleX);
        mutableParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y, prevScaleY);
    }

    if (wroteSharpness) {
        mutableParams->Set(NVSDK_NGX_Parameter_Sharpness, hadPrevSharpness ? prevSharpness : 0.0f);
    }

    evalPresetOverrides.Restore(mutableParams);

    return res;
}

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

HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryW(LPCWSTR lpLibFileName) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryW)>(interceptor->m_detourLoadLibraryW) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName) : LoadLibraryW(lpLibFileName);

    if (interceptor && hMod && !IsDataOrImageResourceLoad(0, hMod)) {
        if (MatchesNgxName(lpLibFileName)) {
            interceptor->AttachModule(hMod);
        } else {
            interceptor->NotifyModuleLoaded(hMod, lpLibFileName);
        }
    }
    return hMod;
}

HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryExW)>(interceptor->m_detourLoadLibraryExW) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName, hFile, dwFlags) : LoadLibraryExW(lpLibFileName, hFile, dwFlags);

    if (interceptor && hMod && !IsDataOrImageResourceLoad(dwFlags, hMod)) {
        if (MatchesNgxName(lpLibFileName)) {
            interceptor->AttachModule(hMod);
        } else {
            interceptor->NotifyModuleLoaded(hMod, lpLibFileName);
        }
    }
    return hMod;
}

HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryA(LPCSTR lpLibFileName) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryA)>(interceptor->m_detourLoadLibraryA) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName) : LoadLibraryA(lpLibFileName);

    if (interceptor && hMod && !IsDataOrImageResourceLoad(0, hMod)) {
        if (MatchesNgxName(lpLibFileName)) {
            interceptor->AttachModule(hMod);
        } else {
            interceptor->NotifyModuleLoaded(hMod, lpLibFileName);
        }
    }
    return hMod;
}

HMODULE WINAPI NgxInterceptor::Intercept_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    NgxInterceptor* interceptor = LoadHookHost();
    auto orig = interceptor ? Original<decltype(&LoadLibraryExA)>(interceptor->m_detourLoadLibraryExA) : nullptr;
    HMODULE hMod = orig ? orig(lpLibFileName, hFile, dwFlags) : LoadLibraryExA(lpLibFileName, hFile, dwFlags);

    if (interceptor && hMod && !IsDataOrImageResourceLoad(dwFlags, hMod)) {
        if (MatchesNgxName(lpLibFileName)) {
            interceptor->AttachModule(hMod);
        } else {
            interceptor->NotifyModuleLoaded(hMod, lpLibFileName);
        }
    }
    return hMod;
}

}
