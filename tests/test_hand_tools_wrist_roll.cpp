#include "hand_tools_test_support.h"
#include <cmath>
#include <string>

namespace {

using namespace hand_tools_test;

constexpr double kPi = 3.14159265358979323846;

constexpr float kLevelBar = static_cast<float>(kPi / 2.0);
constexpr float kBarForward = 1.4f;
constexpr float kHeading = 0.9f;

constexpr const char* kRollHelpers = R"luau(
    function _G.barDir() return { StubScene.worldDirection(saw.rootNode, 0, 0, 1) } end
    function _G.toolUp() return { StubScene.worldDirection(saw.rootNode, 0, 1, 0) } end
    function _G.dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
    function _G.planeNormal()
        local _, _, _, nx, ny, nz = saw:calculateCutPlane()
        return { nx, ny, nz }
    end
    function _G.roll() return saw.spec_chainsaw.currentCutRoll end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.barRay() return { StubHandTool.barRay(saw) } end
    function _G.state()
        local s = saw.spec_chainsaw.currentCutState
        if s == ChainsawCutState.CUTTING then return "cutting" end
        if s == ChainsawCutState.DELIMBING then return "delimbing" end
        return "idle"
    end
)luau";

constexpr float kBarFromTheFingers = static_cast<float>(kPi / 4.0);
inline fs25vr::HeadPose RolledPose(float x, float y, float z, float pitch, float heading, float twist) {
    using fs25vr::Quaternion;
    fs25vr::HeadPose pose = HandAt(x, y, z);
    const Quaternion yaw(0.0f, std::sin(heading * 0.5f), 0.0f, std::cos(heading * 0.5f));
    const Quaternion tilt(std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f));
    const Quaternion roll(0.0f, std::sin(twist * 0.5f), 0.0f, std::cos(twist * 0.5f));
    const Quaternion hold(std::sin(-kBarFromTheFingers * 0.5f), 0.0f, 0.0f, std::cos(-kBarFromTheFingers * 0.5f));
    pose.orientation = yaw * tilt * roll * hold;
    return pose;
}

class VirtualGamepadHandToolsWristRollTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    void Start(const char* variant = "ok") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        StartOptions options;
        const std::string stubs = ChainsawStubs(variant);
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kRollHelpers, "=rollHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void Roll(float twist, float pitch = kLevelBar) {
        const fs25vr::HeadPose pose = RolledPose(0.25f, -0.35f, -0.30f, pitch, kHeading, twist);
        headset.SetHandPose(kRight, pose);
        headset.SetHandGripPose(kRight, pose);
    }

    void StartWithChainsawInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
        Roll(0.0f);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    void PlantTreeAtTheBar() {
        ASSERT_EQ(RunLuauChunk(L, "_G.barTree = StubHandTool.treeAcross(barRay(), 0.3, 0.15)", "=plantTree"), 0);
    }

    double AngleBetween(const char* a, const char* b) {
        const double cosine = Number(L, std::string("dot(") + a + ", " + b + ")");
        return std::acos(std::fmax(-1.0, std::fmin(1.0, cosine)));
    }

    std::string State() { return EvalLua(L, "state()"); }
    bool True(const char* expression) { return EvalLua(L, expression) == "true"; }
    int Cuts() { return static_cast<int>(Number(L, "#StubHandTool.cuts")); }
    void RollAction(double delta) {
        const std::string lua = "HandToolChainsaw.onRollAction(saw, nil, " + std::to_string(delta) + ")";
        ASSERT_EQ(RunLuauChunk(L, lua.c_str(), "=rollAction"), 0);
    }
    double GameRoll() { return Number(L, "roll()"); }
};

}

TEST_F(VirtualGamepadHandToolsWristRollTest, TheCutPlaneRollsWithTheWristByTheSameAngleAboutTheBar) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAtTheBar());
    FRAMES(2);
    ASSERT_NEAR(Number(L, "planeNormal()[2]"), 0.0, 1e-3) << "an upright plane with the wrist not rolled";
    ASSERT_EQ(RunLuauChunk(L, "_G.upright = planeNormal()", "=upright"), 0);

    for (const float twist : { 0.5f, -0.5f, 1.2f, -1.2f, 0.0f }) {
        SCOPED_TRACE(twist);
        Roll(twist);
        FRAMES(2);
        EXPECT_NEAR(AngleBetween("planeNormal()", "upright"), std::abs(twist), 2e-3) << "the plane is rolled by the angle the wrist is";
        EXPECT_NEAR(Number(L, "dot(planeNormal(), toolUp())"), 0.0, 2e-3) << "the plane holds the tool's up, the way the wrist turned it";
        EXPECT_NEAR(Number(L, "dot(planeNormal(), barDir())"), 0.0, 2e-3) << "and the bar";
        EXPECT_NEAR(std::abs(GameRoll()), std::abs(twist), 2e-3);
    }
}

