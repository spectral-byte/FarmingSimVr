#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>

namespace fs25vr {

using PFN_CreateDXGIFactory = HRESULT(WINAPI*)(REFIID riid, void** ppFactory);
using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID riid, void** ppFactory);
using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT Flags, REFIID riid, void** ppFactory);
using PFN_DXGIDeclareAdapterRemovalSupport = HRESULT(WINAPI*)();
using PFN_DXGIGetDebugInterface1 = HRESULT(WINAPI*)(UINT Flags, REFIID riid, void** pDebug);

class ISystemDxgi {
public:
    virtual ~ISystemDxgi() = default;

    virtual bool LoadSystemDxgi() = 0;
    virtual FARPROC GetExport(const char* name) = 0;

    virtual PFN_CreateDXGIFactory GetCreateDXGIFactory() = 0;
    virtual PFN_CreateDXGIFactory1 GetCreateDXGIFactory1() = 0;
    virtual PFN_CreateDXGIFactory2 GetCreateDXGIFactory2() = 0;
    virtual PFN_DXGIDeclareAdapterRemovalSupport GetDXGIDeclareAdapterRemovalSupport() = 0;
    virtual PFN_DXGIGetDebugInterface1 GetDXGIGetDebugInterface1() = 0;
};

class SystemDxgi : public ISystemDxgi {
public:
    static SystemDxgi& Instance();

    SystemDxgi();
    ~SystemDxgi() override;

    bool LoadSystemDxgi() override;
    FARPROC GetExport(const char* name) override;

    PFN_CreateDXGIFactory GetCreateDXGIFactory() override { return m_pfnCreateDXGIFactory; }
    PFN_CreateDXGIFactory1 GetCreateDXGIFactory1() override { return m_pfnCreateDXGIFactory1; }
    PFN_CreateDXGIFactory2 GetCreateDXGIFactory2() override { return m_pfnCreateDXGIFactory2; }
    PFN_DXGIDeclareAdapterRemovalSupport GetDXGIDeclareAdapterRemovalSupport() override { return m_pfnDXGIDeclareAdapterRemovalSupport; }
    PFN_DXGIGetDebugInterface1 GetDXGIGetDebugInterface1() override { return m_pfnDXGIGetDebugInterface1; }

private:
    HMODULE m_hModule = nullptr;
    PFN_CreateDXGIFactory m_pfnCreateDXGIFactory = nullptr;
    PFN_CreateDXGIFactory1 m_pfnCreateDXGIFactory1 = nullptr;
    PFN_CreateDXGIFactory2 m_pfnCreateDXGIFactory2 = nullptr;
    PFN_DXGIDeclareAdapterRemovalSupport m_pfnDXGIDeclareAdapterRemovalSupport = nullptr;
    PFN_DXGIGetDebugInterface1 m_pfnDXGIGetDebugInterface1 = nullptr;
};

} // namespace fs25vr
