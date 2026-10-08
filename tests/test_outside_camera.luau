-- ====================================================================
-- Test Suite for the Outside Camera: 6DOF head tracking and stereo on a level base
-- Drives the Companion Mod through the vehicle camera update, the Sequential Dual-Pass
-- mission update (and its eye sync), and asserts on the transforms written to nodes,
-- the camera bound for rendering and the calls made to VRBridge.
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
local HALF_IPD = 0.032

local function near(a, b, eps)
    return math.abs(a - b) < (eps or EPS)
end

local function wrap(angle)
    while angle > math.pi do angle = angle - 2 * math.pi end
    while angle < -math.pi do angle = angle + 2 * math.pi end
    return angle
end

-- --------------------------------------------------------------------
-- Mock GIANTS scene graph: nodes with a parent, a local translation and a local
-- Euler rotation composed as R = Ry * Rx * Rz. Cameras look along local -Z.
-- --------------------------------------------------------------------
local ROOT = 1
local scene = {}
local engineNodes = {}
local engineWrites = {}
local nextNodeId = 5000
local bound = nil
local audioListener = nil
local fovByNode = {}

local function rotMatrix(rx, ry, rz)
    local ca, sa = math.cos(rx), math.sin(rx)
    local cb, sb = math.cos(ry), math.sin(ry)
    local cc, sc = math.cos(rz), math.sin(rz)
    return {
        { cb * cc + sb * sa * sc, -cb * sc + sb * sa * cc, sb * ca },
        { ca * sc, ca * cc, -sa },
        { -sb * cc + cb * sa * sc, sb * sc + cb * sa * cc, cb * ca },
    }
end

local function matMul(a, b)
    local m = { {}, {}, {} }
    for i = 1, 3 do
        for j = 1, 3 do
            m[i][j] = a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j]
        end
    end
    return m
end

local function matApply(m, x, y, z)
    return m[1][1] * x + m[1][2] * y + m[1][3] * z,
        m[2][1] * x + m[2][2] * y + m[2][3] * z,
        m[3][1] * x + m[3][2] * y + m[3][3] * z
end

local function worldTransform(id)
    local n = scene[id]
    if n == nil then
        return rotMatrix(0, 0, 0), 0, 0, 0
    end
    local r = rotMatrix(n.r[1], n.r[2], n.r[3])
    local x, y, z = n.t[1], n.t[2], n.t[3]
    if n.parent ~= 0 and scene[n.parent] ~= nil then
        local pr, px, py, pz = worldTransform(n.parent)
        local wx, wy, wz = matApply(pr, x, y, z)
        return matMul(pr, r), px + wx, py + wy, pz + wz
    end
    return r, x, y, z
end

-- Euler angles (rx, ry, rz) of a world rotation matrix, for R = Ry * Rx * Rz
local function worldEuler(id)
    local m = worldTransform(id)
    local s = math.max(-1, math.min(1, -m[2][3]))
    return math.asin(s), math.atan2(m[1][3], m[3][3]), math.atan2(m[2][1], m[2][2])
end

local function addNode(id, parent, t, r)
    scene[id] = { parent = parent, t = { t[1], t[2], t[3] }, r = { r[1], r[2], r[3] } }
    return id
end

local function newNode()
    nextNodeId = nextNodeId + 1
    addNode(nextNodeId, 0, { 0, 0, 0 }, { 0, 0, 0 })
    return nextNodeId
end

local function logEngineWrite(fnName, node)
    if engineNodes[node] then
        table.insert(engineWrites, string.format("%s(%s)", fnName, tostring(node)))
    end
end

