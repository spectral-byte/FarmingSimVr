#include "vr_bridge.h"
#include "core/logger.h"

namespace fs25vr {

VRBridge& VRBridge::Instance() {
    static VRBridge instance;
    return instance;
}

VRBridge::VRBridge(std::shared_ptr<OpenXrManager> openXrManager,
                   std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe)
    : m_openXrManager(openXrManager ? std::move(openXrManager) : std::shared_ptr<OpenXrManager>(&OpenXrManager::Instance(), [](auto*) {})),
      m_telemetryProbe(telemetryProbe ? std::move(telemetryProbe) : std::shared_ptr<GraphicsTelemetryProbe>(&GraphicsTelemetryProbe::Instance(), [](auto*) {})) {
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
    lua_createtable(L, 0, 11);

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

    // VRBridge.getStereoTelemetry = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_GetStereoTelemetry, "getStereoTelemetry", 1);
    lua_setfield(L, -2, "getStereoTelemetry");

    // VRBridge.isStereoActive = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_IsStereoActive, "isStereoActive", 1);
    lua_setfield(L, -2, "isStereoActive");

    // VRBridge.getHeadPose = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_GetHeadPose, "getHeadPose", 1);
    lua_setfield(L, -2, "getHeadPose");

    // VRBridge.recenter = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_Recenter, "recenter", 1);
    lua_setfield(L, -2, "recenter");

    // VRBridge.getEyeProjection = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_GetEyeProjection, "getEyeProjection", 1);
    lua_setfield(L, -2, "getEyeProjection");

    // VRBridge.getProjection = alias to getEyeProjection
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_GetEyeProjection, "getProjection", 1);
    lua_setfield(L, -2, "getProjection");

    // VRBridge.getViews = function() [bound with 'this' upvalue]
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_GetViews, "getViews", 1);
    lua_setfield(L, -2, "getViews");

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

int VRBridge::Lua_GetStereoTelemetry(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto* probe = (self && self->m_telemetryProbe) ? self->m_telemetryProbe.get() : &GraphicsTelemetryProbe::Instance();

    ProbeSnapshot snap = probe->GetSnapshot();

    lua_createtable(L, 0, 7);

    lua_pushinteger(L, static_cast<int>(snap.frameIndex));
    lua_setfield(L, -2, "frameIndex");

    lua_pushinteger(L, static_cast<int>(snap.maxViewports));
    lua_setfield(L, -2, "maxViewports");

    lua_pushinteger(L, static_cast<int>(snap.passCount));
    lua_setfield(L, -2, "passCount");

    lua_pushboolean(L, snap.nativeStereo ? 1 : 0);
    lua_setfield(L, -2, "nativeStereo");

    lua_pushboolean(L, snap.afrFallback ? 1 : 0);
    lua_setfield(L, -2, "afrFallback");

    const char* verdictStr = "Inconclusive";
    switch (snap.verdict) {
    case StereoVerdict::NativeStereoDetected: verdictStr = "NativeStereoDetected"; break;
    case StereoVerdict::AfrFallbackRequired: verdictStr = "AfrFallbackRequired"; break;
    default: break;
    }
    lua_pushstring(L, verdictStr);
    lua_setfield(L, -2, "verdict");

    lua_pushstring(L, snap.summary.c_str());
    lua_setfield(L, -2, "summary");

    return 1;
}

int VRBridge::Lua_IsStereoActive(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto* probe = (self && self->m_telemetryProbe) ? self->m_telemetryProbe.get() : &GraphicsTelemetryProbe::Instance();

    lua_pushboolean(L, probe->HasNativeStereoDetected() ? 1 : 0);
    return 1;
}

HeadPose VRBridge::GetHeadPose() {
    if (m_openXrManager) {
        return m_openXrManager->PollHeadPose();
    }
    return {};
}

void VRBridge::Recenter() {
    if (m_openXrManager) {
        m_openXrManager->Recenter();
    }
    LogInfo("VRBridge: Recenter triggered.");
}

int VRBridge::Lua_GetHeadPose(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) self = &Instance();

    HeadPose pose = self->GetHeadPose();

    lua_createtable(L, 0, 13);

    lua_pushnumber(L, pose.x);
    lua_setfield(L, -2, "x");

    lua_pushnumber(L, pose.y);
    lua_setfield(L, -2, "y");

    lua_pushnumber(L, pose.z);
    lua_setfield(L, -2, "z");

    lua_pushnumber(L, pose.pitch);
    lua_setfield(L, -2, "pitch");

    lua_pushnumber(L, pose.yaw);
    lua_setfield(L, -2, "yaw");

    lua_pushnumber(L, pose.roll);
    lua_setfield(L, -2, "roll");

    lua_pushnumber(L, pose.qx);
    lua_setfield(L, -2, "qx");

    lua_pushnumber(L, pose.qy);
    lua_setfield(L, -2, "qy");

    lua_pushnumber(L, pose.qz);
    lua_setfield(L, -2, "qz");

    lua_pushnumber(L, pose.qw);
    lua_setfield(L, -2, "qw");

    lua_pushboolean(L, pose.isValid ? 1 : 0);
    lua_setfield(L, -2, "isValid");

    lua_pushboolean(L, pose.positionTracked ? 1 : 0);
    lua_setfield(L, -2, "isPositionTracked");

    lua_pushboolean(L, pose.orientationTracked ? 1 : 0);
    lua_setfield(L, -2, "isOrientationTracked");

    return 1;
}

