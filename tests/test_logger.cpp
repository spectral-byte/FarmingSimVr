#include <gtest/gtest.h>
#include "core/logger.h"
#include <fstream>
#include <string>
#include <filesystem>
#include <windows.h>

namespace {

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream file(path);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void WriteAll(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << content;
}

void RemoveLogAndBackup(const std::filesystem::path& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(fs25vr::Logger::BackupPathFor(path));
}

// The Logger is a process-wide singleton; put it back on its default file and level after
// each test so later tests' log output still reaches disk.
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override { m_originalLevel = fs25vr::Logger::Instance().GetMinLevel(); }
    void TearDown() override {
        auto& logger = fs25vr::Logger::Instance();
        logger.SetMinLevel(m_originalLevel);
        logger.SetLogFile(fs25vr::Logger::DefaultLogPath());
    }

private:
    fs25vr::LogLevel m_originalLevel = fs25vr::LogLevel::Info;
};

} // namespace

TEST_F(LoggerTest, DebugIsSuppressedByDefaultAndCanBeEnabled) {
    const std::string path = "test_level_filter.log";
    RemoveLogAndBackup(path);

    auto& logger = fs25vr::Logger::Instance();
    logger.SetLogFile(path);

    EXPECT_EQ(fs25vr::Logger::ParseLevel(nullptr), fs25vr::LogLevel::Info);

    logger.SetMinLevel(fs25vr::Logger::ParseLevel(nullptr));
    logger.Log(fs25vr::LogLevel::Debug, "hidden-debug");
    logger.Log(fs25vr::LogLevel::Info, "visible-info");

    logger.SetMinLevel(fs25vr::Logger::ParseLevel("debug"));
    logger.Log(fs25vr::LogLevel::Debug, "shown-debug");

    logger.SetMinLevel(fs25vr::LogLevel::Error);
    logger.Log(fs25vr::LogLevel::Warning, "hidden-warning");
    logger.Log(fs25vr::LogLevel::Error, "shown-error");
    logger.Flush();

    const std::string content = ReadAll(path);
    EXPECT_EQ(content.find("hidden-debug"), std::string::npos);
    EXPECT_NE(content.find("visible-info"), std::string::npos);
    EXPECT_NE(content.find("shown-debug"), std::string::npos);
    EXPECT_EQ(content.find("hidden-warning"), std::string::npos);
    EXPECT_NE(content.find("shown-error"), std::string::npos);

    logger.Close();
    RemoveLogAndBackup(path);
}

TEST_F(LoggerTest, LevelComesFromEnvironmentAndDefaultsToInfo) {
    char saved[32];
    const DWORD savedLen = GetEnvironmentVariableA("FS25VR_LOG_LEVEL", saved, sizeof(saved));

    SetEnvironmentVariableA("FS25VR_LOG_LEVEL", nullptr);
    EXPECT_EQ(fs25vr::Logger::LevelFromEnvironment(), fs25vr::LogLevel::Info);

    SetEnvironmentVariableA("FS25VR_LOG_LEVEL", "Debug");
    EXPECT_EQ(fs25vr::Logger::LevelFromEnvironment(), fs25vr::LogLevel::Debug);

    SetEnvironmentVariableA("FS25VR_LOG_LEVEL", "error");
    EXPECT_EQ(fs25vr::Logger::LevelFromEnvironment(), fs25vr::LogLevel::Error);

    const bool hadValue = savedLen > 0 && savedLen < sizeof(saved);
    SetEnvironmentVariableA("FS25VR_LOG_LEVEL", hadValue ? saved : nullptr);
}

TEST_F(LoggerTest, ParseLevelAcceptsKnownNamesCaseInsensitively) {
    using fs25vr::LogLevel;
    EXPECT_EQ(fs25vr::Logger::ParseLevel("DEBUG"), LogLevel::Debug);
    EXPECT_EQ(fs25vr::Logger::ParseLevel("info"), LogLevel::Info);
    EXPECT_EQ(fs25vr::Logger::ParseLevel("Warning"), LogLevel::Warning);
    EXPECT_EQ(fs25vr::Logger::ParseLevel("error"), LogLevel::Error);
    EXPECT_EQ(fs25vr::Logger::ParseLevel("nonsense"), LogLevel::Info);
    EXPECT_EQ(fs25vr::Logger::ParseLevel(""), LogLevel::Info);
}

