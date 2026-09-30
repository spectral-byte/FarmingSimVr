-- ====================================================================
-- FS25 VR Companion Mod: In-Engine Luau Coordination & Handshake
-- Interfaces with in-process native VRBridge table injected by dxgi.dll
-- ====================================================================

VRCompanion = {}
VRCompanion.name = g_currentModName or "FS25_VRCompanion"
VRCompanion.modDirectory = g_currentModDirectory or ""
VRCompanion.version = "1.0.0.0"
VRCompanion.handshakeComplete = false
VRCompanion.stereoToggled = false
VRCompanion.lastReportedVerdict = nil
VRCompanion.frameTicks = 0
VRCompanion.vehicleCameraHooked = false
VRCompanion.playerCameraHooked = false
VRCompanion.enterableHooked = false
VRCompanion.hudHooked = false
VRCompanion.origHudHooks = nil
VRCompanion.lastControlledVehicle = nil
VRCompanion.origCamSettings = {}
VRCompanion.vehicleRotationStates = {}
VRCompanion.isMenuVisible = false
VRCompanion.guiVisibilityInitialized = false

-- VR HUD Inset and UI Scale Constants
VRCompanion.DEFAULT_HUD_INSET_X = 0.20
VRCompanion.DEFAULT_HUD_INSET_Y = 0.16
VRCompanion.DEFAULT_HUD_INSET_TOP = nil -- when nil, defaults to hudInsetY
VRCompanion.DEFAULT_HUD_UI_SCALE = 0.88

VRCompanion.MIN_HUD_INSET_X = 0.0
VRCompanion.MAX_HUD_INSET_X = 0.40
VRCompanion.MIN_HUD_INSET_Y = 0.0
VRCompanion.MAX_HUD_INSET_Y = 0.35
VRCompanion.MIN_HUD_INSET_TOP = 0.0
VRCompanion.MAX_HUD_INSET_TOP = 0.50
VRCompanion.MIN_HUD_UI_SCALE = 0.50
VRCompanion.MAX_HUD_UI_SCALE = 1.50

-- VR HUD Three-Stage State Machine Constants (Issue #25)
VRCompanion.HUD_STATE_HIDDEN = 0
VRCompanion.HUD_STATE_STREAMLINED = 1
VRCompanion.HUD_STATE_FULL = 2

VRCompanion.HUD_STATES = {
    HIDDEN = 0,
    STREAMLINED = 1,
    FULL = 2
}

VRCompanion.HUD_STATE_NAMES = {
    [0] = "Hidden",
    [1] = "Streamlined",
    [2] = "Full"
}

-- VR HUD Convergence Parity Constants (Issue #27)
VRCompanion.DEFAULT_CONVERGENCE_DEPTH = 1.8
VRCompanion.DEFAULT_CONVERGENCE_DISPARITY = 0.012
VRCompanion.DEFAULT_IPD = 0.064
VRCompanion.MIN_IPD = 0.040
VRCompanion.MAX_IPD = 0.085
VRCompanion.MIN_CONVERGENCE_DEPTH = 0.5
VRCompanion.MAX_CONVERGENCE_DEPTH = 5.0
VRCompanion.MIN_CONVERGENCE_DISPARITY = 0.0
VRCompanion.MAX_CONVERGENCE_DISPARITY = 0.040
-- Parallax scaling constant mapping physical IPD/depth to normalized screen displacement:
-- (0.012 disparity * 1.8m depth) / 0.064m nominal IPD = 0.3375
VRCompanion.CONVERGENCE_PARALLAX_FACTOR = 0.3375
VRCompanion.CONVERGENCE_SMOOTH_SPEED = 12.0

-- VR Fill Level Element Positioning
VRCompanion.DEFAULT_FILL_LEVEL_UNDER_VEHICLE = true
VRCompanion.fillLevelUnderVehicle = true
VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP = 0.004
VRCompanion.hudFillLevelGap = VRCompanion.DEFAULT_HUD_FILL_LEVEL_GAP
VRCompanion.activeFillLevelsDisplay = nil

-- VR HUD Notifications
VRCompanion.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION = false
VRCompanion.showHudLoadNotification = false

-- Recenter VR Tracking Space Configuration (Issue #33)
VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE = true
VRCompanion.recenterVrTrackingSpace = true

VRCompanion.RECENTER_MODES = {
    BOTH = "Both",
    VEHICLE_ONLY = "VehicleOnly"
}

function VRCompanion:getRecenterMode()
    if self.recenterVrTrackingSpace == false then
        return "VehicleOnly"
    end
    return "Both"
end

function VRCompanion:setRecenterMode(modeOrBool)
    if modeOrBool == nil or modeOrBool == "" or (type(modeOrBool) == "string" and string.lower(modeOrBool) == "status") then
        return self:getRecenterMode()
    end

    local newTracking = nil
    if type(modeOrBool) == "boolean" then
        newTracking = modeOrBool
    elseif type(modeOrBool) == "string" then
        local lower = string.lower(modeOrBool)
        if lower == "toggle" then
            newTracking = not (self.recenterVrTrackingSpace ~= false)
        elseif lower == "vehicle" or lower == "vehicleonly" or lower == "false" or lower == "0" then
            newTracking = false
        elseif lower == "both" or lower == "tracking" or lower == "true" or lower == "1" then
            newTracking = true
        else
            return nil, string.format("Usage: vrRecenterMode <Both | VehicleOnly | toggle> (current: %s)", self:getRecenterMode())
        end
    end

    if newTracking ~= nil then
        self.recenterVrTrackingSpace = newTracking
        self:saveConfiguration()
    end
    return self:getRecenterMode()
end

-- Configured VR HUD state
VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN
VRCompanion.hudInsetX = VRCompanion.DEFAULT_HUD_INSET_X
VRCompanion.hudInsetY = VRCompanion.DEFAULT_HUD_INSET_Y
VRCompanion.hudInsetTop = nil
VRCompanion.hudUiScaleMultiplier = VRCompanion.DEFAULT_HUD_UI_SCALE
VRCompanion.hudConvergenceDepth = VRCompanion.DEFAULT_CONVERGENCE_DEPTH
VRCompanion.hudAnchorLeftBase = 0.20
VRCompanion.hudAnchorRightBase = 0.80
VRCompanion.hudAnchorBottomBase = 0.16
VRCompanion.hudAnchorTopBase = 0.84
VRCompanion.hudAnchorLeft = 0.20
VRCompanion.hudAnchorRight = 0.80
VRCompanion.hudAnchorBottom = 0.16
VRCompanion.hudAnchorTop = 0.84
VRCompanion.effectiveUiScale = 0.88
VRCompanion.currentConvergenceDisparity = nil
VRCompanion.currentConvergenceOffset = 0.0
VRCompanion.passThroughMode = nil
VRCompanion.headTrackingEnabled = true
VRCompanion.origHudAnchors = nil
VRCompanion.origUiScale = nil

function VRCompanion:computeConvergenceDisparity(ipd, depth)
    local targetDepth = depth or self.hudConvergenceDepth or self.DEFAULT_CONVERGENCE_DEPTH
    if targetDepth == nil or targetDepth <= 0.001 then
        targetDepth = self.DEFAULT_CONVERGENCE_DEPTH
    end
    targetDepth = self:clamp(targetDepth, self.MIN_CONVERGENCE_DEPTH, self.MAX_CONVERGENCE_DEPTH)

    local targetIpd = ipd
    if targetIpd == nil then
        local bridge = VRBridge or (_G and _G.VRBridge)
        if bridge ~= nil then
            if type(bridge.getIpd) == "function" then
                targetIpd = bridge.getIpd()
            elseif bridge.ipd ~= nil then
                targetIpd = bridge.ipd
            end
        end
    end

    -- If IPD is still nil or non-positive, use safe fallback to 0.012
    if targetIpd == nil or targetIpd <= 0.001 then
        return self.DEFAULT_CONVERGENCE_DISPARITY
    end

    local clampedIpd = self:clamp(targetIpd, self.MIN_IPD, self.MAX_IPD)
    local disparity = (clampedIpd / targetDepth) * self.CONVERGENCE_PARALLAX_FACTOR

    return self:clamp(disparity, self.MIN_CONVERGENCE_DISPARITY, self.MAX_CONVERGENCE_DISPARITY)
end

function VRCompanion:getConvergenceDisparity()
    if self.currentConvergenceDisparity ~= nil then
        return self.currentConvergenceDisparity
    end
    return self:computeConvergenceDisparity()
end

function VRCompanion:getConvergenceOffset(parity, disparity)
    local targetParity = parity
    if targetParity == nil then
        targetParity = self:getFrameParity()
    end

    local targetDisparity = disparity
    if targetDisparity == nil then
        targetDisparity = self:getConvergenceDisparity()
    end

    -- Parity 0 (even, Left eye): positive offset (+Delta x)
    -- Parity 1 (odd, Right eye): negative offset (-Delta x)
    local sign = (targetParity % 2 == 1) and -1.0 or 1.0
    return sign * targetDisparity
end

function VRCompanion:isPassThroughMode()
    if self.passThroughMode ~= nil then
        return self.passThroughMode == true
    end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil then
        return true
    end
    if type(bridge.isReady) == "function" then
        local ok, ready = pcall(function() return bridge.isReady() end)
        if ok and not ready then
            return true
        end
    end
    if self.handshakeComplete == false and not (bridge and bridge.isCompanionActive) then
        return true
    end
    return false
end

function VRCompanion:setPassThroughMode(enabled)
    self.passThroughMode = (enabled == true)
end

function VRCompanion:isHeadTrackingActive()
    if self.headTrackingEnabled ~= nil and not self.headTrackingEnabled then
        return false
    end
    local gs = g_gameSettings or _G.g_gameSettings
    local gsClass = GameSettings or _G.GameSettings
    if gs ~= nil and type(gs.getValue) == "function" and gsClass ~= nil and gsClass.SETTING ~= nil and gsClass.SETTING.IS_HEAD_TRACKING_ENABLED ~= nil then
        local ok, enabled = pcall(function() return gs:getValue(gsClass.SETTING.IS_HEAD_TRACKING_ENABLED) end)
        if ok and enabled == false then
            return false
        end
    end
    if type(isHeadTrackingAvailable) == "function" then
        local ok, avail = pcall(isHeadTrackingAvailable)
        if ok and avail == false then
            return false
        end
    end
    return true
end

function VRCompanion:setHeadTrackingEnabled(enabled)
    self.headTrackingEnabled = (enabled == true)
end

function VRCompanion:getFillLevelsDisplay(hud)
    if self.activeFillLevelsDisplay ~= nil then
        return self.activeFillLevelsDisplay
    end
    local targetHud = hud
    if targetHud == nil then
        local mission = g_currentMission or _G.g_currentMission
        targetHud = (mission ~= nil) and mission.hud or nil
    end
    if targetHud ~= nil then
        if targetHud.fillLevels ~= nil then
            return targetHud.fillLevels
        end
        if targetHud.fillLevelsDisplay ~= nil then
            return targetHud.fillLevelsDisplay
        end
        if targetHud.speedMeter ~= nil then
            if targetHud.speedMeter.fillLevels ~= nil then
                return targetHud.speedMeter.fillLevels
            end
            if targetHud.speedMeter.fillLevelsDisplay ~= nil then
                return targetHud.speedMeter.fillLevelsDisplay
            end
        end
        if targetHud.speedMeterDisplay ~= nil then
            if targetHud.speedMeterDisplay.fillLevels ~= nil then
                return targetHud.speedMeterDisplay.fillLevels
            end
            if targetHud.speedMeterDisplay.fillLevelsDisplay ~= nil then
                return targetHud.speedMeterDisplay.fillLevelsDisplay
            end
        end
    end
    local gFill = _G.g_fillLevelsDisplay or (FillLevelsDisplay and FillLevelsDisplay.instance)
    if gFill ~= nil then
        return gFill
    end
    return nil
end

function VRCompanion:positionFillLevelsUnderVehicle(display, offset)
    if display == nil then return end

    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local sm = nil
    if hud ~= nil then
        sm = hud.speedMeter or hud.speedMeterDisplay
    end

    local currentOffset = offset or self.currentConvergenceOffset or 0.0
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_INSET_X)
    local shiftedRight = self:clamp(baseRight + currentOffset, 0.0, 1.0)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_INSET_Y
    local uiScale = self.effectiveUiScale or 0.88

    -- Determine single fill level row height
    local flHeight = nil
    if type(display.getHeight) == "function" then
        local ok, h = pcall(function() return display:getHeight() end)
        if ok and type(h) == "number" and h > 0 then flHeight = h end
    end
    if flHeight == nil and display.height ~= nil and type(display.height) == "number" and display.height > 0 then
        flHeight = display.height
    end
    if flHeight == nil and display.bgLeft ~= nil and display.bgLeft.height ~= nil and display.bgLeft.height > 0 then
        flHeight = display.bgLeft.height
    end
    if flHeight == nil and display.bgScale ~= nil and display.bgScale.height ~= nil and display.bgScale.height > 0 then
        flHeight = display.bgScale.height
    end
    if flHeight == nil then
        flHeight = 0.045 * uiScale
    end

    -- Determine total fill level display width
    local flWidth = nil
    if type(display.getWidth) == "function" then
        local ok, w = pcall(function() return display:getWidth() end)
        if ok and type(w) == "number" and w > 0 then flWidth = w end
    end
    if flWidth == nil and display.width ~= nil and type(display.width) == "number" and display.width > 0 then
        flWidth = display.width
    end
    if flWidth == nil and display.bgScale ~= nil and display.bgLeft ~= nil and display.bgRight ~= nil then
        local totalW = (display.bgLeft.width or 0) + (display.bgScale.width or 0) + (display.bgRight.width or 0)
        if totalW > 0 then flWidth = totalW end
    end
    if flWidth == nil then
        flWidth = 0.12 * uiScale
    end

    -- Count active fill level units (e.g. multi-tank vehicles, trailers) to scale total height
    local numLevels = 1
    if display.fillLevelData ~= nil and type(display.fillLevelData) == "table" and #display.fillLevelData > 0 then
        numLevels = #display.fillLevelData
    elseif display.vehicle ~= nil and type(display.vehicle.getFillUnits) == "function" then
        local ok, units = pcall(function() return display.vehicle:getFillUnits() end)
        if ok and type(units) == "table" and #units > 0 then
            numLevels = math.max(1, #units)
        end
    end
    local rowGap = 0.004 * uiScale
    local totalFlHeight = numLevels * flHeight + math.max(0, numLevels - 1) * rowGap

    -- Lowest point of SpeedMeterDisplay is baseBottom (all inner gauges/dials sit above baseBottom)
    local smLowestY = baseBottom
    local gapMultiplier = self.hudFillLevelGap or self.DEFAULT_HUD_FILL_LEVEL_GAP
    local gap = gapMultiplier * uiScale

    local fillTop = smLowestY - gap
    local fillBottom = fillTop - totalFlHeight
    local targetSmBottom = baseBottom
    local minMarginY = 0.015

    if fillBottom < minMarginY then
        fillBottom = minMarginY
        smLowestY = fillBottom + totalFlHeight + gap
        targetSmBottom = smLowestY
    end

    -- Align fill level element so its right boundary stays within shiftedRight, completely in view
    local flPosX = shiftedRight - flWidth
    if self.hudAnchorLeft ~= nil and flPosX < self.hudAnchorLeft then
        flPosX = self.hudAnchorLeft
    end

    display.anchorX = flPosX
    display.anchorY = fillBottom
    display.anchorLeft = self.hudAnchorLeft
    display.anchorRight = self.hudAnchorRight
    display.anchorBottom = self.hudAnchorBottom
    display.anchorTop = self.hudAnchorTop
    display.origX = flPosX
    display.origY = fillBottom
    display.x = flPosX
    display.y = fillBottom
    display.offsetX = 0
    display.offsetY = 0

    if display.vrUiScale ~= uiScale then
        display.vrUiScale = uiScale
        if type(display.setScale) == "function" then
            pcall(function() display:setScale(uiScale) end)
        end
    end

    if type(display.setPosition) == "function" then
        pcall(function() display:setPosition(flPosX, fillBottom) end)
    end
    if type(display.storeOriginalPosition) == "function" then
        pcall(function() display:storeOriginalPosition() end)
    end
    if type(display.reposition) == "function" then
        pcall(function() display:reposition() end)
    end
    if type(display.updatePosition) == "function" then
        pcall(function() display:updatePosition() end)
    end

    -- Explicitly position background parts and progress bar so they are placed at (flPosX, fillBottom)
    if display.bgLeft ~= nil and type(display.bgLeft.setPosition) == "function" then
        pcall(function() display.bgLeft:setPosition(flPosX, fillBottom) end)
    end
    local bgScaleX = flPosX + (display.bgLeft and display.bgLeft.width or 0)
    if display.bgScale ~= nil and type(display.bgScale.setPosition) == "function" then
        pcall(function() display.bgScale:setPosition(bgScaleX, fillBottom) end)
    end
    local bgRightX = bgScaleX + (display.bgScale and display.bgScale.width or 0)
    if display.bgRight ~= nil and type(display.bgRight.setPosition) == "function" then
        pcall(function() display.bgRight:setPosition(bgRightX, fillBottom) end)
    end
    if display.bar ~= nil and type(display.bar.setPosition) == "function" then
        local barX = flPosX + (display.barOffsetX or (display.bgLeft and display.bgLeft.width or 0))
        local barY = fillBottom + (display.barOffsetY or 0)
        pcall(function() display.bar:setPosition(barX, barY) end)
    end
    if display.maxWeightIcon ~= nil and type(display.maxWeightIcon.setPosition) == "function" then
        local mwX = flPosX + (display.maxWeightIconOffsetX or 0)
        local mwY = fillBottom + (display.maxWeightIconOffsetY or 0)
        pcall(function() display.maxWeightIcon:setPosition(mwX, mwY) end)
    end
    if type(display.fillTypeIcons) == "table" then
        for _, icon in pairs(display.fillTypeIcons) do
            if icon ~= nil and type(icon.setPosition) == "function" then
                local icX = flPosX + (display.fillTypeIconOffsetX or 0)
                local icY = fillBottom + (display.fillTypeIconOffsetY or 0)
                pcall(function() icon:setPosition(icX, icY) end)
            end
        end
    end

    -- If sm was lifted to accommodate fill levels at small insets, ensure sm is at targetSmBottom
    if sm ~= nil and targetSmBottom ~= baseBottom and type(sm.setPosition) == "function" then
        pcall(function() sm:setPosition(shiftedRight, targetSmBottom) end)
    end

    return flPosX, fillBottom, targetSmBottom
end

function VRCompanion:updateHudConvergence(dt)
    local isPassThrough = self:isPassThroughMode()
    local isTrackingActive = self:isHeadTrackingActive()

    local targetDisparity = 0.0
    if not isPassThrough and isTrackingActive and not self.isMenuVisible then
        local rawIpd = self:getIpd()
        targetDisparity = self:computeConvergenceDisparity(rawIpd, self.hudConvergenceDepth)
    end

    -- Smooth convergence transition (especially when reverting to zero)
    if self.currentConvergenceDisparity == nil or dt == nil then
        self.currentConvergenceDisparity = targetDisparity
    else
        local dtSec = 0.0166
        if dt > 0 then
            dtSec = (dt > 1.0) and (dt * 0.001) or dt
        end
        local smoothSpeed = self.CONVERGENCE_SMOOTH_SPEED or 12.0
        local step = math.min(1.0, dtSec * smoothSpeed)
        self.currentConvergenceDisparity = self.currentConvergenceDisparity + (targetDisparity - self.currentConvergenceDisparity) * step
        if math.abs(self.currentConvergenceDisparity - targetDisparity) < 0.0001 then
            self.currentConvergenceDisparity = targetDisparity
        end
    end

    -- Active AFR frame parity:
    -- 0: Left eye (positive offset +Delta x)
    -- 1: Right eye (negative offset -Delta x)
    local parity = self:getFrameParity()
    local offset = self:getConvergenceOffset(parity, self.currentConvergenceDisparity)

    self.currentConvergenceOffset = offset

    -- Calculate base anchors (immutable anchor references to prevent drift)
    local baseLeft = self.hudAnchorLeftBase or self.DEFAULT_HUD_INSET_X
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_INSET_X)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_INSET_Y
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_INSET_Y)

    -- Apply convergence offset to horizontal anchors without cumulative drift
    local shiftedLeft = self:clamp(baseLeft + offset, 0.0, 1.0)
    local shiftedRight = self:clamp(baseRight + offset, 0.0, 1.0)

    self.hudAnchorLeft = shiftedLeft
    self.hudAnchorRight = shiftedRight
    self.hudAnchorBottom = baseBottom
    self.hudAnchorTop = baseTop

    _G.g_hudAnchorLeft = shiftedLeft
    _G.g_hudAnchorRight = shiftedRight
    _G.g_hudAnchorBottom = baseBottom
    _G.g_hudAnchorTop = baseTop

    -- Fast per-frame convergence position update without storeScaledValues/rebuild
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud ~= nil then
        local sm = hud.speedMeter or hud.speedMeterDisplay
        local fl = self:getFillLevelsDisplay(hud)
        local targetSmBottom = baseBottom

        if fl ~= nil and self.fillLevelUnderVehicle then
            local _, fillBottom, liftedSmBottom = self:positionFillLevelsUnderVehicle(fl, offset)
            if liftedSmBottom ~= nil then
                targetSmBottom = liftedSmBottom
            end
        end

        if sm ~= nil and type(sm.setPosition) == "function" then
            pcall(function() sm:setPosition(shiftedRight, targetSmBottom) end)
        end
        local ih = hud.inputHelp or hud.inputHelpDisplay
        if ih ~= nil and type(ih.setPosition) == "function" then
            pcall(function() ih:setPosition(shiftedLeft, baseTop) end)
        end
        local gi = hud.gameInfo or hud.gameInfoDisplay
        if gi ~= nil and type(gi.setPosition) == "function" then
            pcall(function() gi:setPosition(shiftedRight, baseTop) end)
        end
        local ca = hud.contextAction or hud.contextActionDisplay
        if ca ~= nil and type(ca.setPosition) == "function" then
            pcall(function() ca:setPosition(0.5 + offset, baseBottom) end)
        end
        local im = hud.ingameMap or hud.ingameMapDisplay
        if im ~= nil and type(im.setPosition) == "function" then
            pcall(function() im:setPosition(shiftedLeft, baseBottom) end)
        end
    end

    return shiftedLeft, shiftedRight, offset
end

function VRCompanion:getHudState()
    return self.hudState or self.HUD_STATE_HIDDEN
end

local STREAMLINED_ALLOWED_DISPLAYS = {
    SpeedMeterDisplay = true,
    speedMeter = true,
    speedMeterDisplay = true,
    ContextActionDisplay = true,
    contextAction = true,
    contextActionDisplay = true,
    FillLevelsDisplay = true,
    fillLevels = true,
    fillLevelsDisplay = true
}

function VRCompanion:isDisplayVisible(displayName, state)
    if displayName == nil then return false end
    state = state or self:getHudState()
    if state == self.HUD_STATE_HIDDEN then
        return false
    elseif state == self.HUD_STATE_STREAMLINED then
        return STREAMLINED_ALLOWED_DISPLAYS[displayName] == true
    elseif state == self.HUD_STATE_FULL then
        return true
    end
    return false
end

local function setDisplayVisible(display, isVisible)
    if display == nil then return end
    display.isVisible = isVisible
    if type(display.setIsVisible) == "function" then
        pcall(function() display:setIsVisible(isVisible) end)
    elseif type(display.setVisible) == "function" then
        pcall(function() display:setVisible(isVisible) end)
    end
end

function VRCompanion:applyHudState(state)
    state = state or self.hudState or self.HUD_STATE_HIDDEN

    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud == nil then
        return
    end

    local fillLevels = self:getFillLevelsDisplay(hud)

    local isHudVisible = (state ~= self.HUD_STATE_HIDDEN)
    if type(hud.setIsVisible) == "function" then
        pcall(function() hud:setIsVisible(isHudVisible) end)
    end
    hud.isVisible = isHudVisible

    -- Update individual displays using the single-source-of-truth isDisplayVisible helper
    local displays = {
        { display = hud.speedMeter or hud.speedMeterDisplay, name = "SpeedMeterDisplay" },
        { display = fillLevels, name = "FillLevelsDisplay" },
        { display = hud.contextAction or hud.contextActionDisplay, name = "ContextActionDisplay" },
        { display = hud.inputHelp or hud.inputHelpDisplay, name = "InputHelpDisplay" },
        { display = hud.ingameMap or hud.ingameMapDisplay, name = "IngameMap" },
        { display = hud.gameInfo or hud.gameInfoDisplay, name = "GameInfoDisplay" }
    }
    for _, entry in ipairs(displays) do
        local vis = self:isDisplayVisible(entry.name, state)
        setDisplayVisible(entry.display, vis)
    end

    -- Propagate visibility to individual vehicle telemetry subcomponents if present
    local sm = hud.speedMeter or hud.speedMeterDisplay
    if sm ~= nil then
        local smVis = self:isDisplayVisible("SpeedMeterDisplay", state)
        local smSubMetrics = {
            sm.vehicleDamage or sm.damage,
            sm.fuelLevel or sm.fuel,
            sm.gear,
            sm.rpm,
            sm.fillLevelsDisplay or sm.fillLevels
        }
        for _, subMetric in ipairs(smSubMetrics) do
            if subMetric ~= nil then
                setDisplayVisible(subMetric, smVis)
            end
        end
    end

    -- Update engine HUD display toggle setters
    local inputHelpVis = self:isDisplayVisible("InputHelpDisplay", state)
    local ingameMapVis = self:isDisplayVisible("IngameMap", state)
    local gameInfoVis = self:isDisplayVisible("GameInfoDisplay", state)

    if type(hud.setInputHelpVisible) == "function" then
        pcall(function() hud:setInputHelpVisible(inputHelpVis) end)
    end
    if type(hud.setIngameMapVisible) == "function" then
        pcall(function() hud:setIngameMapVisible(ingameMapVis) end)
    end
    if type(hud.setGameInfoVisible) == "function" then
        pcall(function() hud:setGameInfoVisible(gameInfoVis) end)
    end
end

function VRCompanion:setHudState(newState)
    local state = self:clamp(newState or self.HUD_STATE_HIDDEN, self.HUD_STATE_HIDDEN, self.HUD_STATE_FULL)
    self.hudState = state
    self:applyHudState(state)
    return self.hudState
end

function VRCompanion:cycleHudState()
    local curTime = g_time
    if curTime ~= nil and self.lastHudToggleTime ~= nil and (curTime - self.lastHudToggleTime) < 300 then
        return self:getHudState()
    end
    self.lastHudToggleTime = curTime

    local curState = self:getHudState()
    local nextState = (curState + 1) % 3
    self:setHudState(nextState)
    local stateName = (self.HUD_STATE_NAMES and self.HUD_STATE_NAMES[nextState]) or tostring(nextState)
    print(string.format("[FS25_VR] HUD state changed: %s (%d)", stateName, nextState))

    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        local desc = "All Off"
        if nextState == self.HUD_STATE_STREAMLINED then
            desc = "Vehicle Telemetry Only"
        elseif nextState == self.HUD_STATE_FULL then
            desc = "All Elements On"
        end
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: %s (%s)", stateName, desc), 2500)
        end)
    end

    return nextState
