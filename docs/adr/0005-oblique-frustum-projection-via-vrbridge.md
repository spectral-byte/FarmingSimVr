# Oblique Frustum Projection via VRBridge

## Context and Decision
VR headset optical displays exhibit asymmetric fields of view (nasal vs. temporal and top vs. bottom) due to lens canting, IPD adjustment, and optical center alignment. Rather than attempting low-level D3D12 constant buffer or vertex shader projection matrix patching—which is fragile across engine updates—the native Injector extracts per-eye optical field of view tangents ($tanLeft, tanRight, tanUp, tanDown$) from OpenXR's view state (`XrFovf`), translates them into vertical FOV and tan-angle offsets ($xOffset = \tan(\theta_R) + \tan(\theta_L)$, $yOffset = \tan(\theta_U) + \tan(\theta_D)$), and publishes them through the in-process `VRBridge` table. The Companion Mod consumes these parameters each frame to configure `setFovY` and, when enabled, apply off-axis lens offsets via GIANTS Engine 10's native `setProjectionOffset(camNode, x, y)` API.

**Symmetric projection is the default.** The Companion Mod keeps the camera on the optical axis (`setProjectionOffset(camNode, 0, 0)`), and the Injector submits a matching symmetric FOV to OpenXR: vertical FOV from the headset, horizontal half-angle derived from the eye swapchain aspect ratio ($\tan(\theta_{x}/2) = aspect \cdot \tan(\theta_{y}/2)$). Because the rendered image and the submitted FOV describe the same symmetric frustum, the compositor reprojects it without stretching or perspective swim, and parallel rays at infinity avoid cross-eyed disparity shear. On headsets with strongly asymmetric or canted optics this can trade away some field-of-view coverage.

**Oblique Frustum is opt-in** via the `allowObliqueProjection` setting (`<allowObliqueProjection>` in `modSettings/FS25_VRCompanion/settings.xml`, or the console command `vrObliqueProjection <true|false|toggle>`). When enabled, the published `xOffset`/`yOffset` are applied per eye.

## Status
Accepted, amended: symmetric projection is the default, Oblique Frustum is opt-in.

## Considered Options
- Direct D3D12 constant buffer matrix patching (rejected: multithreaded command recording creates high risk of descriptor/constant buffer race conditions and patch desynchronization).
- Symmetrical field of view approximation with post-process shader distortion (rejected: causes geometric distortion and perspective shear during head motion).
- Tan-angle offset extraction from OpenXR and native `setProjectionOffset` via VRBridge (selected as the mechanism: native engine support for oblique frustums, exact optical center alignment, zero constant buffer tampering, and clean restoration on mod unload).
- Aspect-matched symmetric FOV submitted to OpenXR with a centered engine projection (selected as the default: submitted FOV and rendered frustum agree exactly; oblique offsets remain available for canted optics).
