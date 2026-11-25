# Decisions behind the OpenXR Session, Eye Images and Input

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0004](0004-graceful-openxr-pass-through-fallback.md), [ADR-0006](0006-openxr-floating-quad-layer-for-menus.md), [ADR-0009](0009-openxr-alpha-blended-mouse-cursor-layer.md), [ADR-0011](0011-openxr-reference-space-recentering.md), [ADR-0019](0019-begin-the-openxr-frame-before-the-engine-update.md), [ADR-0025](0025-motion-controller-input-reaches-lua-as-raw-per-hand-state.md).

## Context and Decision
This record holds the non-obvious decisions behind the OpenXR session and its lifecycle, the eye copies, the menu quad, the cursor overlay and Motion Controller input. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `src/openxr/d3d12_eye_copy.cpp`

- **`D3D12EyeCopy::EnsureTimestamps`, `LogWarn("OpenXrManager: GPU timestamps unavailable; vrPerfTrace logs no GPU e...`**: The trace still has its CPU timings; only the GPU eye copy lines are missing.
- **`D3D12EyeCopy::ReportTimestamps`, `if (m_copiesSinceCalibration++ % 64 == 0) {`**: The GPU and CPU clocks drift apart slowly; recalibrating every few dozen copies keeps the lag exact.
- **`D3D12EyeCopy::Copy`, `if (SUCCEEDED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ...`**: A new list is created open. Close it so every use starts from a closed list that can be reset.
- **`D3D12EyeCopy::Copy`, `if (!(curCommandList && curAllocator && destTexture)) {`**: A copy with no destination texture records nothing: the slot's list stays closed, nothing is executed and its fence value is not advanced.
- **`D3D12EyeCopy::Copy`, `ReportTimestamps(ringSlot);`**: The slot's earlier copy has run, so its timestamps are written.
- **`D3D12EyeCopy::Copy`, `curAllocator->Reset();`**: The slot's fence has passed, so the allocator is idle; its list is closed, so it can be reset.
- **`D3D12EyeCopy::Copy`, `barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;`**: OpenXR D3D12: acquired color swapchain images are in RENDER_TARGET and must be returned to RENDER_TARGET before release.
- **`D3D12EyeCopy::ClearToBlack`, `barrier`**: The game hands the back buffer to Present in the PRESENT state (as Copy assumes); it goes back to that state after the clear.

### `src/openxr/d3d12_eye_copy.h`

- **`D3D12EyeCopy`**: The Direct3D 12 half of the eye copy (spec #247): copies the game's back buffer into a destination texture and clears the back buffer to black, on the game's own queue. It knows nothing about OpenXR: the OpenXR manager acquires, waits on and releases the swapchain image and hands this module the image's texture in between.

  It owns the copy ring (command allocators and lists, one pair per slot), the fence and its event, the GPU timestamps for `vrPerfTrace` and the clear-to-black resources. Single-threaded, like the Present path that calls it (ADR-0018).

- **`D3D12EyeCopy`, `m_timestampHeap`**: `vrPerfTrace`: GPU timestamps before and after each eye copy, two per ring slot, read back when the slot is reused (its fence has passed by then). Created on the first copy a trace times.

### `src/openxr/menu_quad.cpp`

- **`MenuQuad::PoseFromHeadPose`, `pose.position.y = headPose.position.y;`**: Decouple pitch/roll: virtual screen remains at eye level
- **`MenuQuad::AlignToHeadPose`, `m_awaitsHead = !head.isValid;`**: The first menu frame of a session usually has no head pose yet (the frame is not rendered, or the runtime is not tracking): the quad then sits at the local space origin, which can be far below the eyes, until the head is tracked.
- **`MenuQuad::RayHitAlong`, `kMinApproach`**: The panel is only visible from its front: the ray must start in front of it and head into it.

### `src/openxr/menu_quad.h`

- **`MenuQuad`**: The menu quad's geometry (spec #247): where the panel for menus and 2D Floating Window Mode sits, how big it is, how far in front of the head it is placed (Menu Distance), and where a ray lands on it (the Pointer Ray's panel hit). Plain maths on poses and vectors: it holds no OpenXR handle, and the OpenXR manager keeps the spaces and the layer submission and asks it for the pose and size to submit. Single-threaded, like its caller (ADR-0018).
- **`MenuQuad`, `SetDistance`**: Sets the Menu Distance. A quad already placed moves straight back or forward along the way it faces, so it stays in front of the head that placed it.
- **`MenuQuad`, `AlignToHeadPose`**: Places the quad `Distance()` in front of `head`, at eye level along the head's yaw. The head must be located in the space the quad is submitted in. A head that is not valid (the first menu frame of a session often has none) leaves the quad waiting for one: see AwaitsHead.
- **`MenuQuad`, `AwaitHeadAfter`**: The quad moves to the head on the first rendered frame with a valid head pose predicted at or after the time given to AwaitHeadAfter: it was placed from no pose, the runtime recentered the local space under it, or the running session was focused for the first time. AlignToHeadPose settles it (or waits again, without a pose).

