VRCompanion = VRCompanion or {}

VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE = true
if VRCompanion.recenterVrTrackingSpace == nil then
    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
end

VRCompanion.RECENTER_MODES = VRCompanion.RECENTER_MODES or {
    BOTH = "Both",
    VEHICLE_ONLY = "VehicleOnly"
}

VRCompanion.DEFAULT_SNAP_TURN_ANGLE = math.pi * 0.5
if VRCompanion.snapTurnAngle == nil then
    VRCompanion.snapTurnAngle = VRCompanion.DEFAULT_SNAP_TURN_ANGLE
end
VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS = 250
if VRCompanion.snapTurnDebounceMs == nil then
    VRCompanion.snapTurnDebounceMs = VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS
end
VRCompanion.lastSnapTurnTime = nil
local WEAK_KEYS_MT = { __mode = "k" }
VRCompanion.vehicleRotationStates = setmetatable(VRCompanion.vehicleRotationStates or {}, WEAK_KEYS_MT)

local function ensureVehicleRotationStates(self)
    if self.vehicleRotationStates == nil then
        self.vehicleRotationStates = setmetatable({}, WEAK_KEYS_MT)
    elseif getmetatable(self.vehicleRotationStates) == nil then
        setmetatable(self.vehicleRotationStates, WEAK_KEYS_MT)
    end
    return self.vehicleRotationStates
end

function VRCompanion:getVehicleCameraRotationKey(veh, cam, camIndex)
    if camIndex ~= nil then
        return camIndex
    end
    local enterable = type(veh) == "table" and veh.spec_enterable or nil
    if enterable ~= nil then
        if type(enterable.cameras) == "table" and cam ~= nil then
            local activeIdx = enterable.camIndex
            if activeIdx ~= nil and enterable.cameras[activeIdx] == cam then
                return activeIdx
            end
            for index, candidate in ipairs(enterable.cameras) do
                if candidate == cam or (cam.cameraNode ~= nil and candidate.cameraNode == cam.cameraNode) then
                    return index
                end
            end
        end
        if enterable.camIndex ~= nil then
            return enterable.camIndex
        end
    end
    return (type(cam) == "table" and cam.cameraNode) or 1
end

function VRCompanion:getVehicleRotationState(veh, cam, camIndex)
    if veh == nil or self.vehicleRotationStates == nil then return nil end
    local perVehicle = self.vehicleRotationStates[veh]
    if perVehicle == nil then return nil end
    local key = self:getVehicleCameraRotationKey(veh, cam, camIndex)
    return perVehicle[key]
end

function VRCompanion:setVehicleRotationState(veh, cam, state, camIndex)
    if veh == nil then return end
    local states = ensureVehicleRotationStates(self)
    local perVehicle = states[veh]
    if perVehicle == nil then
        perVehicle = {}
        states[veh] = perVehicle
    end
    local key = self:getVehicleCameraRotationKey(veh, cam, camIndex)
    perVehicle[key] = state
end

function VRCompanion:updateVehicleRotationState(veh, cam, rotY, rotX, camIndex)
    local saved = self:getVehicleRotationState(veh, cam, camIndex)
    if saved == nil then return end
    if rotY ~= nil then saved.rotY = rotY end
    if rotX ~= nil then saved.rotX = rotX end
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

    self:recordHeadTrackingForward(cam)
end

function VRCompanion:recordHeadTrackingForward(cam)
    if cam.headTrackingNode == nil or cam.headTrackingNode == 0 or cam.origHeadTrackingRotY ~= nil then
        return
    end
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

function VRCompanion:recordOutsideCameraDefaults(cam)
    if cam.origRotY == nil then
        cam.origRotY = cam.rotY or 0
    end
    if cam.origRotX == nil then
        cam.origRotX = cam.rotX or 0
    end
end

