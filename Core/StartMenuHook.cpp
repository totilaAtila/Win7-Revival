#include "StartMenuHook.h"
#include "Diagnostics.h"

namespace GlassBar {

// Static instance pointer
StartMenuHook* StartMenuHook::s_instance = nullptr;

StartMenuHook::StartMenuHook() {
    s_instance = this;
}

StartMenuHook::~StartMenuHook() {
    Shutdown();
    s_instance = nullptr;
}

bool StartMenuHook::Initialize() {
    CF_LOG(Info, "StartMenuHook::Initialize");

    // Find the Start button
    FindStartButton();

    // Install keyboard hook (low-level to intercept Windows key)
    m_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(NULL), 0);
    if (!m_keyboardHook) {
        CF_LOG(Error, "Failed to install keyboard hook: " << GetLastError());
        return false;
    }

    // Install mouse hook (low-level to intercept Start button clicks)
    m_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
    if (!m_mouseHook) {
        CF_LOG(Error, "Failed to install mouse hook: " << GetLastError());
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
        return false;
    }

    CF_LOG(Info, "StartMenuHook initialized successfully");
    return true;
}

void StartMenuHook::Shutdown() {
    CF_LOG(Info, "StartMenuHook::Shutdown");

    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }

    if (m_mouseHook) {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
}

void StartMenuHook::SetEnabled(bool enabled) {
    m_enabled = enabled;
    CF_LOG(Info, "StartMenuHook " << (enabled ? "ENABLED" : "DISABLED"));
}

void StartMenuHook::SetShowMenuCallback(ShowMenuCallback callback) {
    m_showMenuCallback = callback;
}

void StartMenuHook::SetHideMenuCallback(HideMenuCallback callback) {
    m_hideMenuCallback = callback;
}

void StartMenuHook::SetIsMenuVisibleCallback(IsMenuVisibleCallback callback) {
    m_isMenuVisibleCallback = callback;
}

void StartMenuHook::SetGetMenuBoundsCallback(GetMenuBoundsCallback callback) {
    m_getMenuBoundsCallback = callback;
}

void StartMenuHook::SetForwardKeyCallback(ForwardKeyCallback callback) {
    m_forwardKeyCallback = callback;
}

// EnumChildWindows callback — finds the widest centered child of Shell_TrayWnd
// (the WinUI 3 XAML island that hosts the taskbar icon group on Win11 22H2+).
struct XamlIslandSearch {
    RECT taskbarRect;
    RECT bestRect;
    int  bestWidth = 0;
    int  screenW   = 0;
};

static BOOL CALLBACK FindCenteredIsland(HWND child, LPARAM lp)
{
    auto* s = reinterpret_cast<XamlIslandSearch*>(lp);
    RECT rc;
    if (!GetWindowRect(child, &rc)) return TRUE;

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int taskH = s->taskbarRect.bottom - s->taskbarRect.top;

    // Must span most of the taskbar height and be at least 100px wide
    if (h < taskH - 8 || w < 100) return TRUE;

    // Prefer the widest window that is roughly centered on screen
    int midX = (rc.left + rc.right) / 2;
    if (abs(midX - s->screenW / 2) < s->screenW / 3 && w > s->bestWidth) {
        s->bestWidth = w;
        s->bestRect  = rc;
    }
    return TRUE;
}

