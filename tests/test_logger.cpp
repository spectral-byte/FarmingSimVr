#include <gtest/gtest.h>
#include "core/logger.h"
#include <fstream>
#include <string>
#include <filesystem>

TEST(LoggerTest, WritesMessagesToFile) {
    const std::string testLogPath = "test_fs25_vr.log";
    if (std::filesystem::exists(testLogPath)) {
        std::filesystem::remove(testLogPath);
    }

    fs25vr::Logger::Instance().SetLogFile(testLogPath);
    fs25vr::Logger::Instance().Log(fs25vr::LogLevel::Info, "Testing logger initialization");
    fs25vr::Logger::Instance().Log(fs25vr::LogLevel::Warning, "Testing logger warning");
    fs25vr::Logger::Instance().Flush();

    ASSERT_TRUE(std::filesystem::exists(testLogPath));

    std::ifstream file(testLogPath);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("[INFO]"), std::string::npos);
    EXPECT_NE(content.find("Testing logger initialization"), std::string::npos);
    EXPECT_NE(content.find("[WARN]"), std::string::npos);
    EXPECT_NE(content.find("Testing logger warning"), std::string::npos);

    // Clean up
    file.close();
    fs25vr::Logger::Instance().Close();
    std::filesystem::remove(testLogPath);
    fs25vr::Logger::Instance().SetLogFile("fs25_vr.log");
}
