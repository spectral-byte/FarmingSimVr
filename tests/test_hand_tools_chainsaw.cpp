// Hand Tools, the chainsaw (spec #109, ticket #114, ADR-0029), seen from outside: the real Companion Mod runs against the real
// VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (companion_mod_stubs.h: StubHandTool and its chainsaw,
// kStubHandToolChainsaw: the game's HandToolChainsaw with its update, cut handler and cut-state machine, trees for the
// targeter to find). The test moves the fake Motion Controllers, drives frames the way the game would and asserts what the
// game or the player would see: where the targeter found a tree, where the chainsaw's model points, the node the cut plane
// hangs from and the plane the game would cut along, the chainsaw's cut state and the cuts it made, the calls of its own cut
// handler, what XInput answers for the Tool Hand's trigger, the log. Never the mod's own state.
// Prior art: test_hand_tools_trigger.cpp (the Tool Hand's trigger), test_hand_tools_look_ray.cpp (the targeter's ray) and
// test_wheel_grab_steering_wrap.cpp (a wrapped game function that is missing or reshaped).

#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

// Lua helpers: the rays and planes a test compares. A ray is { x, y, z, dirX, dirY, dirZ } in the world.
//   barRay()          where the chainsaw's bar points (the root's origin and +Z)
//   headRay()         what the game's own getLookRay answers for the player: the first person camera
//   modelDir()        the way the chainsaw's model points
//   dot(a, b)         the dot product of two directions { x, y, z }
//   dirOf(ray)        the direction of a ray
//   horizontal(d)     a direction flattened to the ground and made a unit vector
//   hitHeight(ray, distance)   the height a ray reaches at `distance` metres along the ground
//   cutNodeY()        the height of the node the cut plane hangs from
//   planeNormal()     the normal of the plane the game would cut along now
//   planeOrigin()     a point on it
//   state()           the chainsaw's cut state: "idle", "cutting" or "delimbing"
constexpr const char* kChainsawHelpers = R"luau(
    function _G.barRay() return { StubHandTool.barRay(saw) } end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.modelDir() return { StubHandTool.modelDirection(saw) } end
    function _G.dirOf(ray) return { ray[4], ray[5], ray[6] } end
    function _G.dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
    function _G.horizontal(d)
        local length = math.sqrt(d[1] ^ 2 + d[3] ^ 2)
        return { d[1] / length, 0, d[3] / length }
    end
    function _G.hitHeight(ray, distance)
        return ray[2] + ray[5] * distance / math.sqrt(ray[4] ^ 2 + ray[6] ^ 2)
    end
    function _G.cutNodeY()
        local _, y = getWorldTranslation(saw.spec_chainsaw.cameraRotationNode)
        return y
    end
    function _G.planeNormal()
        local _, _, _, nx, ny, nz = saw:calculateCutPlane()
        return { nx, ny, nz }
    end
    function _G.planeOrigin()
        local x, y, z = saw:calculateCutPlane()
        return { x, y, z }
    end
    function _G.state()
        local s = saw.spec_chainsaw.currentCutState
        if s == ChainsawCutState.CUTTING then return "cutting" end
        if s == ChainsawCutState.DELIMBING then return "delimbing" end
        return "idle"
    end
)luau";

