#include "dxgi_proxy.h"
#include "core/logger.h"
#include <utility>

namespace fs25vr {

namespace {

template <typename PFN, typename... Args>
HRESULT ForwardFactoryCall(const char* funcName, PFN pfn, Args&&... args) {
    if (!pfn) {
        LogError(std::string("System ") + funcName + " pointer is null.");
        return E_FAIL;
    }
    return pfn(std::forward<Args>(args)...);
}

} // anonymous namespace

DxgiProxy& DxgiProxy::Instance() {
    static DxgiProxy instance;
    return instance;
}

DxgiProxy::DxgiProxy(std::shared_ptr<ISystemDxgi> systemDxgi,
                     std::shared_ptr<OpenXrManager> openXrManager,
                     std::shared_ptr<LuauInterceptor> luauInterceptor,
                     std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe,
                     std::shared_ptr<D3D12Interceptor> d3d12Interceptor)
    : m_systemDxgi(systemDxgi ? std::move(systemDxgi) : std::shared_ptr<ISystemDxgi>(&SystemDxgi::Instance(), [](auto*) {})),
      m_openXrManager(openXrManager ? std::move(openXrManager) : std::make_shared<OpenXrManager>()),
      m_luauInterceptor(luauInterceptor ? std::move(luauInterceptor) : std::shared_ptr<LuauInterceptor>(&LuauInterceptor::Instance(), [](auto*) {})),
      m_telemetryProbe(telemetryProbe ? std::move(telemetryProbe) : std::shared_ptr<GraphicsTelemetryProbe>(&GraphicsTelemetryProbe::Instance(), [](auto*) {})),
      m_d3d12Interceptor(d3d12Interceptor ? std::move(d3d12Interceptor) : std::shared_ptr<D3D12Interceptor>(&D3D12Interceptor::Instance(), [](auto*) {})) {
}

void DxgiProxy::EnsureVrInitialized() {
    if (!m_vrInitialized) {
        m_vrInitialized = true;
        LogInfo("Intercepted DXGI startup sequence. Initializing OpenXR subsystem...");
        m_openXrManager->Initialize();
        if (m_openXrManager->IsPassThrough()) {
            LogInfo("Injector operating in Pass-Through Mode (forwarding all calls silently).");
        } else if (m_openXrManager->IsActive()) {
            LogInfo("Injector operating in Active VR Mode.");
        }
        if (m_luauInterceptor) {
            m_luauInterceptor->AttachToEngine();
        }
        LogInfo("In-process Luau state interception channel armed.");
        if (m_d3d12Interceptor) {
            m_d3d12Interceptor->SetTelemetryProbe(m_telemetryProbe);
        }
        LogInfo("Direct3D 12 stereo telemetry probe armed.");
    }
}

void DxgiProxy::OnPrePresent(IDXGISwapChain* /*swapChain*/) {
    if (m_telemetryProbe) {
        m_telemetryProbe->OnFrameBoundary();
    }
}

HRESULT DxgiProxy::CreateDXGIFactory(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    return ForwardFactoryCall("CreateDXGIFactory", m_systemDxgi->GetCreateDXGIFactory(), riid, ppFactory);
}

HRESULT DxgiProxy::CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    return ForwardFactoryCall("CreateDXGIFactory1", m_systemDxgi->GetCreateDXGIFactory1(), riid, ppFactory);
}

HRESULT DxgiProxy::CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    EnsureVrInitialized();
    return ForwardFactoryCall("CreateDXGIFactory2", m_systemDxgi->GetCreateDXGIFactory2(), Flags, riid, ppFactory);
}

HRESULT DxgiProxy::DXGIDeclareAdapterRemovalSupport() {
    auto pfn = m_systemDxgi->GetDXGIDeclareAdapterRemovalSupport();
    return pfn ? pfn() : E_FAIL;
}

HRESULT DxgiProxy::DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    auto pfn = m_systemDxgi->GetDXGIGetDebugInterface1();
    return pfn ? pfn(Flags, riid, pDebug) : E_FAIL;
}

} // namespace fs25vr

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

