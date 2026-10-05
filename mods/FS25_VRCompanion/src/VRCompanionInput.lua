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

    local actionEvents = inputBinding.actionEvents or (inputBinding.events and inputBinding.events)
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

    self:diagnoseActionEventStorage(inputBinding, force)
end

-- TEMPORARY diagnostic for issue #16: logs how g_inputBinding stores registered
-- action events so the periodic re-registration check can be matched to FS25.
-- Capped so a mis-keyed check cannot flood the log. Remove once confirmed.
function VRCompanion:diagnoseActionEventStorage(inputBinding, force)
    self.eventStorageDiagCount = (self.eventStorageDiagCount or 0) + 1
    if self.eventStorageDiagCount > 6 then
        return
    end
    local ids = {
        hud = self.hudToggleActionEventId, recenter = self.recenterActionEventId,
        snapL = self.snapLeftActionEventId, snapR = self.snapRightActionEventId,
        view = self.toggleViewActionEventId,
    }
    print(string.format("[FS25_VR][diag#16] registerActionEvents call %d force=%s ids: hud=%s recenter=%s snapL=%s snapR=%s view=%s",
        self.eventStorageDiagCount, tostring(force), tostring(ids.hud), tostring(ids.recenter),
        tostring(ids.snapL), tostring(ids.snapR), tostring(ids.view)))
    print(string.format("[FS25_VR][diag#16] inputBinding.actionEvents=%s inputBinding.events=%s",
        type(inputBinding.actionEvents), type(inputBinding.events)))
    local probe = ids.hud
    local keys = {}
    for k, v in pairs(inputBinding) do
        if type(v) == "table" then
            local hit, count = "no", 0
            pcall(function()
                for _ in pairs(v) do count = count + 1 end
                if probe ~= nil and v[probe] ~= nil then hit = "YES(" .. type(v[probe]) .. ")" end
            end)
            table.insert(keys, string.format("%s[n=%d,hasId=%s]", tostring(k), count, hit))
        end
    end
    table.sort(keys)
    print("[FS25_VR][diag#16] inputBinding table fields: " .. table.concat(keys, ", "))
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
            self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_TOP,
            self.hudUiScaleMultiplier or self.DEFAULT_HUD_UI_SCALE)
    end
    self:configureHudInsets(ix, iy, sc or self.hudUiScaleMultiplier, it or self.hudInsetTop or iy)
    self:applyHudInsets()
    self:saveConfiguration()
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR HUD Insets: X=%.2f, Y=%.2f, Top=%.2f, Scale=%.2fx (Saved)",
                self.hudInsetX, self.hudInsetY, self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_TOP, self.effectiveUiScale), 2500)
        end)
    end
    return string.format("VR HUD insets updated and saved: Left=%.2f, Right=%.2f, Bottom=%.2f, Top=%.2f, Scale=%.2fx",
        self.hudAnchorLeft, self.hudAnchorRight, self.hudAnchorBottom, self.hudAnchorTop, self.effectiveUiScale)
end

function VRCompanion:consoleCommandSetHudTopInset(topInsetStr)
    local topVal = tonumber(topInsetStr)
    if topVal == nil then
        return string.format("Usage: vrHudTopInset <topInset> (0.0 to 0.50, current: %.2f, topAnchor: %.2f)",
            self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_TOP,
            self.hudAnchorTop or (1.0 - (self.hudInsetTop or self.hudInsetY or self.DEFAULT_HUD_INSET_TOP)))
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
        return "Usage: vrDlssMvScale <scaleX> [scaleY] (default 2.0 under AFR; 1.0 disables eye-stride compensation)"
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
VRCompanion.Input.consoleCommandSetHudInsets = function(self, ...) return VRCompanion.consoleCommandSetHudInsets(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudTopInset = function(self, ...) return VRCompanion.consoleCommandSetHudTopInset(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudState = function(self, ...) return VRCompanion.consoleCommandSetHudState(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudFillUnderVehicle = function(self, ...) return VRCompanion.consoleCommandSetHudFillUnderVehicle(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudFillGap = function(self, ...) return VRCompanion.consoleCommandSetHudFillGap(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetHudLoadNotification = function(self, ...) return VRCompanion.consoleCommandSetHudLoadNotification(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetRecenterMode = function(self, ...) return VRCompanion.consoleCommandSetRecenterMode(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetSnapTurnAngle = function(self, ...) return VRCompanion.consoleCommandSetSnapTurnAngle(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSet2DView = function(self, ...) return VRCompanion.consoleCommandSet2DView(VRCompanion, ...) end
VRCompanion.Input.consoleCommandToggleView = function(self, ...) return VRCompanion.consoleCommandToggleView(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssMvScale = function(self, ...) return VRCompanion.consoleCommandSetDlssMvScale(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssSharpness = function(self, ...) return VRCompanion.consoleCommandSetDlssSharpness(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetDlssPreset = function(self, ...) return VRCompanion.consoleCommandSetDlssPreset(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetRenderingMode = function(self, ...) return VRCompanion.consoleCommandSetRenderingMode(VRCompanion, ...) end
VRCompanion.Input.consoleCommandSetObliqueProjection = function(self, ...) return VRCompanion.consoleCommandSetObliqueProjection(VRCompanion, ...) end

return VRCompanion.Input
