# Decisions behind VRBridge and the Luau State

Moved out of the code under [ADR-0041](0041-code-carries-no-comments.md). Related: [ADR-0002](0002-in-process-luau-state-interception.md), [ADR-0018](0018-lua-and-present-share-one-thread.md).

## Context and Decision
This record holds the non-obvious decisions behind VRBridge and the Luau interception: how the state is captured, what each function may allocate, and how the game's garbage collector is kept out of the Injector's calls. They used to be comments next to the code. Each entry names the symbol or the line its comment stood at, so that a search for the symbol finds it. The wording is the comment's own: "below", "above" and "this file" mean the file in the heading, and a bare ticket number (`#164`) is a number of the old issue tracker.

### `src/luau/luau_interceptor.cpp`

- **`static_assert(LUA_VECTOR_SIZE == 3, "TValue layout differs from the engine's");`**: The Injector calls its own Luau VM on the game's lua_State, so its struct layouts must match the engine's. The host's layout cannot be read from the binary; these pin the Injector's side to stock 0.662 defaults, which the in-game check (ADR-0002 amendment) confirmed the engine uses.
- **`LuauInterceptor::AttachToHostSections`, `if (!HostEmbedsMatchingLuau(rdataBase, rdataSize)) {`**: The Injector calls its own statically linked Luau VM on the game's lua_State, which is only safe when the engine embeds the same release. Refuse before any detour can reach VRBridge injection.

### `src/luau/luau_interceptor.h`

- **`kExpectedLuauVersion`**: The Luau release the Injector links, and which FS25 (GIANTS Engine 10) embeds. See the ADR-0002 amendment. Must equal GIT_TAG in CMakeLists.txt (FS25VR_LUAU_TAG); a test enforces it.
- **`kHostLuauFingerprint`**: Fast-flag names the engine keeps as NUL-terminated strings in .rdata. Finding all of them identifies the embedded Luau release, for both halves of it:

  - Ast/Compiler flags pin the front end to 0.662 exactly (0.660-0.661 lack LuauFixDoBlockEndLocation, 0.663 and later drop LuauCompileMathLerp and LuauAllowFragmentParsing).
  - VM flags pin the VM, which is what the Injector's API calls touch, to 0.661-0.662 (0.660 and earlier lack LuauLibWhereErrorAutoreserve and LuauStringFormatFixC, 0.663 and later drop LuauMathLerp and LuauVector2Constructor). VM/ is identical between 0.661 and 0.662.

  LuauBufferBitMethods2 is deliberately absent: it is a 0.662 VM flag, but the FS25 executable lacks it.

- **`LuauInterceptor`**: The composition root (InjectorRuntime) supplies the bridge; a null one leaves the interceptor with nothing to inject.
- **`LuauInterceptor`, `SetOwnedByRuntime`**: Attach installs the detour only while a live InjectorRuntime owns the interceptor, so the hook can always find the interceptor that holds its original.

### `src/luau/vr_bridge.cpp`

