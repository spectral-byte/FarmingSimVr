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

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, EachSpecializationsGlobalClassIsLoggedWithItsFunctionNamesHandlersKeptOnTheClassOnlyIncluded) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    RunCommand();

    const auto lines = Probe();
    const std::string line = LineWith(lines, { "class OtherModHandTool", "otherModTool" });
    EXPECT_NE(line.find("onUseAction"), std::string::npos) << "the use handler is on the class only: " << Joined(lines);
    EXPECT_NE(line.find("onRollAction"), std::string::npos) << Joined(lines);
    EXPECT_EQ(LineWith(lines, { "spec_otherModLine", "class" }), "") << "a spec table without a class has no class line: " << Joined(lines);
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, TheFunctionsOnTheToolsTypeAreLogged) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    RunCommand();

    const auto lines = Probe();
    const std::string line = LineWith(lines, { "type otherModTool", "functions" });
    EXPECT_NE(line.find("getFloatDepth"), std::string::npos) << Joined(lines);
    EXPECT_NE(line.find("isOtherModReeling"), std::string::npos) << Joined(lines);
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, EveryRegisteredActionEventIsLoggedWithItsCallbacksNameAndTheEdgesItFiresOn) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    RunCommand();

    const auto lines = Probe();
    const std::string use = LineWith(lines, { "action event ACTIVATE_HANDTOOL" });
    EXPECT_NE(use.find("OtherModHandTool.onUseAction"), std::string::npos) << "the callback is named from the class: " << Joined(lines);
    EXPECT_NE(use.find("press"), std::string::npos) << use;
    EXPECT_NE(use.find("release"), std::string::npos) << use;
    EXPECT_EQ(use.find("every frame"), std::string::npos) << use;

    const std::string roll = LineWith(lines, { "action event AXIS_ROTATE_HANDTOOL" });
    EXPECT_NE(roll.find("OtherModHandTool.onRollAction"), std::string::npos) << Joined(lines);
    EXPECT_NE(roll.find("every frame"), std::string::npos) << roll;
    EXPECT_EQ(roll.find("press"), std::string::npos) << roll;
    EXPECT_EQ(roll.find("release"), std::string::npos) << roll;

    const std::string nudge = LineWith(lines, { "action event AXIS_OTHERMOD_NUDGE" });
    EXPECT_NE(nudge.find("callback not found"), std::string::npos) << "a callback that is on no class is said so: " << Joined(lines);
    EXPECT_NE(nudge.find("release"), std::string::npos) << nudge;
    EXPECT_EQ(nudge.find("press"), std::string::npos) << nudge;
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, TheNodesBelowTheToolsRootAreLoggedByNameToAFixedDepth) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    RunCommand();

    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "node otherModRoot/otherModFirstPerson" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "node otherModRoot/otherModRod/otherModGuide" }).empty()) << Joined(lines);
    const std::string tip = LineWith(lines, { "node otherModRoot/otherModRod/otherModGuide/otherModTip" });
    EXPECT_FALSE(tip.empty()) << Joined(lines);
    EXPECT_NE(tip.find("1 child not listed"), std::string::npos) << "the depth cut is said: " << tip;
    EXPECT_EQ(Joined(lines).find("otherModFloat"), std::string::npos) << "below the depth, a node is not named: " << Joined(lines);
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, TheLogSaysWhetherTheGamePlacedTheToolThroughAttachToolToCamera) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());
    RunCommand();
    EXPECT_FALSE(LineWith(Probe(), { "placement: through attachToolToCamera" }).empty()) << Joined(Probe());

    // The player goes to the third person view: the game puts the tool in the avatar's hand.
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, true)", "=thirdPerson"), 0);
    RunCommand();
    EXPECT_FALSE(LineWith(Probe(), { "placement: not through attachToolToCamera" }).empty()) << Joined(Probe());
    EXPECT_TRUE(LineWith(Probe(), { "placement: through attachToolToCamera" }).empty());
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, AToolTheGameNeverPlacedThroughTheCameraIsDescribedToo) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.thirdPerson(foot.player, true)", "=thirdPerson"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)", "=hold"), 0);

    RunCommand();

    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "config file: mods/FS25_OtherMod" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "placement: not through attachToolToCamera" }).empty()) << Joined(lines);
}

