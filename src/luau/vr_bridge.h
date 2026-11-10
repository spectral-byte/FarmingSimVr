#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include "lua.h"
#include "lualib.h"
#include "core/camera_records.h"
#include "core/injector_version.h"
#include "input/virtual_gamepad.h"
#include "openxr/openxr_manager.h"

namespace fs25vr {

class NgxInterceptor;

enum class VRRenderingMode : uint32_t {
    AFR = 0,
    SequentialDualPass = 1,
    AlternateFrame = AFR
};

class VRBridge {
public:
    // The composition root (InjectorRuntime) supplies both subsystems; a null one is absent. It also supplies the
    // Injector's release version, which VRBridge reports as VRBridge.version and in the handshake result; a test
    // that does not care gets the build's version, one that does passes its own.
    VRBridge(std::shared_ptr<OpenXrManager> openXrManager,
             std::shared_ptr<NgxInterceptor> ngxInterceptor,
             std::string version = std::string(kInjectorVersion));

    bool Inject(lua_State* L);
    bool IsInjected(lua_State* L);

    const std::string& GetVersion() const { return m_version; }
    bool IsReady() const;
    bool PerformHandshake();
    bool IsCompanionActive() const { return m_handshakeCompleted; }
    void SetCompanionActive(bool active) { m_handshakeCompleted = active; }
    std::shared_ptr<OpenXrManager> GetOpenXrManager() const { return m_openXrManager; }
    std::shared_ptr<NgxInterceptor> GetNgxInterceptor() const { return m_ngxInterceptor; }

    HeadPose GetHeadPose();
    HeadPose GetHeadPoseForEye(uint32_t eyeIndex);
    // One Motion Controller's state (0 = left, 1 = right); untracked defaults without a runtime,
    // when the runtime refused the action set, or for any other hand id. Does not touch the render-pose records.
    HandState GetHandState(uint32_t hand);
    // A short vibration on one hand (0 = left, 1 = right): `durationMs` long at `strength` (0..1).
    // False when nothing played: no runtime, the action set was refused, or an unknown hand.
    bool PulseHaptic(uint32_t hand, float durationMs, float strength);
    // The Virtual Gamepad the game sees in place of the Motion Controllers' buttons (ADR-0028). The Companion Mod
    // sets it through VRBridge.setVirtualGamepad and reads it back through VRBridge.getVirtualGamepad; the XInput
    // hooks of the Injector answer the game from it.
    VirtualGamepad& GetVirtualGamepad() { return m_virtualGamepad; }
    // Where one hand's ray lands on the visible menu / 2D Floating Window Mode panel, in normalized
    // panel coordinates (PanelHit). A miss when no panel is showing, the hand is untracked or there is
    // no runtime. The Injector owns the panel pose and size, so the geometry stays here (ADR-0006, ADR-0008).
    PanelHit GetPanelHit(uint32_t hand);
    // The same for a ray the Companion Mod gives, in the space the hand poses are in: the Pointer Ray starts at the
    // index fingertip and runs along the finger, not along the aim pose.
    PanelHit GetPanelHitAlongRay(uint32_t hand, const Vector3& origin, const Vector3& direction);
    // Draws the menu cursor at (u, v) while the Pointer Ray has control, or gives it back to the desk
    // mouse (active = false). Cleared whenever a panel appears or goes away.
    void SetMenuCursor(bool active, float u, float v);
    void Recenter();
    EyeProjection GetEyeProjection(uint32_t eyeIndex = 0);

    // GIANTS Engine 10 under D3D12 renders one viewport and one scene pass (ADR-0015, ADR-0017),
    // so AFR or SDP is always required.
    bool RequiresAfrFallback() const { return true; }

    VRRenderingMode GetRenderingMode() const { return m_renderingMode.load(); }
    void SetRenderingMode(VRRenderingMode mode);

    // Stereo Presentation Mode changes requested while the compositor is active are only queued here.
    // The DXGI proxy latches them at a completed compositor frame boundary (ADR-0015).
    void SetPendingRenderingMode(VRRenderingMode mode) { m_pendingRenderingMode.store(static_cast<int>(mode)); }
    std::optional<VRRenderingMode> GetPendingRenderingMode() const;
    void ClearPendingRenderingMode() { m_pendingRenderingMode.store(kNoPendingMode); }
    // Applies the queued mode, if any. Returns true when a mode was latched.
    bool LatchPendingRenderingMode();
    bool IsSequentialDualPass() const { return m_renderingMode.load() == VRRenderingMode::SequentialDualPass; }

