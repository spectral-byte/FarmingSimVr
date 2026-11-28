# Decisions behind Injector Startup, Lifecycle, Logging and Crash Reporting

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0036](0036-the-injector-reads-the-menu-distance-from-the-companion-mods-settings.md), [ADR-0038](0038-the-injector-is-the-games-version-dll.md), [ADR-0039](0039-the-installer-turns-on-windows-crash-dumps-for-the-game.md), [ADR-0040](0040-the-injector-reports-its-own-crashes.md).

## Context and Decision
This record holds the non-obvious decisions behind the Injector outside VR: startup and the loader lock, the version.dll exports, detours, the logger, the performance trace and the crash reporter. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `src/core/camera_records.cpp`

- **`CameraRecordStore::Resolve`, `if (out.sameEyePrevious) {`**: A same-eye record is only the eye's previous render when every frame in between has a record of the other eye: a frame with no record could have been this eye's own render, and its camera is unknown.

### `src/core/camera_records.h`

- **`CameraRecord`**: The camera the engine rendered one stereo sub-pass with, as the Companion Mod reports it after binding the eye camera. World transform in double precision (map coordinates reach millions of metres), orientation as a unit quaternion (x, y, z, w).
- **`RelativeCamera`**: Another camera's transform relative to a reference ("current") camera, computed in double precision: translation is the other camera's position expressed in the reference camera's axes, rotation is conj(reference) \* other.
- **`ResolvedCameras`**: The cameras a DLSS evaluation needs. A record that was not reported for the right frame is absent; relative transforms exist only when the current camera and the other record both do.
- **`CameraRecordStore`**: A short ring of camera records stamped with a rendered-frame counter. The Companion Mod reports (Luau thread), the DXGI proxy advances the counter once per Present in stereo presentation, and the NGX hook resolves (render thread), so access is serialised.
- **`CameraRecordStore`, `kMaxSameEyeFrameGap`**: The oldest frame distance at which a record still counts as the same eye's previous camera: the eyes alternate every frame, so an eye renders two frames after its previous render.
- **`CameraRecordStore`, `Report`**: Stamps the record with the current rendered frame and keeps it. Returns false (and keeps nothing) for an unknown eye or a non-finite or zero-length orientation. The same eye reported twice in one frame keeps the latest.
- **`CameraRecordStore`, `Clear`**: Drops every record: frames outside stereo presentation report none, and the counter does not advance there, so without this their records would read as the current frame's.

### `src/core/companion_settings.cpp`

- **`kDocumentsFolderId`**: FOLDERID_Documents, spelled out so the Injector needs neither knownfolders.h's definition (uuid.lib) nor shell32 at load time.
- **`KnownDocumentsFolder`**: SHGetKnownFolderPath and CoTaskMemFree are looked up when they are needed, so the Injector, which the game loads early as its version.dll, keeps shell32 and ole32 out of its import table. Both are loaded in the game process anyway.
- **`ReadCompanionSettingFloat`, `kMaxBytes`**: The file holds a few dozen settings; anything this large is not one the Companion Mod wrote. The read buffer is sized to the actual file length capped at `kMaxBytes`.

### `src/core/companion_settings.h`

- **`DefaultCompanionSettingsPath`**: The Companion Mod's settings.xml, read by the Injector for the settings it needs before a savegame has loaded: the Companion Mod only runs once one has, but the main menu is shown before that. The Companion Mod writes the file; the Injector only ever reads it.
- **`DefaultCompanionSettingsPath`**: \<Documents>\My Games\FarmingSimulator2025\modSettings\FS25_VRCompanion\settings.xml of the user running the game, where Documents is the known folder (so a OneDrive-redirected one too), as the Installer assumes. Empty when the Documents folder cannot be found.

### `src/core/crash_reporter.cpp`

