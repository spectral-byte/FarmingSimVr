#include <gtest/gtest.h>
#include "core/injector_runtime.h"
#include "core/injector_version.h"
#include "luau/vr_bridge.h"
#include "fake_openxr_runtime.h"
#include "fidelityfx/fidelityfx_interceptor.h"
#include "ngx/ngx_interceptor.h"
#include "luacode.h"
#include <atomic>
#include <string>
#include <thread>

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

}

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

    EXPECT_EQ(lua_gettop(L), initialTop);

    bool secondInject = bridge.Inject(L);
    EXPECT_TRUE(secondInject);
    EXPECT_EQ(lua_gettop(L), initialTop);
}

TEST_F(VrBridgeTest, InjectingLeavesARunningCollectorRunning) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_NE(lua_gc(L, LUA_GCISRUNNING, 0), 0);

    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_NE(lua_gc(L, LUA_GCISRUNNING, 0), 0) << "the game's collector runs again after the injection";
}

TEST_F(VrBridgeTest, InjectingLeavesAStoppedCollectorStopped) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    lua_gc(L, LUA_GCSTOP, 0);
    ASSERT_EQ(lua_gc(L, LUA_GCISRUNNING, 0), 0);

    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_EQ(lua_gc(L, LUA_GCISRUNNING, 0), 0) << "a collector the game had stopped is not started by the injection";
}

TEST_F(VrBridgeTest, ExposesVersionAndIsReadyAndHandshakeToLuauScript) {
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    ASSERT_TRUE(bridge.Inject(L));

    std::string script = R"(
        assert(VRBridge ~= nil, "VRBridge global table must exist")
        assert(VRBridge.version == "7.8.9", "VRBridge.version must be the version it was given")
        assert(type(VRBridge.isReady) == "function", "VRBridge.isReady must be a function")
        assert(type(VRBridge.handshake) == "function", "VRBridge.handshake must be a function")

        local ready = VRBridge.isReady()
        assert(type(ready) == "boolean", "isReady() must return boolean")

        local hs = VRBridge.handshake()
        assert(type(hs) == "table", "handshake() must return a table")
        assert(hs.success == true, "hs.success must be true")
        assert(hs.version == "7.8.9", "hs.version must match")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, TheHandshakeAcceptsTheCompanionModsVersionAsAnExtraArgumentAndKeepsReportingItsOwn) {
    fs25vr::VRBridge bridge(nullptr, nullptr, "7.8.9");
    ASSERT_TRUE(bridge.Inject(L));

    const int status = RunLuauString(L, R"(
        -- No mode, the mod's version: what the Companion Mod passes
        local hs = VRBridge.handshake(nil, "1.2.0.0")
        assert(hs.success == true, "the handshake completes")
        assert(hs.version == "7.8.9", "it still reports the Injector's version, not the one passed")
        assert(VRBridge.version == "7.8.9")

        -- A mode as before, with a version after it
        local afr = VRBridge.handshake("AFR", "1.2.0.0")
        assert(afr.success == true and afr.renderingMode == "AFR", "the mode is still negotiated")

        -- A version that is not text is ignored
        assert(VRBridge.handshake(nil, 42).success == true)
    )");
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, ReportsTheInjectorsReleaseVersionWhenNoneIsSupplied) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    const std::string expected(fs25vr::kInjectorVersion);
    lua_pushstring(L, expected.c_str());
    lua_setglobal(L, "expectedVersion");

    std::string script = R"(
        assert(VRBridge.version == expectedVersion, "VRBridge.version: " .. tostring(VRBridge.version))
        assert(VRBridge.handshake().version == expectedVersion, "the handshake reports the same version")
    )";

    int status = RunLuauString(L, script);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
    EXPECT_EQ(bridge.GetVersion(), expected);
}

TEST_F(VrBridgeTest, TheRuntimeBuildsItsBridgeWithTheBuildsVersionOrOneATestSupplies) {
    {
        fs25vr::InjectorRuntime runtime;
        EXPECT_EQ(runtime.GetVrBridge().GetVersion(), std::string(fs25vr::kInjectorVersion));
    }

    fs25vr::InjectorRuntime::Dependencies deps;
    deps.injectorVersion = "7.8.9";
    fs25vr::InjectorRuntime runtime(std::move(deps));
    ASSERT_TRUE(runtime.GetVrBridge().Inject(L));

    const int status = RunLuauString(L, R"(assert(VRBridge.version == "7.8.9", tostring(VRBridge.version)))");
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
    simPose.orientation = fs25vr::QuaternionFromEuler(0.25f, -0.15f, 0.05f);
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
    simPose.orientation = fs25vr::QuaternionFromEuler(0.1f, 0.3f, 0.0f);
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
        assert(math.abs(left.fovY - 1.80) < 0.01, "left fovY mismatch (rendered FOV covering the outer side)")

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
        assert(math.abs(right.fovY - 1.80) < 0.01, "right fovY mismatch (rendered FOV covering the outer side)")

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

    bridge.AdvanceFrame();
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    EXPECT_EQ(bridge.GetFrameIndex(), 1u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.frameParity == 1, "VRBridge.frameParity must now be 1")
        assert(VRBridge.getFrameParity() == 1, "VRBridge.getFrameParity() must now be 1")
        assert(VRBridge.frameIndex == 1, "VRBridge.frameIndex must now be 1")
    )luau"), 0);

    bridge.AdvanceFrame();
    EXPECT_EQ(bridge.GetFrameParity(), 0u);
    EXPECT_EQ(bridge.GetFrameIndex(), 2u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.frameParity == 0, "VRBridge.frameParity must now be 0")
        assert(VRBridge.frameIndex == 2, "VRBridge.frameIndex must now be 2")
    )luau"), 0);

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

        -- A table lacking any field is never written into (the Injector must not insert keys into a
        -- game-created table); a complete fresh table comes back instead, which is then filled in place.
        local incomplete = {}
        local out = VRBridge.getHeadPose(incomplete)
        assert(out ~= incomplete and next(incomplete) == nil, "incomplete table must be left untouched")
        assert(VRBridge.getHeadPose(out) == out, "getHeadPose must return a complete supplied table")
        assert(sameFields(out, VRBridge.getHeadPose(), "headPose") == 13, "head pose has 13 fields")

        -- getEyePose: index, string, default, and either argument order
        local eyeOut = VRBridge.getEyePose(1, {})
        assert(VRBridge.getEyePose(1, eyeOut) == eyeOut, "getEyePose(1, out) returns out")
        assert(VRBridge.getEyePose(1, out) ~= out, "a head-pose table lacks the eye field, so it is not filled in place")
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
        local proj = VRBridge.getEyeProjection(1, {})
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
        local reused = VRBridge.getEyeProjection(0, {})
        VRBridge.getEyeProjection(0, reused)
        local leftX = reused.xOffset
        VRBridge.getEyeProjection(1, reused)
        assert(reused.xOffset ~= leftX, "refill must overwrite the previous eye's values")
    )luau"), 0);
}