    // Eye cameras the Companion Mod reports per stereo sub-pass, stamped with the rendered-frame
    // counter the DXGI proxy advances once per Present in stereo presentation.
    CameraRecordStore& GetCameraRecords() { return m_cameraRecords; }
    const CameraRecordStore& GetCameraRecords() const { return m_cameraRecords; }

    uint32_t GetSubPassIndex() const { return m_subPassIndex.load(); }
    void SetSubPassIndex(uint32_t index) { m_subPassIndex.store(index % 2); }
    uint32_t AdvanceSubPass() {
        return m_subPassIndex.fetch_xor(1) ^ 1;
    }
    void ResetSubPass() { m_subPassIndex.store(0); }

    // The eye the current frame renders: the sub-pass under SDP (left first), the frame parity under AFR.
    uint32_t GetActiveEye() const {
        if (m_renderingMode.load() == VRRenderingMode::SequentialDualPass) {
            return m_subPassIndex.load();
        }
        return m_frameParity;
    }
    uint32_t GetFrameParity() const { return GetActiveEye(); }
    uint64_t GetFrameIndex() const { return m_frameIndex; }
    void AdvanceFrame();
    void SetFrameParity(uint32_t parity);
    void SetFrameIndex(uint64_t index);
    float GetIpd() const;
    void SetIpd(float ipd);
    HeadPose GetEyePose(uint32_t eyeIndex);

    bool IsMenuVisible() const { return m_menuVisible; }
    void SetMenuVisible(bool visible);
    void AlignMenuQuad();

    bool Is2DViewActive() const { return m_2dViewActive; }
    void Set2DViewActive(bool active);

    // Whether the game presents to the Desktop Mirror Window while the headset session runs. On by
    // default; the Companion Mod sets it (`vrDesktopMirror`) and the DXGI proxy reads it every present.
    bool IsDesktopMirrorEnabled() const { return m_desktopMirrorEnabled; }
    void SetDesktopMirrorEnabled(bool enabled) { m_desktopMirrorEnabled = enabled; }

    // FOV Overscan in percent, 0 to 20 (0 by default; the Companion Mod sets it with `vrFovOverscan`). The bridge clamps and
    // forwards it to the OpenXR manager, which applies it when the next frame begins. Without a manager (Pass-Through Mode)
    // it is accepted and has no effect. A map unload puts it back to 0.
    float GetFovOverscan() const { return m_fovOverscan; }
    void SetFovOverscan(float percent);

    // Menu Distance in metres, 1.0 to 5.0 (1.5 by default; the Companion Mod sets it with `vrMenuDistance`): how far in front
    // of the head menus and 2D Floating Window Mode are shown. The bridge clamps and forwards it to the OpenXR manager, which
    // moves a quad already shown at once. Without a manager (Pass-Through Mode) it is accepted and has no effect. The Injector
    // sets the saved value at launch (InjectorRuntime), and a map unload leaves it, so the main menu is shown at it too.
    float GetMenuDistance() const { return m_menuDistance; }
    void SetMenuDistance(float metres);

    // `vrEyeSnapshot` (diagnostics): the next stereo frame the DXGI proxy copies to each eye is also written to a picture
    // file next to the log, once per eye. Consume returns true once for each eye of a request.
    void RequestEyeSnapshot() { m_eyeSnapshotRequests.store(0b11u); }
    bool ConsumeEyeSnapshotRequest(uint32_t eyeIndex) {
        const uint32_t bit = 1u << (eyeIndex & 1u);
        return (m_eyeSnapshotRequests.fetch_and(~bit) & bit) != 0;
    }

    // Whether Mouse Confinement may keep the Desk Mouse on the game picture while the headset is in use.
    // On by default; the Companion Mod sets it (`vrMouseConfinement`) and the DXGI proxy reads it every present.
    bool IsMouseConfinementEnabled() const { return m_mouseConfinementEnabled; }
    void SetMouseConfinementEnabled(bool enabled) { m_mouseConfinementEnabled = enabled; }

    void SignalDiscontinuity();
    bool HasPendingDiscontinuity(uint32_t contextIndex = 0) const;
    bool ConsumeDiscontinuity(uint32_t contextIndex = 0);

