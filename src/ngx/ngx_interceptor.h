#pragma once

#include "ngx_defs.h"
#include "virtual_ngx_handle.h"
#include "core/detour.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <windows.h>

namespace fs25vr {

class NgxInterceptor {
public:
    static NgxInterceptor& Instance();

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

    // VRBridge dependency injection for testing and runtime synchronization
    class VRBridge* GetVRBridge() const;
    void SetVRBridge(class VRBridge* bridge);

    // DxgiProxy and OpenXrManager dependency injection for decoupled resolution pipeline
    class DxgiProxy* GetDxgiProxy() const;
    void SetDxgiProxy(class DxgiProxy* proxy);
    class OpenXrManager* GetOpenXrManager() const;
    void SetOpenXrManager(class OpenXrManager* mgr);

    void GetTargetUpscalingDimensions(uint32_t& outWidth, uint32_t& outHeight) const;

    // Check if nvngx is already loaded in the process and attach to it
    void CheckAndAttachLoadedModule();

    // Hook LoadLibraryW/ExW/A/ExA to automatically detect when nvngx is loaded dynamically
    bool InstallModuleLoadHooks();
    void UninstallModuleLoadHooks();

    // Detach entry-point hooks on NGX
    void Detach();

    // Detach all hooks including module load hooks and clear registries
    void DetachAll();

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

    // Getters for original function pointers
    PFN_NVSDK_NGX_D3D12_CreateFeature GetOriginalCreateFeature() const { return m_origCreateFeature; }
    PFN_NVSDK_NGX_D3D12_ReleaseFeature GetOriginalReleaseFeature() const { return m_origReleaseFeature; }
    PFN_NVSDK_NGX_D3D12_EvaluateFeature GetOriginalEvaluateFeature() const { return m_origEvaluateFeature; }
    PFN_NVSDK_NGX_D3D12_AllocateParameters GetOriginalAllocateParameters() const { return m_origAllocateParameters; }
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters GetOriginalGetCapabilityParameters() const { return m_origGetCapabilityParameters; }
    PFN_NVSDK_NGX_D3D12_DestroyParameters GetOriginalDestroyParameters() const { return m_origDestroyParameters; }

    // DLSS tuning overrides & motion vector scaling
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

private:
    bool AttachDirectInternal(
        PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreate,
        PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnRelease,
        PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluate,
        PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAlloc,
        PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroy,
        PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCaps
    );

    mutable std::recursive_mutex m_mutex;
    bool m_isAttached = false;
    bool m_moduleHooksInstalled = false;
    HMODULE m_attachedModule = nullptr;
    class VRBridge* m_vrBridge = nullptr;
    class DxgiProxy* m_dxgiProxy = nullptr;
    class OpenXrManager* m_openXrManager = nullptr;

    int m_dlssPresetOverride = NVSDK_NGX_DLSS_Hint_Render_Preset_K;
    float m_sharpnessOverride = 0.50f;
    bool m_forceSharpnessOverride = true;
    float m_mvScaleMultiplierX = 0.0f;
    float m_mvScaleMultiplierY = 0.0f;
    std::atomic<uint64_t> m_evalCount{0};

    std::atomic<int> m_lastRenderingMode{-1};
    std::atomic<bool> m_internalDiscontinuityPendingLeft{false};
    std::atomic<bool> m_internalDiscontinuityPendingRight{false};

    std::atomic<uint32_t> m_nextVirtualId{1000};

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

    // Original NGX pointers
    PFN_NVSDK_NGX_D3D12_CreateFeature m_origCreateFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_ReleaseFeature m_origReleaseFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_EvaluateFeature m_origEvaluateFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_AllocateParameters m_origAllocateParameters = nullptr;
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters m_origGetCapabilityParameters = nullptr;
    PFN_NVSDK_NGX_D3D12_DestroyParameters m_origDestroyParameters = nullptr;

    // Original LoadLibrary pointers
    HMODULE (WINAPI *m_origLoadLibraryW)(LPCWSTR) = nullptr;
    HMODULE (WINAPI *m_origLoadLibraryExW)(LPCWSTR, HANDLE, DWORD) = nullptr;
    HMODULE (WINAPI *m_origLoadLibraryA)(LPCSTR) = nullptr;
    HMODULE (WINAPI *m_origLoadLibraryExA)(LPCSTR, HANDLE, DWORD) = nullptr;
};

} // namespace fs25vr