end

function VRCompanion:onHudToggleAction(...)
    return self:cycleHudState()
end


function VRCompanion:clamp(val, minVal, maxVal)
    if val == nil then return minVal end
    if val < minVal then return minVal end
    if val > maxVal then return maxVal end
    return val
end

function VRCompanion:calculateHudAnchors(insetX, insetY, scaleMultiplier, insetTop)
    local clampedInsetX = self:clamp(insetX or self.DEFAULT_HUD_INSET_X, self.MIN_HUD_INSET_X, self.MAX_HUD_INSET_X)
    local clampedInsetY = self:clamp(insetY or self.DEFAULT_HUD_INSET_Y, self.MIN_HUD_INSET_Y, self.MAX_HUD_INSET_Y)
    local rawInsetTop = insetTop or clampedInsetY
    local clampedInsetTop = self:clamp(rawInsetTop, self.MIN_HUD_INSET_TOP, self.MAX_HUD_INSET_TOP)
    local clampedScale = self:clamp(scaleMultiplier or self.DEFAULT_HUD_UI_SCALE, self.MIN_HUD_UI_SCALE, self.MAX_HUD_UI_SCALE)

    local left = clampedInsetX
    local right = 1.0 - clampedInsetX
    local bottom = clampedInsetY
    local top = 1.0 - clampedInsetTop

    return left, right, bottom, top, clampedScale
end

function VRCompanion:configureHudInsets(insetX, insetY, scaleMultiplier, insetTop)
    local left, right, bottom, top, scale = self:calculateHudAnchors(insetX, insetY, scaleMultiplier, insetTop)
    self.hudInsetX = self:clamp(insetX or self.DEFAULT_HUD_INSET_X, self.MIN_HUD_INSET_X, self.MAX_HUD_INSET_X)
    self.hudInsetY = self:clamp(insetY or self.DEFAULT_HUD_INSET_Y, self.MIN_HUD_INSET_Y, self.MAX_HUD_INSET_Y)
    if insetTop ~= nil then
        self.hudInsetTop = self:clamp(insetTop, self.MIN_HUD_INSET_TOP, self.MAX_HUD_INSET_TOP)
    else
        self.hudInsetTop = nil
    end
    self.hudUiScaleMultiplier = scale
    self.hudAnchorLeftBase = left
    self.hudAnchorRightBase = right
    self.hudAnchorBottomBase = bottom
    self.hudAnchorTopBase = top
    self.hudAnchorLeft = left
    self.hudAnchorRight = right
    self.hudAnchorBottom = bottom
    self.hudAnchorTop = top
    self.effectiveUiScale = scale
    self.currentConvergenceOffset = 0.0
end

function VRCompanion:getHudInsets()
    return {
        left = self.hudAnchorLeft or self.DEFAULT_HUD_INSET_X,
        right = self.hudAnchorRight or (1.0 - self.DEFAULT_HUD_INSET_X),
        bottom = self.hudAnchorBottom or self.DEFAULT_HUD_INSET_Y,
        top = self.hudAnchorTop or (1.0 - self.DEFAULT_HUD_INSET_Y),
        baseLeft = self.hudAnchorLeftBase or self.hudAnchorLeft or self.DEFAULT_HUD_INSET_X,
        baseRight = self.hudAnchorRightBase or self.hudAnchorRight or (1.0 - self.DEFAULT_HUD_INSET_X),
        insetX = self.hudInsetX or self.DEFAULT_HUD_INSET_X,
        insetY = self.hudInsetY or self.DEFAULT_HUD_INSET_Y,
        insetTop = self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y,
        scaleMultiplier = self.hudUiScaleMultiplier or self.DEFAULT_HUD_UI_SCALE,
        effectiveScale = self.effectiveUiScale or self.DEFAULT_HUD_UI_SCALE,
        convergenceOffset = self.currentConvergenceOffset or 0.0,
        convergenceDisparity = self.currentConvergenceDisparity or 0.0,
        convergenceDepth = self.hudConvergenceDepth or self.DEFAULT_CONVERGENCE_DEPTH,
        fillLevelUnderVehicle = (self.fillLevelUnderVehicle == true),
        fillLevelGap = self.hudFillLevelGap or self.DEFAULT_HUD_FILL_LEVEL_GAP,
        showHudLoadNotification = (self.showHudLoadNotification == true)
    }
