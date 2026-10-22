// Hand Tools, the chainsaw (ADR-0029), seen from outside: the real Companion Mod runs against the real
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
//   edgeRay(height)   the same line lowered to the chain's lower edge: `height` along the root's own up from its origin (default
//                     -0.04, the stub model's cut node: about 4 cm below the bar's centre line, as in the game's models)
//   headRay()         what the game's own getLookRay answers for the player: the first person camera
//   modelDir()        the way the chainsaw's model points
//   dot(a, b)         the dot product of two directions { x, y, z }
//   dirOf(ray)        the direction of a ray
//   horizontal(d)     a direction flattened to the ground and made a unit vector
//   hitHeight(ray, distance)   the height a ray reaches at `distance` metres along the ground
//   heightOnTree(ray, tree)    the height a ray reaches where it meets the trunk of a tree of the stub
//   distanceFromLine(ray, point)   how far a point { x, y, z } is from the line a ray lies on
//   cutNodeY()        the height of the node the cut plane hangs from
//   cutDistanceFromTheBarsBase()          how far the cut is from where the bar starts
//   distanceOfTheGamesLookRayFromTheCut() how far the targeter's ray start (what the game's own stop measures) is from the cut
//   planeNormal()     the normal of the plane the game would cut along now
//   planeOrigin()     a point on it
//   state()           the chainsaw's cut state: "idle", "cutting" or "delimbing"
constexpr const char* kChainsawHelpers = R"luau(
    function _G.barRay() return { StubHandTool.barRay(saw) } end
    function _G.edgeRay(height)
        local x, y, z = localToWorld(saw.rootNode, 0, height or -0.04, 0)
        local _, _, _, dx, dy, dz = StubHandTool.barRay(saw)
        return { x, y, z, dx, dy, dz }
    end
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
    function _G.heightOnTree(ray, tree)
        local length = math.sqrt(ray[4] ^ 2 + ray[6] ^ 2)
        local hx, hz = ray[4] / length, ray[6] / length
        local along = (tree.x - ray[1]) * hx + (tree.z - ray[3]) * hz
        local across = (tree.x - ray[1]) * hz - (tree.z - ray[3]) * hx
        return ray[2] + ray[5] / length * (along - math.sqrt(tree.radius ^ 2 - across ^ 2))
    end
    function _G.distanceFromLine(ray, point)
        local vx, vy, vz = point[1] - ray[1], point[2] - ray[2], point[3] - ray[3]
        local cx, cy, cz = vy * ray[6] - vz * ray[5], vz * ray[4] - vx * ray[6], vx * ray[5] - vy * ray[4]
        return math.sqrt(cx ^ 2 + cy ^ 2 + cz ^ 2) / math.sqrt(ray[4] ^ 2 + ray[5] ^ 2 + ray[6] ^ 2)
    end
    function _G.cutNodeY()
        local _, y = getWorldTranslation(saw.spec_chainsaw.cameraRotationNode)
        return y
    end
    function _G.cutDistanceFromTheBarsBase()
        local cx, cy, cz = getWorldTranslation(saw.spec_chainsaw.cameraRotationNode)
        local bx, by, bz = table.unpack(barRay())
        return math.sqrt((cx - bx) ^ 2 + (cy - by) ^ 2 + (cz - bz) ^ 2)
    end
    function _G.distanceOfTheGamesLookRayFromTheCut()
        local t = foot.player.targeter
        local cx, cy, cz = getWorldTranslation(saw.spec_chainsaw.cameraRotationNode)
        return math.sqrt((t.lastRayX - cx) ^ 2 + (t.lastRayY - cy) ^ 2 + (t.lastRayZ - cz) ^ 2)
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

// The game's minimum cut distance in these tests: longer than the stub chainsaw's bar (0.4 m), so a look ray from the bar's base
// misses a trunk the bar is against, as the game's does. The stub's own default is 0 (no minimum).
constexpr const char* kGameMinimum = "HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.7";

// How far along the bar (horizontally, from its base) the surface of the trunk the tests plant for it is: mid-bar.
constexpr double kBarTreeDistance = 0.2;

