-- ====================================================================
-- Test Suite for Sequential Dual-Pass Pacing & Camera Switching (Issue #56)
-- ====================================================================

-- Mock GIANTS Engine globals before loading VRCompanion
_G.g_currentModName = "FS25_VRCompanion"
_G.g_hudAnchorLeft = 0.0
_G.g_hudAnchorRight = 1.0
_G.g_hudAnchorBottom = 0.0
_G.g_hudAnchorTop = 1.0
_G.currentCamera = 0

function _G.setCamera(node)
    _G.currentCamera = node
end

function _G.getCamera()
    return _G.currentCamera
end

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

print("Running Sequential Dual-Pass Pacing Tests...")

-- Slice 2: Sub-Pass State Tracking & Synchronization
test("SubPassIndexTrackingAndSynchronization", function()
    assert(type(VRCompanion.getSubPassIndex) == "function", "getSubPassIndex must be a function")
    assert(type(VRCompanion.setSubPassIndex) == "function", "setSubPassIndex must be a function")
    assert(type(VRCompanion.advanceSubPass) == "function", "advanceSubPass must be a function")

    -- Initial state
    VRCompanion:setSubPassIndex(0)
    assert(VRCompanion:getSubPassIndex() == 0, "initial getSubPassIndex must be 0")

    -- Set sub-pass 1
    VRCompanion:setSubPassIndex(1)
    assert(VRCompanion:getSubPassIndex() == 1, "getSubPassIndex must be 1 after setSubPassIndex(1)")

    -- Advance sub-pass
    local nextPass = VRCompanion:advanceSubPass()
    assert(nextPass == 0, "advanceSubPass from 1 must return 0")
    assert(VRCompanion:getSubPassIndex() == 0, "subPassIndex must be 0")

    nextPass = VRCompanion:advanceSubPass()
    assert(nextPass == 1, "advanceSubPass from 0 must return 1")
    assert(VRCompanion:getSubPassIndex() == 1, "subPassIndex must be 1")

    -- Test getActiveEyeIndex in Dual-Pass mode
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false

    VRCompanion:setSubPassIndex(0)
    assert(VRCompanion:getActiveEyeIndex(false) == 0, "getActiveEyeIndex in Dual-Pass on subPass 0 must be 0 (Left Eye)")

    VRCompanion:setSubPassIndex(1)
    assert(VRCompanion:getActiveEyeIndex(false) == 1, "getActiveEyeIndex in Dual-Pass on subPass 1 must be 1 (Right Eye)")

    -- Reset to AFR
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
end)

-- Slice 3: Static Eye-Locked HUD Convergence in Dual-Pass Mode
test("HudConvergenceLockedToEyeIndexInDualPass", function()
    local curSubPass = 0
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return 0 end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return curSubPass end,
        setSubPassIndex = function(idx) curSubPass = idx end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
    VRCompanion.currentConvergenceDisparity = 0.012

    -- In DualPass mode: SubPass 0 is Left Eye -> must produce +0.012 offset
    VRCompanion:setSubPassIndex(0)
    local offset0 = VRCompanion:getConvergenceOffset(nil, 0.012)
    assert(math.abs(offset0 - 0.012) < 0.0001, string.format("Sub-Pass 0 must produce positive convergence offset +0.012, got %f", offset0))

    -- In DualPass mode: SubPass 1 is Right Eye -> must produce -0.012 offset
    VRCompanion:setSubPassIndex(1)
    local offset1 = VRCompanion:getConvergenceOffset(nil, 0.012)
    assert(math.abs(offset1 - (-0.012)) < 0.0001, string.format("Sub-Pass 1 must produce negative convergence offset -0.012, got %f", offset1))

    -- Check updateHudConvergence in Dual-Pass mode
    -- SubPass 0: anchors must be shifted by +0.012
    VRCompanion:setSubPassIndex(0)
    VRCompanion:updateHudConvergence(0.016)
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.012) < 0.0001, "updateHudConvergence on SubPass 0 must set currentConvergenceOffset to +0.012")

    -- SubPass 1: anchors must be shifted by -0.012
    VRCompanion:setSubPassIndex(1)
    VRCompanion:updateHudConvergence(0.016)
    assert(math.abs(VRCompanion.currentConvergenceOffset - (-0.012)) < 0.0001, "updateHudConvergence on SubPass 1 must set currentConvergenceOffset to -0.012")

    -- Verify that in DualPass mode, frame parity does NOT override subPassIndex
    -- Even if getFrameParity() were to return 1, SubPass 0 must still produce +0.012
    VRCompanion:setSubPassIndex(0)
    local origGetFrameParity = VRCompanion.getFrameParity
    VRCompanion.getFrameParity = function() return 1 end -- simulate mismatched frame parity
    VRCompanion:updateHudConvergence(0.016)
    assert(math.abs(VRCompanion.currentConvergenceOffset - 0.012) < 0.0001, "In Dual-Pass, subPassIndex 0 must lock to Left Eye (+0.012) regardless of frameParity")

    -- Cleanup
    VRCompanion.getFrameParity = origGetFrameParity
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
    _G.VRBridge = nil
