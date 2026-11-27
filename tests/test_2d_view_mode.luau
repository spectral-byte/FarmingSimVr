_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

_G.cameraFovY = {}
_G.cameraProjectionOffsets = {}
function setFovY(node, fov)
    _G.cameraFovY[node] = fov
end
function getFovY(node)
    return _G.cameraFovY[node] or 1.0472
end
function setProjectionOffset(node, x, y)
    _G.cameraProjectionOffsets[node] = { x = x, y = y }
end
function getProjectionOffset(node)
    local curr = _G.cameraProjectionOffsets[node]
    if curr then return curr.x, curr.y end
    return 0.0, 0.0
end
MathUtil = {
    getValidLimit = function(angle)
        while angle > math.pi do angle = angle - 2 * math.pi end
        while angle < -math.pi do angle = angle + 2 * math.pi end
        return angle
    end
}
_G.MathUtil = MathUtil

_G.mockBridge2DActive = false
local mockVRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    handshake = function() return { success = true, version = "1.0.0.0", mode = "OpenXR" } end,
    getIs2DViewActive = function() return _G.mockBridge2DActive end,
    set2DViewActive = function(active) _G.mockBridge2DActive = (active == true) end,
    getHeadPose = function()
        return { isValid = true, x = 0.1, y = 0.05, z = -0.1, pitch = 0.15, yaw = 0.2, roll = 0.0 }
    end,
    getEyeProjection = function(eye)
        return { isValid = true, fovY = 1.8326, xOffset = 0.0, yOffset = 0.0 }
    end,
    getFrameParity = function() return 0 end,
    getIpd = function() return 0.064 end,
    setMenuVisible = function(v) end,
    alignMenuQuad = function() end,
    recenter = function() end,
    resetTemporalHistory = function() end,
    onMapUnload = function() _G.mockBridge2DActive = false end
}
_G.VRBridge = mockVRBridge
VRBridge = mockVRBridge

loadCompanionMod()

local passed = 0
local failed = 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print(string.format("  [PASS] %s", name))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s: %s", name, tostring(err)))
    end
end

print("Running In-Engine 2D Floating Window Mode Tests...")

test("Is2DViewActiveStateTrackingAndSynchronization", function()
    assert(VRCompanion.is2DViewActive == false, "is2DViewActive must default to false")
    assert(VRCompanion:getIs2DViewActive() == false, "getIs2DViewActive must return false")
    assert(VRBridge.getIs2DViewActive() == false, "VRBridge must default to false")

    VRCompanion:set2DViewActive(true)
    assert(VRCompanion.is2DViewActive == true, "is2DViewActive must be true")
    assert(VRCompanion:getIs2DViewActive() == true, "getIs2DViewActive must return true")
    assert(VRBridge.getIs2DViewActive() == true, "VRBridge must reflect true state")

    VRCompanion:set2DViewActive(false)
    assert(VRCompanion.is2DViewActive == false, "is2DViewActive must be false")
    assert(VRCompanion:getIs2DViewActive() == false, "getIs2DViewActive must return false")
    assert(VRBridge.getIs2DViewActive() == false, "VRBridge must reflect false state")

    VRBridge.set2DViewActive(true)
    assert(VRCompanion.is2DViewActive == false)
    VRCompanion:update(16.6)
    assert(VRCompanion.is2DViewActive == true, "VRCompanion must sync with external VRBridge on update")

    VRCompanion:set2DViewActive(false)
end)

