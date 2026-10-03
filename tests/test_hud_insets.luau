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

-- Slice 1: Anchor calculations, default values, and clamp boundaries
test("CalculatesDefaultHudAnchorsAndScale", function()
    assert(VRCompanion.DEFAULT_HUD_INSET_X == 0.20, "DEFAULT_HUD_INSET_X must be 0.20")
    assert(VRCompanion.DEFAULT_HUD_INSET_Y == 0.16, "DEFAULT_HUD_INSET_Y must be 0.16")
    assert(VRCompanion.DEFAULT_HUD_UI_SCALE == 0.88, "DEFAULT_HUD_UI_SCALE must be 0.88")

    local left, right, bottom, top, scale = VRCompanion:calculateHudAnchors()
    assert(math.abs(left - 0.20) < 0.0001, "Default left anchor must be 0.20")
    assert(math.abs(right - 0.80) < 0.0001, "Default right anchor must be 0.80")
    assert(math.abs(bottom - 0.16) < 0.0001, "Default bottom anchor must be 0.16")
    assert(math.abs(top - 0.84) < 0.0001, "Default top anchor must be 0.84")
    assert(math.abs(scale - 0.88) < 0.0001, "Default scale multiplier must be 0.88")
end)

test("CalculatesCustomHudAnchorsCorrectly", function()
    local left, right, bottom, top, scale = VRCompanion:calculateHudAnchors(0.10, 0.05, 1.0)
    assert(math.abs(left - 0.10) < 0.0001, "Custom left anchor must be 0.10")
    assert(math.abs(right - 0.90) < 0.0001, "Custom right anchor must be 0.90")
    assert(math.abs(bottom - 0.05) < 0.0001, "Custom bottom anchor must be 0.05")
    assert(math.abs(top - 0.95) < 0.0001, "Custom top anchor must be 0.95")
    assert(math.abs(scale - 1.0) < 0.0001, "Custom scale must be 1.0")
end)

test("ClampsHudAnchorsAndScaleWithinSafeBoundaries", function()
    -- Negative inputs clamped to min
    local leftMin, rightMin, bottomMin, topMin, scaleMin = VRCompanion:calculateHudAnchors(-0.10, -0.05, 0.20)
    assert(math.abs(leftMin - 0.0) < 0.0001, "Negative insetX clamped to 0.0")
    assert(math.abs(rightMin - 1.0) < 0.0001, "Right anchor clamped to 1.0")
    assert(math.abs(bottomMin - 0.0) < 0.0001, "Negative insetY clamped to 0.0")
    assert(math.abs(topMin - 1.0) < 0.0001, "Top anchor clamped to 1.0")
    assert(math.abs(scaleMin - 0.50) < 0.0001, "Scale clamped to min 0.50")

    -- Excessive inputs clamped to max
    local leftMax, rightMax, bottomMax, topMax, scaleMax = VRCompanion:calculateHudAnchors(0.50, 0.45, 2.50)
    assert(math.abs(leftMax - 0.40) < 0.0001, "Excessive insetX clamped to 0.40")
    assert(math.abs(rightMax - 0.60) < 0.0001, "Right anchor clamped to 0.60")
    assert(math.abs(bottomMax - 0.35) < 0.0001, "Excessive insetY clamped to 0.35")
    assert(math.abs(topMax - 0.65) < 0.0001, "Top anchor clamped to 0.65")
    assert(math.abs(scaleMax - 1.50) < 0.0001, "Scale clamped to max 1.50")
end)

