-- ====================================================================
-- Test Suite for VR HUD Convergence Parity
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

print("Running VR HUD Convergence Parity Tests...")

-- Every setting back to its default (HUD Group Margins included), then the HUD Safe Area set
local function useSafeArea(horizontal, vertical)
    VRCompanion:loadConfiguration("non_existent_file.xml")
    VRCompanion:configureHudSafeArea(horizontal, vertical)
end

-- Slice 1: Mathematical scaling, target depth, fallback, and boundary clamping
test("DefinesConvergenceConstantsAndDefaults", function()
    assert(VRCompanion.DEFAULT_CONVERGENCE_DEPTH == 1.8, "DEFAULT_CONVERGENCE_DEPTH must be 1.8m")
    assert(VRCompanion.DEFAULT_CONVERGENCE_DISPARITY == 0.012, "DEFAULT_CONVERGENCE_DISPARITY must be 0.012")
    assert(VRCompanion.DEFAULT_IPD == 0.064, "DEFAULT_IPD must be 0.064m")
    assert(VRCompanion.MIN_IPD == 0.040, "MIN_IPD must be 0.040m")
    assert(VRCompanion.MAX_IPD == 0.085, "MAX_IPD must be 0.085m")
    assert(VRCompanion.MIN_CONVERGENCE_DISPARITY == 0.0, "MIN_CONVERGENCE_DISPARITY must be 0.0")
    assert(VRCompanion.MAX_CONVERGENCE_DISPARITY == 0.040, "MAX_CONVERGENCE_DISPARITY must be 0.040")
end)

test("ComputesConvergenceDisparityForDefaultIpdAtTargetDepth", function()
    local disparity = VRCompanion:computeConvergenceDisparity(0.064, 1.8)
    assert(math.abs(disparity - 0.012) < 0.0001, string.format("Default IPD (0.064) at 1.8m depth must yield 0.012, got %f", disparity))
end)

test("FallsBackToSafeDisparityWhenIpdIsNilOrInvalid", function()
    local disparityNil = VRCompanion:computeConvergenceDisparity(nil, 1.8)
    assert(math.abs(disparityNil - 0.012) < 0.0001, "Nil IPD must fall back to 0.012")

    local disparityZero = VRCompanion:computeConvergenceDisparity(0.0, 1.8)
    assert(math.abs(disparityZero - 0.012) < 0.0001, "Zero IPD must fall back to 0.012")

    local disparityNeg = VRCompanion:computeConvergenceDisparity(-0.05, 1.8)
    assert(math.abs(disparityNeg - 0.012) < 0.0001, "Negative IPD must fall back to 0.012")
end)

test("ScalesDisparityLinearlyWithHeadsetIpd", function()
    -- 70mm IPD
    local disparity70 = VRCompanion:computeConvergenceDisparity(0.070, 1.8)
    local expected70 = 0.012 * (0.070 / 0.064)
    assert(math.abs(disparity70 - expected70) < 0.0001, string.format("70mm IPD should yield %f, got %f", expected70, disparity70))

    -- 58mm IPD
    local disparity58 = VRCompanion:computeConvergenceDisparity(0.058, 1.8)
    local expected58 = 0.012 * (0.058 / 0.064)
    assert(math.abs(disparity58 - expected58) < 0.0001, string.format("58mm IPD should yield %f, got %f", expected58, disparity58))
end)

test("ScalesDisparityInverselyWithVirtualDepth", function()
    -- Closer depth (0.9m) -> double disparity
    local disparityNear = VRCompanion:computeConvergenceDisparity(0.064, 0.9)
    assert(math.abs(disparityNear - 0.024) < 0.0001, string.format("0.9m depth should yield 0.024, got %f", disparityNear))

    -- Farther depth (3.6m) -> half disparity
    local disparityFar = VRCompanion:computeConvergenceDisparity(0.064, 3.6)
    assert(math.abs(disparityFar - 0.006) < 0.0001, string.format("3.6m depth should yield 0.006, got %f", disparityFar))
end)

test("ClampsDisparityAndIpdWithinSafeBoundaries", function()
    -- Extremely small IPD clamped to MIN_IPD (0.040)
    local disparityMinIpd = VRCompanion:computeConvergenceDisparity(0.010, 1.8)
    local expectedMin = 0.012 * (0.040 / 0.064)
    assert(math.abs(disparityMinIpd - expectedMin) < 0.0001, "Small IPD must be clamped to MIN_IPD")

    -- Extremely large IPD clamped to MAX_IPD (0.085)
    local disparityMaxIpd = VRCompanion:computeConvergenceDisparity(0.200, 1.8)
    local expectedMax = 0.012 * (0.085 / 0.064)
    assert(math.abs(disparityMaxIpd - expectedMax) < 0.0001, "Large IPD must be clamped to MAX_IPD")

    -- Extremely close depth clamped to MAX_CONVERGENCE_DISPARITY (0.040)
    local disparityExtNear = VRCompanion:computeConvergenceDisparity(0.085, 0.1)
    assert(disparityExtNear <= VRCompanion.MAX_CONVERGENCE_DISPARITY, "Extreme near depth must not exceed MAX_CONVERGENCE_DISPARITY")
    assert(disparityExtNear >= VRCompanion.MIN_CONVERGENCE_DISPARITY, "Must not be below MIN_CONVERGENCE_DISPARITY")
end)

-- Slice 2: Parity directionality and frame parity offset calculation
test("CalculatesPositiveConvergenceOffsetOnLeftEyeParity0", function()
    local offset = VRCompanion:getConvergenceOffset(0, 0.012)
    assert(math.abs(offset - 0.012) < 0.0001, "Parity 0 (Left eye) must yield positive +0.012 offset")
end)

