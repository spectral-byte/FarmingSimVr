// Scrolling and going back in menus with Motion Controllers (spec #71), seen from
// outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test
// pushes the fake Motion Controllers' thumbsticks, grips and buttons, drives frames and the game clock, and
// observes what the stub GUI receives. The stub GUI scrolls its list the way the game's list and
// slider elements do (they poll the mouse wheel state while handling a mouse event) and records the
// back/close input it is given.

#include "companion_mod_fixture.h"

namespace {

using fs25vr::FakeOpenXrControllers;
using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

// The game's GUI as the mod sees it. `list` covers the middle of the screen (GUI coordinates, y up
// from the bottom) and, like the game's SmoothListElement, scrolls when it handles a mouse-down
// inside it while the native wheel state says the wheel moved; `scrolls` records each step
// (-1 = list moved up, +1 = down). `backs` records the menu back input.
constexpr const char* kGuiStubs = R"luau(
    _G.addConsoleCommand = function() end
    _G.removeConsoleCommand = function() end
    _G.Input = {
        MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1, MOUSE_BUTTON_WHEEL_UP = 4, MOUSE_BUTTON_WHEEL_DOWN = 5,
        isMouseButtonPressed = function(button) return false end,
    }
    _G.InputAction = { MENU_BACK = "MENU_BACK" }
    _G.originalIsMouseButtonPressed = _G.Input.isMouseButtonPressed
    _G.list = { x0 = 0.2, y0 = 0.2, x1 = 0.8, y1 = 0.8 }
    _G.scrolls = {}
    _G.backs = {}
    _G.guiEvents = {}
    _G.g_gui = {
        visible = true,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function(self, x, y, isDown, isUp, button)
            if _G.guiFails then error("menu code failed") end
            table.insert(_G.guiEvents, { x = x, y = y, down = isDown, up = isUp, button = button })
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

// The point on the menu panel a hand at the head's height aims at when its ray runs parallel to the
// panel's normal: u across (left to right), v down (top to bottom).
HeadPose AimAt(float u, float v) {
    return PoseAt((u - 0.5f) * 1.6f, 1.6f + (0.5f - v) * 0.9f, 0.0f);
}

FakeOpenXrControllers::Inputs Grip(float value) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.grip = value;
    return inputs;
}

FakeOpenXrControllers::Inputs LowerButton() {
    FakeOpenXrControllers::Inputs inputs;
    inputs.buttonLower = true;
    return inputs;
}

FakeOpenXrControllers::Inputs Stick(float x, float y) {
    FakeOpenXrControllers::Inputs inputs;
    inputs.thumbX = x;
    inputs.thumbY = y;
    return inputs;
}

class MenuScrollBackTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // Starts the mod over the headset with a menu showing and the right hand pointing at the middle
    // of the panel, which is over the stub list.
    void StartWithMenuOpen() {
        headset.SetHeadPose(PoseAt(0.0f, 1.6f, 0.0f));
        headset.manager->PollHeadPose();
        SetGameTime(10000);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kGuiStubs, "=guiStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        headset.SetHandPointing(kRight, AimAt(0.5f, 0.5f));
        NextFrame(&headset);
        headset.SetHandPointing(kRight, AimAt(0.55f, 0.5f));  // a real move: the hand is in use and points
        NextFrame(&headset);
    }

    void CloseMenu() { ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0); }
    void OpenMenu() { ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = true", "=openMenu"), 0); }

    void Hold(uint32_t hand, const FakeOpenXrControllers::Inputs& inputs) {
        headset.SetHandInputs(hand, inputs);
        NextFrame(&headset);
    }
    void Release(uint32_t hand) { Hold(hand, FakeOpenXrControllers::Inputs{}); }

    int Backs() { return std::stoi(EvalLua(L, "#backs")); }
    int Scrolls() { return std::stoi(EvalLua(L, "#scrolls")); }
};

} // namespace

TEST_F(MenuScrollBackTest, GripGoesBackOncePerPressInAMenu) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(Backs(), 0);

    Hold(kRight, Grip(1.0f));
    EXPECT_EQ(Backs(), 1);
    EXPECT_EQ(EvalLua(L, "backs[1].action"), "MENU_BACK");
    EXPECT_EQ(EvalLua(L, "backs[1].value"), "1");

    for (int i = 0; i < 5; ++i) NextFrame(&headset);  // holding the grip is not more backs
    EXPECT_EQ(Backs(), 1);

    Release(kRight);
    Hold(kRight, Grip(1.0f));
    EXPECT_EQ(Backs(), 2) << "the next press is the next back";
}

