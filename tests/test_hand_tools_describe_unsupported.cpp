// Hand Tools, describing a Hand Tool the Companion Mod does not support (ADR-0030): while the local player holds one, the
// console command vrHandToolsDiagnostics also writes what the game gives that tool to the game log, so that a log from the
// headset is enough to design the tool's support. Seen from outside: the real Companion Mod runs against the real VRBridge
// over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h, StubHandTool), another mod's tool
// (StubHandTool.otherModTool) is the tool, and the test runs the console command as the game would and asserts what the
// developer reads in the log and what the command answers. Never the mod's own state.
// Prior art: test_hand_tools_offset_tuning.cpp (a console command and its log lines) and the diagnostics test in
// test_hand_tools_lance.cpp.

#include "hand_tools_test_support.h"
#include <string>
#include <vector>

namespace {

using namespace hand_tools_test;

// Every line the probe writes starts with this (after the mod's own [FS25_VR] tag).
constexpr const char* kProbeTag = "[FS25_VR] Hand Tool probe: ";

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsDescribeUnsupportedTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The mod started with a player on foot who holds another mod's tool (made with the Lua options table given).
    void StartHolding(const std::string& toolOptions = "{}") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
        SetGameTime(10000);
        ASSERT_EQ(RunLuauChunk(L, "_G.otherTool = StubHandTool.otherModTool(foot.player, " + toolOptions + "); StubHandTool.hold(otherTool)",
                               "=holdOtherModTool"), 0);
    }

    // Runs the command as the game would. The log lines are what the game logged during it; Probe() are the ones the probe wrote.
    std::string RunCommand() {
        s_capturedLogs.clear();
        return EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    }
    std::vector<std::string> Probe() {
        std::vector<std::string> lines;
        for (const auto& line : s_capturedLogs) {
            if (line.rfind(kProbeTag, 0) == 0) lines.push_back(line.substr(std::string(kProbeTag).size()));
        }
        return lines;
    }
    static bool AnyLineHas(const std::vector<std::string>& lines, const std::string& text) {
        for (const auto& line : lines) {
            if (line.find(text) != std::string::npos) return true;
        }
        return false;
    }
    // The first probe line that has all of the texts, or "" when there is none.
    static std::string LineWith(const std::vector<std::string>& lines, std::initializer_list<const char*> texts) {
        for (const auto& line : lines) {
            bool all = true;
            for (const char* text : texts) all = all && line.find(text) != std::string::npos;
            if (all) return line;
        }
        return "";
    }
    static std::string Joined(const std::vector<std::string>& lines) {
        std::string all;
        for (const auto& line : lines) all += line + "\n";
        return all;
    }
};

}  // namespace

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, HoldingAnotherModsToolTheCommandLogsTheToolsConfigFileName) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    RunCommand();

    const auto lines = Probe();
    EXPECT_TRUE(AnyLineHas(lines, "config file: mods/FS25_OtherMod/handTools/otherModTool.xml")) << Joined(lines);
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, EverySpecTableIsLoggedOneLevelDeepWithEachFieldsNameTypeAndShortValue) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    RunCommand();

    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.charge (number) = 5" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.isReeling (boolean) = false" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.lines (table) = 3 keys" }).empty()) << "a table by its key count: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.onBite (function)" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModLine.length (number) = 12.5" }).empty()) << "every spec_ table, not only the first: " << Joined(lines);

    const std::string label = LineWith(lines, { "spec_otherModTool.label (string)" });
    EXPECT_NE(label.find("\"A rather long"), std::string::npos) << label;
    EXPECT_EQ(label.find("written to the log file"), std::string::npos) << "a long string is cut short: " << label;

    const std::string node = LineWith(lines, { "spec_otherModTool.bobberNode (number)" });
    EXPECT_NE(node.find("\"otherModBobber\""), std::string::npos) << "a node is named: " << node;
}
