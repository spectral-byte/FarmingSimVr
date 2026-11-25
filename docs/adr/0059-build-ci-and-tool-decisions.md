# Decisions behind the Build, CI, Release Scripts and Tools

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0024](0024-unsigned-inno-setup-installer-and-static-runtime-injector.md).

## Context and Decision
This record holds the non-obvious decisions behind the build, CI and release scripts, and the VDXR simulation layer. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `.github/workflows/build-and-test.yml`

- **`on:`**: The steps CI runs on every change and a release runs on a tag, defined once so the two cannot drift apart. Called by ci.yml and release.yml; it only reads the repository, whoever calls it.
- **`Check pins`**: Fails when a fetched dependency, an Action or the Inno Setup install is not pinned to fixed content; its own test proves, on scratch copies, that it can fail.
- **`Set up MSVC developer environment`**: The same script a local build runs (see "Building from source" in the README); under GitHub Actions it also exports the environment to the later steps. Done by a script of our own because ilammy/msvc-dev-cmd has no release off deprecated Node 20.
- **`Cache fetched dependencies`**: Fetched dependencies (OpenXR-SDK, GoogleTest, Luau, MinHook) live outside the build tree, in .deps (FETCHCONTENT_BASE_DIR in CMakePresets.json; change the two together), so they can be cached across runs. Only the sources and the FetchContent stamps are cached; the \*-build trees are rebuilt anyway. Keyed on CMakeLists.txt, where the pinned dependency commits are declared.
- **`Verify full inlining`**: The shipped configuration compiles with full inlining (/Ob2); fail if the cache says otherwise (e.g. CMake's default /Ob1 crept back in), or if the ci preset lost the warnings-as-errors option.
- **`Verify Injector imports`**: The Injector is built against the static C runtime; fail if the built version.dll imports VCRUNTIME140/MSVCP140/api-ms-win-crt-\* (players would need the VC++ redistributable), or version.dll (the game loads the Injector as its version.dll).
- **`Test (single process)`**: ctest runs each test in its own process; a single-process run catches state leaking between tests through process-wide state (the hook registries and InjectorRuntime::Instance()).
- **`Install Inno Setup`**: Build the Installer exactly as a release does (the zip bundle and the Setup exe from the same staged files), then drive it silently against a fake game folder. At a stated version, so a new Inno Setup release cannot change the Installer unnoticed; upgrade with --allow-downgrade also replaces a different version the runner image ships.
- **`Check the symbols zip`**: The release also carries the Injector's PDB as a symbols zip (so a crash dump from any release opens with symbols): it must exist, hold the PDB the linker wrote, and that PDB must belong to the Injector the bundle ships (same GUID and age). The packaging test then proves, in a scratch copy, that packaging fails without the PDB and that this check can fail.
- **`Test the release tag check`**: The release workflow runs scripts/check_release_tag.ps1 on a tag, which a CI run never has; its own test keeps that rule exercised on every change.

### `.github/workflows/ci.yml`

- **`concurrency:`**: Superseded PR pushes are cancelled; every push to main or beta keeps its own result.

### `.github/workflows/release.yml`

- **`on:`**: Pushing a version tag (v1.2.0, or v1.2.0-pre.1 for a pre-release) builds and tests exactly as CI does and then creates a DRAFT GitHub release holding the zip bundle, the Setup exe and the symbols zip. The workflow never publishes: read the draft, write the notes and publish it by hand.
- **`permissions:`**: The only write access to the repository in any workflow.
- **`Name the assets for the release`**: Packaging names the assets after the Companion Mod's four-part version (FS25_VR_v1.2.0.0.zip); releases drop its last .0 (FS25_VR_v1.2.0.zip), which is what the tag names.

### `.gitignore`

- **`/dataS/`**: Proprietary / third-party reference scripts (keep local copies only, never commit)

### `CMakeLists.txt`

- **`if(MSVC)`**: Shipped configuration: RelWithDebInfo with full inlining (/Ob2) and a PDB. CMake's MSVC default is /Ob1, which only inlines functions marked inline. Set before FetchContent so the statically linked dependencies (Luau, MinHook, OpenXR) match. CI and scripts/package_release.ps1 verify this value in CMakeCache.txt.
- **`if(MSVC)`**: Static MSVC runtime (/MT): the Injector must not import VCRUNTIME140.dll, MSVCP140.dll or the api-ms-win-crt-\* DLLs, so players need no VC++ redistributable. Set before FetchContent so the statically linked dependencies and the test executable use the same runtime as the Injector. CI verifies the built version.dll with scripts/check_injector_imports.ps1.
- **`set(DYNAMIC_LOADER OFF CACHE BOOL "Build static OpenXR loader" FORCE)`**: Every fetched dependency is pinned to a full commit hash with its readable version in a comment, so its sources cannot change under a moved tag; scripts/check_pins.ps1 enforces it. CMake can only shallow clone a branch or a tag, so these are full clones (CI caches them). To bump one, resolve the new tag with `gh api repos/<owner>/<repo>/commits/<tag> --jq .sha` and update both.
- **`if(MSVC)`**: As a static library the OpenXR loader sets its own MSVC_RUNTIME_LIBRARY to the dynamic CRT, which overrides CMAKE_MSVC_RUNTIME_LIBRARY above; set it back to the Injector's static runtime.
- **`set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)`**: GoogleTest must follow the static runtime chosen above (the test executable and the Injector agree).
- **`set(FS25VR_LUAU_TAG 0.662)`**: Must match the Luau FS25 embeds; bumping it also means updating the host fingerprint (ADR-0002 amendment) and GIT_TAG below to the commit this tag names. FS25VR_LUAU_TAG reaches the test that compares it with kExpectedLuauVersion.
- **`set(FS25VR_MODDESC "${CMAKE_CURRENT_SOURCE_DIR}/mods/FS25_VRCompanion/modDesc...`**: Version resource of the Injector: "FS25 VR" product identity (the Installer uses it to tell our version.dll from a foreign one, and an earlier dxgi.dll of ours) and the Companion Mod's version from modDesc.xml.
- **`target_compile_definitions(fs25_vr_core PUBLIC FS25VR_VERSION="${FS25VR_VERSI...`**: The same number reaches the core library (src/core/injector_version.h), which the Injector and the tests share: VRBridge reports it and the Injector's first log line names it.
- **`option(FS25VR_WARNINGS_AS_ERRORS "Treat compiler warnings in the project's ow...`**: Warnings as errors for the project's own targets only (never the fetched dependencies, which share the /W4 above but are not ours to fix). Off by default so a newer compiler cannot block a local build; CI turns it on and scripts/check_build_config.ps1 -RequireWarningsAsErrors verifies it.

### `tools/vdxr_sim_layer/harness.cpp`

- **the file**:

  ```text
  Imitates the game's startup with an FS25 VR Injector, then calls CreateDXGIFactory1 on a worker thread, as the game's
  first factory call. With the VDXR simulation layer enabled, the OpenXR runtime calls CreateDXGIFactory1 again from inside
  xrGetSystem while the Injector is still starting VR.

    harness <Injector version.dll> [dxgi-first | dxgi-later]
      The Injector from 1.2 (ADR-0038). It hooks CreateDXGIFactory/1/2 in the system's dxgi.dll, and the harness calls the
      system's export, as the game does.
      dxgi-first (default): the system's dxgi.dll is loaded before version.dll, as when the game imports it directly. The
                            Injector hooks it at startup.
      dxgi-later:           version.dll first, then the system's dxgi.dll, as when ReShade's dxgi.dll loads it on the
                            game's first call. The Injector hooks it from its DLL load notification.
    harness <proxy dxgi.dll>
      An Injector up to 1.1.9, the game's dxgi.dll: the harness calls the proxy's own CreateDXGIFactory1 export.

  Exit code 0 = startup completed, 2 = still blocked after 20 s (deadlock), 3 = the Injector did not hook the system's
  CreateDXGIFactory1, 1 = bad arguments or a load failure.
  ```

- **`ReadFileBytesAtRva`**: The first bytes at `rva` in the system dxgi.dll file on disk, through its section table. (LoadLibraryEx as a data file or image resource hands back the loaded module, hooked, when it is already loaded.)

### `tools/vdxr_sim_layer/vdxr_sim_layer.cpp`

- **the file**: Test-only OpenXR API layer that imitates VDXR's xrGetSystem: it creates a DXGI factory through its static dxgi.dll import before passing the call on to the real runtime. Loaded into the game, that call re-enters the FS25 VR Injector's CreateDXGIFactory1 hook (up to 1.1.9: the dxgi.dll proxy) while it is still starting VR, the same path that deadlocked under VDXR.
- **`Log`**: Each line is flushed, so a hang inside CreateDXGIFactory1 still shows the "calling" line. The log is appended to and shared, so every process that loads the layer keeps its lines.

### `scripts/check_build_config.ps1`

- **the file**: Verifies that a CMake build tree uses the shipped configuration: RelWithDebInfo with full inlining (/Ob2) and debug info (/Zi). With -RequireWarningsAsErrors it also requires the FS25VR_WARNINGS_AS_ERRORS option to be on (the ci preset sets it, so a CI configure that loses the option cannot go unnoticed).

### `scripts/check_injector_imports.ps1`

- **the file**: Verifies that the built Injector does not import the Microsoft C/C++ runtime DLLs (VCRUNTIME140\*.dll, MSVCP140\*.dll, CONCRT140.dll, api-ms-win-crt-\*.dll), i.e. that it is built against the static runtime and needs no VC++ redistributable on the player's machine, and that it does not import version.dll: the game loads the Injector as its version.dll (ADR-0038), so that import would bind to the Injector itself. It does not import dbghelp.dll either: the crash reporter loads the system's dbghelp.dll by its full path at the first DXGI factory call, and an import would load it (or a dbghelp.dll next to the game's exe) from the loader, at process start, before the Injector ran a line. Reads the PE import tables (regular and delay-load) directly, so it needs no Visual Studio tools and checks the built artifact rather than a CMake flag.

### `scripts/check_pins.ps1`

- **the file**: Fails when anything the build fetches or CI runs is not pinned to fixed content (spec #280):

  - every FetchContent_Declare in a CMakeLists.txt or \*.cmake file fetches either GIT_TAG \<full 40-hex commit hash> or a URL with a URL_HASH, with the readable version in a comment beside it (GIT_TAG c2e7...e998c # 0.662). A hash does not work with GIT_SHALLOW (CMake can only shallow clone a branch or tag), so pinned git dependencies are cloned in full;
    - every Action a workflow uses (other than a local ./ one) is owner/repo@\<full commit SHA> with its version in a comment (uses: actions/checkout@3d3c...0b1 # v7);
    - the workflows install Inno Setup with choco at a stated --version.

  Each problem is reported with its file and line. scripts\test_check_pins.ps1 is this script's own check.

- **`Write-Host "Not pinned:"`**: One line per problem on stdout, so no console width wraps a file name or a reference.

### `scripts/check_proprietary.ps1`

- **the file**: Fails if proprietary GIANTS scripts or third-party mod files are tracked. Checks tracked files only, so local ignored copies under dataS/ or reference/ are fine.
- **`$problems`, the `git grep` for file headers**: Headers of the removed files. This script is excluded so its own patterns don't match.

### `scripts/check_release_symbols.ps1`

- **the file**: Verifies that release packaging produced the symbols zip (FS25_VR_v\<version>_Symbols.zip) and that it holds version.pdb, byte for byte the PDB the linker wrote for the Injector, and that this PDB belongs to the Injector the bundle ships (the GUID and age in the DLL's debug directory are the PDB's own). Run it after scripts\package_release.ps1; CI does, and so does scripts\test_package_release.ps1.

### `scripts/check_release_tag.ps1`

- **the file**:

  ```text
  Verifies that a release tag names the Companion Mod's version, and says what the release workflow
  needs to know about it. A release is named by its tag: v1.2.0, or v1.2.0-pre.1 for a pre-release
  (any suffix after the version). Its assets carry the Companion Mod's four-part version (modDesc.xml,
  for example 1.2.0.0) with the last ".0" dropped, so the tag must equal that: v1.2.0 for 1.2.0.0.
  A mod version that does not end in ".0" has no release name on that rule and cannot be released.

  When GITHUB_OUTPUT is set (GitHub Actions), writes these outputs for later steps, and only once every
  check has passed:
    version      the tag's version without the leading v or the suffix (1.2.0)
    mod_version  the Companion Mod's version (1.2.0.0), the one packaging puts in the asset names
    prerelease   true when the tag has a suffix, else false
  scripts\test_check_release_tag.ps1 is this script's own check.
  ```

### `scripts/deploy.ps1`

- **`$oldInjector`**: An Injector from before ADR-0038 was the game's dxgi.dll. With it still there, the new one stays inactive, so remove it, and put back a dxgi.dll the Installer moved aside for it (ReShade, for one).
- **`$companionDir`**: 2. Package Companion Mod with tar.exe (preserving POSIX forward slashes)

### `scripts/enter_msvc_env.ps1`

- **the file**: Enters the MSVC x64 developer environment: finds the newest Visual Studio (or Build Tools) with the x64 C++ tools, runs its vcvars64.bat and copies every environment variable it sets or changes into this process, so cl, link and the Windows SDK are found by the commands that follow in the same PowerShell. Under GitHub Actions it also writes them to $env:GITHUB_ENV, so the job's later steps get them too. Usage: scripts\enter_msvc_env.ps1 (then cmake --preset local, ...)
- **`if ($env:VSCMD_ARG_TGT_ARCH -eq "x64" -and (Get-Command cl.exe -ErrorAction S...`**: Already in it (a second run, or a Developer PowerShell): nothing to do, and PATH does not grow again.
- **`$env:PATH += ";$installer"`**: vcvars64.bat runs vswhere itself, so its folder goes on PATH first.

### `scripts/package_release.ps1`

- **`& (Join-Path $PSScriptRoot "check_build_config.ps1") (Join-Path $repoRoot "bu...`**: Only ship the optimised configuration (RelWithDebInfo, /Ob2).
- **`if (-not (Test-Path $builtPdb)) {`**: Every release carries the Injector's symbols (the symbols zip), so a crash dump from it can be read. The next build overwrites the PDB, so a release cannot be symbolised afterwards: refuse to package without it, before anything in build\release is touched.
- **`. (Join-Path $PSScriptRoot "symbols_identity.ps1")`**: A PDB left over from another build would be zipped as this release's symbols and open none of its crash dumps: it must carry the GUID and age the linker stamped into this version.dll.
- **`$depsDir`**: version.dll statically links these dependencies; their licenses require shipping the notices. Fetched sources live wherever the build was configured to put them (the CMake presets put them in .deps).
- **`$modelAttribution`**: The Virtual Hand model is CC BY: its attribution also travels with the bundle, not only inside the mod zip.
- **`Copy-Item $licensePath "$bundleDir\LICENSE" -Force`**: GPL-3.0: every distributed copy (the bundle and the standalone mod zip) carries the license.
- **`$symbolsZip`**: Symbols zip: the PDB the linker wrote for the staged Injector, published next to the zip bundle and the Setup exe so a crash dump from this release opens with full symbols. It is a file, not a second FS25_VR_v\* folder: scripts\test_installer.ps1 takes the one such folder in build\release as its staging dir.
- **`$iscc`**: Build the Installer from the same staged bundle folder, so it carries the identical binaries, LICENSE and THIRD_PARTY_NOTICES as the zip bundle. Inno Setup's compiler is not on PATH by default.

### `scripts/symbols_identity.ps1`

- **the file**: Helpers for telling whether a PDB belongs to a DLL: the linker stamps both with the same GUID and age. The DLL holds them in the CodeView record (RSDS) of its debug directory, the PDB in the PDB info stream (stream 1) of its MSF container. Dot-source this file; it needs nothing but .NET. Used by package_release.ps1 and check_release_symbols.ps1, so a release cannot carry symbols that do not open the Injector it ships.

  Only the MSF 7.00 container that MSVC's linker writes is understood. Any other file is an error that says so, never a pass.

- **`Get-PdbIdentity`, `$directoryBlockCount`**: The directory lists, for every stream, its size and the blocks it lives in. The block map lists the directory's own blocks.
- **`Get-PdbIdentity`, `$skipBlocks`**: Stream 0 (the old directory) is skipped to reach the block list of stream 1; an empty one is 0xFFFFFFFF.

### `scripts/test_check_pins.ps1`

- **the file**: Runs scripts\check_pins.ps1 against the repository and against scratch copies of its CMakeLists.txt and workflows with one pin broken, and inspects its exit code and message. Needs no build. Fails (non-zero exit) if any assertion fails. No Pester.

  Extending: add a Test-Case at the bottom. New-ScratchRepo copies what the check reads; Edit-File breaks one pin in the copy; Invoke-PinCheck runs the script in a child shell.

- **`Edit-File`**: Replaces the first match of a regex in a scratch file; throws when it does not match, so a case cannot pass because the repository changed shape under it.

### `scripts/test_check_release_tag.ps1`

- **the file**: Runs scripts\check_release_tag.ps1 against scratch modDesc files and inspects its exit code, its message and the outputs it hands to the release workflow (the file named by GITHUB_OUTPUT). Needs no build. Fails (non-zero exit) if any assertion fails. No Pester.

  Extending: add a Test-Case at the bottom. Invoke-TagCheck runs the script in a child shell.

- **`Assert-Rejected`, `if ($result.Outputs.Count -ne 0) { throw "a failed check must write no output...`**: The workflow must not go on to build from anything a failed check left behind.

### `scripts/test_package_release.ps1`

- **the file**: Runs scripts\package_release.ps1 against a scratch copy of the repo and inspects what it leaves in the scratch build\release, so the real build\release is never touched. Needs a built Injector and a configured build tree (build\version.dll, build\version.pdb, build\CMakeCache.txt); a run that has to succeed also needs Inno Setup, because packaging compiles the Installer. Fails (non-zero exit) if any assertion fails. No Pester.

  Extending: add a Test-Case at the bottom. New-ScratchRepo builds the world, Invoke-Packaging runs the script in it, the Assert-\* helpers check what it left behind.

- **`New-ScratchRepo`**: A copy of everything package_release.ps1 reads from the repo, with the real build's outputs in its build\ folder. CMakeCache.txt still names the real dependency sources, which the licence notices come from.
- **`$stagingFolders`**: scripts\test_installer.ps1 stages its newer Installers from the one FS25_VR_v\* folder in build\release.
- **`. (Join-Path $PSScriptRoot "symbols_identity.ps1")`**: A PDB belongs to a DLL when both carry the GUID and age the linker stamped them with. These cases make a DLL that names another PDB than the one it was linked with, by changing its CodeView record, so that the real PDB in the scratch build no longer belongs to it.
- **`Get-OneLine`**: The console wraps an error message at its own width: compare the message with its line breaks folded away.

### `tools/vdxr_sim_layer/build.ps1`

- **the file**:

  ```text
  Builds the VDXR simulation layer (XrApiLayer_vdxr_sim.dll + its manifest) and harness.exe with the MSVC x64 toolchain.
  Outputs go to <BuildDir>\vdxr_sim_layer (git-ignored). The OpenXR headers come from the repository's CMake build, so
  configure the repository first (any target fetches them).
    .\build.ps1                     # <repo>\build
    .\build.ps1 -BuildDir <path>    # another CMake build folder
  ```

- **`$cache`**: The OpenXR SDK as the build fetched it: under FETCHCONTENT_BASE_DIR (the CMake presets put it in \<repo>\.deps; \<BuildDir>\_deps without them), in openxr-src with the generated headers in openxr-build; or the source folder FETCHCONTENT_SOURCE_DIR_OPENXR points at.

### `tools/vdxr_sim_layer/enable.ps1`

- **the file**:

  ```text
  Enables the test layer for every OpenXR app started afterwards by this user, from the folder build.ps1 built it into.
  Steam must be fully restarted (Steam > Exit) so the game inherits the variables. disable.ps1 undoes it.
    .\enable.ps1                     # <repo>\build\vdxr_sim_layer
    .\enable.ps1 -BuildDir <path>    # another CMake build folder
  ```

### `tools/vdxr_sim_layer/run.ps1`

- **the file**:

  ```text
  Runs the harness against the Injector in build\ (version.dll, from 1.2) in both startup orders, with the VDXR simulation
  layer enabled for this shell only (no User variables, no Steam restart). Each run uses a fresh copy of version.dll in its
  own folder under build\vdxr_sim_layer, since the Injector writes fs25_vr.log next to itself. Run build.ps1 first.
    .\run.ps1                       # <repo>\build\version.dll
    .\run.ps1 -Injector <path>      # another version.dll, or a proxy dxgi.dll up to 1.1.9 (one run, no modes)
    .\run.ps1 -BuildDir <path>      # another CMake build folder (its version.dll and its build.ps1 output)
  ```

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 65 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
