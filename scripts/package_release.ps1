# Package script for Farming Simulator 25 VR release distribution
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$builtDll = Join-Path $repoRoot "build\dxgi.dll"
$companionDir = Join-Path $repoRoot "mods\FS25_VRCompanion"
$releaseDir = Join-Path $repoRoot "build\release"
$modDescPath = Join-Path $companionDir "modDesc.xml"
$licensePath = Join-Path $repoRoot "LICENSE"

Write-Host "Packaging FS25 VR Release..."

# Only ship the optimised configuration (RelWithDebInfo, /Ob2).
& (Join-Path $PSScriptRoot "check_build_config.ps1") (Join-Path $repoRoot "build")

if (-not (Test-Path $builtDll)) {
    Write-Error "build\dxgi.dll not found. Build project first (ninja -C build dxgi)."
}

# Only ship an Injector that needs no VC++ redistributable (static C runtime).
& (Join-Path $PSScriptRoot "check_injector_imports.ps1") $builtDll

# Read the release version from modDesc
[xml]$modDesc = Get-Content $modDescPath -Raw
$version = $modDesc.modDesc.version
if ([string]::IsNullOrWhiteSpace($version)) {
    Write-Error "No <version> element found in $modDescPath."
}
$bundleName = "FS25_VR_v$version"
$bundleDir = Join-Path $releaseDir $bundleName

