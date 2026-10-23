// Animated Objects, the haptic pulses on the Pointer Hand (ADR-0034, spec #180, ticket #182), seen from outside: the real Companion Mod runs
// against the real VRBridge over FakeTrackedHeadset, with the game stood in by the Lua stubs of animated_object_test_support.h. The test
// moves the fake Motion Controllers, pulls the trigger and asserts which pulses reached which hand: the light one as the ray lands on a door
// it could work (the select pulse of a Click Point), the firmer one for the pull that toggles it (the click pulse). Prior art: test_haptics.cpp
// (the same pulses for Click Points) and test_object_grab_feedback.cpp (the landing pulse of Object Grab).

#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

// A second door beside the first one: the ray of a hand 0.8 m to the side lands on it.
constexpr const char* kDoorBeside = "_G.besideDoor = StubAnimatedObject.door({ name = 'besideDoor', x = -1.2, y = -0.2, z = 2.0 })";
// One Animated Object with two moving parts a little apart (a double gate): the ray of the hand at kHandX lands on the first, the ray
// of a hand 1.2 m to the side on the second.
constexpr const char* kGateOfTwoLeaves =
    "_G.door = StubAnimatedObject.door({ name = 'gate', parts = { { x = -0.4, y = -0.2, z = 2.0 }, { x = -1.6, y = -0.2, z = 2.0 } } })";

// The strongest pulse a hand got (0..1), or 0 when it got none.
float StrongestTo(const FakeTrackedHeadset& headset, uint32_t hand) {
    float strongest = 0.0f;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == static_cast<int>(hand) && pulse.amplitude > strongest) strongest = pulse.amplitude;
    }
    return strongest;
}

}  // namespace

// ---- The select pulse -----------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadAnimatedObjectsTest, OneLightPulseOnThePointerHandAsTheRayLandsOnADoorAndNoneWhileItStaysThere) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // beside the door
    ASSERT_EQ(PulsesTo(kRight), 0) << "nothing under the ray: nothing felt";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // lands on it
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    EXPECT_EQ(PulsesTo(kRight), 1) << "one pulse as it lands";
    EXPECT_GT(StrongestTo(headset, kRight), 0.0f);
    EXPECT_LE(StrongestTo(headset, kRight), 0.3f) << "a light one";
    EXPECT_EQ(PulsesTo(kLeft), 0) << "the other hand feels nothing";

    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 1) << "and none while it stays";

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // leaves
    EXPECT_EQ(PulsesTo(kRight), 1) << "none as it leaves";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // lands again
    EXPECT_EQ(PulsesTo(kRight), 2) << "one more as it lands again";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, MovingTheRayFromOneMovingPartToAnotherOfTheSameDoorGivesNoNewPulse) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, kGateOfTwoLeaves, "=gate"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // the first leaf
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    ASSERT_EQ(PulsesTo(kRight), 1);

    POINT_HAND(kRight, kHandX + 1.2f, kHandY, kHandZ);  // the second leaf of the same gate
    ASSERT_EQ(DrawnEnd(), "-1.60 -0.20 1.95");
    EXPECT_EQ(PulsesTo(kRight), 1) << "the same Animated Object: no new pulse";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 1) << "and back";

    POINT_HAND(kRight, kHandX + 0.6f, kHandY, kHandZ);  // between the leaves (the gap is at world x -1.0): on nothing
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

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // straight onto the other door
    ASSERT_EQ(DrawnEnd(), "-1.20 -0.20 1.95");
    EXPECT_EQ(PulsesTo(kRight), 2) << "a different Animated Object";
    EXPECT_EQ(PulsesTo(kLeft), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ADoorOfTheLeftHandPulsesTheLeftHandNeverTheRightOne) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, kDoorOnTheLeft, "=leftDoor"), 0);
    EnterZone("leftDoor");
    // The right Motion Controller is tracked but lies still, so it is not in use.
    headset.SetHandPointing(kRight, HandAt(kHandX, kHandY, kHandZ));

    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "0.40 -0.20 1.95");
    ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "0");
    PullTrigger(kLeft);
    ASSERT_EQ(Toggles("leftDoor"), 1);

    EXPECT_EQ(PulsesTo(kLeft), 2) << "one landing, one click";
    EXPECT_EQ(PulsesTo(kRight), 0);
}

// ---- The click pulse ------------------------------------------------------------------------------------------------------------

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
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);  // aimed over the door
    ASSERT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(PulsesTo(kRight), 0) << "a pull with the ray on no door is no click";
}

// ---- Nothing to feel ------------------------------------------------------------------------------------------------------------

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
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);  // aimed over the door
    PullTrigger(kRight);                        // held for a binding, say
    ASSERT_EQ(Pad().bRightTrigger, 255);

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // the ray lands with the trigger still down
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