end

local HUD_DISPLAY_ANCHORS = {
    SpeedMeterDisplay = { alignX = "right", alignY = "bottom" },
    InputHelpDisplay = { alignX = "left", alignY = "top" },
    GameInfoDisplay = { alignX = "right", alignY = "top" },
    ContextActionDisplay = { alignX = "left", alignY = "bottom" },
    IngameMap = { alignX = "left", alignY = "bottom" },
    FillLevelsDisplay = { alignX = "right", alignY = "bottom" }
}

local function readXmlFloat(getXmlFloatFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        local val = getXmlFloatFn(xmlFile, k)
        if val ~= nil then
            return val
        end
    end
    return nil
end

local function readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        if type(getXmlBoolFn) == "function" then
            local val = getXmlBoolFn(xmlFile, k)
            if val ~= nil then
                return val
            end
        end
        if type(getXmlStringFn) == "function" then
            local str = getXmlStringFn(xmlFile, k)
            if str ~= nil then
                local lower = string.lower(tostring(str))
                if lower == "true" or lower == "1" then return true
                elseif lower == "false" or lower == "0" then return false end
            end
        end
    end
    return nil
end

function VRCompanion:getUserSettingsPath()
    local getAppPathFn = getUserProfileAppPath or _G.getUserProfileAppPath
    if type(getAppPathFn) == "function" then
        local appPath = getAppPathFn()
        if appPath ~= nil and appPath ~= "" then
            return appPath .. "modSettings/FS25_VRCompanion/settings.xml"
        end
    end
    return nil
end

function VRCompanion:saveConfiguration(xmlPath)
    local path = xmlPath or self:getUserSettingsPath()
    if path == nil then return false end

    local createFolderFn = createFolder or _G.createFolder
    local createXmlFn = createXMLFile or _G.createXMLFile
    local setXmlFloatFn = setXMLFloat or _G.setXMLFloat
    local setXmlBoolFn = setXMLBool or _G.setXMLBool
    local saveXmlFn = saveXMLFile or _G.saveXMLFile
    local deleteXmlFn = delete or _G.delete

    if type(createXmlFn) ~= "function" or type(setXmlFloatFn) ~= "function" or type(saveXmlFn) ~= "function" then
        return false
    end

    local getAppPathFn = getUserProfileAppPath or _G.getUserProfileAppPath
    if type(createFolderFn) == "function" and type(getAppPathFn) == "function" then
        local appPath = getAppPathFn()
        if appPath ~= nil and appPath ~= "" then
            pcall(createFolderFn, appPath .. "modSettings")
            pcall(createFolderFn, appPath .. "modSettings/FS25_VRCompanion")
        end
    end

    local ok, xmlFile = pcall(createXmlFn, "VRCompanionConfig", path, "vrSettings")
    if ok and xmlFile ~= 0 and xmlFile ~= nil then
        setXmlFloatFn(xmlFile, "vrSettings.hudInsetHorizontal", self.hudInsetX or self.DEFAULT_HUD_INSET_X)
        setXmlFloatFn(xmlFile, "vrSettings.hudInsetVertical", self.hudInsetY or self.DEFAULT_HUD_INSET_Y)
        setXmlFloatFn(xmlFile, "vrSettings.hudInsetTop", self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y)
        setXmlFloatFn(xmlFile, "vrSettings.hudUiScale", self.hudUiScaleMultiplier or self.DEFAULT_HUD_UI_SCALE)
        setXmlFloatFn(xmlFile, "vrSettings.hudConvergenceDepth", self.hudConvergenceDepth or self.DEFAULT_CONVERGENCE_DEPTH)
        if type(setXmlBoolFn) == "function" then
            setXmlBoolFn(xmlFile, "vrSettings.fillLevelUnderVehicle", self.fillLevelUnderVehicle == true)
            setXmlBoolFn(xmlFile, "vrSettings.showHudLoadNotification", self.showHudLoadNotification == true)
            setXmlBoolFn(xmlFile, "vrSettings.recenterVrTrackingSpace", self.recenterVrTrackingSpace ~= false)
        end
        setXmlFloatFn(xmlFile, "vrSettings.hudFillLevelGap", self.hudFillLevelGap or self.DEFAULT_HUD_FILL_LEVEL_GAP)

        pcall(saveXmlFn, xmlFile)
        if type(deleteXmlFn) == "function" then
            pcall(deleteXmlFn, xmlFile)
        end
        print(string.format("[FS25_VR] Saved VR HUD configuration to: %s", tostring(path)))
        return true
    end
    return false
end

function VRCompanion:loadConfiguration(xmlPath)
    local insetX = self.DEFAULT_HUD_INSET_X
    local insetY = self.DEFAULT_HUD_INSET_Y
    local scale = self.DEFAULT_HUD_UI_SCALE
    local insetTop = nil

    local loadXmlFn = loadXMLFile or _G.loadXMLFile
    local getXmlFloatFn = getXMLFloat or _G.getXMLFloat
    local getXmlBoolFn = getXMLBool or _G.getXMLBool
    local getXmlStringFn = getXMLString or _G.getXMLString
    local deleteXmlFn = delete or _G.delete

    local function readSettingsFromXml(filePath)
        if filePath ~= nil and type(loadXmlFn) == "function" and type(getXmlFloatFn) == "function" then
            local ok, xmlFile = pcall(loadXmlFn, "VRCompanionConfig", filePath)
            if ok and xmlFile ~= 0 and xmlFile ~= nil then
                local loadedX = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudInsetHorizontal", "vrSettings.hudInsetHorizontal#value",
                    "modDesc.vrSettings.hudInsetHorizontal", "modDesc.vrSettings.hudInsetHorizontal#value")
                if loadedX ~= nil then insetX = loadedX end

                local loadedY = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudInsetVertical", "vrSettings.hudInsetVertical#value",
                    "modDesc.vrSettings.hudInsetVertical", "modDesc.vrSettings.hudInsetVertical#value")
                if loadedY ~= nil then insetY = loadedY end

                local loadedInsetTop = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudInsetTop", "vrSettings.hudInsetTop#value",
                    "vrSettings.hudTopInset", "vrSettings.hudTopInset#value",
                    "vrSettings.hudInsetVerticalTop", "vrSettings.hudInsetVerticalTop#value",
                    "modDesc.vrSettings.hudInsetTop", "modDesc.vrSettings.hudInsetTop#value",
                    "modDesc.vrSettings.hudTopInset", "modDesc.vrSettings.hudTopInset#value",
                    "modDesc.vrSettings.hudInsetVerticalTop", "modDesc.vrSettings.hudInsetVerticalTop#value")
                if loadedInsetTop ~= nil then
                    insetTop = self:clamp(loadedInsetTop, self.MIN_HUD_INSET_TOP, self.MAX_HUD_INSET_TOP)
                end

                local loadedScale = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudUiScale", "vrSettings.hudUiScale#value",
                    "modDesc.vrSettings.hudUiScale", "modDesc.vrSettings.hudUiScale#value")
                if loadedScale ~= nil then scale = loadedScale end

                local loadedDepth = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudConvergenceDepth", "vrSettings.hudConvergenceDepth#value",
                    "modDesc.vrSettings.hudConvergenceDepth", "modDesc.vrSettings.hudConvergenceDepth#value")
                if loadedDepth ~= nil then
                    self.hudConvergenceDepth = loadedDepth
                else
                    self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
                end

                local loadedFillUnder = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.fillLevelUnderVehicle", "vrSettings.fillLevelUnderVehicle#value",
                    "modDesc.vrSettings.fillLevelUnderVehicle", "modDesc.vrSettings.fillLevelUnderVehicle#value")
                if loadedFillUnder ~= nil then
                    self.fillLevelUnderVehicle = loadedFillUnder
                end

                local loadedGap = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.hudFillLevelGap", "vrSettings.hudFillLevelGap#value",
                    "modDesc.vrSettings.hudFillLevelGap", "modDesc.vrSettings.hudFillLevelGap#value")
                if loadedGap ~= nil then
                    self.hudFillLevelGap = self:clamp(loadedGap, 0.0, 0.05)
                else
                    self.hudFillLevelGap = self.DEFAULT_HUD_FILL_LEVEL_GAP
                end

                local loadedShowNotification = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.showHudLoadNotification", "vrSettings.showHudLoadNotification#value",
                    "vrSettings.showHudLoadAlert", "vrSettings.showHudLoadAlert#value",
                    "vrSettings.showLoadNotification", "vrSettings.showLoadNotification#value",
                    "modDesc.vrSettings.showHudLoadNotification", "modDesc.vrSettings.showHudLoadNotification#value",
                    "modDesc.vrSettings.showHudLoadAlert", "modDesc.vrSettings.showHudLoadAlert#value")
                if loadedShowNotification ~= nil then
                    self.showHudLoadNotification = loadedShowNotification
                end

                local loadedRecenterTracking = readXmlBool(getXmlBoolFn, getXmlStringFn, xmlFile,
                    "vrSettings.recenterVrTrackingSpace", "vrSettings.recenterVrTrackingSpace#value",
                    "vrSettings.recenterTracking", "vrSettings.recenterTracking#value",
                    "modDesc.vrSettings.recenterVrTrackingSpace", "modDesc.vrSettings.recenterVrTrackingSpace#value",
                    "modDesc.vrSettings.recenterTracking", "modDesc.vrSettings.recenterTracking#value")
                if loadedRecenterTracking ~= nil then
                    self.recenterVrTrackingSpace = loadedRecenterTracking
                end

                if type(deleteXmlFn) == "function" then
                    pcall(deleteXmlFn, xmlFile)
                end
                return true
            end
        end
        return false
    end

    if xmlPath ~= nil then
        readSettingsFromXml(xmlPath)
    else
        -- 1. Read base defaults from modDesc.xml
        local modDir = self.modDirectory or _G.g_currentModDirectory or ""
        if modDir ~= "" then
            readSettingsFromXml(modDir .. "modDesc.xml")
        end

        -- 2. Read user override from modSettings/FS25_VRCompanion/settings.xml if present
        local userPath = self:getUserSettingsPath()
        local loadedUser = false
        if userPath ~= nil then
            loadedUser = readSettingsFromXml(userPath)
            if loadedUser then
                print(string.format("[FS25_VR] Loaded user settings from: %s", tostring(userPath)))
            end
        end

        -- 3. If user settings file doesn't exist, create it with current values
        if not loadedUser and userPath ~= nil then
            self:saveConfiguration(userPath)
        end
    end

    if self.fillLevelUnderVehicle == nil then
        self.fillLevelUnderVehicle = self.DEFAULT_FILL_LEVEL_UNDER_VEHICLE
    end
    if self.showHudLoadNotification == nil then
        self.showHudLoadNotification = self.DEFAULT_SHOW_HUD_LOAD_NOTIFICATION
    end
    if self.recenterVrTrackingSpace == nil then
        self.recenterVrTrackingSpace = self.DEFAULT_RECENTER_VR_TRACKING_SPACE
    end

    self:configureHudInsets(insetX, insetY, scale, insetTop)
    return self:getHudInsets()
end

function VRCompanion:positionHudDisplay(display, displayName)
    if display == nil then return end

    if displayName == "FillLevelsDisplay" and self.fillLevelUnderVehicle then
        if type(display.setScale) == "function" then
            pcall(function() display:setScale(self.effectiveUiScale) end)
        end
        self:positionFillLevelsUnderVehicle(display)
        return
    end

    local anchorConfig = HUD_DISPLAY_ANCHORS[displayName] or { alignX = "left", alignY = "bottom" }
    local w = (type(display.getWidth) == "function" and display:getWidth()) or display.width or 0
    local h = (type(display.getHeight) == "function" and display:getHeight()) or display.height or 0

    local anchorX = (anchorConfig.alignX == "right") and (self.hudAnchorRight - w) or self.hudAnchorLeft
    local anchorY = (anchorConfig.alignY == "top") and (self.hudAnchorTop - h) or self.hudAnchorBottom

    display.anchorX = anchorX
    display.anchorY = anchorY
    display.anchorLeft = self.hudAnchorLeft
    display.anchorRight = self.hudAnchorRight
    display.anchorBottom = self.hudAnchorBottom
    display.anchorTop = self.hudAnchorTop
    display.vrUiScale = self.effectiveUiScale

    if type(display.setPosition) == "function" then
        pcall(function() display:setPosition(anchorX, anchorY) end)
    end
    if type(display.setScale) == "function" then
        pcall(function() display:setScale(self.effectiveUiScale) end)
    end
    if type(display.storeScaledValues) == "function" then
        pcall(function() display:storeScaledValues() end)
    end
    if type(display.reposition) == "function" then
        pcall(function() display:reposition() end)
    end
    if type(display.updatePosition) == "function" then
        pcall(function() display:updatePosition() end)
    end
end

function VRCompanion:repositionAllHudDisplays(hud)
    local targetHud = hud
    if targetHud == nil then
        local mission = g_currentMission or _G.g_currentMission
        targetHud = (mission ~= nil) and mission.hud or nil
    end
    if targetHud == nil then return end

    local d1 = targetHud.speedMeter or targetHud.speedMeterDisplay
    if d1 ~= nil then self:positionHudDisplay(d1, "SpeedMeterDisplay") end

    local d2 = targetHud.inputHelp or targetHud.inputHelpDisplay
    if d2 ~= nil then self:positionHudDisplay(d2, "InputHelpDisplay") end

    local d3 = targetHud.gameInfo or targetHud.gameInfoDisplay
    if d3 ~= nil then self:positionHudDisplay(d3, "GameInfoDisplay") end

    local d4 = targetHud.contextAction or targetHud.contextActionDisplay
    if d4 ~= nil then self:positionHudDisplay(d4, "ContextActionDisplay") end

    local d5 = targetHud.ingameMap or targetHud.ingameMapDisplay
    if d5 ~= nil then self:positionHudDisplay(d5, "IngameMap") end

    local d6 = self:getFillLevelsDisplay(targetHud)
    if d6 ~= nil then self:positionHudDisplay(d6, "FillLevelsDisplay") end

    if type(targetHud.storeScaledValues) == "function" then
        pcall(function() targetHud:storeScaledValues() end)
    end
    if type(targetHud.updateDimensions) == "function" then
        pcall(function() targetHud:updateDimensions() end)
    end
end

function VRCompanion:applyHudInsets()
    -- 1. Backup original engine anchors if not previously saved
    if self.origHudAnchors == nil then
        self.origHudAnchors = {
            left = _G.g_hudAnchorLeft or 0.0,
            right = _G.g_hudAnchorRight or 1.0,
            bottom = _G.g_hudAnchorBottom or 0.0,
            top = _G.g_hudAnchorTop or 1.0
        }
    end

    -- 2. Update global engine HUD anchors
    _G.g_hudAnchorLeft = self.hudAnchorLeft
    _G.g_hudAnchorRight = self.hudAnchorRight
    _G.g_hudAnchorBottom = self.hudAnchorBottom
    _G.g_hudAnchorTop = self.hudAnchorTop

    -- 3. Calculate effective UI scale from base setting and VR multiplier
    local baseScale = 1.0
    local gs = g_gameSettings or _G.g_gameSettings
    local gsClass = GameSettings or _G.GameSettings
    if gs ~= nil and type(gs.getValue) == "function" and gsClass ~= nil and gsClass.SETTING ~= nil and gsClass.SETTING.UI_SCALE ~= nil then
        baseScale = gs:getValue(gsClass.SETTING.UI_SCALE) or 1.0
    end
    if self.origUiScale == nil then
        self.origUiScale = baseScale
    end
    self.effectiveUiScale = baseScale * self.hudUiScaleMultiplier

    -- 4. Reposition all standard HUD displays
    self:repositionAllHudDisplays(hud)

    print(string.format("[FS25_VR] Applied VR HUD Insets: Left=%.2f, Right=%.2f, Bottom=%.2f, Top=%.2f (UI Scale: %.2fx)",
        self.hudAnchorLeft, self.hudAnchorRight, self.hudAnchorBottom, self.hudAnchorTop, self.effectiveUiScale))
