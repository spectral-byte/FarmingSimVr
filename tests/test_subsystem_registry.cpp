// The Companion Mod's subsystem registry (spec #244, ticket #256): a subsystem registers itself by name when its file loads, with
// the hooks it needs (map load, map unload, per-frame update) and the console commands it owns, and the core runs them. Seen from
// outside: the real Companion Mod is loaded in modDesc order, a subsystem is registered the way a file would, and the test drives
// the mod as the game does (map load, frames, map unload) and asserts what ran, in which order, which console commands the game
// was given and what log.txt says. Never the registry's contents.
// Prior art: test_hand_tools_look_ray.cpp (a look ray aimer that raises is logged once and the next one is asked),
// test_companion_mod.cpp EveryConsoleCommandIsRemovedOnUnloadSoASecondMapLoadsCleanly (the engine's console command table).

#include "companion_mod_fixture.h"
#include <gtest/gtest-spi.h>
#include <string>

namespace {

// The game's console command table (a name can only be added while it is free) and the few engine functions a map load calls.
const char* kEngineStubs = R"luau(
    _G.present = {}
    _G.handlers = {}
    _G.duplicates = {}
    _G.addConsoleCommand = function(cmd, desc, funcName, target)
        if _G.present[cmd] then table.insert(_G.duplicates, cmd) end
        _G.present[cmd] = true
        _G.handlers[cmd] = { desc = desc, funcName = funcName, target = target }
    end
    _G.removeConsoleCommand = function(cmd) _G.present[cmd] = nil end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function() end
    _G.setXMLBool = function() end
    _G.setXMLString = function() end
    _G.saveXMLFile = function() return true end
    _G.delete = function() end
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.events = {}
)luau";

int CountLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

// A fresh Lua state with the mod loaded and a subsystem whose update hook raises, driven for one update with no expectation of
// the error. Static and self-contained so that EXPECT_NONFATAL_FAILURE can run it.
void UpdateOnceWithARaisingHookAndNoExpectation() {
    lua_State* state = luaL_newstate();
    luaL_openlibs(state);
    lua_pushcclosure(state, &MockPrint, "print", 0);
    lua_setglobal(state, "print");
    LoadCompanionMod(state);
    RunLuauChunk(state, R"luau(
        VRCompanion:registerSubsystem("boom", { update = function(self) error("kaput") end })
        VRCompanion:update(16.667)
    )luau", "=raisingHookWithoutExpectation");
    lua_close(state);
}

class SubsystemRegistryTest : public CompanionModTest {
protected:
    void SetUp() override {
        CompanionModTest::SetUp();
        ASSERT_TRUE(bridge_.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kEngineStubs, "=subsystemRegistryStubs"), 0);
    }

    fs25vr::VRBridge bridge_{nullptr, nullptr};
};

}  // namespace

TEST_F(SubsystemRegistryTest, HooksRunAtMapLoadOnEveryUpdateAndAtMapUnloadAndNothingRunsBeforeThat) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local function subsystem(name)
            return {
                mapLoad = function(self) table.insert(events, name .. ":mapLoad") end,
                update = function(self, dt) table.insert(events, name .. ":update") end,
                mapUnload = function(self) table.insert(events, name .. ":mapUnload") end,
            }
        end
        VRCompanion:registerSubsystem("first", subsystem("first"))
        VRCompanion:registerSubsystem("second", subsystem("second"))
        assert(#events == 0, "registering runs nothing: " .. table.concat(events, ","))
    )luau", "=registerSubsystems"), 0);

    ASSERT_EQ(RunLuauChunk(L, R"luau(VRCompanion:loadMap("testMap"))luau", "=loadMap"), 0);
    EXPECT_EQ(EvalLua(L, "table.concat(events, ',')"), "first:mapLoad,second:mapLoad");

    NextFrame();
    NextFrame();
    EXPECT_EQ(EvalLua(L, "table.concat(events, ',')"),
        "first:mapLoad,second:mapLoad,first:update,second:update,first:update,second:update");

    ASSERT_EQ(RunLuauChunk(L, "events = {}; VRCompanion:deleteMap()", "=deleteMap"), 0);
    EXPECT_EQ(EvalLua(L, "table.concat(events, ',')"), "second:mapUnload,first:mapUnload");
}

