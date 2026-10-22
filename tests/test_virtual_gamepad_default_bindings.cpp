// The Virtual Gamepad's VR-comfort default bindings (ADR-0028, spec #98, ticket #104), seen from outside: the real Companion
// Mod runs against the real VRBridge over FakeTrackedHeadset, with a stand-in for the game's g_inputBinding shaped after the
// finding of spike #99 (companion_mod_stubs.h). The test turns the Virtual Gamepad on, lets frames run and reads what the
// game's bindings look like and whether the game was told to save them. Covered: the one-time rewrite of the Virtual
// Gamepad's device only, a real pad left alone, the saved flag that stops a second rewrite for good (also after the player
// changed a binding), and the fallback (no API, an API that raises) that leaves the bindings and the flag as they were.
//
// The Virtual Gamepad takes XInput slot 3 here (no real pads in the test process), so its device is 3_XINPUT_GAMEPAD; the
// stand-in also has a real pad in slot 0. The button numbers are the game's own for an Xbox pad (the default gamepad binding
// template): BUTTON_5 / BUTTON_6 are LB / RB, BUTTON_10 Start, BUTTON_11 / BUTTON_12 the left / right stick click.

#include "companion_mod_fixture.h"
#include "companion_mod_stubs.h"
#include "input/virtual_gamepad.h"
#include <memory>
#include <string>

