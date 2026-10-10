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
        Injector     = Join-Path $world.Game "x64\dxgi.dll"
        Log          = Join-Path $world.Game "x64\fs25_vr.log"
        MyGames      = $myGames
        DefaultMods  = Join-Path $myGames "mods"
        ModZip       = Join-Path $myGames "mods\FS25_VRCompanion.zip"
        GameXml      = Join-Path $myGames "game.xml"
        GameXmlBackup = Join-Path $myGames "game.xml.fs25vr-backup"
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
}

# Where the Installer records the Render Resolution it changed, for the uninstall restore (#69): a text file
# next to the uninstaller (see the Render Resolution record in installer\fs25_vr.iss).
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

# Always leave the machine clean, whether or not the case passed.
function Remove-TestWorld($world) {
    if (Get-UninstallEntry) { try { Uninstall-Fs25Vr } catch { Write-Warning "Cleanup uninstall failed: $_" } }
    Remove-Item $recordPath -Force -ErrorAction SilentlyContinue   # the uninstall leaves it for #69 to read
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

# --- Render Resolution (#66) ---------------------------------------------------------------------

Test-Case "Render Resolution WxH writes width, height and windowed, changes nothing else, backs up game.xml once and records the values" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $original = [IO.File]::ReadAllBytes($p.GameXml)

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-True ($exit -eq 0) "Setup exited with $exit."

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

    $exit = Install-Fs25Vr $world
    Assert-True ($exit -eq 0) "Setup exited with $exit."

    Assert-SameBytes $p.GameXml $original "game.xml"
    Assert-Missing $p.GameXmlBackup "game.xml.fs25vr-backup"
    Assert-True ($null -eq (Get-RenderResolutionRecord)) "A Render Resolution record was written although no resolution was applied."
}

Test-Case "Render Resolution accepts the 640 and 8192 limits" {
    param($world)
    New-FakeGame $world
    Set-RealGameXml $world
    $p = Get-GamePaths $world

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=640x8192")
    Assert-True ($exit -eq 0) "Setup exited with $exit."
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

    $exit = Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")
    Assert-True ($exit -eq 0) "Setup exited with $exit."

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

Test-Case "Applying again keeps the first backup and the first originals; applying after an in-game change records the new originals" {
    param($world)
    # A Documents path with non-ASCII characters checks that the record survives the round trip
    # (the Installer re-reads it to decide which originals to keep).
    $world.Documents = Join-Path $world.Root ("Dokumente " + [char]0xF6 + [char]0x20AC)
    New-FakeGame $world
    Set-RealGameXml $world "2560" "1440" "fullscreen"
    $p = Get-GamePaths $world
    $firstOriginal = [IO.File]::ReadAllBytes($p.GameXml)

    Assert-True ((Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4000x2500")) -eq 0) "First Setup failed."
    Assert-True ((Install-Fs25Vr $world @("/FS25VR_RESOLUTION=5000x3000")) -eq 0) "Second Setup (upgrade) failed."

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
    Assert-True ((Install-Fs25Vr $world @("/FS25VR_RESOLUTION=4500x2800")) -eq 0) "Third Setup failed."
    $r = Get-RenderResolutionRecord
    Assert-True ($r.OriginalWidth -eq "1920" -and $r.OriginalHeight -eq "1080" -and $r.OriginalFullscreenMode -eq "windowed_fullscreen") `
        "After an in-game change the record's originals are $($r.OriginalWidth)x$($r.OriginalHeight) $($r.OriginalFullscreenMode), expected 1920x1080 windowed_fullscreen."
    Assert-SameBytes $p.GameXmlBackup $firstOriginal "game.xml.fs25vr-backup after the third apply"
}

if ($failures.Count -gt 0) {
    Write-Error "$($failures.Count) installer test(s) failed: $($failures -join '; ')"
}
Write-Host "All installer tests passed."