TEST_F(SubsystemRegistryTest, MapLoadHooksRunAfterTheHandshakeAndTheConfigurationAreLoadedAndReceiveTheCompanionMod) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:registerSubsystem("probe", {
            mapLoad = function(self)
                _G.sawSelf = (self == VRCompanion)
                _G.sawHandshake = VRCompanion.handshakeComplete
                _G.sawGuiPolled = VRCompanion.guiVisibilityInitialized
            end,
            update = function(self, dt) _G.sawDt = dt end,
        })
        VRCompanion:loadMap("testMap")
        VRCompanion:update(33)
    )luau", "=probe"), 0);
    EXPECT_EQ(EvalLua(L, "sawSelf"), "true");
    EXPECT_EQ(EvalLua(L, "sawHandshake"), "true") << "the core's handshake is done before the subsystems load";
    EXPECT_EQ(EvalLua(L, "sawGuiPolled"), "true") << "the core's own map load steps come first";
    EXPECT_EQ(EvalLua(L, "sawDt"), "33");
}

TEST_F(SubsystemRegistryTest, ARegisteredSubsystemsConsoleCommandsExistAfterMapLoadAndAreGoneAfterMapUnload) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion.consoleCommandProbeOne = function(self, arg) return "one:" .. tostring(arg) end
        VRCompanion.consoleCommandProbeTwo = function(self) return "two" end
        VRCompanion:registerSubsystem("probe", {
            consoleCommands = {
                { name = "vrProbeOne", description = "First probe: vrProbeOne [x]", method = "consoleCommandProbeOne" },
                { name = "vrProbeTwo", description = "Second probe", method = "consoleCommandProbeTwo" },
            },
        })
        assert(not present.vrProbeOne, "no command before the map loads")
        VRCompanion:loadMap("testMap")
        assert(present.vrProbeOne and present.vrProbeTwo, "both commands exist after map load")
        local entry = handlers.vrProbeOne
        assert(entry.desc == "First probe: vrProbeOne [x]", "the description is passed on: " .. tostring(entry.desc))
        assert(entry.funcName == "consoleCommandProbeOne", "the method name is passed on unchanged")
        assert(entry.target == VRCompanion, "the command runs on the Companion Mod")
        assert(entry.target[entry.funcName](entry.target, "a") == "one:a")

        VRCompanion:deleteMap()
        assert(not present.vrProbeOne and not present.vrProbeTwo, "both commands are gone after map unload")

        VRCompanion:loadMap("secondMap")
        assert(#duplicates == 0, "a second map load adds no command twice: " .. table.concat(duplicates, ","))
        assert(present.vrProbeOne and present.vrProbeTwo, "both commands exist again on the second map")
        VRCompanion:deleteMap()
        assert(not present.vrProbeOne, "and are gone again")
    )luau", "=consoleCommands"), 0);
}

TEST_F(SubsystemRegistryTest, ASubsystemThatIsRegisteredAgainKeepsItsPlaceAndTheNewHooksReplaceTheOld) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:registerSubsystem("first", { update = function() table.insert(events, "first:old") end })
        VRCompanion:registerSubsystem("second", { update = function() table.insert(events, "second") end })
        VRCompanion:registerSubsystem("first", { update = function() table.insert(events, "first:new") end })
        VRCompanion:loadMap("testMap")
        VRCompanion:update(16)
        assert(table.concat(events, ",") == "first:new,second", table.concat(events, ","))
    )luau", "=reRegister"), 0);
}

TEST_F(SubsystemRegistryTest, ASubsystemRegisteredAgainWithoutAnUpdateHookStopsUpdatingAndOneWithAnEarlyHookGainsIt) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:registerSubsystem("first", { update = function() table.insert(events, "first:update") end })
        VRCompanion:registerSubsystem("second", { update = function() table.insert(events, "second:update") end })
        VRCompanion:registerSubsystem("first", { mapLoad = function() end })
        VRCompanion:registerSubsystem("third", { updateEarly = function() table.insert(events, "third:early") end })
        VRCompanion:registerSubsystem("second", {
            updateEarly = function() table.insert(events, "second:early") end,
            update = function() table.insert(events, "second:update") end,
        })
        VRCompanion:loadMap("testMap")
        VRCompanion:update(16)
        assert(table.concat(events, ",") == "second:early,third:early,second:update", table.concat(events, ","))
    )luau", "=reRegisterUpdateHooks"), 0);
}