end

function VRCompanion:restoreHudInsets()
    local left = 0.0
    local right = 1.0
    local bottom = 0.0
    local top = 1.0

    if self.origHudAnchors ~= nil then
        left = self.origHudAnchors.left or 0.0
        right = self.origHudAnchors.right or 1.0
        bottom = self.origHudAnchors.bottom or 0.0
        top = self.origHudAnchors.top or 1.0
        self.origHudAnchors = nil
    end

    _G.g_hudAnchorLeft = left
    _G.g_hudAnchorRight = right
    _G.g_hudAnchorBottom = bottom
    _G.g_hudAnchorTop = top

    local baseLeft = self.hudAnchorLeftBase or self.DEFAULT_HUD_INSET_X
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_INSET_X)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_INSET_Y
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_INSET_Y)

    self.hudAnchorLeft = baseLeft
    self.hudAnchorRight = baseRight
    self.hudAnchorBottom = baseBottom
    self.hudAnchorTop = baseTop
    self.currentConvergenceOffset = 0.0
    self.currentConvergenceDisparity = 0.0

    local restoredScale = self.origUiScale or 1.0
    self.origUiScale = nil

    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud ~= nil then
        local standardDisplays = {
            hud.speedMeter or hud.speedMeterDisplay,
            hud.inputHelp or hud.inputHelpDisplay,
            hud.gameInfo or hud.gameInfoDisplay,
            hud.contextAction or hud.contextActionDisplay,
            hud.ingameMap or hud.ingameMapDisplay
        }
        for _, display in ipairs(standardDisplays) do
            if display ~= nil then
                if type(display.storeScaledValues) == "function" then
                    pcall(function() display:storeScaledValues() end)
                end
                if type(display.setScale) == "function" then
                    pcall(function() display:setScale(restoredScale) end)
                end
                if type(display.reposition) == "function" then
                    pcall(function() display:reposition() end)
                end
            end
        end
        if type(hud.storeScaledValues) == "function" then
            pcall(function() hud:storeScaledValues() end)
        end
    end
    print("[FS25_VR] Restored standard 2D HUD corner anchors.")
end

local HUD_HOOKED_CLASSES = {
    { className = "SpeedMeterDisplay", displayName = "SpeedMeterDisplay" },
    { className = "ContextActionDisplay", displayName = "ContextActionDisplay" },
    { className = "FillLevelsDisplay", displayName = "FillLevelsDisplay" },
    { className = "IngameMap", displayName = "IngameMap" },
    { className = "InputHelpDisplay", displayName = "InputHelpDisplay" },
    { className = "GameInfoDisplay", displayName = "GameInfoDisplay" }
}

local function getGlobalClass(className)
    if className == "SpeedMeterDisplay" then return SpeedMeterDisplay or (_G and _G.SpeedMeterDisplay)
    elseif className == "ContextActionDisplay" then return ContextActionDisplay or (_G and _G.ContextActionDisplay)
    elseif className == "FillLevelsDisplay" then return FillLevelsDisplay or (_G and _G.FillLevelsDisplay)
    elseif className == "IngameMap" then return IngameMap or (_G and _G.IngameMap)
    elseif className == "InputHelpDisplay" then return InputHelpDisplay or (_G and _G.InputHelpDisplay)
    elseif className == "GameInfoDisplay" then return GameInfoDisplay or (_G and _G.GameInfoDisplay)
    elseif className == "HUD" then return HUD or (_G and _G.HUD)
    end
    return _G and _G[className]
end

function VRCompanion:hookHud()
    if self.hudHooked then
        self:unhookHud()
    end

    self.origHudHooks = {}

    local function hookMethod(tbl, methodName, hookFn)
        if tbl ~= nil and tbl[methodName] ~= nil and type(tbl[methodName]) == "function" then
            table.insert(self.origHudHooks, {
                tbl = tbl,
                methodName = methodName,
                orig = tbl[methodName]
            })
            tbl[methodName] = hookFn
        end
    end

    -- 1. Hook display classes (draw, getIsVisible)
    for _, info in ipairs(HUD_HOOKED_CLASSES) do
        local cls = getGlobalClass(info.className)
        if cls ~= nil then
            local displayName = info.displayName

            -- Hook draw
            if cls.draw ~= nil and type(cls.draw) == "function" then
                local origDraw = cls.draw
                hookMethod(cls, "draw", function(displaySelf, ...)
                    if displayName == "FillLevelsDisplay" then
                        self.activeFillLevelsDisplay = displaySelf
                        if self.fillLevelUnderVehicle then
                            self:positionFillLevelsUnderVehicle(displaySelf)
                        end
                        if not self.hasLoggedFillLevelsDisplayInfo then
                            self.hasLoggedFillLevelsDisplayInfo = true
                            local bgLeftX = (displaySelf.bgLeft and displaySelf.bgLeft.x) or "nil"
                            local bgLeftY = (displaySelf.bgLeft and displaySelf.bgLeft.y) or "nil"
                            local bgRightX = (displaySelf.bgRight and displaySelf.bgRight.x) or "nil"
                            local bgRightY = (displaySelf.bgRight and displaySelf.bgRight.y) or "nil"
                            local countData = (type(displaySelf.fillLevelData) == "table" and #displaySelf.fillLevelData) or 0
                            print(string.format("[FS25_VR] [FillLevelsDisplay Diagnostics] x=%.3f, y=%.3f, bgLeft=(%s, %s), bgRight=(%s, %s), numLevels=%s",
                                displaySelf.x or -1, displaySelf.y or -1,
                                tostring(bgLeftX), tostring(bgLeftY),
                                tostring(bgRightX), tostring(bgRightY),
                                tostring(countData)))
                        end
                    end
                    if not self:isDisplayVisible(displayName) then
                        return
                    end
                    return origDraw(displaySelf, ...)
                end)
            end

            -- Hook getIsVisible
            if cls.getIsVisible ~= nil and type(cls.getIsVisible) == "function" then
                local origGetIsVisible = cls.getIsVisible
                hookMethod(cls, "getIsVisible", function(displaySelf, ...)
                    if displayName == "FillLevelsDisplay" then
                        self.activeFillLevelsDisplay = displaySelf
                    end
                    if not self:isDisplayVisible(displayName) then
                        return false
                    end
                    return origGetIsVisible(displaySelf, ...)
                end)
            end
        end
    end

    -- 2. Hook HUD class or active instance
    local hudCls = getGlobalClass("HUD")
    local mission = g_currentMission or (_G and _G.g_currentMission)
    local hudInstance = (mission ~= nil) and mission.hud or nil
    local hudTargets = {}
    if hudCls ~= nil then table.insert(hudTargets, hudCls) end
    if hudInstance ~= nil and hudInstance ~= hudCls then table.insert(hudTargets, hudInstance) end

    for _, target in ipairs(hudTargets) do
        if target.draw ~= nil and type(target.draw) == "function" then
            local origHudDraw = target.draw
            hookMethod(target, "draw", function(hudSelf, ...)
                if self:getHudState() == self.HUD_STATE_HIDDEN then
                    return
                end
                return origHudDraw(hudSelf, ...)
            end)
        end

        if target.setIsVisible ~= nil and type(target.setIsVisible) == "function" then
            local origSetIsVisible = target.setIsVisible
            hookMethod(target, "setIsVisible", function(hudSelf, isVisible, ...)
                if self:getHudState() == self.HUD_STATE_HIDDEN then
                    return origSetIsVisible(hudSelf, false, ...)
                end
                return origSetIsVisible(hudSelf, isVisible, ...)
            end)
        end

        local hudSetterHooks = {
            { methodName = "setInputHelpVisible", displayName = "InputHelpDisplay" },
            { methodName = "setIngameMapVisible", displayName = "IngameMap" },
            { methodName = "setGameInfoVisible", displayName = "GameInfoDisplay" }
        }
        for _, setter in ipairs(hudSetterHooks) do
            local mName = setter.methodName
            local dName = setter.displayName
            if target[mName] ~= nil and type(target[mName]) == "function" then
                local origSetter = target[mName]
                hookMethod(target, mName, function(hudSelf, isVisible, ...)
                    if not self:isDisplayVisible(dName) then
                        return origSetter(hudSelf, false, ...)
                    end
                    return origSetter(hudSelf, isVisible, ...)
                end)
            end
        end
    end

    if #self.origHudHooks > 0 then
        self.hudHooked = true
        print("[FS25_VR] Hooked native engine HUD display interfaces.")
    end
end

function VRCompanion:unhookHud()
    if self.origHudHooks ~= nil then
        for _, entry in ipairs(self.origHudHooks) do
            if entry.tbl ~= nil and entry.methodName ~= nil then
                entry.tbl[entry.methodName] = entry.orig
            end
        end
        self.origHudHooks = nil
    end
    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and mission.hud ~= nil then
        if type(mission.hud.setIsVisible) == "function" then
            pcall(function() mission.hud:setIsVisible(true) end)
        end
        mission.hud.isVisible = true
    end
    self.hudHooked = false
    print("[FS25_VR] Unhooked native engine HUD display interfaces.")
end


-- Proactively probe head tracking availability to trigger native LuauInterceptor detour
if type(isHeadTrackingAvailable) == "function" then
    pcall(isHeadTrackingAvailable)
elseif type(getHeadTrackingTranslation) == "function" then
    pcall(getHeadTrackingTranslation)
end

function VRCompanion:registerActionEvents(force)
    local inputBinding = g_inputBinding or _G.g_inputBinding
    if inputBinding == nil or type(inputBinding.registerActionEvent) ~= "function" then
        return
    end

    local actionEvents = inputBinding.actionEvents or (inputBinding.events and inputBinding.events)
    local isHudEventActive = false
    local isRecenterEventActive = false

    if not force and actionEvents ~= nil then
        if self.hudToggleActionEventId ~= nil and actionEvents[self.hudToggleActionEventId] ~= nil then
            isHudEventActive = true
        end
        if self.recenterActionEventId ~= nil and actionEvents[self.recenterActionEventId] ~= nil then
            isRecenterEventActive = true
        end
    end

    if force or not isHudEventActive then
        local actionId = (InputAction and InputAction.VR_HUD_TOGGLE) or (_G.InputAction and _G.InputAction.VR_HUD_TOGGLE) or "VR_HUD_TOGGLE"
        local success, eventId = inputBinding:registerActionEvent(actionId, self, self.onHudToggleAction, false, true, false, true)
        if success and eventId then
            self.hudToggleActionEventId = eventId
            if type(inputBinding.setActionEventTextPriority) == "function" then
                pcall(function() inputBinding:setActionEventTextPriority(eventId, (GS_PRIO_HIGH or _G.GS_PRIO_HIGH or 1)) end)
            end
            if type(inputBinding.setActionEventActive) == "function" then
                pcall(function() inputBinding:setActionEventActive(eventId, true) end)
            end
            if type(inputBinding.setActionEventTextVisibility) == "function" then
                pcall(function() inputBinding:setActionEventTextVisibility(eventId, true) end)
            end
            if type(inputBinding.setActionEventText) == "function" then
                pcall(function()
                    local txt = (g_i18n and g_i18n:getText("input_VR_HUD_TOGGLE")) or "Toggle VR HUD Mode"
                    inputBinding:setActionEventText(eventId, txt)
                end)
            end
            if not self.hudToggleLoggedOnce then
                print("[FS25_VR] Registered in-engine VR_HUD_TOGGLE event.")
                self.hudToggleLoggedOnce = true
            end
        end
    end

    if force or not isRecenterEventActive then
        local actionId = (InputAction and InputAction.VR_RECENTER_VIEW) or (_G.InputAction and _G.InputAction.VR_RECENTER_VIEW) or (InputAction and InputAction.VR_RECENTER) or (_G.InputAction and _G.InputAction.VR_RECENTER) or "VR_RECENTER_VIEW"
        local success, eventId = inputBinding:registerActionEvent(actionId, self, self.onRecenterAction, false, true, false, true)
        if not success and actionId ~= "VR_RECENTER" then
            local fallbackActionId = (InputAction and InputAction.VR_RECENTER) or (_G.InputAction and _G.InputAction.VR_RECENTER) or "VR_RECENTER"
            success, eventId = inputBinding:registerActionEvent(fallbackActionId, self, self.onRecenterAction, false, true, false, true)
        end
        if success and eventId then
            self.recenterActionEventId = eventId
            if type(inputBinding.setActionEventTextPriority) == "function" then
                pcall(function() inputBinding:setActionEventTextPriority(eventId, (GS_PRIO_HIGH or _G.GS_PRIO_HIGH or 1)) end)
            end
            if type(inputBinding.setActionEventActive) == "function" then
                pcall(function() inputBinding:setActionEventActive(eventId, true) end)
            end
            if type(inputBinding.setActionEventTextVisibility) == "function" then
                pcall(function() inputBinding:setActionEventTextVisibility(eventId, true) end)
            end
            if type(inputBinding.setActionEventText) == "function" then
                pcall(function()
                    local txt = (g_i18n and (g_i18n:getText("input_VR_RECENTER_VIEW") or g_i18n:getText("input_VR_RECENTER"))) or "Recenter VR View"
                    inputBinding:setActionEventText(eventId, txt)
                end)
            end
            if not self.recenterLoggedOnce then
                print("[FS25_VR] Registered in-engine VR_RECENTER event.")
                self.recenterLoggedOnce = true
            end
        end
    end
end

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

    -- Proactively trigger native detour in case loadMap executes before any camera update
    if VRBridge == nil and type(isHeadTrackingAvailable) == "function" then
        pcall(isHeadTrackingAvailable)
    end

    -- 1. Perform native-to-Luau handshake via VRBridge
    if VRBridge ~= nil then
        local version = VRBridge.version or "unknown"
        local ready = false
        if type(VRBridge.isReady) == "function" then
            ready = VRBridge.isReady()
        end

        local handshakeResult = nil
        if type(VRBridge.handshake) == "function" then
            handshakeResult = VRBridge.handshake()
        end

        if handshakeResult and handshakeResult.success == true then
            self.handshakeComplete = true
            print(string.format("[FS25_VR] Handshake successful! VRBridge detected (version: %s, mode: %s, ready: %s).",
                tostring(handshakeResult.version or version),
                tostring(handshakeResult.mode or "unknown"),
                tostring(ready)))
        else
            self.handshakeComplete = false
            print(string.format("[FS25_VR] Handshake failed or rejected by native VRBridge (version: %s, ready: %s).",
                tostring(version),
                tostring(ready)))
        end
    else
        print("[FS25_VR] VRBridge not detected. Running in standard 2D Pass-Through Mode.")
    end

    -- 2. Invoke engine undocumented stereo rendering toggles
    if type(setStereoRendering) == "function" then
        setStereoRendering(true)
        self.stereoToggled = true
        print("[FS25_VR] Invoked engine stereo rendering toggle: setStereoRendering(true)")
    else
        print("[FS25_VR] Engine stereo rendering toggle 'setStereoRendering' not found in Luau environment.")
    end

    -- 3. Query initial telemetry probe status if available
    if VRBridge ~= nil and type(VRBridge.getStereoTelemetry) == "function" then
        local telemetry = VRBridge.getStereoTelemetry()
        self.lastReportedVerdict = telemetry.verdict
        print(string.format("[FS25_VR] Telemetry Probe initialized (Frame: %d, Verdict: %s, Max Viewports: %d).",
            telemetry.frameIndex or 0,
            tostring(telemetry.verdict or "unknown"),
            telemetry.maxViewports or 0))
    end

    -- 3b. Ensure engine head tracking setting is enabled
    if g_gameSettings ~= nil and type(g_gameSettings.setValue) == "function" and GameSettings ~= nil and GameSettings.SETTING ~= nil and GameSettings.SETTING.IS_HEAD_TRACKING_ENABLED ~= nil then
        pcall(function() g_gameSettings:setValue(GameSettings.SETTING.IS_HEAD_TRACKING_ENABLED, true) end)
        print("[FS25_VR] Confirmed game setting IS_HEAD_TRACKING_ENABLED is active.")
    end

    -- 4. Hook camera and HUD systems for 6DOF tracking and selective telemetry filtering
    self:hookVehicleCamera()
    self:hookPlayerCamera()
    self:hookEnterable()
    self:hookHud()

    -- 5. Register in-engine actions (Recenter and HUD Toggle)
    self:registerActionEvents(true)

    -- 6. Synchronize initial GUI menu visibility state
    self.isMenuVisible = false
    self.guiVisibilityInitialized = false
    self:pollGuiVisibility()

    -- 7. Apply VR HUD Inset anchoring and UI scale foundation
    self:loadConfiguration()
    self:applyHudInsets()

    -- 8. Initialize HUD state to Hidden for full VR immersion (Issue #25)
    self:setHudState(self.HUD_STATE_HIDDEN)

    -- 9. Register developer and tuning console commands
    if addConsoleCommand ~= nil then
        addConsoleCommand("vrHudInsets", "Set VR HUD insets: vrHudInsets <insetX> <insetY> [scale] [topInset]", "consoleCommandSetHudInsets", self)
        addConsoleCommand("vrHudTopInset", "Set VR HUD top inset: vrHudTopInset <topInset>", "consoleCommandSetHudTopInset", self)
        addConsoleCommand("vrHudInsetTop", "Set VR HUD top inset: vrHudInsetTop <topInset>", "consoleCommandSetHudTopInset", self)
        addConsoleCommand("vrHudState", "Set VR HUD state: vrHudState <0=Hidden | 1=Streamlined | 2=Full>", "consoleCommandSetHudState", self)
        addConsoleCommand("vrHudFillUnderVehicle", "Position fill level HUD under vehicle: vrHudFillUnderVehicle <true|false>", "consoleCommandSetHudFillUnderVehicle", self)
        addConsoleCommand("vrHudFillGap", "Set gap between speedometer and fill level: vrHudFillGap <gap>", "consoleCommandSetHudFillGap", self)
        addConsoleCommand("vrHudFillLevelGap", "Set gap between speedometer and fill level: vrHudFillLevelGap <gap>", "consoleCommandSetHudFillGap", self)
        addConsoleCommand("vrHudLoadNotification", "Toggle HUD alert on map load: vrHudLoadNotification <true|false>", "consoleCommandSetHudLoadNotification", self)
        addConsoleCommand("vrHudLoadAlert", "Toggle HUD alert on map load: vrHudLoadAlert <true|false>", "consoleCommandSetHudLoadNotification", self)
        addConsoleCommand("vrRecenterMode", "Set VR Recenter Action mode: vrRecenterMode [Both | VehicleOnly | toggle]", "consoleCommandSetRecenterMode", self)
        addConsoleCommand("vrRecenterTracking", "Set VR Recenter Action mode: vrRecenterTracking [Both | VehicleOnly | toggle]", "consoleCommandSetRecenterMode", self)
    end

    if self.showHudLoadNotification == true then
        local mission = g_currentMission or _G.g_currentMission
        if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
            pcall(function()
                mission:showBlinkingWarning("VR HUD: Hidden (Press RCtrl+H to cycle)", 4000)
            end)
        end
    end
end

function VRCompanion:pollGuiVisibility()
    local isGuiVisible = false
    local gui = g_gui or _G.g_gui
    if gui ~= nil and type(gui.getIsGuiVisible) == "function" then
        local ok, visible = pcall(function() return gui:getIsGuiVisible() end)
        if ok then
            isGuiVisible = (visible == true)
        end
    end

    if self.isMenuVisible ~= isGuiVisible or not self.guiVisibilityInitialized then
        local wasMenuVisible = self.isMenuVisible
        self.isMenuVisible = isGuiVisible
        self.guiVisibilityInitialized = true
        if wasMenuVisible and not isGuiVisible then
            self:restoreMenuBackgroundCamera()
            self:applyHudState()
        end
        if VRBridge ~= nil and type(VRBridge.setMenuVisible) == "function" then
            VRBridge.setMenuVisible(isGuiVisible)
        end
        if VRBridge ~= nil and type(VRBridge.resetTemporalHistory) == "function" then
            VRBridge.resetTemporalHistory()
        end
        if isGuiVisible then
            print("[FS25_VR] In-game GUI menu opened; switched presentation to 6DOF Quad layer.")
        elseif self.frameTicks > 0 then
            print("[FS25_VR] In-game GUI menu closed; restored stereoscopic 3D projection rendering.")
        end
    end
    return isGuiVisible
end

function VRCompanion:getPlayerCamera()
    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and mission.player ~= nil then
        return mission.player.camera
    end
    return nil
end

function VRCompanion:getControlledVehicle()
    if g_localPlayer ~= nil and type(g_localPlayer.getCurrentVehicle) == "function" then
        local ok, veh = pcall(g_localPlayer.getCurrentVehicle, g_localPlayer)
        if ok and veh ~= nil then return veh end
    end
    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and mission.controlledVehicle ~= nil then
        return mission.controlledVehicle
    end
    return nil
end

function VRCompanion:getActiveVehicleCamera()
    local vehicle = self:getControlledVehicle()
    if vehicle ~= nil then
        return self:getVehicleActiveCamera(vehicle)
    end
    return nil
end

function VRCompanion:getVehicleActiveCamera(veh)
    if veh == nil then return nil end
    if type(veh.getActiveCamera) == "function" then
        local ok, cam = pcall(veh.getActiveCamera, veh)
        if ok and cam ~= nil then return cam end
    end
    if veh.spec_enterable ~= nil then
        if veh.spec_enterable.activeCamera ~= nil then
            return veh.spec_enterable.activeCamera
        end
        if veh.spec_enterable.cameras ~= nil and veh.spec_enterable.camIndex ~= nil then
            return veh.spec_enterable.cameras[veh.spec_enterable.camIndex]
        end
    end
    if veh.activeCamera ~= nil then
        return veh.activeCamera
    end
    return nil
end

function VRCompanion:initVehicleCameraState(veh, cam)
    if cam == nil then return end

    if cam.origRotY == nil then
        cam.origRotY = cam.rotY or 0
    end
    if cam.origRotX == nil then
        cam.origRotX = cam.rotX or 0
    end
    if cam.origRotZ == nil then
        cam.origRotZ = cam.rotZ or 0
    end
    if cam.rotY == nil then
        cam.rotY = cam.origRotY
    end
    if cam.rotX == nil then
        cam.rotX = cam.origRotX
    end

    if cam.origTransX == nil and cam.transX ~= nil then
        cam.origTransX = cam.transX
    end
    if cam.origTransY == nil and cam.transY ~= nil then
        cam.origTransY = cam.transY
    end
    if cam.origTransZ == nil and cam.transZ ~= nil then
        cam.origTransZ = cam.transZ
    end

    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        if cam.origHeadTrackingRotY == nil or math.abs(cam.origHeadTrackingRotY) < 0.0001 then
            local baseRy = cam.origRotY or math.pi
            local getRotationFn = getRotation or (_G and _G.getRotation)
            if type(getRotationFn) == "function" then
                local ok, rx, ry, rz = pcall(getRotationFn, cam.headTrackingNode)
                if ok and ry ~= nil and math.abs(ry) > 0.0001 then
                    baseRy = ry
                end
            end
            cam.origHeadTrackingRotX = 0
            cam.origHeadTrackingRotY = baseRy
            cam.origHeadTrackingRotZ = 0
        end
    end
end

function VRCompanion:saveVehicleRotationState(veh)
    if veh == nil then return end
    if self.vehicleRotationStates == nil then
        self.vehicleRotationStates = {}
    end

    local cam = self:getVehicleActiveCamera(veh)
    if cam ~= nil and cam.isInside then
        self:initVehicleCameraState(veh, cam)
        self.vehicleRotationStates[veh] = {
            rotY = cam.rotY,
            rotX = cam.rotX,
            origRotY = cam.origRotY,
            origRotX = cam.origRotX,
            origHeadTrackingRotX = cam.origHeadTrackingRotX,
            origHeadTrackingRotY = cam.origHeadTrackingRotY,
            origHeadTrackingRotZ = cam.origHeadTrackingRotZ,
        }
    end
end

function VRCompanion:restoreVehicleRotationState(veh)
    if veh == nil then return end
    if self.vehicleRotationStates == nil then
        self.vehicleRotationStates = {}
    end

    local cam = self:getVehicleActiveCamera(veh)
    if cam == nil or not cam.isInside then return end

    self:initVehicleCameraState(veh, cam)

    local saved = self.vehicleRotationStates[veh]
    if saved ~= nil then
        if saved.origRotY ~= nil then cam.origRotY = saved.origRotY end
        if saved.origRotX ~= nil then cam.origRotX = saved.origRotX end
        if saved.origHeadTrackingRotX ~= nil then cam.origHeadTrackingRotX = saved.origHeadTrackingRotX end
        if saved.origHeadTrackingRotY ~= nil then cam.origHeadTrackingRotY = saved.origHeadTrackingRotY end
        if saved.origHeadTrackingRotZ ~= nil then cam.origHeadTrackingRotZ = saved.origHeadTrackingRotZ end
        if saved.rotY ~= nil then cam.rotY = saved.rotY end
        if saved.rotX ~= nil then cam.rotX = saved.rotX end
    else
        if cam.origRotY ~= nil and cam.rotY == nil then
            cam.rotY = cam.origRotY
        end
        self:saveVehicleRotationState(veh)
    end
end

function VRCompanion:resetVehicleTransforms(veh)
    if veh == nil then return end
    local cam = self:getVehicleActiveCamera(veh)
    if cam ~= nil and cam.isInside then
        local setRotationFn = setRotation or (_G and _G.setRotation)
        local setTranslationFn = setTranslation or (_G and _G.setTranslation)
        local neutralRotY = cam.origRotY or cam.origHeadTrackingRotY or math.pi
        if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
            if type(setRotationFn) == "function" then
                pcall(setRotationFn, cam.headTrackingNode, cam.origHeadTrackingRotX or 0, neutralRotY, cam.origHeadTrackingRotZ or 0)
            end
        end
        if cam.cameraNode ~= nil and cam.cameraNode ~= 0 then
            local baseX = cam.origTransX or cam.transX or 0
            local baseY = cam.origTransY or cam.transY or 0
            local baseZ = cam.origTransZ or cam.transZ or 0
            if type(setTranslationFn) == "function" then
                pcall(setTranslationFn, cam.cameraNode, baseX, baseY, baseZ)
            end
            if type(setRotationFn) == "function" then
                pcall(setRotationFn, cam.cameraNode, 0, neutralRotY, 0)
            end
        end
    end
end

function VRCompanion:onEnterVehicle(veh, isControlling, player)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.resetTemporalHistory) == "function" then
        bridge.resetTemporalHistory()
    end

    local oldVeh = self.lastControlledVehicle
    if oldVeh ~= nil and oldVeh ~= veh then
        self:saveVehicleRotationState(oldVeh)
        self:resetVehicleTransforms(oldVeh)
    end

    self.lastControlledVehicle = veh
    self:restoreVehicleRotationState(veh)

    self:registerActionEvents(true)
    self:applyHudState()
end

function VRCompanion:onLeaveVehicle(veh)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.resetTemporalHistory) == "function" then
        bridge.resetTemporalHistory()
    end

    local targetVeh = veh or self.lastControlledVehicle
    if targetVeh ~= nil then
        self:saveVehicleRotationState(targetVeh)
        self:resetVehicleTransforms(targetVeh)
    end

    self.lastControlledVehicle = nil
    self:registerActionEvents(true)
    self:applyHudState()
