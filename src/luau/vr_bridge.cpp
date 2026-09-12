#include "vr_bridge.h"
#include "core/logger.h"

namespace fs25vr {

VRBridge& VRBridge::Instance() {
    static VRBridge instance;
    return instance;
}

VRBridge::VRBridge(std::shared_ptr<OpenXrManager> openXrManager)
    : m_openXrManager(openXrManager ? std::move(openXrManager) : std::make_shared<OpenXrManager>()) {
}

bool VRBridge::IsInjected(lua_State* L) {
    if (!L) return false;
    int origTop = lua_gettop(L);
    lua_getglobal(L, "VRBridge");
    bool exists = (lua_type(L, -1) == LUA_TTABLE);
    lua_settop(L, origTop);
    return exists;
}

bool VRBridge::Inject(lua_State* L) {
    if (!L) {
        LogError("VRBridge::Inject: lua_State pointer is null.");
        return false;
    }

    if (IsInjected(L)) {
        return true;
    }

    int origTop = lua_gettop(L);

    // Create table for VRBridge
    lua_createtable(L, 0, 4);

    // VRBridge.version = "1.0.0"
    lua_pushstring(L, m_version.c_str());
    lua_setfield(L, -2, "version");

    // VRBridge.isReady = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_IsReady, "isReady", 1);
    lua_setfield(L, -2, "isReady");

    // VRBridge.handshake = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_Handshake, "handshake", 1);
    lua_setfield(L, -2, "handshake");

    // Set as global variable _G["VRBridge"]
    lua_setglobal(L, "VRBridge");

    // Strictly restore stack state to guarantee zero stack leakage
    lua_settop(L, origTop);

    LogInfo("VRBridge: Successfully injected global VRBridge table into Luau environment.");
    return true;
}

bool VRBridge::IsReady() const {
    return m_openXrManager ? m_openXrManager->IsActive() : false;
}

bool VRBridge::PerformHandshake() {
    m_handshakeCompleted = true;
    LogInfo("[VRBridge] Native-to-Luau handshake completed successfully.");
    return true;
}

int VRBridge::Lua_IsReady(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) self = &Instance();

    lua_pushboolean(L, self->IsReady() ? 1 : 0);
    return 1;
}

int VRBridge::Lua_Handshake(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) self = &Instance();

    self->PerformHandshake();

    lua_createtable(L, 0, 3);

    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "success");

    lua_pushstring(L, self->GetVersion().c_str());
    lua_setfield(L, -2, "version");

    const char* modeStr = self->IsReady() ? "Active" : "PassThrough";
    lua_pushstring(L, modeStr);
    lua_setfield(L, -2, "mode");

    return 1;
}

} // namespace fs25vr
