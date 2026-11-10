# Drives the built Installer (FS25_VR_v<version>_Setup.exe) silently against a throwaway fake game
# folder and a throwaway Documents folder, then inspects the files on disk. Run it after
# scripts\package_release.ps1. Fails (non-zero exit) if any assertion fails. No Pester.
#
# The Installer needs admin. CI runs elevated and exercises the real path; when this script is run
# from a non-elevated shell it passes /CURRENTUSER (which the Installer allows on the command line
# only) so it can run without a UAC prompt, and looks for the uninstall entry in HKCU instead.
#
# Windows' own crash dumps for the game are a key under HKEY_LOCAL_MACHINE, which only an elevated Setup
# can write. The cases that need that run only in an elevated shell and are reported as [SKIP], never as
# [PASS], in a non-elevated one (Test-Case -Needs Elevated). The case for the per-user run passes /CURRENTUSER
# itself, so it runs in both kinds of shell. Every Setup run is given a throwaway game exe name (the hidden
# /FS25VR_GAMEEXE switch, see New-TestWorld), so the key of the real FarmingSimulator2025Game.exe is never read
# or written.
#
# On GitHub Actions (GITHUB_ACTIONS is "true"), which is elevated, nothing may be skipped: a skipped case there
# means the elevated cases are not running, and the script then fails instead of reporting success.
#
# Extending: add a Test-Case at the bottom. Build the world with New-FakeGame / New-FakeDocuments,
# drive it with Install-Fs25Vr / Uninstall-Fs25Vr (both always clean up via Remove-TestWorld),
# assert with the Assert-* helpers.
param(
    [string]$SetupExe = "",
    # Only run the cases whose name matches this wildcard (e.g. "*override*"); all cases when empty.
    [string]$Filter = ""
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
$machineUninstallKeyPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$uninstallKeyName"
$userUninstallKeyPath = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$uninstallKeyName"
# Where an install of this run registers itself: machine-wide when elevated, per user otherwise.
$uninstallKeyPath = if ($isAdmin) { $machineUninstallKeyPath } else { $userUninstallKeyPath }
# Where an install can be found. An elevated run also makes per-user installs (/CURRENTUSER, see the per-user case),
# which register under HKCU.
$uninstallKeyPaths = if ($isAdmin) { @($machineUninstallKeyPath, $userUninstallKeyPath) } else { @($userUninstallKeyPath) }

# What the Installer must have copied: the staged release files, the same ones the zip bundle holds.
$stagedInjector = Join-Path $releaseDir "version.dll"
$stagedMod = Join-Path $releaseDir "FS25_VRCompanion.zip"

# A game.xml shaped like the game's (<game><graphic><display>), used as the untouched-settings baseline.
# It holds the display width, height and fullscreen mode only; the mods directory override lives in
# gameSettings.xml, not here.
$sampleGameXml = @"
<?xml version="1.0" encoding="UTF-8"?>
<game>
    <graphic>
        <display>
            <width>2560</width>
            <height>1440</height>
            <fullscreenMode>fullscreen</fullscreenMode>
        </display>
    </graphic>
</game>
"@

# A gameSettings.xml shaped like the game's: the mods directory override is its second line, here in
# the state a real machine has after the player once looked at the setting (inactive, directory left over).
$sampleGameSettingsXml = @"
<?xml version="1.0" encoding="UTF-8"?>
<gameSettings revision="15">
    <modsDirectoryOverride active="false" directory="C:/Temp"/>
    <defaultMultiplayerPort>10823</defaultMultiplayerPort>
    <isHeadTrackingEnabled>true</isHeadTrackingEnabled>
</gameSettings>
"@
$sampleOverrideElement = '<modsDirectoryOverride active="false" directory="C:/Temp"/>'

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
        # The exe name every Setup run writes Windows' crash dump key for (the hidden /FS25VR_GAMEEXE
        # switch). Unique per world, never the real game's.
        ExeName   = "fs25vr-test-" + [guid]::NewGuid().ToString("N").Substring(0, 8) + ".exe"
    }
}

# A folder the Installer must accept: it holds x64\FarmingSimulator2025Game.exe.
function New-FakeGame($world) {
    $x64 = Join-Path $world.Game "x64"
    New-Item -ItemType Directory -Path $x64 -Force | Out-Null
    Set-Content -Path (Join-Path $x64 "FarmingSimulator2025Game.exe") -Value "not a real game" -Encoding ascii
}

# <Documents>\My Games\FarmingSimulator2025 with a game.xml (display settings), a gameSettings.xml (holds
# the mods directory override) and a modSettings folder holding VR settings. Pass $null to leave a file out.
function New-FakeDocuments($world, [string]$gameXml = $sampleGameXml, [string]$gameSettingsXml = $sampleGameSettingsXml) {
    $myGames = Join-Path $world.Documents "My Games\FarmingSimulator2025"
    New-Item -ItemType Directory -Path (Join-Path $myGames "modSettings\FS25_VRCompanion") -Force | Out-Null
    Set-Content -Path (Join-Path $myGames "modSettings\FS25_VRCompanion\settings.xml") -Value "<settings/>" -Encoding ascii
    if ($gameXml) { [IO.File]::WriteAllText((Join-Path $myGames "game.xml"), $gameXml, (New-Object Text.UTF8Encoding $false)) }
    if ($gameSettingsXml) { [IO.File]::WriteAllText((Join-Path $myGames "gameSettings.xml"), $gameSettingsXml, (New-Object Text.UTF8Encoding $false)) }
}

# A game.xml in the shape the game really writes (UTF-8 with BOM, CRLF line endings, 4-space indent;
# copied from a real FS25 game.xml). The display <width>, <height> and <fullscreenMode> are the elements
# the Render Resolution step edits; <previousWidth>, <previousHeight> and <previousFullscreenMode> sit next
# to them and must stay as they are. The expected file after an edit is rendered from this same template
# with different numbers, so the assertions do not depend on how the Installer edits.
function New-RealGameXml([string]$width = "2560", [string]$height = "1440", [string]$mode = "fullscreen") {
    $text = @"
<?xml version="1.0" encoding="UTF-8"?>
<game>
    <language>-1</language>
    <graphic>
        <display>
            <width>$width</width>
            <height>$height</height>
            <brightness>1</brightness>
            <fullscreenMode>$mode</fullscreenMode>
            <vsync adaptive="true">false</vsync>
            <userConfirmed>true</userConfirmed>
            <previousWidth>3840</previousWidth>
            <previousHeight>2160</previousHeight>
            <scaling>1.000000</scaling>
            <hdrOutput>true</hdrOutput>
            <previousFullscreenMode>windowed_fullscreen</previousFullscreenMode>
        </display>
        <scalability>
            <performanceClass>Very High</performanceClass>
            <postProcessAA>FSR3</postProcessAA>
        </scalability>
        <renderer>D3D_12</renderer>
    </graphic>
    <audio enable="true" volume="1.000000"/>
    <!-- NONASCII -->
    <startMode>1</startMode>
</game>

"@
    # Real non-ASCII text (this script stays ASCII), so UTF-8 multi-byte sequences sit in the file and
    # must survive an edit byte for byte.
    $text = $text.Replace("NONASCII", ("Gr" + [char]0xF6 + [char]0xDF + "e " + [char]0x20AC))
    $text.Replace("`r`n", "`n").Replace("`n", "`r`n")
}
function Get-GameXmlBytes([string]$text) {
    $bom = [byte[]](0xEF, 0xBB, 0xBF)
    $bom + [Text.Encoding]::UTF8.GetBytes($text)
}
# game.xml as the game writes it, in place of the plain file New-FakeDocuments makes.
function Set-RealGameXml($world, [string]$width = "2560", [string]$height = "1440", [string]$mode = "fullscreen") {
    $p = Get-GamePaths $world
    New-Item -ItemType Directory -Path $p.MyGames -Force | Out-Null
    [IO.File]::WriteAllBytes($p.GameXml, (Get-GameXmlBytes (New-RealGameXml $width $height $mode)))
}

function Get-GamePaths($world) {
    $myGames = Join-Path $world.Documents "My Games\FarmingSimulator2025"
    [pscustomobject]@{
        Injector     = Join-Path $world.Game "x64\version.dll"
        # The Injector of FS25 VR up to 1.1.9, which an upgrade removes (ADR-0038).
        EarlierInjector = Join-Path $world.Game "x64\dxgi.dll"
        Log          = Join-Path $world.Game "x64\fs25_vr.log"
        # The minidump the Injector writes next to its log when the game crashes (src/core/crash_reporter.cpp).
        Dump         = Join-Path $world.Game "x64\fs25_vr_crash.dmp"
        MyGames      = $myGames
        DefaultMods  = Join-Path $myGames "mods"
        ModZip       = Join-Path $myGames "mods\FS25_VRCompanion.zip"
        GameXml      = Join-Path $myGames "game.xml"
        GameSettingsXml = Join-Path $myGames "gameSettings.xml"
        GameXmlBackup = Join-Path $myGames "game.xml.fs25vr-backup"
        ModSettings  = Join-Path $myGames "modSettings\FS25_VRCompanion\settings.xml"
    }
}

# ---------------------------------------------------------------------------------------------
# Installer drivers
# ---------------------------------------------------------------------------------------------

function Get-UninstallEntry {
    foreach ($path in $uninstallKeyPaths) {
        $entry = Get-ItemProperty -Path $path -ErrorAction SilentlyContinue
        if ($entry) { return $entry }
    }
}

