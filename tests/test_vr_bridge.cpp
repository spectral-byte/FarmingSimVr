#include <gtest/gtest.h>
#include "luau/vr_bridge.h"
#include "luacode.h"
#include <string>

namespace {

int RunLuauString(lua_State* L, const std::string& code) {
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code.c_str(), code.length(), nullptr, &bytecodeSize);
    if (!bytecode) {
        return -1;
    }
    int loadResult = luau_load(L, "=test_chunk", bytecode, bytecodeSize, 0);
    free(bytecode);
    if (loadResult != 0) {
        return loadResult;
    }
    return lua_pcall(L, 0, 0, 0);
}

} // anonymous namespace

class VrBridgeTest : public ::testing::Test {
protected:
    lua_State* L = nullptr;

    void SetUp() override {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    void TearDown() override {
        if (L) {
            lua_close(L);
            L = nullptr;
        }
    }
};

TEST_F(VrBridgeTest, InjectsGlobalTableWithoutStackCorruption) {
    fs25vr::VRBridge bridge;

    int initialTop = lua_gettop(L);
    EXPECT_FALSE(bridge.IsInjected(L));

    bool injected = bridge.Inject(L);
    EXPECT_TRUE(injected);
    EXPECT_TRUE(bridge.IsInjected(L));

    // The Lua stack top must remain strictly unaltered
    EXPECT_EQ(lua_gettop(L), initialTop);

    // Second injection should be a clean no-op
    bool secondInject = bridge.Inject(L);
    EXPECT_TRUE(secondInject);
    EXPECT_EQ(lua_gettop(L), initialTop);
}

TEST_F(VrBridgeTest, ExposesVersionAndIsReadyAndHandshakeToLuauScript) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"(
        assert(VRBridge ~= nil, "VRBridge global table must exist")
        assert(VRBridge.version == "1.0.0", "VRBridge.version must be '1.0.0'")
        assert(type(VRBridge.isReady) == "function", "VRBridge.isReady must be a function")
        assert(type(VRBridge.handshake) == "function", "VRBridge.handshake must be a function")

        local ready = VRBridge.isReady()
        assert(type(ready) == "boolean", "isReady() must return boolean")

        local hs = VRBridge.handshake()
        assert(type(hs) == "table", "handshake() must return a table")
        assert(hs.success == true, "hs.success must be true")
        assert(hs.version == "1.0.0", "hs.version must match")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, ExposesStereoTelemetryAndStereoStatusToLuauScript) {
    auto probe = std::make_shared<fs25vr::GraphicsTelemetryProbe>();
    fs25vr::VRBridge bridge(nullptr, probe);
    ASSERT_TRUE(bridge.Inject(L));

    D3D12_VIEWPORT vp{ 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    probe->RecordViewports(1, &vp);
    probe->OnFrameBoundary();

    std::string script = R"(
        assert(type(VRBridge.getStereoTelemetry) == "function", "getStereoTelemetry must be a function")
        assert(type(VRBridge.isStereoActive) == "function", "isStereoActive must be a function")

        local t = VRBridge.getStereoTelemetry()
        assert(type(t) == "table", "getStereoTelemetry must return a table")
        assert(t.frameIndex == 1, "frameIndex must be 1")
        assert(t.maxViewports == 1, "maxViewports must be 1")
        assert(t.passCount == 1, "passCount must be 1")
        assert(type(t.nativeStereo) == "boolean", "nativeStereo must be boolean")
        assert(type(t.afrFallback) == "boolean", "afrFallback must be boolean")
        assert(type(t.verdict) == "string", "verdict must be string")
        assert(type(t.summary) == "string", "summary must be string")

        local active = VRBridge.isStereoActive()
        assert(type(active) == "boolean", "isStereoActive must return boolean")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

