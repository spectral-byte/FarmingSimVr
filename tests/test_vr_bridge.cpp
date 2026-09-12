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

TEST_F(VrBridgeTest, ExposesHeadPoseToLuauScript) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.x = 0.12f;
    simPose.y = -0.05f;
    simPose.z = 0.88f;
    simPose.pitch = 0.25f;
    simPose.yaw = -0.15f;
    simPose.roll = 0.05f;
    simPose.qx = 0.12f;
    simPose.qy = -0.07f;
    simPose.qz = 0.02f;
    simPose.qw = 0.99f;
    simPose.isValid = true;
    simPose.positionTracked = true;
    simPose.orientationTracked = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"(
        assert(type(VRBridge.getHeadPose) == "function", "getHeadPose must be function")
        assert(type(VRBridge.recenter) == "function", "recenter must be function")

        local pose = VRBridge.getHeadPose()
        assert(type(pose) == "table", "getHeadPose must return table")
        assert(math.abs(pose.x - 0.12) < 0.001, "pose.x mismatch")
        assert(math.abs(pose.y - (-0.05)) < 0.001, "pose.y mismatch")
        assert(math.abs(pose.z - 0.88) < 0.001, "pose.z mismatch")
        assert(math.abs(pose.pitch - 0.25) < 0.001, "pose.pitch mismatch")
        assert(math.abs(pose.yaw - (-0.15)) < 0.001, "pose.yaw mismatch")
        assert(math.abs(pose.roll - 0.05) < 0.001, "pose.roll mismatch")
        assert(pose.isValid == true, "pose.isValid must be true")
        assert(pose.isPositionTracked == true, "pose.isPositionTracked must be true")
        assert(pose.isOrientationTracked == true, "pose.isOrientationTracked must be true")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, RecenterViaLuauResetsTranslationalOffsets) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.x = 1.5f;
    simPose.y = 0.5f;
    simPose.z = -2.0f;
    simPose.pitch = 0.1f;
    simPose.yaw = 0.3f;
    simPose.isValid = true;
    simPose.positionTracked = true;
    simPose.orientationTracked = true;
    oxr->SetSimulatedPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"(
        local res = VRBridge.recenter()
        assert(type(res) == "table", "recenter must return table")
        assert(res.success == true, "recenter success must be true")

        local pose = VRBridge.getHeadPose()
        assert(math.abs(pose.x) < 0.001, "recenter must zero x")
        assert(math.abs(pose.y) < 0.001, "recenter must zero y")
        assert(math.abs(pose.z) < 0.001, "recenter must zero z")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

