#include "core/injector_runtime.h"

#include "core/dll_lifecycle.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "ngx/ngx_interceptor.h"
#include "openxr/openxr_manager.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/mirror_window_controller.h"
#include "proxy/system_dxgi.h"
#include "telemetry/d3d12_interceptor.h"
#include "telemetry/graphics_telemetry_probe.h"

namespace fs25vr {

namespace {

// Wraps a process-lifetime singleton without taking ownership.
template <typename T>
std::shared_ptr<T> Borrow(T& singleton) {
    return std::shared_ptr<T>(&singleton, [](T*) {});
}

template <typename T>
void FillFromSingleton(std::shared_ptr<T>& slot) {
    if (!slot) {
        slot = Borrow(T::Instance());
    }
}

} // namespace

InjectorRuntime& InjectorRuntime::Instance() {
    static InjectorRuntime runtime;
    return runtime;
}

InjectorRuntime::InjectorRuntime(Dependencies deps) : m_deps(std::move(deps)) {
    // Only an injected NGX interceptor is wired to this runtime. The NGX singleton already falls back
    // to the production subsystems behind IsActive() checks; overwriting its wiring would bypass
    // those checks (Pass-Through would see the mirror target resolution) and let any other runtime
    // rewire or unwire the production one.
    m_wiresNgxInterceptor = static_cast<bool>(m_deps.ngxInterceptor);

    if (!m_deps.systemDxgi) m_deps.systemDxgi = Borrow<ISystemDxgi>(SystemDxgi::Instance());
    FillFromSingleton(m_deps.openXrManager);
    FillFromSingleton(m_deps.vrBridge);
    FillFromSingleton(m_deps.luauInterceptor);
    FillFromSingleton(m_deps.telemetryProbe);
    FillFromSingleton(m_deps.d3d12Interceptor);
    FillFromSingleton(m_deps.windowController);
    FillFromSingleton(m_deps.ngxInterceptor);

    m_proxy = std::make_unique<DxgiProxy>(
        m_deps.systemDxgi, m_deps.openXrManager, m_deps.luauInterceptor, m_deps.telemetryProbe,
        m_deps.d3d12Interceptor, m_deps.vrBridge, m_deps.windowController);

    if (m_wiresNgxInterceptor) {
        m_deps.ngxInterceptor->SetVRBridge(m_deps.vrBridge.get());
        m_deps.ngxInterceptor->SetOpenXrManager(m_deps.openXrManager.get());
        m_deps.ngxInterceptor->SetDxgiProxy(m_proxy.get());
    }
}

InjectorRuntime::~InjectorRuntime() {
    // The setters take the NGX mutex, which a thread killed at termination may still own.
    if (!m_wiresNgxInterceptor || IsProcessTerminating()) return;
    // Do not leave the NGX interceptor pointing at objects we are about to free.
    m_deps.ngxInterceptor->SetDxgiProxy(nullptr);
    m_deps.ngxInterceptor->SetVRBridge(nullptr);
    m_deps.ngxInterceptor->SetOpenXrManager(nullptr);
}

} // namespace fs25vr
