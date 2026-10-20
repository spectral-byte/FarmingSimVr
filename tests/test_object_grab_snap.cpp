// Object Grab, the hold snaps (ADR-0033, spec #156, ticket #162), seen from outside: the real Companion Mod runs against the real VRBridge over
// FakeTrackedHeadset, with the game stood in by Lua stubs (the game's empty hands and loose objects in object_grab_test_support.h). Without
// super strength, yanking a heavy Held Object breaks the game's joint: the game calls onHeldItemJointBroken with the joint's index, and the
// hands let go. A test fires that report (StubHandTool.breakJoint) and asserts the haptic pulses the hand got, what the Virtual Gamepad
// reads for the grip, and what the game was called with. A drop by the game itself (dropHeldItem, which reports no joint) stays clean.
// Prior art: test_object_grab_carry.cpp (the carry and the game's own drop) and test_hand_tools_haptics.cpp (pulses).

#include "object_grab_test_support.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace hand_tools_test;
using namespace object_grab_test;

constexpr float kHandX = 0.4f, kHandY = -0.2f, kHandZ = -0.3f;
constexpr const char* kBaleAhead = "_G.bale = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.05, { name = 'bale' })";

class VirtualGamepadObjectGrabSnapTest : public VirtualGamepadObjectGrabTest {
protected:
    // The right hand pointing at the bale and squeezing: the bale is its Held Object.
    void StartCarrying(const StartOptions& startOptions = StartOptions()) {
        ASSERT_NO_FATAL_FAILURE(Start(startOptions));
        ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        Squeeze(kRight, 1.0f);
        FRAMES(3);
        ASSERT_EQ(PickUps(), 1) << "the grab";
    }

    // The pulses that reached a hand after the first `from` ones.
    std::vector<FakeOpenXrControllers::HapticPulse> PulsesAfter(size_t from, uint32_t hand) const {
        std::vector<FakeOpenXrControllers::HapticPulse> result;
        const auto& all = headset.HapticPulses();
        for (size_t i = from; i < all.size(); ++i) {
            if (all[i].hand == static_cast<int>(hand)) result.push_back(all[i]);
        }
        return result;
    }
    size_t PulseCount() const { return headset.HapticPulses().size(); }
    // The sharp ones among them (the grab is 0.6 and the ray landing on an object 0.25).
    int SharpPulsesAfter(size_t from) const {
        int count = 0;
        const auto& all = headset.HapticPulses();
        for (size_t i = from; i < all.size(); ++i) {
            if (all[i].amplitude >= 0.9f) ++count;
        }
        return count;
    }
};

}  // namespace

TEST_F(VirtualGamepadObjectGrabSnapTest, AJointBreakWhileCarryingGivesTheCarryingHandOneSharpPulse) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    const size_t before = PulseCount();
    float grabStrength = 0.0f;  // the strongest felt so far: the grab
    for (const auto& pulse : headset.HapticPulses()) grabStrength = std::max(grabStrength, pulse.amplitude);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.breakJoint(hands)", "=snap"), 0);
    FRAMES(10);
    EXPECT_EQ(SharpPulsesAfter(before), 1) << "one sharp pulse, however many frames follow";
    for (const auto& pulse : PulsesAfter(before, kRight)) {
        if (pulse.amplitude >= 0.9f) {
            EXPECT_GT(pulse.amplitude, grabStrength) << "sharper than the one that told of the grab";
            EXPECT_GE(static_cast<double>(pulse.duration) / 1.0e6, 80.0) << "and long enough to feel";
        }
    }
    EXPECT_EQ(PulsesAfter(before, kLeft).size(), 0u) << "the other hand feels nothing";
    EXPECT_EQ(Drops(), 0) << "the game broke the hold; Object Grab drops nothing";
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode)"), "nil");
    EXPECT_EQ(EvalLua(L, "tostring(#StubHandTool.breaks)"), "1") << "and the game's own report ran";
}

TEST_F(VirtualGamepadObjectGrabSnapTest, ASnapWithTheGripStillHeldDoesNotGrabAgainUntilItIsLetGoAndSqueezedAgain) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.breakJoint(hands)", "=snap"), 0);
    FRAMES(10);
    EXPECT_EQ(PickUps(), 1) << "the grip is still held and the ray is still on the bale";
    EXPECT_FALSE(PadGrip(kRight)) << "and what the carry took stays off the pad until it is let go";

    Squeeze(kRight, 0.0f);
    FRAMES(3);
    EXPECT_EQ(PickUps(), 1);
    EXPECT_EQ(Drops(), 0);

    Squeeze(kRight, 1.0f);
    FRAMES(2);
    EXPECT_EQ(PickUps(), 2) << "squeezed again, it grabs";
}

TEST_F(VirtualGamepadObjectGrabSnapTest, ASnapOfTheGripLetGoInTheSameBreathStillGivesItsPulseOnce) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    const size_t before = PulseCount();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.breakJoint(hands)", "=snap"), 0);
    Squeeze(kRight, 0.0f);
    FRAMES(5);
    EXPECT_EQ(SharpPulsesAfter(before), 1);
    EXPECT_EQ(Drops(), 0) << "the object is gone from the hands; there is nothing to drop";
}

