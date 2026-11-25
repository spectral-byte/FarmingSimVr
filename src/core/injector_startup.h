#pragma once

#include <windows.h>

namespace fs25vr {

void StartInjector();

void StopInjector();

bool IsFs25VrModule(HMODULE module);

HMODULE FindEarlierInjector();

}
