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
- **Hand Tools** (on foot): the pressure washer lance, the flashlight, the chainsaw, the spray can, the horse brush and the fishing rod (Highlands Fishing DLC) sit in your Tool Hand, the right Motion Controller. Switch it off with `vrHandTools off`.
- **Animated Objects** (on foot): standing in the zone around a building's door or gate, or a barrier placed on the map, point the Pointer Ray at one of its moving parts (or touch it up close) and pull the trigger: it opens, closes, or turns back while it moves.

## Installation

**Requirements**: Windows 10/11 (64-bit), Farming Simulator 25 (Steam, Epic or GIANTS standalone), and an OpenXR-capable PC VR headset.

### Installer (recommended)

1. Download `FS25_VR_v<version>_Setup.exe` from the [latest release](https://github.com/spectral-byte/FarmingSimVr/releases) and run it. It needs administrator rights.
2. Pick your game installation. If it isn't found, use **Browse...** to select the folder that contains `x64\FarmingSimulator2025Game.exe`.
3. Optional: tick **Render Resolution** to set the game to your headset's per-eye resolution (windowed, 640 to 8192). Your `game.xml` is backed up first. If you've never launched the game, launch it once and run the Installer again.
4. Finish the wizard. The game must be closed.

The Installer copies `dxgi.dll` to the game's `x64` folder and `FS25_VRCompanion.zip` (not extracted) to your `mods` folder. An existing `dxgi.dll` such as ReShade is moved to `dxgi.dll.fs25vr-backup`.

> [!NOTE]
> The Installer isn't code-signed, so Windows SmartScreen will warn you. Click **More info**, then **Run anyway**.

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
| Set VR seat position | `Right Ctrl + NumPad 5` | none; bind a pad input in the Controls screen |
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
| `vrDesktopMirror [on\|off]` | Turn the Desktop Mirror Window on or off while the headset is in use, to give the GPU time it costs to the headset. Nothing changes in the headset. It shows by itself while the headset is asleep or on the dashboard and before a savegame has loaded. Saved, on by default. Untested with frame generation: turn the mirror back on if something breaks with it. No arguments reports the current state |
| `vrEyePairTrace [frames]` | Log how far the vehicle and the eye camera move between rendered eye frames (default 32 frames) |
| `vrSnapTurnAngle <degrees>` | Snap turn step (default `90`) |
| `vrHudGroup <map\|speedometer\|info\|controls> <horizontal> <vertical> [scale]` | Place and scale a HUD group |
| `vrHudGroupShow <group> <streamlined\|full> <on\|off>` | Choose which groups show in each HUD state |
| `vrHudSafeArea <horizontal> <vertical>` | Keep other HUD elements (warnings, notifications, other mods) inside the lens |
| `vrSeatOffset [<forward> <up> <right> \| reset]` | Move where your head rests in the cab you are in, in metres from the vehicle's own eye point (each axis up to 0.5 either way; saved, for that vehicle model and camera). Without arguments it prints the current offset |
| `vrMotionControllers <on\|off>` | Use Motion Controllers (default `on`, saved). `off` makes the mod ignore Motion Controller input |
| `vrVirtualGamepad <on\|off>` | The Motion Controllers' triggers, grips, thumbsticks and buttons as an Xbox pad in the game, bound in the game's Controls screen (default `on`, saved). Appears in the highest XInput slot no real pad occupies, `off` removes it from the game, either way without a restart, and it reads neutral while `vrMotionControllers` is `off`. |
| `vrWheelGrab <on\|off>` | Wheel Grab: steer by squeezing the grip with a hand at the Steering Wheel's rim and turning it, in the in-cab camera (default `on`, saved). `off` switches only Wheel Grab off, e.g. to rest your hands near the wheel while driving with a Wheel Peripheral |
| `vrHandTools <on\|off>` | Hand Tools: the pressure washer lance, the flashlight, the chainsaw, the spray can, the horse brush and the fishing rod go into your Tool Hand (the right Motion Controller) and follow it, so the lance washes, the flashlight shines, the chainsaw cuts, the spray can marks, the horse brush cleans and the rod casts where the hand points it (default `on`, saved). `off` leaves every Hand Tool as the game has it, and so does `vrMotionControllers off` |
| `vrToolHand <left\|right>` | Hand Tools: which Motion Controller is your Tool Hand, the one that holds the Hand Tool (default `right`, saved). With `left`, the tool sits in the left Virtual Hand, the left trigger uses it (and reads as released on the pad), and the right hand is the one that points at Click Points |
| `vrObjectGrab <on\|off>` | Object Grab: on foot with empty hands, the Pointer Hand aims what the game targets by looking (the Pointer Ray lands on an object the hands can pick up, squeezing its grip there picks the object up, and the hand carries it as you hold the grip, with the angle it had when you grabbed it (the game's rotate and level actions still turn it); letting go of the grip drops it, or throws it along the way the hand is moving if the hand is moving fast enough (default `on`, saved). The game's own mass limit, pickup distance and super strength apply and `off` leaves picking up as the game has it, and so does `vrMotionControllers off` |
| `vrFishCatch [ask\|sell\|release]` | What happens when you catch a fish with the fishing rod (Highlands Fishing DLC). `ask` (the default) lets the game's sell-or-release dialog show. `sell` or `release` answers it for you the moment it opens and shows a short message instead: "Sold trout for 100 $" or "Released trout" (just "Fish sold" or "Fish released" if the dialog's text is not the English one). No arguments reports the current choice. Saved |

### Motion Controller buttons

The Motion Controllers' buttons, triggers, grips and thumbsticks reach the game as the Virtual Gamepad, an Xbox-style pad (`vrVirtualGamepad`, on by default). Bind it in the game's Controls screen like any gamepad, for any game action and for the VR actions (Snap turn, Recenter view, Cycle HUD, Toggle 3D VR / 2D window). Each control on each hand is its own pad input:

## License

Copyright (C) 2026 spectral-byte. Licensed under the [GNU General Public License v3.0](LICENSE). Built with Khronos OpenXR, Luau, MinHook and GoogleTest; release bundles include `THIRD_PARTY_NOTICES.txt`.
The Virtual Hand glove model is "Gloved Hand" by J-Toastie ([source](https://poly.pizza/m/MTB4egc4Vh)), licensed under [Creative Commons Attribution 3.0](https://creativecommons.org/licenses/by/3.0/) (CC BY 3.0) and modified here (scaled, rigged for the game engine, mirrored for the right hand); see `mods/FS25_VRCompanion/models/VirtualHand_ATTRIBUTION.txt`.
Farming Simulator 25 is a trademark of GIANTS Software GmbH.