TEST_F(VrBridgeTest, GetHandStateReportsAimPoseAndRawInputsPerHandIntoACallerSuppliedTable) {
    fs25vr::FakeTrackedHeadset headset;
    auto oxr = headset.manager;
    fs25vr::HeadPose aim;
    aim.position = fs25vr::Vector3(0.30f, 1.20f, -0.40f);
    aim.orientation = fs25vr::QuaternionFromEuler(0.10f, -0.20f, 0.0f);
    aim.isValid = true;
    aim.positionTracked = true;
    aim.orientationTracked = true;
    headset.SetHandPose(1, aim);
    fs25vr::FakeOpenXrControllers::Inputs right;
    right.trigger = 0.75f;
    right.grip = 0.25f;
    right.thumbX = -0.5f;
    right.thumbY = 0.9f;
    right.thumbClick = true;
    right.buttonLower = true;
    right.menu = true;
    headset.SetHandInputs(1, right);
    headset.NextFrame();

    fs25vr::VRBridge bridge(oxr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        local right = VRBridge.getHandState(1)
        assert(right.tracked == true)
        assert(math.abs(right.x - 0.30) < 1e-6 and math.abs(right.y - 1.20) < 1e-6 and math.abs(right.z + 0.40) < 1e-6)
        assert(math.abs(right.pitch - 0.10) < 1e-4 and math.abs(right.yaw + 0.20) < 1e-4)
        assert(right.isValid == true and right.isPositionTracked == true and right.isOrientationTracked == true)
        assert(math.abs(right.trigger - 0.75) < 1e-6 and math.abs(right.grip - 0.25) < 1e-6)
        assert(math.abs(right.thumbX + 0.5) < 1e-6 and math.abs(right.thumbY - 0.9) < 1e-6)
        assert(right.thumbClick == true and right.buttonLower == true and right.buttonUpper == false and right.menu == true)

        -- Either hand selector style reaches the same hand; the left hand is separate.
        assert(VRBridge.getHandState("right").trigger == right.trigger)
        assert(VRBridge.getHandState("R").trigger == right.trigger)
        local left = VRBridge.getHandState(0)
        assert(left.trigger == 0 and left.menu == false and VRBridge.getHandState("left").trigger == 0)
        assert(VRBridge.getHandState("Left").trigger == 0 and VRBridge.getHandState("l").trigger == 0)

        -- A caller-supplied table is filled in place (no allocation per frame) once it is complete.
        local incomplete = {}
        local out = VRBridge.getHandState(1, incomplete)
        assert(out ~= incomplete and next(incomplete) == nil, "an incomplete table must be left untouched")
        assert(VRBridge.getHandState(1, out) == out, "a complete table is filled in place")
        assert(VRBridge.getHandState(out, "right") == out, "the table may precede the hand")
        local n = 0
        for k in pairs(out) do n = n + 1 end
        assert(n == 33, "hand state has 33 fields, got " .. n)

        -- Refilling overwrites: the same table now holds the left hand.
        assert(VRBridge.getHandState(0, out) == out)
        assert(out.trigger == 0 and out.x == 0 and out.tracked == true)
    )luau"), 0);
}

TEST_F(VrBridgeTest, GetHandStateReportsTheGripPoseNextToTheAimPoseWithItsOwnTrackedFlag) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::HeadPose aim;
    aim.position = fs25vr::Vector3(0.30f, 1.20f, -0.40f);
    fs25vr::HeadPose grip;
    grip.position = fs25vr::Vector3(0.25f, 1.10f, -0.30f);
    grip.orientation = fs25vr::QuaternionFromEuler(0.10f, -0.20f, 0.05f);
    headset.SetHandPose(1, aim);
    headset.SetHandGripPose(1, grip);
    headset.SetHandPose(0, aim);
    headset.SetHandGripPose(0, grip, false);
    headset.NextFrame();

    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        local right = VRBridge.getHandState(1)
        assert(right.tracked == true and right.gripPoseTracked == true)
        assert(math.abs(right.gripPoseX - 0.25) < 1e-6 and math.abs(right.gripPoseY - 1.10) < 1e-6 and math.abs(right.gripPoseZ + 0.30) < 1e-6)
        assert(math.abs(right.gripPosePitch - 0.10) < 1e-4 and math.abs(right.gripPoseYaw + 0.20) < 1e-4 and math.abs(right.gripPoseRoll - 0.05) < 1e-4)
        local len = math.sqrt(right.gripPoseQx^2 + right.gripPoseQy^2 + right.gripPoseQz^2 + right.gripPoseQw^2)
        assert(math.abs(len - 1) < 1e-5, "the grip orientation is a unit quaternion")
        -- The aim pose keeps its own fields and values.
        assert(math.abs(right.x - 0.30) < 1e-6 and math.abs(right.y - 1.20) < 1e-6 and right.isValid == true)

        -- The grip pose has its own tracked flag: this hand's controller is on, its grip pose is not.
        local left = VRBridge.getHandState(0)
        assert(left.tracked == true and left.gripPoseTracked == false)
        assert(left.gripPoseX == 0 and left.gripPoseY == 0 and left.gripPoseZ == 0 and left.gripPoseQw == 1)

        -- It fills the same caller-supplied table in place.
        local out = VRBridge.getHandState(1, {})
        assert(VRBridge.getHandState(1, out) == out and out.gripPoseTracked == true)
        assert(VRBridge.getHandState(0, out) == out and out.gripPoseTracked == false and out.gripPoseX == 0)
    )luau"), 0);
}