-- Slice 2: Configuration API and metadata / XML fallback
test("ConfiguresHudInsetsAndQueriesState", function()
    VRCompanion:configureHudInsets(0.25, 0.18, 0.90)
    local insets = VRCompanion:getHudInsets()

    assert(math.abs(insets.left - 0.25) < 0.0001, "getHudInsets().left must match")
    assert(math.abs(insets.right - 0.75) < 0.0001, "getHudInsets().right must match")
    assert(math.abs(insets.bottom - 0.18) < 0.0001, "getHudInsets().bottom must match")
    assert(math.abs(insets.top - 0.82) < 0.0001, "getHudInsets().top must match")
    assert(math.abs(insets.insetX - 0.25) < 0.0001, "getHudInsets().insetX must match")
    assert(math.abs(insets.insetY - 0.18) < 0.0001, "getHudInsets().insetY must match")
    assert(math.abs(insets.scaleMultiplier - 0.90) < 0.0001, "getHudInsets().scaleMultiplier must match")
end)

test("LoadsConfigurationWithGracefulFallbackWhenUnspecified", function()
    -- Reset to nil or unconfigured
    VRCompanion.hudInsetX = nil
    VRCompanion.hudInsetY = nil
    VRCompanion.hudInsetTop = nil
    VRCompanion.hudUiScaleMultiplier = nil

    -- Call loadConfiguration with non-existent path
    local insets = VRCompanion:loadConfiguration("non_existent_file.xml")
    assert(math.abs(insets.left - 0.20) < 0.0001, "Must fall back to default left 0.20")
    assert(math.abs(insets.right - 0.80) < 0.0001, "Must fall back to default right 0.80")
    assert(math.abs(insets.bottom - 0.16) < 0.0001, "Must fall back to default bottom 0.16")
    assert(math.abs(insets.top - 0.84) < 0.0001, "Must fall back to default top 0.84")
    assert(math.abs(insets.scaleMultiplier - 0.88) < 0.0001, "Must fall back to default scale 0.88")
end)

test("LoadsConfigurationFromXmlWhenSpecified", function()
    -- Mock GIANTS XML functions
    _G.mockXmlData = {
        ["modDesc.vrSettings.hudInsetHorizontal"] = 0.22,
        ["modDesc.vrSettings.hudInsetVertical"] = 0.15,
        ["modDesc.vrSettings.hudUiScale"] = 0.85
    }
    _G.loadXMLFile = function(tag, path) return 123 end
    _G.getXMLFloat = function(xmlFile, key)
        return _G.mockXmlData[key]
    end
    _G.delete = function(xmlFile) end

    local insets = VRCompanion:loadConfiguration("mockModDesc.xml")
    assert(math.abs(insets.left - 0.22) < 0.0001, "Loaded left anchor must be 0.22")
    assert(math.abs(insets.right - 0.78) < 0.0001, "Loaded right anchor must be 0.78")
    assert(math.abs(insets.bottom - 0.15) < 0.0001, "Loaded bottom anchor must be 0.15")
    assert(math.abs(insets.top - 0.85) < 0.0001, "Loaded top anchor must be 0.85")
    assert(math.abs(insets.scaleMultiplier - 0.85) < 0.0001, "Loaded scale must be 0.85")

    -- Clean up XML mocks
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    _G.mockXmlData = nil
end)

