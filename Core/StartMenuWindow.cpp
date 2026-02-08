#include "StartMenuWindow.h"
#include "Diagnostics.h"
#include "Renderer.h" // For SetWindowCompositionAttribute
#include <dwmapi.h>
#include <windowsx.h> // For GET_X_LPARAM, GET_Y_LPARAM
#include <shellapi.h> // For ShellExecuteW
#include <shlobj.h> // For SHGetKnownFolderPath
#include <fstream>
#include <sstream>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

namespace CrystalFrame {

// Submenu content definitions (Windows 7 style)
static SubMenuItem g_controlPanelSubmenu[] = {
    {L"System", L"control system"},
    {L"Network and Sharing Center", L"control /name Microsoft.NetworkAndSharingCenter"},
    {L"Power Options", L"control powercfg.cpl"},
    {L"Personalization", L"control desk.cpl"},
    {L"Devices and Printers", L"control printers"}
};

static SubMenuItem g_deviceManagerSubmenu[] = {
    {L"Device Manager", L"devmgmt.msc"},
    {L"Disk Management", L"diskmgmt.msc"},
    {L"Computer Management", L"compmgmt.msc"}
};

static SubMenuItem g_installedAppsSubmenu[] = {
    {L"Programs and Features", L"appwiz.cpl"},
    {L"Default Programs", L"control /name Microsoft.DefaultPrograms"},
    {L"Add Features", L"optionalfeatures"}
};

static SubMenuItem g_documentsSubmenu[] = {
    {L"Documents", L"shell:Personal"},
    {L"Downloads", L"shell:Downloads"},
    {L"Desktop", L"shell:Desktop"}
};

static SubMenuItem g_picturesSubmenu[] = {
    {L"Pictures", L"shell:My Pictures"},
    {L"Screenshots", L"shell:Screenshots"},
    {L"Camera Roll", L"shell:Camera Roll"}
};

static SubMenuItem g_videosSubmenu[] = {
    {L"Videos", L"shell:My Video"},
    {L"Recorded TV", L"shell:RecordedTVLibrary"}
};

static SubMenuItem g_recentSubmenu[] = {
    {L"Recent Files", L"shell:Recent"},
    {L"Frequent Places", L"shell:Frequent"}
};

StartMenuWindow::StartMenuWindow()
    : m_opacity(30) // Start with 30% opacity (more opaque, easier to see)
{
    // Initialize submenu pointers
    m_menuItems[0].submenuItems = g_controlPanelSubmenu;
    m_menuItems[0].submenuCount = _countof(g_controlPanelSubmenu);

    m_menuItems[1].submenuItems = g_deviceManagerSubmenu;
    m_menuItems[1].submenuCount = _countof(g_deviceManagerSubmenu);

    m_menuItems[2].submenuItems = g_installedAppsSubmenu;
    m_menuItems[2].submenuCount = _countof(g_installedAppsSubmenu);

    m_menuItems[3].submenuItems = g_documentsSubmenu;
    m_menuItems[3].submenuCount = _countof(g_documentsSubmenu);

    m_menuItems[4].submenuItems = g_picturesSubmenu;
    m_menuItems[4].submenuCount = _countof(g_picturesSubmenu);

    m_menuItems[5].submenuItems = g_videosSubmenu;
    m_menuItems[5].submenuCount = _countof(g_videosSubmenu);

    m_menuItems[6].submenuItems = g_recentSubmenu;
    m_menuItems[6].submenuCount = _countof(g_recentSubmenu);

    // Load custom menu names from JSON
    LoadCustomNames();
}

StartMenuWindow::~StartMenuWindow() {
    Shutdown();
}

bool StartMenuWindow::Initialize() {
    CF_LOG(Info, "StartMenuWindow::Initialize");

    // Register main window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "Failed to register window class: " << error);
            return false;
        }
    }

    // Register flyout window class
    WNDCLASSEXW flyoutWc = {};
    flyoutWc.cbSize = sizeof(WNDCLASSEXW);
    flyoutWc.lpfnWndProc = FlyoutWindowProc;
    flyoutWc.hInstance = GetModuleHandle(NULL);
    flyoutWc.lpszClassName = FLYOUT_CLASS;
    flyoutWc.hCursor = LoadCursor(NULL, IDC_ARROW);
    flyoutWc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClassExW(&flyoutWc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "Failed to register flyout window class: " << error);
            return false;
        }
    }

    // Register edit dialog window class
    WNDCLASSEXW editWc = {};
    editWc.cbSize = sizeof(WNDCLASSEXW);
    editWc.lpfnWndProc = EditDialogProc;
    editWc.hInstance = GetModuleHandle(NULL);
    editWc.lpszClassName = EDIT_DIALOG_CLASS;
    editWc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    editWc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassExW(&editWc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "Failed to register edit dialog class: " << error);
            return false;
        }
    }

    CF_LOG(Info, "StartMenuWindow initialized successfully");
    return true;
}

