#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

}

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
    POINT_HAND(kRight, kHandX + 0.3f, kHandY, kHandZ);
    EXPECT_FALSE(PadGrip(kRight));
}

TEST_F(VirtualGamepadObjectGrabTest, ASqueezeAtNothingGrabbableReachesThePadAsTheGripBindingAndGrabsNothing) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);

    Squeeze(kRight, 1.0f);
    FRAMES(1);
    EXPECT_TRUE(PadGrip(kRight)) << "nothing under the ray: RB reaches the game";
    EXPECT_EQ(PickUps(), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(1);
    EXPECT_FALSE(PadGrip(kRight));

    Squeeze(kLeft, 1.0f);
    FRAMES(1);
    EXPECT_TRUE(PadGrip(kLeft));
    EXPECT_EQ(PickUps(), 0);
}

TEST_F(VirtualGamepadObjectGrabTest, ASqueezeAtAnObjectTheGameWillNotPickUpIsTheGripBindingAsWell) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, "_G.pallet = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.5, { name = 'pallet' })", "=pallet"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    Squeeze(kRight, 1.0f);
    FRAMES(2);
    EXPECT_EQ(PickUps(), 0);
    EXPECT_TRUE(PadGrip(kRight)) << "the game said no: the grip is not taken";
}

TEST_F(VirtualGamepadObjectGrabTest, AGripAlreadyHeldWhenTheRayLandsOnAnObjectKeepsItsBindingAndGrabsNothingUntilLetGoAndSqueezedAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    Squeeze(kRight, 1.0f);
    FRAMES(1);
    ASSERT_TRUE(PadGrip(kRight));

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
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
