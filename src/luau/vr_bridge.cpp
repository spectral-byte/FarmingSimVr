#include "vr_bridge.h"
#include "core/logger.h"
#include "core/thread_trace.h"
#include "ngx/ngx_interceptor.h"
#include "fidelityfx/fidelityfx_interceptor.h"

namespace fs25vr {

static bool ParseRenderingMode(const char* s, VRRenderingMode& outMode) {
    if (!s) return false;
    if (_stricmp(s, "sequentialdualpass") == 0 || _stricmp(s, "dualpass") == 0 ||
        _stricmp(s, "dual_pass") == 0 || _stricmp(s, "dual") == 0 ||
        _stricmp(s, "sequential") == 0 || _stricmp(s, "sdp") == 0 ||
        _stricmp(s, "1") == 0) {
        outMode = VRRenderingMode::SequentialDualPass;
        return true;
    }
    if (_stricmp(s, "afr") == 0 || _stricmp(s, "alternate") == 0 ||
        _stricmp(s, "alternateframe") == 0 || _stricmp(s, "0") == 0) {
        outMode = VRRenderingMode::AFR;
        return true;
    }
    return false;
}

VRBridge& VRBridge::FromUpvalue(lua_State* L) {
    auto* self = static_cast<VRBridge*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) luaL_error(L, "VRBridge: bridge upvalue is missing");  // does not return
    return *self;
}

VRBridge::VRBridge(std::shared_ptr<OpenXrManager> openXrManager,
                   std::shared_ptr<NgxInterceptor> ngxInterceptor)
    : m_openXrManager(std::move(openXrManager)),
      m_ngxInterceptor(std::move(ngxInterceptor)) {
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

    // Create table for VRBridge pre-allocated for all exposed fields (version + every table entry)
    // to prevent dynamic table resizing/rehashing during startup
    lua_createtable(L, 0, kExposedFieldCount);

    // VRBridge.version = "1.0.0"
    lua_pushstring(L, m_version.c_str());
    lua_setfield(L, -2, "version");

    // VRBridge.<name> = function [bound with 'this' upvalue], driven by kFunctionTable
    for (const LuaFunctionEntry& entry : kFunctionTable) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    // Set metatable on VRBridge table for dynamic property access (__index)
    lua_createtable(L, 0, 1);
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VRBridge::Lua_Index, "__index", 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);

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
    m_subPassIndex.store(0);
    m_dualPassPairIndex.store(0);
    SetFrameParity(0);
    LogInfo("[VRBridge] Native-to-Luau handshake completed successfully. SubPassIndex and FrameParity aligned to 0.");
    return true;
}

int VRBridge::Lua_IsReady(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    lua_pushboolean(L, self->IsReady() ? 1 : 0);
    return 1;
}

int VRBridge::Lua_Handshake(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    // Handshake negotiation: allow Luau to optionally specify a preferred mode
    if (lua_gettop(L) >= 1) {
        if (lua_isnumber(L, 1)) {
            int val = lua_tointeger(L, 1);
            self->SetRenderingMode((val == 1) ? VRRenderingMode::SequentialDualPass : VRRenderingMode::AFR);
        } else if (lua_isstring(L, 1)) {
            VRRenderingMode parsedMode;
            if (ParseRenderingMode(lua_tostring(L, 1), parsedMode)) {
                self->SetRenderingMode(parsedMode);
            }
        }
    }

    self->PerformHandshake();

    lua_createtable(L, 0, 5);

    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "success");

    lua_pushstring(L, self->GetVersion().c_str());
    lua_setfield(L, -2, "version");

    const char* modeStr = self->IsReady() ? "Active" : "PassThrough";
    lua_pushstring(L, modeStr);
    lua_setfield(L, -2, "mode");

    const char* rendStr = (self->GetRenderingMode() == VRRenderingMode::SequentialDualPass) ? "SequentialDualPass" : "AFR";
    lua_pushstring(L, rendStr);
    lua_setfield(L, -2, "renderingMode");

    lua_pushinteger(L, static_cast<int>(self->GetRenderingMode()));
    lua_setfield(L, -2, "renderingModeIndex");

    return 1;
}

int VRBridge::Lua_IsCompanionActive(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    lua_pushboolean(L, self->IsCompanionActive() ? 1 : 0);
    return 1;
}

