#include <windows.h>
#include "core/logger.h"
#include "ngx/ngx_interceptor.h"

BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/, DWORD fdwReason, LPVOID /*lpReserved*/) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(static_cast<HMODULE>(NULL));
            fs25vr::LogInfo("Farming Simulator 25 VR Injector attached to process.");
            fs25vr::NgxInterceptor::Instance().InstallModuleLoadHooks();
            fs25vr::NgxInterceptor::Instance().CheckAndAttachLoadedModule();
            break;
        case DLL_PROCESS_DETACH:
            fs25vr::LogInfo("Farming Simulator 25 VR Injector detaching from process.");
            fs25vr::NgxInterceptor::Instance().DetachAll();
            fs25vr::Logger::Instance().Flush();
            break;
        default:
            break;
    }
    return TRUE;
}
