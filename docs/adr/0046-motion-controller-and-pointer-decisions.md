# Decisions behind Motion Controllers, the Pointer Ray and Interactive Control

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0025](0025-motion-controller-input-reaches-lua-as-raw-per-hand-state.md), [ADR-0026](0026-interactive-control-support-by-wrapping-its-click-points.md).

## Context and Decision
This record holds the non-obvious decisions behind the Motion Controllers as the Companion Mod sees them, the Pointer Hand and the Pointer Ray, the pointer in menus, and the Click Points of Interactive Control. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionInteractiveControl.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Interactive Control support (ADR-0026)

  Lets the Pointer Ray select and click the Click Points of the Interactive Control mod
  (FS25_interactiveControl). It does that by wrapping the Click Point class's own functions, because
  Interactive Control has no hook for another pointer. The wrap is installed only when the mod is
  loaded and its Click Point class and manager have every function this module relies on; otherwise
  the Companion Mod logs ONE warning and leaves Interactive Control, and every other feature, alone.

  What it relies on (release 1.2.0.1; pinned by tests/test_pointer_ray.cpp against a stand-in):
    FS25_interactiveControl.InteractiveClickPoint   update, updateClickable, updateScreenPosition,
        setClickable, isClickable, isExecutable, setActivated        (instances also have isActivated,
        node, screenPosX / screenPosY, interactiveController)
    g_currentMission.interactiveControl             getActiveInteractiveController, onActionEventExecute
    the active controller                            isAnalog

  While the Pointer Ray controls selection, Interactive Control's mouse test is replaced: exactly the
  Click Point nearest the ray's centre within `vrPointerCone` degrees (angle at the hand, not
  Interactive Control's screen-space test, which would jump between eyes under Alternate Frame
  Rendering) is made clickable. Everything else of Interactive Control keeps running: its icons, hover
  blink, action text and its own execute path, which one trigger pull runs once (and every frame while
  the trigger is held on an analog Click Point).

  Interactive Control updates the Click Points in a cab every frame, from the vehicle's draw, but those on a vehicle's
  exterior only on the vehicle's update tick, every few frames; in between it does not look at them, and its execute
  path runs the controller it took as clickable on that last update. So a Click Point stays active until Interactive
  Control has not updated it for STALE_MS (counting updates instead made one outside the cab drop out and come back on
  every tick), and a pull that comes before Interactive Control has taken the selected Click Point waits for it.

  The Pointer Ray controls selection only while it has control of the pointer (whichever of the desk
  mouse and the Pointer Hand moved last; VRCompanion:isPointerRayInControl). While the mouse has
  control Interactive Control selects by its own mouse test exactly as without Motion Controllers, the
  Pointer Ray is not drawn and the trigger clicks nothing; a trigger pulled in that time does not click
  (or keep an analog Click Point acting) once the hand moves, only a new pull does.

  A Click Point shares the cone with a Coupling Point of Manual Attach (VRCompanionManualAttach.lua, ADR-0035): the one nearer the ray's centre is the
  target. peekInteractiveControlTarget() reports the best Click Point's angle (as a cosine) before it is committed, and
  updateInteractiveControl(dt, selectNone) selects no Click Point for that update when the Coupling Point is nearer.

  Other modules build on:
    VRCompanion:onClickPointEvent(kind, clickPoint)   the Motion Controllers' hook, called with "selected" (the
        selected Click Point changed, clickPoint may be nil) and "clicked" (a trigger pull was executed:
        once per pull, not on every frame an analog Click Point keeps acting)
    rayControlsSelection()                            the one place that decides whether the Pointer
        Ray, rather than the mouse, selects Click Points
  ```

- **`STALE_MS`**: A Click Point that Interactive Control has not updated for this long (ms, summed over the Companion Mod's updates) is gone (its vehicle left, or it was deleted) and no longer counts as active. It is longer than the vehicle's update tick, on which Interactive Control updates the Click Points outside the cab. A pull waits at most as long for Interactive Control to take the Click Point it was aimed at.
- **`giveUpOnce`**: Giving up goes through the give-up part of the core's wrap helper (VRCompanionGameWraps.lua): ONE warning. Interactive Control installs its wraps itself (installWrappers) and only once everything was checked, so there is nothing to lift; map unload forgets it gave up.
- **`rayControlsSelection`**: Who selects Click Points: the Pointer Ray while it has control of the pointer (the Pointer Hand moved after the desk mouse did, see VRCompanion:isPointerRayInControl) and could be placed in the world; otherwise Interactive Control's own mouse selection, untouched.
- **`installWrappers`, `wrappers.updateClickable`**: The mouse test. While the Pointer Ray controls selection the selected Click Point is shown to the original as if the mouse were on it (so its hover blink and icon logic run) and every other Click Point as if the mouse were nowhere; the ray's choice is then final.
- **`wrappers.updateClickable`, `if mx == nil or my == nil then`**: Interactive Control asks for a test with no mouse position when it will not let this Click Point be pointed at (a static outdoor one facing away from the camera): it is then not clickable, whatever the ray says, and selectFromRay leaves it out. Everything else that decides whether a Click Point can be executed (its folding / animation / forced-state conditions and the indoor or outdoor mode) is isActivated, which selection already requires.
- **`tryInstall`, `if not state.waitingLogged then`**: Interactive Control has not created its manager yet; say so once so a stuck wait shows in the log.
- **`CAB_UPDATE_FUNCTIONS`**: Cab Click Points while the VR HUD is hidden Interactive Control updates a cab's Click Points from the vehicle's draw, and the game draws the vehicle being driven as part of the HUD. With the VR HUD hidden (its default, VRCompanionHud.lua) that draw never runs, so no indoor Click Point became active and the ray had nothing to hit in a cab; outdoor ones are updated on the vehicle's tick and kept working. While the HUD is hidden the Companion Mod runs Interactive Control's indoor update itself, once per update, as Interactive Control does from its tick in the game's no-HUD mode: the call its onDraw makes, under the same conditions (its state on and its indoor check), without the hand-tool crosshair the draw would also render. Not while a menu takes the input.
- **`VEHICLE_CHECK_MS`**: What Interactive Control reports for the vehicle being driven Interactive Control makes a cab's (indoor) Click Points active only while its state is on, its indoor check passes (an inside camera, or indoor sound), the vehicle is active for input (entered, no AI helper driving it), and the vehicle's draw runs, which updates them every frame. When the ray finds nothing in a cab, this says which of those failed: it is logged when it changes (looked at once a second while driving, at most VEHICLE_LOG_LIMIT lines a map) and shown by vrPointerDiagnostics.
- **`describeVehicle`, `if type(action) == "table" and action.updateClickable ~= nil and INDOOR_TYPES...`**: Click Points are the actions with a mouse test; buttons have none.
- **`clickSelected`**: One trigger pull runs Interactive Control's own execute path once; an analog Click Point keeps acting every update while the trigger is held. Only the pull itself is a click event (the haptic pulse).
- **`clickSelected`, `if mayUseTrigger == false then pressed, held = false, false end`**: A trigger already held for a Virtual Gamepad binding when the ray landed on the Click Point stays that binding's until it is let go (VRCompanionVirtualGamepad.lua): it clicks nothing.
- **`clickSelected`, `if pressed then`**: Only a pull that went down while the ray had control can keep an analog Click Point acting; one that began under the mouse never turns into a click when the hand moves later.
- **`clickSelected`, `if pressed and selected ~= nil then`**: Interactive Control executes the controller it took as clickable on its last update of the Click Point (outside the cab, the vehicle's last tick). A pull it has not taken yet waits for that, even once the trigger is let go, but only while the ray stays on the Click Point it was aimed at, and at most STALE_MS.
- **`stateSettingIsToggle`**: Interactive Control switches on while the Pointer Hand is in use in a cab Picking up the Pointer Hand in a vehicle switches Interactive Control on for that vehicle, so the player need not press its toggle key first; putting the Pointer Hand down, or leaving the vehicle, puts that vehicle's state back as it was. Interactive Control's synced state is written ONLY at those transitions (and once more when the vehicle's camera changes, because Interactive Control switches itself off then), never per frame, so multiplayer sees no extra traffic. When Interactive Control's own setting says always on or always off, that setting wins and nothing is written.

  Also relied on (release 1.2.0.1): g_currentMission.interactiveControl.settings:getSetting("IC_STATE") with FS25_interactiveControl.InteractiveControlManager.SETTING_STATE_TOGGLE; on the vehicle spec_interactiveControl (state, interactiveControllers) and activateInteractiveControl(state, noEventSend). When they are not there the Pointer Hand simply does not switch Interactive Control.

- **`updateAutoOn`**: Activates only during live in-cab gameplay (`not self:doesMenuTakeInput()` and `getVehicleActiveCamera(vehicle).isInside == true`, #247). A menu opening or switching to an Outside Camera ends auto-on and restores the vehicle's previous state until live in-cab gameplay resumes.
- **`updateAutoOn`, `holding`**: A hand holding the Steering Wheel is not the Pointer Hand, but grabbing it does not touch Interactive Control's state either: what was switched on stays on while a hand holds (Wheel Grab never starts it).
- **`updateAutoOn`, `camera`**: Interactive Control switched itself off when the camera changed: switch it on again, once.
- **`VRCompanion:peekInteractiveControlTarget`**: The Click Point the ray would select now, before updateInteractiveControl commits: the cosine of its angle from the ray's centre (larger is nearer the centre), or nil when none is within the cone. The Pointer Ray code ranks it against a Coupling Point (VRCompanionManualAttach.lua) and then tells updateInteractiveControl whether to select none.
- **`VRCompanion:updateInteractiveControl`, `ray`**: A Stereo Dialog takes the input and is answered with the pad's buttons: the Pointer Ray hits no Click Point behind it and the trigger clicks nothing, so a press for the dialog is not also a click.
- **`VRCompanion:updateInteractiveControl`, `mayUseTrigger`**: While the ray is on a Click Point the Pointer Hand's trigger is Interactive Control's: it reads released on the Virtual Gamepad, unless it was already held when the ray landed.
- **`VRCompanion:updateInteractiveControl`, `return ray ~= nil and live > 0, hx, hy, hz`**: With the mouse in control there is nothing for the ray to hit: it is not drawn.
- **`registerSubsystem "interactiveControl"`**: Map unload only (the support installs itself from the update, not at map load, and has no commands). Order: it comes after Wheel Grab and the Virtual Gamepad in modDesc.xml, so it unloads before their resets, which it does not depend on.

### `mods/FS25_VRCompanion/src/VRCompanionMenuPointer.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Menu Pointer

  Lets the player point at a game menu, or at the 2D Floating Window Mode panel, with the Pointer
  Hand: aiming moves the game's cursor, a trigger pull is a left click (ADR-0025). It works without
  Interactive Control and does nothing unless the Pointer Ray has control of the pointer (see "Who
  controls the pointer" in VRCompanionMotionControllers.lua).

  How it works, per update (called from updateMotionControllers, once the hands were polled):
    * The Injector owns the panel pose (ADR-0006, ADR-0008), so it reports where the Pointer Hand's
      ray lands on the visible panel: VRBridge.getPanelHitAlongRay(hand, ox, oy, oz, dx, dy, dz) ->
      {hit, u, v, distance}, with u across from the left and v down from the top, both 0..1. The ray is the
      Pointer Ray (it starts at the index fingertip and runs along the finger, VRCompanion:
      getPointerRayInTrackingSpace), the same as in the world. (VRBridge.getPanelHit(hand) tests the aim pose
      and is the fallback for an Injector without the other call.)
    * The game's cursor is moved and clicked through the game's own GUI mouse handling,
      g_gui:mouseEvent(posX, posY, isDown, isUp, button), exactly the call the engine makes for the
      desk mouse (the Injector never synthesizes OS input). The GUI measures y up from the BOTTOM, so
      the cursor is at (u, 1 - v).
    * The drawn cursor (ADR-0009) is placed with VRBridge.setMenuCursor(true, u, v) while the ray has
      control, and handed back to the desk mouse with VRBridge.setMenuCursor(false).
    * The trigger is the left mouse button: down on the pull (only if the ray was on the panel), up on
      the release, wherever the hand points then. Holding it while moving the hand drags. The trigger is
      VRCompanion:getPointerTrigger (one set of thresholds with the Pointer Ray), so a wobbling trigger
      is one click, and the pull that switched the Pointer Hand is not a click.

  Also in menus (not in 2D gameplay, where no GUI is open to receive them), on EITHER hand and only
  while a menu is showing and Motion Controllers are on:
    * Back / close: the grip (counts at 0.6, lets go below 0.4) or the lower face button, once per
      press, delivered as the game's own MENU_BACK menu input (g_gui:onMenuInput). A button already
      held when the menu opens, or a hand appears, is not a press.
    * Scroll: thumbstick up/down (engages at 0.7, more up/down than sideways) is the mouse wheel at
      the cursor: one notch on the push, then one per MENU_SCROLL_REPEAT_MS while held. The game's list
      and slider elements read the wheel by polling Input.isMouseButtonPressed while handling a mouse
      event, so the module sends g_gui:mouseEvent(x, y, true, false, wheel button) with that poll
      answering "pressed" for that one call (see sendWheel; the original is restored after).
    * Snap Turn is off in menus (VRCompanion:snapTurn refuses). The Virtual Gamepad reads neutral while a menu
      shows (VRCompanionVirtualGamepad.lua), so a game action bound on one of these inputs yields to them.

  PUBLIC SURFACE
    VRCompanion.MENU_BACK_PRESS / MENU_BACK_RELEASE                      grip thresholds (0..1)
    VRCompanion.MENU_SCROLL_ENGAGE / MENU_SCROLL_RELEASE / MENU_SCROLL_REPEAT_MS
    VRCompanion:updateMenuPointer(dt)   called by updateMotionControllers; idempotent per frame
    VRCompanion:resetMenuPointer()      map unload: lets go of the button and the drawn cursor
  ```

- **`reportFailureOnce`**: A failure inside menu code must never break the update, and is reported once rather than every frame. Takes the results of a pcall.
- **`letGo`**: Lets go of anything the ray was holding: a pressed button (released where it last was) and the drawn cursor (given back to the desk mouse).
- **`hands`**: [hand] = { back = bool } for every tracked hand; nil for a hand that is not tracked. Edges are tracked all the time, also with no menu showing, so a button already held when a menu opens (or a hand appears, or Motion Controllers come back on) is not a press.
- **`sendBack`**: The game's own menu input for "back": the call the engine's input binding makes for MENU_BACK, so screens, dialogs and the in-game menu react exactly as to the keyboard.
- **`sendWheel`**: Thumbstick up/down is the mouse wheel. The game's list and slider elements do not read the wheel from the mouse event: they poll the native mouse state, Input.isMouseButtonPressed(wheel button), while they handle a mouse-down event under the cursor. A mod cannot move the real wheel, so for the duration of ONE synchronous mouse event the poll answers "yes" for the wheel button being sent, then the original function is put back (also when the menu code raises an error). Everything else is the game's own handling: the event goes through g_gui:mouseEvent to whatever is under the cursor.
- **`scrollPosition`**: Where the scroll lands: the cursor the ray moved last while it controls the pointer, otherwise the desk mouse's position when the game can tell it, otherwise where the ray last pointed.
- **`updateMenuButtons`**: Grip or the lower face button, on either hand, goes back once per press; the thumbstick up/down scrolls. Both only while a menu shows and Motion Controllers are on, and neither while the pause menu's Settings page (`isSettingsPageShowing()`, ADR-0048) is showing so rebinding controls never fires `MENU_BACK` or scrolls (#247).
- **`updateMenuButtons`, `if not self:doesMenuTakeInput() or self:isStereoDialogShowing() or self:isSettingsPageShowing() then`**: A Stereo Dialog is answered with the gamepad's own menu buttons, and the Settings page needs grip/buttons/stick for rebinding; edge tracking (`hands`) still runs so inputs held when entering or leaving Settings do not misfire on transition.
- **`updateMenuButtons`, `now`**: A fresh push scrolls at once; a held stick repeats when `now` (`getCurrentTimeMs()`) is non-nil (nil clock still permits the initial push without arithmetic failure). A second hand joining a push that is already scrolling is not a fresh push.
- **`VRCompanion:updateMenuPointer`, `getPanelHitAlongRay`**: The ray is the one drawn in the world: it starts at the index fingertip and runs along the finger (or the aim pose while the grip pose is not tracked), in the space the hand poses are in, which is the space the Injector's panel is in. An Injector without getPanelHitAlongRay tests the aim pose. When the ray aims off-panel (`onPanel == false`), `setMenuCursor(false)` hides the drawn cursor instead of leaving it frozen on the panel border (#247).
- **`VRCompanion:updateMenuPointer`, `local pulled, held = self:getPointerTrigger()`**: The click follows the Pointer Hand's trigger, gated on `self:doesMenuTakeInput()` so 2D Floating Window Mode gameplay without a menu open never double-fires a GUI click alongside Virtual Gamepad `RT` (#247). A pull that switched the Pointer Hand to this hand is neither pressed nor held (see getPointerTrigger), and a trigger already held when the ray took over was never pressed, so neither clicks.
- **`registerSubsystem "menuPointer"`**: Registered with the core (the registry is documented in VRCompanionCore.lua). No update hook: updateMenuPointer runs from the Motion Controller poll, which fixes where in the update it comes (see updateMotionControllers). Unload runs in reverse file order, so it comes after the Virtual Hands, Wheel Grab and Virtual Gamepad have reset; none of them reads what this lets go of.

### `mods/FS25_VRCompanion/src/VRCompanionMotionControllers.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Motion Controller Subsystem

  Makes the player's Motion Controllers visible to the rest of the mod: it polls each hand's state
  from the Injector once per update, decides which hands are in use, picks the Pointer Hand, and owns
  the `vrMotionControllers` setting and the status console command. Motion Controller input is optional;
  everything here is inert when VRBridge.getHandState is missing or the runtime offers no Motion Controllers.

  PUBLIC SURFACE (other modules build on it; see ADR-0025)

    Hand ids          VRCompanion.MOTION_HAND_LEFT = 0, VRCompanion.MOTION_HAND_RIGHT = 1.
                      The same ids VRBridge.getHandState and VRBridge.pulseHaptic use. Anywhere a
                      hand is a parameter it is one of these two numbers.

    VRBridge.getHandState(hand [, out]) -> table   (the bridge call, filled in place like getHeadPose)
      tracked            boolean  aim pose valid and the Motion Controller active (false when the
                                  runtime has no Motion Controllers, refused the action set, or it is off)
      x, y, z            metres   aim pose position, tracking space, SAME convention, reference space
      pitch, yaw, roll   radians  and predicted display time as VRBridge.getHeadPose (follows the
      qx, qy, qz, qw              Recenter Action). Compose it into the world exactly like the head.
      isValid, isPositionTracked, isOrientationTracked   booleans, as in getHeadPose
      gripPoseTracked    boolean  the grip pose (where a Virtual Hand is drawn) is located; its own flag, independent
                                  of the aim pose's: false when the hand is untracked, when the runtime refuses the grip pose
                                  or when it can not locate it
      gripPoseX/Y/Z, gripPosePitch/Yaw/Roll, gripPoseQx/Qy/Qz/Qw   the grip pose, same conventions, reference
                                  space and predicted display time as the aim pose above (identity when untracked)
      trigger, grip      0..1     analog value (Khronos simple controller profile: select/click as trigger)
      thumbX, thumbY     -1..1    thumbstick (Vive: trackpad); +X right, +Y up
      thumbClick         boolean  thumbstick (Vive: trackpad) click
      buttonLower        boolean  lower face button: Touch X (left) / A (right), Index A
      buttonUpper        boolean  upper face button: Touch Y (left) / B (right), Index B
      menu               boolean  menu button (Touch: left hand only; Index has none)
      An untracked hand reports tracked = false, isValid = false, an identity aim pose and zeroed inputs.

    Per-frame cached state (updated once per VRCompanion:update by updateMotionControllers(dt)):
      VRCompanion:getHandState(hand) -> table   the bridge fields above, the same table every frame
                                                (filled in place; read it, never keep or edit it).
                                                Untracked defaults when Motion Controllers are off.
      VRCompanion:isHandInUse(hand) -> boolean  tracked and moved within MOTION_CONTROLLER_IN_USE_MS
      VRCompanion:getPointerHand() -> 0 | 1 | nil
      VRCompanion.pointerHand                    mirror of getPointerHand() (read-only for others)
        Pointer Hand rule of this module: exactly one hand in use -> that hand; none in use -> nil;
        both in use -> the previous Pointer Hand (falling back to the configured Tool Hand when
        previous was nil, e.g. waking both hands together or releasing a two-handed Wheel Grab),
        until a trigger pull on the other hand moves it there. That switching pull is consumed:
        getPointerTrigger() reports it as neither pressed nor held until the trigger is let go,
        so it never clicks in a menu either.

    VRCompanion.motionControllersEnabled  boolean, default true, saved (`vrMotionControllers on|off`).
      When false the mod ignores Motion Controller input: hands report untracked, none is in use, there is
      no Pointer Hand. The Injector session and its action set are never touched by this setting.

    Who controls the pointer (read by menus and by Interactive Control selection)
      The pointer is the cursor / selection the player aims: either the desk mouse or the Pointer Ray.
      Whichever moved last controls it. Starts with the mouse; the ray takes control when the Pointer
      Hand moves (the same "moved" that puts a hand in use), the mouse takes it back when it moves.
      VRCompanion:getPointerOwner() -> "mouse" | "ray"   (VRCompanion.POINTER_CONTROL_MOUSE / _RAY)
        "ray" only while the ray is the last mover AND a Pointer Hand is in use (so Motion Controllers
        off, an untracked hand and the 3 s idle timeout all read as "mouse").
      VRCompanion:isPointerRayInControl() -> boolean   (getPointerOwner() == "ray")
      VRCompanion:notePointerRayMoved() / VRCompanion:notePointerMouseMoved()   hand the control over;
        called by this module (hand moves; mouseEvent listener; sampling g_inputBinding:getMousePosition)
        and callable from tests or other modules that see either one move.

    The Pointer Ray (see "The Pointer Ray" below, and VRCompanionInteractiveControl.lua)
      VRCompanion:getPointerRay() -> table | nil   world origin / direction of the ray this update: it starts at the
                                                   Pointer Hand's index fingertip and runs along the finger
      VRCompanion:getPointerRayInTrackingSpace(hand) -> ox, oy, oz, dx, dy, dz, source | nil   the same ray before
                                                   the head carries it into the world (for the menu / 2D panel hit)
      VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION  true (default): along the finger; false: keeps the aim direction
      VRCompanion:getPointerTrigger() -> pressed, held   the Pointer Hand's trigger (hysteresis 0.6 / 0.4)
      VRCompanion.pointerConeDegrees               saved by `vrPointerCone <degrees>` (default 5)
      VRCompanion:placePointerRayForRender()      the line and dot, drawn before every rendered frame from the update's
                                                   end points relative to the head (called by placeAndBindEyes)

    Haptics (VRBridge.pulseHaptic(hand, durationMs, strength) -> boolean)
      VRCompanion:pulseHand(hand, durationMs, strength) -> boolean   one hand; false when off or the runtime refuses
      VRCompanion:pulsePointerHand(durationMs, strength) -> boolean   Pointer Hand only; false when off
      VRCompanion:onClickPointEvent(kind, clickPoint)   "selected" (non-nil point) and "clicked" pulse

    Time: every time in this module is VRCompanion:getCurrentTimeMs(), i.e. the global g_time in
    milliseconds (a wall clock only when g_time is nil). Tests drive it by assigning _G.g_time.

    Hooks into the rest of the mod:
      Core.lua    registers this subsystem (see the end of this file): the poll is its update hook (updateMotionControllers(dt)),
                  the four commands below are its console commands, and it resets at map unload (resetMotionControllers())
      Config.lua  saveConfiguration / loadConfiguration run its saveSettings / loadSettings hooks
                  (saveMotionControllerSettings / loadMotionControllerSettings)
      Locomotion  onRecenterAction -> resetMotionControllerMovement() (a recenter moves every pose)
      MenuPointer updateMotionControllers -> updateMenuPointer(dt) at the end of every update; it registers its own unload (resetMenuPointer())
                  (VRCompanionMenuPointer.lua)
      VirtualHands updateMotionControllers -> updateVirtualHands(dt) at the end of every update (also when input is
                  ignored, so the hands hide); it registers its own console commands and unload (VRCompanionVirtualHands.lua).
                  The Virtual Hands place the grip pose through VRCompanion:headRelativePoseToWorld, the transform the Pointer Ray uses too.
      WheelGrab   updateMotionControllers -> updateWheelGrab(dt) after the hands are polled and before the Pointer Hand is chosen
                  (also when input is ignored, or the Injector offers no hand states, so a held Steering Wheel is let go), and it registers its own
                  settings hooks (saveWheelGrabSetting / loadWheelGrabSetting), console commands, map load (the Drivable wrap) and unload (VRCompanionWheelGrab.lua). A hand
                  holding the Steering Wheel (isHandHoldingSteeringWheel) stays in use but is never a Pointer Hand
                  candidate, so it casts no Pointer Ray; Interactive Control's auto-on keeps its state while it holds.
      HandTools   updateMotionControllers -> updateHandTools(dt) on every path, after the hands are polled and before the Pointer Hand
                  is chosen (also when input is ignored, or the Injector offers no hand states, so a Hand Tool goes back to
                  where the game puts it); the settings hooks (saveHandToolsSetting / loadHandToolsSetting), the map-load and
                  unload hooks and the console commands are registered
                  with the core by the file itself, its unload before the Virtual Hands go (VRCompanionHandTools.lua, ADR-0029). The Tool Hand holding a
                  supported Hand Tool (isHandHoldingHandTool) is never a Pointer Hand candidate in the world, as a hand on the
                  Steering Wheel is not; in a menu either hand can. While its Hand Tool keeps it in use
                  (isHandKeptInUseByHandTool) the Tool Hand stays in use however still it is held, so a held-still cut or spray
                  does not drop the tool.
      ObjectGrab  updateMotionControllers -> updateObjectGrab(dt) on every path (the grip edges and the carry), updatePointerRay ->
                  peekObjectGrabRayTarget() and settleObjectGrabRayTarget() (VRCompanionObjectGrab.lua, ADR-0033; the object is looked
                  at first, settled once the nearer of it and an Animated Object is known). A hand carrying a Held Object
                  (isHandCarryingHeldObject) stays in use however still it is held and is never a Pointer Hand candidate.
      AnimatedObjects (wraps the game's trigger callback to follow the zones, at map load, from its own registered hook),
                  updatePointerRay -> updateAnimatedObjects(dt, suppressed, rivalDistance) after
                  Interactive Control, between Object Grab's peek and settle (the nearer wins; also, as suppressed, on the paths
                  where input is ignored, so the selection clears), getPointerDiagnostics -> getAnimatedObjectsDiagnostics()
                  (VRCompanionAnimatedObjects.lua, ADR-0034). While the
                  ray is on one, the Pointer Hand's trigger is taken off the Virtual Gamepad and a pull runs the game's own toggle.
      ManualAttach updatePointerRay -> peekInteractiveControlTarget() (the angle of the best Click Point), updateManualAttach(dt, suppressed,
                  clickCos) (the Coupling Point nearer the ray's centre than that Click Point is the target), then updateInteractiveControl(dt,
                  selectNone) (told to select none when the Coupling Point won), all before Animated Objects and Object Grab (either one selected
                  suppresses both; also, as suppressed, on the paths where input is ignored, so the selection clears), getPointerDiagnostics -> getManualAttachDiagnostics()
                  (VRCompanionManualAttach.lua, ADR-0035; its unload hook is registered by the file). The one thing of Manual Attach it wraps is the handler class's candidate update.
      VirtualGamepad updateMotionControllers -> updateVirtualGamepad(dt) first of all (also when input is ignored), the status
                  command ends with the pad's state and it registers its own settings hooks, console command and unload (VRCompanionVirtualGamepad.lua, ADR-0028)
      The buttons, triggers and thumbsticks do nothing here beyond the uses above (pointing, Click Points, menus, Wheel Grab):
                  Snap Turn, the Recenter Action and the HUD Toggle Action from the Motion Controllers arrive only as game actions
                  bound on the Virtual Gamepad (ADR-0028). No button is mapped to an action here, and no setting does it.
  ```

- **`VRCompanion.DEFAULT_MOTION_CONTROLLERS_ENABLED`**: vrMotionControllers on|off: saved, default on. Off makes the mod ignore Motion Controller input; it never touches the Injector's session or action set.
- **`VRCompanion.MOTION_CONTROLLER_MOVE_DISTANCE`**: "Moved" means the aim pose left the position (metres) or orientation (radians) it was last at by more than this. Measured from the last pose that counted, so a slow drift adds up to a move while a Motion Controller lying on a desk (sensor noise well below a millimetre) never does.
- **`VRCompanion.POINTER_TRIGGER_PRESS`**: The trigger counts as pulled above PRESS and as released below RELEASE (hysteresis against a trigger that rests half-way).
- **`newState`, `triggerConsumed`**: [hand] = a Pointer Hand switch took this pull: it counts for nothing until let go
- **`movedFromReference`**: Whether the aim pose moved away from the reference pose far enough to count; the pose becomes the new reference when it did.
- **`isHoldingSteeringWheel`**: The Pointer Hand: the one hand in use. With both in use it is the previous one (or the configured Tool Hand when there was no previous Pointer Hand, such as when both hands wake up together or are released from a two-handed Wheel Grab), until the trigger is pulled on the other hand: that pull moves the Pointer Hand there and is consumed (it clicks nothing, see getPointerTrigger). A hand holding the Steering Wheel is in use but never a candidate (Wheel Grab, see VRCompanionWheelGrab.lua): the Pointer Hand that starts holding falls to the other hand in use, or to nobody, and can point again once it lets go (falling back to the configured Tool Hand when both hands become free together).
- **`isHoldingHandTool`**: The Tool Hand holding a supported Hand Tool is never a candidate either, as it uses the tool with its trigger (Hand Tools, ADR-0029). That holds in the world only: in a menu the tool is back where the game puts it and either hand can point.
- **`isCarryingHeldObject`**: The hand carrying a Held Object (Object Grab, ADR-0033) is never a candidate in the world: no ray sticks out of the object it holds. In a menu (`isMenuQuadShown()`), matching `isHoldingHandTool`, the carrying hand can act as the Pointer Hand while the Held Object stays preserved until unpaused.
- **`choosePointerHand`, `if hand ~= previous and self:isObjectGrabSqueeze(hand) == true then`**: So does squeezing the grip of a hand that points at something it can grab (Object Grab): it grabs in that squeeze.
- **`clearHands`, `state.ray.visible`**: the update that draws it does not run now: a ray left drawn would stay on its last target
- **`POINTER_OWNER_LOG_LIMIT`**: Who controls the pointer One place for "desk mouse or Pointer Ray: whichever moved last" (see the header for the API).
- **`noteDeskMouse`**: The desk mouse was seen at (x, y), either reported by the engine (isEvent: any event is the player using the mouse) or sampled (a first sample only learns where it is). It took control when it moved.
- **`VRCompanion:mouseEvent`**: The engine tells mod listeners about the desk mouse. The mod's own synthetic GUI events never come through here, so they never look like the mouse moving.
- **`sampleDeskMouse`**: Sampling covers an engine that does not deliver mouseEvent to the mod in this screen.
- **`settleInteractiveControl`**: Interactive Control still has to see a Pointer Hand go away (its state is put back).
- **`updatePointerUsers`**: What reads the pointer after the hands were polled: the menus, the Virtual Hands, then the Virtual Gamepad's uses. The Virtual Hands read the settled hands too (VRCompanionVirtualHands.lua), so they follow every exit of the update, including the ones where Motion Controller input is ignored.
- **`VRCompanion:updateMotionControllers`**: THE UPDATE ORDER of this subsystem and the ones it drives. Motion Controllers register ONE update hook (at the end of this file), and this function is its fan-out: direct calls, in this order, because modDesc.xml order (Motion Controllers, Menu Pointer, Virtual Hands, Wheel Grab, Virtual Gamepad, Hand Tools, Object Grab, ...) does not give it. The Virtual Gamepad updates FIRST, before the hands are read; then the desk mouse is sampled; then, on the normal path, the hands are polled, Wheel Grab and Object Grab run on them, Hand Tools run, the Pointer Hand is chosen, the Pointer Ray is cast (Interactive Control, Manual Attach, Animated Objects, Object Grab), and LAST come the Menu Pointer, the Virtual Hands and the Virtual Gamepad's uses (updatePointerUsers). The two paths where nothing is polled (input ignored, no hand states from the Injector) run Wheel Grab, Hand Tools, Object Grab in that order, then settle Interactive Control and friends, then the pointer users. A subsystem that needs to run between these steps cannot be a separate update hook of its own.
- **`VRCompanion:updateMotionControllers`, `clearHands()`**: The cached hands are stale from here on: read as untracked, so that a hand holding the Steering Wheel is no longer located and Wheel Grab lets go (and stops dropping the other devices' steering).
- **`VRCompanion:updateMotionControllers`, `result`**: The bridge fills a table in place once it holds every field, and otherwise hands back a complete one that is adopted here, so nothing is allocated per frame.
- **`VRCompanion:updateMotionControllers`, `motion.lastMovedMs`**: the clock went backwards: forget the old move
- **`VRCompanion:updateMotionControllers`, `for _, hand in ipairs(HANDS) do`**: A hand holding the Steering Wheel stays in use (it keeps its Virtual Hand), however still it is held. So does the Tool Hand while its Hand Tool keeps it in use (Hand Tools says when): a cut or a spray held with a still hand must not drop the tool. And the hand carrying a Held Object (Object Grab), which is however still it is held.
- **`VRCompanion:updateMotionControllers`, `if state.pointerHand ~= nil and state.movedThisUpdate[state.pointerHand] then`**: The Pointer Hand moving is the ray moving: it takes the pointer from the desk mouse. Pulling the off-hand trigger to switch (or pick) the Pointer Hand also calls `notePointerRayMoved()` immediately even if that hand was still, and reserves `"trigger"` via `takeVirtualGamepadInput(hand, "trigger")` while `state.triggerConsumed[hand]` is set so that switching pull is suppressed on the Virtual Gamepad until release (#247).
- **`VRCompanion:resetMotionControllerMovement`**: A Recenter Action moves the origin every pose is measured from. Hands lying still must not look moved, so the movement references are dropped and taken again at the next update.
- **`qrotate`**:

  ```text
  The Pointer Ray
  The ray starts at the index fingertip of the Pointer Hand in the world and runs along the finger (below: "The
  Pointer Ray on the index finger"); the aim pose is only the fallback. A hand pose is in the head pose's
  tracking space, so it is carried into the world the way the head is: the Cyclopean Audio Listener
  Node sits at the head's centre with the head's full world rotation (all three camera paths place it,
  through Snap Turn and the Recenter Action), so the hand's offset and direction relative to the head
  are rotated by the listener's world rotation and added to its world position. Anchoring at the head
  centre keeps the ray the same in both eyes.

    VRCompanion:getPointerRay() -> table | nil   the ray this update, or nil when there is none:
      ox, oy, oz (origin, world), dx, dy, dz (unit direction), visible (drawn), ex, ey, ez (end point)
      Read it, never keep or edit it.
    VRCompanion:getPointerTrigger() -> pressed, held   the Pointer Hand's trigger this update
    VRCompanion:placePointerRayForRender()       draws the line and dot for a rendered frame (see below)
  ```

- **`headIsPlacedInTheWorld`**: Whether the head is currently placed in the world by the mod's own camera chain; otherwise the listener node says nothing about where the player's head is. (The engine's own head-tracking setting, VRCompanion:isHeadTrackingActive, is not a gate: the mod places the head itself whatever it says, and in the game it reads off while the head is in fact placed.)
- **`qmultiply`**:

  ```text
  Head-relative poses in the world (shared by the Pointer Ray and the Virtual Hands)
  A hand pose from VRBridge is in the head pose's tracking space. Carrying it into the world is one
  transform, whatever the pose is for: take the pose relative to the head, then apply the Cyclopean
  Audio Listener Node's world pose (the head's centre with the head's full world rotation).

    VRCompanion:isHeadPlacedInTheWorld() -> boolean
    VRCompanion:headRelativePoseToWorld(x, y, z, qx, qy, qz, qw, out, outRelative) -> true | false, reason
      On true, out.x, out.y, out.z (world position) and out.qx, out.qy, out.qz, out.qw (unit world
      rotation) are filled in place; when outRelative is provided, the head-relative pose is also
      filled into it in place so callers do not need a world-to-listener round-trip. On false, out
      is untouched and reason says why in one line.
    VRCompanion.multiplyQuaternions(ax, ay, az, aw, bx, by, bz, bw) -> x, y, z, w   (a then b is applied as a * b)
  ```

- **`VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION`**:

  ```text
  The Pointer Ray on the index finger
  The ray starts at the index fingertip of the hand and runs along the finger: a fixed offset in the grip frame
  (VRCompanion.VIRTUAL_HAND_POINTER, VRCompanionVirtualHands.lua, the relaxed finger of the glove, so the ray does not
  twitch as the trigger curls the finger) applied to the GRIP pose. It does not depend on Virtual Hands being shown or
  their model loaded. Only when the grip pose is not tracked (a runtime that refuses it) does the ray start at the aim
  pose and run along its -Z axis.

  VRCompanion.POINTER_RAY_FOLLOWS_FINGER_DIRECTION  true (default): the ray runs along the finger. false: it keeps the
      aim pose's direction (-Z) and only starts at the fingertip.
    VRCompanion:getPointerRayInTrackingSpace(hand) -> ox, oy, oz, dx, dy, dz, source | nil   the same ray in the
      tracking space the hand poses are in (before the head carries it into the world), for the Injector's menu and 2D
      panel hit test; source is "index finger" or "aim pose"; nil while the hand is not tracked.
  ```

- **`fingerRay`**: The ray of a hand whose grip pose (gx..gqw) and aim rotation (aqx..aqw) are given in one frame (tracking space or the world): the fingertip as origin and the finger (or the aim -Z axis) as direction. Not normalized.
- **`placePointerRay`, `state.rayPose`**: The aim pose in the world: where the ray starts and runs when there is no grip pose, and the direction it keeps when it is not to follow the finger.
- **`noteRayInHead`**: Takes the drawn line's ends into the space of the head (the Cyclopean Audio Listener Node), for the frames rendered until the next update (VRCompanion:placePointerRayForRender).
- **`VRCompanion:updatePointerRay`, `if reason ~= state.rayReason then`**: Logged when it changes, so a session where pointing does not work says why in the game log.
- **`VRCompanion:updatePointerRay`, `if source == SOURCE_FINGER and not state.fingerLogged[hand] then`**: Once per hand, the first time the ray is placed from its index finger: how far the finger points from where the runtime aims the controller, so a headset session shows whether pointing along the finger feels sane.
- **`VRCompanion:updatePointerRay`, `clickCos`**: Click Points and Coupling Points (the hitch, its PTO stub, its hose couplers) share the cone (ADR-0035): of the Click Point and the Coupling Point within it, the one nearer the ray's centre is the target and the other selects nothing (a tie is the Click Point's). Interactive Control reports its best Click Point before it commits, Manual Attach support ranks its Coupling Point against it, and Interactive Control is then told to select none when the Coupling Point won.
- **`VRCompanion:updatePointerRay`, `if onCouplingPoint then hasTarget, hitX, hitY, hitZ = true, couplingX, coupli...`**: Either of them selected wins over everything else (Animated Objects and Object Grab): Interactive Control answers a hit point only when it selected one (it answers hasTarget, with no hit point, merely for "Click Points exist", which an Animated Object or Object Grab target may replace; Object Grab ray peeking runs whenever `hitX == nil`).
- **`VRCompanion:updatePointerRay`, `local onAnimatedObject, animatedX, animatedY, animatedZ = self:updateAnimated...`**: Animated Objects: the ray is on a door or gate in whose zone the player stands. Of the two the ray meets first, the nearer wins.
- **`VRCompanion:getPointerTrigger`**: The Pointer Hand's trigger: pressed (it went down this update), held (it is down). A pull that switched the Pointer Hand to this hand is neither, however long it is held on: it is let go first.
- **`VRCompanion:pulseHand`**: Vibrates one hand (never the other). Nothing happens, and false is returned, while Motion Controllers are off, without a hand, or when the Injector has no haptics call or the runtime refuses it.
- **`VRCompanion:placePointerRayForRender`**: A line from the hand with a dot where it lands, drawn with the engine's debug drawing (`RAY_COLORS` distinguishes `idle`, `clickPoint`, `couplingPoint`, `animatedObject`, and `objectGrab` while `state.ray.visible` remains gated on `hasTarget`, #247). Called by VRCompanion:placeAndBindEyes once it has placed the head for a frame, which happens before every frame the engine renders, after the Virtual Hands are placed for it. The ends are the update's, relative to the head, carried into the world from where the head is now: the game renders some frames with no update before them, after the cab moved again (Sequential Dual-Pass's second pass), and the update runs before the game moves the cab camera. A line drawn from the update's world ends (in VRCompanion:draw) was a copy lagging behind the cab in one eye, as the Virtual Hands were. The camera path may place the head more than once before a frame; each time draws the same line again.
- **`VRCompanion:resetMotionControllers`**: Map unload: forget everything seen this session. The saved setting stays as it is. The Menu Pointer, Virtual Hands, Wheel Grab and Virtual Gamepad reset in hooks of their own, and so do Hand Tools, Object Grab, Interactive Control, Animated Objects and Manual Attach (registered unload order is the reverse of modDesc.xml order, so Hand Tools reset before the Virtual Hands go: a tool hangs below a hand); the registry removes the console commands.
- **`registerSubsystem "motionControllers"`**:

  ```text
  Registered with the core (the registry is documented in VRCompanionCore.lua).
    update: the whole poll, ONE hook with the fan-out of updateMotionControllers (its order is stated there). The Menu Pointer, Virtual
      Hands, Wheel Grab and Virtual Gamepad do not register updates of their own: file order (Menu Pointer and Virtual Hands before Wheel
      Grab and Virtual Gamepad) is not the order they run in.
    mapUnload: runs last of the Motion Controller subsystems (it registered first, and unload runs in reverse), after the Virtual
      Gamepad, Wheel Grab, Virtual Hands and Menu Pointer and the subsystems of the fan-out have reset; nothing of theirs reads this state.
      The group's resets run in reverse file order (Manual Attach, Animated Objects, Interactive Control, Object Grab, Hand Tools,
      Virtual Gamepad, Wheel Grab, Virtual Hands, Menu Pointer, this state). Two orders matter, and file order gives both: Hand
      Tools reset before the Virtual Hands (a tool hangs below a hand), this state last. Everything else is independent, because
      each reset touches only its own state and:
        Menu Pointer      lets go of a held mouse button (g_gui) and of the drawn cursor (VRBridge.setMenuCursor)
        Hand Tools        ends a use in progress (the tool's own use handler, called with the trigger released), lifts the wraps of
                          HandTool and of Player.getLookRay (held through the core's count), gives the held tool back to the game
        Object Grab       lifts the wraps of its classes and its hold on Player.getLookRay
        Virtual Hands     gives a tool back, deletes the hands' nodes
        Wheel Grab        lifts the wrap of Drivable's steering input
        Virtual Gamepad   zeroes the Injector's pad (VRBridge.setVirtualGamepad, setVirtualGamepadUse)
        Interactive Control, Animated Objects, Manual Attach   put another mod's / the game's class function back
      None of them calls a function another one has wrapped (the tool's use handler is neither a trigger callback, nor a
      candidate update, nor Interactive Control's class; a look ray asked during it is answered first by Hand Tools, which are
      still in place at that point), none reads a field another one resets, and the three VRBridge calls are plain stores into
      independent Injector fields (src/luau/vr_bridge.cpp: Lua_SetMenuCursor, Lua_SetVirtualGamepad, Lua_SetVirtualGamepadUse)
      that its threads read later; neither side can see the order of two of them. The Perf Trace stop (Injector) and the Dual
      Pass Physics release (engine) of the other subsystems are likewise independent of the group.
  Settings: the Motion Controllers' on/off and the Pointer Cone. Wheel Grab, Hand Tools, Object Grab and the Virtual Gamepad save
  and load their own; none of the loads reads a setting another one loads, so their order does not matter.
  ```

### `tests/test_haptics.cpp`

- **`CompanionModTest.TheRayStayingOnAClickPointOutsideTheCabPulsesOnceThoughItUpdatesOnlyOnTheVehiclesTick`, `ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('doorHandle', -0.3, 1.5, 2.3, { ou...`**: Interactive Control updates a Click Point on a vehicle's exterior only on the vehicle's update tick, every few frames.

### `tests/test_interactive_control_auto_on.cpp`

- **`CompanionModTest.WithTheHudHiddenTheCabClickPointsAreUpdatedWithoutTheVehiclesDraw`**: The game draws the vehicle being driven as part of the HUD, and Interactive Control updates a cab's Click Points from that draw. With the VR HUD hidden the draw does not run, so the Companion Mod runs Interactive Control's indoor update itself and the cab's Click Points become active; with the HUD shown, or while a menu takes the input, it leaves them alone.

### `tests/test_menu_pointer.cpp`

- **`CompanionModTest.BridgeReportsWhereAGivenRayHitsThePanelWhateverTheHandsAimPoseSays`**: The Pointer Ray starts at the index fingertip and runs along the finger, not along the aim pose, and the panel is the Injector's geometry: the Companion Mod hands it the ray (tracking space, the space the hand poses are in).
- **`CompanionModTest.BridgeSetsTheDrawnCursorWhileThePointerRayHasControlAndClearsItOnTheMenuChanging`, `ASSERT_EQ(RunLuauChunk(L, "VRBridge.setMenuCursor(true, 0.5, 0.5)", "=setAgai...`**: A stale ray cursor must not survive a menu opening or closing.
- **`kGuiStubs`**: The game's GUI as the mod sees it: it records every mouse event the mod delivers (normalized screen position with the origin at the BOTTOM left, as the game's GUI uses).
- **`MenuPointerTest.TheCursorFollowsTheFingerTheRayRunsAlongNotTheAimPose`, `headset.SetHandPose(kRight, AimAt(0.25f, 0.5f));`**: The aim pose is the fallback while the grip pose is not tracked: the cursor goes where the aim pose points.

### `tests/test_menu_pointer_panel.cpp`

- **the file**: Menu pointing in the Injector (spec #71): where a Motion Controller's ray lands on the floating menu / 2D Floating Window Mode panel, in normalized panel coordinates, and the drawn cursor override the Pointer Ray uses. The panel geometry lives here (ADR-0006, ADR-0008); Lua only consumes the hit.

  Panel coordinates: u runs left to right and v top to bottom, both 0..1, the same convention as the drawn cursor (MenuCursorState::normX/normY). The game's GUI uses a bottom-left origin, so the Companion Mod flips v when it talks to the GUI.

### `tests/test_menu_scroll_back.cpp`

- **`kGuiStubs`**: The game's GUI as the mod sees it. `list` covers the middle of the screen (GUI coordinates, y up from the bottom) and, like the game's SmoothListElement, scrolls when it handles a mouse-down inside it while the native wheel state says the wheel moved; `scrolls` records each step (-1 = list moved up, +1 = down). `backs` records the menu back input.

### `tests/test_motion_controllers.cpp`

- **`CompanionModTest.ARecenterDoesNotMakeAControllerLyingStillLookMoved`, `ASSERT_EQ(RunLuauChunk(L, "VRCompanion:onRecenterAction()", "=recenter"), 0);`**: The Recenter Action moves the origin to the head: every pose, the desk Motion Controller's too, shifts.

### `tests/test_pointer_ray.cpp`

- **`CompanionModTest.ThePointerRayIsPlacedWhileTheEnginesHeadTrackingReadsOff`, `headset`**: In the game isHeadTrackingActive() reads false (the engine's own setting) while the mod places the head itself, so it must not stop the ray (seen in the headset: "the head is not placed in the world").
- **`CompanionModTest.AClickPointInteractiveControlWillNotTestIsNeverSelectedByTheRay`, `ASSERT_EQ(RunLuauChunk(L, R"luau(`**: Interactive Control tests a static outdoor Click Point that faces away from the camera with no mouse position, so it can never be clicked. It is nearest the ray's centre here.
- **`CompanionModTest.ThePointerRayStaysOnTheFingerAfterASnapTurnAndTheRecenterAction`, `ASSERT_EQ(RunLuauChunk(L, R"luau(`**: What must not change while the hand and head are held still is the ray's origin (the fingertip) and direction in the head's own frame (the Cyclopean Audio Listener Node is the head in the world).
- **`CompanionModTest.ClickPointsOnAVehicleExteriorAndOnAPlacedObjectCanBeClickedOnFoot`, `FRAMES(2 * OutdoorTickFrames(L));`**: Interactive Control looks at the door handle on the vehicle's update tick only: the first tick activates it, the next treats it as clickable.
- **`CompanionModTest.AClickPointInteractiveControlStoppedUpdatingIsNoLongerSelectedOrDrawnToward`, `ASSERT_EQ(RunLuauChunk(L, "StubIC.points = {}", "=vehicleGone"), 0);`**: The vehicle is gone: Interactive Control no longer updates its Click Points, and they stay marked active, but nobody can click them any more. Interactive Control updates every Click Point it has at least every half second (those outside the cab only on the vehicle's tick), so one it has not updated for longer is gone.
- **`CompanionModTest.AClickPointOutsideTheCabStaysSelectedBetweenTheVehiclesTicksAndEveryPullClicksIt`**: Click Points outside the cab

  Interactive Control updates the Click Points in a cab every frame, from the vehicle's draw, but those on a vehicle's exterior only on the vehicle's update tick, every few frames (StubIC.outdoorTickFrames). Between two ticks it does not look at them, and the controller a pull executes is the one it took as clickable on the last tick.

- **`CompanionModTest.APullInteractiveControlCouldNotTakeForHalfASecondIsNotClickedLater`, `ASSERT_EQ(RunLuauChunk(L, "StubIC.addPoint('doorHandle', -0.3, 1.5, 2.3, { ou...`**: Interactive Control keeps the door handle active but runs no mouse test on it while it projects off the screen, so it never takes it as clickable.
- **`namespace {`**: The Pointer Ray starts at the index fingertip of the Virtual Hand and runs along the finger

  The fingertip and the finger's direction are a fixed offset in the grip frame of each hand (the relaxed glove finger, VRCompanion.VIRTUAL_HAND_POINTER), applied to the GRIP pose. The numbers below are written out here on purpose, as the derivation scripts/derive_virtual_hand_pointer.py prints them for the left hand (metres, grip frame: -Y runs toward the fingertips, -Z from the little finger toward the thumb, +X out of a left palm); the right hand is the left with X negated. The tests move the grip pose and the aim pose to different places and turns, so a ray that started at either of them could not pass.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 93 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
