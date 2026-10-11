-- ====================================================================
-- Test Suite for Vehicle In-Cab Mouse & Controller Yaw View Rotation (Issue #29)
-- ====================================================================

-- Mock GIANTS Engine globals before loading VRCompanion
_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

-- Load the Companion Mod module files in modDesc order, exactly as the engine does
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

print("Running Vehicle In-Cab Yaw View Rotation Tests...")

-- Slice 1: Mathematical offset calculation and angle wrapping
test("CalculatesVehicleLookYawFromMouseAndControllerOffset", function()
    -- Standard look to the right: rotY > origRotY
    local camRight = {
        rotY = 0.75,
        origRotY = 0.25
    }
    local yawRight = VRCompanion:getVehicleLookYaw(camRight)
    assert(math.abs(yawRight - 0.50) < 0.0001, string.format("Expected 0.50 look yaw, got %f", yawRight))

    -- Standard look to the left: rotY < origRotY
    local camLeft = {
        rotY = -0.30,
        origRotY = 0.20
    }
    local yawLeft = VRCompanion:getVehicleLookYaw(camLeft)
    assert(math.abs(yawLeft - (-0.50)) < 0.0001, string.format("Expected -0.50 look yaw, got %f", yawLeft))

    -- Neutral forward orientation
    local camNeutral = {
        rotY = 1.20,
        origRotY = 1.20
    }
    local yawNeutral = VRCompanion:getVehicleLookYaw(camNeutral)
    assert(math.abs(yawNeutral - 0.0) < 0.0001, string.format("Neutral orientation must yield 0, got %f", yawNeutral))

    -- When origRotY is nil, treat rotY as raw offset
    local camNoOrig = {
        rotY = 0.42
    }
    local yawNoOrig = VRCompanion:getVehicleLookYaw(camNoOrig)
    assert(math.abs(yawNoOrig - 0.42) < 0.0001, string.format("Expected 0.42, got %f", yawNoOrig))

    -- Nil camera or nil rotY returns 0
    assert(VRCompanion:getVehicleLookYaw(nil) == 0, "Nil cam must return 0")
    assert(VRCompanion:getVehicleLookYaw({}) == 0, "Empty cam must return 0")
end)

test("NormalizesVehicleLookYawAroundPiBoundaries", function()
    -- Look yaw wrapping beyond +pi
    local camWrapPos = {
        rotY = 3.5,
        origRotY = 0.0
    }
    local yawWrapPos = VRCompanion:getVehicleLookYaw(camWrapPos)
    local expectedPos = 3.5 - 2 * math.pi
    assert(math.abs(yawWrapPos - expectedPos) < 0.0001, string.format("Angles > pi must wrap to negative, got %f", yawWrapPos))

    -- Look yaw wrapping beyond -pi
    local camWrapNeg = {
        rotY = -3.8,
        origRotY = 0.0
    }
    local yawWrapNeg = VRCompanion:getVehicleLookYaw(camWrapNeg)
    local expectedNeg = -3.8 + 2 * math.pi
    assert(math.abs(yawWrapNeg - expectedNeg) < 0.0001, string.format("Angles < -pi must wrap to positive, got %f", yawWrapNeg))
end)

-- Slice 2: Collinear 6DOF translation rotation
test("RotatesVectorCollinearlyWithDriverGazeAngle", function()
    -- Zero yaw returns original vector
    local x0, y0, z0 = VRCompanion:rotateVectorByYaw(0.1, 0.2, -0.3, 0.0)
    assert(math.abs(x0 - 0.1) < 0.0001 and math.abs(y0 - 0.2) < 0.0001 and math.abs(z0 - (-0.3)) < 0.0001, "Zero yaw must not alter vector")

    -- 90 degrees right yaw (pi/2)
    -- Physical forward lean (z = -0.4): when looking 90 deg right, forward lean becomes translation along cab +X
    -- x' = x*cos(pi/2) + z*sin(pi/2) = 0 + (-0.4)*1 = -0.4 (pointing left/forward relative to cab)
    -- In standard right-handed: looking right by +pi/2, forward in gaze space (z = -0.4) rotates to x = -0.4
    local xRight, yRight, zRight = VRCompanion:rotateVectorByYaw(0.0, 0.0, -0.4, math.pi * 0.5)
    assert(math.abs(xRight - (-0.4)) < 0.0001, string.format("Expected x = -0.4, got %f", xRight))
    assert(math.abs(yRight - 0.0) < 0.0001, "y must remain unchanged")
    assert(math.abs(zRight - 0.0) < 0.0001, string.format("Expected z = 0.0, got %f", zRight))

    -- Physical sideways lean (x = 0.2): when looking 90 deg right, leaning right becomes moving backward in cab (+Z)
    local xSideways, ySideways, zSideways = VRCompanion:rotateVectorByYaw(0.2, 0.0, 0.0, math.pi * 0.5)
    assert(math.abs(xSideways - 0.0) < 0.0001, string.format("Expected x = 0.0, got %f", xSideways))
    assert(math.abs(zSideways - (-0.2)) < 0.0001, string.format("Expected z = -0.2, got %f", zSideways))
end)

-- Slice 3 & 4: In-cab vehicle camera with head tracking node
test("UpdatesTrackingNodeOrientationAndCompositesHeadPoseYaw", function()
    local nodeRotations = {}
    local nodeTranslations = {}

    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end
    _G.setTranslation = function(node, x, y, z)
        nodeTranslations[node] = { x = x, y = y, z = z }
    end
    _G.getTranslation = function(node)
        local t = nodeTranslations[node]
        if t then return t.x, t.y, t.z end
        return 0, 0, 0
    end
    _G.getParent = function(node) return 999 end

    _G.localRotationToLocal = function(fromNode, toNode, rx, ry, rz)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        -- In GIANTS Engine, localRotationToLocal composites the local rotation of fromNode with the input rotation
        return rx + fromR.rx, ry + fromR.ry, rz + fromR.rz
    end
    _G.localToLocal = function(fromNode, toNode, x, y, z)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        local fromT = nodeTranslations[fromNode] or { x = 0, y = 0, z = 0 }
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, fromR.ry)
        return fromT.x + rx, fromT.y + ry, fromT.z + rz
    end

    -- Setup mock VRBridge with head pose: pitch = 0.15, yaw = -0.20, roll = 0.01, physical lean = (0.05, 0.02, -0.10)
    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function()
            return {
                isValid = true,
                x = 0.05, y = 0.02, z = -0.10,
                pitch = 0.15, yaw = -0.20, roll = 0.01,
                qx = 0, qy = 0, qz = 0, qw = 1
            }
        end
    }

    local mockCam = {
        isInside = true,
        cameraNode = 101,
        headTrackingNode = 201,
        cameraPositionNode = 50,
        origRotY = 0.20,
        rotY = 0.80, -- Mouse look yaw offset = 0.80 - 0.20 = 0.60
        rotX = 0.75, -- Mouse pitch offset (must be ignored to keep physical horizon level)
        origRotX = 0.0
    }

    -- Set initial headTrackingNode translation at seat head anchor (0.0, 1.6, 0.2)
    nodeTranslations[201] = { x = 0.0, y = 1.6, z = 0.2 }

    VRCompanion:onVehicleCameraUpdate(mockCam, 16.6)

    -- 1. Tracking node orientation must be updated with base + lookYaw (0.20 + 0.60 = 0.80), rx=0 (horizon level), rz=0
    local headNodeRot = nodeRotations[201]
    assert(headNodeRot ~= nil, "Tracking node rotation must be set")
    assert(math.abs(headNodeRot.rx - 0.0) < 0.0001, "Tracking node pitch must be strictly 0 to preserve horizon")
    assert(math.abs(headNodeRot.ry - 0.80) < 0.0001, string.format("Tracking node yaw must equal base + lookYaw (0.80), got %f", headNodeRot.ry))
    assert(math.abs(headNodeRot.rz - 0.0) < 0.0001, "Tracking node roll must be strictly 0")

    -- 2. Final cameraNode rotation must composite base + lookYaw (0.80) + head tracking yaw (-0.20) = 0.60
    local camRot = nodeRotations[101]
    assert(camRot ~= nil, "Camera node rotation must be set")
    assert(math.abs(camRot.ry - 0.60) < 0.001, string.format("Expected composite yaw 0.60, got %f", camRot.ry))

    -- 3. Vertical pitch must match physical head pose pitch (0.15), completely ignoring mouse rotX (0.75)
    assert(math.abs(camRot.rx - 0.15) < 0.001, string.format("Camera pitch must equal head pose pitch 0.15, got %f", camRot.rx))
    assert(math.abs(camRot.rz - 0.01) < 0.001, string.format("Camera roll must match head pose roll 0.01, got %f", camRot.rz))

    -- 4. 6DOF physical head translation must be rotated by targetHeadRotY (0.80) and added to seat anchor
    local camTrans = nodeTranslations[101]
    assert(camTrans ~= nil, "Camera node translation must be set")
    local expectedRotX, expectedRotY, expectedRotZ = VRCompanion:rotateVectorByYaw(0.05, 0.02, -0.10, 0.80)
    assert(math.abs(camTrans.x - (0.0 + expectedRotX)) < 0.001, "Camera x translation mismatch with collinear rotation")
    assert(math.abs(camTrans.y - (1.6 + 0.02)) < 0.001, "Camera y translation mismatch")
    assert(math.abs(camTrans.z - (0.2 + expectedRotZ)) < 0.001, "Camera z translation mismatch with collinear rotation")
