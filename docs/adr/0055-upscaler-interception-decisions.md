# Decisions behind Upscaler Interception and the Motion Vector Correction

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0016](0016-dual-feature-context-virtualization-for-temporal-upscalers.md), [ADR-0020](0020-per-eye-motion-vector-correction-for-temporal-upscalers.md).

## Context and Decision
This record holds the non-obvious decisions behind the NGX and FidelityFX interceptors, the per-eye motion vector correction and the near plane estimate. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `src/fidelityfx/ffx_defs.h`

- **the file**: Minimal declarations of the FidelityFX API (ffx-api, amd_fidelityfx_loader_dx12.dll) that the interceptor touches: the five entry points, the header every descriptor starts with, and the upscaler's create and dispatch descriptors. Layouts follow the public ffx_api.h / ffx_upscale.h; only the upscale descriptors are interpreted, every other descriptor type is forwarded untouched. The constants, struct layouts, return codes and descriptor type ids were compared with ffx_api.h, ffx_api_types.h and ffx_upscale.h of SDK v1.1.3 and v1.1.4 (not run against the game). The resource state values used to be wrong (COMPUTE_READ and PIXEL_READ were one bit too low, so the motion vector texture was declared as UNORDERED_ACCESS | COMPUTE_READ); FfxDefsTest pins them as 4, 8 and 12. FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION is not in those headers and is only a name in the create flag log.

### `src/fidelityfx/ffx_mv_conventions.h`

- **`FfxMvConventions`**: The conventions the motion vector correction needs for one FSR3 dispatch. FSR3 carries them in its own descriptors, so they are read from the dispatch description (scale, jitter, camera near/far/FOV) and the flags the context was created with (depth convention), never from the DLSS creation flags.
- **`FfxMvConventions`, `hasProjection`**: The dispatch's camera fields give a usable projection (finite near/far, a sane FOV). Otherwise the camera record's projection is used.
- **`ReadFfxMvConventions`, `if (!c.depthInverted) {`**: The pass is built for reverse-Z depth (finite or infinite far plane), render-resolution vectors with no jitter in them. The motion vector scale may be negative (the engine's vector can point the other way from FSR3's convention): the sign stays in the scale, which converts stored vectors to pixels.
- **`ReadFfxMvConventions`, `factor`**: FSR3 takes near and far in view-space units (scaled to metres by viewSpaceToMetersFactor, 0 meaning unset) and, for reverse depth, applications pass them either way round: normalise.
- **`ReadFfxMvConventions`, `nearPlane`**: Infinite far plane: only the near plane matters. Applications leave the other field as FLT_MAX, 0, or swap the near plane into it, so take the smallest positive finite one.

### `src/fidelityfx/fidelityfx_interceptor.cpp`

- **`s_host`**: The instance whose detours are live. The detour entry points are plain function pointers, so they resolve the interceptor that installed them here instead of reaching for a singleton.
- **`FidelityFxInterceptor::~FidelityFxInterceptor`, `(void)m_mvPass.release();`**: Releasing GPU objects while the process is going down is not safe; the OS reclaims them.
- **`FidelityFxInterceptor::ApplyMvCorrection`, `correctionRequest.objectMotionFactor =`**: AFR: the simulation advances every frame, so only the engine's object motion needs the eye stride (the same eye's history is two frames back). Sequential Dual-Pass replays one time.
- **`FidelityFxInterceptor::AttachDirect`, `previousHost`**: Publish the host before any detour goes live: a game thread can enter a detour the moment it is enabled, and an entry point with no host fails the call.
- **`FidelityFxInterceptor::RecordDiagnostics`, `if (desc.reset) reset = true;`**: The engine may request a reset itself, not only our Discontinuity Flush.
- **`FidelityFxInterceptor::Intercept_Configure`, `left`**: Both eye contexts must hold the same settings.
- **`FidelityFxInterceptor::Intercept_Query`, `left`**: Both contexts share one description, so they answer alike.
- **`FidelityFxInterceptor::Intercept_Dispatch`, `if (desc->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {`**: Not an upscale dispatch: there is no eye to route and no history to reset.
- **`FidelityFxInterceptor::Intercept_Dispatch`, `bypass`**: The motion vector pass: FSR3 reads the Injector's texture for this one dispatch. Monoscopic presentation (menus, 2D Floating Window Mode) keeps the engine's vectors.
- **`FidelityFxInterceptor::Intercept_Dispatch`, `if (wroteReset) upscale->reset = false;`**: Always undo our writes so a reused descriptor never carries our reset or texture into later dispatches.