test("CalculatesNegativeConvergenceOffsetOnRightEyeParity1", function()
    local offset = VRCompanion:getConvergenceOffset(1, 0.012)
    assert(math.abs(offset - (-0.012)) < 0.0001, "Parity 1 (Right eye) must yield negative -0.012 offset")
end)

test("AlternatesOffsetSignAcrossArbitrarySequentialParityIndices", function()
    -- Even indices (Left eye) -> positive
    assert(math.abs(VRCompanion:getConvergenceOffset(2, 0.015) - 0.015) < 0.0001)
    assert(math.abs(VRCompanion:getConvergenceOffset(100, 0.015) - 0.015) < 0.0001)

    -- Odd indices (Right eye) -> negative
    assert(math.abs(VRCompanion:getConvergenceOffset(3, 0.015) - (-0.015)) < 0.0001)
    assert(math.abs(VRCompanion:getConvergenceOffset(101, 0.015) - (-0.015)) < 0.0001)
end)

test("ResolvesOffsetFromBridgeWhenParityOmitted", function()
    _G.VRBridge = {
        getFrameParity = function() return 0 end,
        getIpd = function() return 0.064 end
    }
    local offsetLeft = VRCompanion:getConvergenceOffset()
    assert(math.abs(offsetLeft - 0.012) < 0.0001, "Should read parity 0 from VRBridge and return +0.012")

    _G.VRBridge.getFrameParity = function() return 1 end
    local offsetRight = VRCompanion:getConvergenceOffset()
    assert(math.abs(offsetRight - (-0.012)) < 0.0001, "Should read parity 1 from VRBridge and return -0.012")

    _G.VRBridge = nil
end)

