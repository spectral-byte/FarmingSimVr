#include "system_dxgi.h"
#include "core/logger.h"
#include <iterator>

namespace fs25vr {

const std::wstring& SystemDxgiPath() {
    static const std::wstring path = [] {
        wchar_t systemDir[MAX_PATH];
        const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return std::wstring();
        return std::wstring(systemDir, length) + L"\\dxgi.dll";
    }();
    return path;
}

bool IsSystemDxgiModule(HMODULE module) {
    if (!module) return false;
    const std::wstring& expected = SystemDxgiPath();
    if (expected.empty()) return false;
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) return false;
    return CompareStringOrdinal(path, static_cast<int>(length), expected.c_str(), static_cast<int>(expected.size()),
                                TRUE) == CSTR_EQUAL;
}

bool IsInSystemDxgi(const void* address) {
    HMODULE module = nullptr;
    if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCWSTR>(address), &module)) {
        return false;
    }
    return IsSystemDxgiModule(module);
}

SystemDxgi::SystemDxgi() {
    LoadSystemDxgi();
}

SystemDxgi::~SystemDxgi() {
    if (m_hModule) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
    }
}

bool SystemDxgi::LoadSystemDxgi() {
    if (m_hModule) {
        return true;
    }

    const std::wstring& dxgiPath = SystemDxgiPath();
    if (dxgiPath.empty()) {
        LogError("Failed to retrieve system directory.");
        return false;
    }

    m_hModule = LoadLibraryW(dxgiPath.c_str());
    if (!m_hModule) {
        char pathA[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, dxgiPath.c_str(), -1, pathA, MAX_PATH, nullptr, nullptr);
        LogError(std::string("Failed to load system dxgi.dll from ") + pathA);
        return false;
    }

    LogInfo("Loaded system dxgi.dll successfully.");

    m_pfnCreateDXGIFactory = reinterpret_cast<PFN_CreateDXGIFactory>(GetProcAddress(m_hModule, "CreateDXGIFactory"));
    m_pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(m_hModule, "CreateDXGIFactory1"));
    m_pfnCreateDXGIFactory2 = reinterpret_cast<PFN_CreateDXGIFactory2>(GetProcAddress(m_hModule, "CreateDXGIFactory2"));

    return true;
}

FARPROC SystemDxgi::GetExport(const char* name) {
    if (!m_hModule && !LoadSystemDxgi()) {
        return nullptr;
    }
    return GetProcAddress(m_hModule, name);
}

} // namespace fs25vr
