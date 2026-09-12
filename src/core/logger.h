#pragma once

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

    void SetLogFile(const std::string& filepath);
    void Log(LogLevel level, const std::string& message);
    void Flush();
    void Close();

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string LogLevelToString(LogLevel level);

    std::ofstream m_fileStream;
    std::mutex m_mutex;
    std::string m_filepath;
};

void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);

} // namespace fs25vr