TEST_F(SubsystemRegistryTest, HooksAreCalledWithTheCompanionModTheRunPointWasCalledOn) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local other = setmetatable({}, { __index = VRCompanion })
        local seen = {}
        VRCompanion:registerSubsystem("probe", {
            mapLoad = function(self) seen.mapLoad = self end,
            updateEarly = function(self) seen.updateEarly = self end,
            update = function(self) seen.update = self end,
            mapUnload = function(self) seen.mapUnload = self end,
            consoleCommands = { { name = "vrSelfProbe", description = "probe", method = "consoleCommandSelfProbe" } },
        })
        other:runSubsystemMapLoad()
        other:runSubsystemUpdateEarly(1)
        other:runSubsystemUpdate(1)
        other:addSubsystemConsoleCommands()
        other:runSubsystemMapUnload()
        for _, hook in ipairs({ "mapLoad", "updateEarly", "update", "mapUnload" }) do
            assert(seen[hook] == other, hook .. " is called with the table the run point was called on")
        end
        assert(handlers.vrSelfProbe.target == other, "and so are the console commands")
    )luau", "=hookSelf"), 0);
}

TEST_F(SubsystemRegistryTest, ConsoleCommandsThatAreNotWellFormedRaiseWhenTheSubsystemRegistersNotWhenTheMapLoads) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local function failsWith(commands, expected)
            local ok, err = pcall(function() VRCompanion:registerSubsystem("probe", { consoleCommands = commands }) end)
            assert(not ok, "registering must raise")
            assert(string.find(tostring(err), expected, 1, true), "the message names " .. expected .. ": " .. tostring(err))
            assert(string.find(tostring(err), "probe", 1, true), "and the subsystem: " .. tostring(err))
        end
        failsWith("vrX", "consoleCommands")
        failsWith({ "vrX" }, "consoleCommands")
        failsWith({ { description = "d", method = "consoleCommandX" } }, "name")
        failsWith({ { name = "", description = "d", method = "consoleCommandX" } }, "name")
        failsWith({ { name = "vrX", description = "d" } }, "method")
        failsWith({ { name = "vrX", description = "d", method = 5 } }, "method")
        failsWith({ { name = "vrX", method = "consoleCommandX" } }, "description")
        failsWith({ { name = "vrX", description = "d", method = "consoleCommandX" }, { name = "vrY", description = "d" } }, "method")
        -- A well formed list is accepted, and a bad one left nothing behind: the map loads
        VRCompanion:registerSubsystem("probe", { consoleCommands = { { name = "vrX", description = "d", method = "consoleCommandX" } } })
        VRCompanion:loadMap("testMap")
        assert(present.vrX and not present.vrY)
    )luau", "=consoleCommandValidation"), 0);
}

// The order of a map's update: the registry moved the subsystems' updates into hooks, but the steps the core runs between them
// keep their places (spec #244: the order of effects the game and the Injector observe stays).
TEST_F(SubsystemRegistryTest, AnUpdateRunsTheCoresStepsAndTheSubsystemsInTheOrderTheyRanBeforeTheRegistry) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local order = {}
        local function record(name)
            local original = VRCompanion[name]
            VRCompanion[name] = function(self, ...)
                table.insert(order, name)
                return original(self, ...)
            end
        end
        for _, name in ipairs({ "refreshPerfTracing", "updateFishCatch", "pollGuiVisibility", "updateMenuBackgroundCamera",
                "updateHudConvergence", "registerActionEvents", "onVehicleTransition", "updateMotionControllers",
                "applyDlssNearClip" }) do
            record(name)
        end
        -- The menu shows (so the background camera follows the head) and a trace is running
        _G.g_gui = { getIsGuiVisible = function() return true end }
        VRCompanion:loadMap("testMap")
        VRCompanion.perfTracing = true
        VRCompanion:update(16)
        order = {}

        -- The frame the controlled vehicle changes
        _G.g_currentMission = { controlledVehicle = {} }
        VRCompanion.perfTracing = true
        VRCompanion:update(16)
        _G.vehicleFrameOrder = table.concat(order, ",")
    )luau", "=updateOrder"), 0);
    EXPECT_EQ(EvalLua(L, "vehicleFrameOrder"),
        "refreshPerfTracing,updateFishCatch,pollGuiVisibility,updateMenuBackgroundCamera,updateHudConvergence,"
        "registerActionEvents,onVehicleTransition,registerActionEvents,updateMotionControllers,applyDlssNearClip");
}