- **`g_installed`**: Pieces of state the filter reads, which it cannot be handed: the OS calls it with the exception alone.
- **`g_log`**: The log, looked up while installing so that the filter never has to construct it inside a crashing process.
- **`kMaxFiltersPassedOn`**: The filters the reporter passes a crash on to, the newest first: the one that was on top when the reporter was installed, and each one it found on top when it installed again. More than one, because a filter that took the top place after the reporter may have called the filter it replaced (the reporter) from its own, as filters that chain do. The reporter, installed again on top of it, passes the crash on to it, it calls the reporter, and the reporter would pass the crash on to it again, for ever. The way out is to count how many filters this thread has been passed on to: the reporter, reached again from inside one, passes on to the next older one, the filter that was below it before the later one came. Each filter is called once, and the chain ends when the list does.
- **`kDumpFileName`**: The dump sits next to fs25_vr.log under a fixed name, so that the next crash replaces it and the Installer's uninstall can remove it. The crash line names it.
- **`kDumpTimeoutMilliseconds`**: How long the filter waits for the helper thread. A dump of the whole game takes a few seconds at most; a helper that takes longer is stuck (a thread it needs is suspended or holds a lock), and the game's exit must not wait for it.
- **`kDumpType`**: What goes into the dump: the exception record, every thread's stack, the module list (unloaded modules included) and the memory the stacks point at, but not the whole process. The Installer asks Windows for the same contents for the game's own dump (CustomDumpFlags in installer/fs25_vr.iss, see kCrashDumpFlags).
- **`g_helperReady`**: The helper thread waits for a crash. It does the crash line as well as the dump: the crashing thread may have next to no stack (after a stack overflow), and writing a log line takes more than it has. Without the helper (a crash before the first DXGI factory call) the crashing thread writes the line itself, if its stack allows.
- **`g_dumpPath`**: Everything the helper needs is prepared ahead of the crash and kept in static storage: nothing is allocated, loaded or created while the process is crashing.
- **`kStackNeededForTheLine`**: The stack the crash line needs on the crashing thread, when there is no helper to write it. The log's write and flush run through the C runtime, and the first write allocates the file's buffer. Measured with a child that raises a stack overflow with a given margin (tests/test_crash_reporter.cpp): the line faulted with up to 16 KB left at the raise, the exception dispatcher's share of it included, and never with more. A filter that runs out of stack faults, and the crash is then reported as that fault instead of what it was; so a thread with less than this left writes no line. That is every thread after a real stack overflow, which leaves a few kilobytes: with no helper (before the first DXGI factory call) such a crash has no line, and Windows' own dump of the game is what is left.
- **`CrashText`**: Text put together without the heap: the filter may run in a process whose heap is what crashed, on a stack that has just overflowed. Used by one crash only, so it can live in static storage instead of on the stack.
- **`using NtQueryVirtualMemoryFn = LONG(NTAPI*)(HANDLE process, PVOID address, in...`**: The name of the file that is mapped at an address: the query behind GetMappedFileName, without that function's heap buffer, which a crash in the heap would leave locked. Found while installing, since looking it up is not for the filter. The answer goes to static storage for the same reason as the text: it is long, and the stack may be short.
- **`AddAddress`**: Names where the address lies as module+0xoffset, the form a dump or a map file is read with. Plain queries of the memory manager: GetModuleHandleEx and GetModuleFileName may wait for the loader lock, and a thread stuck in a load must not turn the crash into a hang.
- **`AddAddress`, `path`**: The path is the NT path of the image ("\Device\HarddiskVolume3\...\game.exe"): keep the file name.
- **`AddDumpOutcome`**: How the line ends: the dump's file name, or why there is no dump. The line is written before the dump, so that the log is complete whatever the dump does to the process; a dump that then fails or takes too long is told in a line of its own.
- **`PassOn`**: Passes the crash on to the next filter of the list that this thread has not been passed on to: see g_filters. With none left, Windows continues its own handling.
- **`RememberFilterFoundOnTop`**: Puts what SetUnhandledExceptionFilter found on top in front of the list, or empties the list when it found none: whoever removed the reporter removed the chain with it, and a filter that has been taken away may sit in a module that is gone. The crash then goes to Windows' own handling.
- **`ReportDumpFailure`**: A dump that was announced in the crash line and then did not come: the line that follows says so. Runs on the helper thread, which knows what went wrong, or on the crashing thread when the helper never finished, using its own static `CrashText` buffer so a helper timeout cannot race with `DumpHelper`'s `g_crashText`.
- **`ReportDumpFailure`, `if (!log || g_failureReported.exchange(true)) return;`**: Once: a helper that finishes after the crashing thread gave up on it would tell its failure over the other's line.
- **`WriteTheDump`, `DeleteFileW(g_dumpPath);`**: Not a dump anyone could open: better no file than a misleading one.
- **`DumpHelper`**: The helper thread. It waits from the first DXGI factory call until a crash wakes it, then does the work the crashing thread has no stack for: the crash line first, flushed, so that the log is complete whatever the dump does to the process; then the dump. Win32 calls only: it runs without the C runtime's per-thread state, and it must not touch the heap before a crash asks for it. It is ended with TerminateThread when the Injector unloads, since its code goes with the Injector (RemoveCrashReporter).
- **`HandTheCrashToTheHelper`, `if (waited != WAIT_OBJECT_0 && HasStackFor(kStackNeededForTheLine)) {`**: The helper tells its own failures. One that never finished cannot, and the line is written here if there is room.
- **`CrashFilter`**: The order of work: the crash line first, flushed, so that the log is complete whatever happens next; then (when there is one) the dump; then the previous filter. All of it on the helper thread when there is one.
- **`CrashFilter`, `ReportCrash`**: Before the first DXGI factory call. The state cannot be Ready here, but it may say why there is no helper.
- **`PrepareTheDump`, `LogWarn("Injector: A crash will leave no dump: the path " + std::filesystem::...`**: Not NoDumpReason: the warning names the path, which the crash line has no room for.
- **`PrepareTheDump`, `systemDirectory`**: By the full path of the system directory, and with that directory alone for the library's own dependencies: a dbghelp.dll or dbgcore.dll that sits next to the game's exe must never be the one that runs.
- **`PrepareTheDump`, `SystemVersionDll();`**: Writing the dump reads each module's version resource through version.dll's functions. In the game that name is the Injector itself, whose stubs look up `SystemVersionDll()` on first use: prime `SystemVersionDll()` directly now, so the crash does not call `LoadLibraryExW`. Kept loaded, as the Injector's own stubs keep it.
- **`PrepareCrashDump`, `g_dumpRequested = CreateEventW(nullptr, TRUE, FALSE, nullptr);`**: The helper thread is started whether or not a dump can be written: it also writes the crash line, which the crashing thread may have no stack for.
- **`PrepareCrashDump`, `Logger::Instance().Flush();`**: The C runtime allocates the log file's buffer at the first write to it. Make that write now, so that the crash line does not have to allocate while the process is crashing (and the lines still buffered reach the file).
- **`RemoveCrashReporter`, `top`**: The only way to look at the top filter is to replace it: put the filter that was here before the reporter in its place, and the one that was on top back if it was not the reporter.
- **`RemoveCrashReporter`, `if (g_crashSeen.load()) return;`**: Not while a crash is being handled: the process is going down then, and the helper may be writing the dump.
- **`RemoveCrashReporter`, `g_helperReady = false;`**: The helper thread runs code of the Injector, which is about to be unloaded. It cannot be asked to leave: it would have to run on until it exits, and a thread's exit needs the loader lock, which this thread holds (DllMain). It is parked in a wait that holds nothing, so it is ended where it stands.
- **`RemoveCrashReporter`, `g_dumpState = DumpState::NotPrepared;`**: The system's dbghelp.dll stays loaded: the library loader is locked here too.

### `src/core/crash_reporter.h`

