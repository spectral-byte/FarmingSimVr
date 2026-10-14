// The Virtual Gamepad while a menu shows (ADR-0028, spec #98, ticket #101), seen from outside: the real
// Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test drives the fake Motion
// Controllers and the game's menu, and reads what the game would read from XInput (the answers the hooks route
// the game's calls to) next to what the stub GUI receives. While a menu shows, the Companion Mod is using every
// input on both hands (the Pointer Ray menus: cursor, trigger click, grip or lower face button for back, stick
// scroll), so none of them reaches the pad, except an input already held when the menu came up, which keeps its
// binding until it is let go.

#include "companion_mod_fixture.h"
#include "input/virtual_gamepad.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

// The engine's console, and the game's GUI as the mod sees it: it records the mouse events (cursor moves, clicks, wheel
// steps) and the menu back input it is given. `list` covers the middle of the screen and scrolls the way the game's list
// elements do (they poll the mouse wheel state while handling a mouse-down inside them).
constexpr const char* kGuiStubs = R"luau(
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
    _G.Input = {
        MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1, MOUSE_BUTTON_WHEEL_UP = 4, MOUSE_BUTTON_WHEEL_DOWN = 5,
        isMouseButtonPressed = function(button) return false end,
    }
    _G.InputAction = { MENU_BACK = "MENU_BACK" }
    _G.list = { x0 = 0.2, y0 = 0.2, x1 = 0.8, y1 = 0.8 }
    _G.scrolls, _G.backs, _G.clicks = {}, {}, 0
    _G.g_gui = {
        visible = false,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function(self, x, y, isDown, isUp, button)
            if button == Input.MOUSE_BUTTON_LEFT and isDown then _G.clicks = _G.clicks + 1 end
            local over = x >= list.x0 and x <= list.x1 and y >= list.y0 and y <= list.y1
            if over and isDown then
                if Input.isMouseButtonPressed(Input.MOUSE_BUTTON_WHEEL_UP) then
                    table.insert(_G.scrolls, -1)
                elseif Input.isMouseButtonPressed(Input.MOUSE_BUTTON_WHEEL_DOWN) then
                    table.insert(_G.scrolls, 1)
                end
            end
        end,
        onMenuInput = function(self, action, value)
            table.insert(_G.backs, { action = action, value = value })
        end,
    }
)luau";

HeadPose PoseAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// The point on the menu panel a hand at the head's height aims at when its ray runs parallel to the panel's normal.
HeadPose AimAt(float u, float v) {
    return PoseAt((u - 0.5f) * 1.6f, 1.6f + (0.5f - v) * 0.9f, 0.0f);
}

FakeOpenXrControllers::Inputs Trigger(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

FakeOpenXrControllers::Inputs Stick(float x, float y) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.thumbX = x;
    inputs.thumbY = y;
    return inputs;
}

// Every control of a hand at once. (The right stick click is also the D-pad chord and reports RS on release, so a test that wants a quiet
// pad after letting go leaves it out.)
FakeOpenXrControllers::Inputs Everything(bool stickClick = true) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = 1.0f;
    inputs.grip = 1.0f;
    inputs.thumbX = 1.0f;
    inputs.thumbY = 1.0f;
    inputs.thumbClick = stickClick;
    inputs.buttonLower = true;
    inputs.buttonUpper = true;
    inputs.menu = true;
    return inputs;
}

class VirtualGamepadMenuUseTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // The mod started over the headset with the Virtual Gamepad on, no menu showing, and the right hand pointing at
    // the middle of the panel, which is over the stub list.
    void StartWithPadOn() {
        headset.SetHeadPose(PoseAt(0.0f, 1.6f, 0.0f));
        headset.manager->PollHeadPose();
        SetGameTime(10000);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kGuiStubs, "=guiStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0);
        headset.SetHandPointing(kRight, AimAt(0.5f, 0.5f));
        NextFrame(&headset);
        headset.SetHandPointing(kRight, AimAt(0.55f, 0.5f));  // a real move: the hand is in use and points
        NextFrame(&headset);
    }

    void OpenMenu() { ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=openMenu"), 0); NextFrame(&headset); }
    void CloseMenu() { ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0); NextFrame(&headset); }

    void Hold(uint32_t hand, const FakeOpenXrControllers::Inputs& inputs) {
        headset.SetHandInputs(hand, inputs);
        NextFrame(&headset);
    }
    void Release(uint32_t hand) { Hold(hand, FakeOpenXrControllers::Inputs{}); }

    // What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
    XINPUT_GAMEPAD Pad() {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    }

    static bool IsNeutral(const XINPUT_GAMEPAD& pad) {
        return pad.wButtons == 0 && pad.bLeftTrigger == 0 && pad.bRightTrigger == 0 && pad.sThumbLX == 0 &&
               pad.sThumbLY == 0 && pad.sThumbRX == 0 && pad.sThumbRY == 0;
    }

    int Clicks() { return std::stoi(EvalLua(L, "_G.clicks")); }
    int Backs() { return std::stoi(EvalLua(L, "#backs")); }
    int Scrolls() { return std::stoi(EvalLua(L, "#scrolls")); }
};

} // namespace