int VRBridge::Lua_OnMapUnload(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    FS25VR_TRACE_THREAD("VRBridge.onMapUnload");
    self->SetCompanionActive(false);
    self->SetFrameParity(0);
    self->ResetSubPass();
    self->SetMenuVisible(false);
    self->Set2DViewActive(false);
    LogInfo("[VRBridge] Companion Mod map unloaded; reverted to Menu Quad layer mode.");
    return 0;
}

int VRBridge::Lua_GetStereoTelemetry(lua_State* L) {
    // Fixed verdict: the telemetry probe was retired (ADR-0017); ADR-0015 records that
    // GIANTS Engine 10 under D3D12 always needs AFR or SDP.
    VRBridge* self = &FromUpvalue(L);
    const bool afrFallback = self->RequiresAfrFallback();

    lua_createtable(L, 0, 7);

    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "frameIndex");

    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "maxViewports");

    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "passCount");

    lua_pushboolean(L, afrFallback ? 0 : 1);
    lua_setfield(L, -2, "nativeStereo");

    lua_pushboolean(L, afrFallback ? 1 : 0);
    lua_setfield(L, -2, "afrFallback");

    lua_pushstring(L, afrFallback ? "AfrFallbackRequired" : "NativeStereoDetected");
    lua_setfield(L, -2, "verdict");

    lua_pushstring(L, afrFallback ? "Native stereo is not available under D3D12; AFR or SDP is required."
                                  : "Native stereo is available.");
    lua_setfield(L, -2, "summary");

    return 1;
}

int VRBridge::Lua_IsStereoActive(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);
    lua_pushboolean(L, self->RequiresAfrFallback() ? 0 : 1);
    return 1;
}

HeadPose VRBridge::GetHeadPose() {
    return GetHeadPoseForEye(GetFrameParity());
}

HeadPose VRBridge::GetHeadPoseForEye(uint32_t eyeIndex) {
    if (m_openXrManager) {
        HeadPose pose = m_openXrManager->PollHeadPose();
        // Remember the pose handed out for this eye's render so submission uses it.
        m_openXrManager->RecordRenderPose(eyeIndex, pose);
        return pose;
    }
    return {};
}

void VRBridge::Recenter() {
    if (m_openXrManager) {
        m_openXrManager->Recenter();
    }
    SignalDiscontinuity();
    LogInfo("VRBridge: Recenter triggered.");
}

// Scans the call arguments for an optional eye selector (index or "left"/"right") and an optional
// caller-supplied output table. Position-independent so the table can never be mistaken for the eye.
// Returns the stack index of the output table, or 0 when none was supplied.
static int ParseEyeAndOutputArgs(lua_State* L, uint32_t& eyeIndex) {
    int outIndex = 0;
    const int argc = lua_gettop(L);
    bool eyeSeen = false;
    for (int i = 1; i <= argc; ++i) {
        if (lua_type(L, i) == LUA_TTABLE) {
            if (outIndex == 0) outIndex = i;
        } else if (!eyeSeen) {
            if (lua_isnumber(L, i)) {
                eyeIndex = (lua_tointeger(L, i) == 1) ? 1 : 0;
                eyeSeen = true;
            } else if (lua_isstring(L, i)) {
                const char* s = lua_tostring(L, i);
                eyeIndex = (s && (_stricmp(s, "right") == 0 || _stricmp(s, "r") == 0)) ? 1 : 0;
                eyeSeen = true;
            }
        }
    }
    return outIndex;
}

// The Injector runs its own statically linked Luau on the game's lua_State (ADR-0002). A table the
// game created points at the game's empty-node sentinel, which the Injector's table code does not
// recognise, so inserting a new key into it writes through the sentinel and crashes. Overwriting a
// key that already exists never inserts, so a caller-supplied table is only filled in place when it
// already holds every field written; otherwise a fresh Injector-made table is returned instead,
// which the caller can keep and pass back on later calls.
static bool HasAllFields(lua_State* L, int tableIndex, const char* const* names, size_t count) {
    tableIndex = lua_absindex(L, tableIndex);
    for (size_t i = 0; i < count; ++i) {
        lua_rawgetfield(L, tableIndex, names[i]);
        const bool present = lua_type(L, -1) != LUA_TNIL;
        lua_pop(L, 1);
        if (!present) return false;
    }
    return true;
}

