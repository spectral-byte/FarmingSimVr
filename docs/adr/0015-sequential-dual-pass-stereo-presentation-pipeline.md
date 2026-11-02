# Sequential Dual-Pass Stereo Presentation Pipeline

## Context and Decision
In Farming Simulator 25, Alternate Frame Rendering (AFR) provides a computationally efficient stereoscopic presentation by alternating Left and Right eye camera perspectives on consecutive engine frames. However, AFR introduces severe visual degradation when paired with modern temporal reconstruction techniques:
1. **In-Engine TAA Ghosting**: GIANTS Engine 10 maintains a single persistent temporal accumulation buffer. Alternating camera positions across successive frames injects an artificial 64mm horizontal disparity jump ($\mathbf{v}_{\text{disparity}} = \frac{f_x \cdot \text{IPD}}{Z}$) into history reprojection, permanently blending the Left and Right eye perspectives and creating jarring double-vision.
2. **DLSS / Temporal Smearing**: Even with dual virtualized NGX feature handles (`hLeft` and `hRight`), each eye samples temporal history from $2\Delta t$ in the past while the engine simulation steps at $1\Delta t$. Multiplying motion vectors by $2.0\times$ compensates for linear translation but causes severe rotational overshoot ($\boldsymbol{\omega} \times \mathbf{r}$), disocclusion halos, edge detachment on rotating tractor wheels, and foliage crawling.

To eliminate ghosting and deliver pristine temporal reconstruction while preserving AFR as a lightweight option for midrange GPUs, we implement **Sequential Dual-Pass (SDP)** as an officially supported, user-switchable **Stereo Presentation Mode**:

1. **Double-Pumped Lockstep Presentation Engine**:
   Rather than attempting low-level D3D12 command list duplication, the presentation pipeline coordinates two sequential engine passes per compositor frame:
   - **Sub-Pass 0 (Left Eye)**: The engine executes its normal update tick with real delta time $\Delta t$. The Companion Mod activates `cameraNodeLeft` with the eye's symmetric projection (see ADR-0005). The engine renders the Left Eye scene to the swapchain backbuffer. In `DxgiProxy::OnPresent`, the backbuffer is copied to OpenXR swapchain 0 (`CopyBackBufferToEye(0)`), and the physical desktop swapchain flip is suppressed (`return S_OK`).
   - **Sub-Pass 1 (Right Eye)**: The Companion Mod activates `cameraNodeRight` and clamps the simulation clock to zero ($\Delta t = 0$) by hooking `FSBaseMission.update` / `Mission00.update`. Game world simulation, physics, vehicle velocity, and foliage wind remain frozen at the exact same logical timestamp. The engine renders the Right Eye scene to the backbuffer. In `DxgiProxy::OnPresent`, the backbuffer is copied to OpenXR swapchain 1 (`CopyBackBufferToEye(1)`), `OpenXrManager::SubmitFramePair()` is called to submit both eyes synchronously via `xrEndFrame`, and the hardware DXGI present is executed to refresh the Win32 Desktop Mirror Window.

2. **Frame-Boundary Atomic Transitions with Discontinuity Flush**:
   - Switching between AFR and Sequential Dual-Pass is queued atomically in `VRBridge` (`m_pendingRenderingMode`); the DXGI proxy latches it at the frame boundary.
   - Transitions take effect exclusively on clean compositor frame boundaries following `SubmitFramePair()`.
   - On transition, `VRBridge` resets sub-pass and parity counters to 0, and `NgxInterceptor` asserts `NVSDK_NGX_Parameter_Reset = 1` for one frame across both DLSS contexts to discard stale history cleanly without texture smearing or black frames.

3. **Context-Aware Motion Vector Scaling**:
   - Under AFR, `NgxInterceptor` maintains $2.0\times$ motion vector scaling (or user-tuned `vrDlssMvScale`) to match the $2\Delta t$ alternating stride. *Amended: this is now only the fallback while the motion vector correction is off or has disabled itself; see the amendment below.*
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

## Amendment: Eye Order Alternation (removed)
An earlier amendment swapped which eye renders in sub-pass 0 every 4 pairs, to give each eye's DLSS context the full jitter pattern (with a fixed left-then-right order each eye saw only one horizontal half of the pixel). It did not fix the blur it was meant to fix, so it was removed: sub-pass 0 is always the left eye and sub-pass 1 the right. `VRBridge::GetActiveEye()` remains and returns the sub-pass under Sequential Dual-Pass.

The amendment also corrected item 1 of the Context. The double-pumped engine still renders the eyes as consecutive frames through one pipeline, so **in-engine TAA and FSR3 keep a single history shared across eyes, and Sequential Dual-Pass does not remove their ghosting**. Only DLSS gets per-eye history (Dual Feature Context). A per-eye FidelityFX interceptor would be needed for FSR3, and native TAA has no API boundary to split.

## Amendment: Per-Eye Motion Vector Correction
The Dual Feature Context alone left DLAA/DLSS blurry under Sequential Dual-Pass, because the engine's motion vectors point at the other eye's frame, not the same eye's previous frame. The Injector now re-aims them before each per-eye evaluation, and item 3 above (motion vector scaling locked to $1.0\times$) no longer suffices on its own. The correction, the self-check and the known object-motion gap in the second sub-pass are recorded in [ADR-0020](0020-per-eye-motion-vector-correction-for-temporal-upscalers.md).

The same applies to the $2.0\times$ AFR scaling in item 3, which is superseded in the same way. The engine's motion vectors target the previous *rendered* frame, which under AFR is the other eye, so scaling them can never aim at the same eye's frame two frames back; it only lengthens a vector that points at the wrong place, which is the rotational overshoot and halos described in item 2. Under AFR the correction re-aims the camera component from the other eye's camera at the same eye's camera two frames back, and applies the eye-stride factor ($2.0$) to the engine's object-motion component only, because the simulation advances every AFR frame and the engine's object motion covers just one of the two frames. `vrDlssMvScale` now applies only while the correction is off (`vrDlssMvFix off`) or has disabled itself for the session; with the correction on it is ignored. With the correction off, AFR behaves exactly as before.

## Amendment: Desktop Mirror Window off (2026-10-28)
With the Desktop Mirror Window switched off (`vrDesktopMirror off`), the hardware DXGI present that sub-pass 1 executes is skipped. The frame that switches the mirror off presents once, black; every later frame is not presented to DXGI. The eye copies and `SubmitFramePair()` are unchanged, so the headset is unaffected. The mirror counts as off only while the OpenXR session is focused: a sleeping headset (SYNCHRONIZED) or the dashboard (VISIBLE) keeps the session running but shows the mirror again, and the next focus presents one black frame again.
