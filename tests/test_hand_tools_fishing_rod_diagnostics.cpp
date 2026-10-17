// Hand Tools, the fishing rod in vrHandToolsDiagnostics (ADR-0031): with the rod in the Tool Hand, the console command also logs
// the rod's state, its last cast (the rod's and the head's directions and where the bobber landed) and the last action it was
// given, so that the headset round can tell whether the cast follows the rod or the head and learn what the secondary action does.
// Seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua
// stubs (companion_mod_stubs.h, StubHandTool and the Highlands Fishing DLC's rod, kStubHandToolFishingRod). A test drives the fake
// Motion Controllers and the rod's stub as the game would, runs the command as the game runs it and asserts what the developer
// reads in the log and what the command answers. Never the mod's own state.
// Prior art: test_hand_tools_describe_unsupported.cpp and test_hand_tools_offset_tuning.cpp (a console command and its log lines),
// test_hand_tools_fishing_rod.cpp (the rod in the hand).

#include "hand_tools_test_support.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace hand_tools_test;

// Every line the command writes about the rod starts with this.
constexpr const char* kRodTag = "[FS25_VR] fishing rod ";

// Lua helpers, as the game would know it. Each ray is { x, y, z, dirX, dirY, dirZ } in the world.
//   headRay()          the first person camera: the ray the game casts without a Hand Tool
//   useCalls(action)   the values the rod's handler for `action` was called with, in order, as one text ("1,0")
constexpr const char* kRodDiagnosticsHelpers = R"luau(
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.useCalls(action)
        local values = {}
        for _, call in ipairs(StubHandTool.useCalls) do
            if call.tool == "fishingRod" and call.action == action then table.insert(values, tostring(call.value)) end
        end
        return table.concat(values, ",")
    end
)luau";

// The hand held so that the rod points well away from the way the head looks (down -Z).
constexpr float kAimPitch = 1.2f;
constexpr float kAimYaw = 1.0f;

// Three numbers, as the command writes a direction or a position: "(x, y, z)".
struct Triple {
    bool found = false;
    double x = 0, y = 0, z = 0;
};

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsFishingRodDiagnosticsTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    // The mod started with a player on foot in a game with the Highlands Fishing DLC and the game clock set.
    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = std::string(kXmlStubs) + FishingRodStubs("ok");
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kRodDiagnosticsHelpers, "=rodDiagnosticsHelpers"), 0);
        SetGameTime(10000);
    }

    // The rod held on foot with the right Motion Controller picked up, aimed away from the head, and a few frames run.
    void StartWithTheRodInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        headset.SetHandPose(kRight, HandAt(0.25f, -0.10f, -0.30f, kAimPitch, kAimYaw));
        headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.30f, kAimPitch, kAimYaw));
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    // Runs the command as the game would; the log lines are what the game logged during it.
    std::string RunCommand() {
        s_capturedLogs.clear();
        return EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    }
    // The lines the command wrote about the rod, without the tag.
    std::vector<std::string> RodLines() {
        std::vector<std::string> lines;
        for (const auto& line : s_capturedLogs) {
            if (line.rfind(kRodTag, 0) == 0) lines.push_back(line.substr(std::string(kRodTag).size()));
        }
        return lines;
    }
    // The rod line that starts with `field` (without it), or "" when there is none.
    std::string RodLine(const std::string& field) {
        for (const auto& line : RodLines()) {
            if (line.rfind(field, 0) == 0) return line.substr(field.size());
        }
        return "";
    }
    std::string Log() {
        std::string all;
        for (const auto& line : s_capturedLogs) all += line + "\n";
        return all;
    }
    // The numbers right after `label` in `line`.
    static Triple After(const std::string& line, const std::string& label) {
        Triple t;
        const size_t at = line.find(label);
        if (at == std::string::npos) return t;
        t.found = std::sscanf(line.c_str() + at + label.size(), " (%lf, %lf, %lf)", &t.x, &t.y, &t.z) == 3;
        return t;
    }
};

}  // namespace

