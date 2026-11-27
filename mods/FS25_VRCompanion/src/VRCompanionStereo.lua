VRCompanion = VRCompanion or {}

VRCompanion.RENDERING_MODE_AFR = "AFR"
VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS = "SequentialDualPass"
VRCompanion.DEFAULT_RENDERING_MODE = VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
if VRCompanion.renderingMode == nil then
    VRCompanion.renderingMode = VRCompanion.DEFAULT_RENDERING_MODE
end

VRCompanion.RENDERING_MODE_DUAL_PASS = VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
if VRCompanion.headTrackingEnabled == nil then
    VRCompanion.headTrackingEnabled = true
end
VRCompanion.subPassIndex = VRCompanion.subPassIndex or 0
VRCompanion.accumulatedPass1Dt = VRCompanion.accumulatedPass1Dt or 0.0

VRCompanion.DEFAULT_TOGGLE_VIEW_DEBOUNCE_MS = 250
if VRCompanion.toggleViewDebounceMs == nil then
    VRCompanion.toggleViewDebounceMs = VRCompanion.DEFAULT_TOGGLE_VIEW_DEBOUNCE_MS
end
VRCompanion.lastToggleViewTime = nil
if VRCompanion.is2DViewActive == nil then
    VRCompanion.is2DViewActive = false
end

VRCompanion.cameraNodeLeft = VRCompanion.cameraNodeLeft or nil
VRCompanion.cameraNodeRight = VRCompanion.cameraNodeRight or nil
VRCompanion.cyclopeanAudioListenerNode = VRCompanion.cyclopeanAudioListenerNode or nil
VRCompanion.origCamSettings = VRCompanion.origCamSettings or {}
VRCompanion.menuCameraOffsets = VRCompanion.menuCameraOffsets or {}

local function rotateVectorByQuaternion(qx, qy, qz, qw, vx, vy, vz)
    local tx = 2 * (qy * vz - qz * vy)
    local ty = 2 * (qz * vx - qx * vz)
    local tz = 2 * (qx * vy - qy * vx)
    local rx = vx + qw * tx + (qy * tz - qz * ty)
    local ry = vy + qw * ty + (qz * tx - qx * tz)
    local rz = vz + qw * tz + (qx * ty - qy * tx)
    return rx, ry, rz
end

local function multiplyQuaternions(ax, ay, az, aw, bx, by, bz, bw)
    return aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz
end

local function headPoseQuaternion(pitch, yaw, roll)
    local cp, sp = math.cos(pitch * 0.5), math.sin(pitch * 0.5)
    local cy, sy = math.cos(yaw * 0.5), math.sin(yaw * 0.5)
    local cr, sr = math.cos(roll * 0.5), math.sin(roll * 0.5)
    return multiplyQuaternions(cy * sp, sy * cp, -sy * sp, cy * cp, 0, 0, sr, cr)
end

local function headPoseEuler(qx, qy, qz, qw)
    local sinPitch = math.max(-1, math.min(1, 2 * (qw * qx - qy * qz)))
    return math.asin(sinPitch),
        math.atan2(2 * (qw * qy + qx * qz), 1 - 2 * (qx * qx + qy * qy)),
        math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qx * qx + qz * qz))
end

function VRCompanion:yawQuaternion(yaw)
    return 0, math.sin(yaw * 0.5), 0, math.cos(yaw * 0.5)
end

function VRCompanion:getRotationInSpace(node, spaceNode)
    local getWorldQuaternionFn = getWorldQuaternion or (_G and _G.getWorldQuaternion)
    if type(getWorldQuaternionFn) ~= "function" then return nil end
    local nx, ny, nz, nw = getWorldQuaternionFn(node)
    if nw == nil then return nil end
    if spaceNode == nil or spaceNode == 0 then return nx, ny, nz, nw end
    local sx, sy, sz, sw = getWorldQuaternionFn(spaceNode)
    if sw == nil then return nil end
    return multiplyQuaternions(-sx, -sy, -sz, sw, nx, ny, nz, nw)
end

function VRCompanion:setNodeQuaternion(node, qx, qy, qz, qw)
    local setQuaternionFn = setQuaternion or (_G and _G.setQuaternion)
    if type(setQuaternionFn) == "function" then
        setQuaternionFn(node, qx, qy, qz, qw)
        return
    end
    local setRotationFn = setRotation or (_G and _G.setRotation)
    if type(setRotationFn) == "function" then
        setRotationFn(node, headPoseEuler(qx, qy, qz, qw))
    end
end

function VRCompanion:composeHeadRotation(pose, bx, by, bz, bw)
    local hx, hy, hz, hw = headPoseQuaternion(pose.pitch or 0, pose.yaw or 0, pose.roll or 0)
    return multiplyQuaternions(bx, by, bz, bw, hx, hy, hz, hw)
end

