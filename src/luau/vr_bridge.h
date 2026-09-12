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
    std::shared_ptr<GraphicsTelemetryProbe> GetTelemetryProbe() const { return m_telemetryProbe; }

    static int Lua_IsReady(lua_State* L);
    static int Lua_Handshake(lua_State* L);
    static int Lua_GetStereoTelemetry(lua_State* L);
    static int Lua_IsStereoActive(lua_State* L);

private:
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::shared_ptr<GraphicsTelemetryProbe> m_telemetryProbe;
    std::string m_version = "1.0.0";
    bool m_handshakeCompleted = false;
};

} // namespace fs25vr
