#pragma once

#include "system_dxgi.h"
#include "openxr/openxr_manager.h"
#include "luau/luau_interceptor.h"
#include <memory>

namespace fs25vr {

class DxgiProxy {
public:
    static DxgiProxy& Instance();

    explicit DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi = nullptr,
                       std::shared_ptr<OpenXrManager> openXrManager = nullptr,
                       std::shared_ptr<LuauInterceptor> luauInterceptor = nullptr);

    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory);
    HRESULT CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory);
    HRESULT DXGIDeclareAdapterRemovalSupport();
    HRESULT DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug);

    OpenXrManager& GetOpenXrManager() { return *m_openXrManager; }
    ISystemDxgi& GetSystemDxgi() { return *m_systemDxgi; }
    LuauInterceptor& GetLuauInterceptor() { return *m_luauInterceptor; }

private:
    void EnsureVrInitialized();

    std::shared_ptr<ISystemDxgi> m_systemDxgi;
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<LuauInterceptor> m_luauInterceptor;
    bool m_vrInitialized = false;
};

} // namespace fs25vr