test("InCabVehicleCameraBypasses6DOFTrackingIn2DMode", function()
    local nodeTrans = { 0.5, 2.0, 1.0 }
    local nodeRot = { 0.0, 3.14159, 0.0 }
    function setTranslation(node, x, y, z) nodeTrans = { x, y, z } end
    function getTranslation(node) return nodeTrans[1], nodeTrans[2], nodeTrans[3] end
    function setRotation(node, rx, ry, rz) nodeRot = { rx, ry, rz } end
    function getRotation(node) return nodeRot[1], nodeRot[2], nodeRot[3] end

    local mockVehCam = {
        isInside = true,
        cameraNode = 10,
        origRotY = 3.14159,
        rotY = 3.14159,
        rotX = 0.0,
        rotZ = 0.0,
        transX = 0.5,
        transY = 2.0,
        transZ = 1.0,
        origTransX = 0.5,
        origTransY = 2.0,
        origTransZ = 1.0,
    }

    VRCompanion:set2DViewActive(false)
    VRCompanion:onVehicleCameraUpdate(mockVehCam, 16.6)
    assert(math.abs(nodeRot[1] - 0.15) < 0.001, "3D VR mode must apply HMD pitch (0.15) to camera")

    VRCompanion:set2DViewActive(true)
    mockVehCam.rotX = 0.35
    mockVehCam.rotY = 2.80
    nodeRot = { 0.35, 2.80, 0.0 }
    nodeTrans = { 0.5, 2.0, 1.0 }

    VRCompanion:onVehicleCameraUpdate(mockVehCam, 16.6)
    assert(math.abs(nodeRot[1] - 0.35) < 0.001, "2D mode must preserve mouse pitch and bypass HMD pitch")
    assert(math.abs(nodeRot[2] - 2.80) < 0.001, "2D mode must preserve mouse yaw and bypass HMD yaw")
    assert(math.abs(nodeTrans[1] - 0.5) < 0.001, "2D mode must preserve vehicle camera translation")

    VRCompanion:set2DViewActive(false)
end)

test("OnFootPlayerCameraBypasses6DOFTrackingAndSupports2DMouseLook", function()
    local mockInput = { cameraRotationX = 0.20, cameraRotationY = -0.30 }
    local curCamRot = { 0.0, 0.0, 0.0 }
    local curCamTrans = { 0.0, 1.8, 0.0 }

    local mockPlayerCam = {
        cameraNode = 20,
        firstPersonCamera = 20,
        player = { inputComponent = mockInput },
        setRotation = function(self, rx, ry, rz)
            curCamRot = { rx, ry, rz }
        end,
        getRotation = function(self)
            return curCamRot[1], curCamRot[2], curCamRot[3]
        end
    }
    function setTranslation(node, x, y, z) curCamTrans = { x, y, z } end
    function getTranslation(node) return curCamTrans[1], curCamTrans[2], curCamTrans[3] end
    function setRotation(node, rx, ry, rz) curCamRot = { rx, ry, rz } end
    function getRotation(node) return curCamRot[1], curCamRot[2], curCamRot[3] end

    VRCompanion:set2DViewActive(false)
    VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
    assert(curCamRot[1] == 0.0, "3D VR mode must zero neck pitch")

    VRCompanion:set2DViewActive(true)
    curCamRot = { 0.0, 0.0, 0.0 }
    curCamTrans = { 0.0, 1.8, 0.0 }

    VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
    assert(math.abs(curCamRot[1] - 0.20) < 0.001, "2D mode must update pitch from mouse Y")
    assert(math.abs(curCamRot[2] - 0.30) < 0.001, "2D mode must update yaw from mouse X")

    VRCompanion:onPlayerCameraUpdate(mockPlayerCam, 16.6)
    assert(math.abs(curCamRot[1] - 0.20) < 0.001, "2D mode must not overwrite pitch with HMD pose")
    assert(math.abs(curCamTrans[1] - 0.0) < 0.001, "2D mode must not apply HMD translation")

    VRCompanion:set2DViewActive(false)
end)

local function installRenderedCamera(initial)
    local rendered = initial
    local registered = { [initial] = true }
    _G.setCamera = function(node) end
    _G.getCamera = function() return rendered end
    _G.g_cameraManager = {
        addCamera = function(self, node) registered[node] = true end,
        setActiveCamera = function(self, node) if registered[node] then rendered = node end end,
        getActiveCamera = function(self) return rendered end,
    }
    local nextEye = 7000
    _G.createCamera = function() nextEye = nextEye + 1; return nextEye end
end

local function removeRenderedCamera()
    _G.setCamera = nil
    _G.getCamera = nil
    _G.g_cameraManager = nil
    VRCompanion.registeredEyeCameras = nil
    VRCompanion.gameCameraNode = nil
    _G.createCamera = nil
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    VRCompanion.activeRenderingCamera = nil
end

