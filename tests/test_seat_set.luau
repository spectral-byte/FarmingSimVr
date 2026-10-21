-- ====================================================================
-- Test Suite for the Seat Set Action (spec #166, ticket #170): in a cab the player leans to where they
-- want their eyes and presses it; the Seat Offset takes up how far the head was from where it would rest
-- and the view recenters as the Recenter Action does. Drives the whole Companion Mod through what the
-- engine calls (the in-cab camera update, the action callback, the action event registration) over a mock
-- scene graph, and asserts on what the engine would see: where the engine camera, the eye camera and the
-- audio listener are, the bridge calls, the saved settings and the blinking warnings.
-- Axes: forward, up and right, in the in-cab camera's parent space. There forward is +Z and the vehicle's
-- left is +X (the engine's vehicles are modelled that way). The head pose is in the tracking space, where
-- -Z is forward and +X is right; a cab looking out of the windscreen has its head tracking node turned a
-- half turn (origRotY pi), so a lean (x, y, z) in the tracking space is (-x, y, -z) in the cab.
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

local function near(a, b) return math.abs(a - b) < 0.0005 end

-- --------------------------------------------------------------------
-- Mock scene: nodes with a parent, a local translation and a yaw (as in test_seat_offset.luau)
-- --------------------------------------------------------------------
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
        local n = nodeOf(from)
        local rx, ry, rz = VRCompanion:rotateVectorByYaw(x, y, z, n.yaw)
        return n.t[1] + rx, n.t[2] + ry, n.t[3] + rz
    end
    _G.localRotationToLocal = function(from, to, rx, ry, rz) return rx, ry + nodeOf(from).yaw, rz end
end

-- A bridge whose head pose is the table `state.pose` (nil: tracking lost). It records the calls; a recenter puts the head
-- pose back at the tracking space's origin, as re-zeroing the tracking space does.
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

local state
local saves, motionControllerResets, warnings

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
    VRCompanion.seatOffsets = {}
    VRCompanion.activePlayerCamera = nil
end

local function setup(pose)
    installScene()
    state = { pose = pose or poseAt(0, 0, 0) }
    installBridge(state)
    resetCompanion()
    saves, motionControllerResets, warnings = 0, 0, {}
    -- The save entry point the other settings use, and the Motion Controller references a recenter resets
    VRCompanion.saveConfiguration = function() saves = saves + 1 return true end
    VRCompanion.resetMotionControllerMovement = function() motionControllerResets = motionControllerResets + 1 end
    _G.g_currentMission = nil
    _G.g_cameraManager = nil
end

-- A vehicle of model `config` whose cameras are `cams` (the in-cab camera first, as the game lists them), with camera
-- `index` active
local function makeVehicle(config, cams, index)
    local veh = { configFileName = config, spec_enterable = { cameras = cams, camIndex = index or 1 } }
    veh.getActiveCamera = function(self) return self.spec_enterable.cameras[self.spec_enterable.camIndex] end
    for _, cam in ipairs(cams) do cam.vehicle = veh end
    return veh
end

local modelCount = 0
local function freshModel()
    modelCount = modelCount + 1
    return string.format("data/vehicles/make/seatset%d.xml", modelCount)
end

local nextCameraNode = 100
-- A fallback in-cab camera, with no head tracking node, with its eye point at (0.3, 1.4, -0.2)
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

-- An in-cab camera with a head tracking node at (0, 1.6, 0.2), looking out of the windscreen (origRotY pi), turned by
-- `lookYaw` from there by the mouse or a Snap Turn
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

local function newMission(veh)
    return {
        controlledVehicle = veh,
        showBlinkingWarning = function(self, text, ms) warnings[#warnings + 1] = text end,
    }
end

local function enter(veh)
    _G.g_currentMission = newMission(veh)
    VRCompanion:onEnterVehicle(veh, true, nil)
end

local function translationOf(id) local t = nodeOf(id).t return { x = t[1], y = t[2], z = t[3] } end

-- The three places the head-tracked view puts the head: the engine camera node, the eye camera and the Cyclopean Audio
-- Listener Node
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

-- What the console command prints for the Seat Offset of the cab being shown
local function offsetText() return VRCompanion:consoleCommandSeatOffset() end

print("Running Seat Set Action Tests...")

-- Slice 1: a press absorbs the lean, and the view rests where the head was (head tracking node path)
test("PressAbsorbsALeanIntoTheSeatOffsetAndTheViewRestsWhereTheHeadWas", function()
    setup()
    local cam = trackedCab(0)
    enter(makeVehicle(freshModel(), { cam }))
    local base = viewAfterUpdate(cam)

    state.pose = poseAt(0.1, 0.05, -0.2)  -- 0.1 right, 0.05 up, 0.2 forward
    local leaned = viewAfterUpdate(cam)
    VRCompanion:onSeatSetAction()
    local seated = viewAfterUpdate(cam)

    assertMoved(leaned, base, -0.1, 0.05, 0.2, "the lean")
    assertMoved(seated, leaned, 0, 0, 0, "after the press the head rests where it was")
    local text = offsetText()
    assert(string.find(text, "forward 0.200, up 0.050, right 0.100", 1, true), "the lean is the offset, got: " .. tostring(text))
end)

-- The press at a lean in a cab built by `makeCam`: the view at the lean, then at rest after the press
local function leanAndPress(makeCam, pose, lookYaw)
    setup()
    local cam = makeCam(lookYaw)
    enter(makeVehicle(freshModel(), { cam }))
    local base = viewAfterUpdate(cam)
    state.pose = pose
    local leaned = viewAfterUpdate(cam)
    VRCompanion:onSeatSetAction()
    return cam, base, leaned, viewAfterUpdate(cam)
end

-- Slice 2: the fallback path, with no head tracking node, takes up the lean as well
test("PressAbsorbsALeanInTheFallbackCabPathToo", function()
    local _, base, leaned, seated = leanAndPress(fallbackCab, poseAt(-0.15, 0.08, 0.1))
    assertMoved(leaned, base, 0.15, 0.08, -0.1, "the lean")
    assertMoved(seated, leaned, 0, 0, 0, "after the press the head rests where it was")
    assert(string.find(offsetText(), "forward -0.100, up 0.080, right -0.150", 1, true), "got: " .. tostring(offsetText()))
end)

-- Slice 3: a lean made while looking to the side lands in the right place in the cab
test("PressAbsorbsALeanMadeWhileLookingToTheSideWhereItIsInTheCab", function()
    -- looking a quarter turn from the windscreen, the tracking space's forward (-Z) points along the cab's +X: a lean of 0.2 forward
    -- and 0.1 right in the tracking space is (0.2, 0, 0.1) in the cab, which is 0.1 forward and 0.2 to the cab's left (right -0.2)
    local _, base, leaned, seated = leanAndPress(trackedCab, poseAt(0.1, 0.05, -0.2), math.pi * 0.5)
    assertMoved(leaned, base, 0.2, 0.05, 0.1, "the lean in the cab")
    assertMoved(seated, leaned, 0, 0, 0, "after the press the head rests where it was")
    assert(string.find(offsetText(), "forward 0.100, up 0.050, right -0.200", 1, true), "got: " .. tostring(offsetText()))
end)

test("PressAddsTheLeanToTheSeatOffsetAlreadySet", function()
    setup()
    local cam = trackedCab(0)
    enter(makeVehicle(freshModel(), { cam }))
    VRCompanion:consoleCommandSeatOffset("0.1", "-0.1", "0.2")
    local seated = viewAfterUpdate(cam)

    state.pose = poseAt(0.05, 0.1, -0.1)  -- 0.05 right, 0.1 up, 0.1 forward of where the head rests
    local leaned = viewAfterUpdate(cam)
    VRCompanion:onSeatSetAction()

    assertMoved(leaned, seated, -0.05, 0.1, 0.1, "the lean from the seat")
    assertMoved(viewAfterUpdate(cam), leaned, 0, 0, 0, "after the press the head rests where it was")
    assert(string.find(offsetText(), "forward 0.200, up 0.000, right 0.250", 1, true), "got: " .. tostring(offsetText()))
end)

-- Slice 4: what a press does besides the offset
local function pressAt(mode)
    setup()
    local cam = trackedCab(0)
    enter(makeVehicle(freshModel(), { cam }))
    VRCompanion.recenterVrTrackingSpace = (mode == "Both")
    viewAfterUpdate(cam)
    state.pose = poseAt(0.1, 0.05, -0.2)
    viewAfterUpdate(cam)
    local flushes = state.flushes
    VRCompanion:onSeatSetAction()
    return cam, flushes
end

for _, mode in ipairs({ "Both", "VehicleOnly" }) do
    test("PressRecentersTheBridgeResetsTheMotionControllersFlushesAndSavesIn" .. mode .. "Mode", function()
        local cam, flushes = pressAt(mode)
        assert(state.recenters == 1, "the tracking space is re-zeroed once, even in VehicleOnly mode, got " .. state.recenters)
        assert(motionControllerResets == 1, "the Motion Controller movement references are reset, got " .. motionControllerResets)
        assert(state.flushes == flushes + 1, "one Discontinuity Flush, got " .. (state.flushes - flushes))
        assert(saves == 1, "the settings are saved once, got " .. saves)
        assert(#warnings == 0, "an ordinary press shows nothing, got: " .. tostring(warnings[1]))
    end)
end

-- Slice 5: past the limit the offset stops at it, and a short blinking warning names the axes that stopped
test("PressPastTheLimitStopsAtTheLimitAndWarnsNamingOnlyTheClampedAxes", function()
    setup()
    local cam = trackedCab(0)
    enter(makeVehicle(freshModel(), { cam }))
    local base = viewAfterUpdate(cam)
    state.pose = poseAt(0.7, 0.1, -0.2)  -- 0.7 right (past the limit), 0.1 up, 0.2 forward
    VRCompanion:onSeatSetAction()

    assert(string.find(offsetText(), "forward 0.200, up 0.100, right 0.500", 1, true), "got: " .. tostring(offsetText()))
    assertMoved(viewAfterUpdate(cam), base, -0.5, 0.1, 0.2, "the head rests as near the lean as it can")
    assert(#warnings == 1, "one warning, got " .. #warnings)
    assert(string.find(warnings[1], "right", 1, true), "the warning names the right axis, got: " .. warnings[1])
    assert(string.find(warnings[1], "forward", 1, true) == nil and string.find(warnings[1], "up", 1, true) == nil,
        "and only that one, got: " .. warnings[1])
    assert(saves == 1 and state.recenters == 1, "the press still saves and recenters")
end)

test("PressPastTheLimitOnSeveralAxesNamesThemAll", function()
    setup()
    local cam = trackedCab(0)
    enter(makeVehicle(freshModel(), { cam }))
    VRCompanion:consoleCommandSeatOffset("0.4", "-0.4", "0")
    state.pose = poseAt(0, -0.3, -0.3)  -- 0.3 more forward and 0.3 further down: forward 0.7 and up -0.7 are past the limit
    viewAfterUpdate(cam)
    VRCompanion:onSeatSetAction()
    assert(string.find(offsetText(), "forward 0.500, up -0.500, right 0.000", 1, true), "got: " .. tostring(offsetText()))
    assert(#warnings == 1 and string.find(warnings[1], "forward", 1, true) and string.find(warnings[1], "up", 1, true)
        and string.find(warnings[1], "right", 1, true) == nil, "forward and up stopped, got: " .. tostring(warnings[1]))
end)

-- Slice 6: outside an in-cab camera in the head-tracked view with no menu shown, a press does nothing
local function assertPressDoesNothing(label)
    local flushes, savesBefore = state.flushes, saves
    VRCompanion:onSeatSetAction()
    assert(state.recenters == 0, label .. ": the tracking space is not re-zeroed")
    assert(motionControllerResets == 0, label .. ": the Motion Controller references are left alone")
    assert(state.flushes == flushes, label .. ": no Discontinuity Flush")
    assert(saves == savesBefore, label .. ": nothing saved")
    assert(#warnings == 0, label .. ": nothing shown, got: " .. tostring(warnings[1]))
end

-- An in-cab camera with a lean, a Seat Offset of (0.1, 0, 0) to see it unchanged
local function leaningInCab()
    setup(poseAt(0.1, 0.05, -0.2))
    local cam = trackedCab(0)
    local veh = makeVehicle(freshModel(), { cam })
    enter(veh)
    VRCompanion:consoleCommandSeatOffset("0.1", "0", "0")
    viewAfterUpdate(cam)
    return cam, veh
end

local function assertOffsetUnchanged(label)
    assert(string.find(offsetText(), "forward 0.100, up 0.000, right 0.000", 1, true), label .. ": the offset is unchanged, got: " .. tostring(offsetText()))
end

test("PressOnFootDoesNothing", function()
    local _, veh = leaningInCab()
    VRCompanion:onLeaveVehicle(veh)
    _G.g_currentMission = newMission(nil)
    assertPressDoesNothing("on foot")
    _G.g_currentMission = newMission(veh)
    assertOffsetUnchanged("back in the cab")
end)

test("PressInAnOutsideCameraDoesNothing", function()
    setup(poseAt(0.1, 0.05, -0.2))
    local cabCam = trackedCab(0)
    nextCameraNode = nextCameraNode + 1
    local outsideCam = { isInside = false, cameraNode = nextCameraNode, rotY = 0, rotX = 0 }
    local veh = makeVehicle(freshModel(), { cabCam, outsideCam })
    enter(veh)
    VRCompanion:consoleCommandSeatOffset("0.1", "0", "0")
    veh.spec_enterable.camIndex = 2
    assertPressDoesNothing("in an Outside Camera")
    veh.spec_enterable.camIndex = 1
    assertOffsetUnchanged("back in the cab")
end)

test("PressIn2DFloatingWindowModeDoesNothing", function()
    leaningInCab()
    VRCompanion.is2DViewActive = true
    assertPressDoesNothing("in 2D Floating Window Mode")
    VRCompanion.is2DViewActive = false
    assertOffsetUnchanged("back in the stereoscopic view")
end)

test("PressWhileAMenuIsShownDoesNothing", function()
    leaningInCab()
    VRCompanion.isMenuVisible = true
    assertPressDoesNothing("with the menu quad shown")
    VRCompanion.isMenuVisible = false
    VRCompanion.menuTakesInput = true
    assertPressDoesNothing("with a menu taking the input")
    VRCompanion.menuTakesInput = false
    assertOffsetUnchanged("with the menu gone")
end)

test("PressWhileTheGameShowsACameraOfItsOwnDoesNothing", function()
    leaningInCab()
    _G.g_cameraManager = { getActiveCamera = function() return 424242 end }  -- the shop's camera, say
    assertPressDoesNothing("with another camera shown")
    _G.g_cameraManager = nil
    assertOffsetUnchanged("with the cab camera back")
end)

test("PressWithHeadTrackingLostDoesNothing", function()
    leaningInCab()
    state.pose = nil
    assertPressDoesNothing("with tracking lost")
    assertOffsetUnchanged("with tracking back")
end)

-- Slice 7: the action event is registered like the other VR actions, kept active, and re-registered when the engine drops it
test("TheSeatSetActionEventIsRegisteredActiveAndKeptThere", function()
    local registered, texts, active, visible = {}, {}, {}, {}
    local nextEventId = 200
    _G.g_inputBinding = {
        events = {},
        registerActionEvent = function(self, actionName, target, callback, triggerUp, triggerDown, triggerAlways, startActive)
            nextEventId = nextEventId + 1
            local event = { id = nextEventId, actionName = actionName, target = target, callback = callback,
                triggerUp = triggerUp, triggerDown = triggerDown, startActive = startActive }
            registered[actionName] = event
            self.events[event.id] = event
            return true, event.id
        end,
        setActionEventTextPriority = function() end,
        setActionEventActive = function(self, id, isActive) active[id] = isActive end,
        setActionEventTextVisibility = function(self, id, isVisible) visible[id] = isVisible end,
        setActionEventText = function(self, id, text) texts[id] = text end,
    }
    _G.g_i18n = { getText = function(self, key) return key == "input_VR_SET_SEAT" and "Set VR Seat Position" or key end }
    _G.g_currentMission = nil
    VRCompanion.actionEventsContextName = nil
    VRCompanion.seatSetActionEventId = nil

    VRCompanion:registerActionEvents(true)
    local event = registered["VR_SET_SEAT"]
    assert(event ~= nil, "VR_SET_SEAT must be registered with g_inputBinding")
    assert(event.target == VRCompanion and event.callback == VRCompanion.onSeatSetAction, "its callback is onSeatSetAction")
    assert(event.triggerDown == true and event.triggerUp == false, "a single press fires it")
    assert(event.startActive == true and active[event.id] == true, "and it is active")
    assert(visible[event.id] == true and texts[event.id] == "Set VR Seat Position", "its text is shown, from the l10n")

    -- The periodic check leaves it alone, and puts it back when the engine dropped it
    local before = nextEventId
    VRCompanion:registerActionEvents(false)
    assert(nextEventId == before, "nothing is re-registered while the event is in place")
    _G.g_inputBinding.events = {}
    VRCompanion:registerActionEvents(false)
    assert(registered["VR_SET_SEAT"].id > event.id and active[registered["VR_SET_SEAT"].id] == true, "a dropped event is registered again")

    _G.g_inputBinding, _G.g_i18n = nil, nil
end)

print(string.format("Seat Set Action Tests: %d passed, %d failed", passed, failed))
if failed > 0 then
    error(string.format("%d Seat Set Action test(s) failed", failed))
end
