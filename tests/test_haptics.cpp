// Haptic pulses on the Pointer Hand (spec #71, ticket #76). Two layers, both seen from outside:
//  - OpenXrManagerTest: OpenXrManager::PulseHaptic against FakeOpenXrRuntime, which records every
//    pulse the runtime was asked to play (hand, duration in nanoseconds, amplitude).
//  - CompanionModTest: the real Companion Mod over FakeTrackedHeadset and the real VRBridge, with the
//    Interactive Control stand-in of companion_mod_stubs.h. The test moves the controllers, drives
//    frames, and asserts which pulses reached which hand.
// The bridge call itself (VRBridge.pulseHaptic) is covered in test_vr_bridge.cpp.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::FakeTrackedHeadsetOptions;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

constexpr const char* kConsoleStubs = R"luau(
    _G.registeredCommands = {}
    _G.addConsoleCommand = function(name, desc, method, target)
        _G.registeredCommands[name] = { desc = desc, method = method, target = target }
    end
    _G.removeConsoleCommand = function(name) _G.registeredCommands[name] = nil end
    _G.runCommand = function(name, ...)
        local command = _G.registeredCommands[name]
        assert(command ~= nil, name .. " is not registered")
        return command.target[command.method](command.target, ...)
    end
)luau";

// A cab at the world origin and an Interactive Control release the Companion Mod supports. The player
// sits at (0, 1.5, 0) looking along world +Z, so a hand held at (x, y, -z) from the head points along
// +Z from world (-x, 1.5 + y, +z). "center" is straight ahead of a hand held at (+-0.3, 0, -0.3) and
// "threeDegrees" lies 3 degrees off that ray's centre, still inside the default 5 degree cone.
void StartMod(lua_State* L, fs25vr::VRBridge& bridge) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok')", "=installIc"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubScene.cab({})", "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
    )luau", "=preFrame"), 0);
}

} // namespace

// ---- OpenXrManager::PulseHaptic ---------------------------------------------------------------

TEST(OpenXrManagerTest, PulseHapticPlaysOnTheNamedHandInNanosecondsAndNeverOnTheOther) {
    FakeTrackedHeadset headset;

    EXPECT_TRUE(headset.manager->PulseHaptic(kRight, 0.04, 0.5f));
    ASSERT_EQ(headset.HapticPulses().size(), 1u);
    EXPECT_EQ(headset.HapticPulses()[0].hand, 1);
    EXPECT_EQ(headset.HapticPulses()[0].duration, 40000000) << "OpenXR wants nanoseconds";
    EXPECT_FLOAT_EQ(headset.HapticPulses()[0].amplitude, 0.5f);
    EXPECT_EQ(headset.HapticPulses()[0].frequency, XR_FREQUENCY_UNSPECIFIED);

    EXPECT_TRUE(headset.manager->PulseHaptic(kLeft, 0.02, 1.0f));
    ASSERT_EQ(headset.HapticPulses().size(), 2u);
    EXPECT_EQ(headset.HapticPulses()[1].hand, 0);
    EXPECT_EQ(headset.HapticPulses()[1].duration, 20000000);
}

TEST(OpenXrManagerTest, PulseHapticClampsStrengthAndRejectsNothingToPlay) {
    FakeTrackedHeadset headset;

    EXPECT_TRUE(headset.manager->PulseHaptic(kRight, 0.02, 7.0f));
    EXPECT_TRUE(headset.manager->PulseHaptic(kRight, 0.02, -3.0f));
    ASSERT_EQ(headset.HapticPulses().size(), 2u);
    EXPECT_FLOAT_EQ(headset.HapticPulses()[0].amplitude, 1.0f);
    EXPECT_FLOAT_EQ(headset.HapticPulses()[1].amplitude, 0.0f);

    EXPECT_FALSE(headset.manager->PulseHaptic(kRight, 0.0f, 0.5f)) << "no duration";
    EXPECT_FALSE(headset.manager->PulseHaptic(kRight, -1.0f, 0.5f));
    EXPECT_FALSE(headset.manager->PulseHaptic(2, 0.02f, 0.5f)) << "there are two hands";
    EXPECT_EQ(headset.HapticPulses().size(), 2u);
}

TEST(OpenXrManagerTest, PulseHapticIsHarmlessWhenTheRuntimeRefusedTheActionSetOrTheCall) {
    FakeTrackedHeadset refused(FakeTrackedHeadsetOptions{ .refuseActionSet = true });
    EXPECT_FALSE(refused.manager->HasMotionControllerActions());
    EXPECT_FALSE(refused.manager->PulseHaptic(kRight, 0.02, 0.5f));
    EXPECT_TRUE(refused.HapticPulses().empty());

    FakeTrackedHeadset failing;
    failing.runtime->controllers.applyHapticResult = XR_ERROR_RUNTIME_FAILURE;
    EXPECT_FALSE(failing.manager->PulseHaptic(kRight, 0.02, 0.5f));
    EXPECT_TRUE(failing.HapticPulses().empty());
}

// ---- The Companion Mod: pulses on landing and click -------------------------------------------

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// The hand is picked up (it moves) and held so that its ray runs along +Z from the given spot.
#define PICK_UP_AND_AIM(hand, x, y, z) \
    do { \
        SetGameTime(10000); \
        headset.SetHandPose((hand), HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPose((hand), HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)

namespace {

void AddCenterAndThreeDegrees(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('center', -0.3, 1.5, 2.3)
        StubIC.addPoint('threeDegrees', -0.3 + 0.1048, 1.5, 2.3)
    )luau", "=addPoints"), 0);
}

size_t PulsesTo(const FakeTrackedHeadset& headset, int hand) {
    size_t count = 0;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == hand) ++count;
    }
    return count;
}

} // namespace