end)

-- Slice 4: Mission update loop hooking lifecycle
test("HooksAndUnhooksMissionUpdate", function()
    local origCalled = 0
    _G.FSBaseMission = {
        update = function(self, dt)
            origCalled = origCalled + 1
        end
    }

    assert(type(VRCompanion.hookMissionUpdate) == "function", "hookMissionUpdate must be a function")
    assert(type(VRCompanion.unhookMissionUpdate) == "function", "unhookMissionUpdate must be a function")

    VRCompanion:hookMissionUpdate()
    assert(VRCompanion.missionUpdateHooked == true, "missionUpdateHooked must be true after hook")

    -- Call update
    _G.FSBaseMission:update(16)
    assert(origCalled == 1, "original mission update must be called")

    -- Unhook
    VRCompanion:unhookMissionUpdate()
    assert(VRCompanion.missionUpdateHooked == false, "missionUpdateHooked must be false after unhook")

    -- Call update after unhook
    _G.FSBaseMission:update(16)
    assert(origCalled == 2, "original mission update must be called directly after unhook")

    _G.FSBaseMission = nil
end)

test("UnhookRestoresMission00UpdateAndAllowsRehookOnNextMapLoad", function()
    local origCalled = 0
    local function origUpdate(self, dt)
        origCalled = origCalled + 1
    end
    _G.Mission00 = { update = origUpdate }

    VRCompanion:hookMissionUpdate()
    assert(_G.Mission00.update ~= origUpdate, "Mission00.update must be wrapped after hook")

    VRCompanion:unhookMissionUpdate()
    assert(_G.Mission00.update == origUpdate, "Mission00.update must be restored after unhook")
    assert(VRCompanion.origMission00Update == nil, "origMission00Update must be cleared after unhook")

    -- Next map load in the same session must hook Mission00 again
    VRCompanion:hookMissionUpdate()
    assert(VRCompanion.missionUpdateHooked == true, "Mission00 must be re-hooked after an unhook")
    _G.Mission00:update(16)
    assert(origCalled == 1, "re-hooked Mission00.update must still call the original")

    VRCompanion:unhookMissionUpdate()
    _G.Mission00 = nil
end)

-- Slice 5: Sub-Pass 0 real dt & cameraNodeLeft, Sub-Pass 1 dt=0 & cameraNodeRight
test("SubPass0ExecutesWithRealDtAndBindsCameraLeft", function()
    local receivedDt = nil
    _G.FSBaseMission = {
        update = function(self, dt)
            receivedDt = dt
        end
    }
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return 0 end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return 0 end,
        setSubPassIndex = function() end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false

    VRCompanion.cameraNodeLeft = 2001
    VRCompanion.cameraNodeRight = 2002
    _G.currentCamera = 0

    VRCompanion:hookMissionUpdate()
    VRCompanion:setSubPassIndex(0)

    _G.FSBaseMission:update(16.666)
    assert(math.abs(receivedDt - 16.666) < 0.001, string.format("Sub-Pass 0 must receive real dt 16.666, got %s", tostring(receivedDt)))
    assert(_G.currentCamera == 2001, string.format("Sub-Pass 0 must bind cameraNodeLeft (2001), got %s", tostring(_G.currentCamera)))
    assert(VRCompanion.activeRenderingCamera == 2001, "activeRenderingCamera must be 2001")

    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
end)

test("SubPass1FreezesSimulationAndBindsCameraRight", function()
    local receivedDt = nil
    _G.FSBaseMission = {
        update = function(self, dt)
            receivedDt = dt
        end
    }
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return 1 end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return 1 end,
        setSubPassIndex = function() end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false

    VRCompanion.cameraNodeLeft = 2001
    VRCompanion.cameraNodeRight = 2002
    _G.currentCamera = 0

    VRCompanion:hookMissionUpdate()
    VRCompanion:setSubPassIndex(1)

    _G.FSBaseMission:update(16.666)
    assert(receivedDt == nil, string.format("Sub-Pass 1 must freeze simulation (FSBaseMission:update not invoked), got %s", tostring(receivedDt)))
    assert(_G.currentCamera == 2002, string.format("Sub-Pass 1 must bind cameraNodeRight (2002), got %s", tostring(_G.currentCamera)))
    assert(VRCompanion.activeRenderingCamera == 2002, "activeRenderingCamera must be 2002")

    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
end)