local function installSceneGraph()
    scene = {}
    engineNodes = {}
    engineWrites = {}
    fovByNode = {}
    bound = nil
    audioListener = nil
    addNode(ROOT, 0, { 0, 0, 0 }, { 0, 0, 0 })

    _G.getRootNode = function() return ROOT end
    _G.getParent = function(node) return scene[node] and scene[node].parent or 0 end
    _G.link = function(parent, child)
        logEngineWrite("link", parent)
        logEngineWrite("link", child)
        if scene[child] == nil then addNode(child, 0, { 0, 0, 0 }, { 0, 0, 0 }) end
        scene[child].parent = parent
    end
    _G.setTranslation = function(node, x, y, z)
        logEngineWrite("setTranslation", node)
        if scene[node] == nil then addNode(node, 0, { 0, 0, 0 }, { 0, 0, 0 }) end
        scene[node].t = { x, y, z }
    end
    _G.setRotation = function(node, rx, ry, rz)
        logEngineWrite("setRotation", node)
        if scene[node] == nil then addNode(node, 0, { 0, 0, 0 }, { 0, 0, 0 }) end
        scene[node].r = { rx, ry, rz }
    end
    _G.getTranslation = function(node)
        local n = scene[node]
        if n == nil then return 0, 0, 0 end
        return n.t[1], n.t[2], n.t[3]
    end
    _G.getRotation = function(node)
        local n = scene[node]
        if n == nil then return 0, 0, 0 end
        return n.r[1], n.r[2], n.r[3]
    end
    _G.getWorldTranslation = function(node)
        local _, x, y, z = worldTransform(node)
        return x, y, z
    end
    _G.getWorldRotation = function(node)
        return worldEuler(node)
    end
    _G.localDirectionToWorld = function(node, x, y, z)
        local m = worldTransform(node)
        return matApply(m, x, y, z)
    end
    _G.createCamera = function(name, fovY, nearClip, farClip)
        local id = newNode()
        fovByNode[id] = fovY
        return id
    end
    _G.createTransformGroup = function(name)
        return newNode()
    end
    _G.setFovY = function(node, fovY)
        logEngineWrite("setFovY", node)
        fovByNode[node] = fovY
    end
    _G.getFovY = function(node) return fovByNode[node] or 1.0 end
    _G.getNearClip = function(node) return 0.1 end
    _G.getFarClip = function(node) return 2000 end
    _G.setProjectionOffset = function(node, x, y)
        logEngineWrite("setProjectionOffset", node)
    end
    _G.getProjectionOffset = function(node) return 0, 0 end
    _G.setAudioListener = function(node) audioListener = node end
    _G.g_cameraManager = {
        setActiveCamera = function(self, node) bound = node end
    }
end

local function removeSceneGraph()
    for _, name in ipairs({ "getRootNode", "getParent", "link", "setTranslation", "setRotation",
        "getTranslation", "getRotation", "getWorldTranslation", "getWorldRotation", "localDirectionToWorld",
        "createCamera", "createTransformGroup", "setFovY", "getFovY", "getNearClip", "getFarClip",
        "setProjectionOffset", "getProjectionOffset", "setAudioListener", "g_cameraManager",
        "g_currentMission", "VRBridge", "FSBaseMission" }) do
        _G[name] = nil
    end
end

-- --------------------------------------------------------------------
-- A vehicle pitched and rolled on a slope, with an orbit camera around it:
-- vehicle -> rotate node (orbit pitch/yaw) -> position node (zoom) -> camera node
-- --------------------------------------------------------------------
local VEHICLE, ROTATE, POSITION, CAMERA = 10, 11, 12, 13

local function installOrbitCamera(vehicleRot)
    addNode(VEHICLE, ROOT, { 120, 4, -60 }, vehicleRot or { 0.15, 0.7, -0.12 })
    addNode(ROTATE, VEHICLE, { 0, 2.5, 0 }, { -0.35, 2.4, 0 })
    addNode(POSITION, ROTATE, { 0, 0, 14 }, { 0, 0, 0 })
    addNode(CAMERA, POSITION, { 0, 0, 0 }, { 0, 0, 0 })
    for _, id in ipairs({ VEHICLE, ROTATE, POSITION, CAMERA }) do engineNodes[id] = true end
    local cam = {
        isInside = false, isRotatable = true,
        cameraNode = CAMERA, rotateNode = ROTATE, cameraPositionNode = POSITION,
        rotX = -0.35, rotY = 2.4, origRotX = -0.35, origRotY = 2.4, zoom = 14,
    }
    _G.g_currentMission = { controlledVehicle = { rootNode = VEHICLE, getActiveCamera = function() return cam end } }
    return cam