end)

-- Slice 5: Fallback path when no pre-existing head tracking node exists
test("HandlesFallbackPathCollinearlyWithoutTrackingNode", function()
    local nodeRotations = {}
    local nodeTranslations = {}

    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.setTranslation = function(node, x, y, z)
        nodeTranslations[node] = { x = x, y = y, z = z }
    end
    _G.getParent = function(node) return 0 end -- No parent / no tracking node
    _G.createTransformGroup = nil -- Disable auto-construction to test fallback

    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function()
            return {
                isValid = true,
                x = 0.08, y = -0.04, z = -0.15,
                pitch = -0.10, yaw = 0.25, roll = 0.03,
                qx = 0, qy = 0, qz = 0, qw = 1
            }
        end
    }

    local mockFallbackCam = {
        isInside = true,
        cameraNode = 305,
        headTrackingNode = nil,
        origTransX = 0.3,
        origTransY = 1.4,
        origTransZ = -0.2,
        origRotY = 0.10,
        rotY = 0.70, -- lookYaw = 0.60
        rotX = -0.40, -- mouse pitch (must be ignored)
        origRotX = 0.0
    }

    VRCompanion:onVehicleCameraUpdate(mockFallbackCam, 16.6)

    -- 1. Camera rotation must composite origRotY (0.10) + lookYaw (0.60) + head yaw (0.25) = 0.95
    local camRot = nodeRotations[305]
    assert(camRot ~= nil, "Camera rotation must be set in fallback path")
    assert(math.abs(camRot.ry - 0.95) < 0.001, string.format("Expected composite fallback yaw 0.95, got %f", camRot.ry))
    assert(math.abs(camRot.rx - (-0.10)) < 0.001, string.format("Pitch must match physical head pitch -0.10, got %f", camRot.rx))
    assert(math.abs(camRot.rz - 0.03) < 0.001, "Roll must match head roll 0.03")

    -- 2. Physical head translation must rotate collinearly with total gaze yaw (origRotY + lookYaw = 0.70) relative to cab space
    local camTrans = nodeTranslations[305]
    assert(camTrans ~= nil, "Camera translation must be set in fallback path")
    local totalGazeYaw = 0.10 + 0.60
    local expRotX, expRotY, expRotZ = VRCompanion:rotateVectorByYaw(0.08, -0.04, -0.15, totalGazeYaw)
    assert(math.abs(camTrans.x - (0.3 + expRotX)) < 0.001, "Fallback x translation mismatch")
    assert(math.abs(camTrans.y - (1.4 - 0.04)) < 0.001, "Fallback y translation mismatch")
    assert(math.abs(camTrans.z - (-0.2 + expRotZ)) < 0.001, "Fallback z translation mismatch")