-- Slice 6: Simulation checks verify identical vehicle and object coordinates across Sub-Pass 0 and Sub-Pass 1
test("SimulationFrozenWithIdenticalVehicleAndObjectCoordinatesAcrossSubPass0And1", function()
    -- Simulated vehicle with kinematics
    local mockVehicle = {
        posX = 100.0,
        posY = 10.0,
        posZ = -50.0,
        rotY = 0.5,
        speedKmh = 36.0, -- 10 m/s
        update = function(self, dt)
            -- kinematic update: dx = v * dtSec
            local dtSec = (dt > 1.0) and (dt * 0.001) or dt
            local speedMs = (self.speedKmh or 0) / 3.6
            self.posX = self.posX + math.sin(self.rotY) * speedMs * dtSec
            self.posZ = self.posZ + math.cos(self.rotY) * speedMs * dtSec
        end
    }

    -- Simulated dynamic world object (e.g. foliage/wind particle)
    local mockWorldObject = {
        windPhase = 1.25,
        update = function(self, dt)
            local dtSec = (dt > 1.0) and (dt * 0.001) or dt
            self.windPhase = self.windPhase + 2.0 * dtSec
        end
    }

    local currentSubPass = 0
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return currentSubPass end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return currentSubPass end,
        setSubPassIndex = function(idx) currentSubPass = idx end
    }

    _G.FSBaseMission = {
        controlledVehicle = mockVehicle,
        update = function(self, dt)
            mockVehicle:update(dt)
            mockWorldObject:update(dt)
        end
    }

    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion.cameraNodeLeft = 3001
    VRCompanion.cameraNodeRight = 3002
    VRCompanion:hookMissionUpdate()

    -- Sub-Pass 0 (Left Eye) with 50ms delta time
    currentSubPass = 0
    VRCompanion:setSubPassIndex(0)
    _G.FSBaseMission:update(50.0)

    -- Capture coordinates after Sub-Pass 0
    local pass0VehicleX = mockVehicle.posX
    local pass0VehicleZ = mockVehicle.posZ
    local pass0WindPhase = mockWorldObject.windPhase
    local pass0Camera = _G.currentCamera

    assert(_G.currentCamera == 3001, "Pass 0 camera must be cameraNodeLeft")
    assert(pass0VehicleX ~= 100.0, "Vehicle must advance on Sub-Pass 0 with real dt")

    -- Sub-Pass 1 (Right Eye) with identical clock input
    currentSubPass = 1
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission:update(50.0)

    -- Capture coordinates after Sub-Pass 1
    local pass1VehicleX = mockVehicle.posX
    local pass1VehicleZ = mockVehicle.posZ
    local pass1WindPhase = mockWorldObject.windPhase
    local pass1Camera = _G.currentCamera

    assert(_G.currentCamera == 3002, "Pass 1 camera must be cameraNodeRight")

    -- VERIFY IDENTICAL VEHICLE AND OBJECT COORDINATES ACROSS SUB-PASS 0 AND 1
    assert(pass0VehicleX == pass1VehicleX, string.format("Vehicle X must be identical across passes: P0=%.6f, P1=%.6f", pass0VehicleX, pass1VehicleX))
    assert(pass0VehicleZ == pass1VehicleZ, string.format("Vehicle Z must be identical across passes: P0=%.6f, P1=%.6f", pass0VehicleZ, pass1VehicleZ))
    assert(pass0WindPhase == pass1WindPhase, string.format("Wind phase must be identical across passes: P0=%.6f, P1=%.6f", pass0WindPhase, pass1WindPhase))

    -- Cleanup
    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
end)

test("AfrAnd2DModesDoNotClampDt", function()
    local receivedDt = nil
    _G.FSBaseMission = {
        update = function(self, dt)
            receivedDt = dt
        end
    }

    VRCompanion:hookMissionUpdate()

    -- AFR mode
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission:update(20.0)
    assert(receivedDt == 20.0, "AFR mode must not clamp dt even if subPassIndex is 1")

    -- 2D Floating Window mode
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion:set2DViewActive(true)
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission:update(25.0)
    assert(receivedDt == 25.0, "2D mode must not clamp dt even in DualPass")

    -- Cleanup
    VRCompanion:set2DViewActive(false)
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
end)

