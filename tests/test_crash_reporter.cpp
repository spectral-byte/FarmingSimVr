// The Injector's crash reporter, seen the way a crashed game is: a child process loads the built Injector (version.dll)
// by its full path, as the game does, and faults. The parent then reads what the child left next to the Injector.
//
// The child is an EXPECT_EXIT death test, the pattern of test_dxgi_proxy.cpp. Two things are particular to a crash:
//  - The fault is raised on a thread the child starts itself. GoogleTest catches the structured exceptions of the
//    thread that runs the test, so a fault there never reaches the process's unhandled exception filter.
//  - The child shows no error dialog: CI is unattended. The "game's" crash handling in the child is a filter that
//    ends the process with the exception's code, which is what Windows Error Reporting does after its own work.
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

constexpr DWORD kAccessViolation = 0xC0000005;  // EXCEPTION_ACCESS_VIOLATION

// ---- The child ----

__declspec(noinline) DWORD WINAPI WriteThroughANullPointer(void*) {
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    return 0;
}

#pragma warning(suppress : 4717)  // recursive on all control paths: that is the point
__declspec(noinline) int RecurseUntilTheStackOverflows(int depth) {
    volatile char page[4096];
    page[0] = static_cast<char>(depth);
    return RecurseUntilTheStackOverflows(depth + 1) + page[0];
}

DWORD WINAPI OverflowTheStack(void*) {
    return static_cast<DWORD>(RecurseUntilTheStackOverflows(0));
}

// The log's lock cannot be taken from outside the Injector, but the Injector's Logger calls OutputDebugStringA while it
// holds it. Taking the place of that function (a hook in the child, with a FunctionDetour that never calls the
// original), the child faults inside it, on a call from the Injector: the crashed thread then holds the Injector's
// log lock, as a thread that crashes in the middle of writing a log line would.
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
    // Through the Injector's hook of the system's dxgi.dll, which logs on this thread.
    IDXGIFactory1* factory = nullptr;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    std::fprintf(stderr, "the Injector did not log on this thread\n");
    return 0;
}

// A crash handler that waits for something that never comes would hang the test. Ends the child instead, with a code
// that is not the exception's, so the test fails with a message.
DWORD WINAPI EndTheChildIfItHangs(void*) {
    Sleep(30000);
    std::fprintf(stderr, "the child hung\n");
    TerminateProcess(GetCurrentProcess(), 99);
    return 0;
}

// What the child's own filter does the first time the Injector's filter hands it a crash, before it ends the process.
enum class FirstCall {
    EndsTheProcess,
    CrashesItself,         // a crash inside the Injector's filter, which is on the stack
    CrashesAnotherThread,  // a second crash while the first is still being handled
};
FirstCall g_firstCall = FirstCall::EndsTheProcess;
std::atomic<int> g_filterCalls{0};

// Stands for the filter the game or Windows already has when the Injector loads. It reports on stderr what the
// Injector's filter handed it, then ends the process the way Windows Error Reporting does: with the exception's code
// and without unloading any DLL (an unload would flush the Injector's log and hide a missing flush).
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

// The modules the child had loaded just before it faulted, to see what loading the crash caused.
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
    // After a stack overflow the thread has a few kilobytes of stack left, which the Injector's work has partly used:
    // stdio would run out of it. The process ends at once, as the filters of the game and of Windows do.
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

// Two regions of text for the dump to find or not find: one that the faulting thread's stack points at (as a local variable
// in its frames does), and one that only a global points at. A dump has the memory the stacks point at, and not the whole
// process. The text is put together at run time, so that it is not in the executable's image either.
std::string MarkerText(const char* which) {
    return std::string("FS25VR-DUMP-MARKER-") + which + "-" + "END";
}

void* NewRegionWithText(const std::string& text) {
    void* region = VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (region) std::memcpy(region, text.c_str(), text.size() + 1);
    return region;
}

void* g_regionOnNoStack = nullptr;

// On a thread of its own that ends, so that no stack of the child still holds the region's address or its text.
DWORD WINAPI PutTextInARegionOnNoStack(void*) {
    g_regionOnNoStack = NewRegionWithText(MarkerText("ON-NO-STACK"));
    return 0;
}

