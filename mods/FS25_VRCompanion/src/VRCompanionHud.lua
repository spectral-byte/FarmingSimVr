-- ====================================================================
-- FS25 VR Companion Mod: HUD Subsystem
-- Manages in-engine VR HUD Group placement and scaling, the HUD Safe Area,
-- stereoscopic convergence, the three-stage visibility state machine, and
-- the Speedometer group's stack of fill levels under the speedometer.
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
if VRCompanion.hudSafeAreaHorizontal == nil then VRCompanion.hudSafeAreaHorizontal = VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL end
if VRCompanion.hudSafeAreaVertical == nil then VRCompanion.hudSafeAreaVertical = VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL end
if VRCompanion.hudConvergenceDepth == nil then VRCompanion.hudConvergenceDepth = VRCompanion.DEFAULT_CONVERGENCE_DEPTH end
if VRCompanion.hudAnchorLeftBase == nil then VRCompanion.hudAnchorLeftBase = VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL end
if VRCompanion.hudAnchorRightBase == nil then VRCompanion.hudAnchorRightBase = 1.0 - VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL end
if VRCompanion.hudAnchorBottomBase == nil then VRCompanion.hudAnchorBottomBase = VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL end
if VRCompanion.hudAnchorTopBase == nil then VRCompanion.hudAnchorTopBase = 1.0 - VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL end
if VRCompanion.hudAnchorLeft == nil then VRCompanion.hudAnchorLeft = VRCompanion.hudAnchorLeftBase end
if VRCompanion.hudAnchorRight == nil then VRCompanion.hudAnchorRight = VRCompanion.hudAnchorRightBase end
if VRCompanion.hudAnchorBottom == nil then VRCompanion.hudAnchorBottom = VRCompanion.hudAnchorBottomBase end
if VRCompanion.hudAnchorTop == nil then VRCompanion.hudAnchorTop = VRCompanion.hudAnchorTopBase end
if VRCompanion.currentConvergenceOffset == nil then VRCompanion.currentConvergenceOffset = 0.0 end
-- The four HUD Groups (CONTEXT.md, ADR-0022). Each group lists its engine displays and its
-- fixed home corner, and says whether it shows in the Streamlined and Full HUD. Placement and
-- visibility both look a display up through its group. The context action display is in no group.
-- The group names, in order, are VRCompanion.HUD_GROUP_NAMES (VRCompanionConfig.lua): that one list
-- drives layout, visibility, save/load and listing.
VRCompanion.HUD_GROUPS = {
    map = { displays = { "IngameMap" }, alignX = "left", alignY = "bottom" },
    speedometer = { displays = { "SpeedMeterDisplay", "FillLevelsDisplay" }, alignX = "right", alignY = "bottom" },
    info = { displays = { "GameInfoDisplay" }, alignX = "right", alignY = "top" },
    controls = { displays = { "InputHelpDisplay" }, alignX = "left", alignY = "top" }
}
-- Visibility flags start at their DEFAULT_* constants (VRCompanionConfig.lua)
for _, name in ipairs(VRCompanion.HUD_GROUP_NAMES) do
    local group = VRCompanion.HUD_GROUPS[name]
    group.streamlined = VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED[name]
    group.full = VRCompanion.DEFAULT_HUD_GROUP_FULL[name]
end

-- Not in any group: bottom-centered on the Speedometer group's bottom margin, drawn at that
-- group's scale, and shown in both the Streamlined and Full HUD.
local CONTEXT_ACTION_DISPLAY = "ContextActionDisplay"
local CONTEXT_ACTION_ANCHOR_GROUP = "speedometer"

-- The group a display belongs to, and the group's name
local function findHudGroup(displayName)
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local group = VRCompanion.HUD_GROUPS[groupName]
        for _, member in ipairs(group.displays) do
            if member == displayName then return group, groupName end
        end
    end
    return nil, nil
end

-- A group's HUD Group Margins and scale multiplier, clamped to their ranges
function VRCompanion:getHudGroupLayout(groupName)
    local stored = (self.hudGroupLayout or {})[groupName] or {}
    return self:sanitizeHudGroupLayout(stored.marginX, stored.marginY, stored.scale)
end

-- Sets a group's margins, and its scale when one is given. Returns the clamped layout, or nil for
-- a name that is not a HUD Group.
function VRCompanion:setHudGroupLayout(groupName, marginX, marginY, scale)
    if self.HUD_GROUPS[groupName] == nil then return nil end
    if self.hudGroupLayout == nil then self.hudGroupLayout = {} end
    local current = self:getHudGroupLayout(groupName)
    local layout = self:sanitizeHudGroupLayout(marginX, marginY, scale or current.scale)
    self.hudGroupLayout[groupName] = layout
    return layout
end

-- The game's own UI scale setting
function VRCompanion:getHudBaseUiScale()
    local gs = g_gameSettings or _G.g_gameSettings
    local gsClass = GameSettings or _G.GameSettings
    if gs ~= nil and type(gs.getValue) == "function" and gsClass ~= nil and gsClass.SETTING ~= nil and gsClass.SETTING.UI_SCALE ~= nil then
        return gs:getValue(gsClass.SETTING.UI_SCALE) or 1.0
    end
    return 1.0
end

-- The scale a group is drawn at: the game's UI scale setting times the group's own scale
function VRCompanion:getHudGroupScale(groupName)
    return self:getHudBaseUiScale() * self:getHudGroupLayout(groupName).scale
end

-- Width and height of a display, 0 where it does not say
local function getDisplaySize(display)
    local w, h = 0, 0
    if type(display.getWidth) == "function" then
        local ok, v = pcall(display.getWidth, display)
        if ok and type(v) == "number" then w = v end
    elseif type(display.width) == "number" then
        w = display.width
    end
    if type(display.getHeight) == "function" then
        local ok, v = pcall(display.getHeight, display)
        if ok and type(v) == "number" then h = v end
    elseif type(display.height) == "number" then
        h = display.height
    end
    return w, h
