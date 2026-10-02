#pragma once

#include "system_dxgi.h"
#include "openxr/openxr_manager.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "telemetry/graphics_telemetry_probe.h"
#include "telemetry/d3d12_interceptor.h"
#include "core/detour.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <commctrl.h>
#include <wrl/client.h>
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

inline constexpr UINT_PTR kMirrorWindowSubclassId = 0x56524653; // 'FSVR'

class DxgiProxy {
public:
    static DxgiProxy& Instance();

    explicit DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi = nullptr,
                       std::shared_ptr<OpenXrManager> openXrManager = nullptr,
                       std::shared_ptr<LuauInterceptor> luauInterceptor = nullptr,
                       std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe = nullptr,
                       std::shared_ptr<D3D12Interceptor> d3d12Interceptor = nullptr,
                       std::shared_ptr<VRBridge> vrBridge = nullptr);
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
    void OnPresent(IDXGISwapChain* swapChain, UINT syncInterval = 0, UINT flags = 0);
    void OnPresent1(IDXGISwapChain1* swapChain, UINT syncInterval = 0, UINT flags = 0, const DXGI_PRESENT_PARAMETERS* params = nullptr);

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

    HWND GetGameHwnd() const { return m_gameHwnd; }
    void SetGameHwnd(HWND hWnd);
    void UpdateMenuCursorState(IDXGISwapChain* swapChain);
    static bool ScreenToNormalizedClient(HWND hWnd, POINT screenPt, float& outNormX, float& outNormY);
    static bool ScaleVirtualCursorToPhysical(HWND hWnd, int screenX, int screenY, int& outPhysScreenX, int& outPhysScreenY);
    static bool ConstrainVirtualClipRectToPhysical(HWND hWnd, const RECT* lpRect, RECT& outPhysClipRect);
    static RECT ComputeClampedMirrorWindowRect(const RECT& workArea, DWORD dwStyle, DWORD dwExStyle, bool hasMenu = false);
    static RECT ComputeClampedMirrorWindowRect(HWND hWnd);

    bool AttachWindowSubclass(HWND hWnd);
    bool DetachWindowSubclass();
    bool IsWindowSubclassed() const { return m_windowSubclassed; }
    bool IsInSizeMove() const { return m_inSizeMove; }
    bool IsActive() const { return m_openXrManager && m_openXrManager->IsActive(); }
    uint32_t GetTargetRenderWidth() const { return m_targetRenderWidth; }
    uint32_t GetTargetRenderHeight() const { return m_targetRenderHeight; }
    void SetTargetRenderResolution(uint32_t width, uint32_t height) {
        m_targetRenderWidth = width;
        m_targetRenderHeight = height;
    }
    bool ApplyInitialWindowClamp(HWND hWnd);
    void EnforceClampedMirrorWindowDimensions(HWND hWnd);
    static LRESULT CALLBACK MirrorWindowSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    // Detour entry points
    static HRESULT STDMETHODCALLTYPE Intercept_Present(IDXGISwapChain* self, UINT syncInterval, UINT flags);
    static HRESULT STDMETHODCALLTYPE Intercept_Present1(IDXGISwapChain1* self, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* params);
    static HRESULT STDMETHODCALLTYPE Intercept_ResizeBuffers(IDXGISwapChain* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT STDMETHODCALLTYPE Intercept_ResizeBuffers1(IDXGISwapChain3* self, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue);
    static HRESULT STDMETHODCALLTYPE Intercept_CreateSwapChain(IDXGIFactory* self, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
    static HRESULT STDMETHODCALLTYPE Intercept_CreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
    static BOOL WINAPI Intercept_GetClientRect(HWND hWnd, LPRECT lpRect);
    static BOOL WINAPI Intercept_ScreenToClient(HWND hWnd, LPPOINT lpPoint);
    static BOOL WINAPI Intercept_SetCursorPos(int X, int Y);
    static BOOL WINAPI Intercept_ClipCursor(const RECT* lpRect);

    bool IsDetourGetClientRectInstalled() const { return m_detourGetClientRect.IsInstalled(); }
    bool IsDetourScreenToClientInstalled() const { return m_detourScreenToClient.IsInstalled(); }
    bool IsDetourSetCursorPosInstalled() const { return m_detourSetCursorPos.IsInstalled(); }
    bool IsDetourClipCursorInstalled() const { return m_detourClipCursor.IsInstalled(); }
    bool IsDetourResizeBuffersInstalled() const { return m_detourResizeBuffers.IsInstalled(); }
    bool IsDetourResizeBuffers1Installed() const { return m_detourResizeBuffers1.IsInstalled(); }

private:
    void EnsureVrInitialized();

    HWND m_gameHwnd = nullptr;

    std::shared_ptr<ISystemDxgi> m_systemDxgi;
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<LuauInterceptor> m_luauInterceptor;
    std::shared_ptr<GraphicsTelemetryProbe> m_telemetryProbe;
    std::shared_ptr<D3D12Interceptor> m_d3d12Interceptor;
    std::shared_ptr<VRBridge> m_vrBridge;

    Microsoft::WRL::ComPtr<ID3D12Device> m_d3d12Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_d3d12Queue;

    FunctionDetour m_detourPresent;
    FunctionDetour m_detourPresent1;
    FunctionDetour m_detourResizeBuffers;
    FunctionDetour m_detourResizeBuffers1;
    FunctionDetour m_detourCreateSwapChain;
    FunctionDetour m_detourCreateSwapChainForHwnd;
    FunctionDetour m_detourGetClientRect;
    FunctionDetour m_detourScreenToClient;
    FunctionDetour m_detourSetCursorPos;
    FunctionDetour m_detourClipCursor;

    bool m_swapChainAttached = false;
    bool m_factoryAttached = false;
    bool m_vrInitialized = false;
    bool m_wasInQuadMode = false;
    bool m_windowSubclassed = false;
    bool m_inSizeMove = false;
    bool m_windowClampDirty = true;
    int m_lastEnforcedWidth = 0;
    int m_lastEnforcedHeight = 0;
    HMONITOR m_lastMonitor = nullptr;
    uint32_t m_targetRenderWidth = 0;
    uint32_t m_targetRenderHeight = 0;

    IDXGISwapChain* m_cachedSwapChain = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_cachedSwapChain3;
};

} // namespace fs25vr