# Waits for a process, but never forever: a stray dialog in a silent run must fail the case, not hang CI.
function Wait-GuardedProcess($process, [string]$what, [int]$timeoutSeconds = 120) {
    if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
        Get-Process | Where-Object { $_.Name -like "FS25_VR_v*_Setup*" -or $_.Name -like "unins*" } | Stop-Process -Force -ErrorAction SilentlyContinue
        throw "$what did not finish within $timeoutSeconds seconds (a dialog is probably waiting for input)."
    }
    $process.ExitCode
}
# Runs Setup silently into $world.Game with $world.Documents standing in for the user's Documents
# folder (the hidden /FS25VR_DOCUMENTS switch) and $world.ExeName standing in for the game's exe name
# (the hidden /FS25VR_GAMEEXE switch). Returns Setup's exit code.
function Install-Fs25Vr($world, [string[]]$extraArgs = @()) {
    $setupArgs = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
              "/LOG=`"$($world.Root)\setup.log`"",
              "/DIR=`"$($world.Game)`"",
              "/FS25VR_DOCUMENTS=`"$($world.Documents)`"",
              "/FS25VR_GAMEEXE=$($world.ExeName)")
    $setupArgs += $extraArgs
    # A non-elevated run can only install per user; an elevated one does so when a case asks for /CURRENTUSER.
    if (-not $isAdmin -and $setupArgs -notcontains "/CURRENTUSER") { $setupArgs += "/CURRENTUSER" }
    Wait-GuardedProcess (Start-Process -FilePath $SetupExe -ArgumentList $setupArgs -PassThru) "Setup"
}

# Runs the registered uninstaller silently and waits until it is really gone: Inno's uninstaller
# hands over to a temporary copy of itself, so the first process can exit before the work is done.
function Uninstall-Fs25Vr {
    $entry = Get-UninstallEntry
    if (-not $entry) { throw "FS25 VR is not registered in Add/Remove Programs; nothing to uninstall." }
    $uninstaller = $entry.UninstallString.Trim('"')
    Wait-GuardedProcess (Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -PassThru) "Uninstaller" | Out-Null
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-UninstallEntry) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    if (Get-UninstallEntry) { throw "Uninstall did not finish within 60 seconds." }
    # The registry entry goes first; the uninstaller's own [Code] (restoring a backed-up version.dll, ...)
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
    $exit = Wait-GuardedProcess (Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -PassThru) "Uninstaller"
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

# Where the Installer records the Render Resolution it changed, for later installs and the uninstall restore:
# a text file next to the uninstaller (see the Render Resolution record in installer\fs25_vr.iss).
$recordPath = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) "FS25 VR\render-resolution.ini"
# Reads the record into a hashtable of key -> value, or $null when there is none.
function Get-RenderResolutionRecord {
    if (-not (Test-Path $recordPath)) { return $null }
    $record = @{}
    foreach ($line in [IO.File]::ReadAllLines($recordPath)) {
        if ($line -match '^([A-Za-z]+)=(.*)$') { $record[$Matches[1]] = $Matches[2] }
    }
    $record
}

# Windows' own crash dumps for the game: the per-application key under Windows Error Reporting's
# LocalDumps key in HKEY_LOCAL_MACHINE (64-bit view), and the Installer's record of what it wrote, next to
# the uninstaller. Every helper refuses the real game's exe name: the tests only ever use a throwaway one.
$localDumpsKeyPath = "SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps"
$crashDumpRecordPath = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) "FS25 VR\crash-dumps.ini"
function Assert-ThrowawayExeName([string]$exeName) {
    if ($exeName -notlike "fs25vr-test-*.exe") { throw "Refusing to touch the crash dump key of '$exeName': tests use a throwaway exe name only." }
}
# The 64-bit view of HKEY_LOCAL_MACHINE, however this script was started.
function Open-MachineKey([string]$subKey, [bool]$writable = $false) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
    try { $base.OpenSubKey($subKey, $writable) } finally { $base.Dispose() }
}
function Test-MachineKeyExists([string]$subKey) {
    $key = Open-MachineKey $subKey
    if ($key) { $key.Dispose() }
    $null -ne $key
}
# Removes a key only when it holds nothing (the test run's own tidying of a LocalDumps key it caused).
function Remove-EmptyMachineKey([string]$subKey) {
    $key = Open-MachineKey $subKey
    if (-not $key) { return }
    try { $empty = ($key.ValueCount -eq 0) -and ($key.SubKeyCount -eq 0) } finally { $key.Dispose() }
    if ($empty) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
        try { $base.DeleteSubKey($subKey, $false) } finally { $base.Dispose() }
    }
}
# What the per-application key holds: @{ Values = name -> @{ Kind; Data }; SubKeys = names }, or $null when
# there is no such key.
function Get-CrashDumpKey([string]$exeName) {
    Assert-ThrowawayExeName $exeName
    $key = Open-MachineKey "$localDumpsKeyPath\$exeName"
    if (-not $key) { return $null }
    try {
        $values = @{}
        foreach ($name in $key.GetValueNames()) {
            $values[$name] = [pscustomobject]@{ Kind = $key.GetValueKind($name).ToString(); Data = $key.GetValue($name) }
        }
        [pscustomobject]@{ Values = $values; SubKeys = @($key.GetSubKeyNames()) }
    } finally { $key.Dispose() }
}
# Creates or replaces a key as a player (or another tool) would have: @{ Name = value } (numbers become DWORDs).
function Set-CrashDumpKey([string]$exeName, [hashtable]$values) {
    Assert-ThrowawayExeName $exeName
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
    try {
        $key = $base.CreateSubKey("$localDumpsKeyPath\$exeName")
        try {
            foreach ($name in $values.Keys) {
                $v = $values[$name]
                if ($v -is [int]) { $key.SetValue($name, $v, [Microsoft.Win32.RegistryValueKind]::DWord) }
                else { $key.SetValue($name, $v, [Microsoft.Win32.RegistryValueKind]::String) }
            }
        } finally { $key.Dispose() }
    } finally { $base.Dispose() }
}
function Remove-CrashDumpKey([string]$exeName) {
    Assert-ThrowawayExeName $exeName
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
    try { $base.DeleteSubKeyTree("$localDumpsKeyPath\$exeName", $false) } finally { $base.Dispose() }
}
# Reads the Installer's crash dump record into a hashtable of key -> value, or $null when there is none.
function Get-CrashDumpRecord {
    if (-not (Test-Path $crashDumpRecordPath)) { return $null }
    $record = @{}
    foreach ($line in [IO.File]::ReadAllLines($crashDumpRecordPath)) {
        if ($line -match '^([A-Za-z]+)=(.*)$') { $record[$Matches[1]] = $Matches[2] }
    }
    $record
}

# Always leave the machine clean, whether or not the case passed.
function Remove-TestWorld($world) {
    Stop-StandInGame      # a running game would (rightly) block the cleanup uninstall
    if (Get-UninstallEntry) { try { Uninstall-Fs25Vr } catch { Write-Warning "Cleanup uninstall failed: $_" } }
    Remove-Item $recordPath -Force -ErrorAction SilentlyContinue   # in case a case failed before the uninstall removed it
    Remove-Item $crashDumpRecordPath -Force -ErrorAction SilentlyContinue
    if ($isAdmin) { try { Remove-CrashDumpKey $world.ExeName } catch { Write-Warning "Cleanup of the crash dump key failed: $_" } }
    Remove-Item $world.Root -Recurse -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------------------------

function Assert-True([bool]$condition, [string]$message) { if (-not $condition) { throw $message } }
# Runs Setup silently (see Install-Fs25Vr) and fails the case unless it exits with 0.
function Assert-InstallSucceeds($world, [string[]]$extraArgs = @(), [string]$what = "Setup") {
    $exit = Install-Fs25Vr $world $extraArgs
    Assert-True ($exit -eq 0) "$what exited with $exit."
}
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
$passed = 0
$skipped = 0
# $data (optional) is handed to the body as its second argument, for cases generated in a loop.
# -Needs Elevated marks a case that writes or reads machine-wide state only an elevated Setup can write. In a
# non-elevated run it is reported as [SKIP], not [PASS], and its body does not run. (Every other case runs in
# both kinds of run; the per-user case drives /CURRENTUSER itself so that elevated CI runs it too.)
function Test-Case([string]$name, [scriptblock]$body, $data = $null, [ValidateSet('', 'Elevated')][string]$Needs = '') {
    if ($Filter -and $name -notlike $Filter) { return }
    if ($Needs -eq 'Elevated' -and -not $isAdmin) {
        Write-Host "[SKIP] $name -- needs an elevated run"
        $script:skipped++
        return
    }
    $world = New-TestWorld
    try {
        & $body $world $data
        Write-Host "[PASS] $name"
        $script:passed++
    } catch {
        Write-Host "[FAIL] $name -- $($_.Exception.Message)"
        $failures.Add($name)
    } finally {
        Remove-TestWorld $world
    }
}

# Refuse to run over a real FS25 VR install: the fixed application ID means we would upgrade it.
if (Get-UninstallEntry) { Write-Error "FS25 VR is already installed on this machine; uninstall it before running the installer tests." }

# Whether Windows Error Reporting's LocalDumps key was there before this run. An install creates it when it
# is not (and the uninstall takes it away again); if a failing case left it behind, the end of the run does.
$localDumpsExistedAtStart = Test-MachineKeyExists $localDumpsKeyPath
Write-Host "This run is $(if ($isAdmin) { 'elevated' } else { 'not elevated' }); the LocalDumps key is $(if ($localDumpsExistedAtStart) { 'there' } else { 'not there' }) before it."

# ---------------------------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------------------------

Test-Case "Clean install places the Injector and the Companion Mod and leaves game.xml alone" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)

    Assert-InstallSucceeds $world

    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
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
    Assert-InstallSucceeds $world
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

Test-Case "Uninstall removes the Injector's crash dump along with fs25_vr.log" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world
    # What a crash of the game leaves next to the Injector: the log and a minidump.
    Set-Content -Path $p.Log -Value "written by the Injector while the game ran" -Encoding ascii
    [IO.File]::WriteAllBytes($p.Dump, [byte[]](0x4D, 0x44, 0x4D, 0x50, 0x93, 0xA7, 0x00, 0x00) + (1..200))
    # Any other file in the game's x64 folder is not the Installer's to remove.
    $other = Join-Path $world.Game "x64\other-tool.dmp"
    [IO.File]::WriteAllBytes($other, [byte[]](1..16))

    Uninstall-Fs25Vr

    Assert-Missing $p.Log "fs25_vr.log"
    Assert-Missing $p.Dump "The Injector's crash dump (fs25_vr_crash.dmp)"
    Assert-Exists $other "A dump of another tool in the x64 folder"
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
    Assert-InstallSucceeds $world @() "Setup after the game closed"
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
}

Test-Case "A silent uninstall while the game is running fails and changes nothing" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)
    Start-StandInGame $world

    $exit = Invoke-Uninstaller
    Assert-True ($exit -ne 0) "The uninstaller succeeded while Farming Simulator 25 was running."

    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
    Assert-True ($null -ne (Get-UninstallEntry)) "A refused uninstall removed the Add/Remove Programs entry."

    # Once the game has closed, the same uninstall goes through.
    Stop-StandInGame
    Uninstall-Fs25Vr
    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
}

# --- Mods directory override ----------------------------------------------------------------
# The override lives in gameSettings.xml (<modsDirectoryOverride active="..." directory="..."/>), not in
# game.xml. game.xml only holds the display settings the Render Resolution step edits.

# A gameSettings.xml whose mods directory override element is set as given (the rest is the sample baseline).
function New-GameSettingsWithOverride([string]$active, [string]$directory) {
    Assert-True $sampleGameSettingsXml.Contains($sampleOverrideElement) "The sample gameSettings.xml no longer holds the expected override element."
    $sampleGameSettingsXml.Replace($sampleOverrideElement, "<modsDirectoryOverride active=`"$active`" directory=`"$directory`"/>")
}

