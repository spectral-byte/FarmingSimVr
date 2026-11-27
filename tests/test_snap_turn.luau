_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0

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

print("Running In-Cab Snap Turn, State & Debounce Tests...")

test("SnapTurnStateMachineDefaultsAndConfigurableTurnAngle", function()
    assert(VRCompanion.DEFAULT_SNAP_TURN_ANGLE ~= nil, "DEFAULT_SNAP_TURN_ANGLE constant must be defined")
    assert(math.abs(VRCompanion.DEFAULT_SNAP_TURN_ANGLE - (math.pi * 0.5)) < 0.0001,
        string.format("DEFAULT_SNAP_TURN_ANGLE must be pi/2 (1.5708 rad), got %f", VRCompanion.DEFAULT_SNAP_TURN_ANGLE))
    assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001,
        "getSnapTurnAngle must return pi/2 by default")

    assert(VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS ~= nil, "DEFAULT_SNAP_TURN_DEBOUNCE_MS constant must be defined")
    assert(VRCompanion.DEFAULT_SNAP_TURN_DEBOUNCE_MS >= 250, "DEFAULT_SNAP_TURN_DEBOUNCE_MS must be at least 250ms")

    VRCompanion:setSnapTurnAngle(math.pi * 0.25)
    assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.25)) < 0.0001, "Configured snap turn angle to pi/4")

    VRCompanion:setSnapTurnAngle(90)
    assert(math.abs(VRCompanion:getSnapTurnAngle() - (math.pi * 0.5)) < 0.0001, "Setting 90 degrees should convert to pi/2 radians")

    VRCompanion:setSnapTurnAngle(math.pi * 0.5)
end)

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

    local ok1, newRot1 = VRCompanion:snapTurnRight(nil, 1000)
    assert(ok1 == true, "First snap turn at t=1000 must succeed")
    assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Camera rotY must be -pi/2")

    local ok2, err2 = VRCompanion:snapTurnRight(nil, 1100)
    assert(ok2 == false, "Snap turn 100ms later must be rejected by debounce")
    assert(err2 == "debounced", "Error message must indicate debounced")
    assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Camera rotY must not change during cooldown")

    local ok3, _ = VRCompanion:snapTurnRight(nil, 1240)
    assert(ok3 == false, "Snap turn 240ms later must still be debounced")

    local ok4, newRot4 = VRCompanion:snapTurnRight(nil, 1250)
    assert(ok4 == true, "Snap turn at t=1250 (250ms elapsed) must succeed")
    assert(math.abs(math.abs(mockCam.rotY) - math.pi) < 0.0001, "Camera rotY must advance to -pi / pi (180 deg)")

    VRCompanion:resetSnapTurnDebounce()
end)

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
        origHeadTrackingRotY = math.pi,
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

    local okR, newRotR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == true, "snapTurnRight must succeed")
    assert(math.abs(testCam.rotY - (math.pi * 0.5)) < 0.0001,
        string.format("Expected cam.rotY = +pi/2 (1.571), got %f", testCam.rotY))

    local headRotR = nodeRotations[302]
    assert(headRotR ~= nil, "headTrackingNode rotation must be updated")
    assert(math.abs(headRotR.ry - (math.pi * 0.5)) < 0.0001,
        string.format("headTrackingNode yaw must equal +pi/2, got %f", headRotR.ry))
    assert(math.abs(headRotR.rx) < 0.0001 and math.abs(headRotR.rz) < 0.0001, "Pitch and roll must remain 0")

    local savedState = VRCompanion:getVehicleRotationState(veh, testCam)
    assert(savedState ~= nil, "Vehicle rotation state must exist")
    assert(math.abs(savedState.rotY - (math.pi * 0.5)) < 0.0001,
        "Vehicle rotation state must track snapped rotY")

    local okL, newRotL = VRCompanion:snapTurnLeft(nil, 1300)
    assert(okL == true, "snapTurnLeft must succeed after cooldown")
    assert(math.abs(math.abs(testCam.rotY) - math.pi) < 0.0001,
        string.format("Expected cam.rotY = pi (-pi), got %f", testCam.rotY))

    local headRotL = nodeRotations[302]
    assert(math.abs(math.abs(headRotL.ry) - math.pi) < 0.0001,
        string.format("headTrackingNode yaw must equal pi (-pi), got %f", headRotL.ry))

    VRCompanion:resetSnapTurnDebounce()
