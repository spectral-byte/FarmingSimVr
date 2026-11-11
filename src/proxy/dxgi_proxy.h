#pragma once

#include "system_dxgi.h"
#include "mirror_window_controller.h"
#include "present_plan.h"
#include "openxr/openxr_manager.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "core/detour.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace fs25vr {

class NgxInterceptor;

// Canonical DXGI vtable slot indices
inline constexpr size_t kPresentVTableSlot = 8;
inline constexpr size_t kResizeBuffersVTableSlot = 13;
inline constexpr size_t kCreateSwapChainVTableSlot = 10;
inline constexpr size_t kCreateSwapChainForHwndVTableSlot = 15;
inline constexpr size_t kPresent1VTableSlot = 22;
inline constexpr size_t kCheckColorSpaceSupportVTableSlot = 37;
inline constexpr size_t kSetColorSpace1VTableSlot = 38;
inline constexpr size_t kResizeBuffers1VTableSlot = 39;
inline constexpr size_t kOutputGetDesc1VTableSlot = 27;

// HDR10, scRGB and HLG colour spaces. OpenXR runtimes show the eye image as SDR, so HDR output
// copied into an eye swapchain looks washed out; while VR is active the game is told the display
// and swap chain support none of these, and renders SDR instead.
bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace);

