-- ====================================================================
-- Test Suite for VR HUD Toggle Action & State Machine (Issue #25)
-- ====================================================================

local fs = require("@lune/fs")

-- Load the Companion Mod module files in modDesc order, exactly as the engine does
loadCompanionMod()

local passed = 0
local failed = 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print(string.format("  [PASS] %s", name))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
end

print("Running VR HUD State Machine Tests...")

-- Slice 1: modDesc.xml declarations
test("ModDescDeclaresHudToggleActionAndBindings", function()
    local modDescContent = fs.readFile("mods/FS25_VRCompanion/modDesc.xml")
    assert(string.find(modDescContent, '<action name="VR_HUD_TOGGLE" category="ONFOOT VEHICLE"/>') ~= nil,
        "modDesc.xml must declare action VR_HUD_TOGGLE")
    assert(string.find(modDescContent, '<actionBinding action="VR_HUD_TOGGLE">') ~= nil,
        "modDesc.xml must declare actionBinding for VR_HUD_TOGGLE")
    assert(string.find(modDescContent, '<text name="input_VR_HUD_TOGGLE">') ~= nil,
        "modDesc.xml must declare l10n text for input_VR_HUD_TOGGLE")
end)

-- Slice 2: State constants, query/set, cycle transitions, and clamping
test("DefinesThreeStageHudStateConstants", function()
    assert(VRCompanion.HUD_STATE_HIDDEN == 0, "HUD_STATE_HIDDEN must be 0")
    assert(VRCompanion.HUD_STATE_STREAMLINED == 1, "HUD_STATE_STREAMLINED must be 1")
    assert(VRCompanion.HUD_STATE_FULL == 2, "HUD_STATE_FULL must be 2")
end)

test("CyclesThroughThreeStageStateMachine", function()
    -- Reset to Hidden
    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    assert(VRCompanion:getHudState() == 0, "Initial state must be 0 (Hidden)")

    -- Cycle 1: Hidden (0) -> Streamlined (1)
    local s1 = VRCompanion:cycleHudState()
    assert(s1 == 1, "First cycle must advance to 1 (Streamlined)")
    assert(VRCompanion:getHudState() == 1, "getHudState must report 1")

    -- Cycle 2: Streamlined (1) -> Full (2)
    local s2 = VRCompanion:cycleHudState()
    assert(s2 == 2, "Second cycle must advance to 2 (Full)")
    assert(VRCompanion:getHudState() == 2, "getHudState must report 2")

    -- Cycle 3: Full (2) -> Hidden (0)
    local s3 = VRCompanion:cycleHudState()
    assert(s3 == 0, "Third cycle must return to 0 (Hidden)")
    assert(VRCompanion:getHudState() == 0, "getHudState must report 0")
end)

test("ClampsHudStateValuesWithinRange", function()
    VRCompanion:setHudState(-5)
    assert(VRCompanion:getHudState() == 0, "Negative state clamped to 0")

    VRCompanion:setHudState(10)
    assert(VRCompanion:getHudState() == 2, "Excessive state clamped to 2")
end)

test("OnHudToggleActionAdvancesStateMachine", function()
    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    local res1 = VRCompanion:onHudToggleAction("VR_HUD_TOGGLE", 1)
    assert(res1 == 1, "onHudToggleAction must advance state to 1")
    assert(VRCompanion:getHudState() == 1, "hudState must be 1")

    local res2 = VRCompanion:onHudToggleAction("VR_HUD_TOGGLE", 1)
    assert(res2 == 2, "onHudToggleAction must advance state to 2")
    assert(VRCompanion:getHudState() == 2, "hudState must be 2")

    local res3 = VRCompanion:onHudToggleAction("VR_HUD_TOGGLE", 1)
    assert(res3 == 0, "onHudToggleAction must return state to 0")
    assert(VRCompanion:getHudState() == 0, "hudState must be 0")
end)

test("IsDisplayVisibleFiltersComponentsAcrossAllThreeStates", function()
    local displays = {
        "SpeedMeterDisplay",
        "ContextActionDisplay",
        "FillLevelsDisplay",
        "IngameMap",
        "InputHelpDisplay",
        "GameInfoDisplay"
    }

    -- 1. Hidden (0): All elements must be suppressed
    for _, d in ipairs(displays) do
        assert(VRCompanion:isDisplayVisible(d, VRCompanion.HUD_STATE_HIDDEN) == false,
            d .. " must be false in Hidden state")
    end

    -- 2. Streamlined (1): Essential driving/implement telemetry visible; background panels suppressed
    assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true,
        "SpeedMeterDisplay must be visible in Streamlined state")
    assert(VRCompanion:isDisplayVisible("ContextActionDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true,
        "ContextActionDisplay prompts must be visible in Streamlined state")
    assert(VRCompanion:isDisplayVisible("FillLevelsDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true,
        "FillLevelsDisplay must be visible in Streamlined state")

    assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_STREAMLINED) == false,
        "IngameMap minimap must be suppressed in Streamlined state")
    assert(VRCompanion:isDisplayVisible("InputHelpDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false,
        "InputHelpDisplay key table must be suppressed in Streamlined state")
    assert(VRCompanion:isDisplayVisible("GameInfoDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false,
        "GameInfoDisplay clock/weather must be suppressed in Streamlined state")

    -- 3. Full (2): All elements restored
    for _, d in ipairs(displays) do
        assert(VRCompanion:isDisplayVisible(d, VRCompanion.HUD_STATE_FULL) == true,
            d .. " must be visible in Full state")
    end

    -- 4. Defaults to active state when state parameter is omitted
    VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
    assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay") == true, "SpeedMeterDisplay visible with omitted state")
    assert(VRCompanion:isDisplayVisible("IngameMap") == false, "IngameMap suppressed with omitted state")

    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay") == false, "SpeedMeterDisplay hidden with omitted state")
end)

-- HUD Groups (spec #50, ticket #51): visibility is looked up through a display's group
test("DefinesTheFourHudGroupsWithTheirMemberDisplaysAndHomeCorners", function()
    local groups = VRCompanion.HUD_GROUPS
    assert(type(groups) == "table", "HUD_GROUPS must be defined")

    local function memberOf(group, displayName)
        for _, name in ipairs(group.displays) do
            if name == displayName then return true end
        end
        return false
    end

    assert(memberOf(groups.map, "IngameMap") and #groups.map.displays == 1, "Map group holds only IngameMap")
    assert(groups.map.alignX == "left" and groups.map.alignY == "bottom", "Map group is bottom-left")

    assert(memberOf(groups.speedometer, "SpeedMeterDisplay") and memberOf(groups.speedometer, "FillLevelsDisplay")
        and #groups.speedometer.displays == 2, "Speedometer group holds the speedometer and the fill levels")
    assert(groups.speedometer.alignX == "right" and groups.speedometer.alignY == "bottom", "Speedometer group is bottom-right")

    assert(memberOf(groups.info, "GameInfoDisplay") and #groups.info.displays == 1, "Info group holds only GameInfoDisplay")
    assert(groups.info.alignX == "right" and groups.info.alignY == "top", "Info group is top-right")

    assert(memberOf(groups.controls, "InputHelpDisplay") and #groups.controls.displays == 1, "Controls group holds only InputHelpDisplay")
    assert(groups.controls.alignX == "left" and groups.controls.alignY == "top", "Controls group is top-left")

    local count = 0
    for _, group in pairs(groups) do
        count = count + 1
        assert(not memberOf(group, "ContextActionDisplay"), "The context action display belongs to no group")
    end
    assert(count == 4, "There are exactly four HUD Groups")
end)

-- Per-group visibility (spec #50, ticket #53)
test("GroupVisibilityDefaultsComeFromDefaultConstants", function()
    local streamlinedDefaults = VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED
    local fullDefaults = VRCompanion.DEFAULT_HUD_GROUP_FULL
    assert(type(streamlinedDefaults) == "table" and type(fullDefaults) == "table", "per-group visibility defaults must be DEFAULT_* constants")

    -- Streamlined = Speedometer group only; Full = all four groups
    for name, _ in pairs(VRCompanion.HUD_GROUPS) do
        assert(streamlinedDefaults[name] == (name == "speedometer"), name .. " Streamlined default")
        assert(fullDefaults[name] == true, name .. " Full default")
    end

    -- A fresh copy of the mod starts every group at those defaults
    local saved = _G.VRCompanion
    _G.VRCompanion = nil
    local ok, err = pcall(function()
        loadCompanionMod()
        local fresh = _G.VRCompanion
        for name, group in pairs(fresh.HUD_GROUPS) do
            assert(group.streamlined == fresh.DEFAULT_HUD_GROUP_STREAMLINED[name], name .. " starts at its Streamlined default")
            assert(group.full == fresh.DEFAULT_HUD_GROUP_FULL[name], name .. " starts at its Full default")
        end
    end)
    _G.VRCompanion = saved
    assert(ok, tostring(err))
end)

test("DisplayVisibilityFollowsItsGroupsFlagsInStreamlinedAndFull", function()
    local info = VRCompanion.HUD_GROUPS.info
    local origStreamlined, origFull = info.streamlined, info.full
    local ok, err = pcall(function()
        info.streamlined = true
        info.full = false
        assert(VRCompanion:isDisplayVisible("GameInfoDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true,
            "A display follows its group's Streamlined flag")
        assert(VRCompanion:isDisplayVisible("GameInfoDisplay", VRCompanion.HUD_STATE_FULL) == false,
            "A display follows its group's Full flag")
        assert(VRCompanion:isDisplayVisible("GameInfoDisplay", VRCompanion.HUD_STATE_HIDDEN) == false,
            "Hidden shows nothing whatever the group flags say")
    end)
    info.streamlined, info.full = origStreamlined, origFull
    assert(ok, err)
end)

test("SpeedometerGroupFlagsGovernBothItsDisplays", function()
    local group = VRCompanion.HUD_GROUPS.speedometer
    local orig = group.streamlined
    local ok, err = pcall(function()
        group.streamlined = false
        assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false)
        assert(VRCompanion:isDisplayVisible("FillLevelsDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false)
    end)
    group.streamlined = orig
    assert(ok, err)
end)

test("ContextActionVisibilityIsIndependentOfTheGroups", function()
    local saved = {}
    for name, group in pairs(VRCompanion.HUD_GROUPS) do
        saved[name] = { group.streamlined, group.full }
        group.streamlined, group.full = false, false
    end
    local ok, err = pcall(function()
        assert(VRCompanion:isDisplayVisible("ContextActionDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true)
        assert(VRCompanion:isDisplayVisible("ContextActionDisplay", VRCompanion.HUD_STATE_FULL) == true)
        assert(VRCompanion:isDisplayVisible("ContextActionDisplay", VRCompanion.HUD_STATE_HIDDEN) == false)
    end)
    for name, flags in pairs(saved) do
        VRCompanion.HUD_GROUPS[name].streamlined, VRCompanion.HUD_GROUPS[name].full = flags[1], flags[2]
    end
    assert(ok, err)
end)

-- Runs fn with the group flags restored to their defaults afterwards and the settings file and
-- the on-screen warning mocked. Returns what the game would have observed.
local function withGroupVisibilityHarness(fn)
    local savedFlags = {}
    for name, group in pairs(VRCompanion.HUD_GROUPS) do
        savedFlags[name] = { group.streamlined, group.full }
    end
    local origState = VRCompanion:getHudState()
    local store, saveCount, warnings = {}, 0, {}
    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.createFolder = function() end
    _G.createXMLFile = function() return 999 end
    _G.setXMLFloat = function(_, key, val) store[key] = val end
    _G.setXMLBool = function(_, key, val) store[key] = val end
    _G.setXMLString = function(_, key, val) store[key] = val end
    _G.saveXMLFile = function() saveCount = saveCount + 1 end
    _G.delete = function() end
    _G.g_currentMission = {
        hud = nil,
        showBlinkingWarning = function(self, msg, duration) table.insert(warnings, msg) end
    }
    local ok, err = pcall(fn, { store = store, warnings = warnings, saveCount = function() return saveCount end })
    for name, flags in pairs(savedFlags) do
        VRCompanion.HUD_GROUPS[name].streamlined, VRCompanion.HUD_GROUPS[name].full = flags[1], flags[2]
    end
    VRCompanion:setHudState(origState)
    _G.getUserProfileAppPath, _G.createFolder, _G.createXMLFile = nil, nil, nil
    _G.setXMLFloat, _G.setXMLBool, _G.setXMLString, _G.saveXMLFile, _G.delete = nil, nil, nil, nil, nil
    _G.g_currentMission = nil
    assert(ok, err)
end

test("VrHudGroupShowAppliesSavesAndConfirms", function()
    withGroupVisibilityHarness(function(observed)
        local function mockDisplay() return { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end } end
        local hud = {
            setIsVisible = function(self, v) self.isVisible = v end,
            speedMeter = mockDisplay(), fillLevels = mockDisplay(), contextAction = mockDisplay(),
            inputHelp = mockDisplay(), ingameMap = mockDisplay(), gameInfo = mockDisplay()
        }
        _G.g_currentMission.hud = hud
        VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
        assert(hud.ingameMap.isVisible == false, "the map starts out of the Streamlined HUD")

        -- Map on in Streamlined: applies at once, saves, and confirms on screen
        local result = VRCompanion:consoleCommandHudGroupShow("map", "streamlined", "on")
        assert(hud.ingameMap.isVisible == true, "the change must apply immediately")
        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_STREAMLINED) == true)
        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_FULL) == true, "Full flag is untouched")
        assert(observed.saveCount() == 1, "the change must be saved")
        assert(observed.store["vrSettings.hudGroups.map.showStreamlined"] == true, "saved Streamlined flag")
        assert(observed.store["vrSettings.hudGroups.map.showFull"] == true, "saved Full flag")
        assert(#observed.warnings == 1 and string.find(observed.warnings[1], "Map", 1, true) ~= nil, "on-screen confirmation names the group")
        assert(type(result) == "string" and string.find(result, "map", 1, true) ~= nil, "console result confirms the change")

        -- A group may be on in Streamlined but off in Full
        VRCompanion:consoleCommandHudGroupShow("map", "full", "off")
        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_STREAMLINED) == true)
        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_FULL) == false)
        assert(observed.store["vrSettings.hudGroups.map.showFull"] == false, "saved Full flag off")
        VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
        assert(hud.ingameMap.isVisible == false, "map is out of the Full HUD")
        assert(hud.speedMeter.isVisible == true, "other groups keep their flags")

        -- The speedometer group's flag governs both its displays; the context action prompt ignores it
        VRCompanion:consoleCommandHudGroupShow("speedometer", "full", "off")
        assert(hud.speedMeter.isVisible == false and hud.fillLevels.isVisible == false, "both speedometer displays follow the group")
        assert(hud.contextAction.isVisible == true, "context action stays visible in Full")

        -- Hidden shows nothing whatever the flags say
        for name, _ in pairs(VRCompanion.HUD_GROUPS) do
            VRCompanion:consoleCommandHudGroupShow(name, "streamlined", "on")
            VRCompanion:consoleCommandHudGroupShow(name, "full", "on")
        end
        VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
        for _, displayName in ipairs({ "SpeedMeterDisplay", "FillLevelsDisplay", "IngameMap", "InputHelpDisplay", "GameInfoDisplay", "ContextActionDisplay" }) do
            assert(VRCompanion:isDisplayVisible(displayName) == false, displayName .. " must be hidden in Hidden whatever the flags")
        end
        VRCompanion:consoleCommandHudGroupShow("info", "streamlined", "off")
        assert(hud.gameInfo.isVisible == false and hud.isVisible == false, "changing a flag while Hidden shows nothing")
    end)
end)

test("VrHudGroupShowWithBadArgumentsPrintsUsageAndEveryGroupsVisibility", function()
    withGroupVisibilityHarness(function(observed)
        VRCompanion.HUD_GROUPS.info.streamlined = true
        VRCompanion.HUD_GROUPS.info.full = false

        local attempts = {
            { },
            { "map" },
            { "map", "streamlined" },
            { "castle", "streamlined", "on" },
            { "map", "hidden", "on" },
            { "map", "streamlined", "maybe" },
        }
        for _, args in ipairs(attempts) do
            local result = VRCompanion:consoleCommandHudGroupShow(args[1], args[2], args[3])
            assert(string.find(result, "Usage: vrHudGroupShow", 1, true) ~= nil, "usage expected for " .. table.concat(args, " "))
            for _, name in ipairs({ "map", "speedometer", "info", "controls" }) do
                assert(string.find(result, name, 1, true) ~= nil, name .. " must be listed with its visibility")
            end
        end
        -- Current values appear: info is Streamlined on, Full off
        local result = VRCompanion:consoleCommandHudGroupShow()
        assert(string.find(result, "info: streamlined=on full=off", 1, true) ~= nil, "current visibility listed: " .. result)
        assert(string.find(result, "speedometer: streamlined=on full=on", 1, true) ~= nil, "default speedometer visibility listed")
        assert(string.find(result, "map: streamlined=off full=on", 1, true) ~= nil, "default map visibility listed")

        assert(observed.saveCount() == 0, "invalid input must not save")
        assert(#observed.warnings == 0, "invalid input must not show a confirmation")
        assert(VRCompanion.HUD_GROUPS.map.streamlined == false and VRCompanion.HUD_GROUPS.map.full == true, "invalid input must not change flags")
    end)
end)

test("VrHudGroupShowAcceptsWordsAndNumbersForOnOff", function()
    withGroupVisibilityHarness(function()
        VRCompanion:consoleCommandHudGroupShow("controls", "streamlined", "true")
        assert(VRCompanion.HUD_GROUPS.controls.streamlined == true)
        VRCompanion:consoleCommandHudGroupShow("CONTROLS", "Streamlined", "0")
        assert(VRCompanion.HUD_GROUPS.controls.streamlined == false)
        VRCompanion:consoleCommandHudGroupShow("controls", "full", "false")
        assert(VRCompanion.HUD_GROUPS.controls.full == false)
        VRCompanion:consoleCommandHudGroupShow("controls", "full", "1")
        assert(VRCompanion.HUD_GROUPS.controls.full == true)
    end)
end)

test("VrHudGroupShowIsRegisteredAsAConsoleCommand", function()
    local registered = {}
    local orig = _G.addConsoleCommand
    _G.addConsoleCommand = function(cmd, desc, fnName, target) registered[cmd] = fnName end
    VRCompanion:loadMap("mapUS")
    _G.addConsoleCommand = orig
    assert(registered.vrHudGroupShow == "consoleCommandHudGroupShow", "vrHudGroupShow must be registered")
    VRCompanion:deleteMap()
end)

test("HudCycleNotificationListsTheGroupsThatAreOnInTheNewState", function()
    withGroupVisibilityHarness(function(observed)
        local function cycleTo(state)
            VRCompanion.lastHudToggleTime = nil
            VRCompanion:setHudState((state + 2) % 3)
            VRCompanion.lastHudToggleTime = nil
            VRCompanion:cycleHudState()
            return observed.warnings[#observed.warnings]
        end

        assert(cycleTo(VRCompanion.HUD_STATE_STREAMLINED) == "VR HUD: Streamlined (Speedometer)", "default Streamlined lists only the speedometer group")
        assert(cycleTo(VRCompanion.HUD_STATE_FULL) == "VR HUD: Full (Speedometer, Map, Info, Controls)", "default Full lists all four groups")
        assert(cycleTo(VRCompanion.HUD_STATE_HIDDEN) == "VR HUD: Hidden", "Hidden lists no groups")

        VRCompanion.HUD_GROUPS.map.streamlined = true
        assert(cycleTo(VRCompanion.HUD_STATE_STREAMLINED) == "VR HUD: Streamlined (Speedometer, Map)", "custom Streamlined lists the enabled groups")

        VRCompanion.HUD_GROUPS.info.full = false
        VRCompanion.HUD_GROUPS.controls.full = false
        assert(cycleTo(VRCompanion.HUD_STATE_FULL) == "VR HUD: Full (Speedometer, Map)", "custom Full lists the enabled groups")

        for _, group in pairs(VRCompanion.HUD_GROUPS) do group.streamlined = false end
        assert(cycleTo(VRCompanion.HUD_STATE_STREAMLINED) == "VR HUD: Streamlined (none)", "a state with no groups says so")
    end)
end)

-- Slice 3: HUD visibility and display state application
test("AppliesHiddenStateToHudAndDisplays", function()
    local hud = {
        isVisible = true,
        setIsVisibleCalled = false,
        setIsVisible = function(self, vis)
            self.setIsVisibleCalled = true
            self.isVisible = vis
        end,
        speedMeter = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        fillLevels = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        contextAction = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
    }
    _G.g_currentMission = { hud = hud }

    VRCompanion:applyHudState(VRCompanion.HUD_STATE_HIDDEN)

    assert(hud.setIsVisibleCalled == true, "hud:setIsVisible must be called")
    assert(hud.isVisible == false, "hud:setIsVisible(false) must be applied")
    assert(hud.speedMeter.isVisible == false, "speedMeter must be hidden")
    assert(hud.fillLevels.isVisible == false, "fillLevels must be hidden")
    assert(hud.inputHelp.isVisible == false, "inputHelp must be hidden")
    assert(hud.gameInfo.isVisible == false, "gameInfo must be hidden")
    assert(hud.contextAction.isVisible == false, "contextAction must be hidden")
    assert(hud.ingameMap.isVisible == false, "ingameMap must be hidden")
end)

test("AppliesStreamlinedStatePreservingEssentialTelemetryAndSuppressingBulkyPanels", function()
    local hud = {
        isVisible = false,
        setIsVisibleCalled = false,
        setIsVisible = function(self, vis)
            self.setIsVisibleCalled = true
            self.isVisible = vis
        end,
        speedMeter = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        fillLevels = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        contextAction = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
    }
    _G.g_currentMission = { hud = hud }

    VRCompanion:applyHudState(VRCompanion.HUD_STATE_STREAMLINED)

    assert(hud.setIsVisibleCalled == true, "hud:setIsVisible must be called")
    assert(hud.isVisible == true, "hud:setIsVisible(true) must be applied in Streamlined mode")

    -- Essential driving and machine telemetry preserved
    assert(hud.speedMeter.isVisible == true, "speedMeter must be visible in Streamlined mode")
    assert(hud.fillLevels.isVisible == true, "machine fill levels must be visible in Streamlined mode")
    assert(hud.contextAction.isVisible == true, "contextAction prompts must be visible in Streamlined mode")

    -- Bulky secondary panels suppressed (minimap, input help, clock, weather)
    assert(hud.inputHelp.isVisible == false, "inputHelp must be suppressed in Streamlined mode")
    assert(hud.ingameMap.isVisible == false, "ingameMap must be suppressed in Streamlined mode")
    assert(hud.gameInfo.isVisible == false, "gameInfo (clock, weather) must be suppressed in Streamlined mode")
end)

test("AppliesFullStateRestoringAllDisplays", function()
    local hud = {
        isVisible = false,
        setIsVisibleCalled = false,
        setIsVisible = function(self, vis)
            self.setIsVisibleCalled = true
            self.isVisible = vis
        end,
        speedMeter = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        fillLevels = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        inputHelp = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        gameInfo = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        contextAction = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        ingameMap = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end }
    }
    _G.g_currentMission = { hud = hud }

    VRCompanion:applyHudState(VRCompanion.HUD_STATE_FULL)

    assert(hud.setIsVisibleCalled == true, "hud:setIsVisible must be called")
    assert(hud.isVisible == true, "hud:setIsVisible(true) must be applied in Full mode")

    -- All base-game elements visible
    assert(hud.speedMeter.isVisible == true, "speedMeter must be visible in Full mode")
    assert(hud.fillLevels.isVisible == true, "fillLevels must be visible in Full mode")
    assert(hud.inputHelp.isVisible == true, "inputHelp must be visible in Full mode")
    assert(hud.gameInfo.isVisible == true, "gameInfo must be visible in Full mode")
    assert(hud.contextAction.isVisible == true, "contextAction must be visible in Full mode")
    assert(hud.ingameMap.isVisible == true, "ingameMap must be visible in Full mode")
end)

test("StreamlinedStatePreservesGranularDrivingAndImplementMetrics", function()
    local makeMetric = function()
        return { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end }
    end

    local hud = {
        isVisible = false,
        setIsVisible = function(self, vis) self.isVisible = vis end,
        speedMeter = {
            isVisible = false,
            setIsVisible = function(self, v) self.isVisible = v end,
            vehicleDamage = makeMetric(),
            fuelLevel = makeMetric(),
            gear = makeMetric(),
            rpm = makeMetric(),
            fillLevelsDisplay = makeMetric()
        },
        contextAction = makeMetric(),
        inputHelp = makeMetric(),
        gameInfo = makeMetric(),
        ingameMap = makeMetric()
    }
    _G.g_currentMission = { hud = hud }

    -- 1. Streamlined mode: essential metrics remain visible, bulky panels suppressed
    VRCompanion:applyHudState(VRCompanion.HUD_STATE_STREAMLINED)
    assert(hud.isVisible == true)
    assert(hud.speedMeter.isVisible == true)
    assert(hud.speedMeter.vehicleDamage.isVisible == true, "vehicleDamage visible in Streamlined")
    assert(hud.speedMeter.fuelLevel.isVisible == true, "fuelLevel visible in Streamlined")
    assert(hud.speedMeter.gear.isVisible == true, "gear visible in Streamlined")
    assert(hud.speedMeter.rpm.isVisible == true, "rpm visible in Streamlined")
    assert(hud.speedMeter.fillLevelsDisplay.isVisible == true, "fillLevelsDisplay visible in Streamlined")
    assert(hud.contextAction.isVisible == true, "contextAction visible in Streamlined")
    assert(hud.inputHelp.isVisible == false, "inputHelp suppressed in Streamlined")
    assert(hud.gameInfo.isVisible == false, "gameInfo suppressed in Streamlined")
    assert(hud.ingameMap.isVisible == false, "ingameMap suppressed in Streamlined")

    -- 2. Hidden mode: all elements suppressed
    VRCompanion:applyHudState(VRCompanion.HUD_STATE_HIDDEN)
    assert(hud.isVisible == false)
    assert(hud.speedMeter.isVisible == false)
    assert(hud.speedMeter.vehicleDamage.isVisible == false, "vehicleDamage hidden in Hidden")
    assert(hud.speedMeter.fuelLevel.isVisible == false, "fuelLevel hidden in Hidden")
    assert(hud.speedMeter.gear.isVisible == false, "gear hidden in Hidden")
    assert(hud.speedMeter.rpm.isVisible == false, "rpm hidden in Hidden")
    assert(hud.speedMeter.fillLevelsDisplay.isVisible == false, "fillLevelsDisplay hidden in Hidden")
    assert(hud.contextAction.isVisible == false, "contextAction hidden in Hidden")

    -- 3. Full mode: all elements restored
    VRCompanion:applyHudState(VRCompanion.HUD_STATE_FULL)
    assert(hud.isVisible == true)
    assert(hud.speedMeter.isVisible == true)
    assert(hud.speedMeter.vehicleDamage.isVisible == true, "vehicleDamage visible in Full")
    assert(hud.speedMeter.fuelLevel.isVisible == true, "fuelLevel visible in Full")
    assert(hud.speedMeter.gear.isVisible == true, "gear visible in Full")
    assert(hud.speedMeter.rpm.isVisible == true, "rpm visible in Full")
    assert(hud.speedMeter.fillLevelsDisplay.isVisible == true, "fillLevelsDisplay visible in Full")
    assert(hud.contextAction.isVisible == true, "contextAction visible in Full")
    assert(hud.inputHelp.isVisible == true, "inputHelp visible in Full")
    assert(hud.gameInfo.isVisible == true, "gameInfo visible in Full")
    assert(hud.ingameMap.isVisible == true, "ingameMap visible in Full")
end)

-- Slice 4: Map lifecycle (defaults to Hidden on loadMap, restored on deleteMap)
test("LoadMapInitializesHudInHiddenStateAndRegistersAction", function()
    local registeredActions = {}
    _G.g_inputBinding = {
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            table.insert(registeredActions, {
                actionName = actionName,
                target = target,
                callback = callback,
                startActive = startActive
            })
            return true, #registeredActions
        end,
        setActionEventTextPriority = function(self, eventId, priority) end
    }

    local mockHud = {
        isVisible = true,
        setIsVisibleCalled = false,
        lastVisibleParam = nil,
        setIsVisible = function(self, vis)
            self.setIsVisibleCalled = true
            self.lastVisibleParam = vis
            self.isVisible = vis
        end,
        speedMeter = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        contextAction = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
    }
    _G.g_currentMission = { hud = mockHud }

    VRCompanion:configureHudSafeArea()
    VRCompanion:loadMap("mapUS")

    -- Check HUD state defaulted to Hidden (0)
    assert(VRCompanion:getHudState() == 0, "HUD state must default to 0 (Hidden) on map load")
    assert(mockHud.setIsVisibleCalled == true, "hud:setIsVisible must be called on map load")
    assert(mockHud.lastVisibleParam == false, "hud:setIsVisible(false) must be called on map load")
    assert(mockHud.isVisible == false, "mockHud.isVisible must be false")

    -- Check VR_HUD_TOGGLE action was registered
    local foundToggleAction = false
    for _, reg in ipairs(registeredActions) do
        if reg.actionName == "VR_HUD_TOGGLE" then
            foundToggleAction = true
            assert(reg.callback == VRCompanion.onHudToggleAction, "Callback must be onHudToggleAction")
        end
    end
    assert(foundToggleAction, "VR_HUD_TOGGLE action must be registered in loadMap")
end)

test("DeleteMapRestoresFullHudVisibility", function()
    local mockHud = {
        isVisible = false,
        setIsVisibleCalled = false,
        lastVisibleParam = nil,
        setIsVisible = function(self, vis)
            self.setIsVisibleCalled = true
            self.lastVisibleParam = vis
            self.isVisible = vis
        end,
        speedMeter = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        inputHelp = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        gameInfo = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        contextAction = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end },
        ingameMap = { isVisible = false, setIsVisible = function(self, v) self.isVisible = v end }
    }
    _G.g_currentMission = { hud = mockHud }

    VRCompanion:loadMap("mapUS")
    VRCompanion:deleteMap()

    assert(mockHud.setIsVisibleCalled == true, "hud:setIsVisible must be called on map delete")
    assert(mockHud.lastVisibleParam == true, "hud:setIsVisible(true) must be called on map delete")
    assert(mockHud.isVisible == true, "mockHud.isVisible must be true on map delete")
    assert(mockHud.speedMeter.isVisible == true, "speedMeter must be restored on map delete")
    assert(mockHud.inputHelp.isVisible == true, "inputHelp must be restored on map delete")
end)

-- Slice 5: GUI menu transition preserves user-selected HUD state
test("PreservesSelectedHudStateWhenTransitioningThroughGuiMenus", function()
    local mockHud = {
        isVisible = true,
        setIsVisible = function(self, vis) self.isVisible = vis end,
        speedMeter = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        inputHelp = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        gameInfo = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        contextAction = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end },
        ingameMap = { isVisible = true, setIsVisible = function(self, v) self.isVisible = v end }
    }
    _G.g_currentMission = { hud = mockHud }

    local guiVisible = false
    _G.g_gui = {
        getIsGuiVisible = function() return guiVisible end
    }

    VRCompanion:loadMap("mapUS")

    -- 1. User cycles to Streamlined (1)
    VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
    assert(VRCompanion:getHudState() == 1, "User selected Streamlined (1)")
    assert(mockHud.speedMeter.isVisible == true, "Speedmeter visible")
    assert(mockHud.inputHelp.isVisible == false, "inputHelp hidden")

    -- 2. User enters ESC pause menu or dealership GUI
    guiVisible = true
    VRCompanion:update(16)
    assert(VRCompanion.isMenuVisible == true, "Companion detected menu visible")
    assert(VRCompanion:getHudState() == 1, "hudState preserved as Streamlined during menu open")

    -- Simulate vanilla engine / screen altering HUD during menu
    mockHud.inputHelp.isVisible = true

    -- 3. User exits menu back to active 3D gameplay
    guiVisible = false
    VRCompanion:update(16)
    assert(VRCompanion.isMenuVisible == false, "Companion detected menu closed")
    assert(VRCompanion:getHudState() == 1, "hudState must still be 1 (Streamlined)")
    assert(mockHud.isVisible == true, "HUD must be visible in active gameplay")
    assert(mockHud.speedMeter.isVisible == true, "Speedmeter still visible in Streamlined mode")
    assert(mockHud.inputHelp.isVisible == false, "inputHelp preserved as hidden in Streamlined mode")
    assert(mockHud.ingameMap.isVisible == false, "ingameMap preserved as hidden in Streamlined mode")

    -- 4. User cycles to Hidden (0)
    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    assert(VRCompanion:getHudState() == 0, "User selected Hidden (0)")
    assert(mockHud.isVisible == false, "HUD hidden in state 0")

    -- Enter GUI menu again
    guiVisible = true
    VRCompanion:update(16)
    -- Simulate vanilla restoring HUD to visible on menu close
    mockHud.isVisible = true

    -- Exit GUI menu back to active 3D gameplay
    guiVisible = false
    VRCompanion:update(16)
    assert(VRCompanion:getHudState() == 0, "hudState preserved as Hidden (0)")
    assert(mockHud.isVisible == false, "HUD must remain hidden after exiting menu")
end)

-- Slice 6: Component visibility hooks into native engine HUD display interfaces (Issue #26)
test("HooksNativeEngineHudDisplaysAndSuppressesFilteredComponentsOnDraw", function()
    -- Set up mock global display classes
    local draws = {
        HUD = 0,
        SpeedMeterDisplay = 0,
        ContextActionDisplay = 0,
        FillLevelsDisplay = 0,
        IngameMap = 0,
        InputHelpDisplay = 0,
        GameInfoDisplay = 0
    }

    _G.HUD = {
        draw = function(self) draws.HUD = draws.HUD + 1 end,
        setInputHelpVisible = function(self, vis) self.inputHelpVisible = vis end,
        setIngameMapVisible = function(self, vis) self.ingameMapVisible = vis end
    }
    _G.SpeedMeterDisplay = {
        draw = function(self) draws.SpeedMeterDisplay = draws.SpeedMeterDisplay + 1 end,
        getIsVisible = function(self) return true end
    }
    _G.ContextActionDisplay = {
        draw = function(self) draws.ContextActionDisplay = draws.ContextActionDisplay + 1 end,
        getIsVisible = function(self) return true end
    }
    _G.FillLevelsDisplay = {
        draw = function(self) draws.FillLevelsDisplay = draws.FillLevelsDisplay + 1 end,
        getIsVisible = function(self) return true end
    }
    _G.IngameMap = {
        draw = function(self) draws.IngameMap = draws.IngameMap + 1 end,
        getIsVisible = function(self) return true end
    }
    _G.InputHelpDisplay = {
        draw = function(self) draws.InputHelpDisplay = draws.InputHelpDisplay + 1 end,
        getIsVisible = function(self) return true end
    }
    _G.GameInfoDisplay = {
        draw = function(self) draws.GameInfoDisplay = draws.GameInfoDisplay + 1 end,
        getIsVisible = function(self) return true end
    }

    -- 1. Hook HUD
    VRCompanion:hookHud()
    assert(VRCompanion.hudHooked == true, "hudHooked must be true after hookHud()")

    -- 2. Test Streamlined state (1):
    VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
    draws = { HUD = 0, SpeedMeterDisplay = 0, ContextActionDisplay = 0, FillLevelsDisplay = 0, IngameMap = 0, InputHelpDisplay = 0, GameInfoDisplay = 0 }

    _G.SpeedMeterDisplay:draw()
    _G.ContextActionDisplay:draw()
    _G.FillLevelsDisplay:draw()
    _G.InputHelpDisplay:draw()
    _G.IngameMap:draw()
    _G.GameInfoDisplay:draw()
    _G.HUD:draw()

    assert(draws.SpeedMeterDisplay == 1, "SpeedMeterDisplay:draw must execute in Streamlined mode")
    assert(draws.ContextActionDisplay == 1, "ContextActionDisplay:draw must execute in Streamlined mode")
    assert(draws.FillLevelsDisplay == 1, "FillLevelsDisplay:draw must execute in Streamlined mode")
    assert(draws.InputHelpDisplay == 0, "InputHelpDisplay:draw must be suppressed in Streamlined mode")
    assert(draws.IngameMap == 0, "IngameMap:draw must be suppressed in Streamlined mode")
    assert(draws.GameInfoDisplay == 0, "GameInfoDisplay:draw must be suppressed in Streamlined mode")
    assert(draws.HUD == 1, "HUD:draw must execute in Streamlined mode")

    assert(_G.SpeedMeterDisplay:getIsVisible() == true, "SpeedMeterDisplay:getIsVisible must be true")
    assert(_G.ContextActionDisplay:getIsVisible() == true, "ContextActionDisplay:getIsVisible must be true")
    assert(_G.InputHelpDisplay:getIsVisible() == false, "InputHelpDisplay:getIsVisible must be false")
    assert(_G.IngameMap:getIsVisible() == false, "IngameMap:getIsVisible must be false")
    assert(_G.GameInfoDisplay:getIsVisible() == false, "GameInfoDisplay:getIsVisible must be false")

    -- 3. Test Hidden state (0):
    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    draws = { HUD = 0, SpeedMeterDisplay = 0, ContextActionDisplay = 0, FillLevelsDisplay = 0, IngameMap = 0, InputHelpDisplay = 0, GameInfoDisplay = 0 }

    _G.SpeedMeterDisplay:draw()
    _G.ContextActionDisplay:draw()
    _G.FillLevelsDisplay:draw()
    _G.InputHelpDisplay:draw()
    _G.IngameMap:draw()
    _G.GameInfoDisplay:draw()
    _G.HUD:draw()

    assert(draws.SpeedMeterDisplay == 0, "SpeedMeterDisplay:draw must be suppressed in Hidden mode")
    assert(draws.ContextActionDisplay == 0, "ContextActionDisplay:draw must be suppressed in Hidden mode")
    assert(draws.FillLevelsDisplay == 0, "FillLevelsDisplay:draw must be suppressed in Hidden mode")
    assert(draws.InputHelpDisplay == 0, "InputHelpDisplay:draw must be suppressed in Hidden mode")
    assert(draws.IngameMap == 0, "IngameMap:draw must be suppressed in Hidden mode")
    assert(draws.GameInfoDisplay == 0, "GameInfoDisplay:draw must be suppressed in Hidden mode")
    assert(draws.HUD == 0, "HUD:draw must be suppressed in Hidden mode")

    assert(_G.SpeedMeterDisplay:getIsVisible() == false, "SpeedMeterDisplay:getIsVisible must be false in Hidden mode")
    assert(_G.IngameMap:getIsVisible() == false, "IngameMap:getIsVisible must be false in Hidden mode")

    -- 4. Test Full state (2):
    VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
    draws = { HUD = 0, SpeedMeterDisplay = 0, ContextActionDisplay = 0, FillLevelsDisplay = 0, IngameMap = 0, InputHelpDisplay = 0, GameInfoDisplay = 0 }

    _G.SpeedMeterDisplay:draw()
    _G.ContextActionDisplay:draw()
    _G.FillLevelsDisplay:draw()
    _G.InputHelpDisplay:draw()
    _G.IngameMap:draw()
    _G.GameInfoDisplay:draw()
    _G.HUD:draw()

    assert(draws.SpeedMeterDisplay == 1, "SpeedMeterDisplay:draw must execute in Full mode")
    assert(draws.ContextActionDisplay == 1, "ContextActionDisplay:draw must execute in Full mode")
    assert(draws.FillLevelsDisplay == 1, "FillLevelsDisplay:draw must execute in Full mode")
    assert(draws.InputHelpDisplay == 1, "InputHelpDisplay:draw must execute in Full mode")
    assert(draws.IngameMap == 1, "IngameMap:draw must execute in Full mode")
    assert(draws.GameInfoDisplay == 1, "GameInfoDisplay:draw must execute in Full mode")
    assert(draws.HUD == 1, "HUD:draw must execute in Full mode")

    assert(_G.InputHelpDisplay:getIsVisible() == true, "InputHelpDisplay:getIsVisible must be true in Full mode")
    assert(_G.IngameMap:getIsVisible() == true, "IngameMap:getIsVisible must be true in Full mode")
    assert(_G.GameInfoDisplay:getIsVisible() == true, "GameInfoDisplay:getIsVisible must be true in Full mode")

    -- Clean up hooks
    VRCompanion:unhookHud()
    assert(VRCompanion.hudHooked == false, "hudHooked must be false after unhookHud()")
end)

test("DrawVisibilityAndHudSetterHooksHonorGroupSettings", function()
    withGroupVisibilityHarness(function()
        local draws = { IngameMap = 0, GameInfoDisplay = 0 }
        local setterArgs = {}
        _G.IngameMap = { draw = function() draws.IngameMap = draws.IngameMap + 1 end, getIsVisible = function() return true end }
        _G.GameInfoDisplay = { draw = function() draws.GameInfoDisplay = draws.GameInfoDisplay + 1 end, getIsVisible = function() return true end }
        _G.HUD = {
            draw = function() end,
            setIngameMapVisible = function(self, vis) setterArgs.map = vis end,
            setGameInfoVisible = function(self, vis) setterArgs.info = vis end
        }
        VRCompanion:hookHud()

        -- Map on in Streamlined, info off in Full
        VRCompanion.HUD_GROUPS.map.streamlined = true
        VRCompanion.HUD_GROUPS.info.full = false

        VRCompanion.hudState = VRCompanion.HUD_STATE_STREAMLINED
        _G.IngameMap:draw()
        _G.HUD:setIngameMapVisible(true)
        assert(draws.IngameMap == 1, "the map draws in Streamlined when its group is on")
        assert(_G.IngameMap:getIsVisible() == true, "the map reports visible in Streamlined when its group is on")
        assert(setterArgs.map == true, "the HUD setter passes the engine's value through for an enabled group")

        VRCompanion.hudState = VRCompanion.HUD_STATE_FULL
        _G.GameInfoDisplay:draw()
        _G.HUD:setGameInfoVisible(true)
        assert(draws.GameInfoDisplay == 0, "the info display is suppressed in Full when its group is off")
        assert(_G.GameInfoDisplay:getIsVisible() == false, "the info display reports hidden in Full when its group is off")
        assert(setterArgs.info == false, "the HUD setter forces an off group hidden")

        VRCompanion:unhookHud()
        _G.IngameMap, _G.GameInfoDisplay, _G.HUD = nil, nil, nil
    end)
end)

-- The engine builds the map's layout in the screen corner and draws its content, ring and north arrow
-- there at absolute overlay positions that setPosition does not move, so the draw itself is shifted from
-- the engine's corner to the Map group's position (#56).
test("MapDrawIsShiftedFromTheEnginesCornerToTheMapGroup", function()
    withGroupVisibilityHarness(function()
        local savedLayout = VRCompanion.hudGroupLayout
        local savedOffset, saved2D = VRCompanion.currentConvergenceOffset, VRCompanion.is2DViewActive
        _G.g_safeFrameOffsetX, _G.g_safeFrameOffsetY = 0.013, 0.0156
        local rendered = {}
        local origRender = function(overlay, cx1, cy1, cx2, cy2)
            table.insert(rendered, { x = overlay.x, y = overlay.y, cx1 = cx1, cy1 = cy1, cx2 = cx2, cy2 = cy2 })
        end
        _G.Overlay = { render = origRender }
        local content = { x = -0.2637, y = -0.441 }
        local ring = { x = 0.0156, y = 0.0187 }
        local origDrawAfter = function() _G.Overlay.render(ring) end
        _G.IngameMapLayoutCircle = { drawAfter = origDrawAfter }
        _G.IngameMap = {
            draw = function(self)
                _G.Overlay.render(content, 0.02, 0.03, 0.12, 0.14)
                _G.IngameMapLayoutCircle.drawAfter({})
            end,
            getIsVisible = function() return true end
        }
        _G.g_currentMission.hud = { ingameMap = _G.IngameMap }
        VRCompanion:hookHud()
        VRCompanion:setHudGroupLayout("map", 0.12, 0.34)
        VRCompanion.currentConvergenceOffset = 0.01
        VRCompanion.is2DViewActive = false
        VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

        local function near(a, b) return math.abs(a - b) < 0.0001 end
        local function drawMap()
            rendered = {}
            _G.IngameMap:draw()
            return rendered[1], rendered[2]
        end

        -- Shifted by the Map group's position (margins plus convergence offset) minus the engine's corner
        local c, r = drawMap()
        assert(near(c.x, -0.2637 + 0.117) and near(c.y, -0.441 + 0.3244), "the map content is shifted to the Map group")
        assert(near(c.cx1, 0.02 + 0.117) and near(c.cy1, 0.03 + 0.3244) and near(c.cx2, 0.12 + 0.117) and near(c.cy2, 0.14 + 0.3244),
            "the clip rectangle is shifted with the content")
        assert(near(r.x, 0.0156 + 0.117) and near(r.y, 0.0187 + 0.3244), "the layout's ring is shifted with the content")
        assert(content.x == -0.2637 and content.y == -0.441 and ring.x == 0.0156 and ring.y == 0.0187,
            "the overlays are put back where the engine had them")

        -- Draws outside the map's own methods are not shifted
        rendered = {}
        _G.Overlay.render(ring)
        assert(near(rendered[1].x, 0.0156) and near(rendered[1].y, 0.0187), "other overlays are drawn where the engine put them")

        -- Changing the Map group's margins moves the map
        VRCompanion:setHudGroupLayout("map", 0.2, 0.25)
        c, r = drawMap()
        assert(near(r.x, 0.0156 + (0.21 - 0.013)) and near(r.y, 0.0187 + (0.25 - 0.0156)), "changing the Map group's margins moves the map")

        -- Fullscreen, 2D Floating Window Mode and the Hidden HUD keep the engine's position
        _G.IngameMap.isFullscreen = true
        c, r = drawMap()
        assert(near(r.x, 0.0156) and near(r.y, 0.0187), "the fullscreen map keeps the engine's position")
        _G.IngameMap.isFullscreen = false
        VRCompanion.is2DViewActive = true
        c, r = drawMap()
        assert(near(r.x, 0.0156) and near(r.y, 0.0187), "2D Floating Window Mode keeps the engine's position")
        VRCompanion.is2DViewActive = false
        VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
        c, r = drawMap()
        assert(c == nil, "the Hidden HUD draws no map")
        VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

        -- A draw that fails leaves later draws unshifted outside the map
        VRCompanion:unhookHud()
        _G.IngameMapLayoutCircle.drawAfter = function() error("draw failed") end
        VRCompanion:hookHud()
        assert(not pcall(function() _G.IngameMap:draw() end), "an error in the map's draw still propagates")
        rendered = {}
        _G.Overlay.render(ring)
        assert(near(rendered[1].x, 0.0156), "overlays outside the map are not shifted after a failed map draw")

        VRCompanion:unhookHud()
        assert(_G.Overlay.render == origRender, "unhooking puts the overlay renderer back")
        _G.IngameMapLayoutCircle.drawAfter = origDrawAfter
        _G.IngameMap, _G.IngameMapLayoutCircle, _G.Overlay = nil, nil, nil
        _G.g_safeFrameOffsetX, _G.g_safeFrameOffsetY = nil, nil
        VRCompanion.hudGroupLayout = savedLayout
        VRCompanion.currentConvergenceOffset, VRCompanion.is2DViewActive = savedOffset, saved2D
    end)
end)

test("UnhookHudRestoresOriginalFunctionsCleanly", function()
    local origDrawCalled = false
    local origDraw = function(self) origDrawCalled = true end

    _G.InputHelpDisplay = { draw = origDraw }
    _G.HUD = { draw = function(self) end }

    VRCompanion:hookHud()
    assert(VRCompanion.hudHooked == true)
    assert(_G.InputHelpDisplay.draw ~= origDraw, "draw function must be replaced by hook")

    VRCompanion:unhookHud()
    assert(VRCompanion.hudHooked == false)
    assert(_G.InputHelpDisplay.draw == origDraw, "Original draw function must be restored")

    _G.InputHelpDisplay:draw()
    assert(origDrawCalled == true, "Original draw function must be callable after unhook")
end)

test("LoadMapAndLifecycleIntegratesHudHooking", function()
    _G.HUD = { draw = function(self) end }
    _G.SpeedMeterDisplay = { draw = function(self) end }
    _G.InputHelpDisplay = { draw = function(self) end }

    VRCompanion:loadMap("mapUS")
    assert(VRCompanion.hudHooked == true, "loadMap must hook HUD displays")

    VRCompanion:deleteMap()
    assert(VRCompanion.hudHooked == false, "deleteMap must unhook HUD displays")
end)

-- Bindings live in modDesc.xml and reach the mod only through g_inputBinding action events.
-- A keyEvent on the mod listener would act on raw keys and ignore rebinding in the Controls menu.
test("ModHasNoRawKeyEventHandlerSoBindingsComeOnlyFromInputBinding", function()
    assert(VRCompanion.keyEvent == nil, "VRCompanion must not define keyEvent")
    assert(VRCompanion.Input.keyEvent == nil, "VRCompanion.Input must not export keyEvent")
end)

test("RegisterActionEventsReArmsOnVehicleTransitionsAndPeriodicUpdate", function()
    local registrations = 0
    _G.g_inputBinding = {
        events = {},
        actionEvents = {}, -- FS25: keyed by action name, not event id
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            registrations = registrations + 1
            local id = actionName .. "|table: 0x0|" .. registrations
            local ev = { actionName = actionName, callback = callback, target = target }
            self.events[id] = ev
            self.actionEvents[actionName] = { ev }
            return true, id
        end,
        setActionEventTextPriority = function(self, id, prio) end,
        setActionEventActive = function(self, id, active) end,
        setActionEventTextVisibility = function(self, id, vis) end,
        setActionEventText = function(self, id, txt) end
    }

    VRCompanion.hudToggleActionEventId = nil
    VRCompanion.recenterActionEventId = nil
    VRCompanion.snapLeftActionEventId = nil
    VRCompanion.snapRightActionEventId = nil
    VRCompanion.toggleViewActionEventId = nil
    VRCompanion:registerActionEvents(true)
    assert(registrations == 5, "Should register HUD_TOGGLE, RECENTER, SNAP_LEFT, SNAP_RIGHT, and TOGGLE_VIEW")
    assert(VRCompanion.hudToggleActionEventId ~= nil)
    assert(VRCompanion.recenterActionEventId ~= nil)
    assert(VRCompanion.snapLeftActionEventId ~= nil)
    assert(VRCompanion.snapRightActionEventId ~= nil)
    assert(VRCompanion.toggleViewActionEventId ~= nil)

    -- With all five events still registered, the periodic check registers nothing
    VRCompanion:registerActionEvents(false)
    assert(registrations == 5, "Periodic check must not re-register events that are still registered")

    -- Simulate engine dropping the events upon entering a vehicle
    _G.g_inputBinding.events = {}
    _G.g_inputBinding.actionEvents = {}

    -- Calling registerActionEvents() detects missing events and re-registers
    VRCompanion:registerActionEvents(false)
    assert(registrations == 10, "Should automatically re-register missing action events")

    _G.g_inputBinding = nil
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d tests failed", failed))
end