-- Slice 7: Vehicle Speed & Ferry dt=0 Guards
test("VehicleSpeedAndFerryDtZeroGuardsPreventDivideByZero", function()
    local vehSpeedCallCount = 0
    local ferryCallCount = 0

    _G.Vehicle = {
        updateVehicleSpeed = function(self, dt)
            vehSpeedCallCount = vehSpeedCallCount + 1
            if dt == 0 then
                error("Divide by zero error in Vehicle:updateVehicleSpeed")
            end
            return 100 / dt
        end
    }

    _G.Ferry = {
        updatePosition = function(self, dt)
            ferryCallCount = ferryCallCount + 1
            if dt == 0 then
                error("Divide by zero error in Ferry:updatePosition")
            end
            return 50 / dt
        end
    }

    VRCompanion:hookVehicleSpeed()

    -- 1. Calling with dt = 0 must be safely intercepted without error
    local okVeh0 = pcall(function() _G.Vehicle:updateVehicleSpeed(0.0) end)
    assert(okVeh0, "Vehicle:updateVehicleSpeed(0.0) must not throw")
    assert(vehSpeedCallCount == 0, "Original Vehicle:updateVehicleSpeed must not be called when dt is 0")

    -- 2. Calling with tiny dt <= 0.00001 must also be intercepted
    local okVehTiny = pcall(function() _G.Vehicle:updateVehicleSpeed(0.000005) end)
    assert(okVehTiny, "Vehicle:updateVehicleSpeed with epsilon dt must not throw")
    assert(vehSpeedCallCount == 0, "Original Vehicle:updateVehicleSpeed must not be called for epsilon dt")

    -- 3. Calling with dt = nil must be intercepted
    local okVehNil = pcall(function() _G.Vehicle:updateVehicleSpeed(nil) end)
    assert(okVehNil, "Vehicle:updateVehicleSpeed(nil) must not throw")
    assert(vehSpeedCallCount == 0, "Original Vehicle:updateVehicleSpeed must not be called for nil dt")

    -- 4. Normal dt > 0 must pass through to original function
    local speed = _G.Vehicle:updateVehicleSpeed(16.0)
    assert(vehSpeedCallCount == 1, "Original Vehicle:updateVehicleSpeed must be called for valid dt")
    assert(math.abs(speed - (100 / 16.0)) < 0.001, "Original Vehicle:updateVehicleSpeed must return correct speed")

    -- 5. Ferry dt = 0 must be intercepted
    local okFerry0 = pcall(function() _G.Ferry:updatePosition(0.0) end)
    assert(okFerry0, "Ferry:updatePosition(0.0) must not throw")
    assert(ferryCallCount == 0, "Original Ferry:updatePosition must not be called when dt is 0")

    -- 6. Ferry normal dt must pass through
    _G.Ferry:updatePosition(10.0)
    assert(ferryCallCount == 1, "Original Ferry:updatePosition must be called for valid dt")

    -- Cleanup
    VRCompanion:unhookVehicleSpeed()
    assert(VRCompanion.vehicleSpeedHooked == false, "vehicleSpeedHooked must be false after unhook")
    _G.Vehicle = nil
    _G.Ferry = nil
end)

-- Slice 8: Mission Update Reentrancy Guard and Pcall Error Protection
test("MissionUpdateReentrancyGuardAndPcallProtection", function()
    local outerCalled = 0
    local innerCalled = 0

    -- Simulate Mission00 delegating to FSBaseMission
    _G.FSBaseMission = {
        update = function(self, dt)
            innerCalled = innerCalled + 1
        end
    }
    _G.Mission00 = {
        update = function(self, dt)
            outerCalled = outerCalled + 1
            _G.FSBaseMission:update(dt)
        end
    }

    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return 0 end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return 0 end,
        setSubPassIndex = function() end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion:hookMissionUpdate()

    -- Call Mission00:update
    _G.Mission00:update(16.666)
    assert(outerCalled == 1, "Mission00:update must be called once")
    assert(innerCalled == 1, "FSBaseMission:update must be called once via delegation")
    assert(VRCompanion.inMissionUpdate == false, "inMissionUpdate must be false after update completes")

    -- Test pcall protection when inner update throws an error (e.g. unexpected script error on SubPass 1)
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission.update = function(self, dt)
        error("Simulated script exception in third-party mod on dt=0")
    end

    local updateThrew = false
    local ok = pcall(function() _G.Mission00:update(16.666) end)
    assert(ok, "onMissionUpdate must catch internal script exceptions via pcall and not crash the game")
    assert(VRCompanion.inMissionUpdate == false, "inMissionUpdate must be reset even if inner update throws")

    -- Cleanup
    VRCompanion:unhookMissionUpdate()
    _G.Mission00 = nil
    _G.FSBaseMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
end)

-- Slice 9: Camera Manager Integration in bindRenderingCamera
test("CameraManagerIntegrationInBindRenderingCamera", function()
    local camManagerBoundCam = nil
    local rawSetCameraCalled = 0

    local origSetCamera = _G.setCamera
    _G.setCamera = function(node)
        rawSetCameraCalled = rawSetCameraCalled + 1
        origSetCamera(node)
    end

    _G.g_cameraManager = {
        setActiveCamera = function(self, cam)
            camManagerBoundCam = cam
        end
    }

    VRCompanion:bindRenderingCamera(5005)
    assert(camManagerBoundCam == 5005, "g_cameraManager:setActiveCamera must be called with targetCam")
    assert(rawSetCameraCalled == 0, "Raw setCamera must NOT be called when g_cameraManager is available")
    assert(VRCompanion.activeRenderingCamera == 5005, "activeRenderingCamera must be 5005")
    assert(_G.currentCamera == 5005, "_G.currentCamera must be updated to 5005")
    assert(_G.activeCamera == 5005, "_G.activeCamera must be updated to 5005")

    -- When g_cameraManager is absent, must cleanly fallback to setCamera
    _G.g_cameraManager = nil
    VRCompanion:bindRenderingCamera(6006)
    assert(rawSetCameraCalled == 1, "Raw setCamera must be called when g_cameraManager is nil")
    assert(_G.currentCamera == 6006, "_G.currentCamera must be updated to 6006")

    -- Cleanup
    _G.setCamera = origSetCamera
end)