function VRCompanion:getRenderingMode()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil then
        if type(bridge.getRenderingMode) == "function" then
            local mode = bridge.getRenderingMode()
            if mode == "SequentialDualPass" or mode == "DualPass" or mode == 1 then
                return self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS
            elseif mode == "AFR" or mode == 0 then
                return self.RENDERING_MODE_AFR
            end
        elseif type(bridge.getFrameParity) == "function" and not self.renderingModeExplicit then
            return self.RENDERING_MODE_AFR
        end
    end
    return self.renderingMode or self.DEFAULT_RENDERING_MODE
end

function VRCompanion:setRenderingMode(modeStr, saveConfig)
    local targetMode = self.DEFAULT_RENDERING_MODE
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
    self.renderingModeExplicit = true

    if VRBridge ~= nil and type(VRBridge.setRenderingMode) == "function" then
        VRBridge.setRenderingMode(targetMode)
    end

    if oldMode ~= targetMode then
        self:signalDiscontinuityFlush()
        self.accumulatedPass1Dt = 0.0
    end

    print(string.format("[FS25_VR] Active VR Rendering Mode: %s", targetMode))

    if saveConfig == true then
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

function VRCompanion:getDualPassEye()
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.getActiveEye) == "function" then
        return (bridge.getActiveEye() % 2 == 1) and 1 or 0
    end
    return (self:getSubPassIndex() % 2 == 1) and 1 or 0
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

    local baseNode = nil
    if cam ~= nil then
        baseNode = (cam.firstPersonCamera ~= nil) and cam.firstPersonCamera or cam.cameraNode
    end
    local camLeft, camRight = self:ensureDualEyeCameraNodes(baseNode)
    if camLeft == nil then
        camLeft, camRight = 2001, 2001
        self.cameraNodeLeft, self.cameraNodeRight = camLeft, camRight
    end
    return camLeft, camRight
end

function VRCompanion:isDualEyeCameraNode(node)
    if node == nil or node == 0 then return false end
    return node == self.cameraNodeLeft or node == self.cameraNodeRight
        or (self.registeredEyeCameras ~= nil and self.registeredEyeCameras[node] == true)
end

function VRCompanion:getManagerActiveCamera(camMgr)
    if camMgr == nil or type(camMgr.getActiveCamera) ~= "function" then return nil end
    local ok, node = pcall(camMgr.getActiveCamera, camMgr)
    if ok and node ~= nil and node ~= 0 then return node end
    return nil
end

function VRCompanion:registerDualEyeCameraNode(camMgr, node)
    if self.registeredEyeCameras == nil then self.registeredEyeCameras = {} end
    if self.registeredEyeCameras[node] or type(camMgr.addCamera) ~= "function" then return end
    if pcall(camMgr.addCamera, camMgr, node, nil, false) then
        self.registeredEyeCameras[node] = true
    end
end

function VRCompanion:unregisterDualEyeCameraNodes()
    if self.registeredEyeCameras == nil then return end
    local camMgr = g_cameraManager or (_G and _G.g_cameraManager)
    local entityExistsFn = entityExists or (_G and _G.entityExists)
    for registered in pairs(self.registeredEyeCameras) do
        local exists = type(entityExistsFn) ~= "function" or entityExistsFn(registered)
        if exists and camMgr ~= nil and type(camMgr.removeCamera) == "function" then
            pcall(camMgr.removeCamera, camMgr, registered)
        end
    end
    self.registeredEyeCameras = nil
end

function VRCompanion:getGameCamera()
    local active = self:getManagerActiveCamera(g_cameraManager or (_G and _G.g_cameraManager))
    if self:isDualEyeCameraNode(active) then
        return self.gameCameraNode
    end
    return active
end

function VRCompanion:bindRenderingCamera(targetCam)
    if targetCam == nil then return end
    if self.deferRenderingCameraBind then return end
    local camMgr = g_cameraManager or (_G and _G.g_cameraManager)
    if camMgr ~= nil and type(camMgr.setActiveCamera) == "function" then
        local active = self:getManagerActiveCamera(camMgr)
        if self:isDualEyeCameraNode(targetCam) then
            if active ~= nil and not self:isDualEyeCameraNode(active) then
                self.gameCameraNode = active
            end
            self:registerDualEyeCameraNode(camMgr, targetCam)
        end
        if active ~= targetCam then
            pcall(camMgr.setActiveCamera, camMgr, targetCam)
        end
    else
        local setCameraFn = setCamera or (_G and _G.setCamera)
        if type(setCameraFn) == "function" then
            pcall(setCameraFn, targetCam)
        end
    end
    self.activeRenderingCamera = targetCam
    if _G then
        _G.activeCamera = targetCam
    end
end