TEST_F(SubsystemRegistryTest, AHookThatRaisesIsLoggedOnceWithTheSubsystemAndHookNamesAndTheOthersStillRun) {
    ExpectHookErrors();
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:registerSubsystem("alpha", {
            mapLoad = function() error("alpha load failed") end,
            update = function() error("alpha update failed") end,
            mapUnload = function() error("alpha unload failed") end,
        })
        VRCompanion:registerSubsystem("beta", {
            mapLoad = function() table.insert(events, "beta:mapLoad") end,
            update = function() table.insert(events, "beta:update") end,
            mapUnload = function() table.insert(events, "beta:mapUnload") end,
        })
        VRCompanion:registerSubsystem("gamma", {
            update = function() error("gamma update failed") end,
        })
        VRCompanion:loadMap("testMap")
    )luau", "=raisingHooksLoad"), 0);

    for (int i = 0; i < 30; ++i) NextFrame();
    ASSERT_EQ(RunLuauChunk(L, "VRCompanion:deleteMap()", "=raisingHooksUnload"), 0);

    EXPECT_EQ(CountLines("[FS25_VR] [Error] Subsystem 'alpha' mapLoad hook raised: "), 1);
    EXPECT_EQ(CountLines("[FS25_VR] [Error] Subsystem 'alpha' update hook raised: "), 1) << "30 frames, one line";
    EXPECT_EQ(CountLines("[FS25_VR] [Error] Subsystem 'alpha' mapUnload hook raised: "), 1);
    EXPECT_EQ(CountLines("[FS25_VR] [Error] Subsystem 'gamma' update hook raised: "), 1) << "once for each subsystem";
    EXPECT_EQ(CountLines("alpha update failed"), 1) << "the error text is in the line";
    EXPECT_EQ(CountLines("Subsystem 'beta'"), 0) << "a subsystem that works is not reported";

    // Everything after the one that raised still ran, every time
    EXPECT_EQ(EvalLua(L, "events[1]"), "beta:mapLoad");
    EXPECT_EQ(EvalLua(L, "#events"), "32") << "map load, 30 updates, map unload";
    EXPECT_EQ(EvalLua(L, "events[32]"), "beta:mapUnload");

    // A second map does not log the same hook again
    const size_t before = s_capturedLogs.size();
    ASSERT_EQ(RunLuauChunk(L, R"luau(VRCompanion:loadMap("again"); VRCompanion:update(16); VRCompanion:deleteMap())luau", "=raisingHooksAgain"), 0);
    int errorsOnSecondMap = 0;
    for (size_t i = before; i < s_capturedLogs.size(); ++i) {
        if (IsHookErrorLine(s_capturedLogs[i])) ++errorsOnSecondMap;
    }
    EXPECT_EQ(errorsOnSecondMap, 0);
}

TEST_F(SubsystemRegistryTest, ARaisingHookWhoseErrorTheTestDoesNotExpectFailsTheTest) {
    EXPECT_NONFATAL_FAILURE(UpdateOnceWithARaisingHookAndNoExpectation(),
        "unexpected Companion Mod hook error: [FS25_VR] [Error] Subsystem 'boom' update hook raised: ");
}

TEST_F(SubsystemRegistryTest, DesktopMirrorsCommandExistsAfterMapLoadIsGoneAfterMapUnloadAndTheSavedSettingReachesVRBridge) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local store = { ["vrSettings.desktopMirror"] = false }
        _G.loadXMLFile = function() return 2 end
        _G.getXMLFloat = function() return nil end
        _G.getXMLString = function() return nil end
        _G.getXMLBool = function(xml, key) return store[key] end

        assert(not present.vrDesktopMirror, "no command before the map loads")
        VRCompanion:loadMap("testMap")
        assert(present.vrDesktopMirror, "vrDesktopMirror exists after map load")
        assert(handlers.vrDesktopMirror.funcName == "consoleCommandSetDesktopMirror")
        assert(handlers.vrDesktopMirror.target == VRCompanion)
        assert(VRBridge.getDesktopMirror() == false, "the saved off reaches VRBridge")

        VRCompanion:deleteMap()
        assert(not present.vrDesktopMirror, "vrDesktopMirror is gone after map unload")
    )luau", "=desktopMirrorThroughTheRegistry"), 0);
}

