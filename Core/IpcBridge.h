#pragma once
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace CrystalFrame {

// Callback interface for IPC commands
class IIpcCallback {
public:
    virtual ~IIpcCallback() = default;
    
    virtual void OnSetTaskbarOpacity(int opacity) = 0;
    virtual void OnSetStartOpacity(int opacity) = 0;
    virtual void OnSetTaskbarEnabled(bool enabled) = 0;
    virtual void OnSetStartEnabled(bool enabled) = 0;
    virtual void OnGetStatus() = 0;
    virtual void OnShutdown() = 0;
};

struct StatusData {
    struct {
        bool found;
        std::string edge;
        bool autoHide;
        bool enabled;
        int opacity;
    } taskbar;
    
    struct {
        bool detected;
        bool isOpen;
        float confidence;
        bool enabled;
        int opacity;
    } start;
};

class IpcBridge {
public:
    IpcBridge();
    ~IpcBridge();
    
    bool Initialize(IIpcCallback* callback);
    void Shutdown();
    
    void SendStatusUpdate(const StatusData& status);
    void SendError(const std::string& message, const std::string& code);
    
private:
    HANDLE m_hPipe = INVALID_HANDLE_VALUE;
    std::thread m_listenerThread;
    std::atomic<bool> m_running{false};
    IIpcCallback* m_callback = nullptr;
    
    void ListenerThread();
    void HandleMessage(const std::string& json);
    void SendMessage(const std::string& json);
    
    std::string ParseCommand(const std::string& json, const std::string& key);
    int ParseInt(const std::string& json, const std::string& key);
    bool ParseBool(const std::string& json, const std::string& key);
};

} // namespace CrystalFrame