// A game in which the Companion Mod could not wrap attachToolToCamera (a game update renamed it): the mod never sees the
// placement, so it does not claim a path it cannot know.
TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, WithoutThePlacementWrapThePlacementPathIsSaidToBeUnknownNotToBeAnotherPath) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    StartOptions options;
    options.variant = "missing";
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
    ASSERT_EQ(RunLuauChunk(L, "_G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)", "=hold"), 0);
    ASSERT_EQ(EvalLua(L, "tostring(StubHandTool.cameraAttaches)"), "1") << "the game did place the tool in front of the camera";

    RunCommand();

    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "config file: mods/FS25_OtherMod" }).empty()) << "the tool is described all the same: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "placement: unknown (the placement wrap is not installed)" }).empty()) << Joined(lines);
    EXPECT_TRUE(LineWith(lines, { "not through attachToolToCamera" }).empty()) << Joined(lines);
}

// ---- nothing to describe, and the answer ---------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, TheOneLineAnswerStaysTheSameAndOnlySaysTheToolIsUnsupported) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());

    const std::string reply = RunCommand();

    EXPECT_EQ(reply, EvalLua(L, "VRCompanion:getHandToolsDiagnostics()")) << "the answer is the one-line diagnostics";
    EXPECT_NE(reply.find("held Hand Tool: unsupported | in the Tool Hand: no (the Hand Tool is not supported)"), std::string::npos) << reply;
    EXPECT_EQ(reply.find("probe"), std::string::npos) << "the description goes to the log, not into the answer: " << reply;
    EXPECT_EQ(reply.find("config file"), std::string::npos) << reply;
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, WithNothingHeldTheCommandLogsExactlyTheLinesItsAnswerHas) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);

    const std::string reply = RunCommand();

    EXPECT_NE(reply.find("held Hand Tool: none"), std::string::npos) << reply;
    EXPECT_TRUE(Probe().empty()) << Joined(Probe());
    // One log line per field of the answer, nothing else: the command logs what it logged before.
    int fields = 1;
    for (size_t at = reply.find(" | "); at != std::string::npos; at = reply.find(" | ", at + 3)) ++fields;
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), fields) << reply;
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, WithASupportedToolHeldTheCommandLogsExactlyTheLinesItsAnswerHas) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));

    const std::string reply = RunCommand();

    EXPECT_NE(reply.find("held Hand Tool: lance (supported)"), std::string::npos) << reply;
    EXPECT_TRUE(Probe().empty()) << Joined(Probe());
    int fields = 1;
    for (size_t at = reply.find(" | "); at != std::string::npos; at = reply.find(" | ", at + 3)) ++fields;
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), fields) << reply;
}

