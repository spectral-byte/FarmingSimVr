// Hand Tools, no screen-centre overlays while a Hand Tool is in the Tool Hand (ADR-0030), seen from outside: the real Companion
// Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h,
// StubHandTool: a player on foot, the game's HandToolSprayCan and HandToolChainsaw with the onDraw each has, and the game's draw,
// StubHandTool.draw, which calls the class's onDraw for every held tool). A test moves the fake Motion Controllers, runs game
// frames and draws, and asserts what the game rendered for each tool (StubHandTool.rendering), the warning lines in the log and
// the diagnostics line. Never the mod's own state.
// Prior art: test_hand_tools_chainsaw.cpp (class-level wraps with install variants and the one warning) and
// test_hand_tools_spray_can.cpp (the can in the Tool Hand, another mod's tool, another player's tool).

#include "hand_tools_test_support.h"
#include <string>

namespace {

using namespace hand_tools_test;

// The two tools whose onDraw is wrapped. `holdLua` takes the tool in hand (as global `global`); `inTheGame` is what the game
// renders for it with the head's view free of the Tool Hand (the can with a tree found by the head's ray, the saw idle);
// `remoteMaker` makes the tool of another player; `function` is the name the warning gives.
struct DrawnTool {
    const char* holdLua;
    const char* global;
    const char* inTheGame;
    const char* remoteMaker;
    const char* remoteInTheGame;
    const char* function;
};

const DrawnTool kSprayCan{ kHoldSprayCan, "can", "markerTypeIcon+treeIcon", "sprayCan", "markerTypeIcon", "HandToolSprayCan.onDraw" };
const DrawnTool kChainsaw{ kHoldChainsaw, "saw", "crosshair", "chainsaw", "crosshair", "HandToolChainsaw.onDraw" };

// The hand held so that the tool points level and well away from the way the head looks (down -Z).
constexpr float kAimPitch = 1.4f;
constexpr float kAimYaw = 1.2f;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsDrawTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    // The mod started in a game whose spray can and chainsaw draw are as the variants say ("ok", "missing", "reshaped" for the
    // can; "ok", "drawMissing", "drawReshaped" for the saw), the player on foot and the game clock set (the in-use timeout is on it).
    void Start(const char* canVariant = "ok", const char* sawVariant = "ok") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = SprayCanStubs(canVariant) + ChainsawStubs(sawVariant);
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        SetGameTime(10000);
    }

    void AimTheHand(uint32_t hand, float x, float y, float z) {
        headset.SetHandPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
    }

    // The tool taken in hand with the right Motion Controller picked up. Trees are planted where the head looks (found by the
    // game's own targeter when the tool is not in the hand) and, for the can, at its nozzle, so that the can has a tree found
    // wherever it is held and would draw its tree icon.
    void TakeToolInTheRightHand(const DrawnTool& tool) {
        ASSERT_EQ(RunLuauChunk(L, tool.holdLua, "=holdTool"), 0);
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            StubHandTool.trees = {}
            _G.headTree = StubHandTool.treeAt({ StubHandTool.originalGetLookRay(foot.player) }, 1.0, 0.15)
            if _G.can ~= nil then _G.handTree = StubHandTool.treeAt({ StubHandTool.canRay(can) }, 0.4, 0.15) end
        )luau", "=plantTrees"), 0);
        FRAMES(2);
    }

    bool InTheHand() { return EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)") == "true"; }

    // What the game renders for the tool in one frame's draw.
    std::string Draws(const DrawnTool& tool) {
        EXPECT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
        return EvalLua(L, std::string("StubHandTool.rendering(") + tool.global + ")");
    }

    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }

    void SetTheToolHandDown() {
        AdvanceGameTime(3100);  // the Motion Controller lies still for longer than the in-use timeout
        FRAMES(2);
    }

    void ExpectNothingIsDrawnInTheHandAndTheGamesOverlaysOtherwise(const DrawnTool& tool) {
        ASSERT_NO_FATAL_FAILURE(TakeToolInTheRightHand(tool));
        ASSERT_TRUE(InTheHand()) << Dump();
        if (std::string(tool.global) == "can") {
            ASSERT_EQ(EvalLua(L, "tostring(can.spec_sprayCan.foundTargetTree ~= nil)"), "true") << "the can has a tree found, which the game would draw an icon for";
        }
        EXPECT_EQ(Draws(tool), "") << "in the tracked Tool Hand nothing is drawn at the centre of the view";

        headset.SetHandTracked(kRight, false);
        FRAMES(2);
        EXPECT_EQ(Draws(tool), tool.inTheGame) << "untracked: as the game draws it";
        headset.SetHandTracked(kRight, true);
        FRAMES(2);
        ASSERT_TRUE(InTheHand());
        EXPECT_EQ(Draws(tool), "") << "tracked again: nothing";

        SetTheToolHandDown();
        EXPECT_EQ(Draws(tool), tool.inTheGame) << "set down: as the game draws it";
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(2);
        ASSERT_TRUE(InTheHand());
        EXPECT_EQ(Draws(tool), "") << "picked up again: nothing";

        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
        FRAMES(2);
        EXPECT_EQ(Draws(tool), tool.inTheGame) << "vrHandTools off: as the game draws it";
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
        FRAMES(2);
        EXPECT_EQ(Draws(tool), "") << "vrHandTools on: nothing again";

        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=mcOff"), 0);
        FRAMES(2);
        EXPECT_EQ(Draws(tool), tool.inTheGame) << "Motion Controllers off: as the game draws it";
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'on')", "=mcOn"), 0);
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);  // the Motion Controllers start again with the hands set down
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(2);
        ASSERT_TRUE(InTheHand());
        EXPECT_EQ(Draws(tool), "") << "Motion Controllers on: nothing again";
    }

    // Another player's tool of the same kind, held in a multiplayer session: the game draws it, whatever the local player's tool
    // is doing.
    void ExpectAnotherPlayersToolIsDrawnAsInTheGame(const DrawnTool& tool) {
        ASSERT_NO_FATAL_FAILURE(TakeToolInTheRightHand(tool));
        ASSERT_TRUE(InTheHand());
        const std::string lua = std::string(R"luau(
            local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
            local remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
            _G.remoteTool = StubHandTool.)luau") + tool.remoteMaker + R"luau((remote)
            remoteTool.isHeld = true
            remoteTool.carryingPlayer = remote
            remoteTool:attachTool()
        )luau";
        ASSERT_EQ(RunLuauChunk(L, lua.c_str(), "=holdRemoteTool"), 0);
        FRAMES(2);

        EXPECT_EQ(Draws(tool), "") << "the local player's tool, in the Tool Hand: nothing";
        EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(remoteTool)"), tool.remoteInTheGame) << "the other player's tool: as the game draws it";
    }

    // A game update that renames or reshapes the tool's draw: one warning, and the tool is drawn as in the game.
    void ExpectOneWarningAndTheGamesOverlay(const DrawnTool& tool, const char* canVariant, const char* sawVariant) {
        ASSERT_NO_FATAL_FAILURE(Start(canVariant, sawVariant));
        ASSERT_NO_FATAL_FAILURE(TakeToolInTheRightHand(tool));

        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "exactly one warning";
        EXPECT_EQ(CountLogLines(tool.function), 1) << "that names the function";
        EXPECT_FALSE(InTheHand()) << "Hand Tools gave up: the tool is where the game puts it";
        EXPECT_EQ(Draws(tool), tool.inTheGame) << "and it is drawn as in the game";
        FRAMES(5);
        EXPECT_EQ(Draws(tool), tool.inTheGame);
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and the warning is not repeated";
    }
};

}  // namespace

