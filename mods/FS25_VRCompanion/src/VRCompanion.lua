-- ====================================================================
-- FS25 VR Companion Mod: In-Engine Luau Coordination & Handshake
-- Interfaces with in-process native VRBridge table injected by dxgi.dll
-- ====================================================================

VRCompanion = {}
VRCompanion.name = g_currentModName or "FS25_VRCompanion"
VRCompanion.version = "1.0.0.0"
VRCompanion.handshakeComplete = false
VRCompanion.stereoToggled = false
VRCompanion.lastReportedVerdict = nil
VRCompanion.frameTicks = 0
VRCompanion.vehicleCameraHooked = false
VRCompanion.playerCameraHooked = false
VRCompanion.origCamSettings = {}
-- Proactively probe head tracking availability to trigger native LuauInterceptor detour
if type(isHeadTrackingAvailable) == "function" then
    pcall(isHeadTrackingAvailable)
elseif type(getHeadTrackingTranslation) == "function" then
    pcall(getHeadTrackingTranslation)
end

function VRCompanion:loadMap(name)
    print(string.format("[FS25_VR] Initializing Companion Mod on map '%s'...", tostring(name)))

    -- Proactively trigger native detour in case loadMap executes before any camera update
    if VRBridge == nil and type(isHeadTrackingAvailable) == "function" then
        pcall(isHeadTrackingAvailable)
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

function VRCompanion:onPlayerCameraUpdateRotation(cam, dt)
    if cam == nil then return end
    if cam.getHasOverriddenTarget ~= nil and cam:getHasOverriddenTarget() then
        if cam.updateRotationFromTarget ~= nil then
            cam:updateRotationFromTarget()
        end
        return
    end

    local cameraSensitivity = 1.0
    if g_gameSettings ~= nil and type(g_gameSettings.getValue) == "function" and GameSettings ~= nil and GameSettings.SETTING ~= nil then
        cameraSensitivity = g_gameSettings:getValue(GameSettings.SETTING.CAMERA_SENSITIVITY) or 1.0
    end

    local rotationDeltaY = 0
    if cam.player ~= nil and cam.player.inputComponent ~= nil then
        rotationDeltaY = -(cam.player.inputComponent.cameraRotationY or 0) * cameraSensitivity
    end

    local _, cameraYaw, _ = 0, 0, 0
    if type(cam.getRotation) == "function" then
        _, cameraYaw, _ = cam:getRotation()
    end

    if MathUtil ~= nil and type(MathUtil.getValidLimit) == "function" then
        cameraYaw = MathUtil.getValidLimit(cameraYaw + rotationDeltaY)
    else
        cameraYaw = (cameraYaw + rotationDeltaY) % (2 * math.pi)
    end

    -- In VR mode: keep neck level (pitch=0, roll=0); mouse/stick only turns body yaw.
    -- Head pitch and roll are tracked by the HMD and applied during onPlayerCameraUpdate.
    if type(cam.setRotation) == "function" then
        cam:setRotation(0, cameraYaw, 0)
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

    if not VRBridge or type(VRBridge.getEyeProjection) ~= "function" then
        return
    end

    local proj = VRBridge.getEyeProjection(eyeIndex or 0)
    if not proj or not proj.isValid then
        return
    end

    -- Cache initial camera projection parameters for clean restoration on map unload
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

    -- Update camera vertical Field of View to match VR headset display optics
    if type(setFovY) == "function" and proj.fovY ~= nil and proj.fovY > 0 then
        setFovY(camNode, proj.fovY)
    end

    -- In VR AFR mode: keep camera centered on optical axis (0, 0) to guarantee
    -- parallel rays at infinity and natural binocular convergence without cross-eyed disparity shear.
    -- If allowObliqueProjection is explicitly enabled, pass the asymmetric projection offsets.
    if type(setProjectionOffset) == "function" then
        local offsetX = 0
        local offsetY = 0
        if self.allowObliqueProjection and proj.xOffset ~= nil and proj.yOffset ~= nil then
            offsetX = proj.xOffset * (self.projectionOffsetScale or 1.0)
            offsetY = proj.yOffset * (self.projectionOffsetScale or 1.0)
        end
        setProjectionOffset(camNode, offsetX, offsetY)
    end
end

