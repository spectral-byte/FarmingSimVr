# Farming Simulator 25 VR (FS25 VR)

[![CI](https://github.com/spectral-byte/FarmingSimVr/actions/workflows/ci.yml/badge.svg)](https://github.com/spectral-byte/FarmingSimVr/actions/workflows/ci.yml)
[![OpenXR](https://img.shields.io/badge/OpenXR-1.0-blue.svg)]()
[![DirectX](https://img.shields.io/badge/Direct3D-12-orange.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)]()
[![Release](https://img.shields.io/badge/release-v1.0.0-success.svg)](https://github.com/spectral-byte/FarmingSimVr/releases)

A VR mod for **Farming Simulator 25** (Direct3D 12). It works with any OpenXR runtime: Meta Quest Link/AirLink, Virtual Desktop, SteamVR, Pimax and Bigscreen Beyond.

## How it works

Two parts work together:

- **`dxgi.dll`** is a Direct3D 12 proxy placed in the game folder. It hooks the swapchain, runs the OpenXR session, and shares head poses with the game through the `VRBridge` table.
- **`FS25_VRCompanion.zip`** is a Luau mod that runs inside the game. It reads `VRBridge` and drives the cameras, HUD, snap turn and view toggles.

## Features

- **6DOF head tracking** on foot and in vehicle cabs.
- **Two stereo modes**:
  - **Sequential Dual-Pass (SDP)**, the default. Renders both eyes each frame and suits high-end GPUs.
  - **Alternate Frame Rendering (AFR)** alternates eyes per frame and is lighter on midrange GPUs. Enable it with `vrRenderingMode afr`.
- **Upscaling**: DLSS/DLAA and FSR3 work in both modes with per-eye history. The in-engine TAA ghosts in VR, so don't use it.
- **VR HUD** split into four groups (Map, Speedometer, Info, Controls) that you can place, scale and show or hide. It cycles Full, Streamlined and Hidden.
- **Floating menu layer** for 2D menus, with a mouse cursor.
- **2D view toggle** between full VR and a floating virtual screen.
- **Snap turn** (30°, 45° or 90°) on foot and in vehicles.
- **Desktop mirror window** that stays unscaled whatever the headset resolution.
- **Virtual Hands**: a gloved hand drawn where each Motion Controller is held. The trigger curls the index finger and the grip the other three. The Pointer Ray starts at the index fingertip and runs along the finger.
- **Hand Tools** (on foot): the pressure washer lance sits in your Tool Hand, the right Motion Controller, closed around the handle, and follows it, and what the game highlights by look (the targeter) follows the tool: the look ray starts at the lance and runs along its nozzle; the Tool Hand's trigger uses it (hold it to wash) and the other hand points at Click Points. Set the controller down or lose tracking and the tool goes back in front of your view and the look ray back to your head. Other Hand Tools stay as the game has them. Switch it off with `vrHandTools off`.

## Installation

**Requirements**: Windows 10/11 (64-bit), Farming Simulator 25 (Steam, Epic or GIANTS standalone), and an OpenXR-capable PC VR headset.

### Installer (recommended)

1. Download `FS25_VR_v<version>_Setup.exe` from the [latest release](https://github.com/spectral-byte/FarmingSimVr/releases) and run it. It needs administrator rights.
2. Pick your game installation. If it isn't found, use **Browse...** to select the folder that contains `x64\FarmingSimulator2025Game.exe`.
3. Optional: tick **Render Resolution** to set the game to your headset's per-eye resolution (windowed, 640 to 8192). Your `game.xml` is backed up first. If you've never launched the game, launch it once and run the Installer again.
4. Finish the wizard. The game must be closed.

The Installer copies `dxgi.dll` to the game's `x64` folder and `FS25_VRCompanion.zip` (not extracted) to your `mods` folder. An existing `dxgi.dll` such as ReShade is moved to `dxgi.dll.fs25vr-backup`.

> [!NOTE]
> The Installer isn't code-signed yet, so Windows SmartScreen will warn you. Click **More info**, then **Run anyway**.

**Uninstall**: run the Setup exe again and choose **Uninstall**, or use *Settings > Apps > Installed apps*. For scripts, use `/FS25VR_UNINSTALL`. This removes `dxgi.dll`, the mod zip and `fs25_vr.log`, restores your backed-up `dxgi.dll` and Render Resolution, and keeps your VR settings.

### Manual install

1. Copy `dxgi.dll` from the release zip into the game's `x64` folder (next to `FarmingSimulator2025Game.exe`).
2. Copy `FS25_VRCompanion.zip` into `Documents\My Games\FarmingSimulator2025\mods\`. **Do not unzip it.**

To disable VR, rename or delete `dxgi.dll`.

### Launch

1. Start your OpenXR runtime (Quest Link, SteamVR, Virtual Desktop, etc.) and connect your headset.
2. Launch Farming Simulator 25.
3. When you start a savegame, make sure **FS25 VR Companion Mod** is enabled in the mod list.

## Controls

| Action | Keyboard | Gamepad |
| :--- | :--- | :--- |
| Recenter view | `NumPad 5` | L3 (left stick click) |
| Toggle 3D VR / 2D window | `Right Ctrl + V` | Back (Button 9); on the Virtual Gamepad L3 + Start |
| Cycle HUD (Full, Streamlined, Hidden) | `Right Ctrl + H` | Button 10 |
| Snap turn left | `NumPad 4` | LB |
| Snap turn right | `NumPad 6` | RB |

You can rebind these in the in-game Controls menu under **FS25 VR**.

## Settings and console commands

Settings are saved to `Documents\My Games\FarmingSimulator2025\modSettings\FS25_VRCompanion\settings.xml`.

Open the console with `` ` `` / `~`. Commands that take arguments print their usage and current values when run without any.

| Command | Purpose |
| :--- | :--- |
| `vrRenderingMode <afr\|dualpass\|toggle>` | Switch stereo mode |
| `vrSnapTurnAngle <degrees>` | Snap turn step (default `90`) |
| `vrHudGroup <map\|speedometer\|info\|controls> <horizontal> <vertical> [scale]` | Place and scale a HUD group |
| `vrHudGroupShow <group> <streamlined\|full> <on\|off>` | Choose which groups show in each HUD state |
| `vrHudSafeArea <horizontal> <vertical>` | Keep other HUD elements (warnings, notifications, other mods) inside the lens |
| `vrMotionControllers <on\|off>` | Use Motion Controllers (default `on`, saved). `off` makes the mod ignore Motion Controller input |
| `vrMotionControllerStatus` | Report each Motion Controller's tracked and in-use state, the Pointer Hand and whether the Virtual Gamepad is present (and in which XInput slot) |
| `vrVirtualGamepad <on\|off>` | The Motion Controllers' triggers, grips, thumbsticks and buttons as an Xbox pad in the game, bound in the game's Controls screen (default `on`, saved). Appears in the highest XInput slot no real pad occupies, `off` removes it from the game, either way without a restart, and it reads neutral while `vrMotionControllers` is `off`. The first time the game lists it, the Companion Mod writes VR-comfortable bindings to that pad only, once (Snap Turn on the right stick instead of smooth look, the VR View Toggle Action on left stick click + Start; the Recenter Action stays on the left stick click, and a profile from an earlier build that had it on RS is moved back once); after that every change in the Controls screen sticks, and real pads are never touched. The game log says whether it ran |
| `vrPointerDiagnostics` | Report why the Pointer Ray may not select Click Points (ray placement, who controls the pointer, Interactive Control support; also each hand's aim pose and grip pose and whether they are tracked, how far each index finger points from the aim direction, and each Virtual Hand's state) |
| `vrVirtualHandsDiagnostics` | Report whether the Virtual Hands (gloved hands drawn where the Motion Controllers are held) are available, and each hand's state with the reason when it is hidden |
| `vrPointerCone <degrees>` | How wide a cone around the Pointer Ray selects the nearest Interactive Control Click Point (default `5`, saved) |
| `vrWheelGrab <on\|off>` | Wheel Grab: steer by squeezing the grip with a hand at the Steering Wheel's rim and turning it, in the in-cab camera (default `on`, saved). `off` switches only Wheel Grab off, e.g. to rest your hands near the wheel while driving with a Wheel Peripheral |
| `vrWheelGrabDiagnostics` | Report whether the vehicle has a Steering Wheel, each hand's distance to the rim ring, who is holding, the steering value sent and whether the wheel is at full lock (also in the game log) |
| `vrHandTools <on\|off>` | Hand Tools: the pressure washer lance goes into your Tool Hand (the right Motion Controller) and follows it, so it washes, and the look ray points, where the hand points; the hand's trigger washes while held, and reads as released on the pad meanwhile (the game's own use-tool binding still works) (default `on`, saved). `off` leaves every Hand Tool as the game has it, and so does `vrMotionControllers off` |
| `vrToolHand <left\|right>` | Hand Tools: which Motion Controller is your Tool Hand, the one that holds the Hand Tool (default `right`, saved). With `left`, the tool sits in the left Virtual Hand, the left trigger uses it (and reads as released on the pad), and the right hand is the one that points at Click Points; changing it while you hold a tool moves the tool to the other hand at once |
| `vrHandToolsDiagnostics` | Report the Tool Hand, the Hand Tool you hold, whether it is in the hand (and why not) and where the look ray starts (also in the game log) |

### Motion Controller buttons

The Motion Controllers' buttons, triggers, grips and thumbsticks reach the game as the Virtual Gamepad, an Xbox-style pad (`vrVirtualGamepad`, on by default). Bind it in the game's Controls screen like any gamepad, for any game action and for the VR actions (Snap turn, Recenter view, Cycle HUD, Toggle 3D VR / 2D window). Each control on each hand is its own pad input:

| Motion Controller | Pad input |
| :--- | :--- |
| Trigger (left / right) | LT / RT |
| Grip (left / right) | LB / RB |
| Thumbstick (left / right) | Left / right stick |
| Thumbstick click (left / right) | LS / RS |
| Lower / upper face button, left hand | X / Y |
| Lower / upper face button, right hand | A / B |
| Left menu button | Start |
| Right stick click held, then push the right stick | D-pad in that direction |

A right stick click let go without a push is an ordinary RS press. The menu button is on the left Motion Controller only on Touch and does not exist on Index, so Index has no Start. Out of the box Snap turn is on the right stick's left and right and Recenter view on LS (left stick click). Don't bind Recenter view to RS: the game stops firing it there.

The Companion Mod's own uses of the controllers keep working, and while one is in use that input does nothing else: the trigger clicks a Click Point under the Pointer Ray (or, in the Tool Hand holding a Hand Tool, uses the tool, and then that hand never points in the world while the other one does), the grip grabs the Steering Wheel when your hand is at its rim, and while a menu shows the pad does nothing and the Pointer Ray works the menu (trigger click, grip or lower face button for back, thumbstick to scroll). With `vrVirtualGamepad off` the Motion Controllers bind nothing, but pointing, Click Points, menus and Wheel Grab still work.

`hudConvergenceDepth` (default `1.8`) is set in `settings.xml` only.


## License

Copyright (C) 2026 spectral-byte. Licensed under the [GNU General Public License v3.0](LICENSE). Built with Khronos OpenXR, Luau, MinHook and GoogleTest; release bundles include `THIRD_PARTY_NOTICES.txt`.
The Virtual Hand glove model is "Gloved Hand" by J-Toastie ([source](https://poly.pizza/m/MTB4egc4Vh)), licensed under [Creative Commons Attribution 3.0](https://creativecommons.org/licenses/by/3.0/) (CC BY 3.0) and modified here (scaled, rigged for the game engine, mirrored for the right hand); see `mods/FS25_VRCompanion/models/VirtualHand_ATTRIBUTION.txt`.
Farming Simulator 25 is a trademark of GIANTS Software GmbH.
