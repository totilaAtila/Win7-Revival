#pragma once
#include <Windows.h>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

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
    // Called when taskbar list changes (multi-monitor); default delegates to OnTaskbarChanged for primary
    virtual void OnTaskbarsChanged(const std::vector<TaskbarInfo>& infos) {
        if (!infos.empty()) OnTaskbarChanged(infos[0]);
    }
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
    std::vector<TaskbarInfo> GetTaskbarInfoList() const;
    StartInfo GetStartInfo() const;

    // Re-run taskbar detection (call periodically to catch alignment/position changes)
    void RefreshTaskbar() { DetectTaskbar(); }
    
private:
    IShellTargetCallback* m_callback = nullptr;
    
    // Taskbar tracking
    TaskbarInfo m_taskbarInfo;
    std::vector<TaskbarInfo> m_taskbarInfoList;
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
    Edge DetermineEdge(HWND hwnd, const RECT& rect);
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

inline const char* EdgeToString(Edge edge) {
    switch (edge) {
        case Edge::Bottom: return "bottom";
        case Edge::Top:    return "top";
        case Edge::Left:   return "left";
        case Edge::Right:  return "right";
        default:           return "unknown";
    }
}

} // namespace CrystalFrame