__declspec(noinline) DWORD WINAPI WriteThroughANullPointerWithMemoryOnItsStack(void*) {
    char* volatile onThisStack = static_cast<char*>(NewRegionWithText(MarkerText("ON-A-STACK")));
    volatile int* nowhere = nullptr;
    *nowhere = onThisStack ? 1 : 2;
    return 0;
}

// How much stack the filter finds after a stack overflow depends on the operating system and the processor: the
// exception dispatcher saves the thread's context on the stack first, and that is a few kilobytes on a processor with
// wide vector registers, which leaves the filter next to nothing. The child takes the guesswork out: it runs down its
// own stack until exactly this much is left and raises the overflow there, so the filter has the same margin on every
// machine (the dispatcher's share comes out of it).
size_t g_stackToLeave = 0;

__declspec(noinline) int RunDownTheStackAndRaiseTheOverflow(int depth) {
    volatile char frame[256];
    frame[0] = static_cast<char>(depth);
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    const ULONG_PTR left = reinterpret_cast<ULONG_PTR>(&frame[0]) - low;
    if (left <= g_stackToLeave) {
        // Nothing that prints here: stdio needs more stack than this.
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

// Where this process loaded dbghelp.dll from, or "not loaded".
std::string PathOfDbghelpInThisProcess() {
    HMODULE dbghelp = GetModuleHandleW(L"dbghelp.dll");
    if (!dbghelp) return "not loaded";
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(dbghelp, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).string();
}

// The dump is written by the system's MiniDumpWriteDump, which the Injector looks up in the system's dbghelp.dll. The child
// can take its place, as it does for OutputDebugStringA above: to count how often the Injector asks for a dump, or to
// stand for a dump that never finishes (a thread it needs is suspended, a lock is held).
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

// The child loads the system's dbghelp.dll itself for this, so a test with a stand-in cannot tell where the Injector
// loads it.
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

// A filter that another module installs after the Injector's. It may call the filter it replaced, which is the Injector's
// (filters that chain do), or leave it alone and end the process itself.
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

// The Injector loads the system's dbghelp.dll with LoadLibraryExW. The child can make that fail, as it would on a machine
// where the library is missing or cannot be loaded.
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

// What else the child does between loading the Injector and faulting.
struct ChildSetup {
    bool dbghelpCannotBeLoaded = false;
    // Installed right after the Injector is loaded, so that the Injector's filter is not on top any more when the child
    // creates its DXGI factory and faults.
    FilterInstalledLater laterFilter = FilterInstalledLater::None;
    // The game creates its DXGI factory at start-up. The Injector prepares its crash dump at the first factory call, so
    // a child that never creates one stands for a crash before that point.
    bool createsAFactory = false;
    DumpWriter dumpWriter = DumpWriter::TheSystems;
    // A library the child loaded and freed again before the crash: what a dump's list of unloaded modules is for.
    bool unloadsAModule = false;
    // For Fault::OverflowTheStackWithGivenStackLeft: how much stack the faulting thread has left when it raises the
    // overflow.
    size_t stackToLeave = 0;
};

ChildSetup AfterTheFirstFactory(DumpWriter writer = DumpWriter::TheSystems) {
    ChildSetup setup;
    setup.createsAFactory = true;
    setup.dumpWriter = writer;
    return setup;
}

// A library that is in no process by itself and does nothing when it loads (the stand-in for nvngx.dll).
constexpr const char* kUnloadedModuleName = "nvngx.dll";

void LoadAndFreeALibrary() {
    std::filesystem::path path = FAKE_NVNGX_PATH;
    HMODULE module = LoadLibraryW(path.make_preferred().c_str());
    if (!module || !FreeLibrary(module)) {
        std::fprintf(stderr, "could not load and free %s\n", kUnloadedModuleName);
        ExitProcess(1);
    }
}

// Never returns: the process ends in GamesFilter, or here when the fault did not reach it.
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
        // Through the Injector's hook of the system's dxgi.dll.
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
    // A small reserve for the thread that runs down its stack: it gets there at once.
    const bool smallStack = fault == Fault::OverflowTheStackWithGivenStackLeft;
    HANDLE thread = CreateThread(nullptr, smallStack ? 256 * 1024 : 0, ThreadThatFaults(fault), nullptr,
                                 smallStack ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, nullptr);
    WaitForSingleObject(thread, INFINITE);
    std::fprintf(stderr, "the fault did not end the process\n");
    ExitProcess(0);
}

// ---- The parent ----

// The death test's matcher runs in the parent, on the text the child wrote to stderr. This one accepts any text and
// keeps it, so the test can read what the child reported.
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

// The rest of the line that starts with `prefix`, as the child wrote it.
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

// A dbghelp.dll next to the test executable, where a load by name would find it first.
std::filesystem::path DecoyDbghelpPath() {
    wchar_t path[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).parent_path() / L"dbghelp.dll";
}

// The text after `key` up to the next space, as the child's filter wrote it ("rva=1A2B" -> "1A2B").
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

// Just enough of the minidump format to read what a dump holds. Not dbghelp.dll: loading it into this process would
// hide whether the Injector loads it (and the tests that watch the loaded modules would depend on it).
class Minidump {
public:
    explicit Minidump(std::vector<unsigned char> bytes) : m_bytes(std::move(bytes)) {}

    bool HasSignature() const { return m_bytes.size() >= 4 && std::memcmp(m_bytes.data(), "MDMP", 4) == 0; }
    size_t Size() const { return m_bytes.size(); }

    // The stream's bytes, or an empty view when the dump has none of that type.
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

    // The threads in the thread list, each with the number of bytes of its stack that the dump holds.
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

    // The file names of the modules in the list of unloaded modules.
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

    // Whether the bytes of `text` are anywhere in the file.
    bool Contains(const std::string& text) const {
        return std::search(m_bytes.begin(), m_bytes.end(), text.begin(), text.end()) != m_bytes.end();
    }

    // The exception record in the exception stream.
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
    void SetUp() override {
        // The Injector loaded from the build folder writes the same fs25_vr.log as this process's own logger, which
        // holds it open. Let go of it and clear the way, as a game start finds the folder.
        Logger::Instance().Close();
        RemoveFilesOfTheChild();
    }

    void TearDown() override {
        RemoveFilesOfTheChild();
        Logger::Instance().SetLogFile(Logger::DefaultLogPath());
    }

    static std::filesystem::path LogPath() {
        return std::filesystem::path(InjectorPath()).parent_path() / L"fs25_vr.log";
    }

    // The dump has a fixed name (the Installer's uninstall removes the file by it), so the next crash replaces it.
    static std::filesystem::path DumpPath() {
        return std::filesystem::path(InjectorPath()).parent_path() / L"fs25_vr_crash.dmp";
    }

    static void RemoveFilesOfTheChild() {
        std::error_code ignored;
        std::filesystem::remove(LogPath(), ignored);
        std::filesystem::remove(Logger::BackupPathFor(LogPath()), ignored);
        std::filesystem::remove(DumpPath(), ignored);
        std::filesystem::remove(DecoyDbghelpPath(), ignored);
    }

    // What the child wrote to stderr.
    std::string m_childReport;
};

} // namespace