TEST_F(VrBridgeTest, GetHandStateReportsAnUntrackedZeroedHandWhenThereIsNothingToTrack) {
    fs25vr::FakeTrackedHeadset refused(fs25vr::FakeTrackedHeadsetOptions{ true });
    fs25vr::FakeTrackedHeadset headset;
    headset.SetHandPose(0, fs25vr::HeadPose(), false);
    headset.NextFrame();

    fs25vr::VRBridge noRuntime(nullptr, nullptr);
    fs25vr::VRBridge refusedBridge(refused.manager, nullptr);
    fs25vr::VRBridge offBridge(headset.manager, nullptr);
    const std::string check = R"luau(
        for _, hand in ipairs({ 0, 1, "left", "right", 7, "nonsense" }) do
            local state = VRBridge.getHandState(hand)
            assert(state.tracked == false, "untracked: " .. tostring(hand))
            assert(state.isValid == false and state.trigger == 0 and state.grip == 0, "zeroed: " .. tostring(hand))
            assert(state.thumbX == 0 and state.thumbY == 0 and state.x == 0 and state.qw == 1, "zeroed: " .. tostring(hand))
            assert(state.thumbClick == false and state.buttonLower == false and state.buttonUpper == false and state.menu == false)
            assert(state.gripPoseTracked == false and state.gripPoseX == 0 and state.gripPoseY == 0 and state.gripPoseZ == 0, "grip untracked: " .. tostring(hand))
            assert(state.gripPosePitch == 0 and state.gripPoseYaw == 0 and state.gripPoseRoll == 0 and state.gripPoseQw == 1, "grip identity: " .. tostring(hand))
        end
        assert(VRBridge.getHandState() ~= nil, "a call without a hand still returns a table")
    )luau";

    for (fs25vr::VRBridge* bridge : { &noRuntime, &refusedBridge }) {
        lua_State* state = luaL_newstate();
        luaL_openlibs(state);
        ASSERT_TRUE(bridge->Inject(state));
        EXPECT_EQ(RunLuauString(state, check), 0);
        lua_close(state);
    }
    ASSERT_TRUE(offBridge.Inject(L));
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getHandState(0).tracked == false and VRBridge.getHandState(0).gripPoseTracked == false)
        assert(VRBridge.getHandState(1).tracked == true and VRBridge.getHandState(1).gripPoseTracked == true)
        assert(VRBridge.getHandState(2).tracked == false, "out of range")
    )luau"), 0);
}

TEST_F(VrBridgeTest, GetHandStateReadsLeaveTheLuaStackBalanced) {
    fs25vr::FakeTrackedHeadset headset;
    headset.NextFrame();
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    const int before = lua_gettop(L);
    ASSERT_EQ(RunLuauString(L, "local t = VRBridge.getHandState(0, {}); VRBridge.getHandState(0, t); VRBridge.getHandState(1)"), 0);
    EXPECT_EQ(lua_gettop(L), before);
}

TEST_F(VrBridgeTest, PulseHapticSendsMillisecondsAndStrengthToTheNamedHandOnly) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    const int before = lua_gettop(L);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.pulseHaptic(1, 40, 0.5) == true)
        assert(VRBridge.pulseHaptic("left", 20, 0.25) == true)
        assert(VRBridge.pulseHaptic("R", 10, 1) == true)
    )luau"), 0);
    EXPECT_EQ(lua_gettop(L), before);

    const auto& pulses = headset.HapticPulses();
    ASSERT_EQ(pulses.size(), 3u);
    EXPECT_EQ(pulses[0].hand, 1);
    EXPECT_EQ(pulses[0].duration, 40000000) << "OpenXR wants nanoseconds";
    EXPECT_FLOAT_EQ(pulses[0].amplitude, 0.5f);
    EXPECT_EQ(pulses[1].hand, 0);
    EXPECT_EQ(pulses[1].duration, 20000000);
    EXPECT_FLOAT_EQ(pulses[1].amplitude, 0.25f);
    EXPECT_EQ(pulses[2].hand, 1);
}

