-- ====================================================================
-- Test Suite for In-Cab Vehicle Snap Turn, State & Debounce (Issue #35)
-- ====================================================================

-- Mock GIANTS Engine globals before loading VRCompanion
_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

-- Load the Companion Mod module files in modDesc order, exactly as the engine does
loadCompanionMod()
local fs = require("@lune/fs")

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

print("Running In-Cab Snap Turn, State & Debounce Tests (Issue #35)...")

-- Slice 1: Core Snap Turn State Machine Defaults & Configuration
test("SnapTurnStateMachineDefaultsAndConfigurableTurnAngle", function()
    -- Default turn angle is pi/2 radians (90 degrees)
    assert(VRCompanion.DEFAULT_SNAP_TURN_ANGLE ~= nil, "DEFAULT_SNAP_TURN_ANGLE constant must be defined")
    assert(math.abs(VRCompanion.DEFAULT_SNAP_TURN_ANGLE - (math.pi * 0.5)) < 0.0001,
        string.format("DEFAULT_SNAP_TURN_ANGLE must be pi/2 (1.5708 rad), got %f", VRCompanion.DEFAULT_SNAP_TURN_ANGLE))
    assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001,
        "getSnapTurnAngle must return pi/2 by default")

    -- Debounce cooldown constant is at least 250ms
    assert(VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS ~= nil, "DEFAULT_SNAP_TURN_DEBOUNCE_MS constant must be defined")
    assert(VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS >= 250, "DEFAULT_SNAP_TURN_DEBOUNCE_MS must be at least 250ms")

    -- Set configurable turn angle in radians (e.g. 45 degrees = pi/4)
    VRCompanion:setSnapTurnAngle(math.pi * 0.25)
    assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.25)) < 0.0001, "Configured snap turn angle to pi/4")

    -- Set configurable turn angle in degrees (e.g. 90 degrees)
    VRCompanion:setSnapTurnAngle(90)
    assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001, "Setting 90 degrees should convert to pi/2 radians")

    -- Reset to default pi/2 for subsequent tests
    VRCompanion:setSnapTurnAngle(math.pi * 0.5)
end)

-- Slice 2: Debounce Timing Cooldown Enforcement (minimum 250ms)
test("DebounceCooldownEnforcesMinimum250msBetweenSnapTurns", function()
    VRCompanion:resetSnapTurnDebounce()

    local mockCam = {
        isInside = true,
        cameraNode = 101,
        origRotY = 0.0,
        rotY = 0.0
    }
    local mockVeh = {
        getActiveCamera = function() return mockCam end
    }
    _G.g_currentMission = {
        controlledVehicle = mockVeh
    }

    -- 1. First snap turn at t = 1000ms: succeeds
    local ok1, newRot1 = VRCompanion:snapTurnRight(nil, 1000)
    assert(ok1 == true, "First snap turn at t=1000 must succeed")
    assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Camera rotY must be -pi/2")

    -- 2. Rapid repeat at t = 1100ms (100ms later, < 250ms): rejected by debounce
    local ok2, err2 = VRCompanion:snapTurnRight(nil, 1100)
    assert(ok2 == false, "Snap turn 100ms later must be rejected by debounce")
    assert(err2 == "debounced", "Error message must indicate debounced")
    assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Camera rotY must not change during cooldown")

    -- 3. Snap turn at t = 1240ms (240ms later, still < 250ms): rejected
    local ok3, _ = VRCompanion:snapTurnRight(nil, 1240)
    assert(ok3 == false, "Snap turn 240ms later must still be debounced")

    -- 4. Snap turn at t = 1250ms (exactly 250ms cooldown elapsed): succeeds
    local ok4, newRot4 = VRCompanion:snapTurnRight(nil, 1250)
    assert(ok4 == true, "Snap turn at t=1250 (250ms elapsed) must succeed")
    assert(math.abs(math.abs(mockCam.rotY) - math.pi) < 0.0001, "Camera rotY must advance to -pi / pi (180 deg)")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 4: In-Cab Snap Turning Adjusts cam.rotY and Updates headTrackingNode
test("InCabSnapTurnAdjustsCameraRotYAndUpdatesHeadTrackingNode", function()
    VRCompanion:resetSnapTurnDebounce()

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
        cameraNode = 301,
        headTrackingNode = 302,
        origHeadTrackingRotX = 0,
        origHeadTrackingRotY = math.pi, -- 180 deg neutral forward in cab
        origHeadTrackingRotZ = 0,
        origRotY = math.pi,
        rotY = math.pi
    }
    nodeRotations[302] = { rx = 0, ry = math.pi, rz = 0 }

    local veh = {
        id = "Tractor_Snap",
        getActiveCamera = function() return testCam end
    }
    _G.g_currentMission = {
        controlledVehicle = veh
    }
    VRCompanion:onEnterVehicle(veh, true, nil)

    -- 1. Snap turn right (-pi/2)
    local okR, newRotR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == true, "snapTurnRight must succeed")
    -- cam.rotY = normalizeAngle(pi - pi/2) = +pi/2
    assert(math.abs(testCam.rotY - (math.pi * 0.5)) < 0.0001,
        string.format("Expected cam.rotY = +pi/2 (1.571), got %f", testCam.rotY))

    -- headTrackingNode must be immediately updated
    local headRotR = nodeRotations[302]
    assert(headRotR ~= nil, "headTrackingNode rotation must be updated")
    assert(math.abs(headRotR.ry - (math.pi * 0.5)) < 0.0001,
        string.format("headTrackingNode yaw must equal +pi/2, got %f", headRotR.ry))
    assert(math.abs(headRotR.rx) < 0.0001 and math.abs(headRotR.rz) < 0.0001, "Pitch and roll must remain 0")

    -- Vehicle rotation state table must also be updated
    assert(VRCompanion.vehicleRotationStates[veh] ~= nil, "Vehicle rotation state must exist")
    assert(math.abs(VRCompanion.vehicleRotationStates[veh].rotY - (math.pi * 0.5)) < 0.0001,
        "Vehicle rotation state must track snapped rotY")

    -- 2. Snap turn left (+pi/2) after 300ms
    local okL, newRotL = VRCompanion:snapTurnLeft(nil, 1300)
    assert(okL == true, "snapTurnLeft must succeed after cooldown")
    -- cam.rotY = normalizeAngle(pi/2 + pi/2) = pi
    assert(math.abs(math.abs(testCam.rotY) - math.pi) < 0.0001,
        string.format("Expected cam.rotY = pi (-pi), got %f", testCam.rotY))

    local headRotL = nodeRotations[302]
    assert(math.abs(math.abs(headRotL.ry) - math.pi) < 0.0001,
        string.format("headTrackingNode yaw must equal pi (-pi), got %f", headRotL.ry))

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 5: Angle Wrapping Around [-pi, pi] Boundaries Over Full 360-Degree Cycles
test("AngleWrappingAcrossPiBoundariesAndFull360Cycles", function()
    VRCompanion:resetSnapTurnDebounce()

    local testCam = {
        isInside = true,
        cameraNode = 401,
        origRotY = 0.0,
        rotY = 0.0
    }
    _G.g_currentMission = {
        controlledVehicle = {
            getActiveCamera = function() return testCam end
        }
    }

    local t = 1000
    -- Step 1: Turn right from 0 -> -90 deg (-pi/2)
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(testCam.rotY - (-math.pi * 0.5)) < 0.0001, "Step 1: -pi/2")

    -- Step 2: Turn right from -90 -> -180 deg (-pi)
    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(math.abs(testCam.rotY) - math.pi) < 0.0001, "Step 2: pi")

    -- Step 3: Turn right from -180 -> -270 deg (wraps to +90 deg / +pi/2)
    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(testCam.rotY - (math.pi * 0.5)) < 0.0001, "Step 3: wraps to +pi/2")

    -- Step 4: Turn right from +90 -> 0 deg (completing 360 deg circle)
    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(testCam.rotY - 0.0) < 0.0001, "Step 4: completes full circle to 0.0")

    -- Now turn left 4 times to reverse the full circle
    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(testCam.rotY - (math.pi * 0.5)) < 0.0001, "Reverse Step 1: +pi/2")

    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(math.abs(testCam.rotY) - math.pi) < 0.0001, "Reverse Step 2: pi")

    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(testCam.rotY - (-math.pi * 0.5)) < 0.0001, "Reverse Step 3: wraps to -pi/2")

    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(testCam.rotY - 0.0) < 0.0001, "Reverse Step 4: back to 0.0")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 6: 6DOF OpenXR Head Tracking Preserved on Top of Snapped View