- **`kCrashDumpFlags`**: What the Injector's crash dump holds, as the MINIDUMP_TYPE flags it passes to MiniDumpWriteDump: thread information (0x1000), the memory the stacks point at (0x40) and the unloaded modules (0x20), on top of the stacks and the module list every minidump holds. crash_reporter.cpp checks that the number is that combination. The Installer asks Windows for the same contents in the game's own dump (CustomDumpFlags in installer/fs25_vr.iss), so both dumps of one crash can be read the same way; tests/test_crash_dump_consistency.cpp checks that it, the README and ADR-0039 agree.
- **`InstallCrashReporter`**: The Injector's crash reporter: the process's top-level unhandled exception filter, so it sees crashes only, never the exceptions the game handles. When the process crashes it finishes fs25_vr.log (flushing the lines still buffered), adds one line that names the crash, writes a small minidump next to the log, then passes the crash on to the filter that was installed before it, so the game's own crash handling and Windows Error Reporting run as before. With no earlier filter it lets Windows continue its own handling.

  It runs once per process. A second crash, or a crash inside the reporter, goes straight to the earlier filter. Inside a crashing process it does nothing risky: it loads no library, creates no thread and never waits for a lock, the log's lock included (the crashed thread may hold it, and then the line is skipped). The crashing thread itself does next to nothing, because after a stack overflow it has next to no stack: it hands the exception to a helper thread that was prepared ahead of time (PrepareCrashDump) and waits for it with a time limit, so that a stuck helper cannot hang the game's exit. The helper writes the crash line and then the dump, with the system's dbghelp.dll. Before the first DXGI factory call there is no helper: the crashing thread writes the line itself when its stack has room for it, and leaves it out when it has not.

- **`InstallCrashReporter`**: Puts the reporter on top of the unhandled exception filter chain. Loader-lock safe: loads no library and installs no hook, so DllMain may call it (StartInjector does). Calling it again is harmless: when another filter was installed on top meanwhile, that one becomes the filter to pass crashes on to.
- **`ReinstallCrashReporter`**: Puts the reporter on top again after another module took the place (the game may install a filter of its own after the Injector started), but only when InstallCrashReporter ran: the tests that drive the DXGI proxy in-process must not leave a filter behind. A different filter found on top becomes the first one to pass crashes on to; the ones from before follow it. When none is on top, the earlier ones are forgotten too: whoever removed the reporter removed the chain with it, and a filter taken away may sit in a module that is gone, so the crash goes to Windows' own handling instead.

  A filter on top that called the one it replaced (the reporter) from its own makes a loop with the reporter, which passes the crash on to it: the reporter, reached again from inside a filter, passes the crash on to the next older one, so that each is called once. The Injector calls this at the first DXGI factory call (PrepareCrashDump) and when it attaches to the game's swap chain, which it hooks in Active VR Mode only.

- **`PrepareCrashDump`**: Prepares the crash dump, once per process, at the first DXGI factory call, where the Injector's other deferred start-up runs: puts the reporter on top again, loads the system's dbghelp.dll by its full path and starts the helper thread that waits for a crash. Never from DllMain (it loads a library) and never from the filter itself. Does nothing when InstallCrashReporter did not run. A crash before this point gets the crash line only, which says why.
- **`RemoveCrashReporter`**: Takes the reporter off again, but only when it is still the top filter: a filter installed after it may have kept a pointer to it. Does nothing when it was never installed. Also ends the dump helper thread, since its code goes away with the Injector. For the Injector's unload with FreeLibrary; at process termination the reporter is left in place.

### `src/core/detour.cpp`

- **`HookState`**: Every target MinHook holds a hook for on our behalf. Live: a FunctionDetour owns it. Orphaned: its MH_RemoveHook failed, so MinHook still holds a disabled hook that nothing owns; only these may be reclaimed by a later Install.
- **`ResolveThunks`**: Resolve through incremental link table (ILT) jmp thunks (E9 rel32) and 6-byte indirect import thunks (FF 25 disp32) to the real function body.
- **`ResolveThunks`, `kMaxHops`**: Real thunk chains are one or two hops; the cap stops a jmp cycle from hanging us.
- **`ResolveThunks`, `if (IsHooked(function)) return function;`**: A target we already hooked starts with our own patch jmp; following it would hook the detour itself. Stop here so MinHook reports the target as taken.
- **`FunctionDetour::~FunctionDetour`, `if (IsProcessTerminating()) return;`**: At termination the patch stays and the trampoline is never freed (see IsProcessTerminating).
- **`FunctionDetour::Install`, `LogWarn("FunctionDetour: Removing a stale hook left by a failed uninstall.");`**: A previous Uninstall could not remove its hook, so MinHook still holds a stale one for this target that nothing owns. Drop it and retry.
- **`FunctionDetour::Install`, `m_original.store(original, std::memory_order_release);`**: Publish the atomic trampoline and `m_installed` before the patch goes live: a thread entering the detour the moment other threads resume must already see GetOriginal() and IsInstalled().
- **`FunctionDetour::Install`, `status = MH_EnableHook(targetFunction);`**: MinHook suspends all other threads while patching the target.
- **`FunctionDetour::Uninstall`, `LogError(std::string("FunctionDetour: MH_DisableHook failed: ") + MH_StatusTo...`**: The patch is still live, so callers still need the trampoline: stay installed. Once disabled, `m_installed` and `m_original` are cleared before `MH_RemoveHook` frees the trampoline.
- **`FunctionDetour::Uninstall`, `SetHookState(m_target, HookState::Orphaned);`**: The hook is disabled but MinHook still holds it; let a later Install reclaim it.

### `src/core/detour.h`

- **`FunctionDetour`**: Inline function hook backed by MinHook. MinHook is initialized exactly once per process; installing and removing hooks suspends the other threads while the target is patched.
- **`FunctionDetour`, `m_installed`**: Read lock-free from hook entry points on game threads while Install/Uninstall write it.

### `src/core/dll_lifecycle.cpp`

- **`OnProcessAttach`, `LogInfo("Farming Simulator 25 VR Injector " + std::string(kInjectorVersion) +...`**: The first line of fs25_vr.log: it names the version, so a log alone says which Injector the player runs.
- **`OnProcessDetach`, `s_processTerminating = true;`**: Set before returning (and before any logging/flushing): the CRT runs our static destructors right after DllMain, and other killed threads may already hold `Logger::m_mutex`.
- **`OnProcessDetach`, `NgxInterceptor::DetachLiveHooks();`**: FreeLibrary: safe to unwind our own hooks. Unwind whichever interceptor owns the live detours, not the newest runtime's, so no hook is left pointing into the unloaded DLL.