function VRCompanion:isGameShowingCamera(camNode, altNode)
    local active = self:getGameCamera()
    if active == nil then return true end
    local shown = (active == camNode) or (altNode ~= nil and active == altNode)
    if shown ~= self.lastGameShowingTrackedCamera then
        self.lastGameShowingTrackedCamera = shown
        if not shown then
            print(string.format("[FS25_VR] The game is showing camera %s; leaving rendering to the game.", tostring(active)))
        end
    end
    return shown
end

function VRCompanion:syncActiveEyeCamera(subPass)
    local curVeh = self:getControlledVehicle()
    if curVeh ~= nil then
        local cam = self:getVehicleActiveCamera(curVeh)
        if cam ~= nil then
            self:onVehicleCameraUpdate(cam, 0.0)
            return
        end
    end

    local playerCam = self:getPlayerCamera()
    if playerCam ~= nil then
        self:onPlayerCameraUpdate(playerCam, 0.0)
    end
end

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
        if self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0 then
            self.cameraNodeLeft = self.cameraNodeRight
        elseif type(createCameraFn) == "function" then
            self.cameraNodeLeft = createCameraFn("vrEyeCamera", fovY, nearClip, farClip)
        elseif type(createTransformGroupFn) == "function" then
            self.cameraNodeLeft = createTransformGroupFn("vrEyeCamera")
        end
    end
    if self.cameraNodeRight == nil or self.cameraNodeRight == 0 then
        self.cameraNodeRight = self.cameraNodeLeft
    end

    return self.cameraNodeLeft, self.cameraNodeRight
end

function VRCompanion:ensureDualEyeCameraNodes(referenceCamNode)
    local entityExistsFn = entityExists or (_G and _G.entityExists)
    if type(entityExistsFn) == "function" and self.cameraNodeLeft ~= nil and self.cameraNodeLeft ~= 0
        and not entityExistsFn(self.cameraNodeLeft) then
        if self.cameraNodeRight == self.cameraNodeLeft then
            self.cameraNodeRight = nil
        end
        self.cameraNodeLeft = nil
    end
    if type(entityExistsFn) == "function" and self.cameraNodeRight ~= nil and self.cameraNodeRight ~= 0
        and not entityExistsFn(self.cameraNodeRight) then
        self.cameraNodeRight = nil
    end
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
    local entityExistsFn = entityExists or (_G and _G.entityExists)
    if type(entityExistsFn) == "function" and self.cyclopeanAudioListenerNode ~= nil
        and self.cyclopeanAudioListenerNode ~= 0 and not entityExistsFn(self.cyclopeanAudioListenerNode) then
        self.cyclopeanAudioListenerNode = nil
    end
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

local function store2DViewActive(self, newActive)
    local wasActive = (self.is2DViewActive == true)
    self.is2DViewActive = newActive

    if VRBridge ~= nil and type(VRBridge.set2DViewActive) == "function" then
        VRBridge.set2DViewActive(newActive)
    end

    if wasActive == newActive then return false end
    self:signalDiscontinuityFlush()
    return true
end

function VRCompanion:leave2DViewOnMapUnload()
    if store2DViewActive(self, false) then
        print("[FS25_VR] Left 2D Floating Window Mode for the map unload.")
    end
end

function VRCompanion:set2DViewActive(active)
    local newActive = (active == true)
    if store2DViewActive(self, newActive) then
        if newActive then
            print("[FS25_VR] Entered 2D Floating Window Mode.")
            self:restoreMenuBackgroundCamera()
            self:restoreCameraProjectionsFor2D()
            if self.activePlayerCamera ~= nil and self:getActiveVehicleCamera() == nil then
                self:realignPlayerCameraToRoot(self.activePlayerCamera)
            end
            self:restoreHudSafeArea(true)
        else
            print("[FS25_VR] Exited 2D Floating Window Mode; returning to 3D VR mode.")
            self:applyHudSafeArea()
            self:applyHudState()
        end
    end
end

function VRCompanion:toggle2DView()
    self:set2DViewActive(not self.is2DViewActive)
    return self.is2DViewActive
end

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
        if cam ~= nil and cam.isInside and cam.cameraNode ~= nil then
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

function VRCompanion:beginFrameChecks()
    local depth = self.frameCheckDepth or 0
    if depth == 0 then
        self:clearFrameChecks()
    end
    self.frameCheckDepth = depth + 1
end

function VRCompanion:endFrameChecks()
    local depth = (self.frameCheckDepth or 0) - 1
    if depth <= 0 then
        depth = 0
        self:clearFrameChecks()
    end
    self.frameCheckDepth = depth
end

function VRCompanion:clearFrameChecks()
    self.cachedPassThroughMode = nil
    self.cachedHeadTrackingActive = nil
end

function VRCompanion:resetFrameChecks()
    self.frameCheckDepth = 0
    self:clearFrameChecks()
end

function VRCompanion:resolvePassThroughMode()
    if self.passThroughMode ~= nil then
        return self.passThroughMode == true
    end
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge == nil then
        return true
    end
    if type(bridge.isReady) == "function" then
        local ok, ready = pcall(bridge.isReady)
        if ok and not ready then
            return true
        end
    end
    if self.handshakeComplete == false and not (bridge and bridge.isCompanionActive) then
        return true
    end
    return false
