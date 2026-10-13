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
| Recenter view | `NumPad 5` | R3 (right stick click) |
| Toggle 3D VR / 2D window | `Right Ctrl + V` | L3 (left stick click) |
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
| `vrMotionControllerStatus` | Report each Motion Controller's tracked and in-use state and the Pointer Hand |
| `vrPointerDiagnostics` | Report why the Pointer Ray may not select Click Points (ray placement, who controls the pointer, Interactive Control support) |
| `vrPointerCone <degrees>` | How wide a cone around the Pointer Ray selects the nearest Interactive Control Click Point (default `5`, saved) |

### Motion Controller buttons

With Motion Controllers on, these inputs work on either hand (a push or press acts once, with the same debounce as the keys):

| Input | Action |
| :--- | :--- |
| Thumbstick left / right | Snap turn left / right (off while a menu is showing) |
| Thumbstick click | Recenter |
| Menu button | Toggle the HUD |

The menu button is on the left Motion Controller only on Touch and does not exist on Index, so the HUD toggle needs another input there. `vrMotionControllerMap` changes or removes each mapping and saves it:

| Command | Purpose |
| :--- | :--- |
| `vrMotionControllerMap` | Show the current mappings |
| `vrMotionControllerMap <snapTurn\|recenter\|hudToggle> <input>` | Put an action on an input: `thumbClick`, `buttonLower`, `buttonUpper` or `menu` (`snapTurn` only takes `thumbX`) |
| `vrMotionControllerMap <action> none` | Remove the mapping |
| `vrMotionControllerMap <action> default` / `vrMotionControllerMap reset` | Restore one / all defaults |

`hudConvergenceDepth` (default `1.8`) is set in `settings.xml` only.


## License

Copyright (C) 2026 spectral-byte. Licensed under the [GNU General Public License v3.0](LICENSE). Built with Khronos OpenXR, Luau, MinHook and GoogleTest; release bundles include `THIRD_PARTY_NOTICES.txt`.
Farming Simulator 25 is a trademark of GIANTS Software GmbH.
