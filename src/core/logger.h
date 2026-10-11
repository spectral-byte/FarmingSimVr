#pragma once

#include <atomic>
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

    // Absolute path of fs25_vr.log in the directory of the module containing this code
    // (the Injector DLL in the game, the test executable under test).
    static std::filesystem::path DefaultLogPath();
    // Where the previous session's log is kept (a single backup).
    static std::filesystem::path BackupPathFor(const std::filesystem::path& filepath);
    // Parses "debug"/"info"/"warning"/"error" (case-insensitive); anything else yields Info.
    static LogLevel ParseLevel(const char* name);
    // The minimum level named by FS25VR_LOG_LEVEL, or Info when it is unset.
    static LogLevel LevelFromEnvironment();

    // Starts a new session at filepath: any existing file is moved to the backup path
    // (replacing the previous backup) and a fresh file is created. If the existing file
    // can't be moved (still held open elsewhere), it and the backup are left intact and
    // the new session is appended to it.
    void SetLogFile(const std::filesystem::path& filepath);
    void SetMinLevel(LogLevel level);
    LogLevel GetMinLevel() const;
    void Log(LogLevel level, const std::string& message);
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
    std::filesystem::path m_filepath;
    std::atomic<LogLevel> m_minLevel{LogLevel::Info};
};

void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);

} // namespace fs25vr
