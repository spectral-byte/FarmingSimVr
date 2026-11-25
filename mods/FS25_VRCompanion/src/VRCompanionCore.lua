VRCompanion = VRCompanion or {}

VRCompanion.name = VRCompanion.name or g_currentModName or "FS25_VRCompanion"

VRCompanion.VERSION = "1.2.0.0"

VRCompanion.LEGACY_INJECTOR_VERSION = "1.0.0"

VRCompanion.VERSION_WARNING_MS = 8000

VRCompanion.VEHICLE_SPEED_HOOK_RETRY_TICKS = 180

VRCompanion.handshakeComplete = (VRCompanion.handshakeComplete ~= nil) and VRCompanion.handshakeComplete or false
VRCompanion.frameTicks = VRCompanion.frameTicks or 0
VRCompanion.lastControlledVehicle = VRCompanion.lastControlledVehicle or nil
VRCompanion.isMenuVisible = (VRCompanion.isMenuVisible ~= nil) and VRCompanion.isMenuVisible or false
VRCompanion.menuTakesInput = (VRCompanion.menuTakesInput ~= nil) and VRCompanion.menuTakesInput or false
VRCompanion.stereoDialogShowing = (VRCompanion.stereoDialogShowing ~= nil) and VRCompanion.stereoDialogShowing or false
VRCompanion.guiVisibilityInitialized = (VRCompanion.guiVisibilityInitialized ~= nil) and VRCompanion.guiVisibilityInitialized or false
VRCompanion.vehicleCameraHooked = (VRCompanion.vehicleCameraHooked ~= nil) and VRCompanion.vehicleCameraHooked or false
VRCompanion.playerCameraHooked = (VRCompanion.playerCameraHooked ~= nil) and VRCompanion.playerCameraHooked or false
VRCompanion.enterableHooked = (VRCompanion.enterableHooked ~= nil) and VRCompanion.enterableHooked or false
VRCompanion.missionUpdateHooked = (VRCompanion.missionUpdateHooked ~= nil) and VRCompanion.missionUpdateHooked or false
VRCompanion.vehicleSpeedHooked = (VRCompanion.vehicleSpeedHooked ~= nil) and VRCompanion.vehicleSpeedHooked or false
VRCompanion.stereoToggled = (VRCompanion.stereoToggled ~= nil) and VRCompanion.stereoToggled or false
VRCompanion.inMissionUpdate = (VRCompanion.inMissionUpdate ~= nil) and VRCompanion.inMissionUpdate or false

function VRCompanion.getEngineGlobal(name)
    return _G and _G[name]
end

VRCompanion.LOOK_RAY_AIMER_ORDER = { "handTools", "objectGrab" }

local lookRayAimers = {}

function VRCompanion:registerLookRayAimer(name, aimer)
    if type(aimer) ~= "function" then return false end
    for _, known in ipairs(VRCompanion.LOOK_RAY_AIMER_ORDER) do
        if known == name then
            lookRayAimers[name] = aimer
            return true
        end
    end
    return false
end

local aimersLoggedAsRaising = {}

function VRCompanion:getAimedLookRay(player)
    for _, name in ipairs(VRCompanion.LOOK_RAY_AIMER_ORDER) do
        local aimer = lookRayAimers[name]
        if aimer ~= nil then
            local ok, x, y, z, dx, dy, dz = pcall(aimer, player)
            if ok and type(x) == "number" then return x, y, z, dx, dy, dz end
            if not ok and not aimersLoggedAsRaising[name] then
                aimersLoggedAsRaising[name] = true
                print(string.format("[FS25_VR] [Warning] Look ray aimer '%s' raised: %s", name, tostring(x)))
            end
        end
    end
    return nil
end

local subsystems = {}
local updateEarlyHooks = {}
local updateHooks = {}
local hooksLoggedAsRaising = {}

local function checkConsoleCommands(name, commands)
    if commands == nil then return end
    if type(commands) ~= "table" then
        error(string.format("registerSubsystem: the consoleCommands of '%s' are not a list", name), 3)
    end
    for index, command in ipairs(commands) do
        if type(command) ~= "table" then
            error(string.format("registerSubsystem: consoleCommands[%d] of '%s' is not a table", index, name), 3)
        end
        for _, field in ipairs({ "name", "description", "method" }) do
            if type(command[field]) ~= "string" or command[field] == "" then
                error(string.format("registerSubsystem: consoleCommands[%d] of '%s' has no %s", index, name, field), 3)
            end
        end
    end
end

