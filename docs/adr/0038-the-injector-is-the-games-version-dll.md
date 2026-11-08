# The Injector Is the Game's version.dll and Hooks the System's dxgi.dll

Amends [ADR-0024](0024-unsigned-inno-setup-installer-and-static-runtime-injector.md), item 3.

## Context and Decision
The Injector was the game's `dxgi.dll`: Windows looks in the game's `x64` folder first for a DLL the game imports, so a `dxgi.dll` there took the place of the system's and caught every `CreateDXGIFactory*` call. ReShade, and other tools that hook Direct3D, install a `dxgi.dll` in the same folder. The two replaced each other, and the Installer had to move ReShade aside to install FS25 VR.

**The Injector is the game's `version.dll` instead, and hooks the factory functions inside the system's `dxgi.dll`.**

1. **`version.dll` is a DLL the game imports that no graphics tool uses.** `FarmingSimulator2025Game.exe` imports `VERSION.dll`, which is not one of the system's KnownDLLs, so `x64\version.dll` is loaded at startup, before the game creates a DXGI factory. The Injector exports the 17 functions of the system's `version.dll` under the same names and ordinals. Each one calls the system's, which is loaded from the system directory on first use. `VerLanguageNameA` and `VerLanguageNameW` are forwarded to kernel32, as the system's `version.dll` forwards them. The Injector must never import `version.dll` itself, since that import would bind to the Injector. `scripts/check_injector_imports.ps1` checks this.
2. **The Injector hooks `CreateDXGIFactory`, `CreateDXGIFactory1` and `CreateDXGIFactory2` in the system's `dxgi.dll`** (`DxgiExportHooks`). Every factory created in the process goes through `DxgiProxy`, whichever module asks for it: the game, a `dxgi.dll` in the game folder such as ReShade's, or the OpenXR runtime. While the proxy handles a call on a thread, a call on that thread to one of the three goes straight to the system. That covers the proxy passing the call on, the OpenXR runtime creating a factory while VR starts (VDXR does), and `dxgi.dll` calling its own export.
3. **The three hooks are installed from `DllMain`** (`StartInjector`). This is the one exception to "no hooks from `DllMain`". There is no later moment that is sure to come before the game's first factory call. Patching takes no lock the loader holds.
   - When the system's `dxgi.dll` is already loaded, the hooks go in right away. That is the case when the game imports it with no `dxgi.dll` in its folder.
   - Otherwise an ntdll DLL load notification hooks it the moment it loads. A `dxgi.dll` in the game folder, such as ReShade's, loads it on the game's first call.
   - Either way, the system's `dxgi.dll` is pinned so it can never unload from under the hooks.
4. **The proxy hooks the system's factory behind a wrapper.** ReShade hands the game a factory and swap chain of its own that wrap the system's. Hooking the wrapper's methods would put the Present hook outside ReShade: the eye copy would run before its effects, and with the Desktop Mirror Window off ReShade would see no Present at all. So when a factory's methods are not in the system's `dxgi.dll`, `AttachFactory` gets the system's factory through an adapter's parent and hooks that. ReShade runs first, its effects reach the eye images, and the swap chain, queue and device the proxy sees are the system's.
5. **An Injector of an earlier version still in the game folder keeps this one inactive.** If the game's `dxgi.dll` is an FS25 VR Injector (by its version resource), both would hook the game. The new Injector logs that the old file must be deleted, and stays out of the way.
   - An Installer upgrade deletes that `dxgi.dll` once the new files are placed, and puts back the `dxgi.dll` it had moved aside (ReShade's, for one).
   - A `dxgi.dll` that is not ours is never touched. The uninstall also sets it aside before Inno removes files, since an upgraded install's file list still names `x64\dxgi.dll`.
   - A foreign `version.dll` is moved to `version.dll.fs25vr-backup` and put back on uninstall, as a foreign `dxgi.dll` was.

## Status
Accepted.

## Considered Options
- **Stay `dxgi.dll` and load ReShade renamed to `ReShade64.dll`** (rejected: players would rename ReShade's file by hand, ReShade's own setup puts `dxgi.dll` back on every update, and every other tool that is a `dxgi.dll` still collides).
- **A small `version.dll` that loads an `FSVR.dll`** (rejected: it occupies the same name, so it collides just as often, and adds a second file to install).
- **Another DLL the game imports:**
  - `winmm.dll`, `dinput8.dll`, `xinput1_4.dll` or `hid.dll` (rejected: controller and wheel tools and ASI loaders use them, and `winmm.dll` has about 180 exports).
  - `d3d12.dll` (rejected: ReShade's other name, and the Agility SDK exports make it hard to stand in for).
  - `bcrypt.dll`, `crypt32.dll` or `iphlpapi.dll` (rejected: they would put the Injector in the path of encryption and network code).
- **A launcher that injects the Injector, or patching the game's exe to import it** (rejected: antivirus flags injectors, and a game update or a Steam file check undoes a patched exe).
- **Install the hooks outside the loader lock, from a thread started in `DllMain` or from a hook on one of the exe's imports** (rejected: the thread races the game's first factory call, and an import hook only sees the exe's own calls, not ReShade's or the runtime's).

## Consequences
- ReShade, and other `dxgi.dll` tools, stay installed alongside FS25 VR and keep their normal setup and updates.
- With ReShade, its effects are drawn into each eye's image. It runs once per eye Present, so its GPU cost doubles under [ADR-0015](0015-sequential-dual-pass-stereo-presentation-pipeline.md), and effects that build up over frames alternate between the eyes. How depth-based effects and ReShade's overlay look in the headset has yet to be checked.
- A tool that is a `version.dll` (some ASI loaders) now collides instead. The Installer moves it aside and puts it back.
- Crash dumps and logs name the Injector `version.dll`.
- Players who installed by hand must delete the old `dxgi.dll` once. The Injector's log says so.
