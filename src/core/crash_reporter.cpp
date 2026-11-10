#include "core/crash_reporter.h"

#include "core/injector_version.h"
#include "core/logger.h"

#include <windows.h>
#include <intrin.h>
#include <minidumpapiset.h>
#include <winternl.h>
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
std::atomic<bool> g_installed{false};
// The log, looked up while installing so that the filter never has to construct it inside a crashing process.
std::atomic<Logger*> g_log{nullptr};
// Set by the first crash to reach the filter; every later one goes straight to the previous filter.
std::atomic<bool> g_crashSeen{false};

// The filters the reporter passes a crash on to, the newest first: the one that was on top when the reporter was
// installed, and each one it found on top when it installed again. More than one, because a filter that took the top
// place after the reporter may have called the filter it replaced (the reporter) from its own, as filters that chain do.
// The reporter, installed again on top of it, passes the crash on to it, it calls the reporter, and the reporter would
// pass the crash on to it again, for ever. The way out is to count how many filters this thread has been passed on to:
// the reporter, reached again from inside one, passes on to the next older one, the filter that was below it before the
// later one came. Each filter is called once, and the chain ends when the list does.
constexpr int kMaxFiltersPassedOn = 4;
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_filters[kMaxFiltersPassedOn];
std::atomic<int> g_filterCount{0};
thread_local int t_filtersPassedOnByThisThread = 0;

// ---- The crash dump ----

// The dump sits next to fs25_vr.log under a fixed name, so that the next crash replaces it and the Installer's uninstall
// can remove it. The crash line names it.
constexpr wchar_t kDumpFileName[] = L"fs25_vr_crash.dmp";

// How long the filter waits for the helper thread. A dump of the whole game takes a few seconds at most; a helper that
// takes longer is stuck (a thread it needs is suspended or holds a lock), and the game's exit must not wait for it.
constexpr DWORD kDumpTimeoutMilliseconds = 10000;

// What goes into the dump: the exception record, every thread's stack, the module list (unloaded modules included) and
// the memory the stacks point at, but not the whole process. The Installer asks Windows for the same contents for the
// game's own dump (CustomDumpFlags in installer/fs25_vr.iss, see kCrashDumpFlags).
constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>(kCrashDumpFlags);
static_assert(kCrashDumpFlags == (MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules),
              "kCrashDumpFlags, which the Installer's CustomDumpFlags must equal, names thread information, the memory the "
              "stacks point at and the unloaded modules.");

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                          PMINIDUMP_EXCEPTION_INFORMATION exception,
                                          PMINIDUMP_USER_STREAM_INFORMATION userStreams,
                                          PMINIDUMP_CALLBACK_INFORMATION callback);

// Why there is, or is not, a dump.
enum class DumpState {
    NotPrepared,         // before the first DXGI factory call
    Ready,               // the helper thread writes the dump when a crash asks for it
    NoDbghelp,           // the system's dbghelp.dll could not be loaded
    NoMiniDumpWriteDump, // ... and has no MiniDumpWriteDump
    NoHelperThread,      // the events or the thread could not be created
    PathTooLong,         // the dump's path does not fit the buffer
};
std::atomic<DumpState> g_dumpState{DumpState::NotPrepared};
std::atomic<DWORD> g_dumpPrepareError{0};
std::atomic<bool> g_dumpPreparing{false};
// The helper thread waits for a crash. It does the crash line as well as the dump: the crashing thread may have next
// to no stack (after a stack overflow), and writing a log line takes more than it has. Without the helper (a crash
// before the first DXGI factory call) the crashing thread writes the line itself, if its stack allows.
std::atomic<bool> g_helperReady{false};

// Everything the helper needs is prepared ahead of the crash and kept in static storage: nothing is allocated, loaded or
// created while the process is crashing.
wchar_t g_dumpPath[1024];
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;
HANDLE g_dumpThread = nullptr;
HANDLE g_dumpRequested = nullptr;  // set by the filter: the process has crashed, write the line and the dump
HANDLE g_dumpFinished = nullptr;   // set by the helper: it is done
MINIDUMP_EXCEPTION_INFORMATION g_dumpException;

enum class DumpResult { Pending, Written, CouldNotCreateFile, WriteFailed };
std::atomic<DumpResult> g_dumpResult{DumpResult::Pending};
std::atomic<DWORD> g_dumpResultError{0};
std::atomic<bool> g_failureReported{false};

// The stack the crash line needs on the crashing thread, when there is no helper to write it. The log's write and flush
// run through the C runtime, and the first write allocates the file's buffer. Measured with a child that raises a stack
// overflow with a given margin (tests/test_crash_reporter.cpp): the line faulted with up to 16 KB left at the raise,
// the exception dispatcher's share of it included, and never with more. A filter that runs out of stack faults, and the
// crash is then reported as that fault instead of what it was; so a thread with less than this left writes no line.
// That is every thread after a real stack overflow, which leaves a few kilobytes: with no helper (before the first DXGI
// factory call) such a crash has no line, and Windows' own dump of the game is what is left.
constexpr std::size_t kStackNeededForTheLine = 32 * 1024;

