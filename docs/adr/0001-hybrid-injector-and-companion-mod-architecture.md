# Hybrid Architecture: Injector and In-Engine Companion Mod via VRBridge

## Context and Decision
Rendering stereoscopic VR in Direct3D 12 requires dynamically altering camera view positions ($\pm\frac{\text{IPD}}{2}$) and applying Oblique Frustums matched to VR optics. Instead of reverse engineering and dynamically patching D3D12 constant buffers in memory—which is brittle across game patches and hazardous with multithreaded command recording—we decided on a hybrid architecture. The native Direct3D 12 Injector interfaces with OpenXR and publishes pose and projection matrices to the in-process VRBridge, while the Companion Mod running inside GIANTS Engine 10 consumes this table to drive native camera transforms and `setProjectionOffset`.

## Status
Accepted, amended: the Oblique Frustum was removed and projection is always symmetric, see [ADR-0005](0005-oblique-frustum-projection-via-vrbridge.md).

## Considered Options
- Direct D3D12 constant buffer interception and memory patching (brittle across updates, prone to multithread race conditions).
- Native C++ engine function detours (unstable across minor binary builds, requires extensive reverse engineering).
- Hybrid native Injector and Luau Companion Mod via VRBridge (selected: robust, uses native engine APIs, zero constant buffer tampering).
