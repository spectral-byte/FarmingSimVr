# One Registered VR Camera for Both Eyes

Amends [ADR-0015](0015-sequential-dual-pass-stereo-presentation-pipeline.md) and [ADR-0016](0016-dual-feature-context-virtualization-for-temporal-upscalers.md).

## Context and Decision
ADR-0016 has the Companion Mod alternate two distinct scene-graph camera nodes (`cameraNodeLeft`, `cameraNodeRight`), and ADR-0015's sub-passes activate one or the other. Making the Outside Camera render through those nodes showed in the headset that they had never rendered:

- **The game's camera manager ignores cameras it didn't register.** `g_cameraManager:setActiveCamera` silently did nothing for the eye nodes. `getCamera()` always reported the game's own camera. The cab and on-foot views only looked right because those paths also write the head pose into the game's camera.
- **The global `setCamera` is the game's deprecation wrapper.** It logs "Use g_cameraManager:setActiveCamera(cameraNode) instead of setCamera(cameraNode)!" on every call and didn't render the eye nodes either.
- **A camera registered with `g_cameraManager:addCamera` renders** through `setActiveCamera`, as a debug build confirmed.
- **Two cameras that really alternate break DLSS and FSR.** With both eye nodes registered and alternating every frame, the temporal upscalers drop their history on each rendering camera change, so the image lost its anti-aliasing.

**The Companion Mod renders both eyes through one camera node, registered with the camera manager.** The node is moved to each eye's position (and given that eye's projection) every AFR frame or Sequential Dual-Pass sub-pass. The engine sees a single camera, as it did when the game's own camera was moved between the eyes, and the Injector's per-eye correction (ADR-0020) handles the eye-stride motion vectors.

- `cameraNodeLeft` and `cameraNodeRight` both hold the one node (the Dual Eye Camera Nodes in `CONTEXT.md`).
- The mod registers the node with `addCamera` before binding it through `setActiveCamera`. The global `setCamera` is only the fallback when there is no camera manager.
- The mod remembers the game's own camera, the one the VR camera took the place of. It uses that to leave game screens with a camera of their own (the shop) alone, and to hand rendering back on map unload, which also unregisters the node.
- The camera takes the lens (field of view, near and far clip) of the game camera it replaces.

## Status
Accepted. Verified in the headset: Outside Camera 6DOF, DLSS and FSR anti-aliasing, and the cab and on-foot views.

## Considered Options
- **Raw `setCamera`** (rejected: the game's wrapper warns on every call and doesn't render the camera).
- **Two registered eye cameras** (rejected: the temporal upscalers drop their history on every camera change).
- **Write the head pose into the game's own camera, as the cab does** (rejected for the Outside Camera: the game's orbit, zoom and collision logic reads that node back, see #43).

## Consequences
- ADR-0016's "two distinct scene-graph camera nodes" and ADR-0015's sub-pass activation of `cameraNodeLeft`/`cameraNodeRight` now refer to the one shared node.
- The camera manager's active camera is the VR camera while the mod renders, so game code that asks the manager for the active camera gets the VR camera. The mod keeps the game's own camera separately.
- The node is linked under the camera path's parent (in the cab, under the vehicle), so it can be deleted with a vehicle. The mod recreates it, and remembers the deleted id in case the camera manager still names it active.
