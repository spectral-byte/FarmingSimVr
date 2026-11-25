#include <gtest/gtest.h>
#include "core/crash_reporter.h"
#include "core/detour.h"
#include "core/injector_version.h"
#include "core/logger.h"
#include "injector_binary.h"
#include "proxy/dxgi_proxy.h"
#include "proxy/mirror_window_controller.h"
#include <windows.h>
#include <dxgi.h>
#include <intrin.h>
#include <minidumpapiset.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs25vr {
namespace {

constexpr DWORD kAccessViolation = 0xC0000005;
constexpr DWORD kStackOverflow = 0xC00000FD;

__declspec(noinline) DWORD WINAPI WriteThroughANullPointer(void*) {
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    return 0;
}

#pragma warning(suppress : 4717)
__declspec(noinline) int RecurseUntilTheStackOverflows(int depth) {
    volatile char page[4096];
    page[0] = static_cast<char>(depth);
    return RecurseUntilTheStackOverflows(depth + 1) + page[0];
}

DWORD WINAPI OverflowTheStack(void*) {
    return static_cast<DWORD>(RecurseUntilTheStackOverflows(0));
}

HMODULE g_injector = nullptr;
thread_local bool t_faultWhenTheInjectorLogs = false;

__declspec(noinline) void WINAPI OutputDebugStringStandIn(LPCSTR) {
    HMODULE caller = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(_ReturnAddress()), &caller);
    if (t_faultWhenTheInjectorLogs && caller == g_injector) {
        std::fprintf(stderr, "faulting while the Injector holds its log lock\n");
        WriteThroughANullPointer(nullptr);
    }
}

DWORD WINAPI FaultWhileTheInjectorLogs(void*) {
    t_faultWhenTheInjectorLogs = true;
    IDXGIFactory1* factory = nullptr;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    std::fprintf(stderr, "the Injector did not log on this thread\n");
    return 0;
}

DWORD WINAPI EndTheChildIfItHangs(void*) {
    Sleep(30000);
    std::fprintf(stderr, "the child hung\n");
    TerminateProcess(GetCurrentProcess(), 99);
    return 0;
}

enum class FirstCall {
    EndsTheProcess,
    CrashesItself,
    CrashesAnotherThread,
};
FirstCall g_firstCall = FirstCall::EndsTheProcess;
std::atomic<int> g_filterCalls{0};

std::vector<HMODULE> LoadedModules() {
    std::vector<HMODULE> modules(256);
    DWORD needed = 0;
    while (K32EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                                 &needed) &&
           needed > modules.size() * sizeof(HMODULE)) {
        modules.resize(needed / sizeof(HMODULE) + 16);
    }
    modules.resize(needed / sizeof(HMODULE));
    std::sort(modules.begin(), modules.end());
    return modules;
}

std::vector<HMODULE> g_modulesBeforeTheFault;

void ReportModulesLoadedSinceTheFault() {
    const std::vector<HMODULE> now = LoadedModules();
    int loaded = 0;
    for (HMODULE module : now) {
        if (std::binary_search(g_modulesBeforeTheFault.begin(), g_modulesBeforeTheFault.end(), module)) continue;
        wchar_t path[MAX_PATH * 2];
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        std::fprintf(stderr, "module loaded since the fault: %s\n",
                     std::filesystem::path(std::wstring(path, length)).string().c_str());
        ++loaded;
    }
    std::fprintf(stderr, "modules loaded since the fault: %d\n", loaded);
}

