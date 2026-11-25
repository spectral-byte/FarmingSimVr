#define HAND_TOOLS_PRE_FRAME_EXTRA_LUA "if StubIC and StubIC.tick then StubIC.tick() end"

#include "animated_object_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace animated_object_test;

}

TEST_F(VirtualGamepadAnimatedObjectsTest, WhileTheRayIsOnADoorThePointerHandsTriggerReadsReleasedOnThePadAndTheOtherHandsStillReachesIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(Pad().bLeftTrigger, 128) << "the hand that is not the Pointer Hand keeps its trigger";
    EXPECT_EQ(Toggles(), 0) << "and toggles nothing";
    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(1);

    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that toggles the door is not also a binding";
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "for as long as the trigger is held";
    ReleaseTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, WithNoDoorUnderTheRayTheTriggerReachesThePad) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "outside the door's zone the trigger is a binding's like any other";
    ReleaseTrigger(kRight);

    EnterZone();
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "so is it in the zone when the ray is not on the door";
    EXPECT_EQ(Toggles(), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ATriggerHeldWhenTheRayLandsOnADoorKeepsItsBindingAndTogglesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(Toggles(), 0) << "and is not a toggle";

    ReleaseTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1) << "pulled afresh it is a toggle";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and not a binding";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ATriggerThatToggledStaysOffThePadUntilLetGoEvenIfTheRayLeavesTheDoor) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Toggles(), 1);

    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that toggled does not turn into a binding as the ray moves off";
    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "pulled afresh away from the door it is the pad's";
    EXPECT_EQ(Toggles(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, WhileTheDeskMouseHasControlTheRayIsNotDrawnOnADoorAndAPullTogglesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:notePointerMouseMoved()", "=mouseMoved"), 0);
    FRAMES(1);
    EXPECT_EQ(DrawnEnd(), "none") << "the mouse has the pointer: no ray on the door";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "and the trigger is a binding's";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    FRAMES(2);
    EXPECT_EQ(Toggles(), 0) << "a trigger pulled while the mouse had control toggles nothing once the hand moves";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it keeps its binding";

    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1) << "only a new pull does";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, InAMenuNothingIsSelectedAndNothingIsToggled) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "and the door works again once the menu closes";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, WhileAStereoDialogShowsTheRayIsOnNoDoorAndThePullIsForTheDialog) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

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
    EXPECT_EQ(Toggles(), 0) << "a pull for the dialog is not also a toggle";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it reaches the dialog through the pad";

    ReleaseTrigger(kRight);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false; g_gui.currentDialogName = ''; g_gui.dialogs = {}", "=closeFishDialog"), 0);
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "the ray works again once it closes";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, In2DFloatingWindowModeNothingIsSelectedAndNothingIsToggled) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(true)", "=2d"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getIs2DViewActive()"), "true");
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(false)", "=vr"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
}

TEST_F(VirtualGamepadAnimatedObjectsTest, WithMotionControllersOffNothingIsSelectedOrToggledAndOnAgainTheDoorWorks) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    FRAMES(2);
    EXPECT_EQ(Toggles(), 0);
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=on"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, InAVehicleADoorIsNotWorked) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "-0.40 -0.20 1.95");

    ASSERT_EQ(RunLuauChunk(L, "g_currentMission.controlledVehicle = { getActiveCamera = function() return nil end }", "=getIn"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "out of scope from the cab: the ray is not drawn on the door";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, TheToolHandHoldingAHandToolNeverTogglesADoorWhileTheOtherHandCan) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, kDoorOnTheLeft, "=leftDoor"), 0);
    EnterZone("door");
    EnterZone("leftDoor");
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");

    headset.SetHandPointing(kRight, HandAt(kHandX, kHandY, kHandZ));
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none") << "the Tool Hand is no Pointer Hand";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 0);
    ReleaseTrigger(kRight);

    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "0.40 -0.20 1.95");
    PullTrigger(kLeft);
    EXPECT_EQ(Toggles("leftDoor"), 1) << "the other hand can work a door";
    ReleaseTrigger(kLeft);

    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1) << "and the Tool Hand's pull still toggles nothing";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, AHandCarryingAHeldObjectNeverTogglesADoorWhileTheOtherHandCan) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor("ok", false));
    ASSERT_EQ(RunLuauChunk(L, kDoorOnTheLeft, "=leftDoor"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })", "=bale"), 0);
    EnterZone("leftDoor");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    ASSERT_EQ(PickUps(), 1) << "the right hand carries the bale";

    ASSERT_EQ(EvalLua(L, "tostring(VRCompanion:getPointerHand())"), "nil") << "a hand carrying a Held Object is no Pointer Hand";
    headset.SetHandInputs(kRight, hand_tools_test::Squeeze(1.0f, 1.0f));
    FRAMES(2);
    EXPECT_EQ(Toggles(), 0);
    ASSERT_EQ(Drops(), 0) << "still carrying";
    headset.SetHandInputs(kRight, hand_tools_test::Squeeze(0.0f, 1.0f));
    FRAMES(1);

    POINT_HAND(kLeft, -kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), "0.40 -0.20 1.95");
    PullTrigger(kLeft);
    EXPECT_EQ(Toggles("leftDoor"), 1) << "the other hand can work a door";
}

TEST_F(VirtualGamepadAnimatedObjectsTest, AClickPointWithinThePointerConeWinsOverADoorBehindIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, StubChunk("interactive_control"), "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok'); _G.point = StubIC.addPoint('lever', -0.4, -0.2, 1.0)", "=clickPoint"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.00") << "the ray ends on the Click Point, not on the door behind it";

    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lever");
    EXPECT_EQ(Toggles(), 0) << "the pull clicks the Click Point and toggles no door";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "point.inRange = false", "=pointGone"), 0);
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "without the Click Point the door is the ray's";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
}

TEST_F(VirtualGamepadAnimatedObjectsTest, ClickPointsOutsideTheConeDoNotHideADoorFromTheRay) {
    ASSERT_NO_FATAL_FAILURE(StartWithDoor());
    ASSERT_EQ(RunLuauChunk(L, StubChunk("interactive_control"), "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok'); StubIC.addPoint('farOff', 3.0, 1.5, 1.0)", "=clickPoint"), 0);
    EnterZone();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "tostring(StubIC.selected())"), "nil");
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.95") << "Click Points exist, none is selected: the ray is the door's";
    PullTrigger(kRight);
    EXPECT_EQ(Toggles(), 1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0");
}