end

-- A fixed camera bolted to the vehicle (a hitch camera), aimed by the game with localRot
local function installFixedCamera(vehicleRot, localRot)
    addNode(VEHICLE, ROOT, { 120, 4, -60 }, vehicleRot)
    addNode(CAMERA, VEHICLE, { 0, 2.2, -3.5 }, localRot)
    engineNodes[VEHICLE] = true
    engineNodes[CAMERA] = true
    local cam = { isInside = false, isRotatable = false, cameraNode = CAMERA, rotX = localRot[1], rotY = localRot[2] }
    _G.g_currentMission = { controlledVehicle = { rootNode = VEHICLE, getActiveCamera = function() return cam end } }
    return cam
end

-- Fake VRBridge. state.mode is "AFR" or "SequentialDualPass"; state.parity / state.subPass
-- pick the eye; state.pose is the head pose (nil = invalid).
local function installBridge(state)
    state.discontinuities = 0
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        requiresAfrFallback = true,
        getIpd = function() return HALF_IPD * 2 end,
        getFrameParity = function() return state.parity or 0 end,
        getRenderingMode = function() return state.mode or "AFR" end,
        getSubPassIndex = function() return state.subPass or 0 end,
        setSubPassIndex = function(idx) state.subPass = idx end,
        getEyeProjection = function(eye)
            return { isValid = true, fovY = (eye == 1) and 1.25 or 1.05, xOffset = 0, yOffset = 0 }
        end,
        getHeadPose = function()
            if state.pose == nil then return { isValid = false } end
            return state.pose
        end,
        signalDiscontinuity = function() state.discontinuities = state.discontinuities + 1 end,
    }
end

local function resetCompanion()
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    VRCompanion.cyclopeanAudioListenerNode = nil
    VRCompanion.activeRenderingCamera = nil
    VRCompanion.activeRenderingCameraNode = nil
    VRCompanion.deferRenderingCameraBind = false
    VRCompanion.origCamSettings = {}
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:endEyeProjectionScope()
end

local function setup(state)
    installSceneGraph()
    installBridge(state)
    resetCompanion()
end

local function cleanup()
    resetCompanion()
    removeSceneGraph()
end

-- Heading of a world direction's horizontal projection (yaw that turns local -Z onto it)
local function headingOf(x, z)
    return math.atan2(-x, -z)
end

local function engineCameraHeading()
    local x, _, z = localDirectionToWorld(CAMERA, 0, 0, -1)
    return headingOf(x, z)
end

local function rotateY(x, y, z, yaw)
    local c, s = math.cos(yaw), math.sin(yaw)
    return x * c + z * s, y, -x * s + z * c
end

