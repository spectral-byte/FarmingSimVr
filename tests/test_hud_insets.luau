-- ====================================================================
-- Test Suite for VR HUD Insets & UI Scale (Issue #24)
-- ====================================================================

-- Mock GIANTS Engine globals before loading VRCompanion
_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

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

print("Running VR HUD Inset Tests...")

-- Mock display that records the last position and scale it was given
local function makeDisplay(width, height)
    return {
        width = width, height = height, position = nil, scale = nil,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end,
        setScale = function(self, s) self.scale = s end
    }
end

local function makeHud()
    return {
        ingameMap = makeDisplay(0.20, 0.20),
        speedMeter = makeDisplay(0.15, 0.10),
        gameInfo = makeDisplay(0.20, 0.12),
        inputHelp = makeDisplay(0.25, 0.30),
        contextAction = makeDisplay(0.30, 0.04)
    }
end

local DISPLAY_NAMES = { "ingameMap", "speedMeter", "gameInfo", "inputHelp", "contextAction" }
local function near(a, b) return a ~= nil and b ~= nil and math.abs(a - b) < 0.0001 end

-- Every setting back to its default (a missing settings file yields the defaults)
local function resetHudSettings()
    VRCompanion:loadConfiguration("non_existent_file.xml")
end

local function setGroup(name, horizontal, vertical, scale)
    return VRCompanion:consoleCommandSetHudGroup(name, horizontal and tostring(horizontal), vertical and tostring(vertical), scale and tostring(scale))
end

local function loadMapWith(hud)
    _G.g_hudAnchorLeft = 0.0
    _G.g_hudAnchorRight = 1.0
    _G.g_hudAnchorBottom = 0.0
    _G.g_hudAnchorTop = 1.0
    _G.g_currentMission = { hud = hud }
    resetHudSettings()
    VRCompanion:loadMap("mapUS")
end

local function unloadMap()
    VRCompanion:deleteMap()
    _G.g_currentMission = nil
    resetHudSettings()
end

-- Slice 1: HUD Safe Area anchors (the engine's global HUD anchors), default values, and clamp boundaries
test("CalculatesDefaultHudAnchors", function()
    assert(VRCompanion.DEFAULT_HUD_INSET_X == 0.30, "DEFAULT_HUD_INSET_X must be 0.30")
    assert(VRCompanion.DEFAULT_HUD_INSET_Y == 0.22, "DEFAULT_HUD_INSET_Y must be 0.22")

    local left, right, bottom, top = VRCompanion:calculateHudAnchors()
    assert(near(left, 0.30), "Default left anchor must be 0.30")
    assert(near(right, 0.70), "Default right anchor must be 0.70")
    assert(near(bottom, 0.22), "Default bottom anchor must be 0.22")
    assert(near(top, 0.78), "Default top anchor must be 0.78")
end)

test("CalculatesCustomHudAnchorsCorrectly", function()
    local left, right, bottom, top = VRCompanion:calculateHudAnchors(0.10, 0.05)
    assert(near(left, 0.10), "Custom left anchor must be 0.10")
    assert(near(right, 0.90), "Custom right anchor must be 0.90")
    assert(near(bottom, 0.05), "Custom bottom anchor must be 0.05")
    assert(near(top, 0.95), "Custom top anchor must be 0.95")
end)

test("ClampsHudAnchorsWithinSafeBoundaries", function()
    local leftMin, rightMin, bottomMin, topMin = VRCompanion:calculateHudAnchors(-0.10, -0.05)
    assert(near(leftMin, 0.0), "Negative insetX clamped to 0.0")
    assert(near(rightMin, 1.0), "Right anchor clamped to 1.0")
    assert(near(bottomMin, 0.0), "Negative insetY clamped to 0.0")
    assert(near(topMin, 1.0), "Top anchor clamped to 1.0")

    local leftMax, rightMax, bottomMax, topMax = VRCompanion:calculateHudAnchors(0.50, 0.45)
    assert(near(leftMax, 0.40), "Excessive insetX clamped to 0.40")
    assert(near(rightMax, 0.60), "Right anchor clamped to 0.60")
    assert(near(bottomMax, 0.35), "Excessive insetY clamped to 0.35")
    assert(near(topMax, 0.65), "Top anchor clamped to 0.65")
end)

-- Slice 2: Configuration API and metadata / XML fallback
test("ConfiguresHudInsetsAndQueriesState", function()
    VRCompanion:configureHudInsets(0.25, 0.18)
    local insets = VRCompanion:getHudInsets()

    assert(near(insets.left, 0.25), "getHudInsets().left must match")
    assert(near(insets.right, 0.75), "getHudInsets().right must match")
    assert(near(insets.bottom, 0.18), "getHudInsets().bottom must match")
    assert(near(insets.top, 0.82), "getHudInsets().top must match")
    assert(near(insets.insetX, 0.25), "getHudInsets().insetX must match")
    assert(near(insets.insetY, 0.18), "getHudInsets().insetY must match")
    assert(insets.scaleMultiplier == nil, "the single UI scale multiplier is gone")
    resetHudSettings()
end)

test("LoadsConfigurationWithGracefulFallbackWhenUnspecified", function()
    VRCompanion.hudInsetX = nil
    VRCompanion.hudInsetY = nil
    VRCompanion.hudInsetTop = nil

    local insets = VRCompanion:loadConfiguration("non_existent_file.xml")
    assert(near(insets.left, 0.30), "Must fall back to default left 0.30")
    assert(near(insets.right, 0.70), "Must fall back to default right 0.70")
    assert(near(insets.bottom, 0.22), "Must fall back to default bottom 0.22")
    assert(near(insets.top, 0.78), "Must fall back to default top 0.78")
end)

test("LoadsConfigurationFromXmlWhenSpecified", function()
    _G.mockXmlData = {
        ["vrSettings.hudInsetHorizontal"] = 0.22,
        ["vrSettings.hudInsetVertical"] = 0.15,
        ["vrSettings.hudInsetTop"] = 0.15
    }
    _G.loadXMLFile = function(tag, path) return 123 end
    _G.getXMLFloat = function(xmlFile, key)
        return _G.mockXmlData[key]
    end
    _G.delete = function(xmlFile) end

    local insets = VRCompanion:loadConfiguration("mockSettings.xml")
    assert(near(insets.left, 0.22), "Loaded left anchor must be 0.22")
    assert(near(insets.right, 0.78), "Loaded right anchor must be 0.78")
    assert(near(insets.bottom, 0.15), "Loaded bottom anchor must be 0.15")
    assert(near(insets.top, 0.85), "Loaded top anchor must be 0.85")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    _G.mockXmlData = nil
    resetHudSettings()
end)

-- Slice 3: HUD Groups are placed by their own HUD Group Margins
test("DefinesTheGroupMarginAndScaleDefaultsOnce", function()
    assert(VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X == 0.30, "horizontal group margin default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y == 0.22, "vertical group margin default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_SCALE == 0.82, "group scale default")
    assert(VRCompanion.MIN_HUD_GROUP_MARGIN == 0.0 and VRCompanion.MAX_HUD_GROUP_MARGIN == 0.45, "group margins clamp to 0-0.45")
    assert(VRCompanion.MIN_HUD_GROUP_SCALE == 0.50 and VRCompanion.MAX_HUD_GROUP_SCALE == 1.50, "group scale clamps to 0.50-1.50")
    assert(VRCompanion.DEFAULT_HUD_UI_SCALE == nil and VRCompanion.MIN_HUD_UI_SCALE == nil and VRCompanion.MAX_HUD_UI_SCALE == nil,
        "the single UI scale multiplier constants are removed")
end)

test("PlacesEveryGroupAtItsDefaultMarginsUponMapLoad", function()
    local hud = makeHud()
    loadMapWith(hud)

    -- The Safe Area still drives the engine's global anchors
    assert(near(_G.g_hudAnchorLeft, 0.30), "g_hudAnchorLeft must be 0.30 upon map load")
    assert(near(_G.g_hudAnchorRight, 0.70), "g_hudAnchorRight must be 0.70 upon map load")
    assert(near(_G.g_hudAnchorBottom, 0.22), "g_hudAnchorBottom must be 0.22 upon map load")
    assert(near(_G.g_hudAnchorTop, 0.78), "g_hudAnchorTop must be 0.78 upon map load")

    -- Map group, bottom-left: left and bottom edges sit at the margins
    assert(near(hud.ingameMap.position.x, 0.30) and near(hud.ingameMap.position.y, 0.22), "map at its bottom-left margins")
    -- Speedometer group, bottom-right: the display's width is subtracted from 1 - margin
    assert(near(hud.speedMeter.position.x, 1.0 - 0.30 - 0.15) and near(hud.speedMeter.position.y, 0.22), "speedometer at its bottom-right margins")
    -- Info group, top-right: width and height are subtracted
    assert(near(hud.gameInfo.position.x, 1.0 - 0.30 - 0.20) and near(hud.gameInfo.position.y, 1.0 - 0.22 - 0.12), "info at its top-right margins")
    -- Controls group, top-left: the height is subtracted
    assert(near(hud.inputHelp.position.x, 0.30) and near(hud.inputHelp.position.y, 1.0 - 0.22 - 0.30), "controls at its top-left margins")
    -- The context action prompt is centered on the Speedometer group's bottom margin
    assert(near(hud.contextAction.position.x, 0.5) and near(hud.contextAction.position.y, 0.22), "context action centered at the speedometer bottom margin")

    for _, name in ipairs(DISPLAY_NAMES) do
        local display = hud[name]
        assert(near(display.scale, 0.82), name .. " must be drawn at the default group scale 0.82")
    end
    unloadMap()
end)

test("ChangingOneGroupsMarginsMovesOnlyThatGroup", function()
    local hud = makeHud()
    loadMapWith(hud)
    local before = {}
    for _, name in ipairs(DISPLAY_NAMES) do local display = hud[name]; before[name] = { x = display.position.x, y = display.position.y, scale = display.scale } end

    local result = setGroup("info", 0.10, 0.05)
    assert(type(result) == "string" and not string.find(result, "Usage"), "valid command must confirm: " .. tostring(result))

    assert(near(hud.gameInfo.position.x, 1.0 - 0.10 - 0.20), "info outer right edge sits at its new horizontal margin")
    assert(near(hud.gameInfo.position.y, 1.0 - 0.05 - 0.12), "info outer top edge sits at its new vertical margin")
    for _, name in ipairs({ "ingameMap", "speedMeter", "inputHelp", "contextAction" }) do
        local d = hud[name]
        assert(near(d.position.x, before[name].x) and near(d.position.y, before[name].y) and near(d.scale, before[name].scale),
            name .. " must not move when only the info group changes")
    end
    unloadMap()
end)

test("EachGroupMeasuresItsMarginsFromItsOwnHomeCorner", function()
    local hud = makeHud()
    loadMapWith(hud)
    setGroup("map", 0.05, 0.10)
    setGroup("speedometer", 0.07, 0.12)
    setGroup("controls", 0.09, 0.14)
    assert(near(hud.ingameMap.position.x, 0.05) and near(hud.ingameMap.position.y, 0.10), "map: left/bottom")
    assert(near(hud.speedMeter.position.x, 1.0 - 0.07 - 0.15) and near(hud.speedMeter.position.y, 0.12), "speedometer: right/bottom")
    assert(near(hud.inputHelp.position.x, 0.09) and near(hud.inputHelp.position.y, 1.0 - 0.14 - 0.30), "controls: left/top")
    unloadMap()
end)

test("ClampsGroupMarginsAndScaleToTheirRanges", function()
    local hud = makeHud()
    loadMapWith(hud)
    setGroup("map", -1, 5, 9)
    assert(near(hud.ingameMap.position.x, 0.0), "negative horizontal margin clamps to 0")
    assert(near(hud.ingameMap.position.y, 0.45), "excessive vertical margin clamps to 0.45")
    assert(near(hud.ingameMap.scale, 1.50), "excessive scale clamps to 1.50")
    setGroup("map", 0.45, 0.0, 0.01)
    assert(near(hud.ingameMap.position.x, 0.45) and near(hud.ingameMap.position.y, 0.0), "margins at the range ends are kept")
    assert(near(hud.ingameMap.scale, 0.50), "tiny scale clamps to 0.50")
    unloadMap()
end)

test("GroupScaleCombinesWithTheGamesUiScale", function()
    _G.GameSettings = { SETTING = { UI_SCALE = "uiScale" } }
    _G.g_gameSettings = { getValue = function(self, key) if key == "uiScale" then return 1.20 end return 1.0 end }
    local hud = makeHud()
    loadMapWith(hud)

    assert(near(hud.ingameMap.scale, 1.20 * 0.82), "default group scale is multiplied by the UI scale setting")
    setGroup("speedometer", 0.30, 0.22, 0.90)
    assert(near(hud.speedMeter.scale, 1.20 * 0.90), "the speedometer group's own scale is multiplied by the UI scale setting")
    assert(near(hud.contextAction.scale, 1.20 * 0.90), "the context action prompt follows the speedometer group's scale")
    assert(near(hud.ingameMap.scale, 1.20 * 0.82), "other groups keep their own scale")

    -- Unloading restores the game's own UI scale
    unloadMap()
    assert(near(hud.speedMeter.scale, 1.20), "map unload restores the base UI scale")
    _G.GameSettings = nil
    _G.g_gameSettings = nil
end)

test("ContextActionStaysCenteredOnTheSpeedometerGroupsBottomMargin", function()
    local hud = makeHud()
    loadMapWith(hud)
    setGroup("speedometer", 0.10, 0.31)
    assert(near(hud.contextAction.position.x, 0.5) and near(hud.contextAction.position.y, 0.31), "follows the speedometer group's bottom margin")
    setGroup("map", 0.02, 0.02)
    setGroup("info", 0.02, 0.02)
    setGroup("controls", 0.02, 0.02)
    assert(near(hud.contextAction.position.x, 0.5) and near(hud.contextAction.position.y, 0.31), "ignores the other groups' margins")
    unloadMap()
end)

test("TwoDFloatingWindowModeKeepsTheEnginesLayoutWhenAGroupChanges", function()
    local hud = makeHud()
    loadMapWith(hud)
    VRCompanion:set2DViewActive(true)
    for _, name in ipairs(DISPLAY_NAMES) do hud[name].position = nil end

    setGroup("map", 0.10, 0.10)
    for _, name in ipairs(DISPLAY_NAMES) do
        local display = hud[name]
        assert(display.position == nil, name .. " must not be placed in 2D Floating Window Mode")
    end

    -- Back in the stereoscopic view the saved margins apply
    VRCompanion:set2DViewActive(false)
    assert(near(hud.ingameMap.position.x, 0.10) and near(hud.ingameMap.position.y, 0.10), "the margins set in 2D apply on returning to VR")
    VRCompanion:set2DViewActive(false)
    unloadMap()
end)

test("PositionsIndividualDisplaysViaHelper", function()
    resetHudSettings()
    local sm = { width = 0.15, storeScaledValues = function(self) self.stored = true end }
    VRCompanion:positionHudDisplay(sm, "SpeedMeterDisplay")
    assert(sm.stored == true, "positionHudDisplay must trigger storeScaledValues")
    assert(near(sm.anchorX, 0.55), "SpeedMeter anchorX must be 1 - 0.30 - width")
    assert(near(sm.anchorY, 0.22), "SpeedMeter anchorY must be 0.22")

    local ih = { height = 0.30 }
    VRCompanion:positionHudDisplay(ih, "InputHelpDisplay")
    assert(near(ih.anchorX, 0.30), "InputHelp anchorX must be 0.30")
    assert(near(ih.anchorY, 0.48), "InputHelp anchorY must be 1 - 0.22 - height")

    local gi = {}
    VRCompanion:positionHudDisplay(gi, "GameInfoDisplay")
    assert(near(gi.anchorX, 0.70), "GameInfo anchorX must be 0.70")
    assert(near(gi.anchorY, 0.78), "GameInfo anchorY must be 0.78")

    local ca = {}
    VRCompanion:positionHudDisplay(ca, "ContextActionDisplay")
    assert(near(ca.anchorX, 0.5), "ContextAction anchorX must be centered")
    assert(near(ca.anchorY, 0.22), "ContextAction anchorY must be the speedometer bottom margin")

    local map = {}
    VRCompanion:positionHudDisplay(map, "IngameMap")
    assert(near(map.anchorX, 0.30), "IngameMap anchorX must be 0.30")
    assert(near(map.anchorY, 0.22), "IngameMap anchorY must be 0.22")
end)

-- HUD Groups (spec #50, ticket #51): a display's home corner comes from its group
test("PlacesADisplayAtItsGroupsHomeCorner", function()
    resetHudSettings()
    local info = VRCompanion.HUD_GROUPS.info
    local origX, origY = info.alignX, info.alignY
    local ok, err = pcall(function()
        info.alignX, info.alignY = "left", "bottom"
        local gi = { width = 0.20, height = 0.12 }
        VRCompanion:positionHudDisplay(gi, "GameInfoDisplay")
        assert(near(gi.anchorX, 0.30), "Info display follows its group's left edge")
        assert(near(gi.anchorY, 0.22), "Info display follows its group's bottom edge")
    end)
    info.alignX, info.alignY = origX, origY
    assert(ok, err)
end)

test("RestoresOriginalHudAnchorsAndUiScaleOnMapUnload", function()
    _G.GameSettings = { SETTING = { UI_SCALE = "uiScale" } }
    _G.g_gameSettings = {
        getValue = function(self, key)
            if key == "uiScale" then return 1.20 end
            return 1.0
        end
    }

    local mockHud = { speedMeter = makeDisplay(0.15, 0.10) }
    loadMapWith(mockHud)

    assert(near(mockHud.speedMeter.scale, 0.984), "SpeedMeter display scale is base 1.20 * 0.82")

    unloadMap()

    -- Original 2D full-screen corner anchors and base UI scale (1.20) must be restored
    assert(near(_G.g_hudAnchorLeft, 0.0), "g_hudAnchorLeft restored to 0.0")
    assert(near(_G.g_hudAnchorRight, 1.0), "g_hudAnchorRight restored to 1.0")
    assert(near(_G.g_hudAnchorBottom, 0.0), "g_hudAnchorBottom restored to 0.0")
    assert(near(_G.g_hudAnchorTop, 1.0), "g_hudAnchorTop restored to 1.0")
    assert(near(mockHud.speedMeter.scale, 1.20), "SpeedMeter display scale restored to base 1.20")

    _G.GameSettings = nil
    _G.g_gameSettings = nil
end)

-- Slice 4: User settings path resolution, saving, and modSettings override
test("ResolvesUserSettingsPathFromAppPath", function()
    assert(VRCompanion:getUserSettingsPath() == nil, "Should return nil when getUserProfileAppPath is undefined")

    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    local path = VRCompanion:getUserSettingsPath()
    assert(path == "C:/Users/test/Documents/My Games/FarmingSimulator2025/modSettings/FS25_VRCompanion/settings.xml",
        "Should construct path in modSettings/FS25_VRCompanion/settings.xml")

    _G.getUserProfileAppPath = nil
end)

test("SavesConfigurationToXmlFile", function()
    local savedData = {}
    local createdFolder = {}
    local fileSaved = false
    local fileDeleted = false

    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.createFolder = function(folder) table.insert(createdFolder, folder) end
    _G.createXMLFile = function(tag, path, rootNode) return 999 end
    _G.setXMLFloat = function(xmlFile, key, val) savedData[key] = val end
    _G.saveXMLFile = function(xmlFile) fileSaved = true end
    _G.delete = function(xmlFile) fileDeleted = true end

    resetHudSettings()
    VRCompanion:configureHudInsets(0.32, 0.24)
    setGroup("info", 0.11, 0.12, 0.90)
    VRCompanion.hudConvergenceDepth = 1.9
    local success = VRCompanion:saveConfiguration()

    assert(success == true, "saveConfiguration should succeed with mock XML functions")
    assert(math.abs(savedData["vrSettings.hudInsetHorizontal"] - 0.32) < 0.0001, "Saved hudInsetHorizontal should be 0.32")
    assert(math.abs(savedData["vrSettings.hudInsetVertical"] - 0.24) < 0.0001, "Saved hudInsetVertical should be 0.24")
    assert(savedData["vrSettings.hudUiScale"] == nil, "the single UI scale is no longer written")
    assert(near(savedData["vrSettings.hudGroupInfoMarginHorizontal"], 0.11), "Saved info group horizontal margin")
    assert(near(savedData["vrSettings.hudGroupInfoMarginVertical"], 0.12), "Saved info group vertical margin")
    assert(near(savedData["vrSettings.hudGroupInfoScale"], 0.90), "Saved info group scale")
    for _, groupName in ipairs({ "Map", "Speedometer", "Controls" }) do
        assert(near(savedData["vrSettings.hudGroup" .. groupName .. "MarginHorizontal"], 0.30), groupName .. " horizontal margin saved at its default")
        assert(near(savedData["vrSettings.hudGroup" .. groupName .. "MarginVertical"], 0.22), groupName .. " vertical margin saved at its default")
        assert(near(savedData["vrSettings.hudGroup" .. groupName .. "Scale"], 0.82), groupName .. " scale saved at its default")
    end
    assert(math.abs(savedData["vrSettings.hudConvergenceDepth"] - 1.9) < 0.0001, "Saved hudConvergenceDepth should be 1.9")
    assert(fileSaved == true, "saveXMLFile must be invoked")
    assert(fileDeleted == true, "delete XML file handle must be invoked")

    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.saveXMLFile = nil
    _G.delete = nil
end)

test("LoadsUserSettingsXmlWithoutReadingModDesc", function()
    local loadedPaths = {}
    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.loadXMLFile = function(tag, path)
        table.insert(loadedPaths, path)
        if string.find(path, "modSettings") then
            return 101 -- user settings file handle
        end
        return 0
    end
    _G.getXMLFloat = function(xmlFile, key)
        if xmlFile == 101 then
            if key == "vrSettings.hudInsetHorizontal" then return 0.33 end
            if key == "vrSettings.hudInsetVertical" then return 0.25 end
            if key == "vrSettings.hudGroupMapMarginHorizontal" then return 0.12 end
            if key == "vrSettings.hudGroupMapMarginVertical" then return 0.34 end
            if key == "vrSettings.hudGroupMapScale" then return 0.75 end
            if key == "vrSettings.hudGroupControlsScale" then return 5.0 end
            if key == "vrSettings.hudUiScale" then return 0.60 end
            if key == "vrSettings.hudConvergenceDepth" then return 2.1 end
        end
        return nil
    end
    _G.delete = function(xmlFile) end

    local insets = VRCompanion:loadConfiguration()

    assert(math.abs(insets.left - 0.33) < 0.0001, "User settings should set left to 0.33")
    assert(math.abs(insets.bottom - 0.25) < 0.0001, "User settings should set bottom to 0.25")
    local map = VRCompanion:getHudGroupLayout("map")
    assert(near(map.marginX, 0.12) and near(map.marginY, 0.34) and near(map.scale, 0.75), "User settings set the map group's margins and scale")
    local controls = VRCompanion:getHudGroupLayout("controls")
    assert(near(controls.marginX, 0.30) and near(controls.marginY, 0.22), "A group with no saved margins keeps the defaults")
    assert(near(controls.scale, 1.50), "A saved scale is clamped to the maximum")
    local speedometer = VRCompanion:getHudGroupLayout("speedometer")
    assert(near(speedometer.scale, 0.82), "The removed hudUiScale key is ignored rather than applied to the groups")
    assert(math.abs(VRCompanion.hudConvergenceDepth - 2.1) < 0.0001, "User settings should set convergence depth to 2.1")
    for _, p in ipairs(loadedPaths) do
        assert(not string.find(p, "modDesc"), "modDesc.xml must not be read for setting defaults")
    end

    _G.getUserProfileAppPath = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
end)

-- Issue #83: every default is defined exactly once, in the DEFAULT_* constants
test("DefaultsMatchTheValuesPreviouslyYieldedByModDesc", function()
    assert(VRCompanion.DEFAULT_HUD_INSET_X == 0.30, "horizontal inset default")
    assert(VRCompanion.DEFAULT_HUD_INSET_Y == 0.22, "vertical inset default")
    assert(VRCompanion.DEFAULT_HUD_INSET_TOP == 0.22, "top inset default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X == 0.30, "group horizontal margin default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y == 0.22, "group vertical margin default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_SCALE == 0.82, "group scale default")
    assert(VRCompanion.DEFAULT_CONVERGENCE_DEPTH == 1.8, "convergence depth default")
    assert(VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP == 0.004, "fill level gap default")
    assert(VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE == true, "fill level under vehicle default")
    assert(VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION == false, "load notification default")
    assert(VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE == true, "recenter tracking default")
    assert(math.abs(VRCompanion.DEFAULT_SNAP_TURN_ANGLE - math.rad(90)) < 0.0001, "snap turn default")
    assert(VRCompanion.DEFAULT_RENDERING_MODE == VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, "stereo presentation mode default")
    assert(VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION == false, "oblique projection default")
end)

test("ModDescDoesNotDefineSettingDefaults", function()
    local fs = require("@lune/fs")
    local modDescContent = fs.readFile("mods/FS25_VRCompanion/modDesc.xml")
    assert(string.find(modDescContent, "<vrSettings>", 1, true) == nil, "modDesc.xml must not define <vrSettings> defaults")
    assert(string.find(modDescContent, "setting_vrHudUiScale", 1, true) == nil, "the removed UI scale setting has no label")
end)

test("SavedSettingsRoundTripThroughLoader", function()
    local store = {}
    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.createFolder = function() end
    _G.createXMLFile = function() return 999 end
    _G.setXMLFloat = function(_, key, val) store[key] = val end
    _G.setXMLBool = function(_, key, val) store[key] = val end
    _G.setXMLString = function(_, key, val) store[key] = val end
    _G.saveXMLFile = function() end
    _G.loadXMLFile = function() return 555 end
    _G.getXMLFloat = function(_, key) return store[key] end
    _G.getXMLBool = function(_, key) return store[key] end
    _G.getXMLString = function(_, key) return store[key] end
    _G.delete = function() end

    VRCompanion:configureHudInsets(0.31, 0.19, 0.27)
    setGroup("map", 0.05, 0.06, 1.10)
    setGroup("speedometer", 0.07, 0.08, 0.70)
    setGroup("info", 0.09, 0.10, 0.60)
    setGroup("controls", 0.11, 0.12, 1.40)
    VRCompanion.hudConvergenceDepth = 2.4
    VRCompanion.hudFillLevelGap = 0.01
    VRCompanion.fillLevelUnderVehicle = false
    VRCompanion.showHudLoadNotification = true
    VRCompanion.recenterVrTrackingSpace = false
    VRCompanion.allowObliqueProjection = true
    VRCompanion.snapTurnAngle = math.rad(45)
    VRCompanion.dlssNearClipCab = 0.1
    VRCompanion.dlssNearClipOutside = 0.3
    VRCompanion.dlssNearClipOnFoot = 0
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_AFR, false)
    assert(VRCompanion:saveConfiguration("settings.xml") == true, "save must succeed")

    -- Reset everything to defaults, then load what was saved
    VRCompanion:configureHudInsets()
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do VRCompanion:setHudGroupLayout(groupName, 0.30, 0.22, 0.82) end
    VRCompanion.hudConvergenceDepth = nil
    VRCompanion.hudFillLevelGap = nil
    VRCompanion.fillLevelUnderVehicle = nil
    VRCompanion.showHudLoadNotification = nil
    VRCompanion.recenterVrTrackingSpace = nil
    VRCompanion.allowObliqueProjection = nil
    VRCompanion.snapTurnAngle = nil
    VRCompanion.dlssNearClipCab = nil
    VRCompanion.dlssNearClipOutside = nil
    VRCompanion.dlssNearClipOnFoot = nil
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, false)
    local insets = VRCompanion:loadConfiguration("settings.xml")

    assert(math.abs(insets.insetX - 0.31) < 0.0001, "horizontal inset must round-trip")
    assert(math.abs(insets.insetY - 0.19) < 0.0001, "vertical inset must round-trip")
    assert(math.abs(insets.insetTop - 0.27) < 0.0001, "top inset must round-trip")
    local expectedGroups = { map = { 0.05, 0.06, 1.10 }, speedometer = { 0.07, 0.08, 0.70 }, info = { 0.09, 0.10, 0.60 }, controls = { 0.11, 0.12, 1.40 } }
    for groupName, expected in pairs(expectedGroups) do
        local layout = VRCompanion:getHudGroupLayout(groupName)
        assert(near(layout.marginX, expected[1]), groupName .. " horizontal margin must round-trip")
        assert(near(layout.marginY, expected[2]), groupName .. " vertical margin must round-trip")
        assert(near(layout.scale, expected[3]), groupName .. " scale must round-trip")
    end
    assert(math.abs(VRCompanion.hudConvergenceDepth - 2.4) < 0.0001, "convergence depth must round-trip")
    assert(math.abs(VRCompanion.hudFillLevelGap - 0.01) < 0.0001, "fill level gap must round-trip")
    assert(VRCompanion.fillLevelUnderVehicle == false, "fill level under vehicle must round-trip")
    assert(VRCompanion.showHudLoadNotification == true, "load notification must round-trip")
    assert(VRCompanion.recenterVrTrackingSpace == false, "recenter tracking must round-trip")
    assert(VRCompanion.allowObliqueProjection == true, "oblique projection must round-trip")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45)) < 0.0001, "snap turn angle must round-trip")
    assert(VRCompanion:getRenderingMode() == VRCompanion.RENDERING_MODE_AFR, "stereo presentation mode must round-trip")
    assert(math.abs(VRCompanion.dlssNearClipCab - 0.1) < 0.0001, "cab DLSS near plane must round-trip")
    assert(math.abs(VRCompanion.dlssNearClipOutside - 0.3) < 0.0001, "outside camera DLSS near plane must round-trip")
    assert(VRCompanion.dlssNearClipOnFoot == 0, "on-foot DLSS near plane (auto) must round-trip")

    -- Restore defaults for later tests
    VRCompanion.dlssNearClipCab = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_CAB
    VRCompanion.dlssNearClipOutside = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE
    VRCompanion.dlssNearClipOnFoot = VRCompanion.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT
    VRCompanion.allowObliqueProjection = VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION
    VRCompanion.snapTurnAngle = VRCompanion.DEFAULT_SNAP_TURN_ANGLE
    VRCompanion.fillLevelUnderVehicle = VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE
    VRCompanion.showHudLoadNotification = VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION
    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
    VRCompanion.hudFillLevelGap = VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP
    VRCompanion.hudConvergenceDepth = VRCompanion.DEFAULT_CONVERGENCE_DEPTH
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, false)
    VRCompanion:configureHudInsets()
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.setXMLString = nil
    _G.saveXMLFile = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.getXMLString = nil
    _G.delete = nil