# Clean and recreate staging directories
if (Test-Path $releaseDir) {
    Remove-Item $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Path "$bundleDir\x64" -Force | Out-Null
New-Item -ItemType Directory -Path "$bundleDir\mods" -Force | Out-Null

# Copy Injector DLL
Copy-Item $builtDll "$bundleDir\x64\dxgi.dll" -Force
Copy-Item $builtDll "$releaseDir\dxgi.dll" -Force
$builtPdb = Join-Path $repoRoot "build\dxgi.pdb"
if (Test-Path $builtPdb) { Copy-Item $builtPdb "$releaseDir\dxgi.pdb" -Force }
Write-Host "[Package] Staged dxgi.dll"

# dxgi.dll statically links these dependencies; their licenses require shipping the notices.
# Fetched sources live wherever the build was configured to put them (CI uses -DFETCHCONTENT_BASE_DIR=.deps).
$depsDir = Join-Path $repoRoot "build\_deps"
$cmakeCache = Join-Path $repoRoot "build\CMakeCache.txt"
if (Test-Path $cmakeCache) {
    $baseDirLine = Select-String -Path $cmakeCache -Pattern '^FETCHCONTENT_BASE_DIR:[A-Z]+=(.+)$' | Select-Object -First 1
    if ($baseDirLine) {
        $depsDir = $baseDirLine.Matches[0].Groups[1].Value.Trim()
    }
}
$notices = @(
    @{ Name = "MinHook";            File = "minhook-src\LICENSE.txt" },
    @{ Name = "Luau";               File = "luau-src\LICENSE.txt" },
    @{ Name = "Lua (used by Luau)"; File = "luau-src\lua_LICENSE.txt" },
    @{ Name = "OpenXR SDK";         File = "openxr-src\LICENSE" }
)
$noticeText = foreach ($notice in $notices) {
    $path = Join-Path $depsDir $notice.File
    if (-not (Test-Path $path)) {
        Write-Error "$path not found. Configure the build first (cmake -B build) so dependencies are fetched."
    }
    "=" * 80
    "  $($notice.Name)"
    "=" * 80
    Get-Content $path -Raw
    ""
}
Set-Content -Path "$bundleDir\THIRD_PARTY_NOTICES.txt" -Value $noticeText -Encoding utf8
Write-Host "[Package] Wrote THIRD_PARTY_NOTICES.txt"

# GPL-3.0: every distributed copy (the bundle and the standalone mod zip) carries the license.
Copy-Item $licensePath "$bundleDir\LICENSE" -Force
Write-Host "[Package] Staged LICENSE"

# Package Companion Mod
Push-Location $companionDir
try {
    tar.exe -a -cf "$bundleDir\mods\FS25_VRCompanion.zip" modDesc.xml icon_vr.png src/*.lua -C $repoRoot LICENSE
    Copy-Item "$bundleDir\mods\FS25_VRCompanion.zip" "$releaseDir\FS25_VRCompanion.zip" -Force
    Write-Host "[Package] Packaged FS25_VRCompanion.zip"
} finally {
    Pop-Location
}

# Write README.txt
$readmeContent = @"
================================================================================
  Farming Simulator 25 VR (FS25 VR) - Version $version
  Virtual Reality for GIANTS Engine 10 under Direct3D 12
================================================================================

RECOMMENDED: USE THE INSTALLER
--------------------------------------------------------------------------------
Run FS25_VR_v${version}_Setup.exe (published next to this zip on the GitHub Release).
It finds your Farming Simulator 25 folder (Steam, Epic Games or GIANTS) and
pre-selects it; if there are several you pick one, and if none is found you
browse to the folder that contains x64\FarmingSimulator2025Game.exe. It places the
files below for you and adds FS25 VR to Add/Remove Programs for uninstalling.
Close the game first.

- Render Resolution page: optionally tick "Set the game's Render Resolution" and
  enter your headset's per-eye width and height (640 to 8192). The Installer sets
  the game to windowed mode at that size and keeps a copy of your game.xml as
  game.xml.fs25vr-backup. Launch the game once first if there is no game.xml yet.
- Silent install: FS25_VR_v${version}_Setup.exe /VERYSILENT /DIR="<game folder>"
  Add /FS25VR_RESOLUTION=WxH (for example /FS25VR_RESOLUTION=4000x2500) to set the
  Render Resolution without the wizard page. An invalid value fails the install.
  If game.xml can't be edited (for example it is read-only), FS25 VR stays
  installed and Setup exits with code 20.
  A silent run over an existing install upgrades it and never asks anything.
- A dxgi.dll that is not part of FS25 VR (for example ReShade) is moved to
  dxgi.dll.fs25vr-backup before FS25 VR is installed.

Windows may show "Windows protected your PC" because the Installer is not
code-signed yet. This is expected: click "More info", then "Run anyway".

MANUAL INSTALLATION INSTRUCTIONS
--------------------------------------------------------------------------------

1. INSTALL INJECTOR DLL:
   Copy 'dxgi.dll' from the 'x64' folder into your Farming Simulator 25 'x64' directory:
   
   - Steam Default Path:
     C:\Program Files (x86)\Steam\steamapps\common\Farming Simulator 25\x64\
   
   - GIANTS Standalone Default Path:
     C:\Program Files\Farming Simulator 2025\x64\
   
   - Epic Games Default Path:
     C:\Program Files\Epic Games\FarmingSimulator25\x64\

   (This folder contains 'FarmingSimulator2025Game.exe'.)

2. INSTALL COMPANION MOD:
   Copy 'FS25_VRCompanion.zip' from the 'mods' folder into your Farming Simulator 25 mods folder:
   
   %USERPROFILE%\Documents\My Games\FarmingSimulator2025\mods\

   NOTE: Do NOT extract or unzip 'FS25_VRCompanion.zip'! Farming Simulator reads it directly.

3. PLAY:
   - Start your VR runtime (Meta Quest Link / AirLink, Virtual Desktop, or SteamVR).
   - Launch Farming Simulator 25 normally.
   - When loading or starting a Career / Savegame, make sure "FS25 VR Companion Mod" is checked/enabled.
   - Put on your VR headset and enjoy!


CONTROLS & KEYBINDINGS
--------------------------------------------------------------------------------
- Recenter VR View:               NumPad 5             or  Gamepad R3 (Right Stick Click / Button 11)
- Toggle VR View Mode (3D / 2D):  Right Ctrl + V or 0  or  Gamepad L3 (Left Stick Click / Button 9)
- Toggle VR HUD Mode:             Right Ctrl + H       or  Gamepad Button 10
- VR Snap Turn Left:              NumPad 4             or  Gamepad LB (Left Bumper / Button 5)
- VR Snap Turn Right:             NumPad 6             or  Gamepad RB (Right Bumper / Button 6)


PERFORMANCE & SETTINGS
--------------------------------------------------------------------------------
- Sequential Dual-Pass (SDP):
  Default high-fidelity mode. Clamps right-eye simulation delta to 0, completely
  eliminating TAA double vision and DLSS ghosting. Recommended for RTX 3080/4080/4090.

- Alternate Frame Rendering (AFR):
  Alternates eye rendering per frame. Lightweight for midrange GPUs.

- In-Game Console Commands (press ~ to open console):
  * vrRenderingMode <afr|dualpass|toggle>
  * vrHudGroup <map|speedometer|info|controls> <horizontal> <vertical> [scale]
                                         (defaults: speedometer 0.30 0.20 0.82, map 0.30 0.22 0.82,
                                          info 0.30 0.30 0.82, controls 0.30 0.30 0.82)
  * vrHudSafeArea <horizontal> <vertical>      (HUD Safe Area, default: 0.30 0.22)
  * vrHudConvergence <depth_in_meters>   (default: 1.8)
  * vrSnapTurnAngle <degrees>            (default: 90)

Settings are automatically saved in:
Documents\My Games\FarmingSimulator2025\modSettings\FS25_VRCompanion\settings.xml


UNINSTALLATION
--------------------------------------------------------------------------------
- Installed with the Installer: run FS25_VR_v${version}_Setup.exe again and choose
  "Uninstall" (the other choices are "Upgrade or reinstall" and "Cancel"), or
  uninstall "FS25 VR" from Add/Remove Programs. For scripts,
  FS25_VR_v${version}_Setup.exe /FS25VR_UNINSTALL runs the uninstaller unattended
  and exits with its exit code (non-zero if FS25 VR is not installed or the game
  is running).
  Uninstalling removes dxgi.dll, FS25_VRCompanion.zip and fs25_vr.log, and keeps
  your VR settings in modSettings. It puts back the dxgi.dll it moved aside (or
  keeps a dxgi.dll you installed over FS25 VR's since), and
  restores your original Render Resolution unless you changed the width, height
  or fullscreen mode in the game since. game.xml.fs25vr-backup stays.
- Installed manually:
  Delete 'dxgi.dll' from the game's 'x64' folder, and
  delete 'FS25_VRCompanion.zip' from your 'mods' folder.

LICENSE
--------------------------------------------------------------------------------
Copyright (C) 2026 TBD.
FS25 VR is free software licensed under the GNU General Public License v3.0;
see LICENSE. Bundled third-party library licenses are in THIRD_PARTY_NOTICES.txt.

For full documentation and source code, visit:
https://github.com/spectral-byte/FarmingSimVr
"@

Set-Content -Path "$bundleDir\README.txt" -Value $readmeContent -Encoding utf8

# Create master bundle zip
$zipBundle = Join-Path $releaseDir "$bundleName.zip"
Push-Location $bundleDir
try {
    tar.exe -a -cf $zipBundle x64/dxgi.dll mods/FS25_VRCompanion.zip README.txt LICENSE THIRD_PARTY_NOTICES.txt
    Write-Host "[Package] Created $zipBundle"
} finally {
    Pop-Location
}

# Build the Installer from the same staged bundle folder, so it carries the identical binaries,
# LICENSE and THIRD_PARTY_NOTICES as the zip bundle. Inno Setup's compiler is not on PATH by default.
$iscc = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }),
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    Write-Error "Inno Setup compiler (ISCC.exe) not found. Install Inno Setup 6 (choco install innosetup)."
}
& $iscc "/DAppVersion=$version" "/DStageDir=$bundleDir" "/DOutputDir=$releaseDir" (Join-Path $repoRoot "installer\fs25_vr.iss")
if ($LASTEXITCODE -ne 0) {
    Write-Error "Inno Setup failed to build the Installer (exit code $LASTEXITCODE)."
}
$setupExe = Join-Path $releaseDir "${bundleName}_Setup.exe"
if (-not (Test-Path $setupExe)) {
    Write-Error "Expected $setupExe was not produced."
}
Write-Host "[Package] Built $setupExe"

Write-Host "Release packaging complete! Files located in: $releaseDir"
