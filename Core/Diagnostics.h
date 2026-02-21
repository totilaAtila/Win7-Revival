#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <atomic>

namespace CrystalFrame {

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

    // Minimum level to actually write (default: Warning — silences Debug/Info spam).
    // Call SetMinLevel(LogLevel::Debug) to enable full verbose output.
    void     SetMinLevel(LogLevel level) { m_minLevel.store(static_cast<int>(level)); }
    LogLevel GetMinLevel() const         { return static_cast<LogLevel>(m_minLevel.load()); }

    bool IsEnabled(LogLevel level) const {
        return static_cast<int>(level) >= m_minLevel.load();
    }

    void Log(LogLevel level, const std::string& message, const char* file, int line);

    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();

    std::wofstream m_logFile;
    std::mutex m_mutex;
    bool m_initialized = false;
    // Default: Warning — hides Debug/Info hot-path messages in normal operation.
    // Diagnostic builds may lower this to Debug via SetMinLevel().
    std::atomic<int> m_minLevel{ static_cast<int>(LogLevel::Warning) };

    std::wstring GetTimestamp();
    std::wstring LevelToString(LogLevel level);
    std::wstring GetThreadId();
};

// CF_LOG — filtered by min level; level-check is lock-free (atomic read) so
// no overhead when the message is below the threshold.
// Usage: CF_LOG(Info, "value=" << x);
#define CF_LOG(level, ...) \
    do { \
        if (CrystalFrame::Logger::Instance().IsEnabled(CrystalFrame::LogLevel::level)) { \
            std::ostringstream oss; \
            oss << __VA_ARGS__; \
            CrystalFrame::Logger::Instance().Log( \
                CrystalFrame::LogLevel::level, \
                oss.str(), \
                __FILE__, \
                __LINE__ \
            ); \
        } \
    } while(0)

} // namespace CrystalFrame
