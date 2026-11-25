#include <gtest/gtest.h>
#include "core/injector_version.h"
#include "core/logger.h"
#include "injector_binary.h"
#include "proxy/dxgi_export_hooks.h"
#include "proxy/system_dxgi.h"
#include <windows.h>
#include <winver.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace fs25vr {
namespace {

constexpr const char* kVersionExports[] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW", "VerLanguageNameA",
    "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW",
};

class InjectorBinaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_NE(LoadedSystemDxgi(), nullptr) << "The test process has no system dxgi.dll loaded.";
        m_createFactory1 = DxgiExportHooks::FindExport(LoadedSystemDxgi(), "CreateDXGIFactory1");
        ASSERT_NE(m_createFactory1, nullptr);
        m_unhooked = FirstBytes(m_createFactory1);
        m_injector = LoadLibraryW(InjectorPath().c_str());
        ASSERT_NE(m_injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    }

    void TearDown() override {
        if (m_injector) FreeLibrary(m_injector);
        m_injector = nullptr;
    }

    template <typename Fn>
    Fn Export(const char* name) {
        return reinterpret_cast<Fn>(GetProcAddress(m_injector, name));
    }

    HMODULE m_injector = nullptr;
    void* m_createFactory1 = nullptr;
    std::array<unsigned char, 16> m_unhooked{};
};

}

TEST_F(InjectorBinaryTest, ExportsEveryFunctionOfTheSystemsVersionDll) {
    for (const char* name : kVersionExports) {
        EXPECT_NE(GetProcAddress(m_injector, name), nullptr) << name;
    }
    EXPECT_EQ(GetProcAddress(m_injector, "CreateDXGIFactory1"), nullptr) << "The Injector is no longer a dxgi.dll.";
}

TEST_F(InjectorBinaryTest, VersionExportsReadAVersionResourceAsTheSystemsDo) {
    auto sizeW = Export<decltype(&GetFileVersionInfoSizeW)>("GetFileVersionInfoSizeW");
    auto infoW = Export<decltype(&GetFileVersionInfoW)>("GetFileVersionInfoW");
    auto queryW = Export<decltype(&VerQueryValueW)>("VerQueryValueW");
    ASSERT_TRUE(sizeW && infoW && queryW);

    const std::wstring path = InjectorPath();
    DWORD handle = 0;
    const DWORD size = sizeW(path.c_str(), &handle);
    ASSERT_GT(size, 0u);
    std::vector<BYTE> block(size);
    ASSERT_TRUE(infoW(path.c_str(), 0, size, block.data()));
    wchar_t* product = nullptr;
    UINT productLength = 0;
    ASSERT_TRUE(queryW(block.data(), L"\\StringFileInfo\\040904b0\\ProductName", reinterpret_cast<void**>(&product),
                       &productLength));
    EXPECT_EQ(std::wstring(product), L"FS25 VR");

    auto sizeA = Export<decltype(&GetFileVersionInfoSizeA)>("GetFileVersionInfoSizeA");
    auto infoA = Export<decltype(&GetFileVersionInfoA)>("GetFileVersionInfoA");
    auto queryA = Export<decltype(&VerQueryValueA)>("VerQueryValueA");
    ASSERT_TRUE(sizeA && infoA && queryA);
    const std::string pathA = InjectorPathA();
    const DWORD sizeOfA = sizeA(pathA.c_str(), &handle);
    ASSERT_GT(sizeOfA, 0u);
    std::vector<BYTE> blockA(sizeOfA);
    ASSERT_TRUE(infoA(pathA.c_str(), 0, sizeOfA, blockA.data()));
    char* description = nullptr;
    UINT descriptionLength = 0;
    ASSERT_TRUE(queryA(blockA.data(), "\\StringFileInfo\\040904b0\\FileDescription",
                       reinterpret_cast<void**>(&description), &descriptionLength));
    EXPECT_STREQ(description, "FS25 VR Injector");
}

TEST_F(InjectorBinaryTest, VersionExportsReportErrorsAsTheSystemsDo) {
    auto sizeW = Export<decltype(&GetFileVersionInfoSizeW)>("GetFileVersionInfoSizeW");
    ASSERT_NE(sizeW, nullptr);
    DWORD handle = 0;
    SetLastError(0);
    EXPECT_EQ(sizeW(L"C:\\no\\such\\file.dll", &handle), 0u);
    EXPECT_NE(GetLastError(), 0u);
}

TEST_F(InjectorBinaryTest, VerLanguageNameIsForwardedToKernel32) {
    auto languageNameW = Export<decltype(&VerLanguageNameW)>("VerLanguageNameW");
    ASSERT_NE(languageNameW, nullptr);
    wchar_t name[128] = {};
    EXPECT_GT(languageNameW(0x409, name, 128), 0u);
}

