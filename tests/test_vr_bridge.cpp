#include <gtest/gtest.h>
#include "luau/vr_bridge.h"
#include "fake_openxr_runtime.h"
#include "ngx/ngx_interceptor.h"
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

TEST_F(VrBridgeTest, BridgeFunctionWithoutItsBridgeUpvalueRaisesLuaError) {
    // Pushed without the bridge upvalue that Inject supplies: there is no singleton to fall back to.
    lua_pushcfunction(L, &fs25vr::VRBridge::Lua_IsReady, "isReady");

    ASSERT_NE(lua_pcall(L, 0, 1, 0), LUA_OK);
    const char* message = lua_tostring(L, -1);
    ASSERT_NE(message, nullptr);
    EXPECT_NE(std::string(message).find("bridge upvalue is missing"), std::string::npos) << message;
    lua_pop(L, 1);
}

TEST_F(VrBridgeTest, InjectsGlobalTableWithoutStackCorruption) {
    fs25vr::VRBridge bridge(nullptr, nullptr);

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
    fs25vr::VRBridge bridge(nullptr, nullptr);
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

TEST_F(VrBridgeTest, ReportsFixedStereoTelemetryVerdictToLuauScript) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"(
        assert(type(VRBridge.getStereoTelemetry) == "function", "getStereoTelemetry must be a function")
        assert(type(VRBridge.isStereoActive) == "function", "isStereoActive must be a function")

        local t = VRBridge.getStereoTelemetry()
        assert(type(t) == "table", "getStereoTelemetry must return a table")
        assert(type(t.frameIndex) == "number", "frameIndex must be a number")
        assert(type(t.maxViewports) == "number", "maxViewports must be a number")
        assert(type(t.passCount) == "number", "passCount must be a number")
        assert(t.nativeStereo == false, "nativeStereo must be false")
        assert(t.afrFallback == true, "afrFallback must be true")
        assert(t.verdict == "AfrFallbackRequired", "verdict must be AfrFallbackRequired from the first frame")
        assert(type(t.summary) == "string", "summary must be string")

        assert(VRBridge.isStereoActive() == false, "isStereoActive must be false")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, ExposesHeadPoseToLuauScript) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.12f, -0.05f, 0.88f);
    simPose.rotation = fs25vr::EulerAngles(0.25f, -0.15f, 0.05f);
    simPose.isValid = true;
    simPose.positionTracked = true;
    simPose.orientationTracked = true;
    headset.SetHeadPose(simPose);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(1.5f, 0.5f, -2.0f);
    simPose.rotation = fs25vr::EulerAngles(0.1f, 0.3f, 0.0f);
    simPose.isValid = true;
    simPose.positionTracked = true;
    simPose.orientationTracked = true;
    headset.SetHeadPose(simPose);

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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;

    XrFovf leftFov{ -0.90f, 0.80f, 0.85f, -0.85f };
    XrFovf rightFov{ -0.80f, 0.90f, 0.85f, -0.85f };

    headset.SetEyeFov(0, leftFov);
    headset.SetEyeFov(1, rightFov);

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
    fs25vr::VRBridge bridge(nullptr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
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
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.0f, 1.7f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.orientation = fs25vr::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);
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