-- Slice 3: Standard HUD display repositioning and map load / delete lifecycle
test("AppliesDefaultVRHudInsetsUponMapLoad", function()
    -- Set default 2D anchors
    _G.g_hudAnchorLeft = 0.0
    _G.g_hudAnchorRight = 1.0
    _G.g_hudAnchorBottom = 0.0
    _G.g_hudAnchorTop = 1.0

    -- Setup mock HUD with standard displays
    local mockDisplays = {
        speedMeter = { name = "SpeedMeterDisplay", storeScaledValuesCalled = false, position = nil, scale = nil,
            storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
            setScale = function(self, s) self.scale = s end,
            setPosition = function(self, x, y) self.position = { x = x, y = y } end
        },
        inputHelp = { name = "InputHelpDisplay", storeScaledValuesCalled = false, position = nil,
            storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
            setPosition = function(self, x, y) self.position = { x = x, y = y } end
        },
        gameInfo = { name = "GameInfoDisplay", storeScaledValuesCalled = false, position = nil,
            storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
            setPosition = function(self, x, y) self.position = { x = x, y = y } end
        },
        contextAction = { name = "ContextActionDisplay", storeScaledValuesCalled = false, position = nil,
            storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
            setPosition = function(self, x, y) self.position = { x = x, y = y } end
        },
        ingameMap = { name = "IngameMap", storeScaledValuesCalled = false, position = nil,
            storeScaledValues = function(self) self.storeScaledValuesCalled = true end,
            setPosition = function(self, x, y) self.position = { x = x, y = y } end
        }
    }

    _G.g_currentMission = {
        hud = mockDisplays
    }

    -- Reset to default config before loading
    VRCompanion:configureHudInsets()

    -- Simulate map load
    VRCompanion:loadMap("mapUS")

    -- Global engine anchors must contract inward by 20% horizontal and 16% vertical
    assert(math.abs(_G.g_hudAnchorLeft - 0.20) < 0.0001, "g_hudAnchorLeft must be 0.20 upon map load")
    assert(math.abs(_G.g_hudAnchorRight - 0.80) < 0.0001, "g_hudAnchorRight must be 0.80 upon map load")
    assert(math.abs(_G.g_hudAnchorBottom - 0.16) < 0.0001, "g_hudAnchorBottom must be 0.16 upon map load")
    assert(math.abs(_G.g_hudAnchorTop - 0.84) < 0.0001, "g_hudAnchorTop must be 0.84 upon map load")

    -- UI scale multiplier applied (0.88x)
    assert(math.abs(VRCompanion.effectiveUiScale - 0.88) < 0.0001, "effectiveUiScale must be 0.88")
    assert(mockDisplays.speedMeter.scale == 0.88, "SpeedMeterDisplay must receive 0.88 scale")

    -- All 5 standard HUD displays must have repositioned / updated
    assert(mockDisplays.speedMeter.storeScaledValuesCalled == true, "SpeedMeterDisplay.storeScaledValues must be called")
    assert(mockDisplays.inputHelp.storeScaledValuesCalled == true, "InputHelpDisplay.storeScaledValues must be called")
    assert(mockDisplays.gameInfo.storeScaledValuesCalled == true, "GameInfoDisplay.storeScaledValues must be called")
    assert(mockDisplays.contextAction.storeScaledValuesCalled == true, "ContextActionDisplay.storeScaledValues must be called")
    assert(mockDisplays.ingameMap.storeScaledValuesCalled == true, "IngameMap.storeScaledValues must be called")

    -- Verify specific display positions match configured VR safe insets
    assert(mockDisplays.speedMeter.position ~= nil, "SpeedMeterDisplay must have position set")
    assert(math.abs(mockDisplays.speedMeter.position.x - 0.80) < 0.0001, "SpeedMeterDisplay must anchor to right 0.80")
    assert(math.abs(mockDisplays.speedMeter.position.y - 0.16) < 0.0001, "SpeedMeterDisplay must anchor to bottom 0.16")

    assert(mockDisplays.inputHelp.position ~= nil, "InputHelpDisplay must have position set")
    assert(math.abs(mockDisplays.inputHelp.position.x - 0.20) < 0.0001, "InputHelpDisplay must anchor to left 0.20")
    assert(math.abs(mockDisplays.inputHelp.position.y - 0.84) < 0.0001, "InputHelpDisplay must anchor to top 0.84")

    assert(mockDisplays.gameInfo.position ~= nil, "GameInfoDisplay must have position set")
    assert(math.abs(mockDisplays.gameInfo.position.x - 0.80) < 0.0001, "GameInfoDisplay must anchor to right 0.80")
    assert(math.abs(mockDisplays.gameInfo.position.y - 0.84) < 0.0001, "GameInfoDisplay must anchor to top 0.84")

    assert(mockDisplays.contextAction.position ~= nil, "ContextActionDisplay must have position set")
    assert(math.abs(mockDisplays.contextAction.position.x - 0.20) < 0.0001, "ContextActionDisplay must anchor to left 0.20")
    assert(math.abs(mockDisplays.contextAction.position.y - 0.16) < 0.0001, "ContextActionDisplay must anchor to bottom 0.16")

    assert(mockDisplays.ingameMap.position ~= nil, "IngameMap must have position set")
    assert(math.abs(mockDisplays.ingameMap.position.x - 0.20) < 0.0001, "IngameMap must anchor to left 0.20")
    assert(math.abs(mockDisplays.ingameMap.position.y - 0.16) < 0.0001, "IngameMap must anchor to bottom 0.16")

    -- Clean up map lifecycle
    VRCompanion:deleteMap()
end)

