#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <memory>
#include <string>

namespace {

using namespace hand_tools_test;

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

constexpr const char* kGameMinimum = "HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.7";

// How far along the bar (horizontally, from its base) the surface of the trunk the tests plant for it is: mid-bar.
constexpr double kBarTreeDistance = 0.2;

// Where the tests hold the Tool Hand, and the way its bar then points (kBarForward, kBarYaw). Backing the hand away along the
// bar's heading is a step of (kBackX, kBackZ) a metre on the ground.
constexpr float kHandX = 0.25f;
constexpr float kHandY = -0.35f;
constexpr float kHandZ = -0.30f;
constexpr float kBarYaw = 0.9f;
constexpr float kBackX = 0.7833269f;
constexpr float kBackZ = 0.62161f;

class VirtualGamepadHandToolsChainsawTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    bool settingsFile = false;

    void Start(const char* variant = "ok") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        StartOptions options;
        const std::string stubs = (settingsFile ? StubChunk("xml_settings") : std::string()) + ChainsawStubs(variant);
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kChainsawHelpers, "=chainsawHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void AimHand(uint32_t hand, float x, float y, float z, float pitch, float yaw) {
        headset.SetHandPose(hand, HandAt(x, y, z, pitch, yaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, pitch, yaw));
    }

    void StartWithChainsawInTheRightHand(const char* gameLua = kGameMinimum, const char* variant = "ok") {
        ASSERT_NO_FATAL_FAILURE(Start(variant));
        ASSERT_EQ(RunLuauChunk(L, gameLua, "=gameLua"), 0);
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, kHandX, kHandY, kHandZ);
        AimHand(kRight, kHandX, kHandY, kHandZ, kBarForward, kBarYaw);
        FRAMES(2);
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    void LimitTheReach() {
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0.15')", "=limitTheReach"), 0);
    }

    void PullTheHandBack(float metres) {
        AimHand(kRight, kHandX + metres * kBackX, kHandY, kHandZ + metres * kBackZ, kBarForward, kBarYaw);
    }

    void PlantTreesAtTheBarAndInFrontOfTheHead(double barDistance = kBarTreeDistance, double headDistance = 1.2) {
        const std::string lua = "_G.barTreeDistance = " + std::to_string(barDistance) + "; "
                                "_G.barTree = StubHandTool.treeAcross(barRay(), " + std::to_string(barDistance) + ", 0.15); "
                                "_G.headTree = StubHandTool.treeAcross(headRay(), " + std::to_string(headDistance) + ", 0.15)";
        ASSERT_EQ(RunLuauChunk(L, lua.c_str(), "=plantTrees"), 0);
    }

    void PlantTreeAlongTheBar(double alongTheBar) {
        const double horizontal = alongTheBar * Number(L, "math.sqrt(barRay()[4] ^ 2 + barRay()[6] ^ 2)");
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead(horizontal));
    }

    void StartCuttingATreeAlongTheBar(double alongTheBar) {
        ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(alongTheBar));
        FRAMES(2);
        headset.SetHandInputs(kRight, Trigger(1.0f));
        FRAMES(2);
        ASSERT_EQ(State(), "cutting");
    }

    void StartWithChainsawAndTreesInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(StartWithChainsawInTheRightHand());
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
        FRAMES(2);
    }

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

}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheChainsawInTheToolHandFollowsTheHandAndItsModelPointsAlongTheHandNotWhereTheHeadLooks) {
    StartWithChainsawInTheRightHand();

    EXPECT_TRUE(True("isBelow(saw.rootNode, handHolder('right'))")) << "the chainsaw's root hangs below the Virtual Hand";
    EXPECT_GT(Number(L, "dot(modelDir(), dirOf(barRay()))"), 0.999) << "the model points along the bar, which is the hand's";
    EXPECT_LT(Number(L, "dot(modelDir(), dirOf(headRay()))"), 0.95) << "and not where the head looks (the game turns it there)";

    ASSERT_EQ(RunLuauChunk(L, "_G.before = modelDir()", "=before"), 0);
    AimHand(kRight, 0.45f, -0.20f, -0.50f, kBarForward - 0.1f, -0.6f);
    FRAMES(2);
    EXPECT_GT(Number(L, "dot(modelDir(), dirOf(barRay()))"), 0.999);
    EXPECT_LT(Number(L, "dot(modelDir(), before)"), 0.95) << "it turned with the hand";
    EXPECT_LT(Number(L, "dot(modelDir(), dirOf(headRay()))"), 0.95);
}