-- Slice 10: Explicit Active Eye Camera Sync for Sub-Pass 1 (Right Eye)
test("SyncActiveEyeCameraPositionsRightEyeOnSubPass1", function()
    local setTransCalls = {}
    local setRotCalls = {}

    local mockVehCam = {
        isInside = true,
        cameraNode = 7001,
        origRotY = 0, origRotX = 0, origRotZ = 0,
        origTransX = 0, origTransY = 1.5, origTransZ = 0
    }

    local mockVehicle = {
        getActiveCamera = function(self)
            return mockVehCam
        end
    }

    _G.g_currentMission = {
        controlledVehicle = mockVehicle
    }

    _G.setTranslation = function(node, x, y, z)
        setTransCalls[node] = { x = x, y = y, z = z }
    end

    _G.setRotation = function(node, rx, ry, rz)
        setRotCalls[node] = { rx = rx, ry = ry, rz = rz }
    end

    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return 0 end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return 1 end,
        setSubPassIndex = function() end,
        getHeadPose = function()
            return { isValid = true, x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0, qx = 0, qy = 0, qz = 0, qw = 1 }
        end
    }

    VRCompanion.cameraNodeLeft = 8001
    VRCompanion.cameraNodeRight = 8002
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion:setSubPassIndex(1)
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false

    -- Call syncActiveEyeCamera on SubPass 1
    VRCompanion:syncActiveEyeCamera(1)

    -- In SubPass 1: cameraNodeRight must be updated with positive eye offset (+halfIpd = +0.032)
    assert(setTransCalls[8002] ~= nil, "cameraNodeRight must receive translation on SubPass 1")
    local rightX = setTransCalls[8002].x
    assert(math.abs(rightX - 0.032) < 0.005, string.format("cameraNodeRight must have +0.032 offset on SubPass 1, got %f", rightX))
    assert(_G.currentCamera == 8002, "Active rendering camera must be bound to cameraNodeRight (8002)")

    -- Cleanup
    _G.setTranslation = nil
    _G.setRotation = nil
    _G.g_currentMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
end)

-- Slice 11: Realtime Cadence Delta-Time Accumulation Across Dual-Pass Pairs
test("SequentialDualPassAccumulatesSubPass1DtForFullRealtimeCadence", function()
    local receivedDts = {}
    _G.FSBaseMission = {
        update = function(self, dt)
            table.insert(receivedDts, dt)
        end
    }

    local curSubPass = 0
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return curSubPass end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return curSubPass end,
        setSubPassIndex = function(idx) curSubPass = idx end
    }

    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
    VRCompanion.cameraNodeLeft = 2001
    VRCompanion.cameraNodeRight = 2002

    VRCompanion:hookMissionUpdate()

    -- Simulate 3 consecutive stereo pairs (6 frames total) with dt = 16.666ms per render frame
    -- Pair 1
    curSubPass = 0
    VRCompanion:setSubPassIndex(0)
    _G.FSBaseMission:update(16.666) -- P0: initial frame receives 16.666

    curSubPass = 1
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission:update(16.666) -- P1: clamped to 0, accumulates 16.666

    -- Pair 2
    curSubPass = 0
    VRCompanion:setSubPassIndex(0)
    _G.FSBaseMission:update(16.666) -- P0: receives 16.666 + 16.666 = 33.332

    curSubPass = 1
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission:update(16.666) -- P1: clamped to 0, accumulates 16.666

    -- Pair 3
    curSubPass = 0
    VRCompanion:setSubPassIndex(0)
    _G.FSBaseMission:update(16.666) -- P0: receives 16.666 + 16.666 = 33.332

    curSubPass = 1
    VRCompanion:setSubPassIndex(1)
    _G.FSBaseMission:update(16.666) -- P1: simulation frozen, accumulates 16.666

    assert(#receivedDts == 3, string.format("Expected 3 simulation update calls (SubPass 0 only), got %d", #receivedDts))
    assert(math.abs(receivedDts[1] - 16.666) < 0.001, string.format("Frame 0 must receive initial 16.666, got %f", receivedDts[1]))
    assert(math.abs(receivedDts[2] - 33.332) < 0.001, string.format("Frame 2 (SubPass 0) must receive accumulated 33.332, got %f", receivedDts[2]))
    assert(math.abs(receivedDts[3] - 33.332) < 0.001, string.format("Frame 4 (SubPass 0) must receive accumulated 33.332, got %f", receivedDts[3]))

    -- Verify total elapsed simulation time matches total clock time between frame 0 and frame 5
    local totalSimDt = 0
    for _, d in ipairs(receivedDts) do
        totalSimDt = totalSimDt + d
    end
    -- 5 intervals of 16.666ms elapsed between frame 0 and frame 5 = 83.330ms
    assert(math.abs(totalSimDt - (16.666 * 5)) < 0.005, string.format("Total simulation time (%.3f) must match clock time (%.3f)", totalSimDt, 16.666 * 5))

    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
end)

