#include "Diagnostics.h"
#include <iostream>

namespace CrystalFrame {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::wstring& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        return;
    }
    
    m_logFile.open(logFilePath, std::ios::out | std::ios::app);
    
    if (!m_logFile.is_open()) {
        std::wcerr << L"Failed to open log file: " << logFilePath << std::endl;
        return;
    }
    
    m_initialized = true;
    
    // Write startup header
    m_logFile << L"\n========================================\n";
    m_logFile << L"CrystalFrame Core Starting\n";
    m_logFile << L"Timestamp: " << GetTimestamp() << L"\n";
    m_logFile << L"========================================\n";
    m_logFile.flush();
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized && m_logFile.is_open()) {
        m_logFile << L"\n========================================\n";
        m_logFile << L"CrystalFrame Core Shutdown\n";
        m_logFile << L"Timestamp: " << GetTimestamp() << L"\n";
        m_logFile << L"========================================\n";
        m_logFile.flush();
        m_logFile.close();
    }
    
    m_initialized = false;
}

Logger::~Logger() {
    Shutdown();
}

void Logger::Log(LogLevel level, const std::string& message, const char* file, int line) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) {
        return;
    }
    
    // Extract just the filename from full path
    std::string filename(file);
    size_t lastSlash = filename.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }
    
    // Format: [TIMESTAMP][THREAD][LEVEL] message (file:line)
    m_logFile << L"[" << GetTimestamp() << L"]";
    m_logFile << L"[" << GetThreadId() << L"]";
    m_logFile << L"[" << LevelToString(level) << L"] ";
    
    // Convert message to wstring
    std::wstring wmessage(message.begin(), message.end());
    m_logFile << wmessage;
    
    // Add file:line for warnings and errors
    if (level == LogLevel::Warning || level == LogLevel::Error) {
        m_logFile << L" (";
        std::wstring wfilename(filename.begin(), filename.end());
        m_logFile << wfilename << L":" << line << L")";
    }
    
    m_logFile << L"\n";
    m_logFile.flush();
    
    // Also output to debugger
    if (IsDebuggerPresent()) {
        std::wostringstream debugMsg;
        debugMsg << L"[CF][" << LevelToString(level) << L"] " << wmessage << L"\n";
        OutputDebugStringW(debugMsg.str().c_str());
    }
}

std::wstring Logger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::tm localTime;
    localtime_s(&localTime, &time);
    
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::wostringstream oss;
    oss << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S");
    oss << L"." << std::setfill(L'0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::wstring Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return L"DEBUG";
        case LogLevel::Info:    return L"INFO ";
        case LogLevel::Warning: return L"WARN ";
        case LogLevel::Error:   return L"ERROR";
        default:                return L"?????";
    }
}

std::wstring Logger::GetThreadId() {
    std::wostringstream oss;
    oss << L"T" << std::this_thread::get_id();
    return oss.str();
}

} // namespace CrystalFrame
