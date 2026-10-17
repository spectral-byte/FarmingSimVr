// Hand Tools, the Tool Hand's trigger (ADR-0029), seen from outside: the real Companion Mod runs
// against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h,
// StubHandTool). The test moves the fake Motion Controllers, drives frames the way the game would and asserts what the game
// would see: the calls of the lance's own use handler (the one the game's ACTIVATE_HANDTOOL binding calls), what XInput
// answers for the Virtual Gamepad's triggers, which hand is the Pointer Hand. Never the mod's own state.
// Prior art: test_virtual_gamepad_cab_use.cpp (an input the mod uses reads released on XInput; the held-input rule),
// test_pointer_hand_switch.cpp (Pointer Hand rules) and test_hand_tools_lance.cpp (the lance in the hand).

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsTriggerTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The mod started with a player on foot, the Virtual Gamepad on and the game clock set (the in-use timeout is on it).
    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // The lance in the right Tool Hand, the hand picked up with the trigger let go.
    void StartWithLanceInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldLance(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    // The lance's own use handler, as the game's binding calls it: how many calls so far and the value of the last one.
    int UseCalls() { return static_cast<int>(Number(L, "#StubHandTool.useCalls")); }
    double LastUseValue() { return Number(L, "StubHandTool.useCalls[#StubHandTool.useCalls].value"); }
    bool Spraying() { return EvalLua(L, "lance.spec_highPressureWasherLance.isActivated") == "true"; }
    void ForgetUseCalls() { ASSERT_EQ(RunLuauChunk(L, "StubHandTool.useCalls = {}", "=forgetUseCalls"), 0); }
    std::string PointerHand() { return EvalLua(L, "VRCompanion:getPointerHand()"); }
};

}  // namespace

TEST_F(VirtualGamepadHandToolsTriggerTest, HoldingTheToolHandsTriggerCallsTheLancesUseHandlerAsHeldAndLettingGoEndsTheUse) {
    StartWithLanceInTheRightHand();
    ForgetUseCalls();
    ASSERT_FALSE(Spraying());

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    ASSERT_GE(UseCalls(), 1) << "the pull calls the use handler";
    EXPECT_GT(LastUseValue(), 0.0) << "as held";
    EXPECT_TRUE(Spraying());

    FRAMES(5);
    EXPECT_GT(LastUseValue(), 0.0);
    EXPECT_TRUE(Spraying()) << "it sprays for as long as the trigger is held";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(LastUseValue(), 0.0) << "letting go ends the use";
    EXPECT_FALSE(Spraying());

    const int calls = UseCalls();
    FRAMES(3);
    EXPECT_FALSE(Spraying());
    EXPECT_EQ(UseCalls(), calls) << "and the handler is left alone while the trigger is up";
}

TEST_F(VirtualGamepadHandToolsTriggerTest, AHalfPulledTriggerSpraysOnlyOncePastTheThresholdAndKeepsSprayingUntilItIsMostlyReleased) {
    StartWithLanceInTheRightHand();

    headset.SetHandInputs(kRight, Trigger(0.3f));
    FRAMES(1);
    EXPECT_FALSE(Spraying()) << "a light touch is not a pull";
    headset.SetHandInputs(kRight, Trigger(0.7f));
    FRAMES(1);
    EXPECT_TRUE(Spraying());
    headset.SetHandInputs(kRight, Trigger(0.5f));
    FRAMES(1);
    EXPECT_TRUE(Spraying()) << "a trigger that rests half-way does not flicker";
    headset.SetHandInputs(kRight, Trigger(0.2f));
    FRAMES(1);
    EXPECT_FALSE(Spraying());
}

TEST_F(VirtualGamepadHandToolsTriggerTest, TheToolHandsTriggerReadsReleasedOnXInputWhileItHoldsTheLanceAndTheOtherHandsTriggerStillReachesThePad) {
    StartWithLanceInTheRightHand();

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the trigger that sprays is not also a binding";

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(Pad().bLeftTrigger, 128) << "the other hand's trigger keeps its binding";
    EXPECT_EQ(Pad().bRightTrigger, 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("the Companion Mod is using: right trigger"), std::string::npos)
        << EvalLua(L, "runCommand('vrMotionControllerStatus')");

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

// The held-input rule of every Virtual Gamepad use: an input already held when the use would begin keeps its binding until it
// is let go, and the use does not act on it.
TEST_F(VirtualGamepadHandToolsTriggerTest, ATriggerAlreadyHeldWhenTheLanceComesIntoTheHandKeepsItsBindingAndDoesNotSprayUntilLetGoAndPulledAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));  // held for a binding, with no Hand Tool in the player's hands
    FRAMES(2);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    ASSERT_NO_FATAL_FAILURE(HoldLance(L));  // the player takes the lance
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_FALSE(Spraying()) << "and does not spray";
    EXPECT_EQ(UseCalls(), 0);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 0);
    EXPECT_FALSE(Spraying());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_TRUE(Spraying()) << "pulled afresh it sprays";
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and is not a binding";
}

TEST_F(VirtualGamepadHandToolsTriggerTest, ATriggerHeldWhenTheToolHandIsPickedUpWithTheLanceInHandKeepsItsBindingToo) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    headset.SetHandInputs(kRight, Trigger(1.0f));  // the controller is lifted with the trigger squeezed
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_FALSE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 255);
}