LONG WINAPI GamesFilter(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        TerminateProcess(GetCurrentProcess(), EXCEPTION_STACK_OVERFLOW);
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const int call = ++g_filterCalls;
    std::fprintf(stderr, "previous filter ran: call=%d code=%08lX rva=%llX thread=%lu\n", call,
                 info->ExceptionRecord->ExceptionCode, static_cast<unsigned long long>(address - base),
                 GetCurrentThreadId());
    if (call == 1) ReportModulesLoadedSinceTheFault();
    if (call == 1 && g_firstCall == FirstCall::CrashesItself) {
        WriteThroughANullPointer(nullptr);
    } else if (call == 1 && g_firstCall == FirstCall::CrashesAnotherThread) {
        WaitForSingleObject(CreateThread(nullptr, 0, &WriteThroughANullPointer, nullptr, 0, nullptr), INFINITE);
    }
    TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

std::string MarkerText(const char* which) {
    return std::string("FS25VR-DUMP-MARKER-") + which + "-" + "END";
}

void* NewRegionWithMarker(const char* which) {
    char* region = static_cast<char*>(VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!region) return nullptr;
    char* at = region;
    for (const char* part : {"FS25VR-DUMP-MARKER-", which, "-", "END"}) {
        const size_t length = std::strlen(part);
        std::memcpy(at, part, length);
        at += length;
    }
    *at = '\0';
    return region;
}

void* g_regionOnNoStack = nullptr;

DWORD WINAPI PutTextInARegionOnNoStack(void*) {
    g_regionOnNoStack = NewRegionWithMarker("ON-NO-STACK");
    return 0;
}

__declspec(noinline) DWORD WINAPI WriteThroughANullPointerWithMemoryOnItsStack(void*) {
    char* volatile onThisStack = static_cast<char*>(NewRegionWithMarker("ON-A-STACK"));
    volatile int* nowhere = nullptr;
    *nowhere = onThisStack ? 1 : 2;
    return 0;
}

size_t g_stackToLeave = 0;

__declspec(noinline) int RunDownTheStackAndRaiseTheOverflow(int depth) {
    volatile char frame[256];
    frame[0] = static_cast<char>(depth);
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    const ULONG_PTR left = reinterpret_cast<ULONG_PTR>(&frame[0]) - low;
    if (left <= g_stackToLeave) {
        RaiseException(EXCEPTION_STACK_OVERFLOW, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 0;
    }
    return RunDownTheStackAndRaiseTheOverflow(depth + 1) + frame[0];
}

DWORD WINAPI RaiseTheOverflowWithLittleStackLeft(void*) {
    return static_cast<DWORD>(RunDownTheStackAndRaiseTheOverflow(0));
}

enum class Fault {
    WriteThroughANullPointer,
    WriteThroughANullPointerWithMemoryOnItsStack,
    WhileTheInjectorWritesItsLog,
    OverflowTheStack,
    OverflowTheStackWithGivenStackLeft,
};

LPTHREAD_START_ROUTINE ThreadThatFaults(Fault fault) {
    switch (fault) {
        case Fault::WhileTheInjectorWritesItsLog: return &FaultWhileTheInjectorLogs;
        case Fault::OverflowTheStack: return &OverflowTheStack;
        case Fault::OverflowTheStackWithGivenStackLeft: return &RaiseTheOverflowWithLittleStackLeft;
        case Fault::WriteThroughANullPointerWithMemoryOnItsStack: return &WriteThroughANullPointerWithMemoryOnItsStack;
        default: return &WriteThroughANullPointer;
    }
}

std::string PathOfDbghelpInThisProcess() {
    HMODULE dbghelp = GetModuleHandleW(L"dbghelp.dll");
    if (!dbghelp) return "not loaded";
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(dbghelp, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).string();
}

enum class DumpWriter {
    TheSystems,
    CountsTheCalls,
    NeverReturns,
};

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
FunctionDetour g_miniDumpWriteDump;

BOOL WINAPI CountingMiniDumpWriteDump(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                      PMINIDUMP_EXCEPTION_INFORMATION exception, PMINIDUMP_USER_STREAM_INFORMATION streams,
                                      PMINIDUMP_CALLBACK_INFORMATION callback) {
    std::fprintf(stderr, "MiniDumpWriteDump called\n");
    return reinterpret_cast<MiniDumpWriteDumpFn>(g_miniDumpWriteDump.GetOriginal())(process, processId, file, type,
                                                                                    exception, streams, callback);
}

BOOL WINAPI MiniDumpWriteDumpThatNeverReturns(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION) {
    std::fprintf(stderr, "MiniDumpWriteDump called and never returns\n");
    Sleep(INFINITE);
    return FALSE;
}

void StandInForTheDumpWriter(DumpWriter writer) {
    if (writer == DumpWriter::TheSystems) return;
    wchar_t systemDirectory[MAX_PATH];
    GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
    HMODULE dbghelp = LoadLibraryExW((std::wstring(systemDirectory) + L"\\dbghelp.dll").c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
    void* target = dbghelp ? reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    void* standIn = writer == DumpWriter::CountsTheCalls ? reinterpret_cast<void*>(&CountingMiniDumpWriteDump)
                                                         : reinterpret_cast<void*>(&MiniDumpWriteDumpThatNeverReturns);
    if (!target || !g_miniDumpWriteDump.Install(target, standIn)) {
        std::fprintf(stderr, "could not stand in for MiniDumpWriteDump\n");
        ExitProcess(1);
    }
}

LPTOP_LEVEL_EXCEPTION_FILTER g_replacedByTheLaterFilter = nullptr;

LONG WINAPI LaterFilterThatChains(EXCEPTION_POINTERS* info) {
    std::fprintf(stderr, "later filter ran, and chains\n");
    return g_replacedByTheLaterFilter ? g_replacedByTheLaterFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI LaterFilterThatDoesNotChain(EXCEPTION_POINTERS* info) {
    std::fprintf(stderr, "later filter ran, and does not chain\n");
    TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

enum class FilterInstalledLater {
    None,
    Chains,
    DoesNotChain,
};

FunctionDetour g_loadLibraryEx;

HMODULE WINAPI LoadLibraryExThatCannotLoadDbghelp(LPCWSTR name, HANDLE file, DWORD flags) {
    const std::wstring_view wanted = L"dbghelp.dll";
    const std::wstring_view given = name ? name : L"";
    if (given.size() >= wanted.size() &&
        _wcsicmp(given.substr(given.size() - wanted.size()).data(), wanted.data()) == 0) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return reinterpret_cast<decltype(&LoadLibraryExW)>(g_loadLibraryEx.GetOriginal())(name, file, flags);
}

struct ChildSetup {
    bool dbghelpCannotBeLoaded = false;
    FilterInstalledLater laterFilter = FilterInstalledLater::None;
    bool createsAFactory = false;
    DumpWriter dumpWriter = DumpWriter::TheSystems;
    bool unloadsAModule = false;
    size_t stackToLeave = 0;
};

ChildSetup AfterTheFirstFactory(DumpWriter writer = DumpWriter::TheSystems) {
    ChildSetup setup;
    setup.createsAFactory = true;
    setup.dumpWriter = writer;
    return setup;
}

ChildSetup AfterTheFirstFactoryWithoutDbghelp() {
    ChildSetup setup = AfterTheFirstFactory();
    setup.dbghelpCannotBeLoaded = true;
    return setup;
}

ChildSetup AfterTheFirstFactoryWithLittleStackLeft(size_t kilobytes) {
    ChildSetup setup = AfterTheFirstFactory();
    setup.stackToLeave = kilobytes * 1024;
    return setup;
}

ChildSetup BeforeAnyFactoryWithLittleStackLeft(size_t kilobytes) {
    ChildSetup setup;
    setup.stackToLeave = kilobytes * 1024;
    return setup;
}

constexpr const char* kUnloadedModuleName = "nvngx.dll";

void LoadAndFreeALibrary() {
    std::filesystem::path path = FAKE_NVNGX_PATH;
    HMODULE module = LoadLibraryW(path.make_preferred().c_str());
    if (!module || !FreeLibrary(module)) {
        std::fprintf(stderr, "could not load and free %s\n", kUnloadedModuleName);
        ExitProcess(1);
    }
}

void CrashLikeTheGame(Fault fault = Fault::WriteThroughANullPointer, FirstCall firstCall = FirstCall::EndsTheProcess,
                      ChildSetup setup = {}) {
    g_firstCall = firstCall;
    g_stackToLeave = setup.stackToLeave;
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    DWORD guardThreadId = 0;
    CreateThread(nullptr, 0, &EndTheChildIfItHangs, nullptr, 0, &guardThreadId);
    SetUnhandledExceptionFilter(&GamesFilter);
    if (setup.unloadsAModule) LoadAndFreeALibrary();
    g_injector = LoadLibraryW(InjectorPath().c_str());
    if (!g_injector) {
        std::fprintf(stderr, "could not load the Injector (error %lu)\n", GetLastError());
        ExitProcess(1);
    }
    StandInForTheDumpWriter(setup.dumpWriter);
    if (setup.dbghelpCannotBeLoaded &&
        !g_loadLibraryEx.Install(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryExW")),
                                 reinterpret_cast<void*>(&LoadLibraryExThatCannotLoadDbghelp))) {
        std::fprintf(stderr, "could not stand in for LoadLibraryExW\n");
        ExitProcess(1);
    }
    if (setup.laterFilter == FilterInstalledLater::Chains) {
        g_replacedByTheLaterFilter = SetUnhandledExceptionFilter(&LaterFilterThatChains);
    } else if (setup.laterFilter == FilterInstalledLater::DoesNotChain) {
        g_replacedByTheLaterFilter = SetUnhandledExceptionFilter(&LaterFilterThatDoesNotChain);
    }
    std::fprintf(stderr, "dbghelp after loading the Injector: %s\n", PathOfDbghelpInThisProcess().c_str());
    if (setup.createsAFactory) {
        IDXGIFactory1* factory = nullptr;
        const HRESULT created = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        std::fprintf(stderr, "factory created: hr=%08lX\n", static_cast<unsigned long>(created));
        std::fprintf(stderr, "dbghelp after the first factory call: %s\n", PathOfDbghelpInThisProcess().c_str());
    }
    FunctionDetour outputDebugString;
    if (fault == Fault::WhileTheInjectorWritesItsLog &&
        !outputDebugString.Install(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "OutputDebugStringA")),
            reinterpret_cast<void*>(&OutputDebugStringStandIn))) {
        std::fprintf(stderr, "could not stand in for OutputDebugStringA\n");
        ExitProcess(1);
    }
    if (fault == Fault::WriteThroughANullPointerWithMemoryOnItsStack) {
        WaitForSingleObject(CreateThread(nullptr, 0, &PutTextInARegionOnNoStack, nullptr, 0, nullptr), INFINITE);
    }
    std::fprintf(stderr, "threads: main=%lu guard=%lu\n", GetCurrentThreadId(), guardThreadId);
    g_modulesBeforeTheFault = LoadedModules();
    const bool smallStack = fault == Fault::OverflowTheStackWithGivenStackLeft;
    HANDLE thread = CreateThread(nullptr, smallStack ? 256 * 1024 : 0, ThreadThatFaults(fault), nullptr,
                                 smallStack ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, nullptr);
    WaitForSingleObject(thread, INFINITE);
    std::fprintf(stderr, "the fault did not end the process\n");
    ExitProcess(0);
}

class KeepStderr : public ::testing::MatcherInterface<const std::string&> {
public:
    explicit KeepStderr(std::string* kept) : m_kept(kept) {}
    bool MatchAndExplain(const std::string& text, ::testing::MatchResultListener*) const override {
        *m_kept = text;
        return true;
    }
    void DescribeTo(std::ostream* os) const override { *os << "is the child's stderr"; }
    void DescribeNegationTo(std::ostream* os) const override { *os << "is not the child's stderr"; }

private:
    std::string* m_kept;
};

::testing::Matcher<const std::string&> KeepingStderrIn(std::string* kept) {
    return ::testing::Matcher<const std::string&>(new KeepStderr(kept));
}

std::string LineOf(const std::string& report, const std::string& prefix) {
    const size_t at = report.find(prefix);
    if (at == std::string::npos) return {};
    const size_t from = at + prefix.size();
    std::string line = report.substr(from, report.find_first_of("\r\n", from) - from);
    return line;
}

std::filesystem::path SystemDirectory() {
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, static_cast<UINT>(std::size(directory)));
    return std::filesystem::path(std::wstring(directory, length));
}

std::filesystem::path DecoyDbghelpPath() {
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).parent_path() / L"dbghelp.dll";
}

std::string ValueOf(const std::string& report, const std::string& key) {
    const size_t at = report.find(key);
    if (at == std::string::npos) return {};
    const size_t from = at + key.size();
    return report.substr(from, report.find_first_of(" \r\n", from) - from);
}

std::string NameOfThisExecutable() {
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).filename().string();
}