test("Preserves6DofOpenXrHeadTrackingOnTopOfSnappedView", function()
    VRCompanion:resetSnapTurnDebounce()

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
        return rx + fromR.rx, ry + fromR.ry, rz + fromR.rz
    end
    _G.localToLocal = function(fromNode, toNode, x, y, z)
        local fromR = nodeRotations[fromNode] or { rx = 0, ry = 0, rz = 0 }
        local fromT = nodeTranslations[fromNode] or { x = 0, y = 0, z = 0 }
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, fromR.ry)
        return fromT.x + rx, fromT.y + ry, fromT.z + rz
    end

    -- Setup physical head pose with slight tilt and forward lean
    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function()
            return {
                isValid = true,
                x = 0.05, y = 0.02, z = -0.10,
                pitch = 0.12, yaw = 0.08, roll = -0.01,
                qx = 0, qy = 0, qz = 0, qw = 1
            }
        end
    }

    local cam = {
        isInside = true,
        cameraNode = 501,
        headTrackingNode = 502,
        origHeadTrackingRotX = 0,
        origHeadTrackingRotY = math.pi,
        origHeadTrackingRotZ = 0,
        origRotY = math.pi,
        rotY = math.pi,
        origRotX = 0.0,
        rotX = 0.0
    }
    nodeTranslations[502] = { x = 0.0, y = 1.6, z = 0.1 }
    nodeRotations[502] = { rx = 0, ry = math.pi, rz = 0 }

    local veh = {
        getActiveCamera = function() return cam end
    }
    _G.g_currentMission = {
        controlledVehicle = veh
    }

    -- 1. Perform snap turn right (-90 deg / -pi/2 rad)
    VRCompanion:snapTurnRight(nil, 1000)

    -- 2. Execute onVehicleCameraUpdate tick
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    -- headTrackingNode yaw must be +pi/2
    local headRot = nodeRotations[502]
    assert(math.abs(headRot.ry - (math.pi * 0.5)) < 0.001, "headTrackingNode yaw must equal +pi/2")

    -- Final cameraNode rotation must composite snapped view (+pi/2) + head tracking yaw (0.08)
    local camRot = nodeRotations[501]
    local expectedCamYaw = VRCompanion:normalizeAngle(math.pi * 0.5 + 0.08)
    assert(math.abs(camRot.ry - expectedCamYaw) < 0.001,
        string.format("Camera composite yaw must be %f, got %f", expectedCamYaw, camRot.ry))

    -- Physical pitch (0.12) and roll (-0.01) must be preserved
    assert(math.abs(camRot.rx - 0.12) < 0.001, "Physical head pitch must be preserved")
    assert(math.abs(camRot.rz - (-0.01)) < 0.001, "Physical head roll must be preserved")

    -- Physical head translation (0.05, 0.02, -0.10) must be rotated collinearly by snapped gaze yaw
    local camTrans = nodeTranslations[501]
    local expX, expY, expZ = VRCompanion:rotateVectorByYaw(0.05, 0.02, -0.10, math.pi * 0.5)
    assert(math.abs(camTrans.x - (0.0 + expX)) < 0.001, "Collinear translation x mismatch")
    assert(math.abs(camTrans.y - (1.6 + 0.02)) < 0.001, "Collinear translation y mismatch")
    assert(math.abs(camTrans.z - (0.1 + expZ)) < 0.001, "Collinear translation z mismatch")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 7: Recenter Action Resets Snapped cam.rotY to cam.origRotY Forward Windshield