- **`CollectorHold`**: Holds the game's garbage collector off while it lives and lets it run again when it ends, whichever way that is: if the collector was running it is stopped and then restarted, and the game's own next allocation runs the step that became due; if the game had stopped it, it stays stopped. Luau raises a Lua error as a C++ exception, so a scope that ends in one releases the hold too.
- **`kRenderingModeNames`**: The names of the rendering modes, which the handshake, getRenderingMode and VRBridge.renderingMode hand out, indexed by the mode's value. The list of anchored strings is built from this table, so a mode named here is anchored.
- **`StringArgument`**: The string argument at `index`, or null when it is anything else. A number is not converted: lua_tostring would make a string from it, which allocates on the game's heap.
- **`VRBridge::Inject`, `{`**: Inject runs inside a hook on the game's own call into this state, at a point the game does not expect the collector to run. A collection step started from our pushes below has crashed in the allocator (freeblock, from luaC_step) on several launches. Hold the collector off for the injection and let it run again afterwards, from the game's own allocations; leave it stopped if the game had stopped it.
- **`origTop`, `anchoredStrings`**: The strings the per-frame functions return, made once here and carried by every closure as kAnchorsUpvalue so that returning one allocates nothing (see PushAnchoredString).
- **`origTop`, `lua_createtable(L, 0, static_cast<int>(anchoredStrings.size()));`**: A hash part only, like every other table made here: filled by integer index it had an array part, and the game crashed freeing it (ADR-0002, "the game's collector around VRBridge calls").
- **`origTop`, `for (const LuaFunctionEntry& entry : kFunctionTable) {`**: VRBridge.\<name> = function [bound with 'this' upvalue], driven by kFunctionTable. A per-frame function is registered as it is and never touches the collector on its steady-state zero-allocation hot path; its cold-path fallback table allocations (the 6 per-frame table getters when given no table or an incomplete table) and argument validation errors in `Lua_ReportCameraRecord` hold the collector with `CollectorHold`. Every other function is registered behind `RunOccasional`, which holds the collector for each call. The upvalues are pushed in the order of their slots (kBridgeUpvalue, kAnchorsUpvalue, kEntryUpvalue), and a closure carries as many as its highest slot.
- **`VRBridge::RunOccasional`, `collectorHold`**: The function runs as this closure, so its own kBridgeUpvalue and kAnchorsUpvalue are the bridge and the anchored strings bound at injection.
- **`VRBridge::Lua_Handshake`, `companionVersion`**: The Companion Mod passes its own version as the second argument (the first is the optional mode). One from before that passes none, and an Injector from before it ignores the argument.
- **`VRBridge::Lua_OnMapUnload`, `LogInfo("[VRBridge] Companion Mod map unloaded; reverted to Menu Quad layer m...`**: The Menu Distance stays: the main menu is a menu too, and the Injector shows it at the saved distance from launch.
- **`VRBridge::PulseHaptic`, `if (played) m_virtualGamepad.NoteCompanionPulse(hand, durationMs);`**: The Companion Mod's own pulses win over the game's rumble on that hand while they play.
- **`ParseEyeAndOutputArgs`**: Scans the call arguments for an optional eye selector (index or "left"/"right") and an optional caller-supplied output table. Position-independent so the table can never be mistaken for the eye. Returns the stack index of the output table, or 0 when none was supplied.
- **`HasAllFields`**: The Injector runs its own statically linked Luau on the game's lua_State (ADR-0002). A table the game created points at the game's empty-node sentinel, which the Injector's table code does not recognise, so inserting a new key into it writes through the sentinel and crashes. Overwriting a key that already exists never inserts, so a caller-supplied table is only filled in place when it already holds every field written; otherwise a fresh Injector-made table is returned instead, which the caller can keep and pass back on later calls.
- **`ParseHandAndOutputArgs`**: Scans the call arguments for a hand selector (0/1 or "left"/"right", also "l"/"r") and an optional caller-supplied output table, in either order. An unrecognised or missing selector yields kInvalidHand, which reports an untracked hand. Returns the stack index of the output table or 0.
- **`VRBridge::Lua_GetHandState`**: VRBridge.getHandState(hand [, outTable]) -> table. Like getHeadPose it fills a caller-supplied table in place when that table already holds every field, so nothing is allocated per frame.
- **`VRBridge::Lua_GetVirtualGamepad`**: VRBridge.getVirtualGamepad() -> present, slot, reason, leftGripHeld, rightGripHeld: whether the game sees the Virtual Gamepad, its XInput slot (0..3, nil while absent) and, while absent, why not ("off", "no OpenXR session", ...), then whether each hand's grip is held as the pad reads it (LB / RB, with the pad's own hysteresis), present or not. Returns plain values so a per-frame caller allocates nothing.
- **`VRBridge::SetRenderingMode`, `m_cameraRecords.Clear();`**: The camera records of the old mode would read as history under the new one (the eye cadence differs).
- **`VRBridge::SetMenuVisible`, `SetMenuCursor(false, 0.0f, 0.0f);`**: a ray cursor from the previous panel must not linger
- **`VRBridge::Lua_GetPanelHitAlongRay`**: VRBridge.getPanelHitAlongRay(hand, ox, oy, oz, dx, dy, dz [, outTable]) -> { hit, u, v, distance }: the same for a ray the caller gives, in the space the hand poses of getHandState are in. The Pointer Ray starts at the index fingertip and runs along the finger, which only the Companion Mod knows. The direction need not be a unit vector. A miss when an argument is missing, the direction has no length, or the hand is not tracked.
- **`VRBridge::Lua_SetMenuDistance`, `self->SetMenuDistance(lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L,...`**: Anything that is not a number is the default (lua_tonumber would make it 0, which clamps to the nearest distance)
- **`VRBridge::Lua_Index`, `if (const char* key = StringArgument(L, 2)) {`**: A key that is not a string (a number, say) is not converted to one: that would allocate. It reads nil.
- **`VRBridge::Lua_SetDlssMvFix`, `self->SetMvFixEnabled(enabled)`**: True when the request took effect on the active upscaler backend; turning the pass on after it disabled itself for the session reads false.
- **`VRBridge::Lua_DlssDiag`, `self->ResolveFidelityFxInterceptor()`**: FSR3 dispatches are traced through the same command, even when NGX is absent.

