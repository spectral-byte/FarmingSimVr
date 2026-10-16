// Hand Tools, the wrist's roll sets the chainsaw's cut angle (ADR-0029), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h:
// StubHandTool and its chainsaw, kStubHandToolChainsaw, whose update takes the roll action's input and lays the cut plane out
// from the roll). The test rolls the fake Motion Controller about the chainsaw's bar and asserts what the game or the player
// would see: the plane the game would cut along, the roll the game holds for it, the plane a finished cut sliced along, what
// the roll action (AXIS_ROTATE_HANDTOOL, HandToolChainsaw.onRollAction) does, the stub's input locks, the log. Never the mod's
// own state.
// Prior art: test_hand_tools_chainsaw.cpp (the chainsaw by its bar).

#include "hand_tools_test_support.h"
#include <cmath>
#include <string>

namespace {

using namespace hand_tools_test;

constexpr double kPi = 3.14159265358979323846;

// The bar exactly level: with the controller pitched a quarter turn, the fingers (and the bar along them) point along the
// ground. Rolling the wrist is then a turn about the controller's own long axis; the pose is built from a quaternion so that
// the roll is a turn about that axis whatever the heading.
constexpr float kLevelBar = static_cast<float>(kPi / 2.0);
// A little less than level: the bar points slightly down, as a hand holds it.
constexpr float kBarForward = 1.4f;
constexpr float kHeading = 0.9f;

// Lua helpers: the planes and directions a test compares.
//   barDir()          the way the chainsaw's bar points
//   toolUp()          the way the chainsaw's own up points (the thumb side of the closed hand)
//   dot(a, b)         the dot product of two directions { x, y, z }
//   planeNormal()     the normal of the plane the game would cut along now
//   roll()            the roll the game holds for the cut plane
//   state()           the chainsaw's cut state: "idle", "cutting" or "delimbing"
//   headRay()         what the game's own getLookRay answers for the player: the first person camera
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

// A hand at (x, y, z) pitched and turned to `heading`, then rolled by `twist` radians about the bar, aim and grip pose the
// same. The chainsaw sits in the hand with its bar halfway between the fingers and the thumb (a quarter of a half turn about
// the hand's X axis from the fingers, HAND_TOOL_HANDLE_OFFSETS.chainsaw), so the controller is first turned the other way by
// that much: the pose that puts the BAR where the test says, as a hand that holds the saw would.
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

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsWristRollTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The mod started in a game whose chainsaw is as `variant` says, the player on foot, the Virtual Gamepad on and the game
    // clock set (the in-use timeout is on it).
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

    // The right Tool Hand held with the bar at `pitch` and rolled by `twist`.
    void Roll(float twist, float pitch = kLevelBar) {
        const fs25vr::HeadPose pose = RolledPose(0.25f, -0.35f, -0.30f, pitch, kHeading, twist);
        headset.SetHandPose(kRight, pose);
        headset.SetHandGripPose(kRight, pose);
    }

    // The chainsaw in the right Tool Hand with its bar level and the wrist not rolled.
    void StartWithChainsawInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
        Roll(0.0f);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    // A trunk the chainsaw's bar is against.
    void PlantTreeAtTheBar() {
        ASSERT_EQ(RunLuauChunk(L, "_G.barTree = StubHandTool.treeAcross(barRay(), 0.6, 0.15)", "=plantTree"), 0);
    }

    // The angle in radians between two directions.
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
    // The roll the game holds, as the stub's chainsaw has it.
    double GameRoll() { return Number(L, "roll()"); }
};

}  // namespace

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
    const double side = GameRoll() > 0.0 ? 1.0 : -1.0;  // which way a roll of the wrist one way goes

    Roll(2.0f);  // 115 degrees the same way
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), side * kPi / 2.0, 1e-4);
    EXPECT_NEAR(std::abs(Number(L, "planeNormal()[2]")), 1.0, 1e-3) << "a flat plane: the most the game allows";

    Roll(-2.0f);
    FRAMES(2);
    EXPECT_NEAR(GameRoll(), -side * kPi / 2.0, 1e-4);

    Roll(1.5f);  // inside the range again: the roll is the wrist's
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

    for (const float twist : { 1.0f, -0.6f, 0.0f }) {  // the bar stays against the trunk: only the wrist turns
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

    // The cut is over: the next one takes the wrist's roll again.
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

    // Not in a cut either, and what the action does not do leaves the cut's input lock as the cut alone has it.
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
    // The game's action event holds the handler it was given when the tool was equipped; an input that reaches the chainsaw
    // without going through the class's handler (here: put in the tool's own field) must not roll the plane either.
    StartWithChainsawInTheRightHand();
    Roll(0.3f);
    FRAMES(2);
    const double wrist = GameRoll();
    ASSERT_NEAR(std::abs(wrist), 0.3, 2e-3);
    ASSERT_EQ(RunLuauChunk(L, "saw.spec_chainsaw.rollInput = 30", "=staleInput"), 0);
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), wrist, 1e-6);
}

// The three ways a Hand Tool is not in a tracked Tool Hand, and the roll action works as in the game.
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
    AdvanceGameTime(3100);  // the controller lies still for longer than the in-use timeout
    ExpectTheRollActionWorks("set down", false);
}

TEST_F(VirtualGamepadHandToolsWristRollTest, ABarPointingStraightDownHasNoRollAndKeepsTheLastOne) {
    StartWithChainsawInTheRightHand();
    Roll(0.6f);
    FRAMES(2);
    const double wrist = GameRoll();
    ASSERT_NEAR(std::abs(wrist), 0.6, 2e-3);

    Roll(0.0f, 0.0f);  // the fingers straight down
    FRAMES(3);
    EXPECT_NEAR(GameRoll(), wrist, 1e-6);
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);

    Roll(0.2f, kLevelBar);  // and level again: the wrist's roll again
    FRAMES(2);
    EXPECT_NEAR(std::abs(GameRoll()), 0.2, 2e-3);
}

// A game update that renames or reshapes the roll action's handler: one warning, and the chainsaw is the game's.
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
