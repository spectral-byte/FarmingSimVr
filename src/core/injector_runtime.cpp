#include "core/injector_runtime.h"

#include "core/dll_lifecycle.h"
#include "luau/luau_interceptor.h"
#include "luau/vr_bridge.h"
#include "ngx/ngx_interceptor.h"
#include "openxr/openxr_manager.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/mirror_window_controller.h"
#include "proxy/system_dxgi.h"

#include <algorithm>
#include <atomic>
#include <shared_mutex>
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
    static InjectorRuntime runtime{ProductionTag{}};
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

InjectorRuntime::InjectorRuntime(ProductionTag) : InjectorRuntime(Dependencies{}, /*wiresNgxInterceptor=*/true) {}

// Only a runtime that owns the NGX interceptor wires it: the production runtime (the NGX singleton) or a
// test runtime given its own. Any other runtime would rewire the production interceptor to its fakes
// and unwire it on destruction.
InjectorRuntime::InjectorRuntime(Dependencies deps)
    : InjectorRuntime(deps, static_cast<bool>(deps.ngxInterceptor)) {}

InjectorRuntime::InjectorRuntime(Dependencies deps, bool wiresNgxInterceptor)
    : m_deps(std::move(deps)), m_wiresNgxInterceptor(wiresNgxInterceptor) {
    if (!m_deps.systemDxgi) m_deps.systemDxgi = Borrow<ISystemDxgi>(SystemDxgi::Instance());
    FillFromSingleton(m_deps.openXrManager);
    FillFromSingleton(m_deps.vrBridge);
    if (!m_deps.luauInterceptor) m_deps.luauInterceptor = std::make_shared<LuauInterceptor>(m_deps.vrBridge);
    if (!m_deps.windowController) m_deps.windowController = std::make_shared<MirrorWindowController>();
    FillFromSingleton(m_deps.ngxInterceptor);

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

    if (m_wiresNgxInterceptor) {
        m_deps.ngxInterceptor->SetVRBridge(m_deps.vrBridge.get());
        m_deps.ngxInterceptor->SetOpenXrManager(m_deps.openXrManager.get());
        m_deps.ngxInterceptor->SetDxgiProxy(m_proxy.get());
    }
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
    m_proxy->DetachAll();
    m_deps.luauInterceptor->Detach();

    Unregister(this, /*mayBlock=*/true);

    if (!m_wiresNgxInterceptor) return;
    // Do not leave the NGX interceptor pointing at objects we are about to free.
    m_deps.ngxInterceptor->SetDxgiProxy(nullptr);
    m_deps.ngxInterceptor->SetVRBridge(nullptr);
    m_deps.ngxInterceptor->SetOpenXrManager(nullptr);
}

} // namespace fs25vr