TEST_F(CrashReporterTest, ACrashLeavesACrashLineAtTheEndOfTheLog) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("0xC0000005"), std::string::npos) << log.back();
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheModuleAndOffsetOfTheFaultingAddress) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    // The offset the operating system reported, as the child's own filter saw it.
    const std::string rva = ValueOf(m_childReport, "rva=");
    ASSERT_FALSE(rva.empty()) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find(NameOfThisExecutable() + "+0x" + rva), std::string::npos) << log.back();
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheCrashingThread) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    // The filter runs on the thread that faulted, so the child's filter reports that thread's id.
    const std::string thread = ValueOf(m_childReport, "thread=");
    ASSERT_FALSE(thread.empty()) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("thread " + thread), std::string::npos) << log.back();
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheInjectorsVersion) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find(" " + std::string(kInjectorVersion)), std::string::npos) << log.back();
}

// A player attaches the dump to a report, so it has to be a real minidump, next to the log it belongs to.
TEST_F(CrashReporterTest, ACrashAfterTheFirstFactoryLeavesAMinidumpNextToTheInjector) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_GT(dump.Size(), 0u) << "No dump at " << DumpPath() << "\n" << m_childReport;
    EXPECT_TRUE(dump.HasSignature()) << "The file does not start with the minidump signature.";
}

