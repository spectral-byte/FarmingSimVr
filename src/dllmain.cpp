#include <windows.h>
#include "core/dll_lifecycle.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            fs25vr::OnProcessAttach(static_cast<HMODULE>(hinstDLL));
            break;
        case DLL_PROCESS_DETACH:
            fs25vr::OnProcessDetach(lpReserved);
            break;
        default:
            break;
    }
    return TRUE;
}
