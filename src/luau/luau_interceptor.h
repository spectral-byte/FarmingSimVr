#pragma once

#include <memory>
#include "vr_bridge.h"
#include "core/detour.h"

namespace fs25vr {

class LuauInterceptor {
public:
    // A null bridge leaves the interceptor with nothing to inject; there is no singleton fallback.
    // Production wiring comes from InjectorRuntime.
    explicit LuauInterceptor(std::shared_ptr<VRBridge> bridge = nullptr);
    ~LuauInterceptor();

    // Attach installs the detour only while a live InjectorRuntime owns the interceptor, so the hook
    // can always find the interceptor that holds its original.
    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }
    bool Attach(void* targetQueryFunction);
    bool AttachToEngine();
    void Detach();

    bool IsIntercepted() const { return m_activeState != nullptr; }
    lua_State* GetActiveState() const { return m_activeState; }

    int HandleQuery(lua_State* L);

    // Hook entry point: forwards to the interceptor that installed the detour.
    static int DetourEntryPoint(lua_State* L);
    static void* FindQueryFunctionInText(const uint8_t* textBase, size_t textSize, const uint8_t* stringAddr);

    FunctionDetour& GetDetour() { return m_detour; }

private:
    std::shared_ptr<VRBridge> m_bridge;
    FunctionDetour m_detour;
    lua_State* m_activeState = nullptr;
    bool m_bridgeInjected = false;
    bool m_ownedByRuntime = false;
};

} // namespace fs25vr
