param([string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build'))
$ErrorActionPreference = 'Stop'
$layerDir = Join-Path $BuildDir 'vdxr_sim_layer'
if (-not (Test-Path (Join-Path $layerDir 'XrApiLayer_vdxr_sim.json'))) {
    throw "No built layer in $layerDir; run build.ps1 first."
}
$layerDir = (Resolve-Path $layerDir).Path
[Environment]::SetEnvironmentVariable('XR_API_LAYER_PATH', $layerDir, 'User')
[Environment]::SetEnvironmentVariable('XR_ENABLE_API_LAYERS', 'XR_APILAYER_FS25VR_vdxr_sim', 'User')
Write-Host "VDXR simulation layer enabled from $layerDir. Exit Steam completely and start it again before launching FS25."
