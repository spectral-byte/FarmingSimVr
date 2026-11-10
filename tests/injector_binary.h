#pragma once

// Helpers for the tests that load the built Injector (version.dll) into a process by its full path, the way the game
// loads it from its x64 folder.
#include "proxy/system_dxgi.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <string>

#ifndef INJECTOR_DLL_PATH
#error INJECTOR_DLL_PATH must be defined by the build
#endif

namespace fs25vr {

// CMake writes the path with forward slashes; Windows paths take backslashes.
inline std::string InjectorPathA() {
    std::string path = INJECTOR_DLL_PATH;
    for (char& c : path) {
        if (c == '/') c = '\\';
    }
    return path;
}

inline std::wstring InjectorPath() {
    const std::string narrow = InjectorPathA();
    return std::wstring(narrow.begin(), narrow.end());
}

inline HMODULE LoadedSystemDxgi() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, SystemDxgiPath().c_str(), &module);
    return module;
}

inline std::array<unsigned char, 16> FirstBytes(const void* function) {
    std::array<unsigned char, 16> bytes{};
    std::memcpy(bytes.data(), function, bytes.size());
    return bytes;
}

} // namespace fs25vr
