# Deploy script for Farming Simulator 25 VR Injector and Companion Mod
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Farming Simulator 25"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$builtDll = Join-Path $repoRoot "build\dxgi.dll"
$gameX64Dir = Join-Path $GameDir "x64"
$documentsDir = [Environment]::GetFolderPath("MyDocuments")
$modsDir = Join-Path $documentsDir "My Games\FarmingSimulator2025\mods"

Write-Host "Deploying FS25 VR artifacts..."

# 1. Copy Injector DLL
if (Test-Path $builtDll) {
    Copy-Item $builtDll (Join-Path $gameX64Dir "dxgi.dll") -Force
    Write-Host "[Deploy] Deployed dxgi.dll to $gameX64Dir"
} else {
    Write-Error "build\dxgi.dll not found. Build project first."
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
