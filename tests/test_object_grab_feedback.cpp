#include "object_grab_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kObjectAhead = "StubHandTool.looseObject(-0.4, -0.2, 1.5, %s, { name = 'object' })";

std::string ObjectAhead(const char* mass) {
    char buffer[200];
    snprintf(buffer, sizeof(buffer), kObjectAhead, mass);
    return std::string("_G.object = ") + buffer;
}

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

}

#define OBJECT_GRAB_START() \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, StartOptions())); \
    SetGameTime(10000)

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
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.5").c_str(), "=object"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "tostring(drawnRayEnd())"), "nil");
}

TEST_F(CompanionModTest, ThePointerRayShowsOnAHeavyObjectWithSuperStrengthAndItsLongerReach) {
    OBJECT_GRAB_START();
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
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
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

TEST_F(CompanionModTest, OneLightPulseOnThePointerHandAsTheRayLandsOnAGrabbableObjectAndNoneWhileItStaysThere) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    ASSERT_EQ(PulsesTo(headset, kRight), 0) << "nothing under the ray: nothing felt";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(headset, kRight), 1) << "one pulse as it lands";
    EXPECT_GT(StrongestTo(headset, kRight), 0.0f);
    EXPECT_LE(StrongestTo(headset, kRight), 0.3f) << "a light one";
    EXPECT_EQ(PulsesTo(headset, kLeft), 0) << "the other hand feels nothing";

    FRAMES(30);
    EXPECT_EQ(PulsesTo(headset, kRight), 1) << "and none while it stays";

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(headset, kRight), 1) << "none as it leaves";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(headset, kRight), 2) << "one more as it lands again";
}

TEST_F(CompanionModTest, NoPulseAsTheRayLandsOnAnObjectTheGameWillNotPickUp) {
    OBJECT_GRAB_START();
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.5").c_str(), "=heavy"), 0);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(headset, kRight), 0);
}

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

    AdvanceGameTime(3100);
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

TEST_F(VirtualGamepadObjectGrabTest, ObjectGrabStillAimsAndPulsesWhenInteractiveControlClickPointsAreNearbyButOutsideTheRay) {
    const std::string icStubs = StubChunk("interactive_control") + "\nStubIC.install('ok')";
    StartOptions options;
    options.extraStubs = icStubs.c_str();
    ASSERT_NO_FATAL_FAILURE(Start(options));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('sideSwitch', -2.0, -0.2, 1.5)", "=addSideIc"), 0);
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);

    for (int i = 0; i < 2; ++i) {
        ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
        FRAMES(1);
    }
    headset.SetHandPointing(kRight, HandAt(kHandX - 0.1f, kHandY, kHandZ));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
    FRAMES(1);
    headset.SetHandPointing(kRight, HandAt(kHandX, kHandY, kHandZ));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil");
    ASSERT_EQ(EvalLua(L, "tostring(drawnRayEnd() ~= nil)"), "true");
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', drawnRayEnd())"), "-0.40 -0.20 1.35");
    EXPECT_EQ(PulsesTo(kRight), 1);

    Squeeze(kRight, 1.0f);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
    FRAMES(1);
    EXPECT_EQ(PickUps(), 1);
}

TEST_F(VirtualGamepadObjectGrabTest, WhenAClickPointSuppressesObjectGrabMovingBackOntoTheSameObjectPulsesHapticsAgain) {
    const std::string icStubs = StubChunk("interactive_control") + "\nStubIC.install('ok')";
    StartOptions options;
    options.extraStubs = icStubs.c_str();
    ASSERT_NO_FATAL_FAILURE(Start(options));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('switch', -0.4, -0.2, 1.0, { inRange = false })", "=addIc"), 0);
    ASSERT_EQ(RunLuauChunk(L, ObjectAhead("0.05").c_str(), "=object"), 0);

    headset.SetHandPointing(kRight, HandAt(kHandX - 0.1f, kHandY, kHandZ));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
    FRAMES(1);
    headset.SetHandPointing(kRight, HandAt(kHandX, kHandY, kHandZ));
    ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', drawnRayEnd())"), "-0.40 -0.20 1.35");
    ASSERT_EQ(PulsesTo(kRight), 1);

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = true", "=icInRange"), 0);
    for (int i = 0; i < 2; ++i) {
        ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
        FRAMES(1);
    }
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "switch");
    ASSERT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', drawnRayEnd())"), "-0.40 -0.20 1.00");
    const int pulsesAfterIc = PulsesTo(kRight);

    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false", "=icOutOfRange"), 0);
    for (int i = 0; i < 2; ++i) {
        ASSERT_EQ(RunLuauChunk(L, "StubIC.tick()", "=icTick"), 0);
        FRAMES(1);
    }
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil");
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', drawnRayEnd())"), "-0.40 -0.20 1.35");
    EXPECT_EQ(PulsesTo(kRight), pulsesAfterIc + 1) << "landed-target state reset while suppressed, so re-landing pulses again";
}
