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
VRCompanion.is2DViewActive = false
VRCompanion.guiVisibilityInitialized = false

-- Stereo Presentation Mode constants (Issue #54, Issue #56)
VRCompanion.RENDERING_MODE_AFR = "AFR"
VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS = "SequentialDualPass"
VRCompanion.RENDERING_MODE_DUAL_PASS = VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
VRCompanion.renderingMode = VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
VRCompanion.subPassIndex = 0
VRCompanion.cameraNodeLeft = nil
VRCompanion.cameraNodeRight = nil
VRCompanion.activeRenderingCamera = nil
VRCompanion.missionUpdateHooked = false
VRCompanion.origFSBaseMissionUpdate = nil
VRCompanion.origMission00Update = nil
VRCompanion.origMissionInstanceUpdate = nil
VRCompanion.inMissionUpdate = false
VRCompanion.vehicleSpeedHooked = false
VRCompanion.origVehicleUpdateVehicleSpeed = nil
VRCompanion.origVehicleSpeedFuncs = nil
VRCompanion.origFerryUpdatePosition = nil
VRCompanion.accumulatedPass1Dt = 0.0
VRCompanion.lastMissionUpdateError = nil
VRCompanion.hudToggleActionEventId = nil
VRCompanion.recenterActionEventId = nil
VRCompanion.snapLeftActionEventId = nil
VRCompanion.snapRightActionEventId = nil
VRCompanion.toggleViewActionEventId = nil
VRCompanion.hudToggleLoggedOnce = false
VRCompanion.recenterLoggedOnce = false
VRCompanion.snapLeftLoggedOnce = false
VRCompanion.snapRightLoggedOnce = false
VRCompanion.toggleViewLoggedOnce = false

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

function VRCompanion:getRenderingMode()
    if VRBridge ~= nil and type(VRBridge.getRenderingMode) == "function" then
        local mode = VRBridge.getRenderingMode()
        if mode == "SequentialDualPass" or mode == "DualPass" or mode == 1 then
            return self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
        elseif mode == "AFR" or mode == 0 then
            return self.RENDERING_MODE_AFR
        end
    end
    return self.renderingMode or self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
end

function VRCompanion:setRenderingMode(modeStr, saveConfig)
    local targetMode = self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
    if modeStr ~= nil then
        local lower = string.lower(tostring(modeStr))
        if lower == "sequentialdualpass" or lower == "dualpass" or lower == "dual" or lower == "dual_pass" or lower == "sequential" or lower == "sdp" or lower == "1" then
            targetMode = self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
        elseif lower == "toggle" then
            targetMode = (self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) 
                and self.RENDERING_MODE_AFR 
                or self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
        elseif lower == "afr" or lower == "alternate" or lower == "alternateframe" or lower == "0" then
            targetMode = self.RENDERING_MODE_AFR
        end
    end

    local oldMode = self:getRenderingMode()
    self.renderingMode = targetMode

    if VRBridge ~= nil and type(VRBridge.setRenderingMode) == "function" then
        VRBridge.setRenderingMode(targetMode)
    end

    if oldMode ~= targetMode then
        self:signalDiscontinuityFlush()
        self.accumulatedPass1Dt = 0.0
    end

    print(string.format("[FS25_VR] Active VR Rendering Mode: %s", targetMode))

    if saveConfig == true and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end

    return targetMode
end

function VRCompanion:getSubPassIndex()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if type(bridge.getSubPassIndex) == "function" then
            return bridge.getSubPassIndex()
        elseif bridge.subPassIndex ~= nil then
            return bridge.subPassIndex
        end
    end
    return self.subPassIndex or 0
end

function VRCompanion:setSubPassIndex(idx)
    local val = (idx or 0) % 2
    self.subPassIndex = val
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if type(bridge.setSubPassIndex) == "function" then
            bridge.setSubPassIndex(val)
        elseif bridge.subPassIndex ~= nil then
            bridge.subPassIndex = val
        end
    end
    return val
end

function VRCompanion:advanceSubPass()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.advanceSubPass) == "function" then
        local nextPass = bridge.advanceSubPass()
        self.subPassIndex = nextPass
        return nextPass
    end
    local current = self:getSubPassIndex()
    local nextPass = (current == 0) and 1 or 0
    return self:setSubPassIndex(nextPass)
end

function VRCompanion:getStereoCameraNodes(activeCam)
    local cam = activeCam
    if cam == nil then
        local curVeh = self:getControlledVehicle()
        if curVeh ~= nil then
            cam = self:getVehicleActiveCamera(curVeh)
        end
    end
    if cam == nil then
        cam = self.activePlayerCamera
    end

    local camLeft = self.cameraNodeLeft
    local camRight = self.cameraNodeRight

    if cam ~= nil then
        if cam.cameraNodeLeft ~= nil then
            camLeft = cam.cameraNodeLeft
        end
        if cam.cameraNodeRight ~= nil then
            camRight = cam.cameraNodeRight
        end

        -- If not yet created for this camera, initialize them
        if camLeft == nil or camRight == nil then
            local baseNode = (cam.firstPersonCamera ~= nil) and cam.firstPersonCamera or cam.cameraNode
            if camLeft == nil then
                if type(createCamera) == "function" then
                    local fovY = (type(getFovY) == "function" and baseNode and getFovY(baseNode)) or 1.0472
                    camLeft = createCamera("vrStereoCamLeft", fovY, 0.15, 2000.0)
                else
                    camLeft = (baseNode and (baseNode .. "_left")) or (baseNode or 2001)
                end
            end
            if camRight == nil then
                if type(createCamera) == "function" then
                    local fovY = (type(getFovY) == "function" and baseNode and getFovY(baseNode)) or 1.0472
                    camRight = createCamera("vrStereoCamRight", fovY, 0.15, 2000.0)
                else
                    camRight = (baseNode and (baseNode .. "_right")) or ((baseNode or 2001) + 1)
                end
            end
            cam.cameraNodeLeft = camLeft
            cam.cameraNodeRight = camRight
        end
    end

    if camLeft == nil then
        if type(createCamera) == "function" then
            camLeft = createCamera("vrDefaultCamLeft", 1.0472, 0.15, 2000.0)
        else
            camLeft = 2001
        end
    end
    if camRight == nil then
        if type(createCamera) == "function" then
            camRight = createCamera("vrDefaultCamRight", 1.0472, 0.15, 2000.0)
        else
            camRight = 2002
        end
    end

    self.cameraNodeLeft = camLeft
    self.cameraNodeRight = camRight

    return camLeft, camRight
end

