# Decisions behind the Virtual Gamepad

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0028](0028-motion-controller-buttons-reach-the-game-as-a-virtual-gamepad.md).

## Context and Decision
This record holds the non-obvious decisions behind the Virtual Gamepad on both sides of VRBridge: the XInput hooks, the held-input rule, rumble, the default bindings and the Recenter migration. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `mods/FS25_VRCompanion/src/VRCompanionVirtualGamepad.lua`

- **the file**:

  ```text
  FS25 VR Companion Mod: Virtual Gamepad Subsystem (ADR-0028)

  The Injector presents the Motion Controllers' triggers, grips, thumbsticks and buttons to the game as an Xbox
  pad it reads through XInput (the Virtual Gamepad, CONTEXT.md), so the player binds them in the game's own
  Controls screen. The pad itself lives in the Injector; this module owns what only the Companion Mod knows:
  the saved `vrVirtualGamepad` setting, the Motion Controller switch the pad follows, and the game-log and
  status-command words about it.

  PUBLIC SURFACE

    VRBridge.setVirtualGamepad(enabled, live)       tell the Injector what is wanted (sent when it changes)
      enabled  the saved `vrVirtualGamepad` setting
      live     Motion Controller input is on (`vrMotionControllers`); the pad reads neutral while it is not
    VRBridge.getVirtualGamepad() -> present, slot, reason, leftGripHeld, rightGripHeld   what the game sees: the XInput
                                                     slot (0..3) while present, otherwise why not; and whether each hand's grip is
                                                     held as the pad reads it (the Injector owns that threshold and its hysteresis)

    VRBridge.setVirtualGamepadUse(leftInputs, rightInputs)   which inputs of each hand the Companion Mod is using itself
                                                     (bit set, see INPUT_BITS below); sent every update

    VRCompanion.virtualGamepadEnabled  boolean, default true, saved
                                       (`vrVirtualGamepad on|off`).
    VRCompanion:getVirtualGamepadStatus() -> string  one phrase for the status command

    VRCompanion.virtualGamepadDefaultsWritten  boolean, default false, saved: the VR-comfort default bindings have been
                                       written to the Virtual Gamepad, once and for good (see THE ONE-TIME DEFAULT REWRITE).
    VRCompanion.virtualGamepadRecenterMigrated  boolean, default false, saved: the Recenter migration has run or was not needed
                                       (see THE RECENTER MIGRATION).

  THE MOD'S OWN USES (ticket #101, ADR-0028): while the Companion Mod is using a Motion Controller input itself, that input
  reads as released on the pad, so a VR use never also fires a game binding. A use asks for an input every update it wants
  it, and the answer is whether it may have it:
    VRCompanion:takeVirtualGamepadInput(hand, input) -> boolean   input is one of "trigger", "grip", "stick",
                                       "stickClick", "lower", "upper", "menu". False while the input is still held for its
                                       binding (held when the use would have begun): the use must not act on it.
  Uses made here, at the end of every update: every input of both hands while a menu shows (the Pointer Ray menus act on
  the raw hand state, as they always did), and the grip of a hand holding the Steering Wheel. Interactive Control's click
  asks for the Pointer Hand's trigger itself, as the ray lands on a Click Point, and Hand Tools ask for the trigger of the Tool
  Hand while it holds a supported Hand Tool (VRCompanionHandTools.lua, ADR-0029), which calls the tool's use handler with it.
  THE SETTINGS PAGE EXCEPTION: the player binds the pad in the Controls screen, which is part of the pause menu's Settings page,
  and the game's binding capture needs the pad's inputs to arrive (a pad that reads neutral cannot be bound). So while the
  pause menu shows its Settings page (g_gui.currentGuiName == "InGameMenu" and g_inGameMenu.currentPage.id == "pageSettings",
  both measured in the game) the menu does NOT take the pad's inputs; every other menu and page keeps taking them. The
  Controls pages are sub-pages of Settings and cannot be told apart from it, so the exception covers all of Settings.
  (g_inputBinding.isInputCapturing does not turn true during binding, so it cannot be used instead.) THE TRADE-OFF: on the
  Settings page the Pointer Ray menu still moves and clicks so the player can select Controls and rows, and a trigger click there also reaches the game as RT; however, Menu Pointer suppresses `MENU_BACK` (grip / lower face button) and thumbstick scroll shortcuts on `pageSettings` (VRCompanionMenuPointer.lua, #247) so binding grip, face buttons, or thumbsticks in Controls never exits or scrolls the page. If g_gui, g_inGameMenu or the page is missing, the menu counts as any other menu. The held-input rule holds on the way into and out of the Settings page: what the menu took stays off the pad until it is let go, and what was held on the Settings page keeps its binding when another page's menu use begins.
  THE STEREO DIALOG EXCEPTION (ADR-0032): a Stereo Dialog (VRCompanion.STEREO_DIALOGS, the fish catch dialog) is a menu that takes the
  input but is answered with the gamepad's own menu buttons (MENU_ACCEPT, MENU_BACK), so while one shows in the stereoscopic view the menu
  does NOT take the pad's inputs either: the Motion Controllers' bindings reach the game as outside a menu and the dialog's button hints
  answer it. Nothing takes the input, so there is no held-input rule to apply. In 2D Floating Window Mode it is any other menu.
  THE HELD-INPUT RULE: an input already held (as the pad reads it) when a use begins keeps its binding until it is let go,
  and the use does not act on it meanwhile. The same holds the other way round: an input a use took stays off the pad
  until it is let go, even if the use ends first (a grip that closed a menu does not fire its binding as the menu goes).

  THE ONE-TIME DEFAULT REWRITE (ticket #104, ADR-0028): the game's default gamepad bindings apply to every pad and cannot target
  one device, so the first time the Virtual Gamepad is among the game's input devices this module edits that one device's
  bindings, as the Controls screen would, and saves them (the calls are the ones spike #99 proved in the game:
  g_inputBinding:getActionByName, action.bindings, Binding.new + addBinding, taking a binding out of action.bindings, and
  saveToXMLFile; plus resetActiveActionBindings, see below). The layout (button numbers are the game's own for an Xbox pad: 5 / 6 LB / RB, 10 Start, 11 / 12 the
  left / right stick click, AXIS_3 the right stick X):
    - Snap Turn left / right on the right stick's left / right half (AXIS_3- / AXIS_3+), off LB / RB
    - the game's smooth look left / right (AXIS_LOOK_LEFTRIGHT_PLAYER and _VEHICLE) off the right stick X
    - the VR View Toggle Action on left stick click + Start, off Back (which the pad never sends)
  The Recenter Action is NOT rewritten: it keeps the Companion Mod's own GAMEPAD_DEFAULT for the pad, the left stick click
  (BUTTON_11). Measured in the game: while VR_RECENTER_VIEW carries an XInput BUTTON_12 binding the engine never fires the action
  in the world at all, keyboard numpad 5 included (BUTTON_8 and BUTTON_11 do not do that). An earlier build put Recenter on RS; see
  THE RECENTER MIGRATION below.
  Two things measured in the headset: (1) Binding.new(deviceId, inputNames, axisComponent, negative) takes an axis input's sign
  from the fourth argument alone and silently drops a "-" inside the name, so Snap Turn left is Binding.new(device, { "AXIS_3" },
  "+", true) (inputString "AXIS_3-"); (2) taking a binding out of action.bindings only changes what the Controls screen shows, the
  game acts on action.activeBindings until g_inputBinding:resetActiveActionBindings() rebuilds it, so the rewrite calls that
  after its changes (before saving).
  A new binding takes the `index` slot the binding it replaces had, otherwise the lowest one the device has free on that
  action (addBinding refuses an occupied slot). Only bindings of the Virtual Gamepad's device are looked at; a real pad's are
  never touched. The device is "<slot>_XINPUT_GAMEPAD" with the slot VRBridge reports. It counts as among the game's input
  devices once the game has attached it as a connected device (g_inputBinding:getDeviceById(<device>) is a table; the spike
  found it nil for a pad that is only saved, where getMissingDeviceById has the table) and has bindings for it on the Companion
  Mod's own actions (the game applies GAMEPAD_DEFAULT to every pad it lists). The bindings alone are not enough: an unplugged
  pad that once sat in the same slot keeps its saved bindings, and editing those would set the flag before the game has the
  Virtual Gamepad. Until then the module looks again about once a second and says only that it is waiting. (Without
  getDeviceById the bindings alone are the signal.)
  THE SAVED FLAG is one global flag (the spec's single saved flag), not one per device. The pad takes its slot once, when it
  appears, and a real pad plugged in later takes a lower one, so the device is normally the same session after session. If
  the slot ever does change, the new device keeps the game's defaults: that is the lesser harm next to rewriting a player's
  bindings a second time. Once set (after the bindings were saved) the rewrite never runs again, so what the player changes
  in the Controls screen sticks.
  THE RECENTER MIGRATION: a profile whose rewrite already ran in a build that put Recenter on RS has VR_RECENTER_VIEW on
  BUTTON_12 of the Virtual Gamepad's device, which stops the engine firing the action. A second saved flag
  (virtualGamepadRecenterMigrated, default false) makes the next session, once the device is listed as for the rewrite, take a
  VR_RECENTER_VIEW binding of that device whose input is exactly BUTTON_12 out of action.bindings, add BUTTON_11 for that device
  unless the action already has it there (same index rule as the rewrite), rebuild the active bindings, save, and set the flag.
  Nothing is changed when there is no such binding (a Recenter the player moved elsewhere stays), and no other device is looked
  at. A fresh profile's rewrite sets the flag too. The fallback is the rewrite's: one warning, bindings restored, no flag.
  THE FALLBACK: the binding API missing (g_inputBinding, Binding.new, getActionByName, addBinding, resetActiveActionBindings or saveToXMLFile) or raising
  is one warning in the game log for the session, no flag, and the bindings put back as they were. The pad then keeps the
  game's defaults plus the Companion Mod's GAMEPAD_DEFAULT bindings, and the player rebinds in the Controls screen.

    Hooks into the rest of the mod:
      MotionControllers.lua  updateMotionControllers -> updateVirtualGamepad(dt) first of all and updateVirtualGamepadUse(dt) last,
                             getMotionControllerStatus -> getVirtualGamepadStatus. The settings hooks
                             (save/loadVirtualGamepadSetting), the console command and the unload (resetVirtualGamepad) are
                             registered with the core at the end of this file.
  ```

