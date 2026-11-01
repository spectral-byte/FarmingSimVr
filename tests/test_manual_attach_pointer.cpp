// Manual Attach, who may work a hitch and when (ADR-0035, spec #227, ticket #228), seen from outside: the real Companion Mod runs against the real
// VRBridge over FakeTrackedHeadset, with the game and Manual Attach stood in by Lua stubs (manual_attach_test_support.h). Covered here: the Pointer
// Hand's trigger on the Virtual Gamepad while the ray is on a hitch, who has control of the pointer and what takes the player out of the world (the
// desk mouse, a menu, a Stereo Dialog, 2D Floating Window Mode, Motion Controllers off, a vehicle), which hand can work a hitch (never a Tool Hand
// holding a Hand Tool nor a hand carrying a Held Object), what wins when the ray is on more than one thing, what Manual Attach is left as, and the
// pulses. Prior art: test_animated_objects_pointer.cpp and test_animated_objects_haptics.cpp (the same rules for Animated Objects).

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

// The strongest pulse a hand got (0..1), or 0 when it got none.
float StrongestTo(const FakeTrackedHeadset& headset, uint32_t hand) {
    float strongest = 0.0f;
    for (const auto& pulse : headset.HapticPulses()) {
        if (pulse.hand == static_cast<int>(hand) && pulse.amplitude > strongest) strongest = pulse.amplitude;
    }
    return strongest;
}

}  // namespace

// ---- The Virtual Gamepad -------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, WhileTheRayIsOnAHitchThePointerHandsTriggerReadsReleasedOnThePadAndTheOtherHandsStillReachesIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(Pad().bLeftTrigger, 128) << "the hand that is not the Pointer Hand keeps its trigger";
    EXPECT_EQ(Performs(), 0) << "and works nothing";
    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(1);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that works the hitch is not also a binding";
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "for as long as the trigger is held";
    EXPECT_EQ(Performs("joint"), 1) << "and it works the hitch once";
    ReleaseTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadManualAttachTest, ATriggerHeldWhenTheRayLandsOnAHitchKeepsItsBindingAndWorksNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);  // aimed over the hitch
    PullTrigger(kRight);                        // held for throttle, say
    ASSERT_EQ(Pad().bRightTrigger, 255);

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // the ray lands on the hitch with the trigger still down
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(Performs(), 0) << "and works nothing";

    ReleaseTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1) << "pulled afresh it works the hitch";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and is not a binding";
}

TEST_F(VirtualGamepadManualAttachTest, ATriggerThatWorkedAHitchStaysOffThePadUntilLetGoEvenIfTheRayLeavesTheHitch) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 1);

    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that worked the hitch does not turn into a binding as the ray moves off";
    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "pulled afresh away from the hitch it is the pad's";
    EXPECT_EQ(Performs(), 1);
}

// ---- Who has control of the pointer ----------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, WhileTheDeskMouseHasControlTheRayIsNotDrawnOnAHitchAndNothingIsWorked) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:notePointerMouseMoved()", "=mouseMoved"), 0);
    FRAMES(1);
    EXPECT_EQ(DrawnEnd(), "none") << "the mouse has the pointer: no ray on the hitch";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "and the trigger is a binding's";

    // The hand moves: the ray has the pointer again, but the pull that began under the mouse works nothing.
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    FRAMES(2);
    EXPECT_EQ(Performs(), 0) << "a trigger pulled while the mouse had control works nothing once the hand moves";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it keeps its binding";

    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1) << "only a new pull does";
}

// ---- Out of the world -------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, InAMenuNothingIsSelectedAndNothingIsWorked) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "and the hitch works again once the menu closes";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
}

TEST_F(VirtualGamepadManualAttachTest, WhileAStereoDialogShowsTheRayIsOnNoHitchAndThePullIsForTheDialog) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        g_gui.visible = true
        g_gui.currentDialogName = "FishDialog"
        g_gui.dialogs = { { name = "FishDialog", target = {} } }
    )luau", "=openFishDialog"), 0);
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isStereoDialogShowing()"), "true");
    EXPECT_EQ(DrawnEnd(), "none");

    PullTrigger(kRight);
    FRAMES(1);
    EXPECT_EQ(Performs(), 0) << "a pull for the dialog works no hitch";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it reaches the dialog through the pad";

    ReleaseTrigger(kRight);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false; g_gui.currentDialogName = ''; g_gui.dialogs = {}", "=closeFishDialog"), 0);
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the ray works again once it closes";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
}