static const char* const kPoseFields[] = {
    "x", "y", "z", "pitch", "yaw", "roll", "qx", "qy", "qz", "qw",
    "isValid", "isPositionTracked", "isOrientationTracked", "eye"
};
static constexpr size_t kHeadPoseFieldCount = 13;  // kPoseFields without the trailing "eye"
static constexpr size_t kEyePoseFieldCount = 14;

static const char* const kEyeProjectionFields[] = {
    "fovY", "fovX", "xOffset", "yOffset", "tanLeft", "tanRight", "tanUp", "tanDown",
    "angleLeft", "angleRight", "angleUp", "angleDown", "aspectRatio", "isValid", "eye"
};
static constexpr size_t kEyeProjectionFieldCount = 15;

void VRBridge::PushPoseTable(lua_State* L, const HeadPose& pose, int eyeIndex) {
    lua_createtable(L, 0, eyeIndex >= 0 ? 14 : 13);
    FillPoseTable(L, -1, pose, eyeIndex);
}

void VRBridge::FillPoseTable(lua_State* L, int tableIndex, const HeadPose& pose, int eyeIndex) {
    tableIndex = lua_absindex(L, tableIndex);
    lua_pushvalue(L, tableIndex);  // fields below are written to the table at -2 after each push

    lua_pushnumber(L, pose.position.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pose.position.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pose.position.z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, pose.rotation.pitch); lua_setfield(L, -2, "pitch");
    lua_pushnumber(L, pose.rotation.yaw); lua_setfield(L, -2, "yaw");
    lua_pushnumber(L, pose.rotation.roll); lua_setfield(L, -2, "roll");
    lua_pushnumber(L, pose.orientation.x); lua_setfield(L, -2, "qx");
    lua_pushnumber(L, pose.orientation.y); lua_setfield(L, -2, "qy");
    lua_pushnumber(L, pose.orientation.z); lua_setfield(L, -2, "qz");
    lua_pushnumber(L, pose.orientation.w); lua_setfield(L, -2, "qw");
    lua_pushboolean(L, pose.isValid ? 1 : 0); lua_setfield(L, -2, "isValid");
    lua_pushboolean(L, pose.positionTracked ? 1 : 0); lua_setfield(L, -2, "isPositionTracked");
    lua_pushboolean(L, pose.orientationTracked ? 1 : 0); lua_setfield(L, -2, "isOrientationTracked");
    if (eyeIndex >= 0) {
        lua_pushinteger(L, eyeIndex); lua_setfield(L, -2, "eye");
    }
    lua_pop(L, 1);
}

int VRBridge::Lua_GetHeadPose(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    FS25VR_TRACE_THREAD("VRBridge.getHeadPose");
    HeadPose pose = self->GetHeadPose();

    uint32_t unusedEye = 0;
    int outIndex = ParseEyeAndOutputArgs(L, unusedEye);
    if (outIndex != 0 && HasAllFields(L, outIndex, kPoseFields, kHeadPoseFieldCount)) {
        FillPoseTable(L, outIndex, pose);
        lua_pushvalue(L, outIndex);
    } else {
        PushPoseTable(L, pose);
    }
    return 1;
}

int VRBridge::Lua_Recenter(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    FS25VR_TRACE_THREAD("VRBridge.recenter");
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
    lua_createtable(L, 0, 15);
    FillEyeProjectionTable(L, -1, proj, eyeIndex);
}

void VRBridge::FillEyeProjectionTable(lua_State* L, int tableIndex, const EyeProjection& proj, uint32_t eyeIndex) {
    tableIndex = lua_absindex(L, tableIndex);
    lua_pushvalue(L, tableIndex);  // fields below are written to the table at -2 after each push

    lua_pushnumber(L, proj.fovY);
    lua_setfield(L, -2, "fovY");

    lua_pushnumber(L, proj.fovX);
    lua_setfield(L, -2, "fovX");

    lua_pushnumber(L, proj.xOffset);
    lua_setfield(L, -2, "xOffset");

    lua_pushnumber(L, proj.yOffset);
    lua_setfield(L, -2, "yOffset");

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
    lua_pop(L, 1);
}

int VRBridge::Lua_GetEyeProjection(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    uint32_t eyeIndex = self->GetFrameParity();
    int outIndex = ParseEyeAndOutputArgs(L, eyeIndex);

    FS25VR_TRACE_THREAD("VRBridge.getEyeProjection");
    EyeProjection proj = self->GetEyeProjection(eyeIndex);
    if (outIndex != 0 && HasAllFields(L, outIndex, kEyeProjectionFields, kEyeProjectionFieldCount)) {
        FillEyeProjectionTable(L, outIndex, proj, eyeIndex);
        lua_pushvalue(L, outIndex);
    } else {
        PushEyeProjectionTable(L, proj, eyeIndex);
    }
    return 1;
}

