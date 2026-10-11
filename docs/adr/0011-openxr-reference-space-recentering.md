# OpenXR Reference Space Recentering via poseInReferenceSpace

## Context and Decision
In virtual reality simulation games like Farming Simulator 25, players need to recenter their view to align their physical seating orientation and position with the in-game driver's seat or camera view.

Previously, recentering attempts that applied software rotation or translation offsets at the camera node or artificial view pose offsets inside `SubmitFramePair` suffered from several severe issues:
1. **Compositor Reprojection Swimming**: The OpenXR runtime compositor's asynchronous timewarp (ATW/reprojection) expects the projection layer pose to be defined within a continuous, stationary reference space. Artificially injecting view rotation offsets can cause visual shearing and swimming during rapid head turns.
2. **2D Panel Timewarp Lock**: When recentering while looking significantly off-center, offsetting the projection layer without altering the underlying reference space origin creates perceptual distortion and misalignment with spatial audio and peripheral boundaries.

To solve this cleanly and natively at the tracking runtime level, we established the following architectural design:

1. **Dual Reference Space Hierarchy**:
   - `OpenXrManager::CreateTrackingSpaces` instantiates two reference spaces of type `XR_REFERENCE_SPACE_TYPE_LOCAL`:
     - `m_baseLocalSpace`: Created with identity pose at session startup. It remains untouched for the lifetime of the session, providing an immutable baseline tracking origin.
     - `m_localSpace`: The active reference space queried for head pose tracking and passed to `xrLocateViews` and `XrCompositionLayerProjection`. Initially created with identity pose.

2. **Reference Space Re-Anchoring on Recenter**:
   - When recentering is triggered (`OpenXrManager::Recenter`), the headset's current position and yaw in `m_baseLocalSpace` are sampled.
   - Yaw is extracted from the orientation quaternion, while pitch and roll are kept zeroed ($q_x = 0, q_z = 0$) to ensure the player's horizon remains level with gravity.
   - The existing `m_localSpace` is destroyed via `xrDestroySpace`.
   - A new `m_localSpace` is recreated via `xrCreateReferenceSpace` specifying `poseInReferenceSpace` matching the current head position and horizon-level yaw.
   - Because `poseInReferenceSpace` defines the pose of the new reference space relative to the base tracking space, subsequent calls to `xrLocateSpace(m_viewSpace, m_localSpace, ...)` natively yield `(0, 0, 0)` position and zero yaw at the recentered pose.

3. **Separation of Raw Tracking and Local Head Pose**:
   - `PollHeadPose` locates `m_viewSpace` relative to `m_localSpace` to produce `m_lastPose` (the recentered pose used by the game engine camera and floating menu quad).
   - `PollHeadPose` simultaneously locates `m_viewSpace` relative to `m_baseLocalSpace` to populate `m_rawPose`, preserving uncentered telemetry and diagnostics.
   - No synthetic rotation offsets are injected into `SubmitFramePair` or projection layers, allowing the OpenXR compositor to perform timewarp reprojection without swimming or artifacts.

## Status
Accepted

## Considered Options
- **Artificial projection layer pose offset in `SubmitFramePair`** (rejected: causes compositor timewarp reprojection artifacts and visual swimming during head rotations).
- **CPU transformation solely on GIANTS Engine camera nodes in Luau** (rejected: leaves OpenXR floating menu quad, mouse layer, and compositor out of sync with recentered coordinates).
- **OpenXR reference space recreation with `poseInReferenceSpace`** (selected: native OpenXR standard compliance, zero reprojection swimming, automatic 360-degree stereoscopic view preservation, level horizon guarantee).
