# Per-Eye Motion Vector Correction for Temporal Upscalers

Amends [ADR-0015](0015-sequential-dual-pass-stereo-presentation-pipeline.md) and builds on [ADR-0016](0016-dual-feature-context-virtualization-for-temporal-upscalers.md).

## Context and Decision
Under Sequential Dual-Pass, the Dual Feature Context removed Cross-Eye History Poisoning, but DLAA/DLSS lost fine detail: gravel and distant texture turned to mush, even with the head perfectly still. Near objects such as the tractor stayed crisp.

**The engine's motion vectors cannot be used as they are with a Dual Feature Context.** The engine computes them against the previous *rendered* frame, which under Sequential Dual-Pass is the other eye's frame (the engine renders the eyes as consecutive frames through one pipeline). A shared history matches that, but mixes the eyes. A per-eye history is one same-eye frame back, so every pixel's vector is misregistered by the camera offset between the eyes (the IPD plus any head motion in between). The upscaler discards history for large errors, so near objects, whose offset is large, stayed sharp. It blends history for small errors, so distant detail, whose offset is a fraction of a pixel, was blurred. Setting the Sequential Dual-Pass motion vector scale to 0 with the head still made the image sharp, which confirmed the cause. Eye Order Alternation (ADR-0015) fixed the jitter coverage but not this.

**The Injector corrects the motion vectors it hands to each per-eye upscaler context.** A compute pass recorded on the game's command list just before the upscaler evaluation writes motion vectors into a texture the Injector owns:

```
W      = unproject(pixel, depth, current camera)
MV_out = MV_engine + project(same eye's previous camera, W)
                   - project(previous frame's camera, W)
```

- The engine's motion vectors keep their object-motion component. The correction replaces only the camera part.
- The pixel is unprojected from the engine's reverse-Z depth. The sky is treated as a point on the far plane, so it still receives the camera rotation.
- The camera records (world transform and projection of the eye camera each sub-pass bound) come from the Companion Mod, are stamped with a rendered-frame counter, and the relative transforms are computed in double precision before being passed to the shader in single precision.
- The upscaler reads the corrected texture for that one call. The game's parameters are restored afterwards, so a reused parameter block never carries our texture into later calls.
- The correction is divided by the motion vector scale the upscaler receives, so it stays in the engine's vector units.
- **Eye order swap**: when the previous rendered frame is already the same eye, the engine's vectors are right and no correction is applied.
- **Missing camera records** (loading, menus, the first frames of a session), a zero scale, or an unusable projection: the engine's vectors pass through unchanged.
- The pass is skipped in monoscopic presentation (menus, 2D Floating Window Mode), where Single-Context Bypass applies.
- Any failure (shader compile, unsupported format, resource creation) disables the pass for the session with a logged reason. The upscaler then keeps the engine's vectors.
- `vrDlssMvFix on|off` switches the correction for A/B comparison. `vrDlssSdpMvScale` stays at 1.0 and is no longer part of the fix.

**The self-check.** The pass can predict the engine's motion vectors for static geometry from depth and the camera records, and score them under both sign conventions on a sparse grid. Results are read back a few frames later and logged through `vrDlssDiag`, so the depth mapping, projection, units and sign are proven in game rather than assumed. Run alongside the correction, it scores the *corrected* vectors against the same eye's previous camera, where a sub-pixel error means the vectors land on the same eye's previous frame.

## Status
Accepted. The shader and its CPU mirror (`mv_reprojection`) are unit tested. The GPU path is verified in game only, through the self-check log and a visual A/B with `vrDlssMvFix`.

## FSR3
FSR3 under Sequential Dual-Pass goes through the same pass, recorded in the FidelityFX interceptor's dispatch hook before the upscale dispatch, with the same swap-and-restore of the game's dispatch description (its motion vector resource) and the same switch (`vrDlssMvFix`) and disable-on-failure (its own pass instance). Its conventions come from FSR3's own descriptors, not the DLSS creation flags: the motion vector scale from the dispatch description; the camera near, far, vertical FOV and view-space-to-metres factor from the dispatch description (near and far normalised, since reverse-depth callers pass them either way round; the camera record's projection is the fallback when they are unusable); the depth convention from the context creation flags. Conventions the pass cannot honour (forward-Z or infinite depth, display-resolution motion vectors, jitter-cancelled motion vectors) keep the engine's vectors and are named in the `vrDlssDiag` trace. The self-check runs for FSR3 dispatches too. AFR keeps the engine's vectors for FSR3 until the AFR correction is applied there.

## Known Gap: Object Motion in the Second Sub-Pass
The eye rendered in the second sub-pass runs with the simulation frozen ($\Delta t = 0$), so its engine motion vectors carry no object motion. The correction cannot recover what the engine never wrote. Moving objects (vehicles, animals) may still smear slightly in that eye. Eye Order Alternation spreads this across both eyes. Recovering it, for example by reprojecting the first sub-pass's object motion into the other eye, is future work.

## Considered Options
- **Zero motion vectors under Sequential Dual-Pass** (rejected: sharp when still, but turning the head or moving smears the whole image).
- **Fix the motion vectors in the engine or Companion Mod** (rejected: the vectors are produced inside the engine's render pipeline, with no hook; a post-process on the Injector's side of the upscaler boundary needs no engine changes).
- **Per-eye previous-frame render** (rejected: it would mean rendering each eye twice per frame).
- **Correct only the translation difference** (rejected: head rotation moves distant detail the most, and the unproject/project formulation covers rotation and translation together).

## Consequences
- Sequential Dual-Pass with DLAA/DLSS is expected to be as sharp as the old shared-context image, with no cross-eye ghosting, standing still or turning the head.
- `vrDlssMvFix` is off by default until the GPU path has been verified in game.
- One extra lightweight compute dispatch per upscaler evaluation.
- The correction reuses the camera records for AFR and FSR3 (planned). FSR3 needs a different conventions source.
- **AFR** uses the same correction instead of eye-stride scaling. The previous rendered frame is the other eye and the same eye's previous camera is two frames back; the Companion Mod reports the bound eye camera from the AFR camera updates (keyed by the frame parity), as the Sequential Dual-Pass sub-passes do. Because the simulation advances every AFR frame, the engine's object motion covers one frame where the same-eye history is two frames back: the object-motion component (the engine's vector less the static-scene prediction toward the previous rendered frame's camera) is multiplied by the eye stride (2.0), and the camera component is not. `vrDlssMvScale` applies only while the correction is off or has disabled itself, and AFR with the correction off is unchanged. The self-check under AFR scores the corrected vectors against the same eye's camera two frames back; static geometry is unaffected by the object-motion factor.
- Projection offsets (Oblique Frustum, ADR-0005) are not part of the camera record's projection used by the correction; the symmetric projection is the default.