// The desk mouse and the Pointer Ray hand over control (spec #71), seen from outside: the
// real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a stand-in for the
// Interactive Control mod (companion_mod_stubs.h) whose mouse position the test moves. The test asserts
// which Click Point Interactive Control treats as clickable or executed, whether the ray is drawn, and
// (for menus) what the game's GUI and the drawn cursor are told.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

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

// The engine globals the Companion Mod looks at when it draws.
constexpr const char* kStubs = R"luau(
    _G.debugLines, _G.debugPoints = {}, {}
    _G.drawDebugLine = function(x0, y0, z0, r0, g0, b0, x1, y1, z1, r1, g1, b1, solid)
        table.insert(_G.debugLines, { x0 = x0, y0 = y0, z0 = z0, x1 = x1, y1 = y1, z1 = z1 })
    end
    _G.drawDebugPoint = function(x, y, z, r, g, b, a, solid)
        table.insert(_G.debugPoints, { x = x, y = y, z = z })
    end
    -- How many lines and dots the Pointer Ray draws in one pass.
    _G.rayDrawn = function()
        _G.debugLines, _G.debugPoints = {}, {}
        VRCompanion:draw()
        return #_G.debugLines + #_G.debugPoints
    end
)luau";

// The game's GUI, which records what the menu pointer delivers (normalized, origin at the bottom left).
constexpr const char* kGuiStubs = R"luau(
    _G.addConsoleCommand = function() end
    _G.removeConsoleCommand = function() end
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1 }
    _G.guiEvents = {}
    _G.g_gui = {
        visible = false,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function(self, x, y, isDown, isUp, button)
            table.insert(_G.guiEvents, { x = x, y = y, down = isDown, up = isUp, button = button })
        end,
    }
    _G.guiDowns = function() local n = 0 for _, e in ipairs(_G.guiEvents) do if e.down then n = n + 1 end end return n end
)luau";

// A cab at the world origin and an Interactive Control release the Companion Mod supports. The player
// sits at (0, 1.5, 0) looking along world +Z, so a hand whose index fingertip is held at (x, y, -z) from the
// head and points straight ahead (SetHandPointing) has its ray start at world (-x, 1.5 + y, +z) and run along +Z.
void StartMod(lua_State* L, fs25vr::VRBridge& bridge, bool withInteractiveControl = true) {
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubSceneGraph, "=sceneGraph"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubInteractiveControl, "=interactiveControl"), 0);
    ASSERT_EQ(RunLuauChunk(L, kStubs, "=stubs"), 0);
    if (withInteractiveControl) {
        ASSERT_EQ(RunLuauChunk(L, "StubIC.install('ok')", "=installIc"), 0);
    }
    ASSERT_EQ(RunLuauChunk(L, "StubScene.cab({})", "=scene"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
}

// What the game does each frame before the Companion Mod's update: the camera update that places the
// head in the world, and Interactive Control's own update of its Click Points.
void PreFrame(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        if StubScene.updateCamera then StubScene.updateCamera() end
        if StubIC.tick then StubIC.tick() end
    )luau", "=preFrame"), 0);
}

// A fingertip held 0.3 m right of and 0.3 m ahead of the head, pointing straight ahead, makes a ray from
// world (-0.3, 1.5, 0.3) along +Z, and 'aimedAt' sits on it. 'underMouse' is far from the ray, at
// screen (0.4, 0.6) where the desk mouse can point at it.
void AddAimedAtAndUnderMouse(lua_State* L) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('aimedAt', -0.3, 1.5, 2.3, { screenX = 0.2, screenY = 0.2 })
        StubIC.addPoint('underMouse', 30, 1.5, 2.3, { screenX = 0.4, screenY = 0.6 })
    )luau", "=addPoints"), 0);
}

} // namespace

#define FRAMES(count) \
    for (int frame_ = 0; frame_ < (count); ++frame_) { PreFrame(L); NextFrame(&headset); }

