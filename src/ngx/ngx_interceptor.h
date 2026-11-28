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

struct ResolvedCameras;

class NgxInterceptor {
public:
    NgxInterceptor();
    ~NgxInterceptor();

    NgxInterceptor(const NgxInterceptor&) = delete;
    NgxInterceptor& operator=(const NgxInterceptor&) = delete;

    bool AttachDirect(
        PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreate,
        PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnRelease,
        PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluate = nullptr,
        PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAlloc = nullptr,
        PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroy = nullptr,
        PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCaps = nullptr
    );

    bool AttachModule(HMODULE hNgxModule);

    class VRBridge* GetVRBridge() const;
    void SetVRBridge(class VRBridge* bridge);

    class DxgiProxy* GetDxgiProxy() const;
    void SetDxgiProxy(class DxgiProxy* proxy);
    class OpenXrManager* GetOpenXrManager() const;
    void SetOpenXrManager(class OpenXrManager* mgr);

    void GetTargetUpscalingDimensions(uint32_t& outWidth, uint32_t& outHeight) const;

    class FidelityFxInterceptor* GetFidelityFxInterceptor() const;
    void SetFidelityFxInterceptor(class FidelityFxInterceptor* interceptor);

    void CheckAndAttachLoadedModule();

    bool InstallModuleLoadHooks();
    void UninstallModuleLoadHooks();

    void Detach();

    void DetachAll();

    static void DetachLiveHooks();

    bool IsAttached() const;
    bool AreModuleHooksInstalled() const;
    bool IsSingleContextBypassActive() const;

    uint32_t GetActiveDualPassEyeIndex() const;

    void TriggerDiscontinuityFlush();
    bool HasPendingDiscontinuity(uint32_t contextIndex = 0) const;
    bool ConsumeDiscontinuity(uint32_t contextIndex = 0);

    size_t GetActiveVirtualHandleCount() const;
    bool IsVirtualHandle(const NVSDK_NGX_Handle* handle) const;
    VirtualNgxHandle* GetVirtualHandle(const NVSDK_NGX_Handle* handle) const;
    InFlightLease<VirtualNgxHandle> AcquireVirtualHandle(const NVSDK_NGX_Handle* handle) const;

    void RegisterVirtualHandle(std::shared_ptr<VirtualNgxHandle> handle);
    std::shared_ptr<VirtualNgxHandle> UnregisterVirtualHandle(NVSDK_NGX_Handle* handle);

    void RegisterTrackedParameter(std::unique_ptr<NgxParameterImpl> param);
    bool UnregisterTrackedParameter(NVSDK_NGX_Parameter* param);

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

    static HMODULE WINAPI Intercept_LoadLibraryW(LPCWSTR lpLibFileName);
    static HMODULE WINAPI Intercept_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    static HMODULE WINAPI Intercept_LoadLibraryA(LPCSTR lpLibFileName);
    static HMODULE WINAPI Intercept_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

    static bool MatchesNgxName(LPCWSTR lpLibFileName);
    static bool MatchesNgxName(LPCSTR lpLibFileName);

    PFN_NVSDK_NGX_D3D12_CreateFeature GetOriginalCreateFeature() const { return Original<PFN_NVSDK_NGX_D3D12_CreateFeature>(m_detourCreateFeature); }
    PFN_NVSDK_NGX_D3D12_ReleaseFeature GetOriginalReleaseFeature() const { return Original<PFN_NVSDK_NGX_D3D12_ReleaseFeature>(m_detourReleaseFeature); }
    PFN_NVSDK_NGX_D3D12_EvaluateFeature GetOriginalEvaluateFeature() const { return Original<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(m_detourEvaluateFeature); }
    PFN_NVSDK_NGX_D3D12_AllocateParameters GetOriginalAllocateParameters() const { return Original<PFN_NVSDK_NGX_D3D12_AllocateParameters>(m_detourAllocateParameters); }
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters GetOriginalGetCapabilityParameters() const { return Original<PFN_NVSDK_NGX_D3D12_GetCapabilityParameters>(m_detourGetCapabilityParameters); }
    PFN_NVSDK_NGX_D3D12_DestroyParameters GetOriginalDestroyParameters() const { return Original<PFN_NVSDK_NGX_D3D12_DestroyParameters>(m_detourDestroyParameters); }

    static constexpr int kDefaultDlssPresetOverride = NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    static constexpr float kDefaultSharpnessOverride = 0.50f;
    static constexpr bool kDefaultForceSharpnessOverride = true;
    static constexpr bool kDefaultMvFixEnabled = true;
    static constexpr float kDefaultAfrMvScale = 2.0f;