namespace {

using fs25vr::FakeTrackedHeadset;

// Console commands as the engine registers them, so tests can run the handlers.
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

// The engine's XML settings API, remembering what was saved so a session can be replayed.
constexpr const char* kXmlStubs = R"luau(
    _G.savedXml = _G.savedXml or {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLBool = function(file, key, value) _G.savedXml[key] = value end
    _G.setXMLString = function(file, key, value) _G.savedXml[key] = value end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.loadXMLFile = function() return 2 end
    _G.getXMLFloat = function(file, key) local v = _G.savedXml[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.savedXml[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.savedXml[key]; return type(v) == "string" and v or nil end
)luau";

const std::string kPad = "3_XINPUT_GAMEPAD";   // the Virtual Gamepad's device
const std::string kRealPad = "0_XINPUT_GAMEPAD";

class VirtualGamepadDefaultBindingsTest : public CompanionModTest {
protected:
    FakeTrackedHeadset headset;
    std::unique_ptr<fs25vr::VRBridge> bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);

    // Starts the Companion Mod over the headset with the game's bindings as `bindings` (Lua options for StubInput.install)
    // and the settings file as `settingsFile` (Lua that fills savedXml: what an earlier game session left), and lets a few
    // frames run. The Virtual Gamepad is on out of the box; with `turnOn` false the settings file has the player's `off`
    // saved, for a test that turns it on itself.
    void StartGame(const std::string& bindings = "{}", const std::string& settingsFile = "", bool turnOn = true) {
        ASSERT_TRUE(bridgeOwner->Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kConsoleStubs, "=consoleStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, kXmlStubs, "=xmlStubs"), 0);
        ASSERT_EQ(RunLuauChunk(L, settingsFile, "=settingsFile"), 0);
        if (!turnOn) ASSERT_EQ(RunLuauChunk(L, "savedXml['vrSettings.virtualGamepad'] = false", "=padSavedOff"), 0);
        ASSERT_EQ(RunLuauChunk(L, kStubInputBinding, "=inputBindingStub"), 0);
        ASSERT_EQ(RunLuauChunk(L, "StubInput.install(" + bindings + ")", "=installBindings"), 0);
        ASSERT_EQ(RunLuauChunk(L, "VRCompanion:loadMap('x')", "=loadMap"), 0);
        SetGameTime(10000);
        NextFrame(&headset);
        if (turnOn) Frames(5);
    }

    void Frames(int count) {
        for (int i = 0; i < count; ++i) NextFrame(&headset);
    }

    // A new game session over the same headset: a fresh Lua state and a fresh Injector bridge. The game loads the bindings
    // the last session saved (what the stand-in holds now, as the saved file would), and the Companion Mod's settings file\n    // is what the last session left. `stubOptions` adds StubInput.install options (Lua, e.g. "variant = 'saveRaises'").
    // `settingsOfAnEarlierBuild` leaves the Recenter migration flag out of the settings file, as a build from before the Recenter
    // fix did (its rewrite had run and saved the first flag only).
    void RestartGameSession(const std::string& stubOptions = "", bool settingsOfAnEarlierBuild = false) {
        const std::string saved = EvalLua(L, "StubInput.serialize()");
        const std::string flag = EvalLua(L, "savedXml['vrSettings.virtualGamepadDefaultsWritten']");
        const std::string migrated = EvalLua(L, "savedXml['vrSettings.virtualGamepadRecenterMigrated']");
        const std::string enabled = EvalLua(L, "savedXml['vrSettings.virtualGamepad']");
        lua_close(L);
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcclosure(L, &MockPrint, "print", 0);
        lua_setglobal(L, "print");
        s_capturedLogs.clear();
        bridgeOwner = std::make_unique<fs25vr::VRBridge>(headset.manager, nullptr);
        std::string seed = "_G.savedXml = {}";
        if (flag != "nil") seed += "; savedXml['vrSettings.virtualGamepadDefaultsWritten'] = " + flag;
        if (migrated != "nil" && !settingsOfAnEarlierBuild) seed += "; savedXml['vrSettings.virtualGamepadRecenterMigrated'] = " + migrated;
        if (enabled != "nil") seed += "; savedXml['vrSettings.virtualGamepad'] = " + enabled;
        StartGame("{ state = [[" + saved + "]]" + (stubOptions.empty() ? "" : ", " + stubOptions) + " }", seed, false);
        TurnOn();
    }

    void TurnOn() {
        EvalLua(L, "runCommand('vrVirtualGamepad', 'on')");
        Frames(5);
    }

    // A long stretch of play: well past the Companion Mod's look at the bindings every second.
    void PlayFor(int seconds) { Frames(seconds * 60); }

    std::string Lua(const std::string& expression) { return EvalLua(L, expression); }
    std::string List(const std::string& action, const std::string& device) {
        return Lua("StubInput.list('" + action + "', '" + device + "')");
    }
    std::string Dump(const std::string& device) { return Lua("StubInput.dump('" + device + "')"); }

    int LogLinesContaining(const std::string& text) {
        int count = 0;
        for (const std::string& line : s_capturedLogs) {
            if (line.find(text) != std::string::npos) ++count;
        }
        return count;
    }
};

} // namespace

TEST_F(VirtualGamepadDefaultBindingsTest, TheFirstSessionRewritesTheVirtualGamepadToTheComfortLayoutAndSavesIt) {
    ASSERT_NO_FATAL_FAILURE(StartGame());
    ASSERT_EQ(Lua("select(2, VRBridge.getVirtualGamepad())"), "3") << "the test machine's Virtual Gamepad slot";

    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "AXIS_3-/+@4") << "Snap Turn left on the right stick's left half, off LB";
    EXPECT_EQ(List("VR_SNAP_TURN_RIGHT", kPad), "AXIS_3+/+@4") << "Snap Turn right on the right stick's right half, off RB";
    EXPECT_EQ(List("AXIS_LOOK_LEFTRIGHT_PLAYER", kPad), "") << "smooth look is off the right stick X on foot";
    EXPECT_EQ(List("AXIS_LOOK_LEFTRIGHT_VEHICLE", kPad), "") << "and in vehicles";
    // The engine drops VR_RECENTER_VIEW altogether (keyboard numpad 5 too) while it carries an XInput BUTTON_12 binding, measured
    // in the game: the rewrite leaves Recenter on the Companion Mod's own GAMEPAD_DEFAULT (the left stick click).
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4") << "the Recenter Action keeps the game's default for the pad";
    EXPECT_EQ(Lua("StubInput.get('VR_RECENTER_VIEW', 'KB_MOUSE_DEFAULT').inputString"), "KEY_numPad5") << "and the keyboard's";
    EXPECT_EQ(List("VR_TOGGLE_VIEW", kPad), "BUTTON_11 BUTTON_10/+@4") << "the VR View Toggle Action on left stick click + Start";
    EXPECT_EQ(List("VR_HUD_TOGGLE", kPad), "BUTTON_10/+@4") << "the rest of the Companion Mod's pad defaults stay";
    EXPECT_EQ(List("JUMP", kPad), "BUTTON_2/+@4") << "and so do the game's other bindings";
    EXPECT_EQ(List("VR_SET_SEAT", kPad), "") << "the Seat Set Action gets no pad input from the rewrite: the player binds it in the Controls screen";
    EXPECT_EQ(Lua("StubInput.get('VR_SET_SEAT', 'KB_MOUSE_DEFAULT').inputString"), "KEY_rctrl KEY_numPad5") << "and keeps its keyboard default";
    EXPECT_EQ(List("AXIS_LOOK_UPDOWN_PLAYER", kPad), "AXIS_4+/+@4 ; AXIS_4-/-@4");

    EXPECT_EQ(Lua("StubInput.saves"), "1") << "the bindings were saved, once";
    EXPECT_EQ(Lua("StubInput.saved == StubInput.dump(nil)"), "true") << "after the rewrite, not before";
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true") << "and the flag is saved with the settings";
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true")
        << "a fresh profile also saves the migration flag: it never needs the Recenter migration";
    EXPECT_EQ(LogLinesContaining("Virtual Gamepad default bindings written"), 1) << "the game log says the rewrite ran";
}

TEST_F(VirtualGamepadDefaultBindingsTest, AFreshProfileNeverRunsTheRecenterMigrationLater) {
    ASSERT_NO_FATAL_FAILURE(StartGame());
    ASSERT_EQ(Lua("StubInput.saves"), "1");

    // The player puts Recenter on RS in the Controls screen themselves: it is theirs, in this session and in the next.
    ASSERT_EQ(RunLuauChunk(L, "StubInput.change('VR_RECENTER_VIEW', '3_XINPUT_GAMEPAD', 'BUTTON_11', 'BUTTON_12')", "=playerChange"), 0);
    ASSERT_NO_FATAL_FAILURE(RestartGameSession());
    PlayFor(3);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_12/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_EQ(LogLinesContaining("Recenter binding"), 0);
}

// ---- The Recenter migration: profiles whose rewrite ran in a build that put Recenter on RS (BUTTON_12) -------------------------
//
// The engine drops VR_RECENTER_VIEW while it carries an XInput BUTTON_12 binding, so such a profile's saved inputBinding.xml
// has to be put right. The tests build that profile from a normal first session (LeaveProfileOfAnEarlierBuild): the saved
// bindings are edited as the earlier build left them (Recenter on BUTTON_12 on the device(s) named) and the settings file has
// the first flag only. The next session (StartNextSession) is the one in which the migration may run.
class VirtualGamepadRecenterMigrationTest : public VirtualGamepadDefaultBindingsTest {
protected:
    void LeaveProfileOfAnEarlierBuild(const std::string& edits, const std::string& firstSession = "{}") {
        ASSERT_NO_FATAL_FAILURE(StartGame(firstSession));
        ASSERT_EQ(RunLuauChunk(L, edits, "=profileEdits"), 0);
    }

