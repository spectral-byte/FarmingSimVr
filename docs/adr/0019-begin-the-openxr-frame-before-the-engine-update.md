# Begin the OpenXR Frame Before the Engine Update

## Context and Decision
The Injector used to wait on and begin an OpenXR frame lazily, at the first eye copy inside `Present`. The Companion Mod asks for the head pose during the engine update, before that copy. So the first eye of a pair (AFR eye 0, Sequential Dual-Pass sub-pass 0) was located for "now" (`GetCurrentXrTime`, an extrapolation from the last predicted display time), while the second eye was located inside the begun frame at its predicted display time. The two eyes of one pair were rendered from poses for different times, the first eye had no display-time prediction, and its lookups never hit the per-time pose cache.

**The frame wait moves to the end of the previous `Present`.** `DxgiProxy::OnPresentReturned()` runs after the real `Present` (or after a suppressed sub-pass 0 `Present`) and calls `OpenXrManager::BeginFrame()` (`xrWaitFrame` + `xrBeginFrame`) when the session runs and eye swapchains exist. `BeginFrame` locates the views and head pose at the frame's `predictedDisplayTime`. Every pose request for the coming pair, for either eye, then reads that frame's display-time pose from the cache.

- The next frame is begun after the pair's `xrEndFrame`, so the loop is the canonical OpenXR `wait, begin, render, end` loop. In steady state the time per pair is unchanged: the wait moves from before eye 0's copy to the end of the previous `Present`.
- The wait happens after the physical DXGI present, so the Desktop Mirror Window flip is not delayed by it (ADR-0014). Sync interval stays 0 while xrWaitFrame paces frames.
- Quad (menu / 2D Floating Window) frames use the same rule: the quad copy and `SubmitMenuQuad` consume the frame begun after the previous `Present`.
- Lazy `BeginFrame` in `CopyBackBufferToEye` stays as the fallback for the very first `Present`, and whenever a frame could not be begun early (session not yet running, `xrWaitFrame` failure). Poses requested with no frame begun still use `GetCurrentXrTime`.
- Mode switches (ADR-0015) still latch only at `SubmitFramePair`/quad boundaries. The frame begun afterwards is mode-agnostic.
- Session stop, loss and Recenter already reset or re-locate a begun frame; an early-begun frame goes through the same paths as a lazily begun one.

## Status
Accepted. Resolves #18. Amends ADR-0003 and ADR-0015.

## Consequences
- AFR: eye 0 and eye 1 are rendered one engine frame apart but from the same display-time pose, which matches what the pair is submitted with.
- A begun frame stays open while the game does not call `Present` (loading screens). The runtime then sees a frame that began but did not end; `xrWaitFrame` is not called again until the frame ends, so there is no call-order violation.

## Considered Options
- **Predict the first eye with the runtime's display period** (rejected: `last predictedDisplayTime + predictedDisplayPeriod` only approximates the time `xrWaitFrame` will return, so the two eyes would still be located at different times and the cache would not be shared).
- **Wait and begin inside `SubmitFramePair`** (rejected: the physical present of the Desktop Mirror Window would then wait behind `xrWaitFrame`).
- **Wait and begin after the physical present (selected).**
