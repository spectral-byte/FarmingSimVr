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
    simPose.position = fs25vr::Vector3(0.12f, -0.05f, 0.88f);
    simPose.rotation = fs25vr::EulerAngles(0.25f, -0.15f, 0.05f);
    simPose.orientation = fs25vr::Quaternion(0.12f, -0.07f, 0.02f, 0.99f);
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
    simPose.position = fs25vr::Vector3(1.5f, 0.5f, -2.0f);
    simPose.rotation = fs25vr::EulerAngles(0.1f, 0.3f, 0.0f);
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

TEST_F(VrBridgeTest, ExposesEyeProjectionAndPrunesAliasesInLuauScript) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };

    oxr->SetSimulatedProjection(0, fs25vr::OpenXrManager::ComputeProjectionFromFov(leftFov));
    oxr->SetSimulatedProjection(1, fs25vr::OpenXrManager::ComputeProjectionFromFov(rightFov));

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        assert(type(VRBridge.getEyeProjection) == "function", "getEyeProjection must be a function")
        -- Unrequested view accessor and redundant aliases must be pruned
        assert(VRBridge.getViews == nil, "getViews must be pruned")
        assert(VRBridge.getProjection == nil, "getProjection alias must be pruned")

        -- Eye 0 (Left Eye)
        local left = VRBridge.getEyeProjection(0)
        assert(type(left) == "table", "getEyeProjection(0) must return table")
        assert(left.isValid == true, "left projection must be valid")
        assert(left.eye == 0, "left eye index must be 0")
        assert(math.abs(left.xOffset - (-0.23052)) < 0.005, "left xOffset mismatch")
        assert(math.abs(left.yOffset) < 0.005, "left yOffset mismatch")
        assert(math.abs(left.fovY - 1.70) < 0.01, "left fovY mismatch")

        -- Verify redundant aliases are pruned from projection table
        assert(left.offsetX == nil, "offsetX alias must be pruned")
        assert(left.offsetY == nil, "offsetY alias must be pruned")
        assert(left.xOffsetTan == nil, "xOffsetTan alias must be pruned")
        assert(left.yOffsetTan == nil, "yOffsetTan alias must be pruned")
        assert(left.xOffsetNorm == nil, "xOffsetNorm alias must be pruned")
        assert(left.yOffsetNorm == nil, "yOffsetNorm alias must be pruned")

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

        -- Flexible indexing
        local byStrL = VRBridge.getEyeProjection("left")
        local byStrR = VRBridge.getEyeProjection("right")
        assert(math.abs(byStrL.xOffset - left.xOffset) < 0.0001, "byStrL mismatch")
        assert(math.abs(byStrR.xOffset - right.xOffset) < 0.0001, "byStrR mismatch")

        -- Default call without argument returns active parity eye
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

        -- Mutating parity functions must NOT be exposed to Lua (read-only for scripts)
        assert(VRBridge.advanceFrame == nil, "VRBridge.advanceFrame must not be exposed to Lua")
        assert(VRBridge.setFrameParity == nil, "VRBridge.setFrameParity must not be exposed to Lua")

        -- requiresAfrFallback property and function
        assert(VRBridge.requiresAfrFallback == true, "requiresAfrFallback property must be true")
        assert(type(VRBridge.getRequiresAfrFallback) == "function", "getRequiresAfrFallback must be function")
        assert(VRBridge.getRequiresAfrFallback() == true, "getRequiresAfrFallback() must return true")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }

    // Advance frame from native C++ to frame 1 (parity 1 = Right eye)
    bridge.AdvanceFrame();
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    EXPECT_EQ(bridge.GetFrameIndex(), 1u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.frameParity == 1, "VRBridge.frameParity must now be 1")
        assert(VRBridge.getFrameParity() == 1, "VRBridge.getFrameParity() must now be 1")
        assert(VRBridge.frameIndex == 1, "VRBridge.frameIndex must now be 1")
    )luau"), 0);

    // Advance frame from native C++ to frame 2 (parity 0 = Left eye)
    bridge.AdvanceFrame();
    EXPECT_EQ(bridge.GetFrameParity(), 0u);
    EXPECT_EQ(bridge.GetFrameIndex(), 2u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.frameParity == 0, "VRBridge.frameParity must now be 0")
        assert(VRBridge.frameIndex == 2, "VRBridge.frameIndex must now be 2")
    )luau"), 0);

    // Direct C++ SetFrameParity
    bridge.SetFrameParity(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.frameParity == 1, "VRBridge.frameParity must be 1 after native SetFrameParity(1)")
    )luau"), 0);
}

