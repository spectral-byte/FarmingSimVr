# Decisions behind Present, the DXGI Proxy and the Desktop Mirror Window

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0014](0014-desktop-mirror-window-resolution-decoupling.md), [ADR-0015](0015-sequential-dual-pass-stereo-presentation-pipeline.md), [ADR-0019](0019-begin-the-openxr-frame-before-the-engine-update.md).

## Context and Decision
This record holds the non-obvious decisions behind the DXGI hooks and Present, the presentation plan, HDR, the Desktop Mirror Window and the Desk Mouse. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `src/proxy/desktop_cursor_port.h`

- **`IDesktopCursorPort`**: Everything the Mirror Window Controller asks Windows about the cursor and focus, behind one narrow interface: which window is in the foreground, the cursor clip (read, set, clear), the cursor position and moving the cursor. The controller makes no user32 call for these except through its port, so a test can supply a fake and never touch the developer's own mouse.
- **`Win32DesktopCursorPort`**: The port that talks to user32. The clip and the cursor move go through the originals of the ClipCursor and SetCursorPos detours, so it never re-enters the Injector's own hooks; until a detour is installed it calls user32 directly.
- **`Win32DesktopCursorPort`, `Win32DesktopCursorPort(const FunctionDetour& clipCursorDetour, const Function...`**: The detours are the controller's own and must outlive the port.

### `src/proxy/dxgi_export_hooks.cpp`

- **`s_installed`**: The installed instance. Published before the first hook goes live, so a hook always finds its trampolines.
- **`t_entryDepth`**: Entries running on this thread. Calls made while one runs go straight to the system.
- **`DxgiExportHooks::~DxgiExportHooks`, `if (IsProcessTerminating()) return;`**: At termination the patches stay, like every other hook's (see FunctionDetour).
- **`DxgiExportHooks::FindExport`, `if (rva >= directory.VirtualAddress && rva < directory.VirtualAddress + direc...`**: An address inside the export directory names a forwarder ("DLL.Function"), not code.

### `src/proxy/dxgi_export_hooks.h`

- **`DxgiExportHooks`**: Hooks CreateDXGIFactory, CreateDXGIFactory1 and CreateDXGIFactory2 in the system's dxgi.dll (ADR-0038), so a factory created by any module in the process (the game, a dxgi.dll in the game folder such as ReShade's, the OpenXR runtime) is created through the entry. While the entry runs on a thread, a call on that thread to any of the three goes straight to the system: that is the entry passing the call on through the system's export, the OpenXR runtime creating a factory while the entry starts it, or dxgi.dll calling its own export. One instance can be installed at a time.
- **`DxgiExportHooks`, `FindExport`**: The address of a module's export, read from its export table rather than asked of the loader, so it works while the loader is still loading the module. Null when there is no such export or it is forwarded.

### `src/proxy/dxgi_proxy.cpp`

