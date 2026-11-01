// Click Points and Coupling Points share the pointer cone (ADR-0035, spec #227, ticket #231), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game, Interactive Control and Manual Attach stood in by Lua stubs (manual_attach_test_support.h). Of a
// Click Point and a hitch both within the pointer cone, the one nearer the ray's centre is the target, either way round; the other selects nothing.
// Prior art: test_manual_attach_pointer.cpp (a Click Point and a hitch exactly on the ray's centre, where the Click Point wins the tie).

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

// The tractor's rear hitch with a plow lined up is 7 cm to the side of where the ray of kHandX runs (2.4 degrees at 1.7 m): inside the cone, off its centre.
constexpr const char* kTractorHitchOffCentre = R"luau(
    _G.tractor = StubManualAttach.tractor({ rear = { -0.33, -0.2, 2.0 } })
    _G.plow = StubManualAttach.implement('plow')
    StubManualAttach.lineUp(plow, tractor, 1)
    StubManualAttach.detectAll()
)luau";

}  // namespace

TEST_F(VirtualGamepadManualAttachTest, AHitchNearerTheRaysCentreThanAClickPointIsTheTargetAndTheClickPointSelectsNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);  // the rear hitch is on the ray's centre
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.37, -0.2, 1.0)");  // 3 cm off the centre, 0.7 m ahead: about 2.5 degrees
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the ray ends on the hitch, which is nearer its centre";
    EXPECT_EQ(EvalLua(L, "tostring(StubIC.selected())"), "nil") << "the Click Point is not clickable";

    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1) << "the pull works the hitch";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "and clicks nothing";
}

TEST_F(VirtualGamepadManualAttachTest, AClickPointNearerTheRaysCentreThanAHitchIsTheTargetAndTheHitchWorksNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorHitchOffCentre);
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.4, -0.2, 1.0)");  // on the ray's centre
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.00") << "the ray ends on the Click Point, not on the hitch 2.4 degrees off its centre";

    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lever");
    EXPECT_EQ(Performs(), 0) << "the pull works only the Click Point";
}

TEST_F(VirtualGamepadManualAttachTest, MovingTheRayBetweenAClickPointAndAHitchHandsTheSelectionOverWithTheSelectPulseOfTheNewOne) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorHitchOffCentre);
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.4, -0.2, 1.5)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // the Click Point is on the centre, the hitch 2.4 degrees off it
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    ASSERT_EQ(PulsesTo(kRight), 1);

    POINT_HAND(kRight, kHandX - 0.06f, kHandY, kHandZ);  // the hitch is now nearly on the centre, the Click Point 2.9 degrees off it
    EXPECT_EQ(DrawnEnd(), "-0.33 -0.20 2.00");
    EXPECT_EQ(EvalLua(L, "tostring(StubIC.selected())"), "nil");
    EXPECT_EQ(PulsesTo(kRight), 2) << "the select pulse of the hitch";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(PulsesTo(kRight), 3) << "and of the Click Point again";
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 3) << "none while the selection stays";
}

TEST_F(VirtualGamepadManualAttachTest, AClickPointToldToSelectNoneShowsNoHoverBlinkInInteractiveControl) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);  // the hitch is on the ray's centre
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.37, -0.2, 1.0)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    FRAMES(5);
    EXPECT_EQ(EvalLua(L, "tostring(StubIC.blinks.lever)"), "nil") << "Interactive Control's hover follows: the Click Point is not under the pointer";
}

TEST_F(VirtualGamepadManualAttachTest, WithOnlyAnAnalogClickPointInTheConeInteractiveControlActsEveryFrameTheTriggerIsHeldAsBefore) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);  // the hitch is 25 degrees off the ray below, outside the cone
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('dial', -1.2, -0.2, 1.0, { analog = true })");
    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "dial");
    ASSERT_EQ(DrawnEnd(), "-1.20 -0.20 1.00");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(6);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "6") << "once per frame while held";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "6") << "and not after the trigger is let go";
    EXPECT_EQ(Performs(), 0);
}

TEST_F(VirtualGamepadManualAttachTest, WithOnlyAHitchInTheConeItWorksAsInTheFirstTicketWhileAClickPointOutsideItIsLeftAlone) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('farOff', 3.0, 1.5, 1.0)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0");
}

TEST_F(VirtualGamepadManualAttachTest, InTheCabNoHitchIsOfferedSoTheClickPointKeepsTheRayAndInteractiveControlSwitchesOn) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);  // the hitch is on the ray's centre, the Click Point 2.5 degrees off it
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.37, -0.2, 1.0)");
    Lua("g_currentMission.controlledVehicle = { getActiveCamera = function() return nil end }; StubIC.equip(g_currentMission.controlledVehicle, false)");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);

    EXPECT_EQ(EvalLua(L, "tostring(#StubIC.stateWrites > 0 and StubIC.stateWrites[1].state)"), "true") << "Interactive Control's auto-on is as before";
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(DrawnEnd(), "-0.37 -0.20 1.00");
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lever");
    EXPECT_EQ(Performs(), 0);
}

TEST_F(VirtualGamepadManualAttachTest, EitherOfThemSelectedWinsOverAnAnimatedObjectAndAnObjectObjectGrabCouldPickUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorHitchOffCentre);
    Lua(kStubInteractiveControl);
    Lua("StubIC.install('ok'); StubIC.addPoint('lever', -0.4, -0.2, 1.5)");
    Lua("_G.door = StubAnimatedObject.door({ name = 'door', x = -0.4, y = -0.2, z = 1.2 }); StubAnimatedObject.enterZone(door)");
    Lua("_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.3, 0.05, { name = 'bale' })");

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);  // the Click Point is nearer the centre
    EXPECT_EQ(DrawnEnd(), "-0.40 -0.20 1.50") << "the Click Point, not the bale or the door the ray meets first";
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "lever");
    EXPECT_EQ(Number(L, "#StubAnimatedObject.toggles"), 0);
    ReleaseTrigger(kRight);
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(2);

    POINT_HAND(kRight, kHandX - 0.06f, kHandY, kHandZ);  // the hitch is nearer the centre
    EXPECT_EQ(DrawnEnd(), "-0.33 -0.20 2.00") << "the hitch, not the bale or the door";
    PullTrigger(kRight);
    EXPECT_EQ(Performs("joint"), 1);
    EXPECT_EQ(Number(L, "#StubAnimatedObject.toggles"), 0);
    ReleaseTrigger(kRight);
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 0);
}
