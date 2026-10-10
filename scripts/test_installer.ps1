# Drives the built Installer (FS25_VR_v<version>_Setup.exe) silently against a throwaway fake game
# folder and a throwaway Documents folder, then inspects the files on disk. Run it after
# scripts\package_release.ps1. Fails (non-zero exit) if any assertion fails. No Pester.
#
# The Installer needs admin. CI runs elevated and exercises the real path; when this script is run
# from a non-elevated shell it passes /CURRENTUSER (which the Installer allows on the command line
# only) so it can run without a UAC prompt, and looks for the uninstall entry in HKCU instead.
#
# Extending: add a Test-Case at the bottom. Build the world with New-FakeGame / New-FakeDocuments,
# drive it with Install-Fs25Vr / Uninstall-Fs25Vr (both always clean up via Remove-TestWorld),
# assert with the Assert-* helpers.
param(
    [string]$SetupExe = ""
)
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $repoRoot "build\release"

if (-not $SetupExe) {
    $found = Get-ChildItem $releaseDir -Filter "FS25_VR_v*_Setup.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) { Write-Error "No FS25_VR_v*_Setup.exe in $releaseDir. Run scripts\package_release.ps1 first." }
    $SetupExe = $found.FullName
}
if (-not (Test-Path $SetupExe)) { Write-Error "$SetupExe not found." }
Write-Host "Testing $SetupExe"

# Fixed application ID of the Installer (AppId in installer\fs25_vr.iss). Add/Remove Programs key.
$appId = "{C85F7B59-39C3-48F5-94FB-38E291A376AE}"
$uninstallKeyName = "${appId}_is1"
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
$uninstallKeyPath = if ($isAdmin) { "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$uninstallKeyName" }
                    else { "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$uninstallKeyName" }

# What the Installer must have copied: the staged release files, the same ones the zip bundle holds.
$stagedInjector = Join-Path $releaseDir "dxgi.dll"
$stagedMod = Join-Path $releaseDir "FS25_VRCompanion.zip"

# A game.xml shaped like the game's, used as the untouched-settings baseline.
$sampleGameXml = @"
<?xml version="1.0" encoding="utf-8" standalone="no" ?>
<gameSettings>
    <modsDirectoryOverride active="false" directory=""/>
    <gameplay>
        <fullscreenMode>fullscreen</fullscreenMode>
    </gameplay>
    <display>
        <width>2560</width>
        <height>1440</height>
    </display>
</gameSettings>
"@

# ---------------------------------------------------------------------------------------------
# World helpers
# ---------------------------------------------------------------------------------------------