end)

test("LoaderIgnoresAliasAndLegacyKeysNoReleaseWrote", function()
    _G.loadXMLFile = function() return 555 end
    _G.getXMLFloat = function(_, key)
        if key == "vrSettings.hudTopInset" or key == "vrSettings.hudInsetVerticalTop"
            or key == "vrSettings.hudInsetTop#value" then return 0.45 end
        return nil
    end
    _G.getXMLBool = function(_, key)
        if key == "vrSettings.showHudLoadAlert" or key == "vrSettings.showLoadNotification"
            or key == "vrSettings.recenterTracking" then return true end
        return nil
    end
    _G.delete = function() end

    VRCompanion.showHudLoadNotification = nil
    VRCompanion.recenterVrTrackingSpace = nil
    local insets = VRCompanion:loadConfiguration("settings.xml")
    assert(math.abs(insets.insetTop - VRCompanion.DEFAULT_HUD_INSET_TOP) < 0.0001, "alias top inset keys must be ignored")
    assert(VRCompanion.showHudLoadNotification == VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION, "alias notification keys must be ignored")
    assert(VRCompanion.recenterVrTrackingSpace == VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE, "alias recenter key must be ignored")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
    VRCompanion:configureHudInsets()
end)

test("RuntimeStateStartsFromDefaultConstants", function()
    -- Load a fresh copy of the mod and check every setting's runtime value against its constant
    local saved = _G.VRCompanion
    _G.VRCompanion = nil
    local ok, err = pcall(function()
        loadCompanionMod()
        local fresh = _G.VRCompanion
        assert(fresh.hudInsetX == fresh.DEFAULT_HUD_INSET_X, "hudInsetX starts at its default")
        assert(fresh.hudInsetY == fresh.DEFAULT_HUD_INSET_Y, "hudInsetY starts at its default")
        assert(fresh.hudInsetTop == fresh.DEFAULT_HUD_INSET_TOP, "hudInsetTop starts at its default")
        assert(math.abs(fresh.hudAnchorTopBase - (1.0 - fresh.DEFAULT_HUD_INSET_TOP)) < 0.0001, "top anchor starts at the top inset default")
        for _, groupName in ipairs(fresh.HUD_GROUP_NAMES) do
            local layout = fresh.hudGroupLayout[groupName]
            assert(layout.marginX == fresh.DEFAULT_HUD_GROUP_MARGIN_X, groupName .. " horizontal margin starts at its default")
            assert(layout.marginY == fresh.DEFAULT_HUD_GROUP_MARGIN_Y, groupName .. " vertical margin starts at its default")
            assert(layout.scale == fresh.DEFAULT_HUD_GROUP_SCALE, groupName .. " scale starts at its default")
        end
        assert(fresh.hudConvergenceDepth == fresh.DEFAULT_CONVERGENCE_DEPTH, "convergence depth starts at its default")
        assert(fresh.hudFillLevelGap == fresh.DEFAULT_HUD_FILL_LEVEL_GAP, "fill level gap starts at its default")
        assert(fresh.fillLevelUnderVehicle == fresh.DEFAULT_FILL_LEVEL_UNDER_VEHICLE, "fill level under vehicle starts at its default")
        assert(fresh.showHudLoadNotification == fresh.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION, "load notification starts at its default")
        assert(fresh.recenterVrTrackingSpace == fresh.DEFAULT_RECENTER_VR_TRACKING_SPACE, "recenter tracking starts at its default")
        assert(fresh.snapTurnAngle == fresh.DEFAULT_SNAP_TURN_ANGLE, "snap turn angle starts at its default")
        assert(fresh.renderingMode == fresh.DEFAULT_RENDERING_MODE, "stereo presentation mode starts at its default")
        assert(fresh.allowObliqueProjection == fresh.DEFAULT_ALLOW_OBLIQUE_PROJECTION, "oblique projection starts at its default")
        assert(fresh.DEFAULT_DLSS_NEAR_CLIP_CAB == 0.1, "cab DLSS near plane defaults to 0.1 m")
        assert(fresh.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE == 0, "outside camera DLSS near plane defaults to auto")
        assert(fresh.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT == 0.15, "on-foot DLSS near plane defaults to 0.15 m")
        assert(fresh.dlssNearClipCab == fresh.DEFAULT_DLSS_NEAR_CLIP_CAB, "cab DLSS near plane starts at its default")
        assert(fresh.dlssNearClipOutside == fresh.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE, "outside camera DLSS near plane starts at its default (auto)")
        assert(fresh.dlssNearClipOnFoot == fresh.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT, "on-foot DLSS near plane starts at its default")
    end)
    _G.VRCompanion = saved
    assert(ok, tostring(err))

    -- The runtime values are assigned from the constants, not from a second copy of the literal
    local fs = require("@lune/fs")
    local configSource = fs.readFile("mods/FS25_VRCompanion/src/VRCompanionConfig.lua")
    for _, field in ipairs({ "fillLevelUnderVehicle", "showHudLoadNotification", "recenterVrTrackingSpace", "hudFillLevelGap", "allowObliqueProjection" }) do
        local literal = string.match(configSource, "\nVRCompanion%." .. field .. " = ([%w%.]+)\n")
        assert(literal == nil or string.find(literal, "DEFAULT_", 1, true) ~= nil,
            string.format("VRCompanion.%s must be initialised from its DEFAULT_* constant, got %s", field, tostring(literal)))
    end
end)

