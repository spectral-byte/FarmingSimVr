#pragma once

#include <memory>
#include <string>
#include "lua.h"
#include "lualib.h"
#include "openxr/openxr_manager.h"

namespace fs25vr {

class VRBridge {
public:
    static VRBridge& Instance();

    explicit VRBridge(std::shared_ptr<OpenXrManager> openXrManager = nullptr);

    bool Inject(lua_State* L);
    bool IsInjected(lua_State* L);

    const std::string& GetVersion() const { return m_version; }
    bool IsReady() const;
    bool PerformHandshake();

    static int Lua_IsReady(lua_State* L);
    static int Lua_Handshake(lua_State* L);

private:
    std::shared_ptr<OpenXrManager> m_openXrManager;
    std::string m_version = "1.0.0";
    bool m_handshakeCompleted = false;
};

} // namespace fs25vr
