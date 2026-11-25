# Decisions behind VR Actions, Locomotion and the Seat Offset

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0011](0011-openxr-reference-space-recentering.md), [ADR-0012](0012-in-engine-snap-turn-locomotion.md).

## Context and Decision
This record holds the non-obvious decisions behind the VR actions and their registration with the game, Snap Turn and locomotion, and the Seat Offset. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionInput.lua`

- **`VRCompanion:getActionEventsContextName`**: The input context the VR actions belong in: the vehicle's while controlling one, otherwise the player's on foot. registerActionEvent adds to the context that is current, and at map load that is the loading screen's menu context, which gameplay never uses: events registered there exist in g_inputBinding.events but never fire.
- **`VRCompanion.ACTION_EVENTS`**: The VR action events, one row each: the field holding the registered event id, the input action, the handler (named, so it is looked up on VRCompanion when registering), the i18n text key and its fallback text, and the field remembering that the event was logged this map. Adding an event means adding a row.
- **`VRCompanion:registerActionEvents`, `contextName`**: Events in another context than the gameplay one (the menu context at map load, or the vehicle's after getting out) move to it: they still exist there, so the check below alone would never notice.
- **`VRCompanion:registerActionEvents`, `actionEvents`**: FS25 keys `events` by the event id registerActionEvent returns; `actionEvents` is keyed by action name, so it must not be used for id lookups.
- **`VRCompanion:registerActionEvents`, `print("[FS25_VR] [Warning] g_inputBinding.events is missing; action events wi...`**: Without it the periodic check cannot see registered events and re-registers them every time.
- **`VRCompanion:registerActionEvents`, `if force and self.actionEventsContextName ~= nil and type(inputBinding.remove...`**: A forced registration takes the old events out first, so a context never holds two of an action
- **`VRCompanion:registerActionEvents`, `inputBinding:beginActionEventsModification(contextName)`**: Register into the gameplay context without switching to it, as PlayerInputComponent does from the UI context
- **`VRCompanion:subscribeToInputBindingChanges`**: The game ties an action event to its action's bindings when the event is registered, so a binding changed in the Controls screen only reaches an event registered after the change: Snap Turn kept firing from the right stick after the player had moved it to smooth look. The game's own owners of events (the on-foot player, an entered vehicle) register theirs again on MessageType.INPUT_BINDINGS_CHANGED; the VR events do the same, forced, at the next update.
- **`VRCompanion:consoleCommandSetHudSafeArea`, `if not self.is2DViewActive then`**: 2D Floating Window Mode keeps the full-screen anchors; the Safe Area applies on returning to VR
- **`VRCompanion:consoleCommandSetHudGroup`, `if not self.is2DViewActive then`**: 2D Floating Window Mode keeps the engine's layout; the margins apply on returning to VR
- **`VRCompanion:updateActionEvents`**: Keeps the VR action events registered: across context transitions (the vehicle's context and the player's on foot), every 30 ticks or when one is missing, and all of them again after a binding change. The core's update calls it between the HUD convergence and the vehicle transition. The transition registers the events again, forced, so on the frame the vehicle changes the game sees this check's registration and then the transition's, which takes out what came before. Called after the transition, the check would find every event active and register nothing: the events end up the same, with one registration on that frame instead of two.
- **`registerSubsystem "input"`**: Input in the subsystem registry Map load: the VR actions (Recenter, HUD Toggle, ...) are registered with the game, and again whenever the player changes a binding. This runs after the core read settings.xml. Registering reads the input context of the vehicle being controlled and the game's text, and reading the settings touches no action event, so neither needs the other first. No update hook: the check of the events (updateActionEvents, above) is a step of the core's update. Map unload: Input lets go of the binding-change message and forgets the event ids.

### `mods/FS25_VRCompanion/src/VRCompanionLocomotion.lua`

- **`VRCompanion:recordHeadTrackingForward`**: Records the cab's forward yaw on its head tracking node, once: the node's own yaw before the mod first turns it, or the camera's neutral yaw when the node has none. Never read again, since from then on the node is turned by the look yaw every frame and reading it would take that up as forward.
- **`VRCompanion:recordOutsideCameraDefaults`**: Records an Outside Camera's default orbit yaw and pitch ("forward" for the Recenter Action) the first time it becomes active, the way the cab records its forward direction. The engine's own origRotX/origRotY, when present, already are that default. No per-vehicle orbit memory: the game's camera-reset setting owns it.
- **`VRCompanion:onVehicleCameraChanged`, `if veh == self:getControlledVehicle() then`**: Any camera switch of the controlled vehicle (cab or Outside Camera, either direction) jumps to an unrelated viewpoint, so the temporal upscaler must drop its history.
- **`VRCompanion:snapTurnOutsideCamera`**: Snap Turn in an Outside Camera steps the engine's orbit yaw around the vehicle (VehicleCamera.update applies cam.rotY to its rotate node). A fixed Outside Camera (hitch, header) has no orbit to step.
- **`VRCompanion:snapTurnOutsideCamera`, `cam.rotY`**: The orbit yaw turns the opposite way to the in-cab view yaw, so the sign is flipped
- **`VRCompanion:getCabRestPose`**: Where the head rests in a cab, in the in-cab camera's parent space (camParent): the position (x, y, z) and the base rotation (qx, qy, qz, qw) the head pose is composed on. The in-cab camera update and the Recenter Action's realign to the seat both take it from here, so the head tracking node path and the fallback without one are worked out in one place. With a head tracking node, that is the node's position and rotation (the rotation read as a quaternion, see getRotationInSpace) and targetHeadRotY is the node's yaw. Without one, it is the camera's own eye point turned by the camera's neutral yaw (cam.origRotY, or defaultRotY when the camera has none yet: the camera update takes 0 there and the realign pi) plus lookYaw. The Seat Offset moves the position either way. Also gives what cabPointFromHead needs to carry a head-space vector into the cab: rest.yaw and, with a node, rest.node. The returned table is reused: read it before asking again.
- **`VRCompanion:getCabRestPose`, `rest.seatX, rest.seatY, rest.seatZ = self:getCabSeatOffset(cam, camParent)`**: The Seat Offset moves the rest position in the cab, whatever the look yaw (VRCompanionSeatOffset.lua)
- **`VRCompanion:cabPointFromHead`, `local cx, cy, cz = localToLocalFn(rest.node, camParent, x, y, z)`**: From the head tracking node, which sits at the vehicle's own eye point: the Seat Offset moves it on
- **`VRCompanion:resetOutsideCameraOrbit`**: Swings an Outside Camera's orbit back to forward: orbit yaw and pitch go back to the camera's default, the way the cab's Recenter resets mouse look. Zoom is left alone.
- **`VRCompanion:recenterView`**: What the Recenter Action does. alwaysRecenterTracking re-zeroes the tracking space whatever the Recenter mode is (VehicleOnly leaves it alone otherwise): the Seat Set Action needs the head back at the tracking space's origin to rest where it leaned.
- **`VRCompanion:recenterView`, `self:resetOutsideCameraOrbit(vehCam)`**: A fixed Outside Camera (hitch, header) has no orbit to reset
- **`registerSubsystem "locomotion"`**: Locomotion in the subsystem registry Map unload: the rotation states kept for each vehicle are forgotten (after the core lifted the camera hooks that read them). Snap Turn and the Recenter Action are driven by the Input subsystem's action events and the console commands in the core's list; the core tells Locomotion when the controlled vehicle changes (onVehicleTransition), which is part of its own update.

### `mods/FS25_VRCompanion/src/VRCompanionSeatOffset.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Seat Offset

  How far the head's rest position in a cab is moved from the vehicle's own in-cab eye point (spec #166, ticket #168). Each in-cab
  camera of each vehicle model keeps its own: forward, up and right, in metres, each axis clamped to +-0.5 m. See CONTEXT.md.

  PUBLIC SURFACE
    VRCompanion:getSeatOffset(model, cameraIndex)                 forward, up, right (0, 0, 0 when none is set)
    VRCompanion:setSeatOffset(model, cameraIndex, f, u, r)        clamps, stores (0 0 0 is dropped), returns the axes it clamped
    VRCompanion:getCabSeatOffset(cam, camParent)                  the offset of the vehicle and in-cab camera `cam` as a translation in the
                                                                  camera's parent space (x, y, z), 0 0 0 where `cam` is not an in-cab
                                                                  camera of a vehicle; the cab camera path adds it to the head's rest position
    VRCompanion:getSeatOffsetTarget(cam)                          model, cameraIndex of an in-cab camera, nil otherwise
    VRCompanion:onSeatSetAction()                                 the Seat Set Action: adds the head's lean to the offset, recenters, saves
    Console: vrSeatOffset [<forward> <up> <right> | reset]
    Registered with the core: the console command only; the registry adds it on map load and removes it on map unload

  IDENTITY: the vehicle model is the vehicle's configFileName (two vehicles of one model, mod vehicles included, share an offset), the
  camera is its index in the vehicle's camera list (vehicle.spec_enterable.cameras). A passenger camera is another index.

  FRAME: the vehicle's own frame (its root node), where forward is +Z and the vehicle's left is +X (the engine's vehicles; the head
  pose's x is to the right). So the offset (forward f, up u, right r) is the vector (-r, u, f) there. The in-cab camera hangs from a
  free node in world space, not from the vehicle, so the cab camera path turns that vector into the camera parent's space every update
  (getCabSeatOffset): the offset follows the vehicle's heading. It is not rotated by look yaw or a Snap Turn. No rotation component.

  The Seat Set Action (VR_SET_SEAT, onSeatSetAction): in an in-cab camera, takes the head's lean as the new rest position (ticket #170).
  SAVING: the saveSettings / loadSettings hooks (saveSeatOffsetSettings / loadSeatOffsetSettings); a vrSeatOffset change and a Seat Set press save the settings at once.
  ```

- **`VRCompanion:getSeatOffsetFrameNode`**: The node whose frame the Seat Offset is measured in: the vehicle's own root node (forward +Z, left +X), which turns with the vehicle. The in-cab camera's parent is not it: the game hangs cab cameras from a free node in world space.
- **`vectorBetweenFrames`**: A vector from one node's frame into another's (the difference of two points, so the nodes' own positions drop out). Unchanged when either node or the engine call is missing.
- **`VRCompanion:getCabSeatOffset`**: The Seat Offset of the in-cab camera `cam` as a translation in camParent's space (the in-cab camera's parent): forward is the vehicle's forward, wherever the vehicle points in the world.
- **`VRCompanion:consoleCommandSeatOffset`, `self:signalDiscontinuityFlush()`**: The next camera update places the head with it; the view jumps, so the upscaler drops its history
- **`VRCompanion:getHeadLeanInCab`**: How far the head is from where it would rest, as the Seat Offset's forward, up and right of the vehicle: the head pose's position carried into the cab the way the cab camera path does (through the head tracking node's rotation, look yaw included, else the camera's own eye point turned by the view's yaw), then turned from the in-cab camera's parent space into the vehicle's frame.
- **`VRCompanion:getHeadLeanInCab`, `headYaw`**: As the cab camera update (VRCompanionStereo.lua) places the head, so a camera without a neutral yaw yet defaults to 0 here too
- **`VRCompanion:onSeatSetAction`**: The Seat Set Action: in an in-cab camera, makes where the head is now the head's rest position (see CONTEXT.md). The lean is added to the Seat Offset, each axis clamped; the view then recenters as the Recenter Action does, always re-zeroing the tracking space, so the head rests where it leaned. The settings are saved. Only a clamped axis is shown: a short blinking warning.
- **`VRCompanion:onSeatSetAction`, `if self.is2DViewActive or self:isMenuQuadShown() or self:doesMenuTakeInput() ...`**: Only the head-tracked view of an in-cab camera, with no menu shown: anywhere else it is a no-op. A menu is shown when its quad is (what the cab camera hook stands down for) or when it takes the input (also a Stereo Dialog, which has no quad).
- **`VRCompanion:saveSeatOffsetSettings`, `models`**: In a stable order, so the file doesn't shuffle between saves
- **`registerSubsystem "seatOffset"`**: Map load and unload: only the console command, which the registry adds and removes. The offsets are saved and loaded by the settings hooks (saveConfiguration / loadConfiguration run them), and the cab camera path reads them every update. Order: none matters against the other subsystems.

### `tests/test_action_events.luau`

- **`local subscriptions`**: The game ties an action event to its action's bindings when the event is registered, so a binding the player changes in the Controls screen (Snap Turn taken off the right stick for smooth look) reaches the VR events only once they are registered again. They listen for the game's INPUT_BINDINGS_CHANGED, as the on-foot player and an entered vehicle do.
- **`TheFrameTheControlledVehicleChangesRegistersTheEventsBeforeTheTransitionDoesAndThenItDoesAgain`**: The frame the player gets into a vehicle, when it is also the periodic check's tick: the update looks after the events first (they are in the on-foot context, and the vehicle's is the one now), and the vehicle transition registers them once more after that. Both registrations, in that order, are what the engine saw before the subsystem registry moved the update's step.

### `tests/test_seat_offset.luau`

- **the file**: Test Suite for the Seat Offset (spec #166, ticket #168): how far the head's rest position in a cab is moved from the vehicle's own in-cab eye point. Drives the whole Companion Mod through what the engine calls (the in-cab camera update, the Recenter Action, the console command, the map load and unload) over a mock scene graph, and asserts on what the engine would see: where the engine camera, the eye camera and the audio listener are, the replies and notifications, and the bridge calls. Axes: forward, up and right, in the in-cab camera's parent space. There forward is +Z and the vehicle's left is +X (the engine's vehicles are modelled that way), so an offset of (forward f, up u, right r) is the translation (-r, u, f).
- **`VirtualHandsAndPointerRayFollowTheHeadTheSeatOffsetMovesWithNoChangeOfTheirOwn`**: The Virtual Hands and the Pointer Ray are carried from the Cyclopean Audio Listener Node (headRelativePoseToWorld, shared by both): a Motion Controller's pose is taken relative to the head and put back into the world from the listener. So they follow the head the Seat Offset moves, with the player's real hands staying where they are relative to it, and need nothing of their own.
- **`ROOT`**: The in-cab camera hangs from a free node in world space, not from the vehicle: so forward is the vehicle's forward, wherever it points

### `tests/test_seat_set.luau`

- **the file**: Test Suite for the Seat Set Action (spec #166, ticket #170): in a cab the player leans to where they want their eyes and presses it; the Seat Offset takes up how far the head was from where it would rest and the view recenters as the Recenter Action does. Drives the whole Companion Mod through what the engine calls (the in-cab camera update, the action callback, the action event registration) over a mock scene graph, and asserts on what the engine would see: where the engine camera, the eye camera and the audio listener are, the bridge calls, the saved settings and the blinking warnings. Axes: forward, up and right, in the in-cab camera's parent space. There forward is +Z and the vehicle's left is +X (the engine's vehicles are modelled that way). The head pose is in the tracking space, where -Z is forward and +X is right; a cab looking out of the windscreen has its head tracking node turned a half turn (origRotY pi), so a lean (x, y, z) in the tracking space is (-x, y, -z) in the cab.

### `tests/test_snap_turn.luau`

- **`SnapTurnIsRefusedWhileAMenuTakesTheInputEvenWithoutTheMenuQuad`**: The menu quad showing and a menu taking the input are two states. Polling the GUI sets both together; the input one alone (no quad shown) must still refuse the turn.
- **`ActionEventsRegisterIntoTheGameplayContextAndFollowItIntoAndOutOfVehicles`**: Slice 16b: the events go into the gameplay input context, not the one that is current. At map load the loading screen's menu context is current; events registered there exist but never fire until a vehicle is entered.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 43 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