test("FirstRunWithoutSettingsFileAppliesAndSavesEveryDefault", function()
    local store = {}
    local saveCount = 0
    local bridgeMode = nil
    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.loadXMLFile = function() return 0 end -- no settings.xml yet
    _G.createFolder = function() end
    _G.createXMLFile = function() return 999 end
    _G.setXMLFloat = function(_, key, val) store[key] = val end
    _G.setXMLBool = function(_, key, val) store[key] = val end
    _G.setXMLString = function(_, key, val) store[key] = val end
    _G.saveXMLFile = function() saveCount = saveCount + 1 end
    _G.delete = function() end
    -- A bridge that exposes frame parity but no rendering mode reads as AFR until a mode is set explicitly
    _G.VRBridge = {
        getFrameParity = function() return 0 end,
        setRenderingMode = function(mode) bridgeMode = mode end,
    }

    -- Leftover state from an earlier session must not leak into the first-run file
    VRCompanion:configureHudInsets(0.10, 0.10, 0.40)
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do VRCompanion:setHudGroupLayout(groupName, 0.01, 0.02, 1.30) end
    VRCompanion.hudConvergenceDepth = nil
    VRCompanion.hudFillLevelGap = nil
    VRCompanion.fillLevelUnderVehicle = nil
    VRCompanion.showHudLoadNotification = nil
    VRCompanion.recenterVrTrackingSpace = nil
    VRCompanion.allowObliqueProjection = nil
    VRCompanion.snapTurnAngle = nil
    VRCompanion.renderingMode = VRCompanion.RENDERING_MODE_AFR
    VRCompanion.renderingModeExplicit = nil

    local insets = VRCompanion:loadConfiguration()

    -- Every default is applied
    assert(math.abs(insets.insetX - VRCompanion.DEFAULT_HUD_INSET_X) < 0.0001, "horizontal inset default applied")
    assert(math.abs(insets.insetY - VRCompanion.DEFAULT_HUD_INSET_Y) < 0.0001, "vertical inset default applied")
    assert(math.abs(insets.insetTop - VRCompanion.DEFAULT_HUD_INSET_TOP) < 0.0001, "top inset default applied")
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local layout = VRCompanion:getHudGroupLayout(groupName)
        assert(near(layout.marginX, VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X), groupName .. " horizontal margin default applied")
        assert(near(layout.marginY, VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y), groupName .. " vertical margin default applied")
        assert(near(layout.scale, VRCompanion.DEFAULT_HUD_GROUP_SCALE), groupName .. " scale default applied")
    end
    assert(VRCompanion.hudConvergenceDepth == VRCompanion.DEFAULT_CONVERGENCE_DEPTH, "convergence depth default applied")
    assert(VRCompanion.hudFillLevelGap == VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP, "fill level gap default applied")
    assert(VRCompanion.fillLevelUnderVehicle == VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE, "fill level under vehicle default applied")
    assert(VRCompanion.showHudLoadNotification == VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION, "load notification default applied")
    assert(VRCompanion.recenterVrTrackingSpace == VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE, "recenter tracking default applied")
    assert(VRCompanion.allowObliqueProjection == VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION, "oblique projection default applied")
    assert(VRCompanion:getSnapTurnAngle() == VRCompanion.DEFAULT_SNAP_TURN_ANGLE, "snap turn default applied")
    assert(VRCompanion:getRenderingMode() == VRCompanion.DEFAULT_RENDERING_MODE, "stereo presentation mode default applied")
    assert(VRCompanion.renderingModeExplicit == true, "default mode is applied through setRenderingMode")
    assert(bridgeMode == VRCompanion.DEFAULT_RENDERING_MODE, "default mode must reach VRBridge")

    -- ...and the first-run settings file contains exactly those defaults
    assert(saveCount == 1, "first run must create the settings file")
    assert(math.abs(store["vrSettings.hudInsetHorizontal"] - VRCompanion.DEFAULT_HUD_INSET_X) < 0.0001, "saved horizontal inset")
    assert(math.abs(store["vrSettings.hudInsetVertical"] - VRCompanion.DEFAULT_HUD_INSET_Y) < 0.0001, "saved vertical inset")
    assert(math.abs(store["vrSettings.hudInsetTop"] - VRCompanion.DEFAULT_HUD_INSET_TOP) < 0.0001, "saved top inset")
    assert(store["vrSettings.hudUiScale"] == nil, "the removed UI scale key is not written")
    for _, groupName in ipairs({ "Map", "Speedometer", "Info", "Controls" }) do
        local key = "vrSettings.hudGroup" .. groupName
        assert(near(store[key .. "MarginHorizontal"], VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X), "saved " .. groupName .. " horizontal margin")
        assert(near(store[key .. "MarginVertical"], VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y), "saved " .. groupName .. " vertical margin")
        assert(near(store[key .. "Scale"], VRCompanion.DEFAULT_HUD_GROUP_SCALE), "saved " .. groupName .. " scale")
    end
    assert(math.abs(store["vrSettings.hudConvergenceDepth"] - VRCompanion.DEFAULT_CONVERGENCE_DEPTH) < 0.0001, "saved convergence depth")
    assert(math.abs(store["vrSettings.hudFillLevelGap"] - VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP) < 0.0001, "saved fill level gap")
    assert(store["vrSettings.fillLevelUnderVehicle"] == VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE, "saved fill level under vehicle")
    assert(store["vrSettings.showHudLoadNotification"] == VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION, "saved load notification")
    assert(store["vrSettings.recenterVrTrackingSpace"] == VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE, "saved recenter tracking")
    assert(store["vrSettings.allowObliqueProjection"] == VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION, "saved oblique projection")
    assert(math.abs(store["vrSettings.snapTurnAngle"] - math.deg(VRCompanion.DEFAULT_SNAP_TURN_ANGLE)) < 0.0001, "saved snap turn angle")
    assert(store["vrSettings.dlssNearClipCab"] == VRCompanion.DEFAULT_DLSS_NEAR_CLIP_CAB, "saved cab DLSS near plane")
    assert(store["vrSettings.dlssNearClipOutside"] == VRCompanion.DEFAULT_DLSS_NEAR_CLIP_OUTSIDE, "saved outside camera DLSS near plane")
    assert(store["vrSettings.dlssNearClipOnFoot"] == VRCompanion.DEFAULT_DLSS_NEAR_CLIP_ON_FOOT, "saved on-foot DLSS near plane")
    assert(store["vrSettings.renderingMode"] == "DualPass","saved stereo presentation mode must be DualPass, got " .. tostring(store["vrSettings.renderingMode"]))

    _G.VRBridge = nil
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, false)
    VRCompanion:configureHudInsets()
    _G.getUserProfileAppPath = nil
    _G.loadXMLFile = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.setXMLString = nil
    _G.saveXMLFile = nil
    _G.delete = nil