void VRBridge::ResetSubPassAndParity() {
    if (m_renderingMode.load() == VRRenderingMode::SequentialDualPass) {
        m_frameParity = 0;
    } else {
        m_frameParity = static_cast<uint32_t>(m_frameIndex % 2);
    }
    m_subPassIndex.store(0);
}

void VRBridge::AdvanceFrame() {
    m_frameIndex++;
    if (m_renderingMode.load() == VRRenderingMode::SequentialDualPass) {
        m_dualPassPairIndex.fetch_add(1);
    }
    ResetSubPassAndParity();
}

void VRBridge::SetFrameIndex(uint64_t index) {
    m_frameIndex = index;
    ResetSubPassAndParity();
}

void VRBridge::SetRenderingMode(VRRenderingMode mode) {
    m_renderingMode.store(mode);
    // The camera records of the old mode would read as history under the new one (the eye cadence differs).
    m_cameraRecords.Clear();
    m_subPassIndex.store(0);
    m_dualPassPairIndex.store(0);
    SetFrameParity(0);
    LogInfo(std::string("VRBridge: Rendering mode set to: ") +
            (mode == VRRenderingMode::SequentialDualPass ? "SequentialDualPass" : "AFR"));
}

std::optional<VRRenderingMode> VRBridge::GetPendingRenderingMode() const {
    int pending = m_pendingRenderingMode.load();
    if (pending == kNoPendingMode) return std::nullopt;
    return static_cast<VRRenderingMode>(pending);
}

bool VRBridge::LatchPendingRenderingMode() {
    int pending = m_pendingRenderingMode.exchange(kNoPendingMode);
    if (pending == kNoPendingMode) return false;
    auto newMode = static_cast<VRRenderingMode>(pending);
    SetRenderingMode(newMode);
    LogInfo("[VRBridge] Latched pending rendering mode on completed compositor frame boundary: " +
            std::string(newMode == VRRenderingMode::SequentialDualPass ? "SequentialDualPass" : "AFR"));
    return true;
}

void VRBridge::SetFrameParity(uint32_t parity) {
    m_frameParity = parity % 2;
    if (static_cast<uint32_t>(m_frameIndex % 2) != m_frameParity) {
        m_frameIndex++;
    }
}

float VRBridge::GetIpd() const {
    if (m_hasCustomIpd) {
        return m_simulatedIpd;
    }
    return m_openXrManager ? m_openXrManager->GetIpd() : 0.064f;
}

void VRBridge::SetIpd(float ipd) {
    m_simulatedIpd = ipd;
    m_hasCustomIpd = true;
    if (m_openXrManager) {
        m_openXrManager->SetIpd(ipd);
    }
}

HeadPose VRBridge::GetEyePose(uint32_t eyeIndex) {
    HeadPose pose = GetHeadPoseForEye(eyeIndex);
    return OpenXrManager::ComputeEyePose(pose, GetIpd(), eyeIndex);
}

int VRBridge::Lua_RequiresAfrFallback(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);
    lua_pushboolean(L, self->RequiresAfrFallback() ? 1 : 0);
    return 1;
}

int VRBridge::Lua_GetFrameParity(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);
    lua_pushinteger(L, self->GetFrameParity());
    return 1;
}

int VRBridge::Lua_GetFrameIndex(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);
    lua_pushinteger(L, static_cast<int>(self->GetFrameIndex()));
    return 1;
}

int VRBridge::Lua_GetIpd(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);
    lua_pushnumber(L, self->GetIpd());
    return 1;
}

int VRBridge::Lua_GetEyePose(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    uint32_t eyeIndex = 0;
    int outIndex = ParseEyeAndOutputArgs(L, eyeIndex);

    FS25VR_TRACE_THREAD("VRBridge.getEyePose");
    HeadPose pose = self->GetEyePose(eyeIndex);
    if (outIndex != 0 && HasAllFields(L, outIndex, kPoseFields, kEyePoseFieldCount)) {
        FillPoseTable(L, outIndex, pose, static_cast<int>(eyeIndex));
        lua_pushvalue(L, outIndex);
    } else {
        PushPoseTable(L, pose, static_cast<int>(eyeIndex));
    }
    return 1;
}

