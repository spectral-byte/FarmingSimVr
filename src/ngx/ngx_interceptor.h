#pragma once

#include "ngx_defs.h"
#include "mv_pass.h"
#include "near_estimator.h"
#include "virtual_ngx_handle.h"
#include "core/detour.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace fs25vr {

class NgxInterceptor {
public:
    NgxInterceptor();
    ~NgxInterceptor();

    NgxInterceptor(const NgxInterceptor&) = delete;
    NgxInterceptor& operator=(const NgxInterceptor&) = delete;

    // Attach detours using raw function pointers (primarily for unit testing and direct invocation)
    bool AttachDirect(
        PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreate,
        PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnRelease,
        PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluate = nullptr,
        PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAlloc = nullptr,
        PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroy = nullptr,
        PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCaps = nullptr
    );

    // Inspect exports of a loaded module (nvngx.dll) and install detours
    bool AttachModule(HMODULE hNgxModule);

    // Dependencies are wired by InjectorRuntime (or by a test). The interceptor never falls back to a
    // global singleton: an unset dependency reads as null and its feature degrades gracefully.
    // VRBridge dependency for mode, menu and discontinuity state
    class VRBridge* GetVRBridge() const;
    void SetVRBridge(class VRBridge* bridge);

    // DxgiProxy and OpenXrManager dependency injection for decoupled resolution pipeline
    class DxgiProxy* GetDxgiProxy() const;
    void SetDxgiProxy(class DxgiProxy* proxy);
    class OpenXrManager* GetOpenXrManager() const;
    void SetOpenXrManager(class OpenXrManager* mgr);

    void GetTargetUpscalingDimensions(uint32_t& outWidth, uint32_t& outHeight) const;

    // The FidelityFX interceptor shares this interceptor's LoadLibrary hooks: a load of the FidelityFX
    // loader is forwarded to it, and CheckAndAttachLoadedModule also checks for an already loaded one.
    class FidelityFxInterceptor* GetFidelityFxInterceptor() const;
    void SetFidelityFxInterceptor(class FidelityFxInterceptor* interceptor);

    // Check if nvngx is already loaded in the process and attach to it
    void CheckAndAttachLoadedModule();

    // Hook LoadLibraryW/ExW/A/ExA to automatically detect when nvngx is loaded dynamically
    bool InstallModuleLoadHooks();
    void UninstallModuleLoadHooks();

    // Detach entry-point hooks on NGX
    void Detach();

    // Detach all hooks including module load hooks and clear registries
    void DetachAll();

    // DetachAll on whichever interceptors own the live detours, whoever owns those interceptors.
    static void DetachLiveHooks();

    bool IsAttached() const;
    bool AreModuleHooksInstalled() const;
    bool IsSingleContextBypassActive() const;

    // Active Dual-Pass sub-pass eye index query
    uint32_t GetActiveDualPassEyeIndex() const;

    // Discontinuity flushing coordination
    void TriggerDiscontinuityFlush();
    void ResetTemporalHistory() { TriggerDiscontinuityFlush(); }
    bool HasPendingDiscontinuity(uint32_t contextIndex = 0) const;
    bool ConsumeDiscontinuity(uint32_t contextIndex = 0);

    // Handle and parameter registry access
    size_t GetActiveVirtualHandleCount() const;
    bool IsVirtualHandle(const NVSDK_NGX_Handle* handle) const;
    VirtualNgxHandle* GetVirtualHandle(const NVSDK_NGX_Handle* handle) const;

    void RegisterVirtualHandle(std::unique_ptr<VirtualNgxHandle> handle);
    std::unique_ptr<VirtualNgxHandle> UnregisterVirtualHandle(NVSDK_NGX_Handle* handle);

    void RegisterTrackedParameter(std::unique_ptr<NgxParameterImpl> param);
    bool UnregisterTrackedParameter(NVSDK_NGX_Parameter* param);

    // Detour entry points
    static NVSDK_NGX_Result Intercept_CreateFeature(
        ID3D12GraphicsCommandList* InCmdList,
        NVSDK_NGX_Feature InFeatureID,
        NVSDK_NGX_Parameter* InParameters,
        NVSDK_NGX_Handle** OutHandle
    );

    static NVSDK_NGX_Result Intercept_ReleaseFeature(
        NVSDK_NGX_Handle* InHandle
    );