end)

test("SwitchingModeWhileCompositorActivePersistsTheNewMode", function()
    local store = {}
    local latchedMode = "AFR"
    local queuedMode = nil
    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.createXMLFile = function() return 999 end
    _G.setXMLFloat = function() end
    _G.setXMLString = function(_, key, val) store[key] = val end
    _G.saveXMLFile = function() end
    _G.delete = function() end
    -- An active compositor only queues the change; the latched mode is unchanged until a frame boundary
    _G.VRBridge = {
        getRenderingMode = function() return latchedMode end,
        setRenderingMode = function(mode) queuedMode = mode end,
    }
    VRCompanion.renderingMode = VRCompanion.RENDERING_MODE_AFR

    VRCompanion:setRenderingMode("dualpass", true)
    assert(queuedMode == VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, "the new mode is queued on VRBridge")
    assert(store["vrSettings.renderingMode"] == "DualPass",
        "switching to dual-pass must persist DualPass, got " .. tostring(store["vrSettings.renderingMode"]))

    latchedMode = "SequentialDualPass"
    VRCompanion:setRenderingMode("afr", true)
    assert(store["vrSettings.renderingMode"] == "AFR",
        "switching to AFR must persist AFR, got " .. tostring(store["vrSettings.renderingMode"]))

    _G.VRBridge = nil
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, false)
    _G.getUserProfileAppPath = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLString = nil
    _G.saveXMLFile = nil
    _G.delete = nil