// The suite name is one the filter for the Companion Mod tests (VirtualGamepad*) covers; run it with
// --gtest_filter='VirtualGamepad*:LuauTestSuiteTest.*:CompanionModTest.*' (the filter without VirtualGamepad* skips it).
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
    // `gameLua` runs before the player takes the chainsaw in hand: what the game's chainsaw registers with the targeter when
    // held is read then (the game's minimum cut distance, kGameMinimum unless a test sets another), and `variant` is how the
    // game's chainsaw is.
    void StartWithChainsawInTheRightHand(const char* gameLua = kGameMinimum, const char* variant = "ok") {
        ASSERT_NO_FATAL_FAILURE(Start(variant));
        ASSERT_EQ(RunLuauChunk(L, gameLua, "=gameLua"), 0);
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, 0.25f, -0.35f, -0.30f);
        AimHand(kRight, 0.25f, -0.35f, -0.30f, kBarForward, 0.9f);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    // A trunk the chainsaw's bar is against (its surface `barDistance` metres along the bar from its base) and one the head looks
    // straight at.
    void PlantTreesAtTheBarAndInFrontOfTheHead(double barDistance = kBarTreeDistance, double headDistance = 1.2) {
        const std::string lua = "_G.barTreeDistance = " + std::to_string(barDistance) + "; "
                                "_G.barTree = StubHandTool.treeAcross(barRay(), " + std::to_string(barDistance) + ", 0.15); "
                                "_G.headTree = StubHandTool.treeAcross(headRay(), " + std::to_string(headDistance) + ", 0.15)";
        ASSERT_EQ(RunLuauChunk(L, lua.c_str(), "=plantTrees"), 0);
    }

    // A trunk whose surface the bar meets `alongTheBar` metres from its base, measured along the bar (which points down at an angle,
    // so that is further than the trunk is horizontally).
    void PlantTreeAlongTheBar(double alongTheBar) {
        const double horizontal = alongTheBar * Number(L, "math.sqrt(barRay()[4] ^ 2 + barRay()[6] ^ 2)");
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead(horizontal));
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
    // The haptic pulses so far to a hand and the strongest of them; ForgetPulses starts counting afresh.
    size_t PulsesTo(int hand) const {
        size_t count = 0;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) ++count;
        }
        return count;
    }
    float StrongestPulseTo(int hand) const {
        float strongest = 0.0f;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) strongest = std::max(strongest, pulse.amplitude);
        }
        return strongest;
    }
    void ForgetPulses() { headset.runtime->controllers.hapticPulses.clear(); }
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
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[2] - heightOnTree(edgeRay(), barTree)"), 0.0, 2e-3)
        << "at the height where the bar meets it";

    // Take the bar off that trunk: the head's tree is the one in the way of nothing, so nothing is targeted once the bar points away.
    AimHand(kRight, 0.25f, -0.35f, -0.30f, kBarForward, 2.8f);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the bar points at no trunk, and the head's does not count";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)"));
}

// The bar's length is 0.4 m (the stub's cut node, at the bar's tip) and the game's minimum cut distance is longer (kGameMinimum),
// so a look ray from the bar's base would miss every one of these (ADR-0029, amended): the ray starts the minimum behind the bar.
TEST_F(VirtualGamepadHandToolsChainsawTest, ATrunkAcrossTheBarNearItsBaseMidBarOrAtItsTipIsFoundShowsTheRingWhereTheBarMeetsItAndTheTriggerCutsIt) {
    StartWithChainsawInTheRightHand();

    for (const double surface : { 0.05, 0.2, 0.4 }) {
        SCOPED_TRACE("the trunk's surface " + std::to_string(surface) + " m along the bar");
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead(surface));
        FRAMES(2);

        EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "the trunk is found";
        EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the ring shows on it";
        EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[2] - heightOnTree(edgeRay(), barTree)"), 0.0, 2e-3)
            << "at the height where the bar meets the bark";

        headset.SetHandInputs(kRight, Trigger(1.0f));
        FRAMES(2);
        EXPECT_EQ(State(), "cutting") << "the trigger starts a cut";
        EXPECT_TRUE(True("saw.spec_chainsaw.cuttingTreeNode == barTree.node"));
        headset.SetHandInputs(kRight, Trigger(0.0f));
        FRAMES(2);
        ASSERT_EQ(State(), "idle");
    }
}