-- Slice 12: RailroadVehicle & Subclass Speed Guards
test("RailroadVehicleAndSubclassSpeedGuardsPreventDivideByZero", function()
    local rrCallCount = 0
    local locoCallCount = 0

    _G.RailroadVehicle = {
        updateVehicleSpeed = function(self, dt)
            rrCallCount = rrCallCount + 1
            if dt == 0 then
                error("Divide by zero error in RailroadVehicle:updateVehicleSpeed")
            end
            return 120 / dt
        end
    }

    _G.Locomotive = {
        updateVehicleSpeed = function(self, dt)
            locoCallCount = locoCallCount + 1
            if dt == 0 then
                error("Divide by zero error in Locomotive:updateVehicleSpeed")
            end
            return 150 / dt
        end
    }

    VRCompanion:hookVehicleSpeed()

    -- 1. RailroadVehicle: dt = 0 must be safely intercepted
    local okRR0 = pcall(function() _G.RailroadVehicle:updateVehicleSpeed(0.0) end)
    assert(okRR0, "RailroadVehicle:updateVehicleSpeed(0.0) must not throw")
    assert(rrCallCount == 0, "Original RailroadVehicle:updateVehicleSpeed must not be called when dt is 0")

    -- 2. Locomotive: dt = 0 must be safely intercepted
    local okLoco0 = pcall(function() _G.Locomotive:updateVehicleSpeed(0.0) end)
    assert(okLoco0, "Locomotive:updateVehicleSpeed(0.0) must not throw")
    assert(locoCallCount == 0, "Original Locomotive:updateVehicleSpeed must not be called when dt is 0")

    -- 3. Normal dt > 0 must pass through to original functions
    local rrSpeed = _G.RailroadVehicle:updateVehicleSpeed(16.0)
    assert(rrCallCount == 1, "Original RailroadVehicle:updateVehicleSpeed must be called for valid dt")
    assert(math.abs(rrSpeed - (120 / 16.0)) < 0.001, "RailroadVehicle must return correct speed")

    local locoSpeed = _G.Locomotive:updateVehicleSpeed(15.0)
    assert(locoCallCount == 1, "Original Locomotive:updateVehicleSpeed must be called for valid dt")
    assert(math.abs(locoSpeed - (150 / 15.0)) < 0.001, "Locomotive must return correct speed")

    -- Cleanup
    VRCompanion:unhookVehicleSpeed()
    assert(VRCompanion.vehicleSpeedHooked == false, "vehicleSpeedHooked must be false after unhook")
    _G.RailroadVehicle = nil
    _G.Locomotive = nil
end)

-- Slice 13: Base Camera Stereoscopic Translation in Dual-Pass and AFR Modes
test("BaseCameraTranslatesStereoscopicallyInDualPassMode", function()
    local setTransCalls = {}

    local mockVehCam = {
        isInside = true,
        cameraNode = 9001,
        origRotY = 0, origRotX = 0, origRotZ = 0,
        origTransX = 0, origTransY = 1.5, origTransZ = 0
    }

    _G.setTranslation = function(node, x, y, z)
        setTransCalls[node] = { x = x, y = y, z = z }
    end
    _G.setRotation = function(node, rx, ry, rz) end

    local curSubPass = 0
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return curSubPass end,
        getRenderingMode = function() return "SequentialDualPass" end,
        getSubPassIndex = function() return curSubPass end,
        setSubPassIndex = function(idx) curSubPass = idx end,
        getHeadPose = function()
            return { isValid = true, x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0, qx = 0, qy = 0, qz = 0, qw = 1 }
        end
    }

    VRCompanion.cameraNodeLeft = 9002
    VRCompanion.cameraNodeRight = 9003
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false

    -- === 1. Sequential Dual-Pass Mode ===
    VRCompanion:setRenderingMode("DualPass")

    -- SubPass 0 (Left Eye)
    curSubPass = 0
    VRCompanion:setSubPassIndex(0)
    VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)

    assert(setTransCalls[9002] ~= nil, "cameraNodeLeft must receive translation on SubPass 0")
    assert(math.abs(setTransCalls[9002].x - (-0.032)) < 0.005, "cameraNodeLeft must have -0.032 offset")
    -- CRITICAL: mockVehCam.cameraNode (base camera) must align with Left Eye (-0.032) to prevent cross-eyed distortion
    assert(setTransCalls[9001] ~= nil, "mockVehCam.cameraNode must receive translation")
    assert(math.abs(setTransCalls[9001].x - (-0.032)) < 0.005, string.format("In DualPass SubPass 0, base cameraNode must align with Left Eye (-0.032), got %f", setTransCalls[9001].x))

    -- SubPass 1 (Right Eye)
    curSubPass = 1
    VRCompanion:setSubPassIndex(1)
    VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)

    assert(setTransCalls[9003] ~= nil, "cameraNodeRight must receive translation on SubPass 1")
    assert(math.abs(setTransCalls[9003].x - 0.032) < 0.005, "cameraNodeRight must have +0.032 offset")
    -- CRITICAL: mockVehCam.cameraNode (base camera) must align with Right Eye (+0.032) to prevent cross-eyed distortion
    assert(math.abs(setTransCalls[9001].x - 0.032) < 0.005, string.format("In DualPass SubPass 1, base cameraNode must align with Right Eye (+0.032), got %f", setTransCalls[9001].x))

    -- === 2. AFR Mode (Alternating Eye on Base Camera) ===
    VRCompanion:setRenderingMode("AFR")
    _G.VRBridge.getRenderingMode = function() return "AFR" end

    -- Frame 0 (even, left eye, parity = 0)
    curSubPass = 0
    VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
    assert(math.abs(setTransCalls[9001].x - (-0.032)) < 0.005, "In AFR Frame 0, base cameraNode must receive left eye offset (-0.032)")

    -- Frame 1 (odd, right eye, parity = 1)
    curSubPass = 1
    VRCompanion:onVehicleCameraUpdate(mockVehCam, 0.016)
    assert(math.abs(setTransCalls[9001].x - 0.032) < 0.005, "In AFR Frame 1, base cameraNode must receive right eye offset (+0.032)")

    -- Cleanup
    _G.setTranslation = nil
    _G.setRotation = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