end

function VRCompanion:isPassThroughMode()
    if (self.frameCheckDepth or 0) == 0 then
        return self:resolvePassThroughMode()
    end
    local cached = self.cachedPassThroughMode
    if cached == nil then
        cached = self:resolvePassThroughMode()
        self.cachedPassThroughMode = cached
    end
    return cached
end

function VRCompanion:setPassThroughMode(enabled)
    self.passThroughMode = (enabled == true)
    self.cachedPassThroughMode = nil
end

function VRCompanion:resolveHeadTrackingActive()
    if self.headTrackingEnabled ~= nil and not self.headTrackingEnabled then
        return false
    end
    local gs = g_gameSettings or _G.g_gameSettings
    local gsClass = GameSettings or _G.GameSettings
    if gs ~= nil and type(gs.getValue) == "function" and gsClass ~= nil and gsClass.SETTING ~= nil and gsClass.SETTING.IS_HEAD_TRACKING_ENABLED ~= nil then
        local ok, enabled = pcall(gs.getValue, gs, gsClass.SETTING.IS_HEAD_TRACKING_ENABLED)
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

function VRCompanion:isHeadTrackingActive()
    if (self.frameCheckDepth or 0) == 0 then
        return self:resolveHeadTrackingActive()
    end
    local cached = self.cachedHeadTrackingActive
    if cached == nil then
        cached = self:resolveHeadTrackingActive()
        self.cachedHeadTrackingActive = cached
    end
    return cached
end

function VRCompanion:setHeadTrackingEnabled(enabled)
    self.headTrackingEnabled = (enabled == true)
    self.cachedHeadTrackingActive = nil
end

function VRCompanion:fetchEyeProjection(eyeIndex)
    if self.is2DViewActive then
        return false
    end
    eyeIndex = eyeIndex or 0
    local cache = self.eyeProjectionCache
    if cache ~= nil and cache.active and cache.eye == eyeIndex then
        return cache.proj
    end

    local proj = false
    if VRBridge and type(VRBridge.getEyeProjection) == "function" then
        local scratch = self.eyeProjectionScratch
        if scratch == nil then
            scratch = {}
            self.eyeProjectionScratch = scratch
        end
        local target = scratch[eyeIndex]
        if target == nil then
            target = {}
            scratch[eyeIndex] = target
        end
        local fetched = VRBridge.getEyeProjection(eyeIndex, target)
        if type(fetched) == "table" then scratch[eyeIndex] = fetched end
        if fetched and fetched.isValid then
            proj = fetched
            if type(fetched.fovX) == "number" then self.renderedFovX = fetched.fovX end
        end
    end
    if cache ~= nil and cache.active and proj then
        cache.eye = eyeIndex
        cache.proj = proj
    end
    return proj
end

function VRCompanion:beginEyeProjectionScope()
    local cache = self.eyeProjectionCache
    if cache == nil then
        cache = {}
        self.eyeProjectionCache = cache
    end
    cache.active = true
    cache.eye = nil
    cache.proj = nil
end

function VRCompanion:endEyeProjectionScope()
    local cache = self.eyeProjectionCache
    if cache ~= nil then
        cache.active = false
        cache.eye = nil
        cache.proj = nil
    end
end

function VRCompanion:applyCameraProjection(camNode, eyeIndex, proj)
    if camNode == nil or camNode == 0 then
        return
    end

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

    if proj == nil then
        proj = self:fetchEyeProjection(eyeIndex or 0)
    end
    if not proj then
        return
    end

    if type(setFovY) == "function" and proj.fovY ~= nil and proj.fovY > 0 then
        setFovY(camNode, proj.fovY)
    end

    if type(setProjectionOffset) == "function" then
        setProjectionOffset(camNode, 0, 0)
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
    if not afrFallback or self:isMenuQuadShown() then
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
    local bridge = VRBridge or (_G and _G.VRBridge)
    if bridge ~= nil and type(bridge.getHeadPose) == "function" then
        local scratch = self.headPoseScratch
        if scratch == nil then
            scratch = {}
            self.headPoseScratch = scratch
        end
        pose = bridge.getHeadPose(scratch)
        if type(pose) == "table" then
            self.headPoseScratch = pose
        end
    end

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
            pose = self.trackIrPoseScratch
            if pose == nil then
                pose = {}
                self.trackIrPoseScratch = pose
            end
            pose.x = tx or 0
            pose.y = ty or 0
            pose.z = tz or 0
            pose.pitch = pitch or 0
            pose.yaw = yaw or 0
            pose.roll = roll or 0
            pose.isValid = true
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
    if not self:isMenuQuadShown() and not self.is2DViewActive then
        if self:getRenderingMode() == self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then
            return self:getDualPassEye()
        end
        if afrFallback then
            local parity = self:getFrameParity()
            return (parity % 2 == 1) and 1 or 0
        end
    end
    return 0