### `src/core/dll_lifecycle.h`

- **`OnProcessAttach`**: Work performed from DllMain. Must stay loader-lock safe: no LoadLibrary, no hook installation. NGX module-load hooks are installed lazily on the first DXGI factory call instead. The one exception is StartInjector (injector_startup.h), which DllMain calls next to hook the system's DXGI factory exports.
- **`OnProcessDetach`**: lpReserved is non-null when the process is terminating. In that case other threads are already gone (and may hold `Logger::m_mutex`), so `s_processTerminating` is set first and hooks, trampolines, and logger locking/flushing are skipped.
- **`IsProcessTerminating`**: True once OnProcessDetach has seen process termination. The CRT still runs static destructors after DllMain returns, so every destructor and logger path that removes hooks, calls `FreeLibrary`, invokes COM `IUnknown::Release()`, or takes locks checks this and returns or detaches early: a killed thread may own the lock, be executing inside a patched prologue, or leave driver COM state torn down.

### `src/core/injector_runtime.cpp`

- **`Live`**: Function-local so it is constructed before, and destroyed after, the first runtime.
- **`Unregister`**: Removes a destroyed runtime. When blocking is not allowed (process termination, where a killed thread may hold the lock) the entry is left behind if the lock is taken: no hook runs after that.
- **`ApplySavedMenuDistance`**: The saved Menu Distance, so the main menu is shown at it before the Companion Mod runs. A value the Companion Mod would refuse (out of range) is not used, as the mod does not use it either.
- **`InjectorRuntime::InjectorRuntime`, `if (m_deps.vrBridge) {`**: Every subsystem the caller did not supply is created here, so the runtime owns the whole graph. A supplied VRBridge brings its collaborators: adopt them, so the bridge and the hooks share one OpenXR manager and one NGX interceptor.
- **`InjectorRuntime::InjectorRuntime`, `m_proxy->SetOwnedByRuntime(true);`**: Detours may be installed only from here on, now that hooks can find this runtime.
- **`InjectorRuntime::~InjectorRuntime`, `if (IsProcessTerminating()) {`**: A thread killed at termination may hold the detour, registry or NGX locks: take none of them.
- **`InjectorRuntime::~InjectorRuntime`, `m_proxy->SetOwnedByRuntime(false);`**: Remove this runtime's detours while hooks can still find it, so none fires without its original.
- **`InjectorRuntime::~InjectorRuntime`, `m_deps.ngxInterceptor->SetFidelityFxInterceptor(nullptr);`**: Do not leave the NGX interceptor pointing at objects we are about to free.

### `src/core/injector_runtime.h`

- **`InjectorRuntime`**: Composition root: constructs and wires every Injector subsystem.

  Production uses Instance(), created on the first call into a DXGI export. Tests construct their own runtime with fakes. Any subsystem left null in Dependencies is created here, so the runtime is the only place subsystems are built and no subsystem has a singleton accessor of its own. Inject every subsystem a test observes; the others are fresh, real ones wired to the ones supplied. The NGX interceptor is wired to the runtime's OpenXR manager, VRBridge and DXGI proxy, and reaches no other subsystem.

  Hook entry points are plain function pointers with no context argument, so they find their subsystem through the registry of live runtimes (ForHook). The DXGI, Win32 and Luau subsystems install detours only while a live runtime owns them, and a runtime removes those detours before it leaves the registry, so a hook always finds the subsystem that holds its original function.

- **`Dependencies`, `companionSettingsFile`**: The Companion Mod's settings.xml, read once on construction for the settings the main menu needs before a savegame has loaded (the Menu Distance). Empty: nothing is read. Instance() passes the player's file.
- **`Dependencies`, `hostModule`**: The host module (an HMODULE) whose .rdata and .text sections the Luau interceptor scans for the game's head-tracking query. Null: nothing is scanned and the interceptor stands by, which is what a runtime built by a test gets. Instance() passes the process's executable.
- **`Dependencies`, `injectorVersion`**: The Injector's release version, given to the VRBridge the runtime creates itself (a supplied VRBridge brings its own). The build's version unless a test supplies another.
- **`InjectorRuntime`, `Current`**: The most recently constructed runtime that is still alive, whatever order runtimes are destroyed in; null when none is alive. Never falls back to Instance(). Lock-free.
- **`InjectorRuntime`, `template <typename Pred>`**: The runtime a hook entry point acts for: the newest live runtime whose subsystem installed the hook (installed(runtime) is true), else Current(). installed may run under the registry lock, so it must only read detour state.
- **`ForHook`, `current`**: Lock-free fast path for the hot hooks (Present, GetClientRect, ...): Current() is the newest live runtime, so when it installed the hook it is the answer. Production has one runtime.

### `src/core/injector_startup.cpp`

- **`kDllNotificationReasonLoaded`**: The DLL load notification of ntdll (documented, but not declared in the Windows SDK headers).
- **`RuntimeFactoryEntry`**: Passes each caught factory call to the process's InjectorRuntime, which is created by the first one. The first call also prepares the crash dump, before the runtime starts: a crash while it initializes OpenXR is the likeliest one. Here and not in the proxy, since this runs once per process however many proxies the tests create.
- **`HookSystemDxgi`**: Hooks the system's dxgi.dll, kept loaded from then on so the patched code never goes away under the hooks.
- **`OnDllNotification`**: Runs with the loader lock held, after the module is mapped and before its own initialization.
- **`IsFs25VrModule`, `kKey`**: A String entry of the version resource: header, the key with its terminating null, padding to 32 bits, then the value. The resource itself starts 32-bit aligned.
- **`StartInjector`, `InstallCrashReporter();`**: From DllMain, where it is loader-lock safe: it loads no library and installs no hook.
- **`StartInjector`, `registerNotification`**: Not loaded yet: the game's dxgi.dll is another one (ReShade's, for one), which loads the system's when the game first calls it.

### `src/core/injector_startup.h`

