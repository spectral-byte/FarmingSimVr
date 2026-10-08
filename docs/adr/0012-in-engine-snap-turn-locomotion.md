# In-Engine Snap Turn Locomotion

## Context and Decision
Virtual reality navigation requires rotation mechanisms for players in seated or forward-facing setups without inducing vestibular motion sickness. Continuous analog turning via mouse or thumbstick is a primary trigger of simulation sickness.

We established the following architectural design for Snap Turn locomotion:

1. **Engine-Level Yaw Transformation**:
   - Rather than rotating the OpenXR tracking reference space or injecting synthetic matrix rotations into the rendering pipeline, Snap Turn operates at the engine camera hierarchy level:
     - **On Foot (`PlayerCamera`)**: The snap turn offset is applied directly to the character root's `yawNode` orientation. Because GIANTS Engine derives walking vectors ($W/A/S/D$) from `yawNode`, snapping `yawNode` keeps character movement and gaze direction completely collinear.
     - **In-Cab (`VehicleCamera`)**: The snap turn offset modifies the vehicle camera's gaze yaw (`cam.rotY`), rotating the intermediate `headTrackingNode` while keeping 6DOF OpenXR head tracking composited cleanly on top.
     - **Outside Camera (`VehicleCamera`, not `isInside`)**: On a rotatable Outside Camera the snap turn steps the engine's orbit yaw (`cam.rotY`) around the vehicle, which `VehicleCamera.update` applies to its rotate node; orbit pitch and zoom are left alone. The Companion Mod writes only that field, never the engine's camera nodes. A fixed Outside Camera (hitch, header) has no orbit, so the snap turn is not handled there and starts no debounce cooldown.
   - Physical 6DOF room-scale head tracking remains continuous and unaffected by snap turns.

2. **Discrete Instantaneous Execution**:
   - Rotation occurs instantaneously in a single frame ($0$ transition duration) by default in $90^\circ$ increments ($\pm \frac{\pi}{2}$ rad), with user-configurable angles (e.g., $45^\circ$, $90^\circ$).
   - Single-fire on key/button down with a minimum debounce cooldown interval ($250$ms) prevents rapid accidental spin-outs.

3. **Recenter and GUI Lifecycle Rules**:
   - Triggering the Recenter Action (`VR_RECENTER_VIEW`) resets any in-cab snap turn back to neutral windshield alignment (`cam.origRotY`).
   - In a rotatable Outside Camera the Recenter Action swings the orbit back to its recorded default: orbit yaw and pitch return to `cam.origRotY` / `cam.origRotX`, the engine's own values when present, otherwise recorded once when the camera becomes active. Zoom is left alone. A fixed Outside Camera has no orbit to reset.
   - Snap turn actions are suppressed when in-game GUI menus are open (`isMenuVisible == true`) to protect 3D floating menu quad alignment.

## Status
Accepted

## Considered Options
- **OpenXR Reference Space Recreation for Snap Turns** (rejected: destroys and recreates `m_localSpace` on every turn, causing hitching and desynchronizing engine character walking physics from tracking space).
- **Camera-only local offset without updating character `yawNode`** (rejected: causes character movement $W/A/S/D$ to move diagonally/sideways relative to the snapped view).
- **Continuous / Animated Smooth Pan** (rejected: causes severe vestibular conflict and motion sickness for VR users).
- **In-Engine `yawNode` and `cam.rotY` discrete stepping** (selected: maintains movement collinearity, zero motion sickness, seamless integration with vanilla input binding system).