TEST_F(VrBridgeTest, ExposesEyePoseWithInterpupillaryShiftToLuau) {
    auto oxr = std::make_shared<fs25vr::OpenXrManager>();
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.0f, 1.7f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.orientation = fs25vr::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
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

TEST_F(VrBridgeTest, RequiresAfrFallbackReflectsTelemetryProbeVerdict) {
    auto probe = std::make_shared<fs25vr::GraphicsTelemetryProbe>();
    fs25vr::VRBridge bridge(nullptr, probe);
    ASSERT_TRUE(bridge.Inject(L));

    // Initially 0 frames, Inconclusive -> RequiresAfrFallback() is false
    EXPECT_FALSE(bridge.RequiresAfrFallback());

    // Record 5 monoscopic frames
    D3D12_VIEWPORT vp{ 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    D3D12_RECT scissor{ 0, 0, 1920, 1080 };
    for (int i = 0; i < 5; ++i) {
        probe->RecordViewports(1, &vp);
        probe->RecordScissorRects(1, &scissor);
        probe->OnFrameBoundary();
    }

    EXPECT_TRUE(probe->RequiresAfrFallback());
    EXPECT_TRUE(bridge.RequiresAfrFallback());
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.requiresAfrFallback == true, "must be true when AFR fallback required")
        assert(VRBridge.getRequiresAfrFallback() == true, "must return true when AFR fallback required")
    )luau"), 0);

    // Reset probe and simulate native stereo pass
    probe->Reset();
    D3D12_VIEWPORT vps[2]{
        { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f },
        { 1920.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f }
    };
    D3D12_RECT scissors[2]{
        { 0, 0, 1920, 1080 },
        { 1920, 0, 3840, 1080 }
    };
    probe->RecordViewports(2, vps);
    probe->RecordScissorRects(2, scissors);
    probe->OnFrameBoundary();

    EXPECT_TRUE(probe->HasNativeStereoDetected());
    EXPECT_FALSE(probe->RequiresAfrFallback());
    EXPECT_FALSE(bridge.RequiresAfrFallback());
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.requiresAfrFallback == false, "must be false when native stereo detected")
        assert(VRBridge.getRequiresAfrFallback() == false, "must return false when native stereo detected")
    )luau"), 0);
}

TEST_F(VrBridgeTest, ExposesCompanionActiveStatusAndHandlesMapUnload) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_FALSE(bridge.IsCompanionActive());

    // Initially inactive before handshake
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.isCompanionActive) == "function", "isCompanionActive must be a function")
        assert(type(VRBridge.onMapUnload) == "function", "onMapUnload must be a function")
        assert(VRBridge.isCompanionActive() == false, "must be false before handshake")
    )luau"), 0);

    // Perform handshake
    ASSERT_EQ(RunLuauString(L, R"luau(
        local res = VRBridge.handshake()
        assert(res.success == true, "handshake must succeed")
        assert(VRBridge.isCompanionActive() == true, "must be true after handshake")
    )luau"), 0);

    EXPECT_TRUE(bridge.IsCompanionActive());

    // Unload map
    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.onMapUnload()
        assert(VRBridge.isCompanionActive() == false, "must be false after map unload")
    )luau"), 0);

    EXPECT_FALSE(bridge.IsCompanionActive());
}

TEST_F(VrBridgeTest, ExposesMenuVisibilityAndSynchronizesStateBetweenLuauAndCpp) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    // Initially false in both C++ and Luau
    EXPECT_FALSE(bridge.IsMenuVisible());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.setMenuVisible) == "function", "setMenuVisible must be a function")
        assert(type(VRBridge.getIsMenuVisible) == "function", "getIsMenuVisible must be a function")
        assert(VRBridge.getIsMenuVisible() == false, "initial getIsMenuVisible must be false")
        assert(VRBridge.isMenuVisible == false, "initial isMenuVisible property must be false")
    )luau"), 0);

    // Set menu visible from Luau
    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.setMenuVisible(true)
        assert(VRBridge.getIsMenuVisible() == true, "getIsMenuVisible must return true after setMenuVisible(true)")
        assert(VRBridge.isMenuVisible == true, "isMenuVisible property must return true after setMenuVisible(true)")
    )luau"), 0);

    EXPECT_TRUE(bridge.IsMenuVisible());

    // Frame parity reset test: setting parity to 1, then setMenuVisible should reset parity to 0
    bridge.SetFrameParity(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    bridge.SetMenuVisible(false);
    EXPECT_FALSE(bridge.IsMenuVisible());
    EXPECT_EQ(bridge.GetFrameParity(), 0u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIsMenuVisible() == false, "getIsMenuVisible must reflect C++ SetMenuVisible(false)")
    )luau"), 0);

    // Dynamic property access via __index
    bridge.SetMenuVisible(true);
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIsMenuVisible() == true, "must be true")
    )luau"), 0);
}

TEST_F(VrBridgeTest, MapUnloadResetsMenuVisibilityToFalse) {
    fs25vr::VRBridge bridge;
    ASSERT_TRUE(bridge.Inject(L));

    bridge.SetMenuVisible(true);
    EXPECT_TRUE(bridge.IsMenuVisible());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIsMenuVisible() == true, "menu should be visible before unload")
        VRBridge.onMapUnload()
        assert(VRBridge.getIsMenuVisible() == false, "menu visibility must be reset to false after onMapUnload")
    )luau"), 0);

    EXPECT_FALSE(bridge.IsMenuVisible());
}

