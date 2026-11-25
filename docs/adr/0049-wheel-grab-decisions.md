# Decisions behind Wheel Grab

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0027](0027-wheel-grab-steers-as-a-wheel-peripheral.md).

## Context and Decision
This record holds the non-obvious decisions behind Wheel Grab: finding the Steering Wheel, the grab ring, hand over hand, the end stop, and how the hands win over other steering. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionWheelGrab.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Wheel Grab

  Lets the player steer by taking hold of the Steering Wheel with a Motion Controller (spec #89, ADR-0027).
  In the in-cab camera of the vehicle being driven, squeezing the grip with the hand's grip position inside a
  ring around the Steering Wheel's centre takes hold of it; turning the hand turns the Steering Wheel by the
  same angle, and letting go of the grip lets go. The vehicle is steered the way a Wheel Peripheral steers it:
  every frame while the Steering Wheel is held, vehicle:setSteeringInput(value, true, InputDevice.CATEGORY.WHEEL)
  is called, and nothing else is touched (the vehicle's own steering value is never written, so the game keeps
  its steering rate limit, its end stops and its multiplayer sync). When nothing is held nothing is sent, and the
  game's own steering back centres the Steering Wheel.

  TERMS (CONTEXT.md): the Steering Wheel is the one modelled in the cab, a Wheel Peripheral is a game input device
  connected to the PC. A hand "holds" the Steering Wheel from the grab until the grip is let go.

  THE STEERING WHEEL: the node the game turns, found the way the game finds it (steeringWheelOf): the turned seat's
  or the crab steering mode's own Steering Wheel when the vehicle has one, otherwise the drivable's. The game sets that node's
  rotation to (0, angle, 0) every frame, so it turns about its LOCAL Y axis, whatever tilts it is in the node above.
  The angle is rotatedTime * (the in-cab rotation) * (the node direction: -1 on a turned seat), the same product the
  game uses, so the ACTUAL angle is read from those fields and never from the node's Euler angles (which wrap at
  half a turn while a vanilla Steering Wheel turns up to one and a half turns each way). The node is used for geometry only.

  TURNING: a hand's angle about the axis is measured in the plane of the Steering Wheel, in the frame of the node ABOVE
  the Steering Wheel node (which does not turn with it), from the hand's grip position. Each frame the target angle moves by the
  AVERAGE of the angle changes of the hands that held on the update before and still hold (a hand that grabs or lets go on an
  update is not counted on it, so it neither halves nor adds to the others' turn), starting from the Steering Wheel's actual angle at the first grab, and
  is clamped to full lock each way (the angles at the vehicle's minimum and maximum rotated time) with no slack kept
  past the stop: turning back moves the Steering Wheel at once. The steering value sent is the inverse of what the game does
  with a value (Wheels.lua updateVehiclePhysics), so the node comes to rest at the target:
    rotatedTime = angle / (inCabRotation * direction)
    value = -rotatedTime / (maxRotTime * steeringDirection)  for rotatedTime >= 0
    value =  rotatedTime / (minRotTime * steeringDirection)  for rotatedTime < 0
  (steeringDirection is vehicle:getSteeringDirection(), read live: a turned seat flips both it and the node direction.)
  The game steers toward the value at a capped rate, so the node lags a fast hand; it is the node's actual rotation the
  hand has to follow, never the target.

  THE PINNED HAND: at the grab the hand's grip pose (position and orientation) is stored in the LOCAL frame of the Steering Wheel
  node. While the hand holds, getWheelGrabHandPose(hand) carries it back to the world through the node's ACTUAL rotation
  (not the target), so the Virtual Hand turns with the Steering Wheel exactly as the game turns it, the steering rate lag shows as
  the hand staying on the Steering Wheel, and no angle is kept or wrapped. VRCompanionVirtualHands.lua draws the hand there. A hand
  whose real grip position is more than WHEEL_GRAB_PULL_AWAY_DISTANCE from its pinned point lets go.

  HAPTICS: one short pulse in a hand when it grabs, one firmer pulse in every holding hand when the target reaches full
  lock (again only after the target has left the stop; a hand that grabs while another holds at the stop, or a first hand that
  grabs with the Steering Wheel already at the stop, feels only its own grab), none on release.

  AVAILABILITY (findSteeringWheel): the mod is on (`vrWheelGrab`, Motion Controllers on), no menu is open and 2D
  Floating Window Mode is off, the player is driving the controlled vehicle from its in-cab camera (not a passenger
  camera, not an Outside Camera), and the vehicle has a Steering Wheel and a setSteeringInput, and no hired helper is
  driving it (as for keyboard steering). When it stops being
  available, or the controlled vehicle changes, every hold ends as if the grip were let go (nothing is sent from the
  next update on); so does a hand's hold when that hand can no longer be located (it lost tracking). A grip still
  squeezed when the situation returns is not a press: it has to be let go and squeezed afresh (updateGrips), and a hand
  that is tracked again with its grip squeezed has not squeezed afresh either.

  THE GRAB: the grip counts as squeezed at VRCompanion.WHEEL_GRAB_GRIP_PRESS and as let go below
  VRCompanion.WHEEL_GRAB_GRIP_RELEASE (hysteresis, like the menu back button). A grab starts on the update the grip is
  SQUEEZED (an edge: a grip already squeezed when the hand reaches the ring or when Wheel Grab becomes available does
  not grab) by a hand that is in use, with its grip position inside the ring: between WHEEL_GRAB_RING_INNER and
  WHEEL_GRAB_RING_OUTER from the Steering Wheel's axis and within WHEEL_GRAB_RING_HALF_THICKNESS of its plane. The ring is the
  same on every vehicle: it is not measured from the Steering Wheel's mesh.

  THE HANDS WIN: while the Steering Wheel is held, every other call of vehicle:setSteeringInput is dropped (the keyboard,
  a gamepad, a Wheel Peripheral, the game's phone tilt steering); only Wheel Grab's own call passes. The function is wrapped
  on the Drivable class and on the controlled vehicle's own copy (the engine copies it onto each vehicle), see the section
  "The hands win over other steering" below. If the function is missing or has another shape, Wheel Grab is unavailable, one
  warning is logged and the steering of other devices is untouched; Wheel Grab does the same when its own call raises on
  WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP updates in a row. vrWheelGrabDiagnostics reports the wrap.

  PUBLIC SURFACE
    VRCompanion.wheelGrabEnabled           boolean, default true, saved (`vrWheelGrab on|off`)
    VRCompanion.WHEEL_GRAB_GRIP_PRESS / _RELEASE            grip value that grabs / lets go (0..1)
    VRCompanion.WHEEL_GRAB_RING_INNER / _OUTER              the ring's radii from the Steering Wheel's axis (m)
    VRCompanion.WHEEL_GRAB_RING_HALF_THICKNESS              how far from the Steering Wheel's plane the ring reaches (m)
    VRCompanion:updateWheelGrab(dt)         once per update, called by updateMotionControllers after the hands are polled
    VRCompanion:isHandHoldingSteeringWheel(hand) -> boolean   the hand holds the Steering Wheel (as of the last update)
    VRCompanion:isSteeringWheelHeld() -> boolean              some hand does
    VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE               real grip position this far (m) from its pin lets go
    VRCompanion.WHEEL_GRAB_GRAB_PULSE_MS / _STRENGTH        the pulse in the hand that grabs
    VRCompanion.WHEEL_GRAB_FULL_LOCK_PULSE_MS / _STRENGTH   the pulse in every holding hand at full lock
    VRCompanion:getWheelGrabHandPose(hand) -> x, y, z, qx, qy, qz, qw   where a holding hand's Virtual Hand is drawn (world),
                                                              or nil when the hand does not hold
    VRCompanion:isWheelGrabAvailable() -> boolean, reason     whether a grab is possible now (as of the last update)
    VRCompanion:setWheelGrabEnabled(enabled, save)
    VRCompanion:saveWheelGrabSetting(xmlFile, xml) / loadWheelGrabSetting(xmlFile, xml)   settings chain
    VRCompanion:getWheelGrabDiagnostics() -> string          the state of Wheel Grab, one line
    VRCompanion:loadWheelGrab() / resetWheelGrab()   map load / unload (registered with the core, with the two console commands)
    Console: vrWheelGrab [on|off], vrWheelGrabDiagnostics

  Hooks into the rest of the mod:
    MotionControllers  updateMotionControllers -> updateWheelGrab(dt), after the hands are polled; the saved setting
                       is saved and loaded by this file's own settings hooks (see the registration)
    VirtualHands       updateHand asks getWheelGrabHandPose(hand): a holding hand is drawn there, and stays shown however
                       long it is held still
  ```

- **`VRCompanion.WHEEL_GRAB_GRIP_PRESS`**: The grip counts as squeezed at PRESS and as let go below RELEASE (hysteresis against a grip that rests half-way, the same as the menu back button's 0.6 and 0.3).
- **`VRCompanion.WHEEL_GRAB_RING_INNER`**: THE RIM RING, in metres, the same for every vehicle: the grip position of a squeezing hand must be this far from the Steering Wheel's axis (measured in its plane) and this close to its plane. Vanilla Steering Wheels are about 0.2 to 0.25 m across the rim from the centre, and in VR a hand is not precise, so the ring is generous: 10 to 25 cm, which leaves out a hand resting on the hub and one resting on the dashboard behind a small Steering Wheel. The plane is the node's origin plane, which is not where the rim visibly is: in the first headset test, hands gripping the rim were consistently 8 to 11 cm from it (always on the same side), so the ring reaches 15 cm either side. Tune in the headset with `vrWheelGrabDiagnostics` and the log line written for every squeeze.
- **`VRCompanion.WHEEL_GRAB_PULL_AWAY_DISTANCE`**: PULL-AWAY: a holding hand's real grip position this far (m) from the point its Virtual Hand is pinned to ends the grab, as if the grip were let go: the player cannot steer from somewhere they obviously could not reach. The pinned hand lags the real one while the game's steering catches up with a quick turn; 20 cm let go too easily in the first headset test, so it is 30 cm, which is still short of walking away from the cab. Tune in the headset: `vrWheelGrabDiagnostics` reports each hand's distance to its pin.
- **`VRCompanion.WHEEL_GRAB_GRAB_PULSE_MS`**: HAPTICS (milliseconds, strength 0..1): a short pulse in the hand that grabs, a firmer, longer one in every holding hand when the target reaches full lock. Nothing on release and no continuous rumble. For scale, the Pointer Hand's Click Point select is 25 ms at 0.25 and a click is 45 ms at 0.6 (VRCompanionMotionControllers.lua).
- **`VRCompanion.WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP`**: Wheel Grab's own setSteeringInput call raising on this many updates in a row makes it give up for the map, with one warning, exactly as for a missing or reshaped function. Three: a one-off failure is retried the next update, while a call that is broken fails on every one.
- **`STEERING_FUNCTION`**: The hands win over other steering (ADR-0027)

  Every steering device calls vehicle:setSteeringInput(value, isAnalog, deviceCategory): the keyboard, a gamepad, a Wheel Peripheral and the game's own phone tilt steering and steering slider. While the Steering Wheel is held, Wheel Grab drops every one of those calls; its own call (made with `state.sending` set) is the only one that passes. The game gives each vehicle its OWN COPY of the function when it builds the vehicle types (Vehicle:load -> copyTypeFunctionsInto, after the mod's files are loaded), and the steering input calls the copy, so there are two wraps:

  - the class (Drivable.setSteeringInput), wrapped as the files load (or as soon as Drivable is there), so that every vehicle type the game builds afterwards copies the wrapper;
    - the controlled vehicle's own copy, wrapped for as long as the Steering Wheel is held in it, for a vehicle whose copy was taken from the function before the class was wrapped.

  Both pass every call through unchanged whenever nothing is held. Like Interactive Control's wraps, a restore only puts back a function when it is still the wrapper put there: a wrap another mod added later is never undone. When the function is missing or not shaped as the wrap expects, Wheel Grab gives up with ONE warning: it is unavailable and the steering of every other device is left untouched.

- **`isHeldIn`**: Whether the vehicle's Steering Wheel is held, which is when the wraps drop calls. The wrap passes everything when Wheel Grab or Motion Controllers are switched off, even before the next update lets go of the Steering Wheel.
- **`STEERING_PARAMETERS`**: The steering function, declared to the core's wrap helper (VRCompanionGameWraps.lua, which says what each field means): the class wrap, once per class table, and the controlled vehicle's own copy (the helper's instance wrap, which leaves a copy the class wrap reaches alone). Giving up lifts both, with ONE warning: Wheel Grab never steers again this map (it is unavailable, see steeringInputProblem), so that the steering of every other device is exactly the game's. Map unload forgets it gave up.
- **`wrapClass`**: Wraps the Drivable class function once per class table. A class whose function is not as expected is left alone (steeringInputProblem reports it when a vehicle needs it).
- **`steeringInputProblem`**: Why Wheel Grab cannot rely on the vehicle's steering function, or nil. The first time it cannot, one warning is logged and Wheel Grab stays unavailable for the rest of the map.
- **`steeringWheelOf`**: The table the game would turn right now: { node, indoorRotation, ... } and the direction it turns the node in, or nil. A turned seat and a crab steering mode may bring their own Steering Wheel (ReverseDriving / CrabSteering updateSteeringWheel).
- **`actualAngle`**: Where the vehicle's Steering Wheel is turned to, in radians and unwrapped: the product the game sets the node's rotation to.
- **`steeringValue`**: The steering value that brings the Steering Wheel to rest at `angle`, or nil when the vehicle has no steering range there (or its steering direction is changing sign, as a turned seat's does while the seat turns).
- **`askVehicle`**: What a vehicle answers to a yes-or-no question (a method of the game's that returns a boolean first), or nil when it has no such method or it raises: only an explicit false counts as no.
- **`findSteeringWheel`, `if self:doesMenuTakeInput() then return nil, nil, nil, "a menu is open" end`**: A menu or 2D Floating Window Mode takes the player out of the cab: the hands cannot be located in it either.
- **`findSteeringWheel`, `if askVehicle(vehicle, "getIsEntered") == false then`**: Wheel Grab is available exactly where the keyboard could steer the vehicle: it is entered, nothing blocks the player's control and it is powered (a vehicle with its motor off does not follow any steering input).
- **`findSteeringWheel`, `if askVehicle(vehicle, "getIsAIActive") == true then`**: A hired helper driving the vehicle makes Wheel Grab unavailable, just as it does the keyboard's steering (the game registers no steer action while getIsAIActive() is true). Nothing here ever touches the helper: squeezing the grip at the rim neither stops the job nor takes the Steering Wheel from it, and a hold in progress simply ends (updateWheelGrab).
- **`updateGrips`**: Updates each hand's squeezed state with hysteresis (always, whether or not Wheel Grab is available, so a grip that is already squeezed when it becomes available is not a press) and notes the updates it went down.
- **`updateGrips`, `entry.pressed`**: A grip already squeezed when the hand is tracked again (it lost tracking while holding) is not a press: the grip has to be let go and squeezed afresh.
- **`measureHand`**: Where the hand's grip position is relative to the Steering Wheel: the angle about its axis (radians, in the plane of it, turning the same way as the node's rotation about its local Y), the distance from the axis and the signed distance from the plane. All measured in the frame of the node above the Steering Wheel node, which does not turn with it. Returns nil and why when the hand is not located (an untracked grip pose, or no head placed in the world: a menu, 2D Floating Window Mode, another camera, Pass-Through Mode).
- **`pinHand`**: Stores the hand's grip pose, which measureHand has just left in state.scratch (world), in the local frame of the Steering Wheel node: the point the Virtual Hand is pinned to. Without the engine's node functions the hand is not pinned and is drawn at its grip pose as usual.
- **`pinInTheWorld`**: Where a pin is in the world right now, carried through the Steering Wheel node's actual rotation: x, y, z, qx, qy, qz, qw, or nil when the engine cannot say.
- **`reportSqueeze`**: Logs every grip squeeze while Wheel Grab is available, with where the hand was and why it did or did not take hold, so a squeeze that does nothing in the headset can be read from the game log (one line per squeeze, never per frame). measureHand's results are `angle`, `radial` and `axial`, or nil and why in `radial` when the hand is not located.
- **`sendSteering`**: Wheel Grab's own call: the only place that steers the vehicle (ADR-0027). state.sending is true while it runs, which is how a wrap of setSteeringInput tells this call from every other device's.

  The call is made inside pcall, and a game that raises from it (a function that is not what the shape check could tell, or one that cannot be inspected at all: the engine's Lua may offer neither debug.info nor debug.getinfo) is given up on like a missing one (giveUp) once the call has raised on WHEEL_GRAB_STEERING_FAILURES_BEFORE_GIVING_UP updates in a row. One raise on its own, or two, does not switch Wheel Grab off for the map: a call can fail for a moment (a vehicle being torn down) and the next update sends the value again. Returns whether Wheel Grab gave up.

- **`VRCompanion:updateWheelGrab`, `if wheel == nil or (state.vehicle ~= nil and state.vehicle ~= vehicle) then`**: Nothing is held where Wheel Grab is not available, or in another vehicle than the one grabbed in.
- **`VRCompanion:updateWheelGrab`, `local low, high = angleLimits(vehicle, wheel, direction)`**: The target moves by the average turn, since the last update, of the hands that held then and still hold. A hand that grabs now has not turned anything yet and is not counted (so it cannot halve the other hand's turn); a hand that lets go now is not counted either, and its last turn is ignored with it.
- **`VRCompanion:updateWheelGrab`, `releaseHand(hand)`**: pulled away from the Steering Wheel with the grip still squeezed: as if let go
- **`VRCompanion:updateWheelGrab`, `state.target`**: The first hand to take hold: the target starts from where the Steering Wheel actually is. A Steering Wheel already at a stop is "at full lock" from the start, so the stop is felt only after the target has left it and come back (the grab's own pulse is all this hand feels at the stop).
- **`VRCompanion:updateWheelGrab`, `for _, hand in ipairs(HANDS) do`**: The target reached a stop: every holding hand feels it, once, until the target has left the stop again.
- **`VRCompanion:updateWheelGrab`, `for _, hand in ipairs(HANDS) do releaseHand(hand) end`**: Wheel Grab gave up on the steering function: every hold ends, as if the grips were let go.
- **`diagnosticFields`, `wrapText`**: Whether the wrap that drops other devices' steering while the Steering Wheel is held is in place.
- **`VRCompanion:loadWheelGrab`**: Map load: the class is wrapped again (resetWheelGrab lifted the wrap with the last map).
- **`registerSubsystem "wheelGrab"`**: Registered with the core (the registry is documented in VRCompanionCore.lua). No update hook: updateWheelGrab runs from the Motion Controller poll, right after the hands are polled. The map-load hook is the re-wrap; it runs at every map load, whether or not the engine has addConsoleCommand. Unload is in reverse file order, after the Virtual Gamepad and before the Virtual Hands and the Menu Pointer; none of them reads the wrap.

### `tests/test_wheel_grab.cpp`

- **`CompanionModTest.AHandGrippingTheRimTenCentimetresFromTheNodesPlaneStillTakesTheSteeringWheel`, `headset`**: In the headset the node's plane was 8 to 11 cm from where the rim visibly is, so a hand on the rim measured that far.

### `tests/test_wheel_grab_pinned_hand.cpp`

- **the file**: Wheel Grab (spec #89, ticket #91): the Virtual Hand stays on the Steering Wheel while it is held, and the player feels the grab and the end stop. The real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the cab vehicle that steers the way the game's does (companion_mod_stubs.h, StubSteer) and the hand model's scene-graph stubs (StubModel). The test moves the fake Motion Controllers, drives frames the way the game would and asserts only what the player would see and feel: where the Virtual Hand node is drawn, how the fingers curl, the haptic pulses the fake runtime recorded, the steering calls that reached the vehicle and the diagnostics line. Prior art: test_wheel_grab.cpp (the same cab), test_virtual_hands.cpp (hand nodes), test_haptics.cpp (pulses).

  The cab is at the world origin facing +Z with the head at the tracking space origin, so a point at world (x, y, z) is where a hand at tracking (-x, y - 1.5, -z) is. The Steering Wheel hangs 0.5 m in front of the head and 0.3 m below it, leaning toward the driver; a hand "at the rim" is 0.18 m from its axis. A small full-lock rotation (120 degrees, a tracked tractor's) keeps full lock within a hand's reach and makes the game's steering rate lag easy to see: the Steering Wheel moves 0.087 rad per frame at most.

### `tests/test_wheel_grab_steering_wrap.cpp`

- **the file**: Wheel Grab, the hands win over other steering (spec #89, ticket #93, ADR-0027), seen from outside: while a hand holds the Steering Wheel, every steering call another device makes (the keyboard, a gamepad, a Wheel Peripheral, the game's own phone tilt steering) is dropped before it reaches the vehicle, Wheel Grab's own calls reach it, and everything passes again as soon as the hand lets go. The Companion Mod runs for real against the real VRBridge over FakeTrackedHeadset; the vehicle is the stub of companion_mod_stubs.h (StubSteer) that, like the game, COPIES Drivable.setSteeringInput onto the vehicle, so a wrap that only replaced the class function too late would not reach it. Every assertion is on the steering calls that reached the vehicle, the function the vehicle ends up with and the log, never on the mod's own state. Prior art: test_wheel_grab.cpp (#90) and Interactive Control's wrap of the Click Point functions (test_interactive_control_auto_on.cpp).

  The cab geometry is the one of test_wheel_grab.cpp: a hand "at the rim" is 0.18 m from the Steering Wheel's axis.

- **`StartMod`**: The game before the Companion Mod loads, as it is in the game: the Drivable class is there when the mod's files load. `variant` is a StubSteer.install variant ("ok", "missing", "reshaped"); `equipOptions` are more StubSteer.equip options, e.g. "copyOfOriginal = true" for a vehicle whose copy was taken before the class was wrapped.
- **`OtherDevicesSteer`, `"StubSteer.wheelPeripheral(cab.vehicle, " + kPhoneTiltValue + ")\n").c_str(),...`**: the game's own phone tilt steering and the HUD slider use the Wheel Peripheral's category too

### `tests/test_wheel_grab_two_hands.cpp`

- **the file**: Wheel Grab with both hands and hand over hand (spec #89 ticket #92, ADR-0027), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the way the game's does (companion_mod_stubs.h, StubSteer). The test moves the two fake Motion Controllers, drives frames the way the game would and asserts only what the player would see and what the game was told: the steering calls that reached the vehicle and how far the Steering Wheel turned. One hand alone is covered by test_wheel_grab.cpp.

  The cab is at the world origin facing +Z with the head at tracking space origin (see test_wheel_grab.cpp). A hand "at the rim" is 0.18 m from the Steering Wheel's axis, at an angle about it: 0 is the direction the Steering Wheel's rim point starts in, a hand turned by +a is where the rim point of a Steering Wheel turned by a is, and a hand at angle + pi is on the opposite side of the rim.

- **`CompanionModTest.AHandRegrabbingAfterLettingGoPicksUpFromTheSteeringWheelsCurrentAngleNotWhereTheLastGrabWasHeading`, `ASSERT_NO_FATAL_FAILURE(StartMod(L, bridge, SteerableCab("steeringDuration = ...`**: A very slow steering duration: the Steering Wheel stays well behind a hand that turns it, and drifts only a little towards centre while nothing holds it.

### `tests/test_wheel_grab_vehicles.cpp`

- **the file**: Wheel Grab on turned seats and with hired helpers (spec #89, ticket #96, ADR-0027), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a cab vehicle that steers the way the game's does (companion_mod_stubs.h, StubSteer, with its turnedSeat and helper options). The test moves the fake Motion Controllers and asserts only what the player would see and what the game was told: the steering calls that reached the vehicle, how far the Steering Wheel turned and which way the wheels on the ground steered.

  A turned seat (ReverseDriving) flips BOTH the direction the game turns the Steering Wheel node in and the vehicle's steering direction, so the visible Steering Wheel follows the hand exactly as on any seat and the wheels on the ground steer the other way, as they must when driving backwards. The two flips cancel in the steering value, so for the same turn of the Steering Wheel the value Wheel Grab sends is the same on a turned seat as on an unturned one. (Ticket #96 words this as "steering of the opposite sign"; that is what the vehicle's wheels do, not the value sent. The tests assert both.)

  The cab is the same as in test_wheel_grab.cpp: at the world origin facing +Z with the head at tracking space origin, the Steering Wheel 0.5 m in front of the head and 0.3 m below it; a hand "at the rim" is 0.18 m from its axis.

- **`CompanionModTest.OnATurnedSeatTheSteeringWheelISeeFollowsMyHandAndTheWheelsOnTheGroundSteerTheWayThatSteeringWheelWould`, `EXPECT_NEAR(RoadWheelAngle(L), -0.6 / kFullLockAngle, 0.002)`**: Driving backwards the wheels on the ground steer the other way for the same turn of the Steering Wheel: this is what "steers the way that Steering Wheel would" is, and it is the game's doing (it reverses the steering direction).
- **`CompanionModTest.OnATurnedSeatTheSteeringWheelISeeFollowsMyHandAndTheWheelsOnTheGroundSteerTheWayThatSteeringWheelWould`, `EXPECT_LT(Number(L, "StubSteer.calls[#StubSteer.calls].value"), 0.0) << "the ...`**: The game reverses the way it turns the Steering Wheel and the vehicle's steering direction alike, so the value that turns the Steering Wheel a given way is the same on every seat. Reversing it again would fight the game: the Steering Wheel would turn away from the hand.

### `tests/wheel_grab_test_support.h`

- **the file**: What the Wheel Grab tests (tests/test_wheel_grab\*.cpp, spec #89, ADR-0027) share: the cab geometry, the fake hands, the engine's console and menu stubs, the steerable cab, how the mod is started, the frame the game runs before the Companion Mod's update, and the macros that move the Motion Controllers. Header-only (inline), in its own namespace; a test file brings it in with `using namespace wheel_grab_test;` inside its anonymous namespace.

  The cab is at the world origin facing +Z with the head at the tracking space origin, so a point at world (x, y, z) is where a hand at tracking (-x, y - 1.5, -z) is. The Steering Wheel hangs 0.5 m in front of the head and 0.3 m below it, leaning toward the driver (companion_mod_stubs.h, StubSteer); a hand "at the rim" is 0.18 m from its axis.

  What differs between the files stays in the files: which stubs a mod is started with (StartOptions), the cab's options, what else happens in a game frame (WHEEL_GRAB_PRE_FRAME_EXTRA_LUA) and every assertion helper that reads something only that file looks at.

- **`kNoPullAway`**: Wheel Grab lets go of a hand that is further than WHEEL_GRAB_PULL_AWAY_DISTANCE from the point its Virtual Hand is pinned to. Most of these tests turn the hand faster and further than an arm could while the Steering Wheel follows (the stops, hand over hand), which the pull-away rule would take for a hand pulled off the Steering Wheel, so they set the distance so large it never happens. The pull-away rule itself is tested in test_wheel_grab_pinned_hand.cpp, which leaves the distance alone.
- **`#ifndef WHEEL_GRAB_PRE_FRAME_EXTRA_LUA`**: A test file whose game frame has more in it defines this to a Lua chunk before including the header.
- **`FRAMES`**: The macros read `L` and `headset` of the test body, as the tests do.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 48 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
