// Hand Tools, haptics on the Tool Hand (ADR-0029), seen from outside: the real Companion Mod runs against
// the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h, StubHandTool: the
// lance, the chainsaw with its cut-state machine and trees, the flashlight). A test moves the fake Motion Controllers, drives
// frames the way the game would and asserts what the player would feel: which haptic pulses reached which fake Motion
// Controller (headset.HapticPulses()), how strong they were, and that they stop. Never the mod's own state.
// Prior art: test_haptics.cpp (pulses on the fake controllers), test_virtual_gamepad_rumble.cpp (the Companion Mod's pulses win
// on a hand over the game's rumble: nothing here changes that, the pulses go the same way), and the other test_hand_tools_*.cpp
// files (the lance, the chainsaw and the flashlight in the Tool Hand).

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <algorithm>
#include <string>

namespace {

using namespace hand_tools_test;

// Lua helpers for the chainsaw: a trunk the bar is against.
constexpr const char* kHapticsHelpers = R"luau(
    function _G.barRay() return { StubHandTool.barRay(saw) } end
    function _G.plantTreeAtTheBar() _G.barTree = StubHandTool.treeAcross(barRay(), 0.6, 0.15) end
    function _G.sawState()
        local s = saw.spec_chainsaw.currentCutState
        if s == ChainsawCutState.CUTTING then return "cutting" end
        if s == ChainsawCutState.DELIMBING then return "delimbing" end
        return "idle"
    end
)luau";

// A hand held so that the grip frame's fingers (and the bar along them) point level, the way a chainsaw is held.
constexpr float kBarForward = 1.4f;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsHapticsTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;
    std::string gameStubs;

    // The mod started with a player on foot who has the game's chainsaw and flashlight classes, the Virtual Gamepad on and the
    // game clock set (the in-use timeout is on it).
    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = std::string(kXmlStubs) + ChainsawStubs();
        gameStubs = FlashlightGame();
        StartOptions options;
        options.extraStubs = stubs.c_str();
        options.gameStubs = gameStubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kHapticsHelpers, "=hapticsHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // Both the aim and the grip pose of the hand set (the Virtual Hand follows the grip pose).
    void AimHand(uint32_t hand, float x, float y, float z, float pitch, float yaw) {
        headset.SetHandPose(hand, HandAt(x, y, z, pitch, yaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, pitch, yaw));
    }

    // The tool is picked up into the Tool Hand `hand` (the other hand is up too, with the trigger let go).
    void PickUpToolHand(uint32_t hand) {
        const float side = hand == kRight ? 1.0f : -1.0f;
        PICK_UP(hand, 0.25f * side, -0.35f, -0.30f);
        AimHand(hand, 0.25f * side, -0.35f, -0.30f, kBarForward, 0.9f * side);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, std::string("VRCompanion:isHandHoldingHandTool(") + std::to_string(hand) + ")"), "true")
            << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    void HoldTheChainsawIn(uint32_t hand) {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        ASSERT_EQ(RunLuauChunk(L, hand == kLeft ? "runCommand('vrToolHand', 'left')" : "runCommand('vrToolHand', 'right')", "=toolHand"), 0);
        ASSERT_NO_FATAL_FAILURE(PickUpToolHand(hand));
    }

    void HoldTheLanceIn(uint32_t hand) {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldLance(L));
        ASSERT_EQ(RunLuauChunk(L, hand == kLeft ? "runCommand('vrToolHand', 'left')" : "runCommand('vrToolHand', 'right')", "=toolHand"), 0);
        ASSERT_NO_FATAL_FAILURE(PickUpToolHand(hand));
    }

    void HoldTheFlashlightIn(uint32_t hand) {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldFlashlight(L));
        ASSERT_EQ(RunLuauChunk(L, hand == kLeft ? "runCommand('vrToolHand', 'left')" : "runCommand('vrToolHand', 'right')", "=toolHand"), 0);
        ASSERT_NO_FATAL_FAILURE(PickUpToolHand(hand));
    }

    // The pulses so far to a hand, and the strongest of them. ForgetPulses starts counting afresh.
    size_t PulsesTo(int hand) const {
        size_t count = 0;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) ++count;
        }
        return count;
    }
    float StrongestTo(int hand) const {
        float strongest = 0.0f;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) strongest = std::max(strongest, pulse.amplitude);
        }
        return strongest;
    }
    float WeakestTo(int hand) const {
        float weakest = 1.0f;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) weakest = std::min(weakest, pulse.amplitude);
        }
        return weakest;
    }
    void ForgetPulses() { headset.runtime->controllers.hapticPulses.clear(); }

    // Whether the hand was pulsed on every stretch of `window` frames over the next `frames` frames: a buzz that goes on.
    // Counts the frames (each the game's 16.7 ms) that pass without a pulse in the longest gap.
    int LongestSilenceTo(int hand, int frames) {
        int longest = 0, current = 0;
        for (int i = 0; i < frames; ++i) {
            const size_t before = PulsesTo(hand);
            FRAMES(1);
            if (PulsesTo(hand) > before) {
                current = 0;
            } else {
                longest = std::max(longest, ++current);
            }
        }
        return longest;
    }

    bool Spraying() { return EvalLua(L, "lance.spec_highPressureWasherLance.isActivated") == "true"; }
    std::string SawState() { return EvalLua(L, "sawState()"); }
    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }
};

}  // namespace

