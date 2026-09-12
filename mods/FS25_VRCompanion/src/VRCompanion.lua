-- ====================================================================
-- FS25 VR Companion Mod: In-Engine Luau Coordination & Handshake
-- Interfaces with in-process native VRBridge table injected by dxgi.dll
-- ====================================================================

VRCompanion = {}
VRCompanion.name = g_currentModName or "FS25_VRCompanion"
VRCompanion.version = "1.0.0.0"
VRCompanion.handshakeComplete = false
VRCompanion.stereoToggled = false
VRCompanion.stereoInversionToggled = false
VRCompanion.lastReportedVerdict = nil
VRCompanion.frameTicks = 0
VRCompanion.vehicleCameraHooked = false
VRCompanion.playerCameraHooked = false

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

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
            print(string.format("[FS25_VR] Handshake successful! VRBridge detected (version: %s, mode: %s, ready: %s).",
                tostring(handshakeResult.version or version),
                tostring(handshakeResult.mode or "unknown"),
                tostring(ready)))
        else
            self.handshakeComplete = false
            print(string.format("[FS25_VR] Handshake failed or rejected by native VRBridge (version: %s, ready: %s).",
                tostring(version),
                tostring(ready)))
        end
    else
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

    if type(setInvertStereoRendering) == "function" then
        setInvertStereoRendering(false)
        self.stereoInversionToggled = true
        print("[FS25_VR] Invoked engine stereo inversion toggle: setInvertStereoRendering(false)")
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

    -- 4. Hook camera systems for 6DOF tracking
    self:hookVehicleCamera()
    self:hookPlayerCamera()

    -- 5. Register in-engine Recenter Action
    if g_inputBinding ~= nil and type(g_inputBinding.registerActionEvent) == "function" then
        local actionId = (InputAction and InputAction.VR_RECENTER) or "VR_RECENTER"
        local _, eventId = g_inputBinding:registerActionEvent(actionId, self, self.onRecenterAction, false, true, false, true)
        if eventId and type(g_inputBinding.setActionEventTextPriority) == "function" then
            g_inputBinding:setActionEventTextPriority(eventId, (GS_PRIO_HIGH or 1))
        end
        print("[FS25_VR] Registered in-engine Recenter Action event (VR_RECENTER).")
    end
end

function VRCompanion:getPlayerCamera()
    if g_currentMission ~= nil and g_currentMission.player ~= nil then
        return g_currentMission.player.camera
    end
    return nil
end

function VRCompanion:getActiveVehicleCamera()
    if g_currentMission ~= nil and g_currentMission.controlledVehicle ~= nil then
        local vehicle = g_currentMission.controlledVehicle
        if type(vehicle.getActiveCamera) == "function" then
            return vehicle:getActiveCamera()
        end
    end
    return nil
end

function VRCompanion:getPlayerBaseTranslation(cam)
    if cam.vrBaseTranslation == nil and type(getTranslation) == "function" then
        local lx, ly, lz = getTranslation(cam.firstPersonCamera)
        cam.vrBaseTranslation = { x = lx, y = ly, z = lz, [1] = lx, [2] = ly, [3] = lz }
    end
    return cam.vrBaseTranslation or { x = 0, y = 0, z = 0, [1] = 0, [2] = 0, [3] = 0 }
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

function VRCompanion:hookPlayerCamera()
    if PlayerCamera ~= nil and not self.playerCameraHooked then
        if PlayerCamera.makeCurrent ~= nil then
            self.origPlayerCameraMakeCurrent = PlayerCamera.makeCurrent
            PlayerCamera.makeCurrent = function(cam)
                self.origPlayerCameraMakeCurrent(cam)
                VRCompanion:onPlayerCameraMakeCurrent(cam)
            end
        end
        if PlayerCamera.update ~= nil then
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