-- Slice 3: Frame anchor updates and drift-free preservation
test("UpdatesHorizontalAnchorsWithPositiveOffsetOnParity0", function()
    useSafeArea(0.20, 0.16)
    _G.VRBridge = {
        getFrameParity = function() return 0 end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local left, right, offset = VRCompanion:updateHudConvergence()
    assert(math.abs(offset - 0.012) < 0.0001, "Offset must be +0.012")
    assert(math.abs(left - 0.212) < 0.0001, "Left anchor must be 0.20 + 0.012 = 0.212")
    assert(math.abs(right - 0.812) < 0.0001, "Right anchor must be 0.80 + 0.012 = 0.812")
    assert(math.abs(_G.g_hudAnchorLeft - 0.212) < 0.0001, "_G.g_hudAnchorLeft must be 0.212")
    assert(math.abs(_G.g_hudAnchorRight - 0.812) < 0.0001, "_G.g_hudAnchorRight must be 0.812")

    _G.VRBridge = nil
end)

test("UpdatesHorizontalAnchorsWithNegativeOffsetOnParity1", function()
    useSafeArea(0.20, 0.16)
    _G.VRBridge = {
        getFrameParity = function() return 1 end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local left, right, offset = VRCompanion:updateHudConvergence()
    assert(math.abs(offset - (-0.012)) < 0.0001, "Offset must be -0.012")
    assert(math.abs(left - 0.188) < 0.0001, "Left anchor must be 0.20 - 0.012 = 0.188")
    assert(math.abs(right - 0.788) < 0.0001, "Right anchor must be 0.80 - 0.012 = 0.788")
    assert(math.abs(_G.g_hudAnchorLeft - 0.188) < 0.0001, "_G.g_hudAnchorLeft must be 0.188")
    assert(math.abs(_G.g_hudAnchorRight - 0.788) < 0.0001, "_G.g_hudAnchorRight must be 0.788")

    _G.VRBridge = nil
end)

test("MaintainsBaseAnchorsInvariantlyAcrossTenThousandFramesWithoutDrift", function()
    useSafeArea(0.20, 0.16)
    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    for frame = 1, 10000 do
        curParity = frame % 2
        VRCompanion:updateHudConvergence()
    end

    -- Verify base anchors remain strictly invariant
    assert(math.abs(VRCompanion.hudAnchorLeftBase - 0.20) < 0.0000001, "hudAnchorLeftBase must not drift")
    assert(math.abs(VRCompanion.hudAnchorRightBase - 0.80) < 0.0000001, "hudAnchorRightBase must not drift")

    -- Final frame was 10000 % 2 == 0 (parity 0)
    assert(math.abs(VRCompanion.hudAnchorLeft - 0.212) < 0.0000001, "Final frame left anchor must be exactly 0.212")
    assert(math.abs(VRCompanion.hudAnchorRight - 0.812) < 0.0000001, "Final frame right anchor must be exactly 0.812")

    _G.VRBridge = nil
end)

test("RepositionsActiveHudDisplaysWithFrameConvergenceDisparity", function()
    useSafeArea(0.20, 0.16)
    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local mockSpeedMeter = {
        name = "SpeedMeterDisplay",
        position = nil,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }
    _G.g_currentMission = {
        hud = {
            speedMeter = mockSpeedMeter
        }
    }
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

    -- Frame 0 (Parity 0 -> the Speedometer group's right margin edge shifts by +0.012: 1 - 0.30 + 0.012 = 0.712)
    curParity = 0
    VRCompanion:updateHudConvergence()
    assert(mockSpeedMeter.position ~= nil)
    assert(math.abs(mockSpeedMeter.position.x - 0.712) < 0.0001, "SpeedMeter aligned with parity 0 shifted margin edge")

    -- Frame 1 (Parity 1 -> shifts by -0.012: 1 - 0.30 - 0.012 = 0.688)
    curParity = 1
    VRCompanion:updateHudConvergence()
    assert(math.abs(mockSpeedMeter.position.x - 0.688) < 0.0001, "SpeedMeter aligned with parity 1 shifted margin edge")

    _G.g_currentMission = nil
    _G.VRBridge = nil
end)

-- HUD Groups (spec #50, ticket #51): the per-frame placement takes each display's home corner from its group
test("ConvergencePlacementTakesTheHomeCornerFromTheDisplaysGroup", function()
    useSafeArea(0.20, 0.16)
    _G.VRBridge = {
        getFrameParity = function() return 0 end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local mockInfo = {
        position = nil,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }
    _G.g_currentMission = { hud = { gameInfo = mockInfo } }
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

    VRCompanion:updateHudConvergence()
    assert(math.abs(mockInfo.position.x - (1.0 - 0.30 + 0.012)) < 0.0001, "Info group is anchored to the right edge")
    assert(math.abs(mockInfo.position.y - (1.0 - 0.30)) < 0.0001, "Info group is anchored to the top edge")

    local info = VRCompanion.HUD_GROUPS.info
    local origX, origY = info.alignX, info.alignY
    local ok, err = pcall(function()
        info.alignX, info.alignY = "left", "bottom"
        VRCompanion:updateHudConvergence()
        assert(math.abs(mockInfo.position.x - (0.30 + 0.012)) < 0.0001, "Moving the group's corner moves the display to the left edge")
        assert(math.abs(mockInfo.position.y - 0.30) < 0.0001, "Moving the group's corner moves the display to the bottom edge")
    end)
    info.alignX, info.alignY = origX, origY

    _G.g_currentMission = nil
    _G.VRBridge = nil
    assert(ok, err)
end)

test("ConvergenceDisparitySynchronizesSpeedMeterAndStackedFillLevelsAcrossParity", function()
    useSafeArea(0.20, 0.16)
    VRCompanion:consoleCommandSetHudGroup("speedometer", "0.35", "0.25", "0.5")
    VRCompanion.activeFillLevelsDisplay = nil

    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local mockSpeedMeter = {
        name = "SpeedMeterDisplay",
        width = 0.10,
        height = 0.12,
        position = nil,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }
    local mockFill = {
        name = "FillLevelsDisplay",
        width = 0.08,
        height = 0.03,
        position = nil,
        setPosition = function(self, x, y) self.position = { x = x, y = y } end
    }
    _G.g_currentMission = {
        hud = {
            speedMeter = mockSpeedMeter,
            fillLevels = mockFill
        }
    }
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

    -- Frame 0: Left Eye (Parity 0 -> the group's right margin edge = 0.65 + 0.012 = 0.662)
    curParity = 0
    VRCompanion:updateHudConvergence()
    assert(mockSpeedMeter.position ~= nil)
    assert(mockFill.position ~= nil)
    assert(math.abs(mockSpeedMeter.position.x - (0.662 - 0.10)) < 0.0001, "SpeedMeter must receive +0.012 disparity (0.662 - its 0.10 width)")
    assert(math.abs(mockFill.position.x - 0.582) < 0.0001, "FillLevels must receive matching +0.012 disparity (0.662 - 0.08 = 0.582)")
    assert(math.abs(mockFill.position.y - 0.25) < 0.0001, "FillLevels sit on the Speedometer group's bottom margin")
    assert(math.abs(mockSpeedMeter.position.y - (0.25 + 0.03 + 0.002)) < 0.0001, "SpeedMeter is stacked above the fill levels, one scaled gap clear")

    -- Frame 1: Right Eye (Parity 1 -> right anchor = 0.65 - 0.012 = 0.638)
    curParity = 1
    VRCompanion:updateHudConvergence()
    assert(math.abs(mockSpeedMeter.position.x - (0.638 - 0.10)) < 0.0001, "SpeedMeter must receive -0.012 disparity (0.638 - its 0.10 width)")
    assert(math.abs(mockFill.position.x - 0.558) < 0.0001, "FillLevels must receive matching -0.012 disparity (0.638 - 0.08 = 0.558)")
    assert(math.abs(mockFill.position.y - 0.25) < 0.0001, "FillLevels remain vertically invariant")
    assert(math.abs(mockSpeedMeter.position.y - (0.25 + 0.03 + 0.002)) < 0.0001, "SpeedMeter remains vertically invariant")

    _G.g_currentMission = nil
    _G.VRBridge = nil
end)

test("ConvergenceShiftsEveryGroupAndTheContextActionByTheSameAmount", function()
    useSafeArea(0.20, 0.16)
    local consoleSet = function(...) VRCompanion:consoleCommandSetHudGroup(...) end
    consoleSet("map", "0.05", "0.10", "1.0")
    consoleSet("speedometer", "0.07", "0.12", "0.9")
    consoleSet("info", "0.09", "0.14", "0.7")
    consoleSet("controls", "0.11", "0.16", "0.6")
    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local function mk()
        return { width = 0.10, height = 0.05, position = nil, setPosition = function(self, x, y) self.position = { x = x, y = y } end }
    end
    local hud = { ingameMap = mk(), speedMeter = mk(), gameInfo = mk(), inputHelp = mk(), contextAction = mk() }
    _G.g_currentMission = { hud = hud }
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL
    local names = { "ingameMap", "speedMeter", "gameInfo", "inputHelp", "contextAction" }

    curParity = 0
    VRCompanion:updateHudConvergence()
    local left = {}
    for _, n in ipairs(names) do left[n] = { x = hud[n].position.x, y = hud[n].position.y } end
    curParity = 1
    VRCompanion:updateHudConvergence()
    for _, n in ipairs(names) do
        assert(math.abs((left[n].x - hud[n].position.x) - 0.024) < 0.0001, n .. " must shift by the full eye-to-eye disparity")
        assert(math.abs(left[n].y - hud[n].position.y) < 0.0001, n .. " must not move vertically")
    end
    -- Margins still hold at the left eye's offset (+0.012)
    assert(math.abs(left.ingameMap.x - (0.05 + 0.012)) < 0.0001, "map left margin")
    assert(math.abs(left.speedMeter.x - (1.0 - 0.07 - 0.10 + 0.012)) < 0.0001, "speedometer right margin")
    assert(math.abs(left.gameInfo.y - (1.0 - 0.14 - 0.05)) < 0.0001, "info top margin")
    assert(math.abs(left.contextAction.x - (0.5 + 0.012)) < 0.0001 and math.abs(left.contextAction.y - 0.12) < 0.0001, "context action on the speedometer bottom margin")

    VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
    _G.g_currentMission = nil
    _G.VRBridge = nil
    useSafeArea(0.30, 0.22)
end)

test("ConsoleCommandsKeepTheConvergenceOffsetOnEveryGroupFillLevelsAndTheContextAction", function()
    useSafeArea(0.20, 0.16)
    local consoleSet = function(...) VRCompanion:consoleCommandSetHudGroup(...) end
    consoleSet("map", "0.05", "0.10", "1.0")
    consoleSet("speedometer", "0.07", "0.12", "1.0")
    consoleSet("info", "0.09", "0.14", "1.0")
    consoleSet("controls", "0.11", "0.16", "1.0")
    VRCompanion.activeFillLevelsDisplay = nil
    _G.VRBridge = {
        getFrameParity = function() return 0 end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    local function mk()
        return { width = 0.10, height = 0.05, position = nil, setPosition = function(self, x, y) self.position = { x = x, y = y } end }
    end
    local hud = { ingameMap = mk(), speedMeter = mk(), fillLevels = mk(), gameInfo = mk(), inputHelp = mk(), contextAction = mk() }
    _G.g_currentMission = { hud = hud }
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL

    -- The left eye's frame sets the +0.012 offset; the console command that follows places everything with it
    VRCompanion:updateHudConvergence()
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.012) < 0.0001, "left eye offset")
    for _, n in ipairs({ "ingameMap", "speedMeter", "fillLevels", "gameInfo", "inputHelp", "contextAction" }) do hud[n].position = nil end
    consoleSet("speedometer", "0.07", "0.12", "1.0")

    assert(math.abs(hud.speedMeter.position.x - (1.0 - 0.07 - 0.10 + 0.012)) < 0.0001, "speedometer keeps the offset after the console command")
    assert(math.abs(hud.fillLevels.position.x - (1.0 - 0.07 - 0.10 + 0.012)) < 0.0001, "fill levels keep the offset after the console command")
    assert(math.abs(hud.ingameMap.position.x - (0.05 + 0.012)) < 0.0001, "map keeps the offset")
    assert(math.abs(hud.gameInfo.position.x - (1.0 - 0.09 - 0.10 + 0.012)) < 0.0001, "info keeps the offset")
    assert(math.abs(hud.inputHelp.position.x - (0.11 + 0.012)) < 0.0001, "controls keep the offset")
    assert(math.abs(hud.contextAction.position.x - (0.5 + 0.012)) < 0.0001, "context action keeps the offset")

    VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
    VRCompanion.activeFillLevelsDisplay = nil
    _G.g_currentMission = nil
    _G.VRBridge = nil
    useSafeArea(0.30, 0.22)
end)

-- Hidden HUD: no repositioning, but convergence state keeps updating
local function makeCountingHud()
    local calls = { count = 0 }
    local function mk(name)
        return {
            name = name, width = 0.08, height = 0.03, position = nil,
            setPosition = function(self, x, y) calls.count = calls.count + 1; self.position = { x = x, y = y } end,
            setScale = function() calls.count = calls.count + 1 end,
            storeOriginalPosition = function() calls.count = calls.count + 1 end,
            reposition = function() calls.count = calls.count + 1 end,
            updatePosition = function() calls.count = calls.count + 1 end
        }
    end
    local hud = {
        speedMeter = mk("SpeedMeterDisplay"),
        fillLevels = mk("FillLevelsDisplay"),
        inputHelp = mk("InputHelpDisplay"),
        gameInfo = mk("GameInfoDisplay"),
        contextAction = mk("ContextActionDisplay"),
        ingameMap = mk("IngameMap")
    }
    hud.fillLevels.bgLeft = mk("bgLeft")
    hud.fillLevels.bar = mk("bar")
    return hud, calls
end

local function setupHiddenHudScene(parity)
    useSafeArea(0.20, 0.16)
    VRCompanion.is2DViewActive = false
    VRCompanion.activeFillLevelsDisplay = nil
    _G.VRBridge = {
        getFrameParity = function() return parity.value end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true
    local hud, calls = makeCountingHud()
    _G.g_currentMission = { hud = hud }
    return hud, calls
end

local function teardownHiddenHudScene()
    VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
    VRCompanion.activeFillLevelsDisplay = nil
    _G.g_currentMission = nil
    _G.VRBridge = nil
end

test("HiddenHudMakesNoRepositionCallsButStillUpdatesConvergenceState", function()
    local parity = { value = 0 }
    local hud, calls = setupHiddenHudScene(parity)
    VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN

    for f = 1, 4 do
        parity.value = f % 2
        local left, right, offset = VRCompanion:updateHudConvergence(16)
        assert(left ~= nil and right ~= nil and offset ~= nil, "Hidden update must still return anchors and offset")
    end
    assert(calls.count == 0, string.format("Hidden update made %d HUD reposition calls", calls.count))
    assert(hud.speedMeter.position == nil and hud.fillLevels.position == nil, "No element may be moved while Hidden")

    parity.value = 0
    VRCompanion:updateHudConvergence(16)
    assert(math.abs(_G.g_hudAnchorLeft - VRCompanion.hudAnchorLeft) < 0.0000001 and VRCompanion.hudAnchorLeft ~= 0.20, "Anchors must keep converging while Hidden")
    assert(VRCompanion.currentConvergenceOffset ~= 0.0, "Convergence offset must keep updating while Hidden")
    teardownHiddenHudScene()
end)

test("SwitchingFromHiddenPositionsElementsWithCurrentConvergenceOffset", function()
    local parity = { value = 0 }
    local hud, calls = setupHiddenHudScene(parity)
    VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
    VRCompanion:updateHudConvergence()
    assert(calls.count == 0)
    local offset = VRCompanion.currentConvergenceOffset
    assert(math.abs(offset - 0.012) < 0.0001)

    VRCompanion:setHudState(VRCompanion.HUD_STATE_FULL)
    assert(hud.inputHelp.position ~= nil and hud.speedMeter.position ~= nil and hud.ingameMap.position ~= nil
        and hud.gameInfo.position ~= nil and hud.contextAction.position ~= nil and hud.fillLevels.position ~= nil,
        "Every element must be positioned immediately on leaving Hidden")
    assert(math.abs(hud.inputHelp.position.x - 0.312) < 0.0001, "InputHelp must use its group's shifted left margin")
    assert(math.abs(hud.gameInfo.position.x - (0.712 - 0.08)) < 0.0001, "GameInfo must use its group's shifted right margin edge")
    assert(math.abs(hud.contextAction.position.x - 0.512) < 0.0001, "ContextAction must carry the offset")
    assert(math.abs(hud.ingameMap.position.x - 0.312) < 0.0001, "Map must use its group's shifted left margin")
    assert(math.abs(hud.fillLevels.position.x - (0.712 - 0.08)) < 0.0001, "Fill levels must carry the offset")

    -- Streamlined path too
    VRCompanion:setHudState(VRCompanion.HUD_STATE_HIDDEN)
    parity.value = 1
    VRCompanion:updateHudConvergence()
    hud.speedMeter.position = nil
    VRCompanion:setHudState(VRCompanion.HUD_STATE_STREAMLINED)
    assert(hud.speedMeter.position ~= nil and math.abs(hud.speedMeter.position.x - (0.688 - 0.08)) < 0.0001, "Streamlined must position with the current (-) offset")
    teardownHiddenHudScene()
end)

test("VisibleStatesKeepRepositioningEveryUpdate", function()
    local parity = { value = 0 }
    local hud, calls = setupHiddenHudScene(parity)
    VRCompanion.hudState = VRCompanion.HUD_STATE_STREAMLINED
    VRCompanion:updateHudConvergence()
    assert(calls.count > 0, "Streamlined update must reposition")
    local after = calls.count
    parity.value = 1
    VRCompanion.hudState = VRCompanion.HUD_STATE_FULL
    VRCompanion:updateHudConvergence()
    assert(calls.count > after, "Full update must reposition")
    assert(math.abs(hud.speedMeter.position.x - (0.688 - 0.08)) < 0.0001)
    teardownHiddenHudScene()
end)

-- Slice 4: Pass-Through mode and disabled head tracking smooth reversion to zero
test("PassThroughModeRevertsConvergenceOffsetSmoothlyToZero", function()
    useSafeArea(0.20, 0.16)
    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    -- Initialize active disparity
    VRCompanion:updateHudConvergence()
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001)

    -- Activate Pass-Through mode
    VRCompanion.passThroughMode = true
    assert(VRCompanion:isPassThroughMode() == true, "Pass-Through mode must be active")

    -- Step 1 frame with 16ms: disparity should decrease smoothly, not instantly drop to 0
    curParity = 1
    VRCompanion:updateHudConvergence(16)
    local d1 = VRCompanion.currentConvergenceDisparity
    assert(d1 < 0.012, "Disparity must decrease from initial 0.012")
    assert(d1 > 0.0, "Disparity must smoothly transition rather than instantly vanishing on frame 1")

    -- Step 25 frames (~400ms): disparity must smoothly reach exactly 0.0
    for f = 1, 25 do
        curParity = f % 2
        VRCompanion:updateHudConvergence(16)
    end
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.0) < 0.00001, "Disparity must converge to 0.0 in Pass-Through mode")
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0) < 0.00001, "Offset must be 0.0 in Pass-Through mode")
    assert(math.abs(VRCompanion.hudAnchorLeft - 0.20) < 0.00001, "Left anchor must revert to base 0.20")
    assert(math.abs(VRCompanion.hudAnchorRight - 0.80) < 0.00001, "Right anchor must revert to base 0.80")
    assert(math.abs(_G.g_hudAnchorLeft - 0.20) < 0.00001, "_G.g_hudAnchorLeft must revert to base 0.20")

    _G.VRBridge = nil
    VRCompanion.passThroughMode = nil
end)

test("DisabledHeadTrackingRevertsConvergenceOffsetSmoothlyToZero", function()
    useSafeArea(0.20, 0.16)
    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    -- Initialize active disparity
    VRCompanion:updateHudConvergence()
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001)

    -- Disable head tracking
    VRCompanion.headTrackingEnabled = false
    assert(VRCompanion:isHeadTrackingActive() == false, "Head tracking must report inactive")

    -- Step 1 frame: smooth decrement
    VRCompanion:updateHudConvergence(16)
    local d1 = VRCompanion.currentConvergenceDisparity
    assert(d1 < 0.012 and d1 > 0.0, "Disparity must decay smoothly on frame 1")

    -- Step remaining frames to reach 0.0
    for f = 1, 25 do
        curParity = f % 2
        VRCompanion:updateHudConvergence(16)
    end
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.0) < 0.00001, "Disparity must smoothly reach 0.0")
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0) < 0.00001, "Offset must be 0.0")

    -- Both Left (0) and Right (1) parity frames now produce zero offset
    curParity = 0
    local left0, right0, off0 = VRCompanion:updateHudConvergence(16)
    assert(math.abs(off0) < 0.00001 and math.abs(left0 - 0.20) < 0.00001)

    curParity = 1
    local left1, right1, off1 = VRCompanion:updateHudConvergence(16)
    assert(math.abs(off1) < 0.00001 and math.abs(left1 - 0.20) < 0.00001)

    -- Re-enable tracking: smoothly recovers
    VRCompanion.headTrackingEnabled = true
    for f = 1, 35 do
        curParity = f % 2
        VRCompanion:updateHudConvergence(16)
    end
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001, "Disparity smoothly recovers to 0.012")

    _G.VRBridge = nil
    VRCompanion.headTrackingEnabled = true
