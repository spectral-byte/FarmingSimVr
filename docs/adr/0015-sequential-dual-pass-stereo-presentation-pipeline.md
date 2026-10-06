# Sequential Dual-Pass Stereo Presentation Pipeline

## Context and Decision
In Farming Simulator 25, Alternate Frame Rendering (AFR) provides a computationally efficient stereoscopic presentation by alternating Left and Right eye camera perspectives on consecutive engine frames. However, AFR introduces severe visual degradation when paired with modern temporal reconstruction techniques:
1. **In-Engine TAA Ghosting**: GIANTS Engine 10 maintains a single persistent temporal accumulation buffer. Alternating camera positions across successive frames injects an artificial 64mm horizontal disparity jump ($\mathbf{v}_{\text{disparity}} = \frac{f_x \cdot \text{IPD}}{Z}$) into history reprojection, permanently blending the Left and Right eye perspectives and creating jarring double-vision.
2. **DLSS / Temporal Smearing**: Even with dual virtualized NGX feature handles (`hLeft` and `hRight`), each eye samples temporal history from $2\Delta t$ in the past while the engine simulation steps at $1\Delta t$. Multiplying motion vectors by $2.0\times$ compensates for linear translation but causes severe rotational overshoot ($\boldsymbol{\omega} \times \mathbf{r}$), disocclusion halos, edge detachment on rotating tractor wheels, and foliage crawling.

To eliminate ghosting and deliver pristine temporal reconstruction while preserving AFR as a lightweight option for midrange GPUs, we implement **Sequential Dual-Pass (SDP)** as an officially supported, user-switchable **Stereo Presentation Mode**:

1. **Double-Pumped Lockstep Presentation Engine**:
   Rather than attempting low-level D3D12 command list duplication, the presentation pipeline coordinates two sequential engine passes per compositor frame:
   - **Sub-Pass 0 (Left Eye)**: The engine executes its normal update tick with real delta time $\Delta t$. The Companion Mod activates `cameraNodeLeft` with the eye's projection (symmetric by default, Oblique Frustum when enabled; see ADR-0005). The engine renders the Left Eye scene to the swapchain backbuffer. In `DxgiProxy::OnPresent`, the backbuffer is copied to OpenXR swapchain 0 (`CopyBackBufferToEye(0)`), and the physical desktop swapchain flip is suppressed (`return S_OK`).
   - **Sub-Pass 1 (Right Eye)**: The Companion Mod activates `cameraNodeRight` and clamps the simulation clock to zero ($\Delta t = 0$) by hooking `FSBaseMission.update` / `Mission00.update`. Game world simulation, physics, vehicle velocity, and foliage wind remain frozen at the exact same logical timestamp. The engine renders the Right Eye scene to the backbuffer. In `DxgiProxy::OnPresent`, the backbuffer is copied to OpenXR swapchain 1 (`CopyBackBufferToEye(1)`), `OpenXrManager::SubmitFramePair()` is called to submit both eyes synchronously via `xrEndFrame`, and the hardware DXGI present is executed to refresh the Win32 Desktop Mirror Window.

2. **Frame-Boundary Atomic Transitions with Discontinuity Flush**:
   - Switching between AFR and Sequential Dual-Pass is queued atomically in `VRBridge` (`m_pendingRenderingMode`); the DXGI proxy latches it at the frame boundary.
   - Transitions take effect exclusively on clean compositor frame boundaries following `SubmitFramePair()`.
   - On transition, `VRBridge` resets sub-pass and parity counters to 0, and `NgxInterceptor` asserts `NVSDK_NGX_Parameter_Reset = 1` for one frame across both DLSS contexts to discard stale history cleanly without texture smearing or black frames.

3. **Context-Aware Motion Vector Scaling**:
   - Under AFR, `NgxInterceptor` maintains $2.0\times$ motion vector scaling (or user-tuned `vrDlssMvScale`) to match the $2\Delta t$ alternating stride.
   - Under Sequential Dual-Pass, motion vector scaling is automatically locked to $1.0\times$, matching the engine's true $1\Delta t$ per-eye frame cadence.

4. **Settings Persistence & In-Game Controls**:
   - Sequential Dual-Pass is the default out-of-the-box mode (`VRCompanion.DEFAULT_RENDERING_MODE`), because it avoids the TAA ghosting and DLSS smearing described above. The trade-off is GPU cost: SDP roughly doubles per-frame draw work, so midrange GPUs should switch to AFR (`vrRenderingMode afr`), which remains a fully supported option.
   - User preference is saved to `modSettings/FS25_VRCompanion/settings.xml` under `<renderingMode>AFR</renderingMode>` or `<renderingMode>DualPass</renderingMode>`.
   - Live switching is exposed via the in-game console command `vrRenderingMode <afr|dualpass|toggle>`.

## Status
Accepted

## Considered Options
- **Direct3D 12 Command List Duplication / Replay** (rejected: closed D3D12 command lists are immutable; re-recording or patching root descriptors, descriptor heaps, and split barriers across GIANTS Engine 10's 3-thread asynchronous render pool carries critical risk of `DXGI_ERROR_DEVICE_REMOVED`).
- **Undocumented `setStereoRendering(true)`** (rejected: telemetry probe in `fs25_vr.log` confirmed GIANTS Engine 10 records only 1 viewport and 1 scene pass under D3D12; legacy DX11 stub).
- **Native OS Timer Detouring for $\Delta t = 0$** (rejected: detouring `QueryPerformanceCounter` system-wide risks disrupting audio thread synchronization and Win32 message processing; Luau update hook clamping is strictly isolated to game simulation).
- **Dual-Pass Only / Deprecating AFR** (rejected: Sequential Dual-Pass increases draw calls by ~85% and requires high-end hardware like RTX 4080/4090/5080; preserving AFR as a switchable option ensures smooth playability on midrange GPUs).

## Amendment
The OpenXR frame is now waited on and begun after the previous pair's `Present`, before the engine update, so sub-pass 0 and sub-pass 1 both locate the head at the pair's predicted display time. Sub-pass 0 therefore finds a frame already begun. See [ADR-0019](0019-begin-the-openxr-frame-before-the-engine-update.md).
