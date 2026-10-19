# Stereo Dialogs Stay in the Stereoscopic View

Amends [ADR-0007](0007-in-game-gui-menu-detection-and-quad-layer-switching.md).

## Context and Decision
ADR-0007 moves the presentation to the floating menu quad whenever `g_gui:getIsGuiVisible()` is true. That is right for the pause menu, the shop or the map, but the fishing rod (Highlands Fishing DLC) opens a small sell-or-release dialog on every catch. Each catch then drops the player out of the 3D world, their Virtual Hands and the rod in the Tool Hand, for a two-button choice, and snaps back.

**A short list of named Stereo Dialogs stays in the stereoscopic view. Every other menu and dialog still gets the quad.**

- The list starts with the fish catch dialog only. A dialog joins it only after it is tested in the headset; a dialog the mod does not know falls back to the quad, so the player is never left in stereo with a dialog they cannot use.
- "A menu is open" splits into two meanings. While a Stereo Dialog shows, input stays a menu: locomotion, Snap Turn and Wheel Grab are refused, and the Virtual Gamepad sends buttons to the dialog. Presentation stays stereo: the Companion Mod does not tell VRBridge a menu is visible, so the Injector keeps the projection layer, and head placement, the Virtual Hands and the Tool Hand keep running. Since the presentation does not change, there is no Discontinuity Flush.
- The dialog is answered with its own gamepad buttons through the Virtual Gamepad. The Pointer Ray does not reach it, because it only hits the quad panel.
- The dialog is drawn at HUD depth: its root position is shifted by the HUD Convergence Offset on alternating AFR eye frames, like the HUD Groups, and moved back after each draw. If a dialog has no root position that moves it cleanly, it is drawn without a shift (zero disparity).
- A saved console command, `vrFishCatch [ask|sell|release]` (default `ask`), can answer the fish catch dialog as it opens and show a short notification of the catch instead.

## Status
Accepted

## Considered Options
- **Every dialog that is not a full-screen menu stays in stereo** (rejected: sweeps in untested dialogs, such as text entry, that need the Pointer Ray).
- **Keep the quad, only smaller** (rejected: the dialog and the world share one backbuffer, so the world behind it would still be flat).
- **Skip the dialog always by auto-answering** (rejected as the only option: the player loses the choice; kept as an opt-in setting).
- **Keep every menu gate on and change only the presentation** (rejected: stereo with no hands and a frozen head is worse than the quad).

## Consequences
- The dialog's name, its answer path and what it knows about the fish come from a headset probe, because the DLC's Lua is encrypted.
- "In a menu" in the glossary entries for the Virtual Gamepad and the Virtual Hand gains the Stereo Dialog exception.
