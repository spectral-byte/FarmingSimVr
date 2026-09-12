# Oblique Frustum Projection via VRBridge

## Context and Decision
VR headset optical displays exhibit asymmetric fields of view (nasal vs. temporal and top vs. bottom) due to lens canting, IPD adjustment, and optical center alignment. Standard symmetrical perspective projection matrices produce severe visual distortion, stretching, and perspective swim during head rotation. Rather than attempting low-level D3D12 constant buffer or vertex shader projection matrix patching—which is fragile across engine updates—we decided to extract per-eye optical field of view tangents ($tanLeft, tanRight, tanUp, tanDown$) directly from OpenXR's view state (`XrFovf`) in the native Injector, translate them into vertical FOV and tan-angle offsets ($xOffset = \tan(\theta_R) + \tan(\theta_L)$, $yOffset = \tan(\theta_U) + \tan(\theta_D)$), and publish them through the in-process `VRBridge` table. The Companion Mod consumes these parameters each frame to configure `setFovY` and apply off-axis lens offsets via GIANTS Engine 10's native `setProjectionOffset(camNode, x, y)` API.

## Status
Accepted

## Considered Options
- Direct D3D12 constant buffer matrix patching (rejected: multithreaded command recording creates high risk of descriptor/constant buffer race conditions and patch desynchronization).
- Symmetrical field of view approximation with post-process shader distortion (rejected: causes geometric distortion and perspective shear during head motion).
- Tan-angle offset extraction from OpenXR and native `setProjectionOffset` via VRBridge (selected: native engine support for oblique frustums, exact optical center alignment, zero constant buffer tampering, and clean restoration on mod unload).