test("In2DFloatingWindowModeTheCabRendersThroughTheGamesOwnCamera", function()
    installRenderedCamera(10)
    function setTranslation() end
    function setRotation() end
    local cab = { isInside = true, cameraNode = 10, origRotY = 3.14159, rotY = 3.14159, rotX = 0.0, rotZ = 0.0,
        origTransX = 0.5, origTransY = 2.0, origTransZ = 1.0 }

    local ok, err = pcall(function()
        VRCompanion:set2DViewActive(false)
        VRCompanion:onVehicleCameraUpdate(cab, 16.6)
        assert(getCamera() ~= 10, "precondition: in VR the cab renders through an eye camera")

        VRCompanion:set2DViewActive(true)
        VRCompanion:onVehicleCameraUpdate(cab, 16.6)
        assert(getCamera() == 10, "2D Floating Window Mode must render through the game's cab camera, got " .. tostring(getCamera()))
    end)
    VRCompanion:set2DViewActive(false)
    removeRenderedCamera()
    assert(ok, tostring(err))
end)

test("In2DFloatingWindowModeOnFootRendersThroughTheGamesOwnCamera", function()
    installRenderedCamera(20)
    function setTranslation() end
    function setRotation() end
    local playerCam = { cameraNode = 20, firstPersonCamera = 20 }

    local ok, err = pcall(function()
        VRCompanion:set2DViewActive(false)
        VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
        assert(getCamera() ~= 20, "precondition: in VR on foot renders through an eye camera")

        VRCompanion:set2DViewActive(true)
        VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
        assert(getCamera() == 20, "2D Floating Window Mode must render through the game's player camera, got " .. tostring(getCamera()))
    end)
    VRCompanion:set2DViewActive(false)
    VRCompanion.activePlayerCamera = nil
    removeRenderedCamera()
    assert(ok, tostring(err))
end)

test("CameraProjectionRestoresOriginalFovAndZeroOffsetsIn2DMode", function()
    local camNode = 50
    setFovY(camNode, 1.0472)
    setProjectionOffset(camNode, 0.0, 0.0)

    VRCompanion:set2DViewActive(false)
    VRCompanion:applyCameraProjection(camNode, 0)
    assert(math.abs(getFovY(camNode) - 1.8326) < 0.01, "3D VR mode must apply VR FOV")

    VRCompanion:set2DViewActive(true)
    VRCompanion:applyCameraProjection(camNode, 0)
    local restoredFov = getFovY(camNode)
    local offX, offY = getProjectionOffset(camNode)
    assert(math.abs(restoredFov - 1.0472) < 0.0001, "2D mode must restore original FOV")
    assert(offX == 0.0 and offY == 0.0, "2D mode must enforce zero projection offsets")

    VRCompanion:set2DViewActive(false)
    VRCompanion:applyCameraProjection(camNode, 0)
    assert(math.abs(getFovY(camNode) - 1.8326) < 0.01, "Returning to 3D VR mode restores VR FOV")
end)

-- Slice 5: HUD Convergence Disparity Zeroing & Full-Frame Anchors
test("HudConvergenceDisparityZeroedAndFullFrameAnchorsAppliedIn2DMode", function()
    VRCompanion.handshakeComplete = true
    VRCompanion.passThroughMode = false
    VRCompanion.headTrackingEnabled = true

    _G.g_hudAnchorLeft = 0.20
    _G.g_hudAnchorRight = 0.80
    _G.g_hudAnchorBottom = 0.16
    _G.g_hudAnchorTop = 0.84

    VRCompanion:set2DViewActive(false)
    VRCompanion:updateHudConvergence(16.6)
    assert(VRCompanion.currentConvergenceDisparity > 0.0, "3D VR mode has non-zero disparity")

    VRCompanion:set2DViewActive(true)
    VRCompanion:updateHudConvergence(16.6)
    assert(VRCompanion.currentConvergenceDisparity == 0.0, "2D mode zeroes disparity")
    assert(VRCompanion.currentConvergenceOffset == 0.0, "2D mode zeroes convergence offset")
    assert(_G.g_hudAnchorLeft == 0.0, "2D mode applies full-frame left anchor (0.0)")
    assert(_G.g_hudAnchorRight == 1.0, "2D mode applies full-frame right anchor (1.0)")
    assert(_G.g_hudAnchorBottom == 0.0, "2D mode applies full-frame bottom anchor (0.0)")
    assert(_G.g_hudAnchorTop == 1.0, "2D mode applies full-frame top anchor (1.0)")

    VRCompanion:set2DViewActive(false)
end)

