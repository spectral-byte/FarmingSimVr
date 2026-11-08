#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>

namespace fs25vr {

using PFN_CreateDXGIFactory = HRESULT(WINAPI*)(REFIID riid, void** ppFactory);
using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID riid, void** ppFactory);
using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT Flags, REFIID riid, void** ppFactory);

// The full path of the system's dxgi.dll (in the system directory), or empty if the system directory cannot be read.
const std::wstring& SystemDxgiPath();
// True when `module` is the system's dxgi.dll, not a dxgi.dll loaded from elsewhere (such as ReShade's in the game folder).
bool IsSystemDxgiModule(HMODULE module);
// True when `address` lies inside the system's dxgi.dll.
bool IsInSystemDxgi(const void* address);

class ISystemDxgi {
public:
    virtual ~ISystemDxgi() = default;

    virtual bool LoadSystemDxgi() = 0;
    virtual FARPROC GetExport(const char* name) = 0;

    virtual PFN_CreateDXGIFactory GetCreateDXGIFactory() = 0;
    virtual PFN_CreateDXGIFactory1 GetCreateDXGIFactory1() = 0;
    virtual PFN_CreateDXGIFactory2 GetCreateDXGIFactory2() = 0;
};

// The factory exports of the system's dxgi.dll. In the game they are hooked (DxgiExportHooks), and a call the proxy
// makes through them while it handles a hooked call goes straight to the system.
class SystemDxgi : public ISystemDxgi {
public:
    SystemDxgi();
    ~SystemDxgi() override;

    bool LoadSystemDxgi() override;
    FARPROC GetExport(const char* name) override;

    PFN_CreateDXGIFactory GetCreateDXGIFactory() override { return m_pfnCreateDXGIFactory; }
    PFN_CreateDXGIFactory1 GetCreateDXGIFactory1() override { return m_pfnCreateDXGIFactory1; }
    PFN_CreateDXGIFactory2 GetCreateDXGIFactory2() override { return m_pfnCreateDXGIFactory2; }

private:
    HMODULE m_hModule = nullptr;
    PFN_CreateDXGIFactory m_pfnCreateDXGIFactory = nullptr;
    PFN_CreateDXGIFactory1 m_pfnCreateDXGIFactory1 = nullptr;
    PFN_CreateDXGIFactory2 m_pfnCreateDXGIFactory2 = nullptr;
};

} // namespace fs25vr