end)

test("SettingsFileWithoutHudInsetTopUsesTheTopInsetDefault", function()
    _G.loadXMLFile = function() return 555 end
    _G.getXMLFloat = function(_, key)
        if key == "vrSettings.hudInsetHorizontal" then return 0.12 end
        if key == "vrSettings.hudInsetVertical" then return 0.10 end
        return nil
    end
    _G.delete = function() end

    VRCompanion:configureHudInsets(0.30, 0.30, 0.45)
    local insets = VRCompanion:loadConfiguration("settings.xml")
    assert(math.abs(insets.insetY - 0.10) < 0.0001, "vertical inset is read from the file")
    assert(math.abs(insets.insetTop - VRCompanion.DEFAULT_HUD_INSET_TOP) < 0.0001, "missing top inset falls back to DEFAULT_HUD_INSET_TOP")
    assert(math.abs(insets.top - (1.0 - VRCompanion.DEFAULT_HUD_INSET_TOP)) < 0.0001, "top anchor uses DEFAULT_HUD_INSET_TOP")
    assert(math.abs(insets.bottom - 0.10) < 0.0001, "bottom anchor uses the loaded vertical inset")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    VRCompanion:configureHudInsets()
end)

-- Slice 5: Fill level positioning under vehicle element (SpeedMeterDisplay)
test("ResolvesFillLevelsDisplayAcrossAllHierarchyVariants", function()
    -- 1. Direct active instance
    local mockActive = { name = "ActiveFill" }
    VRCompanion.activeFillLevelsDisplay = mockActive
    assert(VRCompanion:getFillLevelsDisplay() == mockActive, "Should return cached active instance")
    VRCompanion.activeFillLevelsDisplay = nil

    -- 2. On hud.fillLevels
    local mockHud1 = { fillLevels = { name = "HudFillLevels" } }
    assert(VRCompanion:getFillLevelsDisplay(mockHud1) == mockHud1.fillLevels, "Should resolve from hud.fillLevels")

    -- 3. On hud.fillLevelsDisplay
    local mockHud2 = { fillLevelsDisplay = { name = "HudFillLevelsDisplay" } }
    assert(VRCompanion:getFillLevelsDisplay(mockHud2) == mockHud2.fillLevelsDisplay, "Should resolve from hud.fillLevelsDisplay")

    -- 4. On hud.speedMeter.fillLevels
    local mockHud3 = { speedMeter = { fillLevels = { name = "SmFillLevels" } } }
    assert(VRCompanion:getFillLevelsDisplay(mockHud3) == mockHud3.speedMeter.fillLevels, "Should resolve from hud.speedMeter.fillLevels")

    -- 5. On hud.speedMeter.fillLevelsDisplay
    local mockHud4 = { speedMeter = { fillLevelsDisplay = { name = "SmFillLevelsDisplay" } } }
    assert(VRCompanion:getFillLevelsDisplay(mockHud4) == mockHud4.speedMeter.fillLevelsDisplay, "Should resolve from hud.speedMeter.fillLevelsDisplay")

    -- 6. Global fallback
    _G.g_fillLevelsDisplay = { name = "GlobalFillLevels" }
    assert(VRCompanion:getFillLevelsDisplay({}) == _G.g_fillLevelsDisplay, "Should resolve from _G.g_fillLevelsDisplay")
    _G.g_fillLevelsDisplay = nil
end)

