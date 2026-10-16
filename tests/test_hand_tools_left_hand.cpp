// Hand Tools, choosing the left hand as the Tool Hand (spec #109, ticket #117, ADR-0029), seen from outside: the real Companion
// Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h,
// StubHandTool). The pressure washer lance is the tool: whatever the Tool Hand does for it, it does for every supported tool.
// The test moves the fake Motion Controllers, drives frames the way the game would and asserts what the player or the game
// would see: which Virtual Hand the lance hangs below, the calls of the lance's own use handler, what XInput answers for the
// Virtual Gamepad's triggers (the left one is LT), which hand is the Pointer Hand, what is written to the settings file.
// Prior art: test_hand_tools_trigger.cpp (the right hand), test_wheel_grab.cpp (a setting saved across sessions).

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsLeftHandTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The mod started with a player on foot, the Virtual Gamepad on and the game clock set (the in-use timeout is on it).
    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        StartOptions options;
        options.extraStubs = kXmlStubs;
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // The lance in hand, both Motion Controllers picked up, the triggers let go; the Tool Hand is the one given.
    void StartWithLanceAndBothHandsUp(const char* toolHand) {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldLance(L));
        const std::string command = std::string("runCommand('vrToolHand', '") + toolHand + "')";
        ASSERT_EQ(RunLuauChunk(L, command.c_str(), "=toolHand"), 0);
        PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
    }

    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    bool InTheHand(const char* which) { return EvalLua(L, std::string("isBelow(lance.rootNode, handHolder('") + which + "'))") == "true"; }
    bool Spraying() { return EvalLua(L, "lance.spec_highPressureWasherLance.isActivated") == "true"; }
    std::string PointerHand() { return EvalLua(L, "VRCompanion:getPointerHand()"); }
    std::string Diagnostics() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }
};

}  // namespace

// ---- The setting --------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsLeftHandTest, TheToolHandIsTheRightHandByDefaultAndTheCommandReportsIt) {
    ASSERT_NO_FATAL_FAILURE(Start());
    EXPECT_EQ(EvalLua(L, "VRCompanion:getToolHand()"), "1");
    const std::string status = EvalLua(L, "runCommand('vrToolHand')");
    EXPECT_NE(status.find("right"), std::string::npos) << status;
    EXPECT_NE(Diagnostics().find("Tool Hand: right"), std::string::npos) << Diagnostics();
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, VrToolHandChoosesTheHandAndRejectsAnythingElse) {
    ASSERT_NO_FATAL_FAILURE(Start());
    EXPECT_NE(EvalLua(L, "runCommand('vrToolHand', 'left')").find("left"), std::string::npos);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getToolHand()"), "0");
    EXPECT_NE(Diagnostics().find("Tool Hand: left"), std::string::npos) << Diagnostics();
    EXPECT_NE(EvalLua(L, "runCommand('vrToolHand')").find("left"), std::string::npos);

    const std::string usage = EvalLua(L, "runCommand('vrToolHand', 'sideways')");
    EXPECT_NE(usage.find("Usage"), std::string::npos) << usage;
    EXPECT_EQ(EvalLua(L, "VRCompanion:getToolHand()"), "0") << "an argument that is neither hand changes nothing";

    EXPECT_NE(EvalLua(L, "runCommand('vrToolHand', 'right')").find("right"), std::string::npos);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getToolHand()"), "1");
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, TheToolHandIsSavedAcrossSessions) {
    {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=left"), 0);
        EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.toolHand']"), "left") << "the choice is written to the settings file";
    }

    // A fresh game session reads it back.
    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    FakeTrackedHeadset secondHeadset;
    fs25vr::VRBridge secondBridge(secondHeadset.manager, nullptr);
    ASSERT_TRUE(secondBridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.toolHand'] = 'left'; VRCompanion:loadMap('x')", "=loadMapAgain"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getToolHand()"), "0");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'right')", "=right"), 0);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.toolHand']"), "right");
}

