# Enables the test layer for every OpenXR app started afterwards by this user.
# Steam must be fully restarted (Steam > Exit) so the game inherits the variables.
[Environment]::SetEnvironmentVariable('XR_API_LAYER_PATH', $PSScriptRoot, 'User')
[Environment]::SetEnvironmentVariable('XR_ENABLE_API_LAYERS', 'XR_APILAYER_FS25VR_vdxr_sim', 'User')
Write-Host "VDXR simulation layer enabled. Exit Steam completely and start it again before launching FS25."