// Every console command the mod adds, subsystem by subsystem and in the core: the whole set exists once a map has loaded and not
// one is left once it has unloaded, so a subsystem that forgets to remove (or to add) a command fails here. The names are written
// out on purpose: a command added or dropped is a decision, and this list is where it shows. Ticket #261.
TEST_F(SubsystemRegistryTest, EveryConsoleCommandTheModAddsExistsAfterMapLoadAndNoneRemainsAfterMapUnload) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local expected = {
            -- the core
            "vrHudSafeArea", "vrHudGroup", "vrHudDepth", "vrHudTrace", "vrHudEyeTest", "vrEyeSnapshot", "vrHudState",
            "vrHudLoadNotification", "vrHudLoadAlert", "vrRecenterMode", "vrRecenterTracking", "vrSnapTurnAngle", "vr2DView",
            "vrToggleView", "vrDlssMvScale", "vrDlssSharpness", "vrDlssPreset", "vrDlssSdpMvScale", "vrDlssMvFix",
            "vrDlssDepthModel", "vrDlssNearClip", "vrDlssNearClipCab", "vrDlssNearClipOutside", "vrDlssNearClipOnFoot",
            "vrDlssDiag", "vrRenderingMode", "vrHudGroupShow",
            -- the subsystems
            "vrDesktopMirror", "vrFovOverscan", "vrMenuDistance", "vrMouseConfinement", "vrPerfTrace", "vrSeatOffset",
            "vrFishCatch", "vrDialogDiagnostics", "vrDialogDepth", "vrDualPassPhysicsPause", "vrEyePairTrace",
            "vrMotionControllers", "vrMotionControllerStatus", "vrPointerCone", "vrPointerDiagnostics",
            "vrVirtualHandsGeometry", "vrVirtualHandsDiagnostics", "vrVirtualHandsTrace", "vrVirtualGamepad",
            "vrHandTools", "vrHandToolsDiagnostics", "vrHandToolOffset", "vrToolHand", "vrChainsawReach",
            "vrObjectGrab", "vrObjectGrabDiagnostics", "vrWheelGrab", "vrWheelGrabDiagnostics",
        }
        local function names()
            local list = {}
            for name in pairs(present) do table.insert(list, name) end
            table.sort(list)
            return table.concat(list, ",")
        end
        local sortedExpected = {}
        for _, name in ipairs(expected) do table.insert(sortedExpected, name) end
        table.sort(sortedExpected)
        local expectedText = table.concat(sortedExpected, ",")

        assert(names() == "", "no command before the map loads: " .. names())
        for map = 1, 2 do
            VRCompanion:loadMap("map" .. map)
            assert(names() == expectedText, "after map load " .. map .. " the commands are not the expected ones:\n" .. names() .. "\n" .. expectedText)
            for _, name in ipairs(expected) do
                local entry = handlers[name]
                assert(entry.target == VRCompanion, name .. " runs on the Companion Mod")
                assert(type(entry.target[entry.funcName]) == "function", name .. " names a method that does not exist: " .. tostring(entry.funcName))
                assert(type(entry.desc) == "string" and entry.desc ~= "", name .. " has a description")
            end
            VRCompanion:deleteMap()
            assert(names() == "", "commands left after map unload " .. map .. ": " .. names())
        end
        assert(#duplicates == 0, "no command is added while it is present: " .. table.concat(duplicates, ","))
    )luau", "=everyConsoleCommand"), 0);
}

// The standalone Luau runner has the same rule: a hook error in the log fails the run unless the chunk expects it.
TEST(SubsystemRegistryLuauRunnerTest, ALoggedHookErrorFailsTheRunUnlessTheChunkExpectsIt) {
    LuauTestRunner runner;
    const char* line = "print(\"[FS25_VR] [Error] Subsystem 'boom' update hook raised: kaput\")";

    LuauExecutionResult unexpected = runner.ExecuteCode(line, "=unexpectedHookError");
    EXPECT_FALSE(unexpected.success);
    EXPECT_NE(unexpected.errorMessage.find("Subsystem 'boom' update hook raised"), std::string::npos) << unexpected.errorMessage;

    LuauExecutionResult expected = runner.ExecuteCode(std::string("expectHookError(\"Subsystem 'boom' update\")\n") + line,
        "=expectedHookError");
    EXPECT_TRUE(expected.success) << expected.errorMessage;

    LuauExecutionResult otherSubsystem = runner.ExecuteCode(
        std::string("expectHookError(\"Subsystem 'other'\")\n") + line, "=otherHookError");
    EXPECT_FALSE(otherSubsystem.success) << "an expectation covers only the lines it names";

    LuauExecutionResult warning = runner.ExecuteCode("print(\"[FS25_VR] [Warning] Look ray aimer 'x' raised: y\")", "=warning");
    EXPECT_TRUE(warning.success) << "a warning is not a hook error";

    LuauExecutionResult fresh = runner.ExecuteCode("print(\"nothing\")", "=fresh");
    EXPECT_TRUE(fresh.success) << "the expectation of an earlier run does not carry over";
}