// ---- The left hand is the Tool Hand -------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsLeftHandTest, WithTheLeftChosenTheLanceHangsBelowTheLeftVirtualHandAndFollowsIt) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=left"), 0);
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(1);

    ASSERT_TRUE(InTheHand("left")) << Diagnostics();
    EXPECT_FALSE(InTheHand("right"));
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(0)"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(lance.rootNode, 'left') }", "=before"), 0);

    MOVE_HAND(kLeft, -0.55f, 0.10f, -0.45f);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(lance.rootNode, 'left')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the lance stayed where it was in the left hand")
    )luau", "=follows"), 0);
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, WithTheLeftChosenAnUnusedRightHandDoesNotCarryTheLanceAndTheLeftOneTakesItWhenPickedUp) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=left"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    EXPECT_FALSE(InTheHand("right")) << "the right hand is not the Tool Hand";
    EXPECT_FALSE(InTheHand("left")) << "and the left one is not picked up";
    EXPECT_EQ(EvalLua(L, "getParent(lance.rootNode) == foot.cam.pitchNode"), "true") << "the lance is where the game puts it";
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    EXPECT_TRUE(InTheHand("left"));
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, WithTheLeftChosenTheLeftTriggerUsesTheLanceAndReadsReleasedOnLTWhileTheRightTriggerKeepsItsBinding) {
    ASSERT_NO_FATAL_FAILURE(StartWithLanceAndBothHandsUp("left"));
    ASSERT_TRUE(InTheHand("left")) << Diagnostics();

    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    EXPECT_TRUE(Spraying()) << "the left trigger sprays";
    EXPECT_EQ(Pad().bLeftTrigger, 0) << "and is not also a binding";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the right trigger keeps its binding";
    EXPECT_EQ(Pad().bLeftTrigger, 0);
    EXPECT_TRUE(Spraying());
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("the Companion Mod is using: left trigger"), std::string::npos)
        << EvalLua(L, "runCommand('vrMotionControllerStatus')");

    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(1);
    EXPECT_FALSE(Spraying()) << "letting go ends the spray";
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, WithTheLeftChosenARightTriggerPullDoesNotSprayTheLance) {
    ASSERT_NO_FATAL_FAILURE(StartWithLanceAndBothHandsUp("left"));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.useCalls = {}", "=forget"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_FALSE(Spraying());
    EXPECT_EQ(EvalLua(L, "#StubHandTool.useCalls"), "0");
    EXPECT_EQ(Pad().bRightTrigger, 255);
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, WithTheLeftChosenAndTheLanceInHandTheRightHandIsThePointerHandInTheWorld) {
    ASSERT_NO_FATAL_FAILURE(StartWithLanceAndBothHandsUp("left"));
    ASSERT_TRUE(InTheHand("left")) << Diagnostics();
    EXPECT_EQ(PointerHand(), "1") << "the free right hand points at Click Points";

    // The Tool Hand's pull would move the Pointer Hand to it, were it not holding a tool.
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(PointerHand(), "1");
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, WithTheLeftChosenInAMenuEitherHandCanStillPointWhileTheLeftHoldsTheLance) {
    ASSERT_NO_FATAL_FAILURE(StartWithLanceAndBothHandsUp("left"));
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(PointerHand(), "0") << "in a menu the left hand can point";
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(PointerHand(), "1");
}

// ---- Changing the setting while a tool is held --------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsLeftHandTest, SwitchingToTheLeftWhileHoldingTheLanceMovesTheToolTheTriggerUseAndThePointingRuleOnTheNextFrame) {
    ASSERT_NO_FATAL_FAILURE(StartWithLanceAndBothHandsUp("right"));
    ASSERT_TRUE(InTheHand("right")) << Diagnostics();
    ASSERT_EQ(PointerHand(), "0");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=left"), 0);
    FRAMES(1);

    EXPECT_TRUE(InTheHand("left")) << "the lance is in the left hand: " << Diagnostics();
    EXPECT_FALSE(InTheHand("right"));
    EXPECT_EQ(PointerHand(), "1") << "the right hand, free now, is the Pointer Hand";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_FALSE(Spraying()) << "the right trigger no longer sprays";
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it is the pad's again";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    EXPECT_TRUE(Spraying()) << "the left one does";
    EXPECT_EQ(Pad().bLeftTrigger, 0);
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, SwitchingBackToTheRightMovesEverythingBackAndASprayInProgressEnds) {
    ASSERT_NO_FATAL_FAILURE(StartWithLanceAndBothHandsUp("left"));
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(Spraying());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'right')", "=right"), 0);
    FRAMES(1);
    EXPECT_FALSE(Spraying()) << "the spray ends with the move: the held trigger is not the Tool Hand's any more";
    EXPECT_TRUE(InTheHand("right")) << Diagnostics();
    EXPECT_EQ(PointerHand(), "0");

    headset.SetHandInputs(kLeft, Trigger(0.0f));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_TRUE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadHandToolsLeftHandTest, SwitchingToAHandThatIsSetDownLeavesTheLanceWhereTheGamePutsItUntilThatHandIsPickedUp) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    ASSERT_TRUE(InTheHand("right")) << Diagnostics();

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=left"), 0);
    FRAMES(1);
    EXPECT_FALSE(InTheHand("right"));
    EXPECT_FALSE(InTheHand("left"));
    EXPECT_EQ(EvalLua(L, "getParent(lance.rootNode) == foot.cam.pitchNode"), "true") << "back where the game puts it";

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    EXPECT_TRUE(InTheHand("left"));
}

