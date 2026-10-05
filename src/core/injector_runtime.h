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
// runtime with fakes. Any subsystem left null in Dependencies is created here, so the runtime is the
// only place subsystems are built and no subsystem has a singleton accessor of its own. Inject every
// subsystem a test observes; the others are fresh, real ones wired to the ones supplied.
// The NGX interceptor is wired to the runtime's OpenXR manager, VRBridge and DXGI proxy, and reaches
// no other subsystem.
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

    // Process-wide runtime, created on first use.
    static InjectorRuntime& Instance();

    // The most recently constructed runtime that is still alive, whatever order runtimes are
    // destroyed in; null when none is alive. Never falls back to Instance(). Lock-free.
    static InjectorRuntime* Current();

    // The runtime a hook entry point acts for: the newest live runtime whose subsystem installed the
    // hook (installed(runtime) is true), else Current(). installed may run under the registry lock, so
    // it must only read detour state.
    template <typename Pred>
    static InjectorRuntime* ForHook(Pred installed) {
        // Lock-free fast path for the hot hooks (Present, GetClientRect, ...): Current() is the newest
        // live runtime, so when it installed the hook it is the answer. Production has one runtime.
        InjectorRuntime* current = Current();
        if (current && installed(*current)) return current;
        InjectorRuntime* owner = FindLive(&installed, [](void* ctx, InjectorRuntime& runtime) {
            return (*static_cast<Pred*>(ctx))(runtime);
        });
        return owner ? owner : Current();
    }

    explicit InjectorRuntime(Dependencies deps = {});
    ~InjectorRuntime();

    InjectorRuntime(const InjectorRuntime&) = delete;
    InjectorRuntime& operator=(const InjectorRuntime&) = delete;

    ISystemDxgi& GetSystemDxgi() { return *m_deps.systemDxgi; }
    DxgiProxy& GetDxgiProxy() { return *m_proxy; }
    OpenXrManager& GetOpenXrManager() { return *m_deps.openXrManager; }
    VRBridge& GetVrBridge() { return *m_deps.vrBridge; }
    LuauInterceptor& GetLuauInterceptor() { return *m_deps.luauInterceptor; }
    MirrorWindowController& GetWindowController() { return *m_deps.windowController; }
    NgxInterceptor& GetNgxInterceptor() { return *m_deps.ngxInterceptor; }

private:
    // The newest live runtime for which pred(ctx, runtime) holds, or null.
    static InjectorRuntime* FindLive(void* ctx, bool (*pred)(void*, InjectorRuntime&));

    Dependencies m_deps;
    std::unique_ptr<DxgiProxy> m_proxy;
};

} // namespace fs25vr