Test-Case "Active mods directory override in gameSettings.xml: the mod lands in the override folder, not the default one" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"         # spaces on purpose; does not exist yet
    New-FakeDocuments $world $sampleGameXml (New-GameSettingsWithOverride "true" $override)
    $p = Get-GamePaths $world
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)
    $settingsBefore = [IO.File]::ReadAllBytes($p.GameSettingsXml)

    Assert-InstallSucceeds $world

    Assert-SameFile (Join-Path $override "FS25_VRCompanion.zip") $stagedMod "Companion Mod zip in the override folder"
    Assert-Missing $p.ModZip "Companion Mod zip in the default mods folder"
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-SameBytes $p.GameSettingsXml $settingsBefore "gameSettings.xml (the override element)"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
}

Test-Case "Uninstall removes the mod from the override folder and leaves the override alone" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"
    New-FakeDocuments $world $sampleGameXml (New-GameSettingsWithOverride "true" $override)
    $p = Get-GamePaths $world
    $settingsBefore = [IO.File]::ReadAllBytes($p.GameSettingsXml)
    Assert-InstallSucceeds $world
    Assert-Exists (Join-Path $override "FS25_VRCompanion.zip") "Companion Mod zip in the override folder"

    Uninstall-Fs25Vr

    Assert-Missing (Join-Path $override "FS25_VRCompanion.zip") "Companion Mod zip in the override folder"
    Assert-Missing $p.ModZip "Companion Mod zip in the default mods folder"
    Assert-Missing $p.Injector "Injector"
    Assert-SameBytes $p.GameSettingsXml $settingsBefore "gameSettings.xml (the override element)"
}

Test-Case "Inactive mods directory override with a directory set: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"
    New-FakeDocuments $world $sampleGameXml (New-GameSettingsWithOverride "false" $override)
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
    Assert-Missing (Join-Path $override "FS25_VRCompanion.zip") "Companion Mod zip in the inactive override folder"
}

Test-Case "No mods directory override element at all in gameSettings.xml: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world $sampleGameXml ($sampleGameSettingsXml.Replace($sampleOverrideElement, ''))
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
}

Test-Case "No gameSettings.xml at all: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world $sampleGameXml $null
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
    Assert-Missing $p.GameSettingsXml "gameSettings.xml"
}

Test-Case "Active override with an empty directory: the default mods folder is used" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world $sampleGameXml (New-GameSettingsWithOverride "true" "")
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
}

Test-Case "An override element in game.xml is ignored: only gameSettings.xml holds the mods directory override" {
    param($world)
    New-FakeGame $world
    $elsewhere = Join-Path $world.Root "Wrong place"
    $gameXmlWithOverride = $sampleGameXml.Replace("<graphic>", "<modsDirectoryOverride active=`"true`" directory=`"$elsewhere`"/>`r`n    <graphic>")
    Assert-True ($gameXmlWithOverride -ne $sampleGameXml) "The sample game.xml no longer holds the expected <graphic> element."
    New-FakeDocuments $world $gameXmlWithOverride
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world

    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip in the default mods folder"
    Assert-Missing (Join-Path $elsewhere "FS25_VRCompanion.zip") "Companion Mod zip in the folder named by game.xml"
}

Test-Case "Active override plus a Render Resolution: the mod goes to the override, game.xml gets the resolution, gameSettings.xml is unchanged" {
    param($world)
    New-FakeGame $world
    $override = Join-Path $world.Root "Moved mods"
    New-FakeDocuments $world $sampleGameXml (New-GameSettingsWithOverride "true" $override)
    $p = Get-GamePaths $world
    $settingsBefore = [IO.File]::ReadAllBytes($p.GameSettingsXml)

    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500")

    Assert-SameFile (Join-Path $override "FS25_VRCompanion.zip") $stagedMod "Companion Mod zip in the override folder"
    Assert-SameBytes $p.GameSettingsXml $settingsBefore "gameSettings.xml"
    $gameXml = [IO.File]::ReadAllText($p.GameXml)
    Assert-True ($gameXml.Contains("<width>4000</width>") -and $gameXml.Contains("<height>2500</height>") -and $gameXml.Contains("<fullscreenMode>windowed</fullscreenMode>")) `
        "game.xml does not hold the Render Resolution 4000x2500 windowed."
    Assert-Exists $p.GameXmlBackup "game.xml.fs25vr-backup"
}
# ---------------------------------------------------------------------------------------------
# Existing version.dll and upgrade helpers
# ---------------------------------------------------------------------------------------------

# Runs an Installer exe silently like Install-Fs25Vr, but for any Setup exe, and optionally without
# /DIR so the Installer has to pick the game folder itself (the previously used one on an upgrade).
function Invoke-SetupExe([string]$exe, $world, [string[]]$extraArgs = @(), [switch]$NoDir) {
    $setupArgs = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
              "/LOG=`"$($world.Root)\setup-$([guid]::NewGuid().ToString('N').Substring(0, 6)).log`"",
              "/FS25VR_DOCUMENTS=`"$($world.Documents)`"",
              "/FS25VR_GAMEEXE=$($world.ExeName)")
    if (-not $NoDir) { $setupArgs += "/DIR=`"$($world.Game)`"" }
    if (-not $isAdmin) { $setupArgs += "/CURRENTUSER" }
    $setupArgs += $extraArgs
    Wait-GuardedProcess (Start-Process -FilePath $exe -ArgumentList $setupArgs -PassThru) "Setup"
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

# Marker text the Installer logs when it moves a foreign version.dll aside.
$foreignWarning = "not part of FS25 VR"

# A real version.dll that is not ours: the copy of Windows' own one (it has a version resource, just a different product).
function Get-ForeignDllBytes { [IO.File]::ReadAllBytes((Join-Path $env:SystemRoot "System32\version.dll")) }

# A dxgi.dll that is not ours, standing in for ReShade's: the copy of Windows' own one.
function Get-ForeignDxgiBytes { [IO.File]::ReadAllBytes((Join-Path $env:SystemRoot "System32\dxgi.dll")) }

# ---------------------------------------------------------------------------------------------
# Cases: existing version.dll and upgrade
# ---------------------------------------------------------------------------------------------

Test-Case "A foreign version.dll is warned about, backed up on install and restored on uninstall" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $foreign = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)

    Assert-InstallSucceeds $world
    Assert-True ((Get-LastSetupLog $world) -match $foreignWarning) "Setup did not warn about the foreign version.dll."
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-SameBytes $backup $foreign "version.dll.fs25vr-backup (the foreign version.dll)"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.Injector $foreign "The foreign version.dll restored on uninstall"
    Assert-Missing $backup "version.dll.fs25vr-backup after uninstall"
}

Test-Case "Our own older version.dll is overwritten with no warning and no backup" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    # An older build of ours: same version resource, different bytes.
    [IO.File]::WriteAllBytes($p.Injector, [byte[]]([IO.File]::ReadAllBytes($stagedInjector) + [byte[]](1..64)))

    Assert-InstallSucceeds $world
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-Missing "$($p.Injector).fs25vr-backup" "A version.dll.fs25vr-backup of our own older Injector"
    Assert-True (-not ((Get-LastSetupLog $world) -match $foreignWarning)) "Setup warned about our own version.dll."
}

# ---------------------------------------------------------------------------------------------
# Cases: the dxgi.dll of FS25 VR up to 1.1.9, and a dxgi.dll that is not ours (ReShade's)
# ---------------------------------------------------------------------------------------------

