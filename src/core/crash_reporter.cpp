#include "core/crash_reporter.h"

#include "core/injector_version.h"
#include "core/logger.h"

#include <windows.h>
#include <minidumpapiset.h>
#include <psapi.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>

namespace fs25vr {

namespace {

// Pieces of state the filter reads, which it cannot be handed: the OS calls it with the exception alone.
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_previousFilter{nullptr};
std::atomic<bool> g_installed{false};
// The log, looked up while installing so that the filter never has to construct it inside a crashing process.
std::atomic<Logger*> g_log{nullptr};
// Set by the first crash to reach the filter; every later one goes straight to the previous filter.
std::atomic<bool> g_crashSeen{false};
// Set while this thread is inside the previous filter. A filter that another module installed on top may have chained to
// ours (called the filter it replaced), and ours then finds it on top and calls it: the second call to ours on the same
// thread is that loop, and ends it.
thread_local bool t_passingOn = false;

// ---- The crash dump ----

// The dump sits next to fs25_vr.log under a fixed name, so that the next crash replaces it and the Installer's uninstall
// can remove it. The crash line names it.
constexpr wchar_t kDumpFileName[] = L"fs25_vr_crash.dmp";

// How long the filter waits for the helper thread. A dump of the whole game takes a few seconds at most; a helper that
// takes longer is stuck (a thread it needs is suspended or holds a lock), and the game's exit must not wait for it.
constexpr DWORD kDumpTimeoutMilliseconds = 10000;

// What goes into the dump: the exception record, every thread's stack, the module list (unloaded modules included) and
// the memory the stacks point at, but not the whole process. The Installer asks Windows for the same contents for the
// game's own dump (CustomDumpFlags 0x1060 in installer/fs25_vr.iss).
constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                                              MiniDumpWithUnloadedModules);
static_assert(kDumpType == 0x1060, "The Installer's CustomDumpFlags for Windows' own dump must name the same contents.");

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                          PMINIDUMP_EXCEPTION_INFORMATION exception,
                                          PMINIDUMP_USER_STREAM_INFORMATION userStreams,
                                          PMINIDUMP_CALLBACK_INFORMATION callback);

// Why there is, or is not, a dump to hand the crash to.
enum class DumpState {
    NotPrepared,         // before the first DXGI factory call
    Ready,               // the helper thread waits for a crash
    NoDbghelp,           // the system's dbghelp.dll could not be loaded
    NoMiniDumpWriteDump, // ... and has no MiniDumpWriteDump
    NoHelperThread,      // the events or the thread could not be created
    PathTooLong,         // the dump's path does not fit the buffer
};
std::atomic<DumpState> g_dumpState{DumpState::NotPrepared};
std::atomic<DWORD> g_dumpPrepareError{0};
std::atomic<bool> g_dumpPreparing{false};

// Everything the helper needs is prepared ahead of the crash and kept in static storage: nothing is allocated, loaded or
// created while the process is crashing.
wchar_t g_dumpPath[1024];
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;
HANDLE g_dumpThread = nullptr;
HANDLE g_dumpRequested = nullptr;  // set by the filter: write the dump now
HANDLE g_dumpFinished = nullptr;   // set by the helper: the dump is written, or could not be
MINIDUMP_EXCEPTION_INFORMATION g_dumpException;

enum class DumpResult { Pending, Written, CouldNotCreateFile, WriteFailed };
std::atomic<DumpResult> g_dumpResult{DumpResult::Pending};
std::atomic<DWORD> g_dumpResultError{0};

// Text put together without the heap: the filter may run in a process whose heap is what crashed, on a stack that has
// just overflowed. Used by one crash only, so it can live in static storage instead of on the stack.
struct CrashText {
    char text[400];
    std::size_t length = 0;