test("RecenterActionResetsSnappedRotYBackToNeutralWindshieldAlignment", function()
    VRCompanion:resetSnapTurnDebounce()

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
        recenter = function() return { success = true } end,
        resetTemporalHistory = function() end
    }

    local cam = {
        isInside = true,
        cameraNode = 601,
        headTrackingNode = 602,
        origHeadTrackingRotX = 0,
        origHeadTrackingRotY = math.pi,
        origHeadTrackingRotZ = 0,
        origRotY = math.pi, -- Forward out windshield
        rotY = math.pi
    }
    nodeTranslations[602] = { x = 0.0, y = 1.6, z = 0.0 }
    nodeRotations[602] = { rx = 0, ry = math.pi, rz = 0 }

    local veh = {
        getActiveCamera = function() return cam end
    }
    _G.g_currentMission = {
        controlledVehicle = veh
    }

    -- 1. Execute two snap turns right -> turned 180 degrees, looking out rear window
    VRCompanion:snapTurnRight(nil, 1000)
    VRCompanion:snapTurnRight(nil, 1300)
    assert(math.abs(cam.rotY - 0.0) < 0.0001, "Camera facing backward (rotY = 0)")

    -- 2. Trigger VR_RECENTER_VIEW
    VRCompanion:onRecenterAction()

    -- 3. cam.rotY must be cleanly restored to cam.origRotY (math.pi, facing forward windshield)
    assert(math.abs(cam.rotY - cam.origRotY) < 0.0001,
        string.format("Recenter must reset rotY back to origRotY (%.3f), got %.3f", cam.origRotY, cam.rotY))

    -- 4. headTrackingNode must be restored to origHeadTrackingRotY
    local headRot = nodeRotations[602]
    assert(math.abs(headRot.ry - math.pi) < 0.001,
        string.format("headTrackingNode yaw must reset to pi (180 deg), got %f", headRot.ry))

    -- 5. Subsequent update verifies view faces forward (+Z) out windshield
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    local camRot = nodeRotations[601]
    local lookX, _, lookZ = VRCompanion:rotateVectorByYaw(0, 0, -1, camRot.ry)
    assert(lookZ > 0.99, string.format("View after recenter must face forward (+Z), but lookZ = %f", lookZ))

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 9: Fallback Vehicle Camera Without Tracking Node
test("FallbackVehicleCameraWithoutTrackingNodeSupportsSnapTurnAndRecenter", function()
    VRCompanion:resetSnapTurnDebounce()

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

    local fallbackCam = {
        isInside = true,
        cameraNode = 801,
        headTrackingNode = nil,
        origTransX = 0, origTransY = 1.5, origTransZ = 0,
        origRotY = 0.0,
        rotY = 0.0,
        origRotX = 0.0,
        rotX = 0.0
    }
    _G.g_currentMission = {
        controlledVehicle = {
            getActiveCamera = function() return fallbackCam end
        }
    }

    -- 1. Snap turn left on fallback camera
    local ok, newRot = VRCompanion:snapTurnLeft(nil, 1000)
    assert(ok == true, "Fallback camera snapTurnLeft must succeed")
    assert(math.abs(fallbackCam.rotY - (math.pi * 0.5)) < 0.0001, "Fallback rotY must be +pi/2")

    -- 2. Recenter resets fallback camera rotY back to 0.0
    VRCompanion:onRecenterAction()
    assert(math.abs(fallbackCam.rotY - 0.0) < 0.0001, "Recenter resets fallback cam rotY to 0.0")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 10: GUI Menu Guard Suppresses Snap Turn
test("GuiMenuGuardSuppressesSnapTurnsWhileMenuVisible", function()
    VRCompanion:resetSnapTurnDebounce()

    local mockCam = {
        isInside = true,
        cameraNode = 901,
        origRotY = 0.0,
        rotY = 0.0
    }
    _G.g_currentMission = {
        controlledVehicle = {
            getActiveCamera = function() return mockCam end
        }
    }

    -- Set menu visible
    VRCompanion.isMenuVisible = true

    local okR, errR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == false, "Snap turn right must be rejected when menu is visible")
    assert(errR == "menu_visible", "Error reason must be menu_visible")
    assert(math.abs(mockCam.rotY - 0.0) < 0.0001, "Camera rotY must not change while menu is open")

    local okL, errL = VRCompanion:snapTurnLeft(nil, 1100)
    assert(okL == false, "Snap turn left must be rejected when menu is visible")
    assert(math.abs(mockCam.rotY - 0.0) < 0.0001, "Camera rotY must not change")

    VRCompanion.isMenuVisible = false
    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 11: On-Foot Snap Turn Adjusts Player Character yawNode
test("OnFootSnapTurnAdjustsPlayerCharacterYawNode", function()
    VRCompanion:resetSnapTurnDebounce()

    local nodeRotations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end

    local mockPlayerCam = {
        isFirstPerson = true,
        firstPersonCamera = 40,
        cameraRootNode = 30,
        pitchNode = 20,
        yawNode = 10,
        setRotation = function(self, pitch, yaw, roll)
            setRotation(self.pitchNode, pitch or 0, 0, 0)
            setRotation(self.yawNode, 0, yaw or 0, 0)
            setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
        end,
        getRotation = function(self)
            local r = nodeRotations[self.yawNode] or { rx = 0, ry = 0, rz = 0 }
            return 0, r.ry, 0
        end
    }
    nodeRotations[10] = { rx = 0, ry = 0, rz = 0 }
    nodeRotations[20] = { rx = 0, ry = 0, rz = 0 }
    nodeRotations[30] = { rx = 0, ry = math.pi, rz = 0 }
    nodeRotations[40] = { rx = 0, ry = 0, rz = 0 }

    _G.g_currentMission = {
        controlledVehicle = nil,
        player = {
            camera = mockPlayerCam
        }
    }

    -- 1. Trigger snap turn right (+90 deg -> -pi/2 yawNode)
    local okR, newYawR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == true, "On-foot snapTurnRight must succeed")
    assert(math.abs(newYawR - (-math.pi * 0.5)) < 0.0001,
        string.format("Expected newYawR = -pi/2 (-1.571), got %s", tostring(newYawR)))
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001,
        string.format("yawNode rotation must equal -pi/2, got %f", nodeRotations[10].ry))

    -- 2. Trigger snap turn left (-90 deg -> back to 0.0 yawNode) after 300ms cooldown
    local okL, newYawL = VRCompanion:snapTurnLeft(nil, 1300)
    assert(okL == true, "On-foot snapTurnLeft must succeed after cooldown")
    assert(math.abs(newYawL - 0.0) < 0.0001,
        string.format("Expected newYawL = 0.0, got %s", tostring(newYawL)))
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001,
        string.format("yawNode rotation must equal 0.0, got %f", nodeRotations[10].ry))

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 12: Walking and Strafing Collinear Alignment After Snap Turn Without Rotational Drift
test("WalkingAndStrafingImmediatelyAlignWithSnappedHeadingWithoutDrift", function()
    VRCompanion:resetSnapTurnDebounce()

    local nodes = {
        [10] = { name = "yawNode", parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} },
        [20] = { name = "pitchNode", parent = 10, trans = {0, 0, 0}, rot = {0, 0, 0} },
        [30] = { name = "cameraRootNode", parent = 20, trans = {0, 0, 0}, rot = {0, math.pi, 0} },
        [40] = { name = "firstPersonCamera", parent = 30, trans = {0, 0, 0}, rot = {0, 0, 0} },
    }
    local nextNodeId = 100

    _G.createTransformGroup = function(name)
        nextNodeId = nextNodeId + 1
        nodes[nextNodeId] = { name = name, parent = 0, trans = {0, 0, 0}, rot = {0, 0, 0} }
        return nextNodeId
    end

    _G.link = function(parent, child)
        if nodes[child] then
            nodes[child].parent = parent
        end
    end

    _G.getParent = function(node)
        return nodes[node] and nodes[node].parent or 0
    end

    _G.setTranslation = function(node, x, y, z)
        if nodes[node] then
            nodes[node].trans = { x, y, z }
        end
    end

    _G.getTranslation = function(node)
        local t = nodes[node] and nodes[node].trans or {0, 0, 0}
        return t[1], t[2], t[3]
    end

    _G.setRotation = function(node, rx, ry, rz)
        if nodes[node] then
            nodes[node].rot = { rx, ry, rz }
        end
    end

    _G.getRotation = function(node)
        local r = nodes[node] and nodes[node].rot or {0, 0, 0}
        return r[1], r[2], r[3]
    end

    _G.localDirectionToWorld = function(node, x, y, z)
        local cur = node
        local totalY = 0
        while cur ~= 0 and nodes[cur] do
            totalY = totalY + (nodes[cur].rot[2] or 0)
            cur = nodes[cur].parent
        end
        local cosY = math.cos(totalY)
        local sinY = math.sin(totalY)
        local wx = x * cosY + z * sinY
        local wz = -x * sinY + z * cosY
        return wx, y, wz
    end

    _G.localRotationToLocal = function(srcNode, dstNode, rx, ry, rz)
        local srcY = 0
        local cur = srcNode
        while cur ~= 0 and nodes[cur] do
            srcY = srcY + (nodes[cur].rot[2] or 0)
            cur = nodes[cur].parent
        end
        local dstY = 0
        cur = dstNode
        while cur ~= 0 and nodes[cur] do
            dstY = dstY + (nodes[cur].rot[2] or 0)
            cur = nodes[cur].parent
        end
        local diffY = srcY - dstY
        return rx, ry + diffY, rz
    end

    _G.localToLocal = function(srcNode, dstNode, x, y, z)
        return x, y, z
    end

    local mockPlayerCam = {
        isFirstPerson = true,
        firstPersonCamera = 40,
        cameraRootNode = 30,
        pitchNode = 20,
        yawNode = 10,
        setRotation = function(self, pitch, yaw, roll)
            _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
            _G.setRotation(self.yawNode, 0, yaw or 0, 0)
            _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
        end,
        getRotation = function(self)
            local r = nodes[self.yawNode] and nodes[self.yawNode].rot or {0, 0, 0}
            return 0, r[2] or 0, 0
        end
    }

    local mockInput = {
        player = { camera = mockPlayerCam },
        cameraRotationY = 0,
        calculateNormalisedMovementDirection = function(self, moveRight, moveForward)
            if moveRight ~= 0 or moveForward ~= 0 then
                local len = math.sqrt(moveRight * moveRight + moveForward * moveForward)
                return moveRight / len, moveForward / len
            end
            return 0, 0
        end,
        calculateNormalisedWorldMovementDirection = function(self, moveRight, moveForward)
            local localDirectionX, localDirectionZ = self:calculateNormalisedMovementDirection(moveRight, moveForward)
            if localDirectionX ~= 0 or localDirectionZ ~= 0 then
                local wx, _, wz = _G.localDirectionToWorld(self.player.camera.yawNode, -localDirectionX, 0, localDirectionZ)
                return wx, wz
            end
            return 0, 0
        end
    }
    mockPlayerCam.player = { inputComponent = mockInput }

    _G.g_currentMission = {
        controlledVehicle = nil,
        player = {
            camera = mockPlayerCam
        }
    }

    -- Step 1: Initial state (yawNode = 0.0) -> facing +Z forward
    local camLookX, _, camLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
    local walkWX, walkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1) -- W
    local dotInitial = camLookX * walkWX + camLookZ * walkWZ
    assert(math.abs(dotInitial - 1.0) < 0.001, "Initial gaze and W walk must be collinear forward")

    -- Step 2: Trigger snap turn right (+90 deg -> yawNode = -pi/2)
    local okR, newYawR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == true, "snapTurnRight must succeed")
    assert(math.abs(newYawR - (-math.pi * 0.5)) < 0.0001, "newYawR must be -pi/2")
    assert(math.abs(nodes[10].rot[2] - (-math.pi * 0.5)) < 0.0001, "yawNode rot must be -pi/2")

    -- Step 3: Verify walking forward (W) immediately aligns with snapped heading
    local snappedLookX, _, snappedLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
    local snappedWalkWX, snappedWalkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1) -- W
    local dotW = snappedLookX * snappedWalkWX + snappedLookZ * snappedWalkWZ
    assert(math.abs(dotW - 1.0) < 0.001,
        string.format("After snap turn right, W must walk collinear with gaze! dotW = %f", dotW))
    -- Must be facing right in world space (-X)
    assert(math.abs(snappedLookX - (-1.0)) < 0.001, "Snapped gaze must face -X (right)")
    assert(math.abs(snappedWalkWX - (-1.0)) < 0.001, "Snapped W walk must face -X (right)")

    -- Step 4: Verify strafing (A/D) aligns with snapped orientation
    local snappedWalkDX, snappedWalkDZ = mockInput:calculateNormalisedWorldMovementDirection(1, 0) -- D
    local snappedWalkAX, snappedWalkAZ = mockInput:calculateNormalisedWorldMovementDirection(-1, 0) -- A
    local camRightX, _, camRightZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 1, 0, 0)
    local camLeftX, _, camLeftZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, -1, 0, 0)

    local dotD = camRightX * snappedWalkDX + camRightZ * snappedWalkDZ
    assert(math.abs(dotD - 1.0) < 0.001,
        string.format("D strafe must match camera right after snap turn! dotD = %f", dotD))

    local dotA = camLeftX * snappedWalkAX + camLeftZ * snappedWalkAZ
    assert(math.abs(dotA - 1.0) < 0.001,
        string.format("A strafe must match camera left after snap turn! dotA = %f", dotA))

    -- Step 5: Verify zero rotational drift across subsequent camera rotation update
    mockInput.cameraRotationY = 0 -- no mouse input
    VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
    local _, updatedYaw, _ = _G.getRotation(mockPlayerCam.yawNode)
    assert(math.abs(updatedYaw - (-math.pi * 0.5)) < 0.001,
        string.format("yawNode must maintain -pi/2 without drift during subsequent frame update, got %f", updatedYaw))

    -- Step 6: Subsequent snap turn left (-90 deg) restores neutral forward heading
    local okL, newYawL = VRCompanion:snapTurnLeft(nil, 1300)
    assert(okL == true, "snapTurnLeft must succeed")
    assert(math.abs(newYawL - 0.0) < 0.0001, "newYawL must be 0.0")

    local restoredLookX, _, restoredLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
    local restoredWalkWX, restoredWalkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1)
    local dotRestored = restoredLookX * restoredWalkWX + restoredLookZ * restoredWalkWZ
    assert(math.abs(dotRestored - 1.0) < 0.001, "Restored forward gaze and walk must be collinear")
    assert(math.abs(restoredLookZ - 1.0) < 0.001, "Restored gaze must face +Z forward")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 13: On-Foot GUI Menu Guard Suppresses Snap Turn
