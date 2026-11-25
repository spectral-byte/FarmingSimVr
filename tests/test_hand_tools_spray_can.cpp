#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace hand_tools_test;

constexpr const char* kSprayCanHelpers = R"luau(
    function _G.isWhereTheGamePutsIt(tool)
        if getParent(tool.rootNode) ~= foot.cam.pitchNode then return false end
        local x, y, z = getTranslation(tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        return math.abs(x - p.x) < 1e-6 and math.abs(y - p.y) < 1e-6 and math.abs(z - p.z) < 1e-6
    end
    function _G.isInTheHand(tool, which) return isBelow(tool.rootNode, handHolder(which)) end
    function _G.canRay() return { StubHandTool.canRay(can) } end
    function _G.headRay() return { StubHandTool.originalGetLookRay(foot.player) } end
    function _G.targeterRay() return { StubHandTool.lastRay() } end
    function _G.sameRay(a, b)
        for i = 1, 6 do
            if a[i] == nil or b[i] == nil or math.abs(a[i] - b[i]) > 1e-3 then return false end
        end
        return true
    end
    function _G.apart(a, b) return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2) end
    function _G.rayText(r) return r[1] and string.format("(%.3f %.3f %.3f) -> (%.3f %.3f %.3f)", table.unpack(r)) or "none" end
)luau";

constexpr float kAimPitch = 1.4f;
constexpr float kAimYaw = 1.2f;

class VirtualGamepadHandToolsSprayCanTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    void Start() {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = StubChunk("xml_settings") + SprayCanStubs();
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kSprayCanHelpers, "=sprayCanHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void StartWithTheCanInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldSprayCan(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(EvalLua(L, "isInTheHand(can, 'right')"), "true") << EvalLua(L, "VRCompanion:getHandToolsDiagnostics()");
    }

    void AimTheHand(uint32_t hand, float x, float y, float z) {
        headset.SetHandPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
    }

    std::string InTheHand(const char* which) { return EvalLua(L, std::string("isInTheHand(can, '") + which + "')"); }
    std::string WhereTheGamePutsIt() { return EvalLua(L, "isWhereTheGamePutsIt(can)"); }
    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }

    bool FindsATrunkAt(const std::string& ray, double distance) {
        EXPECT_EQ(RunLuauChunk(L, ("StubHandTool.trees = {}; _G.trunk = StubHandTool.treeAt(" + ray + ", " + std::to_string(distance) + ", 0.15)").c_str(), "=plantTrunk"), 0);
        FRAMES(1);
        return EvalLua(L, "tostring((foot.player.targeter.closestTargetsByKey[HandToolSprayCan] or {}).node == trunk.node)") == "true";
    }
};

}