// ---- The chainsaw ---------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsHapticsTest, TheChainsawInTheToolHandBuzzesItLightlyWhileItIdlesAndWhileTheMotorRevs) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    ForgetPulses();
    FRAMES(10);
    ASSERT_EQ(SawState(), "idle");
    EXPECT_GE(PulsesTo(kRight), 3u) << "an idling saw buzzes the Tool Hand";
    EXPECT_GT(WeakestTo(kRight), 0.0f);
    EXPECT_LT(StrongestTo(kRight), 0.5f) << "lightly";
    EXPECT_EQ(PulsesTo(kLeft), 0u) << "and only that hand";
    const float idling = StrongestTo(kRight);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(SawState(), "delimbing") << "no tree for the bar: the motor revs";
    ForgetPulses();
    FRAMES(10);
    EXPECT_GE(PulsesTo(kRight), 3u) << "the revving motor buzzes it too";
    EXPECT_LT(StrongestTo(kRight), 0.5f) << "lightly";
    EXPECT_FLOAT_EQ(StrongestTo(kRight), idling) << "as lightly as the idle";
    EXPECT_EQ(PulsesTo(kLeft), 0u);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(SawState(), "idle");
    EXPECT_GE(PulsesTo(kRight), 3u) << "and letting go of the trigger leaves the idle buzz";
}

TEST_F(VirtualGamepadHandToolsHapticsTest, TheChainsawsBuzzEndsWhenTheToolHandIsUntrackedOrTheChainsawIsPutAway) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    FRAMES(5);
    ASSERT_GE(PulsesTo(kRight), 1u);

    headset.SetHandTracked(kRight, false);
    FRAMES(3);  // a pulse already requested finishes by itself
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "an untracked Tool Hand is not pulsed";

    headset.SetHandTracked(kRight, true);
    FRAMES(5);
    ASSERT_GE(PulsesTo(kRight), 1u) << "and it buzzes again once the hand is back";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(saw)", "=drop"), 0);
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "no chainsaw in the hand, no buzz";
}

TEST_F(VirtualGamepadHandToolsHapticsTest, TheBuzzIsContinuousNotASinglePulse) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(SawState(), "delimbing");
    ForgetPulses();
    // Every pulse lasts a little longer than the gap to the next, so that the hand never goes quiet in between.
    const int silence = LongestSilenceTo(kRight, 30);
    EXPECT_LE(silence, 3) << "frames of 16.7 ms without a pulse in the longest gap";
    ASSERT_GE(PulsesTo(kRight), 5u);
    const double frameMs = 16.667;
    const double durationMs = static_cast<double>(headset.HapticPulses().front().duration) / 1.0e6;
    EXPECT_GE(durationMs, (silence + 1) * frameMs - 1.0) << "a pulse covers the gap to the next one";
}

TEST_F(VirtualGamepadHandToolsHapticsTest, CuttingBuzzesTheToolHandHarderThanRevvingDoes) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(SawState(), "delimbing");
    ForgetPulses();
    FRAMES(6);
    const float revving = StrongestTo(kRight);
    ASSERT_GT(revving, 0.0f);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(4);
    ASSERT_EQ(RunLuauChunk(L, "plantTreeAtTheBar()", "=plantTree"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(SawState(), "cutting") << Dump();
    ForgetPulses();
    FRAMES(6);
    ASSERT_EQ(SawState(), "cutting");
    EXPECT_GT(WeakestTo(kRight), revving) << "while it cuts every pulse is stronger than the revving";
    EXPECT_LE(StrongestTo(kRight), 1.0f);
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, TheCutEndingGoesBackToTheLightBuzzOfTheIdlingSawWhenTheTriggerIsLetGo) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "plantTreeAtTheBar()", "=plantTree"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(SawState(), "cutting");
    ForgetPulses();
    FRAMES(4);
    const float cutting = WeakestTo(kRight);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(4);
    ForgetPulses();
    FRAMES(8);
    EXPECT_EQ(SawState(), "idle");
    EXPECT_GE(PulsesTo(kRight), 1u) << "the saw idles in the hand: the light buzz";
    EXPECT_LT(StrongestTo(kRight), cutting) << "lighter than the cut was";
    EXPECT_GT(cutting, 0.0f);
}

