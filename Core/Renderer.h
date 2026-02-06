#pragma once
#include <Windows.h>
#include <dcomp.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <chrono>
#include <atomic>

using Microsoft::WRL::ComPtr;

namespace CrystalFrame {

// Custom message for deferred DirectComposition commit
constexpr UINT WM_DCOMP_COMMIT = WM_USER + 100;

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool Initialize(HWND hwndTaskbar, HWND hwndStart);
    void Shutdown();
    
    // Opacity control (0.0 to 1.0)
    void SetTaskbarOpacity(float opacity);
    void SetStartOpacity(float opacity);
    
    // Enable/disable visuals
    void SetTaskbarEnabled(bool enabled);
    void SetStartEnabled(bool enabled);
    
    // Call from message loop when WM_DCOMP_COMMIT is received
    void OnDeferredCommit();
    
    // Update surface (for diagnostics mode)
    bool CreateOverlaySurface(bool isTaskbar, int width, int height);
    
private:
    // Target window for posting deferred commit messages
    HWND m_hwndHost = nullptr;
    ComPtr<IDCompositionDevice> m_dcompDevice;
    
    // Taskbar
    ComPtr<IDCompositionTarget> m_targetTaskbar;
    ComPtr<IDCompositionVisual> m_visualTaskbar;
    float m_taskbarOpacity = 0.75f;
    bool m_taskbarEnabled = true;
    
    // Start Menu
    ComPtr<IDCompositionTarget> m_targetStart;
    ComPtr<IDCompositionVisual> m_visualStart;
    float m_startOpacity = 0.5f;
    bool m_startEnabled = true;
    
    // Commit throttling
    std::chrono::steady_clock::time_point m_lastCommit;
    std::atomic<bool> m_pendingCommit{false};
    
    void ScheduleCommit();
    void CommitNow();
    
    bool CreateSolidColorSurface(IDCompositionVisual* visual, D2D1_COLOR_F color, int width, int height);
};

} // namespace CrystalFrame
