#include "IpcBridge.h"
#include "Diagnostics.h"
#include <sstream>

namespace GlassBar {

IpcBridge::IpcBridge() {
}

IpcBridge::~IpcBridge() {
    Shutdown();
}

bool IpcBridge::Initialize(IIpcCallback* callback) {
    m_callback = callback;

    // Retry logic for ERROR_PIPE_BUSY (231) - handles cases where old instance didn't cleanup
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 1000;

    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        if (attempt > 0) {
            CF_LOG(Info, "Retrying pipe creation (attempt " << (attempt + 1) << "/" << MAX_RETRIES << ")...");
            Sleep(RETRY_DELAY_MS);
        }

        // Create named pipe
        m_hPipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\GlassBar",
            PIPE_ACCESS_DUPLEX,  // Bidirectional
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,  // Max 1 instance (single Dashboard)
            4096,  // Out buffer size
            4096,  // In buffer size
            0,  // Default timeout
            nullptr
        );

        if (m_hPipe != INVALID_HANDLE_VALUE) {
            CF_LOG(Info, "IPC pipe created successfully, waiting for Dashboard connection...");

            // Start listener thread
            m_running = true;
            m_listenerThread = std::thread(&IpcBridge::ListenerThread, this);

            return true;
        }

        DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            CF_LOG(Warning, "Pipe is busy (ERROR_PIPE_BUSY), another Core instance may be running");
            // Continue retry loop
        } else {
            CF_LOG(Error, "CreateNamedPipe failed: " << error);
            return false;
        }
    }

    CF_LOG(Error, "Failed to create pipe after " << MAX_RETRIES << " attempts");
    return false;
}

void IpcBridge::Shutdown() {
    m_running = false;
    
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(m_hPipe);
        CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }
    
    if (m_listenerThread.joinable()) {
        m_listenerThread.join();
    }
    
    CF_LOG(Info, "IpcBridge shutdown");
}

void IpcBridge::ListenerThread() {
    CF_LOG(Info, "IPC listener thread started");
    
    while (m_running) {
        // Wait for client connection
        BOOL connected = ConnectNamedPipe(m_hPipe, nullptr);
        
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            CF_LOG(Error, "ConnectNamedPipe failed: " << GetLastError());
            Sleep(1000);
            continue;
        }
        
        CF_LOG(Info, "Dashboard connected to IPC pipe");
        
        // Read loop
        while (m_running) {
            char buffer[4096];
            DWORD bytesRead = 0;
            
            BOOL success = ReadFile(m_hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
            
            if (!success || bytesRead == 0) {
                DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE) {
                    CF_LOG(Info, "Dashboard disconnected");
                } else {
                    CF_LOG(Error, "ReadFile failed: " << error);
                }
                break;
            }
            
            buffer[bytesRead] = '\0';
            std::string json(buffer);
            
            // Handle message
            HandleMessage(json);
        }
        
        // Disconnect and wait for next connection
        DisconnectNamedPipe(m_hPipe);
    }
    
    CF_LOG(Info, "IPC listener thread stopped");
}

void IpcBridge::HandleMessage(const std::string& json) {
    try {
        std::string type = ParseCommand(json, "type");
        
        if (type.empty()) {
            CF_LOG(Warning, "IPC message missing 'type' field");
            return;
        }
        
        CF_LOG(Debug, "IPC received: " << type);
        
        if (type == "SetTaskbarOpacity") {
            int opacity = ParseInt(json, "opacity");
            if (m_callback) {
                m_callback->OnSetTaskbarOpacity(opacity);
            }
        }
        else if (type == "SetStartOpacity") {
            int opacity = ParseInt(json, "opacity");
            if (m_callback) {
                m_callback->OnSetStartOpacity(opacity);
            }
        }
        else if (type == "SetTaskbarEnabled") {
            bool enabled = ParseBool(json, "enabled");
            if (m_callback) {
                m_callback->OnSetTaskbarEnabled(enabled);
            }
        }
        else if (type == "SetStartEnabled") {
            bool enabled = ParseBool(json, "enabled");
            if (m_callback) {
                m_callback->OnSetStartEnabled(enabled);
            }
        }
        else if (type == "GetStatus") {
            if (m_callback) {
                m_callback->OnGetStatus();
            }
        }
        else if (type == "Shutdown") {
            CF_LOG(Info, "IPC: Shutdown command received");
            if (m_callback) {
                m_callback->OnShutdown();
            }
        }
        else if (type == "Ping") {
            // Task 6 heartbeat: reply immediately so the Dashboard knows Core is alive.
            CF_LOG(Debug, "IPC: Ping received — sending Pong");
            SendMessage("{\"type\":\"Pong\",\"data\":{}}\n");
        }
        else {
            CF_LOG(Warning, "Unknown IPC message type: " << type);
        }
    }
    catch (const std::exception& ex) {
        CF_LOG(Error, "IPC message parse error: " << ex.what());
    }
}

