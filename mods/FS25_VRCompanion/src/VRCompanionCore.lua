-- ====================================================================
-- FS25 VR Companion Mod: Core Subsystem
-- Manages map lifecycle (loadMap, deleteMap, update), GUI visibility polling,
-- camera/vehicle discovery, speed limit dt=0 guards, and mission loop hooks.
-- ====================================================================

VRCompanion = VRCompanion or {}

VRCompanion.name = VRCompanion.name or g_currentModName or "FS25_VRCompanion"

-- Slow retry cadence (in update ticks) for re-running the dt=0 guard install so a late
-- Ferry class or newly spawned vehicles are picked up without scanning _G every frame.
VRCompanion.VEHICLE_SPEED_HOOK_RETRY_TICKS = 180

VRCompanion.handshakeComplete = (VRCompanion.handshakeComplete ~= nil) and VRCompanion.handshakeComplete or false
VRCompanion.frameTicks = VRCompanion.frameTicks or 0
VRCompanion.lastControlledVehicle = VRCompanion.lastControlledVehicle or nil
-- The menu quad is shown (presentation); see VRCompanion:isMenuQuadShown
VRCompanion.isMenuVisible = (VRCompanion.isMenuVisible ~= nil) and VRCompanion.isMenuVisible or false
-- A menu takes the input; see VRCompanion:doesMenuTakeInput
VRCompanion.menuTakesInput = (VRCompanion.menuTakesInput ~= nil) and VRCompanion.menuTakesInput or false
-- A Stereo Dialog is what is showing instead of the menu quad (ADR-0032); see VRCompanion:isStereoDialogShowing
VRCompanion.stereoDialogShowing = (VRCompanion.stereoDialogShowing ~= nil) and VRCompanion.stereoDialogShowing or false
VRCompanion.guiVisibilityInitialized = (VRCompanion.guiVisibilityInitialized ~= nil) and VRCompanion.guiVisibilityInitialized or false
VRCompanion.vehicleCameraHooked = (VRCompanion.vehicleCameraHooked ~= nil) and VRCompanion.vehicleCameraHooked or false
VRCompanion.playerCameraHooked = (VRCompanion.playerCameraHooked ~= nil) and VRCompanion.playerCameraHooked or false
VRCompanion.enterableHooked = (VRCompanion.enterableHooked ~= nil) and VRCompanion.enterableHooked or false
VRCompanion.missionUpdateHooked = (VRCompanion.missionUpdateHooked ~= nil) and VRCompanion.missionUpdateHooked or false
VRCompanion.vehicleSpeedHooked = (VRCompanion.vehicleSpeedHooked ~= nil) and VRCompanion.vehicleSpeedHooked or false
VRCompanion.stereoToggled = (VRCompanion.stereoToggled ~= nil) and VRCompanion.stereoToggled or false
VRCompanion.inMissionUpdate = (VRCompanion.inMissionUpdate ~= nil) and VRCompanion.inMissionUpdate or false

-- An engine global by name, or nil when the game does not have it: the engine's functions and tables are read through
-- this, so that one that is missing is a nil to test for and not an error. Virtual Hands and Wheel Grab alias it as
-- their local `engine`; this file loads before both.
function VRCompanion.getEngineGlobal(name)
    return _G and _G[name]
end

-- ---- Who aims the look ray ----------------------------------------------------------------------------------
-- The game's Player:getLookRay is wrapped once, by this file, for as long as a subsystem that aims holds the wrap (Hand Tools and
-- Object Grab each hold it, so that one giving up leaves the other aiming: see holdLookRayWrap). The wrap asks each subsystem
-- that can aim, in this fixed order, and answers the first ray it gets: the game's own (the head's) when none does. A subsystem
-- registers by name with an aimer, function(player) -> x, y, z, dirX, dirY, dirZ in the world, or nil when it is not aiming for
-- that player (Hand Tools: a supported Hand Tool is in a tracked Tool Hand; Object Grab: the hands are empty and the Pointer Hand
-- points).
VRCompanion.LOOK_RAY_AIMER_ORDER = { "handTools", "objectGrab" }

local lookRayAimers = {}

-- Registers (or replaces) the aimer of a subsystem named in LOOK_RAY_AIMER_ORDER. Returns whether it was taken.
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

