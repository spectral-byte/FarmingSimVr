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
    VRBridge(std::shared_ptr<OpenXrManager> openXrManager,
             std::shared_ptr<NgxInterceptor> ngxInterceptor,
             std::string version = std::string(kInjectorVersion));

    bool Inject(lua_State* L);
    bool IsInjected(lua_State* L);

    const std::string& GetVersion() const { return m_version; }
    bool IsReady() const;
    bool PerformHandshake();
    bool IsCompanionActive() const { return m_handshakeCompleted.load(); }
    void SetCompanionActive(bool active) { m_handshakeCompleted.store(active); }
    std::shared_ptr<OpenXrManager> GetOpenXrManager() const { return m_openXrManager; }
    std::shared_ptr<NgxInterceptor> GetNgxInterceptor() const { return m_ngxInterceptor; }

    HeadPose GetHeadPose();
    HeadPose GetHeadPoseForEye(uint32_t eyeIndex);
    HandState GetHandState(uint32_t hand);
    bool PulseHaptic(uint32_t hand, float durationMs, float strength);
    VirtualGamepad& GetVirtualGamepad() { return m_virtualGamepad; }
    PanelHit GetPanelHit(uint32_t hand);
    PanelHit GetPanelHitAlongRay(uint32_t hand, const Vector3& origin, const Vector3& direction);
    void SetMenuCursor(bool active, float u, float v);
    void Recenter();
    EyeProjection GetEyeProjection(uint32_t eyeIndex = 0);

    bool RequiresAfrFallback() const { return true; }

    VRRenderingMode GetRenderingMode() const { return m_renderingMode.load(); }
    void SetRenderingMode(VRRenderingMode mode);

    void SetPendingRenderingMode(VRRenderingMode mode) { m_pendingRenderingMode.store(static_cast<int>(mode)); }
    std::optional<VRRenderingMode> GetPendingRenderingMode() const;
    void ClearPendingRenderingMode() { m_pendingRenderingMode.store(kNoPendingMode); }
    bool LatchPendingRenderingMode();
    bool IsSequentialDualPass() const { return m_renderingMode.load() == VRRenderingMode::SequentialDualPass; }

    CameraRecordStore& GetCameraRecords() { return m_cameraRecords; }
    const CameraRecordStore& GetCameraRecords() const { return m_cameraRecords; }

    uint32_t GetSubPassIndex() const { return m_subPassIndex.load(); }
    void SetSubPassIndex(uint32_t index) { m_subPassIndex.store(index % 2); }
    uint32_t AdvanceSubPass() {
        return m_subPassIndex.fetch_xor(1) ^ 1;
    }
    void ResetSubPass() { m_subPassIndex.store(0); }

    uint32_t GetActiveEye() const {
        if (m_renderingMode.load() == VRRenderingMode::SequentialDualPass) {
            return m_subPassIndex.load();
        }
        return m_frameParity.load();
    }
    uint32_t GetFrameParity() const { return GetActiveEye(); }
    uint64_t GetFrameIndex() const { return m_frameIndex; }
    void AdvanceFrame();
    void SetFrameParity(uint32_t parity);
    void SetFrameIndex(uint64_t index);
    float GetIpd() const;
    void SetIpd(float ipd);
    HeadPose GetEyePose(uint32_t eyeIndex);

    bool IsMenuVisible() const { return m_menuVisible.load(); }
    void SetMenuVisible(bool visible);
    void AlignMenuQuad();

    bool Is2DViewActive() const { return m_2dViewActive.load(); }
    void Set2DViewActive(bool active);

    bool IsDesktopMirrorEnabled() const { return m_desktopMirrorEnabled; }
    void SetDesktopMirrorEnabled(bool enabled) { m_desktopMirrorEnabled = enabled; }

    float GetFovOverscan() const { return m_fovOverscan; }
    void SetFovOverscan(float percent);

    float GetMenuDistance() const { return m_menuDistance; }
    void SetMenuDistance(float metres);

    void RequestEyeSnapshot() { m_eyeSnapshotRequests.store(0b11u); }
    bool ConsumeEyeSnapshotRequest(uint32_t eyeIndex) {
        const uint32_t bit = 1u << (eyeIndex & 1u);
        return (m_eyeSnapshotRequests.fetch_and(~bit) & bit) != 0;
    }

    bool IsMouseConfinementEnabled() const { return m_mouseConfinementEnabled; }
    void SetMouseConfinementEnabled(bool enabled) { m_mouseConfinementEnabled = enabled; }

    void SignalDiscontinuity();
    bool HasPendingDiscontinuity(uint32_t contextIndex = 0) const;
    bool ConsumeDiscontinuity(uint32_t contextIndex = 0);

    static int Lua_IsReady(lua_State* L);
    static int Lua_Handshake(lua_State* L);
    static int Lua_IsCompanionActive(lua_State* L);
    static int Lua_OnMapUnload(lua_State* L);
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
    static int Lua_StartPerfTrace(lua_State* L);
    static int Lua_StopPerfTrace(lua_State* L);
    static int Lua_IsPerfTracing(lua_State* L);
    static int Lua_PerfTraceBegin(lua_State* L);
    static int Lua_PerfTraceEnd(lua_State* L);
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
    static void PushPoseTable(lua_State* L, const HeadPose& pose, int eyeIndex = -1);
    static void FillPoseTable(lua_State* L, int tableIndex, const HeadPose& pose, int eyeIndex = -1);
    static void PushHandStateTable(lua_State* L, const HandState& hand);
    static void FillHandStateTable(lua_State* L, int tableIndex, const HandState& hand);
    static void FillEyeProjectionTable(lua_State* L, int tableIndex, const EyeProjection& proj, uint32_t eyeIndex);

    static constexpr int kBridgeUpvalue = 1;
    static constexpr int kAnchorsUpvalue = 2;
    static constexpr int kEntryUpvalue = 3;

    static void PushAnchoredString(lua_State* L, int anchorsIndex, const char* text);

    enum class CallKind : uint8_t { Unmarked = 0, PerFrame, Occasional };

    struct LuaFunctionEntry {
        const char* name;
        lua_CFunction fn;
        CallKind kind = CallKind::Unmarked;
    };

    static constexpr LuaFunctionEntry kFunctionTable[] = {
        {"isReady", &Lua_IsReady, CallKind::PerFrame},
        {"handshake", &Lua_Handshake, CallKind::Occasional},
        {"isCompanionActive", &Lua_IsCompanionActive, CallKind::PerFrame},
        {"onMapUnload", &Lua_OnMapUnload, CallKind::Occasional},
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

    static constexpr int kExposedFieldCount =
        1 + static_cast<int>(sizeof(kFunctionTable) / sizeof(kFunctionTable[0]));

private:
    void LogVersions(const char* companionVersion) const;

    static VRBridge& FromUpvalue(lua_State* L);

    static int RunOccasional(lua_State* L);

    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<NgxInterceptor> m_ngxInterceptor;
    VirtualGamepad m_virtualGamepad{m_openXrManager};
    CameraRecordStore m_cameraRecords;
    std::string m_version;
    std::atomic<bool> m_handshakeCompleted{false};
    std::atomic<bool> m_menuVisible{false};
    std::atomic<bool> m_2dViewActive{false};
    bool m_desktopMirrorEnabled = true;
    bool m_mouseConfinementEnabled = true;
    float m_fovOverscan = 0.0f;
    float m_menuDistance = OpenXrManager::kDefaultMenuQuadDistance;
    std::atomic<uint32_t> m_eyeSnapshotRequests{0};
    void UpdateFovOverscanSuspension();

    std::atomic<VRRenderingMode> m_renderingMode{VRRenderingMode::SequentialDualPass};
    static constexpr int kNoPendingMode = -1;
    std::atomic<int> m_pendingRenderingMode{kNoPendingMode};
    std::atomic<uint32_t> m_subPassIndex{0}; // 0 = first sub-pass of the pair (advances simulation), 1 = second

    std::atomic<uint32_t> m_frameParity{0}; // 0 = Left (2k), 1 = Right (2k+1)
    uint64_t m_frameIndex = 0;
    float m_simulatedIpd = 0.064f;
    bool m_hasCustomIpd = false;
    void ResetSubPassAndParity();
    std::atomic<bool> m_discontinuityPendingLeft{false};
    std::atomic<bool> m_discontinuityPendingRight{false};
};

}
