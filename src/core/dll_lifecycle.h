#pragma once

#include <windows.h>

namespace fs25vr {

void OnProcessAttach(HMODULE selfModule);

void OnProcessDetach(LPVOID lpReserved);

bool IsProcessTerminating();

void ResetProcessTerminatingForTesting();

}