local function rebuildUpdateHooks()
    updateEarlyHooks, updateHooks = {}, {}
    for _, subsystem in ipairs(subsystems) do
        local hooks = subsystem.hooks
        if hooks.updateEarly ~= nil then
            updateEarlyHooks[#updateEarlyHooks + 1] = { name = subsystem.name, hook = hooks.updateEarly }
        end
        if hooks.update ~= nil then
            updateHooks[#updateHooks + 1] = { name = subsystem.name, hook = hooks.update }
        end
    end
end

function VRCompanion:registerSubsystem(name, hooks)
    if type(name) ~= "string" or name == "" then
        error("registerSubsystem: the subsystem needs a name", 2)
    end
    hooks = hooks or {}
    for _, hookName in ipairs({ "mapLoad", "mapUnload", "update", "updateEarly", "saveSettings", "loadSettings" }) do
        if hooks[hookName] ~= nil and type(hooks[hookName]) ~= "function" then
            error(string.format("registerSubsystem: the %s hook of '%s' is not a function", hookName, name), 2)
        end
    end
    checkConsoleCommands(name, hooks.consoleCommands)
    local replaced = false
    for _, subsystem in ipairs(subsystems) do
        if subsystem.name == name then
            subsystem.hooks = hooks
            replaced = true
            break
        end
    end
    if not replaced then
        subsystems[#subsystems + 1] = { name = name, hooks = hooks }
    end
    rebuildUpdateHooks()
end

local function logHookRaised(name, hookName, err)
    local key = name .. "/" .. hookName
    if not hooksLoggedAsRaising[key] then
        hooksLoggedAsRaising[key] = true
        print(string.format("[FS25_VR] [Error] Subsystem '%s' %s hook raised: %s", name, hookName, tostring(err)))
    end
end

local function runSubsystemHook(self, subsystem, hookName, ...)
    local hook = subsystem.hooks[hookName]
    if hook == nil then return end
    local ok, err = pcall(hook, self, ...)
    if not ok then logHookRaised(subsystem.name, hookName, err) end
end

function VRCompanion:runSubsystemMapLoad()
    for _, subsystem in ipairs(subsystems) do
        runSubsystemHook(self, subsystem, "mapLoad")
    end
end

function VRCompanion:runSubsystemSaveSettings(xmlFile, setters)
    for _, subsystem in ipairs(subsystems) do
        runSubsystemHook(self, subsystem, "saveSettings", xmlFile, setters)
    end
end

function VRCompanion:runSubsystemLoadSettings(xmlFile, getters)
    for _, subsystem in ipairs(subsystems) do
        runSubsystemHook(self, subsystem, "loadSettings", xmlFile, getters)
    end
end

function VRCompanion:addSubsystemConsoleCommands()
    if addConsoleCommand == nil then return end
    for _, subsystem in ipairs(subsystems) do
        for _, command in ipairs(subsystem.hooks.consoleCommands or {}) do
            addConsoleCommand(command.name, command.description, command.method, self)
        end
    end
end

function VRCompanion:runSubsystemUpdateEarly(dt)
    for i = 1, #updateEarlyHooks do
        local entry = updateEarlyHooks[i]
        local ok, err = pcall(entry.hook, self, dt)
        if not ok then logHookRaised(entry.name, "updateEarly", err) end
    end
end

function VRCompanion:runSubsystemUpdate(dt)
    for i = 1, #updateHooks do
        local entry = updateHooks[i]
        local ok, err = pcall(entry.hook, self, dt)
        if not ok then logHookRaised(entry.name, "update", err) end
    end
end

function VRCompanion:runSubsystemMapUnload()
    for i = #subsystems, 1, -1 do
        local subsystem = subsystems[i]
        if removeConsoleCommand ~= nil then
            for _, command in ipairs(subsystem.hooks.consoleCommands or {}) do
                removeConsoleCommand(command.name)
            end
        end
        runSubsystemHook(self, subsystem, "mapUnload")
    end
end
VRCompanion.CORE_CONSOLE_COMMANDS = {
    { name = "vrHudSafeArea", description = "Set the VR HUD Safe Area, the margin that keeps warnings, notifications and other mods' HUDs inside the lens: vrHudSafeArea <horizontal> <vertical>", method = "consoleCommandSetHudSafeArea" },
    { name = "vrHudGroup", description = "Set a VR HUD Group Margin and scale: vrHudGroup <map|speedometer|info|controls> <horizontal> <vertical> [scale]", method = "consoleCommandSetHudGroup" },
    { name = "vrHudDepth", description = "How far away the HUD is in the headset, in metres: further away it looks smaller and nearer the middle of the view (saved; 2.5 by default): vrHudDepth [1.0-5.0]", method = "consoleCommandSetHudDepth" },
    { name = "vrHudTrace", description = "Log to log.txt where the HUD is drawn for each eye, for the next rendered frames (diagnostics): vrHudTrace [frames]", method = "consoleCommandHudTrace" },
    { name = "vrHudEyeTest", description = "Move the HUD far right in the frames rendered for one eye only, to check which eye shows them (diagnostics): vrHudEyeTest [left|right|off]", method = "consoleCommandHudEyeTest" },
    { name = "vrEyeSnapshot", description = "Write the next frame handed to each eye as fs25_vr_eye0.bmp / fs25_vr_eye1.bmp next to fs25_vr.log (diagnostics): vrEyeSnapshot", method = "consoleCommandEyeSnapshot" },
    { name = "vrHudState", description = "Set VR HUD state: vrHudState <0=Hidden | 1=Streamlined | 2=Full>", method = "consoleCommandSetHudState" },
    { name = "vrHudLoadNotification", description = "Toggle HUD alert on map load: vrHudLoadNotification <true|false>", method = "consoleCommandSetHudLoadNotification" },
    { name = "vrHudLoadAlert", description = "Toggle HUD alert on map load: vrHudLoadAlert <true|false>", method = "consoleCommandSetHudLoadNotification" },
    { name = "vrRecenterMode", description = "Set VR Recenter Action mode: vrRecenterMode [Both | VehicleOnly | toggle]", method = "consoleCommandSetRecenterMode" },
    { name = "vrRecenterTracking", description = "Set VR Recenter Action mode: vrRecenterTracking [Both | VehicleOnly | toggle]", method = "consoleCommandSetRecenterMode" },
    { name = "vrSnapTurnAngle", description = "Set or query VR snap turn angle in degrees: vrSnapTurnAngle [degrees]", method = "consoleCommandSetSnapTurnAngle" },
    { name = "vr2DView", description = "Set or query 2D Floating Window Mode: vr2DView [true|false|toggle]", method = "consoleCommandSet2DView" },
    { name = "vrToggleView", description = "Toggle 2D Floating Window Mode: vrToggleView", method = "consoleCommandToggleView" },
    { name = "vrDlssMvScale", description = "Set DLSS motion vector scale multiplier: vrDlssMvScale <scaleX> [scaleY]", method = "consoleCommandSetDlssMvScale" },
    { name = "vrDlssSharpness", description = "Set DLSS sharpness override: vrDlssSharpness <sharpness 0.0-1.0>", method = "consoleCommandSetDlssSharpness" },
    { name = "vrDlssPreset", description = "Set DLSS preset hint: vrDlssPreset <0=Default|3=C|4=D|10=J|11=K|12=L>", method = "consoleCommandSetDlssPreset" },
    { name = "vrDlssSdpMvScale", description = "Set DLSS motion vector scale under Sequential Dual-Pass: vrDlssSdpMvScale <scaleX> [scaleY]", method = "consoleCommandSetDlssSdpMvScale" },
    { name = "vrDlssMvFix", description = "Switch the DLSS motion vector pass for A/B comparison: vrDlssMvFix <on|off>", method = "consoleCommandDlssMvFix" },
    { name = "vrDlssDepthModel", description = "Set the depth model of the DLSS motion vector correction for A/B comparison: vrDlssDepthModel <infinite|finite>", method = "consoleCommandDlssDepthModel" },
    { name = "vrDlssNearClip", description = "Set the near plane of the DLSS motion vector correction for every camera in metres (0 or auto = estimate it automatically): vrDlssNearClip <meters|auto>", method = "consoleCommandDlssNearClip" },
    { name = "vrDlssNearClipCab", description = "Set the DLSS near plane for a vehicle's inside camera: vrDlssNearClipCab <meters|auto>", method = "consoleCommandDlssNearClipCab" },
    { name = "vrDlssNearClipOutside", description = "Set the DLSS near plane for a vehicle's outside cameras: vrDlssNearClipOutside <meters|auto>", method = "consoleCommandDlssNearClipOutside" },
    { name = "vrDlssNearClipOnFoot", description = "Set the DLSS near plane on foot: vrDlssNearClipOnFoot <meters|auto>", method = "consoleCommandDlssNearClipOnFoot" },
    { name = "vrDlssDiag", description = "Trace DLSS evaluations to fs25_vr.log: vrDlssDiag [evaluations]", method = "consoleCommandDlssDiag" },
    { name = "vrRenderingMode", description = "Set Stereo Presentation Mode: vrRenderingMode <afr|dualpass|toggle>", method = "consoleCommandSetRenderingMode" },
    { name = "vrHudGroupShow", description = "Show or hide a HUD Group in the Streamlined or Full HUD: vrHudGroupShow <map|speedometer|info|controls> <streamlined|full> <on|off>", method = "consoleCommandHudGroupShow" },
}

local function mismatchedInjector(self, injectorVersion, older, version)
    if type(injectorVersion) ~= "string" or injectorVersion == self.VERSION then return nil end
    if injectorVersion == self.LEGACY_INJECTOR_VERSION then return older() end
    return version(injectorVersion)
end

function VRCompanion:describeVersionMismatch(injectorVersion)
    local injectorText = mismatchedInjector(self, injectorVersion,
        function() return "an older version" end,
        function(version) return "version " .. version end)
    if injectorText == nil then return nil end
    return string.format("The Companion Mod is version %s but the Injector is %s. Install both files from the same release.",
        self.VERSION, injectorText)
end

function VRCompanion:describeVersionMismatchOnScreen(injectorVersion)
    local injectorText = mismatchedInjector(self, injectorVersion,
        function() return VRCompanion.getLocalisedText("vrVersion_injectorOlder", "an older version") end,
        function(version) return VRCompanion.formatLocalisedText("vrVersion_injectorVersion", "version %s", version) end)
    if injectorText == nil then return nil end
    return VRCompanion.formatLocalisedText("vrVersion_mismatch",
        "FS25 VR: The Companion Mod is version %s but the Injector is %s. Install both files from the same release.",
        self.VERSION, injectorText)
end

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

    if VRBridge == nil and type(isHeadTrackingAvailable) == "function" then
        pcall(isHeadTrackingAvailable)
    end

    self:set2DViewActive(false)
    self:resetToggleViewDebounce()

    local versionWarning = nil
    local versionWarningOnScreen = nil
    if VRBridge ~= nil then
        local version = VRBridge.version or "unknown"
        local ready = false
        if type(VRBridge.isReady) == "function" then
            ready = VRBridge.isReady()
        end

        local handshakeResult = nil
        if type(VRBridge.handshake) == "function" then
            handshakeResult = VRBridge.handshake(nil, self.VERSION)
        end

        local injectorVersion = type(handshakeResult) == "table" and handshakeResult.version or VRBridge.version
        print(string.format("[FS25_VR] Versions: Companion Mod %s, Injector %s.", self.VERSION, tostring(injectorVersion)))
        versionWarning = self:describeVersionMismatch(injectorVersion)
        versionWarningOnScreen = self:describeVersionMismatchOnScreen(injectorVersion)
        if versionWarning ~= nil then
            print("[FS25_VR] [Warning] " .. versionWarning)
        end

        if handshakeResult and handshakeResult.success == true then
            self.handshakeComplete = true

            local nativeMode = handshakeResult.renderingMode
            if nativeMode == nil and type(VRBridge.getRenderingMode) == "function" then
                nativeMode = VRBridge.getRenderingMode()
            end

            if nativeMode == "AFR" or nativeMode == 0 then
                self.renderingMode = self.RENDERING_MODE_AFR
            else
                self.renderingMode = self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
            end

            print(string.format("[FS25_VR] Handshake successful! VRBridge detected (version: %s, mode: %s, ready: %s, renderingMode: %s).",
                tostring(handshakeResult.version or version),
                tostring(handshakeResult.mode or "unknown"),
                tostring(ready),
                tostring(self:getRenderingMode())))
        else
            self.handshakeComplete = false
            print(string.format("[FS25_VR] Handshake failed or rejected by native VRBridge (version: %s, ready: %s).",
                tostring(version),
                tostring(ready)))
        end
    else
        self.renderingMode = self.DEFAULT_RENDERING_MODE
        print("[FS25_VR] VRBridge not detected. Running in standard 2D Pass-Through Mode.")
    end

    if type(setStereoRendering) == "function" then
        setStereoRendering(true)
        self.stereoToggled = true
        print("[FS25_VR] Invoked engine stereo rendering toggle: setStereoRendering(true)")
    else
        print("[FS25_VR] Engine stereo rendering toggle 'setStereoRendering' not found in Luau environment.")
    end

    if VRBridge ~= nil then
        print(string.format("[FS25_VR] Stereo presentation: AFR fallback required: %s.",
            tostring(self:requiresAfrFallback())))
    end

    if g_gameSettings ~= nil and type(g_gameSettings.setValue) == "function" and GameSettings ~= nil and GameSettings.SETTING ~= nil and GameSettings.SETTING.IS_HEAD_TRACKING_ENABLED ~= nil then
        pcall(function() g_gameSettings:setValue(GameSettings.SETTING.IS_HEAD_TRACKING_ENABLED, true) end)
        print("[FS25_VR] Confirmed game setting IS_HEAD_TRACKING_ENABLED is active.")
    end

    self:hookVehicleCamera()
    self:hookPlayerCamera()
    self:hookEnterable()
    self:hookVehicleSpeed()
    self:hookHud()
    self:hookMissionUpdate()

    self.isMenuVisible = false
    self.menuTakesInput = false
    self.stereoDialogShowing = false
    self.guiVisibilityInitialized = false
    self:pollGuiVisibility()

    self:loadConfiguration()
    self:runSubsystemMapLoad()
    self:applyDlssNearClip(true)

    if addConsoleCommand ~= nil then
        for _, command in ipairs(self.CORE_CONSOLE_COMMANDS) do
            addConsoleCommand(command.name, command.description, command.method, self)
        end
    end
    self:addSubsystemConsoleCommands()

    if self.showHudLoadNotification == true then
        local mission = g_currentMission or _G.g_currentMission
        if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
            local alert = self.HUD_LOAD_ALERT_TEXTS[self:getHudState()] or self.HUD_LOAD_ALERT_TEXTS[self.HUD_STATE_HIDDEN]
            local text = VRCompanion.getLocalisedText(alert.key, alert.english)
            pcall(function()
                mission:showBlinkingWarning(text, 4000)
            end)
        end
    end

    if versionWarning ~= nil then
        local mission = g_currentMission or _G.g_currentMission
        if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
            pcall(function()
                mission:showBlinkingWarning(versionWarningOnScreen, self.VERSION_WARNING_MS)
            end)
        end
    end
end

function VRCompanion:pollGuiVisibility()
    local isGuiVisible = false
    local gui = g_gui or _G.g_gui
    if gui ~= nil and type(gui.getIsGuiVisible) == "function" then
        local ok, visible = pcall(gui.getIsGuiVisible, gui)
        if ok then
            isGuiVisible = (visible == true)
        end
    end

    local wasStereoDialogShowing = self.stereoDialogShowing == true
    self.stereoDialogShowing = isGuiVisible and not self.is2DViewActive and self:findStereoDialog(gui) ~= nil
    local menuQuadShown = isGuiVisible and not self.stereoDialogShowing
    self.menuTakesInput = isGuiVisible

    if self.isMenuVisible ~= menuQuadShown or not self.guiVisibilityInitialized then
        local wasMenuQuadShown = self.isMenuVisible
        self.isMenuVisible = menuQuadShown
        self.guiVisibilityInitialized = true
        if wasMenuQuadShown and not menuQuadShown then
            self:restoreMenuBackgroundCamera()
            if self.is2DViewActive then
                self:restoreHudSafeArea(true)
            else
                self:applyHudState()
            end
        end
        if VRBridge ~= nil and type(VRBridge.setMenuVisible) == "function" then
            VRBridge.setMenuVisible(menuQuadShown)
        end
        if VRBridge ~= nil and type(VRBridge.resetTemporalHistory) == "function" then
            VRBridge.resetTemporalHistory()
        end
        if isGuiVisible and not self.stereoDialogShowing then
            print("[FS25_VR] In-game GUI menu opened; switched presentation to 6DOF Quad layer.")
        elseif self.frameTicks > 0 then
            if self.is2DViewActive then
                print("[FS25_VR] In-game GUI menu closed; remaining in 2D Floating Window Mode.")
            else
                print("[FS25_VR] In-game GUI menu closed; restored stereoscopic 3D projection rendering.")
            end
        end
    end
    if self.stereoDialogShowing and not wasStereoDialogShowing then
        print("[FS25_VR] In-game GUI Stereo Dialog opened; kept the stereoscopic view, the menu takes the input.")
    elseif wasStereoDialogShowing and not self.stereoDialogShowing then
        print("[FS25_VR] In-game GUI Stereo Dialog closed.")
    end
    return isGuiVisible
end

VRCompanion.STEREO_DIALOGS = {
    FishDialog = true,
}

local function readLoneStereoDialog(gui)
    local screen = gui.currentGuiName
    if screen ~= nil and screen ~= "" then return nil end
    local dialogs = gui.dialogs
    if type(dialogs) ~= "table" or #dialogs ~= 1 then return nil end
    local name = gui.currentDialogName
    if type(name) ~= "string" or VRCompanion.STEREO_DIALOGS[name] ~= true then return nil end
    return dialogs[1]
end

function VRCompanion:findStereoDialog(gui)
    gui = gui or g_gui or _G.g_gui
    if type(gui) ~= "table" then return nil end
    local ok, root = pcall(readLoneStereoDialog, gui)
    return ok and root or nil
end

function VRCompanion:isMenuQuadShown()
    return self.isMenuVisible == true
end

function VRCompanion:doesMenuTakeInput()
    return self.menuTakesInput == true or self.isMenuVisible == true
end

function VRCompanion:isStereoDialogShowing()
    return self.stereoDialogShowing == true
end

function VRCompanion:getPlayerCamera()
    if self.activePlayerCamera ~= nil then
        return self.activePlayerCamera
    end
    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and mission.player ~= nil and mission.player.camera ~= nil then
        return mission.player.camera
    end
    if g_localPlayer ~= nil and g_localPlayer.camera ~= nil then
        return g_localPlayer.camera
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
    if veh == nil or type(veh) ~= "table" then return nil end
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

function VRCompanion:getDlssNearClipMode()
    local veh = self:getControlledVehicle()
    if veh == nil then return self.DLSS_NEAR_CLIP_ON_FOOT end
    local cam = self:getVehicleActiveCamera(veh)
    if cam ~= nil and cam.isInside then return self.DLSS_NEAR_CLIP_CAB end
    return self.DLSS_NEAR_CLIP_OUTSIDE
end

function VRCompanion:getDlssNearClipForCurrentCamera()
    local cab, outside, onFoot = self.dlssNearClipCab, self.dlssNearClipOutside, self.dlssNearClipOnFoot
    if cab ~= nil and cab == outside and outside == onFoot then return cab end
    local mode = self:getDlssNearClipMode()
    return self[self.DLSS_NEAR_CLIP_FIELDS[mode]] or self.DEFAULT_DLSS_NEAR_CLIPS[mode]
end

function VRCompanion:applyDlssNearClip(force)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.setDlssNearClip) ~= "function" then return false end
    local meters = self:getDlssNearClipForCurrentCamera()
    if not force and self.appliedDlssNearClip == meters then return false end
    self.appliedDlssNearClip = meters
    return bridge.setDlssNearClip(meters) == true
end

function VRCompanion:hookVehicleCamera()
    if VehicleCamera ~= nil and VehicleCamera.update ~= nil and not self.vehicleCameraHooked then
        self.origVehicleCameraUpdate = VehicleCamera.update
        VehicleCamera.update = function(cam, dt)
            self.origVehicleCameraUpdate(cam, dt)
            if VRCompanion.perfTracing then VRCompanion:perfTraceBegin("modCamera") end
            VRCompanion:onVehicleCameraUpdate(cam, dt)
            if VRCompanion.perfTracing then VRCompanion:perfTraceEnd("modCamera") end
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

function VRCompanion:hookTableSpeed(target, label)
    if target == nil or type(target.updateVehicleSpeed) ~= "function" then
        return
    end
    if self.origVehicleSpeedFuncs[target] ~= nil or self.origClassSpeedFuncs[target] ~= nil then
        return
    end
    local origFn = target.updateVehicleSpeed
    if label then
        self.origClassSpeedFuncs[target] = origFn
    else
        self.origVehicleSpeedFuncs[target] = origFn
    end
    target.updateVehicleSpeed = function(vehSelf, dt, ...)
        if dt == nil or dt <= 0.00001 then
            return
        end
        return origFn(vehSelf, dt, ...)
    end
    if label then
        print(string.format("[FS25_VR] Hooked %s.updateVehicleSpeed for dt=0 guard.", label))
    end
end

function VRCompanion:installVehicleSpeedGuards(scanGlobals)
    if self.origVehicleSpeedFuncs == nil then
        self.origVehicleSpeedFuncs = setmetatable({}, { __mode = "k" })
    end
    if self.origClassSpeedFuncs == nil then
        self.origClassSpeedFuncs = {}
    end

    self:hookTableSpeed(Vehicle or (_G and _G.Vehicle), "Vehicle")
    self:hookTableSpeed(RailroadVehicle or (_G and _G.RailroadVehicle), "RailroadVehicle")
    self:hookTableSpeed(Locomotive or (_G and _G.Locomotive), "Locomotive")

    if scanGlobals and _G ~= nil then
        for name, val in pairs(_G) do
            if type(name) == "string" and type(val) == "table" then
                self:hookTableSpeed(val, name)
            end
        end
    end

    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and mission.vehicles ~= nil then
        for _, veh in pairs(mission.vehicles) do
            if type(veh) == "table" then
                self:hookTableSpeed(veh, nil)
            end
        end
    end

    local ferryClass = Ferry or (_G and _G.Ferry)
    if ferryClass ~= nil and type(ferryClass.updatePosition) == "function" and self.origFerryUpdatePosition == nil then
        self.origFerryUpdatePosition = ferryClass.updatePosition
        ferryClass.updatePosition = function(ferrySelf, dt, ...)
            if dt == nil or dt <= 0.00001 then
                return
            end
            return self.origFerryUpdatePosition(ferrySelf, dt, ...)
        end
        print("[FS25_VR] Hooked Ferry.updatePosition for dt=0 guard.")
    end

    self.vehicleSpeedHooked = true
    self.lastVehicleSpeedHookTick = self.frameTicks or 0
end

function VRCompanion:hookVehicleSpeed()
    self:installVehicleSpeedGuards(true)
end

function VRCompanion:refreshVehicleSpeedGuards()
    self:installVehicleSpeedGuards(false)
end

function VRCompanion:unhookVehicleSpeed()
    for _, originals in ipairs({ self.origVehicleSpeedFuncs or {}, self.origClassSpeedFuncs or {} }) do
        for target, origFn in pairs(originals) do
            if target ~= nil and origFn ~= nil then
                target.updateVehicleSpeed = origFn
            end
        end
    end
    self.origVehicleSpeedFuncs = nil
    self.origClassSpeedFuncs = nil

    local ferryClass = Ferry or (_G and _G.Ferry)
    if self.origFerryUpdatePosition ~= nil and ferryClass ~= nil then
        ferryClass.updatePosition = self.origFerryUpdatePosition
        self.origFerryUpdatePosition = nil
    end

    self.vehicleSpeedHooked = false
    print("[FS25_VR] Unhooked Vehicle/Ferry dt=0 guards.")
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
                if VRCompanion.is2DViewActive and VRCompanion.origPlayerCameraUpdateRotation ~= nil then
                    VRCompanion.origPlayerCameraUpdateRotation(cam, dt)
                    return
                end
                if VRCompanion.perfTracing then VRCompanion:perfTraceBegin("modCamera") end
                VRCompanion:onPlayerCameraUpdateRotation(cam, dt)
                if VRCompanion.perfTracing then VRCompanion:perfTraceEnd("modCamera") end
            end
        end
        if PlayerCamera.updatePosition ~= nil then
            self.origPlayerCameraUpdatePosition = PlayerCamera.updatePosition
            PlayerCamera.updatePosition = function(cam, dt)
                self.origPlayerCameraUpdatePosition(cam, dt)
                if VRCompanion.perfTracing then VRCompanion:perfTraceBegin("modCamera") end
                VRCompanion:onPlayerCameraUpdate(cam, dt)
                if VRCompanion.perfTracing then VRCompanion:perfTraceEnd("modCamera") end
            end
        elseif PlayerCamera.update ~= nil then
            self.origPlayerCameraUpdate = PlayerCamera.update
            PlayerCamera.update = function(cam, dt)
                self.origPlayerCameraUpdate(cam, dt)
                if VRCompanion.perfTracing then VRCompanion:perfTraceBegin("modCamera") end
                VRCompanion:onPlayerCameraUpdate(cam, dt)
                if VRCompanion.perfTracing then VRCompanion:perfTraceEnd("modCamera") end
            end
        end
        self.playerCameraHooked = true
        print("[FS25_VR] Hooked PlayerCamera for 6DOF VR head tracking.")
    end
end

local traceback = (debug ~= nil) and debug.traceback or nil

local function withRaiseTraceback(err)
    if traceback ~= nil and type(err) == "string" then
        return traceback(err, 2)
    end
    return err
end

local function endNestedFrameChecks(self, ...)
    self:endFrameChecks()
    return ...
end

local function endOutermostFrameChecks(self, ok, ...)
    self:resetFrameChecks()
    if not ok then
        self.inMissionUpdate = false
        error((...), 0)
    end
    return ...
end

local function runWithFrameChecks(self, fn, ...)
    local outermost = (self.frameCheckDepth or 0) == 0
    self:beginFrameChecks()
    if outermost then
        return endOutermostFrameChecks(self, xpcall(fn, withRaiseTraceback, self, ...))
    end
    return endNestedFrameChecks(self, fn(self, ...))
end

function VRCompanion:onMissionUpdate(missionSelf, dt, origUpdateFn, ...)
    if self.inMissionUpdate then
        return origUpdateFn(missionSelf, dt, ...)
    end
    return runWithFrameChecks(self, self.runMissionUpdate, missionSelf, dt, origUpdateFn, ...)
end

local function endAfrGameUpdate(self, ...)
    self.inMissionUpdate = false
    return ...
end

function VRCompanion:runMissionUpdate(missionSelf, dt, origUpdateFn, ...)

    local isSequentialDualPass = (self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS)
        and not self.is2DViewActive
        and not self:isMenuQuadShown()
        and not self:isPassThroughMode()

    if not isSequentialDualPass then
        self.accumulatedPass1Dt = 0.0
        self.deferRenderingCameraBind = false
        self:endEyeProjectionScope()
        self:releaseDualPassPhysicsPause()
        self.inMissionUpdate = true
        if self.perfTracing then
            self:perfTraceBegin("gameUpdate")
            return endAfrGameUpdate(self, self:perfTraceEnd("gameUpdate", origUpdateFn(missionSelf, dt, ...)))
        end
        return endAfrGameUpdate(self, origUpdateFn(missionSelf, dt, ...))
    end

    local rawDt = dt or 0.0
    if rawDt < 0.0 then
        rawDt = 0.0
    end

    local subPass = self:getSubPassIndex() % 2
    local eye = self:getDualPassEye()

    self:beginEyeProjectionScope()
    self.deferRenderingCameraBind = true

    local camLeft, camRight = self:getStereoCameraNodes()
    local targetCam = (eye == 1) and camRight or camLeft

    if subPass == 1 then
        self.accumulatedPass1Dt = (self.accumulatedPass1Dt or 0.0) + rawDt
        if self.accumulatedPass1Dt > 200.0 then
            self.accumulatedPass1Dt = 200.0
        end

        if targetCam ~= nil then
            self:runSubPassStep(subPass, self.applyCameraProjection, targetCam, eye)
        end

        self:finishSubPass(subPass, eye, targetCam, rawDt)
        self:updateHudConvergence(0.0)
        self:applyDualPassPhysicsPause(subPass, rawDt)

        return nil
    end

    local effectiveDt = rawDt + (self.accumulatedPass1Dt or 0.0)
    self.accumulatedPass1Dt = 0.0
    if effectiveDt > 200.0 then
        effectiveDt = 200.0
    end

    if targetCam ~= nil then
        self:runSubPassStep(subPass, self.applyCameraProjection, targetCam, eye)
    end

    self.inMissionUpdate = true
    if self.perfTracing then self:perfTraceBegin("gameUpdate") end
    local ok, res = pcall(origUpdateFn, missionSelf, effectiveDt, ...)
    if self.perfTracing then self:perfTraceEnd("gameUpdate") end
    self.inMissionUpdate = false
    if not ok then
        if self.lastMissionUpdateError ~= tostring(res) then
            print(string.format("[FS25_VR] [Warning] Mission update error on Sub-Pass %d (dt=%s): %s",
                subPass, tostring(effectiveDt), tostring(res)))
            self.lastMissionUpdateError = tostring(res)
        end
        res = nil
    end

    self:finishSubPass(subPass, eye, targetCam, rawDt)
    self:updateHudConvergence(effectiveDt)
    self:applyDualPassPhysicsPause(subPass, rawDt)

    return res
end

function VRCompanion:runSubPassStep(subPass, fn, ...)
    local ok, err = pcall(fn, self, ...)
    if not ok and self.lastMissionUpdateError ~= tostring(err) then
        print(string.format("[FS25_VR] [Warning] Camera update error on Sub-Pass %d: %s", subPass, tostring(err)))
        self.lastMissionUpdateError = tostring(err)
    end
end

function VRCompanion:finishSubPass(subPass, eyeIndex, targetCam, frameDt)
    self.trackedCameraShown = nil
    self:runSubPassStep(subPass, self.syncActiveEyeCamera, eyeIndex)
    self.deferRenderingCameraBind = false
    if self.trackedCameraShown ~= false then
        self:bindRenderingCamera(targetCam)
        self:runSubPassStep(subPass, self.reportEyeCamera, eyeIndex, targetCam)
        self:runSubPassStep(subPass, self.traceEyeFrame, eyeIndex, targetCam, subPass, frameDt)
    end
    self:endEyeProjectionScope()
end

function VRCompanion:reportEyeCamera(eyeIndex, camNode)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.reportCameraRecord) ~= "function" then return false end
    if camNode == nil or camNode == 0 then return false end

    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    local getWorldQuaternionFn = getWorldQuaternion or (_G and _G.getWorldQuaternion)
    local getFovYFn = getFovY or (_G and _G.getFovY)
    local getNearClipFn = getNearClip or (_G and _G.getNearClip)
    local getFarClipFn = getFarClip or (_G and _G.getFarClip)
    local getProjectionOffsetFn = getProjectionOffset or (_G and _G.getProjectionOffset)
    if type(getWorldTranslationFn) ~= "function" or type(getWorldQuaternionFn) ~= "function"
        or type(getFovYFn) ~= "function" or type(getNearClipFn) ~= "function"
        or type(getFarClipFn) ~= "function" then
        return false
    end

    local x, y, z = getWorldTranslationFn(camNode)
    local qx, qy, qz, qw = getWorldQuaternionFn(camNode)
    local fovY = getFovYFn(camNode)
    local nearClip = getNearClipFn(camNode)
    local farClip = getFarClipFn(camNode)
    if x == nil or qw == nil or fovY == nil or nearClip == nil or farClip == nil then return false end

    local offsetX, offsetY = 0, 0
    if type(getProjectionOffsetFn) == "function" then
        offsetX, offsetY = getProjectionOffsetFn(camNode)
    end

    return bridge.reportCameraRecord(eyeIndex % 2, x, y, z, qx, qy, qz, qw, fovY, nearClip, farClip,
        offsetX or 0, offsetY or 0)
end

function VRCompanion:reportAfrEyeCamera(eyeIndex, camNode)
    if self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then return false end
    local ok, reported = pcall(self.reportEyeCamera, self, eyeIndex, camNode)
    return ok and reported == true
end
function VRCompanion:hookMissionUpdate()
    if self.missionUpdateHooked then
        return
    end

    local hooked = false

    local fsBase = FSBaseMission or (_G and _G.FSBaseMission)
    if fsBase ~= nil and type(fsBase.update) == "function" and self.origFSBaseMissionUpdate == nil then
        self.origFSBaseMissionUpdate = fsBase.update
        fsBase.update = function(missionSelf, dt, ...)
            return self:onMissionUpdate(missionSelf, dt, self.origFSBaseMissionUpdate, ...)
        end
        hooked = true
    end

    local m00 = Mission00 or (_G and _G.Mission00)
    if m00 ~= nil and type(m00.update) == "function" and self.origMission00Update == nil then
        self.origMission00Update = m00.update
        m00.update = function(missionSelf, dt, ...)
            return self:onMissionUpdate(missionSelf, dt, self.origMission00Update, ...)
        end
        hooked = true
    end

    self.missionUpdateHooked = hooked
    if hooked then
        print("[FS25_VR] Hooked mission update loop for Sequential Dual-Pass simulation pacing.")
    end
end

function VRCompanion:unhookMissionUpdate()
    local fsBase = FSBaseMission or (_G and _G.FSBaseMission)
    if self.origFSBaseMissionUpdate ~= nil and fsBase ~= nil then
        fsBase.update = self.origFSBaseMissionUpdate
        self.origFSBaseMissionUpdate = nil
    end

    local m00 = Mission00 or (_G and _G.Mission00)
    if self.origMission00Update ~= nil and m00 ~= nil then
        m00.update = self.origMission00Update
        self.origMission00Update = nil
    end

    self.accumulatedPass1Dt = 0.0
    self.deferRenderingCameraBind = false
    self:endEyeProjectionScope()
    self.missionUpdateHooked = false
    print("[FS25_VR] Unhooked mission update loop.")
end

function VRCompanion:update(dt)
    if self.perfTracing then
        self:perfTraceBegin("modUpdate")
        return self:perfTraceEnd("modUpdate", runWithFrameChecks(self, self.runUpdate, dt))
    end
    return runWithFrameChecks(self, self.runUpdate, dt)
end

function VRCompanion:runUpdate(dt)
    self.frameTicks = (self.frameTicks or 0) + 1
    if self.perfTracing then self:refreshPerfTracing() end

    if VRBridge ~= nil and type(VRBridge.getIs2DViewActive) == "function" then
        local bridge2D = VRBridge.getIs2DViewActive()
        if bridge2D ~= nil and bridge2D ~= self.is2DViewActive then
            self:set2DViewActive(bridge2D)
        end
    end

    self:runSubsystemUpdateEarly(dt)

    self:pollGuiVisibility()

    if self:isMenuQuadShown() then
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
    if not self.missionUpdateHooked then
        self:hookMissionUpdate()
    end
    if self.guiDrawHook == nil then
        self:hookGuiDraw()
    end
    local speedHookRetryDue = self.vehicleSpeedHooked
        and (self.frameTicks - (self.lastVehicleSpeedHookTick or 0)) >= self.VEHICLE_SPEED_HOOK_RETRY_TICKS
    if not self.vehicleSpeedHooked then
        self:hookVehicleSpeed()
    elseif speedHookRetryDue then
        self:refreshVehicleSpeedGuards()
    end

    self:updateHudConvergence(dt)

    self:updateActionEvents()

    local curVehicle = self:getControlledVehicle()
    if self.lastControlledVehicle ~= curVehicle then
        local oldVeh = self.lastControlledVehicle
        self.lastControlledVehicle = curVehicle
        self:refreshVehicleSpeedGuards()
        self:onVehicleTransition(oldVeh, curVehicle)
    end

    self:runSubsystemUpdate(dt)

    self:applyDlssNearClip(false)
end

function VRCompanion:deleteMap()
    print("[FS25_VR] Unloading Companion Mod from map.")

    self.deferRenderingCameraBind = false
    self:restoreGameActiveCamera()

    if self.stereoToggled and type(setStereoRendering) == "function" then
        setStereoRendering(false)
        print("[FS25_VR] Restored engine stereo rendering toggle: setStereoRendering(false)")
        self.stereoToggled = false
    end

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

    self:unhookHud()
    self:unhookGuiDraw()

    self:unhookMissionUpdate()

    self:unhookVehicleSpeed()

    self.inMissionUpdate = false
    self.handshakeComplete = false
    self:clearFrameChecks()
    self.frameTicks = 0
    self.lastVehicleSpeedHookTick = 0
    self.vehicleCameraHooked = false
    self.playerCameraHooked = false
    self.activePlayerCamera = nil
    self.activeRenderingCamera = nil
    self.subPassIndex = 0
    self.accumulatedPass1Dt = 0.0
    self.enterableHooked = false
    self.lastControlledVehicle = nil
    self.appliedDlssNearClip = nil
    self.isMenuVisible = false
    self.menuTakesInput = false
    self.stereoDialogShowing = false
    self.guiVisibilityInitialized = false

    if VRBridge ~= nil and type(VRBridge.setMenuVisible) == "function" then
        VRBridge.setMenuVisible(false)
    end

    if VRBridge ~= nil and type(VRBridge.onMapUnload) == "function" then
        VRBridge.onMapUnload()
    end

    self.passThroughMode = nil
    self.headTrackingEnabled = true

    if removeConsoleCommand ~= nil then
        for _, command in ipairs(self.CORE_CONSOLE_COMMANDS) do
            removeConsoleCommand(command.name)
        end
    end

    self:runSubsystemMapUnload()

end