function New-TestWorld {
    $root = Join-Path ([IO.Path]::GetTempPath()) ("fs25vr-installer-test-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory -Path $root | Out-Null
    [pscustomobject]@{
        Root      = $root
        Game      = Join-Path $root "Game folder"          # spaces on purpose
        Documents = Join-Path $root "Documents"
    }
}

# A folder the Installer must accept: it holds x64\FarmingSimulator2025Game.exe.
function New-FakeGame($world) {
    $x64 = Join-Path $world.Game "x64"
    New-Item -ItemType Directory -Path $x64 -Force | Out-Null
    Set-Content -Path (Join-Path $x64 "FarmingSimulator2025Game.exe") -Value "not a real game" -Encoding ascii
}

# <Documents>\My Games\FarmingSimulator2025 with a game.xml and a modSettings folder holding VR settings.
function New-FakeDocuments($world, [string]$gameXml = $sampleGameXml) {
    $myGames = Join-Path $world.Documents "My Games\FarmingSimulator2025"
    New-Item -ItemType Directory -Path (Join-Path $myGames "modSettings\FS25_VRCompanion") -Force | Out-Null
    Set-Content -Path (Join-Path $myGames "modSettings\FS25_VRCompanion\settings.xml") -Value "<settings/>" -Encoding ascii
    if ($gameXml) { [IO.File]::WriteAllText((Join-Path $myGames "game.xml"), $gameXml, (New-Object Text.UTF8Encoding $false)) }
}

function Get-GamePaths($world) {
    $myGames = Join-Path $world.Documents "My Games\FarmingSimulator2025"
    [pscustomobject]@{
        Injector     = Join-Path $world.Game "x64\dxgi.dll"
        Log          = Join-Path $world.Game "x64\fs25_vr.log"
        MyGames      = $myGames
        DefaultMods  = Join-Path $myGames "mods"
        ModZip       = Join-Path $myGames "mods\FS25_VRCompanion.zip"
        GameXml      = Join-Path $myGames "game.xml"
        ModSettings  = Join-Path $myGames "modSettings\FS25_VRCompanion\settings.xml"
    }
}

# ---------------------------------------------------------------------------------------------
# Installer drivers
# ---------------------------------------------------------------------------------------------

function Get-UninstallEntry { Get-ItemProperty -Path $uninstallKeyPath -ErrorAction SilentlyContinue }

# Waits for a process, but never forever: a stray dialog in a silent run must fail the case, not hang CI.
function Wait-Process-Guarded($process, [string]$what, [int]$timeoutSeconds = 120) {
    if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
        Get-Process | Where-Object { $_.Name -like "FS25_VR_v*_Setup*" -or $_.Name -like "unins*" } | Stop-Process -Force -ErrorAction SilentlyContinue
        throw "$what did not finish within $timeoutSeconds seconds (a dialog is probably waiting for input)."
    }
    $process.ExitCode
}
# Runs Setup silently into $world.Game with $world.Documents standing in for the user's Documents
# folder (the hidden /FS25VR_DOCUMENTS switch). Returns Setup's exit code.
function Install-Fs25Vr($world, [string[]]$extraArgs = @()) {
    $setupArgs = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
              "/LOG=`"$($world.Root)\setup.log`"",
              "/DIR=`"$($world.Game)`"",
              "/FS25VR_DOCUMENTS=`"$($world.Documents)`"")
    if (-not $isAdmin) { $setupArgs += "/CURRENTUSER" }
    $setupArgs += $extraArgs
    Wait-Process-Guarded (Start-Process -FilePath $SetupExe -ArgumentList $setupArgs -PassThru) "Setup"
}

# Runs the registered uninstaller silently and waits until it is really gone: Inno's uninstaller
# hands over to a temporary copy of itself, so the first process can exit before the work is done.
function Uninstall-Fs25Vr {
    $entry = Get-UninstallEntry
    if (-not $entry) { throw "FS25 VR is not registered in Add/Remove Programs; nothing to uninstall." }
    $uninstaller = $entry.UninstallString.Trim('"')
    Wait-Process-Guarded (Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -PassThru) "Uninstaller" | Out-Null
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-UninstallEntry) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Get-UninstallEntry) { throw "Uninstall did not finish within 60 seconds." }
    # The registry entry goes first; the uninstaller's own [Code] (restoring a backed-up dxgi.dll, ...)
    # runs after it. The uninstaller deletes itself last, so wait for that.
    while ((Test-Path $uninstaller) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Test-Path $uninstaller) { throw "The uninstaller was still running after 60 seconds." }
}

# Runs the registered uninstaller silently once and returns its exit code, without requiring that it
# succeeds (Uninstall-Fs25Vr throws unless the Add/Remove Programs entry disappears). Then waits for
# Inno's temporary second-phase copy to be gone, so the files can be inspected.
function Invoke-Uninstaller {
    $entry = Get-UninstallEntry
    if (-not $entry) { throw "FS25 VR is not registered in Add/Remove Programs; nothing to uninstall." }
    $uninstaller = $entry.UninstallString.Trim('"')
    $exit = Wait-Process-Guarded (Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -PassThru) "Uninstaller"
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Process -Name "_iu*", "unins*" -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    $exit
}

# A stand-in for the running game: a copy of a harmless Windows exe named FarmingSimulator2025Game.exe,
# running for a long time. The Installer must detect the game by process image name, so this is enough.
$script:standIn = $null
function Start-StandInGame($world) {
    $exe = Join-Path $world.Root "standin\FarmingSimulator2025Game.exe"
    New-Item -ItemType Directory -Path (Split-Path $exe) -Force | Out-Null
    Copy-Item (Join-Path $env:SystemRoot "System32\PING.EXE") $exe -Force
    $script:standIn = Start-Process -FilePath $exe -ArgumentList @("-n", "600", "127.0.0.1") -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds(10)
    while (-not (Get-Process -Name "FarmingSimulator2025Game" -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Assert-True ($null -ne (Get-Process -Name "FarmingSimulator2025Game" -ErrorAction SilentlyContinue)) "The stand-in game process did not start."
}
function Stop-StandInGame {
    Get-Process -Name "FarmingSimulator2025Game" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    if ($script:standIn) { $script:standIn.WaitForExit(10000) | Out-Null; $script:standIn = $null }
}

# Always leave the machine clean, whether or not the case passed.
function Remove-TestWorld($world) {
    Stop-StandInGame      # a running game would (rightly) block the cleanup uninstall
    if (Get-UninstallEntry) { try { Uninstall-Fs25Vr } catch { Write-Warning "Cleanup uninstall failed: $_" } }
    Remove-Item $world.Root -Recurse -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------------------------

function Assert-True([bool]$condition, [string]$message) { if (-not $condition) { throw $message } }
function Assert-Exists([string]$path, [string]$what) { Assert-True (Test-Path $path) "$what should exist: $path" }
function Assert-Missing([string]$path, [string]$what) { Assert-True (-not (Test-Path $path)) "$what should not exist: $path" }
function Assert-SameFile([string]$actual, [string]$expected, [string]$what) {
    Assert-Exists $actual $what
    $a = (Get-FileHash $actual -Algorithm SHA256).Hash
    $e = (Get-FileHash $expected -Algorithm SHA256).Hash
    Assert-True ($a -eq $e) "$what differs from the staged release file ($expected)."
}
function Assert-SameBytes([string]$path, [byte[]]$expected, [string]$what) {
    Assert-Exists $path $what
    $actual = [IO.File]::ReadAllBytes($path)
    Assert-True ([Linq.Enumerable]::SequenceEqual($actual, $expected)) "$what was modified: $path"
}

# ---------------------------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------------------------

$failures = New-Object Collections.Generic.List[string]
function Test-Case([string]$name, [scriptblock]$body) {
    $world = New-TestWorld
    try {
        & $body $world
        Write-Host "[PASS] $name"
    } catch {
        Write-Host "[FAIL] $name -- $($_.Exception.Message)"
        $failures.Add($name)
    } finally {
        Remove-TestWorld $world
    }
}

# Refuse to run over a real FS25 VR install: the fixed application ID means we would upgrade it.
if (Get-UninstallEntry) { Write-Error "FS25 VR is already installed on this machine; uninstall it before running the installer tests." }

# ---------------------------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------------------------

Test-Case "Clean install places the Injector and the Companion Mod and leaves game.xml alone" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."

    Assert-SameFile $p.Injector $stagedInjector "Injector (dxgi.dll in x64)"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
    Assert-Missing (Join-Path $p.DefaultMods "FS25_VRCompanion") "An extracted Companion Mod folder"

    $entry = Get-UninstallEntry
    Assert-True ($null -ne $entry) "FS25 VR is not registered in Add/Remove Programs."
    Assert-True ($entry.DisplayName -eq "FS25 VR") "Add/Remove Programs shows '$($entry.DisplayName)', expected 'FS25 VR'."
}

Test-Case "Uninstall removes the Injector, the mod zip and fs25_vr.log and keeps modSettings" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."
    Set-Content -Path $p.Log -Value "written by the Injector while the game ran" -Encoding ascii
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)

    Uninstall-Fs25Vr

    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-Missing $p.Log "fs25_vr.log"
    Assert-Exists $p.ModSettings "modSettings VR settings"
    Assert-Exists (Join-Path $world.Game "x64\FarmingSimulator2025Game.exe") "The game executable"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
    Assert-True ($null -eq (Get-UninstallEntry)) "FS25 VR is still registered in Add/Remove Programs."
}

Test-Case "A folder without x64\FarmingSimulator2025Game.exe is refused" {
    param($world)
    New-Item -ItemType Directory -Path $world.Game -Force | Out-Null      # no x64\...Game.exe inside
    New-FakeDocuments $world
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup accepted a folder that is not a Farming Simulator 25 installation."
    Assert-True ((Get-Content "$($world.Root)\setup.log" -Raw) -match "not a Farming Simulator 25 installation") "Setup did not say why it refused the folder."

    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-True ($null -eq (Get-UninstallEntry)) "A refused install was still registered in Add/Remove Programs."
}

Test-Case "A silent install while the game is running fails and changes nothing" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)
    Start-StandInGame $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup succeeded while Farming Simulator 25 was running."
    Assert-True ((Get-Content "$($world.Root)\setup.log" -Raw) -match "Close Farming Simulator 25 to continue") "Setup did not say why it refused to run."

    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-Missing $p.DefaultMods "mods folder"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
    Assert-True ($null -eq (Get-UninstallEntry)) "A refused install was still registered in Add/Remove Programs."

    # Once the game has closed, the same install goes through.
    Stop-StandInGame
    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit after the game closed."
    Assert-SameFile $p.Injector $stagedInjector "Injector (dxgi.dll in x64)"
}

Test-Case "A silent uninstall while the game is running fails and changes nothing" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)
    Start-StandInGame $world

    $exit = Invoke-Uninstaller
    Assert-True ($exit -ne 0) "The uninstaller succeeded while Farming Simulator 25 was running."

    Assert-SameFile $p.Injector $stagedInjector "Injector (dxgi.dll in x64)"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
    Assert-True ($null -ne (Get-UninstallEntry)) "A refused uninstall removed the Add/Remove Programs entry."

    # Once the game has closed, the same uninstall goes through.
    Stop-StandInGame
    Uninstall-Fs25Vr
    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
}

# --- Mods directory override (ticket #64) ---------------------------------------------------

# A game.xml whose mods directory override element is set as given (the rest is the sample baseline).
function New-GameXmlWithOverride([string]$active, [string]$directory) {
    $old = '<modsDirectoryOverride active="false" directory=""/>'
    $new = "<modsDirectoryOverride active=`"$active`" directory=`"$directory`"/>"
    Assert-True $sampleGameXml.Contains($old) "The sample game.xml no longer holds the expected override element."
    $sampleGameXml.Replace($old, $new)
}

