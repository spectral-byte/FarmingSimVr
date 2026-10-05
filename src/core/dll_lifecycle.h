#pragma once

#include <windows.h>

namespace fs25vr {

// Work performed from DllMain. Must stay loader-lock safe: no LoadLibrary, no hook installation.
// NGX module-load hooks are installed lazily on the first DXGI factory call instead.
void OnProcessAttach(HMODULE selfModule);

// lpReserved is non-null when the process is terminating. In that case other threads are already
// gone, so hooks and trampolines are left alone and only the log is flushed.
void OnProcessDetach(LPVOID lpReserved);

// True once OnProcessDetach has seen process termination. The CRT still runs static destructors
// after DllMain returns, so every destructor that removes hooks or takes locks checks this and
// returns early: a killed thread may own the lock or be executing inside a patched prologue.
bool IsProcessTerminating();

// Tests only: OnProcessDetach(non-null) is irreversible in a real process.
void ResetProcessTerminatingForTesting();

} // namespace fs25vr
