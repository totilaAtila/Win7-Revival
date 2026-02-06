#pragma once
#include <Windows.h>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace CrystalFrame {

enum class Edge {
    Bottom,
    Top,
    Left,
    Right
};

struct TaskbarInfo {
    HWND hwnd = nullptr;
    RECT rect = {};
    Edge edge = Edge::Bottom;
    bool autoHide = false;
    bool found = false;
};

struct StartInfo {
    HWND hwnd = nullptr;
    RECT rect = {};
    bool isOpen = false;
    float confidence = 0.0f;  // 0.0 to 1.0
    bool detected = false;
};

// Callback interface for events
class IShellTargetCallback {
public:
    virtual ~IShellTargetCallback() = default;
    
    virtual void OnTaskbarChanged(const TaskbarInfo& info) = 0;
    virtual void OnStartShown(const StartInfo& info) = 0;
    virtual void OnStartHidden() = 0;
    virtual void OnStartDetectionFailed() = 0;
};

class ShellTargetLocator {
public:
    ShellTargetLocator();
    ~ShellTargetLocator();
    
    bool Initialize(IShellTargetCallback* callback);
    void Shutdown();
    
    TaskbarInfo GetTaskbarInfo() const;
    StartInfo GetStartInfo() const;
    
private:
    IShellTargetCallback* m_callback = nullptr;
    
    // Taskbar tracking
    TaskbarInfo m_taskbarInfo;
    HWND m_msgWindow = nullptr;
    UINT m_taskbarCreatedMsg = 0;
    
    // Start Menu tracking
    StartInfo m_startInfo;
    std::thread m_monitorThread;
    std::atomic<bool> m_running{false};
    int m_lowConfidenceCount = 0;
    bool m_startEnabled = true;
    
    mutable std::mutex m_mutex;
    
    // Taskbar methods
    bool DetectTaskbar();
    Edge DetermineEdge(const RECT& rect);
    bool CheckAutoHide(HWND hwnd);
    void MonitorTaskbar();
    
    // Start Menu methods
    void MonitorStart();
    StartInfo DetectStart();
    HWND FindStartMenuWindow();
    bool VerifyStartMenuRect(const RECT& rect);
    float CalculateConfidence(HWND hwnd, const RECT& rect);
    bool IsStartMenuForeground();
    
    // Message window
    bool CreateMessageWindow();
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

inline const wchar_t* EdgeToString(Edge edge) {
    switch (edge) {
        case Edge::Bottom: return L"bottom";
        case Edge::Top:    return L"top";
        case Edge::Left:   return L"left";
        case Edge::Right:  return L"right";
        default:           return L"unknown";
    }
}

} // namespace CrystalFrame
