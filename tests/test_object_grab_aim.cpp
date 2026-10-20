// Object Grab, the Pointer Hand aims the empty hands (ADR-0033, spec #156), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h, StubHandTool, and the game's empty
// hands and loose objects in object_grab_test_support.h). The game's PlayerTargeter reads Player:getLookRay() once per update, as the
// game's does, and the stub keeps the ray it cast. A test asserts that ray (where it starts and which way it points) against the
// Pointer Ray the Companion Mod placed and the head's ray, and what the game's pick-up button then picks up. Prior art:
// test_hand_tools_look_ray.cpp (the look ray seen from the targeter).

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// A small loose object straight ahead of a hand pointing at tracking (0.4, -0.2, -0.3). In the world of the on-foot harness the player
// looks along +Z and tracking x and z are mirrored, so the hand's fingertip is at world (-0.4, -0.2, 0.3) and the object 1.2 m ahead of it,
// level with the hand and well to the side of where the head looks; light enough for the game's hands.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

// The ray the targeter casts when the Pointer Hand aims: from the Pointer Ray's origin moved back along the finger by the reach
// behind (so that an object a touching hand is in is above the hands target's 0.5 m minimum), along the finger.
constexpr float kReachBehind = 0.6f;

}  // namespace

#define OBJECT_GRAB_START() \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, StartOptions())); \
    SetGameTime(10000)

TEST_F(CompanionModTest, WithEmptyHandsAndThePointerHandInUseTheTargeterCastsThePointerRaysRay) {
    OBJECT_GRAB_START();
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "no hand is up: the head's ray";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    ASSERT_EQ(EvalLua(L, "tostring(pointerRay() ~= nil)"), "true") << "the Pointer Ray is placed: " << EvalLua(L, "VRCompanion:getPointerDiagnostics()");
    const std::string expected = "pulledBack(pointerRay(), " + std::to_string(kReachBehind) + ")";
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), " + expected + ")"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())") << ", the Pointer Ray is " << EvalLua(L, "rayText(pointerRay())");
    EXPECT_GT(Number(L, "apart(targeterRay(), headRay())"), 0.3) << "and not from the head: the hand is held away from it";

    // The hand moves; the ray follows (the targeter reads the ray before the mod's update of its frame).
    headset.SetHandPointing(kRight, HandAt(-0.3f, 0.1f, -0.4f, 0.5f, 0.8f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), " + expected + ")"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())") << ", the Pointer Ray is " << EvalLua(L, "rayText(pointerRay())");
}

TEST_F(CompanionModTest, TheOtherHandAimsWhenItIsTheOnlyOneInUse) {
    OBJECT_GRAB_START();
    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "tostring(pointerRay() ~= nil)"), "true");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), pulledBack(pointerRay(), " + std::to_string(kReachBehind) + "))"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())");
    EXPECT_GT(Number(L, "pointerRay()[1]"), 0.0) << "it is the left hand's ray";
}

TEST_F(CompanionModTest, WithASupportedHandToolInTheToolHandTheTargeterStillCastsTheToolsRay) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, StartOptions(), "ok", false));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    FRAMES(2);

    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    ASSERT_EQ(EvalLua(L, "tostring(pointerRay() ~= nil)"), "true") << "the other hand points";
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), { StubHandTool.nozzleRay(lance) })"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())");
}

TEST_F(CompanionModTest, WithAHandToolTheCompanionModDoesNotSupportInHandTheTargeterCastsTheHeadsRay) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, StartOptions(), "ok", false));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the hands are not empty: " << EvalLua(L, "rayText(targeterRay())");
}

TEST_F(CompanionModTest, WithObjectGrabOffTheTargeterCastsTheHeadsRayAndItIsSaved) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    StartOptions options;
    options.extraStubs = kXmlStubs;
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, options));
    SetGameTime(10000);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false") << "on: the hand aims";

    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab', 'off')"), "Object Grab set to: off (Saved)");
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "off: the head's ray, at once";

    EXPECT_EQ(EvalLua(L, "runCommand('vrObjectGrab', 'on')"), "Object Grab set to: on (Saved)");
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false") << "on again: the hand aims";
}

TEST_F(CompanionModTest, InAVehicleTheTargeterCastsTheHeadsRayEvenWithAHandPointing) {
    OBJECT_GRAB_START();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false") << "on foot: the hand aims";

    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle = {}", "=enterVehicle"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "in a vehicle: the head's ray";

    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle = nil", "=leaveVehicle"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false") << "on foot again: the hand aims";
}