end)

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
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(testCam.rotY - (-math.pi * 0.5)) < 0.0001, "Step 1: -pi/2")

    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(math.abs(testCam.rotY) - math.pi) < 0.0001, "Step 2: pi")

    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(testCam.rotY - (math.pi * 0.5)) < 0.0001, "Step 3: wraps to +pi/2")

    t = t + 300
    VRCompanion:snapTurnRight(nil, t)
    assert(math.abs(testCam.rotY - 0.0) < 0.0001, "Step 4: completes full circle to 0.0")

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

    VRCompanion:snapTurnRight(nil, 1000)

    VRCompanion:onVehicleCameraUpdate(cam, 16.6)

    local headRot = nodeRotations[502]
    assert(math.abs(headRot.ry - (math.pi * 0.5)) < 0.001, "headTrackingNode yaw must equal +pi/2")

    local camRot = nodeRotations[501]
    local expectedCamYaw = VRCompanion:normalizeAngle(math.pi * 0.5 + 0.08)
    assert(math.abs(camRot.ry - expectedCamYaw) < 0.001,
        string.format("Camera composite yaw must be %f, got %f", expectedCamYaw, camRot.ry))

    assert(math.abs(camRot.rx - 0.12) < 0.001, "Physical head pitch must be preserved")
    assert(math.abs(camRot.rz - (-0.01)) < 0.001, "Physical head roll must be preserved")

    local camTrans = nodeTranslations[501]
    local expX, expY, expZ = VRCompanion:rotateVectorByYaw(0.05, 0.02, -0.10, math.pi * 0.5)
    assert(math.abs(camTrans.x - (0.0 + expX)) < 0.001, "Collinear translation x mismatch")
    assert(math.abs(camTrans.y - (1.6 + 0.02)) < 0.001, "Collinear translation y mismatch")
    assert(math.abs(camTrans.z - (0.1 + expZ)) < 0.001, "Collinear translation z mismatch")

    VRCompanion:resetSnapTurnDebounce()
end)

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
        origRotY = math.pi,
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

    VRCompanion:snapTurnRight(nil, 1000)
    VRCompanion:snapTurnRight(nil, 1300)
    assert(math.abs(cam.rotY - 0.0) < 0.0001, "Camera facing backward (rotY = 0)")

    VRCompanion:onRecenterAction()

    assert(math.abs(cam.rotY - cam.origRotY) < 0.0001,
        string.format("Recenter must reset rotY back to origRotY (%.3f), got %.3f", cam.origRotY, cam.rotY))

    local headRot = nodeRotations[602]
    assert(math.abs(headRot.ry - math.pi) < 0.001,
        string.format("headTrackingNode yaw must reset to pi (180 deg), got %f", headRot.ry))

    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    local camRot = nodeRotations[601]
    local lookX, _, lookZ = VRCompanion:rotateVectorByYaw(0, 0, -1, camRot.ry)
    assert(lookZ > 0.99, string.format("View after recenter must face forward (+Z), but lookZ = %f", lookZ))

    VRCompanion:resetSnapTurnDebounce()
end)

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

    local ok, newRot = VRCompanion:snapTurnLeft(nil, 1000)
    assert(ok == true, "Fallback camera snapTurnLeft must succeed")
    assert(math.abs(fallbackCam.rotY - (math.pi * 0.5)) < 0.0001, "Fallback rotY must be +pi/2")

    VRCompanion:onRecenterAction()
    assert(math.abs(fallbackCam.rotY - 0.0) < 0.0001, "Recenter resets fallback cam rotY to 0.0")

    VRCompanion:resetSnapTurnDebounce()
end)

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

test("SnapTurnIsRefusedWhileAMenuTakesTheInputEvenWithoutTheMenuQuad", function()
    VRCompanion:resetSnapTurnDebounce()

    local mockCam = { isInside = true, cameraNode = 902, origRotY = 0.0, rotY = 0.0 }
    _G.g_currentMission = {
        controlledVehicle = { getActiveCamera = function() return mockCam end }
    }

    VRCompanion.isMenuVisible = false
    VRCompanion.menuTakesInput = true
    local ok, err = VRCompanion:snapTurnRight(nil, 1000)
    assert(ok == false, "Snap turn must be rejected while a menu takes the input")
    assert(err == "menu_visible", "Error reason must be menu_visible, got " .. tostring(err))
    assert(math.abs(mockCam.rotY - 0.0) < 0.0001, "Camera rotY must not change while a menu takes the input")

    VRCompanion.menuTakesInput = false
    VRCompanion:resetSnapTurnDebounce()
end)