### `src/luau/vr_bridge.h`

- **`VRBridge`**: The composition root (InjectorRuntime) supplies both subsystems; a null one is absent. It also supplies the Injector's release version, which VRBridge reports as VRBridge.version and in the handshake result; a test that does not care gets the build's version, one that does passes its own.
- **`VRBridge`, `GetPanelHit`**: Where one hand's ray lands on the visible menu / 2D Floating Window Mode panel, in normalized panel coordinates (PanelHit). A miss when no panel is showing, the hand is untracked or there is no runtime. The Injector owns the panel pose and size, so the geometry stays here (ADR-0006, ADR-0008).
- **`VRBridge`, `GetPanelHitAlongRay`**: The same for a ray the Companion Mod gives, in the space the hand poses are in: the Pointer Ray starts at the index fingertip and runs along the finger, not along the aim pose.
- **`VRBridge`, `RequiresAfrFallback`**: GIANTS Engine 10 under D3D12 renders one viewport and one scene pass (ADR-0015, ADR-0017), so AFR or SDP is always required.
- **`VRBridge`, `SetPendingRenderingMode`**: Stereo Presentation Mode changes requested while the compositor is active are only queued here. The DXGI proxy latches them at a completed compositor frame boundary (ADR-0015).
- **`VRBridge`, `GetCameraRecords`**: Eye cameras the Companion Mod reports per stereo sub-pass, stamped with the rendered-frame counter the DXGI proxy advances once per Present in stereo presentation.
- **`VRBridge`, `GetActiveEye`**: The eye the current frame renders: the sub-pass under SDP (left first), the frame parity under AFR.
- **`VRBridge`, `IsDesktopMirrorEnabled`**: Whether the game presents to the Desktop Mirror Window while the headset session runs. On by default; the Companion Mod sets it (`vrDesktopMirror`) and the DXGI proxy reads it every present.
- **`VRBridge`, `GetFovOverscan`**: FOV Overscan in percent, 0 to 20 (0 by default; the Companion Mod sets it with `vrFovOverscan`). The bridge clamps and forwards it to the OpenXR manager, which applies it when the next frame begins. Without a manager (Pass-Through Mode) it is accepted and has no effect. A map unload puts it back to 0.
- **`VRBridge`, `GetMenuDistance`**: Menu Distance in metres, 1.0 to 5.0 (1.5 by default; the Companion Mod sets it with `vrMenuDistance`): how far in front of the head menus and 2D Floating Window Mode are shown. The bridge clamps and forwards it to the OpenXR manager, which moves a quad already shown at once. Without a manager (Pass-Through Mode) it is accepted and has no effect. The Injector sets the saved value at launch (InjectorRuntime), and a map unload leaves it, so the main menu is shown at it too.
- **`VRBridge`, `RequestEyeSnapshot`**: `vrEyeSnapshot` (diagnostics): the next stereo frame the DXGI proxy copies to each eye is also written to a picture file next to the log, once per eye. Consume returns true once for each eye of a request.
- **`VRBridge`, `IsMouseConfinementEnabled`**: Whether Mouse Confinement may keep the Desk Mouse on the game picture while the headset is in use. On by default; the Companion Mod sets it (`vrMouseConfinement`) and the DXGI proxy reads it every present.
- **`VRBridge`, `kBridgeUpvalue`**: The upvalues Inject() binds to every registered function's closure; the function runs as that closure, so these are its lua_upvalueindex(...) slots. The bridge; the table of strings the per-frame functions return (see PushAnchoredString); and, for an Occasional function only, its table entry (see RunOccasional).
- **`VRBridge`, `PushAnchoredString`**: Pushes the string `text` for a per-frame function to return, without allocating. A string the Injector pushes is made anew whenever the game's collector has freed the last one (a long string, over 40 bytes, is never shared at all), so the strings a per-frame function hands out are made once at injection and kept in the table every closure carries as kAnchorsUpvalue. `anchorsIndex` is that table (lua_upvalueindex(kAnchorsUpvalue) from inside a function); a text it does not hold, or a function pushed without the table, falls back to lua_pushstring.
- **`VRBridge`, `CallKind`**:

  ```text
  How often the Companion Mod calls a function, which decides what the collector does around the call. The Injector
  links its own copy of Luau and runs it on the game's heap (ADR-0002), and a collection step started from that copy
  has crashed the game, so no call may let one start from there.
   PerFrame:   called on every update or render. Allocates nothing on the game's heap once the caller's reusable
               tables hold their fields, and never touches the collector: restarting it makes a step due at the next
               allocation, and doing that every frame would change the game's collection pacing.
   Occasional: called at handshake, on map load or unload, when a setting changes or from a console command. May
               allocate; Inject() registers it so the collector is held off for the call and left as it was found,
               a Lua error included (see RunOccasional).
   Unmarked:   a table entry that names no kind. Treated like Occasional, and the function table test fails on it.
  ```