end

function VRCompanion:onVehicleTransition(oldVeh, newVeh)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.resetTemporalHistory) == "function" then
        bridge.resetTemporalHistory()
    end

    if oldVeh ~= nil then
        self:saveVehicleRotationState(oldVeh)
        self:resetVehicleTransforms(oldVeh)
    end

    if newVeh ~= nil then
        self:restoreVehicleRotationState(newVeh)
    end

    self:registerActionEvents(true)
    self:applyHudState()
end

function VRCompanion:onVehicleCameraChanged(veh, camIndex)
    if veh ~= nil then
        local cam = self:getVehicleActiveCamera(veh)
        if cam ~= nil and cam.isInside then
            self:initVehicleCameraState(veh, cam)
            local saved = self.vehicleRotationStates and self.vehicleRotationStates[veh]
            if saved ~= nil and saved.rotY ~= nil then
                cam.rotY = saved.rotY
                if saved.rotX ~= nil then cam.rotX = saved.rotX end
            end
        end
    end
end

function VRCompanion:getPlayerBaseTranslation(cam)
    local activeCam = (cam and cam.firstPersonCamera) or (cam and cam.cameraNode)
    if cam and cam.vrBaseTranslation == nil and activeCam ~= nil and type(getTranslation) == "function" then
        local lx, ly, lz = getTranslation(activeCam)
        cam.vrBaseTranslation = { x = lx, y = ly, z = lz, [1] = lx, [2] = ly, [3] = lz }
    end
    return (cam and cam.vrBaseTranslation) or { x = 0, y = 0, z = 0, [1] = 0, [2] = 0, [3] = 0 }
end

function VRCompanion:hookVehicleCamera()
    if VehicleCamera ~= nil and VehicleCamera.update ~= nil and not self.vehicleCameraHooked then
        self.origVehicleCameraUpdate = VehicleCamera.update
        VehicleCamera.update = function(cam, dt)
            self.origVehicleCameraUpdate(cam, dt)
            VRCompanion:onVehicleCameraUpdate(cam, dt)
        end
        self.vehicleCameraHooked = true
        print("[FS25_VR] Hooked VehicleCamera.update for 6DOF VR head tracking.")
    end
end

function VRCompanion:hookEnterable()
    if Enterable ~= nil and not self.enterableHooked then
        if Enterable.onEnterVehicle ~= nil then
            self.origEnterableOnEnterVehicle = Enterable.onEnterVehicle
            Enterable.onEnterVehicle = function(veh, isControlling, player)
                self.origEnterableOnEnterVehicle(veh, isControlling, player)
                VRCompanion:onEnterVehicle(veh, isControlling, player)
            end
        end
        if Enterable.onLeaveVehicle ~= nil then
            self.origEnterableOnLeaveVehicle = Enterable.onLeaveVehicle
            Enterable.onLeaveVehicle = function(veh)
                self.origEnterableOnLeaveVehicle(veh)
                VRCompanion:onLeaveVehicle(veh)
            end
        end
        if Enterable.setActiveCameraIndex ~= nil then
            self.origEnterableSetActiveCameraIndex = Enterable.setActiveCameraIndex
            Enterable.setActiveCameraIndex = function(veh, camIndex)
                self.origEnterableSetActiveCameraIndex(veh, camIndex)
                VRCompanion:onVehicleCameraChanged(veh, camIndex)
            end
        end
        self.enterableHooked = true
    end
end

