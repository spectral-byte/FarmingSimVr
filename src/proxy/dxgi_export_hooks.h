#pragma once

#include "core/detour.h"
#include <windows.h>
#include <unknwn.h>

namespace fs25vr {

// Where the CreateDXGIFactory* calls that DxgiExportHooks catches go.
class IDxgiFactoryEntry {
public:
    virtual ~IDxgiFactoryEntry() = default;
    virtual HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory) = 0;
    virtual HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory) = 0;
    virtual HRESULT CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) = 0;
};

// Hooks CreateDXGIFactory, CreateDXGIFactory1 and CreateDXGIFactory2 in the system's dxgi.dll (ADR-0038), so a
// factory created by any module in the process (the game, a dxgi.dll in the game folder such as ReShade's, the
// OpenXR runtime) is created through the entry. While the entry runs on a thread, a call on that thread to any of
// the three goes straight to the system: that is the entry passing the call on through the system's export, the
// OpenXR runtime creating a factory while the entry starts it, or dxgi.dll calling its own export.
// One instance can be installed at a time.
class DxgiExportHooks {
public:
    explicit DxgiExportHooks(IDxgiFactoryEntry& entry);
    // Removes the hooks, except while the process terminates (see IsProcessTerminating).
    ~DxgiExportHooks();

    DxgiExportHooks(const DxgiExportHooks&) = delete;
    DxgiExportHooks& operator=(const DxgiExportHooks&) = delete;

    // Hooks the three exports of `systemDxgi`, all or none: false, with nothing hooked, when one cannot be hooked
    // or another instance is installed. True if already installed.
    bool Install(HMODULE systemDxgi);
    void Uninstall();
    bool IsInstalled() const;

    // The address of a module's export, read from its export table rather than asked of the loader, so it works
    // while the loader is still loading the module. Null when there is no such export or it is forwarded.
    static void* FindExport(HMODULE module, const char* name);

private:
    static HRESULT WINAPI Hook_CreateDXGIFactory(REFIID riid, void** ppFactory);
    static HRESULT WINAPI Hook_CreateDXGIFactory1(REFIID riid, void** ppFactory);
    static HRESULT WINAPI Hook_CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory);

    IDxgiFactoryEntry& m_entry;
    FunctionDetour m_createFactory;
    FunctionDetour m_createFactory1;
    FunctionDetour m_createFactory2;
};

} // namespace fs25vr