void StartMenuWindow::Shutdown() {
    HideFlyout();

    if (m_hoverTimer) {
        KillTimer(m_hwnd, m_hoverTimer);
        m_hoverTimer = 0;
    }

    if (m_hwndFlyout) {
        DestroyWindow(m_hwndFlyout);
        m_hwndFlyout = nullptr;
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool StartMenuWindow::CreateMenuWindow() {
    if (m_hwnd) {
        return true; // Already created
    }

    // Create layered window for transparency
    // NOTE: NO WS_EX_TRANSPARENT - we want to receive mouse events!
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        WINDOW_CLASS,
        L"CrystalFrame Start Menu",
        WS_POPUP, // Popup window (no title bar)
        0, 0, WIDTH, HEIGHT,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        this // Pass 'this' pointer for WindowProc
    );

    if (!m_hwnd) {
        CF_LOG(Error, "Failed to create Start Menu window: " << GetLastError());
        return false;
    }

    // Initialize layered window attributes for proper transparency
    if (!SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA)) {
        CF_LOG(Warning, "Failed to set layered window attributes: " << GetLastError());
    }

    // Apply rounded corners (Windows 11 style) using DWM
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &cornerPref, sizeof(cornerPref));

    CF_LOG(Info, "Start Menu window created - HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(m_hwnd) << std::dec);
    return true;
}

void StartMenuWindow::Show(int x, int y) {
    CF_LOG(Info, "StartMenuWindow::Show at (" << x << ", " << y << ")");

    if (!m_hwnd && !CreateMenuWindow()) {
        return;
    }

    // Get precise taskbar position
    HWND hwndTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT taskbarRect = {};
    int menuY = y;

    if (hwndTaskbar && GetWindowRect(hwndTaskbar, &taskbarRect)) {
        // Position menu 1px above taskbar's top edge
        menuY = taskbarRect.top - HEIGHT - 1;
        CF_LOG(Info, "Taskbar top edge: " << taskbarRect.top << ", menu Y: " << menuY);
    } else {
        // Fallback to old logic
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        if (y > screenHeight - 100) {
            menuY = y - HEIGHT;
        }
    }

    // Ensure menu stays on screen
    if (menuY < 0) menuY = 0;

    // Position 5px from left edge of monitor
    int menuX = 5;

    // Ensure menu doesn't go off right edge
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    if (menuX + WIDTH > screenWidth) {
        menuX = screenWidth - WIDTH;
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, menuX, menuY, WIDTH, HEIGHT,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    ApplyTransparency();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hwnd);

    m_visible = true;
    CF_LOG(Info, "Start Menu window shown at (" << menuX << ", " << menuY << ")");
}

