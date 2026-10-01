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

**VR HUD Inset**:
The normalized viewport margin applied to engine HUD anchors (`g_hudAnchor*`) that contracts 2D interface elements inward from screen edges into the optical sweet spot of the VR headset.
_Avoid_: UI padding, screen margin, HUD border

**HUD Convergence Offset**:
The alternating horizontal screen-space displacement applied across Alternate Frame Rendering (AFR) frames to render 2D interface elements at a comfortable stereoscopic depth.
_Avoid_: 3D HUD shift, eye disparity offset, stereo HUD hack

**HUD Toggle Action**:
The bindable in-engine input action that toggles or pulses the visibility of the in-game HUD between hidden and visible states.
_Avoid_: Show HUD button, HUD keybind, UI switch

**Streamlined HUD**:
The decluttered VR presentation mode that preserves essential driving and machine telemetry while suppressing bulky secondary panels (such as the minimap and input help).
_Avoid_: Mini HUD, compact UI, lite HUD

**Snap Turn**:
An instantaneous rotational increment applied to the player's body or in-cab gaze angle to reorient view direction without vestibular motion sickness.
_Avoid_: Quick turn, instant rotation, snap rotate, flick turn

**VR View Toggle Action**:
The bindable in-engine input action that toggles presentation between immersive stereoscopic 3D VR view and 2D Floating Window Mode.
_Avoid_: 2D toggle, screen switch, VR off button, flat toggle

**2D Floating Window Mode**:
The presentation state where in-engine 3D gameplay is rendered monoscopically to the backbuffer and presented on a 6DOF floating quad layer in front of the player.
_Avoid_: Theater mode, flat mode, cinema view, 2D mode

**Desktop Mirror Window**:
The Win32 desktop window displaying a scaled presentation of the game backbuffer while the player is in VR.
_Avoid_: Companion window, flat window, monitor mirror, game screen

**Resolution Decoupling**:
The operational state where the in-engine Direct3D 12 swapchain rendering resolution is decoupled from the physical Win32 Desktop Mirror Window dimensions.
_Avoid_: Resolution override, window scaling hack, virtual resolution