end)

-- Slice 6: Recenter resets lookYaw back to neutral dead center (cam.origRotY) and realigns reference frame
test("RecenterActionResetsLookYawBackToOrigRotYAndRealignsReferenceFrame", function()
    local nodeRotations = {}
    local nodeTranslations = {}

    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end
    _G.setTranslation = function(node, x, y, z)
        nodeTranslations[node] = { x = x, y = y, z = z }
    end
    _G.getParent = function(node) return 888 end

    _G.localRotationToLocal = function(fromNode, toNode, rx, ry, rz)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        return rx + fromR.rx, ry + fromR.ry, rz + fromR.rz
    end
    _G.localToLocal = function(fromNode, toNode, x, y, z)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        local fromT = nodeTranslations[fromNode] or { x = 0, y = 0, z = 0 }
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, fromR.ry)
        return fromT.x + rx, fromT.y + ry, fromT.z + rz
    end

    local simPose = {
        x = 0.30, y = 0.10, z = -0.20,
        pitch = 0.10, yaw = 0.15, roll = 0.00,
        isValid = true
    }

    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function() return simPose end,
        recenter = function()
            simPose.x = 0
            simPose.y = 0
            simPose.z = 0
            simPose.pitch = 0
            simPose.yaw = 0
            simPose.roll = 0
            return { success = true }
        end,
        resetTemporalHistory = function() end
    }

    local mockCam = {
        isInside = true,
        cameraNode = 401,
        headTrackingNode = 501,
        origRotY = 0.25,
        rotY = 1.0, -- lookYaw = 0.75
        origRotX = 0.05,
        rotX = 0.40
    }
    nodeTranslations[501] = { x = 0.0, y = 1.5, z = 0.0 }
    nodeRotations[501] = { rx = 0, ry = 0, rz = 0 }

    local mockVehicle = {
        getActiveCamera = function() return mockCam end
    }

    _G.g_currentMission = {
        controlledVehicle = mockVehicle
    }

    -- 1. Pre-update with offset
    VRCompanion:onVehicleCameraUpdate(mockCam, 16.6)
    assert(nodeTranslations[401] ~= nil, "Camera should have translation")

    -- 2. Trigger recenter
    VRCompanion:onRecenterAction()

    -- 3. In-engine VR Recenter action resets cam.rotY back to cam.origRotY (0.25)
    assert(math.abs(mockCam.rotY - mockCam.origRotY) < 0.0001,
        string.format("Recenter must reset rotY (%.3f) back to origRotY (%.3f)", mockCam.rotY, mockCam.origRotY))
    assert(math.abs(mockCam.rotX - mockCam.origRotX) < 0.0001,
        string.format("Recenter must reset rotX (%.3f) back to origRotX (%.3f)", mockCam.rotX, mockCam.origRotX))

    -- 4. Recenter zeroes head translation (matching seat origin 0, 1.5, 0)
    local camTrans = nodeTranslations[401]
    assert(math.abs(camTrans.x - 0.0) < 0.001, "Recenter must zero x offset")
    assert(math.abs(camTrans.y - 1.5) < 0.001, "Recenter must realign to base seat height")
    assert(math.abs(camTrans.z - 0.0) < 0.001, "Recenter must zero z offset")

    -- 5. Recenter resets camera node rotation back to neutral dead center (origRotY)
    local camRot = nodeRotations[401]
    assert(math.abs(camRot.ry - mockCam.origRotY) < 0.001,
        string.format("Recenter must reset camera yaw back to neutral dead center (%.3f), got %f", mockCam.origRotY, camRot.ry))
    assert(math.abs(camRot.rx - 0.0) < 0.001, "Recenter must keep pitch level at 0")

    -- 6. Subsequent onVehicleCameraUpdate computes 0 lookYaw and keeps reference frame neutral
    VRCompanion:onVehicleCameraUpdate(mockCam, 16.6)
    local nextCamRot = nodeRotations[401]
    assert(math.abs(nextCamRot.ry - mockCam.origRotY) < 0.001,
        string.format("Post-recenter update must remain at neutral dead center (%.3f), got %f", mockCam.origRotY, nextCamRot.ry))