- **`StartInjector`**: Called from the Injector's DllMain on process attach (ADR-0038). The game loads the Injector as its version.dll, before it creates any DXGI factory, so the factory exports of the system's dxgi.dll are hooked here: right away when that dxgi.dll is already loaded, else from a DLL load notification the moment it loads (a dxgi.dll in the game folder, such as ReShade's, loads it later). Every caught factory call goes to InjectorRuntime::Instance(). Installing these three hooks is the one exception to "no hooks from DllMain" (see dll_lifecycle.h). Puts the crash reporter on the process's unhandled exception filter first (crash_reporter.h), which loads no library and installs no hook either. Does neither when an FS25 VR Injector of an earlier version, which was the game's dxgi.dll, is loaded too: both would hook the game.
- **`StopInjector`**: Called from DllMain when the Injector is unloaded with FreeLibrary: removes the hooks and the load notification, and takes the crash reporter off again if it is still the top filter. Not called at process termination, where hooks and the crash reporter are left as they are.
- **`IsFs25VrModule`**: True when the module's version resource gives "FS25 VR" as ProductName. Reads the resource directly, without version.dll (which is the Injector itself). Works on a module loaded as a data file too.
- **`FindEarlierInjector`**: The game's dxgi.dll when it is not the system's and is an FS25 VR Injector of an earlier version, else null.

### `src/core/injector_version.h`

- **`kInjectorVersion`**: The Injector's release version as built, for example "1.2.0.0": the \<version> of the Companion Mod's modDesc.xml, the same number the build writes into the Injector's version resource. The Injector and the Companion Mod are released together, so there is one number to bump. A constant rather than something found at run time, because the first log line names it from DllMain, before any subsystem exists.

### `src/core/logger.cpp`

- **`Logger::SetLogFile`, `if (!MoveFileExW(m_filepath.c_str(), BackupPathFor(m_filepath).c_str(), MOVEF...`**: A single replacing move: if the old log is still held open (e.g. by a hung previous session) this fails without touching either file, and we append rather than lose it.
- **`Logger::Log`, `if (level >= LogLevel::Info) {`**: On disk at once, so a game that hangs leaves a complete log. Debug lines stay buffered: debug logging would otherwise cost a file write per line.
- **`Logger::TryLogWithoutWaiting`, `now`**: The layout of the lines Log writes, put together by hand: snprintf and localtime_s need more stack than a thread that has just overflowed its stack has left, and the CRT's time functions take a lock of their own.

### `src/core/logger.h`

- **`Logger`, `DefaultLogPath`**: Absolute path of fs25_vr.log in the directory of the module containing this code (the Injector DLL in the game, the test executable under test).
- **`Logger`, `SetLogFile`**: Starts a new session at filepath: any existing file is moved to the backup path (replacing the previous backup) and a fresh file is created. If the existing file can't be moved (still held open elsewhere), it and the backup are left intact and the new session is appended to it.
- **`Logger`, `TryLogWithoutWaiting`**: For the crash reporter, which runs in a process that may be broken. Appends one line to the log and flushes it, but only if the log is open and its lock is free right now: it never waits for the lock, because the crashed thread may hold it. Allocates nothing and uses next to no stack. Returns whether the line was written.
- **`Logger`, `m_emergencyLine`**: The line TryLogWithoutWaiting puts together, under the lock, so that it needs no heap and no stack.

### `src/core/perf_trace.cpp`

- **`PerfTrace::PerfTrace`**: Info lines reach the file as they are logged, so the trace can be read while the game runs.
- **`PerfTrace::Output::Write`**: The sink runs after the lock is released: it writes to the log file, and the motion vector pass's render thread would otherwise wait on that I/O to add a GPU sample.

### `src/core/perf_trace.h`

- **`QpcFrequency`**: The QueryPerformanceCounter frequency in ticks per second. It is fixed at boot, so it is queried once.
- **`QpcTicksToNanoseconds`**: The QueryPerformanceCounter ticks in nanoseconds, without overflowing for any counter. The one conversion the perf trace and the OpenXR timing share.
- **`PerfTrace`**: Frame timing for a few seconds (`vrPerfTrace`), logged to fs25_vr.log as one line per second and a summary over the whole trace. Off, every call is one relaxed atomic load, so the hooks stay in place. Calls come from the Lua/Present thread (ADR-0018) and the render thread (the motion vector pass).
- **`PerfTrace`, `SetGpuTimestampFrequency`**: The presenting queue's timestamp frequency, which the motion vector pass reads its timestamps with (it records on the game's command list and never sees the queue). 0 until the first eye copy is timed.
- **`PerfTrace`, `Output`**: Lines built while the lock is held, written to the sink after it is released.

### `src/core/texture_snapshot.h`

- **`DecodePixelToBgr8`**: Diagnostics (`vrEyeSnapshot`): what the Injector hands an eye, written to a picture file that can be looked at. The texture is copied to the CPU on the given queue and the call blocks until the GPU has done the copy, so it is for one-off use only, never every frame.

### `src/core/thread_trace.cpp`

- **`ThreadTrace::Record`, `if (m_lastThread.load(std::memory_order_relaxed) == threadId) return false;`**: Steady state is a plain load; only a different thread pays for the exchange.

### `src/core/thread_trace.h`

- **`ThreadTrace`**: Records which thread reaches a call site, to establish and then guard the Lua/Present thread model (ADR-0018). Logs the first thread seen and any later change, so a steady-state frame loop adds no log lines. A change means the single-thread model no longer holds, so it is logged as a warning; after kMaxChangeLogs changes the site goes quiet, so threads that alternate every frame can't flood the log from the present path.

### `src/dllmain.cpp`

- **`DllMain`, `if (lpReserved == nullptr) fs25vr::StopInjector();`**: Only on FreeLibrary: at process termination other threads may still be inside the hooks.

### `src/injector.rc.in`

