#pragma once

#include "system_dxgi.h"
#include "mirror_window_controller.h"
#include "openxr/openxr_manager.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "core/detour.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
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

    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory);
    HRESULT DXGIDeclareAdapterRemovalSupport();
    HRESULT DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug);

    bool AttachSwapChain(IDXGISwapChain* swapChain);
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
    bool IsDetourResizeBuffersInstalled() const { return m_detourResizeBuffers.IsInstalled(); }
    bool IsDetourResizeBuffers1Installed() const { return m_detourResizeBuffers1.IsInstalled(); }
    bool IsDetourColorSpaceInstalled() const { return m_detourCheckColorSpaceSupport.IsInstalled() && m_detourSetColorSpace1.IsInstalled(); }

private:
    void EnsureVrInitialized();
    // Attaches the first output of any adapter the factory enumerates.
    void AttachFirstOutput(IDXGIFactory* factory);

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
    // Set by OnPresentImpl when the Present was a stereo presentation (an eye copy), so OnPresent
    // advances the rendered-frame counter for it and discards camera records for any other Present.
    bool m_presentWasStereo = false;
    bool OnPresentImpl(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    // True while the Desktop Mirror Window is off right now: the player turned it off (VRBridge flag), the
    // Companion Mod's handshake has completed, and the headset session runs with eye swapchains. Everywhere
    // else (Pass-Through Mode, session not running, before the handshake) the mirror shows by itself.
    bool IsDesktopMirrorOffNow() const;
    // The decision at each Present that would reach DXGI: records it in m_lastPresentSuppressed and returns
    // whether the game's real Present is called (false with the mirror off).
    bool PresentToDesktopMirror();

    IDXGISwapChain* m_cachedSwapChain = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_cachedSwapChain3;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_cachedBackBuffers;
};

} // namespace fs25vr