end)

-- Slice 7: Recenter reset behavior on fallback vehicle camera without tracking node
test("RecenterActionResetsLookYawOnFallbackCameraWithoutTrackingNode", function()
    local nodeRotations = {}
    local nodeTranslations = {}

    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end
    _G.setTranslation = function(node, x, y, z)
        nodeTranslations[node] = { x = x, y = y, z = z }
    end
    _G.getParent = function(node) return 0 end
    _G.createTransformGroup = nil

    local simPose = {
        x = 0.20, y = 0.05, z = -0.10,
        pitch = 0.05, yaw = 0.10, roll = 0.00,
        isValid = true
    }

    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function() return simPose end,
        recenter = function()
            simPose.x = 0
            simPose.y = 0
            simPose.z = 0
            simPose.pitch = 0
            simPose.yaw = 0
            simPose.roll = 0
            return { success = true }
        end,
        resetTemporalHistory = function() end
    }

    local fallbackCam = {
        isInside = true,
        cameraNode = 601,
        headTrackingNode = nil,
        origTransX = 0.2,
        origTransY = 1.3,
        origTransZ = -0.1,
        origRotY = 0.15,
        rotY = 0.85, -- lookYaw = 0.70
        origRotX = -0.05,
        rotX = 0.20
    }

    _G.g_currentMission = {
        controlledVehicle = {
            getActiveCamera = function() return fallbackCam end
        }
    }

    VRCompanion:onVehicleCameraUpdate(fallbackCam, 16.6)
    VRCompanion:onRecenterAction()

    assert(math.abs(fallbackCam.rotY - 0.15) < 0.0001, "Fallback rotY must reset to origRotY (0.15)")
    assert(math.abs(fallbackCam.rotX - (-0.05)) < 0.0001, "Fallback rotX must reset to origRotX (-0.05)")

    local camTrans = nodeTranslations[601]
    assert(math.abs(camTrans.x - 0.2) < 0.001, "Fallback transX must reset to origTransX")
    assert(math.abs(camTrans.y - 1.3) < 0.001, "Fallback transY must reset to origTransY")
    assert(math.abs(camTrans.z - (-0.1)) < 0.001, "Fallback transZ must reset to origTransZ")

    local camRot = nodeRotations[601]
    assert(math.abs(camRot.ry - 0.15) < 0.001, "Fallback cam rotY must equal origRotY (0.15)")
    assert(math.abs(camRot.rx - 0.0) < 0.001, "Fallback cam pitch must remain level at 0.0")
