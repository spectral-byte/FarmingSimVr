-- ====================================================================
-- FS25 VR Companion Mod: Input Subsystem
-- Manages in-engine action events and console commands. All key and
-- gamepad bindings come from modDesc.xml via g_inputBinding, so players
-- rebind them in the game's Controls menu.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Input = VRCompanion.Input or {}

function VRCompanion:registerActionEvents(force)
    local inputBinding = g_inputBinding or _G.g_inputBinding
    if inputBinding == nil or type(inputBinding.registerActionEvent) ~= "function" then
        return
    end

    -- FS25 keys `events` by the event id registerActionEvent returns; `actionEvents`
    -- is keyed by action name, so it must not be used for id lookups.
    local actionEvents = inputBinding.events
    if actionEvents == nil and not self.missingInputEventsLogged then
        -- Without it the periodic check cannot see registered events and re-registers them every time.
        print("[FS25_VR] [Warning] g_inputBinding.events is missing; action events will be re-registered on every check.")
        self.missingInputEventsLogged = true
    end
    local isHudEventActive = false
    local isRecenterEventActive = false
    local isSnapLeftEventActive = false
    local isSnapRightEventActive = false
    local isToggleViewEventActive = false
    if not force and actionEvents ~= nil then
        if self.hudToggleActionEventId ~= nil and actionEvents[self.hudToggleActionEventId] ~= nil then
            isHudEventActive = true
        end
        if self.recenterActionEventId ~= nil and actionEvents[self.recenterActionEventId] ~= nil then
            isRecenterEventActive = true
        end
        if self.snapLeftActionEventId ~= nil and actionEvents[self.snapLeftActionEventId] ~= nil then
            isSnapLeftEventActive = true
        end
        if self.snapRightActionEventId ~= nil and actionEvents[self.snapRightActionEventId] ~= nil then
            isSnapRightEventActive = true
        end
        if self.toggleViewActionEventId ~= nil and actionEvents[self.toggleViewActionEventId] ~= nil then
            isToggleViewEventActive = true
        end
        if isHudEventActive and isRecenterEventActive and isSnapLeftEventActive and isSnapRightEventActive and isToggleViewEventActive then
            return
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
        local actionId = (InputAction and InputAction.VR_RECENTER_VIEW) or (_G.InputAction and _G.InputAction.VR_RECENTER_VIEW) or "VR_RECENTER_VIEW"
        local success, eventId = inputBinding:registerActionEvent(actionId, self, self.onRecenterAction, false, true, false, true)
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
                    local txt = (g_i18n and g_i18n:getText("input_VR_RECENTER_VIEW")) or "Recenter VR View"
                    inputBinding:setActionEventText(eventId, txt)
                end)
            end
            if not self.recenterLoggedOnce then
                print("[FS25_VR] Registered in-engine VR_RECENTER_VIEW event.")
                self.recenterLoggedOnce = true
            end
        end
    end

    if force or not isSnapLeftEventActive then
        local actionId = (InputAction and InputAction.VR_SNAP_TURN_LEFT) or (_G.InputAction and _G.InputAction.VR_SNAP_TURN_LEFT) or "VR_SNAP_TURN_LEFT"
        local success, eventId = inputBinding:registerActionEvent(actionId, self, self.onSnapTurnLeftAction, false, true, false, true)
        if success and eventId then
            self.snapLeftActionEventId = eventId
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
                    local txt = (g_i18n and g_i18n:getText("input_VR_SNAP_TURN_LEFT")) or "VR Snap Turn Left"
                    inputBinding:setActionEventText(eventId, txt)
                end)
            end
            if not self.snapLeftLoggedOnce then
                print("[FS25_VR] Registered in-engine VR_SNAP_TURN_LEFT event.")
                self.snapLeftLoggedOnce = true
            end
        end
    end

    if force or not isSnapRightEventActive then
        local actionId = (InputAction and InputAction.VR_SNAP_TURN_RIGHT) or (_G.InputAction and _G.InputAction.VR_SNAP_TURN_RIGHT) or "VR_SNAP_TURN_RIGHT"
        local success, eventId = inputBinding:registerActionEvent(actionId, self, self.onSnapTurnRightAction, false, true, false, true)
        if success and eventId then
            self.snapRightActionEventId = eventId
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
                    local txt = (g_i18n and g_i18n:getText("input_VR_SNAP_TURN_RIGHT")) or "VR Snap Turn Right"
                    inputBinding:setActionEventText(eventId, txt)
                end)
            end
            if not self.snapRightLoggedOnce then
                print("[FS25_VR] Registered in-engine VR_SNAP_TURN_RIGHT event.")
                self.snapRightLoggedOnce = true
            end
        end
    end

    if force or not isToggleViewEventActive then
        local actionId = (InputAction and InputAction.VR_TOGGLE_VIEW) or (_G.InputAction and _G.InputAction.VR_TOGGLE_VIEW) or "VR_TOGGLE_VIEW"
        local success, eventId = inputBinding:registerActionEvent(actionId, self, self.onToggleVrViewAction, false, true, false, true)
        if success and eventId then
            self.toggleViewActionEventId = eventId
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
                    local txt = (g_i18n and g_i18n:getText("input_VR_TOGGLE_VIEW")) or "Toggle VR / 2D View"
                    inputBinding:setActionEventText(eventId, txt)
                end)
            end
            if not self.toggleViewLoggedOnce then
                print("[FS25_VR] Registered in-engine VR_TOGGLE_VIEW event.")
                self.toggleViewLoggedOnce = true
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