// ---- The rod's state ------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, WithTheRodInTheHandTheCommandLogsItsStateForEachOfTheRodsStates) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    const std::pair<const char*, const char*> states[] = {
        { "idle", "idle" }, { "charging", "charging" }, { "waiting", "line out" }, { "bite", "bite" },
        { "reeling", "reeling" }, { "caught", "caught" },
    };
    for (const auto& [stubState, logged] : states) {
        ASSERT_EQ(RunLuauChunk(L, std::string("StubHandTool.setRodState(rod, '") + stubState + "')", "=setRodState"), 0);
        FRAMES(1);
        RunCommand();
        EXPECT_EQ(RodLine("state: "), logged) << stubState << ":\n" << Log();
    }
}

// A read of the rod's spec that fails while the rod is in the hand (a rod reshaped from the start is the game's with a warning,
// test_hand_tools_fishing_rod.cpp): the line says "unknown", nothing errs, and the rod stays in the hand.
TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, ARodSpecThatCannotBeReadInTheHandGivesAnUnknownLineNeverAnError) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    s_capturedLogs.clear();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'waiting'); StubHandTool.rodSpec(rod).state = nil", "=unreadable"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << "a failed read is no reshaped rod";
    EXPECT_EQ(CountLogLines("[Warning]"), 0) << Log();

    RunCommand();

    EXPECT_EQ(RodLine("state: "), "unknown") << Log();
    for (const auto& line : s_capturedLogs) {
        EXPECT_EQ(line.find("rror"), std::string::npos) << line;
        EXPECT_EQ(line.find("could not"), std::string::npos) << line;
    }

    ASSERT_EQ(RunLuauChunk(L, "rod['spec_pdlc_highlandsFishingPack.fishingRod'] = setmetatable({}, { __index = function() error('reshaped') end })",
                           "=unreadable"), 0);
    RunCommand();  // no frame: the stub's own update would raise too
    EXPECT_EQ(RodLine("state: "), "unknown") << "a spec that raises on every read: " << Log();
}

// The secondary action is noted through the game's own action event only while the rod is in the Tool Hand: once the rod leaves
// the hand, is put away, or Hand Tools are off, the event calls the rod's handler as the game registered it (ADR-0029: off leaves
// every Hand Tool as the game has it).
TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, TheSecondaryEventIsTheGamesOwnAgainOnceTheRodLeavesTheHandOrHandToolsAreOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    auto gamesOwn = [&]() {
        return EvalLua(L, "tostring(rod.actionEvents[2].callback == StubHandTool.fishingRodClass.onActivateSecondaryAction)");
    };
    ASSERT_EQ(gamesOwn(), "false") << "noted while the rod is in the hand";

    AdvanceGameTime(3100);  // set down
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");
    EXPECT_EQ(gamesOwn(), "true") << "set down";

    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    ASSERT_EQ(gamesOwn(), "false") << "in the hand again";
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    EXPECT_EQ(gamesOwn(), "true") << "vrHandTools off, at once";
    FRAMES(2);
    EXPECT_EQ(gamesOwn(), "true") << "and it stays the game's";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressSecondaryBinding(rod, 1); StubHandTool.pressSecondaryBinding(rod, 0)", "=secondaryOff"), 0);
    EXPECT_EQ(EvalLua(L, "useCalls('ACTIVATE_HANDTOOL_SECONDARY')"), "1,0") << "the game's binding still reaches the rod";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(1);
    ASSERT_EQ(gamesOwn(), "false") << "on again, in the hand";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(rod)", "=putAway"), 0);
    FRAMES(1);
    EXPECT_EQ(gamesOwn(), "true") << "put away";
}

// ---- What the command logs without the rod in the hand -------------------------------------------------------------------------

