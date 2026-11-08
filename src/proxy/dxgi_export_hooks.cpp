#include "dxgi_export_hooks.h"
#include "system_dxgi.h"
#include "core/dll_lifecycle.h"
#include "core/logger.h"
#include <atomic>
#include <cstring>

namespace fs25vr {

namespace {

// The installed instance. Published before the first hook goes live, so a hook always finds its trampolines.
std::atomic<DxgiExportHooks*> s_installed{nullptr};

// Entries running on this thread. Calls made while one runs go straight to the system.
thread_local int t_entryDepth = 0;

struct EntryScope {
    EntryScope() { ++t_entryDepth; }
    ~EntryScope() { --t_entryDepth; }
    EntryScope(const EntryScope&) = delete;
    EntryScope& operator=(const EntryScope&) = delete;
};

} // namespace

DxgiExportHooks::DxgiExportHooks(IDxgiFactoryEntry& entry) : m_entry(entry) {}

DxgiExportHooks::~DxgiExportHooks() {
    // At termination the patches stay, like every other hook's (see FunctionDetour).
    if (IsProcessTerminating()) return;
    Uninstall();
}

void* DxgiExportHooks::FindExport(HMODULE module, const char* name) {
    if (!module || !name) return nullptr;
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) return nullptr;

    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
    const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    const auto* nameOrdinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
    const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), name) != 0) continue;
        const WORD index = nameOrdinals[i];
        if (index >= exports->NumberOfFunctions) return nullptr;
        const DWORD rva = functions[index];
        // An address inside the export directory names a forwarder ("DLL.Function"), not code.
        if (rva >= directory.VirtualAddress && rva < directory.VirtualAddress + directory.Size) return nullptr;
        return const_cast<BYTE*>(base + rva);
    }
    return nullptr;
}

bool DxgiExportHooks::Install(HMODULE systemDxgi) {
    if (IsInstalled()) return true;

    void* createFactory = FindExport(systemDxgi, "CreateDXGIFactory");
    void* createFactory1 = FindExport(systemDxgi, "CreateDXGIFactory1");
    void* createFactory2 = FindExport(systemDxgi, "CreateDXGIFactory2");
    if (!createFactory || !createFactory1 || !createFactory2) {
        LogError("DxgiExportHooks: The system's dxgi.dll lacks a CreateDXGIFactory export; DXGI is not hooked.");
        return false;
    }

    DxgiExportHooks* none = nullptr;
    if (!s_installed.compare_exchange_strong(none, this)) {
        LogError("DxgiExportHooks: Another instance already hooks the system's dxgi.dll.");
        return false;
    }

    const bool hooked =
        m_createFactory.Install(createFactory, reinterpret_cast<void*>(&DxgiExportHooks::Hook_CreateDXGIFactory)) &&
        m_createFactory1.Install(createFactory1, reinterpret_cast<void*>(&DxgiExportHooks::Hook_CreateDXGIFactory1)) &&
        m_createFactory2.Install(createFactory2, reinterpret_cast<void*>(&DxgiExportHooks::Hook_CreateDXGIFactory2));
    if (!hooked) {
        LogError("DxgiExportHooks: Could not hook the system's dxgi.dll; DXGI is not hooked.");
        Uninstall();
        return false;
    }
    LogInfo("DxgiExportHooks: Hooked CreateDXGIFactory, CreateDXGIFactory1 and CreateDXGIFactory2 in the system's dxgi.dll.");
    return true;
}

void DxgiExportHooks::Uninstall() {
    if (m_createFactory2.IsInstalled()) m_createFactory2.Uninstall();
    if (m_createFactory1.IsInstalled()) m_createFactory1.Uninstall();
    if (m_createFactory.IsInstalled()) m_createFactory.Uninstall();
    DxgiExportHooks* self = this;
    s_installed.compare_exchange_strong(self, nullptr);
}

bool DxgiExportHooks::IsInstalled() const {
    return s_installed.load() == this && m_createFactory.IsInstalled() && m_createFactory1.IsInstalled() &&
           m_createFactory2.IsInstalled();
}

HRESULT WINAPI DxgiExportHooks::Hook_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    DxgiExportHooks* hooks = s_installed.load();
    const auto original = hooks ? reinterpret_cast<PFN_CreateDXGIFactory>(hooks->m_createFactory.GetOriginal()) : nullptr;
    if (!original) return E_FAIL;
    if (t_entryDepth > 0) return original(riid, ppFactory);
    EntryScope scope;
    return hooks->m_entry.CreateDXGIFactory(riid, ppFactory);
}

HRESULT WINAPI DxgiExportHooks::Hook_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    DxgiExportHooks* hooks = s_installed.load();
    const auto original = hooks ? reinterpret_cast<PFN_CreateDXGIFactory1>(hooks->m_createFactory1.GetOriginal()) : nullptr;
    if (!original) return E_FAIL;
    if (t_entryDepth > 0) return original(riid, ppFactory);
    EntryScope scope;
    return hooks->m_entry.CreateDXGIFactory1(riid, ppFactory);
}

HRESULT WINAPI DxgiExportHooks::Hook_CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) {
    DxgiExportHooks* hooks = s_installed.load();
    const auto original = hooks ? reinterpret_cast<PFN_CreateDXGIFactory2>(hooks->m_createFactory2.GetOriginal()) : nullptr;
    if (!original) return E_FAIL;
    if (t_entryDepth > 0) return original(flags, riid, ppFactory);
    EntryScope scope;
    return hooks->m_entry.CreateDXGIFactory2(flags, riid, ppFactory);
}

} // namespace fs25vr