// ---- The lance ------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsHapticsTest, TheLanceSprayingPulsesTheToolHandSteadilyAndItStopsWhenTheSprayDoes) {
    ASSERT_NO_FATAL_FAILURE(HoldTheLanceIn(kRight));
    ForgetPulses();
    FRAMES(5);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "a lance that is not spraying is silent";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(Spraying());
    ForgetPulses();
    const int silence = LongestSilenceTo(kRight, 30);
    EXPECT_GE(PulsesTo(kRight), 5u);
    EXPECT_LE(silence, 3) << "steady: no long gaps";
    EXPECT_GT(WeakestTo(kRight), 0.0f);
    // Steady means the same pulse each time.
    EXPECT_FLOAT_EQ(WeakestTo(kRight), StrongestTo(kRight));
    EXPECT_EQ(PulsesTo(kLeft), 0u) << "the other hand gets nothing";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    ASSERT_FALSE(Spraying());
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "they stop when spraying stops";
}

// ---- The flashlight -------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsHapticsTest, TheFlashlightSendsNoPulsesWhetherItIsSwitchedOnOrOffAndNeitherHandIsPulsed) {
    ASSERT_NO_FATAL_FAILURE(HoldTheFlashlightIn(kRight));
    ForgetPulses();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    ASSERT_EQ(EvalLua(L, "flashlight.spec_flashlight.isActive"), "true") << "the pull switched it on";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(5);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(5);
    EXPECT_EQ(PulsesTo(kRight), 0u);
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

// ---- Only the Tool Hand, only while the tool is in it ------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsHapticsTest, TheLeftToolHandIsTheOneThatBuzzesNotTheRightOne) {
    ASSERT_NO_FATAL_FAILURE(HoldTheLanceIn(kLeft));
    PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
    ForgetPulses();
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    ASSERT_TRUE(Spraying());
    ForgetPulses();
    FRAMES(10);
    EXPECT_GE(PulsesTo(kLeft), 3u);
    EXPECT_EQ(PulsesTo(kRight), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, TheOtherHandsOwnTriggerDoesNotBuzzAnythingWhileTheToolHandIsQuiet) {
    ASSERT_NO_FATAL_FAILURE(HoldTheLanceIn(kRight));
    PICK_UP(kLeft, -0.25f, -0.35f, -0.30f);
    ForgetPulses();
    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kLeft), 0u);
    EXPECT_EQ(PulsesTo(kRight), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, ASprayBeingStoppedWhenTheToolHandLosesTrackingStopsTheBuzz) {
    ASSERT_NO_FATAL_FAILURE(HoldTheLanceIn(kRight));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_TRUE(Spraying());
    ForgetPulses();
    FRAMES(4);
    ASSERT_GE(PulsesTo(kRight), 1u);

    headset.SetHandTracked(kRight, false);
    FRAMES(3);  // a pulse already requested finishes by itself
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "an untracked Tool Hand is not pulsed";
}

TEST_F(VirtualGamepadHandToolsHapticsTest, WithHandToolsOffTheToolHandIsNotBuzzed) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=handToolsOff"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u);
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, TurningHandToolsOffWhileTheChainsawBuzzesStopsTheBuzzAtOnce) {
    ASSERT_NO_FATAL_FAILURE(HoldTheChainsawIn(kRight));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(4);
    ASSERT_EQ(SawState(), "delimbing");
    ForgetPulses();
    FRAMES(4);
    ASSERT_GE(PulsesTo(kRight), 1u);

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=handToolsOff"), 0);
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, WithMotionControllersOffNothingBuzzes) {
    ASSERT_NO_FATAL_FAILURE(HoldTheLanceIn(kRight));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_TRUE(Spraying());
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=motionControllersOff"), 0);
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u);
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, AnUnsupportedToolInTheToolHandDoesNotBuzzIt) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldSprayCan(L));
    PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u);
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

TEST_F(VirtualGamepadHandToolsHapticsTest, TheLancePutAwayWhileItSpraysStopsTheBuzz) {
    ASSERT_NO_FATAL_FAILURE(HoldTheLanceIn(kRight));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_TRUE(Spraying());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance)", "=drop"), 0);
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "no tool in the hand, no buzz";
}
