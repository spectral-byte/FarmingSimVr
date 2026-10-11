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


## Amendment (Luau version compatibility)
The Injector calls its own statically linked Luau VM on the game's `lua_State*`, so it is only safe when GIANTS Engine 10 embeds the same Luau release, because VM internals (state, tables, GC headers) change between releases.

**Finding.** FS25 (`FarmingSimulator2025Game.exe`) embeds **Luau 0.662**, identified from the fast-flag names compiled into the executable. The fingerprint covers both halves of Luau:
- *Front end (Ast/Compiler):* `LuauStoreCSTData`, `LuauFixDoBlockEndLocation`, `ParserNoErrorLimit`, `LuauCompileMathLerp`, `LuauAllowFragmentParsing`. Together they exist only in 0.662: 0.660 and 0.661 lack `LuauFixDoBlockEndLocation`, and 0.663 and later no longer have `LuauCompileMathLerp` or `LuauAllowFragmentParsing`.
- *VM:* `LuauLibWhereErrorAutoreserve`, `LuauStringFormatFixC`, `LuauMathLerp`, `LuauVector2Constructor`. Together they pin the VM to 0.661 or 0.662: 0.660 and earlier lack the first two, and 0.663 and later drop the last two. `VM/` is identical between 0.661 and 0.662. The VM is the half that matters, because it is what the Injector's API calls touch; front-end flags alone would not rule out a newer or older VM.

`LuauBufferBitMethods2`, also a 0.662 VM flag, is absent from the executable, consistent with GIANTS dropping or replacing the `buffer` library. It is therefore not part of the fingerprint. The engine's Luau is GIANTS-modified (extra `table.*`/`string.*` helpers, `Vector2` constructor fixes), but the VM headers the Injector's API calls depend on (`lua.h`, `lobject.h`, `lstate.h`, `ltm.h`) are identical between 0.650 and 0.662 apart from a `Table` to `LuaTable` rename and two new API functions.

**Build configuration.** Struct layouts also depend on `luaconf.h` knobs, which the release fingerprint cannot see: `LUA_VECTOR_SIZE`/`LUA_EXTRA_SIZE` (size of every `TValue`), and `LUA_UTAG_LIMIT`, `LUA_LUTAG_LIMIT`, `LUA_SIZECLASSES` and `LUA_MEMORY_CATEGORIES` (size of `global_State`). They cannot be read from the stripped executable. The Injector `static_assert`s the stock values (3, 1, 128, 128, 40, 256) in `luau_interceptor.cpp`, so its side cannot drift silently. The engine's side is evidenced by the in-game check: a `TValue` size mismatch (16 vs 24 bytes) would misindex the game's stack on the first injected push.

**In-game verification.** On 2026-10-04 VRBridge injection was confirmed working in game (issue #79's acceptance check). All nine fingerprint markers were confirmed present, as whole NUL-delimited strings, in the executable of that game build (dated 2026-09-29).

**Decision.**
1. The Injector now pins Luau **0.662** (previously 0.650, which did not match).
2. `LuauInterceptor::AttachToEngine` scans the host's `.rdata` for the 0.662 fingerprint (`HostEmbedsMatchingLuau`) before installing the detour. On a mismatch, for example after a game patch bumps Luau, it logs an error and skips VRBridge injection instead of touching the game's VM.
3. When the game updates Luau: re-run the fingerprint check against the new executable (front-end and VM flags), update `FS25VR_LUAU_TAG` in `CMakeLists.txt`, `kExpectedLuauVersion` and `kHostLuauFingerprint`, recheck the `luaconf.h` asserts against the new release, and repeat the in-game check. A test fails if `FS25VR_LUAU_TAG` and `kExpectedLuauVersion` disagree.

**Considered and rejected.** Resolving the engine's own `lua_*` entry points from the stripped executable: no symbols, fragile across patches, and it would rewrite most Luau-touching code and tests.