TEST_F(VirtualGamepadMenuUseTest, WhileAMenuShowsTheWholePadReadsNeutralOnBothHands) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());

    // No menu: everything reaches the pad.
    headset.SetHandInputs(kLeft, Everything());
    headset.SetHandInputs(kRight, Everything(false));
    NextFrame(&headset);
    ASSERT_FALSE(IsNeutral(Pad())) << "outside menus the pad carries the Motion Controllers";
    Release(kLeft);
    Release(kRight);
    ASSERT_TRUE(IsNeutral(Pad()));

    OpenMenu();
    headset.SetHandInputs(kLeft, Everything());
    headset.SetHandInputs(kRight, Everything());
    NextFrame(&headset);
    const XINPUT_GAMEPAD pad = Pad();
    EXPECT_EQ(pad.wButtons, 0) << "no button of either hand";
    EXPECT_EQ(pad.bLeftTrigger, 0);
    EXPECT_EQ(pad.bRightTrigger, 0);
    EXPECT_EQ(pad.sThumbLX, 0);
    EXPECT_EQ(pad.sThumbLY, 0);
    EXPECT_EQ(pad.sThumbRX, 0);
    EXPECT_EQ(pad.sThumbRY, 0);
}

TEST_F(VirtualGamepadMenuUseTest, TheMenusOwnTriggerClickBackAndScrollStillWorkAndNoneOfThemReachesThePad) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    OpenMenu();
    ASSERT_EQ(Clicks(), 0);

    Hold(kRight, Trigger(1.0f));
    EXPECT_EQ(Clicks(), 1) << "the Pointer Hand's trigger clicks at the cursor";
    EXPECT_TRUE(IsNeutral(Pad())) << "and fires no binding";
    Release(kRight);

    Hold(kRight, Grip(1.0f));
    EXPECT_EQ(Backs(), 1) << "the grip goes back";
    EXPECT_TRUE(IsNeutral(Pad()));
    Release(kRight);

    FakeOpenXrControllers::Inputs lower;
    lower.buttonLower = true;
    Hold(kLeft, lower);
    EXPECT_EQ(Backs(), 2) << "so does the lower face button, on either hand";
    EXPECT_TRUE(IsNeutral(Pad()));
    Release(kLeft);

    Hold(kRight, Stick(0.0f, 1.0f));
    EXPECT_EQ(Scrolls(), 1) << "the thumbstick scrolls the list under the cursor";
    EXPECT_TRUE(IsNeutral(Pad())) << "and walks nobody anywhere";
    Release(kRight);
    EXPECT_TRUE(IsNeutral(Pad()));
}

TEST_F(VirtualGamepadMenuUseTest, AnInputHeldWhenTheMenuOpensKeepsItsBindingUntilItIsLetGoWhileTheRestReadsNeutral) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());

    // The menu button opens the pause menu, so it is down when the menu comes up; so is a trigger held for throttle.
    FakeOpenXrControllers::Inputs left;
    left.menu = true;
    left.trigger = 1.0f;
    Hold(kLeft, left);
    OpenMenu();
    XINPUT_GAMEPAD pad = Pad();
    EXPECT_NE(pad.wButtons & XINPUT_GAMEPAD_START, 0) << "Start was held before the menu: it keeps its binding";
    EXPECT_EQ(pad.bLeftTrigger, 255) << "so does the trigger";

    // Something else pressed in the menu is the menu's.
    Hold(kRight, Everything());
    pad = Pad();
    EXPECT_EQ(pad.wButtons & ~XINPUT_GAMEPAD_START, 0);
    EXPECT_EQ(pad.bRightTrigger, 0);
    EXPECT_EQ(pad.sThumbRX, 0);

    // Letting go of the trigger hands it to the menu: pulled again, it is the menu's click and not a binding.
    left.trigger = 0.0f;
    Hold(kLeft, left);
    EXPECT_EQ(Pad().bLeftTrigger, 0);
    left.trigger = 1.0f;
    Hold(kLeft, left);
    EXPECT_EQ(Pad().bLeftTrigger, 0) << "a fresh pull in the menu";
    EXPECT_NE(Pad().wButtons & XINPUT_GAMEPAD_START, 0) << "Start is still held, still its binding's";

    Release(kLeft);
    left.menu = true;
    Hold(kLeft, left);
    EXPECT_EQ(Pad().wButtons & XINPUT_GAMEPAD_START, 0) << "pressed again in the menu, it is the menu's";
}

