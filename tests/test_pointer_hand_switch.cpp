// Pointer Hand switching (spec #71), seen from outside: the real Companion Mod runs against
// the real VRBridge over FakeTrackedHeadset, with the Interactive Control stand-in from
// companion_mod_stubs.h (which Click Point executed) and a stub GUI (which clicks reached a menu).
// With both Motion Controllers in use, a trigger pull on the hand that is not the Pointer Hand moves
// the Pointer Hand to it and is consumed; the test pulls triggers on the fake hands and watches what
// the mod does with them.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose HandAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

constexpr const char* kConsoleStubs = R"luau(
    _G.registeredCommands = {}
    _G.addConsoleCommand = function(name, desc, method, target)
        _G.registeredCommands[name] = { desc = desc, method = method, target = target }
    end
    _G.removeConsoleCommand = function(name) _G.registeredCommands[name] = nil end
    _G.runCommand = function(name, ...)
        local command = _G.registeredCommands[name]
        assert(command ~= nil, name .. " is not registered")
        return command.target[command.method](command.target, ...)
    end
)luau";

// The game's GUI as the mod sees it: it records every mouse event the mod delivers.
constexpr const char* kGuiStubs = R"luau(
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1 }
    _G.guiEvents = {}
    _G.g_gui = {
        getIsGuiVisible = function(self) return true end,
        mouseEvent = function(self, x, y, isDown, isUp, button)
            table.insert(_G.guiEvents, { x = x, y = y, down = isDown, up = isUp, button = button })
        end,
    }
    _G.guiDowns = function() local n = 0 for _, e in ipairs(_G.guiEvents) do if e.down then n = n + 1 end end return n end
)luau";

// Both hands are held 0.3 m ahead of the head and 0.3 m to either side, pointing straight ahead. In
// the cab (the player sits at (0, 1.5, 0) looking along world +Z, tracking X is world -X) the right
// hand's ray starts at world (-0.3, 1.5, 0.3) and the left hand's at (0.3, 1.5, 0.3).
class PointerHandSwitchTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    void StartMod() {
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok')", "=installIc"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubScene.cab({})", "=scene"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        // Each hand's own Click Point, straight ahead of it.
        ASSERT_EQ(RunLuauChunk(L, R"luau(
            StubIC.addPoint('rightPoint', -0.3, 1.5, 2.3)
            StubIC.addPoint('leftPoint', 0.3, 1.5, 2.3)
        )luau", "=addPoints"), 0);
    }

    // A frame the way the game runs it: camera update, Interactive Control's update, the mod's update.
    void Frames(int count) {
        for (int i = 0; i < count; ++i) {
            ASSERT_EQ(RunLuauChunk(L, "StubScene.updateCamera(); StubIC.tick()", "=preFrame"), 0);
            NextFrame(&headset);
        }
    }

    static float SideOf(uint32_t hand) { return hand == kRight ? 0.3f : -0.3f; }

    // Lifts the hand off the desk and holds it still where it points: it is now in use.
    void PickUp(uint32_t hand) {
        headset.SetHandPose(hand, HandAt(SideOf(hand) - 0.1f, 0.0f, -0.3f));
        Frames(1);
        headset.SetHandPose(hand, HandAt(SideOf(hand), 0.0f, -0.3f));
        Frames(1);
    }

    // The right hand has been the Pointer Hand since it was picked up; the left joins it.
    void BothHandsInUseRightIsPointerHand() {
        SetGameTime(10000);
        PickUp(kRight);
        PickUp(kLeft);
        Frames(2);
    }

    void Pull(uint32_t hand) { headset.SetHandInputs(hand, Trigger(1.0f)); }
    void Release(uint32_t hand) { headset.SetHandInputs(hand, Trigger(0.0f)); }
    std::string Executed() { return EvalLua(L, "table.concat(StubIC.executed, ',')"); }
    std::string PointerHand() { return EvalLua(L, "VRCompanion:getPointerHand()"); }
};

} // namespace

TEST_F(PointerHandSwitchTest, ATriggerPullOnTheOtherHandMakesItThePointerHandAndClicksNothing) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());
    ASSERT_EQ(PointerHand(), "1");
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "rightPoint");

    Pull(kLeft);
    Frames(1);

    EXPECT_EQ(PointerHand(), "0") << "the pull moved the Pointer Hand to the left hand";
    EXPECT_EQ(Executed(), "") << "the switching pull is consumed";
}

