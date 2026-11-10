// The Injector's crash reporter, seen the way a crashed game is: a child process loads the built Injector (version.dll)
// by its full path, as the game does, and faults. The parent then reads what the child left next to the Injector.
//
// The child is an EXPECT_EXIT death test, the pattern of test_dxgi_proxy.cpp. Two things are particular to a crash:
//  - The fault is raised on a thread the child starts itself. GoogleTest catches the structured exceptions of the
//    thread that runs the test, so a fault there never reaches the process's unhandled exception filter.
//  - The child shows no error dialog: CI is unattended. The "game's" crash handling in the child is a filter that
//    ends the process with the exception's code, which is what Windows Error Reporting does after its own work.
#include <gtest/gtest.h>
#include "core/injector_version.h"
#include "core/logger.h"
#include "injector_binary.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs25vr {
namespace {

constexpr DWORD kAccessViolation = 0xC0000005;  // EXCEPTION_ACCESS_VIOLATION

// ---- The child ----

// Stands for the filter the game or Windows already has when the Injector loads. It reports on stderr what the
// Injector's filter handed it, then ends the process the way Windows Error Reporting does: with the exception's code
// and without unloading any DLL (an unload would flush the Injector's log and hide a missing flush).
LONG WINAPI GamesFilter(EXCEPTION_POINTERS* info) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    std::fprintf(stderr, "previous filter ran: code=%08lX rva=%llX thread=%lu\n", info->ExceptionRecord->ExceptionCode,
                 static_cast<unsigned long long>(address - base), GetCurrentThreadId());
    TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

__declspec(noinline) DWORD WINAPI WriteThroughANullPointer(void*) {
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    return 0;
}

// Never returns: the process ends in GamesFilter, or here when the fault did not reach it.
void CrashLikeTheGame() {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    SetUnhandledExceptionFilter(&GamesFilter);
    if (!LoadLibraryW(InjectorPath().c_str())) {
        std::fprintf(stderr, "could not load the Injector (error %lu)\n", GetLastError());
        ExitProcess(1);
    }
    HANDLE thread = CreateThread(nullptr, 0, &WriteThroughANullPointer, nullptr, 0, nullptr);
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

TEST_F(CrashReporterTest, AFilterInstalledBeforeTheInjectorStillRunsAfterItsWork) {
    EXPECT_EXIT(CrashLikeTheGame(), ::testing::ExitedWithCode(kAccessViolation), KeepingStderrIn(&m_childReport));

    // The child's filter ends the process, so a crash line in the log was written before it ran.
    EXPECT_NE(m_childReport.find("previous filter ran: code=C0000005"), std::string::npos) << m_childReport;
    const std::vector<std::string> log = Lines(LogPath());
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.back().find("Crash"), std::string::npos) << log.back();
}

} // namespace fs25vr