end

local function copyLensSetting(getFn, setFn, eyeNode, referenceCamNode)
    if type(getFn) ~= "function" or type(setFn) ~= "function" then return end
    local ok, value = pcall(getFn, referenceCamNode)
    if not ok or value == nil then return end
    local okEye, current = pcall(getFn, eyeNode)
    if okEye and current ~= value then
        pcall(setFn, eyeNode, value)
    end
end

function VRCompanion:matchEngineCameraLens(eyeNode, referenceCamNode)
    if referenceCamNode == nil or referenceCamNode == 0 or eyeNode == referenceCamNode then return end
    copyLensSetting(getFovY or (_G and _G.getFovY), setFovY or (_G and _G.setFovY), eyeNode, referenceCamNode)
    copyLensSetting(getNearClip or (_G and _G.getNearClip), setNearClip or (_G and _G.setNearClip), eyeNode, referenceCamNode)
    copyLensSetting(getFarClip or (_G and _G.getFarClip), setFarClip or (_G and _G.setFarClip), eyeNode, referenceCamNode)
end

function VRCompanion:placeAndBindEyes(referenceCamNode, parentNode,
        headX, headY, headZ, eyeX, eyeY, eyeZ, qx, qy, qz, qw, eyeIndex, eyeProj, stereoRequested)
    local getParentFn = getParent or (_G and _G.getParent)
    local linkFn = link or (_G and _G.link)
    local setTranslationFn = setTranslation or (_G and _G.setTranslation)

    self:ensureDualEyeCameraNodes(referenceCamNode)
    self:ensureCyclopeanAudioListener(referenceCamNode)

    if parentNode ~= nil and parentNode ~= 0 and type(linkFn) == "function" and type(getParentFn) == "function" then
        if self.cameraNodeLeft ~= nil and getParentFn(self.cameraNodeLeft) ~= parentNode then
            pcall(linkFn, parentNode, self.cameraNodeLeft)
        end
        if self.cameraNodeRight ~= nil and getParentFn(self.cameraNodeRight) ~= parentNode then
            pcall(linkFn, parentNode, self.cameraNodeRight)
        end
        if self.cyclopeanAudioListenerNode ~= nil and getParentFn(self.cyclopeanAudioListenerNode) ~= parentNode then
            pcall(linkFn, parentNode, self.cyclopeanAudioListenerNode)
        end
    end

    if self.cyclopeanAudioListenerNode ~= nil and self.cyclopeanAudioListenerNode ~= 0 then
        if type(setTranslationFn) == "function" then
            setTranslationFn(self.cyclopeanAudioListenerNode, headX, headY, headZ)
        end
        self:setNodeQuaternion(self.cyclopeanAudioListenerNode, qx, qy, qz, qw)
        local setAudioListenerFn = setAudioListener or (_G and _G.setAudioListener)
        if type(setAudioListenerFn) == "function" then
            setAudioListenerFn(self.cyclopeanAudioListenerNode)
        end
        self:placeVirtualHandsForRender()
        self:placePointerRayForRender()
    end

    local isStereoActive = stereoRequested and not self:isMenuQuadShown() and not self.is2DViewActive

    if isStereoActive then
        local isRightEye = (eyeIndex == 1)
        local activeEyeCam = isRightEye and self.cameraNodeRight or self.cameraNodeLeft

        if activeEyeCam ~= nil and activeEyeCam ~= 0 then
            self:matchEngineCameraLens(activeEyeCam, referenceCamNode)
            self:applyCameraProjection(activeEyeCam, eyeIndex, eyeProj)
            if type(setTranslationFn) == "function" then
                setTranslationFn(activeEyeCam, eyeX, eyeY, eyeZ)
            end
            self:setNodeQuaternion(activeEyeCam, qx, qy, qz, qw)
            self:bindRenderingCamera(activeEyeCam)
            self:reportAfrEyeCamera(eyeIndex, activeEyeCam)
            if self:getRenderingMode() ~= self.RENDERING_MODE_SEQUENTIAL_DUAL_PASS then
                self:traceEyeFrame(eyeIndex, activeEyeCam, nil)
            end
        end
    else
        local targetNode = self.cameraNodeLeft or referenceCamNode
        if targetNode ~= nil and targetNode ~= 0 then
            local leftProj = nil
            if eyeIndex == 0 then leftProj = eyeProj end
            self:matchEngineCameraLens(targetNode, referenceCamNode)
            self:applyCameraProjection(targetNode, 0, leftProj)
            if type(setTranslationFn) == "function" then
                setTranslationFn(targetNode, eyeX, eyeY, eyeZ)
            end
            self:setNodeQuaternion(targetNode, qx, qy, qz, qw)
            self:bindRenderingCamera(targetNode)
        end
    end