### `src/openxr/openxr_cursor_overlay.cpp`

- **`OpenXrCursorOverlay::ComputeCursorQuadPose`, `Vector3 localOffset(`**: Hotspot offset: tip of cursor is top-left corner of cursor quad Center of cursor quad is offset by (+cursorWidth/2, -cursorHeight/2) Forward offset of 2mm (+Z in local quad space, towards player) eliminates z-fighting
- **`OpenXrCursorOverlay::UploadTexture`, `barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;`**: OpenXR D3D12: acquired color swapchain images are in RENDER_TARGET and must be returned to RENDER_TARGET before release.

### `src/openxr/openxr_dispatch.h`

- **`IOpenXrDispatch`, `virtual XrResult PollEvent(`**: Dequeues the next runtime event, or returns XR_EVENT_UNAVAILABLE when the queue is empty. Defaults to an empty queue so dispatches that never report events need no override.
- **`IOpenXrDispatch`, `virtual XrResult ConvertWin32PerformanceCounterToTime(`**: xrConvertWin32PerformanceCounterToTimeKHR (XR_KHR_win32_convert_performance_counter_time). Defaults to unsupported so dispatches that do not offer the extension need no override.
- **`IOpenXrDispatch`, `virtual XrResult StringToPath(`**: Input (ADR-0025): the Motion Controller action set. Every call defaults to XR_ERROR_FUNCTION_UNSUPPORTED so a dispatch that offers no input behaves like a runtime that refuses the action set: the Injector then reports both hands untracked and carries on with head tracking only.
- **`NativeOpenXrDispatch`, `lock`**: A later instance may reuse the handle value; resolve the conversion function again.

### `src/openxr/openxr_input.cpp`

- **`OpenXrInput::FailCreate`, `attempted`**: Keeps m_attempted: a refused session is not asked again.
- **`OpenXrInput::CreateGripPoseAction`**: The grip pose is optional: a runtime that refuses it leaves the aim pose and every other input working, and the grip pose then reports untracked (the Virtual Hand has nothing to be drawn at).
- **`OpenXrInput::SuggestBindings`, `lastFailure = res;`**: A runtime that does not know a controller profile says so; the others still bind.
- **`OpenXrInput::Create`, `m_gripSpaces[hand] = XR_NULL_HANDLE;`**: Optional like the action itself: this hand's grip pose reports untracked.
- **`OpenXrInput::Sync`, `ResetHands();`**: Another application has the input focus: no input is delivered, which is not an error.
- **`OpenXrInput::LocatePoses`, `state.gripTracked = LocatePoseSpace(dispatch, m_gripSpaces[hand], m_gripPoseA...`**: The grip pose is located whether or not the aim pose is: each reports its own tracked flag.