function VRCompanion:onVehicleCameraUpdate(cam, dt)
    if not VRBridge or type(VRBridge.getHeadPose) ~= "function" then
        return
    end

    -- Only apply 6DOF tracking to interior in-cab cameras
    if not cam or not cam.isInside or cam.cameraNode == nil then
        return
    end

    local pose = VRBridge.getHeadPose()
    if not pose or not pose.isValid then
        return
    end

    local tx, ty, tz = pose.x, pose.y, pose.z
    local pitch, yaw, roll = pose.pitch, pose.yaw, pose.roll

    local camParent = 0
    if type(getParent) == "function" then
        camParent = getParent(cam.cameraNode)
    end

    local cabTransX, cabTransY, cabTransZ = tx, ty, tz
    local cabRotX, cabRotY, cabRotZ = pitch, yaw, roll

    if cam.headTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            cabTransX, cabTransY, cabTransZ = localToLocal(cam.headTrackingNode, camParent, tx, ty, tz)
        end
        if type(localRotationToLocal) == "function" then
            cabRotX, cabRotY, cabRotZ = localRotationToLocal(cam.headTrackingNode, camParent, pitch, yaw, roll)
        end
    else
        -- Fallback to driver seat anchor baseline when no dedicated headTrackingNode is assigned
        local baseX = cam.origTransX or cam.transX or 0
        local baseY = cam.origTransY or cam.transY or 0
        local baseZ = cam.origTransZ or cam.transZ or 0
        cabTransX = baseX + tx
        cabTransY = baseY + ty
        cabTransZ = baseZ + tz
    end

    if type(setTranslation) == "function" then
        setTranslation(cam.cameraNode, cabTransX, cabTransY, cabTransZ)
    end
    if type(setRotation) == "function" then
        setRotation(cam.cameraNode, cabRotX, cabRotY, cabRotZ)
    end
end

function VRCompanion:onPlayerCameraMakeCurrent(cam)
    if not cam or cam.firstPersonCamera == nil then
        return
    end
    self:getPlayerBaseTranslation(cam)
end

function VRCompanion:onPlayerCameraUpdate(cam, dt)
    if not VRBridge or type(VRBridge.getHeadPose) ~= "function" then
        return
    end

    if not cam or not cam.isFirstPerson or cam.firstPersonCamera == nil then
        return
    end

    local pose = VRBridge.getHeadPose()
    if not pose or not pose.isValid then
        return
    end

    local base = self:getPlayerBaseTranslation(cam)
    local tx = (base.x or base[1]) + pose.x
    local ty = (base.y or base[2]) + pose.y
    local tz = (base.z or base[3]) + pose.z

    if type(setTranslation) == "function" then
        setTranslation(cam.firstPersonCamera, tx, ty, tz)
    end
    if type(setRotation) == "function" then
        setRotation(cam.firstPersonCamera, pose.pitch, pose.yaw, pose.roll)
    end
end

function VRCompanion:onRecenterAction()
    print("[FS25_VR] Recenter Action triggered.")
    if VRBridge ~= nil and type(VRBridge.recenter) == "function" then
        VRBridge.recenter()
    end

    -- Realign vehicle cockpit driver seat origin if seated in a vehicle
    local vehCam = self:getActiveVehicleCamera()
    if vehCam and vehCam.isInside then
        print("[FS25_VR] Cockpit head pose realigned to driver seat reference origin.")
    end

    -- Realign on-foot player camera relative to character root
    local playerCam = self:getPlayerCamera()
    if playerCam and playerCam.firstPersonCamera ~= nil then
        print("[FS25_VR] On-foot head pose realigned to character root reference origin.")
    end

    print("[FS25_VR] Recenter Action complete: reference space origin realigned.")
end

function VRCompanion:update(dt)
    self.frameTicks = (self.frameTicks or 0) + 1

    if VRBridge ~= nil and type(VRBridge.getStereoTelemetry) == "function" then
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
    if self.origPlayerCameraUpdate ~= nil and PlayerCamera ~= nil then
        PlayerCamera.update = self.origPlayerCameraUpdate
        self.origPlayerCameraUpdate = nil
    end

    self.handshakeComplete = false
    self.lastReportedVerdict = nil
    self.frameTicks = 0
    self.vehicleCameraHooked = false
    self.playerCameraHooked = false
end

-- Register as an engine mod event listener if in-game environment
if addModEventListener ~= nil then
    addModEventListener(VRCompanion)
end