TEST_F(CrashReporterTest, TheDumpsExceptionStreamNamesTheAccessViolationOnTheCrashingThread) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const Minidump dump(BytesOf(DumpPath()));
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = dump.Exception();
    ASSERT_TRUE(exception.has_value()) << "The dump has no exception stream.\n" << m_childReport;
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kAccessViolation);
    // The child's own filter saw the exception on the thread that faulted.
    EXPECT_EQ(std::to_string(exception->ThreadId), ValueOf(m_childReport, "thread=")) << m_childReport;
}

TEST_F(CrashReporterTest, TheCrashLineNamesTheDumpFile) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Injector: Crash:"), std::string::npos) << log.back();
    EXPECT_NE(log.back().find("fs25_vr_crash.dmp"), std::string::npos) << log.back();
    EXPECT_EQ(log.back().find("no dump written"), std::string::npos) << log.back();
}

// The dump needs the system's dbghelp.dll and a helper thread, prepared at the first DXGI factory call. A crash before
// that leaves the crash line, which says why there is no dump.
TEST_F(CrashReporterTest, ACrashBeforeAnyFactoryCallLeavesTheCrashLineWithTheReasonAndNoDump) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("no dump written: the crash came before the first DXGI factory call"), std::string::npos)
        << log.back();
    EXPECT_FALSE(std::filesystem::exists(DumpPath()));
}

// Info lines are buffered and only warnings and errors are flushed, so the Injector's start-up lines, which the child
// never flushed, reach the file only because the crash finishes the log.
TEST_F(CrashReporterTest, LogLinesWrittenBeforeTheCrashAreInTheFile) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_GE(log.size(), 2u);
    EXPECT_NE(log.front().find("Injector " + std::string(kInjectorVersion) + " attached to process"), std::string::npos)
        << log.front();
    EXPECT_NE(log.back().find("Crash"), std::string::npos) << log.back();
}

// The previous filter is called from inside the Injector's, so a crash in it is a crash inside the reporter. What the
// operating system does with a fault inside a top-level filter differs: some builds drop it, others hand it to the
// unhandled exception filter again, which calls the Injector's filter a second time on that thread (and, when it
// passes the crash on, the child's filter a second time). Both end the process, and a player finds the same afterwards:
// the earlier filter ran, there is one crash line and one dump, and the process ended with the first crash's code.
TEST_F(CrashReporterTest, ACrashInsideTheReporterAddsNoSecondLineOrDumpAndStillEndsTheProcess) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::CrashesItself,
                                 AfterTheFirstFactory(DumpWriter::CountsTheCalls)),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("previous filter ran: call=1 code=C0000005"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesIn(Lines(LogPath())), 1);
    EXPECT_EQ(Occurrences(m_childReport, "MiniDumpWriteDump called"), 1) << m_childReport;
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature());
}

// The second crash reaches the reporter while it is busy with the first one, and goes to the previous filter.
TEST_F(CrashReporterTest, ASecondCrashGoesStraightToThePreviousFilterAndAddsNoSecondLine) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::CrashesAnotherThread),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("previous filter ran: call=2"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesIn(Lines(LogPath())), 1);
}

// The worst crash: the thread has almost no stack left to run the filter on, so the helper thread writes the line.
TEST_F(CrashReporterTest, AStackOverflowLeavesTheCrashLineToo) {
    constexpr DWORD kStackOverflow = 0xC00000FD;  // EXCEPTION_STACK_OVERFLOW
    EXPECT_EXIT(CrashLikeTheGame(Fault::OverflowTheStack, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kStackOverflow), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Injector: Crash: exception 0xC00000FD"), std::string::npos) << log.back();
}

// Before the first DXGI factory call there is no helper thread, and the crashing thread has no stack for the line. The
// crash must still end as the stack overflow it was, not as a fault of the Injector's filter in the little stack left.
TEST_F(CrashReporterTest, AStackOverflowBeforeTheFirstFactoryCallStillEndsAsTheOverflow) {
    constexpr DWORD kStackOverflow = 0xC00000FD;  // EXCEPTION_STACK_OVERFLOW
    EXPECT_EXIT(CrashLikeTheGame(Fault::OverflowTheStack), ::testing::ExitedWithCode(kStackOverflow),
                KeepingStderrIn(&m_childReport));
    EXPECT_FALSE(std::filesystem::exists(DumpPath()));
}

// The reason the dump is written by a helper thread: the crashing thread has no stack left to run MiniDumpWriteDump on.
TEST_F(CrashReporterTest, AStackOverflowLeavesADumpToo) {
    constexpr DWORD kStackOverflow = 0xC00000FD;  // EXCEPTION_STACK_OVERFLOW
    EXPECT_EXIT(CrashLikeTheGame(Fault::OverflowTheStack, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kStackOverflow), KeepingStderrIn(&m_childReport));

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_TRUE(dump.HasSignature()) << "No dump at " << DumpPath();
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = dump.Exception();
    ASSERT_TRUE(exception.has_value());
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kStackOverflow);
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("fs25_vr_crash.dmp"), std::string::npos) << log.back();
}

