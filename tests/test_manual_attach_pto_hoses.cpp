// Manual Attach, working the PTO stub and the hose couplers with the Pointer Ray (ADR-0035, spec #227, ticket #229), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game and Manual Attach stood in by Lua stubs
// (manual_attach_test_support.h). The PTO stub and the hose couplers are the vehicle's side of an Implement's hitch; the player points at one
// and pulls the trigger, and Manual Attach's own PTO or hose path runs for the Implement on that hitch. Covered here: what is offered and when,
// which Coupling Point of a hitch wins, that each pull runs exactly one path, the pulses, the Virtual Gamepad, and the releases of Manual Attach
// the support gives up on. Prior art: test_manual_attach_hitch.cpp and test_manual_attach_pointer.cpp.

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

class VirtualGamepadManualAttachPtoHosesWrapsTest : public VirtualGamepadManualAttachTest {
protected:
    // Manual Attach or the game is in a release where something the PTO and hose support relies on is missing or reshaped: the module warns once,
    // selects nothing, works nothing, and Manual Attach (its key: the handler's own execute path) works as it does without VR.
    void ExpectTheSupportGivesUpWith(const char* variant) {
        SCOPED_TRACE(variant);
        ASSERT_NO_FATAL_FAILURE(StartWithManualAttach(variant));
        Lua(kTractorWithAttachedImplement);
        NoteManualAttachFunctions();
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        FRAMES(3);
        EXPECT_EQ(Warnings(), 1) << "one warning, however many frames";
        EXPECT_EQ(DrawnEnd(), "none") << "nothing is selected, not even the hitch";
        PullTrigger(kRight);
        EXPECT_EQ(Performs(), 0) << "and a pull works nothing";
        EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is a binding's";
        EXPECT_TRUE(ManualAttachFunctionsAreAsFound()) << "Manual Attach is untouched";
        EXPECT_NE(EvalLua(L, "VRCompanion:getManualAttachDiagnostics()").find("gave up"), std::string::npos);
        EXPECT_EQ(Warnings(), 1);
        ReleaseTrigger(kRight);

        // Manual Attach itself still works its candidate with its own key.
        Lua("StubManualAttach.handler:executeAttachment(StubManualAttach.handler.vehicleJointAttachment)");
        EXPECT_EQ(Attached("plow"), "false") << "its detach, as without VR";
    }
};

}  // namespace

// ---- The PTO stub ---------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, PullingTheTriggerWithTheRayOnThePtoStubConnectsThePtoAndASecondPullDisconnectsIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearPtoStub) << "the ray ends on the vehicle's PTO stub";
    ASSERT_EQ(Performs(), 0);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("pto"), 1) << "one pull runs Manual Attach's PTO attachment once";
    EXPECT_EQ(PtoConnected("plow"), "true");
    ReleaseTrigger(kRight);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("pto"), 2);
    EXPECT_EQ(PtoConnected("plow"), "false") << "the second pull disconnects it";
}

// ---- The hose couplers ----------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, PullingTheTriggerWithTheRayOnAHoseCouplerConnectsAllTheHosesAndASecondPullDisconnectsThem) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHoseCoupler) << "the ray ends on the vehicle's hose coupler";

    PullTrigger(kRight);
    EXPECT_EQ(Performs("hoses"), 1) << "one pull runs Manual Attach's hoses attachment once";
    EXPECT_EQ(HosesConnected("plow"), "true");
    ReleaseTrigger(kRight);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("hoses"), 2);
    EXPECT_EQ(HosesConnected("plow"), "false") << "the second pull disconnects them";
}

// ---- What is offered and when ---------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, ThePtoStubIsOfferedOnlyWhileTheImplementOnTheHitchHasAPto) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    Lua("plow.hasPto = false");
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "an Implement with no PTO: no ray on the PTO stub";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "and the trigger is a binding's";
    ReleaseTrigger(kRight);

    Lua("plow.hasPto = true");
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearPtoStub);
}

TEST_F(VirtualGamepadManualAttachTest, TheHoseCouplersAreOfferedOnlyWhileTheImplementOnTheHitchHasHoses) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    Lua("plow.hasHoses = false");
    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "an Implement with no hoses: no ray on a hose coupler";
    POINT_HAND(kRight, kCustomHoseHandX, kHoseHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "a custom hose target included";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearPtoStub) << "its PTO is still offered";

    Lua("plow.hasHoses = true");
    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHoseCoupler);
}

TEST_F(VirtualGamepadManualAttachTest, WithNothingAttachedAtTheHitchNeitherThePtoStubNorTheHoseCouplersAreOffered) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    Lua("plow.hasPto, plow.hasHoses = true, true");  // lined up with the hitch, not on it
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none");
    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "only the hitch, which attaches it";
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 1);
    ReleaseTrigger(kRight);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearPtoStub) << "and once it is on the hitch, its PTO stub too";
}

