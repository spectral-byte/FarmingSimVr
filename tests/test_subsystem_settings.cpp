// A Subsystem saves and loads its own settings through the registry (spec #280, ticket #286): registerSubsystem takes a
// saveSettings and a loadSettings hook, and saveConfiguration / loadConfiguration run them. Seen from outside: the real Companion
// Mod is loaded in modDesc order, the engine's XML functions are a fake settings file kept in Lua, and the tests assert what the
// file holds after a save, what a load leads to (the mod's settings and the calls on VRBridge at map load) and what log.txt says.
// The two characterization tests pin the settings file as it was before the registry took the settings over: the same keys and
// values on save, and the same VRBridge calls in the same order when an old file is loaded at map load.

#include "companion_mod_fixture.h"
#include <string>

namespace {

// The engine's XML functions over a settings file kept in Lua (_G.xmlFiles[path] = { key = value }), and the few engine
// functions a map load calls. A getter answers only for a value of its own type, as a key written by another setter would read
// back nothing useful from the engine either; getXMLString answers for a bool too ("true"/"false"), as the engine does.
const char* kEngineStubs = R"luau(
    _G.present = {}
    _G.addConsoleCommand = function(cmd) _G.present[cmd] = true end
    _G.removeConsoleCommand = function(cmd) _G.present[cmd] = nil end
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() end
    _G.SETTINGS_PATH = "C:/mockAppPath/modSettings/FS25_VRCompanion/settings.xml"