// The fields of the one-line answer, counted by its separators.
static int FieldsOf(const std::string& answer) {
    int fields = 1;
    for (size_t at = answer.find(" | "); at != std::string::npos; at = answer.find(" | ", at + 3)) ++fields;
    return fields;
}

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, WithAnotherToolOrNothingHeldTheCommandLogsWhatItLoggedBefore) {
    ASSERT_NO_FATAL_FAILURE(Start());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    std::string answer = RunCommand();
    EXPECT_NE(answer.find("held Hand Tool: none"), std::string::npos) << answer;
    EXPECT_TRUE(RodLines().empty()) << Log();
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), FieldsOf(answer)) << "one log line per field of the answer: " << Log();

    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true");
    answer = RunCommand();
    EXPECT_NE(answer.find("held Hand Tool: lance (supported)"), std::string::npos) << answer;
    EXPECT_TRUE(RodLines().empty()) << Log();
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), FieldsOf(answer)) << Log();
}

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, TheRodLinesAreInTheLogOnlyWhileTheRodIsInTheToolHandAndNeverInTheOneLineAnswer) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    std::string answer = RunCommand();
    EXPECT_FALSE(RodLines().empty()) << Log();
    EXPECT_EQ(answer.find("fishing rod"), std::string::npos) << "the one-line answer is as it was: " << answer;
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), FieldsOf(answer) + static_cast<int>(RodLines().size())) << Log();

    AdvanceGameTime(3100);  // set down: the rod is where the game puts it
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");
    answer = RunCommand();
    EXPECT_TRUE(RodLines().empty()) << Log();
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), FieldsOf(answer)) << Log();
}

// ---- The last cast --------------------------------------------------------------------------------------------------------------

// Whether the cast follows the rod or the head: the rod's ray direction and the head's direction when the state left charging, and
// where the bobber landed.
TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, AfterACastTheCommandLogsTheRodsRayDirectionTheHeadsDirectionAndTheBobbersPosition) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    RunCommand();
    EXPECT_EQ(RodLine("last cast: "), "none yet") << Log();

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(20);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(Number(L, "#StubHandTool.casts"), 1.0);
    // Where the stub threw it: along the rod (its ray at the release) and ropeLength out.
    const double rod[3] = { Number(L, "StubHandTool.casts[1].ray[4]"), Number(L, "StubHandTool.casts[1].ray[5]"), Number(L, "StubHandTool.casts[1].ray[6]") };
    const double head[3] = { Number(L, "headRay()[4]"), Number(L, "headRay()[5]"), Number(L, "headRay()[6]") };
    const double bobber[3] = { Number(L, "StubHandTool.casts[1].bobber[1]"), Number(L, "StubHandTool.casts[1].bobber[2]"), Number(L, "StubHandTool.casts[1].bobber[3]") };
    ASSERT_GT(std::abs(rod[0] - head[0]) + std::abs(rod[1] - head[1]) + std::abs(rod[2] - head[2]), 0.5) << "the rod points away from the view";

    // The hand moves on: the line still tells of the cast.
    headset.SetHandPose(kRight, HandAt(0.30f, 0.0f, -0.40f));
    headset.SetHandGripPose(kRight, HandAt(0.30f, 0.0f, -0.40f));
    FRAMES(2);
    RunCommand();

    const std::string line = RodLine("last cast: ");
    const Triple rodLogged = After(line, "rod direction");
    const Triple headLogged = After(line, "head direction");
    const Triple bobberLogged = After(line, "bobber at");
    ASSERT_TRUE(rodLogged.found && headLogged.found && bobberLogged.found) << line;
    EXPECT_NEAR(rodLogged.x, rod[0], 0.01) << line;
    EXPECT_NEAR(rodLogged.y, rod[1], 0.01) << line;
    EXPECT_NEAR(rodLogged.z, rod[2], 0.01) << line;
    EXPECT_NEAR(headLogged.x, head[0], 0.01) << line;
    EXPECT_NEAR(headLogged.y, head[1], 0.01) << line;
    EXPECT_NEAR(headLogged.z, head[2], 0.01) << line;
    EXPECT_NEAR(bobberLogged.x, bobber[0], 0.01) << line;
    EXPECT_NEAR(bobberLogged.y, bobber[1], 0.01) << line;
    EXPECT_NEAR(bobberLogged.z, bobber[2], 0.01) << line;
}