test("OnFootGuiMenuGuardSuppressesSnapTurnsWhileMenuVisible", function()
    VRCompanion:resetSnapTurnDebounce()

    local nodeRotations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end

    local mockPlayerCam = {
        isFirstPerson = true,
        firstPersonCamera = 40,
        cameraRootNode = 30,
        pitchNode = 20,
        yawNode = 10,
        setRotation = function(self, pitch, yaw, roll)
            _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
            _G.setRotation(self.yawNode, 0, yaw or 0, 0)
            _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
        end,
        getRotation = function(self)
            local r = nodeRotations[self.yawNode] or { rx = 0, ry = 0, rz = 0 }
            return 0, r.ry, 0
        end
    }
    nodeRotations[10] = { rx = 0, ry = 0, rz = 0 }

    _G.g_currentMission = {
        controlledVehicle = nil,
        player = {
            camera = mockPlayerCam
        }
    }

    -- Set menu visible (e.g. shop, pause, settings open)
    VRCompanion.isMenuVisible = true

    -- 1. Snap turn right must be rejected
    local okR, errR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == false, "On-foot snap turn right must be rejected when menu is visible")
    assert(errR == "menu_visible", "Error reason must be menu_visible")
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode must not change while menu is open")

    -- 2. Snap turn left must be rejected
    local okL, errL = VRCompanion:snapTurnLeft(nil, 1100)
    assert(okL == false, "On-foot snap turn left must be rejected when menu is visible")
    assert(errL == "menu_visible", "Error reason must be menu_visible")
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode must not change while menu is open")

    -- 3. Close menu -> snap turn succeeds
    VRCompanion.isMenuVisible = false
    local okAfter, newYawAfter = VRCompanion:snapTurnRight(nil, 1300)
    assert(okAfter == true, "On-foot snap turn must succeed after menu is closed")
    assert(math.abs(newYawAfter - (-math.pi * 0.5)) < 0.0001, "yawNode must step to -pi/2")
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001, "yawNode rotation must equal -pi/2")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 14: On-Foot Angle Wrapping Across [-pi, pi] Over Full 360-Degree Cycles
test("OnFootAngleWrappingAcrossPiBoundariesAndFull360Cycles", function()
    VRCompanion:resetSnapTurnDebounce()

    local nodeRotations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end

    local mockPlayerCam = {
        isFirstPerson = true,
        firstPersonCamera = 40,
        cameraRootNode = 30,
        pitchNode = 20,
        yawNode = 10,
        setRotation = function(self, pitch, yaw, roll)
            _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
            _G.setRotation(self.yawNode, 0, yaw or 0, 0)
            _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
        end,
        getRotation = function(self)
            local r = nodeRotations[self.yawNode] or { rx = 0, ry = 0, rz = 0 }
            return 0, r.ry, 0
        end
    }
    nodeRotations[10] = { rx = 0, ry = 0, rz = 0 }

    _G.g_currentMission = {
        controlledVehicle = nil,
        player = {
            camera = mockPlayerCam
        }
    }

    local t = 1000

    -- Cycle 1: Four right turns (0 -> -pi/2 -> -pi -> +pi/2 -> 0)
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001, "Step R1: -pi/2 (-90 deg)")

    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(math.abs(nodeRotations[10].ry) - math.pi) < 0.0001, "Step R2: -pi / pi (-180 deg)")

    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(nodeRotations[10].ry - (math.pi * 0.5)) < 0.0001, "Step R3: +pi/2 (+90 deg wrapped from -270)")

    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "Step R4: 0.0 (360 deg full circle)")

    -- Cycle 2: Four left turns (0 -> +pi/2 -> pi -> -pi/2 -> 0)
    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(nodeRotations[10].ry - (math.pi * 0.5)) < 0.0001, "Step L1: +pi/2 (+90 deg)")

    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(math.abs(nodeRotations[10].ry) - math.pi) < 0.0001, "Step L2: pi (180 deg)")

    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001, "Step L3: -pi/2 (-90 deg wrapped from +270)")

    t = t + 300
    VRCompanion:snapTurnLeft(nil, t)
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "Step L4: 0.0 (360 deg full circle restored)")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 15: On-Foot Action Events
test("OnFootActionEventsTriggerSnapTurns", function()
    VRCompanion:resetSnapTurnDebounce()

    local nodeRotations = {}
    _G.setRotation = function(node, rx, ry, rz)
        nodeRotations[node] = { rx = rx, ry = ry, rz = rz }
    end
    _G.getRotation = function(node)
        local r = nodeRotations[node]
        if r then return r.rx, r.ry, r.rz end
        return 0, 0, 0
    end

    local mockPlayerCam = {
        isFirstPerson = true,
        firstPersonCamera = 40,
        cameraRootNode = 30,
        pitchNode = 20,
        yawNode = 10,
        setRotation = function(self, pitch, yaw, roll)
            _G.setRotation(self.pitchNode, pitch or 0, 0, 0)
            _G.setRotation(self.yawNode, 0, yaw or 0, 0)
            _G.setRotation(self.cameraRootNode, 0, math.pi, roll or 0)
        end,
        getRotation = function(self)
            local r = nodeRotations[self.yawNode] or { rx = 0, ry = 0, rz = 0 }
            return 0, r.ry, 0
        end
    }
    nodeRotations[10] = { rx = 0, ry = 0, rz = 0 }

    _G.g_currentMission = {
        controlledVehicle = nil,
        player = {
            camera = mockPlayerCam
        }
    }
    _G.g_time = 1000

    VRCompanion:onSnapTurnRightAction()
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001,
        "onSnapTurnRightAction must snap turn right to -pi/2")

    _G.g_time = 1300
    VRCompanion:onSnapTurnLeftAction()
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001,
        "onSnapTurnLeftAction must snap turn left back to 0.0")

    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 16: In-Engine Action Event Registration via g_inputBinding:registerActionEvent
