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

std::atomic<bool> g_installed{false};
std::atomic<Logger*> g_log{nullptr};
std::atomic<bool> g_crashSeen{false};

constexpr int kMaxFiltersPassedOn = 4;
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_filters[kMaxFiltersPassedOn];
std::atomic<int> g_filterCount{0};
thread_local int t_filtersPassedOnByThisThread = 0;

constexpr wchar_t kDumpFileName[] = L"fs25_vr_crash.dmp";

constexpr DWORD kDumpTimeoutMilliseconds = 10000;

constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>(kCrashDumpFlags);
static_assert(kCrashDumpFlags == (MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules),
              "kCrashDumpFlags, which the Installer's CustomDumpFlags must equal, names thread information, the memory the "
              "stacks point at and the unloaded modules.");

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                          PMINIDUMP_EXCEPTION_INFORMATION exception,
                                          PMINIDUMP_USER_STREAM_INFORMATION userStreams,
                                          PMINIDUMP_CALLBACK_INFORMATION callback);

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
std::atomic<bool> g_helperReady{false};

wchar_t g_dumpPath[1024];
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;
HANDLE g_dumpThread = nullptr;
HANDLE g_dumpRequested = nullptr;
HANDLE g_dumpFinished = nullptr;
MINIDUMP_EXCEPTION_INFORMATION g_dumpException;

enum class DumpResult { Pending, Written, CouldNotCreateFile, WriteFailed };
std::atomic<DumpResult> g_dumpResult{DumpResult::Pending};
std::atomic<DWORD> g_dumpResultError{0};
std::atomic<bool> g_failureReported{false};

constexpr std::size_t kStackNeededForTheLine = 32 * 1024;

bool HasStackFor(std::size_t bytes) {
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return reinterpret_cast<ULONG_PTR>(_AddressOfReturnAddress()) - low >= bytes;
}

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

using NtQueryVirtualMemoryFn = LONG(NTAPI*)(HANDLE process, PVOID address, int infoClass, PVOID info, SIZE_T length,
                                            PSIZE_T returned);
constexpr int kMemoryMappedFilenameInformation = 2;
std::atomic<NtQueryVirtualMemoryFn> g_ntQueryVirtualMemory{nullptr};
struct MappedFileName {
    UNICODE_STRING name;
    WCHAR characters[1024];
};
MappedFileName g_mappedFileName;

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

void AddDumpOutcome(CrashText& line, DumpState state) {
    if (state == DumpState::Ready) {
        line.Add("dump ");
        AddDumpFileName(line);
        return;
    }
    line.Add("no dump written: ");
    line.Add(NoDumpReason(state));
    if (state == DumpState::NoDbghelp || state == DumpState::NoHelperThread) {
        AddError(line, g_dumpPrepareError.load());
    }
}

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
    line.Add(", Injector ");
    line.Add(kInjectorVersion);
    line.Add(", ");
    AddDumpOutcome(line, state);
    log->TryLogWithoutWaiting(LogLevel::Error, line.text, line.length);
}

void ReportDumpFailure(DWORD waited) {
    Logger* log = g_log.load();
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
        DeleteFileW(g_dumpPath);
        g_dumpResult = DumpResult::WriteFailed;
    }
}

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

void HandTheCrashToTheHelper(EXCEPTION_POINTERS* info) {
    g_dumpException.ThreadId = GetCurrentThreadId();
    g_dumpException.ExceptionPointers = info;
    g_dumpException.ClientPointers = FALSE;
    SetEvent(g_dumpRequested);
    const DWORD waited = WaitForSingleObject(g_dumpFinished, kDumpTimeoutMilliseconds);
    if (waited != WAIT_OBJECT_0 && HasStackFor(kStackNeededForTheLine)) {
        ReportDumpFailure(waited);
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (!g_crashSeen.exchange(true)) {
        if (g_helperReady.load()) {
            HandTheCrashToTheHelper(info);
        } else if (HasStackFor(kStackNeededForTheLine)) {
            ReportCrash(info, GetCurrentThreadId(),
                        g_dumpState.load() == DumpState::NoHelperThread ? DumpState::NoHelperThread : DumpState::NotPrepared);
        }
    }
    return PassOn(info);
}

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

void PrepareTheDump(const std::wstring& path) {
    if (path.size() >= std::size(g_dumpPath)) {
        g_dumpState = DumpState::PathTooLong;
        LogWarn("Injector: A crash will leave no dump: the path " + std::filesystem::path(path).string() + " is too long.");
        return;
    }
    path.copy(g_dumpPath, path.size());
    g_dumpPath[path.size()] = L'\0';

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
    LoadLibraryExW((std::wstring(systemDirectory, length) + L"\\version.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!g_miniDumpWriteDump) {
        g_dumpState = DumpState::NoMiniDumpWriteDump;
        LogWarn(std::string("Injector: A crash will leave no dump: ") + NoDumpReason(DumpState::NoMiniDumpWriteDump) + ".");
        return;
    }
    g_dumpState = DumpState::Ready;
}

}

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
    Logger::Instance().Flush();
}

void RemoveCrashReporter() {
    if (!g_installed.exchange(false)) return;
    LPTOP_LEVEL_EXCEPTION_FILTER top =
        SetUnhandledExceptionFilter(g_filterCount.load() > 0 ? g_filters[0].load() : nullptr);
    if (top != &CrashFilter) {
        SetUnhandledExceptionFilter(top);
    }

    if (g_crashSeen.load()) return;
    g_filterCount = 0;

    g_helperReady = false;
    if (g_dumpThread) {
        TerminateThread(g_dumpThread, 0);
    }
    CloseHandleIfOpen(g_dumpThread);
    CloseHandleIfOpen(g_dumpRequested);
    CloseHandleIfOpen(g_dumpFinished);
    g_dumpState = DumpState::NotPrepared;
    g_dumpPreparing = false;
}

}
