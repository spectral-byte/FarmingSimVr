# Manual D3D12 debug-layer check for the eye-swapchain copy

The unit tests that use a mock OpenXR dispatch have no real D3D12 device, so they cannot validate resource-state
correctness of `OpenXrManager::CopyBackBufferToEye`. The WARP test described under "Automated check on a WARP
device" below can, when the D3D12 debug layer is installed. Where it is not, or after touching the copy path,
verify manually:

1. Enable the D3D12 debug layer for the game (DirectX Control Panel, or `ID3D12Debug::EnableDebugLayer`),
   and optionally GPU-based validation.
2. Run the game with the Injector in Active VR Mode and a real OpenXR runtime.
3. Watch the debugger output / Injector log for `D3D12 ERROR` messages mentioning
   `RESOURCE_MANIPULATION` or resource state mismatch on the eye swapchain image.

Expected resource states per the OpenXR D3D12 extension:

- Eye image after `xrWaitSwapchainImage`: `RENDER_TARGET`
- Around the copy: `RENDER_TARGET` -> `COPY_DEST` -> `RENDER_TARGET` (required before `xrReleaseSwapchainImage`)
- Back buffer: `PRESENT` -> `COPY_SOURCE` -> `PRESENT`

The negotiated eye swapchain format must be in the same format family as the back buffer
(UNORM/sRGB variants allowed). If the runtime offers none, the Injector logs an error and
stays in Pass-Through Mode.

## Automated check on a WARP device

`OpenXrEyeCopyD3D12Test.EyeCopyOnARealDeviceReportsNoDebugLayerErrors` (`tests/test_openxr_eye_copy_d3d12.cpp`) runs the
eye copy against a real D3D12 device on the WARP adapter with the debug layer on, and fails on any error the layer
reports. It copies more frames than the copy ring has slots, recreates the eye swapchains, and runs a copy with no
destination texture. It skips when the debug layer is not installed (the Windows optional feature Graphics Tools), so
the manual check above stays the fallback on such a machine.
