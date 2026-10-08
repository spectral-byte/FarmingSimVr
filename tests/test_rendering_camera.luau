-- ====================================================================
-- Test Suite for the camera the engine renders through in the cab and on foot
-- The engine renders through whatever raw setCamera last bound (getCamera() reports it); the
-- game's camera manager names the camera the game itself wants shown. Drives the Companion Mod
-- through the vehicle and player camera updates and the Sequential Dual-Pass mission update.
-- ====================================================================

_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

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

local EPS = 1e-6
local function near(a, b) return math.abs(a - b) < EPS end

-- --------------------------------------------------------------------
-- Fake engine: flat nodes (world transform = local transform), cameras with lens settings,
-- a rendering camera and a camera manager that names the game's active camera
-- --------------------------------------------------------------------
local ROOT = 1
local CAB, PLAYER, SHOP = 10, 20, 30
local nodes = {}
local rendered = nil
local gameActive = nil
local nextNodeId = 7000

local function addNode(id, t, r, lens)
    nodes[id] = { t = { t[1], t[2], t[3] }, r = { r[1], r[2], r[3] }, parent = 0,
        fov = lens and lens.fov or 1.0, nearClip = lens and lens.nearClip or 0.1, farClip = lens and lens.farClip or 2000 }
    return id
end

