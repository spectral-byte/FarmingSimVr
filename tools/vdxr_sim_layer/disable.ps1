# Removes the variables enable.ps1 set. Restart Steam afterwards.
[Environment]::SetEnvironmentVariable('XR_API_LAYER_PATH', $null, 'User')
[Environment]::SetEnvironmentVariable('XR_ENABLE_API_LAYERS', $null, 'User')
Write-Host "VDXR simulation layer disabled. Exit Steam completely and start it again."