end

VRCompanion.OUTSIDE_CAMERA_VERTICAL_BLEND = 0.1

function VRCompanion:getOutsideCameraBaseHeading(camNode, localDirectionToWorldFn)
    local lx, ly, lz = localDirectionToWorldFn(camNode, 0, 0, -1)
    if lx == nil or ly == nil or lz == nil then return nil end
    local fx, fz = lx, lz
    local horizontal = math.sqrt(lx * lx + lz * lz)
    local blendBelow = self.OUTSIDE_CAMERA_VERTICAL_BLEND
    if horizontal < blendBelow then
        local ux, _, uz = localDirectionToWorldFn(camNode, 0, 1, 0)
        if ux ~= nil and uz ~= nil then
            local weight = 1 - horizontal / blendBelow
            if ly > 0 then weight = -weight end
            fx = fx + weight * ux
            fz = fz + weight * uz
        end
    end
    if fx * fx + fz * fz < 1e-12 then return nil end
    return math.atan2(-fx, -fz)
end

function VRCompanion:renderThroughEngineCamera(camNode)
    if self.activeRenderingCamera ~= nil and self.activeRenderingCamera ~= camNode then
        self:bindRenderingCamera(camNode)
    end
end

function VRCompanion:showEngineCameraView(camNode, eyeIndex, eyeProj, stereoRequested)
    local getRootNodeFn = getRootNode or (_G and _G.getRootNode)
    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    if type(getRootNodeFn) ~= "function" or type(getWorldTranslationFn) ~= "function" then
        self:renderThroughEngineCamera(camNode)
        return
    end
    local rootNode = getRootNodeFn()
    local qx, qy, qz, qw = self:getRotationInSpace(camNode, nil)
    if rootNode == nil or rootNode == 0 or qw == nil then
        self:renderThroughEngineCamera(camNode)
        return
    end
    local x, y, z = getWorldTranslationFn(camNode)
    self:placeAndBindEyes(camNode, rootNode, x, y, z, x, y, z, qx, qy, qz, qw, eyeIndex, eyeProj, stereoRequested)
end

function VRCompanion:restoreGameActiveCamera()
    local gameCamera = self:getGameCamera()
    if gameCamera ~= nil then
        self:renderThroughEngineCamera(gameCamera)
    end
    self:unregisterDualEyeCameraNodes()
    self.gameCameraNode = nil
end

function VRCompanion:onOutsideCameraUpdate(cam)
    local camNode = cam.cameraNode
    if self.is2DViewActive or self:isMenuQuadShown() then
        self:renderThroughEngineCamera(camNode)
        return
    end

    local getRootNodeFn = getRootNode or (_G and _G.getRootNode)
    local getWorldTranslationFn = getWorldTranslation or (_G and _G.getWorldTranslation)
    local localDirectionToWorldFn = localDirectionToWorld or (_G and _G.localDirectionToWorld)
    if type(getRootNodeFn) ~= "function" or type(getWorldTranslationFn) ~= "function"
        or type(localDirectionToWorldFn) ~= "function" then
        self:renderThroughEngineCamera(camNode)
        return
    end
    local rootNode = getRootNodeFn()
    self:ensureDualEyeCameraNodes(camNode)
    if rootNode == nil or rootNode == 0 or self.cameraNodeLeft == nil or self.cameraNodeLeft == 0 then
        self:renderThroughEngineCamera(camNode)
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = self:getActiveEyeIndex(afrFallback)
    local eyeProj = self:fetchEyeProjection(eyeIndex)

    local baseX, baseY, baseZ = getWorldTranslationFn(camNode)

    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then
        self:showEngineCameraView(camNode, eyeIndex, eyeProj, afrFallback)
        return
    end
    local heading = self:getOutsideCameraBaseHeading(camNode, localDirectionToWorldFn)
        or self.outsideCameraHeading or 0
    self.outsideCameraHeading = heading

    local tx, ty, tz = pose.x or 0, pose.y or 0, pose.z or 0
    local offX, offY, offZ = self:computeEyeOffset(pose, eyeIndex, afrFallback)

    local baseNode = self:placeOutsideCameraBase(rootNode, baseX, baseY, baseZ, heading)
    if baseNode == nil then
        self:showEngineCameraView(camNode, eyeIndex, eyeProj, afrFallback)
        return
    end
    local qx, qy, qz, qw = self:composeHeadRotation(pose, 0, 0, 0, 1)
    self:placeAndBindEyes(camNode, baseNode, tx, ty, tz, tx + offX, ty + offY, tz + offZ,
        qx, qy, qz, qw, eyeIndex, eyeProj, afrFallback)
end