# An earlier Injector of ours: the staged one's version resource, different bytes.
function Get-EarlierInjectorBytes { [byte[]]([IO.File]::ReadAllBytes($stagedInjector) + [byte[]](1..64)) }

Test-Case "A dxgi.dll that is not ours (ReShade's) is left where it is by install and uninstall" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $reshade = Get-ForeignDxgiBytes
    [IO.File]::WriteAllBytes($p.EarlierInjector, $reshade)

    Assert-InstallSucceeds $world
    Assert-True (-not ((Get-LastSetupLog $world) -match $foreignWarning)) "Setup warned about a dxgi.dll, which FS25 VR no longer uses."
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-SameBytes $p.EarlierInjector $reshade "The dxgi.dll that is not ours, after install"
    Assert-Missing "$($p.EarlierInjector).fs25vr-backup" "dxgi.dll.fs25vr-backup after install"

    Uninstall-Fs25Vr

    Assert-Missing $p.Injector "Injector"
    Assert-SameBytes $p.EarlierInjector $reshade "The dxgi.dll that is not ours, after uninstall"
    Assert-Missing "$($p.EarlierInjector).fs25vr-keep" "dxgi.dll.fs25vr-keep after uninstall"
}

Test-Case "An upgrade from the dxgi.dll Injector deletes it and puts back the dxgi.dll it had moved aside" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.EarlierInjector).fs25vr-backup"
    $reshade = Get-ForeignDxgiBytes
    [IO.File]::WriteAllBytes($p.EarlierInjector, (Get-EarlierInjectorBytes))
    [IO.File]::WriteAllBytes($backup, $reshade)

    Assert-InstallSucceeds $world
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-SameBytes $p.EarlierInjector $reshade "The dxgi.dll put back from dxgi.dll.fs25vr-backup"
    Assert-Missing $backup "dxgi.dll.fs25vr-backup after the upgrade"

    Uninstall-Fs25Vr

    Assert-Missing $p.Injector "Injector"
    Assert-SameBytes $p.EarlierInjector $reshade "The put-back dxgi.dll after uninstall"
}

Test-Case "An upgrade from the dxgi.dll Injector with nothing moved aside leaves no dxgi.dll" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    [IO.File]::WriteAllBytes($p.EarlierInjector, (Get-EarlierInjectorBytes))

    Assert-InstallSucceeds $world
    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-Missing $p.EarlierInjector "The earlier FS25 VR dxgi.dll"
}

# An Injector from before the version resource (the v1.0.0 zip bundle carries "fs25_vr.log" as ASCII,
# later builds as UTF-16) is ours too, and was a dxgi.dll: the upgrade deletes it like any earlier Injector.
foreach ($encoding in @("ASCII", "UTF-16")) {
    Test-Case "An FS25 VR dxgi.dll from before the Installer ($encoding log name, no version resource) is deleted with no warning" {
        param($world, $encoding)
        New-FakeGame $world
        New-FakeDocuments $world
        $p = Get-GamePaths $world
        $text = if ($encoding -eq "ASCII") { [Text.Encoding]::ASCII } else { [Text.Encoding]::Unicode }
        [IO.File]::WriteAllBytes($p.EarlierInjector, [byte[]]((1..200) + $text.GetBytes("fs25_vr.log") + (1..200)))

        Assert-InstallSucceeds $world
        Assert-True (-not ((Get-LastSetupLog $world) -match $foreignWarning)) "Setup warned about an FS25 VR Injector from before the Installer."
        Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
        Assert-Missing $p.EarlierInjector "The FS25 VR dxgi.dll from before the Installer"
        Assert-Missing "$($p.EarlierInjector).fs25vr-backup" "A dxgi.dll.fs25vr-backup of an FS25 VR Injector"
    } $encoding
}

Test-Case "A failed install leaves the dxgi.dll Injector and what it moved aside as they were" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.EarlierInjector).fs25vr-backup"
    $earlier = Get-EarlierInjectorBytes
    $reshade = Get-ForeignDxgiBytes
    [IO.File]::WriteAllBytes($p.EarlierInjector, $earlier)
    [IO.File]::WriteAllBytes($backup, $reshade)
    # A file where the mods folder must go: the Companion Mod cannot be placed, so Setup rolls back.
    Set-Content -Path $p.DefaultMods -Value "in the way" -Encoding ascii

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup succeeded although the Companion Mod could not be placed."
    Assert-SameBytes $p.EarlierInjector $earlier "The earlier FS25 VR dxgi.dll after a rolled-back install"
    Assert-SameBytes $backup $reshade "dxgi.dll.fs25vr-backup after a rolled-back install"
    Assert-Missing $p.Injector "Injector after a rolled-back install"
}

Test-Case "A version.dll that replaced ours after install survives the uninstall" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world
    $replacement = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $replacement)      # e.g. another mod's version.dll installed over FS25 VR

    Uninstall-Fs25Vr

    Assert-SameBytes $p.Injector $replacement "The version.dll that replaced ours"
    Assert-Missing "$($p.Injector).fs25vr-keep" "version.dll.fs25vr-keep after uninstall"
    Assert-Missing "$($p.Injector).fs25vr-backup" "version.dll.fs25vr-backup after uninstall"
}

Test-Case "A version.dll that replaced ours wins over the one moved aside at install, which stays as the backup" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $original = [byte[]](1..200)
    [IO.File]::WriteAllBytes($p.Injector, $original)
    Assert-InstallSucceeds $world
    Assert-SameBytes $backup $original "version.dll.fs25vr-backup after install"
    $replacement = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $replacement)

    Uninstall-Fs25Vr

    Assert-SameBytes $p.Injector $replacement "The version.dll that replaced ours"
    Assert-SameBytes $backup $original "The version.dll moved aside at install, still under its backup name"
    Assert-Missing "$($p.Injector).fs25vr-keep" "version.dll.fs25vr-keep after uninstall"
}

Test-Case "A failure while placing files puts a foreign version.dll that was moved aside back" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $foreign = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)
    # A file where the mods folder must go: the Companion Mod cannot be placed, so Setup rolls back after
    # the foreign version.dll was moved aside.
    Set-Content -Path $p.DefaultMods -Value "in the way" -Encoding ascii

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup succeeded although the Companion Mod could not be placed."
    Assert-SameBytes $p.Injector $foreign "The foreign version.dll after a rolled-back install"
    Assert-Missing "$($p.Injector).fs25vr-backup" "version.dll.fs25vr-backup after a rolled-back install"
    Assert-True ($null -eq (Get-UninstallEntry)) "A rolled-back install was registered in Add/Remove Programs."
}

Test-Case "An existing version.dll.fs25vr-backup survives an upgrade untouched and is still restored on uninstall" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $foreign = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)
    Assert-InstallSucceeds $world @() "First Setup"
    Assert-SameBytes $backup $foreign "version.dll.fs25vr-backup after the first install"

    $newer = New-NewerSetup $world
    $exit = Invoke-SetupExe $newer $world
    Assert-True ($exit -eq 0) "Upgrade Setup exited with $exit."
    Assert-True (-not ((Get-LastSetupLog $world) -match $foreignWarning)) "The upgrade warned about a foreign version.dll although only ours is installed."
    Assert-SameFile $p.Injector $stagedInjector "Injector after the upgrade"
    Assert-SameBytes $backup $foreign "version.dll.fs25vr-backup after the upgrade"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.Injector $foreign "The original foreign version.dll restored on uninstall"
    Assert-Missing $backup "version.dll.fs25vr-backup after uninstall"
}

Test-Case "A foreign version.dll never replaces an existing version.dll.fs25vr-backup" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $original = [byte[]](1..200)
    $another = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($backup, $original)
    [IO.File]::WriteAllBytes($p.Injector, $another)

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup went on although it had nowhere safe to move the foreign version.dll."
    Assert-SameBytes $backup $original "The existing version.dll.fs25vr-backup"
    Assert-SameBytes $p.Injector $another "The foreign version.dll"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-True ($null -eq (Get-UninstallEntry)) "A refused install was still registered in Add/Remove Programs."
}

Test-Case "A newer Installer upgrades under one Add/Remove Programs entry and pre-selects the previous game folder" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world @() "First Setup"
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

# Game installation detection reads the real machine, so only the Game Pass rejection is automated;
# the rest is docs\installer-release-checklist.md. A silent run must refuse a Store folder with a
# logged reason, never a message box (Wait-GuardedProcess fails the case if a dialog hangs Setup).
Test-Case "A Game Pass folder (XboxGames\...\Content with MicrosoftGame.config) is refused as unsupported" {
    param($world)
    $world.Game = Join-Path $world.Root "XboxGames\Farming Simulator 25\Content"
    New-FakeGame $world
    Set-Content -Path (Join-Path $world.Game "MicrosoftGame.config") -Value "<Game/>" -Encoding ascii
    New-FakeDocuments $world
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup accepted a Game Pass folder."
    $setupLog = Get-Content "$($world.Root)\setup.log" -Raw
    Assert-True ($setupLog -match "Game Pass installations of Farming Simulator 25 are not supported") "Setup did not say that Game Pass is unsupported."

    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-True ($null -eq (Get-UninstallEntry)) "A refused install was still registered in Add/Remove Programs."
}

Test-Case "A Microsoft Store folder under WindowsApps is refused as unsupported" {
    param($world)
    $world.Game = Join-Path $world.Root "WindowsApps\GIANTSSoftware.FarmingSimulator25_1.0_x64__abc"
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -ne 0) "Setup accepted a Microsoft Store folder."
    Assert-True ((Get-Content "$($world.Root)\setup.log" -Raw) -match "are not supported") "Setup did not say that the Microsoft Store version is unsupported."
    Assert-Missing $p.Injector "Injector"
}

# --- Render Resolution ---------------------------------------------------------------------------