function VRCompanion:saveVehicleRotationState(veh, camOverride, camIndex)
    if veh == nil then return end
    ensureVehicleRotationStates(self)

    local cam = camOverride or self:getVehicleActiveCamera(veh)
    if cam ~= nil and cam.isInside then
        self:initVehicleCameraState(veh, cam)
        self:setVehicleRotationState(veh, cam, {
            rotY = cam.rotY,
            rotX = cam.rotX,
            origRotY = cam.origRotY,
            origRotX = cam.origRotX,
            origHeadTrackingRotX = cam.origHeadTrackingRotX,
            origHeadTrackingRotY = cam.origHeadTrackingRotY,
            origHeadTrackingRotZ = cam.origHeadTrackingRotZ,
        }, camIndex)
    end
end

function VRCompanion:restoreVehicleRotationState(veh, camOverride, camIndex)
    if veh == nil then return end
    ensureVehicleRotationStates(self)

    local cam = camOverride or self:getVehicleActiveCamera(veh)
    if cam == nil then return end
    if not cam.isInside then
        self:recordOutsideCameraDefaults(cam)
        return
    end

    self:initVehicleCameraState(veh, cam)

    local saved = self:getVehicleRotationState(veh, cam, camIndex)
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
        self:saveVehicleRotationState(veh, cam, camIndex)
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
    self.outsideCameraHeading = nil
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
    self.outsideCameraHeading = nil
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
    self.outsideCameraHeading = nil
    self:resetSnapTurnDebounce()
    self:resetToggleViewDebounce()
    self:registerActionEvents(true)
    self:applyHudState()
end

function VRCompanion:onVehicleCameraChanged(veh, camIndex)
    if veh ~= nil then
        if veh == self:getControlledVehicle() then
            self:signalDiscontinuityFlush()
            self.outsideCameraHeading = nil
        end

        local cam = self:getVehicleActiveCamera(veh)
        if cam ~= nil then
            self:restoreVehicleRotationState(veh, cam, camIndex)
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
    local lookYaw = self:getVehicleLookYaw(cam)

    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        self:recordHeadTrackingForward(cam)

        local targetHeadRotY = self:normalizeAngle((cam.origHeadTrackingRotY or cam.origRotY or math.pi) + lookYaw)

        if type(setRotationFn) == "function" then
            pcall(setRotationFn, cam.headTrackingNode, 0, targetHeadRotY, 0)
        end
        return targetHeadRotY
    elseif cam.cameraNode ~= nil and type(setRotationFn) == "function" then
        local combYaw = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)
        pcall(setRotationFn, cam.cameraNode, 0, combYaw, 0)
        return combYaw
    end
    return nil
end

function VRCompanion:snapTurn(direction, angleOverride, curTime)
    local menuTakesInput = self:doesMenuTakeInput()
    if menuTakesInput or self.is2DViewActive then
        return false, (menuTakesInput and "menu_visible" or "2d_view_active")
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
        if curVehicle ~= nil then
            self:updateVehicleRotationState(curVehicle, vehCam, vehCam.rotY, nil)
        end

        self:updateVehicleCameraTrackingNode(vehCam)
        self:completeSnapTurn(t, "In-cab", direction, angle, "rotY", vehCam.rotY)
        return true, vehCam.rotY
    elseif vehCam then
        return self:snapTurnOutsideCamera(vehCam, direction, angleOverride, t)
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

            self:completeSnapTurn(t, "On-foot", direction, angle, "yawNode", newYaw)
            return true, newYaw
        end
    end

    return false, "no_active_camera"
end

function VRCompanion:snapTurnOutsideCamera(cam, direction, angleOverride, t)
    if not cam.isRotatable then
        return false, "fixed_outside_camera"
    end
    local angle = angleOverride or self:getSnapTurnAngle()
    cam.rotY = self:normalizeAngle((cam.rotY or 0) + direction * angle)
    self:completeSnapTurn(t, "Outside camera", direction, angle, "orbit rotY", cam.rotY)
    return true, cam.rotY
end

function VRCompanion:completeSnapTurn(t, viewLabel, direction, angle, yawLabel, newYaw)
    self.lastSnapTurnTime = t
    self:signalDiscontinuityFlush()
    print(string.format("[FS25_VR] %s snap turn %s (%.1f deg): %s = %.3f",
        viewLabel, direction < 0 and "left" or "right", math.deg(angle), yawLabel, newYaw))