    // `stubOptions`: more StubInput.install options for the game's bindings of this session.
    void StartNextSession(const std::string& stubOptions = "") {
        ASSERT_NO_FATAL_FAILURE(RestartGameSession(stubOptions, true));
    }

    static std::string RecenterOnRs(const std::string& device) {
        return "StubInput.change('VR_RECENTER_VIEW', '" + device + "', 'BUTTON_11', 'BUTTON_12')";
    }

    std::string Logs() {
        std::string all;
        for (const std::string& line : s_capturedLogs) all += line + "\n";
        return all;
    }
};

TEST_F(VirtualGamepadRecenterMigrationTest, RecenterOnRsOfTheVirtualGamepadGoesBackToTheStickClickOnceAndIsSaved) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(RecenterOnRs(kPad)));
    ASSERT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_12/+@4") << "the profile the earlier build left";
    std::string expectedDump = Dump(kPad);
    const std::string rsLine = "VR_RECENTER_VIEW 3_XINPUT_GAMEPAD: BUTTON_12/+@4";
    ASSERT_NE(expectedDump.find(rsLine), std::string::npos);
    expectedDump.replace(expectedDump.find(rsLine), rsLine.size(), "VR_RECENTER_VIEW 3_XINPUT_GAMEPAD: BUTTON_11/+@4");

    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);

    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4") << "off RS, onto the left stick click, in the slot RS had";
    EXPECT_EQ(Lua("StubInput.listActive('VR_RECENTER_VIEW', '3_XINPUT_GAMEPAD')"), "BUTTON_11/+@4") << "and what the game acts on follows";
    EXPECT_EQ(Lua("StubInput.get('VR_RECENTER_VIEW', 'KB_MOUSE_DEFAULT').inputString"), "KEY_numPad5") << "the keyboard keeps numpad 5";
    EXPECT_EQ(Lua("StubInput.saves"), "1") << "saved, once";
    EXPECT_EQ(Lua("StubInput.saved == StubInput.dump(nil)"), "true") << "after the change, not before";
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true");
    EXPECT_EQ(LogLinesContaining("Virtual Gamepad Recenter binding migrated"), 1) << "the game log says it ran";
    EXPECT_EQ(LogLinesContaining("default bindings written"), 0) << "the comfort rewrite does not run again";
    EXPECT_EQ(Dump(kPad), expectedDump) << "nothing else of the device was touched";
}

