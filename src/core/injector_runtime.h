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
// runtime with fakes. The window controller and Luau interceptor are created here when not injected.
// Any other subsystem left null in Dependencies falls back to the existing singleton (wrapped in a
// no-op deleter), so those singletons keep working until they are migrated. Those singletons are
// wired to each other, not to injected fakes: inject every subsystem a test observes.
// The NGX interceptor is wired to the runtime's OpenXR manager, VRBridge and DXGI proxy, and reaches
// no other subsystem. The production runtime wires the NGX singleton; a test runtime wires only an
// NGX interceptor it was given, leaving the singleton as it is.
//
// Hook entry points are plain function pointers with no context argument, so they find their
// subsystem through the registry of live runtimes (ForHook). The DXGI, Win32 and Luau subsystems
// install detours only while a live runtime owns them, and a runtime removes those detours before it
// leaves the registry, so a hook always finds the subsystem that holds its original function.
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

    // The most recently constructed runtime that is still alive, whatever order runtimes are
    // destroyed in; null when none is alive. Never falls back to Instance(). Lock-free.
    static InjectorRuntime* Current();

    // The runtime a hook entry point acts for: the newest live runtime whose subsystem installed the
    // hook (installed(runtime) is true), else Current(). installed runs under the registry lock, so it
    // must only read detour state.
    template <typename Pred>
    static InjectorRuntime* ForHook(Pred installed) {
        InjectorRuntime* owner = FindLive(&installed, [](void* ctx, InjectorRuntime& runtime) {
            return (*static_cast<Pred*>(ctx))(runtime);
        });
        return owner ? owner : Current();
    }

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

    // The newest live runtime for which pred(ctx, runtime) holds, or null.
    static InjectorRuntime* FindLive(void* ctx, bool (*pred)(void*, InjectorRuntime&));

    Dependencies m_deps;
    std::unique_ptr<DxgiProxy> m_proxy;
    bool m_wiresNgxInterceptor = false;
};

} // namespace fs25vr
