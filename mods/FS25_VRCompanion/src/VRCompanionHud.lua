-- ====================================================================
-- FS25 VR Companion Mod: HUD Subsystem
-- Manages in-engine VR HUD insets, UI scaling, stereoscopic convergence,
-- three-stage visibility state machine, and fill-level element repositioning.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Hud = VRCompanion.Hud or {}

-- VR HUD Three-Stage State Machine Constants
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

-- VR HUD runtime state defaults (initialized only when not already set)
if VRCompanion.hudState == nil then VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN end
if VRCompanion.hudInsetX == nil then VRCompanion.hudInsetX = VRCompanion.DEFAULT_HUD_INSET_X end
if VRCompanion.hudInsetY == nil then VRCompanion.hudInsetY = VRCompanion.DEFAULT_HUD_INSET_Y end
if VRCompanion.hudInsetTop == nil then VRCompanion.hudInsetTop = VRCompanion.DEFAULT_HUD_INSET_TOP end
if VRCompanion.hudUiScaleMultiplier == nil then VRCompanion.hudUiScaleMultiplier = VRCompanion.DEFAULT_HUD_UI_SCALE end
if VRCompanion.hudConvergenceDepth == nil then VRCompanion.hudConvergenceDepth = VRCompanion.DEFAULT_CONVERGENCE_DEPTH end
if VRCompanion.hudAnchorLeftBase == nil then VRCompanion.hudAnchorLeftBase = VRCompanion.DEFAULT_HUD_INSET_X end
if VRCompanion.hudAnchorRightBase == nil then VRCompanion.hudAnchorRightBase = 1.0 - VRCompanion.DEFAULT_HUD_INSET_X end
if VRCompanion.hudAnchorBottomBase == nil then VRCompanion.hudAnchorBottomBase = VRCompanion.DEFAULT_HUD_INSET_Y end
if VRCompanion.hudAnchorTopBase == nil then VRCompanion.hudAnchorTopBase = 1.0 - VRCompanion.DEFAULT_HUD_INSET_TOP end
if VRCompanion.hudAnchorLeft == nil then VRCompanion.hudAnchorLeft = VRCompanion.hudAnchorLeftBase end
if VRCompanion.hudAnchorRight == nil then VRCompanion.hudAnchorRight = VRCompanion.hudAnchorRightBase end
if VRCompanion.hudAnchorBottom == nil then VRCompanion.hudAnchorBottom = VRCompanion.hudAnchorBottomBase end
if VRCompanion.hudAnchorTop == nil then VRCompanion.hudAnchorTop = VRCompanion.hudAnchorTopBase end
if VRCompanion.effectiveUiScale == nil then VRCompanion.effectiveUiScale = VRCompanion.DEFAULT_HUD_UI_SCALE end
if VRCompanion.currentConvergenceOffset == nil then VRCompanion.currentConvergenceOffset = 0.0 end
local HUD_DISPLAY_ANCHORS = {
    SpeedMeterDisplay = { alignX = "right", alignY = "bottom" },
    InputHelpDisplay = { alignX = "left", alignY = "top" },
    GameInfoDisplay = { alignX = "right", alignY = "top" },
    ContextActionDisplay = { alignX = "left", alignY = "bottom" },
    IngameMap = { alignX = "left", alignY = "bottom" },
    FillLevelsDisplay = { alignX = "right", alignY = "bottom" }
}

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

local function setDisplayVisible(display, isVisible)
    if display == nil then return end
    display.isVisible = isVisible
    if type(display.setIsVisible) == "function" then
        pcall(function() display:setIsVisible(isVisible) end)
    elseif type(display.setVisible) == "function" then
        pcall(function() display:setVisible(isVisible) end)
    end