    int GetDlssPresetOverride() const { return m_dlssPresetOverride.load(); }
    void SetDlssPresetOverride(int preset) { m_dlssPresetOverride.store(preset); }
    float GetSharpnessOverride() const { return m_sharpnessOverride.load(); }
    void SetSharpnessOverride(float sharpness, bool force = true) {
        m_sharpnessOverride.store(sharpness);
        m_forceSharpnessOverride.store(force);
    }
    bool IsForceSharpnessOverride() const { return m_forceSharpnessOverride.load(); }
    float GetMvScaleMultiplierX() const { return m_mvScaleMultiplierX.load(); }
    float GetMvScaleMultiplierY() const { return m_mvScaleMultiplierY.load(); }
    void SetMvScaleMultiplier(float scaleX, float scaleY) {
        m_mvScaleMultiplierX.store(scaleX);
        m_mvScaleMultiplierY.store(scaleY);
    }
    float GetSdpMvScaleMultiplierX() const { return m_sdpMvScaleMultiplierX.load(); }
    float GetSdpMvScaleMultiplierY() const { return m_sdpMvScaleMultiplierY.load(); }
    void SetSdpMvScaleMultiplier(float scaleX, float scaleY) {
        m_sdpMvScaleMultiplierX.store(scaleX);
        m_sdpMvScaleMultiplierY.store(scaleY);
    }

    bool SetMvFixEnabled(bool enabled);
    bool IsMvFixEnabled() const { return m_mvFixEnabled.load(); }
    bool IsMvFixDisabledForSession() const { return m_mvFixDisabled.load(); }
    std::string GetMvFixDisableReason() const;
    void SetDepthModelInfinite(bool infinite);
    bool IsDepthModelInfinite() const { return m_depthModelInfinite.load(); }
    void SetNearClipOverride(float meters);
    float GetNearClipOverride() const { return m_nearClipOverride.load(); }
    float GetAutoNearEstimate() const { return m_nearEstimator.Estimate(); }
    // Every Nth cross-eye evaluation records a near plane estimate (a sparse self-check read back later).
    static constexpr uint32_t kNearEstimateInterval = 4;
    void SetMotionVectorPassFactory(MotionVectorPassFactory factory);
    const char* DescribeMvFixState() const;

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
    JitterSummary GetDiagJitterSummary(uint32_t contextIndex) const;

private:
    template <typename PFN>
    static PFN Original(const FunctionDetour& detour) { return reinterpret_cast<PFN>(detour.GetOriginal()); }

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

    std::atomic<int> m_dlssPresetOverride{kDefaultDlssPresetOverride};
    std::atomic<float> m_sharpnessOverride{kDefaultSharpnessOverride};
    std::atomic<bool> m_forceSharpnessOverride{kDefaultForceSharpnessOverride};
    std::atomic<float> m_mvScaleMultiplierX{kDefaultAfrMvScale};
    std::atomic<float> m_mvScaleMultiplierY{kDefaultAfrMvScale};
    std::atomic<float> m_sdpMvScaleMultiplierX{1.0f};
    std::atomic<float> m_sdpMvScaleMultiplierY{1.0f};
    std::atomic<uint64_t> m_evalCount{0};

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
    std::atomic<bool> m_diagProjectionLogged{false};
    std::atomic<bool> m_mvFixEnabled{kDefaultMvFixEnabled};
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
                           bool reset, bool isDualPass, const ResolvedCameras* resolvedCameras = nullptr);
    void LogDiagSummary();
    static JitterSummary Summarize(const std::vector<DiagSample>& samples);

    std::atomic<uint32_t> m_diagRemaining{0};
    std::atomic<bool> m_diagRawRangeLogged{false};
    mutable std::mutex m_diagMutex;
    std::vector<DiagSample> m_diagSamples[2];
    int m_diagLastContext = -1;
    uint32_t m_diagContextRun = 0;
    uint32_t m_diagLongestContextRun = 0;

    std::unordered_map<NVSDK_NGX_Handle*, std::shared_ptr<VirtualNgxHandle>> m_virtualHandles;
    std::unordered_map<NVSDK_NGX_Parameter*, std::unique_ptr<NgxParameterImpl>> m_trackedParameters;

    FunctionDetour m_detourCreateFeature;
    FunctionDetour m_detourReleaseFeature;
    FunctionDetour m_detourEvaluateFeature;
    FunctionDetour m_detourAllocateParameters;
    FunctionDetour m_detourGetCapabilityParameters;
    FunctionDetour m_detourDestroyParameters;

    FunctionDetour m_detourLoadLibraryW;
    FunctionDetour m_detourLoadLibraryExW;
    FunctionDetour m_detourLoadLibraryA;
    FunctionDetour m_detourLoadLibraryExA;
};

}