TEST_F(VrBridgeTest, PulseHapticIsHarmlessWithoutARuntimeOrWithTheActionSetRefused) {
    fs25vr::FakeTrackedHeadset refused(fs25vr::FakeTrackedHeadsetOptions{ .refuseActionSet = true });
    fs25vr::VRBridge noRuntime(nullptr, nullptr);
    fs25vr::VRBridge refusedBridge(refused.manager, nullptr);

    for (fs25vr::VRBridge* bridge : { &noRuntime, &refusedBridge }) {
        lua_State* state = luaL_newstate();
        luaL_openlibs(state);
        ASSERT_TRUE(bridge->Inject(state));
        EXPECT_EQ(RunLuauString(state, R"luau(
            assert(VRBridge.pulseHaptic(1, 40, 0.5) == false)
            assert(VRBridge.pulseHaptic(nil, nil, nil) == false, "missing arguments are not an error")
            assert(VRBridge.pulseHaptic(5, 40, 0.5) == false, "an unknown hand")
        )luau"), 0);
        lua_close(state);
    }
    EXPECT_TRUE(refused.HapticPulses().empty());
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

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.isCompanionActive) == "function", "isCompanionActive must be a function")
        assert(type(VRBridge.onMapUnload) == "function", "onMapUnload must be a function")
        assert(VRBridge.isCompanionActive() == false, "must be false before handshake")
    )luau"), 0);

    ASSERT_EQ(RunLuauString(L, R"luau(
        local res = VRBridge.handshake()
        assert(res.success == true, "handshake must succeed")
        assert(VRBridge.isCompanionActive() == true, "must be true after handshake")
    )luau"), 0);

    EXPECT_TRUE(bridge.IsCompanionActive());

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

    EXPECT_FALSE(bridge.IsMenuVisible());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.setMenuVisible) == "function", "setMenuVisible must be a function")
        assert(type(VRBridge.getIsMenuVisible) == "function", "getIsMenuVisible must be a function")
        assert(VRBridge.getIsMenuVisible() == false, "initial getIsMenuVisible must be false")
        assert(VRBridge.isMenuVisible == false, "initial isMenuVisible property must be false")
    )luau"), 0);

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.setMenuVisible(true)
        assert(VRBridge.getIsMenuVisible() == true, "getIsMenuVisible must return true after setMenuVisible(true)")
        assert(VRBridge.isMenuVisible == true, "isMenuVisible property must return true after setMenuVisible(true)")
    )luau"), 0);

    EXPECT_TRUE(bridge.IsMenuVisible());

    bridge.SetFrameParity(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    bridge.SetMenuVisible(false);
    EXPECT_FALSE(bridge.IsMenuVisible());
    EXPECT_EQ(bridge.GetFrameParity(), 0u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIsMenuVisible() == false, "getIsMenuVisible must reflect C++ SetMenuVisible(false)")
    )luau"), 0);

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
    simPose.orientation = fs25vr::QuaternionFromEuler(0.0f, 1.5707963f, 0.0f);
    simPose.isValid = true;
    headset.SetHeadPose(simPose);
    openXrManager->PollHeadPose();

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_FALSE(openXrManager->HasMenuQuadPose());

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

    EXPECT_FALSE(bridge.Is2DViewActive());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.set2DViewActive) == "function", "set2DViewActive must be a function")
        assert(type(VRBridge.getIs2DViewActive) == "function", "getIs2DViewActive must be a function")
        assert(VRBridge.getIs2DViewActive() == false, "initial getIs2DViewActive must be false")
        assert(VRBridge.is2DViewActive == false, "initial is2DViewActive property must be false")
    )luau"), 0);

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.set2DViewActive(true)
        assert(VRBridge.getIs2DViewActive() == true, "getIs2DViewActive must return true after set2DViewActive(true)")
        assert(VRBridge.is2DViewActive == true, "is2DViewActive property must return true after set2DViewActive(true)")
    )luau"), 0);

    EXPECT_TRUE(bridge.Is2DViewActive());

    bridge.SetFrameParity(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    bridge.Set2DViewActive(false);
    EXPECT_FALSE(bridge.Is2DViewActive());
    EXPECT_EQ(bridge.GetFrameParity(), 0u);

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIs2DViewActive() == false, "getIs2DViewActive must reflect C++ Set2DViewActive(false)")
    )luau"), 0);

    bridge.Set2DViewActive(true);
    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getIs2DViewActive() == true, "must be true")
        assert(VRBridge.is2DViewActive == true, "property must be true")
    )luau"), 0);
}

TEST_F(VrBridgeTest, DesktopMirrorFlagDefaultsToOnIsSetFromLuauAndResetsOnMapUnload) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    EXPECT_TRUE(bridge.IsDesktopMirrorEnabled());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getDesktopMirror() == true, "the desktop mirror is on by default")
        VRBridge.setDesktopMirror(false)
        assert(VRBridge.getDesktopMirror() == false, "setDesktopMirror(false) turns it off")
    )luau"), 0);
    EXPECT_FALSE(bridge.IsDesktopMirrorEnabled());

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.onMapUnload()
        assert(VRBridge.getDesktopMirror() == true, "a map unload puts the mirror back on")
        VRBridge.setDesktopMirror(false)
        VRBridge.setDesktopMirror(true)
        assert(VRBridge.getDesktopMirror() == true, "setDesktopMirror(true) turns it on")
    )luau"), 0);
    EXPECT_TRUE(bridge.IsDesktopMirrorEnabled());
}

TEST_F(VrBridgeTest, FovOverscanDefaultsToZeroIsClampedToZeroToTwentyAndResetsOnMapUnload) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getFovOverscan() == 0, "the overscan is 0 by default")
        VRBridge.setFovOverscan(8)
        assert(VRBridge.getFovOverscan() == 8, "setFovOverscan(8) is read back")
        VRBridge.setFovOverscan(7.5)
        assert(VRBridge.getFovOverscan() == 7.5, "decimals are kept")
        VRBridge.setFovOverscan(35)
        assert(VRBridge.getFovOverscan() == 20, "the bridge clamps above 20")
        VRBridge.setFovOverscan(-4)
        assert(VRBridge.getFovOverscan() == 0, "the bridge clamps below 0")
        VRBridge.setFovOverscan(12)
        VRBridge.onMapUnload()
        assert(VRBridge.getFovOverscan() == 0, "a map unload puts the overscan back to 0")
    )luau"), 0);
}

TEST_F(VrBridgeTest, FovOverscanWithoutAnOpenXrManagerIsAcceptedAndHasNoEffect) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.setFovOverscan(10)
        assert(type(VRBridge.getFovOverscan()) == "number")
        local projection = VRBridge.getEyeProjection(0)
        assert(projection == nil or projection.isValid ~= true, "Pass-Through Mode has no projection to widen")
        VRBridge.onMapUnload()
        assert(VRBridge.getFovOverscan() == 0)
    )luau"), 0);
}