    static NVSDK_NGX_Result Intercept_EvaluateFeature(
        ID3D12GraphicsCommandList* InCmdList,
        const NVSDK_NGX_Handle* InFeatureHandle,
        const NVSDK_NGX_Parameter* InParameters,
        PFN_NVSDK_NGX_ProgressCallback InCallback
    );

    static NVSDK_NGX_Result Intercept_AllocateParameters(
        NVSDK_NGX_Parameter** OutParameters
    );

    static NVSDK_NGX_Result Intercept_GetCapabilityParameters(
        NVSDK_NGX_Parameter** OutParameters
    );

    static NVSDK_NGX_Result Intercept_DestroyParameters(
        NVSDK_NGX_Parameter* InParameters
    );

    // Module load detours
    static HMODULE WINAPI Intercept_LoadLibraryW(LPCWSTR lpLibFileName);
    static HMODULE WINAPI Intercept_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    static HMODULE WINAPI Intercept_LoadLibraryA(LPCSTR lpLibFileName);
    static HMODULE WINAPI Intercept_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

    static bool MatchesNgxName(LPCWSTR lpLibFileName);
    static bool MatchesNgxName(LPCSTR lpLibFileName);

    // Getters for original function pointers. They read the detour's trampoline, which FunctionDetour
    // publishes before the patch goes live, so a call racing the install already sees it.
    PFN_NVSDK_NGX_D3D12_CreateFeature GetOriginalCreateFeature() const { return Original<PFN_NVSDK_NGX_D3D12_CreateFeature>(m_detourCreateFeature); }
    PFN_NVSDK_NGX_D3D12_ReleaseFeature GetOriginalReleaseFeature() const { return Original<PFN_NVSDK_NGX_D3D12_ReleaseFeature>(m_detourReleaseFeature); }
    PFN_NVSDK_NGX_D3D12_EvaluateFeature GetOriginalEvaluateFeature() const { return Original<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(m_detourEvaluateFeature); }
    PFN_NVSDK_NGX_D3D12_AllocateParameters GetOriginalAllocateParameters() const { return Original<PFN_NVSDK_NGX_D3D12_AllocateParameters>(m_detourAllocateParameters); }
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters GetOriginalGetCapabilityParameters() const { return Original<PFN_NVSDK_NGX_D3D12_GetCapabilityParameters>(m_detourGetCapabilityParameters); }
    PFN_NVSDK_NGX_D3D12_DestroyParameters GetOriginalDestroyParameters() const { return Original<PFN_NVSDK_NGX_D3D12_DestroyParameters>(m_detourDestroyParameters); }

    // DLSS tuning overrides & motion vector scaling
    static constexpr int kDefaultDlssPresetOverride = NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    static constexpr float kDefaultSharpnessOverride = 0.50f;
    static constexpr bool kDefaultForceSharpnessOverride = true;
    // Eye-Stride Motion Vector compensation under AFR (ADR-0015). Overridable
    // via SetMvScaleMultiplier; SDP always evaluates at 1.0x.
    static constexpr float kDefaultAfrMvScale = 2.0f;

    int GetDlssPresetOverride() const { return m_dlssPresetOverride; }
    void SetDlssPresetOverride(int preset) { m_dlssPresetOverride = preset; }
    float GetSharpnessOverride() const { return m_sharpnessOverride; }
    void SetSharpnessOverride(float sharpness, bool force = true) {
        m_sharpnessOverride = sharpness;
        m_forceSharpnessOverride = force;
    }
    bool IsForceSharpnessOverride() const { return m_forceSharpnessOverride; }
    float GetMvScaleMultiplierX() const { return m_mvScaleMultiplierX; }
    float GetMvScaleMultiplierY() const { return m_mvScaleMultiplierY; }
    void SetMvScaleMultiplier(float scaleX, float scaleY) {
        m_mvScaleMultiplierX = scaleX;
        m_mvScaleMultiplierY = scaleY;
    }
    // Diagnostic multiplier for per-eye contexts under SDP (1.0 by default, independent of the AFR one).
    float GetSdpMvScaleMultiplierX() const { return m_sdpMvScaleMultiplierX; }
    float GetSdpMvScaleMultiplierY() const { return m_sdpMvScaleMultiplierY; }
    void SetSdpMvScaleMultiplier(float scaleX, float scaleY) {
        m_sdpMvScaleMultiplierX = scaleX;
        m_sdpMvScaleMultiplierY = scaleY;
    }

