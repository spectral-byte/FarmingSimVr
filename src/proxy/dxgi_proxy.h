#pragma once

#include "system_dxgi.h"
#include "mirror_window_controller.h"
#include "openxr/openxr_manager.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "telemetry/graphics_telemetry_probe.h"
#include "telemetry/d3d12_interceptor.h"
#include "core/detour.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <optional>

namespace fs25vr {

// Canonical DXGI vtable slot indices
inline constexpr size_t kPresentVTableSlot = 8;
inline constexpr size_t kResizeBuffersVTableSlot = 13;
inline constexpr size_t kCreateSwapChainVTableSlot = 10;
inline constexpr size_t kCreateSwapChainForHwndVTableSlot = 15;
inline constexpr size_t kPresent1VTableSlot = 22;
inline constexpr size_t kResizeBuffers1VTableSlot = 39;

class DxgiProxy {
public:
    static DxgiProxy& Instance();

    explicit DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi = nullptr,
                       std::shared_ptr<OpenXrManager> openXrManager = nullptr,
                       std::shared_ptr<LuauInterceptor> luauInterceptor = nullptr,
                       std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe = nullptr,
                       std::shared_ptr<D3D12Interceptor> d3d12Interceptor = nullptr,
                       std::shared_ptr<VRBridge> vrBridge = nullptr,
                       std::shared_ptr<MirrorWindowController> windowController = nullptr);
    ~DxgiProxy();

    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory);
    HRESULT DXGIDeclareAdapterRemovalSupport();
    HRESULT DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug);

    bool AttachSwapChain(IDXGISwapChain* swapChain);
    bool AttachFactory(IDXGIFactory* factory);
    void DetachAll();

    bool IsSwapChainAttached() const { return m_swapChainAttached; }
    bool IsFactoryAttached() const { return m_factoryAttached; }

    void OnPrePresent(IDXGISwapChain* swapChain);
    bool OnPresent(IDXGISwapChain* swapChain, UINT syncInterval = 0, UINT flags = 0);
    bool OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval = 0, UINT flags = 0, const DXGI_PRESENT_PARAMETERS* params = nullptr);
    // Sync interval forwarded to the real Present: 0 while xrWaitFrame paces frames, else the game's.
    UINT EffectiveSyncInterval(UINT gameSyncInterval) const;

    bool IsPresentSuppressed() const { return m_lastPresentSuppressed; }

    void SetPendingRenderingMode(VRRenderingMode mode) { m_pendingRenderingMode = mode; }
    std::optional<VRRenderingMode> GetPendingRenderingMode() const { return m_pendingRenderingMode; }
    void LatchPendingRenderingMode();
    static DxgiProxy* GetActiveProxy();

    void SetD3D12DeviceAndQueue(ID3D12Device* device, ID3D12CommandQueue* queue);
    static bool ExtractD3D12DeviceAndQueue(
        IUnknown* source,
        Microsoft::WRL::ComPtr<ID3D12Device>& outDevice,
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>& outQueue);
    bool ResolveD3D12DeviceAndQueue(IUnknown* source);

    ID3D12Device* GetD3D12Device() const { return m_d3d12Device.Get(); }
    ID3D12CommandQueue* GetD3D12CommandQueue() const { return m_d3d12Queue.Get(); }

    OpenXrManager& GetOpenXrManager() { return *m_openXrManager; }
    ISystemDxgi& GetSystemDxgi() { return *m_systemDxgi; }
    LuauInterceptor& GetLuauInterceptor() { return *m_luauInterceptor; }
    GraphicsTelemetryProbe& GetTelemetryProbe() { return *m_telemetryProbe; }
    D3D12Interceptor& GetD3D12Interceptor() { return *m_d3d12Interceptor; }
    VRBridge& GetVrBridge() { return *m_vrBridge; }
    MirrorWindowController& GetWindowController() { return *m_windowController; }

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
    bool IsDetourResizeBuffersInstalled() const { return m_detourResizeBuffers.IsInstalled(); }
    bool IsDetourResizeBuffers1Installed() const { return m_detourResizeBuffers1.IsInstalled(); }

private:
    void EnsureVrInitialized();

    std::shared_ptr<ISystemDxgi> m_systemDxgi;
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<LuauInterceptor> m_luauInterceptor;
    std::shared_ptr<GraphicsTelemetryProbe> m_telemetryProbe;
    std::shared_ptr<D3D12Interceptor> m_d3d12Interceptor;
    std::shared_ptr<VRBridge> m_vrBridge;
    std::shared_ptr<MirrorWindowController> m_windowController;

    Microsoft::WRL::ComPtr<ID3D12Device> m_d3d12Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_d3d12Queue;

    FunctionDetour m_detourPresent;
    FunctionDetour m_detourPresent1;
    FunctionDetour m_detourResizeBuffers;
    FunctionDetour m_detourResizeBuffers1;
    FunctionDetour m_detourCreateSwapChain;
    FunctionDetour m_detourCreateSwapChainForHwnd;

    bool m_swapChainAttached = false;
    bool m_factoryAttached = false;
    std::atomic<bool> m_vrInitialized{false};
    bool m_wasInQuadMode = false;
    bool m_lastPresentSuppressed = false;
    std::optional<VRRenderingMode> m_pendingRenderingMode;

    IDXGISwapChain* m_cachedSwapChain = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_cachedSwapChain3;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_cachedBackBuffers;
};

} // namespace fs25vr
