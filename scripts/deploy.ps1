# Deploy script for Farming Simulator 25 VR Injector and Companion Mod
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Farming Simulator 25"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$builtDll = Join-Path $repoRoot "build\version.dll"
$gameX64Dir = Join-Path $GameDir "x64"
$documentsDir = [Environment]::GetFolderPath("MyDocuments")
$modsDir = Join-Path $documentsDir "My Games\FarmingSimulator2025\mods"

Write-Host "Deploying FS25 VR artifacts..."

# 1. Copy Injector DLL (the game loads it as its version.dll, ADR-0038)
if (Test-Path $builtDll) {
    Copy-Item $builtDll (Join-Path $gameX64Dir "version.dll") -Force
    Write-Host "[Deploy] Deployed version.dll to $gameX64Dir"
} else {
    Write-Error "build\version.dll not found. Build project first (cmake --build build --target injector)."
}

# An Injector from before ADR-0038 was the game's dxgi.dll. With it still there, the new one stays inactive, so
# remove it, and put back a dxgi.dll the Installer moved aside for it (ReShade, for one).
$oldInjector = Join-Path $gameX64Dir "dxgi.dll"
if ((Test-Path $oldInjector) -and ([Diagnostics.FileVersionInfo]::GetVersionInfo($oldInjector).ProductName -eq "FS25 VR")) {
    Remove-Item $oldInjector -Force
    Write-Host "[Deploy] Removed the earlier FS25 VR dxgi.dll"
    $backup = "$oldInjector.fs25vr-backup"
    if (Test-Path $backup) {
        Move-Item $backup $oldInjector
        Write-Host "[Deploy] Put back the dxgi.dll that was moved aside for it"
    }
}

# 2. Package Companion Mod with tar.exe (preserving POSIX forward slashes)
$companionDir = Join-Path $repoRoot "mods\FS25_VRCompanion"
$destZip = Join-Path $modsDir "FS25_VRCompanion.zip"

Push-Location $companionDir
try {
    tar.exe -a -cf $destZip modDesc.xml icon_vr.png src/*.lua models/*
    Write-Host "[Deploy] Packaged and deployed FS25_VRCompanion.zip to $modsDir"
} finally {
    Pop-Location
}
