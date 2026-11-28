#pragma once

#include "ffx_defs.h"
#include "core/detour.h"
#include "ngx/mv_pass.h"
#include "ngx/upscaler_mv_helper.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace fs25vr {

class VRBridge;

class FidelityFxInterceptor {
public:
    FidelityFxInterceptor();
    ~FidelityFxInterceptor();

    FidelityFxInterceptor(const FidelityFxInterceptor&) = delete;
    FidelityFxInterceptor& operator=(const FidelityFxInterceptor&) = delete;

    bool AttachDirect(PFN_ffxCreateContext pfnCreate, PFN_ffxDestroyContext pfnDestroy,
                      PFN_ffxConfigure pfnConfigure = nullptr, PFN_ffxQuery pfnQuery = nullptr,
                      PFN_ffxDispatch pfnDispatch = nullptr);

    bool AttachModule(HMODULE hLoaderModule);
    void CheckAndAttachLoadedModule();
    static bool MatchesLoaderName(LPCWSTR lpLibFileName);
    static bool MatchesLoaderName(LPCSTR lpLibFileName);

    void Detach();
    void DetachAll();
    static void DetachLiveHooks();
    bool IsAttached() const;

    VRBridge* GetVRBridge() const;
    void SetVRBridge(VRBridge* bridge);

    size_t GetActiveVirtualContextCount() const;
    bool IsVirtualContext(ffxContext context) const;
    VirtualFfxContext* GetVirtualContext(ffxContext context) const;
    InFlightLease<VirtualFfxContext> AcquireVirtualContext(ffxContext context) const;

    static constexpr uint32_t kDefaultDiagDispatches = UpscalerDiagnostics::kDefaultWindow;
    static constexpr uint32_t kMaxDiagDispatches = UpscalerDiagnostics::kMaxWindow;
    void ArmDiagnostics(uint32_t dispatches = kDefaultDiagDispatches);
    uint32_t GetDiagDispatchesRemaining() const { return m_diag.Remaining(); }

    struct JitterSummary {
        uint32_t dispatches = 0;
        uint32_t resets = 0;
        uint32_t distinctOffsets = 0;
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    };
    JitterSummary GetDiagJitterSummary(uint32_t contextIndex) const;
    uint32_t GetDiagLongestContextRun() const;

    static constexpr bool kDefaultMvFixEnabled = UpscalerMvPassRunner::kDefaultEnabled;

    void SetMotionVectorPassFactory(MotionVectorPassFactory factory);
    bool SetMvFixEnabled(bool enabled);
    bool IsMvFixEnabled() const { return m_mvRunner.IsEnabled(); }
    bool IsMvFixDisabledForSession() const { return m_mvRunner.IsDisabledForSession(); }
    std::string GetMvFixDisableReason() const;
    const char* DescribeMvFixState() const;

    void TriggerDiscontinuityFlush();
    bool ConsumeDiscontinuity(uint32_t contextIndex);

    static ffxReturnCode_t Intercept_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                   const ffxAllocationCallbacks* memCb);
    static ffxReturnCode_t Intercept_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb);
    static ffxReturnCode_t Intercept_Configure(ffxContext* context, const ffxConfigureDescHeader* desc);
    static ffxReturnCode_t Intercept_Query(ffxContext* context, ffxQueryDescHeader* desc);
    static ffxReturnCode_t Intercept_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc);

private:
    template <typename PFN>
    static PFN Original(const FunctionDetour& detour) { return reinterpret_cast<PFN>(detour.GetOriginal()); }

    static FidelityFxInterceptor* Host();

    void RecordDiagnostics(uint64_t dispatchIndex, uint32_t contextIndex, const ffxDispatchDescUpscale& desc,
                           bool reset, bool isDualPass);

    bool ApplyMvCorrection(ffxDispatchDescUpscale& desc, uint32_t createFlags, uint32_t eye, uint64_t dispatchIndex,
                           ffxApiResource& savedMv, const char*& state);

    mutable std::recursive_mutex m_mutex;
    bool m_isAttached = false;
    VRBridge* m_vrBridge = nullptr;

    FunctionDetour m_detourCreate;
    FunctionDetour m_detourDestroy;
    FunctionDetour m_detourConfigure;
    FunctionDetour m_detourQuery;
    FunctionDetour m_detourDispatch;

    std::atomic<uint32_t> m_nextVirtualId{2000};
    std::unordered_map<ffxContext, std::shared_ptr<VirtualFfxContext>> m_virtualContexts;

    std::atomic<uint64_t> m_dispatchCount{0};
    std::atomic<int> m_lastRenderingMode{-1};
    std::atomic<bool> m_internalDiscontinuityPendingLeft{false};
    std::atomic<bool> m_internalDiscontinuityPendingRight{false};

    UpscalerMvPassRunner m_mvRunner{"FidelityFxInterceptor", "FSR3"};
    UpscalerDiagnostics m_diag{"FidelityFxInterceptor", "dispatches", "Dispatches"};
};

}
