-- ====================================================================
-- Test Suite for VR HUD Convergence Parity (Issue #27)
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
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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

    -- Frame 0 (Parity 0 -> right aligned anchor shifts by +0.012: 0.80 + 0.012 = 0.812)
    curParity = 0
    VRCompanion:updateHudConvergence()
    assert(mockSpeedMeter.position ~= nil)
    assert(math.abs(mockSpeedMeter.position.x - 0.812) < 0.0001, "SpeedMeter aligned with parity 0 shifted right anchor")

    -- Frame 1 (Parity 1 -> right aligned anchor shifts by -0.012: 0.80 - 0.012 = 0.788)
    curParity = 1
    VRCompanion:updateHudConvergence()
    assert(math.abs(mockSpeedMeter.position.x - 0.788) < 0.0001, "SpeedMeter aligned with parity 1 shifted right anchor")

    _G.g_currentMission = nil
    _G.VRBridge = nil
end)

test("ConvergenceDisparitySynchronizesSpeedMeterAndFillLevelsUnderVehicleAcrossParity", function()
    VRCompanion:configureHudInsets(0.35, 0.25, 0.50)
    VRCompanion.fillLevelUnderVehicle = true

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

    -- Frame 0: Left Eye (Parity 0 -> right anchor = 0.65 + 0.012 = 0.662)
    curParity = 0
    VRCompanion:updateHudConvergence()
    assert(mockSpeedMeter.position ~= nil)
    assert(mockFill.position ~= nil)
    assert(math.abs(mockSpeedMeter.position.x - 0.662) < 0.0001, "SpeedMeter must receive +0.012 disparity")
    assert(math.abs(mockFill.position.x - 0.582) < 0.0001, "FillLevels must receive matching +0.012 disparity (0.662 - 0.08 = 0.582)")
    assert(math.abs(mockFill.position.y - 0.218) < 0.0001, "FillLevels must be under vehicle element at 0.218")

    -- Frame 1: Right Eye (Parity 1 -> right anchor = 0.65 - 0.012 = 0.638)
    curParity = 1
    VRCompanion:updateHudConvergence()
    assert(math.abs(mockSpeedMeter.position.x - 0.638) < 0.0001, "SpeedMeter must receive -0.012 disparity")
    assert(math.abs(mockFill.position.x - 0.558) < 0.0001, "FillLevels must receive matching -0.012 disparity (0.638 - 0.08 = 0.558)")
    assert(math.abs(mockFill.position.y - 0.218) < 0.0001, "FillLevels remains vertically invariant under vehicle")

    _G.g_currentMission = nil
    _G.VRBridge = nil
end)

-- Slice 4: Pass-Through mode and disabled head tracking smooth reversion to zero
test("PassThroughModeRevertsConvergenceOffsetSmoothlyToZero", function()
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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
        ["vrSettings.hudInsetHorizontal"] = 0.20,
        ["vrSettings.hudInsetVertical"] = 0.16,
        ["vrSettings.hudUiScale"] = 0.88,
        ["vrSettings.hudConvergenceDepth"] = 2.0
    }
    _G.loadXMLFile = function(tag, path) return 123 end
    _G.getXMLFloat = function(xmlFile, key) return _G.mockXmlData[key] end
    _G.delete = function(xmlFile) end

    local insets = VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion.hudConvergenceDepth - 2.0) < 0.0001, "Loaded convergence depth must be 2.0")
    assert(math.abs(insets.convergenceDepth - 2.0) < 0.0001, "getHudInsets().convergenceDepth must be 2.0")

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
    VRCompanion:configureHudInsets(0.20, 0.16, 0.88)
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

print(string.format("Summary: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d tests failed", failed))
end
