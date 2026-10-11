# Unsigned Inno Setup Installer, Static-Runtime Injector, Version-Resource Identity

## Context and Decision
Players used to copy `dxgi.dll` and `FS25_VRCompanion.zip` by hand, and needed the VC++ redistributable installed. Three decisions came with the Installer (see `CONTEXT.md`):

1. **The Installer is an Inno Setup program, shipped unsigned.** One `.exe` with the standard wizard and uninstaller, built from the command line by `scripts/package_release.ps1` from the same staged files as the zip bundle. There is no code-signing certificate yet, so Windows SmartScreen shows "Windows protected your PC" and the release notes and READMEs tell players to click "More info", then "Run anyway". It requires administrator rights and is English only.
2. **The Injector is built with the static C runtime (`/MT`).** It must import no `VCRUNTIME140`, `MSVCP140` or `api-ms-win-crt-*` DLLs, so no player needs the VC++ redistributable. `CMakeLists.txt` sets `CMAKE_MSVC_RUNTIME_LIBRARY` before the dependencies are fetched, `scripts/check_injector_imports.ps1` reads the built DLL's import tables, and both packaging and CI run it.
3. **The Installer recognises its own `dxgi.dll` by the `ProductName` value, "FS25 VR", in the DLL's version resource** (`src/dxgi.rc.in`, checked by `IsOurInjector` in `installer/fs25_vr.iss`). Our DLL found in the game folder is overwritten. Any other `dxgi.dll`, such as ReShade, is moved to `dxgi.dll.fs25vr-backup` and put back on uninstall. An Injector built before the version resource existed has no `ProductName`, so it is treated as foreign: it is moved to the backup and restored on uninstall rather than overwritten.

## Status
Accepted.

## Considered Options
- **Compare the installed `dxgi.dll` with the bundled one by hash** (rejected: every release differs, so every upgrade would look foreign).
- **Sign the Installer** (deferred: needs a certificate; the SmartScreen warning is documented instead).
- **Keep the dynamic runtime and make the redistributable a prerequisite** (rejected: a missing-DLL error on first launch is the failure the Installer exists to remove).

## Consequences
- Upgrading from an Injector built before the version resource existed leaves that old Injector in `dxgi.dll.fs25vr-backup`, and uninstalling restores it. Later upgrades are clean.
- Changing `ProductName` in `src/dxgi.rc.in` makes every earlier Injector look foreign; change it together with `IsOurInjector`.
- Every dependency linked into `dxgi.dll` must use the static runtime. The OpenXR loader sets its own dynamic runtime as a static library, so `CMakeLists.txt` resets it.
