# Dynamic Quad Centering and Background 6DOF Camera Tracking

## Context and Decision
When opening menus in Farming Simulator 25 or invoking the in-engine Recenter Action (`VR_RECENTER`), placing the floating menu quad layer (`XrCompositionLayerQuad`) at static or unaligned coordinates causes the virtual menu screen to appear off to the side, behind the user, or tilted at uncomfortable angles if the user had pitched or rolled their head. Furthermore, when navigating menus with a 3D background scene (such as vehicle customization showroom, dealership view, or in-game pause menu), freezing the active rendering camera node breaks immersion and prevents natural 6DOF head movement behind the floating UI.

To deliver natural, comfortable VR usability, we established two complementary systems:
1. **Dynamic Quad Auto-Centering and Pitch/Roll Decoupling**:
   - Whenever a menu opens or the player invokes the Recenter Action (`VR_RECENTER`), `OpenXrManager::AlignMenuQuadToHeadPose` calculates the quad layer's anchor pose directly along the player's horizontal forward gaze vector (~1.5m distance at eye level).
   - Pitch and roll tilts are explicitly decoupled from the quad anchor orientation ($q_{\text{yaw}} = (0, \sin(\text{yaw}/2), 0, \cos(\text{yaw}/2))$), keeping the virtual screen upright and comfortable without perspective roll.
   - Once anchored in local tracking space (`XR_REFERENCE_SPACE_TYPE_LOCAL`), the quad remains world-anchored, granting natural 6DOF head movement (leaning in, leaning back, looking around) until the next menu transition or recenter action.
2. **Background 6DOF Active Camera Tracking for 3D Menus**:
   - During active GUI states (`g_gui:getIsGuiVisible()`), the Companion Mod's update hook retrieves the current active rendering camera node (`getCamera()`).
   - 6DOF translation and rotation offsets from `VRBridge.getHeadPose()` are applied to the active camera node via `setTranslation` and `setRotation`, allowing the 3D showroom vehicle or farm environment to track the player's head in full 6DOF.
   - Initial base transforms are cached and cleanly restored when menus close or upon map unload, preventing camera drift or coordinate jumps.
   - 2D mouse and controller UI interactions remain completely unaffected and functional.

## Status
Accepted

## Considered Options
- Static quad anchoring in local tracking space without dynamic recenter (rejected: causes menus to appear behind or beside the player if seated orientation changes).
- Coupling pitch and roll tilts to the menu quad (rejected: creates tilted, rolled virtual screens when looking down or to the side, leading to disorientation and optical fatigue).
- Freezing background 3D camera in showroom and pause menus (rejected: creates unnatural static stereoscopic background behind 6DOF quad layer, breaking immersion in vehicle customization).
- Dynamic quad centering with decoupled pitch/roll and active 6DOF background camera tracking (selected: guarantees upright, centered floating menus in front of the player and responsive 6DOF background camera tracking during showroom and pause screens).