    static int Lua_IsReady(lua_State* L);
    static int Lua_Handshake(lua_State* L);
    static int Lua_IsCompanionActive(lua_State* L);
    static int Lua_OnMapUnload(lua_State* L);
    static int Lua_GetStereoTelemetry(lua_State* L);
    static int Lua_IsStereoActive(lua_State* L);
    static int Lua_RequiresAfrFallback(lua_State* L);
    static int Lua_GetHeadPose(lua_State* L);
    static int Lua_GetHandState(lua_State* L);
    static int Lua_PulseHaptic(lua_State* L);
    static int Lua_SetVirtualGamepad(lua_State* L);
    static int Lua_GetVirtualGamepad(lua_State* L);
    static int Lua_SetVirtualGamepadUse(lua_State* L);
    static int Lua_Recenter(lua_State* L);
    static int Lua_AlignMenuQuad(lua_State* L);
    static int Lua_GetPanelHit(lua_State* L);
    static int Lua_GetPanelHitAlongRay(lua_State* L);
    static int Lua_SetMenuCursor(lua_State* L);
    static int Lua_GetEyeProjection(lua_State* L);
    static int Lua_SetMenuVisible(lua_State* L);
    static int Lua_GetIsMenuVisible(lua_State* L);
    static int Lua_Set2DViewActive(lua_State* L);
    static int Lua_GetIs2DViewActive(lua_State* L);
    static int Lua_SetDesktopMirror(lua_State* L);
    static int Lua_GetDesktopMirror(lua_State* L);
    static int Lua_SetFovOverscan(lua_State* L);
    static int Lua_GetFovOverscan(lua_State* L);
    static int Lua_SetMenuDistance(lua_State* L);
    static int Lua_GetMenuDistance(lua_State* L);
    static int Lua_SnapshotEyes(lua_State* L);
    static int Lua_SetMouseConfinement(lua_State* L);
    static int Lua_GetMouseConfinement(lua_State* L);
    static int Lua_ResetTemporalHistory(lua_State* L);
    static int Lua_SignalDiscontinuity(lua_State* L);
    static int Lua_SetDlssMvScale(lua_State* L);
    static int Lua_SetDlssSharpness(lua_State* L);
    static int Lua_SetDlssPreset(lua_State* L);
    static int Lua_DlssDiag(lua_State* L);
    static int Lua_SetDlssMvFix(lua_State* L);
    static int Lua_SetDlssDepthModel(lua_State* L);
    static int Lua_SetDlssNearClip(lua_State* L);
    static int Lua_SetDlssSdpMvScale(lua_State* L);
    // `vrPerfTrace` (core/perf_trace.h): startPerfTrace([seconds]), stopPerfTrace(), isPerfTracing(), and
    // perfTraceBegin(name) / perfTraceEnd(name) around a Companion Mod section ("gameUpdate", "modUpdate", ...).
    static int Lua_StartPerfTrace(lua_State* L);
    static int Lua_StopPerfTrace(lua_State* L);
    static int Lua_IsPerfTracing(lua_State* L);
    static int Lua_PerfTraceBegin(lua_State* L);
    static int Lua_PerfTraceEnd(lua_State* L);
    // reportCameraRecord(eye, x, y, z, qx, qy, qz, qw, fovY, nearClip, farClip, projOffsetX, projOffsetY)
    static int Lua_ReportCameraRecord(lua_State* L);

    static int Lua_GetRenderingMode(lua_State* L);
    static int Lua_SetRenderingMode(lua_State* L);
    static int Lua_GetSubPassIndex(lua_State* L);
    static int Lua_SetSubPassIndex(lua_State* L);
    static int Lua_AdvanceSubPass(lua_State* L);
    static int Lua_GetActiveEye(lua_State* L);

    static int Lua_GetFrameParity(lua_State* L);
    static int Lua_GetFrameIndex(lua_State* L);
    static int Lua_GetIpd(lua_State* L);
    static int Lua_GetEyePose(lua_State* L);
    static int Lua_Index(lua_State* L);

    static void PushEyeProjectionTable(lua_State* L, const EyeProjection& proj, uint32_t eyeIndex);
    // Builds a pose table; eyeIndex < 0 omits the "eye" field (head pose).
    static void PushPoseTable(lua_State* L, const HeadPose& pose, int eyeIndex = -1);
    // In-place variants: write the same fields into the existing table at tableIndex (no allocation).
    static void FillPoseTable(lua_State* L, int tableIndex, const HeadPose& pose, int eyeIndex = -1);
    static void PushHandStateTable(lua_State* L, const HandState& hand);
    static void FillHandStateTable(lua_State* L, int tableIndex, const HandState& hand);
    static void FillEyeProjectionTable(lua_State* L, int tableIndex, const EyeProjection& proj, uint32_t eyeIndex);

