-- ====================================================================
-- FS25 VR Companion Mod: Input Subsystem
-- Manages in-engine action events and console commands. All key and
-- gamepad bindings come from modDesc.xml via g_inputBinding, so players
-- rebind them in the game's Controls menu.
-- ====================================================================

VRCompanion = VRCompanion or {}

-- The input context the VR actions belong in: the vehicle's while controlling one, otherwise the player's on foot.
-- registerActionEvent adds to the context that is current, and at map load that is the loading screen's menu context,
-- which gameplay never uses: events registered there exist in g_inputBinding.events but never fire.
function VRCompanion:getActionEventsContextName()
    if type(self.getControlledVehicle) == "function" and self:getControlledVehicle() ~= nil then
        return (Vehicle ~= nil and Vehicle.INPUT_CONTEXT_NAME) or "VEHICLE"
    end
    return (PlayerInputComponent ~= nil and PlayerInputComponent.INPUT_CONTEXT_NAME) or "PLAYER"
end

-- The VR action events, one row each: the field holding the registered event id, the input action, the handler
-- (named, so it is looked up on VRCompanion when registering), the i18n text key and its fallback text, and the
-- field remembering that the event was logged this map. Adding an event means adding a row.
VRCompanion.ACTION_EVENTS = {
    { idField = "hudToggleActionEventId", action = "VR_HUD_TOGGLE", handler = "onHudToggleAction",
        textKey = "input_VR_HUD_TOGGLE", fallbackText = "Toggle VR HUD Mode", loggedField = "hudToggleLoggedOnce" },
    { idField = "recenterActionEventId", action = "VR_RECENTER_VIEW", handler = "onRecenterAction",
        textKey = "input_VR_RECENTER_VIEW", fallbackText = "Recenter VR View", loggedField = "recenterLoggedOnce" },
    { idField = "snapLeftActionEventId", action = "VR_SNAP_TURN_LEFT", handler = "onSnapTurnLeftAction",
        textKey = "input_VR_SNAP_TURN_LEFT", fallbackText = "VR Snap Turn Left", loggedField = "snapLeftLoggedOnce" },
    { idField = "snapRightActionEventId", action = "VR_SNAP_TURN_RIGHT", handler = "onSnapTurnRightAction",
        textKey = "input_VR_SNAP_TURN_RIGHT", fallbackText = "VR Snap Turn Right", loggedField = "snapRightLoggedOnce" },
    { idField = "toggleViewActionEventId", action = "VR_TOGGLE_VIEW", handler = "onToggleVrViewAction",
        textKey = "input_VR_TOGGLE_VIEW", fallbackText = "Toggle VR / 2D View", loggedField = "toggleViewLoggedOnce" },
    { idField = "seatSetActionEventId", action = "VR_SET_SEAT", handler = "onSeatSetAction",
        textKey = "input_VR_SET_SEAT", fallbackText = "Set VR Seat Position", loggedField = "seatSetLoggedOnce" },
}

function VRCompanion:registerActionEvents(force)
    local inputBinding = g_inputBinding or _G.g_inputBinding
    if inputBinding == nil or type(inputBinding.registerActionEvent) ~= "function" then
        return
    end

    -- Events in another context than the gameplay one (the menu context at map load, or the vehicle's after getting
    -- out) move to it: they still exist there, so the check below alone would never notice.
    local contextName = self:getActionEventsContextName()
    local canPickContext = type(inputBinding.beginActionEventsModification) == "function"
        and type(inputBinding.endActionEventsModification) == "function"
    if canPickContext and self.actionEventsContextName ~= contextName then
        force = true
    end

    -- FS25 keys `events` by the event id registerActionEvent returns; `actionEvents`
    -- is keyed by action name, so it must not be used for id lookups.
    local actionEvents = inputBinding.events
    if actionEvents == nil and not self.missingInputEventsLogged then
        -- Without it the periodic check cannot see registered events and re-registers them every time.
        print("[FS25_VR] [Warning] g_inputBinding.events is missing; action events will be re-registered on every check.")
        self.missingInputEventsLogged = true
    end
    local active = {}
    if not force and actionEvents ~= nil then
        local allActive = true
        for _, row in ipairs(self.ACTION_EVENTS) do
            local isActive = self[row.idField] ~= nil and actionEvents[self[row.idField]] ~= nil
            active[row.idField] = isActive
            allActive = allActive and isActive
        end
        if allActive then
            return
        end
    end

    if not canPickContext then
        self:registerActionEventsInContext(inputBinding, force, active)
        return
    end
    -- A forced registration takes the old events out first, so a context never holds two of an action
    if force and self.actionEventsContextName ~= nil and type(inputBinding.removeActionEvent) == "function" then
        pcall(function()
            inputBinding:beginActionEventsModification(self.actionEventsContextName)
            for _, row in ipairs(self.ACTION_EVENTS) do
                if self[row.idField] ~= nil then inputBinding:removeActionEvent(self[row.idField]) end
            end
            inputBinding:endActionEventsModification()
        end)
    end
    -- Register into the gameplay context without switching to it, as PlayerInputComponent does from the UI context
    inputBinding:beginActionEventsModification(contextName)
    local ok, err = pcall(self.registerActionEventsInContext, self, inputBinding, force, active)
    inputBinding:endActionEventsModification()
    if not ok then error(err, 0) end
    if self.actionEventsContextName ~= contextName then
        print("[FS25_VR] VR action events registered in input context " .. tostring(contextName) .. ".")
        self.actionEventsContextName = contextName
    end