TEST_F(VirtualGamepadHandToolsWristRollTest, TheRollIsAboutTheBarEvenWhenTheBarPointsSlightlyDown) {
    StartWithChainsawInTheRightHand();
    Roll(0.0f, kBarForward);
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), 0.0, 2e-3) << "a bar slightly down is not a roll";
    for (const float twist : { 0.7f, -0.4f }) {
        SCOPED_TRACE(twist);
        Roll(twist, kBarForward);
        FRAMES(2);
        EXPECT_NEAR(std::abs(GameRoll()), std::abs(twist), 2e-3) << "the roll about the bar, not about the heading";
    }
}

TEST_F(VirtualGamepadHandToolsWristRollTest, RollsBeyondNinetyDegreesAreClampedToNinetyDegrees) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAtTheBar());
    Roll(0.5f);
    FRAMES(2);
    const double side = GameRoll() > 0.0 ? 1.0 : -1.0;

    Roll(2.0f);
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), side * kPi / 2.0, 1e-4);
    EXPECT_NEAR(std::abs(Number(L, "planeNormal()[2]")), 1.0, 1e-3) << "a flat plane: the most the game allows";

    Roll(-2.0f);
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), -side * kPi / 2.0, 1e-4);

    Roll(1.5f);
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), side * 1.5, 2e-3);
}

TEST_F(VirtualGamepadHandToolsWristRollTest, DuringACutRollingTheWristDoesNotChangeTheCutAngle) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAtTheBar());
    Roll(0.4f);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.startRoll = roll(); _G.startNormal = planeNormal()", "=start"), 0);
    ASSERT_GT(std::abs(Number(L, "startRoll")), 0.3);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");

    for (const float twist : { 1.0f, -0.6f, 0.0f }) {
        SCOPED_TRACE(twist);
        Roll(twist);
        FRAMES(3);
        ASSERT_EQ(State(), "cutting");
        EXPECT_NEAR(GameRoll(), Number(L, "startRoll"), 1e-6) << "the cut keeps the roll it began with";
        EXPECT_NEAR(Number(L, "dot(planeNormal(), startNormal)"), 1.0, 1e-4);
    }

    FRAMES(60);
    ASSERT_EQ(Cuts(), 1) << "the game cut the tree";
    EXPECT_NEAR(Number(L, "StubHandTool.cuts[1].normalX * startNormal[1] + StubHandTool.cuts[1].normalY * startNormal[2] + StubHandTool.cuts[1].normalZ * startNormal[3]"),
                1.0, 1e-3) << "along the plane at the angle the cut began with";
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    Roll(-0.9f);
    FRAMES(3);
    EXPECT_NEAR(std::abs(GameRoll()), 0.9, 2e-3);
}

TEST_F(VirtualGamepadHandToolsWristRollTest, TheRollActionDoesNothingWhileTheChainsawIsInATrackedToolHand) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAtTheBar());
    Roll(0.3f);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.wristRoll = roll(); _G.wristNormal = planeNormal()", "=wrist"), 0);
    ASSERT_GT(std::abs(Number(L, "wristRoll")), 0.2);

    ASSERT_NO_FATAL_FAILURE(RollAction(30));
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), Number(L, "wristRoll"), 1e-6) << "the stick does not fight the wrist";
    EXPECT_NEAR(Number(L, "dot(planeNormal(), wristNormal)"), 1.0, 1e-4);
    ASSERT_NO_FATAL_FAILURE(RollAction(-80));
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), Number(L, "wristRoll"), 1e-6);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 1.0) << "the cut locks the player's input, once";
    ASSERT_NO_FATAL_FAILURE(RollAction(50));
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), Number(L, "wristRoll"), 1e-6);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    EXPECT_EQ(State(), "idle");
    EXPECT_NEAR(GameRoll(), Number(L, "wristRoll"), 1e-6) << "and what it was given during the cut is not applied after it";
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0);
}

TEST_F(VirtualGamepadHandToolsWristRollTest, AnInputTheRollActionGaveByAnotherRouteIsNotAppliedEither) {
    StartWithChainsawInTheRightHand();
    Roll(0.3f);
    FRAMES(2);
    const double wrist = GameRoll();
    ASSERT_NEAR(std::abs(wrist), 0.3, 2e-3);
    ASSERT_EQ(RunLuauChunk(L, "saw.spec_chainsaw.rollInput = 30", "=staleInput"), 0);
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), wrist, 1e-6);
}