TEST_F(VirtualGamepadMenuUseTest, AnInputThePlayerPressedInTheMenuStaysOffThePadUntilLetGoEvenAfterTheMenuCloses) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    OpenMenu();

    Hold(kRight, Grip(1.0f));  // goes back; the game closes the menu
    CloseMenu();
    EXPECT_EQ(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0) << "the grip that went back does not fire its binding as the menu goes";

    Release(kRight);
    Hold(kRight, Grip(1.0f));
    EXPECT_NE(Pad().wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0) << "pressed afresh outside the menu it is the pad's again";
}

TEST_F(VirtualGamepadMenuUseTest, TheStatusCommandListsTheInputsTheCompanionModIsUsing) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("Virtual Gamepad: present in XInput slot 3; the Companion Mod is using: none"),
              std::string::npos) << EvalLua(L, "runCommand('vrMotionControllerStatus')");

    OpenMenu();
    const std::string status = EvalLua(L, "runCommand('vrMotionControllerStatus')");
    EXPECT_NE(status.find("the Companion Mod is using: left trigger, left grip, left thumbstick, left thumbstick click, left lower button, "
                          "left upper button, left menu button, right trigger, right grip, right thumbstick, right thumbstick click, "
                          "right lower button, right upper button"),
              std::string::npos) << status;

    CloseMenu();
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("the Companion Mod is using: none"), std::string::npos);
}

TEST_F(VirtualGamepadMenuUseTest, WithMotionControllersOffTheMenuUsesNothingAndTheCompanionModIsUsingNothing) {
    StartWithPadOn();
    ASSERT_FALSE(HasFatalFailure());
    EvalLua(L, "runCommand('vrMotionControllers', 'off')");
    OpenMenu();
    EXPECT_NE(EvalLua(L, "runCommand('vrMotionControllerStatus')").find("the Companion Mod is using: none"), std::string::npos);
}
// The Injector's side of the same thing, without the Companion Mod: whatever the Companion Mod says it is using reads
// as released or centred, per hand, and only that.
TEST_F(CompanionModTest, TheInputsTheCompanionModSaysItIsUsingReadReleasedOnThePadPerHandAndOnlyThose) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setVirtualGamepad(true, true)", "=padOn"), 0);
    headset.SetHandInputs(kLeft, Everything());
    headset.SetHandInputs(kRight, Everything(false));
    headset.NextFrame();
    const auto read = [&] {
        XINPUT_STATE state{};
        EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
        return state.Gamepad;
    };
    const WORD all = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB |
                     XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X |
                     XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_START;
    ASSERT_EQ(read().wButtons, all);

    // Bits: 1 trigger, 2 grip, 4 thumbstick, 8 thumbstick click, 16 lower, 32 upper, 64 menu.
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setVirtualGamepadUse(1 + 2 + 8, 4)", "=use"), 0);
    XINPUT_GAMEPAD pad = read();
    EXPECT_EQ(pad.bLeftTrigger, 0);
    EXPECT_EQ(pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER, 0);
    EXPECT_EQ(pad.bRightTrigger, 255) << "the right hand's trigger is not in use";
    EXPECT_NE(pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER, 0);
    EXPECT_EQ(pad.sThumbRX, 0);
    EXPECT_EQ(pad.sThumbRY, 0);
    EXPECT_EQ(pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB, 0) << "the left thumbstick click is in use";
    EXPECT_EQ(pad.sThumbLX, 32767) << "the left thumbstick is not";
    EXPECT_NE(pad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_START), 0);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setVirtualGamepadUse(16 + 32 + 64, 16 + 32 + 64)", "=buttons"), 0);
    pad = read();
    EXPECT_EQ(pad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_START), 0);
    EXPECT_NE(pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER, 0);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setVirtualGamepadUse()", "=none"), 0);
    EXPECT_EQ(read().wButtons, all) << "a call with no masks puts everything back";
}