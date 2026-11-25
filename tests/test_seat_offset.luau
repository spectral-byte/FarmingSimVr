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

local function near(a, b) return math.abs(a - b) < 0.0005 end

local CAB = 999
local nodes, nextNode

local function nodeOf(id)
    local n = nodes[id]
    if n == nil then
        n = { t = { 0, 0, 0 }, yaw = 0, parent = CAB }
        nodes[id] = n
    end
    return n
end

local function installScene()
    nodes, nextNode = {}, 7000
    _G.setTranslation = function(id, x, y, z) nodeOf(id).t = { x, y, z } end
    _G.getTranslation = function(id) local t = nodeOf(id).t return t[1], t[2], t[3] end
    _G.setRotation = function(id, rx, ry, rz) nodeOf(id).yaw = ry end
    _G.getRotation = function(id) return 0, nodeOf(id).yaw, 0 end
    _G.getParent = function(id) return nodeOf(id).parent end
    _G.link = function(parent, id) nodeOf(id).parent = parent end
    _G.createTransformGroup = function(name)
        nextNode = nextNode + 1
        nodeOf(nextNode)
        return nextNode
    end
    _G.localToLocal = function(from, to, x, y, z)
        if from ~= to and nodeOf(to).parent == from then
            local c = nodeOf(to)
            return VRCompanion:rotateVectorByYaw(x - c.t[1], y - c.t[2], z - c.t[3], -c.yaw)
        end
        local n = nodeOf(from)
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, n.yaw)
        return n.t[1] + rx, n.t[2] + ry, n.t[3] + rz
    end
    _G.localRotationToLocal = function(from, to, rx, ry, rz) return rx, ry + nodeOf(from).yaw, rz end
end

-- A bridge whose head pose is the table `state.pose` (nil: tracking lost) and which records the calls
local function installBridge(state)
    state.flushes, state.recenters = 0, 0
    _G.VRBridge = {
        isReady = function() return true end,
        getFrameParity = function() return 0 end,
        getRequiresAfrFallback = function() return false end,
        getHeadPose = function()
            if state.pose == nil then return { isValid = false } end
            return state.pose
        end,
        recenter = function()
            state.recenters = state.recenters + 1
            if state.pose ~= nil then
                state.pose = { isValid = true, x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0 }
            end
            return { success = true }
        end,
        signalDiscontinuity = function() state.flushes = state.flushes + 1 end,
        resetTemporalHistory = function() end,
    }
end

local function poseAt(x, y, z) return { isValid = true, x = x, y = y, z = z, pitch = 0, yaw = 0, roll = 0 } end

local function resetCompanion()
    VRCompanion.cameraNodeLeft = nil
    VRCompanion.cameraNodeRight = nil
    VRCompanion.cyclopeanAudioListenerNode = nil
    VRCompanion.activeRenderingCamera = nil
    VRCompanion.registeredEyeCameras = nil
    VRCompanion.gameCameraNode = nil
    VRCompanion.origCamSettings = {}
    VRCompanion.is2DViewActive = false
    VRCompanion.isMenuVisible = false
    VRCompanion.menuTakesInput = false
    VRCompanion.recenterVrTrackingSpace = true
    VRCompanion.headPoseScratch = nil
    VRCompanion.vehicleRotationStates = {}
    VRCompanion.lastControlledVehicle = nil
end

local function makeVehicle(config, cams, index)
    local veh = { configFileName = config, spec_enterable = { cameras = cams, camIndex = index or 1 } }
    veh.getActiveCamera = function(self) return self.spec_enterable.cameras[self.spec_enterable.camIndex] end
    for _, cam in ipairs(cams) do cam.vehicle = veh end
    return veh
end

local modelCount = 0
local function freshModel()
    modelCount = modelCount + 1
    return string.format("data/vehicles/make/model%d.xml", modelCount)
end