TEST_F(MenuScrollBackTest, TheLowerFaceButtonGoesBackToo) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kRight, LowerButton());
    EXPECT_EQ(Backs(), 1);
    for (int i = 0; i < 3; ++i) NextFrame(&headset);
    EXPECT_EQ(Backs(), 1) << "once per press";
    Release(kRight);
    EXPECT_EQ(Backs(), 1) << "letting go is not a back";
}

TEST_F(MenuScrollBackTest, BackWorksOnEitherHandAndTwoHandsPressingTogetherAreOneBack) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kLeft, Grip(1.0f));
    EXPECT_EQ(Backs(), 1) << "the hand that is not the Pointer Hand";
    Release(kLeft);

    headset.SetHandInputs(kLeft, LowerButton());
    headset.SetHandInputs(kRight, Grip(1.0f));
    NextFrame(&headset);
    EXPECT_EQ(Backs(), 2) << "both hands in the same frame: one back";
}

TEST_F(MenuScrollBackTest, AWobblyGripIsOnePress) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kRight, Grip(0.3f));
    EXPECT_EQ(Backs(), 0) << "a light touch is not a press";
    Hold(kRight, Grip(1.0f));
    for (float value : { 0.8f, 0.5f, 0.7f, 0.5f, 0.8f }) Hold(kRight, Grip(value));
    EXPECT_EQ(Backs(), 1) << "wobbling between the thresholds is the same press";
    Release(kRight);
    Hold(kRight, Grip(1.0f));
    EXPECT_EQ(Backs(), 2);
}

TEST_F(MenuScrollBackTest, OutsideMenusGripAndTheLowerFaceButtonDoNotGoBack) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    CloseMenu();
    NextFrame(&headset);

    Hold(kRight, Grip(1.0f));
    Release(kRight);
    Hold(kLeft, LowerButton());
    Release(kLeft);
    EXPECT_EQ(Backs(), 0);
}

TEST_F(MenuScrollBackTest, AButtonAlreadyHeldWhenTheMenuOpensIsNotAPress) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    CloseMenu();
    Hold(kRight, Grip(1.0f));
    OpenMenu();
    for (int i = 0; i < 3; ++i) NextFrame(&headset);
    EXPECT_EQ(Backs(), 0) << "the grip was pressed before the menu opened";
    Release(kRight);
    Hold(kRight, Grip(1.0f));
    EXPECT_EQ(Backs(), 1);
}

TEST_F(MenuScrollBackTest, WithMotionControllersOffNothingGoesBack) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setMotionControllersEnabled(false)", "=off"), 0);
    Hold(kRight, Grip(1.0f));
    Release(kRight);
    Hold(kLeft, LowerButton());
    EXPECT_EQ(Backs(), 0);
}

TEST_F(MenuScrollBackTest, AMenuThatRaisesAnErrorNeverBreaksTheGameUpdate) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, "g_gui.onMenuInput = function() error('menu code failed') end", "=failingBack"), 0);
    Hold(kRight, Grip(1.0f));  // NextFrame fails the test if the update raises
    Release(kRight);
    Hold(kRight, Grip(1.0f));
    int reports = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find("menu pointer") != std::string::npos) ++reports;
    }
    EXPECT_EQ(reports, 1) << "reported once, not every press";
}

// ---- Scrolling ----

TEST_F(MenuScrollBackTest, ThumbstickUpAndDownScrollTheListUnderTheCursor) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(Scrolls(), 0);

    Hold(kRight, Stick(0.0f, 1.0f));
    ASSERT_EQ(Scrolls(), 1);
    EXPECT_EQ(EvalLua(L, "scrolls[1]"), "-1") << "thumbstick up scrolls the list up";
    Release(kRight);

    Hold(kRight, Stick(0.0f, -1.0f));
    ASSERT_EQ(Scrolls(), 2);
    EXPECT_EQ(EvalLua(L, "scrolls[2]"), "1") << "thumbstick down scrolls the list down";
}

TEST_F(MenuScrollBackTest, HoldingTheThumbstickKeepsScrollingAtAMeasuredPaceAndReleasingStops) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kRight, Stick(0.0f, -1.0f));
    ASSERT_EQ(Scrolls(), 1);
    NextFrame(&headset);
    NextFrame(&headset);
    EXPECT_EQ(Scrolls(), 1) << "not on every frame";

    AdvanceGameTime(200);
    NextFrame(&headset);
    EXPECT_EQ(Scrolls(), 2) << "one more step after the repeat interval";
    AdvanceGameTime(2000);
    NextFrame(&headset);
    EXPECT_EQ(Scrolls(), 3) << "a long gap is one step, not a catch-up burst";

    Release(kRight);
    AdvanceGameTime(2000);
    NextFrame(&headset);
    EXPECT_EQ(Scrolls(), 3);

    Hold(kRight, Stick(0.0f, -1.0f));
    EXPECT_EQ(Scrolls(), 4) << "a new push scrolls at once";
}

