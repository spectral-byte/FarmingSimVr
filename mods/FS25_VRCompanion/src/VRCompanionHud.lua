VRCompanion = VRCompanion or {}

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

VRCompanion.HUD_LOAD_ALERT_TEXTS = {
    [0] = { key = "vrHud_loadAlert_hidden", english = "VR HUD: Hidden" },
    [1] = { key = "vrHud_loadAlert_streamlined", english = "VR HUD: Streamlined" },
    [2] = { key = "vrHud_loadAlert_full", english = "VR HUD: Full" },
}

-- What a notification names each HUD state and HUD Group by: its l10n key (modDesc.xml) and its English text.
local HUD_STATE_TEXTS = {
    [0] = { key = "vrHud_stateHidden", english = "Hidden" },
    [1] = { key = "vrHud_stateStreamlined", english = "Streamlined" },
    [2] = { key = "vrHud_stateFull", english = "Full" },
}
local HUD_GROUP_TEXTS = {
    map = { key = "vrHud_groupMap", english = "Map" },
    speedometer = { key = "vrHud_groupSpeedometer", english = "Speedometer" },
    info = { key = "vrHud_groupInfo", english = "Info" },
    controls = { key = "vrHud_groupControls", english = "Controls" },
}

function VRCompanion:getHudStateText(state)
    local text = HUD_STATE_TEXTS[state]
    if text == nil then return (self.HUD_STATE_NAMES and self.HUD_STATE_NAMES[state]) or tostring(state) end
    return VRCompanion.getLocalisedText(text.key, text.english)
end

function VRCompanion:getHudGroupText(groupName)
    local text = HUD_GROUP_TEXTS[groupName]
    if text == nil then return self:getHudGroupLabel(groupName) end
    return VRCompanion.getLocalisedText(text.key, text.english)
end

if VRCompanion.hudState == nil then VRCompanion.hudState = VRCompanion.HUD_STATE_HIDDEN end
if VRCompanion.hudSafeAreaHorizontal == nil then VRCompanion.hudSafeAreaHorizontal = VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL end
if VRCompanion.hudSafeAreaVertical == nil then VRCompanion.hudSafeAreaVertical = VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL end
if VRCompanion.hudConvergenceDepth == nil then VRCompanion.hudConvergenceDepth = VRCompanion.DEFAULT_HUD_DEPTH end
if VRCompanion.hudAnchorLeftBase == nil then VRCompanion.hudAnchorLeftBase = VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL end
if VRCompanion.hudAnchorRightBase == nil then VRCompanion.hudAnchorRightBase = 1.0 - VRCompanion.DEFAULT_HUD_SAFE_AREA_HORIZONTAL end
if VRCompanion.hudAnchorBottomBase == nil then VRCompanion.hudAnchorBottomBase = VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL end
if VRCompanion.hudAnchorTopBase == nil then VRCompanion.hudAnchorTopBase = 1.0 - VRCompanion.DEFAULT_HUD_SAFE_AREA_VERTICAL end
if VRCompanion.hudAnchorLeft == nil then VRCompanion.hudAnchorLeft = VRCompanion.hudAnchorLeftBase end
if VRCompanion.hudAnchorRight == nil then VRCompanion.hudAnchorRight = VRCompanion.hudAnchorRightBase end
if VRCompanion.hudAnchorBottom == nil then VRCompanion.hudAnchorBottom = VRCompanion.hudAnchorBottomBase end
if VRCompanion.hudAnchorTop == nil then VRCompanion.hudAnchorTop = VRCompanion.hudAnchorTopBase end
if VRCompanion.currentConvergenceOffset == nil then VRCompanion.currentConvergenceOffset = 0.0 end
VRCompanion.HUD_GROUPS = {
    map = { displays = { "IngameMap" }, alignX = "left", alignY = "bottom" },
    speedometer = { displays = { "SpeedMeterDisplay", "FillLevelsDisplay" }, alignX = "right", alignY = "bottom" },
    info = { displays = { "GameInfoDisplay" }, alignX = "right", alignY = "top" },
    controls = { displays = { "InputHelpDisplay" }, alignX = "left", alignY = "top" }
}
for _, name in ipairs(VRCompanion.HUD_GROUP_NAMES) do
    local group = VRCompanion.HUD_GROUPS[name]
    group.streamlined = VRCompanion.DEFAULT_HUD_GROUP_STREAMLINED[name]
    group.full = VRCompanion.DEFAULT_HUD_GROUP_FULL[name]
end

local CONTEXT_ACTION_DISPLAY = "ContextActionDisplay"
local CONTEXT_ACTION_ANCHOR_GROUP = "speedometer"

local function findHudGroup(displayName)
    for _, groupName in ipairs(VRCompanion.HUD_GROUP_NAMES) do
        local group = VRCompanion.HUD_GROUPS[groupName]
        for _, member in ipairs(group.displays) do
            if member == displayName then return group, groupName end
        end
    end
    return nil, nil
end

function VRCompanion:getHudGroupLayout(groupName)
    if self.hudGroupLayout == nil then self.hudGroupLayout = {} end
    local stored = self.hudGroupLayout[groupName]
    if stored ~= nil then
        return stored
    end
    local defaults = self.DEFAULT_HUD_GROUP_LAYOUTS and self.DEFAULT_HUD_GROUP_LAYOUTS[groupName]
    if defaults == nil then return nil end
    local layout = self:sanitizeHudGroupLayout(groupName, defaults.marginX, defaults.marginY, defaults.scale)
    self.hudGroupLayout[groupName] = layout
    return layout
end

-- Sets a group's margins, and its scale when one is given. Returns the clamped layout, or nil for
-- a name that is not a HUD Group.
function VRCompanion:setHudGroupLayout(groupName, marginX, marginY, scale)
    if self.HUD_GROUPS[groupName] == nil then return nil end
    if self.hudGroupLayout == nil then self.hudGroupLayout = {} end
    local current = self:getHudGroupLayout(groupName)
    local layout = self:sanitizeHudGroupLayout(groupName, marginX, marginY, scale or current.scale)
    self.hudGroupLayout[groupName] = layout
    return layout
end

function VRCompanion:getHudBaseUiScale()
    local gs = g_gameSettings or _G.g_gameSettings
    local gsClass = GameSettings or _G.GameSettings
    if gs ~= nil and type(gs.getValue) == "function" and gsClass ~= nil and gsClass.SETTING ~= nil and gsClass.SETTING.UI_SCALE ~= nil then
        return gs:getValue(gsClass.SETTING.UI_SCALE) or 1.0
    end
    return 1.0