    void Add(const char* more) {
        while (*more && length < sizeof(text)) text[length++] = *more++;
    }
    void Add(std::string_view more) {
        for (char c : more) AddChar(c);
    }
    void AddChar(char c) {
        if (length < sizeof(text)) text[length++] = c;
    }
    void AddDecimal(std::uint64_t value) {
        char digits[20];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count > 0) AddChar(digits[--count]);
    }
    // Zero-padded to at least minDigits, upper case, without a prefix.
    void AddHex(std::uint64_t value, int minDigits) {
        char digits[16];
        int count = 0;
        do {
            digits[count++] = "0123456789ABCDEF"[value & 0xF];
            value >>= 4;
        } while (value != 0);
        while (count < minDigits && count < 16) digits[count++] = '0';
        while (count > 0 && length < sizeof(text)) text[length++] = digits[--count];
    }
};
CrashText g_crashText;

wchar_t g_mappedPath[MAX_PATH];

// Names where the address lies as module+0xoffset, the form a dump or a map file is read with. Both system calls
// are plain queries of the memory manager: GetModuleHandleEx and GetModuleFileName may wait for the loader lock,
// and a thread stuck in a load must not turn the crash into a hang.
void AddAddress(CrashText& line, const void* address) {
    MEMORY_BASIC_INFORMATION region;
    DWORD pathLength = 0;
    if (VirtualQuery(address, &region, sizeof(region)) == sizeof(region) && region.Type == MEM_IMAGE &&
        region.AllocationBase) {
        pathLength = K32GetMappedFileNameW(GetCurrentProcess(), region.AllocationBase, g_mappedPath,
                                           static_cast<DWORD>(std::size(g_mappedPath)));
    }
    if (pathLength == 0) {
        line.Add("0x");
        line.AddHex(reinterpret_cast<std::uintptr_t>(address), 16);
        return;
    }
    // The path is the NT path of the image ("\Device\HarddiskVolume3\...\game.exe"): keep the file name.
    DWORD nameStart = 0;
    for (DWORD i = 0; i < pathLength; ++i) {
        if (g_mappedPath[i] == L'\\') nameStart = i + 1;
    }
    for (DWORD i = nameStart; i < pathLength; ++i) {
        line.AddChar(g_mappedPath[i] >= 0x20 && g_mappedPath[i] < 0x7F ? static_cast<char>(g_mappedPath[i]) : '?');
    }
    line.Add("+0x");
    line.AddHex(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(region.AllocationBase), 1);
}


void AddDumpFileName(CrashText& line) {
    for (wchar_t c : std::wstring_view(kDumpFileName)) line.AddChar(static_cast<char>(c));
}

void AddError(CrashText& line, DWORD error) {
    line.Add(" (error ");
    line.AddDecimal(error);
    line.AddChar(')');
}

// How the line ends: the dump's file name, or why there is no dump. The line is written before the dump, so that the
// log is complete whatever the dump does to the process; a dump that then fails or takes too long is told in a line of
// its own (ReportDumpFailure).
void AddDumpOutcome(CrashText& line) {
    switch (g_dumpState.load()) {
        case DumpState::Ready:
            line.Add("dump ");
            AddDumpFileName(line);
            return;
        case DumpState::NotPrepared:
            line.Add("no dump written: the crash came before the first DXGI factory call");
            return;
        case DumpState::NoDbghelp:
            line.Add("no dump written: the system's dbghelp.dll could not be loaded");
            AddError(line, g_dumpPrepareError.load());
            return;
        case DumpState::NoMiniDumpWriteDump:
            line.Add("no dump written: the system's dbghelp.dll has no MiniDumpWriteDump");
            return;
        case DumpState::NoHelperThread:
            line.Add("no dump written: the dump helper thread could not be started");
            AddError(line, g_dumpPrepareError.load());
            return;
        case DumpState::PathTooLong:
            line.Add("no dump written: the dump's path is too long");
            return;
    }
}

// Calling the previous filter from inside ours is how a loop starts when a module on top chained to us: see t_passingOn.
LONG PassOn(EXCEPTION_POINTERS* info) {
    LPTOP_LEVEL_EXCEPTION_FILTER previous = g_previousFilter.load();
    if (!previous || t_passingOn) return EXCEPTION_CONTINUE_SEARCH;
    t_passingOn = true;
    const LONG result = previous(info);
    t_passingOn = false;
    return result;
}