void VRBridge::SetMenuVisible(bool visible) {
    if (m_menuVisible != visible) {
        m_menuVisible = visible;
        SetFrameParity(0);
        ResetSubPass();
        SignalDiscontinuity();
        if (visible) {
            if (m_openXrManager) {
                m_openXrManager->AlignMenuQuadToHeadPose();
            }
            LogInfo("[VRBridge] In-game menu opened (isMenuVisible=true); switched to 6DOF Quad layer.");
        } else {
            LogInfo("[VRBridge] In-game menu closed (isMenuVisible=false); restored stereoscopic 3D projection.");
        }
    }
}

void VRBridge::AlignMenuQuad() {
    if (m_openXrManager) {
        m_openXrManager->AlignMenuQuadToHeadPose();
    }
}

int VRBridge::Lua_AlignMenuQuad(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    FS25VR_TRACE_THREAD("VRBridge.alignMenuQuad");
    self->AlignMenuQuad();
    return 0;
}

int VRBridge::Lua_SetMenuVisible(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    bool visible = false;
    if (lua_gettop(L) >= 1) {
        visible = (lua_toboolean(L, 1) != 0);
    }
    FS25VR_TRACE_THREAD("VRBridge.setMenuVisible");
    self->SetMenuVisible(visible);
    return 0;
}

int VRBridge::Lua_GetIsMenuVisible(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    lua_pushboolean(L, self->IsMenuVisible() ? 1 : 0);
    return 1;
}

void VRBridge::Set2DViewActive(bool active) {
    if (m_2dViewActive != active) {
        m_2dViewActive = active;
        SetFrameParity(0);
        ResetSubPass();
        SignalDiscontinuity();
        if (active) {
            if (m_openXrManager) {
                m_openXrManager->AlignMenuQuadToHeadPose();
            }
            LogInfo("[VRBridge] 2D Floating Window View enabled (is2DViewActive=true); switched to 6DOF Quad layer.");
        } else {
            LogInfo("[VRBridge] 2D Floating Window View disabled (is2DViewActive=false); restored stereoscopic 3D projection.");
        }
    }
}

int VRBridge::Lua_Set2DViewActive(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    bool active = false;
    if (lua_gettop(L) >= 1) {
        active = (lua_toboolean(L, 1) != 0);
    }
    FS25VR_TRACE_THREAD("VRBridge.set2DViewActive");
    self->Set2DViewActive(active);
    return 0;
}

int VRBridge::Lua_GetIs2DViewActive(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    lua_pushboolean(L, self->Is2DViewActive() ? 1 : 0);
    return 1;
}

int VRBridge::Lua_GetRenderingMode(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    VRRenderingMode mode = self->GetRenderingMode();
    const char* str = (mode == VRRenderingMode::SequentialDualPass) ? "SequentialDualPass" : "AFR";
    int intVal = static_cast<int>(mode);

    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        const char* fmt = lua_tostring(L, 1);
        if (fmt && (_stricmp(fmt, "int") == 0 || _stricmp(fmt, "number") == 0 || _stricmp(fmt, "index") == 0)) {
            lua_pushinteger(L, intVal);
            return 1;
        }
    }

    lua_pushstring(L, str);
    lua_pushinteger(L, intVal);
    return 2;
}

int VRBridge::Lua_SetRenderingMode(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    FS25VR_TRACE_THREAD("VRBridge.setRenderingMode");
    if (lua_gettop(L) >= 1) {
        VRRenderingMode targetMode = VRRenderingMode::SequentialDualPass;
        bool hasTarget = false;
        if (lua_isnumber(L, 1)) {
            int val = lua_tointeger(L, 1);
            targetMode = (val == 0) ? VRRenderingMode::AFR : VRRenderingMode::SequentialDualPass;
            hasTarget = true;
        } else if (lua_isstring(L, 1)) {
            const char* s = lua_tostring(L, 1);
            if (s && (_stricmp(s, "toggle") == 0)) {
                targetMode = self->IsSequentialDualPass() ? VRRenderingMode::AFR : VRRenderingMode::SequentialDualPass;
                hasTarget = true;
            } else {
                VRRenderingMode parsedMode;
                if (ParseRenderingMode(s, parsedMode)) {
                    targetMode = parsedMode;
                    hasTarget = true;
                } else {
                    targetMode = VRRenderingMode::SequentialDualPass;
                    hasTarget = true;
                }
            }
        }

        if (hasTarget) {
            if (self->m_openXrManager && self->m_openXrManager->IsActive()) {
                self->SetPendingRenderingMode(targetMode);
            } else {
                self->SetRenderingMode(targetMode);
            }
        }
    }
    return 0;
}

