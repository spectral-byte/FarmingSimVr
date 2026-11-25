# Decisions behind the Test Stubs and Fakes

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0002](0002-in-process-luau-state-interception.md).

## Context and Decision
This record holds the non-obvious decisions behind the Lua stubs of the game, the fakes and the fixtures the tests share, with what was observed in the game or read in its source that gave each its shape. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `tests/companion_mod_fixture.h`

- **`s_expectHookErrors`**: A hook the core caught and logged (IsHookErrorLine) fails the test, unless the test called ExpectHookErrors() first: the core protects every subsystem hook, so the log is the only place a hook that raises shows up. Checked as the line is printed, so a test that clears s_capturedLogs midway cannot hide one.
- **`CompanionModTest`, `PlaceHudAtLayoutDepth`**: Puts the HUD at the depth its layouts are set for (HUD_LAYOUT_DEPTH, 1.8 m): drawn at layout size, with the 0.012 disparity of the default IPD. Tests of HUD placement and disparity are written for it, not for the HUD Depth a new install starts at.
- **`CompanionModTest`, `SetGameTime`**: Driving time and frames The mod reads the game clock through VRCompanion:getCurrentTimeMs(), which is the global g_time (milliseconds) and falls back to the real clock when g_time is nil. g_time is not defined until a test calls SetGameTime/AdvanceGameTime, so tests that never touch it behave exactly as before. Tests that depend on time (the 3 s in-use window, debounces) set it and advance it by hand, so they never depend on wall-clock time.
- **`RegisterTestSetFrameParity`, `lua_pushlightuserdata(L, &bridge);`**: With an active OpenXR session a mode change is queued until the DXGI proxy latches it at a completed frame boundary. These tests have no proxy, so Luau latches it explicitly.

### `tests/companion_mod_stubs.h`

- **the file**:

  ```text
  Engine and mod stubs for Companion Mod tests. Each stub is a Luau file under tests/stubs/, read from disk by name
  every time a test asks for it, so an edited stub takes effect on the next test run without a rebuild:

    ASSERT_EQ(RunLuauChunk(L, StubChunk("scene_graph"), "=sceneGraph"), 0);   // runs tests/stubs/scene_graph.luau

  Each file starts with what it models and the functions it offers. The shared ones:

    scene_graph            a small scene graph (nodes, parents, local and world transforms) with the engine's node
                           functions, and StubScene.cab / StubScene.onFoot building a camera chain the real Companion Mod drives
    model_loading          the engine's model loading and node functions the Virtual Hands use (StubModel), on top of
                           the scene graph: a loadable hand model with finger joints, clones, visibility, scale, world
                           transforms, joint rotations, shadow flags, and a model that is missing, failing or lacking joints
    interactive_control    a stand-in for the Interactive Control mod's Click Point class and manager (StubIC), with a
                           mismatched variant
    input_binding          a stand-in for g_inputBinding and Binding (StubInput), shaped after the finding of spike #99:
                           the actions and their bindings per device, addBinding, saveToXMLFile, with variants that lack
                           the API or raise
    steering_vehicle       a cab vehicle with a Steering Wheel and the game's analog steering (StubSteer): the
                           Drivable.setSteeringInput that records every call, a Steering Wheel node and the per-frame
                           tick that moves the steering toward the input and turns the node (ADR-0027)
    hand_tool              a player on foot who holds Hand Tools (StubHandTool): the game's HandTool, Player and
                           PlayerTargeter, the pressure washer lance and another mod's tool (which the Companion Mod
                           does not support), with attachToolToCamera / getLookRay install variants and extension
                           points for the flashlight, the chainsaw, the spray can and the look ray (ADR-0029)
    hand_tool_flashlight   the flashlight for StubHandTool (run after hand_tool and model_loading)
    hand_tool_chainsaw     the chainsaw for StubHandTool (ADR-0029)
    hand_tool_spray_can    the spray can for StubHandTool (ADR-0030)
    hand_tool_horse_brush  the horse brush for StubHandTool (ADR-0030)
    hand_tool_fishing_rod  the Highlands Fishing DLC's rod for StubHandTool (ADR-0031)

  They model only the names and shapes the Companion Mod depends on (ADR-0026, ADR-0027). Nothing here is
  copied from Interactive Control or from the game.
  ```

### `tests/fake_desktop_cursor_port.h`

- **`FakeDesktopCursorPort`**: A stand-in for the desktop: records the cursor clip and the cursor position the Mirror Window Controller sets, and lets a test choose the foreground window and whether the cursor is showing. A test that drives the Injector's cursor handling uses this, never the real user32: a test calling the real ClipCursor would trap the developer's own mouse, and a test window is not reliably the foreground window.

### `tests/fake_openxr_controllers.h`

- **`FakeOpenXrControllers`**: Motion Controller model for fake OpenXR dispatches. Tests place each hand and set its inputs; the fake forwards the action calls (xrStringToPath ... xrApplyHapticFeedback) here and behaves like a runtime: it records what was declared, refuses what the knobs say to refuse, latches inputs at xrSyncActions, and locates the aim spaces in the same re-based tracking origin as the head.

  Hand ids: 0 = left, 1 = right.

- **`SyncActions`, `m_latchedValid = false;`**: A session that is not focused delivers no input: every action is inactive.
- **`FakeOpenXrControllers`, `LocateActionSpace`**: Locates an aim or grip space. Valid only while the latched pose action is active (a controller that is off, a grip pose that alone is untracked or a session that is not focused has no pose).

### `tests/fake_openxr_runtime.h`

- **`FakeTrackedHeadsetOptions`**: Options for FakeTrackedHeadset that must be in place before the session is created.
- **`FakeTrackedHeadset`, `SetHandPointing`**: A hand held so that its index fingertip is at the position of `aim` and its finger runs along `aim`'s -Z axis: the aim pose is `aim` and the grip pose is the one that puts the relaxed glove finger there (the Pointer Ray starts at the fingertip and runs along the finger, so for a test that says "the hand points here" this is the hand that does). The fingertip and the finger direction are the grip-frame numbers of the Gloved Hand, metres, left hand (the right hand is the left with X negated), written out here independently of the mod: scripts/derive_virtual_hand_pointer.py prints them.

### `tests/fake_openxr_tracking.h`

- **`QuaternionFromEuler`**: The quaternion of Euler angles composed R = Ry(yaw) * Rx(pitch) * Rz(roll), the order `QuaternionToEuler` reads back. A test that wants a head or hand turned by angles builds the pose's quaternion with it, because a `HeadPose` holds the quaternion alone, as the runtime reports it. `SetHeadPose` and `SetHandPose` pass that quaternion on unchanged.
- **`FakeOpenXrTracking`, `SetEyeFov`**: Sets one eye's field of view. A runtime always reports both eyes, so an eye that was never set mirrors the first one set (left/right swapped and negated). Eyes sit at the head origin so the manager keeps its configured IPD.
- **`FakeOpenXrTracking`, `LocateSpace`**: Locates the head relative to baseSpace; a LOCAL base is re-based by its pose in the runtime's tracking origin.
- **`FakeOpenXrTracking`, `RebasePose`**: A pose given in the runtime's tracking origin, expressed in baseSpace. A LOCAL base is re-based by its pose (which is how Recenter() moves the origin); any other space leaves the pose in the tracking origin. Hands and the head share this so they always agree.

### `tests/luau_test_runner.cpp`

- **`LuauCollectGarbage`**: Luau's base library only has gcinfo. collectgarbage serves weak-table tests (a forced full collection, the default) and allocation tests: "collect", "count" (KB, with the byte remainder), "stop" and "restart".

### `tests/luau_test_runner.h`

- **`CompanionModLoadOrder`**: Companion Mod harness: the module files are the only implementation, loaded in modDesc order. CompanionModLoadOrder() must mirror \<extraSourceFiles> in modDesc.xml (guarded by a test).
- **`IsHookErrorLine`**:

  ```text
  The line the Companion Mod's core logs when a subsystem's hook raises (VRCompanion:registerSubsystem):
    [FS25_VR] [Error] Subsystem '<name>' <hook> hook raised: <error>
  Both harnesses fail a test in which such a line was logged unless the test said it expects it, because a hook that raises is
  caught by the core and would otherwise pass unnoticed.
  ```

### `tests/stubs/animated_objects.luau`

- **the file**:

  ```text
  Animated Objects (ADR-0034)
  StubAnimatedObject.install(variant)   defines the globals the game has: AnimatedObject (the class: triggerCallback, getCanBeTriggered,
    setDirection, raiseActive), AnimatedMapObject (a subclass that defines none of its own, like the game's, for a barrier placed on the
    map), AnimatedObjectActivatable (getIsActivatable, onAnimationInputToggle, updateActionEventTexts), FSCareerMissionInfo and
    g_currentMission.missionInfo:isa, g_currentMission.activatableObjectsSystem (addActivatable / removeActivatable keep
    StubAnimatedObject.activatables), CollisionFlag / CollisionMask, and the engine's raycastClosest.
    variant   how the game is in this "game version": nil / "ok", "triggerCallbackMissing" (a game update renamed triggerCallback, which
    the engine calls by the new name), "triggerCallbackReshaped" (it takes no parameters but self: the engine's arguments are read from
    StubAnimatedObject.pending), "toggleMissing" (onAnimationInputToggle renamed), "toggleReshaped" (it takes no parameters at all),
    "canBeTriggeredMissing" (getCanBeTriggered renamed), "canBeTriggeredReshaped" (it takes no parameters at all).

  StubAnimatedObject.door(options) -> object   an Animated Object with its zone (a trigger node) and one moving part, a box at the
    world position x, y, z with the half extents hx, hy, hz (defaults: a leaf 1 m wide, 1 m high and 10 cm thick).
    options: name, x, y, z, hx, hy, hz, map (an AnimatedMapObject, not an AnimatedObject), canBeTriggered (false: the check answers no),
    farmCheck (the object has an own getCanBeTriggered, like PlaceableAnimatedObjects, that also asks object.farmAllowed),
    negAction (a second action: the object moves while a button is held), sameAction (one action for both directions: the same),
    noAction (no action at all: no prompt), collisionChild (the box is on a child of the part's node, as a shape below a transform
    group is), parts (a list of { x, y, z, hx, hy, hz } instead of the one part).
    object.controls = { posAction, negAction, posActionText, negActionText }, object.animation = { parts = { { node } }, time, direction
    (0 stopped, 1 opening, -1 closing), duration }, object.activatable, object.triggerNode, object.isEnabled, object.isDeleted.
  StubAnimatedObject.enterZone(object) / leaveZone(object)   the local player enters or leaves the zone: the engine calls the trigger
    callback, by the name the game has it, with the zone's trigger node and the local player's root node.
    StubAnimatedObject.otherEnters(object)   another node enters, which the callback does not count.
  StubAnimatedObject.inZone(object)   the game put the object's activatable in its system (the prompt can show it).
  StubAnimatedObject.wall(name, x, y, z, hx, hy, hz) -> node   a box of the world the ray cast hits and no Animated Object owns.
  StubAnimatedObject.showPrompt(object) / pressButton(object)   the game's prompt: it registers the toggle handler on the bound button
    (controls.posActionEventId is set from then on) and the button calls what was registered.
  StubAnimatedObject.toggles   every run of the toggle handler, { object, direction }, in order; toggleCount(object)
  StubAnimatedObject.advance(object, fraction)   the animation moves by that part of its duration in its direction, and stops (direction
    0, as the game's server sets it) at either end. setDirection decides a stopped door's direction by its time, as the game's does.
  StubAnimatedObject.originalTriggerCallback   the game's trigger callback as the stub made it (the class has it unless a wrap replaced it).
  StubAnimatedObject.castSeesLooseObjects   (default true) the raycastClosest also hits the loose objects of the hands stubs
    (StubHandTool.looseObjectList, spheres), as the engine reports any rigid body: an object in front of a door hides the door from it.
  StubAnimatedObject.casts   every raycastClosest call, { x, y, z, dx, dy, dz, maxDistance, mask }.
  StubAnimatedObject.delete(object)   the object is deleted with no leave event for the zone.
  StubAnimatedObject.textCalls   the prompt texts set, { eventId }; setting one for an event that is not registered raises, as the game's
    input binding does for a nil id.
  ```

