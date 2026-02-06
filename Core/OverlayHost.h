#pragma once
#include <Windows.h>
#include "ShellTargetLocator.h"

namespace CrystalFrame {

class OverlayHost {
public:
    OverlayHost();
    ~OverlayHost();
    
    bool Initialize(HINSTANCE hInstance);
    void Shutdown();
    
    HWND GetTaskbarOverlayWindow() const { return m_hwndTaskbar; }
    HWND GetStartOverlayWindow() const { return m_hwndStart; }
    
    // Update overlay positions
    void UpdateTaskbarOverlay(const TaskbarInfo& info);
    void ShowStartOverlay(const StartInfo& info);
    void HideStartOverlay();
    
private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_hwndTaskbar = nullptr;
    HWND m_hwndStart = nullptr;
    
    bool RegisterWindowClasses();
    HWND CreateOverlayWindow(const wchar_t* className, const wchar_t* windowName);
    
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace CrystalFrame