end

function VRCompanion:getHudFovCompensation()
    local percent = self.fovOverscan or 0
    if percent <= 0 or self.is2DViewActive then return 1.0 end
    return 1.0 / (1.0 + percent / 100.0)
end

function VRCompanion:compensateHudEdgeDistance(distance)
    local compensation = self:getHudFovCompensation()
    if compensation == 1.0 then return distance end
    return 0.5 - (0.5 - distance) * compensation
end

function VRCompanion:getHudDepthScale()
    local depth = self.hudConvergenceDepth
    if self.is2DViewActive or type(depth) ~= "number" or depth ~= depth then return 1.0 end
    return self.HUD_LAYOUT_DEPTH / self:clamp(depth, self.MIN_HUD_DEPTH, self.MAX_HUD_DEPTH)
end

function VRCompanion:getHudGroupViewScale()
    return self:getHudFovCompensation() * self:getHudDepthScale()
end

function VRCompanion:placeHudGroupEdgeDistance(margin)
    local viewScale = self:getHudGroupViewScale()
    if viewScale == 1.0 then return margin end
    return self:clamp(0.5 - (0.5 - margin) * viewScale, 0.0, 0.5)
end

function VRCompanion:getHudGroupScale(groupName)
    return self:getHudBaseUiScale() * self:getHudGroupLayout(groupName).scale * self:getHudGroupViewScale()
end

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

function VRCompanion:getHudDisplayScale(displayName)
    local _, groupName = findHudGroup(displayName)
    return self:getHudGroupScale(groupName or CONTEXT_ACTION_ANCHOR_GROUP)
end

function VRCompanion:getHudDisplayPosition(display, displayName, convergenceOffset)
    local offset = convergenceOffset or 0.0
    local group, groupName = findHudGroup(displayName)
    if group == nil then
        return 0.5 + offset, self:placeHudGroupEdgeDistance(self:getHudGroupLayout(CONTEXT_ACTION_ANCHOR_GROUP).marginY)
    end
    local layout = self:getHudGroupLayout(groupName)
    local marginX = self:placeHudGroupEdgeDistance(layout.marginX)
    local marginY = self:placeHudGroupEdgeDistance(layout.marginY)
    local w, h
    if displayName == "FillLevelsDisplay" then
        w, h = self:getFillLevelsBlockSize(display)
    else
        w, h = getDisplaySize(display)
    end
    local x = (group.alignX == "right") and (1.0 - marginX - w) or marginX
    local y = (group.alignY == "top") and (1.0 - marginY - h) or marginY
    if displayName == "SpeedMeterDisplay" then
        local fillLevels = self:getFillLevelsDisplay()
        if fillLevels ~= nil then
            local _, fillHeight = self:getFillLevelsBlockSize(fillLevels)
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
        pcall(display.setIsVisible, display, isVisible)
    elseif type(display.setVisible) == "function" then
        pcall(display.setVisible, display, isVisible)
    end
end

local function renderedTanHalfFovX(self)
    local fovX = self.renderedFovX
    if type(fovX) ~= "number" or not (fovX > 0.01 and fovX < math.pi - 0.01) then return nil end
    return math.tan(fovX * 0.5)
end

function VRCompanion:computeConvergenceDisparity(ipd, depth)
    local targetDepth = depth or self.hudConvergenceDepth or self.DEFAULT_HUD_DEPTH
    if targetDepth == nil or targetDepth <= 0.001 then
        targetDepth = self.DEFAULT_HUD_DEPTH
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

    local compensation = self:getHudFovCompensation()
    if targetIpd == nil or targetIpd <= 0.001 then
        return self.DEFAULT_CONVERGENCE_DISPARITY * compensation
    end

    local clampedIpd = self:clamp(targetIpd, self.MIN_IPD, self.MAX_IPD)
    local tanHalfFovX = renderedTanHalfFovX(self)
    if tanHalfFovX ~= nil then
        return self:clamp(clampedIpd / (4.0 * targetDepth * tanHalfFovX), self.MIN_CONVERGENCE_DISPARITY, self.MAX_CONVERGENCE_DISPARITY)
    end
    local disparity = (clampedIpd / targetDepth) * self.CONVERGENCE_PARALLAX_FACTOR

    return self:clamp(disparity, self.MIN_CONVERGENCE_DISPARITY, self.MAX_CONVERGENCE_DISPARITY) * compensation
end

function VRCompanion:setHudConvergenceDepth(depth, save)
    if not VRCompanion.isFiniteNumber(depth) or depth < self.MIN_HUD_DEPTH or depth > self.MAX_HUD_DEPTH then
        return false
    end
    self.hudConvergenceDepth = depth
    if not self.is2DViewActive then self:repositionAllHudDisplays() end
    if save then self:saveConfiguration() end
    return true
end

function VRCompanion:consoleCommandSetHudDepth(arg)
    local usage = string.format("Usage: vrHudDepth [%.1f-%.1f] (metres, saved, %.1f by default)",
        self.MIN_HUD_DEPTH, self.MAX_HUD_DEPTH, self.DEFAULT_HUD_DEPTH)
    if arg == nil or arg == "" then
        return string.format("HUD depth: %g m. %s", self.hudConvergenceDepth or self.DEFAULT_HUD_DEPTH, usage)
    end
    local depth = VRCompanion.parseFiniteNumber(arg)
    if not self:setHudConvergenceDepth(depth, true) then return usage end
    print(string.format("[FS25_VR] HUD depth set to: %g m", depth))
    return string.format("HUD depth set to: %g m (Saved)", depth)
end

local HUD_TRACE_DEFAULT_FRAMES = 12
local HUD_TRACE_MAX_FRAMES = 120
local HUD_TRACE_UPDATES_PER_FRAME = 4
local HUD_TRACE_DISPLAYS = { "speedometer", "fillLevels", "info", "controls", "map", "context", "other" }
local hudTrace = { frames = 0, updates = 0, wrappedDraws = {} }

local function hudTraceDisplays(self)
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud == nil then return {} end
    return {
        speedometer = hud.speedMeter or hud.speedMeterDisplay,
        fillLevels = self:getFillLevelsDisplay(hud),
        info = hud.gameInfo or hud.gameInfoDisplay,
        controls = hud.inputHelp or hud.inputHelpDisplay,
        map = hud.ingameMap or hud.ingameMapDisplay,
        context = hud.contextAction or hud.contextActionDisplay,
    }
end

local function endTracedDraw(previous, ok, ...)
    hudTrace.current = previous
    if not ok then error((...), 0) end
    return ...