TEST_F(InjectorBinaryTest, HooksTheSystemsDxgiWhileLoadedAndAFactoryIsStillCreated) {
    EXPECT_NE(FirstBytes(m_createFactory1), m_unhooked) << "Loading the Injector did not hook CreateDXGIFactory1.";

    auto create1 = reinterpret_cast<PFN_CreateDXGIFactory1>(m_createFactory1);
    ComPtr<IDXGIFactory1> factory;
    EXPECT_EQ(create1(IID_PPV_ARGS(&factory)), S_OK);
    EXPECT_NE(factory.Get(), nullptr);

    FreeLibrary(m_injector);
    m_injector = nullptr;
    EXPECT_EQ(FirstBytes(m_createFactory1), m_unhooked) << "Unloading the Injector left CreateDXGIFactory1 hooked.";
}

TEST(InjectorLogTest, FirstLineNamesTheInjectorsVersion) {
    LetTheInjectorHaveItsLog theLog;
    const std::filesystem::path logPath = InjectorLogPath();

    HMODULE injector = LoadLibraryW(InjectorPath().c_str());
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    FreeLibrary(injector);

    std::ifstream log(logPath);
    ASSERT_TRUE(log.is_open()) << logPath;
    std::string firstLine;
    std::getline(log, firstLine);
    EXPECT_NE(firstLine.find(" " + std::string(kInjectorVersion) + " "), std::string::npos) << firstLine;
}

TEST(InjectorCrashFilterTest, FreeingTheInjectorLeavesTheProcessesPreviousFilterInPlace) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);

    HMODULE injector = LoadLibraryW(InjectorPath().c_str());
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    EXPECT_NE(TopUnhandledExceptionFilter(), &PreviousFilter) << "Loading the Injector did not install its filter.";

    FreeLibrary(injector);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &PreviousFilter);
}

TEST(InjectorCrashFilterTest, TheFirstFactoryCallPutsTheInjectorsFilterOnTopOfOneThatTookThePlace) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    HMODULE injector = LoadLibraryW(InjectorPath().c_str());
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    SetUnhandledExceptionFilter(&LaterFilter);
    void* createFactory1 = DxgiExportHooks::FindExport(LoadedSystemDxgi(), "CreateDXGIFactory1");
    ASSERT_NE(createFactory1, nullptr);

    ComPtr<IDXGIFactory1> factory;
    EXPECT_EQ(reinterpret_cast<PFN_CreateDXGIFactory1>(createFactory1)(IID_PPV_ARGS(&factory)), S_OK);
    const LPTOP_LEVEL_EXCEPTION_FILTER top = TopUnhandledExceptionFilter();
    factory.Reset();
    FreeLibrary(injector);

    EXPECT_NE(top, &LaterFilter) << "The first factory call did not put the Injector's filter on top again.";
    EXPECT_NE(top, &PreviousFilter);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter) << "The filter found on top is not the one to pass crashes on to.";
}

TEST(InjectorCrashFilterTest, TheInjectorPreparesItsCrashDumpAgainWhenItIsLoadedAgain) {
    LetTheInjectorHaveItsLog theLog;
    const std::filesystem::path logPath = InjectorLogPath();
    void* createFactory1 = DxgiExportHooks::FindExport(LoadedSystemDxgi(), "CreateDXGIFactory1");
    ASSERT_NE(createFactory1, nullptr);

    for (int load = 0; load < 2; ++load) {
        HMODULE injector = LoadLibraryW(InjectorPath().c_str());
        ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
        ComPtr<IDXGIFactory1> factory;
        EXPECT_EQ(reinterpret_cast<PFN_CreateDXGIFactory1>(createFactory1)(IID_PPV_ARGS(&factory)), S_OK);
        factory.Reset();
        FreeLibrary(injector);
    }

    std::ifstream log(logPath);
    std::string line;
    std::getline(log, line);  // the second load's own log: the first one was moved to the backup
    bool prepared = false;
    for (; std::getline(log, line);) {
        prepared = prepared || line.find("A crash will leave a dump at") != std::string::npos;
    }
    EXPECT_TRUE(prepared) << "The second load did not prepare its crash dump.";
}

TEST(InjectorCrashFilterTest, FreeingTheInjectorLeavesAFilterInstalledAfterItOnTop) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);

    HMODULE injector = LoadLibraryW(InjectorPath().c_str());
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    SetUnhandledExceptionFilter(&LaterFilter);

    FreeLibrary(injector);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter);
}

}
