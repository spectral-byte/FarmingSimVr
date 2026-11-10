#include "core/crash_reporter.h"

#include "core/injector_version.h"
#include "core/logger.h"

#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace fs25vr {

namespace {

// Pieces of state the filter reads, which it cannot be handed: the OS calls it with the exception alone.
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_previousFilter{nullptr};
std::atomic<bool> g_installed{false};
// The log, found while installing so that the filter never constructs it inside a crashing process (the Injector's
// first log line, written from DllMain, has already created it by then).
std::atomic<Logger*> g_log{nullptr};
// Set by the first crash to reach the filter; every later one goes straight to the previous filter.
std::atomic<bool> g_crashSeen{false};

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

// How the line ends: the dump's file name, or why there is no dump. Nothing writes a dump yet.
void AddDumpOutcome(CrashText& line) {
    line.Add("no dump written: the crash reporter does not write crash dumps yet");
}

LONG PassOn(EXCEPTION_POINTERS* info) {
    LPTOP_LEVEL_EXCEPTION_FILTER previous = g_previousFilter.load();
    return previous ? previous(info) : EXCEPTION_CONTINUE_SEARCH;
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

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
    if (!g_crashSeen.exchange(true)) {
        ReportCrash(info);
    }
    return PassOn(info);
}

} // namespace

void InstallCrashReporter() {
    g_log = &Logger::Instance();
    LPTOP_LEVEL_EXCEPTION_FILTER found = SetUnhandledExceptionFilter(&CrashFilter);
    if (found != &CrashFilter) {
        g_previousFilter = found;
    }
    g_installed = true;
}

void RemoveCrashReporter() {
    if (!g_installed.exchange(false)) return;
    // The only way to look at the top filter is to replace it: put ours' predecessor in its place, and the top one
    // back if it was not ours.
    LPTOP_LEVEL_EXCEPTION_FILTER top = SetUnhandledExceptionFilter(g_previousFilter.load());
    if (top != &CrashFilter) {
        SetUnhandledExceptionFilter(top);
    }
}

} // namespace fs25vr