void ReportCrash(const EXCEPTION_POINTERS* info) {
    Logger* log = g_log.load();
    if (!log || !info || !info->ExceptionRecord) return;

    CrashText& line = g_crashText;
    line.length = 0;
    line.Add("Injector: Crash: exception 0x");
    line.AddHex(info->ExceptionRecord->ExceptionCode, 8);
    line.Add(" at ");
    AddAddress(line, info->ExceptionRecord->ExceptionAddress);
    // The filter runs on the thread that faulted.
    line.Add(" on thread ");
    line.AddDecimal(GetCurrentThreadId());
    // Which symbols a dump or a map file of this crash is read with.
    line.Add(", Injector ");
    line.Add(kInjectorVersion);
    line.Add(", ");
    AddDumpOutcome(line);
    log->TryLogWithoutWaiting(LogLevel::Error, line.text, line.length);
}

// A dump that was announced in the crash line and then did not come: the line that follows says so.
void ReportDumpFailure(DWORD waited) {
    Logger* log = g_log.load();
    if (!log) return;

    CrashText& line = g_crashText;
    line.length = 0;
    line.Add("Injector: Crash dump not written: ");
    if (waited == WAIT_TIMEOUT) {
        line.Add("the helper thread did not finish in ");
        line.AddDecimal(kDumpTimeoutMilliseconds / 1000);
        line.Add(" seconds, so ");
        AddDumpFileName(line);
        line.Add(" may be incomplete");
    } else if (waited != WAIT_OBJECT_0) {
        line.Add("could not wait for the helper thread");
        AddError(line, GetLastError());
    } else if (g_dumpResult.load() == DumpResult::CouldNotCreateFile) {
        line.Add("could not create ");
        AddDumpFileName(line);
        AddError(line, g_dumpResultError.load());
    } else {
        line.Add("MiniDumpWriteDump failed");
        AddError(line, g_dumpResultError.load());
    }
    log->TryLogWithoutWaiting(LogLevel::Error, line.text, line.length);
}

// Hands the crash to the helper thread and waits for it, for no longer than the time limit. From this thread the dump
// cannot be written: after a stack overflow it has no stack left for MiniDumpWriteDump. The exception pointers stay
// valid while this thread waits, since they live on its stack.
void WriteCrashDump(EXCEPTION_POINTERS* info) {
    if (g_dumpState.load() != DumpState::Ready || !info) return;

    g_dumpException.ThreadId = GetCurrentThreadId();
    g_dumpException.ExceptionPointers = info;
    g_dumpException.ClientPointers = FALSE;
    SetEvent(g_dumpRequested);
    const DWORD waited = WaitForSingleObject(g_dumpFinished, kDumpTimeoutMilliseconds);
    if (waited == WAIT_OBJECT_0 && g_dumpResult.load() == DumpResult::Written) return;
    ReportDumpFailure(waited);
}

// The helper thread. Win32 calls only: it runs without the C runtime's per-thread state, and it must not touch the heap
// until a crash asks for the dump. It is ended with TerminateThread when the Injector unloads, since its code goes with
// the Injector (RemoveCrashReporter).
DWORD WINAPI DumpHelper(void*) {
    WaitForSingleObject(g_dumpRequested, INFINITE);

    HANDLE file = CreateFileW(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        g_dumpResultError = GetLastError();
        g_dumpResult = DumpResult::CouldNotCreateFile;
    } else {
        const BOOL written = g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kDumpType,
                                                 &g_dumpException, nullptr, nullptr);
        g_dumpResultError = written ? 0 : GetLastError();
        CloseHandle(file);
        if (written) {
            g_dumpResult = DumpResult::Written;
        } else {
            // Not a dump anyone could open: better no file than a misleading one.
            DeleteFileW(g_dumpPath);
            g_dumpResult = DumpResult::WriteFailed;
        }
    }
    SetEvent(g_dumpFinished);
    return 0;
}

// The order of work: the crash line first, flushed, so that the log is complete whatever happens next; then (when
// there is one) the dump; then the previous filter.
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (!g_crashSeen.exchange(true)) {
        ReportCrash(info);
        WriteCrashDump(info);
    }
    return PassOn(info);
}

void FailPreparing(DumpState state, DWORD error, const char* why) {
    g_dumpPrepareError = error;
    g_dumpState = state;
    LogWarn(std::string("Injector: A crash will leave no dump: ") + why + " (error " + std::to_string(error) + ").");
}

