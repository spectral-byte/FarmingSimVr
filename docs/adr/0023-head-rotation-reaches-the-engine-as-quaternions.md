# Head Rotation Reaches the Engine as Quaternions

## Context and Decision
Looking about a quarter turn to the left or right stopped the view pitching: the image followed the head vertically while yaw still worked. That happened in the cab and on foot.

Both paths composed the head pose onto the head tracking node with `localRotationToLocal`, which hands the result back as Euler angles in the engine's own convention, and then wrote those with `setRotation`.
- **The engine's convention is not documented.** The game's own camera code (`VehicleCamera:updateRotateNodeRotation`) shows it applies yaw before pitch, so the yaw is the middle angle (R = Rz * Ry * Rx, or R = Ry * Rx * Rz).
- **Euler angles read back lose the pitch at the singularity.** For R = Rz * Ry * Rx the singularity is a yaw of 90 degrees, where pitch and roll turn about one axis. The head tracking node faces a half turn from its parent, so looking a quarter turn to the side lands there. In single precision the pitch and roll are then read from rounding noise.
- **The pose's own angles were read in the engine's convention**, while VRBridge composes them R = Ry(yaw) * Rx(pitch) * Rz(roll). With the head turned, head roll showed as pitch.

**The Companion Mod composes the head rotation as quaternions and writes it with `setQuaternion`. It never reads Euler angles back from the engine.**
- The pose's Euler angles become a quaternion, R = Ry(yaw) * Rx(pitch) * Rz(roll), as VRBridge defines them.
- The rotation the head turns on (the head tracking node's, in the camera parent's space) comes from `getWorldQuaternion` of the node and of that parent.
- `setQuaternion` places the Dual Eye Camera Nodes, the Cyclopean Audio Listener Node and the game's own camera. The view shown while head tracking is lost, the Outside Camera on its level base and the recenter realignment use the same path.

## Status
Accepted. Verified in the headset: the view keeps pitching when looking a quarter turn to the side. The Luau suite `test_head_rotation.luau` covers the cab and on-foot paths at every head yaw against a mock engine in both conventions, modelling the precision loss at the singularity.

## Considered Options
- **Keep `localRotationToLocal` and fix the angle order** (rejected: the read-back still loses the pitch at the singularity).
- **Convert with the engine's `mathQuaternionToEuler`** (rejected: same read-back, same singularity).
- **Probe the engine's convention at runtime and convert in Lua** (rejected: any Euler write with the yaw in the middle still meets the singularity).

## Consequences
- Head rotation no longer depends on the engine's Euler convention.
- With no `setQuaternion`, the mod falls back to `setRotation` with R = Ry * Rx * Rz angles. With no `getWorldQuaternion`, the head turns on the view's yaw alone. The engine has both, so only the test mocks use the fallbacks.
- `updateMenuBackgroundCamera` is unchanged: it still adds the head's Euler angles to the angles it reads back from the camera behind a menu, so it can meet the same singularity there.
