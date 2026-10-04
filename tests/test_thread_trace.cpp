#include <gtest/gtest.h>
#include "core/logger.h"
#include "core/thread_trace.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <windows.h>

using fs25vr::ThreadTrace;

namespace {

const std::filesystem::path kTraceLogPath = "test_thread_trace.log";

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

size_t CountOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + 1)) {
        ++count;
    }
    return count;
}

// The whole log line containing needle, or "" when there is none.
std::string LineContaining(const std::string& content, const std::string& needle) {
    size_t pos = content.find(needle);
    if (pos == std::string::npos) return {};
    size_t begin = content.rfind('\n', pos);
    begin = (begin == std::string::npos) ? 0 : begin + 1;
    size_t end = content.find('\n', pos);
    return content.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

void RemoveLogAndBackup(const std::filesystem::path& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(fs25vr::Logger::BackupPathFor(path));
}

// Sends the Logger singleton to a private file for each test, then puts it back on its
// default file and level so later tests' output still reaches disk.
class ThreadTraceTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& logger = fs25vr::Logger::Instance();
        m_originalLevel = logger.GetMinLevel();
        logger.SetMinLevel(fs25vr::LogLevel::Info);
        RemoveLogAndBackup(kTraceLogPath);
        logger.SetLogFile(kTraceLogPath);
    }
    void TearDown() override {
        auto& logger = fs25vr::Logger::Instance();
        logger.Close();
        RemoveLogAndBackup(kTraceLogPath);
        logger.SetMinLevel(m_originalLevel);
        logger.SetLogFile(fs25vr::Logger::DefaultLogPath());
    }

    std::string LogContent() {
        fs25vr::Logger::Instance().Flush();
        return ReadAll(kTraceLogPath);
    }

private:
    fs25vr::LogLevel m_originalLevel = fs25vr::LogLevel::Info;
};

void TracedCallSite() {
    FS25VR_TRACE_THREAD("test-macro-site");
}

} // namespace

TEST_F(ThreadTraceTest, LogsFirstSightingOnly) {
    ThreadTrace trace("test-first");
    EXPECT_TRUE(trace.Record(100));
    EXPECT_FALSE(trace.Record(100));
    EXPECT_FALSE(trace.Record(100));

    const std::string content = LogContent();
    EXPECT_EQ(CountOccurrences(content, "[ThreadTrace] test-first"), 1u);
    const std::string line = LineContaining(content, "[ThreadTrace] test-first on thread 100");
    EXPECT_NE(line.find("[INFO]"), std::string::npos) << line;
}

TEST_F(ThreadTraceTest, FirstSightingIsOnDiskWithoutAnExplicitFlush) {
    ThreadTrace trace("test-durable");
    trace.Record(100);

    // Read without Logger::Flush: the trace flushes so a crash can't lose the line.
    EXPECT_NE(ReadAll(kTraceLogPath).find("test-durable on thread 100"), std::string::npos);
}

TEST_F(ThreadTraceTest, LogsThreadChangesAsWarningsNamingThePreviousThread) {
    ThreadTrace trace("test-change");
    EXPECT_TRUE(trace.Record(100));
    EXPECT_TRUE(trace.Record(200));
    EXPECT_FALSE(trace.Record(200));
    EXPECT_TRUE(trace.Record(100));

    const std::string content = LogContent();
    const std::string toOther = LineContaining(content, "[ThreadTrace] test-change on thread 200 (was 100)");
    const std::string back = LineContaining(content, "[ThreadTrace] test-change on thread 100 (was 200)");
    EXPECT_NE(toOther.find("[WARN]"), std::string::npos) << toOther;
    EXPECT_NE(back.find("[WARN]"), std::string::npos) << back;
}

TEST_F(ThreadTraceTest, AlternatingThreadsStopLoggingAfterTheCap) {
    ThreadTrace trace("test-alternating");
    EXPECT_TRUE(trace.Record(100));
    uint32_t logged = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        if (trace.Record(i % 2 == 0 ? 200 : 100)) ++logged;
    }

    // kMaxChangeLogs change lines plus one suppression notice, then silence.
    EXPECT_EQ(logged, ThreadTrace::kMaxChangeLogs + 1);
    const std::string content = LogContent();
    EXPECT_EQ(CountOccurrences(content, "test-alternating on thread"), 1u + ThreadTrace::kMaxChangeLogs);
    EXPECT_EQ(CountOccurrences(content, "test-alternating keeps changing threads"), 1u);
}

TEST_F(ThreadTraceTest, RecordsTheCallingThread) {
    ThreadTrace trace("test-calling");
    EXPECT_TRUE(trace.Record());
    EXPECT_FALSE(trace.Record());

    bool loggedFromOtherThread = false;
    std::thread other([&] { loggedFromOtherThread = trace.Record(); });
    other.join();
    EXPECT_TRUE(loggedFromOtherThread);

    EXPECT_NE(LogContent().find("test-calling on thread " + std::to_string(GetCurrentThreadId())),
              std::string::npos);
}

TEST_F(ThreadTraceTest, MacroKeepsOneTracerPerCallSite) {
    TracedCallSite();
    TracedCallSite();
    std::thread other(TracedCallSite);
    other.join();

    const std::string content = LogContent();
    EXPECT_EQ(CountOccurrences(content, "test-macro-site on thread"), 2u);
    EXPECT_EQ(CountOccurrences(content, "(was " + std::to_string(GetCurrentThreadId()) + ")"), 1u);
}
