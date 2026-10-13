// Menu pointing in the Companion Mod (spec #71): the real Companion Mod over the real
// VRBridge over FakeTrackedHeadset, with a stub GUI that records the cursor moves and clicks the
// mod delivers through the game's own GUI mouse handling. The first block covers the two bridge
// calls the mod relies on (panel hit, drawn cursor); the rest covers the mod's behaviour.

#include "companion_mod_fixture.h"

namespace {

using fs25vr::FakeTrackedHeadset;
using fs25vr::HeadPose;
using fs25vr::Vector3;

constexpr uint32_t kLeft = 0;
constexpr uint32_t kRight = 1;

HeadPose PoseAt(float x, float y, float z) {
    HeadPose pose;
    pose.position = Vector3(x, y, z);
    pose.isValid = true;
    pose.positionTracked = true;
    pose.orientationTracked = true;
    return pose;
}

// Head at (0, 1.6, 0) looking ahead; the menu panel will sit 1.5 m in front of it at eye height.
void PlaceHead(FakeTrackedHeadset& headset) {
    headset.SetHeadPose(PoseAt(0.0f, 1.6f, 0.0f));
    headset.manager->PollHeadPose();
}

} // namespace

// ---- VRBridge.getPanelHit / VRBridge.setMenuCursor ----

TEST_F(CompanionModTest, BridgeReportsWhereAHandsRayHitsTheVisibleMenuPanel) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    bridge.SetMenuVisible(true);
    headset.SetHandPose(kRight, PoseAt(0.0f, 1.6f, 0.0f));
    headset.SetHandPose(kLeft, PoseAt(0.4f, 1.6f + 0.225f, 0.0f));
    headset.NextFrame();

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local right = VRBridge.getPanelHit(1)
        assert(right.hit == true, "right hand hits")
        assert(math.abs(right.u - 0.5) < 1e-4 and math.abs(right.v - 0.5) < 1e-4, "centre")
        assert(math.abs(right.distance - 1.5) < 1e-4, "distance along the ray")
        local left = VRBridge.getPanelHit("left")
        assert(left.hit == true and math.abs(left.u - 0.75) < 1e-4 and math.abs(left.v - 0.25) < 1e-4, "left hand, by name")
    )luau", "=panelHit"), 0);
}

TEST_F(CompanionModTest, BridgePanelHitFillsACallerSuppliedTableInPlaceLikeTheOtherPoseCalls) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    bridge.SetMenuVisible(true);
    headset.SetHandPose(kRight, PoseAt(0.0f, 1.6f, 0.0f));
    headset.NextFrame();

    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local incomplete = {}
        local fresh = VRBridge.getPanelHit(1, incomplete)
        assert(fresh ~= incomplete and next(incomplete) == nil, "an incomplete table is left untouched")
        assert(fresh.hit == true)
        local count = 0
        for _ in pairs(fresh) do count = count + 1 end
        assert(count == 4, "hit, u, v, distance")
        assert(VRBridge.getPanelHit(1, fresh) == fresh, "a complete table is filled in place")
        assert(VRBridge.getPanelHit(fresh, 1) == fresh, "argument order does not matter")
    )luau", "=panelHitInPlace"), 0);
}