void CloseHandleIfOpen(HANDLE& handle) {
    if (handle) CloseHandle(handle);
    handle = nullptr;
}

} // namespace

void InstallCrashReporter() {
    g_log = &Logger::Instance();
    LPTOP_LEVEL_EXCEPTION_FILTER found = SetUnhandledExceptionFilter(&CrashFilter);
    if (found != &CrashFilter) {
        // Another filter, or none: when none, the earlier one is forgotten as well (see the header).
        g_previousFilter = found;
    }
    g_installed = true;
}

void ReinstallCrashReporter() {
    if (g_installed.load()) {
        InstallCrashReporter();
    }
}

void PrepareCrashDump() {
    if (!g_installed.load() || g_dumpPreparing.exchange(true)) return;
    ReinstallCrashReporter();

    const std::wstring path = (Logger::DefaultLogPath().parent_path() / kDumpFileName).wstring();
    if (path.size() >= std::size(g_dumpPath)) {
        g_dumpState = DumpState::PathTooLong;
        LogWarn("Injector: A crash will leave no dump: the path " + std::filesystem::path(path).string() + " is too long.");
        return;
    }
    path.copy(g_dumpPath, path.size());
    g_dumpPath[path.size()] = L'\0';

    // By the full path of the system directory, and with that directory alone for the library's own dependencies: a
    // dbghelp.dll or dbgcore.dll that sits next to the game's exe must never be the one that runs.
    wchar_t systemDirectory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
    HMODULE dbghelp = nullptr;
    if (length != 0 && length < std::size(systemDirectory)) {
        dbghelp = LoadLibraryExW((std::wstring(systemDirectory, length) + L"\\dbghelp.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!dbghelp) {
        FailPreparing(DumpState::NoDbghelp, GetLastError(), "the system's dbghelp.dll could not be loaded");
        return;
    }
    g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!g_miniDumpWriteDump) {
        g_dumpState = DumpState::NoMiniDumpWriteDump;
        LogWarn("Injector: A crash will leave no dump: the system's dbghelp.dll has no MiniDumpWriteDump.");
        return;
    }

    g_dumpRequested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_dumpFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_dumpRequested && g_dumpFinished) {
        g_dumpThread = CreateThread(nullptr, 0, &DumpHelper, nullptr, 0, nullptr);
    }
    if (!g_dumpThread) {
        const DWORD error = GetLastError();
        CloseHandleIfOpen(g_dumpRequested);
        CloseHandleIfOpen(g_dumpFinished);
        FailPreparing(DumpState::NoHelperThread, error, "the dump helper thread could not be started");
        return;
    }
    g_dumpState = DumpState::Ready;
    LogInfo("Injector: A crash will leave a dump at " + std::filesystem::path(path).string() + ".");
}

void RemoveCrashReporter() {
    if (!g_installed.exchange(false)) return;
    // The only way to look at the top filter is to replace it: put the filter that was here before the reporter in its
    // place, and the one that was on top back if it was not the reporter.
    LPTOP_LEVEL_EXCEPTION_FILTER top = SetUnhandledExceptionFilter(g_previousFilter.load());
    if (top != &CrashFilter) {
        SetUnhandledExceptionFilter(top);
    }

    // Not while a crash is being handled: the process is going down then, and the helper may be writing the dump.
    if (g_crashSeen.load()) return;

    // The helper thread runs code of the Injector, which is about to be unloaded. It cannot be asked to leave: it would
    // have to run on until it exits, and a thread's exit needs the loader lock, which this thread holds (DllMain).
    // It is parked in a wait that holds nothing, so it is ended where it stands.
    if (g_dumpThread) {
        TerminateThread(g_dumpThread, 0);
    }
    CloseHandleIfOpen(g_dumpThread);
    CloseHandleIfOpen(g_dumpRequested);
    CloseHandleIfOpen(g_dumpFinished);
    // The system's dbghelp.dll stays loaded: the library loader is locked here too.
    g_dumpState = DumpState::NotPrepared;
    g_dumpPreparing = false;
}

} // namespace fs25vr
