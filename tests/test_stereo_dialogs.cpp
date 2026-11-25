#include "wheel_grab_test_support.h"
#include "input/virtual_gamepad.h"
#include <string>

namespace {

using namespace wheel_grab_test;

constexpr const char* kGuiStubs = R"luau(
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1, isMouseButtonPressed = function() return false end }
    _G.InputAction = { MENU_BACK = "MENU_BACK" }
    _G.backs, _G.clicks = {}, 0
    _G.g_gui = {
        currentGuiName = "",
        currentDialogName = "",
        dialogs = {},
        getIsGuiVisible = function(self) return self.currentGuiName ~= "" or #self.dialogs > 0 end,
        mouseEvent = function(self, x, y, isDown, isUp, button)
            if button == Input.MOUSE_BUTTON_LEFT and isDown then _G.clicks = _G.clicks + 1 end
        end,
        onMenuInput = function(self, action, value) table.insert(_G.backs, { action = action, value = value }) end,
    }
    -- A dialog opened on top of whatever is showing, as Gui:showDialog does.
    _G.showDialog = function(name)
        local target = {
            yesButton = { inputActionName = "MENU_ACCEPT" },
            noButton = { inputActionName = "MENU_BACK" },
            onYes = function() end, onNo = function() end,
        }
        local root = { name = name, target = target }
        target.dialogElement = root
        table.insert(g_gui.dialogs, root)
        g_gui.currentDialogName = name
        return root
    end
    -- The newest dialog closed.
    _G.closeDialog = function()
        table.remove(g_gui.dialogs)
        g_gui.currentDialogName = #g_gui.dialogs > 0 and g_gui.dialogs[#g_gui.dialogs].name or ""
    end
    -- A screen (the pause menu) shown or closed.
    _G.showScreen = function(name) g_gui.currentGuiName = name end
    _G.closeScreen = function() g_gui.currentGuiName = "" end

    -- What VRBridge was told, counted without changing what it does.
    _G.menuVisibleCalls = {}
    local setMenuVisible = VRBridge.setMenuVisible
    VRBridge.setMenuVisible = function(visible)
        table.insert(_G.menuVisibleCalls, visible)
        return setMenuVisible(visible)
    end
)luau";

#define START_STEREO_DIALOG_SCENE() \
    do { \
        headset.SetHeadPose(HandAt(0.0f, 1.6f, 0.0f)); \
        headset.manager->PollHeadPose(); \
        SetGameTime(10000); \
        StartOptions options_; \
        options_.modelLoading = true; \
        options_.noPullAway = true; \
        options_.extraStubs = kGuiStubs; \
        ASSERT_NO_FATAL_FAILURE(StartWheelGrabMod(L, bridge, SteerableCab(), options_)); \
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrVirtualGamepad', 'on')", "=padOn"), 0); \
        BRING_HAND(kRight, (Point{ 0.4, 1.2, 0.4 })); \
        BRING_HAND(kLeft, (Point{ -0.4, 1.2, 0.4 })); \
        FRAMES(3); \
        bridge.ConsumeDiscontinuity(0); \
        bridge.ConsumeDiscontinuity(1); \
        ASSERT_EQ(RunLuauChunk(L, "_G.menuVisibleCalls = {}", "=forgetSetupCalls"), 0); \
    } while (0)

bool HasPendingDiscontinuity(fs25vr::VRBridge& bridge) {
    return bridge.HasPendingDiscontinuity(0) || bridge.HasPendingDiscontinuity(1);
}

// "turned" when a Snap Turn right at the given game time was made, "refused:menu_visible" when it was refused and why.
std::string SnapTurnRight(lua_State* L, double atMs) {
    return EvalLua(L, "(function() local ok, why = VRCompanion:snapTurnRight(nil, " + std::to_string(atMs) +
                          "); return ok and 'turned' or ('refused:' .. tostring(why)) end)()");
}

bool HandIsShown(lua_State* L, const char* which) {
    return EvalLua(L, std::string("StubModel.hands().") + which + " ~= nil and StubModel.isShown(StubModel.hands()." + which + ")") == "true";
}

int SteeringCalls(lua_State* L) { return static_cast<int>(Number(L, "#StubSteer.calls")); }

FakeOpenXrControllers::Inputs Everything() {
    FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = 1.0f;
    inputs.grip = 1.0f;
    inputs.thumbX = 1.0f;
    inputs.thumbY = 1.0f;
    inputs.buttonLower = true;
    inputs.buttonUpper = true;
    inputs.menu = true;
    return inputs;
}

// What the game reads from XInput slot 3, the highest free slot of a machine without real pads.
XINPUT_GAMEPAD Pad(fs25vr::VRBridge& bridge) {
    XINPUT_STATE state{};
    EXPECT_EQ(bridge.GetVirtualGamepad().GetState(3, &state), static_cast<DWORD>(ERROR_SUCCESS));
    return state.Gamepad;
}

bool IsNeutral(const XINPUT_GAMEPAD& pad) {
    return pad.wButtons == 0 && pad.bLeftTrigger == 0 && pad.bRightTrigger == 0 && pad.sThumbLX == 0 && pad.sThumbLY == 0 &&
           pad.sThumbRX == 0 && pad.sThumbRY == 0;
}

int Backs(lua_State* L) { return std::stoi(EvalLua(L, "#backs")); }

}