HRESULT WINAPI Proxy_DXGID3D10CreateDevice(HMODULE hModule, IDXGIAdapter* pAdapter, UINT Flags, DWORD RuntimeVersion, void* pOutputs) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(HMODULE, IDXGIAdapter*, UINT, DWORD, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10CreateDevice"));
    return pfn ? pfn(hModule, pAdapter, Flags, RuntimeVersion, pOutputs) : E_FAIL;
}

HRESULT WINAPI Proxy_DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(void*, void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10CreateLayeredDevice"));
    return pfn ? pfn(a, b, c, d, e) : E_FAIL;
}

SIZE_T WINAPI Proxy_DXGID3D10GetLayeredDeviceSize(const void* pConfiguration, UINT Size) {
    auto pfn = reinterpret_cast<SIZE_T(WINAPI*)(const void*, UINT)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10GetLayeredDeviceSize"));
    return pfn ? pfn(pConfiguration, Size) : 0;
}

HRESULT WINAPI Proxy_DXGID3D10RegisterLayers(const void* pLayers, UINT NumLayers) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(const void*, UINT)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGID3D10RegisterLayers"));
    return pfn ? pfn(pLayers, NumLayers) : E_FAIL;
}

HRESULT WINAPI Proxy_DXGIDisableVBlankVirtualization() {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)()>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGIDisableVBlankVirtualization"));
    return pfn ? pfn() : E_FAIL;
}

HRESULT WINAPI Proxy_DXGIReportAdapterConfiguration(void* pConf) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGIReportAdapterConfiguration"));
    return pfn ? pfn(pConf) : E_FAIL;
}

void WINAPI Proxy_DXGIDumpJournal(void* pJournal) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("DXGIDumpJournal"));
    if (pfn) pfn(pJournal);
}

void WINAPI Proxy_PIXBeginCapture(DWORD captureFlags, const void* captureArgs) {
    auto pfn = reinterpret_cast<void(WINAPI*)(DWORD, const void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("PIXBeginCapture"));
    if (pfn) pfn(captureFlags, captureArgs);
}

void WINAPI Proxy_PIXEndCapture() {
    auto pfn = reinterpret_cast<void(WINAPI*)()>(
        fs25vr::SystemDxgi::Instance().GetExport("PIXEndCapture"));
    if (pfn) pfn();
}

DWORD WINAPI Proxy_PIXGetCaptureState() {
    auto pfn = reinterpret_cast<DWORD(WINAPI*)()>(
        fs25vr::SystemDxgi::Instance().GetExport("PIXGetCaptureState"));
    return pfn ? pfn() : 0;
}

HRESULT WINAPI Proxy_SetAppCompatStringPointer(DWORD unknown, const char* str) {
    auto pfn = reinterpret_cast<HRESULT(WINAPI*)(DWORD, const char*)>(
        fs25vr::SystemDxgi::Instance().GetExport("SetAppCompatStringPointer"));
    return pfn ? pfn(unknown, str) : E_FAIL;
}

void WINAPI Proxy_UpdateHMDEmulationStatus(BOOL status) {
    auto pfn = reinterpret_cast<void(WINAPI*)(BOOL)>(
        fs25vr::SystemDxgi::Instance().GetExport("UpdateHMDEmulationStatus"));
    if (pfn) pfn(status);
}

void WINAPI Proxy_ApplyCompatResolutionQuirking(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("ApplyCompatResolutionQuirking"));
    if (pfn) pfn(a, b, c, d);
}

void WINAPI Proxy_CompatString(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("CompatString"));
    if (pfn) pfn(a, b, c, d);
}

void WINAPI Proxy_CompatValue(void* a, void* b, void* c, void* d) {
    auto pfn = reinterpret_cast<void(WINAPI*)(void*, void*, void*, void*)>(
        fs25vr::SystemDxgi::Instance().GetExport("CompatValue"));
    if (pfn) pfn(a, b, c, d);
}

} // extern "C"