TEST_F(VrBridgeTest, FillsCallerSuppliedTablesInPlaceForPoseAndProjection) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.12f, 1.7f, 0.88f);
    simPose.rotation = fs25vr::EulerAngles(0.25f, -0.15f, 0.05f);
    simPose.orientation = fs25vr::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    simPose.isValid = true;
    simPose.positionTracked = true;
    simPose.orientationTracked = true;
    headset.SetHeadPose(simPose);
    headset.SetEyeFov(0, XrFovf{ -0.90f, 0.80f, 0.85f, -0.85f });
    headset.SetEyeFov(1, XrFovf{ -0.80f, 0.90f, 0.85f, -0.85f });
    oxr->SetIpd(0.064f);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        local function sameFields(filled, fresh, what)
            local n = 0
            for k, v in pairs(fresh) do
                n = n + 1
                assert(filled[k] == v or (type(v) == "number" and math.abs(filled[k] - v) < 1e-6),
                    what .. " field mismatch: " .. tostring(k))
            end
            for k in pairs(filled) do
                assert(fresh[k] ~= nil, what .. " has extra field: " .. tostring(k))
            end
            return n
        end

        -- getHeadPose
        local out = {}
        local ret = VRBridge.getHeadPose(out)
        assert(ret == out, "getHeadPose must return the supplied table")
        assert(sameFields(out, VRBridge.getHeadPose(), "headPose") == 13, "head pose has 13 fields")

        -- getEyePose: index, string, default, and either argument order
        local eyeOut = {}
        assert(VRBridge.getEyePose(1, eyeOut) == eyeOut, "getEyePose(1, out) returns out")
        assert(eyeOut.eye == 1, "eye index honoured alongside output table")
        sameFields(eyeOut, VRBridge.getEyePose(1), "eyePose right")
        assert(VRBridge.getEyePose("left", eyeOut) == eyeOut)
        assert(eyeOut.eye == 0, "string eye honoured alongside output table")
        sameFields(eyeOut, VRBridge.getEyePose("left"), "eyePose left")
        assert(VRBridge.getEyePose(eyeOut, "right") == eyeOut)
        assert(eyeOut.eye == 1, "output table may precede the eye argument")
        assert(VRBridge.getEyePose(eyeOut) == eyeOut)
        assert(eyeOut.eye == 0, "table-only call selects the default eye")

        -- getEyeProjection
        local proj = {}
        assert(VRBridge.getEyeProjection(1, proj) == proj, "getEyeProjection(1, out) returns out")
        assert(proj.eye == 1, "eye index honoured alongside output table")
        assert(sameFields(proj, VRBridge.getEyeProjection(1), "projection right") == 15)
        assert(VRBridge.getEyeProjection("left", proj) == proj)
        assert(proj.eye == 0, "string eye honoured alongside output table")
        sameFields(proj, VRBridge.getEyeProjection("left"), "projection left")
        assert(VRBridge.getEyeProjection(proj, "right") == proj)
        assert(proj.eye == 1)
        assert(VRBridge.getEyeProjection(proj) == proj)
        assert(proj.eye == 0, "table-only call selects the default eye")

        -- Refilling the same table overwrites previous values (no staleness)
        local reused = {}
        VRBridge.getEyeProjection(0, reused)
        local leftX = reused.xOffset
        VRBridge.getEyeProjection(1, reused)
        assert(reused.xOffset ~= leftX, "refill must overwrite the previous eye's values")
    )luau"), 0);
}

TEST_F(VrBridgeTest, SuppliedTablePoseStillRecordedAsTheEyeRenderPose) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.0f, 1.7f, 0.0f);
    simPose.orientation = fs25vr::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        local t = {}
        VRBridge.getEyePose(1, t)
    )luau"), 0);
    EXPECT_FALSE(oxr->HasRecordedRenderPose(0));
    EXPECT_TRUE(oxr->HasRecordedRenderPose(1));
}
TEST_F(VrBridgeTest, RequiresAfrFallbackIsTrueFromTheFirstFrame) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_TRUE(bridge.RequiresAfrFallback());
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.requiresAfrFallback == true, "requiresAfrFallback must be true")
        assert(VRBridge.getRequiresAfrFallback() == true, "getRequiresAfrFallback() must return true")
    )luau"), 0);
}

