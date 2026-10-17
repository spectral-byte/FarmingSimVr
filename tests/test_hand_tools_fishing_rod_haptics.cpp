// Hand Tools, the fishing rod's haptics on the Tool Hand (ADR-0031), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h, StubHandTool: a player on foot
// and the Highlands Fishing DLC's rod, kStubHandToolFishingRod, whose bite has a hook window of 880 ms of updates and whose
// rodBendFactor while reeling is the fish's pull). A test moves the fake Motion Controllers, drives the rod and frames the way the
// game would and asserts what the player would feel: which haptic pulses reached which fake Motion Controller
// (headset.HapticPulses()), how strong and long they were, and when they stop. Never the mod's own state.
// Prior art: test_hand_tools_haptics.cpp (the other tools' buzz) and test_hand_tools_fishing_rod.cpp (the rod in the Tool Hand).

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace hand_tools_test;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsFishingRodHapticsTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    // The mod started with a player on foot in a game with the Highlands Fishing DLC, the Virtual Gamepad on and the game clock set
    // (the in-use timeout is on it).
    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = std::string(kXmlStubs) + FishingRodStubs("ok");
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // The rod held on foot in the Tool Hand `hand`, both Motion Controllers picked up (the other hand is up too, trigger let go).
    void HoldTheRodIn(uint32_t hand) {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
        ASSERT_EQ(RunLuauChunk(L, hand == kLeft ? "runCommand('vrToolHand', 'left')" : "runCommand('vrToolHand', 'right')", "=toolHand"), 0);
        const uint32_t other = hand == kRight ? kLeft : kRight;
        const float side = hand == kRight ? 1.0f : -1.0f;
        PICK_UP(other, -0.25f * side, -0.10f, -0.30f);
        PICK_UP(hand, 0.25f * side, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(EvalLua(L, std::string("VRCompanion:isHandHoldingHandTool(") + std::to_string(hand) + ")"), "true") << Dump();
    }

    // A full cast thrown with the Tool Hand's trigger (held to charge, let go to throw): the rod waits for a bite with about 22 m
    // of line out, which a held reel takes about two seconds to bring in.
    void Cast(uint32_t hand) {
        headset.SetHandInputs(hand, Trigger(1.0f));
        FRAMES(90);
        headset.SetHandInputs(hand, Trigger(0.0f));
        FRAMES(1);
        ASSERT_EQ(RodState(), "waiting") << Dump();
    }

    // The rod in the Tool Hand `hand`, its line out and waiting for a bite.
    void WaitForABiteIn(uint32_t hand) {
        ASSERT_NO_FATAL_FAILURE(HoldTheRodIn(hand));
        ASSERT_NO_FATAL_FAILURE(Cast(hand));
    }

    // The pulses so far to a hand, the strongest and the weakest of them. ForgetPulses starts counting afresh.
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

    // The longest run of frames (each the game's 16.7 ms) without a pulse to the hand over the next `frames` frames.
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

    std::string RodState() { return EvalLua(L, "StubHandTool.rodState(rod)"); }
    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }
};

}  // namespace

// ---- The bite -----------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, ABiteMakesTheToolHandPulseRepeatedlyForAsLongAsTheHookWindowLastsThenStop) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);

    // Frame by frame through the hook window (880 ms of the rod's updates), noting the frames that brought a pulse.
    std::vector<int> pulseFrames;
    int frames = 0;
    while (RodState() == "bite" && frames < 200) {
        const size_t before = PulsesTo(kRight);
        FRAMES(1);
        ++frames;
        if (PulsesTo(kRight) > before) pulseFrames.push_back(frames);
    }
    ASSERT_EQ(RodState(), "waiting") << "not hooked in the window: the fish got away";
    ASSERT_GE(pulseFrames.size(), 3u) << "a pulse, repeated";
    EXPECT_LE(pulseFrames.front(), 1) << "at once when the fish bites";
    EXPECT_GE(pulseFrames.back(), frames - 15) << "and on until the window is nearly over (" << frames << " frames)";
    EXPECT_EQ(PulsesTo(kLeft), 0u) << "the other hand feels nothing";

    ForgetPulses();
    FRAMES(30);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the window over, the rod waits again: silent";
}

