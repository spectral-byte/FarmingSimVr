# Manual D3D12 debug-layer check for the eye-swapchain copy

The unit tests use a mock OpenXR dispatch with no real D3D12 device, so resource-state
correctness of `OpenXrManager::CopyBackBufferToEye` cannot be validated automatically.
Verify it manually after touching the copy path:

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
