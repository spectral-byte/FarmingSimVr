#include <windows.h>
#include "core/logger.h"

BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/, DWORD fdwReason, LPVOID /*lpReserved*/) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(static_cast<HMODULE>(NULL));
            fs25vr::LogInfo("Farming Simulator 25 VR Injector attached to process.");
            break;
        case DLL_PROCESS_DETACH:
            fs25vr::LogInfo("Farming Simulator 25 VR Injector detaching from process.");
            fs25vr::Logger::Instance().Flush();
            break;
        default:
            break;
    }
    return TRUE;
}