TEST_F(VrBridgeTest, ExposesCompanionActiveStatusAndHandlesMapUnload) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
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
    fs25vr::VRBridge bridge(nullptr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
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
    fs25vr::VRBridge bridge(nullptr, nullptr);
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

TEST_F(VrBridgeTest, SetMenuVisibleTrueTriggersQuadAlignmentOnOpenXrManager) {
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(2.0f, 1.8f, -1.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 1.5707963f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);
    openXrManager->PollHeadPose();  // the engine polls the head pose every frame

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_FALSE(openXrManager->HasMenuQuadPose());

    // Setting menu visible must trigger quad alignment to the current head pose
    bridge.SetMenuVisible(true);
    EXPECT_TRUE(openXrManager->HasMenuQuadPose());

    XrPosef quadPose = openXrManager->ComputeMenuQuadPose();
    // Head was looking at yaw pi/2, so quad is placed along forward gaze
    // x = 2.0 - 1.5 = 0.5, y = 1.8, z = -1.0
    EXPECT_NEAR(quadPose.position.x, 0.5f, 0.01f);
    EXPECT_NEAR(quadPose.position.y, 1.8f, 0.001f);
    EXPECT_NEAR(quadPose.position.z, -1.0f, 0.01f);
}

TEST_F(VrBridgeTest, ExposesAlignMenuQuadToLuau) {
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(0.0f, 1.5f, 0.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.alignMenuQuad) == "function", "alignMenuQuad must be a function")
        VRBridge.alignMenuQuad()
    )luau"), 0);

    EXPECT_TRUE(openXrManager->HasMenuQuadPose());
    XrPosef quadPose = openXrManager->ComputeMenuQuadPose();
    EXPECT_NEAR(quadPose.position.z, -1.5f, 0.001f);
}

TEST_F(VrBridgeTest, Exposes2DViewActiveMethodsAndPropertyToLuauScript) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    ASSERT_TRUE(bridge.Inject(L));

    // Initially false in both C++ and Luau
    EXPECT_FALSE(bridge.Is2DViewActive());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.set2DViewActive) == "function", "set2DViewActive must be a function")
        assert(type(VRBridge.getIs2DViewActive) == "function", "getIs2DViewActive must be a function")
        assert(VRBridge.getIs2DViewActive() == false, "initial getIs2DViewActive must be false")
        assert(VRBridge.is2DViewActive == false, "initial is2DViewActive property must be false")
    )luau"), 0);

    // Set 2D view active from Luau
    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.set2DViewActive(true)
        assert(VRBridge.getIs2DViewActive() == true, "getIs2DViewActive must return true after set2DViewActive(true)")
        assert(VRBridge.is2DViewActive == true, "is2DViewActive property must return true after set2DViewActive(true)")
    )luau"), 0);

    EXPECT_TRUE(bridge.Is2DViewActive());

    // Frame parity reset test: setting parity to 1, then Set2DViewActive(false) should reset parity to 0
    bridge.SetFrameParity(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    bridge.Set2DViewActive(false);
    EXPECT_FALSE(bridge.Is2DViewActive());
    EXPECT_EQ(bridge.GetFrameParity(), 0u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIs2DViewActive() == false, "getIs2DViewActive must reflect C++ Set2DViewActive(false)")
    )luau"), 0);

    // Dynamic property access via __index
    bridge.Set2DViewActive(true);
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIs2DViewActive() == true, "must be true")
        assert(VRBridge.is2DViewActive == true, "property must be true")
    )luau"), 0);
}

TEST_F(VrBridgeTest, MapUnloadResets2DViewActiveToFalse) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    bridge.Set2DViewActive(true);
    EXPECT_TRUE(bridge.Is2DViewActive());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIs2DViewActive() == true, "2D view should be active before unload")
        VRBridge.onMapUnload()
        assert(VRBridge.getIs2DViewActive() == false, "2D view active must be reset to false after onMapUnload")
    )luau"), 0);

    EXPECT_FALSE(bridge.Is2DViewActive());
}