// With the controller upright the fingers of the grip frame, and the bar along them, point down; pitched a quarter turn the bar
// points along the ground. A little less than that leaves it slightly off level.
constexpr float kBarForward = 1.4f;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) already covers.
class VirtualGamepadHandToolsChainsawTest : public CompanionModTest {
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
        ASSERT_EQ(RunLuauChunk(L, kChainsawHelpers, "=chainsawHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    // Both the aim and the grip pose of the hand set, so the Virtual Hand (grip pose) and the targeter's hand agree.
    void AimHand(uint32_t hand, float x, float y, float z, float pitch, float yaw) {
        headset.SetHandPose(hand, HandAt(x, y, z, pitch, yaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, pitch, yaw));
    }

    // The chainsaw in the right Tool Hand, picked up with the trigger let go and turned away from where the head looks, so that
    // what follows the hand and what follows the head are told apart.
    void StartWithChainsawInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
        AimHand(kRight, 0.25f, -0.35f, -0.30f, kBarForward, 0.9f);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    // A trunk the chainsaw's bar is against (its surface `distance` metres along the bar) and one the head looks straight at.
    void PlantTreesAtTheBarAndInFrontOfTheHead(double barDistance = 0.6, double headDistance = 1.2) {
        const std::string lua = "_G.barTree = StubHandTool.treeAcross(barRay(), " + std::to_string(barDistance) + ", 0.15); "
                                "_G.headTree = StubHandTool.treeAcross(headRay(), " + std::to_string(headDistance) + ", 0.15)";
        ASSERT_EQ(RunLuauChunk(L, lua.c_str(), "=plantTrees"), 0);
    }

    void StartWithChainsawAndTreesInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(StartWithChainsawInTheRightHand());
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
        FRAMES(2);
    }

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    std::string Dump(const char* expression) { return EvalLua(L, expression); }
    bool True(const char* expression) { return EvalLua(L, expression) == "true"; }
    std::string State() { return EvalLua(L, "state()"); }
    int UseCalls() { return static_cast<int>(Number(L, "#StubHandTool.useCalls")); }
    int Cuts() { return static_cast<int>(Number(L, "#StubHandTool.cuts")); }
    void ForgetUseCalls() { ASSERT_EQ(RunLuauChunk(L, "StubHandTool.useCalls = {}", "=forgetUseCalls"), 0); }
};

}  // namespace

TEST_F(VirtualGamepadHandToolsChainsawTest, TheChainsawInTheToolHandFollowsTheHandAndItsModelPointsAlongTheHandNotWhereTheHeadLooks) {
    StartWithChainsawInTheRightHand();

    EXPECT_TRUE(True("isBelow(saw.rootNode, handHolder('right'))")) << "the chainsaw's root hangs below the Virtual Hand";
    EXPECT_GT(Number(L, "dot(modelDir(), dirOf(barRay()))"), 0.999) << "the model points along the bar, which is the hand's";
    EXPECT_LT(Number(L, "dot(modelDir(), dirOf(headRay()))"), 0.95) << "and not where the head looks (the game turns it there)";

    // The hand moves and turns; the model goes on pointing along it.
    ASSERT_EQ(RunLuauChunk(L, "_G.before = modelDir()", "=before"), 0);
    AimHand(kRight, 0.45f, -0.20f, -0.50f, kBarForward - 0.1f, -0.6f);
    FRAMES(2);
    EXPECT_GT(Number(L, "dot(modelDir(), dirOf(barRay()))"), 0.999);
    EXPECT_LT(Number(L, "dot(modelDir(), before)"), 0.95) << "it turned with the hand";
    EXPECT_LT(Number(L, "dot(modelDir(), dirOf(headRay()))"), 0.95);
}


