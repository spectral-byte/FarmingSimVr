#include "hand_tools_test_support.h"
#include "input/virtual_gamepad.h"
#include <algorithm>
#include <string>

namespace {

using namespace hand_tools_test;

constexpr const char* kHorseBrushHelpers = R"luau(
    function _G.isWhereTheGamePutsIt(tool)
        if getParent(tool.rootNode) ~= foot.cam.pitchNode then return false end
        local x, y, z = getTranslation(tool.rootNode)
        local p = StubHandTool.CAMERA_PLACEMENT
        return math.abs(x - p.x) < 1e-6 and math.abs(y - p.y) < 1e-6 and math.abs(z - p.z) < 1e-6
    end
    function _G.isInTheHand(tool, which) return isBelow(tool.rootNode, handHolder(which)) end
    function _G.bristleRay() return { StubHandTool.bristleRay(brush) } end
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
    function _G.plantAnimal(ray, distance)
        StubHandTool.animals = {}
        _G.animal = StubHandTool.animalAt(ray, distance, 0.4)
        return animal
    end
    function _G.found() return (foot.player.targeter.closestTargetsByKey[HandToolHorseBrush] or {}).node end
    function _G.cleaned() return 50 - animal.cluster.dirt end
)luau";

constexpr float kAimPitch = 1.4f;
constexpr float kAimYaw = 1.2f;

// Pulses stronger than this are not "light" (the lance's pressure is 0.35, the chainsaw's cut 0.6).
constexpr float kLight = 0.5f;

class VirtualGamepadHandToolsHorseBrushTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };
    std::string stubs;

    void Start(const char* handler = "ok", const char* lookup = "ok") {
        headset.SetHeadPose(HandAt(0.0f, 0.0f, 0.0f));
        stubs = StubChunk("xml_settings") + HorseBrushStubs(handler, lookup);
        StartOptions options;
        options.extraStubs = stubs.c_str();
        ASSERT_NO_FATAL_FAILURE(StartHandToolsMod(L, bridge, options));
        ASSERT_EQ(RunLuauChunk(L, kHorseBrushHelpers, "=horseBrushHelpers"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        SetGameTime(10000);
    }

    void StartWithTheBrushInTheRightHand() {
        ASSERT_NO_FATAL_FAILURE(Start());
        ASSERT_NO_FATAL_FAILURE(HoldHorseBrush(L));
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
        FRAMES(1);
        ASSERT_EQ(EvalLua(L, "isInTheHand(brush, 'right')"), "true") << Dump();
    }

    void AimTheHand(uint32_t hand, float x, float y, float z) {
        headset.SetHandPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
        headset.SetHandGripPose(hand, HandAt(x, y, z, kAimPitch, kAimYaw));
    }

    std::string InTheHand(const char* which) { return EvalLua(L, std::string("isInTheHand(brush, '") + which + "')"); }
    std::string WhereTheGamePutsIt() { return EvalLua(L, "isWhereTheGamePutsIt(brush)"); }
    std::string Dump() { return EvalLua(L, "VRCompanion:getHandToolsDiagnostics()"); }

    size_t PulsesTo(int hand) const {
        size_t count = 0;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) ++count;
        }
        return count;
    }
    float StrongestTo(int hand) const {
        float strongest = 0.0f;
        for (const auto& pulse : headset.HapticPulses()) {
            if (pulse.hand == hand) strongest = std::max(strongest, pulse.amplitude);
        }
        return strongest;
    }
    void ForgetPulses() { headset.runtime->controllers.hapticPulses.clear(); }

    int LongestSilenceTo(int hand, int frames) {
        int longest = 0, current = 0;
        for (int i = 0; i < frames; ++i) {
            const size_t before = PulsesTo(hand);
            FRAMES(1);
            if (PulsesTo(hand) > before) {
                current = 0;
            } else {
                longest = std::max(longest, ++current);
            }
        }
        return longest;
    }
};

}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheBrushHeldOnFootHangsBelowTheRightVirtualHandAndFollowsItAcrossFramesAndARenderWithNoUpdate) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(brush.rootNode, 'right') }; _G.handBefore = { getWorldTranslation(handHolder('right')) }", "=before"), 0);

    MOVE_HAND(kRight, 0.55f, 0.10f, -0.45f);
    FRAMES(1);

    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local hx, hy, hz = getWorldTranslation(handHolder('right'))
        local moved = math.sqrt((hx - handBefore[1]) ^ 2 + (hy - handBefore[2]) ^ 2 + (hz - handBefore[3]) ^ 2)
        assert(moved > 0.3, "the hand moved " .. moved)
        local x, y, z = offsetInHolder(brush.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the brush stayed where it was in the hand")
    )luau", "=follows"), 0);

    ASSERT_EQ(RunLuauChunk(L, "_G.brushBefore = { getWorldTranslation(brush.rootNode) }", "=brushBefore"), 0);
    ASSERT_EQ(RunLuauChunk(L, "setTranslation(foot.parent, 0.12, 0, 0.05); StubScene.updateCamera()", "=moveWithoutUpdate"), 0);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = getWorldTranslation(brush.rootNode)
        assert(math.abs(x - brushBefore[1] - 0.12) < 1e-3 and math.abs(y - brushBefore[2]) < 1e-3 and math.abs(z - brushBefore[3] - 0.05) < 1e-3,
            "the brush moved by " .. (x - brushBefore[1]) .. " " .. (z - brushBefore[3]))
    )luau", "=carriedAlong"), 0);
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheBrushIsWhereTheGamePutsItWhileTheToolHandIsSetDownOrUntrackedAndBackInTheHandWhenItIsUsedAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());

    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "set down: the game's own attachToolToCamera put the brush in front of the view";
    EXPECT_EQ(InTheHand("right"), "false");
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("right"), "true") << "picked up again: the brush is back in the hand";

    headset.SetHandTracked(kRight, false);
    FRAMES(1);
    EXPECT_EQ(WhereTheGamePutsIt(), "true") << "untracked: the brush is where the game puts it";
    headset.SetHandTracked(kRight, true);
    FRAMES(1);
    EXPECT_EQ(InTheHand("right"), "true") << "tracked again: back in the hand";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheLeftHandChosenAsTheToolHandTheBrushGoesIntoTheLeftVirtualHand) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldHorseBrush(L));
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrToolHand', 'left')", "=toolHandLeft"), 0);
    PICK_UP(kLeft, -0.25f, -0.10f, -0.30f);
    FRAMES(1);

    EXPECT_EQ(InTheHand("left"), "true") << Dump();
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    EXPECT_EQ(InTheHand("left"), "true") << "the other hand being picked up does not take it";
    EXPECT_EQ(InTheHand("right"), "false");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheBrushInTheToolHandTheTargetersRayRunsAlongTheBristlesStartingHalfAMetreBehindThemAndFollowsTheHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    FRAMES(2);

    EXPECT_NEAR(Number(L, "apart(targeterRay(), bristleRay())"), 0.5, 1e-3) << "the ray starts the game's minimum distance behind the bristles";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local ray, bristles = targeterRay(), bristleRay()
        for i = 4, 6 do assert(math.abs(ray[i] - bristles[i]) < 1e-3, "it points along the bristles: " .. rayText(ray) .. " against " .. rayText(bristles)) end
        local behind = (bristles[1] - ray[1]) * bristles[4] + (bristles[2] - ray[2]) * bristles[5] + (bristles[3] - ray[3]) * bristles[6]
        assert(math.abs(behind - 0.5) < 1e-3, "behind the bristles, on their line: " .. behind)
    )luau", "=behind"), 0);
    EXPECT_GT(Number(L, "apart(targeterRay(), headRay())"), 0.2) << "and not from the head";

    ASSERT_EQ(RunLuauChunk(L, "_G.before = targeterRay()", "=before"), 0);
    AimTheHand(kRight, 0.45f, 0.10f, -0.50f);
    FRAMES(2);
    EXPECT_NEAR(Number(L, "apart(targeterRay(), bristleRay())"), 0.5, 1e-3) << "it follows the hand";
    EXPECT_GT(Number(L, "apart(before, targeterRay())"), 0.2);
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WhileTheToolHandIsSetDownOrUntrackedTheRayIsTheHeadsAndTheGames3MetreReachByLookingWorksAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "untracked: the head's ray, " << EvalLua(L, "rayText(targeterRay())");
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(headRay(), 2.8)", "=farAnimal"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "tostring(found() == animal.node)"), "true") << "an animal 2.8 m away is found by looking";
    EXPECT_NE(EvalLua(L, "StubHandTool.inputHelp(brush)"), "") << "and the input help offers cleaning it";
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(headRay(), 0.3)", "=nearAnimal"), 0);
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "tostring(found())"), "nil") << "and the game's minimum distance: an animal against the face is not found";

    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    EXPECT_NEAR(Number(L, "apart(targeterRay(), bristleRay())"), 0.5, 1e-3) << "tracked again: the bristles' ray";

    AdvanceGameTime(3100);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "set down: the head's ray";
}