TEST_F(MenuScrollBackTest, ScrollingWorksFromEitherHand) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kLeft, Stick(0.0f, 1.0f));
    ASSERT_EQ(Scrolls(), 1);
    EXPECT_EQ(EvalLua(L, "scrolls[1]"), "-1") << "the left hand, though the right hand points";

    headset.SetHandInputs(kRight, Stick(0.0f, 1.0f));
    NextFrame(&headset);
    EXPECT_EQ(Scrolls(), 1) << "two hands pushing the same way are not twice the speed";
}

TEST_F(MenuScrollBackTest, ASmallOrSidewaysPushDoesNotScroll) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kRight, Stick(0.0f, 0.5f));
    EXPECT_EQ(Scrolls(), 0) << "below the engage deflection";
    Hold(kRight, Stick(0.9f, 0.8f));
    EXPECT_EQ(Scrolls(), 0) << "more sideways than up";
}

TEST_F(MenuScrollBackTest, TheCursorDecidesWhatScrolls) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    headset.SetHandPointing(kRight, AimAt(0.95f, 0.5f));  // over the panel, beside the list
    NextFrame(&headset);
    Hold(kRight, Stick(0.0f, 1.0f));
    EXPECT_EQ(Scrolls(), 0) << "the wheel turns where the cursor is, not over the list";
    EXPECT_EQ(RunLuauChunk(L, R"luau(
        local wheel
        for _, e in ipairs(guiEvents) do if e.button == Input.MOUSE_BUTTON_WHEEL_UP and e.down then wheel = e end end
        assert(wheel ~= nil, "the game was handed no wheel-up event")
        assert(math.abs(wheel.x - 0.95) < 1e-3 and math.abs(wheel.y - 0.5) < 1e-3, "at the cursor, not elsewhere")
    )luau", "=wheelEvent"), 0);
}

TEST_F(MenuScrollBackTest, TheGamesWheelStateIsOnlyWheelUpWhileTheEventIsHandled) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    Hold(kRight, Stick(0.0f, 1.0f));
    ASSERT_EQ(Scrolls(), 1);
    EXPECT_EQ(EvalLua(L, "Input.isMouseButtonPressed == originalIsMouseButtonPressed"), "true");
    EXPECT_EQ(EvalLua(L, "Input.isMouseButtonPressed(Input.MOUSE_BUTTON_WHEEL_UP)"), "false");

    ASSERT_EQ(RunLuauChunk(L, "_G.guiFails = true", "=failing"), 0);
    Release(kRight);
    Hold(kRight, Stick(0.0f, 1.0f));  // the GUI raises an error while handling the wheel
    EXPECT_EQ(EvalLua(L, "Input.isMouseButtonPressed == originalIsMouseButtonPressed"), "true")
        << "a failing menu must not leave the wheel state patched";
}

TEST_F(MenuScrollBackTest, ScrollingOnlyHappensWhileAMenuIsShowingAndMotionControllersAreOn) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    CloseMenu();
    NextFrame(&headset);
    Hold(kRight, Stick(0.0f, 1.0f));
    Release(kRight);
    EXPECT_EQ(Scrolls(), 0) << "no menu";

    OpenMenu();
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setMotionControllersEnabled(false)", "=off"), 0);
    Hold(kRight, Stick(0.0f, 1.0f));
    EXPECT_EQ(Scrolls(), 0) << "Motion Controllers off";
}

TEST_F(MenuScrollBackTest, AStickAlreadyPushedWhenTheMenuOpensDoesNotScrollUntilPushedAgain) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    CloseMenu();
    Hold(kRight, Stick(0.0f, 1.0f));
    OpenMenu();
    AdvanceGameTime(500);
    NextFrame(&headset);
    AdvanceGameTime(500);
    NextFrame(&headset);
    EXPECT_EQ(Scrolls(), 0);
    Release(kRight);
    Hold(kRight, Stick(0.0f, 1.0f));
    EXPECT_EQ(Scrolls(), 1);
}

