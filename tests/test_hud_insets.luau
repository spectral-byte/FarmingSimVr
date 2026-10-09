-- ====================================================================
-- Test Suite for the HUD Safe Area, HUD Group placement and settings persistence
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

print("Running VR HUD Safe Area and HUD Group Tests...")

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

-- Slice 1: the HUD Safe Area sets the engine's global HUD anchors (spec #50, ticket #55)
test("CalculatesDefaultHudAnchors", function()
    assert(VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL == 0.30, "DEFAULT_HUD_SAFE_AREA_HORIZONTAL must be 0.30")
    assert(VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL == 0.22, "DEFAULT_HUD_SAFE_AREA_VERTICAL must be 0.22")

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
    assert(near(leftMin, 0.0), "Negative horizontal margin clamped to 0.0")
    assert(near(rightMin, 1.0), "Right anchor clamped to 1.0")
    assert(near(bottomMin, 0.0), "Negative vertical margin clamped to 0.0")
    assert(near(topMin, 1.0), "Top anchor clamped to 1.0")

    local leftMax, rightMax, bottomMax, topMax = VRCompanion:calculateHudAnchors(0.50, 0.45)
    assert(near(leftMax, 0.40), "Excessive horizontal margin clamped to 0.40")
    assert(near(rightMax, 0.60), "Right anchor clamped to 0.60")
    assert(near(bottomMax, 0.35), "Excessive vertical margin clamped to 0.35")
    assert(near(topMax, 0.65), "Top anchor clamped to 0.65")
end)

test("TheGlobalInsetStateAndConstantsAreRemoved", function()
    for _, name in ipairs({ "DEFAULT_HUD_INSET_X", "DEFAULT_HUD_INSET_Y", "DEFAULT_HUD_INSET_TOP",
            "MIN_HUD_INSET_X", "MAX_HUD_INSET_X", "MIN_HUD_INSET_Y", "MAX_HUD_INSET_Y", "MIN_HUD_INSET_TOP", "MAX_HUD_INSET_TOP" }) do
        assert(VRCompanion[name] == nil, name .. " must be removed")
    end
    for _, name in ipairs({ "hudInsetX", "hudInsetY", "hudInsetTop", "configureHudInsets", "getHudInsets", "applyHudInsets",
            "restoreHudInsets", "consoleCommandSetHudInsets", "consoleCommandSetHudTopInset" }) do
        assert(VRCompanion[name] == nil, name .. " must be removed")
    end
end)

-- Slice 2: Configuration API and metadata / XML fallback
test("ConfiguresHudSafeAreaAndQueriesState", function()
    VRCompanion:configureHudSafeArea(0.25, 0.18)
    local safeArea = VRCompanion:getHudSafeArea()

    assert(near(safeArea.left, 0.25), "getHudSafeArea().left must match")
    assert(near(safeArea.right, 0.75), "getHudSafeArea().right must match")
    assert(near(safeArea.bottom, 0.18), "getHudSafeArea().bottom must match")
    assert(near(safeArea.top, 0.82), "getHudSafeArea().top must match")
    assert(near(safeArea.horizontal, 0.25), "getHudSafeArea().horizontal must match")
    assert(near(safeArea.vertical, 0.18), "getHudSafeArea().vertical must match")
    assert(safeArea.scaleMultiplier == nil, "the single UI scale multiplier is gone")
    resetHudSettings()
end)

test("LoadsConfigurationWithGracefulFallbackWhenUnspecified", function()
    VRCompanion:configureHudSafeArea(0.10, 0.10)

    local safeArea = VRCompanion:loadConfiguration("non_existent_file.xml")
    assert(near(safeArea.left, 0.30), "Must fall back to default left 0.30")
    assert(near(safeArea.right, 0.70), "Must fall back to default right 0.70")
    assert(near(safeArea.bottom, 0.22), "Must fall back to default bottom 0.22")
    assert(near(safeArea.top, 0.78), "Must fall back to default top 0.78")
end)

test("LoadsConfigurationFromXmlWhenSpecified", function()
    _G.mockXmlData = {
        ["vrSettings.hudSafeAreaHorizontal"] = 0.22,
        ["vrSettings.hudSafeAreaVertical"] = 0.15
    }
    _G.loadXMLFile = function(tag, path) return 123 end
    _G.getXMLFloat = function(xmlFile, key)
        return _G.mockXmlData[key]
    end
    _G.delete = function(xmlFile) end

    local safeArea = VRCompanion:loadConfiguration("mockSettings.xml")
    assert(near(safeArea.left, 0.22), "Loaded left anchor must be 0.22")
    assert(near(safeArea.right, 0.78), "Loaded right anchor must be 0.78")
    assert(near(safeArea.bottom, 0.15), "Loaded bottom anchor must be 0.15")
    assert(near(safeArea.top, 0.85), "Loaded top anchor must be 0.85")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    _G.mockXmlData = nil
    resetHudSettings()
end)

