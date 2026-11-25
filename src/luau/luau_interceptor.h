#pragma once

#include <memory>
#include "vr_bridge.h"
#include "core/detour.h"

namespace fs25vr {

constexpr const char* kExpectedLuauVersion = "0.662";

constexpr const char* kHostLuauFingerprint[] = {
    "LuauStoreCSTData",
    "LuauFixDoBlockEndLocation",
    "ParserNoErrorLimit",
    "LuauCompileMathLerp",
    "LuauAllowFragmentParsing",
    "LuauLibWhereErrorAutoreserve",
    "LuauStringFormatFixC",
    "LuauMathLerp",
    "LuauVector2Constructor",
};

bool HostEmbedsMatchingLuau(const uint8_t* rdataBase, size_t rdataSize);

class LuauInterceptor {
public:
    explicit LuauInterceptor(std::shared_ptr<VRBridge> bridge);
    ~LuauInterceptor();

    void SetOwnedByRuntime(bool owned) { m_ownedByRuntime = owned; }
    void SetHostModule(void* hostModule) { m_hostModule = hostModule; }
    bool Attach(void* targetQueryFunction);
    bool AttachToEngine();
    bool AttachToHostSections(const uint8_t* rdataBase, size_t rdataSize, const uint8_t* textBase, size_t textSize);
    void Detach();

    bool IsIntercepted() const { return m_activeState != nullptr; }
    lua_State* GetActiveState() const { return m_activeState; }

    int HandleQuery(lua_State* L);

    static int DetourEntryPoint(lua_State* L);
    static void* FindQueryFunctionInText(const uint8_t* textBase, size_t textSize, const uint8_t* stringAddr);

    FunctionDetour& GetDetour() { return m_detour; }

private:
    std::shared_ptr<VRBridge> m_bridge;
    FunctionDetour m_detour;
    lua_State* m_activeState = nullptr;
    bool m_bridgeInjected = false;
    bool m_ownedByRuntime = false;
    void* m_hostModule = nullptr;
};

}
