#include "dxgi_proxy.h"
#include "core/logger.h"

namespace fs25vr {

DxgiProxy& DxgiProxy::Instance() {
    static DxgiProxy instance;
    return instance;
}

DxgiProxy::DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi,
                     std::shared_ptr<OpenXrManager> openXrManager)
    : m_systemDxgi(systemDxgi ? std::move(systemDxgi) : std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {})),
      m_openXrManager(openXrManager ? std::move(openXrManager) : std::make_shared<OpenXrManager>()) {
}

void DxgiProxy::EnsureVrInitialized() {
    if (!m_vrInitialized) {
        m_vrInitialized = true;
        LogInfo("Intercepted DXGI startup sequence. Initializing OpenXR subsystem...");
        m_openXrManager->Initialize();
        if (m_openXrManager->IsPassThrough()) {
            LogInfo("DXGI Proxy operating in Pass-Through Mode (forwarding all calls silently).");
        } else if (m_openXrManager->IsActive()) {
            LogInfo("DXGI Proxy operating in Active VR Mode.");
        }
    }
}

HRESULT DxgiProxy::CreateDXGIFactory(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    auto pfn = m_systemDxgi->GetCreateDXGIFactory();
    if (!pfn) {
        LogError("System CreateDXGIFactory pointer is null.");
        return E_FAIL;
    }
    return pfn(riid, ppFactory);
}

HRESULT DxgiProxy::CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    auto pfn = m_systemDxgi->GetCreateDXGIFactory1();
    if (!pfn) {
        LogError("System CreateDXGIFactory1 pointer is null.");
        return E_FAIL;
    }
    return pfn(riid, ppFactory);
}

HRESULT DxgiProxy::CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    auto pfn = m_systemDxgi->GetCreateDXGIFactory2();
    if (!pfn) {
        LogError("System CreateDXGIFactory2 pointer is null.");
        return E_FAIL;
    }
    return pfn(Flags, riid, ppFactory);
}

HRESULT DxgiProxy::DXGIDeclareAdapterRemovalSupport() {
    auto pfn = m_systemDxgi->GetDXGIDeclareAdapterRemovalSupport();
    if (!pfn) {
        return E_FAIL;
    }
    return pfn();
}

HRESULT DxgiProxy::DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    auto pfn = m_systemDxgi->GetDXGIGetDebugInterface1();
    if (!pfn) {
        return E_FAIL;
    }
    return pfn(Flags, riid, pDebug);
}

} // namespace fs25vr

#define FORWARD_EXPORT(name) \
    extern "C" void WINAPI Proxy_##name() { \
        auto pfn = fs25vr::SystemDxgi::Instance().GetExport(#name); \
        if (pfn) { \
            reinterpret_cast<void(WINAPI*)()>(pfn)(); \
        } \
    }

FORWARD_EXPORT(ApplyCompatResolutionQuirking)
FORWARD_EXPORT(CompatString)
FORWARD_EXPORT(CompatValue)
FORWARD_EXPORT(DXGID3D10CreateDevice)
FORWARD_EXPORT(DXGID3D10CreateLayeredDevice)
FORWARD_EXPORT(DXGID3D10GetLayeredDeviceSize)
FORWARD_EXPORT(DXGID3D10RegisterLayers)
FORWARD_EXPORT(DXGIDisableVBlankVirtualization)
FORWARD_EXPORT(DXGIDumpJournal)
FORWARD_EXPORT(DXGIReportAdapterConfiguration)
FORWARD_EXPORT(PIXBeginCapture)
FORWARD_EXPORT(PIXEndCapture)
FORWARD_EXPORT(PIXGetCaptureState)
FORWARD_EXPORT(SetAppCompatStringPointer)
FORWARD_EXPORT(UpdateHMDEmulationStatus)

extern "C" {

HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    return fs25vr::DxgiProxy::Instance().CreateDXGIFactory(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    return fs25vr::DxgiProxy::Instance().CreateDXGIFactory1(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    return fs25vr::DxgiProxy::Instance().CreateDXGIFactory2(Flags, riid, ppFactory);
}

HRESULT WINAPI Proxy_DXGIDeclareAdapterRemovalSupport() {
    return fs25vr::DxgiProxy::Instance().DXGIDeclareAdapterRemovalSupport();
}

HRESULT WINAPI Proxy_DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    return fs25vr::DxgiProxy::Instance().DXGIGetDebugInterface1(Flags, riid, pDebug);
}

}
