# Runs the harness against the Injector in build\ (version.dll, from 1.2) in both startup orders, with the VDXR simulation
# layer enabled for this shell only (no User variables, no Steam restart). Each run uses a fresh copy of version.dll in its
# own folder, since the Injector writes fs25_vr.log next to itself.
#   .\run.ps1                     # build\version.dll
#   .\run.ps1 -Injector <path>    # another version.dll, or a proxy dxgi.dll up to 1.1.9 (one run, no modes)
param([string]$Injector = (Join-Path $PSScriptRoot '..\..\build\version.dll'))
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$Injector = (Resolve-Path $Injector).Path

$env:XR_API_LAYER_PATH = $here
$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_FS25VR_vdxr_sim'
$layerLog = Join-Path $here 'vdxr_sim_layer.log'
$name = Split-Path $Injector -Leaf
$modes = if ($name -ieq 'dxgi.dll') { @('') } else { @('dxgi-first', 'dxgi-later') }

$failed = $false
foreach ($mode in $modes) {
    $dir = Join-Path $here ("run-" + $(if ($mode) { $mode } else { 'proxy' }))
    New-Item -ItemType Directory -Force $dir | Out-Null
    Remove-Item (Join-Path $dir '*') -Force -ErrorAction SilentlyContinue
    Copy-Item $Injector (Join-Path $dir $name)
    $layerLinesBefore = if (Test-Path $layerLog) { (Get-Content $layerLog).Count } else { 0 }

    Write-Host "=== $name $mode" -ForegroundColor Cyan
    $args = @((Join-Path $dir $name)); if ($mode) { $args += $mode }
    & (Join-Path $here 'harness.exe') @args
    $code = $LASTEXITCODE
    Write-Host "exit code $code" -ForegroundColor $(if ($code -eq 0) { 'Green' } else { 'Red' })
    if ($code -ne 0) { $failed = $true }

    Write-Host "--- layer log (this run)"
    if (Test-Path $layerLog) { Get-Content $layerLog | Select-Object -Skip $layerLinesBefore | Where-Object { $_ -match 'harness\.exe' } }
    Write-Host "--- fs25_vr.log (startup lines)"
    $log = Join-Path $dir 'fs25_vr.log'
    if (Test-Path $log) {
        Get-Content $log | Where-Object { $_ -match 'Injector:|DxgiExportHooks|OpenXR|Pass-Through|Active VR|ERROR|WARN' } | Select-Object -First 15
    } else { Write-Host "(no fs25_vr.log)" }
}
if ($failed) { exit 1 }
