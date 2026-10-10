-- ====================================================================
-- Test Suite for Head Rotation: the head's rotation reaches the view intact at any head yaw
-- Drives the in-cab and on-foot camera updates on a mock scene graph whose engine reads Euler
-- angles in either of two conventions, and asserts on the world rotation of the camera the
-- engine renders through.
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

local EPS = 1e-5

local function near(a, b, eps)
    return math.abs(a - b) < (eps or EPS)
end

-- --------------------------------------------------------------------
-- Mock GIANTS scene graph: nodes with a parent, a local translation and a local rotation
-- matrix. Cameras look along local -Z.
-- --------------------------------------------------------------------
local ROOT = 1
local scene = {}
local nextNodeId = 5000
local bound = nil
local managerActive = nil

local function matMul(a, b)
    local m = { {}, {}, {} }
    for i = 1, 3 do
        for j = 1, 3 do
            m[i][j] = a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j]
        end
    end
    return m
end

local function transpose(m)
    return { { m[1][1], m[2][1], m[3][1] }, { m[1][2], m[2][2], m[3][2] }, { m[1][3], m[2][3], m[3][3] } }
end

local function matApply(m, x, y, z)
    return m[1][1] * x + m[1][2] * y + m[1][3] * z,
        m[2][1] * x + m[2][2] * y + m[2][3] * z,
        m[3][1] * x + m[3][2] * y + m[3][3] * z
end

local function axisRotation(axis, angle)
    local c, s = math.cos(angle), math.sin(angle)
    if axis == "X" then return { { 1, 0, 0 }, { 0, c, -s }, { 0, s, c } } end
    if axis == "Y" then return { { c, 0, s }, { 0, 1, 0 }, { -s, 0, c } } end
    return { { c, -s, 0 }, { s, c, 0 }, { 0, 0, 1 } }
end

-- How the fake engine reads Euler angles: "YXZ" composes R = Ry * Rx * Rz, "ZYX" composes
-- R = Rz * Ry * Rx. Whichever the engine uses, the head's rotation must reach the view intact.
local eulerOrder = "YXZ"

local function rotMatrix(rx, ry, rz)
    local angles = { X = rx, Y = ry, Z = rz }
    local m = axisRotation(string.sub(eulerOrder, 1, 1), angles[string.sub(eulerOrder, 1, 1)])
    for i = 2, 3 do
        local axis = string.sub(eulerOrder, i, i)
        m = matMul(m, axisRotation(axis, angles[axis]))
    end
    return m
end

-- Within this cosine of a convention's singularity (for "ZYX" a yaw of +-90 degrees, for "YXZ"
-- a pitch of +-90 degrees) the engine's single-precision extraction reads only rounding noise
local SINGULARITY_COS = 1e-3

-- Euler angles (rx, ry, rz) of a rotation matrix in the fake engine's convention. At the
-- singularity the other two angles turn about one axis and are read from rounding noise, which
-- is modelled as reading them as zero: the view stops pitching while yaw still works.
local function eulerOf(m)
    if eulerOrder == "ZYX" then
        local s = math.max(-1, math.min(1, -m[3][1]))
        if math.sqrt(1 - s * s) < SINGULARITY_COS then
            return 0, math.asin(s), 0
        end
        return math.atan2(m[3][2], m[3][3]), math.asin(s), math.atan2(m[2][1], m[1][1])
    end
    local s = math.max(-1, math.min(1, -m[2][3]))
    if math.sqrt(1 - s * s) < SINGULARITY_COS then
        return math.asin(s), 0, 0
    end
    return math.asin(s), math.atan2(m[1][3], m[3][3]), math.atan2(m[2][1], m[2][2])
end