Test-Case "Active mods directory override: the mod lands in the override folder, not the default one" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"         # spaces on purpose; does not exist yet
    New-FakeDocuments $world (New-GameXmlWithOverride "true" $override)
    $p = Get-GamePaths $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."

    Assert-SameFile (Join-Path $override "FS25_VRCompanion.zip") $stagedMod "Companion Mod zip in the override folder"
    Assert-Missing $p.ModZip "Companion Mod zip in the default mods folder"
    Assert-SameFile $p.Injector $stagedInjector "Injector (dxgi.dll in x64)"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml (the override element)"
}

Test-Case "Uninstall removes the mod from the override folder and leaves the override alone" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"
    New-FakeDocuments $world (New-GameXmlWithOverride "true" $override)
    $p = Get-GamePaths $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)
    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."
    Assert-Exists (Join-Path $override "FS25_VRCompanion.zip") "Companion Mod zip in the override folder"

    Uninstall-Fs25Vr

    Assert-Missing (Join-Path $override "FS25_VRCompanion.zip") "Companion Mod zip in the override folder"
    Assert-Missing $p.ModZip "Companion Mod zip in the default mods folder"
    Assert-Missing $p.Injector "Injector"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml (the override element)"
}

Test-Case "Inactive mods directory override with a directory set: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"
    New-FakeDocuments $world (New-GameXmlWithOverride "false" $override)
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
    Assert-Missing (Join-Path $override "FS25_VRCompanion.zip") "Companion Mod zip in the inactive override folder"
}

