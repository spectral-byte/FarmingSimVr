# Farming Simulator VR (FS25 VR)

A high-performance virtual reality modding architecture for Farming Simulator 25 running on GIANTS Engine 10 under Direct3D 12.

## Language

**Injector**:
The native Direct3D 12 proxy library loaded by the game process that interacts with the OpenXR runtime and intercepts swapchain presentation.
_Avoid_: Wrapper, loader, hook DLL

**Companion Mod**:
The in-engine Luau script mod executing inside GIANTS Engine 10 that manipulates camera transforms and asymmetric frustum projections.
_Avoid_: Script hook, Lua plugin

**Installer**:
The setup program a player runs to place the Injector into a chosen game installation and the Companion Mod into their mods folder, and to remove both again. Never part of the running game.
_Avoid_: Injector installer, setup script, launcher

**VRBridge**:
The global Luau table injected into GIANTS Engine 10 by the Injector that exposes OpenXR view state, asymmetric projection parameters, and frame synchronization flags to the Companion Mod.
_Avoid_: Tracking bridge, shared memory buffer, IPC pipe

**Alternate Frame Rendering (AFR)**:
A stereoscopic rendering technique where the engine renders left and right eye perspectives on alternating sequential frames.
_Avoid_: Alternate eye rendering, frame interlacing

**Sequential Dual-Pass (SDP)**:
A stereoscopic rendering technique where left and right eye perspectives are rendered sequentially within the same logical frame tick and submitted synchronously to the VR compositor.
_Avoid_: Dual rendering, double pass, simultaneous pass, split-frame stereo

**Stereo Presentation Mode**:
The selectable operational pipeline governing how stereoscopic views are rendered and paced (Alternate Frame Rendering or Sequential Dual-Pass).
_Avoid_: 3D mode, render method, stereo toggle, vr mode

**Oblique Frustum**:
An off-axis perspective projection with asymmetric view angles matched to the optical center and field of view of a VR headset display.
_Avoid_: Skewed frustum, asymmetric projection

**Pass-Through Mode**:
The inert operational state where the Injector forwards all DXGI calls to the system library without initiating an OpenXR session when no VR runtime is detected.
_Avoid_: Bypass mode, fallback state

**Recenter Action**:
The bindable in-engine input action that zeroes the player's 6DOF offset and resets the OpenXR reference space origin relative to the active vehicle camera (in-cab or Outside Camera) or player camera.
_Avoid_: Reset view, camera zeroing

**Outside Camera**:
Any camera of the controlled vehicle other than its in-cab camera, whether the rotatable orbit camera around the vehicle or a fixed mount such as a hitch or header camera. It receives the same 6DOF head tracking and stereoscopic presentation as the in-cab camera.
_Avoid_: Third-person view, exterior view, chase cam, outside view

**HUD Safe Area**:
The normalized viewport margin applied to the engine's global HUD anchors (`g_hudAnchor*`) that keeps 2D interface elements outside the HUD Groups, such as warnings, notifications, and other mods' HUDs, within the optical sweet spot of the VR headset.
_Avoid_: VR HUD Inset, UI padding, screen margin, HUD border

**HUD Convergence Offset**:
The alternating horizontal screen-space displacement applied across Alternate Frame Rendering (AFR) frames to render 2D interface elements at a comfortable stereoscopic depth.
_Avoid_: 3D HUD shift, eye disparity offset, stereo HUD hack

**HUD Toggle Action**:
The bindable in-engine input action that toggles or pulses the visibility of the in-game HUD between hidden and visible states.
_Avoid_: Show HUD button, HUD keybind, UI switch

**HUD Group**:
One of the four independently placed clusters of engine HUD elements, each fixed to a home screen corner: the Map group (bottom-left), the Speedometer group with its fill levels (bottom-right), the Info group with clock and money (top-right), and the Controls group of input help (top-left). Each has its own HUD Group Margin, scale, and visibility in the Streamlined and Full HUD. The Controls group also keeps whether the player has it maximized or minimized with the game's help toggle (F1), across HUD states and sessions.
_Avoid_: HUD element, HUD panel, widget