TEST_F(VirtualGamepadHandToolsChainsawTest, TheTreeTheBarIsAgainstIsTheTargetedOneNotTheOneTheHeadLooksAtAndTheRingMarkerSitsWhereTheBarMeetsIt) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);

    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil")) << "a tree is targeted";
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree.node == barTree.node")) << "the trunk the bar is against";
    EXPECT_FALSE(True("saw.spec_chainsaw.targetedTree.node == headTree.node")) << "not the one in front of the face";

    ASSERT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the game shows its ring marker";
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[1] - barTree.x"), 0.0, 1e-3) << "on the trunk the bar is against";
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[3] - barTree.z"), 0.0, 1e-3);
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[2] - hitHeight(barRay(), 0.6)"), 0.0, 1e-2)
        << "at the height where the bar meets it";

    // Take the bar off that trunk: the head's tree is the one in the way of nothing, so nothing is targeted once the bar points away.
    AimHand(kRight, 0.25f, -0.35f, -0.30f, kBarForward, 2.8f);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the bar points at no trunk, and the head's does not count";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)"));
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheCutHeightIsWhereTheBarMeetsTheTrunkAndThePlaneIsAlignedWithTheBarNotTheCamera) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);

    EXPECT_NEAR(Number(L, "cutNodeY()"), Number(L, "hitHeight(barRay(), 0.6)"), 1e-2) << "the node the cut plane hangs from is where the bar meets the trunk";
    EXPECT_GT(std::abs(Number(L, "cutNodeY() - hitHeight(headRay(), 1.2)")), 0.1) << "not where the head's ray meets its trunk";
    EXPECT_NEAR(Number(L, "planeOrigin()[2] - cutNodeY()"), 0.0, 1e-3);

    // The plane's heading is the bar's: its normal runs across the bar's heading, not across the camera's.
    EXPECT_NEAR(Number(L, "dot(planeNormal(), horizontal(dirOf(barRay())))"), 0.0, 0.02);
    EXPECT_GT(std::abs(Number(L, "dot(planeNormal(), horizontal(dirOf(headRay())))")), 0.4);

    // The hand moves its bar to the same trunk from another side of the hand: the plane follows the bar.
    ASSERT_EQ(RunLuauChunk(L, "_G.before = planeNormal()", "=before"), 0);
    AimHand(kRight, 0.25f, -0.35f, -0.30f, kBarForward, 1.2f);
    FRAMES(2);
    EXPECT_LT(Number(L, "dot(planeNormal(), before)"), 0.99) << "the plane turned with the bar";
    EXPECT_NEAR(Number(L, "dot(planeNormal(), horizontal(dirOf(barRay())))"), 0.0, 0.02);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, HoldingTheToolHandsTriggerStartsACutOnTheTargetedTreeKeepsCuttingAndReleasingStopsItWhileTheTriggerReadsReleasedOnThePad) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    ForgetUseCalls();
    ASSERT_EQ(State(), "idle");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getChainsawCutState()"), "idle");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(State(), "cutting") << "the pull starts a cut";
    EXPECT_TRUE(True("saw.spec_chainsaw.cuttingTreeNode == barTree.node")) << "on the tree the bar is against";
    EXPECT_EQ(EvalLua(L, "VRCompanion:getChainsawCutState()"), "cutting") << "and the cut state can be read back from the tool";
    ASSERT_EQ(UseCalls(), 1) << "through the chainsaw's own cut handler, once, as the game's action event calls it";
    EXPECT_EQ(Number(L, "StubHandTool.useCalls[1].value"), 1.0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.useCalls[1].tool"), "chainsaw");
    EXPECT_EQ(Pad().bRightTrigger, 0) << "and the trigger that cuts is not also a binding";

    FRAMES(6);
    EXPECT_EQ(State(), "cutting") << "it goes on cutting while the trigger is held";
    EXPECT_EQ(UseCalls(), 1);
    EXPECT_EQ(Cuts(), 0) << "and has not gone through the tree yet";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(State(), "idle") << "letting go stops the cut";
    ASSERT_EQ(UseCalls(), 2);
    EXPECT_EQ(Number(L, "StubHandTool.useCalls[2].value"), 0.0);
    EXPECT_EQ(Cuts(), 0) << "without cutting the tree";
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0);
    EXPECT_EQ(Pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, ACutHeldToTheEndSlicesTheTreeAlongThePlaneItStartedWith) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.startHeight = cutNodeY(); _G.startNormal = planeNormal()", "=start"), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(60);

    ASSERT_EQ(Cuts(), 1) << "the game cut the tree";
    EXPECT_TRUE(True("StubHandTool.cuts[1].tree == barTree.node"));
    EXPECT_NEAR(Number(L, "StubHandTool.cuts[1].y - startHeight"), 0.0, 1e-3) << "at the height where the bar met the trunk";
    EXPECT_NEAR(Number(L, "StubHandTool.cuts[1].normalX * startNormal[1] + StubHandTool.cuts[1].normalY * startNormal[2] + StubHandTool.cuts[1].normalZ * startNormal[3]"),
                1.0, 1e-3) << "along the plane aligned with the bar";
    EXPECT_EQ(State(), "idle");
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, DuringACutMovingTheHandDoesNotMoveThePlaneAndTheModelStaysOnTheCut) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.heightBefore, _G.originBefore, _G.normalBefore = cutNodeY(), planeOrigin(), planeNormal()
        _G.modelBefore = { getWorldTranslation(saw.graphicalNode) }
    )luau", "=before"), 0);

    // A step to the side and a turn of the wrist: the bar is still against the trunk, so the cut goes on.
    AimHand(kRight, 0.30f, -0.31f, -0.28f, kBarForward + 0.05f, 1.3f);
    FRAMES(4);

    ASSERT_EQ(State(), "cutting") << "the bar is still near the cut";
    EXPECT_GT(Number(L, "dot(dirOf(barRay()), dirOf({ 0, 0, 0, 0.7833269, 0, 0.6216100 }))"), 0.0) << "the hand did move: the bar turned";
    EXPECT_LT(Number(L, "dot(dirOf(barRay()), dirOf({ 0, 0, 0, 0.7833269, 0, 0.6216100 }))"), 0.99);
    EXPECT_NEAR(Number(L, "cutNodeY() - heightBefore"), 0.0, 1e-4) << "the cut keeps its height";
    EXPECT_NEAR(Number(L, "math.abs(planeOrigin()[1] - originBefore[1]) + math.abs(planeOrigin()[2] - originBefore[2]) + math.abs(planeOrigin()[3] - originBefore[3])"),
                0.0, 1e-4) << "and its place";
    EXPECT_NEAR(Number(L, "dot(planeNormal(), normalBefore)"), 1.0, 1e-4) << "and its angle";
    EXPECT_EQ(State(), "cutting");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TakingTheBarOffTheTrunkStopsTheCutWithoutCuttingAndTheTriggerStillHeldDoesNotStartItAgain) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_EQ(State(), "cutting");

    AimHand(kRight, 1.50f, -0.35f, -0.30f, kBarForward, 0.9f);  // the hand a metre and a half away from the trunk
    FRAMES(2);
    EXPECT_EQ(State(), "idle") << "a cut that stops being valid stops, as in the game";
    EXPECT_EQ(Cuts(), 0);
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0);
    FRAMES(3);
    EXPECT_EQ(State(), "idle");
    EXPECT_EQ(UseCalls(), 1) << "the cut handler is not called again while the trigger stays held";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithTheTriggerPulledAndNoTreeUnderTheBarTheChainsawRevsAndLettingGoStopsIt) {
    StartWithChainsawInTheRightHand();  // no trees
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "delimbing") << "the game revs the motor without a cut";
    EXPECT_EQ(EvalLua(L, "VRCompanion:getChainsawCutState()"), "delimbing");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "idle");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheCutAngleStillComesFromTheGamesRollActionAndTheSeamForTheWristsRollIsClampedAndLockedDuringACut) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    ASSERT_NEAR(Number(L, "planeNormal()[2]"), 0.0, 1e-3) << "an upright plane with no roll";

    // The game's roll action (a stick or buttons) turns the plane about the bar.
    ASSERT_EQ(RunLuauChunk(L, "HandToolChainsaw.onRollAction(saw, nil, 30)", "=roll"), 0);
    FRAMES(2);  // the game applies the roll input in an update and lays the plane out from it in the next
    EXPECT_GT(std::abs(Number(L, "planeNormal()[2]")), 0.3) << "the plane tilted";

    // Ticket #115 answers the wrist's roll through getChainsawCutRoll; it replaces the game's, within the game's +-90 degrees.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.getChainsawCutRoll = function() return 5.0 end", "=seam"), 0);
    FRAMES(1);
    EXPECT_NEAR(Number(L, "saw.spec_chainsaw.currentCutRoll"), 1.5707963, 1e-4);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion.getChainsawCutRoll = function() return 0.4 end", "=seam"), 0);
    FRAMES(1);
    EXPECT_NEAR(Number(L, "saw.spec_chainsaw.currentCutRoll"), 0.4, 1e-6);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");
    ASSERT_EQ(RunLuauChunk(L, "_G.normalBefore = planeNormal(); VRCompanion.getChainsawCutRoll = function() return -0.9 end", "=lockRoll"), 0);
    FRAMES(3);
    EXPECT_NEAR(Number(L, "dot(planeNormal(), normalBefore)"), 1.0, 1e-4) << "a cut keeps the angle it started with";
    EXPECT_NEAR(Number(L, "saw.spec_chainsaw.currentCutRoll"), 0.4, 1e-6);
}

