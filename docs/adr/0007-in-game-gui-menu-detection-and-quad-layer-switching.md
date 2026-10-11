# In-Game GUI Menu Detection and Quad Layer Switching

## Context and Decision
During active gameplay in Farming Simulator 25, the player frequently opens in-game 2D menus such as the ESC pause menu, the store/dealership, the map overview, production chains, and vehicle customization screens. In these states, GIANTS Engine 10 renders 2D UI elements across the viewport. Submitting these frames across the stereoscopic projection layer (`XrCompositionLayerProjection`) produces uncomfortable HUD lock, visual disparity, and eye strain.

To solve this, we extended the hybrid VR modding architecture with dynamic presentation mode switching:
1. The Companion Mod polls in-engine GUI visibility every update tick via `g_gui:getIsGuiVisible()`.
2. State transitions are immediately communicated to the native Injector via `VRBridge.setMenuVisible()`.
3. The Injector (`DxgiProxy`) dynamically routes swapchain frame submission between `XrCompositionLayerProjection` (active 3D gameplay) and `XrCompositionLayerQuad` (floating 6DOF quad layer for menus).
4. Frame parity and swapchain copied states are synchronized on transition boundaries, ensuring seamless, zero-stutter transitions without swapchain acquisition deadlocks or descriptor leaks.

## Status
Accepted

## Considered Options
- Static projection rendering during menus (rejected: forces monoscopic 2D menus onto warped stereoscopic projection eyes, causing optical disparity, nausea, and HUD lock).
- Head-locked overlay quad in view tracking space (rejected: glues the menu to the player's face, preventing natural 6DOF head inspection and causing disorientation).
- In-game GUI detection with dynamic 6DOF quad layer switching via `VRBridge` and `g_gui:getIsGuiVisible()` (selected: automatically detects any active menu, immediately presents the backbuffer on a comfortable floating quad at eye level with full 6DOF tracking, and smoothly restores Alternate Frame Rendering (AFR) stereoscopic gameplay as soon as the menu is dismissed).