test("InEngineActionEventRegistrationForSnapTurnLeftAndRight", function()
    VRCompanion:resetSnapTurnDebounce()

    local registeredEvents = {}
    local nextEventId = 100
    local eventTexts = {}
    local eventPriorities = {}
    local eventActive = {}
    local eventVisibility = {}

    _G.g_inputBinding = {
        events = {},
        actionEvents = {}, -- FS25: keyed by action name, not event id
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            nextEventId = nextEventId + 1
            local id = actionName .. "|table: 0x0|" .. nextEventId
            local eventObj = {
                id = id,
                actionName = actionName,
                target = target,
                callback = callback,
                triggerUp = triggerUp,
                triggerDown = triggerDown,
                triggerAlways = triggerAlways,
                startActive = startActive
            }
            registeredEvents[actionName] = eventObj
            self.events[id] = eventObj
            self.actionEvents[actionName] = { eventObj }
            return true, id
        end,
        setActionEventTextPriority = function(self, id, prio)
            eventPriorities[id] = prio
        end,
        setActionEventActive = function(self, id, active)
            eventActive[id] = active
        end,
        setActionEventTextVisibility = function(self, id, vis)
            eventVisibility[id] = vis
        end,
        setActionEventText = function(self, id, txt)
            eventTexts[id] = txt
        end
    }

    _G.g_i18n = {
        getText = function(self, key)
            if key == "input_VR_SNAP_TURN_LEFT" then
                return "VR Snap Turn Left"
            elseif key == "input_VR_SNAP_TURN_RIGHT" then
                return "VR Snap Turn Right"
            end
            return key
        end
    }

    -- Reset stored IDs to force registration
    VRCompanion.snapLeftActionEventId = nil
    VRCompanion.snapRightActionEventId = nil
    VRCompanion.snapLeftLoggedOnce = nil
    VRCompanion.snapRightLoggedOnce = nil

    -- 1. Call registerActionEvents(true)
    VRCompanion:registerActionEvents(true)

    -- Assert VR_SNAP_TURN_LEFT was registered
    local leftEvent = registeredEvents["VR_SNAP_TURN_LEFT"]
    assert(leftEvent ~= nil, "VR_SNAP_TURN_LEFT must be registered with g_inputBinding")
    assert(leftEvent.target == VRCompanion, "leftEvent target must be VRCompanion")
    assert(leftEvent.callback == VRCompanion.onSnapTurnLeftAction, "leftEvent callback must be onSnapTurnLeftAction")
    assert(leftEvent.triggerDown == true, "leftEvent triggerDown must be true")
    assert(leftEvent.triggerUp == false, "leftEvent triggerUp must be false")
    assert(leftEvent.startActive == true, "leftEvent startActive must be true")
    assert(VRCompanion.snapLeftActionEventId == leftEvent.id, "snapLeftActionEventId must match registered ID")
    assert(eventTexts[leftEvent.id] == "VR Snap Turn Left", "Event text must be set from l10n")
    assert(eventActive[leftEvent.id] == true, "Event must be activated")
    assert(eventVisibility[leftEvent.id] == true, "Event text visibility must be true")

    -- Assert VR_SNAP_TURN_RIGHT was registered
    local rightEvent = registeredEvents["VR_SNAP_TURN_RIGHT"]
    assert(rightEvent ~= nil, "VR_SNAP_TURN_RIGHT must be registered with g_inputBinding")
    assert(rightEvent.target == VRCompanion, "rightEvent target must be VRCompanion")
    assert(rightEvent.callback == VRCompanion.onSnapTurnRightAction, "rightEvent callback must be onSnapTurnRightAction")
    assert(rightEvent.triggerDown == true, "rightEvent triggerDown must be true")
    assert(rightEvent.triggerUp == false, "rightEvent triggerUp must be false")
    assert(rightEvent.startActive == true, "rightEvent startActive must be true")
    assert(VRCompanion.snapRightActionEventId == rightEvent.id, "snapRightActionEventId must match registered ID")
    assert(eventTexts[rightEvent.id] == "VR Snap Turn Right", "Event text must be set from l10n")
    assert(eventActive[rightEvent.id] == true, "Event must be activated")
    assert(eventVisibility[rightEvent.id] == true, "Event text visibility must be true")

    -- 2. Test firing the registered callbacks
    local mockCam = {
        isInside = true,
        cameraNode = 1001,
        origRotY = 0.0,
        rotY = 0.0
    }
    _G.g_currentMission = {
        controlledVehicle = {
            getActiveCamera = function() return mockCam end
        }
    }
    _G.g_time = 1000

    -- Invoke right callback via target:callback()
    rightEvent.callback(rightEvent.target)
    assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Invoking right callback must snap turn right to -pi/2")

    _G.g_time = 1300
    -- Invoke left callback
    leftEvent.callback(leftEvent.target)
    assert(math.abs(mockCam.rotY - 0.0) < 0.0001, "Invoking left callback must snap turn left back to 0.0")

    -- 3. Verify re-arming: clearing actionEvents and calling registerActionEvents(false) re-registers
    local preCount = nextEventId
    _G.g_inputBinding.events = {} -- cleared by engine during vehicle switch
    _G.g_inputBinding.actionEvents = {}
    VRCompanion:registerActionEvents(false)
    assert(nextEventId > preCount, "registerActionEvents(false) must re-register missing snap turn events")

    _G.g_inputBinding = nil
    _G.g_i18n = nil
    VRCompanion:resetSnapTurnDebounce()
end)

