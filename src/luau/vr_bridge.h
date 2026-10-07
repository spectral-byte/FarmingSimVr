#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include "lua.h"
#include "lualib.h"
#include "core/camera_records.h"
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
    // The composition root (InjectorRuntime) supplies both subsystems; a null one is absent.
    VRBridge(std::shared_ptr<OpenXrManager> openXrManager,
             std::shared_ptr<NgxInterceptor> ngxInterceptor);

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

    // The engine steps its TAA/upscaler jitter once per rendered frame, so a fixed L-then-R order hands
    // each eye only one parity of the sequence (half a pixel of horizontal jitter). SDP swaps which eye
    // renders in sub-pass 0 every kEyeOrderSwapPairs pairs, which stratifies each eye's horizontal
    // jitter over every 16 frames whatever the engine's sequence phase.
    static constexpr uint64_t kEyeOrderSwapPairs = 4;
    uint64_t GetDualPassPairIndex() const { return m_dualPassPairIndex.load(); }
    bool IsEyeOrderSwapped() const { return ((m_dualPassPairIndex.load() / kEyeOrderSwapPairs) & 1) != 0; }

    // The eye the current frame renders: the sub-pass mapped through the eye order under SDP,
    // the frame parity under AFR.
    uint32_t GetActiveEye() const {
        if (m_renderingMode.load() == VRRenderingMode::SequentialDualPass) {
            return m_subPassIndex.load() ^ (IsEyeOrderSwapped() ? 1u : 0u);
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
    static int Lua_Recenter(lua_State* L);
    static int Lua_AlignMenuQuad(lua_State* L);
    static int Lua_GetEyeProjection(lua_State* L);
    static int Lua_SetMenuVisible(lua_State* L);
    static int Lua_GetIsMenuVisible(lua_State* L);
    static int Lua_Set2DViewActive(lua_State* L);
    static int Lua_GetIs2DViewActive(lua_State* L);
    static int Lua_ResetTemporalHistory(lua_State* L);
    static int Lua_SignalDiscontinuity(lua_State* L);
    static int Lua_SetDlssMvScale(lua_State* L);
    static int Lua_SetDlssSharpness(lua_State* L);
    static int Lua_SetDlssPreset(lua_State* L);
    static int Lua_DlssDiag(lua_State* L);
    static int Lua_SetDlssMvFix(lua_State* L);
    static int Lua_SetDlssDepthModel(lua_State* L);
    static int Lua_SetDlssSdpMvScale(lua_State* L);
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
    static void FillEyeProjectionTable(lua_State* L, int tableIndex, const EyeProjection& proj, uint32_t eyeIndex);

    struct LuaFunctionEntry {
        const char* name;
        lua_CFunction fn;
    };

    // Single source of truth for the Lua-visible functions registered by Inject().
    static constexpr LuaFunctionEntry kFunctionTable[] = {
        {"isReady", &Lua_IsReady},
        {"handshake", &Lua_Handshake},
        {"isCompanionActive", &Lua_IsCompanionActive},
        {"onMapUnload", &Lua_OnMapUnload},
        {"getStereoTelemetry", &Lua_GetStereoTelemetry},
        {"isStereoActive", &Lua_IsStereoActive},
        {"getRequiresAfrFallback", &Lua_RequiresAfrFallback},
        {"getHeadPose", &Lua_GetHeadPose},
        {"recenter", &Lua_Recenter},
        {"getEyeProjection", &Lua_GetEyeProjection},
        {"getFrameParity", &Lua_GetFrameParity},
        {"getFrameIndex", &Lua_GetFrameIndex},
        {"getIpd", &Lua_GetIpd},
        {"getEyePose", &Lua_GetEyePose},
        {"setMenuVisible", &Lua_SetMenuVisible},
        {"getIsMenuVisible", &Lua_GetIsMenuVisible},
        {"alignMenuQuad", &Lua_AlignMenuQuad},
        {"set2DViewActive", &Lua_Set2DViewActive},
        {"getIs2DViewActive", &Lua_GetIs2DViewActive},
        {"getRenderingMode", &Lua_GetRenderingMode},
        {"setRenderingMode", &Lua_SetRenderingMode},
        {"getSubPassIndex", &Lua_GetSubPassIndex},
        {"setSubPassIndex", &Lua_SetSubPassIndex},
        {"advanceSubPass", &Lua_AdvanceSubPass},
        {"getActiveEye", &Lua_GetActiveEye},
        {"resetTemporalHistory", &Lua_ResetTemporalHistory},
        {"signalDiscontinuity", &Lua_SignalDiscontinuity},
        {"setDlssMvScale", &Lua_SetDlssMvScale},
        {"setDlssSharpness", &Lua_SetDlssSharpness},
        {"setDlssPreset", &Lua_SetDlssPreset},
        {"dlssDiag", &Lua_DlssDiag},
        {"setDlssSdpMvScale", &Lua_SetDlssSdpMvScale},
        {"reportCameraRecord", &Lua_ReportCameraRecord},
        {"setDlssMvFix", &Lua_SetDlssMvFix},
        {"setDlssDepthModel", &Lua_SetDlssDepthModel},
    };

    // 1 version property + one field per table entry.
    static constexpr int kExposedFieldCount =
        1 + static_cast<int>(sizeof(kFunctionTable) / sizeof(kFunctionTable[0]));

private:
    // The bridge bound as upvalue 1 of every registered function; raises a Lua error if it is missing.
    static VRBridge& FromUpvalue(lua_State* L);

    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<NgxInterceptor> m_ngxInterceptor;
    CameraRecordStore m_cameraRecords;
    std::string m_version = "1.0.0";
    bool m_handshakeCompleted = false;
    bool m_menuVisible = false;
    bool m_2dViewActive = false;

    std::atomic<VRRenderingMode> m_renderingMode{VRRenderingMode::SequentialDualPass};
    static constexpr int kNoPendingMode = -1;
    std::atomic<int> m_pendingRenderingMode{kNoPendingMode};
    std::atomic<uint32_t> m_subPassIndex{0}; // 0 = first sub-pass of the pair (advances simulation), 1 = second
    std::atomic<uint64_t> m_dualPassPairIndex{0}; // SDP pairs completed since the mode or handshake began

    uint32_t m_frameParity = 0; // 0 = Left (2k), 1 = Right (2k+1)
    uint64_t m_frameIndex = 0;
    float m_simulatedIpd = 0.064f;
    bool m_hasCustomIpd = false;
    void ResetSubPassAndParity();
    std::atomic<bool> m_discontinuityPendingLeft{false};
    std::atomic<bool> m_discontinuityPendingRight{false};
};

} // namespace fs25vr