// ---- Nothing is drawn at the centre of the view while the tool is in the tracked Tool Hand -------------------------------------

TEST_F(VirtualGamepadHandToolsDrawTest, TheSprayCanDrawsNothingInTheTrackedToolHandAndAsInTheGameWhenUntrackedSetDownOrHandToolsOrMotionControllersAreOff) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ExpectNothingIsDrawnInTheHandAndTheGamesOverlaysOtherwise(kSprayCan);
}

TEST_F(VirtualGamepadHandToolsDrawTest, TheChainsawDrawsNothingInTheTrackedToolHandAndAsInTheGameWhenUntrackedSetDownOrHandToolsOrMotionControllersAreOff) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ExpectNothingIsDrawnInTheHandAndTheGamesOverlaysOtherwise(kChainsaw);
}

// ---- Tools the Companion Mod does not move are drawn as in the game -----------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsDrawTest, AnotherModsToolIsDrawnAsInTheGameWhileTheSprayCanAndChainsawDrawsAreWrapped) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_FALSE(InTheHand());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(otherTool)"), "otherModGauge");
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 0);
}

TEST_F(VirtualGamepadHandToolsDrawTest, AnotherPlayersSprayCanIsDrawnAsInTheGameWhileTheLocalPlayersCanInTheToolHandDrawsNothing) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ExpectAnotherPlayersToolIsDrawnAsInTheGame(kSprayCan);
}