end

function VRCompanion:snapTurnLeft(angleOverride, curTime)
    return self:snapTurn(-1, angleOverride, curTime)
end

function VRCompanion:snapTurnRight(angleOverride, curTime)
    return self:snapTurn(1, angleOverride, curTime)
end

function VRCompanion:getCabRestPose(cam, camParent, targetHeadRotY, lookYaw, defaultRotY)
    local localToLocalFn = localToLocal or (_G and _G.localToLocal)
    local getTranslationFn = getTranslation or (_G and _G.getTranslation)

    local rest = self.cabRestPose
    if rest == nil then
        rest = {}
        self.cabRestPose = rest
    end
    rest.node = nil
    rest.yaw = targetHeadRotY
    rest.qx, rest.qy, rest.qz, rest.qw = nil, nil, nil, nil

    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 and camParent ~= 0 then
        rest.node = cam.headTrackingNode
        rest.x, rest.y, rest.z = 0, 0, 0
        if type(localToLocalFn) == "function" then
            rest.x, rest.y, rest.z = localToLocalFn(cam.headTrackingNode, camParent, 0, 0, 0)
        elseif type(getTranslationFn) == "function" then
            local ok, gx, gy, gz = pcall(getTranslationFn, cam.headTrackingNode)
            if ok and gx ~= nil then rest.x, rest.y, rest.z = gx, gy, gz end
        end
        rest.qx, rest.qy, rest.qz, rest.qw = self:getRotationInSpace(cam.headTrackingNode, camParent)
    else
        rest.x = cam.origTransX or cam.transX or 0
        rest.y = cam.origTransY or cam.transY or 0
        rest.z = cam.origTransZ or cam.transZ or 0
        rest.yaw = self:normalizeAngle((cam.origRotY or defaultRotY or 0) + (lookYaw or 0))
    end
    rest.seatX, rest.seatY, rest.seatZ = self:getCabSeatOffset(cam, camParent)
    rest.x, rest.y, rest.z = rest.x + rest.seatX, rest.y + rest.seatY, rest.z + rest.seatZ
    if rest.qw == nil then
        rest.qx, rest.qy, rest.qz, rest.qw = self:yawQuaternion(rest.yaw)
    end
    return rest
end

function VRCompanion:cabPointFromHead(rest, camParent, x, y, z)
    local localToLocalFn = localToLocal or (_G and _G.localToLocal)
    if rest.node ~= nil and type(localToLocalFn) == "function" then
        local cx, cy, cz = localToLocalFn(rest.node, camParent, x, y, z)
        return cx + rest.seatX, cy + rest.seatY, cz + rest.seatZ
    end
    local rx, ry, rz = self:rotateVectorByYaw(x, y, z, rest.yaw)
    return rest.x + rx, rest.y + ry, rest.z + rz
end

function VRCompanion:realignVehicleCameraToSeat(cam)
    if not cam or not cam.cameraNode then
        return
    end

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
    if curVehicle ~= nil then
        self:updateVehicleRotationState(curVehicle, cam, cam.rotY, cam.rotX)
    end

    local getParentFn = getParent or (_G and _G.getParent)
    local setRotationFn = setRotation or (_G and _G.setRotation)
    local setTranslationFn = setTranslation or (_G and _G.setTranslation)

    local camParent = 0
    if type(getParentFn) == "function" then
        camParent = getParentFn(cam.cameraNode)
    end

    local lookYaw = self:getVehicleLookYaw(cam)
    local targetHeadRotY = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)

    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        self:recordHeadTrackingForward(cam)

        targetHeadRotY = self:normalizeAngle((cam.origHeadTrackingRotY or cam.origRotY or math.pi) + lookYaw)

        if type(setRotationFn) == "function" then
            pcall(setRotationFn, cam.headTrackingNode, cam.origHeadTrackingRotX or 0, targetHeadRotY, cam.origHeadTrackingRotZ or 0)
        end
    end

    local rest = self:getCabRestPose(cam, camParent, targetHeadRotY, lookYaw, math.pi)
    local cabTransX, cabTransY, cabTransZ = rest.x, rest.y, rest.z
    local cabQx, cabQy, cabQz, cabQw = rest.qx, rest.qy, rest.qz, rest.qw

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
    self:setRealignedRotation(cam.cameraNode, cabQx, cabQy, cabQz, cabQw)