- **the file**: Version resource of the Injector (version.dll). configure_file() fills in the version from the Companion Mod's modDesc.xml, so the Injector and the release carry the same number.

  The Installer (installer/fs25_vr.iss) recognises an Injector of ours, to overwrite it without treating it as a foreign version.dll, and to remove an earlier one that was the game's dxgi.dll, by ProductName "FS25 VR" below. The Injector reads it too, to find an earlier Injector still loaded as dxgi.dll (IsFs25VrModule). Do not change it without changing IsOurInjector in the Installer script and IsFs25VrModule in src/core/injector_startup.cpp too.

### `src/proxy/version_exports.cpp`

- **the file**: The exports of the system's version.dll. The Injector is loaded as the game's version.dll (ADR-0038), so every module in the game process that imports version.dll binds to these. Each one calls the same function of the system's version.dll, which is loaded from the system directory on first use (never from DllMain). VerLanguageNameA and VerLanguageNameW are forwarded to kernel32 in version_exports.def, as the system's version.dll forwards them.

  The Injector must never import version.dll itself: that import would bind to the Injector. scripts/check_injector_imports.ps1 checks it.

- **`using GenericFourArgFn = INT_PTR(WINAPI*)(INT_PTR, INT_PTR, INT_PTR, INT_PTR);`**: GetFileVersionInfoByHandle is not in the Windows SDK. Its integer and pointer arguments (at most four) arrive in the first four argument registers, so passing on four of them passes on whatever the caller gave.
- **`FS25VR_VERSION_STUB`**: A stub that cannot find the system function returns `fail`, as the system function reports an error (FALSE or 0 with ERROR_PROC_NOT_FOUND; VerFindFile and VerInstallFile have their own error bits). The .def file exports each Fs25Vr_\<name> as \<name>.

### `tests/injector_binary.h`

- **`InjectorPathA`**: CMake writes the path with forward slashes; Windows paths take backslashes.
- **`InjectorLogPath`**: The log the Injector writes when it is loaded from the build folder: next to the Injector, under the name the test process's own logger uses by default too.
- **`LetTheInjectorHaveItsLog`**: For a test that loads the Injector and reads the log it writes. The Injector writes the same fs25_vr.log as the test process's own logger, which holds it open: this lets go of the file and clears the way, as a game start finds the folder, and puts the test process's logger back on its default file when the test is done.
- **`TopUnhandledExceptionFilter`**: The process's top-level unhandled exception filter. The only way to read it is to replace it, so it is put back.
- **`PreviousFilter`**: Two stand-in filters that differ in what they do, so that the linker cannot fold them into one function.

### `tests/test_camera_records.cpp`

- **`CameraRecordStoreTest.RelativeTransformsKeepMillimetrePrecisionAtLargeMapCoordinates`, `base`**: A float cannot hold 4,000,000.064 to better than about 0.25 m; double keeps it to nanometres.

### `tests/test_crash_dump_consistency.cpp`

- **the file**: The crash dump numbers and commands are written down in several places that nothing else ties together: the Injector, the Installer script, the README, the README.txt that scripts/package_release.ps1 writes into the bundle, ADR-0039 and the Installer test. These tests read those files as text, so that changing one and forgetting another fails here.
- **`BundledReadmeText`**: The README.txt that package_release.ps1 writes: the text of its here-string, in which a dollar sign is escaped with a backtick so that PowerShell does not expand it.

### `tests/test_crash_reporter.cpp`

- **the file**: The Injector's crash reporter, seen the way a crashed game is: a child process loads the built Injector (version.dll) by its full path, as the game does, and faults. The parent then reads what the child left next to the Injector.

  The child is an EXPECT_EXIT death test, the pattern of test_dxgi_proxy.cpp. Two things are particular to a crash:

  - The fault is raised on a thread the child starts itself. GoogleTest catches the structured exceptions of the thread that runs the test, so a fault there never reaches the process's unhandled exception filter.
  - The child shows no error dialog: CI is unattended. The "game's" crash handling in the child is a filter that ends the process with the exception's code, which is what Windows Error Reporting does after its own work.