function VRCompanion:getFrameParity()
    if VRBridge ~= nil then
        if VRBridge.frameParity ~= nil then
            return VRBridge.frameParity
        elseif type(VRBridge.getFrameParity) == "function" then
            return VRBridge.getFrameParity()
        end
    end
    return 0
end

function VRCompanion:getIpd()
    if VRBridge ~= nil then
        if type(VRBridge.getIpd) == "function" then
            return VRBridge.getIpd() or 0.064
        elseif VRBridge.ipd ~= nil then
            return VRBridge.ipd
        end
    end
    return 0.064
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
    if VRBridge ~= nil then
        if VRBridge.requiresAfrFallback ~= nil then
            return VRBridge.requiresAfrFallback
        elseif type(VRBridge.getRequiresAfrFallback) == "function" then
            return VRBridge.getRequiresAfrFallback()
        elseif type(VRBridge.requiresAfrFallback) == "function" then
            return VRBridge.requiresAfrFallback()
        end
    end
    return true
end

function VRCompanion:computeEyeOffset(pose, eyeIndex, afrFallback)
    if not afrFallback then
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

function VRCompanion:getResolvedHeadPose()
    local pose = nil
    if VRBridge ~= nil and type(VRBridge.getHeadPose) == "function" then
        pose = VRBridge.getHeadPose()
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