end)

-- Slice 8: Vehicle entry cleanly initializes base rotation states
test("EnteringVehicleCleanlyInitializesBaseRotationStatesWithoutSnappingOrDrift", function()
    local veh1Cam = {
        isInside = true,
        cameraNode = 701,
        headTrackingNode = nil,
        rotY = 0.40,
        rotX = 0.10,
        origRotY = nil, -- Missing origRotY; must be cleanly initialized from rotY
        origRotX = nil,
        transX = 0.1,
        transY = 1.5,
        transZ = 0.0
    }

    local veh1 = {
        getActiveCamera = function() return veh1Cam end
    }

    VRCompanion:onEnterVehicle(veh1, true, nil)

    assert(veh1Cam.origRotY == 0.40, "origRotY must be initialized to initial rotY 0.40")
    assert(veh1Cam.origRotX == 0.10, "origRotX must be initialized to initial rotX 0.10")
    assert(veh1Cam.origTransX == 0.1, "origTransX must be initialized from transX")
    assert(veh1Cam.origTransY == 1.5, "origTransY must be initialized from transY")

    -- Initial lookYaw must be 0 immediately upon entry, preventing any sudden snap
    local lookYaw = VRCompanion:getVehicleLookYaw(veh1Cam)
    assert(math.abs(lookYaw - 0.0) < 0.0001, "Initial lookYaw upon vehicle entry must be 0")
end)