void StartMenuHook::FindStartButton()
{
    m_startButtonHwnd       = nullptr;
    m_startButtonFallbackRect = {};

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) {
        CF_LOG(Warning, "Could not find taskbar window");
        return;
    }

    // --- Try traditional Win32 child windows (Win10 / pre-22H2 Win11) ---
    HWND startButton = FindWindowExW(taskbar, nullptr, L"Start", nullptr);
    if (!startButton)
        startButton = FindWindowExW(taskbar, nullptr, L"TrayButton", nullptr);

    if (startButton) {
        // Verify: button must be in the left quarter of the taskbar (avoids matching
        // the notification-area TrayButton on the right side).
        RECT tbRect, btnRect;
        if (GetWindowRect(taskbar, &tbRect) && GetWindowRect(startButton, &btnRect)) {
            int tbW   = tbRect.right - tbRect.left;
            int btnX  = btnRect.left - tbRect.left;
            if (btnX < tbW / 4) {
                m_startButtonHwnd = startButton;
                CF_LOG(Info, "Found Start button (Win32): 0x"
                    << std::hex << reinterpret_cast<uintptr_t>(startButton) << std::dec);
                return;
            }
            CF_LOG(Warning, "TrayButton found but not in left-quarter of taskbar (x=" << btnX << ") — skipping");
        }
    }

    // --- Win11 22H2+: Start button is inside a WinUI 3 XAML island ---
    // Read taskbar alignment: 0 = left-aligned, 1 = centered (Win11 default)
    DWORD taskbarAl = 1;
    HKEY  hKey      = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD sz = sizeof(taskbarAl);
        RegQueryValueExW(hKey, L"TaskbarAl", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&taskbarAl), &sz);
        RegCloseKey(hKey);
    }

    RECT taskbarRect;
    GetWindowRect(taskbar, &taskbarRect);
    int taskbarH = taskbarRect.bottom - taskbarRect.top;  // typically 48 px

    if (taskbarAl == 0) {
        // Left-aligned: Start button is the first ~48 px at the left edge
        m_startButtonFallbackRect = {
            taskbarRect.left,
            taskbarRect.top,
            taskbarRect.left + taskbarH + 4,
            taskbarRect.bottom
        };
        CF_LOG(Info, "Start button fallback: left-aligned, x=[" << m_startButtonFallbackRect.left
                     << "," << m_startButtonFallbackRect.right << "]");
        return;
    }

    // Centered: enumerate children of Shell_TrayWnd to find the icon group island
    XamlIslandSearch search;
    search.taskbarRect = taskbarRect;
    search.screenW     = GetSystemMetrics(SM_CXSCREEN);
    ZeroMemory(&search.bestRect, sizeof(search.bestRect));
    EnumChildWindows(taskbar, FindCenteredIsland, reinterpret_cast<LPARAM>(&search));

    if (search.bestWidth > 0) {
        // Start button is the leftmost icon in the centered island (~48 px wide)
        m_startButtonFallbackRect = {
            search.bestRect.left,
            search.bestRect.top,
            search.bestRect.left + taskbarH + 4,
            search.bestRect.bottom
        };
        CF_LOG(Info, "Start button fallback: centered island at x=["
                     << m_startButtonFallbackRect.left << ","
                     << m_startButtonFallbackRect.right << "]");
    } else {
        // Last resort: estimate — assume 5-icon group centered on screen
        int groupLeft = search.screenW / 2 - (5 * taskbarH) / 2;
        m_startButtonFallbackRect = {
            groupLeft,
            taskbarRect.top,
            groupLeft + taskbarH + 4,
            taskbarRect.bottom
        };
        CF_LOG(Warning, "Start button fallback: estimated at x=["
                        << m_startButtonFallbackRect.left << ","
                        << m_startButtonFallbackRect.right << "]");
    }
}

bool StartMenuHook::IsClickOnStartButton(POINT pt) {
    // Primary: Win32 HWND (Win10 / pre-22H2)
    if (m_startButtonHwnd) {
        RECT startRect;
        if (GetWindowRect(m_startButtonHwnd, &startRect) && PtInRect(&startRect, pt))
            return true;
    }

    // Secondary: position-based fallback (Win11 22H2+ WinUI 3 taskbar)
    if (m_startButtonFallbackRect.right > m_startButtonFallbackRect.left) {
        return !!PtInRect(&m_startButtonFallbackRect, pt);
    }

    // Legacy last resort: bottom-left corner (Win10 left-aligned only)
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    return pt.y >= screenH - 48 && pt.x < 200;
}

void StartMenuHook::ShowStartMenu(int x, int y) {
    if (m_showMenuCallback) {
        CF_LOG(Info, "Triggering custom Start Menu at (" << x << ", " << y << ")");
        m_showMenuCallback(x, y);
    }
}

