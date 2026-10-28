# Oblique Frustum Projection via VRBridge

## Context and Decision
VR headset optical displays exhibit asymmetric fields of view (nasal vs. temporal and top vs. bottom) due to lens canting, IPD adjustment, and optical center alignment. Rather than attempting low-level D3D12 constant buffer or vertex shader projection matrix patching—which is fragile across engine updates—the native Injector extracts per-eye optical field of view tangents ($tanLeft, tanRight, tanUp, tanDown$) from OpenXR's view state (`XrFovf`), translates them into vertical FOV and tan-angle offsets ($xOffset = \tan(\theta_R) + \tan(\theta_L)$, $yOffset = \tan(\theta_U) + \tan(\theta_D)$), and publishes them through the in-process `VRBridge` table. The Companion Mod consumes the vertical FOV each frame to configure `setFovY`.

**Symmetric projection is the only projection.** The Companion Mod keeps the camera on the optical axis (`setProjectionOffset(camNode, 0, 0)`) for both eyes, in both Stereo Presentation Modes, and the Injector submits a matching symmetric FOV to OpenXR: vertical FOV from the headset, horizontal half-angle derived from the eye swapchain aspect ratio ($\tan(\theta_{x}/2) = aspect \cdot \tan(\theta_{y}/2)$). Because the rendered image and the submitted FOV describe the same symmetric frustum, the compositor reprojects it without stretching or perspective swim, and parallel rays at infinity avoid cross-eyed disparity shear. On headsets with strongly asymmetric or canted optics this trades away some field-of-view coverage.

**The Oblique Frustum was removed.** It had been an opt-in (the `allowObliqueProjection` setting and the `vrObliqueProjection` console command) that applied the published `xOffset`/`yOffset` per eye. The opt-in never matched what the Injector submits: the engine rendered an off-axis frustum while the Injector told the OpenXR runtime the view was symmetric, so the compositor showed a skewed image. The setting, the console command and the projection offset scale are gone. A settings file that still holds `allowObliqueProjection` loads without error, the entry is ignored and it is not written back. The VRBridge eye projection still publishes the runtime's tan offsets, and camera records still carry their projection offset fields (now always zero); the bridge contract is unchanged. Bringing the Oblique Frustum back needs a design that submits the frustum the engine actually renders, plus a headset round to confirm the engine's projection offset units.

## Status
Accepted, amended: Oblique Frustum removed.

## Considered Options
- Direct D3D12 constant buffer matrix patching (rejected: multithreaded command recording creates high risk of descriptor/constant buffer race conditions and patch desynchronization).
- Symmetrical field of view approximation with post-process shader distortion (rejected: causes geometric distortion and perspective shear during head motion).
- Tan-angle offset extraction from OpenXR and native `setProjectionOffset` via VRBridge, applied per eye (originally selected as the mechanism; withdrawn when the Oblique Frustum was removed, because the submitted FOV never described the rendered frustum).
- Aspect-matched symmetric FOV submitted to OpenXR with a centered engine projection (selected: submitted FOV and rendered frustum agree exactly).
