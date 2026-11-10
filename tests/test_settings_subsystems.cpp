// The settings and diagnostics subsystems on the subsystem registry (spec #244, ticket #257): FOV Overscan, Menu Distance, Mouse
// Confinement, the performance trace, Seat Offset, the fish catch and the dialog diagnostics. Seen from outside: the real
// Companion Mod is loaded in modDesc order and driven as the game does (map load, map unload), and the test asserts what the game
// was given: which console commands exist after a map loads and after it unloads, and which saved values VRBridge was sent, in
// which order, after the handshake. Never the registry's contents.
// Prior art: test_subsystem_registry.cpp (Desktop Mirror through the registry), test_companion_mod.cpp
// EveryConsoleCommandIsRemovedOnUnloadSoASecondMapLoadsCleanly.

#include "companion_mod_fixture.h"
#include <string>

namespace {

// The game's console command table (a name can only be added while it is free), the settings file as a table of saved values
// (`saved`) and the few engine functions a map load calls.
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
    _G.saved = {}
    _G.createFolder = function() return true end
    _G.createXMLFile = function() return 1 end
    _G.setXMLFloat = function(file, key, value) _G.saved[key] = value end
    _G.setXMLBool = function(file, key, value) _G.saved[key] = value end
    _G.setXMLString = function(file, key, value) _G.saved[key] = value end
    _G.saveXMLFile = function() return true end
    _G.loadXMLFile = function() return 2 end
    _G.delete = function() end
    _G.getXMLFloat = function(file, key) local v = _G.saved[key]; return type(v) == "number" and v or nil end
    _G.getXMLBool = function(file, key) local v = _G.saved[key]; if type(v) == "boolean" then return v end return nil end
    _G.getXMLString = function(file, key) local v = _G.saved[key]; return type(v) == "string" and v or nil end
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
)luau";

class SettingsSubsystemsTest : public CompanionModTest {
protected:
    void SetUp() override {
        CompanionModTest::SetUp();
        ASSERT_TRUE(bridge_.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kEngineStubs, "=settingsSubsystemsStubs"), 0);
    }

    fs25vr::VRBridge bridge_{nullptr, nullptr};
};

}  // namespace

TEST_F(SettingsSubsystemsTest, EachConsoleCommandExistsAfterMapLoadIsGoneAfterMapUnloadAndBackOnTheNextMap) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local commands = {
            vrFovOverscan = "consoleCommandSetFovOverscan",
            vrMenuDistance = "consoleCommandSetMenuDistance",
            vrMouseConfinement = "consoleCommandSetMouseConfinement",
            vrPerfTrace = "consoleCommandPerfTrace",
            vrSeatOffset = "consoleCommandSeatOffset",
            vrFishCatch = "consoleCommandSetFishCatch",
            vrDialogDiagnostics = "consoleCommandDialogDiagnostics",
        }
        local function check(map)
            for name, method in pairs(commands) do
                assert(present[name], name .. " exists after " .. map .. " loads")
                assert(handlers[name].funcName == method, name .. " runs " .. method .. ", not " .. tostring(handlers[name].funcName))
                assert(handlers[name].target == VRCompanion, name .. " runs on the Companion Mod")
                assert(type(handlers[name].desc) == "string" and handlers[name].desc ~= "", name .. " has a description")
            end
        end
        for name in pairs(commands) do assert(not present[name], name .. " does not exist before the map loads") end

        VRCompanion:loadMap("firstMap")
        check("the first map")
        VRCompanion:deleteMap()
        for name in pairs(commands) do assert(not present[name], name .. " is gone after the map unloads") end

        VRCompanion:loadMap("secondMap")
        assert(#duplicates == 0, "no command is added while it is present: " .. table.concat(duplicates, ","))
        check("the second map")
        VRCompanion:deleteMap()
        for name in pairs(commands) do assert(not present[name], name .. " is gone again") end
    )luau", "=settingsCommands"), 0);
}

TEST_F(SettingsSubsystemsTest, TheSavedSettingsReachVRBridgeAfterTheHandshakeInTheOrderDesktopMirrorFovOverscanMenuDistanceMouseConfinement) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        saved["vrSettings.desktopMirror"] = false
        saved["vrSettings.fovOverscan"] = 7
        saved["vrSettings.menuDistance"] = 2.5
        saved["vrSettings.mouseConfinement"] = false

        -- Record the order in which the Companion Mod talks to VRBridge
        local calls = {}
        local function record(name, label)
            local real = VRBridge[name]
            VRBridge[name] = function(...)
                table.insert(calls, label .. ":" .. tostring((...)))
                return real(...)
            end
        end
        local realHandshake = VRBridge.handshake
        VRBridge.handshake = function(...)
            table.insert(calls, "handshake")
            return realHandshake(...)
        end
        record("setDesktopMirror", "desktopMirror")
        record("setFovOverscan", "fovOverscan")
        record("setMenuDistance", "menuDistance")
        record("setMouseConfinement", "mouseConfinement")

        VRCompanion:loadMap("testMap")

        assert(VRCompanion.fovOverscan == 7 and VRCompanion.menuDistance == 2.5 and VRCompanion.mouseConfinement == false,
            "the saved values are restored on map load")
        assert(calls[1] == "handshake", "the handshake comes first: " .. table.concat(calls, ","))
        local expected = "handshake,desktopMirror:false,fovOverscan:7,menuDistance:2.5,mouseConfinement:false"
        assert(table.concat(calls, ",") == expected, "VRBridge was sent: " .. table.concat(calls, ","))
        assert(VRBridge.getFovOverscan() == 7 and VRBridge.getMenuDistance() == 2.5 and VRBridge.getMouseConfinement() == false,
            "and holds them")

        -- A map unload puts the Injector's values back, and the next map load sends the saved ones again
        VRCompanion:deleteMap()
        calls = {}
        VRCompanion:loadMap("secondMap")
        assert(table.concat(calls, ",") == "handshake,desktopMirror:false,fovOverscan:7,menuDistance:2.5,mouseConfinement:false",
            "the second map sends them again: " .. table.concat(calls, ","))
        VRCompanion:deleteMap()
    )luau", "=settingsReachVRBridge"), 0);
}

TEST_F(SettingsSubsystemsTest, ASettingChangedByItsCommandIsWrittenToTheSettingsFile) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:loadMap("testMap")
        VRCompanion:consoleCommandSetFovOverscan("12")
        VRCompanion:consoleCommandSetMenuDistance("3")
        VRCompanion:consoleCommandSetMouseConfinement("off")
        VRCompanion:consoleCommandSetFishCatch("release")
        assert(saved["vrSettings.fovOverscan"] == 12, "FOV Overscan is saved")
        assert(saved["vrSettings.menuDistance"] == 3, "Menu Distance is saved")
        assert(saved["vrSettings.mouseConfinement"] == false, "Mouse Confinement is saved")
        assert(saved["vrSettings.fishCatchAnswer"] == "release", "the fish catch answer is saved")
        VRCompanion:deleteMap()
    )luau", "=settingsWritten"), 0);
}