test("SettingsFileWithOnlyTheOldInsetKeysLoadsTheNewDefaults", function()
    VRCompanion:configureHudSafeArea(0.05, 0.05)
    _G.loadXMLFile = function() return 555 end
    _G.getXMLFloat = function(_, key)
        if key == "vrSettings.hudInsetHorizontal" then return 0.12 end
        if key == "vrSettings.hudInsetVertical" then return 0.10 end
        if key == "vrSettings.hudInsetTop" then return 0.40 end
        return nil
    end
    _G.delete = function() end

    local safeArea = VRCompanion:loadConfiguration("settings.xml")
    assert(near(safeArea.horizontal, 0.30) and near(safeArea.vertical, 0.22), "old inset keys are ignored, not migrated")
    assert(near(safeArea.left, 0.30) and near(safeArea.right, 0.70), "horizontal anchors use the default")
    assert(near(safeArea.bottom, 0.22) and near(safeArea.top, 0.78), "vertical anchors use the default for both edges")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    resetHudSettings()
end)

test("HudSafeAreaWithOneKeyMissingKeepsTheOtherDefault", function()
    _G.loadXMLFile = function() return 555 end
    _G.getXMLFloat = function(_, key)
        if key == "vrSettings.hudSafeAreaHorizontal" then return 0.12 end
        return nil
    end
    _G.delete = function() end

    local safeArea = VRCompanion:loadConfiguration("settings.xml")
    assert(near(safeArea.horizontal, 0.12), "the saved horizontal margin loads")
    assert(near(safeArea.vertical, 0.22), "the missing vertical margin falls back to its default")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
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
    VRCompanion:configureHudSafeArea(0.32, 0.24)
    setGroup("info", 0.11, 0.12, 0.90)
    VRCompanion.hudConvergenceDepth = 1.9
    local success = VRCompanion:saveConfiguration()

    assert(success == true, "saveConfiguration should succeed with mock XML functions")
    assert(math.abs(savedData["vrSettings.hudSafeAreaHorizontal"] - 0.32) < 0.0001, "Saved hudSafeAreaHorizontal should be 0.32")
    assert(math.abs(savedData["vrSettings.hudSafeAreaVertical"] - 0.24) < 0.0001, "Saved hudSafeAreaVertical should be 0.24")
    assert(savedData["vrSettings.hudInsetHorizontal"] == nil and savedData["vrSettings.hudInsetVertical"] == nil
        and savedData["vrSettings.hudInsetTop"] == nil, "the removed inset keys are not written")
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
            if key == "vrSettings.hudSafeAreaHorizontal" then return 0.33 end
            if key == "vrSettings.hudSafeAreaVertical" then return 0.25 end
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

    local safeArea = VRCompanion:loadConfiguration()

    assert(math.abs(safeArea.left - 0.33) < 0.0001, "User settings should set left to 0.33")
    assert(math.abs(safeArea.bottom - 0.25) < 0.0001, "User settings should set bottom to 0.25")
    assert(math.abs(safeArea.top - 0.75) < 0.0001, "The vertical margin also sets the top anchor")
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
    assert(VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL == 0.30, "HUD Safe Area horizontal default")
    assert(VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL == 0.22, "HUD Safe Area vertical default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X == 0.30, "group horizontal margin default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y == 0.22, "group vertical margin default")
    assert(VRCompanion.DEFAULT_HUD_GROUP_SCALE == 0.82, "group scale default")
    assert(VRCompanion.DEFAULT_CONVERGENCE_DEPTH == 1.8, "convergence depth default")
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
    for _, label in ipairs({ "setting_vrHudInsetHorizontal", "setting_vrHudInsetVertical", "setting_vrHudInsetTop" }) do
        assert(string.find(modDescContent, label, 1, true) == nil, "the removed " .. label .. " setting has no label")
    end
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

    VRCompanion:configureHudSafeArea(0.31, 0.19)
    setGroup("map", 0.05, 0.06, 1.10)
    setGroup("speedometer", 0.07, 0.08, 0.70)
    setGroup("info", 0.09, 0.10, 0.60)
    setGroup("controls", 0.11, 0.12, 1.40)
    VRCompanion.hudConvergenceDepth = 2.4
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
    VRCompanion:configureHudSafeArea()
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do VRCompanion:setHudGroupLayout(groupName, 0.30, 0.22, 0.82) end
    VRCompanion.hudConvergenceDepth = nil
    VRCompanion.showHudLoadNotification = nil
    VRCompanion.recenterVrTrackingSpace = nil
    VRCompanion.allowObliqueProjection = nil
    VRCompanion.snapTurnAngle = nil
    VRCompanion.dlssNearClipCab = nil
    VRCompanion.dlssNearClipOutside = nil
    VRCompanion.dlssNearClipOnFoot = nil
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, false)
    local safeArea = VRCompanion:loadConfiguration("settings.xml")

    assert(math.abs(safeArea.horizontal - 0.31) < 0.0001, "HUD Safe Area horizontal margin must round-trip")
    assert(math.abs(safeArea.vertical - 0.19) < 0.0001, "HUD Safe Area vertical margin must round-trip")
    assert(math.abs(safeArea.left - 0.31) < 0.0001 and math.abs(safeArea.top - 0.81) < 0.0001, "the loaded margins set the global anchors")
    local expectedGroups = { map = { 0.05, 0.06, 1.10 }, speedometer = { 0.07, 0.08, 0.70 }, info = { 0.09, 0.10, 0.60 }, controls = { 0.11, 0.12, 1.40 } }
    for groupName, expected in pairs(expectedGroups) do
        local layout = VRCompanion:getHudGroupLayout(groupName)
        assert(near(layout.marginX, expected[1]), groupName .. " horizontal margin must round-trip")
        assert(near(layout.marginY, expected[2]), groupName .. " vertical margin must round-trip")
        assert(near(layout.scale, expected[3]), groupName .. " scale must round-trip")
    end
    assert(math.abs(VRCompanion.hudConvergenceDepth - 2.4) < 0.0001, "convergence depth must round-trip")
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
    VRCompanion.showHudLoadNotification = VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION
    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
    VRCompanion.hudConvergenceDepth = VRCompanion.DEFAULT_CONVERGENCE_DEPTH
    VRCompanion:setRenderingMode(VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS, false)
    VRCompanion:configureHudSafeArea()
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