Test-Case "Render Resolution WxH writes width, height and windowed, changes nothing else, backs up game.xml once and records the values" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)

    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500")

    $expected = Get-GameXmlBytes (New-RealGameXml "4000" "2500" "windowed")
    Assert-SameBytes $p.GameXml $expected "game.xml (only width, height and fullscreenMode may differ)"
    Assert-SameBytes $p.GameXmlBackup $original "game.xml.fs25vr-backup (the untouched original)"

    $r = Get-RenderResolutionRecord
    Assert-True ($null -ne $r) "No Render Resolution record was written at $recordPath."
    Assert-True ($r.GameXml -eq $p.GameXml) "Record GameXml is '$($r.GameXml)', expected '$($p.GameXml)'."
    Assert-True ($r.OriginalWidth -eq "2560" -and $r.OriginalHeight -eq "1440" -and $r.OriginalFullscreenMode -eq "fullscreen") `
        "Record originals are $($r.OriginalWidth)x$($r.OriginalHeight) $($r.OriginalFullscreenMode), expected 2560x1440 fullscreen."
    Assert-True ($r.WrittenWidth -eq "4000" -and $r.WrittenHeight -eq "2500" -and $r.WrittenFullscreenMode -eq "windowed") `
        "Record written values are $($r.WrittenWidth)x$($r.WrittenHeight) $($r.WrittenFullscreenMode), expected 4000x2500 windowed."
}

Test-Case "Without /FS25VR_RESOLUTION game.xml is untouched and no backup or record is made" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)

    Assert-InstallSucceeds $world

    Assert-SameBytes $p.GameXml $original "game.xml"
    Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "A Render Resolution record was written although no resolution was applied."
}

Test-Case "Render Resolution accepts the 640 and 8192 limits" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=640x8192")
    Assert-SameBytes $p.GameXml (Get-GameXmlBytes (New-RealGameXml "640" "8192" "windowed")) "game.xml"
}

Test-Case "Render Resolution values that are not whole numbers from 640 to 8192 are rejected before anything is placed" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)

    foreach ($bad in @("639x1440", "2560x639", "8193x1440", "2560x8193", "0x0", "abcxdef", "2560x", "x1440", "2560",
                       "2560x1440x3", "25.5x1440", "-2560x1440", "2560x 1440", "1e3x1440", "99999999999x1440")) {
        $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=`"$bad`"")
        Assert-True ($exit -ne 0) "Setup accepted /FS25VR_RESOLUTION=$bad."
        Assert-SameBytes $p.GameXml $original "game.xml after /FS25VR_RESOLUTION=$bad"
        Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup after /FS25VR_RESOLUTION=$bad"
        Assert-Missing $p.Injector "Injector after /FS25VR_RESOLUTION=$bad"
        Assert-True ($null -eq (Get-RenderResolutionRecord)) "A record was written for /FS25VR_RESOLUTION=$bad."
        Assert-True ($null -eq (Get-UninstallEntry)) "A rejected install was registered for /FS25VR_RESOLUTION=$bad."
    }
}

Test-Case "With no game.xml the install succeeds, the resolution is skipped and no game.xml is created" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world $null          # My Games and modSettings exist, game.xml does not
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500")

    Assert-Missing $p.GameXml "game.xml"
    Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "A record was written although there was no game.xml."
    Assert-SameFile $p.Injector $stagedInjector "Injector"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip"
}

Test-Case "A game.xml without display width, height and fullscreenMode fails the install and is left alone" {
    param($world)
    New-FakeGame $world
    $p = Get-GamePaths $world
    New-Item -ItemType Directory -Path $p.MyGames -Force | Out-Null
    $odd = [Text.Encoding]::UTF8.GetBytes("<game><graphic><display><brightness>1</brightness></display></graphic></game>")
    [IO.File]::WriteAllBytes($p.GameXml, $odd)

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-True ($exit -ne 0) "Setup applied a resolution to a game.xml with no width, height and fullscreenMode."
    Assert-SameBytes $p.GameXml $odd "game.xml"
    Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup"
    Assert-Missing $p.Injector "Injector"
}

# A failed install must leave the player's own version.dll (another mod's) exactly where it was: under its
# original name, with no backup made, because nothing is registered to put it back.
function Assert-ForeignDllUntouched($world, [byte[]]$foreign, [string]$when) {
    $p = Get-GamePaths $world
    Assert-SameBytes $p.Injector $foreign "The foreign version.dll in x64 $when"
    Assert-Missing "$($p.Injector).fs25vr-backup" "version.dll.fs25vr-backup $when"
    Assert-True ($null -eq (Get-UninstallEntry)) "A failed install was registered in Add/Remove Programs $when."
}

Test-Case "A rejected /FS25VR_RESOLUTION leaves a foreign version.dll where it was and makes no backup" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world
    $foreign = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=639x1440")
    Assert-True ($exit -ne 0) "Setup accepted /FS25VR_RESOLUTION=639x1440."
    Assert-ForeignDllUntouched $world $foreign "after a rejected /FS25VR_RESOLUTION"
}

Test-Case "A game.xml without display elements leaves a foreign version.dll where it was and makes no backup" {
    param($world)
    New-FakeGame $world
    $p = Get-GamePaths $world
    New-Item -ItemType Directory -Path $p.MyGames -Force | Out-Null
    [IO.File]::WriteAllBytes($p.GameXml, [Text.Encoding]::UTF8.GetBytes("<game><graphic><display><brightness>1</brightness></display></graphic></game>"))
    $foreign = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-True ($exit -ne 0) "Setup applied a resolution to a game.xml with no display elements."
    Assert-ForeignDllUntouched $world $foreign "after a game.xml without display elements"
}

Test-Case "A failure while placing files leaves game.xml unedited and no Render Resolution record" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)
    # A file where the mods folder must go: the Companion Mod cannot be placed, so Setup fails after validation.
    Set-Content -Path $p.DefaultMods -Value "in the way" -Encoding ascii

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-True ($exit -ne 0) "Setup succeeded although the Companion Mod could not be placed."
    Assert-SameBytes $p.GameXml $original "game.xml after a failed install"
    Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup after a failed install"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "A Render Resolution record was written for a failed install."
}

# Exit code Setup ends with when the files are placed but game.xml could not be edited (fs25_vr.iss).
$renderResolutionFailedExitCode = 20

Test-Case "A game.xml that can't be written after the files are placed keeps the install, leaves game.xml and the record alone, and exits with 20" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)
    Set-ItemProperty -Path $p.GameXml -Name IsReadOnly -Value $true     # readable, so validation passes

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-True ($exit -eq $renderResolutionFailedExitCode) "Setup exited with $exit, expected $renderResolutionFailedExitCode."
    Assert-True ((Get-LastSetupLog $world) -match "The Render Resolution was not set") "Setup did not log that the Render Resolution was not set."
    Assert-SameBytes $p.GameXml $original "game.xml"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "A Render Resolution record names values game.xml does not hold."
    Assert-SameFile $p.Injector $stagedInjector "Injector (the install is kept)"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip (the install is kept)"
    Assert-True ($null -ne (Get-UninstallEntry)) "The kept install is not registered in Add/Remove Programs."
}

Test-Case "A game.xml that can't be written on a second apply keeps the first record, so uninstall still restores the originals" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500") "First Setup"
    Set-ItemProperty -Path $p.GameXml -Name IsReadOnly -Value $true

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=3000x2000")
    Assert-True ($exit -eq $renderResolutionFailedExitCode) "Second Setup exited with $exit, expected $renderResolutionFailedExitCode."
    $r = Get-RenderResolutionRecord
    Assert-True ($null -ne $r -and $r.WrittenWidth -eq "4000" -and $r.WrittenHeight -eq "2500") "The record no longer describes the first apply."

    Set-ItemProperty -Path $p.GameXml -Name IsReadOnly -Value $false
    Uninstall-Fs25Vr
    Assert-SameBytes $p.GameXml $original "game.xml after uninstall (the originals restored)"
}