void StartMenuWindow::Hide() {
    if (m_hwnd && m_visible) {
        CF_LOG(Info, "StartMenuWindow::Hide");
        HideFlyout();
        if (m_hoverTimer) {
            KillTimer(m_hwnd, m_hoverTimer);
            m_hoverTimer = 0;
        }
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

RECT StartMenuWindow::GetWindowBounds() const {
    RECT bounds = {};
    if (m_hwnd && m_visible && IsWindow(m_hwnd)) {
        GetWindowRect(m_hwnd, &bounds);
    }
    return bounds;
}

void StartMenuWindow::SetOpacity(int opacity) {
    m_opacity = opacity;
    if (m_visible) {
        ApplyTransparency();
    }
}

void StartMenuWindow::SetBackgroundColor(COLORREF color) {
    m_bgColor = color;
    if (m_visible) {
        ApplyTransparency();
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
}

void StartMenuWindow::SetTextColor(COLORREF color) {
    m_textColor = color;
    if (m_visible) {
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
}

void StartMenuWindow::SetMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                                    bool documents, bool pictures, bool videos, bool recentFiles) {
    m_menuItems[0].visible = controlPanel;
    m_menuItems[1].visible = deviceManager;
    m_menuItems[2].visible = installedApps;
    m_menuItems[3].visible = documents;
    m_menuItems[4].visible = pictures;
    m_menuItems[5].visible = videos;
    m_menuItems[6].visible = recentFiles;

    if (m_visible) {
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
}

void StartMenuWindow::ApplyTransparency() {
    if (!m_hwnd) return;

    // Use SetWindowCompositionAttribute - same as taskbar transparency
    using SetWindowCompositionAttributeFunc = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;

    auto setWindowCompositionAttribute =
        reinterpret_cast<SetWindowCompositionAttributeFunc>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));

    if (!setWindowCompositionAttribute) {
        CF_LOG(Warning, "SetWindowCompositionAttribute not available");
        return;
    }

    // Calculate alpha: 0% opacity = 255 alpha (opaque), 100% opacity = 0 alpha (transparent)
    BYTE alpha = static_cast<BYTE>(((100 - m_opacity) * 255) / 100);

    ACCENT_POLICY accent = {};
    accent.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT;
    accent.AccentFlags = 2;
    accent.GradientColor = (alpha << 24) | (m_bgColor & 0x00FFFFFF);
    accent.AnimationId = 0;

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);

    setWindowCompositionAttribute(m_hwnd, &data);

    CF_LOG(Debug, "Start Menu transparency applied - opacity=" << m_opacity << "%, alpha=" << (int)alpha);
}

void StartMenuWindow::Paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    // Get client rect
    RECT rect;
    GetClientRect(m_hwnd, &rect);

    // Fill with background color (transparency is handled by composition)
    HBRUSH brush = CreateSolidBrush(m_bgColor);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);

    // Draw a border for visibility
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(100, 100, 255));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);
    Rectangle(hdc, 1, 1, rect.right - 1, rect.bottom - 1);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);

    // Draw placeholder text with configured color
    ::SetTextColor(hdc, m_textColor);
    SetBkMode(hdc, TRANSPARENT);

    HFONT font = CreateFontW(
        32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    HFONT oldFont = (HFONT)SelectObject(hdc, font);

    // Title
    RECT titleRect = rect;
    titleRect.top = 20;
    titleRect.bottom = 70;
    DrawTextW(hdc, GetTitle(), -1, &titleRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Draw menu items
    HFONT itemFont = CreateFontW(
        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    SelectObject(hdc, itemFont);
    ::SetTextColor(hdc, m_textColor);

    int yPos = 100;
    const int itemHeight = 45;
    const int leftMargin = 30;

    for (int i = 0; i < 7; i++) {
        if (m_menuItems[i].visible) {
            RECT itemRect;
            itemRect.left = leftMargin;
            itemRect.top = yPos;
            itemRect.right = rect.right - leftMargin;
            itemRect.bottom = yPos + itemHeight;

            // Draw hover highlight with rounded corners
            if (i == m_hoveredItemIndex) {
                HBRUSH hoverBrush = CreateSolidBrush(CalculateHoverColor());
                HBRUSH oldHoverBrush = (HBRUSH)SelectObject(hdc, hoverBrush);
                HPEN nullHoverPen = (HPEN)GetStockObject(NULL_PEN);
                HPEN oldHoverPen = (HPEN)SelectObject(hdc, nullHoverPen);

                // Rounded rectangle (5px corner radius)
                RoundRect(hdc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom,
                          5, 5);

                SelectObject(hdc, oldHoverPen);
                SelectObject(hdc, oldHoverBrush);
                DeleteObject(hoverBrush);
            }

            // Draw item name (custom or default)
            DrawTextW(hdc, GetMenuItemName(i), -1, &itemRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            yPos += itemHeight;
        }
    }

    // Cleanup fonts
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteObject(itemFont);

    EndPaint(m_hwnd, &ps);
}

int StartMenuWindow::GetItemAtPoint(POINT pt) {
    // Convert to client coordinates
    POINT clientPt = pt;

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);

    const int itemHeight = 45;
    const int leftMargin = 30;
    int yPos = 100;

    for (int i = 0; i < 7; i++) {
        if (m_menuItems[i].visible) {
            RECT itemRect;
            itemRect.left = leftMargin;
            itemRect.top = yPos;
            itemRect.right = clientRect.right - leftMargin;
            itemRect.bottom = yPos + itemHeight;

            if (PtInRect(&itemRect, clientPt)) {
                return i;
            }

            yPos += itemHeight;
        }
    }

    return -1; // No item at this point
}

void StartMenuWindow::ExecuteMenuItem(int index) {
    if (index < 0 || index >= 7 || !m_menuItems[index].visible) {
        return;
    }

    CF_LOG(Info, "Executing menu item index: " << index);

    // Execute shell command based on item
    switch (index) {
        case 0: // Control Panel
            ShellExecuteW(NULL, L"open", L"control", NULL, NULL, SW_SHOW);
            break;
        case 1: // Device Manager
            ShellExecuteW(NULL, L"open", L"devmgmt.msc", NULL, NULL, SW_SHOW);
            break;
        case 2: // Installed Apps
            ShellExecuteW(NULL, L"open", L"ms-settings:appsfeatures", NULL, NULL, SW_SHOW);
            break;
        case 3: // Documents
            ShellExecuteW(NULL, L"explore", L"shell:Personal", NULL, NULL, SW_SHOW);
            break;
        case 4: // Pictures
            ShellExecuteW(NULL, L"explore", L"shell:My Pictures", NULL, NULL, SW_SHOW);
            break;
        case 5: // Videos
            ShellExecuteW(NULL, L"explore", L"shell:My Video", NULL, NULL, SW_SHOW);
            break;
        case 6: // Recent Files
            ShellExecuteW(NULL, L"explore", L"shell:Recent", NULL, NULL, SW_SHOW);
            break;
    }
}

RECT StartMenuWindow::GetItemRect(int index) {
    RECT rect = {};
    if (index < 0 || index >= 7) {
        return rect;
    }

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);

    const int itemHeight = 45;
    const int leftMargin = 30;
    int yPos = 100;

    // Find the position accounting for hidden items
    for (int i = 0; i < 7; i++) {
        if (m_menuItems[i].visible) {
            if (i == index) {
                rect.left = leftMargin;
                rect.top = yPos;
                rect.right = clientRect.right - leftMargin;
                rect.bottom = yPos + itemHeight;
                break;
            }
            yPos += itemHeight;
        }
    }

    return rect;
}

LRESULT CALLBACK StartMenuWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StartMenuWindow* window = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<StartMenuWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<StartMenuWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->HandleMessage(msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT StartMenuWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            Paint();
            return 0;

        case WM_MOUSEMOVE: {
            // Track mouse for hover effects
            if (!m_trackingMouse) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = m_hwnd;
                TrackMouseEvent(&tme);
                m_trackingMouse = true;
            }

            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int itemIndex = GetItemAtPoint(pt);

            if (itemIndex != m_hoveredItemIndex) {
                m_hoveredItemIndex = itemIndex;
                InvalidateRect(m_hwnd, NULL, FALSE); // Repaint for highlight

                // Kill existing hover timer
                if (m_hoverTimer) {
                    KillTimer(m_hwnd, m_hoverTimer);
                    m_hoverTimer = 0;
                }

                // If hovering over an item, start timer to show flyout
                if (itemIndex >= 0 && m_menuItems[itemIndex].submenuCount > 0) {
                    m_hoverTimer = SetTimer(m_hwnd, 1, 300, NULL); // 300ms delay (Windows 7 style)
                    CF_LOG(Debug, "Hover timer started for item: " << itemIndex);
                } else {
                    // Hide flyout if moving to non-item area
                    HideFlyout();
                }
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == 1 && m_hoverTimer) {
                // Timer fired - show flyout for hovered item
                KillTimer(m_hwnd, m_hoverTimer);
                m_hoverTimer = 0;

                if (m_hoveredItemIndex >= 0) {
                    ShowFlyout(m_hoveredItemIndex);
                }
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            m_trackingMouse = false;

            // Kill hover timer
            if (m_hoverTimer) {
                KillTimer(m_hwnd, m_hoverTimer);
                m_hoverTimer = 0;
            }

            // Check if mouse is entering flyout window
            POINT cursorPos;
            GetCursorPos(&cursorPos);

            bool isOverFlyout = false;
            if (m_hwndFlyout && m_flyoutVisible) {
                RECT flyoutRect;
                GetWindowRect(m_hwndFlyout, &flyoutRect);
                isOverFlyout = PtInRect(&flyoutRect, cursorPos);
            }

            // Only clear hover if not moving to flyout
            if (!isOverFlyout && m_hoveredItemIndex != -1) {
                m_hoveredItemIndex = -1;
                InvalidateRect(m_hwnd, NULL, FALSE);
            }

            // Don't hide flyout - let it stay visible so user can move to it
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int itemIndex = GetItemAtPoint(pt);

            if (itemIndex >= 0) {
                CF_LOG(Info, "Clicked on item index: " << itemIndex);
                ExecuteMenuItem(itemIndex);
                Hide(); // Close menu after click
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // Check if click is on title area (top 70px)
            if (pt.y >= 20 && pt.y <= 70) {
                CF_LOG(Info, "Right-clicked on title");
                ShowTitleEditDialog();
                return 0;
            }

            // Otherwise check menu items
            int itemIndex = GetItemAtPoint(pt);
            if (itemIndex >= 0) {
                CF_LOG(Info, "Right-clicked on item index: " << itemIndex);
                ShowEditDialog(itemIndex, false, -1);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                CF_LOG(Debug, "ESC pressed - closing Start Menu");
                Hide();
            }
            return 0;

        case WM_DESTROY:
            return 0;

        default:
            return DefWindowProc(m_hwnd, msg, wParam, lParam);
    }
}

bool StartMenuWindow::CreateFlyoutWindow() {
    if (m_hwndFlyout) {
        return true; // Already created
    }

    // Create layered flyout window
    m_hwndFlyout = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        FLYOUT_CLASS,
        L"CrystalFrame Flyout",
        WS_POPUP,
        0, 0, FLYOUT_WIDTH, 100, // Height will be adjusted based on content
        NULL,
        NULL,
        GetModuleHandle(NULL),
        this
    );

    if (!m_hwndFlyout) {
        CF_LOG(Error, "Failed to create flyout window: " << GetLastError());
        return false;
    }

    // Initialize layered window attributes
    if (!SetLayeredWindowAttributes(m_hwndFlyout, 0, 255, LWA_ALPHA)) {
        CF_LOG(Warning, "Failed to set flyout layered window attributes: " << GetLastError());
    }

    // Apply rounded corners to flyout window
    DWM_WINDOW_CORNER_PREFERENCE flyoutCornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwndFlyout, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &flyoutCornerPref, sizeof(flyoutCornerPref));

    CF_LOG(Info, "Flyout window created");
    return true;
}