    // Motion vector pass (vrDlssMvFix). While on, a compute pass recorded on the game's command list ahead
    // of each per-eye DLSS evaluation produces the motion vectors DLSS receives for that one evaluation.
    // Off by default until verified in game. A pass failure turns it off for the session (the engine's
    // motion vectors are then used) and "on" no longer takes effect.
    // Returns the resulting enabled state, so a request to turn it on after a failure reads false.
    bool SetMvFixEnabled(bool enabled);
    bool IsMvFixEnabled() const { return m_mvFixEnabled.load(); }
    bool IsMvFixDisabledForSession() const { return m_mvFixDisabled.load(); }
    std::string GetMvFixDisableReason() const;
    // Depth model the correction and the self-check read the engine's depth with (vrDlssDepthModel).
    // Infinite (the default): reverse-Z with an infinite far plane, view depth = near / depth, the model
    // the engine's FSR3 path declares (FFX_UPSCALE_ENABLE_DEPTH_INFINITE) and the same renderer feeds
    // DLSS. Finite: reverse-Z between the camera record's near and far planes. For A/B comparison.
    void SetDepthModelInfinite(bool infinite);
    bool IsDepthModelInfinite() const { return m_depthModelInfinite.load(); }
    // Near plane in metres (vrDlssNearClip) for the correction and the self-check, on every camera.
    // m > 0 is a manual override and always wins. 0 (the default, also written uto) estimates the
    // renderer's near from the engine's own motion vectors (see mvreproj::SolveNear): the Companion Mod's
    // camera record reports the same near for every camera while the renderer's differs (about 0.1 in a
    // vehicle cab, 0.15 on foot). Until the first estimate, and after a Discontinuity Flush, the record's
    // near is used. Negative or non-finite values count as 0.
    void SetNearClipOverride(float meters);
    float GetNearClipOverride() const { return m_nearClipOverride.load(); }
    // The smoothed automatic estimate in metres, or 0 when there is none (yet).
    float GetAutoNearEstimate() const { return m_nearEstimator.Estimate(); }
    // Every Nth cross-eye evaluation records a near plane estimate (a sparse self-check read back later).
    static constexpr uint32_t kNearEstimateInterval = 4;
    // Replace how the pass is created (tests install a fake). Defaults to the D3D12 implementation.
    void SetMotionVectorPassFactory(MotionVectorPassFactory factory);
    const char* DescribeMvFixState() const;

    // Evaluation diagnostics: while armed, every EvaluateFeature logs a trace line and feeds a
    // per-context jitter summary, logged when the countdown reaches zero. Armed automatically when a
    // feature is virtualized or the Stereo Presentation Mode changes, and on demand (vrDlssDiag).
    static constexpr uint32_t kDefaultDiagEvaluations = 128;
    static constexpr uint32_t kMaxDiagEvaluations = 4096;
    void ArmDiagnostics(uint32_t evaluations = kDefaultDiagEvaluations);
    uint32_t GetDiagEvaluationsRemaining() const { return m_diagRemaining.load(); }

    struct JitterSummary {
        uint32_t evaluations = 0;
        uint32_t resets = 0;
        uint32_t distinctOffsets = 0;
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    };
    // Summary of the jitter offsets context contextIndex received in the current diagnostic window.
    JitterSummary GetDiagJitterSummary(uint32_t contextIndex) const;

private:
    template <typename PFN>
    static PFN Original(const FunctionDetour& detour) { return reinterpret_cast<PFN>(detour.GetOriginal()); }

    // The interceptor whose detours are live, or null when none is. Published before the detours are
    // installed and cleared after they are removed, so a live detour always finds its host.
    static NgxInterceptor* FeatureHost();
    static NgxInterceptor* LoadHookHost();

    bool AttachDirectInternal(
        PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreate,
        PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnRelease,
        PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluate,
        PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAlloc,
        PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroy,
        PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCaps
    );

    void NotifyModuleLoaded(HMODULE hMod, LPCWSTR name);
    void NotifyModuleLoaded(HMODULE hMod, LPCSTR name);

    mutable std::recursive_mutex m_mutex;
    class FidelityFxInterceptor* m_fidelityFx = nullptr;
    bool m_isAttached = false;
    bool m_moduleHooksInstalled = false;
    HMODULE m_attachedModule = nullptr;
    class VRBridge* m_vrBridge = nullptr;
    class DxgiProxy* m_dxgiProxy = nullptr;
    class OpenXrManager* m_openXrManager = nullptr;