void IpcBridge::SendMessage(const std::string& json) {
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        return;
    }
    
    DWORD bytesWritten = 0;
    BOOL success = WriteFile(
        m_hPipe,
        json.c_str(),
        static_cast<DWORD>(json.size()),
        &bytesWritten,
        nullptr
    );
    
    if (!success) {
        CF_LOG(Error, "WriteFile failed: " << GetLastError());
        return;
    }
    
    FlushFileBuffers(m_hPipe);
}

void IpcBridge::SendStatusUpdate(const StatusData& status) {
    CF_LOG(Debug, "Sending status: taskbar.found=" << status.taskbar.found
                  << ", start.detected=" << status.start.detected);

    // Send as single line so Dashboard's ReadLineAsync works correctly
    std::ostringstream json;
    json << "{\"type\":\"StatusUpdate\",\"data\":{";
    json << "\"taskbar\":{";
    json << "\"found\":" << (status.taskbar.found ? "true" : "false") << ",";
    json << "\"edge\":\"" << status.taskbar.edge << "\",";
    json << "\"autoHide\":" << (status.taskbar.autoHide ? "true" : "false") << ",";
    json << "\"enabled\":" << (status.taskbar.enabled ? "true" : "false") << ",";
    json << "\"opacity\":" << status.taskbar.opacity << "},";
    json << "\"start\":{";
    json << "\"detected\":" << (status.start.detected ? "true" : "false") << ",";
    json << "\"isOpen\":" << (status.start.isOpen ? "true" : "false") << ",";
    json << "\"confidence\":" << status.start.confidence << ",";
    json << "\"enabled\":" << (status.start.enabled ? "true" : "false") << ",";
    json << "\"opacity\":" << status.start.opacity << "}";
    json << "}}\n";

    SendMessage(json.str());
}

void IpcBridge::SendError(const std::string& message, const std::string& code) {
    // Send as single line so Dashboard's ReadLineAsync works correctly
    std::ostringstream json;
    json << "{\"type\":\"Error\",\"data\":{\"message\":\"" << message << "\",\"code\":\"" << code << "\"}}\n";

    SendMessage(json.str());
}

// Simple JSON parsing helpers (no external dependencies)
std::string IpcBridge::ParseCommand(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    
    if (pos == std::string::npos) {
        return "";
    }
    
    size_t colonPos = json.find(':', pos);
    if (colonPos == std::string::npos) {
        return "";
    }
    
    size_t startQuote = json.find('"', colonPos);
    if (startQuote == std::string::npos) {
        return "";
    }
    
    size_t endQuote = json.find('"', startQuote + 1);
    if (endQuote == std::string::npos) {
        return "";
    }
    
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

int IpcBridge::ParseInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    
    if (pos == std::string::npos) {
        return 0;
    }
    
    size_t colonPos = json.find(':', pos);
    if (colonPos == std::string::npos) {
        return 0;
    }
    
    // Find the number after colon
    size_t numStart = colonPos + 1;
    while (numStart < json.size() && (json[numStart] == ' ' || json[numStart] == '\n')) {
        numStart++;
    }
    
    return std::stoi(json.substr(numStart));
}

bool IpcBridge::ParseBool(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    
    if (pos == std::string::npos) {
        return false;
    }
    
    size_t truePos = json.find("true", pos);
    size_t falsePos = json.find("false", pos);
    
    if (truePos != std::string::npos && (falsePos == std::string::npos || truePos < falsePos)) {
        return true;
    }
    
    return false;
}

} // namespace GlassBar