TEST_F(PointerHandSwitchTest, TheSwitchingPullClicksNothingEvenHeldThroughTheFollowingFramesAndTheNextPullClicks) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());

    Pull(kLeft);
    Frames(10);
    EXPECT_EQ(PointerHand(), "0");
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "leftPoint") << "the left hand's ray now selects";
    EXPECT_EQ(Executed(), "") << "a switching pull held on does not turn into a click";

    Release(kLeft);
    Frames(2);
    EXPECT_EQ(Executed(), "") << "letting go clicks nothing either";

    Pull(kLeft);
    Frames(1);
    EXPECT_EQ(Executed(), "leftPoint") << "the next pull on the new Pointer Hand clicks normally";
    Frames(5);
    EXPECT_EQ(Executed(), "leftPoint") << "once";
}

TEST_F(PointerHandSwitchTest, ASwitchingPullHeldOnDoesNotWorkAnAnalogClickPointEither) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.points[2].inRange = false
        StubIC.addPoint('leftLever', 0.3, 1.5, 2.3, { analog = true })
    )luau", "=addLever"), 0);
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());

    Pull(kLeft);
    Frames(6);
    ASSERT_EQ(PointerHand(), "0");
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "leftLever");
    EXPECT_EQ(Executed(), "") << "the held switching pull does not act on the lever";

    Release(kLeft);
    Frames(2);
    Pull(kLeft);
    Frames(3);
    EXPECT_NE(Executed(), "") << "the next pull works the lever, for as long as it is held";
}

TEST_F(PointerHandSwitchTest, ATriggerPullOnThePointerHandItselfStillClicksWithBothHandsInUse) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());

    Pull(kRight);
    Frames(1);
    EXPECT_EQ(PointerHand(), "1");
    EXPECT_EQ(Executed(), "rightPoint");
}

TEST_F(PointerHandSwitchTest, OnlyThePointerHandCastsAPointerRay) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());

    // Both hands point at their own Click Point, but only the right hand's ray exists.
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "rightPoint");
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', VRCompanion:getPointerRay().ox, VRCompanion:getPointerRay().oy, VRCompanion:getPointerRay().oz)"),
              "-0.30 1.50 0.30") << "the ray starts at the right hand";

    Pull(kLeft);
    Frames(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "leftPoint");
    EXPECT_EQ(EvalLua(L, "string.format('%.2f %.2f %.2f', VRCompanion:getPointerRay().ox, VRCompanion:getPointerRay().oy, VRCompanion:getPointerRay().oz)"),
              "0.30 1.50 0.30") << "now it starts at the left hand";
}

TEST_F(PointerHandSwitchTest, ATriggerPullOnAHandThatIsNotInUseDoesNotSwitch) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    SetGameTime(1000);
    headset.SetHandPose(kLeft, HandAt(SideOf(kLeft), 0.0f, -0.3f));  // lying on the desk: tracked, placed long ago
    Frames(2);
    SetGameTime(10000);
    PickUp(kRight);
    Frames(2);
    ASSERT_EQ(PointerHand(), "1");

    Pull(kLeft);
    Frames(3);
    EXPECT_EQ(PointerHand(), "1");
    EXPECT_EQ(Executed(), "");
}

TEST_F(PointerHandSwitchTest, WhenThePointerHandStopsBeingInUseTheOtherHandBecomesThePointerHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());
    ASSERT_EQ(PointerHand(), "1");

    // The right hand is put down; the left keeps moving for longer than a hand stays in use.
    for (int step = 0; step < 8; ++step) {
        AdvanceGameTime(500);
        headset.SetHandPose(kLeft, HandAt(SideOf(kLeft) + (step % 2 == 0 ? 0.05f : 0.0f), 0.0f, -0.3f));
        Frames(1);
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "false");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0)"), "true");
    EXPECT_EQ(PointerHand(), "0");

    // And it is picked up again: the left hand stays the Pointer Hand until a pull on the right switches.
    headset.SetHandPose(kRight, HandAt(SideOf(kRight) + 0.1f, 0.0f, -0.3f));
    Frames(1);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandInUse(1)"), "true");
    EXPECT_EQ(PointerHand(), "0");
}