TEST_F(CompanionModTest, BridgePanelHitIsAMissWithoutAVisiblePanelATrackedHandOrAnInjectorRuntime) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    headset.SetHandPose(kRight, PoseAt(0.0f, 1.6f, 0.0f));
    headset.NextFrame();

    // No menu and no 2D Floating Window: there is no panel to hit.
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local miss = VRBridge.getPanelHit(1)
        assert(miss.hit == false and miss.u == 0 and miss.v == 0 and miss.distance == 0)
    )luau", "=noPanel"), 0);

    // The 2D Floating Window Mode panel counts the same as a menu.
    bridge.Set2DViewActive(true);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRBridge.getPanelHit(1).hit == true, '2D panel')", "=panel2d"), 0);
    bridge.Set2DViewActive(false);

    bridge.SetMenuVisible(true);
    headset.SetHandTracked(kRight, false);
    headset.NextFrame();
    ASSERT_EQ(RunLuauChunk(L, "assert(VRBridge.getPanelHit(1).hit == false, 'untracked')", "=untracked"), 0);
    ASSERT_EQ(RunLuauChunk(L, "assert(VRBridge.getPanelHit(5).hit == false and VRBridge.getPanelHit().hit == false, 'bad hand')", "=badHand"), 0);

    lua_State* L2 = luaL_newstate();
    luaL_openlibs(L2);
    fs25vr::VRBridge bare(nullptr, nullptr);
    ASSERT_TRUE(bare.Inject(L2));
    bare.SetMenuVisible(true);
    ASSERT_EQ(RunLuauChunk(L2, "assert(VRBridge.getPanelHit(1).hit == false)", "=noRuntime"), 0);
    lua_close(L2);
}

TEST_F(CompanionModTest, BridgeSetsTheDrawnCursorWhileThePointerRayHasControlAndClearsItOnTheMenuChanging) {
    FakeTrackedHeadset headset;
    PlaceHead(headset);
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    ASSERT_TRUE(bridge.Inject(L));
    const auto& pointer = [&]() { return headset.manager->GetPointerCursorOverride(); };

    EXPECT_FALSE(pointer().active);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.25, 0.75)", "=setCursor"), 0);
    EXPECT_TRUE(pointer().active);
    EXPECT_FLOAT_EQ(pointer().u, 0.25f);
    EXPECT_FLOAT_EQ(pointer().v, 0.75f);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 2.0, -1.0)", "=clampCursor"), 0);
    EXPECT_FLOAT_EQ(pointer().u, 1.0f) << "kept on the panel";
    EXPECT_FLOAT_EQ(pointer().v, 0.0f);

    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(false)", "=releaseCursor"), 0);
    EXPECT_FALSE(pointer().active) << "the desk mouse has control again";

    // A stale ray cursor must not survive a menu opening or closing.
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgain"), 0);
    bridge.SetMenuVisible(true);
    EXPECT_FALSE(pointer().active);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgain2"), 0);
    bridge.SetMenuVisible(false);
    EXPECT_FALSE(pointer().active);
    ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgain3"), 0);
    bridge.Set2DViewActive(true);
    EXPECT_FALSE(pointer().active);
}

// ---- The Companion Mod points at menus ----

namespace {

// The game's GUI as the mod sees it: it records every mouse event the mod delivers (normalized
// screen position with the origin at the BOTTOM left, as the game's GUI uses).
constexpr const char* kGuiStubs = R"luau(
    _G.addConsoleCommand = function() end
    _G.removeConsoleCommand = function() end
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1 }
    _G.guiEvents = {}
    _G.g_gui = {
        visible = true,
        getIsGuiVisible = function(self) return self.visible end,
        mouseEvent = function(self, x, y, isDown, isUp, button)
            if _G.guiFails then error("menu code failed") end
            table.insert(_G.guiEvents, { x = x, y = y, down = isDown, up = isUp, button = button })
        end,
    }
    _G.guiLast = function() return _G.guiEvents[#_G.guiEvents] end
    _G.guiDowns = function() local n = 0 for _, e in ipairs(_G.guiEvents) do if e.down then n = n + 1 end end return n end
    _G.guiUps = function() local n = 0 for _, e in ipairs(_G.guiEvents) do if e.up then n = n + 1 end end return n end
)luau";

// The point on the panel a hand at the head's height aims at when its ray runs parallel to the
// panel's normal: u across (left to right), v down (top to bottom).
HeadPose AimAt(float u, float v) {
    return PoseAt((u - 0.5f) * 1.6f, 1.6f + (0.5f - v) * 0.9f, 0.0f);
}

fs25vr::FakeOpenXrControllers::Inputs Trigger(float value) {
    fs25vr::FakeOpenXrControllers::Inputs inputs;
    inputs.trigger = value;
    return inputs;
}

class MenuPointerTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge{ headset.manager, nullptr };

    // Starts the mod over the headset with a menu showing and the Pointer Hand (right) in use,
    // aimed at the middle of the panel.
    void StartWithMenuOpen() {
        PlaceHead(headset);
        SetGameTime(10000);
        ASSERT_TRUE(bridge.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kGuiStubs, "=guiStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        headset.SetHandPose(kRight, AimAt(0.5f, 0.5f));
        NextFrame(&headset);
        AimRightHandAt(0.55f, 0.5f);  // a real move: the hand is in use and the ray takes control
    }

    void AimRightHandAt(float u, float v) {
        headset.SetHandPose(kRight, AimAt(u, v));
        NextFrame(&headset);
    }
    void PullRightTrigger(float value) {
        headset.SetHandInputs(kRight, Trigger(value));
        NextFrame(&headset);
    }
    void ExpectLastGuiEventAt(double x, double y) {
        const std::string chunk = "local e = guiLast(); assert(e ~= nil, 'no GUI event'); assert(math.abs(e.x - " +
            std::to_string(x) + ") < 1e-3 and math.abs(e.y - " + std::to_string(y) + ") < 1e-3, 'at ' .. e.x .. ', ' .. e.y)";
        EXPECT_EQ(RunLuauChunk(L, chunk, "=lastGuiEvent"), 0);
    }
    int Count(const char* expression) { return std::stoi(EvalLua(L, expression)); }
};

} // namespace

TEST_F(MenuPointerTest, AimingThePointerHandAtTheMenuMovesTheGamesCursorThereAndTheDrawnCursorFollows) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());

    AimRightHandAt(0.75f, 0.25f);

    // u is across from the left and v is down from the top on the panel; the game's GUI measures y up
    // from the bottom, so the cursor lands at (0.75, 0.75) there.
    ExpectLastGuiEventAt(0.75, 0.75);
    EXPECT_EQ(EvalLua(L, "guiLast().down"), "false");
    EXPECT_EQ(EvalLua(L, "guiLast().up"), "false");
    EXPECT_EQ(Count("guiDowns()"), 0) << "aiming alone never clicks";

    const auto& drawn = headset.manager->GetPointerCursorOverride();
    EXPECT_TRUE(drawn.active);
    EXPECT_NEAR(drawn.u, 0.75f, 1e-3f);
    EXPECT_NEAR(drawn.v, 0.25f, 1e-3f);

    AimRightHandAt(0.20f, 0.90f);
    ExpectLastGuiEventAt(0.20, 0.10);
    EXPECT_NEAR(headset.manager->GetPointerCursorOverride().u, 0.20f, 1e-3f);
    EXPECT_NEAR(headset.manager->GetPointerCursorOverride().v, 0.90f, 1e-3f);
}

TEST_F(MenuPointerTest, ATriggerPullIsOneLeftClickAtTheCursorPosition) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.40f, 0.30f);
    ASSERT_EQ(Count("guiDowns()"), 0);

    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 1);
    EXPECT_EQ(Count("guiUps()"), 0) << "the button stays down while the trigger is held";
    EXPECT_EQ(EvalLua(L, "guiLast().button"), "1") << "the left mouse button";
    ExpectLastGuiEventAt(0.40, 0.70);

    for (int i = 0; i < 5; ++i) NextFrame(&headset);  // holding the trigger is not more clicks
    EXPECT_EQ(Count("guiDowns()"), 1);

    PullRightTrigger(0.0f);
    EXPECT_EQ(Count("guiDowns()"), 1);
    EXPECT_EQ(Count("guiUps()"), 1);
    EXPECT_EQ(EvalLua(L, "guiLast().button"), "1");
    ExpectLastGuiEventAt(0.40, 0.70);

    PullRightTrigger(1.0f);
    PullRightTrigger(0.0f);
    EXPECT_EQ(Count("guiDowns()"), 2) << "the next pull is the next click";
    EXPECT_EQ(Count("guiUps()"), 2);
}