local function assertEngineUntouched()
    assert(#engineWrites == 0, "engine Outside Camera nodes must be read only, got: " .. table.concat(engineWrites, ", "))
end

print("Running Outside Camera Tests...")

test("BoundEyeCameraHasHeadPitchAndRollInWorldOnAPitchedAndRolledVehicle", function()
    local state = { mode = "AFR", parity = 0,
        pose = { isValid = true, x = 0, y = 0, z = 0, pitch = 0.25, yaw = -0.4, roll = 0.08, qx = 0, qy = 0, qz = 0, qw = 1 } }
    setup(state)
    local cam = installOrbitCamera()

    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    assert(bound ~= nil and bound ~= CAMERA, "an eye camera must be bound for rendering, got " .. tostring(bound))
    local pitch, _, roll = worldEuler(bound)
    assert(near(pitch, 0.25), string.format("world pitch must be the head pitch 0.25 (no vehicle tilt or orbit pitch), got %f", pitch))
    assert(near(roll, 0.08), string.format("world roll must be the head roll 0.08 (no vehicle roll), got %f", roll))
    assertEngineUntouched()
    cleanup()
end)

test("BoundEyeCameraHeadingIsTheEngineCameraHorizontalHeadingPlusHeadYaw", function()
    local state = { mode = "AFR", parity = 0,
        pose = { isValid = true, x = 0, y = 0, z = 0, pitch = 0.25, yaw = -0.4, roll = 0.08, qx = 0, qy = 0, qz = 0, qw = 1 } }
    setup(state)
    local cam = installOrbitCamera()

    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    local lx, _, lz = localDirectionToWorld(bound, 0, 0, -1)
    local delta = wrap(headingOf(lx, lz) - engineCameraHeading())
    assert(near(delta, -0.4), string.format("eye heading must be the engine camera heading plus head yaw -0.4, got %+f", delta))
    assertEngineUntouched()
    cleanup()
end)

-- Head yaw only, with a matching quaternion, so the per-eye offset lies along the eye's own right axis
local function leaningPose()
    local yaw = 0.3
    return { isValid = true, x = 0.12, y = 0.05, z = -0.2, pitch = 0, yaw = yaw, roll = 0,
        qx = 0, qy = math.sin(yaw / 2), qz = 0, qw = math.cos(yaw / 2) }
end

-- Engine camera world position plus the head translation rotated by the base heading
local function expectedHeadPosition()
    local bx, by, bz = getWorldTranslation(CAMERA)
    local hx, hy, hz = rotateY(0.12, 0.05, -0.2, engineCameraHeading())
    return bx + hx, by + hy, bz + hz
end

local function assertEyePlaced(eyeNode, eye, label)
    local hx, hy, hz = expectedHeadPosition()
    local rx, ry, rz = localDirectionToWorld(eyeNode, 1, 0, 0)
    local sign = (eye == 1) and 1 or -1
    local ex, ey, ez = getWorldTranslation(eyeNode)
    assert(near(ex, hx + sign * HALF_IPD * rx) and near(ey, hy + sign * HALF_IPD * ry) and near(ez, hz + sign * HALF_IPD * rz),
        string.format("%s: eye %d must sit at the head position plus its half-IPD offset, got (%f, %f, %f) expected (%f, %f, %f)",
            label, eye, ex, ey, ez, hx + sign * HALF_IPD * rx, hy + sign * HALF_IPD * ry, hz + sign * HALF_IPD * rz))
    local fov = (eye == 1) and 1.25 or 1.05
    assert(near(fovByNode[eyeNode], fov), string.format("%s: eye %d must get its own projection (fovY %f), got %s",
        label, eye, fov, tostring(fovByNode[eyeNode])))
end

test("AfrPlacesEachEyeAtTheEngineCameraPlusTheRotatedHeadTranslationAndEyeOffset", function()
    local state = { mode = "AFR", pose = leaningPose() }
    setup(state)
    local cam = installOrbitCamera()

    local eyeNodes = {}
    for eye = 0, 1 do
        state.parity = eye
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        eyeNodes[eye] = bound
        assertEyePlaced(bound, eye, "AFR")
    end
    assert(eyeNodes[0] ~= eyeNodes[1], "left and right eyes must render through their own Dual Eye Camera Nodes")

    local lx, ly, lz = getWorldTranslation(audioListener)
    local hx, hy, hz = expectedHeadPosition()
    assert(near(lx, hx) and near(ly, hy) and near(lz, hz), "the Cyclopean Audio Listener Node must follow the head centre")
    assertEngineUntouched()
    cleanup()
end)

test("SequentialDualPassPositionsTheCorrectEyeOnBothSubPassesIncludingTheSubPass1EyeSync", function()
    local state = { mode = "SequentialDualPass", subPass = 0, pose = leaningPose() }
    setup(state)
    local cam = installOrbitCamera()
    local missionUpdates = 0
    _G.FSBaseMission = {
        update = function(self, dt)
            missionUpdates = missionUpdates + 1
            -- the engine's vehicle camera update runs inside the mission update on sub-pass 0
            VRCompanion:onVehicleCameraUpdate(cam, dt)
        end
    }
    VRCompanion:hookMissionUpdate()

    local ok, err = pcall(function()
        local eyeNodes = {}
        for subPass = 0, 1 do
            state.subPass = subPass
            _G.FSBaseMission:update(16.6)
            eyeNodes[subPass] = bound
            assertEyePlaced(bound, subPass, "sub-pass " .. subPass)
        end
        assert(missionUpdates == 1, "only sub-pass 0 runs the mission update; sub-pass 1 relies on the eye sync")
        assert(eyeNodes[0] ~= eyeNodes[1], "each sub-pass must render through its own eye camera")
        assertEngineUntouched()
    end)
    VRCompanion:unhookMissionUpdate()
    cleanup()
    assert(ok, tostring(err))
end)

local function tiltedPose()
    return { isValid = true, x = 0.1, y = -0.05, z = 0.15, pitch = -0.2, yaw = 0.5, roll = -0.06, qx = 0, qy = 0, qz = 0, qw = 1 }
end

test("FixedOutsideCameraAimedDownwardStillYieldsALevelBase", function()
    local state = { mode = "AFR", parity = 1, pose = tiltedPose() }
    setup(state)
    -- A rear hitch camera, turned to face backwards and aimed 0.9 rad down at the drawbar
    local cam = installFixedCamera({ 0.15, 0.7, -0.12 }, { -0.9, math.pi, 0 })

    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    assert(bound ~= nil and bound ~= CAMERA, "an eye camera must be bound for rendering")
    local pitch, _, roll = worldEuler(bound)
    assert(near(pitch, -0.2), string.format("world pitch must be the head pitch alone, got %f", pitch))
    assert(near(roll, -0.06), string.format("world roll must be the head roll alone, got %f", roll))
    local lx, _, lz = localDirectionToWorld(bound, 0, 0, -1)
    assert(near(wrap(headingOf(lx, lz) - engineCameraHeading()), 0.5), "the base must keep the camera's own heading")
    assertEngineUntouched()
    cleanup()
end)

local function isFinite(v)
    return v == v and v ~= math.huge and v ~= -math.huge
end

test("CameraPointingStraightDownProducesFiniteStableTransforms", function()
    local state = { mode = "AFR", parity = 0, pose = tiltedPose() }
    setup(state)
    local headings = {}
    -- Exactly straight down, then a hair either side of vertical: the horizontal projection of the
    -- look direction flips by pi across vertical, the view must not
    for i, pitchOffset in ipairs({ 0, 1e-5, -1e-5, 1e-3, -1e-3 }) do
        local cam = installFixedCamera({ 0, 0.7, 0 }, { -math.pi / 2 + pitchOffset, 0, 0 })
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)

        assert(bound ~= nil and bound ~= CAMERA, "an eye camera must be bound for rendering")
        local x, y, z = getWorldTranslation(bound)
        local rx, ry, rz = getRotation(bound)
        for _, v in ipairs({ x, y, z, rx, ry, rz }) do
            assert(isFinite(v), string.format("transforms must be finite, got (%s, %s, %s) rot (%s, %s, %s)",
                tostring(x), tostring(y), tostring(z), tostring(rx), tostring(ry), tostring(rz)))
        end
        local pitch, heading, roll = worldEuler(bound)
        assert(near(pitch, -0.2) and near(roll, -0.06), "the base must stay level")
        headings[i] = heading
        assert(near(wrap(heading - headings[1]), 0, 0.01),
            string.format("heading must be stable near vertical: %f at offset %g vs %f straight down", heading, pitchOffset, headings[1]))
    end
    assertEngineUntouched()
    cleanup()
end)