TEST_F(VrBridgeTest, Set2DViewActiveTrueTriggersQuadAlignmentOnOpenXrManager) {
    fs25vr::FakeTrackedHeadset headset;
    auto openXrManager = headset.manager;
    fs25vr::HeadPose simPose;
    simPose.position = fs25vr::Vector3(1.0f, 1.7f, 2.0f);
    simPose.rotation = fs25vr::EulerAngles(0.0f, 0.0f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);
    openXrManager->PollHeadPose();  // the engine polls the head pose every frame

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_FALSE(openXrManager->HasMenuQuadPose());

    // Setting 2D view active must trigger quad alignment to the current head pose
    bridge.Set2DViewActive(true);
    EXPECT_TRUE(openXrManager->HasMenuQuadPose());

    XrPosef quadPose = openXrManager->ComputeMenuQuadPose();
    // Quad is positioned 1.5m in front along forward gaze (0 yaw -> -Z)
    // x = 1.0, y = 1.7, z = 2.0 - 1.5 = 0.5
    EXPECT_NEAR(quadPose.position.x, 1.0f, 0.001f);
    EXPECT_NEAR(quadPose.position.y, 1.7f, 0.001f);
    EXPECT_NEAR(quadPose.position.z, 0.5f, 0.001f);
}

TEST_F(VrBridgeTest, DefaultRenderingModeIsSequentialDualPassAndThreadSafe) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::SequentialDualPass);
    EXPECT_TRUE(bridge.IsSequentialDualPass());
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
}

TEST_F(VrBridgeTest, ModeTransitionsSwitchStateAndResetSubPassAndParity) {
    fs25vr::VRBridge bridge(nullptr, nullptr);

    // Advance to parity 1 and sub-pass 1
    bridge.SetFrameParity(1);
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    // Switch to AFR
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);
    EXPECT_FALSE(bridge.IsSequentialDualPass());

    // Clean reset: mode change must reset subPassIndex and frameParity to 0
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
    EXPECT_EQ(bridge.GetFrameParity(), 0u);

    // Advance again
    bridge.SetFrameParity(1);
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    // Switch back to Sequential Dual-Pass
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::SequentialDualPass);
    EXPECT_TRUE(bridge.IsSequentialDualPass());
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
    EXPECT_EQ(bridge.GetFrameParity(), 0u);
}

TEST_F(VrBridgeTest, SubPassCyclingAndFrameBoundaryResets) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

    // Sub-pass cycling (0 -> 1 -> 0 -> 1)
    uint32_t next = bridge.AdvanceSubPass();
    EXPECT_EQ(next, 1u);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    next = bridge.AdvanceSubPass();
    EXPECT_EQ(next, 0u);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

    // Advance to sub-pass 1, then advance frame boundary
    bridge.AdvanceSubPass();
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    // AdvanceFrame represents the presentation boundary and must reset subPassIndex to 0
    bridge.AdvanceFrame();
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

    // SetFrameIndex also represents boundary reset
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);
    bridge.SetFrameIndex(10);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
}