    _G.xmlFiles = {}
    local handles = {}
    _G.createXMLFile = function(_, path)
        local store = {}
        handles[#handles + 1] = { path = path, store = store }
        return #handles
    end
    _G.loadXMLFile = function(_, path)
        if _G.xmlFiles[path] == nil then return 0 end
        local store = {}
        for k, v in pairs(_G.xmlFiles[path]) do store[k] = v end
        handles[#handles + 1] = { path = path, store = store }
        return #handles
    end
    _G.saveXMLFile = function(h) _G.xmlFiles[handles[h].path] = handles[h].store end
    _G.delete = function() end
    local function setter(kind)
        return function(h, key, value)
            assert(type(value) == kind, "set " .. key .. " with a " .. type(value))
            handles[h].store[key] = value
        end
    end
    _G.setXMLFloat = setter("number")
    _G.setXMLInt = setter("number")
    _G.setXMLBool = setter("boolean")
    _G.setXMLString = setter("string")
    local function getter(kind)
        return function(h, key)
            local value = handles[h].store[key]
            if type(value) == kind then return value end
            return nil
        end
    end
    _G.getXMLFloat = getter("number")
    _G.getXMLInt = getter("number")
    _G.getXMLBool = getter("boolean")
    _G.getXMLString = function(h, key)
        local value = handles[h].store[key]
        if type(value) == "string" then return value end
        if type(value) == "boolean" then return tostring(value) end
        return nil
    end
    _G.hasXMLProperty = function(h, key)
        for k in pairs(handles[h].store) do
            if string.sub(k, 1, #key) == key then return true end
        end
        return false
    end

    -- The file's keys and values, one "key=value" per line, sorted
    _G.dumpSettings = function(path)
        local lines = {}
        for k, v in pairs(_G.xmlFiles[path] or {}) do
            local text = type(v) == "number" and string.format("%.4g", v) or tostring(v)
            lines[#lines + 1] = k .. "=" .. text
        end
        table.sort(lines)
        return table.concat(lines, "\n")
    end
)luau";

int CountLines(const std::string& text) {
    int count = 0;
    for (const auto& line : s_capturedLogs) {
        if (line.find(text) != std::string::npos) ++count;
    }
    return count;
}

class SubsystemSettingsTest : public CompanionModTest {
protected:
    void SetUp() override {
        CompanionModTest::SetUp();
        ASSERT_TRUE(bridge_.Inject(L));
        ASSERT_EQ(LoadCompanionModChecked(L), 0);
        ASSERT_EQ(RunLuauChunk(L, kEngineStubs, "=subsystemSettingsStubs"), 0);
    }

    fs25vr::VRBridge bridge_{nullptr, nullptr};
};

}  // namespace

// Every Subsystem setting away from its default, saved: the file holds what it held before the registry saved the settings.
TEST_F(SubsystemSettingsTest, ASavedFileHoldsTheSameKeysAndValuesAsBeforeTheRegistry) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion.motionControllersEnabled = false
        VRCompanion.pointerConeDegrees = 7
        VRCompanion.wheelGrabEnabled = false
        VRCompanion.handToolsEnabled = false
        VRCompanion.toolHand = 0
        VRCompanion.chainsawReach = 0.15
        VRCompanion.objectGrabEnabled = false
        VRCompanion.virtualGamepadEnabled = true
        VRCompanion.virtualGamepadDefaultsWritten = true
        VRCompanion.virtualGamepadRecenterMigrated = true
        VRCompanion.fishCatchAnswer = "sell"
        VRCompanion.desktopMirror = false
        VRCompanion.fovOverscan = 10
        VRCompanion.menuDistance = 2.5
        VRCompanion.mouseConfinement = false
        VRCompanion.stereoDialogDepth = 3
        VRCompanion.seatOffsets = { ["data/tractor.xml"] = { [2] = { forward = 0.1, up = 0.2, right = -0.1 } } }
        assert(VRCompanion:saveConfiguration(SETTINGS_PATH))
        _G.saved = dumpSettings(SETTINGS_PATH)
    )luau", "=saveEverySetting"), 0);
    EXPECT_EQ(EvalLua(L, "saved"),
        "vrSettings.chainsawReach=0.15\n"
        "vrSettings.desktopMirror=false\n"
        "vrSettings.dlssNearClipCab=0.1\n"
        "vrSettings.dlssNearClipOnFoot=0.15\n"
        "vrSettings.dlssNearClipOutside=0\n"
        "vrSettings.fishCatchAnswer=sell\n"
        "vrSettings.fovOverscan=10\n"
        "vrSettings.handTools=false\n"
        "vrSettings.hudConvergenceDepth=2.5\n"
        "vrSettings.hudGroupControlsMarginHorizontal=0.3\n"
        "vrSettings.hudGroupControlsMarginVertical=0.3\n"
        "vrSettings.hudGroupControlsScale=0.82\n"
        "vrSettings.hudGroupInfoMarginHorizontal=0.3\n"
        "vrSettings.hudGroupInfoMarginVertical=0.3\n"
        "vrSettings.hudGroupInfoScale=0.82\n"
        "vrSettings.hudGroupMapMarginHorizontal=0.3\n"
        "vrSettings.hudGroupMapMarginVertical=0.22\n"
        "vrSettings.hudGroupMapScale=0.82\n"
        "vrSettings.hudGroupSpeedometerMarginHorizontal=0.3\n"
        "vrSettings.hudGroupSpeedometerMarginVertical=0.2\n"
        "vrSettings.hudGroupSpeedometerScale=0.82\n"
        "vrSettings.hudGroups.controls.expanded=true\n"
        "vrSettings.hudGroups.controls.showFull=true\n"
        "vrSettings.hudGroups.controls.showStreamlined=false\n"
        "vrSettings.hudGroups.info.showFull=true\n"
        "vrSettings.hudGroups.info.showStreamlined=false\n"
        "vrSettings.hudGroups.map.showFull=true\n"
        "vrSettings.hudGroups.map.showStreamlined=false\n"
        "vrSettings.hudGroups.speedometer.showFull=true\n"
        "vrSettings.hudGroups.speedometer.showStreamlined=true\n"
        "vrSettings.hudSafeAreaHorizontal=0.3\n"
        "vrSettings.hudSafeAreaVertical=0.22\n"
        "vrSettings.hudState=Hidden\n"
        "vrSettings.menuDistance=2.5\n"
        "vrSettings.motionControllers=false\n"
        "vrSettings.mouseConfinement=false\n"
        "vrSettings.objectGrab=false\n"
        "vrSettings.pointerConeDegrees=7\n"
        "vrSettings.recenterVrTrackingSpace=true\n"
        "vrSettings.renderingMode=DualPass\n"
        "vrSettings.seatOffsets.seatOffset(0)#camera=2\n"
        "vrSettings.seatOffsets.seatOffset(0)#forward=0.1\n"
        "vrSettings.seatOffsets.seatOffset(0)#right=-0.1\n"
        "vrSettings.seatOffsets.seatOffset(0)#up=0.2\n"
        "vrSettings.seatOffsets.seatOffset(0)#vehicle=data/tractor.xml\n"
        "vrSettings.showHudLoadNotification=false\n"
        "vrSettings.snapTurnAngle=90\n"
        "vrSettings.stereoDialogDepth=3\n"
        "vrSettings.toolHand=left\n"
        "vrSettings.virtualGamepad=true\n"
        "vrSettings.virtualGamepadDefaultsWritten=true\n"
        "vrSettings.virtualGamepadRecenterMigrated=true\n"
        "vrSettings.wheelGrab=false");
}

// A file written before the change, every Subsystem setting away from its default, loaded at map load: the mod holds the same
// settings and makes the same calls on VRBridge, in the same order, as before the registry loaded the settings.
TEST_F(SubsystemSettingsTest, AnOldFileLoadsToTheSameSettingsAndTheSameVRBridgeCallsAtMapLoad) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        xmlFiles[SETTINGS_PATH] = {
            ["vrSettings.motionControllers"] = false,
            ["vrSettings.pointerConeDegrees"] = 7,
            ["vrSettings.wheelGrab"] = false,
            ["vrSettings.handTools"] = false,
            ["vrSettings.toolHand"] = "left",
            ["vrSettings.chainsawReach"] = "0.15",
            ["vrSettings.objectGrab"] = false,
            ["vrSettings.virtualGamepad"] = true,
            ["vrSettings.virtualGamepadDefaultsWritten"] = true,
            ["vrSettings.virtualGamepadRecenterMigrated"] = true,
            ["vrSettings.fishCatchAnswer"] = "release",
            ["vrSettings.desktopMirror"] = false,
            ["vrSettings.fovOverscan"] = 12,
            ["vrSettings.menuDistance"] = 3,
            ["vrSettings.mouseConfinement"] = false,
            ["vrSettings.stereoDialogDepth"] = 4,
            ["vrSettings.seatOffsets.seatOffset(0)#vehicle"] = "data/tractor.xml",
            ["vrSettings.seatOffsets.seatOffset(0)#camera"] = 2,
            ["vrSettings.seatOffsets.seatOffset(0)#forward"] = 0.1,
            ["vrSettings.seatOffsets.seatOffset(0)#up"] = 0.2,
            ["vrSettings.seatOffsets.seatOffset(0)#right"] = -0.1,
            ["vrSettings.renderingMode"] = "DualPass",
            ["vrSettings.hudState"] = "Full",
            ["vrSettings.snapTurnAngle"] = 45,
            ["vrSettings.hudSafeAreaHorizontal"] = 0.25,
            ["vrSettings.dlssNearClipCab"] = 0.3,
        }
        local calls = {}
        for name, fn in pairs(VRBridge) do
            if type(fn) == "function" and string.sub(name, 1, 3) ~= "get" and string.sub(name, 1, 2) ~= "is" then
                VRBridge[name] = function(...)
                    local args = {}
                    for i = 1, select("#", ...) do
                        local a = select(i, ...)
                        args[i] = type(a) == "number" and string.format("%.4g", a) or tostring(a)
                    end
                    calls[#calls + 1] = name .. "(" .. table.concat(args, ",") .. ")"
                    return fn(...)
                end
            end
        end
        VRCompanion:loadMap("testMap")
        _G.bridgeCalls = table.concat(calls, "\n")
        local seat = VRCompanion.seatOffsets["data/tractor.xml"][2]
        _G.loaded = table.concat({
            tostring(VRCompanion.motionControllersEnabled), tostring(VRCompanion.pointerConeDegrees),
            tostring(VRCompanion.wheelGrabEnabled), tostring(VRCompanion.handToolsEnabled), tostring(VRCompanion.toolHand),
            tostring(VRCompanion.chainsawReach), tostring(VRCompanion.objectGrabEnabled),
            tostring(VRCompanion.virtualGamepadEnabled), tostring(VRCompanion.virtualGamepadDefaultsWritten),
            tostring(VRCompanion.virtualGamepadRecenterMigrated), tostring(VRCompanion.fishCatchAnswer),
            tostring(VRCompanion.desktopMirror), tostring(VRCompanion.fovOverscan), tostring(VRCompanion.menuDistance),
            tostring(VRCompanion.mouseConfinement), tostring(VRCompanion.stereoDialogDepth),
            string.format("%g,%g,%g", seat.forward, seat.up, seat.right),
            -- and the core's own settings around them
            tostring(VRCompanion.persistedHudState), string.format("%g", math.deg(VRCompanion.snapTurnAngle)),
            string.format("%g", VRCompanion.hudSafeAreaHorizontal), tostring(VRCompanion.dlssNearClipCab),
        }, " ")
    )luau", "=loadOldFile"), 0);
    EXPECT_EQ(EvalLua(L, "loaded"), "false 7 false false 0 0.15 false true true true release false 12 3 false 4 0.1,0.2,-0.1 2 45 0.25 0.3");
    EXPECT_EQ(EvalLua(L, "bridgeCalls"),
        "set2DViewActive(false)\n"
        "handshake(nil,1.2.0.0)\n"
        "setMenuVisible(false)\n"
        "resetTemporalHistory()\n"
        "setRenderingMode(SequentialDualPass)\n"
        "setDesktopMirror(false)\n"
        "setFovOverscan(12)\n"
        "setMenuDistance(3)\n"
        "setMouseConfinement(false)\n"
        "setDlssNearClip(0.15)");
}

