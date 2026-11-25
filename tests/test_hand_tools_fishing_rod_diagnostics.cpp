#include "hand_tools_test_support.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace hand_tools_test;

constexpr const char* kRodTag = "[FS25_VR] fishing rod ";

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

constexpr float kAimPitch = 1.2f;
constexpr float kAimYaw = 1.0f;

struct Triple {
    bool found = false;
    double x = 0, y = 0, z = 0;
};

class VirtualGamepadHandToolsFishingRodDiagnosticsTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = StubChunk("xml_settings") + FishingRodStubs("ok");
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kRodDiagnosticsHelpers, "=rodDiagnosticsHelpers"), 0);
        SetGameTime(10000);
    }

    void StartWithTheRodInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldFishingRod(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        headset.SetHandPose(kRight, HandAt(0.25f, -0.10f, -0.30f, kAimPitch, kAimYaw));
        headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.30f, kAimPitch, kAimYaw));
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    std::string RunCommand() {
        s_capturedLogs.clear();
        return EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    }
    std::vector<std::string> RodLines() {
        std::vector<std::string> lines;
        for (const auto& line : s_capturedLogs) {
            if (line.rfind(kRodTag, 0) == 0) lines.push_back(line.substr(std::string(kRodTag).size()));
        }
        return lines;
    }
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
    static Triple After(const std::string& line, const std::string& label) {
        Triple t;
        const size_t at = line.find(label);
        if (at == std::string::npos) return t;
        t.found = sscanf_s(line.c_str() + at + label.size(), " (%lf, %lf, %lf)", &t.x, &t.y, &t.z) == 3;
        return t;
    }
};

}

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

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, TheCommandLogsWhatTheRodsLastDrawInEachStateRenderedAndHowItWasMoved) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    RunCommand();
    EXPECT_EQ(RodLine("screen draw: "), "none yet") << Log();
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw(); StubHandTool.setRodState(rod, 'reeling'); rod:setRodBendFactor(0.5); StubHandTool.draw()", "=draws"), 0);
    RunCommand();
    EXPECT_EQ(RodLine("screen draw: "), "idle: nothing rendered, nothing moved; "
                                        "reeling: 4 rendered (0 unmoved), x 0.655 to 0.770, y 0.370 to 0.710, "
                                        "9 layout numbers and 6 Overlays moved by -0.100, -0.050")
        << Log();
}

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, WhileTheRodReelsTheCommandLogsItsStateFieldsAndItsSpecNumbers) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    RunCommand();
    EXPECT_EQ(RodLine("reeling state: "), "") << "idle: " << Log();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.setRodState(rod, 'reeling')", "=reel"), 0);
    FRAMES(1);
    RunCommand();
    const std::string fields = RodLine("reeling state: ");
    EXPECT_NE(fields.find("fishTargetBarOverlay = Overlay fishTargetBar.png (0.6850, 0.4100, 0.0500, 0.3000), "), std::string::npos)
        << fields;
    EXPECT_NE(fields.find("posX = 0.6500, posY = 0.4000, "), std::string::npos) << fields;
    EXPECT_EQ(fields.find("fishingRod"), std::string::npos) << "not the back reference to the rod: " << fields;
    const std::string numbers = RodLine("spec numbers: ");
    EXPECT_NE(numbers.find("angleMax = 2.2689"), std::string::npos) << numbers;
    EXPECT_NE(numbers.find("rodBendFactor = "), std::string::npos) << numbers;
}

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
    RunCommand();
    EXPECT_EQ(RodLine("state: "), "unknown") << "a spec that raises on every read: " << Log();
}

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, TheSecondaryEventIsTheGamesOwnAgainOnceTheRodLeavesTheHandOrHandToolsAreOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    auto gamesOwn = [&]() {
        return EvalLua(L, "tostring(rod.actionEvents[2].callback == StubHandTool.fishingRodClass.onActivateSecondaryAction)");
    };
    ASSERT_EQ(gamesOwn(), "false") << "noted while the rod is in the hand";

    AdvanceGameTime(3100);
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

    AdvanceGameTime(3100);
    FRAMES(1);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");
    answer = RunCommand();
    EXPECT_TRUE(RodLines().empty()) << Log();
    EXPECT_EQ(static_cast<int>(s_capturedLogs.size()), FieldsOf(answer)) << Log();
}

TEST_F(VirtualGamepadHandToolsFishingRodDiagnosticsTest, AfterACastTheCommandLogsTheRodsRayDirectionTheHeadsDirectionAndTheBobbersPosition) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheRodInTheRightHand());
    RunCommand();
    EXPECT_EQ(RodLine("last cast: "), "none yet") << Log();

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(20);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    ASSERT_EQ(Number(L, "#StubHandTool.casts"), 1.0);
    const double rod[3] = { Number(L, "StubHandTool.casts[1].ray[4]"), Number(L, "StubHandTool.casts[1].ray[5]"), Number(L, "StubHandTool.casts[1].ray[6]") };
    const double head[3] = { Number(L, "headRay()[4]"), Number(L, "headRay()[5]"), Number(L, "headRay()[6]") };
    const double bobber[3] = { Number(L, "StubHandTool.casts[1].bobber[1]"), Number(L, "StubHandTool.casts[1].bobber[2]"), Number(L, "StubHandTool.casts[1].bobber[3]") };
    ASSERT_GT(std::abs(rod[0] - head[0]) + std::abs(rod[1] - head[1]) + std::abs(rod[2] - head[2]), 0.5) << "the rod points away from the view";

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
    FRAMES(2);

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