void StartMenuWindow::ShowFlyout(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= 7 || !m_menuItems[itemIndex].visible) {
        return;
    }

    if (m_menuItems[itemIndex].submenuCount == 0) {
        return; // No submenu for this item
    }

    CF_LOG(Info, "Showing flyout for item: " << itemIndex);

    // Create flyout window if needed
    if (!m_hwndFlyout && !CreateFlyoutWindow()) {
        return;
    }

    m_flyoutForItemIndex = itemIndex;

    // Get item rect in screen coordinates
    RECT itemRect = GetItemRect(itemIndex);
    POINT topLeft = {itemRect.left, itemRect.top};
    POINT bottomRight = {itemRect.right, itemRect.bottom};
    ClientToScreen(m_hwnd, &topLeft);
    ClientToScreen(m_hwnd, &bottomRight);

    // Position flyout directly at the edge of the item (no gap)
    int flyoutX = bottomRight.x;
    int flyoutY = topLeft.y;

    // Calculate flyout height based on submenu count
    int flyoutHeight = m_menuItems[itemIndex].submenuCount * FLYOUT_ITEM_HEIGHT + 10; // 10px padding

    // Ensure flyout stays on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    if (flyoutX + FLYOUT_WIDTH > screenWidth) {
        flyoutX = topLeft.x - FLYOUT_WIDTH; // Show on left instead (no gap)
    }

    if (flyoutY + flyoutHeight > screenHeight) {
        flyoutY = screenHeight - flyoutHeight;
    }

    // Position and show flyout
    SetWindowPos(m_hwndFlyout, HWND_TOPMOST, flyoutX, flyoutY, FLYOUT_WIDTH, flyoutHeight,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    // Apply transparency (same as main menu)
    ApplyFlyoutTransparency();

    ShowWindow(m_hwndFlyout, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hwndFlyout);

    m_flyoutVisible = true;
    m_hoveredSubmenuIndex = -1;
}