test("PollingAnOpenGuiRefusesSnapTurnUntilItCloses", function()
    VRCompanion:resetSnapTurnDebounce()

    local mockCam = { isInside = true, cameraNode = 903, origRotY = 0.0, rotY = 0.0 }
    _G.g_currentMission = {
        controlledVehicle = { getActiveCamera = function() return mockCam end }
    }
    local guiVisible = true
    _G.g_gui = { getIsGuiVisible = function() return guiVisible end }

    VRCompanion:pollGuiVisibility()
    local okOpen = VRCompanion:snapTurnRight(nil, 1000)
    assert(okOpen == false, "Snap turn must be rejected once polling sees the GUI open")

    guiVisible = false
    VRCompanion:pollGuiVisibility()
    local okClosed = VRCompanion:snapTurnRight(nil, 1300)
    assert(okClosed == true, "Snap turn must work again once polling sees the GUI closed")

    _G.g_gui = nil
    VRCompanion:resetSnapTurnDebounce()
end)

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

    local okR, newYawR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == true, "On-foot snapTurnRight must succeed")
    assert(math.abs(newYawR - (-math.pi * 0.5)) < 0.0001,
        string.format("Expected newYawR = -pi/2 (-1.571), got %s", tostring(newYawR)))
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001,
        string.format("yawNode rotation must equal -pi/2, got %f", nodeRotations[10].ry))

    local okL, newYawL = VRCompanion:snapTurnLeft(nil, 1300)
    assert(okL == true, "On-foot snapTurnLeft must succeed after cooldown")
    assert(math.abs(newYawL - 0.0) < 0.0001,
        string.format("Expected newYawL = 0.0, got %s", tostring(newYawL)))
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001,
        string.format("yawNode rotation must equal 0.0, got %f", nodeRotations[10].ry))

    VRCompanion:resetSnapTurnDebounce()
end)

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

    local camLookX, _, camLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
    local walkWX, walkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1)
    local dotInitial = camLookX * walkWX + camLookZ * walkWZ
    assert(math.abs(dotInitial - 1.0) < 0.001, "Initial gaze and W walk must be collinear forward")

    local okR, newYawR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == true, "snapTurnRight must succeed")
    assert(math.abs(newYawR - (-math.pi * 0.5)) < 0.0001, "newYawR must be -pi/2")
    assert(math.abs(nodes[10].rot[2] - (-math.pi * 0.5)) < 0.0001, "yawNode rot must be -pi/2")

    local snappedLookX, _, snappedLookZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 0, 0, -1)
    local snappedWalkWX, snappedWalkWZ = mockInput:calculateNormalisedWorldMovementDirection(0, 1)
    local dotW = snappedLookX * snappedWalkWX + snappedLookZ * snappedWalkWZ
    assert(math.abs(dotW - 1.0) < 0.001,
        string.format("After snap turn right, W must walk collinear with gaze! dotW = %f", dotW))
    assert(math.abs(snappedLookX - (-1.0)) < 0.001, "Snapped gaze must face -X (right)")
    assert(math.abs(snappedWalkWX - (-1.0)) < 0.001, "Snapped W walk must face -X (right)")

    local snappedWalkDX, snappedWalkDZ = mockInput:calculateNormalisedWorldMovementDirection(1, 0)
    local snappedWalkAX, snappedWalkAZ = mockInput:calculateNormalisedWorldMovementDirection(-1, 0)
    local camRightX, _, camRightZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, 1, 0, 0)
    local camLeftX, _, camLeftZ = _G.localDirectionToWorld(mockPlayerCam.firstPersonCamera, -1, 0, 0)

    local dotD = camRightX * snappedWalkDX + camRightZ * snappedWalkDZ
    assert(math.abs(dotD - 1.0) < 0.001,
        string.format("D strafe must match camera right after snap turn! dotD = %f", dotD))

    local dotA = camLeftX * snappedWalkAX + camLeftZ * snappedWalkAZ
    assert(math.abs(dotA - 1.0) < 0.001,
        string.format("A strafe must match camera left after snap turn! dotA = %f", dotA))

    mockInput.cameraRotationY = 0
    VRCompanion:onPlayerCameraUpdateRotation(mockPlayerCam, 16.6)
    local _, updatedYaw, _ = _G.getRotation(mockPlayerCam.yawNode)
    assert(math.abs(updatedYaw - (-math.pi * 0.5)) < 0.001,
        string.format("yawNode must maintain -pi/2 without drift during subsequent frame update, got %f", updatedYaw))

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

    VRCompanion.isMenuVisible = true

    local okR, errR = VRCompanion:snapTurnRight(nil, 1000)
    assert(okR == false, "On-foot snap turn right must be rejected when menu is visible")
    assert(errR == "menu_visible", "Error reason must be menu_visible")
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode must not change while menu is open")

    local okL, errL = VRCompanion:snapTurnLeft(nil, 1100)
    assert(okL == false, "On-foot snap turn left must be rejected when menu is visible")
    assert(errL == "menu_visible", "Error reason must be menu_visible")
    assert(math.abs(nodeRotations[10].ry - 0.0) < 0.0001, "yawNode must not change while menu is open")

    VRCompanion.isMenuVisible = false
    local okAfter, newYawAfter = VRCompanion:snapTurnRight(nil, 1300)
    assert(okAfter == true, "On-foot snap turn must succeed after menu is closed")
    assert(math.abs(newYawAfter - (-math.pi * 0.5)) < 0.0001, "yawNode must step to -pi/2")
    assert(math.abs(nodeRotations[10].ry - (-math.pi * 0.5)) < 0.0001, "yawNode rotation must equal -pi/2")

    VRCompanion:resetSnapTurnDebounce()
