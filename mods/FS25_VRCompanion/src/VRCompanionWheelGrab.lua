-- ====================================================================
-- FS25 VR Companion Mod: Wheel Grab
--
-- Lets the player steer by taking hold of the Steering Wheel with a Motion Controller (spec #89, ADR-0027).
-- In the in-cab camera of the vehicle being driven, squeezing the grip with the hand's grip position inside a
-- ring around the Steering Wheel's centre takes hold of it; turning the hand turns the Steering Wheel by the
-- same angle, and letting go of the grip lets go. The vehicle is steered the way a Wheel Peripheral steers it:
-- every frame while the Steering Wheel is held, vehicle:setSteeringInput(value, true, InputDevice.CATEGORY.WHEEL)
-- is called, and nothing else is touched (the vehicle's own steering value is never written, so the game keeps
-- its steering rate limit, its end stops and its multiplayer sync). When nothing is held nothing is sent, and the
-- game's own steering back centres the Steering Wheel.
--
-- TERMS (CONTEXT.md): the Steering Wheel is the one modelled in the cab, a Wheel Peripheral is a game input device
-- connected to the PC. A hand "holds" the Steering Wheel from the grab until the grip is let go.
--
-- THE STEERING WHEEL: the node the game turns, found the way the game finds it (steeringWheelOf): the turned seat's
-- or the crab steering mode's own Steering Wheel when the vehicle has one, otherwise the drivable's. The game sets that node's
-- rotation to (0, angle, 0) every frame, so it turns about its LOCAL Y axis, whatever tilts it is in the node above.
-- The angle is rotatedTime * (the in-cab rotation) * (the node direction: -1 on a turned seat), the same product the
-- game uses, so the ACTUAL angle is read from those fields and never from the node's Euler angles (which wrap at
-- half a turn while a vanilla Steering Wheel turns up to one and a half turns each way). The node is used for geometry only.
--
-- TURNING: a hand's angle about the axis is measured in the plane of the Steering Wheel, in the frame of the node ABOVE
-- the Steering Wheel node (which does not turn with it), from the hand's grip position. Each frame the target angle moves by the
-- AVERAGE of the angle changes of the hands that held on the update before and still hold (a hand that grabs or lets go on an
-- update is not counted on it, so it neither halves nor adds to the others' turn), starting from the Steering Wheel's actual angle at the first grab, and
-- is clamped to full lock each way (the angles at the vehicle's minimum and maximum rotated time) with no slack kept
-- past the stop: turning back moves the Steering Wheel at once. The steering value sent is the inverse of what the game does
-- with a value (Wheels.lua updateVehiclePhysics), so the node comes to rest at the target:
--   rotatedTime = angle / (inCabRotation * direction)
--   value = -rotatedTime / (maxRotTime * steeringDirection)  for rotatedTime >= 0
--   value =  rotatedTime / (minRotTime * steeringDirection)  for rotatedTime < 0
-- (steeringDirection is vehicle:getSteeringDirection(), read live: a turned seat flips both it and the node direction.)
-- The game steers toward the value at a capped rate, so the node lags a fast hand; it is the node's actual rotation the
-- hand has to follow, never the target.
--
-- THE PINNED HAND: at the grab the hand's grip pose (position and orientation) is stored in the LOCAL frame of the Steering Wheel
-- node. While the hand holds, getWheelGrabHandPose(hand) carries it back to the world through the node's ACTUAL rotation
-- (not the target), so the Virtual Hand turns with the Steering Wheel exactly as the game turns it, the steering rate lag shows as
-- the hand staying on the Steering Wheel, and no angle is kept or wrapped. VRCompanionVirtualHands.lua draws the hand there. A hand
-- whose real grip position is more than WHEEL_GRAB_PULL_AWAY_DISTANCE from its pinned point lets go.
--
-- HAPTICS: one short pulse in a hand when it grabs, one firmer pulse in every holding hand when the target reaches full
-- lock (again only after the target has left the stop; a hand that grabs while another holds at the stop, or a first hand that
-- grabs with the Steering Wheel already at the stop, feels only its own grab), none on release.
--
-- AVAILABILITY (findSteeringWheel): the mod is on (`vrWheelGrab`, Motion Controllers on), no menu is open and 2D
-- Floating Window Mode is off, the player is driving the controlled vehicle from its in-cab camera (not a passenger
-- camera, not an Outside Camera), and the vehicle has a Steering Wheel and a setSteeringInput, and no hired helper is
-- driving it (as for keyboard steering). When it stops being
-- available, or the controlled vehicle changes, every hold ends as if the grip were let go (nothing is sent from the
-- next update on); so does a hand's hold when that hand can no longer be located (it lost tracking). A grip still
-- squeezed when the situation returns is not a press: it has to be let go and squeezed afresh (updateGrips), and a hand
-- that is tracked again with its grip squeezed has not squeezed afresh either.
--
-- THE GRAB: the grip counts as squeezed at VRCompanion.WHEEL_GRAB_GRIP_PRESS and as let go below
-- VRCompanion.WHEEL_GRAB_GRIP_RELEASE (hysteresis, like the menu back button). A grab starts on the update the grip is
-- SQUEEZED (an edge: a grip already squeezed when the hand reaches the ring or when Wheel Grab becomes available does
-- not grab) by a hand that is in use, with its grip position inside the ring: between WHEEL_GRAB_RING_INNER and
-- WHEEL_GRAB_RING_OUTER from the Steering Wheel's axis and within WHEEL_GRAB_RING_HALF_THICKNESS of its plane. The ring is the
-- same on every vehicle: it is not measured from the Steering Wheel's mesh.
--
-- THE HANDS WIN: while the Steering Wheel is held, every other call of vehicle:setSteeringInput is dropped (the keyboard,
-- a gamepad, a Wheel Peripheral, the game's phone tilt steering); only Wheel Grab's own call passes. The function is wrapped
-- on the Drivable class and on the controlled vehicle's own copy (the engine copies it onto each vehicle), see the section
-- "The hands win over other steering" below. If the function is missing or has another shape, Wheel Grab is unavailable, one
-- warning is logged and the steering of other devices is untouched; Wheel Grab does the same when its own call raises on
-- WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP updates in a row. vrWheelGrabDiagnostics reports the wrap.
--
-- PUBLIC SURFACE
--   VRCompanion.wheelGrabEnabled           boolean, default true, saved (`vrWheelGrab on|off`)
--   VRCompanion.WHEEL_GRAB_GRIP_PRESS / _RELEASE            grip value that grabs / lets go (0..1)
--   VRCompanion.WHEEL_GRAB_RING_INNER / _OUTER              the ring's radii from the Steering Wheel's axis (m)
--   VRCompanion.WHEEL_GRAB_RING_HALF_THICKNESS              how far from the Steering Wheel's plane the ring reaches (m)
--   VRCompanion:updateWheelGrab(dt)         once per update, called by updateMotionControllers after the hands are polled
--   VRCompanion:isHandHoldingSteeringWheel(hand) -> boolean   the hand holds the Steering Wheel (as of the last update)
--   VRCompanion:isSteeringWheelHeld() -> boolean              some hand does
--   VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE               real grip position this far (m) from its pin lets go
--   VRCompanion.WHEEL_GRAB_GRAB_PULSE_MS / _STRENGTH        the pulse in the hand that grabs
--   VRCompanion.WHEEL_GRAB_FULL_LOCK_PULSE_MS / _STRENGTH   the pulse in every holding hand at full lock
--   VRCompanion:getWheelGrabHandPose(hand) -> x, y, z, qx, qy, qz, qw   where a holding hand's Virtual Hand is drawn (world),
--                                                             or nil when the hand does not hold
--   VRCompanion:isWheelGrabAvailable() -> boolean, reason     whether a grab is possible now (as of the last update)
--   VRCompanion:setWheelGrabEnabled(enabled, save)
--   VRCompanion:saveWheelGrabSetting(xmlFile, xml) / loadWheelGrabSetting(xmlFile, xml)   settings chain
--   VRCompanion:getWheelGrabDiagnostics() -> string          the state of Wheel Grab, one line
--   VRCompanion:registerWheelGrabConsoleCommands() / resetWheelGrab()   map load / unload
--   Console: vrWheelGrab [on|off], vrWheelGrabDiagnostics
--
-- Hooks into the rest of the mod (each a single guarded call):
--   MotionControllers  updateMotionControllers -> updateWheelGrab(dt), after the hands are polled; the saved setting, the
--                      console commands and the reset chain through the Motion Controller functions of the same names
--   VirtualHands       updateHand asks getWheelGrabHandPose(hand): a holding hand is drawn there, and stays shown however
--                      long it is held still
-- ====================================================================

VRCompanion = VRCompanion or {}

-- vrWheelGrab on|off: saved, default on. Wheel Grab also needs Motion Controllers on.
VRCompanion.DEFAULT_WHEEL_GRAB_ENABLED = true
VRCompanion.wheelGrabEnabled = VRCompanion.DEFAULT_WHEEL_GRAB_ENABLED

-- The grip counts as squeezed at PRESS and as let go below RELEASE (hysteresis against a grip that rests half-way,
-- the same as the menu back button's 0.6 and 0.3).
VRCompanion.WHEEL_GRAB_GRIP_PRESS = 0.6
VRCompanion.WHEEL_GRAB_GRIP_RELEASE = 0.3

-- THE RIM RING, in metres, the same for every vehicle: the grip position of a squeezing hand must be this far from the
-- Steering Wheel's axis (measured in its plane) and this close to its plane. Vanilla Steering Wheels are about 0.2 to 0.25 m
-- across the rim from the centre, and in VR a hand is not precise, so the ring is generous: 10 to 25 cm, which leaves out
-- a hand resting on the hub and one resting on the dashboard behind a small Steering Wheel. The plane is the node's origin
-- plane, which is not where the rim visibly is: in the first headset test, hands gripping the rim were consistently 8 to
-- 11 cm from it (always on the same side), so the ring reaches 15 cm either side. Tune in the headset with
-- `vrWheelGrabDiagnostics` and the log line written for every squeeze.
VRCompanion.WHEEL_GRAB_RING_INNER = 0.10
VRCompanion.WHEEL_GRAB_RING_OUTER = 0.25
VRCompanion.WHEEL_GRAB_RING_HALF_THICKNESS = 0.15

-- PULL-AWAY: a holding hand's real grip position this far (m) from the point its Virtual Hand is pinned to ends the
-- grab, as if the grip were let go: the player cannot steer from somewhere they obviously could not reach. The pinned hand
-- lags the real one while the game's steering catches up with a quick turn; 20 cm let go too easily in the first headset
-- test, so it is 30 cm, which is still short of walking away from the cab. Tune in the headset: `vrWheelGrabDiagnostics`
-- reports each hand's distance to its pin.
VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE = 0.30

-- HAPTICS (milliseconds, strength 0..1): a short pulse in the hand that grabs, a firmer, longer one in every holding
-- hand when the target reaches full lock. Nothing on release and no continuous rumble. For scale, the Pointer Hand's
-- Click Point select is 25 ms at 0.25 and a click is 45 ms at 0.6 (VRCompanionMotionControllers.lua).
VRCompanion.WHEEL_GRAB_GRAB_PULSE_MS = 30
VRCompanion.WHEEL_GRAB_GRAB_PULSE_STRENGTH = 0.4
VRCompanion.WHEEL_GRAB_FULL_LOCK_PULSE_MS = 70
VRCompanion.WHEEL_GRAB_FULL_LOCK_PULSE_STRENGTH = 0.8

-- Wheel Grab's own setSteeringInput call raising on this many updates in a row makes it give up for the map, with one
-- warning, exactly as for a missing or reshaped function. Three: a one-off failure is retried the next update, while a
-- call that is broken fails on every one.
VRCompanion.WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP = 3

local HANDS = { 0, 1 }
local HAND_NAMES = { [0] = "left", [1] = "right" }

local function newState()
    return {
        hands = {
            [0] = { gripDown = false, pressed = false, holding = false, angle = 0, tracked = false },
            [1] = { gripDown = false, pressed = false, holding = false, angle = 0, tracked = false },
        },
        vehicle = nil,       -- the vehicle the Steering Wheel is held in
        target = 0,          -- the target angle of the Steering Wheel (radians, unwrapped) while held
        sending = false,     -- Wheel Grab's own setSteeringInput call is running
        reason = "not updated yet",  -- why Wheel Grab is unavailable (nil when it is available), as of the last update
        reported = nil,      -- the availability text last logged
        scratch = nil,       -- reused table for a hand's grip pose in the world
        sent = nil,          -- the steering value sent in the last update that sent one, nil while nothing is held
        atFullLock = false,  -- the target is at a steering stop
        sendFailures = 0,    -- updates in a row on which Wheel Grab's own setSteeringInput call raised
    }
end

local state = newState()

local engine = VRCompanion.getEngineGlobal  -- VRCompanionCore.lua, loaded before this file

local function isNode(node)
    return node ~= nil and node ~= 0
end

-- ---- The hands win over other steering (ADR-0027) --------------------------------------------------------------------
--
-- Every steering device calls vehicle:setSteeringInput(value, isAnalog, deviceCategory): the keyboard, a gamepad, a Wheel
-- Peripheral and the game's own phone tilt steering and steering slider. While the Steering Wheel is held, Wheel Grab drops
-- every one of those calls; its own call (made with `state.sending` set) is the only one that passes. The game gives each
-- vehicle its OWN COPY of the function when it builds the vehicle types (Vehicle:load -> copyTypeFunctionsInto, after the
-- mod's files are loaded), and the steering input calls the copy, so there are two wraps:
--   * the class (Drivable.setSteeringInput), wrapped as the files load (or as soon as Drivable is there), so that
--     every vehicle type the game builds afterwards copies the wrapper;
--   * the controlled vehicle's own copy, wrapped for as long as the Steering Wheel is held in it, for a vehicle whose
--     copy was taken from the function before the class was wrapped.
-- Both pass every call through unchanged whenever nothing is held. Like Interactive Control's wraps, a restore only puts
-- back a function when it is still the wrapper put there: a wrap another mod added later is never undone.
-- When the function is missing or not shaped as the wrap expects, Wheel Grab gives up with ONE warning: it is unavailable
-- and the steering of every other device is left untouched.

local STEERING_FUNCTION = "setSteeringInput"
local STEERING_PARAMETERS = 4  -- (vehicle, inputValue, isAnalog, deviceCategory)

local wrap = {
    class = nil,         -- the Drivable table the class wrap was put on
    classOriginal = nil, -- what Drivable.setSteeringInput was before
    classWrapper = nil,  -- what the wrap put there
    instance = nil,      -- { vehicle, original, hadOwn, wrapper } of the controlled vehicle's own copy while it is wrapped
    problem = nil,       -- why Wheel Grab gave up, once it did (the warning is logged then and never again)
}

-- Whether the vehicle's Steering Wheel is held, which is when the wraps drop calls. The wrap passes everything when
-- Wheel Grab or Motion Controllers are switched off, even before the next update lets go of the Steering Wheel.
local function isHeldIn(vehicle)
    return vehicle ~= nil and state.vehicle == vehicle and (state.hands[0].holding or state.hands[1].holding)
        and not state.sending and VRCompanion.wheelGrabEnabled ~= false and VRCompanion.motionControllersEnabled ~= false
end

local function makeWrapper(original)
    return function(vehicle, inputValue, isAnalog, deviceCategory, ...)
        if isHeldIn(vehicle) then return end
        return original(vehicle, inputValue, isAnalog, deviceCategory, ...)
    end
end

-- How many parameters a Lua function declares, or nil when it takes any number or the runtime cannot tell.
local function parameterCount(fn)
    local library = _G and _G.debug
    if type(library) ~= "table" then return nil end
    if type(library.info) == "function" then
        local ok, count, isVararg = pcall(library.info, fn, "a")
        if ok and type(count) == "number" then return (not isVararg) and count or nil end
    end
    if type(library.getinfo) == "function" then
        local ok, info = pcall(library.getinfo, fn, "u")
        if ok and type(info) == "table" and type(info.nparams) == "number" then
            return (not info.isvararg) and info.nparams or nil
        end
    end
    return nil
end

-- Why a steering function is not what the wrap expects, or nil when it is.
local function shapeProblem(fn)
    if fn == nil then return "it is missing" end
    if type(fn) ~= "function" then return "it is not a function" end
    local count = parameterCount(fn)
    if count ~= nil and count < STEERING_PARAMETERS then
        return string.format("it takes %d parameters, not (vehicle, value, isAnalog, deviceCategory)", count)
    end
    return nil
end

-- Wraps the Drivable class function once per class table. A class whose function is not as expected is left alone
-- (steeringInputProblem reports it when a vehicle needs it).
local function wrapClass()
    local class = engine("Drivable")
    if type(class) ~= "table" or class == wrap.class then return end
    local original = class[STEERING_FUNCTION]
    if shapeProblem(original) ~= nil then return end
    wrap.class, wrap.classOriginal, wrap.classWrapper = class, original, makeWrapper(original)
    class[STEERING_FUNCTION] = wrap.classWrapper
end

-- Gives the vehicle its own copy back, unless something else has replaced the wrapper since.
local function unwrapInstance()
    local current = wrap.instance
    wrap.instance = nil
    if current ~= nil and rawget(current.vehicle, STEERING_FUNCTION) == current.wrapper then
        current.vehicle[STEERING_FUNCTION] = current.hadOwn and current.original or nil
    end
end

-- Wraps the controlled vehicle's own copy of the function (replacing a wrap of an earlier vehicle).
local function wrapInstance(vehicle)
    local current = wrap.instance
    if current ~= nil and current.vehicle == vehicle and rawget(vehicle, STEERING_FUNCTION) == current.wrapper then return end
    if current ~= nil then unwrapInstance() end
    local original = vehicle[STEERING_FUNCTION]
    if type(original) ~= "function" or original == wrap.classWrapper then return end  -- the class wrap reaches it
    local wrapper = makeWrapper(original)
    wrap.instance = { vehicle = vehicle, original = original, hadOwn = rawget(vehicle, STEERING_FUNCTION) ~= nil, wrapper = wrapper }
    vehicle[STEERING_FUNCTION] = wrapper
end

-- Lifts both wraps. The class function is only put back when it is still the wrapper.
local function liftWraps()
    unwrapInstance()
    local class = wrap.class
    if class ~= nil and class[STEERING_FUNCTION] == wrap.classWrapper then
        class[STEERING_FUNCTION] = wrap.classOriginal
    end
    wrap.class, wrap.classOriginal, wrap.classWrapper = nil, nil, nil
end

-- Lifts both wraps and forgets that Wheel Grab gave up (map unload).
local function unwrapAll()
    liftWraps()
    wrap.problem = nil
end

-- Wheel Grab cannot rely on the vehicle's steering function: it logs ONE warning, never steers again this map (it is
-- unavailable, see steeringInputProblem), and lifts its wraps at once, so that the steering of every other device is
-- exactly the game's.
local function giveUp(problem)
    wrap.problem = problem
    liftWraps()
    print("[FS25_VR] [Warning] Wheel Grab disabled: " .. problem .. ". Steering from the keyboard, a gamepad or a "
        .. "Wheel Peripheral is unchanged.")
end

-- Why Wheel Grab cannot rely on the vehicle's steering function, or nil. The first time it cannot, one warning is logged
-- and Wheel Grab stays unavailable for the rest of the map.
local function steeringInputProblem(vehicle)
    if wrap.problem ~= nil then return wrap.problem end
    wrapClass()
    local class = engine("Drivable")
    local problem = nil
    if type(class) ~= "table" then
        problem = "Drivable is missing"
    else
        local classProblem = shapeProblem(wrap.classOriginal or class[STEERING_FUNCTION])
        if classProblem ~= nil then problem = "Drivable." .. STEERING_FUNCTION .. ": " .. classProblem end
    end
    if problem == nil then
        local instanceProblem = shapeProblem(vehicle[STEERING_FUNCTION])
        if instanceProblem ~= nil then problem = "the vehicle's " .. STEERING_FUNCTION .. ": " .. instanceProblem end
    end
    if problem ~= nil then giveUp(problem) end
    return problem
end

wrapClass()  -- Drivable is there as the files load: the types the game builds afterwards copy the wrapper

-- ---- The Steering Wheel of a vehicle --------------------------------------------------------------------------------

-- The table the game would turn right now: { node, indoorRotation, ... } and the direction it turns the node in, or nil.
-- A turned seat and a crab steering mode may bring their own Steering Wheel (ReverseDriving / CrabSteering updateSteeringWheel).
local function steeringWheelOf(vehicle)
    local direction = 1
    local wheel = vehicle.spec_drivable and vehicle.spec_drivable.steeringWheel or nil
    local reverse = vehicle.spec_reverseDriving
    if reverse ~= nil and reverse.isReverseDriving then
        direction = -1
        if reverse.steeringWheel ~= nil then wheel = reverse.steeringWheel end
    end
    local crab = vehicle.spec_crabSteering
    if crab ~= nil and crab.hasSteeringModes and crab.steeringModes ~= nil then
        local mode = crab.steeringModes[crab.state]
        if mode ~= nil and mode.steeringWheel ~= nil then wheel = mode.steeringWheel end
    end
    if wheel == nil or not isNode(wheel.node) then return nil end
    return wheel, direction
end

-- Where the vehicle's Steering Wheel is turned to, in radians and unwrapped: the product the game sets the node's
-- rotation to.
local function actualAngle(vehicle, wheel, direction)
    return (vehicle.rotatedTime or 0) * (wheel.indoorRotation or 0) * direction
end

-- The angles at the steering stops, lowest first.
local function angleLimits(vehicle, wheel, direction)
    local a = (vehicle.minRotTime or 0) * (wheel.indoorRotation or 0) * direction
    local b = (vehicle.maxRotTime or 0) * (wheel.indoorRotation or 0) * direction
    if a <= b then return a, b end
    return b, a
end

-- The steering value that brings the Steering Wheel to rest at `angle`, or nil when the vehicle has no steering range
-- there (or its steering direction is changing sign, as a turned seat's does while the seat turns).
local function steeringValue(vehicle, wheel, direction, angle)
    local rotation = (wheel.indoorRotation or 0) * direction
    if rotation == 0 then return nil end
    local rotatedTime = angle / rotation
    local steeringDirection = 1
    if type(vehicle.getSteeringDirection) == "function" then
        local ok, answer = pcall(vehicle.getSteeringDirection, vehicle)
        if not ok or type(answer) ~= "number" then return nil end
        steeringDirection = answer
    end
    if math.abs(steeringDirection) < 0.05 then return nil end
    local span = rotatedTime >= 0 and (vehicle.maxRotTime or 0) or (vehicle.minRotTime or 0)
    if span == 0 then return nil end
    local value = rotatedTime / (span * steeringDirection)
    if rotatedTime >= 0 then value = -value end
    return math.max(-1, math.min(1, value))
end

-- What a vehicle answers to a yes-or-no question (a method of the game's that returns a boolean first), or nil when it has no
-- such method or it raises: only an explicit false counts as no.
local function askVehicle(vehicle, method)
    if type(vehicle[method]) ~= "function" then return nil end
    local ok, answer = pcall(vehicle[method], vehicle)
    if ok then return answer end
    return nil
end

-- The vehicle the player is driving, its Steering Wheel and the direction of the Steering Wheel node, or nil and why not. A vehicle
-- is returned whenever the player is driving one, whatever is wrong with it.
local function findSteeringWheel(self)
    if self.wheelGrabEnabled == false then return nil, nil, nil, "Wheel Grab is off" end
    if self.motionControllersEnabled == false then return nil, nil, nil, "Motion Controllers are off" end
    -- A menu or 2D Floating Window Mode takes the player out of the cab: the hands cannot be located in it either.
    if self:doesMenuTakeInput() then return nil, nil, nil, "a menu is open" end
    if self.is2DViewActive == true then return nil, nil, nil, "2D Floating Window Mode is on" end
    local vehicle = self:getControlledVehicle()
    if vehicle == nil then return nil, nil, nil, "not driving a vehicle" end
    local camera = self:getVehicleActiveCamera(vehicle)
    if camera == nil or camera.isInside ~= true or camera.isPassengerCamera == true then
        return vehicle, nil, nil, "not in the in-cab camera"
    end
    local wheel, direction = steeringWheelOf(vehicle)
    if wheel == nil then return vehicle, nil, nil, "the vehicle has no Steering Wheel" end
    if engine("InputDevice") == nil or engine("InputDevice").CATEGORY == nil or engine("InputDevice").CATEGORY.WHEEL == nil then
        return vehicle, nil, nil, "the vehicle takes no steering input"
    end
    -- The steering function must be there and shaped as the wrap expects (gives up with one warning when it is not).
    local problem = steeringInputProblem(vehicle)
    if problem ~= nil then return vehicle, nil, nil, "steering input is not supported (" .. problem .. ")" end
    -- Wheel Grab is available exactly where the keyboard could steer the vehicle: it is entered, nothing blocks the
    -- player's control and it is powered (a vehicle with its motor off does not follow any steering input).
    if askVehicle(vehicle, "getIsEntered") == false then
        return vehicle, nil, nil, "the vehicle is not entered"
    end
    -- A hired helper driving the vehicle makes Wheel Grab unavailable, just as it does the keyboard's steering (the game
    -- registers no steer action while getIsAIActive() is true). Nothing here ever touches the helper: squeezing the grip
    -- at the rim neither stops the job nor takes the Steering Wheel from it, and a hold in progress simply ends (updateWheelGrab).
    if askVehicle(vehicle, "getIsAIActive") == true then
        return vehicle, nil, nil, "a hired helper is driving"
    end
    if askVehicle(vehicle, "getIsPlayerVehicleControlAllowed") == false then
        return vehicle, nil, nil, "steering the vehicle is not allowed right now"
    end
    if askVehicle(vehicle, "getIsPowered") == false then
        return vehicle, nil, nil, "the vehicle is not powered"
    end
    return vehicle, wheel, direction, nil
end

-- ---- Hands -----------------------------------------------------------------------------------------------------------

-- Updates each hand's squeezed state with hysteresis (always, whether or not Wheel Grab is available, so a grip that is
-- already squeezed when it becomes available is not a press) and notes the updates it went down.
local function updateGrips(self)
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        local pose = self:getHandState(hand)
        local squeezed = false
        if pose.tracked == true then
            local threshold = entry.gripDown and self.WHEEL_GRAB_GRIP_RELEASE or self.WHEEL_GRAB_GRIP_PRESS
            squeezed = (pose.grip or 0) >= threshold
        end
        -- A grip already squeezed when the hand is tracked again (it lost tracking while holding) is not a press: the
        -- grip has to be let go and squeezed afresh.
        entry.pressed = squeezed and not entry.gripDown and entry.tracked
        entry.gripDown = squeezed
        entry.tracked = pose.tracked == true
    end
end

-- Where the hand's grip position is relative to the Steering Wheel: the angle about its axis (radians, in the plane of it,
-- turning the same way as the node's rotation about its local Y), the distance from the axis and the signed distance from
-- the plane. All measured in the frame of the node above the Steering Wheel node, which does not turn with it. Returns nil
-- and why when the hand is not located (an untracked grip pose, or no head placed in the world: a menu, 2D Floating
-- Window Mode, another camera, Pass-Through Mode).
local function measureHand(self, hand, wheel)
    local pose = self:getHandState(hand)
    if pose.tracked ~= true or pose.gripPoseTracked ~= true then return nil, "grip pose not tracked" end
    state.scratch = state.scratch or {}
    local ok, why = self:headRelativePoseToWorld(pose.gripPoseX, pose.gripPoseY, pose.gripPoseZ,
        pose.gripPoseQx, pose.gripPoseQy, pose.gripPoseQz, pose.gripPoseQw, state.scratch)
    if not ok then return nil, why end
    for _, name in ipairs({ "getParent", "worldToLocal", "getTranslation" }) do
        if type(engine(name)) ~= "function" then return nil, name .. " is not available" end
    end
    local parent = engine("getParent")(wheel.node)
    local x, y, z = engine("worldToLocal")(parent, state.scratch.x, state.scratch.y, state.scratch.z)
    local cx, cy, cz = engine("getTranslation")(wheel.node)
    local dx, dy, dz = x - cx, y - cy, z - cz
    return math.atan2(-dz, dx), math.sqrt(dx * dx + dz * dz), dy
end

-- How far a hand at this distance from the Steering Wheel's axis and from its plane is from the rim ring (0 inside it).
local function ringDistance(self, radial, axial)
    local along = math.max(self.WHEEL_GRAB_RING_INNER - radial, radial - self.WHEEL_GRAB_RING_OUTER, 0)
    local across = math.max(math.abs(axial) - self.WHEEL_GRAB_RING_HALF_THICKNESS, 0)
    return math.sqrt(along * along + across * across)
end

local function wrapAngle(angle)
    return (angle + math.pi) % (2 * math.pi) - math.pi
end

-- ---- The update ------------------------------------------------------------------------------------------------------

local function releaseHand(hand)
    local entry = state.hands[hand]
    entry.holding = false
    entry.pin = nil
end

-- Stores the hand's grip pose, which measureHand has just left in state.scratch (world), in the local frame of the Steering Wheel
-- node: the point the Virtual Hand is pinned to. Without the engine's node functions the hand is not pinned and is drawn
-- at its grip pose as usual.
local function pinHand(hand, wheel)
    local entry = state.hands[hand]
    entry.pin = nil
    local worldToLocal, getWorldQuaternion = engine("worldToLocal"), engine("getWorldQuaternion")
    if type(worldToLocal) ~= "function" or type(getWorldQuaternion) ~= "function" then return end
    local world = state.scratch
    local ok, lx, ly, lz, nx, ny, nz, nw = pcall(function()
        local x, y, z = worldToLocal(wheel.node, world.x, world.y, world.z)
        local qx, qy, qz, qw = getWorldQuaternion(wheel.node)
        return x, y, z, qx, qy, qz, qw
    end)
    if not ok or type(lx) ~= "number" then return end
    -- the grip's rotation in the node's frame: the inverse of the node's world rotation, then the grip's
    local qx, qy, qz, qw = VRCompanion.multiplyQuaternions(-nx, -ny, -nz, nw, world.qx, world.qy, world.qz, world.qw)
    entry.pin = { node = wheel.node, x = lx, y = ly, z = lz, qx = qx, qy = qy, qz = qz, qw = qw }
end

-- Where a pin is in the world right now, carried through the Steering Wheel node's actual rotation: x, y, z, qx, qy, qz, qw, or
-- nil when the engine cannot say.
local function pinInTheWorld(pin)
    local localToWorld, getWorldQuaternion = engine("localToWorld"), engine("getWorldQuaternion")
    if pin == nil or type(localToWorld) ~= "function" or type(getWorldQuaternion) ~= "function" then return nil end
    local ok, x, y, z, nx, ny, nz, nw = pcall(function()
        local px, py, pz = localToWorld(pin.node, pin.x, pin.y, pin.z)
        local qx, qy, qz, qw = getWorldQuaternion(pin.node)
        return px, py, pz, qx, qy, qz, qw
    end)
    if not ok or type(x) ~= "number" then return nil end
    local qx, qy, qz, qw = VRCompanion.multiplyQuaternions(nx, ny, nz, nw, pin.qx, pin.qy, pin.qz, pin.qw)
    return x, y, z, qx, qy, qz, qw
end

-- How far the hand just measured (measureHand leaves its grip position in state.scratch) is from its pinned point, or nil
-- when the hand is not pinned.
local function distanceToPin(hand)
    local x, y, z = pinInTheWorld(state.hands[hand].pin)
    if x == nil then return nil end
    local world = state.scratch
    local dx, dy, dz = world.x - x, world.y - y, world.z - z
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function anyHolding()
    return state.hands[0].holding or state.hands[1].holding
end

-- Logs why Wheel Grab is unavailable, or that it is available again, when that changes (never every frame).
local function reportAvailability(reason)
    local text = reason ~= nil and ("unavailable (" .. reason .. ")") or "available"
    if text ~= state.reported then
        state.reported = text
        print("[FS25_VR] Wheel Grab: " .. text)
    end
    state.reason = reason
end

-- Logs every grip squeeze while Wheel Grab is available, with where the hand was and why it did or did not take hold, so a
-- squeeze that does nothing in the headset can be read from the game log (one line per squeeze, never per frame).
-- measureHand's results are `angle`, `radial` and `axial`, or nil and why in `radial` when the hand is not located.
local function reportSqueeze(self, hand, inUse, angle, radial, axial)
    local outcome
    if not inUse then
        outcome = "not taken: the hand has not moved in the last few seconds, so it is not in use"
    elseif angle == nil then
        outcome = "not taken: the hand is not located (" .. tostring(radial) .. ")"
    else
        local distance = ringDistance(self, radial, axial)
        if distance == 0 then
            outcome = "taken"
        else
            outcome = string.format("not taken: %.2f m from the ring", distance)
        end
    end
    local where = ""
    if angle ~= nil then
        where = string.format(" (%.2f m from the axis, %.2f m from the plane)", radial, axial)
    end
    print(string.format("[FS25_VR] Wheel Grab: %s grip squeezed, Steering Wheel %s%s", HAND_NAMES[hand], outcome, where))
end

-- Wheel Grab's own call: the only place that steers the vehicle (ADR-0027). state.sending is true while it
-- runs, which is how a wrap of setSteeringInput tells this call from every other device's.
--
-- The call is made inside pcall, and a game that raises from it (a function that is not what the shape check could tell, or
-- one that cannot be inspected at all: the engine's Lua may offer neither debug.info nor debug.getinfo) is given up on
-- like a missing one (giveUp) once the call has raised on WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP updates in a row.
-- One raise on its own, or two, does not switch Wheel Grab off for the map: a call can fail for a moment (a vehicle being
-- torn down) and the next update sends the value again. Returns whether Wheel Grab gave up.
local function sendSteering(vehicle, value)
    state.sending = true
    local ok, failure = pcall(vehicle.setSteeringInput, vehicle, value, true, engine("InputDevice").CATEGORY.WHEEL)
    state.sending = false
    if ok then
        state.sendFailures = 0
        return false
    end
    state.sendFailures = state.sendFailures + 1
    if state.sendFailures < VRCompanion.WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP then return false end
    giveUp("setSteeringInput raised: " .. tostring(failure))
    return true
end

function VRCompanion:updateWheelGrab(dt)
    updateGrips(self)
    local vehicle, wheel, direction, reason = findSteeringWheel(self)
    reportAvailability(reason)
    -- Nothing is held where Wheel Grab is not available, or in another vehicle than the one grabbed in.
    if wheel == nil or (state.vehicle ~= nil and state.vehicle ~= vehicle) then
        for _, hand in ipairs(HANDS) do releaseHand(hand) end
    end
    if not anyHolding() then
        state.vehicle, state.sent, state.atFullLock, state.sendFailures = nil, nil, false, 0
    end
    if wheel == nil then
        unwrapInstance()
        return
    end

    -- The target moves by the average turn, since the last update, of the hands that held then and still hold. A hand that
    -- grabs now has not turned anything yet and is not counted (so it cannot halve the other hand's turn); a hand that
    -- lets go now is not counted either, and its last turn is ignored with it.
    local low, high = angleLimits(vehicle, wheel, direction)
    local sum, count = 0, 0
    for _, hand in ipairs(HANDS) do
        local entry = state.hands[hand]
        if entry.holding and not entry.gripDown then releaseHand(hand) end
        if entry.holding then
            local angle = measureHand(self, hand, wheel)
            if angle == nil then
                releaseHand(hand)  -- the hand can no longer be located
            else
                local away = distanceToPin(hand)
                if away ~= nil and away > self.WHEEL_GRAB_PULL_AWAY_DISTANCE then
                    releaseHand(hand)  -- pulled away from the Steering Wheel with the grip still squeezed: as if let go
                else
                    sum = sum + wrapAngle(angle - entry.angle)
                    count = count + 1
                    entry.angle = angle
                end
            end
        elseif entry.pressed then
            local inUse = self:isHandInUse(hand)
            local angle, radial, axial = measureHand(self, hand, wheel)
            reportSqueeze(self, hand, inUse, angle, radial, axial)
            if inUse and angle ~= nil and ringDistance(self, radial, axial) == 0 then
                if not anyHolding() then
                    -- The first hand to take hold: the target starts from where the Steering Wheel actually is. A Steering Wheel already
                    -- at a stop is "at full lock" from the start, so the stop is felt only after the target has left it
                    -- and come back (the grab's own pulse is all this hand feels at the stop).
                    state.target = actualAngle(vehicle, wheel, direction)
                    state.atFullLock = state.target <= low or state.target >= high
                    state.vehicle = vehicle
                end
                entry.holding = true
                entry.angle = angle
                pinHand(hand, wheel)
                self:pulseHand(hand, self.WHEEL_GRAB_GRAB_PULSE_MS, self.WHEEL_GRAB_GRAB_PULSE_STRENGTH)
            end
        end
    end
    if not anyHolding() then
        state.vehicle, state.sent, state.atFullLock, state.sendFailures = nil, nil, false, 0
        unwrapInstance()
        return
    end
    wrapInstance(vehicle)  -- the other devices' calls are dropped from here on

    local wasAtFullLock = state.atFullLock
    if count > 0 then
        state.target = state.target + sum / count
    end
    state.target = math.max(low, math.min(high, state.target))
    state.atFullLock = state.target <= low or state.target >= high
    if state.atFullLock and not wasAtFullLock then
        -- The target reached a stop: every holding hand feels it, once, until the target has left the stop again.
        for _, hand in ipairs(HANDS) do
            if state.hands[hand].holding then
                self:pulseHand(hand, self.WHEEL_GRAB_FULL_LOCK_PULSE_MS, self.WHEEL_GRAB_FULL_LOCK_PULSE_STRENGTH)
            end
        end
    end
    local value = steeringValue(vehicle, wheel, direction, state.target)
    state.sent = value
    if value ~= nil and sendSteering(vehicle, value) then
        -- Wheel Grab gave up on the steering function: every hold ends, as if the grips were let go.
        for _, hand in ipairs(HANDS) do releaseHand(hand) end
        state.vehicle, state.sent, state.atFullLock = nil, nil, false
    end
end

function VRCompanion:isHandHoldingSteeringWheel(hand)
    local entry = state.hands[hand]
    return entry ~= nil and entry.holding
end

-- Where the hand's Virtual Hand is drawn while the hand holds: x, y, z (world) and qx, qy, qz, qw (unit world rotation).
-- nil for a hand that does not hold, and for one the engine cannot place (it is then drawn at its grip pose).
function VRCompanion:getWheelGrabHandPose(hand)
    local entry = state.hands[hand]
    if entry == nil or not entry.holding then return nil end
    return pinInTheWorld(entry.pin)
end

function VRCompanion:isSteeringWheelHeld()
    return anyHolding()
end

function VRCompanion:isWheelGrabAvailable()
    return state.reason == nil, state.reason
end

-- ---- Settings, persistence, console commands -------------------------------------------------------------------------

local SETTING_KEY_ENABLED = "vrSettings.wheelGrab"

function VRCompanion:setWheelGrabEnabled(enabled, save)
    self.wheelGrabEnabled = (enabled ~= false)
    if save and type(self.saveConfiguration) == "function" then
        self:saveConfiguration()
    end
end

-- Called from the Motion Controller settings with the open settings file and the engine's setters (any may be nil).
function VRCompanion:saveWheelGrabSetting(xmlFile, xml)
    if type(xml.setBool) == "function" then
        xml.setBool(xmlFile, SETTING_KEY_ENABLED, self.wheelGrabEnabled ~= false)
    end
end

-- Called from the Motion Controller settings with the open settings file and the engine's getters (any may be nil).
-- A key that is missing keeps the current value.
function VRCompanion:loadWheelGrabSetting(xmlFile, xml)
    local loaded = nil
    if type(xml.getBool) == "function" then
        loaded = xml.getBool(xmlFile, SETTING_KEY_ENABLED)
    end
    if loaded == nil and type(xml.getString) == "function" then
        local str = xml.getString(xmlFile, SETTING_KEY_ENABLED)
        if str ~= nil then loaded = VRCompanion.parseOnOffArgument(str) end
    end
    if loaded ~= nil then
        self.wheelGrabEnabled = loaded
    end
end

function VRCompanion:consoleCommandSetWheelGrab(arg)
    local enabled = VRCompanion.parseOnOffArgument(arg)
    if arg == nil or arg == "" or string.lower(tostring(arg)) == "status" then
        return string.format("Wheel Grab is %s. Usage: vrWheelGrab <on|off> (saved)", self.wheelGrabEnabled ~= false and "on" or "off")
    end
    if enabled == nil then
        return "Usage: vrWheelGrab <on|off>"
    end
    self:setWheelGrabEnabled(enabled, true)
    local mission = g_currentMission or _G.g_currentMission
    if mission ~= nil and type(mission.showBlinkingWarning) == "function" then
        pcall(function()
            mission:showBlinkingWarning(string.format("VR Wheel Grab: %s (Saved)", enabled and "on" or "off"), 2500)
        end)
    end
    print(string.format("[FS25_VR] Wheel Grab set to: %s", enabled and "on" or "off"))
    return string.format("Wheel Grab set to: %s (Saved)", enabled and "on" or "off")
end

-- One hand's line for the diagnostics: how far its grip position is from the rim ring (the number to tune the ring by),
-- its grip value and whether it holds.
local function describeHand(self, hand, wheel)
    local pose = self:getHandState(hand)
    local where, detail = "no Steering Wheel to measure from", ""
    if wheel ~= nil then
        local angle, radial, axial = measureHand(self, hand, wheel)
        if angle == nil then
            where = "not located (" .. tostring(radial) .. ")"
        else
            local distance = ringDistance(self, radial, axial)
            where = distance == 0 and "inside the ring" or string.format("%.2f m from the ring", distance)
            detail = string.format(" (%.2f m from the axis, %.2f m from the plane)", radial, axial)
            -- A holding hand also says how far its real grip position is from the point its Virtual Hand is pinned to:
            -- the number the pull-away distance is tuned by.
            local away = state.hands[hand].holding and distanceToPin(hand) or nil
            if away ~= nil then
                detail = detail .. string.format(", %.2f m from its pinned point", away)
            end
        end
    end
    local name = (HAND_NAMES[hand]:gsub("^%l", string.upper))
    return string.format("%s hand: %s, grip %.2f, %s%s", name, where, pose.grip or 0,
        state.hands[hand].holding and "holding" or "not holding", detail)
end

-- The state of Wheel Grab as a list of fields: whether it is on and available, whether the vehicle has a Steering Wheel,
-- each hand's distance to the rim ring and whether it holds, the steering value sent and whether the Steering Wheel is at full lock.
local function diagnosticFields(self)
    local vehicle = self:getControlledVehicle()
    local wheel, direction = nil, nil
    if vehicle ~= nil then wheel, direction = steeringWheelOf(vehicle) end
    local _, _, _, reason = findSteeringWheel(self)
    local wheelText = "Steering Wheel: none"
    if wheel ~= nil then
        local low, high = angleLimits(vehicle, wheel, direction)
        wheelText = string.format("Steering Wheel: found (full lock %.2f to %.2f rad)", low, high)
    end
    local sentText = "steering sent: none (nothing held)"
    if state.sent ~= nil then sentText = string.format("steering sent: %.2f (analog, Wheel Peripheral)", state.sent) end
    -- Whether the wrap that drops other devices' steering while the Steering Wheel is held is in place.
    local wrapText = "steering input wrap: not installed"
    if wrap.problem ~= nil then
        wrapText = "steering input wrap: gave up (" .. wrap.problem .. ")"
    elseif wrap.classWrapper ~= nil then
        wrapText = "steering input wrap: installed" .. (wrap.instance ~= nil and " (on the vehicle too)" or "")
    end
    return {
        string.format("Wheel Grab: %s", self.wheelGrabEnabled ~= false and "on" or "off"),
        reason == nil and "available: yes" or ("available: no (" .. reason .. ")"),
        wheelText,
        describeHand(self, 0, wheel),
        describeHand(self, 1, wheel),
        sentText,
        string.format("at full lock: %s", state.atFullLock and "yes" or "no"),
        wrapText,
    }
end

-- One line (used by the Pointer diagnostics too).
function VRCompanion:getWheelGrabDiagnostics()
    return table.concat(diagnosticFields(self), " | ")
end

-- Prints the fields to the game log, one tagged line each, and returns them as one line.
function VRCompanion:consoleCommandWheelGrabDiagnostics()
    for _, field in ipairs(diagnosticFields(self)) do
        print("[FS25_VR] " .. field)
    end
    return self:getWheelGrabDiagnostics()
end

function VRCompanion:registerWheelGrabConsoleCommands()
    wrapClass()  -- map load: the class again (resetWheelGrab lifted the wrap with the last map)
    if addConsoleCommand == nil then return end
    addConsoleCommand("vrWheelGrab", "Turn Wheel Grab, steering by holding the Steering Wheel with a Motion Controller, on or off (saved): vrWheelGrab [on|off]", "consoleCommandSetWheelGrab", self)
    addConsoleCommand("vrWheelGrabDiagnostics", "Report the Steering Wheel, each hand's distance to the rim ring, the grab and the steering sent: vrWheelGrabDiagnostics", "consoleCommandWheelGrabDiagnostics", self)
end

-- Map unload: forget everything seen this session. The saved setting stays as it is.
function VRCompanion:resetWheelGrab()
    if removeConsoleCommand ~= nil then
        removeConsoleCommand("vrWheelGrab")
        removeConsoleCommand("vrWheelGrabDiagnostics")
    end
    unwrapAll()
    state = newState()
end