-- Per-group Streamlined and Full HUD visibility (spec #50, ticket #53)
local function withMockedSettingsFile(store, fn)
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
    local ok, err = pcall(fn)
    for _, name in ipairs({ "getUserProfileAppPath", "createFolder", "createXMLFile", "setXMLFloat", "setXMLBool",
            "setXMLString", "saveXMLFile", "loadXMLFile", "getXMLFloat", "getXMLBool", "getXMLString", "delete" }) do
        _G[name] = nil
    end
    assert(ok, err)
end

local function resetGroupVisibilityToDefaults()
    for name, group in pairs(VRCompanion.HUD_GROUPS) do
        group.streamlined = VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED[name]
        group.full = VRCompanion.DEFAULT_HUD_GROUP_FULL[name]
    end
end

test("GroupVisibilitySettingsRoundTripThroughSaveAndLoad", function()
    local store = {}
    withMockedSettingsFile(store, function()
        VRCompanion.HUD_GROUPS.map.streamlined = true
        VRCompanion.HUD_GROUPS.map.full = false
        VRCompanion.HUD_GROUPS.speedometer.streamlined = false
        VRCompanion.HUD_GROUPS.info.full = false
        VRCompanion.HUD_GROUPS.controls.streamlined = true
        assert(VRCompanion:saveConfiguration("settings.xml") == true, "save must succeed")
        assert(store["vrSettings.hudGroups.map.showStreamlined"] == true, "map Streamlined key")
        assert(store["vrSettings.hudGroups.map.showFull"] == false, "map Full key")
        assert(store["vrSettings.hudGroups.speedometer.showStreamlined"] == false, "speedometer Streamlined key")
        assert(store["vrSettings.hudGroups.speedometer.showFull"] == true, "speedometer Full key")
        assert(store["vrSettings.hudGroups.info.showFull"] == false, "info Full key")
        assert(store["vrSettings.hudGroups.controls.showStreamlined"] == true, "controls Streamlined key")

        resetGroupVisibilityToDefaults()
        VRCompanion:loadConfiguration("settings.xml")
        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_STREAMLINED) == true, "map Streamlined must round-trip")
        assert(VRCompanion:isDisplayVisible("IngameMap", VRCompanion.HUD_STATE_FULL) == false, "map Full must round-trip")
        assert(VRCompanion:isDisplayVisible("SpeedMeterDisplay", VRCompanion.HUD_STATE_STREAMLINED) == false, "speedometer Streamlined must round-trip")
        assert(VRCompanion:isDisplayVisible("FillLevelsDisplay", VRCompanion.HUD_STATE_FULL) == true, "speedometer Full must round-trip")
        assert(VRCompanion:isDisplayVisible("GameInfoDisplay", VRCompanion.HUD_STATE_FULL) == false, "info Full must round-trip")
        assert(VRCompanion:isDisplayVisible("InputHelpDisplay", VRCompanion.HUD_STATE_STREAMLINED) == true, "controls Streamlined must round-trip")
    end)
    resetGroupVisibilityToDefaults()
    VRCompanion:configureHudSafeArea()
end)