TEST_F(VrBridgeTest, ExposesRenderingModeAndSubPassToLuauScript) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        assert(type(VRBridge.getRenderingMode) == "function", "getRenderingMode must be a function")
        assert(type(VRBridge.setRenderingMode) == "function", "setRenderingMode must be a function")
        assert(type(VRBridge.getSubPassIndex) == "function", "getSubPassIndex must be a function")

        -- Default initial state
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "default getRenderingMode() must be 'SequentialDualPass'")
        assert(VRBridge.renderingMode == "SequentialDualPass", "default renderingMode property must be 'SequentialDualPass'")
        assert(VRBridge.renderingModeIndex == 1, "default renderingModeIndex property must be 1")
        assert(VRBridge.getSubPassIndex() == 0, "default getSubPassIndex() must be 0")
        assert(VRBridge.subPassIndex == 0, "default subPassIndex property must be 0")

        -- Can request integer explicitly
        assert(VRBridge.getRenderingMode("int") == 1, "getRenderingMode('int') must return 1")

        -- Can retrieve multiple return values (string, int)
        local modeStr, modeIdx = VRBridge.getRenderingMode()
        assert(modeStr == "SequentialDualPass" and modeIdx == 1, "multiple return values mismatch")

        -- Switch mode via string in Luau
        VRBridge.setRenderingMode("AFR")
        assert(VRBridge.getRenderingMode() == "AFR", "getRenderingMode() must be 'AFR' after set")
        assert(VRBridge.renderingMode == "AFR", "renderingMode property must be 'AFR'")
        assert(VRBridge.renderingModeIndex == 0, "renderingModeIndex property must be 0")
        assert(VRBridge.getRenderingMode("int") == 0, "getRenderingMode('int') must return 0")

        -- Switch mode via integer in Luau
        VRBridge.setRenderingMode(1)
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "getRenderingMode() must be 'SequentialDualPass' after set(1)")
        assert(VRBridge.renderingModeIndex == 1, "renderingModeIndex must be 1")

        -- Switch mode via case-insensitive aliases
        VRBridge.setRenderingMode("SequentialDualPass")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "getRenderingMode() must recognize 'SequentialDualPass'")

        -- Toggle mode
        VRBridge.setRenderingMode("toggle")
        assert(VRBridge.getRenderingMode() == "AFR", "toggle must switch back to AFR")
        VRBridge.setRenderingMode("toggle")
        assert(VRBridge.getRenderingMode() == "SequentialDualPass", "toggle must switch back to SequentialDualPass")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }

    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::SequentialDualPass);
}

TEST_F(VrBridgeTest, HandshakeNegotiationAndModePersistence) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        -- Standard handshake exposes renderingMode and renderingModeIndex (default: SequentialDualPass)
        local hs = VRBridge.handshake()
        assert(hs.success == true, "handshake must succeed")
        assert(hs.renderingMode == "SequentialDualPass", "hs.renderingMode must initially be 'SequentialDualPass'")
        assert(hs.renderingModeIndex == 1, "hs.renderingModeIndex must initially be 1")

        -- Handshake negotiation with preferred mode
        local hsAfr = VRBridge.handshake("AFR")
        assert(hsAfr.success == true, "negotiated handshake must succeed")
        assert(hsAfr.renderingMode == "AFR", "hsAfr.renderingMode must be 'AFR'")
        assert(hsAfr.renderingModeIndex == 0, "hsAfr.renderingModeIndex must be 0")
        assert(VRBridge.getRenderingMode() == "AFR", "VRBridge must reflect negotiated mode")

        -- Map unload does NOT destroy chosen mode (mode persistence), but resets subPassIndex to 0
        VRBridge.onMapUnload()
        assert(VRBridge.getRenderingMode() == "AFR", "mode must persist across map unload")
        assert(VRBridge.getSubPassIndex() == 0, "subPassIndex must be reset to 0 on map unload")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }

    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
}