local function quatMatrix(x, y, z, w)
    return {
        { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
        { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
        { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) },
    }
end

local function matQuat(m)
    local trace = m[1][1] + m[2][2] + m[3][3]
    if trace > 0 then
        local s = math.sqrt(trace + 1) * 2
        return (m[3][2] - m[2][3]) / s, (m[1][3] - m[3][1]) / s, (m[2][1] - m[1][2]) / s, 0.25 * s
    elseif m[1][1] > m[2][2] and m[1][1] > m[3][3] then
        local s = math.sqrt(1 + m[1][1] - m[2][2] - m[3][3]) * 2
        return 0.25 * s, (m[1][2] + m[2][1]) / s, (m[1][3] + m[3][1]) / s, (m[3][2] - m[2][3]) / s
    elseif m[2][2] > m[3][3] then
        local s = math.sqrt(1 + m[2][2] - m[1][1] - m[3][3]) * 2
        return (m[1][2] + m[2][1]) / s, 0.25 * s, (m[2][3] + m[3][2]) / s, (m[1][3] - m[3][1]) / s
    end
    local s = math.sqrt(1 + m[3][3] - m[1][1] - m[2][2]) * 2
    return (m[1][3] + m[3][1]) / s, (m[2][3] + m[3][2]) / s, 0.25 * s, (m[2][1] - m[1][2]) / s
end

local function addNode(id, parent, t, r)
    scene[id] = { parent = parent, t = { t[1], t[2], t[3] }, m = rotMatrix(r[1], r[2], r[3]) }
    return id
end

local function newNode()
    nextNodeId = nextNodeId + 1
    addNode(nextNodeId, 0, { 0, 0, 0 }, { 0, 0, 0 })
    return nextNodeId
end

local function ensureNode(node)
    if scene[node] == nil then addNode(node, 0, { 0, 0, 0 }, { 0, 0, 0 }) end
    return scene[node]
end

local function worldTransform(id)
    local n = scene[id]
    if n == nil then
        return rotMatrix(0, 0, 0), 0, 0, 0
    end
    local x, y, z = n.t[1], n.t[2], n.t[3]
    if n.parent ~= 0 and scene[n.parent] ~= nil then
        local pr, px, py, pz = worldTransform(n.parent)
        local wx, wy, wz = matApply(pr, x, y, z)
        return matMul(pr, n.m), px + wx, py + wy, pz + wz
    end
    return n.m, x, y, z
end

local function installSceneGraph()
    scene = {}
    bound = nil
    addNode(ROOT, 0, { 0, 0, 0 }, { 0, 0, 0 })

    _G.getRootNode = function() return ROOT end
    _G.getParent = function(node) return scene[node] and scene[node].parent or 0 end
    _G.link = function(parent, child) ensureNode(child).parent = parent end
    _G.setTranslation = function(node, x, y, z) ensureNode(node).t = { x, y, z } end
    _G.setRotation = function(node, rx, ry, rz) ensureNode(node).m = rotMatrix(rx, ry, rz) end
    _G.setQuaternion = function(node, x, y, z, w) ensureNode(node).m = quatMatrix(x, y, z, w) end
    _G.getTranslation = function(node)
        local n = scene[node]
        if n == nil then return 0, 0, 0 end
        return n.t[1], n.t[2], n.t[3]
    end
    _G.getRotation = function(node)
        local n = scene[node]
        if n == nil then return 0, 0, 0 end
        return eulerOf(n.m)
    end
    _G.getQuaternion = function(node)
        local n = scene[node]
        if n == nil then return 0, 0, 0, 1 end
        return matQuat(n.m)
    end
    _G.getWorldTranslation = function(node)
        local _, x, y, z = worldTransform(node)
        return x, y, z
    end
    _G.getWorldRotation = function(node) return eulerOf((worldTransform(node))) end
    _G.getWorldQuaternion = function(node) return matQuat((worldTransform(node))) end
    _G.localDirectionToWorld = function(node, x, y, z) return matApply((worldTransform(node)), x, y, z) end
    _G.localToLocal = function(fromNode, toNode, x, y, z)
        local fm, fx, fy, fz = worldTransform(fromNode)
        local wx, wy, wz = matApply(fm, x, y, z)
        local tm, tx, ty, tz = worldTransform(toNode)
        return matApply(transpose(tm), fx + wx - tx, fy + wy - ty, fz + wz - tz)
    end
    _G.localRotationToLocal = function(fromNode, toNode, rx, ry, rz)
        local fm = worldTransform(fromNode)
        local tm = worldTransform(toNode)
        return eulerOf(matMul(transpose(tm), matMul(fm, rotMatrix(rx, ry, rz))))
    end
    _G.createCamera = function() return newNode() end
    _G.createTransformGroup = function() return newNode() end
    _G.setFovY = function() end
    _G.getFovY = function() return 1.0 end
    _G.setProjectionOffset = function() end
    _G.getProjectionOffset = function() return 0, 0 end
    _G.setAudioListener = function() end
    local registered = {}
    managerActive = nil
    _G.g_cameraManager = {
        addCamera = function(self, node) registered[node] = true end,
        removeCamera = function(self, node) registered[node] = nil end,
        setActiveCamera = function(self, node)
            bound = node
            managerActive = node
        end,
        getActiveCamera = function(self) return managerActive end,
    }
    _G.setCamera = function() end
    _G.getCamera = function() return bound end
end

local function removeSceneGraph()
    for _, name in ipairs({ "getRootNode", "getParent", "link", "setTranslation", "setRotation", "setQuaternion",
        "getTranslation", "getRotation", "getQuaternion", "getWorldTranslation", "getWorldRotation", "getWorldQuaternion",
        "localDirectionToWorld", "localToLocal", "localRotationToLocal", "createCamera", "createTransformGroup",
        "setFovY", "getFovY", "setProjectionOffset", "getProjectionOffset", "setAudioListener", "g_cameraManager",
        "setCamera", "getCamera", "g_currentMission", "VRBridge", "isHeadTrackingAvailable", "getHeadTrackingRotation",
        "getHeadTrackingTranslation" }) do
        _G[name] = nil
    end
end

-- A head pose as VRBridge reports it: Euler angles composed R = Ry(yaw) * Rx(pitch) * Rz(roll),
-- and the matching quaternion
local function headMatrix(pitch, yaw, roll)
    return matMul(axisRotation("Y", yaw), matMul(axisRotation("X", pitch), axisRotation("Z", roll)))
end

local function headPose(pitch, yaw, roll)
    local qx, qy, qz, qw = matQuat(headMatrix(pitch, yaw, roll))
    return { isValid = true, x = 0.04, y = 0.02, z = -0.06, pitch = pitch, yaw = yaw, roll = roll,
        qx = qx, qy = qy, qz = qz, qw = qw }
end

local function installBridge(state)
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        requiresAfrFallback = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return 0 end,
        getRenderingMode = function() return "AFR" end,
        getEyeProjection = function() return { isValid = true, fovY = 1.1, xOffset = 0, yOffset = 0 } end,
        getHeadPose = function()
            if state.pose == nil then return { isValid = false } end
            return state.pose
        end,
    }