test("ModeIsStickyAcrossInGameGuiMenus", function()
    local menuVisible = false
    _G.g_gui = {
        getIsGuiVisible = function() return menuVisible end
    }

    VRCompanion:set2DViewActive(true)
    assert(VRCompanion.is2DViewActive == true)

    menuVisible = true
    VRCompanion:update(16.6)
    assert(VRCompanion.isMenuVisible == true)
    assert(VRCompanion.is2DViewActive == true, "2D mode must persist while in GUI menu")

    menuVisible = false
    VRCompanion:update(16.6)
    assert(VRCompanion.isMenuVisible == false)
    assert(VRCompanion.is2DViewActive == true, "2D mode must remain active after GUI menu closes")

    VRCompanion:set2DViewActive(false)
end)

test("FreshMapLoadAlwaysInitializesIn3DVrMode", function()
    VRCompanion:set2DViewActive(true)
    assert(VRCompanion.is2DViewActive == true)

    VRCompanion:loadMap("mapUS")
    assert(VRCompanion.is2DViewActive == false, "loadMap must initialize is2DViewActive to false")
    assert(VRBridge.getIs2DViewActive() == false, "VRBridge must be synchronized to false")
end)

test("SnapTurnSuppressedWhen2DViewActive", function()
    local mockVehCam = { isInside = true, rotY = 0.0, origRotY = 0.0 }
    VRCompanion.getActiveVehicleCamera = function() return mockVehCam end
    VRCompanion.getControlledVehicle = function() return 1 end

    VRCompanion:set2DViewActive(false)
    assert(VRCompanion:snapTurn(1) == true, "Snap turn succeeds in 3D VR mode")

    VRCompanion:set2DViewActive(true)
    VRCompanion.lastSnapTurnTime = 0
    local ok, reason = VRCompanion:snapTurn(1)
    assert(ok == false, "Snap turn must be suppressed in 2D mode")
    assert(reason == "2d_view_active", "Reason must be 2d_view_active")

    VRCompanion:set2DViewActive(false)
end)

test("ConsoleCommandsManage2DViewMode", function()
    VRCompanion:set2DViewActive(false)

    local status = VRCompanion:consoleCommandSet2DView("status")
    assert(string.find(status, "false") ~= nil)

    VRCompanion:consoleCommandSet2DView("true")
    assert(VRCompanion.is2DViewActive == true)

    VRCompanion:consoleCommandToggleView()
    assert(VRCompanion.is2DViewActive == false)

    VRCompanion:consoleCommandToggleView()
    assert(VRCompanion.is2DViewActive == true)

    VRCompanion:set2DViewActive(false)
end)

test("ToggleViewActionRegistrationAndCleanup", function()
    local registeredEvents = {}
    _G.g_inputBinding = {
        events = {}, actionEvents = {},
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            local id = actionName .. "|table: 0x0|" .. (self.nextEventNumber or 1); self.nextEventNumber = (self.nextEventNumber or 1) + 1
            local ev = { id = id, actionName = actionName, target = target, callback = callback }
            self.events[id] = ev; self.actionEvents[actionName] = { ev }
            registeredEvents[actionName] = ev
            return true, id
        end,
        setActionEventTextPriority = function() end,
        setActionEventActive = function() end,
        setActionEventTextVisibility = function() end,
        setActionEventText = function() end
    }

    VRCompanion:registerActionEvents(true)
    assert(registeredEvents["VR_TOGGLE_VIEW"] ~= nil, "VR_TOGGLE_VIEW must be registered")
    assert(registeredEvents["VR_TOGGLE_VIEW"].callback == VRCompanion.onToggleVrViewAction, "Callback must match onToggleVrViewAction")
    assert(VRCompanion.toggleViewActionEventId ~= nil, "toggleViewActionEventId must be set")

    VRCompanion:deleteMap()
    assert(VRCompanion.toggleViewActionEventId == nil, "toggleViewActionEventId must be cleared by deleteMap")
end)