- **`VRBridge`, `RunOccasional`**: What Inject() registers in place of an Occasional function: runs the function named by kEntryUpvalue (its table entry) with the game's collector held off, so that no collection step starts from the Injector's copy of Luau during the call. The one place the hold is applied to a function-table function.
- **`VRBridge`, `UpdateFovOverscanSuspension`**: The OpenXR manager renders without the overscan while a menu or 2D Floating Window Mode is shown.

### `tests/test_luau_interceptor.cpp`

- **`MockHost`**: An executable stand-in for the host module, laid out as a minimal PE image: headers, then a .text section holding a query function plus the registration code that pairs it with its name, then a .rdata section holding that name followed by the given strings. Executable so the detour can really be installed when the guard lets AttachToHostSections through; a PE image so the whole module can be handed to AttachToEngine.
- **`LuauInterceptorTest.AttachToHostSectionsRefusesHostWhoseVmMarkersAreMissing`, `frontEndOnly`**: Front end matches 0.662 but the VM half of the fingerprint is absent: still a mismatch.
- **`InjectorVersionPinTest.BuildVersionIsTheVersionInModDesc`, `std::ifstream modDescFile(std::filesystem::path(PROJECT_ROOT_DIR) / "mods" / ...`**: The Injector and the Companion Mod are released together, so the Companion Mod's modDesc.xml is where the release version lives. The build copies it to the Injector (its version resource and kInjectorVersion): a build whose copy differs from the file fails here.
- **`namespace {`**: The composition root names the module the interceptor scans (Dependencies::hostModule). A runtime built by a test names none, so nothing is scanned and the test process is never hooked.
- **`HostModuleTest.RuntimeBuiltWithoutAHostModuleLeavesASuppliedInterceptorsHostModuleAlone`, `MockHost host(AllFingerprintMarkers());`**: Empty means do nothing: a caller that configured its own interceptor keeps what it configured.

### `tests/test_vr_bridge_collector.cpp`

- **the file**: The game's garbage collector around VRBridge calls (spec #245, ADR-0002).

  The Injector links its own copy of Luau and runs it on the game's heap, and a collection step started from that copy has crashed the game. Every function in VRBridge's function table is therefore marked per-frame or occasional: an occasional one holds the collector off for the call and leaves it as it found it, a per-frame one never touches it. These tests walk the whole table and watch the Luau state from outside: whether the collector runs, and whether the destructor of an unreachable userdata has run.

