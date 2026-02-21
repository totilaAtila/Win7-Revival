#include "StartMenuHook.h"
#include "Diagnostics.h"

namespace CrystalFrame {

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

void StartMenuHook::FindStartButton() {
    // Find taskbar
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) {
        CF_LOG(Warning, "Could not find taskbar window");
        return;
    }

    // Find Start button (it's a child of the taskbar)
    // On Windows 11, the Start button is typically a "Start" button within the taskbar
    HWND startButton = FindWindowExW(taskbar, nullptr, L"Start", nullptr);
    if (!startButton) {
        // Try alternate class names
        startButton = FindWindowExW(taskbar, nullptr, L"TrayButton", nullptr);
    }

    if (startButton) {
        m_startButtonHwnd = startButton;
        CF_LOG(Info, "Found Start button: 0x" << std::hex << reinterpret_cast<uintptr_t>(startButton) << std::dec);
    } else {
        CF_LOG(Warning, "Could not find Start button window");
    }
}

bool StartMenuHook::IsClickOnStartButton(POINT pt) {
    if (!m_startButtonHwnd) {
        // If we don't have the Start button window, check if click is in bottom-left corner
        // Windows 11 Start button is typically in the taskbar bottom-center, but we'll check bottom-left area
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int taskbarHeight = 48; // Approximate

        // Check if click is in bottom taskbar area and left portion
        if (pt.y >= screenHeight - taskbarHeight && pt.x < 200) {
            return true;
        }
        return false;
    }

    // Check if click is within Start button rect
    RECT startRect;
    if (GetWindowRect(m_startButtonHwnd, &startRect)) {
        if (PtInRect(&startRect, pt)) {
            return true;
        }
    }

    return false;
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
        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (kbd->vkCode == VK_LWIN || kbd->vkCode == VK_RWIN) {
                return 1; // Suppress Windows key up
            }
        }

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
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
            // Check for Windows key press (Left or Right)
            else if (kbd->vkCode == VK_LWIN || kbd->vkCode == VK_RWIN) {
                CF_LOG(Debug, "Windows key pressed - showing custom Start Menu");

                // Get Start button position
                int x = 0, y = 0;
                if (s_instance->m_startButtonHwnd) {
                    RECT startRect;
                    if (GetWindowRect(s_instance->m_startButtonHwnd, &startRect)) {
                        x = startRect.left;
                        y = startRect.top;
                    }
                } else {
                    // Default to bottom-left corner
                    y = GetSystemMetrics(SM_CYSCREEN) - 48; // Above taskbar
                    x = 0;
                }

                s_instance->ShowStartMenu(x, y);

                // Suppress Windows key down - don't pass to Windows
                return 1;
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

} // namespace CrystalFrame