end)

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

    VRCompanion.snapLeftActionEventId = nil
    VRCompanion.snapRightActionEventId = nil
    VRCompanion.snapLeftLoggedOnce = nil
    VRCompanion.snapRightLoggedOnce = nil

    VRCompanion:registerActionEvents(true)

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

    rightEvent.callback(rightEvent.target)
    assert(math.abs(mockCam.rotY - (-math.pi * 0.5)) < 0.0001, "Invoking right callback must snap turn right to -pi/2")

    _G.g_time = 1300
    leftEvent.callback(leftEvent.target)
    assert(math.abs(mockCam.rotY - 0.0) < 0.0001, "Invoking left callback must snap turn left back to 0.0")

    local preCount = nextEventId
    _G.g_inputBinding.events = {}
    _G.g_inputBinding.actionEvents = {}
    VRCompanion:registerActionEvents(false)
    assert(nextEventId > preCount, "registerActionEvents(false) must re-register missing snap turn events")

    _G.g_inputBinding = nil
    _G.g_i18n = nil
    VRCompanion:resetSnapTurnDebounce()
end)

test("ActionEventsRegisterIntoTheGameplayContextAndFollowItIntoAndOutOfVehicles", function()
    local nextEventId = 0
    local contexts = {}  -- context name -> action name -> event
    local inputBinding
    inputBinding = {
        events = {},
        currentContextName = "MENU",
        registrationContextName = nil,
        beginActionEventsModification = function(self, name) self.registrationContextName = name end,
        endActionEventsModification = function(self) self.registrationContextName = nil end,
        registerActionEvent = function(self, actionName, target, callback)
            local contextName = self.registrationContextName or self.currentContextName
            contexts[contextName] = contexts[contextName] or {}
            assert(contexts[contextName][actionName] == nil, actionName .. " registered twice in " .. contextName)
            nextEventId = nextEventId + 1
            local event = { id = nextEventId, actionName = actionName, contextName = contextName, callback = callback }
            contexts[contextName][actionName] = event
            self.events[event.id] = event
            return true, event.id
        end,
        removeActionEvent = function(self, id)
            local event = self.events[id]
            if event == nil then return end
            assert(event.contextName == self.registrationContextName, "an event must be removed from the context it is in")
            contexts[event.contextName][event.actionName] = nil
            self.events[id] = nil
        end,
    }
    _G.g_inputBinding = inputBinding
    _G.g_currentMission = {}
    for _, field in ipairs({ "hudToggleActionEventId", "recenterActionEventId", "snapLeftActionEventId", "snapRightActionEventId", "toggleViewActionEventId", "seatSetActionEventId" }) do
        VRCompanion[field] = nil
    end
    VRCompanion.actionEventsContextName = nil

    local function eventsIn(contextName)
        local count = 0
        for _ in pairs(contexts[contextName] or {}) do count = count + 1 end
        return count
    end

    VRCompanion:registerActionEvents(true)
    assert(eventsIn("MENU") == 0, "no VR action event may land in the loading screen's menu context")
    assert(eventsIn("PLAYER") == 6, "all six VR action events must be in the on-foot context, got " .. eventsIn("PLAYER"))
    assert(inputBinding.registrationContextName == nil, "the registration context must be reset afterwards")

    local before = nextEventId
    VRCompanion:registerActionEvents(false)
    assert(nextEventId == before, "the periodic check must not re-register events that are in place")

    _G.g_currentMission.controlledVehicle = {}
    VRCompanion:registerActionEvents(false)
    assert(eventsIn("PLAYER") == 0 and eventsIn("VEHICLE") == 6, "the events must move to the vehicle context")
    assert(contexts.VEHICLE.VR_SNAP_TURN_LEFT.id == VRCompanion.snapLeftActionEventId, "the stored id must be the new event's")

    VRCompanion:registerActionEvents(true)
    assert(eventsIn("VEHICLE") == 6, "a forced registration must not duplicate events")

    _G.g_currentMission.controlledVehicle = nil
    VRCompanion:registerActionEvents(false)
    assert(eventsIn("VEHICLE") == 0 and eventsIn("PLAYER") == 6, "the events must move back to the on-foot context")

    _G.g_inputBinding = nil
    _G.g_currentMission = nil
    VRCompanion.actionEventsContextName = nil
end)