end

-- Registers the VR action events that are not active (all of them when forced) in the context being registered into.
function VRCompanion:registerActionEventsInContext(inputBinding, force, active)
    for _, row in ipairs(self.ACTION_EVENTS) do
        if force or active[row.idField] ~= true then
            local actionId = (InputAction and InputAction[row.action]) or (_G.InputAction and _G.InputAction[row.action]) or row.action
            local success, eventId = inputBinding:registerActionEvent(actionId, self, self[row.handler], false, true, false, true)
            if success and eventId then
                self[row.idField] = eventId
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
                        local txt = (g_i18n and g_i18n:getText(row.textKey)) or row.fallbackText
                        inputBinding:setActionEventText(eventId, txt)
                    end)
                end
                if not self[row.loggedField] then
                    print("[FS25_VR] Registered in-engine " .. row.action .. " event.")
                    self[row.loggedField] = true
                end
            end
        end
    end
end

-- Usage text for vrHudSafeArea: the syntax followed by the current margins
function VRCompanion:getHudSafeAreaUsage()
    return string.format("Usage: vrHudSafeArea <horizontal> <vertical>\n  margins %.2f to %.2f (horizontal) and %.2f to %.2f (vertical) from the screen edges, keeping warnings, notifications and other mods' HUDs inside the lens\n  current: horizontal=%.2f, vertical=%.2f",
        self.MIN_HUD_SAFE_AREA, self.MAX_HUD_SAFE_AREA_HORIZONTAL, self.MIN_HUD_SAFE_AREA, self.MAX_HUD_SAFE_AREA_VERTICAL,
        self.hudSafeAreaHorizontal or self.DEFAULT_HUD_SAFE_AREA_HORIZONTAL,
        self.hudSafeAreaVertical or self.DEFAULT_HUD_SAFE_AREA_VERTICAL)
end

-- A usable number: tonumber's nil for text and NaN are both rejected
local function isNumber(v) return v ~= nil and v == v end

function VRCompanion:consoleCommandSetHudSafeArea(horizontalStr, verticalStr)
    local horizontal = tonumber(horizontalStr)
    local vertical = tonumber(verticalStr)
    if not isNumber(horizontal) or not isNumber(vertical) then
        return self:getHudSafeAreaUsage()
    end

    self:configureHudSafeArea(horizontal, vertical)
    -- 2D Floating Window Mode keeps the full-screen anchors; the Safe Area applies on returning to VR
    if not self.is2DViewActive then
        self:applyHudSafeArea()
    end
    self:saveConfiguration()

    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD Safe Area: Horizontal=%.2f, Vertical=%.2f (Saved)",
                self.hudSafeAreaHorizontal, self.hudSafeAreaVertical), 2500)
        end)
    end
    return string.format("VR HUD Safe Area updated and saved: horizontal=%.2f, vertical=%.2f",
        self.hudSafeAreaHorizontal, self.hudSafeAreaVertical)
end

