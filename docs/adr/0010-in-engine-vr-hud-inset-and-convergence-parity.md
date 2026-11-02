# In-Engine VR HUD Inset and Convergence Parity

## Context and Decision
In Farming Simulator 25, GIANTS Engine 10 renders 2D HUD interface elements (speedometer, minimap, input help, clock, cash, and context action notifications) directly into the primary swapchain backbuffer. During active stereoscopic VR projection rendering (`XrCompositionLayerProjection`), elements anchored at default normalized screen corners `(0, 0)` and `(1, 1)` are distorted by asymmetric oblique frustum projections and pushed into extreme peripheral angles outside the optical sweet spot and lens display borders of the VR headset. Furthermore, continuous head-locked 2D elements cause ocular convergence strain and visual fatigue against the 3D stereoscopic scene.

To deliver a comfortable, responsive, and immersive HUD in VR without fragile or invasive Direct3D 12 render-target duplication, we established the following architectural design:

1. **In-Engine VR HUD Inset & UI Scale** *(Amended by [ADR-0022](0022-per-group-hud-margins-and-a-separate-safe-area.md): each HUD Group has its own margins and scale, and the anchors keep only a HUD Safe Area.)*:
   - The Companion Mod contracts the engine's global anchors (`g_hudAnchorLeft`, `g_hudAnchorRight`, `g_hudAnchorTop`, `g_hudAnchorBottom`) inward by configurable safe margins (defaulting to 20% horizontal and 16% vertical inset).
   - A dedicated VR UI scale multiplier (defaulting to 0.88x) is applied, keeping telemetry compact and legible within the central ~60° viewing cone.
   - Values are exposed in configuration for fine-tuning across different headset fields of view.

2. **Physical IPD-Scaled Convergence Parity**:
   - In Alternate Frame Rendering (AFR), the 3D scene renders alternating Left (parity 0) and Right (parity 1) eye perspectives.
   - If 2D UI elements are drawn at identical pixel coordinates on both frames, they appear at optical infinity (zero disparity) or cause ocular divergence against near-field 3D geometry (such as tractor steering wheels or windshields).
   - The Companion Mod dynamically modulates the horizontal anchors by $\pm \Delta x$ across alternating frame parity based on the headset's physical IPD from `VRBridge.getIpd()`. This produces true stereoscopic convergence, causing the visual cortex to perceive the HUD floating at a comfortable virtual reading depth (~1.8 meters) without double-vision.

3. **HUD Toggle Action with Three-Stage Cycle**:
   - For maximum immersion, the HUD is hidden by default upon map loading.
   - An in-engine bindable action (`VR_HUD_TOGGLE`) allows the player to cycle through three states:
     - `Hidden`: Pure immersion, zero 2D overlay clutter.
     - `Streamlined`: Displays essential driving and machine telemetry (speedometer, gear, RPM, fuel, vehicle damage, implement fill levels, and context action prompts) while suppressing bulky secondary windows (minimap and input help table).
     - `Full`: Unhides all base-game HUD elements with full VR insets and convergence disparity.

## Status
Accepted, amended: the global VR HUD inset and UI scale (item 1) are replaced by per-group HUD margins and a separate HUD Safe Area, see [ADR-0022](0022-per-group-hud-margins-and-a-separate-safe-area.md). The convergence parity (item 2) and the three-stage toggle (item 3) stand, with the Streamlined and Full contents now set per group. Also amended: the Oblique Frustum this ADR cites as distorting the HUD was removed and projection is always symmetric, see [ADR-0005](0005-oblique-frustum-projection-via-vrbridge.md).

## Considered Options
- Unmodified corner screen-space rendering (rejected: corners are pushed outside VR headset optics, causing chromatic aberration, lens cutoff, and illegibility).
- Native D3D12 multi-target separation for an OpenXR Quad HUD overlay (rejected: GIANTS Engine 10 does not support independent UI render targets in Luau; intercepting multithreaded render worker draw lists introduces high risk of device removal crashes and descriptor leaks).
- Permanent always-on full HUD (rejected: occupies excessive visual field in VR, causes visual fatigue, and obscures in-cab mirrors and field visibility).
- In-Engine VR Inset, IPD-scaled Convergence Parity, and Three-Stage Latching Toggle (selected: native GIANTS Engine UI compatibility, zero D3D12 performance penalty, true stereoscopic depth at ~1.8m, and user control over immersion and telemetry).
