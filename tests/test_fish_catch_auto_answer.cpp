// Stereo Dialogs, the fish catch auto-answer (spec #142, ticket #147, ADR-0032): the saved console command
// `vrFishCatch [ask|sell|release]` answers the fish catch dialog (Highlands Fishing DLC) as it opens, so the player never sees it,
// in the stereoscopic view and in 2D Floating Window Mode alike, and shows a short notification of the catch. Seen from outside:
// the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the GUI as
// the engine's Gui.lua keeps it and as the headset probe of ticket #143 measured it; the mission's blinking warning, which is the
// notification), the test runs the console command, opens the dialog, drives frames the way the game would and asserts what the
// player gets: which answer the dialog was given, whether it is still open, what the notification said, what the command
// answers and what is written to the settings file. Never the mod's own state.
// Prior art: test_stereo_dialogs.cpp (the dialog stub), test_hand_tools_left_hand.cpp (a console setting saved across sessions).

#include "wheel_grab_test_support.h"
#include <string>

namespace {

using namespace wheel_grab_test;

// The game's GUI with the fish catch dialog as the headset probe measured it (spec #142, the first comment): the dialog is the
// root element in g_gui.dialogs, g_gui.currentDialogName is "FishDialog", g_gui.currentGuiName stays empty, its target is a
// generic yes/no dialog (onYes is SELL, onNo is RELEASE) and the catch is only in the text, target.dialogTextElement.text.
// Answering a dialog closes it, as the game's does. The answers it was given are recorded in `answers`, the notifications the
// mission was asked to show in `notices`. `showFishDialog{...}` opens one:
//   text         the text the dialog shows (the probe's, by default)
//   stayOpen     an answer does not close the dialog
//   noMethods    the dialog has no onYes / onNo
// `showDialog(name)` opens another dialog.
constexpr const char* kFishStubs = R"luau(
    _G.Input = { MOUSE_BUTTON_NONE = 0, MOUSE_BUTTON_LEFT = 1, isMouseButtonPressed = function() return false end }
    _G.InputAction = { MENU_BACK = "MENU_BACK" }
    _G.g_gui = {
        currentGuiName = "",
        currentDialogName = "",
        dialogs = {},
        getIsGuiVisible = function(self) return self.currentGuiName ~= "" or #self.dialogs > 0 end,
        mouseEvent = function() end,
        onMenuInput = function() end,
    }
    _G.answers, _G.notices = {}, {}
    _G.g_currentMission = _G.g_currentMission or {}
    _G.g_currentMission.showBlinkingWarning = function(self, text, ms) table.insert(_G.notices, text) end

    _G.closeDialog = function()
        table.remove(g_gui.dialogs)
        g_gui.currentDialogName = #g_gui.dialogs > 0 and g_gui.dialogs[#g_gui.dialogs].name or ""
    end
    _G.showDialog = function(name, target)
        target = target or {}
        local root = { name = name, target = target }
        target.dialogElement = root
        table.insert(g_gui.dialogs, root)
        g_gui.currentDialogName = name
        return root
    end
    _G.showFishDialog = function(options)
        options = options or {}
        local target = {
            yesButton = { inputActionName = "MENU_ACCEPT" },
            noButton = { inputActionName = "MENU_BACK" },
            dialogTextElement = { text = options.text or
                "You've caught a trout!\n\nDo you want to release the fish to the water or sell it for 100 $?" },
        }
        local function answer(choice)
            table.insert(_G.answers, choice)
            if not options.stayOpen then closeDialog() end
        end
        if not options.noMethods then
            target.onYes = function(self) answer("sell") end
            target.onNo = function(self) answer("release") end
        end
        return showDialog("FishDialog", target)
    end

    -- The engine's XML settings API, remembering what was saved (in savedXml) so a session can be replayed into a fresh Lua state.
    _G.savedXml = _G.savedXml or {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

// The mod started on a map with a player in a cab and the GUI above.
#define START_FISH_SCENE() \
    do { \
        headset.SetHeadPose(HandAt(0.0f, 1.6f, 0.0f)); \
        headset.manager->PollHeadPose(); \
        SetGameTime(10000); \
        StartOptions options_; \
        options_.noPullAway = true; \
        options_.extraStubs = kFishStubs; \
        ASSERT_NO_FATAL_FAILURE(StartWheelGrabMod(L, bridge, SteerableCab(), options_)); \
        FRAMES(3); \
    } while (0)

std::string Answers(lua_State* L) { return EvalLua(L, "table.concat(answers, ',')"); }
std::string Notices(lua_State* L) { return EvalLua(L, "table.concat(notices, ' | ')"); }
int NoticeCount(lua_State* L) { return std::stoi(EvalLua(L, "#notices")); }
bool DialogIsOpen(lua_State* L) { return EvalLua(L, "#g_gui.dialogs > 0") == "true"; }

bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

}  // namespace

// ---- It answers as the dialog opens ----

TEST_F(CompanionModTest, WithVrFishCatchSellTheFishDialogIsSoldAsItOpensAndTheCatchIsNotified) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openFishDialog"), 0);
    FRAMES(1);

