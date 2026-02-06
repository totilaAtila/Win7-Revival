#include "OverlayHost.h"
#include "Diagnostics.h"

namespace CrystalFrame {

OverlayHost::OverlayHost() {
}

OverlayHost::~OverlayHost() {
    Shutdown();
}

bool OverlayHost::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    
    if (!RegisterWindowClasses()) {
        CF_LOG(Error, "Failed to register window classes");
        return false;
    }
    
    // Create Taskbar overlay window
    m_hwndTaskbar = CreateOverlayWindow(L"CrystalFrameTaskbarOverlay", L"CrystalFrame Taskbar Overlay");
    if (!m_hwndTaskbar) {
        CF_LOG(Error, "Failed to create Taskbar overlay window");
        return false;
    }
    
    // Create Start overlay window
    m_hwndStart = CreateOverlayWindow(L"CrystalFrameStartOverlay", L"CrystalFrame Start Overlay");
    if (!m_hwndStart) {
        CF_LOG(Error, "Failed to create Start overlay window");
        return false;
    }
    
    CF_LOG(Info, "OverlayHost initialized - Taskbar HWND: 0x" << std::hex 
                 << reinterpret_cast<uintptr_t>(m_hwndTaskbar)
                 << ", Start HWND: 0x" << std::hex 
                 << reinterpret_cast<uintptr_t>(m_hwndStart));
    
    return true;
}

void OverlayHost::Shutdown() {
    if (m_hwndStart) {
        DestroyWindow(m_hwndStart);
        m_hwndStart = nullptr;
    }
    
    if (m_hwndTaskbar) {
        DestroyWindow(m_hwndTaskbar);
        m_hwndTaskbar = nullptr;
    }
    
    CF_LOG(Info, "OverlayHost shutdown");
}

bool OverlayHost::RegisterWindowClasses() {
    // Register Taskbar overlay class
    WNDCLASSEXW wcTaskbar = {};
    wcTaskbar.cbSize = sizeof(WNDCLASSEXW);
    wcTaskbar.lpfnWndProc = OverlayWndProc;
    wcTaskbar.hInstance = m_hInstance;
    wcTaskbar.lpszClassName = L"CrystalFrameTaskbarOverlay";
    wcTaskbar.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    if (!RegisterClassExW(&wcTaskbar)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "RegisterClassExW (Taskbar) failed: " << error);
            return false;
        }
    }
    
    // Register Start overlay class
    WNDCLASSEXW wcStart = {};
    wcStart.cbSize = sizeof(WNDCLASSEXW);
    wcStart.lpfnWndProc = OverlayWndProc;
    wcStart.hInstance = m_hInstance;
    wcStart.lpszClassName = L"CrystalFrameStartOverlay";
    wcStart.hCursor = LoadCursor(nullptr, IDC_ARROW);
    
    if (!RegisterClassExW(&wcStart)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "RegisterClassExW (Start) failed: " << error);
            return false;
        }
    }
    
    return true;
}

HWND OverlayHost::CreateOverlayWindow(const wchar_t* className, const wchar_t* windowName) {
    // Create layered, transparent, topmost window
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        className,
        windowName,
        WS_POPUP,
        0, 0, 0, 0,  // Position and size will be set later
        nullptr,
        nullptr,
        m_hInstance,
        nullptr
    );
    
    if (!hwnd) {
        CF_LOG(Error, "CreateWindowExW failed: " << GetLastError());
        return nullptr;
    }
    
    // Enable click-through (WS_EX_TRANSPARENT already set)
    // Set alpha to fully opaque (DirectComposition will handle opacity)
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    
    return hwnd;
}

void OverlayHost::UpdateTaskbarOverlay(const TaskbarInfo& info) {
    if (!m_hwndTaskbar || !info.found) {
        return;
    }
    
    // Position overlay exactly over taskbar
    SetWindowPos(
        m_hwndTaskbar,
        HWND_TOPMOST,
        info.rect.left,
        info.rect.top,
        info.rect.right - info.rect.left,
        info.rect.bottom - info.rect.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    
    CF_LOG(Debug, "Taskbar overlay updated: (" 
                  << info.rect.left << "," << info.rect.top << ") "
                  << (info.rect.right - info.rect.left) << "x"
                  << (info.rect.bottom - info.rect.top));
}

void OverlayHost::ShowStartOverlay(const StartInfo& info) {
    if (!m_hwndStart || !info.detected) {
        return;
    }
    
    // Position overlay over Start Menu
    SetWindowPos(
        m_hwndStart,
        HWND_TOPMOST,
        info.rect.left,
        info.rect.top,
        info.rect.right - info.rect.left,
        info.rect.bottom - info.rect.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
    
    CF_LOG(Debug, "Start overlay shown: (" 
                  << info.rect.left << "," << info.rect.top << ") "
                  << (info.rect.right - info.rect.left) << "x"
                  << (info.rect.bottom - info.rect.top));
}

void OverlayHost::HideStartOverlay() {
    if (!m_hwndStart) {
        return;
    }
    
    ShowWindow(m_hwndStart, SW_HIDE);
    CF_LOG(Debug, "Start overlay hidden");
}

LRESULT CALLBACK OverlayHost::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            // DirectComposition handles rendering
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_DESTROY:
            return 0;
            
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace CrystalFrame
