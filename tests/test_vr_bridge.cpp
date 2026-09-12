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

TEST_F(VrBridgeTest, ExposesEyeProjectionAndViewsToLuauScript) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };

    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));
    oxr->SetSimulatedProjection(1, fs25vr::OpenXrManager::ComputeProjectionFromFov(rightFov));

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        assert(type(VRBridge.getEyeProjection) == "function", "getEyeProjection must be a function")
        assert(type(VRBridge.getViews) == "function", "getViews must be a function")
        assert(type(VRBridge.getProjection) == "function", "getProjection alias must be a function")

        -- Eye 0 (Left Eye)
        local left = VRBridge.getEyeProjection(0)
        assert(type(left) == "table", "getEyeProjection(0) must return table")
        assert(left.isValid == true, "left projection must be valid")
        assert(left.eye == 0, "left eye index must be 0")
        assert(math.abs(left.xOffset - (-0.23052)) < 0.005, "left xOffset mismatch")
        assert(math.abs(left.yOffset) < 0.005, "left yOffset mismatch")
        assert(math.abs(left.fovY - 1.70) < 0.01, "left fovY mismatch")
        assert(left.offsetX ~= nil and math.abs(left.offsetX - left.xOffset) < 0.0001, "offsetX alias mismatch")
        assert(left.offsetY ~= nil and math.abs(left.offsetY - left.yOffset) < 0.0001, "offsetY alias mismatch")

        -- Eye 1 (Right Eye)
        local right = VRBridge.getEyeProjection(1)
        assert(type(right) == "table", "getEyeProjection(1) must return table")
        assert(right.isValid == true, "right projection must be valid")
        assert(right.eye == 1, "right eye index must be 1")
        assert(math.abs(right.xOffset - 0.23052) < 0.005, "right xOffset mismatch")
        assert(math.abs(right.yOffset) < 0.005, "right yOffset mismatch")
        assert(math.abs(right.fovY - 1.70) < 0.01, "right fovY mismatch")

        -- Asymmetry verify
        assert(left.xOffset < 0.0, "left xOffset must be negative (nasal/temporal)")
        assert(right.xOffset > 0.0, "right xOffset must be positive")

        -- Views array and table
        local views = VRBridge.getViews()
        assert(type(views) == "table", "getViews must return table")
        assert(views[1] ~= nil and views[2] ~= nil, "views array entries [1] and [2] must exist")
        assert(views.left ~= nil and views.right ~= nil, "views.left and views.right must exist")
        assert(math.abs(views.left.xOffset - left.xOffset) < 0.0001, "views.left must match left")
        assert(math.abs(views.right.xOffset - right.xOffset) < 0.0001, "views.right must match right")

        -- Flexible indexing
        local byStrL = VRBridge.getEyeProjection("left")
        local byStrR = VRBridge.getEyeProjection("right")
        assert(math.abs(byStrL.xOffset - left.xOffset) < 0.0001, "byStrL mismatch")
        assert(math.abs(byStrR.xOffset - right.xOffset) < 0.0001, "byStrR mismatch")

        -- Default call without argument
        local def = VRBridge.getEyeProjection()
        assert(def.isValid == true, "default projection must be valid")
        assert(math.abs(def.xOffset - left.xOffset) < 0.0001, "default must be left eye")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, ExposesFrameParityAndFrameIndexAndIpdToLuau) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        assert(VRBridge ~= nil, "VRBridge global table must exist")

        -- Initial frame parity and index
        assert(VRBridge.frameParity == 0, "frameParity must initially be 0")
        assert(type(VRBridge.getFrameParity) == "function", "getFrameParity must be function")
        assert(VRBridge.getFrameParity() == 0, "getFrameParity() must initially return 0")

        assert(VRBridge.frameIndex == 0, "frameIndex must initially be 0")
        assert(type(VRBridge.getFrameIndex) == "function", "getFrameIndex must be function")
        assert(VRBridge.getFrameIndex() == 0, "getFrameIndex() must initially return 0")

        -- IPD property and function
        assert(type(VRBridge.ipd) == "number", "ipd must be a number")
        assert(math.abs(VRBridge.ipd - 0.064) < 0.001, "ipd default must be ~0.064")
        assert(type(VRBridge.getIpd) == "function", "getIpd must be a function")
        assert(math.abs(VRBridge.getIpd() - 0.064) < 0.001, "getIpd() default must be ~0.064")

        -- Advance frame to frame 1 (parity 1 = Right eye)
        assert(type(VRBridge.advanceFrame) == "function", "advanceFrame must be function")
        local newParity = VRBridge.advanceFrame()
        assert(newParity == 1, "advanceFrame must return 1")
        assert(VRBridge.frameParity == 1, "VRBridge.frameParity must now be 1")
        assert(VRBridge.getFrameParity() == 1, "VRBridge.getFrameParity() must now be 1")
        assert(VRBridge.frameIndex == 1, "VRBridge.frameIndex must now be 1")

        -- Advance frame to frame 2 (parity 0 = Left eye)
        newParity = VRBridge.advanceFrame()
        assert(newParity == 0, "advanceFrame must return 0")
        assert(VRBridge.frameParity == 0, "VRBridge.frameParity must now be 0")
        assert(VRBridge.frameIndex == 2, "VRBridge.frameIndex must now be 2")

        -- Direct setFrameParity
        assert(type(VRBridge.setFrameParity) == "function", "setFrameParity must be function")
        VRBridge.setFrameParity(1)
        assert(VRBridge.frameParity == 1, "VRBridge.frameParity must be 1 after setFrameParity(1)")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, ExposesEyePoseWithInterpupillaryShiftToLuau) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.x = 0.0f;
    simPose.y = 1.7f;
    simPose.z = 0.0f;
    simPose.yaw = 0.0f;
    simPose.isValid = true;
    oxr->SetSimulatedPose(simPose);
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        assert(type(VRBridge.getEyePose) == "function", "getEyePose must be function")

        local leftEye = VRBridge.getEyePose(0)
        assert(type(leftEye) == "table", "leftEye must be table")
        assert(leftEye.isValid == true, "leftEye must be valid")
        assert(leftEye.eye == 0, "leftEye.eye must be 0")
        -- Left eye shifted by -IPD/2 (-0.032m)
        assert(math.abs(leftEye.x - (-0.032)) < 0.001, "left eye x offset mismatch")

        local rightEye = VRBridge.getEyePose(1)
        assert(type(rightEye) == "table", "rightEye must be table")
        assert(rightEye.isValid == true, "rightEye must be valid")
        assert(rightEye.eye == 1, "rightEye.eye must be 1")
        -- Right eye shifted by +IPD/2 (+0.032m)
        assert(math.abs(rightEye.x - 0.032) < 0.001, "right eye x offset mismatch")

        -- Difference between eyes must be exact IPD (0.064m)
        assert(math.abs((rightEye.x - leftEye.x) - 0.064) < 0.001, "IPD disparity mismatch")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}
