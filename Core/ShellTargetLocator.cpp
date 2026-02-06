#include "ShellTargetLocator.h"
#include "Diagnostics.h"
#include <shellapi.h>
#include <algorithm>

namespace CrystalFrame {

ShellTargetLocator::ShellTargetLocator() {
}

ShellTargetLocator::~ShellTargetLocator() {
    Shutdown();
}

bool ShellTargetLocator::Initialize(IShellTargetCallback* callback) {
    m_callback = callback;
    
    // Create message window for taskbar events
    if (!CreateMessageWindow()) {
        CF_LOG(Error, "Failed to create message window");
        return false;
    }
    
    // Register for TaskbarCreated message (Explorer restart)
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    
    // Initial taskbar detection
    if (!DetectTaskbar()) {
        CF_LOG(Warning, "Taskbar not detected initially");
    }
    
    // Start monitoring thread for Start Menu
    m_running = true;
    m_monitorThread = std::thread(&ShellTargetLocator::MonitorStart, this);
    
    CF_LOG(Info, "ShellTargetLocator initialized");
    
    return true;
}

void ShellTargetLocator::Shutdown() {
    m_running = false;
    
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    
    if (m_msgWindow) {
        DestroyWindow(m_msgWindow);
        m_msgWindow = nullptr;
    }
    
    CF_LOG(Info, "ShellTargetLocator shutdown");
}

TaskbarInfo ShellTargetLocator::GetTaskbarInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_taskbarInfo;
}

StartInfo ShellTargetLocator::GetStartInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_startInfo;
}

// ========== TASKBAR DETECTION ==========

bool ShellTargetLocator::DetectTaskbar() {
    // Find Shell_TrayWnd
    HWND hwnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskbarInfo.found = false;
        m_taskbarInfo.hwnd = nullptr;
        return false;
    }
    
    // Get taskbar rect
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        CF_LOG(Error, "GetWindowRect failed for taskbar");
        return false;
    }
    
    // Determine edge
    Edge edge = DetermineEdge(rect);
    
    // Check auto-hide
    bool autoHide = CheckAutoHide(hwnd);
    
    // Update info
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskbarInfo.hwnd = hwnd;
        m_taskbarInfo.rect = rect;
        m_taskbarInfo.edge = edge;
        m_taskbarInfo.autoHide = autoHide;
        m_taskbarInfo.found = true;
    }
    
    CF_LOG(Info, "Taskbar found: edge=" << EdgeToString(edge) 
                 << ", autoHide=" << autoHide
                 << ", rect=(" << rect.left << "," << rect.top << "," 
                 << rect.right << "," << rect.bottom << ")");
    
    // Notify callback
    if (m_callback) {
        m_callback->OnTaskbarChanged(m_taskbarInfo);
    }
    
    return true;
}

Edge ShellTargetLocator::DetermineEdge(const RECT& rect) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Heuristic: if height < width, it's horizontal (top or bottom)
    if (height < width) {
        // Horizontal taskbar
        if (rect.top < screenHeight / 2) {
            return Edge::Top;
        } else {
            return Edge::Bottom;
        }
    } else {
        // Vertical taskbar
        if (rect.left < screenWidth / 2) {
            return Edge::Left;
        } else {
            return Edge::Right;
        }
    }
}

bool ShellTargetLocator::CheckAutoHide(HWND hwnd) {
    APPBARDATA abd = {};
    abd.cbSize = sizeof(APPBARDATA);
    abd.hWnd = hwnd;
    
    UINT state = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
    return (state & ABS_AUTOHIDE) != 0;
}

// ========== MESSAGE WINDOW ==========

bool ShellTargetLocator::CreateMessageWindow() {
    const wchar_t* className = L"CrystalFrameMessageWindow";
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = className;
    
    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "RegisterClassExW failed: " << error);
            return false;
        }
    }
    
    m_msgWindow = CreateWindowExW(
        0,
        className,
        L"CrystalFrame Message",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr,
        GetModuleHandle(NULL),
        this  // Pass 'this' pointer
    );
    
    if (!m_msgWindow) {
        CF_LOG(Error, "CreateWindowExW failed: " << GetLastError());
        return false;
    }
    
    return true;
}