### `src/openxr/openxr_input.h`

- **`OpenXrInput`**: Motion Controller input (ADR-0025): one OpenXR action set of raw per-hand inputs, declared and attached once per session, synced once per frame, and read back as HandState values. The meaning of a button lives in the Companion Mod; this class only reports raw inputs.

  Any refused step (a runtime that does not support input, a profile it does not know, a failed attach) is logged once; both hands then report untracked and the session carries on unchanged.

- **`OpenXrInput`, `Create`**: Declares the action set and its actions, suggests bindings for the Oculus Touch, Valve Index, HTC Vive and Khronos simple controller profiles, creates one aim action space per hand and attaches the set to the session. Attempted once per session: later calls return the first outcome. Returns whether Motion Controllers are usable.
- **`OpenXrInput`, `Sync`**: xrSyncActions plus a read of every action. A session that is not FOCUSED reports no input (hands untracked); that is not an error.
- **`OpenXrInput`, `PulseHaptic`**: Plays a vibration on one hand's haptic output: `durationNs` nanoseconds at `amplitude` (0..1) and the runtime's default frequency. False when the set is not ready, the hand does not exist or the runtime fails the call (a runtime that is not focused may; that is not an error here).
- **`OpenXrInput`, `m_gripPoseAction`**: stays null when the runtime refuses it: grip pose untracked

### `src/openxr/openxr_manager.cpp`

