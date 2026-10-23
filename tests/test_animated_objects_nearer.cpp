// The Pointer Ray on an Animated Object and on an object Object Grab can pick up (ADR-0034, spec #180, ticket #183), seen from outside: the
// real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (animated_object_test_support.h
// for the doors, object_grab_test_support.h for the empty hands and the loose objects). When the ray could select both, the one it meets first
// is the target: where the ray ends, whether a trigger pull toggles the door, whether the trigger or the grip is taken off the Virtual
// Gamepad, and which pulses and grabs happen. A Click Point within the cone still comes before both. Prior art: test_animated_objects_pointer.cpp
// (one door), test_object_grab_feedback.cpp and test_object_grab_grip.cpp (one object).

// What the game does each frame before the Companion Mod's update, besides the camera update and the player's own: Interactive Control's update
// of its Click Points (when a test has them).
#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA "if StubIC and StubIC.tick then StubIC.tick() end"

#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

// A loose object the hands can pick up (0.05 t; the hands' limit is 0.1 t) straight ahead of the fingertip (at z = 0.3),
// centred at the given z: its near surface is 0.15 m nearer (z = 1.35 for 1.5, 1.05 m from the fingertip). The door's leaf (z = 2.0) is
// 1.65 m from the fingertip.
std::string ObjectAt(const char* z) {
    return std::string("_G.object = StubHandTool.looseObject(-0.4, -0.2, ") + z + ", 0.05, { name = 'object' })";
}

}  // namespace

class VirtualGamepadAnimatedObjectsNearerTest : public VirtualGamepadAnimatedObjectsTest {
protected:
    // A door straight ahead in whose zone the player stands, and a loose object straight ahead at `z`.
    void StartWithDoorAndObject(const char* z) {
        ASSERT_NO_FATAL_FAILURE(StartWithDoor());
        ASSERT_EQ(RunLuauChunk(L, ObjectAt(z).c_str(), "=object"), 0);
        EnterZone();
    }
};

// ---- A loose object in front of a door ------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, WithALooseObjectInFrontOfADoorTheRayEndsOnTheObjectAndAPullTogglesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("1.5"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.35") << "the ray ends on the object, not on the door behind it";

    PullTrigger(kRight);
    FRAMES(2);
    EXPECT_EQ(Toggles(), 0) << "a pull toggles nothing: the door is not what the player aims at";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is a binding's, as it is when the ray is on any object";
    ReleaseTrigger(kRight);
}

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, TheGripGrabsTheObjectInFrontOfTheDoorAsObjectGrabAlwaysDid) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("1.5"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 1) << "one light pulse as the ray lands on the object (and none for the door behind it)";

    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 1);
    EXPECT_EQ(Toggles(), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, TheObjectIsTheTargetFromTheMomentItIsNearerAndTheDoorIsAgainOnceItIsGone) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("1.5"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.35");

    ASSERT_EQ(RunLuauChunk(L, "object.mass = 5", "=theObjectGetsTooHeavy"), 0) << "the hands can no longer pick it up";
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "without it the door is the ray's";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
}

// ---- A door in front of a loose object ------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, WithADoorInFrontOfALooseObjectTheRayEndsOnTheDoorAndAPullTogglesIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("2.5"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "the ray ends on the door, not on the object behind it";

    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that toggles the door is not also a binding";
    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 2) << "a second pull toggles it back";
}

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, TheObjectBehindTheDoorIsNeitherLandedOnNorGrabbed) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("2.5"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0) << "the ray is on the door: a squeeze does not reach through it for the object behind";
    Squeeze(kRight, 0.0f);
    FRAMES(1);

    // The door goes out of reach of the ray: the object is the target, and landing on it is felt as it always was.
    const int pulsesBefore = PulsesTo(kRight);
    LeaveZone();
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 2.35");
    EXPECT_EQ(PulsesTo(kRight), pulsesBefore + 1) << "one light pulse as the ray lands on the object";
}

// ---- Either one alone, and the Click Point ----------------------------------------------------------------------------------------

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, AClickPointWithinTheConeWinsOverBothTheObjectAndTheDoor) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("1.5"));
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok'); _G.point = StubIC.addPoint('lever', -0.4, -0.2, 1.0)", "=clickPoint"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.00") << "the ray ends on the Click Point, in front of the object and the door";
    const int pulses = PulsesTo(kRight);

    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lever");
    EXPECT_EQ(Toggles(), 0);
    ReleaseTrigger(kRight);

    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0) << "and a squeeze grabs no object while the Click Point has the ray";
    EXPECT_GE(PulsesTo(kRight), pulses);
}

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, WithOnlyAnObjectOnTheRayObjectGrabIsAsItWas) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, ObjectAt("1.5").c_str(), "=object"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.35");
    EXPECT_EQ(PulsesTo(kRight), 1);
    PullTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    ReleaseTrigger(kRight);
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, ADoorOutOfTheZoneDoesNotHideAnObjectInFrontOfOrBehindIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, ObjectAt("2.5").c_str(), "=object"), 0);  // behind the door, which the player is not in the zone of
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 2.35");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
}

// ---- Object Grab's report ---------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadAnimatedObjectsNearerTest, ObjectGrabsReportOfItsTargetCarriesTheHitsDistanceAlongTheRayFromTheFingertip) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("1.5"));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local has, x, y, z, distance = VRCompanion:peekObjectGrabRayTarget()
        _G.report = string.format("%s %.2f %.2f %.2f %.2f", tostring(has), x, y, z, distance)
    )luau", "=report"), 0);
    EXPECT_EQ(EvalLua(L, "report"), "true -0.40 -0.20 1.35 1.05") << "the fingertip is at z = 0.3, the object's near surface at z = 1.35";
}