-- Usage text for vrHudGroup: the syntax followed by every group's current margins and scale
function VRCompanion:getHudGroupUsage()
    local lines = {
        "Usage: vrHudGroup <map|speedometer|info|controls> <horizontal> <vertical> [scale]",
        string.format("  margins %.2f to %.2f from the group's home corner, scale %.2f to %.2f",
            self.MIN_HUD_GROUP_MARGIN, self.MAX_HUD_GROUP_MARGIN, self.MIN_HUD_GROUP_SCALE, self.MAX_HUD_GROUP_SCALE)
    }
    for _, groupName in ipairs(self.HUD_GROUP_NAMES) do
        local group = self.HUD_GROUPS[groupName]
        local layout = self:getHudGroupLayout(groupName)
        table.insert(lines, string.format("  %s (%s-%s): horizontal=%.2f, vertical=%.2f, scale=%.2f",
            groupName, group.alignY, group.alignX, layout.marginX, layout.marginY, layout.scale))
    end
    return table.concat(lines, "\n")
end

function VRCompanion:consoleCommandSetHudGroup(groupStr, horizontalStr, verticalStr, scaleStr)
    local groupName = (groupStr ~= nil) and string.lower(tostring(groupStr)) or nil
    local horizontal = tonumber(horizontalStr)
    local vertical = tonumber(verticalStr)
    local hasScale = (scaleStr ~= nil and scaleStr ~= "")
    local scale = hasScale and tonumber(scaleStr) or nil
    if groupName == nil or self.HUD_GROUPS[groupName] == nil
        or not isNumber(horizontal) or not isNumber(vertical) or (hasScale and not isNumber(scale)) then
        return self:getHudGroupUsage()
    end

    local layout = self:setHudGroupLayout(groupName, horizontal, vertical, scale)
    -- 2D Floating Window Mode keeps the engine's layout; the margins apply on returning to VR
    if not self.is2DViewActive then
        self:repositionAllHudDisplays()
    end
    self:saveConfiguration()

    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD %s group: Horizontal=%.2f, Vertical=%.2f, Scale=%.2fx (Saved)",
                groupName, layout.marginX, layout.marginY, layout.scale), 2500)
        end)
    end
    return string.format("VR HUD %s group updated and saved: horizontal=%.2f, vertical=%.2f, scale=%.2f",
        groupName, layout.marginX, layout.marginY, layout.scale)
end

function VRCompanion:consoleCommandSetHudState(stateStr)
    local st = tonumber(stateStr)
    if st == nil or st < 0 or st > 2 then
        return "Usage: vrHudState <0=Hidden | 1=Streamlined | 2=Full>"
    end
    self:setHudState(st)
    -- The next map starts in the state the player last chose
    self.persistedHudState = st
    self:saveConfiguration()
    local stateName = self.HUD_STATE_NAMES[st] or tostring(st)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: %s (%d)", stateName, st), 2500)
        end)
    end
    return string.format("VR HUD state set to: %s (%d)", stateName, st)
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

local function hudGroupShowUsage(self)
    local lines = { "Usage: vrHudGroupShow <map|speedometer|info|controls> <streamlined|full> <on|off>" }
    for _, name in ipairs(self.HUD_GROUP_NAMES) do
        local group = self.HUD_GROUPS[name]
        table.insert(lines, string.format("  %s: streamlined=%s full=%s", name,
            group.streamlined and "on" or "off", group.full and "on" or "off"))
    end
    return table.concat(lines, "\n")
end

function VRCompanion:consoleCommandHudGroupShow(groupStr, stateStr, onOffStr)
    local groupName = (groupStr ~= nil) and string.lower(tostring(groupStr)) or nil
    local stateName = (stateStr ~= nil) and string.lower(tostring(stateStr)) or nil
    local onOff = (onOffStr ~= nil) and string.lower(tostring(onOffStr)) or nil

    local enabled = nil
    if onOff == "on" or onOff == "true" or onOff == "1" then
        enabled = true
    elseif onOff == "off" or onOff == "false" or onOff == "0" then
        enabled = false
    end
    if groupName == nil or enabled == nil or not self:setHudGroupVisibility(groupName, stateName, enabled) then
        return hudGroupShowUsage(self)
    end

    self:saveConfiguration()
    local label = self:getHudGroupLabel(groupName)
    local stateLabel = self:getHudGroupLabel(stateName)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD: %s group %s in %s HUD (Saved)",
                label, enabled and "on" or "off", stateLabel), 2500)
        end)
    end
    return string.format("VR HUD %s group (%s) %s in the %s HUD and saved.",
        label, groupName, enabled and "shown" or "hidden", stateLabel)
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

