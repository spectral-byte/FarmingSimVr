# Decisions behind Animated Objects and Manual Attach

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0034](0034-animated-objects-are-worked-through-the-games-own-toggle.md), [ADR-0035](0035-manual-attach-is-worked-from-three-coupling-points-through-its-own-paths.md).

## Context and Decision
This record holds the non-obvious decisions behind the Animated Objects of the game and the Coupling Points of the Manual Attach mod, both worked with the Pointer Ray. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionAnimatedObjects.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Animated Objects (ADR-0034, spec #180)

  Lets the Pointer Ray work the Animated Objects of the game (CONTEXT.md: a building's door or gate, a barrier placed on the map)
  on foot: standing in the zone around one, the player points the Pointer Ray at one of its moving parts (or touches it up close) and
  pulls the trigger, and it does what its prompt does: it opens, closes, or turns back while it moves. Standing in the zones of
  several, the player works the one the ray is on. The game's prompt and its bound button keep working as before.

  How it works. The game puts an Animated Object's activatable in its system while the local player is in the object's zone, from the
  trigger callback the engine calls by name. The module wraps that callback at the class level, ORIGINAL FIRST and with the same
  conditions the original applies, to know which objects the player is in the zone of. Each update it casts the Pointer Ray against the
  world's collision (the engine's raycastClosest, up to the ray's length) and walks up from the hit node to one of the animation parts
  of an object in the zone: that object is the selected one, and the ray ends where it hit. A pull of the trigger runs the same toggle
  the prompt's button runs (the activatable's toggle handler), guarded so that an error never reaches the game.
  The Pointer Hand feels it as a Click Point (VRCompanion:onClickPointEvent): a light pulse as the ray lands on an object it was not on
  (not when it moves to another part of the same one), a firmer one for each pull that toggled. Never the other hand.

  What it relies on (release of the game this was written against: the SDK's AnimatedObject.lua, AnimatedObjectActivatable.lua; pinned by
  tests/test_animated_objects_wraps.cpp against stand-ins, with the game's functions missing or reshaped):
    AnimatedObject.triggerCallback(self, triggerId, otherId, onEnter, onLeave, onStay)   wrapped
    AnimatedObject.getCanBeTriggered(self)       the game's "can be triggered" check, asked of the object itself (an own copy on a
        placeable's object, which also asks the farm, answers the question the prompt asks)
    AnimatedObjectActivatable.onAnimationInputToggle(self)   the toggle handler of a single press action; never wrapped
    g_currentMission.missionInfo:isa(FSCareerMissionInfo), g_localPlayer.rootNode, raycastClosest, getParent
    on an object: controls.posAction / controls.negAction (a single press action has the first and not the second), animation.parts[i].node,
        activatable, isDeleted
  When the game does not have the functions the wrap needs, or they have another shape, the Companion Mod logs ONE warning when
  Motion Controllers are in use and leaves the game's Animated Objects, and every other feature, alone.

  What is selected. An object in the zone whose "can be triggered" check answers true and whose action is a single press (a hold-to-move
  one, with a negative action or the same action for both directions, is never selected: it needs the button held). The cast starts a
  little behind the fingertip, so a finger that has gone into the leaf still sees it, and only a hit within the Pointer Ray's length
  counts. The ray is not drawn on an Animated Object nor is the trigger taken while the desk mouse has control of the pointer, in a menu
  or a Stereo Dialog, in a vehicle, off the first person view, or with Motion Controllers off.

  PUBLIC SURFACE
    VRCompanion:updateAnimatedObjects(dt, suppressed, rivalDistance) -> hasTarget, hitX, hitY, hitZ   once per update after the Pointer
        Ray is placed (VRCompanion:updatePointerRay); suppressed: another target (a Click Point) has the ray, so nothing is selected;
        rivalDistance: how far along the ray an object Object Grab can pick up is (nil: none), and the nearer of the two is the ray's
        target (ticket #183).
        While the ray is on an Animated Object the Pointer Hand's trigger is taken off the Virtual Gamepad
        (VRCompanion:takeVirtualGamepadInput(hand, "trigger")).
    VRCompanion:getAnimatedObjectsDiagnostics() -> string   one line for vrPointerDiagnostics
    VRCompanion:loadAnimatedObjectsSupport() / resetAnimatedObjectsSupport()   map load / unload (hands the class back as found); both are the
        hooks of the "animatedObjects" subsystem this file registers with the core

  Hooks into the rest of the mod
    MotionControllers   updatePointerRay -> updateAnimatedObjects(dt, ...) after Interactive Control, before Object Grab;
        settleInteractiveControl (the paths where Motion Controller input is ignored) -> updateAnimatedObjects(dt, true) so the
        selection clears; getPointerDiagnostics
  ```

- **`DEFAULT_REACH_BEHIND`**: The cast starts as far (m) behind the fingertip as the game's hands' look ray does: Object Grab's VRCompanion.OBJECT_GRAB_REACH_BEHIND, the one place it is tuned (the nearer-wins comparison with an object Object Grab can pick up measures both from the same point). The fallback is for a mod that loads without Object Grab. A hit nearer than that minus the margin is behind the finger and does not count.
- **`WRAP_SPECS`**: The game's functions the Subsystem relies on, declared to the core's wrap helper (VRCompanionGameWraps.lua, which says what each field means): the trigger callback is wrapped (its `make` is attached with the zone below), the other two are CHECK-ONLY. Checked in the declared order (the set's `order`): the callback, the "can be triggered" check, the toggle handler.
- **`wraps`**: Giving up is ONE warning, logged only when Motion Controllers are in use (see updateAnimatedObjects); nothing is wrapped then.
- **`IGNORED_FLAGS` / `castMask`**: The collision flags the ray cast leaves out: the zone is made of trigger shapes, the player's own capsule is around the head, and the terrain is never a door. `castMask` clears them bitwise via `bit32.band(..., bit32.bnot(...))` rather than subtraction so flags absent from `CollisionMask.ALL` never corrupt higher bits (spectral-byte/FarmingSimVr-issues#247).
- **`trackZone`**: The three conditions the game's trigger callback applies before it puts the activatable in its system or takes it out: a career mission, an enter or a leave, and the local player's root node. Reference-counted per object (`state.zone[object]`) so leaving one overlapping trigger zone keeps the object active while still inside another (spectral-byte/FarmingSimVr-issues#247). Errors never reach the game (the original has run by now).
- **`WRAP_SPECS.callback.make`**: The wrap of the game's trigger callback: original first, then the zone is tracked (errors never reach the game).
- **`VRCompanion:loadAnimatedObjectsSupport`**: Map load: wraps the game's trigger callback when the game has what the module relies on. When it does not, the reason is kept and warned of once, when Motion Controllers are in use (a player without them is never told).
- **`registerSubsystem "animatedObjects"`**: Map load wraps the game's trigger callback; map unload hands it back. No commands. Order: loadAnimatedObjectsSupport runs with the other map-load hooks, after loadConfiguration; it does not depend on any of them. Map unload comes in reverse file order, before the Virtual Gamepad's and Wheel Grab's resets.
- **`rayMayWork`, `if self:doesMenuTakeInput() or self:getControlledVehicle() ~= nil then return...`**: A menu or a Stereo Dialog takes the input; a vehicle and the other views are out of scope (ADR-0034).
- **`objectUnderTheRay`, `node`**: From the hit node up: a leaf's shape is below the part's node, the part itself counts, a building around it does not. If the back-offset cast hits non-part geometry behind the fingertip (`hit.distance < behind`), `objectUnderTheRay` retries forward from the fingertip so rear walls or railings never self-occlude doors in front (spectral-byte/FarmingSimVr-issues#247).
- **`toggle`**: A pull runs the toggle the prompt's button runs. When the prompt is not the one shown, its action event is not registered, and the handler's refresh of the prompt's texts would raise for it: the refresh is left out then. Guarded: nothing reaches the game. Returns whether the handler ran without an error.
- **`VRCompanion:updateAnimatedObjects`**: Called by the Pointer Ray code once per Companion Mod update, after the ray is placed. Returns whether the ray is on an Animated Object that can be worked and, then, where it hits. `suppressed`: a Click Point has the ray. `rivalDistance`: how far along the ray from the fingertip a loose object Object Grab can pick up is; one nearer than the Animated Object is the ray's target instead (a tie is the Animated Object's), and nothing is selected then.
- **`VRCompanion:updateAnimatedObjects`, `if selected ~= state.landedOn then`**: The select pulse of a Click Point: one on the Pointer Hand as the ray lands on an Animated Object it was not on, again only after it was on none or on another. Moving from one moving part to another of the same object is not landing.
- **`VRCompanion:updateAnimatedObjects`, `mayUseTrigger`**: While the ray is on an Animated Object the Pointer Hand's trigger is its: it reads released on the Virtual Gamepad, unless it was already held when the ray landed (it then keeps its binding and toggles nothing). Only the update the trigger went down toggles.
- **`describeObject`**: What names an object in the diagnostics: its save id (a placed object has one), else the name of its first animation part's node (a building's door has none, only the nodes of its i3d), else that node's id, else just "yes".

### `mods/FS25_VRCompanion/src/VRCompanionManualAttach.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Manual Attach support (ADR-0035, spec #227)

  Lets the Pointer Ray work the Coupling Points of the Manual Attach mod (FS25_manualAttach, release 1.1.0.0) on foot (CONTEXT.md: Coupling
  Point, Implement): a hitch, its PTO stub and its hose couplers. Manual Attach makes the player attach and detach Implements by hand, but picks the hitch by distance, not by aim, and puts the
  action on a key. Here the player stands within Manual Attach's own reach of a hitch, points the Pointer Ray at it and pulls the trigger: the
  Implement lined up there is attached, or the one on it is detached, by Manual Attach's own code (its reach, rules, warnings, sounds and
  multiplayer events are unchanged). The hitch is the first of the three Coupling Points of the spec. The other two are on the same vehicle for
  the Implement on the hitch: its PTO stub (each of its own PTO outputs for that joint that has an output node: a pass-through output, which
  resolves to another vehicle, has none) and its hose couplers (each hose target node and custom hose target for that joint). A pull on the PTO
  stub runs the handler's execute path for the PTO attachment, on any hose coupler for the hoses attachment (all the hoses together); each
  is pointable when that attachment would show its prompt for the Implement, and the one of all of them nearest the centre of the ray is
  selected. Pointing at a PTO stub never reaches the hoses or the hitch and the other way round: one pull, one of Manual Attach's paths.

  How it works. Manual Attach keeps the vehicles its detection has found (handler.detectedVehicles). Each update the module lists, from those
  and with Manual Attach's own helpers and constants, every hitch within its reach of the player: a free hitch with an Implement lined up
  (VehicleAttachmentHandler.getAttachableInJointRange) and a hitch with an Implement on it. Manual Attach's own search keeps only the nearest of
  them; the module keeps all. A hitch can be pointed at when Manual Attach's joint attachment would show its attach or detach prompt for that
  hitch's candidate (getActionEventInfo), so what Manual Attach refuses in its perform path (an Implement not lowered, a PTO or hoses still
  connected) is still pointed at, and the pull shows Manual Attach's warning. The Coupling Point nearest the centre of the ray, within the
  pointer cone (vrPointerCone, the angle at the hand, as for Click Points), is selected, and the ray ends on it. A pull sets the handler's five
  candidate fields to its hitch's and runs the handler's own execute path for the attachment of that Coupling Point, guarded so that an error
  never reaches the game.

  What it relies on (release 1.1.0.0; pinned by tests/test_manual_attach_wraps.cpp against stand-ins, with the functions missing or reshaped):
    FS25_manualAttach.g_manualAttach (the global table is the fallback)   canPlayerPerformManualAttachment, vehicleAttachmentHandler
    FS25_manualAttach.VehicleAttachmentHandler (the global table is the fallback)   the constants MAX_ATTACH_DISTANCE_SQ, MAX_ATTACH_ANGLE and
        PLAYER_MIN_DISTANCE_SQ; the static getAttachableInJointRange(vehicleSystem, vehicle, attacherJoint, maxDistanceSq, maxAngle, isPlayerBased)
        and isVehicleValid(vehicle)
    VehicleAttachmentHandler.updateCandidates(handler) (wrapped, see below)
    the handler   detectedVehicles, executeAttachment(attachment), vehicleJointAttachment, playerCanPerformManualAttachment, and the candidate fields attacherVehicle,
        attacherVehicleJointDescIndex, attachable, attachableJointDescIndex, attachedImplement
    the joint attachment   getActionEventInfo(attacherVehicle, attachable, attachedImplement, jointDescIndex, playerCanPerform), performAttachment
    the PTO attachment (powerTakeOffAttachment) and the hoses attachment (connectionHosesAttachment)   the same getActionEventInfo and
        performAttachment (they act on the attached Implement alone)
    g_currentMission.vehicleSystem, g_localPlayer.rootNode, localToLocal, getWorldTranslation
    on a vehicle   spec_attacherJoints.attacherJoints[i].jointTransform, getAttachedImplements() ({ object, jointDescIndex })
    the game's vehicle side of the PTO and hoses, when a vehicle has the specialization (else it has no such Coupling Point; a specialization
        with another shape turns the support off): getOutputPowerTakeOffs() ({ outputNode, attacherJointIndices = { [jointIndex] = true } }),
        spec_connectionHoses.targetNodes ({ node, attacherJointIndices }) and .customHoseTargetsByAttacher[jointIndex] ({ node })
  Prompts and keys follow the pointed hitch (ticket #230). The module wraps ONE function of Manual Attach, VehicleAttachmentHandler.updateCandidates
  (the candidate update, at the class level so the handler instance finds it), original first: after Manual Attach has picked its nearest
  candidate, while the ray is on a Coupling Point (and the player can attach by hand), the five candidate fields are set to its hitch's. Manual
  Attach's own draw (the input help prompts, the context box) and its own keys read those fields, so the text, the keys and the pull never
  disagree; with the ray on no Coupling Point the wrap changes nothing. Map unload hands the function back as found. Nothing else of Manual Attach is
  wrapped or rewritten, its bindings included; the module otherwise only reads, and sets the candidate fields before it runs the execute path.
  When what it relies on (updateCandidates included, one parameter: the handler) is missing or has another shape, the Companion Mod logs ONE
  warning when Motion Controllers are in use, wraps nothing and leaves Manual Attach, and every other feature, alone. Its instance and attachment
  handler, which it makes as the map loads, are waited for (WAIT_FOR_OBJECTS_MS, counted over the updates with a tracked Motion Controller) before
  that warning; the mod not being loaded, or a function missing or reshaped, is final at the first look.

  When the ray can work a Coupling Point. Only while Manual Attach says the player can attach by hand (canPlayerPerformManualAttachment: on
  foot, no Hand Tool in hand, no Held Object), only while the Pointer Ray has control of the pointer (not the desk mouse), the head is placed in
  the world, no menu or Stereo Dialog takes the input, and Motion Controllers are on. Of a Click Point and a Coupling Point both within the
  cone, the one nearer the ray's centre wins (a tie is the Click Point's) and the other selects nothing; either one over an Animated Object and
  over an object Object Grab can pick up.

  PUBLIC SURFACE
    VRCompanion:updateManualAttach(dt, suppressed, clickCos) -> hasTarget, hitX, hitY, hitZ   once per update after the Pointer Ray is placed
        (VRCompanion:updatePointerRay), after Interactive Control has reported its best Click Point (peekInteractiveControlTarget) and before it
        commits; suppressed: nothing is selected; clickCos: the cosine of that Click Point's angle from the ray's centre, or nil without one.
        While the ray is on a Coupling Point the Pointer Hand's trigger is taken off the Virtual Gamepad (VRCompanion:takeVirtualGamepadInput(hand,
        "trigger")). The Pointer Hand feels it as a Click Point (VRCompanion:onClickPointEvent): a light pulse as the ray lands on a Coupling
        Point it was not on, a firmer one for each pull that ran Manual Attach's path.
    VRCompanion:getManualAttachDiagnostics() -> string   one line for vrPointerDiagnostics
    VRCompanion:resetManualAttachSupport()               map unload (hands the wrapped candidate update back as found); the hook of the
        "manualAttach" subsystem this file registers with the core

  Hooks into the rest of the mod
    MotionControllers   updatePointerRay -> peekInteractiveControlTarget(), updateManualAttach(dt, false, clickCos), updateInteractiveControl(dt,
        selectNone), before Animated Objects and Object Grab; settleInteractiveControl (the paths where Motion Controller input is ignored) -> updateManualAttach(dt, true) so the selection
        clears; getPointerDiagnostics -> getManualAttachDiagnostics()
  ```

- **`TIE_COSINE`**: How near in cosine (about 0.08 degrees at the ray's centre) a Click Point and a Coupling Point must be to count as equally near the ray's centre.
- **`WAIT_FOR_OBJECTS_MS`**: How long (ms of updates with a tracked Motion Controller) Manual Attach's instance and attachment handler are waited for before the support gives up. Manual Attach makes them as the map loads, so in release 1.1.0.0 they are there at the first look.
- **`findManualAttach`, `if type(instance) ~= "table" then return nil, nil, nil, "g_manualAttach was n...`**: The instance and its handler are made when the map loads: a release or a load order that makes them later is waited for (the fifth value says so). The class and everything below are there when the mod's files are, and are final.
- **`findManualAttach`, `if type(handler.playerCanPerformManualAttachment) ~= "boolean" then`**: The candidate update wrap reads the handler's own copy of the flag.
- **`makeCandidateUpdate`**: The wrap of the class's candidate update, original first: after Manual Attach has picked its nearest candidate, while the ray is on a Coupling Point, the five candidate fields are its hitch's, so Manual Attach's prompts, context box and keys describe and act on the pointed hitch.
- **`wraps`**: The one function the Subsystem wraps, declared to the core's wrap helper (VRCompanionGameWraps.lua): on the class found in Manual Attach's environment (findManualAttach, which checks it with the rest). Giving up is ONE warning and lifts the wrap; map unload forgets it.
- **`tryInstall`, `state.waitedMs`**: Not there yet: look again next update, for a bounded time of updates with a tracked Motion Controller, then give up as for the rest.
- **`registerSubsystem "manualAttach"`**: Map unload only (the support installs itself from the update, not at map load, and has no commands). Order: it comes after Wheel Grab and the Virtual Gamepad in modDesc.xml, so it unloads before their resets, which it does not depend on.
- **`vehicleLookupProblem`**: Why the game's PTO output or hose target lookup on a vehicle is not what the module expects, or nil when it is (or the vehicle has none). The vehicle side of the PTO stub and the hose couplers is read from the game's PowerTakeOffs and ConnectionHoses specializations.
- **`collectHitches`**: Lists every hitch within Manual Attach's reach of the player, from the vehicles its detection has found: a free hitch with an Implement lined up (through its own helper and constants) and a hitch with an Implement on it, as its nearest-candidate search walks them. Returns why the support cannot go on when the game's side of a vehicle has another shape than expected.
- **`isPointable`**: Whether one of Manual Attach's attachments (the joint, PTO or hoses attachment) would show its prompt for the hitch's candidate. What it refuses in its perform path is not asked here: a refused hitch is pointed at and the pull shows Manual Attach's warning.
- **`considerPtoStubs`**: The hitch's Coupling Points on the vehicle's side that are not the hitch: its own PTO outputs for the joint that have an output node (a pass-through output, which resolves to another vehicle, has none of its own).
- **`selectFromRay`, `if hitch.implement ~= nil then`**: The PTO and the hoses act on the Implement on the hitch: with nothing attached there is nothing to connect.
- **`rayMayWork`**: Whether the Pointer Ray may work Coupling Points now. Manual Attach's own on-foot check (its flag) covers the cab, Hand Tools and Held Objects.
- **`VRCompanion:updateManualAttach`, `if wraps:problem() ~= nil or self.motionControllersEnabled == false or not an...`**: Looked for once, when a Motion Controller is in use: a player without them is never told.
- **`VRCompanion:updateManualAttach`, `state.installed, state.selected, state.selectedKind, state.count = false, nil...`**: The game's side of a vehicle is not what the support expects: it switches itself off, with one warning, for good.
- **`VRCompanion:updateManualAttach`, `state.count`**: An error in the game's side of a vehicle: nothing is selected this update. Each message is logged once (until the map is unloaded), not every frame.
- **`VRCompanion:updateManualAttach`, `if selected ~= nil and clickCos ~= nil and clickCos >= selected.cos - TIE_COS...`**: (Within TIE_COSINE of each other, floating point noise included, they are equally near: the Click Point's.)
- **`VRCompanion:updateManualAttach`, `mayUseTrigger`**: While the ray is on a Coupling Point the Pointer Hand's trigger is Manual Attach's: it reads released on the Virtual Gamepad, unless it was already held when the ray landed (it then keeps its binding and works nothing). Only the update the trigger went down works the Coupling Point. After `work(selected, kind)` succeeds, `state.selected` and `state.selectedKind` clear immediately so `makeCandidateUpdate` never overwrites live post-action attachment state for a frame (spectral-byte/FarmingSimVr-issues#247).
- **`VRCompanion:getManualAttachDiagnostics`, `which`**: Only the hitch is called a hitch: the PTO stub and the hose coupler are Coupling Points of one, on the same vehicle.

### `tests/animated_object_test_support.h`

- **the file**: What the Animated Objects tests (tests/test_animated_objects\*.cpp, ADR-0034, spec #180) share, on top of object_grab_test_support.h (the player on foot with the game's empty hands, the Lua helpers that read the rays, the macros that point a Motion Controller): the game's Animated Objects as a stub (StubAnimatedObject, shaped after AnimatedObject, AnimatedObjectActivatable and PlaceableAnimatedObjects in the SDK's gameSource.zip), the engine's raycastClosest over the boxes of its parts, and a fixture. Header-only (inline), in its own namespace; a test file brings it in with `using namespace animated_object_test;` inside its anonymous namespace.

  The stub is a chunk of its own (animated_objects.luau), like the Hand Tools' tool chunks, passed as StartOptions::extraStubs (AnimatedObjectStubs(variant)): it runs after the Companion Mod's files are loaded and before the map is, so the mod finds the game's classes when it looks. A door is made with StubAnimatedObject.door(options) after the scene exists (the zone is entered by the local player's root node, the node the scene's player stands on).

  What the stub offers (StubAnimatedObject.install, door, enterZone, ...) is described at the top of tests/stubs/animated_objects.luau.

- **`kHandX`**: A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (the same geometry as the Object Grab tests). The door's leaf is straight ahead of it, 1.65 m from the fingertip.
- **`VirtualGamepadAnimatedObjectsTest`**: A test of the Pointer Ray working Animated Objects, seen from what the game reads from the Virtual Gamepad: the mod started on a player on foot with the game's empty hands, the Virtual Gamepad on, the game clock set, and the game's Animated Objects in the game. The suite name is one the filter for the Companion Mod tests (VirtualGamepad\*) covers.

### `tests/manual_attach_test_support.h`

- **the file**: What the Manual Attach tests (tests/test_manual_attach\*.cpp, ADR-0035, spec #227, ticket #228) share, on top of animated_object_test_support.h (the player on foot with the game's empty hands, the Lua helpers that read the rays, the macros that point a Motion Controller): a stand-in for the Manual Attach mod (FS25_manualAttach, release 1.1.0.0, shaped after its src/ files), the vehicles it works on as scene nodes, and a fixture. Header-only (inline), in its own namespace; a test file brings it in with `using namespace manual_attach_test;` inside its anonymous namespace.

  The stand-in is two chunks (manual_attach.luau, manual_attach_vehicles.luau), like the Hand Tools' tool chunks, passed as StartOptions::extraStubs (ManualAttachStubs(variant)): they run after the Companion Mod's files are loaded and before the map is, so the mod finds Manual Attach when it looks. The vehicles are made after the scene exists. The game of a frame runs StubManualAttach.tick() (Manual Attach's own update: the "can attach by hand" flag from the player, and its handler's candidate update); this header defines HAND_TOOLS_PRE_FRAME_EXTRA_LUA to do so (and Interactive Control's update of its Click Points, when a test has them), so it is included FIRST by a test file.

  What the stand-in offers (StubManualAttach.install, tick, detect, ...) is described at the top of tests/stubs/manual_attach.luau, and the vehicles (vehicle, tractor, implement, lineUp, attach, ...) at the top of tests/stubs/manual_attach_vehicles.luau.

- **`kTractorAhead`**: A hand pointing at tracking (0.4, -0.2, -0.3) has its fingertip at world (-0.4, -0.2, 0.3) and the Pointer Ray runs along +Z (the same geometry as the Object Grab and Animated Objects tests): the rear hitch of the stand-in's tractor is straight ahead of it, 1.7 m away. A tractor on foot's side, within Manual Attach's reach of the player (who stands at the world origin).
- **`kRearPtoStub`**: Where the rays end on the tractor's Coupling Points of the rear hitch other than the hitch: its PTO stub, its hose target and its custom hose target. The hand that points at the hitch (kHandX, kHandY) points at these at kHandX / kHandY of kPtoHandY, kHoseHandX / kHoseHandY and kCustomHoseHandX / kHoseHandY (the tracking space is the world mirrored in X and Z).

### `tests/test_animated_objects.cpp`

- **the file**: Animated Objects, the Pointer Ray working a door or gate on foot (ADR-0034, spec #180, ticket #181), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with the game stood in by Lua stubs (the game's Animated Objects and the engine's ray cast over the boxes of their parts in animated_object_test_support.h). The test enters and leaves the zone the way the engine does (through the trigger callback), moves the fake Motion Controllers and pulls the trigger, and asserts what the game would see: the end of the Pointer Ray that was drawn, and the runs of the toggle handler the prompt's own button runs. Never the mod's own state. Prior art: test_object_grab_feedback.cpp (the ray drawn where the target is) and test_virtual_gamepad_cab_use.cpp (the trigger and the Virtual Gamepad). The pad rules and the gates are in test_animated_objects_pointer.cpp, the game giving the mod the wrong functions in test_animated_objects_wraps.cpp.

### `tests/test_animated_objects_nearer.cpp`

- **`VirtualGamepadAnimatedObjectsNearerTest.TheObjectInFrontStillWinsWhenTheEnginesCastDoesNotSeeIt`, `ASSERT_NO_FATAL_FAILURE(StartWithDoorAndObject("1.5"));`**: The engine's ray cast stops at any rigid body, so a pallet hides the door behind it from the cast. Where it did not (a collision the cast leaves out), the rule is the nearer of the two hits: the object's distance comes from Object Grab's report.

### `tests/test_manual_attach_wraps.cpp`

- **`VirtualGamepadManualAttachWrapsTest.ManualAttachsInstanceAppearingAfterTheFirstTrackedUpdateIsStillFound`**: Manual Attach creates its instance and its attachment handler when the map starts to load; a release or a load order that makes them later is waited for, for a while, before the support gives up with its one warning. (A missing function or a mod that is not loaded is final at once.)

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 46 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
