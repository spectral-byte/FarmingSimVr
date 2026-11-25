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

inline constexpr size_t kPresentVTableSlot = 8;
inline constexpr size_t kResizeBuffersVTableSlot = 13;
inline constexpr size_t kCreateSwapChainVTableSlot = 10;
inline constexpr size_t kCreateSwapChainForHwndVTableSlot = 15;
inline constexpr size_t kPresent1VTableSlot = 22;
inline constexpr size_t kCheckColorSpaceSupportVTableSlot = 37;
inline constexpr size_t kSetColorSpace1VTableSlot = 38;
inline constexpr size_t kResizeBuffers1VTableSlot = 39;
inline constexpr size_t kOutputGetDesc1VTableSlot = 27;

bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace);

class DxgiProxy {
public:
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

    bool AttachSwapChain(IDXGISwapChain* swapChain);
    bool AttachFactory(IDXGIFactory* factory);
    bool AttachOutput(IDXGIOutput* output);
    void DetachAll();

    bool IsSwapChainAttached() const { return m_swapChainAttached; }
    bool IsFactoryAttached() const { return m_factoryAttached; }
    bool IsOutputAttached() const { return m_outputAttached; }

    bool OnPresent(IDXGISwapChain* swapChain, UINT syncInterval = 0, UINT flags = 0);
    bool OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval = 0, UINT flags = 0, const DXGI_PRESENT_PARAMETERS* params = nullptr);
    void OnPresentReturned(HRESULT presentResult = S_OK);
    UINT EffectiveSyncInterval(UINT gameSyncInterval) const;

    bool IsPresentSuppressed() const { return m_lastPresentSuppressed; }

    void LatchPendingRenderingMode();

    void SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    static bool ExtractD3D12DeviceAndQueue(
        IUnknown* source,
        Microsoft::WRL::ComPtr<ID3D12Device>& outDevice,
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>& outQueue);
    bool ResolveD3D12DeviceAndQueue(IUnknown* source);

    ID3D12Device* GetD3D12Device() const { return m_d3d12Device.Get(); }
    ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_d3d12Queue.Get(); }

    OpenXrManager* GetOpenXrManager() { return m_openXrManager.get(); }
    ISystemDxgi* GetSystemDxgi() { return m_systemDxgi.get(); }
    LuauInterceptor* GetLuauInterceptor() { return m_luauInterceptor.get(); }
    VRBridge* GetVrBridge() { return m_vrBridge.get(); }
    MirrorWindowController& GetWindowController() { return *m_windowController; }

    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }

    bool IsActive() const { return m_openXrManager && m_openXrManager->IsActive(); }
    void GetDecoupledTargetResolution(uint32_t& outWidth, uint32_t& outHeight) const;

    static HRESULT STDMETHODCALLTYPE Intercept_Present(IDXGISwapChain* self, UINT syncInterval, UINT flags);
    static HRESULT STDMETHODCALLTYPE Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params);
    static HRESULT STDMETHODCALLTYPE Intercept_ResizeBuffers(IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT STDMETHODCALLTYPE Intercept_ResizeBuffers1(IDXGISwapChain3* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue);
    static HRESULT STDMETHODCALLTYPE Intercept_CreateSwapChain(IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
    static HRESULT STDMETHODCALLTYPE Intercept_CreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
    static HRESULT STDMETHODCALLTYPE Intercept_CheckColorSpaceSupport(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE colorSpace, UINT* pColorSpaceSupport);
    static HRESULT STDMETHODCALLTYPE Intercept_SetColorSpace1(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE colorSpace);
    static HRESULT STDMETHODCALLTYPE Intercept_OutputGetDesc1(IDXGIOutput6* self, DXGI_OUTPUT_DESC1* pDesc);
    static HRESULT WINAPI Intercept_D3D11On12CreateDevice(IUnknown* pDevice, UINT flags, const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT featureLevels, IUnknown* const* ppCommandQueues, UINT numQueues, UINT nodeMask, void** ppDevice,
        void** ppImmediateContext, D3D_FEATURE_LEVEL* pChosenFeatureLevel);
    std::string UnwrapForD3D11On12(IUnknown*& device, IUnknown** queues, UINT numQueues);
    bool IsDetourResizeBuffersInstalled() const { return m_detourResizeBuffers.IsInstalled(); }
    bool IsDetourColorSpaceInstalled() const { return m_detourCheckColorSpaceSupport.IsInstalled() && m_detourSetColorSpace1.IsInstalled(); }

private:
    void EnsureVrInitialized();
    void AttachFirstOutput(IDXGIFactory* factory);

    static void PrepareResize(DxgiProxy* proxy, const char* hookName, UINT& width, UINT& height);
    void AttachCreatedFactory(HRESULT hr, void** ppFactory);

    static DxgiProxy* ForHook(FunctionDetour DxgiProxy::*detour);

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
    std::optional<DXGI_COLOR_SPACE_TYPE> m_lastSetColorSpace;
    uint32_t m_loggedHiddenColorSpaces = 0;  // bit per DXGI_COLOR_SPACE_TYPE value
    bool m_loggedHiddenOutputHdr = false;
    bool m_ownedByRuntime = false;
    std::once_flag m_vrInitOnce;
    bool m_wasInQuadMode = false;
    bool m_lastPresentSuppressed = false;
    bool m_presentWasStereo = false;
    bool OnPresentImpl(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    PresentFacts GatherPresentFacts(IDXGISwapChain* swapChain, Microsoft::WRL::ComPtr<ID3D12Resource>& backBufferHolder,
                                    ID3D12Resource*& pBackBuffer);
    bool CarryOutPresentPlan(IDXGISwapChain* swapChain, ID3D12Resource* backBuffer, PresentFacts facts, PresentPlan plan);
    // `vrEyeSnapshot`: after an eye copy, writes the back buffer just copied to that eye to fs25_vr_eye<N>.bmp next to the
    // log when VRBridge has a request for that eye. Blocks until the GPU has read it back.
    void SnapshotEyeIfRequested(uint32_t eyeIndex, ID3D12Resource* backBuffer);
    bool IsDesktopMirrorOffNow() const;
    bool m_desktopMirrorWasShowing = false;
    void UpdateDesktopMirrorTitle();
    bool m_desktopMirrorOffAnnounced = false;
    void UpdateMouseConfinement();
    bool m_mouseConfinementUpdated = false;

    std::atomic<const void*> m_gameSwapChain{ nullptr };
    bool IsGameSwapChain(const void* swapChain) const { return swapChain && swapChain == m_gameSwapChain.load(); }
    bool IsForGameWindow(HWND window) const;
    bool PrepareGameSwapChainCreation(const char* hookName, HWND window);
    void ReleaseCachedBackBuffers();

    IDXGISwapChain* m_cachedSwapChain = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_cachedSwapChain3;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_cachedBackBuffers;
};

}