end

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
        if self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then
            targetParity = self:getSubPassIndex() % 2
        else
            targetParity = self:getFrameParity()
        end
    end

    local targetDisparity = disparity
    if targetDisparity == nil then
        targetDisparity = self:getConvergenceDisparity()
    end

    local sign = (targetParity % 2 == 1) and -1.0 or 1.0
    return sign * targetDisparity
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
    local uiScale = self.effectiveUiScale or self.DEFAULT_HUD_UI_SCALE

    local flHeight = nil
    if type(display.getHeight) == "function" then
        local ok, h = pcall(display.getHeight, display)
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

    local flWidth = nil
    if type(display.getWidth) == "function" then
        local ok, w = pcall(display.getWidth, display)
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

    local numLevels = 1
    if display.fillLevelData ~= nil and type(display.fillLevelData) == "table" and #display.fillLevelData > 0 then
        numLevels = #display.fillLevelData
    elseif display.vehicle ~= nil and type(display.vehicle.getFillUnits) == "function" then
        local ok, units = pcall(display.vehicle.getFillUnits, display.vehicle)
        if ok and type(units) == "table" and #units > 0 then
            numLevels = math.max(1, #units)
        end
    end
    local rowGap = 0.004 * uiScale
    local totalFlHeight = numLevels * flHeight + math.max(0, numLevels - 1) * rowGap

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
            pcall(display.setScale, display, uiScale)
        end
    end

    if type(display.setPosition) == "function" then
        pcall(display.setPosition, display, flPosX, fillBottom)
    end
    if type(display.storeOriginalPosition) == "function" then
        pcall(display.storeOriginalPosition, display)
    end
    if type(display.reposition) == "function" then
        pcall(display.reposition, display)
    end
    if type(display.updatePosition) == "function" then
        pcall(display.updatePosition, display)
    end

    if display.bgLeft ~= nil and type(display.bgLeft.setPosition) == "function" then
        pcall(display.bgLeft.setPosition, display.bgLeft, flPosX, fillBottom)
    end
    local bgScaleX = flPosX + (display.bgLeft and display.bgLeft.width or 0)
    if display.bgScale ~= nil and type(display.bgScale.setPosition) == "function" then
        pcall(display.bgScale.setPosition, display.bgScale, bgScaleX, fillBottom)
    end
    local bgRightX = bgScaleX + (display.bgScale and display.bgScale.width or 0)
    if display.bgRight ~= nil and type(display.bgRight.setPosition) == "function" then
        pcall(display.bgRight.setPosition, display.bgRight, bgRightX, fillBottom)
    end
    if display.bar ~= nil and type(display.bar.setPosition) == "function" then
        local barX = flPosX + (display.barOffsetX or (display.bgLeft and display.bgLeft.width or 0))
        local barY = fillBottom + (display.barOffsetY or 0)
        pcall(display.bar.setPosition, display.bar, barX, barY)
    end
    if display.maxWeightIcon ~= nil and type(display.maxWeightIcon.setPosition) == "function" then
        local mwX = flPosX + (display.maxWeightIconOffsetX or 0)
        local mwY = fillBottom + (display.maxWeightIconOffsetY or 0)
        pcall(display.maxWeightIcon.setPosition, display.maxWeightIcon, mwX, mwY)
    end
    if type(display.fillTypeIcons) == "table" then
        for _, icon in pairs(display.fillTypeIcons) do
            if icon ~= nil and type(icon.setPosition) == "function" then
                local icX = flPosX + (display.fillTypeIconOffsetX or 0)
                local icY = fillBottom + (display.fillTypeIconOffsetY or 0)
                pcall(icon.setPosition, icon, icX, icY)
            end
        end
    end

    if sm ~= nil and targetSmBottom ~= baseBottom and type(sm.setPosition) == "function" then
        pcall(sm.setPosition, sm, shiftedRight, targetSmBottom)
    end

    return flPosX, fillBottom, targetSmBottom
end

function VRCompanion:updateHudConvergence(dt)
    if self.is2DViewActive then
        self.currentConvergenceOffset = 0.0
        self.currentConvergenceDisparity = 0.0

        self.hudAnchorLeft = 0.0
        self.hudAnchorRight = 1.0
        self.hudAnchorBottom = 0.0
        self.hudAnchorTop = 1.0

        _G.g_hudAnchorLeft = 0.0
        _G.g_hudAnchorRight = 1.0
        _G.g_hudAnchorBottom = 0.0
        _G.g_hudAnchorTop = 1.0
        return
    end

    local isPassThrough = self:isPassThroughMode()
    local isTrackingActive = self:isHeadTrackingActive()

    local targetDisparity = 0.0
    if not isPassThrough and isTrackingActive and not self.isMenuVisible and not self.is2DViewActive then
        local rawIpd = self:getIpd()
        targetDisparity = self:computeConvergenceDisparity(rawIpd, self.hudConvergenceDepth)
    end

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

    local eyeOrParity = nil
    if self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then
        eyeOrParity = self:getSubPassIndex() % 2
    else
        eyeOrParity = self:getFrameParity()
    end
    local offset = self:getConvergenceOffset(eyeOrParity, self.currentConvergenceDisparity)

    self.currentConvergenceOffset = offset

    local baseLeft = self.hudAnchorLeftBase or self.DEFAULT_HUD_INSET_X
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_INSET_X)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_INSET_Y
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_INSET_TOP)

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
            pcall(sm.setPosition, sm, shiftedRight, targetSmBottom)
        end
        local ih = hud.inputHelp or hud.inputHelpDisplay
        if ih ~= nil and type(ih.setPosition) == "function" then
            pcall(ih.setPosition, ih, shiftedLeft, baseTop)
        end
        local gi = hud.gameInfo or hud.gameInfoDisplay
        if gi ~= nil and type(gi.setPosition) == "function" then
            pcall(gi.setPosition, gi, shiftedRight, baseTop)
        end
        local ca = hud.contextAction or hud.contextActionDisplay
        if ca ~= nil and type(ca.setPosition) == "function" then
            pcall(ca.setPosition, ca, 0.5 + offset, baseBottom)
        end
        local im = hud.ingameMap or hud.ingameMapDisplay
        if im ~= nil and type(im.setPosition) == "function" then
            pcall(im.setPosition, im, shiftedLeft, baseBottom)
        end
    end

    return shiftedLeft, shiftedRight, offset