- **`VrBridgeCollectorTest`, `MakeCollectionDue`**: Leaves a collection due: unreachable userdata with a destructor exists, and the next allocation on this state would start a collection step, each step finishing a phase and the next allocation starting the next one, so that a handful of allocations are enough to free the userdata. The function under test is on the stack.
- **`MakeCollectionDue`, `lua_checkstack(L, 64);`**: a full collection shrinks the stack; growing it again must not happen inside the call
- **`VrBridgeCollectorTest.TheGamesNextAllocationsCollectWhatIsDueOnceNoCallIsHolding`, `lua_pushnil(L);`**: The arrangement the other tests rely on: with the collector running and a collection due, the game's own allocations free the unreachable userdata. Without this, "it has not run" below would prove nothing.
- **`VrBridgeCollectorTest.AnOccasionalCallThatEndsWithALuaErrorLeavesTheCollectorAsItFoundIt`, `for (const char* name : {"setDlssMvScale", "setDlssSharpness", "setDlssPreset...`**: The occasional functions known to check a number. A function that starts to raise is covered by the loop above; one of these that stops raising makes this fail, so that the list is kept honest.
- **`VrBridgeCollectorTest.APerFrameCallNeverTouchesTheCollector`, `perFrame`**: Stopping and restarting the collector makes a step due at the next allocation. With headroom before the next collection, a call that left the collector alone leaves the game's following allocations free of one.

### `tests/test_vr_bridge_per_frame_allocation.cpp`

- **the file**: What VRBridge's per-frame functions allocate (spec #245, ADR-0002).

  The Injector links its own copy of Luau and runs it on the game's heap, and a collection step started from that copy has crashed the game. A function the Companion Mod calls on every update or render therefore allocates nothing once the caller's reusable tables hold their fields: it cannot start a collection, and it adds no garbage for the game to collect. These tests walk VRBridge's whole function table and watch the Luau state from outside: the bytes it holds.

- **`VrBridgePerFrameAllocationTest`, `BytesAllocatedByRepeatedCalls`**: The bytes the state holds after the call has run kMeasuredCalls times with the collector stopped, less the bytes before: what the calls allocated and kept. A full collection first, so that what the call needs and does not keep alive (a string only it references) is made again, as it is in the game between two frames.
- **`BytesAllocatedByRepeatedCalls`, `lua_checkstack(L, 128);`**: a full collection shrinks the stack; growing it again is not the call's doing
- **`VrBridgePerFrameAllocationTest.EveryPerFrameFunctionThatTakesNoArgumentsAndReturnsNoTableAllocatesNothing`, `measured`**: The Companion Mod calls a getter such as getRenderingMode or getVirtualGamepad with nothing. A function that hands back a table builds one when it is given none; the test above passes it the table to fill instead.
- **`VrBridgePerFrameAllocationTest.EveryReasonTheVirtualGamepadCanGiveReachesLuaWithoutAllocating`, `lua_getglobal(L, "VRBridge");`**: Some of the reasons are longer than a short string, which Luau does not intern: each push would make a new one.
- **`VrBridgePerFrameAllocationTest.TheAnchoredStringsTableHasNoArrayPart`, `lua_getglobal(L, "VRBridge");`**: In the game, an array-part table made by the Injector crashed the engine when it was freed (the soft restart after quitting to the menu, or the first collection once nothing held the table): every table the Injector makes has a hash part only. A table filled by integer index 1..n has a border of n; one filled by key has a border of 0.
- **`VrBridgePerFrameAllocationTest.ReadingANumberKeyAllocatesNothing`, `for (double key : {1.0, 2.5, -7.0}) {`**: lua_isstring accepts a number and lua_tostring then turns it into a string, which allocates; the property reader answers a key that is not a string with nil without converting it.

### `tests/vr_bridge_fixture.h`

- **`kNumberArgs`**: The most numbers any VRBridge function takes (reportCameraRecord), so a call with this many reaches every function's work and not just its argument check.

## Status
Accepted.

## Considered Options
- Leave the decisions next to the code (rejected, ADR-0041).
- Rewrite each one as a short record of its own (rejected: 66 entries for one area, and every rewrite could lose a measured fact).
- **Move them as they stand into one record for the area, under the file and symbol they belong to (selected).**

## Consequences
- A change to one of these files starts with a search of this record for the symbol it touches. A change that overturns an entry edits the entry in the same pull request.
- An entry whose symbol is renamed or deleted has to be renamed or deleted here by hand. Nothing checks it.
