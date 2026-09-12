#pragma once

#include "system_dxgi.h"
#include "openxr/openxr_manager.h"
#include "luau/luau_interceptor.h"
#include "telemetry/graphics_telemetry_probe.h"
#include "telemetry/d3d12_interceptor.h"
#include <memory>

namespace fs25vr {

class DxgiProxy {
public:
    static DxgiProxy& Instance();

    explicit DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi = nullptr,
                       std::shared_ptr<OpenXrManager> openXrManager = nullptr,
                       std::shared_ptr<LuauInterceptor> luauInterceptor = nullptr,
                       std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe = nullptr,
                       std::shared_ptr<D3D12Interceptor> d3d12Interceptor = nullptr);

    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory);
    HRESULT DXGIDeclareAdapterRemovalSupport();
    HRESULT DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug);
    void OnPrePresent(IDXGISwapChain* swapChain);

    OpenXrManager& GetOpenXrManager() { return *m_openXrManager; }
    ISystemDxgi& GetSystemDxgi() { return *m_systemDxgi; }
    LuauInterceptor& GetLuauInterceptor() { return *m_luauInterceptor; }
    GraphicsTelemetryProbe& GetTelemetryProbe() { return *m_telemetryProbe; }
    D3D12Interceptor& GetD3D12Interceptor() { return *m_d3d12Interceptor; }

private:
    void EnsureVrInitialized();

    std::shared_ptr<ISystemDxgi> m_systemDxgi;
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<LuauInterceptor> m_luauInterceptor;
    std::shared_ptr<GraphicsTelemetryProbe> m_telemetryProbe;
    std::shared_ptr<D3D12Interceptor> m_d3d12Interceptor;
    bool m_vrInitialized = false;
};

} // namespace fs25vr