int VRBridge::Lua_Recenter(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) self = &Instance();

    self->Recenter();

    lua_createtable(L, 0, 1);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "success");
    return 1;
}

EyeProjection VRBridge::GetEyeProjection(uint32_t eyeIndex) {
    if (m_openXrManager) {
        return m_openXrManager->PollEyeProjection(eyeIndex);
    }
    return {};
}

void VRBridge::PushEyeProjectionTable(lua_State* L, const EyeProjection& proj, uint32_t eyeIndex) {
    lua_createtable(L, 0, 20);

    lua_pushnumber(L, proj.fovY);
    lua_setfield(L, -2, "fovY");

    lua_pushnumber(L, proj.fovX);
    lua_setfield(L, -2, "fovX");

    lua_pushnumber(L, proj.xOffset);
    lua_setfield(L, -2, "xOffset");

    lua_pushnumber(L, proj.yOffset);
    lua_setfield(L, -2, "yOffset");

    lua_pushnumber(L, proj.xOffset);
    lua_setfield(L, -2, "offsetX");

    lua_pushnumber(L, proj.yOffset);
    lua_setfield(L, -2, "offsetY");

    lua_pushnumber(L, proj.xOffset);
    lua_setfield(L, -2, "xOffsetTan");

    lua_pushnumber(L, proj.yOffset);
    lua_setfield(L, -2, "yOffsetTan");

    lua_pushnumber(L, proj.xOffsetNorm);
    lua_setfield(L, -2, "xOffsetNorm");

    lua_pushnumber(L, proj.yOffsetNorm);
    lua_setfield(L, -2, "yOffsetNorm");

    lua_pushnumber(L, proj.tanLeft);
    lua_setfield(L, -2, "tanLeft");

    lua_pushnumber(L, proj.tanRight);
    lua_setfield(L, -2, "tanRight");

    lua_pushnumber(L, proj.tanUp);
    lua_setfield(L, -2, "tanUp");

    lua_pushnumber(L, proj.tanDown);
    lua_setfield(L, -2, "tanDown");

    lua_pushnumber(L, proj.angleLeft);
    lua_setfield(L, -2, "angleLeft");

    lua_pushnumber(L, proj.angleRight);
    lua_setfield(L, -2, "angleRight");

    lua_pushnumber(L, proj.angleUp);
    lua_setfield(L, -2, "angleUp");

    lua_pushnumber(L, proj.angleDown);
    lua_setfield(L, -2, "angleDown");

    lua_pushnumber(L, proj.aspectRatio);
    lua_setfield(L, -2, "aspectRatio");

    lua_pushboolean(L, proj.isValid ? 1 : 0);
    lua_setfield(L, -2, "isValid");

    lua_pushinteger(L, static_cast<int>(eyeIndex));
    lua_setfield(L, -2, "eye");
}

int VRBridge::Lua_GetEyeProjection(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) self = &Instance();

    uint32_t eyeIndex = 0;
    if (lua_gettop(L) >= 1) {
        if (lua_isnumber(L, 1)) {
            int idx = lua_tointeger(L, 1);
            eyeIndex = (idx == 1) ? 1 : 0;
        } else if (lua_isstring(L, 1)) {
            const char* str = lua_tostring(L, 1);
            if (str && (_stricmp(str, "right") == 0 || _stricmp(str, "r") == 0)) {
                eyeIndex = 1;
            } else {
                eyeIndex = 0;
            }
        }
    }

    EyeProjection proj = self->GetEyeProjection(eyeIndex);
    PushEyeProjectionTable(L, proj, eyeIndex);
    return 1;
}

int VRBridge::Lua_GetViews(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) self = &Instance();

    EyeProjection leftProj = self->GetEyeProjection(0);
    EyeProjection rightProj = self->GetEyeProjection(1);

    lua_createtable(L, 2, 2);

    PushEyeProjectionTable(L, leftProj, 0);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, 1);
    lua_setfield(L, -2, "left");

    PushEyeProjectionTable(L, rightProj, 1);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, 2);
    lua_setfield(L, -2, "right");

    return 1;
}

} // namespace fs25vr