test("PositionsFillLevelsDirectlyUnderVehicleSpeedMeter", function()
    resetHudSettings()
    setGroup("speedometer", 0.35, 0.25, 0.50) -- User's exact configured settings
    VRCompanion.fillLevelUnderVehicle = true

    local mockSpeedMeter = {
        name = "SpeedMeterDisplay",
        width = 0.10,
        height = 0.12,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }
    local mockFill = {
        name = "FillLevelsDisplay",
        width = 0.08,
        height = 0.03,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }

    _G.g_currentMission = {
        hud = {
            speedMeter = mockSpeedMeter,
            fillLevels = mockFill
        }
    }

    local flPosX, fillBottom, targetSmBottom = VRCompanion:positionFillLevelsUnderVehicle(mockFill)

    -- Right anchor = 1.0 - 0.35 = 0.65
    -- SpeedMeter bottom = 0.25, smLowestY = 0.25
    -- Fill width = 0.08, flPosX = shiftedRight - flWidth = 0.65 - 0.08 = 0.57 (so right boundary is at 0.65)
    -- Fill height = 0.03, gap = 0.004 * 0.50 = 0.002
    -- fillBottom = 0.25 - 0.002 - 0.03 = 0.218
    assert(math.abs(flPosX - 0.57) < 0.0001, "FillLevels must position its right edge at shiftedRight 0.65")
    assert(math.abs(fillBottom - 0.218) < 0.0001, "FillLevels must be positioned directly under SpeedMeter at 0.218")
    assert(math.abs(targetSmBottom - 0.25) < 0.0001, "SpeedMeter stays at base bottom 0.25 when ample space exists")

    assert(mockFill.position ~= nil)
    assert(math.abs(mockFill.position.x - 0.57) < 0.0001)
    assert(math.abs(mockFill.position.y - 0.218) < 0.0001)

    _G.g_currentMission = nil
end)

test("AutoLiftsSpeedMeterWhenFillLevelsApproachBottomBoundary", function()
    -- Minimal bottom margin: base bottom = 0.02, uiScale = 1.0
    resetHudSettings()
    setGroup("speedometer", 0.20, 0.02, 1.0)
    VRCompanion.fillLevelUnderVehicle = true
    VRCompanion.hudFillLevelGap = 0.004

    local mockSmPos = nil
    local mockSpeedMeter = {
        width = 0.10,
        setPosition = function(self, x, y) mockSmPos = { x = x, y = y } end
    }
    local mockFill = {
        height = 0.04,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }

    _G.g_currentMission = {
        hud = {
            speedMeter = mockSpeedMeter,
            fillLevels = mockFill
        }
    }

    local flPosX, fillBottom, targetSmBottom = VRCompanion:positionFillLevelsUnderVehicle(mockFill)

    -- smLowestY = 0.02, gap = 0.004 * 1.0 = 0.004
    -- fillBottom = 0.02 - 0.004 - 0.04 = -0.024 (< 0.015)
    -- So fillBottom is clamped to min margin 0.015
    -- targetSmBottom lifted to 0.015 + 0.04 + 0.004 = 0.059
    assert(math.abs(fillBottom - 0.015) < 0.0001, "fillBottom clamped to min margin 0.015")
    assert(math.abs(targetSmBottom - 0.059) < 0.0001, "targetSmBottom lifted to 0.059")
    assert(mockSmPos ~= nil, "SpeedMeter must be repositioned to lifted height")
    assert(math.abs(mockSmPos.y - 0.059) < 0.0001, "SpeedMeter positioned at lifted height")
    assert(math.abs(mockSmPos.x - (0.80 - 0.10)) < 0.0001, "SpeedMeter keeps its outer right edge at the group's horizontal margin")

    _G.g_currentMission = nil
end)

test("SavesAndLoadsFillLevelUnderVehicleConfiguration", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) end
    _G.createXMLFile = function(tag, path, rootNode) return 888 end
    _G.setXMLFloat = function(xmlFile, key, val) savedData[key] = val end
    _G.setXMLBool = function(xmlFile, key, val) savedData[key] = val end
    _G.saveXMLFile = function(xmlFile) end
    _G.delete = function(xmlFile) end

    VRCompanion.fillLevelUnderVehicle = true
    VRCompanion:saveConfiguration()
    assert(savedData["vrSettings.fillLevelUnderVehicle"] == true, "fillLevelUnderVehicle must be saved to XML")

    -- Test loading from XML
    _G.loadXMLFile = function(tag, path) return 777 end
    _G.getXMLFloat = function(xmlFile, key) return nil end
    _G.getXMLBool = function(xmlFile, key)
        if key == "vrSettings.fillLevelUnderVehicle" then return false end
        return nil
    end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(VRCompanion.fillLevelUnderVehicle == false, "Must load false from XML when configured")

    -- Clean up mocks
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.saveXMLFile = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
end)

test("ConsoleCommandTogglesFillLevelUnderVehicle", function()
    VRCompanion.fillLevelUnderVehicle = false
    local resultTrue = VRCompanion:consoleCommandSetHudFillUnderVehicle("true")
    assert(VRCompanion.fillLevelUnderVehicle == true, "Console command should enable fillLevelUnderVehicle")
    assert(string.find(resultTrue, "true") ~= nil, "Response should confirm true")

    local resultFalse = VRCompanion:consoleCommandSetHudFillUnderVehicle("false")
    assert(VRCompanion.fillLevelUnderVehicle == false, "Console command should disable fillLevelUnderVehicle")
    assert(string.find(resultFalse, "false") ~= nil, "Response should confirm false")

    local usage = VRCompanion:consoleCommandSetHudFillUnderVehicle("invalid")
    assert(string.find(usage, "Usage") ~= nil, "Invalid input should return usage string")

    -- Reset back to default true
    VRCompanion.fillLevelUnderVehicle = true
end)

