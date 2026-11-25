#pragma once

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

inline std::filesystem::path InjectorLogPath() {
    return std::filesystem::path(InjectorPath()).parent_path() / L"fs25_vr.log";
}

inline void RemoveTheInjectorsLogFiles() {
    std::error_code ignored;
    std::filesystem::remove(InjectorLogPath(), ignored);
    std::filesystem::remove(Logger::BackupPathFor(InjectorLogPath()), ignored);
}

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

inline LPTOP_LEVEL_EXCEPTION_FILTER TopUnhandledExceptionFilter() {
    LPTOP_LEVEL_EXCEPTION_FILTER top = SetUnhandledExceptionFilter(nullptr);
    SetUnhandledExceptionFilter(top);
    return top;
}

class RestoreTheUnhandledExceptionFilter {
public:
    RestoreTheUnhandledExceptionFilter() : m_found(TopUnhandledExceptionFilter()) {}
    ~RestoreTheUnhandledExceptionFilter() { SetUnhandledExceptionFilter(m_found); }

private:
    LPTOP_LEVEL_EXCEPTION_FILTER m_found;
};

inline LONG WINAPI PreviousFilter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
inline LONG WINAPI LaterFilter(EXCEPTION_POINTERS*) { return EXCEPTION_EXECUTE_HANDLER; }

}