TEST_F(CompanionModTest, TheFishDialogOpeningAndClosingNeverTellsVRBridgeAMenuIsVisibleNorFlushesTheUpscaler) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_FALSE(bridge.IsMenuVisible()) << "the Injector keeps the projection layer while the fish dialog shows";
    EXPECT_FALSE(HasPendingDiscontinuity(bridge)) << "the presentation did not change: no Discontinuity Flush on open";

    ASSERT_EQ(RunLuauChunk(L, "closeDialog()", "=closeFishDialog"), 0);
    FRAMES(3);
    EXPECT_FALSE(bridge.IsMenuVisible());
    EXPECT_FALSE(HasPendingDiscontinuity(bridge)) << "and none on close: stereo carries on with no transition";
    EXPECT_EQ(EvalLua(L, "#menuVisibleCalls"), "0") << "VRBridge was never told anything about a menu";
}

TEST_F(CompanionModTest, AnythingTheGameShowsThatIsNotALoneFishDialogStillGetsTheMenuQuad) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    struct Case { const char* name; const char* open; const char* close; };
    const Case cases[] = {
        { "a dialog the mod does not know", "showDialog('YesNoDialog')", "closeDialog()" },
        { "a dialog with no name", "showDialog('')", "closeDialog()" },
        { "the pause menu", "showScreen('InGameMenu')", "closeScreen()" },
        { "the fish dialog opened over the pause menu", "showScreen('InGameMenu'); showDialog('FishDialog')", "closeDialog(); closeScreen()" },
        { "a dialog the mod does not know over the fish dialog", "showDialog('FishDialog'); showDialog('YesNoDialog')", "closeDialog(); closeDialog()" },
        { "the fish dialog over a dialog the mod does not know", "showDialog('YesNoDialog'); showDialog('FishDialog')", "closeDialog(); closeDialog()" },
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_EQ(RunLuauChunk(L, c.open, "=open"), 0);
        FRAMES(3);
        EXPECT_TRUE(bridge.IsMenuVisible()) << "the Injector presents the menu quad";
        EXPECT_TRUE(HasPendingDiscontinuity(bridge)) << "and the switch is a Discontinuity Flush";
        EXPECT_FALSE(HandIsShown(L, "right")) << "with no Virtual Hands, as in any menu";
        ASSERT_EQ(RunLuauChunk(L, c.close, "=close"), 0);
        FRAMES(3);
        EXPECT_FALSE(bridge.IsMenuVisible()) << "closing it restores the stereoscopic view";
        bridge.ConsumeDiscontinuity(0);
        bridge.ConsumeDiscontinuity(1);
    }
}