function VRCompanion:onVehicleCameraUpdate(cam, dt)
    -- Only apply 6DOF tracking and projection to interior in-cab cameras
    if not cam or not cam.isInside or cam.cameraNode == nil then
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = 0
    if afrFallback then
        local parity = self:getFrameParity()
        eyeIndex = (parity % 2 == 1) and 1 or 0
    end

    self:applyCameraProjection(cam.cameraNode, eyeIndex)

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
        camParent = getParent(cam.cameraNode)
    end

    -- Auto-construct headTrackingNode matching VehicleCamera:loadFromXML if missing
    if cam.headTrackingNode == nil and type(createTransformGroup) == "function" then
        local posNode = cam.cameraPositionNode or cam.cameraNode
        local pNode = (posNode and type(getParent) == "function") and getParent(posNode) or 0
        if pNode ~= nil and pNode ~= 0 then
            local dx, _, dz = 0, 0, 1
            if type(localDirectionToLocal) == "function" then
                dx, _, dz = localDirectionToLocal(posNode, pNode, 0, 0, 1)
            end
            local lx, ly, lz = 0, 0, 0
            if type(localToLocal) == "function" then
                lx, ly, lz = localToLocal(posNode, pNode, 0, 0, 0)
            end
            local tg = createTransformGroup("headTrackingNode")
            if tg ~= nil and tg ~= 0 then
                if type(link) == "function" then
                    link(pNode, tg)
                end
                if type(setTranslation) == "function" then
                    setTranslation(tg, lx, ly, lz)
                end
                if (math.abs(dx) + math.abs(dz) > 0.0001) and type(setDirection) == "function" then
                    setDirection(tg, dx, 0, dz, 0, 1, 0)
                elseif type(setRotation) == "function" then
                    setRotation(tg, 0, 0, 0)
                end
                cam.headTrackingNode = tg
            end
        end
    end

    local cabTransX, cabTransY, cabTransZ = eyeTx, eyeTy, eyeTz
    local cabRotX, cabRotY, cabRotZ = pitch, yaw, roll

    if cam.headTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            cabTransX, cabTransY, cabTransZ = localToLocal(cam.headTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
        end
        if type(localRotationToLocal) == "function" then
            cabRotX, cabRotY, cabRotZ = localRotationToLocal(cam.headTrackingNode, camParent, pitch, yaw, roll)
        end
    else
        -- Fallback to driver seat anchor baseline when no dedicated headTrackingNode is assigned
        local baseX = cam.origTransX or cam.transX or 0
        local baseY = cam.origTransY or cam.transY or 0
        local baseZ = cam.origTransZ or cam.transZ or 0
        cabTransX = baseX + eyeTx
        cabTransY = baseY + eyeTy
        cabTransZ = baseZ + eyeTz
    end

    if type(setTranslation) == "function" then
        setTranslation(cam.cameraNode, cabTransX, cabTransY, cabTransZ)
    end
    if type(setRotation) == "function" then
        setRotation(cam.cameraNode, cabRotX, cabRotY, cabRotZ)
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
    self:getPlayerBaseTranslation(cam)
end

function VRCompanion:onPlayerCameraUpdate(cam, dt)
    local activeCamNode = (cam and cam.firstPersonCamera) or (cam and cam.cameraNode)
    if not activeCamNode then
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = 0
    if afrFallback then
        local parity = self:getFrameParity()
        eyeIndex = (parity % 2 == 1) and 1 or 0
    end

    self:applyCameraProjection(activeCamNode, eyeIndex)

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
                    setRotation(tg, 0, 0, 0)
                end
                cam.vrHeadTrackingNode = tg
            end
        end
    end

    local finalTx, finalTy, finalTz = eyeTx, eyeTy, eyeTz
    local finalRotX, finalRotY, finalRotZ = pitch, yaw, roll

    if cam.vrHeadTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            finalTx, finalTy, finalTz = localToLocal(cam.vrHeadTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
        end
        if type(localRotationToLocal) == "function" then
            finalRotX, finalRotY, finalRotZ = localRotationToLocal(cam.vrHeadTrackingNode, camParent, pitch, yaw, roll)
        end
    else
        local base = self:getPlayerBaseTranslation(cam)
        finalTx = (base.x or base[1] or 0) + eyeTx
        finalTy = (base.y or base[2] or 0) + eyeTy
        finalTz = (base.z or base[3] or 0) + eyeTz
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

    local camParent = 0
    if type(getParent) == "function" then
        camParent = getParent(cam.cameraNode)
    end

    local cabTransX, cabTransY, cabTransZ = 0, 0, 0
    local cabRotX, cabRotY, cabRotZ = 0, 0, 0

    if cam.headTrackingNode ~= nil and camParent ~= 0 then
        if type(localToLocal) == "function" then
            cabTransX, cabTransY, cabTransZ = localToLocal(cam.headTrackingNode, camParent, 0, 0, 0)
        end
        if type(localRotationToLocal) == "function" then
            cabRotX, cabRotY, cabRotZ = localRotationToLocal(cam.headTrackingNode, camParent, 0, 0, 0)
        end
    else
        cabTransX = cam.origTransX or cam.transX or 0
        cabTransY = cam.origTransY or cam.transY or 0
        cabTransZ = cam.origTransZ or cam.transZ or 0
    end

    if type(setTranslation) == "function" then
        setTranslation(cam.cameraNode, cabTransX, cabTransY, cabTransZ)
    end
    if type(setRotation) == "function" then
        setRotation(cam.cameraNode, cabRotX, cabRotY, cabRotZ)
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
    end

    if type(setTranslation) == "function" then
        setTranslation(activeCamNode, finalTx, finalTy, finalTz)
    end
    if type(setRotation) == "function" then
        setRotation(activeCamNode, finalRotX, finalRotY, finalRotZ)
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
        self:realignVehicleCameraToSeat(vehCam)
        print("[FS25_VR] Cockpit head pose realigned to driver seat reference origin.")
    end

    -- Realign on-foot player camera relative to character root
    local playerCam = self:getPlayerCamera()
    if playerCam and playerCam.firstPersonCamera ~= nil then
        self:realignPlayerCameraToRoot(playerCam)
        print("[FS25_VR] On-foot head pose realigned to character root reference origin.")
    end

    print("[FS25_VR] Recenter Action complete: reference space origin realigned.")
end

function VRCompanion:update(dt)
    self.frameTicks = (self.frameTicks or 0) + 1

    if not self.vehicleCameraHooked then
        self:hookVehicleCamera()
    end
    if not self.playerCameraHooked then
        self:hookPlayerCamera()
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

    self.handshakeComplete = false
    self.lastReportedVerdict = nil
    self.frameTicks = 0
    self.vehicleCameraHooked = false
    self.playerCameraHooked = false

    if VRBridge ~= nil and type(VRBridge.onMapUnload) == "function" then
        VRBridge.onMapUnload()
    end

    -- Restore original camera FOVs and projection offsets
    if self.origCamSettings ~= nil then
        for camNode, orig in pairs(self.origCamSettings) do
            if orig.fovY ~= nil and type(setFovY) == "function" then
                setFovY(camNode, orig.fovY)
            end
            if type(setProjectionOffset) == "function" then
                setProjectionOffset(camNode, orig.offsetX or 0, orig.offsetY or 0)
            end
        end
        self.origCamSettings = {}
    end
end

-- Register as an engine mod event listener if in-game environment
if addModEventListener ~= nil then
    addModEventListener(VRCompanion)
end