test("SavesAndLoadsSnapTurnAngleConfiguration", function()
    local savedData = {}
    _G.getUserProfileAppPath = function() return "C:/mockAppPath/" end
    _G.createFolder = function(folder) return true end
    _G.createXMLFile = function(tag, path, rootNode) return 999 end
    _G.setXMLFloat = function(xmlFile, key, value) savedData[key] = value end
    _G.setXMLBool = function(xmlFile, key, value) savedData[key] = value end
    _G.saveXMLFile = function(xmlFile) return true end
    _G.delete = function(xmlFile) end

    VRCompanion.snapTurnAngle = math.pi * 0.5
    VRCompanion:saveConfiguration()
    assert(savedData["vrSettings.snapTurnAngle"] ~= nil, "vrSettings.snapTurnAngle must be written by saveConfiguration")
    assert(math.abs(savedData["vrSettings.snapTurnAngle"] - 90.0) < 0.001,
        string.format("Expected snapTurnAngle 90.0 deg, got %s", tostring(savedData["vrSettings.snapTurnAngle"])))

    VRCompanion.snapTurnAngle = math.rad(45.0)
    VRCompanion:saveConfiguration()
    assert(math.abs(savedData["vrSettings.snapTurnAngle"] - 45.0) < 0.001,
        string.format("Expected snapTurnAngle 45.0 deg, got %s", tostring(savedData["vrSettings.snapTurnAngle"])))

    _G.loadXMLFile = function(tag, path) return 888 end
    _G.getXMLFloat = function(xmlFile, key)
        if key == "vrSettings.snapTurnAngle" then return 45.0 end
        return nil
    end
    _G.getXMLBool = function(xmlFile, key) return nil end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45.0)) < 0.0001,
        string.format("Must load 45 deg (%.4f rad) from settings.xml, got %.4f", math.rad(45.0), VRCompanion:getSnapTurnAngle()))

    _G.getXMLFloat = function(xmlFile, key)
        if key == "modDesc.vrSettings.snapTurnAngle" then return 30.0 end
        return nil
    end

    VRCompanion:loadConfiguration("mockSettings.xml")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45.0)) < 0.0001,
        "modDesc.vrSettings.snapTurnAngle must not be read as a settings key")

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

    VRCompanion.snapTurnAngle = math.pi * 0.5

    local queryRes = VRCompanion:consoleCommandSetSnapTurnAngle()
    assert(queryRes ~= nil, "Query must return a status string")
    assert(string.find(queryRes, "90") ~= nil, "Query response must show current 90 deg")
    assert(string.find(queryRes, "vrSnapTurnAngle") ~= nil, "Query response must show usage")

    local statusRes = VRCompanion:consoleCommandSetSnapTurnAngle("status")
    assert(string.find(statusRes, "90") ~= nil, "status query must show current 90 deg")

    local set45Res = VRCompanion:consoleCommandSetSnapTurnAngle("45")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(45.0)) < 0.0001,
        string.format("VRCompanion angle must be updated to 45 deg (%.4f rad), got %.4f", math.rad(45.0), VRCompanion:getSnapTurnAngle()))
    assert(string.find(set45Res, "45") ~= nil, "Response must confirm 45 deg")
    assert(savedData["vrSettings.snapTurnAngle"] == 45.0, "Configuration must be saved on change")
    assert(warningShown ~= nil and string.find(warningShown, "45") ~= nil, "Blinking warning must be shown")

    warningShown = nil
    local set30Res = VRCompanion:consoleCommandSetSnapTurnAngle("30")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(30.0)) < 0.0001, "Angle must update to 30 deg")
    assert(savedData["vrSettings.snapTurnAngle"] == 30.0, "Saved config must update to 30 deg")

    local set180Res = VRCompanion:consoleCommandSetSnapTurnAngle("180")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(180.0)) < 0.0001, "Angle must update to 180 deg")
    assert(savedData["vrSettings.snapTurnAngle"] == 180.0, "Saved config must update to 180 deg")

    local set5Res = VRCompanion:consoleCommandSetSnapTurnAngle("5")
    assert(math.abs(VRCompanion:getSnapTurnAngle() - math.rad(5.0)) < 0.0001,
        string.format("Angle 5 deg must be converted to radians (%.4f), got %.4f", math.rad(5.0), VRCompanion:getSnapTurnAngle()))
    assert(savedData["vrSettings.snapTurnAngle"] == 5.0, "Saved config must update to 5 deg")

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

