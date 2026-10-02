# Desktop Mirror Window Resolution Decoupling

## Context and Decision
In Farming Simulator 25, GIANTS Engine 10 couples the Direct3D 12 render target and swapchain backbuffer resolution to the Win32 desktop window (`HWND`) dimensions. When running in Active VR Mode with in-game resolution set to 4K (3840×2160) or higher on a 1440p (2560×1440) or 1080p display, the window physically spans beyond the active monitor boundaries. Because the Windows OS clamps physical mouse cursor movement (`GetCursorPos`) to the display surface, the normalized cursor coordinates in `DxgiProxy::ScreenToNormalizedClient` cannot exceed the monitor boundary ratio (e.g. 2560/3840 ≈ 0.666), trapping the virtual mouse cursor in the top-left portion of the VR menu quad and leaving the right and bottom UI controls unreachable.

To provide pristine 4K+ VR rendering while keeping the desktop mirror fully usable and mouse navigation intact across the entire menu quad, we decouple the Direct3D 12 swapchain rendering resolution from the physical Desktop Mirror Window:

1. **Native 4K Backbuffer Retention**:
   The player configures FS25 in-game resolution to 4K (or higher). GIANTS Engine natively allocates all internal D3D12 render passes, rasterizer viewports, scissor rects, and swapchain backbuffers at full 4K, which `DxgiProxy::OnPresent` copies directly into OpenXR eye swapchains without resolution loss.
2. **DXGI Hardware Scaling Enforcement**:
   In `DxgiProxy::Intercept_CreateSwapChainForHwnd`, the swapchain descriptor scaling is set to `DXGI_SCALING_STRETCH`, enabling the Desktop Window Manager (DWM) hardware scaler to scale the presentation smoothly to any window client rect without cropping.
3. **Win32 Window Subclassing & Work Area Clamping**:
   When the game window `HWND` is registered in Active VR Mode, the Injector subclasses the window procedure via `SetWindowSubclass`. It intercepts `WM_WINDOWPOSCHANGING` and applies an initial clamp during swapchain creation, sizing the window to the maximum 16:9 rectangle that fits inside the active monitor's work area (`rcWork` via `GetMonitorInfo`). Window styles are preserved and non-client borders and title bars are factored in via `AdjustWindowRectEx`, ensuring title bars remain on-screen and draggable.
4. **Coordinate & Cursor Detouring (Viewport Preservation & Bidirectional Mouse Parity)**:
   To ensure GIANTS Engine and SDL2 treat the window as having target render resolution (e.g. 3840×2160), `GetClientRect` is detoured during Active VR Mode to report the virtual render dimensions.
   - `ScreenToClient` transforms physical mouse coordinates to virtual render coordinates (0..3840, 0..2160), allowing in-engine UI raycasting and mouse input to span the full virtual framebuffer.
   - **Crucial Discovery on `ClientToScreen`**: SDL2's `WM_WINDOWPOSCHANGED` handler (`SDL_windowsevents.c`) converts `GetClientRect` corners using `ClientToScreen` to calculate `window->w` and `window->h`. Scaling `ClientToScreen` down caused SDL2 to calculate physical dimensions and dispatch `SDL_WINDOWEVENT_RESIZED`, prompting GIANTS Engine to resize its rasterizer viewport, scissor rects, and render targets down to ~1/3 of the swapchain buffer (leaving the rest black). Therefore, `ClientToScreen` remains unscaled so SDL2 retains the full 3840×2160 viewport.
   - `SetCursorPos` is detoured: When SDL2 / GIANTS Engine centers the cursor (`WIN_WarpMouseInWindow` / `wrapMousePosition` passing virtual center `(1920, 1080)`), `Intercept_SetCursorPos` scales it precisely to the physical center `(physW / 2, physH / 2)` of the clamped mirror window. This guarantees bidirectional mouse view rotation parity (symmetric travel left and right without right-border stopping).
   - `ClipCursor` is detoured: Constrains virtual clip rectangles `[0, 0, 3840, 2160]` to the physical mirror window client bounds, keeping the cursor properly confined.
5. **Pass-Through Mode Inertia**:
   In Pass-Through Mode, window subclassing and dimension clamping are bypassed, preserving stock GIANTS Engine desktop behavior.

## Status
Accepted

## Considered Options
- In-engine swapchain descriptor override (rejected: risks rasterizer viewport and scissor rect mismatch if GIANTS Engine sizes viewports from internal display settings rather than querying the backbuffer description).
- Dedicated offscreen VR render target (rejected: significant VRAM duplication, extra GPU copy passes, and unnecessary engine pipeline interference).
- Fixed 1080p Desktop Mirror Window (rejected: does not adapt to diverse display configurations or ultrawide aspect ratios).