-- Slice 17: XML Configuration Loading and Saving of <snapTurnAngle> (Issue #37)
test("SavesAndLoadsSnapTurnAngleConfiguration", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) return true end
    _G.createXMLFile = function(tag, path, rootNode) return 999 end
    _G.setXMLFloat = function(xmlFile, key, value) savedData[key] = value end
    _G.setXMLBool = function(xmlFile, key, value) savedData[key] = value end
    _G.saveXMLFile = function(xmlFile) return true end
    _G.delete = function(xmlFile) end

    -- 1. Test saving default 90 degrees
    VRCompanion.snapTurnAngle = math.pi * 0.5
    VRCompanion:saveConfiguration()
    assert(savedData["vrSettings.snapTurnAngle"] ~= nil, "vrSettings.snapTurnAngle must be written by saveConfiguration")
    assert(math.abs(savedData["vrSettings.snapTurnAngle"] - 90.0) < 0.001,
        string.format("Expected snapTurnAngle 90.0 deg, got %s", tostring(savedData["vrSettings.snapTurnAngle"])))

    -- 2. Test saving custom 45 degrees
    VRCompanion.snapTurnAngle = math.rad(45.0)
    VRCompanion:saveConfiguration()
    assert(math.abs(savedData["vrSettings.snapTurnAngle"] - 45.0) < 0.001,
        string.format("Expected snapTurnAngle 45.0 deg, got %s", tostring(savedData["vrSettings.snapTurnAngle"])))

    -- 3. Test loading from user settings.xml (e.g. 45 degrees)
    _G.loadXMLFile = function(tag, path) return 888 end
    _G.getXMLFloat = function(xmlFile, key)
        if key == "vrSettings.snapTurnAngle" then return 45.0 end
        return nil
    end
    _G.getXMLBool = function(xmlFile, key) return nil end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45.0)) < 0.0001,
        string.format("Must load 45 deg (%.4f rad) from settings.xml, got %.4f", math.rad(45.0), VRCompanion:getSnapTurnAngle()))

    -- 4. A modDesc.vrSettings key is not a settings source: the angle must stay at the previous value
    _G.getXMLFloat = function(xmlFile, key)
        if key == "modDesc.vrSettings.snapTurnAngle" then return 30.0 end
        return nil
    end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45.0)) < 0.0001,
        "modDesc.vrSettings.snapTurnAngle must not be read as a settings key")

    -- Clean up mocks
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.saveXMLFile = nil
    _G.loadXMLFile = nil
    _G.getXMLFloat = nil
    _G.getXMLBool = nil
    _G.delete = nil
    VRCompanion.snapTurnAngle = VRCompanion.DEFAULT_SNAP_TURN_ANGLE
end)