test("MapLoadMakesTheEyeNodesAndRegistersTheActionEventsAndUnloadUndoesThem", function()
    local nextId = 0
    _G.g_inputBinding = {
        events = {}, actionEvents = {},
        registerActionEvent = function(self, actionName)
            nextId = nextId + 1
            local id = actionName .. "|" .. nextId
            self.events[id] = { id = id }
            return true, id
        end,
        setActionEventTextPriority = function() end,
        setActionEventActive = function() end,
        setActionEventTextVisibility = function() end,
        setActionEventText = function() end
    }
    local nextNode = 7000
    local realCreateCamera, realCreateTransformGroup = _G.createCamera, _G.createTransformGroup
    _G.createCamera = function() nextNode = nextNode + 1; return nextNode end
    _G.createTransformGroup = function() nextNode = nextNode + 1; return nextNode end
    VRCompanion.cameraNodeLeft, VRCompanion.cameraNodeRight, VRCompanion.cyclopeanAudioListenerNode = nil, nil, nil
    VRCompanion.toggleViewActionEventId = nil

    VRCompanion:loadMap("mapUS")
    _G.createCamera, _G.createTransformGroup = realCreateCamera, realCreateTransformGroup
    assert(VRCompanion.cameraNodeLeft ~= nil and VRCompanion.cameraNodeRight ~= nil, "map load makes the Dual Eye Camera Nodes")
    assert(VRCompanion.cyclopeanAudioListenerNode ~= nil, "map load makes the Cyclopean Audio Listener Node")
    for _, row in ipairs(VRCompanion.ACTION_EVENTS) do
        assert(VRCompanion[row.idField] ~= nil, "map load registers " .. row.action)
    end

    VRCompanion:set2DViewActive(true)
    VRCompanion.vehicleRotationStates = { [1] = { rotY = 1.0 } }
    VRCompanion.lastToggleViewTime = 123
    VRCompanion:deleteMap()
    assert(VRCompanion.cameraNodeLeft == nil and VRCompanion.cameraNodeRight == nil, "map unload forgets the Dual Eye Camera Nodes")
    assert(VRCompanion.cyclopeanAudioListenerNode == nil, "map unload forgets the Cyclopean Audio Listener Node")
    assert(VRCompanion.is2DViewActive == false, "map unload leaves 2D Floating Window Mode")
    assert(VRBridge.getIs2DViewActive() == false, "and tells VRBridge")
    assert(next(VRCompanion.vehicleRotationStates) == nil, "map unload forgets the vehicles' rotation states")
    assert(VRCompanion.lastToggleViewTime == nil, "and the toggle view debounce")
    for _, row in ipairs(VRCompanion.ACTION_EVENTS) do
        assert(VRCompanion[row.idField] == nil, "map unload forgets the " .. row.action .. " event")
    end
end)

test("MapUnloadIn2DModeLeavesTheGamesHudAnchorsAtTheFullScreen", function()
    _G.g_hudAnchorLeft, _G.g_hudAnchorRight, _G.g_hudAnchorBottom, _G.g_hudAnchorTop = 0.0, 1.0, 0.0, 1.0
    VRCompanion.origHudAnchors = nil
    VRCompanion.persistedHudState = VRCompanion.HUD_STATE_HIDDEN
    VRCompanion:loadMap("mapUS")
    assert(_G.g_hudAnchorLeft > 0.0, "map load applies the VR HUD Safe Area")

    VRCompanion:set2DViewActive(true)
    VRCompanion:deleteMap()

    assert(VRCompanion.is2DViewActive == false, "map unload leaves 2D Floating Window Mode")
    assert(_G.g_hudAnchorLeft == 0.0, "left anchor back at the full screen, got " .. tostring(_G.g_hudAnchorLeft))
    assert(_G.g_hudAnchorRight == 1.0, "right anchor back at the full screen, got " .. tostring(_G.g_hudAnchorRight))
    assert(_G.g_hudAnchorBottom == 0.0, "bottom anchor back at the full screen, got " .. tostring(_G.g_hudAnchorBottom))
    assert(_G.g_hudAnchorTop == 1.0, "top anchor back at the full screen, got " .. tostring(_G.g_hudAnchorTop))
    assert(VRCompanion:getHudState() == VRCompanion.HUD_STATE_FULL, "the HUD is left in the Full state")
    assert(VRCompanion.origHudAnchors == nil, "no game anchors are kept for the next map")
end)