local function install()
    nodes = {}
    nextNodeId = 7000
    addNode(ROOT, { 0, 0, 0 }, { 0, 0, 0 })
    addNode(CAB, { 5, 2, 3 }, { 0.1, 2.0, -0.05 }, { fov = 1.2, nearClip = 0.05, farClip = 1800 })
    addNode(PLAYER, { -40, 1.8, 12 }, { 0, -0.7, 0 }, { fov = 1.1, nearClip = 0.15, farClip = 2500 })
    addNode(SHOP, { 300, 50, 300 }, { -0.4, 0.3, 0 })
    rendered = nil
    gameActive = nil

    _G.getRootNode = function() return ROOT end
    _G.getParent = function(node) return nodes[node] and nodes[node].parent or 0 end
    _G.link = function(parent, child) if nodes[child] then nodes[child].parent = parent end end
    _G.setTranslation = function(node, x, y, z) if nodes[node] then nodes[node].t = { x, y, z } end end
    _G.setRotation = function(node, x, y, z) if nodes[node] then nodes[node].r = { x, y, z } end end
    _G.getTranslation = function(node) local n = nodes[node]; return n.t[1], n.t[2], n.t[3] end
    _G.getRotation = function(node) local n = nodes[node]; return n.r[1], n.r[2], n.r[3] end
    _G.getWorldTranslation = _G.getTranslation
    _G.getWorldRotation = _G.getRotation
    _G.localDirectionToWorld = function(node, x, y, z) return x, y, z end
    _G.createCamera = function(name, fov, nearClip, farClip)
        nextNodeId = nextNodeId + 1
        return addNode(nextNodeId, { 0, 0, 0 }, { 0, 0, 0 }, { fov = fov, nearClip = nearClip, farClip = farClip })
    end
    _G.entityExists = function(node) return nodes[node] ~= nil end
    _G.getFovY = function(node) return nodes[node] and nodes[node].fov or 1.0 end
    _G.setFovY = function(node, v) if nodes[node] then nodes[node].fov = v end end
    _G.getNearClip = function(node) return nodes[node].nearClip end
    _G.getFarClip = function(node) return nodes[node].farClip end
    _G.setNearClip = function(node, v) nodes[node].nearClip = v end
    _G.setFarClip = function(node, v) nodes[node].farClip = v end
    _G.setProjectionOffset = function() end
    _G.getProjectionOffset = function() return 0, 0 end
    -- As in the game: the camera manager renders through the cameras registered with it (the
    -- game's own, and any added with addCamera) and ignores any other; the global setCamera is the
    -- game's deprecation wrapper and binds nothing
    local registered = { [CAB] = true, [PLAYER] = true, [SHOP] = true }
    _G.setCamera = function(node) end
    _G.getCamera = function() return rendered end
    _G.g_cameraManager = {
        addCamera = function(self, node) registered[node] = true end,
        removeCamera = function(self, node) registered[node] = nil end,
        setActiveCamera = function(self, node)
            if registered[node] then
                rendered = node
                gameActive = node
            end
        end,
        getActiveCamera = function(self) return gameActive end,
    }
end

-- The game makes one of its own cameras active
local function gameActivates(node)
    g_cameraManager:setActiveCamera(node)
end

local function uninstall()
    for _, name in ipairs({ "getRootNode", "getParent", "link", "setTranslation", "setRotation", "getTranslation",
        "getRotation", "getWorldTranslation", "getWorldRotation", "localDirectionToWorld", "createCamera", "entityExists",
        "getFovY", "setFovY", "getNearClip", "getFarClip", "setNearClip", "setFarClip", "setProjectionOffset",
        "getProjectionOffset", "setCamera", "getCamera", "g_cameraManager", "g_currentMission", "VRBridge",
        "FSBaseMission" }) do
        _G[name] = nil
    end
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    VRCompanion.cyclopeanAudioListenerNode = nil
    VRCompanion.activeRenderingCamera = nil
    VRCompanion.activePlayerCamera = nil
    VRCompanion.registeredEyeCameras = nil
    VRCompanion.gameCameraNode = nil
    VRCompanion.deferRenderingCameraBind = false
    VRCompanion.origCamSettings = {}
    VRCompanion.isMenuVisible = false
    VRCompanion.is2DViewActive = false
    VRCompanion:endEyeProjectionScope()
end

-- state.pose nil = invalid head pose; state.projection false = no VR projection
local function installBridge(state)
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        requiresAfrFallback = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return state.parity or 0 end,
        getRenderingMode = function() return state.mode or "AFR" end,
        getSubPassIndex = function() return state.subPass or 0 end,
        setSubPassIndex = function(idx) state.subPass = idx end,
        getEyeProjection = function(eye)
            if state.projection == false then return { isValid = false } end
            return { isValid = true, fovY = 1.7, xOffset = 0, yOffset = 0 }
        end,
        getHeadPose = function()
            if state.pose == nil then return { isValid = false } end
            return state.pose
        end,
        signalDiscontinuity = function() end,
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
end

local function validPose()
    return { isValid = true, x = 0.1, y = 0.02, z = -0.05, pitch = 0.2, yaw = 0.3, roll = 0.05, qx = 0, qy = 0, qz = 0, qw = 1 }
end

local function cabCamera()
    gameActivates(CAB)
    return { isInside = true, cameraNode = CAB, origRotY = 2.0, rotY = 2.0, rotX = 0.1, rotZ = 0,
        origTransX = 5, origTransY = 2, origTransZ = 3 }
end

local function playerCamera()
    gameActivates(PLAYER)
    return { cameraNode = PLAYER, firstPersonCamera = PLAYER }
end

local function run(name, state, fn)
    test(name, function()
        install()
        installBridge(state)
        local ok, err = pcall(fn)
        uninstall()
        assert(ok, tostring(err))
    end)
end

-- The engine renders exactly what the given game camera sees: its position, rotation and lens
local function assertRendersViewOf(camNode, label)
    assert(rendered ~= nil, label .. ": a camera must be rendering")
    local r, c = nodes[rendered], nodes[camNode]
    for i = 1, 3 do
        assert(near(r.t[i], c.t[i]) and near(r.r[i], c.r[i]), string.format(
            "%s: the view must be the game camera's own, got pos (%f, %f, %f) rot (%f, %f, %f)",
            label, r.t[1], r.t[2], r.t[3], r.r[1], r.r[2], r.r[3]))
    end
end

print("Running Rendering Camera Tests...")

run("CabWithLostHeadTrackingShowsTheGamesCabView", { mode = "AFR" }, function()
    local state = { mode = "AFR", pose = validPose() }
    installBridge(state)
    local cab = cabCamera()
    VRCompanion:onVehicleCameraUpdate(cab, 16.6)
    assert(rendered ~= CAB, "precondition: head-tracked stereo renders through an eye camera")

    -- Tracking drops out while the game moves its cab camera (mouse look, the vehicle driving on)
    state.pose = nil
    nodes[CAB].t = { 6, 2.1, 4 }
    nodes[CAB].r = { 0.05, 2.4, 0 }
    VRCompanion:onVehicleCameraUpdate(cab, 16.6)

    assertRendersViewOf(CAB, "AFR")
end)

run("OnFootWithLostHeadTrackingShowsTheGamesPlayerView", { mode = "AFR" }, function()
    local state = { mode = "AFR", pose = validPose() }
    installBridge(state)
    local playerCam = playerCamera()
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    assert(rendered ~= PLAYER, "precondition: head-tracked stereo renders through an eye camera")

    state.pose = nil
    nodes[PLAYER].t = { -41, 1.8, 13 }
    nodes[PLAYER].r = { 0, -0.2, 0 }
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)

    assertRendersViewOf(PLAYER, "AFR")
end)