TEST_F(PointerHandSwitchTest, WithBothHandsInUseAndNoPointerHandYetAPullPicksThatHandAndIsConsumed) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    SetGameTime(10000);
    // Both Motion Controllers are picked up in the same frame: neither was the Pointer Hand first.
    headset.SetHandPose(kRight, HandAt(SideOf(kRight) - 0.1f, 0.0f, -0.3f));
    headset.SetHandPose(kLeft, HandAt(SideOf(kLeft) - 0.1f, 0.0f, -0.3f));
    Frames(1);
    headset.SetHandPose(kRight, HandAt(SideOf(kRight), 0.0f, -0.3f));
    headset.SetHandPose(kLeft, HandAt(SideOf(kLeft), 0.0f, -0.3f));
    Frames(2);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0) and VRCompanion:isHandInUse(1)"), "true");
    EXPECT_EQ(PointerHand(), "nil");
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerRay()"), "nil") << "no Pointer Hand, no ray";

    Pull(kRight);
    Frames(3);
    EXPECT_EQ(PointerHand(), "1");
    EXPECT_EQ(Executed(), "") << "the picking pull is consumed too";

    Release(kRight);
    // Aiming at the Click Point moves the hand, which gives the Pointer Ray control.
    headset.SetHandPose(kRight, HandAt(SideOf(kRight) + 0.05f, 0.0f, -0.3f));
    Frames(1);
    Pull(kRight);
    Frames(1);
    EXPECT_EQ(Executed(), "rightPoint");
}

TEST_F(PointerHandSwitchTest, TheStatusCommandReportsTheCurrentPointerHand) {
    ASSERT_NO_FATAL_FAILURE(StartMod());
    ASSERT_NO_FATAL_FAILURE(BothHandsInUseRightIsPointerHand());
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("Pointer Hand: right"), std::string::npos);

    Pull(kLeft);
    Frames(1);
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("Pointer Hand: left"), std::string::npos);
}

// ---- In a menu ----

namespace {

// The point on the menu panel a hand at the head's height aims at when its ray runs parallel to the
// panel's normal: u across (left to right), v down (top to bottom).
HeadPose AimAtPanel(float u, float v) {
    return HandAt((u - 0.5f) * 1.6f, 1.6f + (0.5f - v) * 0.9f, 0.0f);
}

class PointerHandSwitchMenuTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // A menu is showing and both hands are in use; the right hand is the Pointer Hand, aimed at the
    // middle of the panel.
    void StartWithMenuOpenAndBothHandsInUse() {
        headset.SetHeadPose(HandAt(0.0f, 1.6f, 0.0f));
        headset.manager->PollHeadPose();
        SetGameTime(10000);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kGuiStubs, "=guiStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        headset.SetHandPose(kRight, AimAtPanel(0.5f, 0.5f));
        NextFrame(&headset);
        headset.SetHandPose(kRight, AimAtPanel(0.55f, 0.5f));
        NextFrame(&headset);
        headset.SetHandPose(kLeft, AimAtPanel(0.25f, 0.5f));
        NextFrame(&headset);
        headset.SetHandPose(kLeft, AimAtPanel(0.3f, 0.5f));
        NextFrame(&headset);
        ASSERT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "1");
        ASSERT_EQ(EvalLua(L, "VRCompanion:isHandInUse(0)"), "true");
    }
    int Count(const char* expression) { return std::stoi(EvalLua(L, expression)); }
};

} // namespace

TEST_F(PointerHandSwitchMenuTest, ASwitchingPullDoesNotClickInAMenuAndTheNextPullOnTheNewPointerHandDoes) {
    StartWithMenuOpenAndBothHandsInUse();
    ASSERT_FALSE(HasFatalFailure());

    headset.SetHandInputs(kLeft, Trigger(1.0f));
    for (int i = 0; i < 6; ++i) NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerHand()"), "0");
    EXPECT_EQ(Count("guiDowns()"), 0) << "the switching pull, held on, is not a click";

    headset.SetHandInputs(kLeft, Trigger(0.0f));
    for (int i = 0; i < 2; ++i) NextFrame(&headset);
    EXPECT_EQ(Count("guiDowns()"), 0);

    headset.SetHandInputs(kLeft, Trigger(1.0f));
    NextFrame(&headset);
    EXPECT_EQ(Count("guiDowns()"), 1) << "the next pull on the new Pointer Hand clicks";
}
