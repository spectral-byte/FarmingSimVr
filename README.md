# Farming Simulator 25 VR (FS25 VR)

[![CI](https://github.com/jacoch17/FarmingSimVr/actions/workflows/ci.yml/badge.svg)](https://github.com/jacoch17/FarmingSimVr/actions/workflows/ci.yml)
[![OpenXR](https://img.shields.io/badge/OpenXR-1.0-blue.svg)]()
[![DirectX](https://img.shields.io/badge/Direct3D-12-orange.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)]()
[![Release](https://img.shields.io/badge/release-v1.0.0-success.svg)](https://github.com/jacoch17/FarmingSimVr/releases)

High-performance virtual reality modding architecture for **Farming Simulator 25** running on GIANTS Engine 10 under Direct3D 12. Seamlessly interfaces GIANTS Engine 10 with any standard OpenXR runtime (Meta Quest Link/AirLink, Virtual Desktop, SteamVR, Pimax, Bigscreen Beyond).

---

## Architecture Overview

FS25 VR is built as a hybrid two-tier architecture:

1. **Native Direct3D 12 Proxy Injector (`dxgi.dll`)**:
   - Intercepts DXGI swapchain presentation and hooks into the DirectX 12 rendering pipeline.
   - Initializes and manages the OpenXR session lifecycle, reference spaces, swapchain images, and compositor submission.
   - Virtualizes temporal upscalers (NVIDIA DLSS / NGX) with dual feature contexts to completely eliminate stereoscopic history ghosting.
   - Provides an OpenXR floating quad layer for 2D UI menus and dialogs with hardware-accurate alpha-blended mouse cursor injection.
   - Automatically decouples desktop mirror window rendering from native headset display resolution.
   - Intercepts the in-process Luau VM state and injects the global `VRBridge` state and synchronization table.

2. **In-Engine Luau Companion Mod (`FS25_VRCompanion.zip`)**:
   - Executes directly inside GIANTS Engine 10's Luau environment.
   - Consumes 6DOF head poses and asymmetric perspective projections from `VRBridge`.
   - Manages dual eye camera nodes and cyclopean spatial audio listener alignment.
   - Coordinates Sequential Dual-Pass (SDP) and Alternate Frame Rendering (AFR) pipelines.
   - Features comprehensive VR HUD anchoring with configurable insets and IPD-scaled stereoscopic convergence depth.
   - Implements in-engine snap turn locomotion, 2D floating window view toggling, and reference space recentering.

```
       +-------------------------------------------------------------+
       |                  GIANTS Engine 10 (FS25)                    |
       |                                                             |
       |  +------------------------+      +-----------------------+  |
       |  |  FS25_VRCompanion.zip  |      |   Direct3D 12 Engine  |  |
       |  |   (In-Engine Luau)     |      |      Render Loop      |  |
       |  +-----------+------------+      +-----------+-----------+  |
       +--------------|-------------------------------|--------------+
                      |                               |
          VRBridge    | Interception                  | DXGI Present
          State Table |                               | Interception
                      v                               v
       +-------------------------------------------------------------+
       |                      dxgi.dll (Proxy)                       |
       |                                                             |
       |  +-------------------+  +------------------+  +----------+  |
       |  | Luau Interceptor  |  | Virtual NGX DLSS |  | D3D12    |  |
       |  | & VRBridge Sync   |  | Dual Context     |  | Pipeline |  |
       |  +-------------------+  +------------------+  +----+-----+  |
       +----------------------------------------------------|--------+
                                                            |
                                               xrEndFrame   v
                                             +-----------------------+
                                             |     OpenXR Runtime    |
                                             |  (Quest / SteamVR)    |
                                             +-----------------------+
```

---

## Key Features

- **Stereo Presentation Modes**:
  - **Sequential Dual-Pass (SDP)**: Renders left and right eye perspectives sequentially within a single logical compositor frame. The second sub-pass clamps the Lua simulation delta time to zero ($\Delta t = 0$), so both eyes show the same game state, and which eye renders first alternates every four pairs so each eye's DLSS history gets the full jitter pattern. Use DLSS/DLAA (see the support matrix below). Default mode; recommended for high-end systems (RTX 3080/4080/4090).
  - **Alternate Frame Rendering (AFR)**: Alternates left and right eye perspectives on sequential engine frames for lightweight GPU load on midrange graphics cards. Opt-in via `vrRenderingMode afr`.
  - Dynamically switchable via console command or configuration settings with seamless discontinuity flushing.
  - **Upscaler support per Stereo Presentation Mode**:

    | Upscaler | Sequential Dual-Pass | Alternate Frame Rendering |
    |---|---|---|
    | DLSS / DLAA | Supported. Per-eye history with per-eye motion vector correction (`vrDlssMvFix`, ADR-0020): sharp, no ghosting. The eye rendered in the second sub-pass may smear slightly on moving objects. | Supported. Per-eye history with the same motion vector correction (`vrDlssMvFix`); object motion is scaled by the two-frame stride. `vrDlssMvScale` (2.0x default) applies only while the correction is off. |
    | FSR3 | Supported. Per-eye history with the same per-eye motion vector correction as DLSS (`vrDlssMvFix`, ADR-0020), using the conventions in FSR3's own dispatch description: sharp, no ghosting once the correction is on. The second-sub-pass object-motion caveat applies. | Per-eye history with the same motion vector correction (`vrDlssMvFix`); object motion is scaled by the two-frame stride. |
    | In-engine TAA | Not supported in VR: one history is shared across eyes and ghosts. Use DLSS/DLAA or FSR3. | Not supported in VR (ghosts). |

    `vrDlssMvFix on|off` switches the per-eye motion vector correction for DLSS and FSR3 (the `vrDlssMv*` names cover both upscalers; `vrDlssMvScale` is DLSS only) (off by default until verified in game; with it off, FSR3 under Sequential Dual-Pass has per-eye history but soft fine texture). `vrDlssDepthModel infinite|finite` (default infinite) picks the depth model the DLSS correction reads the engine depth with (FSR3 reads its own create flags). `vrDlssDiag` also logs a one-time `[Diag] MvProjection` line with the projection fed to the correction (FSR3 shows the dispatch fields next to the camera record), and logs FSR3 dispatches and, while the correction is on, the motion vector self-check for them.
- **6DOF Head Tracking**: Positional and rotational tracking for both on-foot locomotion and vehicle cockpits with collinear translation and decoupled look yaw rotation.
- **Symmetric Projection (default) with opt-in Oblique Frustum**: The Injector submits an aspect-matched symmetric FOV and the Companion Mod applies no projection offset by default. Per-eye asymmetric offsets computed from headset optics are available via `vrObliqueProjection true` or `<allowObliqueProjection>true</allowObliqueProjection>` in `settings.xml` (see ADR-0005).
- **Temporal Upscaler Virtualization**:
  - Isolated left and right `NVSDK_NGX_Handle` instances.
  - Context-aware motion vector handling ($1.0\times$ in Dual-Pass; in AFR, the per-eye correction, or $2.0\times$ scaling while it is off).
  - Synchronous discontinuity reset on teleports, snap turns, and view transitions.
- **Dynamic In-Engine VR HUD**:
  - Normalized viewport insets contract interface elements into the headset's optical sweet spot.
  - Physical IPD-scaled stereoscopic convergence depth prevents eye strain.
  - Clean vertical stacking of fill levels and machine telemetry.
  - Three-stage HUD display cycle: **Full HUD** $\rightarrow$ **Streamlined Telemetry** $\rightarrow$ **Hidden**.
- **Floating Quad Layer for Menus**:
  - 2D menus, title screen, and map dialogs render cleanly onto a floating 3D quad layer in OpenXR space.
  - Alpha-blended hardware mouse cursor projected accurately onto the floating quad.
  - Dynamic quad centering and 6DOF background showroom tracking.
- **In-Engine 2D View Toggle**: Instant hotkey toggle between full 3D VR and a 2D floating virtual screen for comfortable cutscene or menu viewing.
- **Snap Turn Locomotion**: Instant rotational increments (configurable 30Â°, 45Â°, 90Â°) for both on-foot walking and in-cab vehicle driving.
- **Desktop Mirror Window Decoupling**: Renders at native headset resolution while maintaining an unscaled, clamped desktop mirror window without aspect ratio distortion.

---

## Installation Guide

### System Requirements

- **Operating System**: Windows 10 or 11 (64-bit)
- **Game**: Farming Simulator 25 (Direct3D 12)
- **VR Headset**: Any PC VR headset with OpenXR support:
  - Meta Quest 2 / 3 / Pro (via Quest Link, AirLink, or Virtual Desktop)
  - Valve Index, HTC Vive, Bigscreen Beyond (via SteamVR)
  - Pimax headsets (via PimaxXR / OpenXR runtime)
- **Software Prerequisites**: Microsoft Visual C++ 2015â€“2022 Redistributable (x64)

### Step 1: Install the Native Injector (`dxgi.dll`)

1. Locate your Farming Simulator 25 installation folder:
   - **Steam**: `C:\Program Files (x86)\Steam\steamapps\common\Farming Simulator 25\`
   - **GIANTS Standalone**: `C:\Program Files\Farming Simulator 2025\`
   - **Epic Games**: `C:\Program Files\Epic Games\FarmingSimulator25\`
2. Open the **`x64`** subdirectory (where `FarmingSimulator2025Game.exe` is located).
3. Copy **`dxgi.dll`** into this **`x64`** folder.

> [!NOTE]
> `dxgi.dll` acts as a DirectX 12 proxy library. To uninstall or disable VR, simply rename or delete `dxgi.dll` from the `x64` folder.

### Step 2: Install the Companion Mod (`FS25_VRCompanion.zip`)

1. Locate your Farming Simulator 25 mods directory:
   - `Documents\My Games\FarmingSimulator2025\mods\`
2. Copy **`FS25_VRCompanion.zip`** into this `mods` directory.
   - **Do NOT unzip this file!** Farming Simulator reads mod archives directly in `.zip` format.

### Step 3: Launch the Game

1. Connect your VR headset and verify your OpenXR runtime is running (e.g. Meta Quest Link app, SteamVR, or Virtual Desktop).
2. Launch Farming Simulator 25 normally (e.g. from Steam or desktop shortcut).
3. When loading or starting a Career / Savegame, make sure **FS25 VR Companion Mod** is checked / enabled in the Mod Selection list.
4. Put on your headset and enter the virtual farm!

---

## Controls and Keybindings

| Action | Keyboard Binding | Gamepad Binding | Description |
| :--- | :--- | :--- | :--- |
| **Recenter VR View** | `NumPad 5` | `R3 / Right Stick Click (Button 11)` | Re-zeros head translation and tracking space |
| **Toggle VR View Mode** | `Right Ctrl + V` or `NumPad 0` | `L3 / Left Stick Click (Button 9)` | Toggles between 3D VR and 2D Floating Window |
| **Toggle VR HUD Mode** | `Right Ctrl + H` | `Button 10` | Cycles Full $\rightarrow$ Streamlined $\rightarrow$ Hidden HUD |
| **VR Snap Turn Left** | `NumPad 4` | `LB / Left Bumper (Button 5)` | Instant left rotation increment |
| **VR Snap Turn Right** | `NumPad 6` | `RB / Right Bumper (Button 6)` | Instant right rotation increment |

> All keybindings can also be customized in the in-game Controls Options menu under the **FS25 VR** category.

---

## Settings & Console Commands

VR settings are automatically persisted to:  
`Documents\My Games\FarmingSimulator2025\modSettings\FS25_VRCompanion\settings.xml`

### In-Game Console Commands

Press the tilde key (`` ` `` / `~`) to open the in-engine developer console:

- `vrRenderingMode <afr|dualpass|toggle>`: Switch between Sequential Dual-Pass and Alternate Frame Rendering.
- `vrObliqueProjection <true|false|toggle>`: Enable per-eye Oblique Frustum projection (default: `false`, symmetric).
- `vrHudInsets <horizontal> <vertical> [scale] [top]`: Adjust normalized HUD margin insets and element scale (default: `0.30 0.22 0.82 0.22`).
- HUD convergence depth is set via `<hudConvergenceDepth>` in `settings.xml` (default: `1.8`).
- `vrSnapTurnAngle <degrees>`: Set snap turn rotational angle step (default: `90`).

---

## Building from Source

### Prerequisites

- Windows 10/11 64-bit
- Visual Studio 2022 or Build Tools with C++20 standard library and MSVC v143+
- CMake 3.20+
- Ninja build system
- Git

### Build Instructions

```powershell
# Clone repository
git clone https://github.com/jacoch17/FarmingSimVr.git
cd FarmingSimVr

# Configure CMake with Ninja. RelWithDebInfo is the shipped configuration: full
# inlining (/Ob2, set in CMakeLists.txt) plus a dxgi.pdb for symbols.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build Injector DLL and Test Executable
ninja -C build dxgi fs25_vr_tests

# Confirm the build tree uses the shipped configuration (also run by CI and the packaging script)
.\scripts\check_build_config.ps1 build

# Run automated unit and integration tests
ctest --test-dir build --output-on-failure
```

### Packaging and Deploying the Companion Mod

```powershell
# Using tar.exe to preserve POSIX directory entries required by GIANTS Engine
cd mods\FS25_VRCompanion
tar.exe -a -cf ..\..\build\FS25_VRCompanion.zip modDesc.xml icon_vr.png src/*.lua

# Or use the automated deployment script to build the zip and deploy directly to game directories:
.\scripts\deploy.ps1
```

---

## Architecture Decision Records (ADRs)

For in-depth technical documentation, refer to the architecture records in [`docs/adr/`](docs/adr/):

- [`ADR-0001`](docs/adr/0001-hybrid-injector-and-companion-mod-architecture.md): Hybrid Injector and In-Engine Companion Mod via VRBridge
- [`ADR-0002`](docs/adr/0002-in-process-luau-state-interception.md): In-Process Luau State Interception & VRBridge Table Injection
- [`ADR-0003`](docs/adr/0003-afr-frame-pacing-and-telemetry-probe.md): Alternate Frame Rendering (AFR) Pacing & Telemetry Probe (probe superseded by ADR-0017)
- [`ADR-0004`](docs/adr/0004-graceful-openxr-pass-through-fallback.md): Graceful OpenXR Pass-Through Fallback
- [`ADR-0005`](docs/adr/0005-oblique-frustum-projection-via-vrbridge.md): Oblique Frustum Projection via VRBridge
- [`ADR-0006`](docs/adr/0006-openxr-floating-quad-layer-for-menus.md): OpenXR Floating Quad Layer for Menus
- [`ADR-0007`](docs/adr/0007-in-game-gui-menu-detection-and-quad-layer-switching.md): In-Game GUI Menu Detection and Quad Layer Switching
- [`ADR-0008`](docs/adr/0008-dynamic-quad-centering-and-background-camera-tracking.md): Dynamic Quad Centering and Background 6DOF Camera Tracking
- [`ADR-0009`](docs/adr/0009-openxr-alpha-blended-mouse-cursor-layer.md): OpenXR Alpha-Blended Mouse Cursor Quad Layer for Menus
- [`ADR-0010`](docs/adr/0010-in-engine-vr-hud-inset-and-convergence-parity.md): In-Engine VR HUD Inset and Convergence Parity
- [`ADR-0011`](docs/adr/0011-openxr-reference-space-recentering.md): OpenXR Reference Space Recentering via poseInReferenceSpace
- [`ADR-0012`](docs/adr/0012-in-engine-snap-turn-locomotion.md): In-Engine Snap Turn Locomotion
- [`ADR-0013`](docs/adr/0013-in-engine-2d-floating-window-view-toggle.md): In-Engine 2D Floating Window View Toggle
- [`ADR-0014`](docs/adr/0014-desktop-mirror-window-resolution-decoupling.md): Desktop Mirror Window Resolution Decoupling
- [`ADR-0015`](docs/adr/0015-sequential-dual-pass-stereo-presentation-pipeline.md): Sequential Dual-Pass Stereo Presentation Pipeline
- [`ADR-0016`](docs/adr/0016-dual-feature-context-virtualization-for-temporal-upscalers.md): Dual Feature Context Virtualization for Temporal Upscalers
- [`ADR-0017`](docs/adr/0017-retire-the-d3d12-telemetry-probe.md): Retire the D3D12 Telemetry Probe
- [`ADR-0018`](docs/adr/0018-lua-and-present-share-one-thread.md): Companion Mod Calls and Present Run on One Thread
- [`ADR-0019`](docs/adr/0019-begin-the-openxr-frame-before-the-engine-update.md): Begin the OpenXR Frame Before the Engine Update
- [`ADR-0020`](docs/adr/0020-per-eye-motion-vector-correction-for-temporal-upscalers.md): Per-Eye Motion Vector Correction for Temporal Upscalers

---

## License & Credits

Copyright (C) 2026 TBD.  
Developed by the **FS25 VR Team** and licensed under the [GNU General Public License v3.0](LICENSE). Built with Khronos OpenXR, Luau, MinHook, and GoogleTest.  
Release bundles include `THIRD_PARTY_NOTICES.txt` with the licenses of the libraries linked into `dxgi.dll`.  
Farming Simulator 25 is a trademark of GIANTS Software GmbH.