TEST_F(VirtualGamepadManualAttachTest, In2DFloatingWindowModeNothingIsSelectedAndNothingIsWorked) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(true)", "=2d"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getIs2DViewActive()"), "true");
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(false)", "=vr"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
}

TEST_F(VirtualGamepadManualAttachTest, WithMotionControllersOffNothingIsSelectedOrWorkedAndOnAgainTheHitchWorks) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    FRAMES(2);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=on"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
}

TEST_F(VirtualGamepadManualAttachTest, InTheCabNoHitchIsOfferedAndManualAttachsOwnCandidatesAreLeftAlone) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    Lua("g_currentMission.controlledVehicle = { getActiveCamera = function() return nil end }");
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "Manual Attach's key and auto attach work from the cab, the ray stays with Click Points";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    EXPECT_EQ(Pad().bRightTrigger, 255);
}

// ---- Which hand ----------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, TheToolHandHoldingAHandToolNeverWorksAHitchAndNoHitchIsOfferedWhileItIsHeld) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");

    headset.SetHandPointing(kRight, HandAt(kHandX, kHandY, kHandZ));
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "the Tool Hand is no Pointer Hand";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);

    // The other hand points at the hitch: Manual Attach lets nobody attach with a Hand Tool in hand, so none is offered.
    POINT_HAND(kLeft, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kLeft);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kLeft);

    Lua("StubHandTool.drop(lance)");
    POINT_HAND(kLeft, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "once the Hand Tool is put away the hitch is offered again";
    PullTrigger(kLeft);
    EXPECT_EQ(Performs("joint"), 1);
}

TEST_F(VirtualGamepadManualAttachTest, ACarriedHeldObjectMeansNoHitchIsOfferedToEitherHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("_G.bale = StubHandTool.looseObject(0.4, -0.2, 1.5, 0.05, { name = 'bale' })");
    POINT_HAND(kLeft, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);

    POINT_HAND(kRight, -kHandX, kHandY, kHandZ);  // the bale, 1.2 m ahead of the right fingertip
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    ASSERT_EQ(PickUps(), 1) << "the right hand carries the bale";

    POINT_HAND(kLeft, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "Manual Attach lets nobody attach while the hands carry something";
    PullTrigger(kLeft);
    EXPECT_EQ(Performs(), 0);
}

TEST_F(VirtualGamepadManualAttachTest, EitherHandWorksAHitchAsThePointerHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    POINT_HAND(kLeft, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kLeft);
    EXPECT_EQ(Attached("plow"), "true");
    EXPECT_EQ(Pad().bLeftTrigger, 0);
    ReleaseTrigger(kLeft);

    // With both hands in use the Pointer Hand changes to the other hand with a pull of its trigger, which is no pull of a hitch.
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    ReleaseTrigger(kRight);
    ASSERT_EQ(EvalLua(L, "tostring(VRCompanion:getPointerHand())"), "1");
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    EXPECT_EQ(Attached("plow"), "false");
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

// ---- What wins ---------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, AClickPointWithinThePointerConeWinsOverAHitch) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); _G.point = StubIC.addPoint('lever', -0.4, -0.2, 1.0)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.00") << "the ray ends on the Click Point, not on the hitch behind it";

    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lever");
    EXPECT_EQ(Performs(), 0) << "the pull clicks the Click Point and works no hitch";
    ReleaseTrigger(kRight);

    Lua("point.inRange = false");
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "without the Click Point the hitch is the ray's";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
}

TEST_F(VirtualGamepadManualAttachTest, ClickPointsOutsideTheConeDoNotHideAHitchFromTheRay) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('farOff', 3.0, 1.5, 1.0)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "tostring(StubIC.selected())"), "nil");
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "Click Points exist, none is selected: the ray is the hitch's";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0");
}