TEST_F(CompanionModTest, TheFishDialogInTwoDFloatingWindowModeGetsTheMenuQuadLikeAnyMenu) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(true)", "=enter2D"), 0);
    FRAMES(3);
    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_TRUE(bridge.IsMenuVisible()) << "in 2D Floating Window Mode the presentation is the quad, and the dialog keeps it";

    ASSERT_EQ(RunLuauChunk(L, "closeDialog()", "=closeFishDialog"), 0);
    FRAMES(3);
    EXPECT_FALSE(bridge.IsMenuVisible());
    EXPECT_EQ(EvalLua(L, "VRCompanion:getIs2DViewActive()"), "true") << "and 2D Floating Window Mode carries on";
}

TEST_F(CompanionModTest, WhileTheFishDialogShowsTheVirtualHandsStayOnTheMotionControllersAndFollowThem) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(HandIsShown(L, "left") && HandIsShown(L, "right")) << "both hands are shown before the dialog";

    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_TRUE(HandIsShown(L, "left"));
    EXPECT_TRUE(HandIsShown(L, "right"));

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.before = { StubScene.worldPosition(StubModel.hands().right) }
    )luau", "=handBefore"), 0);
    headset.SetHandPose(kRight, HandAtWorld(Point{ 0.4, 1.2, 0.4 + 0.2 }));
    FRAMES(2);
    EXPECT_NEAR(Number(L, R"luau((function()
        local x, y, z = StubScene.worldPosition(StubModel.hands().right)
        return math.sqrt((x - before[1]) ^ 2 + (y - before[2]) ^ 2 + (z - before[3]) ^ 2)
    end)())luau"), 0.2, 0.02) << "the Virtual Hand follows its Motion Controller";
    EXPECT_TRUE(HandIsShown(L, "right"));

    ASSERT_EQ(RunLuauChunk(L, "closeDialog()", "=closeFishDialog"), 0);
    FRAMES(3);
    EXPECT_TRUE(HandIsShown(L, "left") && HandIsShown(L, "right")) << "and they are still there when it closes";
}

TEST_F(CompanionModTest, WhileTheFishDialogShowsSnapTurnAndWheelGrabAreRefusedAndWorkAgainOnceItCloses) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_EQ(SnapTurnRight(L, 30000), "refused:menu_visible") << "Snap Turn is refused: the player cannot turn away while it waits";
    const int callsBefore = SteeringCalls(L);
    TRY_TO_STEER(kRight);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isHandHoldingSteeringWheel(1)"), "false") << "Wheel Grab does not take hold";
    EXPECT_EQ(SteeringCalls(L), callsBefore) << "and no steering reached the vehicle";
    EXPECT_NEAR(WheelAngle(L), 0.0, 0.01);

    ASSERT_EQ(RunLuauChunk(L, "closeDialog()", "=closeFishDialog"), 0);
    FRAMES(3);
    TRY_TO_STEER(kRight);
    EXPECT_GT(SteeringCalls(L), callsBefore) << "Wheel Grab works again";
    EXPECT_EQ(SnapTurnRight(L, 40000), "turned") << "and so does Snap Turn";
}

TEST_F(CompanionModTest, WhileTheFishDialogShowsTheVirtualGamepadSendsTheMotionControllersToTheDialogAndTheModsOwnMenuBackStaysOut) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    headset.SetHandInputs(kLeft, Everything());
    headset.SetHandInputs(kRight, Everything());
    FRAMES(2);
    const XINPUT_GAMEPAD pad = Pad(bridge);
    EXPECT_NE(pad.wButtons & XINPUT_GAMEPAD_A, 0) << "the lower face button is the pad's A, which the dialog reads as MENU_ACCEPT";
    EXPECT_NE(pad.wButtons & XINPUT_GAMEPAD_B, 0) << "the upper face button is the pad's B, which the dialog reads as MENU_BACK";
    EXPECT_FALSE(IsNeutral(pad)) << "the pad carries the Motion Controllers";
    EXPECT_EQ(Backs(L), 0) << "and the mod's own back (the grip, the lower face button) does not answer the dialog a second time";
    EXPECT_EQ(EvalLua(L, "_G.clicks"), "0");
}