function VRCompanion:consoleCommandSetObliqueProjection(enabledStr)
    local cur = (self.allowObliqueProjection == true)
    if enabledStr == nil or enabledStr == "" or string.lower(tostring(enabledStr)) == "status" then
        return string.format("Current Oblique Frustum projection: %s. Usage: vrObliqueProjection <true|false|toggle>",
            cur and "Enabled" or "Disabled (symmetric)")
    end

    local low = string.lower(tostring(enabledStr))
    local enabled
    if low == "toggle" then
        enabled = not cur
    elseif low == "true" or low == "1" or low == "on" then
        enabled = true
    elseif low == "false" or low == "0" or low == "off" then
        enabled = false
    else
        return "Usage: vrObliqueProjection <true|false|toggle>"
    end

    self.allowObliqueProjection = enabled
    self:saveConfiguration()
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR Oblique Frustum: %s (Saved)",
                enabled and "Enabled" or "Disabled"), 2500)
        end)
    end
    print(string.format("[FS25_VR] Oblique Frustum projection set to: %s", tostring(enabled)))
    return string.format("Oblique Frustum projection set to: %s (Saved)", tostring(enabled))
end

-- Export to Input subsystem table
VRCompanion.Input.registerActionEvents = function(self, ...) return VRCompanion.registerActionEvents(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudSafeArea = function(self, ...) return VRCompanion.consoleCommandSetHudSafeArea(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudGroup = function(self, ...) return VRCompanion.consoleCommandSetHudGroup(VRCompanion, ...) end
VRCompanion.Input.consoleCommandHudGroupShow = function(self, ...) return VRCompanion.consoleCommandHudGroupShow(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudState = function(self, ...) return VRCompanion.consoleCommandSetHudState(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudLoadNotification = function(self, ...) return VRCompanion.consoleCommandSetHudLoadNotification(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetRecenterMode = function(self, ...) return VRCompanion.consoleCommandSetRecenterMode(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetSnapTurnAngle = function(self, ...) return VRCompanion.consoleCommandSetSnapTurnAngle(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSet2DView = function(self, ...) return VRCompanion.consoleCommandSet2DView(VRCompanion, ...) end
VRCompanion.Input.consoleCommandToggleView = function(self, ...) return VRCompanion.consoleCommandToggleView(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssMvScale = function(self, ...) return VRCompanion.consoleCommandSetDlssMvScale(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssSharpness = function(self, ...) return VRCompanion.consoleCommandSetDlssSharpness(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssPreset = function(self, ...) return VRCompanion.consoleCommandSetDlssPreset(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssSdpMvScale = function(self, ...) return VRCompanion.consoleCommandSetDlssSdpMvScale(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssMvFix = function(self, ...) return VRCompanion.consoleCommandDlssMvFix(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssDepthModel = function(self, ...) return VRCompanion.consoleCommandDlssDepthModel(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssNearClip = function(self, ...) return VRCompanion.consoleCommandDlssNearClip(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssNearClipCab = function(self, ...) return VRCompanion.consoleCommandDlssNearClipCab(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssNearClipOutside = function(self, ...) return VRCompanion.consoleCommandDlssNearClipOutside(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssNearClipOnFoot = function(self, ...) return VRCompanion.consoleCommandDlssNearClipOnFoot(VRCompanion, ...) end
VRCompanion.Input.consoleCommandDlssDiag = function(self, ...) return VRCompanion.consoleCommandDlssDiag(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetRenderingMode = function(self, ...) return VRCompanion.consoleCommandSetRenderingMode(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetObliqueProjection = function(self, ...) return VRCompanion.consoleCommandSetObliqueProjection(VRCompanion, ...) end

return VRCompanion.Input