// Multiplayer: the game attaches the Hand Tool of every player in the session through the same function. Only the local
// player's own tool is described.
TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, AnotherPlayersToolIsNeverDescribed) {
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    SetGameTime(10000);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        local remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteTool = StubHandTool.makeTool(remote, "remoteTool", "spec_remoteTool")
        remoteTool.configFileName = "mods/FS25_Remote/handTools/remote.xml"
        remoteTool.isHeld = true
        remoteTool.carryingPlayer = remote
        remote.currentHandTool = remoteTool
        remoteTool:attachTool()
    )luau", "=remoteTool"), 0);

    const std::string reply = RunCommand();

    EXPECT_NE(reply.find("held Hand Tool: none"), std::string::npos) << reply;
    EXPECT_TRUE(Probe().empty()) << Joined(Probe());
    for (const auto& line : s_capturedLogs) EXPECT_EQ(line.find("remote.xml"), std::string::npos) << line;
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, AnotherPlayersToolAttachedWhileTheLocalPlayerHoldsOneDoesNotReplaceTheLocalDescription) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        local remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteTool = StubHandTool.makeTool(remote, "remoteTool", "spec_remoteTool")
        remoteTool.configFileName = "mods/FS25_Remote/handTools/remote.xml"
        remoteTool.isHeld = true
        remoteTool.carryingPlayer = remote
        remote.currentHandTool = remoteTool
        remoteTool:attachTool()
    )luau", "=remoteTool"), 0);

    RunCommand();

    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "config file: mods/FS25_OtherMod" }).empty()) << Joined(lines);
    EXPECT_TRUE(LineWith(lines, { "remote.xml" }).empty()) << Joined(lines);
}

// ---- partial reads and caps ---------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, ASpecTableThatThrowsWhenReadGivesAPartialDescriptionAndNoError) {
    ASSERT_NO_FATAL_FAILURE(StartHolding("{ brokenSpec = true }"));

    std::string reply;
    ASSERT_NO_THROW(reply = RunCommand());

    EXPECT_NE(reply.find("held Hand Tool: unsupported"), std::string::npos) << "the command still answers: " << reply;
    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "spec_otherModBroken.first (number) = 1" }).empty()) << "what can be read of the table is there: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModBroken.last (number) = 2" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModBroken.gone", "could not be read", "is gone" }).empty()) << "what cannot says so: " << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.charge (number) = 5" }).empty()) << "and the other tables are described as ever";
    EXPECT_FALSE(LineWith(lines, { "node otherModRoot/otherModRod" }).empty()) << "and so are the nodes";
    for (const auto& line : s_capturedLogs) EXPECT_EQ(line.find("Error"), std::string::npos) << line;
}

// ---- caps ---------------------------------------------------------------------------------------------------------
// The fields of a spec table, the functions of a class, the nodes and the lines of a whole run are capped, and each cut says
// how much was left out: what is shown and what is left out add up to what the tool has.