end

function VRCompanion:getHudState()
    return self.hudState or self.HUD_STATE_HIDDEN
end

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

function VRCompanion:calculateHudAnchors(insetX, insetY, scaleMultiplier, insetTop)
    local clampedInsetX = self:clamp(insetX or self.DEFAULT_HUD_INSET_X, self.MIN_HUD_INSET_X, self.MAX_HUD_INSET_X)
    local clampedInsetY = self:clamp(insetY or self.DEFAULT_HUD_INSET_Y, self.MIN_HUD_INSET_Y, self.MAX_HUD_INSET_Y)
    -- No top inset: the top edge follows an explicitly given vertical inset, else DEFAULT_HUD_INSET_TOP
    local rawInsetTop = insetTop
    if rawInsetTop == nil then
        rawInsetTop = (insetY ~= nil) and clampedInsetY or self.DEFAULT_HUD_INSET_TOP
    end
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
    elseif insetY ~= nil then
        self.hudInsetTop = nil -- top edge follows the vertical inset
    else
        self.hudInsetTop = self.DEFAULT_HUD_INSET_TOP
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
        top = self.hudAnchorTop or (1.0 - self.DEFAULT_HUD_INSET_TOP),
        baseLeft = self.hudAnchorLeftBase or self.hudAnchorLeft or self.DEFAULT_HUD_INSET_X,
        baseRight = self.hudAnchorRightBase or self.hudAnchorRight or (1.0 - self.DEFAULT_HUD_INSET_X),
        insetX = self.hudInsetX or self.DEFAULT_HUD_INSET_X,
        insetY = self.hudInsetY or self.DEFAULT_HUD_INSET_Y,
        insetTop = self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_TOP,
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
    if self.origHudAnchors == nil then
        self.origHudAnchors = {
            left = _G.g_hudAnchorLeft or 0.0,
            right = _G.g_hudAnchorRight or 1.0,
            bottom = _G.g_hudAnchorBottom or 0.0,
            top = _G.g_hudAnchorTop or 1.0
        }
    end

    _G.g_hudAnchorLeft = self.hudAnchorLeft
    _G.g_hudAnchorRight = self.hudAnchorRight
    _G.g_hudAnchorBottom = self.hudAnchorBottom
    _G.g_hudAnchorTop = self.hudAnchorTop

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

    self:repositionAllHudDisplays()

    print(string.format("[FS25_VR] Applied VR HUD Insets: Left=%.2f, Right=%.2f, Bottom=%.2f, Top=%.2f (UI Scale: %.2fx)",
        self.hudAnchorLeft, self.hudAnchorRight, self.hudAnchorBottom, self.hudAnchorTop, self.effectiveUiScale))
end