- **`g_injector`**: The log's lock cannot be taken from outside the Injector, but the Injector's Logger calls OutputDebugStringA while it holds it. Taking the place of that function (a hook in the child, with a FunctionDetour that never calls the original), the child faults inside it, on a call from the Injector: the crashed thread then holds the Injector's log lock, as a thread that crashes in the middle of writing a log line would.
- **`EndTheChildIfItHangs`**: A crash handler that waits for something that never comes would hang the test. Ends the child instead, with a code that is not the exception's, so the test fails with a message.
- **`GamesFilter`**: Stands for the filter the game or Windows already has when the Injector loads. It reports on stderr what the Injector's filter handed it, then ends the process the way Windows Error Reporting does: with the exception's code and without unloading any DLL (an unload would flush the Injector's log and hide a missing flush).
- **`GamesFilter`, `if (info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {`**: After a stack overflow the thread has a few kilobytes of stack left, which the Injector's work has partly used: stdio would run out of it. The process ends at once, as the filters of the game and of Windows do.
- **`MarkerText`**: Two regions of text for the dump to find or not find: one that the faulting thread's stack points at (as a local variable in its frames does), and one that only a global points at. A dump has the memory the stacks point at, and not the whole process. The text is put together at run time, so that it is not in the executable's image either, and only ever in the region itself: a copy on the heap (a std::string) would leave the text next to the heap blocks that a stack does point at, and the dump includes a kilobyte or so around each of those.
- **`PutTextInARegionOnNoStack`**: On a thread of its own that ends, so that no stack of the child still holds the region's address or its text.
- **`g_stackToLeave`**: How much stack the filter finds after a stack overflow depends on the operating system and the processor: the exception dispatcher saves the thread's context on the stack first, and that is a few kilobytes on a processor with wide vector registers, which leaves the filter next to nothing. The child takes the guesswork out: it runs down its own stack until exactly this much is left and raises the overflow there, so the filter has the same margin on every machine (the dispatcher's share comes out of it).
- **`StandInForTheDumpWriter`**: The child loads the system's dbghelp.dll itself for this, so a test with a stand-in cannot tell where the Injector loads it.
- **`g_replacedByTheLaterFilter`**: A filter that another module installs after the Injector's. It may call the filter it replaced, which is the Injector's (filters that chain do), or leave it alone and end the process itself.
- **`g_loadLibraryEx`**: The Injector loads the system's dbghelp.dll with LoadLibraryExW. The child can make that fail, as it would on a machine where the library is missing or cannot be loaded.
- **`ChildSetup`, `createsAFactory`**: The game creates its DXGI factory at start-up. The Injector prepares its crash dump at the first factory call, so a child that never creates one stands for a crash before that point.
- **`DecoyDbghelpPath`**: A dbghelp.dll next to the test executable, where a load by name would find it first.
- **`Minidump`**: Just enough of the minidump format to read what a dump holds. Not dbghelp.dll: loading it into this process would hide whether the Injector loads it (and the tests that watch the loaded modules would depend on it).
- **`CrashReporterTest`, `DumpPath`**: The dump has a fixed name (the Installer's uninstall removes the file by it), so the next crash replaces it.
- **`CrashReporterTest.ACrashBeforeAnyFactoryCallLeavesTheCrashLineWithTheReasonAndNoDump`**: The dump needs the system's dbghelp.dll and a helper thread, prepared at the first DXGI factory call. A crash before that leaves the crash line, which says why there is no dump.
- **`CrashReporterTest.ACrashInsideTheReporterAddsNoSecondLineOrDumpAndStillEndsTheProcess`**: The previous filter is called from inside the Injector's, so a crash in it is a crash inside the reporter. What the operating system does with a fault inside a top-level filter differs: some builds drop it, others hand it to the unhandled exception filter again, which calls the Injector's filter a second time on that thread (and, when it passes the crash on, the child's filter a second time). Both end the process, and a player finds the same afterwards: the earlier filter ran, there is one crash line and one dump, and the process ended with the first crash's code.
- **`CrashReporterTest.AStackOverflowBeforeTheFirstFactoryCallStillEndsAsTheOverflow`**: Before the first DXGI factory call there is no helper thread, and the crashing thread has no stack for the line. The crash must still end as the stack overflow it was, not as a fault of the Injector's filter in the little stack left.
- **`CrashReporterTest.ASecondFaultWritesNoSecondDump`**: One dump per process: the first crash is the one that matters, and the second one is usually its consequence.
- **`CrashReporterTest.TheNextCrashingProcessReplacesTheDump`**: The file has a fixed name, so a dump never piles up next to the log, and the report is about the latest crash.
- **`AfterTheFirstFactoryWithALaterFilter`**: Installing the filter again The game may install an unhandled exception filter of its own after the Injector started. Then it, and not the Injector, gets the crash, and neither crash line nor dump is written. The Injector installs its filter again at the first DXGI factory call; what it finds on top becomes the filter it passes the crash on to.
- **`CrashReporterTest.AFilterThatChainedToTheReporterDoesNotMakeAChainThatLoops`**: A filter that chains calls the one it replaced, which is the Injector's. The Injector installs again over it and passes the crash on to it, so each calls the other: the chain must end, and no filter may be left out of it.
- **`AfterTheFirstFactoryWithAnUnloadedModule`**: What the dump holds The contents the Installer asks Windows for in the game's own dump (installer/fs25_vr.iss): every thread's stack, the modules (the unloaded ones too), the exception and the thread information, and the memory the stacks point at, but not the whole process, so that a dump is small enough to attach to an issue once zipped.
- **`CrashReporterTest.DbghelpIsLoadedAtTheFirstFactoryCallAndNotWhenTheInjectorLoads`**: dbghelp.dll loads another library, and a library cannot be loaded from DllMain or from a crashing thread: it is loaded at the first DXGI factory call, where the rest of the deferred start-up runs.
- **`CrashReporterTest.ADumpThatCannotBeCreatedIsToldInALineAfterTheCrashLine`**: A dump that cannot be created (the folder is read-only, the file is open in another program) is announced in the crash line, which comes first, and then told in a line of its own.
- **`CrashReporterTest.AStuckDumpHelperDoesNotHangTheExitAndTheLogSaysThereIsNoDump`**: A helper that never finishes (a thread it needs is suspended, a lock is held) must not hang the game's exit: the filter gives up after its time limit, says so in the log, and passes the crash on.
- **`CrashWithLittleStackLeftTest`**: After a stack overflow the filter has next to no stack, and what it finds depends on the operating system and the processor. The Injector's filter must not fault in it, whatever it finds: a fault in the filter would hide the crash behind a second one. The child runs down its stack to a given margin, so that every machine tests the same.
- **`CrashWithLittleStackLeftTest.ACrashBeforeTheFirstFactoryStillEndsAsTheOverflow`**: Before the first factory call there is no helper thread, and the crashing thread writes the line itself. With little room it writes nothing, but it must end the crash as the overflow, not as a fault of its own; with room (the margin is well over what the line needs) the line is there.
- **`CrashReporterTest.AFaultWhileTheCrashedThreadHoldsTheLogsLockSkipsTheLineAndStillLeavesTheDumpAndPassesTheCrashOn`**: The crashed thread may hold the log's lock (it crashed while writing a line), and then the filter must not wait for it: the line is skipped, the dump is still written, and the crash is still passed on. The thread that faults makes the first DXGI factory call itself, which prepares the dump before the Injector logs on that thread, so the dump is ready when the fault comes.
- **`CrashReporterTest.ACrashInPassThroughModeLeavesTheSameCrashLineAndDump`**: A player without a headset (no OpenXR runtime) runs in Pass-Through Mode, and so does every child here: the Injector logs that it forwards all calls, which is how the test knows it is the mode the child is in. The crash reporter is prepared at the first DXGI factory call, before the Injector decides on a mode, so the crash leaves the same files.
- **`CrashReporterInstallTest.InstallingTheReporterLoadsNoLibraryAndInstallsNoHook`**: DllMain installs the reporter while the loader lock is held, where loading a library or hooking a function can deadlock.
- **`CrashReporterInstallTest.InstallingTheReporterLoadsNoLibraryAndInstallsNoHook`, `Logger::Instance();`**: The Injector's first log line, written before DllMain installs the reporter, has created the log by then. The CRT loads a library of its own the first time the log is created (kernel.appcore.dll), which is not the install's doing.
- **`CrashReporterInstallTest.AttachingToTheGamesSwapChainPutsTheReporterOnTopAgain`**: The Injector attaches to the game's swap chain in Active VR Mode (the swap chain is only hooked there). A filter that took the top place since the first factory call is found then, and the reporter goes on top of it again.
- **`CrashReporterInstallTest.AttachingToTheGamesSwapChainPutsTheReporterOnTopAgain`, `SetUnhandledExceptionFilter(&LaterFilter);`**: the game installs a filter of its own
- **`CrashReporterInstallTest.AReinstallThatFindsNoFilterOnTopForgetsTheOnesFromBefore`, `EXPECT_EQ(TopUnhandledExceptionFilter(), nullptr);`**: Put back as nothing: Windows' own handling. The filter from before may sit in a module that is gone.

### `tests/test_detour.cpp`

- **`__declspec`**: We use \__declspec(noinline) so the compiler generates standard function prologues
- **`DetourTest.SafelyRejectsShortFunctionsContainingRet`, `page`**: Simulate a 3-byte stub: xor eax, eax (31 C0); ret (C3) Surrounding bytes are non-padding (push rbp) so MinHook cannot fall back to a hot-patch hook.

### `tests/test_dll_lifecycle.cpp`

- **`ImmediateSystemDxgi`**: Returns from factory calls immediately, so a racing caller is not hidden behind real DXGI latency.
- **`BlockingOpenXrDispatch`**: Holds OpenXR initialization at its first runtime call until released, so a test can keep the first factory call inside EnsureVrInitialized while a second one races it.
- **`DllLifecycleTest`, `CreateFactoryThroughNewProxy`**: The proxy installs the NGX hooks on the interceptor it is given; these tests observe m_ngx.
- **`DllLifecycleTest.ProcessDetachUnwindsTheHookOwnerNotTheNewestRuntime`, `deps`**: A newer runtime with an interceptor of its own must not hide the hooks m_ngx owns.
- **`DllLifecycleTest.DestructorsRunAfterTerminationDetachLeaveHooksInPlace`, `ASSERT_EQ(MH_RemoveHook(patched), MH_OK);`**: The destructor deliberately left the hook. Remove it through MinHook so its hook table forgets the target too, or a later Install (e.g. --gtest_repeat) fails.
- **`DllLifecycleTest.ConcurrentFirstFactoryCallsReturnOnlyAfterHooksInstalled`, `kRounds`**: The race is timing-dependent; several fresh rounds make a regression show up reliably.
- **`DllLifecycleTest.ConcurrentFactoryCallWaitsForOpenXrInitialization`, `std::this_thread::sleep_for(std::chrono::milliseconds(200));`**: The second caller decides from the OpenXR state whether to hook its factory, so it must not return while that state is still being established.

### `tests/test_injector_binary.cpp`

- **`InjectorBinaryTest.HooksTheSystemsDxgiWhileLoadedAndAFactoryIsStillCreated`, `create1`**: Through the hook into the Injector's own runtime, which goes to Pass-Through Mode with no OpenXR runtime (see test_process_isolation.cpp), and on to the system.
- **`InjectorCrashFilterTest.TheInjectorPreparesItsCrashDumpAgainWhenItIsLoadedAgain`**: The dump is prepared with a thread that is ended when the Injector unloads, since its code goes with it. Tests and tools that load the Injector again must find it working again.

### `tests/test_logger.cpp`

- **`LoggerTest.LogStillHeldOpenIsAppendedToAndBackupIsKept`, `held`**: Hold the log the way a still-running previous session would: shared for read/write but not delete, so it can't be moved to the backup path.
- **`LoggerTest.AnInfoLineIsInTheFileBeforeAnyFlush`**: A game that hangs instead of crashing never flushes the log again, so whatever reached it at Info or above must be on disk already.
- **`LoggerTest.ADebugLineDoesNotFlushTheFile`**: Debug logging writes many lines a frame; a file write for each would cost the frame rate it is meant to diagnose.
- **`LoggerTest.ALineWrittenWithoutWaitingHasTheLayoutOfALineFromLog`**: The crash reporter puts its line together by hand (TryLogWithoutWaiting) because the Log function needs more stack and heap than a crashing process may have. Whoever reads fs25_vr.log, a person or a script, finds one layout, so the two must not drift apart: a timestamp, the level in brackets, then the message.

### `tests/test_perf_trace.cpp`

- **`PerfTraceTest.WritesToTheSinkOutsideItsLock`**: A sink that waits for another thread's AddSample, as the motion vector pass's render thread would while a line is being written. If the lock were still held while writing, that AddSample could not finish until the sink returned, so the sink would time out waiting for it.
- **`PerfTraceTest.NegativeDurationsCountAsZero`, `trace.Start(10);`**: The GPU queue lag can come out a little negative from calibration jitter

### `tests/test_process_isolation.cpp`

- **`NoRealOpenXrRuntimeEnvironment`**: The tests build the production Injector runtime, and some load the freshly built Injector (version.dll), whose hooks send the process's DXGI factory calls to it, so a DXGI factory call reaches the real OpenXR loader. On a machine with a VR runtime installed that starts the runtime (SteamVR launches vrserver) and the test run hangs. Pointing the loader's runtime override at a manifest that does not exist makes every such call enter Pass-Through Mode instead. Tests that need an Active VR Mode inject the fake OpenXR runtime.
- **`[[maybe_unused]] ::testing::Environment* const g_noRealOpenXrRuntime =`**: Registered during static initialization so it runs in every test process, whether ctest starts one per test or one process runs the whole suite, and before the first test body.
- **`TestProcessIsolationTest.DxgiFactoryCallThroughProductionRuntimeEntersPassThrough`, `runtime`**: A runtime of its own, built from production parts, so the hooks the factory call installs are unwound when it goes out of scope instead of lingering for the tests that run after this one.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 181 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