TEST_F(MenuPointerTest, ALightTouchOnTheTriggerDoesNotClickAndASlowReleaseDoesNotClickTwice) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.40f, 0.30f);

    PullRightTrigger(0.30f);
    EXPECT_EQ(Count("guiDowns()"), 0);
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 1);
    for (float value : { 0.8f, 0.5f, 0.7f, 0.5f, 0.8f }) {  // wobbling between the two thresholds
        PullRightTrigger(value);
    }
    EXPECT_EQ(Count("guiDowns()"), 1) << "hysteresis: the wobble is the same pull";
    EXPECT_EQ(Count("guiUps()"), 0);
    PullRightTrigger(0.1f);
    EXPECT_EQ(Count("guiUps()"), 1);
}

TEST_F(MenuPointerTest, MovingTheHandWhileTheTriggerIsHeldDragsWithTheButtonDown) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.30f, 0.50f);
    PullRightTrigger(1.0f);
    AimRightHandAt(0.60f, 0.50f);

    ExpectLastGuiEventAt(0.60, 0.50);
    EXPECT_EQ(Count("guiDowns()"), 1);
    EXPECT_EQ(Count("guiUps()"), 0);
    PullRightTrigger(0.0f);
    ExpectLastGuiEventAt(0.60, 0.50);
    EXPECT_EQ(Count("guiUps()"), 1) << "released where the hand now points";
}

TEST_F(MenuPointerTest, ATriggerPullOffThePanelDoesNothingEvenIfTheHandSweepsOntoItWhileHeld) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    const int eventsOnPanel = Count("#guiEvents");
    AimRightHandAt(1.30f, 0.50f);  // well right of the panel
    EXPECT_EQ(Count("#guiEvents"), eventsOnPanel) << "the cursor stays where it was";

    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 0) << "nothing under the ray";
    AimRightHandAt(0.50f, 0.50f);
    EXPECT_EQ(Count("guiDowns()"), 0) << "a pull that began off the panel is not a click";
    PullRightTrigger(0.0f);
    EXPECT_EQ(Count("guiUps()"), 0);
}

TEST_F(MenuPointerTest, MovingTheDeskMouseTakesControlAndMovingThePointerHandTakesItBack) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.40f, 0.30f);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "ray");
    ASSERT_TRUE(headset.manager->GetPointerCursorOverride().active);
    const int eventsWhileRayControlled = Count("#guiEvents");

    // The engine reports the desk mouse to the mod as mouse events.
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:mouseEvent(0.20, 0.60, false, false, 0)", "=deskMouse"), 0);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "mouse");
    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active) << "the drawn cursor is the desk mouse's again";
    EXPECT_EQ(Count("#guiEvents"), eventsWhileRayControlled) << "the ray leaves the game's cursor alone";
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 0) << "the trigger does not click while the mouse has control";
    PullRightTrigger(0.0f);

    AimRightHandAt(0.70f, 0.30f);  // the Pointer Hand moves
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "ray");
    EXPECT_TRUE(headset.manager->GetPointerCursorOverride().active);
    ExpectLastGuiEventAt(0.70, 0.70);
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 1);
}

TEST_F(MenuPointerTest, TheDeskMouseIsAlsoNoticedBySamplingItsPositionWhenNoMouseEventsReachTheMod) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        _G.g_inputBinding = { x = 0.5, y = 0.5, getMousePosition = function(self) return self.x, self.y end }
    )luau", "=inputBinding"), 0);
    NextFrame(&headset);  // first sample: just remembers where the mouse is
    AimRightHandAt(0.40f, 0.30f);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "ray");

    ASSERT_EQ(RunLuauChunk(L, "g_inputBinding.x = 0.52", "=mouseMoved"), 0);
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "mouse");
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "mouse") << "a mouse lying still stays in control";
}

