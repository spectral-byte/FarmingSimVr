#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

constexpr const char* kDoorBeside = "_G.besideDoor = StubAnimatedObject.door({ name = 'besideDoor', x = -1.2, y = -0.2, z = 2.0 })";
constexpr const char* kGateOfTwoLeaves =
    "_G.door = StubAnimatedObject.door({ name = 'gate', parts = { { x = -0.4, y = -0.2, z = 2.0 }, { x = -1.6, y = -0.2, z = 2.0 } } })";

float StrongestTo(const FakeTrackedHeadset& headset, uint32_t hand) {
    float strongest = 0.0f;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == static_cast<int>(hand) && pulse.amplitude > strongest) strongest = pulse.amplitude;
    }
    return strongest;
}

}

TEST_F(VirtualGamepadAnimatedObjectsTest, OneLightPulseOnThePointerHandAsTheRayLandsOnADoorAndNoneWhileItStaysThere) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 0) << "nothing under the ray: nothing felt";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    EXPECT_EQ(PulsesTo(kRight), 1) << "one pulse as it lands";
    EXPECT_GT(StrongestTo(headset, kRight), 0.0f);
    EXPECT_LE(StrongestTo(headset, kRight), 0.3f) << "a light one";
    EXPECT_EQ(PulsesTo(kLeft), 0) << "the other hand feels nothing";

    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 1) << "and none while it stays";

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 1) << "none as it leaves";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 2) << "one more as it lands again";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, MovingTheRayFromOneMovingPartToAnotherOfTheSameDoorGivesNoNewPulse) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, kGateOfTwoLeaves, "=gate"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    ASSERT_EQ(PulsesTo(kRight), 1);

    POINT_HAND(kRight, kHandX + 1.2f, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-1.60 -0.20 1.95");
    EXPECT_EQ(PulsesTo(kRight), 1) << "the same Animated Object: no new pulse";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 1) << "and back";

    POINT_HAND(kRight, kHandX + 0.6f, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "none");
    POINT_HAND(kRight, kHandX + 1.2f, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 2) << "the ray left the gate and landed on it again";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, LandingOnADifferentDoorPulsesAgainEvenWithoutLeavingAllDoors) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, kDoorBeside, "=besideDoor"), 0);
    EnterZone("door");
    EnterZone("besideDoor");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    ASSERT_EQ(PulsesTo(kRight), 1);

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-1.20 -0.20 1.95");
    EXPECT_EQ(PulsesTo(kRight), 2) << "a different Animated Object";
    EXPECT_EQ(PulsesTo(kLeft), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ADoorOfTheLeftHandPulsesTheLeftHandNeverTheRightOne) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, kDoorOnTheLeft, "=leftDoor"), 0);
    EnterZone("leftDoor");
    headset.SetHandPointing(kRight, HandAt(kHandX, kHandY, kHandZ));

    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "0.40 -0.20 1.95");
    ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "0");
    PullTrigger(kLeft);
    ASSERT_EQ(Toggles("leftDoor"), 1);

    EXPECT_EQ(PulsesTo(kLeft), 2) << "one landing, one click";
    EXPECT_EQ(PulsesTo(kRight), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ThePullThatTogglesADoorPulsesOnceFirmerThanTheLandingAndHoldingTheTriggerPulsesNoMore) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 1) << "the landing";
    const float landing = StrongestTo(headset, kRight);

    PullTrigger(kRight);
    ASSERT_EQ(Toggles(), 1);
    EXPECT_EQ(PulsesTo(kRight), 2) << "the click";
    EXPECT_GT(StrongestTo(headset, kRight), landing) << "a click is felt more than a landing";

    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 2) << "the trigger is held: nothing more";

    ReleaseTrigger(kRight);
    EXPECT_EQ(PulsesTo(kRight), 2) << "letting go is silent";
    PullTrigger(kRight);
    ASSERT_EQ(Toggles(), 2);
    EXPECT_EQ(PulsesTo(kRight), 3) << "the second pull is the second click";
    EXPECT_EQ(PulsesTo(kLeft), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, APullThatTogglesNothingPulsesNoClick) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    ASSERT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 0) << "a pull with the ray on no door is no click";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ADoorThatCannotBeTriggeredPulsesNothingOnLandingOrOnThePull) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, "_G.door = StubAnimatedObject.door({ x = -0.4, y = -0.2, z = 2.0, canBeTriggered = false })", "=lockedDoor"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 0);
    EXPECT_EQ(PulsesTo(kLeft), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ADoorThatBecomesLockedStopsPulsingAndPulsesAgainWhenItIsUnlocked) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 1);

    ASSERT_EQ(RunLuauChunk(L, "door.canBeTriggered = false", "=lock"), 0);
    FRAMES(2);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 1) << "locked: no click, and losing it is silent";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "door.canBeTriggered = true", "=unlock"), 0);
    FRAMES(2);
    EXPECT_EQ(PulsesTo(kRight), 2) << "the ray is on a door it can work again: it lands again";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ADoorOutsideTheZonePulsesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 0);
    ReleaseTrigger(kRight);

    EnterZone();
    FRAMES(2);
    EXPECT_EQ(PulsesTo(kRight), 1) << "in the zone the same ray lands on the door";
    LeaveZone();
    FRAMES(2);
    PullTrigger(kRight);
    EXPECT_EQ(PulsesTo(kRight), 1) << "out of it again: nothing more";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ATriggerHeldOverFromAPadBindingPulsesNoClickAndAFreshPullDoes) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    FRAMES(3);
    ASSERT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 1) << "the landing is felt, the held trigger toggles nothing and so is no click";

    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    ASSERT_EQ(Toggles(), 1);
    EXPECT_EQ(PulsesTo(kRight), 2) << "pulled afresh it is a click";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, NothingPulsesWhileMotionControllersAreOffAndOnAgainTheDoorPulsesAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 1);

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(2);
    PullTrigger(kRight);
    FRAMES(2);
    ReleaseTrigger(kRight);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 1) << "no landing and no click pulses while off";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=on"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 2) << "turned back on, the ray lands on the door again";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
    EXPECT_EQ(PulsesTo(kRight), 3);
}
