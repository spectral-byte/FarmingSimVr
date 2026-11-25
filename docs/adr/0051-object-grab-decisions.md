# Decisions behind Object Grab

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0033](0033-object-grab-moves-the-games-own-carry-to-the-hand.md).

## Context and Decision
This record holds the non-obvious decisions behind Object Grab: aiming the empty hands, the carry, the throw, the snap and the off-hand. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionObjectGrab.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Object Grab

  Lets the player pick up, carry and throw loose objects with the Motion Controllers (spec #156, ADR-0033). On foot with empty hands
  the game carries one object at a time with its "hands" hand tool (HandToolHands): it targets what the look ray meets
  (PlayerTargeter, fed by Player:getLookRay) and the player's pick-up button takes it.

  TERMS (CONTEXT.md): the Pointer Hand is the Motion Controller that points, the Pointer Ray runs from its index fingertip along the
  finger, empty hands are the game's hands tool (Player.hands) held, which it is whenever the player holds no Hand Tool
  (Player:getHeldHandTool() is nil then: the hands are not a Hand Tool the player holds).

  AIMING: while Object Grab is on, the local player is on foot in the first person view with empty hands, no menu takes the input, and
  the Pointer Hand is tracked and in use (it has a Pointer Ray), Player:getLookRay answers the Pointer Ray's ray with its origin moved
  back along the finger by OBJECT_GRAB_REACH_BEHIND. So everything the game targets by looking follows the hand: the hands' pick-up
  target, the prompts on animals, vehicles and trees, and the holders Hand Tools are taken from. A supported Hand Tool in a tracked
  Tool Hand aims first (Core asks Hand Tools before Object Grab, see "Who aims the look ray"). Otherwise, and before a grab, the ray
  is the game's own (the head's). Another player's look ray is never touched.
  The origin is moved back because the hands' target type only finds things between 0.5 m and the player's pickup distance of the ray's
  origin: a hand that touches an object, or has gone a little into it, would be below the minimum. (The spray can and the horse brush
  do the same.) The 10 cm beyond the minimum are for a fingertip that has gone into the object.

  THE POINTER RAY ON AN OBJECT: the targeter's closest target for the hands (closestTargetsByKey[HandToolHands]) is a node the hands
  can pick up when the game's own getCanPickUpNode says so (so the mass limit, the pickup distance and super strength are the game's).
  While the Pointer Hand aims and the target is one, the Pointer Ray is drawn, to the point where the targeter hit it, and the Pointer
  Hand gets one light pulse as the ray lands (again only after it left the object, or on another object). A Click Point or an Animated
  Object (a door) nearer along the ray is the ray's target instead: the object is then neither drawn nor pulsed nor grabbed.

  NO CENTRE CROSSHAIR: while the hand aims, the hands tool's onDraw is skipped for the local player (its crosshair and grab icon at the
  centre of the view are head-locked, and would point where the hand does not). The game's own function draws them otherwise.

  THE GAME'S PICK-UP BUTTON is the game's: it picks up what the targeter found, which is now what the hand points at. The game then
  carries it the game's way (along the look ray, so along the hand), and the grip leaves it alone.

  THE GRAB (#159): while the Pointer Hand points at an object the hands can pick up, and while a hand carries, Object Grab takes that
  hand's grip from the Virtual Gamepad (ADR-0028, takeVirtualGamepadInput(hand, "grip")), so the grip reads released on the pad. A
  squeeze the pad let the hand have calls the game's pickUpTarget with the targeter's target, with one pulse on the hand. A grip held
  before the ray landed keeps its binding and grabs nothing until it is let go and squeezed again. A squeeze at nothing grabbable is LB/RB.

  THE OFF-HAND (#161): either hand grabs in one squeeze, even with both in use. See "The off-hand": a physics ray cast along the other
  hand's finger only tells whether its grip may be taken; the squeeze makes it the Pointer Hand, the targeter runs again with its ray,
  and the game's pickUpTarget picks up what the targeter finds. A trigger pull still moves the Pointer Hand, as before.

  THE CARRY: the hand that grabbed carries the Held Object until the grip is let go (releaseHeldObject: a drop through the game's
  dropHeldItem, or a throw, see THE THROW) or the game itself ends the carry (getting into a vehicle, taking a Hand Tool): the carry ends cleanly, and the grip
  goes back to the pad once it is let go. While it lasts the hand is in use however still it is held, is never the Pointer Hand (the
  other hand can point), and the kinematic node and the rotation node of the game's carry are placed from the hand (see "Placing the
  Held Object"), keeping the object's place and angle in the hand as it was at the grab. A hand that is not tracked leaves them where
  they were, and has not let go; they follow it again when it is tracked. Nothing is felt while it carries.

  THE THROW (#160): letting go of the grip while the hand moves faster than OBJECT_GRAB_THROW_MIN_SPEED throws the Held Object through the
  game's throwHeldItemWithForceVector(dirX, dirY, dirZ, forceScalar, noEventSend), along the hand's travel, with the force scalar the
  hand's speed over OBJECT_GRAB_THROW_FULL_SPEED clamped to 0..1 (never harder than the game's full throw). Slower is the drop above.
  The hand's velocity is sampled in the world from its grip pose each update (see "THE HAND'S SPEED"); a Snap Turn or a Recenter does
  not count as a swing. After either release the grip is up, so grabbing again needs a fresh squeeze.

  ROTATE AND LEVEL (#162): the game's rotate actions and its level action (pallets and square bales) keep working as bound while the grip
  carries. They turn the rotation node, and the turn goes into the offset the hand holds the object with (see "Placing the Held
  Object"): the Held Object is turned on top of the wrist, and stays turned as the hand moves. A level action squares it and keeps its
  heading, which then follows the wrist like any other turn.

  THE SNAP (#162): without super strength, yanking a heavy Held Object breaks the game's joint, the game calls onHeldItemJointBroken with
  the joint's index and the hands let go. The carry ends, and the hand gets one sharp pulse (OBJECT_GRAB_SNAP_PULSE_*). A drop by the game
  itself passes no joint index and is silent. The grip, if still held, does not grab again until it is let go and squeezed again. With
  super strength the game makes the joint unbreakable and nothing here changes.

  WRAPS: Player.getLookRay (shared with Hand Tools: the wrap helper owns the one wrap and each holds it, so that one giving up does not take the
  look ray away from the other), HandToolHands.onDraw, HandToolHands.updateKinematicNode and HandToolHands.onHeldItemJointBroken (the
  class AND the local player's hands tool, which may hold a copy of the last two); getCanPickUpNode is only checked. When one is missing or has another shape
  at map load, Object Grab gives up: ONE warning ("Object Grab disabled: ..."), its wraps are lifted, and picking up is the
  game's. Hand Tools are not affected. A game without HandToolHands (no hands to aim) is not a failure: nothing is wrapped.

  PUBLIC SURFACE
    VRCompanion.objectGrabEnabled              boolean, default true, saved (`vrObjectGrab on|off`)
    VRCompanion.OBJECT_GRAB_REACH_BEHIND       how far (m) the look ray's origin is moved back along the finger
    VRCompanion.OBJECT_GRAB_LAND_PULSE_MS / _STRENGTH   the light pulse on the Pointer Hand as the ray lands on something grabbable
    VRCompanion:peekObjectGrabRayTarget() -> hasTarget, x, y, z, distance   the Pointer Ray's object target and how far along the ray
                                       from the fingertip it is (called by updatePointerRay); only looks: no pulse, no grab
    VRCompanion:settleObjectGrabRayTarget(isTarget)   the answer for the last peek: the object is what the ray ends on (the landing
                                       pulse, and a squeeze there grabs it) or something nearer is (nothing happens)
    VRCompanion:updateObjectGrab(dt)    once per Motion Controller update, after the hands are polled and before the Pointer Hand is
                                       chosen: the grip edges, and the carry (records the offset, lets go, ends)
    VRCompanion:isHandCarryingHeldObject(hand) -> boolean   the hand carries a Held Object it grabbed (stays in use, never points)
    VRCompanion:isObjectGrabSqueeze(hand) -> boolean   the hand, not the Pointer Hand, was squeezed this update while pointing at
                                       something grabbable: it becomes the Pointer Hand (asked by choosePointerHand)
    VRCompanion.OBJECT_GRAB_ANSWER_WAIT_MS   how long such a squeeze waits for the targeter's answer to the new ray
    VRCompanion.OBJECT_GRAB_GRAB_PULSE_MS / _STRENGTH   the pulse on the hand that grabs
    VRCompanion.OBJECT_GRAB_THROW_MIN_SPEED / _FULL_SPEED (m/s) / _WINDOW_MS   when a release throws, the speed of a full throw, the
                                       span the hand's speed is measured over
    VRCompanion:getObjectGrabLastRelease() -> kind ("throw"|"drop"), speed, dirX, dirY, dirZ, force | nil   the last release
    VRCompanion.OBJECT_GRAB_SNAP_PULSE_MS / _STRENGTH   the sharp pulse on the hand whose hold snaps
    VRCompanion:setObjectGrabEnabled(enabled, save)
    VRCompanion:saveObjectGrabSetting(xmlFile, xml) / loadObjectGrabSetting(xmlFile, xml)   settings chain
    VRCompanion:getObjectGrabDiagnostics() -> string          the state of Object Grab, one line
    VRCompanion:resetObjectGrab()                              map unload (registered with the core as the "objectGrab" subsystem, see the
                                                               end of the file: the map-load hook wraps the classes again, the registry
                                                               adds and removes the commands)
    VRCompanion:consoleCommandObjectGrabDiagnostics()   logs the fields one tagged line each, returns them as one line; reads only
    Console: vrObjectGrab [on|off], vrObjectGrabDiagnostics

  Hooks into the rest of the mod:
    Core               registerLookRayAimer("objectGrab", ...); GameWraps (the wrap helper) holds the shared "lookRay" wrap
    MotionControllers  updatePointerRay -> peekObjectGrabRayTarget() / settleObjectGrabRayTarget() (after Interactive Control, with
                       the Animated Objects module in between: the nearer of the two wins); updateMotionControllers ->
                       updateObjectGrab(dt) on every path; isHandCarryingHeldObject(hand) keeps the hand in use and out of the Pointer
                       Hand candidates; the saved setting and the diagnostics chain through the
                       Motion Controller functions of the same names
    VirtualGamepad     takeVirtualGamepadInput(hand, "grip"); VRBridge.getVirtualGamepad() for what the pad reads as squeezed
  ```

- **`VRCompanion.OBJECT_GRAB_REACH_BEHIND`**: The look ray starts this far (m) behind the fingertip: the game's 0.5 m minimum distance of the hands' target type, and 10 cm for a fingertip that has gone into the object.
- **`VRCompanion.OBJECT_GRAB_ANSWER_WAIT_MS`**: How long (ms) an off-hand's squeeze waits for the targeter's answer to the new ray (the game's ray cast is asynchronous).
- **`VRCompanion.OBJECT_GRAB_THROW_MIN_SPEED`**: THE THROW (#160), speeds in m/s of the hand in the world: letting go of the grip while the hand moves faster than OBJECT_GRAB_THROW_MIN_SPEED throws the Held Object, below it the release is a drop. The throw's force scalar is the hand's speed over OBJECT_GRAB_THROW_FULL_SPEED, clamped to 0..1, so a hand at or above that speed throws as hard as the game's own full throw. The hand's speed is measured over the last OBJECT_GRAB_THROW_WINDOW_MS.
- **`WRAP_SPECS`**: Wraps The game functions Object Grab wraps or relies on, declared to the core's wrap helper (VRCompanionGameWraps.lua, which says what each field means). The wrappers (`make`) are attached by the sections that own them, below. getLookRay is the shared look ray wrap, which Hand Tools hold too; getCanPickUpNode is CHECK-ONLY.
- **`wraps`**: Giving up (ONE warning, every wrap lifted, so that picking up is exactly the game's) leaves Hand Tools alone: they hold the look ray wrap themselves.
- **`INSTANCE_WRAPS`**: The functions of the hands tool that the game registers on its type, and that the tool may hold its own copy of ("Placing the Held Object"): wrapped on the local player's hands tool itself for as long as it is the hands, unless the class wrap reaches it.
- **`handToolInHand`**: Whether the player holds a Hand Tool (the game's Player:getHeldHandTool). The game's hands are NOT one: with empty hands it answers nil, and the hands are Player.hands (the game's holders and winches read it that way).
- **`emptyHands`**: The game's hands tool (Player.hands, with spec_hands) while the player holds no Hand Tool and the hands are held (the game holds them whenever no tool is), or nil.
- **`padGripHeld`**: The grip The grip is the Virtual Gamepad's LB / RB (ADR-0028), and what counts as squeezed is what the pad reads: the Injector owns the threshold and its hysteresis, and an untracked hand's grip is not held. Object Grab takes a hand's grip from the pad while that hand points at something the hands can pick up and while it carries, and acts on a squeeze only when the pad let it have the grip (a grip already held when the use would begin keeps its binding, and does not grab, until it is let go).
- **`qrotate`**: Placing the Held Object The game carries the object with two nodes: a kinematic node (a physics body the object's joint hangs from) and a rotation node under it, which the joint's frame follows. The game places the kinematic node along the look ray, level, every update (HandToolHands:updateKinematicNode). While the hand carries, Object Grab places them from the hand instead, so that the object keeps the place and angle it had in the hand when it was grabbed (OFFSET), and moves and turns with the wrist. THE MULTIPLAYER SHAPE (from the game source): the owning client streams to the server only the kinematic node's world position, its level heading and the rotation node's WORLD rotation. The rotation node's position under the kinematic node is never streamed: the server sets it at the grab. So the kinematic node stays level, the wrist's tilt goes into the rotation node, and the kinematic node is placed so that the rotation node's offset (read each update: it is the game's) lands where the hand holds the object. The server applies the rotation node to the joint with setJointFrame, as the game's rotate action does; a single player game is the server and has no stream, so it does that itself. The dirty flag is raised, as the game's own update raises it. THE GAME'S ROTATE AND LEVEL ACTIONS (#162) turn the rotation node, and the game only does that between two of our placements: the turn is folded into the offset (absorbTurn), on top of how the wrist holds the object, and kept as the hand moves. THE OFFSET is recorded once the rotation node is at the object. The game moves it there itself (orientRotationNodeToItem): at once on a client, but on the server only when the joint is made, a few physics frames after the pick-up. Until then the game's own placement runs.
- **`recordGrabOffset`**: Records where the Held Object is in the hand's frame (carry.offset), from the rotation node, which the game puts at the object. Not yet while the game has the rotation node still to move (the server's joint is made a few physics frames after the pick-up), or the hand cannot be placed.
- **`TURN_EPSILON`**: The game's rotate and level actions turn the rotation node between two placements (the game is the only one to touch it). Whatever turned it is kept: the turn is folded into the offset, so that the Held Object stays turned in the hand as the hand moves. The turn is measured against the hand as it was at the last placement, so it is not mixed up with the hand's own movement since (and a turn made while the hand is not tracked is kept for when it is again).
- **`placeFromHand`**: Places the kinematic node and the rotation node of the carrying `tool` from the hand: the object at the hand's pose times the offset. A hand that cannot be placed (tracking lost) leaves the nodes where they were.
- **`placeFromHand`, `local fx, _, fz = qrotate(qx, qy, qz, qw, 0, 0, 1)`**: the kinematic node's level heading: the way the object faces (when it faces straight up or down it has no heading to give, and the last one is kept). The rotation node is placed in the world, so the heading does not matter to where the object is.
- **`placeFromHand`, `local lx, ly, lz = engine("getTranslation")(rotation)`**: the kinematic node goes where the rotation node's offset from it (the game's) lands on the place the hand holds the object
- **`mayPlaceNodes`**: The guards of the game's updateKinematicNode that placing from the hand keeps: the hands are held, and their carrying player is the owner (the game's own function does nothing otherwise).
- **`WRAP_SPECS.handsUpdateKinematicNode.make`**: The wrap of the game's updateKinematicNode: the carry places the nodes from the hand once the offset is recorded, and the game places them otherwise (another player's hands, a pick-up by the game's own button, a grab not settled yet, hands not held).
- **`wrapInstance`**: The game registers updateKinematicNode (and onHeldItemJointBroken) on the hands tool's type, and the type's functions may be copied onto the tool: a wrap of the class does not reach a tool that holds its own copy (the trap of Wheel Grab's steering wrap). So the local player's hands tool is wrapped itself, for as long as it is the hands, unless the class wrap reaches it.
- **`WRAP_SPECS.handsJointBroken.make`**: The wrap of the game's onHeldItemJointBroken: the game's joint break report passes the joint's index, whereas its own letting go (dropHeldItem, which getting into a vehicle or taking a Hand Tool end in) passes none. A break of the hold of the hand that carries is a snap: updateCarry gives the hand its pulse.
- **`startGrab`**: The game picks the target up (pickUpTarget, with the targeter's own target: the game decides, and sends the event) and the hand carries it from now on. One pulse on the hand that grabbed.
- **`OFF_HAND_MINIMUM_DISTANCE`**: The off-hand While both hands are in use with empty hands, the hand that does not point is checked with a physics ray cast along its finger (the Pointer Ray's ray, from the same place behind the fingertip as the look ray), ONLY to learn whether it points at something the game's getCanPickUpNode accepts. While it does, its grip is taken from the pad, so that squeezing it does not reach the game first (the held-input rule would hand that squeeze to the binding). The squeeze makes that hand the Pointer Hand (see choosePointerHand), the targeter is run again with its ray, and the game's pickUpTarget picks up what the TARGETER finds. The check never decides what is picked up: when the targeter finds nothing the game can pick up, nothing is. The targeter's ray cast is asynchronous in the game, so its answer to the new ray may come a few updates later: the squeeze waits for it (OBJECT_GRAB_ANSWER_WAIT_MS) for as long as the grip stays down.
- **`OFF_HAND_MINIMUM_DISTANCE`**: the hands' target type finds nothing nearer to the ray's origin (HandToolHands:onHeldStart)
- **`pointsAtGrabbable`, `if self:headRelativePoseToWorld(tx, ty, tz, 0, 0, 0, 1, checkPose) ~= true th...`**: tracking space to the world: a pose with no rotation of its own comes out as the rotation of the tracking space
- **`VRCompanion:isObjectGrabSqueeze`**: Asked by the Pointer Hand's choice: the hand's grip was squeezed this update as it points at something the hands can pick up, and the pad let it have the grip. The hand becomes the Pointer Hand, and grabs there.
- **`runTargeterAgain`**: Runs the targeter again, now that the Pointer Ray is the new Pointer Hand's. What it found for the hands before (with the ray of the hand that pointed) is forgotten first, so that the squeeze can only pick up what the new ray finds.
- **`SWING_RIG_JUMP_DISTANCE`**: THE HAND'S SPEED: the hand state carries no velocity, so the grip pose of the carrying hand is sampled in the world at each update (sampleSwing) and the speed is its travel over the last OBJECT_GRAB_THROW_WINDOW_MS. It is measured in the world, so the hand moving with the player (walking) counts, and the world moving round the hand does not: a Snap Turn turns the player's view and a Recenter moves the tracking space, and the hand's place in the world jumps with either though it did not move. The tracking space's own place in the world (where its origin and axes are) shows the jump: when it changes by more than SWING_RIG_JUMP_DISTANCE or SWING_RIG_JUMP_ANGLE between two updates, the samples before it are forgotten (the hand is not known to be moving until it has been seen moving since).
- **`sampleSwing`, `if now == nil then`**: no clock: the hand's speed is not known, and what was sampled before it went is stale
- **`releaseHeldObject`**: The grip of the carrying hand was let go: what happens to the Held Object. The decision is this one function. A hand moving faster than OBJECT_GRAB_THROW_MIN_SPEED throws it, through the game's throwHeldItemWithForceVector along the hand's travel with the force scalar the speed over OBJECT_GRAB_THROW_FULL_SPEED (0..1, never harder than the game's full throw); otherwise it is dropped, through the game's dropHeldItem. What was done is kept for the diagnostics (getObjectGrabLastRelease).
- **`carryHasEnded`**: Whether the carry has ended without the grip being let go: the game dropped the object itself (getting into a vehicle, taking a Hand Tool, the object gone), or Object Grab or the Motion Controllers were switched off, or the hands are not the hands any more.
- **`mustDropOnEnd`**: Whether Object Grab itself ended the carry while the game's hands still hold the item: it was switched off, the Motion Controllers were, or it gave up. The game would go on carrying it along the head's look ray, with the grip back on the pad, so the hands let go (the game's dropHeldItem). A carry the game ended itself (the hands put away, the item gone, a snap) has nothing left to drop.
- **`gripWasLetGo`**: Whether the carrying hand let go of its grip. A hand that is not tracked has not (nothing is known of it until it is tracked again).
- **`updateCarry`, `state.carry`**: The grip goes back to the pad once it is let go (the Virtual Gamepad keeps what a use took off it), and a grip still held does not grab again: a grab needs the grip to go down. A snap is felt; a drop by the game is not.
- **`VRCompanion:peekObjectGrabRayTarget`, `return true, x, y, z, (tonumber(target.distance) or 0) - self.OBJECT_GRAB_REA...`**: The targeter measures from the look ray's origin, which is OBJECT_GRAB_REACH_BEHIND behind the fingertip.
- **`VRCompanion:settleObjectGrabRayTarget`**: The caller's answer for what the last peek found: whether the object is the ray's target. Then the Pointer Hand gets one light pulse as it lands, and squeezing its grip there grabs the object (see "The grab"). When it is not (a Click Point or an Animated Object is nearer), nothing of that happens, and the ray landing on the object again later is felt as a new landing.
- **`drawIsHeldBack`**: No centre crosshair The hands tool's onDraw is read off the specialization class by the game's event dispatch at every call, so the wrap is on the class.
- **`HAND_NAMES`**: Diagnostics vrObjectGrabDiagnostics only READS: no wrap is installed and no state is touched (the targeter's and the game's own getters are asked, and the hand is placed in a scratch table). The game's limits are read from the hands tool each time, because the constants are stripped from the game's source and the player can turn super strength on.
- **`statusField`**: Whether Object Grab is on, and when it is not working why not: the setting, or the reason the Pointer Hand does not aim (Motion Controllers off, a wrap that failed, in a vehicle, a Hand Tool in hand, a menu, no Pointer Hand...). Carrying counts as working.
- **`registerSubsystem "objectGrab"`**: Map load: the classes are wrapped again (resetObjectGrab lifted the wraps with the last map), then checked. Map unload runs in reverse file order, so Object Grab lifts its wraps before Hand Tools do (their classes are different; they share only the look ray wrap, which the wrap helper lifts when the last holder lets go).

### `tests/object_grab_test_support.h`

- **the file**: What the Object Grab tests (tests/test_object_grab_\*.cpp, ADR-0033) share, on top of hand_tools_test_support.h (the player on foot, the frame the game runs before the Companion Mod's update, the fake hands): the game's empty hands as a stub (StubHandTool.hands, shaped after HandToolHands in the SDK's gameSource.zip), loose objects with a mass that its targeter can hit (StubHandTool.looseObject), the Lua helpers that read the rays, and the macros that point a Motion Controller. Header-only (inline), in its own namespace; a test file brings it in with `using namespace object_grab_test;` inside its anonymous namespace.

  The stubs are a chunk of their own (hand_tool_hands.luau), like the Hand Tools' tool chunks, so that the later Object Grab tickets extend it without touching the Hand Tools' stubs. A test passes HandsStubs(variant) as StartOptions::extraStubs (concatenated after xml_settings.luau, if it uses both), and the player's hands are taken by HoldHands (the game's empty hands are Player.hands, held while the player holds no Hand Tool: Player:getHeldHandTool() is nil then, and answers the Hand Tool once one is held).

  What the stub offers (StubHandTool.installHands, hands, looseObject, ...) is described at the top of tests/stubs/hand_tool_hands.luau.

- **`VirtualGamepadObjectGrabTest`**: A test of Object Grab that reads what the game reads from the Virtual Gamepad: the mod started on a player on foot with the game's hands, the Virtual Gamepad on and the game clock set (the in-use timeout is measured on it). The suite name is one the filter for the Companion Mod tests (VirtualGamepad\*) covers.
- **`VirtualGamepadObjectGrabTest`, `Pad`**: What the game reads from XInput slot 3, the highest free slot of a machine without real pads.

### `tests/test_object_grab_aim.cpp`

- **`CompanionModTest.OnFootWithEmptyHandsTheGameHoldsNoHandToolAndTheHandAimsThem`**: The game's empty hands are Player.hands, held while no Hand Tool is: Player:getHeldHandTool() is nil then (the game's holders and winches read it that way), and answers the Hand Tool once one is held.
- **`CompanionModTest.ATouchingHandPicksUpWhatItTouchesAndAnObjectOutOfReachIsNotPickedUp`**: A hand that touches the object: its fingertip is at the surface (or has gone a few centimetres into it). The ray starts further back so that the hit is not under the 0.5 m minimum distance of the hands target type.
- **`CompanionModTest.TheEmptyHandsAreNotAnUnsupportedHandTool`**: The game counts its empty hands as a hand tool; here they are none, so the Hand Tools' diagnostics do not call them an unsupported tool.

### `tests/test_object_grab_carry.cpp`

- **`VirtualGamepadObjectGrabCarryTest.TheRotationNodeGoesToWhereTheHandHoldsTheObjectWhateverTheKinematicNodesOffset`, `ASSERT_EQ(RunLuauChunk(L, "setTranslation(rotationNode(), 0.3, -0.2, 0.5)", "...`**: The rotation node's offset from the kinematic node is set by the game's server at the grab (the node is not at the kinematic node's origin): the kinematic node is placed so that the rotation node still lands in the hand.
- **`VirtualGamepadObjectGrabCarryTest.TheDirtyFlagIsRaisedEachUpdateAndTheJointFrameIsSetWhenTheGameHasAJointOnTheServer`, `ASSERT_EQ(RunLuauChunk(L, "hands.isServer = false; StubHandTool.jointFrames =...`**: A client sends the rotation node's pose in the stream and the server applies it: it does not set the joint frame itself.
- **`VirtualGamepadObjectGrabCarryTest.SwitchingObjectGrabOffWhileCarryingDropsTheObjectThroughTheGameOnce`**: A carry that Object Grab itself ends The game keeps carrying along its own look ray if nobody lets go: the object would jump to the head's ray with the grip still on the pad. So when Object Grab ends the carry (it is switched off, the Motion Controllers are, or it gave up) the hands drop what they hold.
- **`VirtualGamepadObjectGrabCarryTest.TheCarrysPlacementLeavesTheNodesAloneForHandsThatAreNotHeldAsTheGamesOwnDoes`**: The game's guards stay The game's updateKinematicNode does nothing for hands that are not held, or whose player is not the owner. Taking over the placement must not place the nodes then either (a frame can run between the hands being put away and the carry being seen to end).

### `tests/test_object_grab_off_hand.cpp`

- **`kRaycastStub`**: The engine's physics ray cast the mod uses for the check (raycastClosest, synchronous, calling back with the node it hit), over every object the game knows (StubHandTool.looseObjects), and a targeter whose answer to a look ray comes one update late (the real targeter's ray cast is asynchronous).

### `tests/test_object_grab_throw.cpp`

- **`kKeepListenerStill`**: The Cyclopean Audio Listener Node (the head's place in the world) stays where it is from now on: the mod's camera chain may move it with the head pose every frame, and this makes it ignore that.
- **`VirtualGamepadObjectGrabThrowTest.AJumpOfTheTrackingSpaceInTheWorldJustBeforeTheLetGoIsNotASwing`**: The Recenter above only drops the mod's own memory of the hand's movement. The tracking space moving UNDER the player with the head placement (the Cyclopean Audio Listener Node) staying put makes the hand's place in the world jump without the hand moving: the mod sees it in where the tracking space's origin is, and must not take it for a swing.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 52 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