// One dump per process: the first crash is the one that matters, and the second one is usually its consequence.
TEST_F(CrashReporterTest, ASecondFaultWritesNoSecondDump) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::CrashesAnotherThread,
                                 AfterTheFirstFactory(DumpWriter::CountsTheCalls)),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("previous filter ran: call=2"), std::string::npos) << m_childReport;
    EXPECT_EQ(Occurrences(m_childReport, "MiniDumpWriteDump called"), 1) << m_childReport;
    // The dump is still the first crash's: the thread the child's filter saw it on first.
    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = Minidump(BytesOf(DumpPath())).Exception();
    ASSERT_TRUE(exception.has_value()) << m_childReport;
    EXPECT_EQ(std::to_string(exception->ThreadId), ValueOf(m_childReport, "thread=")) << m_childReport;
}

// The file has a fixed name, so a dump never piles up next to the log, and the report is about the latest crash.
TEST_F(CrashReporterTest, TheNextCrashingProcessReplacesTheDump) {
    // What an earlier crash left behind.
    {
        std::ofstream stale(DumpPath(), std::ios::binary);
        stale << "STALE DUMP OF AN EARLIER CRASH";
    }

    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const Minidump dump(BytesOf(DumpPath()));
    EXPECT_TRUE(dump.HasSignature()) << "The earlier dump is still there.";
    int dumps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(DumpPath().parent_path())) {
        if (entry.path().extension() == L".dmp") ++dumps;
    }
    EXPECT_EQ(dumps, 1) << "The dump is one file with a fixed name.";
}

// ---- Installing the filter again ----
// The game may install an unhandled exception filter of its own after the Injector started. Then it, and not the
// Injector, gets the crash, and neither crash line nor dump is written. The Injector installs its filter again at the
// first DXGI factory call; what it finds on top becomes the filter it passes the crash on to.

ChildSetup AfterTheFirstFactoryWithALaterFilter(FilterInstalledLater later) {
    ChildSetup setup = AfterTheFirstFactory();
    setup.laterFilter = later;
    return setup;
}

TEST_F(CrashReporterTest, AFilterInstalledAfterTheInjectorDoesNotKeepTheCrashFromTheLogAndTheDump) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
                                 AfterTheFirstFactoryWithALaterFilter(FilterInstalledLater::DoesNotChain)),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    // The later filter is still called: it is the one the Injector passes the crash on to.
    EXPECT_NE(m_childReport.find("later filter ran, and does not chain"), std::string::npos) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Injector: Crash:"), std::string::npos) << log.back();
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature());
}

// A filter that chains calls the one it replaced, which is the Injector's. The Injector installs again over it and
// passes the crash on to it, so each calls the other: the chain must end, and no filter may be left out of it.
TEST_F(CrashReporterTest, AFilterThatChainedToTheReporterDoesNotMakeAChainThatLoops) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
                                 AfterTheFirstFactoryWithALaterFilter(FilterInstalledLater::Chains)),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_EQ(Occurrences(m_childReport, "later filter ran, and chains"), 1) << m_childReport;
    // The filter that was below the Injector's before the later one came is called too, once.
    EXPECT_EQ(Occurrences(m_childReport, "previous filter ran: call=1"), 1) << m_childReport;
    EXPECT_EQ(Occurrences(m_childReport, "previous filter ran: call=2"), 0) << m_childReport;
    EXPECT_EQ(CrashLinesIn(Lines(LogPath())), 1);
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature());
}

