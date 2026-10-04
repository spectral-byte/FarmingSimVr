# Retire the D3D12 Telemetry Probe

## Context and Decision
ADR-0003 deployed an automated telemetry probe that hooked the Direct3D 12 viewport, scissor, render-target and command-list-execution calls to detect whether GIANTS Engine 10 renders stereo natively. ADR-0015 already records the probe's answer: under D3D12 the engine records one viewport and one scene pass, so Alternate Frame Rendering (AFR) or Sequential Dual-Pass (SDP) is always required. The probe has no remaining decision to make, and keeping it has real costs:

- A mutex is taken on every hooked call from every render thread.
- The hooks are removed mid-game from the present thread without suspending worker threads, so an in-flight call can skip the real viewport call or execute freed trampoline memory.
- Any frame that sets two or more viewports (shadow cascades, for example) is classified as native stereo, which makes the Injector submit the same image to both eyes.

This ADR supersedes the telemetry probe portion of ADR-0003. The AFR frame-pacing decision in ADR-0003 stands.

1. **No hooks on D3D12 command list or command queue methods.** `GraphicsTelemetryProbe` and `D3D12Interceptor` are removed. The Injector installs only DXGI hooks.
2. **No native-stereo present path.** `DxgiProxy::OnPresent` presents through the floating quad layer, AFR or SDP only.
3. **VRBridge's Lua API is unchanged**, so the Companion Mod needs no change. The functions now return a fixed verdict:
   - `VRBridge.getRequiresAfrFallback()` and `VRBridge.requiresAfrFallback` are `true` from the first frame.
   - `VRBridge.isStereoActive()` is `false`.
   - `VRBridge.getStereoTelemetry()` returns `nativeStereo = false`, `afrFallback = true`, `verdict = "AfrFallbackRequired"`, and zero counters.

## Status
Accepted. Supersedes the telemetry probe in [ADR-0003](0003-afr-frame-pacing-and-telemetry-probe.md).

## Considered Options
- **Keep the probe but make it thread-safe** (rejected: the answer is already known and recorded in ADR-0015, so any hook on the render path is pure cost and risk).
- **Keep the probe but disable it by default** (rejected: dead code that still contradicts ADR-0015 and can be re-enabled by mistake).
- **Remove the probe and fix the verdict (selected).** Removes the data race and the shadow-cascade misclassification, and keeps the Companion Mod contract stable.
