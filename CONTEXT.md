# Farming Simulator VR (FS25 VR)

A high-performance virtual reality modding architecture for Farming Simulator 25 running on GIANTS Engine 10 under Direct3D 12.

## Language

**Injector**:
The native Direct3D 12 proxy library loaded by the game process that interacts with the OpenXR runtime and intercepts swapchain presentation.
_Avoid_: Wrapper, loader, hook DLL

**Companion Mod**:
The in-engine Luau script mod executing inside GIANTS Engine 10 that manipulates camera transforms and asymmetric frustum projections.
_Avoid_: Script hook, Lua plugin

**VRBridge**:
The global Luau table injected into GIANTS Engine 10 by the Injector that exposes OpenXR view state, asymmetric projection parameters, and frame synchronization flags to the Companion Mod.
_Avoid_: Tracking bridge, shared memory buffer, IPC pipe

**Alternate Frame Rendering (AFR)**:
A stereoscopic rendering technique where the engine renders left and right eye perspectives on alternating sequential frames.
_Avoid_: Alternate eye rendering, frame interlacing

**Oblique Frustum**:
An off-axis perspective projection with asymmetric view angles matched to the optical center and field of view of a VR headset display.
_Avoid_: Skewed frustum, asymmetric projection

**Pass-Through Mode**:
The inert operational state where the Injector forwards all DXGI calls to the system library without initiating an OpenXR session when no VR runtime is detected.
_Avoid_: Bypass mode, fallback state

**Recenter Action**:
The bindable in-engine input action that zeroes the player's 6DOF offset and resets the OpenXR reference space origin relative to the vehicle cockpit or player camera.
_Avoid_: Reset view, camera zeroing