-- Slice 9: Vehicle switching lifecycle preserves independent rotation states
test("VehicleSwitchingLifecyclePreservesIndependentRotationStates", function()
    local veh1Cam = {
        isInside = true,
        cameraNode = 801,
        origRotY = 0.0,
        rotY = 0.0,
        origRotX = 0.0,
        rotX = 0.0
    }
    local veh1 = {
        id = "Tractor_A",
        getActiveCamera = function() return veh1Cam end
    }

    local veh2Cam = {
        isInside = true,
        cameraNode = 802,
        origRotY = 0.30,
        rotY = 0.30,
        origRotX = 0.0,
        rotX = 0.0
    }
    local veh2 = {
        id = "Harvester_B",
        getActiveCamera = function() return veh2Cam end
    }

    -- 1. Enter Tractor A and rotate view right by 0.60 rad
    VRCompanion:onEnterVehicle(veh1, true, nil)
    veh1Cam.rotY = 0.60
    VRCompanion:saveVehicleRotationState(veh1)

    -- 2. Switch to Harvester B
    VRCompanion:onVehicleTransition(veh1, veh2)

    -- Harvester B must start cleanly with its own neutral orientation, not inheriting Tractor A's rotation
    assert(math.abs(veh2Cam.rotY - 0.30) < 0.0001, "Harvester B must not inherit Tractor A yaw")
    local lookYawB = VRCompanion:getVehicleLookYaw(veh2Cam)
    assert(math.abs(lookYawB - 0.0) < 0.0001, "Harvester B initial lookYaw must be 0")

    -- Rotate Harvester B view left by -0.40 rad
    veh2Cam.rotY = 0.30 - 0.40
    VRCompanion:saveVehicleRotationState(veh2)

    -- 3. Switch back to Tractor A
    VRCompanion:onVehicleTransition(veh2, veh1)

    -- Tractor A must restore its saved rotation state (0.60) without snap or drift
    assert(math.abs(veh1Cam.rotY - 0.60) < 0.0001, "Tractor A must cleanly restore its rotated yaw state (0.60)")
    local lookYawA = VRCompanion:getVehicleLookYaw(veh1Cam)
    assert(math.abs(lookYawA - 0.60) < 0.0001, "Tractor A lookYaw must cleanly evaluate to 0.60")
end)

-- Slice 10: Repeated enter/exit/switch cycles do not cause rotational drift
test("RepeatedLifecycleTransitionsDoNotCauseRotationalDrift", function()
    local nodeRotations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end

    local testCam = {
        isInside = true,
        cameraNode = 901,
        headTrackingNode = 902,
        origRotY = 0.10,
        rotY = 0.10,
        origRotX = 0.0,
        rotX = 0.0
    }
    nodeRotations[902] = { rx = 0, ry = 0.10, rz = 0 }

    local veh = {
        getActiveCamera = function() return testCam end
    }

    -- Simulate 50 enter -> rotate -> exit -> enter cycles
    for i = 1, 50 do
        VRCompanion:onEnterVehicle(veh, true, nil)
        testCam.rotY = testCam.origRotY + 0.35 -- Look right by 0.35
        VRCompanion:onVehicleCameraUpdate(testCam, 16.6)
        VRCompanion:onLeaveVehicle(veh)
    end

    -- Re-enter and verify base rotation and tracking node origin haven't drifted
    VRCompanion:onEnterVehicle(veh, true, nil)
    assert(math.abs(testCam.origRotY - 0.10) < 0.0001,
        string.format("origRotY must remain strictly 0.10 after 50 cycles, got %f", testCam.origRotY))
    assert(math.abs(testCam.origHeadTrackingRotY - 0.10) < 0.0001,
        string.format("origHeadTrackingRotY must remain strictly 0.10, got %f", testCam.origHeadTrackingRotY))
end)

