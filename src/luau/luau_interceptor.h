#pragma once

#include <memory>
#include "vr_bridge.h"
#include "core/detour.h"

namespace fs25vr {

class LuauInterceptor {
public:
    static LuauInterceptor& Instance();

    explicit LuauInterceptor(std::shared_ptr<VRBridge> bridge = nullptr);
    ~LuauInterceptor();

    bool Attach(void* targetQueryFunction);
    bool AttachToEngine();
    void Detach();

    bool IsIntercepted() const { return m_activeState != nullptr; }
    lua_State* GetActiveState() const { return m_activeState; }

    int HandleQuery(lua_State* L);

    static int DetourEntryPoint(lua_State* L);

    FunctionDetour& GetDetour() { return m_detour; }

private:
    std::shared_ptr<VRBridge> m_bridge;
    FunctionDetour m_detour;
    lua_State* m_activeState = nullptr;
    bool m_bridgeInjected = false;
};

} // namespace fs25vr