TEST_F(VrBridgeTest, MenuDistanceDefaultsToOneAndAHalfIsClampedReachesTheManagerAndOutlivesAMapUnload) {
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getMenuDistance() == 1.5, "the menu distance is 1.5 m by default")
        VRBridge.setMenuDistance(3)
        assert(VRBridge.getMenuDistance() == 3, "setMenuDistance(3) is read back")
        VRBridge.setMenuDistance(2.25)
        assert(VRBridge.getMenuDistance() == 2.25, "decimals are kept")
        VRBridge.setMenuDistance(40)
        assert(VRBridge.getMenuDistance() == 5, "the bridge clamps above 5")
        VRBridge.setMenuDistance(0)
        assert(VRBridge.getMenuDistance() == 1, "the bridge clamps below 1")
        VRBridge.setMenuDistance("far")
        assert(VRBridge.getMenuDistance() == 1.5, "anything that is not a number is the default")
        VRBridge.setMenuDistance(2.5)
    )luau"), 0);
    EXPECT_FLOAT_EQ(headset.manager->GetMenuQuadDistance(), 2.5f) << "the distance reaches the OpenXR manager";

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.onMapUnload()
        assert(VRBridge.getMenuDistance() == 2.5, "a map unload leaves the distance for the main menu")
    )luau"), 0);
    EXPECT_FLOAT_EQ(headset.manager->GetMenuQuadDistance(), 2.5f);
}

TEST_F(VrBridgeTest, MenuDistanceWithoutAnOpenXrManagerIsAcceptedAndHasNoEffect) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.setMenuDistance(3)
        assert(VRBridge.getMenuDistance() == 3)
    )luau"), 0);
}

TEST_F(VrBridgeTest, AnEyeSnapshotRequestFromLuauIsConsumedOnceForEachEye) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    EXPECT_FALSE(bridge.ConsumeEyeSnapshotRequest(0)) << "nothing is requested at first";
    EXPECT_FALSE(bridge.ConsumeEyeSnapshotRequest(1));

    ASSERT_EQ(RunLuauString(L, "VRBridge.snapshotEyes()"), 0);
    EXPECT_TRUE(bridge.ConsumeEyeSnapshotRequest(1)) << "either eye may come first";
    EXPECT_FALSE(bridge.ConsumeEyeSnapshotRequest(1)) << "each eye once";
    EXPECT_TRUE(bridge.ConsumeEyeSnapshotRequest(0));
    EXPECT_FALSE(bridge.ConsumeEyeSnapshotRequest(0));
}

TEST_F(VrBridgeTest, MouseConfinementFlagDefaultsToOnIsSetFromLuauAndResetsOnMapUnload) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    EXPECT_TRUE(bridge.IsMouseConfinementEnabled());

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(VRBridge.getMouseConfinement() == true, "Mouse Confinement is on by default")
        VRBridge.setMouseConfinement(false)
        assert(VRBridge.getMouseConfinement() == false, "setMouseConfinement(false) turns it off")
    )luau"), 0);
    EXPECT_FALSE(bridge.IsMouseConfinementEnabled());
    EXPECT_TRUE(bridge.IsDesktopMirrorEnabled()) << "the two flags are independent";

    ASSERT_EQ(RunLuauString(L, R"luau(
        VRBridge.onMapUnload()
        assert(VRBridge.getMouseConfinement() == true, "a map unload puts Mouse Confinement back on")
        VRBridge.setMouseConfinement(false)
        VRBridge.setMouseConfinement(true)
        assert(VRBridge.getMouseConfinement() == true, "setMouseConfinement(true) turns it on")
    )luau"), 0);
    EXPECT_TRUE(bridge.IsMouseConfinementEnabled());
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
    simPose.isValid = true;
    headset.SetHeadPose(simPose);
    openXrManager->PollHeadPose();

    fs25vr::VRBridge bridge(openXrManager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    EXPECT_FALSE(openXrManager->HasMenuQuadPose());

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

    bridge.SetFrameParity(1);
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(bridge.GetFrameParity(), 1u);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::AFR);
    EXPECT_FALSE(bridge.IsSequentialDualPass());

    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
    EXPECT_EQ(bridge.GetFrameParity(), 0u);

    bridge.SetFrameParity(1);
    bridge.SetSubPassIndex(1);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    EXPECT_EQ(bridge.GetRenderingMode(), fs25vr::VRRenderingMode::SequentialDualPass);
    EXPECT_TRUE(bridge.IsSequentialDualPass());
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);
    EXPECT_EQ(bridge.GetFrameParity(), 0u);
}

TEST_F(VrBridgeTest, SubPassCyclingAndFrameBoundaryResets) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

    uint32_t next = bridge.AdvanceSubPass();
    EXPECT_EQ(next, 1u);
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    next = bridge.AdvanceSubPass();
    EXPECT_EQ(next, 0u);
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

    bridge.AdvanceSubPass();
    EXPECT_EQ(bridge.GetSubPassIndex(), 1u);

    bridge.AdvanceFrame();
    EXPECT_EQ(bridge.GetSubPassIndex(), 0u);

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

    bridge.SignalDiscontinuity();
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));

    EXPECT_TRUE(bridge.ConsumeDiscontinuity(0));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));

    EXPECT_TRUE(bridge.ConsumeDiscontinuity(1));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(1));

    ASSERT_EQ(RunLuauString(L, R"luau(
        assert(type(VRBridge.resetTemporalHistory) == "function", "resetTemporalHistory must be a function")
        VRBridge.resetTemporalHistory()
    )luau"), 0);

    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));

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

    bridge.Recenter();
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    bridge.Set2DViewActive(true);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

    bridge.Set2DViewActive(true);
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(0));
    EXPECT_FALSE(bridge.HasPendingDiscontinuity(1));

    bridge.Set2DViewActive(false);
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(0));
    EXPECT_TRUE(bridge.HasPendingDiscontinuity(1));
    bridge.ConsumeDiscontinuity(0);
    bridge.ConsumeDiscontinuity(1);

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
        assert(VRBridge.dlssDiag(0) == false, "dlssDiag must reject a zero count")
        assert(VRBridge.dlssDiag(32) == true, "dlssDiag failed")
        assert(VRBridge.setDlssSdpMvScale(0.0, 0.25) == true, "setDlssSdpMvScale failed")
        assert(type(VRBridge.setDlssMvFix) == "function", "setDlssMvFix missing")
        assert(VRBridge.setDlssMvFix(true) == true, "setDlssMvFix on failed")
        assert(type(VRBridge.setDlssDepthModel) == "function", "setDlssDepthModel missing")
        assert(VRBridge.setDlssDepthModel(false) == true, "setDlssDepthModel finite failed")
        assert(type(VRBridge.setDlssNearClip) == "function", "setDlssNearClip missing")
        assert(VRBridge.setDlssNearClip(0.1) == true, "setDlssNearClip failed")
    )";

    EXPECT_EQ(RunLuauString(L, testScript), 0);
    EXPECT_FLOAT_EQ(ngx->GetMvScaleMultiplierX(), 0.5f);
    EXPECT_FLOAT_EQ(ngx->GetMvScaleMultiplierY(), 0.75f);
    EXPECT_FLOAT_EQ(ngx->GetSharpnessOverride(), 0.65f);
    EXPECT_EQ(ngx->GetDlssPresetOverride(), 11);
    EXPECT_EQ(ngx->GetDiagEvaluationsRemaining(), 32u);
    EXPECT_FLOAT_EQ(ngx->GetSdpMvScaleMultiplierX(), 0.0f);
    EXPECT_FLOAT_EQ(ngx->GetSdpMvScaleMultiplierY(), 0.25f);
    EXPECT_TRUE(ngx->IsMvFixEnabled());
    EXPECT_FALSE(ngx->IsDepthModelInfinite());
    EXPECT_FLOAT_EQ(ngx->GetNearClipOverride(), 0.1f);

    EXPECT_EQ(RunLuauString(L, "assert(VRBridge.setDlssMvFix(false) == true, \"setDlssMvFix off failed\")"), 0);
    EXPECT_FALSE(ngx->IsMvFixEnabled());
    EXPECT_EQ(RunLuauString(L, "assert(VRBridge.setDlssDepthModel(true) == true, \"setDlssDepthModel infinite failed\")"), 0);
    EXPECT_TRUE(ngx->IsDepthModelInfinite());
}