TEST_F(SubsystemSettingsTest, ASubsystemRegisteredWithSaveAndLoadHooksHasItsValueWrittenOnSaveAndReadBackOnLoad) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion.probeLevel = 0.75
        VRCompanion:registerSubsystem("probe", {
            saveSettings = function(self, xmlFile, xml)
                _G.saveSawSelf = (self == VRCompanion)
                xml.setFloat(xmlFile, "vrSettings.probeLevel", self.probeLevel)
            end,
            loadSettings = function(self, xmlFile, xml)
                _G.loadSawSelf = (self == VRCompanion)
                local loaded = xml.getFloat(xmlFile, "vrSettings.probeLevel")
                if loaded ~= nil then self.probeLevel = loaded end
            end,
        })
        assert(VRCompanion:saveConfiguration(SETTINGS_PATH))
        assert(xmlFiles[SETTINGS_PATH]["vrSettings.probeLevel"] == 0.75, "the value is in the file")

        VRCompanion.probeLevel = 0.1
        VRCompanion:loadConfiguration(SETTINGS_PATH)
        _G.levelAfterLoad = VRCompanion.probeLevel
    )luau", "=probeRoundTrip"), 0);
    EXPECT_EQ(EvalLua(L, "levelAfterLoad"), "0.75");
    EXPECT_EQ(EvalLua(L, "saveSawSelf"), "true");
    EXPECT_EQ(EvalLua(L, "loadSawSelf"), "true");
}

