// The Virtual Gamepad while the Companion Mod uses a Motion Controller input in the world (ADR-0028, spec #98, ticket
// #101), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, in a cab
// whose vehicle steers the way the game's does (StubSteer) and has the stand-in for the Interactive Control mod (StubIC)
// with a Click Point straight ahead of each hand (companion_mod_stubs.h). The test moves the fake Motion Controllers
// and reads what the game would read from XInput (the answers the hooks route the game's calls to) next to what
// Interactive Control executed and whether a hand holds the Steering Wheel. Two uses are covered:
//   - the Pointer Hand's trigger while the Pointer Ray is on a Click Point: it clicks it and reads released on the pad
//   - the grip of a hand holding the Steering Wheel: it steers and reads released on the pad
// and in both the held-input rule: an input already held on the pad when the use would begin keeps its binding until it
// is let go, and the use does not act on it. Prior art: test_wheel_grab_pointer.cpp and test_virtual_gamepad.cpp.
//
// The cab is at the world origin facing +Z with the head at the tracking space origin (see wheel_grab_test_support.h).

// What the game does each frame before the Companion Mod's update, besides the camera update and the vehicle's own
// update: Interactive Control's update of its Click Points.
#define WHEEL_GRAB_PRE_FRAME_EXTRA_LUA "if StubIC.tick then StubIC.tick() end"

#include "wheel_grab_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace wheel_grab_test;

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

class VirtualGamepadCabUseTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The mod started in a steerable cab with Interactive Control's stand-in and a Click Point straight ahead of each
    // hand, and the Virtual Gamepad on.
    void StartWithPadOn() {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubSteeringVehicle, "=steeringVehicle"), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok'); StubSteer.install()", "=install"), 0);
        ASSERT_EQ(RunLuauChunk(L, SteerableCab().c_str(), "=scene"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubIC.equip(cab.vehicle, false)", "=equipIc"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            StubIC.addPoint('rightPoint', -0.3, 1.5, 2.3)
            StubIC.addPoint('leftPoint', 0.3, 1.5, 2.3)
            runCommand('vrVirtualGamepad', 'on')
        )luau", "=addPointsAndPad"), 0);
        SetGameTime(10000);
    }

    // The hand lifted and held so that its index fingertip is 0.3 m to its side of the head and 0.3 m ahead, pointing
    // straight ahead at its own Click Point (the right hand at 'rightPoint').
    void PointAtOwnClickPoint(uint32_t hand) {
        const float side = hand == kRight ? 0.3f : -0.3f;
        headset.SetHandPointing(hand, HandAt(side - 0.1f, 0.0f, -0.3f));
        FRAMES(1);
        headset.SetHandPointing(hand, HandAt(side, 0.0f, -0.3f));
        FRAMES(3);
    }

    // The hand lifted and aimed at nothing (no Click Point lies that way), so it is the Pointer Hand without a Click Point.
    void PointAtNothing(uint32_t hand) {
        const float side = hand == kRight ? 0.3f : -0.3f;
        headset.SetHandPointing(hand, HandAt(side - 0.1f, 1.0f, -0.3f));
        FRAMES(1);
        headset.SetHandPointing(hand, HandAt(side, 1.0f, -0.3f));
        FRAMES(3);
    }

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    std::string Executed() { return EvalLua(L, "table.concat(StubIC.executed, ',')"); }
};

} // namespace

TEST_F(VirtualGamepadCabUseTest, ThePointerHandsTriggerClicksTheClickPointItIsOnAndReadsReleasedOnThePad) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    PointAtOwnClickPoint(kRight);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "rightPoint");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Executed(), "rightPoint") << "the pull clicks the Click Point";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and is not also a binding";

    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "for as long as the trigger is held";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadCabUseTest, TheOtherHandsTriggerAndATriggerWithNoClickPointSelectedStillReachThePad) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    PointAtOwnClickPoint(kRight);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "rightPoint");

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(Pad().bLeftTrigger, 128) << "the hand that is not the Pointer Hand keeps its trigger";
    EXPECT_EQ(Executed(), "") << "and clicks nothing";
    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(1);

    // The Pointer Hand aimed at nothing: its trigger is a binding's like any other.
    PointAtNothing(kRight);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "nil");
    ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "with no Click Point selected the trigger reaches the pad";
    EXPECT_EQ(Executed(), "");
}

TEST_F(VirtualGamepadCabUseTest, ATriggerHeldWhenTheRayLandsOnAClickPointKeepsItsBindingAndClicksNothing) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    PointAtNothing(kRight);
    headset.SetHandInputs(kRight, Trigger(1.0f));  // held for throttle
    FRAMES(1);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    PointAtOwnClickPoint(kRight);  // the ray lands on a Click Point with the trigger still down
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "rightPoint");
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(Executed(), "") << "and is not a click";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(Executed(), "");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Executed(), "rightPoint") << "pulled afresh it is a click";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and not a binding";
}

TEST_F(VirtualGamepadCabUseTest, ATriggerThatClickedStaysOffThePadUntilLetGoEvenIfTheRayLeavesTheClickPoint) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    PointAtOwnClickPoint(kRight);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    ASSERT_EQ(Executed(), "rightPoint");

    PointAtNothing(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that clicked does not turn into a binding as the ray moves off";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "pulled afresh away from any Click Point it is the pad's";
}

TEST_F(VirtualGamepadCabUseTest, TheGripOfAHandHoldingTheSteeringWheelReadsReleasedOnThePadAndTheOtherHandsGripStillReachesIt) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());

    GRAB(kRight, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");
    EXPECT_EQ(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0) << "the grip that holds the Steering Wheel is not also a binding";

    headset.SetHandInputs(kLeft, Grip(1.0f));
    FRAMES(1);
    EXPECT_NE(Pad().wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER, 0) << "the other hand's grip, away from the Steering Wheel, is the pad's";
    EXPECT_EQ(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0);

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false");
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    // Squeezed again at the rim it holds again (the hand is still there).
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true");
    EXPECT_EQ(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0);
}

TEST_F(VirtualGamepadCabUseTest, AGripHeldWhenTheHandReachesTheRimKeepsItsBindingAndDoesNotGrab) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());

    headset.SetHandInputs(kRight, Grip(1.0f));  // squeezed away from the Steering Wheel, for a binding
    FRAMES(1);
    ASSERT_NE(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0);

    BRING_HAND(kRight, RimPoint(L, kRimRadius, 0.0));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false") << "a grip squeezed before the rim does not grab";
    EXPECT_NE(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0) << "it keeps its binding";

    headset.SetHandInputs(kRight, Grip(0.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Grip(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "true") << "let go and squeezed afresh at the rim, it grabs";
    EXPECT_EQ(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0) << "and is the Steering Wheel's";
}

TEST_F(VirtualGamepadCabUseTest, TheStatusCommandListsTheClickAndTheGripItIsUsing) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    PointAtOwnClickPoint(kRight);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "rightPoint");
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("the Companion Mod is using: right trigger"), std::string::npos)
        << EvalLua(L, "runCommand('vrMotionControllerStatus')");

    GRAB(kLeft, RimPoint(L, kRimRadius, 0.0));
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(0)"), "true");
    const std::string status = EvalLua(L, "runCommand('vrMotionControllerStatus')");
    EXPECT_NE(status.find("left grip"), std::string::npos) << status;
}