end

local function resetCompanion()
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    VRCompanion.cyclopeanAudioListenerNode = nil
    VRCompanion.activeRenderingCamera = nil
    VRCompanion.registeredEyeCameras = nil
    VRCompanion.gameCameraNode = nil
    VRCompanion.outsideCameraBaseNode = nil
    VRCompanion.deferRenderingCameraBind = false
    VRCompanion.origCamSettings = {}
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion.headPoseScratch = nil
    VRCompanion.trackIrPoseScratch = nil
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

local function assertWorldRotation(node, expected, label)
    local actual = worldTransform(node)
    for i = 1, 3 do
        for j = 1, 3 do
            assert(near(actual[i][j], expected[i][j]), string.format(
                "%s: world rotation row %d col %d is %f, expected %f (look direction (%f, %f, %f), expected (%f, %f, %f))",
                label, i, j, actual[i][j], expected[i][j], -actual[1][3], -actual[2][3], -actual[3][3],
                -expected[1][3], -expected[2][3], -expected[3][3]))
        end
    end
end

-- --------------------------------------------------------------------
-- An in-cab camera on a vehicle standing on a slope, looking out of the windshield
-- (rotation "-18 180 0"), with the game's level head tracking node beside it
-- --------------------------------------------------------------------
local VEHICLE, CAB_CAMERA, CAB_HEAD_NODE = 10, 11, 12