// ---- What the dump holds ----
// The contents the Installer asks Windows for in the game's own dump (installer/fs25_vr.iss): every thread's stack, the
// modules (the unloaded ones too), the exception and the thread information, and the memory the stacks point at, but
// not the whole process, so that a dump is small enough to attach to an issue once zipped.

ChildSetup AfterTheFirstFactoryWithAnUnloadedModule() {
    ChildSetup setup = AfterTheFirstFactory();
    setup.unloadsAModule = true;
    return setup;
}

TEST_F(CrashReporterTest, TheDumpHoldsEveryThreadsStack) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const Minidump dump(BytesOf(DumpPath()));
    const auto threads = dump.ThreadsWithStackBytes();
    ASSERT_FALSE(threads.empty()) << "The dump has no thread list.\n" << m_childReport;
    // The threads the child knows by id: the one that faulted, the one that waits for it, and the child's watchdog.
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
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
                                 AfterTheFirstFactoryWithAnUnloadedModule()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

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
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointerWithMemoryOnItsStack, FirstCall::EndsTheProcess,
                                 AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const Minidump dump(BytesOf(DumpPath()));
    ASSERT_TRUE(dump.HasSignature()) << m_childReport;
    EXPECT_TRUE(dump.Has(MemoryListStream));
    EXPECT_TRUE(dump.Contains(MarkerText("ON-A-STACK"))) << "The memory a stack points at is not in the dump.";
    EXPECT_FALSE(dump.Contains(MarkerText("ON-NO-STACK"))) << "Memory that no stack points at is in the dump.";
    EXPECT_FALSE(dump.Has(Memory64ListStream)) << "The dump holds the whole process.";
    // A dump of all the memory of this child is about 77 MB; the dump holds a few hundred kilobytes.
    EXPECT_LT(dump.Size(), 8u * 1024 * 1024) << "The dump is " << dump.Size() / 1024 << " KB.";
    RecordProperty("DumpSizeBytes", static_cast<int>(dump.Size()));
}

// dbghelp.dll loads another library, and a library cannot be loaded from DllMain or from a crashing thread: it is
// loaded at the first DXGI factory call, where the rest of the deferred start-up runs.
TEST_F(CrashReporterTest, DbghelpIsLoadedAtTheFirstFactoryCallAndNotWhenTheInjectorLoads) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_EQ(LineOf(m_childReport, "dbghelp after loading the Injector: "), "not loaded") << m_childReport;
    EXPECT_NE(LineOf(m_childReport, "dbghelp after the first factory call: "), "not loaded") << m_childReport;
}

// The path is the system directory's, and nothing next to the game's exe can take its place.
TEST_F(CrashReporterTest, DbghelpIsTheSystemsEvenWhenAnotherOneSitsNextToTheExe) {
    ASSERT_TRUE(std::filesystem::copy_file(FAKE_NVNGX_PATH, DecoyDbghelpPath(),
                                           std::filesystem::copy_options::overwrite_existing));

    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::string loaded = LineOf(m_childReport, "dbghelp after the first factory call: ");
    EXPECT_EQ(_stricmp(loaded.c_str(), (SystemDirectory() / L"dbghelp.dll").string().c_str()), 0) << loaded;
    EXPECT_TRUE(Minidump(BytesOf(DumpPath())).HasSignature()) << "The system's dbghelp.dll did not write the dump.";
}

// Everything the crash needs is loaded and started before it: the crash itself loads no library.
TEST_F(CrashReporterTest, TheCrashLoadsNoLibrary) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("modules loaded since the fault: 0"), std::string::npos) << m_childReport;
}

// With no dbghelp.dll there is no dump, but the helper thread still writes the crash line, and the line says why.
TEST_F(CrashReporterTest, WithoutDbghelpTheCrashLineSaysWhyThereIsNoDump) {
    ChildSetup setup = AfterTheFirstFactory();
    setup.dbghelpCannotBeLoaded = true;
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, setup),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("no dump written: the system's dbghelp.dll could not be loaded (error 126)"),
              std::string::npos)
        << log.back();
    EXPECT_FALSE(std::filesystem::exists(DumpPath()));
}