local nextCameraNode = 100
local function fallbackCab()
    nextCameraNode = nextCameraNode + 1
    nodeOf(nextCameraNode).t = { 0.3, 1.4, -0.2 }
    nodeOf(nextCameraNode).parent = 0
    return {
        isInside = true, cameraNode = nextCameraNode,
        origTransX = 0.3, origTransY = 1.4, origTransZ = -0.2,
        origRotY = math.pi, rotY = math.pi, origRotX = 0, rotX = 0,
    }
end

local nextHeadNode = 500
local function trackedCab(lookYaw)
    nextCameraNode = nextCameraNode + 1
    nextHeadNode = nextHeadNode + 1
    nodeOf(nextCameraNode).t = { 0, 1.6, 0.2 }
    nodeOf(nextHeadNode).t = { 0, 1.6, 0.2 }
    nodeOf(nextHeadNode).yaw = math.pi
    return {
        isInside = true, cameraNode = nextCameraNode, headTrackingNode = nextHeadNode,
        origRotY = math.pi, rotY = math.pi + (lookYaw or 0), origRotX = 0, rotX = 0,
    }
end

local state
local function setup(pose)
    installScene()
    state = { pose = pose or poseAt(0, 0, 0) }
    installBridge(state)
    resetCompanion()
    _G.g_currentMission = nil
end

local function enter(veh)
    _G.g_currentMission = { controlledVehicle = veh }
    VRCompanion:onEnterVehicle(veh, true, nil)
end

local function translationOf(id) local t = nodeOf(id).t return { x = t[1], y = t[2], z = t[3] } end

local function viewAfterUpdate(cam)
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    return {
        engine = translationOf(cam.cameraNode),
        eye = translationOf(VRCompanion.cameraNodeLeft),
        listener = translationOf(VRCompanion.cyclopeanAudioListenerNode),
    }
end

local function assertMoved(view, base, dx, dy, dz, label)
    for _, place in ipairs({ "engine", "eye", "listener" }) do
        local v, b = view[place], base[place]
        assert(near(v.x - b.x, dx) and near(v.y - b.y, dy) and near(v.z - b.z, dz), string.format(
            "%s: the %s moved by (%.3f, %.3f, %.3f), expected (%.3f, %.3f, %.3f)",
            label, place, v.x - b.x, v.y - b.y, v.z - b.z, dx, dy, dz))
    end
end

print("Running Seat Offset Tests...")

test("SeatOffsetMovesTheFallbackCabViewByForwardUpAndRightInTheCab", function()
    setup()
    local cam = fallbackCab()
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    local base = viewAfterUpdate(cam)

    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local view = viewAfterUpdate(cam)

    -- forward 0.1 is +Z, up 0.2 is +Y, right 0.3 is -X
    assertMoved(view, base, -0.3, 0.2, 0.1, "fallback path")
end)

test("SeatOffsetMovesTheTrackedCabViewInTheCabWhateverTheLookYaw", function()
    setup(poseAt(0.05, 0.02, -0.1))
    local cam = trackedCab(0.6)
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    local base = viewAfterUpdate(cam)

    VRCompanion:consoleCommandSeatOffset("-0.2", "-0.1", "0.4")
    local view = viewAfterUpdate(cam)

    assertMoved(view, base, -0.4, -0.1, -0.2, "head tracking node path, looking 0.6 rad to the side")
end)

-- Slice 3: a Snap Turn does not move the offset in the cab
test("SnapTurnLeavesTheSeatOffsetWhereItWasInTheCab", function()
    setup()
    local cam = trackedCab(0)
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    VRCompanion:consoleCommandSeatOffset("0", "0", "0")
    local base = viewAfterUpdate(cam)

    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local before = viewAfterUpdate(cam)
    assert(VRCompanion:snapTurnRight(math.pi * 0.5, 1000), "the Snap Turn must be taken")
    local after = viewAfterUpdate(cam)

    assertMoved(before, base, -0.3, 0.2, 0.1, "before the Snap Turn")
    assertMoved(after, base, -0.3, 0.2, 0.1, "after the Snap Turn")
end)