test("SavesAndLoadsHudFillLevelGapConfiguration", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) end
    _G.createXMLFile = function(tag, path, rootNode) return 888 end
    _G.setXMLFloat = function(xmlFile, key, val) savedData[key] = val end
    _G.setXMLBool = function(xmlFile, key, val) savedData[key] = val end
    _G.saveXMLFile = function(xmlFile) end
    _G.delete = function(xmlFile) end

    VRCompanion.hudFillLevelGap = 0.002
    VRCompanion:saveConfiguration()
    assert(math.abs((savedData["vrSettings.hudFillLevelGap"] or 0) - 0.002) < 0.0001, "hudFillLevelGap must be saved to XML")

    -- Test loading from XML
    _G.loadXMLFile = function(tag, path) return 777 end
    _G.getXMLFloat = function(xmlFile, key)
        if key == "vrSettings.hudFillLevelGap" then return 0.006 end
        return nil
    end
    _G.getXMLBool = function(xmlFile, key) return nil end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion.hudFillLevelGap - 0.006) < 0.0001, "Must load 0.006 from XML when configured")

    -- Clean up mocks
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.saveXMLFile = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
    VRCompanion.hudFillLevelGap = VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP
end)

test("ConsoleCommandSetsHudFillLevelGap", function()
    VRCompanion.hudFillLevelGap = 0.004
    local result = VRCompanion:consoleCommandSetHudFillGap("0.0025")
    assert(math.abs(VRCompanion.hudFillLevelGap - 0.0025) < 0.0001, "Console command should set hudFillLevelGap to 0.0025")
    assert(string.find(result, "0.0025") ~= nil, "Response should confirm 0.0025")

    local usage = VRCompanion:consoleCommandSetHudFillGap("invalid")
    assert(string.find(usage, "Usage") ~= nil, "Invalid input should return usage string")

    -- Clamping test
    VRCompanion:consoleCommandSetHudFillGap("0.10")
    assert(math.abs(VRCompanion.hudFillLevelGap - 0.05) < 0.0001, "Should clamp gap to max 0.05")

    -- Reset back to default
    VRCompanion.hudFillLevelGap = VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP
end)

test("CalculatesCustomTopAnchorWhenInsetTopSpecified", function()
    local left, right, bottom, top = VRCompanion:calculateHudAnchors(0.35, 0.25, 0.38)
    assert(math.abs(left - 0.35) < 0.0001, "Left anchor must be 0.35")
    assert(math.abs(right - 0.65) < 0.0001, "Right anchor must be 0.65")
    assert(math.abs(bottom - 0.25) < 0.0001, "Bottom anchor must be 0.25")
    assert(math.abs(top - 0.62) < 0.0001, "Top anchor must be 1.0 - 0.38 = 0.62")
end)

test("ClampsTopInsetWithinSafeBoundaries", function()
    local _, _, _, topMin = VRCompanion:calculateHudAnchors(0.20, 0.16, -0.10)
    assert(math.abs(topMin - 1.0) < 0.0001, "Negative top inset clamped to 0.0 -> top anchor 1.0")

    local _, _, _, topMax = VRCompanion:calculateHudAnchors(0.20, 0.16, 0.80)
    assert(math.abs(topMax - 0.50) < 0.0001, "Excessive top inset clamped to 0.50 -> top anchor 0.50")
end)

test("SavesAndLoadsHudInsetTopConfiguration", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) end
    _G.createXMLFile = function(tag, path, rootNode) return 888 end
    _G.setXMLFloat = function(xmlFile, key, val) savedData[key] = val end
    _G.setXMLBool = function(xmlFile, key, val) savedData[key] = val end
    _G.saveXMLFile = function(xmlFile) end
    _G.delete = function(xmlFile) end

    VRCompanion.hudInsetTop = 0.35
    VRCompanion:saveConfiguration()
    assert(math.abs((savedData["vrSettings.hudInsetTop"] or 0) - 0.35) < 0.0001, "hudInsetTop must be saved to XML")

    -- Test loading from XML
    _G.loadXMLFile = function(tag, path) return 777 end
    _G.getXMLFloat = function(xmlFile, key)
        if key == "vrSettings.hudInsetTop" then return 0.40 end
        return nil
    end
    _G.getXMLBool = function(xmlFile, key) return nil end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion.hudInsetTop - 0.40) < 0.0001, "Must load 0.40 from XML when configured")
    assert(math.abs(VRCompanion.hudAnchorTop - 0.60) < 0.0001, "Top anchor must be 0.60")

    -- Clean up mocks
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.saveXMLFile = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
    VRCompanion.hudInsetTop = nil
end)

test("ConsoleCommandSetsHudTopInset", function()
    local result = VRCompanion:consoleCommandSetHudTopInset("0.35")
    assert(math.abs(VRCompanion.hudInsetTop - 0.35) < 0.0001, "Console command should set hudInsetTop to 0.35")
    assert(math.abs(VRCompanion.hudAnchorTop - 0.65) < 0.0001, "Console command should update hudAnchorTop to 0.65")
    assert(string.find(result, "0.35") ~= nil, "Response should confirm 0.35")

    local usage = VRCompanion:consoleCommandSetHudTopInset("invalid")
    assert(string.find(usage, "Usage") ~= nil, "Invalid input should return usage string")

    -- Clamping test
    VRCompanion:consoleCommandSetHudTopInset("0.90")
    assert(math.abs(VRCompanion.hudInsetTop - 0.50) < 0.0001, "Should clamp top inset to max 0.50")

    -- Reset back
    VRCompanion.hudInsetTop = nil
    resetHudSettings()
end)

test("ConvergencePositionsTopGroupsFromTheirOwnVerticalMargins", function()
    resetHudSettings()
    VRCompanion:configureHudInsets(0.35, 0.25, 0.38)
    setGroup("controls", 0.10, 0.12)
    setGroup("info", 0.10, 0.31)
    local mockIhPos = nil
    local mockGiPos = nil
    local mockIh = { height = 0.20, setPosition = function(self, x, y) mockIhPos = { x = x, y = y } end }
    local mockGi = { height = 0.10, setPosition = function(self, x, y) mockGiPos = { x = x, y = y } end }
    _G.g_currentMission = {
        hud = {
            inputHelp = mockIh,
            gameInfo = mockGi
        }
    }
    _G.VRBridge = { getTrackingState = function() return { headPose = { parity = 0, ipd = 0.064 } } end }
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

    VRCompanion:updateHudConvergence()
    assert(mockIhPos ~= nil, "InputHelp must be positioned")
    assert(mockGiPos ~= nil, "GameInfo must be positioned")
    assert(math.abs(mockIhPos.y - (1.0 - 0.12 - 0.20)) < 0.0001, "InputHelp top edge sits at the controls group's vertical margin")
    assert(math.abs(mockGiPos.y - (1.0 - 0.31 - 0.10)) < 0.0001, "GameInfo top edge sits at the info group's vertical margin")
    assert(math.abs(VRCompanion.hudAnchorTop - 0.62) < 0.0001, "the Safe Area's top anchor is unaffected by the groups")

    _G.g_currentMission = nil
    _G.VRBridge = nil
    VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
    resetHudSettings()
end)
test("SavesAndLoadsHudLoadNotificationConfiguration", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) end
    _G.createXMLFile = function(tag, path, rootNode) return 888 end
    _G.setXMLFloat = function(xmlFile, key, val) savedData[key] = val end
    _G.setXMLBool = function(xmlFile, key, val) savedData[key] = val end
    _G.saveXMLFile = function(xmlFile) end
    _G.delete = function(xmlFile) end

    VRCompanion.showHudLoadNotification = false
    VRCompanion:saveConfiguration()
    assert(savedData["vrSettings.showHudLoadNotification"] == false, "showHudLoadNotification must be saved as false")

    VRCompanion.showHudLoadNotification = true
    VRCompanion:saveConfiguration()
    assert(savedData["vrSettings.showHudLoadNotification"] == true, "showHudLoadNotification must be saved as true")

    -- Test loading from XML
    _G.loadXMLFile = function(tag, path) return 777 end
    _G.getXMLFloat = function(xmlFile, key) return nil end
    _G.getXMLBool = function(xmlFile, key)
        if key == "vrSettings.showHudLoadNotification" then return false end
        return nil
    end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(VRCompanion.showHudLoadNotification == false, "Must load false from XML")

    -- Clean up mocks
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.saveXMLFile = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
    VRCompanion.showHudLoadNotification = false