function VRCompanion:bindRenderingCamera(targetCam)
    if targetCam == nil then return end
    local bound = false
    local camMgr = g_cameraManager or (_G and _G.g_cameraManager)
    if camMgr ~= nil and type(camMgr.setActiveCamera) == "function" then
        local ok = pcall(camMgr.setActiveCamera, camMgr, targetCam)
        if ok then
            bound = true
        end
    end
    if not bound then
        local setCameraFn = setCamera or (_G and _G.setCamera)
        if type(setCameraFn) == "function" then
            pcall(setCameraFn, targetCam)
        end
    end
    self.activeRenderingCamera = targetCam
    if _G then
        _G.currentCamera = targetCam
        _G.activeCamera = targetCam
    end
end

function VRCompanion:syncActiveEyeCamera(subPass)
    local curVeh = self:getControlledVehicle()
    if curVeh ~= nil then
        local cam = self:getVehicleActiveCamera(curVeh)
        if cam ~= nil and cam.isInside then
            self:onVehicleCameraUpdate(cam, 0.0)
            return
        end
    end

    local playerCam = self:getPlayerCamera()
    if playerCam ~= nil then
        self:onPlayerCameraUpdate(playerCam, 0.0)
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
        return origUpdateFn(missionSelf, dt, ...)
    end

    local rawDt = dt or 0.0
    if rawDt < 0.0 then
        rawDt = 0.0
    end

    local subPass = self:getSubPassIndex() % 2

    if subPass == 1 then
        -- Sub-Pass 1 (Right Eye): Freeze simulation at timestamp T.
        -- Accumulate delta time for Sub-Pass 0 of the next stereo frame to maintain 100% realtime speed.
        self.accumulatedPass1Dt = (self.accumulatedPass1Dt or 0.0) + rawDt
        if self.accumulatedPass1Dt > 200.0 then
            self.accumulatedPass1Dt = 200.0
        end

        local camLeft, camRight = self:getStereoCameraNodes()
        local targetCam = camRight

        self:bindRenderingCamera(targetCam)
        if targetCam ~= nil then
            self:applyCameraProjection(targetCam, 1)
        end

        -- Synchronize right eye camera transforms without running engine simulation
        self:syncActiveEyeCamera(1)
        self:bindRenderingCamera(targetCam)
        self:updateHudConvergence(0.0)

        return nil
    end

    -- Sub-Pass 0 (Left Eye): Advance simulation with accumulated delta time.
    local effectiveDt = rawDt + (self.accumulatedPass1Dt or 0.0)
    self.accumulatedPass1Dt = 0.0
    if effectiveDt > 200.0 then
        effectiveDt = 200.0
    end

    -- Stereoscopic camera selection:
    -- Sub-Pass 0: cameraNodeLeft is bound as active rendering camera
    local camLeft, camRight = self:getStereoCameraNodes()
    local targetCam = camLeft

    self:bindRenderingCamera(targetCam)

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
    self:bindRenderingCamera(targetCam)
    self:updateHudConvergence(effectiveDt)

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

