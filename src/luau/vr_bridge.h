#pragma once

#include <memory>
#include <string>
#include "lua.h"
#include "lualib.h"
#include "openxr/openxr_manager.h"
#include "telemetry/graphics_telemetry_probe.h"

namespace fs25vr {

class VRBridge {
public:
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
    void Recenter();
    EyeProjection GetEyeProjection(uint32_t eyeIndex = 0);

    bool RequiresAfrFallback() const;

    uint32_t GetFrameParity() const { return m_frameParity; }
    uint64_t GetFrameIndex() const { return m_frameIndex; }
    void AdvanceFrame();
    void SetFrameParity(uint32_t parity);
    void SetFrameIndex(uint64_t index) { m_frameIndex = index; m_frameParity = static_cast<uint32_t>(index % 2); }
    float GetIpd() const;
    void SetIpd(float ipd);
    HeadPose GetEyePose(uint32_t eyeIndex);

    bool IsMenuVisible() const { return m_menuVisible; }
    void SetMenuVisible(bool visible);
    void AlignMenuQuad();

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

    uint32_t m_frameParity = 0; // 0 = Left (2k), 1 = Right (2k+1)
    uint64_t m_frameIndex = 0;
    float m_simulatedIpd = 0.064f;
    bool m_hasCustomIpd = false;
};

} // namespace fs25vr