    // How often the Companion Mod calls a function, which decides what the collector does around the call. The Injector
    // links its own copy of Luau and runs it on the game's heap (ADR-0002), and a collection step started from that copy
    // has crashed the game, so no call may let one start from there.
    //  PerFrame:   called on every update or render. Allocates nothing on the game's heap once the caller's reusable
    //              tables hold their fields, and never touches the collector: restarting it makes a step due at the next
    //              allocation, and doing that every frame would change the game's collection pacing.
    //  Occasional: called at handshake, on map load or unload, when a setting changes or from a console command. May
    //              allocate; Inject() registers it so the collector is held off for the call and left as it was found,
    //              a Lua error included (see RunOccasional).
    //  Unmarked:   a table entry that names no kind. Treated like Occasional, and the function table test fails on it.
    enum class CallKind : uint8_t { Unmarked = 0, PerFrame, Occasional };

    struct LuaFunctionEntry {
        const char* name;
        lua_CFunction fn;
        CallKind kind = CallKind::Unmarked;
    };

    // Single source of truth for the Lua-visible functions registered by Inject(); the kind of each is written next to it.
    static constexpr LuaFunctionEntry kFunctionTable[] = {
        {"isReady", &Lua_IsReady, CallKind::PerFrame},
        {"handshake", &Lua_Handshake, CallKind::Occasional},
        {"isCompanionActive", &Lua_IsCompanionActive, CallKind::PerFrame},
        {"onMapUnload", &Lua_OnMapUnload, CallKind::Occasional},
        {"getStereoTelemetry", &Lua_GetStereoTelemetry, CallKind::Occasional},
        {"isStereoActive", &Lua_IsStereoActive, CallKind::Occasional},
        {"getRequiresAfrFallback", &Lua_RequiresAfrFallback, CallKind::PerFrame},
        {"getHeadPose", &Lua_GetHeadPose, CallKind::PerFrame},
        {"getHandState", &Lua_GetHandState, CallKind::PerFrame},
        {"pulseHaptic", &Lua_PulseHaptic, CallKind::PerFrame},
        {"setVirtualGamepad", &Lua_SetVirtualGamepad, CallKind::PerFrame},
        {"getVirtualGamepad", &Lua_GetVirtualGamepad, CallKind::PerFrame},
        {"setVirtualGamepadUse", &Lua_SetVirtualGamepadUse, CallKind::PerFrame},
        {"recenter", &Lua_Recenter, CallKind::Occasional},
        {"getEyeProjection", &Lua_GetEyeProjection, CallKind::PerFrame},
        {"getFrameParity", &Lua_GetFrameParity, CallKind::PerFrame},
        {"getFrameIndex", &Lua_GetFrameIndex, CallKind::PerFrame},
        {"getIpd", &Lua_GetIpd, CallKind::PerFrame},
        {"getEyePose", &Lua_GetEyePose, CallKind::PerFrame},
        {"setMenuVisible", &Lua_SetMenuVisible, CallKind::Occasional},
        {"getIsMenuVisible", &Lua_GetIsMenuVisible, CallKind::PerFrame},
        {"alignMenuQuad", &Lua_AlignMenuQuad, CallKind::Occasional},
        {"getPanelHit", &Lua_GetPanelHit, CallKind::PerFrame},
        {"getPanelHitAlongRay", &Lua_GetPanelHitAlongRay, CallKind::PerFrame},
        {"setMenuCursor", &Lua_SetMenuCursor, CallKind::PerFrame},
        {"set2DViewActive", &Lua_Set2DViewActive, CallKind::Occasional},
        {"getIs2DViewActive", &Lua_GetIs2DViewActive, CallKind::PerFrame},
        {"setDesktopMirror", &Lua_SetDesktopMirror, CallKind::Occasional},
        {"getDesktopMirror", &Lua_GetDesktopMirror, CallKind::PerFrame},
        {"setMouseConfinement", &Lua_SetMouseConfinement, CallKind::Occasional},
        {"getMouseConfinement", &Lua_GetMouseConfinement, CallKind::PerFrame},
        {"setFovOverscan", &Lua_SetFovOverscan, CallKind::Occasional},
        {"getFovOverscan", &Lua_GetFovOverscan, CallKind::PerFrame},
        {"setMenuDistance", &Lua_SetMenuDistance, CallKind::Occasional},
        {"getMenuDistance", &Lua_GetMenuDistance, CallKind::PerFrame},
        {"snapshotEyes", &Lua_SnapshotEyes, CallKind::Occasional},
        {"getRenderingMode", &Lua_GetRenderingMode, CallKind::PerFrame},
        {"setRenderingMode", &Lua_SetRenderingMode, CallKind::Occasional},
        {"getSubPassIndex", &Lua_GetSubPassIndex, CallKind::PerFrame},
        {"setSubPassIndex", &Lua_SetSubPassIndex, CallKind::PerFrame},
        {"advanceSubPass", &Lua_AdvanceSubPass, CallKind::PerFrame},
        {"getActiveEye", &Lua_GetActiveEye, CallKind::PerFrame},
        {"resetTemporalHistory", &Lua_ResetTemporalHistory, CallKind::Occasional},
        {"signalDiscontinuity", &Lua_SignalDiscontinuity, CallKind::Occasional},
        {"setDlssMvScale", &Lua_SetDlssMvScale, CallKind::Occasional},
        {"setDlssSharpness", &Lua_SetDlssSharpness, CallKind::Occasional},
        {"setDlssPreset", &Lua_SetDlssPreset, CallKind::Occasional},
        {"dlssDiag", &Lua_DlssDiag, CallKind::Occasional},
        {"setDlssSdpMvScale", &Lua_SetDlssSdpMvScale, CallKind::Occasional},
        {"reportCameraRecord", &Lua_ReportCameraRecord, CallKind::PerFrame},
        {"setDlssMvFix", &Lua_SetDlssMvFix, CallKind::Occasional},
        {"setDlssDepthModel", &Lua_SetDlssDepthModel, CallKind::Occasional},
        {"setDlssNearClip", &Lua_SetDlssNearClip, CallKind::Occasional},
        {"startPerfTrace", &Lua_StartPerfTrace, CallKind::Occasional},
        {"stopPerfTrace", &Lua_StopPerfTrace, CallKind::Occasional},
        {"isPerfTracing", &Lua_IsPerfTracing, CallKind::PerFrame},
        {"perfTraceBegin", &Lua_PerfTraceBegin, CallKind::PerFrame},
        {"perfTraceEnd", &Lua_PerfTraceEnd, CallKind::PerFrame},
    };

