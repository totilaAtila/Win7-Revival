#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace GlassBar {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance();

    void Initialize(const std::wstring& logFilePath);
    void Shutdown();

    void Log(LogLevel level, const std::string& message, const char* file, int line);
    void SetMinLevel(LogLevel level) { m_minLevel.store(level, std::memory_order_relaxed); }
    LogLevel GetMinLevel() const { return m_minLevel.load(std::memory_order_relaxed); }

    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();

    std::wofstream m_logFile;
    std::mutex m_mutex;
    bool m_initialized = false;
    std::atomic<LogLevel> m_minLevel { LogLevel::Info };  // Debug suppressed by default
    
    std::wstring GetTimestamp();
    std::wstring LevelToString(LogLevel level);
    std::wstring GetThreadId();
};

// Macro for convenient logging with file/line info
#define CF_LOG(level, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        GlassBar::Logger::Instance().Log( \
            GlassBar::LogLevel::level, \
            oss.str(), \
            __FILE__, \
            __LINE__ \
        ); \
    } while(0)

} // namespace GlassBar
