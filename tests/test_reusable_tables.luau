-- ====================================================================
-- Test Suite: Companion Mod reuses head pose / eye projection tables (Issue #12)
-- VRBridge fills a caller-supplied table in place, so per-frame reads must not allocate.
-- ====================================================================

_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

local nodeTrans = { 0.0, 0.0, 0.0 }
local nodeRot = { 0.0, 0.0, 0.0 }
function setTranslation(node, x, y, z) nodeTrans = { x, y, z } end
function getTranslation(node) return nodeTrans[1], nodeTrans[2], nodeTrans[3] end
function setRotation(node, rx, ry, rz) nodeRot = { rx, ry, rz } end
function getRotation(node) return nodeRot[1], nodeRot[2], nodeRot[3] end
function setFovY(node, fov) end
function getFovY(node) return 1.0 end
function setProjectionOffset(node, x, y) end
function getProjectionOffset(node) return 0.0, 0.0 end
_G.MathUtil = {
    getValidLimit = function(angle)
        while angle > math.pi do angle = angle - 2 * math.pi end
        while angle < -math.pi do angle = angle + 2 * math.pi end
        return angle
    end
}

-- Mock VRBridge that honours the output-table argument like the native binding: a supplied
-- head pose table is filled in place only when it already holds every field, otherwise a
-- fresh table is returned.
local POSE_FIELDS = { "x", "y", "z", "pitch", "yaw", "roll", "qx", "qy", "qz", "qw",
    "isValid", "isPositionTracked", "isOrientationTracked" }
local function hasAllFields(t, fields)
    for _, k in ipairs(fields) do
        if t[k] == nil then return false end
    end
    return true
end
local poseTablesReturned = {}
local projTablesSeen = {}
local poseCalls, projCalls = 0, 0
local mockVRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    handshake = function() return { success = true, version = "1.0.0.0", mode = "OpenXR" } end,
    getHeadPose = function(out)
        poseCalls = poseCalls + 1
        if type(out) ~= "table" or not hasAllFields(out, POSE_FIELDS) then
            out = {}
        end
        poseTablesReturned[out] = true
        out.x, out.y, out.z = 0.1, 0.05, -0.1
        out.pitch, out.yaw, out.roll = 0.15, 0.2, 0.0
        out.qx, out.qy, out.qz, out.qw = 0.0, 0.0, 0.0, 1.0
        out.isValid = true
        out.isPositionTracked, out.isOrientationTracked = true, true
        return out
    end,
    getEyeProjection = function(eye, out)
        projCalls = projCalls + 1
        out = out or {}
        projTablesSeen[out] = true
        out.isValid = true
        out.fovY = 1.8326
        out.xOffset, out.yOffset = 0.0, 0.0
        out.eye = eye
        return out
    end,
    getFrameParity = function() return 0 end,
    getIpd = function() return 0.064 end,
    setMenuVisible = function() end,
    alignMenuQuad = function() end,
    recenter = function() end,
    resetTemporalHistory = function() end,
    onMapUnload = function() end
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

local function count(set)
    local n = 0
    for _ in pairs(set) do n = n + 1 end
    return n
end

print("Running Reusable Pose/Projection Table Tests (Issue #12)...")

test("HeadPoseReadsReuseOneTableAcrossCalls", function()
    local before = poseCalls
    local first = VRCompanion:getResolvedHeadPose()
    local second = VRCompanion:getResolvedHeadPose()
    local third = VRCompanion:getResolvedHeadPose()
    assert(poseCalls - before == 3, "getHeadPose must be read each time")
    assert(first == second and second == third, "head pose table identity must be stable")
    assert(first.isValid == true and math.abs(first.pitch - 0.15) < 1e-6, "pose must carry bridge values")
    assert(count(poseTablesReturned) == 1, "bridge must only ever return one pose table")
end)

test("EyeProjectionReadsReuseOneTableAcrossCalls", function()
    local before = projCalls
    VRCompanion:applyCameraProjection(7, 0)
    VRCompanion:applyCameraProjection(7, 1)
    VRCompanion:applyCameraProjection(7, 0)
    assert(projCalls - before == 3, "getEyeProjection must be read each time")
    assert(count(projTablesSeen) == 2, "bridge must only ever be handed one projection table per eye")
end)

test("PerFrameVehicleCameraUpdatesReuseBothTables", function()
    local cam = {
        isInside = true, cameraNode = 10,
        origRotY = 3.14159, rotY = 3.14159, rotX = 0.0, rotZ = 0.0,
        transX = 0.5, transY = 2.0, transZ = 1.0,
        origTransX = 0.5, origTransY = 2.0, origTransZ = 1.0,
    }
    for _ = 1, 5 do
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    end
    assert(count(poseTablesReturned) == 1, "pose table must be reused across updates")
    assert(count(projTablesSeen) <= 2, "projection tables (one per eye) must be reused across updates")
end)

test("TrackIrFallbackReusesItsTableAndKeepsTheBridgeTableComplete", function()
    -- Bridge pose invalid: fall back to TrackIR, which has no quaternion.
    local original = mockVRBridge.getHeadPose
    local bridgeValid = false
    mockVRBridge.getHeadPose = function(out)
        local ret = original(out)
        ret.isValid = bridgeValid
        return ret
    end
    _G.isHeadTrackingAvailable = function() return true end
    _G.getHeadTrackingTranslation = function() return 1, 2, 3 end
    _G.getHeadTrackingRotation = function() return 0.1, 0.2, 0.3 end

    VRCompanion:getResolvedHeadPose()
    local returnedBefore = count(poseTablesReturned)
    local first = VRCompanion:getResolvedHeadPose()
    for _ = 1, 5 do
        assert(VRCompanion:getResolvedHeadPose() == first, "fallback pose table identity must be stable")
    end
    assert(count(poseTablesReturned) == returnedBefore, "the fallback must not make VRBridge allocate a fresh pose table")
    assert(first ~= VRCompanion.headPoseScratch, "TrackIR values must not be written into the VRBridge table")
    assert(first.isValid == true and first.x == 1 and first.yaw == 0.2, "fallback values applied")
    assert(first.qx == nil and first.qw == nil, "the TrackIR pose carries no quaternion")

    bridgeValid = true
    local bridgePose = VRCompanion:getResolvedHeadPose()
    assert(bridgePose == VRCompanion.headPoseScratch and bridgePose.qw == 1.0, "a valid bridge pose is used again")

    mockVRBridge.getHeadPose = original
    _G.isHeadTrackingAvailable = nil
end)

test("PerFrameOutsideCameraUpdatesReuseBothTables", function()
    -- An orbit Outside Camera at (10, 2, 30) looking along world +X, rendered through world-space eye nodes
    local nextNode = 800
    _G.createCamera = function() nextNode = nextNode + 1; return nextNode end
    _G.getRootNode = function() return 1 end
    _G.getParent = function() return 1 end
    _G.link = function() end
    _G.getWorldTranslation = function() return 10.0, 2.0, 30.0 end
    _G.localDirectionToWorld = function(node, x, y, z) return -z, y, x end
    local cam = { isInside = false, isRotatable = true, cameraNode = 20, rotX = -0.3, rotY = 1.2 }

    local ok, err = pcall(function()
        VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        local poseBefore, projBefore = count(poseTablesReturned), count(projTablesSeen)
        for _ = 1, 5 do
            VRCompanion:onVehicleCameraUpdate(cam, 16.6)
        end
        assert(count(poseTablesReturned) == poseBefore and count(projTablesSeen) == projBefore,
            string.format("Outside Camera frames must reuse the pose and projection tables, got %d new pose and %d new projection tables",
                count(poseTablesReturned) - poseBefore, count(projTablesSeen) - projBefore))
    end)
    for _, name in ipairs({ "createCamera", "getRootNode", "getParent", "link", "getWorldTranslation", "localDirectionToWorld" }) do
        _G[name] = nil
    end
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    assert(ok, tostring(err))
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_reusable_tables.luau", failed))