-- Dual Eye Camera Nodes & Cyclopean Audio Listener (Issue #49)
VRCompanion.cameraNodeLeft = nil
VRCompanion.cameraNodeRight = nil
VRCompanion.cyclopeanAudioListenerNode = nil
VRCompanion.activeRenderingCameraNode = nil

function VRCompanion:initDualEyeCameraNodes(referenceCamNode)
    local createCameraFn = createCamera or (_G and _G.createCamera)
    local createTransformGroupFn = createTransformGroup or (_G and _G.createTransformGroup)

    local fovY = 1.0472
    local nearClip = 0.15
    local farClip = 2000.0

    if referenceCamNode ~= nil and referenceCamNode ~= 0 then
        local getFovYFn = getFovY or (_G and _G.getFovY)
        if type(getFovYFn) == "function" then
            local ok, f = pcall(getFovYFn, referenceCamNode)
            if ok and f ~= nil then fovY = f end
        end
        local getNearClipFn = getNearClip or (_G and _G.getNearClip)
        if type(getNearClipFn) == "function" then
            local ok, nc = pcall(getNearClipFn, referenceCamNode)
            if ok and nc ~= nil then nearClip = nc end
        end
        local getFarClipFn = getFarClip or (_G and _G.getFarClip)
        if type(getFarClipFn) == "function" then
            local ok, fc = pcall(getFarClipFn, referenceCamNode)
            if ok and fc ~= nil then farClip = fc end
        end
    end

    if self.cameraNodeLeft == nil or self.cameraNodeLeft == 0 then
        if type(createCameraFn) == "function" then
            self.cameraNodeLeft = createCameraFn("cameraNodeLeft", fovY, nearClip, farClip)
        elseif type(createTransformGroupFn) == "function" then
            self.cameraNodeLeft = createTransformGroupFn("cameraNodeLeft")
        end
    end

    if self.cameraNodeRight == nil or self.cameraNodeRight == 0 then
        if type(createCameraFn) == "function" then
            self.cameraNodeRight = createCameraFn("cameraNodeRight", fovY, nearClip, farClip)
        elseif type(createTransformGroupFn) == "function" then
            self.cameraNodeRight = createTransformGroupFn("cameraNodeRight")
        end
    end

    return self.cameraNodeLeft, self.cameraNodeRight
end

function VRCompanion:ensureDualEyeCameraNodes(referenceCamNode)
    if self.cameraNodeLeft == nil or self.cameraNodeRight == nil or self.cameraNodeLeft == 0 or self.cameraNodeRight == 0 then
        self:initDualEyeCameraNodes(referenceCamNode)
    end
    return self.cameraNodeLeft, self.cameraNodeRight
end

function VRCompanion:initCyclopeanAudioListener(referenceNode)
    local createTransformGroupFn = createTransformGroup or (_G and _G.createTransformGroup)
    local setAudioListenerFn = setAudioListener or (_G and _G.setAudioListener)

    if self.cyclopeanAudioListenerNode == nil or self.cyclopeanAudioListenerNode == 0 then
        if type(createTransformGroupFn) == "function" then
            self.cyclopeanAudioListenerNode = createTransformGroupFn("cyclopeanAudioListenerNode")
        end
    end

    if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
        if type(setAudioListenerFn) == "function" then
            pcall(setAudioListenerFn, self.cyclopeanAudioListenerNode)
        end
    end

    return self.cyclopeanAudioListenerNode
end

function VRCompanion:ensureCyclopeanAudioListener(referenceNode)
    if self.cyclopeanAudioListenerNode == nil or self.cyclopeanAudioListenerNode == 0 then
        self:initCyclopeanAudioListener(referenceNode)
    end
    return self.cyclopeanAudioListenerNode
end

function VRCompanion:signalDiscontinuityFlush()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if type(bridge.signalDiscontinuity) == "function" then
            bridge.signalDiscontinuity()
        elseif type(bridge.resetTemporalHistory) == "function" then
            bridge.resetTemporalHistory()
        end
    end
end

function VRCompanion:getIs2DViewActive()
    return self.is2DViewActive == true
end

function VRCompanion:set2DViewActive(active)
    local wasActive = (self.is2DViewActive == true)
    local newActive = (active == true)
    self.is2DViewActive = newActive

    if VRBridge ~= nil and type(VRBridge.set2DViewActive) == "function" then
        VRBridge.set2DViewActive(newActive)
    end

    if wasActive ~= newActive then
        self:signalDiscontinuityFlush()
        if newActive then
            print("[FS25_VR] Entered 2D Floating Window Mode.")
            self:restoreCameraProjectionsFor2D()
            if self.activePlayerCamera ~= nil then
                self:realignPlayerCameraToRoot(self.activePlayerCamera)
            end
            self:restoreHudInsets(true)
        else
            print("[FS25_VR] Exited 2D Floating Window Mode; returning to 3D VR mode.")
            self:applyHudInsets()
            self:applyHudState()
        end
    end
end

function VRCompanion:toggle2DView()
    self:set2DViewActive(not self.is2DViewActive)
    return self.is2DViewActive
end

-- VR View Mode Toggle Configuration (Issue #41)
VRCompanion.DEFAULT_TOGGLE_VIEW_DEBOUNCE_MS = 250
VRCompanion.toggleViewDebounceMs = VRCompanion.DEFAULT_TOGGLE_VIEW_DEBOUNCE_MS
VRCompanion.lastToggleViewTime = nil
VRCompanion.isToggleViewKeyDown = false

function VRCompanion:isToggleViewDebounced(curTime)
    local t = curTime or self:getCurrentTimeMs()
    if t == nil then
        return false
    end
    if self.lastToggleViewTime ~= nil then
        local cooldown = self.toggleViewDebounceMs or self.DEFAULT_TOGGLE_VIEW_DEBOUNCE_MS or 250
        local elapsed = t - self.lastToggleViewTime
        if elapsed >= 0 and elapsed < cooldown then
            return true
        end
    end
    return false
end

function VRCompanion:resetToggleViewDebounce()
    self.lastToggleViewTime = nil
    self.isToggleViewKeyDown = false
end

function VRCompanion:onToggleVrViewAction(actionName, keyStatus, curTime)
    local t = nil
    if type(curTime) == "number" then
        t = curTime
    elseif type(keyStatus) == "number" and keyStatus > 1 then
        t = keyStatus
    elseif type(actionName) == "number" then
        t = actionName
    end
    t = t or self:getCurrentTimeMs()

    if self:isToggleViewDebounced(t) then
        return false, "debounced"
    end

    self.lastToggleViewTime = t
    return self:toggle2DView()
end

function VRCompanion:restoreCameraProjectionsFor2D()
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
                    pcall(setProjectionOffset, camNode, 0, 0)
                end
            end
        end
    end
    local veh = self:getControlledVehicle()
    if veh ~= nil then
        local cam = self:getVehicleActiveCamera(veh)
        if cam ~= nil and cam.cameraNode ~= nil then
            self:applyCameraProjection(cam.cameraNode, 0)
        end
    end
    if self.activePlayerCamera ~= nil then
        local camNode = self.activePlayerCamera.firstPersonCamera or self.activePlayerCamera.cameraNode
        if camNode ~= nil then
            self:applyCameraProjection(camNode, 0)
        end
    end
end

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

-- VR Snap Turn Locomotion Configuration (Issue #35)
VRCompanion.DEFAULT_SNAP_TURN_ANGLE = math.pi * 0.5 -- 90 degrees (pi/2 radians)
VRCompanion.snapTurnAngle = VRCompanion.DEFAULT_SNAP_TURN_ANGLE
VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS = 250 -- Minimum 250ms debounce cooldown
VRCompanion.snapTurnDebounceMs = VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS
VRCompanion.lastSnapTurnTime = nil
VRCompanion.isSnapLeftKeyDown = false
VRCompanion.isSnapRightKeyDown = false

function VRCompanion:getSnapTurnAngle()
    return self.snapTurnAngle or self.DEFAULT_SNAP_TURN_ANGLE
end

function VRCompanion:setSnapTurnAngle(angle, inDegrees, saveConfig)
    if type(angle) == "number" and angle > 0 then
        if inDegrees == true then
            self.snapTurnAngle = math.rad(angle)
        elseif inDegrees == false then
            self.snapTurnAngle = angle
        else
            if angle > math.pi * 2 then
                self.snapTurnAngle = math.rad(angle)
            else
                self.snapTurnAngle = angle
            end
        end
        if saveConfig == true then
            self:saveConfiguration()
        end
    end
    return self:getSnapTurnAngle()
end

function VRCompanion:getSnapTurnAngleDegrees()
    local rad = self:getSnapTurnAngle()
    local deg = math.deg(rad)
    if math.abs(deg - math.floor(deg + 0.5)) < 0.001 then
        return math.floor(deg + 0.5)
    end
    return math.floor(deg * 10 + 0.5) / 10
end

function VRCompanion:getCurrentTimeMs()
    local t = g_time or (_G and _G.g_time)
    if t ~= nil then return t end
    if type(os) == "table" and type(os.clock) == "function" then
        return os.clock() * 1000
    end
    return nil
end

function VRCompanion:isSnapTurnDebounced(curTime)
    local t = curTime or self:getCurrentTimeMs()
    if t == nil then
        return false
    end
    if self.lastSnapTurnTime ~= nil then
        local cooldown = self.snapTurnDebounceMs or self.DEFAULT_SNAP_TURN_DEBOUNCE_MS or 250
        local elapsed = t - self.lastSnapTurnTime
        if elapsed >= 0 and elapsed < cooldown then
            return true
        end
    end
    return false
end

function VRCompanion:resetSnapTurnDebounce()
    self.lastSnapTurnTime = nil
    self.isSnapLeftKeyDown = false
    self.isSnapRightKeyDown = false
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

    -- Parity / Eye index 0 (Left eye): positive offset (+Delta x)
    -- Parity / Eye index 1 (Right eye): negative offset (-Delta x)
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

    -- Determine total fill level display width
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

    -- Count active fill level units (e.g. multi-tank vehicles, trailers) to scale total height
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

    -- Explicitly position background parts and progress bar so they are placed at (flPosX, fillBottom)
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

    -- If sm was lifted to accommodate fill levels at small insets, ensure sm is at targetSmBottom
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

    -- Stereoscopic eye convergence offset:
    -- In Sequential Dual-Pass: locked statically to sub-pass eye index (0 = Left, 1 = Right)
    -- In Alternate Frame Rendering (AFR): alternate on frame parity (0 = Left, 1 = Right)
    local eyeOrParity = nil
    if self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then
        eyeOrParity = self:getSubPassIndex() % 2
    else
        eyeOrParity = self:getFrameParity()
    end
    local offset = self:getConvergenceOffset(eyeOrParity, self.currentConvergenceDisparity)

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

local function readXmlString(getXmlStringFn, xmlFile, ...)
    local keys = {...}
    for _, k in ipairs(keys) do
        if type(getXmlStringFn) == "function" then
            local val = getXmlStringFn(xmlFile, k)
            if val ~= nil and val ~= "" then
                return tostring(val)
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
    local setXmlStringFn = setXMLString or _G.setXMLString
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
        if type(setXmlStringFn) == "function" then
            local modeStr = (self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS) and "DualPass" or "AFR"
            setXmlStringFn(xmlFile, "vrSettings.renderingMode", modeStr)
        end
        setXmlFloatFn(xmlFile, "vrSettings.hudFillLevelGap", self.hudFillLevelGap or self.DEFAULT_HUD_FILL_LEVEL_GAP)
        setXmlFloatFn(xmlFile, "vrSettings.snapTurnAngle", self:getSnapTurnAngleDegrees())

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

                local loadedSnapAngle = readXmlFloat(getXmlFloatFn, xmlFile,
                    "vrSettings.snapTurnAngle", "vrSettings.snapTurnAngle#value",
                    "modDesc.vrSettings.snapTurnAngle", "modDesc.vrSettings.snapTurnAngle#value")
                if loadedSnapAngle ~= nil and loadedSnapAngle > 0 then
                    self.snapTurnAngle = math.rad(loadedSnapAngle)
                end

                local loadedMode = readXmlString(getXmlStringFn, xmlFile,
                    "vrSettings.renderingMode", "vrSettings.renderingMode#value",
                    "renderingMode", "renderingMode#value",
                    "modDesc.vrSettings.renderingMode", "modDesc.vrSettings.renderingMode#value")
                if loadedMode ~= nil and loadedMode ~= "" then
                    self:setRenderingMode(loadedMode, false)
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
    if self.snapTurnAngle == nil then
        self.snapTurnAngle = self.DEFAULT_SNAP_TURN_ANGLE
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
    local baseTop = self.hudAnchorTopBase or (1.0 - self.DEFAULT_HUD_INSET_Y)

    self.hudAnchorLeft = left
    self.hudAnchorRight = right
    self.hudAnchorBottom = bottom
    self.hudAnchorTop = top
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

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

    -- Proactively trigger native detour in case loadMap executes before any camera update
    if VRBridge == nil and type(isHeadTrackingAvailable) == "function" then
        pcall(isHeadTrackingAvailable)
    end

    -- 0. Cleanly initialize 2D Floating Window Mode state to 3D VR mode (is2DViewActive = false)
    self:set2DViewActive(false)
    self:resetToggleViewDebounce()

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
        self.renderingMode = self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
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
    self:hookVehicleSpeed()
    self:hookHud()
    self:hookMissionUpdate()

    -- 4a. Initialize Dual Eye Camera Nodes and Cyclopean Audio Listener (Issue #49)
    self:initDualEyeCameraNodes()
    self:initCyclopeanAudioListener()

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
        addConsoleCommand("vrSnapTurnAngle", "Set or query VR snap turn angle in degrees: vrSnapTurnAngle [degrees]", "consoleCommandSetSnapTurnAngle", self)
        addConsoleCommand("vr2DView", "Set or query 2D Floating Window Mode: vr2DView [true|false|toggle]", "consoleCommandSet2DView", self)
        addConsoleCommand("vrToggleView", "Toggle 2D Floating Window Mode: vrToggleView", "consoleCommandToggleView", self)
        addConsoleCommand("vrDlssMvScale", "Set DLSS motion vector scale multiplier: vrDlssMvScale <scaleX> [scaleY]", "consoleCommandSetDlssMvScale", self)
        addConsoleCommand("vrDlssSharpness", "Set DLSS sharpness override: vrDlssSharpness <sharpness 0.0-1.0>", "consoleCommandSetDlssSharpness", self)
        addConsoleCommand("vrDlssPreset", "Set DLSS preset hint: vrDlssPreset <0=Default|3=C|4=D|10=J|11=K|12=L>", "consoleCommandSetDlssPreset", self)
        addConsoleCommand("vrRenderingMode", "Set Stereo Presentation Mode: vrRenderingMode <afr|dualpass|toggle>", "consoleCommandSetRenderingMode", self)
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
            self:restoreMenuBackgroundCamera()
            if self.is2DViewActive then
                self:restoreHudInsets(true)
            else
                self:applyHudState()
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
    self:resetSnapTurnDebounce()
    self:resetToggleViewDebounce()

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
    self:resetSnapTurnDebounce()
    self:resetToggleViewDebounce()
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

    self.lastControlledVehicle = newVeh
    self:resetSnapTurnDebounce()
    self:resetToggleViewDebounce()
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

function VRCompanion:getPlayerCameraYaw(cam)
    if not cam then return 0 end
    local getRotationFn = getRotation or (_G and _G.getRotation)
    if cam.yawNode ~= nil and type(getRotationFn) == "function" then
        local _, yRot, _ = getRotationFn(cam.yawNode)
        return yRot or 0
    elseif type(cam.getRotation) == "function" then
        local _, yRot, _ = cam:getRotation()
        return yRot or 0
    end
    return 0
end

function VRCompanion:onPlayerCameraUpdateRotation(cam, dt)
    if cam == nil then return end
    if cam.getHasOverriddenTarget ~= nil and cam:getHasOverriddenTarget() then
        if cam.updateRotationFromTarget ~= nil then
            cam:updateRotationFromTarget()
        end
        return
    end

    if self.is2DViewActive and self.origPlayerCameraUpdateRotation ~= nil then
        self.origPlayerCameraUpdateRotation(cam, dt)
        return
    end

    local cameraSensitivity = 1.0
    if g_gameSettings ~= nil and type(g_gameSettings.getValue) == "function" and GameSettings ~= nil and GameSettings.SETTING ~= nil then
        cameraSensitivity = g_gameSettings:getValue(GameSettings.SETTING.CAMERA_SENSITIVITY) or 1.0
    end

    local rotationDeltaY = 0
    local rotationDeltaX = 0
    if cam.player ~= nil and cam.player.inputComponent ~= nil then
        rotationDeltaY = -(cam.player.inputComponent.cameraRotationY or 0) * cameraSensitivity
        rotationDeltaX = (cam.player.inputComponent.cameraRotationX or 0) * cameraSensitivity
    end

    local cameraYaw = self:getPlayerCameraYaw(cam)

    if MathUtil ~= nil and type(MathUtil.getValidLimit) == "function" then
        cameraYaw = MathUtil.getValidLimit(cameraYaw + rotationDeltaY)
    else
        cameraYaw = (cameraYaw + rotationDeltaY + math.pi) % (2 * math.pi) - math.pi
    end

    if self.is2DViewActive then
        -- In 2D Floating Window Mode: 2D mouse/thumbstick controls both pitch and yaw.
        local curPitch = 0
        if cam.pitchNode ~= nil and type(getRotation) == "function" then
            curPitch, _, _ = getRotation(cam.pitchNode)
        elseif type(cam.getRotation) == "function" then
            curPitch, _, _ = cam:getRotation()
        end
        local newPitch = (curPitch or 0) + rotationDeltaX
        if cam.pitchMin ~= nil and cam.pitchMax ~= nil then
            newPitch = math.max(cam.pitchMin, math.min(cam.pitchMax, newPitch))
        end
        if cam.pitchNode ~= nil and type(setRotation) == "function" then
            setRotation(cam.pitchNode, newPitch, 0, 0)
        end
        if type(cam.setRotation) == "function" then
            cam:setRotation(newPitch, cameraYaw, 0)
        elseif cam.yawNode ~= nil and type(setRotation) == "function" then
            setRotation(cam.yawNode, 0, cameraYaw, 0)
        end
    else
        -- In VR mode: keep neck level (pitch=0, roll=0); mouse/stick only turns body yaw.
        -- Head pitch and roll are tracked by the HMD and applied during onPlayerCameraUpdate.
        if type(cam.setRotation) == "function" then
            cam:setRotation(0, cameraYaw, 0)
        end
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

function VRCompanion:applyCameraProjection(camNode, eyeIndex)
    if camNode == nil or camNode == 0 then
        return
    end

    -- Cache initial camera projection parameters for clean restoration on map unload or 2D mode
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

    if self.is2DViewActive then
        local orig = self.origCamSettings[camNode]
        if orig ~= nil then
            if orig.fovY ~= nil and type(setFovY) == "function" then
                pcall(setFovY, camNode, orig.fovY)
            end
            if type(setProjectionOffset) == "function" then
                pcall(setProjectionOffset, camNode, 0, 0)
            end
        end
        return
    end

    if not VRBridge or type(VRBridge.getEyeProjection) ~= "function" then
        return
    end

    local proj = VRBridge.getEyeProjection(eyeIndex or 0)
    if not proj or not proj.isValid then
        return
    end

    -- Update camera vertical Field of View to match VR headset display optics
    if type(setFovY) == "function" and proj.fovY ~= nil and proj.fovY > 0 then
        setFovY(camNode, proj.fovY)
    end

    -- In both AFR and Sequential Dual-Pass modes: keep camera centered on optical axis (0, 0) by default to guarantee
    -- parallel rays at infinity and natural binocular convergence without cross-eyed disparity shear, matching
    -- OpenXrManager::SubmitFramePair's aspect-ratio matched symmetric FOV.
    -- If allowObliqueProjection is explicitly enabled, pass asymmetric projection offsets.
    if type(setProjectionOffset) == "function" then
        local offsetX = 0
        local offsetY = 0
        local allowOblique = (self.allowObliqueProjection == true)
        if allowOblique and proj.xOffset ~= nil and proj.yOffset ~= nil then
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

function VRCompanion:updateVehicleCameraTrackingNode(cam)
    if not cam then return end
    local setRotationFn = setRotation or (_G and _G.setRotation)
    local getRotationFn = getRotation or (_G and _G.getRotation)
    local lookYaw = self:getVehicleLookYaw(cam)

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

        local targetHeadRotY = self:normalizeAngle((cam.origHeadTrackingRotY or cam.origRotY or math.pi) + lookYaw)

        if type(setRotationFn) == "function" then
            pcall(setRotationFn, cam.headTrackingNode, 0, targetHeadRotY, 0)
        end
        return targetHeadRotY
    elseif cam.cameraNode ~= nil and type(setRotationFn) == "function" then
        -- Fallback vehicle camera without tracking node
        local combYaw = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)
        pcall(setRotationFn, cam.cameraNode, 0, combYaw, 0)
        return combYaw
    end
    return nil
end

function VRCompanion:snapTurn(direction, angleOverride, curTime)
    if self.isMenuVisible or self.is2DViewActive then
        return false, (self.isMenuVisible and "menu_visible" or "2d_view_active")
    end

    local t = curTime or self:getCurrentTimeMs()
    if self:isSnapTurnDebounced(t) then
        return false, "debounced"
    end

    local vehCam = self:getActiveVehicleCamera()
    if vehCam and vehCam.isInside then
        local angle = angleOverride or self:getSnapTurnAngle()
        local delta = -direction * angle

        if vehCam.origRotY == nil then
            vehCam.origRotY = vehCam.rotY or 0
        end

        local currentRotY = vehCam.rotY or vehCam.origRotY or 0
        vehCam.rotY = self:normalizeAngle(currentRotY + delta)

        local curVehicle = self:getControlledVehicle()
        if curVehicle ~= nil and self.vehicleRotationStates ~= nil and self.vehicleRotationStates[curVehicle] ~= nil then
            self.vehicleRotationStates[curVehicle].rotY = vehCam.rotY
        end

        self:updateVehicleCameraTrackingNode(vehCam)
        self.lastSnapTurnTime = t
        self:signalDiscontinuityFlush()
        print(string.format("[FS25_VR] In-cab snap turn %s (%.1f deg): rotY = %.3f",
            direction < 0 and "left" or "right", math.deg(angle), vehCam.rotY))
        return true, vehCam.rotY
    elseif self:getControlledVehicle() == nil then
        local playerCam = self:getPlayerCamera()
        if playerCam ~= nil then
            local angle = angleOverride or self:getSnapTurnAngle()
            local delta = -direction * angle
            local currentYaw = self:getPlayerCameraYaw(playerCam)
            local newYaw = self:normalizeAngle(currentYaw + delta)

            local setRotationFn = setRotation or (_G and _G.setRotation)
            local getRotationFn = getRotation or (_G and _G.getRotation)

            if playerCam.yawNode ~= nil and type(setRotationFn) == "function" then
                local rx, _, rz = 0, 0, 0
                if type(getRotationFn) == "function" then
                    rx, _, rz = getRotationFn(playerCam.yawNode)
                end
                setRotationFn(playerCam.yawNode, rx or 0, newYaw, rz or 0)
            end

            if type(playerCam.setRotation) == "function" then
                pcall(playerCam.setRotation, playerCam, 0, newYaw, 0)
            end

            self.lastSnapTurnTime = t
            self:signalDiscontinuityFlush()
            print(string.format("[FS25_VR] On-foot snap turn %s (%.1f deg): yawNode = %.3f",
                direction < 0 and "left" or "right", math.deg(angle), newYaw))
            return true, newYaw
        end
    end

    return false, "no_active_camera"
end

function VRCompanion:snapTurnLeft(angleOverride, curTime)
    return self:snapTurn(-1, angleOverride, curTime)
end

function VRCompanion:snapTurnRight(angleOverride, curTime)
    return self:snapTurn(1, angleOverride, curTime)
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
    if not self.isMenuVisible and not self.is2DViewActive and afrFallback then
        if self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then
            return (self:getSubPassIndex() % 2 == 1) and 1 or 0
        end
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

    if cam.origRotY == nil then
        cam.origRotY = cam.rotY or 0
    end
    if cam.origRotX == nil then
        cam.origRotX = cam.rotX or 0
    end
    if cam.origRotZ == nil then
        cam.origRotZ = cam.rotZ or 0
    end

    local curVeh = self:getControlledVehicle()
    if curVeh ~= nil and self.vehicleRotationStates ~= nil and self.vehicleRotationStates[curVeh] ~= nil then
        local activeCam = self:getVehicleActiveCamera(curVeh)
        if activeCam == cam or (activeCam and cam and activeCam.cameraNode == cam.cameraNode) then
            self.vehicleRotationStates[curVeh].rotY = cam.rotY
        end
    end

    -- In 2D Floating Window Mode: bypass 6DOF head tracking updates completely.
    -- VehicleCamera.update handles standard 2D mouse and thumbstick camera rotation.
    if self.is2DViewActive then
        return
    end

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

    -- Look yaw offset relative to neutral forward orientation (cam.rotY - cam.origRotY)
    local lookYaw = self:getVehicleLookYaw(cam)

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
        local nodeRotY = self:updateVehicleCameraTrackingNode(cam)
        if nodeRotY ~= nil then
            targetHeadRotY = nodeRotY
        end
    end

    local cabTransX, cabTransY, cabTransZ = eyeTx, eyeTy, eyeTz
    local cabRotX, cabRotY, cabRotZ = pitch, yaw, roll
    local midCabTransX, midCabTransY, midCabTransZ = tx, ty, tz

    if cam.headTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocalFn) == "function" then
            cabTransX, cabTransY, cabTransZ = localToLocalFn(cam.headTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
            midCabTransX, midCabTransY, midCabTransZ = localToLocalFn(cam.headTrackingNode, camParent, tx, ty, tz)
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

            local rotMidTx, rotMidTy, rotMidTz = self:rotateVectorByYaw(tx, ty, tz, targetHeadRotY)
            midCabTransX = nodeTransX + rotMidTx
            midCabTransY = nodeTransY + rotMidTy
            midCabTransZ = nodeTransZ + rotMidTz
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

        local rotMidTx, rotMidTy, rotMidTz = self:rotateVectorByYaw(tx, ty, tz, totalGazeYaw)
        midCabTransX = baseX + rotMidTx
        midCabTransY = baseY + rotMidTy
        midCabTransZ = baseZ + rotMidTz

        -- Combined head tracking rotation: total driver gaze yaw in cab + physical head yaw
        -- Vertical pitch is maintained level to the physical head horizon
        local combYaw = self:normalizeAngle(totalGazeYaw + yaw)

        cabRotX = pitch
        cabRotY = combYaw
        cabRotZ = roll
    end

    self:ensureDualEyeCameraNodes(cam.cameraNode)
    self:ensureCyclopeanAudioListener(cam.cameraNode)

    if camParent ~= nil and camParent ~= 0 and type(linkFn) == "function" and type(getParentFn) == "function" then
        if self.cameraNodeLeft ~= nil and getParentFn(self.cameraNodeLeft) ~= camParent then
            pcall(linkFn, camParent, self.cameraNodeLeft)
        end
        if self.cameraNodeRight ~= nil and getParentFn(self.cameraNodeRight) ~= camParent then
            pcall(linkFn, camParent, self.cameraNodeRight)
        end
        if self.cyclopeanAudioListenerNode ~= nil and getParentFn(self.cyclopeanAudioListenerNode) ~= camParent then
            pcall(linkFn, camParent, self.cyclopeanAudioListenerNode)
        end
    end

    if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
        if type(setTranslationFn) == "function" then
            setTranslationFn(self.cyclopeanAudioListenerNode, midCabTransX, midCabTransY, midCabTransZ)
        end
        if type(setRotationFn) == "function" then
            setRotationFn(self.cyclopeanAudioListenerNode, cabRotX, cabRotY, cabRotZ)
        end
        local setAudioListenerFn = setAudioListener or (_G and _G.setAudioListener)
        if type(setAudioListenerFn) == "function" then
            setAudioListenerFn(self.cyclopeanAudioListenerNode)
        end
    end

    local isStereoActive = afrFallback and not self.isMenuVisible and not self.is2DViewActive

    if isStereoActive then
        local isRightEye = (eyeIndex == 1)
        local activeEyeCam = isRightEye and self.cameraNodeRight or self.cameraNodeLeft

        if activeEyeCam ~= nil and activeEyeCam ~= 0 then
            self:applyCameraProjection(activeEyeCam, eyeIndex)
            if type(setTranslationFn) == "function" then
                setTranslationFn(activeEyeCam, cabTransX, cabTransY, cabTransZ)
            end
            if type(setRotationFn) == "function" then
                setRotationFn(activeEyeCam, cabRotX, cabRotY, cabRotZ)
            end
            self:bindRenderingCamera(activeEyeCam)
            self.activeRenderingCameraNode = activeEyeCam
        end
    else
        local targetNode = self.cameraNodeLeft or cam.cameraNode
        if targetNode ~= nil and targetNode ~= 0 then
            self:applyCameraProjection(targetNode, 0)
            if type(setTranslationFn) == "function" then
                setTranslationFn(targetNode, cabTransX, cabTransY, cabTransZ)
            end
            if type(setRotationFn) == "function" then
                setRotationFn(targetNode, cabRotX, cabRotY, cabRotZ)
            end
            self:bindRenderingCamera(targetNode)
            self.activeRenderingCameraNode = targetNode
        end
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
    self.activePlayerCamera = cam
    self:getPlayerBaseTranslation(cam)
end

function VRCompanion:onPlayerCameraUpdate(cam, dt)
    if cam ~= nil then
        self.activePlayerCamera = cam
    end
    local activeCamNode = (cam and cam.firstPersonCamera) or (cam and cam.cameraNode)
    if not activeCamNode then
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = self:getActiveEyeIndex(afrFallback)

    self:applyCameraProjection(activeCamNode, eyeIndex)

    -- In 2D Floating Window Mode: bypass 6DOF head tracking updates completely.
    -- Standard on-foot camera rotation is driven via mouse/thumbstick.
    if self.is2DViewActive then
        return
    end

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
    local finalMidTx, finalMidTy, finalMidTz = tx, ty, tz

    if cam.vrHeadTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            finalTx, finalTy, finalTz = localToLocal(cam.vrHeadTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
            finalMidTx, finalMidTy, finalMidTz = localToLocal(cam.vrHeadTrackingNode, camParent, tx, ty, tz)
        end
        if type(localRotationToLocal) == "function" then
            finalRotX, finalRotY, finalRotZ = localRotationToLocal(cam.vrHeadTrackingNode, camParent, pitch, yaw, roll)
        end
    else
        local base = self:getPlayerBaseTranslation(cam)
        finalTx = (base.x or base[1] or 0) + eyeTx
        finalTy = (base.y or base[2] or 0) + eyeTy
        finalTz = (base.z or base[3] or 0) + eyeTz

        finalMidTx = (base.x or base[1] or 0) + tx
        finalMidTy = (base.y or base[2] or 0) + ty
        finalMidTz = (base.z or base[3] or 0) + tz

        finalRotX = pitch
        finalRotY = yaw + math.pi
        finalRotZ = roll
    end

    self:ensureDualEyeCameraNodes(activeCamNode)
    self:ensureCyclopeanAudioListener(activeCamNode)

    if camParent ~= nil and camParent ~= 0 and type(link) == "function" and type(getParent) == "function" then
        if self.cameraNodeLeft ~= nil and getParent(self.cameraNodeLeft) ~= camParent then
            pcall(link, camParent, self.cameraNodeLeft)
        end
        if self.cameraNodeRight ~= nil and getParent(self.cameraNodeRight) ~= camParent then
            pcall(link, camParent, self.cameraNodeRight)
        end
        if self.cyclopeanAudioListenerNode ~= nil and getParent(self.cyclopeanAudioListenerNode) ~= camParent then
            pcall(link, camParent, self.cyclopeanAudioListenerNode)
        end
    end

    if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
        if type(setTranslation) == "function" then
            setTranslation(self.cyclopeanAudioListenerNode, finalMidTx, finalMidTy, finalMidTz)
        end
        if type(setRotation) == "function" then
            setRotation(self.cyclopeanAudioListenerNode, finalRotX, finalRotY, finalRotZ)
        end
        local setAudioListenerFn = setAudioListener or (_G and _G.setAudioListener)
        if type(setAudioListenerFn) == "function" then
            setAudioListenerFn(self.cyclopeanAudioListenerNode)
        end
    end

    local isStereoActive = afrFallback and not self.isMenuVisible and not self.is2DViewActive

    if isStereoActive then
        local isRightEye = (eyeIndex == 1)
        local activeEyeCam = isRightEye and self.cameraNodeRight or self.cameraNodeLeft

        if activeEyeCam ~= nil and activeEyeCam ~= 0 then
            self:applyCameraProjection(activeEyeCam, eyeIndex)
            if type(setTranslation) == "function" then
                setTranslation(activeEyeCam, finalTx, finalTy, finalTz)
            end
            if type(setRotation) == "function" then
                setRotation(activeEyeCam, finalRotX, finalRotY, finalRotZ)
            end
            self:bindRenderingCamera(activeEyeCam)
            self.activeRenderingCameraNode = activeEyeCam
        end
    else
        local targetNode = self.cameraNodeLeft or activeCamNode
        if targetNode ~= nil and targetNode ~= 0 then
            self:applyCameraProjection(targetNode, 0)
            if type(setTranslation) == "function" then
                setTranslation(targetNode, finalTx, finalTy, finalTz)
            end
            if type(setRotation) == "function" then
                setRotation(targetNode, finalRotX, finalRotY, finalRotZ)
            end
            self:bindRenderingCamera(targetNode)
            self.activeRenderingCameraNode = targetNode
        end
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
        if self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0 then
            setTranslationFn(self.cameraNodeLeft, cabTransX, cabTransY, cabTransZ)
        end
        if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
            setTranslationFn(self.cameraNodeRight, cabTransX, cabTransY, cabTransZ)
        end
        if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
            setTranslationFn(self.cyclopeanAudioListenerNode, cabTransX, cabTransY, cabTransZ)
        end
    end
    if type(setRotationFn) == "function" then
        setRotationFn(cam.cameraNode, cabRotX, cabRotY, cabRotZ)
        if self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0 then
            setRotationFn(self.cameraNodeLeft, cabRotX, cabRotY, cabRotZ)
        end
        if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
            setRotationFn(self.cameraNodeRight, cabRotX, cabRotY, cabRotZ)
        end
        if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
            setRotationFn(self.cyclopeanAudioListenerNode, cabRotX, cabRotY, cabRotZ)
        end
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
        if self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0 then
            setTranslation(self.cameraNodeLeft, finalTx, finalTy, finalTz)
        end
        if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
            setTranslation(self.cameraNodeRight, finalTx, finalTy, finalTz)
        end
        if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
            setTranslation(self.cyclopeanAudioListenerNode, finalTx, finalTy, finalTz)
        end
    end
    if type(setRotation) == "function" then
        setRotation(activeCamNode, finalRotX, finalRotY, finalRotZ)
        if self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0 then
            setRotation(self.cameraNodeLeft, finalRotX, finalRotY, finalRotZ)
        end
        if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
            setRotation(self.cameraNodeRight, finalRotX, finalRotY, finalRotZ)
        end
        if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
            setRotation(self.cyclopeanAudioListenerNode, finalRotX, finalRotY, finalRotZ)
        end
    end
end

function VRCompanion:updateMenuBackgroundCamera(dt)
    if not self.isMenuVisible or self.is2DViewActive then
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
    self:signalDiscontinuityFlush()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if self.is2DViewActive then
        if bridge ~= nil and type(bridge.alignMenuQuad) == "function" then
            bridge.alignMenuQuad()
        end
        print("[FS25_VR] 2D Floating Window Mode: Realigned 6DOF floating quad in front of head gaze.")
        return
    end
    local recenterTracking = (self.recenterVrTrackingSpace ~= false)
    if recenterTracking then
        if bridge ~= nil and type(bridge.recenter) == "function" then
            bridge.recenter()
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
            self:updateVehicleCameraTrackingNode(vehCam)
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

function VRCompanion:onSnapTurnLeftAction()
    self:snapTurnLeft()
end

function VRCompanion:onSnapTurnRightAction()
    self:snapTurnRight()
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
    if not self.missionUpdateHooked then
        self:hookMissionUpdate()
    end
    if not self.vehicleSpeedHooked or self.origFerryUpdatePosition == nil then
        self:hookVehicleSpeed()
    end

    -- Update physical IPD-scaled HUD convergence parity across alternating AFR frames
    self:updateHudConvergence(dt)

    -- Keep in-engine action events active across context transitions
    if (self.frameTicks % 30 == 0) or (self.hudToggleActionEventId == nil) or (self.recenterActionEventId == nil) or (self.snapLeftActionEventId == nil) or (self.snapRightActionEventId == nil) or (self.toggleViewActionEventId == nil) then
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

    -- Unhook mission update loop cleanly
    self:unhookMissionUpdate()

    -- Unhook vehicle speed guards cleanly
    self:unhookVehicleSpeed()

    self.inMissionUpdate = false
    self.handshakeComplete = false
    self.lastReportedVerdict = nil
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
    self:resetToggleViewDebounce()

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

    self:set2DViewActive(false)

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
    end
end

function VRCompanion:isKeySnapLeft(sym, unicode)
    local input = Input or (_G and _G.Input)
    if input ~= nil and (input.KEY_kp_4 ~= nil or input.KEY_numPad4 ~= nil) then
        return (sym == input.KEY_kp_4 or sym == input.KEY_numPad4)
    end
    return (sym == 260 or unicode == 52)
end

function VRCompanion:isKeySnapRight(sym, unicode)
    local input = Input or (_G and _G.Input)
    if input ~= nil and (input.KEY_kp_6 ~= nil or input.KEY_numPad6 ~= nil) then
        return (sym == input.KEY_kp_6 or sym == input.KEY_numPad6)
    end
    return (sym == 262 or unicode == 54)
end

function VRCompanion:isKeyNumPad0(sym, unicode)
    local input = Input or (_G and _G.Input)
    if input ~= nil and (input.KEY_kp_0 ~= nil or input.KEY_numPad0 ~= nil) then
        return (sym == input.KEY_kp_0 or sym == input.KEY_numPad0)
    end
    return (sym == 256 or unicode == 48)
end

function VRCompanion:isKeyV(sym, unicode)
    local input = Input or (_G and _G.Input)
    if input ~= nil and input.KEY_v ~= nil then
        return (sym == input.KEY_v)
    end
    return (sym == 118 or unicode == 118 or unicode == 86)
end

function VRCompanion:isCtrlHeld(input, modifier)
    if input ~= nil and type(input.isKeyPressed) == "function" then
        if (input.KEY_rctrl ~= nil and input.isKeyPressed(input.KEY_rctrl))
                or (input.KEY_lctrl ~= nil and input.isKeyPressed(input.KEY_lctrl)) then
            return true
        end
    end
    if modifier ~= nil and modifier > 0 then
        local band = (bit32 and bit32.band) or (bit and bit.band) or function(a, b) return 0 end
        local ctrlMask = (input and (input.MOD_RCTRL or input.MOD_LCTRL or input.MOD_CTRL)) or 0
        if ctrlMask > 0 and band(modifier, ctrlMask) > 0 then
            return true
        elseif modifier == 4 or modifier == 32 or modifier == 64 or modifier == 96 then
            return true
        end
    end
    return false
end

function VRCompanion:keyEvent(unicode, sym, modifier, isDown)
    local input = Input or (_G and _G.Input)
    if not isDown then
        if self:isKeySnapLeft(sym, unicode) then
            self.isSnapLeftKeyDown = false
        elseif self:isKeySnapRight(sym, unicode) then
            self.isSnapRightKeyDown = false
        end
        if self:isKeyNumPad0(sym, unicode) or self:isKeyV(sym, unicode) then
            self.isToggleViewKeyDown = false
        end
        return
    end

    -- Direct hardware shortcut for VR_HUD_TOGGLE (Ctrl + H)
    local isH = false
    if input ~= nil and input.KEY_h ~= nil then
        isH = (sym == input.KEY_h)
    else
        isH = (sym == 104 or unicode == 104 or unicode == 72)
    end

    if isH then
        if self:isCtrlHeld(input, modifier) then
            local curTime = g_time or (_G and _G.g_time)
            if curTime == nil or self.lastHudToggleTime == nil or (curTime - self.lastHudToggleTime) >= 300 then
                print("[FS25_VR] Hardware keyEvent: Ctrl + H detected -> cycling HUD state.")
                self:onHudToggleAction()
            end
            return
        end
    end

    -- Direct hardware shortcut for VR_TOGGLE_VIEW (Ctrl + V)
    if self:isKeyV(sym, unicode) then
        if self:isCtrlHeld(input, modifier) then
            if self.isToggleViewKeyDown then
                return
            end
            self.isToggleViewKeyDown = true
            print("[FS25_VR] Hardware keyEvent: Ctrl + V detected -> toggling VR view mode.")
            self:onToggleVrViewAction()
            return
        end
    end

    -- Direct hardware shortcut for VR_TOGGLE_VIEW (Numpad 0)
    if self:isKeyNumPad0(sym, unicode) then
        if self.isToggleViewKeyDown then
            return
        end
        self.isToggleViewKeyDown = true
        print("[FS25_VR] Hardware keyEvent: Numpad 0 detected -> toggling VR view mode.")
        self:onToggleVrViewAction()
        return
    end

    -- Direct hardware shortcut for VR_SNAP_TURN_LEFT (Numpad 4)
    if self:isKeySnapLeft(sym, unicode) then
        if self.isSnapLeftKeyDown then
            return
        end
        self.isSnapLeftKeyDown = true
        print("[FS25_VR] Hardware keyEvent: Numpad 4 detected -> snap turn left.")
        self:snapTurnLeft()
        return
    end

    -- Direct hardware shortcut for VR_RECENTER (Numpad 5)
    local isRecenter = false
    if input ~= nil and (input.KEY_kp_5 ~= nil or input.KEY_numPad5 ~= nil) then
        isRecenter = (sym == input.KEY_kp_5 or sym == input.KEY_numPad5)
    else
        isRecenter = (sym == 261 or unicode == 53)
    end
    if isRecenter then
        print("[FS25_VR] Hardware keyEvent: Numpad 5 detected -> recentering VR view.")
        self:onRecenterAction()
        return
    end

    -- Direct hardware shortcut for VR_SNAP_TURN_RIGHT (Numpad 6)
    if self:isKeySnapRight(sym, unicode) then
        if self.isSnapRightKeyDown then
            return
        end
        self.isSnapRightKeyDown = true
        print("[FS25_VR] Hardware keyEvent: Numpad 6 detected -> snap turn right.")
        self:snapTurnRight()
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
        return "Usage: vrDlssMvScale <scaleX> [scaleY] (e.g. 0.0 to eliminate stereoscopic smearing, 1.0 for default)"
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

-- Register as an engine mod event listener if in-game environment
if addModEventListener ~= nil then
    addModEventListener(VRCompanion)
end