end

function VRCompanion:setRealignedRotation(camNode, qx, qy, qz, qw)
    self:setNodeQuaternion(camNode, qx, qy, qz, qw)
    if self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0 then
        self:setNodeQuaternion(self.cameraNodeLeft, qx, qy, qz, qw)
    end
    if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
        self:setNodeQuaternion(self.cameraNodeRight, qx, qy, qz, qw)
    end
    if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
        self:setNodeQuaternion(self.cyclopeanAudioListenerNode, qx, qy, qz, qw)
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
    local finalQx, finalQy, finalQz, finalQw = nil, nil, nil, nil

    if cam.vrHeadTrackingNode ~= nil and cam.vrHeadTrackingNode ~= 0 and camParent ~= 0 then
        if type(localToLocal) == "function" then
            finalTx, finalTy, finalTz = localToLocal(cam.vrHeadTrackingNode, camParent, 0, 0, 0)
        end
        finalQx, finalQy, finalQz, finalQw = self:getRotationInSpace(cam.vrHeadTrackingNode, camParent)
    else
        local base = self:getPlayerBaseTranslation(cam)
        finalTx = base.x or base[1] or 0
        finalTy = base.y or base[2] or 0
        finalTz = base.z or base[3] or 0
    end
    if finalQw == nil then
        finalQx, finalQy, finalQz, finalQw = self:yawQuaternion(math.pi)
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
    self:setRealignedRotation(activeCamNode, finalQx, finalQy, finalQz, finalQw)
end

function VRCompanion:resetOutsideCameraOrbit(cam)
    self:recordOutsideCameraDefaults(cam)
    cam.rotY = cam.origRotY
    cam.rotX = cam.origRotX
    print(string.format("[FS25_VR] Outside camera orbit reset to default: rotX = %.3f, rotY = %.3f", cam.rotX, cam.rotY))
end

function VRCompanion:onRecenterAction()
    self:recenterView(false)
end

function VRCompanion:recenterView(alwaysRecenterTracking)
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
    local recenterTracking = alwaysRecenterTracking or (self.recenterVrTrackingSpace ~= false)
    if recenterTracking then
        if bridge ~= nil and type(bridge.recenter) == "function" then
            bridge.recenter()
        end
        self:resetMotionControllerMovement()
        if self:isMenuQuadShown() then
            print("[FS25_VR] Menu Quad layer realigned directly in front of head pose.")
        end
    else
        print("[FS25_VR] VehicleOnly mode: OpenXR tracking space recenter bypassed.")
    end

    local vehCam = self:getActiveVehicleCamera()
    if vehCam and vehCam.isInside then
        if recenterTracking then
            self:realignVehicleCameraToSeat(vehCam)
            print("[FS25_VR] Cockpit head pose realigned to driver seat reference origin.")
        else
            if vehCam.origRotY ~= nil then
                vehCam.rotY = vehCam.origRotY
            end
            local curVehicle = self:getControlledVehicle()
            if curVehicle ~= nil then
                self:updateVehicleRotationState(curVehicle, vehCam, vehCam.rotY, nil)
            end
            self:updateVehicleCameraTrackingNode(vehCam)
            print("[FS25_VR] VehicleOnly mode: Cockpit vehicle camera look yaw reset to origRotY.")
        end
    elseif vehCam and vehCam.isRotatable then
        self:resetOutsideCameraOrbit(vehCam)
    elseif vehCam == nil and recenterTracking then
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

VRCompanion:registerSubsystem("locomotion", {
    mapUnload = function(self)
        self.vehicleRotationStates = setmetatable({}, WEAK_KEYS_MT)
        self:resetSnapTurnDebounce()
    end,
})