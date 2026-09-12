# In-Process Luau State Interception & VRBridge Table Injection

## Context and Decision
GIANTS Engine 10 executes game scripts inside a heavily sandboxed Luau runtime where standard C module loading (`package.loadlib`), file I/O, and raw memory access are stripped. Because the Injector (`dxgi.dll`) executes within the same process address space as the game engine, we decided to intercept the active `lua_State*` pointer on engine startup by installing a lightweight C-function detour on a known global engine export (`isHeadTrackingAvailable`). Once intercepted, the Injector registers a native `VRBridge` table directly into the global Luau environment.

## Status
Accepted

## Considered Options
- Shared memory / memory-mapped file with custom disk reading (rejected: Luau sandbox lacks raw memory access, and disk polling causes frame stutters).
- TrackIR / NPClient protocol multiplexing (rejected: `TRACKIRDATA` only carries 6DOF pose data, lacking fields for Oblique Frustum projection offsets, eye indices, and frame sync).
- Direct binary patching of `FarmingSimulator2025Game.exe` (rejected: breaks base-game integrity checks and fails on game updates).
- In-process Luau hook via `isHeadTrackingAvailable` (selected: zero game binary modifications, bypasses Luau sandbox cleanly, and enables zero-copy C-to-Lua telemetry).