// The three ways a Hand Tool is not in a tracked Tool Hand, and the chainsaw is the game's: the head's tree is targeted, the model
// and the plane face the camera, and the game's own binding cuts.
class VirtualGamepadHandToolsChainsawAsInTheGameTest : public VirtualGamepadHandToolsChainsawTest {
protected:
    void ExpectTheChainsawIsAsInTheGame(const char* why) {
        SCOPED_TRACE(why);
        FRAMES(2);
        EXPECT_FALSE(True("VRCompanion:isHandHoldingHandTool(1)"));
        EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == headTree.node")) << "the tree the head looks at is the targeted one";
        EXPECT_GT(Number(L, "dot(modelDir(), dirOf(headRay()))"), 0.999) << "the model faces the camera";
        EXPECT_NEAR(Number(L, "dot(planeNormal(), horizontal(dirOf(headRay())))"), 0.0, 0.02) << "the plane is aligned with the camera";

        ForgetUseCalls();
        headset.SetHandInputs(kRight, Trigger(1.0f));
        FRAMES(2);
        EXPECT_EQ(State(), "idle") << "the Tool Hand's trigger does nothing";
        EXPECT_EQ(UseCalls(), 0);
        headset.SetHandInputs(kRight, Trigger(0.0f));
        FRAMES(1);

        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(saw, 1)", "=bindingDown"), 0);
        EXPECT_EQ(State(), "cutting") << "the game's own binding cuts";
        EXPECT_TRUE(True("saw.spec_chainsaw.cuttingTreeNode == headTree.node"));
        FRAMES(3);
        EXPECT_EQ(State(), "cutting");
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(saw, 0)", "=bindingUp"), 0);
        EXPECT_EQ(State(), "idle");
    }
};