-- Slice 18: Runtime Console Command vrSnapTurnAngle <degrees> (Issue #37)
test("ConsoleCommandQueriesAndSetsSnapTurnAngle", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) return true end
    _G.createXMLFile = function(tag, path, rootNode) return 999 end
    _G.setXMLFloat = function(xmlFile, key, value) savedData[key] = value end
    _G.setXMLBool = function(xmlFile, key, value) savedData[key] = value end
    _G.saveXMLFile = function(xmlFile) return true end
    _G.delete = function(xmlFile) end

    local warningShown = nil
    _G.g_currentMission = {
        showBlinkingWarning = function(self, msg, duration)
            warningShown = msg
        end
    }

    -- Reset to default 90 degrees
    VRCompanion.snapTurnAngle = math.pi * 0.5

    -- 1. Query current angle with no arguments
    local queryRes = VRCompanion:consoleCommandSetSnapTurnAngle()
    assert(queryRes ~= nil, "Query must return a status string")
    assert(string.find(queryRes, "90") ~= nil, "Query response must show current 90 deg")
    assert(string.find(queryRes, "vrSnapTurnAngle") ~= nil, "Query response must show usage")

    -- Query with "status"
    local statusRes = VRCompanion:consoleCommandSetSnapTurnAngle("status")
    assert(string.find(statusRes, "90") ~= nil, "status query must show current 90 deg")

    -- 2. Update to 45 degrees
    local set45Res = VRCompanion:consoleCommandSetSnapTurnAngle("45")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45.0)) < 0.0001,
        string.format("VRCompanion angle must be updated to 45 deg (%.4f rad), got %.4f", math.rad(45.0), VRCompanion:getSnapTurnAngle()))
    assert(string.find(set45Res, "45") ~= nil, "Response must confirm 45 deg")
    assert(savedData["vrSettings.snapTurnAngle"] == 45.0, "Configuration must be saved on change")
    assert(warningShown ~= nil and string.find(warningShown, "45") ~= nil, "Blinking warning must be shown")

    -- 3. Update to 30 degrees
    warningShown = nil
    local set30Res = VRCompanion:consoleCommandSetSnapTurnAngle("30")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(30.0)) < 0.0001, "Angle must update to 30 deg")
    assert(savedData["vrSettings.snapTurnAngle"] == 30.0, "Saved config must update to 30 deg")

    -- 4. Update to 180 degrees
    local set180Res = VRCompanion:consoleCommandSetSnapTurnAngle("180")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(180.0)) < 0.0001, "Angle must update to 180 deg")
    assert(savedData["vrSettings.snapTurnAngle"] == 180.0, "Saved config must update to 180 deg")

    -- 4b. Small degree values (e.g. 5 deg) must be parsed as degrees, not radians
    local set5Res = VRCompanion:consoleCommandSetSnapTurnAngle("5")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(5.0)) < 0.0001,
        string.format("Angle 5 deg must be converted to radians (%.4f), got %.4f", math.rad(5.0), VRCompanion:getSnapTurnAngle()))
    assert(savedData["vrSettings.snapTurnAngle"] == 5.0, "Saved config must update to 5 deg")

    -- 5. Invalid inputs must return usage and NOT change angle
    local prevAngle = VRCompanion:getSnapTurnAngle()
    local inv1 = VRCompanion:consoleCommandSetSnapTurnAngle("notanumber")
    assert(string.find(inv1, "Usage:") ~= nil, "Invalid string must return usage error")
    assert(VRCompanion:getSnapTurnAngle() == prevAngle, "Angle must not change on invalid input")

    local inv2 = VRCompanion:consoleCommandSetSnapTurnAngle("0")
    assert(string.find(inv2, "Usage:") ~= nil, "Zero angle must return usage error")
    assert(VRCompanion:getSnapTurnAngle() == prevAngle, "Angle must not change on zero")

    local inv3 = VRCompanion:consoleCommandSetSnapTurnAngle("-45")
    assert(string.find(inv3, "Usage:") ~= nil, "Negative angle must return usage error")
    assert(VRCompanion:getSnapTurnAngle() == prevAngle, "Angle must not change on negative")

    local inv4 = VRCompanion:consoleCommandSetSnapTurnAngle("400")
    assert(string.find(inv4, "Usage:") ~= nil, "Angle > 360 must return usage error")
    assert(VRCompanion:getSnapTurnAngle() == prevAngle, "Angle must not change on > 360")

    -- Clean up mocks
    _G.getUserProfileAppPath = nil
    _G.createFolder = nil
    _G.createXMLFile = nil
    _G.setXMLFloat = nil
    _G.setXMLBool = nil
    _G.saveXMLFile = nil
    _G.delete = nil
    _G.g_currentMission = nil
    VRCompanion.snapTurnAngle = VRCompanion.DEFAULT_SNAP_TURN_ANGLE
end)

