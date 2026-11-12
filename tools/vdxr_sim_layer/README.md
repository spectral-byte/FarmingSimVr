# VDXR simulation harness

A regression check for the Injector's startup under VDXR (Virtual Desktop's OpenXR runtime), without a Quest or Pico.

## What it reproduces

VDXR's `xrGetSystem` creates a DXGI factory (`CreateDXGIFactory1`) before it answers. The Injector starts VR from inside the
game's first `CreateDXGIFactory1` call, so that nested call re-enters the Injector's hook while it is still starting VR.
Builds before the fix deadlocked there.

- `vdxr_sim_layer.cpp` is a test-only OpenXR API layer, `XR_APILAYER_FS25VR_vdxr_sim`. Its `xrGetSystem` calls
  `CreateDXGIFactory1` through its static `dxgi.dll` import, then passes the call on to the active runtime, as VDXR does.
  It appends its lines to `vdxr_sim_layer.log` next to the DLL, tagged with the exe name and pid.
- `harness.cpp` loads the Injector and the system's `dxgi.dll` the way the game does, checks that the Injector hooked the
  system's `CreateDXGIFactory1`, and then calls it on a worker thread, as the game's first factory call. With the layer
  enabled, the runtime's nested call happens inside that one.
- `XrApiLayer_vdxr_sim.json` is the layer's manifest.

It covers only the re-entrant `CreateDXGIFactory1`. It never creates a D3D12 device, so it does not check the order of
session and device creation.

It is not part of the CMake build and does not run in CI.

## Build

Configure the repository's CMake build first (any target; the layer needs the OpenXR headers it fetches), and build the
`injector` target for `build\version.dll`. Then, from PowerShell:

```powershell
.\tools\vdxr_sim_layer\build.ps1                  # outputs to build\vdxr_sim_layer\
.\tools\vdxr_sim_layer\build.ps1 -BuildDir <dir>  # another CMake build folder
```

It needs the MSVC x64 tools: it uses `cl` from PATH, or finds Visual Studio with `vswhere`. Everything it writes (the DLL,
the manifest copy, `harness.exe`, objects, logs and the run folders) stays in the git-ignored build folder.

## Run

```powershell
.\tools\vdxr_sim_layer\run.ps1                      # build\version.dll, both startup orders
.\tools\vdxr_sim_layer\run.ps1 -Injector <path>     # another version.dll, or a proxy dxgi.dll up to 1.1.9 (one run)
.\tools\vdxr_sim_layer\run.ps1 -BuildDir <dir>      # that folder's version.dll and harness
```

`run.ps1` enables the layer for its own shell only. For each startup order it copies the Injector into its own folder
(`build\vdxr_sim_layer\run-<order>\`, since the Injector writes `fs25_vr.log` next to itself), runs the harness, and prints
the exit code, the layer's lines for that run and the Injector's startup lines. It exits 1 if any run failed.

**A run starts an OpenXR instance on the active OpenXR runtime** (SteamVR may start its `vrserver`, for example). The
layer's `runtime returned -35` (`XR_ERROR_FORM_FACTOR_UNAVAILABLE`) only means no headset was available, and the Luau
"does not match" error in `fs25_vr.log` is expected outside the game.

### The two startup orders

- `dxgi-first`: the system's `dxgi.dll` is loaded before `version.dll`, as when the game imports it directly. The Injector
  hooks it at startup.
- `dxgi-later`: `version.dll` is loaded first and the system's `dxgi.dll` after it, as when ReShade's `dxgi.dll` loads it on
  the game's first call. The Injector hooks it from its DLL load notification.

An old proxy `dxgi.dll` (up to 1.1.9) is run once, through its own `CreateDXGIFactory1` export.

### Exit codes (harness)

| Code | Meaning |
| ---- | ------- |
| 0 | OK: startup completed. |
| 1 | Bad arguments, or a DLL failed to load. |
| 2 | Deadlock: `CreateDXGIFactory1` was still blocked after 20 s. |
| 3 | The Injector did not hook the system's `CreateDXGIFactory1`. |

## In the game

`enable.ps1` sets the User variables `XR_API_LAYER_PATH` (to `build\vdxr_sim_layer`) and `XR_ENABLE_API_LAYERS`, so
every OpenXR app started afterwards loads the layer, the game included. Exit Steam completely and start it again so the game
inherits them. `disable.ps1` clears them. Lines from the game show up in `vdxr_sim_layer.log` as
`FarmingSimulator2025Game.exe`.