namespace {

// The N of "N more" in a line, -1 when the line has none.
int LeftOutCount(const std::string& line) {
    const size_t more = line.find(" more ");
    if (more == std::string::npos) return -1;
    size_t first = more;
    while (first > 0 && std::isdigit(static_cast<unsigned char>(line[first - 1]))) --first;
    return first == more ? -1 : std::stoi(line.substr(first, more - first));
}

int CountLinesStartingWith(const std::vector<std::string>& lines, const std::string& prefix) {
    int count = 0;
    for (const auto& line : lines) count += line.rfind(prefix, 0) == 0 ? 1 : 0;
    return count;
}

}  // namespace

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, ASpecTableWithManyFieldsIsCutAndTheLeftOutCountIsLogged) {
    ASSERT_NO_FATAL_FAILURE(StartHolding("{ extraSpecFields = 100 }"));  // 6 fields of its own and 100 more

    RunCommand();

    const auto lines = Probe();
    const int shown = CountLinesStartingWith(lines, "spec_otherModTool.");
    EXPECT_GT(shown, 5) << "a good part of the table is there";
    EXPECT_LT(shown, 106) << "but not all of it";
    const int leftOut = LeftOutCount(LineWith(lines, { "spec_otherModTool: ", "more fields left out" }));
    EXPECT_GT(leftOut, 0) << Joined(lines);
    EXPECT_EQ(shown + leftOut, 106) << "what is shown and what is left out are the fields of the table";
    EXPECT_EQ(CountLinesStartingWith(lines, "spec_otherModLine."), 1) << "another spec table has its own cap";
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, AClassWithManyFunctionsIsCutAndTheLeftOutCountIsLogged) {
    ASSERT_NO_FATAL_FAILURE(StartHolding("{ extraClassFunctions = 100 }"));  // onUseAction, onRollAction and 100 more

    RunCommand();

    const auto lines = Probe();
    const std::string line = LineWith(lines, { "class OtherModHandTool", "more left out" });
    ASSERT_FALSE(line.empty()) << Joined(lines);
    const size_t listStart = line.find("functions: ") + std::string("functions: ").size();
    const std::string list = line.substr(listStart, line.find(" (and ") - listStart);
    int shown = 1;
    for (size_t at = list.find(", "); at != std::string::npos; at = list.find(", ", at + 2)) ++shown;
    EXPECT_GT(shown, 5);
    EXPECT_LT(shown, 102);
    EXPECT_EQ(shown + LeftOutCount(line), 102) << line;
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, ATreeWithManyNodesIsCutAndTheLeftOutCountIsLogged) {
    // Within three levels the tool has FirstPerson, Rod, Guide, Tip and 100 more children of the root: 104 nodes.
    ASSERT_NO_FATAL_FAILURE(StartHolding("{ extraNodes = 100 }"));

    RunCommand();

    const auto lines = Probe();
    const int shown = CountLinesStartingWith(lines, "node otherModRoot/");
    EXPECT_GT(shown, 3);
    EXPECT_LT(shown, 104);
    const int leftOut = LeftOutCount(LineWith(lines, { "node tree: ", "more nodes left out" }));
    EXPECT_GT(leftOut, 0) << Joined(lines);
    EXPECT_EQ(shown + leftOut, 104);
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, ARunIsCappedInLinesTheMostUsefulLinesSurviveAndTheLeftOutCountIsLogged) {
    // 15 more specializations of 20 fields each (under the cap of a table): 300 field lines on top of the rest.
    ASSERT_NO_FATAL_FAILURE(StartHolding("{ extraSpecTables = 15 }"));

    RunCommand();

    const auto lines = Probe();
    const std::string leftOutLine = LineWith(lines, { "more lines left out" });
    const int leftOut = LeftOutCount(leftOutLine);
    EXPECT_GT(leftOut, 0) << "the cut is said";
    const int shown = static_cast<int>(lines.size()) - 1;  // the last line is the left-out line itself
    EXPECT_EQ(lines.back(), leftOutLine) << "and it is the last line";
    EXPECT_LT(shown, 300);
    EXPECT_GE(shown + leftOut, 300) << "what is shown and what is left out are at least the fields of the 15 tables";
    EXPECT_FALSE(LineWith(lines, { "config file" }).empty()) << "the first lines survive";
    EXPECT_FALSE(LineWith(lines, { "class OtherModHandTool" }).empty());
    EXPECT_FALSE(LineWith(lines, { "action event ACTIVATE_HANDTOOL" }).empty());
    EXPECT_FALSE(LineWith(lines, { "placement:" }).empty());
}

// ---- the probe only reads -----------------------------------------------------------------------------------------