#define HORSE_BRUSH_HELPERS() \
    auto events = [&]() { return static_cast<int>(Number(L, "#StubHandTool.cleanEvents")); }; \
    auto help = [&]() { return EvalLua(L, "StubHandTool.inputHelp(brush)"); }; \
    auto pad = [&]() { \
        XINPUT_STATE state{}; \
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS)); \
        return state.Gamepad; \
    }; \
    (void)events; (void)help; (void)pad

// A second of game frames: longer than the two half seconds in which the game sends its clean events.
constexpr int kASecond = 60;

TEST_F(VirtualGamepadHandToolsHorseBrushTest, BristlesTenCentimetresFromTheAnimalWithTheTriggerHeldSendCleanEventsAndTheInputHelpOffersCleaning) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(2);
    EXPECT_EQ(help(), "Clean Silver") << "the game's input help names the animal the bristles are on";
    ASSERT_EQ(events(), 0);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    EXPECT_GE(events(), 1) << "holding the trigger cleans";
    EXPECT_GT(Number(L, "cleaned()"), 0.0) << "and the animal is cleaner";
    EXPECT_EQ(EvalLua(L, "tostring(StubHandTool.cleanEvents[1].husbandry == animal.husbandry and StubHandTool.cleanEvents[1].clusterId == animal.cluster.id)"), "true")
        << "the animal the bristles are on";
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.cleaningSoundPlaying)"), "true") << "and the game's cleaning sound plays";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, BristlesFortyCentimetresFromTheAnimalSendNothingAndTheInputHelpDoesNotOfferCleaning) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.4)", "=animal"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);

    EXPECT_EQ(EvalLua(L, "tostring(found() == animal.node)"), "true") << "the game's targeter does find the animal, 0.9 m from the ray's origin";
    EXPECT_EQ(help(), "") << "but it is out of the brush's touch: no cleaning is offered";
    EXPECT_EQ(events(), 0) << "and nothing is sent";
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.cleaningSoundPlaying)"), "false");
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.targetedHusbandry)"), "nil");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheTouchReachesAboutAQuarterMetrePastTheBristlesAndNoFurther) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.02)", "=animal"), 0);
    FRAMES(2);
    EXPECT_EQ(help(), "Clean Silver") << "right against the bristles";
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.2)", "=animal"), 0);
    FRAMES(2);
    EXPECT_EQ(help(), "Clean Silver") << "20 cm from them";
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.3)", "=animal"), 0);
    FRAMES(2);
    EXPECT_EQ(help(), "") << "30 cm from them is beyond";
}
TEST_F(VirtualGamepadHandToolsHorseBrushTest, ReleasingTheTriggerStopsTheCleanEventsAndPullingItAgainStartsThemAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(2);

    FRAMES(kASecond);
    EXPECT_EQ(events(), 0) << "with the trigger released the brush only touches the animal";
    EXPECT_NE(help(), "") << "(and the input help still offers cleaning)";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    const int whileHeld = events();
    ASSERT_GE(whileHeld, 1);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    const int atRelease = events();
    FRAMES(kASecond);
    EXPECT_EQ(events(), atRelease) << "released: no more events";
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "false");
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.cleaningSoundPlaying)"), "false");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    EXPECT_GT(events(), atRelease) << "pulled again: cleaning again";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, MovingTheBrushOffTheAnimalWithTheTriggerHeldStopsTheEventsAndMovingItBackStartsThemAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    ASSERT_GE(events(), 1);

    AimTheHand(kRight, 0.25f, 1.20f, -0.30f);
    FRAMES(2);
    const int atMoveOff = events();
    EXPECT_EQ(help(), "") << "off the animal: no cleaning is offered";
    FRAMES(kASecond);
    EXPECT_EQ(events(), atMoveOff) << "the trigger is held, the brush is not on the animal: no events";

    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(kASecond);
    EXPECT_GT(events(), atMoveOff) << "back on the animal with the trigger still held: cleaning again";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, SettingTheHandDownWhileCleaningStopsItAndPickingTheHandUpAgainDoesNotResumeCleaningOnItsOwn) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    ASSERT_GE(events(), 1);
    ASSERT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "true");

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "false") << "the game is not left cleaning";
    const int atSetDown = events();
    FRAMES(kASecond);
    EXPECT_EQ(events(), atSetDown);

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    headset.SetHandTracked(kRight, true);
    FRAMES(kASecond);
    ASSERT_EQ(InTheHand("right"), "true") << Dump();
    EXPECT_EQ(events(), atSetDown) << "the brush is back in the hand on the animal and does not clean on its own";
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "false");

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    EXPECT_GT(events(), atSetDown) << "pulled afresh it cleans";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, PuttingTheBrushAwayOrTurningHandToolsOffWhileCleaningLeavesTheGameNotCleaning) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    ASSERT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "true");

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "false") << "Hand Tools off while cleaning";
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    ASSERT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "true");

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.drop(brush)", "=putAway"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "false") << "the brush put away while cleaning";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheToolHandsTriggerReadsReleasedOnXInputWhileItHoldsTheBrushAndTheOtherHandsTriggerStillReachesThePad) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(2);

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    ASSERT_GE(events(), 1);
    EXPECT_EQ(pad().bRightTrigger, 0) << "the trigger that cleans is not also a binding";

    headset.SetHandInputs(kLeft, Trigger(0.5f));
    FRAMES(1);
    EXPECT_EQ(pad().bLeftTrigger, 128) << "the other hand's trigger keeps its binding";
    EXPECT_EQ(pad().bRightTrigger, 0);
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, ATriggerAlreadyHeldWhenTheBrushComesIntoTheHandKeepsItsBindingAndCleansNothingUntilLetGoAndPulledAgain) {
    ASSERT_NO_FATAL_FAILURE(Start());
    HORSE_BRUSH_HELPERS();
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_EQ(pad().bRightTrigger, 255);

    ASSERT_NO_FATAL_FAILURE(HoldHorseBrush(L));
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(kASecond);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << Dump();
    EXPECT_EQ(pad().bRightTrigger, 255) << "the trigger keeps its binding";
    EXPECT_EQ(events(), 0) << "and cleans nothing";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(1);
    EXPECT_EQ(pad().bRightTrigger, 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    EXPECT_GE(events(), 1) << "pulled afresh it does";
    EXPECT_EQ(pad().bRightTrigger, 0) << "and is not a binding";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheGamesOwnUseToolBindingStillCleansWhileTheBrushIsInTheToolHand) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(2);

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(brush, 1)", "=bindingDown"), 0);
    FRAMES(kASecond);
    EXPECT_GE(events(), 1) << "the binding cleans the animal the bristles are on";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(brush, 0)", "=bindingUp"), 0);
    FRAMES(2);
    const int atRelease = events();
    FRAMES(kASecond);
    EXPECT_EQ(events(), atRelease);
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, AHeldStillTriggerDoesNotDropTheBrushAndAfterLettingGoTheHandIsSetDownAsAlways) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
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
    EXPECT_EQ(EvalLua(L, "tostring(brush.spec_horseBrush.isCleaning)"), "false");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, InTheWorldTheToolHandHoldingTheBrushIsNeverThePointerHandAndInAMenuEitherHandCanPoint) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    auto pointerHand = [&]() { return EvalLua(L, "VRCompanion:getPointerHand()"); };
    EXPECT_EQ(pointerHand(), "nil") << "the only hand in use holds the brush";

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

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheToolHandBuzzesLightlyAndSteadilyWhileTheBrushCleansAnAnimalAndOnlyThen) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(5);
    ForgetPulses();
    FRAMES(kASecond);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the brush on the animal with the trigger released: only held, silent";

    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_GT(PulsesTo(kRight), 0u) << "cleaning: the hand buzzes";
    EXPECT_GT(StrongestTo(kRight), 0.0f);
    EXPECT_LE(StrongestTo(kRight), kLight) << "lightly";
    EXPECT_LE(LongestSilenceTo(kRight, kASecond), 5) << "and steadily: the pulses overlap";
    EXPECT_EQ(PulsesTo(kLeft), 0u) << "only the Tool Hand feels it";

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    ForgetPulses();
    FRAMES(kASecond);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "released: quiet again";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheToolHandIsSilentWhileTheBrushOnlyTouchesAirOrAnAnimalOutOfTouchEvenWithTheTriggerHeld) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    ForgetPulses();
    FRAMES(kASecond);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the trigger held over nothing";

    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.4)", "=animal"), 0);
    FRAMES(5);
    ForgetPulses();
    FRAMES(kASecond);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "an animal 40 cm from the bristles is out of touch: nothing is cleaned and nothing is felt";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheBuzzEndsWhenTheHandLosesTrackingAndNothingIsFeltWithHandToolsOffOrWhenUntracked) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(5);
    ASSERT_GT(PulsesTo(kRight), 0u);

    headset.SetHandTracked(kRight, false);
    FRAMES(3);
    ForgetPulses();
    FRAMES(kASecond);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "the hand lost tracking while the brush cleans: no more buzz";

    headset.SetHandTracked(kRight, true);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(3);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    ASSERT_GT(PulsesTo(kRight), 0u) << "tracked again and pulled afresh: cleaning, buzzing";
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(3);
    ForgetPulses();
    FRAMES(kASecond);
    EXPECT_EQ(PulsesTo(kRight), 0u) << "Hand Tools off: nothing is added to the game";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, NothingIsDrawnInTheTrackedToolHandAndTheGamesCrosshairsComeBackUntrackedSetDownOrWithHandToolsOrMotionControllersOff) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    auto draws = [&]() {
        EXPECT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
        return EvalLua(L, "StubHandTool.rendering(brush)");
    };
    EXPECT_EQ(draws(), "") << "in the tracked Tool Hand nothing is drawn at the centre of the view";

    headset.SetHandTracked(kRight, false);
    FRAMES(2);
    EXPECT_EQ(draws(), "defaultCrosshair") << "untracked: as the game draws it";
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(headRay(), 1.0)", "=animalByLooking"), 0);
    FRAMES(2);
    EXPECT_EQ(draws(), "brushCrosshair") << "and the brush icon while an animal is looked at";
    headset.SetHandTracked(kRight, true);
    FRAMES(2);
    ASSERT_EQ(InTheHand("right"), "true");
    EXPECT_EQ(draws(), "") << "tracked again: nothing";

    AdvanceGameTime(3100);
    FRAMES(2);
    EXPECT_NE(draws(), "") << "set down: as the game draws it";
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    ASSERT_EQ(InTheHand("right"), "true");
    EXPECT_EQ(draws(), "") << "picked up again: nothing";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);
    EXPECT_NE(draws(), "") << "vrHandTools off: as the game draws it";
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    FRAMES(2);
    EXPECT_EQ(draws(), "") << "vrHandTools on: nothing again";

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrMotionControllers', 'off')", "=mcOff"), 0);
    FRAMES(2);
    EXPECT_NE(draws(), "") << "Motion Controllers off: as the game draws it";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithVrHandToolsOffTheBrushTheRayTheReachAndTheTriggerAreTheGamesAgain) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'off')", "=off"), 0);
    FRAMES(2);

    EXPECT_EQ(WhereTheGamePutsIt(), "true");
    EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "the head's ray: " << EvalLua(L, "rayText(targeterRay())");
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(headRay(), 2.5)", "=farAnimal"), 0);
    FRAMES(2);
    EXPECT_EQ(help(), "Clean Silver") << "the game's reach from the head, which a hand's touch cap does not shorten";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(kASecond);
    EXPECT_EQ(pad().bRightTrigger, 255) << "the trigger is the pad's";
    EXPECT_EQ(events(), 0) << "and cleans nothing";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(brush, 1)", "=binding"), 0);
    FRAMES(kASecond);
    EXPECT_GE(events(), 1) << "the game's binding cleans the animal looked at, as in the game";

    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.pressUseBinding(brush, 0)", "=bindingUp"), 0);
    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrHandTools', 'on')", "=on"), 0);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(InTheHand("right"), "true") << "and on again the brush is back in the hand";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, AnotherModsToolIsPlacedAndAimedByTheGameAndItsTriggerIsThePadsWhileTheHorseBrushClassIsInTheGame) {
    ASSERT_NO_FATAL_FAILURE(Start());
    HORSE_BRUSH_HELPERS();
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