TEST_F(VirtualGamepadHandToolsChainsawTest, TheTreeTheBarIsAgainstIsTheTargetedOneNotTheOneTheHeadLooksAtAndTheCutRingSitsWhereTheBarMeetsIt) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);

    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil")) << "a tree is targeted";
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree.node == barTree.node")) << "the trunk the bar is against";
    EXPECT_FALSE(True("saw.spec_chainsaw.targetedTree.node == headTree.node")) << "not the one in front of the face";

    ASSERT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the game shows its Cut Ring";
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[1] - barTree.x"), 0.0, 1e-3) << "on the trunk the bar is against";
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[3] - barTree.z"), 0.0, 1e-3);
    EXPECT_NEAR(Number(L, "({ getWorldTranslation(saw.spec_chainsaw.ringNode) })[2] - heightOnTree(edgeRay(), barTree)"), 0.0, 2e-3)
        << "at the height where the bar meets it";

    AimHand(kRight, kHandX, kHandY, kHandZ, kBarForward, 2.8f);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the bar points at no trunk, and the head's does not count";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)"));
}

TEST_F(VirtualGamepadHandToolsChainsawTest, ATrunkAcrossTheBarNearItsBaseMidBarOrAtItsTipIsFoundShowsTheRingWhereTheBarMeetsItAndTheTriggerCutsIt) {
    StartWithChainsawInTheRightHand();

    for (const double surface : { 0.05, 0.2, 0.4 }) {
        SCOPED_TRACE("the trunk's surface " + std::to_string(surface) + " m along the bar");
        ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
        ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead(surface));
        FRAMES(2);

        EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "the trunk is found";
        EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the Cut Ring shows on it";
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

// With `vrChainsawReach 0.15` (ADR-0029, amended) the bar is 0.4 m long and the saw finds a trunk up to 0.15 m past its tip: 0.55 m
// along the bar from its base.
TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachATrunkJustInsideTheMarginPastTheTipIsFoundAndShowsTheRingAndOneBeyondItIsNot) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());

    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.5));
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "found";
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the Cut Ring shows";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.6));
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the saw has found no tree";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "no Cut Ring";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachAndTheTrunkBeyondTheMarginTheTriggerRevsIntoDelimbingAndMakesNoCut) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
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

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.3));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "cutting");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachTheTargeterStillHoldsTheTreeBeyondTheMarginOnceTheChainsawsUpdateHasRun) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.8));
    FRAMES(3);

    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "the saw found none";
    EXPECT_TRUE(True("foot.player.targeter.closestTargetsByKey[HandToolChainsaw] ~= nil and foot.player.targeter.closestTargetsByKey[HandToolChainsaw].node == barTree.node"))
        << "while the targeter's answer is intact for everything else that reads it";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, AGameWhoseChainsawLacksTheMinimumCutDistanceKeepsTheGamesOwnReachAndHidesNoTree) {
    StartWithChainsawInTheRightHand("", "minimumMissing");

    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.7));
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "found, as in the game";
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "and the Cut Ring shows";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(2.0));
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node"));
}

TEST_F(VirtualGamepadHandToolsChainsawTest, AGameWhoseChainsawLacksTheMinimumCutDistanceDoesNotStopACutWhenTheBarIsPulledOut) {
    StartWithChainsawInTheRightHand("", "minimumMissing");
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));

    PullTheHandBack(0.35f);
    FRAMES(2);
    ASSERT_GT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "further from the bar than the reach would allow";
    EXPECT_EQ(State(), "cutting") << "the game's own end-of-cut rule applies alone";

    FRAMES(60);
    EXPECT_EQ(Cuts(), 1) << "and the cut finishes";
}

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

