// The built Injector (version.dll), loaded into the test process by its full path the way the game loads it from
// its x64 folder: its version.dll exports must behave as the system's do, and while it is loaded it must hook the
// system's dxgi.dll without breaking factory creation (ADR-0038).
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

// The names the system's version.dll exports (Windows 10 and 11).
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

} // namespace

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

    // The Injector's own version resource, read through its own exports.
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

    // Through the hook into the Injector's own runtime, which goes to Pass-Through Mode with no OpenXR runtime
    // (see test_process_isolation.cpp), and on to the system.
    auto create1 = reinterpret_cast<PFN_CreateDXGIFactory1>(m_createFactory1);
    ComPtr<IDXGIFactory1> factory;
    EXPECT_EQ(create1(IID_PPV_ARGS(&factory)), S_OK);
    EXPECT_NE(factory.Get(), nullptr);

    FreeLibrary(m_injector);
    m_injector = nullptr;
    EXPECT_EQ(FirstBytes(m_createFactory1), m_unhooked) << "Unloading the Injector left CreateDXGIFactory1 hooked.";
}

// A log alone should tell the developer which Injector the player runs, so the Injector's first line names its version.
TEST(InjectorLogTest, FirstLineNamesTheInjectorsVersion) {
    LetTheInjectorHaveItsLog theLog;
    const std::filesystem::path logPath = InjectorLogPath();

    HMODULE injector = LoadLibraryW(InjectorPath().c_str());
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    FreeLibrary(injector);  // detaching flushes the log and closes it

    std::ifstream log(logPath);
    ASSERT_TRUE(log.is_open()) << logPath;
    std::string firstLine;
    std::getline(log, firstLine);
    EXPECT_NE(firstLine.find(" " + std::string(kInjectorVersion) + " "), std::string::npos) << firstLine;
}

// Tests and tools that load and unload the Injector must find the process as they left it, the crash handling included.
TEST(InjectorCrashFilterTest, FreeingTheInjectorLeavesTheProcessesPreviousFilterInPlace) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);

    HMODULE injector = LoadLibraryW(InjectorPath().c_str());
    ASSERT_NE(injector, nullptr) << INJECTOR_DLL_PATH << " (error " << GetLastError() << ")";
    EXPECT_NE(TopUnhandledExceptionFilter(), &PreviousFilter) << "Loading the Injector did not install its filter.";

    FreeLibrary(injector);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &PreviousFilter);
}

// The game may install a filter of its own after the Injector loaded. The first DXGI factory call puts the Injector's
// filter on top of it again, and the game's becomes the one the Injector passes a crash on to, which unloading restores.
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

// The dump is prepared with a thread that is ended when the Injector unloads, since its code goes with it. Tests and
// tools that load the Injector again must find it working again.
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
        FreeLibrary(injector);  // detaching flushes the log and closes it
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
    SetUnhandledExceptionFilter(&LaterFilter);  // another module takes the place on top, and may have kept ours

    FreeLibrary(injector);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter);
}

} // namespace fs25vr