class VirtualGamepadHandToolsWristRollAsInTheGameTest : public VirtualGamepadHandToolsWristRollTest {
protected:
    // The wrist is rolled and the chainsaw is not in the tracked Tool Hand: the wrist changes nothing and the roll action rolls
    // the plane, as in the game.
    // (`moveWrist` is false for a controller that has been set down or lost: moving it would put it back in use or track it again.)
    void ExpectTheRollActionWorks(const char* why, bool moveWrist = true) {
        SCOPED_TRACE(why);
        FRAMES(2);
        ASSERT_FALSE(True("VRCompanion:isHandHoldingHandTool(1)"));
        const double before = GameRoll();
        if (moveWrist) {
            Roll(1.0f);
            FRAMES(3);
            EXPECT_NEAR(GameRoll(), before, 1e-6) << "the wrist does not roll a chainsaw that is not in the hand";
        }

        ASSERT_NO_FATAL_FAILURE(RollAction(30));
        FRAMES(2);
        EXPECT_GT(GameRoll(), before + 0.3) << "the roll action rolls the plane, as in the game";
        const double after = GameRoll();
        ASSERT_NO_FATAL_FAILURE(RollAction(-10));
        FRAMES(2);
        EXPECT_LT(GameRoll(), after) << "and back";
    }
};

TEST_F(VirtualGamepadHandToolsWristRollAsInTheGameTest, WithHandToolsSwitchedOffTheRollActionWorksAsInTheGame) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    ExpectTheRollActionWorks("vrHandTools off");
}

TEST_F(VirtualGamepadHandToolsWristRollAsInTheGameTest, WithTheToolHandUntrackedTheRollActionWorksAsInTheGameAndTheWristTakesOverWhenItIsTrackedAgain) {
    StartWithChainsawInTheRightHand();
    headset.SetHandTracked(kRight, false);
    ExpectTheRollActionWorks("untracked", false);

    Roll(0.5f);
    headset.SetHandTracked(kRight, true);
    FRAMES(3);
    ASSERT_TRUE(True("VRCompanion:isHandHoldingHandTool(1)"));
    EXPECT_NEAR(std::abs(GameRoll()), 0.5, 2e-3) << "tracked again: the roll is the wrist's, not what the action left";
    const double wrist = GameRoll();
    ASSERT_NO_FATAL_FAILURE(RollAction(30));
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), wrist, 1e-6) << "and the roll action does nothing again";
}

TEST_F(VirtualGamepadHandToolsWristRollAsInTheGameTest, WithTheToolHandSetDownTheRollActionWorksAsInTheGame) {
    StartWithChainsawInTheRightHand();
    AdvanceGameTime(3100);
    ExpectTheRollActionWorks("set down", false);
}

TEST_F(VirtualGamepadHandToolsWristRollTest, ABarPointingStraightDownHasNoRollAndKeepsTheLastOne) {
    StartWithChainsawInTheRightHand();
    Roll(0.6f);
    FRAMES(2);
    const double wrist = GameRoll();
    ASSERT_NEAR(std::abs(wrist), 0.6, 2e-3);

    Roll(0.0f, 0.0f);
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), wrist, 1e-6);
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);

    Roll(0.2f, kLevelBar);
    FRAMES(2);
    EXPECT_NEAR(std::abs(GameRoll()), 0.2, 2e-3);
}

class VirtualGamepadHandToolsWristRollVariantTest : public VirtualGamepadHandToolsWristRollTest {
protected:
    void ExpectOneWarningAndTheGamesChainsaw(const char* variant) {
        ASSERT_NO_FATAL_FAILURE(Start(variant));
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
        Roll(0.0f);
        FRAMES(3);
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "exactly one warning";
        EXPECT_EQ(CountLogLines("HandToolChainsaw.onRollAction"), 1) << "that names the function";
        EXPECT_FALSE(True("VRCompanion:isHandHoldingHandTool(1)")) << "the chainsaw is not in the hand";
        Roll(1.0f);
        FRAMES(3);
        EXPECT_NEAR(GameRoll(), 0.0, 1e-6) << "the wrist rolls nothing";
        FRAMES(5);
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and the warning is not repeated";
    }
};

TEST_F(VirtualGamepadHandToolsWristRollVariantTest, WithTheRollActionsHandlerMissingTheChainsawWorksAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesChainsaw("rollMissing");
}

TEST_F(VirtualGamepadHandToolsWristRollVariantTest, WithTheRollActionsHandlerReshapedTheChainsawWorksAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesChainsaw("rollReshaped");
}

TEST_F(VirtualGamepadHandToolsWristRollTest, TheDiagnosticsReportWhereTheCutRollComesFrom) {
    StartWithChainsawInTheRightHand();
    const std::string line = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(line.find("cut roll from the wrist"), std::string::npos) << line;
    EXPECT_NE(line.find("chainsawOnRoll wrap: installed"), std::string::npos) << line;
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);
    const std::string off = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(off.find("cut roll from the game's roll action"), std::string::npos) << off;
}