TEST_F(MenuScrollBackTest, WhenTheDeskMouseHasControlTheScrollLandsWhereTheMouseIs) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_inputBinding = { x = 0.5, y = 0.5, getMousePosition = function(self) return self.x, self.y end }
    )luau", "=inputBinding"), 0);
    NextFrame(&headset);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isPointerRayInControl()"), "true");
    ASSERT_EQ(RunLuauChunk(L, "g_inputBinding.x = 0.9; VRCompanion:mouseEvent(0.9, 0.5, false, false, 0)", "=deskMouse"), 0);  // beside the list
    NextFrame(&headset);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isPointerRayInControl()"), "false");

    Hold(kRight, Stick(0.0f, 1.0f));
    EXPECT_EQ(Scrolls(), 0) << "the mouse is beside the list; the ray's last position is over it";
    Release(kRight);
    ASSERT_EQ(RunLuauChunk(L, "g_inputBinding.x = 0.4", "=mouseOverList"), 0);
    Hold(kRight, Stick(0.0f, 1.0f));
    EXPECT_EQ(Scrolls(), 1);
}

TEST_F(MenuScrollBackTest, AGameThatDoesNotLetTheWheelStateBeSetNeverBreaksTheUpdateAndIsReportedOnce) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local locked = setmetatable({}, { __index = Input, __newindex = function() error("Input is read-only") end })
        _G.Input = locked
    )luau", "=lockedInput"), 0);
    Hold(kRight, Stick(0.0f, 1.0f));  // NextFrame fails the test if the update raises
    Release(kRight);
    Hold(kRight, Stick(0.0f, 1.0f));
    int reports = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find("menu pointer") != std::string::npos) ++reports;
    }
    EXPECT_EQ(reports, 1);
    EXPECT_EQ(Scrolls(), 0) << "without the wheel state the game's list cannot know";
    EXPECT_GE(std::stoi(EvalLua(L, "#guiEvents")), 2) << "the wheel events were still delivered";
}

// ---- Snap Turn and the lower face button stay out of the way in menus ----

namespace {

// The player sits in a vehicle cab: Snap Turn steps cam.rotY. recenterCalls counts the Recenter
// Actions that reach the Injector bridge.
constexpr const char* kCabAndRecenterCounter = R"luau(
    _G.cam = { isInside = true, cameraNode = 101, origRotY = 0.0, rotY = 0.0 }
    _G.g_currentMission = { controlledVehicle = { getActiveCamera = function() return _G.cam end } }
    _G.recenterCalls = 0
    local realRecenter = VRBridge.recenter
    VRBridge.recenter = function(...)
        _G.recenterCalls = _G.recenterCalls + 1
        return realRecenter(...)
    end
)luau";

} // namespace

TEST_F(MenuScrollBackTest, SnapTurnStaysOffWhileAMenuIsShowingEvenAsTheStickScrolls) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, kCabAndRecenterCounter, "=cab"), 0);

    Hold(kRight, Stick(1.0f, 0.0f));
    Release(kRight);
    AdvanceGameTime(300);
    Hold(kRight, Stick(0.0f, 1.0f));
    Release(kRight);
    EXPECT_NEAR(std::stod(EvalLua(L, "_G.cam.rotY")), 0.0, 1e-6) << "no Snap Turn with a menu showing";
    EXPECT_EQ(Scrolls(), 1) << "the vertical push scrolled";

    CloseMenu();
    AdvanceGameTime(300);
    Hold(kRight, Stick(1.0f, 0.0f));
    EXPECT_NEAR(std::stod(EvalLua(L, "_G.cam.rotY")), -3.14159265 / 2.0, 1e-4) << "Snap Turn is back with the menu closed";
}

TEST_F(MenuScrollBackTest, AWorldActionOnTheLowerFaceButtonYieldsToBackWhileAMenuIsShowing) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, kCabAndRecenterCounter, "=cab"), 0);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRCompanion:setMotionControllerMapping('recenter', 'buttonLower'))", "=mapRecenter"), 0);
    NextFrame(&headset);

    Hold(kRight, LowerButton());
    Release(kRight);
    EXPECT_EQ(Backs(), 1) << "in a menu the button goes back";
    EXPECT_EQ(EvalLua(L, "_G.recenterCalls"), "0") << "and does not also recenter";

    CloseMenu();
    NextFrame(&headset);
    Hold(kRight, LowerButton());
    Release(kRight);
    EXPECT_EQ(Backs(), 1) << "outside menus it does not go back";
    EXPECT_EQ(EvalLua(L, "_G.recenterCalls"), "1") << "it does what the player mapped it to";
}