function VRCompanion:placeOutsideCameraBase(rootNode, x, y, z, heading)
    local setTranslationFn = setTranslation or (_G and _G.setTranslation)
    local setRotationFn = setRotation or (_G and _G.setRotation)
    if type(setTranslationFn) ~= "function" or type(setRotationFn) ~= "function" then return nil end
    local entityExistsFn = entityExists or (_G and _G.entityExists)
    if self.outsideCameraBaseNode ~= nil and type(entityExistsFn) == "function"
        and not entityExistsFn(self.outsideCameraBaseNode) then
        self.outsideCameraBaseNode = nil
    end
    if self.outsideCameraBaseNode == nil then
        local createTransformGroupFn = createTransformGroup or (_G and _G.createTransformGroup)
        local linkFn = link or (_G and _G.link)
        if type(createTransformGroupFn) ~= "function" or type(linkFn) ~= "function" then return nil end
        local node = createTransformGroupFn("vrOutsideCameraBase")
        if node == nil or node == 0 then return nil end
        linkFn(rootNode, node)
        self.outsideCameraBaseNode = node
    end
    setTranslationFn(self.outsideCameraBaseNode, x, y, z)
    setRotationFn(self.outsideCameraBaseNode, 0, heading, 0)
    return self.outsideCameraBaseNode
end

function VRCompanion:onVehicleCameraUpdate(cam, dt)
    if not cam or cam.cameraNode == nil then
        return
    end
    self.trackedCameraShown = self:isGameShowingCamera(cam.cameraNode)
    if not self.trackedCameraShown then
        return
    end
    if not cam.isInside then
        self:onOutsideCameraUpdate(cam)
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = self:getActiveEyeIndex(afrFallback)

    local eyeProj = self:fetchEyeProjection(eyeIndex)
    self:applyCameraProjection(cam.cameraNode, eyeIndex, eyeProj)

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
    if curVeh ~= nil then
        local activeCam = self:getVehicleActiveCamera(curVeh)
        if activeCam == cam or (activeCam and cam and activeCam.cameraNode == cam.cameraNode) then
            self:updateVehicleRotationState(curVeh, cam, cam.rotY, nil)
        end
    end

    if self.is2DViewActive then
        self:renderThroughEngineCamera(cam.cameraNode)
        return
    end

    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then
        self:showEngineCameraView(cam.cameraNode, eyeIndex, eyeProj, afrFallback)
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
    local createTransformGroupFn = createTransformGroup or (_G and _G.createTransformGroup)
    local linkFn = link or (_G and _G.link)

    local camParent = 0
    if type(getParentFn) == "function" then
        camParent = getParentFn(cam.cameraNode)
    end

    local lookYaw = self:getVehicleLookYaw(cam) or 0

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

    local targetHeadRotY = self:normalizeAngle((cam.origRotY or math.pi) + lookYaw) or ((cam.origRotY or math.pi) + lookYaw)
    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        local nodeRotY = self:updateVehicleCameraTrackingNode(cam)
        if nodeRotY ~= nil then
            targetHeadRotY = nodeRotY
        end
    end

    local rest = self:getCabRestPose(cam, camParent, targetHeadRotY, lookYaw, 0)
    local baseQx, baseQy, baseQz, baseQw = rest.qx, rest.qy, rest.qz, rest.qw
    local cabTransX, cabTransY, cabTransZ = self:cabPointFromHead(rest, camParent, eyeTx, eyeTy, eyeTz)
    local midCabTransX, midCabTransY, midCabTransZ = self:cabPointFromHead(rest, camParent, tx, ty, tz)
    local cabQx, cabQy, cabQz, cabQw = self:composeHeadRotation(pose, baseQx, baseQy, baseQz, baseQw)

    self:placeAndBindEyes(cam.cameraNode, camParent,
        midCabTransX, midCabTransY, midCabTransZ, cabTransX, cabTransY, cabTransZ,
        cabQx, cabQy, cabQz, cabQw, eyeIndex, eyeProj, afrFallback)

    if type(setTranslationFn) == "function" then
        setTranslationFn(cam.cameraNode, cabTransX, cabTransY, cabTransZ)
    end
    self:setNodeQuaternion(cam.cameraNode, cabQx, cabQy, cabQz, cabQw)

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
    self.trackedCameraShown = self:isGameShowingCamera(activeCamNode, cam.cameraNode)
    if not self.trackedCameraShown then
        return
    end

    local afrFallback = self:requiresAfrFallback()
    local eyeIndex = self:getActiveEyeIndex(afrFallback)

    local eyeProj = self:fetchEyeProjection(eyeIndex)
    self:applyCameraProjection(activeCamNode, eyeIndex, eyeProj)

    if self.is2DViewActive then
        self:renderThroughEngineCamera(activeCamNode)
        return
    end

    local pose = self:getResolvedHeadPose()
    if not pose or not pose.isValid then
        self:showEngineCameraView(activeCamNode, eyeIndex, eyeProj, afrFallback)
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
    local finalMidTx, finalMidTy, finalMidTz = tx, ty, tz
    local baseQx, baseQy, baseQz, baseQw = nil, nil, nil, nil

    if cam.vrHeadTrackingNode ~= nil and cam.vrHeadTrackingNode ~= 0 and camParent ~= 0 then
        if type(localToLocal) == "function" then
            finalTx, finalTy, finalTz = localToLocal(cam.vrHeadTrackingNode, camParent, eyeTx, eyeTy, eyeTz)
            finalMidTx, finalMidTy, finalMidTz = localToLocal(cam.vrHeadTrackingNode, camParent, tx, ty, tz)
        end
        baseQx, baseQy, baseQz, baseQw = self:getRotationInSpace(cam.vrHeadTrackingNode, camParent)
    else
        local base = self:getPlayerBaseTranslation(cam) or {0, 0, 0}
        local bx = base.x or base[1] or 0
        local by = base.y or base[2] or 0
        local bz = base.z or base[3] or 0
        finalTx = bx + eyeTx
        finalTy = by + eyeTy
        finalTz = bz + eyeTz

        finalMidTx = bx + tx
        finalMidTy = by + ty
        finalMidTz = bz + tz
    end

    if baseQw == nil then
        baseQx, baseQy, baseQz, baseQw = self:yawQuaternion(math.pi)
    end
    local finalQx, finalQy, finalQz, finalQw = self:composeHeadRotation(pose, baseQx, baseQy, baseQz, baseQw)

    self:placeAndBindEyes(activeCamNode, camParent,
        finalMidTx, finalMidTy, finalMidTz, finalTx, finalTy, finalTz,
        finalQx, finalQy, finalQz, finalQw, eyeIndex, eyeProj, afrFallback)

    if type(setTranslation) == "function" then
        setTranslation(activeCamNode, finalTx, finalTy, finalTz)
    end
    self:setNodeQuaternion(activeCamNode, finalQx, finalQy, finalQz, finalQw)

    self.playerCamTicks = (self.playerCamTicks or 0) + 1
    if self.playerCamTicks == 1 or self.playerCamTicks % 300 == 0 then
        print(string.format("[FS25_VR] [PlayerCamera] Pose: (%.3f, %.3f, %.3f) Rot: (%.2f, %.2f, %.2f) Eye: %d AFR: %s",
            eyeTx, eyeTy, eyeTz, pitch, yaw, roll, eyeIndex, tostring(afrFallback)))
    end
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
        if type(cam.setRotation) == "function" then
            cam:setRotation(0, cameraYaw, 0)
        end
    end