run("WithoutAVrProjectionTheEyeCameraHasTheGameCamerasFieldOfView", { mode = "AFR", pose = validPose(), projection = false }, function()
    -- The eye cameras were created on foot, with the player camera's field of view
    VRCompanion:onPlayerCameraUpdate(playerCamera(), 16.6)
    VRCompanion.activePlayerCamera = nil

    VRCompanion:onVehicleCameraUpdate(cabCamera(), 16.6)

    assert(rendered ~= CAB, "precondition: the cab renders through an eye camera")
    assert(near(nodes[rendered].fov, 1.2), string.format("the eye camera must have the cab camera's fov 1.2, got %f", nodes[rendered].fov))
end)

run("EyeCamerasDeletedWithAVehicleAreRecreatedBeforeRendering", { mode = "AFR", pose = validPose() }, function()
    VRCompanion:onVehicleCameraUpdate(cabCamera(), 16.6)
    -- The vehicle is deleted (sold, reset) and takes the eye cameras linked under it with it
    nodes[VRCompanion.cameraNodeLeft] = nil
    nodes[VRCompanion.cameraNodeRight] = nil

    VRCompanion:onPlayerCameraUpdate(playerCamera(), 16.6)

    assert(rendered ~= PLAYER and nodes[rendered] ~= nil, "the engine must render through an existing eye camera, got " .. tostring(rendered))
end)

run("WithAGuiMenuOpenTheCabViewIsHeadTrackedOnce", { mode = "AFR", pose = validPose() }, function()
    _G.g_gui = { getIsGuiVisible = function() return true end }
    local cab = cabCamera()
    local ok, err = pcall(function()
        VRCompanion:update(16.6)
        assert(VRCompanion.isMenuVisible, "precondition: the menu is open")
        VRCompanion:onVehicleCameraUpdate(cab, 16.6)
        local eye = rendered
        local t, r = nodes[eye].t, nodes[eye].r
        local placed = { t[1], t[2], t[3], r[1], r[2], r[3] }

        VRCompanion:update(16.6)

        local now = { nodes[eye].t[1], nodes[eye].t[2], nodes[eye].t[3], nodes[eye].r[1], nodes[eye].r[2], nodes[eye].r[3] }
        for i = 1, 6 do
            assert(near(now[i], placed[i]), "the menu background must not add the head pose again to the head-tracked eye camera")
        end
    end)
    _G.g_gui = nil
    VRCompanion.menuCameraOffsets = {}
    VRCompanion.guiVisibilityInitialized = false
    assert(ok, tostring(err))
end)

-- A game screen with a camera of its own (the shop, construction mode) is shown
local function showShop()
    gameActivates(SHOP)
end

run("WhileTheGameShowsAnotherCameraOnFootLeavesRenderingAlone", { mode = "AFR", pose = validPose() }, function()
    local playerCam = playerCamera()
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    assert(rendered ~= PLAYER, "precondition: head-tracked stereo renders through an eye camera")
    showShop()
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    assert(rendered == SHOP, "the game's own screen camera must keep rendering, got " .. tostring(rendered))
end)

run("WhileTheGameShowsAnotherCameraTheCabLeavesRenderingAlone", { mode = "AFR", pose = validPose() }, function()
    local cab = cabCamera()
    VRCompanion:onVehicleCameraUpdate(cab, 16.6)
    assert(rendered ~= CAB, "precondition: head-tracked stereo renders through an eye camera")
    showShop()
    VRCompanion:onVehicleCameraUpdate(cab, 16.6)
    assert(rendered == SHOP, "the game's own screen camera must keep rendering, got " .. tostring(rendered))
end)

run("AfterTheGameScreenClosesOnFootRendersInVrAgain", { mode = "AFR", pose = validPose() }, function()
    local playerCam = playerCamera()
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    showShop()
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    gameActivates(PLAYER)
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    assert(rendered ~= PLAYER and rendered ~= SHOP, "head-tracked stereo must render through an eye camera again, got " .. tostring(rendered))
end)

run("WhileTheGameShowsAnotherCameraSequentialDualPassLeavesRenderingAlone", { mode = "SequentialDualPass", subPass = 0, pose = validPose() }, function()
    local playerCam = playerCamera()
    VRCompanion.activePlayerCamera = playerCam
    _G.g_localPlayer = { camera = playerCam, getCurrentVehicle = function() return nil end }
    _G.FSBaseMission = { update = function(self, dt) VRCompanion:onPlayerCameraUpdate(playerCam, dt) end }
    VRCompanion:hookMissionUpdate()
    showShop()

    local ok, err = pcall(function()
        for subPass = 0, 1 do
            VRCompanion:setSubPassIndex(subPass)
            _G.FSBaseMission:update(16.6)
            assert(rendered == SHOP, string.format("sub-pass %d: the game's own screen camera must keep rendering, got %s", subPass, tostring(rendered)))
        end
    end)
    VRCompanion:unhookMissionUpdate()
    _G.g_localPlayer = nil
    assert(ok, tostring(err))
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_rendering_camera.luau", failed))