LRESULT CALLBACK ShellTargetLocator::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ShellTargetLocator* pThis = nullptr;
    
    if (msg == WM_CREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<ShellTargetLocator*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ShellTargetLocator*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (pThis) {
        return pThis->WndProc(hwnd, msg, wParam, lParam);
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ShellTargetLocator::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Handle TaskbarCreated (Explorer restart)
    if (msg == m_taskbarCreatedMsg) {
        CF_LOG(Warning, "Explorer restarted - re-detecting taskbar");
        
        // Wait briefly for shell to stabilize
        Sleep(500);
        
        // Re-detect taskbar
        DetectTaskbar();
        
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ========== START MENU DETECTION ==========

void ShellTargetLocator::MonitorStart() {
    CF_LOG(Info, "Start Menu monitoring thread started");
    
    StartInfo lastState;
    
    while (m_running) {
        if (!m_startEnabled) {
            Sleep(1000);
            continue;
        }
        
        StartInfo newState = DetectStart();
        
        // Check for state change
        bool stateChanged = (newState.isOpen != lastState.isOpen);
        
        if (stateChanged) {
            if (newState.isOpen && newState.confidence >= 0.6f) {
                // Start opened
                CF_LOG(Info, "Start menu opened (confidence: " << newState.confidence << ")");
                
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_startInfo = newState;
                    m_lowConfidenceCount = 0;
                }
                
                if (m_callback) {
                    m_callback->OnStartShown(newState);
                }
            }
            else if (!newState.isOpen && lastState.isOpen) {
                // Start closed
                CF_LOG(Info, "Start menu closed");
                
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_startInfo.isOpen = false;
                }
                
                if (m_callback) {
                    m_callback->OnStartHidden();
                }
            }
        }
        
        // Check for low confidence
        if (newState.isOpen && newState.confidence < 0.6f) {
            m_lowConfidenceCount++;
            
            if (m_lowConfidenceCount > 10) {
                CF_LOG(Warning, "Start menu detection unreliable - disabling");
                m_startEnabled = false;
                m_lowConfidenceCount = 0;
                
                if (m_callback) {
                    m_callback->OnStartDetectionFailed();
                }
            }
        } else {
            m_lowConfidenceCount = 0;
        }
        
        lastState = newState;
        
        // Poll every 250ms
        Sleep(250);
    }
    
    CF_LOG(Info, "Start Menu monitoring thread stopped");
}

StartInfo ShellTargetLocator::DetectStart() {
    StartInfo info = {};
    
    HWND hwnd = FindStartMenuWindow();
    
    if (!hwnd) {
        return info;
    }
    
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        return info;
    }
    
    // Verify rect looks like Start Menu
    if (!VerifyStartMenuRect(rect)) {
        return info;
    }
    
    // Calculate confidence
    float confidence = CalculateConfidence(hwnd, rect);
    
    info.hwnd = hwnd;
    info.rect = rect;
    info.isOpen = true;
    info.confidence = confidence;
    info.detected = true;
    
    return info;
}

HWND ShellTargetLocator::FindStartMenuWindow() {
    // Windows 11 Start Menu class names (may vary by build)
    const wchar_t* classNames[] = {
        L"Windows.UI.Core.CoreWindow",
        L"Xaml_WindowedPopupClass",
    };
    
    for (const auto* className : classNames) {
        HWND found = nullptr;
        
        // Enumerate windows
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* data = reinterpret_cast<std::pair<const wchar_t*, HWND*>*>(lParam);
            
            wchar_t actualClass[256];
            GetClassNameW(hwnd, actualClass, 256);
            
            if (wcscmp(actualClass, data->first) == 0) {
                if (IsWindowVisible(hwnd)) {
                    wchar_t title[256];
                    GetWindowTextW(hwnd, title, 256);
                    
                    // Start menu usually has empty or "Start" title
                    if (wcslen(title) == 0 || wcscmp(title, L"Start") == 0) {
                        *(data->second) = hwnd;
                        return FALSE;  // Stop enumeration
                    }
                }
            }
            
            return TRUE;  // Continue
        }, reinterpret_cast<LPARAM>(&std::make_pair(className, &found)));
        
        if (found) {
            return found;
        }
    }
    
    return nullptr;
}

bool ShellTargetLocator::VerifyStartMenuRect(const RECT& rect) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    // Reasonable size checks
    if (width < 300 || height < 400) {
        return false;
    }
    
    if (width > 1200 || height > 1000) {
        return false;
    }
    
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Check if roughly centered horizontally
    int centerX = rect.left + width / 2;
    int screenCenterX = screenWidth / 2;
    int offset = abs(centerX - screenCenterX);
    
    if (offset > screenWidth / 4) {
        return false;
    }
    
    return true;
}

float ShellTargetLocator::CalculateConfidence(HWND hwnd, const RECT& rect) {
    float confidence = 0.0f;
    
    // Factor 1: Window class match (40%)
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);
    if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
        confidence += 0.4f;
    } else if (wcscmp(className, L"Xaml_WindowedPopupClass") == 0) {
        confidence += 0.3f;
    }
    
    // Factor 2: Process name check (30%)
    if (IsStartMenuForeground()) {
        confidence += 0.3f;
    }
    
    // Factor 3: Rect validation (20%)
    if (VerifyStartMenuRect(rect)) {
        confidence += 0.2f;
    }
    
    // Factor 4: Visibility (10%)
    if (IsWindowVisible(hwnd)) {
        confidence += 0.1f;
    }
    
    return confidence;
}

bool ShellTargetLocator::IsStartMenuForeground() {
    HWND hwndForeground = GetForegroundWindow();
    if (!hwndForeground) {
        return false;
    }
    
    DWORD pid = 0;
    GetWindowThreadProcessId(hwndForeground, &pid);
    
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return false;
    }
    
    wchar_t processPath[MAX_PATH];
    DWORD size = MAX_PATH;
    bool isStart = false;
    
    if (QueryFullProcessImageNameW(hProcess, 0, processPath, &size)) {
        if (wcsstr(processPath, L"StartMenuExperienceHost.exe")) {
            isStart = true;
        }
    }
    
    CloseHandle(hProcess);
    return isStart;
}

} // namespace CrystalFrame