TEST_F(SubsystemSettingsTest, ASaveOrLoadHookThatRaisesIsLoggedOnceAndTheOtherSubsystemsStillSaveAndLoad) {
    ExpectHookErrors();
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        VRCompanion:registerSubsystem("alpha", {
            saveSettings = function() error("alpha save failed") end,
            loadSettings = function() error("alpha load failed") end,
        })
        VRCompanion:registerSubsystem("beta", {
            saveSettings = function(self, xmlFile, xml) xml.setString(xmlFile, "vrSettings.beta", "saved") end,
            loadSettings = function(self, xmlFile, xml) _G.betaLoaded = xml.getString(xmlFile, "vrSettings.beta") end,
        })
        for _ = 1, 3 do
            assert(VRCompanion:saveConfiguration(SETTINGS_PATH), "the save goes on past the hook that raised")
            VRCompanion:loadConfiguration(SETTINGS_PATH)
        end
        _G.fishCatchSaved = xmlFiles[SETTINGS_PATH]["vrSettings.fishCatchAnswer"]
    )luau", "=raisingSettingsHooks"), 0);
    EXPECT_EQ(CountLines("[FS25_VR] [Error] Subsystem 'alpha' saveSettings hook raised: "), 1) << "three saves, one line";
    EXPECT_EQ(CountLines("[FS25_VR] [Error] Subsystem 'alpha' loadSettings hook raised: "), 1) << "three loads, one line";
    EXPECT_EQ(CountLines("alpha save failed"), 1) << "the error text is in the line";
    EXPECT_EQ(CountLines("Subsystem 'beta'"), 0);
    EXPECT_EQ(EvalLua(L, "betaLoaded"), "saved") << "the subsystem after the one that raised saved and loaded";
    EXPECT_EQ(EvalLua(L, "fishCatchSaved"), "ask") << "and so did the mod's own subsystems";
}

TEST_F(SubsystemSettingsTest, SaveAndLoadHooksRunInRegistrationOrder) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        local order = {}
        for _, name in ipairs({ "first", "second", "third" }) do
            VRCompanion:registerSubsystem(name, {
                saveSettings = function() table.insert(order, name .. ":save") end,
                loadSettings = function() table.insert(order, name .. ":load") end,
            })
        end
        assert(VRCompanion:saveConfiguration(SETTINGS_PATH))
        VRCompanion:loadConfiguration(SETTINGS_PATH)
        _G.hookOrder = table.concat(order, ",")
    )luau", "=settingsHookOrder"), 0);
    EXPECT_EQ(EvalLua(L, "hookOrder"), "first:save,second:save,third:save,first:load,second:load,third:load");
}

TEST_F(SubsystemSettingsTest, ASaveOrLoadHookThatIsNotAFunctionRaisesWhenTheSubsystemRegisters) {
    ASSERT_EQ(RunLuauChunk(L, R"luau(
        for _, hookName in ipairs({ "saveSettings", "loadSettings" }) do
            local ok, err = pcall(function() VRCompanion:registerSubsystem("probe", { [hookName] = "vrSettings.probe" }) end)
            assert(not ok, hookName .. ": registering must raise")
            assert(string.find(tostring(err), hookName, 1, true), "the message names the hook: " .. tostring(err))
            assert(string.find(tostring(err), "probe", 1, true), "and the subsystem: " .. tostring(err))
        end
        -- Nothing was left behind: a save and a load run
        assert(VRCompanion:saveConfiguration(SETTINGS_PATH))
        VRCompanion:loadConfiguration(SETTINGS_PATH)
    )luau", "=settingsHookValidation"), 0);
}