test("ModDescDeclaresSnapTurnActionsBindingsL10nAndSettings", function()
    local modDescContent = fs.readFile("mods/FS25_VRCompanion/modDesc.xml")

    assert(string.find(modDescContent, '<action name="VR_SNAP_TURN_LEFT" category="ONFOOT VEHICLE"/>') ~= nil,
        "modDesc.xml must declare VR_SNAP_TURN_LEFT action under category ONFOOT VEHICLE")
    assert(string.find(modDescContent, '<action name="VR_SNAP_TURN_RIGHT" category="ONFOOT VEHICLE"/>') ~= nil,
        "modDesc.xml must declare VR_SNAP_TURN_RIGHT action under category ONFOOT VEHICLE")

    assert(string.find(modDescContent, '<actionBinding action="VR_SNAP_TURN_LEFT">') ~= nil,
        "modDesc.xml must declare actionBinding for VR_SNAP_TURN_LEFT")
    assert(string.find(modDescContent, '<actionBinding action="VR_SNAP_TURN_RIGHT">') ~= nil,
        "modDesc.xml must declare actionBinding for VR_SNAP_TURN_RIGHT")

    local leftBindingMatch = string.match(modDescContent, '<actionBinding action="VR_SNAP_TURN_LEFT">(.-)</actionBinding>')
    assert(leftBindingMatch ~= nil, "Must extract VR_SNAP_TURN_LEFT binding block")
    assert(string.find(leftBindingMatch, 'input="KEY_numPad4"') ~= nil, "Left binding must map to KEY_numPad4")
    assert(string.find(leftBindingMatch, 'input="BUTTON_5"') ~= nil, "Left binding must map to Gamepad BUTTON_5")

    local rightBindingMatch = string.match(modDescContent, '<actionBinding action="VR_SNAP_TURN_RIGHT">(.-)</actionBinding>')
    assert(rightBindingMatch ~= nil, "Must extract VR_SNAP_TURN_RIGHT binding block")
    assert(string.find(rightBindingMatch, 'input="KEY_numPad6"') ~= nil, "Right binding must map to KEY_numPad6")
    assert(string.find(rightBindingMatch, 'input="BUTTON_6"') ~= nil, "Right binding must map to Gamepad BUTTON_6")

    assert(string.find(modDescContent, '<text name="input_VR_SNAP_TURN_LEFT">') ~= nil, "Must have l10n for input_VR_SNAP_TURN_LEFT")
    assert(string.find(modDescContent, '<text name="input_VR_SNAP_TURN_RIGHT">') ~= nil, "Must have l10n for input_VR_SNAP_TURN_RIGHT")

    assert(string.find(modDescContent, '<text name="vrSnapTurn_angleSet">') ~= nil, "Must have l10n for the snap turn angle notification")

    assert(string.find(modDescContent, "VR%-Blickdrehung links") ~= nil, "Must have German translation for snap turn left")
    assert(string.find(modDescContent, "VR%-Blickdrehung rechts") ~= nil, "Must have German translation for snap turn right")
    assert(string.find(modDescContent, "VR%-Blickdrehungswinkel") ~= nil, "Must have German translation for the snap turn angle notification")

    assert(string.find(modDescContent, "<vrSettings>", 1, true) == nil, "modDesc.xml must not define a <vrSettings> default block")
end)

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