function VRCompanion:onPlayerCameraUpdateRotation(cam, dt)
    if cam == nil then return end
    if cam.getHasOverriddenTarget ~= nil and cam:getHasOverriddenTarget() then
        if cam.updateRotationFromTarget ~= nil then
            cam:updateRotationFromTarget()
        end
        return
    end

    local cameraSensitivity = 1.0
    if g_gameSettings ~= nil and type(g_gameSettings.getValue) == "function" and GameSettings ~= nil and GameSettings.SETTING ~= nil then
        cameraSensitivity = g_gameSettings:getValue(GameSettings.SETTING.CAMERA_SENSITIVITY) or 1.0
    end

    local rotationDeltaY = 0
    if cam.player ~= nil and cam.player.inputComponent ~= nil then
        rotationDeltaY = -(cam.player.inputComponent.cameraRotationY or 0) * cameraSensitivity
    end

    local cameraYaw = 0
    if cam.yawNode ~= nil and type(getRotation) == "function" then
        local _, yRot, _ = getRotation(cam.yawNode)
        cameraYaw = yRot or 0
    elseif type(cam.getRotation) == "function" then
        local _, yRot, _ = cam:getRotation()
        cameraYaw = yRot or 0
    end

    if MathUtil ~= nil and type(MathUtil.getValidLimit) == "function" then
        cameraYaw = MathUtil.getValidLimit(cameraYaw + rotationDeltaY)
    else
        cameraYaw = (cameraYaw + rotationDeltaY + math.pi) % (2 * math.pi) - math.pi
    end

    -- In VR mode: keep neck level (pitch=0, roll=0); mouse/stick only turns body yaw.
    -- Head pitch and roll are tracked by the HMD and applied during onPlayerCameraUpdate.
    if type(cam.setRotation) == "function" then
        cam:setRotation(0, cameraYaw, 0)
    end
end

function VRCompanion:hookPlayerCamera()
    if PlayerCamera ~= nil and not self.playerCameraHooked then
        if PlayerCamera.makeCurrent ~= nil then
            self.origPlayerCameraMakeCurrent = PlayerCamera.makeCurrent
            PlayerCamera.makeCurrent = function(cam)
                self.origPlayerCameraMakeCurrent(cam)
                VRCompanion:onPlayerCameraMakeCurrent(cam)
            end
        end
        if PlayerCamera.updateRotation ~= nil then
            self.origPlayerCameraUpdateRotation = PlayerCamera.updateRotation
            PlayerCamera.updateRotation = function(cam, dt)
                VRCompanion:onPlayerCameraUpdateRotation(cam, dt)
            end
        end
        if PlayerCamera.updatePosition ~= nil then
            self.origPlayerCameraUpdatePosition = PlayerCamera.updatePosition
            PlayerCamera.updatePosition = function(cam, dt)
                self.origPlayerCameraUpdatePosition(cam, dt)
                VRCompanion:onPlayerCameraUpdate(cam, dt)
            end
        elseif PlayerCamera.update ~= nil then
            self.origPlayerCameraUpdate = PlayerCamera.update
            PlayerCamera.update = function(cam, dt)
                self.origPlayerCameraUpdate(cam, dt)
                VRCompanion:onPlayerCameraUpdate(cam, dt)
            end
        end
        self.playerCameraHooked = true
        print("[FS25_VR] Hooked PlayerCamera for 6DOF VR head tracking.")
    end
end

function VRCompanion:applyCameraProjection(camNode, eyeIndex)
    if camNode == nil or camNode == 0 then
        return
    end

    if not VRBridge or type(VRBridge.getEyeProjection) ~= "function" then
        return
    end

    local proj = VRBridge.getEyeProjection(eyeIndex or 0)
    if not proj or not proj.isValid then
        return
    end

    -- Cache initial camera projection parameters for clean restoration on map unload
    if self.origCamSettings == nil then
        self.origCamSettings = {}
    end
    if self.origCamSettings[camNode] == nil then
        local origFov = nil
        if type(getFovY) == "function" then
            origFov = getFovY(camNode)
        end
        local origX, origY = 0, 0
        if type(getProjectionOffset) == "function" then
            origX, origY = getProjectionOffset(camNode)
        end
        self.origCamSettings[camNode] = {
            fovY = origFov,
            offsetX = origX,
            offsetY = origY
        }
    end

    -- Update camera vertical Field of View to match VR headset display optics
    if type(setFovY) == "function" and proj.fovY ~= nil and proj.fovY > 0 then
        setFovY(camNode, proj.fovY)
    end

    -- In VR AFR mode: keep camera centered on optical axis (0, 0) to guarantee
    -- parallel rays at infinity and natural binocular convergence without cross-eyed disparity shear.
    -- If allowObliqueProjection is explicitly enabled, pass the asymmetric projection offsets.
    if type(setProjectionOffset) == "function" then
        local offsetX = 0
        local offsetY = 0
        if self.allowObliqueProjection and proj.xOffset ~= nil and proj.yOffset ~= nil then
            offsetX = proj.xOffset * (self.projectionOffsetScale or 1.0)
            offsetY = proj.yOffset * (self.projectionOffsetScale or 1.0)
        end
        setProjectionOffset(camNode, offsetX, offsetY)
    end
end

function VRCompanion:getFrameParity()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if type(bridge.getFrameParity) == "function" then
            return bridge.getFrameParity()
        elseif bridge.frameParity ~= nil then
            return bridge.frameParity
        end
    end
    return 0
end

function VRCompanion:getIpd()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if type(bridge.getIpd) == "function" then
            return bridge.getIpd() or self.DEFAULT_IPD or 0.064
        elseif bridge.ipd ~= nil then
            return bridge.ipd
        end
    end
    return self.DEFAULT_IPD or 0.064
end

local function rotateVectorByQuaternion(qx, qy, qz, qw, vx, vy, vz)
    local tx = 2 * (qy * vz - qz * vy)
    local ty = 2 * (qz * vx - qx * vz)
    local tz = 2 * (qx * vy - qy * vx)
    local rx = vx + qw * tx + (qy * tz - qz * ty)
    local ry = vy + qw * ty + (qz * tx - qx * tz)
    local rz = vz + qw * tz + (qx * ty - qy * tx)
    return rx, ry, rz
end

function VRCompanion:requiresAfrFallback()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if bridge.requiresAfrFallback ~= nil then
            return bridge.requiresAfrFallback
        elseif type(bridge.getRequiresAfrFallback) == "function" then
            return bridge.getRequiresAfrFallback()
        elseif type(bridge.requiresAfrFallback) == "function" then
            return bridge.requiresAfrFallback()
        end
    end
    return true
end

function VRCompanion:computeEyeOffset(pose, eyeIndex, afrFallback)
    if not afrFallback or self.isMenuVisible then
        return 0, 0, 0
    end
    local ipd = self:getIpd()
    local halfIpd = ipd * 0.5
    local eyeOffsetX = (eyeIndex == 1) and halfIpd or -halfIpd
    local qx = (pose and pose.qx) or 0
    local qy = (pose and pose.qy) or 0
    local qz = (pose and pose.qz) or 0
    local qw = (pose and pose.qw) or 1
    return rotateVectorByQuaternion(qx, qy, qz, qw, eyeOffsetX, 0, 0)
end

function VRCompanion:normalizeAngle(rad)
    if rad == nil then return 0 end
    local mathUtilLimit = (MathUtil and MathUtil.getValidLimit) or (_G and _G.MathUtil and _G.MathUtil.getValidLimit)
    if type(mathUtilLimit) == "function" then
        return mathUtilLimit(rad)
    end
    while rad > math.pi do rad = rad - 2 * math.pi end
    while rad < -math.pi do rad = rad + 2 * math.pi end
    return rad
end

function VRCompanion:getVehicleLookYaw(cam)
    if not cam then return 0 end
    local lookYaw = 0
    if cam.rotY ~= nil and cam.origRotY ~= nil then
        lookYaw = cam.rotY - cam.origRotY
    elseif cam.rotY ~= nil then
        lookYaw = cam.rotY
    end
    return self:normalizeAngle(lookYaw)
end

function VRCompanion:rotateVectorByYaw(x, y, z, yaw)
    if yaw == nil or math.abs(yaw) < 0.000001 then
        return x or 0, y or 0, z or 0
    end
    local cosYaw = math.cos(yaw)
    local sinYaw = math.sin(yaw)
    local rx = (x or 0) * cosYaw + (z or 0) * sinYaw
    local ry = y or 0
    local rz = -(x or 0) * sinYaw + (z or 0) * cosYaw
    return rx, ry, rz
end

function VRCompanion:getResolvedHeadPose()
    local pose = nil
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.getHeadPose) == "function" then
        pose = bridge.getHeadPose()
    end

    -- Fallback to native engine TrackIR functions if VRBridge is not providing a valid pose
    if (not pose or not pose.isValid) and type(isHeadTrackingAvailable) == "function" and isHeadTrackingAvailable() then
        local tx, ty, tz = 0, 0, 0
        if type(getHeadTrackingTranslation) == "function" then
            tx, ty, tz = getHeadTrackingTranslation()
        end
        local pitch, yaw, roll = 0, 0, 0
        if type(getHeadTrackingRotation) == "function" then
            pitch, yaw, roll = getHeadTrackingRotation()
        end
        if pitch ~= nil then
            pose = {
                x = tx or 0,
                y = ty or 0,
                z = tz or 0,
                pitch = pitch or 0,
                yaw = yaw or 0,
                roll = roll or 0,
                isValid = true
            }
        end
    end

    if not pose or not pose.isValid then
        self.invalidPoseTicks = (self.invalidPoseTicks or 0) + 1
        if self.invalidPoseTicks == 1 or self.invalidPoseTicks % 300 == 0 then
            print(string.format("[FS25_VR] [HeadPose] Pose invalid or tracking lost (VRBridge present: %s, ticks: %d)",
                tostring(VRBridge ~= nil), self.invalidPoseTicks))
        end
    end

    return pose
end

function VRCompanion:getActiveEyeIndex(afrFallback)
    if afrFallback and not self.isMenuVisible then
        local parity = self:getFrameParity()
        return (parity % 2 == 1) and 1 or 0
    end
    return 0
end