// A dump that cannot be created (the folder is read-only, the file is open in another program) is announced in the crash
// line, which comes first, and then told in a line of its own.
TEST_F(CrashReporterTest, ADumpThatCannotBeCreatedIsToldInALineAfterTheCrashLine) {
    // Not a file that can be replaced: a folder in its place.
    ASSERT_TRUE(std::filesystem::create_directory(DumpPath()));

    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess, AfterTheFirstFactory()),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_GE(log.size(), 2u);
    EXPECT_NE(log[log.size() - 2].find("Injector: Crash:"), std::string::npos) << log[log.size() - 2];
    EXPECT_NE(log.back().find("Injector: Crash dump not written: could not create fs25_vr_crash.dmp (error "),
              std::string::npos)
        << log.back();
}

// A helper that never finishes (a thread it needs is suspended, a lock is held) must not hang the game's exit: the
// filter gives up after its time limit, says so in the log, and passes the crash on.
TEST_F(CrashReporterTest, AStuckDumpHelperDoesNotHangTheExitAndTheLogSaysThereIsNoDump) {
    const DWORD started = GetTickCount();
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::EndsTheProcess,
                                 AfterTheFirstFactory(DumpWriter::NeverReturns)),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));
    const DWORD seconds = (GetTickCount() - started) / 1000;

    // Ended by the crash, passed on to the child's filter, not by the child's own watchdog (exit code 99 after 30 s).
    EXPECT_NE(m_childReport.find("MiniDumpWriteDump called and never returns"), std::string::npos) << m_childReport;
    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    EXPECT_LT(seconds, 25u) << "The crash took " << seconds << " s to end the process.";
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_GE(log.size(), 2u);
    EXPECT_NE(log.back().find("Injector: Crash dump not written: the helper thread did not finish in 10 seconds"),
              std::string::npos)
        << log.back();
}

// After a stack overflow the filter has next to no stack, and what it finds depends on the operating system and the
// processor. The Injector's filter must not fault in it, whatever it finds: a fault in the filter would hide the crash
// behind a second one. The child runs down its stack to a given margin, so that every machine tests the same.
class CrashWithLittleStackLeftTest : public CrashReporterTest, public ::testing::WithParamInterface<size_t> {};

TEST_P(CrashWithLittleStackLeftTest, ADumpIsWrittenAndTheOverflowIsTheCrashReported) {
    constexpr DWORD kStackOverflow = 0xC00000FD;  // EXCEPTION_STACK_OVERFLOW
    ChildSetup setup = AfterTheFirstFactory();
    setup.stackToLeave = GetParam() * 1024;
    EXPECT_EXIT(CrashLikeTheGame(Fault::OverflowTheStackWithGivenStackLeft, FirstCall::EndsTheProcess, setup),
                ::testing::ExitedWithCode(kStackOverflow), KeepingStderrIn(&m_childReport));

    const std::optional<MINIDUMP_EXCEPTION_STREAM> exception = Minidump(BytesOf(DumpPath())).Exception();
    ASSERT_TRUE(exception.has_value()) << m_childReport;
    EXPECT_EQ(exception->ExceptionRecord.ExceptionCode, kStackOverflow);
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Injector: Crash: exception 0xC00000FD"), std::string::npos) << log.back();
}

// Before the first factory call there is no helper thread, and the crashing thread writes the line itself. With little
// room it writes nothing, but it must end the crash as the overflow, not as a fault of its own; with room (the margin
// is well over what the line needs) the line is there.
TEST_P(CrashWithLittleStackLeftTest, ACrashBeforeTheFirstFactoryStillEndsAsTheOverflow) {
    constexpr DWORD kStackOverflow = 0xC00000FD;  // EXCEPTION_STACK_OVERFLOW
    constexpr size_t kAmpleKilobytes = 48;
    ChildSetup setup;
    setup.stackToLeave = GetParam() * 1024;
    EXPECT_EXIT(CrashLikeTheGame(Fault::OverflowTheStackWithGivenStackLeft, FirstCall::EndsTheProcess, setup),
                ::testing::ExitedWithCode(kStackOverflow), KeepingStderrIn(&m_childReport));

    if (GetParam() >= kAmpleKilobytes) {
        const std::vector<std::string> log = Lines(LogPath());
        ASSERT_FALSE(log.empty());
        EXPECT_NE(log.back().find("Injector: Crash: exception 0xC00000FD"), std::string::npos) << log.back();
    }
}