end

local function wrapHudTraceDraws(self)
    for name, display in pairs(hudTraceDisplays(self)) do
        if type(display) == "table" and type(display.draw) == "function" then
            local own = rawget(display, "draw")
            local original = display.draw
            local wrapper = function(instance, ...)
                local previous = hudTrace.current
                hudTrace.current = name
                return endTracedDraw(previous, pcall(original, instance, ...))
            end
            display.draw = wrapper
            table.insert(hudTrace.wrappedDraws, { display = display, own = own, wrapper = wrapper })
        end
    end
end

local function unwrapHudTraceDraws()
    for _, wrapped in ipairs(hudTrace.wrappedDraws) do
        if rawget(wrapped.display, "draw") == wrapped.wrapper then
            wrapped.display.draw = wrapped.own
        end
    end
    hudTrace.wrappedDraws = {}
    hudTrace.current = nil
end

local function hudTraceFrame()
    local bridge = VRBridge or (_G and _G.VRBridge)
    local function read(name)
        if bridge ~= nil and type(bridge[name]) == "function" then
            local ok, value = pcall(bridge[name])
            if ok and type(value) == "number" then return value end
        end
        return 0
    end
    local frame, subPass, eye = read("getFrameIndex"), read("getSubPassIndex"), read("getActiveEye")
    return frame * 2 + subPass, frame, subPass, eye
end

local function describeX(x)
    return (type(x) == "number") and string.format("%.4f", x) or "-"
end

