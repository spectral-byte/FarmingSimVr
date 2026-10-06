#pragma once

#include "ffx_defs.h"
#include "core/detour.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace fs25vr {

class VRBridge;

// Gives FSR3 a Dual Feature Context like DLSS: an upscaler context created through the FidelityFX
// loader becomes two contexts with the same description behind one virtual handle, and dispatches
// route to the active eye's context. Mirrors NgxInterceptor (virtual handle, eye routing,
// Single-Context Bypass, reset on Discontinuity Flush). Context types it does not understand, such
// as frame generation, pass through untouched.
class FidelityFxInterceptor {
public:
    FidelityFxInterceptor();
    ~FidelityFxInterceptor();

    FidelityFxInterceptor(const FidelityFxInterceptor&) = delete;
    FidelityFxInterceptor& operator=(const FidelityFxInterceptor&) = delete;

    // Attach detours using raw function pointers (unit testing and direct invocation).
    bool AttachDirect(PFN_ffxCreateContext pfnCreate, PFN_ffxDestroyContext pfnDestroy,
                      PFN_ffxConfigure pfnConfigure = nullptr, PFN_ffxQuery pfnQuery = nullptr,
                      PFN_ffxDispatch pfnDispatch = nullptr);

    // Inspect the exports of the loaded FidelityFX loader and install detours.
    bool AttachModule(HMODULE hLoaderModule);
    void CheckAndAttachLoadedModule();
    static bool MatchesLoaderName(LPCWSTR lpLibFileName);
    static bool MatchesLoaderName(LPCSTR lpLibFileName);

    void Detach();
    void DetachAll();
    // DetachAll on whichever interceptor owns the live detours.
    static void DetachLiveHooks();
    bool IsAttached() const;

    // Wired by InjectorRuntime (or a test). An unset bridge reads as null: dispatches go to the left context.
    VRBridge* GetVRBridge() const;
    void SetVRBridge(VRBridge* bridge);

    size_t GetActiveVirtualContextCount() const;
    // The handle value the game holds (what ffxCreateContext wrote), not a pointer to it.
    bool IsVirtualContext(ffxContext context) const;
    VirtualFfxContext* GetVirtualContext(ffxContext context) const;

    // Dispatch diagnostics, matching the DLSS ones: while armed, every upscale dispatch logs a trace line
    // and feeds a per-context jitter summary, logged when the countdown reaches zero. Armed when a context
    // is virtualized, when the Stereo Presentation Mode changes, and on demand (vrDlssDiag).
    static constexpr uint32_t kDefaultDiagDispatches = 128;
    static constexpr uint32_t kMaxDiagDispatches = 4096;
    void ArmDiagnostics(uint32_t dispatches = kDefaultDiagDispatches);
    uint32_t GetDiagDispatchesRemaining() const { return m_diagRemaining.load(); }

    struct JitterSummary {
        uint32_t dispatches = 0;
        uint32_t resets = 0;
        uint32_t distinctOffsets = 0;
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    };
    JitterSummary GetDiagJitterSummary(uint32_t contextIndex) const;
    // Longest run of consecutive dispatches to one context in the current diagnostic window
    // (1 means the contexts strictly alternated).
    uint32_t GetDiagLongestContextRun() const;

    // Discontinuity Flush coordination (shares the VRBridge state with the DLSS path).
    void TriggerDiscontinuityFlush();
    bool ConsumeDiscontinuity(uint32_t contextIndex);

    // Detour entry points
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

    struct DiagSample {
        float jitterX = 0.0f;
        float jitterY = 0.0f;
        bool reset = false;
    };
    void RecordDiagnostics(uint64_t dispatchIndex, uint32_t contextIndex, const ffxDispatchDescUpscale& desc,
                           bool reset, bool isDualPass);
    void LogDiagSummary();
    static JitterSummary Summarize(const std::vector<DiagSample>& samples);

    mutable std::recursive_mutex m_mutex;
    bool m_isAttached = false;
    VRBridge* m_vrBridge = nullptr;

    FunctionDetour m_detourCreate;
    FunctionDetour m_detourDestroy;
    FunctionDetour m_detourConfigure;
    FunctionDetour m_detourQuery;
    FunctionDetour m_detourDispatch;

    std::atomic<uint32_t> m_nextVirtualId{2000};
    std::unordered_map<ffxContext, std::unique_ptr<VirtualFfxContext>> m_virtualContexts;

    std::atomic<uint64_t> m_dispatchCount{0};
    std::atomic<int> m_lastRenderingMode{-1};
    std::atomic<bool> m_internalDiscontinuityPendingLeft{false};
    std::atomic<bool> m_internalDiscontinuityPendingRight{false};

    std::atomic<uint32_t> m_diagRemaining{0};
    mutable std::mutex m_diagMutex;
    std::vector<DiagSample> m_diagSamples[2];
    int m_diagLastContext = -1;
    uint32_t m_diagContextRun = 0;
    uint32_t m_diagLongestContextRun = 0;
};

} // namespace fs25vr