Test-Case "Applying again keeps the first backup and the first originals; applying after an in-game change records the new originals" {
    param($world)
    # A Documents path with non-ASCII characters checks that the record survives the round trip
    # (the Installer re-reads it to decide which originals to keep).
    $world.Documents = Join-Path $world.Root ("Dokumente " + [char]0xF6 + [char]0x20AC)
    New-FakeGame $world
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $firstOriginal = [IO.File]::ReadAllBytes($p.GameXml)

    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500") "First Setup"
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=5000x3000") "Second Setup"

    Assert-SameBytes $p.GameXml (Get-GameXmlBytes (New-RealGameXml "5000" "3000" "windowed")) "game.xml after the second apply"
    Assert-SameBytes $p.GameXmlBackup $firstOriginal "game.xml.fs25vr-backup (must still be the very first original)"
    $r = Get-RenderResolutionRecord
    Assert-True ($r.GameXml -eq $p.GameXml) "Record GameXml is '$($r.GameXml)', expected '$($p.GameXml)'."
    Assert-True ($r.OriginalWidth -eq "2560" -and $r.OriginalHeight -eq "1440" -and $r.OriginalFullscreenMode -eq "fullscreen") `
        "After a second apply the record's originals are $($r.OriginalWidth)x$($r.OriginalHeight) $($r.OriginalFullscreenMode), expected the first original 2560x1440 fullscreen."
    Assert-True ($r.WrittenWidth -eq "5000" -and $r.WrittenHeight -eq "3000" -and $r.WrittenFullscreenMode -eq "windowed") `
        "Record written values are $($r.WrittenWidth)x$($r.WrittenHeight) $($r.WrittenFullscreenMode), expected 5000x3000 windowed."

    # The player changes the resolution in the game, then applies again: those are the new originals.
    Set-RealGameXml $world "1920" "1080" "windowed_fullscreen"
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4500x2800") "Third Setup"
    $r = Get-RenderResolutionRecord
    Assert-True ($r.OriginalWidth -eq "1920" -and $r.OriginalHeight -eq "1080" -and $r.OriginalFullscreenMode -eq "windowed_fullscreen") `
        "After an in-game change the record's originals are $($r.OriginalWidth)x$($r.OriginalHeight) $($r.OriginalFullscreenMode), expected 1920x1080 windowed_fullscreen."
    Assert-SameBytes $p.GameXmlBackup $firstOriginal "game.xml.fs25vr-backup after the third apply"
}

# --- Uninstall restores the Render Resolution ---------------------------------------------------

Test-Case "Uninstall restores the original width, height and fullscreen mode when game.xml still holds what the Installer wrote" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world $null $null                    # only the modSettings VR settings
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500") "Setup"
    Assert-SameBytes $p.GameXml (Get-GameXmlBytes (New-RealGameXml "4000" "2500" "windowed")) "game.xml after install"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.GameXml $original "game.xml after uninstall (the originals restored, nothing else changed)"
    Assert-SameBytes $p.GameXmlBackup $original "game.xml.fs25vr-backup (kept)"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "The Render Resolution record was not deleted by the uninstall."
    Assert-Missing (Split-Path -Parent $recordPath) "The Installer's common application data folder"
    Assert-Exists $p.ModSettings "modSettings VR settings"
    Assert-Missing $p.Injector "Injector"
}

Test-Case "Uninstall restores the originals recorded by the first apply after a second apply" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500") "First Setup"
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=5000x3000") "Second Setup"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.GameXml $original "game.xml after uninstall"
}

# Each of the three values on its own: a change to any one means the player's choice wins.
foreach ($changed in @(
        @{ Name = "width";           W = "3000"; H = "2500"; Mode = "windowed" },
        @{ Name = "height";          W = "4000"; H = "1800"; Mode = "windowed" },
        @{ Name = "fullscreen mode"; W = "4000"; H = "2500"; Mode = "windowed_fullscreen" })) {
    Test-Case "Uninstall leaves game.xml alone when the $($changed.Name) was changed since install, and keeps the backup" {
        param($world, $c)
        New-FakeGame $world
        Set-RealGameXml $world "2560" "1440" "fullscreen"
        $p = Get-GamePaths $world
        $original = [IO.File]::ReadAllBytes($p.GameXml)
        Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500") "Setup"
        Set-RealGameXml $world $c.W $c.H $c.Mode          # the player changed it in the game
        $current = [IO.File]::ReadAllBytes($p.GameXml)

        Uninstall-Fs25Vr

        Assert-SameBytes $p.GameXml $current "game.xml after uninstall (the player's current $($c.Name))"
        Assert-SameBytes $p.GameXmlBackup $original "game.xml.fs25vr-backup (kept)"
        Assert-True ($null -eq (Get-RenderResolutionRecord)) "The Render Resolution record was not deleted by the uninstall."
        Assert-Missing $p.Injector "Injector"
    } $changed
}

Test-Case "Uninstall leaves game.xml alone when the Installer never set a Render Resolution" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world "4000" "2500" "windowed"       # the player's own values, even if they look like a headset size
    $p = Get-GamePaths $world
    $before = [IO.File]::ReadAllBytes($p.GameXml)
    Assert-InstallSucceeds $world @() "Setup"

    Uninstall-Fs25Vr

    Assert-SameBytes $p.GameXml $before "game.xml after uninstall"
    Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup (none was ever made)"
    Assert-Missing $p.Injector "Injector"
}

Test-Case "Uninstall copes with a game.xml that is gone and still removes the record" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500") "Setup"
    Remove-Item $p.GameXml -Force

    Uninstall-Fs25Vr

    Assert-Missing $p.GameXml "game.xml (the uninstall must not create it)"
    Assert-Exists $p.GameXmlBackup "game.xml.fs25vr-backup"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "The Render Resolution record was not deleted by the uninstall."
    Assert-Missing $p.Injector "Injector"
}

# --- Setup offers to uninstall --------------------------------------------------------------------
# Running the Setup exe while FS25 VR is installed asks (interactive runs only) whether to upgrade,
# uninstall or cancel; the question needs a person, so only the unattended parts are automated here
# (see docs\installer-release-checklist.md for the dialog). /FS25VR_UNINSTALL runs the existing
# uninstaller unattended through Setup; silent runs never ask and keep upgrading.

# Runs the Setup exe with exactly these arguments (plus /LOG, /FS25VR_DOCUMENTS and, outside an elevated
# shell, /CURRENTUSER) and returns its exit code. Unlike Install-Fs25Vr it does not add /SUPPRESSMSGBOXES
# or /DIR, so a message box the Installer should not show would hang and fail the case.
function Invoke-SetupWithArgs($world, [string[]]$setupArgs) {
    $all = @("/LOG=`"$($world.Root)\setup-$([guid]::NewGuid().ToString('N').Substring(0, 6)).log`"",
             "/FS25VR_DOCUMENTS=`"$($world.Documents)`"",
             "/FS25VR_GAMEEXE=$($world.ExeName)") + $setupArgs
    if (-not $isAdmin) { $all += "/CURRENTUSER" }
    Wait-GuardedProcess (Start-Process -FilePath $SetupExe -ArgumentList $all -PassThru) "Setup"
}
# Setup /FS25VR_UNINSTALL, run the way CI would. It must not need /DIR. Setup ends itself early for this
# switch, so also check that it left none of its temporary is-*.tmp folders behind. (The uninstaller's own
# is-*-uninstall.tmp folder is removed by its temporary copy a moment after Setup returns; not checked.)
function Get-SetupTempFolders {
    @(Get-ChildItem ([IO.Path]::GetTempPath()) -Directory -Filter "is-*.tmp" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike "*-uninstall.tmp" } | ForEach-Object FullName)
}
function Invoke-SetupUninstall($world, [string[]]$extraArgs = @()) {
    $before = Get-SetupTempFolders
    $exit = Invoke-SetupWithArgs $world (@("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/FS25VR_UNINSTALL") + $extraArgs)
    $left = @(Get-SetupTempFolders | Where-Object { $before -notcontains $_ })
    Assert-True ($left.Count -eq 0) "Setup /FS25VR_UNINSTALL left its temporary folder behind: $($left -join ', ')"
    $exit
}

Test-Case "Setup /FS25VR_UNINSTALL uninstalls: removes the Injector, the mod zip and fs25_vr.log, keeps modSettings and unregisters" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world
    Set-Content -Path $p.Log -Value "written by the Injector while the game ran" -Encoding ascii
    [IO.File]::WriteAllBytes($p.Dump, [byte[]](0x4D, 0x44, 0x4D, 0x50, 0x93, 0xA7, 0x00, 0x00) + (1..200))
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)

    $exit = Invoke-SetupUninstall $world
    Assert-True ($exit -eq 0) "Setup /FS25VR_UNINSTALL exited with $exit."

    # Checked right away: Setup must wait until the uninstaller is completely done.
    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-Missing $p.Log "fs25_vr.log"
    Assert-Missing $p.Dump "The Injector's crash dump (fs25_vr_crash.dmp)"
    Assert-Exists $p.ModSettings "modSettings VR settings"
    Assert-Exists (Join-Path $world.Game "x64\FarmingSimulator2025Game.exe") "The game executable"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml"
    Assert-True ($null -eq (Get-UninstallEntry)) "FS25 VR is still registered in Add/Remove Programs."
    Assert-Missing (Split-Path -Parent $recordPath) "The Installer's common application data folder (uninstaller and notices)"
}

Test-Case "Setup /FS25VR_UNINSTALL does what the uninstaller does: restores the foreign version.dll and the Render Resolution" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world $null $null
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $backup = "$($p.Injector).fs25vr-backup"
    $foreign = Get-ForeignDllBytes
    [IO.File]::WriteAllBytes($p.Injector, $foreign)
    $gameXmlBefore = [IO.File]::ReadAllBytes($p.GameXml)
    Assert-InstallSucceeds $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-SameBytes $p.GameXml (Get-GameXmlBytes (New-RealGameXml "4000" "2500" "windowed")) "game.xml after install"

    $exit = Invoke-SetupUninstall $world
    Assert-True ($exit -eq 0) "Setup /FS25VR_UNINSTALL exited with $exit."

    Assert-SameBytes $p.Injector $foreign "The foreign version.dll restored"
    Assert-Missing $backup "version.dll.fs25vr-backup"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-SameBytes $p.GameXml $gameXmlBefore "game.xml (the Render Resolution restored)"
    Assert-Exists $p.GameXmlBackup "game.xml.fs25vr-backup (kept)"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "The Render Resolution record was not deleted."
    Assert-Exists $p.ModSettings "modSettings VR settings"
    Assert-True ($null -eq (Get-UninstallEntry)) "FS25 VR is still registered in Add/Remove Programs."
}

Test-Case "Setup /FS25VR_UNINSTALL with FS25 VR not installed fails with a logged reason and changes nothing" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world

    $exit = Invoke-SetupUninstall $world        # a message box here would hang and fail the case
    Assert-True ($exit -ne 0) "Setup /FS25VR_UNINSTALL succeeded although FS25 VR is not installed."
    Assert-True ((Get-LastSetupLog $world) -match "FS25 VR is not installed") "Setup did not log that FS25 VR is not installed."

    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-True ($null -eq (Get-UninstallEntry)) "FS25 VR was registered by an uninstall request."
}

Test-Case "Setup /FS25VR_UNINSTALL while the game is running fails and changes nothing" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world
    Assert-InstallSucceeds $world
    Start-StandInGame $world

    $exit = Invoke-SetupUninstall $world
    Assert-True ($exit -ne 0) "Setup /FS25VR_UNINSTALL succeeded while Farming Simulator 25 was running."

    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip"
    Assert-True ($null -ne (Get-UninstallEntry)) "A refused uninstall removed the Add/Remove Programs entry."

    # Once the game has closed, the same request goes through.
    Stop-StandInGame
    $exit = Invoke-SetupUninstall $world
    Assert-True ($exit -eq 0) "Setup /FS25VR_UNINSTALL after the game closed exited with $exit."
    Assert-Missing $p.Injector "Injector"
    Assert-Missing $p.ModZip "Companion Mod zip"
    Assert-True ($null -eq (Get-UninstallEntry)) "FS25 VR is still registered in Add/Remove Programs."
}

# Silent runs never show the upgrade/uninstall question. These run WITHOUT /SUPPRESSMSGBOXES (which would
# answer the question for them), so a question shown anyway hangs Setup and fails the case.
foreach ($mode in @("/VERYSILENT", "/SILENT")) {
    Test-Case "Setup $mode over an existing install upgrades without asking" {
        param($world, $mode)
        New-FakeGame $world
        New-FakeDocuments $world
        $p = Get-GamePaths $world
        Assert-InstallSucceeds $world @() "First Setup"
        Remove-Item $p.Injector, $p.ModZip -Force      # make the second run visible

        $exit = Invoke-SetupWithArgs $world @($mode, "/NORESTART", "/DIR=`"$($world.Game)`"")
        Assert-True ($exit -eq 0) "Setup $mode over an existing install exited with $exit."

        Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
        Assert-SameFile $p.ModZip $stagedMod "Companion Mod zip"
        Assert-True ($null -ne (Get-UninstallEntry)) "FS25 VR is no longer registered after the upgrade."
    } $mode
}

