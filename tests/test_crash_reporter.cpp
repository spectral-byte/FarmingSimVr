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
#include <windows.h>
#include <dxgi.h>
#include <intrin.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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
LONG WINAPI GamesFilter(EXCEPTION_POINTERS* info) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const int call = ++g_filterCalls;
    std::fprintf(stderr, "previous filter ran: call=%d code=%08lX rva=%llX thread=%lu\n", call,
                 info->ExceptionRecord->ExceptionCode, static_cast<unsigned long long>(address - base),
                 GetCurrentThreadId());
    if (call == 1 && g_firstCall == FirstCall::CrashesItself) {
        WriteThroughANullPointer(nullptr);
    } else if (call == 1 && g_firstCall == FirstCall::CrashesAnotherThread) {
        WaitForSingleObject(CreateThread(nullptr, 0, &WriteThroughANullPointer, nullptr, 0, nullptr), INFINITE);
    }
    TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

enum class Fault {
    WriteThroughANullPointer,
    WhileTheInjectorWritesItsLog,
    OverflowTheStack,
};

LPTHREAD_START_ROUTINE ThreadThatFaults(Fault fault) {
    switch (fault) {
        case Fault::WhileTheInjectorWritesItsLog: return &FaultWhileTheInjectorLogs;
        case Fault::OverflowTheStack: return &OverflowTheStack;
        default: return &WriteThroughANullPointer;
    }
}

// Never returns: the process ends in GamesFilter, or here when the fault did not reach it.
void CrashLikeTheGame(Fault fault = Fault::WriteThroughANullPointer, FirstCall firstCall = FirstCall::EndsTheProcess) {
    g_firstCall = firstCall;
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    CreateThread(nullptr, 0, &EndTheChildIfItHangs, nullptr, 0, nullptr);
    SetUnhandledExceptionFilter(&GamesFilter);
    g_injector = LoadLibraryW(InjectorPath().c_str());
    if (!g_injector) {
        std::fprintf(stderr, "could not load the Injector (error %lu)\n", GetLastError());
        ExitProcess(1);
    }
    FunctionDetour outputDebugString;
    if (fault == Fault::WhileTheInjectorWritesItsLog &&
        !outputDebugString.Install(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "OutputDebugStringA")),
            reinterpret_cast<void*>(&OutputDebugStringStandIn))) {
        std::fprintf(stderr, "could not stand in for OutputDebugStringA\n");
        ExitProcess(1);
    }
    HANDLE thread = CreateThread(nullptr, 0, ThreadThatFaults(fault), nullptr, 0, nullptr);
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

    static void RemoveFilesOfTheChild() {
        std::error_code ignored;
        std::filesystem::remove(LogPath(), ignored);
        std::filesystem::remove(Logger::BackupPathFor(LogPath()), ignored);
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

TEST_F(CrashReporterTest, TheCrashLineSaysThatNoDumpWasWritten) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("no dump written"), std::string::npos) << log.back();
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

// The previous filter is called from inside the Injector's, so a crash in it is a crash inside the reporter. Windows
// does not run the top-level filter again for a fault inside it (the child's first call is its only call), so what
// is left to see is that the process still ends with the first crash's code, in the time it takes, and that the
// reporter added nothing.
TEST_F(CrashReporterTest, ACrashInsideTheReporterAddsNoSecondLineAndStillEndsTheProcess) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::CrashesItself),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_EQ(CrashLinesIn(Lines(LogPath())), 1);
}

// The second crash reaches the reporter while it is busy with the first one, and goes to the previous filter.
TEST_F(CrashReporterTest, ASecondCrashGoesStraightToThePreviousFilterAndAddsNoSecondLine) {
    EXPECT_EXIT(CrashLikeTheGame(Fault::WriteThroughANullPointer, FirstCall::CrashesAnotherThread),
                ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    EXPECT_NE(m_childReport.find("previous filter ran: call=2"), std::string::npos) << m_childReport;
    EXPECT_EQ(CrashLinesIn(Lines(LogPath())), 1);
}

// The worst crash: the thread has almost no stack left to run the filter on.
TEST_F(CrashReporterTest, AStackOverflowLeavesTheCrashLineToo) {
    constexpr DWORD kStackOverflow = 0xC00000FD;  // EXCEPTION_STACK_OVERFLOW
    EXPECT_EXIT(CrashLikeTheGame(Fault::OverflowTheStack), ::testing::ExitedWithCode(kStackOverflow),
                KeepingStderrIn(&m_childReport));

    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Injector: Crash: exception 0xC00000FD"), std::string::npos) << log.back();
}

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