Test-Case "No mods directory override element at all: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world ($sampleGameXml.Replace('<modsDirectoryOverride active="false" directory=""/>', ''))
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
}

Test-Case "Active override with an empty directory: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world (New-GameXmlWithOverride "true" "")
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
}

# ---------------------------------------------------------------------------------------------
# Existing dxgi.dll and upgrade helpers (ticket #65)
# ---------------------------------------------------------------------------------------------

# Runs an Installer exe silently like Install-Fs25Vr, but for any Setup exe, and optionally without
# /DIR so the Installer has to pick the game folder itself (the previously used one on an upgrade).
function Invoke-SetupExe([string]$exe, $world, [string[]]$extraArgs = @(), [switch]$NoDir) {
    $setupArgs = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
              "/LOG=`"$($world.Root)\setup-$([guid]::NewGuid().ToString('N').Substring(0, 6)).log`"",
              "/FS25VR_DOCUMENTS=`"$($world.Documents)`"")
    if (-not $NoDir) { $setupArgs += "/DIR=`"$($world.Game)`"" }
    if (-not $isAdmin) { $setupArgs += "/CURRENTUSER" }
    $setupArgs += $extraArgs
    Wait-Process-Guarded (Start-Process -FilePath $exe -ArgumentList $setupArgs -PassThru) "Setup"
}

# The text of the newest Setup log in the world (each Invoke-SetupExe run writes its own log).
function Get-LastSetupLog($world) {
    $log = Get-ChildItem $world.Root -Filter "setup*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $log) { throw "No Setup log found in $($world.Root)." }
    Get-Content $log.FullName -Raw
}

# A "newer" Installer: the same script and staged files compiled with a higher version number.
function New-NewerSetup($world, [string]$version = "99.0.0.0") {
    $iscc = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }),
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup compiler (ISCC.exe) not found; needed to build a newer Installer for the upgrade tests." }
    $stageDir = Get-ChildItem $releaseDir -Directory -Filter "FS25_VR_v*" | Select-Object -First 1
    if (-not $stageDir) { throw "No staged FS25_VR_v* folder in $releaseDir." }
    $outDir = Join-Path $world.Root "newer-setup"
    & $iscc "/Q" "/DAppVersion=$version" "/DStageDir=$($stageDir.FullName)" "/DOutputDir=$outDir" (Join-Path $repoRoot "installer\fs25_vr.iss")
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed to build the newer Installer (exit code $LASTEXITCODE)." }
    (Get-ChildItem $outDir -Filter "*_Setup.exe" | Select-Object -First 1).FullName
}

# Marker text the Installer logs when it moves a foreign dxgi.dll aside.
$foreignWarning = "not part of FS25 VR"

# A real dxgi.dll that is not ours: the copy of Windows' own one (it has a version resource, just a different product).
function Get-ForeignDxgiBytes { [IO.File]::ReadAllBytes((Join-Path $env:SystemRoot "System32\dxgi.dll")) }

# ---------------------------------------------------------------------------------------------
# Cases: existing dxgi.dll and upgrade (ticket #65)
# ---------------------------------------------------------------------------------------------

Test-Case "A foreign dxgi.dll is warned about, backed up on install and restored on uninstall" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $foreign = Get-ForeignDxgiBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."
    Assert-True ((Get-LastSetupLog $world) -match $foreignWarning) "Setup did not warn about the foreign dxgi.dll."
    Assert-SameFile $p.Injector $stagedInjector "Injector (dxgi.dll in x64)"
    Assert-SameBytes $backup $foreign "dxgi.dll.fs25vr-backup (the foreign dxgi.dll)"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.Injector $foreign "The foreign dxgi.dll restored on uninstall"
    Assert-Missing $backup "dxgi.dll.fs25vr-backup after uninstall"
}

Test-Case "Our own older dxgi.dll is overwritten with no warning and no backup" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    # An older build of ours: same version resource, different bytes.
    [IO.File]::WriteAllBytes($p.Injector, [byte[]]([IO.File]::ReadAllBytes($stagedInjector) + [byte[]](1..64)))

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."
    Assert-SameFile $p.Injector $stagedInjector "Injector (dxgi.dll in x64)"
    Assert-Missing "$($p.Injector).fs25vr-backup" "A dxgi.dll.fs25vr-backup of our own older Injector"
    Assert-True (-not ((Get-LastSetupLog $world) -match $foreignWarning)) "Setup warned about our own dxgi.dll."
}

Test-Case "An existing dxgi.dll.fs25vr-backup survives an upgrade untouched and is still restored on uninstall" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $foreign = Get-ForeignDxgiBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)
    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "First Setup exited with $exit."
    Assert-SameBytes $backup $foreign "dxgi.dll.fs25vr-backup after the first install"

    $newer = New-NewerSetup $world
    $exit = Invoke-SetupExe $newer $world
    Assert-True ($exit -eq 0) "Upgrade Setup exited with $exit."
    Assert-True (-not ((Get-LastSetupLog $world) -match $foreignWarning)) "The upgrade warned about a foreign dxgi.dll although only ours is installed."
    Assert-SameFile $p.Injector $stagedInjector "Injector after the upgrade"
    Assert-SameBytes $backup $foreign "dxgi.dll.fs25vr-backup after the upgrade"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.Injector $foreign "The original foreign dxgi.dll restored on uninstall"
    Assert-Missing $backup "dxgi.dll.fs25vr-backup after uninstall"
}

Test-Case "A foreign dxgi.dll never replaces an existing dxgi.dll.fs25vr-backup" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $original = [byte[]](1..200)
    $another = Get-ForeignDxgiBytes
    [IO.File]::WriteAllBytes($backup, $original)
    [IO.File]::WriteAllBytes($p.Injector, $another)

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup went on although it had nowhere safe to move the foreign dxgi.dll."
    Assert-SameBytes $backup $original "The existing dxgi.dll.fs25vr-backup"
    Assert-SameBytes $p.Injector $another "The foreign dxgi.dll"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-True ($null -eq (Get-UninstallEntry)) "A refused install was still registered in Add/Remove Programs."
}

Test-Case "A newer Installer upgrades under one Add/Remove Programs entry and pre-selects the previous game folder" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "First Setup exited with $exit."
    # Make the upgrade visible: remove what the first install placed.
    Remove-Item $p.Injector, $p.ModZip -Force

    $newer = New-NewerSetup $world
    $exit = Invoke-SetupExe $newer $world -NoDir      # no /DIR: the Installer must offer the previous folder
    Assert-True ($exit -eq 0) "Upgrade Setup exited with $exit."

    Assert-SameFile $p.Injector $stagedInjector "Injector in the previously used game folder"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip"
    $entry = Get-UninstallEntry
    Assert-True ($null -ne $entry) "FS25 VR is not registered in Add/Remove Programs after the upgrade."
    Assert-True ($entry.DisplayVersion -eq "99.0.0.0") "Add/Remove Programs shows version '$($entry.DisplayVersion)', expected 99.0.0.0."
    $parent = Split-Path -Parent $uninstallKeyPath
    $entries = @(Get-ChildItem $parent | Where-Object { (Get-ItemProperty $_.PSPath).DisplayName -eq "FS25 VR" })
    Assert-True ($entries.Count -eq 1) "Expected one FS25 VR entry in Add/Remove Programs, found $($entries.Count)."
}

if ($failures.Count -gt 0) {
    Write-Error "$($failures.Count) installer test(s) failed: $($failures -join '; ')"
}
Write-Host "All installer tests passed."
