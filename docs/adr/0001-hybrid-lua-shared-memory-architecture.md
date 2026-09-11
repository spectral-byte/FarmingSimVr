# Hybrid Architecture: Injector and In-Engine Companion Mod via Shared Memory

## Context and Decision
Rendering stereoscopic VR in Direct3D 12 requires dynamically altering camera view positions ($\pm\frac{\text{IPD}}{2}$) and applying asymmetric oblique projection frustums matched to VR optics. Instead of reverse engineering and dynamically patching D3D12 constant buffers in memory—which is brittle across game patches and hazardous with multithreaded command recording—we decided on a hybrid architecture. The native Direct3D 12 Injector interfaces with OpenXR and publishes pose and projection matrices to a memory-mapped Tracking Bridge, while a companion Luau mod running inside GIANTS Engine 10 consumes this buffer to drive native camera transforms and `setProjectionOffset`.

## Status
Accepted

## Considered Options
- Direct D3D12 constant buffer interception and memory patching (brittle across updates, prone to multithread race conditions).
- Native C++ engine function detours (unstable across minor binary builds, requires extensive reverse engineering).
- Hybrid native Injector and Luau Companion Mod via shared memory (selected: robust, uses native engine APIs, zero constant buffer tampering).