local function inTractor()
    setup()
    local cam = trackedCab(0)
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    return cam, veh
end

test("ConsoleWithNoArgumentsPrintsTheCurrentOffsetAndChangesNothing", function()
    local cam = inTractor()
    local zero = VRCompanion:consoleCommandSeatOffset()
    assert(string.find(zero, "forward 0.000", 1, true) and string.find(zero, "up 0.000", 1, true)
        and string.find(zero, "right 0.000", 1, true), "nothing set prints 0 0 0, got: " .. tostring(zero))
    assert(string.find(zero, "vrSeatOffset", 1, true), "the print shows the usage, got: " .. tostring(zero))

    VRCompanion:consoleCommandSeatOffset("0.1", "-0.25", "0.3")
    local flushes = state.flushes
    local base = viewAfterUpdate(cam)
    local reply = VRCompanion:consoleCommandSeatOffset()
    assert(string.find(reply, "forward 0.100", 1, true) and string.find(reply, "up -0.250", 1, true)
        and string.find(reply, "right 0.300", 1, true), "the set offset is printed, got: " .. tostring(reply))
    assert(state.flushes == flushes, "printing signals no Discontinuity Flush")
    assertMoved(viewAfterUpdate(cam), base, 0, 0, 0, "after printing")
end)

test("ConsoleClampsEachAxisToHalfAMetreAndNamesTheClampedAxes", function()
    local cam = inTractor()
    local base = viewAfterUpdate(cam)

    local reply = VRCompanion:consoleCommandSeatOffset("0.9", "0.1", "-0.7")
    assert(string.match(reply, "Clamped to 0.5 m: ([%a, ]+)") == "forward, right",
        "the reply names the clamped axes, got: " .. tostring(reply))
    assertMoved(viewAfterUpdate(cam), base, 0.5, 0.1, 0.5, "clamped")

    local fine = VRCompanion:consoleCommandSeatOffset("0.5", "-0.5", "0")
    assert(string.find(fine, "Clamped", 1, true) == nil, "exactly the limit is not clamped, got: " .. tostring(fine))
    local low = VRCompanion:consoleCommandSeatOffset("-0.51", "0", "0")
    assert(string.match(low, "Clamped to 0.5 m: ([%a, ]+)") == "forward", "just past the limit is clamped, got: " .. tostring(low))
end)

test("ConsoleSetIsAbsoluteSoTypingItTwiceGivesTheSameSeat", function()
    local cam = inTractor()
    local base = viewAfterUpdate(cam)
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local once = viewAfterUpdate(cam)
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    assertMoved(viewAfterUpdate(cam), once, 0, 0, 0, "typed twice")
    assertMoved(once, base, -0.3, 0.2, 0.1, "typed once")
end)

test("ConsoleResetPutsTheSeatBackAtTheVehiclesOwnEyePoint", function()
    local cam = inTractor()
    local base = viewAfterUpdate(cam)
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local reply = VRCompanion:consoleCommandSeatOffset("reset")
    assert(string.find(reply, "reset", 1, true), "the reply says it was reset, got: " .. tostring(reply))
    assertMoved(viewAfterUpdate(cam), base, 0, 0, 0, "reset")
    assert(string.find(VRCompanion:consoleCommandSeatOffset(), "forward 0.000", 1, true), "reset prints 0 0 0")
end)