TEST_F(VirtualGamepadHandToolsHorseBrushTest, AnotherPlayersBrushIsNeverTakenIntoTheToolHandAndItsAnimalLookupIsNotCapped) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    HORSE_BRUSH_HELPERS();
    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.4)", "=outOfTouch"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local pitch = StubScene.newNode("remotePitch", 0, 4, 1.7, 4)
        _G.remote = setmetatable({ isOwner = false, farmId = 1, camera = { isFirstPerson = true, pitchNode = pitch } }, { __index = _G.Player })
        _G.remoteBrush = StubHandTool.horseBrush(remote)
        remoteBrush.isHeld = true
        remoteBrush.carryingPlayer = remote
        remoteBrush:attachTool()
    )luau", "=holdRemoteBrush"), 0);
    FRAMES(2);

    EXPECT_EQ(EvalLua(L, "tostring(getParent(remoteBrush.rootNode) == remote.camera.pitchNode)"), "true") << "where the game put it";
    EXPECT_EQ(EvalLua(L, "isBelow(remoteBrush.rootNode, handHolder('right'))"), "false");
    EXPECT_EQ(InTheHand("right"), "true") << "the local player's brush stays in the hand";
    EXPECT_EQ(help(), "") << "the local player's brush is capped...";
    EXPECT_EQ(EvalLua(L, "tostring(HandToolHorseBrush.getHusbandryAndClusterFromNode(foot.player, animal.node))"), "nil");
    EXPECT_EQ(EvalLua(L, "tostring(HandToolHorseBrush.getHusbandryAndClusterFromNode(remote, animal.node) == animal.husbandry)"), "true")
        << "...the other player's lookup is the game's, whatever the local targeter found";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    ASSERT_GE(Number(L, "#StubHandTool.useCalls"), 1.0);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        for _, call in ipairs(StubHandTool.useCalls) do assert(call.self == brush, "the trigger used another player's brush") end
    )luau", "=onlyTheLocalBrush"), 0) << "the trigger uses the local player's brush only";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheLanceInHandTheGamesAnimalLookupIsNotCapped) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_NO_FATAL_FAILURE(HoldLance(L));
    ASSERT_EQ(RunLuauChunk(L, "_G.brush = StubHandTool.horseBrush(foot.player); plantAnimal({ 0, 1.7, 0, 0, 0, -1 }, 2.0)", "=animalAndABrushOnTheGround"), 0);
    PICK_UP(kRight, 0.25f, -0.10f, -0.30f);
    AimTheHand(kRight, 0.25f, -0.10f, -0.30f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandHoldingHandTool(1)"), "true") << Dump();

    EXPECT_EQ(EvalLua(L, "tostring(HandToolHorseBrush.getHusbandryAndClusterFromNode(foot.player, animal.node) == animal.husbandry)"), "true");
}

