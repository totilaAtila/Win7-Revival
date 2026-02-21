#include "ShellTargetLocator.h"
#include "Diagnostics.h"
#include <shellapi.h>
#include <algorithm>

namespace CrystalFrame {

// Helper function to convert wide string to UTF-8 for logging
static std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";

    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, nullptr, nullptr);
    return result;
}

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

std::vector<TaskbarInfo> ShellTargetLocator::GetTaskbarInfoList() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_taskbarInfoList;
}

StartInfo ShellTargetLocator::GetStartInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_startInfo;
}

// ========== TASKBAR DETECTION ==========

// Helper to build a TaskbarInfo from an HWND
static TaskbarInfo BuildTaskbarInfo(HWND hwnd) {
    TaskbarInfo info;
    info.hwnd = hwnd;
    info.found = false;

    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return info;

    if (!GetWindowRect(hwnd, &info.rect))
        return info;

    APPBARDATA abd = {};
    abd.cbSize = sizeof(APPBARDATA);
    abd.hWnd = hwnd;
    UINT state = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
    info.autoHide = (state & ABS_AUTOHIDE) != 0;
    info.found = true;
    return info;
}

bool ShellTargetLocator::DetectTaskbar() {
    std::vector<TaskbarInfo> infos;

    // ── Primary taskbar ───────────────────────────────────────────────────────
    HWND hwndPrimary = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hwndPrimary || !IsWindow(hwndPrimary) || !IsWindowVisible(hwndPrimary)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskbarInfo.found = false;
        m_taskbarInfo.hwnd = nullptr;
        m_taskbarInfoList.clear();
        return false;
    }

    TaskbarInfo primary = BuildTaskbarInfo(hwndPrimary);
    if (!primary.found) return false;
    primary.edge = DetermineEdge(hwndPrimary, primary.rect);
    infos.push_back(primary);

    // ── Secondary taskbars (multi-monitor) ────────────────────────────────────
    HWND hwndSec = nullptr;
    while ((hwndSec = FindWindowExW(nullptr, hwndSec, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
        if (!IsWindow(hwndSec) || !IsWindowVisible(hwndSec))
            continue;
        TaskbarInfo sec = BuildTaskbarInfo(hwndSec);
        if (sec.found) {
            sec.edge = DetermineEdge(hwndSec, sec.rect);
            infos.push_back(sec);
        }
    }

    // ── Update state ─────────────────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskbarInfo = infos[0];
        m_taskbarInfoList = infos;
    }

    CF_LOG(Info, "Taskbars found: count=" << infos.size()
                 << ", primary edge=" << EdgeToString(infos[0].edge)
                 << ", rect=(" << infos[0].rect.left << "," << infos[0].rect.top << ","
                 << infos[0].rect.right << "," << infos[0].rect.bottom << ")");

    // ── Notify callback ───────────────────────────────────────────────────────
    if (m_callback) {
        m_callback->OnTaskbarsChanged(infos);  // multi-monitor aware
    }

    return true;
}

Edge ShellTargetLocator::DetermineEdge(HWND hwnd, const RECT& rect) {
    int width  = rect.right  - rect.left;
    int height = rect.bottom - rect.top;

    // Use the monitor that contains this taskbar window for per-monitor coordinates
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) {
        // Fallback to primary screen metrics
        mi.rcMonitor = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }

    int screenWidth  = mi.rcMonitor.right  - mi.rcMonitor.left;
    int screenHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

    // Heuristic: horizontal taskbar if height < width
    if (height < width) {
        int midY = mi.rcMonitor.top + screenHeight / 2;
        return (rect.top < midY) ? Edge::Top : Edge::Bottom;
    } else {
        int midX = mi.rcMonitor.left + screenWidth / 2;
        return (rect.left < midX) ? Edge::Left : Edge::Right;
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
        CF_LOG(Warning, "Explorer restarted - scheduling taskbar re-detection");
        // Use a one-shot timer instead of Sleep() so we don't block the message pump
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;
    }

    if (msg == WM_TIMER && wParam == 1) {
        KillTimer(hwnd, 1);
        CF_LOG(Info, "Re-detecting taskbar after Explorer restart");
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
        
        // For state tracking, treat low-confidence detections as "not open"
        // to avoid swallowing events in a false-positive gap
        bool effectivelyOpen = newState.isOpen && newState.confidence >= 0.4f;
        bool stateChanged = (effectivelyOpen != lastState.isOpen);

        if (stateChanged) {
            if (effectivelyOpen) {
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
            else if (!effectivelyOpen && lastState.isOpen) {
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

        // Use effectivelyOpen for lastState so tracking stays consistent
        lastState.isOpen = effectivelyOpen;
        
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
        
        // Update other lastState fields (isOpen was already set above based on effectivelyOpen)
        lastState.hwnd = newState.hwnd;
        lastState.rect = newState.rect;
        lastState.confidence = newState.confidence;
        lastState.detected = newState.detected;

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
        L"Windows.UI.Composition.DesktopWindowContentBridge",  // Windows 11 23H2+
        L"ApplicationFrameWindow",  // Possible on some builds
        L"Shell_CharmWindow",       // Search charm
    };

    for (const auto* className : classNames) {
        HWND found = nullptr;
        auto enumData = std::make_pair(className, &found);

        // Enumerate windows
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* data = reinterpret_cast<std::pair<const wchar_t*, HWND*>*>(lParam);

            wchar_t actualClass[256];
            GetClassNameW(hwnd, actualClass, 256);

            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);

            if (wcscmp(actualClass, data->first) == 0) {
                // Log ALL matches for this class, not just filtered ones
                CF_LOG(Debug, "FOUND MATCH: class=" << WideToUtf8(actualClass)
                              << ", title=\"" << WideToUtf8(title) << "\""
                              << ", visible=" << IsWindowVisible(hwnd));

                if (IsWindowVisible(hwnd)) {
                    // Relaxed filter: accept empty title, or title containing "Start" or "Search"
                    if (wcslen(title) == 0 ||
                        wcsstr(title, L"Start") != nullptr ||
                        wcsstr(title, L"Search") != nullptr) {
                        *(data->second) = hwnd;
                        return FALSE;  // Stop enumeration
                    }
                }
            }

            return TRUE;  // Continue
        }, reinterpret_cast<LPARAM>(&enumData));

        if (found) {
            return found;
        }
    }

    return nullptr;
}

bool ShellTargetLocator::VerifyStartMenuRect(const RECT& rect) {
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Relaxed size checks for various Start menu sizes
    if (width < 200 || height < 200) {
        return false;
    }

    if (width > 2000 || height > 1500) {
        return false;
    }
    
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    (void)GetSystemMetrics(SM_CYSCREEN);  // screenHeight unused for now

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
