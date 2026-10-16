// Hand Tools, nudging how a tool sits in the Tool Hand while the game runs (vrHandToolOffset), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs
// (companion_mod_stubs.h, StubHandTool). The pressure washer lance is the tool. The test runs the console command and
// asserts what the player would see: where the tool's root and a point 1 m along it are, measured in the Virtual Hand's frame,
// and what the command answers.

#include "hand_tools_test_support.h"
#include <string>
#include <vector>

namespace {

using namespace hand_tools_test;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsOffsetTuningTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The lance in the right hand, with a marker node 1 m along its +Z (the way its nozzle points).
    void StartWithTheLanceInTheRightHand() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        StartOptions options;
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        SetGameTime(10000);
        ASSERT_NO_FATAL_FAILURE(HoldLance(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(EvalLua(L, "isBelow(lance.rootNode, handHolder('right'))"), "true");
        ASSERT_EQ(RunLuauChunk(L, "_G.marker = StubScene.newNode('marker', lance.rootNode, 0, 0, 1, 0, 0, 0)", "=marker"), 0);
    }

    std::string Command(const std::string& arguments) { return EvalLua(L, "runCommand('vrHandToolOffset'" + arguments + ")"); }

    // Where the root and the marker are in the right Virtual Hand's frame: x, y, z of the root, then of the marker.
    std::vector<double> Placement() {
        std::vector<double> values;
        for (const char* node : { "lance.rootNode", "marker" }) {
            for (const char* axis : { "1", "2", "3" }) {
                values.push_back(Number(L, std::string("select(") + axis + ", offsetInHolder(" + node + ", 'right'))"));
            }
        }
        return values;
    }
};

}  // namespace

TEST_F(VirtualGamepadHandToolsOffsetTuningTest, WithoutArgumentsTheCommandListsEveryToolsOffsetAndHowToChangeIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheLanceInTheRightHand());
    const std::string text = Command("");
    for (const char* kind : { "lance", "flashlight", "chainsaw" }) {
        EXPECT_NE(text.find(kind), std::string::npos) << kind << " is listed: " << text;
    }
    EXPECT_NE(text.find("nudge"), std::string::npos) << text;
    EXPECT_NE(text.find("turn"), std::string::npos) << text;
    EXPECT_NE(text.find("reset"), std::string::npos) << text;
}

TEST_F(VirtualGamepadHandToolsOffsetTuningTest, NudgeMovesTheToolInTheHandAtOnceByTheMetresGivenAndKeepsItsDirection) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheLanceInTheRightHand());
    const std::vector<double> before = Placement();

    const std::string answer = Command(", 'lance', 'nudge', '0.01', '-0.02', '0.03'");
    EXPECT_EQ(answer.find("Usage"), std::string::npos) << answer;
    const std::vector<double> after = Placement();
    ASSERT_EQ(before.size(), 6u);
    ASSERT_EQ(after.size(), 6u);
    EXPECT_NEAR(after[0] - before[0], 0.01, 1e-4);
    EXPECT_NEAR(after[1] - before[1], -0.02, 1e-4);
    EXPECT_NEAR(after[2] - before[2], 0.03, 1e-4);
    // The marker moved with the root by the same amount: the tool points the way it did.
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_NEAR((after[3 + axis] - after[axis]), (before[3 + axis] - before[axis]), 1e-4);
    }
    EXPECT_NE(answer.find("0.01"), std::string::npos) << "the answer carries the new numbers to write down: " << answer;
}

TEST_F(VirtualGamepadHandToolsOffsetTuningTest, TurnRollsTheToolInTheHandAboutTheHandsAxesInDegreesAndKeepsItsPlace) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheLanceInTheRightHand());
    const std::vector<double> before = Placement();
    // The lance points along the fingertips (-Y of the hand's frame).
    EXPECT_NEAR(before[4] - before[1], -1.0, 1e-3);

    const std::string answer = Command(", 'lance', 'turn', '90', '0', '0'");
    EXPECT_EQ(answer.find("Usage"), std::string::npos) << answer;
    const std::vector<double> after = Placement();
    EXPECT_NEAR(after[0], before[0], 1e-4) << "the root stays where it was in the hand";
    EXPECT_NEAR(after[1], before[1], 1e-4);
    EXPECT_NEAR(after[2], before[2], 1e-4);
    // A quarter turn about the hand's X axis takes the fingertip direction (-Y) onto -Z (toward the thumb) or +Z.
    EXPECT_NEAR(after[4] - after[1], 0.0, 1e-3);
    EXPECT_NEAR(std::abs(after[5] - after[2]), 1.0, 1e-3);
    EXPECT_NEAR(after[3] - after[0], 0.0, 1e-3);
}

TEST_F(VirtualGamepadHandToolsOffsetTuningTest, ResetPutsTheToolBackWhereTheModHasIt) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheLanceInTheRightHand());
    const std::vector<double> before = Placement();
    Command(", 'lance', 'nudge', '0.05', '0.05', '0.05'");
    Command(", 'lance', 'turn', '30', '40', '50'");
    ASSERT_GT(std::abs(Placement()[0] - before[0]), 0.01);

    const std::string answer = Command(", 'lance', 'reset'");
    EXPECT_EQ(answer.find("Usage"), std::string::npos) << answer;
    const std::vector<double> after = Placement();
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_NEAR(after[i], before[i], 1e-4) << "value " << i;
    }
}

TEST_F(VirtualGamepadHandToolsOffsetTuningTest, ANudgeStaysForTheNextTimeTheToolIsTakenIntoTheHandAndOnlyForThatTool) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheLanceInTheRightHand());
    const std::vector<double> before = Placement();
    Command(", 'lance', 'nudge', '0.04', '0', '0'");

    // Set the controller down (the lance goes back to the camera), then pick it up again.
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); StubHandTool.hold(lance)", "=redo"), 0);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "isBelow(lance.rootNode, handHolder('right'))"), "true");
    EXPECT_NEAR(Placement()[0] - before[0], 0.04, 1e-4);

    EXPECT_NEAR(Number(L, "VRCompanion.HAND_TOOL_HANDLE_OFFSETS.chainsaw.position.x"), 0.056, 1e-6) << "the chainsaw's offset is not touched";
}

TEST_F(VirtualGamepadHandToolsOffsetTuningTest, ABadToolActionOrNumberAnswersWithUsageAndChangesNothing) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheLanceInTheRightHand());
    const std::vector<double> before = Placement();
    for (const char* arguments : { ", 'othermodtool', 'nudge', '0', '0', '0'", ", 'lance', 'wiggle', '0', '0', '0'", ", 'lance', 'nudge', 'a', '0', '0'",
                                   ", 'lance', 'nudge', '0.1'", ", 'lance', 'turn'", ", 'lance', 'nudge', '9', '0', '0'" }) {
        const std::string answer = Command(arguments);
        EXPECT_NE(answer.find("Usage"), std::string::npos) << arguments << " -> " << answer;
    }
    const std::vector<double> after = Placement();
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_NEAR(after[i], before[i], 1e-6) << "value " << i;
    }
}