#define EXPECT_THE_GAMES_BEHAVIOUR_AND_ONE_WARNING_NAMING(functionName) \
    do { \
        ASSERT_NO_FATAL_FAILURE(HoldHorseBrush(L)); \
        PICK_UP(kRight, 0.25f, -0.10f, -0.30f); \
        AimTheHand(kRight, 0.25f, -0.10f, -0.30f); \
        FRAMES(2); \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "exactly one warning"; \
        EXPECT_EQ(CountLogLines(functionName), 1) << "that names the function"; \
        EXPECT_EQ(InTheHand("right"), "false") << "Hand Tools gave up: the brush is where the game puts it"; \
        EXPECT_EQ(WhereTheGamePutsIt(), "true"); \
        EXPECT_EQ(EvalLua(L, "sameRay(targeterRay(), headRay())"), "true") << "and aimed by looking"; \
        headset.SetHandInputs(kRight, Trigger(1.0f)); \
        FRAMES(kASecond); \
        EXPECT_EQ(pad().bRightTrigger, 255) << "the trigger is the pad's"; \
        EXPECT_EQ(Number(L, "#StubHandTool.useCalls"), 0.0) << "and uses nothing"; \
        EXPECT_EQ(CountLogLines("[Warning] Hand Tools"), 1) << "and the warning is not repeated"; \
    } while (0)

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheUseHandlerMissingTheBrushIsTheGamesAndTheLogHasOneWarning) {
    ASSERT_NO_FATAL_FAILURE(Start("missing", "ok"));
    HORSE_BRUSH_HELPERS();
    EXPECT_THE_GAMES_BEHAVIOUR_AND_ONE_WARNING_NAMING("HandToolHorseBrush.onBrushAction");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheUseHandlerReshapedTheBrushIsTheGamesAndTheLogHasOneWarning) {
    ASSERT_NO_FATAL_FAILURE(Start("reshaped", "ok"));
    HORSE_BRUSH_HELPERS();
    EXPECT_THE_GAMES_BEHAVIOUR_AND_ONE_WARNING_NAMING("HandToolHorseBrush.onBrushAction");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheAnimalLookupMissingTheBrushIsTheGamesAndTheLogHasOneWarning) {
    ASSERT_NO_FATAL_FAILURE(Start("ok", "missing"));
    HORSE_BRUSH_HELPERS();
    EXPECT_THE_GAMES_BEHAVIOUR_AND_ONE_WARNING_NAMING("HandToolHorseBrush.getHusbandryAndClusterFromNode");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, WithTheAnimalLookupReshapedTheBrushIsTheGamesAndTheLogHasOneWarning) {
    ASSERT_NO_FATAL_FAILURE(Start("ok", "reshaped"));
    HORSE_BRUSH_HELPERS();
    EXPECT_THE_GAMES_BEHAVIOUR_AND_ONE_WARNING_NAMING("HandToolHorseBrush.getHusbandryAndClusterFromNode");
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, AGameUpdateThatReshapesTheLookupLiftsEveryWrapNotOnlyTheLookupsOwn) {
    ASSERT_NO_FATAL_FAILURE(Start("ok", "reshaped"));
    ASSERT_NO_FATAL_FAILURE(HoldHorseBrush(L));

    EXPECT_EQ(EvalLua(L, "tostring(HandTool.attachToolToCamera == StubHandTool.originalAttachToolToCamera)"), "true") << "the placement wrap is lifted too";
    EXPECT_EQ(EvalLua(L, "tostring(Player.getLookRay == StubHandTool.originalGetLookRay)"), "true") << "and the look ray wrap";
    ASSERT_EQ(RunLuauChunk(L, "StubHandTool.draw()", "=draw"), 0);
    EXPECT_EQ(EvalLua(L, "StubHandTool.rendering(brush)"), "defaultCrosshair") << "and the draw is the game's";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, VrHandToolOffsetHorseBrushMovesTheBrushInTheHandAndResetPutsItBack) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    ASSERT_EQ(RunLuauChunk(L, "_G.before = { offsetInHolder(brush.rootNode, 'right') }", "=before"), 0);

    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset', 'horsebrush', 'nudge', 0.02, 0, 0)").find("horsebrush: position"), std::string::npos);
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(brush.rootNode, 'right')
        assert(math.abs(x - before[1] - 0.02) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4,
            "the brush moved 2 cm along the hand's x: " .. (x - before[1]))
    )luau", "=nudged"), 0);

    EvalLua(L, "runCommand('vrHandToolOffset', 'horsebrush', 'reset')");
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local x, y, z = offsetInHolder(brush.rootNode, 'right')
        assert(math.abs(x - before[1]) < 1e-4 and math.abs(y - before[2]) < 1e-4 and math.abs(z - before[3]) < 1e-4, "back where it was")
    )luau", "=reset"), 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolOffset')").find("horsebrush"), std::string::npos) << "the list of tools names it";
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheDiagnosticsShowTheBrushAsASupportedKindWhetherAnAnimalIsInReachWithItsDistanceWhetherItCleansAndTheWrapsStatus) {
    ASSERT_NO_FATAL_FAILURE(StartWithTheBrushInTheRightHand());
    EXPECT_NE(Dump().find("held Hand Tool: horsebrush (supported)"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("in the Tool Hand: yes"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("from the tool"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("horseBrushLookup wrap: installed"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("horseBrushOnDraw wrap: installed"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("centre overlays held back: yes"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("animal in reach: no"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("cleaning: no"), std::string::npos) << Dump();

    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.1)", "=animal"), 0);
    FRAMES(2);
    EXPECT_NE(Dump().find("animal in reach: yes (0.10 m)"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("cleaning: no"), std::string::npos) << Dump();
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_NE(Dump().find("cleaning: yes"), std::string::npos) << Dump();
    EXPECT_NE(EvalLua(L, "runCommand('vrHandToolsDiagnostics')").find("animal in reach: yes (0.10 m)"), std::string::npos);

    ASSERT_EQ(RunLuauChunk(L, "plantAnimal(bristleRay(), 0.4)", "=animalOutOfTouch"), 0);
    FRAMES(2);
    EXPECT_NE(Dump().find("animal in reach: no"), std::string::npos) << "out of touch: " << Dump();
    EXPECT_NE(Dump().find("cleaning: no"), std::string::npos) << Dump();

    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    AdvanceGameTime(3100);
    FRAMES(1);
    EXPECT_NE(Dump().find("in the Tool Hand: no"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("from the head"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("centre overlays held back: no"), std::string::npos) << Dump();
}

TEST_F(VirtualGamepadHandToolsHorseBrushTest, TheDiagnosticsSayTheLookupWrapGaveUpWhenTheLookupIsReshaped) {
    ASSERT_NO_FATAL_FAILURE(Start("ok", "reshaped"));
    ASSERT_NO_FATAL_FAILURE(HoldHorseBrush(L));
    EXPECT_NE(Dump().find("horseBrushLookup wrap: gave up"), std::string::npos) << Dump();
    EXPECT_NE(Dump().find("horseBrushOnDraw wrap: gave up"), std::string::npos) << Dump();
}