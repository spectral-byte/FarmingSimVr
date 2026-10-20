// Hand Tools, the look ray (ADR-0029), seen from outside: the real Companion Mod runs against the
// real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h, StubHandTool). The
// game's PlayerTargeter reads Player:getLookRay() once per update, as the game's does, and the stub keeps the ray it cast
// (StubHandTool.lastRay()). A test asserts only that ray: where it starts and which way it points, against the head's (the
// ray the game gives without VR) and against the tool's pointing axis (the lance's nozzle). Prior art:
// test_hand_tools_lance.cpp (placement) and test_wheel_grab_steering_wrap.cpp (wrapping a game function).

#include "hand_tools_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;

// Lua helpers: the three rays a test compares. Each is { x, y, z, dirX, dirY, dirZ } in the world.
//   targeterRay()   what the targeter cast in the last game frame
//   headRay()       what the game's own getLookRay answers for the player: the first person camera
//   headRayOfTheGame()   the first person camera, read from the scene (the game's ray in any game version)
//   nozzleRay()     where the lance washes from and along
//   sameRay(a, b)   the two rays start at the same point and point the same way
//   apart(a, b)     metres between where the two rays start
constexpr const char* kRayHelpers = R"luau(
    function _G.targeterRay() return { StubHandTool.lastRay() } end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.nozzleRay() return { StubHandTool.nozzleRay(lance) } end
    function _G.headRayOfTheGame()
        local x, y, z = getWorldTranslation(foot.cam.firstPersonCamera)
        local dx, dy, dz = StubScene.worldDirection(foot.cam.firstPersonCamera, 0, 0, -1)
        return { x, y, z, dx, dy, dz }
    end
    function _G.sameRay(a, b)
        for i = 1, 6 do
            if a[i] == nil or b[i] == nil or math.abs(a[i] - b[i]) > 1e-3 then return false end
        end
        return true
    end
    function _G.apart(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
    function _G.rayText(r) return r[1] and string.format("(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)", table.unpack(r)) or "none" end
)luau";

}  // namespace

// The lance held on foot with the right Motion Controller picked up and a few frames run: the common start below.
#define LANCE_IN_THE_RIGHT_HAND() \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge)); \
    ASSERT_EQ(RunLuauChunk(L, kRayHelpers, "=rayHelpers"), 0); \
    SetGameTime(10000); \
    ASSERT_EQ(RunLuauChunk(L, hand_tools_test::kHoldLance, "=holdLance"), 0); \
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
    FRAMES(2); \
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()")

// Both the aim and the grip pose of the hand set, so the Virtual Hand (grip pose) and the pointer (aim pose) agree.
#define AIM_HAND(hand, x, y, z, pitch, yaw) \
    do { \
        headset.SetHandPose((hand), hand_tools_test::HandAt((x), (y), (z), (pitch), (yaw))); \
        headset.SetHandGripPose((hand), hand_tools_test::HandAt((x), (y), (z), (pitch), (yaw))); \
    } while (0)

// The ray is the tool's, not the head's: it starts at the nozzle, which is not where the head is, and points along it.
TEST_F(CompanionModTest, WithTheLanceInTheToolHandTheTargetersRayStartsAtTheNozzleAndPointsAlongItAndFollowsTheHandAcrossFrames) {
    LANCE_IN_THE_RIGHT_HAND();

    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())") << ", the nozzle is " << EvalLua(L, "rayText(nozzleRay())");
    EXPECT_GT(Number(L, "apart(targeterRay(), headRay())"), 0.2) << "and not from the head: the hand is held away from it";

    // The hand moves and turns; the ray follows (the targeter reads the ray before the mod's update of its frame, so the
    // frame after the one that moved the hand shows it).
    ASSERT_EQ(RunLuauChunk(L, "_G.before = targeterRay(); _G.nozzleBefore = nozzleRay()", "=before"), 0);
    AIM_HAND(kRight, 0.50f, 0.15f, -0.55f, 0.6f, 0.9f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true")
        << "the targeter cast " << EvalLua(L, "rayText(targeterRay())") << ", the nozzle is " << EvalLua(L, "rayText(nozzleRay())");
    EXPECT_EQ(EvalLua(L, "sameRay(nozzleBefore, nozzleRay())"), "false") << "the nozzle moved with the hand";
    EXPECT_GT(Number(L, "apart(before, targeterRay())"), 0.3) << "so the ray starts somewhere else now";
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "false");

    // It points the way the hand points: along the nozzle, which is a unit direction that is not the head's.
    EXPECT_NEAR(Number(L, "targeterRay()[4]^2 + targeterRay()[5]^2 + targeterRay()[6]^2"), 1.0, 1e-3);
    EXPECT_LT(Number(L, "targeterRay()[4]*headRay()[4] + targeterRay()[5]*headRay()[5] + targeterRay()[6]*headRay()[6]"), 0.99)
        << "turned away from where the head looks";
}

