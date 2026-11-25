#pragma once

#include "core/injector_version.h"
#include <filesystem>
#include <memory>
#include <string>

namespace fs25vr {

class ISystemDxgi;
class OpenXrManager;
class VRBridge;
class LuauInterceptor;
class MirrorWindowController;
class IDesktopCursorPort;
class NgxInterceptor;
class FidelityFxInterceptor;
class DxgiProxy;

class InjectorRuntime {
public:
    struct Dependencies {
        std::shared_ptr<ISystemDxgi> systemDxgi;
        std::shared_ptr<OpenXrManager> openXrManager;
        std::shared_ptr<VRBridge> vrBridge;
        std::shared_ptr<LuauInterceptor> luauInterceptor;
        std::shared_ptr<MirrorWindowController> windowController;
        std::shared_ptr<IDesktopCursorPort> cursorPort;
        std::shared_ptr<NgxInterceptor> ngxInterceptor;
        std::shared_ptr<FidelityFxInterceptor> ffxInterceptor;
        std::filesystem::path companionSettingsFile;
        void* hostModule = nullptr;
        std::string injectorVersion{kInjectorVersion};
    };

    static InjectorRuntime& Instance();

    static InjectorRuntime* Current();

    template <typename Pred>
    static InjectorRuntime* ForHook(Pred installed) {
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
    FidelityFxInterceptor& GetFidelityFxInterceptor() { return *m_deps.ffxInterceptor; }

private:
    static InjectorRuntime* FindLive(void* ctx, bool (*pred)(void*, InjectorRuntime&));

    Dependencies m_deps;
    std::unique_ptr<DxgiProxy> m_proxy;
};

}
