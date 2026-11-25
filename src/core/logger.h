#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <fstream>
#include <mutex>

namespace fs25vr {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance();

    static std::filesystem::path DefaultLogPath();
    static std::filesystem::path BackupPathFor(const std::filesystem::path& filepath);
    static LogLevel ParseLevel(const char* name);
    static LogLevel LevelFromEnvironment();

    void SetLogFile(const std::filesystem::path& filepath);
    void SetMinLevel(LogLevel level);
    LogLevel GetMinLevel() const;
    void Log(LogLevel level, const std::string& message);
    bool TryLogWithoutWaiting(LogLevel level, const char* message, std::size_t length);
    void Flush();
    void Close();

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    const char* LogLevelToString(LogLevel level);

    std::ofstream m_fileStream;
    std::mutex m_mutex;
    char m_emergencyLine[512];
    std::filesystem::path m_filepath;
    std::atomic<LogLevel> m_minLevel{LogLevel::Info};
};

void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);

}
