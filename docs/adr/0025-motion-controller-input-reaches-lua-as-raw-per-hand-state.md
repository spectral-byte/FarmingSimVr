# Motion Controller Input Reaches Lua as Raw Per-Hand State

## Context and Decision
Motion Controllers drive the Pointer Ray, the Virtual Hand, clicks on Click Points and menus, Snap Turn, the Recenter Action and the HUD Toggle Action. The game has no VR input of its own, so every Motion Controller input has to come from the Injector's OpenXR session. OpenXR lets an application declare either game-level actions (such as "click" or "snap turn left") or plain device inputs, and the runtime's binding screen remaps whatever is declared.

**The Injector declares a fixed set of raw inputs per hand and passes their state to the Companion Mod through VRBridge. The Companion Mod decides what each one does.**
- Per hand: the aim pose (the Pointer Ray's fallback), the grip pose (where a Virtual Hand is drawn and where the Pointer Ray starts), trigger and grip values, the thumbstick axes and click, two face buttons and the menu button, plus whether the hand is tracked.
- The Pointer Ray starts at the index fingertip of the Virtual Hand and runs along the finger: a fixed offset in the grip frame, applied to the grip pose. The aim pose is used only when the grip pose is not tracked (a runtime that refuses it). The Injector's menu and 2D Floating Window Mode panel hit test takes that same ray from the Companion Mod (`VRBridge.getPanelHitAlongRay`), so the ray is the same in the world and on a panel. The original design started the ray at the aim pose and left the fingertip out of scope; it was reversed because the player wants the laser aligned with the finger they see.
- VRBridge also takes haptic pulse requests from the Companion Mod for either hand.
- The Injector declares the action set and its suggested bindings once, when it creates the session, since OpenXR allows attaching action sets only once per session. It declares them whether or not the feature is on. `vrMotionControllers off` makes the Companion Mod ignore the input; it never changes the session.
- Which Motion Controller is the Pointer Hand, what the trigger does in the world or in a menu, the button mappings and their saved settings all live in the Companion Mod, like the rest of the VR behaviour.

## Status
Proposed. Partly replaced by ADR-0028: buttons, triggers and sticks also reach the game as a Virtual Gamepad, bound in the game's Controls screen.

## Considered Options
- **Raw per-hand inputs, with their meaning set in the Companion Mod** (selected: the meaning of a button depends on game state only the Companion Mod sees, such as whether a menu is open, which hand is the Pointer Hand and whether a Click Point is selected. A new mapping is a Lua change, with no new Injector build).
- **Game-level OpenXR actions defined by the Injector** (rejected: every new mapping would need an Injector change and a new session, and the Injector would have to track menu and Click Point state to tell which action fired. The only gain is that players could remap game actions in the SteamVR binding screen; there they can still remap the raw inputs).
- **Emulating a gamepad or the mouse from the Motion Controllers outside the game** (rejected: that loses the hand pose, so it cannot aim a Pointer Ray at a Click Point).

## Consequences
- The SteamVR binding screen shows the raw inputs (trigger, grip, thumbstick and so on), not game actions. The Companion Mod is where players change what a button does.
- An OpenXR runtime that refuses the action set, or a session with no Motion Controllers, must leave head tracking and presentation working: VRBridge reports the hands as untracked and the Companion Mod does nothing with them, as with Pass-Through Mode in ADR-0004.
- Adding a raw input later (for example, a touch sensor) changes the action set, so it needs an Injector release. Changing what an existing input does does not.