// The Pointer Hand: picks the Motion Controller up (it moves) and aims it along the given pose.
#define PICK_UP_AND_AIM(hand, x, y, z) \
    do { \
        SetGameTime(10000); \
        headset.SetHandPointing((hand), HandAt((x) - 0.1f, (y), (z))); \
        FRAMES(1); \
        headset.SetHandPointing((hand), HandAt((x), (y), (z))); \
        FRAMES(1); \
    } while (0)

// The desk mouse moves to a screen position (Interactive Control reads it every frame).
#define MOVE_MOUSE(x, y) \
    ASSERT_EQ(RunLuauChunk(L, "StubIC.mouseX, StubIC.mouseY = " #x ", " #y, "=moveMouse"), 0)

#define ADD_AIMED_AT_AND_UNDER_MOUSE() ASSERT_NO_FATAL_FAILURE(AddAimedAtAndUnderMouse(L))

TEST_F(CompanionModTest, MovingTheDeskMouseWhileTheRayHasControlHandsClickPointSelectionToTheMouse) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ADD_AIMED_AT_AND_UNDER_MOUSE();
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "aimedAt") << "the Pointer Ray has control after the hand moved";

    MOVE_MOUSE(0.4, 0.6);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse") << "Interactive Control selects by its mouse again";
    EXPECT_EQ(EvalLua(L, "StubIC.blinks.underMouse ~= nil"), "true") << "with its own hover blink";

    // Interactive Control's own mouse test, not the ray: with the mouse off every Click Point, the aimed one is not selected.
    MOVE_MOUSE(0.9, 0.9);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "nil") << "the Click Point under the ray is not selected by the ray any more";
}

TEST_F(CompanionModTest, MovingThePointerHandAfterwardsHandsSelectionBackToTheRay) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ADD_AIMED_AT_AND_UNDER_MOUSE();
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    MOVE_MOUSE(0.4, 0.6);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse");

    // The mouse lying still does not keep the control from a hand that is lying still either.
    FRAMES(5);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse") << "a still hand does not take control";

    // The Pointer Hand moves (a little: it still points at the same Click Point).
    headset.SetHandPointing(kRight, HandAt(0.35f, 0.0f, -0.3f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "aimedAt") << "the Pointer Ray selects again, with no command";
}

TEST_F(CompanionModTest, WhileTheMouseHasControlThePointerRayIsHiddenAndTheTriggerClicksNothing) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ADD_AIMED_AT_AND_UNDER_MOUSE();
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "rayDrawn()"), "2") << "the ray is drawn while it has control";

    MOVE_MOUSE(0.4, 0.6);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "rayDrawn()"), "0") << "hidden while the mouse has control";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "the Pointer Hand's trigger clicks nothing, neither the aimed Click Point nor the one under the mouse";

    headset.SetHandPointing(kRight, HandAt(0.35f, 0.0f, -0.3f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "rayDrawn()"), "2") << "the ray is back when the hand moves";
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "StubIC.executed[1]"), "aimedAt") << "and clicks again";
}

TEST_F(CompanionModTest, ATriggerPulledWhileTheMouseHasControlDoesNotClickWhenTheHandMovesLaterNotEvenOnAnAnalogClickPoint) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('switch', -0.3, 1.5, 2.3)
        StubIC.addPoint('lever', -0.3, 1.5, 2.3, { analog = true, inRange = false })
    )luau", "=addPoints"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    MOVE_MOUSE(0.4, 0.6);
    FRAMES(2);

    // Pulled while the mouse has control, still held when the hand moves: not a click.
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    headset.SetHandPointing(kRight, HandAt(0.35f, 0.0f, -0.3f));
    FRAMES(4);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "switch") << "the ray has control and the switch under it is selected";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "a pull that began under the mouse never clicks retroactively";

    // The same pull let go and made again with the ray in control is a click.
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(1);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "1");
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);

    // An analog Click Point is not kept acting by a trigger that was pulled while the mouse had control.
    ASSERT_EQ(RunLuauChunk(L, "StubIC.points[1].inRange = false; StubIC.points[2].inRange = true; StubIC.executed = {}", "=useLever"), 0);
    FRAMES(2);
    MOVE_MOUSE(0.5, 0.7);  // the mouse moves again
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(2);
    headset.SetHandPointing(kRight, HandAt(0.30f, 0.0f, -0.3f));
    FRAMES(5);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "lever");
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0") << "holding the trigger on an analog Click Point acts only after a pull made with the ray in control";
    headset.SetHandInputs(kRight, Trigger(0.0f));
    FRAMES(2);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "3") << "a fresh pull acts as before";
}