TEST_F(CompanionModTest, ASetDownHandHandsTheRayBackToTheHeadAndAHandThatMovesAimsAgain) {
    OBJECT_GRAB_START();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false");

    AdvanceGameTime(3100);  // the hand lay still for longer than a Motion Controller counts as in use
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "set down: the head's ray";

    headset.SetHandPointing(kRight, HandAt(kHandX + 0.1f, kHandY, kHandZ));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false") << "picked up again: the hand aims";

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "tracking lost: the head's ray";
}

TEST_F(CompanionModTest, InTheThirdPersonViewTheTargeterCastsTheHeadsRay) {
    OBJECT_GRAB_START();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false");
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, true)", "=thirdPerson"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true");
}

TEST_F(CompanionModTest, OnlyTheLocalPlayersLookRayIsTheHands) {
    OBJECT_GRAB_START();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.other = setmetatable({ camera = foot.cam, isOwner = false, currentHandTool = hands }, { __index = Player })
        _G.otherRay = { Player.getLookRay(other) }
        _G.otherHead = { StubHandTool.originalGetLookRay(other) }
    )luau", "=anotherPlayer"), 0);
    EXPECT_EQ(EvalLua(L, "sameRay(otherRay, otherHead)"), "true") << "another player's look ray is the game's: " << EvalLua(L, "rayText(otherRay)");
}

// The game's pick-up button picks up what the targeter found along the hand's ray.
TEST_F(CompanionModTest, TheGamesPickUpButtonPicksUpWhatThePointerRayIsOn) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=pressPickUp"), 0);
    EXPECT_EQ(EvalLua(L, "#StubHandTool.pickUps"), "1");
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.pickUps[1] and StubHandTool.pickUps[1].node == bale.node)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == bale.node)"), "true");
}

TEST_F(CompanionModTest, TheGamesPickUpButtonPicksUpWhatTheHeadLooksAtWhenNoHandAims) {
    OBJECT_GRAB_START();
    FRAMES(2);  // the camera update places the head in the world
    ASSERT_EQ(RunLuauChunk(L, "local r = headRay(); _G.bale = StubHandTool.looseObject(r[1] + r[4] * 1.5, r[2] + r[5] * 1.5, r[3] + r[6] * 1.5, 0.05, { name = 'bale' })", "=bale"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=pressPickUp"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == bale.node)"), "true") << "as in the game";
}

// A hand that touches the object: its fingertip is at the surface (or has gone a few centimetres into it). The ray starts further back so
// that the hit is not under the 0.5 m minimum distance of the hands target type.
TEST_F(CompanionModTest, ATouchingHandPicksUpWhatItTouchesAndAnObjectOutOfReachIsNotPickedUp) {
    OBJECT_GRAB_START();
    // The fingertip at world (-0.4, -0.2, 0.3); the object's centre 0.2 m ahead of it: the fingertip is 5 cm inside its surface.
    ASSERT_EQ(RunLuauChunk(L, "_G.touched = StubHandTool.looseObject(-0.4, -0.2, 0.5, 0.05, { name = 'touched', radius = 0.15 })", "=touched"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=pressPickUp"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == touched.node)"), "true") << "touching is a hit";

}

TEST_F(CompanionModTest, AnObjectFurtherThanThePickupDistanceIsNotTargeted) {
    OBJECT_GRAB_START();
    // 6 m ahead of the fingertip: past the game's 3 m reach.
    ASSERT_EQ(RunLuauChunk(L, "_G.far = StubHandTool.looseObject(-0.4, -0.2, 6.3, 0.05, { name = 'far' })", "=far"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=pressPickUp"), 0);
    EXPECT_EQ(EvalLua(L, "#StubHandTool.pickUps"), "0");
}

// The game counts its empty hands as a hand tool; here they are none, so the Hand Tools' diagnostics do not call them an unsupported tool.
TEST_F(CompanionModTest, TheEmptyHandsAreNotAnUnsupportedHandTool) {
    OBJECT_GRAB_START();
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_EQ(diagnostics.find("unsupported"), std::string::npos) << diagnostics;
    EXPECT_NE(diagnostics.find("held Hand Tool: none"), std::string::npos) << diagnostics;
    EXPECT_EQ(CountLogLines("Hand Tool probe"), 0) << "and the probe has nothing to describe";
}