#include "IpcBridge.h"
#include "Diagnostics.h"
#include <sstream>

namespace CrystalFrame {

IpcBridge::IpcBridge() {
}

IpcBridge::~IpcBridge() {
    Shutdown();
}

bool IpcBridge::Initialize(IIpcCallback* callback) {
    m_callback = callback;
    
    // Create named pipe
    m_hPipe = CreateNamedPipeW(
        L"\\\\.\\pipe\\CrystalFrame",
        PIPE_ACCESS_DUPLEX,  // Bidirectional
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,  // Max 1 instance (single Dashboard)
        4096,  // Out buffer size
        4096,  // In buffer size
        0,  // Default timeout
        nullptr
    );
    
    if (m_hPipe == INVALID_HANDLE_VALUE) {
        CF_LOG(Error, "CreateNamedPipe failed: " << GetLastError());
        return false;
    }
    
    CF_LOG(Info, "IPC pipe created, waiting for Dashboard connection...");
    
    // Start listener thread
    m_running = true;
    m_listenerThread = std::thread(&IpcBridge::ListenerThread, this);
    
    return true;
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
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"type\": \"StatusUpdate\",\n";
    oss << "  \"data\": {\n";
    oss << "    \"taskbar\": {\n";
    oss << "      \"found\": " << (status.taskbar.found ? "true" : "false") << ",\n";
    oss << "      \"edge\": \"" << status.taskbar.edge << "\",\n";
    oss << "      \"autoHide\": " << (status.taskbar.autoHide ? "true" : "false") << ",\n";
    oss << "      \"enabled\": " << (status.taskbar.enabled ? "true" : "false") << ",\n";
    oss << "      \"opacity\": " << status.taskbar.opacity << "\n";
    oss << "    },\n";
    oss << "    \"start\": {\n";
    oss << "      \"detected\": " << (status.start.detected ? "true" : "false") << ",\n";
    oss << "      \"isOpen\": " << (status.start.isOpen ? "true" : "false") << ",\n";
    oss << "      \"confidence\": " << status.start.confidence << ",\n";
    oss << "      \"enabled\": " << (status.start.enabled ? "true" : "false") << ",\n";
    oss << "      \"opacity\": " << status.start.opacity << "\n";
    oss << "    }\n";
    oss << "  }\n";
    oss << "}\n";
    
    SendMessage(oss.str());
}

void IpcBridge::SendError(const std::string& message, const std::string& code) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"type\": \"Error\",\n";
    oss << "  \"data\": {\n";
    oss << "    \"message\": \"" << message << "\",\n";
    oss << "    \"code\": \"" << code << "\"\n";
    oss << "  }\n";
    oss << "}\n";
    
    SendMessage(oss.str());
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

} // namespace CrystalFrame