TEST_F(VirtualGamepadRecenterMigrationTest, ItNeverRunsAgainEvenIfThePlayerPutsRecenterBackOnRs) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(RecenterOnRs(kPad)));
    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);
    ASSERT_EQ(Lua("StubInput.saves"), "1");
    ASSERT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4");

    ASSERT_EQ(RunLuauChunk(L, RecenterOnRs(kPad), "=playerChange"), 0);
    ASSERT_NO_FATAL_FAILURE(RestartGameSession());
    PlayFor(3);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_12/+@4") << "their choice, however the engine takes it";
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_EQ(LogLinesContaining("Recenter binding"), 0);
}

TEST_F(VirtualGamepadRecenterMigrationTest, ARealPadsRecenterOnRsAndEveryOtherDevicesBindingsAreNeverTouched) {
    const std::string devices = "{ devices = { '0_XINPUT_GAMEPAD', '1_XINPUT_GAMEPAD', '3_XINPUT_GAMEPAD' } }";
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(
        RecenterOnRs(kPad) + "; " + RecenterOnRs(kRealPad) + "; " + RecenterOnRs("1_XINPUT_GAMEPAD"), devices));
    const std::string realPad = Dump(kRealPad);
    const std::string unpluggedPad = Dump("1_XINPUT_GAMEPAD");
    const std::string keyboard = Dump("KB_MOUSE_DEFAULT");
    ASSERT_EQ(List("VR_RECENTER_VIEW", kRealPad), "BUTTON_12/+@1");

    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);

    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4") << "the Virtual Gamepad was migrated";
    EXPECT_EQ(Dump(kRealPad), realPad) << "a real pad that has BUTTON_12 on Recenter keeps it";
    EXPECT_EQ(List("VR_RECENTER_VIEW", kRealPad), "BUTTON_12/+@1");
    EXPECT_EQ(Dump("1_XINPUT_GAMEPAD"), unpluggedPad) << "also a pad that is not plugged in";
    EXPECT_EQ(Dump("KB_MOUSE_DEFAULT"), keyboard);
}

TEST_F(VirtualGamepadRecenterMigrationTest, AProfileWhoseRecenterIsAlreadyRightIsLeftAloneAndFlagged) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild("-- the file is as the new build writes it"));
    const std::string all = Lua("StubInput.dump(nil)");
    ASSERT_NE(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "nil");

    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);

    EXPECT_EQ(Lua("StubInput.dump(nil)"), all);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "0") << "nothing to change, nothing saved";
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true") << "and it is not looked at again";
    EXPECT_EQ(LogLinesContaining("Recenter binding migrated"), 0);
    EXPECT_EQ(LogLinesContaining("[Warning] Virtual Gamepad"), 0) << Logs();
}

TEST_F(VirtualGamepadRecenterMigrationTest, ARecenterThePlayerMovedElsewhereIsLeftAlone) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(
        "StubInput.change('VR_RECENTER_VIEW', '3_XINPUT_GAMEPAD', 'BUTTON_11', 'BUTTON_8')"));
    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_8/+@4") << "no BUTTON_12 on it: not ours to move";
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true");
}