- **`S.install`, `AnimatedMapObject`**: The subclass the game makes map objects of: it defines no trigger callback or check of its own.
- **`S.install`, `triggerCallback`**: AnimatedObject.lua:730 -- the three conditions, then the activatable goes in or out of the system
- **`S.install`, `canBeTriggered`**: the game's check (opening hours and the like; its body is stripped from the SDK): a test flips object.canBeTriggered
- **`S.install`, `AnimatedObject:setDirection`**: setDirection decides a stopped door's direction by its time (the handler passes 0 \* -1 then)
- **`S.install`, `S.setActionEventText`**: g_inputBinding:setActionEventText: raises for an event that is not registered
- **`_G.raycastClosest`, `if S.castSeesLooseObjects and StubHandTool and StubHandTool.castAtLooseObject...`**: the loose objects of the hands stubs are rigid bodies too, and the engine's cast reports them (a pallet in front of a door hides the door from it) unless a test turns that off to leave the choice to the nearer-wins rule alone
- **`S.door`, `if options.noSaveId then object.saveId = nil end`**: a building's door has none, only the nodes of its i3d

### `tests/stubs/console.luau`

- **the file**: Console and menu The engine's console and menu globals the Companion Mod registers into and polls: the commands can be run with runCommand(name, ...), and g_gui.visible opens and closes a menu.

### `tests/stubs/hand_tool.luau`

- **the file**:

  ```text
  Hand Tools (ADR-0029)
  A player on foot who can hold a Hand Tool, shaped after the game's HandTool, Player and PlayerTargeter. Run after
  scene_graph.luau (and after model_loading.luau when the Virtual Hands are needed: a hand tool goes into a Virtual Hand).
  Run StubHandTool.install BEFORE loading the Companion Mod to have HandTool and Player there at file load, like the game.

  StubHandTool.install(variant, options)   defines the globals the game defines: HandTool, Player and PlayerTargeter.
    variant   how HandTool.attachToolToCamera is in this "game version": nil / "ok", "missing" (a game update renamed it
              to attachToolToCameraNode: the class has no attachToolToCamera, the game attaches through the new name) or
              "reshaped" (it takes no parameters at all, and attaches the tool being held)
    options   getLookRay  "ok" (default), "missing" (Player has lookRay instead) or "reshaped" (Player.getLookRay takes no
              parameters): for the look ray wrap. The targeter reads Player.lookRay in the "missing" game.
              StubHandTool.originalGetLookRay is the game's own function ("ok" game only: nil in the others).
  StubHandTool.onFoot(options) -> { player, cam, parent, yawNode, firstPersonCamera }   StubScene.onFoot(options) with a
    Player: g_currentMission.player and g_localPlayer are it. player.camera is the camera of StubScene.onFoot (isFirstPerson,
    pitchNode: the node the game links a held tool to). Player:getLookRay() -> x, y, z, dirX, dirY, dirZ is the first
    person camera's position and its -Z axis in the world. player.targeter is a PlayerTargeter.
    StubHandTool.thirdPerson(player, on)  the camera is (not) in first person; a held tool attaches again.
  PlayerTargeter:update(dt)   reads player:getLookRay() once, like the game, and keeps it in targeter.lastRayX .. lastRayDirectionZ
    (the fields the game keeps) and in StubHandTool.rays, in order, as { x, y, z, dirX, dirY, dirZ }.
    StubHandTool.lastRay() -> x, y, z, dirX, dirY, dirZ   of the last update, nil before any
  StubHandTool.tick(dt)   one frame of the game's player update: the targeter reads the look ray, then every registered
    updater (StubHandTool.updaters, below) runs
  StubHandTool.lance(player, options) -> tool   the pressure washer lance (spec_highPressureWasherLance), not yet held.
    tool.rootNode (the node the game links), tool.firstPersonNode, tool.spec_highPressureWasherLance.raycastNode (the
    nozzle: it washes from the node's origin along its +Z axis) and tool.activateAction, the function the game's
    ACTIVATE_HANDTOOL binding calls (HandToolHPWLance:onWashAction(_, inputValue)).
    StubHandTool.nozzleRay(tool) -> x, y, z, dirX, dirY, dirZ   where the lance washes from and along, in the world
  StubHandTool.otherModTool(player, options) -> tool   another mod's Hand Tool (spec_otherModTool), which the Companion Mod
    does not support: placed like any other by the game. Its use handler is OtherModHandTool.onUseAction(tool, _, inputValue),
    on the specialization class only (recorded in StubHandTool.useCalls as tool = "otherMod"), and tool.actionEvents lists
    its registered events { actionName, target, callback, triggerUp, triggerDown, triggerAlways, isActive }: ACTIVATE_HANDTOOL
    (up and down, not always) first; tool.activateAction, the game's binding, calls it on those edges. The tool also carries
    what a Hand Tool of the game has to be read from outside (config file, type, specializations, nodes): see the comment
    above the function; options shape it (extra fields, nodes, a spec that cannot be read)
  StubHandTool.hold(tool) / StubHandTool.drop(tool)   the player takes the tool in hand (the game's startHolding: isHeld,
    the carrying player, attachTool) and puts it away (detachTool, not held)
  StubHandTool.pressUseBinding(tool, inputValue)   the game's own binding (X on a pad, the mouse, the keyboard) calling the
    tool's use handler with inputValue (1 pressed or held, 0 released)
  StubHandTool.draw()   one frame of the game's draw: every held tool draws what it draws (the spray can's and the chainsaw's
    onDraw are in their own chunks; another mod's tool draws "otherModGauge")
  StubHandTool.rendering(tool) -> text   what the tool rendered in the last draw, in order, joined by "+" ("" for nothing)
  StubHandTool.drawers   a list of function(tool) the draw runs for each held tool: a tool type's own onDraw goes in here;
    StubHandTool.render(tool, overlay) records one thing rendered
  StubHandTool.useCalls   every call of a tool's use handler, in order, as { tool = <name>, value = <inputValue>, self = tool }
  StubHandTool.cameraAttaches   how many times the game's own attachToolToCamera ran (whatever placed the tool then)
  StubHandTool.CAMERA_PLACEMENT   { x, y, z } where the game puts a tool's root below the pitch node (a tool placed the
    game's way is there; the lance's firstPersonNode is its root's origin)
  StubHandTool.tools   every tool made, in order
  Extension points for the later Hand Tools tickets (each adds its own chunk, run after this one, and touches nothing above):
    StubHandTool.updaters   a list of function(dt) the tick runs after the targeter: a tool's own onUpdate goes in here
    StubHandTool.makeTool(player, name, specName, options) -> tool   the common tool: a root node, a firstPersonNode at its
      origin, spec table `specName`, the HandTool methods through the class; a tool type adds its own nodes and handlers
    (the flashlight and the chainsaw add StubHandTool.flashlight / chainsaw, in their own chunks)
  ```

- **`StubHandTool.render`**: One frame of the game's draw: StubHandTool.rendered is emptied, then every held tool draws (each registered drawer, in StubHandTool.drawers, runs function(tool) for the tools of its kind, reading the class's onDraw at that moment, like the game's event dispatch). A tool's onDraw records what it rendered with StubHandTool.render(tool, overlay).
- **`unreadableNames`**: A node whose name cannot be read (a node that is gone while a tool is being read): getName raises for it. The engine's getName is replaced only while a tool has such a node, kept in StubHandTool.engineGetName, and put back by drop(tool).
- **`StubHandTool.otherModTool`**:

  ```text
  another mod's tool
  A Hand Tool the Companion Mod has never heard of, shaped like the tools mods add to the game: its own specialization
  (spec_otherModTool, which no SUPPORTED_TOOLS entry names), a use handler on its specialization class only
  (OtherModHandTool.onUseAction, called with the tool as `self`, like HandToolChainsaw.onCutAction) and an
  ACTIVATE_HANDTOOL action event registered for it (tool.actionEvents), as the game's registerActionEvent keeps one:
  called for trigger down (inputValue 1) and trigger up (0), not on every update the input is held.

  It is also what the game gives any Hand Tool to be read from outside (HandTool.lua, SpecializationUtil.lua):
    tool.configFileName ("mods/FS25_OtherMod/handTools/otherModTool.xml"), tool.typeName
    tool.specializations / tool.specializationNames   the specialization classes and their names, in step (OtherModHandTool
      "otherModTool"); the specialization tables are tool.spec_<name>. A third table, spec_otherModLine, has no class.
    tool.type = { name, functions }   the tool's type and the functions it gives the instance (copied onto it)
    tool.actionEvents   three registered events: ACTIVATE_HANDTOOL (up and down, callback on the class), AXIS_ROTATE_HANDTOOL
      (every frame, callback on the class) and AXIS_OTHERMOD_NUDGE (up only, a callback that is on no class)
    the nodes: root -> FirstPerson, Rod -> Guide -> Tip -> Float (four levels below the root)
  options (all optional): nestedTables = true adds spec_otherModTool.nested ({ inner = { deep }, speed, name }) and an empty
  table; bigTable = N adds a table of N entries; pathNamedNode = true adds a field naming an entity whose name is a file path
  (as the engine's images and sounds are); extraSpecFields = N more fields in spec_otherModTool; extraSpecTables = N more specializations of
  20 fields each; extraNodes = N more children of the root; extraClassFunctions = N more functions on OtherModHandTool (the
  first tool made in a game decides); brokenSpec = true adds spec_otherModBroken, which has a field
  naming a node whose getName raises (a node that is gone while the tool is being read); brokenNode = true adds a last child of
  the root whose name cannot be read the same way; both are the tool's, and StubHandTool.drop(tool) makes the names readable
  again (see makeNameUnreadable); configFileName replaces the name.
  ```