TEST_F(VirtualGamepadHandToolsChainsawTest, ATrunkNearerToTheLookRaysStartThanTheMinimumIsNotFoundAndTheFoundTreeReportsItsDistance) {
    StartWithChainsawInTheRightHand("HandToolChainsaw.MINIMUM_CUT_DISTANCE = 0.8");
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);

    // The trunk is 0.6 m along the head's ray, nearer than the minimum: the targeter misses it.
    ASSERT_EQ(RunLuauChunk(L, "_G.nearTree = StubHandTool.treeAcross(headRay(), 0.6, 0.15)", "=plantNear"), 0);
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree == nil")) << "nothing found nearer than the minimum";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)"));

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

    EXPECT_NEAR(Number(L, "dot(planeNormal(), horizontal(dirOf(barRay())))"), 0.0, 0.02);
    EXPECT_GT(std::abs(Number(L, "dot(planeNormal(), horizontal(dirOf(headRay())))")), 0.4);

    ASSERT_EQ(RunLuauChunk(L, "_G.before = planeNormal()", "=before"), 0);
    AimHand(kRight, kHandX, kHandY, kHandZ, kBarForward, 1.2f);
    FRAMES(2);
    EXPECT_LT(Number(L, "dot(planeNormal(), before)"), 0.99) << "the plane turned with the bar";
    EXPECT_NEAR(Number(L, "dot(planeNormal(), horizontal(dirOf(barRay())))"), 0.0, 0.02);
}

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
        << "the Cut Ring is at the height of this model's cut node";

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.logTop = heightOnTree(edgeRay(-0.10), barTree) + 0.005
        barTree.y0, barTree.y1 = logTop - 0.3, logTop
    )luau", "=layLog"), 0);
    ASSERT_GT(Number(L, "heightOnTree(edgeRay(), barTree) - logTop"), 0.03) << "a 4 cm edge would pass over it";
    FRAMES(2);
    EXPECT_TRUE(True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node")) << "found from the model's own edge";
}

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

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachTakingTheBarOffTheTrunkStopsTheCutWithoutCuttingAndTheTriggerStillHeldDoesNotStartItAgain) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(PlantTreesAtTheBarAndInFrontOfTheHead());
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_EQ(State(), "cutting");

    AimHand(kRight, 1.50f, kHandY, kHandZ, kBarForward, kBarYaw);
    FRAMES(2);
    EXPECT_EQ(State(), "idle") << "a cut that stops being valid stops, as in the game";
    EXPECT_EQ(Cuts(), 0);
    EXPECT_EQ(Number(L, "StubHandTool.inputLocks"), 0.0);
    FRAMES(3);
    EXPECT_EQ(State(), "idle");
    EXPECT_EQ(UseCalls(), 1) << "the cut handler is not called again while the trigger stays held";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachACutKeepsRunningWhileTheHandMovesWithTheCutWithinTheBarsReachAndSlicesTheTreeWhenItFinishes) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));

    PullTheHandBack(0.08f);
    FRAMES(4);
    ASSERT_LT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "the cut is still within the bar and a hand's width past its tip";
    EXPECT_EQ(State(), "cutting") << "so the cut goes on";

    FRAMES(60);
    EXPECT_EQ(Cuts(), 1) << "and slices the tree when it finishes";
    EXPECT_EQ(State(), "idle");
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachPullingTheBarOutOfTheWoodPastTheReachStopsTheCutWithoutASliceAndTheSawIsIdleAgain) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));

    PullTheHandBack(0.35f);
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

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachACutKeepsItsHeightHeadingAndTiltWhileTheBarIsMovedWithinTheReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));
    ASSERT_EQ(RunLuauChunk(L, "_G.heightBefore, _G.normalBefore = cutNodeY(), planeNormal()", "=before"), 0);

    AimHand(kRight, kHandX + 0.06f * kBackX, kHandY + 0.02f, kHandZ + 0.06f * kBackZ, kBarForward + 0.1f, 1.1f);
    FRAMES(4);
    ASSERT_LT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55);
    ASSERT_EQ(State(), "cutting");
    EXPECT_NEAR(Number(L, "cutNodeY() - heightBefore"), 0.0, 1e-4);
    EXPECT_NEAR(Number(L, "dot(planeNormal(), normalBefore)"), 1.0, 1e-4);
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachTheCutEndingBecauseTheBarWasPulledOutEndsTheToolHandsCuttingBuzz) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));
    FRAMES(4);
    ASSERT_EQ(State(), "cutting");
    EXPECT_GT(StrongestPulseTo(kRight), 0.5f) << "the cut buzzes the Tool Hand hard";

    PullTheHandBack(0.35f);
    FRAMES(3);
    ASSERT_EQ(State(), "idle");
    ForgetPulses();
    FRAMES(10);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the cut over, the buzz is over, as for any other cut ending";
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithTheTriggerPulledAndNoTreeUnderTheBarTheChainsawRevsAndLettingGoStopsIt) {
    StartWithChainsawInTheRightHand();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "delimbing") << "the game revs the motor without a cut";
    EXPECT_EQ(EvalLua(L, "VRCompanion:getChainsawCutState()"), "delimbing");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "idle");
}

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
        ASSERT_GT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "the cut is out of the bar's reach";
        EXPECT_EQ(State(), "cutting") << "and still running: the Tool Hand's stop rule does not apply";
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
    AdvanceGameTime(3100);
    FRAMES(2);
    ExpectTheChainsawIsAsInTheGame("set down");
}

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