// The bar is 0.4 m long and the saw finds a trunk up to 0.15 m past its tip (ADR-0029, amended): 0.55 m along the bar from its base.
TEST_F(VirtualGamepadHandToolsChainsawTest, ATrunkJustInsideTheMarginPastTheTipIsFoundAndShowsTheRingAndOneBeyondItIsNot) {
    StartWithChainsawInTheRightHand();

    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.5));  // 10 cm past the tip
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "found";
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the ring shows";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.6));  // 20 cm past the tip
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the saw has found no tree";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "no Cut Ring";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithTheTrunkBeyondTheMarginTheTriggerRevsIntoDelimbingAndMakesNoCut) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.8));
    FRAMES(2);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(60);
    EXPECT_EQ(State(), "delimbing") << "the motor revs, no cut starts";
    EXPECT_TRUE(True("saw.spec_chainsaw.cuttingTreeNode == nil"));
    EXPECT_EQ(Cuts(), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "idle");

    // The bar is brought to the trunk: the same trigger now cuts.
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.3));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "cutting");
}

// The tree is out of the game's sight only while the game's update runs: the targeter keeps what it found.
TEST_F(VirtualGamepadHandToolsChainsawTest, TheTargeterStillHoldsTheTreeBeyondTheMarginOnceTheChainsawsUpdateHasRun) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.8));
    FRAMES(3);

    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the saw found none";
    EXPECT_TRUE(True("foot.player.targeter.closestTargetsByKey[HandToolChainsaw] ~= nil and foot.player.targeter.closestTargetsByKey[HandToolChainsaw].node == barTree.node"))
        << "while the targeter's answer is intact for everything else that reads it";
}

// Without the minimum the ray is not moved back, so the margin is measured from the bar's base: 0.55 m.
TEST_F(VirtualGamepadHandToolsChainsawTest, AGameWhoseChainsawLacksTheMinimumCutDistanceLimitsTheReachFromTheBarsBase) {
    StartWithChainsawInTheRightHand("", "minimumMissing");

    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.5));
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node"));

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.7));
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil"));
}

// The stub's game: a held chainsaw's trees are a target type of the targeter (minimum and maximum distance), as the game's
// HandToolChainsaw adds them, so a trunk too close to the look ray's start is missed (ADR-0029, amended).
TEST_F(VirtualGamepadHandToolsChainsawTest, AHeldChainsawsTreesAreATargetTypeWithAMinimumAndAMaximumDistanceThatPuttingTheSawAwayRemoves) {
    StartWithChainsawInTheRightHand("HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.7");

    EXPECT_EQ(Dump("type(HandToolChainsaw.MINIMUM_CUT_DISTANCE)"), "number") << "the class carries the game's minimum, next to the maximum";
    EXPECT_EQ(Dump("type(HandToolChainsaw.MAXIMUM_CUT_DISTANCE)"), "number");
    ASSERT_TRUE(True("foot.player.targeter.targetedMasks ~= nil and foot.player.targeter.targetedMasks[HandToolChainsaw] ~= nil"))
        << "the held saw's trees are a target type";
    EXPECT_NEAR(Number(L, "foot.player.targeter.targetedMasks[HandToolChainsaw].minDistance"), 0.7, 1e-6) << "from the class's minimum";
    EXPECT_GT(Number(L, "foot.player.targeter.targetedMasks[HandToolChainsaw].maxDistance"), 0.7) << "to a maximum";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(saw)", "=putAway"), 0);
    EXPECT_TRUE(True("foot.player.targeter.targetedMasks[HandToolChainsaw] == nil")) << "put away, the type is gone";
    EXPECT_TRUE(True("foot.player.targeter.closestTargetsByKey[HandToolChainsaw] == nil"));
}

