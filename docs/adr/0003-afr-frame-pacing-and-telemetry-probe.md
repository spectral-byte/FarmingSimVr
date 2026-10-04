# Alternate Frame Rendering (AFR) Pacing & Telemetry Probe

## Context and Decision
FS25 exports an undocumented function `setStereoRendering(bool)` to the Luau VM. To determine whether GIANTS Engine 10 supports internal stereo passes or split viewports under Direct3D 12, the Injector will deploy an automated telemetry probe that logs viewport counts (`RSSetViewports`), scissor rects, and render target passes. If native stereo is non-functional in D3D12, the system falls back to strict Alternate Frame Rendering (AFR), where the Companion Mod alternates camera views on successive frames ($2k$ = Left, $2k+1$ = Right) and the Injector submits synchronized eye pairs to the OpenXR compositor.

## Status
Partially superseded by [ADR-0017](0017-retire-the-d3d12-telemetry-probe.md). The telemetry probe described above is retired; the AFR frame-pacing decision stands.

## Considered Options
- Sequential Dual-Pass Command List Duplication (rejected: high risk of device removal crashes with D3D12 multithreaded render pools and descriptor heap rebinding).
- Asynchronous Single-Eye Timewarp Submission (rejected: creates visual disparity and edge swim between eyes during head motion).
- Strict Alternating L/R with Pair Submission (selected: proven temporal baseline, stable D3D12 command recording, zero reprojection artifacts between eyes).
