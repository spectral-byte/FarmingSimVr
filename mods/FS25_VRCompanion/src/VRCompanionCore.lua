-- ====================================================================
-- FS25 VR Companion Mod: Core Subsystem
-- Manages map lifecycle (loadMap, deleteMap, update), GUI visibility polling,
-- camera/vehicle discovery, speed limit dt=0 guards, and mission loop hooks.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Core = VRCompanion.Core or {}

VRCompanion.name = VRCompanion.name or g_currentModName or "FS25_VRCompanion"
VRCompanion.version = VRCompanion.version or "1.0.0.0"

VRCompanion.handshakeComplete = (VRCompanion.handshakeComplete ~= nil) and VRCompanion.handshakeComplete or false
VRCompanion.frameTicks = VRCompanion.frameTicks or 0
VRCompanion.lastControlledVehicle = VRCompanion.lastControlledVehicle or nil
VRCompanion.isMenuVisible = (VRCompanion.isMenuVisible ~= nil) and VRCompanion.isMenuVisible or false
VRCompanion.guiVisibilityInitialized = (VRCompanion.guiVisibilityInitialized ~= nil) and VRCompanion.guiVisibilityInitialized or false
VRCompanion.vehicleCameraHooked = (VRCompanion.vehicleCameraHooked ~= nil) and VRCompanion.vehicleCameraHooked or false
VRCompanion.playerCameraHooked = (VRCompanion.playerCameraHooked ~= nil) and VRCompanion.playerCameraHooked or false
VRCompanion.enterableHooked = (VRCompanion.enterableHooked ~= nil) and VRCompanion.enterableHooked or false
VRCompanion.missionUpdateHooked = (VRCompanion.missionUpdateHooked ~= nil) and VRCompanion.missionUpdateHooked or false
VRCompanion.vehicleSpeedHooked = (VRCompanion.vehicleSpeedHooked ~= nil) and VRCompanion.vehicleSpeedHooked or false
VRCompanion.stereoToggled = (VRCompanion.stereoToggled ~= nil) and VRCompanion.stereoToggled or false
VRCompanion.inMissionUpdate = (VRCompanion.inMissionUpdate ~= nil) and VRCompanion.inMissionUpdate or false

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

            -- Query and synchronize native rendering mode state (Issue #54)
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

    -- 4a. Initialize Dual Eye Camera Nodes and Cyclopean Audio Listener (Issue #49)
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
    self.guiVisibilityInitialized = false
    self:pollGuiVisibility()

    -- 7. Apply VR HUD Inset anchoring and UI scale foundation
    if type(self.loadConfiguration) == "function" then
        self:loadConfiguration()
    end
    if type(self.applyHudInsets) == "function" then
        self:applyHudInsets()
    end

    -- 8. Initialize HUD state to Hidden for full VR immersion (Issue #25)
    if type(self.setHudState) == "function" then
        self:setHudState(self.HUD_STATE_HIDDEN)
    end

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
        addConsoleCommand("vrSnapTurnAngle", "Set or query VR snap turn angle in degrees: vrSnapTurnAngle [degrees]", "consoleCommandSetSnapTurnAngle", self)
        addConsoleCommand("vr2DView", "Set or query 2D Floating Window Mode: vr2DView [true|false|toggle]", "consoleCommandSet2DView", self)
        addConsoleCommand("vrToggleView", "Toggle 2D Floating Window Mode: vrToggleView", "consoleCommandToggleView", self)
        addConsoleCommand("vrDlssMvScale", "Set DLSS motion vector scale multiplier: vrDlssMvScale <scaleX> [scaleY]", "consoleCommandSetDlssMvScale", self)
        addConsoleCommand("vrDlssSharpness", "Set DLSS sharpness override: vrDlssSharpness <sharpness 0.0-1.0>", "consoleCommandSetDlssSharpness", self)
        addConsoleCommand("vrDlssPreset", "Set DLSS preset hint: vrDlssPreset <0=Default|3=C|4=D|10=J|11=K|12=L>", "consoleCommandSetDlssPreset", self)
        addConsoleCommand("vrRenderingMode", "Set Stereo Presentation Mode: vrRenderingMode <afr|dualpass|toggle>", "consoleCommandSetRenderingMode", self)
        addConsoleCommand("vrObliqueProjection", "Toggle Oblique Frustum projection: vrObliqueProjection <true|false|toggle>", "consoleCommandSetObliqueProjection", self)
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
        local ok, visible = pcall(gui.getIsGuiVisible, gui)
        if ok then
            isGuiVisible = (visible == true)
        end
    end

    if self.isMenuVisible ~= isGuiVisible or not self.guiVisibilityInitialized then
        local wasMenuVisible = self.isMenuVisible
        self.isMenuVisible = isGuiVisible
        self.guiVisibilityInitialized = true
        if wasMenuVisible and not isGuiVisible then
            if type(self.restoreMenuBackgroundCamera) == "function" then
                self:restoreMenuBackgroundCamera()
            end
            if self.is2DViewActive then
                if type(self.restoreHudInsets) == "function" then
                    self:restoreHudInsets(true)
                end
            else
                if type(self.applyHudState) == "function" then
                    self:applyHudState()
                end
            end
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
            if self.is2DViewActive then
                print("[FS25_VR] In-game GUI menu closed; remaining in 2D Floating Window Mode.")
            else
                print("[FS25_VR] In-game GUI menu closed; restored stereoscopic 3D projection rendering.")
            end
        end
    end
    return isGuiVisible
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

function VRCompanion:hookVehicleSpeed()
    if self.origVehicleSpeedFuncs == nil then
        self.origVehicleSpeedFuncs = {}
    end

    local function hookTableSpeed(target, label)
        if target == nil or type(target.updateVehicleSpeed) ~= "function" then
            return
        end
        if self.origVehicleSpeedFuncs[target] ~= nil then
            return
        end
        local origFn = target.updateVehicleSpeed
        self.origVehicleSpeedFuncs[target] = origFn
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

    local vehClass = Vehicle or (_G and _G.Vehicle)
    if vehClass ~= nil and type(vehClass.updateVehicleSpeed) == "function" and self.origVehicleUpdateVehicleSpeed == nil then
        self.origVehicleUpdateVehicleSpeed = vehClass.updateVehicleSpeed
    end
    hookTableSpeed(vehClass, "Vehicle")

    local rrClass = RailroadVehicle or (_G and _G.RailroadVehicle)
    hookTableSpeed(rrClass, "RailroadVehicle")

    local locoClass = Locomotive or (_G and _G.Locomotive)
    hookTableSpeed(locoClass, "Locomotive")

    -- Scan _G for any classes/tables with updateVehicleSpeed
    if _G ~= nil then
        for name, val in pairs(_G) do
            if type(name) == "string" and type(val) == "table" and type(val.updateVehicleSpeed) == "function" then
                if self.origVehicleSpeedFuncs[val] == nil then
                    hookTableSpeed(val, name)
                end
            end
        end
    end

    -- Scan active vehicles in g_currentMission
    local mission = g_currentMission or (_G and _G.g_currentMission)
    if mission ~= nil and mission.vehicles ~= nil then
        for _, veh in pairs(mission.vehicles) do
            if type(veh) == "table" and type(veh.updateVehicleSpeed) == "function" then
                if self.origVehicleSpeedFuncs[veh] == nil then
                    hookTableSpeed(veh, nil)
                end
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
end

function VRCompanion:unhookVehicleSpeed()
    if self.origVehicleSpeedFuncs ~= nil then
        for target, origFn in pairs(self.origVehicleSpeedFuncs) do
            if target ~= nil and origFn ~= nil then
                target.updateVehicleSpeed = origFn
            end
        end
        self.origVehicleSpeedFuncs = {}
    end

    local vehClass = Vehicle or (_G and _G.Vehicle)
    if self.origVehicleUpdateVehicleSpeed ~= nil and vehClass ~= nil then
        vehClass.updateVehicleSpeed = self.origVehicleUpdateVehicleSpeed
        self.origVehicleUpdateVehicleSpeed = nil
    end

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

function VRCompanion:onMissionUpdate(missionSelf, dt, origUpdateFn, ...)
    if self.inMissionUpdate then
        return origUpdateFn(missionSelf, dt, ...)
    end

    local isSequentialDualPass = (self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS)
        and not self.is2DViewActive
        and not self.isMenuVisible
        and not self:isPassThroughMode()

    if not isSequentialDualPass then
        self.accumulatedPass1Dt = 0.0
        self.deferRenderingCameraBind = false
        self:endEyeProjectionScope()
        return origUpdateFn(missionSelf, dt, ...)
    end

    local rawDt = dt or 0.0
    if rawDt < 0.0 then
        rawDt = 0.0
    end

    local subPass = self:getSubPassIndex() % 2

    -- One eye projection fetch and one rendering camera bind per sub-pass: camera
    -- updates inside the sub-pass share the cached projection and leave the bind to
    -- the single bindRenderingCamera call at the end.
    self:beginEyeProjectionScope()
    self.deferRenderingCameraBind = true

    if subPass == 1 then
        -- Sub-Pass 1 (Right Eye): Freeze simulation at timestamp T.
        self.accumulatedPass1Dt = (self.accumulatedPass1Dt or 0.0) + rawDt
        if self.accumulatedPass1Dt > 200.0 then
            self.accumulatedPass1Dt = 200.0
        end

        local camLeft, camRight = self:getStereoCameraNodes()
        local targetCam = camRight

        if targetCam ~= nil then
            self:applyCameraProjection(targetCam, 1)
        end

        -- Synchronize right eye camera transforms without running engine simulation
        self:syncActiveEyeCamera(1)
        self.deferRenderingCameraBind = false
        self:bindRenderingCamera(targetCam)
        self:endEyeProjectionScope()
        if type(self.updateHudConvergence) == "function" then
            self:updateHudConvergence(0.0)
        end

        return nil
    end

    -- Sub-Pass 0 (Left Eye): Advance simulation with accumulated delta time.
    local effectiveDt = rawDt + (self.accumulatedPass1Dt or 0.0)
    self.accumulatedPass1Dt = 0.0
    if effectiveDt > 200.0 then
        effectiveDt = 200.0
    end

    -- Sub-Pass 0: cameraNodeLeft is bound as active rendering camera
    local camLeft, camRight = self:getStereoCameraNodes()
    local targetCam = camLeft

    -- Update camera projection for active eye
    if targetCam ~= nil then
        self:applyCameraProjection(targetCam, 0)
    end

    -- Run mission update with accumulated real dt under reentrancy guard and pcall protection
    self.inMissionUpdate = true
    local ok, res = pcall(origUpdateFn, missionSelf, effectiveDt, ...)
    self.inMissionUpdate = false
    if not ok then
        if self.lastMissionUpdateError ~= tostring(res) then
            print(string.format("[FS25_VR] [Warning] Mission update error on Sub-Pass %d (dt=%s): %s",
                subPass, tostring(effectiveDt), tostring(res)))
            self.lastMissionUpdateError = tostring(res)
        end
        res = nil
    end

    self:syncActiveEyeCamera(0)
    self.deferRenderingCameraBind = false
    self:bindRenderingCamera(targetCam)
    self:endEyeProjectionScope()
    if type(self.updateHudConvergence) == "function" then
        self:updateHudConvergence(effectiveDt)
    end

    return res
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

    local mission = g_currentMission or (_G and _G.g_currentMission)
    if self.origMissionInstanceUpdate ~= nil and mission ~= nil then
        mission.update = self.origMissionInstanceUpdate
        self.origMissionInstanceUpdate = nil
    end

    self.accumulatedPass1Dt = 0.0
    self.missionUpdateHooked = false
    print("[FS25_VR] Unhooked mission update loop.")
end

function VRCompanion:update(dt)
    self.frameTicks = (self.frameTicks or 0) + 1

    -- Synchronize 2D view active state with native VRBridge if changed externally
    if VRBridge ~= nil and type(VRBridge.getIs2DViewActive) == "function" then
        local bridge2D = VRBridge.getIs2DViewActive()
        if bridge2D ~= nil and bridge2D ~= self.is2DViewActive then
            self:set2DViewActive(bridge2D)
        end
    end

    -- Poll in-game GUI visibility and synchronize with native VRBridge
    self:pollGuiVisibility()

    if self.isMenuVisible and type(self.updateMenuBackgroundCamera) == "function" then
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
    if not self.vehicleSpeedHooked or self.origFerryUpdatePosition == nil then
        self:hookVehicleSpeed()
    end

    -- Update physical IPD-scaled HUD convergence parity across alternating AFR frames
    if type(self.updateHudConvergence) == "function" then
        self:updateHudConvergence(dt)
    end

    -- Keep in-engine action events active across context transitions
    if (self.frameTicks % 30 == 0) or (self.hudToggleActionEventId == nil) or (self.recenterActionEventId == nil) or (self.snapLeftActionEventId == nil) or (self.snapRightActionEventId == nil) or (self.toggleViewActionEventId == nil) then
        if type(self.registerActionEvents) == "function" then
            self:registerActionEvents(false)
        end
    end

    -- Check for vehicle transitions (entering/exiting/switching vehicle)
    local curVehicle = self:getControlledVehicle()
    if self.lastControlledVehicle ~= curVehicle then
        local oldVeh = self.lastControlledVehicle
        self.lastControlledVehicle = curVehicle
        if type(self.onVehicleTransition) == "function" then
            self:onVehicleTransition(oldVeh, curVehicle)
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
    if type(self.unhookHud) == "function" then
        self:unhookHud()
    end

    -- Unhook mission update loop cleanly
    self:unhookMissionUpdate()

    -- Unhook vehicle speed guards cleanly
    self:unhookVehicleSpeed()

    self.inMissionUpdate = false
    self.handshakeComplete = false
    self.frameTicks = 0
    self.vehicleCameraHooked = false
    self.playerCameraHooked = false
    self.activePlayerCamera = nil
    self.cameraNodeLeft = nil
    self.cameraNodeRight = nil
    self.activeRenderingCamera = nil
    self.subPassIndex = 0
    self.accumulatedPass1Dt = 0.0
    self.enterableHooked = false
    self.lastControlledVehicle = nil
    self.vehicleRotationStates = {}
    self.isMenuVisible = false
    self.guiVisibilityInitialized = false

    local deleteFn = delete or (_G and _G.delete)
    if type(deleteFn) == "function" then
        if self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0 then
            pcall(deleteFn, self.cameraNodeLeft)
        end
        if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
            pcall(deleteFn, self.cameraNodeRight)
        end
        if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
            pcall(deleteFn, self.cyclopeanAudioListenerNode)
        end
    end
    self.cameraNodeLeft = nil
    self.cameraNodeRight = nil
    self.cyclopeanAudioListenerNode = nil
    self.activeRenderingCameraNode = nil
    if type(self.restoreMenuBackgroundCamera) == "function" then
        self:restoreMenuBackgroundCamera()
    end

    if VRBridge ~= nil and type(VRBridge.setMenuVisible) == "function" then
        VRBridge.setMenuVisible(false)
    end

    if VRBridge ~= nil and type(VRBridge.onMapUnload) == "function" then
        VRBridge.onMapUnload()
    end

    -- Restore HUD state and visibility to standard 2D Full (Issue #25)
    if type(self.setHudState) == "function" then
        self:setHudState(self.HUD_STATE_FULL)
    end

    -- Restore HUD insets to 2D defaults
    if type(self.restoreHudInsets) == "function" then
        self:restoreHudInsets()
    end

    self.currentConvergenceOffset = 0.0
    self.currentConvergenceDisparity = 0.0
    if self.DEFAULT_CONVERGENCE_DEPTH ~= nil then
        self.hudConvergenceDepth = self.DEFAULT_CONVERGENCE_DEPTH
    end
    self.activeFillLevelsDisplay = nil
    self.passThroughMode = nil
    self.headTrackingEnabled = true
    self.hudToggleActionEventId = nil
    self.recenterActionEventId = nil
    self.snapLeftActionEventId = nil
    self.snapRightActionEventId = nil
    self.toggleViewActionEventId = nil
    self.hudToggleLoggedOnce = false
    self.recenterLoggedOnce = false
    self.snapLeftLoggedOnce = false
    self.snapRightLoggedOnce = false
    self.toggleViewLoggedOnce = false
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
        removeConsoleCommand("vrHudInsets")
        removeConsoleCommand("vrHudState")
        removeConsoleCommand("vrHudFillUnderVehicle")
        removeConsoleCommand("vrRecenterMode")
        removeConsoleCommand("vrRecenterTracking")
        removeConsoleCommand("vrSnapTurnAngle")
        removeConsoleCommand("vr2DView")
        removeConsoleCommand("vrToggleView")
        removeConsoleCommand("vrRenderingMode")
        removeConsoleCommand("vrObliqueProjection")
    end
end

-- Export to Core subsystem table
VRCompanion.Core.loadMap = function(self, ...) return VRCompanion.loadMap(VRCompanion, ...) end
VRCompanion.Core.update = function(self, ...) return VRCompanion.update(VRCompanion, ...) end
VRCompanion.Core.deleteMap = function(self, ...) return VRCompanion.deleteMap(VRCompanion, ...) end
VRCompanion.Core.pollGuiVisibility = function(self, ...) return VRCompanion.pollGuiVisibility(VRCompanion, ...) end
VRCompanion.Core.getPlayerCamera = function(self, ...) return VRCompanion.getPlayerCamera(VRCompanion, ...) end
VRCompanion.Core.getControlledVehicle = function(self, ...) return VRCompanion.getControlledVehicle(VRCompanion, ...) end
VRCompanion.Core.getActiveVehicleCamera = function(self, ...) return VRCompanion.getActiveVehicleCamera(VRCompanion, ...) end
VRCompanion.Core.getVehicleActiveCamera = function(self, ...) return VRCompanion.getVehicleActiveCamera(VRCompanion, ...) end
VRCompanion.Core.hookVehicleCamera = function(self, ...) return VRCompanion.hookVehicleCamera(VRCompanion, ...) end
VRCompanion.Core.hookEnterable = function(self, ...) return VRCompanion.hookEnterable(VRCompanion, ...) end
VRCompanion.Core.hookVehicleSpeed = function(self, ...) return VRCompanion.hookVehicleSpeed(VRCompanion, ...) end
VRCompanion.Core.unhookVehicleSpeed = function(self, ...) return VRCompanion.unhookVehicleSpeed(VRCompanion, ...) end
VRCompanion.Core.hookPlayerCamera = function(self, ...) return VRCompanion.hookPlayerCamera(VRCompanion, ...) end
VRCompanion.Core.onMissionUpdate = function(self, ...) return VRCompanion.onMissionUpdate(VRCompanion, ...) end
VRCompanion.Core.hookMissionUpdate = function(self, ...) return VRCompanion.hookMissionUpdate(VRCompanion, ...) end
VRCompanion.Core.unhookMissionUpdate = function(self, ...) return VRCompanion.unhookMissionUpdate(VRCompanion, ...) end

return VRCompanion.Core