test("In2DFloatingWindowModeTheOutsideCameraIsLeftToTheEngine", function()
    local state = { mode = "AFR", parity = 0, pose = tiltedPose() }
    setup(state)
    local cam = installOrbitCamera()
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    assert(bound ~= CAMERA, "precondition: head-tracked stereo renders through an eye camera")

    local ok, err = pcall(function()
        VRCompanion:set2DViewActive(true)
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        assert(bound == CAMERA, "2D Floating Window Mode must render through the engine's own camera, got " .. tostring(bound))
        assertEngineUntouched()
    end)
    VRCompanion.is2DViewActive = false
    cleanup()
    assert(ok, tostring(err))
end)

test("WithAGuiMenuOpenTheOutsideCameraIsLeftToTheEngine", function()
    local state = { mode = "AFR", parity = 1, pose = tiltedPose() }
    setup(state)
    local cam = installOrbitCamera()
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    assert(bound ~= CAMERA, "precondition: head-tracked stereo renders through an eye camera")

    VRCompanion.isMenuVisible = true
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    assert(bound == CAMERA, "an open GUI menu must leave the engine's own camera rendering, got " .. tostring(bound))
    assertEngineUntouched()
    cleanup()
end)

test("MenuFramesThroughTheCompanionUpdateNeverWriteTheEngineOutsideCameraNodes", function()
    local state = { mode = "AFR", parity = 0, pose = tiltedPose() }
    setup(state)
    local cam = installOrbitCamera()
    local guiVisible = true
    _G.g_gui = { getIsGuiVisible = function() return guiVisible end }
    -- With a menu open the engine renders through its own Outside Camera node
    _G.getCamera = function() return bound or CAMERA end

    local ok, err = pcall(function()
        -- The menu opens, the player keeps moving their head, then closes it
        for frame = 1, 3 do
            state.pose.x = 0.05 * frame
            state.pose.yaw = 0.1 * frame
            VRCompanion:onVehicleCameraUpdate(cam, 16.6)
            VRCompanion:update(16.6)
        end
        guiVisible = false
        VRCompanion:update(16.6)
        assertEngineUntouched()
    end)
    _G.g_gui = nil
    _G.getCamera = nil
    VRCompanion.menuCameraOffsets = {}
    VRCompanion.menuCamTicks = 0
    VRCompanion.guiVisibilityInitialized = false
    VRCompanion.lastControlledVehicle = nil
    cleanup()
    assert(ok, tostring(err))
end)

