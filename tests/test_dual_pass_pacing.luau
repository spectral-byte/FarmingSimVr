-- ====================================================================
-- Test Suite for Sequential Dual-Pass Pacing & Camera Switching
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

-- Slice 9: bindRenderingCamera renders through the VR camera via the game's camera manager
test("BindingTheVrCameraMakesTheCameraManagerRenderThroughIt", function()
    -- As in the game: the camera manager renders only through registered cameras, and the global
    -- setCamera is its deprecation wrapper and binds nothing
    local rendered = nil
    local registered = {}
    local origSetCamera = _G.setCamera
    _G.setCamera = function(node) end
    _G.g_cameraManager = {
        addCamera = function(self, cam) registered[cam] = true end,
        setActiveCamera = function(self, cam) if registered[cam] then rendered = cam end end,
    }
    VRCompanion.cameraNodeLeft, VRCompanion.cameraNodeRight = 5005, 5005

    VRCompanion:bindRenderingCamera(5005)

    _G.setCamera = origSetCamera
    _G.g_cameraManager = nil
    VRCompanion.cameraNodeLeft, VRCompanion.cameraNodeRight = nil, nil
    VRCompanion.registeredEyeCameras = nil
    assert(rendered == 5005, "the engine must render through the VR camera, got " .. tostring(rendered))
end)