# --- Windows' own crash dumps for the game --------------------------------------------------------
# The Installer turns them on by creating the game exe's key under Windows Error Reporting's LocalDumps key
# (HKEY_LOCAL_MACHINE, 64-bit view) and records what it wrote next to the uninstaller (docs\adr\0039-...).
# Only an administrative install can write it. Every Setup run here is given a throwaway exe name
# (/FS25VR_GAMEEXE), so the real game's key is never read or written; see the header of this script for
# why some of these cases are skipped outside an elevated run.

# Runs in an elevated shell too: /CURRENTUSER makes Setup install in the non administrative install mode there
# as well (Inno Setup, "Setup Command Line Parameters"), which registers under HKCU and cannot write the key.
Test-Case "A per-user install writes no Windows crash dump key, logs that the setting was skipped and still succeeds" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world @("/CURRENTUSER")

    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "A per-user run wrote the crash dump key for $($world.ExeName)."
    Assert-True ($null -eq (Get-CrashDumpRecord)) "A per-user run recorded a crash dump key it did not write."
    Assert-True ((Get-LastSetupLog $world) -match "Skipped the Windows crash dump setting") "Setup did not log that the Windows crash dump setting was skipped."
    $entry = Get-ItemProperty -Path $userUninstallKeyPath -ErrorAction SilentlyContinue
    Assert-True ($null -ne $entry) "The per-user install is not registered under HKCU."
}

# A silent run is answered by the command line alone: the wizard's Crash Dumps checkbox (ticked by default,
# not offered at all in a per-user run) takes no part in it. With the switch, the log names the switch in an
# elevated and in a per-user run alike, and no key and no record appear either way, not even by the uninstall.
# This one runs in both.
Test-Case "A silent install given /FS25VR_NOCRASHDUMPS logs that the switch left the Windows crash dump setting alone, writes no key and no record, succeeds, and the uninstall has nothing to remove" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $p = Get-GamePaths $world

    Assert-InstallSucceeds $world @("/FS25VR_NOCRASHDUMPS")

    Assert-SameFile $p.Injector $stagedInjector "Injector (version.dll in x64)"
    Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "The crash dump key was written although /FS25VR_NOCRASHDUMPS was given."
    Assert-True ($null -eq (Get-CrashDumpRecord)) "A crash dump record was written although /FS25VR_NOCRASHDUMPS was given."
    $log = Get-LastSetupLog $world
    Assert-True ($log -match "left alone because of /FS25VR_NOCRASHDUMPS") "Setup did not log that /FS25VR_NOCRASHDUMPS left the crash dump setting alone."
    Assert-True ($log -notmatch "Skipped the Windows crash dump setting") "Setup logged the per-user skip although the switch was given."

    Uninstall-Fs25Vr

    Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "A crash dump key appeared by the uninstall."
    Assert-True ($null -eq (Get-CrashDumpRecord)) "A crash dump record appeared by the uninstall."
}

# What the Installer writes for the game's exe, spelled out here from the spec and not from the Installer:
# a custom dump (DumpType 0) with the Injector's own dump contents (CustomDumpFlags 0x1060 =
# MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules, next to the
# stacks and the module list every minidump holds), 3 dumps kept, and no DumpFolder (Windows' default
# CrashDumps folder applies).
function Assert-CrashDumpKeyIsOurs($key, [string]$when) {
    Assert-True ($null -ne $key) "There is no crash dump key $when."
    $expected = [ordered]@{ DumpType = 0; CustomDumpFlags = 0x1060; DumpCount = 3 }
    $names = @($key.Values.Keys | Sort-Object)
    Assert-True (($names -join ",") -eq ((@($expected.Keys) | Sort-Object) -join ",")) "The crash dump key holds the values '$($names -join ', ')' $when, expected '$(@($expected.Keys) -join ', ')' only."
    foreach ($name in $expected.Keys) {
        $v = $key.Values[$name]
        Assert-True ($v.Kind -eq "DWord" -and $v.Data -eq $expected[$name]) "The crash dump key's $name is $($v.Kind) $($v.Data) $when, expected DWord $($expected[$name])."
    }
    Assert-True ($key.SubKeys.Count -eq 0) "The crash dump key has subkeys ${when}: $($key.SubKeys -join ', ')"
}

Test-Case "An install creates the game exe's crash dump key (custom dump, 3 dumps kept, no dump folder) and an uninstall removes it" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world

    Assert-InstallSucceeds $world

    Assert-CrashDumpKeyIsOurs (Get-CrashDumpKey $world.ExeName) "after the install"

    Uninstall-Fs25Vr

    Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "The crash dump key is still there after the uninstall."
} -Needs Elevated

# The key as one comparable line: every value (name, type, data) and every subkey name.
function Format-CrashDumpKey($key) {
    if ($null -eq $key) { return "(no key)" }
    $parts = foreach ($name in ($key.Values.Keys | Sort-Object)) { "$name=$($key.Values[$name].Kind):$($key.Values[$name].Data)" }
    $subs = @($key.SubKeys | Sort-Object) -join ","
    (@($parts) -join "; ") + " [subkeys: $subs]"
}
function Assert-SameCrashDumpKey($expected, $actual, [string]$what) {
    $e = Format-CrashDumpKey $expected
    $a = Format-CrashDumpKey $actual
    Assert-True ($e -eq $a) "$what changed: it was { $e } and is { $a }."
}

# A key that was there before the Installer: the player's own, or another tool's. Either way it is theirs.
foreach ($existing in @(
        @{ Name = "other values";                                Values = @{ DumpType = 2; DumpCount = 10; DumpFolder = "D:\My dumps" } },
        @{ Name = "exactly the values the Installer would write"; Values = @{ DumpType = 0; CustomDumpFlags = 0x1060; DumpCount = 3 } })) {
    Test-Case "A crash dump key that already exists for the game's exe ($($existing.Name)) is unchanged after install and after uninstall, and the log says it was left alone" {
        param($world, $e)
        New-FakeGame $world
        New-FakeDocuments $world
        Set-CrashDumpKey $world.ExeName $e.Values
        $before = Get-CrashDumpKey $world.ExeName

        Assert-InstallSucceeds $world

        Assert-SameCrashDumpKey $before (Get-CrashDumpKey $world.ExeName) "The existing crash dump key after the install"
        Assert-True ((Get-LastSetupLog $world) -match "already exists; leaving it alone") "Setup did not log that it left the existing crash dump key alone."
        Assert-True ($null -eq (Get-CrashDumpRecord)) "The Installer recorded a crash dump key it did not write."

        Uninstall-Fs25Vr

        Assert-SameCrashDumpKey $before (Get-CrashDumpKey $world.ExeName) "The existing crash dump key after the uninstall"
    } $existing -Needs Elevated
}