TEST_F(LoggerTest, EachSessionStartsFreshAndKeepsOneBackup) {
    const std::string path = "test_rotation.log";
    RemoveLogAndBackup(path);
    auto& logger = fs25vr::Logger::Instance();

    logger.SetLogFile(path);
    logger.Log(fs25vr::LogLevel::Info, "session-one");
    logger.Close();

    logger.SetLogFile(path);
    logger.Log(fs25vr::LogLevel::Info, "session-two");
    logger.Close();

    logger.SetLogFile(path);
    logger.Log(fs25vr::LogLevel::Info, "session-three");
    logger.Close();

    const std::string current = ReadAll(path);
    const std::string backup = ReadAll(fs25vr::Logger::BackupPathFor(path));
    EXPECT_NE(current.find("session-three"), std::string::npos);
    EXPECT_EQ(current.find("session-two"), std::string::npos);
    EXPECT_NE(backup.find("session-two"), std::string::npos);
    EXPECT_EQ(backup.find("session-one"), std::string::npos);

    RemoveLogAndBackup(path);
}

TEST_F(LoggerTest, LogStillHeldOpenIsAppendedToAndBackupIsKept) {
    const std::filesystem::path path = "test_locked_rotation.log";
    RemoveLogAndBackup(path);
    WriteAll(fs25vr::Logger::BackupPathFor(path), "older-session\n");
    WriteAll(path, "previous-session\n");

    // Hold the log the way a still-running previous session would: shared for read/write
    // but not delete, so it can't be moved to the backup path.
    HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(held, INVALID_HANDLE_VALUE);

    auto& logger = fs25vr::Logger::Instance();
    logger.SetLogFile(path);
    logger.Log(fs25vr::LogLevel::Info, "new-session");
    logger.Close();
    CloseHandle(held);

    const std::string current = ReadAll(path);
    EXPECT_NE(current.find("previous-session"), std::string::npos);
    EXPECT_NE(current.find("new-session"), std::string::npos);
    EXPECT_NE(ReadAll(fs25vr::Logger::BackupPathFor(path)).find("older-session"), std::string::npos);

    RemoveLogAndBackup(path);
}

TEST_F(LoggerTest, DefaultLogPathIsBesideTheModuleNotTheWorkingDirectory) {
    const auto logPath = fs25vr::Logger::DefaultLogPath();
    ASSERT_TRUE(logPath.is_absolute());
    EXPECT_EQ(logPath.filename(), L"fs25_vr.log");

    wchar_t exe[MAX_PATH];
    ASSERT_GT(GetModuleFileNameW(nullptr, exe, MAX_PATH), 0u);
    EXPECT_EQ(logPath.parent_path(), std::filesystem::path(exe).parent_path());
}

TEST_F(LoggerTest, WritesMessagesToFile) {
    const std::string testLogPath = "test_fs25_vr.log";
    RemoveLogAndBackup(testLogPath);

    fs25vr::Logger::Instance().SetLogFile(testLogPath);
    fs25vr::Logger::Instance().Log(fs25vr::LogLevel::Info, "Testing logger initialization");
    fs25vr::Logger::Instance().Log(fs25vr::LogLevel::Warning, "Testing logger warning");
    fs25vr::Logger::Instance().Flush();

    ASSERT_TRUE(std::filesystem::exists(testLogPath));

    const std::string content = ReadAll(testLogPath);
    EXPECT_NE(content.find("[INFO]"), std::string::npos);
    EXPECT_NE(content.find("Testing logger initialization"), std::string::npos);
    EXPECT_NE(content.find("[WARN]"), std::string::npos);
    EXPECT_NE(content.find("Testing logger warning"), std::string::npos);

    fs25vr::Logger::Instance().Close();
    RemoveLogAndBackup(testLogPath);
}