-- The view shows exactly what the engine camera sees: its world position and full world rotation
local function assertShowsEngineView(node, label)
    local ex, ey, ez = getWorldTranslation(CAMERA)
    local x, y, z = getWorldTranslation(node)
    assert(near(x, ex) and near(y, ey) and near(z, ez),
        string.format("%s: the view must come from the engine camera's position, got (%f, %f, %f) expected (%f, %f, %f)", label, x, y, z, ex, ey, ez))
    local m = worldTransform(node)
    local em = worldTransform(CAMERA)
    for i = 1, 3 do
        for j = 1, 3 do
            assert(near(m[i][j], em[i][j]), string.format("%s: the view must have the engine camera's world rotation", label))
        end
    end
end

test("WithAnInvalidHeadPoseTheOutsideCameraShowsTheEngineViewInsteadOfFreezing", function()
    local state = { mode = "AFR", parity = 0, pose = tiltedPose() }
    setup(state)
    local cam = installOrbitCamera()
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    -- Tracking drops out while the vehicle drives on and the player orbits
    state.pose = nil
    scene[VEHICLE].t = { 135, 4.5, -48 }
    scene[ROTATE].r = { -0.5, 1.9, 0 }
    for eye = 0, 1 do
        state.parity = eye
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        assertShowsEngineView(bound, "AFR eye " .. eye)
    end
    assertEngineUntouched()
    cleanup()
end)

test("WithAnInvalidHeadPoseSequentialDualPassSubPassesShowTheEngineView", function()
    local state = { mode = "SequentialDualPass", subPass = 0, pose = nil }
    setup(state)
    local cam = installOrbitCamera()
    _G.FSBaseMission = { update = function(self, dt) VRCompanion:onVehicleCameraUpdate(cam, dt) end }
    VRCompanion:hookMissionUpdate()

    local ok, err = pcall(function()
        for subPass = 0, 1 do
            state.subPass = subPass
            _G.FSBaseMission:update(16.6)
            assertShowsEngineView(bound, "sub-pass " .. subPass)
        end
        assertEngineUntouched()
    end)
    VRCompanion:unhookMissionUpdate()
    cleanup()
    assert(ok, tostring(err))
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_outside_camera.luau", failed))