std::vector<std::string> Lines(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

int Occurrences(const std::string& text, const std::string& part) {
    int count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++count;
    return count;
}

std::vector<unsigned char> BytesOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

class Minidump {
public:
    explicit Minidump(std::vector<unsigned char> bytes) : m_bytes(std::move(bytes)) {}

    bool HasSignature() const { return m_bytes.size() >= 4 && std::memcmp(m_bytes.data(), "MDMP", 4) == 0; }
    size_t Size() const { return m_bytes.size(); }

    std::vector<unsigned char> Stream(MINIDUMP_STREAM_TYPE type) const {
        for (const MINIDUMP_DIRECTORY& entry : Directory()) {
            if (entry.StreamType == static_cast<ULONG32>(type) &&
                static_cast<size_t>(entry.Location.Rva) + entry.Location.DataSize <= m_bytes.size()) {
                return std::vector<unsigned char>(m_bytes.begin() + entry.Location.Rva,
                                                  m_bytes.begin() + entry.Location.Rva + entry.Location.DataSize);
            }
        }
        return {};
    }
    bool Has(MINIDUMP_STREAM_TYPE type) const { return !Stream(type).empty(); }

    std::vector<std::pair<ULONG32, ULONG32>> ThreadsWithStackBytes() const {
        std::vector<std::pair<ULONG32, ULONG32>> threads;
        const std::vector<unsigned char> stream = Stream(ThreadListStream);
        if (stream.size() < sizeof(ULONG32)) return threads;
        ULONG32 count = 0;
        std::memcpy(&count, stream.data(), sizeof(count));
        for (ULONG32 i = 0; i < count; ++i) {
            const size_t at = sizeof(ULONG32) + i * sizeof(MINIDUMP_THREAD);
            if (at + sizeof(MINIDUMP_THREAD) > stream.size()) break;
            MINIDUMP_THREAD thread;
            std::memcpy(&thread, stream.data() + at, sizeof(thread));
            threads.emplace_back(thread.ThreadId, thread.Stack.Memory.DataSize);
        }
        return threads;
    }

    std::vector<std::wstring> UnloadedModuleNames() const {
        std::vector<std::wstring> names;
        const std::vector<unsigned char> stream = Stream(UnloadedModuleListStream);
        MINIDUMP_UNLOADED_MODULE_LIST list;
        if (stream.size() < sizeof(list)) return names;
        std::memcpy(&list, stream.data(), sizeof(list));
        for (ULONG32 i = 0; i < list.NumberOfEntries; ++i) {
            const size_t at = list.SizeOfHeader + static_cast<size_t>(i) * list.SizeOfEntry;
            if (at + sizeof(MINIDUMP_UNLOADED_MODULE) > stream.size()) break;
            MINIDUMP_UNLOADED_MODULE module;
            std::memcpy(&module, stream.data() + at, sizeof(module));
            names.push_back(StringAt(module.ModuleNameRva));
        }
        return names;
    }

    bool Contains(const std::string& text) const {
        return std::search(m_bytes.begin(), m_bytes.end(), text.begin(), text.end()) != m_bytes.end();
    }

    std::optional<MINIDUMP_EXCEPTION_STREAM> Exception() const {
        const std::vector<unsigned char> stream = Stream(ExceptionStream);
        if (stream.size() < sizeof(MINIDUMP_EXCEPTION_STREAM)) return std::nullopt;
        MINIDUMP_EXCEPTION_STREAM exception;
        std::memcpy(&exception, stream.data(), sizeof(exception));
        return exception;
    }

private:
    // A MINIDUMP_STRING: the length in bytes, then UTF-16 text.
    std::wstring StringAt(RVA rva) const {
        ULONG32 bytes = 0;
        if (static_cast<size_t>(rva) + sizeof(bytes) > m_bytes.size()) return {};
        std::memcpy(&bytes, m_bytes.data() + rva, sizeof(bytes));
        if (static_cast<size_t>(rva) + sizeof(bytes) + bytes > m_bytes.size()) return {};
        std::wstring text(bytes / sizeof(wchar_t), L'\0');
        std::memcpy(text.data(), m_bytes.data() + rva + sizeof(bytes), bytes);
        return text;
    }

    std::vector<MINIDUMP_DIRECTORY> Directory() const {
        std::vector<MINIDUMP_DIRECTORY> entries;
        MINIDUMP_HEADER header;
        if (!HasSignature() || m_bytes.size() < sizeof(header)) return entries;
        std::memcpy(&header, m_bytes.data(), sizeof(header));
        for (ULONG32 i = 0; i < header.NumberOfStreams; ++i) {
            const size_t at = static_cast<size_t>(header.StreamDirectoryRva) + i * sizeof(MINIDUMP_DIRECTORY);
            if (at + sizeof(MINIDUMP_DIRECTORY) > m_bytes.size()) break;
            MINIDUMP_DIRECTORY entry;
            std::memcpy(&entry, m_bytes.data() + at, sizeof(entry));
            entries.push_back(entry);
        }
        return entries;
    }

    std::vector<unsigned char> m_bytes;
};

int CrashLinesIn(const std::vector<std::string>& log) {
    int count = 0;
    for (const std::string& line : log) {
        if (line.find("Injector: Crash:") != std::string::npos) ++count;
    }
    return count;
}

class CrashReporterTest : public ::testing::Test {
protected:
    void SetUp() override { RemoveFilesOfTheChild(); }
    void TearDown() override { RemoveFilesOfTheChild(); }

    static std::filesystem::path LogPath() { return InjectorLogPath(); }

    static std::filesystem::path DumpPath() {
        return std::filesystem::path(InjectorPath()).parent_path() / L"fs25_vr_crash.dmp";
    }

    static void RemoveFilesOfTheChild() {
        std::error_code ignored;
        RemoveTheInjectorsLogFiles();
        std::filesystem::remove(DumpPath(), ignored);
        std::filesystem::remove(DecoyDbghelpPath(), ignored);
    }

    void RunCrash(Fault fault = Fault::WriteThroughANullPointer, FirstCall firstCall = FirstCall::EndsTheProcess,
                  ChildSetup setup = {}, DWORD endsWith = kAccessViolation) {
        EXPECT_EXIT(CrashLikeTheGame(fault, firstCall, setup), ::testing::ExitedWithCode(endsWith),
                    KeepingStderrIn(&m_childReport));
    }

    std::string LogLineFromTheEnd(size_t fromTheEnd) const {
        const std::vector<std::string> log = Lines(LogPath());
        return log.size() < fromTheEnd ? std::string() : log[log.size() - fromTheEnd];
    }
    std::string LastLogLine() const { return LogLineFromTheEnd(1); }

    ::testing::AssertionResult LastLogLineHas(const std::string& part) const { return LogLineFromTheEndHas(1, part); }
    ::testing::AssertionResult NextToLastLogLineHas(const std::string& part) const { return LogLineFromTheEndHas(2, part); }

    int CrashLinesInTheLog() const { return CrashLinesIn(Lines(LogPath())); }

    ::testing::AssertionResult LogLineFromTheEndHas(size_t fromTheEnd, const std::string& part) const {
        const std::string line = LogLineFromTheEnd(fromTheEnd);
        if (line.empty()) {
            return ::testing::AssertionFailure() << "The child left a log of fewer than " << fromTheEnd << " lines.\n"
                                                 << m_childReport;
        }
        if (line.find(part) == std::string::npos) {
            return ::testing::AssertionFailure() << "The log line " << fromTheEnd << " from the end, \"" << line
                                                 << "\", has no \"" << part << "\".";
        }
        return ::testing::AssertionSuccess() << "The log line " << fromTheEnd << " from the end: " << line;
    }

    LetTheInjectorHaveItsLog m_theLog;
    std::string m_childReport;
};

}