TEST_F(VirtualGamepadRecenterMigrationTest, OtherBindingsOfThePlayerOnRecenterStayWhenRsGoesAndTheStickClickComes) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(
        RecenterOnRs(kPad) + "; StubInput.add('VR_RECENTER_VIEW', '3_XINPUT_GAMEPAD', 'BUTTON_2', 1)"));
    ASSERT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_12/+@4 ; BUTTON_2/+@1");
    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4 ; BUTTON_2/+@1") << "RS out, the stick click in, the player's own kept";
    EXPECT_EQ(Lua("StubInput.saves"), "1");
}

TEST_F(VirtualGamepadRecenterMigrationTest, WhenTheStickClickIsAlreadyOnRecenterAlongsideRsOnlyRsGoes) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(
        "StubInput.add('VR_RECENTER_VIEW', '3_XINPUT_GAMEPAD', 'BUTTON_12', 1)"));
    ASSERT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4 ; BUTTON_12/+@1");
    ASSERT_NO_FATAL_FAILURE(StartNextSession());
    PlayFor(3);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4") << "no second BUTTON_11";
    EXPECT_EQ(Lua("StubInput.listActive('VR_RECENTER_VIEW', '3_XINPUT_GAMEPAD')"), "BUTTON_11/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "1");
}

TEST_F(VirtualGamepadRecenterMigrationTest, ItWaitsUntilTheGameHasTheVirtualGamepadAttachedAndLeavesAnOldPadsBindingsMeanwhile) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(RecenterOnRs(kPad)));
    ASSERT_NO_FATAL_FAILURE(StartNextSession("unplugged = { '3_XINPUT_GAMEPAD' }"));
    const std::string oldPad = Dump(kPad);
    PlayFor(3);
    EXPECT_EQ(Dump(kPad), oldPad);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_12/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_NE(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true");
    EXPECT_EQ(LogLinesContaining("[Warning] Virtual Gamepad"), 0) << Logs();

    ASSERT_EQ(RunLuauChunk(L, "StubInput.plug('3_XINPUT_GAMEPAD')", "=plug"), 0);
    PlayFor(2);
    EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4");
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true");
}

class VirtualGamepadRecenterMigrationFallbackTest : public VirtualGamepadRecenterMigrationTest,
                                                    public ::testing::WithParamInterface<const char*> {};

// The binding API missing or raising: one warning, no flag, and the bindings (and the saved file) as the game had them.
TEST_P(VirtualGamepadRecenterMigrationFallbackTest, GivesOneWarningNoFlagAndUnchangedBindings) {
    ASSERT_NO_FATAL_FAILURE(LeaveProfileOfAnEarlierBuild(RecenterOnRs(kPad)));
    const std::string before = Lua("StubInput.dump(nil)");
    const std::string activeBefore = Lua("StubInput.dumpActive(nil)");

    ASSERT_NO_FATAL_FAILURE(StartNextSession(std::string("variant = '") + GetParam() + "'"));
    PlayFor(5);

    if (Lua("g_inputBinding ~= nil") == "true") {
        EXPECT_EQ(Lua("StubInput.dump(nil)"), before) << "the bindings are as the game had them";
        EXPECT_EQ(Lua("StubInput.dumpActive(nil)"), activeBefore) << "and so are the ones the game acts on";
        EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_12/+@4");
    }
    EXPECT_EQ(Lua("StubInput.saves"), "0") << "and nothing was saved";
    EXPECT_NE(Lua("savedXml['vrSettings.virtualGamepadRecenterMigrated']"), "true") << "the flag stays unset";
    EXPECT_EQ(LogLinesContaining("[Warning] Virtual Gamepad Recenter binding not migrated"), 1) << "one warning for the session";
    EXPECT_EQ(LogLinesContaining("Recenter binding migrated"), 0);
}

INSTANTIATE_TEST_SUITE_P(BindingApi, VirtualGamepadRecenterMigrationFallbackTest,
    ::testing::Values("missing", "reshaped", "saveRaises", "noReset", "resetRaises"));