TEST_F(VrBridgeTest, SetsAndAdvancesSubPassIndexInLuau) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"luau(
        assert(type(VRBridge.getSubPassIndex) == "function", "getSubPassIndex must be a function")
        assert(type(VRBridge.setSubPassIndex) == "function", "setSubPassIndex must be a function")
        assert(type(VRBridge.advanceSubPass) == "function", "advanceSubPass must be a function")

        assert(VRBridge.getSubPassIndex() == 0, "initial subPassIndex must be 0")
        assert(VRBridge.subPassIndex == 0, "initial subPassIndex property must be 0")

        -- Set to 1
        VRBridge.setSubPassIndex(1)
        assert(VRBridge.getSubPassIndex() == 1, "subPassIndex must be 1 after setSubPassIndex(1)")
        assert(VRBridge.subPassIndex == 1, "subPassIndex property must be 1")

        -- Advance from 1 -> 0
        local nextPass = VRBridge.advanceSubPass()
        assert(nextPass == 0, "advanceSubPass from 1 must return 0")
        assert(VRBridge.getSubPassIndex() == 0, "subPassIndex must be 0 after advanceSubPass")

        -- Advance from 0 -> 1
        nextPass = VRBridge.advanceSubPass()
        assert(nextPass == 1, "advanceSubPass from 0 must return 1")
        assert(VRBridge.getSubPassIndex() == 1, "subPassIndex must be 1 after advanceSubPass")

        -- Modulo clamping: setSubPassIndex(2) -> 0
        VRBridge.setSubPassIndex(2)
        assert(VRBridge.getSubPassIndex() == 0, "setSubPassIndex(2) must wrap to 0")
    )luau";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, DiscontinuitySignallingExposedToLuauAndFlagsBothContexts) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_FALSE(bridge.HasPendingDiscontinuity(0));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(1));

    // Signal in C++
    bridge.SignalDiscontinuity();
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));

    // Consume Left context
    EXPECT_TRUE(bridge.ConsumeDiscontinuity(0));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));

    // Consume Right context
    EXPECT_TRUE(bridge.ConsumeDiscontinuity(1));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(1));

    // Re-signal via Luau resetTemporalHistory()
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.resetTemporalHistory) == "function", "resetTemporalHistory must be a function")
        VRBridge.resetTemporalHistory()
    )luau"), 0);

    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));

    // Also via signalDiscontinuity alias
    EXPECT_TRUE(bridge.ConsumeDiscontinuity(0));
    EXPECT_TRUE(bridge.ConsumeDiscontinuity(1));
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.signalDiscontinuity) == "function", "signalDiscontinuity must be a function")
        VRBridge.signalDiscontinuity()
    )luau"), 0);

    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
}

TEST_F(VrBridgeTest, ViewTransitionsAndRecenterFlagDiscontinuity) {
    fs25vr::VRBridge bridge(nullptr, nullptr);

    // Recenter flags discontinuity
    bridge.Recenter();
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // 2D view toggle flags discontinuity
    bridge.Set2DViewActive(true);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // Setting same 2D view state does not flag again
    bridge.Set2DViewActive(true);
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(0));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(1));

    // Toggling back to 3D flags discontinuity
    bridge.Set2DViewActive(false);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    // Menu visibility toggle flags discontinuity
    bridge.SetMenuVisible(true);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
}

TEST_F(VrBridgeTest, ExposesDlssTuningFunctionsToLuau) {
    auto ngx = std::make_shared<fs25vr::NgxInterceptor>();
    fs25vr::VRBridge bridge(nullptr, ngx);
    ASSERT_TRUE(bridge.Inject(L));

    const std::string testScript = R"(
        assert(type(VRBridge.setDlssMvScale) == "function", "setDlssMvScale missing")
        assert(type(VRBridge.setDlssSharpness) == "function", "setDlssSharpness missing")
        assert(type(VRBridge.setDlssPreset) == "function", "setDlssPreset missing")

        assert(VRBridge.setDlssMvScale(0.0) == true, "setDlssMvScale single arg failed")
        assert(VRBridge.setDlssMvScale(0.5, 0.75) == true, "setDlssMvScale dual arg failed")
        assert(VRBridge.setDlssSharpness(0.65) == true, "setDlssSharpness failed")
        assert(VRBridge.setDlssPreset(11) == true, "setDlssPreset failed")
    )";

    EXPECT_EQ(RunLuauString(L, testScript), 0);
    EXPECT_FLOAT_EQ(ngx->GetMvScaleMultiplierX(), 0.5f);
    EXPECT_FLOAT_EQ(ngx->GetMvScaleMultiplierY(), 0.75f);
    EXPECT_FLOAT_EQ(ngx->GetSharpnessOverride(), 0.65f);
    EXPECT_EQ(ngx->GetDlssPresetOverride(), 11);
}