namespace {

// Everything about the tool and the game's Hand Tool functions that the command could change, as one text: which function each
// name on the game's classes, the tool's type and the tool's action events is (a wrap would show), the tool's own fields and its
// place in the scene.
constexpr const char* kSnapshotHelpers = R"luau(
    function _G.snapshot()
        local parts = {}
        local function add(text) parts[#parts + 1] = text end
        local function functionsOf(label, t)
            local names = {}
            for name, value in pairs(t) do names[#names + 1] = tostring(name) .. "=" .. tostring(value) end
            table.sort(names)
            add(label .. ": " .. table.concat(names, ";"))
        end
        functionsOf("HandTool", _G.HandTool)
        functionsOf("Player", _G.Player)
        functionsOf("PlayerTargeter", _G.PlayerTargeter)
        functionsOf("OtherModHandTool", _G.OtherModHandTool)
        functionsOf("type", otherTool.type.functions)
        functionsOf("tool", otherTool)
        for index, event in ipairs(otherTool.actionEvents) do functionsOf("event" .. index, event) end
        for name, spec in pairs(otherTool) do
            if type(name) == "string" and name:sub(1, 5) == "spec_" then functionsOf(name, spec) end
        end
        add("parent " .. tostring(getParent(otherTool.rootNode)))
        add("translation " .. table.concat({ getTranslation(otherTool.rootNode) }, ","))
        add("world " .. table.concat({ getWorldTranslation(otherTool.rootNode) }, ","))
        add("uses " .. #StubHandTool.useCalls)
        return table.concat(parts, "\n")
    end
)luau";

}  // namespace

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, RunningTheCommandChangesNothingAboutTheToolNorInstallsAWrapOnTheGamesFunctions) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());
    ASSERT_EQ(RunLuauChunk(L, kSnapshotHelpers, "=snapshotHelpers"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.before = snapshot()", "=before"), 0);

    RunCommand();
    ASSERT_FALSE(Probe().empty());
    RunCommand();

    ASSERT_EQ(RunLuauChunk(L, "_G.after = snapshot()", "=after"), 0);
    EXPECT_EQ(EvalLua(L, "before == after"), "true") << "before:\n" << EvalLua(L, "before") << "\nafter:\n" << EvalLua(L, "after");
    EXPECT_GT(EvalLua(L, "#before").size(), 0u);
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, TheToolsPlacementLookRayAndTriggerAreTheSameBeforeAndAfterTheCommand) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    const std::string placement = "tostring(getParent(otherTool.rootNode)) .. ':' .. table.concat({ getWorldTranslation(otherTool.rootNode) }, ',')";
    const std::string ray = "table.concat({ StubHandTool.lastRay() }, ',')";
    const std::string placementBefore = EvalLua(L, placement);
    const std::string rayBefore = EvalLua(L, ray);
    ASSERT_NE(rayBefore, "");
    EXPECT_EQ(EvalLua(L, "isBelow(otherTool.rootNode, foot.cam.pitchNode)"), "true") << "the game's placement, in front of the camera";

    RunCommand();
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, placement), placementBefore) << "the tool stays where the game put it";
    EXPECT_EQ(EvalLua(L, ray), rayBefore) << "the look ray is the head's, as before";

    // The trigger: the game's own binding uses the tool, the Tool Hand's trigger does not (the tool is not supported).
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.useCalls = {}", "=forgetUses"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubHandTool.useCalls"), "0") << "the Tool Hand's trigger does not use another mod's tool";
    RunCommand();
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "#StubHandTool.useCalls"), "0");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(otherTool, 1); StubHandTool.pressUseBinding(otherTool, 0)", "=binding"), 0);
    EXPECT_EQ(EvalLua(L, "#StubHandTool.useCalls"), "2") << "the game's handler is still called on press and release, once each";
    EXPECT_EQ(EvalLua(L, "StubHandTool.useCalls[1].value"), "1");
    EXPECT_EQ(EvalLua(L, "StubHandTool.useCalls[2].value"), "0");
}

TEST_F(VirtualGamepadHandToolsDescribeUnsupportedTest, RunningTheCommandAgainAfterSomethingHappenedShowsWhatChanged) {
    ASSERT_NO_FATAL_FAILURE(StartHolding());
    RunCommand();
    EXPECT_FALSE(LineWith(Probe(), { "spec_otherModTool.isReeling (boolean) = false" }).empty());

    ASSERT_EQ(RunLuauChunk(L, "otherTool.spec_otherModTool.isReeling = true; otherTool.spec_otherModTool.charge = 9", "=cast"), 0);
    RunCommand();

    const auto lines = Probe();
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.isReeling (boolean) = true" }).empty()) << Joined(lines);
    EXPECT_FALSE(LineWith(lines, { "spec_otherModTool.charge (number) = 9" }).empty()) << Joined(lines);
}