- **`StubHandTool.otherModTool`, `event.callback`**: the function itself, as the game's registerActionEvent keeps it

### `tests/stubs/hand_tool_chainsaw.luau`

- **the file**:

  ```text
  Hand Tools: the chainsaw (ADR-0029)
  The chainsaw for StubHandTool, shaped after the game's HandToolChainsaw. Run after hand_tool.luau (and after the Companion
  Mod is loaded or before it: HandToolChainsaw is read at map load too), then call StubHandTool.installChainsaw(variant).

  StubHandTool.installChainsaw(variant)   defines the globals the game defines: ChainsawCutState, HandToolChainsaw,
    PlayerCamera and g_cameraManager, and makes the targeter honour target types: PlayerTargeter:addTargetType(key, mask,
    minDistance, maxDistance) and :removeTargetType(key) (the spray can's chunk's, when it has added them), and each update fills
    closestTargetsByKey[HandToolChainsaw] with the closest trunk the look ray meets, measured from the ray's origin and only between
    the type's minimum and maximum distance (the game's tryAddTargetWithMask), as { node, x, y, z, distance }. Holding a chainsaw
    adds its target type (trees, from HandToolChainsaw.MINIMUM_CUT_DISTANCE, or 0 when the class has none, to 3 m), as the game's
    onHeldStart does; putting it away removes it. The minimum is read at that moment, so a test sets it before the player takes the
    saw in hand.
    variant   how HandToolChainsaw.onUpdate is in this "game version": nil / "ok", "missing" (a game update renamed it to
              onUpdateTick, which the event dispatch calls instead) or "reshaped" (it takes no parameters, and updates the
              chainsaw being held), or "rollMissing" / "rollReshaped" (the roll action's handler,
              HandToolChainsaw.onRollAction, is gone / takes no parameters; onUpdate is as "ok"), or "drawMissing" /
              "drawReshaped" (the draw event, HandToolChainsaw.onDraw, is gone: a game update renamed it to onDrawOverlays,
              which the draw calls instead / takes no parameters and draws the chainsaw being held; onUpdate is as "ok"), or
              "minimumMissing" (HandToolChainsaw.MINIMUM_CUT_DISTANCE is gone: a game update dropped or renamed it; onUpdate is as "ok")
  The game keeps the chainsaw's handlers on the specialization class and its event dispatch calls them as
  Class.handler(tool, ...) (SpecializationUtil.raiseEvent: spec[eventName](object, ...)). So they are here:
    HandToolChainsaw.onUpdate(tool, dt)       the chainsaw's update (run each tick by StubHandTool.tick): targets the tree the
                                              targeter found, aligns the split plane (updateSplitPlane), turns the model to
                                              face the camera, validates the cut, shows the Cut Ring, advances a cut
    HandToolChainsaw.onDraw(tool)             the chainsaw's draw (StubHandTool.draw): renders "crosshair", the game's default one at
                                              the centre of the view, while the saw is idle
    HandToolChainsaw.onCutAction(tool, _, v)  the ACTIVATE_HANDTOOL handler (v > 0 starts a cut on the targeted tree, or
                                              delimbing without one; v = 0 stops); recorded in StubHandTool.useCalls
    HandToolChainsaw.onRollAction(tool, _, d) the AXIS_ROTATE_HANDTOOL handler (rollInput += d, applied by the next update
                                              when not cutting, to spec.currentCutRoll, clamped to +-pi/2)
    HandToolChainsaw.MINIMUM_CUT_DISTANCE     the nearest a trunk can be to the targeter's ray start and still be found (0 here, so
                                              existing tests find a trunk anywhere along the ray; a test sets the game's own)
    HandToolChainsaw.MAXIMUM_CUT_DISTANCE     a cut stops when the targeter's last ray starts this far from the cut node
  and, as the game's registerFunction does, on the tool itself: updateSplitPlane, calculateCutPlane, updateRingSelector,
  testIfCutValid, beginCutting, stopCutting, beginDelimbing, stopDelimbing, updateCutting, setCurrentCutState.
  PlayerCamera.getRotation(camera) -> pitch, yaw, roll   the player camera's rotation from its look direction (the game's):
    updateSplitPlane takes the cut plane's yaw from it. The player's camera gets it as a method when a chainsaw is made.

  StubHandTool.chainsaw(player, options) -> tool   the chainsaw (spec_chainsaw), not yet held. options.cutTime: ms a cut takes
    (default 600). tool.rootNode, tool.graphicalNodeParent, tool.graphicalNode (the model), and in tool.spec_chainsaw:
    currentCutState (ChainsawCutState.IDLE / CUTTING / DELIMBING), currentCutRoll, cameraRotationNode (the node the cut plane
    hangs from: at the targeted tree's hit point while idle), splitPlaneNode, cutGuideNode (the model rides it while cutting),
    ringNode (the marker, visible while a tree is targeted), cutNode (a child of the model 0.4 m along the bar and 4 cm below its
    centre line, as in the game's models), targetedTree ({ node, x, y, z, distance }), cuttingTreeNode, lastDelimbPosX .. lastDelimbNormalZ.
  StubHandTool.tree(x, z, radius, y0, y1) -> tree   a vertical trunk: node, x, z, radius, y0, y1. A held chainsaw's targeter
    hits the closest trunk its look ray meets between the class's minimum cut distance and 3 m. StubHandTool.trees lists them.
  StubHandTool.treeAcross(ray, distance, radius) -> tree   a tall trunk whose surface the ray meets, horizontally, at `distance`;
    ray = { x, y, z, dirX, dirY, dirZ }
  StubHandTool.barRay(tool) -> x, y, z, dirX, dirY, dirZ   where the chainsaw's bar points now: the root's origin and +Z
  StubHandTool.modelDirection(tool) -> dirX, dirY, dirZ   the way the model's +Z points in the world
  StubHandTool.cuts   every tree the game cut through, in order: { tree, x, y, z, normalX, normalY, normalZ, upX, upY, upZ }
    where the plane was (calculateCutPlane); StubHandTool.inputLocks: how many times the cut locked the player's input minus unlocks
  ```