TEST_F(VirtualGamepadDefaultBindingsTest, SnapTurnLeftAndRightLandOnTheTwoDifferentHalvesOfTheRightStick) {
    // The game's Binding.new takes the sign of an axis input from its fourth argument alone (a "-" in the name is dropped), so
    // Snap Turn left must be asked for as a negative AXIS_3 with the axisComponent "+" the game saves for a half-axis action.
    ASSERT_NO_FATAL_FAILURE(StartGame());
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "AXIS_3-/+@4") << "the left half, not the right one";
    EXPECT_EQ(List("VR_SNAP_TURN_RIGHT", kPad), "AXIS_3+/+@4");
    EXPECT_EQ(Lua("StubInput.get('VR_SNAP_TURN_LEFT', '3_XINPUT_GAMEPAD').inputComponent"), "-");
    EXPECT_EQ(Lua("StubInput.get('VR_SNAP_TURN_RIGHT', '3_XINPUT_GAMEPAD').inputComponent"), "+");
}

TEST_F(VirtualGamepadDefaultBindingsTest, ABindingTakenOffTheVirtualGamepadStopsActingInTheGameAndARealPadsKeepsActing) {
    // The Controls screen shows action.bindings, the game acts on action.activeBindings: both must lose the smooth look.
    ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD', '3_XINPUT_GAMEPAD' } }", "", false));
    ASSERT_EQ(Lua("StubInput.listActive('AXIS_LOOK_LEFTRIGHT_PLAYER', '3_XINPUT_GAMEPAD')"), "AXIS_3+/+@4 ; AXIS_3-/-@4");
    TurnOn();

    EXPECT_EQ(Lua("StubInput.listActive('AXIS_LOOK_LEFTRIGHT_PLAYER', '3_XINPUT_GAMEPAD')"), "") << "no smooth look on the pad on foot";
    EXPECT_EQ(Lua("StubInput.listActive('AXIS_LOOK_LEFTRIGHT_VEHICLE', '3_XINPUT_GAMEPAD')"), "") << "nor in vehicles";
    EXPECT_EQ(Lua("StubInput.listActive('VR_SNAP_TURN_LEFT', '3_XINPUT_GAMEPAD')"), "AXIS_3-/+@4") << "and what was given acts";
    EXPECT_EQ(Lua("StubInput.listActive('VR_SNAP_TURN_RIGHT', '3_XINPUT_GAMEPAD')"), "AXIS_3+/+@4");
    EXPECT_EQ(Lua("StubInput.listActive('VR_SNAP_TURN_LEFT', '0_XINPUT_GAMEPAD')"), "BUTTON_5/+@1") << "a real pad is as it was";
    EXPECT_EQ(Lua("StubInput.listActive('AXIS_LOOK_LEFTRIGHT_PLAYER', '0_XINPUT_GAMEPAD')"), "AXIS_3+/+@1 ; AXIS_3-/-@1");
}

TEST_F(VirtualGamepadDefaultBindingsTest, ARealPadsBindingsAndEveryOtherDevicesAreNeverTouched) {
    ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD', '1_XINPUT_GAMEPAD', '3_XINPUT_GAMEPAD' } }", "", false));
    const std::string realPad = Dump(kRealPad);
    const std::string unpluggedPad = Dump("1_XINPUT_GAMEPAD");
    const std::string keyboard = Dump("KB_MOUSE_DEFAULT");
    ASSERT_NE(realPad.find("VR_SNAP_TURN_LEFT"), std::string::npos);
    const std::string virtualPadBefore = Dump(kPad);

    TurnOn();

    ASSERT_NE(Dump(kPad), virtualPadBefore) << "the Virtual Gamepad was rewritten";
    EXPECT_EQ(Dump(kRealPad), realPad);
    EXPECT_EQ(Dump("1_XINPUT_GAMEPAD"), unpluggedPad) << "also a pad that is not plugged in";
    EXPECT_EQ(Dump("KB_MOUSE_DEFAULT"), keyboard);
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kRealPad), "BUTTON_5/+@1") << "a real pad keeps Snap Turn on LB";
    EXPECT_EQ(List("AXIS_LOOK_LEFTRIGHT_PLAYER", kRealPad), "AXIS_3+/+@1 ; AXIS_3-/-@1") << "and smooth look on the right stick";
}