end

-- The scale a display is drawn at, taken from its group
function VRCompanion:getHudDisplayScale(displayName)
    local _, groupName = findHudGroup(displayName)
    return self:getHudGroupScale(groupName or CONTEXT_ACTION_ANCHOR_GROUP)
end

-- Where a display's bottom-left corner goes: its group's outer edges sit at the group's margins
-- from the home corner, with the display's width or height subtracted for right- and top-anchored
-- groups. The Speedometer group's bottom margin is measured to the bottom of the lowest fill level,
-- so the fill levels sit on it and the speedometer stacks above them, one scaled gap clear. The
-- convergence offset moves every display horizontally by the same amount.
function VRCompanion:getHudDisplayPosition(display, displayName, convergenceOffset)
    local offset = convergenceOffset or 0.0
    local group, groupName = findHudGroup(displayName)
    if group == nil then
        return 0.5 + offset, self:getHudGroupLayout(CONTEXT_ACTION_ANCHOR_GROUP).marginY
    end
    local layout = self:getHudGroupLayout(groupName)
    local w, h
    if displayName == "FillLevelsDisplay" then
        w, h = self:getFillLevelsBlockSize(display)
    else
        w, h = getDisplaySize(display)
    end
    local x = (group.alignX == "right") and (1.0 - layout.marginX - w) or layout.marginX
    local y = (group.alignY == "top") and (1.0 - layout.marginY - h) or layout.marginY
    if displayName == "SpeedMeterDisplay" then
        local fillLevels = self:getFillLevelsDisplay()
        if fillLevels ~= nil then
            local _, fillHeight = self:getFillLevelsBlockSize(fillLevels)
            -- No fill levels shown: nothing to stack above, so no space and no gap are reserved
            if fillHeight > 0 then
                y = y + fillHeight + self.HUD_FILL_LEVEL_GAP * self:getHudGroupScale(groupName)
            end
        end
    end
    return x + offset, y
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
            targetParity = self:getDualPassEye()
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

-- The fill levels display's width, and the height of all its fill levels as drawn at the Speedometer
-- group's scale: one row per fill level, a fixed gap between rows.
function VRCompanion:getFillLevelsBlockSize(display)
    local uiScale = self:getHudGroupScale("speedometer")

    local rowHeight = nil
    if type(display.getHeight) == "function" then
        local ok, h = pcall(display.getHeight, display)
        if ok and type(h) == "number" and h > 0 then rowHeight = h end
    end
    if rowHeight == nil and display.height ~= nil and type(display.height) == "number" and display.height > 0 then
        rowHeight = display.height
    end
    if rowHeight == nil and display.bgLeft ~= nil and display.bgLeft.height ~= nil and display.bgLeft.height > 0 then
        rowHeight = display.bgLeft.height
    end
    if rowHeight == nil and display.bgScale ~= nil and display.bgScale.height ~= nil and display.bgScale.height > 0 then
        rowHeight = display.bgScale.height
    end
    if rowHeight == nil then
        rowHeight = 0.045 * uiScale
    end

    local width = nil
    if type(display.getWidth) == "function" then
        local ok, w = pcall(display.getWidth, display)
        if ok and type(w) == "number" and w > 0 then width = w end
    end
    if width == nil and display.width ~= nil and type(display.width) == "number" and display.width > 0 then
        width = display.width
    end
    if width == nil and display.bgScale ~= nil and display.bgLeft ~= nil and display.bgRight ~= nil then
        local totalW = (display.bgLeft.width or 0) + (display.bgScale.width or 0) + (display.bgRight.width or 0)
        if totalW > 0 then width = totalW end
    end
    if width == nil then
        width = 0.12 * uiScale
    end

    -- The display's own list of fill levels is authoritative, and may be empty. Only a display that
    -- says nothing about its levels is assumed to be showing one row.
    local numLevels = 1
    if type(display.fillLevelData) == "table" then
        numLevels = #display.fillLevelData
    elseif display.vehicle ~= nil and type(display.vehicle.getFillUnits) == "function" then
        local ok, units = pcall(display.vehicle.getFillUnits, display.vehicle)
        if ok and type(units) == "table" then
            numLevels = #units
        end
    end
    local height = numLevels * rowHeight + math.max(0, numLevels - 1) * self.HUD_FILL_LEVEL_GAP * uiScale
    return width, height
end

-- Places the Speedometer group's stack: the fill levels right-aligned on the group's bottom margin,
-- with the speedometer directly above them (see getHudDisplayPosition). Returns the fill levels'
-- x and y and the speedometer's y.
function VRCompanion:positionFillLevelsUnderSpeedometer(display, offset)
    if display == nil then return end

    local currentOffset = offset or self.currentConvergenceOffset or 0.0
    local uiScale = self:getHudGroupScale("speedometer")
    local flPosX, fillBottom = self:getHudDisplayPosition(display, "FillLevelsDisplay", currentOffset)

    self:dbgRecordPlacement("FillLevelsDisplay", flPosX, fillBottom) -- [DEBUG-hudgrp]
    self:dbgFillLevels(display, flPosX, fillBottom) -- [DEBUG-hudgrp]
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

    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local sm = (hud ~= nil) and (hud.speedMeter or hud.speedMeterDisplay) or nil
    local smBottom = nil
    if sm ~= nil and type(sm.setPosition) == "function" then
        local smX
        smX, smBottom = self:getHudDisplayPosition(sm, "SpeedMeterDisplay", currentOffset)
        pcall(sm.setPosition, sm, smX, smBottom)
    end

    return flPosX, fillBottom, smBottom
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
        eyeOrParity = self:getDualPassEye()
    else
        eyeOrParity = self:getFrameParity()
    end
    local offset = self:getConvergenceOffset(eyeOrParity, self.currentConvergenceDisparity)

    self.currentConvergenceOffset = offset

    local baseLeft = self.hudAnchorLeftBase or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_SAFE_AREA_VERTICAL
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_SAFE_AREA_VERTICAL)

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

    -- Anchors and convergence state above always update; moving the elements
    -- themselves is wasted work while the HUD is Hidden. setHudState repositions
    -- them against this state when the HUD becomes visible.
    if self:getHudState() ~= self.HUD_STATE_HIDDEN then
        self:repositionHudForConvergence()
    end

    return shiftedLeft, shiftedRight, offset
