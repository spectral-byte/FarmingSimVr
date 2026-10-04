#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "lua.h"
#include "lualib.h"
#include "openxr/openxr_manager.h"
#include "telemetry/graphics_telemetry_probe.h"

namespace fs25vr {

enum class VRRenderingMode : uint32_t {
    AFR = 0,
    SequentialDualPass = 1,
    AlternateFrame = AFR
};

class VRBridge {
public:
    static constexpr int kExposedFieldCount = 30; // 1 version property + 29 exposed functions

    static VRBridge& Instance();

    explicit VRBridge(std::shared_ptr<OpenXrManager> openXrManager = nullptr,
                      std::shared_ptr<GraphicsTelemetryProbe> telemetryProbe = nullptr);

    bool Inject(lua_State* L);
    bool IsInjected(lua_State* L);

    const std::string& GetVersion() const { return m_version; }
    bool IsReady() const;
    bool PerformHandshake();
    bool IsCompanionActive() const { return m_handshakeCompleted; }
    void SetCompanionActive(bool active) { m_handshakeCompleted = active; }
    std::shared_ptr<GraphicsTelemetryProbe> GetTelemetryProbe() const { return m_telemetryProbe; }
    std::shared_ptr<OpenXrManager> GetOpenXrManager() const { return m_openXrManager; }

    HeadPose GetHeadPose();
    HeadPose GetHeadPoseForEye(uint32_t eyeIndex);
    void Recenter();
    EyeProjection GetEyeProjection(uint32_t eyeIndex = 0);

    bool RequiresAfrFallback() const;

    VRRenderingMode GetRenderingMode() const { return m_renderingMode.load(); }
    void SetRenderingMode(VRRenderingMode mode);
    bool IsSequentialDualPass() const { return m_renderingMode.load() == VRRenderingMode::SequentialDualPass; }

    uint32_t GetSubPassIndex() const { return m_subPassIndex.load(); }
    void SetSubPassIndex(uint32_t index) { m_subPassIndex.store(index % 2); }
    uint32_t AdvanceSubPass() {
        return m_subPassIndex.fetch_xor(1) ^ 1;
    }
    void ResetSubPass() { m_subPassIndex.store(0); }

    uint32_t GetFrameParity() const {
        if (m_renderingMode.load() == VRRenderingMode::SequentialDualPass) {
            return m_subPassIndex.load();
        }
        return m_frameParity;
    }
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

    static int Lua_GetRenderingMode(lua_State* L);
    static int Lua_SetRenderingMode(lua_State* L);
    static int Lua_GetSubPassIndex(lua_State* L);
    static int Lua_SetSubPassIndex(lua_State* L);
    static int Lua_AdvanceSubPass(lua_State* L);

    static int Lua_GetFrameParity(lua_State* L);
    static int Lua_GetFrameIndex(lua_State* L);
    static int Lua_GetIpd(lua_State* L);
    static int Lua_GetEyePose(lua_State* L);
    static int Lua_Index(lua_State* L);

    static void PushEyeProjectionTable(lua_State* L, const EyeProjection& proj, uint32_t eyeIndex);

private:
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<GraphicsTelemetryProbe> m_telemetryProbe;
    std::string m_version = "1.0.0";
    bool m_handshakeCompleted = false;
    bool m_menuVisible = false;
    bool m_2dViewActive = false;

    std::atomic<VRRenderingMode> m_renderingMode{VRRenderingMode::SequentialDualPass};
    std::atomic<uint32_t> m_subPassIndex{0}; // 0 = Left Eye, 1 = Right Eye

    uint32_t m_frameParity = 0; // 0 = Left (2k), 1 = Right (2k+1)
    uint64_t m_frameIndex = 0;
    float m_simulatedIpd = 0.064f;
    bool m_hasCustomIpd = false;
    void ResetSubPassAndParity();
    std::atomic<bool> m_discontinuityPendingLeft{false};
    std::atomic<bool> m_discontinuityPendingRight{false};
};

} // namespace fs25vr