end)

test("PassThroughModeDetectionAndGameSettingsIntegration", function()
    -- When VRBridge is nil -> pass through is true
    _G.VRBridge = nil
    VRCompanion.passThroughMode = nil
    assert(VRCompanion:isPassThroughMode() == true, "Pass-Through mode true when VRBridge is nil")

    -- When VRBridge.isReady returns false -> pass through is true
    _G.VRBridge = { isReady = function() return false end }
    assert(VRCompanion:isPassThroughMode() == true, "Pass-Through mode true when VRBridge not ready")

    -- When VRBridge.isReady returns true and handshake is complete -> pass through is false
    _G.VRBridge = { isReady = function() return true end, isCompanionActive = true }
    VRCompanion.handshakeComplete = true
    assert(VRCompanion:isPassThroughMode() == false, "Pass-Through mode false when VRBridge ready")

    -- Test GameSettings head tracking toggle
    local trackingSettingValue = true
    _G.GameSettings = { SETTING = { IS_HEAD_TRACKING_ENABLED = 42 } }
    _G.g_gameSettings = {
        getValue = function(self, setting)
            if setting == 42 then return trackingSettingValue end
            return nil
        end
    }
    VRCompanion.headTrackingEnabled = nil
    assert(VRCompanion:isHeadTrackingActive() == true, "Head tracking active when GameSettings true")

    trackingSettingValue = false
    assert(VRCompanion:isHeadTrackingActive() == false, "Head tracking inactive when GameSettings false")

    _G.GameSettings = nil
    _G.g_gameSettings = nil
    _G.VRBridge = nil
end)