test("PositionsIndividualDisplaysViaHelper", function()
    local sm = { storeScaledValues = function(self) self.stored = true end }
    VRCompanion:positionHudDisplay(sm, "SpeedMeterDisplay")
    assert(sm.stored == true, "positionHudDisplay must trigger storeScaledValues")
    assert(math.abs(sm.anchorX - 0.80) < 0.0001, "SpeedMeter anchorX must be 0.80")
    assert(math.abs(sm.anchorY - 0.16) < 0.0001, "SpeedMeter anchorY must be 0.16")

    local ih = {}
    VRCompanion:positionHudDisplay(ih, "InputHelpDisplay")
    assert(math.abs(ih.anchorX - 0.20) < 0.0001, "InputHelp anchorX must be 0.20")
    assert(math.abs(ih.anchorY - 0.84) < 0.0001, "InputHelp anchorY must be 0.84")

    local gi = {}
    VRCompanion:positionHudDisplay(gi, "GameInfoDisplay")
    assert(math.abs(gi.anchorX - 0.80) < 0.0001, "GameInfo anchorX must be 0.80")
    assert(math.abs(gi.anchorY - 0.84) < 0.0001, "GameInfo anchorY must be 0.84")

    local ca = {}
    VRCompanion:positionHudDisplay(ca, "ContextActionDisplay")
    assert(math.abs(ca.anchorX - 0.20) < 0.0001, "ContextAction anchorX must be 0.20")
    assert(math.abs(ca.anchorY - 0.16) < 0.0001, "ContextAction anchorY must be 0.16")

    local map = {}
    VRCompanion:positionHudDisplay(map, "IngameMap")
    assert(math.abs(map.anchorX - 0.20) < 0.0001, "IngameMap anchorX must be 0.20")
    assert(math.abs(map.anchorY - 0.16) < 0.0001, "IngameMap anchorY must be 0.16")
end)

test("PositionsDisplaysWithDimensionsOffset", function()
    local smWithDim = {
        width = 0.15,
        height = 0.10,
        storeScaledValues = function(self) self.stored = true end
    }
    VRCompanion:positionHudDisplay(smWithDim, "SpeedMeterDisplay")
    -- SpeedMeter is bottom-right: anchorX = right (0.80) - width (0.15) = 0.65, anchorY = bottom (0.16)
    assert(math.abs(smWithDim.anchorX - 0.65) < 0.0001, "SpeedMeter with width must anchor to 0.65")
    assert(math.abs(smWithDim.anchorY - 0.16) < 0.0001, "SpeedMeter anchorY must be 0.16")

    local giWithDim = {
        getWidth = function() return 0.20 end,
        getHeight = function() return 0.12 end
    }
    VRCompanion:positionHudDisplay(giWithDim, "GameInfoDisplay")
    -- GameInfo is top-right: anchorX = 0.80 - 0.20 = 0.60, anchorY = 0.84 - 0.12 = 0.72
    assert(math.abs(giWithDim.anchorX - 0.60) < 0.0001, "GameInfo with width must anchor to 0.60")
    assert(math.abs(giWithDim.anchorY - 0.72) < 0.0001, "GameInfo with height must anchor to 0.72")
end)