end

function VRCompanion:updateMenuBackgroundCamera(dt)
    if not self:isMenuQuadShown() or self.is2DViewActive then
        return
    end

    local curVeh = self:getControlledVehicle()
    if curVeh ~= nil then
        local vehCam = self:getVehicleActiveCamera(curVeh)
        if vehCam ~= nil and not vehCam.isInside then
            return
        end
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
    if camNode == self.cameraNodeLeft or camNode == self.cameraNodeRight then
        return
    end

    if self.menuCameraOffsets == nil then
        self.menuCameraOffsets = {}
    end

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

    local lastOffset = self.menuCameraOffsets[camNode]
    if lastOffset == nil then
        lastOffset = { tx = 0, ty = 0, tz = 0, rx = 0, ry = 0, rz = 0 }
        self.menuCameraOffsets[camNode] = lastOffset
    end

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

    lastOffset.tx, lastOffset.ty, lastOffset.tz = tx, ty, tz
    lastOffset.rx, lastOffset.ry, lastOffset.rz = pitch, yaw, roll

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

local function deleteDualEyeNodes(self)
    local deleteFn = delete or (_G and _G.delete)
    local entityExistsFn = entityExists or (_G and _G.entityExists)
    local function deleteIfExists(node)
        if node == nil or node == 0 then return end
        if type(entityExistsFn) == "function" and not entityExistsFn(node) then return end
        pcall(deleteFn, node)
    end
    if type(deleteFn) == "function" then
        deleteIfExists(self.cameraNodeLeft)
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
end

local function restoreOriginalCameraSettings(self)
    if self.origCamSettings == nil then return end
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

VRCompanion:registerSubsystem("stereo", {
    mapLoad = function(self)
        self:initDualEyeCameraNodes()
        self:initCyclopeanAudioListener()
    end,
    mapUnload = function(self)
        deleteDualEyeNodes(self)
        self:restoreMenuBackgroundCamera()
        self:resetToggleViewDebounce()
        restoreOriginalCameraSettings(self)
        self:leave2DViewOnMapUnload()
        self.renderedFovX = nil
        self.outsideCameraHeading = nil
        self.lastGameShowingTrackedCamera = nil
        self.trackedCameraShown = nil
        self.gameCameraNode = nil
        self.invalidPoseTicks = 0
        self.vehCamTicks = 0
        self.playerCamTicks = 0
        self.menuCamTicks = 0
    end,
})