-- Slice 19: modDesc.xml Schema, Action Bindings, L10n & Settings Invariants (Issue #37)
test("ModDescDeclaresSnapTurnActionsBindingsL10nAndSettings", function()
    local modDescContent = fs.readFile("mods/FS25_VRCompanion/modDesc.xml")

    -- 1. Actions under category ONFOOT VEHICLE
    assert(string.find(modDescContent, '<action name="VR_SNAP_TURN_LEFT" category="ONFOOT VEHICLE"/>') ~= nil,
        "modDesc.xml must declare VR_SNAP_TURN_LEFT action under category ONFOOT VEHICLE")
    assert(string.find(modDescContent, '<action name="VR_SNAP_TURN_RIGHT" category="ONFOOT VEHICLE"/>') ~= nil,
        "modDesc.xml must declare VR_SNAP_TURN_RIGHT action under category ONFOOT VEHICLE")

    -- 2. Input Bindings: KEY_numPad4 / KEY_numPad6 and Gamepad BUTTON_5 / BUTTON_6
    assert(string.find(modDescContent, '<actionBinding action="VR_SNAP_TURN_LEFT">') ~= nil,
        "modDesc.xml must declare actionBinding for VR_SNAP_TURN_LEFT")
    assert(string.find(modDescContent, '<actionBinding action="VR_SNAP_TURN_RIGHT">') ~= nil,
        "modDesc.xml must declare actionBinding for VR_SNAP_TURN_RIGHT")

    -- Check left bindings
    local leftBindingMatch = string.match(modDescContent, '<actionBinding action="VR_SNAP_TURN_LEFT">(.-)</actionBinding>')
    assert(leftBindingMatch ~= nil, "Must extract VR_SNAP_TURN_LEFT binding block")
    assert(string.find(leftBindingMatch, 'input="KEY_numPad4"') ~= nil, "Left binding must map to KEY_numPad4")
    assert(string.find(leftBindingMatch, 'input="BUTTON_5"') ~= nil, "Left binding must map to Gamepad BUTTON_5")

    -- Check right bindings
    local rightBindingMatch = string.match(modDescContent, '<actionBinding action="VR_SNAP_TURN_RIGHT">(.-)</actionBinding>')
    assert(rightBindingMatch ~= nil, "Must extract VR_SNAP_TURN_RIGHT binding block")
    assert(string.find(rightBindingMatch, 'input="KEY_numPad6"') ~= nil, "Right binding must map to KEY_numPad6")
    assert(string.find(rightBindingMatch, 'input="BUTTON_6"') ~= nil, "Right binding must map to Gamepad BUTTON_6")

    -- 3. Localization strings in English and German
    assert(string.find(modDescContent, '<text name="input_VR_SNAP_TURN_LEFT">') ~= nil, "Must have l10n for input_VR_SNAP_TURN_LEFT")
    assert(string.find(modDescContent, '<text name="input_VR_SNAP_TURN_RIGHT">') ~= nil, "Must have l10n for input_VR_SNAP_TURN_RIGHT")

    assert(string.find(modDescContent, '<text name="setting_vrSnapTurnAngle">') ~= nil, "Must have l10n for setting_vrSnapTurnAngle")

    -- Check German translations exist
    assert(string.find(modDescContent, "VR%-Blickdrehung links") ~= nil, "Must have German translation for snap turn left")
    assert(string.find(modDescContent, "VR%-Blickdrehung rechts") ~= nil, "Must have German translation for snap turn right")
    assert(string.find(modDescContent, "VR%-Blickdrehungswinkel") ~= nil, "Must have German translation for snap turn angle setting")

    -- 4. Defaults live in the DEFAULT_* constants, not in modDesc.xml (Issue #83)
    assert(string.find(modDescContent, "<vrSettings>", 1, true) == nil, "modDesc.xml must not define a <vrSettings> default block")
end)

-- ====================================================================
-- Outside Camera Snap Turn and Recenter Action (Issue #46)
-- ====================================================================

-- Fake VRBridge that counts discontinuity signals and recenters
local function installCountingBridge()
    local calls = { discontinuity = 0, recenter = 0 }
    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function() return { isValid = true, x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0 } end,
        signalDiscontinuity = function() calls.discontinuity = calls.discontinuity + 1 end,
        recenter = function() calls.recenter = calls.recenter + 1; return { success = true } end,
        resetTemporalHistory = function() end,
    }
    return calls
end

local function controlOutsideCamera(cam)
    local veh = { getActiveCamera = function() return cam end }
    _G.g_currentMission = { controlledVehicle = veh }
    return veh
end

test("OutsideCameraSnapTurnStepsOrbitYawBySnapAngleAndSignalsOneDiscontinuity", function()
    VRCompanion:resetSnapTurnDebounce()
    local calls = installCountingBridge()

    local orbitCam = {
        isInside = false,
        isRotatable = true,
        cameraNode = 1101,
        rotateNode = 1102,
        origRotX = 0.3, origRotY = 0.0,
        rotX = 0.5, rotY = 2.8, -- player already orbited with the mouse
        zoom = 12.0,
    }
    controlOutsideCamera(orbitCam)

    -- Snap right by 90 deg: 2.8 - pi/2
    local ok, newRot = VRCompanion:snapTurnRight(nil, 1000)
    assert(ok == true, "Snap turn must be handled in a rotatable Outside Camera")
    assert(math.abs(orbitCam.rotY - (2.8 - math.pi * 0.5)) < 0.0001,
        string.format("Orbit yaw must step right by pi/2, got %f", orbitCam.rotY))
    assert(newRot == orbitCam.rotY, "Snap turn must report the new orbit yaw")
    assert(calls.discontinuity == 1, string.format("Expected one discontinuity, got %d", calls.discontinuity))
    assert(orbitCam.rotX == 0.5, "Orbit pitch must not change")
    assert(orbitCam.zoom == 12.0, "Zoom must not change")

    -- Snap left twice from 2.8 - pi/2: +pi/2 -> 2.8, +pi/2 -> 2.8 + pi/2 wraps to 2.8 + pi/2 - 2pi
    VRCompanion:snapTurnLeft(nil, 1300)
    assert(math.abs(orbitCam.rotY - 2.8) < 0.0001, string.format("Orbit yaw must return to 2.8, got %f", orbitCam.rotY))
    VRCompanion:snapTurnLeft(nil, 1600)
    assert(math.abs(orbitCam.rotY - (2.8 + math.pi * 0.5 - 2 * math.pi)) < 0.0001,
        string.format("Orbit yaw must be normalized into [-pi, pi], got %f", orbitCam.rotY))
    assert(calls.discontinuity == 3, "Each snap turn must signal one discontinuity")

    VRCompanion:resetSnapTurnDebounce()
end)

test("OutsideCameraSnapTurnHonorsDebounce", function()
    VRCompanion:resetSnapTurnDebounce()
    local calls = installCountingBridge()

    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1201, origRotX = 0, origRotY = 0, rotX = 0, rotY = 0 }
    controlOutsideCamera(orbitCam)

    assert(VRCompanion:snapTurnRight(nil, 1000) == true, "First snap turn must succeed")
    local ok, reason = VRCompanion:snapTurnRight(nil, 1100)
    assert(ok == false and reason == "debounced", "Snap turn within the cooldown must be debounced")
    assert(math.abs(orbitCam.rotY - (-math.pi * 0.5)) < 0.0001, "Debounced snap turn must not move the orbit")
    assert(calls.discontinuity == 1, "Debounced snap turn must not signal a discontinuity")

    VRCompanion:resetSnapTurnDebounce()
end)

test("FixedOutsideCameraSnapTurnIsNotHandled", function()
    VRCompanion:resetSnapTurnDebounce()
    local calls = installCountingBridge()

    -- A hitch camera: fixed (not rotatable), aimed down at the drawbar
    local hitchCam = { isInside = false, isRotatable = false, cameraNode = 1301, origRotX = -0.9, origRotY = math.pi, rotX = -0.9, rotY = math.pi }
    controlOutsideCamera(hitchCam)

    local ok, reason = VRCompanion:snapTurnRight(nil, 1000)
    assert(ok == false, "Snap turn must not be handled in a fixed Outside Camera")
    assert(reason ~= nil, "Snap turn must report why it wasn't handled")
    assert(hitchCam.rotY == math.pi and hitchCam.rotX == -0.9, "Fixed Outside Camera must be left unchanged")
    assert(calls.discontinuity == 0, "Unhandled snap turn must not signal a discontinuity")

    -- Not handled means no cooldown starts: a snap turn right after the next camera switch still works
    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1302, origRotX = 0, origRotY = 0, rotX = 0, rotY = 0 }
    controlOutsideCamera(orbitCam)
    assert(VRCompanion:snapTurnRight(nil, 1050) == true, "An unhandled snap turn must not start the debounce cooldown")

    VRCompanion:resetSnapTurnDebounce()
end)

test("RecenterInOutsideCameraSwingsOrbitBackToDefaultRecordedOnActivation", function()
    VRCompanion:resetSnapTurnDebounce()
    VRCompanion.recenterVrTrackingSpace = true
    local calls = installCountingBridge()

    -- Engine state when the orbit camera becomes active: this is "forward" for the camera
    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1401, rotateNode = 1402, rotX = 0.35, rotY = 0.2, zoom = 15.0 }
    local veh = controlOutsideCamera(orbitCam)
    VRCompanion:onVehicleCameraChanged(veh, 2)

    -- Player orbits with the mouse, zooms in, and snap turns
    orbitCam.rotX = 0.9
    orbitCam.rotY = -1.1
    orbitCam.zoom = 8.0
    VRCompanion:snapTurnLeft(nil, 1000)
    local flushesBefore = calls.discontinuity

    VRCompanion:onRecenterAction()

    assert(calls.discontinuity == flushesBefore + 1, "Recenter must signal one discontinuity")
    assert(calls.recenter == 1, "Recenter must recenter the OpenXR reference space")
    assert(math.abs(orbitCam.rotY - 0.2) < 0.0001, string.format("Orbit yaw must reset to 0.2, got %f", orbitCam.rotY))
    assert(math.abs(orbitCam.rotX - 0.35) < 0.0001, string.format("Orbit pitch must reset to 0.35, got %f", orbitCam.rotX))
    assert(orbitCam.zoom == 8.0, "Recenter must leave zoom alone")

    VRCompanion:resetSnapTurnDebounce()
end)

test("RecenterInOutsideCameraInVehicleOnlyModeSkipsTrackingRecenterButStillResetsOrbit", function()
    VRCompanion:resetSnapTurnDebounce()
    VRCompanion.recenterVrTrackingSpace = false
    local calls = installCountingBridge()

    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1501, rotX = 0.4, rotY = 1.0, zoom = 10.0 }
    local veh = controlOutsideCamera(orbitCam)
    VRCompanion:onVehicleCameraChanged(veh, 2)
    orbitCam.rotX = -0.2
    orbitCam.rotY = 2.5

    VRCompanion:onRecenterAction()

    assert(calls.discontinuity == 1, "Recenter must signal one discontinuity")
    assert(calls.recenter == 0, "VehicleOnly mode must not recenter the OpenXR reference space")
    assert(math.abs(orbitCam.rotY - 1.0) < 0.0001 and math.abs(orbitCam.rotX - 0.4) < 0.0001,
        "Orbit yaw and pitch must reset to the default in VehicleOnly mode too")
    assert(orbitCam.zoom == 10.0, "Recenter must leave zoom alone")

    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_snap_turn.luau", failed))