test("MissingGroupVisibilityKeysFallBackToDefaultConstants", function()
    local store = {}
    withMockedSettingsFile(store, function()
        -- Only one key is present; a legacy file has none of them
        for _, group in pairs(VRCompanion.HUD_GROUPS) do group.streamlined, group.full = true, false end
        store["vrSettings.hudGroups.info.showStreamlined"] = true
        VRCompanion:loadConfiguration("settings.xml")
        for name, group in pairs(VRCompanion.HUD_GROUPS) do
            local expectedStreamlined = (name == "info") or VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED[name]
            assert(group.streamlined == expectedStreamlined, name .. " Streamlined falls back to its DEFAULT_* constant")
            assert(group.full == VRCompanion.DEFAULT_HUD_GROUP_FULL[name], name .. " Full falls back to its DEFAULT_* constant")
        end
    end)
    resetGroupVisibilityToDefaults()
    VRCompanion:configureHudSafeArea()
end)

test("FirstRunSavesEveryGroupVisibilityDefault", function()
    local store = {}
    local savedFiles = 0
    withMockedSettingsFile(store, function()
        _G.loadXMLFile = function() return 0 end -- no settings.xml yet
        _G.saveXMLFile = function() savedFiles = savedFiles + 1 end
        for _, group in pairs(VRCompanion.HUD_GROUPS) do group.streamlined, group.full = true, false end
        VRCompanion:loadConfiguration()
        assert(savedFiles == 1, "first run must create the settings file")
        for name, _ in pairs(VRCompanion.HUD_GROUPS) do
            assert(store["vrSettings.hudGroups." .. name .. ".showStreamlined"] == VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED[name], name .. " saved Streamlined default")
            assert(store["vrSettings.hudGroups." .. name .. ".showFull"] == VRCompanion.DEFAULT_HUD_GROUP_FULL[name], name .. " saved Full default")
        end
    end)
    resetGroupVisibilityToDefaults()
    VRCompanion:configureHudSafeArea()
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
    local safeArea = VRCompanion:loadConfiguration("settings.xml")
    assert(math.abs(safeArea.vertical - VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL) < 0.0001, "alias top inset keys must be ignored")
    assert(VRCompanion.showHudLoadNotification == VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION, "alias notification keys must be ignored")
    assert(VRCompanion.recenterVrTrackingSpace == VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE, "alias recenter key must be ignored")

    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
    VRCompanion:configureHudSafeArea()
end)