-- The ray the first aiming subsystem answers for `player`, or nil (the game's own ray). An aimer that raises is not aiming: it is
-- logged once, by name, with its error, and the next one is asked.
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

-- ---- The one wrap of Player:getLookRay ------------------------------------------------------------------------------
-- The wrap is put on the class once and stays while at least one subsystem holds it (holdLookRayWrap), whoever asked for it
-- first. Each subsystem checks the shape of Player.getLookRay itself before holding it, and releases its hold when it gives up
-- (or at map unload), so that a subsystem giving up lifts the wrap only when it was the last one that needed it. The wrapper is
-- lifted only while it is still the class function, so another mod's later wrap is never undone.
local lookRayWrap = nil   -- { class = <Player>, original = <the game's function>, wrapper = <what the wrap put there> }
local lookRayHolders = {}

local function installLookRayWrap()
    local class = VRCompanion.getEngineGlobal("Player")
    if type(class) ~= "table" then return false end
    if lookRayWrap ~= nil and lookRayWrap.class == class then return true end
    local original = class.getLookRay
    if type(original) ~= "function" then return false end
    local wrapper = function(player, ...)
        if type(player) == "table" then
            local x, y, z, dx, dy, dz = VRCompanion:getAimedLookRay(player)
            if x ~= nil then return x, y, z, dx, dy, dz end
        end
        return original(player, ...)
    end
    lookRayWrap = { class = class, original = original, wrapper = wrapper }
    class.getLookRay = wrapper
    return true
end

-- Holds the wrap for the subsystem `owner` (a name), putting it on the class when it is not there. Returns whether it is there.
function VRCompanion:holdLookRayWrap(owner)
    if not installLookRayWrap() then return false end
    lookRayHolders[owner] = true
    return true
end

-- The subsystem lets go of the wrap; the class gets the game's function back when nobody holds it any more.
function VRCompanion:releaseLookRayWrap(owner)
    lookRayHolders[owner] = nil
    if next(lookRayHolders) ~= nil or lookRayWrap == nil then return end
    local wrap = lookRayWrap
    lookRayWrap = nil
    if wrap.class.getLookRay == wrap.wrapper then wrap.class.getLookRay = wrap.original end
end

-- The game's own Player:getLookRay while the wrap is on the class (the head's ray), or nil.
function VRCompanion:getLookRayWrapOriginal()
    return lookRayWrap ~= nil and lookRayWrap.original or nil
end

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

    -- Proactively trigger native detour in case loadMap executes before any camera update
    if VRBridge == nil and type(isHeadTrackingAvailable) == "function" then
        pcall(isHeadTrackingAvailable)
    end

    -- 0. Cleanly initialize 2D Floating Window Mode state to 3D VR mode (is2DViewActive = false)
    if type(self.set2DViewActive) == "function" then
        self:set2DViewActive(false)
    else
        self.is2DViewActive = false
    end
    if type(self.resetToggleViewDebounce) == "function" then
        self:resetToggleViewDebounce()
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

            -- Query and synchronize native rendering mode state
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

    -- 2. Invoke engine undocumented stereo rendering toggles
    if type(setStereoRendering) == "function" then
        setStereoRendering(true)
        self.stereoToggled = true
        print("[FS25_VR] Invoked engine stereo rendering toggle: setStereoRendering(true)")
    else
        print("[FS25_VR] Engine stereo rendering toggle 'setStereoRendering' not found in Luau environment.")
    end

    -- 3. Report the stereo requirement (fixed since the telemetry probe was retired, ADR-0017)
    if VRBridge ~= nil then
        print(string.format("[FS25_VR] Stereo presentation: AFR fallback required: %s.",
            tostring(self:requiresAfrFallback())))
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
    self:hookVehicleSpeed()
    if type(self.hookHud) == "function" then
        self:hookHud()
    end
    self:hookMissionUpdate()

    -- 4a. Initialize Dual Eye Camera Nodes and Cyclopean Audio Listener
    if type(self.initDualEyeCameraNodes) == "function" then
        self:initDualEyeCameraNodes()
    end
    if type(self.initCyclopeanAudioListener) == "function" then
        self:initCyclopeanAudioListener()
    end

    -- 5. Register in-engine actions (Recenter and HUD Toggle)
    if type(self.registerActionEvents) == "function" then
        self:registerActionEvents(true)
    end

    -- 6. Synchronize initial GUI menu visibility state
    self.isMenuVisible = false
    self.menuTakesInput = false
    self.stereoDialogShowing = false
    self.guiVisibilityInitialized = false
    self:pollGuiVisibility()

    -- 7. Apply the HUD Safe Area and place the HUD Groups
    if type(self.loadConfiguration) == "function" then
        self:loadConfiguration()
    end
    -- 7-. Send the saved Desktop Mirror setting to VRBridge: the handshake above is done, so the Injector sees it at once
    if type(self.applyDesktopMirror) == "function" then
        self:applyDesktopMirror()
    end
    if type(self.applyHudSafeArea) == "function" then
        self:applyHudSafeArea()
    end
    -- 7a. Send the loaded DLSS near plane for the starting camera
    self:applyDlssNearClip(true)

    -- 8. Start in the HUD state the player last chose; a first install starts Hidden for full VR immersion
    if type(self.setHudState) == "function" then
        self:setHudState(self.persistedHudState or self.DEFAULT_HUD_STATE)
    end

    -- 9. Register developer and tuning console commands
    if addConsoleCommand ~= nil then
        addConsoleCommand("vrHudSafeArea", "Set the VR HUD Safe Area, the margin that keeps warnings, notifications and other mods' HUDs inside the lens: vrHudSafeArea <horizontal> <vertical>", "consoleCommandSetHudSafeArea", self)
        addConsoleCommand("vrHudGroup", "Set a VR HUD Group Margin and scale: vrHudGroup <map|speedometer|info|controls> <horizontal> <vertical> [scale]", "consoleCommandSetHudGroup", self)
        addConsoleCommand("vrHudState", "Set VR HUD state: vrHudState <0=Hidden | 1=Streamlined | 2=Full>", "consoleCommandSetHudState", self)
        addConsoleCommand("vrHudLoadNotification", "Toggle HUD alert on map load: vrHudLoadNotification <true|false>", "consoleCommandSetHudLoadNotification", self)
        addConsoleCommand("vrHudLoadAlert", "Toggle HUD alert on map load: vrHudLoadAlert <true|false>", "consoleCommandSetHudLoadNotification", self)
        addConsoleCommand("vrRecenterMode", "Set VR Recenter Action mode: vrRecenterMode [Both | VehicleOnly | toggle]", "consoleCommandSetRecenterMode", self)
        addConsoleCommand("vrRecenterTracking", "Set VR Recenter Action mode: vrRecenterTracking [Both | VehicleOnly | toggle]", "consoleCommandSetRecenterMode", self)
        addConsoleCommand("vrSnapTurnAngle", "Set or query VR snap turn angle in degrees: vrSnapTurnAngle [degrees]", "consoleCommandSetSnapTurnAngle", self)
        addConsoleCommand("vr2DView", "Set or query 2D Floating Window Mode: vr2DView [true|false|toggle]", "consoleCommandSet2DView", self)
        addConsoleCommand("vrToggleView", "Toggle 2D Floating Window Mode: vrToggleView", "consoleCommandToggleView", self)
        addConsoleCommand("vrDlssMvScale", "Set DLSS motion vector scale multiplier: vrDlssMvScale <scaleX> [scaleY]", "consoleCommandSetDlssMvScale", self)
        addConsoleCommand("vrDlssSharpness", "Set DLSS sharpness override: vrDlssSharpness <sharpness 0.0-1.0>", "consoleCommandSetDlssSharpness", self)
        addConsoleCommand("vrDlssPreset", "Set DLSS preset hint: vrDlssPreset <0=Default|3=C|4=D|10=J|11=K|12=L>", "consoleCommandSetDlssPreset", self)
        addConsoleCommand("vrDlssSdpMvScale", "Set DLSS motion vector scale under Sequential Dual-Pass: vrDlssSdpMvScale <scaleX> [scaleY]", "consoleCommandSetDlssSdpMvScale", self)
        addConsoleCommand("vrDlssMvFix", "Switch the DLSS motion vector pass for A/B comparison: vrDlssMvFix <on|off>", "consoleCommandDlssMvFix", self)
        addConsoleCommand("vrDlssDepthModel", "Set the depth model of the DLSS motion vector correction for A/B comparison: vrDlssDepthModel <infinite|finite>", "consoleCommandDlssDepthModel", self)
        addConsoleCommand("vrDlssNearClip", "Set the near plane of the DLSS motion vector correction for every camera in metres (0 or auto = estimate it automatically): vrDlssNearClip <meters|auto>", "consoleCommandDlssNearClip", self)
        addConsoleCommand("vrDlssNearClipCab", "Set the DLSS near plane for a vehicle's inside camera: vrDlssNearClipCab <meters|auto>", "consoleCommandDlssNearClipCab", self)
        addConsoleCommand("vrDlssNearClipOutside", "Set the DLSS near plane for a vehicle's outside cameras: vrDlssNearClipOutside <meters|auto>", "consoleCommandDlssNearClipOutside", self)
        addConsoleCommand("vrDlssNearClipOnFoot", "Set the DLSS near plane on foot: vrDlssNearClipOnFoot <meters|auto>", "consoleCommandDlssNearClipOnFoot", self)
        addConsoleCommand("vrDlssDiag","Trace DLSS evaluations to fs25_vr.log: vrDlssDiag [evaluations]", "consoleCommandDlssDiag", self)
        addConsoleCommand("vrRenderingMode", "Set Stereo Presentation Mode: vrRenderingMode <afr|dualpass|toggle>", "consoleCommandSetRenderingMode", self)
        addConsoleCommand("vrObliqueProjection", "Toggle Oblique Frustum projection: vrObliqueProjection <true|false|toggle>", "consoleCommandSetObliqueProjection", self)
        addConsoleCommand("vrHudGroupShow","Show or hide a HUD Group in the Streamlined or Full HUD: vrHudGroupShow <map|speedometer|info|controls> <streamlined|full> <on|off>", "consoleCommandHudGroupShow", self)
    end
    if type(self.registerMotionControllerConsoleCommands) == "function" then self:registerMotionControllerConsoleCommands() end
    if type(self.registerDialogDiagnosticsConsoleCommands) == "function" then self:registerDialogDiagnosticsConsoleCommands() end
    if type(self.registerFishCatchConsoleCommands) == "function" then self:registerFishCatchConsoleCommands() end
    if type(self.registerSeatOffsetConsoleCommands) == "function" then self:registerSeatOffsetConsoleCommands() end
    if type(self.registerStereoDialogDepthConsoleCommands) == "function" then self:registerStereoDialogDepthConsoleCommands() end
    if type(self.registerDualPassPhysicsConsoleCommands) == "function" then self:registerDualPassPhysicsConsoleCommands() end
    if type(self.registerDesktopMirrorConsoleCommands) == "function" then self:registerDesktopMirrorConsoleCommands() end
    if type(self.registerPerfTraceConsoleCommands) == "function" then self:registerPerfTraceConsoleCommands() end

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
        local ok, visible = pcall(gui.getIsGuiVisible, gui)
        if ok then
            isGuiVisible = (visible == true)
        end
    end

    -- A visible GUI takes the input. It also shows the menu quad, unless it is a Stereo Dialog, which stays in the
    -- stereoscopic view. In 2D Floating Window Mode the presentation is already the quad, so a Stereo Dialog is any other menu.
    local wasStereoDialogShowing = self.stereoDialogShowing == true
    self.stereoDialogShowing = isGuiVisible and not self.is2DViewActive and self:findStereoDialog(gui) ~= nil
    local menuQuadShown = isGuiVisible and not self.stereoDialogShowing
    self.menuTakesInput = isGuiVisible

    if self.isMenuVisible ~= menuQuadShown or not self.guiVisibilityInitialized then
        local wasMenuQuadShown = self.isMenuVisible
        self.isMenuVisible = menuQuadShown
        self.guiVisibilityInitialized = true
        if wasMenuQuadShown and not menuQuadShown then
            if type(self.restoreMenuBackgroundCamera) == "function" then
                self:restoreMenuBackgroundCamera()
            end
            if self.is2DViewActive then
                if type(self.restoreHudSafeArea) == "function" then
                    self:restoreHudSafeArea(true)
                end
            else
                if type(self.applyHudState) == "function" then
                    self:applyHudState()
                end
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
    -- A Stereo Dialog does not switch the presentation, so it is told apart from the menu quad in the log.
    if self.stereoDialogShowing and not wasStereoDialogShowing then
        print("[FS25_VR] In-game GUI Stereo Dialog opened; kept the stereoscopic view, the menu takes the input.")
    elseif wasStereoDialogShowing and not self.stereoDialogShowing then
        print("[FS25_VR] In-game GUI Stereo Dialog closed.")
    end
    return isGuiVisible
end

-- The Stereo Dialogs (ADR-0032): the game dialogs that stay in the stereoscopic view instead of getting the menu quad, by the
-- name the game gives the dialog in g_gui.currentDialogName. A dialog joins this list only after it is tested in the headset;
-- one that is not on it gets the quad. Measured in the game (ticket #143): the fish catch dialog (Highlands Fishing DLC) is
-- "FishDialog", a yes/no dialog whose buttons are answered by the gamepad's MENU_ACCEPT (SELL) and MENU_BACK (RELEASE).
VRCompanion.STEREO_DIALOGS = {
    FishDialog = true,
}

-- The Stereo Dialog that is the only thing the game shows, as the dialog's root element (g_gui.dialogs[1], whose `target` is
-- its controller object and whose buttons and position later tickets work on), or nil. A dialog opened over a screen (the pause
-- menu sets g_gui.currentGuiName; a dialog does not) or over another dialog is not alone, and anything that cannot be read as
-- a lone listed dialog counts as a menu, so that the player is never left in stereo with a GUI they cannot use.
-- The GUI fields read in one protected call. Defined once, so that polling a menu every update makes no closure.
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

-- A visible game GUI means two different things, and every reader asks for the one it needs:
--   isMenuQuadShown()  - the presentation: the menu quad is what the player sees (VRBridge is told so, the
--                        stereo pipeline and HUD convergence stand down, the menu background camera runs, the
--                        head is not placed in the world, the Virtual Hands are off)
--   doesMenuTakeInput() - the input: a menu has the controls (locomotion, Snap Turn and Wheel Grab are refused,
--                        the menu buttons and the Virtual Gamepad go to the menu)
-- pollGuiVisibility sets both together. The quad is the field isMenuVisible (the name VRBridge uses for it); a
-- shown quad always takes the input, so doesMenuTakeInput also holds while it is shown.
function VRCompanion:isMenuQuadShown()
    return self.isMenuVisible == true
end

function VRCompanion:doesMenuTakeInput()
    return self.menuTakesInput == true or self.isMenuVisible == true
end

-- A Stereo Dialog is showing in the stereoscopic view: a menu takes the input but the quad is not shown. The dialog is answered
-- with the gamepad's own menu buttons through the Virtual Gamepad, so the Companion Mod's quad menu (the Pointer Ray, the grip
-- and lower face button as back, the thumbstick scroll) and the Virtual Gamepad's taking of every input are not engaged.
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

-- The camera mode the DLSS near plane is configured per: a vehicle's inside camera (cab), any other
-- vehicle camera (outside), or the player camera (on foot).
function VRCompanion:getDlssNearClipMode()
    local veh = self:getControlledVehicle()
    if veh == nil then return self.DLSS_NEAR_CLIP_ON_FOOT end
    local cam = self:getVehicleActiveCamera(veh)
    if cam ~= nil and cam.isInside then return self.DLSS_NEAR_CLIP_CAB end
    return self.DLSS_NEAR_CLIP_OUTSIDE
end

-- The near plane in metres (0 = auto) configured for the camera the player is looking through.
function VRCompanion:getDlssNearClipForCurrentCamera()
    local cab, outside, onFoot = self.dlssNearClipCab, self.dlssNearClipOutside, self.dlssNearClipOnFoot
    -- One value for every mode needs no camera lookup
    if cab ~= nil and cab == outside and outside == onFoot then return cab end
    local mode = self:getDlssNearClipMode()
    return self[self.DLSS_NEAR_CLIP_FIELDS[mode]] or self.DEFAULT_DLSS_NEAR_CLIPS[mode]
end

-- Sends the current camera's near plane to the Injector when it differs from the one last sent, so
-- entering or leaving a vehicle or switching its camera takes effect on the next update. force resends
-- it anyway. Returns the bridge's result when it sends, otherwise false.
function VRCompanion:applyDlssNearClip(force)
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil or type(bridge.setDlssNearClip) ~= "function" then return false end
    local meters = self:getDlssNearClipForCurrentCamera()
    if not force and self.appliedDlssNearClip == meters then return false end
    -- Recorded even when the bridge has no NGX interceptor, so that is not retried every frame
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

-- Wraps target.updateVehicleSpeed with the dt=0 guard. Vehicle instances keep their
-- original in a weak-keyed table so a vehicle that is gone can be collected; classes are
-- kept strongly (they live as long as the game).
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

-- Installs the dt=0 guards on the named classes, the mission's vehicles and Ferry.
-- scanGlobals additionally walks every global table for classes with updateVehicleSpeed,
-- which only the map load install does: it is too slow to repeat during play.
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

    -- Scan active vehicles in g_currentMission
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
    -- Recorded here so the install from loadMap also restarts the slow retry cadence.
    self.lastVehicleSpeedHookTick = self.frameTicks or 0
end

-- Map load install: also finds custom vehicle classes among the globals.
function VRCompanion:hookVehicleSpeed()
    self:installVehicleSpeedGuards(true)
end

-- Retry cadence and vehicle transitions: named classes and the mission's vehicles only.
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

-- Appends the traceback from where an error was raised, so rethrowing it from the
-- outermost frame-check scope does not hide its origin.
local function withRaiseTraceback(err)
    if traceback ~= nil and type(err) == "string" then
        return traceback(err, 2)
    end
    return err
end

-- Closes a nested frame-check scope after a normal return. An error skips this; the
-- outermost scope then closes every scope opened inside it.
local function endNestedFrameChecks(self, ...)
    self:endFrameChecks()
    return ...
end

-- Closes the outermost frame-check scope however the call ended, then passes on its
-- results or rethrows its error, so an error never leaves a scope open.
local function endOutermostFrameChecks(self, ok, ...)
    self:resetFrameChecks()
    if not ok then
        self.inMissionUpdate = false
        error((...), 0)
    end
    return ...
end

-- Runs fn(self, ...) inside a frame-check scope. Only the outermost scope runs fn
-- protected, so the game's own mission update is not wrapped twice when the game
-- runs VRCompanion:update inside it.
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

-- Clears the running mark after the game's update returned, and passes on its results.
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
        -- The game's update runs marked as running, so a second wrapper further down the
        -- class chain passes straight through and the mod's update logic runs once. An error
        -- skips the clear here; the outermost frame-check scope clears it.
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
    -- Sub-pass 0 advances the simulation and sub-pass 1 replays it frozen; which eye each
    -- sub-pass renders alternates between pairs (see getDualPassEye).
    local eye = self:getDualPassEye()

    -- One eye projection fetch and one rendering camera bind per sub-pass: camera
    -- updates inside the sub-pass share the cached projection and leave the bind to
    -- the single bindRenderingCamera call at the end.
    self:beginEyeProjectionScope()
    self.deferRenderingCameraBind = true

    local camLeft, camRight = self:getStereoCameraNodes()
    local targetCam = (eye == 1) and camRight or camLeft

    if subPass == 1 then
        -- Sub-Pass 1: Freeze simulation at timestamp T.
        self.accumulatedPass1Dt = (self.accumulatedPass1Dt or 0.0) + rawDt
        if self.accumulatedPass1Dt > 200.0 then
            self.accumulatedPass1Dt = 200.0
        end

        if targetCam ~= nil then
            self:runSubPassStep(subPass, self.applyCameraProjection, targetCam, eye)
        end

        -- Synchronize the eye's camera transforms without running engine simulation
        self:finishSubPass(subPass, eye, targetCam, rawDt)
        if type(self.updateHudConvergence) == "function" then
            self:updateHudConvergence(0.0)
        end
        -- The engine's physics runs natively whatever dt the update gets (VRCompanionDualPassPhysics.lua)
        self:applyDualPassPhysicsPause(subPass, rawDt)

        return nil
    end

    -- Sub-Pass 0 (Left Eye): Advance simulation with accumulated delta time.
    local effectiveDt = rawDt + (self.accumulatedPass1Dt or 0.0)
    self.accumulatedPass1Dt = 0.0
    if effectiveDt > 200.0 then
        effectiveDt = 200.0
    end

    -- Update camera projection for active eye
    if targetCam ~= nil then
        self:runSubPassStep(subPass, self.applyCameraProjection, targetCam, eye)
    end

    -- Run mission update with accumulated real dt under reentrancy guard and pcall protection
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
    if type(self.updateHudConvergence) == "function" then
        self:updateHudConvergence(effectiveDt)
    end
    self:applyDualPassPhysicsPause(subPass, rawDt)

    return res
end

-- Runs one camera step of a sub-pass; an error is logged instead of escaping, so the
-- sub-pass still ends with its single camera bind and closes its projection scope.
function VRCompanion:runSubPassStep(subPass, fn, ...)
    local ok, err = pcall(fn, self, ...)
    if not ok and self.lastMissionUpdateError ~= tostring(err) then
        print(string.format("[FS25_VR] [Warning] Camera update error on Sub-Pass %d: %s", subPass, tostring(err)))
        self.lastMissionUpdateError = tostring(err)
    end
end

function VRCompanion:finishSubPass(subPass, eyeIndex, targetCam, frameDt)
    -- The eye sync's camera update records whether the game is showing the camera it tracks
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

-- Tells the Injector which camera this sub-pass rendered with: the bound eye camera's world
-- transform and projection, keyed by the eye (the sub-pass index alone does not name it under Eye
-- Order Alternation). The Injector stamps it with its rendered-frame counter. Any missing engine
-- query skips the report, which the Injector then treats as a frame without a camera record.
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

-- Alternate Frame Rendering renders one eye per frame, bound by the camera updates rather than a
-- sub-pass. Reports that eye's camera like a sub-pass does, so the Injector's motion vector correction
-- can re-aim the engine's vectors at the same eye's camera two frames back. Sequential Dual-Pass
-- reports from its mission update instead, so this stays silent there. An engine query failure costs a
-- missing record, never the camera update.
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
    -- `vrPerfTrace`: the Injector ends a trace by itself
    if self.perfTracing then self:refreshPerfTracing() end

    -- Synchronize 2D view active state with native VRBridge if changed externally
    if VRBridge ~= nil and type(VRBridge.getIs2DViewActive) == "function" then
        local bridge2D = VRBridge.getIs2DViewActive()
        if bridge2D ~= nil and bridge2D ~= self.is2DViewActive then
            self:set2DViewActive(bridge2D)
        end
    end

    -- Answer the fish catch dialog if `vrFishCatch` says so: before the poll, so that the dialog is gone when the poll looks
    if type(self.updateFishCatch) == "function" then self:updateFishCatch() end

    -- Poll in-game GUI visibility and synchronize with native VRBridge
    self:pollGuiVisibility()

    if self:isMenuQuadShown() and type(self.updateMenuBackgroundCamera) == "function" then
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
    if not self.hudHooked and type(self.hookHud) == "function" then
        self:hookHud()
    end
    if not self.missionUpdateHooked then
        self:hookMissionUpdate()
    end
    if self.guiDrawHook == nil and type(self.hookGuiDraw) == "function" then
        self:hookGuiDraw()
    end
    -- Install dt=0 guards once, then only retry (to catch a late Ferry class or newly
    -- spawned vehicles) on a slow cadence or on a vehicle transition, never every frame.
    local speedHookRetryDue = self.vehicleSpeedHooked
        and (self.frameTicks - (self.lastVehicleSpeedHookTick or 0)) >= self.VEHICLE_SPEED_HOOK_RETRY_TICKS
    if not self.vehicleSpeedHooked then
        self:hookVehicleSpeed()
    elseif speedHookRetryDue then
        self:refreshVehicleSpeedGuards()
    end

    -- Update physical IPD-scaled HUD convergence parity across alternating AFR frames
    if type(self.updateHudConvergence) == "function" then
        self:updateHudConvergence(dt)
    end

    -- Keep in-engine action events active across context transitions
    if (self.frameTicks % 30 == 0) or (self.hudToggleActionEventId == nil) or (self.recenterActionEventId == nil) or (self.snapLeftActionEventId == nil) or (self.snapRightActionEventId == nil) or (self.toggleViewActionEventId == nil) or (self.seatSetActionEventId == nil) then
        if type(self.registerActionEvents) == "function" then
            self:registerActionEvents(false)
        end
    end

    -- Check for vehicle transitions (entering/exiting/switching vehicle)
    local curVehicle = self:getControlledVehicle()
    if self.lastControlledVehicle ~= curVehicle then
        local oldVeh = self.lastControlledVehicle
        self.lastControlledVehicle = curVehicle
        -- A new vehicle may have just been spawned/entered: re-run the guard install.
        self:refreshVehicleSpeedGuards()
        if type(self.onVehicleTransition) == "function" then
            self:onVehicleTransition(oldVeh, curVehicle)
        end
    end

    -- Motion Controllers: poll both hands once per update
    if type(self.updateMotionControllers) == "function" then self:updateMotionControllers(dt) end

    -- Follow the camera mode with the DLSS near plane (cab, outside camera, on foot)
    self:applyDlssNearClip(false)
end

function VRCompanion:deleteMap()
    print("[FS25_VR] Unloading Companion Mod from map.")

    -- Hand rendering back to the game's active camera before the Dual Eye Camera Nodes go away
    self.deferRenderingCameraBind = false
    self:restoreGameActiveCamera()

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
    if type(self.unhookHud) == "function" then
        self:unhookHud()
    end
    if type(self.unhookGuiDraw) == "function" then
        self:unhookGuiDraw()
    end

    -- Unhook mission update loop cleanly
    self:unhookMissionUpdate()

    -- Unhook vehicle speed guards cleanly
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
    self.vehicleRotationStates = {}
    self.isMenuVisible = false
    self.menuTakesInput = false
    self.stereoDialogShowing = false
    self.guiVisibilityInitialized = false

    local deleteFn = delete or (_G and _G.delete)
    local entityExistsFn = entityExists or (_G and _G.entityExists)
    -- Nodes linked under a vehicle are already gone: the game deletes its vehicles first, and
    -- deleting an unknown entity logs a script error even inside pcall
    local function deleteIfExists(node)
        if node == nil or node == 0 then return end
        if type(entityExistsFn) == "function" and not entityExistsFn(node) then return end
        pcall(deleteFn, node)
    end
    if type(deleteFn) == "function" then
        deleteIfExists(self.cameraNodeLeft)
        -- Both eyes share one camera node unless something set them apart
        if self.cameraNodeRight ~= self.cameraNodeLeft then
            deleteIfExists(self.cameraNodeRight)
        end
        deleteIfExists(self.cyclopeanAudioListenerNode)
        deleteIfExists(self.outsideCameraBaseNode)
    end
    self.outsideCameraBaseNode = nil
    self.cameraNodeLeft = nil
    self.cameraNodeRight = nil
    self.cyclopeanAudioListenerNode = nil
    if type(self.restoreMenuBackgroundCamera) == "function" then
        self:restoreMenuBackgroundCamera()
    end

    if VRBridge ~= nil and type(VRBridge.setMenuVisible) == "function" then
        VRBridge.setMenuVisible(false)
    end

    if VRBridge ~= nil and type(VRBridge.onMapUnload) == "function" then
        VRBridge.onMapUnload()
    end

    -- Restore HUD state and visibility to standard 2D Full
    if type(self.setHudState) == "function" then
        self:setHudState(self.HUD_STATE_FULL)
    end

    -- Restore the global HUD anchors to the full screen
    if type(self.restoreHudSafeArea) == "function" then
        self:restoreHudSafeArea()
    end

    self.currentConvergenceOffset = 0.0
    self.currentConvergenceDisparity = 0.0
    if self.DEFAULT_CONVERGENCE_DEPTH ~= nil then
        self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
    end
    self.activeFillLevelsDisplay = nil
    self.passThroughMode = nil
    self.headTrackingEnabled = true
    for _, row in ipairs(self.ACTION_EVENTS) do
        self[row.idField] = nil
        self[row.loggedField] = false
    end
    self.actionEventsContextName = nil
    if type(self.resetToggleViewDebounce) == "function" then
        self:resetToggleViewDebounce()
    end

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

    if type(self.set2DViewActive) == "function" then
        self:set2DViewActive(false)
    else
        self.is2DViewActive = false
    end

    -- Remove console commands
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrHudSafeArea")
        removeConsoleCommand("vrHudGroup")
        removeConsoleCommand("vrHudState")
        removeConsoleCommand("vrRecenterMode")
        removeConsoleCommand("vrRecenterTracking")
        removeConsoleCommand("vrSnapTurnAngle")
        removeConsoleCommand("vr2DView")
        removeConsoleCommand("vrToggleView")
        removeConsoleCommand("vrRenderingMode")
        removeConsoleCommand("vrObliqueProjection")
        removeConsoleCommand("vrHudGroupShow")
    end

    if type(self.resetDialogDiagnostics) == "function" then self:resetDialogDiagnostics() end
    if type(self.resetFishCatch) == "function" then self:resetFishCatch() end
    if type(self.resetDesktopMirror) == "function" then self:resetDesktopMirror() end
    if type(self.unregisterPerfTraceConsoleCommands) == "function" then self:unregisterPerfTraceConsoleCommands() end
    if type(self.unregisterSeatOffsetConsoleCommands) == "function" then self:unregisterSeatOffsetConsoleCommands() end
    if type(self.unregisterStereoDialogDepthConsoleCommands) == "function" then self:unregisterStereoDialogDepthConsoleCommands() end
    if type(self.unregisterDualPassPhysicsConsoleCommands) == "function" then self:unregisterDualPassPhysicsConsoleCommands() end
    if type(self.resetMotionControllers) == "function" then self:resetMotionControllers() end
end
