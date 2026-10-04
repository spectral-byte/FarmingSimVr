#pragma once

#include <memory>

namespace fs25vr {

class ISystemDxgi;
class OpenXrManager;
class VRBridge;
class LuauInterceptor;
class MirrorWindowController;
class NgxInterceptor;
class DxgiProxy;

// Composition root: constructs and wires every Injector subsystem.
//
// Production uses Instance(), created on the first call into a DXGI export. Tests construct their own
// runtime with fakes. A subsystem left null in Dependencies falls back to the existing singleton
// (wrapped in a no-op deleter), so the singletons keep working during the expand phase. Those
// singletons are wired to each other, not to injected fakes: inject every subsystem a test observes.
// The NGX interceptor is wired to the runtime's OpenXR manager, VRBridge and DXGI proxy, and reaches
// no other subsystem. The production runtime wires the NGX singleton; a test runtime wires only an
// NGX interceptor it was given, leaving the singleton as it is.
class InjectorRuntime {
public:
    struct Dependencies {
        std::shared_ptr<ISystemDxgi> systemDxgi;
        std::shared_ptr<OpenXrManager> openXrManager;
        std::shared_ptr<VRBridge> vrBridge;
        std::shared_ptr<LuauInterceptor> luauInterceptor;
        std::shared_ptr<MirrorWindowController> windowController;
        std::shared_ptr<NgxInterceptor> ngxInterceptor;
    };

    // Process-wide runtime, created on first use from the singletons.
    static InjectorRuntime& Instance();

    explicit InjectorRuntime(Dependencies deps = {});
    ~InjectorRuntime();

    InjectorRuntime(const InjectorRuntime&) = delete;
    InjectorRuntime& operator=(const InjectorRuntime&) = delete;

    DxgiProxy& GetDxgiProxy() { return *m_proxy; }
    OpenXrManager& GetOpenXrManager() { return *m_deps.openXrManager; }
    VRBridge& GetVrBridge() { return *m_deps.vrBridge; }
    LuauInterceptor& GetLuauInterceptor() { return *m_deps.luauInterceptor; }
    MirrorWindowController& GetWindowController() { return *m_deps.windowController; }
    NgxInterceptor& GetNgxInterceptor() { return *m_deps.ngxInterceptor; }

private:
    struct ProductionTag {};
    explicit InjectorRuntime(ProductionTag);
    InjectorRuntime(Dependencies deps, bool wiresNgxInterceptor);

    Dependencies m_deps;
    std::unique_ptr<DxgiProxy> m_proxy;
    bool m_wiresNgxInterceptor = false;
};

} // namespace fs25vr
