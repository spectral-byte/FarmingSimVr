#pragma once

// Helpers for the tests that load the built Injector (version.dll) into a process by its full path, the way the game
// loads it from its x64 folder.
#include "core/logger.h"
#include "proxy/system_dxgi.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

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

// The log the Injector writes when it is loaded from the build folder: next to the Injector, under the name the test
// process's own logger uses by default too.
inline std::filesystem::path InjectorLogPath() {
    return std::filesystem::path(InjectorPath()).parent_path() / L"fs25_vr.log";
}

// Removes the Injector's log and the backup of the one before it, as if no game had started yet.
inline void RemoveTheInjectorsLogFiles() {
    std::error_code ignored;
    std::filesystem::remove(InjectorLogPath(), ignored);
    std::filesystem::remove(Logger::BackupPathFor(InjectorLogPath()), ignored);
}

// For a test that loads the Injector and reads the log it writes. The Injector writes the same fs25_vr.log as the test
// process's own logger, which holds it open: this lets go of the file and clears the way, as a game start finds the
// folder, and puts the test process's logger back on its default file when the test is done.
class LetTheInjectorHaveItsLog {
public:
    LetTheInjectorHaveItsLog() {
        Logger::Instance().Close();
        RemoveTheInjectorsLogFiles();
    }
    ~LetTheInjectorHaveItsLog() { Logger::Instance().SetLogFile(Logger::DefaultLogPath()); }
    LetTheInjectorHaveItsLog(const LetTheInjectorHaveItsLog&) = delete;
    LetTheInjectorHaveItsLog& operator=(const LetTheInjectorHaveItsLog&) = delete;
};

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

// The process's top-level unhandled exception filter. The only way to read it is to replace it, so it is put back.
inline LPTOP_LEVEL_EXCEPTION_FILTER TopUnhandledExceptionFilter() {
    LPTOP_LEVEL_EXCEPTION_FILTER top = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(top);
    return top;
}

// Puts the unhandled exception filter back as the test found it.
class RestoreTheUnhandledExceptionFilter {
public:
    RestoreTheUnhandledExceptionFilter() : m_found(TopUnhandledExceptionFilter()) {}
    ~RestoreTheUnhandledExceptionFilter() { SetUnhandledExceptionFilter(m_found); }

private:
    LPTOP_LEVEL_EXCEPTION_FILTER m_found;
};

// Two stand-in filters that differ in what they do, so that the linker cannot fold them into one function.
inline LONG WINAPI PreviousFilter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
inline LONG WINAPI LaterFilter(EXCEPTION_POINTERS*) { return EXCEPTION_EXECUTE_HANDLER; }

} // namespace fs25vr
