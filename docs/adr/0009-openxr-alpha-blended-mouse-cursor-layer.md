# OpenXR Alpha-Blended Mouse Cursor Quad Layer for Menus

## Context and Decision
In Farming Simulator 25, GIANTS Engine 10 relies on the standard Windows hardware cursor (ShowCursor(TRUE) / Win32 API) for GUI navigation, allowing the Windows Desktop Window Manager (DWM) to composite the mouse pointer directly onto the desktop display plane. Because hardware cursor pixels are never rasterized into the Direct3D 12 backbuffer, the backbuffer captured by the VR Injector and submitted to OpenXR contains the GUI elements but completely lacks a mouse cursor.

In VR, menus are presented on a floating 3D quad layer (XrCompositionLayerQuad). Without a visible cursor, the player cannot see where they are pointing or clicking when navigating the title screen, in-game pause menu, vehicle shop, or settings screens.

To deliver intuitive and seamless menu navigation in VR without modifying engine rendering pipelines, we established an alpha-blended secondary OpenXR quad composition layer:
1. **Cursor Detection and Coordinate Mapping**:
   - DxgiProxy::UpdateMenuCursorState interrogates Win32 cursor state via GetCursorInfo.
   - When the cursor is active (ci.flags & CURSOR_SHOWING), screen coordinates are mapped to client window coordinates via ScreenToClient and normalized to [0, 1] x [0, 1].
   - When cursor is hidden (such as during in-game 3D first-person or vehicle operation), cursor rendering is automatically suppressed (`visible = false`).
2. **Dedicated Cursor Swapchain and Procedural Texture**:
   - OpenXrManager::CreateCursorSwapchain initializes a dedicated OpenXR swapchain (DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) containing a procedurally generated, crisp 64x64 arrow cursor with a high-contrast black outline, white interior, and full alpha channel.
   - Textures are uploaded using direct CPU mapping (D3D12_HEAP_TYPE_UPLOAD), eliminating runtime GPU overhead.
3. **3D Quad Plane Projection and Z-Fighting Elimination**:
   - OpenXrManager::ComputeCursorQuadPose projects the normalized cursor coordinates (u, v) to the floating menu quad's local coordinate space:
     x_local = (u - 0.5 + w_cursor / (2 * w_menu)) * w_menu
     y_local = (0.5 - v - h_cursor / (2 * h_menu)) * h_menu
   - The cursor quad is rotated by the menu quad's orientation quaternion and offset forward by 2 mm (+0.002 m) along the quad's normal vector towards the viewer, completely eliminating z-fighting and depth ambiguity.
4. **Compositor Multi-Layer Submission**:
   - During menu mode, OpenXrManager::SubmitMenuQuad submits up to two composition layers:
     - Layer 0: The floating menu quad displaying the game backbuffer.
     - Layer 1: The cursor quad layer configured with XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT.
   - The OpenXR runtime compositor alpha-blends the cursor over the menu screen at the headset's native refresh rate and resolution.

## Status
Accepted

## Considered Options
- Modifying Direct3D 12 backbuffer via custom blit shader before presentation (rejected: requires creating D3D12 render target views, descriptor heaps, root signatures, pipeline state objects, and mutating the backbuffer state transitions, introducing significant performance overhead and risking crashes or conflicts with engine pipelines).
- In-engine Luau GUI cursor overlay (rejected: GIANTS Engine GUI does not draw software cursors, and manipulating GUI elements from Luau does not give access to mouse pointer bitmaps at native resolution/refresh rates).
- Alpha-blended secondary OpenXR quad composition layer (selected: zero engine-side modifications, zero D3D12 render pipeline modifications, hardware-accelerated compositor alpha blending, native headset refresh rate and resolution, completely clean separation of concerns).