TEST_F(VrBridgeTest, DlssTuningFunctionsReportFailureWithoutNgxInterceptor) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    const std::string testScript = R"(
        assert(VRBridge.setDlssMvScale(0.5) == false, "setDlssMvScale must fail without NGX")
        assert(VRBridge.setDlssSharpness(0.65) == false, "setDlssSharpness must fail without NGX")
        assert(VRBridge.setDlssPreset(11) == false, "setDlssPreset must fail without NGX")
        assert(VRBridge.dlssDiag(32) == false, "dlssDiag must fail without NGX")
        assert(VRBridge.setDlssMvFix(true) == false, "setDlssMvFix must fail without NGX")
        assert(VRBridge.setDlssDepthModel(true) == false, "setDlssDepthModel must fail without NGX")
        assert(VRBridge.setDlssNearClip(0.1) == false, "setDlssNearClip must fail without NGX")
    )";
    EXPECT_EQ(RunLuauString(L, testScript), 0);
}

TEST_F(VrBridgeTest, SetDlssMvFixSurfacesFfxStateWithoutNgxAndIsolatesNgxFailure) {
    auto ffx = std::make_shared<fs25vr::FidelityFxInterceptor>();
    fs25vr::VRBridge ffxOnlyBridge(nullptr, nullptr, std::string(fs25vr::kInjectorVersion), ffx);
    ASSERT_TRUE(ffxOnlyBridge.Inject(L));

    EXPECT_STREQ(ffxOnlyBridge.DescribeMvFixState(), "on");
    EXPECT_EQ(RunLuauString(L, R"(
        assert(VRBridge.setDlssMvFix(false) == true, "setDlssMvFix off failed with FFX only")
        assert(VRBridge.setDlssMvFix(true) == true, "setDlssMvFix on failed with FFX only")
        assert(VRBridge.dlssDiag(16) == true, "dlssDiag failed with FFX only")
    )"), 0);
    EXPECT_TRUE(ffx->IsMvFixEnabled());
    EXPECT_EQ(ffx->GetDiagDispatchesRemaining(), 16u);
}

TEST_F(VrBridgeTest, SetDlssMvFixSurfacesActiveUpscalerFailureIndependently) {
    auto ngx = std::make_shared<fs25vr::NgxInterceptor>();
    auto ffx = std::make_shared<fs25vr::FidelityFxInterceptor>();
    fs25vr::VRBridge bridge(nullptr, ngx, std::string(fs25vr::kInjectorVersion), ffx);
    ASSERT_TRUE(bridge.Inject(L));

    static volatile int s_dummyFfxCalls = 0;
    struct DummyFfx {
        __declspec(noinline) static fs25vr::ffxReturnCode_t Create(fs25vr::ffxContext* ctx,
                                                                  fs25vr::ffxCreateContextDescHeader*,
                                                                  const fs25vr::ffxAllocationCallbacks*) {
            static int s_id = 1;
            s_dummyFfxCalls = s_dummyFfxCalls + 1;
            *ctx = reinterpret_cast<fs25vr::ffxContext>(static_cast<uintptr_t>(++s_id));
            return fs25vr::FFX_API_RETURN_OK;
        }
        __declspec(noinline) static fs25vr::ffxReturnCode_t Destroy(fs25vr::ffxContext* ctx,
                                                                   const fs25vr::ffxAllocationCallbacks*) {
            s_dummyFfxCalls = s_dummyFfxCalls + 2;
            *ctx = nullptr;
            return fs25vr::FFX_API_RETURN_OK;
        }
    };
    ASSERT_TRUE(ffx->AttachDirect(&DummyFfx::Create, &DummyFfx::Destroy));

    ngx->SetMotionVectorPassFactory([](ID3D12GraphicsCommandList*) {
        return std::unique_ptr<fs25vr::IMotionVectorPass>();
    });
    static volatile int s_dummyNgxCalls = 0;
    struct DummyNgx {
        __declspec(noinline) static fs25vr::NVSDK_NGX_Result Create(ID3D12GraphicsCommandList*, fs25vr::NVSDK_NGX_Feature,
                                                                    fs25vr::NVSDK_NGX_Parameter*,
                                                                    fs25vr::NVSDK_NGX_Handle** out) {
            static unsigned int s_id = 1;
            s_dummyNgxCalls = s_dummyNgxCalls + 1;
            auto* h = new fs25vr::NVSDK_NGX_Handle();
            h->Id = s_id++;
            *out = h;
            return fs25vr::NVSDK_NGX_Result_Success;
        }
        __declspec(noinline) static fs25vr::NVSDK_NGX_Result Release(fs25vr::NVSDK_NGX_Handle* h) {
            s_dummyNgxCalls = s_dummyNgxCalls + 2;
            delete h;
            return fs25vr::NVSDK_NGX_Result_Success;
        }
        __declspec(noinline) static fs25vr::NVSDK_NGX_Result Eval(ID3D12GraphicsCommandList*,
                                                                  const fs25vr::NVSDK_NGX_Handle*,
                                                                  const fs25vr::NVSDK_NGX_Parameter*,
                                                                  fs25vr::PFN_NVSDK_NGX_ProgressCallback) {
            s_dummyNgxCalls = s_dummyNgxCalls + 4;
            return fs25vr::NVSDK_NGX_Result_Success;
        }
    };
    ASSERT_TRUE(ngx->AttachDirect(&DummyNgx::Create, &DummyNgx::Release, &DummyNgx::Eval));
    ngx->SetVRBridge(&bridge);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    auto makeCam = [](uint32_t eye, double x) {
        fs25vr::CameraRecord r;
        r.eye = eye;
        r.position[0] = x;
        r.fovY = 1.0f;
        r.nearClip = 0.15f;
        r.farClip = 2000.0f;
        return r;
    };
    ASSERT_TRUE(bridge.GetCameraRecords().Report(makeCam(0, 9.9)));
    bridge.GetCameraRecords().AdvanceFrame();
    ASSERT_TRUE(bridge.GetCameraRecords().Report(makeCam(1, 10.064)));
    bridge.GetCameraRecords().AdvanceFrame();
    ASSERT_TRUE(bridge.GetCameraRecords().Report(makeCam(0, 10.0)));
    bridge.SetSubPassIndex(0);
    fs25vr::NgxParameterImpl params;
    params.Set(fs25vr::NVSDK_NGX_Parameter_Width, 1920u);
    params.Set(fs25vr::NVSDK_NGX_Parameter_Height, 1080u);
    params.Set(fs25vr::NVSDK_NGX_Parameter_Depth, reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x1)));
    params.Set(fs25vr::NVSDK_NGX_Parameter_MotionVectors, reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x2)));
    auto pfnCreate = &DummyNgx::Create;
    auto pfnRelease = &DummyNgx::Release;
    auto pfnEval = &DummyNgx::Eval;
    fs25vr::NVSDK_NGX_Handle* ngxHandle = nullptr;
    ASSERT_EQ(pfnCreate(nullptr, fs25vr::NVSDK_NGX_Feature_SuperSampling, &params, &ngxHandle),
              fs25vr::NVSDK_NGX_Result_Success);
    pfnEval(reinterpret_cast<ID3D12GraphicsCommandList*>(static_cast<uintptr_t>(0x3)), ngxHandle, &params, nullptr);
    ASSERT_TRUE(ngx->IsMvFixDisabledForSession());
    pfnRelease(ngxHandle);
    ngx->Detach();

    EXPECT_FALSE(ngx->IsAttached());
    EXPECT_TRUE(ffx->IsAttached());
    EXPECT_STREQ(bridge.DescribeMvFixState(), "on");
    EXPECT_EQ(RunLuauString(L, R"(
        assert(VRBridge.setDlssMvFix(true) == true, "active FFX should remain controllable when unattached NGX failed")
    )"), 0);

    ffx->DetachAll();
}