// Everything that is not "a supported tool in a tracked Tool Hand" is the game's own ray, from the head.
TEST_F(CompanionModTest, WhileTheToolHandIsSetDownOrUntrackedTheTargetersRayIsTheHeadsAndTheToolsAgainWhenItIsPickedUp) {
    LANCE_IN_THE_RIGHT_HAND();

    AdvanceGameTime(3100);  // set down: the Motion Controller lies still for longer than the in-use timeout
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true")
        << "set down: the targeter cast " << EvalLua(L, "rayText(targeterRay())") << ", the head's is " << EvalLua(L, "rayText(headRay())");

    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true") << "picked up again: the tool's ray";

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "untracked: the head's ray";
    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true") << "tracked again: the tool's ray";

    // Only the grip pose lost: there is no Virtual Hand to hold the tool, so the tool is not in the hand.
    headset.SetHandGripPose(kRight, HandAt(0.25f, -0.10f, -0.30f), /*tracked=*/false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true");
}

TEST_F(CompanionModTest, WithAnUnsupportedHandToolOrNoHandToolHeldTheTargetersRayIsTheHeads) {
    LANCE_IN_THE_RIGHT_HAND();

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); _G.otherTool = StubHandTool.otherModTool(foot.player); StubHandTool.hold(otherTool)", "=otherTool"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true")
        << "another mod's tool is not supported: the targeter cast " << EvalLua(L, "rayText(targeterRay())");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(otherTool)", "=putAway"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "no Hand Tool held";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.hold(lance)", "=lanceAgain"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true") << "the lance taken again: the tool's ray";
}

TEST_F(CompanionModTest, ALeftMotionControllerPickedUpDoesNotMoveTheTargetersRayToTheLance) {
    FakeTrackedHeadset headset;
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kRayHelpers, "=rayHelpers"), 0);
    SetGameTime(10000);
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true");
}

TEST_F(CompanionModTest, WithVrHandToolsOffOrMotionControllersOffTheTargetersRayIsTheHeadsAtOnce) {
    LANCE_IN_THE_RIGHT_HAND();

    // At once: the targeter's very next update, with no mod update in between.
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off'); StubHandTool.tick(16.667)", "=off"), 0);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "vrHandTools off: the head's ray";
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "and it stays the head's however the hand moves";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true") << "on again: the tool's ray";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off'); StubHandTool.tick(16.667)", "=mcOff"), 0);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "Motion Controllers off: the head's ray";
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=mcOn"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true") << "Motion Controllers on again: the tool's ray";
}

// Only the player who holds the tool aims it: another player's look ray (a remote player's, say) is the game's own.
TEST_F(CompanionModTest, AnotherPlayersLookRayIsNotTheLances) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.other = setmetatable({ camera = foot.cam }, { __index = Player })
        _G.otherRay = { other:getLookRay() }
    )luau", "=otherPlayer"), 0);

    EXPECT_EQ(EvalLua(L, "sameRay(otherRay, headRay())"), "true");
}

// A game whose getLookRay is not what the wrap expects: the targeter works as in the game, and the log says so once
// (the shared give-up of Hand Tools: ADR-0029, as ADR-0026 and ADR-0027).
#define HAND_TOOLS_GIVE_UP_WHEN_GET_LOOK_RAY_IS(gameVariant) \
    do { \
        FakeTrackedHeadset headset; \
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
        fs25vr::VRBridge bridge(headset.manager, nullptr); \
        StartOptions options; \
        options.lookRayVariant = (gameVariant); \
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options)); \
        ASSERT_EQ(RunLuauChunk(L, kRayHelpers, "=rayHelpers"), 0); \
        SetGameTime(10000); \
        ASSERT_EQ(RunLuauChunk(L, hand_tools_test::kHoldLance, "=holdLance"), 0); \
        \
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
        FRAMES(3); \
        \
        EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRayOfTheGame())"), "true") \
            << "the targeter works as in the game: it cast " << EvalLua(L, "rayText(targeterRay())"); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "one warning says why"; \
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(lance); StubHandTool.hold(lance)", "=takeAgain"), 0); \
        PICK_UP(kRight, 0.30f, -0.10f, -0.30f); \
        FRAMES(3); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and it is not repeated"; \
        EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRayOfTheGame())"), "true"); \
        EXPECT_NE(EvalLua(L, "VRCompanion:getHandToolsDiagnostics()").find("getLookRay wrap: gave up"), std::string::npos) \
            << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); \
    } while (0)


TEST_F(CompanionModTest, WithGetLookRayMissingTheTargeterWorksAsInTheGameAndTheLogHasOneWarning) {
    HAND_TOOLS_GIVE_UP_WHEN_GET_LOOK_RAY_IS("missing");
    EXPECT_EQ(EvalLua(L, "Player.getLookRay"), "nil") << "the class is as the game made it";
}