- **`_G.setDirection`**: the direction is the node's +Z, in the space of its parent; the up vector keeps the node's +Y near it. (The flashlight's chunk, hand_tool_flashlight.luau, defines one for nodes below the world root; a game that loads both keeps that one.)
- **`StubHandTool.installChainsaw`, `Class`**: (a minimum of 0 leaves a trunk anywhere along the look ray found; a test sets it to the game's own)
- **`StubHandTool.installChainsaw`, `table.insert(StubHandTool.updaters, function(dt)`**: the game's event dispatch: spec[eventName](object, ...), the class read at every call
- **`StubHandTool.chainsaw`, `spec.cutNode`**: below the bar's centre line, as in the game's models (at the lower edge of the chain)

### `tests/stubs/hand_tool_fishing_rod.luau`

- **the file**:

  ```text
  Hand Tools: the fishing rod (ADR-0031)
  The Highlands Fishing DLC's fishing rod for StubHandTool. The DLC's scripts cannot be read: this is shaped after the probe dump
  of the real rod (#125: its specializations, class functions, action events, nodes and spec fields, and how spec.state changed
  while fishing) and after play, not after source. Run after hand_tool.luau, then call StubHandTool.installFishingRod(variant) BEFORE
  loading the Companion Mod, or any time before the map is loaded. A game without the DLC simply never calls it.

  StubHandTool.installFishingRod(variant)   makes the DLC's specialization class. It has NO global name (the DLC's scripts run in
    their own environment): it is StubHandTool.fishingRodClass, and the game reaches it only through each rod's
    tool.specializations, next to its name "pdlc_highlandsFishingPack.fishingRod" in tool.specializationNames (with the storable
    specialization before it, as in the dump). Every handler is on the class only, never on the tool, called with the rod as self.
    variant   how the class's ACTIVATE_HANDTOOL handler is in this "DLC version": nil / "ok" (onActivateAction(rod, actionName,
              inputValue)), "missing" (a DLC update renamed it to onPrimaryAction, which the game's binding calls instead) or
              "reshaped" (onActivateAction takes no parameters, and acts as a press on the rod being held)
    Class functions (the dump's): onActivateAction, onActivateSecondaryAction, onUpdate, onDraw, onHeldStart, onHeldEnd,
      onRegisterActionEvents, onInputModeChanged, setState, getBobberPosition, getTipPosition, getAngleScale, getRodBendFactor,
      getCurrentItem, ... The type's functions (getBobberPosition, getTipPosition, getAngleScale, getRodBendFactor, getCurrentItem,
      setState, ...) are also copied onto each rod, as the game does; the action handlers, onUpdate and onDraw are not.
    The class's onUpdate runs each StubHandTool.tick for the held rod, and its onDraw in StubHandTool.draw, both looked up on the
    class at every call (the game's event dispatch).

  StubHandTool.fishingRod(player, options) -> tool   a rod, not yet held. Its spec table is tool["spec_pdlc_highlandsFishingPack.fishingRod"]
    (also StubHandTool.rodSpec(tool)); tool.spec_storable is the storable specialization's. In the spec, as in the dump:
      state          the current state: ONE OF spec.states (11 tables), recognised only by its keys (each has `fishingRod`, the rod).
                     nil when the game attaches the rod: the rod's first update while held sets it to idle (the headset showed
                     the real rod's state missing at the attach, 2026-10-18):
        idle        { fishingRod }                                   (1 key)
        charging     angleScale (0 to 1 while held), angleScaleIncreasePerMs (0.0007), inputValue, castingChargeOverlay, ... (10 keys)
        waiting      biteTime, biteTimeMin (10000), biteTimeMax (25000), biteHintTimeThreshold, waitForBiteOverlay, ... (10 keys)
        bite         catchTime, duration (880, the hook window), minCatchTimeThreshold (700), glyphButtonText ("Hook fish"),
                     hookFishOverlay, ... (12 keys)
        reeling      the minigame's layout (48 keys; the #140 headset dump named them): posX, posY, width, height, fishYMin,
                     fishYMax, cursorPos, targetLineXStart/End, hookLineX, hookStatusLineXStart/End, progressBarOverlay,
                     fishTargetBarOverlay, fishOverlay, hookOverlay, ... None of them tells the state by itself; the reeling
                     shows in rodBendFactor and fishingLine instead.
        caught       { fishingRod, angleScaleDecreasePerMs, bendResetRate }  (3 keys)
        (and five more states the dump never showed the rod in)
      states         the 11 state tables
      fishingLine    { isInitialized (true from the cast until the line is in), ropeLength, numActiveNodes, minLength (2),
                     maxLength (40), visibility, ... }
      rodBendFactor  0 unless reeling: while reeling, the fish's pull (options.fishPull, 0.5; StubHandTool.setFishPull) while the
                     reel is held, a third of it while it is not
      angleScale, angleMax (2.27), bendMax (0.2), item (the catch, nil until one), the nodes bobber, bobberPhysicsHelper, tipLine,
      tipLinePhysicsHelper, handle (handleJoint), rotor (yokeJoint), spool (spoolJoint), bail (bailJoint), mesh,
      activateActionId / activateActionSecondaryId
    The nodes: fishingRodRoot -> fishingRod -> bodyBoneJoint -> handleJoint, yokeJoint -> bailJoint, spoolJoint, segment01Joint
    -> tipLine (the rod's tip, 1.9 m along the root's +X; the reel below, at -Y); fishingRod -> body, handNode (the game's third person hold, a little up
    the handle from the root), firstPersonNode; fishingRodRoot -> ropeReferenceFrame -> rope_10_100 ...; the bobber is linked to
    the world. tool.handNode is the handNode (the stub's own guess: the i3d is in the packed DLC).
    tool.actionEvents: ACTIVATE_HANDTOOL -> onActivateAction and ACTIVATE_HANDTOOL_SECONDARY -> onActivateSecondaryAction, both
    on the class, both firing on press and release (not every frame). tool.activateAction(self, _, inputValue) and
    tool.activateSecondaryAction(self, _, inputValue) are the game's bindings (X on a pad, the mouse): they call the event's
    callback on those edges only.
    options: fishPull (0.5)

  What the rod does when its ACTIVATE_HANDTOOL handler is called (from play: hold to charge, let go to throw; the same action hooks
  and reels). Each call is in StubHandTool.useCalls as { tool = "fishingRod", action = "ACTIVATE_HANDTOOL", value, self }:
    idle, pressed      -> charging; each update the charge grows (angleScale += 0.0007 per ms, to 1)
    charging, released -> the cast: the line goes out (isInitialized, ropeLength 2 + 20 * angleScale), the bobber lands ropeLength
                          along the player's look ray as the game answers it now (Player:getLookRay: whether the real DLC aims
                          that way is not known), and the rod waits. Each cast is in StubHandTool.casts as
                          { angleScale, ray = { x, y, z, dirX, dirY, dirZ }, bobber = { x, y, z } }
    waiting            -> a bite comes only when the test says: StubHandTool.biteRod(tool) (not by the clock)
    bite, pressed      -> reeling (hooked); not pressed within the hook window (880 ms of updates) -> waiting again (got away)
    reeling            -> while the reel is held (pressed, not yet released) each update takes 10 m per second off the line;
                          at minLength the fish is caught: item set, the line in (isInitialized false), state caught
    caught, pressed    -> idle (the catch dialog is closed), item nil
  The secondary handler only records itself, as { tool = "fishingRod", action = "ACTIVATE_HANDTOOL_SECONDARY", value, self }:
  what it does is not known yet.
  The draw (onDraw, at the centre of the view): "castingChargeOverlay" while charging, "waitForBiteOverlay" while waiting and
  "hookFishOverlay" in the hook window; nothing otherwise.

  StubHandTool.rodState(tool) -> "idle" | "charging" | "waiting" | "bite" | "reeling" | "caught"   what the DLC is doing (the stub
    knows its own state tables; the Companion Mod has only the keys)
  StubHandTool.setRodState(tool, name)   puts the rod straight into a state, with the line and the bend that go with it
  StubHandTool.biteRod(tool)   a fish bites (waiting -> bite)
  StubHandTool.setFishPull(tool, pull)   how hard the fish pulls while reeling (0..1)
  StubHandTool.rodFaceRay(tool) -> x, y, z, dirX, dirY, dirZ   the rod's root and the way its face points, in the world: square
    to the rod, toward the side its reel hangs on (the way the tip swings in a cast)
  StubHandTool.pressSecondaryBinding(tool, inputValue)   the game's own ACTIVATE_HANDTOOL_SECONDARY binding
  StubHandTool.fishingRods   every rod made; StubHandTool.casts   every cast
  ```

- **`if _G.Overlay == nil then`**: The game's Overlay (dataS/scripts/gui/base/Overlay.lua), as far as the rod's draw uses it: Overlay.new(filename, x, y, width, height), and overlay:render(), looked up on the class at every call, which puts it on the screen at x + offsetX, y + offsetY. Defined here unless another stub has.
- **`renderTextNative`**: The game's natives for text and filled rectangles: the engine resolves natives at load, so a mod cannot wrap them (locals here, not globals). Each notes what reached the screen, a rectangle by the name the stub gives it.
- **`states`, `reeling`**: the minigame's layout, by the keys the #140 headset dump showed (its numbers rounded, and some of its keys left out)
- **`StubHandTool.installFishingRod`, `Class.onDraw`**: the draw: the charge meter, the waiting hint and the "Hook fish" prompt, at the centre of the view, each also put on the screen (StubHandTool.screen) from the state's Overlays or as text; and while reeling a minigame far to the right (the headset showed one there; its shape here is made up: a tension bar and its fill, Overlays, and a hint the native puts below the bar, at a place taken from the bar's Overlay, as the real minigame's panel and lines are)
- **`Class.onDraw`, `s`**: as the real minigame (#140 dump): the panel and the lines with the natives at the layout's numbers, the bars from the state's Overlays, and the fish and the hook placed again at every draw from where they are
- **`StubHandTool.fishingRod`, `root`**: the nodes: the rod lies along its +X, root to tip, the reel below it at -Y (the headset, #140: the rod's +Z is its side)
- **`StubHandTool.fishingRod`, `spec.state`**: until the rod's first update while held

### `tests/stubs/hand_tool_flashlight.luau`

- **the file**:

  ```text
  Hand Tools: the flashlight
  Run after hand_tool.luau (and after model_loading.luau: it uses setVisibility and setWorldQuaternion). Its own chunk, so that
  the other Hand Tools tickets do not edit the same lines. Shaped after the game's HandToolFlashlight (the readable source in
  the SDK's gameSource.zip), with the game's placement of the light as ADR-0029 describes it: at the first person camera.

  StubHandTool.installFlashlight(variant)   defines the global HandToolFlashlight, the class the game's flashlight is made of.
    Run it BEFORE loading the Companion Mod to have it there at file load, or any time before the map is loaded.
    variant   how the class's event HandToolFlashlight.onUpdate(tool, dt) is in this "game version": nil / "ok",
              "missing" (a game update renamed it to onUpdateBeam: the class has no onUpdate, the game updates through
              the new name) or "reshaped" (it takes no parameters at all, and updates the flashlight being held)
    StubHandTool.originalFlashlightOnUpdate   is the game's own onUpdate ("ok" game only: nil in the others)
    HandToolFlashlight.updateTransform(tool, isHeld)   the game's placement of the light node: at the camera of the carrying
      player, along its view. (Each flashlight keeps its own copy of the function, as the game's tools do, so that a wrap of
      the class does not reach it.)
    The game updates each carried flashlight once per StubHandTool.tick, after the targeter, through the class's onUpdate
    looked up at that moment, as the game raises its events.
  StubHandTool.flashlight(player, options) -> tool   the flashlight (spec_flashlight), not yet held. The spec is the game's:
    lightNode (the light, linked to the world root; visible only while the flashlight is on), lightNodeParent (the node the
    light was below in the model: the lens, 0.15 m along the tool's +Z), lightNodeOffsetX/Y/Z (where the light sits in the lens)
    and lightNodeForwardX/Y/Z (the way it shines in the lens, +Z), isActive.
    tool:setFlashlightIsActive(isActive, noEventSend)   the flashlight's own switch; every call is in
      StubHandTool.flashlightCalls as { tool = <the flashlight>, isActive = <the argument>, noEventSend = <the argument> }
    player.isFlashlightActive   the player's own flag for the light, false at first (the game restores the light from it)
    StubHandTool.beam(tool) -> x, y, z, dirX, dirY, dirZ   where the light node is now and the way it shines, in the world
    StubHandTool.lensRay(tool) -> x, y, z, dirX, dirY, dirZ   where the light leaves the lens and the way it shines, in the world
    StubHandTool.flashlights   every flashlight made, in order
    setDirection(node, dirX, dirY, dirZ, upX, upY, upZ)   the engine's call: the node's +Z axis along the direction
  ```

- **`StubHandTool.flashlight`, `tool.updateTransform`**: a tool's functions are copies made when it is made
- **`StubHandTool.flashlight`, `table.insert(StubHandTool.flashlights, tool)`**: the game's own binding for the flashlight is not the use binding: the tool has no activateAction

### `tests/stubs/hand_tool_hands.luau`

- **the file**:

  ```text
  Hand Tools: the game's empty hands (ADR-0033)
  StubHandTool.installHands(variant)   defines the global HandToolHands (the class the game's hands are made of; TARGET_MASK,
    and the events onDraw / onUpdate / onHeldStart as the game's event dispatch reads them off the class at every call), and makes
    the targeter honour target types (PlayerTargeter:addTargetType / :removeTargetType, :getLastLookRay, unless the spray can's
    chunk gave them already). Each update the targeter fills closestTargetsByKey[HandToolHands] with the closest loose object the
    look ray meets between the type's minimum (0.5 m) and maximum distance (spec.pickupDistance), measured from the ray's origin,
    as { node, x, y, z, distance }. LIKE THE GAME'S POOL it is ONE table per targeter that the next update overwrites: a mod that
    keeps a reference across frames reads another frame's target. Install it AFTER the other chunks that extend the targeter.
    variant   how the class is in this "game version": nil / "ok", "drawMissing" (a game update renamed onDraw to onDrawOverlays,
    which the draw calls instead), "drawReshaped" (onDraw takes no parameters), "canPickUpMissing" (getCanPickUpNode is gone;
    the hands tool has getCanPickUpTarget instead), "canPickUpReshaped" (it takes no node).

  StubHandTool.hands(player, options) -> tool   the hands tool (spec_hands), player.hands, not yet held. Like the game's registered functions,
    updateKinematicNode, getKinematicNode, getIsHoldingItem, getCanPickUpNode, orientRotationNodeToItem, pickUpTarget,
    dropHeldItem, throwHeldItemWithForceVector and onHeldItemJointBroken are COPIED onto the tool when it is made (a wrap of the
    class does not reach this tool; its events go through the class at every call). tool.spec_hands: hasSuperStrength (false),
    currentMaximumMass (StubHandTool.HANDS.MAXIMUM_MASS), pickupDistance (StubHandTool.HANDS.PICKUP_DISTANCE), heldItemNode,
    heldItemJointId (nil: the stub makes no joint), currentHoldDistance, kinematicNode, kinematicRotationNode (a child of it).
    spec_hands.dirtyFlag (1) with tool:raiseDirtyFlags(flag) recorded in StubHandTool.dirtyRaised ({ tool, flag }); tool.isServer
    (true, a single player game); tool:getIsAwaitingJointCreation() (spec_hands.awaitingJoint, false: the game's server makes the joint
    a few physics frames after the pick-up, and until then the rotation node is not at the object); the engine's setJointFrame
    (jointId, frame, node) recorded in StubHandTool.jointFrames ({ jointId, frame, node }). The stub makes no joint: a test sets
    spec_hands.heldItemJointId itself.
    Holding it (StubHandTool.hold) adds its target type to the player's targeter (0.5 m to pickupDistance), putting it away
    (StubHandTool.drop) removes it. It is NOT Player.currentHandTool: getHeldHandTool() stays nil. Holding another tool puts the held
    hands away, putting that tool away takes them again.
  StubHandTool.superStrength(tool, on)   the game's gsPlayerSuperStrengthToggle: the mass limit rises to
    StubHandTool.HANDS.SUPER_STRENGTH_MASS, the pickup distance to SUPER_STRENGTH_DISTANCE (10 m), and the target type is added
    again. The object's own getCanBePickedUp is not asked under super strength.
  StubHandTool.looseObject(x, y, z, mass, options) -> object   a loose object (a sphere of options.radius, 0.15 m, with a node
    at its centre) at the world position. object.node, object.mass (the game's unit, tonnes), object.canBePickedUp (true: false
    is an object that refuses the pick-up, like a strapped-down one). StubHandTool.looseObjects[node] -> object.
  The hands tool's methods, as the game has them and as a test sees what they did:
    getCanPickUpNode(node)   false for no node, a node that is gone, while holding something, a mass over currentMaximumMass, or an
      object that refuses (unless super strength)
    pickUpTarget(target)   false unless getCanPickUpNode(target.node); otherwise holds it (heldItemNode, currentHoldDistance =
      target.distance), puts the rotation node at the object's centre and the kinematic node along the look ray; recorded in
      StubHandTool.pickUps as { tool, node, x, y, z, distance }
    dropHeldItem(noEventSend)   recorded in StubHandTool.drops ({ tool, held = node }) when something was held; lets go
    throwHeldItemWithForceVector(dirX, dirY, dirZ, force, noEventSend)   recorded in StubHandTool.throws ({ tool, node, dirX,
      dirY, dirZ, force }); lets go
    onHeldItemJointBroken(jointIndex, impulse, noEventSend)   what the game's joint break report calls: recorded in
      StubHandTool.breaks ({ tool, jointIndex }); lets go. dropHeldItem is the game's own letting go: it passes jointIndex nil.
    updateKinematicNode()   the game's (nothing unless the hands are held and the player is the owner): while holding, the kinematic node goes to the look ray's origin plus its direction times
      currentHoldDistance, turned to the ray's heading (level); run each tick after the targeter for a held hands tool
  StubHandTool.pressPickUp(tool)   the game's pick-up button: pickUpTarget with the targeter's closest target for the hands, when
    getCanPickUpNode accepts it. StubHandTool.breakJoint(tool): the held item's joint breaks (onHeldItemJointBroken(1, 500)).

  The draw: StubHandTool.draw() renders "crosshair" (nothing under the centre), "pickUpCrosshair" (holding, or the hands' target
  is one getCanPickUpNode accepts), for the owner in the first person view, as the game's onDraw does; StubHandTool.rendering(tool).
  ```

- **`StubHandTool.installHands`, `local baseHold, baseDrop = StubHandTool.hold, StubHandTool.drop`**: the hands are held when no Hand Tool is (the game: Player.hands is held and Player:getHeldHandTool() is nil; taking a Hand Tool puts the hands away, putting it away takes them again). Held hands target loose objects, put away they stop (the game's onHeldStart / onHeldEnd). Player.currentHandTool is never the hands.
- **`StubHandTool.hands`, `tool.isServer`**: a single player game is the server: it applies the rotation node to the joint itself

### `tests/stubs/hand_tool_horse_brush.luau`

- **the file**:

  ```text
  Hand Tools: the horse brush (ADR-0030)
  The horse brush for StubHandTool, shaped after the game's HandToolHorseBrush (the readable source in the SDK's gameSource.zip). Run
  after hand_tool.luau, then call StubHandTool.installHorseBrush(handlerVariant, lookupVariant) BEFORE loading the Companion Mod, or any
  time before the map is loaded. A game with the spray can too: install the horse brush AFTER the spray can (both fill the targeter).

  StubHandTool.installHorseBrush(handlerVariant, lookupVariant)   defines the global HandToolHorseBrush (the class the game's horse
    brush is made of), AnimalCleanEvent and the game's farm, husbandry and network objects the brush reads, and makes the targeter
    honour the animal target type (PlayerTargeter:addTargetType / :removeTargetType / :getClosestTargetedNodeFromType when the spray
    can has not added them): each update fills closestTargetsByKey[HandToolHorseBrush] with the closest animal the look ray meets,
    measured from the ray's origin and only between the type's minimum and maximum distance (the game's tryAddTargetWithMask), as
    { node, x, y, z, distance }. Holding a brush adds its target type (animals, 0.5 m to 3 m, as onHeldStart does); putting it away
    removes it.
    handlerVariant   how the use handler HandToolHorseBrush.onBrushAction is in this "game version": nil / "ok", "missing" (a game
                     update renamed it to onBrushUse, which the game's binding calls instead) or "reshaped" (it takes no parameters)
    lookupVariant    how the animal lookup HandToolHorseBrush.getHusbandryAndClusterFromNode(player, node) is: nil / "ok", "missing"
                     (renamed to getAnimalFromNode, which the brush's update calls) or "reshaped" (it takes only the node)
    HandToolHorseBrush.onUpdate(tool, dt)   the brush's update, run each tick (StubHandTool.updaters) for a held brush, looked up on
      the class at every call: asks the lookup (on the class) for the animal and cluster of the targeter's closest animal, sets the
      action event's text ("Clean <name>") and active state to match, and while the brush is cleaning an animal (spec.isCleaning with
      one targeted) sways the brush node, plays the cleaning sound (spec.cleaningSoundPlaying) and sends an AnimalCleanEvent every
      half second (+5 dirt cleaned, the game's cleaningDeltaPerMs of 0.01).
    HandToolHorseBrush.onDraw(tool)   the brush's draw (StubHandTool.draw), looked up on the class at every call: renders
      "brushCrosshair" while an animal is targeted, "defaultCrosshair" otherwise (both at the centre of the view)
    HandToolHorseBrush.onBrushAction(tool, _, inputValue)   the ACTIVATE_HANDTOOL handler, on the class ONLY (not on the tool): the
      game's binding calls it with the tool as self; held (spec.isCleaning = inputValue > 0); recorded in StubHandTool.useCalls as
      tool = "horseBrush". The binding itself, tool.activateAction, calls whatever the variant calls it.
    HandToolHorseBrush.getHusbandryAndClusterFromNode(player, node) -> husbandry, cluster   nil, nil without a valid node, an
      animal, an animal of a farm the player may access (the game's accessHandler:canFarmAccess)

  StubHandTool.horseBrush(player, options) -> tool   the horse brush (spec_horseBrush), not yet held. tool.rootNode, and in
    tool.spec_horseBrush: brushNode (`brush`, whose -Y axis runs toward the bristles), originalPos, animalDetectionDistance (3),
    isCleaning, targetedHusbandry, targetedHusbandryId, targetedClusterId, deltaSum, brushText ("Clean %s"), cleaningSoundPlaying.
    The player gets farmId 1 when it has none.
  StubHandTool.bristleRay(tool) -> x, y, z, dirX, dirY, dirZ   where the bristles are and the way they point, in the world: the origin
    of the brush node and its -Y axis
  StubHandTool.animalAt(ray, distance, radius, options) -> animal   a spherical animal the ray (x, y, z, dirX, dirY, dirZ in a table)
    meets `distance` metres along it. options: name ("Silver"), farmId (1: the player's), dirt (50). animal = { node, husbandry,
    cluster, ... }, cluster.dirt the animal's dirt; StubHandTool.animals lists them
  StubHandTool.inputHelp(tool) -> text   what the input help offers for the brush now: the action event's text while it is active
    (e.g. "Clean Silver"), "" while it is not. Together with StubHandTool.actionEvents[tool.spec_horseBrush.activateActionEventId]
  StubHandTool.cleanEvents   every clean event the brush sent (the game's AnimalCleanEvent), in order, as { husbandry, clusterId, delta }
  ```

- **`StubHandTool.horseBrush`, `spec.brushNode`**: the brush node (`brush`) is the root's origin; the bristles (`fibers`, 4 cm along its -Y) are what touches the animal
- **`StubHandTool.horseBrush`, `tool.activateAction`**: the game's own binding calls the handler off the class, with the tool as self

### `tests/stubs/hand_tool_spray_can.luau`

- **the file**:

  ```text
  Hand Tools: the spray can (ADR-0030)
  The spray can for StubHandTool, shaped after the game's HandToolSprayCan (the readable source in the SDK's
  gameSource.zip). Run after hand_tool.luau (it needs model_loading.luau only through the Virtual Hands), then call
  StubHandTool.installSprayCan(variant) BEFORE loading the Companion Mod, or any time before the map is loaded.

  StubHandTool.installSprayCan(variant)   defines the global HandToolSprayCan (the class the game's spray can is made of) and makes the
    targeter honour target types: PlayerTargeter:addTargetType(key, mask, minDistance, maxDistance) and :removeTargetType(key),
    and each update fills closestTargetsByKey[key] with the closest trunk the look ray meets, measured from the ray's origin and
    only between the type's minimum and maximum distance (the game's tryAddTargetWithMask), as { node, x, y, z, distance }.
    Holding a can adds its target type (trees, 0.5 m to spec.sprayDetectionDistance, 1.5 m), as onHeldStart does; putting it
    away removes it.
    HandToolSprayCan.onUpdate(tool, dt)   the can's update, run each tick (StubHandTool.updaters) for a held can, looked up on
      the class at every call: reads the found tree, acts on the RISING edge of spec.activatePressed (a marker event, or the
      game's blinking warning when the can is empty or the tree is not the player's), places a delayed marker, shakes the can
      node while spraying, ends the spray after its duration (500 ms on g_time) and clears spec.activatePressed.
    HandToolSprayCan.onDraw(tool)   the can's draw (StubHandTool.draw), looked up on the class at every call: renders
      "markerTypeIcon" and, while a tree is found, "treeIcon" (both at the centre of the view); nothing for an empty can.
    variant   how that onDraw is in this "game version": nil / "ok", "missing" (a game update renamed it to onDrawOverlays,
      which the draw calls instead) or "reshaped" (it takes no parameters, and draws the can being held)
    The functions the game registers on the tool: activateSpraying(_, inputValue) (spec.activatePressed = inputValue ~= 0; the
      handler the game's ACTIVATE_HANDTOOL binding calls every frame; recorded in StubHandTool.useCalls as tool = "sprayCan"),
      tryToAddTreeMarker, processDelayedMarker, setIsSpraying(isSpraying, force), changeTreeMarkerType(_, inputValue) and
      getIsSprayingAllowed(shape).

  StubHandTool.sprayCan(player, options) -> tool   the spray can (spec_sprayCan), not yet held. options.markers: how many
    markers it holds (default 50). tool.rootNode, and in tool.spec_sprayCan: canNode (sprayCan_vis, the game's shake node: the
    can's visual node, whose +Z axis is the way it sprays), remainingNumMarkers, activatePressed, wasActivatePressed, isSpraying,
    sprayStopTime, sprayDuration, sprayDetectionDistance, foundTargetTree ({ node, x, y, z, distance } or nil), treeMarkerTypeIndex,
    treeMarkerType ({ name, title, iconFilename }, see the marker types below).
    tool.activateAction is activateSpraying: the game's binding. g_currentMission also gets showBlinkingWarning (recorded in
    StubHandTool.warnings as { text, ms }, text being what the game was asked to show) and a treeMarkerSystem with three marker types (CROSS, ARROW, CIRCLE)
    when it has none, and a g_i18n (StubHandTool.texts) when there is none.
  StubHandTool.canRay(tool) -> x, y, z, dirX, dirY, dirZ   where the can sprays from and along, in the world: the origin of canNode
    and its +Z axis
  StubHandTool.treeAt(ray, distance, radius) -> tree   a tall trunk the ray (x, y, z, dirX, dirY, dirZ in a table) meets at
    `distance` metres along the ray (a vertical trunk: the horizontal distance of its surface is distance times the horizontal part of the
    direction). StubHandTool.trees lists them. StubHandTool.forbid(tree): the player may not mark it (getIsSprayingAllowed).
  StubHandTool.markers   every marker event the can sent (the game's SprayCanEvent), in order, as { tool, typeIndex, shape, x, y, z,
    hitX, hitY, hitZ } with x, y, z the look ray's origin the game passed; StubHandTool.placedMarkers: the markers the game then
    placed on the tree (addTreeMarkerCameraBased), same fields; StubHandTool.warnings: the blinking warnings
  ```

- **`StubHandTool.installSprayCan`, `markerTypes`**: the marker system and the player's rights The game's data/maps/maps_treeMarkerTypes.xml gives each type a name (an identifier) and a title (an l10n key written "$l10n_treeMarker_cross"), an icon and a texture; the icon's filename is the one field the readable Lua reads off the type (iconFilename). That the native type keeps the title as `title`, and unresolved, is a guess (headset-only).
- **`StubHandTool.sprayCan`, `spec.canNode`**: the can stands upright in its i3d: the visual node, a little up and forward of the root, sprays along its +Z

### `tests/stubs/hand_tools_scene_helpers.luau`

- **the file**:

  ```text
  Hand Tools: scene helpers
  Lua helpers for reading the scene as the player would see it, run once after the scene exists.
    isBelow(node, ancestor)           the node hangs somewhere below the ancestor
    handHolder(which)                 the Virtual Hand's node ("left" or "right"), nil without one
    distance(a, b)                    metres between two nodes' world positions
    offsetInHolder(node, which)       the node's position in the Virtual Hand's space, x y z
    shown(which)                      the Virtual Hand is shown
    fingerCurl(which, finger)         the curl of the three joints of a finger of a Virtual Hand, radians, toward the palm
  ```

### `tests/stubs/input_binding.luau`

- **the file**:

  ```text
  The game's input binding
  A stand-in for g_inputBinding and Binding, shaped after the finding of spike #99 (spec #98): the game's InputBinding Lua is
  stripped in the readable source, so this models only what the spike saw it do.
    - g_inputBinding:getActionByName(name) -> action | nil. action.bindings is an array of Binding objects (deviceId,
      inputString, axisComponent, neutralInput, index, id), those of devices that are not plugged in included. The methods
      are on the metatable, so pairs(g_inputBinding) does not list them.
    - Binding.new(deviceId, { inputs }, axisComponent, negative) as the game measured it: the sign of an axis input comes only
      from `negative` (any non-nil value), a "-" inside the name is dropped ({"AXIS_3-"} becomes "AXIS_3+"), and the
      argument after the list is the axisComponent. Then the fields set by hand and b:makeId(); g_inputBinding:addBinding(action, b)
      is true when added, false (after the game's "occupied slot" log line) when the device already has a binding of that
      action at b.index.
    - g_inputBinding:getDeviceById(deviceId) is the device table while the pad is connected and nil for a pad that is only saved (an unplugged
      one); g_inputBinding:getMissingDeviceById(deviceId) is the other way round (the spike's finding).
    - g_inputBinding:deleteBinding(...) says true and removes nothing. Taking a binding out of action.bindings does remove it,
      but not from action.activeBindings, which is what the game acts on (the Controls screen shows .bindings): a removed
      binding keeps acting until g_inputBinding:resetActiveActionBindings() rebuilds activeBindings from bindings.
      addBinding adds to .bindings only.
    - g_inputBinding:saveToXMLFile() takes no path; StubInput.saves counts the calls and StubInput.saved is the listing at the
      last one.

    StubInput.install(options)
      options.devices  the pads the game has applied GAMEPAD_DEFAULT to, as device ids (default 0_ and 3_XINPUT_GAMEPAD); a
                       pad that is not plugged in counts. Each gets, at index slot + 1 as in the saved file, the Companion Mod's
                       GAMEPAD_DEFAULT bindings (VR_RECENTER_VIEW BUTTON_11, VR_HUD_TOGGLE BUTTON_10, VR_SNAP_TURN_LEFT BUTTON_5,
                       VR_SNAP_TURN_RIGHT BUTTON_6, VR_TOGGLE_VIEW BUTTON_9) and the game's own (smooth look on the right
                       stick X on foot and in vehicles, JUMP and look up / down), next to the keyboard's.
      options.unplugged  a list of device ids in devices whose pad is saved but not connected: getDeviceById says nil for them
                       until StubInput.plug(deviceId)
      options.variant  "ok" (default); "missing": no g_inputBinding; "reshaped": no addBinding; "raises": addBinding raises
                       on its second call, after the Companion Mod has taken bindings out; "saveRaises": saveToXMLFile raises;
                       "noReset": no resetActiveActionBindings; "resetRaises": resetActiveActionBindings raises
      options.omit     a list of "ACTION@device" default bindings left out
      options.extra    a list of { action, device, input, index } bindings added to the defaults
      options.state    a string from StubInput.serialize(): the bindings of an earlier game session, in place of the defaults
    StubInput.addDevice(deviceId)      the game lists one more pad (connected) and applies GAMEPAD_DEFAULT to it
    StubInput.plug(deviceId)           the game attaches a listed pad as a connected device
    StubInput.list(action, deviceId) -> string   the device's bindings of the action as "input/axisComponent@index", sorted,
                       separated by ' ; ' ("" when there are none)
    StubInput.listActive(action, deviceId) / StubInput.dumpActive(deviceId)   the same for action.activeBindings
    StubInput.get(action, deviceId) -> Binding   the device's first binding of the action
    StubInput.dump(deviceId) -> string   all of the device's bindings as "ACTION: list" lines (every device when nil)
    StubInput.add(action, deviceId, input, index)   one more binding on the device (in bindings and activeBindings)
    StubInput.change(action, deviceId, from, to)   the player changes a binding in the Controls screen: its input becomes `to`
    StubInput.serialize() -> string    every binding, for a later game session
    StubInput.saves, StubInput.saved   see above
  ```

- **`Binding.new`**: The game's Binding.new(deviceId, inputNames, axisComponent, negative), as measured in the game: the second argument after the list is the axisComponent, and ANY non-nil fourth argument makes the input negative. The sign of an axis input comes from that flag alone: a "-" (or "+") given inside the name is dropped, so {"AXIS_3-"} comes out "AXIS_3+". inputComponent is "-" for a negative input, otherwise "+".
- **`put`**: The game loading or applying a binding: it is in action.bindings and in action.activeBindings (what the game acts on). A trailing "-" on an axis input is the binding's negative flag.

### `tests/stubs/interactive_control.luau`

- **the file**:

  ```text
  Interactive Control
  StubIC.install(variant)  installs the globals the Companion Mod looks at, in the release-1.2.0.1 shape
    variants: nil / "ok", "renamedUpdateClickable" (the Click Point class lacks updateClickable),
              "missingExecute" (the manager lacks onActionEventExecute)
    Without a call there is no Interactive Control at all (g_modIsLoaded has no entry).
  StubIC.addPoint(name, x, y, z, options) -> Click Point at a world position
    options: analog (bool), inRange (default true: IC activates it), screenX, screenY (where the stub's
    mouse test finds it; default 0.5, 0.5), facesAway (bool: like a static outdoor Click Point facing away
    from the camera, IC tests it with no mouse position, so it is never clickable), outdoor (bool: on a
    vehicle's exterior, for a player on foot; see StubIC.tick), offScreen (bool: it projects off the screen, so
    IC runs no mouse test on it at all and its clickable state stays as it was)
  StubIC.equip(vehicle, state) / StubIC.stateSetting (1 toggle, 2 always on, 3 always off) /
    StubIC.stateWrites / StubIC.changeCamera(vehicle, cam)  the vehicle side, see below
  StubIC.tick()  one frame of IC's own update of its Click Points (activation, the mouse hit test through
    updateClickable, hover blink). Like IC, a Click Point in a cab updates every frame (IC updates those from the
    vehicle's draw), an outdoor one only on the vehicle's update tick: every StubIC.outdoorTickFrames frames
    (default 4). StubIC.framesToOutdoorTick is how many frames until the next tick (1: the next frame has it).
  StubIC.executed  names of the Click Points IC executed through its controller, in order
  StubIC.blinks[name]  hover blinks IC drew on a Click Point     StubIC.mouseX / mouseY  the desk mouse
  StubIC.selected()  name of the Click Point IC currently treats as the clickable one, or nil
  ```

- **`StubIC.equip`**: StubIC.equip(vehicle, state) gives a vehicle Interactive Control's per-vehicle state: the synced on/off state (spec_interactiveControl.state), isInteractiveControlActivated and activateInteractiveControl(state, noEventSend), which records every call in StubIC.stateWrites as { state, noEventSend } and, like IC, changes the state only when it differs.
- **`StubIC.changeCamera`**: StubIC.changeCamera(vehicle, cam): the vehicle's active camera becomes `cam`. Like IC (unless it keeps itself alive) the vehicle switches itself off when the new camera is an inside camera.
- **`StubIC.tick`, `if not StubIC.noVehicleDraw then point:update(true, false, true) end`**: The cab's Click Points are updated from the vehicle's draw, which the game skips while the HUD is hidden (StubIC.noVehicleDraw).

### `tests/stubs/manual_attach.luau`

- **the file**:

  ```text
  Manual Attach (ADR-0035)
  StubManualAttach.install(variant)   defines what Manual Attach has, in its mod environment (_G.FS25_manualAttach, with g_modIsLoaded):
    g_manualAttach (canPlayerPerformManualAttachment, vehicleAttachmentHandler), the VehicleAttachmentHandler class (the constants
    MAX_ATTACH_DISTANCE_SQ, MAX_ATTACH_ANGLE and PLAYER_MIN_DISTANCE_SQ; the static getAttachableInJointRange and isVehicleValid; the instance
    methods updateCandidates, executeAttachment and getTargetImplement, and the five candidate fields), and the three attachments of the handler
    (vehicleJointAttachment, powerTakeOffAttachment, connectionHosesAttachment, each with getActionEventInfo and performAttachment). The
    mission's vehicleSystem.inputAttacherJoints, accessHandler:canFarmAccess and showBlinkingWarning are the game's.
    variant   how Manual Attach is in this "release": nil / "ok", "globalFallback" (g_manualAttach and the class are plain globals, not in the
    mod environment), "notLoaded" (the mod is not loaded), "instanceMissing" (no g_manualAttach), "handlerMissing" (no attachment handler on it),
    "classMissing" (no VehicleAttachmentHandler class), "rangeHelperMissing" / "rangeHelperReshaped" (getAttachableInJointRange renamed / taking
    fewer parameters), "validHelperMissing" / "validHelperReshaped" (isVehicleValid), "constantsMissing" (PLAYER_MIN_DISTANCE_SQ gone),
    "actionInfoMissing" / "actionInfoReshaped" (the joint attachment's getActionEventInfo), "executeMissing" / "executeReshaped"
    (executeAttachment), "performMissing" (the joint attachment's performAttachment), "detectedMissing" (the handler has no detectedVehicles),
    "handlerFlagMissing" (the handler has no playerCanPerformManualAttachment, which its candidate update reads),
    "ptoActionInfoMissing" / "ptoActionInfoReshaped" / "ptoPerformMissing" and "hoseActionInfoMissing" / "hoseActionInfoReshaped" /
    "hosePerformMissing" (the same of the PTO and the hoses attachments), "ptoLookupReshaped" (the game's vehicles have the PTO specialization
    without getOutputPowerTakeOffs), "hoseTargetsReshaped" (the game's vehicles have spec_connectionHoses without targetNodes).
  StubManualAttach.tick()   Manual Attach's update for one game frame: the flag is true when the player is on foot, has no Hand Tool in hand and
    the hands hold nothing; the handler's candidate update then picks its nearest candidates (as its own does).
  StubManualAttach.detect(...) / detectAll()   the vehicles Manual Attach's detection has found (the handler's detectedVehicles).
  StubManualAttach.performs   every performAttachment of an attachment, in order: { kind ("joint", "pto", "hoses"), attacherVehicle, jointIndex,
    attachable, attachedImplement }. StubManualAttach.warnings   every blinking warning, as the text key (info_lower_warning,
    info_detach_pto_warning, info_detach_hoses_warning). StubManualAttach.performCount(kind)
  The vehicles (manual_attach_vehicles.luau): vehicle(name, joints, options), tractor(options), trailer(options), implement(name, options), lineUp, attachTo, ...
  ```

- **`S.install`, `other`**: the PTO and hoses attachments (PowerTakeOffAttachment.lua, ConnectionHosesAttachment.lua): they act on the attached Implement alone (its attacher vehicle is where the PTO and hoses go), as the real ones do: a connect when it can be connected, else a disconnect. The Implement has a PTO or hoses when it says so (hasPto, hasHoses) and its vehicle has the outputs or targets for them. `prefix` names the variants: \<prefix>PerformMissing, \<prefix>ActionInfoMissing, \<prefix>ActionInfoReshaped.

### `tests/stubs/manual_attach_front_and_rear.luau`

- **the file**: Manual Attach: a tractor with an Implement at each hitch A tractor with an Implement lined up at each hitch: the rear one 2 m ahead (where the ray of kHandX runs), the front one nearer to the player and to the side (where the ray of a hand 0.8 m to the side runs, at world -1.2, -0.2, 1.0), so Manual Attach's own nearest candidate is the front one. \_G.tractor, \_G.rearPlow (joint 1), \_G.frontLoader (joint 2).

### `tests/stubs/manual_attach_tractor_and_implement.luau`

- **the file**: Manual Attach: a tractor and a plow The same tractor with a plow lined up at its rear hitch, as \_G.tractor and \_G.plow.

### `tests/stubs/manual_attach_tractor_with_attached_implement.luau`

- **the file**: Manual Attach: a tractor with a plow attached The same tractor with a plow that has a PTO and hoses already attached at its rear hitch, as \_G.tractor and \_G.plow (the PTO and the hoses are not connected yet).

### `tests/stubs/manual_attach_vehicles.luau`

- **the file**:

  ```text
  Manual Attach: the vehicles
  The vehicles Manual Attach works on, as scene nodes (shaped after the game's AttacherJoints and Attachable): a vehicle with attacher
  joints, an Implement with an input attacher joint, and what lines up and attaches them.

  StubManualAttach.vehicle(name, joints, options) -> vehicle   a vehicle with an attacher joint at each world position { x, y, z } of `joints` (index
    1.. in order). options: farmId (default 1). vehicle.rootNode, vehicle.spec_attacherJoints.attacherJoints[i] = { jointTransform, jointType,
    jointIndex (0 free, else attached), allowsLowering, moveDown }, getAttachedImplements() -> { { object, jointDescIndex } }, getAttacherVehicle(),
    getAttacherJointByJointDescIndex(i), getAttacherJointDescFromObject(object), getActiveFarm(), isDetachAllowed(), getFullName(). It is also an
    Implement: its own input attacher joint is at its root (a trailer is one: StubManualAttach.trailer).
  StubManualAttach.tractor(options) -> vehicle   joint 1 is the rear hitch at options.rear (default -0.4, -0.2, 2.0), joint 2 the front one at
    options.front (default 0.8, -0.2, 2.0). It has the vehicle side of the PTO and the hoses for each joint (options.pto, options.hoses, both
    on by default; options.passThroughPto: PTO outputs with no node of their own instead): a PTO output with an output node 0.4 m above the
    joint and 0.3 m behind it (getOutputPowerTakeOffs(), spec_powerTakeOffs.outputPowerTakeOffs), a hose target node 0.35 m to the joint's +X
    and a custom hose target 0.35 m to its -X, both 0.15 m above and 0.3 m behind (spec_connectionHoses.targetNodes,
    customHoseTargetsByAttacher). Any vehicle() takes these options too.
  StubManualAttach.implement(name, options) -> vehicle   an Implement with no hitch of its own, 20 m away from everything until lined up.
    options: farmId, allowsLowering (default true), hasPto, hasHoses (the Implement has a PTO / hoses; default no). The PTO and hoses attachments
    connect or disconnect them (implement.ptoConnected, implement.hosesConnected) when the Implement is attached and its vehicle has the
    outputs or targets, and Manual Attach refuses to detach while they are connected.
  StubManualAttach.trailer(name, hitch, options) -> vehicle   an Implement with a hitch of its own at the world position `hitch`.
  StubManualAttach.lineUp(implement, vehicle, jointIndex)   the Implement's input joint is at the hitch, lined up. misalign(implement): turned across it.
  StubManualAttach.attach(vehicle, implement, inputJointIndex, jointIndex) / detach(vehicle, implement)   Manual Attach's own attach and detach of the
    state (the stand-in's perform path calls them): the hitch is taken, the Implement is lowered when it allows lowering.
  StubManualAttach.raise(implement)   the hitch of an attached Implement is raised (not lowered): Manual Attach refuses to detach it.
  ```

- **`newVehicle`, `if options.pto or options.passThroughPto then`**: the vehicle side of the PTO and the hoses (PowerTakeOffs.lua, ConnectionHoses.lua), per attacher joint, beside it on the tractor's side: a PTO output with an output node 0.4 m above the joint and 0.3 m behind it, a hose target node 0.35 m to its +X and a custom hose target 0.35 m to its -X (both 0.15 m above and 0.3 m behind). options.pto / options.hoses: have them; options.passThroughPto: PTO outputs with no node of their own that skip to the vehicle's own attacher vehicle (a trailer passing the tractor's PTO on).

### `tests/stubs/model_loading.luau`

- **the file**:

  ```text
  Model loading
  Run after scene_graph.luau. The engine functions the Virtual Hands call to bring in their model, with a
  stand-ins for the shipped Gloved Hand files (VirtualHandLeft.i3d, VirtualHandRight.i3d): each a file root whose
  first child is the hand's root node, with the Wrist > Hand > finger bone chains below it, one shape with the real
  mesh's bounds, and the real bone names and (roughly) positions in the files' own units: METRES, like the shipped
  files, which carry their real-world size and their handedness themselves. The right file is the left one mirrored
  across the model's X = 0 plane (positions with x negated, the Euler Y and Z angles of every bone negated), with the
  same node names. The stub is served by the file name asked for: a name containing "VirtualHandRight" is the right
  hand's.

    StubModel.fileExists (default true)   what fileExists() answers for the model files
    StubModel.missingFiles                set of file names (e.g. { ["VirtualHandRight.i3d"] = true }) fileExists() says
                                          are not there, whatever fileExists is
    StubModel.failLoad (default false)    loadI3DFile answers 0 (and a failure reason) although the files exist
    StubModel.failFiles                   set of file names loadI3DFile fails for
    StubModel.loads                       { { filename, root } } per loadI3DFile call that reached the "engine"
    StubModel.clones                      node ids of every clone() of a node (the hands), in order
    StubModel.deleted                     node id -> true for every deleted node
    StubModel.hands() -> { left = node, right = node }   the hands' holder nodes (named virtualHand_left and
                          virtualHand_right by the mod): each holder is placed at its grip pose, so its local space is the
                          grip pose frame and everything of the hand hangs below it; a missing one is nil
    StubModel.model(hand) -> node         the cloned model below a hand's holder
    StubModel.scaledNodes(node) -> list   the names of the nodes among the node's ancestors and descendants whose scale
                                          is not exactly 1 (none may be: the engine ignores the scale above a skinned mesh)
    StubModel.setScaleCalls               how many times anything called setScale
    StubModel.pointInAncestor(node, ancestor, x, y, z) -> x, y, z   a point of a node's local space in an ancestor's
    StubModel.jointInGripFrame(hand, "index1") -> x, y, z   where a joint is, in the grip pose frame of the hand
    StubModel.meshBoundsInGripFrame(hand) -> minX, minY, minZ, maxX, maxY, maxZ   the box around the glove mesh there
    StubModel.isShown(node)               the node and every ancestor are visible (and it exists)
    StubModel.castsShadow(node) -> true when any shape below casts one
    StubModel.isLinkedToScene(node)       the node's ancestors lead up to the scene root
    StubModel.missingJoints               set of node names of the model's file (e.g. { ["Middle.001"] = true }) the
                                          next loaded model lacks
    StubModel.joint(hand, name) -> node   the finger joint below a hand node, or nil ("thumb1".."little3": finger
                                          and curl segment, looked up in that finger's own subtree)
    StubModel.curl(hand, name) -> radians how far the joint is curled from its rest pose, TOWARD THE PALM, which for
                                          this model (as for the shipped one) is a NEGATIVE rotation about the joint's own
                                          X axis; errors when the joint turned about any other axis
  Every joint of the stub model rests in a slightly bent pose that is NOT the identity rotation (each segment a
  different X angle, plus a small Y and Z twist), so a curl that ignored the model's rest pose would show. Like the
  shipped model's, the stub's joint rotations are small bends relative to their parent: a bone's rotation is NOT a
  90 degree turn of the bone axis (the GIANTS exporter wrote those, and the bones composed to a heap).
  Nodes are visible, unscaled and (for shapes) casting shadows by default, like freshly loaded ones.
  ```

- **`STUB_FINGERS`**: The joint names of the shipped model (VirtualHandLeft.i3d), written out here on purpose: the stub stands for the file, the mod must find these by its own table. Per finger: the base bone, then the three curl joints. The thumb's joints are called Lower, Middle and Top, like the fingers' without the .00N suffix.
- **`STUB_METRES_PER_UNIT`**: The shipped LEFT model's geometry, in the glove's original units (about 2.1 units from the wrist to the middle fingertip) and in the file's own axes: fingers along -Z, the palm facing -Y (the back of the hand +Y), the thumb on the +X side. The numbers are the model's bone positions and mesh bounds as the Blender file gives them, in the exporter's Y-up axes. The shipped files are these times 0.0875 m per unit (a real hand is about 0.19 m long), so buildHand scales them; the right hand is mirrored across X = 0.
- **`buildHand`, `local x, y, z = 0, 0, 0`**: the model's relaxed pose, like the shipped one's: each joint flexed a little relative to its parent (a NEGATIVE turn about X is toward the palm in this model; the shipped file has -3 to -9, -15 to -21 and -14 to -21 degrees for the three segments), with a small twist. The bones run along their local -Z, as the shipped ones do. The first joint sits at the finger's knuckle.
- **`StubModel.curlOfNode`, `return -2 * math.atan2(x, w)`**: a negative turn about X flexes this model's fingers toward the palm, so that is a positive curl

### `tests/stubs/object_grab_helpers.luau`

- **the file**:

  ```text
  Object Grab: Lua helpers
  Lua helpers: the rays a test compares, each { x, y, z, dirX, dirY, dirZ } in the world, and what was drawn.
    targeterRay()      what the targeter cast in the last game frame
    headRay()          what the game's own getLookRay answers for the player: the first person camera
    pointerRay()       the Pointer Ray the Companion Mod placed (VRCompanion:getPointerRay), nil without one
    pulledBack(r, m)   the ray r with its origin moved back along it by m metres
    sameRay(a, b), apart(a, b), rayText(r)
    drawnRayEnd()      runs the camera path (which draws the Pointer Ray for a frame) and answers the end point (x, y, z) of the line it drew for the Pointer Ray, or nil
  ```

### `tests/stubs/scene_graph.luau`

- **the file**:

  ```text
  Scene graph
  Nodes are plain tables. Euler angles use the engine convention R = Ry * Rx * Rz.

    StubScene.newNode(name, parent, x, y, z, rx, ry, rz) -> node id   (parent 0 = world root)
    StubScene.worldPosition(node) -> x, y, z      StubScene.worldDirection(node, x, y, z) -> x, y, z
    The engine's localToWorld(node, x, y, z) and worldToLocal(node, x, y, z) carry a point between a node's space and the world.
    StubScene.cab(options) -> { vehicle, cam, cameraNode, headTrackingNode, parent }
        options: x, y, z, yaw  = the cab's pose in the world (default 0, 0, 0, 0)
      A vehicle in the cab view: camParent -> cameraNode, headTrackingNode (1.5 m up, turned by pi like
      the game's cab cameras). g_currentMission.controlledVehicle is the vehicle and
      StubScene.updateCamera() runs VRCompanion:onVehicleCameraUpdate for it.
    StubScene.outside(options) -> { cam, cameraNode }   an Outside Camera of the controlled vehicle at the world
        pose x, y, z, yaw (the engine only reads it); StubScene.updateCamera() runs onVehicleCameraUpdate
    StubScene.onFoot(options) -> { parent, yawNode, firstPersonCamera }   (a player on foot)
        options: x, y, z, yaw. StubScene.updateCamera() then runs onPlayerCameraUpdate.
  ```

### `tests/stubs/steering_vehicle.luau`

- **the file**:

  ```text
  A vehicle with a Steering Wheel
  Run after scene_graph.luau. The vehicle side of Wheel Grab (ADR-0027): what the game does with a steering input, as
  the real Drivable and Wheels code does it (Drivable.lua setSteeringInput / onUpdate / updateSteeringWheel,
  Wheels.lua updateVehiclePhysics), so a test sees what the player would: the calls that reached the vehicle and how
  the Steering Wheel turned.

  StubSteer.install(variant)  defines the globals the game defines: InputDevice.CATEGORY and the Drivable class whose
    setSteeringInput records every call that reaches a vehicle in StubSteer.calls as { value, analog, category }
    and then stores the input like the game (the analog flag and category only change on a non-zero input).
    variant: nil ("ok"), "missing" (a game update renamed it: the class has setSteeringAxis, same arguments, and no
    setSteeringInput) or "reshaped" (setSteeringInput(vehicle, { value, isAnalog, category }) takes one table).
    or "raises" (the "ok" function, but a call in the Wheel Peripheral's category -- the one Wheel Grab makes -- raises
    an error and records nothing; the other categories work). StubSteer.failNextWheelCalls(n) makes only the next n
    such calls raise, in any variant.
    Run it BEFORE loading the Companion Mod to have Drivable there at file load, like the game; StubSteer.original is
    the unwrapped function of the "ok" and "raises" variants.
  StubSteer.keyboard / gamepad / wheelPeripheral (vehicle, value)  another device steering: the call it makes (by the
    variant's entry point); StubSteer.send(vehicle, value, isAnalog, category) is the general form.
  StubSteer.equip(vehicle, options) -> vehicle   makes a StubScene.cab vehicle steerable. Like the game it COPIES
    Drivable.setSteeringInput onto the vehicle instance, so wrapping only the class afterwards would not reach it.
    options:
      parent            the node the Steering Wheel hangs in (the cab's parent from StubScene.cab); default the world root
      position          { x, y, z } of the Steering Wheel's centre in that node (default 0, 1.2, 0.5: in front of
                        and below the head of StubScene.cab)
      tilt              radians the wheel's base leans about X (default -0.6: the axis points up and toward the driver).
                        The tilt is on a node ABOVE the wheel: the game sets the wheel node's own rotation to (0, angle, 0)
      rotationDeg       the in-cab rotation at full lock, in degrees (default 540: vanilla); outdoorRotation is half
      maxRotTime        (default 1) and minRotTime (default -maxRotTime): the vehicle's steering times
      steeringDuration  seconds of the vehicle's wheelSteeringDuration (default 1)
      rotatedTime       where the steering starts (default 0, straight ahead)
      noWheel           true: the vehicle has no Steering Wheel node
      powered           false: the motor is off, the vehicle does not steer
      controlAllowed    false: getIsPlayerVehicleControlAllowed says no (an attached tool blocks the steering)
      turnedSeat        true: the driver's seat is turned to drive backwards. Like ReverseDriving it flips BOTH the direction
                        the game turns the Steering Wheel node in (-1) and getSteeringDirection() (-1)
      turnedSeatWheel   with turnedSeat: the vehicle has a Steering Wheel of its own for the turned seat (another node,
                        spec_reverseDriving.steeringWheel), which the game turns instead of the drivable's
      helper            true: a hired helper is driving (getIsAIActive says yes), as in StubSteer.setHelper
      copyOfOriginal    true: the vehicle's copy of setSteeringInput is the unwrapped StubSteer.original, as if it was
                        copied before the Companion Mod wrapped the class
  StubSteer.tick(dtMs)  one frame of the game's vehicle update for every equipped vehicle: the steering input of the
    last frame moves the steering value toward it (analog input at 2.5 steering durations per full range, keyboard
    input at 1), the input is cleared (so it must be sent again every frame), the wheels' rotated time follows, and the
    Steering Wheel node is set to (0, angle, 0). With no input the value goes back to centre at the rate of the last
    non-zero input, which stays "analog" after a Wheel Peripheral or Wheel Grab (the game's analog steering back).
  StubSteer.calls                every setSteeringInput that reached a vehicle, in order
  StubSteer.setHelper(vehicle, driving)  a hired helper starts (true) or stops driving the vehicle
  StubSteer.changeCamera(vehicle, cam)  the vehicle's active camera becomes cam (e.g. { isInside = false }, an Outside
    Camera, or { isInside = true, isPassengerCamera = true })
  StubSteer.wheelAngle(vehicle)  the node's angle in radians, unwrapped (rotatedTime * in-cab rotation), 0 without a wheel
  StubSteer.wheelNode(vehicle)   the Steering Wheel node the game turns now; StubSteer.wheelBase(vehicle) the node above it
  StubSteer.roadWheelAngle(vehicle)  rotatedTime: how far the vehicle's wheels on the ground are turned (positive and
    negative are the two ways the vehicle steers)
  StubSteer.rimPoint(vehicle, radius, angle, lift) -> x, y, z   the world position of the point on the wheel at
    `radius` from its axis and `lift` along it (default 0) when the wheel is turned to `angle` radians
  ```

- **`StubSteer.equip`, `vehicle.setSteeringInput`**: Vehicle:load copies the class functions onto the instance

### `tests/test_companion_mod_stubs.cpp`

- **the file**: The engine stubs are .luau files under tests/stubs/, read from disk by name when a test asks for one (spec #280, ticket #287), so a contributor edits a stub as Lua and reruns the tests without a rebuild.
- **`LoadAMissingStub`**: Asks for a stub that is not there. Static and self-contained so that EXPECT_NONFATAL_FAILURE can run it.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 82 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