TEST_F(CompanionModTest, WithNoMotionControllerInUseInteractiveControlSelectsByTheMouseAlone) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ADD_AIMED_AT_AND_UNDER_MOUSE();
    ASSERT_EQ(RunLuauChunk(L, "_G.originalUpdateClickable = StubIC.Point.updateClickable", "=remember"), 0);

    // Motion Controllers not tracked at all.
    SetGameTime(10000);
    headset.SetHandTracked(kRight, false);
    headset.SetHandTracked(0, false);
    FRAMES(3);
    MOVE_MOUSE(0.4, 0.6);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse");
    EXPECT_EQ(EvalLua(L, "StubIC.Point.updateClickable == originalUpdateClickable"), "true") << "Interactive Control has not been touched";
    EXPECT_EQ(EvalLua(L, "rayDrawn()"), "0");

    // A Motion Controller lying on the desk, aimed at a Click Point and never moved.
    headset.SetHandTracked(kRight, true);
    headset.SetHandPointing(kRight, HandAt(0.3f, 0.0f, -0.3f));
    FRAMES(3);
    headset.SetHandInputs(kRight, Trigger(1.0f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse") << "a still Motion Controller does not select";
    EXPECT_EQ(EvalLua(L, "#StubIC.executed"), "0");
    EXPECT_EQ(EvalLua(L, "rayDrawn()"), "0");
    headset.SetHandInputs(kRight, Trigger(0.0f));

    // Motion Controllers off: the hand moves, the mouse still selects.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setMotionControllersEnabled(false)", "=off"), 0);
    headset.SetHandPointing(kRight, HandAt(0.4f, 0.0f, -0.3f));
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse");
    EXPECT_EQ(EvalLua(L, "rayDrawn()"), "0");
}

TEST_F(CompanionModTest, ClickPointsAndMenusFollowTheSameControlOwner) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge));
    ASSERT_EQ(RunLuauChunk(L, kGuiStubs, "=guiStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        StubIC.addPoint('aimedAt', -0.3, 1.5, 2.3, { screenX = 0.2, screenY = 0.2 })
        StubIC.addPoint('underMouse', 30, 1.5, 2.3, { screenX = 0.4, screenY = 0.6 })
    )luau", "=addPoints"), 0);
    PICK_UP_AND_AIM(kRight, 0.3f, 0.0f, -0.3f);
    FRAMES(2);
    ASSERT_EQ(EvalLua(L, "StubIC.selected()"), "aimedAt");

    // The mouse takes control: Interactive Control selects by it ...
    MOVE_MOUSE(0.4, 0.6);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "mouse");
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "underMouse");
    // ... and a menu opening now is left to the mouse too.
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=openMenu"), 0);
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "#guiEvents"), "0") << "the menu pointer stays quiet while the mouse has control";
    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active);

    // The Pointer Hand moves: the menu cursor follows the ray ...
    headset.SetHandPointing(kRight, HandAt(0.35f, 0.0f, -0.3f));
    FRAMES(2);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "ray");
    EXPECT_NE(EvalLua(L, "#guiEvents"), "0");
    EXPECT_TRUE(headset.manager->GetPointerCursorOverride().active);
    // ... and once the menu is closed Interactive Control selects by the ray again.
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    FRAMES(3);
    EXPECT_EQ(EvalLua(L, "StubIC.selected()"), "aimedAt");
    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active);
}