TEST_F(CrashReporterTest, ACrashLeavesACrashLineAtTheEndOfTheLog) {
    RunCrash();

    EXPECT_TRUE(LastLogLineHas("0xC0000005"));
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheModuleAndOffsetOfTheFaultingAddress) {
    RunCrash();

    const std::string rva = ValueOf(m_childReport, "rva=");
    ASSERT_FALSE(rva.empty()) << m_childReport;
    EXPECT_TRUE(LastLogLineHas(NameOfThisExecutable() + "+0x" + rva));
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheCrashingThread) {
    RunCrash();

    const std::string thread = ValueOf(m_childReport, "thread=");
    ASSERT_FALSE(thread.empty()) << m_childReport;
    EXPECT_TRUE(LastLogLineHas("thread " + thread));
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheInjectorsVersion) {
    RunCrash();

    EXPECT_TRUE(LastLogLineHas(" " + std::string(kInjectorVersion)));
}

TEST_F(CrashReporterTest, ACrashAfterTheFirstFactoryLeavesAMinidumpNextToTheInjector) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_GT(dump.Size(), 0u) << "No dump at " << DumpPath() << "\n" << m_childReport;
    EXPECT_TRUE(dump.HasSignature()) << "The file does not start with the minidump signature.";
}

TEST_F(CrashReporterTest, TheDumpsExceptionStreamNamesTheAccessViolationOnTheCrashingThread) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const Minidump dump(BytesOf(DumpPath()));
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = dump.Exception();
    ASSERT_TRUE(exception.has_value()) << "The dump has no exception stream.\n" << m_childReport;
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kAccessViolation);
    EXPECT_EQ(std::to_string(exception->ThreadId), ValueOf(m_childReport, "thread=")) << m_childReport;
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheDumpFile) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    EXPECT_TRUE(LastLogLineHas("Injector: Crash:"));
    EXPECT_TRUE(LastLogLineHas("fs25_vr_crash.dmp"));
    EXPECT_FALSE(LastLogLineHas("no dump written"));
}