function VRCompanion:restoreHudInsets(preserveOrigAnchors)
    local left = 0.0
    local right = 1.0
    local bottom = 0.0
    local top = 1.0

    if self.origHudAnchors ~= nil then
        left = self.origHudAnchors.left or 0.0
        right = self.origHudAnchors.right or 1.0
        bottom = self.origHudAnchors.bottom or 0.0
        top = self.origHudAnchors.top or 1.0
        if not preserveOrigAnchors then
            self.origHudAnchors = nil
        end
    else
        left = 0.0
        right = 1.0
        bottom = 0.0
        top = 1.0
        if preserveOrigAnchors then
            self.origHudAnchors = {
                left = 0.0,
                right = 1.0,
                bottom = 0.0,
                top = 1.0
            }
        end
    end

    _G.g_hudAnchorLeft = left
    _G.g_hudAnchorRight = right
    _G.g_hudAnchorBottom = bottom
    _G.g_hudAnchorTop = top

    local baseLeft = self.hudAnchorLeftBase or self.DEFAULT_HUD_INSET_X
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_INSET_X)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_INSET_Y
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_INSET_TOP)

    self.hudAnchorLeft = baseLeft
    self.hudAnchorRight = baseRight
    self.hudAnchorBottom = baseBottom
    self.hudAnchorTop = baseTop
    self.currentConvergenceOffset = 0.0
    self.currentConvergenceDisparity = 0.0

    local restoredScale = self.origUiScale or 1.0
    if not preserveOrigAnchors then
        self.origUiScale = nil
    end

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

    for _, info in ipairs(HUD_HOOKED_CLASSES) do
        local cls = getGlobalClass(info.className)
        if cls ~= nil then
            local displayName = info.displayName

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

-- Export to Hud subsystem table
VRCompanion.Hud.computeConvergenceDisparity = function(self, ...) return VRCompanion.computeConvergenceDisparity(VRCompanion, ...) end
VRCompanion.Hud.getConvergenceDisparity = function(self, ...) return VRCompanion.getConvergenceDisparity(VRCompanion, ...) end
VRCompanion.Hud.getConvergenceOffset = function(self, ...) return VRCompanion.getConvergenceOffset(VRCompanion, ...) end
VRCompanion.Hud.getFillLevelsDisplay = function(self, ...) return VRCompanion.getFillLevelsDisplay(VRCompanion, ...) end
VRCompanion.Hud.positionFillLevelsUnderVehicle = function(self, ...) return VRCompanion.positionFillLevelsUnderVehicle(VRCompanion, ...) end
VRCompanion.Hud.updateHudConvergence = function(self, ...) return VRCompanion.updateHudConvergence(VRCompanion, ...) end
VRCompanion.Hud.getHudState = function(self, ...) return VRCompanion.getHudState(VRCompanion, ...) end
VRCompanion.Hud.isDisplayVisible = function(self, ...) return VRCompanion.isDisplayVisible(VRCompanion, ...) end
VRCompanion.Hud.applyHudState = function(self, ...) return VRCompanion.applyHudState(VRCompanion, ...) end
VRCompanion.Hud.setHudState = function(self, ...) return VRCompanion.setHudState(VRCompanion, ...) end
VRCompanion.Hud.cycleHudState = function(self, ...) return VRCompanion.cycleHudState(VRCompanion, ...) end
VRCompanion.Hud.onHudToggleAction = function(self, ...) return VRCompanion.onHudToggleAction(VRCompanion, ...) end
VRCompanion.Hud.calculateHudAnchors = function(self, ...) return VRCompanion.calculateHudAnchors(VRCompanion, ...) end
VRCompanion.Hud.configureHudInsets = function(self, ...) return VRCompanion.configureHudInsets(VRCompanion, ...) end
VRCompanion.Hud.getHudInsets = function(self, ...) return VRCompanion.getHudInsets(VRCompanion, ...) end
VRCompanion.Hud.positionHudDisplay = function(self, ...) return VRCompanion.positionHudDisplay(VRCompanion, ...) end
VRCompanion.Hud.repositionAllHudDisplays = function(self, ...) return VRCompanion.repositionAllHudDisplays(VRCompanion, ...) end
VRCompanion.Hud.applyHudInsets = function(self, ...) return VRCompanion.applyHudInsets(VRCompanion, ...) end
VRCompanion.Hud.restoreHudInsets = function(self, ...) return VRCompanion.restoreHudInsets(VRCompanion, ...) end
VRCompanion.Hud.hookHud = function(self, ...) return VRCompanion.hookHud(VRCompanion, ...) end
VRCompanion.Hud.unhookHud = function(self, ...) return VRCompanion.unhookHud(VRCompanion, ...) end

return VRCompanion.Hud