TEST_F(VirtualGamepadHandToolsChainsawTest, AnotherPlayersChainsawIsLeftAloneByTheReachLimitAndThePullOutRule) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        local remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteSaw = StubHandTool.chainsaw(remote)
        remoteSaw.isHeld = true
        remoteSaw.carryingPlayer = remote
        remoteSaw:attachTool()
        -- a targeter answer far past the limit, which the reach limit would take out: a take-out is a write to the answers
        _G.takenOut = 0
        local answers = { [HandToolChainsaw] = { node = pitch, distance = 9 } }
        remote.targeter = { closestTargetsByKey = setmetatable({}, { __index = answers, __newindex = function() takenOut = takenOut + 1 end }) }
        -- a cut under way, with the cut point 2 m from the saw's bar
        local spec = remoteSaw.spec_chainsaw
        local x, y, z = getWorldTranslation(remoteSaw.rootNode)
        setWorldTranslation(spec.cameraRotationNode, x + 2, y, z)
        spec.currentCutState = ChainsawCutState.CUTTING
        spec.cuttingTreeNode = pitch
    )luau", "=holdRemoteSaw"), 0);
    FRAMES(2);
    ASSERT_TRUE(True("VRCompanion:isHandHoldingHandTool(1)")) << "the local saw is in the hand";
    EXPECT_FALSE(True("isBelow(remoteSaw.rootNode, handHolder('right'))")) << "the other player's is not";

    ASSERT_EQ(RunLuauChunk(L, "HandToolChainsaw.onUpdate(remoteSaw, 16.667)", "=updateRemoteSaw"), 0);
    EXPECT_TRUE(True("remoteSaw.spec_chainsaw.currentCutState == ChainsawCutState.CUTTING")) << "the pull-out rule did not stop the cut";

    ASSERT_EQ(RunLuauChunk(L, "remoteSaw.spec_chainsaw.currentCutState = ChainsawCutState.IDLE; remoteSaw.spec_chainsaw.cuttingTreeNode = nil; "
                              "HandToolChainsaw.onUpdate(remoteSaw, 16.667)", "=updateIdleRemoteSaw"), 0);
    EXPECT_EQ(Number(L, "takenOut"), 0.0) << "the reach limit took no tree out of the other player's targeter";
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