TEST_F(CrashReporterTest, ACrashBeforeAnyFactoryCallLeavesTheCrashLineWithTheReasonAndNoDump) {
    RunCrash();

    EXPECT_TRUE(LastLogLineHas("no dump written: the crash came before the first DXGI factory call"));
    EXPECT_FALSE(std::filesystem::exists(DumpPath()));
}

TEST_F(CrashReporterTest, LogLinesWrittenBeforeTheCrashAreInTheFile) {
    RunCrash();

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_GE(log.size(), 2u);
    EXPECT_NE(log.front().find("Injector " + std::string(kInjectorVersion) + " attached to process"), std::string::npos)
        << log.front();
    EXPECT_TRUE(LastLogLineHas("Crash"));
}

TEST_F(CrashReporterTest, ACrashInsideTheReporterAddsNoSecondLineOrDumpAndStillEndsTheProcess) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::CrashesItself,
             AfterTheFirstFactory(DumpWriter::CountsTheCalls));

    EXPECT_NE(m_childReport.find("previous filter ran: call=1 code=C0000005"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesInTheLog(), 1);
    EXPECT_EQ(Occurrences(m_childReport, "MiniDumpWriteDump called"), 1) << m_childReport;
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature());
}

TEST_F(CrashReporterTest, ASecondCrashGoesStraightToThePreviousFilterAndAddsNoSecondLine) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::CrashesAnotherThread);

    EXPECT_NE(m_childReport.find("previous filter ran: call=2"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesInTheLog(), 1);
}

