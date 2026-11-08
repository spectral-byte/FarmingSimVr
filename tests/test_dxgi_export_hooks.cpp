#include <gtest/gtest.h>
#include "core/injector_startup.h"
#include "proxy/dxgi_export_hooks.h"
#include "proxy/system_dxgi.h"
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#ifndef INJECTOR_DLL_PATH
#error INJECTOR_DLL_PATH must be defined by the build
#endif

using Microsoft::WRL::ComPtr;

namespace fs25vr {
namespace {

HMODULE LoadedSystemDxgi() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, SystemDxgiPath().c_str(), &module);
    return module;
}

// The built Injector's path, with the forward slashes CMake writes turned into backslashes.
std::wstring InjectorPath() {
    std::wstring result;
    for (const char* c = INJECTOR_DLL_PATH; *c; ++c) {
        result.push_back(*c == '/' ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(*c)));
    }
    return result;
}

// Passes each call on through the system's exports, the way the proxy does (SystemDxgi), and counts the calls.
class CountingEntry final : public IDxgiFactoryEntry {
public:
    std::atomic<int> calls{0};
    SystemDxgi system;

    HRESULT CreateDXGIFactory(REFIID riid, void** ppFactory) override {
        ++calls;
        return system.GetCreateDXGIFactory()(riid, ppFactory);
    }
    HRESULT CreateDXGIFactory1(REFIID riid, void** ppFactory) override {
        ++calls;
        return system.GetCreateDXGIFactory1()(riid, ppFactory);
    }
    HRESULT CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) override {
        ++calls;
        return system.GetCreateDXGIFactory2()(flags, riid, ppFactory);
    }
};

} // namespace