TEST_F(VirtualGamepadHandToolsSprayCanTest, TheCanHeldOnFootHangsBelowTheRightVirtualHandAndFollowsItAcrossFramesAndARenderWithNoUpdate) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(can.rootNode, 'right') }; _G.handBefore = { getWorldTranslation(handHolder('right')) }", "=before"), 0);

    MOVE_HAND(kRight, 0.55f, 0.10f, -0.45f);
    FRAMES(1);

    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local hx, hy, hz = getWorldTranslation(handHolder('right'))
        local moved = math.sqrt((hx - handBefore[1]) ^ 2 + (hy - handBefore[2]) ^ 2 + (hz - handBefore[3]) ^ 2)
        assert(moved > 0.3, "the hand moved " .. moved)
        local x, y, z = offsetInHolder(can.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the can stayed where it was in the hand")
    )luau", "=follows"), 0);

    ASSERT_EQ(RunLuauChunk(L, "_G.canBefore = { getWorldTranslation(can.rootNode) }", "=canBefore"), 0);
    ASSERT_EQ(RunLuauChunk(L, "setTranslation(foot.parent, 0.12, 0, 0.05); StubScene.updateCamera()", "=moveWithoutUpdate"), 0);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = getWorldTranslation(can.rootNode)
        assert(math.abs(x - canBefore[1] - 0.12) < 1e-3 and math.abs(y - canBefore[2]) < 1e-3 and math.abs(z - canBefore[3] - 0.05) < 1e-3,
            "the can moved by " .. (x - canBefore[1]) .. " " .. (z - canBefore[3]))
    )luau", "=carriedAlong"), 0);
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, TheCanIsWhereTheGamePutsItWhileTheToolHandIsSetDownOrUntrackedAndBackInTheHandWhenItIsUsedAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());

    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "set down: the game's own attachToolToCamera put the can in front of the view";
    EXPECT_EQ(InTheHand("right"), "false");
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("right"), "true") << "picked up again: the can is back in the hand";

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "untracked: the can is where the game puts it";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "tracked again: back in the hand";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, WithTheLeftHandChosenAsTheToolHandTheCanGoesIntoTheLeftVirtualHand) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldSprayCan(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=toolHandLeft"), 0);
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(InTheHand("left"), "true") << Dump();
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("left"), "true") << "the other hand being picked up does not take it";
    EXPECT_EQ(InTheHand("right"), "false");
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, WithTheCanInTheToolHandTheTargetersRayRunsAlongTheNozzleStartingHalfAMetreBehindItAndFollowsTheHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    FRAMES(2);

    EXPECT_NEAR(Number(L, "apart(targeterRay(), canRay())"), 0.5, 1e-3) << "the ray starts the game's minimum distance behind the nozzle";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local ray, nozzle = targeterRay(), canRay()
        for i = 4, 6 do assert(math.abs(ray[i] - nozzle[i]) < 1e-3, "it points along the nozzle: " .. rayText(ray) .. " against " .. rayText(nozzle)) end
        local behind = (nozzle[1] - ray[1]) * nozzle[4] + (nozzle[2] - ray[2]) * nozzle[5] + (nozzle[3] - ray[3]) * nozzle[6]
        assert(math.abs(behind - 0.5) < 1e-3, "behind the nozzle, on its line: " .. behind)
    )luau", "=behind"), 0);
    EXPECT_GT(Number(L, "apart(targeterRay(), headRay())"), 0.2) << "and not from the head";

    ASSERT_EQ(RunLuauChunk(L, "_G.before = targeterRay()", "=before"), 0);
    AimTheHand(kRight, 0.45f, 0.10f, -0.50f);
    FRAMES(2);
    EXPECT_NEAR(Number(L, "apart(targeterRay(), canRay())"), 0.5, 1e-3) << "it follows the hand";
    EXPECT_GT(Number(L, "apart(before, targeterRay())"), 0.2);
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, ATrunkRightAgainstTheNozzleAndOneAMetreOffAreFoundAndOneTwentyCentimetresBeyondThatIsNot) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    FRAMES(2);

    EXPECT_TRUE(FindsATrunkAt("canRay()", 0.02)) << "held right against the trunk, 2 cm from the nozzle";
    EXPECT_TRUE(FindsATrunkAt("canRay()", 0.2)) << "0.2 m from the nozzle";
    EXPECT_TRUE(FindsATrunkAt("canRay()", 0.9)) << "0.9 m from the nozzle";
    EXPECT_FALSE(FindsATrunkAt("canRay()", 1.2)) << "1.2 m from the nozzle is beyond the game's reach";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, TheTreeTheNozzlePointsAtIsFoundAndNotTheOneTheHeadLooksAt) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    FRAMES(2);
    ASSERT_LT(Number(L, "canRay()[4]*headRay()[4] + canRay()[5]*headRay()[5] + canRay()[6]*headRay()[6]"), 0.5)
        << "the can does not point where the head looks";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.trees = {}; _G.nearNozzle = StubHandTool.treeAt(canRay(), 0.4, 0.15); _G.nearHead = StubHandTool.treeAt(headRay(), 1.0, 0.15)", "=trees"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "tostring(foot.player.targeter.closestTargetsByKey[HandToolSprayCan].node == nearNozzle.node)"), "true")
        << "the tree at the nozzle, though the head looks at another";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, WhileTheToolHandIsSetDownOrUntrackedTheRayAndTheReachAreTheGamesAndTheNozzlesAgainWhenItIsPickedUp) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "untracked: the head's ray, " << EvalLua(L, "rayText(targeterRay())");
    EXPECT_FALSE(FindsATrunkAt("headRay()", 0.2)) << "and the game's minimum distance: a trunk against the face is not found";
    EXPECT_TRUE(FindsATrunkAt("headRay()", 1.0));
    EXPECT_TRUE(FindsATrunkAt("headRay()", 1.4));
    EXPECT_FALSE(FindsATrunkAt("headRay()", 1.7)) << "and its maximum";

    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    EXPECT_NEAR(Number(L, "apart(targeterRay(), canRay())"), 0.5, 1e-3) << "tracked again: the nozzle's ray";

    AdvanceGameTime(3100);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "set down: the head's ray";
}

namespace {

constexpr const char* kPlantATrunkAtTheNozzle = "StubHandTool.trees = {}; _G.trunk = StubHandTool.treeAt(canRay(), 0.4, 0.15)";

}

#define SPRAY_CAN_TRIGGER_HELPERS() \
    auto markers = [&]() { return static_cast<int>(Number(L, "#StubHandTool.markers")); }; \
    auto pad = [&]() { \
        XINPUT_STATE state{}; \
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS)); \
        return state.Gamepad; \
    }; \
    (void)markers; (void)pad