TEST_F(VrBridgeTest, ReportCameraRecordStampsTheRecordForTheEyeThatRendered) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    for (int i = 0; i < 7; ++i) bridge.GetCameraRecords().AdvanceFrame();

    ASSERT_EQ(RunLuauString(L, R"(
        local ok = VRBridge.reportCameraRecord(1, 4000000.25, 12.5, -987654.75, 0, 0, 0, 1, 1.2, 0.15, 2000, 0.01, -0.02)
        assert(ok == true, "a valid record must be accepted")
        assert(VRBridge.reportCameraRecord(2, 0, 0, 0, 0, 0, 0, 1, 1, 0.1, 100, 0, 0) == false, "unknown eye must be rejected")
        assert(VRBridge.reportCameraRecord(0, 0, 0, 0, 0, 0, 0, 0, 1, 0.1, 100, 0, 0) == false, "zero quaternion must be rejected")
    )"), 0);

    auto resolved = bridge.GetCameraRecords().Resolve(1);
    ASSERT_TRUE(resolved.current.has_value());
    EXPECT_EQ(resolved.current->frame, 7u);
    EXPECT_DOUBLE_EQ(resolved.current->position[0], 4000000.25);
    EXPECT_DOUBLE_EQ(resolved.current->position[2], -987654.75);
    EXPECT_FLOAT_EQ(resolved.current->fovY, 1.2f);
    EXPECT_FLOAT_EQ(resolved.current->farClip, 2000.0f);
    EXPECT_FLOAT_EQ(resolved.current->projOffsetX, 0.01f);
    EXPECT_FLOAT_EQ(resolved.current->projOffsetY, -0.02f);
    EXPECT_FALSE(bridge.GetCameraRecords().Resolve(0).current.has_value());
}