TEST_F(CrashReporterTest, AStackOverflowLeavesTheCrashLineToo) {
    RunCrash(Fault::OverflowTheStack, FirstCall::EndsTheProcess, AfterTheFirstFactory(), kStackOverflow);

    EXPECT_TRUE(LastLogLineHas("Injector: Crash: exception 0xC00000FD"));
}

TEST_F(CrashReporterTest, AStackOverflowBeforeTheFirstFactoryCallStillEndsAsTheOverflow) {
    RunCrash(Fault::OverflowTheStack, FirstCall::EndsTheProcess, {}, kStackOverflow);
    EXPECT_FALSE(std::filesystem::exists(DumpPath()));
}

TEST_F(CrashReporterTest, AStackOverflowLeavesADumpToo) {
    RunCrash(Fault::OverflowTheStack, FirstCall::EndsTheProcess, AfterTheFirstFactory(), kStackOverflow);

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_TRUE(dump.HasSignature()) << "No dump at " << DumpPath();
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = dump.Exception();
    ASSERT_TRUE(exception.has_value());
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kStackOverflow);
    EXPECT_TRUE(LastLogLineHas("fs25_vr_crash.dmp"));
}

TEST_F(CrashReporterTest, ASecondFaultWritesNoSecondDump) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::CrashesAnotherThread,
             AfterTheFirstFactory(DumpWriter::CountsTheCalls));

    EXPECT_NE(m_childReport.find("previous filter ran: call=2"), std::string::npos) << m_childReport;
    EXPECT_EQ(Occurrences(m_childReport, "MiniDumpWriteDump called"), 1) << m_childReport;
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = Minidump(BytesOf(DumpPath())).Exception();
    ASSERT_TRUE(exception.has_value()) << m_childReport;
    EXPECT_EQ(std::to_string(exception->ThreadId), ValueOf(m_childReport, "thread=")) << m_childReport;
}

TEST_F(CrashReporterTest, TheNextCrashingProcessReplacesTheDump) {
    {
        std::ofstream stale(DumpPath(), std::ios::binary);
        stale << "STALE DUMP OF AN EARLIER CRASH";
    }

    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const Minidump dump(BytesOf(DumpPath()));
    EXPECT_TRUE(dump.HasSignature()) << "The earlier dump is still there.";
    int dumps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(DumpPath().parent_path())) {
        if (entry.path().extension() == L".dmp") ++dumps;
    }
    EXPECT_EQ(dumps, 1) << "The dump is one file with a fixed name.";
}

ChildSetup AfterTheFirstFactoryWithALaterFilter(FilterInstalledLater later) {
    ChildSetup setup = AfterTheFirstFactory();
    setup.laterFilter = later;
    return setup;
}

TEST_F(CrashReporterTest, AFilterInstalledAfterTheInjectorDoesNotKeepTheCrashFromTheLogAndTheDump) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
             AfterTheFirstFactoryWithALaterFilter(FilterInstalledLater::DoesNotChain));

    EXPECT_NE(m_childReport.find("later filter ran, and does not chain"), std::string::npos) << m_childReport;
    EXPECT_TRUE(LastLogLineHas("Injector: Crash:"));
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature());
}

TEST_F(CrashReporterTest, AFilterThatChainedToTheReporterDoesNotMakeAChainThatLoops) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
             AfterTheFirstFactoryWithALaterFilter(FilterInstalledLater::Chains));

    EXPECT_EQ(Occurrences(m_childReport, "later filter ran, and chains"), 1) << m_childReport;
    EXPECT_EQ(Occurrences(m_childReport, "previous filter ran: call=1"), 1) << m_childReport;
    EXPECT_EQ(Occurrences(m_childReport, "previous filter ran: call=2"), 0) << m_childReport;
    EXPECT_EQ(CrashLinesInTheLog(), 1);
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature());
}

ChildSetup AfterTheFirstFactoryWithAnUnloadedModule() {
    ChildSetup setup = AfterTheFirstFactory();
    setup.unloadsAModule = true;
    return setup;
}

TEST_F(CrashReporterTest, TheDumpHoldsEveryThreadsStack) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const Minidump dump(BytesOf(DumpPath()));
    const auto threads = dump.ThreadsWithStackBytes();
    ASSERT_FALSE(threads.empty()) << "The dump has no thread list.\n" << m_childReport;
    for (const char* key : {"thread=", "main=", "guard="}) {
        const std::string id = ValueOf(m_childReport, key);
        ASSERT_FALSE(id.empty()) << key << "\n" << m_childReport;
        const auto found = std::find_if(threads.begin(), threads.end(),
                                        [&](const auto& thread) { return std::to_string(thread.first) == id; });
        ASSERT_NE(found, threads.end()) << "Thread " << key << id << " is not in the dump.";
        EXPECT_GT(found->second, 0u) << "The dump holds none of the stack of thread " << key << id;
    }
}

TEST_F(CrashReporterTest, TheDumpHoldsTheModuleListWithTheUnloadedModulesAndTheThreadInformation) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactoryWithAnUnloadedModule());

    const Minidump dump(BytesOf(DumpPath()));
    EXPECT_TRUE(dump.Has(ModuleListStream));
    EXPECT_TRUE(dump.Has(ThreadInfoListStream));
    ASSERT_TRUE(dump.Has(UnloadedModuleListStream)) << "The dump has no list of unloaded modules.";
    const auto unloaded = dump.UnloadedModuleNames();
    const bool namesTheLibrary = std::any_of(unloaded.begin(), unloaded.end(), [](const std::wstring& name) {
        return std::filesystem::path(name).filename() == kUnloadedModuleName;
    });
    EXPECT_TRUE(namesTheLibrary) << "The library the child freed is not among the " << unloaded.size() << " unloaded modules.";
}