local function installCabCamera()
    addNode(VEHICLE, ROOT, { 120, 4, -60 }, { 0.06, 0.7, -0.04 })
    addNode(CAB_CAMERA, VEHICLE, { 0.3, 2.1, 0.4 }, { math.rad(-18), math.pi, 0 })
    addNode(CAB_HEAD_NODE, VEHICLE, { 0.3, 2.1, 0.4 }, { 0, math.pi, 0 })
    local cam = {
        isInside = true, cameraNode = CAB_CAMERA, cameraPositionNode = CAB_CAMERA, headTrackingNode = CAB_HEAD_NODE,
        origRotX = math.rad(-18), rotX = math.rad(-18), origRotY = math.pi, rotY = math.pi,
    }
    _G.g_currentMission = { controlledVehicle = { rootNode = VEHICLE, getActiveCamera = function() return cam end } }
    g_cameraManager:setActiveCamera(CAB_CAMERA)
    return cam
end

-- --------------------------------------------------------------------
-- The on-foot camera: player yaw node -> pitch node (held level in VR) -> first person camera
-- --------------------------------------------------------------------
local PLAYER_YAW, PLAYER_PITCH, PLAYER_CAMERA = 20, 21, 22

local function installPlayerCamera()
    addNode(PLAYER_YAW, ROOT, { 40, 1.1, 25 }, { 0, 0.9, 0 })
    addNode(PLAYER_PITCH, PLAYER_YAW, { 0, 0.7, 0 }, { 0, 0, 0 })
    addNode(PLAYER_CAMERA, PLAYER_PITCH, { 0, 0, 0 }, { 0, math.pi, 0 })
    local cam = { firstPersonCamera = PLAYER_CAMERA, cameraNode = PLAYER_CAMERA, yawNode = PLAYER_YAW, pitchNode = PLAYER_PITCH }
    g_cameraManager:setActiveCamera(PLAYER_CAMERA)
    return cam
end

local HEAD_YAWS = { math.pi / 2, -math.pi / 2, 0.4, 2.1, -2.1 }

print("Running Head Rotation Tests...")

test("InTheCabEveryHeadYawKeepsTheHeadsPitchAndRollInEitherEulerConvention", function()
    for _, order in ipairs({ "YXZ", "ZYX" }) do
        for _, yaw in ipairs(HEAD_YAWS) do
            eulerOrder = order
            local state = { pose = headPose(0.35, yaw, 0.1) }
            setup(state)
            local ok, err = pcall(function()
                local cam = installCabCamera()
                VRCompanion:onVehicleCameraUpdate(cam, 16.6)

                local label = string.format("%s, head yaw %.3f", order, yaw)
                assert(bound ~= nil and bound ~= CAB_CAMERA, label .. ": an eye camera must be bound for rendering")
                local expected = matMul((worldTransform(CAB_HEAD_NODE)), headMatrix(0.35, yaw, 0.1))
                assertWorldRotation(bound, expected, label .. ", eye camera")
                assertWorldRotation(CAB_CAMERA, expected, label .. ", engine camera")
            end)
            cleanup()
            eulerOrder = "YXZ"
            assert(ok, tostring(err))
        end
    end
end)