TEST(DxgiExportHooksTest, FindExportReadsTheExportTable) {
    HMODULE dxgi = LoadedSystemDxgi();
    ASSERT_NE(dxgi, nullptr);
    for (const char* name : {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"}) {
        EXPECT_EQ(DxgiExportHooks::FindExport(dxgi, name), reinterpret_cast<void*>(GetProcAddress(dxgi, name))) << name;
    }
    EXPECT_EQ(DxgiExportHooks::FindExport(dxgi, "NoSuchExport"), nullptr);
}

TEST(DxgiExportHooksTest, FindExportGivesNoAddressForAForwardedExport) {
    // The system's version.dll forwards VerLanguageNameW to kernel32: there is no code of its own to hook.
    wchar_t path[MAX_PATH];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    ASSERT_GT(length, 0u);
    HMODULE version = LoadLibraryW((std::wstring(path, length) + L"\\version.dll").c_str());
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(DxgiExportHooks::FindExport(version, "VerLanguageNameW"), nullptr);
    EXPECT_NE(DxgiExportHooks::FindExport(version, "GetFileVersionInfoW"), nullptr);
    FreeLibrary(version);
}

TEST(DxgiExportHooksTest, FactoryCallsThroughTheSystemExportsReachTheEntryAndStillCreateAFactory) {
    HMODULE dxgi = LoadedSystemDxgi();
    ASSERT_NE(dxgi, nullptr);
    CountingEntry entry;
    DxgiExportHooks hooks(entry);
    ASSERT_TRUE(hooks.Install(dxgi));
    EXPECT_TRUE(hooks.IsInstalled());

    // Called the way any module calls them. The entry passes each on through the same, hooked, export; that call
    // goes straight to the system instead of back to the entry.
    auto create = reinterpret_cast<PFN_CreateDXGIFactory>(GetProcAddress(dxgi, "CreateDXGIFactory"));
    auto create1 = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    auto create2 = reinterpret_cast<PFN_CreateDXGIFactory2>(GetProcAddress(dxgi, "CreateDXGIFactory2"));
    ComPtr<IDXGIFactory> factory;
    ComPtr<IDXGIFactory1> factory1;
    ComPtr<IDXGIFactory2> factory2;
    EXPECT_EQ(create(IID_PPV_ARGS(&factory)), S_OK);
    EXPECT_EQ(create1(IID_PPV_ARGS(&factory1)), S_OK);
    EXPECT_EQ(create2(0, IID_PPV_ARGS(&factory2)), S_OK);
    EXPECT_NE(factory.Get(), nullptr);
    EXPECT_NE(factory1.Get(), nullptr);
    EXPECT_NE(factory2.Get(), nullptr);
    EXPECT_EQ(entry.calls.load(), 3);

    hooks.Uninstall();
    EXPECT_FALSE(hooks.IsInstalled());
    ComPtr<IDXGIFactory1> after;
    EXPECT_EQ(create1(IID_PPV_ARGS(&after)), S_OK);
    EXPECT_EQ(entry.calls.load(), 3) << "A call after Uninstall reached the entry.";
}

TEST(DxgiExportHooksTest, ACallFromAnotherThreadReachesTheEntryToo) {
    HMODULE dxgi = LoadedSystemDxgi();
    ASSERT_NE(dxgi, nullptr);
    CountingEntry entry;
    DxgiExportHooks hooks(entry);
    ASSERT_TRUE(hooks.Install(dxgi));

    auto create1 = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    HRESULT result = E_PENDING;
    std::thread([&] {
        ComPtr<IDXGIFactory1> factory;
        result = create1(IID_PPV_ARGS(&factory));
    }).join();
    EXPECT_EQ(result, S_OK);
    EXPECT_EQ(entry.calls.load(), 1);
}

TEST(DxgiExportHooksTest, OnlyOneInstanceHooksAtATime) {
    HMODULE dxgi = LoadedSystemDxgi();
    ASSERT_NE(dxgi, nullptr);
    CountingEntry firstEntry;
    CountingEntry secondEntry;
    DxgiExportHooks first(firstEntry);
    DxgiExportHooks second(secondEntry);
    ASSERT_TRUE(first.Install(dxgi));
    EXPECT_FALSE(second.Install(dxgi));
    EXPECT_FALSE(second.IsInstalled());
    EXPECT_TRUE(first.IsInstalled());

    first.Uninstall();
    EXPECT_TRUE(second.Install(dxgi));
    second.Uninstall();
}

TEST(DxgiExportHooksTest, DestroyingTheHooksRemovesThem) {
    HMODULE dxgi = LoadedSystemDxgi();
    ASSERT_NE(dxgi, nullptr);
    void* createFactory1 = DxgiExportHooks::FindExport(dxgi, "CreateDXGIFactory1");
    unsigned char before[8];
    std::memcpy(before, createFactory1, sizeof(before));
    {
        CountingEntry entry;
        DxgiExportHooks hooks(entry);
        ASSERT_TRUE(hooks.Install(dxgi));
        EXPECT_NE(std::memcmp(createFactory1, before, sizeof(before)), 0);
    }
    EXPECT_EQ(std::memcmp(createFactory1, before, sizeof(before)), 0);
}

TEST(InjectorStartupTest, RecognisesAnFs25VrModuleByItsVersionResource) {
    HMODULE injector = LoadLibraryExW(InjectorPath().c_str(), nullptr,
                                      LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH;
    EXPECT_TRUE(IsFs25VrModule(injector));
    FreeLibrary(injector);

    EXPECT_FALSE(IsFs25VrModule(LoadedSystemDxgi()));          // a version resource of another product
    EXPECT_FALSE(IsFs25VrModule(GetModuleHandleW(nullptr)));   // no version resource
    EXPECT_FALSE(IsFs25VrModule(nullptr));
}

TEST(InjectorStartupTest, TheSystemsDxgiIsNoEarlierInjector) {
    // The test process loads the system's dxgi.dll, not an Injector from before FS25 VR was version.dll.
    EXPECT_EQ(FindEarlierInjector(), nullptr);
}

TEST(SystemDxgiTest, TellsTheSystemsDxgiFromOtherModules) {
    HMODULE dxgi = LoadedSystemDxgi();
    ASSERT_NE(dxgi, nullptr);
    EXPECT_TRUE(IsSystemDxgiModule(dxgi));
    EXPECT_TRUE(IsInSystemDxgi(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1"))));
    EXPECT_FALSE(IsSystemDxgiModule(GetModuleHandleW(L"kernel32.dll")));
    EXPECT_FALSE(IsInSystemDxgi(reinterpret_cast<void*>(&LoadedSystemDxgi)));
    EXPECT_FALSE(IsInSystemDxgi(nullptr));
}

} // namespace fs25vr
