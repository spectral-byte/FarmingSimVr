#include "system_dxgi.h"
#include "core/logger.h"
#include <vector>

namespace fs25vr {


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

    wchar_t systemPath[MAX_PATH];
    UINT len = GetSystemDirectoryW(systemPath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        LogError("Failed to retrieve system directory.");
        return false;
    }

    std::wstring dxgiPath = std::wstring(systemPath) + L"\\dxgi.dll";
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
    m_pfnDXGIDeclareAdapterRemovalSupport = reinterpret_cast<PFN_DXGIDeclareAdapterRemovalSupport>(GetProcAddress(m_hModule, "DXGIDeclareAdapterRemovalSupport"));
    m_pfnDXGIGetDebugInterface1 = reinterpret_cast<PFN_DXGIGetDebugInterface1>(GetProcAddress(m_hModule, "DXGIGetDebugInterface1"));

    return true;
}

FARPROC SystemDxgi::GetExport(const char* name) {
    if (!m_hModule && !LoadSystemDxgi()) {
        return nullptr;
    }
    return GetProcAddress(m_hModule, name);
}

} // namespace fs25vr