TEST_F(VirtualGamepadHandToolsChainsawAsInTheGameTest, WithHandToolsSwitchedOffTheChainsawTargetsAlignsAndCutsAsInTheGame) {
    StartWithChainsawAndTreesInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    ExpectTheChainsawIsAsInTheGame("vrHandTools off");
}

TEST_F(VirtualGamepadHandToolsChainsawAsInTheGameTest, WithTheToolHandUntrackedTheChainsawTargetsAlignsAndCutsAsInTheGameAndIsBackInTheHandWhenItIsTrackedAgain) {
    StartWithChainsawAndTreesInTheRightHand();
    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    ExpectTheChainsawIsAsInTheGame("untracked");

    headset.SetHandTracked(kRight, true);
    FRAMES(3);
    EXPECT_TRUE(True("VRCompanion:isHandHoldingHandTool(1)"));
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "tracked again: the bar's tree";
    EXPECT_GT(Number(L, "dot(modelDir(), dirOf(barRay()))"), 0.999);
}

TEST_F(VirtualGamepadHandToolsChainsawAsInTheGameTest, WithTheToolHandSetDownTheChainsawTargetsAlignsAndCutsAsInTheGame) {
    StartWithChainsawAndTreesInTheRightHand();
    AdvanceGameTime(3100);  // the controller lies still for longer than the in-use timeout
    FRAMES(2);
    ExpectTheChainsawIsAsInTheGame("set down");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheToolHandLosingTrackingDuringACutStopsTheCut) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_EQ(State(), "cutting");

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(State(), "idle");
    EXPECT_EQ(Cuts(), 0);
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0) << "the player can look and move again";
}

// A game update that renames or reshapes a function the chainsaw's wrap needs: one warning, and the chainsaw is the game's.
class VirtualGamepadHandToolsChainsawVariantTest : public VirtualGamepadHandToolsChainsawTest {
protected:
    void ExpectOneWarningAndTheGamesChainsaw(const char* variant, const char* function) {
        ASSERT_NO_FATAL_FAILURE(Start(variant));
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
        AimHand(kRight, 0.25f, -0.35f, -0.30f, kBarForward, 0.9f);
        FRAMES(3);
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
        FRAMES(3);

        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "exactly one warning";
        EXPECT_EQ(CountLogLines(function), 1) << "that names the function";
        EXPECT_FALSE(True("VRCompanion:isHandHoldingHandTool(1)")) << "the chainsaw is not in the hand";
        EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == headTree.node"));
        EXPECT_GT(Number(L, "dot(modelDir(), dirOf(headRay()))"), 0.999) << "the model faces the camera";
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(saw, 1)", "=binding"), 0);
        EXPECT_EQ(State(), "cutting") << "the game's own binding cuts";
        FRAMES(5);
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and the warning is not repeated";
    }
};

TEST_F(VirtualGamepadHandToolsChainsawVariantTest, WithTheChainsawsUpdateMissingTheChainsawWorksAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesChainsaw("missing", "HandToolChainsaw.onUpdate");
}

TEST_F(VirtualGamepadHandToolsChainsawVariantTest, WithTheChainsawsUpdateReshapedTheChainsawWorksAsInTheGameAndTheLogHasOneWarning) {
    ExpectOneWarningAndTheGamesChainsaw("reshaped", "HandToolChainsaw.onUpdate");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheDiagnosticsReportWhatTheChainsawIsDoing) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolsDiagnostics')").find("chainsaw: idle"), std::string::npos) << EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolsDiagnostics')").find("chainsaw: cutting"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolsDiagnostics')").find("chainsawOnUpdate wrap: installed"), std::string::npos);
}