TEST_F(VirtualGamepadDefaultBindingsTest, ALaterSessionDoesNotRewriteAgain) {
    ASSERT_NO_FATAL_FAILURE(StartGame());
    const std::string rewritten = Dump(kPad);
    ASSERT_EQ(Lua("StubInput.saves"), "1");

    // The game comes back with the bindings it saved, and a player who changed nothing.
    ASSERT_NO_FATAL_FAILURE(RestartGameSession());
    PlayFor(3);

    EXPECT_EQ(Dump(kPad), rewritten);
    EXPECT_EQ(Lua("StubInput.saves"), "0") << "nothing was saved again";
    EXPECT_EQ(LogLinesContaining("default bindings written"), 0);
}

TEST_F(VirtualGamepadDefaultBindingsTest, AChangeThePlayerMadeToARewrittenBindingSticksThroughLaterSessions) {
    ASSERT_NO_FATAL_FAILURE(StartGame());
    ASSERT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "AXIS_3-/+@4");

    // In the Controls screen the player moves Snap Turn left to the right hand's upper face button and puts smooth look back.
    ASSERT_EQ(RunLuauChunk(L, "StubInput.change('VR_SNAP_TURN_LEFT', '3_XINPUT_GAMEPAD', 'AXIS_3-', 'BUTTON_4')", "=playerChange"), 0);
    PlayFor(3);
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "BUTTON_4/+@4") << "not rewritten later in the same session";
    EXPECT_EQ(Lua("StubInput.saves"), "1");

    ASSERT_NO_FATAL_FAILURE(RestartGameSession());
    PlayFor(3);
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "BUTTON_4/+@4") << "nor in the next";
    EXPECT_EQ(List("VR_SNAP_TURN_RIGHT", kPad), "AXIS_3+/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "0");

    // Not even when the pad is taken off and put back on.
    EvalLua(L, "runCommand('vrVirtualGamepad', 'off')");
    Frames(5);
    TurnOn();
    PlayFor(3);
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "BUTTON_4/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "0");
}