- **`OpenXrManager::GetCurrentXrTime`, `if (m_runtimeInfo.win32TimeConversionEnabled && m_instance != XR_NULL_HANDLE) {`**: Before the first frame: let the runtime convert the counter into its own time base.
- **`OpenXrManager::GetCurrentXrTime`, `return QpcTicksToNanoseconds(now.counter, now.frequency);`**: No runtime conversion: the counter in nanoseconds, computed without overflow. Runtimes that back XrTime with the performance counter share this time base, and the counter is monotonic.
- **`OpenXrManager::~OpenXrManager`, `if (IsProcessTerminating()) return;`**: At termination the runtime DLL may already be detached and a GPU fence wait could hang: leave the session and instance to the OS (see IsProcessTerminating).
- **`OpenXrManager::CreateSession`, `m_session = XR_NULL_HANDLE;`**: Final, like a lost session (ADR-0004): the session is attempted once the game's device is known, and a runtime that refuses that device refuses it again. The swap chain hooks see Pass-Through and change nothing.
- **`OpenXrManager::CreateSession`, `m_input.Create(*m_dispatch, m_instance, m_session);`**: Motion Controllers (ADR-0025): declared and attached once per session. A runtime that refuses any step leaves both hands untracked; it never fails the session.
- **`OpenXrManager::PollEvents`, `for (int i = 0; i < 64; ++i) {`**: Bounded drain so a misbehaving runtime can never stall the present path.
- **`OpenXrManager::PollEvents`, `change`**: The runtime's own recenter (holding the Meta button, SteamVR's Reset Seated Position). Before a savegame loads it is the only recenter there is, since the Recenter Action comes with the Companion Mod. Poses respect it from changeTime on.
- **`OpenXrManager::PollEvents`, `if (m_sessionState == XR_SESSION_STATE_READY && !m_sessionRunning) {`**: READY is delivered only once. If xrBeginSession failed then, keep retrying until it succeeds or the runtime moves the session on.
- **`OpenXrManager::EnterPassThroughIfLost`, `if (res == XR_ERROR_SESSION_LOST) {`**: A crashed or restarted runtime may fail frame calls with \*_LOST without ever queueing a LOSS_PENDING event; treat it the same way.
- **`OpenXrManager::HandleSessionStateChanged`, `if (!m_focusedSinceBegin) {`**: The headset tracks before it is worn, so the first menu can be placed from wherever it rests. The first focus of a running session is when the player has it on.
- **`OpenXrManager::EndRunningSession`, `m_frameBegun = false;`**: Any frame still in flight is abandoned; the runtime discards it when the session ends.
- **`OpenXrManager::ReleaseAndEnterPassThrough`, `m_sessionRunning = false;`**: Keeps Shutdown from calling xrEndSession, which is valid only once the runtime has moved the session to STOPPING; xrDestroySession is valid in any state and ends it.
- **`OpenXrManager::GetHandState`, `if (m_handPosesStale) {`**: Inside a frame every read sees the snapshot taken at frame begin; only a Recenter moves the reference space under it.
- **`OpenXrManager::GetHandState`, `RefreshHandState(GetCurrentXrTime(), /*resync=*/true);`**: No frame snapshot (before the first frame, or between frames): sync now and locate at the same time PollHeadPose uses.
- **`OpenXrManager::Recenter`, `m_handPosesStale = true;`**: the hands are located against the local space that was just re-created
- **`OpenXrManager::SetFovOverscan`, `m_requestedFovOverscan.store(ClampFovOverscan(percent));`**: The bridge clamps too, this keeps the manager safe on its own.
- **`OpenXrManager::ComputeProjectionFromFov`, `aspect`**: The rendered FOV (ADR-0005): symmetric, and wide enough to cover every edge of the headset FOV. Its vertical half-angle tangent is the largest of the up tangent, the down tangent and the wider horizontal tangent divided by the aspect of the image it is rendered into (the eye swapchain; the headset FOV's own aspect before the swapchains exist). The horizontal half-angle follows from the aspect. FOV Overscan multiplies that tangent, so both axes widen by the same factor and the pixels stay square.
- **`OpenXrManager::LocateActiveViews`, `if (!m_frameBegun) {`**: With no frame in flight there is no pair to keep on its FOV, so a new overscan shows at once.
- **`OpenXrManager::LatchFovOverscan`, `m_lastLocatedViewsTime = 0;`**: the views already located were computed with the old overscan
- **`OpenXrManager::SelectCompatibleSwapchainFormat`, `candidates`**: Candidates in preference order. An 8-bit back buffer holds sRGB-encoded colour even when its format is UNORM (flip-model swap chains can't be \_SRGB), and runtimes treat a UNORM swapchain as linear and encode it again, which washes the headset image out. So the sRGB member of the family comes first and UNORM is only a fallback; other formats must match exactly.
- **`OpenXrManager::NegotiateEyeSwapchainFormat`, `supportedFormats`**: The eye image is filled by a straight copy from the back buffer, so the negotiated format must be copy-compatible with it. Never fall back to an incompatible format.
- **`OpenXrManager::NegotiateEyeSwapchainFormat`, `LogWarn("OpenXrManager: Could not enumerate swapchain formats (result " + std...`**: A failed query says nothing about compatibility; let swapchain creation decide.
- **`OpenXrManager::EnterPassThroughForIncompatibleFormat`, `ReleaseAndEnterPassThrough("no copy-compatible swapchain format");`**: Pass-Through Mode means no OpenXR session, so destroy the session and release everything rather than leave a begun session with no frames ever submitted.
- **`OpenXrManager::ClearRenderPoses`, `m_hasRenderPose[0] = false;`**: A record not consumed by its frame's copy (shouldRender false, a skipped copy, a recenter) belongs to an older frame or tracking space; never let a later frame submit it.
- **`OpenXrManager::BeginFrame`, `LatchFovOverscan();`**: A FOV Overscan set meanwhile takes effect with this frame; the views located below use it for the whole pair.
- **`OpenXrManager::BeginFrame`, `if (m_frameState.shouldRender) {`**: OpenXR specification: actively locate views and update projections and head pose for predictedDisplayTime within the active frame lifecycle boundary.
- **`OpenXrManager::BeginFrame`, `observers`**: The list this frame began with: an observer may add or clear observers (or another thread may), which shows from the next frame on. Holding a reference copies nothing.
- **`OpenXrManager::CopyBackBufferToEye`, `if (!m_frameBegun) {`**: Enforce active frame lifecycle: swapchain image acquisition and release must occur strictly within active frame begin and frame end boundaries.
- **`OpenXrManager::CopyBackBufferToEye`, `if (!m_frameState.shouldRender) {`**: OpenXR specification: suppress swapchain image acquisition and rendering passes when runtime indicates shouldRender is false.
- **`OpenXrManager::CopyBackBufferToEye`, `if (m_hasRenderPose[eyeIndex]) {`**: Submit the eye with the pose the engine rendered it with (recorded when VRBridge handed it to the Companion Mod). Fall back to the pose polled at frame begin if none was recorded.
- **`res`, `swapchainTiming`**: Acquire and wait together: the image the compositor still holds can block either.
- **`OpenXrManager::SubmitFramePair`, `halfFovY`**: The submitted FOV is the rendered FOV the eye projection publishes, which is also the vertical FOV the Companion Mod gives the engine camera: the engine renders with that vertical FOV and scales the horizontal one by the backbuffer aspect (W / H), so the submitted frustum matches the rendered one exactly and keeps square pixels (ADR-0005).
- **`OpenXrManager::AlignMenuQuadToHeadPose`, `m_menuQuad.AlignToHeadPose(customPose ? *customPose : m_lastPose);`**: The head as located in m_localSpace, the space SubmitMenuQuad() submits the quad in. The raw pose is in the runtime's own local space, which after a Recenter Action is offset by wherever the head was in it: about 24 m on Pimax OpenXR, enough to put the menu out of sight (issue 238).
- **`OpenXrManager::GetPanelHit`, `return MenuQuad::RayHit(m_menuQuad.Pose(), ComputeMenuQuadSize(), state.aim.p...`**: The hand is located in the local space the quad layer is submitted in, so the pose and size SubmitMenuQuad() uses are the ones to test against.

### `src/openxr/openxr_manager.h`

- **`OpenXrManager`, `HasMotionControllerActions`**: Motion Controllers (ADR-0025) True when the Motion Controller action set was declared and attached for this session. False when the runtime refused any step (the session and head tracking are unaffected).
- **`OpenXrManager`, `PulseHaptic`**: A short vibration on one hand (0 = left, 1 = right): `durationSeconds` long at `amplitude` (clamped to 0..1) at the runtime's default frequency. False, and nothing played, for a hand that does not exist, a duration of zero or less, an unavailable action set, or a runtime that fails the call; never an error for the session.
- **`OpenXrManager`, `GetCurrentXrTime`**: "Now" in the runtime's time base. Extrapolates from the last predicted display time once frames run; before the first frame converts the performance counter through the runtime (XR_KHR_win32_convert_performance_counter_time) or, without it, converts the counter to nanoseconds directly.
- **`OpenXrManager`, `using FrameBeginObserver = std::function<void()>;`**: `observer` runs at the end of every BeginFrame that began a frame, after the hand state is synced. One observer per `owner` (the Virtual Gamepad publishes its snapshot and plays its rumble from here); clearing is by the same owner. Safe from any thread, and from inside an observer: a frame calls the observers it began with.
- **`OpenXrManager`, `ComputeProjectionFromFov`**: `fovY`/`fovX` of the result are the rendered FOV (ADR-0005): symmetric, covering every edge of `fov`, at the aspect of the eye swapchain (`swapchainAspect`; 0 uses the headset FOV's own aspect). The tangents, angles and offsets keep describing the headset's own FOV. `fovOverscanPercent` widens the rendered FOV's tangents by (1 + percent / 100) (FOV Overscan).
- **`OpenXrManager`, `SetFovOverscan`**: FOV Overscan, in percent (0 to 20; clamped). A new value is stored here and takes effect when the next frame begins, so the pair in progress keeps the FOV it began with.
- **`OpenXrManager`, `SetFovOverscanSuspended`**: The overscan only touches the stereoscopic view: while a menu or 2D Floating Window Mode is shown the eyes are rendered without it (it shows again, with the next frame, when the stereoscopic view is back).
- **`OpenXrManager`, `SelectCompatibleSwapchainFormat`**: Picks the supported swapchain format that a plain copy from a back buffer of `backBufferFormat` can target (same format family; UNORM/sRGB variants allowed). For 8-bit families the sRGB variant is preferred over UNORM, whatever the back buffer's variant. Returns DXGI_FORMAT_UNKNOWN when none is compatible.
- **`OpenXrManager`, `EnsureBackBufferFormatSupported`**: Called when the game creates its swap chain, before any DXGI overrides are applied. Releases OpenXR and enters Pass-Through Mode if no eye-swapchain format is copy-compatible with the back buffer. Returns whether VR is still active.
- **`OpenXrManager`, `ClearBackBufferToBlack`**: Clears the game's back buffer to black on the game's queue and leaves it in the PRESENT state the game expects, so the next Present puts a black frame on the Desktop Mirror Window. Call it after the frame's eye copy, which has already taken what the headset needs. Returns false and does nothing when there is no back buffer, device or queue (unit tests, Pass-Through).
- **`OpenXrManager`, `kDefaultMenuQuadDistance`**: Menu Distance: how far in front of the head the menu quad (menus and 2D Floating Window Mode) is placed, in metres. The quad keeps its size, so further away it fills less of the view. A quad already placed moves straight back or forward along the way it faces, so it stays in front of the head that placed it. Clamped to kMinMenuQuadDistance.. kMaxMenuQuadDistance; NaN counts as the default. The setting outlives Shutdown, like the FOV Overscan.
- **`OpenXrManager`, `GetPanelHit`**: Where the ray of `hand` (0 = left, 1 = right) lands on the panel the next SubmitMenuQuad() submits, in normalized panel coordinates (see PanelHit). The hand's aim pose and the layer are both in the Recenter-aware local space, so the test holds across a Recenter. No hit for an untracked hand. Whether a panel is visible is the caller's (VRBridge's) knowledge.
- **`OpenXrManager`, `GetPanelHitAlongRay`**: The same for a ray the caller gives, in the space the hand poses are in (the Pointer Ray starts at the index fingertip and runs along the finger, which only the Companion Mod knows). `direction` need not be a unit vector. No hit for an untracked `hand`, or a direction without a length.
- **`OpenXrManager`, `SetPointerCursorOverride`**: While active the drawn cursor sits at (u, v) in PanelHit coordinates (clamped onto the panel) and MirrorWindowController ignores the Win32 cursor: the Pointer Ray has control of the cursor. Inactive means the desk mouse has it.
- **`OpenXrManager`, `IsSessionFocused`**: The headset is on and the game has the focus. A sleeping headset drops to SYNCHRONIZED and the dashboard to VISIBLE; neither ends the running session.
- **`OpenXrManager`, `PollEvents`**: Drains the runtime event queue and drives the session lifecycle: begin on READY, end on STOPPING, release resources and enter Pass-Through Mode on EXITING, LOSS_PENDING or instance loss. BeginFrame polls too; a caller that must see the lifecycle settled before it reaches BeginFrame (DxgiProxy::OnPresent) calls this first. A second poll in the same frame finds an empty queue.
- **`OpenXrManager`, `RecordRenderPose`**: Records the head pose handed to the Companion Mod for this eye's render. The next CopyBackBufferToEye for that eye uses it (instead of the pose polled at frame begin) as the pose the eye is submitted with. Consumed by the copy.
- **`OpenXrManager`, `m_frameSerial`**: The frame whose hand snapshot (sync + poses) is held; valid only while that frame is begun.
- **`OpenXrManager`, `m_lastLoggedIpd`**: One-time log guards. Per instance and cleared by Shutdown, so each OpenXR session logs these again.
- **`OpenXrManager`, `using FrameBeginObserverList = std::vector<std::pair<const void*, FrameBeginO...`**: Copy-on-write: an edit builds a new list under the mutex, a frame takes a reference to the current one (no copy, no allocation) and walks it without the lock.
- **`OpenXrManager`, `m_requestedFovOverscan`**: The overscan last set, and the one the located projections are rendered with: it is taken over from the requested one when a frame begins (or while no frame is in flight).

### `src/openxr/openxr_types.h`

- **`HandState`**: One hand's Motion Controller state as the Companion Mod sees it (VRBridge.getHandState). Hand ids: 0 = left, 1 = right. `aim` is the aim pose in the head pose's convention: the same reference space (the Recenter-aware local space) and the same predicted display time as the head pose. `gripPose` is the grip pose (where a Virtual Hand is drawn) in the same convention, with its own `gripTracked` flag: it is located separately from the aim pose, so either can be untracked while the other is tracked (for example when the runtime refuses the grip pose, or can not locate it). When no input is delivered at all (no input focus, a failed sync) the hand reports tracked = false, a default aim pose, gripTracked = false, a default grip pose and zeroed inputs.
- **`PanelHit`**: Where a Motion Controller's ray lands on the floating menu / 2D Floating Window Mode panel. u runs left to right and v top to bottom, both 0..1 (the drawn cursor's convention, which is not the game's GUI convention: the GUI's y runs bottom to top). A miss has hit = false and zeros.
- **`PointerCursorOverride`**: The drawn menu cursor placed by the Pointer Ray instead of the desk mouse (ADR-0009). While active, the cursor layer is drawn at (u, v) in PanelHit coordinates and the Win32 cursor is ignored.

### `tests/test_openxr_eye_copy_d3d12.cpp`

- **`FakeD3D12OpenXrRuntime`**: The fake OpenXR runtime with eye swapchain images that are real textures on a D3D12 device, in the state the OpenXR D3D12 extension hands them out in (RENDER_TARGET), so the eye copy runs against real resource and command list rules.
- **`EnumerateSwapchainImages`, `d3d12Images`**: The caller's array holds XrSwapchainImageD3D12KHR elements, so step by that size, not the base header's.
- **`WarpDebugDevice`**: A WARP device with the D3D12 debug layer on, a direct queue and a back buffer in the PRESENT state, as the game hands it to Present. The debug layer is process-wide for devices created after it is enabled, so every test here makes its own device and nothing else in the suite depends on the layer being off.
- **`mismatches`, `eyeCopy.Release();`**: The module has no destructor that waits: its owner calls Release (the OpenXR manager does, on shutdown) before it goes away. Destroying it with the copy and clear above still in flight would free their command allocators under the GPU, which the debug layer reports.

### `tests/test_openxr_manager.cpp`

- **`OpenXrManagerTest.UnormBackBufferGetsSrgbEyeSwapchainEvenWhenUnormIsOffered`, `mockDispatch`**: The game's UNORM back buffer holds sRGB-encoded colour; a UNORM eye swapchain would make the runtime encode it again and wash the headset image out.
- **`OpenXrManagerTest.StaysInPassThroughWhenNoCompatibleSwapchainFormat`, `EXPECT_FALSE(manager.HasSession());`**: Pass-Through Mode holds no OpenXR session. (xrEndSession is not called: it is only valid once the runtime has moved the session to STOPPING; xrDestroySession is valid in any state.)
- **`OpenXrManagerTest.DestructorAfterTerminationDetachLeavesRuntimeAlone`, `EXPECT_FALSE(mockDispatch->destroySessionCalled);`**: The runtime DLL may already be gone at termination: no session or instance teardown calls.
- **`OpenXrManagerTest.SelectCompatibleSwapchainFormatRules`, `EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM, all)...`**: 8-bit: the sRGB variant wins over an exact UNORM match, so the runtime doesn't encode the already sRGB-encoded back buffer a second time
- **`OpenXrManagerTest.SelectCompatibleSwapchainFormatRules`, `EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R8G8B8A8_UNORM, { DX...`**: UNORM is still taken when the runtime offers no sRGB variant
- **`OpenXrManagerTest.SelectCompatibleSwapchainFormatRules`, `EXPECT_EQ(M::SelectCompatibleSwapchainFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, ...`**: 10-bit/FP16 back buffers only match themselves
- **`OpenXrManagerTest.SuppressesSwapchainAcquisitionWhenShouldRenderIsFalse`, `EXPECT_TRUE(manager.SubmitFramePair());`**: SubmitFramePair succeeds and submits with layerCount = 0 per OpenXR specification
- **`MenuQuadSession`**: A session whose local space origin sits well below the player's eyes, as SteamVR's seated origin can: a menu quad placed at the origin shows at knee level.
- **`OpenXrManagerTest.MenuQuadPlacedWhileTheRuntimeSkipsRenderingMovesToTheHeadOnceItRenders`, `s.dispatch->mockShouldRender = XR_FALSE;`**: The session has just begun and is not visible yet: the frame is not rendered, so the head is never located and the panel is placed from no pose at all.
- **`OpenXrManagerTest.RuntimeRecenterOfTheLocalSpaceMovesMenuQuadToTheHead`, `s.dispatch->events.PushReferenceSpaceChangePending(XR_REFERENCE_SPACE_TYPE_LO...`**: The player holds the headset's recenter button: the runtime re-bases the local space, so the head reads somewhere new in it from then on.
- **`OpenXrManagerTest.MenuQuadWaitsForTheChangeTimeOfARuntimeRecenter`, `changeTime`**: Located poses respect the change only from changeTime on; until then they are the old ones.
- **`OpenXrManagerTest.MenuOpenedAfterARecenterIsPlacedInFrontOfTheHeadInTheRecenteredSpace`**: A runtime whose local space origin is far from the head, as Pimax OpenXR's is (issue 238: the head read about 24 m above it). After a Recenter Action a menu opens in front of the head in the recentered space the quad is submitted in, not offset by where the head sits in the runtime's own local space.
- **`OpenXrManagerTest.SequentialDualPassSubmitsEachEyeWithPoseItWasRenderedWith`, `f.bridge->SetSubPassIndex(1);`**: Sub-pass 1 runs inside the open frame, so the engine reads the display-time pose; the head then moves again and is polled before present, so the latest polled pose (the fallback) differs from the one eye 1 was rendered with.
- **`OpenXrManagerTest.DispatchWithoutInputSupportKeepsTheSessionAndReportsUntrackedHands`**: A dispatch that implements none of the input calls (every IOpenXrDispatch input virtual keeps its XR_ERROR_FUNCTION_UNSUPPORTED default) is a runtime that refuses the Motion Controller action set.

### `tests/test_openxr_motion_controllers.cpp`

- **`OpenXrManagerTest.MotionControllerActionSetIsDeclaredAndAttachedExactlyOncePerSession`, `EXPECT_TRUE(headset.manager->CreateSession());`**: Asking for the session again, running many frames and a STOPPING -> READY cycle all reuse the one attached set: OpenXR allows a single attach per session.
- **`OpenXrManagerTest.SuggestedBindingsCoverTouchIndexViveAndKhronosSimpleControllers`, `simple`**: The Khronos simple controller only has select and menu: select acts as the trigger.
- **`ExpectGripRefusalLeavesTheRestAlone`**: A runtime that refuses only the grip pose must leave the aim pose, the inputs, head tracking and presentation alone: the grip pose just reports untracked.
- **`OpenXrManagerTest.HandStateIsFreshEvenWhenNoFrameHasBegun`, `headset`**: Before the first frame (or between frames) there is no frame snapshot: a read syncs itself, the way PollHeadPose falls back to "now".

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 114 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