-- Slice 11: Vehicle exit resets camera transforms to neutral so parked vehicles are not left skewed
test("VehicleExitResetsCameraTransformsToNeutralForTrackingAndFallbackNodes", function()
    local nodeRotations = {}
    local nodeTranslations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.setTranslation = function(node, x, y, z)
        nodeTranslations[node] = { x = x, y = y, z = z }
    end

    -- 1. Test fallback camera exit transform reset
    local fallbackCam = {
        isInside = true,
        cameraNode = 1001,
        headTrackingNode = nil,
        origTransX = 0.5,
        origTransY = 1.6,
        origTransZ = 0.2,
        origRotY = 0.20,
        rotY = 0.80 -- looked right by 0.60
    }
    local fallbackVeh = {
        getActiveCamera = function() return fallbackCam end
    }

    VRCompanion:onEnterVehicle(fallbackVeh, true, nil)
    -- Simulate cameraNode being rotated by mouse look
    nodeRotations[1001] = { rx = 0, ry = 0.80, rz = 0 }
    nodeTranslations[1001] = { x = 0.7, y = 1.6, z = 0.3 }

    VRCompanion:onLeaveVehicle(fallbackVeh)

    -- After leave, fallback cameraNode must be reset to neutral base transform
    local fbRot = nodeRotations[1001]
    assert(math.abs(fbRot.ry - 0.20) < 0.0001, "Fallback cam rotY must reset to origRotY (0.20) upon exit")
    local fbTrans = nodeTranslations[1001]
    assert(math.abs(fbTrans.x - 0.5) < 0.0001 and math.abs(fbTrans.y - 1.6) < 0.0001 and math.abs(fbTrans.z - 0.2) < 0.0001,
        "Fallback cam translation must reset to base seat coordinates upon exit")

    -- 2. Test head tracking node exit transform reset
    local trackingCam = {
        isInside = true,
        cameraNode = 1002,
        headTrackingNode = 1003,
        origHeadTrackingRotX = 0.0,
        origHeadTrackingRotY = 0.05,
        origHeadTrackingRotZ = 0.0,
        origRotY = 0.05,
        rotY = 0.95
    }
    local trackingVeh = {
        getActiveCamera = function() return trackingCam end
    }

    VRCompanion:onEnterVehicle(trackingVeh, true, nil)
    nodeRotations[1003] = { rx = 0, ry = 0.95, rz = 0 }

    VRCompanion:onLeaveVehicle(trackingVeh)

    local trRot = nodeRotations[1003]
    assert(math.abs(trRot.ry - 0.05) < 0.0001, "Tracking node rotY must reset to origHeadTrackingRotY upon exit")
end)

-- Slice 12: In-engine input registration recognizes VR_RECENTER_VIEW action
test("RegistersVRRecenterViewActionAndBindsToRecenterAction", function()
    local registeredActions = {}
    _G.InputAction = {
        VR_RECENTER_VIEW = "VR_RECENTER_VIEW",
        VR_HUD_TOGGLE = "VR_HUD_TOGGLE"
    }
    _G.g_inputBinding = {
        getIsActionEventActive = function(self, id) return false end,
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            table.insert(registeredActions, actionName)
            return true, #registeredActions
        end,
        setActionEventTextPriority = function() end,
        setActionEventActive = function() end,
        setActionEventTextVisibility = function() end,
        setActionEventText = function() end
    }

    VRCompanion.recenterActionEventId = nil
    VRCompanion:registerActionEvents(true)

    local foundRecenterView = false
    for _, act in ipairs(registeredActions) do
        if act == "VR_RECENTER_VIEW" then
            foundRecenterView = true
        end
        assert(act ~= "VR_RECENTER", "Must not register the superseded VR_RECENTER action")
    end
    assert(foundRecenterView, "Must register VR_RECENTER_VIEW action in-engine")
end)