    int m_dlssPresetOverride = kDefaultDlssPresetOverride;
    float m_sharpnessOverride = kDefaultSharpnessOverride;
    bool m_forceSharpnessOverride = kDefaultForceSharpnessOverride;
    float m_mvScaleMultiplierX = kDefaultAfrMvScale;
    float m_mvScaleMultiplierY = kDefaultAfrMvScale;
    float m_sdpMvScaleMultiplierX = 1.0f;
    float m_sdpMvScaleMultiplierY = 1.0f;
    std::atomic<uint64_t> m_evalCount{0};

    // Records the pass for this evaluation. Returns the texture to hand DLSS, or null to keep the
    // engine's motion vectors (also after a failure, which disables the pass for the session).
    // When request is set the pass also records a self-check; reports, when set, receives the self-checks
    // that finished reading back since the previous call.
    ID3D12Resource* RecordMvPass(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth, ID3D12Resource* mv,
                                 const MvSelfCheckRequest* request = nullptr,
                                 std::vector<MvSelfCheckReport>* reports = nullptr,
                                 const MvCorrectionRequest* correction = nullptr);
    void DisableMvFix(const std::string& reason);

    std::atomic<bool> m_depthModelInfinite{true};
    std::atomic<float> m_nearClipOverride{0.0f};
    NearEstimator m_nearEstimator;
    std::atomic<uint32_t> m_nearCrossEyeEvals{0}; // cross-eye evaluations seen, to pick every Nth
    std::atomic<float> m_nearLastLogged{0.0f};
    std::atomic<int64_t> m_nearLastLogMs{INT64_MIN};
    void ForgetNearEstimate();
    void OnNearSolution(uint64_t evalIndex, const mvreproj::NearSolution& solution, float recordNear, uint32_t eye,
                        double gatedFraction);
    std::atomic<bool> m_diagProjectionLogged{false}; // the MvProjection line, once per arming
    std::atomic<bool> m_mvFixEnabled{false};
    std::atomic<bool> m_mvFixDisabled{false};
    mutable std::mutex m_mvFixMutex;
    std::string m_mvFixDisableReason;
    MotionVectorPassFactory m_mvPassFactory;
    std::unique_ptr<IMotionVectorPass> m_mvPass;

    std::atomic<int> m_lastRenderingMode{-1};
    std::atomic<bool> m_internalDiscontinuityPendingLeft{false};
    std::atomic<bool> m_internalDiscontinuityPendingRight{false};

    std::atomic<uint32_t> m_nextVirtualId{1000};

    struct DiagSample {
        float jitterX = 0.0f;
        float jitterY = 0.0f;
        bool reset = false;
    };
    void RecordDiagnostics(uint64_t evalIndex, uint32_t contextIndex, const NVSDK_NGX_Parameter* params,
                           bool reset, bool isDualPass);
    void LogDiagSummary();
    static JitterSummary Summarize(const std::vector<DiagSample>& samples);

    std::atomic<uint32_t> m_diagRemaining{0};
    std::atomic<bool> m_diagRawRangeLogged{false}; // the raw MV range line, once per arming
    mutable std::mutex m_diagMutex;
    std::vector<DiagSample> m_diagSamples[2];
    int m_diagLastContext = -1;
    uint32_t m_diagContextRun = 0;
    uint32_t m_diagLongestContextRun = 0;

    std::unordered_map<NVSDK_NGX_Handle*, std::unique_ptr<VirtualNgxHandle>> m_virtualHandles;
    std::unordered_map<NVSDK_NGX_Parameter*, std::unique_ptr<NgxParameterImpl>> m_trackedParameters;

    // Detours for NGX functions
    FunctionDetour m_detourCreateFeature;
    FunctionDetour m_detourReleaseFeature;
    FunctionDetour m_detourEvaluateFeature;
    FunctionDetour m_detourAllocateParameters;
    FunctionDetour m_detourGetCapabilityParameters;
    FunctionDetour m_detourDestroyParameters;

    // Detours for DLL loading
    FunctionDetour m_detourLoadLibraryW;
    FunctionDetour m_detourLoadLibraryExW;
    FunctionDetour m_detourLoadLibraryA;
    FunctionDetour m_detourLoadLibraryExA;
};

} // namespace fs25vr