function VRCompanion:consoleCommandSetSnapTurnAngle(angleStr)
    local curDeg = self:getSnapTurnAngleDegrees()
    if angleStr == nil or angleStr == "" or string.lower(tostring(angleStr)) == "status" then
        return string.format("Current VR snap turn angle: %.1f deg. Usage: vrSnapTurnAngle <degrees> (e.g. 30, 45, 90, 180)", curDeg)
    end

    local deg = tonumber(angleStr)
    if deg == nil or deg <= 0 or deg > 360 then
        return string.format("Usage: vrSnapTurnAngle <degrees> (valid: 1 to 360, e.g. 30, 45, 90, 180, current: %.1f deg)", curDeg)
    end

    self:setSnapTurnAngle(deg, true, true)

    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR Snap Turn Angle: %.1f deg (Saved)", deg), 2500)
        end)
    end

    print(string.format("[FS25_VR] Snap turn angle set to: %.1f deg (%.4f rad)", deg, self:getSnapTurnAngle()))
    return string.format("VR snap turn angle set to: %.1f deg (Saved)", deg)
end

function VRCompanion:consoleCommandSet2DView(activeStr)
    local cur = self.is2DViewActive
    if activeStr == nil or activeStr == "" or string.lower(tostring(activeStr)) == "status" then
        return string.format("Current 2D Floating Window Mode: %s. Usage: vr2DView <true|false|toggle>", tostring(cur))
    end

    local low = string.lower(tostring(activeStr))
    if low == "toggle" then
        self:toggle2DView()
    elseif low == "true" or low == "1" or low == "on" then
        self:set2DViewActive(true)
    elseif low == "false" or low == "0" or low == "off" then
        self:set2DViewActive(false)
    else
        return "Usage: vr2DView <true|false|toggle>"
    end

    print(string.format("[FS25_VR] 2D Floating Window Mode is now: %s", tostring(self.is2DViewActive)))
    return string.format("2D Floating Window Mode set to: %s", tostring(self.is2DViewActive))
end

function VRCompanion:consoleCommandToggleView()
    self:toggle2DView()
    print(string.format("[FS25_VR] 2D Floating Window Mode toggled to: %s", tostring(self.is2DViewActive)))
    return string.format("2D Floating Window Mode toggled to: %s", tostring(self.is2DViewActive))
end

function VRCompanion:consoleCommandSetDlssMvScale(scaleXStr, scaleYStr)
    local sx = tonumber(scaleXStr)
    if sx == nil then
        return "Usage: vrDlssMvScale <scaleX> [scaleY] (AFR fallback only, used while vrDlssMvFix is off or disabled; default 2.0; 1.0 disables eye-stride compensation)"
    end
    local sy = tonumber(scaleYStr) or sx
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.setDlssMvScale) == "function" then
        bridge.setDlssMvScale(sx, sy)
        print(string.format("[FS25_VR] DLSS Motion Vector scale multiplier set to: X=%.2f, Y=%.2f", sx, sy))
        return string.format("DLSS Motion Vector scale multiplier set to: X=%.2f, Y=%.2f", sx, sy)
    else
        return "VRBridge.setDlssMvScale is not available."
    end
end

function VRCompanion:consoleCommandSetDlssSharpness(sharpStr)
    local s = tonumber(sharpStr)
    if s == nil then
        return "Usage: vrDlssSharpness <sharpness 0.0 - 1.0> (e.g. 0.50)"
    end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.setDlssSharpness) == "function" then
        bridge.setDlssSharpness(s)
        print(string.format("[FS25_VR] DLSS Sharpness override set to: %.2f", s))
        return string.format("DLSS Sharpness override set to: %.2f", s)
    else
        return "VRBridge.setDlssSharpness is not available."
    end
end

function VRCompanion:consoleCommandSetDlssPreset(presetStr)
    local p = tonumber(presetStr)
    if p == nil then
        return "Usage: vrDlssPreset <0=Default|3=C|4=D|10=J|11=K|12=L> (e.g. 11 for Preset K)"
    end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.setDlssPreset) == "function" then
        bridge.setDlssPreset(p)
        print(string.format("[FS25_VR] DLSS Preset hint override set to: %d", p))
        return string.format("DLSS Preset hint override set to: %d", p)
    else
        return "VRBridge.setDlssPreset is not available."
    end
end

