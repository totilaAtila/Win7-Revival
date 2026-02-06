#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

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
    
    std::wstring GetTimestamp();
    std::wstring LevelToString(LogLevel level);
    std::wstring GetThreadId();
};

// Macro for convenient logging with file/line info
#define CF_LOG(level, ...) \
    do { \
        std::ostringstream oss; \
        oss << __VA_ARGS__; \
        CrystalFrame::Logger::Instance().Log( \
            CrystalFrame::LogLevel::level, \
            oss.str(), \
            __FILE__, \
            __LINE__ \
        ); \
    } while(0)

} // namespace CrystalFrame