    EXPECT_EQ(Answers(L), "sell") << "the dialog was answered with SELL";
    EXPECT_FALSE(DialogIsOpen(L)) << "and the player never has it open";
    EXPECT_EQ(NoticeCount(L), 1);
    EXPECT_TRUE(Contains(Notices(L), "trout")) << Notices(L);
    EXPECT_TRUE(Contains(Notices(L), "100 $")) << Notices(L);
}

TEST_F(CompanionModTest, WithVrFishCatchReleaseTheFishDialogIsReleasedAsItOpensAndTheCatchIsNotified) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'release')", "=release"), 0);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openFishDialog"), 0);
    FRAMES(1);

    EXPECT_EQ(Answers(L), "release");
    EXPECT_FALSE(DialogIsOpen(L));
    EXPECT_EQ(NoticeCount(L), 1);
    EXPECT_TRUE(Contains(Notices(L), "Released")) << Notices(L);
    EXPECT_TRUE(Contains(Notices(L), "trout")) << Notices(L);
}

TEST_F(CompanionModTest, TheAutoAnsweredFishDialogNeverTakesTheViewOffTheStereoscopicProjection) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openFishDialog"), 0);
    FRAMES(3);
    EXPECT_FALSE(bridge.IsMenuVisible()) << "answered before the view looks, so there is no menu to show";
}

TEST_F(CompanionModTest, InTwoDFloatingWindowModeTheFishDialogIsAnsweredToo) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:set2DViewActive(true)", "=enter2D"), 0);
    FRAMES(3);
    struct Case { const char* choice; const char* answer; };
    for (const Case& c : { Case{ "sell", "sell" }, Case{ "release", "release" } }) {
        SCOPED_TRACE(c.choice);
        ASSERT_EQ(RunLuauChunk(L, (std::string("answers = {}; notices = {}; runCommand('vrFishCatch', '") + c.choice + "')").c_str(), "=choice"), 0);
        ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openFishDialog"), 0);
        FRAMES(1);
        EXPECT_EQ(Answers(L), c.answer);
        EXPECT_FALSE(DialogIsOpen(L));
        EXPECT_EQ(NoticeCount(L), 1);
        EXPECT_FALSE(bridge.IsMenuVisible()) << "and the quad was never shown for it";
    }
    EXPECT_EQ(EvalLua(L, "VRCompanion:getIs2DViewActive()"), "true") << "2D Floating Window Mode carries on";
}

// ---- It leaves the dialog alone ----

TEST_F(CompanionModTest, WithVrFishCatchAskTheFishDialogStaysOpenForThePlayer) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("ask"), std::string::npos) << "ask is the default";
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openFishDialog"), 0);
    FRAMES(5);
    EXPECT_EQ(Answers(L), "");
    EXPECT_TRUE(DialogIsOpen(L));
    EXPECT_EQ(NoticeCount(L), 0);

    // And choosing ask again after sell puts it back.
    ASSERT_EQ(RunLuauChunk(L, "closeDialog(); runCommand('vrFishCatch', 'sell'); runCommand('vrFishCatch', 'ask'); showFishDialog()", "=askAgain"), 0);
    FRAMES(5);
    EXPECT_EQ(Answers(L), "");
    EXPECT_TRUE(DialogIsOpen(L));
}

TEST_F(CompanionModTest, WithVrFishCatchSellADialogThatIsNotTheFishDialogIsLeftAlone) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        showDialog('YesNoDialog', { onYes = function() table.insert(answers, 'sell') end, onNo = function() table.insert(answers, 'release') end })
    )luau", "=openOtherDialog"), 0);
    FRAMES(5);
    EXPECT_EQ(Answers(L), "");
    EXPECT_TRUE(DialogIsOpen(L));
    EXPECT_EQ(NoticeCount(L), 0);
}

// ---- Once per opening ----

TEST_F(CompanionModTest, ADialogThatStaysOpenAfterTheAnswerIsAnsweredOnlyOnceAndTheNextCatchIsAnsweredAgain) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog({ stayOpen = true })", "=openFishDialog"), 0);
    FRAMES(10);
    EXPECT_EQ(Answers(L), "sell") << "answered once while it stays open";
    EXPECT_EQ(NoticeCount(L), 1);

    ASSERT_EQ(RunLuauChunk(L, "closeDialog()", "=close"), 0);
    FRAMES(2);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openAgain"), 0);
    FRAMES(2);
    EXPECT_EQ(Answers(L), "sell,sell") << "the next catch is a new opening";
    EXPECT_EQ(NoticeCount(L), 2);
}