void StartMenuWindow::HideFlyout() {
    if (m_hwndFlyout && m_flyoutVisible) {
        CF_LOG(Debug, "Hiding flyout");
        ShowWindow(m_hwndFlyout, SW_HIDE);
        m_flyoutVisible = false;
        m_flyoutForItemIndex = -1;
        m_hoveredSubmenuIndex = -1;
        m_trackingMouseFlyout = false;
    }
}

void StartMenuWindow::ApplyFlyoutTransparency() {
    if (!m_hwndFlyout) return;

    using SetWindowCompositionAttributeFunc = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;

    auto setWindowCompositionAttribute =
        reinterpret_cast<SetWindowCompositionAttributeFunc>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));

    if (!setWindowCompositionAttribute) return;

    BYTE alpha = static_cast<BYTE>(((100 - m_opacity) * 255) / 100);

    ACCENT_POLICY accent = {};
    accent.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT;
    accent.AccentFlags = 2;
    accent.GradientColor = (alpha << 24) | (m_bgColor & 0x00FFFFFF);
    accent.AnimationId = 0;

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);

    setWindowCompositionAttribute(m_hwndFlyout, &data);
}

void StartMenuWindow::PaintFlyout() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwndFlyout, &ps);

    // Get client rect
    RECT rect;
    GetClientRect(m_hwndFlyout, &rect);

    // Fill with background color
    HBRUSH brush = CreateSolidBrush(m_bgColor);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);

    // Draw border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 255));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);
    Rectangle(hdc, 0, 0, rect.right, rect.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);

    // Draw submenu items
    if (m_flyoutForItemIndex >= 0 && m_flyoutForItemIndex < 7) {
        HFONT itemFont = CreateFontW(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        HFONT oldFont = (HFONT)SelectObject(hdc, itemFont);
        ::SetTextColor(hdc, m_textColor);
        SetBkMode(hdc, TRANSPARENT);

        int yPos = 5;
        MenuItem& mainItem = m_menuItems[m_flyoutForItemIndex];

        for (int i = 0; i < mainItem.submenuCount; i++) {
            RECT itemRect;
            itemRect.left = 5;
            itemRect.top = yPos;
            itemRect.right = rect.right - 5;
            itemRect.bottom = yPos + FLYOUT_ITEM_HEIGHT;

            // Draw hover highlight with rounded corners
            if (i == m_hoveredSubmenuIndex) {
                HBRUSH hoverBrush = CreateSolidBrush(CalculateHoverColor());
                HBRUSH oldHoverBrush = (HBRUSH)SelectObject(hdc, hoverBrush);
                HPEN nullHoverPen = (HPEN)GetStockObject(NULL_PEN);
                HPEN oldHoverPen = (HPEN)SelectObject(hdc, nullHoverPen);

                RoundRect(hdc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom,
                          5, 5);

                SelectObject(hdc, oldHoverPen);
                SelectObject(hdc, oldHoverBrush);
                DeleteObject(hoverBrush);
            }

            // Draw item text (custom or default)
            DrawTextW(hdc, GetSubmenuItemName(m_flyoutForItemIndex, i), -1, &itemRect,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            yPos += FLYOUT_ITEM_HEIGHT;
        }

        SelectObject(hdc, oldFont);
        DeleteObject(itemFont);
    }

    EndPaint(m_hwndFlyout, &ps);
}

int StartMenuWindow::GetSubmenuItemAtPoint(POINT pt) {
    if (m_flyoutForItemIndex < 0 || m_flyoutForItemIndex >= 7) {
        return -1;
    }

    RECT clientRect;
    GetClientRect(m_hwndFlyout, &clientRect);

    int yPos = 5;
    MenuItem& mainItem = m_menuItems[m_flyoutForItemIndex];

    for (int i = 0; i < mainItem.submenuCount; i++) {
        RECT itemRect;
        itemRect.left = 5;
        itemRect.top = yPos;
        itemRect.right = clientRect.right - 5;
        itemRect.bottom = yPos + FLYOUT_ITEM_HEIGHT;

        if (PtInRect(&itemRect, pt)) {
            return i;
        }

        yPos += FLYOUT_ITEM_HEIGHT;
    }

    return -1;
}

void StartMenuWindow::ExecuteSubmenuItem(int mainIndex, int subIndex) {
    if (mainIndex < 0 || mainIndex >= 7 || !m_menuItems[mainIndex].visible) {
        return;
    }

    MenuItem& mainItem = m_menuItems[mainIndex];
    if (subIndex < 0 || subIndex >= mainItem.submenuCount) {
        return;
    }

    CF_LOG(Info, "Executing submenu item: " << mainIndex << "," << subIndex);

    // Execute the command
    const wchar_t* command = mainItem.submenuItems[subIndex].command;

    // Parse command - check if it starts with "shell:" for special folders
    if (wcsstr(command, L"shell:") == command) {
        ShellExecuteW(NULL, L"explore", command, NULL, NULL, SW_SHOW);
    } else if (wcsstr(command, L"control") == command) {
        ShellExecuteW(NULL, L"open", command, NULL, NULL, SW_SHOW);
    } else {
        // Default: try to open/run it
        ShellExecuteW(NULL, L"open", command, NULL, NULL, SW_SHOW);
    }
}

RECT StartMenuWindow::GetSubmenuItemRect(int index) {
    RECT rect = {};
    if (m_flyoutForItemIndex < 0 || m_flyoutForItemIndex >= 7) {
        return rect;
    }

    MenuItem& mainItem = m_menuItems[m_flyoutForItemIndex];
    if (index < 0 || index >= mainItem.submenuCount) {
        return rect;
    }

    RECT clientRect;
    GetClientRect(m_hwndFlyout, &clientRect);

    rect.left = 5;
    rect.top = 5 + (index * FLYOUT_ITEM_HEIGHT);
    rect.right = clientRect.right - 5;
    rect.bottom = rect.top + FLYOUT_ITEM_HEIGHT;

    return rect;
}

LRESULT CALLBACK StartMenuWindow::FlyoutWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StartMenuWindow* window = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<StartMenuWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<StartMenuWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->HandleFlyoutMessage(msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT StartMenuWindow::HandleFlyoutMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            PaintFlyout();
            return 0;

        case WM_MOUSEMOVE: {
            if (!m_trackingMouseFlyout) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = m_hwndFlyout;
                TrackMouseEvent(&tme);
                m_trackingMouseFlyout = true;
            }

            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int itemIndex = GetSubmenuItemAtPoint(pt);

            if (itemIndex != m_hoveredSubmenuIndex) {
                m_hoveredSubmenuIndex = itemIndex;
                InvalidateRect(m_hwndFlyout, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            m_trackingMouseFlyout = false;

            // Check if mouse is entering main menu window
            POINT cursorPos;
            GetCursorPos(&cursorPos);

            bool isOverMainMenu = false;
            if (m_hwnd && m_visible) {
                RECT mainRect;
                GetWindowRect(m_hwnd, &mainRect);
                isOverMainMenu = PtInRect(&mainRect, cursorPos);
            }

            // Clear submenu hover
            if (m_hoveredSubmenuIndex != -1) {
                m_hoveredSubmenuIndex = -1;
                InvalidateRect(m_hwndFlyout, NULL, FALSE);
            }

            // Hide flyout only if mouse is not over main menu
            if (!isOverMainMenu) {
                HideFlyout();
            }

            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int itemIndex = GetSubmenuItemAtPoint(pt);

            if (itemIndex >= 0) {
                CF_LOG(Info, "Clicked submenu item: " << itemIndex);
                ExecuteSubmenuItem(m_flyoutForItemIndex, itemIndex);
                Hide(); // Close entire menu after click
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int itemIndex = GetSubmenuItemAtPoint(pt);

            if (itemIndex >= 0) {
                CF_LOG(Info, "Right-clicked submenu item: " << itemIndex);
                ShowEditDialog(m_flyoutForItemIndex, true, itemIndex);
            }
            return 0;
        }

        default:
            return DefWindowProc(m_hwndFlyout, msg, wParam, lParam);
    }
}

COLORREF StartMenuWindow::CalculateHoverColor() {
    // Extract RGB components from background color
    int r = GetRValue(m_bgColor);
    int g = GetGValue(m_bgColor);
    int b = GetBValue(m_bgColor);

    // Calculate brightness
    int brightness = (r + g + b) / 3;

    // If dark background, make hover lighter; if light background, make hover darker
    int adjustment = (brightness < 128) ? 40 : -40;

    // Apply adjustment and clamp to 0-255
    r = max(0, min(255, r + adjustment));
    g = max(0, min(255, g + adjustment));
    b = max(0, min(255, b + adjustment));

    return RGB(r, g, b);
}

const wchar_t* StartMenuWindow::GetMenuItemName(int index) {
    if (index < 0 || index >= 7) {
        return L"";
    }

    // Return custom name if set, otherwise default
    if (!m_customMenuNames[index].empty()) {
        return m_customMenuNames[index].c_str();
    }

    return m_menuItems[index].name;
}

const wchar_t* StartMenuWindow::GetSubmenuItemName(int mainIndex, int subIndex) {
    if (mainIndex < 0 || mainIndex >= 7) {
        return L"";
    }

    // Check for custom submenu name
    auto mainIt = m_customSubmenuNames.find(mainIndex);
    if (mainIt != m_customSubmenuNames.end()) {
        auto subIt = mainIt->second.find(subIndex);
        if (subIt != mainIt->second.end() && !subIt->second.empty()) {
            return subIt->second.c_str();
        }
    }

    // Return default name
    MenuItem& mainItem = m_menuItems[mainIndex];
    if (subIndex >= 0 && subIndex < mainItem.submenuCount) {
        return mainItem.submenuItems[subIndex].name;
    }

    return L"";
}

void StartMenuWindow::ShowEditDialog(int itemIndex, bool isSubmenu, int submenuIndex) {
    wchar_t buffer[256] = {};

    // Get current name
    if (isSubmenu) {
        wcscpy_s(buffer, GetSubmenuItemName(itemIndex, submenuIndex));
    } else {
        wcscpy_s(buffer, GetMenuItemName(itemIndex));
    }

    // Store context in dialog data structure
    struct EditContext {
        wchar_t buffer[256];
        bool confirmed;
        int itemIndex;
        bool isSubmenu;
        int submenuIndex;
        StartMenuWindow* pThis;
    };

    EditContext context = {};
    wcscpy_s(context.buffer, buffer);
    context.confirmed = false;
    context.itemIndex = itemIndex;
    context.isSubmenu = isSubmenu;
    context.submenuIndex = submenuIndex;
    context.pThis = this;

    // Create modal dialog window with proper class
    HWND hwndDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        EDIT_DIALOG_CLASS,
        L"Edit Menu Item",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, 400, 150,
        m_hwnd,
        NULL,
        GetModuleHandle(NULL),
        &context
    );

    if (!hwndDialog) {
        CF_LOG(Error, "Failed to create edit dialog: " << GetLastError());
        return;
    }

    // Center dialog on screen
    RECT dialogRect;
    GetWindowRect(hwndDialog, &dialogRect);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - (dialogRect.right - dialogRect.left)) / 2;
    int y = (screenHeight - (dialogRect.bottom - dialogRect.top)) / 2;
    SetWindowPos(hwndDialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // Create label
    CreateWindowExW(
        0, L"STATIC", L"Enter new name:",
        WS_CHILD | WS_VISIBLE,
        10, 10, 380, 20,
        hwndDialog, NULL, GetModuleHandle(NULL), NULL
    );

    // Create edit control
    HWND hwndEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", buffer,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        10, 35, 380, 25,
        hwndDialog, (HMENU)1001, GetModuleHandle(NULL), NULL
    );

    // Create OK button
    CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        200, 80, 90, 30,
        hwndDialog, (HMENU)IDOK, GetModuleHandle(NULL), NULL
    );

    // Create Cancel button
    CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE,
        300, 80, 90, 30,
        hwndDialog, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL
    );

    // Set focus to edit control
    SetFocus(hwndEdit);
    SendMessageW(hwndEdit, EM_SETSEL, 0, -1);

    // Enable dialog (modal)
    EnableWindow(m_hwnd, FALSE);
    if (m_hwndFlyout) EnableWindow(m_hwndFlyout, FALSE);

    // Message loop for modal dialog
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hwndDialog)) break;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Re-enable parent windows
    EnableWindow(m_hwnd, TRUE);
    if (m_hwndFlyout) EnableWindow(m_hwndFlyout, TRUE);
    SetForegroundWindow(m_hwnd);

    // Retrieve result from dialog data
    EditContext* pContext = (EditContext*)GetWindowLongPtr(hwndDialog, GWLP_USERDATA);
    if (pContext && pContext->confirmed && wcslen(pContext->buffer) > 0) {
        if (isSubmenu) {
            m_customSubmenuNames[itemIndex][submenuIndex] = pContext->buffer;
        } else {
            m_customMenuNames[itemIndex] = pContext->buffer;
        }

        SaveCustomNames();

        // Refresh display
        if (isSubmenu && m_flyoutVisible) {
            InvalidateRect(m_hwndFlyout, NULL, TRUE);
        } else {
            InvalidateRect(m_hwnd, NULL, TRUE);
        }

        CF_LOG(Info, "Menu item renamed successfully");
    }

    DestroyWindow(hwndDialog);
}