end

-- Moves every HUD element to its convergence-offset position using the current
-- anchor and convergence state.
function VRCompanion:repositionHudForConvergence()
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud == nil then return end

    local offset = self.currentConvergenceOffset or 0.0

    local fl = self:getFillLevelsDisplay(hud)

    -- Each display sits at its group's margins, shifted by the same convergence offset
    local function place(display, displayName)
        if display == nil or type(display.setPosition) ~= "function" then return end
        local x, y = self:getHudDisplayPosition(display, displayName, offset)
        self:dbgRecordPlacement(displayName, x, y) -- [DEBUG-hudgrp]
        pcall(display.setPosition, display, x, y)
    end

    -- The Speedometer group is one stack: the fill levels on the bottom margin, the speedometer above them
    if fl ~= nil then
        self:positionFillLevelsUnderSpeedometer(fl, offset)
    else
        place(hud.speedMeter or hud.speedMeterDisplay, "SpeedMeterDisplay")
    end
    place(hud.inputHelp or hud.inputHelpDisplay, "InputHelpDisplay")
    place(hud.gameInfo or hud.gameInfoDisplay, "GameInfoDisplay")
    place(hud.ingameMap or hud.ingameMapDisplay, "IngameMap")
    place(hud.contextAction or hud.contextActionDisplay, CONTEXT_ACTION_DISPLAY)
end

function VRCompanion:getHudState()
    return self.hudState or self.HUD_STATE_HIDDEN
end