# Like the Render Resolution: the Installer records what it wrote next to the uninstaller, the uninstall
# removes the key only while it still holds exactly that, and the record is deleted either way.
Test-Case "The Installer records the crash dump key it wrote next to the uninstaller and the uninstall deletes the record and the folder" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world

    Assert-InstallSucceeds $world

    $record = Get-CrashDumpRecord
    Assert-True ($null -ne $record) "The Installer did not record the crash dump key it wrote ($crashDumpRecordPath)."
    Assert-True ($record["ExeName"] -eq $world.ExeName) "The record names '$($record["ExeName"])', expected the exe the key was written for ($($world.ExeName))."
    Assert-True (($record["WrittenDumpType"] -eq "0") -and ($record["WrittenCustomDumpFlags"] -eq "4192") -and ($record["WrittenDumpCount"] -eq "3")) `
        "The record does not hold the written values (DumpType 0, CustomDumpFlags 4192, DumpCount 3)."
    Assert-True ((Split-Path -Parent $crashDumpRecordPath) -eq (Split-Path -Parent (Get-UninstallEntry).UninstallString.Trim('"'))) "The record is not next to the uninstaller."

    Uninstall-Fs25Vr

    Assert-True ($null -eq (Get-CrashDumpRecord)) "The crash dump record was not deleted by the uninstall."
    Assert-Missing (Split-Path -Parent $crashDumpRecordPath) "The Installer's common application data folder"
} -Needs Elevated

# A change to the key by anyone after the install means it is theirs now: the uninstall leaves it.
foreach ($change in @(
        @{ Name = "a value was changed";        Apply = { Set-CrashDumpKey $args[0] @{ DumpCount = 10 } } },
        @{ Name = "a value was added";          Apply = { Set-CrashDumpKey $args[0] @{ DumpFolder = "D:\My dumps" } } },
        @{ Name = "a value was replaced by text"; Apply = { Set-CrashDumpKey $args[0] @{ DumpCount = "3" } } },
        @{ Name = "a value was removed";        Apply = { $k = Open-MachineKey "$localDumpsKeyPath\$($args[0])" $true; try { $k.DeleteValue("CustomDumpFlags") } finally { $k.Dispose() } } },
        @{ Name = "a subkey was added";         Apply = { $k = Open-MachineKey "$localDumpsKeyPath\$($args[0])" $true; try { $k.CreateSubKey("Extra").Dispose() } finally { $k.Dispose() } } })) {
    Test-Case "A crash dump key the player changed after the install ($($change.Name)) is still there after the uninstall, and the record is deleted" {
        param($world, $c)
        New-FakeGame $world
        New-FakeDocuments $world
        Assert-InstallSucceeds $world
        $ours = Get-CrashDumpKey $world.ExeName
        Assert-CrashDumpKeyIsOurs $ours "after the install"
        & $c.Apply $world.ExeName
        $changed = Get-CrashDumpKey $world.ExeName
        Assert-True ((Format-CrashDumpKey $changed) -ne (Format-CrashDumpKey $ours)) "The test did not change the key ($($c.Name))."

        Uninstall-Fs25Vr

        Assert-SameCrashDumpKey $changed (Get-CrashDumpKey $world.ExeName) "The player's crash dump key after the uninstall"
        Assert-True ($null -eq (Get-CrashDumpRecord)) "The crash dump record was not deleted by the uninstall."
    } $change -Needs Elevated
}

# An upgrade or reinstall applies the same rules: it keeps the key and its record while the key is still its
# own, writes the key when there is none, and leaves a key the player changed alone (and forgets the record,
# which no longer describes anything of ours).
Test-Case "An upgrade over an install that has the crash dump key keeps it and its record, and the later uninstall removes it" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    Assert-InstallSucceeds $world @() "First Setup"
    $recordBefore = Get-CrashDumpRecord
    Assert-CrashDumpKeyIsOurs (Get-CrashDumpKey $world.ExeName) "after the first install"

    $newer = New-NewerSetup $world
    $exit = Invoke-SetupExe $newer $world
    Assert-True ($exit -eq 0) "Upgrade Setup exited with $exit."

    Assert-CrashDumpKeyIsOurs (Get-CrashDumpKey $world.ExeName) "after the upgrade"
    $recordAfter = Get-CrashDumpRecord
    Assert-True ($null -ne $recordAfter -and (($recordAfter.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ";") -eq
        (($recordBefore.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ";")) "The crash dump record changed or went missing in the upgrade."
    Assert-True ((Get-LastSetupLog $world) -match "still the one this Installer made") "The upgrade did not log that it kept the crash dump key it had made."

    Uninstall-Fs25Vr

    Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "The crash dump key is still there after the uninstall that followed the upgrade."
    Assert-True ($null -eq (Get-CrashDumpRecord)) "The crash dump record was not deleted by the uninstall."
} -Needs Elevated

# The first install may have created the LocalDumps key itself (when the machine had none). The key being gone
# while LocalDumps stayed must not make the upgrade forget that: the uninstall after it still takes away what the
# install made, and leaves LocalDumps as the first install found it (ADR-0039, item 8). On a machine that had
# LocalDumps already, the same steps check that the upgrade does not start to remove a key that was never ours.
Test-Case "An upgrade writes the crash dump key again when it is gone, and the later uninstall removes it and leaves the LocalDumps key as the first install found it" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $existedBefore = Test-MachineKeyExists $localDumpsKeyPath
    Assert-InstallSucceeds $world @() "First Setup"
    Remove-CrashDumpKey $world.ExeName
    Assert-True (Test-MachineKeyExists $localDumpsKeyPath) "The LocalDumps key went with the game's key; the test needs it to stay."

    $newer = New-NewerSetup $world
    $exit = Invoke-SetupExe $newer $world
    Assert-True ($exit -eq 0) "Upgrade Setup exited with $exit."

    Assert-CrashDumpKeyIsOurs (Get-CrashDumpKey $world.ExeName) "after the upgrade"
    $record = Get-CrashDumpRecord
    Assert-True ($null -ne $record) "The upgrade did not record the crash dump key it wrote."
    if (-not $existedBefore) {
        Assert-True ($record["CreatedLocalDumpsKey"] -eq "1") "The upgrade forgot that the first install created the LocalDumps key (the record says CreatedLocalDumpsKey=$($record["CreatedLocalDumpsKey"]))."
    }

    Uninstall-Fs25Vr

    Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "The crash dump key is still there after the uninstall."
    Assert-True ($null -eq (Get-CrashDumpRecord)) "The crash dump record was not deleted by the uninstall."
    Assert-True ((Test-MachineKeyExists $localDumpsKeyPath) -eq $existedBefore) `
        "The LocalDumps key $(if ($existedBefore) { 'was there before the first install and is gone' } else { 'was not there before the first install and is still there' }) after the uninstall."
} -Needs Elevated

Test-Case "An upgrade over a crash dump key the player changed leaves it alone, and the later uninstall leaves it too" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    Assert-InstallSucceeds $world @() "First Setup"
    Set-CrashDumpKey $world.ExeName @{ DumpCount = 10 }
    $changed = Get-CrashDumpKey $world.ExeName

    $newer = New-NewerSetup $world
    $exit = Invoke-SetupExe $newer $world
    Assert-True ($exit -eq 0) "Upgrade Setup exited with $exit."

    Assert-SameCrashDumpKey $changed (Get-CrashDumpKey $world.ExeName) "The player's crash dump key after the upgrade"
    Assert-True ($null -eq (Get-CrashDumpRecord)) "The upgrade kept a crash dump record for a key that is no longer the Installer's."

    Uninstall-Fs25Vr

    Assert-SameCrashDumpKey $changed (Get-CrashDumpKey $world.ExeName) "The player's crash dump key after the uninstall"
} -Needs Elevated

# Writing the game exe's key creates the LocalDumps key when it is not there. Microsoft does not say what a
# LocalDumps key with nothing in it does (it may keep crash dumps on for the other applications too), so the
# Installer leaves the registry as it found it: it records that it created LocalDumps, and the uninstall takes
# it away again when it is still empty (docs\adr\0039-..., item 8).
Test-Case "An install and an uninstall leave the LocalDumps key as they found it" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $existedBefore = Test-MachineKeyExists $localDumpsKeyPath

    Assert-InstallSucceeds $world
    Assert-True (Test-MachineKeyExists $localDumpsKeyPath) "There is no LocalDumps key after the install."

    Uninstall-Fs25Vr

    Assert-True ((Test-MachineKeyExists $localDumpsKeyPath) -eq $existedBefore) `
        "The LocalDumps key $(if ($existedBefore) { 'was there before the install and is gone' } else { 'was not there before the install and is still there' }) after the uninstall."
} -Needs Elevated

Test-Case "The uninstall leaves the LocalDumps key alone when another application's key has appeared under it" {
    param($world)
    New-FakeGame $world
    New-FakeDocuments $world
    $existedBefore = Test-MachineKeyExists $localDumpsKeyPath
    $sibling = "fs25vr-test-sibling-" + [guid]::NewGuid().ToString("N").Substring(0, 8) + ".exe"
    Assert-InstallSucceeds $world
    try {
        Set-CrashDumpKey $sibling @{ DumpCount = 5 }

        Uninstall-Fs25Vr

        Assert-True (Test-MachineKeyExists "$localDumpsKeyPath\$sibling") "The uninstall removed another application's key under LocalDumps."
        Assert-True ($null -eq (Get-CrashDumpKey $world.ExeName)) "The crash dump key for the game is still there after the uninstall."
    } finally {
        Remove-CrashDumpKey $sibling
        if (-not $existedBefore) { Remove-EmptyMachineKey $localDumpsKeyPath }
    }
} -Needs Elevated

if ($isAdmin -and -not $localDumpsExistedAtStart) { try { Remove-EmptyMachineKey $localDumpsKeyPath } catch { Write-Warning "Cleanup of the LocalDumps key failed: $_" } }

Write-Host "Installer tests: $passed passed, $skipped skipped, $($failures.Count) failed."
if ($failures.Count -gt 0) {
    Write-Error "$($failures.Count) installer test(s) failed: $($failures -join '; ')"
}
if ($skipped -gt 0) {
    Write-Host "No installer test failed, but $skipped case(s) did not run here (see [SKIP] above): a skipped case is not a passed one."
    # CI is elevated, so nothing may be skipped there: a skip means the elevated cases are not running, and a green
    # run that skipped them would say nothing about the Windows crash dump setting.
    if ($env:GITHUB_ACTIONS -eq 'true') {
        Write-Error "$skipped installer test case(s) were skipped on GitHub Actions, which runs elevated; none may be (see [SKIP] above)."
    }
} else {
    Write-Host "All installer tests passed."
}