const wchar_t* StartMenuWindow::GetTitle() {
    if (!m_customTitle.empty()) {
        return m_customTitle.c_str();
    }
    return L"CrystalFrame Start Menu";
}

void StartMenuWindow::ShowTitleEditDialog() {
    wchar_t buffer[256] = {};
    wcscpy_s(buffer, GetTitle());

    struct EditContext {
        wchar_t buffer[256];
        bool confirmed;
        int itemIndex;
        bool isSubmenu;
        int submenuIndex;
        StartMenuWindow* pThis;
    };

    EditContext context = {};
    wcscpy_s(context.buffer, buffer);
    context.confirmed = false;
    context.pThis = this;

    // Create dialog (same logic as ShowEditDialog but for title)
    HWND hwndDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        EDIT_DIALOG_CLASS,
        L"Edit Start Menu Title",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, 400, 150,
        m_hwnd, NULL, GetModuleHandle(NULL), &context
    );

    if (!hwndDialog) return;

    // Center, create controls (same as ShowEditDialog)
    RECT dialogRect;
    GetWindowRect(hwndDialog, &dialogRect);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (dialogRect.right - dialogRect.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (dialogRect.bottom - dialogRect.top)) / 2;
    SetWindowPos(hwndDialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    CreateWindowExW(0, L"STATIC", L"Enter new title:",
                    WS_CHILD | WS_VISIBLE, 10, 10, 380, 20,
                    hwndDialog, NULL, GetModuleHandle(NULL), NULL);

    HWND hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buffer,
                                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                     10, 35, 380, 25,
                                     hwndDialog, (HMENU)1001, GetModuleHandle(NULL), NULL);

    CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    200, 80, 90, 30, hwndDialog, (HMENU)IDOK, GetModuleHandle(NULL), NULL);

    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                    300, 80, 90, 30, hwndDialog, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);

    SetFocus(hwndEdit);
    SendMessageW(hwndEdit, EM_SETSEL, 0, -1);

    EnableWindow(m_hwnd, FALSE);
    if (m_hwndFlyout) EnableWindow(m_hwndFlyout, FALSE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hwndDialog)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(m_hwnd, TRUE);
    if (m_hwndFlyout) EnableWindow(m_hwndFlyout, TRUE);

    EditContext* pContext = (EditContext*)GetWindowLongPtr(hwndDialog, GWLP_USERDATA);
    if (pContext && pContext->confirmed && wcslen(pContext->buffer) > 0) {
        m_customTitle = pContext->buffer;
        SaveCustomNames();
        InvalidateRect(m_hwnd, NULL, TRUE);
        CF_LOG(Info, "Start Menu title renamed");
    }

    DestroyWindow(hwndDialog);
}