LRESULT CALLBACK StartMenuHook::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && s_instance->m_enabled) {
        KBDLLHOOKSTRUCT* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Suppress Windows key KEYUP too - without this, the native Start opens on key release
        const bool isWin  = (kbd->vkCode == VK_LWIN || kbd->vkCode == VK_RWIN);
        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

        if (isWin) {
            if (isDown) {
                // Mark Win as held — do NOT suppress KEYDOWN so Windows can process Win+combos
                s_instance->m_winDown  = true;
                s_instance->m_winCombo = false;
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }
            if (isUp) {
                bool wasCombo = s_instance->m_winCombo;
                s_instance->m_winDown  = false;
                s_instance->m_winCombo = false;
                if (!wasCombo) {
                    // Solo Win press — show custom Start Menu and suppress KEYUP
                    // (suppressing KEYUP prevents native Start Menu from opening)
                    int x = 0, y = 0;
                    if (s_instance->m_startButtonHwnd) {
                        RECT startRect;
                        if (GetWindowRect(s_instance->m_startButtonHwnd, &startRect)) {
                            x = startRect.left;
                            y = startRect.top;
                        }
                    } else {
                        y = GetSystemMetrics(SM_CYSCREEN) - 48;
                    }
                    CF_LOG(Debug, "Windows key solo press - showing custom Start Menu");
                    s_instance->ShowStartMenu(x, y);
                    return 1;
                }
                // Win+combo (Win+D, Win+E, Win+L etc.) — let KEYUP through for Windows to complete
                CF_LOG(Debug, "Windows key combo KEYUP - passing through");
                return CallNextHookEx(NULL, nCode, wParam, lParam);
            }
        }

        // Non-Win key pressed while Win is held = combo (Win+D, Win+E, etc.)
        if (s_instance->m_winDown && isDown)
            s_instance->m_winCombo = true;

        if (isDown) {
            // Navigation / dismiss keys — forward to Start Menu window via PostMessage
            // so that WM_KEYDOWN reaches HandleMessage even though the window is
            // non-activating (WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE).
            if (kbd->vkCode == VK_ESCAPE ||
                kbd->vkCode == VK_UP     ||
                kbd->vkCode == VK_DOWN   ||
                kbd->vkCode == VK_RETURN) {
                if (s_instance->m_isMenuVisibleCallback && s_instance->m_isMenuVisibleCallback()) {
                    CF_LOG(Debug, "Nav key 0x" << std::hex << kbd->vkCode << std::dec
                                  << " forwarded to Start Menu");
                    if (s_instance->m_forwardKeyCallback) {
                        s_instance->m_forwardKeyCallback(kbd->vkCode);
                    }
                    return 1; // Suppress — handled by the menu window
                }
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK StartMenuHook::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && s_instance->m_enabled) {
        MSLLHOOKSTRUCT* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        POINT pt = mouse->pt;

        // Check if click is on Start button - suppress ALL mouse events on it
        bool isOnStartButton = s_instance->IsClickOnStartButton(pt);

        if (isOnStartButton) {
            CF_LOG(Debug, "Click detected on Start button area - suppressing");

            // Handle only left button down to toggle menu
            if (wParam == WM_LBUTTONDOWN) {
                CF_LOG(Debug, "Start button left-click at (" << pt.x << ", " << pt.y << ")");

                // Check if menu is visible
                bool menuVisible = s_instance->m_isMenuVisibleCallback && s_instance->m_isMenuVisibleCallback();

                // Toggle menu: if visible, hide it; if hidden, show it
                if (menuVisible) {
                    CF_LOG(Debug, "Menu already visible - hiding");
                    if (s_instance->m_hideMenuCallback) {
                        s_instance->m_hideMenuCallback();
                    }
                } else {
                    CF_LOG(Debug, "Menu not visible - showing");
                    s_instance->ShowStartMenu(pt.x, pt.y);
                }
            }

            // Suppress click events on Start button, but NOT WM_MOUSEMOVE.
            // Suppressing WM_MOUSEMOVE in a low-level hook freezes the cursor at
            // the Start button boundary, which is the bug the user reported.
            if (wParam != WM_MOUSEMOVE) {
                return 1;
            }
        }

        // Handle clicks when menu is visible (not on Start button)
        if (wParam == WM_LBUTTONDOWN) {
            bool menuVisible = s_instance->m_isMenuVisibleCallback && s_instance->m_isMenuVisibleCallback();

            if (menuVisible) {
                // Get menu bounds
                RECT menuBounds = {};
                if (s_instance->m_getMenuBoundsCallback) {
                    menuBounds = s_instance->m_getMenuBoundsCallback();
                }

                // Check if click is outside menu bounds
                if (!PtInRect(&menuBounds, pt)) {
                    CF_LOG(Debug, "Click outside Start Menu - hiding");
                    if (s_instance->m_hideMenuCallback) {
                        s_instance->m_hideMenuCallback();
                    }
                    // Don't suppress the click - let it propagate
                    return CallNextHookEx(NULL, nCode, wParam, lParam);
                }
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

} // namespace GlassBar