local function orbitedOutsideCamera()
    return {
        isInside = false,
        isRotatable = true,
        cameraNode = 1101,
        rotateNode = 1102,
        origRotX = 0.3, origRotY = 0.0,
        rotX = 0.5, rotY = 2.8,
        zoom = 12.0,
    }
end

test("OutsideCameraSnapTurnStepsOrbitYawBySnapAngle", function()
    VRCompanion:resetSnapTurnDebounce()
    installCountingBridge()
    local orbitCam = orbitedOutsideCamera()
    controlOutsideCamera(orbitCam)

    local ok, newRot = VRCompanion:snapTurnRight(nil, 1000)

    VRCompanion:resetSnapTurnDebounce()
    assert(ok == true and math.abs(newRot - (2.8 + math.pi * 0.5 - 2 * math.pi)) < 0.0001 and newRot == orbitCam.rotY,
        string.format("Orbit yaw must step right by pi/2 and be reported, got ok = %s, rotY = %f, reported %s",
            tostring(ok), orbitCam.rotY, tostring(newRot)))
end)

test("OutsideCameraSnapTurnSignalsOneDiscontinuity", function()
    VRCompanion:resetSnapTurnDebounce()
    local calls = installCountingBridge()
    controlOutsideCamera(orbitedOutsideCamera())

    VRCompanion:snapTurnRight(nil, 1000)

    VRCompanion:resetSnapTurnDebounce()
    assert(calls.discontinuity == 1, string.format("Expected one discontinuity, got %d", calls.discontinuity))
end)

test("OutsideCameraSnapTurnLeavesOrbitPitchAndZoomAlone", function()
    VRCompanion:resetSnapTurnDebounce()
    installCountingBridge()
    local orbitCam = orbitedOutsideCamera()
    controlOutsideCamera(orbitCam)

    VRCompanion:snapTurnRight(nil, 1000)

    VRCompanion:resetSnapTurnDebounce()
    assert(orbitCam.rotX == 0.5 and orbitCam.zoom == 12.0, "Orbit pitch and zoom must not change")
end)

test("OutsideCameraSnapTurnKeepsOrbitYawWithinPlusMinusPi", function()
    VRCompanion:resetSnapTurnDebounce()
    installCountingBridge()
    local orbitCam = orbitedOutsideCamera()
    controlOutsideCamera(orbitCam)

    orbitCam.rotY = -2.8
    VRCompanion:snapTurnLeft(nil, 1000)

    VRCompanion:resetSnapTurnDebounce()
    assert(math.abs(orbitCam.rotY - (-2.8 - math.pi * 0.5 + 2 * math.pi)) < 0.0001,
        string.format("Orbit yaw must be normalized into [-pi, pi], got %f", orbitCam.rotY))
end)

test("OutsideCameraSnapTurnHonorsDebounce", function()
    VRCompanion:resetSnapTurnDebounce()
    local calls = installCountingBridge()

    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1201, origRotX = 0, origRotY = 0, rotX = 0, rotY = 0 }
    controlOutsideCamera(orbitCam)

    assert(VRCompanion:snapTurnRight(nil, 1000) == true, "First snap turn must succeed")
    local ok, reason = VRCompanion:snapTurnRight(nil, 1100)
    assert(ok == false and reason == "debounced", "Snap turn within the cooldown must be debounced")
    assert(math.abs(orbitCam.rotY - (math.pi * 0.5)) < 0.0001, "Debounced snap turn must not move the orbit")
    assert(calls.discontinuity == 1, "Debounced snap turn must not signal a discontinuity")

    VRCompanion:resetSnapTurnDebounce()