// The game's own binding (X on a pad, the mouse, the keyboard) calls the same handler.
TEST_F(VirtualGamepadHandToolsTriggerTest, TheGamesOwnUseToolBindingStillCallsTheLancesHandlerWhileTheLanceIsInTheToolHand) {
    StartWithLanceInTheRightHand();
    ForgetUseCalls();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(lance, 1)", "=bindingDown"), 0);
    EXPECT_EQ(UseCalls(), 1);
    EXPECT_EQ(LastUseValue(), 1.0);
    EXPECT_TRUE(Spraying());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(lance, 0)", "=bindingUp"), 0);
    EXPECT_EQ(LastUseValue(), 0.0);
    EXPECT_FALSE(Spraying());
}

// The Tool Hand does not stay "in use" holding a tool (a hand lying still for 3 s is set down); a held-still spray must not
// drop the lance while the trigger is down.
TEST_F(VirtualGamepadHandToolsTriggerTest, AHeldStillSprayDoesNotDropTheLanceAndAfterLettingGoTheHandIsSetDownAsAlways) {
    StartWithLanceInTheRightHand();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    ASSERT_TRUE(Spraying());

    for (int second = 0; second < 6; ++second) {
        AdvanceGameTime(1000);
        FRAMES(1);
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << "six seconds of spraying with a still hand";
    EXPECT_TRUE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 0);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false") << "set down after three still seconds without the trigger";
    EXPECT_EQ(Pad().bRightTrigger, 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "set down, the trigger is the pad's again";
    EXPECT_FALSE(Spraying());
}

TEST_F(VirtualGamepadHandToolsTriggerTest, WhenTheToolHandLosesTrackingWhileSprayingTheSprayStopsAndTheTriggerIsThePadsAgain) {
    StartWithLanceInTheRightHand();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(Spraying());

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_FALSE(Spraying()) << "an untracked hand does not leave the lance spraying";
    EXPECT_EQ(LastUseValue(), 0.0);

    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << "and the lance is back in the hand";
}

TEST_F(VirtualGamepadHandToolsTriggerTest, PuttingTheLanceAwayWhileSprayingEndsTheUseAndTheTriggerGoesBackToThePad) {
    StartWithLanceInTheRightHand();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(Spraying());

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); _G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)", "=switchTool"), 0);
    FRAMES(2);

    EXPECT_FALSE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 0) << "the pull that sprayed stays off the pad until it is let go";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "pulled afresh with an unsupported tool in hand it is the pad's";
}

// ---- Pointer Hand ---------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsTriggerTest, InTheWorldTheToolHandHoldingTheLanceIsNeverThePointerHandAndTheOtherHandIs) {
    StartWithLanceInTheRightHand();
    EXPECT_EQ(PointerHand(), "nil") << "the only hand in use holds the lance";

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    EXPECT_EQ(PointerHand(), "0") << "the other hand points at Click Points";

    // The Tool Hand's pull would switch the Pointer Hand to it, were it not holding a tool.
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(PointerHand(), "0");
    EXPECT_NE(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
}

TEST_F(VirtualGamepadHandToolsTriggerTest, InAMenuEitherHandCanPointEvenWhileTheToolHandHoldsTheLance) {
    StartWithLanceInTheRightHand();
    ASSERT_EQ(PointerHand(), "nil");

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    FRAMES(2);
    EXPECT_EQ(PointerHand(), "1") << "in a menu the right hand, the only one in use, points";

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    headset.SetHandInputs(kLeft, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(PointerHand(), "0") << "and a pull on the other hand moves it there, as ever";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(PointerHand(), "1") << "either hand";

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(PointerHand(), "0") << "back in the world the Tool Hand holds the lance again and the other hand points";
}

// ---- Nothing changes when the tool is not in a tracked Tool Hand ------------------------------------------------------

TEST_F(VirtualGamepadHandToolsTriggerTest, WithAnUnsupportedHandToolTheTriggerIsThePadsAndThePointerHandRulesAreAsBefore) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(PointerHand(), "1") << "the right hand, the only one in use, points as with nothing in hand";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255) << "the trigger is the pad's";
    EXPECT_EQ(UseCalls(), 0) << "and the other mod's use handler is not called";
}

TEST_F(VirtualGamepadHandToolsTriggerTest, WithVrHandToolsOffTheTriggerIsThePadsAndThePointerHandRulesAreAsBefore) {
    StartWithLanceInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(1);

    EXPECT_EQ(PointerHand(), "1");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    EXPECT_FALSE(Spraying());
    EXPECT_EQ(UseCalls(), 0);
}

TEST_F(VirtualGamepadHandToolsTriggerTest, WithTheToolHandSetDownTheTriggerIsThePadsAndItCanBeThePointerHandAgain) {
    StartWithLanceInTheRightHand();
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    ASSERT_EQ(PointerHand(), "0");

    // The right hand lies still for the in-use timeout while the left one is moved.
    for (int step = 0; step < 4; ++step) {
        AdvanceGameTime(1000);
        MOVE_HAND(kLeft, -0.25f + 0.05f * static_cast<float>(step + 1), -0.10f, -0.30f);
    }
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false") << "set down";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    EXPECT_FALSE(Spraying());

    // Picked up again it holds the lance and is not the Pointer Hand.
    headset.SetHandInputs(kRight, Trigger(0.0f));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_EQ(PointerHand(), "0");
}

