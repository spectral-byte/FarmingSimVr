// Object Grab, what the Pointer Hand's aim shows and feels (ADR-0033, spec #156), seen from outside: the real Companion Mod runs against
// the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and loose objects in
// object_grab_test_support.h). A test asserts what the player sees and feels: whether and where the Pointer Ray is drawn, which haptic
// pulses reached which fake Motion Controller, and what the hands tool drew at the centre of the view. Prior art:
// test_hand_tools_haptics.cpp (pulses on the fake controllers), test_hand_tools_draw.cpp (the centre overlays) and test_pointer_ray.cpp.

#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

// A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (see
// test_object_grab_aim.cpp). Objects are made straight ahead of it, 1.2 m from the fingertip: their near surface is at z = 1.35.
constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kObjectAhead = "StubHandTool.looseObject(-0.4, -0.2, 1.5, %s, { name = 'object' })";

std::string ObjectAhead(const char* mass) {
    char buffer[200];
    snprintf(buffer, sizeof(buffer), kObjectAhead, mass);
    return std::string("_G.object = ") + buffer;
}

// How many pulses reached a hand (the fake controllers keep them all), and the strongest of them.
int PulsesTo(const FakeTrackedHeadset& headset, int hand) {
    int count = 0;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == hand) ++count;
    }
    return count;
}

float StrongestTo(const FakeTrackedHeadset& headset, int hand) {
    float strongest = 0.0f;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == hand && pulse.amplitude > strongest) strongest = pulse.amplitude;
    }
    return strongest;
}

}  // namespace

#define OBJECT_GRAB_START() \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, StartOptions())); \
    SetGameTime(10000)

// ---- The Pointer Ray shows on an object the game can pick up ---------------------------------------------------------------

TEST_F(CompanionModTest, ThePointerRayShowsOnAnObjectTheGameCanPickUpWhereTheTargeterHitIt) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil") << "no hand is up: no Pointer Ray";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "tostring(drawnRayEnd() ~= nil)"), "true") << "the Pointer Ray is drawn";
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', drawnRayEnd())"), "-0.40 -0.20 1.35") << "to where it meets the object";
}

TEST_F(CompanionModTest, ThePointerRayDoesNotShowOnAnObjectThatIsTooHeavy) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.5").c_str(), "=object"), 0);  // the hands' limit is 0.1
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil");
}

TEST_F(CompanionModTest, ThePointerRayShowsOnAHeavyObjectWithSuperStrengthAndItsLongerReach) {
    OBJECT_GRAB_START();
    // 6 m from the fingertip: past the pickup distance, and 0.5 t: past the limit.
    ASSERT_EQ(RunLuauChunk(L, "_G.pallet = StubHandTool.looseObject(-0.4, -0.2, 6.3, 0.5, { name = 'pallet' })", "=pallet"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil") << "out of reach and too heavy";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.superStrength(hands, true)", "=superStrength"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd() ~= nil)"), "true") << "with super strength the game's limits are longer and higher";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.superStrength(hands, false)", "=superStrengthOff"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil") << "and back";
}

TEST_F(CompanionModTest, ThePointerRayDoesNotShowOnAnObjectThatRefusesThePickUpUnlessSuperStrengthSkipsTheQuestion) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);
    ASSERT_EQ(RunLuauChunk(L, "object.canBePickedUp = false", "=refuses"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil") << "the object's own answer is the game's";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.superStrength(hands, true)", "=superStrength"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd() ~= nil)"), "true") << "under super strength the game does not ask it";
}

TEST_F(CompanionModTest, ThePointerRayIsNotDrawnForAnObjectTheHeadLooksAtWhileTheHandPointsElsewhere) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // pointing well beside the object
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil");
}

TEST_F(CompanionModTest, ThePointerRayIsNotDrawnOnAnObjectWithObjectGrabOff) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "tostring(drawnRayEnd() ~= nil)"), "true");
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil");
}

// ---- One light pulse as the ray lands -----------------------------------------------------------------------------------

TEST_F(CompanionModTest, OneLightPulseOnThePointerHandAsTheRayLandsOnAGrabbableObjectAndNoneWhileItStaysThere) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // beside the object
    ASSERT_EQ(PulsesTo(headset, kRight), 0) << "nothing under the ray: nothing felt";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // lands on it
    EXPECT_EQ(PulsesTo(headset, kRight), 1) << "one pulse as it lands";
    EXPECT_GT(StrongestTo(headset, kRight), 0.0f);
    EXPECT_LE(StrongestTo(headset, kRight), 0.3f) << "a light one";
    EXPECT_EQ(PulsesTo(headset, kLeft), 0) << "the other hand feels nothing";

    FRAMES(30);
    EXPECT_EQ(PulsesTo(headset, kRight), 1) << "and none while it stays";

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // leaves
    EXPECT_EQ(PulsesTo(headset, kRight), 1) << "none as it leaves";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // lands again
    EXPECT_EQ(PulsesTo(headset, kRight), 2) << "one more as it lands again";
}

TEST_F(CompanionModTest, NoPulseAsTheRayLandsOnAnObjectTheGameWillNotPickUp) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.5").c_str(), "=heavy"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(headset, kRight), 0);
}

// ---- The hands tool draws no crosshair while the hand aims --------------------------------------------------------------

TEST_F(CompanionModTest, TheHandsToolDrawsNoCrosshairOrGrabIconWhileTheHandAimsAndDrawsThemAsInTheGameOtherwise) {
    OBJECT_GRAB_START();
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "crosshair") << "no hand aims: the game's crosshair";

    ASSERT_EQ(RunLuauChunk(L, "local r = headRay(); _G.bale = StubHandTool.looseObject(r[1] + r[4] * 1.5, r[2] + r[5] * 1.5, r[3] + r[6] * 1.5, 0.05)", "=bale"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "pickUpCrosshair") << "looking at something to pick up: the game's grab icon";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "") << "the hand aims: nothing at the centre of the view";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'off')", "=off"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "pickUpCrosshair") << "Object Grab off: the game's again";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrObjectGrab', 'on')", "=on"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "") << "on again: nothing";

    AdvanceGameTime(3100);  // the hand is set down
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "pickUpCrosshair") << "set down: the head aims, and the game draws its own";
}

TEST_F(CompanionModTest, AnotherPlayersHandsToolIsDrawnTheGamesWayWhileTheLocalHandAims) {
    OBJECT_GRAB_START();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.other = setmetatable({ camera = foot.cam, isOwner = true }, { __index = Player })
        other.targeter = PlayerTargeter.new(other)
        other.targeter.closestTargetsByKey = {}
        _G.othersHands = StubHandTool.hands(other)
        othersHands.isHeld, othersHands.carryingPlayer = true, other
        StubHandTool.draw()
    )luau", "=anotherPlayersHands"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(hands)"), "") << "the local player's: held back";
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(othersHands)"), "crosshair") << "the other player's: the game's";
}