TEST_F(CrashReporterTest, TheDumpHoldsTheMemoryTheStacksPointAtAndNotTheWholeProcess) {
    RunCrash(Fault::WriteThroughANullPointerWithMemoryOnItsStack, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_TRUE(dump.HasSignature()) << m_childReport;
    EXPECT_TRUE(dump.Has(MemoryListStream));
    EXPECT_TRUE(dump.Contains(MarkerText("ON-A-STACK"))) << "The memory a stack points at is not in the dump.";
    EXPECT_FALSE(dump.Contains(MarkerText("ON-NO-STACK"))) << "Memory that no stack points at is in the dump.";
    EXPECT_FALSE(dump.Has(Memory64ListStream)) << "The dump holds the whole process.";
    EXPECT_LT(dump.Size(), 8u * 1024 * 1024) << "The dump is " << dump.Size() / 1024 << " KB.";
    RecordProperty("DumpSizeBytes", static_cast<int>(dump.Size()));
}

TEST_F(CrashReporterTest, DbghelpIsLoadedAtTheFirstFactoryCallAndNotWhenTheInjectorLoads) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    EXPECT_EQ(LineOf(m_childReport, "dbghelp after loading the Injector: "), "not loaded") << m_childReport;
    EXPECT_NE(LineOf(m_childReport, "dbghelp after the first factory call: "), "not loaded") << m_childReport;
}

TEST_F(CrashReporterTest, DbghelpIsTheSystemsEvenWhenAnotherOneSitsNextToTheExe) {
    ASSERT_TRUE(std::filesystem::copy_file(FAKE_NVNGX_PATH, DecoyDbghelpPath(),
                                           std::filesystem::copy_options::overwrite_existing));

    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const std::string loaded = LineOf(m_childReport, "dbghelp after the first factory call: ");
    EXPECT_EQ(_stricmp(loaded.c_str(), (SystemDirectory() / L"dbghelp.dll").string().c_str()), 0) << loaded;
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature()) << "The system's dbghelp.dll did not write the dump.";
}

TEST_F(CrashReporterTest, TheCrashLoadsNoLibrary) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    EXPECT_NE(m_childReport.find("modules loaded since the fault: 0"), std::string::npos) << m_childReport;
}

TEST_F(CrashReporterTest, WithoutDbghelpTheCrashLineSaysWhyThereIsNoDump) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactoryWithoutDbghelp());

    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    EXPECT_TRUE(LastLogLineHas("no dump written: the system's dbghelp.dll could not be loaded (error 126)"));
    EXPECT_FALSE(std::filesystem::exists(DumpPath()));
}

TEST_F(CrashReporterTest, ADumpThatCannotBeCreatedIsToldInALineAfterTheCrashLine) {
    ASSERT_TRUE(std::filesystem::create_directory(DumpPath()));

    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    EXPECT_TRUE(NextToLastLogLineHas("Injector: Crash:"));
    EXPECT_TRUE(LastLogLineHas("Injector: Crash dump not written: could not create fs25_vr_crash.dmp (error "));
}

TEST_F(CrashReporterTest, AStuckDumpHelperDoesNotHangTheExitAndTheLogSaysThereIsNoDump) {
    const DWORD started = GetTickCount();
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
             AfterTheFirstFactory(DumpWriter::NeverReturns));
    const DWORD seconds = (GetTickCount() - started) / 1000;

    EXPECT_NE(m_childReport.find("MiniDumpWriteDump called and never returns"), std::string::npos) << m_childReport;
    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    EXPECT_LT(seconds, 25u) << "The crash took " << seconds << " s to end the process.";
    EXPECT_TRUE(NextToLastLogLineHas("Injector: Crash:"));
    EXPECT_TRUE(LastLogLineHas("Injector: Crash dump not written: the helper thread did not finish in 10 seconds"));
}

class CrashWithLittleStackLeftTest : public CrashReporterTest, public ::testing::WithParamInterface<size_t> {};

TEST_P(CrashWithLittleStackLeftTest, ADumpIsWrittenAndTheOverflowIsTheCrashReported) {
    RunCrash(Fault::OverflowTheStackWithGivenStackLeft, FirstCall::EndsTheProcess,
             AfterTheFirstFactoryWithLittleStackLeft(GetParam()), kStackOverflow);

    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = Minidump(BytesOf(DumpPath())).Exception();
    ASSERT_TRUE(exception.has_value()) << m_childReport;
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kStackOverflow);
    EXPECT_TRUE(LastLogLineHas("Injector: Crash: exception 0xC00000FD"));
}

TEST_P(CrashWithLittleStackLeftTest, ACrashBeforeTheFirstFactoryStillEndsAsTheOverflow) {
    constexpr size_t kAmpleKilobytes = 48;
    RunCrash(Fault::OverflowTheStackWithGivenStackLeft, FirstCall::EndsTheProcess,
             BeforeAnyFactoryWithLittleStackLeft(GetParam()), kStackOverflow);

    if (GetParam() >= kAmpleKilobytes) {
        EXPECT_TRUE(LastLogLineHas("Injector: Crash: exception 0xC00000FD"));
    }
}

INSTANTIATE_TEST_SUITE_P(KilobytesLeft, CrashWithLittleStackLeftTest,
                         ::testing::Values(3u, 4u, 5u, 6u, 8u, 10u, 12u, 14u, 16u, 20u, 24u, 32u, 48u, 64u));