test("ConsoleWithMalformedArgumentsRepliesTheUsageAndChangesNothing", function()
    local cam = inTractor()
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local base = viewAfterUpdate(cam)
    local flushes = state.flushes
    for _, args in ipairs({ { "abc" }, { "0.1", "0.2" }, { "0.1", "0.2", "0.3", "0.4" }, { "a", "b", "c" },
            { "reset", "now" }, { "0.1", "nan", "0.3" }, { "0.1", "inf", "0.3" } }) do
        local reply = VRCompanion:consoleCommandSeatOffset(table.unpack(args))
        assert(string.sub(reply, 1, 6) == "Usage:" and string.find(reply, "vrSeatOffset", 1, true),
            "malformed (" .. table.concat(args, " ") .. ") replies the usage, got: " .. tostring(reply))
    end
    assertMoved(viewAfterUpdate(cam), base, 0, 0, 0, "after malformed arguments")
    assert(state.flushes == flushes, "malformed arguments signal no Discontinuity Flush")
end)

test("ConsoleChangeSignalsOneDiscontinuityFlush", function()
    inTractor()
    local flushes = state.flushes
    VRCompanion:consoleCommandSeatOffset("0.1", "0", "0")
    assert(state.flushes == flushes + 1, "setting signals a flush")
    VRCompanion:consoleCommandSeatOffset("reset")
    assert(state.flushes == flushes + 2, "resetting signals a flush")
end)