TEST_F(VirtualGamepadHandToolsDrawTest, AnotherPlayersChainsawIsDrawnAsInTheGameWhileTheLocalPlayersSawInTheToolHandDrawsNothing) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ExpectAnotherPlayersToolIsDrawnAsInTheGame(kChainsaw);
}

// ---- A game update that renames or reshapes onDraw ------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsDrawTest, WithTheSprayCansDrawMissingTheCanIsDrawnAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesOverlay(kSprayCan, "missing", "ok");
}

TEST_F(VirtualGamepadHandToolsDrawTest, WithTheSprayCansDrawReshapedTheCanIsDrawnAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesOverlay(kSprayCan, "reshaped", "ok");
}

TEST_F(VirtualGamepadHandToolsDrawTest, WithTheChainsawsDrawMissingTheSawIsDrawnAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesOverlay(kChainsaw, "ok", "drawMissing");
}

TEST_F(VirtualGamepadHandToolsDrawTest, WithTheChainsawsDrawReshapedTheSawIsDrawnAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesOverlay(kChainsaw, "ok", "drawReshaped");
}

TEST_F(VirtualGamepadHandToolsDrawTest, AGameUpdateThatReshapesTheDrawLiftsEveryWrapNotOnlyTheDrawsOwn) {
    ASSERT_NO_FATAL_FAILURE(Start("reshaped", "ok"));
    ASSERT_NO_FATAL_FAILURE(TakeToolInTheRightHand(kChainsaw));

    EXPECT_FALSE(InTheHand());
    EXPECT_EQ(EvalLua(L, "tostring(HandTool.attachToolToCamera == StubHandTool.originalAttachToolToCamera)"), "true") << "the placement wrap is lifted too";
    EXPECT_EQ(EvalLua(L, "tostring(Player.getLookRay == StubHandTool.originalGetLookRay)"), "true") << "and the look ray wrap";
    EXPECT_EQ(Draws(kChainsaw), kChainsaw.inTheGame);
}

// ---- Diagnostics ------------------------------------------------------------------------------------------------------------

TEST_F(VirtualGamepadHandToolsDrawTest, TheDiagnosticsShowTheDrawWrapsAndWhetherDrawingIsHeldBackNow) {
    ASSERT_NO_FATAL_FAILURE(Start());
    EXPECT_NE(Dump().find("centre overlays held back: no"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("sprayCanOnDraw wrap: installed"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("chainsawOnDraw wrap: installed"), std::string::npos) << Dump();

    ASSERT_NO_FATAL_FAILURE(TakeToolInTheRightHand(kSprayCan));
    EXPECT_NE(Dump().find("centre overlays held back: yes"), std::string::npos) << Dump();
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolsDiagnostics')").find("centre overlays held back: yes"), std::string::npos);

    SetTheToolHandDown();
    EXPECT_NE(Dump().find("centre overlays held back: no"), std::string::npos) << Dump();
}

TEST_F(VirtualGamepadHandToolsDrawTest, TheDiagnosticsSayTheDrawWrapsGaveUpAndNothingIsHeldBackWhenOnDrawIsReshaped) {
    ASSERT_NO_FATAL_FAILURE(Start("ok", "drawReshaped"));
    ASSERT_NO_FATAL_FAILURE(TakeToolInTheRightHand(kSprayCan));

    EXPECT_NE(Dump().find("sprayCanOnDraw wrap: gave up"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("chainsawOnDraw wrap: gave up"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("centre overlays held back: no"), std::string::npos) << Dump();
}

TEST_F(VirtualGamepadHandToolsDrawTest, TheLanceInTheToolHandHasNoCentreOverlayToHoldBackAndIsNotReportedAsHeldBack) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    ASSERT_TRUE(InTheHand()) << Dump();
    EXPECT_NE(Dump().find("centre overlays held back: no"), std::string::npos) << Dump();
}