-- Slice 13: Starting view in cab and recenter must face forward (+Z) out the windshield
test("StartingInCabViewAndRecenterFacesForwardOutWindshieldCollinearWithVehicleZ", function()
    local nodeRotations = {}
    local nodeTranslations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end
    _G.setTranslation = function(node, x, y, z)
        nodeTranslations[node] = { x = x, y = y, z = z }
    end
    _G.getTranslation = function(node)
        local t = nodeTranslations[node]
        if t then return t.x, t.y, t.z end
        return 0, 0, 0
    end
    _G.getParent = function(node) return 10 end -- parent is vehicle cab (node 10)
    local nextNodeId = 2000
    _G.createTransformGroup = function(name)
        nextNodeId = nextNodeId + 1
        return nextNodeId
    end
    _G.link = function(parent, child) end
    _G.setDirection = nil -- Simulating engine where setDirection is nil or fails, falling back to line 2110: setRotationFn(tg, 0, 0, 0)
    _G.localDirectionToLocal = function(fromNode, toNode, x, y, z)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, fromR.ry)
        return rx, ry, rz
    end

    -- In GIANTS Engine, localRotationToLocal composites local rotation with parent
    _G.localRotationToLocal = function(fromNode, toNode, rx, ry, rz)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        return rx + fromR.rx, ry + fromR.ry, rz + fromR.rz
    end
    _G.localToLocal = function(fromNode, toNode, x, y, z)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        local fromT = nodeTranslations[fromNode] or { x = 0, y = 0, z = 0 }
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, fromR.ry)
        return fromT.x + rx, fromT.y + ry, fromT.z + rz
    end

    local simPose = {
        x = 0, y = 0, z = 0,
        pitch = 0, yaw = 0, roll = 0,
        isValid = true
    }
    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function() return simPose end,
        recenter = function()
            simPose.x = 0; simPose.y = 0; simPose.z = 0
            simPose.pitch = 0; simPose.yaw = 0; simPose.roll = 0
            return { success = true }
        end,
        resetTemporalHistory = function() end
    }

    -- Standard FS25 vehicle camera: XML/I3D defines rotation="-18 180 0" (math.pi)
    local standardCabCam = {
        isInside = true,
        cameraNode = 1101,
        cameraPositionNode = 1102,
        origRotX = math.rad(-18),
        rotX = math.rad(-18),
        origRotY = math.pi, -- 180 degrees neutral forward
        rotY = math.pi,
        origTransX = 0, origTransY = 1.6, origTransZ = -0.2
    }
    nodeRotations[1101] = { rx = math.rad(-18), ry = math.pi, rz = 0 }
    nodeRotations[1102] = { rx = math.rad(-18), ry = math.pi, rz = 0 }
    nodeTranslations[1102] = { x = 0, y = 1.6, z = -0.2 }

    local veh = {
        getActiveCamera = function() return standardCabCam end
    }
    _G.g_currentMission = {
        controlledVehicle = veh
    }

    -- 1. Enter vehicle
    VRCompanion:onEnterVehicle(veh, true, nil)

    -- 2. First update tick upon entering cab
    VRCompanion:onVehicleCameraUpdate(standardCabCam, 16.6)

    -- The camera view frustum in GIANTS points along local -Z.
    -- Forward in vehicle cab space is +Z (0, 0, 1). Backward is -Z (0, 0, -1).
    -- When looking forward out the windshield, the camera node rotation Y in cab space must be math.pi (180 deg),
    -- which rotates local look vector (0, 0, -1) to (0, 0, +1).
    local camRot = nodeRotations[1101]
    assert(camRot ~= nil, "Camera rotation must be set")
    local lookDirX, _, lookDirZ = VRCompanion:rotateVectorByYaw(0, 0, -1, camRot.ry)
    assert(lookDirZ > 0.99, string.format("Starting view in cab must face forward (+Z) out windshield, but lookDirZ = %f (camRot.ry = %f rad, %.1f deg)",
        lookDirZ, camRot.ry, math.deg(camRot.ry)))

    -- 3. Recenter view in cab
    VRCompanion:onRecenterAction()

    local recenterRot = nodeRotations[1101]
    assert(recenterRot ~= nil, "Camera rotation must be set after recenter")
    local recLookX, _, recLookZ = VRCompanion:rotateVectorByYaw(0, 0, -1, recenterRot.ry)
    assert(recLookZ > 0.99, string.format("Recentered view in cab must face forward (+Z) out windshield, but recLookZ = %f (recenterRot.ry = %f rad, %.1f deg)",
        recLookZ, recenterRot.ry, math.deg(recenterRot.ry)))
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_vehicle_camera_yaw.luau", failed))