TEST_F(MenuPointerTest, ThePointerRayStartsWithoutControlAndTheStillDeskMouseDoesNotTakeItBack) {
    PlaceHead(headset);
    SetGameTime(10000);
    ASSERT_TRUE(bridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kGuiStubs, "=guiStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "mouse");
    EXPECT_EQ(EvalLua(L, "VRCompanion:isPointerRayInControl()"), "false");

    headset.SetHandPose(kRight, AimAt(0.5f, 0.5f));
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:getPointerOwner()"), "mouse") << "a Motion Controller that has not moved is not pointing";
    AimRightHandAt(0.6f, 0.5f);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isPointerRayInControl()"), "true");
}

TEST_F(MenuPointerTest, TheRayLosesControlWhenThePointerHandIsPutDown) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.40f, 0.30f);
    ASSERT_EQ(EvalLua(L, "VRCompanion:isPointerRayInControl()"), "true");

    AdvanceGameTime(3500);  // the Motion Controller lies still for longer than the in-use window
    NextFrame(&headset);
    EXPECT_EQ(EvalLua(L, "VRCompanion:isPointerRayInControl()"), "false");
    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active);
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 0);
}

TEST_F(MenuPointerTest, NothingIsMovedOrClickedWhileNoMenuOrFloatingWindowIsShowing) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    NextFrame(&headset);
    const int eventsBefore = Count("#guiEvents");

    AimRightHandAt(0.40f, 0.30f);
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("#guiEvents"), eventsBefore);
    EXPECT_EQ(Count("guiDowns()"), 0);
    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active);
}

TEST_F(MenuPointerTest, ThePointerRayWorksOnThe2DFloatingWindowPanelToo) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    bridge.Set2DViewActive(true);
    NextFrame(&headset);

    AimRightHandAt(0.25f, 0.40f);
    ExpectLastGuiEventAt(0.25, 0.60);
    EXPECT_TRUE(headset.manager->GetPointerCursorOverride().active);
    PullRightTrigger(1.0f);
    PullRightTrigger(0.0f);
    EXPECT_EQ(Count("guiDowns()"), 1);
    EXPECT_EQ(Count("guiUps()"), 1);
}

TEST_F(MenuPointerTest, ClosingTheMenuWhileTheTriggerIsHeldReleasesTheButtonOnceAndHandsTheCursorBack) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.40f, 0.30f);
    PullRightTrigger(1.0f);
    ASSERT_EQ(Count("guiDowns()"), 1);

    ASSERT_EQ(RunLuauChunk(L, "g_gui.visible = false", "=closeMenu"), 0);
    NextFrame(&headset);
    EXPECT_EQ(Count("guiUps()"), 1);
    ExpectLastGuiEventAt(0.40, 0.70);
    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active);
    for (int i = 0; i < 3; ++i) NextFrame(&headset);
    EXPECT_EQ(Count("guiUps()"), 1);
}

TEST_F(MenuPointerTest, WithMotionControllersOffTheMenuIsLeftToTheMouse) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    AimRightHandAt(0.40f, 0.30f);
    ASSERT_TRUE(headset.manager->GetPointerCursorOverride().active);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:setMotionControllersEnabled(false)", "=off"), 0);
    NextFrame(&headset);

    EXPECT_FALSE(headset.manager->GetPointerCursorOverride().active);
    const int eventsBefore = Count("#guiEvents");
    AimRightHandAt(0.70f, 0.70f);
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("#guiEvents"), eventsBefore);
}

TEST_F(MenuPointerTest, MenusWorkWithoutInteractiveControlInstalled) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(EvalLua(L, "tostring(_G.FS25_interactiveControl) .. tostring(_G.g_modIsLoaded)"), "nilnil");
    AimRightHandAt(0.40f, 0.30f);
    PullRightTrigger(1.0f);
    EXPECT_EQ(Count("guiDowns()"), 1);
}

TEST_F(MenuPointerTest, AFailingMenuNeverBreaksTheGameUpdate) {
    StartWithMenuOpen();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, "_G.guiFails = true", "=failing"), 0);
    AimRightHandAt(0.40f, 0.30f);  // NextFrame fails the test if the update raises
    PullRightTrigger(1.0f);
    PullRightTrigger(0.0f);
    int reports = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find("menu pointer") != std::string::npos) ++reports;
    }
    EXPECT_EQ(reports, 1) << "reported once, not every frame";
}