// ---- The notification ----

TEST_F(CompanionModTest, WhenTheDialogTextIsNotTheOneTheProbeMeasuredTheNotificationOnlySaysFishSoldOrReleased) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog({ text = 'Du hast eine Forelle gefangen!' })", "=openFishDialog"), 0);
    FRAMES(1);
    EXPECT_EQ(Notices(L), "Fish sold");

    ASSERT_EQ(RunLuauChunk(L, "notices = {}; runCommand('vrFishCatch', 'release'); showFishDialog({ text = 'Du hast eine Forelle gefangen!' })", "=release"), 0);
    FRAMES(1);
    EXPECT_EQ(Notices(L), "Fish released");
}

TEST_F(CompanionModTest, TheNotificationNamesTheSpeciesAndThePriceFromTheDialogText) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, R"luau(showFishDialog({ text = "You've caught an eel!\n\nDo you want to release the fish to the water or sell it for 1,250 $?" }))luau", "=openFishDialog"), 0);
    FRAMES(1);
    EXPECT_EQ(Notices(L), "Sold eel for 1,250 $");
}

// ---- A dialog without the answer method ----

TEST_F(CompanionModTest, AFishDialogWithoutAnAnswerMethodIsLeftOpenForThePlayerWithNoErrorAndNoNotification) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'sell')", "=sell"), 0);
    ASSERT_EQ(RunLuauChunk(L, "showFishDialog({ noMethods = true })", "=openFishDialog"), 0);
    FRAMES(5);
    EXPECT_TRUE(DialogIsOpen(L));
    EXPECT_EQ(NoticeCount(L), 0);
    EXPECT_FALSE(bridge.IsMenuVisible()) << "it is still a Stereo Dialog";
}

// ---- The command and the setting ----

TEST_F(CompanionModTest, VrFishCatchWithNoArgumentReportsTheValueAndAnInvalidArgumentIsRefusedWithTheUsage) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());

    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("ask"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch', 'sell')").find("sell"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("sell"), std::string::npos);
    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch', 'RELEASE')").find("release"), std::string::npos) << "any case";
    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("release"), std::string::npos);

    for (const char* bad : { "keep", "on", "true", "1" }) {
        SCOPED_TRACE(bad);
        const std::string answer = EvalLua(L, std::string("runCommand('vrFishCatch', '") + bad + "')");
        EXPECT_NE(answer.find("Usage: vrFishCatch [ask|sell|release]"), std::string::npos) << answer;
        EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("release"), std::string::npos) << "an invalid argument changes nothing";
    }
}

TEST_F(CompanionModTest, TheFishCatchSettingIsSavedAcrossSessions) {
    std::string saved;
    {
        FakeTrackedHeadset headset;
        fs25vr::VRBridge bridge(headset.manager, nullptr);
        START_FISH_SCENE();
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_EQ(RunLuauChunk(L, "runCommand('vrFishCatch', 'release')", "=release"), 0);
        saved = EvalLua(L, "savedXml['vrSettings.fishCatchAnswer']");
        EXPECT_EQ(saved, "release") << "the choice is written to the settings file";
    }

    // A fresh game session reads it back.
    lua_close(L);
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcclosure(L, &MockPrint, "print", 0);
    lua_setglobal(L, "print");
    FakeTrackedHeadset secondHeadset;
    fs25vr::VRBridge secondBridge(secondHeadset.manager, nullptr);
    ASSERT_TRUE(secondBridge.Inject(L));
    ASSERT_EQ(LoadCompanionModChecked(L), 0);
    ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, kFishStubs, "=fishStubs"), 0);
    ASSERT_EQ(RunLuauChunk(L, (std::string("savedXml['vrSettings.fishCatchAnswer'] = '") + saved + "'; VRCompanion:loadMap('x')").c_str(), "=loadMapAgain"), 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("release"), std::string::npos) << "the new session starts with the saved choice";

    ASSERT_EQ(RunLuauChunk(L, "showFishDialog()", "=openFishDialog"), 0);
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:update(16.667)", "=update"), 0);
    EXPECT_EQ(Answers(L), "release") << "and answers with it";
}

TEST_F(CompanionModTest, ASavedValueThatIsNotAnAnswerLeavesTheDefault) {
    FakeTrackedHeadset headset;
    fs25vr::VRBridge bridge(headset.manager, nullptr);
    START_FISH_SCENE();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.fishCatchAnswer'] = 'keep'; VRCompanion:loadConfiguration()", "=loadGarbage"), 0);
    EXPECT_NE(EvalLua(L, "runCommand('vrFishCatch')").find("ask"), std::string::npos);
}