TEST_F(VirtualGamepadDefaultBindingsTest, WithTheVirtualGamepadOffNothingIsRewrittenAndNothingIsFlagged) {
    ASSERT_NO_FATAL_FAILURE(StartGame("{}", "", false));
    const std::string before = Lua("StubInput.dump(nil)");
    PlayFor(3);
    EXPECT_EQ(Lua("StubInput.dump(nil)"), before);
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_NE(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true");
}

TEST_F(VirtualGamepadDefaultBindingsTest, ItWaitsQuietlyUntilTheGameListsTheDeviceAndThenRewritesIt) {
    ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD' } }"));
    const std::string before = Lua("StubInput.dump(nil)");
    PlayFor(3);
    EXPECT_EQ(Lua("StubInput.dump(nil)"), before) << "no bindings of the Virtual Gamepad's device to rewrite yet";
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_NE(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true");
    EXPECT_EQ(LogLinesContaining("[Warning] Virtual Gamepad"), 0) << "waiting is not a failure";
    EXPECT_EQ(LogLinesContaining("waiting for the game to list 3_XINPUT_GAMEPAD"), 1) << "and the log says so, once";

    ASSERT_EQ(RunLuauChunk(L, "StubInput.addDevice('3_XINPUT_GAMEPAD')", "=addDevice"), 0);
    PlayFor(2);
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "AXIS_3-/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "1");
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true");
}

TEST_F(VirtualGamepadDefaultBindingsTest, BindingsOfAnOldPadThatUsedTheSameSlotAreNotRewrittenUntilTheGameHasThePadAttached) {
    // An unplugged pad once sat in slot 3: the game still lists its saved bindings (and the Companion Mod's defaults on them),
    // but there is no connected device behind them yet.
    ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD', '3_XINPUT_GAMEPAD' }, unplugged = { '3_XINPUT_GAMEPAD' } }"));
    ASSERT_EQ(Lua("select(2, VRBridge.getVirtualGamepad())"), "3");
    const std::string oldPad = Dump(kPad);
    PlayFor(3);

    EXPECT_EQ(Dump(kPad), oldPad) << "the old pad's saved bindings are left alone";
    EXPECT_EQ(Lua("StubInput.saves"), "0");
    EXPECT_NE(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true") << "and no flag is set for a device the game has not attached";

    // The game attaches the Virtual Gamepad as that device.
    ASSERT_EQ(RunLuauChunk(L, "StubInput.plug('3_XINPUT_GAMEPAD')", "=plug"), 0);
    PlayFor(2);
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "AXIS_3-/+@4");
    EXPECT_EQ(Lua("StubInput.saves"), "1");
    EXPECT_EQ(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true");
}

TEST_F(VirtualGamepadDefaultBindingsTest, ANewBindingTakesAFreeIndexSlotWhenThereIsNoBindingToReplace) {
    // The pad has no VR View Toggle binding of the game's defaults left, and an alternative of the player's in slot 1.
    ASSERT_NO_FATAL_FAILURE(StartGame(
        "{ omit = { 'VR_TOGGLE_VIEW@3_XINPUT_GAMEPAD' }, extra = { { action = 'VR_TOGGLE_VIEW', device = '3_XINPUT_GAMEPAD', input = 'BUTTON_2', index = 1 } } }"));
    EXPECT_EQ(List("VR_TOGGLE_VIEW", kPad), "BUTTON_11 BUTTON_10/+@2 ; BUTTON_2/+@1") << "the first slot the device has free on the action";
    EXPECT_EQ(List("VR_SNAP_TURN_LEFT", kPad), "AXIS_3-/+@4") << "a replaced binding hands its slot on";
    EXPECT_EQ(Lua("StubInput.saves"), "1");
}

TEST_F(VirtualGamepadDefaultBindingsTest, TheStatusCommandSaysWhetherTheDefaultBindingsWereWritten) {
    ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD' } }"));
    EXPECT_NE(Lua("runCommand('vrMotionControllerStatus')").find("default bindings not written yet"), std::string::npos)
        << Lua("runCommand('vrMotionControllerStatus')");
    ASSERT_EQ(RunLuauChunk(L, "StubInput.addDevice('3_XINPUT_GAMEPAD')", "=addDevice"), 0);
    PlayFor(2);
    EXPECT_NE(Lua("runCommand('vrMotionControllerStatus')").find("default bindings written"), std::string::npos)
        << Lua("runCommand('vrMotionControllerStatus')");
}

class VirtualGamepadDefaultBindingsFallbackTest : public VirtualGamepadDefaultBindingsTest,
                                                  public ::testing::WithParamInterface<const char*> {};

// The binding API missing or raising: one warning, no flag, and the bindings (and the saved file) as the game had them.
TEST_P(VirtualGamepadDefaultBindingsFallbackTest, GivesOneWarningNoFlagAndUnchangedBindings) {
    ASSERT_NO_FATAL_FAILURE(StartGame(std::string("{ variant = '") + GetParam() + "' }", "", false));
    const bool hasBindings = Lua("g_inputBinding ~= nil") == "true";
    const std::string before = hasBindings ? Lua("StubInput.dump(nil)") : "";
    const std::string activeBefore = hasBindings ? Lua("StubInput.dumpActive(nil)") : "";

    TurnOn();
    PlayFor(5);

    if (hasBindings) EXPECT_EQ(Lua("StubInput.dump(nil)"), before) << "the bindings are as the game had them";
    if (hasBindings) EXPECT_EQ(Lua("StubInput.dumpActive(nil)"), activeBefore) << "and so are the ones the game acts on";
    EXPECT_EQ(Lua("StubInput.saves"), "0") << "and nothing was saved";
    EXPECT_NE(Lua("savedXml['vrSettings.virtualGamepadDefaultsWritten']"), "true") << "the flag stays unset";
    EXPECT_EQ(LogLinesContaining("[Warning] Virtual Gamepad default bindings not written"), 1) << "one warning for the session";
    EXPECT_EQ(LogLinesContaining("default bindings written"), 0);
    EXPECT_NE(Lua("runCommand('vrMotionControllerStatus')").find("default bindings not written"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(BindingApi, VirtualGamepadDefaultBindingsFallbackTest,
    ::testing::Values("missing", "reshaped", "raises", "saveRaises", "noReset", "resetRaises"));
