param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build'),
    [string]$Injector = ''
)
$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $Injector) { $Injector = Join-Path $BuildDir 'version.dll' }
if (-not (Test-Path $Injector)) { throw "No Injector at $Injector; build the 'injector' target first." }
$Injector = (Resolve-Path $Injector).Path
$layerDir = Join-Path $BuildDir 'vdxr_sim_layer'
$harness = Join-Path $layerDir 'harness.exe'
if (-not (Test-Path $harness)) { throw "No harness at $harness; run build.ps1 first." }

$env:XR_API_LAYER_PATH = $layerDir
$env:XR_ENABLE_API_LAYERS = 'XR_APILAYER_FS25VR_vdxr_sim'
$layerLog = Join-Path $layerDir 'vdxr_sim_layer.log'
$name = Split-Path $Injector -Leaf
$modes = if ($name -ieq 'dxgi.dll') { @('') } else { @('dxgi-first', 'dxgi-later') }

$failed = $false
foreach ($mode in $modes) {
    $dir = Join-Path $layerDir ("run-" + $(if ($mode) { $mode } else { 'proxy' }))
    New-Item -ItemType Directory -Force $dir | Out-Null
    Remove-Item (Join-Path $dir '*') -Force -ErrorAction SilentlyContinue
    Copy-Item $Injector (Join-Path $dir $name)
    $layerLinesBefore = if (Test-Path $layerLog) { (Get-Content $layerLog).Count } else { 0 }

    Write-Host "=== $name $mode" -ForegroundColor Cyan
    $harnessArgs = @((Join-Path $dir $name)); if ($mode) { $harnessArgs += $mode }
    & $harness @harnessArgs
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
