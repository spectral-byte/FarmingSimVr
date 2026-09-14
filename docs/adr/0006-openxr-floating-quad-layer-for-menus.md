# OpenXR Floating Quad Layer for Menus

## Context and Decision
When launching Farming Simulator 25 and sitting in the Main Menu (or anytime before an active map is loaded), GIANTS Engine 10 renders standard 2D monoscopic UI frames to the backbuffer. Warping these 2D frames across immersive stereoscopic projection layers produces severe HUD lock, reprojection swimming, and visual discomfort. Rather than forcing alternate frame rendering or projection warping in menus, we decided to present the 2D backbuffer using an OpenXR Quad composition layer (`XrCompositionLayerQuad`).

The floating quad layer is anchored at a comfortable virtual distance (~1.5m at eye level, with a 16:9 aspect ratio) in the player's local reference space (`XR_REFERENCE_SPACE_TYPE_LOCAL`). This grants the player immediate, natural 6DOF head movement: leaning in, leaning back, moving sideways, and looking around the virtual screen from different angles without HUD lock or reprojection distortion. When the player enters a map, the Companion Mod completes its native handshake via `VRBridge`, triggering an immediate, seamless transition to stereoscopic projection rendering (AFR or native stereo). Upon map unload, `VRBridge` is notified via `VRCompanion:deleteMap()`, cleanly reverting the presentation back to the floating quad layer.

## Status
Accepted

## Considered Options
- Warping monoscopic 2D frames across immersive projection layers (rejected: causes severe visual distortion, edge swimming, and uncomfortable optical disparity).
- Head-locked quad layer in view space (`XR_REFERENCE_SPACE_TYPE_VIEW`) (rejected: locks the menu screen to the player's face, preventing natural 6DOF head motion and increasing disorientation).
- World-anchored quad layer in local tracking space (`XR_REFERENCE_SPACE_TYPE_LOCAL`) with companion handshake gating (selected: delivers immediate, comfortable 6DOF head tracking in pre-game menus, supports in-menu view recentering, and transitions seamlessly to in-engine stereo rendering upon map load).