-- Slice 5: Map lifecycle and GUI menu integration
test("MapLifecycleLoadsCustomDepthAndRestoresZeroOffsetOnUnload", function()
    -- Mock XML with custom depth
    _G.mockXmlData = {
        ["vrSettings.hudSafeAreaHorizontal"] = 0.20,
        ["vrSettings.hudSafeAreaVertical"] = 0.16,
        ["vrSettings.hudConvergenceDepth"] = 2.0
    }
    _G.loadXMLFile = function(tag, path) return 123 end
    _G.getXMLFloat = function(xmlFile, key) return _G.mockXmlData[key] end
    _G.delete = function(xmlFile) end

    local safeArea = VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion.hudConvergenceDepth - 2.0) < 0.0001, "Loaded convergence depth must be 2.0")
    assert(math.abs(safeArea.convergenceDepth - 2.0) < 0.0001, "getHudSafeArea().convergenceDepth must be 2.0")

    -- Verify disparity scaled with 2.0m depth: (0.064 / 2.0) * 0.3375 = 0.0108
    local disparity = VRCompanion:computeConvergenceDisparity(0.064)
    assert(math.abs(disparity - 0.0108) < 0.0001, "Disparity for 2.0m depth must be 0.0108")

    -- Clean up XML mocks
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.delete = nil
    _G.mockXmlData = nil

    -- Now test deleteMap cleanup
    _G.VRBridge = {
        getFrameParity = function() return 0 end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end,
        onMapUnload = function() end,
        setMenuVisible = function() end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    VRCompanion:updateHudConvergence()
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0108) < 0.0001)

    VRCompanion:deleteMap()
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0) < 0.00001, "deleteMap must reset currentConvergenceOffset to 0")
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.0) < 0.00001, "deleteMap must reset currentConvergenceDisparity to 0")
    assert(math.abs(_G.g_hudAnchorLeft - 0.0) < 0.00001, "_G.g_hudAnchorLeft restored to 0.0")
    assert(math.abs(_G.g_hudAnchorRight - 1.0) < 0.00001, "_G.g_hudAnchorRight restored to 1.0")

    _G.VRBridge = nil
