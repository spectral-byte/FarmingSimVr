// Manual Attach, the mod in other releases and the diagnostics (ADR-0035, spec #227, ticket #228), seen from outside: the real Companion Mod runs
// against the real VRBridge over FakeTrackedHeadset, with Manual Attach stood in by a Lua stub (manual_attach_test_support.h) in the "releases"
// the stub can be in. A function the module relies on that is missing or has another shape, or Manual Attach not being loaded at all, turns the
// support off with ONE warning and leaves Manual Attach as it is; the module wraps nothing, so there is nothing to hand back at map unload.
// Prior art: test_animated_objects_wraps.cpp and test_pointer_ray.cpp (Interactive Control missing or mismatched).

#include "manual_attach_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;
using namespace manual_attach_test;

class VirtualGamepadManualAttachWrapsTest : public VirtualGamepadManualAttachTest {
protected:
    // Manual Attach is in a release where something is missing or reshaped: the module warns once, selects nothing, works nothing, and Manual
    // Attach (its key: the handler's own execute path) works as it does without VR.
    void ExpectTheSupportGivesUpWith(const char* variant) {
        SCOPED_TRACE(variant);
        ASSERT_NO_FATAL_FAILURE(StartWithManualAttach(variant));
        Lua(kTractorAndImplement);
        NoteManualAttachFunctions();
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        FRAMES(3);
        EXPECT_EQ(Warnings(), 1) << "one warning, however many frames";
        EXPECT_EQ(DrawnEnd(), "none") << "nothing is selected";
        PullTrigger(kRight);
        EXPECT_EQ(Performs(), 0) << "and a pull works nothing";
        EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is a binding's";
        EXPECT_TRUE(ManualAttachFunctionsAreAsFound()) << "Manual Attach is untouched";
        EXPECT_NE(EvalLua(L, "VRCompanion:getManualAttachDiagnostics()").find("gave up"), std::string::npos);
        EXPECT_EQ(Warnings(), 1);
    }
};

}  // namespace

// ---- Giving up -------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachWrapsTest, ManualAttachNotBeingLoadedTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("notLoaded"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, NoInstanceTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("instanceMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, NoAttachmentHandlerTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("handlerMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, NoHandlerClassTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("classMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, ARenamedRangeHelperTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("rangeHelperMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, AReshapedRangeHelperTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("rangeHelperReshaped"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, ARenamedValidHelperTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("validHelperMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, AReshapedValidHelperTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("validHelperReshaped"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, AMissingReachConstantTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("constantsMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, ARenamedActionInfoTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("actionInfoMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, AReshapedActionInfoTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("actionInfoReshaped"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, ARenamedExecutePathTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("executeMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, AReshapedExecutePathTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("executeReshaped"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, ARenamedPerformTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("performMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, NoListOfDetectedVehiclesTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("detectedMissing"); }
TEST_F(VirtualGamepadManualAttachWrapsTest, NoCanAttachByHandFlagTurnsTheSupportOffWithOneWarning) { ExpectTheSupportGivesUpWith("flagMissing"); }

TEST_F(VirtualGamepadManualAttachWrapsTest, AGameWithoutManualAttachAtAllWarnsOnceAndLeavesTheRestAlone) {
    ASSERT_NO_FATAL_FAILURE(StartWithoutManualAttach());
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    FRAMES(3);
    EXPECT_EQ(Warnings(), 1);
    EXPECT_NE(EvalLua(L, "VRCompanion:getManualAttachDiagnostics()").find("gave up"), std::string::npos);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1") << "the Pointer Hand itself is unaffected";
}

TEST_F(VirtualGamepadManualAttachWrapsTest, ManualAttachsObjectsInTheGlobalTableAreFoundWhenTheModEnvironmentHasNone) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach("globalFallback"));
    Lua(kTractorAndImplement);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_EQ(DrawnEnd(), kRearHitch);
    PullTrigger(kRight);
    EXPECT_EQ(Attached("plow"), "true");
    EXPECT_EQ(Warnings(), 0);
}

TEST_F(VirtualGamepadManualAttachWrapsTest, NoWarningWhileNoMotionControllerIsTrackedOrWhileMotionControllersAreOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach("notLoaded"));
    headset.SetHandTracked(kLeft, false);
    headset.SetHandTracked(kRight, false);
    FRAMES(5);
    EXPECT_EQ(Warnings(), 0) << "a player without Motion Controllers is never told about Manual Attach";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=off"), 0);
    headset.SetHandTracked(kRight, true);
    FRAMES(5);
    EXPECT_EQ(Warnings(), 0) << "with Motion Controllers off the support is never looked at";
}

// ---- Unloading the map -----------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachWrapsTest, UnloadingTheMapLeavesManualAttachExactlyAsFoundAndTheNextMapStartsAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kTractorAndImplement);
    NoteManualAttachFunctions();
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    PullTrigger(kRight);
    ASSERT_EQ(Performs("joint"), 1);

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_TRUE(ManualAttachFunctionsAreAsFound());
    EXPECT_EQ(Warnings(), 0);
    EXPECT_NE(EvalLua(L, "VRCompanion:getManualAttachDiagnostics()").find("not installed yet"), std::string::npos)
        << "the support forgot Manual Attach with the map";
}

// ---- The diagnostics ---------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadManualAttachWrapsTest, ThePointerDiagnosticsCarryOneLineForManualAttach) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach());
    Lua(kFrontAndRear);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Manual Attach support: installed | hitches in reach: 2 | selected: hitch 1 of tractor"),
              std::string::npos) << EvalLua(L, "runCommand('vrPointerDiagnostics')");

    POINT_HAND(kRight, kHandX + 0.8f, kHandY, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("hitches in reach: 2 | selected: hitch 2 of tractor"), std::string::npos);

    POINT_HAND(kRight, kHandX, 1.0f, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("hitches in reach: 2 | selected: none"), std::string::npos);

    Lua("StubManualAttach.detect()");
    FRAMES(2);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("hitches in reach: 0 | selected: none"), std::string::npos);
}

TEST_F(VirtualGamepadManualAttachWrapsTest, ThePointerDiagnosticsSayTheSupportIsWaitingOrGaveUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithManualAttach("constantsMissing"));
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Manual Attach support: not installed yet"), std::string::npos);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    EXPECT_NE(EvalLua(L, "runCommand('vrPointerDiagnostics')").find("Manual Attach support: gave up (see the warning in the log)"), std::string::npos);
}
