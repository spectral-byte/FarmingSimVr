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

**Motion Controller**:
A tracked handheld VR input device, one per hand. The player may set it down at any time and keep driving with a Wheel Peripheral, gamepad, or keyboard.
_Avoid_: VR controller, wand, hand controller, controller (in the Interactive Control mod a "controller" is a switch or lever)

**Virtual Gamepad**:
The Xbox-style gamepad the game sees in place of the Motion Controllers' triggers, grips, thumbsticks and buttons, so that every one of them is bound in the game's own Controls screen like any gamepad. Each physical control on each hand is its own gamepad input; a binding stays with the physical hand, never with the Pointer Hand. Hand poses never go through it. While the mod itself is using an input (a menu is showing, except the pause menu's Settings page, where the player binds the pad in the Controls screen, and a Stereo Dialog, which is answered with the pad's own menu buttons; the Pointer Ray is on a Click Point, a Virtual Hand is holding the Steering Wheel, the Tool Hand's trigger while it holds a Hand Tool), that input reads as released on it, except that an input already held when that use begins keeps its binding until it is let go. Out of the box its bindings are VR-comfortable (Snap Turn on the right stick instead of smooth look); the Recenter Action keeps the game's default, the left stick click, because the game does not fire it while it is bound to the right stick click. It is on by default; the player can turn it off, and the Motion Controllers then bind nothing.
_Avoid_: Emulated controller, fake pad, XInput pad, virtual controller

**Wheel Peripheral**:
A physical steering wheel connected to the PC as a game input device.
_Avoid_: Wheel, racing wheel, steering wheel (that is the Steering Wheel in the cab)

**Steering Wheel**:
The modelled wheel in a vehicle's cab that turns as the vehicle steers. Not every vehicle has one.
_Avoid_: Wheel, cab wheel, steering (alone)

**Wheel Grab**:
Holding the Steering Wheel with a Motion Controller to steer the vehicle being driven, from its in-cab camera, by squeezing the grip with the Virtual Hand at the rim, with one hand or both. The Steering Wheel turns exactly as far as the hands do, so full lock may take turning hand over hand; while it is held, the hand's steering wins over every other steering input, and letting go hands steering back to the game, which centres the wheel as it does for any other input. It can be switched off on its own.
_Avoid_: Grab steering, hand steering, VR steering, wheel grip

**Pointer Hand**:
The Motion Controller currently pointing. It moves on its own to whichever Motion Controller the player is using, so neither hand is fixed as the pointer. Only the Pointer Hand casts a Pointer Ray. A hand holding the Steering Wheel is never the Pointer Hand, nor is the Tool Hand while it holds a Hand Tool, except in a menu.
_Avoid_: Dominant hand, main hand, laser hand

**Pointer Ray**:
The ray from the Pointer Hand that selects what the player is aiming at: a Click Point, or the cursor position on a menu or 2D Floating Window Mode quad. It starts at the index fingertip of the Pointer Hand's Virtual Hand and runs along the finger, in the world and on a menu or 2D Floating Window Mode quad alike, whether or not the Virtual Hand is drawn; it uses the relaxed finger, so curling the finger on the trigger does not move it. (Only while the Motion Controller's grip pose is not tracked does it start at the aim pose instead.) It is shown only while the Pointer Hand is tracked and in use and there is something it could select.
_Avoid_: Laser, beam, raycast, gaze pointer

**Stereo Dialog**:
A game dialog, named in a short list the Companion Mod keeps (for now only the fish catch dialog of the Highlands Fishing DLC), that stays in the stereoscopic view instead of getting the menu quad, so the player keeps the 3D world, head tracking, the Virtual Hands and the Tool Hand while it waits. It is a menu for the input: locomotion, Snap Turn and Wheel Grab are refused, and the Virtual Gamepad sends the Motion Controllers' buttons to it (or, without Motion Controllers, the player's own gamepad or keyboard answers it), so the dialog's own button hints answer it. The Pointer Ray does not reach it. It is drawn at zero disparity for now. In 2D Floating Window Mode, when it opens over another menu or dialog, and for any dialog not on the list, the menu quad is used as for any menu. See ADR-0032.
_Avoid_: Stereo menu, 3D dialog, in-world dialog, VR dialog

**Virtual Hand**:
A gloved hand drawn in the 3D world where a Motion Controller is held (or, while it holds the Steering Wheel, on the rim where it grabbed, turning with the wheel; while it is the Tool Hand holding a Hand Tool, closed around the tool's handle with only the index finger following the trigger), its fingers curling with the trigger and grip, one per Motion Controller, shown while that Motion Controller is in use and Motion Controllers are switched on. Like the Pointer Ray, it appears on foot, in the cab and in an Outside Camera, never in a menu or 2D Floating Window Mode; a Stereo Dialog is not a menu in this sense, so the Virtual Hands stay. Only the player holding the Motion Controllers sees their Virtual Hands; other players in multiplayer do not. The Pointer Ray of the Pointer Hand starts at the index fingertip of its Virtual Hand.
_Avoid_: Hand model, controller model, avatar hand

**Click Point**:
A clickable target the Interactive Control mod places in a cab, on a vehicle's exterior, or on a placed object such as a building, shown as an icon.
_Avoid_: IC button, hotspot, interactive point

**Hand Tool**:
A tool the player carries and uses on foot, such as the flashlight, the chainsaw, the pressure washer lance, the spray can (which marks trees), the horse brush or the fishing rod (from the Highlands Fishing DLC; not "fishing pole"); the game's own term. The player gets and switches Hand Tools the game's way. Empty hands, which the game also counts as a hand tool, is not a Hand Tool here.
_Avoid_: Handtool, equipment, item, tool (alone)

**Tool Hand**:
The Motion Controller that holds the Hand Tool the player is carrying: the right one, unless the player chooses the left. While it is tracked, the Hand Tool sits in its Virtual Hand, closed around the handle, and follows it: the player aims the Hand Tool, and whatever they would otherwise target by looking, with the tool itself (the chainsaw cuts where its bar meets the trunk, at the angle the wrist is rolled to; the spray can marks a tree up to about a metre from its nozzle; the horse brush cleans an animal it is touching; the fishing rod casts the way it points), its trigger uses the Hand Tool (one pull, one tree marker; hold to charge a cast, let go to throw it), and it vibrates while the chainsaw runs, the lance or the spray can sprays, the brush cleans an animal, or a fish bites or pulls on the line. A chainsaw cut keeps the height and angle it started with; taking the bar off the trunk stops it. While it holds a Hand Tool, no crosshair is drawn in the middle of the view; instead the spray can ticks when its nozzle comes onto a tree it can mark, and names its marker type when it is taken in hand or the type is changed. The fishing rod keeps its own cast meter and bite prompts. While it holds a Hand Tool it is never the Pointer Hand in the world. While it is set down or not tracked, the Hand Tool goes back to where the game itself puts it, in front of the view, and is aimed by looking again; holding the fishing rod still while its line is out does not count as setting it down. Only the flashlight, the chainsaw, the pressure washer lance, the spray can, the horse brush and the fishing rod go into the Tool Hand; every other Hand Tool, such as another mod's, stays where the game puts it. It can be switched off on its own, and Hand Tools then behave as in the game.
_Avoid_: Dominant hand, main hand, tool controller