// A bite is felt as a strong, short tap, again and again, with the hand quiet in between: not a buzz like the reeling's.
TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, TheBitesPulsesAreSharpAndApartNotABuzz) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    std::vector<int> pulseFrames;
    for (int frame = 1; frame <= 40; ++frame) {
        const size_t before = PulsesTo(kRight);
        FRAMES(1);
        if (PulsesTo(kRight) > before) pulseFrames.push_back(frame);
    }
    ASSERT_EQ(RodState(), "bite") << "still in the hook window";
    ASSERT_GE(pulseFrames.size(), 3u);
    EXPECT_GE(WeakestTo(kRight), 0.7f) << "strong";

    const double frameMs = 16.667;
    int shortestGap = 1000;
    for (size_t i = 1; i < pulseFrames.size(); ++i) shortestGap = std::min(shortestGap, pulseFrames[i] - pulseFrames[i - 1]);
    double longestMs = 0.0;
    for (const auto& pulse : headset.HapticPulses()) longestMs = std::max(longestMs, static_cast<double>(pulse.duration) / 1.0e6);
    EXPECT_LT(longestMs, (shortestGap - 1) * frameMs) << "each pulse is over well before the next: the hand is quiet in between";
}

// ---- Reeling --------------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, ReelingBuzzesTheToolHandMoreAsTheRodBendsMore) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setFishPull(rod, 0.2); StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));  // hooked, and the reel held
    FRAMES(2);
    ASSERT_EQ(RodState(), "reeling");
    ASSERT_EQ(EvalLua(L, "string.format('%.2f', select(3, VRCompanion:getFishingRodState()))"), "0.20") << "a light pull";
    ForgetPulses();
    const int silence = LongestSilenceTo(kRight, 12);
    EXPECT_LE(silence, 3) << "a buzz: no long gaps";
    ASSERT_GE(PulsesTo(kRight), 3u) << "reeling buzzes the Tool Hand";
    const float light = StrongestTo(kRight);
    EXPECT_GT(WeakestTo(kRight), 0.0f);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setFishPull(rod, 0.8)", "=pullHarder"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "string.format('%.2f', select(3, VRCompanion:getFishingRodState()))"), "0.80") << "the rod bent further";
    ForgetPulses();
    FRAMES(10);
    ASSERT_GE(PulsesTo(kRight), 3u);
    EXPECT_GT(WeakestTo(kRight), light) << "every pulse stronger than at the light pull";
    EXPECT_LE(StrongestTo(kRight), 1.0f);
    const float hard = WeakestTo(kRight);

    headset.SetHandInputs(kRight, Trigger(0.0f));  // the reel let go: the rod bends a third as much
    FRAMES(2);
    ASSERT_EQ(RodState(), "reeling");
    ForgetPulses();
    FRAMES(10);
    EXPECT_GE(PulsesTo(kRight), 3u) << "still reeling, still buzzing";
    EXPECT_GT(WeakestTo(kRight), 0.0f);
    EXPECT_LT(StrongestTo(kRight), hard) << "less, as the rod bends less";
    EXPECT_EQ(PulsesTo(kLeft), 0u) << "the other hand feels nothing";
}

TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, LandingTheFishEndsTheBuzz) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(RodState(), "reeling");
    ForgetPulses();
    FRAMES(5);
    ASSERT_GE(PulsesTo(kRight), 1u) << "buzzing while it reels";
    for (int frame = 0; frame < 300 && RodState() == "reeling"; ++frame) FRAMES(1);
    ASSERT_EQ(RodState(), "caught") << "the held reel brought the fish in";
    FRAMES(1);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "caught, the line in: silent";
}

// ---- Silence -------------------------------------------------------------------------------------------------------------------
// A buzz always means a fish: the rod that is only held, charged or waiting is silent.

TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, TheRodIsSilentWhileIdleChargingAndWaiting) {
    ASSERT_NO_FATAL_FAILURE(HoldTheRodIn(kRight));
    ForgetPulses();
    FRAMES(10);
    ASSERT_EQ(RodState(), "idle");
    EXPECT_EQ(PulsesTo(kRight), 0u) << "idle";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(30);
    ASSERT_EQ(RodState(), "charging");
    EXPECT_EQ(PulsesTo(kRight), 0u) << "charging a cast with the trigger held";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(RodState(), "waiting");
    for (int second = 0; second < 5; ++second) {
        AdvanceGameTime(1000);
        FRAMES(10);
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << "the rod still in the still hand: " << Dump();
    EXPECT_EQ(PulsesTo(kRight), 0u) << "waiting for a bite";
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

// A DLC update that hides the rod's state must not buzz the hand: unknown is silent, whatever the rod is really doing.
TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, AnUnreadableStateBuzzesNothingEvenInABiteOrWhileReeling) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod); StubHandTool.rodSpec(rod).rodBendFactor = 'bent'", "=unreadableBite"), 0);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getFishingRodState()"), "unknown");
    ForgetPulses();
    FRAMES(10);
    ASSERT_EQ(RodState(), "bite");
    EXPECT_EQ(PulsesTo(kRight), 0u) << "a bite the mod cannot read";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubHandTool.setRodState(rod, 'reeling')
        StubHandTool.rodSpec(rod).fishingLine = { isInitialized = 'yes' }
    )luau", "=unreadableReeling"), 0);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:getFishingRodState()"), "unknown");
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "reeling the mod cannot read";
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

// ---- Only the Tool Hand, only while the rod is in it, only the local player's rod -----------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, TheLeftToolHandIsTheOneThatFeelsTheBiteAndTheReelingNotTheRightOne) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kLeft));
    ForgetPulses();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(20);
    EXPECT_GE(PulsesTo(kLeft), 2u) << "the bite";
    EXPECT_EQ(PulsesTo(kRight), 0u);

    headset.SetHandInputs(kLeft, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(RodState(), "reeling");
    ForgetPulses();
    FRAMES(10);
    EXPECT_GE(PulsesTo(kLeft), 3u) << "the reeling";
    EXPECT_EQ(PulsesTo(kRight), 0u);
}

TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, TheRodOutOfTheToolHandBuzzesNothing) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.biteRod(rod)", "=bite"), 0);
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(RodState(), "reeling");

    headset.SetHandTracked(kRight, false);
    FRAMES(3);  // a pulse already requested finishes by itself
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "untracked: the rod is the game's again";
    EXPECT_EQ(PulsesTo(kLeft), 0u);

    headset.SetHandTracked(kRight, true);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=handToolsOff"), 0);
    FRAMES(3);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "Hand Tools off";
    EXPECT_EQ(PulsesTo(kLeft), 0u);
}

// The game attaches a Hand Tool of every player in the session through the same function: another player's rod biting or
// reeling is theirs to feel.
TEST_F(VirtualGamepadHandToolsFishingRodHapticsTest, AnotherPlayersRodBuzzesNothing) {
    ASSERT_NO_FATAL_FAILURE(WaitForABiteIn(kRight));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        _G.remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteRod = StubHandTool.fishingRod(remote)
        remoteRod.isHeld = true
        remoteRod.carryingPlayer = remote
        remoteRod:attachTool()
        StubHandTool.setRodState(remoteRod, 'bite')
    )luau", "=remoteBite"), 0);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "another player's bite";
    EXPECT_EQ(PulsesTo(kLeft), 0u);

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubHandTool.setRodState(remoteRod, 'reeling')
        StubHandTool.rodSpec(remoteRod).rodBendFactor = 0.9
    )luau", "=remoteReeling"), 0);
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "another player's reeling";
    EXPECT_EQ(PulsesTo(kLeft), 0u);
    EXPECT_EQ(RodState(), "waiting") << "the local player's rod still waits";
}
