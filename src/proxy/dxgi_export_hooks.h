#pragma once

#include "core/detour.h"
#include <windows.h>
#include <unknwn.h>

namespace fs25vr {

class IDxgiFactoryEntry {
public:
    virtual ~IDxgiFactoryEntry() = default;
    virtual HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory) = 0;
    virtual HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory) = 0;
    virtual HRESULT CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) = 0;
};

class DxgiExportHooks {
public:
    explicit DxgiExportHooks(IDxgiFactoryEntry& entry);
    ~DxgiExportHooks();

    DxgiExportHooks(const DxgiExportHooks&) = delete;
    DxgiExportHooks& operator=(const DxgiExportHooks&) = delete;

    bool Install(HMODULE systemDxgi);
    void Uninstall();
    bool IsInstalled() const;

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

}
