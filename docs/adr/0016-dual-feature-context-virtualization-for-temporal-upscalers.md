# Dual Feature Context Virtualization for Temporal Upscalers

To eliminate horizontal ghosting artifacts under Alternate Frame Rendering (AFR), the Injector virtualizes temporal upscalers (beginning with NVIDIA DLSS/DLAA via NVNGX) by maintaining two isolated runtime feature handles that alternate per frame parity. This eliminates cross-eye history poisoning while the Companion Mod decouples camera history transforms across two-frame intervals to guarantee valid eye-stride motion vectors.

## Status
accepted

## Considered Options
- **Native Stereo Rendering Pass**: Rejected due to GIANTS Engine 10 instability and device removal crashes under D3D12 multithreaded command recording (established in ADR-0003).
- **History Rejection / Reactive Mask Clamping**: Rejected because forcing history rejection across stereoscopic disocclusion boundaries causes extreme edge shimmering, loss of temporal stability, and high performance overhead.
- **Dual Feature Context Virtualization**: Selected because it preserves the full temporal stability and image quality of DLSS without cross-eye contamination or engine-level pipeline restructuring.

## Consequences
- **NVNGX Detour & Proxy Handle**: The native Injector (`dxgi.dll`) dynamically detours `NVSDK_NGX_D3D12_CreateFeature`, `EvaluateFeature`, and `ReleaseFeature` on `LoadLibrary`, returning an opaque `VirtualNgxHandle` wrapping `{ hLeft, hRight }` and routing evaluation synchronously based on `VRBridge::GetFrameParity()`.
- **Dual Camera Nodes & Cyclopean Audio**: `VRCompanion.lua` alternates between two distinct scene-graph camera nodes (`cameraNodeLeft` and `cameraNodeRight`) to ensure native eye-stride motion vectors ($2\Delta t$), while spatial audio is bound to a stationary `Cyclopean Audio Listener Node` at the head pose midpoint.
- **Resolution & Monoscopic Handling**: DLSS reconstructs directly to the decoupled OpenXR recommended eye resolution (ADR-0014), bypasses virtualization in 2D Floating Window and GUI Menu modes via single-context routing (`hLeft`), and triggers a `Discontinuity Flush` (`Reset = 1`) on snap turns, recentering, and view transitions.