// Hand Tools off, so that the targeter's ray is the game's own: the head's.
TEST_F(VirtualGamepadHandToolsChainsawTest, ATrunkNearerToTheLookRaysStartThanTheMinimumIsNotFoundAndTheFoundTreeReportsItsDistance) {
    StartWithChainsawInTheRightHand("HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.8");
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);

    // The trunk is 0.6 m along the head's ray, nearer than the minimum: the targeter misses it.
    ASSERT_EQ(RunLuauChunk(L, "_G.nearTree = StubHandTool.treeAcross(headRay(), 0.6, 0.15)", "=plantNear"), 0);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "nothing found nearer than the minimum";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)"));

    // A trunk beyond the minimum is found, and the target says how far it is along the ray.
    ASSERT_EQ(RunLuauChunk(L, "_G.farTree = StubHandTool.treeAcross(headRay(), 1.2, 0.15)", "=plantFar"), 0);
    FRAMES(2);
    ASSERT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == farTree.node"));
    EXPECT_NEAR(Number(L, "saw.spec_chainsaw.targetedTree.distance * math.sqrt(headRay()[4] ^ 2 + headRay()[6] ^ 2)"), 1.2, 1e-3)
        << "the horizontal distance to the bark";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheStubModelHasItsCutNodeBelowTheBarsCenterLine) {
    StartWithChainsawInTheRightHand();

    EXPECT_NEAR(Number(L, "({ getTranslation(saw.spec_chainsaw.cutNode) })[2]"), -0.04, 1e-6) << "about 4 cm, as in the game's models";
    EXPECT_NEAR(Number(L, "({ getTranslation(saw.spec_chainsaw.cutNode) })[3]"), 0.4, 1e-6) << "at the bar's tip";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheLookRayStartsTheGamesMinimumCutDistanceBehindTheBarsBaseAlongTheBar) {
    StartWithChainsawInTheRightHand("HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.55");

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.look = { foot.player:getLookRay() }
        _G.bar = barRay()
    )luau", "=rays"), 0);
    EXPECT_NEAR(Number(L, "dot(dirOf(look), dirOf(bar))"), 1.0, 1e-6) << "along the bar";
    EXPECT_NEAR(Number(L, "(bar[1] - look[1]) * bar[4] + (bar[2] - look[2]) * bar[5] + (bar[3] - look[3]) * bar[6]"), 0.55, 1e-4)
        << "starting the game's minimum behind its base (the base is that far ahead of the start, along the bar)";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, AGameWhoseChainsawLacksTheMinimumCutDistanceAimsAsBeforeFindsTheTreeTheBarIsAgainstAndLogsOneWarning) {
    StartWithChainsawInTheRightHand("", "minimumMissing");
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(5);

    EXPECT_EQ(Dump("tostring(HandToolChainsaw.MINIMUM_CUT_DISTANCE)"), "nil") << "this game version has no minimum";
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node"));
    ASSERT_EQ(RunLuauChunk(L, "_G.look = { foot.player:getLookRay() }; _G.bar = edgeRay()", "=rays"), 0);
    EXPECT_NEAR(Number(L, "math.sqrt((look[1] - bar[1]) ^ 2 + (look[2] - bar[2]) ^ 2 + (look[3] - bar[3]) ^ 2)"), 0.0, 1e-4)
        << "the ray starts on the chain's lower edge at the bar's base, not moved back";
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "one warning, not one per frame";
    EXPECT_EQ(CountLogLines("MINIMUM_CUT_DISTANCE"), 1) << "that names what could not be read";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, AMinimumCutDistanceThatIsNotANumberIsNotReadEither) {
    StartWithChainsawInTheRightHand("HandToolChainsaw.MINIMUM_CUT_DISTANCE = 'far'");
    FRAMES(3);

    ASSERT_EQ(RunLuauChunk(L, "_G.look = { foot.player:getLookRay() }; _G.bar = edgeRay()", "=rays"), 0);
    EXPECT_NEAR(Number(L, "math.sqrt((look[1] - bar[1]) ^ 2 + (look[2] - bar[2]) ^ 2 + (look[3] - bar[3]) ^ 2)"), 0.0, 1e-4);
    EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheCutHeightIsWhereTheBarMeetsTheTrunkAndThePlaneIsAlignedWithTheBarNotTheCamera) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);

    EXPECT_NEAR(Number(L, "cutNodeY()"), Number(L, "heightOnTree(edgeRay(), barTree)"), 2e-3) << "the node the cut plane hangs from is where the bar meets the trunk";
    EXPECT_GT(std::abs(Number(L, "cutNodeY() - hitHeight(headRay(), 1.2)")), 0.02) << "not where the head's ray meets its trunk";
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

