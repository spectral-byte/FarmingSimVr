# In-Engine 2D Floating Window View Toggle

## Context and Decision
During extended virtual reality sessions in Farming Simulator 25, players may experience eye strain, fatigue from stereoscopic depth, or need to perform complex UI/precision tasks where a traditional flat display is preferred. To provide seamless relief without quitting the game or restarting the VR session, we designed an on-demand view mode toggle between immersive 3D stereoscopic VR and a monoscopic 2D Floating Window Mode.

We established the following architectural design:

1. **Virtual Cinema / Decoupled 6DOF Presentation Model**:
   - In 2D Floating Window Mode, in-engine camera controls behave as standard 2D FS25 (driven via mouse, gamepad right thumbstick, and vehicle interior look controls).
   - In-engine 6DOF head tracking offsets and rotations are detached from the game camera node so that head movement does not swing the virtual camera.
   - The game renders a monoscopic 2D frame to the backbuffer using standard game camera Field of View (FOV) and zero projection offsets (preventing wide-angle VR fisheye distortion on the virtual screen).
   - The Injector presents this backbuffer on an OpenXR Quad layer (`XrCompositionLayerQuad`) positioned at a comfortable virtual distance (~1.5m at eye level, 16:9 aspect ratio) in `XR_REFERENCE_SPACE_TYPE_LOCAL`.
   - The player retains natural 6DOF head movement in room space to lean in, lean back, and inspect the floating screen from any angle.

2. **Presentation Pipeline & State Coordination**:
   - The Companion Mod exposes the bindable `VR_TOGGLE_VIEW` input action (defaulting to <kbd>RCtrl</kbd> + <kbd>V</kbd> and <kbd>Numpad 0</kbd>, remappable for controllers).
   - View mode state is communicated across the Luau boundary via `VRBridge.set2DViewActive(bool)`.
   - `DxgiProxy::OnPresent` evaluates active quad mode:
     ```cpp
     bool inQuadMode = !isCompanionActive || isMenuVisible || is2DViewActive;
     ```
   - When entering 2D mode, the floating quad automatically aligns in front of the player's current head gaze via `AlignMenuQuadToHeadPose()`.
   - Switching back to 3D VR mode immediately restores stereoscopic projection (`XrCompositionLayerProjection`), AFR parity pacing, 6DOF camera tracking, and HUD convergence offsets with zero swapchain hitches.

3. **Lifecycle, HUD, and Recenter Rules**:
   - **HUD**: In 2D Floating Window Mode, convergence disparity is zeroed out and standard full-frame HUD anchors are used, avoiding redundant insets on the virtual screen.
   - **Recenter**: Triggering `VR_RECENTER` (<kbd>Numpad 5</kbd>) while in 2D mode realigns the floating quad directly in front of the player's current head gaze.
   - **Persistence**: The toggle state is sticky across in-game GUI menus (ESC pause menu, shop, map), but fresh map loads always initialize cleanly in 3D VR mode.
   - **Transition**: Transitions execute instantaneously and silently without intrusive on-screen warning popups.

## Status
Accepted

## Considered Options
- **Head-Locked 2D Overlay** in `XR_REFERENCE_SPACE_TYPE_VIEW` (rejected: attaches the flat screen rigidly to the player's face, inducing disorientation and simulator sickness).
- **Head-Tracked Flat Projection on Quad** (rejected: head tracking drives in-engine camera rotation while rendering flat on the quad, creating visual disconnect and dizziness).
- **Virtual Cinema Model with Gaze-Aligned 6DOF Local Quad** (selected: completely decouples in-game camera look from VR head tracking, restores standard game camera FOV, and allows the player to comfortably view the game as a floating 16:9 monitor in room space).
