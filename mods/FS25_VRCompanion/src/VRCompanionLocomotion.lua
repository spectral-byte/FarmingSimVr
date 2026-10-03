-- ====================================================================
-- FS25 VR Companion Mod: Locomotion Subsystem
-- Manages snap-turning, recentering, driver seat realignments, and
-- vehicle entry/exit orientation transitions.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Locomotion = VRCompanion.Locomotion or {}

-- Recenter VR Tracking Space Configuration
VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE = true
if VRCompanion.recenterVrTrackingSpace == nil then
    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
end

VRCompanion.RECENTER_MODES = VRCompanion.RECENTER_MODES or {
    BOTH = "Both",
    VEHICLE_ONLY = "VehicleOnly"
}

-- VR Snap Turn Locomotion Configuration (Issue #35)
VRCompanion.DEFAULT_SNAP_TURN_ANGLE = math.pi * 0.5 -- 90 degrees (pi/2 radians)
if VRCompanion.snapTurnAngle == nil then
    VRCompanion.snapTurnAngle = VRCompanion.DEFAULT_SNAP_TURN_ANGLE
end
VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS = 250 -- Minimum 250ms debounce cooldown
if VRCompanion.snapTurnDebounceMs == nil then
    VRCompanion.snapTurnDebounceMs = VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS
end
VRCompanion.lastSnapTurnTime = nil
VRCompanion.isSnapLeftKeyDown = false
VRCompanion.isSnapRightKeyDown = false
VRCompanion.vehicleRotationStates = VRCompanion.vehicleRotationStates or {}

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
        if type(self.saveConfiguration) == "function" then
            self:saveConfiguration()
        end
    end
    return self:getRecenterMode()
end

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
        if saveConfig == true and type(self.saveConfiguration) == "function" then
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
        if type(self.signalDiscontinuityFlush) == "function" then
            self:signalDiscontinuityFlush()
        end
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
            if type(self.signalDiscontinuityFlush) == "function" then
                self:signalDiscontinuityFlush()
            end
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

function VRCompanion:onRecenterAction()
    print("[FS25_VR] Recenter Action triggered.")
    if type(self.signalDiscontinuityFlush) == "function" then
        self:signalDiscontinuityFlush()
    end
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

-- Export to Locomotion subsystem table
VRCompanion.Locomotion.getRecenterMode = function(self, ...) return VRCompanion.getRecenterMode(VRCompanion, ...) end
VRCompanion.Locomotion.setRecenterMode = function(self, ...) return VRCompanion.setRecenterMode(VRCompanion, ...) end
VRCompanion.Locomotion.getSnapTurnAngle = function(self, ...) return VRCompanion.getSnapTurnAngle(VRCompanion, ...) end
VRCompanion.Locomotion.setSnapTurnAngle = function(self, ...) return VRCompanion.setSnapTurnAngle(VRCompanion, ...) end
VRCompanion.Locomotion.getSnapTurnAngleDegrees = function(self, ...) return VRCompanion.getSnapTurnAngleDegrees(VRCompanion, ...) end
VRCompanion.Locomotion.getCurrentTimeMs = function(self, ...) return VRCompanion.getCurrentTimeMs(VRCompanion, ...) end
VRCompanion.Locomotion.isSnapTurnDebounced = function(self, ...) return VRCompanion.isSnapTurnDebounced(VRCompanion, ...) end
VRCompanion.Locomotion.resetSnapTurnDebounce = function(self, ...) return VRCompanion.resetSnapTurnDebounce(VRCompanion, ...) end
VRCompanion.Locomotion.initVehicleCameraState = function(self, ...) return VRCompanion.initVehicleCameraState(VRCompanion, ...) end
VRCompanion.Locomotion.saveVehicleRotationState = function(self, ...) return VRCompanion.saveVehicleRotationState(VRCompanion, ...) end
VRCompanion.Locomotion.restoreVehicleRotationState = function(self, ...) return VRCompanion.restoreVehicleRotationState(VRCompanion, ...) end
VRCompanion.Locomotion.resetVehicleTransforms = function(self, ...) return VRCompanion.resetVehicleTransforms(VRCompanion, ...) end
VRCompanion.Locomotion.onEnterVehicle = function(self, ...) return VRCompanion.onEnterVehicle(VRCompanion, ...) end
VRCompanion.Locomotion.onLeaveVehicle = function(self, ...) return VRCompanion.onLeaveVehicle(VRCompanion, ...) end
VRCompanion.Locomotion.onVehicleTransition = function(self, ...) return VRCompanion.onVehicleTransition(VRCompanion, ...) end
VRCompanion.Locomotion.onVehicleCameraChanged = function(self, ...) return VRCompanion.onVehicleCameraChanged(VRCompanion, ...) end
VRCompanion.Locomotion.getPlayerBaseTranslation = function(self, ...) return VRCompanion.getPlayerBaseTranslation(VRCompanion, ...) end
VRCompanion.Locomotion.normalizeAngle = function(self, ...) return VRCompanion.normalizeAngle(VRCompanion, ...) end
VRCompanion.Locomotion.getVehicleLookYaw = function(self, ...) return VRCompanion.getVehicleLookYaw(VRCompanion, ...) end
VRCompanion.Locomotion.rotateVectorByYaw = function(self, ...) return VRCompanion.rotateVectorByYaw(VRCompanion, ...) end
VRCompanion.Locomotion.updateVehicleCameraTrackingNode = function(self, ...) return VRCompanion.updateVehicleCameraTrackingNode(VRCompanion, ...) end
VRCompanion.Locomotion.snapTurn = function(self, ...) return VRCompanion.snapTurn(VRCompanion, ...) end
VRCompanion.Locomotion.snapTurnLeft = function(self, ...) return VRCompanion.snapTurnLeft(VRCompanion, ...) end
VRCompanion.Locomotion.snapTurnRight = function(self, ...) return VRCompanion.snapTurnRight(VRCompanion, ...) end
VRCompanion.Locomotion.realignVehicleCameraToSeat = function(self, ...) return VRCompanion.realignVehicleCameraToSeat(VRCompanion, ...) end
VRCompanion.Locomotion.realignPlayerCameraToRoot = function(self, ...) return VRCompanion.realignPlayerCameraToRoot(VRCompanion, ...) end
VRCompanion.Locomotion.onRecenterAction = function(self, ...) return VRCompanion.onRecenterAction(VRCompanion, ...) end
VRCompanion.Locomotion.onSnapTurnLeftAction = function(self, ...) return VRCompanion.onSnapTurnLeftAction(VRCompanion, ...) end
VRCompanion.Locomotion.onSnapTurnRightAction = function(self, ...) return VRCompanion.onSnapTurnRightAction(VRCompanion, ...) end

return VRCompanion.Locomotion