int VRBridge::Lua_GetSubPassIndex(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    lua_pushinteger(L, static_cast<int>(self->GetSubPassIndex()));
    return 1;
}

int VRBridge::Lua_SetSubPassIndex(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    if (lua_gettop(L) >= 1 && lua_isnumber(L, 1)) {
        self->SetSubPassIndex(static_cast<uint32_t>(lua_tointeger(L, 1)));
    }
    return 0;
}

int VRBridge::Lua_AdvanceSubPass(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    uint32_t next = self->AdvanceSubPass();
    lua_pushinteger(L, static_cast<int>(next));
    return 1;
}

int VRBridge::Lua_GetActiveEye(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    lua_pushinteger(L, static_cast<int>(self->GetActiveEye()));
    return 1;
}

int VRBridge::Lua_Index(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    if (lua_isstring(L, 2)) {
        const char* key = lua_tostring(L, 2);
        if (strcmp(key, "requiresAfrFallback") == 0) {
            lua_pushboolean(L, self->RequiresAfrFallback() ? 1 : 0);
            return 1;
        }
        if (strcmp(key, "frameParity") == 0) {
            lua_pushinteger(L, self->GetFrameParity());
            return 1;
        }
        if (strcmp(key, "frameIndex") == 0) {
            lua_pushinteger(L, static_cast<int>(self->GetFrameIndex()));
            return 1;
        }
        if (strcmp(key, "renderingMode") == 0) {
            const char* str = (self->GetRenderingMode() == VRRenderingMode::SequentialDualPass) ? "SequentialDualPass" : "AFR";
            lua_pushstring(L, str);
            return 1;
        }
        if (strcmp(key, "renderingModeIndex") == 0) {
            lua_pushinteger(L, static_cast<int>(self->GetRenderingMode()));
            return 1;
        }
        if (strcmp(key, "subPassIndex") == 0) {
            lua_pushinteger(L, static_cast<int>(self->GetSubPassIndex()));
            return 1;
        }
        if (strcmp(key, "ipd") == 0) {
            lua_pushnumber(L, self->GetIpd());
            return 1;
        }
        if (strcmp(key, "isCompanionActive") == 0) {
            lua_pushboolean(L, self->IsCompanionActive() ? 1 : 0);
            return 1;
        }
        if (strcmp(key, "isMenuVisible") == 0) {
            lua_pushboolean(L, self->IsMenuVisible() ? 1 : 0);
            return 1;
        }
        if (strcmp(key, "is2DViewActive") == 0) {
            lua_pushboolean(L, self->Is2DViewActive() ? 1 : 0);
            return 1;
        }
        if (strcmp(key, "hasPendingDiscontinuity") == 0) {
            lua_pushboolean(L, (self->HasPendingDiscontinuity(0) || self->HasPendingDiscontinuity(1)) ? 1 : 0);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

void VRBridge::SignalDiscontinuity() {
    m_discontinuityPendingLeft = true;
    m_discontinuityPendingRight = true;
    LogInfo("[VRBridge] SignalDiscontinuity: Discontinuity flush flagged across dual contexts.");
}

bool VRBridge::HasPendingDiscontinuity(uint32_t contextIndex) const {
    if (contextIndex == 1) {
        return m_discontinuityPendingRight.load();
    }
    return m_discontinuityPendingLeft.load();
}

bool VRBridge::ConsumeDiscontinuity(uint32_t contextIndex) {
    if (contextIndex == 1) {
        return m_discontinuityPendingRight.exchange(false);
    }
    return m_discontinuityPendingLeft.exchange(false);
}

int VRBridge::Lua_ResetTemporalHistory(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    self->SignalDiscontinuity();
    return 0;
}

int VRBridge::Lua_SignalDiscontinuity(lua_State* L) {
    VRBridge* self = &FromUpvalue(L);

    self->SignalDiscontinuity();
    return 0;
}

int VRBridge::Lua_SetDlssMvScale(lua_State* L) {
    double scaleX = luaL_checknumber(L, 1);
    double scaleY = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : scaleX;
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: DLSS tuning is unavailable, which the Companion Mod can report.
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->SetMvScaleMultiplier(static_cast<float>(scaleX), static_cast<float>(scaleY));
    lua_pushboolean(L, 1);
    return 1;
}

int VRBridge::Lua_SetDlssSharpness(lua_State* L) {
    double sharp = luaL_checknumber(L, 1);
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: DLSS tuning is unavailable, which the Companion Mod can report.
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->SetSharpnessOverride(static_cast<float>(sharp), true);
    lua_pushboolean(L, 1);
    return 1;
}

int VRBridge::Lua_SetDlssPreset(lua_State* L) {
    int preset = static_cast<int>(luaL_checkinteger(L, 1));
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: DLSS tuning is unavailable, which the Companion Mod can report.
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->SetDlssPresetOverride(preset);
    lua_pushboolean(L, 1);
    return 1;
}

int VRBridge::Lua_SetDlssSdpMvScale(lua_State* L) {
    double scaleX = luaL_checknumber(L, 1);
    double scaleY = lua_isnumber(L, 2) ? lua_tonumber(L, 2) : scaleX;
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: DLSS tuning is unavailable, which the Companion Mod can report.
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->SetSdpMvScaleMultiplier(static_cast<float>(scaleX), static_cast<float>(scaleY));
    lua_pushboolean(L, 1);
    return 1;
}

int VRBridge::Lua_ReportCameraRecord(lua_State* L) {
    CameraRecord record;
    record.eye = static_cast<uint32_t>((std::max)(0, static_cast<int>(luaL_checkinteger(L, 1))));
    for (int i = 0; i < 3; ++i) record.position[i] = luaL_checknumber(L, 2 + i);
    for (int i = 0; i < 4; ++i) record.orientation[i] = luaL_checknumber(L, 5 + i);
    record.fovY = static_cast<float>(luaL_checknumber(L, 9));
    record.nearClip = static_cast<float>(luaL_checknumber(L, 10));
    record.farClip = static_cast<float>(luaL_checknumber(L, 11));
    record.projOffsetX = static_cast<float>(luaL_optnumber(L, 12, 0.0));
    record.projOffsetY = static_cast<float>(luaL_optnumber(L, 13, 0.0));
    VRBridge* self = &FromUpvalue(L);
    lua_pushboolean(L, self->m_cameraRecords.Report(record) ? 1 : 0);
    return 1;
}

int VRBridge::Lua_SetDlssMvFix(lua_State* L) {
    bool enabled = lua_toboolean(L, 1) != 0;
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: there is no motion vector pass to switch.
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // True when the request took effect; turning the pass on after it disabled itself for the session reads false.
    lua_pushboolean(L, self->m_ngxInterceptor->SetMvFixEnabled(enabled) == enabled ? 1 : 0);
    return 1;
}

int VRBridge::Lua_SetDlssDepthModel(lua_State* L) {
    bool infinite = lua_toboolean(L, 1) != 0;
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: there is no correction to switch.
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->SetDepthModelInfinite(infinite);
    lua_pushboolean(L, 1);
    return 1;
}

int VRBridge::Lua_SetDlssNearClip(lua_State* L) {
    float meters = static_cast<float>(luaL_checknumber(L, 1));
    VRBridge* self = &FromUpvalue(L);
    if (!self->m_ngxInterceptor) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->SetNearClipOverride(meters); // 0 = automatic
    lua_pushboolean(L, 1);
    return 1;
}

int VRBridge::Lua_DlssDiag(lua_State* L) {
    int evaluations = lua_isnumber(L, 1) ? static_cast<int>(lua_tointeger(L, 1))
                                         : static_cast<int>(NgxInterceptor::kDefaultDiagEvaluations);
    VRBridge* self = &FromUpvalue(L);
    // No NGX interceptor wired: there are no DLSS evaluations to trace.
    if (!self->m_ngxInterceptor || evaluations <= 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    self->m_ngxInterceptor->ArmDiagnostics(static_cast<uint32_t>(evaluations));
    // FSR3 dispatches are traced through the same command.
    if (FidelityFxInterceptor* ffx = self->m_ngxInterceptor->GetFidelityFxInterceptor()) {
        ffx->ArmDiagnostics(static_cast<uint32_t>(evaluations));
    }
    lua_pushboolean(L, 1);
    return 1;
}

} // namespace fs25vr