test("ToggleViewActionDebounceAndSilentExecution", function()
    local warningCalls = 0
    _G.g_currentMission = {
        showBlinkingWarning = function(self, msg, duration)
            warningCalls = warningCalls + 1
        end
    }

    VRCompanion:set2DViewActive(false)
    VRCompanion:resetToggleViewDebounce()
    assert(VRCompanion.is2DViewActive == false)

    local ok1 = VRCompanion:onToggleVrViewAction("VR_TOGGLE_VIEW", 1, 1000)
    assert(ok1 == true, "First toggle press must succeed")
    assert(VRCompanion.is2DViewActive == true, "is2DViewActive must be true")
    assert(warningCalls == 0, "No blinking warning on toggle")

    local ok2, reason = VRCompanion:onToggleVrViewAction("VR_TOGGLE_VIEW", 1, 1100)
    assert(ok2 == false, "Rapid consecutive press must be debounced")
    assert(reason == "debounced", "Failure reason must be 'debounced'")
    assert(VRCompanion.is2DViewActive == true, "Mode must remain 2D")
    assert(warningCalls == 0, "No warning on debounced press")

    local ok3 = VRCompanion:onToggleVrViewAction("VR_TOGGLE_VIEW", 1, 1300)
    assert(ok3 == false, "Toggling back returns false")
    assert(VRCompanion.is2DViewActive == false, "Must return to 3D VR mode")
    assert(warningCalls == 0, "No warning on returning to 3D mode")
end)

test("RecenterIn2DModeRealignsFloatingQuadWithoutResettingVehicleOrientation", function()
    local alignMenuQuadCalls = 0
    local recenterCalls = 0
    mockVRBridge.alignMenuQuad = function() alignMenuQuadCalls = alignMenuQuadCalls + 1 end
    mockVRBridge.recenter = function() recenterCalls = recenterCalls + 1 end

    local mockVehCam = {
        isInside = true,
        rotY = 1.25,
        origRotY = 0.0,
        cameraNode = 101
    }
    VRCompanion.getActiveVehicleCamera = function() return mockVehCam end
    VRCompanion.getControlledVehicle = function() return {} end

    VRCompanion:set2DViewActive(true)

    VRCompanion:onRecenterAction()

    assert(alignMenuQuadCalls == 1, "alignMenuQuad must be called in 2D mode recenter")
    assert(recenterCalls == 0, "VRBridge.recenter must NOT be called in 2D mode")
    assert(mockVehCam.rotY == 1.25, "Vehicle camera rotY must remain unchanged")

    VRCompanion:set2DViewActive(false)
    VRCompanion:onRecenterAction()

    assert(recenterCalls == 1, "VRBridge.recenter must be called in 3D VR mode")
    assert(alignMenuQuadCalls == 1, "alignMenuQuad must NOT be called in 3D mode")
end)

