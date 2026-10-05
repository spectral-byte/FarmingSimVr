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

-- Mock VRBridge that honours the output-table argument exactly like the native binding.
local poseTablesSeen = {}
local projTablesSeen = {}
local poseCalls, projCalls = 0, 0
local mockVRBridge = {
    version = "1.0.0.0",
    isReady = function() return true end,
    handshake = function() return { success = true, version = "1.0.0.0", mode = "OpenXR" } end,
    getHeadPose = function(out)
        poseCalls = poseCalls + 1
        out = out or {}
        poseTablesSeen[out] = true
        out.x, out.y, out.z = 0.1, 0.05, -0.1
        out.pitch, out.yaw, out.roll = 0.15, 0.2, 0.0
        out.isValid = true
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
    assert(count(poseTablesSeen) == 1, "bridge must only ever be handed one pose table")
end)

test("EyeProjectionReadsReuseOneTableAcrossCalls", function()
    local before = projCalls
    VRCompanion:applyCameraProjection(7, 0)
    VRCompanion:applyCameraProjection(7, 1)
    VRCompanion:applyCameraProjection(7, 0)
    assert(projCalls - before == 3, "getEyeProjection must be read each time")
    assert(count(projTablesSeen) == 1, "bridge must only ever be handed one projection table")
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
    assert(count(poseTablesSeen) == 1, "pose table must be reused across updates")
    assert(count(projTablesSeen) == 1, "projection table must be reused across updates")
end)

test("TrackIrFallbackKeepsReusedTableAndDropsStaleQuaternion", function()
    -- Bridge pose invalid: fall back to TrackIR, which has no quaternion.
    local original = mockVRBridge.getHeadPose
    mockVRBridge.getHeadPose = function(out)
        out.isValid = false
        out.qx, out.qy, out.qz, out.qw = 0.5, 0.5, 0.5, 0.5
        return out
    end
    _G.isHeadTrackingAvailable = function() return true end
    _G.getHeadTrackingTranslation = function() return 1, 2, 3 end
    _G.getHeadTrackingRotation = function() return 0.1, 0.2, 0.3 end

    local pose = VRCompanion:getResolvedHeadPose()
    assert(pose == VRCompanion.headPoseScratch, "fallback must reuse the scratch table")
    assert(pose.isValid == true and pose.x == 1 and pose.yaw == 0.2, "fallback values applied")
    assert(pose.qx == nil and pose.qw == nil, "stale bridge quaternion must be cleared")

    mockVRBridge.getHeadPose = original
    _G.isHeadTrackingAvailable = nil
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_reusable_tables.luau", failed))