local function flushHudTraceFrame()
    local f = hudTrace.rendered
    if f == nil then return end
    hudTrace.rendered = nil
    local parts = {}
    for _, name in ipairs(HUD_TRACE_DISPLAYS) do
        local range = f.byDisplay[name]
        if range ~= nil then
            table.insert(parts, string.format("%s %s..%s (%d)", name, describeX(range.min), describeX(range.max), range.count))
        end
    end
    print(string.format("[FS25_VR] HUD trace: rendered frame %d sub-pass %d eye %d: %s",
        f.frame, f.subPass, f.eye, (#parts > 0) and table.concat(parts, ", ") or "no overlays"))
    hudTrace.frames = hudTrace.frames - 1
    if hudTrace.frames <= 0 then VRCompanion:stopHudTrace() end
end

local function recordHudTraceOverlay(overlay)
    local key, frame, subPass, eye = hudTraceFrame()
    if hudTrace.rendered ~= nil and hudTrace.rendered.key ~= key then
        flushHudTraceFrame()
        if hudTrace.frames <= 0 then return end
    end
    local f = hudTrace.rendered
    if f == nil then
        f = { key = key, frame = frame, subPass = subPass, eye = eye, byDisplay = {} }
        hudTrace.rendered = f
    end
    local x, width = tonumber(overlay.x), tonumber(overlay.width) or 0
    if x == nil then return end
    if VRCompanion.mapDrawDepth > 0 and VRCompanion.mapHotspotDepth == 0 then
        x = x + (VRCompanion.mapDrawShiftX or 0.0)
    end
    local name = hudTrace.current or "other"
    local range = f.byDisplay[name]
    if range == nil then
        range = { min = x, max = x + width, count = 0 }
        f.byDisplay[name] = range
    end
    range.min = math.min(range.min, x)
    range.max = math.max(range.max, x + width)
    range.count = range.count + 1
end

function VRCompanion:isHudTracing()
    return hudTrace.frames > 0
end

function VRCompanion:startHudTrace(frames)
    local overlayClass = Overlay or (_G and _G.Overlay)
    if type(overlayClass) ~= "table" or type(overlayClass.render) ~= "function" then return false end
    self:stopHudTrace()
    hudTrace.frames = frames
    hudTrace.updates = frames * HUD_TRACE_UPDATES_PER_FRAME
    hudTrace.overlayClass = overlayClass
    local original = overlayClass.render
    hudTrace.originalRender = original
    hudTrace.wrapper = function(overlay, ...)
        if hudTrace.frames > 0 then pcall(recordHudTraceOverlay, overlay) end
        return original(overlay, ...)
    end
    overlayClass.render = hudTrace.wrapper
    wrapHudTraceDraws(self)
    local _, frame, subPass, eye = hudTraceFrame()
    print(string.format("[FS25_VR] HUD trace: start, %d frames; mode %s, HUD state %s, depth %g m, disparity %.4f, frame %d sub-pass %d eye %d",
        frames, (self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "Sequential Dual-Pass" or "AFR",
        tostring(self.HUD_STATE_NAMES[self:getHudState()]), self.hudConvergenceDepth or self.DEFAULT_HUD_DEPTH,
        self.currentConvergenceDisparity or 0.0, frame, subPass, eye))
    return true
end

function VRCompanion:stopHudTrace()
    local overlayClass = hudTrace.overlayClass
    if overlayClass ~= nil and overlayClass.render == hudTrace.wrapper then
        overlayClass.render = hudTrace.originalRender
    end
    unwrapHudTraceDraws()
    local wasTracing = hudTrace.overlayClass ~= nil
    hudTrace.frames, hudTrace.updates = 0, 0
    hudTrace.overlayClass, hudTrace.originalRender, hudTrace.wrapper, hudTrace.rendered = nil, nil, nil, nil
    if wasTracing then print("[FS25_VR] HUD trace: end") end
end

function VRCompanion:traceHudConvergenceUpdate(eye, offset)
    if hudTrace.frames <= 0 or hudTrace.updates <= 0 then return end
    hudTrace.updates = hudTrace.updates - 1
    local _, frame, subPass, activeEye = hudTraceFrame()
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local function displayX(display)
        if display == nil or type(display.getPosition) ~= "function" then return nil end
        local ok, x = pcall(display.getPosition, display)
        return ok and x or nil
    end
    local speedo, controls, info, map = nil, nil, nil, nil
    if hud ~= nil then
        speedo = displayX(hud.speedMeter or hud.speedMeterDisplay)
        controls = displayX(hud.inputHelp or hud.inputHelpDisplay)
        info = displayX(hud.gameInfo or hud.gameInfoDisplay)
        map = displayX(hud.ingameMap or hud.ingameMapDisplay)
    end
    print(string.format("[FS25_VR] HUD trace: update frame %d sub-pass %d active eye %d, placed for eye %s at offset %+.4f: speedometer x=%s controls x=%s info x=%s map x=%s",
        frame, subPass, activeEye, tostring(eye), offset or 0.0, describeX(speedo), describeX(controls), describeX(info), describeX(map)))
    if hudTrace.updates <= 0 and hudTrace.rendered == nil then
        print("[FS25_VR] HUD trace: no overlays were rendered during the trace")
        self:stopHudTrace()
    end
end

function VRCompanion:consoleCommandHudTrace(arg)
    local usage = string.format("Usage: vrHudTrace [frames] (1-%d, %d by default): logs to log.txt where the HUD is drawn for each eye",
        HUD_TRACE_MAX_FRAMES, HUD_TRACE_DEFAULT_FRAMES)
    local frames = HUD_TRACE_DEFAULT_FRAMES
    if arg ~= nil and arg ~= "" then
        frames = VRCompanion.parseFiniteNumber(arg, 1, HUD_TRACE_MAX_FRAMES, true)
        if frames == nil then return usage end
    end
    if not self:startHudTrace(frames) then
        return "HUD trace unavailable: the game's Overlay class was not found"
    end
    return string.format("HUD trace started for %d rendered frames; read log.txt", frames)
end

VRCompanion.HUD_EYE_TEST_SHIFT = 0.15
local HUD_EYE_TEST_EYES = { left = 0, right = 1 }

function VRCompanion:consoleCommandHudEyeTest(arg)
    local usage = "Usage: vrHudEyeTest [left|right|off]: moves the HUD far right in the frames rendered for that eye only"
    local name = (arg ~= nil) and string.lower(tostring(arg)) or ""
    if name == "" then
        local current = "off"
        for eyeName, eye in pairs(HUD_EYE_TEST_EYES) do
            if self.hudEyeTestEye == eye then current = eyeName end
        end
        return string.format("HUD eye test: %s. %s", current, usage)
    end
    if name == "off" then
        self.hudEyeTestEye = nil
        print("[FS25_VR] HUD eye test: off")
        return "HUD eye test: off"
    end
    local eye = HUD_EYE_TEST_EYES[name]
    if eye == nil then return usage end
    self.hudEyeTestEye = eye
    print(string.format("[FS25_VR] HUD eye test: the HUD is moved right by %.2f of the screen in the frames rendered for the %s eye (eye %d)",
        self.HUD_EYE_TEST_SHIFT, name, eye))
    return string.format("HUD eye test: %s. Close one eye at a time: only the %s eye should show the HUD moved right", name, name)
end

function VRCompanion:consoleCommandEyeSnapshot()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.snapshotEyes) ~= "function" then
        return "Eye snapshot unavailable: this Injector cannot write eye snapshots"
    end
    bridge.snapshotEyes()
    print("[FS25_VR] Eye snapshot requested")
    return "Eye snapshot requested: fs25_vr_eye0.bmp (left) and fs25_vr_eye1.bmp (right) are written next to fs25_vr.log"
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

local lastFillLevelsPlacement = {
    display = nil,
    frameTicks = nil,
    scale = nil,
    offset = nil,
    marginX = nil,
    marginY = nil,
    w = nil,
    h = nil,
    x = nil,
    y = nil,
    sm = nil,
}

local function isFillLevelsPlacementCurrent(self, display)
    if lastFillLevelsPlacement.display ~= display then return false end
    if lastFillLevelsPlacement.frameTicks ~= self.frameTicks then return false end
    local uiScale = self:getHudGroupScale("speedometer")
    if display.vrUiScale ~= uiScale or lastFillLevelsPlacement.scale ~= uiScale then return false end
    local currentOffset = self.currentConvergenceOffset or 0.0
    if lastFillLevelsPlacement.offset ~= currentOffset then return false end
    local layout = self:getHudGroupLayout("speedometer")
    if lastFillLevelsPlacement.marginX ~= layout.marginX or lastFillLevelsPlacement.marginY ~= layout.marginY then return false end
    if display.x ~= lastFillLevelsPlacement.x or display.y ~= lastFillLevelsPlacement.y then return false end
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local sm = (hud ~= nil) and (hud.speedMeter or hud.speedMeterDisplay) or nil
    if lastFillLevelsPlacement.sm ~= sm then return false end
    local w, h = self:getFillLevelsBlockSize(display)
    if lastFillLevelsPlacement.w ~= w or lastFillLevelsPlacement.h ~= h then return false end
    return true
end

function VRCompanion:positionFillLevelsUnderSpeedometer(display, offset)
    if display == nil then return end

    local currentOffset = offset or self.currentConvergenceOffset or 0.0
    local uiScale = self:getHudGroupScale("speedometer")
    if display.vrUiScale ~= uiScale then
        display.vrUiScale = uiScale
        if type(display.setScale) == "function" then
            pcall(display.setScale, display, uiScale)
        end
    end

    local flPosX, fillBottom = self:getHudDisplayPosition(display, "FillLevelsDisplay", currentOffset)

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

    local layout = self:getHudGroupLayout("speedometer")
    local w, h = self:getFillLevelsBlockSize(display)
    lastFillLevelsPlacement.display = display
    lastFillLevelsPlacement.frameTicks = self.frameTicks
    lastFillLevelsPlacement.scale = uiScale
    lastFillLevelsPlacement.offset = currentOffset
    lastFillLevelsPlacement.marginX = layout.marginX
    lastFillLevelsPlacement.marginY = layout.marginY
    lastFillLevelsPlacement.w = w
    lastFillLevelsPlacement.h = h
    lastFillLevelsPlacement.x = flPosX
    lastFillLevelsPlacement.y = fillBottom
    lastFillLevelsPlacement.sm = sm

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
    local isTrackingActive = self.headTrackingEnabled ~= false

    local targetDisparity = 0.0
    if not isPassThrough and isTrackingActive and not self:isMenuQuadShown() and not self.is2DViewActive then
        local rawIpd = self:getIpd()
        targetDisparity = self:computeConvergenceDisparity(rawIpd, self.hudConvergenceDepth)
    end

    if self.currentConvergenceDisparity == nil or dt == nil then
        self.currentConvergenceDisparity = targetDisparity
    elseif type(dt) == "number" and dt > 0 and dt == dt then
        local dtSec = (dt > 1.0) and (dt * 0.001) or dt
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
    if self.hudEyeTestEye ~= nil and eyeOrParity == self.hudEyeTestEye then
        offset = offset + self.HUD_EYE_TEST_SHIFT
    end

    self.currentConvergenceOffset = offset

    local baseLeft = self:compensateHudEdgeDistance(self.hudAnchorLeftBase or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL)
    local baseRight = 1.0 - self:compensateHudEdgeDistance(1.0 - (self.hudAnchorRightBase or (1.0 - self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL)))
    local baseBottom = self:compensateHudEdgeDistance(self.hudAnchorBottomBase or self.DEFAULT_HUD_SAFE_AREA_VERTICAL)
    local baseTop = 1.0 - self:compensateHudEdgeDistance(1.0 - (self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_SAFE_AREA_VERTICAL)))

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

    if self:getHudState() ~= self.HUD_STATE_HIDDEN then
        self:repositionHudForConvergence()
    end
    if self:isHudTracing() then
        self:traceHudConvergenceUpdate(eyeOrParity, offset)
    end

    return shiftedLeft, shiftedRight, offset
end

local function placeDisplayAtOffset(self, display, displayName, offset)
    if display == nil or type(display.setPosition) ~= "function" then return end
    local x, y = self:getHudDisplayPosition(display, displayName, offset)
    pcall(display.setPosition, display, x, y)
end

function VRCompanion:repositionHudForConvergence()
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud == nil then return end

    local offset = self.currentConvergenceOffset or 0.0

    local fl = self:getFillLevelsDisplay(hud)

    if fl ~= nil then
        self:positionFillLevelsUnderSpeedometer(fl, offset)
    else
        placeDisplayAtOffset(self, hud.speedMeter or hud.speedMeterDisplay, "SpeedMeterDisplay", offset)
    end
    placeDisplayAtOffset(self, hud.inputHelp or hud.inputHelpDisplay, "InputHelpDisplay", offset)
    placeDisplayAtOffset(self, hud.gameInfo or hud.gameInfoDisplay, "GameInfoDisplay", offset)
    placeDisplayAtOffset(self, hud.ingameMap or hud.ingameMapDisplay, "IngameMap", offset)
    placeDisplayAtOffset(self, hud.contextAction or hud.contextActionDisplay, CONTEXT_ACTION_DISPLAY, offset)
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
        pcall(hud.setIsVisible, hud, isHudVisible)
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
    local inputHelpVis = self:isDisplayVisible("InputHelpDisplay", state) and self.controlsExpanded ~= false

    for _, entry in ipairs(displays) do
        local vis = self:isDisplayVisible(entry.name, state)
        if entry.name == "InputHelpDisplay" then vis = inputHelpVis end
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

    local ingameMapVis = self:isDisplayVisible("IngameMap", state)
    local gameInfoVis = self:isDisplayVisible("GameInfoDisplay", state)

    if type(hud.setInputHelpVisible) == "function" then
        pcall(hud.setInputHelpVisible, hud, inputHelpVis)
    end
    if type(hud.setIngameMapVisible) == "function" then
        pcall(hud.setIngameMapVisible, hud, ingameMapVis)
    end
    if type(hud.setGameInfoVisible) == "function" then
        pcall(hud.setGameInfoVisible, hud, gameInfoVis)
    end
end

function VRCompanion:setHudState(newState)
    local state = self:clamp(newState or self.HUD_STATE_HIDDEN, self.HUD_STATE_HIDDEN, self.HUD_STATE_FULL)
    self.hudState = state
    self:applyHudState(state)
    if state ~= self.HUD_STATE_HIDDEN and not self.is2DViewActive then
        self:repositionHudForConvergence()
    end
    return self.hudState
end

function VRCompanion:cycleHudState()
    local curTime = self:getCurrentTimeMs()
    if curTime ~= nil and self.lastHudToggleTime ~= nil then
        local elapsed = curTime - self.lastHudToggleTime
        if elapsed >= 0 and elapsed < 300 then
            return self:getHudState()
        end
    end
    self.lastHudToggleTime = curTime

    local curState = self:getHudState()
    local nextState = (curState + 1) % 3
    self:setHudState(nextState)
    self.persistedHudState = nextState
    self:saveConfiguration()
    local stateName = (self.HUD_STATE_NAMES and self.HUD_STATE_NAMES[nextState]) or tostring(nextState)
    print(string.format("[FS25_VR] HUD state changed: %s (%d)", stateName, nextState))

    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        local stateText = self:getHudStateText(nextState)
        local message = VRCompanion.formatLocalisedText("vrHud_stateChanged", "VR HUD: %s", stateText)
        if nextState ~= self.HUD_STATE_HIDDEN then
            local groupLabels = {}
            for _, name in ipairs(self:getEnabledHudGroups(nextState)) do
                table.insert(groupLabels, self:getHudGroupText(name))
            end
            local groups = (#groupLabels > 0) and table.concat(groupLabels, ", ")
                or VRCompanion.getLocalisedText("vrHud_noGroups", "none")
            message = VRCompanion.formatLocalisedText("vrHud_stateChangedWithGroups", "VR HUD: %s (%s)", stateText, groups)
        end
        pcall(function()
            mission:showBlinkingWarning(message, 2500)
        end)
    end

    return nextState
end

function VRCompanion:getHudGroupLabel(groupName)
    return (string.gsub(groupName, "^%l", string.upper))
end

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

function VRCompanion:setHudGroupVisibility(groupName, stateName, enabled)
    local group = self.HUD_GROUPS[groupName]
    if group == nil or (stateName ~= "streamlined" and stateName ~= "full") then
        return false
    end
    group[stateName] = (enabled == true)
    self:applyHudState(self:getHudState())
    return true
end

function VRCompanion:rememberControlsExpanded(isExpanded)
    if type(isExpanded) ~= "boolean" or not self:isDisplayVisible("InputHelpDisplay") then
        return
    end
    local menuOpen = self:doesMenuTakeInput()
    local gui = g_gui or _G.g_gui
    if gui ~= nil and type(gui.getIsGuiVisible) == "function" then
        local ok, visible = pcall(gui.getIsGuiVisible, gui)
        if ok and visible == true then menuOpen = true end
    end
    if menuOpen then return end
    if self.controlsExpanded == isExpanded then return end
    self.controlsExpanded = isExpanded
    self:saveConfiguration()
end

function VRCompanion:onHudToggleAction(...)
    return self:cycleHudState()
end

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
        convergenceDepth = self.hudConvergenceDepth or self.DEFAULT_HUD_DEPTH,
        showHudLoadNotification = (self.showHudLoadNotification == true)
    }
end

function VRCompanion:positionHudDisplay(display, displayName)
    if display == nil then return end

    if displayName == "FillLevelsDisplay" then
        self:positionFillLevelsUnderSpeedometer(display)
        return
    end

    local scale = self:getHudDisplayScale(displayName)

    if type(display.setScale) == "function" then
        pcall(display.setScale, display, scale)
    end

    local anchorX, anchorY = self:getHudDisplayPosition(display, displayName, self.currentConvergenceOffset)

    display.anchorX = anchorX
    display.anchorY = anchorY
    display.anchorLeft = self.hudAnchorLeft
    display.anchorRight = self.hudAnchorRight
    display.anchorBottom = self.hudAnchorBottom
    display.anchorTop = self.hudAnchorTop
    display.vrUiScale = scale

    if type(display.setPosition) == "function" then
        pcall(display.setPosition, display, anchorX, anchorY)
    end
    if type(display.storeScaledValues) == "function" then
        pcall(display.storeScaledValues, display)
    end
    if type(display.reposition) == "function" then
        pcall(display.reposition, display)
    end
    if type(display.updatePosition) == "function" then
        pcall(display.updatePosition, display)
    end
end

function VRCompanion:repositionAllHudDisplays(hud)
    local targetHud = hud
    if targetHud == nil then
        local mission = g_currentMission or _G.g_currentMission
        targetHud = (mission ~= nil) and mission.hud or nil
    end
    if targetHud == nil then return end

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
        pcall(targetHud.storeScaledValues, targetHud)
    end
    if type(targetHud.updateDimensions) == "function" then
        pcall(targetHud.updateDimensions, targetHud)
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

local function restoreHudDisplay(display, restoredScale)
    if display == nil then return end
    display.vrUiScale = nil
    if type(display.setScale) == "function" then
        pcall(display.setScale, display, restoredScale)
    end
    if type(display.storeScaledValues) == "function" then
        pcall(display.storeScaledValues, display)
    end
    if type(display.reposition) == "function" then
        pcall(display.reposition, display)
    end
    if type(display.updatePosition) == "function" then
        pcall(display.updatePosition, display)
    end
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
    lastFillLevelsPlacement.display = nil

    local restoredScale = self.origUiScale or 1.0
    if not preserveOrigAnchors then
        self.origUiScale = nil
    end

    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    if hud ~= nil then
        local fillLevels = self:getFillLevelsDisplay(hud)
        local speedMeter = hud.speedMeter or hud.speedMeterDisplay
        restoreHudDisplay(speedMeter, restoredScale)
        restoreHudDisplay(hud.inputHelp or hud.inputHelpDisplay, restoredScale)
        restoreHudDisplay(hud.gameInfo or hud.gameInfoDisplay, restoredScale)
        restoreHudDisplay(hud.contextAction or hud.contextActionDisplay, restoredScale)
        restoreHudDisplay(hud.ingameMap or hud.ingameMapDisplay, restoredScale)
        if fillLevels ~= nil and fillLevels ~= speedMeter then
            restoreHudDisplay(fillLevels, restoredScale)
        end
        if type(hud.storeScaledValues) == "function" then
            pcall(hud.storeScaledValues, hud)
        end
    end
    print("[FS25_VR] Restored standard 2D HUD corner anchors.")
end

local MAP_DRAW_CLASSES = { "IngameMap", "IngameMapLayout", "IngameMapLayoutNone", "IngameMapLayoutCircle",
    "IngameMapLayoutSquare", "IngameMapLayoutSquareLarge", "IngameMapLayoutFullscreen" }

VRCompanion.mapDrawDepth = 0
VRCompanion.mapHotspotDepth = 0
VRCompanion.mapDrawShiftX = 0.0
VRCompanion.mapDrawShiftY = 0.0

function VRCompanion:getMapDrawShift()
    local mission = g_currentMission or _G.g_currentMission
    local hud = (mission ~= nil) and mission.hud or nil
    local map = hud and (hud.ingameMap or hud.ingameMapDisplay)
    if map == nil or map.isFullscreen == true then return nil end
    if self.is2DViewActive or self:getHudState() == self.HUD_STATE_HIDDEN then return nil end
    local x, y = self:getHudDisplayPosition(map, "IngameMap", self.currentConvergenceOffset)
    local baseX = g_safeFrameOffsetX or (_G and _G.g_safeFrameOffsetX) or 0.0
    local baseY = g_safeFrameOffsetY or (_G and _G.g_safeFrameOffsetY) or 0.0
    return x - baseX, y - baseY
end

local function finishMapDraw(self, ok, ...)
    self.mapDrawDepth = self.mapDrawDepth - 1
    if not ok then error((...), 0) end
    return ...
end

function VRCompanion:runMapDraw(fn, ...)
    if self.mapDrawDepth > 0 then
        self.mapDrawDepth = self.mapDrawDepth + 1
    else
        local dx, dy = self:getMapDrawShift()
        self.mapDrawShiftX, self.mapDrawShiftY = dx or 0.0, dy or 0.0
        self.mapDrawDepth = 1
    end
    return finishMapDraw(self, pcall(fn, ...))
end

local function finishMapLayoutShift(layout, origX, origY, ok, ...)
    layout.mapPosX, layout.mapPosY = origX, origY
    if not ok then error((...), 0) end
    return ...
end

function VRCompanion:callWithMapLayoutShifted(layout, fn, ...)
    local dx, dy = self.mapDrawShiftX, self.mapDrawShiftY
    if (dx == 0.0 and dy == 0.0) or type(layout) ~= "table" or type(layout.mapPosX) ~= "number" or type(layout.mapPosY) ~= "number" then
        return fn(layout, ...)
    end
    local origX, origY = layout.mapPosX, layout.mapPosY
    layout.mapPosX, layout.mapPosY = origX + dx, origY + dy
    return finishMapLayoutShift(layout, origX, origY, pcall(fn, layout, ...))
end

local function finishHotspotDraw(self, ok, ...)
    self.mapHotspotDepth = self.mapHotspotDepth - 1
    if not ok then error((...), 0) end
    return ...
end

local function runHotspotDraw(self, origMethod, ...)
    self.mapHotspotDepth = self.mapHotspotDepth + 1
    return finishHotspotDraw(self, pcall(origMethod, ...))
end

local function finishOverlayRender(overlay, origX, origY, ok, ...)
    overlay.x, overlay.y = origX, origY
    if not ok then error((...), 0) end
    return ...
end

local hudWraps = VRCompanion.GameWraps.newGroup({ weakTargets = true })

function VRCompanion:hookHud()
    if self.hudHooked then
        self:unhookHud()
    end

    local hookedCount = 0

    local function hookMethod(tbl, methodName, makeFn)
        if hudWraps:wrap(tbl, methodName, makeFn) then
            hookedCount = hookedCount + 1
        end
    end

    for _, info in ipairs(HUD_HOOKED_CLASSES) do
        local cls = getGlobalClass(info.className)
        if cls ~= nil then
            local displayName = info.displayName

            hookMethod(cls, "draw", function(origDraw, entry)
                return function(displaySelf, ...)
                    if not entry.active then
                        return origDraw(displaySelf, ...)
                    end
                    if displayName == "FillLevelsDisplay" then
                        self.activeFillLevelsDisplay = displaySelf
                        if self:getHudState() ~= self.HUD_STATE_HIDDEN and not self.is2DViewActive
                            and not isFillLevelsPlacementCurrent(self, displaySelf) then
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
                        return self:runMapDraw(origDraw, displaySelf, ...)
                    end
                    return origDraw(displaySelf, ...)
                end
            end)

            hookMethod(cls, "getIsVisible", function(origGetIsVisible, entry)
                return function(displaySelf, ...)
                    if not entry.active then
                        return origGetIsVisible(displaySelf, ...)
                    end
                    if displayName == "FillLevelsDisplay" then
                        self.activeFillLevelsDisplay = displaySelf
                    end
                    if not self:isDisplayVisible(displayName) then
                        return false
                    end
                    return origGetIsVisible(displaySelf, ...)
                end
            end)

            if displayName == "InputHelpDisplay" then
                hookMethod(cls, "setVisible", function(origSetVisible, entry)
                    return function(displaySelf, isVisible, ...)
                        if not entry.active then
                            return origSetVisible(displaySelf, isVisible, ...)
                        end
                        self:rememberControlsExpanded(isVisible)
                        return origSetVisible(displaySelf, isVisible, ...)
                    end
                end)
            end
        end
    end

    self.mapDrawDepth = 0
    self.mapHotspotDepth = 0
    for _, className in ipairs(MAP_DRAW_CLASSES) do
        local cls = getGlobalClass(className)
        if cls ~= nil then
            local drawMethods = {}
            for key, value in pairs(cls) do
                if type(key) == "string" and string.sub(key, 1, 4) == "draw" and type(value) == "function"
                    and not (className == "IngameMap" and key == "draw") then
                    drawMethods[#drawMethods + 1] = key
                end
            end
            for _, key in ipairs(drawMethods) do
                if className ~= "IngameMap" and (key == "drawBefore" or key == "drawCoordinates" or key == "drawLatency") then
                    hookMethod(cls, key, function(origMethod, entry)
                        return function(layout, ...)
                            if not entry.active then
                                return origMethod(layout, ...)
                            end
                            return self:runMapDraw(self.callWithMapLayoutShifted, self, layout, origMethod, ...)
                        end
                    end)
                elseif className == "IngameMap" and (string.find(key, "Hotspot", 1, true) or string.find(key, "PointsOfInterest", 1, true)) then
                    hookMethod(cls, key, function(origMethod, entry)
                        return function(...)
                            if not entry.active then
                                return origMethod(...)
                            end
                            return self:runMapDraw(runHotspotDraw, self, origMethod, ...)
                        end
                    end)
                else
                    hookMethod(cls, key, function(origMethod, entry)
                        return function(...)
                            if not entry.active then
                                return origMethod(...)
                            end
                            return self:runMapDraw(origMethod, ...)
                        end
                    end)
                end
            end
            hookMethod(cls, "getMapObjectPosition", function(origObjectPosition, entry)
                return function(layout, ...)
                    local x, y, rotation, visible = origObjectPosition(layout, ...)
                    if entry.active and self.mapDrawDepth > 0 and visible and type(x) == "number" and type(y) == "number" then
                        x, y = x + self.mapDrawShiftX, y + self.mapDrawShiftY
                    end
                    return x, y, rotation, visible
                end
            end)
        end
    end
    local overlayCls = getGlobalClass("Overlay")
    if overlayCls ~= nil then
        hookMethod(overlayCls, "render", function(origRender, entry)
            return function(overlay, clipX1, clipY1, clipX2, clipY2, ...)
                if not entry.active then
                    return origRender(overlay, clipX1, clipY1, clipX2, clipY2, ...)
                end
                local dx, dy = self.mapDrawShiftX, self.mapDrawShiftY
                if self.mapDrawDepth > 0 and self.mapHotspotDepth == 0 and (dx ~= 0.0 or dy ~= 0.0) and type(overlay) == "table"
                    and type(overlay.x) == "number" and type(overlay.y) == "number" then
                    local origX, origY = overlay.x, overlay.y
                    overlay.x, overlay.y = origX + dx, origY + dy
                    if clipX1 ~= nil and clipY1 ~= nil and clipX2 ~= nil and clipY2 ~= nil then
                        clipX1, clipY1, clipX2, clipY2 = clipX1 + dx, clipY1 + dy, clipX2 + dx, clipY2 + dy
                    end
                    return finishOverlayRender(overlay, origX, origY, pcall(origRender, overlay, clipX1, clipY1, clipX2, clipY2, ...))
                end
                return origRender(overlay, clipX1, clipY1, clipX2, clipY2, ...)
            end
        end)
    end

    local hudCls = getGlobalClass("HUD")
    local mission = g_currentMission or (_G and _G.g_currentMission)
    local hudInstance = (mission ~= nil) and mission.hud or nil
    local hudTargets = {}
    if hudCls ~= nil then table.insert(hudTargets, hudCls) end
    if hudInstance ~= nil and hudInstance ~= hudCls then table.insert(hudTargets, hudInstance) end

    for _, target in ipairs(hudTargets) do
        hookMethod(target, "draw", function(origHudDraw, entry)
            return function(hudSelf, ...)
                if entry.active and self:getHudState() == self.HUD_STATE_HIDDEN then
                    return
                end
                return origHudDraw(hudSelf, ...)
            end
        end)

        hookMethod(target, "setIsVisible", function(origSetIsVisible, entry)
            return function(hudSelf, isVisible, ...)
                if entry.active and self:getHudState() == self.HUD_STATE_HIDDEN then
                    return origSetIsVisible(hudSelf, false, ...)
                end
                return origSetIsVisible(hudSelf, isVisible, ...)
            end
        end)

        local hudSetterHooks = {
            { methodName = "setInputHelpVisible", displayName = "InputHelpDisplay" },
            { methodName = "setIngameMapVisible", displayName = "IngameMap" },
            { methodName = "setGameInfoVisible", displayName = "GameInfoDisplay" }
        }
        for _, setter in ipairs(hudSetterHooks) do
            local mName = setter.methodName
            local dName = setter.displayName
            hookMethod(target, mName, function(origSetter, entry)
                return function(hudSelf, isVisible, ...)
                    if not entry.active then
                        return origSetter(hudSelf, isVisible, ...)
                    end
                    if dName == "InputHelpDisplay" then
                        self:rememberControlsExpanded(isVisible)
                    end
                    if not self:isDisplayVisible(dName) then
                        return origSetter(hudSelf, false, ...)
                    end
                    return origSetter(hudSelf, isVisible, ...)
                end
            end)
        end
    end

    if hookedCount > 0 then
        self.hudHooked = true
        print("[FS25_VR] Hooked native engine HUD display interfaces.")
    end
end

function VRCompanion:unhookHud()
    self:stopHudTrace()
    hudWraps:liftAll()
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

VRCompanion.DEFAULT_STEREO_DIALOG_DEPTH = 2.5
VRCompanion.stereoDialogDepth = VRCompanion.stereoDialogDepth or VRCompanion.DEFAULT_STEREO_DIALOG_DEPTH

local STEREO_DIALOG_DEPTH_KEY = "vrSettings.stereoDialogDepth"

function VRCompanion:getStereoDialogDrawShift(gui)
    if self.is2DViewActive or not self:isStereoDialogShowing() then return nil end
    if (self.currentConvergenceDisparity or 0.0) == 0.0 then return nil end
    local root = self:findStereoDialog(gui)
    if type(root) ~= "table" or type(root.setAbsolutePosition) ~= "function" or type(root.absPosition) ~= "table" then
        return nil
    end
    local disparity = self:computeConvergenceDisparity(self:getIpd(), self.stereoDialogDepth or self.DEFAULT_STEREO_DIALOG_DEPTH)
    local offset = self:getConvergenceOffset(nil, disparity)
    if type(offset) ~= "number" or offset == 0.0 then return nil end
    return root, offset
end

function VRCompanion:setStereoDialogDepth(depth, save)
    if not VRCompanion.isFiniteNumber(depth) or depth < self.MIN_CONVERGENCE_DEPTH or depth > self.MAX_CONVERGENCE_DEPTH then return false end
    self.stereoDialogDepth = depth
    if save then self:saveConfiguration() end
    return true
end

function VRCompanion:saveStereoDialogDepthSetting(xmlFile, xml)
    if type(xml.setFloat) == "function" then
        xml.setFloat(xmlFile, STEREO_DIALOG_DEPTH_KEY, self.stereoDialogDepth or self.DEFAULT_STEREO_DIALOG_DEPTH)
    end
end

function VRCompanion:loadStereoDialogDepthSetting(xmlFile, xml)
    if type(xml.getFloat) ~= "function" then return end
    self:setStereoDialogDepth(xml.getFloat(xmlFile, STEREO_DIALOG_DEPTH_KEY))
end

function VRCompanion:consoleCommandSetStereoDialogDepth(arg)
    local usage = string.format("Usage: vrDialogDepth [%.1f-%.1f] (metres, saved)", self.MIN_CONVERGENCE_DEPTH, self.MAX_CONVERGENCE_DEPTH)
    if arg == nil or arg == "" then
        return string.format("Dialog depth: %.1f m. %s", self.stereoDialogDepth or self.DEFAULT_STEREO_DIALOG_DEPTH, usage)
    end
    local depth = VRCompanion.parseFiniteNumber(arg)
    if not self:setStereoDialogDepth(depth, true) then return usage end
    print(string.format("[FS25_VR] Dialog depth set to: %.1f m", depth))
    return string.format("Dialog depth set to: %.1f m (Saved)", depth)
end

local function shiftGuiRoot(root, offset)
    root:setAbsolutePosition(root.absPosition[1] + offset, root.absPosition[2])
end

local function finishGuiDrawAtDepth(root, offset, ok, ...)
    pcall(shiftGuiRoot, root, -offset)
    if not ok then error((...), 0) end
    return ...
end

function VRCompanion:drawGuiAtHudDepth(original, gui, ...)
    local root, offset = self:getStereoDialogDrawShift(gui)
    if root == nil then return original(gui, ...) end
    if not pcall(shiftGuiRoot, root, offset) then return original(gui, ...) end
    return finishGuiDrawAtDepth(root, offset, pcall(original, gui, ...))
end

function VRCompanion:hookGuiDraw()
    local class = Gui or (_G and _G.Gui)
    if type(class) ~= "table" or type(class.draw) ~= "function" then return end
    local original = class.draw
    local wrapper = function(gui, ...) return self:drawGuiAtHudDepth(original, gui, ...) end
    class.draw = wrapper
    self.guiDrawHook = { class = class, original = original, wrapper = wrapper }
end

function VRCompanion:unhookGuiDraw()
    local hook = self.guiDrawHook
    if hook ~= nil and hook.class.draw == hook.wrapper then hook.class.draw = hook.original end
    self.guiDrawHook = nil
end

VRCompanion:registerSubsystem("hud", {
    updateBeforeActions = function(self, dt) self:updateHudConvergence(dt) end,
    mapLoad = function(self)
        self:applyHudSafeArea()
        self:setHudState(self.persistedHudState or self.DEFAULT_HUD_STATE)
    end,
    mapUnload = function(self)
        self:setHudState(self.HUD_STATE_FULL)
        self:restoreHudSafeArea()
        self.currentConvergenceOffset = 0.0
        self.currentConvergenceDisparity = 0.0
        if self.DEFAULT_HUD_DEPTH ~= nil then
            self.hudConvergenceDepth = self.DEFAULT_HUD_DEPTH
        end
        self.lastHudToggleTime = nil
        self.activeFillLevelsDisplay = nil
        lastFillLevelsPlacement.display = nil
        self:stopHudTrace()
        self.hudEyeTestEye = nil
        self.hasLoggedFillLevelsDisplayInfo = false
    end,
    saveSettings = function(self, xmlFile, xml) self:saveStereoDialogDepthSetting(xmlFile, xml) end,
    loadSettings = function(self, xmlFile, xml) self:loadStereoDialogDepthSetting(xmlFile, xml) end,
    resetSettings = function(self) self.stereoDialogDepth = self.DEFAULT_STEREO_DIALOG_DEPTH end,
    consoleCommands = {
        { name = "vrDialogDepth", description = "How far back a Stereo Dialog (the fish catch dialog) reads in the headset, in metres (saved; 2.5 by default; the HUD is at 2.5 by default): vrDialogDepth [0.5-5.0]", method = "consoleCommandSetStereoDialogDepth" },
    },
})