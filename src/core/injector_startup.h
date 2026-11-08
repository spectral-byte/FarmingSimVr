#pragma once

#include <windows.h>

namespace fs25vr {

// Called from the Injector's DllMain on process attach (ADR-0038). The game loads the Injector as its version.dll,
// before it creates any DXGI factory, so the factory exports of the system's dxgi.dll are hooked here: right away
// when that dxgi.dll is already loaded, else from a DLL load notification the moment it loads (a dxgi.dll in the
// game folder, such as ReShade's, loads it later). Every caught factory call goes to InjectorRuntime::Instance().
// Installing these three hooks is the one exception to "no hooks from DllMain" (see dll_lifecycle.h).
// Hooks nothing when an FS25 VR Injector of an earlier version, which was the game's dxgi.dll, is loaded too:
// both would hook the game.
void StartInjector();

// Called from DllMain when the Injector is unloaded with FreeLibrary: removes the hooks and the load notification.
// Not called at process termination, where hooks are left as they are.
void StopInjector();

// True when the module's version resource gives "FS25 VR" as ProductName. Reads the resource directly, without
// version.dll (which is the Injector itself). Works on a module loaded as a data file too.
bool IsFs25VrModule(HMODULE module);

// The game's dxgi.dll when it is not the system's and is an FS25 VR Injector of an earlier version, else null.
HMODULE FindEarlierInjector();

} // namespace fs25vr