class VirtualGamepadHandToolsChainsawVariantTest : public VirtualGamepadHandToolsChainsawTest {
protected:
    void ExpectOneWarningAndTheGamesChainsaw(const char* variant, const char* function) {
        ASSERT_NO_FATAL_FAILURE(Start(variant));
        ASSERT_NO_FATAL_FAILURE(HoldChainsaw(L));
        PICK_UP(kRight, kHandX, kHandY, kHandZ);
        AimHand(kRight, kHandX, kHandY, kHandZ, kBarForward, kBarYaw);
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

TEST_F(VirtualGamepadHandToolsChainsawTest, TheDiagnosticsNameTheGamesOwnReachPastTheTipAsTheDefault) {
    StartWithChainsawInTheRightHand();
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(diagnostics.find("reach past the tip: the game's own reach (default)"), std::string::npos) << diagnostics;
}

TEST_F(VirtualGamepadHandToolsChainsawTest, WithALimitedReachTheDiagnosticsNameTheReachPastTheTip) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(diagnostics.find("reach past the tip: 0.15 m (bar 0.40 m)"), std::string::npos) << diagnostics;
}

TEST_F(VirtualGamepadHandToolsChainsawTest, TheDiagnosticsSayWhenTheGamesMinimumCutDistanceCouldNotBeRead) {
    StartWithChainsawInTheRightHand("", "minimumMissing");
    FRAMES(2);
    const std::string diagnostics = EvalLua(L, "runCommand('vrHandToolsDiagnostics')");
    EXPECT_NE(diagnostics.find("minimum cut distance: could not be read"), std::string::npos) << diagnostics;
    EXPECT_NE(diagnostics.find("reach past the tip: not applied"), std::string::npos) << diagnostics;
}

// ---- vrChainsawReach: how far past the tip the chainsaw finds a trunk (ADR-0029, amended) ------------------------------
// The bar is 0.4 m long. A trunk 1 m past its tip is 1.4 m along the bar from its base; the stub targeter's own reach is 3 m from
// the ray's start, which is the game's minimum (0.7 m) behind the base.

class VirtualGamepadHandToolsChainsawReachSettingTest : public VirtualGamepadHandToolsChainsawTest {
protected:
    VirtualGamepadHandToolsChainsawReachSettingTest() { settingsFile = true; }

    std::unique_ptr<FakeTrackedHeadset> laterHeadset;
    std::unique_ptr<fs25vr::VRBridge> laterBridge;

    std::string Reach(const char* argument = nullptr) {
        const std::string command = argument == nullptr ? std::string("runCommand('vrChainsawReach')")
                                                        : std::string("runCommand('vrChainsawReach', '") + argument + "')";
        return EvalLua(L, command);
    }
    bool Found() { return True("saw.spec_chainsaw.targetedTree ~= nil and saw.spec_chainsaw.targetedTree.node == barTree.node"); }
    std::string ReachDiagnostics() { return EvalLua(L, "runCommand('vrHandToolsDiagnostics')"); }

    void ExpectTheCutRingStaysAroundACut() {
        ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(kBarTreeDistance));
        FRAMES(2);
        ASSERT_TRUE(Found());
        ASSERT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)"));