void StartMenuWindow::LoadCustomNames() {
    // Get AppData\Local\CrystalFrame path
    PWSTR localAppDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath))) {
        CF_LOG(Warning, "Failed to get LocalAppData path");
        return;
    }

    std::wstring configPath = localAppDataPath;
    CoTaskMemFree(localAppDataPath);

    configPath += L"\\CrystalFrame\\menu_names.json";

    // Read JSON file
    std::wifstream file(configPath);
    if (!file.is_open()) {
        CF_LOG(Info, "No custom menu names file found (first run)");
        return;
    }

    // Simple JSON parsing (manual for now)
    std::wstring line;

    while (std::getline(file, line)) {
        // Simple parsing: look for "menu_X": "Name"
        size_t menuPos = line.find(L"\"menu_");
        if (menuPos != std::wstring::npos) {
            int index = line[menuPos + 6] - L'0';  // Extract index
            size_t colonPos = line.find(L':', menuPos);
            size_t firstQuote = line.find(L'\"', colonPos);
            size_t secondQuote = line.find(L'\"', firstQuote + 1);

            if (index >= 0 && index < 7 && firstQuote != std::wstring::npos && secondQuote != std::wstring::npos) {
                m_customMenuNames[index] = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            }
        }

        // Look for submenu entries: "submenu_X_Y": "Name"
        size_t submenuPos = line.find(L"\"submenu_");
        if (submenuPos != std::wstring::npos) {
            int mainIdx = line[submenuPos + 9] - L'0';
            int subIdx = line[submenuPos + 11] - L'0';

            size_t colonPos = line.find(L':', submenuPos);
            size_t firstQuote = line.find(L'\"', colonPos);
            size_t secondQuote = line.find(L'\"', firstQuote + 1);

            if (mainIdx >= 0 && mainIdx < 7 && firstQuote != std::wstring::npos && secondQuote != std::wstring::npos) {
                m_customSubmenuNames[mainIdx][subIdx] = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            }
        }

        // Look for custom title: "title": "Name"
        size_t titlePos = line.find(L"\"title\"");
        if (titlePos != std::wstring::npos) {
            size_t colonPos = line.find(L':', titlePos);
            size_t firstQuote = line.find(L'\"', colonPos);
            size_t secondQuote = line.find(L'\"', firstQuote + 1);

            if (firstQuote != std::wstring::npos && secondQuote != std::wstring::npos) {
                m_customTitle = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            }
        }
    }

    file.close();
    CF_LOG(Info, "Custom menu names loaded from JSON");
}