// A felled log lying under the bar is stood in for by a trunk of the stub whose top is 5 mm over the chain's lower edge where the bar
// meets it: the edge touches it, the bar's centre line (4 cm higher) passes over it.
TEST_F(VirtualGamepadHandToolsChainsawTest, ALogUnderTheBarTouchedByTheChainsLowerEdgeButNotByTheCenterLineIsFoundAndShowsTheCutRing) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.logTop = heightOnTree(edgeRay(), barTree) + 0.005
        barTree.y0, barTree.y1 = logTop - 0.3, logTop
    )luau", "=layLog"), 0);
    ASSERT_GT(Number(L, "heightOnTree(barRay(), barTree) - logTop"), 0.02) << "the centre line passes over the log";
    FRAMES(2);

    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "the log is found";
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "and the Cut Ring shows on it";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "cutting") << "the trigger cuts it";
    EXPECT_TRUE(True("saw.spec_chainsaw.cuttingTreeNode == barTree.node"));
}

TEST_F(VirtualGamepadHandToolsChainsawTest, AChainsawModelWithADifferentCutNodeHeightReachesFromItsOwn) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "setTranslation(saw.spec_chainsaw.cutNode, 0, -0.10, 0.4)", "=lowerCutNode"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);

    ASSERT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node"));
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[2] - heightOnTree(edgeRay(-0.10), barTree)"), 0.0, 2e-3)
        << "the ring is at the height of this model's cut node";

    // A log that only an edge as low as this model's reaches.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.logTop = heightOnTree(edgeRay(-0.10), barTree) + 0.005
        barTree.y0, barTree.y1 = logTop - 0.3, logTop
    )luau", "=layLog"), 0);
    ASSERT_GT(Number(L, "heightOnTree(edgeRay(), barTree) - logTop"), 0.03) << "a 4 cm edge would pass over it";
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "found from the model's own edge";
}

// The cut node rides the cut guide while cutting (the model is linked to it), so its live place says nothing about the chain: the
// height is the one the model has under the tool.
TEST_F(VirtualGamepadHandToolsChainsawTest, WhileCuttingTheLookRayStaysOnTheChainsLowerEdgeAlthoughTheModelRidesTheCutGuide) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.look = { foot.player:getLookRay() }", "=idleRay"), 0);
    EXPECT_NEAR(Number(L, "distanceFromLine(edgeRay(), look)"), 0.0, 1e-4) << "idle: along the lower edge";
    EXPECT_GT(Number(L, "distanceFromLine(barRay(), look)"), 0.03) << "and not along the centre line";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_EQ(State(), "cutting");
    ASSERT_TRUE(True("getParent(saw.graphicalNode) == saw.spec_chainsaw.cutGuideNode")) << "the model rides the cut guide";
    ASSERT_EQ(RunLuauChunk(L, "_G.look = { foot.player:getLookRay() }", "=cuttingRay"), 0);
    EXPECT_NEAR(Number(L, "distanceFromLine(edgeRay(), look)"), 0.0, 1e-4) << "cutting: still along the lower edge";
}