### `src/fidelityfx/fidelityfx_interceptor.h`

- **`FidelityFxInterceptor`**: Gives FSR3 a Dual Feature Context like DLSS: an upscaler context created through the FidelityFX loader becomes two contexts with the same description behind one virtual handle, and dispatches route to the active eye's context. Mirrors NgxInterceptor (virtual handle, eye routing, Single-Context Bypass, reset on Discontinuity Flush). Context types it does not understand, such as frame generation, pass through untouched.
- **`FidelityFxInterceptor`, `IsVirtualContext`**: The handle value the game holds (what ffxCreateContext wrote), not a pointer to it.
- **`FidelityFxInterceptor`, `SetMotionVectorPassFactory`**: Motion vector correction. Under Sequential Dual-Pass an upscale dispatch for an eye context first runs the same motion vector pass as DLSS, then FSR3 reads the corrected texture for that one call and the game's dispatch description is restored. The switch is the DLSS one (vrDlssMvFix on the NGX interceptor); the pass, and its disable-on-failure, are this interceptor's own.

### `src/ngx/mv_pass.cpp`

- **`MvPassShaderSource`, `return R"hlsl(`**: "main" is the pass-through: the output is the engine's motion vector, unchanged. "correct" mirrors mv_reprojection.cpp (CorrectMotionVector): it unprojects the pixel from reverse-Z depth through the current camera and adds project(same eye's previous camera) - project(previous rendered frame's camera), in pixels divided by the MV scale, to the engine's vector. The sky is a point on the far plane. Where a projection is impossible the engine's vector is copied. "selfcheck" mirrors mv_reprojection.cpp (EvaluateSample): per sample on a sparse grid it unprojects the pixel from reverse-Z depth through the current camera, projects it through the previous rendered frame's camera and writes the error of the engine's vector (times the MV scale DLSS receives) against that prediction, under both sign conventions, and also the scaled and raw engine vectors and the prediction themselves (summed on the CPU into mean magnitudes and least-squares ratios). It writes only to the results buffer.
- **`MvPassCompileShader`, `return ok;`**: The compiler stays loaded for the process: its COM objects were released above, and a later Record never needs it again, but unloading buys nothing.
- **`Record`, `Transition(cl, m_output.Get(), m_outputState, D3D12_RESOURCE_STATE_UNORDERED_...`**: Our own texture: shader resource (as the upscaler reads it) -> UAV for the write -> back. The engine's depth and motion vectors are only read, in the state the upscaler already needs.
- **`Record`, `if (in.selfCheck && EnsureReadbackBuffers()) {`**: The self-check only measures: it reads the engine's depth and vectors and writes the results buffer, which is copied to a readback buffer that is read a few evaluations later. A near plane estimate always reads the engine's original vectors, even when the correction ran.
- **`PollSelfCheck`, `ready`**: Every element carries the stamp of the evaluation that wrote it; a mismatch means the GPU has not run that copy yet (or a later one overwrote it), so look again later.
- **`D3D12MotionVectorPass`, `TimingSlot`**: The pass records on the game's command list, so no fence says when its timestamps are written: they are read many calls later, and a slot whose start stamp is unchanged since its last read has not run yet.
- **`EnsureReadbackBuffers`, `LogWarn("MvPass: self-check readback buffer creation failed (" + HrString(hr)...`**: The self-check is a diagnostic: losing it must not cost the pass itself.

### `src/ngx/mv_pass.h`

- **`MvSelfCheckRequest`**: The motion vector pass recorded on the game's command list just ahead of an upscaler evaluation. It reads the engine's depth and motion vectors and writes motion vectors into a texture the Injector owns. The GPU work sits behind this interface so the interceptor is testable with a fake. Asks the pass to predict the engine's motion vectors for static geometry from depth and the camera records and to score the engine's vectors against the prediction. The prediction is only measured: the pass output stays a copy of the engine's vectors. The pass fills in the render aspect itself.
- **`MvSelfCheckRequest`, `nearEstimate`**: The check feeds the automatic near plane estimate: it always targets the previous rendered frame's camera and reads the engine's original vectors, and its report carries a mvreproj::NearSolution computed with current.nearClip as the assumed near.
- **`BuildMvSelfCheckRequest`**: Builds the self-check request for one evaluation from the raw camera records, or says why it cannot (missing record, unusable projection). The prediction targets the previous rendered frame's camera; when corrected is a Correct plan the evaluation's vectors are re-aimed at the same eye's previous camera, so it targets that camera instead. Built from the records, not the plan, so it also works on frames the plan declines to correct.
- **`MvCorrectionRequest`**: Asks the pass to re-aim the engine's motion vectors from the previous rendered frame's camera at the same eye's previous camera (see mv_reprojection.h). Absent (null) means the pass output is a plain copy of the engine's vectors. The pass fills in the render aspect itself.
- **`MvCorrectionRequest`, `toSameEye`**: current camera space -> same eye's previous camera
- **`MvCorrectionRequest`, `objectMotionFactor`**: Multiplies the engine's object-motion component only (1 under Sequential Dual-Pass, the eye stride under Alternate Frame Rendering). See mvreproj::CorrectMotionVector.
- **`MvCorrectionRequest`, `gateNonStatic`**: Pixels whose engine vector is not static-consistent (mvreproj::IsStaticConsistent) keep it unchanged. NGX turns it on. FidelityFX leaves it off: its correction is verified as it is.
- **`MvPassInputs`, `depth`**: The engine's resources, read in the state the upscaler already requires (never transitioned).
- **`MvPassResult`, `output`**: The Injector's motion vector texture, in the state the upscaler requires. Valid until the upscaler evaluation that follows has been recorded.
- **`IMotionVectorPass`, `PollSelfCheck`**: Self-checks whose results have been read back since the last call. The readback lags the recording by a few evaluations (no fence on the game's queue).

### `src/ngx/mv_reprojection.cpp`

- **`ReverseZToViewDepth`, `if (!(depth > nearClip / kInfiniteSkyViewDepth)) return kInfiniteSkyViewDepth;`**: depth \<= near / cap would exceed the cap (and 0 divides by zero): that is the sky.
- **`EvaluateSample`, `rotP`**: The same point as if infinitely far: only the relative rotation moves it. Depth cancels in the projection, so the unit direction stands in for the point.
- **`SolveNear`, `retained`**: At candidate scale s a term is outside the near field when its view depth z0 / s reaches the cutoff (a term without a depth always is), and an inlier when its vector matches the static prediction R + s T.
- **`SolveNear`, `ratios`**: Candidate scales: the per-sample ratios (quantiles when there are many). The best candidate is the one most samples agree with; a camera-attached or moving minority cannot pull it the way it pulls a mean.

### `src/ngx/mv_reprojection.h`

- **`namespace fs25vr::mvreproj {`**: CPU mirror of the motion vector self-check shader (see mv_pass.cpp). The shader and these functions implement the same steps, so the conventions they share are pinned down by unit tests that need no GPU.

  Conventions (all assumptions the self-check exists to prove in game):

  - Depth is reverse-Z (DLSS creation flag DepthInverted): 1 at the near plane, 0 at the far plane.
  - Camera space is the engine camera node's: +X right, +Y up, looking down -Z.
  - Pixel coordinates have the origin top-left, +Y down; pixel centres sit at +0.5.
  - The motion vector DLSS receives is the engine's vector times the MV scale it is given, in pixels. Two sign conventions are scored: "to previous" (vector = previous position - current position, which is DLSS's documented convention) and "to current" (the opposite).

- **`Projection`, `infiniteDepth`**: Reverse-Z with an infinite far plane (FSR3 DepthInfinite): view depth = nearClip / depth, depth 0 is infinitely far and farClip is unused. Only the camera that rendered the depth buffer reads it.
- **`kInfiniteSkyViewDepth`**: View depth given to a sky pixel (depth 0) under an infinite far plane: far enough that translation is below a hundredth of a pixel, near enough for float32 to keep the rotation exact.
- **`kStaticGateAbsPx`**: Static-consistency gate

  The correction and the near estimate both assume static world pixels. A pixel is static-consistent when the engine vector e (pixels, after the scale) is within max(kStaticGateAbsPx, kStaticGateRelative \* |d|) of the static prediction d toward the previous rendered frame's camera. Camera-attached geometry (e is about 0 while d is large) and moving objects fail it. Shared by the CPU mirror and the "correct" shader.

- **`kMinNearSamples`**: Near plane estimate

  For static geometry the engine vector (pixels, after the scale) is e = R + T / near_true, where R is the rotation-only displacement and T the translation part of the prediction to the previous rendered frame's camera, computed with an assumed near n0 (T = predicted - R, scaling as 1/near under infinite reverse-Z). Least squares gives s = sum((e - R).T) / sum(T.T) and near_true = n0 / s.

- **`kMinInformativeTranslationSq`**: A sample only informs the ratio when its translation is at least a pixel; below that every s is consistent.
- **`kNearFieldViewDepth`**: Samples closer than this (metres, reconstructed at the candidate scale: view depth z0 / s) are camera-attached geometry or hands and tools; they are left out of the fit. Evaluated per candidate, so a wrong estimate cannot exclude the world and lock itself in.
- **`SolveNear`**: Solves for the renderer's near from a self-check recorded against the previous rendered frame's camera (not the same-eye one) on the engine's original vectors, with n0 = assumedNear. A RANSAC-style search over candidate scales keeps the one with the most samples that pass IsStaticConsistent (camera-attached and moving samples fail it) outside the near field, then refines it by least squares over those inliers. Refuses s \<= 0, too few samples, and too low an inlier count or fraction.
- **`UnprojectAtViewDepth`**:

  ```text
  Per-eye correction (Sequential Dual-Pass)

  The engine's motion vector points at where the pixel was in the previous *rendered* frame, which under
  Sequential Dual-Pass is the other eye's frame. A per-eye upscaler history is one same-eye frame back,
  so the vector is re-aimed: unproject the pixel through the current camera, then add
    project(same eye's previous camera, W) - project(previous rendered frame's camera, W)
  (pixels) to it. The camera part is replaced; the engine's object motion stays.
  ```

- **`CorrectMotionVector`**: Corrects one pixel's engine vector (in the units DLSS receives it: the stored value times the scale). outX/outY receive the corrected stored vector. Returns false and leaves the engine's vector untouched when no correction is possible (a scale of zero, or the point behind either camera). The sky corrects like a point on the far plane, which carries the camera rotation.

  With gateNonStatic a pixel that is not static-consistent (IsStaticConsistent: the engine vector against the static prediction toward the previous rendered frame) keeps the engine's vector and returns false.

  objectMotionFactor multiplies the engine's object-motion component (its vector minus the static-scene prediction toward the previous rendered frame) and nothing else. Sequential Dual-Pass leaves it at 1: the second sub-pass replays the same simulation time. Alternate Frame Rendering advances the simulation every frame, so the engine's object motion spans one frame while the same eye's history is two frames back: it passes kAfrEyeStride. The camera component spans the right gap by construction.

### `src/ngx/near_estimator.cpp`

- **`NearEstimator::Submit`, `if (solution.used < mvreproj::kMinNearSamples || !(solution.inlierFraction >=...`**: Low confidence: hold the previous value rather than let a poorly supported solution into the window.
- **`NearEstimator::Submit`, `if (m_published > 0.0f && m_window.size() >= kJumpSolutions) {`**: A real jump (a different camera): the latest kJumpSolutions agree with each other, and the published value is not within the plausible factor of them. The window restarts from those solutions.

### `src/ngx/near_estimator.h`

- **`NearEstimator`**: Smooths the per-evaluation near plane solutions (mvreproj::SolveNear) into the one value the correction uses: a median over the last kWindow confident solutions, published with hysteresis so it does not jitter, and nothing at all until a few solutions exist (the caller then falls back to the camera record's near). Holding the previous value beats publishing a spike: solutions with too few inliers are dropped, and the published value stays within kPlausibleFactor of the previous one unless kJumpSolutions consecutive solutions agree on a new value (a camera change: cab versus on foot). Thread-safe: solutions arrive from the render thread, a reset can come from the script thread.
- **`NearEstimator`, `Reset`**: Forgets everything, and ignores solutions recorded on evaluations before fromEvalIndex (their readbacks may still be in flight when the scene changes).

### `src/ngx/ngx_interceptor.cpp`

- **`s_featureHost`**: The instance whose detours are live. The detour entry points are plain function pointers, so they resolve the interceptor that installed them here instead of reaching for the singleton.
- **`NgxInterceptor::~NgxInterceptor`, `(void)m_mvPass.release();`**: Releasing GPU objects while the process is going down is not safe; the OS reclaims them.
- **`NgxInterceptor::MatchesNgxName`, `if (s.find(L"dlss") != std::wstring::npos ||`**: Explicitly reject child plugin DLLs (Super Resolution, Frame Generation, Ray Reconstruction, Update service) to ensure we hook the core runtime / loader.
- **`previousHost`**: Publish the host before any detour goes live: a game thread can enter a detour the moment it is enabled, and an entry point with no host fails the call.
- **`NgxInterceptor::GetTargetUpscalingDimensions`, `proxy`**: Pass-Through Mode: an inactive proxy would report the mirror target resolution, not the headset's.
- **`NgxInterceptor::AttachModule`, `if (!pfnAlloc || !pfnDestroy || !pfnGetCaps) {`**: Fall back to nvngx.dll if parameter management functions are not exported by this module (e.g. nvngx_dlss.dll)
- **`NgxInterceptor::NotifyModuleLoaded`**: LoadLibrary can be hooked only once per process and this interceptor owns those hooks, so a load of the FidelityFX loader is routed to its interceptor from here.
- **`NgxInterceptor::InstallModuleLoadHooks`, `noHost`**: Publish the host before any detour goes live. A LoadLibrary detour with no trampoline calls the hooked LoadLibrary again and recurses until the stack overflows, so the detours must never see a host other than the one whose trampolines they are. LoadLibrary can be hooked only once, so a second interceptor stays out while another owns the hooks.
- **`NgxInterceptor::RecordDiagnostics`, `resetParam`**: The engine may request a reset itself, not only our Discontinuity Flush.
- **`vHandle`**: One registry lookup resolves the Virtual NGX Handle; the bridge read above is reused for the bypass and eye-index decisions instead of re-taking the interceptor lock.
- **`eyeStride`**: Eye-stride compensation only applies when consecutive evaluations of a context are two frames apart: AFR alternating a virtual handle's two eye contexts during stereo presentation, which starts at the Companion Mod handshake. SDP per-eye contexts use their own diagnostic multiplier (1.0x unless set). The monoscopic single-context bypass, a handle that was not split per eye, and pre-handshake (quad layer) frames are 1.0x.
- **`afrFallbackMulX`**: Under AFR the eye-stride multiplier (vrDlssMvScale) is only the fallback for when the motion vector pass is off or has disabled itself. While the pass is available, the correction handles the stride itself (it scales the object-motion component only), so the multiplier is held back here and applied below only if the pass did not take over this evaluation.
- **`scale`**: An absent MV scale means 1.0 to DLSS, which is also what we restore it to.
- **`prevMv`**: The motion vector pass: DLSS reads the Injector's texture for this one evaluation, then the game's motion vectors are put back. Monoscopic presentation (menus, 2D Floating Window Mode) and handles that are not split per eye keep the engine's motion vectors.
- **`effScaleX`**: Per-eye correction: re-aim the engine's vectors from the previous rendered frame's camera at the same eye's previous camera. Frames without a usable plan (missing record, a zero MV scale) get the engine's vectors back unchanged.
- **`nearOverride`**: Near plane: a manual override wins, then the automatic estimate, then the camera record's.
- **`correctionRequest.objectMotionFactor = eyeStride ? mvreproj::kAfrEyeStride : ...`**: AFR: the simulation advances every frame, so only the engine's object motion needs the eye stride (the same eye's history is two frames back).
- **`correctionRequest.gateNonStatic = true;`**: Camera-attached geometry (the cab) and moving objects keep the engine's vector.
- **`checkRequest`**: Self-check (vrDlssDiag window only): predict the engine's vectors from depth and the camera records and score them. Measured only; the pass output stays a copy of the engine's vectors.
- **`nearRequest`**: Automatic near plane estimate, running whenever the correction does (not only in diagnostics windows): every Nth cross-eye evaluation records a sparse self-check against the previous rendered frame's camera on the engine's original vectors; it is read back a few evaluations later like any self-check. Missing records are skipped.
- **`if (nearOverride <= 0.0f && resolved.current && resolved.previousFrame &&`**: One check per evaluation: on its every Nth evaluation the estimate takes the slot, also in a diagnostics window (which arms itself at start up and on mode changes).
- **`if (eyeStride && mvFixWillRun && !swappedMv && mutableParams) {`**: AFR with the pass expected to run but not taking over (it failed on this evaluation, or the engine's depth or vectors were unavailable): fall back to eye-stride scaling.
- **`if (wroteScale) {`**: Always undo our write so a reused parameter block never compounds the multiplier
- **`NgxInterceptor::Intercept_LoadLibraryW`, `orig`**: No trampoline means the hook is already gone, so the plain call cannot re-enter this detour.

### `src/ngx/ngx_interceptor.h`

- **`NgxInterceptor`, `VRBridge`**: Dependencies are wired by InjectorRuntime (or by a test). The interceptor never falls back to a global singleton: an unset dependency reads as null and its feature degrades gracefully. VRBridge dependency for mode, menu and discontinuity state
- **`NgxInterceptor`, `FidelityFxInterceptor`**: The FidelityFX interceptor shares this interceptor's LoadLibrary hooks: a load of the FidelityFX loader is forwarded to it, and CheckAndAttachLoadedModule also checks for an already loaded one.
- **`NgxInterceptor`, `GetOriginalCreateFeature`**: Getters for original function pointers. They read the detour's trampoline, which FunctionDetour publishes before the patch goes live, so a call racing the install already sees it.
- **`NgxInterceptor`, `kDefaultAfrMvScale`**: Eye-Stride Motion Vector compensation under AFR (ADR-0015). Overridable via SetMvScaleMultiplier; SDP always evaluates at 1.0x.
- **`NgxInterceptor`, `GetSdpMvScaleMultiplierX`**: Diagnostic multiplier for per-eye contexts under SDP (1.0 by default, independent of the AFR one).
- **`NgxInterceptor`, `SetMvFixEnabled`**: Motion vector pass (vrDlssMvFix). While on, a compute pass recorded on the game's command list ahead of each per-eye DLSS evaluation produces the motion vectors DLSS receives for that one evaluation. On by default. A pass failure turns it off for the session (the engine's motion vectors are then used) and "on" no longer takes effect. Returns the resulting enabled state, so a request to turn it on after a failure reads false.
- **`NgxInterceptor`, `SetDepthModelInfinite`**: Depth model the correction and the self-check read the engine's depth with (vrDlssDepthModel). Infinite (the default): reverse-Z with an infinite far plane, view depth = near / depth, the model the engine's FSR3 path declares (FFX_UPSCALE_ENABLE_DEPTH_INFINITE) and the same renderer feeds DLSS. Finite: reverse-Z between the camera record's near and far planes. For A/B comparison.
- **`NgxInterceptor`, `SetNearClipOverride`**: Near plane in metres for the correction and the self-check, on every camera. The Companion Mod sets it per camera mode (vrDlssNearClipCab / Outside / OnFoot) as the player enters, leaves or switches cameras. m > 0 is a manual override and always wins. 0 (the default, also written `auto`) estimates the renderer's near from the engine's own motion vectors (see mvreproj::SolveNear): the Companion Mod's camera record reports the same near for every camera while the renderer's differs (about 0.1 in a vehicle cab, 0.15 on foot). Until the first estimate, and after a Discontinuity Flush, the record's near is used. Negative or non-finite values count as 0.
- **`NgxInterceptor`, `kDefaultDiagEvaluations`**: Evaluation diagnostics: while armed, every EvaluateFeature logs a trace line and feeds a per-context jitter summary, logged when the countdown reaches zero. Armed automatically when a feature is virtualized or the Stereo Presentation Mode changes, and on demand (vrDlssDiag).
- **`NgxInterceptor`, `FeatureHost`**: The interceptor whose detours are live, or null when none is. Published before the detours are installed and cleared after they are removed, so a live detour always finds its host.
- **`NgxInterceptor`, `RecordMvPass`**: Records the pass for this evaluation. Returns the texture to hand DLSS, or null to keep the engine's motion vectors (also after a failure, which disables the pass for the session). When request is set the pass also records a self-check; reports, when set, receives the self-checks that finished reading back since the previous call.

### `tests/test_dlss_evaluation.cpp`

- **`SetUp`, `m_bridge.SetCompanionActive(true);`**: Stereo presentation (and with it AFR eye alternation) starts at the Companion Mod handshake.
- **`DlssEvaluationTest.DeriveDlssRenderResolutionScalesAcrossAllQualityModes`, `fallback`**: Unknown quality value: gracefully defaults to 1.0f (native/DLAA, no downscale artifacts)
- **`DlssEvaluationTest.TargetResolutionPrefersSwapchainRenderResolutionOverOpenXrRecommendedView`, `decoupledW`**: Target render resolution (1920x1080) MUST take precedence over supersampled HMD view (4940x4136)
- **`DlssEvaluationTest.SingleContextBypassEvaluatesAt1xUnderAfr`, `bridge.Set2DViewActive(true);`**: 2D Floating Window and GUI menu frames are monoscopic and one frame apart
- **`DlssEvaluationTest.AfrEvaluatesAt1xBeforeCompanionHandshake`, `bridge.SetCompanionActive(false);`**: Main menu before the handshake: presented as a quad layer, so parity never advances and every evaluation of the context is one frame apart, even with AFR selected.
- **`DlssEvaluationTest.AfrEvaluatesAt1xForHandleNotSplitPerEye`, `plainHandle`**: A handle the interceptor did not virtualize is a single context evaluated every frame.
- **`MvCorrectionEvaluationTest.TheEngineDepthIsReadAsInfiniteReverseZByDefault`, `EXPECT_TRUE(c.current.infiniteDepth);`**: Only the camera that rendered the depth buffer reads it.
- **`AutoNearTest.EveryFourthCrossEyeEvaluationRecordsAnEstimateOutsideADiagnosticsWindow`, `EXPECT_NEAR(r.toPrevious.c[0], -0.064f, 1e-5f);`**: It targets the previous rendered frame's camera (the right eye, 0.064 m aside), not the same eye's.
- **`AutoNearTest.ADiscontinuityFlushForgetsTheEstimateAndReadbacksStillInFlight`, `for (size_t i = 0; i < NearEstimator::kMinSolutions; ++i) m_pass->pendingRepo...`**: A solution recorded before the flush and read back after it describes the old scene.

### `tests/test_fidelityfx_interceptor.cpp`

- **`FfxMvConventionsTest.InfiniteDepthTakesTheNearPlaneWhicheverFieldCarriesIt`, `desc.cameraNear = 0.1f;`**: The far field is meaningless with an infinite far plane: FLT_MAX, zero, or the near plane swapped in.
- **`FfxMvConventionsTest.ANegativeXMotionVectorScaleIsReadWithItsSign`, `desc.motionVectorScale = {-3004.0f, 1690.0f};`**: -width/2, +height/2 of a 6008x3380 render, as seen in game
- **`SetUpSdpWithRecords`, `m_bridge.SetRenderingMode(mode);`**: switching mode drops camera records, so it comes first
- **`FfxMvCorrectionTest.InfiniteDepthContextsAreCorrectedAndTheRequestSaysSo`, `context`**: The game's FSR3 flags: HDR | DepthInverted | DepthInfinite (0x19), with a negative x scale.

### `tests/test_mv_reprojection.cpp`

- **`namespace {`**: Static-consistency gating (vrDlssMvFix, NGX)

  The correction assumes the pixel is static world. A pixel that moves with the camera (cab interior) or on its own (another vehicle) has an engine vector that disagrees with the static prediction toward the previous rendered frame: |e - d| > max(kStaticGateAbsPx, kStaticGateRelative \* |d|). Those pass through.

- **`namespace {`**: AFR Under AFR the previous frame is the other eye and the same eye's previous frame is two frames back. The simulation advances every frame, so the engine's object motion covers one frame where the same-eye history is two frames back: object motion takes the eye-stride factor, camera motion does not.
- **`namespace {`**: Automatic near plane estimate (vrDlssNearClip auto)

  For static geometry the engine vector is e = R + T / near_true: R the rotation-only displacement, T the translation part of the prediction computed with an assumed near n0. s = sum((e-R).T) / sum(T.T) and near_true = n0 / s. The expected values below are worked out by hand.

- **`NearEstimateTest.TheRotationIsSubtractedBeforeFitting`, `samples`**: A large rotation on top of the translation must not bias s: with R left in, s would be far from 1.5.

### `tests/test_near_estimator.cpp`

- **`NearEstimatorTest.TheSpikesSeenWhileDrivingDoNotMoveTheEstimate`, `e`**: The log of the vehicle driving: 0.107, 0.174, 0.401, 0.334, 0.167, 0.221, 0.108, 0.100, 0.434, 0.269.

### `tests/test_ngx_interceptor.cpp`

- **`NgxInterceptorTest.SecondInterceptorCannotTakeOverLiveModuleLoadHooks`, `other`**: Taking over the host would leave the live LoadLibrary detours without a trampoline, and they would recurse into the hooked LoadLibrary until the stack overflowed.

### `tests/test_virtual_ngx_handle.cpp`

- **`VirtualNgxHandleTest.MatchesNgxNameFiltering`, `EXPECT_FALSE(NgxInterceptor::MatchesNgxName(L"nvngx_dlss.dll"));`**: Child plugin DLLs MUST BE REJECTED to prevent hook hijacking and mismatched handle crashes
- **`VirtualNgxHandleTest.ParameterDetoursAndNativeMirroringOnFeatureCreation`, `virtualHandle`**: When creating a feature, right handle creation allocates native parameters, mirrors to it, and destroys it

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 110 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