test("InTheCabAfterMouseLookTheHeadRotationComposesOnTheTurnedView", function()
    for _, order in ipairs({ "YXZ", "ZYX" }) do
        eulerOrder = order
        local state = { pose = headPose(-0.3, 0.25, 0.05) }
        setup(state)
        local ok, err = pcall(function()
            local cam = installCabCamera()
            -- The player turned the view a quarter turn to the left with the mouse or a snap turn
            cam.rotY = math.pi + math.pi / 2
            VRCompanion:onVehicleCameraUpdate(cam, 16.6)

            local vehicle = worldTransform(VEHICLE)
            local expected = matMul(vehicle, matMul(axisRotation("Y", math.pi + math.pi / 2), headMatrix(-0.3, 0.25, 0.05)))
            assertWorldRotation(bound, expected, order .. ", eye camera")
            assertWorldRotation(CAB_CAMERA, expected, order .. ", engine camera")
        end)
        cleanup()
        eulerOrder = "YXZ"
        assert(ok, tostring(err))
    end
end)

test("OnFootEveryHeadYawKeepsTheHeadsPitchAndRollInEitherEulerConvention", function()
    for _, order in ipairs({ "YXZ", "ZYX" }) do
        for _, yaw in ipairs(HEAD_YAWS) do
            eulerOrder = order
            local state = { pose = headPose(0.35, yaw, 0.1) }
            setup(state)
            local ok, err = pcall(function()
                local cam = installPlayerCamera()
                VRCompanion:onPlayerCameraUpdate(cam, 16.6)

                local label = string.format("%s, head yaw %.3f", order, yaw)
                assert(bound ~= nil and bound ~= PLAYER_CAMERA, label .. ": an eye camera must be bound for rendering")
                -- The mod's head tracking node: the player's yaw node turned to face forward
                local base = matMul((worldTransform(PLAYER_YAW)), axisRotation("Y", math.pi))
                local expected = matMul(base, headMatrix(0.35, yaw, 0.1))
                assertWorldRotation(bound, expected, label .. ", eye camera")
                assertWorldRotation(PLAYER_CAMERA, expected, label .. ", engine camera")
            end)
            cleanup()
            eulerOrder = "YXZ"
            assert(ok, tostring(err))
        end
    end
end)

test("ATrackIrPoseWithoutAQuaternionStillPitchesTheCabViewAtAQuarterTurn", function()
    for _, order in ipairs({ "YXZ", "ZYX" }) do
        eulerOrder = order
        local state = { pose = nil }
        setup(state)
        _G.isHeadTrackingAvailable = function() return true end
        _G.getHeadTrackingTranslation = function() return 0, 0, 0 end
        _G.getHeadTrackingRotation = function() return 0.3, math.pi / 2, 0 end
        local ok, err = pcall(function()
            local cam = installCabCamera()
            VRCompanion:onVehicleCameraUpdate(cam, 16.6)

            local expected = matMul((worldTransform(CAB_HEAD_NODE)), headMatrix(0.3, math.pi / 2, 0))
            assertWorldRotation(CAB_CAMERA, expected, order .. ", engine camera")
        end)
        cleanup()
        eulerOrder = "YXZ"
        assert(ok, tostring(err))
    end
end)

test("WithTrackingLostTheViewKeepsTheEngineCamerasPitchWhenItFacesAQuarterTurn", function()
    for _, order in ipairs({ "YXZ", "ZYX" }) do
        eulerOrder = order
        local state = { pose = nil }
        setup(state)
        local ok, err = pcall(function()
            local cam = installCabCamera()
            -- The vehicle stands level, turned so the engine camera faces a quarter turn in the world
            scene[VEHICLE].m = axisRotation("Y", -math.pi / 2)
            VRCompanion:onVehicleCameraUpdate(cam, 16.6)

            assert(bound ~= nil, "a camera must be bound for rendering")
            assertWorldRotation(bound, (worldTransform(CAB_CAMERA)), order .. ", the view")
        end)
        cleanup()
        eulerOrder = "YXZ"
        assert(ok, tostring(err))
    end
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_head_rotation.luau", failed))