void StartMenuWindow::SaveCustomNames() {
    // Get AppData\Local\CrystalFrame path
    PWSTR localAppDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppDataPath))) {
        CF_LOG(Error, "Failed to get LocalAppData path for saving");
        return;
    }

    std::wstring configDir = localAppDataPath;
    CoTaskMemFree(localAppDataPath);

    configDir += L"\\CrystalFrame";

    // Ensure directory exists
    CreateDirectoryW(configDir.c_str(), NULL);

    std::wstring configPath = configDir + L"\\menu_names.json";

    // Write JSON file
    std::wofstream file(configPath);
    if (!file.is_open()) {
        CF_LOG(Error, "Failed to open menu names file for writing");
        return;
    }

    file << L"{\n";

    // Write main menu names
    bool firstEntry = true;
    for (int i = 0; i < 7; i++) {
        if (!m_customMenuNames[i].empty()) {
            if (!firstEntry) file << L",\n";
            file << L"  \"menu_" << i << L"\": \"" << m_customMenuNames[i] << L"\"";
            firstEntry = false;
        }
    }

    // Write submenu names
    for (const auto& mainPair : m_customSubmenuNames) {
        for (const auto& subPair : mainPair.second) {
            if (!subPair.second.empty()) {
                if (!firstEntry) file << L",\n";
                file << L"  \"submenu_" << mainPair.first << L"_" << subPair.first << L"\": \"" << subPair.second << L"\"";
                firstEntry = false;
            }
        }
    }

    // Write custom title if set
    if (!m_customTitle.empty()) {
        if (!firstEntry) file << L",\n";
        file << L"  \"title\": \"" << m_customTitle << L"\"";
        firstEntry = false;
    }

    file << L"\n}\n";
    file.close();

    CF_LOG(Info, "Custom menu names saved to JSON");
}

LRESULT CALLBACK StartMenuWindow::EditDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Store context pointer in window data
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == IDOK) {
                // Get edit control text
                HWND hwndEdit = GetDlgItem(hwnd, 1001);
                void* pData = (void*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

                if (pData && hwndEdit) {
                    struct EditContext {
                        wchar_t buffer[256];
                        bool confirmed;
                        int itemIndex;
                        bool isSubmenu;
                        int submenuIndex;
                        StartMenuWindow* pThis;
                    };

                    EditContext* pContext = (EditContext*)pData;
                    GetWindowTextW(hwndEdit, pContext->buffer, 256);
                    pContext->confirmed = true;
                }

                DestroyWindow(hwnd);
                PostQuitMessage(0);
                return 0;
            }
            else if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDCLOSE) {
                DestroyWindow(hwnd);
                PostQuitMessage(0);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            PostQuitMessage(0);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace CrystalFrame