test("RuntimeStateStartsFromDefaultConstants", function()
    -- Load a fresh copy of the mod and check every setting's runtime value against its constant
    local saved = _G.VRCompanion
    _G.VRCompanion = nil
    local ok, err = pcall(function()
        loadCompanionMod()
        local fresh = _G.VRCompanion
        assert(fresh.hudSafeAreaHorizontal == fresh.DEFAULT_HUD_SAFE_AREA_HORIZONTAL, "HUD Safe Area horizontal starts at its default")
        assert(fresh.hudSafeAreaVertical == fresh.DEFAULT_HUD_SAFE_AREA_VERTICAL, "HUD Safe Area vertical starts at its default")
        assert(math.abs(fresh.hudAnchorLeftBase - fresh.DEFAULT_HUD_SAFE_AREA_HORIZONTAL) < 0.0001, "left anchor starts at the horizontal default")
        assert(math.abs(fresh.hudAnchorTopBase - (1.0 - fresh.DEFAULT_HUD_SAFE_AREA_VERTICAL)) < 0.0001, "top anchor starts at the vertical default")
        for _, groupName in ipairs(fresh.HUD_GROUP_NAMES) do
            local layout = fresh.hudGroupLayout[groupName]
            assert(layout.marginX == fresh.DEFAULT_HUD_GROUP_MARGIN_X, groupName .. " horizontal margin starts at its default")
            assert(layout.marginY == fresh.DEFAULT_HUD_GROUP_MARGIN_Y, groupName .. " vertical margin starts at its default")
            assert(layout.scale == fresh.DEFAULT_HUD_GROUP_SCALE, groupName .. " scale starts at its default")
        end
        assert(fresh.hudConvergenceDepth == fresh.DEFAULT_CONVERGENCE_DEPTH, "convergence depth starts at its default")
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
    for _, field in ipairs({ "showHudLoadNotification", "recenterVrTrackingSpace", "allowObliqueProjection" }) do
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
    VRCompanion:configureHudSafeArea(0.10, 0.10)
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do VRCompanion:setHudGroupLayout(groupName, 0.01, 0.02, 1.30) end
    VRCompanion.hudConvergenceDepth = nil
    VRCompanion.showHudLoadNotification = nil
    VRCompanion.recenterVrTrackingSpace = nil
    VRCompanion.allowObliqueProjection = nil
    VRCompanion.snapTurnAngle = nil
    VRCompanion.renderingMode = VRCompanion.RENDERING_MODE_AFR
    VRCompanion.renderingModeExplicit = nil

    local safeArea = VRCompanion:loadConfiguration()

    -- Every default is applied
    assert(math.abs(safeArea.horizontal - VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL) < 0.0001, "HUD Safe Area horizontal default applied")
    assert(math.abs(safeArea.vertical - VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL) < 0.0001, "HUD Safe Area vertical default applied")
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local layout = VRCompanion:getHudGroupLayout(groupName)
        assert(near(layout.marginX, VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X), groupName .. " horizontal margin default applied")
        assert(near(layout.marginY, VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y), groupName .. " vertical margin default applied")
        assert(near(layout.scale, VRCompanion.DEFAULT_HUD_GROUP_SCALE), groupName .. " scale default applied")
    end
    assert(VRCompanion.hudConvergenceDepth == VRCompanion.DEFAULT_CONVERGENCE_DEPTH, "convergence depth default applied")
    assert(VRCompanion.showHudLoadNotification == VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION, "load notification default applied")
    assert(VRCompanion.recenterVrTrackingSpace == VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE, "recenter tracking default applied")
    assert(VRCompanion.allowObliqueProjection == VRCompanion.DEFAULT_ALLOW_OBLIQUE_PROJECTION, "oblique projection default applied")
    assert(VRCompanion:getSnapTurnAngle() == VRCompanion.DEFAULT_SNAP_TURN_ANGLE, "snap turn default applied")
    assert(VRCompanion:getRenderingMode() == VRCompanion.DEFAULT_RENDERING_MODE, "stereo presentation mode default applied")
    assert(VRCompanion.renderingModeExplicit == true, "default mode is applied through setRenderingMode")
    assert(bridgeMode == VRCompanion.DEFAULT_RENDERING_MODE, "default mode must reach VRBridge")

    -- ...and the first-run settings file contains exactly those defaults
    assert(saveCount == 1, "first run must create the settings file")
    assert(math.abs(store["vrSettings.hudSafeAreaHorizontal"] - VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL) < 0.0001, "saved HUD Safe Area horizontal margin")
    assert(math.abs(store["vrSettings.hudSafeAreaVertical"] - VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL) < 0.0001, "saved HUD Safe Area vertical margin")
    assert(store["vrSettings.hudInsetHorizontal"] == nil and store["vrSettings.hudInsetVertical"] == nil
        and store["vrSettings.hudInsetTop"] == nil, "the removed inset keys are not written")
    assert(store["vrSettings.hudUiScale"] == nil, "the removed UI scale key is not written")
    for _, groupName in ipairs({ "Map", "Speedometer", "Info", "Controls" }) do
        local key = "vrSettings.hudGroup" .. groupName
        assert(near(store[key .. "MarginHorizontal"], VRCompanion.DEFAULT_HUD_GROUP_MARGIN_X), "saved " .. groupName .. " horizontal margin")
        assert(near(store[key .. "MarginVertical"], VRCompanion.DEFAULT_HUD_GROUP_MARGIN_Y), "saved " .. groupName .. " vertical margin")
        assert(near(store[key .. "Scale"], VRCompanion.DEFAULT_HUD_GROUP_SCALE), "saved " .. groupName .. " scale")
    end
    assert(math.abs(store["vrSettings.hudConvergenceDepth"] - VRCompanion.DEFAULT_CONVERGENCE_DEPTH) < 0.0001, "saved convergence depth")
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
    VRCompanion:configureHudSafeArea()
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

-- Speedometer group (spec #50, ticket #54): the fill levels are stacked directly under the speedometer
local function makeStackHud(fillLevelCount)
    local hud = makeHud()
    hud.speedMeter = makeDisplay(0.10, 0.12)
    hud.fillLevels = makeDisplay(0.08, 0.03)
    if fillLevelCount ~= nil then
        hud.fillLevels.fillLevelData = {}
        for i = 1, fillLevelCount do hud.fillLevels.fillLevelData[i] = {} end
    end
    VRCompanion.activeFillLevelsDisplay = nil
    return hud
end

test("StacksTheFillLevelsDirectlyUnderTheSpeedometerWithTheGroupsBottomMarginAtTheirBottom", function()
    local hud = makeStackHud()
    loadMapWith(hud)
    setGroup("speedometer", 0.35, 0.25, 0.50)

    -- Right edge at 1 - 0.35 = 0.65 for both; fill width 0.08, speedometer width 0.10
    assert(near(hud.fillLevels.position.x, 0.57) and near(hud.fillLevels.position.x + 0.08, 0.65), "fill levels are right-aligned with the group's horizontal margin")
    assert(near(hud.speedMeter.position.x + 0.10, 0.65), "the speedometer's right edge is on the same line as the fill levels'")
    assert(near(hud.fillLevels.position.y, 0.25), "the bottom margin is measured to the bottom of the fill levels")
    -- Fixed gap of 0.004 scaled by the group's scale 0.50 = 0.002, above the 0.03-high fill levels
    assert(near(hud.speedMeter.position.y, 0.25 + 0.03 + 0.002), "the speedometer sits above the fill levels, one scaled gap clear of them")
    unloadMap()
end)

test("MeasuresTheBottomMarginToTheLowestOfSeveralFillLevelsAndStacksTheSpeedometerAboveAllOfThem", function()
    local hud = makeStackHud(3)
    loadMapWith(hud)
    setGroup("speedometer", 0.35, 0.25, 0.50)

    -- Three 0.03-high levels with two row gaps of 0.004 * 0.50 = 0.002 between them
    assert(near(hud.fillLevels.position.y, 0.25), "the lowest fill level's bottom is on the bottom margin")
    assert(near(hud.speedMeter.position.y, 0.25 + 3 * 0.03 + 2 * 0.002 + 0.002), "the speedometer clears all three fill levels by the scaled gap")
    assert(near(hud.fillLevels.position.x + 0.08, 0.65) and near(hud.speedMeter.position.x + 0.10, 0.65), "right-aligned however many levels there are")
    unloadMap()
end)

test("ChangingTheSpeedometerGroupsMarginsMovesTheSpeedometerAndTheFillLevelsTogether", function()
    local hud = makeStackHud()
    loadMapWith(hud)
    setGroup("speedometer", 0.35, 0.25, 1.0)
    local fillBefore = { x = hud.fillLevels.position.x, y = hud.fillLevels.position.y }
    local smBefore = { x = hud.speedMeter.position.x, y = hud.speedMeter.position.y }

    setGroup("speedometer", 0.20, 0.10, 1.0)
    assert(near(hud.fillLevels.position.x - fillBefore.x, 0.15) and near(hud.fillLevels.position.y - fillBefore.y, -0.15), "fill levels follow the new margins")
    assert(near(hud.speedMeter.position.x - smBefore.x, 0.15) and near(hud.speedMeter.position.y - smBefore.y, -0.15), "the speedometer moves by exactly the same amount")
    assert(near(hud.speedMeter.position.y - hud.fillLevels.position.y, 0.03 + 0.004), "the stack keeps its fixed gap")
    unloadMap()
end)

test("ScalesTheGapBetweenTheSpeedometerAndTheFillLevelsWithTheGroupsScale", function()
    local hud = makeStackHud()
    loadMapWith(hud)
    setGroup("speedometer", 0.30, 0.20, 1.0)
    assert(near(hud.speedMeter.position.y - (hud.fillLevels.position.y + 0.03), 0.004), "gap at scale 1.0")
    setGroup("speedometer", 0.30, 0.20, 1.5)
    assert(near(hud.speedMeter.position.y - (hud.fillLevels.position.y + 0.03), 0.006), "gap at scale 1.5")
    unloadMap()
end)

test("DrawingTheFillLevelsRestacksTheSpeedometerWhenTheNumberOfFillLevelsChanges", function()
    local hud = makeStackHud(1)
    _G.FillLevelsDisplay = { draw = function() end }
    loadMapWith(hud)
    setGroup("speedometer", 0.35, 0.25, 0.50)
    VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
    assert(near(hud.speedMeter.position.y, 0.25 + 0.03 + 0.002), "one fill level")

    -- An implement with three fill levels is attached: the next draw of the fill levels restacks the group
    hud.fillLevels.fillLevelData = { {}, {}, {} }
    _G.FillLevelsDisplay.draw(hud.fillLevels)
    assert(near(hud.fillLevels.position.y, 0.25), "the lowest fill level stays on the bottom margin")
    assert(near(hud.speedMeter.position.y, 0.25 + 3 * 0.03 + 2 * 0.002 + 0.002), "the speedometer rises to clear all three")

    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    unloadMap()
    _G.FillLevelsDisplay = nil
    VRCompanion.activeFillLevelsDisplay = nil
end)

test("KeepsTheFillLevelsOnTheBottomMarginWithoutLiftingTheSpeedometerAwayFromTheScreenEdge", function()
    local hud = makeStackHud()
    loadMapWith(hud)
    setGroup("speedometer", 0.20, 0.0, 1.0)
    assert(near(hud.fillLevels.position.y, 0.0), "a zero bottom margin puts the fill levels' bottom on the screen edge")
    assert(near(hud.speedMeter.position.y, 0.03 + 0.004), "the speedometer is stacked above them, not pushed by an auto-lift")
    unloadMap()
end)

test("PlacesTheFillLevelsAndSpeedometerStackWhenTheHudLeavesHidden", function()
    local hud = makeStackHud()
    loadMapWith(hud)
    setGroup("speedometer", 0.35, 0.25, 0.50)
    hud.fillLevels.position, hud.speedMeter.position = nil, nil
    VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
    assert(hud.fillLevels.position ~= nil and near(hud.fillLevels.position.y, 0.25), "fill levels on the bottom margin")
    assert(hud.speedMeter.position ~= nil and near(hud.speedMeter.position.y, 0.25 + 0.03 + 0.002), "speedometer stacked above the fill levels")
    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    unloadMap()
end)

test("FillLevelStackingHasNoSettingsOfItsOwn", function()
    assert(VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE == nil and VRCompanion.fillLevelUnderVehicle == nil, "the fill-under-vehicle toggle is gone")
    assert(VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP == nil and VRCompanion.hudFillLevelGap == nil, "the adjustable fill level gap is gone")
    assert(VRCompanion.MIN_HUD_FILL_LEVEL_GAP == nil and VRCompanion.MAX_HUD_FILL_LEVEL_GAP == nil, "the fill level gap has no limits to adjust within")
    assert(VRCompanion.consoleCommandSetHudFillUnderVehicle == nil and VRCompanion.consoleCommandSetHudFillGap == nil, "their console handlers are gone")
end)

test("NeverWritesTheRemovedFillLevelKeysAndIgnoresThemWhenLoading", function()
    local store = {}
    withMockedSettingsFile(store, function()
        assert(VRCompanion:saveConfiguration("settings.xml") == true, "save must succeed")
        assert(store["vrSettings.fillLevelUnderVehicle"] == nil, "the fill-under-vehicle key is not written")
        assert(store["vrSettings.hudFillLevelGap"] == nil, "the fill level gap key is not written")

        -- A settings file from an earlier release carrying both keys must not change the stack
        store["vrSettings.fillLevelUnderVehicle"] = false
        store["vrSettings.hudFillLevelGap"] = 0.05
        VRCompanion:loadConfiguration("settings.xml")
        local hud = makeStackHud()
        loadMapWith(hud)
        setGroup("speedometer", 0.30, 0.20, 1.0)
        assert(near(hud.fillLevels.position.y, 0.20), "fill levels stay on the bottom margin whatever the old toggle said")
        assert(near(hud.speedMeter.position.y, 0.20 + 0.03 + 0.004), "the old gap value does not change the fixed gap")
        unloadMap()
    end)
end)

test("RemovedFillLevelConsoleCommandsAreNotRegistered", function()
    local registered = {}
    _G.addConsoleCommand = function(cmd, desc, funcName) registered[cmd] = funcName end
    _G.removeConsoleCommand = function() end
    loadMapWith(makeHud())
    assert(registered["vrHudGroup"] ~= nil, "the group command is still registered")
    for _, cmd in ipairs({ "vrHudFillUnderVehicle", "vrHudFillGap", "vrHudFillLevelGap" }) do
        assert(registered[cmd] == nil, cmd .. " must no longer be registered")
    end
    unloadMap()
    _G.addConsoleCommand = nil
    _G.removeConsoleCommand = nil
end)

test("ConvergencePositionsTopGroupsFromTheirOwnVerticalMargins", function()
    resetHudSettings()
    VRCompanion:configureHudSafeArea(0.35, 0.25)
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
    assert(math.abs(VRCompanion.hudAnchorTop - 0.75) < 0.0001, "the Safe Area's top anchor is unaffected by the groups")

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

-- vrHudSafeArea console command and the HUD Safe Area lifecycle (spec #50, ticket #55)
local function anchorsAre(left, right, bottom, top)
    return near(_G.g_hudAnchorLeft, left) and near(_G.g_hudAnchorRight, right)
        and near(_G.g_hudAnchorBottom, bottom) and near(_G.g_hudAnchorTop, top)
end

test("VrHudSafeAreaAppliesSavesAndConfirmsOnScreen", function()
    local hud = makeHud()
    loadMapWith(hud)
    local warnings = {}
    _G.g_currentMission.showBlinkingWarning = function(self, msg) table.insert(warnings, msg) end
    local saved = installSaveMocks()
    local before = {}
    for _, name in ipairs(DISPLAY_NAMES) do before[name] = hud[name].position end

    local result = VRCompanion:consoleCommandSetHudSafeArea("0.25", "0.18")

    assert(anchorsAre(0.25, 0.75, 0.18, 0.82), "the global HUD anchors apply immediately")
    assert(saved.count >= 1, "saves the configuration")
    assert(near(saved.store["vrSettings.hudSafeAreaHorizontal"], 0.25), "saved horizontal margin")
    assert(near(saved.store["vrSettings.hudSafeAreaVertical"], 0.18), "saved vertical margin")
    assert(#warnings == 1 and string.find(warnings[1], "Safe Area", 1, true) and string.find(warnings[1], "Saved", 1, true),
        "shows an on-screen confirmation")
    assert(type(result) == "string" and not string.find(result, "Usage", 1, true), "console reply confirms the change")
    assert(string.find(result, "0.25", 1, true) and string.find(result, "0.18", 1, true), "the reply names the new margins")
    -- The Safe Area does not move the groups
    assert(near(hud.ingameMap.position.x, 0.30) and near(hud.ingameMap.position.y, 0.22), "the map group stays on its own margins")
    assert(near(hud.speedMeter.position.x, 1.0 - 0.30 - 0.15), "the speedometer group stays on its own margins")

    removeSaveMocks()
    unloadMap()
end)

test("VrHudSafeAreaClampsItsMargins", function()
    loadMapWith(makeHud())
    VRCompanion:consoleCommandSetHudSafeArea("0.9", "0.9")
    assert(anchorsAre(0.40, 0.60, 0.35, 0.65), "excessive margins clamp to 0.40 / 0.35")
    VRCompanion:consoleCommandSetHudSafeArea("-1", "-1")
    assert(anchorsAre(0.0, 1.0, 0.0, 1.0), "negative margins clamp to 0")
    unloadMap()
end)

test("VrHudSafeAreaWithoutArgumentsPrintsUsageAndTheCurrentValues", function()
    loadMapWith(makeHud())
    VRCompanion:consoleCommandSetHudSafeArea("0.12", "0.34")
    local saved = installSaveMocks()

    local usage = VRCompanion:consoleCommandSetHudSafeArea()
    assert(string.find(usage, "Usage: vrHudSafeArea <horizontal> <vertical>", 1, true), "prints the usage")
    assert(string.find(usage, "horizontal=0.12", 1, true) and string.find(usage, "vertical=0.34", 1, true), "lists the current margins")
    assert(anchorsAre(0.12, 0.88, 0.34, 0.66), "printing the usage changes nothing")
    assert(saved.count == 0, "printing the usage saves nothing")

    removeSaveMocks()
    unloadMap()
end)

test("VrHudSafeAreaWithInvalidArgumentsPrintsUsageAndChangesNothing", function()
    loadMapWith(makeHud())
    local saved = installSaveMocks()
    local badCalls = { { "0.1" }, { "left", "0.1" }, { "0.1", "tall" }, { "nan", "0.1" }, { "0.1", "nan" }, { nil, "0.1" } }
    for i, args in ipairs(badCalls) do
        local result = VRCompanion:consoleCommandSetHudSafeArea(args[1], args[2])
        assert(string.find(result, "Usage: vrHudSafeArea", 1, true), "invalid call " .. i .. " must print the usage")
        assert(string.find(result, "horizontal=0.30, vertical=0.22", 1, true), "invalid call " .. i .. " lists the current values")
    end
    assert(anchorsAre(0.30, 0.70, 0.22, 0.78), "invalid input changes nothing")
    assert(saved.count == 0, "invalid input saves nothing")

    removeSaveMocks()
    unloadMap()
end)

test("HudSafeAreaCommandIsRegisteredAndTheRemovedInsetCommandsAreNot", function()
    local registered, removed = {}, {}
    _G.addConsoleCommand = function(cmd, desc, funcName) registered[cmd] = { desc = desc, funcName = funcName } end
    _G.removeConsoleCommand = function(cmd) removed[cmd] = true end
    loadMapWith(makeHud())
    assert(registered["vrHudSafeArea"] ~= nil and registered["vrHudSafeArea"].funcName == "consoleCommandSetHudSafeArea", "vrHudSafeArea is registered")
    for _, name in ipairs({ "vrHudInsets", "vrHudInsetTop", "vrHudTopInset" }) do
        assert(registered[name] == nil, name .. " must no longer be registered")
    end
    unloadMap()
    assert(removed["vrHudSafeArea"] == true, "vrHudSafeArea is removed on map unload")
    _G.addConsoleCommand = nil
    _G.removeConsoleCommand = nil
end)

test("MapUnloadRestoresTheFullScreenAnchorsWhateverTheSafeArea", function()
    loadMapWith(makeHud())
    VRCompanion:consoleCommandSetHudSafeArea("0.15", "0.10")
    assert(anchorsAre(0.15, 0.85, 0.10, 0.90), "the custom Safe Area is applied")
    unloadMap()
    assert(anchorsAre(0.0, 1.0, 0.0, 1.0), "the global anchors are back at the full screen")
end)

test("TwoDFloatingWindowModeUsesTheFullScreenAnchorsAndTheSafeAreaReturnsWithVr", function()
    local hud = makeHud()
    loadMapWith(hud)
    VRCompanion:set2DViewActive(true)
    assert(anchorsAre(0.0, 1.0, 0.0, 1.0), "2D Floating Window Mode keeps the full-screen anchors")

    -- A Safe Area set while in 2D saves, but the engine's layout stays untouched until VR returns
    VRCompanion:consoleCommandSetHudSafeArea("0.20", "0.12")
    assert(anchorsAre(0.0, 1.0, 0.0, 1.0), "the Safe Area does not reach the anchors in 2D Floating Window Mode")

    VRCompanion:set2DViewActive(false)
    assert(anchorsAre(0.20, 0.80, 0.12, 0.88), "returning to VR applies the Safe Area")
    unloadMap()
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d tests failed", failed))
end
