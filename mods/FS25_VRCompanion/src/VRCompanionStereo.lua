-- ====================================================================
-- FS25 VR Companion Mod: Stereo Subsystem
-- Manages stereoscopic camera projections, IPD eye offsetting, 6DOF head
-- pose resolution, dual-pass sub-pass sequencing, and 2D view toggling.
-- ====================================================================

VRCompanion = VRCompanion or {}
VRCompanion.Stereo = VRCompanion.Stereo or {}

VRCompanion.RENDERING_MODE_AFR = "AFR"
VRCompanion.RENDERING_MODE_SEQUENTIAL_DUAL_PASS = "SequentialDualPass"
-- Default Stereo Presentation Mode (the single source of truth for this default)
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

-- VR View Mode Toggle Configuration (Issue #41)
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
VRCompanion.activeRenderingCameraNode = VRCompanion.activeRenderingCameraNode or nil
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

-- The eye the current Sequential Dual-Pass sub-pass renders. The Injector alternates which eye
-- goes first between pairs, so the sub-pass index alone does not name the eye.
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
    -- During a Sequential Dual-Pass sub-pass the mission update owns the single bind
    -- of the eye's camera; camera updates inside the sub-pass only position nodes.
    if self.deferRenderingCameraBind then return end
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
            if self.activePlayerCamera ~= nil and type(self.realignPlayerCameraToRoot) == "function" then
                self:realignPlayerCameraToRoot(self.activePlayerCamera)
            end
            if type(self.restoreHudInsets) == "function" then
                self:restoreHudInsets(true)
            end
        else
            print("[FS25_VR] Exited 2D Floating Window Mode; returning to 3D VR mode.")
            if type(self.applyHudInsets) == "function" then
                self:applyHudInsets()
            end
            if type(self.applyHudState) == "function" then
                self:applyHudState()
            end
        end
    end
end

function VRCompanion:toggle2DView()
    self:set2DViewActive(not self.is2DViewActive)
    return self.is2DViewActive
end

function VRCompanion:isToggleViewDebounced(curTime)
    local t = curTime or (type(self.getCurrentTimeMs) == "function" and self:getCurrentTimeMs())
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
    t = t or (type(self.getCurrentTimeMs) == "function" and self:getCurrentTimeMs())

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

-- The pass-through and head-tracking checks run several times per frame. While a
-- frame scope is open (VRCompanion:update and the mission update hook open one),
-- each check resolves once and later callers in that scope read the cached answer.
-- Scopes nest: the game runs VRCompanion:update inside the hooked mission update,
-- and the inner scope shares the outer one's cache. Outside a scope the checks
-- resolve on every call. The outermost scope opens and closes with a clean cache,
-- so a change is picked up no later than the next update.
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

-- Closes every open scope at once (the outermost scope ending, however it ended).
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

-- Returns the valid eye projection from VRBridge, or false when none is available
-- (no bridge, 2D Floating Window Mode, or an invalid projection). While a sub-pass
-- scope is open (see beginEyeProjectionScope) the result for an eye is reused, so
-- one Sequential Dual-Pass sub-pass reads VRBridge once however many camera
-- updates it triggers.
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
        -- One reusable table per eye: VRBridge fills it in place instead of allocating, and the
        -- cached projection of one eye is not overwritten by a fetch for the other.
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
        -- VRBridge only fills a table in place once it holds every field; until then it returns its own
        -- table. Keep that one so later calls reuse it.
        if type(fetched) == "table" then scratch[eyeIndex] = fetched end
        if fetched and fetched.isValid then
            proj = fetched
        end
    end
    -- Only a valid projection is reused, so one that becomes valid mid sub-pass is picked up.
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

    -- A caller applying one eye's projection to several nodes fetches it once and passes it in.
    -- false means "already fetched, nothing usable"; nil means "fetch it now".
    if proj == nil then
        proj = self:fetchEyeProjection(eyeIndex or 0)
    end
    if not proj then
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

function VRCompanion:getResolvedHeadPose()
    local pose = nil
    local bridge = VRBridge or (_G and _G.VRBridge)
    -- Reuse one pose table across frames: VRBridge fills it in place instead of allocating.
    if bridge ~= nil and type(bridge.getHeadPose) == "function" then
        local scratch = self.headPoseScratch
        if scratch == nil then
            scratch = {}
            self.headPoseScratch = scratch
        end
        pose = bridge.getHeadPose(scratch)
        -- See fetchEyeProjection: adopt the table VRBridge returns so later reads fill it in place.
        if type(pose) == "table" then
            self.headPoseScratch = pose
        end
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
            -- A separate reusable table: writing TrackIR values (which carry no quaternion) into
            -- the VRBridge table would leave it incomplete, and VRBridge would then allocate a
            -- fresh one on every call instead of filling it in place.
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
    if not self.isMenuVisible and not self.is2DViewActive then
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

function VRCompanion:onVehicleCameraUpdate(cam, dt)
    -- Only apply 6DOF tracking and projection to interior in-cab cameras
    if not cam or not cam.isInside or cam.cameraNode == nil then
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
    local linkFn = link or (_G and _G.link)

    local camParent = 0
    if type(getParentFn) == "function" then
        camParent = getParentFn(cam.cameraNode)
    end

    -- Look yaw offset relative to neutral forward orientation (cam.rotY - cam.origRotY)
    local lookYaw = (type(self.getVehicleLookYaw) == "function" and self:getVehicleLookYaw(cam)) or 0

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
    local targetHeadRotY = (type(self.normalizeAngle) == "function" and self:normalizeAngle((cam.origRotY or math.pi) + lookYaw)) or ((cam.origRotY or math.pi) + lookYaw)
    if cam.headTrackingNode ~= nil and cam.headTrackingNode ~= 0 then
        if type(self.updateVehicleCameraTrackingNode) == "function" then
            local nodeRotY = self:updateVehicleCameraTrackingNode(cam)
            if nodeRotY ~= nil then
                targetHeadRotY = nodeRotY
            end
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
            local rotTx, rotTy, rotTz = eyeTx, eyeTy, eyeTz
            if type(self.rotateVectorByYaw) == "function" then
                rotTx, rotTy, rotTz = self:rotateVectorByYaw(eyeTx, eyeTy, eyeTz, targetHeadRotY)
            end
            cabTransX = nodeTransX + rotTx
            cabTransY = nodeTransY + rotTy
            cabTransZ = nodeTransZ + rotTz

            local rotMidTx, rotMidTy, rotMidTz = tx, ty, tz
            if type(self.rotateVectorByYaw) == "function" then
                rotMidTx, rotMidTy, rotMidTz = self:rotateVectorByYaw(tx, ty, tz, targetHeadRotY)
            end
            midCabTransX = nodeTransX + rotMidTx
            midCabTransY = nodeTransY + rotMidTy
            midCabTransZ = nodeTransZ + rotMidTz
        end

        if type(localRotationToLocalFn) == "function" then
            cabRotX, cabRotY, cabRotZ = localRotationToLocalFn(cam.headTrackingNode, camParent, pitch, yaw, roll)
        else
            local combYaw = (type(self.normalizeAngle) == "function" and self:normalizeAngle(targetHeadRotY + yaw)) or (targetHeadRotY + yaw)
            cabRotX = pitch
            cabRotY = combYaw
            cabRotZ = roll
        end
    else
        -- Fallback to driver seat anchor baseline when no dedicated headTrackingNode is assigned
        local baseX = cam.origTransX or cam.transX or 0
        local baseY = cam.origTransY or cam.transY or 0
        local baseZ = cam.origTransZ or cam.transZ or 0

        local totalGazeYaw = (type(self.normalizeAngle) == "function" and self:normalizeAngle((cam.origRotY or 0) + lookYaw)) or ((cam.origRotY or 0) + lookYaw)
        local rotTx, rotTy, rotTz = eyeTx, eyeTy, eyeTz
        if type(self.rotateVectorByYaw) == "function" then
            rotTx, rotTy, rotTz = self:rotateVectorByYaw(eyeTx, eyeTy, eyeTz, totalGazeYaw)
        end
        cabTransX = baseX + rotTx
        cabTransY = baseY + rotTy
        cabTransZ = baseZ + rotTz

        local rotMidTx, rotMidTy, rotMidTz = tx, ty, tz
        if type(self.rotateVectorByYaw) == "function" then
            rotMidTx, rotMidTy, rotMidTz = self:rotateVectorByYaw(tx, ty, tz, totalGazeYaw)
        end
        midCabTransX = baseX + rotMidTx
        midCabTransY = baseY + rotMidTy
        midCabTransZ = baseZ + rotMidTz

        local combYaw = (type(self.normalizeAngle) == "function" and self:normalizeAngle(totalGazeYaw + yaw)) or (totalGazeYaw + yaw)
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
            self:applyCameraProjection(activeEyeCam, eyeIndex, eyeProj)
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
            -- This branch always shows the left eye; reuse eyeProj only when it was fetched for eye 0.
            local leftProj = nil
            if eyeIndex == 0 then leftProj = eyeProj end
            self:applyCameraProjection(targetNode, 0, leftProj)
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
    if type(self.getPlayerBaseTranslation) == "function" then
        self:getPlayerBaseTranslation(cam)
    end
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

    local eyeProj = self:fetchEyeProjection(eyeIndex)
    self:applyCameraProjection(activeCamNode, eyeIndex, eyeProj)

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
        local base = (type(self.getPlayerBaseTranslation) == "function" and self:getPlayerBaseTranslation(cam)) or {0, 0, 0}
        local bx = base.x or base[1] or 0
        local by = base.y or base[2] or 0
        local bz = base.z or base[3] or 0
        finalTx = bx + eyeTx
        finalTy = by + eyeTy
        finalTz = bz + eyeTz

        finalMidTx = bx + tx
        finalMidTy = by + ty
        finalMidTz = bz + tz

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
            self:applyCameraProjection(activeEyeCam, eyeIndex, eyeProj)
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
            -- This branch always shows the left eye; reuse eyeProj only when it was fetched for eye 0.
            local leftProj = nil
            if eyeIndex == 0 then leftProj = eyeProj end
            self:applyCameraProjection(targetNode, 0, leftProj)
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

    -- One record per camera node, allocated on first use and updated in place afterwards
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

-- Export to Stereo subsystem table
VRCompanion.Stereo.getRenderingMode = function(self, ...) return VRCompanion.getRenderingMode(VRCompanion, ...) end
VRCompanion.Stereo.setRenderingMode = function(self, ...) return VRCompanion.setRenderingMode(VRCompanion, ...) end
VRCompanion.Stereo.getSubPassIndex = function(self, ...) return VRCompanion.getSubPassIndex(VRCompanion, ...) end
VRCompanion.Stereo.getDualPassEye = function(self, ...) return VRCompanion.getDualPassEye(VRCompanion, ...) end
VRCompanion.Stereo.setSubPassIndex = function(self, ...) return VRCompanion.setSubPassIndex(VRCompanion, ...) end
VRCompanion.Stereo.advanceSubPass = function(self, ...) return VRCompanion.advanceSubPass(VRCompanion, ...) end
VRCompanion.Stereo.getStereoCameraNodes = function(self, ...) return VRCompanion.getStereoCameraNodes(VRCompanion, ...) end
VRCompanion.Stereo.bindRenderingCamera = function(self, ...) return VRCompanion.bindRenderingCamera(VRCompanion, ...) end
VRCompanion.Stereo.syncActiveEyeCamera = function(self, ...) return VRCompanion.syncActiveEyeCamera(VRCompanion, ...) end
VRCompanion.Stereo.initDualEyeCameraNodes = function(self, ...) return VRCompanion.initDualEyeCameraNodes(VRCompanion, ...) end
VRCompanion.Stereo.ensureDualEyeCameraNodes = function(self, ...) return VRCompanion.ensureDualEyeCameraNodes(VRCompanion, ...) end
VRCompanion.Stereo.initCyclopeanAudioListener = function(self, ...) return VRCompanion.initCyclopeanAudioListener(VRCompanion, ...) end
VRCompanion.Stereo.ensureCyclopeanAudioListener = function(self, ...) return VRCompanion.ensureCyclopeanAudioListener(VRCompanion, ...) end
VRCompanion.Stereo.signalDiscontinuityFlush = function(self, ...) return VRCompanion.signalDiscontinuityFlush(VRCompanion, ...) end
VRCompanion.Stereo.getIs2DViewActive = function(self, ...) return VRCompanion.getIs2DViewActive(VRCompanion, ...) end
VRCompanion.Stereo.set2DViewActive = function(self, ...) return VRCompanion.set2DViewActive(VRCompanion, ...) end
VRCompanion.Stereo.toggle2DView = function(self, ...) return VRCompanion.toggle2DView(VRCompanion, ...) end
VRCompanion.Stereo.isToggleViewDebounced = function(self, ...) return VRCompanion.isToggleViewDebounced(VRCompanion, ...) end
VRCompanion.Stereo.resetToggleViewDebounce = function(self, ...) return VRCompanion.resetToggleViewDebounce(VRCompanion, ...) end
VRCompanion.Stereo.onToggleVrViewAction = function(self, ...) return VRCompanion.onToggleVrViewAction(VRCompanion, ...) end
VRCompanion.Stereo.restoreCameraProjectionsFor2D = function(self, ...) return VRCompanion.restoreCameraProjectionsFor2D(VRCompanion, ...) end
VRCompanion.Stereo.isPassThroughMode = function(self, ...) return VRCompanion.isPassThroughMode(VRCompanion, ...) end
VRCompanion.Stereo.setPassThroughMode = function(self, ...) return VRCompanion.setPassThroughMode(VRCompanion, ...) end
VRCompanion.Stereo.isHeadTrackingActive = function(self, ...) return VRCompanion.isHeadTrackingActive(VRCompanion, ...) end
VRCompanion.Stereo.setHeadTrackingEnabled = function(self, ...) return VRCompanion.setHeadTrackingEnabled(VRCompanion, ...) end
VRCompanion.Stereo.applyCameraProjection = function(self, ...) return VRCompanion.applyCameraProjection(VRCompanion, ...) end
VRCompanion.Stereo.getFrameParity = function(self, ...) return VRCompanion.getFrameParity(VRCompanion, ...) end
VRCompanion.Stereo.getIpd = function(self, ...) return VRCompanion.getIpd(VRCompanion, ...) end
VRCompanion.Stereo.requiresAfrFallback = function(self, ...) return VRCompanion.requiresAfrFallback(VRCompanion, ...) end
VRCompanion.Stereo.computeEyeOffset = function(self, ...) return VRCompanion.computeEyeOffset(VRCompanion, ...) end
VRCompanion.Stereo.getResolvedHeadPose = function(self, ...) return VRCompanion.getResolvedHeadPose(VRCompanion, ...) end
VRCompanion.Stereo.getActiveEyeIndex = function(self, ...) return VRCompanion.getActiveEyeIndex(VRCompanion, ...) end
VRCompanion.Stereo.onVehicleCameraUpdate = function(self, ...) return VRCompanion.onVehicleCameraUpdate(VRCompanion, ...) end
VRCompanion.Stereo.onPlayerCameraMakeCurrent = function(self, ...) return VRCompanion.onPlayerCameraMakeCurrent(VRCompanion, ...) end
VRCompanion.Stereo.onPlayerCameraUpdate = function(self, ...) return VRCompanion.onPlayerCameraUpdate(VRCompanion, ...) end
VRCompanion.Stereo.getPlayerCameraYaw = function(self, ...) return VRCompanion.getPlayerCameraYaw(VRCompanion, ...) end
VRCompanion.Stereo.onPlayerCameraUpdateRotation = function(self, ...) return VRCompanion.onPlayerCameraUpdateRotation(VRCompanion, ...) end
VRCompanion.Stereo.updateMenuBackgroundCamera = function(self, ...) return VRCompanion.updateMenuBackgroundCamera(VRCompanion, ...) end
VRCompanion.Stereo.restoreMenuBackgroundCamera = function(self, ...) return VRCompanion.restoreMenuBackgroundCamera(VRCompanion, ...) end

return VRCompanion.Stereo
