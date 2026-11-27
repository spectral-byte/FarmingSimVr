# Decisions behind the Stereoscopic View and Dual-Pass Physics

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0005](0005-oblique-frustum-projection-via-vrbridge.md), [ADR-0013](0013-in-engine-2d-floating-window-view-toggle.md), [ADR-0015](0015-sequential-dual-pass-stereo-presentation-pipeline.md), [ADR-0021](0021-one-registered-vr-camera-for-both-eyes.md), [ADR-0023](0023-head-rotation-reaches-the-engine-as-quaternions.md).

## Context and Decision
This record holds the non-obvious decisions behind the Companion Mod's stereoscopic view: the Dual Eye Camera Nodes, the Cyclopean Audio Listener Node, the in-cab and Outside Camera updates, 2D Floating Window Mode and the second pass that must not move the simulation. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionDualPassPhysics.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Dual-Pass Physics Pause and the eye pair trace

  Sequential Dual-Pass freezes the game's Lua simulation for the second sub-pass, but the engine simulates physics natively
  every rendered frame, whatever dt the mission update is given. So a driven vehicle moves between the two eyes of a pair,
  and the eye cameras ride on it. Driving at 20 km/h it covers about 6 cm in an 11 ms frame, one IPD: looking to the side
  the eyes then seem twice as far apart (the world shrinks) or not apart at all (it goes flat and huge).

  The engine launches each frame's physics step as the frame starts, with that frame's dt (the dt its update then gets) and
  the pause and time scale the previous update left; the update after it sees the step's result (eye pair traces in the
  headset: g_physicsDt is always the previous update's dt). So the second sub-pass's update pauses the next first sub-pass's
  step, the one between the eyes of a pair, and the first sub-pass's update sets the time scale of the second's step, which
  has to simulate the paused time too. It simulates one pair period: the headset's refresh locks each second sub-pass's
  frame and the next first sub-pass's together (their dts add up to 22.2 ms within hundredths at 90 Hz, while a pair's own
  two dts swing about 3 ms), so that sum, smoothed, is the time each pair is shown for. The step's own dt is not known yet
  when the time scale is set, so it is predicted from the second sub-passes' smoothed dt; a miss is multiplied by the time
  scale (about 2.4). The physics time owed (real dt the steps have not simulated) keeps physics with real time: a fraction of
  what is left over is added each pair, so a missed prediction is made up over a few pairs instead of swinging the next one.

  In multiplayer the pause stands aside whatever its setting. The game predicts the player's movement on foot and, on each
  server update, puts the character back where the server has it and replays the moves the server has not confirmed yet;
  those land in the next physics step, so a paused step shows the player pulled back and the view rubber-bands. On a client
  the controlled vehicle is placed by the game's Lua interpolation, which already stands still for the second sub-pass.

  PUBLIC SURFACE
    VRCompanion:setDualPassPhysicsPause(mode)                       "on" (the default) or "off"; session only; held off in
                                                                    multiplayer
    VRCompanion:applyDualPassPhysicsPause(subPass, frameDt)         called by runMissionUpdate at the end of each sub-pass's
                                                                    update, with the frame's real dt
    VRCompanion:releaseDualPassPhysicsPause()                       physics running at normal speed again; outside Sequential
                                                                    Dual-Pass, on turning the pause off and on map unload
    VRCompanion:traceEyeFrame(eyeIndex, camNode, subPass, frameDt)  one vrEyePairTrace line for a rendered eye frame (subPass
                                                                    nil under AFR; frameDt the update's real dt); returns the
                                                                    line, nil when not tracing
    Console: vrDualPassPhysicsPause [off|on], vrEyePairTrace [frames]
    Registered as the subsystem "dualPassPhysics": the two commands are added at map load and removed at map unload, where the pause is released
  ```

- **`VRCompanion.DUAL_PASS_PHYSICS_MAX_OWED_TIME`**: Physics time owed is kept within this many ms either way, so a hitch is not caught up over many pairs
- **`gameHoldsPhysics`**: The game pauses physics itself while it reloads vehicles; the mod must not resume it then
- **`smoothed`**: Called at the end of each sub-pass's update; frameDt is this frame's real dt, the dt this frame's physics step ran with. The second sub-pass pauses the next step; the first resumes the one after it at the time scale that simulates the smoothed pair period and a fraction of the time owed beyond this frame's paused step, over the step's predicted dt.
- **`VRCompanion:applyDualPassPhysicsPause`, `stepPaused`**: This frame's step ran with what the previous update left, which is still applied
- **`VRCompanion:applyDualPassPhysicsPause`, `if not stepPaused or lastSecondDt == nil or predicted == nil or predicted <= ...`**: Nothing paused yet (the pause was just turned on): this pair runs at normal speed and the next one starts the pause
- **`VRCompanion:applyDualPassPhysicsPause`, `leftOver`**: What the last resumed step left over, before this frame's paused step added its dt
- **`trace`**: vrEyePairTrace [frames] For the next frames (default 32), logs each rendered eye frame: the sub-pass (Sequential Dual-Pass) or AFR, the eye, the engine's physics update index, the controlled vehicle's speed, and how far its root node and the eye camera moved in the world since the previous eye frame, with the part of the vehicle's move along the eyes (the head's left-right axis), which is what adds to or takes from the IPD. A still vehicle between the eyes of a pair shows as 0.00 cm on the second eye. Then the timing behind the move: the update's real dt (on a second sub-pass with the pause on, with the dt the time scale was set for), the engine's physics dts as the game's Lua sees them (g_physicsDt, g_physicsDtUnclamped, g_physicsDtNonInterpolated, which is 0 on a frame that simulated nothing), and the physics pause state as it stands when the line is logged: set by the earlier sub-passes, before this one's own pause or resume.

### `mods/FS25_VRCompanion/src/VRCompanionStereo.lua`

- **`headPoseQuaternion`**: Quaternion of a head pose's Euler angles, composed R = Ry(yaw) \* Rx(pitch) \* Rz(roll) as VRBridge derives them from the headset's orientation
- **`VRCompanion:getRotationInSpace`**: Rotation of node in spaceNode's space as a quaternion, from the engine's world quaternions so no Euler angles are read back. Euler angles read back from the engine are unreliable: each convention has a singular orientation where they lose the pitch (with the yaw as the middle angle, as the engine has it, a yaw of 90 degrees). nil when the engine can't report world quaternions.
- **`VRCompanion:setNodeQuaternion`**: Sets node's local rotation from a quaternion. setQuaternion takes no Euler angles, so the engine's convention and its singularity never come into it; Euler angles composed R = Ry \* Rx \* Rz are only the fallback for an engine without it.
- **`VRCompanion:getDualPassEye`**: The eye the current Sequential Dual-Pass sub-pass renders. The Injector alternates which eye goes first between pairs, so the sub-pass index alone does not name the eye.
- **`VRCompanion:getStereoCameraNodes`, `baseNode`**: Every game camera shares the Dual Eye Camera Nodes, seeded from the first one they replace
- **`VRCompanion:unregisterDualEyeCameraNodes`**: Takes the Dual Eye Camera Nodes off the camera manager. A node deleted with its vehicle is only forgotten: removing an unknown entity from the camera manager could make it activate a default camera the mod would then mistake for the game's own.
- **`VRCompanion:getGameCamera`**: The game's own camera: the camera manager's active camera, or the one a Dual Eye Camera Node took the place of. Nil when the camera manager can't say.
- **`VRCompanion:bindRenderingCamera`, `if self.deferRenderingCameraBind then return end`**: During a Sequential Dual-Pass sub-pass the mission update owns the single bind of the eye's camera; camera updates inside the sub-pass only position nodes.
- **`VRCompanion:bindRenderingCamera`, `camMgr`**: The game's camera manager renders only through cameras registered with it and silently ignores any other, so a Dual Eye Camera Node is registered before it is bound. The global setCamera is the game's deprecation wrapper (it warns and doesn't bind such a camera) and is only the fallback when there is no camera manager.
- **`VRCompanion:isGameShowingCamera`**: Whether the game's own camera (getGameCamera) is the camera with node camNode (or altNode). False while a game screen with a camera of its own (the shop, construction mode) is shown; the mod then leaves rendering to the game. Without a camera manager to ask, the camera being updated is assumed shown.
- **`VRCompanion:initDualEyeCameraNodes`, `if self.cameraNodeLeft == nil or self.cameraNodeLeft == 0 then`**: Both eyes render through one camera node, moved to each eye in turn: temporal upscalers (DLSS, FSR) drop their history whenever the rendering camera changes
- **`VRCompanion:ensureDualEyeCameraNodes`, `entityExistsFn`**: The camera node linked under a vehicle is deleted with it (sold, reset): recreate it. The deleted id stays in registeredEyeCameras so it is still recognised if the camera manager names it active.
- **`VRCompanion:ensureCyclopeanAudioListener`, `entityExistsFn`**: Mirrors `ensureDualEyeCameraNodes` so selling or resetting a vehicle while the Cyclopean Audio Listener Node is parented under it recreates the listener node instead of leaving a dead entity id.
- **`VRCompanion:set2DViewActive`, `self:restoreMenuBackgroundCamera()`**: Toggling into 2D Floating Window Mode while a menu quad is visible restores the menu background camera immediately instead of leaving the last 6DOF head offset on it; on-foot realignment (`realignPlayerCameraToRoot`) only runs when `getActiveVehicleCamera() == nil`.
- **`VRCompanion:leave2DViewOnMapUnload`**: Map unload leaves 2D Floating Window Mode without the HUD Safe Area and the HUD state that returning to VR applies: the HUD's own unload has already handed the game its full-screen anchors in the Full state.
- **`VRCompanion:restoreCameraProjectionsFor2D`, `if cam ~= nil and cam.isInside and cam.cameraNode ~= nil then`**: An Outside Camera's own node never gets a VR projection, so there is nothing to restore
- **`VRCompanion:beginFrameChecks`**: The pass-through and head-tracking checks run several times per frame. While a frame scope is open (VRCompanion:update and the mission update hook open one), each check resolves once and later callers in that scope read the cached answer. Scopes nest: the game runs VRCompanion:update inside the hooked mission update, and the inner scope shares the outer one's cache. Outside a scope the checks resolve on every call. The outermost scope opens and closes with a clean cache, so a change is picked up no later than the next update.
- **`VRCompanion:fetchEyeProjection`**: Returns the valid eye projection from VRBridge, or false when none is available (no bridge, 2D Floating Window Mode, or an invalid projection). While a sub-pass scope is open (see beginEyeProjectionScope) the result for an eye is reused, so one Sequential Dual-Pass sub-pass reads VRBridge once however many camera updates it triggers.
- **`VRCompanion:fetchEyeProjection`, `scratch`**: One reusable table per eye: VRBridge fills it in place instead of allocating, and the cached projection of one eye is not overwritten by a fetch for the other.
- **`VRCompanion:fetchEyeProjection`, `if type(fetched) == "table" then scratch[eyeIndex] = fetched end`**: VRBridge only fills a table in place once it holds every field; until then it returns its own table. Keep that one so later calls reuse it.
- **`VRCompanion:fetchEyeProjection`, `if cache ~= nil and cache.active and proj then`**: Only a valid projection is reused, so one that becomes valid mid sub-pass is picked up.
- **`VRCompanion:applyCameraProjection`, `if proj == nil then`**: A caller applying one eye's projection to several nodes fetches it once and passes it in. false means "already fetched, nothing usable"; nil means "fetch it now".
- **`VRCompanion:applyCameraProjection`, `if type(setProjectionOffset) == "function" then`**: In both AFR and Sequential Dual-Pass modes: keep the camera centered on the optical axis (0, 0) to guarantee parallel rays at infinity and natural binocular convergence without cross-eyed disparity shear, matching OpenXrManager::SubmitFramePair's aspect-ratio matched symmetric FOV (ADR-0005). The eye's tan offsets in proj stay published by VRBridge but are not applied.
- **`VRCompanion:getResolvedHeadPose`, `pose`**: A separate reusable table: writing TrackIR values (which carry no quaternion) into the VRBridge table would leave it incomplete, and VRBridge would then allocate a fresh one on every call instead of filling it in place.
- **`VRCompanion:matchEngineCameraLens`**: Gives an eye camera the lens of the engine camera it renders in place of: its near and far clip planes, and its field of view for when no VR projection replaces it. The Dual Eye Camera Nodes are shared by every camera path, and each engine camera has its own lens.
- **`VRCompanion:placeAndBindEyes`**: Final step of a head-tracked camera frame, shared by every camera path: links the Dual Eye Camera Nodes and the Cyclopean Audio Listener Node under parentNode, places the listener at the centre of the head and the active eye at the eye position, gives that eye the lens of referenceCamNode, applies its projection and binds it for rendering, for whichever Stereo Presentation Mode is active. All transforms are in parentNode's space and share one rotation, the quaternion (qx, qy, qz, qw). referenceCamNode is the engine camera the eye renders in place of: it seeds node creation, lends its lens and is the mono fallback target when no left eye node exists. stereoRequested (the bridge's requiresAfrFallback: the mod renders the eyes itself) asks for the eyeIndex eye; it is still overridden to mono, left eye, while a GUI menu or 2D Floating Window Mode is shown. Allocates nothing.
- **`VRCompanion:placeAndBindEyes`, `self:placeVirtualHandsForRender()`**: The Virtual Hands and the Pointer Ray follow the head for this rendered frame (a frame may be rendered with no update before it).
- **`VRCompanion:placeAndBindEyes`, `leftProj`**: This branch always shows the left eye; reuse eyeProj only when it was fetched for eye 0.
- **`VRCompanion.OUTSIDE_CAMERA_VERTICAL_BLEND`**: Below this horizontal length of the engine camera's unit look direction (about 6 degrees from vertical) the camera's up direction is blended into the Outside Camera's base heading.
- **`VRCompanion:getOutsideCameraBaseHeading`**: World heading of an Outside Camera's level base: the engine camera's world look direction projected onto the horizontal plane. Near vertical that projection shrinks to nothing and flips as the camera crosses straight down, so the camera's up direction is blended in there: for a camera looking down it points the way the camera would look if levelled (for one looking up, the opposite way). The blend fades out continuously, so the heading stays continuous through straight down and straight up. A camera pitched past vertical (turned over) is the exception: its look heading and its levelled heading point opposite ways, so the blended vector passes through zero about 5 degrees past vertical and the heading flips by 180 degrees there. Any rule that follows the look heading away from vertical has to turn that half circle somewhere. Returns nil when no heading can be derived.
- **`VRCompanion:renderThroughEngineCamera`**: Leaves the view to the engine: renders through the engine's own camera again when an eye camera was bound in its place (activeRenderingCamera is the camera bindRenderingCamera last bound). Binding changes no transform of the engine's nodes.
- **`VRCompanion:showEngineCameraView`**: Head tracking lost: shows exactly what the engine camera sees (its world position and rotation) rather than eyes frozen where tracking dropped. Placing the eyes there, instead of binding the engine camera, also holds when a Sequential Dual-Pass sub-pass binds its eye camera at the end.
- **`VRCompanion:restoreGameActiveCamera`**: Renders through the game's own camera again and takes the Dual Eye Camera Nodes off the camera manager, so the engine isn't left on one of them, e.g. when the mod unloads.
- **`VRCompanion:onOutsideCameraUpdate`**: Outside Camera: any camera of the controlled vehicle other than its in-cab camera. Runs after the engine's own camera update, so orbit, zoom and collision pull-in are already applied. The engine's camera nodes are only read: the view renders through the Dual Eye Camera Nodes placed in world space on a level base (the engine camera's position and horizontal heading, zero pitch and roll), with the head pose composited on top.
- **`VRCompanion:onOutsideCameraUpdate`, `self:ensureDualEyeCameraNodes(camNode)`**: The shared step renders through, and writes, its reference node when no eye node exists
- **`VRCompanion:onOutsideCameraUpdate`, `heading`**: No derivable heading (a degenerate engine transform) keeps the last one rather than snapping. `outsideCameraHeading` is cleared on vehicle entry/exit/transition, active camera switch, and map unload so a new vehicle or camera never inherits a previous vehicle's heading.
- **`VRCompanion:onVehicleCameraUpdate`, `if self.is2DViewActive then`**: In 2D Floating Window Mode: bypass 6DOF head tracking updates completely and show the game's own camera. VehicleCamera.update handles standard 2D mouse and thumbstick rotation.
- **`VRCompanion:onVehicleCameraUpdate`, `if cam.headTrackingNode == nil and type(createTransformGroupFn) == "function"...`**: Auto-construct headTrackingNode matching VehicleCamera:loadFromXML if missing
- **`VRCompanion:onVehicleCameraUpdate`, `targetHeadRotY`**: Update camera tracking node orientation with mouse/controller look yaw so head pose is composited cleanly on top.
- **`VRCompanion:onVehicleCameraUpdate`, `rest`**: Where the head rests in the cab, and the rotation the head pose turns on, both in camParent's space (the same step the Recenter Action realigns to). Composed as quaternions: Euler angles read back from the engine lose the head's pitch when it looks a quarter turn to the side.
- **`VRCompanion:onPlayerCameraUpdate`, `local baseQx, baseQy, baseQz, baseQw = nil, nil, nil, nil`**: The rotation the head pose turns on, in camParent's space: the head tracking node's, else a half turn to face forward. Composed as quaternions, as in the cab.
- **`VRCompanion:onPlayerCameraUpdateRotation`, `if type(cam.setRotation) == "function" then`**: In VR mode: keep neck level (pitch=0, roll=0); mouse/stick only turns body yaw. Head pitch and roll are tracked by the HMD and applied during onPlayerCameraUpdate.
- **`VRCompanion:updateMenuBackgroundCamera`, `curVeh`**: An Outside Camera behind a menu is left to the engine: its nodes are never written
- **`VRCompanion:updateMenuBackgroundCamera`, `if camNode == self.cameraNodeLeft or camNode == self.cameraNodeRight then`**: A Dual Eye Camera Node is already head tracked by its camera path
- **`VRCompanion:updateMenuBackgroundCamera`, `lastOffset`**: One record per camera node, allocated on first use and updated in place afterwards
- **`VRCompanion:updateMenuBackgroundCamera`, `baseX`**: Subtract previous VR offset to recover underlying base transform (preserving mouse/controller orbit deltas)
- **`deleteDualEyeNodes`**: Stereo in the subsystem registry Map load: the Dual Eye Camera Nodes and the Cyclopean Audio Listener Node are made, after the core polled the GUI and read settings.xml. Neither uses them: the poll only reads the GUI (the menu camera it restores on a closing menu is the game's, and there is no menu to close yet), loadConfiguration reads settings.xml and sends the Stereo Presentation Mode and the HUD layout (setRenderingMode, configureHudSafeArea) and touches no node, no camera setting and no action event. The nodes are made from fixed defaults, which no setting changes. No update hook: the game's menu camera follows the head while the menu quad is shown from a direct call of the core's update (updateMenuBackgroundCamera, right after the GUI poll: the camera is written before the hooks the core installs next and the vehicle transition, which may move it, and its offsets are relative to what is there). Map unload: the nodes go and the cameras are put back. Unload runs after the core's teardown (VRBridge.onMapUnload included) and after the HUD's and Input's (they are registered after this file, so they unload first); the 2D Floating Window Mode goes last, leaving the HUD as the HUD's unload left it. Why each of these orders holds or does not matter is in the core's deleteMap.
- **`deleteDualEyeNodes`, `deleteIfExists`**: Nodes linked under a vehicle are already gone: the game deletes its vehicles first, and deleting an unknown entity logs a script error even inside pcall
- **`deleteDualEyeNodes`, `if self.cameraNodeRight ~= self.cameraNodeLeft then`**: Both eyes share one camera node unless something set them apart
- **`registerSubsystem "stereo"`**: Map unload runs after the HUD's (the HUD registers after Stereo), as it did before the registry, so unloading behaves as it did. The nodes go before the camera settings are restored, which skips cameras that no longer exist. Transient per-map stereo state (`renderedFovX`, `outsideCameraHeading`, `lastGameShowingTrackedCamera`, `trackedCameraShown`, `gameCameraNode`, and tick counters) is cleared. 2D Floating Window Mode is left last, without applying the HUD again, so the full-screen HUD the HUD's unload left stays (see the core's deleteMap).

### `tests/test_2d_view_mode.luau`

- **`installRenderedCamera`**: The camera the engine renders through, as getCamera() reports it. As in the game, the camera manager renders through registered cameras (the game's own, and any added with addCamera) and ignores others; the global setCamera is the game's deprecation wrapper and binds nothing.
- **`_G.g_inputBinding`, `events`**: FS25: events keyed by event id, actionEvents keyed by action name

### `tests/test_dual_pass_pacing.luau`

- **`VRCompanion.hudConvergenceDepth`**: These tests place the HUD at the depth its layouts are set for (1.8 m), not at the HUD Depth a new install starts at
- **`UpdateDoesNotScanGlobalTableEveryFrameWithoutFerry`**: Slice 12b: Per-frame update must not rescan \_G / mission vehicles
- **`installCameraReportMocks`**: Under AFR each frame renders one eye's camera; the Companion Mod reports it to the Injector like a Sequential Dual-Pass sub-pass does, so the motion vector correction has the cameras it needs.
- **`SequentialDualPassCameraUpdatesDoNotReportThroughTheAfrPath`, `vehCam`**: Sub-passes report once, from the mission update; the camera updates inside them must not add a second record.
- **`withPhysicsRecorder`**: Dual-Pass Physics Pause: the engine simulates physics natively every frame, whatever dt the mission update is given, so the vehicle moved between the two sub-passes. The pause stops it for one of the two frames of a pair.
- **`DualPassPhysicsPauseSimulatesASteadyPairPeriodWhenThePairsOwnDtsSwing`, `secondDts`**: As in the headset: each second sub-pass's frame and the next first sub-pass's add up to the 22.2 ms refresh, while the second sub-pass's dt swings, so a pair's own two dts swing by twice as much
- **`DualPassPhysicsPauseNeverTouchesPhysicsInMultiplayer`**: In multiplayer the game corrects the player's predicted movement against the server's through physics steps, and a paused step makes the view rubber-band, so the pause stands aside there whatever its setting.

### `tests/test_head_rotation.luau`

- **`eulerOrder`**: How the fake engine reads Euler angles: "YXZ" composes R = Ry \* Rx \* Rz, "ZYX" composes R = Rz \* Ry \* Rx. Whichever the engine uses, the head's rotation must reach the view intact.
- **`SINGULARITY_COS`**: Within this cosine of a convention's singularity (for "ZYX" a yaw of +-90 degrees, for "YXZ" a pitch of +-90 degrees) the engine's single-precision extraction reads only rounding noise
- **`eulerOf`**: Euler angles (rx, ry, rz) of a rotation matrix in the fake engine's convention. At the singularity the other two angles turn about one axis and are read from rounding noise, which is modelled as reading them as zero: the view stops pitching while yaw still works.

### `tests/test_outside_camera.luau`

- **`eulerOrder`**: How the fake engine reads Euler angles: "YXZ" composes R = Ry \* Rx \* Rz, "ZYX" composes R = Rz \* Ry \* Rx. The mod must not depend on which one the engine uses.
- **`CameraPointingStraightDownProducesFiniteStableTransforms`, `for i, pitchOffset in ipairs({ 0, 1e-5, -1e-5, 1e-3, -1e-3 }) do`**: Exactly straight down, then a hair either side of vertical: the horizontal projection of the look direction flips by pi across vertical, the view must not

### `tests/test_rendering_camera.luau`

- **`install`, `registered`**: As in the game: the camera manager renders through the cameras registered with it (the game's own, and any added with addCamera) and ignores any other; the global setCamera is the game's deprecation wrapper and binds nothing

### `tests/test_vehicle_camera_yaw.luau`

- **`_G.localRotationToLocal`, `return rx + fromR.rx, ry + fromR.ry, rz + fromR.rz`**: In GIANTS Engine, localRotationToLocal composites the local rotation of fromNode with the input rotation
- **`StartingInCabViewAndRecenterFacesForwardOutWindshieldCollinearWithVehicleZ`, `standardCabCam`**: Standard FS25 vehicle camera: XML/I3D defines rotation="-18 180 0" (math.pi)
- **`StartingInCabViewAndRecenterFacesForwardOutWindshieldCollinearWithVehicleZ`, `camRot`**: The camera view frustum in GIANTS points along local -Z. Forward in vehicle cab space is +Z (0, 0, 1). Backward is -Z (0, 0, -1). When looking forward out the windshield, the camera node rotation Y in cab space must be math.pi (180 deg), which rotates local look vector (0, 0, -1) to (0, 0, +1).
- **`runRestPositionAgreement`**: The cab rest position: the camera update at a zero head pose and the Recenter Action's realign put the head in the same place (translation and rotation), whichever way the cab is set up
- **`CabForwardOfZeroDoesNotTakeUpTheLookYaw`**: A cab whose forward yaw reads 0, entered with mouse look already turned (the game's "don't reset vehicle camera" brings back the last rotY): the head tracking node the mod builds and turns by the look yaw every frame must not be read back as the cab's forward, or the look yaw becomes the new forward and the Recenter Action rests there

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 76 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