end)

test("MenuTransitionsPreserveConvergenceParityState", function()
    useSafeArea(0.20, 0.16)
    local curParity = 0
    _G.VRBridge = {
        getFrameParity = function() return curParity end,
        getIpd = function() return 0.064 end,
        isReady = function() return true end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    -- In game, VR active
    VRCompanion.isMenuVisible = false
    VRCompanion:updateHudConvergence()
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001)

    -- Player enters GUI menu (floating quad active, HUD convergence disabled)
    VRCompanion.isMenuVisible = true
    for f = 1, 35 do
        curParity = f % 2
        VRCompanion:updateHudConvergence(16)
    end
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.0) < 0.0001, "In menu, disparity reverts to 0")
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.0) < 0.0001, "In menu, offset is 0")

    -- Player exits GUI menu (stereoscopic 3D projection rendering resumes)
    VRCompanion.isMenuVisible = false
    for f = 1, 35 do
        curParity = f % 2
        VRCompanion:updateHudConvergence(16)
    end
    assert(math.abs(VRCompanion.currentConvergenceDisparity - 0.012) < 0.0001, "Exiting menu, disparity recovers to 0.012")

    -- Verify parity alternation resumes
    curParity = 0
    local _, _, off0 = VRCompanion:updateHudConvergence()
    assert(math.abs(off0 - 0.012) < 0.0001, "Parity 0 offset is +0.012")

    curParity = 1
    local _, _, off1 = VRCompanion:updateHudConvergence()
    assert(math.abs(off1 - (-0.012)) < 0.0001, "Parity 1 offset is -0.012")

    _G.VRBridge = nil