TEST_F(VirtualGamepadManualAttachTest, AHitchTheRayIsOnWinsOverAnAnimatedObjectInFrontOfIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("_G.door = StubAnimatedObject.door({ name = 'door', x = -0.4, y = -0.2, z = 1.2 }); StubAnimatedObject.enterZone(door)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the hitch, not the door the ray meets first";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    EXPECT_EQ(Number(L, "#StubAnimatedObject.toggles"), 0) << "the pull toggles no door";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    Lua("StubManualAttach.detect()");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.15") << "with no hitch in reach the door is the ray's";
}

TEST_F(VirtualGamepadManualAttachTest, AHitchTheRayIsOnWinsOverAnObjectObjectGrabCouldPickUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.3, 0.05, { name = 'bale' })");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the hitch, not the bale the ray meets first";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    ReleaseTrigger(kRight);

    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0) << "and the grip picks up nothing while the ray is on the hitch";
}

TEST_F(VirtualGamepadManualAttachTest, WithTheRayOnNoHitchManualAttachKeepsItsOwnNearestCandidate) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kFrontAndRear);
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);  // over both hitches
    PullTrigger(kRight);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(StubManualAttach.handler.attachable == frontLoader and StubManualAttach.handler.attacherVehicleJointDescIndex == 2)"),
              "true") << "Manual Attach's nearest hitch, untouched";
    EXPECT_EQ(Performs(), 0);

    // What the key does is what it did: Manual Attach's execute path on its own candidate.
    Lua("StubManualAttach.handler:executeAttachment(StubManualAttach.handler.vehicleJointAttachment)");
    EXPECT_EQ(Attached("frontLoader"), "true");
    EXPECT_EQ(Attached("rearPlow"), "false");
}

TEST_F(VirtualGamepadManualAttachTest, PointingAndPullingLeavesManualAttachItselfAsFoundBesidesTheCandidateItWorks) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    NoteManualAttachFunctions();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 2);
    EXPECT_TRUE(ManualAttachFunctionsAreAsFound()) << "nothing of Manual Attach is wrapped, replaced or added: its bindings and actions are as they were";
}

// ---- The pulses ---------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, OneLightPulseOnThePointerHandAsTheRayLandsOnAHitchAndAFirmerOneForThePullThatWorkedIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kFrontAndRear);
    POINT_HAND(kRight, kHandX + 1.6f, kHandY, kHandZ);  // beside the hitches
    ASSERT_EQ(PulsesTo(kRight), 0) << "nothing under the ray: nothing felt";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // lands on the rear hitch
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    EXPECT_EQ(PulsesTo(kRight), 1) << "one pulse as it lands";
    const float light = StrongestTo(headset, kRight);
    EXPECT_GT(light, 0.0f);
    EXPECT_LE(light, 0.3f) << "a light one";
    EXPECT_EQ(PulsesTo(kLeft), 0) << "the other hand feels nothing";
    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 1) << "and none while it stays";

    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 1);
    EXPECT_EQ(PulsesTo(kRight), 2) << "a pull that ran Manual Attach's path gives one more";
    EXPECT_GT(StrongestTo(headset, kRight), light) << "a firmer one";
    FRAMES(3);
    EXPECT_EQ(PulsesTo(kRight), 2) << "and attaching changes nothing about being on the hitch";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHandX + 1.6f, kHandY, kHandZ);  // leaves
    EXPECT_EQ(PulsesTo(kRight), 2) << "none as it leaves";
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(PulsesTo(kRight), 3) << "one more as it lands again";
}

TEST_F(VirtualGamepadManualAttachTest, LandingOnAnotherHitchPulsesAgainAndARefusedPullStillGivesTheClickPulse) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kFrontAndRear);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 1);
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);  // straight onto the other hitch
    ASSERT_EQ(DrawnEnd(), "-1.20 -0.20 1.00");
    EXPECT_EQ(PulsesTo(kRight), 2) << "a different hitch";

    Lua("StubManualAttach.attach(tractor, frontLoader, 1, 2); StubManualAttach.raise(frontLoader)");
    PullTrigger(kRight);
    ASSERT_EQ(BlinkingWarnings(), 1) << "Manual Attach refused the detach";
    EXPECT_EQ(PulsesTo(kRight), 3) << "its path ran, so the pull is felt";
}
