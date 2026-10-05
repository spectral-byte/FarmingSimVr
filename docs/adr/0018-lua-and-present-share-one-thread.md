# Companion Mod Calls and Present Run on One Thread

## Context and Decision
Issue #80 asked whether Companion Mod (Luau) calls into `VRBridge` and swapchain `Present` run on different threads, because VRBridge mixes atomic and plain fields, a Recenter Action recreates the local reference space that frame submission uses, and the menu quad pose is written from Lua and read at present. ADR-0015 mentions a 3-thread asynchronous render pool, which suggested a possible data race.

`ThreadTrace` logged the thread ID at the Lua calls `VRBridge.recenter`, `setMenuVisible`, `set2DViewActive`, `setRenderingMode` and `getHeadPose`, and at `DxgiProxy::OnPresent`, in a real VR session. The Lua calls and Present reported the same thread ID.

1. **No cross-thread command queue.** Lua-originated commands (recenter, menu and 2D Floating Window Mode changes, Stereo Presentation Mode changes) are applied directly, because they already run on the present thread. Recenter cannot destroy a reference space while Present is using it.
2. **The thread trace stays in place** as a regression canary, extended to the other Lua calls that touch shared state: `alignMenuQuad` (which writes the menu quad pose read at present), `getEyeProjection`, `getEyePose` and `onMapUnload`. A `[WARN] [ThreadTrace]` line that reports a changed thread ID (`was N`) means the assumption no longer holds and this ADR must be revisited. Each call site logs at most a few changes, so threads that alternate every frame can't flood the log.
3. **Render pool threads were not traced.** This ADR covers only the Lua thread and Present. If a render pool thread turns out to call into `VRBridge` or OpenXR, that path is unguarded and this ADR must be revisited.

## Status
Accepted. Resolves #80.

## Considered Options
- **Queue Lua commands and apply them on the present thread** (rejected: the measured threads match, so the queue would add latency and code for no race).
- **Lock VRBridge and OpenXrManager state** (rejected: same reason, and it would put a mutex on the per-frame path).
- **Document the measured model and keep the trace (selected).**