end)

-- The pass-through and head-tracking checks resolve once per update
local function withProbedUpdate(probe)
    local real = VRCompanion.updateHudConvergence
    VRCompanion.updateHudConvergence = function(self, dt)
        probe()
        return real(self, dt)
    end
    local ok, err = pcall(function() VRCompanion:update(16) end)
    VRCompanion.updateHudConvergence = real
    assert(ok, tostring(err))
end

-- Peak heap growth (KB) while running fn many times; per-call closures show up as growth
local function peakHeapGrowthKb(fn, iterations)
    fn()
    local base = gcinfo()
    local peak = 0
    for i = 1, iterations do
        fn()
        local grown = gcinfo() - base
        if grown > peak then peak = grown end
    end
    return peak
end
local function installCountingMocks()
    local counts = { ready = 0, setting = 0, available = 0 }
    local state = { ready = true, setting = true, available = true }
    _G.VRBridge = { isReady = function() counts.ready = counts.ready + 1; return state.ready end, isCompanionActive = true }
    _G.GameSettings = { SETTING = { IS_HEAD_TRACKING_ENABLED = 42 } }
    _G.g_gameSettings = {
        getValue = function(_, setting)
            counts.setting = counts.setting + 1
            return state.setting
        end
    }
    _G.isHeadTrackingAvailable = function() counts.available = counts.available + 1; return state.available end
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = nil
    VRCompanion.headTrackingEnabled = true
    return counts, state
end

local function clearCountingMocks()
    _G.VRBridge = nil
    _G.GameSettings = nil
    _G.g_gameSettings = nil
    _G.isHeadTrackingAvailable = nil
    VRCompanion.passThroughMode = nil
    VRCompanion.headTrackingEnabled = true
end