TEST_F(VirtualGamepadObjectGrabSnapTest, TheGamesOwnDropAndThrowEndTheCarryWithoutAPulse) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    const size_t before = PulseCount();
    ASSERT_EQ(RunLuauChunk(L, "hands:dropHeldItem()", "=gameDrops"), 0);  // getting into a vehicle, taking a Hand Tool: no joint index
    FRAMES(5);
    EXPECT_EQ(SharpPulsesAfter(before), 0) << "a drop is not a snap";

    Squeeze(kRight, 0.0f);
    FRAMES(2);
    Squeeze(kRight, 1.0f);
    FRAMES(2);
    ASSERT_EQ(PickUps(), 2);
    const size_t again = PulseCount();
    ASSERT_EQ(RunLuauChunk(L, "hands:throwHeldItemWithForceVector(0, 0.2, 1, 0.5)", "=gameThrows"), 0);
    FRAMES(5);
    EXPECT_EQ(SharpPulsesAfter(again), 0) << "nor is a throw by the game";
}

TEST_F(VirtualGamepadObjectGrabSnapTest, ABreakOfAnObjectTheGamesOwnButtonCarriesIsLeftToTheGame) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, kBaleAhead, "=bale"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressPickUp(hands)", "=gamesButton"), 0);
    ASSERT_EQ(PickUps(), 1);
    FRAMES(2);
    const size_t before = PulseCount();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.breakJoint(hands)", "=snap"), 0);
    FRAMES(5);
    EXPECT_EQ(SharpPulsesAfter(before), 0) << "the hand did not hold it";
    EXPECT_EQ(EvalLua(L, "tostring(#StubHandTool.breaks)"), "1");
}

TEST_F(VirtualGamepadObjectGrabSnapTest, WithSuperStrengthTheJointDoesNotBreakAndTheCarryGoesOnWhateverTheHandDoes) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.superStrength(hands, true)", "=superStrength"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.pallet = StubHandTool.looseObject(-0.4, -0.2, 1.5, 0.8, { name = 'pallet' })", "=pallet"), 0);
    POINT_HAND(kRight, kHandX, kHandY, kHandZ);
    Squeeze(kRight, 1.0f);
    FRAMES(3);
    ASSERT_EQ(PickUps(), 1);
    const size_t before = PulseCount();

    for (int i = 0; i < 6; ++i) {  // yanked about
        headset.SetHandPointing(kRight, HandAt(i % 2 == 0 ? 0.9f : -0.7f, 0.5f, -0.6f, 0.8f, 1.4f));
        FRAMES(2);
    }
    EXPECT_EQ(SharpPulsesAfter(before), 0) << "the game makes the joint unbreakable: nothing is felt";
    EXPECT_EQ(Drops(), 0);
    EXPECT_EQ(EvalLua(L, "tostring(hands.spec_hands.heldItemNode == pallet.node)"), "true");
}

// ---- The game's report, as the wraps see it -----------------------------------------------------------------------------------

TEST_F(CompanionModTest, AJointBreakReportThatIsMissingOrReshapedTurnsObjectGrabOffWithOneWarningAndPickingUpIsTheGames) {
    for (const char* shape : { "HandToolHands.onHeldItemJointBroken = nil", "HandToolHands.onHeldItemJointBroken = function() end" }) {
        SCOPED_TRACE(shape);
        s_capturedLogs.clear();
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        FakeTrackedHeadset headset;
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        StartOptions options;
        const std::string scene = std::string("_G.foot = StubHandTool.onFoot({}); ") + shape;
        options.scene = scene.c_str();
        ASSERT_NO_FATAL_FAILURE(StartObjectGrabMod(L, bridge, options));
        SetGameTime(10000);

        POINT_HAND(kRight, kHandX, kHandY, kHandZ);
        FRAMES(3);
        EXPECT_EQ(CountLogLines("[Warning] Object Grab disabled"), 1) << "once, however many frames";
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);
        EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the head's ray: the hand does not aim";
        EXPECT_NE(EvalLua(L, "VRCompanion:getObjectGrabDiagnostics()").find("gave up"), std::string::npos);
    }
}

TEST_F(VirtualGamepadObjectGrabSnapTest, UnloadingTheMapGivesTheGamesJointBreakReportBack) {
    ASSERT_NO_FATAL_FAILURE(StartCarrying());
    ASSERT_EQ(EvalLua(L, "tostring(hands.onHeldItemJointBroken == StubHandTool.handsRegistered.onHeldItemJointBroken)"), "false") << "wrapped";
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "tostring(hands.onHeldItemJointBroken == StubHandTool.handsRegistered.onHeldItemJointBroken)"), "true");
    EXPECT_EQ(EvalLua(L, "tostring(HandToolHands.onHeldItemJointBroken == StubHandTool.handsRegistered.onHeldItemJointBroken)"), "true");
}