test("Entering2DViewWhileMenuQuadIsOpenRestoresMenuBackgroundCameraImmediately", function()
    local menuCamNode = 9050
    local camTrans = { 10, 2, 30 }
    local camRot = { 0.1, 0.5, 0.0 }
    local realGetCamera = _G.getCamera
    local realGetTrans, realSetTrans = _G.getTranslation, _G.setTranslation
    local realGetRot, realSetRot = _G.getRotation, _G.setRotation
    local realGui = _G.g_gui

    _G.getCamera = function() return menuCamNode end
    _G.getTranslation = function(node)
        if node == menuCamNode then return camTrans[1], camTrans[2], camTrans[3] end
        return 0, 0, 0
    end
    _G.setTranslation = function(node, x, y, z)
        if node == menuCamNode then camTrans = { x, y, z } end
    end
    _G.getRotation = function(node)
        if node == menuCamNode then return camRot[1], camRot[2], camRot[3] end
        return 0, 0, 0
    end
    _G.setRotation = function(node, rx, ry, rz)
        if node == menuCamNode then camRot = { rx, ry, rz } end
    end
    _G.g_gui = { getIsGuiVisible = function() return true end }

    VRCompanion:set2DViewActive(false)
    VRCompanion.menuCameraOffsets = {}
    VRCompanion:update(16.6)
    assert(VRCompanion:isMenuQuadShown() == true, "precondition: menu quad is open")
    assert(math.abs(camTrans[1] - 10) > 0.01, "precondition: menu background camera has head offset")

    VRCompanion:set2DViewActive(true)

    assert(math.abs(camTrans[1] - 10) < 0.0001 and math.abs(camTrans[2] - 2) < 0.0001 and math.abs(camTrans[3] - 30) < 0.0001,
        string.format("menu background camera translation must restore immediately, got (%.3f, %.3f, %.3f)", camTrans[1], camTrans[2], camTrans[3]))
    assert(math.abs(camRot[1] - 0.1) < 0.0001 and math.abs(camRot[2] - 0.5) < 0.0001 and math.abs(camRot[3] - 0.0) < 0.0001,
        string.format("menu background camera rotation must restore immediately, got (%.3f, %.3f, %.3f)", camRot[1], camRot[2], camRot[3]))
    assert(next(VRCompanion.menuCameraOffsets) == nil, "menuCameraOffsets must be cleared")

    _G.g_gui = realGui
    VRCompanion:update(16.6)
    VRCompanion:set2DViewActive(false)
    _G.getCamera = realGetCamera
    _G.getTranslation, _G.setTranslation = realGetTrans, realSetTrans
    _G.getRotation, _G.setRotation = realGetRot, realSetRot
end)

test("MapUnloadResetsTransientStereoLocomotionAndLogState", function()
    VRCompanion:loadMap("mapUS")

    VRCompanion.renderedFovX = 1.9
    VRCompanion.outsideCameraHeading = 1.2
    VRCompanion.lastGameShowingTrackedCamera = false
    VRCompanion.lastSnapTurnTime = 1234
    VRCompanion.lastMissionUpdateError = "err"
    VRCompanion.invalidPoseTicks = 17
    VRCompanion.vehCamTicks = 23
    VRCompanion.playerCamTicks = 31
    VRCompanion.menuCamTicks = 41
    VRCompanion.missingInputEventsLogged = true
    VRCompanion.hasLoggedFillLevelsDisplayInfo = true
    VRCompanion.gameCameraNode = 999

    VRCompanion:deleteMap()

    assert(VRCompanion.renderedFovX == nil, "renderedFovX must reset on map unload")
    assert(VRCompanion.outsideCameraHeading == nil, "outsideCameraHeading must reset on map unload")
    assert(VRCompanion.lastGameShowingTrackedCamera == nil, "lastGameShowingTrackedCamera must reset on map unload")
    assert(VRCompanion.lastSnapTurnTime == nil, "lastSnapTurnTime must reset on map unload")
    assert(VRCompanion.lastMissionUpdateError == nil, "lastMissionUpdateError must reset on map unload")
    assert((VRCompanion.invalidPoseTicks or 0) == 0, "invalidPoseTicks must reset on map unload")
    assert((VRCompanion.vehCamTicks or 0) == 0, "vehCamTicks must reset on map unload")
    assert((VRCompanion.playerCamTicks or 0) == 0, "playerCamTicks must reset on map unload")
    assert((VRCompanion.menuCamTicks or 0) == 0, "menuCamTicks must reset on map unload")
    assert(not VRCompanion.missingInputEventsLogged, "missingInputEventsLogged must reset on map unload")
    assert(not VRCompanion.hasLoggedFillLevelsDisplayInfo, "hasLoggedFillLevelsDisplayInfo must reset on map unload")
    assert(VRCompanion.gameCameraNode == nil, "gameCameraNode must reset on map unload")
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_2d_view_mode.luau", failed))
