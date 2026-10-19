# Stereo Dialogs Stay in the Stereoscopic View

Amends [ADR-0007](0007-in-game-gui-menu-detection-and-quad-layer-switching.md).

## Context and Decision
ADR-0007 moves the presentation to the floating menu quad whenever `g_gui:getIsGuiVisible()` is true. That is right for the pause menu, the shop or the map, but the fishing rod (Highlands Fishing DLC) opens a small sell-or-release dialog on every catch. Each catch then drops the player out of the 3D world, their Virtual Hands and the rod in the Tool Hand, for a two-button choice, and snaps back.

**A short list of named Stereo Dialogs stays in the stereoscopic view. Every other menu and dialog still gets the quad.**

- The list starts with the fish catch dialog only. A dialog joins it only after it is tested in the headset; a dialog the mod does not know falls back to the quad, so the player is never left in stereo with a dialog they cannot use.
- "A menu is open" splits into two meanings. While a Stereo Dialog shows, input stays a menu: locomotion, Snap Turn and Wheel Grab are refused, and the Virtual Gamepad sends buttons to the dialog. Presentation stays stereo: the Companion Mod does not tell VRBridge a menu is visible, so the Injector keeps the projection layer, and head placement, the Virtual Hands and the Tool Hand keep running. Since the presentation does not change, there is no Discontinuity Flush.
- The dialog is answered with its own gamepad buttons through the Virtual Gamepad. The Pointer Ray does not reach it, because it only hits the quad panel.
- The dialog is drawn at HUD depth: its root position is shifted by the HUD Convergence Offset on alternating AFR eye frames, like the HUD Groups, and moved back after each draw. If a dialog has no root position that moves it cleanly, it is drawn without a shift (zero disparity).
  - The probe (#143) found the dialog's root to be a full-screen element at 0,0, size 1x1, with the visible box a child (`target.dialogElement`). The engine's GuiElement:setAbsolutePosition moves an element and all its descendants by the same amount, so that is the root position that moves the whole dialog cleanly (read from the game's Lua source; the headset check of #146 is what confirms it on screen). The Companion Mod wraps Gui.draw, the draw of every screen and dialog, so the move happens in the draw of the eye being rendered: the root is moved by the current frame parity's offset, the draw runs, and the root is moved back by the same amount from wherever the draw left it (also when the draw fails), so values the game derives from the dialog's own position cannot drift.
  - The first headset check found the dialog at the HUD's 1.8 m too close: it fills far more of the view than a HUD group. The dialog therefore has a depth of its own, default 2.5 m, kept in a saved setting and changed with `vrDialogDepth [0.5-5.0]`; the shift is the disparity of that depth, with the sign of the eye being rendered. It is still no shift while the HUD's convergence is off (pass-through, no tracking).
  - The fallback to zero disparity stays for a dialog whose root has no setAbsolutePosition or `absPosition`. It is not in use for the fish catch dialog; if the headset check shows the dialog does not move as one image, this is where to fall back and record why.
- A Stereo Dialog stays in stereo whatever the input device; without the Virtual Gamepad the player answers it with their own gamepad or keyboard. In 2D Floating Window Mode it behaves like any menu, since the presentation is already the quad.
- A saved console command, `vrFishCatch [ask|sell|release]` (default `ask`), can answer the fish catch dialog as it opens, in the stereoscopic view and 2D Floating Window Mode alike, and show a short notification of the catch instead. It answers through the dialog's own answer path, or, if it has none the mod can call, by sending the dialog's button through the Virtual Gamepad. Built (ticket #147): the answer path is the dialog's `onYes()` (sell) and `onNo()` (release), which the engine's own yes/no dialog class calls itself; a dialog without them is left open for the player, because the Virtual Gamepad only mirrors the Motion Controllers and the mod cannot press a pad button of its own.

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