- **`t_initializingProxy`**: The proxy whose VR initialization is running on this thread. A runtime that creates a DXGI factory during setup (VDXR's xrGetSystem does) does it on this thread. In the game, DxgiExportHooks passes such a call straight to the system; a caller that reaches the proxy itself (tests do) calls back into EnsureVrInitialized.
- **`SystemFactoryBehind`**: The system's factory behind `factory` when `factory` is a wrapper whose methods are not the system's, as a dxgi.dll in the game folder (ReShade's) hands out; null when `factory` is the system's own or none is found. An adapter is the system's even when the factory is wrapped, and its parent is the system's factory.
- **`FormatSwapChainFields`**: The swapchain fields shared by both descriptor structs, in one log-friendly string. Flags are hex with the bits worth reading off (tearing, frame-latency waitable object) named, so a log shows at a glance whether the game's swapchain can present without vsync or paces itself on a waitable handle.
- **`DxgiProxy::EnsureVrInitialized`, `if (m_ngxInterceptor) m_ngxInterceptor->InstallModuleLoadHooks();`**: Deferred from DllMain (loader lock). Runs before the game creates its D3D12 device. Installed on every factory call (idempotent and locked), not only the first, so a concurrent caller blocks until the LoadLibrary hooks exist instead of loading nvngx unhooked.
- **`DxgiProxy::EnsureVrInitialized`, `if (t_initializingProxy == this) return;`**: A runtime calling back in from inside our own initialization would wait on call_once forever; it gets its factory from the system dxgi.dll with the OpenXR state as initialization left it.
- **`DxgiProxy::EnsureVrInitialized`, `std::call_once`**: call_once, not a flag: a concurrent caller waits until initialization finishes, because it decides from the OpenXR state whether to hook the factory it is about to return.
- **`std::call_once`, `LogInfo("Injector operating in Active VR Mode.");`**: No session yet: the game has no D3D12 device to bind one to until it creates its swap chain, and SetD3D12DeviceAndQueue creates the session then.
- **`DxgiProxy::SetD3D12DeviceAndQueue`, `if (m_ownedByRuntime && !m_detourD3D11On12CreateDevice.IsInstalled()) {`**: SteamVR draws quad layers through a D3D11On12 device it creates during Present. With ReShade it passes ReShade's wrapped device and queue, which ReShade passes on unwrapped while inside Present, and D3D11On12 on wrapped D3D12 objects gets the device removed (DXGI_ERROR_INVALID_CALL). The hook hands it the real ones.
- **`DxgiProxy::SetD3D12DeviceAndQueue`, `required`**: The session is attempted next. A runtime bound to another GPU refuses the game's device; the attempt is still made, as the runtime decides, but the log says why VR may not start.
- **`DxgiProxy::UnwrapForD3D11On12`, `realModule`**: A wrapper's methods live in another module than the real object's (ReShade's in its dxgi.dll).
- **`DxgiProxy::UnwrapForD3D11On12`, `device = realDevice;`**: D3D12 keeps one device per adapter, so a device wrapper on the game's adapter wraps the game's device.
- **`DxgiProxy::UnwrapForD3D11On12`, `queues[i] = realQueue;`**: The game makes one queue of each type, so a wrapper of the swap chain queue's type wraps that queue.
- **`DxgiProxy::AttachSwapChain`, `ReinstallCrashReporter();`**: The game may have installed a crash filter of its own since the first factory call (a no-op unless the Injector installed its crash reporter).
- **`DxgiProxy::AttachSwapChain`, `m_gameSwapChain = swapChain;`**: Recorded after the inactive check on purpose: Pass-Through is final. Initialize runs once, before the game has a swap chain, and an Active session that is lost only ever falls back to Pass-Through; nothing makes the manager Active again. So a swap chain created while inactive can never become the one to act on, and leaving it unrecorded keeps every hook passing straight through, which is what Pass-Through means. (A replaced swap chain stays recorded until the next attach, and a new one at a recycled address would be taken for it: both narrow and harmless, as the old chain was the game's.)
- **`DxgiProxy::AttachFactory`, `systemFactory`**: Hooking a wrapper's methods would put the swap chain hooks outside it: the eye copy would run before what the wrapper draws on Present (ReShade's effects), and the swap chain and queue seen would be its wrappers. The system's factory inside it is hooked instead, so whatever wraps DXGI runs first.
- **`DxgiProxy::AttachFactory`, `AttachFirstOutput(factory);`**: The game asks the display whether it is HDR before it creates its swap chain.
- **`DxgiProxy::AttachFirstOutput`, `for (UINT a = 0; a < 16 && !m_outputAttached; ++a) {`**: An adapter with no display (a render-only GPU) has no outputs, so look past it.
- **`DxgiProxy::PrepareGameSwapChainCreation`, `LogInfo(std::string("DxgiProxy: ") + hookName + " for a window other than the...`**: Not the game's window: its swap chain is never the game's, so it gets none of the game's treatment.
- **`DxgiProxy::PrepareGameSwapChainCreation`, `ReleaseCachedBackBuffers();`**: The game's window gets a new swap chain: nothing of the old one may outlive the creation.
- **`DxgiProxy::OnPresent`, `if (m_vrBridge) {`**: The rendered-frame counter stamps the Companion Mod's camera records and resolves them for DLSS evaluations. It advances per stereo Present only; a Present outside stereo presentation (menu, 2D Floating Window Mode, Pass-Through) renders no eye camera, so its records are dropped rather than left to read as the current frame's.
- **`DxgiProxy::OnPresentImpl`, `backBufferHolder`**: Decide first, then act: the facts of this Present, the plan they give, and the plan carried out. The back buffer is held here until the Present is done, whether or not the cache holds it too.
- **`DxgiProxy::GatherPresentFacts`, `return facts;`**: Not Active: no eye pair can be in flight, so a queued mode change must not be lost.
- **`DxgiProxy::GatherPresentFacts`, `m_openXrManager->PollEvents();`**: Poll runtime events before deciding how to present; this may begin/end the session or drop the Injector into Pass-Through Mode (EXITING, LOSS_PENDING, instance loss).
- **`DxgiProxy::GatherPresentFacts`, `return facts;`**: Dropped to Pass-Through after a mode was queued; the released eye pair is abandoned.
- **`DxgiProxy::GatherPresentFacts`, `m_openXrManager->CreateEyeSwapchains(bufferWidth, bufferHeight, format);`**: Create eye swapchains matching the backbuffer resolution for direct zero-overhead D3D12 copy. If dimensions are unavailable (0), CreateEyeSwapchains will query runtime recommended views as fallback.
- **`DxgiProxy::CarryOutPresentPlan`, `UpdateMouseConfinement();`**: Mouse Confinement first: when it starts holding it brings the Desk Mouse to the middle of the picture, and this frame's drawn cursor reads where the Desk Mouse is.
- **`DxgiProxy::CarryOutPresentPlan`, `m_openXrManager->SubmitMenuQuad();`**: Floating quad layer mode: Backbuffer is presented on an OpenXR Quad layer positioned in 3D tracking space rather than warping across an immersive projection layer. Active during pre-game / Main Menu, in-game GUI menus (ESC pause menu, shop, etc.), or in 2D Floating Window Mode.
- **`DxgiProxy::CarryOutPresentPlan`, `mirrorOffNow`**: Whether the Desktop Mirror Window is off is the one fact a step above can change (a submission that finds the session lost drops the Injector to Pass-Through Mode), so it is asked again, and only the tail of the plan follows.
- **`DxgiProxy::CarryOutPresentPlan`, `if (plan.clearBackBufferToBlack && m_openXrManager) {`**: Called after this frame's eye copy, so the headset has the game's frame; only the Desktop Mirror Window gets the black one. A null back buffer (unit tests) leaves nothing to clear.
- **`DxgiProxy::UpdateDesktopMirrorTitle`, `if (!m_desktopMirrorOffAnnounced) {`**: The transition is logged once. The title is applied whenever it is not shown, because it can be missing while the mirror stays off: the game window may be unknown yet, or may have changed.
- **`DxgiProxy::UpdateDesktopMirrorTitle`, `reason`**: The handshake and Pass-Through checks come first: a savegame unload also resets the flag to on.
- **`DxgiProxy::UpdateMouseConfinement`, `state`**: The Injector is inert in Pass-Through Mode; "session running" is the one that decides whether the Desktop Mirror Window shows by itself (a running session that is not focused still counts). The vrMouseConfinement setting only counts once a savegame has loaded (the Companion Mod's handshake): before that, in the main menu, it always holds. It is tested before the focus so that a setting turned off while the game window is not in front is the reason that is logged when it is turned on again.
- **`DxgiProxy::OnPresentReturned`, `if (FAILED(presentResult)) return;`**: A failed Present (device removed or reset) leaves the next frame to the lazy begin in CopyBackBufferToEye rather than blocking in xrWaitFrame ahead of device-lost handling.
- **`DxgiProxy::EffectiveSyncInterval`, `xrPacing`**: xrWaitFrame paces frames only while the session runs and eye swapchains exist (OnPresent calls it only then). While it is paused (headset asleep, not yet READY) or swapchain creation failed, keep the game's own vsync so the Desktop Mirror Window does not run uncapped.
- **`DxgiProxy::Intercept_Present`, `passThrough`**: Another swap chain in the process (an overlay, a capture tool): DXGI sees the call as its caller made it.
- **`if (proxy && proxy->IsGameSwapChain(self)) {`**: Another swap chain keeps the size it asked for and leaves the game's cached back buffers alone.
- **`proxy->m_openXrManager->EnsureBackBufferFormatSupported(pDesc->BufferDesc.For...`**: Before any override below: an unusable format must leave the swap chain untouched.
- **`LogInfo("DxgiProxy: CreateSwapChainForHwnd requested: " + FormatSwapChainDesc...`**: Before the overrides below, which also write through to the caller's descriptors.
- **`proxy->m_openXrManager->EnsureBackBufferFormatSupported(pDesc->Format);`**: Before any override below: an unusable format must leave the swap chain untouched.

### `src/proxy/dxgi_proxy.h`

- **`IsHdrColorSpace`**: HDR10, scRGB and HLG colour spaces. OpenXR runtimes show the eye image as SDR, so HDR output copied into an eye swapchain looks washed out; while VR is active the game is told the display and swap chain support none of these, and renders SDR instead.
- **`DxgiProxy`**: Every collaborator is supplied by the caller (production wiring comes from InjectorRuntime); a null one is absent and its feature is skipped. The window controller is required. AttachSwapChain/AttachFactory install detours only while a live InjectorRuntime owns the proxy.
- **`DxgiProxy`, `CreateDXGIFactory`**: The game's factory calls, caught by DxgiExportHooks in the system's dxgi.dll (ADR-0038). Each starts VR on the first call, passes the call on to the system and hooks the factory it returns.
- **`DxgiProxy`, `AttachFactory`**: Hooks swap chain creation on the factory. When the factory is another tool's wrapper (ReShade's), the system's factory behind it is hooked instead, so the wrapper runs before the hooks.
- **`DxgiProxy`, `AttachOutput`**: Hooks IDXGIOutput6::GetDesc1 (shared by every output) so the display reads as SDR while VR is active. AttachFactory calls it with the first output it finds.
- **`DxgiProxy`, `OnPresentReturned`**: Called once the real Present (if any) has returned. Waits on and begins the next OpenXR frame so the Companion Mod's pose requests for the coming eye pair are located at that frame's predicted display time (ADR-0019). A no-op when the real Present failed, a frame is already begun or the session cannot run frames.
- **`DxgiProxy`, `LatchPendingRenderingMode`**: Latches the Stereo Presentation Mode change queued in VRBridge. Called only at completed compositor frame-pair boundaries (ADR-0015).
- **`DxgiProxy`, `UnwrapForD3D11On12`**: Swaps a wrapper of the game's D3D12 device or queue (ReShade's) for the real object behind it. Returns a description of what was passed and what was swapped, for the log.
- **`DxgiProxy`, `PrepareResize`**: The part of both ResizeBuffers hooks that precedes the call to the original: forgets the cached back buffers, marks the window clamp dirty and, in Active VR Mode, replaces the requested size with the Render Resolution.
- **`DxgiProxy`, `m_presentWasStereo`**: Set by the bookkeeping of every Present (from the plan) when the Present was a stereo presentation (an eye copy), so OnPresent advances the rendered-frame counter for it and discards camera records for any other Present.
- **`DxgiProxy`, `OnPresentImpl`**: One Present in three steps (present_plan.h): the facts, the plan PlanPresent makes of them, the plan carried out with the bookkeeping done once at its end. Returns whether the game's real Present is called.
- **`DxgiProxy`, `GatherPresentFacts`**: Does the setup the decision needs, then reports the facts. The setup has side effects: it polls the runtime's events, learns the game window, resolves the device and creates the D3D12 device/queue and the eye swapchains, and finds the back buffer (cached; held in `backBufferHolder` when the cache does not hold it alone). It makes no plan decisions: a Present with no VR work is a plan, not an early return.
- **`DxgiProxy`, `CarryOutPresentPlan`**: Carries `plan` out in the one order the OpenXR runtime and DXGI see (ADR-0015, ADR-0019): pacing reset, the eye copy, the submission, the counters, the latch, then the Desktop Mirror Window's black frame; then the bookkeeping. `facts` are the ones the plan was made from.
- **`DxgiProxy`, `IsDesktopMirrorOffNow`**: True while the Desktop Mirror Window is off right now: the player turned it off (VRBridge flag), the Companion Mod's handshake has completed, and the headset session runs with eye swapchains. Everywhere else (Pass-Through Mode, session not running, before the handshake) the mirror shows by itself.
- **`DxgiProxy`, `m_desktopMirrorWasShowing`**: True while the Desktop Mirror Window showed at the last Present (on, or shown by itself); false once it switched off. A frame that goes from showing to off presents once, black (PresentPlan::clearBackBufferToBlack).
- **`DxgiProxy`, `UpdateDesktopMirrorTitle`**: After each Present: logs each change of IsDesktopMirrorOffNow() once, and keeps the game window's title matching it (off note whenever the mirror is off and the title is not shown, the kept title when it shows again). Leaves the title alone otherwise.
- **`DxgiProxy`, `UpdateMouseConfinement`**: At each Present, on every Present path: tells the Mirror Window Controller whether Mouse Confinement holds the Desk Mouse to the game picture, so that it can also let go on every path. The menu quad path does it before it reads the Desk Mouse for the drawn cursor, so the frame where holding starts already draws the cursor where it was brought to; every other path does it after the Present's work. It runs once per Present (m_mouseConfinementUpdated).
- **`DxgiProxy`, `m_gameSwapChain`**: The game's swap chain, by identity only: no reference is held, so the game can still destroy it. Set when the proxy attaches to it; every hook acts on this one and passes any other swap chain's call straight through. Atomic because another tool's swap chain (an overlay, a capture tool) may present from a thread of its own while the game's creation writes this: a statement about the process, not a guarantee the design gives.
- **`DxgiProxy`, `ReleaseCachedBackBuffers`**: Lets go of the game's cached back buffers and the swap chain they were cached from, so the Injector keeps nothing of it alive: before a resize and before a creation for the game window is passed on.

### `src/proxy/mirror_window_controller.cpp`

- **`MirrorWindowController::UpdateMenuCursorState`, `pointer`**: The Pointer Ray has control: the cursor is drawn where the ray lands on the panel, whatever the desk mouse is doing or which window has focus.
- **`MirrorWindowController::UpdateMouseConfinement`, `if (m_hasHeldMouse || state == MouseConfinementState::TurnedOffByCommand) m_l...`**: The latest cause is recorded whether or not it was holding, so the line for the next start names what actually ended last (a setting turned on while focus is lost is not why it holds again). Until it has held once, the first start is just the headset coming into use, whatever kept it from holding before (the window not in front yet, the session not running); only the setting is kept.
- **`MirrorWindowController::UpdateMouseConfinement`, `cursor`**: A Desk Mouse that is off the picture when holding starts comes to the middle of the physical client area, through the port's original SetCursorPos. Only at the start: never while it keeps holding.
- **`MirrorWindowController::UpdateMouseConfinement`, `current`**: Set again only when the clip differs from the picture: Windows or another program may have cleared it.
- **`MirrorWindowController::GameClipOnScreen`, `OffsetRect(&clip, picture.left - m_gameClipOrigin->x, picture.top - m_gameCli...`**: The window may have moved since the game asked: the clip keeps its place in the picture.
- **`MirrorWindowController::StopHoldingMouse`, `set`**: Put back what the game last asked for, so its own mouse-look capture survives. Only a clip that is still the picture it set itself is its to release: one the game, Windows or another program put there since (or took away) stays as it is.
- **`MirrorWindowController::HandleGameClipCursor`, `return SetPictureClip(picture);`**: The game lets go (it does in a menu) but the Desk Mouse stays on the picture.
- **`MirrorWindowController::HandleGameClipCursor`, `m_setMousePicture.reset();`**: The game's own clip is on the desktop now: not one Mouse Confinement set, so not its to release.
- **`MirrorWindowController::Intercept_SetCursorPos`, `invokeOrig`**: With a live runtime the move goes through the desktop cursor port; without one, straight to user32.

### `src/proxy/mirror_window_controller.h`

- **`MirrorWindowController`, `IsGameWindowForeground`**: Whether the game window has focus: the foreground window is the game window or one of its children. The one answer wherever "the game window has focus" matters.
- **`MirrorWindowController`, `MouseConfinementState`**: Mouse Confinement: while the headset is in use and the game window has focus, the Desk Mouse is kept on the game picture (the window's client area in physical screen coordinates). The game's Present says once per frame whether it should hold, and if not why not (the reason is only logged).
- **`MirrorWindowController`, `HandleGameClipCursor`**: The game's own ClipCursor call, as the hook receives it: remembers what the game asked for and puts it on the desktop, except that while Mouse Confinement holds the game's release becomes the picture.
- **`MirrorWindowController`, `SetOwnedByRuntime`**: Win32 Detours management. Detours are installed only while a live InjectorRuntime owns the controller, so the hooks can always find the controller that holds their originals.
- **`MirrorWindowController`, `ShowDesktopMirrorOffTitle`**: Game window title while the Desktop Mirror Window is off: keeps the window's current title and sets the off note; RestoreTitle puts the kept title back. Both are no-ops when there is nothing to do, and the controller restores the title itself in DetachAll.
- **`MirrorWindowController`, `GetGamePicture`**: The game picture: the game window's real client area in screen coordinates, whatever size the game is told (Resolution Decoupling). False when the window has no client area to hold the mouse to.
- **`MirrorWindowController`, `StopHoldingMouse`**: Mouse Confinement lets go: puts back what the game last asked for, but only while the clip on the desktop is still the picture it set itself.
- **`MirrorWindowController`, `m_gameClip`**: Mouse Confinement. m_gameClip is what the game last asked ClipCursor for (empty: a release), in screen coordinates as of when it asked; m_gameClipOrigin is where the game picture's top left was then, so the clip can be put back where the window is now (GameClipOnScreen). m_holdingMouse is whether it holds now; m_setMousePicture is the picture it last put on the desktop as the clip (empty: it has set nothing that is still its to release). m_lastMouseRelease is the latest reason it was not holding, for the log line when it holds again.

### `src/proxy/present_plan.cpp`

- **`PresentAsTheGameDid`**: A Present that does no VR work: the game presents as it always did, no eye pair is in flight (so a queued Stereo Presentation Mode may be applied) and the Desktop Mirror Window shows by itself.
- **`ApplyDesktopMirror`**: With the headset fed, the Desktop Mirror Window decides whether the real Present is called. A mirror going from showing to off presents once more, black, so the window does not keep the last frame. A mirror that stays on keeps the plan's defaults: present, mirror Showing.
- **`PlanPresent`, `if (!facts.active || !facts.eyeSwapchains) {`**: Not Active, or no eye swapchains (creation failed): no eye pair can be in flight.
- **`PlanPresent`, `plan`**: IDLE or STOPPING (headset asleep, dashboard) or not yet READY: no VR frames, the game keeps presenting to the Desktop Mirror Window. On resume a fresh eye pair starts and the menu quad is aligned to the new head pose.
- **`PlanPresent`, `plan.copyToEye = 0;`**: Pre-game, a menu or 2D Floating Window Mode: the back buffer goes on the floating quad layer, a completed compositor frame of its own.
- **`PlanPresent`, `plan.resetPacing = facts.wasInQuadMode;`**: Stereoscopic 3D view. Coming back from the quad starts the eye pair over, so the phase is that of a fresh one.
- **`PlanPresent`, `plan.advance = FrameAdvance::ToSecondSubPass;`**: Sub-Pass 0: the left eye, the physical Present is suppressed, the next Present is sub-pass 1.
- **`PlanPresent`, `plan.advance = FrameAdvance::ToNextFrame;`**: Sub-Pass 1, or Alternate Frame Rendering: the pair is whole after the right eye.

### `src/proxy/present_plan.h`

- **`PresentFacts`**: What one Present of the game's swap chain is, decided in one step from facts alone (ADR-0015, ADR-0019). The DxgiProxy gathers the facts, asks PlanPresent for the plan, carries it out in a fixed order and does the bookkeeping once, so a case cannot forget one of the bookkeeping steps every case shares.

### `src/proxy/system_dxgi.h`

- **`IsSystemDxgiModule`**: True when `module` is the system's dxgi.dll, not a dxgi.dll loaded from elsewhere (such as ReShade's in the game folder).
- **`SystemDxgi`**: The factory exports of the system's dxgi.dll. In the game they are hooked (DxgiExportHooks), and a call the proxy makes through them while it handles a hooked call goes straight to the system.

### `tests/test_d3d11on12_unwrap.cpp`

- **`FakeD3D12Wrapper`**: A D3D12 device or queue wrapper as ReShade hands them out: another object, with its methods in another module (this test executable rather than D3D12Core.dll), forwarding to the real object. Only the calls the swap makes are implemented; any other slot records that it was called.
- **`FakeD3D12Wrapper`, `GetAdapterLuid`**: Members returning a struct take a hidden result pointer after `this` and return it.

### `tests/test_dxgi_export_hooks.cpp`

- **`DxgiExportHooksTest.FindExportGivesNoAddressForAForwardedExport`, `path`**: The system's version.dll forwards VerLanguageNameW to kernel32: there is no code of its own to hook.
- **`DxgiExportHooksTest.FactoryCallsThroughTheSystemExportsReachTheEntryAndStillCreateAFactory`, `create`**: Called the way any module calls them. The entry passes each on through the same, hooked, export; that call goes straight to the system instead of back to the entry.

### `tests/test_dxgi_proxy.cpp`

- **`static std::unique_ptr<InjectorRuntime> MakeRuntime(`**: Detours are installed only by a proxy a live runtime owns, and hook entry points resolve their subsystem through the live runtimes, so tests that drive them build their proxy through one. Each runtime gets its own window controller and Luau interceptor.
- **`DxgiCallingOpenXrRuntime`**: Like VDXR: xrGetSystem creates a DXGI factory on the thread that starts VR. In the game DxgiExportHooks passes that call straight to the system; a runtime that reached the proxy itself must not deadlock either.
- **`DxgiProxyTest.RuntimeCreatingAFactoryDuringInitializationDoesNotDeadlock`, `runtime`**: Leaked on a timeout: the blocked worker thread still uses them.
- **`DxgiProxyTest.RuntimeCreatingAFactoryDuringInitializationDoesNotDeadlock`, `result`**: A detached thread rather than std::async, whose pool would keep a deadlocked process from exiting.
- **`GraphicsBindingRequiredOpenXrRuntime`**: Like a real runtime: a session needs a graphics binding once a graphics extension is enabled.
- **`InjectorRuntimeTest.NgxInterceptorIgnoresAnInactiveProxyInPassThrough`, `runtime`**: In Pass-Through the NGX hooks must not take the proxy's mirror target resolution, or DLSS would be created at that size instead of the game's own output size.
- **`InjectorRuntimeTest.DestructorTakesNoNgxLockWhileProcessTerminates`, `ngx`**: A thread killed at termination may own the NGX mutex; the destructor must not touch it.
- **`ExpectBothEyesLocatedAtThePairsDisplayTime`**: Drives eye pairs through OnPresent/OnPresentReturned the way the Present detour does. For each pair the head is at x = pairIndex when the frame begins and has moved on by the time the Companion Mod asks for either eye's pose; both eyes must still get the pose located at the pair's predicted display time.
- **`ExpectBothEyesLocatedAtThePairsDisplayTime`, `for (XrTime time : mockDispatch->locateSpaceTimes) {`**: Every pose lookup used a frame's predicted display time, never a time derived from "now".
- **`DxgiProxyTest.OnPresentReturnedDoesNotBeginAFrameAfterAFailedPresent`, `proxy.OnPresentReturned(DXGI_STATUS_OCCLUDED);`**: a success code: the next frame begins
- **`MirrorPresentRig`**: The game's swapchain Present as the Desktop Mirror Window tests drive it: an active OpenXR mock, a real VRBridge and a mock swapchain whose real Present calls are counted in s_mockPresentCalls. The game has a window (an on-screen test window of 800x600, whose client area in screen coordinates is clientArea), and the runtime reaches the desktop's cursor and focus through cursorPort, a fake: the test chooses the foreground window and reads the clip and the cursor position. Tests never touch the real cursor.
- **`DxgiProxyTest.DesktopMirrorOffShowsAgainWhileTheHeadsetIsAsleepEvenThoughTheSessionKeepsRunning`, `rig.dispatch->events.PushSessionState(XR_SESSION_STATE_VISIBLE);`**: Real runtimes drop to SYNCHRONIZED when the headset sleeps; the session is not stopped.
- **`DxgiProxyTest.BlackFrameTurningTheMirrorOffWhileTheSessionRunsPresentsExactlyOnceInAfr`**: Desktop Mirror Window black frame: each time the mirror goes from showing to off, one real Present reaches DXGI (the backbuffer cleared black after the eye copy), then none until the mirror shows again.
- **`FS25VR_CHILD_CHECK`**: These need a process with no live runtime, but other tests leave InjectorRuntime::Instance() alive in the shared test process. EXPECT_EXIT runs each body in a fresh child process instead; the child reports each failed check on stderr and exits with the number of failures.
- **`s_childClipCalls`**: Takes the place of user32's ClipCursor in the child process below (hooked with a FunctionDetour that never calls the original), so the no-runtime fallback of the ClipCursor hook is covered without a call that could trap the developer's own mouse.
- **`WrappingFactory`**: A factory that wraps the system's and passes every call on to it, the way a dxgi.dll in the game folder (ReShade's) hands the game a factory of its own.
- **`DxgiProxyTest.KeepsGameSyncIntervalWhileSessionIsPaused`, `EXPECT_EQ(proxy.EffectiveSyncInterval(1), 1u);`**: Before READY nothing calls xrWaitFrame, so the game's vsync must stay in effect.
- **`DxgiProxyTest.KeepsGameSyncIntervalWhileSessionRunsWithoutEyeSwapchains`, `vrBridge`**: CreateEyeSwapchains is deliberately skipped: it failed, so OnPresent never calls xrWaitFrame.
- **`DxgiProxyTest.PendingRenderingModeLatchesWhileSessionIsPaused`, `vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);`**: Headset asleep: no eye pair will be submitted, so the change must not wait for one.
- **`DxgiProxyTest.PendingRenderingModeLatchesWhenNoEyeSwapchainsExist`, `vrBridge->SetPendingRenderingMode(VRRenderingMode::AFR);`**: No eye pair can ever be in flight, so the queued change must not wait for a pair boundary.
- **`DxgiProxyTest.ClampedWindowRestoresFullNormalizedMouseRangeAcrossEdges`, `unclampedW`**: Contrast with the 66% clipping issue on an unclamped window: If the window were unclamped at 3840x2160 on a 2560x1440 display, the OS clamps GetCursorPos to monitor boundaries (max screen X = 2560, max screen Y = 1440).
- **`DxgiProxyTest.NormalizedMouseCoordinatesReachAbsoluteMenuQuadBoundariesInVr`, `clippedNormX`**: 2. Contrast with the 66% clipping issue before resolution decoupling:

  Without resolution decoupling, normalized cursor coords were clipped at \~0.6667

- **`DxgiProxyTest.SetCursorPosCentersCursorAndEnablesBidirectionalLookParity`, `testWnd`**: Replicate Desktop Mirror Window geometry (clamped on ultrawide/4K display). The window must lie fully on screen (CI runners are 1024x768) or the OS clamps ClipCursor to the desktop and the hooked clip rect can't be verified exactly.
- **`DxgiProxyTest.DesktopMirrorOffTitleIsSetWhenTheGameWindowIsKnownOnlyAfterTheMirrorWentOff`**: The game window can be unknown when the mirror goes off (it arrives with a later swapchain).
- **`DxgiProxyTest.MouseConfinementBringsTheDeskMouseToThePhysicalMiddleWhenTheRenderSizeIsLargerThanTheWindow`, `EXPECT_EQ(rig.cursorPort->setCursorPosCalls, 1);`**: The port is the original SetCursorPos: the point is the window's real middle, not the render-size one.
- **`namespace {`**: Another swap chain in the process (an overlay, a capture tool, a second window) shares the system's DXGI code with the game's, so its calls reach the Injector's hooks too. The hooks act only on the game's swap chain (#264): everything else reaches DXGI exactly as the caller made it. The rig has a game swap chain the proxy attached to and a second one the proxy never saw, both objects of the same class (one vtable), and records what reached the "system" for each.
- **`GameAndOtherSwapChainRig`, `windows`**: Declared before the runtime so the windows outlive it: the runtime's last DetachAll may still touch them.
- **`DxgiProxyTest.TheGamesSwapChainIsTheSystemObjectTheHooksAreCalledWithNotAWrapperAroundIt`, `rig`**: Behind a wrapper such as ReShade's the game holds the wrapper and the wrapper calls the system's swap chain, so the hooks see only the system object the creation handed out (ADR-0038).
- **`DxgiProxyTest.ASwapChainCreatedAtTheOldOnesAddressDoesNotInheritTheOldBackBuffers`, `rig`**: The game destroys its swap chain and creates the next one; the allocator may hand out the same address.
- **`SessionRefusingOpenXrRuntime`**: A runtime that refuses the session for the game's device, as one does when the headset is on another GPU than the one the game renders on. Its graphics requirements name `requiredAdapter`.
- **`RefusedSessionRig`, `CreateForHwnd`**: The game creates its swap chain the way FS25 does: windowed=FALSE, at 3840x2160, scaling NONE.

### `tests/test_mirror_window_controller.cpp`

- **`s_standInClipCalls`**: Stand-ins for user32's ClipCursor and SetCursorPos, so the production port can be exercised without touching the developer's real cursor: the "original" records what reached it, the detour what re-entered.

### `tests/test_present_plan.cpp`

- **`PresentPlanTest.ReturningFromTheQuadStartsTheEyePairAgainFromTheLeftEye`, `for (bool sdp : {false, true}) {`**: The phase in the facts is the one before the pacing is reset; the plan copies as it stands after.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 132 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