// Without a cut node to read (the game fills in the root node when a chainsaw's i3d names none; one that is no node is no better)
// the ray stays on the bar's centre line, at the root's height.
class VirtualGamepadHandToolsChainsawCutNodeTest : public VirtualGamepadHandToolsChainsawTest {
protected:
    void ExpectTheCenterLineWhenTheCutNodeIs(const char* cutNode) {
        ASSERT_NO_FATAL_FAILURE(StartWithChainsawInTheRightHand());
        ASSERT_EQ(RunLuauChunk(L, (std::string("saw.spec_chainsaw.cutNode = ") + cutNode).c_str(), "=breakCutNode"), 0);
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
        FRAMES(3);

        EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "the bar's trunk is found";
        EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[2] - hitHeight(barRay(), barTreeDistance)"), 0.0, 1e-2)
            << "at the height of the bar's centre line";
        ASSERT_EQ(RunLuauChunk(L, "_G.look = { foot.player:getLookRay() }", "=ray"), 0);
        EXPECT_NEAR(Number(L, "distanceFromLine(barRay(), look)"), 0.0, 1e-4) << "the ray runs along the bar's centre line";
    }
};

TEST_F(VirtualGamepadHandToolsChainsawCutNodeTest, AChainsawWithNoCutNodeAimsFromTheRootsHeight) {
    ExpectTheCenterLineWhenTheCutNodeIs("nil");
}

TEST_F(VirtualGamepadHandToolsChainsawCutNodeTest, AChainsawWhoseCutNodeIsItsRootAimsFromTheRootsHeight) {
    ExpectTheCenterLineWhenTheCutNodeIs("saw.rootNode");
}

TEST_F(VirtualGamepadHandToolsChainsawCutNodeTest, AChainsawWhoseCutNodeIsNotANodeAimsFromTheRootsHeight) {
    ExpectTheCenterLineWhenTheCutNodeIs("'broken'");
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

// A running cut and the reach (ADR-0029, amended): the game stops a cut when the targeter's ray start, which is the game's minimum
// cut distance behind the bar's base, is 1.5 m from the cut. The mod stops it when the cut is further from the bar than the bar and
// a hand's width past its tip (0.55 m from its base), though the game's own check would still let it run.
TEST_F(VirtualGamepadHandToolsChainsawTest, ACutKeepsRunningWhileTheHandMovesWithTheCutWithinTheBarsReachAndSlicesTheTreeWhenItFinishes) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.3));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");

    AimHand(kRight, 0.25f + 0.08f * 0.7833269f, -0.35f, -0.30f + 0.08f * 0.62161f, kBarForward, 0.9f);  // the hand 8 cm back
    FRAMES(4);
    ASSERT_LT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "the cut is still within the bar and a hand's width past its tip";
    EXPECT_EQ(State(), "cutting") << "so the cut goes on";

    FRAMES(60);
    EXPECT_EQ(Cuts(), 1) << "and slices the tree when it finishes";
    EXPECT_EQ(State(), "idle");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, PullingTheBarOutOfTheWoodPastTheReachStopsTheCutWithoutASliceAndTheSawIsIdleAgain) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.3));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");

    AimHand(kRight, 0.25f + 0.35f * 0.7833269f, -0.35f, -0.30f + 0.35f * 0.62161f, kBarForward, 0.9f);  // the hand 35 cm back
    FRAMES(1);
    ASSERT_GT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "the cut is further from the bar than the reach";
    ASSERT_LT(Number(L, "distanceOfTheGamesLookRayFromTheCut()"), 1.5) << "which the game's own check still lets run";
    FRAMES(1);
    EXPECT_EQ(State(), "idle") << "the cut is stopped";
    EXPECT_EQ(Cuts(), 0) << "without slicing the tree";
    EXPECT_TRUE(True("saw.spec_chainsaw.cuttingTreeNode == nil"));
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0) << "the player can look and move again";

    FRAMES(60);
    EXPECT_EQ(State(), "idle") << "and the trigger still held does not start it again";
    EXPECT_EQ(Cuts(), 0);
    EXPECT_EQ(UseCalls(), 1);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, ACutKeepsItsHeightHeadingAndTiltWhileTheBarIsMovedWithinTheReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.3));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(State(), "cutting");
    ASSERT_EQ(RunLuauChunk(L, "_G.heightBefore, _G.normalBefore = cutNodeY(), planeNormal()", "=before"), 0);

    AimHand(kRight, 0.25f + 0.06f * 0.7833269f, -0.33f, -0.30f + 0.06f * 0.62161f, kBarForward + 0.1f, 1.1f);
    FRAMES(4);
    ASSERT_LT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55);
    ASSERT_EQ(State(), "cutting");
    EXPECT_NEAR(Number(L, "cutNodeY() - heightBefore"), 0.0, 1e-4);
    EXPECT_NEAR(Number(L, "dot(planeNormal(), normalBefore)"), 1.0, 1e-4);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheCutEndingBecauseTheBarWasPulledOutEndsTheToolHandsCuttingBuzz) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.3));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(6);
    ASSERT_EQ(State(), "cutting");
    EXPECT_GT(StrongestPulseTo(kRight), 0.5f) << "the cut buzzes the Tool Hand hard";

    AimHand(kRight, 0.25f + 0.35f * 0.7833269f, -0.35f, -0.30f + 0.35f * 0.62161f, kBarForward, 0.9f);
    FRAMES(3);
    ASSERT_EQ(State(), "idle");
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the cut over, the buzz is over, as for any other cut ending";
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