    // 1 version property + one field per table entry.
    static constexpr int kExposedFieldCount =
        1 + static_cast<int>(sizeof(kFunctionTable) / sizeof(kFunctionTable[0]));

private:
    // Writes the handshake's line to fs25_vr.log: both versions, a warning when they differ or the Companion Mod passed none.
    void LogVersions(const char* companionVersion) const;

    // The bridge bound as upvalue 1 of every registered function; raises a Lua error if it is missing.
    static VRBridge& FromUpvalue(lua_State* L);

    // What Inject() registers in place of an Occasional function: runs the function named by upvalue 2 (its table
    // entry) with the game's collector held off, so that no collection step starts from the Injector's copy of Luau
    // during the call. The one place the hold is applied to a function-table function.
    static int RunOccasional(lua_State* L);

    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<NgxInterceptor> m_ngxInterceptor;
    VirtualGamepad m_virtualGamepad{m_openXrManager};
    CameraRecordStore m_cameraRecords;
    std::string m_version;
    bool m_handshakeCompleted = false;
    bool m_menuVisible = false;
    bool m_2dViewActive = false;
    bool m_desktopMirrorEnabled = true;
    bool m_mouseConfinementEnabled = true;
    float m_fovOverscan = 0.0f;
    float m_menuDistance = OpenXrManager::kDefaultMenuQuadDistance;
    std::atomic<uint32_t> m_eyeSnapshotRequests{0};
    // The OpenXR manager renders without the overscan while a menu or 2D Floating Window Mode is shown.
    void UpdateFovOverscanSuspension();

    std::atomic<VRRenderingMode> m_renderingMode{VRRenderingMode::SequentialDualPass};
    static constexpr int kNoPendingMode = -1;
    std::atomic<int> m_pendingRenderingMode{kNoPendingMode};
    std::atomic<uint32_t> m_subPassIndex{0}; // 0 = first sub-pass of the pair (advances simulation), 1 = second

    uint32_t m_frameParity = 0; // 0 = Left (2k), 1 = Right (2k+1)
    uint64_t m_frameIndex = 0;
    float m_simulatedIpd = 0.064f;
    bool m_hasCustomIpd = false;
    void ResetSubPassAndParity();
    std::atomic<bool> m_discontinuityPendingLeft{false};
    std::atomic<bool> m_discontinuityPendingRight{false};
};

} // namespace fs25vr