TEST_F(CrashReporterTest, AFaultWhileTheCrashedThreadHoldsTheLogsLockSkipsTheLineAndStillLeavesTheDumpAndPassesTheCrashOn) {
    RunCrash(Fault::WhileTheInjectorWritesItsLog);

    EXPECT_NE(m_childReport.find("faulting while the Injector holds its log lock"), std::string::npos) << m_childReport;
    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesInTheLog(), 0);

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_TRUE(dump.HasSignature()) << "No dump at " << DumpPath() << "\n" << m_childReport;
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = dump.Exception();
    ASSERT_TRUE(exception.has_value()) << "The dump has no exception stream.\n" << m_childReport;
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kAccessViolation);
    EXPECT_EQ(std::to_string(exception->ThreadId), ValueOf(m_childReport, "thread=")) << m_childReport;
}

TEST_F(CrashReporterTest, ACrashInPassThroughModeLeavesTheSameCrashLineAndDump) {
    RunCrash(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory());

    const std::vector<std::string> log = Lines(LogPath());
    EXPECT_TRUE(std::any_of(log.begin(), log.end(), [](const std::string& line) {
        return line.find("Injector operating in Pass-Through Mode") != std::string::npos;
    })) << "The child is not in Pass-Through Mode.\n" << m_childReport;

    EXPECT_TRUE(LastLogLineHas("Injector: Crash: exception 0xC0000005"));
    EXPECT_TRUE(LastLogLineHas(NameOfThisExecutable() + "+0x" + ValueOf(m_childReport, "rva=")));
    EXPECT_TRUE(LastLogLineHas("Injector " + std::string(kInjectorVersion)));
    EXPECT_TRUE(LastLogLineHas("fs25_vr_crash.dmp"));
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = Minidump(BytesOf(DumpPath())).Exception();
    ASSERT_TRUE(exception.has_value()) << "The dump has no exception stream.\n" << m_childReport;
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kAccessViolation);
}

TEST_F(CrashReporterTest, AFilterInstalledBeforeTheInjectorStillRunsAfterItsWork) {
    RunCrash();

    EXPECT_NE(m_childReport.find("previous filter ran: call=1 code=C0000005"), std::string::npos) << m_childReport;
    EXPECT_TRUE(LastLogLineHas("Crash"));
}

namespace {

std::vector<std::array<unsigned char, 16>> StartsOfTheHookableFunctions() {
    std::vector<std::array<unsigned char, 16>> starts;
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    for (const char* name : {"LoadLibraryW", "LoadLibraryExW", "GetProcAddress", "SetUnhandledExceptionFilter"}) {
        starts.push_back(FirstBytes(reinterpret_cast<void*>(GetProcAddress(kernel32, name))));
    }
    for (const char* name : {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"}) {
        starts.push_back(FirstBytes(reinterpret_cast<void*>(GetProcAddress(LoadedSystemDxgi(), name))));
    }
    return starts;
}

}

TEST(CrashReporterInstallTest, InstallingTheReporterLoadsNoLibraryAndInstallsNoHook) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    Logger::Instance();
    const auto modulesBefore = LoadedModules();
    const auto startsBefore = StartsOfTheHookableFunctions();

    InstallCrashReporter();
    const LPTOP_LEVEL_EXCEPTION_FILTER top = TopUnhandledExceptionFilter();
    const auto modulesAfter = LoadedModules();
    const auto startsAfter = StartsOfTheHookableFunctions();
    RemoveCrashReporter();

    EXPECT_NE(top, &PreviousFilter) << "The reporter was not installed.";
    EXPECT_EQ(modulesAfter, modulesBefore);
    EXPECT_EQ(startsAfter, startsBefore);
}

TEST(CrashReporterInstallTest, InstallingAgainKeepsTheFilterItPassesCrashesOnTo) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);

    InstallCrashReporter();
    InstallCrashReporter();
    RemoveCrashReporter();

    EXPECT_EQ(TopUnhandledExceptionFilter(), &PreviousFilter);
}

TEST(CrashReporterInstallTest, AttachingToTheGamesSwapChainPutsTheReporterOnTopAgain) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    InstallCrashReporter();
    SetUnhandledExceptionFilter(&LaterFilter);
    DxgiProxy proxy(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

    proxy.AttachSwapChain(nullptr);
    const LPTOP_LEVEL_EXCEPTION_FILTER top = TopUnhandledExceptionFilter();
    RemoveCrashReporter();

    EXPECT_NE(top, &LaterFilter) << "The reporter was not put on top again.";
    EXPECT_NE(top, &PreviousFilter);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter) << "The filter found on top is not the one passed on to.";
}

TEST(CrashReporterInstallTest, AttachingToASwapChainInstallsNoReporterThatWasNotInstalled) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    DxgiProxy proxy(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

    proxy.AttachSwapChain(nullptr);

    EXPECT_EQ(TopUnhandledExceptionFilter(), &PreviousFilter);
}

TEST(CrashReporterInstallTest, AReinstallThatFindsNoFilterOnTopForgetsTheOnesFromBefore) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    InstallCrashReporter();
    SetUnhandledExceptionFilter(nullptr);

    ReinstallCrashReporter();
    RemoveCrashReporter();

    EXPECT_EQ(TopUnhandledExceptionFilter(), nullptr);
}

TEST(CrashReporterInstallTest, AFilterFoundOnTopWhenInstallingAgainBecomesTheOneItPassesCrashesOnTo) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    InstallCrashReporter();
    SetUnhandledExceptionFilter(&LaterFilter);

    InstallCrashReporter();
    RemoveCrashReporter();

    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter);
}

}