class DxgiProxy {
public:
    // Every collaborator is supplied by the caller (production wiring comes from InjectorRuntime); a
    // null one is absent and its feature is skipped. The window controller is required.
    // AttachSwapChain/AttachFactory install detours only while a live InjectorRuntime owns the proxy.
    DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi,
              std::shared_ptr<OpenXrManager> openXrManager,
              std::shared_ptr<LuauInterceptor> luauInterceptor,
              std::shared_ptr<VRBridge> vrBridge,
              std::shared_ptr<MirrorWindowController> windowController,
              std::shared_ptr<NgxInterceptor> ngxInterceptor);
    ~DxgiProxy();

    // The game's factory calls, caught by DxgiExportHooks in the system's dxgi.dll (ADR-0038). Each starts VR on the
    // first call, passes the call on to the system and hooks the factory it returns.
    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory);

    bool AttachSwapChain(IDXGISwapChain* swapChain);
    // Hooks swap chain creation on the factory. When the factory is another tool's wrapper (ReShade's), the system's
    // factory behind it is hooked instead, so the wrapper runs before the hooks.
    bool AttachFactory(IDXGIFactory* factory);
    // Hooks IDXGIOutput6::GetDesc1 (shared by every output) so the display reads as SDR while VR is
    // active. AttachFactory calls it with the first output it finds.
    bool AttachOutput(IDXGIOutput* output);
    void DetachAll();

    bool IsSwapChainAttached() const { return m_swapChainAttached; }
    bool IsFactoryAttached() const { return m_factoryAttached; }
    bool IsOutputAttached() const { return m_outputAttached; }

    bool OnPresent(IDXGISwapChain* swapChain, UINT syncInterval = 0, UINT flags = 0);
    bool OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval = 0, UINT flags = 0, const DXGI_PRESENT_PARAMETERS* params = nullptr);
    // Called once the real Present (if any) has returned. Waits on and begins the next OpenXR frame
    // so the Companion Mod's pose requests for the coming eye pair are located at that frame's
    // predicted display time (ADR-0019). A no-op when the real Present failed, a frame is already
    // begun or the session cannot run frames.
    void OnPresentReturned(HRESULT presentResult = S_OK);
    // Sync interval forwarded to the real Present: 0 while xrWaitFrame paces frames, else the game's.
    UINT EffectiveSyncInterval(UINT gameSyncInterval) const;

    bool IsPresentSuppressed() const { return m_lastPresentSuppressed; }

    // Latches the Stereo Presentation Mode change queued in VRBridge. Called only at completed
    // compositor frame-pair boundaries (ADR-0015).
    void LatchPendingRenderingMode();

    void SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    static bool ExtractD3D12DeviceAndQueue(
        IUnknown* source,
        Microsoft::WRL::ComPtr<ID3D12Device>& outDevice,
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>& outQueue);
    bool ResolveD3D12DeviceAndQueue(IUnknown* source);

    ID3D12Device* GetD3D12Device() const { return m_d3d12Device.Get(); }
    ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_d3d12Queue.Get(); }

    // Null when the collaborator was not supplied.
    OpenXrManager* GetOpenXrManager() { return m_openXrManager.get(); }
    ISystemDxgi* GetSystemDxgi() { return m_systemDxgi.get(); }
    LuauInterceptor* GetLuauInterceptor() { return m_luauInterceptor.get(); }
    VRBridge* GetVrBridge() { return m_vrBridge.get(); }
    // Never null: the controller is a required collaborator.
    MirrorWindowController& GetWindowController() { return *m_windowController; }

    // Set by InjectorRuntime while it owns this proxy and hooks can find it.
    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }

    bool IsActive() const { return m_openXrManager && m_openXrManager->IsActive(); }
    // Unlike the controller method, this resolves against this proxy's OpenXR manager.
    void GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight) const;

    // Detour entry points
    static HRESULT STDMETHODCALLTYPE Intercept_Present(IDXGISwapChain* self, UINT syncInterval, UINT flags);
    static HRESULT STDMETHODCALLTYPE Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params);
    static HRESULT STDMETHODCALLTYPE Intercept_ResizeBuffers(IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT STDMETHODCALLTYPE Intercept_ResizeBuffers1(IDXGISwapChain3* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue);
    static HRESULT STDMETHODCALLTYPE Intercept_CreateSwapChain(IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
    static HRESULT STDMETHODCALLTYPE Intercept_CreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
    static HRESULT STDMETHODCALLTYPE Intercept_CheckColorSpaceSupport(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE colorSpace, UINT* pColorSpaceSupport);
    static HRESULT STDMETHODCALLTYPE Intercept_SetColorSpace1(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE colorSpace);
    static HRESULT STDMETHODCALLTYPE Intercept_OutputGetDesc1(IDXGIOutput6* self, DXGI_OUTPUT_DESC1* pDesc);
    // d3d11.dll's D3D11On12CreateDevice (ppDevice and ppImmediateContext are ID3D11Device** and ID3D11DeviceContext**).
    static HRESULT WINAPI Intercept_D3D11On12CreateDevice(IUnknown* pDevice, UINT flags, const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT featureLevels, IUnknown* const* ppCommandQueues, UINT numQueues, UINT nodeMask, void** ppDevice,
        void** ppImmediateContext, D3D_FEATURE_LEVEL* pChosenFeatureLevel);
    // Swaps a wrapper of the game's D3D12 device or queue (ReShade's) for the real object behind it. Returns a
    // description of what was passed and what was swapped, for the log.
    std::string UnwrapForD3D11On12(IUnknown*& device, IUnknown** queues, UINT numQueues);
    bool IsDetourResizeBuffersInstalled() const { return m_detourResizeBuffers.IsInstalled(); }
    bool IsDetourColorSpaceInstalled() const { return m_detourCheckColorSpaceSupport.IsInstalled() && m_detourSetColorSpace1.IsInstalled(); }

private:
    void EnsureVrInitialized();
    // Attaches the first output of any adapter the factory enumerates.
    void AttachFirstOutput(IDXGIFactory* factory);

    // The part of both ResizeBuffers hooks that precedes the call to the original: forgets the cached
    // back buffers, marks the window clamp dirty and, in Active VR Mode, replaces the requested size
    // with the Render Resolution.
    static void PrepareResize(DxgiProxy* proxy, const char* hookName, UINT& width, UINT& height);
    // Hooks the factory a successful CreateDXGIFactory* call returned, in Active VR Mode.
    void AttachCreatedFactory(HRESULT hr, void** ppFactory);

    // The proxy a hook entry point acts for: the one in a live runtime that installed `detour`, else
    // the current runtime's; null when no runtime is alive.
    static DxgiProxy* ForHook(FunctionDetour DxgiProxy::*detour);

    // A system export, or null when no system DXGI was supplied.
    template <typename Pfn>
    Pfn SystemPfn(Pfn (ISystemDxgi::*getter)()) {
        return m_systemDxgi ? (m_systemDxgi.get()->*getter)() : nullptr;
    }

    std::shared_ptr<ISystemDxgi> m_systemDxgi;
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<LuauInterceptor> m_luauInterceptor;
    std::shared_ptr<VRBridge> m_vrBridge;
    std::shared_ptr<MirrorWindowController> m_windowController;
    std::shared_ptr<NgxInterceptor> m_ngxInterceptor;

    Microsoft::WRL::ComPtr<ID3D12Device> m_d3d12Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_d3d12Queue;

    FunctionDetour m_detourPresent;
    FunctionDetour m_detourPresent1;
    FunctionDetour m_detourResizeBuffers;
    FunctionDetour m_detourResizeBuffers1;
    FunctionDetour m_detourCreateSwapChain;
    FunctionDetour m_detourCreateSwapChainForHwnd;
    FunctionDetour m_detourCheckColorSpaceSupport;
    FunctionDetour m_detourSetColorSpace1;
    FunctionDetour m_detourOutputGetDesc1;
    FunctionDetour m_detourD3D11On12CreateDevice;

    bool m_swapChainAttached = false;
    bool m_factoryAttached = false;
    bool m_outputAttached = false;
    // Colour-space log state, so a game that polls these each frame logs each fact once.
    std::optional<DXGI_COLOR_SPACE_TYPE> m_lastSetColorSpace;
    uint32_t m_loggedHiddenColorSpaces = 0;  // bit per DXGI_COLOR_SPACE_TYPE value
    bool m_loggedHiddenOutputHdr = false;
    bool m_ownedByRuntime = false;
    std::once_flag m_vrInitOnce;
    bool m_wasInQuadMode = false;
    bool m_lastPresentSuppressed = false;
    // Set by the bookkeeping of every Present (from the plan) when the Present was a stereo presentation (an eye
    // copy), so OnPresent advances the rendered-frame counter for it and discards camera records for any other Present.
    bool m_presentWasStereo = false;
    // One Present in three steps (present_plan.h): the facts, the plan PlanPresent makes of them, the plan carried out
    // with the bookkeeping done once at its end. Returns whether the game's real Present is called.
    bool OnPresentImpl(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    // Does the setup the decision needs, then reports the facts. The setup has side effects: it polls the runtime's
    // events, learns the game window, resolves the device and creates the D3D12 device/queue and the eye swapchains,
    // and finds the back buffer (cached; held in `backBufferHolder` when the cache does not hold it alone). It makes no
    // plan decisions: a Present with no VR work is a plan, not an early return.
    PresentFacts GatherPresentFacts(IDXGISwapChain* swapChain, Microsoft::WRL::ComPtr<ID3D12Resource>& backBufferHolder,
                                    ID3D12Resource*& pBackBuffer);
    // Carries `plan` out in the one order the OpenXR runtime and DXGI see (ADR-0015, ADR-0019): pacing reset, the eye copy,
    // the submission, the counters, the latch, then the Desktop Mirror Window's black frame; then the bookkeeping. `facts`
    // are the ones the plan was made from.
    bool CarryOutPresentPlan(IDXGISwapChain* swapChain, ID3D12Resource* backBuffer, PresentFacts facts, PresentPlan plan);
    // `vrEyeSnapshot`: after an eye copy, writes the back buffer just copied to that eye to fs25_vr_eye<N>.bmp next to the
    // log when VRBridge has a request for that eye. Blocks until the GPU has read it back.
    void SnapshotEyeIfRequested(uint32_t eyeIndex, ID3D12Resource* backBuffer);
    // True while the Desktop Mirror Window is off right now: the player turned it off (VRBridge flag), the
    // Companion Mod's handshake has completed, and the headset session runs with eye swapchains. Everywhere
    // else (Pass-Through Mode, session not running, before the handshake) the mirror shows by itself.
    bool IsDesktopMirrorOffNow() const;
    // True while the Desktop Mirror Window showed at the last Present (on, or shown by itself); false once it switched off.
    // A frame that goes from showing to off presents once, black (PresentPlan::clearBackBufferToBlack).
    bool m_desktopMirrorWasShowing = false;
    // After each Present: logs each change of IsDesktopMirrorOffNow() once, and keeps the game window's title
    // matching it (off note whenever the mirror is off and the title is not shown, the kept title when it shows
    // again). Leaves the title alone otherwise.
    void UpdateDesktopMirrorTitle();
    bool m_desktopMirrorOffAnnounced = false;
    // At each Present, on every Present path: tells the Mirror Window Controller whether Mouse
    // Confinement holds the Desk Mouse to the game picture, so that it can also let go on every path.
    // The menu quad path does it before it reads the Desk Mouse for the drawn cursor, so the frame
    // where holding starts already draws the cursor where it was brought to; every other path does it
    // after the Present's work. It runs once per Present (m_mouseConfinementUpdated).
    void UpdateMouseConfinement();
    bool m_mouseConfinementUpdated = false;

    // The game's swap chain, by identity only: no reference is held, so the game can still destroy it. Set when the
    // proxy attaches to it; every hook acts on this one and passes any other swap chain's call straight through.
    // Atomic because another tool's swap chain (an overlay, a capture tool) may present from a thread of its own while
    // the game's creation writes this: a statement about the process, not a guarantee the design gives.
    std::atomic<const void*> m_gameSwapChain{ nullptr };
    bool IsGameSwapChain(const void* swapChain) const { return swapChain && swapChain == m_gameSwapChain.load(); }
    // Whether a swap chain created for `window` can be the game's: the game's window is not known yet, or it is
    // that one (a null window is unknown). One rule for every creation hook.
    bool IsForGameWindow(HWND window) const;
    // The start of both creation hooks, before the original is called. Returns false, having logged it, when the
    // creation is for another window (the hook then passes it on untouched); otherwise releases the old swap chain's
    // cached back buffers, since nothing of them may outlive the creation, and returns true.
    bool PrepareGameSwapChainCreation(const char* hookName, HWND window);
    // Lets go of the game's cached back buffers and the swap chain they were cached from, so the Injector keeps
    // nothing of it alive: before a resize and before a creation for the game window is passed on.
    void ReleaseCachedBackBuffers();

    IDXGISwapChain* m_cachedSwapChain = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_cachedSwapChain3;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_cachedBackBuffers;
};

} // namespace fs25vr