TEST_F(CompanionModTest, AButtonHeldWhenTheFishDialogOpensDoesNotAnswerItButAFreshPressDoes) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    FakeOpenXrControllers::Inputs held;
    held.buttonLower = true;
    headset.SetHandInputs(kRight, held);
    FRAMES(2);
    EXPECT_NE(Pad(bridge).wButtons & XINPUT_GAMEPAD_A, 0) << "outside the dialog it is the pad's A as always";

    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_EQ(Pad(bridge).wButtons & XINPUT_GAMEPAD_A, 0) << "held across the open, it is not the dialog's";

    FakeOpenXrControllers::Inputs both = held;
    both.buttonUpper = true;
    headset.SetHandInputs(kRight, both);
    FRAMES(2);
    EXPECT_NE(Pad(bridge).wButtons & XINPUT_GAMEPAD_B, 0) << "a fresh press reaches the dialog";
    EXPECT_EQ(Pad(bridge).wButtons & XINPUT_GAMEPAD_A, 0) << "while the held one still is not";

    headset.SetHandInputs(kRight, FakeOpenXrControllers::Inputs{});
    FRAMES(2);
    EXPECT_TRUE(IsNeutral(Pad(bridge))) << "letting go sends nothing to the dialog";

    headset.SetHandInputs(kRight, held);
    FRAMES(2);
    EXPECT_NE(Pad(bridge).wButtons & XINPUT_GAMEPAD_A, 0) << "pressed again in the dialog, it is the dialog's";
}
TEST_F(CompanionModTest, TheLogSaysTheFishDialogKeptTheStereoscopicViewAndOnlyAMenuThatGetsTheQuadSwitchedToIt) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    auto logged = [](const char* text) {
        for (const auto& line : s_capturedLogs) {
            if (line.find(text) != std::string::npos) return true;
        }
        return false;
    };
    s_capturedLogs.clear();
    ASSERT_EQ(RunLuauChunk(L, "showDialog('FishDialog')", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_TRUE(logged("Stereo Dialog opened")) << "the dialog's opening is in the log";
    EXPECT_TRUE(logged("stereoscopic view")) << "and says the view stayed stereoscopic";
    EXPECT_FALSE(logged("Quad layer")) << "not that it switched to the quad";

    ASSERT_EQ(RunLuauChunk(L, "closeDialog()", "=closeFishDialog"), 0);
    FRAMES(3);
    EXPECT_TRUE(logged("Stereo Dialog closed"));

    s_capturedLogs.clear();
    ASSERT_EQ(RunLuauChunk(L, "showDialog('YesNoDialog')", "=openOtherDialog"), 0);
    FRAMES(3);
    EXPECT_TRUE(logged("switched presentation to 6DOF Quad layer")) << "a menu that gets the quad says so";
    EXPECT_FALSE(logged("Stereo Dialog opened"));
}

TEST_F(CompanionModTest, AMenuThatGetsTheQuadStillTakesEveryInputOffThePadAndGoesBackOnTheGrip) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_STEREO_DIALOG_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    struct Case { const char* name; const char* open; const char* close; };
    const Case cases[] = {
        { "a dialog the mod does not know", "showDialog('YesNoDialog')", "closeDialog()" },
        { "the pause menu", "showScreen('InGameMenu')", "closeScreen()" },
        { "the fish dialog in 2D Floating Window Mode", "VRCompanion:set2DViewActive(true); showDialog('FishDialog')",
          "closeDialog(); VRCompanion:set2DViewActive(false)" },
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_EQ(RunLuauChunk(L, c.open, "=open"), 0);
        FRAMES(3);
        headset.SetHandInputs(kLeft, Everything());
        headset.SetHandInputs(kRight, Everything());
        FRAMES(2);
        EXPECT_TRUE(IsNeutral(Pad(bridge))) << "the menu is using every input: none reaches the pad";
        EXPECT_GE(Backs(L), 1) << "and the grip goes back";
        headset.SetHandInputs(kLeft, FakeOpenXrControllers::Inputs{});
        headset.SetHandInputs(kRight, FakeOpenXrControllers::Inputs{});
        FRAMES(2);
        ASSERT_EQ(RunLuauChunk(L, std::string(c.close) + "; _G.backs = {}", "=close"), 0);
        FRAMES(3);
    }
}