TEST_F(VirtualGamepadManualAttachTest, APassThroughPtoOutputWithNoNodeOfItsOwnGivesNoPtoCouplingPointAndManualAttachsKeyStillConnectsIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    // A trailer that passes the PTO on to what it tows: its output skips to the tractor's, so the trailer has no output node of its own.
    Lua(R"luau(
        _G.trailer = StubManualAttach.trailer('trailer', { -0.4, -0.2, 2.0 }, { passThroughPto = true })
        _G.baler = StubManualAttach.implement('baler', { hasPto = true })
        StubManualAttach.lineUp(baler, trailer, 1)
        StubManualAttach.attach(trailer, baler, 1, 1)
        StubManualAttach.detect(trailer)
    )luau");
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), "none") << "no PTO stub to point at";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0) << "a pull there works nothing";
    ReleaseTrigger(kRight);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the hitch is still a Coupling Point";

    Lua("StubManualAttach.handler:executeAttachment(StubManualAttach.handler.powerTakeOffAttachment)");
    EXPECT_EQ(PtoConnected("baler"), "true") << "Manual Attach's own key connects it";
}

// ---- Which Coupling Point wins --------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, WithTheHitchThePtoStubAndAHoseCouplerAllWithinTheConeTheOneNearestTheCentreOfTheRayIsSelectedAndWorked) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    Lua("VRCompanion.pointerConeDegrees = 25");  // wide enough for all three from where the player stands

    POINT_HAND(kRight, kHandX, 0.1f, kHandZ);  // a little above the hitch, nearest the PTO stub
    EXPECT_EQ(DrawnEnd(), kRearPtoStub);
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 1);
    EXPECT_EQ(Performs("pto"), 1) << "the PTO stub is worked, not the hitch or the hoses";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, 0.1f, -0.1f, kHandZ);  // beside it, nearest the hose coupler
    EXPECT_EQ(DrawnEnd(), kRearHoseCoupler);
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 2);
    EXPECT_EQ(Performs("hoses"), 1) << "the hose coupler is worked";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHandX, -0.15f, kHandZ);  // on the hitch
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 3);
    EXPECT_EQ(Performs("joint"), 1) << "the hitch is worked";
}

TEST_F(VirtualGamepadManualAttachTest, EachPullRunsExactlyOneOfManualAttachsPathsAndNeverAlsoTheOthers) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    Lua(R"luau(
        function _G.pathsRun()
            local kinds = {}
            for _, perform in ipairs(StubManualAttach.performs) do table.insert(kinds, perform.kind) end
            return table.concat(kinds, ",")
        end
    )luau");

    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(EvalLua(L, "pathsRun()"), "pto") << "the PTO stub touches neither the hoses nor the hitch";
    ReleaseTrigger(kRight);
    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "pathsRun()"), "pto,hoses") << "a hose coupler touches neither the PTO nor the hitch";
    ReleaseTrigger(kRight);
    POINT_HAND(kRight, kCustomHoseHandX, kHoseHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "pathsRun()"), "pto,hoses,hoses");
    ReleaseTrigger(kRight);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(EvalLua(L, "pathsRun()"), "pto,hoses,hoses,joint") << "the hitch touches neither the PTO nor the hoses";
    EXPECT_EQ(PtoConnected("plow"), "true") << "the PTO and the hoses stay as the earlier pulls left them";
    EXPECT_EQ(HosesConnected("plow"), "false");
    EXPECT_EQ(Attached("plow"), "true") << "and the detach was refused, with Manual Attach's own warning, as the PTO is on";
    EXPECT_EQ(BlinkingWarnings(), 1);
}

// ---- Giving up -------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, ARenamedPtoActionInfoTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("ptoActionInfoMissing"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, AReshapedPtoActionInfoTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("ptoActionInfoReshaped"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, ARenamedPtoPerformTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("ptoPerformMissing"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, ARenamedHoseActionInfoTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("hoseActionInfoMissing"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, AReshapedHoseActionInfoTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("hoseActionInfoReshaped"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, ARenamedHosePerformTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("hosePerformMissing"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, AGamePtoOutputLookupThatIsGoneTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("ptoLookupReshaped"); }
TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, AGameHoseTargetListThatIsGoneTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("hoseTargetsReshaped"); }