function VRCompanion:onVehicleCameraUpdate(cam, dt)
    -- Only apply 6DOF tracking and projection to interior in-cab cameras
    if not cam or not cam.isInside or cam.cameraNode == nil then
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = self:getActiveEyeIndex(afrFallback)

    self:applyCameraProjection(cam.cameraNode, eyeIndex)

    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then
        return
    end

    local tx, ty, tz = pose.x or 0, pose.y or 0, pose.z or 0
    local pitch, yaw, roll = pose.pitch or 0, pose.yaw or 0, pose.roll or 0

    local eyeRotX, eyeRotY, eyeRotZ = self:computeEyeOffset(pose, eyeIndex, afrFallback)
    local eyeTx = tx + eyeRotX
    local eyeTy = ty + eyeRotY
    local eyeTz = tz + eyeRotZ

    local getParentFn = getParent or (_G and _G.getParent)
    local setRotationFn = setRotation or (_G and _G.setRotation)
    local getRotationFn = getRotation or (_G and _G.getRotation)
    local setTranslationFn = setTranslation or (_G and _G.setTranslation)
    local getTranslationFn = getTranslation or (_G and _G.getTranslation)
    local localToLocalFn = localToLocal or (_G and _G.localToLocal)
    local localRotationToLocalFn = localRotationToLocal or (_G and _G.localRotationToLocal)
    local createTransformGroupFn = createTransformGroup or (_G and _G.createTransformGroup)
    local localDirectionToLocalFn = localDirectionToLocal or (_G and _G.localDirectionToLocal)
    local linkFn = link or (_G and _G.link)
    local setDirectionFn = setDirection or (_G and _G.setDirection)

    local camParent = 0
    if type(getParentFn) == "function" then
        camParent = getParentFn(cam.cameraNode)
    end

    if cam.origRotY == nil then
        cam.origRotY = cam.rotY or 0
    end
    if cam.origRotX == nil then
        cam.origRotX = cam.rotX or 0
    end
    if cam.origRotZ == nil then
        cam.origRotZ = cam.rotZ or 0
    end

    -- Look yaw offset relative to neutral forward orientation (cam.rotY - cam.origRotY)
    local lookYaw = self:getVehicleLookYaw(cam)

    local curVeh = self:getControlledVehicle()
    if curVeh ~= nil and self.vehicleRotationStates ~= nil and self.vehicleRotationStates[curVeh] ~= nil then
        local activeCam = self:getVehicleActiveCamera(curVeh)
        if activeCam == cam or (activeCam and cam and activeCam.cameraNode == cam.cameraNode) then
            self.vehicleRotationStates[curVeh].rotY = cam.rotY
        end
    end

    -- Auto-construct headTrackingNode matching VehicleCamera:loadFromXML if missing
    if cam.headTrackingNode == nil and type(createTransformGroupFn) == "function" then
        local posNode = cam.cameraPositionNode or cam.cameraNode
        local pNode = (posNode and type(getParentFn) == "function") and getParentFn(posNode) or 0
        if pNode ~= nil and pNode ~= 0 then
            local lx, ly, lz = 0, 0, 0
            if type(localToLocalFn) == "function" then
                lx, ly, lz = localToLocalFn(posNode, pNode, 0, 0, 0)
            elseif type(getTranslationFn) == "function" then
                local ok, gx, gy, gz = pcall(getTranslationFn, posNode)
                if ok and gx ~= nil then lx, ly, lz = gx, gy, gz end
            end
            local tg = createTransformGroupFn("headTrackingNode")
            if tg ~= nil and tg ~= 0 then
                if type(linkFn) == "function" then
                    linkFn(pNode, tg)
                end
                if type(setTranslationFn) == "function" then
                    setTranslationFn(tg, lx, ly, lz)
                end
                local baseRotY = cam.origRotY or math.pi
                if type(setRotationFn) == "function" then
                    setRotationFn(tg, 0, baseRotY, 0)
                end
                cam.headTrackingNode = tg
                cam.origHeadTrackingRotX = 0
                cam.origHeadTrackingRotY = baseRotY
                cam.origHeadTrackingRotZ = 0
            end
        end
    end

    -- Update camera tracking node orientation with mouse/controller look yaw so head pose is composited cleanly on top.
    -- Vertical pitch remains locked level with the physical horizon (rx = 0.0) to eliminate vestibular conflict.
    local targetHeadRotY = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)
    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        if cam.origHeadTrackingRotY == nil or math.abs(cam.origHeadTrackingRotY) < 0.0001 then
            local baseRy = cam.origRotY or math.pi
            if type(getRotationFn) == "function" then
                local ok, rx, ry, rz = pcall(getRotationFn, cam.headTrackingNode)
                if ok and ry ~= nil and math.abs(ry) > 0.0001 then
                    baseRy = ry
                end
            end
            cam.origHeadTrackingRotX = 0
            cam.origHeadTrackingRotY = baseRy
            cam.origHeadTrackingRotZ = 0
        end

        targetHeadRotY = self:normalizeAngle((cam.origHeadTrackingRotY or cam.origRotY or math.pi) + lookYaw)

        if type(setRotationFn) == "function" then
            pcall(setRotationFn, cam.headTrackingNode, 0, targetHeadRotY, 0)
        end
    end

    local cabTransX, cabTransY, cabTransZ = eyeTx, eyeTy, eyeTz
    local cabRotX, cabRotY, cabRotZ = pitch, yaw, roll

    if cam.headTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocalFn) == "function" then
            cabTransX, cabTransY, cabTransZ = localToLocalFn(cam.headTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
        else
            local nodeTransX, nodeTransY, nodeTransZ = 0, 0, 0
            if type(getTranslationFn) == "function" then
                local ok, rawX, rawY, rawZ = pcall(getTranslationFn, cam.headTrackingNode)
                if ok and rawX ~= nil then nodeTransX, nodeTransY, nodeTransZ = rawX, rawY, rawZ end
            end
            local rotTx, rotTy, rotTz = self:rotateVectorByYaw(eyeTx, eyeTy, eyeTz, targetHeadRotY)
            cabTransX = nodeTransX + rotTx
            cabTransY = nodeTransY + rotTy
            cabTransZ = nodeTransZ + rotTz
        end

        if type(localRotationToLocalFn) == "function" then
            cabRotX, cabRotY, cabRotZ = localRotationToLocalFn(cam.headTrackingNode, camParent, pitch, yaw, roll)
        else
            local combYaw = self:normalizeAngle(targetHeadRotY + yaw)
            cabRotX = pitch
            cabRotY = combYaw
            cabRotZ = roll
        end
    else
        -- Fallback to driver seat anchor baseline when no dedicated headTrackingNode is assigned
        local baseX = cam.origTransX or cam.transX or 0
        local baseY = cam.origTransY or cam.transY or 0
        local baseZ = cam.origTransZ or cam.transZ or 0

        -- 6DOF physical head translations (leaning) rotate relative to current driver gaze angle in cab space (collinear translation)
        local totalGazeYaw = self:normalizeAngle((cam.origRotY or 0) + lookYaw)
        local rotTx, rotTy, rotTz = self:rotateVectorByYaw(eyeTx, eyeTy, eyeTz, totalGazeYaw)
        cabTransX = baseX + rotTx
        cabTransY = baseY + rotTy
        cabTransZ = baseZ + rotTz

        -- Combined head tracking rotation: total driver gaze yaw in cab + physical head yaw
        -- Vertical pitch is maintained level to the physical head horizon
        local combYaw = self:normalizeAngle(totalGazeYaw + yaw)

        cabRotX = pitch
        cabRotY = combYaw
        cabRotZ = roll
    end

    if type(setTranslationFn) == "function" then
        setTranslationFn(cam.cameraNode, cabTransX, cabTransY, cabTransZ)
    end
    if type(setRotationFn) == "function" then
        setRotationFn(cam.cameraNode, cabRotX, cabRotY, cabRotZ)
    end

    self.vehCamTicks = (self.vehCamTicks or 0) + 1
    if self.vehCamTicks == 1 or self.vehCamTicks % 300 == 0 then
        print(string.format("[FS25_VR] [VehicleCamera] Pose: (%.3f, %.3f, %.3f) Rot: (%.2f, %.2f, %.2f) Eye: %d AFR: %s",
            eyeTx, eyeTy, eyeTz, pitch, yaw, roll, eyeIndex, tostring(afrFallback)))
    end
end

function VRCompanion:onPlayerCameraMakeCurrent(cam)
    if not cam or cam.firstPersonCamera == nil then
        return
    end
    self:getPlayerBaseTranslation(cam)
end

function VRCompanion:onPlayerCameraUpdate(cam, dt)
    local activeCamNode = (cam and cam.firstPersonCamera) or (cam and cam.cameraNode)
    if not activeCamNode then
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = self:getActiveEyeIndex(afrFallback)

    self:applyCameraProjection(activeCamNode, eyeIndex)

    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then
        return
    end

    local tx, ty, tz = pose.x or 0, pose.y or 0, pose.z or 0
    local pitch, yaw, roll = pose.pitch or 0, pose.yaw or 0, pose.roll or 0

    local eyeRotX, eyeRotY, eyeRotZ = self:computeEyeOffset(pose, eyeIndex, afrFallback)
    local eyeTx = tx + eyeRotX
    local eyeTy = ty + eyeRotY
    local eyeTz = tz + eyeRotZ

    local camParent = 0
    if type(getParent) == "function" then
        camParent = getParent(activeCamNode)
    end

    if cam.vrHeadTrackingNode == nil and type(createTransformGroup) == "function" then
        local anchorNode = (cam.yawNode ~= nil and cam.yawNode ~= 0) and cam.yawNode or camParent
        if anchorNode ~= nil and anchorNode ~= 0 then
            local lx, ly, lz = 0, 0, 0
            if anchorNode == camParent and type(getTranslation) == "function" then
                lx, ly, lz = getTranslation(activeCamNode)
            end
            local tg = createTransformGroup("vrPlayerHeadTrackingNode")
            if tg ~= nil and tg ~= 0 then
                if type(link) == "function" then
                    link(anchorNode, tg)
                end
                if type(setTranslation) == "function" then
                    setTranslation(tg, lx, ly, lz)
                end
                if type(setRotation) == "function" then
                    setRotation(tg, 0, math.pi, 0)
                end
                cam.vrHeadTrackingNode = tg
            end
        end
    end

    local finalTx, finalTy, finalTz = eyeTx, eyeTy, eyeTz
    local finalRotX, finalRotY, finalRotZ = pitch, yaw, roll

    if cam.vrHeadTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            finalTx, finalTy, finalTz = localToLocal(cam.vrHeadTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
        end
        if type(localRotationToLocal) == "function" then
            finalRotX, finalRotY, finalRotZ = localRotationToLocal(cam.vrHeadTrackingNode, camParent, pitch, yaw, roll)
        end
    else
        local base = self:getPlayerBaseTranslation(cam)
        finalTx = (base.x or base[1] or 0) + eyeTx
        finalTy = (base.y or base[2] or 0) + eyeTy
        finalTz = (base.z or base[3] or 0) + eyeTz
        finalRotX = pitch
        finalRotY = yaw + math.pi
        finalRotZ = roll
    end

    if type(setTranslation) == "function" then
        setTranslation(activeCamNode, finalTx, finalTy, finalTz)
    end
    if type(setRotation) == "function" then
        setRotation(activeCamNode, finalRotX, finalRotY, finalRotZ)
    end

    self.playerCamTicks = (self.playerCamTicks or 0) + 1
    if self.playerCamTicks == 1 or self.playerCamTicks % 300 == 0 then
        print(string.format("[FS25_VR] [PlayerCamera] Pose: (%.3f, %.3f, %.3f) Rot: (%.2f, %.2f, %.2f) Eye: %d AFR: %s",
            eyeTx, eyeTy, eyeTz, pitch, yaw, roll, eyeIndex, tostring(afrFallback)))
    end
end

function VRCompanion:realignVehicleCameraToSeat(cam)
    if not cam or not cam.cameraNode then
        return
    end

    -- In-engine VR Recenter action resets cam.rotY back to cam.origRotY and re-centers the in-cab tracking reference frame
    if cam.origRotY ~= nil then
        cam.rotY = cam.origRotY
    end
    if cam.origRotX ~= nil then
        cam.rotX = cam.origRotX
    end
    if cam.origRotZ ~= nil then
        cam.rotZ = cam.origRotZ
    end

    local curVehicle = self:getControlledVehicle()
    if curVehicle ~= nil and self.vehicleRotationStates ~= nil and self.vehicleRotationStates[curVehicle] ~= nil then
        self.vehicleRotationStates[curVehicle].rotY = cam.rotY
        self.vehicleRotationStates[curVehicle].rotX = cam.rotX
    end

    local getParentFn = getParent or (_G and _G.getParent)
    local setRotationFn = setRotation or (_G and _G.setRotation)
    local getRotationFn = getRotation or (_G and _G.getRotation)
    local setTranslationFn = setTranslation or (_G and _G.setTranslation)
    local getTranslationFn = getTranslation or (_G and _G.getTranslation)
    local localToLocalFn = localToLocal or (_G and _G.localToLocal)
    local localRotationToLocalFn = localRotationToLocal or (_G and _G.localRotationToLocal)

    local camParent = 0
    if type(getParentFn) == "function" then
        camParent = getParentFn(cam.cameraNode)
    end

    local lookYaw = self:getVehicleLookYaw(cam)
    local targetHeadRotY = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)

    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        if cam.origHeadTrackingRotY == nil or math.abs(cam.origHeadTrackingRotY) < 0.0001 then
            local baseRy = cam.origRotY or math.pi
            if type(getRotationFn) == "function" then
                local ok, rx, ry, rz = pcall(getRotationFn, cam.headTrackingNode)
                if ok and ry ~= nil and math.abs(ry) > 0.0001 then
                    baseRy = ry
                end
            end
            cam.origHeadTrackingRotX = 0
            cam.origHeadTrackingRotY = baseRy
            cam.origHeadTrackingRotZ = 0
        end

        targetHeadRotY = self:normalizeAngle((cam.origHeadTrackingRotY or cam.origRotY or math.pi) + lookYaw)

        if type(setRotationFn) == "function" then
            pcall(setRotationFn, cam.headTrackingNode, cam.origHeadTrackingRotX or 0, targetHeadRotY, cam.origHeadTrackingRotZ or 0)
        end
    end

    local cabTransX, cabTransY, cabTransZ = 0, 0, 0
    local cabRotX, cabRotY, cabRotZ = 0, 0, 0

    if cam.headTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocalFn) == "function" then
            cabTransX, cabTransY, cabTransZ = localToLocalFn(cam.headTrackingNode, camParent, 0, 0, 0)
        else
            if type(getTranslationFn) == "function" then
                local ok, gx, gy, gz = pcall(getTranslationFn, cam.headTrackingNode)
                if ok and gx ~= nil then cabTransX, cabTransY, cabTransZ = gx, gy, gz end
            end
        end
        if type(localRotationToLocalFn) == "function" then
            cabRotX, cabRotY, cabRotZ = localRotationToLocalFn(cam.headTrackingNode, camParent, 0, 0, 0)
        else
            cabRotX = 0
            cabRotY = targetHeadRotY
            cabRotZ = 0
        end
    else
        cabTransX = cam.origTransX or cam.transX or 0
        cabTransY = cam.origTransY or cam.transY or 0
        cabTransZ = cam.origTransZ or cam.transZ or 0

        local combYaw = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)

        cabRotX = 0
        cabRotY = combYaw
        cabRotZ = 0
    end

    if type(setTranslationFn) == "function" then
        setTranslationFn(cam.cameraNode, cabTransX, cabTransY, cabTransZ)
    end
    if type(setRotationFn) == "function" then
        setRotationFn(cam.cameraNode, cabRotX, cabRotY, cabRotZ)
    end
end

function VRCompanion:realignPlayerCameraToRoot(cam)
    local activeCamNode = (cam and cam.firstPersonCamera) or (cam and cam.cameraNode)
    if not activeCamNode then
        return
    end

    local camParent = 0
    if type(getParent) == "function" then
        camParent = getParent(activeCamNode)
    end

    local finalTx, finalTy, finalTz = 0, 0, 0
    local finalRotX, finalRotY, finalRotZ = 0, 0, 0

    if cam.vrHeadTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            finalTx, finalTy, finalTz = localToLocal(cam.vrHeadTrackingNode, camParent, 0, 0, 0)
        end
        if type(localRotationToLocal) == "function" then
            finalRotX, finalRotY, finalRotZ = localRotationToLocal(cam.vrHeadTrackingNode, camParent, 0, 0, 0)
        end
    else
        local base = self:getPlayerBaseTranslation(cam)
        finalTx = base.x or base[1] or 0
        finalTy = base.y or base[2] or 0
        finalTz = base.z or base[3] or 0
        finalRotY = math.pi
    end

    if type(setTranslation) == "function" then
        setTranslation(activeCamNode, finalTx, finalTy, finalTz)
    end
    if type(setRotation) == "function" then
        setRotation(activeCamNode, finalRotX, finalRotY, finalRotZ)
    end
end

function VRCompanion:updateMenuBackgroundCamera(dt)
    if not self.isMenuVisible then
        return
    end

    local camNode = nil
    if type(getCamera) == "function" then
        local ok, node = pcall(getCamera)
        if ok and node ~= nil and node ~= 0 then
            camNode = node
        end
    end

    if camNode == nil or camNode == 0 then
        return
    end

    if self.menuCameraOffsets == nil then
        self.menuCameraOffsets = {}
    end

    -- Retrieve head pose from VRBridge or TrackIR
    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then
        return
    end

    local tx = pose.x or 0
    local ty = pose.y or 0
    local tz = pose.z or 0
    local pitch = pose.pitch or 0
    local yaw = pose.yaw or 0
    local roll = pose.roll or 0

    local lastOffset = self.menuCameraOffsets[camNode] or { tx = 0, ty = 0, tz = 0, rx = 0, ry = 0, rz = 0 }

    local curX, curY, curZ = 0, 0, 0
    if type(getTranslation) == "function" then
        local ok, x, y, z = pcall(getTranslation, camNode)
        if ok and x ~= nil then curX, curY, curZ = x, y, z end
    end

    local curRx, curRy, curRz = 0, 0, 0
    if type(getRotation) == "function" then
        local ok, rx, ry, rz = pcall(getRotation, camNode)
        if ok and rx ~= nil then curRx, curRy, curRz = rx, ry, rz end
    end

    -- Subtract previous VR offset to recover underlying base transform (preserving mouse/controller orbit deltas)
    local baseX = curX - lastOffset.tx
    local baseY = curY - lastOffset.ty
    local baseZ = curZ - lastOffset.tz
    local baseRx = curRx - lastOffset.rx
    local baseRy = curRy - lastOffset.ry
    local baseRz = curRz - lastOffset.rz

    local finalTx = baseX + tx
    local finalTy = baseY + ty
    local finalTz = baseZ + tz
    local finalRotX = baseRx + pitch
    local finalRotY = baseRy + yaw
    local finalRotZ = baseRz + roll

    self.menuCameraOffsets[camNode] = {
        tx = tx, ty = ty, tz = tz,
        rx = pitch, ry = yaw, rz = roll
    }

    if type(setTranslation) == "function" then
        pcall(setTranslation, camNode, finalTx, finalTy, finalTz)
    end
    if type(setRotation) == "function" then
        pcall(setRotation, camNode, finalRotX, finalRotY, finalRotZ)
    end

    self.menuCamTicks = (self.menuCamTicks or 0) + 1
    if self.menuCamTicks == 1 or self.menuCamTicks % 300 == 0 then
        print(string.format("[FS25_VR] [MenuCamera] Active camera 6DOF tracking: Node %s, Trans: (%.3f, %.3f, %.3f), Rot: (%.2f, %.2f, %.2f)",
            tostring(camNode), finalTx, finalTy, finalTz, finalRotX, finalRotY, finalRotZ))
    end
end

function VRCompanion:restoreMenuBackgroundCamera()
    if self.menuCameraOffsets ~= nil then
        for camNode, offset in pairs(self.menuCameraOffsets) do
            local exists = true
            if type(entityExists) == "function" then
                exists = entityExists(camNode)
            end
            if exists and offset ~= nil then
                local curX, curY, curZ = 0, 0, 0
                if type(getTranslation) == "function" then
                    local ok, x, y, z = pcall(getTranslation, camNode)
                    if ok and x ~= nil then curX, curY, curZ = x, y, z end
                end
                local curRx, curRy, curRz = 0, 0, 0
                if type(getRotation) == "function" then
                    local ok, rx, ry, rz = pcall(getRotation, camNode)
                    if ok and rx ~= nil then curRx, curRy, curRz = rx, ry, rz end
                end
                if type(setTranslation) == "function" then
                    pcall(setTranslation, camNode, curX - offset.tx, curY - offset.ty, curZ - offset.tz)
                end
                if type(setRotation) == "function" then
                    pcall(setRotation, camNode, curRx - offset.rx, curRy - offset.ry, curRz - offset.rz)
                end
            end
        end
        self.menuCameraOffsets = {}
        self.menuCamTicks = 0
    end
end

function VRCompanion:onRecenterAction()
    print("[FS25_VR] Recenter Action triggered.")
    local recenterTracking = (self.recenterVrTrackingSpace ~= false)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if recenterTracking then
        if bridge ~= nil and type(bridge.recenter) == "function" then
            bridge.recenter()
        end
        if bridge ~= nil and type(bridge.resetTemporalHistory) == "function" then
            bridge.resetTemporalHistory()
        end
        if self.isMenuVisible then
            print("[FS25_VR] Menu Quad layer realigned directly in front of head pose.")
        end
    else
        print("[FS25_VR] VehicleOnly mode: OpenXR tracking space recenter bypassed.")
    end

    -- Realign vehicle cockpit driver seat origin if seated in a vehicle
    local vehCam = self:getActiveVehicleCamera()
    if vehCam and vehCam.isInside then
        if recenterTracking then
            self:realignVehicleCameraToSeat(vehCam)
            print("[FS25_VR] Cockpit head pose realigned to driver seat reference origin.")
        else
            -- In VehicleOnly mode, reset only in-cab mouse/controller look yaw (cam.rotY = cam.origRotY)
            if vehCam.origRotY ~= nil then
                vehCam.rotY = vehCam.origRotY
            end
            local curVehicle = self:getControlledVehicle()
            if curVehicle ~= nil and self.vehicleRotationStates ~= nil and self.vehicleRotationStates[curVehicle] ~= nil then
                self.vehicleRotationStates[curVehicle].rotY = vehCam.rotY
            end
            print("[FS25_VR] VehicleOnly mode: Cockpit vehicle camera look yaw reset to origRotY.")
        end
    end

    -- Realign on-foot player camera relative to character root
    if recenterTracking then
        local playerCam = self:getPlayerCamera()
        if playerCam and playerCam.firstPersonCamera ~= nil then
            self:realignPlayerCameraToRoot(playerCam)
            print("[FS25_VR] On-foot head pose realigned to character root reference origin.")
        end
    end

    print(string.format("[FS25_VR] Recenter Action complete (%s mode).", recenterTracking and "Both" or "VehicleOnly"))
