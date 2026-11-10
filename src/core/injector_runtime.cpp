#include "core/injector_runtime.h"

#include "core/companion_settings.h"
#include "core/dll_lifecycle.h"
#include "core/logger.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "ngx/ngx_interceptor.h"
#include "fidelityfx/fidelityfx_interceptor.h"
#include "openxr/openxr_manager.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/mirror_window_controller.h"
#include "proxy/system_dxgi.h"

#include <algorithm>
#include <atomic>
#include <shared_mutex>
#include <sstream>
#include <vector>

namespace fs25vr {

namespace {

// Live runtimes, oldest first. g_current mirrors the newest so Current() stays lock-free.
struct LiveRuntimes {
    std::shared_mutex mutex;
    std::vector<InjectorRuntime*> runtimes;
};

// Function-local so it is constructed before, and destroyed after, the first runtime.
LiveRuntimes& Live() {
    static LiveRuntimes live;
    return live;
}

std::atomic<InjectorRuntime*> g_current{nullptr};

// Removes a destroyed runtime. When blocking is not allowed (process termination, where a killed
// thread may hold the lock) the entry is left behind if the lock is taken: no hook runs after that.
void Unregister(InjectorRuntime* runtime, bool mayBlock) {
    LiveRuntimes& live = Live();
    std::unique_lock lock(live.mutex, std::defer_lock);
    if (mayBlock) {
        lock.lock();
    } else if (!lock.try_lock()) {
        return;
    }
    live.runtimes.erase(std::remove(live.runtimes.begin(), live.runtimes.end(), runtime), live.runtimes.end());
    g_current.store(live.runtimes.empty() ? nullptr : live.runtimes.back());
}

// The saved Menu Distance, so the main menu is shown at it before the Companion Mod runs. A value the Companion Mod
// would refuse (out of range) is not used, as the mod does not use it either.
void ApplySavedMenuDistance(VRBridge& bridge, const std::filesystem::path& settingsFile) {
    const std::optional<float> saved = ReadCompanionSettingFloat(settingsFile, "menuDistance");
    std::ostringstream ss;
    if (!saved) {
        ss << "[InjectorRuntime] No saved Menu Distance; menus are shown at " << bridge.GetMenuDistance() << " m.";
    } else if (*saved < OpenXrManager::kMinMenuQuadDistance || *saved > OpenXrManager::kMaxMenuQuadDistance) {
        ss << "[InjectorRuntime] Saved Menu Distance " << *saved << " m is out of range; menus are shown at "
           << bridge.GetMenuDistance() << " m.";
    } else {
        bridge.SetMenuDistance(*saved);
        ss << "[InjectorRuntime] Menus are shown at the saved Menu Distance of " << bridge.GetMenuDistance() << " m.";
    }
    LogInfo(ss.str());
}

} // namespace

InjectorRuntime& InjectorRuntime::Instance() {
    static InjectorRuntime runtime([] {
        Dependencies deps;
        deps.companionSettingsFile = DefaultCompanionSettingsPath();
        return deps;
    }());
    return runtime;
}

InjectorRuntime* InjectorRuntime::Current() {
    return g_current.load();
}

InjectorRuntime* InjectorRuntime::FindLive(void* ctx, bool (*pred)(void*, InjectorRuntime&)) {
    LiveRuntimes& live = Live();
    std::shared_lock lock(live.mutex);
    for (auto it = live.runtimes.rbegin(); it != live.runtimes.rend(); ++it) {
        if (pred(ctx, **it)) return *it;
    }
    return nullptr;
}

InjectorRuntime::InjectorRuntime(Dependencies deps) : m_deps(std::move(deps)) {
    // Every subsystem the caller did not supply is created here, so the runtime owns the whole graph.
    // A supplied VRBridge brings its collaborators: adopt them, so the bridge and the hooks share one
    // OpenXR manager and one NGX interceptor.
    if (m_deps.vrBridge) {
        if (!m_deps.openXrManager) m_deps.openXrManager = m_deps.vrBridge->GetOpenXrManager();
        if (!m_deps.ngxInterceptor) m_deps.ngxInterceptor = m_deps.vrBridge->GetNgxInterceptor();
    }
    if (!m_deps.systemDxgi) m_deps.systemDxgi = std::make_shared<SystemDxgi>();
    if (!m_deps.openXrManager) {
        m_deps.openXrManager = std::make_shared<OpenXrManager>(std::make_shared<NativeOpenXrDispatch>());
    }
    if (!m_deps.ngxInterceptor) m_deps.ngxInterceptor = std::make_shared<NgxInterceptor>();
    if (!m_deps.ffxInterceptor) m_deps.ffxInterceptor = std::make_shared<FidelityFxInterceptor>();
    if (!m_deps.vrBridge) {
        m_deps.vrBridge = std::make_shared<VRBridge>(m_deps.openXrManager, m_deps.ngxInterceptor,
                                                     m_deps.injectorVersion);
    }
    if (!m_deps.companionSettingsFile.empty()) {
        ApplySavedMenuDistance(*m_deps.vrBridge, m_deps.companionSettingsFile);
    }
    if (!m_deps.luauInterceptor) m_deps.luauInterceptor = std::make_shared<LuauInterceptor>(m_deps.vrBridge);
    if (!m_deps.windowController) m_deps.windowController = std::make_shared<MirrorWindowController>(nullptr, m_deps.cursorPort);

    m_proxy = std::make_unique<DxgiProxy>(
        m_deps.systemDxgi, m_deps.openXrManager, m_deps.luauInterceptor,
        m_deps.vrBridge, m_deps.windowController, m_deps.ngxInterceptor);

    {
        LiveRuntimes& live = Live();
        std::unique_lock lock(live.mutex);
        live.runtimes.push_back(this);
        g_current.store(this);
    }
    // Detours may be installed only from here on, now that hooks can find this runtime.
    m_proxy->SetOwnedByRuntime(true);
    m_deps.windowController->SetOwnedByRuntime(true);
    m_deps.luauInterceptor->SetOwnedByRuntime(true);
    m_deps.vrBridge->GetVirtualGamepad().SetOwnedByRuntime(true);

    m_deps.ngxInterceptor->SetVRBridge(m_deps.vrBridge.get());
    m_deps.ngxInterceptor->SetOpenXrManager(m_deps.openXrManager.get());
    m_deps.ngxInterceptor->SetDxgiProxy(m_proxy.get());
    m_deps.ffxInterceptor->SetVRBridge(m_deps.vrBridge.get());
    m_deps.ngxInterceptor->SetFidelityFxInterceptor(m_deps.ffxInterceptor.get());
}

InjectorRuntime::~InjectorRuntime() {
    // A thread killed at termination may hold the detour, registry or NGX locks: take none of them.
    if (IsProcessTerminating()) {
        Unregister(this, /*mayBlock=*/false);
        return;
    }

    // Remove this runtime's detours while hooks can still find it, so none fires without its original.
    m_proxy->SetOwnedByRuntime(false);
    m_deps.windowController->SetOwnedByRuntime(false);
    m_deps.luauInterceptor->SetOwnedByRuntime(false);
    m_deps.vrBridge->GetVirtualGamepad().SetOwnedByRuntime(false);
    m_proxy->DetachAll();
    m_deps.luauInterceptor->Detach();

    Unregister(this, /*mayBlock=*/true);

    // Do not leave the NGX interceptor pointing at objects we are about to free.
    m_deps.ngxInterceptor->SetFidelityFxInterceptor(nullptr);
    m_deps.ffxInterceptor->DetachAll();
    m_deps.ngxInterceptor->SetDxgiProxy(nullptr);
    m_deps.ngxInterceptor->SetVRBridge(nullptr);
    m_deps.ngxInterceptor->SetOpenXrManager(nullptr);
}

} // namespace fs25vr