TEST_F(CompanionModTest, WithGetLookRayReshapedTheTargeterWorksAsInTheGameAndTheLogHasOneWarning) {
    HAND_TOOLS_GIVE_UP_WHEN_GET_LOOK_RAY_IS("reshaped");
}

// The wrap is on the class while the map is loaded and gone with it; another mod's later wrap is never undone.
TEST_F(CompanionModTest, UnloadingTheMapGivesPlayerGetLookRayBackAndAnotherModsLaterWrapIsNeverUndone) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(EvalLua(L, "Player.getLookRay == StubHandTool.originalGetLookRay"), "false") << "wrapped while the map is loaded";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "Player.getLookRay == StubHandTool.originalGetLookRay"), "true") << "the class has its own function back";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap('y')
        local inner = Player.getLookRay
        _G.theirWrap = function(...) return inner(...) end
        Player.getLookRay = _G.theirWrap
        VRCompanion:deleteMap()
    )luau", "=anotherModWraps"), 0);
    EXPECT_EQ(EvalLua(L, "Player.getLookRay == theirWrap"), "true") << "the other mod's wrap is still the class function";
}

// The wrap of Player:getLookRay asks each subsystem that can aim, in a fixed order (Hand Tools first, then Object Grab), and
// takes the first ray it gets; the game's own (the head's) is the fallback. A subsystem registers with
// VRCompanion:registerLookRayAimer(name, function(player) -> x, y, z, dirX, dirY, dirZ | nil).
constexpr const char* kFixedAim = R"luau(
    _G.fixedAim = { 0.5, 1.25, -2.0, 0, 0, -1 }
    _G.aimerAsked = 0
    function _G.aimerAnswers(player)
        _G.aimerAsked = _G.aimerAsked + 1
        _G.aimerAskedFor = player
        return table.unpack(_G.fixedAim)
    end
)luau";

#define NO_HAND_TOOL_START() \
    FakeTrackedHeadset headset; \
    headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f)); \
    fs25vr::VRBridge bridge(headset.manager, nullptr); \
    ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge)); \
    ASSERT_EQ(RunLuauChunk(L, kRayHelpers, "=rayHelpers"), 0); \
    ASSERT_EQ(RunLuauChunk(L, kFixedAim, "=fixedAim"), 0); \
    SetGameTime(10000)

TEST_F(CompanionModTest, ASubsystemThatAimsAnswersTheTargetersRayAndTheHeadsIsTheFallbackWhenItDoesNot) {
    NO_HAND_TOOL_START();
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "nobody aims: the head's ray";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:registerLookRayAimer('objectGrab', aimerAnswers)", "=register"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), fixedAim)"), "true") << "it aims: the targeter cast " << EvalLua(L, "rayText(targeterRay())");
    EXPECT_EQ(EvalLua(L, "aimerAskedFor == foot.player"), "true") << "it is asked for the player whose ray the game wants";

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:registerLookRayAimer('objectGrab', function() return nil end)", "=registerAgain"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "it no longer aims: the head's ray again";
}

TEST_F(CompanionModTest, TheHandToolsAreAskedFirstSoTheToolsRayWinsOverAnotherSubsystemsAndIsNeverAskedAround) {
    LANCE_IN_THE_RIGHT_HAND();
    ASSERT_EQ(RunLuauChunk(L, kFixedAim, "=fixedAim"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:registerLookRayAimer('objectGrab', aimerAnswers)", "=register"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), nozzleRay())"), "true") << "the tool's ray: " << EvalLua(L, "rayText(targeterRay())");
    EXPECT_EQ(EvalLua(L, "aimerAsked"), "0") << "the first answer ends the asking";

    AdvanceGameTime(3100);  // the lance is set down: the Hand Tools do not aim, so the other subsystem does
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), fixedAim)"), "true") << "the targeter cast " << EvalLua(L, "rayText(targeterRay())");
}

TEST_F(CompanionModTest, ASubsystemThatRaisesWhenAskedLeavesTheTargetersRayTheHeads) {
    NO_HAND_TOOL_START();
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:registerLookRayAimer('objectGrab', function() error('broken') end)", "=register"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the targeter cast " << EvalLua(L, "rayText(targeterRay())");
}

TEST_F(CompanionModTest, ThereIsStillOneWrapOfGetLookRayHoweverManySubsystemsAreRegistered) {
    NO_HAND_TOOL_START();
    ASSERT_EQ(RunLuauChunk(L, "_G.wrapped = Player.getLookRay", "=wrapped"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:registerLookRayAimer('objectGrab', aimerAnswers); VRCompanion:loadMap('again')", "=register"), 0);
    EXPECT_EQ(EvalLua(L, "Player.getLookRay == wrapped"), "true") << "registering does not wrap again";
    EXPECT_EQ(EvalLua(L, "Player.getLookRay == StubHandTool.originalGetLookRay"), "false");
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "Player.getLookRay == StubHandTool.originalGetLookRay"), "true") << "and one lift gives the game's function back";
}