end

function VRCompanion:update(dt)
    self.frameTicks = (self.frameTicks or 0) + 1

    -- Poll in-game GUI visibility and synchronize with native VRBridge
    self:pollGuiVisibility()

    if self.isMenuVisible then
        self:updateMenuBackgroundCamera(dt)
    end

    if not self.vehicleCameraHooked then
        self:hookVehicleCamera()
    end
    if not self.playerCameraHooked then
        self:hookPlayerCamera()
    end
    if not self.enterableHooked then
        self:hookEnterable()
    end
    if not self.hudHooked then
        self:hookHud()
    end

    -- Update physical IPD-scaled HUD convergence parity across alternating AFR frames
    self:updateHudConvergence(dt)

    -- Keep in-engine action events active across context transitions
    if (self.frameTicks % 30 == 0) or (self.hudToggleActionEventId == nil) then
        self:registerActionEvents(false)
    end

    -- Check for vehicle transitions (entering/exiting/switching vehicle)
    local curVehicle = self:getControlledVehicle()
    if self.lastControlledVehicle ~= curVehicle then
        local oldVeh = self.lastControlledVehicle
        self.lastControlledVehicle = curVehicle
        self:onVehicleTransition(oldVeh, curVehicle)
    end

    if (self.lastReportedVerdict == nil or self.lastReportedVerdict == "Inconclusive") and self.frameTicks < 300 and VRBridge ~= nil and type(VRBridge.getStereoTelemetry) == "function" then
        local telemetry = VRBridge.getStereoTelemetry()
        if telemetry then
            if telemetry.verdict ~= self.lastReportedVerdict then
                self.lastReportedVerdict = telemetry.verdict
                print(string.format("[FS25_VR] Telemetry Probe Resolution: %s (Max Viewports: %d, AFR Fallback: %s)",
                    tostring(telemetry.verdict),
                    telemetry.maxViewports or 0,
                    tostring(telemetry.afrFallback)))
            elseif self.frameTicks == 1 then
                print(string.format("[FS25_VR] Telemetry Probe active (Frame: %d, Passes: %d, Verdict: %s)",
                    telemetry.frameIndex or 0,
                    telemetry.passCount or 0,
                    tostring(telemetry.verdict or "unknown")))
            end
        end
    end
end

function VRCompanion:deleteMap()
    print("[FS25_VR] Unloading Companion Mod from map.")

    if self.stereoToggled and type(setStereoRendering) == "function" then
        setStereoRendering(false)
        print("[FS25_VR] Restored engine stereo rendering toggle: setStereoRendering(false)")
        self.stereoToggled = false
    end

    -- Unhook hooked camera functions cleanly to prevent accumulating wrapper layers
    if self.origVehicleCameraUpdate ~= nil and VehicleCamera ~= nil then
        VehicleCamera.update = self.origVehicleCameraUpdate
        self.origVehicleCameraUpdate = nil
    end
    if self.origPlayerCameraMakeCurrent ~= nil and PlayerCamera ~= nil then
        PlayerCamera.makeCurrent = self.origPlayerCameraMakeCurrent
        self.origPlayerCameraMakeCurrent = nil
    end
    if self.origPlayerCameraUpdatePosition ~= nil and PlayerCamera ~= nil then
        PlayerCamera.updatePosition = self.origPlayerCameraUpdatePosition
        self.origPlayerCameraUpdatePosition = nil
    end
    if self.origPlayerCameraUpdateRotation ~= nil and PlayerCamera ~= nil then
        PlayerCamera.updateRotation = self.origPlayerCameraUpdateRotation
        self.origPlayerCameraUpdateRotation = nil
    end
    if self.origPlayerCameraUpdate ~= nil and PlayerCamera ~= nil then
        PlayerCamera.update = self.origPlayerCameraUpdate
        self.origPlayerCameraUpdate = nil
    end
    if self.origEnterableOnEnterVehicle ~= nil and Enterable ~= nil then
        Enterable.onEnterVehicle = self.origEnterableOnEnterVehicle
        self.origEnterableOnEnterVehicle = nil
    end
    if self.origEnterableOnLeaveVehicle ~= nil and Enterable ~= nil then
        Enterable.onLeaveVehicle = self.origEnterableOnLeaveVehicle
        self.origEnterableOnLeaveVehicle = nil
    end
    if self.origEnterableSetActiveCameraIndex ~= nil and Enterable ~= nil then
        Enterable.setActiveCameraIndex = self.origEnterableSetActiveCameraIndex
        self.origEnterableSetActiveCameraIndex = nil
    end

    -- Unhook HUD display hooks cleanly
    self:unhookHud()

    self.handshakeComplete = false
    self.lastReportedVerdict = nil
    self.frameTicks = 0
    self.vehicleCameraHooked = false
    self.playerCameraHooked = false
    self.enterableHooked = false
    self.lastControlledVehicle = nil
    self.vehicleRotationStates = {}
    self.isMenuVisible = false
    self.guiVisibilityInitialized = false
    self:restoreMenuBackgroundCamera()

    if VRBridge ~= nil and type(VRBridge.setMenuVisible) == "function" then
        VRBridge.setMenuVisible(false)
    end

    if VRBridge ~= nil and type(VRBridge.onMapUnload) == "function" then
        VRBridge.onMapUnload()
    end

    -- Restore HUD state and visibility to standard 2D Full (Issue #25)
    self:setHudState(self.HUD_STATE_FULL)

    -- Restore HUD insets to 2D defaults
    self:restoreHudInsets()

    self.currentConvergenceOffset = 0.0
    self.currentConvergenceDisparity = 0.0
    self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
    self.activeFillLevelsDisplay = nil
    self.passThroughMode = nil
    self.headTrackingEnabled = true

    -- Restore original camera FOVs and projection offsets
    if self.origCamSettings ~= nil then
        for camNode, orig in pairs(self.origCamSettings) do
            local exists = true
            if type(entityExists) == "function" then
                exists = entityExists(camNode)
            end
            if exists then
                if orig.fovY ~= nil and type(setFovY) == "function" then
                    pcall(setFovY, camNode, orig.fovY)
                end
                if type(setProjectionOffset) == "function" then
                    pcall(setProjectionOffset, camNode, orig.offsetX or 0, orig.offsetY or 0)
                end
            end
        end
        self.origCamSettings = {}
    end

    -- Remove console commands
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrHudInsets")
        removeConsoleCommand("vrHudState")
        removeConsoleCommand("vrHudFillUnderVehicle")
        removeConsoleCommand("vrRecenterMode")
        removeConsoleCommand("vrRecenterTracking")
    end
end

function VRCompanion:keyEvent(unicode, sym, modifier, isDown)
    if not isDown then return end

    -- Direct hardware shortcut for VR_HUD_TOGGLE (Ctrl + H)
    local isH = false
    if Input ~= nil and Input.KEY_h ~= nil then
        isH = (sym == Input.KEY_h)
    else
        isH = (sym == 104 or unicode == 104 or unicode == 72)
    end

    if isH then
        local ctrlHeld = false
        if Input ~= nil and type(Input.isKeyPressed) == "function" then
            ctrlHeld = (Input.KEY_rctrl ~= nil and Input.isKeyPressed(Input.KEY_rctrl))
                    or (Input.KEY_lctrl ~= nil and Input.isKeyPressed(Input.KEY_lctrl))
        end
        if not ctrlHeld and modifier ~= nil and modifier > 0 then
            local band = (bit32 and bit32.band) or (bit and bit.band) or function(a, b) return 0 end
            local ctrlMask = (Input and (Input.MOD_RCTRL or Input.MOD_LCTRL or Input.MOD_CTRL)) or 0
            if ctrlMask > 0 and band(modifier, ctrlMask) > 0 then
                ctrlHeld = true
            elseif modifier == 4 or modifier == 32 or modifier == 64 or modifier == 96 then
                ctrlHeld = true
            end
        end
        if ctrlHeld then
            local curTime = g_time
            if curTime == nil or self.lastHudToggleTime == nil or (curTime - self.lastHudToggleTime) >= 300 then
                print("[FS25_VR] Hardware keyEvent: Ctrl + H detected -> cycling HUD state.")
                self:onHudToggleAction()
            end
            return
        end
    end

    -- Direct hardware shortcut for VR_RECENTER (Numpad 5)
    local isRecenter = false
    if Input ~= nil and (Input.KEY_kp_5 ~= nil or Input.KEY_numPad5 ~= nil) then
        isRecenter = (sym == Input.KEY_kp_5 or sym == Input.KEY_numPad5)
    else
        isRecenter = (sym == 261 or unicode == 53)
    end
    if isRecenter then
        print("[FS25_VR] Hardware keyEvent: Numpad 5 detected -> recentering VR view.")
        self:onRecenterAction()
        return
    end
end

function VRCompanion:consoleCommandSetHudInsets(insetXStr, insetYStr, scaleStr, topInsetStr)
    local ix = tonumber(insetXStr)
    local iy = tonumber(insetYStr)
    local sc = tonumber(scaleStr)
    local it = tonumber(topInsetStr)
    if ix == nil or iy == nil then
        return string.format("Usage: vrHudInsets <insetX> <insetY> [scale] [topInset] (current: insetX=%.2f, insetY=%.2f, topInset=%.2f, scale=%.2f)",
            self.hudInsetX or self.DEFAULT_HUD_INSET_X,
            self.hudInsetY or self.DEFAULT_HUD_INSET_Y,
            self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y,
            self.hudUiScaleMultiplier or self.DEFAULT_HUD_UI_SCALE)
    end
    self:configureHudInsets(ix, iy, sc or self.hudUiScaleMultiplier, it or self.hudInsetTop or iy)
    self:applyHudInsets()
    self:saveConfiguration()
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD Insets: X=%.2f, Y=%.2f, Top=%.2f, Scale=%.2fx (Saved)",
                self.hudInsetX, self.hudInsetY, self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y, self.effectiveUiScale), 2500)
        end)
    end
    return string.format("VR HUD insets updated and saved: Left=%.2f, Right=%.2f, Bottom=%.2f, Top=%.2f, Scale=%.2fx",
        self.hudAnchorLeft, self.hudAnchorRight, self.hudAnchorBottom, self.hudAnchorTop, self.effectiveUiScale)
end

function VRCompanion:consoleCommandSetHudTopInset(topInsetStr)
    local topVal = tonumber(topInsetStr)
    if topVal == nil then
        return string.format("Usage: vrHudTopInset <topInset> (0.0 to 0.50, current: %.2f, topAnchor: %.2f)",
            self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y,
            self.hudAnchorTop or (1.0 - (self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_Y)))
    end
    local clampedTop = self:clamp(topVal, self.MIN_HUD_INSET_TOP, self.MAX_HUD_INSET_TOP)
    self.hudInsetTop = clampedTop
    self:configureHudInsets(self.hudInsetX, self.hudInsetY, self.hudUiScaleMultiplier, clampedTop)
    self:applyHudInsets()
    self:saveConfiguration()
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD Top Inset: %.2f (Top Anchor: %.2f, Saved)",
                clampedTop, self.hudAnchorTop), 2500)
        end)
    end
    return string.format("VR HUD top inset set to: %.2f (top anchor: %.2f)", clampedTop, self.hudAnchorTop)
end

function VRCompanion:consoleCommandSetHudState(stateStr)
    local st = tonumber(stateStr)
    if st == nil or st < 0 or st > 2 then
        return "Usage: vrHudState <0=Hidden | 1=Streamlined | 2=Full>"
    end
    self:setHudState(st)
    local stateName = self.HUD_STATE_NAMES[st] or tostring(st)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: %s (%d)", stateName, st), 2500)
        end)
    end
    return string.format("VR HUD state set to: %s (%d)", stateName, st)
end

function VRCompanion:consoleCommandSetHudFillUnderVehicle(enableStr)
    local enable = nil
    if enableStr ~= nil then
        local lower = string.lower(tostring(enableStr))
        if lower == "true" or lower == "1" or lower == "on" then
            enable = true
        elseif lower == "false" or lower == "0" or lower == "off" then
            enable = false
        end
    end
    if enable == nil then
        return string.format("Usage: vrHudFillUnderVehicle <true|false> (current: %s)",
            tostring(self.fillLevelUnderVehicle))
    end
    self.fillLevelUnderVehicle = enable
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local fl = self:getFillLevelsDisplay(hud)
    if fl ~= nil then
        self:positionHudDisplay(fl, "FillLevelsDisplay")
    end
    self:saveConfiguration()
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: Fill Level Under Vehicle: %s (Saved)",
                enable and "Enabled" or "Disabled"), 2500)
        end)
    end
    return string.format("VR HUD fill level under vehicle set to: %s", tostring(enable))
end

function VRCompanion:consoleCommandSetHudFillGap(gapStr)
    local gapVal = tonumber(gapStr)
    if gapVal == nil then
        return string.format("Usage: vrHudFillGap <gap> (0.0 to 0.05, current: %.4f)",
            self.hudFillLevelGap or self.DEFAULT_HUD_FILL_LEVEL_GAP)
    end
    self.hudFillLevelGap = self:clamp(gapVal, 0.0, 0.05)
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local fl = self:getFillLevelsDisplay(hud)
    if fl ~= nil then
        self:positionHudDisplay(fl, "FillLevelsDisplay")
    end
    self:saveConfiguration()
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: Fill Level Gap: %.4f (Saved)", self.hudFillLevelGap), 2500)
        end)
    end
    return string.format("VR HUD fill level gap set to: %.4f", self.hudFillLevelGap)
end

function VRCompanion:consoleCommandSetHudLoadNotification(enabledStr)
    if enabledStr == nil or enabledStr == "" then
        return string.format("Usage: vrHudLoadNotification <true|false> (current: %s)", tostring(self.showHudLoadNotification == true))
    end
    local lower = string.lower(tostring(enabledStr))
    local enabled = (lower == "true" or lower == "1" or lower == "on")
    self.showHudLoadNotification = enabled
    self:saveConfiguration()
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: Load Notification: %s (Saved)",
                enabled and "Enabled" or "Disabled"), 2500)
        end)
    end
    return string.format("VR HUD load notification set to: %s", tostring(enabled))
end

function VRCompanion:consoleCommandSetRecenterMode(modeStr)
    local curMode = self:getRecenterMode()
    if modeStr == nil or modeStr == "" or string.lower(tostring(modeStr)) == "status" then
        return string.format("Current VR recenter mode: %s (%s). Usage: vrRecenterMode <Both | VehicleOnly | toggle>",
            curMode,
            (curMode == "Both") and "resets vehicle camera & VR tracking space" or "resets vehicle camera only")
    end

    local newMode, err = self:setRecenterMode(modeStr)
    if not newMode then
        return err
    end

    local modeDesc = (newMode == "Both") and "resets vehicle camera & VR tracking space" or "resets vehicle camera only"
    print(string.format("[FS25_VR] VR recenter mode set to: %s (%s)", newMode, modeDesc))
    return string.format("VR recenter mode set to: %s (%s)", newMode, modeDesc)
end

-- Register as an engine mod event listener if in-game environment
if addModEventListener ~= nil then
    addModEventListener(VRCompanion)
end