test("RestoresOriginalHudAnchorsAndUiScaleOnMapUnload", function()
    _G.GameSettings = { SETTING = { UI_SCALE = "uiScale" } }
    _G.g_gameSettings = {
        getValue = function(self, key)
            if key == "uiScale" then return 1.20 end
            return 1.0
        end
    }

    local mockHud = {
        speedMeter = {
            scale = 1.0,
            setScale = function(self, s) self.scale = s end
        }
    }
    _G.g_currentMission = { hud = mockHud }

    -- Load map with custom base UI scale (1.20)
    VRCompanion:configureHudInsets()
    VRCompanion:loadMap("mapUS")

    -- Effective scale: 1.20 * 0.88 = 1.056
    assert(math.abs(VRCompanion.effectiveUiScale - 1.056) < 0.0001, "Effective scale should be base 1.20 * 0.88")
    assert(math.abs(mockHud.speedMeter.scale - 1.056) < 0.0001, "SpeedMeter display scale set to 1.056")

    -- Unload map
    VRCompanion:deleteMap()

    -- Original 2D full-screen corner anchors and base UI scale (1.20) must be restored
    assert(math.abs(_G.g_hudAnchorLeft - 0.0) < 0.0001, "g_hudAnchorLeft restored to 0.0")
    assert(math.abs(_G.g_hudAnchorRight - 1.0) < 0.0001, "g_hudAnchorRight restored to 1.0")
    assert(math.abs(_G.g_hudAnchorBottom - 0.0) < 0.0001, "g_hudAnchorBottom restored to 0.0")
    assert(math.abs(_G.g_hudAnchorTop - 1.0) < 0.0001, "g_hudAnchorTop restored to 1.0")
    assert(math.abs(mockHud.speedMeter.scale - 1.20) < 0.0001, "SpeedMeter display scale restored to base 1.20")

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

    VRCompanion:configureHudInsets(0.32, 0.24, 0.80)
    VRCompanion.hudConvergenceDepth = 1.9
    local success = VRCompanion:saveConfiguration()

    assert(success == true, "saveConfiguration should succeed with mock XML functions")
    assert(math.abs(savedData["vrSettings.hudInsetHorizontal"] - 0.32) < 0.0001, "Saved hudInsetHorizontal should be 0.32")
    assert(math.abs(savedData["vrSettings.hudInsetVertical"] - 0.24) < 0.0001, "Saved hudInsetVertical should be 0.24")
    assert(math.abs(savedData["vrSettings.hudUiScale"] - 0.80) < 0.0001, "Saved hudUiScale should be 0.80")
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

test("LoadsUserSettingsXmlOverridingModDescDefaults", function()
    _G.getUserProfileAppPath = function() return "C:/Users/test/Documents/My Games/FarmingSimulator2025/" end
    _G.loadXMLFile = function(tag, path)
        if string.find(path, "modSettings") then
            return 101 -- user settings file handle
        elseif string.find(path, "modDesc") then
            return 102 -- modDesc file handle
        end
        return 0
    end
    _G.getXMLFloat = function(xmlFile, key)
        if xmlFile == 102 then
            -- modDesc defaults
            if key == "modDesc.vrSettings.hudInsetHorizontal" then return 0.20 end
            if key == "modDesc.vrSettings.hudInsetVertical" then return 0.16 end
            if key == "modDesc.vrSettings.hudUiScale" then return 0.88 end
        elseif xmlFile == 101 then
            -- user settings override
            if key == "vrSettings.hudInsetHorizontal" then return 0.33 end
            if key == "vrSettings.hudInsetVertical" then return 0.25 end
            if key == "vrSettings.hudUiScale" then return 0.75 end
            if key == "vrSettings.hudConvergenceDepth" then return 2.1 end
        end
        return nil
    end
    _G.delete = function(xmlFile) end

    VRCompanion.modDirectory = "C:/mockModDir/"
    local insets = VRCompanion:loadConfiguration()

    assert(math.abs(insets.left - 0.33) < 0.0001, "User settings should override left to 0.33")
    assert(math.abs(insets.bottom - 0.25) < 0.0001, "User settings should override bottom to 0.25")
    assert(math.abs(insets.scaleMultiplier - 0.75) < 0.0001, "User settings should override scale to 0.75")
    assert(math.abs(VRCompanion.hudConvergenceDepth - 2.1) < 0.0001, "User settings should override convergence depth to 2.1")

    VRCompanion.modDirectory = ""
    _G.getUserProfileAppPath = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
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

test("PositionsFillLevelsDirectlyUnderVehicleSpeedMeter", function()
    VRCompanion:configureHudInsets(0.35, 0.25, 0.50) -- User's exact configured settings
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
    VRCompanion:configureHudInsets(0.20, 0.02, 1.0)
    VRCompanion.fillLevelUnderVehicle = true
    VRCompanion.hudFillLevelGap = 0.004

    local mockSmPos = nil
    local mockSpeedMeter = {
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
    local left, right, bottom, top, scale = VRCompanion:calculateHudAnchors(0.35, 0.25, 0.50, 0.38)
    assert(math.abs(left - 0.35) < 0.0001, "Left anchor must be 0.35")
    assert(math.abs(right - 0.65) < 0.0001, "Right anchor must be 0.65")
    assert(math.abs(bottom - 0.25) < 0.0001, "Bottom anchor must be 0.25")
    assert(math.abs(top - 0.62) < 0.0001, "Top anchor must be 1.0 - 0.38 = 0.62")
    assert(math.abs(scale - 0.50) < 0.0001, "Scale must be 0.50")
end)

test("ClampsTopInsetWithinSafeBoundaries", function()
    local _, _, _, topMin = VRCompanion:calculateHudAnchors(0.20, 0.16, 0.88, -0.10)
    assert(math.abs(topMin - 1.0) < 0.0001, "Negative top inset clamped to 0.0 -> top anchor 1.0")

    local _, _, _, topMax = VRCompanion:calculateHudAnchors(0.20, 0.16, 0.88, 0.80)
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
    VRCompanion:configureHudInsets(VRCompanion.DEFAULT_HUD_INSET_X, VRCompanion.DEFAULT_HUD_INSET_Y, VRCompanion.DEFAULT_HUD_UI_SCALE)
end)

test("ConvergencePositionsTopElementsAtCustomTopAnchor", function()
    VRCompanion:configureHudInsets(0.35, 0.25, 0.50, 0.38)
    local mockIhPos = nil
    local mockGiPos = nil
    local mockIh = { setPosition = function(self, x, y) mockIhPos = { x = x, y = y } end }
    local mockGi = { setPosition = function(self, x, y) mockGiPos = { x = x, y = y } end }
    _G.g_currentMission = {
        hud = {
            inputHelp = mockIh,
            gameInfo = mockGi
        }
    }
    _G.VRBridge = { getTrackingState = function() return { headPose = { parity = 0, ipd = 0.064 } } end }

    VRCompanion:updateHudConvergence()
    assert(mockIhPos ~= nil, "InputHelp must be positioned")
    assert(mockGiPos ~= nil, "GameInfo must be positioned")
    assert(math.abs(mockIhPos.y - 0.62) < 0.0001, "InputHelp must be at top anchor 0.62")
    assert(math.abs(mockGiPos.y - 0.62) < 0.0001, "GameInfo must be at top anchor 0.62")

    _G.g_currentMission = nil
    _G.VRBridge = nil
    VRCompanion.hudInsetTop = nil
    VRCompanion:configureHudInsets(VRCompanion.DEFAULT_HUD_INSET_X, VRCompanion.DEFAULT_HUD_INSET_Y, VRCompanion.DEFAULT_HUD_UI_SCALE)
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

print(string.format("Summary: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d tests failed", failed))
end
