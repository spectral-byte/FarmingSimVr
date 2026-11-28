#include "dll_lifecycle.h"
#include "injector_version.h"
#include "logger.h"
#include "ngx/ngx_interceptor.h"
#include "fidelityfx/fidelityfx_interceptor.h"
#include <atomic>
#include <string>

namespace fs25vr {

namespace {
std::atomic<bool> s_processTerminating{false};
}

void OnProcessAttach(HMODULE selfModule) {
    DisableThreadLibraryCalls(selfModule);
    LogInfo("Farming Simulator 25 VR Injector " + std::string(kInjectorVersion) + " attached to process.");
}

void OnProcessDetach(LPVOID lpReserved) {
    if (lpReserved != nullptr) {
        s_processTerminating = true;
        return;
    }
    LogInfo("Farming Simulator 25 VR Injector detaching from process.");
    NgxInterceptor::DetachLiveHooks();
    FidelityFxInterceptor::DetachLiveHooks();
    Logger::Instance().Flush();
}

bool IsProcessTerminating() {
    return s_processTerminating;
}

void ResetProcessTerminatingForTesting() {
    s_processTerminating = false;
}

}