- **`VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_DEFAULTS_WRITTEN`**: Set once the VR-comfort default bindings have been written to the Virtual Gamepad and saved. Saved: never rewrites again.
- **`VRCompanion.DEFAULT_VIRTUAL_GAMEPAD_RECENTER_MIGRATED`**: Set once the Recenter migration (see THE RECENTER MIGRATION) has run, or was not needed: a fresh profile's rewrite sets it too. Saved: the migration never runs again.
- **`INPUTS`**: The inputs of a hand a use can take, with the bit the Injector knows each by (VirtualGamepad::kInput\*).
- **`HELD_THRESHOLDS`**: When an input counts as held, as the pad reads it (pressed above the first number, let go below the second): the trigger is analog, so any pull the game would notice counts; the thumbstick is held out of the game's dead zone. The grip is the pad's digital LB / RB: the Injector decides when it is held, and tells through VRBridge.getVirtualGamepad.
- **`VRCompanion:updateVirtualGamepad`, `if state.seenKey ~= nil or present or reason ~= "off" then`**: The first look at a pad that is simply off says nothing: a player who never turned it on reads no line.
- **`VRCompanion:takeVirtualGamepadInput`**: A use of the Companion Mod asks for an input of a hand, every update it wants it. True: the use has it, and the input reads as released on the pad from here on. False: the input was already held when the use would have begun, so it keeps its binding until it is let go and the use must not act on it. Asking again in the same update gives the same answer.
- **`VRCompanion:takeVirtualGamepadInput`, `continuing`**: The use goes on from the update before, or from one that took the input and still has it held (a ray that flickers off a Click Point for a frame does not hand a pulled trigger to the pad). Otherwise it begins now, and an input that was already held before this update is the binding's: the squeeze that starts the use is not.
- **`VRCompanion:isSettingsPageShowing`** (in `VRCompanionCore.lua`, shared with `VRCompanionMenuPointer.lua`): Whether the pause menu's Settings page is the page showing (the Controls screen is a sub-page of it and cannot be told apart). Measured in the game: while it shows, g_gui.currentGuiName is "InGameMenu" and g_inGameMenu.currentPage is the page object whose id is "pageSettings" (it is also g_inGameMenu.pageSettings). Anything missing or raising counts as not showing.
- **`VRCompanion:updateVirtualGamepadUse`, `dialogShowing`**: A Stereo Dialog is answered with the pad's own buttons, so a fresh press reaches it. Its answer buttons act on release, though: an input already held when it opened is not the dialog's, and reads released until it is let go (the held-input rule of any menu, for the dialog).
- **`VRCompanion:updateVirtualGamepadUse`, `for _, hand in ipairs(HANDS) do`**: Every input of both hands: the Pointer Ray menus act on the raw hand state themselves.
- **`VRCompanion:updateVirtualGamepadUse`, `record.suppressed`**: No use wants it any more, but what a use took stays off the pad until it is let go.
- **`VRCompanion:updateVirtualGamepadUse`, `record.heldAtDialog`**: Held across the dialog's opening: off the pad until let go, even if the dialog closes first.
- **`VR_ACTIONS`**: The VR actions: the game applies GAMEPAD_DEFAULT to every pad it lists, so a binding of one of these on the Virtual Gamepad's device is how the Companion Mod knows the game has given the device its defaults.
- **`GIVEN`**: axisComponent is "+" on every binding the rewrite adds, Snap Turn's two stick halves included. The Snap Turn actions are plain (axisType HALF) actions, and the game stores a stick half on such an action with axisComponent "+" whatever its sign: the shipped templates and the player's saved inputBinding.xml give every button and key binding of these actions "+", and a half-axis action a player bound to a stick half by hand in the Controls screen (AutoDrive's ADSelectPreviousTargetUnload on "BUTTON_5 BUTTON_6 AXIS_3-", ADDebugChangeNeighbor on "AXIS_4-", ADSelectNextFillType on "AXIS_2-") is saved with axisComponent="+". The sign-matching pairs (AXIS_3- with "-") belong to FULL-axis actions such as AXIS_LOOK_LEFTRIGHT_PLAYER, which the rewrite takes off the pad rather than adds.
- **`MIGRATION_RULES`**: THE RECENTER MIGRATION: an earlier build's rewrite put the Recenter Action on RS (BUTTON_12), and the engine then never fires that action at all (measured in the game: keyboard numpad 5 too). For a profile whose rewrite already ran, the migration takes a VR_RECENTER_VIEW binding that is exactly BUTTON_12 off the Virtual Gamepad's device and, only then, gives it the stick click (BUTTON_11, the Companion Mod's own GAMEPAD_DEFAULT for the action) unless the device already has it on the action.
- **`bindingArguments`**: Binding.new(deviceId, inputNames, axisComponent, negative) takes the sign of an axis input from its fourth argument alone: a "-" inside the name is dropped ({ "AXIS_3-" } comes out "AXIS_3+", measured in the game). So a GIVEN input such as "AXIS_3-" is passed as the name "AXIS_3" and negative = true; a name without a "-" passes nil (any non-nil value makes the input negative). A combination such as { "BUTTON_11", "BUTTON_10" } has no axis and passes as it is.
- **`planRewrite`, `index`**: The slot of the binding it replaces if there is one, otherwise the lowest the device has free on the action.
- **`applyRewrite`, `binding`**: axisComponent "+" and, for the left half of the stick, negative (see GIVEN and bindingArguments).
- **`applyRewrite`, `inputBinding:resetActiveActionBindings()`**: The Controls screen shows action.bindings, the game acts on action.activeBindings: a binding taken out of the former keeps acting (the smooth look went on turning the view) until the latter is rebuilt from it.
- **`VRCompanion:updateVirtualGamepadDefaults`, `changes`**: A profile that needs no migration changes nothing and saves no bindings: it is only flagged.
- **`VRCompanion:updateVirtualGamepadDefaults`, `if type(inputBinding.resetActiveActionBindings) == "function" then`**: What the game acts on goes back with them (it may have been rebuilt from the changed bindings before the failure).
- **`VRCompanion:updateVirtualGamepadDefaults`, `self.virtualGamepadRecenterMigrated`**: A fresh profile gets the layout without Recenter on RS, so it never needs the migration either.
- **`VRCompanion:resetVirtualGamepad`**: Map unload: the pad stays in the game (the OpenXR session goes on) but reads neutral; the next map sends everything again.
- **`registerSubsystem "virtualGamepad"`**: Registered with the core (the registry is documented in VRCompanionCore.lua). No update hook: updateVirtualGamepad runs FIRST of the Motion Controller poll and updateVirtualGamepadUse LAST (see updateMotionControllers), which file order cannot give. Unload runs in reverse file order, so the pad is zeroed first of the Motion Controller subsystems, through VRBridge, after the core's VRBridge.onMapUnload. It shares no state with the Wheel Grab, Virtual Hands and Menu Pointer resets that follow it.

### `src/input/virtual_gamepad.cpp`

- **`g_originalGetState`**: The real XInput functions, kept by the hook layer itself (set when a pad installs its detours, cleared when it removes them): a hooked call that finds no pad to answer for it (a runtime going away under a call in flight) still reaches the real function, so a real pad never vanishes from the game.
- **`template <typename Fn>`**: The real function a call for a slot that is not the pad's goes to: the pad's own original while its detour is installed, otherwise the hook layer's (a hook entry point can be routed to a pad whose detour is not the installed one).
- **`VirtualGamepad::RefreshSnapshot`**: Publishes both hands as this frame began, the only thing the game's XInput thread reads. OpenXR is asked here, with no lock held; the game's thread never asks it.
- **`VirtualGamepad::EvaluateLocked`, `reason = kReasonNoHooks;`**: A pad the game cannot see is no pad: the Companion Mod would write bindings for it and set its permanent flag.
- **`VirtualGamepad::EvaluateLocked`, `if (reason == kReasonPresent && m_slot != kNoSlot && NowMs() >= m_nextSlotChe...`**: A real pad that plugs into the pad's slot keeps it: the pad gives the slot up and takes a free one below.
- **`VirtualGamepad::EvaluateLocked`, `for (int slot = kSlotCount - 1; slot >= 0; --slot) {`**: The pad appears: the highest slot no real pad occupies, so a real Xbox pad keeps the first one.
- **`VirtualGamepad::BuildGamepadLocked`, `for (uint32_t hand = OpenXrInput::kLeft; hand <= OpenXrInput::kRight; ++hand) {`**: Each physical control on each hand is its own pad input (ADR-0028). Back is never fed; the D-pad comes only from the right stick chord below.
- **`VirtualGamepad::BuildGamepadLocked`, `state`**: as the last frame began: the game's thread asks OpenXR for nothing
- **`VirtualGamepad::BuildGamepadLocked`, `used`**: An input the Companion Mod is using reads as released (the grip's hysteresis keeps tracking underneath).
- **`VirtualGamepad::ApplyRightStickChordLocked`**: With the right stick click held, the stick is the D-pad (the direction of the dominant axis, engaged at kStickEngage and held down to kStickRelease so a wobble between them neither drops nor flips it) and reads centred; RS is never reported while the click is down (ADR-0028).
- **`VirtualGamepad::GetCapabilities`, `capabilities->Gamepad.wButtons = 0xF3FF;`**: What a wired Xbox pad reports: every button but the guide button, full-range triggers and sticks.
- **`VirtualGamepad::InstallHooksLocked`, `install(m_detourGetState, "XInputGetState", reinterpret_cast<void*>(&VirtualG...`**: The state hook goes last: it is the one the other calls and the slot probe wait for.
- **`VirtualGamepad::InstallHooksLocked`, `RollBackHooksLocked();`**: Half a set of hooks would show the game a pad it cannot poll: none, and no pad (see EvaluateLocked).
- **`VirtualGamepad::UninstallHooksLocked`, `forget`**: The kept originals go before the trampolines they point into.

### `src/input/virtual_gamepad.h`

- **`VirtualGamepad`**: The Virtual Gamepad (ADR-0028): the Motion Controllers' triggers, grips, thumbsticks and buttons presented to the game as an Xbox pad it reads through XInput. The Injector answers the game's XInputGetState, XInputGetCapabilities and XInputSetState for one slot, the highest no real pad occupies, and passes every other slot through to the real xinput1_4.dll. The pad is built from the per-hand raw inputs the OpenXR action set already delivers (ADR-0025); what it feeds is fixed here, what each input does is bound in the game's own Controls screen.

  The pad is wanted from the start (the Companion Mod's saved setting can turn it off, and tells this class through VRBridge with Configure, as it does the Motion Controller switch). The XInput hooks are installed once a live InjectorRuntime owns the pad, it is wanted, and the OpenXR session is active with its Motion Controller action set: the main menu and the Controls screen reached from it already list the pad, and a game in Pass-Through Mode (no session) has no hook at all.

  Threads: the game polls XInput from a thread of its own. That thread never calls OpenXR: the render thread publishes an immutable snapshot of both hands at the start of every OpenXR frame (and once at start) and the poll reads only that, under the pad's mutex.

- **`VirtualGamepad`, `Configure`**: `enabled`: the vrVirtualGamepad setting (the pad is wanted). `live`: Motion Controller input is on (vrMotionControllers); while it is off the pad stays present but reads neutral.
- **`VirtualGamepad`, `kInputTrigger`**: The Companion Mod's own uses of the Motion Controllers (a menu, a Click Point click, the Steering Wheel): while it uses an input, that input reads as released or centred on the pad, so a use never also fires a binding. One bit set per input of the hand (0 = left, 1 = right), sent by the Companion Mod each update; the Companion Mod applies the held-input rule before it asks, so an input already held for a binding is simply not in the set.
- **`VirtualGamepad`, `GetStatus`**: Whether the pad exists now and in which slot. It exists while it is enabled, the OpenXR session is active with its Motion Controller action set attached, the XInput hooks are installed (when a runtime owns the pad), and a slot is free. A real pad that plugs into the pad's slot makes the pad give the slot up and take a free one (looked at about once a second).
- **`VirtualGamepad`, `GetState`**: The answers the XInput hooks route the game's calls to. Calls for any slot but the pad's go to the real function (ERROR_DEVICE_NOT_CONNECTED when there is none to call).
- **`VirtualGamepad`, `NoteCompanionPulse`**: The Companion Mod played a pulse of its own on `hand` (a Click Point tick, a Wheel Grab pulse) lasting `durationMs`: the game's rumble stays off that hand while it plays, then resumes.
- **`VirtualGamepad`, `SetOwnedByRuntime`**: Detours are installed only while a live InjectorRuntime owns the pad, so the hooks can always find the pad that holds their originals. They are put in when the pad is wanted and the OpenXR session has its action set (checked when ownership begins, when the setting changes and at the start of every OpenXR frame).
- **`VirtualGamepad`, `kGripPress`**: Grips are digital on the pad (LB / RB): pressed above this, released below the other.
- **`VirtualGamepad`, `kRumbleScale`**: The game's rumble plays on the Motion Controllers as short pulses, renewed on every OpenXR frame while a motor runs (a pulse that is not renewed dies out by itself), at a fixed fraction of a gamepad's strength.
- **`VirtualGamepad`, `kStickEngage`**: The right-stick D-pad chord (ADR-0028): with the right stick click held, a push of that stick is the D-pad and the stick reads centred. The thresholds are the ones the Companion Mod's own Snap Turn push used before that mapping was removed (0.7 / 0.4).
- **`VirtualGamepad`, `kRsReleasePulseMs`**: A click let go without a push is reported as RS pressed for this long, so the game sees a press.

### `tests/test_virtual_gamepad.cpp`

- **`StartMod`, `Pad().SetClockForTesting([this] { return GameTime(); });`**: a click let go times its RS press by the game clock
- **`VirtualGamepadTest`, `Read`**: What the game reads from XInput slot 3 (the highest free slot of a machine without real pads).
- **`VirtualGamepadTest.TheGameReadsTheSnapshotOfTheLastFrameAndAReadNeverSyncsOpenXr`, `headset.manager->EndFrame();`**: The controller moves between two frames (the frame has ended, the next has not begun). XInput is polled off the render thread, so the poll answers from what the last frame began with and asks OpenXR for nothing (a read with no frame in flight would sync the actions again, and xrSyncActions is the render thread's alone).

### `tests/test_virtual_gamepad_buttons.cpp`

- **the file**: The Virtual Gamepad is how Motion Controller buttons work (ADR-0028, spec #98, ticket #105), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test pushes the fake Motion Controllers' thumbsticks and buttons, drives frames and the game clock, and observes the camera's yaw, the Recenter calls reaching the bridge, the HUD state and what the game would read from XInput.

  These replace the tests of the Companion Mod's own button mapping (spec #71), which is gone: Snap Turn, the Recenter Action and the HUD Toggle Action from the Motion Controllers now arrive only as game actions bound on the Virtual Gamepad, so with the pad off the buttons fire none of them, and with it on they reach the game as pad inputs and nothing fires twice.

### `tests/test_virtual_gamepad_cab_use.cpp`

- **the file**: The Virtual Gamepad while the Companion Mod uses a Motion Controller input in the world (ADR-0028, spec #98, ticket #101), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, in a cab whose vehicle steers the way the game's does (StubSteer) and has the stand-in for the Interactive Control mod (StubIC) with a Click Point straight ahead of each hand (companion_mod_stubs.h). The test moves the fake Motion Controllers and reads what the game would read from XInput (the answers the hooks route the game's calls to) next to what Interactive Control executed and whether a hand holds the Steering Wheel. Two uses are covered:

  - the Pointer Hand's trigger while the Pointer Ray is on a Click Point: it clicks it and reads released on the pad
    - the grip of a hand holding the Steering Wheel: it steers and reads released on the pad

  and in both the held-input rule: an input already held on the pad when the use would begin keeps its binding until it is let go, and the use does not act on it. Prior art: test_wheel_grab_pointer.cpp and test_virtual_gamepad.cpp.

  The cab is at the world origin facing +Z with the head at the tracking space origin (see wheel_grab_test_support.h).

- **`VirtualGamepadCabUseTest.WhileAStereoDialogShowsThePointerRayHitsNoClickPointAndTheTriggerClicksNothing`**: A Stereo Dialog (spec #142, story 10): the dialog is answered with the pad's buttons, so the Pointer Ray does not select Click Points behind it and a pull of the trigger is not also a click.

### `tests/test_virtual_gamepad_default_bindings.cpp`

- **the file**: The Virtual Gamepad's VR-comfort default bindings (ADR-0028, spec #98, ticket #104), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, with a stand-in for the game's g_inputBinding shaped after the finding of spike #99 (companion_mod_stubs.h). The test turns the Virtual Gamepad on, lets frames run and reads what the game's bindings look like and whether the game was told to save them. Covered: the one-time rewrite of the Virtual Gamepad's device only, a real pad left alone, the saved flag that stops a second rewrite for good (also after the player changed a binding), and the fallback (no API, an API that raises) that leaves the bindings and the flag as they were.

  The Virtual Gamepad takes XInput slot 3 here (no real pads in the test process), so its device is 3_XINPUT_GAMEPAD; the stand-in also has a real pad in slot 0. The button numbers are the game's own for an Xbox pad (the default gamepad binding template): BUTTON_5 / BUTTON_6 are LB / RB, BUTTON_10 Start, BUTTON_11 / BUTTON_12 the left / right stick click.

- **`VirtualGamepadDefaultBindingsTest.TheFirstSessionRewritesTheVirtualGamepadToTheComfortLayoutAndSavesIt`, `EXPECT_EQ(List("VR_RECENTER_VIEW", kPad), "BUTTON_11/+@4") << "the Recenter A...`**: The engine drops VR_RECENTER_VIEW altogether (keyboard numpad 5 too) while it carries an XInput BUTTON_12 binding, measured in the game: the rewrite leaves Recenter on the Companion Mod's own GAMEPAD_DEFAULT (the left stick click).
- **`VirtualGamepadRecenterMigrationTest`**: The Recenter migration: profiles whose rewrite ran in a build that put Recenter on RS (BUTTON_12)

  The engine drops VR_RECENTER_VIEW while it carries an XInput BUTTON_12 binding, so such a profile's saved inputBinding.xml has to be put right. The tests build that profile from a normal first session (LeaveProfileOfAnEarlierBuild): the saved bindings are edited as the earlier build left them (Recenter on BUTTON_12 on the device(s) named) and the settings file has the first flag only. The next session (StartNextSession) is the one in which the migration may run.

- **`VirtualGamepadDefaultBindingsTest.SnapTurnLeftAndRightLandOnTheTwoDifferentHalvesOfTheRightStick`, `ASSERT_NO_FATAL_FAILURE(StartGame());`**: The game's Binding.new takes the sign of an axis input from its fourth argument alone (a "-" in the name is dropped), so Snap Turn left must be asked for as a negative AXIS_3 with the axisComponent "+" the game saves for a half-axis action.
- **`VirtualGamepadDefaultBindingsTest.ABindingTakenOffTheVirtualGamepadStopsActingInTheGameAndARealPadsKeepsActing`, `ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD', '3_XINPU...`**: The Controls screen shows action.bindings, the game acts on action.activeBindings: both must lose the smooth look.
- **`VirtualGamepadDefaultBindingsTest.BindingsOfAnOldPadThatUsedTheSameSlotAreNotRewrittenUntilTheGameHasThePadAttached`, `ASSERT_NO_FATAL_FAILURE(StartGame("{ devices = { '0_XINPUT_GAMEPAD', '3_XINPU...`**: An unplugged pad once sat in slot 3: the game still lists its saved bindings (and the Companion Mod's defaults on them), but there is no connected device behind them yet.

### `tests/test_virtual_gamepad_menu_use.cpp`

- **the file**: The Virtual Gamepad while a menu shows (ADR-0028, spec #98, ticket #101), seen from outside: the real Companion Mod runs against the real VRBridge over FakeTrackedHeadset, the test drives the fake Motion Controllers and the game's menu, and reads what the game would read from XInput (the answers the hooks route the game's calls to) next to what the stub GUI receives. While a menu shows, the Companion Mod is using every input on both hands (the Pointer Ray menus: cursor, trigger click, grip or lower face button for back, stick scroll), so none of them reaches the pad, except an input already held when the menu came up, which keeps its binding until it is let go.
- **`kGuiStubs`**: The engine's console, and the game's GUI as the mod sees it: it records the mouse events (cursor moves, clicks, wheel steps) and the menu back input it is given. `list` covers the middle of the screen and scrolls the way the game's list elements do (they poll the mouse wheel state while handling a mouse-down inside them).
- **`VirtualGamepadMenuUseTest.OnThePauseMenusSettingsPageTheMotionControllersReachThePadSoTheyCanBeBoundInTheControlsScreen`**: The pause menu's Settings page (the Controls screen is a sub-page of it): the player must be able to bind the pad

### `tests/test_xinput_hooks.cpp`

- **`XInputHooksTest.TheHooksComeWithTheOpenXrSessionAndItsActionSetWithoutTheCompanionModAndNotInPassThroughMode`, `fakeRuntime`**: The session starts after the Injector is up (the game reaches its first frame): the pad appears with the first frame, no Companion Mod needed, so the main menu and the Controls screen reached from it already list it.
- **`XInputHooksTest.WhenTheHooksCannotBeInstalledThereIsNoPadAndTheReasonSaysSo`, `secondHeadset`**: A second Injector in the process cannot hook what the first has hooked: the game could never see its pad.
- **`XInputHooksTest.ACallThatReachesNoPadStillGoesToTheRealFunctionSoRealPadsDoNotVanish`, `s_stateSlot`**: The hook entry points can run with no pad to answer (a runtime going away under a call already in flight). The original functions are kept by the hook layer itself, so such a call reaches the real XInput.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 68 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