INSTANTIATE_TEST_SUITE_P(KilobytesLeft, CrashWithLittleStackLeftTest,
                         ::testing::Values(3u, 4u, 5u, 6u, 8u, 10u, 12u, 14u, 16u, 20u, 24u, 32u, 48u, 64u));

// The crashed thread may hold the log's lock (it crashed while writing a line), and then the filter must not wait for
// it: the line is skipped and the crash is still passed on.
TEST_F(CrashReporterTest, TheReporterNeverWaitsForTheLogsLockAndStillPassesTheCrashOn) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WhileTheInjectorWritesItsLog), ::testing::ExitedWithCode(kAccessViolation),
                KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("faulting while the Injector holds its log lock"), std::string::npos) << m_childReport;
    EXPECT_NE(m_childReport.find("previous filter ran: call=1"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesIn(Lines(LogPath())), 0);
}

TEST_F(CrashReporterTest, AFilterInstalledBeforeTheInjectorStillRunsAfterItsWork) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    // The child's filter ends the process, so a crash line in the log was written before it ran.
    EXPECT_NE(m_childReport.find("previous filter ran: call=1 code=C0000005"), std::string::npos) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Crash"), std::string::npos) << log.back();
}

namespace {

// The start of every function that a hook of the Injector's (the system's DXGI factories, the library loader) or of
// anything else would patch.
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

} // namespace

// DllMain installs the reporter while the loader lock is held, where loading a library or hooking a function can
// deadlock.
TEST(CrashReporterInstallTest, InstallingTheReporterLoadsNoLibraryAndInstallsNoHook) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    // The Injector's first log line, written before DllMain installs the reporter, has created the log by then. The
    // CRT loads a library of its own the first time the log is created (kernel.appcore.dll), which is not the
    // install's doing.
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

// The Injector installs the reporter again later (it may have been replaced), so that has to be harmless.
TEST(CrashReporterInstallTest, InstallingAgainKeepsTheFilterItPassesCrashesOnTo) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);

    InstallCrashReporter();
    InstallCrashReporter();
    RemoveCrashReporter();

    EXPECT_EQ(TopUnhandledExceptionFilter(), &PreviousFilter);
}

// The Injector attaches to the game's swap chain in Active VR Mode (the swap chain is only hooked there). A filter that
// took the top place since the first factory call is found then, and the reporter goes on top of it again.
TEST(CrashReporterInstallTest, AttachingToTheGamesSwapChainPutsTheReporterOnTopAgain) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    InstallCrashReporter();
    SetUnhandledExceptionFilter(&LaterFilter);  // the game installs a filter of its own
    DxgiProxy proxy(nullptr, nullptr, nullptr, nullptr, std::make_shared<MirrorWindowController>(), nullptr);

    proxy.AttachSwapChain(nullptr);
    const LPTOP_LEVEL_EXCEPTION_FILTER top = TopUnhandledExceptionFilter();
    RemoveCrashReporter();

    EXPECT_NE(top, &LaterFilter) << "The reporter was not put on top again.";
    EXPECT_NE(top, &PreviousFilter);
    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter) << "The filter found on top is not the one passed on to.";
}

// The proxies the tests of the DXGI proxy create must leave the process as they found it: only the Injector, which
// installs the reporter, has it put on top again.
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
    SetUnhandledExceptionFilter(nullptr);  // whoever did this took the chain with it

    ReinstallCrashReporter();
    RemoveCrashReporter();

    // Put back as nothing: Windows' own handling. The filter from before may sit in a module that is gone.
    EXPECT_EQ(TopUnhandledExceptionFilter(), nullptr);
}

TEST(CrashReporterInstallTest, AFilterFoundOnTopWhenInstallingAgainBecomesTheOneItPassesCrashesOnTo) {
    RestoreTheUnhandledExceptionFilter restore;
    SetUnhandledExceptionFilter(&PreviousFilter);
    InstallCrashReporter();
    SetUnhandledExceptionFilter(&LaterFilter);  // another module takes the top place

    InstallCrashReporter();
    RemoveCrashReporter();

    EXPECT_EQ(TopUnhandledExceptionFilter(), &LaterFilter);
}

} // namespace fs25vr
