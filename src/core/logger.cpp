#include "logger.h"
#include <windows.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace fs25vr {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : m_filepath("fs25_vr.log") {
    SetLogFile(m_filepath);
}

Logger::~Logger() {
    Flush();
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

void Logger::SetLogFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
    m_filepath = filepath;
    if (!m_filepath.empty()) {
        m_fileStream.open(m_filepath, std::ios::out | std::ios::app);
    }
}

void Logger::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

std::string Logger::LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "[DEBUG]";
        case LogLevel::Info:    return "[INFO] ";
        case LogLevel::Warning: return "[WARN] ";
        case LogLevel::Error:   return "[ERROR]";
        default:                return "[LOG]  ";
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    struct tm buf;
    localtime_s(&buf, &in_time_t);

    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S.")
       << std::setfill('0') << std::setw(3) << ms.count() << " "
       << LogLevelToString(level) << " " << message << "\n";

    std::string formatted = ss.str();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream << formatted;
        if (level == LogLevel::Warning || level == LogLevel::Error) {
            m_fileStream.flush();
        }
    }

    OutputDebugStringA(formatted.c_str());
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream.flush();
    }
}

void LogDebug(const std::string& message) {
    Logger::Instance().Log(LogLevel::Debug, message);
}

void LogInfo(const std::string& message) {
    Logger::Instance().Log(LogLevel::Info, message);
}

void LogWarn(const std::string& message) {
    Logger::Instance().Log(LogLevel::Warning, message);
}

void LogError(const std::string& message) {
    Logger::Instance().Log(LogLevel::Error, message);
}

} // namespace fs25vr
