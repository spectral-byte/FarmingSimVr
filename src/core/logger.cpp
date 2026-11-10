#include "logger.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace fs25vr {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

// Set FS25VR_LOG_LEVEL=debug in the game's environment to enable Debug messages.
constexpr const char* kLogLevelEnvVar = "FS25VR_LOG_LEVEL";

Logger::Logger() {
    m_minLevel = LevelFromEnvironment();
    SetLogFile(DefaultLogPath());
}

LogLevel Logger::LevelFromEnvironment() {
    char level[32];
    DWORD len = GetEnvironmentVariableA(kLogLevelEnvVar, level, sizeof(level));
    if (len == 0 || len >= sizeof(level)) {
        return LogLevel::Info;
    }
    return ParseLevel(level);
}

std::filesystem::path Logger::DefaultLogPath() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&Logger::DefaultLogPath), &module)) {
        // Can't resolve our own module; nullptr makes the lookup below fall back to the exe's directory.
        module = nullptr;
    }
    // Grow the buffer until the path fits, so long install paths aren't truncated.
    std::wstring modulePath(MAX_PATH, L'\0');
    while (true) {
        DWORD len = GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        if (len == 0) {
            return L"fs25_vr.log";
        }
        if (len < modulePath.size()) {
            modulePath.resize(len);
            break;
        }
        modulePath.resize(modulePath.size() * 2);
    }
    return std::filesystem::path(modulePath).parent_path() / L"fs25_vr.log";
}

std::filesystem::path Logger::BackupPathFor(const std::filesystem::path& filepath) {
    std::filesystem::path backup = filepath;
    backup += L".bak";
    return backup;
}

LogLevel Logger::ParseLevel(const char* name) {
    if (!name) {
        return LogLevel::Info;
    }
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "debug") return LogLevel::Debug;
    if (s == "warning" || s == "warn") return LogLevel::Warning;
    if (s == "error") return LogLevel::Error;
    return LogLevel::Info;
}

void Logger::SetMinLevel(LogLevel level) {
    m_minLevel = level;
}

LogLevel Logger::GetMinLevel() const {
    return m_minLevel;
}

Logger::~Logger() {
    Flush();
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

void Logger::SetLogFile(const std::filesystem::path& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
    m_filepath = filepath;
    if (m_filepath.empty()) {
        return;
    }
    std::ios::openmode mode = std::ios::out | std::ios::trunc;
    std::error_code ec;
    if (std::filesystem::exists(m_filepath, ec)) {
        // A single replacing move: if the old log is still held open (e.g. by a hung previous
        // session) this fails without touching either file, and we append rather than lose it.
        if (!MoveFileExW(m_filepath.c_str(), BackupPathFor(m_filepath).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            mode = std::ios::out | std::ios::app;
        }
    }
    m_fileStream.open(m_filepath, mode);
    if (!m_fileStream.is_open()) {
        OutputDebugStringW((L"[fs25_vr] Could not open log file " + m_filepath.wstring() + L"\n").c_str());
    }
}

void Logger::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

const char* Logger::LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "[DEBUG]";
        case LogLevel::Info:    return "[INFO] ";
        case LogLevel::Warning: return "[WARN] ";
        case LogLevel::Error:   return "[ERROR]";
        default:                return "[LOG]  ";
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < m_minLevel) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm buf;
    localtime_s(&buf, &in_time_t);

    char timePrefix[32];
    std::strftime(timePrefix, sizeof(timePrefix), "%Y-%m-%d %H:%M:%S", &buf);

    char header[64];
    int headerLen = snprintf(header, sizeof(header), "%s.%03lld %s ",
                             timePrefix, static_cast<long long>(ms.count()), LogLevelToString(level));

    std::string formatted;
    if (headerLen > 0) {
        formatted.reserve(static_cast<size_t>(headerLen) + message.size() + 1);
        formatted.append(header, static_cast<size_t>(headerLen));
    }
    formatted.append(message);
    formatted.push_back('\n');

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fileStream.is_open()) {
        m_fileStream << formatted;
        if (level == LogLevel::Warning || level == LogLevel::Error) {
            m_fileStream.flush();
        }
    }

    OutputDebugStringA(formatted.c_str());
}

namespace {

// Writes value as exactly `digits` decimal digits; returns the end of what it wrote.
char* PutDigits(char* out, unsigned value, int digits) {
    for (int i = digits - 1; i >= 0; --i) {
        out[i] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    return out + digits;
}

} // namespace

bool Logger::TryLogWithoutWaiting(LogLevel level, const char* message, std::size_t length) {
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !m_fileStream.is_open()) {
        return false;
    }

    // The layout of the lines Log writes, put together by hand: snprintf and localtime_s need more stack than a
    // thread that has just overflowed its stack has left, and the CRT's time functions take a lock of their own.
    SYSTEMTIME now;
    GetLocalTime(&now);
    char* out = m_emergencyLine;
    out = PutDigits(out, now.wYear, 4);
    *out++ = '-';
    out = PutDigits(out, now.wMonth, 2);
    *out++ = '-';
    out = PutDigits(out, now.wDay, 2);
    *out++ = ' ';
    out = PutDigits(out, now.wHour, 2);
    *out++ = ':';
    out = PutDigits(out, now.wMinute, 2);
    *out++ = ':';
    out = PutDigits(out, now.wSecond, 2);
    *out++ = '.';
    out = PutDigits(out, now.wMilliseconds, 3);
    *out++ = ' ';
    for (const char* tag = LogLevelToString(level); *tag; ++tag) {
        *out++ = *tag;
    }
    *out++ = ' ';
    const std::size_t room = sizeof(m_emergencyLine) - static_cast<std::size_t>(out - m_emergencyLine) - 1;
    const std::size_t kept = length < room ? length : room;
    for (std::size_t i = 0; i < kept; ++i) {
        *out++ = message[i];
    }
    *out++ = '\n';

    m_fileStream.write(m_emergencyLine, out - m_emergencyLine);
    m_fileStream.flush();
    return !m_fileStream.fail();
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