end)

test("FixedOutsideCameraSnapTurnIsNotHandled", function()
    VRCompanion:resetSnapTurnDebounce()
    local calls = installCountingBridge()

    local hitchCam = { isInside = false, isRotatable = false, cameraNode = 1301, origRotX = -0.9, origRotY = math.pi, rotX = -0.9, rotY = math.pi }
    controlOutsideCamera(hitchCam)

    local ok, reason = VRCompanion:snapTurnRight(nil, 1000)
    assert(ok == false, "Snap turn must not be handled in a fixed Outside Camera")
    assert(reason ~= nil, "Snap turn must report why it wasn't handled")
    assert(hitchCam.rotY == math.pi and hitchCam.rotX == -0.9, "Fixed Outside Camera must be left unchanged")
    assert(calls.discontinuity == 0, "Unhandled snap turn must not signal a discontinuity")

    VRCompanion:resetSnapTurnDebounce()
end)

test("FixedOutsideCameraSnapTurnStartsNoDebounceCooldown", function()
    VRCompanion:resetSnapTurnDebounce()
    installCountingBridge()

    local hitchCam = { isInside = false, isRotatable = false, cameraNode = 1301, origRotX = -0.9, origRotY = math.pi, rotX = -0.9, rotY = math.pi }
    controlOutsideCamera(hitchCam)
    VRCompanion:snapTurnRight(nil, 1000)

    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1302, origRotX = 0, origRotY = 0, rotX = 0, rotY = 0 }
    controlOutsideCamera(orbitCam)
    local ok = VRCompanion:snapTurnRight(nil, 1050)

    VRCompanion:resetSnapTurnDebounce()
    assert(ok == true, "An unhandled snap turn must not start the debounce cooldown")
end)

test("RecenterInOutsideCameraSwingsOrbitBackToDefaultRecordedOnActivation", function()
    VRCompanion:resetSnapTurnDebounce()
    VRCompanion.recenterVrTrackingSpace = true
    local calls = installCountingBridge()

    local orbitCam = { isInside = false, isRotatable = true, cameraNode = 1401, rotateNode = 1402, rotX = 0.35, rotY = 0.2, zoom = 15.0 }
    local veh = controlOutsideCamera(orbitCam)
    VRCompanion:onVehicleCameraChanged(veh, 2)

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
    local flushesBefore = calls.discontinuity

    VRCompanion:onRecenterAction()

    assert(calls.discontinuity == flushesBefore + 1, "Recenter must signal one discontinuity")
    assert(calls.recenter == 0, "VehicleOnly mode must not recenter the OpenXR reference space")
    assert(math.abs(orbitCam.rotY - 1.0) < 0.0001 and math.abs(orbitCam.rotX - 0.4) < 0.0001,
        "Orbit yaw and pitch must reset to the default in VehicleOnly mode too")
    assert(orbitCam.zoom == 10.0, "Recenter must leave zoom alone")

    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
end)

test("RecenterLeavesAFixedOutsideCameraAimedAsTheGameAimedIt", function()
    VRCompanion:resetSnapTurnDebounce()
    VRCompanion.recenterVrTrackingSpace = true
    installCountingBridge()

    local hitchCam = { isInside = false, isRotatable = false, cameraNode = 1601, origRotX = 0, origRotY = 0, rotX = -0.9, rotY = math.pi }
    local veh = controlOutsideCamera(hitchCam)
    VRCompanion:onVehicleCameraChanged(veh, 3)

    VRCompanion:onRecenterAction()

    assert(hitchCam.rotX == -0.9 and hitchCam.rotY == math.pi,
        string.format("Recenter must not touch a fixed Outside Camera's aim, got rotX = %f, rotY = %f", hitchCam.rotX, hitchCam.rotY))

    VRCompanion.recenterVrTrackingSpace = VRCompanion.DEFAULT_RECENTER_VR_TRACKING_SPACE
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed in test_snap_turn.luau", failed))