TEST_F(VrBridgeTest, ReportCameraRecordRaisesLuaErrorForMissingArguments) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    EXPECT_NE(RunLuauString(L, "VRBridge.reportCameraRecord(0, 1, 2)"), 0);
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

    const char* expectedKeys[] = {
        "version",
        "isReady",
        "handshake",
        "isCompanionActive",
        "onMapUnload",
        "getRequiresAfrFallback",
        "getHeadPose",
        "getHandState",
        "pulseHaptic",
        "setVirtualGamepad",
        "getVirtualGamepad",
        "setVirtualGamepadUse",
        "recenter",
        "getEyeProjection",
        "getFrameParity",
        "getFrameIndex",
        "getIpd",
        "getEyePose",
        "setMenuVisible",
        "getIsMenuVisible",
        "alignMenuQuad",
        "getPanelHit",
        "getPanelHitAlongRay",
        "setMenuCursor",
        "set2DViewActive",
        "getIs2DViewActive",
        "setDesktopMirror",
        "getDesktopMirror",
        "setMouseConfinement",
        "getMouseConfinement",
        "setFovOverscan",
        "getFovOverscan",
        "setMenuDistance",
        "getMenuDistance",
        "snapshotEyes",
        "getRenderingMode",
        "setRenderingMode",
        "getSubPassIndex",
        "setSubPassIndex",
        "advanceSubPass",
        "getActiveEye",
        "resetTemporalHistory",
        "signalDiscontinuity",
        "setDlssMvScale",
        "setDlssSharpness",
        "setDlssPreset",
        "dlssDiag",
        "setDlssSdpMvScale",
        "reportCameraRecord",
        "setDlssMvFix",
        "setDlssDepthModel",
        "setDlssNearClip",
        "startPerfTrace",
        "stopPerfTrace",
        "isPerfTracing",
        "perfTraceBegin",
        "perfTraceEnd"
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

    for (const char* key : expectedKeys) {
        lua_getfield(L, -1, key);
        EXPECT_FALSE(lua_isnil(L, -1)) << "Missing expected field: " << key;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}


TEST_F(VrBridgeTest, SwitchingTheStereoPresentationModeDiscardsCameraRecords) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::SequentialDualPass);
    fs25vr::CameraRecord record;
    record.eye = 0;
    ASSERT_TRUE(bridge.GetCameraRecords().Report(record));
    ASSERT_TRUE(bridge.GetCameraRecords().Resolve(0).current.has_value());

    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);

    EXPECT_FALSE(bridge.GetCameraRecords().Resolve(0).current.has_value());
}

TEST_F(VrBridgeTest, HandshakeAndSetRenderingModeParseIntegersAndStringsConsistentlyAndRejectUnknownModes) {
    fs25vr::VRBridge bridge(nullptr, nullptr);
    ASSERT_TRUE(bridge.Inject(L));

    const int status = RunLuauString(L, R"luau(
        assert(VRBridge.getRenderingMode() == "SequentialDualPass")

        assert(VRBridge.handshake("bogus").renderingMode == "SequentialDualPass")
        assert(VRBridge.handshake(2).renderingMode == "SequentialDualPass")
        assert(VRBridge.handshake(-1).renderingMode == "SequentialDualPass")
        assert(VRBridge.handshake(1.5).renderingMode == "SequentialDualPass")

        assert(VRBridge.handshake(0).renderingMode == "AFR")
        assert(VRBridge.setRenderingMode("bogus") == false)
        assert(VRBridge.getRenderingMode() == "AFR")
        assert(VRBridge.setRenderingMode(2) == false)
        assert(VRBridge.getRenderingMode() == "AFR")
        assert(VRBridge.setRenderingMode(-1) == false)
        assert(VRBridge.getRenderingMode() == "AFR")
        assert(VRBridge.setRenderingMode(1.5) == false)
        assert(VRBridge.getRenderingMode() == "AFR")

        assert(VRBridge.setRenderingMode(1) == true)
        assert(VRBridge.getRenderingMode() == "SequentialDualPass")
        assert(VRBridge.setRenderingMode("bogus") == false)
        assert(VRBridge.getRenderingMode() == "SequentialDualPass")

        assert(VRBridge.setRenderingMode("0") == true)
        assert(VRBridge.getRenderingMode() == "AFR")
        assert(VRBridge.handshake("1").renderingMode == "SequentialDualPass")
        assert(VRBridge.handshake("0").renderingMode == "AFR")
        assert(VRBridge.handshake(1).renderingMode == "SequentialDualPass")
    )luau");
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Luau script error: " << (err ? err : "unknown error");
    }
}

TEST_F(VrBridgeTest, CrossThreadAtomicFlagsAndOverridesAreReadableConcurrently) {
    auto ngx = std::make_shared<fs25vr::NgxInterceptor>();
    fs25vr::FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, ngx);
    bridge.SetRenderingMode(fs25vr::VRRenderingMode::AFR);

    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load()) {
            (void)bridge.IsCompanionActive();
            (void)bridge.IsMenuVisible();
            (void)bridge.Is2DViewActive();
            (void)bridge.GetActiveEye();
            (void)ngx->GetDlssPresetOverride();
            (void)ngx->GetSharpnessOverride();
            (void)ngx->IsForceSharpnessOverride();
            (void)ngx->GetMvScaleMultiplierX();
            (void)ngx->GetMvScaleMultiplierY();
            (void)ngx->GetSdpMvScaleMultiplierX();
            (void)ngx->GetSdpMvScaleMultiplierY();
            (void)headset.manager->GetState();
            (void)headset.manager->HasSession();
            (void)headset.manager->HasMotionControllerActions();
        }
    });

    for (int i = 0; i < 200; ++i) {
        const bool flag = (i % 2) == 0;
        bridge.SetCompanionActive(flag);
        bridge.SetMenuVisible(flag);
        bridge.Set2DViewActive(!flag);
        bridge.SetFrameParity(static_cast<uint32_t>(i));
        ngx->SetDlssPresetOverride(10 + (i % 3));
        ngx->SetSharpnessOverride(flag ? 0.5f : 0.75f, flag);
        ngx->SetMvScaleMultiplier(flag ? 1.0f : 2.0f, flag ? 1.0f : 2.0f);
        ngx->SetSdpMvScaleMultiplier(flag ? 1.0f : 1.5f, flag ? 1.0f : 1.5f);
    }

    stop.store(true);
    reader.join();
}
