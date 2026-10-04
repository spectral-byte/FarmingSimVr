#include "dll_lifecycle.h"
#include "logger.h"
#include "ngx/ngx_interceptor.h"
#include <atomic>

namespace fs25vr {

namespace {
std::atomic<bool> s_processTerminating{false};
} // anonymous namespace

void OnProcessAttach(HMODULE selfModule) {
    DisableThreadLibraryCalls(selfModule);
    LogInfo("Farming Simulator 25 VR Injector attached to process.");
}

void OnProcessDetach(LPVOID lpReserved) {
    LogInfo("Farming Simulator 25 VR Injector detaching from process.");
    if (lpReserved != nullptr) {
        // Set before returning: the CRT runs our static destructors right after DllMain.
        s_processTerminating = true;
    } else {
        // FreeLibrary: safe to unwind our own hooks.
        NgxInterceptor::Instance().DetachAll();
    }
    Logger::Instance().Flush();
}

bool IsProcessTerminating() {
    return s_processTerminating;
}

void ResetProcessTerminatingForTesting() {
    s_processTerminating = false;
}

} // namespace fs25vr
