# Motion Controller Buttons Reach the Game as a Virtual Gamepad

## Context and Decision
Players want each Motion Controller's buttons bound on their own and changed in the game like any controller's, for any game action, not only the VR actions. Until now the Companion Mod mapped three VR actions (Snap Turn, the Recenter Action, the HUD Toggle Action) onto buttons shared by both hands, changed only from the console (`vrMotionControllerMap`). FS25 reads gamepads natively (XInput for Xbox pads, Raw Input and HID for the rest) and rebuilds its device list itself, so a device cannot be added to the game's Controls screen from Lua.

**The Injector presents the Motion Controllers' triggers, grips, thumbsticks and buttons to the game as a Virtual Gamepad: an Xbox pad answered from the game's own XInput calls. Players bind it in FS25's Controls screen. Hand poses stay on VRBridge.**
- Each physical control on each hand is its own pad input: triggers to LT / RT, grips to LB / RB, thumbsticks (analog) and their clicks to the sticks and LS / RS, the face buttons to X / Y (left) and A / B (right), the left menu button to Start. Back is not fed. A binding stays with the physical hand, never with the Pointer Hand.
- While the right stick's click is held, pushing that stick is the D-pad in that direction; a click let go without a push is an RS click, sent on release.
- The Virtual Gamepad exists for the whole OpenXR session, in the highest free XInput slot, so a real Xbox pad keeps the first one. It reads neutral while Motion Controllers are off, untracked or put down. `vrVirtualGamepad off` removes it. In Pass-Through Mode there is none.
- While the Companion Mod is using an input, that input reads as released on the Virtual Gamepad: every input while a menu shows (the Companion Mod's own menu handling stays: the Pointer Ray cursor, trigger click, grip or lower face button for back, stick scroll), the Pointer Hand's trigger while the Pointer Ray is on a Click Point, and the grip of a Virtual Hand holding the Steering Wheel. An input already held when such a use begins keeps its binding until it is let go. The Companion Mod tells the Injector through VRBridge which inputs it is using.
- The game's rumble for the pad plays on the Motion Controllers' haptics, left motor on the left hand, scaled down; the Companion Mod's own Click Point pulses win on that hand while they play.
- The game's default gamepad bindings apply to every pad, the Virtual Gamepad included, and cannot target one device. The first time the Companion Mod sees the Virtual Gamepad it rewrites that one device's bindings once (a saved flag stops it doing so again): Snap Turn on the right stick's left and right in place of smooth look, the Recenter Action on RS, Snap Turn off the grips, the VR View Toggle Action on a free combination. Real pads keep their bindings.
- The Companion Mod's own button mapping (`VRCompanionMotionControllerMap.lua`, `vrMotionControllerMap`) is removed; its saved settings are ignored.

## Status
Proposed. Partly replaces ADR-0025: the raw per-hand inputs and their declaration stay as they are, but the meaning of buttons, triggers and sticks outside the Companion Mod's own uses moves from the Companion Mod to the game's bindings.

## Considered Options
- **A Virtual Gamepad from the Injector, bound in the game's Controls screen** (selected: it is the only way the Motion Controllers bind like any controller, for every game action including analog driving and walking, with the game's own saving, glyphs and defaults. An Xbox pad's layout matches the Motion Controllers' almost control for control).
- **A bindings page in the Companion Mod that fires game actions** (rejected: it is not the game's Controls screen, analog actions are hard to fake, and an action the Companion Mod does not know about cannot be reached).
- **Keep the Companion Mod's mapping, per hand** (rejected: still only the VR actions, and still not configurable like a controller).
- **A virtual Raw Input or HID device** (rejected: far more of the game's native device code would have to be answered than for XInput, for no gain to the player).
- **Gamepad emulation outside the game**, rejected in ADR-0025 because it loses the hand pose, does not apply: the poses stay on VRBridge.

## Consequences
- The Injector now answers the game's XInput calls, which is new in-process work beside presentation, and it needs a release whenever the pad's layout changes.
- The game shows Xbox glyphs for the Motion Controllers; on Touch controllers they read like the physical buttons.
- Back and the D-pad have no dedicated control: the shop is reached through the pause menu, and D-pad actions through the right stick click.
- Index controllers have no menu button, so they have no Start; a D-pad or menu on their trackpad needs a new raw input under ADR-0025.
- The one-time rewrite depends on the game's InputBinding, whose readable source is stripped. If the rewrite turns out impossible or a game patch breaks it, the Virtual Gamepad starts with the game's defaults plus the Companion Mod's own pad defaults, and the player rebinds Snap Turn in the Controls screen.
- Wheel Grab still steers through `setSteeringInput` (ADR-0027); the Virtual Gamepad's stick steering is dropped like any other device's while the Steering Wheel is held.