test("WithoutACameraManagerTheGlobalSetCameraBindsTheCamera", function()
    local rendered = nil
    local origSetCamera = _G.setCamera
    _G.setCamera = function(node) rendered = node end
    _G.g_cameraManager = nil

    VRCompanion:bindRenderingCamera(6006)

    _G.setCamera = origSetCamera
    assert(rendered == 6006, "without a camera manager, setCamera must bind the camera, got " .. tostring(rendered))
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

-- Slice 12b: Per-frame update must not rescan _G / mission vehicles
test("UpdateDoesNotScanGlobalTableEveryFrameWithoutFerry", function()
    _G.Vehicle = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.Ferry = nil
    VRCompanion:unhookVehicleSpeed()
    _G.g_currentMission = nil

    local realPairs = pairs
    local globalScans = 0
    _G.pairs = function(t, ...)
        if t == _G then globalScans = globalScans + 1 end
        return realPairs(t, ...)
    end

    local ok, err = pcall(function()
        for _ = 1, 300 do VRCompanion:update(16.6) end
    end)
    _G.pairs = realPairs
    assert(ok, tostring(err))

    assert(globalScans >= 1, "guards must still be installed by update")
    assert(globalScans <= 5, "update must not scan _G every frame, scans=" .. tostring(globalScans))

    -- Guard still swallows dt=0
    assert(pcall(function() _G.Vehicle:updateVehicleSpeed(0.0) end))

    VRCompanion:unhookVehicleSpeed()
    _G.Vehicle = nil
end)

test("LateFerryAndLateVehicleAreGuardedWithinBoundedUpdates", function()
    local ferryCalls = 0
    local vehCalls = 0
    _G.Vehicle = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.Ferry = nil
    VRCompanion:unhookVehicleSpeed()
    _G.g_currentMission = { vehicles = {} }
    for _ = 1, 3 do VRCompanion:update(16.6) end

    -- Ferry class appears after loadMap
    local origFerry = function(self, dt)
        ferryCalls = ferryCalls + 1
        if dt == 0 then error("div by zero") end
    end
    _G.Ferry = { updatePosition = origFerry }
    for _ = 1, 600 do VRCompanion:update(16.6) end
    assert(_G.Ferry.updatePosition ~= origFerry, "late Ferry must get guarded within bounded updates")
    assert(pcall(function() _G.Ferry:updatePosition(0.0) end), "Ferry dt=0 must be swallowed")
    assert(ferryCalls == 0)

    -- A vehicle instance appearing later
    local veh = { updateVehicleSpeed = function(self, dt) vehCalls = vehCalls + 1 end }
    local origVeh = veh.updateVehicleSpeed
    _G.g_currentMission.vehicles[1] = veh
    for _ = 1, 600 do VRCompanion:update(16.6) end
    assert(veh.updateVehicleSpeed ~= origVeh, "late vehicle must get guarded within bounded updates")
    veh:updateVehicleSpeed(0.0)
    assert(vehCalls == 0)

    -- deleteMap-equivalent restore
    VRCompanion:unhookVehicleSpeed()
    assert(_G.Ferry.updatePosition == origFerry, "Ferry original must be restored")
    assert(veh.updateVehicleSpeed == origVeh, "vehicle original must be restored")
    _G.Vehicle = nil
    _G.Ferry = nil
    _G.g_currentMission = nil
end)

test("LateFerryIsGuardedWithinBoundedUpdatesAfterMapReload", function()
    _G.Vehicle = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.Ferry = nil
    VRCompanion:unhookVehicleSpeed()
    _G.g_currentMission = { vehicles = {} }

    -- A long first session, then back to the menu and into another save.
    VRCompanion.frameTicks = 100000
    VRCompanion:update(16.6)
    VRCompanion:deleteMap()
    _G.g_currentMission = { vehicles = {} }
    VRCompanion:hookVehicleSpeed() -- what loadMap does

    local origFerry = function(self, dt)
        if dt == 0 then error("div by zero") end
    end
    _G.Ferry = { updatePosition = origFerry }
    for _ = 1, 600 do VRCompanion:update(16.6) end
    assert(_G.Ferry.updatePosition ~= origFerry, "late Ferry must get guarded within bounded updates after a reload")

    VRCompanion:unhookVehicleSpeed()
    _G.Vehicle = nil
    _G.Ferry = nil
    _G.g_currentMission = nil
end)

test("EnteringANewVehicleGuardsItWithoutWaitingForTheRetry", function()
    _G.Vehicle = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.Ferry = nil
    VRCompanion:unhookVehicleSpeed()
    _G.g_currentMission = { vehicles = {} }
    for _ = 1, 3 do VRCompanion:update(16.6) end

    local vehCalls = 0
    local veh = { updateVehicleSpeed = function(self, dt) vehCalls = vehCalls + 1 end }
    local origVeh = veh.updateVehicleSpeed
    _G.g_currentMission.vehicles[1] = veh
    _G.g_currentMission.controlledVehicle = veh
    VRCompanion:update(16.6)
    assert(veh.updateVehicleSpeed ~= origVeh, "an entered vehicle must be guarded on the transition update")
    veh:updateVehicleSpeed(0.0)
    assert(vehCalls == 0)

    VRCompanion:unhookVehicleSpeed()
    VRCompanion.lastControlledVehicle = nil
    _G.Vehicle = nil
    _G.g_currentMission = nil
end)

test("AGlobalClassAddedAfterLoadIsNotHookedByTheRetryOrATransition", function()
    _G.Vehicle = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.Ferry = nil
    VRCompanion:unhookVehicleSpeed()
    _G.g_currentMission = { vehicles = {} }
    for _ = 1, 3 do VRCompanion:update(16.6) end

    local lateClassFn = function(self, dt) return 1 end
    _G.LateVehicleClass = { updateVehicleSpeed = lateClassFn }
    for _ = 1, 600 do VRCompanion:update(16.6) end
    assert(_G.LateVehicleClass.updateVehicleSpeed == lateClassFn, "the retry cadence must not scan globals")

    local veh = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.g_currentMission.vehicles[1] = veh
    _G.g_currentMission.controlledVehicle = veh
    VRCompanion:update(16.6)
    assert(_G.LateVehicleClass.updateVehicleSpeed == lateClassFn, "a vehicle transition must not scan globals")

    VRCompanion:unhookVehicleSpeed()
    VRCompanion.lastControlledVehicle = nil
    _G.LateVehicleClass = nil
    _G.Vehicle = nil
    _G.g_currentMission = nil
end)

test("TheLoadTimeScanStillGuardsAnyGlobalClassWithUpdateVehicleSpeed", function()
    local calls = 0
    _G.CustomVehicleClass = { updateVehicleSpeed = function(self, dt) calls = calls + 1 end }
    local orig = _G.CustomVehicleClass.updateVehicleSpeed
    VRCompanion:unhookVehicleSpeed()
    VRCompanion:hookVehicleSpeed()
    assert(_G.CustomVehicleClass.updateVehicleSpeed ~= orig, "the map load scan must guard custom classes")
    _G.CustomVehicleClass:updateVehicleSpeed(0.0)
    assert(calls == 0)
    VRCompanion:unhookVehicleSpeed()
    assert(_G.CustomVehicleClass.updateVehicleSpeed == orig, "unload must restore the custom class")
    _G.CustomVehicleClass = nil
end)

test("AVehicleDroppedFromTheMissionIsCollected", function()
    _G.Vehicle = { updateVehicleSpeed = function(self, dt) return 1 end }
    _G.Ferry = nil
    VRCompanion:unhookVehicleSpeed()
    local probe = setmetatable({}, { __mode = "k" })
    _G.g_currentMission = { vehicles = {} }
    local function addVehicle()
        local veh = { updateVehicleSpeed = function(self, dt) return 1 end }
        probe[veh] = true
        _G.g_currentMission.vehicles[1] = veh
    end
    addVehicle()
    for _ = 1, 600 do VRCompanion:update(16.6) end

    _G.g_currentMission.vehicles[1] = nil
    collectgarbage("collect")
    collectgarbage("collect")
    assert(next(probe) == nil, "a vehicle that is gone from the mission must be collectable")

    VRCompanion:unhookVehicleSpeed()
    _G.Vehicle = nil
    _G.g_currentMission = nil
end)

test("UnloadRestoresTheClassesAndTheVehiclesStillAlive", function()
    local vehicleClassFn = function(self, dt) return 1 end
    local ferryFn = function(self, dt) end
    local vehFn = function(self, dt) end
    _G.Vehicle = { updateVehicleSpeed = vehicleClassFn }
    _G.Ferry = { updatePosition = ferryFn }
    local veh = { updateVehicleSpeed = vehFn }
    _G.g_currentMission = { vehicles = { veh } }
    VRCompanion:unhookVehicleSpeed()
    VRCompanion:hookVehicleSpeed()
    assert(_G.Vehicle.updateVehicleSpeed ~= vehicleClassFn and veh.updateVehicleSpeed ~= vehFn
        and _G.Ferry.updatePosition ~= ferryFn, "everything must be guarded first")

    VRCompanion:unhookVehicleSpeed()
    assert(_G.Vehicle.updateVehicleSpeed == vehicleClassFn, "Vehicle class must be restored")
    assert(_G.Ferry.updatePosition == ferryFn, "Ferry must be restored")
    assert(veh.updateVehicleSpeed == vehFn, "vehicle must be restored")

    _G.Vehicle = nil
    _G.Ferry = nil
    _G.g_currentMission = nil
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

-- Under AFR each frame renders one eye's camera; the Companion Mod reports it to the Injector like a
-- Sequential Dual-Pass sub-pass does, so the motion vector correction has the cameras it needs.
local function installCameraReportMocks(state)
    state.reports = {}
    _G.VRBridge.reportCameraRecord = function(eye, x, y, z, qx, qy, qz, qw, fovY, nearClip, farClip, offX, offY)
        table.insert(state.reports, { eye = eye, x = x, qw = qw, fovY = fovY, nearClip = nearClip, farClip = farClip })
        return true
    end
    _G.getWorldTranslation = function(node) return node, 1.5, -2.5 end
    _G.getWorldQuaternion = function(node) return 0, 0, 0, 1 end
    _G.getFovY = function(node) return 1.1 end
    _G.getNearClip = function(node) return 0.15 end
    _G.getFarClip = function(node) return 2000 end
    _G.getProjectionOffset = function(node) return 0, 0 end
end

local function removeCameraReportMocks()
    _G.getWorldTranslation = nil
    _G.getWorldQuaternion = nil
    _G.getFovY = nil
    _G.getNearClip = nil
    _G.getFarClip = nil
    _G.getProjectionOffset = nil
end

test("AfrPlayerCameraUpdateReportsTheBoundEyeCameraKeyedByTheFrameParity", function()
    local playerCam = { firstPersonCamera = 9601 }
    for eye = 0, 1 do
        local state = { mode = "AFR", parity = eye }
        installProjectionCountingBridge(state)
        installCameraReportMocks(state)
        VRCompanion.cameraNodeLeft = 9602
        VRCompanion.cameraNodeRight = 9603
        VRCompanion:setRenderingMode("AFR")

        VRCompanion:onPlayerCameraUpdate(playerCam, 0.016)

        local expectedNode = (eye == 1) and 9603 or 9602
        assert(#state.reports == 1, string.format("eye %d: expected 1 camera report, got %d", eye, #state.reports))
        assert(state.reports[1].eye == eye, "the record must be keyed by the eye the frame rendered")
        assert(state.reports[1].x == expectedNode, "the record must come from the bound eye camera node")
        assert(math.abs(state.reports[1].fovY - 1.1) < 1e-9 and state.reports[1].farClip == 2000, "projection must be reported")
        removeCameraReportMocks()
        cleanupProjectionCountingBridge()
    end
end)

test("AfrVehicleCameraUpdateReportsTheBoundEyeCameraKeyedByTheFrameParity", function()
    local vehCam = { isInside = true, cameraNode = 9701, origRotY = 0, origRotX = 0, origRotZ = 0 }
    for eye = 0, 1 do
        local state = { mode = "AFR", parity = eye }
        installProjectionCountingBridge(state)
        installCameraReportMocks(state)
        VRCompanion.cameraNodeLeft = 9702
        VRCompanion.cameraNodeRight = 9703
        VRCompanion:setRenderingMode("AFR")

        VRCompanion:onVehicleCameraUpdate(vehCam, 0.016)

        assert(#state.reports == 1, string.format("eye %d: expected 1 camera report, got %d", eye, #state.reports))
        assert(state.reports[1].eye == eye, "the record must be keyed by the eye the frame rendered")
        assert(state.reports[1].x == ((eye == 1) and 9703 or 9702), "the record must come from the bound eye camera node")
        removeCameraReportMocks()
        cleanupProjectionCountingBridge()
    end
end)

test("AfrCameraReportNeverBreaksTheCameraUpdateWhenEngineQueriesFail", function()
    local playerCam = { firstPersonCamera = 9801 }
    local state = { mode = "AFR", parity = 0 }
    installProjectionCountingBridge(state)
    installCameraReportMocks(state)
    _G.getWorldQuaternion = function() error("engine query failed") end
    VRCompanion.cameraNodeLeft = 9802
    VRCompanion.cameraNodeRight = 9803
    VRCompanion:setRenderingMode("AFR")

    local ok, err = pcall(function() VRCompanion:onPlayerCameraUpdate(playerCam, 0.016) end)
    local binds = #state.binds
    removeCameraReportMocks()
    cleanupProjectionCountingBridge()
    assert(ok, tostring(err))
    assert(binds == 1, "the camera must still be bound")
end)

test("SequentialDualPassCameraUpdatesDoNotReportThroughTheAfrPath", function()
    -- Sub-passes report once, from the mission update; the camera updates inside them must not add a second record.
    local vehCam = { isInside = true, cameraNode = 9901, origRotY = 0, origRotX = 0, origRotZ = 0 }
    local state = { mode = "SequentialDualPass", subPass = 0 }
    installProjectionCountingBridge(state)
    installCameraReportMocks(state)
    VRCompanion.cameraNodeLeft = 9902
    VRCompanion.cameraNodeRight = 9903
    VRCompanion:setRenderingMode("DualPass")

    VRCompanion:onVehicleCameraUpdate(vehCam, 0.016)

    local reports = #state.reports
    removeCameraReportMocks()
    cleanupProjectionCountingBridge()
    assert(reports == 0, "expected no AFR-path report under Sequential Dual-Pass, got " .. reports)
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

test("NonStereoCameraUpdateAppliesTheLeftEyeProjectionToTheLeftCamera", function()
    local vehCam = { isInside = true, cameraNode = 9501, origRotY = 0, origRotX = 0, origRotZ = 0 }
    local state = { mode = "SequentialDualPass", subPass = 1 }
    installProjectionCountingBridge(state)
    _G.VRBridge.requiresAfrFallback = false
    _G.VRBridge.getEyeProjection = function(eye)
        state.projectionFetches = state.projectionFetches + 1
        return { isValid = true, fovY = (eye == 1) and 2.0 or 1.0, xOffset = 0, yOffset = 0 }
    end
    local fovByNode = {}
    _G.setFovY = function(node, fov) fovByNode[node] = fov end
    VRCompanion.cameraNodeLeft = 9502
    VRCompanion.cameraNodeRight = 9503
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion:setSubPassIndex(1)

    local ok, err = pcall(function()
        VRCompanion:onVehicleCameraUpdate(vehCam, 0.016)
    end)
    _G.setFovY = nil
    cleanupProjectionCountingBridge()
    assert(ok, tostring(err))
    assert(fovByNode[9502] == 1.0, "the left camera must get the left eye's projection, got fovY=" .. tostring(fovByNode[9502]))
end)

test("SubPassStillBindsItsEyeCameraWhenTheCameraSyncThrows", function()
    local state = { mode = "SequentialDualPass", subPass = 1 }
    installProjectionCountingBridge(state)
    _G.FSBaseMission = { update = function(self, dt) end }
    VRCompanion.cameraNodeLeft = 9602
    VRCompanion.cameraNodeRight = 9603
    VRCompanion:setRenderingMode("DualPass")
    VRCompanion:hookMissionUpdate()
    local origSync = VRCompanion.syncActiveEyeCamera
    VRCompanion.syncActiveEyeCamera = function() error("camera node was deleted") end

    local results = {}
    local ok, err = pcall(function()
        for subPass = 0, 1 do
            state.subPass = subPass
            VRCompanion:setSubPassIndex(subPass)
            state.binds = {}
            _G.FSBaseMission:update(16.666)
            results[subPass] = {
                binds = state.binds,
                deferred = VRCompanion.deferRenderingCameraBind,
                scopeOpen = VRCompanion.eyeProjectionCache ~= nil and VRCompanion.eyeProjectionCache.active,
            }
        end
    end)
    VRCompanion.syncActiveEyeCamera = origSync
    VRCompanion:unhookMissionUpdate()
    _G.FSBaseMission = nil
    cleanupProjectionCountingBridge()
    assert(ok, tostring(err))

    for subPass = 0, 1 do
        local r = results[subPass]
        local expected = (subPass == 1) and 9603 or 9602
        assert(#r.binds == 1 and r.binds[1] == expected, string.format("sub-pass %d: eye camera %d must still be bound", subPass, expected))
        assert(r.deferred == false, string.format("sub-pass %d: the deferred bind must be lifted", subPass))
        assert(not r.scopeOpen, string.format("sub-pass %d: the projection scope must be closed", subPass))
    end
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

-- Dual-Pass Physics Pause: the engine simulates physics natively every frame, whatever dt the mission update is given,
-- so the vehicle moved between the two sub-passes. The pause stops it for one of the two frames of a pair.
local function withPhysicsRecorder(fn)
    local calls = {}
    _G.simulatePhysics = function(state) table.insert(calls, "simulate " .. tostring(state)) end
    _G.simulatePhysicsTimeScale = function(scale) table.insert(calls, string.format("scale %.2f", scale)) end
    _G.FSBaseMission = { update = function() end }
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
    VRCompanion.accumulatedPass1Dt = 0.0
    VRCompanion:hookMissionUpdate()

    local function subPass(index, dt)
        curSubPass = index
        VRCompanion:setSubPassIndex(index)
        _G.FSBaseMission:update(dt)
    end
    local ok, err = pcall(fn, calls, subPass)

    VRCompanion:releaseDualPassPhysicsPause()
    VRCompanion:setDualPassPhysicsPause("on")
    VRCompanion:unhookMissionUpdate()
    VRCompanion:setRenderingMode("AFR")
    VRCompanion:setSubPassIndex(0)
    _G.FSBaseMission = nil
    _G.VRBridge = nil
    _G.simulatePhysics = nil
    _G.simulatePhysicsTimeScale = nil
    if not ok then error(err, 0) end
end

test("DualPassPhysicsPauseIsOnByDefault", function()
    assert(VRCompanion.dualPassPhysicsPause == "on",
        "the pause must be on when the Companion Mod loads, got " .. tostring(VRCompanion.dualPassPhysicsPause))
end)

test("DualPassPhysicsPauseOffNeverTouchesPhysics", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("off")
        subPass(0, 11); subPass(1, 11); subPass(0, 11); subPass(1, 11)
        assert(#calls == 0, "with the pause off the physics natives must not be called, got " .. table.concat(calls, ", "))
    end)
end)

test("DualPassPhysicsPauseOnStopsPhysicsForTheSecondSubPassAndCatchesUpOnTheFirst", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        subPass(0, 11)
        assert(#calls == 0, "the first sub-pass with nothing paused must not touch physics, got " .. table.concat(calls, ", "))
        subPass(1, 11)
        assert(table.concat(calls, ", ") == "simulate false", "the second sub-pass must pause physics, got " .. table.concat(calls, ", "))
        subPass(0, 11)
        assert(table.concat(calls, ", ") == "simulate false, simulate true, scale 2.00",
            "the next first sub-pass must resume physics at the pair's time scale, got " .. table.concat(calls, ", "))
        subPass(1, 11)
        assert(calls[#calls] == "simulate false", "every second sub-pass must pause physics again")
    end)
end)

test("DualPassPhysicsPauseTimeScaleSimulatesThePairPeriodOverTheSecondSubPassesPredictedDt", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        -- The first pair runs at normal speed: nothing was paused before it
        subPass(0, 10)
        assert(#calls == 0, "the pair the pause starts in must run at normal speed, got " .. table.concat(calls, ", "))
        subPass(1, 8); subPass(0, 12)
        -- The pair period is the last second sub-pass's 8 ms and this first sub-pass's 12; the next step, predicted
        -- at the only second sub-pass's 8 ms, must simulate it: 20 / 8
        assert(calls[#calls] == "scale 2.50", "time scale must be the pair period over the predicted dt, got " .. tostring(calls[#calls]))
    end)
end)

test("DualPassPhysicsPauseMakesUpAMissedPredictionAFractionAtATime", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        subPass(0, 10); subPass(1, 8); subPass(0, 12)
        -- The second sub-pass took 10 ms, not the predicted 8: its step at 2.5 simulated 25 ms of the 22 owed
        subPass(1, 10)
        assert(math.abs(VRCompanion.physicsTimeOwed - (-3)) < 1e-6,
            "physics time owed must be the real dt minus the simulated, got " .. tostring(VRCompanion.physicsTimeOwed))
        subPass(0, 12)
        -- The period moves toward this pair's 10 + 12, only a fraction of the 3 ms over is taken off, and the
        -- prediction moves toward the 10 ms second sub-pass
        local period = 20 + VRCompanion.DUAL_PASS_PHYSICS_PAIR_PERIOD_SMOOTHING * (22 - 20)
        local predicted = 8 + VRCompanion.DUAL_PASS_PHYSICS_SECOND_SUB_PASS_DT_SMOOTHING * (10 - 8)
        local expected = (period + VRCompanion.DUAL_PASS_PHYSICS_OWED_TIME_GAIN * -3) / predicted
        assert(math.abs(VRCompanion.physicsTimeScaleApplied - expected) < 1e-6,
            string.format("time scale must be (period + gain x left over) / predicted dt: expected %.4f, got %.4f",
                expected, VRCompanion.physicsTimeScaleApplied))
    end)
end)

test("DualPassPhysicsPauseSimulatesASteadyPairPeriodWhenThePairsOwnDtsSwing", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        -- As in the headset: each second sub-pass's frame and the next first sub-pass's add up to the 22.2 ms refresh,
        -- while the second sub-pass's dt swings, so a pair's own two dts swing by twice as much
        local secondDts = { 9.2, 10.1, 8.7, 10.0, 8.6, 9.9, 9.0, 10.2, 8.8, 9.8, 8.9, 10.0, 8.7, 9.9, 9.1, 10.1 }
        local simulatedPerPair = {}
        local firstDt = 13.0
        for i = 1, #secondDts do
            subPass(0, firstDt)
            local scale = VRCompanion.physicsTimeScaleApplied or 1.0
            subPass(1, secondDts[i])
            table.insert(simulatedPerPair, secondDts[i] * scale)
            firstDt = 22.2 - secondDts[i]
        end
        local total = 0
        for i = 4, #simulatedPerPair do
            total = total + simulatedPerPair[i]
            -- The second sub-pass's dt swings 0.65 ms either side of the smoothed one, times a time scale of about 2.4
            assert(math.abs(simulatedPerPair[i] - 22.2) < 2.5, string.format(
                "pair %d simulated %.2f ms of a 22.2 ms period; only the prediction's miss may move it", i, simulatedPerPair[i]))
        end
        local mean = total / (#simulatedPerPair - 3)
        assert(math.abs(mean - 22.2) < 0.5, string.format("physics must keep real time on average, simulated %.2f ms per pair", mean))
        assert(math.abs(VRCompanion.physicsTimeOwed) < 5,
            "physics time owed must stay bounded, got " .. tostring(VRCompanion.physicsTimeOwed))
    end)
end)

test("LeavingDualPassReleasesThePhysicsPause", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        subPass(0, 11); subPass(1, 11); subPass(0, 11); subPass(1, 11)
        -- 2D Floating Window Mode renders mono, outside Sequential Dual-Pass
        VRCompanion.is2DViewActive = true
        _G.FSBaseMission:update(11)
        VRCompanion.is2DViewActive = false
        assert(table.concat(calls, ", ") == "simulate false, simulate true, scale 2.00, simulate false, simulate true, scale 1.00",
            "leaving Sequential Dual-Pass must resume physics at normal speed, got " .. table.concat(calls, ", "))
        VRCompanion.is2DViewActive = true
        _G.FSBaseMission:update(11)
        VRCompanion.is2DViewActive = false
        assert(#calls == 6, "once released the physics natives must not be called again")
    end)
end)

test("TurningThePhysicsPauseOffReleasesIt", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        subPass(0, 11); subPass(1, 11)
        VRCompanion:consoleCommandDualPassPhysicsPause("off")
        -- The time scale was never changed, so only the pause is undone
        assert(table.concat(calls, ", ") == "simulate false, simulate true",
            "turning the pause off must resume physics at once, got " .. table.concat(calls, ", "))
    end)
end)

test("DualPassPhysicsPauseLeavesTheGamesOwnVehicleReloadPauseAlone", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        subPass(0, 11); subPass(1, 11)
        _G.g_currentMission = { vehicleSystem = { isReloadRunning = true } }
        subPass(0, 11)
        _G.g_currentMission = nil
        assert(table.concat(calls, ", ") == "simulate false",
            "while the game reloads vehicles with physics paused the mod must not resume it, got " .. table.concat(calls, ", "))
    end)
end)

-- In multiplayer the game corrects the player's predicted movement against the server's through physics steps, and a
-- paused step makes the view rubber-band, so the pause stands aside there whatever its setting.
test("DualPassPhysicsPauseNeverTouchesPhysicsInMultiplayer", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        _G.g_currentMission = { missionDynamicInfo = { isMultiplayer = true } }
        local ok, err = pcall(function()
            subPass(0, 11); subPass(1, 11); subPass(0, 11); subPass(1, 11)
        end)
        _G.g_currentMission = nil
        if not ok then error(err, 0) end
        assert(#calls == 0, "in multiplayer the physics natives must not be called, got " .. table.concat(calls, ", "))
        assert(VRCompanion.dualPassPhysicsPause == "on", "multiplayer must not change the player's setting")
    end)
end)

test("DualPassPhysicsPauseStillPausesInASinglePlayerMission", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        _G.g_currentMission = { missionDynamicInfo = { isMultiplayer = false } }
        local ok, err = pcall(function()
            subPass(0, 11); subPass(1, 11)
        end)
        _G.g_currentMission = nil
        if not ok then error(err, 0) end
        assert(table.concat(calls, ", ") == "simulate false",
            "a single-player mission must still pause physics, got " .. table.concat(calls, ", "))
    end)
end)

test("DualPassPhysicsPauseAppliedWhenAMultiplayerMissionStartsIsReleased", function()
    withPhysicsRecorder(function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        subPass(0, 11); subPass(1, 11)
        _G.g_currentMission = { missionDynamicInfo = { isMultiplayer = true } }
        local ok, err = pcall(function()
            subPass(0, 11); subPass(1, 11)
        end)
        _G.g_currentMission = nil
        if not ok then error(err, 0) end
        assert(table.concat(calls, ", ") == "simulate false, simulate true",
            "a pause left from before must be resumed at normal speed in multiplayer, got " .. table.concat(calls, ", "))
    end)
end)

test("DualPassPhysicsPauseConsoleCommandSaysItIsHeldOffInMultiplayer", function()
    _G.g_currentMission = { missionDynamicInfo = { isMultiplayer = true } }
    local ok, reply = pcall(function() return VRCompanion:consoleCommandDualPassPhysicsPause() end)
    _G.g_currentMission = nil
    if not ok then error(reply, 0) end
    assert(type(reply) == "string" and reply:find("multiplayer", 1, true),
        "in multiplayer the reply must say the pause is held off, got " .. tostring(reply))
    reply = VRCompanion:consoleCommandDualPassPhysicsPause()
    assert(not reply:find("multiplayer", 1, true), "outside multiplayer the reply must not mention it, got " .. reply)
end)

test("DualPassPhysicsPauseConsoleCommand", function()
    local reply = VRCompanion:consoleCommandDualPassPhysicsPause("on")
    assert(VRCompanion.dualPassPhysicsPause == "on", "on must set the pause")
    assert(type(reply) == "string" and reply:find("on", 1, true), "the reply must name the new setting")
    VRCompanion:consoleCommandDualPassPhysicsPause("OFF")
    assert(VRCompanion.dualPassPhysicsPause == "off", "the argument must be case-insensitive")
    VRCompanion:consoleCommandDualPassPhysicsPause("on")
    reply = VRCompanion:consoleCommandDualPassPhysicsPause()
    assert(VRCompanion.dualPassPhysicsPause == "on" and reply:find("is on", 1, true), "no argument must report the setting")
    reply = VRCompanion:consoleCommandDualPassPhysicsPause("shifted")
    assert(VRCompanion.dualPassPhysicsPause == "on" and reply:find("Usage", 1, true), "an unknown argument must print the usage")
    VRCompanion:consoleCommandDualPassPhysicsPause("off")
    assert(VRCompanion.dualPassPhysicsPause == "off", "off must clear the pause")
    VRCompanion:setDualPassPhysicsPause("on")
end)

test("EyePairTraceLogsHowFarTheVehicleMovedSidewaysBetweenEyeFrames", function()
    local vehiclePos = { 100, 0, 50 }
    local cameraPos = { 100, 2, 50 }
    local physicsIndex = 7
    local vehicle = { rootNode = 9100, getLastSpeed = function() return 21.6 end }
    _G.g_currentMission = { controlledVehicle = vehicle }
    _G.getWorldTranslation = function(node)
        if node == 9100 then return vehiclePos[1], vehiclePos[2], vehiclePos[3] end
        return cameraPos[1], cameraPos[2], cameraPos[3]
    end
    -- The head looks along the vehicle's left: its right axis is the vehicle's forward, world +Z here
    _G.localDirectionToWorld = function(node, x, y, z) return 0, 0, 1 end
    _G.getPhysicsUpdateIndex = function() return physicsIndex end
    local lines = {}
    local function trace(eye, subPass)
        local line = VRCompanion:traceEyeFrame(eye, 2001, subPass)
        if line ~= nil then table.insert(lines, line) end
    end

    local ok, err = pcall(function()
        VRCompanion:consoleCommandEyePairTrace("2")
        trace(0, 0)
        vehiclePos[3] = 50.06
        cameraPos[3] = 50.06
        physicsIndex = 8
        trace(1, 1)
        trace(0, 0)
    end)
    _G.g_currentMission = nil
    _G.getWorldTranslation = nil
    _G.localDirectionToWorld = nil
    _G.getPhysicsUpdateIndex = nil
    if not ok then error(err, 0) end

    assert(#lines == 2, string.format("a trace of 2 frames must log 2 lines, got %d", #lines))
    local second = lines[2]
    assert(second:find("sub-pass 1", 1, true) and second:find("eye 1", 1, true), "the line must name the sub-pass and eye: " .. second)
    assert(second:find("physics 8", 1, true), "the line must give the physics update index: " .. second)
    assert(second:find("vehicle moved 6.00 cm", 1, true), "the line must give how far the vehicle moved: " .. second)
    assert(second:find("6.00 cm along the eyes", 1, true), "the line must give the part along the eyes: " .. second)
    assert(second:find("21.6 km/h", 1, true), "the line must give the vehicle's speed: " .. second)
end)

test("EyePairTraceLogsTheDtsAndTimeScaleBehindTheMove", function()
    _G.getWorldTranslation = function() return 0, 0, 0 end
    _G.g_physicsDt, _G.g_physicsDtUnclamped, _G.g_physicsDtNonInterpolated = 8.5, 9.25, 0
    local lines = {}
    local traceEyeFrame = VRCompanion.traceEyeFrame
    VRCompanion.traceEyeFrame = function(...)
        local line = traceEyeFrame(...)
        if line ~= nil then table.insert(lines, line) end
        return line
    end

    local ok, err = pcall(withPhysicsRecorder, function(calls, subPass)
        VRCompanion:setDualPassPhysicsPause("on")
        VRCompanion:consoleCommandEyePairTrace("4")
        subPass(0, 10)
        subPass(1, 12)
        _G.g_physicsDtNonInterpolated = 16.6
        subPass(0, 10)
        subPass(1, 12)
    end)
    VRCompanion.traceEyeFrame = traceEyeFrame
    _G.getWorldTranslation = nil
    _G.g_physicsDt, _G.g_physicsDtUnclamped, _G.g_physicsDtNonInterpolated = nil, nil, nil
    if not ok then error(err, 0) end

    assert(#lines == 4, string.format("a trace of 4 frames must log 4 lines, got %d", #lines))
    local second = lines[2]
    assert(second:find("sub-pass 1", 1, true) and second:find("rawDt 12.00", 1, true),
        "the line must give the sub-pass update's real dt: " .. second)
    assert(second:find("physics dts 8.50/9.25/0.00", 1, true), "the line must give the engine's physics dts: " .. second)
    -- Each line shows the state the earlier sub-passes left: logged before the sub-pass's own pause or resume
    assert(second:find("physics scale 1.00", 1, true), "nothing is paused before the first pause: " .. second)
    assert(lines[3]:find("physics paused", 1, true), "the first sub-pass after a pause must show it: " .. lines[3])
    assert(lines[3]:find("physics dts 8.50/9.25/16.60", 1, true), "the dts must be read on each line: " .. lines[3])
    -- Resumed physics covers the paused 10 ms step and the next, predicted at the last second sub-pass's 12 ms
    assert(lines[4]:find("physics scale 1.83", 1, true), "the line must give the applied time scale: " .. lines[4])
    assert(not second:find("predicted", 1, true), "nothing is predicted before the first pair: " .. second)
    assert(lines[4]:find("rawDt 12.00 (predicted 12.00)", 1, true),
        "a second sub-pass's line must give the dt its time scale was set for: " .. lines[4])
end)

print(string.format("Summary: %d passed, %d failed", passed, failed))
assert(failed == 0, string.format("%d tests failed", failed))
