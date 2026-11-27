#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

constexpr const char* kFlashlightHelpers = R"luau(
    function _G.isInTheHand(tool, which) return isBelow(tool.rootNode, handHolder(which)) end
    function _G.isWhereTheGamePutsIt(tool)
        if getParent(tool.rootNode) ~= foot.cam.pitchNode then return false end
        local x, y, z = getTranslation(tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        return math.abs(x - p.x) < 1e-6 and math.abs(y - p.y) < 1e-6 and math.abs(z - p.z) < 1e-6
    end
    function _G.beam() return { StubHandTool.beam(flashlight) } end
    function _G.lens() return { StubHandTool.lensRay(flashlight) } end
    function _G.targeterRay() return { StubHandTool.lastRay() } end
    function _G.headRay()
        local x, y, z = getWorldTranslation(foot.cam.firstPersonCamera)
        local dx, dy, dz = StubScene.worldDirection(foot.cam.firstPersonCamera, 0, 0, -1)
        return { x, y, z, dx, dy, dz }
    end
    function _G.sameRay(a, b)
        for i = 1, 6 do
            if a[i] == nil or b[i] == nil or math.abs(a[i] - b[i]) > 1e-3 then return false end
        end
        return true
    end
    function _G.apart(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
    function _G.rayText(r) return r[1] and string.format("(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)", table.unpack(r)) or "none" end
)luau";

class VirtualGamepadHandToolsFlashlightTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string gameStubs;

    void Start(const char* variant = "ok") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        gameStubs = FlashlightGame(variant);
        StartOptions options;
        options.gameStubs = gameStubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kFlashlightHelpers, "=flashlightHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void StartWithFlashlightInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldFlashlight(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "isInTheHand(flashlight, 'right')"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    int Switches() { return static_cast<int>(Number(L, "#StubHandTool.flashlightCalls")); }
    bool On() { return EvalLua(L, "flashlight.spec_flashlight.isActive") == "true"; }
    bool PlayerThinksItIsOn() { return EvalLua(L, "foot.player.isFlashlightActive") == "true"; }
    bool InTheHand() { return EvalLua(L, "isInTheHand(flashlight, 'right')") == "true"; }
    bool WhereTheGamePutsIt() { return EvalLua(L, "isWhereTheGamePutsIt(flashlight)") == "true"; }
    bool BeamFromTheLens() { return EvalLua(L, "sameRay(beam(), lens())") == "true"; }
    bool BeamFromTheCamera() { return EvalLua(L, "sameRay(beam(), headRay())") == "true"; }
    std::string BeamAndLens() { return "the beam is " + EvalLua(L, "rayText(beam())") + ", the lens is " + EvalLua(L, "rayText(lens())"); }

    void AimHand(uint32_t hand, float x, float y, float z, float pitch, float yaw) {
        headset.SetHandPose(hand, HandAt(x, y, z, pitch, yaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, pitch, yaw));
    }

    void ExpectTheGameHasItsWayWhen(const char* variant) {
        ASSERT_NO_FATAL_FAILURE(Start(variant));
        ASSERT_NO_FATAL_FAILURE(HoldFlashlight(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(3);

        EXPECT_TRUE(WhereTheGamePutsIt()) << "the flashlight is where the game puts it";
        EXPECT_FALSE(InTheHand());
        EXPECT_TRUE(BeamFromTheCamera()) << "and its beam is placed as in the game: " << BeamAndLens();
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "one warning says why";
        headset.SetHandInputs(kRight, Trigger(1.0f));
        FRAMES(2);
        EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is the pad's";
        EXPECT_EQ(Switches(), 0) << "and does not switch the flashlight";
        headset.SetHandInputs(kRight, Trigger(0.0f));
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(flashlight); StubHandTool.hold(flashlight)", "=takeAgain"), 0);
        PICK_UP(kRight, 0.30f, -0.10f, -0.30f);
        FRAMES(3);
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and it is not repeated";
        EXPECT_TRUE(WhereTheGamePutsIt());
        EXPECT_NE(EvalLua(L, "VRCompanion:getHandToolsDiagnostics()").find("flashlightBeam wrap: gave up"), std::string::npos)
            << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }
};

}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheFlashlightHeldOnFootHangsBelowTheRightVirtualHandAndFollowsItAcrossFrames) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldFlashlight(L));
    EXPECT_TRUE(WhereTheGamePutsIt()) << "before any hand is picked up the game's placement is all there is";

    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_TRUE(InTheHand()) << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(flashlight.rootNode, 'right') }; _G.handBefore = { getWorldTranslation(handHolder('right')) }", "=before"), 0);

    MOVE_HAND(kRight, 0.55f, 0.10f, -0.45f);
    FRAMES(1);

    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local hx, hy, hz = getWorldTranslation(handHolder('right'))
        local moved = math.sqrt((hx - handBefore[1]) ^ 2 + (hy - handBefore[2]) ^ 2 + (hz - handBefore[3]) ^ 2)
        assert(moved > 0.3, "the hand moved " .. moved)
        local x, y, z = offsetInHolder(flashlight.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the flashlight stayed where it was in the hand")
    )luau", "=follows"), 0);
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheFlashlightIsWhereTheGamePutsItWhileTheToolHandIsSetDownOrUntrackedAndBackInTheHandWhenItIsUsedAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_TRUE(WhereTheGamePutsIt()) << "set down: the game's own attachToolToCamera put the flashlight in front of the view";
    EXPECT_FALSE(InTheHand());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_TRUE(InTheHand()) << "picked up again: the flashlight is back in the hand";
    EXPECT_FALSE(WhereTheGamePutsIt());

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_TRUE(WhereTheGamePutsIt()) << "untracked: the flashlight is where the game puts it";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_TRUE(InTheHand()) << "tracked again: back in the hand";

    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.30f), false);
    FRAMES(1);
    EXPECT_TRUE(WhereTheGamePutsIt());
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, ALeftMotionControllerPickedUpDoesNotTakeTheFlashlightAndNeitherDoesAHandToolsSwitchedOff) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldFlashlight(L));
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(2);
    EXPECT_TRUE(WhereTheGamePutsIt()) << "the Tool Hand is the right one";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    EXPECT_TRUE(WhereTheGamePutsIt()) << "with vrHandTools off the game has the flashlight";
    EXPECT_TRUE(BeamFromTheCamera()) << BeamAndLens();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(2);
    EXPECT_TRUE(InTheHand()) << "and with it on again the right hand has it";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheLightNodeSitsAtTheFlashlightAndShinesAlongItAndFollowsTheHandNotTheCamera) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    EXPECT_TRUE(BeamFromTheLens()) << BeamAndLens();
    EXPECT_GT(Number(L, "apart(beam(), headRay())"), 0.2) << "and not at the camera: the hand is held away from the head";
    EXPECT_NEAR(Number(L, "beam()[4]^2 + beam()[5]^2 + beam()[6]^2"), 1.0, 1e-3);

    ASSERT_EQ(RunLuauChunk(L, "_G.beamBefore = beam()", "=before"), 0);
    AimHand(kRight, 0.50f, 0.15f, -0.55f, 0.6f, 0.9f);
    FRAMES(2);

    EXPECT_TRUE(BeamFromTheLens()) << BeamAndLens();
    EXPECT_GT(Number(L, "apart(beamBefore, beam())"), 0.3) << "the light moved with the hand";
    EXPECT_FALSE(BeamFromTheCamera());
    EXPECT_LT(Number(L, "beam()[4]*headRay()[4] + beam()[5]*headRay()[5] + beam()[6]*headRay()[6]"), 0.99) << "and turned away from where the head looks";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheLightNodeIsPlacedFromTheCameraAsInTheGameWhileTheHandIsSetDownOrUntrackedAndFromTheFlashlightAgainWhenItIsBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_TRUE(BeamFromTheLens()) << BeamAndLens();

    AdvanceGameTime(3100);
    FRAMES(2);
    EXPECT_TRUE(BeamFromTheCamera()) << "set down: " << BeamAndLens() << ", the camera's ray is " << EvalLua(L, "rayText(headRay())");

    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    EXPECT_TRUE(BeamFromTheLens()) << "picked up again: " << BeamAndLens();

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_TRUE(BeamFromTheCamera()) << "untracked: " << BeamAndLens();
    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    EXPECT_TRUE(BeamFromTheLens()) << "tracked again: " << BeamAndLens();

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_TRUE(BeamFromTheCamera()) << "Hand Tools off, at once: " << BeamAndLens();
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheBeamOfTheFlashlightInHandIsPlacedWhetherTheLightIsOnOrOffAndTheSwitchDoesNotMoveIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "_G.beamOff = beam()", "=beamOff"), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(On());
    EXPECT_TRUE(BeamFromTheLens()) << BeamAndLens();
    EXPECT_EQ(EvalLua(L, "sameRay(beamOff, beam())"), "true") << "the same place with the light on";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheLightNodeIsCarriedAlongByAFrameRenderedWithoutAnUpdateAfterThePlayerMoved) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_TRUE(BeamFromTheLens()) << BeamAndLens();
    ASSERT_EQ(RunLuauChunk(L, "_G.beamBefore = beam()", "=before"), 0);

    ASSERT_EQ(RunLuauChunk(L, "setTranslation(foot.parent, 0.12, 0, 0.05); StubScene.updateCamera()", "=moveWithoutUpdate"), 0);

    EXPECT_TRUE(BeamFromTheLens()) << BeamAndLens();
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local now = beam()
        assert(math.abs(now[1] - beamBefore[1] - 0.12) < 1e-3 and math.abs(now[2] - beamBefore[2]) < 1e-3
            and math.abs(now[3] - beamBefore[3] - 0.05) < 1e-3, "the light moved by " .. (now[1] - beamBefore[1]) .. " " .. (now[3] - beamBefore[3]))
    )luau", "=carriedAlong"), 0);
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WithTheFlashlightInTheToolHandTheTargetersRayStartsAtTheFlashlightAndPointsAlongItAndIsTheHeadsOtherwise) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), lens())"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())") << ", " << BeamAndLens();
    EXPECT_GT(Number(L, "apart(targeterRay(), headRay())"), 0.2) << "and not from the head";

    AimHand(kRight, 0.50f, 0.15f, -0.55f, 0.6f, 0.9f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), lens())"), "true") << "it follows the hand: " << EvalLua(L, "rayText(targeterRay())");

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "untracked: the head's ray";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, APressOfTheToolHandsTriggerSwitchesTheFlashlightOnceHoweverLongItIsHeldAndTheNextPressSwitchesItOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_FALSE(On());
    ASSERT_EQ(Switches(), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 1) << "the pull switches it";
    EXPECT_TRUE(On());
    EXPECT_EQ(EvalLua(L, "StubHandTool.flashlightCalls[1].tool == flashlight and StubHandTool.flashlightCalls[1].isActive"), "true")
        << "through the flashlight's own switch";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.flashlightCalls[1].noEventSend)"), "nil") << "which tells the other players";

    FRAMES(10);
    EXPECT_EQ(Switches(), 1) << "once, not once per frame held";
    EXPECT_TRUE(On());

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(Switches(), 1) << "letting go does nothing";
    EXPECT_TRUE(On());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(Switches(), 2);
    EXPECT_FALSE(On()) << "the next press switches it off";
    EXPECT_EQ(EvalLua(L, "StubHandTool.flashlightCalls[2].isActive"), "false");
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, APressSwitchesTheLightOffWhenTheGameHadSwitchedItOnAndThePlayersFlagFollows) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "flashlight:setFlashlightIsActive(true, true); foot.player.isFlashlightActive = true; StubHandTool.flashlightCalls = {}", "=gameSwitchedItOn"), 0);
    ASSERT_TRUE(On());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_FALSE(On());
    EXPECT_FALSE(PlayerThinksItIsOn()) << "the game restores the light from the player's flag when a Hand Tool is dropped";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_TRUE(On());
    EXPECT_TRUE(PlayerThinksItIsOn());
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, AHalfPulledTriggerSwitchesOnlyOncePastTheThresholdAndNotAgainUntilItIsMostlyReleasedAndPulledAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    headset.SetHandInputs(kRight, Trigger(0.3f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 0) << "a light touch is not a pull";
    headset.SetHandInputs(kRight, Trigger(0.7f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 1);
    headset.SetHandInputs(kRight, Trigger(0.5f));
    FRAMES(2);
    EXPECT_EQ(Switches(), 1) << "a trigger that rests half-way does not flicker";
    headset.SetHandInputs(kRight, Trigger(0.75f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 1) << "and pulling on from half-way is the same pull";
    headset.SetHandInputs(kRight, Trigger(0.2f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 1);
    headset.SetHandInputs(kRight, Trigger(0.8f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 2) << "pulled afresh";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheToolHandsTriggerReadsReleasedOnXInputWhileItHoldsTheFlashlightAndTheOtherHandsTriggerStillReachesThePad) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(On());
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the trigger that switches the light is not also a binding";

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(Pad().bLeftTrigger, 128) << "the other hand's trigger keeps its binding";
    EXPECT_EQ(Pad().bRightTrigger, 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("the Companion Mod is using: right trigger"), std::string::npos)
        << EvalLua(L, "runCommand('vrMotionControllerStatus')");

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and it still reads released while the flashlight is lit, not pulled";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, ATriggerAlreadyHeldWhenTheFlashlightComesIntoTheHandKeepsItsBindingAndSwitchesNothingUntilLetGoAndPulledAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    ASSERT_NO_FATAL_FAILURE(HoldFlashlight(L));
    FRAMES(3);
    ASSERT_TRUE(InTheHand());
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(Switches(), 0) << "and does not switch the flashlight";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 0);
    EXPECT_EQ(Switches(), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Switches(), 1) << "pulled afresh it switches";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and is not a binding";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheFlashlightDoesNotVibrateTheToolHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_TRUE(On());
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_FALSE(On());

    EXPECT_TRUE(headset.HapticPulses().empty()) << headset.HapticPulses().size() << " pulses";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, TheGamesOwnSwitchStillWorksWhileTheFlashlightIsInTheToolHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    ASSERT_EQ(RunLuauChunk(L, "flashlight:setFlashlightIsActive(true)", "=gamesOwnSwitch"), 0);
    EXPECT_TRUE(On()) << "nothing here replaces the game's own switch";
    FRAMES(2);
    EXPECT_TRUE(On());
    EXPECT_TRUE(InTheHand());
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WithTheToolHandSetDownTheTriggerIsThePadsAndSwitchesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    AdvanceGameTime(3100);
    FRAMES(1);
    ASSERT_FALSE(InTheHand());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    EXPECT_EQ(Switches(), 0);
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WithVrHandToolsOffTheTriggerIsThePadsAndSwitchesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(1);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    EXPECT_EQ(Switches(), 0);
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WhenThePlayerPutsTheFlashlightAwayForAnUnsupportedHandToolTheTriggerIsThePadsAndSwitchesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(flashlight)", "=putAway"), 0);
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    FRAMES(2);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    EXPECT_EQ(Switches(), 0);
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WithTheFlashlightsOnUpdateMissingTheFlashlightWorksAsInTheGameAndTheLogHasOneWarning) {
    ExpectTheGameHasItsWayWhen("missing");
    EXPECT_EQ(EvalLua(L, "HandToolFlashlight.onUpdate"), "nil") << "the class is as the game made it";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WithTheFlashlightsOnUpdateReshapedTheFlashlightWorksAsInTheGameAndTheLogHasOneWarning) {
    ExpectTheGameHasItsWayWhen("reshaped");
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, WithoutTheGamesFlashlightClassAtAllThereIsNoWarningAndTheLanceStillWorks) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, UnloadingTheMapGivesTheFlashlightsOnUpdateBackAndAnotherModsLaterWrapIsNeverUndone) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());
    ASSERT_EQ(EvalLua(L, "HandToolFlashlight.onUpdate == StubHandTool.originalFlashlightOnUpdate"), "false") << "wrapped while the map is loaded";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "HandToolFlashlight.onUpdate == StubHandTool.originalFlashlightOnUpdate"), "true") << "the class has its own function back";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap('y')
        local inner = HandToolFlashlight.onUpdate
        _G.theirWrap = function(...) return inner(...) end
        HandToolFlashlight.onUpdate = _G.theirWrap
        VRCompanion:deleteMap()
    )luau", "=anotherModWraps"), 0);
    EXPECT_EQ(EvalLua(L, "HandToolFlashlight.onUpdate == theirWrap"), "true") << "the other mod's wrap is still the class function";
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, AFlashlightPointedNearlyStraightUpOrDownKeepsAFiniteNonFlippingBeam) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    AimHand(kRight, 0.25f, -0.10f, -0.30f, 2.3561945f, 0.0f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(beam()[5] == beam()[5])"), "true");
    EXPECT_TRUE(BeamFromTheLens()) << BeamAndLens();

    AimHand(kRight, 0.25f, -0.10f, -0.30f, -0.7853982f, 0.0f);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(beam()[5] == beam()[5])"), "true");
    EXPECT_TRUE(BeamFromTheLens()) << BeamAndLens();
}

TEST_F(VirtualGamepadHandToolsFlashlightTest, AnActiveFlashlightKeepsTheToolHandInUsePastTheIdleTimeoutUntilSwitchedOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithFlashlightInTheRightHand());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_TRUE(On());

    AdvanceGameTime(3500);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true");
    EXPECT_TRUE(InTheHand());
    EXPECT_FALSE(WhereTheGamePutsIt());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_FALSE(On());

    AdvanceGameTime(3500);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_FALSE(InTheHand());
    EXPECT_TRUE(WhereTheGamePutsIt());
}