// The real bobber flies out after the cast and drifts once it is on the water: the command logs where it landed (where it first
// came to rest), never a point of its flight, nor where it drifted to since.
TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, TheBobberIsLoggedWhereItLandedNotInFlightNorWhereItDriftedTo) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(20);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "StubHandTool.rodState(rod)"), "waiting");

    // The flight: 0.3 m a frame (18 m/s), down to the water at (6.5, -0.5, -10).
    auto bobberAt = [&](double x, double y, double z) {
        ASSERT_EQ(RunLuauChunk(L, "setTranslation(StubHandTool.rodSpec(rod).bobber, " + std::to_string(x) + ", " + std::to_string(y) + ", " +
                                      std::to_string(z) + ")", "=bobberAt"), 0);
    };
    for (int step = 0; step <= 5; ++step) {
        bobberAt(5.0 + 0.3 * step, -0.5 + 0.1 * (5 - step), -10.0);
        FRAMES(1);
        if (step == 3) {
            RunCommand();
            const std::string line = RodLine("last cast: ");
            EXPECT_FALSE(After(line, "bobber at").found) << "in flight, the bobber has not landed: " << line;
            EXPECT_NE(line.find("not landed"), std::string::npos) << line;
        }
    }
    FRAMES(2);  // at rest on the water

    // The drift: 1 cm a frame (0.6 m/s), a metre in all.
    for (int step = 1; step <= 100; ++step) {
        bobberAt(6.5 + 0.01 * step, -0.5, -10.0);
        FRAMES(1);
    }
    RunCommand();
    const std::string line = RodLine("last cast: ");
    const Triple bobber = After(line, "bobber at");
    ASSERT_TRUE(bobber.found) << line;
    EXPECT_NEAR(bobber.x, 6.5, 0.01) << "where it landed: " << line;
    EXPECT_NEAR(bobber.y, -0.5, 0.01) << line;
    EXPECT_NEAR(bobber.z, -10.0, 0.01) << line;
}

// ---- The last action ------------------------------------------------------------------------------------------------------------

// What the secondary action does is not known yet (ADR-0031): the command says which action the rod was given last, the trigger's
// primary use or a secondary seen through the game's own binding.
TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, AfterASecondaryThroughTheGamesBindingItLogsThatAsTheLastActionAndAfterATriggerPullThePrimary) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    RunCommand();
    EXPECT_EQ(RodLine("last action: "), "none yet") << Log();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressSecondaryBinding(rod, 1)", "=secondaryDown"), 0);
    FRAMES(1);
    RunCommand();
    std::string line = RodLine("last action: ");
    EXPECT_EQ(line.rfind("secondary", 0), 0u) << Log();
    EXPECT_NE(line.find("(input 1)"), std::string::npos) << line;
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressSecondaryBinding(rod, 0)", "=secondaryUp"), 0);
    RunCommand();
    EXPECT_NE(RodLine("last action: ").find("(input 0)"), std::string::npos) << "the release too: " << Log();
    EXPECT_EQ(EvalLua(L, "useCalls('ACTIVATE_HANDTOOL_SECONDARY')"), "1,0") << "the rod still got both";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    RunCommand();
    line = RodLine("last action: ");
    EXPECT_EQ(line.rfind("primary", 0), 0u) << Log();
    EXPECT_NE(line.find("trigger"), std::string::npos) << line;
    EXPECT_NE(line.find("(input 1)"), std::string::npos) << line;
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    RunCommand();
    EXPECT_NE(RodLine("last action: ").find("(input 0)"), std::string::npos) << "the let-go: " << Log();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressSecondaryBinding(rod, 1)", "=secondaryAgain"), 0);
    RunCommand();
    EXPECT_EQ(RodLine("last action: ").rfind("secondary", 0), 0u) << "the secondary again: " << Log();
}