test("RepeatedChecksWithinOneUpdateQueryEachSourceOnce", function()
    local counts = installCountingMocks()
    local results = {}
    withProbedUpdate(function()
        counts.ready, counts.setting, counts.available = 0, 0, 0
        for i = 1, 10 do
            results[#results + 1] = VRCompanion:isPassThroughMode()
            results[#results + 1] = VRCompanion:isHeadTrackingActive()
        end
    end)
    assert(counts.ready <= 1, "VRBridge.isReady queried " .. counts.ready .. " times in one update")
    assert(counts.setting <= 1, "game setting read " .. counts.setting .. " times in one update")
    assert(counts.available <= 1, "isHeadTrackingAvailable called " .. counts.available .. " times in one update")
    for i = 1, #results, 2 do
        assert(results[i] == false and results[i + 1] == true, "cached answers must match the resolved answers")
    end
    clearCountingMocks()
end)

local function bothChecks()
    VRCompanion:isPassThroughMode()
    VRCompanion:isHeadTrackingActive()
end

test("CachedChecksDoNotAllocateWithinAnUpdate", function()
    installCountingMocks()
    local peak
    withProbedUpdate(function() peak = peakHeapGrowthKb(bothChecks, 20000) end)
    assert(peak < 8, "cached checks grew the heap by " .. peak .. " KB")
    clearCountingMocks()
end)

test("UncachedChecksDoNotCreateAClosurePerCall", function()
    installCountingMocks()
    local peak = peakHeapGrowthKb(bothChecks, 20000)
    assert(peak < 8, "checks grew the heap by " .. peak .. " KB")
    clearCountingMocks()
end)
test("ReadinessChangeIsReflectedByTheNextUpdate", function()
    local counts, state = installCountingMocks()
    local seen
    withProbedUpdate(function() seen = VRCompanion:isPassThroughMode() end)
    assert(seen == false)
    state.ready = false
    withProbedUpdate(function() seen = VRCompanion:isPassThroughMode() end)
    assert(seen == true, "runtime becoming unready must show up on the next update")
    _G.VRBridge = nil
    withProbedUpdate(function() seen = VRCompanion:isPassThroughMode() end)
    assert(seen == true)
    clearCountingMocks()
end)

test("HeadTrackingSettingAndAvailabilityChangesAreReflectedByTheNextUpdate", function()
    local counts, state = installCountingMocks()
    local seen
    withProbedUpdate(function() seen = VRCompanion:isHeadTrackingActive() end)
    assert(seen == true)
    state.setting = false
    withProbedUpdate(function() seen = VRCompanion:isHeadTrackingActive() end)
    assert(seen == false, "head-tracking setting toggle must show up on the next update")
    state.setting = true
    state.available = false
    withProbedUpdate(function() seen = VRCompanion:isHeadTrackingActive() end)
    assert(seen == false, "head tracking becoming unavailable must show up on the next update")
    clearCountingMocks()
end)

test("SettersTakeEffectNoLaterThanTheNextUpdate", function()
    installCountingMocks()
    local seen
    withProbedUpdate(function() seen = VRCompanion:isPassThroughMode() end)
    assert(seen == false)
    VRCompanion:setPassThroughMode(true)
    withProbedUpdate(function() seen = VRCompanion:isPassThroughMode() end)
    assert(seen == true, "setPassThroughMode(true) must apply by the next update")
    VRCompanion:setPassThroughMode(false)
    withProbedUpdate(function() seen = VRCompanion:isPassThroughMode() end)
    assert(seen == false)

    withProbedUpdate(function() seen = VRCompanion:isHeadTrackingActive() end)
    assert(seen == true)
    VRCompanion:setHeadTrackingEnabled(false)
    withProbedUpdate(function() seen = VRCompanion:isHeadTrackingActive() end)
    assert(seen == false, "setHeadTrackingEnabled(false) must apply by the next update")
    VRCompanion:setHeadTrackingEnabled(true)
    withProbedUpdate(function() seen = VRCompanion:isHeadTrackingActive() end)
    assert(seen == true)
    clearCountingMocks()
end)

-- The game runs VRCompanion:update inside the hooked mission update
local function withHookedMissionUpdate(missionUpdate, fn, mode)
    _G.FSBaseMission = { update = missionUpdate }
    VRCompanion:setRenderingMode(mode or "AFR")
    VRCompanion:setSubPassIndex(0)
    VRCompanion:hookMissionUpdate()
    local ok, err = pcall(fn)
    VRCompanion:unhookMissionUpdate()
    VRCompanion:setRenderingMode("AFR")
    _G.FSBaseMission = nil
    assert(ok, tostring(err))
end

test("UpdateNestedInTheHookedMissionUpdateQueriesEachSourceOncePerFrame", function()
    local counts = installCountingMocks()
    local real = VRCompanion.updateHudConvergence
    VRCompanion.updateHudConvergence = function(self, dt)
        for _ = 1, 10 do bothChecks() end
        return real(self, dt)
    end
    local ok, err = true, nil
    for _, mode in ipairs({ "AFR", "DualPass" }) do
        if ok then
            ok, err = pcall(withHookedMissionUpdate, function(self, dt) VRCompanion:update(dt) end, function()
                for _ = 1, 3 do
                    counts.ready, counts.setting, counts.available = 0, 0, 0
                    -- Sub-pass 0 runs the mission update (and with it VRCompanion:update)
                    VRCompanion:setSubPassIndex(0)
                    _G.FSBaseMission:update(16)
                    local where = mode .. ": "
                    assert(counts.ready == 1, where .. "VRBridge.isReady queried " .. counts.ready .. " times in one frame")
                    assert(counts.setting == 1, where .. "game setting read " .. counts.setting .. " times in one frame")
                    assert(counts.available == 1, where .. "isHeadTrackingAvailable called " .. counts.available .. " times in one frame")
                end
            end, mode)
        end
    end
    VRCompanion.updateHudConvergence = real
    clearCountingMocks()
    assert(ok, tostring(err))
end)

test("AnErrorDuringAnUpdateClosesTheFrameCheckScope", function()
    local counts, state = installCountingMocks()
    local real = VRCompanion.updateHudConvergence
    VRCompanion.updateHudConvergence = function(self, dt)
        bothChecks()
        error("hud update failed")
    end
    local updateOk = pcall(function() VRCompanion:update(16) end)
    VRCompanion.updateHudConvergence = real

    local missionOk
    local ok, err = pcall(withHookedMissionUpdate, function(self, dt)
        bothChecks()
        error("mission update failed")
    end, function()
        missionOk = pcall(function() _G.FSBaseMission:update(16) end)
    end)

    local depth = VRCompanion.frameCheckDepth or 0
    state.ready = false
    local seen = VRCompanion:isPassThroughMode()
    clearCountingMocks()
    assert(ok, tostring(err))
    assert(updateOk == false and missionOk == false, "the errors must still reach the caller")
    assert(depth == 0, "an error must not leave a frame-check scope open, depth=" .. tostring(depth))
    assert(seen == true, "checks outside an update must resolve fresh after an error")
end)

test("AnErrorInANestedUpdateKeepsItsRaiseSiteAndClosesEveryScope", function()
    installCountingMocks()
    local real = VRCompanion.updateHudConvergence
    local function failingHudUpdate()
        error("nested hud update failed")
    end
    VRCompanion.updateHudConvergence = failingHudUpdate

    local missionOk, missionErr
    local ok, err = pcall(withHookedMissionUpdate, function(self, dt) VRCompanion:update(dt) end, function()
        missionOk, missionErr = pcall(function() _G.FSBaseMission:update(16) end)
    end)
    VRCompanion.updateHudConvergence = real
    local depth = VRCompanion.frameCheckDepth or 0
    clearCountingMocks()

    assert(ok, tostring(err))
    missionErr = tostring(missionErr)
    assert(missionOk == false and string.find(missionErr, "nested hud update failed", 1, true) ~= nil,
        "the error must still reach the game: " .. missionErr)
    if debug ~= nil and debug.traceback ~= nil then
        assert(string.find(missionErr, "failingHudUpdate", 1, true) ~= nil,
            "the rethrown error must carry the traceback from where it was raised: " .. missionErr)
    end
    assert(depth == 0, "an error in a nested scope must not leave a scope open, depth=" .. tostring(depth))
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d tests failed", failed))
end