end)

test("ConsoleCommandTogglesHudLoadNotification", function()
    local res1 = VRCompanion:consoleCommandSetHudLoadNotification("true")
    assert(VRCompanion.showHudLoadNotification == true, "Should set showHudLoadNotification to true")
    assert(string.find(res1, "true") ~= nil, "Response should confirm true")

    local res2 = VRCompanion:consoleCommandSetHudLoadNotification("false")
    assert(VRCompanion.showHudLoadNotification == false, "Should set showHudLoadNotification to false")
    assert(string.find(res2, "false") ~= nil, "Response should confirm false")

    local usage = VRCompanion:consoleCommandSetHudLoadNotification("")
    assert(string.find(usage, "Usage") ~= nil, "Empty input should return usage string")

    -- Reset back to default false
    VRCompanion.showHudLoadNotification = false
end)

test("LoadMapSuppressesHudHiddenWarningWhenNotificationDisabled", function()
    local warningShown = false
    local warningMsg = nil
    local mockMission = {
        showBlinkingWarning = function(self, msg, duration)
            warningShown = true
            warningMsg = msg
        end,
        hud = {}
    }
    _G.g_currentMission = mockMission

    -- 1. When disabled (default), warning must NOT be shown
    VRCompanion.showHudLoadNotification = false
    VRCompanion:loadMap("mapUS")
    assert(warningShown == false, "Warning must be suppressed on map load when showHudLoadNotification is false")

    -- 2. When enabled, warning must be shown
    warningShown = false
    VRCompanion.showHudLoadNotification = true
    VRCompanion:loadMap("mapUS")
    assert(warningShown == true, "Warning must be shown on map load when showHudLoadNotification is true")
    assert(string.find(warningMsg, "VR HUD: Hidden") ~= nil, "Warning message must announce Hidden state")

    -- Reset back to default false
    VRCompanion.showHudLoadNotification = false
    _G.g_currentMission = nil
end)

-- vrHudGroup console command (spec #50, ticket #52)
local function installSaveMocks()
    local saved = { store = {}, count = 0 }
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function() end
    _G.createXMLFile = function() return 888 end
    _G.setXMLFloat = function(_, key, val) saved.store[key] = val end
    _G.setXMLBool = function(_, key, val) saved.store[key] = val end
    _G.setXMLString = function(_, key, val) saved.store[key] = val end
    _G.saveXMLFile = function() saved.count = saved.count + 1 end
    _G.delete = function() end
    return saved
end

local function removeSaveMocks()
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.setXMLString = nil
    _G.saveXMLFile = nil
    _G.delete = nil
end

test("VrHudGroupAppliesSavesAndConfirmsOnScreen", function()
    local hud = makeHud()
    loadMapWith(hud)
    local warnings = {}
    _G.g_currentMission.showBlinkingWarning = function(self, msg) table.insert(warnings, msg) end
    local saved = installSaveMocks()

    local result = setGroup("map", 0.12, 0.34, 0.70)

    assert(near(hud.ingameMap.position.x, 0.12) and near(hud.ingameMap.position.y, 0.34), "applies immediately")
    assert(near(hud.ingameMap.scale, 0.70), "scale applies immediately")
    assert(saved.count >= 1, "saves the configuration")
    assert(near(saved.store["vrSettings.hudGroupMapMarginHorizontal"], 0.12), "saved horizontal margin")
    assert(near(saved.store["vrSettings.hudGroupMapMarginVertical"], 0.34), "saved vertical margin")
    assert(near(saved.store["vrSettings.hudGroupMapScale"], 0.70), "saved scale")
    assert(#warnings == 1 and string.find(warnings[1], "map", 1, true) and string.find(warnings[1], "Saved", 1, true),
        "shows an on-screen confirmation")
    assert(string.find(result, "map", 1, true) and not string.find(result, "Usage", 1, true), "console reply confirms the change")

    -- The scale stays when only the margins are given
    setGroup("map", 0.20, 0.20)
    assert(near(hud.ingameMap.scale, 0.70), "an omitted scale keeps the group's current scale")

    removeSaveMocks()
    unloadMap()
end)

test("VrHudGroupAcceptsAnyLetterCaseOfTheGroupName", function()
    local hud = makeHud()
    loadMapWith(hud)
    setGroup("Speedometer", 0.10, 0.20)
    assert(near(hud.speedMeter.position.x, 1.0 - 0.10 - 0.15) and near(hud.speedMeter.position.y, 0.20), "group names are not case sensitive")
    unloadMap()
end)

test("VrHudGroupWithoutArgumentsListsUsageAndEveryGroupsValues", function()
    local hud = makeHud()
    loadMapWith(hud)
    setGroup("info", 0.11, 0.12, 0.90)
    local saved = installSaveMocks()
    local before = hud.gameInfo.position

    local usage = VRCompanion:consoleCommandSetHudGroup()
    assert(string.find(usage, "Usage: vrHudGroup <map|speedometer|info|controls> <horizontal> <vertical> [scale]", 1, true), "prints the usage")
    for _, groupName in ipairs({ "map", "speedometer", "info", "controls" }) do
        assert(string.find(usage, groupName, 1, true), "lists the " .. groupName .. " group")
    end
    assert(string.find(usage, "horizontal=0.11, vertical=0.12, scale=0.90", 1, true), "lists a group's current margins and scale")
    assert(string.find(usage, "horizontal=0.30, vertical=0.22, scale=0.82", 1, true), "lists the defaults of untouched groups")
    assert(saved.count == 0, "printing the usage saves nothing")
    assert(hud.gameInfo.position == before, "printing the usage moves nothing")

    removeSaveMocks()
    unloadMap()
end)

test("VrHudGroupWithInvalidArgumentsPrintsUsageAndChangesNothing", function()
    local hud = makeHud()
    loadMapWith(hud)
    local saved = installSaveMocks()
    local snapshot = {}
    for _, name in ipairs(DISPLAY_NAMES) do snapshot[name] = hud[name].position end

    local badCalls = {
        { "sidebar", "0.1", "0.1" },
        { "map", "0.1" },
        { "map", "left", "0.1" },
        { "map", "0.1", "0.1", "big" },
        { "map", "nan", "0.1" },
        { nil, "0.1", "0.1" },
    }
    for i, args in ipairs(badCalls) do
        local result = VRCompanion:consoleCommandSetHudGroup(args[1], args[2], args[3], args[4])
        assert(string.find(result, "Usage: vrHudGroup", 1, true), "invalid call " .. i .. " must print the usage")
        assert(string.find(result, "horizontal=0.30, vertical=0.22, scale=0.82", 1, true), "invalid call " .. i .. " lists the current values")
    end
    for _, name in ipairs(DISPLAY_NAMES) do
        assert(hud[name].position == snapshot[name], name .. " must not move on invalid input")
    end
    assert(saved.count == 0, "invalid input saves nothing")

    removeSaveMocks()
    unloadMap()
end)

test("VrHudGroupConsoleCommandIsRegisteredOnMapLoadAndRemovedOnUnload", function()
    local registered, removed = {}, {}
    _G.addConsoleCommand = function(cmd, desc, funcName) registered[cmd] = { desc = desc, funcName = funcName } end
    _G.removeConsoleCommand = function(cmd) removed[cmd] = true end
    loadMapWith(makeHud())
    assert(registered["vrHudGroup"] ~= nil and registered["vrHudGroup"].funcName == "consoleCommandSetHudGroup", "vrHudGroup is registered")
    unloadMap()
    assert(removed["vrHudGroup"] == true, "vrHudGroup is removed on map unload")
    _G.addConsoleCommand = nil
    _G.removeConsoleCommand = nil
end)

test("GroupMarginsDoNotMoveTheHudSafeArea", function()
    loadMapWith(makeHud())
    setGroup("map", 0.05, 0.05)
    setGroup("info", 0.40, 0.40)
    assert(near(_G.g_hudAnchorLeft, 0.30) and near(_G.g_hudAnchorRight, 0.70), "global horizontal anchors stay on the Safe Area")
    assert(near(_G.g_hudAnchorBottom, 0.22) and near(_G.g_hudAnchorTop, 0.78), "global vertical anchors stay on the Safe Area")
    unloadMap()
end)

test("GroupLayoutsStartFromTheDefaultsWhenTheSettingsFileHasNoGroupKeys", function()
    setGroup("map", 0.01, 0.02, 1.4)
    _G.loadXMLFile = function() return 555 end
    _G.getXMLFloat = function(_, key)
        if key == "vrSettings.hudInsetHorizontal" then return 0.25 end
        return nil
    end
    _G.delete = function() end
    VRCompanion:loadConfiguration("settings.xml")
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local layout = VRCompanion:getHudGroupLayout(groupName)
        assert(near(layout.marginX, 0.30) and near(layout.marginY, 0.22) and near(layout.scale, 0.82), groupName .. " falls back to the defaults")
    end
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    resetHudSettings()
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d tests failed", failed))
end