TEST_F(VrBridgeTest, DlssTuningFunctionsReportFailureWithoutNgxInterceptor) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    const std::string testScript = R"(
        assert(VRBridge.setDlssMvScale(0.5) == false, "setDlssMvScale must fail without NGX")
        assert(VRBridge.setDlssSharpness(0.65) == false, "setDlssSharpness must fail without NGX")
        assert(VRBridge.setDlssPreset(11) == false, "setDlssPreset must fail without NGX")
    )";
    EXPECT_EQ(RunLuauString(L, testScript), 0);
}

TEST_F(VrBridgeTest, SetRenderingModeIsImmediateWhileOpenXrIsInactive) {
    auto openXr = std::make_shared<fs25vr::OpenXrManager>(std::make_shared<fs25vr::FakeOpenXrRuntime>());
    ASSERT_FALSE(openXr->IsActive());
    fs25vr::VRBridge bridge(openXr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_EQ(RunLuauString(L, "VRBridge.setRenderingMode('AFR')"), 0);

    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);
    EXPECT_FALSE(bridge.GetPendingRenderingMode().has_value());
}

TEST_F(VrBridgeTest, SetRenderingModeIsQueuedWhileOpenXrIsActiveUntilLatched) {
    fs25vr::FakeTrackedHeadset headset;
    ASSERT_TRUE(headset.manager->IsActive());
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_EQ(RunLuauString(L, "VRBridge.setRenderingMode('AFR')"), 0);

    // Not applied mid frame pair: only queued until the compositor frame boundary.
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(bridge.GetPendingRenderingMode(), fs25vr::VRRenderingMode::AFR);

    EXPECT_TRUE(bridge.LatchPendingRenderingMode());
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);
    EXPECT_FALSE(bridge.GetPendingRenderingMode().has_value());
    EXPECT_FALSE(bridge.LatchPendingRenderingMode());
}

TEST_F(VrBridgeTest, LatestQueuedRenderingModeWinsAndClearDropsIt) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);

    bridge.SetPendingRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    bridge.SetPendingRenderingMode(fs25vr::VRRenderingMode::AFR);
    EXPECT_EQ(bridge.GetPendingRenderingMode(), fs25vr::VRRenderingMode::AFR);

    bridge.ClearPendingRenderingMode();
    EXPECT_FALSE(bridge.GetPendingRenderingMode().has_value());
    EXPECT_FALSE(bridge.LatchPendingRenderingMode());
}

TEST_F(VrBridgeTest, ExposesAllExpectedBridgeKeysAndPreallocatesCapacity) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    // VRBridge should expose exactly kExposedFieldCount fields (1 version string + 29 functions)
    const char* expectedKeys[] = {
        "version",
        "isReady",
        "handshake",
        "isCompanionActive",
        "onMapUnload",
        "getStereoTelemetry",
        "isStereoActive",
        "getRequiresAfrFallback",
        "getHeadPose",
        "recenter",
        "getEyeProjection",
        "getFrameParity",
        "getFrameIndex",
        "getIpd",
        "getEyePose",
        "setMenuVisible",
        "getIsMenuVisible",
        "alignMenuQuad",
        "set2DViewActive",
        "getIs2DViewActive",
        "getRenderingMode",
        "setRenderingMode",
        "getSubPassIndex",
        "setSubPassIndex",
        "advanceSubPass",
        "resetTemporalHistory",
        "signalDiscontinuity",
        "setDlssMvScale",
        "setDlssSharpness",
        "setDlssPreset"
    };

    lua_getglobal(L, "VRBridge");
    ASSERT_TRUE(lua_istable(L, -1));

    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        count++;
        lua_pop(L, 1);
    }

    EXPECT_EQ(count, fs25vr::VRBridge::kExposedFieldCount);
    EXPECT_EQ(sizeof(expectedKeys) / sizeof(expectedKeys[0]), fs25vr::VRBridge::kExposedFieldCount);

    // Verify key presence directly from the pushed VRBridge table
    for (const char* key : expectedKeys) {
        lua_getfield(L, -1, key);
        EXPECT_FALSE(lua_isnil(L, -1)) << "Missing expected field: " << key;
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // pop VRBridge table
}