**HUD Group Margin**:
The normalized horizontal and vertical distance from a HUD Group's home screen corner to the group's outer edges.
_Avoid_: Offset, XY position, inset

**Streamlined HUD**:
The decluttered VR presentation state that shows only the HUD Groups the player has enabled for it, by default the speedometer group.
_Avoid_: Vehicle HUD, mini HUD, compact UI, lite HUD

**Full HUD**:
The VR presentation state that shows every HUD Group the player has enabled for it, by default all four.
_Avoid_: All-on HUD, complete HUD

**Snap Turn**:
An instantaneous rotational increment applied to the player's body, the in-cab gaze angle, or an Outside Camera's orbit around the vehicle to reorient view direction without vestibular motion sickness.
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

**Dual Feature Context**:
A pair of independent temporal upscaler runtime handles (one dedicated to the left eye and one to the right eye) maintained by the Injector to isolate history buffers across Alternate Frame Rendering (AFR) cycles.
_Avoid_: Dual DLSS instances, stereo history buffers, split upscaler

**Per-Eye Motion Vector Correction**:
The re-aiming of the engine's motion vectors, before each per-eye upscaler evaluation, from the previous rendered frame's camera (the other eye under Sequential Dual-Pass) to the same eye's previous camera, keeping the engine's object motion. It applies to DLSS and FSR3 alike, and the `vrDlssMv*` console names cover both. See ADR-0020.
_Avoid_: MV fix, motion vector hack, MV scaling

**Cross-Eye History Poisoning**:
The corruption of temporal antialiasing and upscaling history buffers caused by alternating between left and right stereoscopic viewpoints across successive frames.
_Avoid_: Interlaced ghosting, AFR smearing, stereo trail artifact

**Eye-Stride Motion Vectors**:
Screen-space velocity vectors computed across a two-frame interval ($2\Delta t$) representing the motion of a single eye perspective rather than the inter-eye camera jump.
_Avoid_: 2-frame velocity, stride vectors, eye-hop delta

**Virtual NGX Handle**:
An opaque proxy struct allocated by the Injector that encapsulates paired underlying `NVSDK_NGX_Handle` instances for left and right eye DLSS evaluation.
_Avoid_: Dummy handle, shadow DLSS handle, fake handle

**Dual Eye Camera Nodes**:
The left- and right-eye rendering cameras of the Companion Mod, registered with the game's camera manager so the engine renders through them. Both eyes share one camera node, moved to each eye's position on alternate frames, because the temporal upscalers drop their history whenever the rendering camera changes.
_Avoid_: Split cameras, twin cameras, alternating cameras

**Discontinuity Flush**:
The deliberate signalling of the temporal upscaler reset parameter (`Reset = 1`) to discard history upon camera teleports, snap turns, recentering, or 2D/3D presentation transitions.
_Avoid_: History purge, buffer clear, DLSS reset trigger

**Cyclopean Audio Listener Node**:
The stationary in-engine transform node positioned at the midpoint between the player's eyes to which the spatial audio listener is bound, isolating spatial acoustics from alternating eye camera hops.
_Avoid_: Neutral audio node, center ear node, mono listener node

**Render Resolution**:
The width and height the engine allocates its backbuffer at, taken from the game's display settings. Under Resolution Decoupling it is independent of the Desktop Mirror Window size, so it can match the headset's per-eye resolution even when no monitor supports that size.
_Avoid_: Game resolution, screen resolution, window size

**Headset-Native Target Upscaling**:
The operational configuration where temporal upscaler output dimensions are matched directly to the decoupled OpenXR recommended eye resolution rather than the desktop mirror window.
_Avoid_: VR DLSS resolution, mirror scaling, headset supersampling target

**Single-Context Bypass**:
The operational state where dual upscaler multiplexing is suspended during monoscopic presentation (such as 2D Floating Window Mode or GUI menus) and all frames are routed to a single feature handle.
_Avoid_: Mono upscaler fallback, 2D bypass, menu pass-through