test("ConsoleOutsideAnInCabCameraRepliesThereIsNoneAndChangesNothing", function()
    setup()
    local cabCam = trackedCab(0)
    nextCameraNode = nextCameraNode + 1
    local outsideCam = { isInside = false, cameraNode = nextCameraNode, rotY = 0, rotX = 0 }
    local veh = makeVehicle(freshModel(), { cabCam, outsideCam }, 2)
    enter(veh)
    local flushes = state.flushes

    for _, args in ipairs({ {}, { "0.1", "0.2", "0.3" }, { "reset" } }) do
        local reply = VRCompanion:consoleCommandSeatOffset(table.unpack(args))
        assert(string.find(reply, "no in-cab camera", 1, true),
            "in an Outside Camera (" .. table.concat(args, " ") .. ") the reply says there is no in-cab camera, got: " .. tostring(reply))
    end
    _G.g_currentMission = { controlledVehicle = nil }
    assert(string.find(VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3"), "no in-cab camera", 1, true), "on foot")
    assert(state.flushes == flushes, "nothing changed, so no Discontinuity Flush")

    veh.spec_enterable.camIndex = 1
    _G.g_currentMission = { controlledVehicle = veh }
    local base = viewAfterUpdate(cabCam)
    assert(string.find(VRCompanion:consoleCommandSeatOffset(), "forward 0.000", 1, true), "the cab still has no offset")
    veh.spec_enterable.camIndex = 1
    VRCompanion:consoleCommandSeatOffset("0.1", "0", "0")
    assertMoved(viewAfterUpdate(cabCam), base, 0, 0, 0.1, "the cab camera")
end)

local function recenterLandsAtEyePointPlusOffset(makeCam, eyePoint, mode)
    setup(poseAt(0.2, 0.1, -0.3))
    local cam = makeCam()
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    VRCompanion.recenterVrTrackingSpace = (mode == "Both")
    VRCompanion:consoleCommandSeatOffset("0.1", "-0.2", "0.3")
    viewAfterUpdate(cam)

    VRCompanion:onRecenterAction()
    if mode == "Both" then
        assert(state.recenters == 1, "Both re-zeroes the tracking space")
    else
        assert(state.recenters == 0, "VehicleOnly leaves the tracking space alone")
        state.pose = poseAt(0, 0, 0)
    end
    local view = viewAfterUpdate(cam)

    for _, place in ipairs({ "engine", "eye", "listener" }) do
        local v = view[place]
        assert(near(v.x, eyePoint[1] - 0.3) and near(v.y, eyePoint[2] - 0.2) and near(v.z, eyePoint[3] + 0.1), string.format(
            "%s mode: the %s is at (%.3f, %.3f, %.3f), expected the eye point moved by the offset (%.3f, %.3f, %.3f)",
            mode, place, v.x, v.y, v.z, eyePoint[1] - 0.3, eyePoint[2] - 0.2, eyePoint[3] + 0.1))
    end
    local reply = VRCompanion:consoleCommandSeatOffset()
    assert(string.find(reply, "forward 0.100, up -0.200, right 0.300", 1, true), "Recenter never changes the offset, got: " .. tostring(reply))
end

test("RecenterLandsAtTheTrackedEyePointMovedByTheSeatOffsetInBothMode", function()
    recenterLandsAtEyePointPlusOffset(function() return trackedCab(0.4) end, { 0, 1.6, 0.2 }, "Both")
end)

test("RecenterLandsAtTheTrackedEyePointMovedByTheSeatOffsetInVehicleOnlyMode", function()
    recenterLandsAtEyePointPlusOffset(function() return trackedCab(0.4) end, { 0, 1.6, 0.2 }, "VehicleOnly")
end)

test("RecenterLandsAtTheFallbackEyePointMovedByTheSeatOffsetInBothMode", function()
    recenterLandsAtEyePointPlusOffset(fallbackCab, { 0.3, 1.4, -0.2 }, "Both")
end)

test("RecenterLandsAtTheFallbackEyePointMovedByTheSeatOffsetInVehicleOnlyMode", function()
    recenterLandsAtEyePointPlusOffset(fallbackCab, { 0.3, 1.4, -0.2 }, "VehicleOnly")
end)

test("TwoVehiclesOfOneModelShareASeatOffsetAndAnotherModelHasItsOwn", function()
    setup()
    local model = freshModel()
    local camA, camB, camC = trackedCab(0), trackedCab(0), trackedCab(0)
    local vehA = makeVehicle(model, { camA })
    local vehB = makeVehicle(model, { camB })
    local vehC = makeVehicle(freshModel(), { camC })
    enter(vehA)
    local baseA, baseB, baseC = viewAfterUpdate(camA), viewAfterUpdate(camB), viewAfterUpdate(camC)

    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")

    assertMoved(viewAfterUpdate(camA), baseA, -0.3, 0.2, 0.1, "the vehicle it was set in")
    assertMoved(viewAfterUpdate(camB), baseB, -0.3, 0.2, 0.1, "another vehicle of the same model")
    assertMoved(viewAfterUpdate(camC), baseC, 0, 0, 0, "a vehicle of another model")
end)

test("APassengerCameraKeepsASeatOffsetApartFromTheDriversAndEachComesBack", function()
    setup()
    local driver, passenger = trackedCab(0), trackedCab(0)
    nextCameraNode = nextCameraNode + 1
    local outside = { isInside = false, cameraNode = nextCameraNode, rotY = 0, rotX = 0 }
    local veh = makeVehicle(freshModel(), { driver, outside, passenger })
    enter(veh)
    local baseDriver, basePassenger = viewAfterUpdate(driver), viewAfterUpdate(passenger)

    VRCompanion:consoleCommandSeatOffset("0.1", "0", "0")
    veh.spec_enterable.camIndex = 3
    assertMoved(viewAfterUpdate(passenger), basePassenger, 0, 0, 0, "the passenger starts with none")
    VRCompanion:consoleCommandSeatOffset("0", "0.4", "0")      -- the passenger's
    assertMoved(viewAfterUpdate(passenger), basePassenger, 0, 0.4, 0, "the passenger's own")

    veh.spec_enterable.camIndex = 2
    VRCompanion:onVehicleCameraChanged(veh, 2)
    veh.spec_enterable.camIndex = 1
    VRCompanion:onVehicleCameraChanged(veh, 1)
    assertMoved(viewAfterUpdate(driver), baseDriver, 0, 0, 0.1, "the driver's own, back from the Outside Camera")
end)

test("EnteringAndLeavingAndSwitchingVehiclesNeedNoExtraStateForTheSeatOffset", function()
    setup()
    local camA, camB = trackedCab(0), trackedCab(0)
    local vehA = makeVehicle(freshModel(), { camA })
    local vehB = makeVehicle(freshModel(), { camB })
    enter(vehA)
    local baseA, baseB = viewAfterUpdate(camA), viewAfterUpdate(camB)
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")

    VRCompanion:onLeaveVehicle(vehA)
    _G.g_currentMission = { controlledVehicle = vehB }
    VRCompanion:onEnterVehicle(vehB, true, nil)
    assertMoved(viewAfterUpdate(camB), baseB, 0, 0, 0, "the other vehicle")
    VRCompanion:onVehicleTransition(vehB, vehA)
    _G.g_currentMission = { controlledVehicle = vehA }
    assertMoved(viewAfterUpdate(camA), baseA, -0.3, 0.2, 0.1, "back in the first vehicle")
end)

local function installWorldReads()
    _G.getRootNode = function() return 1 end
    _G.getWorldTranslation = function(id) local t = nodeOf(id).t return t[1], t[2], t[3] end
    _G.getWorldQuaternion = function() return 0, 0, 0, 1 end
    _G.localDirectionToWorld = function(id, x, y, z) return x, y, z end
end

local function removeWorldReads()
    _G.getRootNode, _G.getWorldTranslation, _G.getWorldQuaternion, _G.localDirectionToWorld = nil, nil, nil, nil
    _G.g_cameraManager = nil
end

test("TwoDFloatingWindowModeLeavesTheGameCameraAsItIs", function()
    local cam = inTractor()
    VRCompanion:consoleCommandSeatOffset("0.4", "0.3", "-0.2")
    VRCompanion.is2DViewActive = true
    nodeOf(cam.cameraNode).t = { 0, 1.6, 0.2 }
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    local t = translationOf(cam.cameraNode)
    assert(near(t.x, 0) and near(t.y, 1.6) and near(t.z, 0.2), string.format(
        "in 2D Floating Window Mode the camera is at (%.3f, %.3f, %.3f), the game's own (0, 1.6, 0.2)", t.x, t.y, t.z))
end)

test("AMenuShowingTheGamesOwnCameraLeavesItAsItIs", function()
    local cam = inTractor()
    VRCompanion:consoleCommandSeatOffset("0.4", "0.3", "-0.2")
    local menuCamera = 424242
    _G.g_cameraManager = { getActiveCamera = function() return menuCamera end }
    nodeOf(cam.cameraNode).t = { 0, 1.6, 0.2 }
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    removeWorldReads()
    local t = translationOf(cam.cameraNode)
    assert(near(t.x, 0) and near(t.y, 1.6) and near(t.z, 0.2), string.format(
        "behind a menu the camera is at (%.3f, %.3f, %.3f), the game's own (0, 1.6, 0.2)", t.x, t.y, t.z))
end)

test("LostHeadTrackingShowsTheEngineCameraViewWithoutTheSeatOffset", function()
    local cam = inTractor()
    installWorldReads()
    viewAfterUpdate(cam)
    VRCompanion:consoleCommandSeatOffset("0.4", "0.3", "-0.2")
    state.pose = nil
    nodeOf(cam.cameraNode).t = { 0, 1.6, 0.2 }
    VRCompanion:onVehicleCameraUpdate(cam, 16.6)
    local eye, listener = translationOf(VRCompanion.cameraNodeLeft), translationOf(VRCompanion.cyclopeanAudioListenerNode)
    removeWorldReads()
    for name, t in pairs({ eye = eye, listener = listener }) do
        assert(near(t.x, 0) and near(t.y, 1.6) and near(t.z, 0.2), string.format(
            "with tracking lost the %s is at (%.3f, %.3f, %.3f), the engine camera's (0, 1.6, 0.2)", name, t.x, t.y, t.z))
    end
end)

test("OutsideCamerasIgnoreTheSeatOffset", function()
    setup(poseAt(0.05, 0.02, -0.1))
    installWorldReads()
    local cabCam = trackedCab(0)
    nextCameraNode = nextCameraNode + 1
    nodeOf(nextCameraNode).t = { 5, 3, -8 }
    local outsideCam = { isInside = false, cameraNode = nextCameraNode, rotY = 0, rotX = 0 }
    local veh = makeVehicle(freshModel(), { cabCam, outsideCam }, 2)
    enter(veh)
    VRCompanion:onVehicleCameraUpdate(outsideCam, 16.6)
    local before = translationOf(VRCompanion.cyclopeanAudioListenerNode)

    veh.spec_enterable.camIndex = 1
    VRCompanion:consoleCommandSeatOffset("0.4", "0.3", "-0.2")
    veh.spec_enterable.camIndex = 2
    VRCompanion:onVehicleCameraUpdate(outsideCam, 16.6)
    local after = translationOf(VRCompanion.cyclopeanAudioListenerNode)
    removeWorldReads()
    assert(near(after.x, before.x) and near(after.y, before.y) and near(after.z, before.z), string.format(
        "the Outside Camera's head moved from (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)",
        before.x, before.y, before.z, after.x, after.y, after.z))
end)

test("OnFootIgnoresTheSeatOffset", function()
    setup(poseAt(0.05, 0.02, -0.1))
    nextCameraNode = nextCameraNode + 1
    nodeOf(nextCameraNode).t = { 0, 1.7, 0 }
    local playerCam = { cameraNode = nextCameraNode, firstPersonCamera = nextCameraNode }
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    local before = translationOf(VRCompanion.cyclopeanAudioListenerNode)

    local cam = trackedCab(0)
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    VRCompanion:consoleCommandSeatOffset("0.4", "0.3", "-0.2")
    VRCompanion:onLeaveVehicle(veh)
    _G.g_currentMission = { controlledVehicle = nil }
    VRCompanion:onPlayerCameraUpdate(playerCam, 16.6)
    local after = translationOf(VRCompanion.cyclopeanAudioListenerNode)
    assert(near(after.x, before.x) and near(after.y, before.y) and near(after.z, before.z), string.format(
        "on foot the head moved from (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)",
        before.x, before.y, before.z, after.x, after.y, after.z))
end)

test("VirtualHandsAndPointerRayFollowTheHeadTheSeatOffsetMovesWithNoChangeOfTheirOwn", function()
    local headPose = poseAt(0.05, 0.02, -0.1)
    headPose.qx, headPose.qy, headPose.qz, headPose.qw = 0, 0, 0, 1
    setup(headPose)
    installWorldReads()
    local handshakeComplete = VRCompanion.handshakeComplete
    VRCompanion.handshakeComplete = true
    local cam = trackedCab(0.6)
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    local hands = {
        { name = "right hand", x = 0.25, y = -0.1, z = -0.2 },
        { name = "left hand", x = -0.25, y = -0.1, z = -0.2 },
        { name = "Pointer Ray origin", x = 0.3, y = -0.2, z = -0.4 },
    }
    local function handsInTheWorld()
        local world = {}
        for _, hand in ipairs(hands) do
            local out = {}
            local ok, why = VRCompanion:headRelativePoseToWorld(hand.x, hand.y, hand.z, 0, 0, 0, 1, out)
            if not ok then VRCompanion.handshakeComplete = handshakeComplete end
            assert(ok, hand.name .. " is not placed in the world: " .. tostring(why))
            world[hand.name] = out
        end
        return world
    end

    local base = viewAfterUpdate(cam)
    local before = handsInTheWorld()
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    local view = viewAfterUpdate(cam)
    local after = handsInTheWorld()
    VRCompanion.handshakeComplete = handshakeComplete
    removeWorldReads()

    assertMoved(view, base, -0.3, 0.2, 0.1, "the head")
    for _, hand in ipairs(hands) do
        local a, b = after[hand.name], before[hand.name]
        assert(near(a.x - b.x, -0.3) and near(a.y - b.y, 0.2) and near(a.z - b.z, 0.1), string.format(
            "the %s moved by (%.3f, %.3f, %.3f) in the world, the head by (-0.3, 0.2, 0.1)", hand.name, a.x - b.x, a.y - b.y, a.z - b.z))
    end
end)

-- The rest pose without a neutral yaw: the camera update has always taken 0 for it and the realign to the seat pi, so each asks
-- for its own and the two are told apart
test("RestPoseOfACameraWithNoNeutralYawUsesTheDefaultTheCallerAsksFor", function()
    setup()
    local cam = fallbackCab()
    cam.origRotY = nil
    local rest = VRCompanion:getCabRestPose(cam, 0, 0, 0.5, 0)
    assert(near(rest.yaw, 0.5), "a default of 0 turns the fallback by the look yaw alone, got " .. tostring(rest.yaw))
    rest = VRCompanion:getCabRestPose(cam, 0, 0, 0.5, math.pi)
    assert(near(rest.yaw, VRCompanion:normalizeAngle(math.pi + 0.5)), "a default of pi turns it from there, got " .. tostring(rest.yaw))
    cam.origRotY = 1.0
    rest = VRCompanion:getCabRestPose(cam, 0, 0, 0.5, math.pi)
    assert(near(rest.yaw, 1.5), "the camera's own neutral yaw wins over any default, got " .. tostring(rest.yaw))
end)

test("ConsoleCommandIsAddedAtMapLoadAndRemovedAtMapUnload", function()
    setup()
    local registered, removed = {}, {}
    _G.addConsoleCommand = function(name, description, method, target) registered[name] = { method = method, target = target } end
    _G.removeConsoleCommand = function(name) removed[name] = true end
    _G.g_currentMission = { hud = {} }

    VRCompanion:loadMap("mapUS")
    local command = registered["vrSeatOffset"]
    assert(command ~= nil, "vrSeatOffset is registered at map load")
    assert(type(command.target[command.method]) == "function", "the engine finds the handler on the target by name")
    assert(removed["vrSeatOffset"] == nil, "and is not removed yet")

    VRCompanion:deleteMap()
    assert(removed["vrSeatOffset"] == true, "vrSeatOffset is removed at map unload")
    _G.addConsoleCommand, _G.removeConsoleCommand, _G.g_currentMission = nil, nil, nil
end)

local ROOT = 900
local function pointVehicle(veh, yaw)
    veh.rootNode = ROOT
    nodeOf(ROOT).t = { 50, 0, 80 }
    nodeOf(ROOT).yaw = yaw
end

test("SeatOffsetFollowsTheVehiclesHeadingNotTheMapsDirection", function()
    setup()
    local cam = trackedCab(0)
    local veh = makeVehicle(freshModel(), { cam })
    pointVehicle(veh, 0)
    enter(veh)
    local base = viewAfterUpdate(cam)

    -- the vehicle's own frame: forward 0.1 is +Z, up 0.2 is +Y, right 0.3 is -X
    VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
    assertMoved(viewAfterUpdate(cam), base, -0.3, 0.2, 0.1, "the vehicle pointing along the map's +Z")

    for _, heading in ipairs({ math.pi * 0.5, math.pi, -0.7 }) do
        pointVehicle(veh, heading)
        local turnedBase = (function()
            VRCompanion:consoleCommandSeatOffset("reset")
            local v = viewAfterUpdate(cam)
            VRCompanion:consoleCommandSeatOffset("0.1", "0.2", "0.3")
            return v
        end)()
        local wx, wy, wz = VRCompanion:rotateVectorByYaw(-0.3, 0.2, 0.1, heading)
        assertMoved(viewAfterUpdate(cam), turnedBase, wx, wy, wz, string.format("the vehicle heading %.2f", heading))
    end
end)

print(string.format("Seat Offset Tests: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d Seat Offset test(s) failed", failed))
end