end)

-- Slice 14: One eye projection fetch and one rendering camera bind per camera update / sub-pass
local function installProjectionCountingBridge(state)
    state.projectionFetches = 0
    state.binds = {}
    _G.setTranslation = function() end
    _G.setRotation = function() end
    _G.g_cameraManager = {
        setActiveCamera = function(self, cam)
            table.insert(state.binds, cam)
        end
    }
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getIpd = function() return 0.064 end,
        getFrameParity = function() return state.parity or 0 end,
        getRenderingMode = function() return state.mode end,
        getSubPassIndex = function() return state.subPass or 0 end,
        setSubPassIndex = function(idx) state.subPass = idx end,
        getEyeProjection = function(eye)
            state.projectionFetches = state.projectionFetches + 1
            return { isValid = true, fovY = 1.0, xOffset = 0, yOffset = 0 }
        end,
        getHeadPose = function()
            return { isValid = true, x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0, qx = 0, qy = 0, qz = 0, qw = 1 }
        end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
end

local function cleanupProjectionCountingBridge()
    _G.setTranslation = nil
    _G.setRotation = nil
    _G.g_cameraManager = nil
    _G.g_currentMission = nil
    _G.VRBridge = nil
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
end

test("VehicleCameraUpdateFetchesEyeProjectionOnceAndBindsOnce", function()
    local vehCam = { isInside = true, cameraNode = 9101, origRotY = 0, origRotX = 0, origRotZ = 0 }
    for _, mode in ipairs({ "AFR", "DualPass" }) do
        for eye = 0, 1 do
            local state = { mode = (mode == "AFR") and "AFR" or "SequentialDualPass", parity = eye, subPass = eye }
            installProjectionCountingBridge(state)
            VRCompanion.cameraNodeLeft = 9102
            VRCompanion.cameraNodeRight = 9103
            VRCompanion:setRenderingMode(mode)
            VRCompanion:setSubPassIndex(eye)

            VRCompanion:onVehicleCameraUpdate(vehCam, 0.016)

            assert(state.projectionFetches == 1, string.format("%s eye %d: expected 1 eye projection fetch, got %d", mode, eye, state.projectionFetches))
            assert(#state.binds == 1, string.format("%s eye %d: expected 1 camera bind, got %d", mode, eye, #state.binds))
            local expected = (eye == 1) and 9103 or 9102
            assert(state.binds[1] == expected, string.format("%s eye %d: expected bind of %d, got %s", mode, eye, expected, tostring(state.binds[1])))
            cleanupProjectionCountingBridge()
        end
    end
end)

test("PlayerCameraUpdateFetchesEyeProjectionOnceAndBindsOnce", function()
    local playerCam = { firstPersonCamera = 9201 }
    for eye = 0, 1 do
        local state = { mode = "AFR", parity = eye }
        installProjectionCountingBridge(state)
        VRCompanion.cameraNodeLeft = 9202
        VRCompanion.cameraNodeRight = 9203
        VRCompanion:setRenderingMode("AFR")

        VRCompanion:onPlayerCameraUpdate(playerCam, 0.016)

        assert(state.projectionFetches == 1, string.format("eye %d: expected 1 eye projection fetch, got %d", eye, state.projectionFetches))
        assert(#state.binds == 1, string.format("eye %d: expected 1 camera bind, got %d", eye, #state.binds))
        local expected = (eye == 1) and 9203 or 9202
        assert(state.binds[1] == expected, string.format("eye %d: expected bind of %d, got %s", eye, expected, tostring(state.binds[1])))
        cleanupProjectionCountingBridge()
    end
end)

test("SequentialDualPassSubPassFetchesEyeProjectionOnceAndBindsOnce", function()
    local vehCam = { isInside = true, cameraNode = 9301, origRotY = 0, origRotX = 0, origRotZ = 0 }
    local state = { mode = "SequentialDualPass", subPass = 0 }
    installProjectionCountingBridge(state)
    _G.g_currentMission = { controlledVehicle = { getActiveCamera = function() return vehCam end } }
    local updates = 0
    _G.FSBaseMission = {
        update = function(self, dt)
            updates = updates + 1
            -- the game's own camera hooks fire during the mission update on sub-pass 0
            VRCompanion:onVehicleCameraUpdate(vehCam, dt)
        end
    }
    VRCompanion.cameraNodeLeft = 9302
    VRCompanion.cameraNodeRight = 9303
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion:hookMissionUpdate()

    for subPass = 0, 1 do
        state.subPass = subPass
        VRCompanion:setSubPassIndex(subPass)
        state.projectionFetches = 0
        state.binds = {}

        _G.FSBaseMission:update(16.666)

        assert(state.projectionFetches == 1, string.format("sub-pass %d: expected 1 eye projection fetch, got %d", subPass, state.projectionFetches))
        assert(#state.binds == 1, string.format("sub-pass %d: expected 1 camera bind, got %d", subPass, #state.binds))
        local expected = (subPass == 1) and 9303 or 9302
        assert(state.binds[1] == expected, string.format("sub-pass %d: expected bind of %d, got %s", subPass, expected, tostring(state.binds[1])))
    end
    assert(updates == 1, "mission update must run only on sub-pass 0")

    -- A projection cached for one sub-pass must not leak into the next camera update
    state.projectionFetches = 0
    VRCompanion:onVehicleCameraUpdate(vehCam, 0.016)
    assert(state.projectionFetches == 1, "a camera update outside a sub-pass must fetch a fresh projection")

    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
    cleanupProjectionCountingBridge()
end)

test("MenuBackgroundCameraReusesOffsetRecordAndRestoresExactOffset", function()
    local camNode = 9401
    local pos = { x = 10, y = 20, z = 30 }
    local rot = { x = 0.1, y = 0.2, z = 0.3 }
    local pose = { isValid = true, x = 1, y = 2, z = 3, pitch = 0.01, yaw = 0.02, roll = 0.03 }

    local origGetCamera = _G.getCamera
    _G.getCamera = function() return camNode end
    _G.getTranslation = function() return pos.x, pos.y, pos.z end
    _G.getRotation = function() return rot.x, rot.y, rot.z end
    _G.setTranslation = function(_, x, y, z) pos.x, pos.y, pos.z = x, y, z end
    _G.setRotation = function(_, x, y, z) rot.x, rot.y, rot.z = x, y, z end
    _G.VRBridge = {
        isReady = function() return true end,
        isCompanionActive = true,
        getHeadPose = function() return pose end
    }
    VRCompanion.handshakeComplete = true
    VRCompanion.headTrackingEnabled = true
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = true
    VRCompanion.menuCameraOffsets = {}

    VRCompanion:updateMenuBackgroundCamera(0.016)
    local record = VRCompanion.menuCameraOffsets[camNode]
    assert(record ~= nil, "an offset record must exist after the first menu camera update")
    assert(pos.x == 11 and pos.y == 22 and pos.z == 33, "first update must apply the head pose translation")

    -- Head moves; the same record is updated in place and the applied offset is replaced, not accumulated
    pose.x, pose.y, pose.z = 4, 5, 6
    pose.pitch, pose.yaw, pose.roll = 0.04, 0.05, 0.06
    VRCompanion:updateMenuBackgroundCamera(0.016)
    VRCompanion:updateMenuBackgroundCamera(0.016)
    assert(rawequal(VRCompanion.menuCameraOffsets[camNode], record), "menu camera offset record must be reused across frames")
    assert(pos.x == 14 and pos.y == 25 and pos.z == 36, "translation must be base + latest pose only")
    assert(math.abs(rot.x - 0.14) < 1e-9 and math.abs(rot.y - 0.25) < 1e-9 and math.abs(rot.z - 0.36) < 1e-9, "rotation must be base + latest pose only")
    assert(record.tx == 4 and record.ty == 5 and record.tz == 6 and record.rx == 0.04 and record.ry == 0.05 and record.rz == 0.06, "record must hold exactly the applied offset")

    -- Restoring on menu close removes exactly the applied offset
    VRCompanion:restoreMenuBackgroundCamera()
    assert(pos.x == 10 and pos.y == 20 and pos.z == 30, "restore must return the base translation")
    assert(math.abs(rot.x - 0.1) < 1e-9 and math.abs(rot.y - 0.2) < 1e-9 and math.abs(rot.z - 0.3) < 1e-9, "restore must return the base rotation")
    assert(next(VRCompanion.menuCameraOffsets) == nil, "offset records must be cleared after restore")

    _G.getCamera = origGetCamera
    _G.getTranslation = nil
    _G.getRotation = nil
    _G.setTranslation = nil
    _G.setRotation = nil
    _G.VRBridge = nil
    VRCompanion.isMenuVisible = false
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed", failed))
