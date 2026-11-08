#include <windows.h>
#include "core/dll_lifecycle.h"
#include "core/injector_startup.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            fs25vr::OnProcessAttach(static_cast<HMODULE>(hinstDLL));
            fs25vr::StartInjector();
            break;
        case DLL_PROCESS_DETACH:
            // Only on FreeLibrary: at process termination other threads may still be inside the hooks.
            if (lpReserved == nullptr) fs25vr::StopInjector();
            fs25vr::OnProcessDetach(lpReserved);
            break;
        default:
            break;
    }
    return TRUE;
}
