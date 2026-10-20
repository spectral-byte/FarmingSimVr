// Object Grab, the grip grabs (ADR-0033, ADR-0028, spec #156, ticket #159), seen from outside: the real Companion Mod runs against the real
// VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and loose objects in
// object_grab_test_support.h). A test moves the fake Motion Controllers and squeezes a grip, and asserts what the game sees: the calls of
// the hands tool's pickUpTarget with the target, what XInput answers for the Virtual Gamepad's LB / RB, and the haptic pulses on the
// fake controllers. Prior art: test_virtual_gamepad_cab_use.cpp (a grip the mod takes reads released on XInput; the held-input rule).

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (see
// test_object_grab_aim.cpp). An object straight ahead of it, 1.2 m from the fingertip, has its near surface at z = 1.35.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

}  // namespace

// ---- A squeeze grabs what the Pointer Ray is on ------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabTest, SqueezingTheGripWhileThePointerRayIsOnAnObjectCallsThePickUpWithThatTargetAndPulsesTheHandOnce) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(PickUps(), 0) << "pointing alone picks nothing up";
    const int pulsesBefore = PulsesTo(kRight);

    Squeeze(kRight, 1.0f);
    FRAMES(1);

    ASSERT_EQ(PickUps(), 1) << "the squeeze calls the game's pickUpTarget";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.pickUps[1].node == bale.node)"), "true") << "with the object the ray is on";
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', StubHandTool.pickUps[1].x, StubHandTool.pickUps[1].y, StubHandTool.pickUps[1].z)"),
              "-0.40 -0.20 1.35") << "as the targeter hit it";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == bale.node)"), "true");
    EXPECT_EQ(PulsesTo(kRight), pulsesBefore + 1) << "the hand gets one pulse on the grab";
    EXPECT_EQ(PulsesTo(kLeft), 0);

    FRAMES(10);
    EXPECT_EQ(PickUps(), 1) << "one grab, however long it is held";
}

TEST_F(VirtualGamepadObjectGrabTest, TheGripReadsReleasedOnThePadWhileTheRayIsOnAGrabbableObjectAndWhileTheHandCarries) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    Squeeze(kRight, 1.0f);
    FRAMES(1);
    ASSERT_EQ(PickUps(), 1);
    EXPECT_FALSE(PadGrip(kRight)) << "the grip that grabbed is not also a binding";

    FRAMES(5);
    EXPECT_FALSE(PadGrip(kRight)) << "nor while the hand carries";
    POINT_HAND(kRight, kHandX + 0.3f, kHandY, kHandZ);  // carried to one side
    EXPECT_FALSE(PadGrip(kRight));
}

TEST_F(VirtualGamepadObjectGrabTest, ASqueezeAtNothingGrabbableReachesThePadAsTheGripBindingAndGrabsNothing) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // well beside the bale

    Squeeze(kRight, 1.0f);
    FRAMES(1);
    EXPECT_TRUE(PadGrip(kRight)) << "nothing under the ray: RB reaches the game";
    EXPECT_EQ(PickUps(), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_FALSE(PadGrip(kRight));

    // The other hand, away from the object, keeps its grip too.
    Squeeze(kLeft, 1.0f);
    FRAMES(1);
    EXPECT_TRUE(PadGrip(kLeft));
    EXPECT_EQ(PickUps(), 0);
}

TEST_F(VirtualGamepadObjectGrabTest, ASqueezeAtAnObjectTheGameWillNotPickUpIsTheGripBindingAsWell) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, "_G.pallet = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.5, { name = 'pallet' })", "=pallet"), 0);  // over the limit
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    Squeeze(kRight, 1.0f);
    FRAMES(2);
    EXPECT_EQ(PickUps(), 0);
    EXPECT_TRUE(PadGrip(kRight)) << "the game said no: the grip is not taken";
}

// ---- The held-input rule ------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadObjectGrabTest, AGripAlreadyHeldWhenTheRayLandsOnAnObjectKeepsItsBindingAndGrabsNothingUntilLetGoAndSqueezedAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    Squeeze(kRight, 1.0f);  // held for its binding, away from the object
    FRAMES(1);
    ASSERT_TRUE(PadGrip(kRight));

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // the ray lands on the bale with the grip still down
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0) << "a grip held before the ray landed does not grab";
    EXPECT_TRUE(PadGrip(kRight)) << "it keeps its binding";

    Squeeze(kRight, 0.0f);
    FRAMES(2);
    EXPECT_FALSE(PadGrip(kRight));
    EXPECT_EQ(PickUps(), 0);
    Squeeze(kRight, 1.0f);
    FRAMES(1);
    EXPECT_EQ(PickUps(), 1) << "let go and squeezed afresh, it grabs";
    EXPECT_FALSE(PadGrip(kRight)) << "and is no binding";
}

TEST_F(VirtualGamepadObjectGrabTest, ASqueezeWithObjectGrabOffIsTheGripBindingAndTheGamesOwnPickUpIsLeftToTheGame) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
    FRAMES(2);

    Squeeze(kRight, 1.0f);
    FRAMES(2);
    EXPECT_EQ(PickUps(), 0);
    EXPECT_TRUE(PadGrip(kRight));
}