function VRCompanion:consoleCommandSetDlssSdpMvScale(scaleXStr, scaleYStr)
    local sx = tonumber(scaleXStr)
    if sx == nil then
        return "Usage: vrDlssSdpMvScale <scaleX> [scaleY] (default 1.0; 0 ignores motion vectors)"
    end
    local sy = tonumber(scaleYStr) or sx
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.setDlssSdpMvScale) == "function" then
        bridge.setDlssSdpMvScale(sx, sy)
        print(string.format("[FS25_VR] DLSS Sequential Dual-Pass motion vector scale set to: X=%.2f, Y=%.2f", sx, sy))
        return string.format("DLSS Sequential Dual-Pass motion vector scale set to: X=%.2f, Y=%.2f", sx, sy)
    else
        return "VRBridge.setDlssSdpMvScale is not available."
    end
end

function VRCompanion:consoleCommandDlssMvFix(arg)
    local value = arg ~= nil and string.lower(tostring(arg)) or ""
    local enable
    if value == "on" or value == "true" or value == "1" then
        enable = true
    elseif value == "off" or value == "false" or value == "0" then
        enable = false
    else
        return "Usage: vrDlssMvFix <on|off>"
    end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.setDlssMvFix) == "function" then
        if bridge.setDlssMvFix(enable) then
            local state = enable and "on" or "off"
            print(string.format("[FS25_VR] DLSS motion vector pass (vrDlssMvFix) is %s", state))
            return string.format("DLSS motion vector pass (vrDlssMvFix) is %s", state)
        end
        return "DLSS motion vector pass unavailable (no NGX interceptor, or it was disabled after a failure; see fs25_vr.log)."
    else
        return "VRBridge.setDlssMvFix is not available."
    end
end

function VRCompanion:consoleCommandDlssDepthModel(arg)
    local value = arg ~= nil and string.lower(tostring(arg)) or ""
    local infinite
    if value == "infinite" then
        infinite = true
    elseif value == "finite" then
        infinite = false
    else
        return "Usage: vrDlssDepthModel <infinite|finite> (depth model the DLSS motion vector correction reads; default infinite)"
    end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.setDlssDepthModel) == "function" then
        if bridge.setDlssDepthModel(infinite) then
            local state = infinite and "infinite" or "finite"
            print(string.format("[FS25_VR] DLSS depth model (vrDlssDepthModel) is %s", state))
            return string.format("DLSS depth model (vrDlssDepthModel) is %s", state)
        end
        return "DLSS depth model unavailable (no NGX interceptor)."
    else
        return "VRBridge.setDlssDepthModel is not available."
    end
end

local DLSS_NEAR_CLIP_MODE_NAMES = { cab = "cab", outside = "outside camera", onFoot = "on foot" }

-- Metres from a console argument: a number >= 0, or "auto" (any case) for 0. nil when it is neither.
local function parseDlssNearClip(arg)
    local meters = tonumber(arg)
    if meters == nil and type(arg) == "string" and string.lower(arg) == "auto" then
        meters = 0
    end
    if meters == nil or meters ~= meters or meters < 0 then return nil end
    return meters
end

local function describeDlssNearClip(meters)
    if meters ~= nil and meters > 0 then
        return string.format("%.3f m", meters)
    end
    return "auto"
end

function VRCompanion:describeDlssNearClips()
    return string.format("cab %s, outside camera %s, on foot %s",
        describeDlssNearClip(self.dlssNearClipCab),
        describeDlssNearClip(self.dlssNearClipOutside),
        describeDlssNearClip(self.dlssNearClipOnFoot))
end

-- Saves the near planes just set and sends the current camera's to the Injector; appends a note to
-- text when the bridge cannot take it.
function VRCompanion:commitDlssNearClips(text)
    self:saveConfiguration()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.setDlssNearClip) ~= "function" then
        text = text .. " (not applied: VRBridge.setDlssNearClip is not available)"
    elseif not self:applyDlssNearClip(true) then
        text = text .. " (not applied: no NGX interceptor)"
    end
    print("[FS25_VR] " .. text)
    return text
end