// Whether the stack of this thread has at least this much room left below the code that asks.
bool HasStackFor(std::size_t bytes) {
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return reinterpret_cast<ULONG_PTR>(_AddressOfReturnAddress()) - low >= bytes;
}

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

// The name of the file that is mapped at an address: the query behind GetMappedFileName, without that function's heap
// buffer, which a crash in the heap would leave locked. Found while installing, since looking it up is not for the
// filter. The answer goes to static storage for the same reason as the text: it is long, and the stack may be short.
using NtQueryVirtualMemoryFn = LONG(NTAPI*)(HANDLE process, PVOID address, int infoClass, PVOID info, SIZE_T length,
                                            PSIZE_T returned);
constexpr int kMemoryMappedFilenameInformation = 2;
std::atomic<NtQueryVirtualMemoryFn> g_ntQueryVirtualMemory{nullptr};
struct MappedFileName {
    UNICODE_STRING name;
    WCHAR characters[1024];
};
MappedFileName g_mappedFileName;

// Names where the address lies as module+0xoffset, the form a dump or a map file is read with. Plain queries of the
// memory manager: GetModuleHandleEx and GetModuleFileName may wait for the loader lock, and a thread stuck in a load
// must not turn the crash into a hang.
void AddAddress(CrashText& line, const void* address) {
    MEMORY_BASIC_INFORMATION region{};
    std::size_t nameLength = 0;
    NtQueryVirtualMemoryFn query = g_ntQueryVirtualMemory.load();
    if (query && VirtualQuery(address, &region, sizeof(region)) == sizeof(region) && region.Type == MEM_IMAGE &&
        region.AllocationBase &&
        query(GetCurrentProcess(), region.AllocationBase, kMemoryMappedFilenameInformation, &g_mappedFileName,
              sizeof(g_mappedFileName), nullptr) >= 0 &&
        g_mappedFileName.name.Buffer) {
        nameLength = g_mappedFileName.name.Length / sizeof(WCHAR);
    }
    if (nameLength == 0) {
        line.Add("0x");
        line.AddHex(reinterpret_cast<std::uintptr_t>(address), 16);
        return;
    }
    // The path is the NT path of the image ("\Device\HarddiskVolume3\...\game.exe"): keep the file name.
    const wchar_t* path = g_mappedFileName.name.Buffer;
    std::size_t nameStart = 0;
    for (std::size_t i = 0; i < nameLength; ++i) {
        if (path[i] == L'\\') nameStart = i + 1;
    }
    for (std::size_t i = nameStart; i < nameLength; ++i) {
        line.AddChar(path[i] >= 0x20 && path[i] < 0x7F ? static_cast<char>(path[i]) : '?');
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

// Why a state has no dump, in the words that the crash line and the warning at start-up share. A literal, since the
// crash line is put together without the heap. Empty for the state that has a dump.
const char* NoDumpReason(DumpState state) {
    switch (state) {
        case DumpState::Ready: return "";
        case DumpState::NotPrepared: return "the crash came before the first DXGI factory call";
        case DumpState::NoDbghelp: return "the system's dbghelp.dll could not be loaded";
        case DumpState::NoMiniDumpWriteDump: return "the system's dbghelp.dll has no MiniDumpWriteDump";
        case DumpState::NoHelperThread: return "the dump helper thread could not be started";
        case DumpState::PathTooLong: return "the dump's path is too long";
    }
    return "";
}

// How the line ends: the dump's file name, or why there is no dump. The line is written before the dump, so that the
// log is complete whatever the dump does to the process; a dump that then fails or takes too long is told in a line of
// its own.
void AddDumpOutcome(CrashText& line, DumpState state) {
    if (state == DumpState::Ready) {
        line.Add("dump ");
        AddDumpFileName(line);
        return;
    }
    line.Add("no dump written: ");
    line.Add(NoDumpReason(state));
    // The two reasons that come from a failed call say which error it was.
    if (state == DumpState::NoDbghelp || state == DumpState::NoHelperThread) {
        AddError(line, g_dumpPrepareError.load());
    }
}

// Passes the crash on to the next filter of the list that this thread has not been passed on to: see g_filters. With
// none left, Windows continues its own handling.
LONG PassOn(EXCEPTION_POINTERS* info) {
    const int position = t_filtersPassedOnByThisThread;
    if (position >= g_filterCount.load()) return EXCEPTION_CONTINUE_SEARCH;
    LPTOP_LEVEL_EXCEPTION_FILTER next = g_filters[position].load();
    if (!next) return EXCEPTION_CONTINUE_SEARCH;
    t_filtersPassedOnByThisThread = position + 1;
    const LONG result = next(info);
    t_filtersPassedOnByThisThread = position;
    return result;
}

// Puts what SetUnhandledExceptionFilter found on top in front of the list, or empties the list when it found none:
// whoever removed the reporter removed the chain with it, and a filter that has been taken away may sit in a module that
// is gone. The crash then goes to Windows' own handling.
void RememberFilterFoundOnTop(LPTOP_LEVEL_EXCEPTION_FILTER found) {
    if (!found) {
        g_filterCount = 0;
        return;
    }
    LPTOP_LEVEL_EXCEPTION_FILTER list[kMaxFiltersPassedOn];
    int count = 0;
    list[count++] = found;
    const int known = g_filterCount.load();
    for (int i = 0; i < known && count < kMaxFiltersPassedOn; ++i) {
        LPTOP_LEVEL_EXCEPTION_FILTER filter = g_filters[i].load();
        if (filter != found) list[count++] = filter;
    }
    for (int i = 0; i < count; ++i) g_filters[i] = list[i];
    g_filterCount = count;
}

// The line that names the crash. Runs on the helper thread, or on the crashing thread when there is none.
void ReportCrash(const EXCEPTION_POINTERS* info, DWORD crashingThread, DumpState state) {
    Logger* log = g_log.load();
    if (!log || !info || !info->ExceptionRecord) return;

    CrashText& line = g_crashText;
    line.length = 0;
    line.Add("Injector: Crash: exception 0x");
    line.AddHex(info->ExceptionRecord->ExceptionCode, 8);
    line.Add(" at ");
    AddAddress(line, info->ExceptionRecord->ExceptionAddress);
    line.Add(" on thread ");
    line.AddDecimal(crashingThread);
    // Which symbols a dump or a map file of this crash is read with.
    line.Add(", Injector ");
    line.Add(kInjectorVersion);
    line.Add(", ");
    AddDumpOutcome(line, state);
    log->TryLogWithoutWaiting(LogLevel::Error, line.text, line.length);
}

// A dump that was announced in the crash line and then did not come: the line that follows says so. Runs on the helper
// thread, which knows what went wrong, or on the crashing thread when the helper never finished.
void ReportDumpFailure(DWORD waited) {
    Logger* log = g_log.load();
    // Once: a helper that finishes after the crashing thread gave up on it would tell its failure over the other's line.
    if (!log || g_failureReported.exchange(true)) return;

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

void WriteTheDump() {
    HANDLE file = CreateFileW(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        g_dumpResultError = GetLastError();
        g_dumpResult = DumpResult::CouldNotCreateFile;
        return;
    }
    const BOOL written = g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kDumpType, &g_dumpException,
                                             nullptr, nullptr);
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

// The helper thread. It waits from the first DXGI factory call until a crash wakes it, then does the work the crashing
// thread has no stack for: the crash line first, flushed, so that the log is complete whatever the dump does to the
// process; then the dump. Win32 calls only: it runs without the C runtime's per-thread state, and it must not touch the
// heap before a crash asks for it. It is ended with TerminateThread when the Injector unloads, since its code goes with
// the Injector (RemoveCrashReporter).
DWORD WINAPI DumpHelper(void*) {
    WaitForSingleObject(g_dumpRequested, INFINITE);

    const DumpState state = g_dumpState.load();
    ReportCrash(g_dumpException.ExceptionPointers, g_dumpException.ThreadId, state);
    if (state == DumpState::Ready) {
        WriteTheDump();
        if (g_dumpResult.load() != DumpResult::Written) {
            ReportDumpFailure(WAIT_OBJECT_0);
        }
    }
    SetEvent(g_dumpFinished);
    return 0;
}

// Hands the crash to the helper thread and waits for it, for no longer than the time limit. This is all the crashing
// thread does: a few calls that need next to no stack. The exception pointers stay valid while it waits, since they
// live on its stack.
void HandTheCrashToTheHelper(EXCEPTION_POINTERS* info) {
    g_dumpException.ThreadId = GetCurrentThreadId();
    g_dumpException.ExceptionPointers = info;
    g_dumpException.ClientPointers = FALSE;
    SetEvent(g_dumpRequested);
    const DWORD waited = WaitForSingleObject(g_dumpFinished, kDumpTimeoutMilliseconds);
    // The helper tells its own failures. One that never finished cannot, and the line is written here if there is room.
    if (waited != WAIT_OBJECT_0 && HasStackFor(kStackNeededForTheLine)) {
        ReportDumpFailure(waited);
    }
}

// The order of work: the crash line first, flushed, so that the log is complete whatever happens next; then (when
// there is one) the dump; then the previous filter. All of it on the helper thread when there is one.
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (!g_crashSeen.exchange(true)) {
        if (g_helperReady.load()) {
            HandTheCrashToTheHelper(info);
        } else if (HasStackFor(kStackNeededForTheLine)) {
            // Before the first DXGI factory call. The state cannot be Ready here, but it may say why there is no helper.
            ReportCrash(info, GetCurrentThreadId(),
                        g_dumpState.load() == DumpState::NoHelperThread ? DumpState::NoHelperThread : DumpState::NotPrepared);
        }
    }
    return PassOn(info);
}

// Preparing the dump failed at a call that has an error to report: the warning at start-up gives the reason that the
// crash line will give.
void FailPreparing(DumpState state, DWORD error) {
    g_dumpPrepareError = error;
    g_dumpState = state;
    LogWarn(std::string("Injector: A crash will leave no dump: ") + NoDumpReason(state) + " (error " + std::to_string(error) +
            ").");
}

void CloseHandleIfOpen(HANDLE& handle) {
    if (handle) CloseHandle(handle);
    handle = nullptr;
}

// Loads what the dump needs, by full path, and says whether it can be written.
void PrepareTheDump(const std::wstring& path) {
    if (path.size() >= std::size(g_dumpPath)) {
        g_dumpState = DumpState::PathTooLong;
        // Not NoDumpReason: the warning names the path, which the crash line has no room for.
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
        FailPreparing(DumpState::NoDbghelp, GetLastError());
        return;
    }
    // Writing the dump reads each module's version resource through version.dll's functions. In the game that name is
    // the Injector itself, which loads the system's version.dll on first use: load it now, since the crash must not
    // load a library. Kept loaded, as the Injector's own stubs keep it.
    LoadLibraryExW((std::wstring(systemDirectory, length) + L"\\version.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!g_miniDumpWriteDump) {
        g_dumpState = DumpState::NoMiniDumpWriteDump;
        LogWarn(std::string("Injector: A crash will leave no dump: ") + NoDumpReason(DumpState::NoMiniDumpWriteDump) + ".");
        return;
    }
    g_dumpState = DumpState::Ready;
}

} // namespace

void InstallCrashReporter() {
    g_log = &Logger::Instance();
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        g_ntQueryVirtualMemory = reinterpret_cast<NtQueryVirtualMemoryFn>(GetProcAddress(ntdll, "NtQueryVirtualMemory"));
    }
    LPTOP_LEVEL_EXCEPTION_FILTER found = SetUnhandledExceptionFilter(&CrashFilter);
    if (found != &CrashFilter) {
        RememberFilterFoundOnTop(found);
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

    PrepareTheDump((Logger::DefaultLogPath().parent_path() / kDumpFileName).wstring());

    // The helper thread is started whether or not a dump can be written: it also writes the crash line, which the
    // crashing thread may have no stack for.
    g_dumpRequested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_dumpFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_dumpRequested && g_dumpFinished) {
        g_dumpThread = CreateThread(nullptr, 0, &DumpHelper, nullptr, 0, nullptr);
    }
    if (!g_dumpThread) {
        const DWORD error = GetLastError();
        CloseHandleIfOpen(g_dumpRequested);
        CloseHandleIfOpen(g_dumpFinished);
        FailPreparing(DumpState::NoHelperThread, error);
        return;
    }
    g_helperReady = true;
    if (g_dumpState.load() == DumpState::Ready) {
        LogInfo("Injector: A crash will leave a dump at " + std::filesystem::path(g_dumpPath).string() + ".");
    }
    // The C runtime allocates the log file's buffer at the first write to it. Make that write now, so that the crash
    // line does not have to allocate while the process is crashing (and the lines still buffered reach the file).
    Logger::Instance().Flush();
}

void RemoveCrashReporter() {
    if (!g_installed.exchange(false)) return;
    // The only way to look at the top filter is to replace it: put the filter that was here before the reporter in its
    // place, and the one that was on top back if it was not the reporter.
    LPTOP_LEVEL_EXCEPTION_FILTER top =
        SetUnhandledExceptionFilter(g_filterCount.load() > 0 ? g_filters[0].load() : nullptr);
    if (top != &CrashFilter) {
        SetUnhandledExceptionFilter(top);
    }

    // Not while a crash is being handled: the process is going down then, and the helper may be writing the dump.
    if (g_crashSeen.load()) return;
    g_filterCount = 0;

    // The helper thread runs code of the Injector, which is about to be unloaded. It cannot be asked to leave: it would
    // have to run on until it exits, and a thread's exit needs the loader lock, which this thread holds (DllMain).
    // It is parked in a wait that holds nothing, so it is ended where it stands.
    g_helperReady = false;
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