TEST_F(VirtualGamepadManualAttachPtoHosesWrapsTest, AVehicleWithNoPtoOutputsOrHoseTargetsAtAllIsNoReleaseProblem) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(R"luau(
        _G.trailer = StubManualAttach.trailer('trailer', { -0.4, -0.2, 2.0 })
        _G.baler = StubManualAttach.implement('baler', { hasPto = true, hasHoses = true })
        StubManualAttach.lineUp(baler, trailer, 1)
        StubManualAttach.attach(trailer, baler, 1, 1)
        StubManualAttach.detect(trailer)
    )luau");
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(DrawnEnd(), kRearHitch) << "the hitch is a Coupling Point; its vehicle simply has no PTO stub or hose coupler";
    EXPECT_EQ(Warnings(), 0);
}

// ---- The pulses -----------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, MovingFromOneOfAHitchsCouplingPointsToAnotherPulsesAgainButMovingBetweenItsHoseCouplersDoesNot) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearPtoStub);
    EXPECT_EQ(PulsesTo(kRight), 1) << "one light pulse as the ray lands on the PTO stub";
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 1) << "and none while it stays";

    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHoseCoupler);
    EXPECT_EQ(PulsesTo(kRight), 2) << "another as it moves onto the hose couplers of the same hitch";

    POINT_HAND(kRight, kCustomHoseHandX, kHoseHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearCustomHoseCoupler);
    EXPECT_EQ(PulsesTo(kRight), 2) << "the hose couplers are one Coupling Point: none between them";

    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHitch);
    EXPECT_EQ(PulsesTo(kRight), 3) << "one more on the hitch";
}

TEST_F(VirtualGamepadManualAttachTest, ThePullThatRanManualAttachsPtoOrHosePathGivesTheFirmerPulse) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 1);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("pto"), 1);
    EXPECT_EQ(PulsesTo(kRight), 2) << "the pull that connected the PTO is felt";
    FRAMES(3);
    EXPECT_EQ(PulsesTo(kRight), 2) << "once";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    ASSERT_EQ(PulsesTo(kRight), 3);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("hoses"), 1);
    EXPECT_EQ(PulsesTo(kRight), 4) << "and the one that connected the hoses";
    EXPECT_EQ(PulsesTo(kLeft), 0) << "the other hand feels nothing";
}

// ---- The Virtual Gamepad --------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachTest, WhileTheRayIsOnThePtoStubOrAHoseCouplerThePointerHandsTriggerReadsReleasedOnThePad) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("pto"), 1);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that works the PTO stub is not also a binding";
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 0);
    EXPECT_EQ(Performs(), 1) << "and it works it once";
    ReleaseTrigger(kRight);

    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("hoses"), 1);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "likewise for a hose coupler";
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and stays off the pad until let go, even if the ray leaves the coupler";
    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "pulled afresh away from every Coupling Point it is the pad's";
    EXPECT_EQ(Performs(), 2);
}

TEST_F(VirtualGamepadManualAttachTest, ATriggerHeldWhenTheRayLandsOnThePtoStubKeepsItsBindingAndWorksNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearPtoStub);
    FRAMES(3);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(Performs(), 0) << "and works nothing";
    ReleaseTrigger(kRight);
    PullTrigger(kRight);
    EXPECT_EQ(Performs("pto"), 1) << "pulled afresh it works the PTO stub";
}

TEST_F(VirtualGamepadManualAttachTest, ThePtoStubAndTheHoseCouplersFollowTheDeskMouseAMenuAndTheMotionControllerSwitchLikeTheHitch) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearPtoStub);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:notePointerMouseMoved()", "=mouseMoved"), 0);
    FRAMES(1);
    EXPECT_EQ(DrawnEnd(), "none") << "the Desk Mouse has the pointer";
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    POINT_HAND(kRight, kHandX, kPtoHandY, kHandZ);
    FRAMES(2);
    EXPECT_EQ(Performs(), 0) << "a pull that began under the mouse works nothing once the hand moves";
    ReleaseTrigger(kRight);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);

    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    ASSERT_EQ(DrawnEnd(), kRearHoseCoupler);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(DrawnEnd(), "none");
    PullTrigger(kRight);
    EXPECT_EQ(Performs(), 0);
    ReleaseTrigger(kRight);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=on"), 0);

    POINT_HAND(kRight, kHoseHandX, kHoseHandY, kHandZ);
    PullTrigger(kRight);
    EXPECT_EQ(Performs("hoses"), 1) << "back to normal, it works";
}

TEST_F(VirtualGamepadManualAttachTest, ACustomHoseTargetIsAHoseCouplerToo) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorWithAttachedImplement);
    POINT_HAND(kRight, kCustomHoseHandX, kHoseHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearCustomHoseCoupler);

    PullTrigger(kRight);
    EXPECT_EQ(Performs("hoses"), 1) << "the same path as any other coupler: all the hoses";
    EXPECT_EQ(HosesConnected("plow"), "true");
}
