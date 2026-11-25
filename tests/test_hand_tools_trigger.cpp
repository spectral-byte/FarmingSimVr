#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

class VirtualGamepadHandToolsTriggerTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void StartWithLanceInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldLance(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    int UseCalls() { return static_cast<int>(Number(L, "#StubHandTool.useCalls")); }
    double LastUseValue() { return Number(L, "StubHandTool.useCalls[#StubHandTool.useCalls].value"); }
    bool Spraying() { return EvalLua(L, "lance.spec_highPressureWasherLance.isActivated") == "true"; }
    void ForgetUseCalls() { ASSERT_EQ(RunLuauChunk(L, "StubHandTool.useCalls = {}", "=forgetUseCalls"), 0); }
    std::string PointerHand() { return EvalLua(L, "VRCompanion:getPointerHand()"); }
};

}

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

TEST_F(VirtualGamepadHandToolsTriggerTest, ATriggerAlreadyHeldWhenTheLanceComesIntoTheHandKeepsItsBindingAndDoesNotSprayUntilLetGoAndPulledAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(Pad().bRightTrigger, 255);

    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
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
    headset.SetHandInputs(kRight, Trigger(1.0f));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_FALSE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 255);
}

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

TEST_F(VirtualGamepadHandToolsTriggerTest, InTheWorldTheToolHandHoldingTheLanceIsNeverThePointerHandAndTheOtherHandIs) {
    StartWithLanceInTheRightHand();
    EXPECT_EQ(PointerHand(), "nil") << "the only hand in use holds the lance";

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    EXPECT_EQ(PointerHand(), "0") << "the other hand points at Click Points";

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

TEST_F(VirtualGamepadHandToolsTriggerTest, WhileAStereoDialogShowsTheToolHandsTriggerDoesNotUseTheLanceAndReachesThePad) {
    StartWithLanceInTheRightHand();
    ForgetUseCalls();

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        g_gui.visible = true
        g_gui.currentDialogName = "FishDialog"
        g_gui.dialogs = { { name = "FishDialog", target = {} } }
    )luau", "=openFishDialog"), 0);
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isStereoDialogShowing()"), "true");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << "the lance stays in the hand";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(UseCalls(), 0) << "the pull does not use the lance";
    EXPECT_FALSE(Spraying());
    EXPECT_EQ(Pad().bRightTrigger, 255) << "it reaches the dialog through the pad";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false; g_gui.currentDialogName = ''; g_gui.dialogs = {}", "=closeFishDialog"), 0);
    FRAMES(3);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_TRUE(Spraying()) << "and the trigger uses the lance again once the dialog has closed";
}

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

    for (int step = 0; step < 4; ++step) {
        AdvanceGameTime(1000);
        MOVE_HAND(kLeft, -0.25f + 0.05f * static_cast<float>(step + 1), -0.10f, -0.30f);
    }
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false") << "set down";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Pad().bRightTrigger, 255);
    EXPECT_FALSE(Spraying());

    headset.SetHandInputs(kRight, Trigger(0.0f));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    EXPECT_EQ(PointerHand(), "0");
}