        headset.SetHandInputs(kRight, Trigger(1.0f));
        FRAMES(3);
        ASSERT_EQ(State(), "cutting");
        headset.SetHandInputs(kRight, Trigger(0.0f));
        for (int frame = 0; frame < 3; ++frame) {
            FRAMES(1);
            EXPECT_EQ(State(), "idle");
            if (frame > 0) {
                EXPECT_TRUE(Found()) << "frame " << frame;
                EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "the Cut Ring is back, frame " << frame;
            }
        }
        EXPECT_EQ(Cuts(), 0);
    }

    void StartANewSessionWithSavedReach(const char* saved) {
        lua_close(L);
        laterBridge.reset();
        laterHeadset = std::make_unique<FakeTrackedHeadset>();
        laterBridge = std::make_unique<fs25vr::VRBridge>(laterHeadset->manager, nullptr);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        ASSERT_TRUE(laterBridge->Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("console"), "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, StubChunk("xml_settings"), "=xmlStubs"), 0);
        const std::string load = std::string("savedXml['vrSettings.chainsawReach'] = ") + saved + "; VRCompanion:loadMap('x')";
        ASSERT_EQ(RunLuauChunk(L, load.c_str(), "=loadMapAgain"), 0);
    }
};

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, WithNoArgumentTheCommandSaysTheChainsawReachesTheGamesOwnDistanceByDefaultAndTheUsage) {
    StartWithChainsawInTheRightHand();
    const std::string reach = Reach();
    EXPECT_NE(reach.find("The chainsaw reaches the game's own distance (default)."), std::string::npos) << reach;
    EXPECT_NE(reach.find("Usage: vrChainsawReach [<metres, 0 to 3> | default | game] (saved)"), std::string::npos) << reach;
    EXPECT_NE(reach.find("vrChainsawReach 0.15 for contact-only cutting"), std::string::npos) << reach;
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, WithALimitedReachTheCommandSaysHowFarPastTheTipTheChainsawReaches) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0.15')", "=reach"), 0);
    const std::string reach = Reach();
    EXPECT_NE(reach.find("The chainsaw reaches 0.15 m past the tip of its bar."), std::string::npos) << reach;
    EXPECT_EQ(reach.find("(default)"), std::string::npos) << reach;
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, ATrunkAMetrePastTheTipIsFoundOutOfTheBoxAndNotWithAReachOfTheHandsWidth) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(1.4));
    FRAMES(2);
    EXPECT_TRUE(Found()) << "by default: the game's own reach";
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)")) << "and the Cut Ring shows";

    const std::string limited = Reach("0.15");
    EXPECT_NE(limited.find("0.15 m"), std::string::npos) << limited;
    EXPECT_NE(limited.find("Saved"), std::string::npos) << limited;
    FRAMES(2);
    EXPECT_FALSE(Found()) << "a limited reach hides it";
    EXPECT_FALSE(True("getVisibility(saw.spec_chainsaw.ringNode)"));

    const std::string answer = Reach("1.5");
    EXPECT_NE(answer.find("1.50 m"), std::string::npos) << answer;
    EXPECT_NE(answer.find("Saved"), std::string::npos) << answer;
    FRAMES(2);
    EXPECT_TRUE(Found());
    EXPECT_NE(Reach().find("1.50 m"), std::string::npos);
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, OutOfTheBoxATrunkAMetrePastTheTipIsCutAndSlicedAndOneTheBarIsAgainstToo) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(1.4));
    FRAMES(4);
    EXPECT_EQ(State(), "cutting");
    FRAMES(60);
    EXPECT_EQ(Cuts(), 1);
    EXPECT_EQ(State(), "idle");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(kBarTreeDistance));
    FRAMES(2);
    EXPECT_TRUE(Found());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "cutting");
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, ATrunkJustBeyondALimitedReachIsFoundAndCutWithAWiderReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.7));
    FRAMES(2);
    ASSERT_FALSE(Found());
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0.5')", "=reach"), 0);
    FRAMES(2);
    EXPECT_TRUE(Found());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "cutting");
    FRAMES(60);
    EXPECT_EQ(Cuts(), 1);
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AShorterReachAlsoMovesTheEdgeOfTheReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0')", "=reachNone"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.5));
    FRAMES(2);
    EXPECT_FALSE(Found());
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.4));
    FRAMES(2);
    EXPECT_TRUE(Found());
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AWiderReachLetsACutRunWhileTheBarIsPulledFurtherFromTheWoodThanAReachOfTheHandsWidthAllows) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '1.5')", "=reach"), 0);
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));

    PullTheHandBack(0.35f);
    FRAMES(2);
    ASSERT_GT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "further from the bar than a reach of 0.15";
    EXPECT_EQ(State(), "cutting") << "within the wider reach the cut goes on";
    FRAMES(60);
    EXPECT_EQ(Cuts(), 1);
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, DefaultPutsTheReachBackToTheGamesOwn) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0.15')", "=reach"), 0);
    const std::string answer = Reach("default");
    EXPECT_NE(answer.find("the game's own distance (default)"), std::string::npos) << answer;
    EXPECT_NE(answer.find("Saved"), std::string::npos) << answer;
    EXPECT_NE(Reach().find("The chainsaw reaches the game's own distance (default)."), std::string::npos);

    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(1.4));
    FRAMES(2);
    EXPECT_TRUE(Found());
    EXPECT_NE(ReachDiagnostics().find("reach past the tip: the game's own reach (default)"), std::string::npos) << ReachDiagnostics();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, GameIsTheSameAsDefaultTheGamesOwnReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0.15')", "=reach"), 0);
    const std::string answer = Reach("game");
    EXPECT_NE(answer.find("the game's own distance (default)"), std::string::npos) << answer;
    EXPECT_NE(Reach().find("The chainsaw reaches the game's own distance (default)."), std::string::npos);
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.chainsawReach']"), "default");
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, TheGamesOwnReachFindsATrunkAsFarAsTheGamesTargeterAllowsStillBackedUpSoContactCuttingWorks) {
    StartWithChainsawInTheRightHand();

    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(2.0));
    FRAMES(2);
    EXPECT_TRUE(Found());
    EXPECT_TRUE(True("getVisibility(saw.spec_chainsaw.ringNode)"));

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(3.0));
    FRAMES(2);
    EXPECT_FALSE(Found()) << "the game's own reach is the limit";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}", "=clearTrees"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(kBarTreeDistance));
    FRAMES(2);
    EXPECT_TRUE(Found());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(State(), "cutting");
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, ByDefaultACutRunsWithTheBarClearOfTheTrunkUntilTheGamesOwnStopEndsItWithoutASlice) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(0.3));

    PullTheHandBack(0.35f);
    FRAMES(2);
    ASSERT_GT(Number(L, "cutDistanceFromTheBarsBase()"), 0.55) << "the bar is clear of the trunk";
    ASSERT_LT(Number(L, "distanceOfTheGamesLookRayFromTheCut()"), 1.5) << "which the game's own check still lets run";
    EXPECT_EQ(State(), "cutting");

    PullTheHandBack(0.9f);  // 1.2 m from the base: still within the game's 1.5 m
    FRAMES(3);
    ASSERT_LT(Number(L, "cutDistanceFromTheBarsBase()"), 1.5);
    EXPECT_EQ(State(), "cutting") << "the game measures from the bar's base, as before the ray was moved back";

    PullTheHandBack(1.4f);  // 1.7 m from the base
    FRAMES(3);
    ASSERT_GE(Number(L, "cutDistanceFromTheBarsBase()"), 1.5);
    ASSERT_GE(Number(L, "distanceOfTheGamesLookRayFromTheCut()"), 1.5);
    EXPECT_EQ(State(), "idle") << "the game's own stop ends it";
    EXPECT_EQ(Cuts(), 0) << "without a slice";
}


TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, WithAWideReachATrunkAMetrePastTheTipIsCutAndSlicedToo) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '1.5')", "=reach"), 0);
    ASSERT_NO_FATAL_FAILURE(StartCuttingATreeAlongTheBar(1.4));
    FRAMES(4);
    EXPECT_EQ(State(), "cutting");
    FRAMES(60);
    EXPECT_EQ(Cuts(), 1);
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, TheCutRingOfATrunkTheBarIsAgainstStaysWhenACutStartsAndWhenItIsLetGoByDefault) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(ExpectTheCutRingStaysAroundACut());
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, TheCutRingOfATrunkTheBarIsAgainstStaysWhenACutStartsAndWhenItIsLetGoWithALimitedReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(LimitTheReach());
    ASSERT_NO_FATAL_FAILURE(ExpectTheCutRingStaysAroundACut());
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AnInvalidReachIsRefusedWithAMessageAndLeavesTheValue) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0.4')", "=reach"), 0);
    for (const char* bad : { "banana", "-1", "-0.5", "inf", "nan", "1e999", "1 2" }) {
        SCOPED_TRACE(bad);
        const std::string answer = Reach(bad);
        EXPECT_NE(answer.find("Usage: vrChainsawReach"), std::string::npos) << answer;
        EXPECT_NE(Reach().find("0.40 m"), std::string::npos) << "the value is left as it was";
    }
    EXPECT_EQ(EvalLua(L, "savedXml['vrSettings.chainsawReach']"), "0.4") << "and so is the saved one";
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AReachBeyondTheMaximumIsClampedToThreeMetresWithAMessage) {
    StartWithChainsawInTheRightHand();
    const std::string answer = Reach("10");
    EXPECT_NE(answer.find("3.00 m"), std::string::npos) << answer;
    EXPECT_NE(answer.find("most"), std::string::npos) << answer;
    EXPECT_NE(ReachDiagnostics().find("reach past the tip: 3.00 m"), std::string::npos) << ReachDiagnostics();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, TheDiagnosticsShowTheCurrentReachOrThatItIsTheGamesOwn) {
    StartWithChainsawInTheRightHand();
    EXPECT_NE(ReachDiagnostics().find("reach past the tip: the game's own reach (default)"), std::string::npos) << ReachDiagnostics();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '1.5')", "=reach"), 0);
    EXPECT_NE(ReachDiagnostics().find("reach past the tip: 1.50 m"), std::string::npos) << ReachDiagnostics();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', 'game')", "=game"), 0);
    EXPECT_NE(ReachDiagnostics().find("reach past the tip: the game's own reach (default)"), std::string::npos) << ReachDiagnostics();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AGameWhoseChainsawLacksTheMinimumCutDistanceStillHasTheGamesOwnReachWhateverIsSet) {
    StartWithChainsawInTheRightHand("", "minimumMissing");
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '0')", "=reach"), 0);
    ASSERT_NO_FATAL_FAILURE(PlantTreeAlongTheBar(0.7));
    FRAMES(2);
    EXPECT_TRUE(Found()) << "found, as in the game";
    EXPECT_NE(ReachDiagnostics().find("reach past the tip: not applied"), std::string::npos) << ReachDiagnostics();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, TheReachIsSavedAcrossSessions) {
    StartWithChainsawInTheRightHand();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', '1.5')", "=reach"), 0);
    const std::string saved = EvalLua(L, "savedXml['vrSettings.chainsawReach']");
    ASSERT_FALSE(saved.empty());
    ASSERT_NE(saved, "nil");

    ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach(("'" + saved + "'").c_str()));
    EXPECT_NE(Reach().find("1.50 m"), std::string::npos) << Reach();

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrChainsawReach', 'default')", "=default"), 0);
    const std::string savedDefault = EvalLua(L, "savedXml['vrSettings.chainsawReach']");
    ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach(("'" + savedDefault + "'").c_str()));
    EXPECT_NE(Reach().find("the game's own distance (default)"), std::string::npos) << Reach();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AnOldSettingsFileWithANumberKeepsItAndOneWithGameLoadsTheGamesOwnReach) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach("'0.15'"));
    EXPECT_NE(Reach().find("The chainsaw reaches 0.15 m past the tip of its bar."), std::string::npos) << Reach();
    ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach("'0.4'"));
    EXPECT_NE(Reach().find("0.40 m"), std::string::npos) << Reach();
    ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach("'game'"));
    EXPECT_NE(Reach().find("the game's own distance (default)"), std::string::npos) << Reach();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AnOldSettingsFileWithoutTheReachLoadsTheDefault) {
    StartWithChainsawInTheRightHand();
    ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach("nil"));
    EXPECT_NE(Reach().find("the game's own distance (default)"), std::string::npos) << Reach();
}

TEST_F(VirtualGamepadHandToolsChainsawReachSettingTest, AnythingElseInTheSettingsFileLoadsTheGamesOwnReach) {
    StartWithChainsawInTheRightHand();
    for (const char* garbage : { "'banana'", "'-2'", "'inf'", "'nan'", "'1e999'", "true", "-3", "0/0" }) {
        SCOPED_TRACE(garbage);
        ASSERT_NO_FATAL_FAILURE(StartANewSessionWithSavedReach(garbage));
        EXPECT_NE(Reach().find("the game's own distance (default)"), std::string::npos) << Reach();
    }
}