TEST_F(CompanionModTest, TheRayLandingOnAClickPointPulsesThePointerHandAndStayingOnItDoesNot) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('center', -0.3, 1.5, 2.3)", "=addPoint"), 0);

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "center");
    ASSERT_EQ(headset.HapticPulses().size(), 1u) << "landing on the Click Point";
    EXPECT_EQ(headset.HapticPulses()[0].hand, 1);
    EXPECT_GT(headset.HapticPulses()[0].duration, 0);
    EXPECT_GT(headset.HapticPulses()[0].amplitude, 0.0f);
    EXPECT_LE(headset.HapticPulses()[0].amplitude, 1.0f);

    FRAMES(10);
    EXPECT_EQ(headset.HapticPulses().size(), 1u) << "staying on the same Click Point is silent";
}

TEST_F(CompanionModTest, AimingAtADifferentClickPointPulsesAgainButLosingTheSelectionDoesNot) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_NO_FATAL_FAILURE(AddCenterAndThreeDegrees(L));

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "center");
    ASSERT_EQ(headset.HapticPulses().size(), 1u);

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false", "=hideCenter"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "threeDegrees");
    EXPECT_EQ(headset.HapticPulses().size(), 2u) << "the ray landed on a different Click Point";

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[2].inRange = false", "=hideSecond"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "nil");
    EXPECT_EQ(headset.HapticPulses().size(), 2u) << "leaving every Click Point is silent";

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = true", "=showCenter"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "center");
    EXPECT_EQ(headset.HapticPulses().size(), 3u) << "landing again after nothing was selected";
    EXPECT_EQ(PulsesTo(headset, 0), 0u);
}

TEST_F(CompanionModTest, EachClickPulsesThePointerHandAndHoldingTheTriggerOnAButtonDoesNot) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('center', -0.3, 1.5, 2.3)", "=addPoint"), 0);

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    ASSERT_EQ(headset.HapticPulses().size(), 1u) << "the landing";
    const auto landing = headset.HapticPulses()[0];

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    ASSERT_EQ(headset.HapticPulses().size(), 2u) << "the click";
    EXPECT_EQ(headset.HapticPulses()[1].hand, 1);
    EXPECT_GT(headset.HapticPulses()[1].amplitude, landing.amplitude) << "a click is felt more than a landing";

    FRAMES(5);
    EXPECT_EQ(headset.HapticPulses().size(), 2u) << "the trigger is held, nothing more is clicked";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "2");
    EXPECT_EQ(headset.HapticPulses().size(), 3u) << "the second click";
    EXPECT_EQ(PulsesTo(headset, 0), 0u);
}

TEST_F(CompanionModTest, PulsesGoToTheLeftHandWhenItIsThePointerHandAndNeverToTheOtherOne) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_NO_FATAL_FAILURE(AddCenterAndThreeDegrees(L));
    // The right controller is on and tracked but lies still, so it is not in use.
    headset.SetHandPose(kRight, HandAt(0.3f, 0.0f, -0.3f));

    PICK_UP_AND_AIM(kLeft, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "0");
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "#StubIC.executed"), "1");

    EXPECT_EQ(headset.HapticPulses().size(), 2u) << "one landing, one click";
    EXPECT_EQ(PulsesTo(headset, 0), 2u);
    EXPECT_EQ(PulsesTo(headset, 1), 0u);
}

TEST_F(CompanionModTest, NoPulsesWhileMotionControllersAreOff) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_NO_FATAL_FAILURE(AddCenterAndThreeDegrees(L));
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    ASSERT_EQ(headset.HapticPulses().size(), 1u);

    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'off')").find("off"), std::string::npos);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false", "=hideCenter"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = true", "=showCenter"), 0);
    FRAMES(5);
    EXPECT_EQ(headset.HapticPulses().size(), 1u) << "no landing and no click pulses while off";

    // Even if some other code reports the events, the setting wins.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:onClickPointEvent("selected", { name = "x" })
        VRCompanion:onClickPointEvent("clicked", { name = "x" })
    )luau", "=directEvents"), 0);
    EXPECT_EQ(headset.HapticPulses().size(), 1u);

    // Turned back on, the pulses come back.
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllers', 'on')").find("on"), std::string::npos);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(3);
    EXPECT_GE(headset.HapticPulses().size(), 2u);
}

TEST_F(CompanionModTest, PulsingIsHarmlessWithTheActionSetRefusedTheCallFailingOrNoPointerHand) {
    // Action set refused: no controller is ever tracked, so there is no Pointer Hand and nothing to pulse.
    FakeTrackedHeadset headset(FakeTrackedHeadsetOptions{ .refuseActionSet = true });
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('center', -0.3, 1.5, 2.3)", "=addPoint"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:onClickPointEvent("selected", { name = "x" })
        VRCompanion:onClickPointEvent("clicked", { name = "x" })
        VRCompanion:onClickPointEvent("selected", nil)
    )luau", "=refusedEvents"), 0);
    EXPECT_TRUE(headset.HapticPulses().empty());
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0");
}

TEST_F(CompanionModTest, AClickStillExecutesWhenTheRuntimeFailsThePulse) {
    FakeTrackedHeadset headset;
    headset.runtime->controllers.applyHapticResult = XR_ERROR_RUNTIME_FAILURE;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('center', -0.3, 1.5, 2.3)", "=addPoint"), 0);

    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);

    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    EXPECT_TRUE(headset.HapticPulses().empty());
    for (const std::string& line : s_capturedLogs) {
        EXPECT_EQ(line.find("Error"), std::string::npos) << line;
    }
}