function VRCompanion:isDisplayVisible(displayName, state)
    if displayName == nil then return false end
    state = state or self:getHudState()
    if state == self.HUD_STATE_HIDDEN then
        return false
    end
    if state ~= self.HUD_STATE_STREAMLINED and state ~= self.HUD_STATE_FULL then
        return false
    end
    if displayName == CONTEXT_ACTION_DISPLAY then
        return true
    end
    local group = findHudGroup(displayName)
    if group == nil then
        return state == self.HUD_STATE_FULL
    end
    if state == self.HUD_STATE_STREAMLINED then
        return group.streamlined == true
    end
    return group.full == true
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
    -- Hidden updates skip repositioning, so place the elements now with the
    -- current convergence offset. 2D Floating Window Mode keeps its own anchors.
    if state ~= self.HUD_STATE_HIDDEN and not self.is2DViewActive then
        self:repositionHudForConvergence()
    end
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
        local message = string.format("VR HUD: %s", stateName)
        if nextState ~= self.HUD_STATE_HIDDEN then
            local groupLabels = {}
            for _, name in ipairs(self:getEnabledHudGroups(nextState)) do
                table.insert(groupLabels, self:getHudGroupLabel(name))
            end
            message = string.format("%s (%s)", message, (#groupLabels > 0) and table.concat(groupLabels, ", ") or "none")
        end
        pcall(function()
            mission:showBlinkingWarning(message, 2500)
        end)
    end

    return nextState
end

-- Display label of a group name, e.g. "speedometer" -> "Speedometer".
function VRCompanion:getHudGroupLabel(groupName)
    return (string.gsub(groupName, "^%l", string.upper))
end

-- The group names that show in a HUD state, in HUD_GROUP_NAMES order. Hidden has none.
function VRCompanion:getEnabledHudGroups(state)
    local enabled = {}
    if state ~= self.HUD_STATE_STREAMLINED and state ~= self.HUD_STATE_FULL then
        return enabled
    end
    local flag = (state == self.HUD_STATE_STREAMLINED) and "streamlined" or "full"
    for _, name in ipairs(self.HUD_GROUP_NAMES) do
        local group = self.HUD_GROUPS[name]
        if group ~= nil and group[flag] == true then
            table.insert(enabled, name)
        end
    end
    return enabled
end

-- Turns a group on or off for the Streamlined ("streamlined") or Full ("full") HUD and applies it
-- to the displays at once. Returns false when the group or state name is unknown.
function VRCompanion:setHudGroupVisibility(groupName, stateName, enabled)
    local group = self.HUD_GROUPS[groupName]
    if group == nil or (stateName ~= "streamlined" and stateName ~= "full") then
        return false
    end
    group[stateName] = (enabled == true)
    self:applyHudState(self:getHudState())
    return true
end

function VRCompanion:onHudToggleAction(...)
    return self:cycleHudState()
end

-- The HUD Safe Area: the engine's global HUD anchors, which keep everything outside the HUD Groups
-- (warnings, notifications, other mods' HUDs) inside the lens. It no longer places the groups.
function VRCompanion:calculateHudAnchors(horizontal, vertical)
    local clampedHorizontal = self:clamp(horizontal or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL, self.MIN_HUD_SAFE_AREA, self.MAX_HUD_SAFE_AREA_HORIZONTAL)
    local clampedVertical = self:clamp(vertical or self.DEFAULT_HUD_SAFE_AREA_VERTICAL, self.MIN_HUD_SAFE_AREA, self.MAX_HUD_SAFE_AREA_VERTICAL)

    local left = clampedHorizontal
    local right = 1.0 - clampedHorizontal
    local bottom = clampedVertical
    local top = 1.0 - clampedVertical

    return left, right, bottom, top
end

function VRCompanion:configureHudSafeArea(horizontal, vertical)
    local left, right, bottom, top = self:calculateHudAnchors(horizontal, vertical)
    self.hudSafeAreaHorizontal = left
    self.hudSafeAreaVertical = bottom
    self.hudAnchorLeftBase = left
    self.hudAnchorRightBase = right
    self.hudAnchorBottomBase = bottom
    self.hudAnchorTopBase = top
    self.hudAnchorLeft = left
    self.hudAnchorRight = right
    self.hudAnchorBottom = bottom
    self.hudAnchorTop = top
    self.currentConvergenceOffset = 0.0
end

function VRCompanion:getHudSafeArea()
    return {
        left = self.hudAnchorLeft or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL,
        right = self.hudAnchorRight or (1.0 - self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL),
        bottom = self.hudAnchorBottom or self.DEFAULT_HUD_SAFE_AREA_VERTICAL,
        top = self.hudAnchorTop or (1.0 - self.DEFAULT_HUD_SAFE_AREA_VERTICAL),
        baseLeft = self.hudAnchorLeftBase or self.hudAnchorLeft or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL,
        baseRight = self.hudAnchorRightBase or self.hudAnchorRight or (1.0 - self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL),
        horizontal = self.hudSafeAreaHorizontal or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL,
        vertical = self.hudSafeAreaVertical or self.DEFAULT_HUD_SAFE_AREA_VERTICAL,
        convergenceOffset = self.currentConvergenceOffset or 0.0,
        convergenceDisparity = self.currentConvergenceDisparity or 0.0,
        convergenceDepth = self.hudConvergenceDepth or self.DEFAULT_CONVERGENCE_DEPTH,
        showHudLoadNotification = (self.showHudLoadNotification == true)
    }
end

function VRCompanion:positionHudDisplay(display, displayName)
    if display == nil then return end

    local scale = self:getHudDisplayScale(displayName)

    -- Scale first, so the display's size is measured as it will be drawn
    if type(display.setScale) == "function" then
        pcall(function() display:setScale(scale) end)
    end

    if displayName == "FillLevelsDisplay" then
        self:positionFillLevelsUnderSpeedometer(display)
        return
    end

    -- The current convergence offset shifts every display, as it does in repositionHudForConvergence
    local anchorX, anchorY = self:getHudDisplayPosition(display, displayName, self.currentConvergenceOffset)

    self:dbgRecordPlacement(displayName, anchorX, anchorY) -- [DEBUG-hudgrp]
    display.anchorX = anchorX
    display.anchorY = anchorY
    display.anchorLeft = self.hudAnchorLeft
    display.anchorRight = self.hudAnchorRight
    display.anchorBottom = self.hudAnchorBottom
    display.anchorTop = self.hudAnchorTop
    display.vrUiScale = scale

    if type(display.setPosition) == "function" then
        pcall(function() display:setPosition(anchorX, anchorY) end)
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

    -- The fill levels go first: the speedometer sits above them, so their size must be settled
    local d6 = self:getFillLevelsDisplay(targetHud)
    if d6 ~= nil then self:positionHudDisplay(d6, "FillLevelsDisplay") end

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

    if type(targetHud.storeScaledValues) == "function" then
        pcall(function() targetHud:storeScaledValues() end)
    end
    if type(targetHud.updateDimensions) == "function" then
        pcall(function() targetHud:updateDimensions() end)
    end
end

function VRCompanion:applyHudSafeArea()
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

    if self.origUiScale == nil then
        self.origUiScale = self:getHudBaseUiScale()
    end

    self:repositionAllHudDisplays()

    print(string.format("[FS25_VR] Applied VR HUD Safe Area: Left=%.2f, Right=%.2f, Bottom=%.2f, Top=%.2f",
        self.hudAnchorLeft, self.hudAnchorRight, self.hudAnchorBottom, self.hudAnchorTop))
end

function VRCompanion:restoreHudSafeArea(preserveOrigAnchors)
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

    local baseLeft = self.hudAnchorLeftBase or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL
    local baseRight = self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL)
    local baseBottom = self.hudAnchorBottomBase or self.DEFAULT_HUD_SAFE_AREA_VERTICAL
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_SAFE_AREA_VERTICAL)

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

-- The engine lays the map's content out from the element's background position, which it reads from
-- g_safeFrameOffsetX/Y rather than from where the element was placed. Placing only the element moves
-- the map's frame and leaves the content at the screen corner, outside the lens (#56). So the map's
-- layout and draw calls run with that position at the Map group's, the way ConstructionScreen shifts it.
local MAP_BASE_POSITION_METHODS = { "draw", "update", "setScale", "storeScaledValues", "setLayout", "toggleSize" }

-- The Map group's position for the map's layout, or nil where the engine's own position applies:
-- the fullscreen map, the Hidden HUD and 2D Floating Window Mode
function VRCompanion:getMapBasePosition(map)
    if map == nil or map.isFullscreen == true then return nil end
    if self.is2DViewActive or self:getHudState() == self.HUD_STATE_HIDDEN then return nil end
    return self:getHudDisplayPosition(map, "IngameMap", self.currentConvergenceOffset)
end

-- Runs fn(map, ...) with the engine's background position at the Map group's, then restores it
function VRCompanion:callWithMapBasePosition(map, fn, ...)
    local x, y = self:getMapBasePosition(map)
    if x == nil then return fn(map, ...) end
    local savedX, savedY = _G.g_safeFrameOffsetX, _G.g_safeFrameOffsetY
    _G.g_safeFrameOffsetX, _G.g_safeFrameOffsetY = x, y
    local results = table.pack(pcall(fn, map, ...))
    _G.g_safeFrameOffsetX, _G.g_safeFrameOffsetY = savedX, savedY
    if not results[1] then error(results[2], 0) end
    return table.unpack(results, 2, results.n)
end

-- [DEBUG-hudgrp] Temporary HUD diagnostics for the map (#56) and the fill levels flicker. Off unless
-- the player runs "vrHudDiag on"; every line is tagged so the whole block can be removed with one grep.
local DBG_TAG = "[FS25_VR] [DEBUG-hudgrp] "
local DBG_DISPLAY_NAMES = { "IngameMap", "FillLevelsDisplay", "SpeedMeterDisplay", "InputHelpDisplay", "GameInfoDisplay", "ContextActionDisplay" }

local function dbgFmt(v)
    if type(v) == "number" then return string.format("%.4f", v) end
    return tostring(v)
end

local function dbgScalars(t)
    local keys = {}
    for k, v in pairs(t) do
        local tv = type(v)
        if tv == "number" or tv == "boolean" or (tv == "string" and #v < 40) then keys[#keys + 1] = tostring(k) end
    end
    table.sort(keys)
    local parts = {}
    for i = 1, math.min(#keys, 60) do
        parts[#parts + 1] = keys[i] .. "=" .. dbgFmt(t[keys[i]])
    end
    return table.concat(parts, " ")
end

-- Names of the functions a display can call, own and inherited through the metatable chain
local function dbgMethods(t)
    local names, seen = {}, {}
    local cur, level = t, 0
    while type(cur) == "table" and level < 5 do
        for k, v in pairs(cur) do
            if type(v) == "function" and not seen[k] then
                seen[k] = true
                names[#names + 1] = tostring(k)
            end
        end
        local ok, mt = pcall(getmetatable, cur)
        if not ok or type(mt) ~= "table" or type(mt.__index) ~= "table" then break end
        cur = mt.__index
        level = level + 1
    end
    table.sort(names)
    return string.sub(table.concat(names, " "), 1, 2500)
end

-- Names and kinds of the table-valued fields, to find where a display keeps its position
local function dbgTables(t)
    local names = {}
    for k, v in pairs(t) do
        if type(v) == "table" then names[#names + 1] = tostring(k) end
    end
    table.sort(names)
    return table.concat(names, " ")
end

local function dbgCall(display, method)
    if type(display[method]) ~= "function" then return "n/a" end
    local ok, a, b = pcall(display[method], display)
    if not ok then return "error" end
    if b ~= nil then return dbgFmt(a) .. "," .. dbgFmt(b) end
    return dbgFmt(a)
end

function VRCompanion:dbgFindDisplay(displayName, hud)
    if displayName == "FillLevelsDisplay" then return self:getFillLevelsDisplay(hud) end
    if displayName == "IngameMap" then return hud.ingameMap or hud.ingameMapDisplay end
    if displayName == "SpeedMeterDisplay" then return hud.speedMeter or hud.speedMeterDisplay end
    if displayName == "InputHelpDisplay" then return hud.inputHelp or hud.inputHelpDisplay end
    if displayName == "GameInfoDisplay" then return hud.gameInfo or hud.gameInfoDisplay end
    return hud.contextAction or hud.contextActionDisplay
end

-- Logs the map's frame and content positions around a draw, a few times only
function VRCompanion:dbgMapSnapshot(map, when)
    self.dbgMapSnaps = (self.dbgMapSnaps or 0) + 1
    if self.dbgMapSnaps > 12 then return end
    local o, m, l = map.overlay or {}, map.mapOverlay or {}, map.layout or {}
    print(DBG_TAG .. string.format("map %s: frame=(%s,%s) content=(%s,%s;%sx%s) layout.screenMapPos=(%s,%s) layout.mapPos=(%s,%s) base=%s globals=(%s,%s)",
        when, dbgFmt(o.x), dbgFmt(o.y), dbgFmt(m.x), dbgFmt(m.y), dbgFmt(m.width), dbgFmt(m.height), dbgFmt(l.screenMapPosX), dbgFmt(l.screenMapPosY),
        dbgFmt(l.mapPosX), dbgFmt(l.mapPosY), tostring(self.dbgMapBase), dbgFmt(_G.g_safeFrameOffsetX), dbgFmt(_G.g_safeFrameOffsetY)))
end

function VRCompanion:dbgRecordPlacement(displayName, x, y)
    if not self.hudDiagnostics then return end
    self.dbgPlaced = self.dbgPlaced or {}
    self.dbgPlaced[displayName] = { x = x, y = y }
end

-- Called at the top of every hooked draw, before the mod touches the display
function VRCompanion:dbgDrawEntry(displayName, display)
    if not self.hudDiagnostics then return end
    self.dbgDraws = self.dbgDraws or {}
    self.dbgOverwrites = self.dbgOverwrites or {}
    local n = (self.dbgDraws[displayName] or 0) + 1
    self.dbgDraws[displayName] = n
    if n == 1 or n % 600 == 0 then
        local mission = g_currentMission or _G.g_currentMission
        local hud = (mission ~= nil) and mission.hud or nil
        local found = hud and self:dbgFindDisplay(displayName, hud) or nil
        print(DBG_TAG .. string.format("draw #%d %s: x=%s y=%s isVisible=%s modVisible=%s hudState=%s sameAsHudField=%s", n, displayName,
            dbgFmt(display.x), dbgFmt(display.y), tostring(display.isVisible), tostring(self:isDisplayVisible(displayName)), tostring(self:getHudState()),
            tostring(found == display)))
    end
    local placed = self.dbgPlaced and self.dbgPlaced[displayName]
    if placed ~= nil and type(display.x) == "number" and type(display.y) == "number"
        and (math.abs(display.x - placed.x) > 0.0005 or math.abs(display.y - placed.y) > 0.0005) then
        local count = (self.dbgOverwrites[displayName] or 0) + 1
        self.dbgOverwrites[displayName] = count
        if count <= 20 then
            print(DBG_TAG .. string.format("ENGINE MOVED %s since the mod placed it: placed=(%s,%s) at draw=(%s,%s) (draw #%d)", displayName,
                dbgFmt(placed.x), dbgFmt(placed.y), dbgFmt(display.x), dbgFmt(display.y), n))
        end
    end
end

-- Called at the end of every fill levels placement: logs whenever what the display reports changes
function VRCompanion:dbgFillLevels(display, placedX, placedY)
    if not self.hudDiagnostics then return end
    local w, h = self:getFillLevelsBlockSize(display)
    local smY = nil
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local sm = hud and (hud.speedMeter or hud.speedMeterDisplay)
    if sm ~= nil then
        local _, y = self:getHudDisplayPosition(sm, "SpeedMeterDisplay", 0)
        smY = y
    end
    local levels = (type(display.fillLevelData) == "table") and #display.fillLevelData or -1
    local sig = string.format("levels=%d placed=(%s,%s) block=(%s,%s) engineSize=(%s;%s) speedometerY=%s isVisible=%s getIsVisible=%s modVisible=%s",
        levels, dbgFmt(placedX), dbgFmt(placedY), dbgFmt(w), dbgFmt(h), dbgCall(display, "getWidth"), dbgCall(display, "getHeight"),
        dbgFmt(smY), tostring(display.isVisible), dbgCall(display, "getIsVisible"), tostring(self:isDisplayVisible("FillLevelsDisplay")))
    if sig ~= self.dbgFillSig then
        self.dbgFillSig = sig
        self.dbgFillLogs = (self.dbgFillLogs or 0) + 1
        if self.dbgFillLogs <= 60 then
            print(DBG_TAG .. "fill levels changed: " .. sig)
        end
    end
end

-- Function names on a class table and what its metatable looks like
local function dbgClass(label, cls)
    if type(cls) ~= "table" then
        print(DBG_TAG .. "  class " .. label .. ": not a table (" .. type(cls) .. ")")
        return
    end
    local names = {}
    for k, v in pairs(cls) do
        if type(v) == "function" then names[#names + 1] = tostring(k) end
    end
    table.sort(names)
    local ok, mt = pcall(getmetatable, cls)
    local mtInfo = (not ok) and "getmetatable error" or (mt == nil and "no metatable" or ("metatable __index=" .. type(mt.__index) .. " __metatable=" .. tostring(mt.__metatable)))
    print(DBG_TAG .. "  class " .. label .. " (" .. mtInfo .. "): " .. string.sub(table.concat(names, " "), 1, 3000))
end

function VRCompanion:dbgDumpMapClasses(map)
    dbgClass("IngameMap", _G.IngameMap)
    local ok, mt = pcall(getmetatable, map)
    if ok and type(mt) == "table" and type(mt.__index) == "table" then
        dbgClass("IngameMap instance __index", mt.__index)
        local ok2, mt2 = pcall(getmetatable, mt.__index)
        if ok2 and type(mt2) == "table" and type(mt2.__index) == "table" then dbgClass("IngameMap superclass", mt2.__index) end
    end
    for _, name in ipairs({ "IngameMapLayout", "IngameMapLayoutNone", "IngameMapLayoutCircle", "IngameMapLayoutSquare", "IngameMapLayoutSquareLarge", "IngameMapLayoutFullscreen" }) do
        dbgClass(name, _G[name])
    end
    if type(map.layout) == "table" then
        local ok3, lmt = pcall(getmetatable, map.layout)
        if ok3 and type(lmt) == "table" and type(lmt.__index) == "table" then dbgClass("current layout instance __index", lmt.__index) end
    end
end

function VRCompanion:dbgDump()
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud == nil then
        print(DBG_TAG .. "no mission HUD")
        return
    end
    local map = hud.ingameMap or hud.ingameMapDisplay
    if map ~= nil then self:dbgDumpMapClasses(map) end
    print(DBG_TAG .. string.format("hudState=%s is2D=%s anchors L=%s R=%s B=%s T=%s convergenceOffset=%s", tostring(self:getHudState()),
        tostring(self.is2DViewActive), dbgFmt(self.hudAnchorLeft), dbgFmt(self.hudAnchorRight), dbgFmt(self.hudAnchorBottom),
        dbgFmt(self.hudAnchorTop), dbgFmt(self.currentConvergenceOffset)))
    for _, name in ipairs(DBG_DISPLAY_NAMES) do
        local display = self:dbgFindDisplay(name, hud)
        local _, groupName = findHudGroup(name)
        if display == nil then
            print(DBG_TAG .. name .. ": display not found on the HUD")
        else
            local px, py = nil, nil
            if groupName ~= nil or name == CONTEXT_ACTION_DISPLAY then
                px, py = self:getHudDisplayPosition(display, name, self.currentConvergenceOffset)
            end
            print(DBG_TAG .. string.format("%s: group=%s wantedPos=(%s,%s) pos=(%s) size=(%s;%s) visible: isVisible=%s getIsVisible=%s modVisible=%s",
                name, tostring(groupName), dbgFmt(px), dbgFmt(py), dbgCall(display, "getPosition"), dbgCall(display, "getWidth"),
                dbgCall(display, "getHeight"), tostring(display.isVisible), dbgCall(display, "getIsVisible"), tostring(self:isDisplayVisible(name))))
            print(DBG_TAG .. "  fields: " .. dbgScalars(display))
            print(DBG_TAG .. "  tables: " .. dbgTables(display))
            print(DBG_TAG .. "  methods: " .. dbgMethods(display))
            for _, sub in ipairs({ "layout", "overlay", "bgLeft", "bgScale", "bgRight", "mapElement", "mapOverlay", "terrainOverlay", "iconZoom", "mapFrame", "frame", "mapScale" }) do
                if type(display[sub]) == "table" then
                    print(DBG_TAG .. "  ." .. sub .. ": " .. dbgScalars(display[sub]))
                end
            end
            if name == "IngameMap" then
                -- The map keeps its position somewhere other than x/y: show every sub-table's numbers
                for k, v in pairs(display) do
                    if type(v) == "table" and type(k) == "string" then
                        print(DBG_TAG .. "  .." .. k .. ": " .. dbgScalars(v))
                    end
                end
            end
        end
    end
end

-- Logs what the current map layout answers for its position and size, with and without the map as argument
local function dbgLayoutGetters(map)
    local layout = map.layout
    if type(layout) ~= "table" then return end
    for _, method in ipairs({ "getPosition", "getMapPosition", "getMapSize", "getMapPivot", "getWidth", "getHeight" }) do
        if type(layout[method]) == "function" then
            local ok1, a1, b1, c1, d1 = pcall(layout[method], layout)
            local ok2, a2, b2, c2, d2 = pcall(layout[method], layout, map)
            print(DBG_TAG .. string.format("  layout:%s() -> %s %s %s %s | layout:%s(map) -> %s %s %s %s", method,
                tostring(ok1), dbgFmt(a1), dbgFmt(b1), dbgFmt(c1), method, tostring(ok2), dbgFmt(a2), dbgFmt(b2), dbgFmt(c2)))
        end
    end
    if type(map.getPosition) == "function" then
        local ok, x, y = pcall(map.getPosition, map)
        print(DBG_TAG .. string.format("  map:getPosition() -> %s %s %s", tostring(ok), dbgFmt(x), dbgFmt(y)))
    end
end

-- While diagnostics are on, logs every render of the map's overlays: the coordinates the engine really draws at
function VRCompanion:dbgStartRenderProbe(map)
    local ids = {}
    if type(map.overlay) == "table" and map.overlay.overlayId then ids[map.overlay.overlayId] = "frame" end
    if type(map.mapOverlay) == "table" and map.mapOverlay.overlayId then ids[map.mapOverlay.overlayId] = "content" end
    if type(map.layout) == "table" and map.layout.overlayMask then ids[map.layout.overlayMask] = "mask" end
    if self.dbgOrigRenderOverlay == nil then self.dbgOrigRenderOverlay = _G.renderOverlay end
    local orig = self.dbgOrigRenderOverlay
    if type(orig) ~= "function" then
        print(DBG_TAG .. "renderOverlay is not a function; render probe not installed")
        return
    end
    self.dbgRenderLogs = 0
    self.dbgMapStack = {}
    _G.renderOverlay = function(id, x, y, w, h, ...)
        local depth = #self.dbgMapStack
        if (ids[id] ~= nil or depth > 0) and self.hudDiagnostics and self.dbgRenderLogs < 60 then
            self.dbgRenderLogs = self.dbgRenderLogs + 1
            print(DBG_TAG .. string.format("renderOverlay [%s] %s id=%s x=%s y=%s w=%s h=%s safeFrame=(%s,%s)", self.dbgMapStack[depth] or "outside map methods",
                tostring(ids[id] or "other"), tostring(id), dbgFmt(x), dbgFmt(y), dbgFmt(w), dbgFmt(h), dbgFmt(_G.g_safeFrameOffsetX), dbgFmt(_G.g_safeFrameOffsetY)))
        end
        return orig(id, x, y, w, h, ...)
    end

    -- Tag every map and layout method while it runs, so each render call names the method that made it
    self.dbgWrapped = {}
    local skip = { new = true, class = true, isa = true, superClass = true, copy = true }
    local classNames = { "IngameMap", "IngameMapLayout", "IngameMapLayoutNone", "IngameMapLayoutCircle", "IngameMapLayoutSquare", "IngameMapLayoutSquareLarge", "IngameMapLayoutFullscreen" }
    for _, className in ipairs(classNames) do
        local cls = _G[className]
        if type(cls) == "table" then
            local names = {}
            for k, v in pairs(cls) do
                if type(v) == "function" and not skip[k] then names[#names + 1] = k end
            end
            for _, k in ipairs(names) do
                local f = cls[k]
                local label = className .. "." .. k
                local wrapper = function(...)
                    local stack = self.dbgMapStack
                    stack[#stack + 1] = label
                    local r = table.pack(pcall(f, ...))
                    stack[#stack] = nil
                    if not r[1] then error(r[2], 0) end
                    return table.unpack(r, 2, r.n)
                end
                cls[k] = wrapper
                self.dbgWrapped[#self.dbgWrapped + 1] = { cls = cls, key = k, orig = f, wrapper = wrapper }
            end
        end
    end
end

function VRCompanion:dbgStopRenderProbe()
    -- Only undo a wrapper that is still the one installed, so a later unhookHud is not overwritten
    for _, w in ipairs(self.dbgWrapped or {}) do
        if w.cls[w.key] == w.wrapper then w.cls[w.key] = w.orig end
    end
    self.dbgWrapped = nil
    if self.dbgOrigRenderOverlay ~= nil then
        _G.renderOverlay = self.dbgOrigRenderOverlay
        self.dbgOrigRenderOverlay = nil
    end
end

function VRCompanion:setHudDiagnostics(enabled)
    self.hudDiagnostics = enabled == true
    self.dbgDraws, self.dbgOverwrites, self.dbgFillSig, self.dbgFillLogs, self.dbgMapSnaps = {}, {}, nil, 0, 0
    print(DBG_TAG .. "HUD diagnostics " .. (self.hudDiagnostics and "on" or "off"))
    self:dbgStopRenderProbe()
    if self.hudDiagnostics then
        self:dbgDump()
        local mission = g_currentMission or _G.g_currentMission
        local map = mission and mission.hud and (mission.hud.ingameMap or mission.hud.ingameMapDisplay)
        if map ~= nil then
            dbgLayoutGetters(map)
            self:dbgStartRenderProbe(map)
        end
    end
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
                    self:dbgDrawEntry(displayName, displaySelf) -- [DEBUG-hudgrp]
                    if displayName == "FillLevelsDisplay" then
                        self.activeFillLevelsDisplay = displaySelf
                        if self:getHudState() ~= self.HUD_STATE_HIDDEN and not self.is2DViewActive then
                            self:positionFillLevelsUnderSpeedometer(displaySelf)
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
                    if displayName == "IngameMap" then
                        if self.hudDiagnostics then -- [DEBUG-hudgrp]
                            self:dbgMapSnapshot(displaySelf, "before draw")
                            local mapBase = { self:getMapBasePosition(displaySelf) }
                            self.dbgMapBase = mapBase[1] ~= nil and string.format("(%s,%s)", dbgFmt(mapBase[1]), dbgFmt(mapBase[2])) or "engine"
                        end
                        local r = table.pack(self:callWithMapBasePosition(displaySelf, origDraw, ...))
                        if self.hudDiagnostics then self:dbgMapSnapshot(displaySelf, "after draw") end -- [DEBUG-hudgrp]
                        return table.unpack(r, 1, r.n)
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

    -- The map's layout reads its position from the engine's background position, not from the element
    local mapCls = getGlobalClass("IngameMap")
    if mapCls ~= nil then
        for _, methodName in ipairs(MAP_BASE_POSITION_METHODS) do
            if methodName ~= "draw" and type(mapCls[methodName]) == "function" then
                local origMethod = mapCls[methodName]
                hookMethod(mapCls, methodName, function(mapSelf, ...)
                    return self:callWithMapBasePosition(mapSelf, origMethod, ...)
                end)
            end
        end
        if type(mapCls.getBackgroundPosition) == "function" then
            local origBackgroundPosition = mapCls.getBackgroundPosition
            hookMethod(mapCls, "getBackgroundPosition", function(mapSelf, ...)
                local x, y = self:getMapBasePosition(mapSelf)
                if x ~= nil then return x, y end
                return origBackgroundPosition(mapSelf, ...)
            end)
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
VRCompanion.Hud.getHudGroupLayout = function(self, ...) return VRCompanion.getHudGroupLayout(VRCompanion, ...) end
VRCompanion.Hud.setHudGroupLayout = function(self, ...) return VRCompanion.setHudGroupLayout(VRCompanion, ...) end
VRCompanion.Hud.getHudBaseUiScale = function(self, ...) return VRCompanion.getHudBaseUiScale(VRCompanion, ...) end
VRCompanion.Hud.getHudGroupScale = function(self, ...) return VRCompanion.getHudGroupScale(VRCompanion, ...) end
VRCompanion.Hud.getHudDisplayScale = function(self, ...) return VRCompanion.getHudDisplayScale(VRCompanion, ...) end
VRCompanion.Hud.getHudDisplayPosition = function(self, ...) return VRCompanion.getHudDisplayPosition(VRCompanion, ...) end
VRCompanion.Hud.getFillLevelsBlockSize = function(self, ...) return VRCompanion.getFillLevelsBlockSize(VRCompanion, ...) end
VRCompanion.Hud.getHudGroupLabel = function(self, ...) return VRCompanion.getHudGroupLabel(VRCompanion, ...) end
VRCompanion.Hud.computeConvergenceDisparity = function(self, ...) return VRCompanion.computeConvergenceDisparity(VRCompanion, ...) end
VRCompanion.Hud.getConvergenceDisparity = function(self, ...) return VRCompanion.getConvergenceDisparity(VRCompanion, ...) end
VRCompanion.Hud.getConvergenceOffset = function(self, ...) return VRCompanion.getConvergenceOffset(VRCompanion, ...) end
VRCompanion.Hud.getFillLevelsDisplay = function(self, ...) return VRCompanion.getFillLevelsDisplay(VRCompanion, ...) end
VRCompanion.Hud.positionFillLevelsUnderSpeedometer = function(self, ...) return VRCompanion.positionFillLevelsUnderSpeedometer(VRCompanion, ...) end
VRCompanion.Hud.updateHudConvergence = function(self, ...) return VRCompanion.updateHudConvergence(VRCompanion, ...) end
VRCompanion.Hud.repositionHudForConvergence = function(self, ...) return VRCompanion.repositionHudForConvergence(VRCompanion, ...) end
VRCompanion.Hud.getHudState =function(self, ...) return VRCompanion.getHudState(VRCompanion, ...) end
VRCompanion.Hud.isDisplayVisible = function(self, ...) return VRCompanion.isDisplayVisible(VRCompanion, ...) end
VRCompanion.Hud.applyHudState = function(self, ...) return VRCompanion.applyHudState(VRCompanion, ...) end
VRCompanion.Hud.setHudState = function(self, ...) return VRCompanion.setHudState(VRCompanion, ...) end
VRCompanion.Hud.getEnabledHudGroups = function(self, ...) return VRCompanion.getEnabledHudGroups(VRCompanion, ...) end
VRCompanion.Hud.setHudGroupVisibility = function(self, ...) return VRCompanion.setHudGroupVisibility(VRCompanion, ...) end
VRCompanion.Hud.cycleHudState = function(self, ...) return VRCompanion.cycleHudState(VRCompanion, ...) end
VRCompanion.Hud.onHudToggleAction = function(self, ...) return VRCompanion.onHudToggleAction(VRCompanion, ...) end
VRCompanion.Hud.calculateHudAnchors = function(self, ...) return VRCompanion.calculateHudAnchors(VRCompanion, ...) end
VRCompanion.Hud.configureHudSafeArea = function(self, ...) return VRCompanion.configureHudSafeArea(VRCompanion, ...) end
VRCompanion.Hud.getHudSafeArea = function(self, ...) return VRCompanion.getHudSafeArea(VRCompanion, ...) end
VRCompanion.Hud.positionHudDisplay = function(self, ...) return VRCompanion.positionHudDisplay(VRCompanion, ...) end
VRCompanion.Hud.repositionAllHudDisplays = function(self, ...) return VRCompanion.repositionAllHudDisplays(VRCompanion, ...) end
VRCompanion.Hud.applyHudSafeArea = function(self, ...) return VRCompanion.applyHudSafeArea(VRCompanion, ...) end
VRCompanion.Hud.restoreHudSafeArea = function(self, ...) return VRCompanion.restoreHudSafeArea(VRCompanion, ...) end
VRCompanion.Hud.hookHud = function(self, ...) return VRCompanion.hookHud(VRCompanion, ...) end
VRCompanion.Hud.unhookHud = function(self, ...) return VRCompanion.unhookHud(VRCompanion, ...) end

return VRCompanion.Hud