// The Tool Hand does not stay "in use" holding a tool (a hand lying still for 3 s is set down); a held-still pull must not drop the
// chainsaw.
TEST_F(VirtualGamepadHandToolsChainsawTest, AHeldStillTriggerDoesNotDropTheChainsawAndAfterLettingGoTheHandIsSetDownAsAlways) {
    StartWithChainsawInTheRightHand();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    for (int second = 0; second < 6; ++second) {
        AdvanceGameTime(1000);
        FRAMES(1);
    }
    EXPECT_TRUE(True("VRCompanion:isHandHoldingHandTool(1)")) << "six seconds of holding the trigger with a still hand";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_FALSE(True("VRCompanion:isHandHoldingHandTool(1)")) << "set down after three still seconds without the trigger";
    EXPECT_TRUE(True("isBelow(saw.rootNode, foot.cam.pitchNode)")) << "and the chainsaw is where the game puts it";
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

// The game's own reach (up to the targeter's 3 m from the head) applies where the margin does not: a trunk 2 m away is found.
TEST_F(VirtualGamepadHandToolsChainsawAsInTheGameTest, WithHandToolsSwitchedOffTheGamesOwnReachApplies) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    ASSERT_EQ(RunLuauChunk(L, "_G.farTree = StubHandTool.treeAcross(headRay(), 2.0, 0.15)", "=plantFar"), 0);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == farTree.node"));
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)"));
}

TEST_F(VirtualGamepadHandToolsChainsawAsInTheGameTest, WithTheToolHandUntrackedTheGamesOwnReachApplies) {
    StartWithChainsawInTheRightHand();
    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.farTree = StubHandTool.treeAcross(headRay(), 2.0, 0.15)", "=plantFar"), 0);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == farTree.node"));
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)"));
}

TEST_F(VirtualGamepadHandToolsChainsawAsInTheGameTest, WithTheToolHandSetDownTheGamesOwnReachApplies) {
    StartWithChainsawInTheRightHand();
    AdvanceGameTime(3100);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "_G.farTree = StubHandTool.treeAcross(headRay(), 2.0, 0.15)", "=plantFar"), 0);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == farTree.node"));
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)"));
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

TEST_F(VirtualGamepadHandToolsChainsawTest, TheDiagnosticsNameTheGamesMinimumCutDistanceTheModRead) {
    StartWithChainsawInTheRightHand("HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.65");
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(diagnostics.find("minimum cut distance: 0.65 m"), std::string::npos) << diagnostics;
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheDiagnosticsNameTheReachPastTheTip) {
    StartWithChainsawInTheRightHand();
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(diagnostics.find("reach past the tip: 0.15 m"), std::string::npos) << diagnostics;
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheDiagnosticsSayWhenTheGamesMinimumCutDistanceCouldNotBeRead) {
    StartWithChainsawInTheRightHand("", "minimumMissing");
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(diagnostics.find("minimum cut distance: could not be read"), std::string::npos) << diagnostics;
}