TEST_F(VirtualGamepadHandToolsSprayCanTest, OnePullOfTheToolHandsTriggerSendsOneMarkerForTheTreeTheNozzlePointsAtHoldingItSendsNoMoreAndASecondPullSendsASecond) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, kPlantATrunkAtTheNozzle, "=trunk"), 0);
    FRAMES(2);
    ASSERT_EQ(markers(), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(markers(), 1) << "the pull makes a marker";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.markers[1].shape == trunk.node and StubHandTool.markers[1].tool == can)"), "true")
        << "of the tree the nozzle points at, from the can";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local marker, ray = StubHandTool.markers[1], targeterRay()
        assert(math.abs(marker.x - ray[1]) < 1e-4 and math.abs(marker.y - ray[2]) < 1e-4 and math.abs(marker.z - ray[3]) < 1e-4,
            "the game was given the look ray's origin, the one behind the nozzle")
    )luau", "=markerOrigin"), 0);
    EXPECT_EQ(Number(L, "can.spec_sprayCan.remainingNumMarkers"), 49.0) << "and the can has one fewer";
    EXPECT_EQ(EvalLua(L, "tostring(#StubHandTool.placedMarkers == 1 and StubHandTool.placedMarkers[1].shape == trunk.node)"), "true")
        << "which the game then puts on the tree";

    FRAMES(60);
    EXPECT_EQ(EvalLua(L, "tostring(can.spec_sprayCan.isSpraying)"), "false");
    EXPECT_EQ(markers(), 1) << "held, the trigger makes no more";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    EXPECT_EQ(markers(), 1);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(markers(), 2) << "a second pull makes a second marker";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, APullAsShortAsOneFrameStillSendsItsMarker) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, kPlantATrunkAtTheNozzle, "=trunk"), 0);
    FRAMES(2);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    EXPECT_EQ(markers(), 1) << "tapped for a frame";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, AnEmptyCanAndATreeThePlayerMayNotMarkWarnAsInTheGameAndSendNoMarker) {
    ASSERT_NO_FATAL_FAILURE(Start());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "_G.can = StubHandTool.sprayCan(foot.player, { markers = 0 }); StubHandTool.hold(can)", "=holdEmptyCan"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(1);
    ASSERT_EQ(InTheHand("right"), "true");
    ASSERT_EQ(RunLuauChunk(L, kPlantATrunkAtTheNozzle, "=trunk"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.warnings = {}", "=forgetTheMarkerTypeMessage"), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(markers(), 0) << "an empty can marks nothing";
    EXPECT_EQ(Number(L, "#StubHandTool.warnings"), 1.0) << "and warns, once for the pull";
    EXPECT_EQ(EvalLua(L, "StubHandTool.warnings[1].text"), "warning_sprayCanIsEmpty");

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(can); _G.can = StubHandTool.sprayCan(foot.player); StubHandTool.hold(can); StubHandTool.forbid(trunk)", "=fullCanForbiddenTree"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.warnings = {}", "=forgetTheMarkerTypeMessage"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(markers(), 0) << "a tree the player may not mark gets no marker";
    EXPECT_EQ(Number(L, "#StubHandTool.warnings"), 1.0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.warnings[1].text"), "warning_youAreNotAllowedToMarkThisTree");
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, TheToolHandsTriggerReadsReleasedOnXInputWhileItHoldsTheCanAndTheOtherHandsTriggerStillReachesThePad) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, kPlantATrunkAtTheNozzle, "=trunk"), 0);
    FRAMES(2);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(markers(), 1);
    EXPECT_EQ(pad().bRightTrigger, 0) << "the trigger that sprays is not also a binding";

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(pad().bLeftTrigger, 128) << "the other hand's trigger keeps its binding";
    EXPECT_EQ(pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, ATriggerAlreadyHeldWhenTheCanComesIntoTheHandKeepsItsBindingAndSendsNoMarkerUntilLetGoAndPulledAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    SPRAY_CAN_TRIGGER_HELPERS();
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(pad().bRightTrigger, 255);

    ASSERT_NO_FATAL_FAILURE(HoldSprayCan(L));
    ASSERT_EQ(RunLuauChunk(L, kPlantATrunkAtTheNozzle, "=trunk"), 0);
    FRAMES(3);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << Dump();
    EXPECT_EQ(pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(markers(), 0) << "and sends no marker";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(pad().bRightTrigger, 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(markers(), 1) << "pulled afresh it does";
    EXPECT_EQ(pad().bRightTrigger, 0) << "and is not a binding";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, TheGamesOwnUseToolBindingStillMarksTreesWhileTheCanIsInTheToolHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, kPlantATrunkAtTheNozzle, "=trunk"), 0);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(can, 1)", "=bindingDown"), 0);
    FRAMES(1);
    EXPECT_EQ(markers(), 1) << "the binding marks the tree";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(can, 0)", "=bindingUp"), 0);
    FRAMES(2);
    EXPECT_EQ(markers(), 1);
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, AHeldStillTriggerDoesNotDropTheCanAndAfterLettingGoTheHandIsSetDownAsAlways) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    for (int second = 0; second < 6; ++second) {
        AdvanceGameTime(1000);
        FRAMES(1);
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << "six seconds of holding the trigger with a still hand";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false") << "set down after three still seconds without the trigger";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, InTheWorldTheToolHandHoldingTheCanIsNeverThePointerHandAndInAMenuEitherHandCanPoint) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    auto pointerHand = [&]() { return EvalLua(L, "VRCompanion:getPointerHand()"); };
    EXPECT_EQ(pointerHand(), "nil") << "the only hand in use holds the can";

    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    EXPECT_EQ(pointerHand(), "0") << "the other hand points at Click Points";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(pointerHand(), "0") << "and a pull of the Tool Hand's trigger does not move it";

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=menu"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(pointerHand(), "1") << "in a menu a pull on the right hand points with it, as ever";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, WithVrHandToolsOffTheCanTheRayTheReachAndTheTriggerAreTheGamesAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);

    EXPECT_EQ(WhereTheGamePutsIt(), "true");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the head's ray: " << EvalLua(L, "rayText(targeterRay())");
    EXPECT_TRUE(FindsATrunkAt("headRay()", 1.0)) << "the game's reach from the head";
    EXPECT_FALSE(FindsATrunkAt("headRay()", 0.2));
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(pad().bRightTrigger, 255) << "the trigger is the pad's";
    EXPECT_EQ(markers(), 0) << "and marks nothing";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(InTheHand("right"), "true") << "and on again the can is back in the hand";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, AnotherModsToolIsPlacedAndAimedByTheGameAndItsTriggerIsThePadsWhileTheSprayCanClassIsInTheGame) {
    ASSERT_NO_FATAL_FAILURE(Start());
    SPRAY_CAN_TRIGGER_HELPERS();
    ASSERT_NO_FATAL_FAILURE(HoldOtherModTool(L));
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "isWhereTheGamePutsIt(otherTool)"), "true");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the head's ray, not pulled back";
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(pad().bRightTrigger, 255);
    EXPECT_EQ(Number(L, "#StubHandTool.useCalls"), 0.0);
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, AnotherPlayersCanIsNeverTakenIntoTheToolHandAndDoesNotChangeTheRay) {
    ASSERT_NO_FATAL_FAILURE(Start());
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        local remote = setmetatable({ isOwner = false, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteCan = StubHandTool.sprayCan(remote)
        remoteCan.isHeld = true
        remoteCan.carryingPlayer = remote
        remoteCan:attachTool()
    )luau", "=holdRemoteCan"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "tostring(getParent(remoteCan.rootNode) == remoteCan.carryingPlayer.camera.pitchNode)"), "true") << "where the game put it";
    EXPECT_EQ(EvalLua(L, "isBelow(remoteCan.rootNode, handHolder('right'))"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "false");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true");
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    EXPECT_EQ(Number(L, "#StubHandTool.useCalls"), 0.0) << "and the trigger does not use it";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, VrHandToolOffsetSprayCanMovesTheCanInTheHandAndResetPutsItBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(can.rootNode, 'right') }", "=before"), 0);

    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset', 'spraycan', 'nudge', 0.02, 0, 0)").find("spraycan: position"), std::string::npos);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(can.rootNode, 'right')
        assert(math.abs(x - before[1] - 0.02) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the can moved 2 cm along the hand's x: " .. (x - before[1]))
    )luau", "=nudged"), 0);

    EvalLua(L, "runCommand('vrHandToolOffset', 'spraycan', 'reset')");
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(can.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4, "back where it was")
    )luau", "=reset"), 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset')").find("spraycan"), std::string::npos) << "the list of tools names it";
}

TEST_F(VirtualGamepadHandToolsSprayCanTest, TheDiagnosticsShowTheSprayCanAsASupportedKindWithItsLookRayFromTheToolAndTheHeadsWhenSetDown) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheCanInTheRightHand());
    EXPECT_NE(Dump().find("held Hand Tool: spraycan (supported)"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("in the Tool Hand: yes"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("from the tool"), std::string::npos) << Dump();

    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_NE(Dump().find("in the Tool Hand: no"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("from the head"), std::string::npos) << Dump();
}