function VRCompanion:consoleCommandDlssNearClip(arg)
    local meters = parseDlssNearClip(arg)
    if meters == nil then
        return "Usage: vrDlssNearClip <meters|auto> (near plane the DLSS motion vector correction assumes for every camera; 0 or auto = estimate it from the engine's motion vectors. vrDlssNearClipCab, vrDlssNearClipOutside and vrDlssNearClipOnFoot set one camera mode; defaults: cab 0.1 m, outside camera auto, on foot 0.15 m.) Now: "
            .. self:describeDlssNearClips()
    end
    for _, field in pairs(self.DLSS_NEAR_CLIP_FIELDS) do
        self[field] = meters
    end
    local text = meters > 0 and string.format("DLSS near plane (vrDlssNearClip) is %.3f m for every camera", meters)
        or "DLSS near plane (vrDlssNearClip) is auto for every camera: estimated from the engine's motion vectors (the camera record's near until then)"
    return self:commitDlssNearClips(text)
end

function VRCompanion:setDlssNearClipForMode(mode, command, arg)
    local field = self.DLSS_NEAR_CLIP_FIELDS[mode]
    local meters = parseDlssNearClip(arg)
    if meters == nil then
        return string.format("Usage: %s <meters|auto> (DLSS near plane for the %s; 0 or auto = estimate it automatically). Now: %s",
            command, DLSS_NEAR_CLIP_MODE_NAMES[mode], describeDlssNearClip(self[field]))
    end
    self[field] = meters
    local text = string.format("DLSS near plane (%s) is %s for the %s; now %s", command,
        describeDlssNearClip(meters), DLSS_NEAR_CLIP_MODE_NAMES[mode], self:describeDlssNearClips())
    return self:commitDlssNearClips(text)
end

function VRCompanion:consoleCommandDlssNearClipCab(arg)
    return self:setDlssNearClipForMode(self.DLSS_NEAR_CLIP_CAB, "vrDlssNearClipCab", arg)
end

function VRCompanion:consoleCommandDlssNearClipOutside(arg)
    return self:setDlssNearClipForMode(self.DLSS_NEAR_CLIP_OUTSIDE, "vrDlssNearClipOutside", arg)
end

function VRCompanion:consoleCommandDlssNearClipOnFoot(arg)
    return self:setDlssNearClipForMode(self.DLSS_NEAR_CLIP_ON_FOOT, "vrDlssNearClipOnFoot", arg)
end

function VRCompanion:consoleCommandDlssDiag(countStr)
    local count = tonumber(countStr) or 128
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.dlssDiag) == "function" then
        if bridge.dlssDiag(count) then
            print(string.format("[FS25_VR] Tracing the next %d DLSS evaluations to fs25_vr.log", count))
            return string.format("Tracing the next %d DLSS evaluations to fs25_vr.log", count)
        end
        return "DLSS diagnostics unavailable (no NGX interceptor or invalid count)."
    else
        return "VRBridge.dlssDiag is not available."
    end
end

function VRCompanion:consoleCommandSetRenderingMode(modeStr)
    local curMode = self:getRenderingMode()
    if modeStr == nil or modeStr == "" or string.lower(tostring(modeStr)) == "status" then
        local modeName = (curMode == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "Sequential Dual-Pass (DualPass)" or "Alternate Frame Rendering (AFR)"
        return string.format("Current Stereo Presentation Mode: %s. Usage: vrRenderingMode <afr|dualpass|toggle>", modeName)
    end

    local low = string.lower(tostring(modeStr))
    if low ~= "afr" and low ~= "dualpass" and low ~= "toggle" and low ~= "sequentialdualpass" and low ~= "dual" and low ~= "dual_pass" and low ~= "sequential" and low ~= "sdp" and low ~= "alternate" and low ~= "alternateframe" and low ~= "0" and low ~= "1" then
        return "Usage: vrRenderingMode <afr|dualpass|toggle>"
    end

    local newMode = self:setRenderingMode(modeStr, true)
    local modeLabel = (newMode == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "Sequential Dual-Pass" or "Alternate Frame Rendering (AFR)"
    local alertMsg = string.format("Stereo Presentation Mode: %s", modeLabel)

    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil then
        if type(mission.addIngameNotification) == "function" then
            pcall(function()
                local notifType = (FSBaseMission and FSBaseMission.INGAME_NOTIFICATION_INFO) or 0
                mission:addIngameNotification(notifType, alertMsg)
            end)
        elseif type(mission.showBlinkingWarning) == "function" then
            pcall(function()
                mission:showBlinkingWarning(alertMsg, 3000)
            end)
        end
    end

    print(string.format("[FS25_VR] Stereo Presentation Mode is now: %s (%s)", tostring(newMode), modeLabel))
    return string.format("Stereo Presentation Mode set to: %s (%s, Saved)", tostring(newMode), modeLabel)
end
