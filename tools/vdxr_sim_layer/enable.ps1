# Enables the test layer for every OpenXR app started afterwards by this user, from the folder build.ps1 built it into.
# Steam must be fully restarted (Steam > Exit) so the game inherits the variables. disable.ps1 undoes it.
#   .\enable.ps1                     # <repo>\build\vdxr_sim_layer
#   .\enable.ps1 -BuildDir <path>    # another CMake build folder
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
