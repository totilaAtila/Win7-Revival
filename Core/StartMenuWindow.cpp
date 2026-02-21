#include "StartMenuWindow.h"
#include "Diagnostics.h"
#include "Renderer.h" // For ACCENT_POLICY / WINDOWCOMPOSITIONATTRIBDATA
#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <powrprof.h>
#include <cwctype>   // towupper

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "powrprof.lib")

namespace CrystalFrame {

// ── Pinned apps (vertical list, left column) ──────────────────────────────────
const PinnedItem StartMenuWindow::s_pinnedItems[StartMenuWindow::PROG_COUNT] = {
    { L"Settings",       L"Set",  L"ms-settings:",     RGB(  0, 103, 192) },
    { L"File Explorer",  L"Exp",  L"explorer.exe",     RGB(255, 185,   0) },
    { L"Edge",           L"Edge", L"msedge.exe",       RGB(  0, 120, 215) },
    { L"Calculator",     L"Calc", L"calc.exe",         RGB( 16, 110, 190) },
    { L"Notepad",        L"Note", L"notepad.exe",      RGB( 40, 160,  40) },
    { L"Task Manager",   L"Task", L"taskmgr.exe",      RGB(196,  43,  28) },
};

// ── Win7 right-column shell links ─────────────────────────────────────────────
// Rules (WORKLOG §6 #3):
//   • Prefer KNOWNFOLDERID (resolved at click-time via SHGetKnownFolderPath).
//   • Fall back to ShellExecute target/params for shell applets.
//   • isSeparator == true  →  draw a line, never clickable.
const Win7RightItem StartMenuWindow::s_rightItems[StartMenuWindow::RIGHT_ITEM_COUNT] = {
    // label               sep    folderId                verb        target   params
    { L"Documents",        false, &FOLDERID_Documents,    L"explore", nullptr, nullptr },
    { L"Pictures",         false, &FOLDERID_Pictures,     L"explore", nullptr, nullptr },
    { L"Music",            false, &FOLDERID_Music,        L"explore", nullptr, nullptr },
    { L"Downloads",        false, &FOLDERID_Downloads,    L"explore", nullptr, nullptr },
    { L"Computer",         false, nullptr, L"open", L"shell:MyComputerFolder", nullptr },
    // ── separator ──────────────────────────────────────────────────────────
    { nullptr,             true,  nullptr,                nullptr,    nullptr, nullptr },
    // ── System applets ─────────────────────────────────────────────────────
    // control:  still works on Win11 (opens legacy Control Panel)
    { L"Control Panel",    false, nullptr, L"open", L"control",                        nullptr },
    // Devices & Printers – CLSID shell link; works on Win10/11
    { L"Devices & Printers", false, nullptr, L"open",
      L"shell:::{A8A91A66-3A7D-4424-8D24-04E180695C7A}",              nullptr },
    // Default Programs – ms-settings:defaultapps is the Win11 equivalent
    { L"Default Programs", false, nullptr, L"open", L"ms-settings:defaultapps",        nullptr },
    // Help and Support – HelpPane.exe is present on Win10/11
    { L"Help and Support", false, nullptr, L"open", L"HelpPane.exe",                   nullptr },
};

// ── Constructor / Destructor ─────────────────────────────────────────────────
StartMenuWindow::StartMenuWindow() {
    // Cache the Windows user name for the right-column header.
    // GetEnvironmentVariableW returns 0 on failure, or the required buffer size
    // (>= len) when the buffer is too small — leaving it unterminated.
    // Only treat the call as a success when 0 < ret < len.
    {
        DWORD len = static_cast<DWORD>(std::size(m_username));
        DWORD ret = GetEnvironmentVariableW(L"USERNAME", m_username, len);
        if (ret == 0 || ret >= len || m_username[0] == L'\0') {
            m_username[0] = L'\0';  // guarantee null-termination before fallback
            // Fallback: try GetUserNameW (requires Advapi32, almost always present)
            DWORD ul = len;
            if (!GetUserNameW(m_username, &ul) || m_username[0] == L'\0')
                wcscpy_s(m_username, L"User");
        }
    }

    LoadCustomNames();
}

StartMenuWindow::~StartMenuWindow() {
    Shutdown();
}

// ── Initialization ───────────────────────────────────────────────────────────
bool StartMenuWindow::Initialize() {
    CF_LOG(Info, "StartMenuWindow::Initialize (Win7 two-column layout)");

    // Register main window class
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "Failed to register window class: " << err);
            return false;
        }
    }

    // Register edit-dialog class
    WNDCLASSEXW editWc   = {};
    editWc.cbSize        = sizeof(WNDCLASSEXW);
    editWc.lpfnWndProc   = EditDialogProc;
    editWc.hInstance     = GetModuleHandle(NULL);
    editWc.lpszClassName = EDIT_DIALOG_CLASS;
    editWc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    editWc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassExW(&editWc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            CF_LOG(Error, "Failed to register edit dialog class: " << err);
            return false;
        }
    }

    // Phase S2 foundation: pre-cache All Programs tree.
    // Done here (not in CreateMenuWindow) because:
    //   • Initialize() is called before HWND creation, so the heavy COM/FS
    //     scan does not delay first window paint.
    //   • BuildAllProgramsTree() self-manages COM; no HWND needed.
    m_programTree = BuildAllProgramsTree();
    CF_LOG(Info, "All Programs tree cached: " << m_programTree.size() << " top-level nodes");

    CF_LOG(Info, "StartMenuWindow initialized successfully");
    return true;
}

void StartMenuWindow::Shutdown() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_visible = false;
}

// ── Window creation ──────────────────────────────────────────────────────────
bool StartMenuWindow::CreateMenuWindow() {
    if (m_hwnd) return true;

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        WINDOW_CLASS,
        L"CrystalFrame Start Menu",
        WS_POPUP,
        0, 0, WIDTH, HEIGHT,
        NULL, NULL,
        GetModuleHandle(NULL),
        this
    );

    if (!m_hwnd) {
        CF_LOG(Error, "Failed to create Start Menu window: " << GetLastError());
        return false;
    }

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    // Windows 11 rounded corners via DWM
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner, sizeof(corner));

    CF_LOG(Info, "Start Menu window created HWND=0x"
                 << std::hex << reinterpret_cast<uintptr_t>(m_hwnd) << std::dec);

    return true;
}

// ── Show / Hide ──────────────────────────────────────────────────────────────
void StartMenuWindow::Show(int x, int y) {
    CF_LOG(Info, "StartMenuWindow::Show");

    if (!m_hwnd && !CreateMenuWindow()) return;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // Locate taskbar and detect its orientation
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT tbRect  = {};
    bool hasTb   = taskbar && GetWindowRect(taskbar, &tbRect);

    // Detect Start button position (best-effort; fallback to hint x/y from hook)
    int sbLeft = x, sbTop = y;
    if (taskbar) {
        HWND sb = FindWindowExW(taskbar, nullptr, L"Start", nullptr);
        if (!sb) sb = FindWindowExW(taskbar, nullptr, L"TrayButton", nullptr);
        if (sb) {
            RECT sbRect = {};
            if (GetWindowRect(sb, &sbRect)) {
                sbLeft = sbRect.left;
                sbTop  = sbRect.top;
            }
        }
    }

    int menuX, menuY;

    if (hasTb) {
        int tbW = tbRect.right  - tbRect.left;
        int tbH = tbRect.bottom - tbRect.top;

        bool tbLeft  = tbW < tbH && tbRect.left <= screenW / 2;
        bool tbRight = tbW < tbH && tbRect.left >  screenW / 2;
        bool tbTop   = tbW >= tbH && tbRect.top <= screenH / 2;

        if (tbLeft) {
            menuX = tbRect.right + 1;
            menuY = sbTop;
        } else if (tbRight) {
            menuX = tbRect.left - WIDTH - 1;
            menuY = sbTop;
        } else if (tbTop) {
            menuX = sbLeft;
            menuY = tbRect.bottom + 1;
        } else {
            menuX = sbLeft;
            menuY = tbRect.top - HEIGHT - 1;
        }
    } else {
        menuX = 0;
        menuY = screenH - HEIGHT - 48;
    }

    menuX = max(0, min(menuX, screenW - WIDTH));
    menuY = max(0, min(menuY, screenH - HEIGHT));

    SetWindowPos(m_hwnd, HWND_TOPMOST, menuX, menuY, WIDTH, HEIGHT,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ApplyTransparency();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hwnd);
    m_visible = true;

    CF_LOG(Info, "Start Menu shown at (" << menuX << ", " << menuY << ")");
}

void StartMenuWindow::Hide() {
    if (m_hwnd && m_visible) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible          = false;
        // Reset to Programs view on every hide
        m_viewMode         = LeftViewMode::Programs;
        m_apNavStack.clear();
        m_hoveredProgIndex  = -1;
        m_hoveredApRow      = false;
        m_hoveredApIndex    = -1;
        m_hoveredRightIndex = -1;
        m_hoveredPower      = false;
        m_keySelProgIndex   = -1;
        m_keySelApRow       = false;
        m_keySelApIndex     = -1;
        m_apScrollOffset    = 0;
        CF_LOG(Info, "StartMenuWindow::Hide");
    }
}

RECT StartMenuWindow::GetWindowBounds() const {
    RECT r = {};
    if (m_hwnd && m_visible && IsWindow(m_hwnd))
        GetWindowRect(m_hwnd, &r);
    return r;
}

// ── Appearance setters ───────────────────────────────────────────────────────
void StartMenuWindow::SetOpacity(int opacity) {
    m_opacity = opacity;
    if (m_visible) ApplyTransparency();
}

void StartMenuWindow::SetBackgroundColor(COLORREF color) {
    m_bgColor = color;
    if (m_visible) {
        ApplyTransparency();
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void StartMenuWindow::SetTextColor(COLORREF color) {
    m_textColor = color;
    if (m_visible) InvalidateRect(m_hwnd, NULL, FALSE);
}

void StartMenuWindow::SetMenuItems(bool controlPanel, bool deviceManager,
                                   bool installedApps, bool documents,
                                   bool pictures,     bool videos,
                                   bool recentFiles) {
    m_menuItems[0].visible = controlPanel;
    m_menuItems[1].visible = deviceManager;
    m_menuItems[2].visible = installedApps;
    m_menuItems[3].visible = documents;
    m_menuItems[4].visible = pictures;
    m_menuItems[5].visible = videos;
    m_menuItems[6].visible = recentFiles;
    // Win7 mode does not render the recommended section, so no invalidate needed.
}

// ── Transparency ─────────────────────────────────────────────────────────────
void StartMenuWindow::ApplyTransparency() {
    if (!m_hwnd) return;

    using SetWCAFunc = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
    HMODULE user32  = GetModuleHandleW(L"user32.dll");
    if (!user32) return;

    auto setWCA = reinterpret_cast<SetWCAFunc>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setWCA) return;

    BYTE alpha = static_cast<BYTE>(((100 - m_opacity) * 255) / 100);

    ACCENT_POLICY accent    = {};
    accent.AccentState      = ACCENT_ENABLE_TRANSPARENTGRADIENT;
    accent.AccentFlags      = 2;
    accent.GradientColor    = (static_cast<DWORD>(alpha) << 24)
                            | (static_cast<DWORD>(GetBValue(m_bgColor)) << 16)
                            | (static_cast<DWORD>(GetGValue(m_bgColor)) <<  8)
                            |  static_cast<DWORD>(GetRValue(m_bgColor));

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib  = WCA_ACCENT_POLICY;
    data.pvData  = &accent;
    data.cbData  = sizeof(accent);

    setWCA(m_hwnd, &data);
}

// ── Color helpers ─────────────────────────────────────────────────────────────
COLORREF StartMenuWindow::CalculateHoverColor() {
    int r   = GetRValue(m_bgColor);
    int g   = GetGValue(m_bgColor);
    int b   = GetBValue(m_bgColor);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    int d   = (lum < 128) ? 38 : -38;
    return RGB(max(0, min(255, r + d)),
               max(0, min(255, g + d)),
               max(0, min(255, b + d)));
}

COLORREF StartMenuWindow::CalculateSubtleColor() {
    int r   = GetRValue(m_bgColor);
    int g   = GetGValue(m_bgColor);
    int b   = GetBValue(m_bgColor);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    int d   = (lum < 128) ? 16 : -16;
    return RGB(max(0, min(255, r + d)),
               max(0, min(255, g + d)),
               max(0, min(255, b + d)));
}

COLORREF StartMenuWindow::CalculateBorderColor() {
    int r   = GetRValue(m_bgColor);
    int g   = GetGValue(m_bgColor);
    int b   = GetBValue(m_bgColor);
    int lum = (r * 299 + g * 587 + b * 114) / 1000;
    int d   = (lum < 128) ? 55 : -55;
    return RGB(max(0, min(255, r + d)),
               max(0, min(255, g + d)),
               max(0, min(255, b + d)));
}

// Blue accent used for keyboard-focus highlight (distinct from mouse-hover gray)
COLORREF StartMenuWindow::CalculateSelectionColor() {
    return RGB(0, 96, 180);
}

// ── DrawIconSquare ────────────────────────────────────────────────────────────
void StartMenuWindow::DrawIconSquare(HDC hdc, int cx, int cy, int sz,
                                     COLORREF bgColor, const wchar_t* label,
                                     COLORREF textColor) {
    int half = sz / 2;
    int x1 = cx - half, y1 = cy - half;
    int x2 = cx + half, y2 = cy + half;

    HBRUSH icoBr  = CreateSolidBrush(bgColor);
    HPEN   noPen  = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH oldBr  = (HBRUSH)SelectObject(hdc, icoBr);
    HPEN   oldPen = (HPEN)SelectObject(hdc, noPen);
    RoundRect(hdc, x1, y1, x2, y2, 6, 6);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(icoBr);

    if (label && label[0]) {
        HFONT font = CreateFontW(
            11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, font);
        ::SetTextColor(hdc, textColor);
        SetBkMode(hdc, TRANSPARENT);
        RECT tr = { x1, y1, x2, y2 };
        DrawTextW(hdc, label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }
}

// ── DrawSeparator ─────────────────────────────────────────────────────────────
void StartMenuWindow::DrawSeparator(HDC hdc, int y, int x1, int x2) {
    HPEN pen    = CreatePen(PS_SOLID, 1, CalculateBorderColor());
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y, NULL);
    LineTo(hdc, x2, y);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// ─────────────────────────────────────────────────────────────────────────────
// PaintProgramsList — Win7-style vertical list of pinned apps (left column)
// Each row: [colored icon square 24×24] [app name]
// ─────────────────────────────────────────────────────────────────────────────
void StartMenuWindow::PaintProgramsList(HDC hdc, const RECT& cr) {
    (void)cr;
    SetBkMode(hdc, TRANSPARENT);

    HFONT nameFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, nameFont);

    for (int i = 0; i < PROG_COUNT; ++i) {
        int itemY = PROG_Y + i * PROG_ITEM_H;

        // Hover / keyboard-selection highlight (mutually exclusive visually)
        bool isKeySel = (i == m_keySelProgIndex);
        bool isHover  = (i == m_hoveredProgIndex) && !isKeySel;
        if (isKeySel || isHover) {
            COLORREF hlColor = isKeySel ? CalculateSelectionColor() : CalculateHoverColor();
            HBRUSH hBr  = CreateSolidBrush(hlColor);
            HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op   = (HPEN)SelectObject(hdc, noPn);
            RoundRect(hdc, MARGIN, itemY + 2,
                      DIVIDER_X - MARGIN, itemY + PROG_ITEM_H - 2, 6, 6);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hBr);
        }

        // Icon square (centred vertically in the row)
        int iconCX = MARGIN + PROG_ICON_SZ / 2 + 4;
        int iconCY = itemY + PROG_ITEM_H / 2;
        DrawIconSquare(hdc, iconCX, iconCY, PROG_ICON_SZ,
                       s_pinnedItems[i].iconColor, s_pinnedItems[i].shortName);

        // App name
        ::SetTextColor(hdc, m_textColor);
        RECT nr = { MARGIN + PROG_ICON_SZ + 12, itemY,
                    DIVIDER_X - MARGIN,          itemY + PROG_ITEM_H };
        DrawTextW(hdc, s_pinnedItems[i].name, -1, &nr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldF);
    DeleteObject(nameFont);

    // Thin separator below programs list
    int sepY = PROG_Y + PROG_COUNT * PROG_ITEM_H + 4;
    if (sepY < AP_ROW_Y)
        DrawSeparator(hdc, sepY, MARGIN, DIVIDER_X - MARGIN);
}

// ─────────────────────────────────────────────────────────────────────────────
// PaintAllProgramsView — shows the current level of the All Programs tree
// (left column, replacing the pinned list when m_viewMode == AllPrograms)
// ─────────────────────────────────────────────────────────────────────────────
void StartMenuWindow::PaintAllProgramsView(HDC hdc, const RECT& cr) {
    (void)cr;
    SetBkMode(hdc, TRANSPARENT);

    const auto& nodes = CurrentApNodes();
    int total = static_cast<int>(nodes.size());

    // Clamp scroll offset in case the list shrank (e.g. after NavigateBack).
    int maxOff = max(0, total - AP_MAX_VISIBLE);
    if (m_apScrollOffset > maxOff) m_apScrollOffset = maxOff;
    if (m_apScrollOffset < 0)      m_apScrollOffset = 0;

    int count = max(0, min(AP_MAX_VISIBLE, total - m_apScrollOffset));

    HFONT nameFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT boldFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, nameFont);

    for (int i = 0; i < count; ++i) {
        int             nodeIdx = m_apScrollOffset + i;
        const MenuNode& node    = nodes[static_cast<size_t>(nodeIdx)];
        int             itemY   = PROG_Y + i * PROG_ITEM_H;

        // Hover / keyboard-selection highlight (both use absolute nodeIdx).
        bool isKeySel = (nodeIdx == m_keySelApIndex);
        bool isHover  = (nodeIdx == m_hoveredApIndex) && !isKeySel;
        if (isKeySel || isHover) {
            COLORREF hlColor = isKeySel ? CalculateSelectionColor() : CalculateHoverColor();
            HBRUSH hBr  = CreateSolidBrush(hlColor);
            HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op   = (HPEN)SelectObject(hdc, noPn);
            RoundRect(hdc, MARGIN, itemY + 2,
                      DIVIDER_X - MARGIN, itemY + PROG_ITEM_H - 2, 6, 6);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hBr);
        }

        int iconCX = MARGIN + PROG_ICON_SZ / 2 + 4;
        int iconCY = itemY + PROG_ITEM_H / 2;

        if (node.isFolder) {
            // Folder: amber icon with "›" glyph
            DrawIconSquare(hdc, iconCX, iconCY, PROG_ICON_SZ,
                           RGB(210, 150, 20), L"\u203a");
            SelectObject(hdc, boldFont);
        } else {
            // Shortcut: teal icon with "»" glyph
            DrawIconSquare(hdc, iconCX, iconCY, PROG_ICON_SZ,
                           RGB(30, 140, 130), L"\u00bb");
            SelectObject(hdc, nameFont);
        }

        ::SetTextColor(hdc, m_textColor);
        RECT nr = { MARGIN + PROG_ICON_SZ + 12, itemY,
                    DIVIDER_X - MARGIN,          itemY + PROG_ITEM_H };
        DrawTextW(hdc, node.name.c_str(), -1, &nr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // "▲ scroll…" accent when items exist above the visible window.
    if (m_apScrollOffset > 0) {
        SelectObject(hdc, nameFont);
        ::SetTextColor(hdc, CalculateBorderColor());
        RECT tr = { MARGIN, PROG_Y, DIVIDER_X - MARGIN, PROG_Y + PROG_ITEM_H / 2 };
        DrawTextW(hdc, L"\u25b2  scroll\u2026", -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // "▼ more…" hint when items remain below the visible window.
    if (m_apScrollOffset + count < total) {
        int lastY = PROG_Y + count * PROG_ITEM_H;
        SelectObject(hdc, nameFont);
        ::SetTextColor(hdc, CalculateBorderColor());
        RECT mr = { MARGIN, lastY, DIVIDER_X - MARGIN, AP_ROW_Y };
        DrawTextW(hdc, L"\u25bc  more\u2026", -1, &mr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldF);
    DeleteObject(nameFont);
    DeleteObject(boldFont);
}

// ─────────────────────────────────────────────────────────────────────────────
// PaintApRow — "All Programs ›" (Programs view) or "◄ Back" (AllPrograms view)
// Positioned at AP_ROW_Y, height AP_ROW_H.  Acts as a separator above the
// search box; draws a thin rule above itself.
// ─────────────────────────────────────────────────────────────────────────────
void StartMenuWindow::PaintApRow(HDC hdc, const RECT& cr) {
    (void)cr;

    // Thin rule above the row
    DrawSeparator(hdc, AP_ROW_Y - 1, MARGIN, DIVIDER_X - MARGIN);

    // Hover / keyboard-selection highlight
    {
        bool isKeySel = m_keySelApRow;
        bool isHover  = m_hoveredApRow && !isKeySel;
        if (isKeySel || isHover) {
            COLORREF hlColor = isKeySel ? CalculateSelectionColor() : CalculateHoverColor();
            HBRUSH hBr  = CreateSolidBrush(hlColor);
            HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op   = (HPEN)SelectObject(hdc, noPn);
            RoundRect(hdc, MARGIN, AP_ROW_Y + 1,
                      DIVIDER_X - MARGIN, AP_ROW_Y + AP_ROW_H - 1, 4, 4);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hBr);
        }
    }

    HFONT rowFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, rowFont);
    ::SetTextColor(hdc, m_textColor);
    SetBkMode(hdc, TRANSPARENT);

    const wchar_t* label = (m_viewMode == LeftViewMode::AllPrograms)
                           ? L"\u25c4  Back"
                           : L"All Programs  \u203a";

    RECT tr = { MARGIN + 6, AP_ROW_Y, DIVIDER_X - MARGIN, AP_ROW_Y + AP_ROW_H };
    DrawTextW(hdc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldF);
    DeleteObject(rowFont);
}

// ─────────────────────────────────────────────────────────────────────────────
// PaintWin7SearchBox — Win7 style: sits at the bottom of the left column,
// just above the bottom bar.  (Moved from top to bottom per Win7 design.)
// ─────────────────────────────────────────────────────────────────────────────
void StartMenuWindow::PaintWin7SearchBox(HDC hdc, const RECT& cr) {
    (void)cr;

    int bx1 = MARGIN,             by1 = SEARCH_Y;
    int bx2 = DIVIDER_X - MARGIN, by2 = SEARCH_Y + SEARCH_H;

    HBRUSH srBr  = CreateSolidBrush(CalculateSubtleColor());
    HPEN   srPen = CreatePen(PS_SOLID, 1, CalculateBorderColor());
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, srBr);
    HPEN   oldPn = (HPEN)SelectObject(hdc, srPen);
    RoundRect(hdc, bx1, by1, bx2, by2, 6, 6);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(srBr);
    DeleteObject(srPen);

    // Magnifier icon
    int icoX = bx1 + 18, icoY = (by1 + by2) / 2, icoR = 6;
    HPEN mgPen = CreatePen(PS_SOLID, 2, RGB(155, 155, 165));
    HPEN oldP2 = (HPEN)SelectObject(hdc, mgPen);
    HBRUSH nb  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, icoX - icoR, icoY - icoR, icoX + icoR, icoY + icoR);
    MoveToEx(hdc, icoX + icoR - 2, icoY + icoR - 2, NULL);
    LineTo(hdc, icoX + icoR + 4, icoY + icoR + 4);
    SelectObject(hdc, oldP2);
    SelectObject(hdc, nb);
    DeleteObject(mgPen);

    // Placeholder text
    HFONT ph = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, ph);
    ::SetTextColor(hdc, RGB(135, 135, 145));
    SetBkMode(hdc, TRANSPARENT);
    RECT tr = { bx1 + 34, by1, bx2 - 8, by2 };
    DrawTextW(hdc, L"Search programs and files",
              -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldF);
    DeleteObject(ph);
}

// ── PaintWin7RightColumn ──────────────────────────────────────────────────────
// Paints the right-column panel: background, username header, shell links.
// Every non-separator entry in s_rightItems is drawn and is clickable.
void StartMenuWindow::PaintWin7RightColumn(HDC hdc, const RECT& cr) {
    // ── Background ───────────────────────────────────────────────────────────
    COLORREF rcBgColor = CalculateSubtleColor();
    HBRUSH   rcBg      = CreateSolidBrush(rcBgColor);
    RECT     rcArea    = { DIVIDER_X, 0, cr.right, BOTTOM_BAR_Y };
    FillRect(hdc, &rcArea, rcBg);
    DeleteObject(rcBg);

    // ── Vertical divider ─────────────────────────────────────────────────────
    HPEN divPen = CreatePen(PS_SOLID, 1, CalculateBorderColor());
    HPEN oldPen = (HPEN)SelectObject(hdc, divPen);
    MoveToEx(hdc, DIVIDER_X, 0, NULL);
    LineTo(hdc, DIVIDER_X, BOTTOM_BAR_Y);
    SelectObject(hdc, oldPen);
    DeleteObject(divPen);

    SetBkMode(hdc, TRANSPARENT);

    // ── Username header ──────────────────────────────────────────────────────
    int avR  = 18;
    int avCX = RC_X + avR + 4;
    int avCY = RC_HDR_H / 2;

    // Avatar circle
    HBRUSH avBr  = CreateSolidBrush(RGB(0, 103, 192));
    HPEN   noPen = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH ob    = (HBRUSH)SelectObject(hdc, avBr);
    HPEN   op    = (HPEN)SelectObject(hdc, noPen);
    Ellipse(hdc, avCX - avR, avCY - avR, avCX + avR, avCY + avR);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(avBr);

    // Initial inside circle
    wchar_t initial[2] = { m_username[0] ? towupper(m_username[0]) : L'U', 0 };
    HFONT initF = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, initF);
    ::SetTextColor(hdc, RGB(255, 255, 255));
    RECT avTR = { avCX - avR, avCY - avR, avCX + avR, avCY + avR };
    DrawTextW(hdc, initial, 1, &avTR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Username text
    HFONT nmF = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, nmF);
    ::SetTextColor(hdc, m_textColor);
    RECT nmR = { avCX + avR + 8, 0, cr.right - 8, RC_HDR_H };
    DrawTextW(hdc, m_username, -1, &nmR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Thin separator below header
    DrawSeparator(hdc, RC_HDR_H, DIVIDER_X + 8, cr.right - 8);

    // ── Shell link items ─────────────────────────────────────────────────────
    HFONT itemF = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, itemF);

    int y = RC_HDR_H + 2;
    for (int i = 0; i < RIGHT_ITEM_COUNT; ++i) {
        const Win7RightItem& item = s_rightItems[i];

        if (item.isSeparator) {
            // Draw a subtle horizontal line centred in the separator row
            DrawSeparator(hdc, y + RC_SEP_H / 2, RC_X + 4, cr.right - 8);
            y += RC_SEP_H;
        } else {
            // Hover highlight
            if (i == m_hoveredRightIndex) {
                HBRUSH hBr  = CreateSolidBrush(CalculateHoverColor());
                HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
                HBRUSH hOb  = (HBRUSH)SelectObject(hdc, hBr);
                HPEN   hOp  = (HPEN)SelectObject(hdc, noPn);
                RoundRect(hdc, RC_X, y + 1, cr.right - 4, y + RC_ITEM_H - 1, 6, 6);
                SelectObject(hdc, hOb);
                SelectObject(hdc, hOp);
                DeleteObject(hBr);
            }

            // Item label
            ::SetTextColor(hdc, m_textColor);
            RECT tr = { RC_X + 10, y, cr.right - 8, y + RC_ITEM_H };
            DrawTextW(hdc, item.label, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            y += RC_ITEM_H;
        }
    }

    SelectObject(hdc, oldF);
    DeleteObject(initF);
    DeleteObject(nmF);
    DeleteObject(itemF);
}

// ── PaintBottomBar ────────────────────────────────────────────────────────────
void StartMenuWindow::PaintBottomBar(HDC hdc, const RECT& cr) {
    SetBkMode(hdc, TRANSPARENT);

    HBRUSH bbBr = CreateSolidBrush(CalculateSubtleColor());
    RECT   bbR  = { 0, BOTTOM_BAR_Y, cr.right, cr.bottom };
    FillRect(hdc, &bbR, bbBr);
    DeleteObject(bbBr);

    // Thin rule at top of bottom bar
    DrawSeparator(hdc, BOTTOM_BAR_Y, MARGIN, WIDTH - MARGIN);

    int barCY = BOTTOM_BAR_Y + BOTTOM_BAR_H / 2;

    // ── Avatar circle (left side) ──
    int avCX = MARGIN + 14, avR = 13;
    HBRUSH avBr  = CreateSolidBrush(RGB(0, 103, 192));
    HPEN   noPen = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, avBr);
    HPEN   oldPn = (HPEN)SelectObject(hdc, noPen);
    Ellipse(hdc, avCX - avR, barCY - avR, avCX + avR, barCY + avR);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(avBr);

    HFONT initF = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, initF);
    ::SetTextColor(hdc, RGB(255, 255, 255));
    wchar_t initial[2] = { m_username[0] ? towupper(m_username[0]) : L'U', 0 };
    RECT avTR = { avCX - avR, barCY - avR, avCX + avR, barCY + avR };
    DrawTextW(hdc, initial, 1, &avTR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ── "User" label ──
    HFONT nmF = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, nmF);
    ::SetTextColor(hdc, m_textColor);
    RECT nmR = { avCX + avR + 6, BOTTOM_BAR_Y, DIVIDER_X - MARGIN, cr.bottom };
    DrawTextW(hdc, m_username, -1, &nmR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // ── Power button (right side) ──
    int pwrCX = cr.right - MARGIN - POWER_BTN_R;
    int pwrCY = barCY;

    if (m_hoveredPower) {
        HBRUSH hBr  = CreateSolidBrush(CalculateHoverColor());
        HBRUSH ob2  = (HBRUSH)SelectObject(hdc, hBr);
        HPEN   op2  = (HPEN)SelectObject(hdc, noPen);
        Ellipse(hdc, pwrCX - POWER_BTN_R - 4, pwrCY - POWER_BTN_R - 4,
                     pwrCX + POWER_BTN_R + 4, pwrCY + POWER_BTN_R + 4);
        SelectObject(hdc, ob2);
        SelectObject(hdc, op2);
        DeleteObject(hBr);
    }

    COLORREF pwrCol = m_hoveredPower ? RGB(255, 255, 255) : RGB(175, 175, 185);
    HPEN pwrPen  = CreatePen(PS_SOLID, 2, pwrCol);
    HPEN oldPen2 = (HPEN)SelectObject(hdc, pwrPen);
    HBRUSH nb2   = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    int ar = POWER_BTN_R - 3;
    Arc(hdc,
        pwrCX - ar, pwrCY - ar, pwrCX + ar, pwrCY + ar,
        pwrCX - ar + 3, pwrCY - ar,
        pwrCX + ar - 3, pwrCY - ar);
    MoveToEx(hdc, pwrCX, pwrCY - ar + 1, NULL);
    LineTo  (hdc, pwrCX, pwrCY - 1);

    SelectObject(hdc, oldPen2);
    SelectObject(hdc, oldF);
    DeleteObject(pwrPen);
    DeleteObject(initF);
    DeleteObject(nmF);
    (void)nb2;
}

// ── Paint (master) ───────────────────────────────────────────────────────────
void StartMenuWindow::Paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT cr;
    GetClientRect(m_hwnd, &cr);

    // Background (whole window)
    HBRUSH bg = CreateSolidBrush(m_bgColor);
    FillRect(hdc, &cr, bg);
    DeleteObject(bg);

    // Outer border
    HPEN  bdrPen = CreatePen(PS_SOLID, 1, CalculateBorderColor());
    HPEN  oldPn  = (HPEN)SelectObject(hdc, bdrPen);
    HBRUSH nb    = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, 0, 0, cr.right, cr.bottom, 12, 12);
    SelectObject(hdc, oldPn);
    SelectObject(hdc, nb);
    DeleteObject(bdrPen);

    // Left column — programs or All Programs tree
    if (m_viewMode == LeftViewMode::Programs)
        PaintProgramsList(hdc, cr);
    else
        PaintAllProgramsView(hdc, cr);

    // Left column — "All Programs" / "Back" row + search box (always visible)
    PaintApRow(hdc, cr);
    PaintWin7SearchBox(hdc, cr);

    // Right column and bottom bar (always visible)
    PaintWin7RightColumn(hdc, cr);
    PaintBottomBar(hdc, cr);

    EndPaint(m_hwnd, &ps);
}

// ── Hit testing ───────────────────────────────────────────────────────────────

// Returns the pinned item index at pt (Programs view), or -1.
int StartMenuWindow::GetProgItemAtPoint(POINT pt) {
    if (pt.x < MARGIN || pt.x >= DIVIDER_X - MARGIN) return -1;
    if (pt.y < PROG_Y || pt.y >= PROG_Y + PROG_COUNT * PROG_ITEM_H) return -1;
    int idx = (pt.y - PROG_Y) / PROG_ITEM_H;
    if (idx >= 0 && idx < PROG_COUNT) return idx;
    return -1;
}

// Returns true if pt is over the "All Programs" / "Back" row.
bool StartMenuWindow::IsOverApRow(POINT pt) {
    if (pt.x >= DIVIDER_X) return false;
    return pt.y >= AP_ROW_Y && pt.y < AP_ROW_Y + AP_ROW_H;
}

// Returns the All Programs list item absolute index at pt, or -1.
// "Absolute" means m_apScrollOffset + visual row, consistent with
// m_keySelApIndex and m_hoveredApIndex which also use absolute indices.
int StartMenuWindow::GetApItemAtPoint(POINT pt) {
    if (pt.x < MARGIN || pt.x >= DIVIDER_X - MARGIN) return -1;
    if (pt.y < PROG_Y || pt.y >= AP_ROW_Y) return -1;
    int visualIdx = (pt.y - PROG_Y) / PROG_ITEM_H;
    int total = static_cast<int>(CurrentApNodes().size());
    int count = max(0, min(AP_MAX_VISIBLE, total - m_apScrollOffset));
    if (visualIdx >= 0 && visualIdx < count) return m_apScrollOffset + visualIdx;
    return -1;
}

bool StartMenuWindow::IsOverPowerButton(POINT pt) {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    int pwrCX = cr.right - MARGIN - POWER_BTN_R;
    int barCY = BOTTOM_BAR_Y + BOTTOM_BAR_H / 2;
    int dx = pt.x - pwrCX, dy = pt.y - barCY;
    return (dx * dx + dy * dy) <= (POWER_BTN_R + 6) * (POWER_BTN_R + 6);
}

// GetRightItemAtPoint ─────────────────────────────────────────────────────────
// Returns the index into s_rightItems that the cursor is over, or -1.
// Separators and the username header are never returned (not clickable).
int StartMenuWindow::GetRightItemAtPoint(POINT pt) {
    if (pt.x <= DIVIDER_X) return -1;      // left column
    if (pt.y >= BOTTOM_BAR_Y) return -1;   // bottom bar handled separately

    int y = RC_HDR_H + 2;
    for (int i = 0; i < RIGHT_ITEM_COUNT; ++i) {
        const Win7RightItem& item = s_rightItems[i];
        int rowH = item.isSeparator ? RC_SEP_H : RC_ITEM_H;

        if (!item.isSeparator) {
            RECT r = { RC_X, y + 1, WIDTH - 4, y + rowH - 1 };
            if (PtInRect(&r, pt)) return i;
        }
        y += rowH;
    }
    return -1;
}

// ── Execution ─────────────────────────────────────────────────────────────────
void StartMenuWindow::ExecutePinnedItem(int index) {
    if (index < 0 || index >= PROG_COUNT) return;
    CF_LOG(Info, "ExecutePinnedItem: " << index);
    ShellExecuteW(NULL, L"open", s_pinnedItems[index].command, NULL, NULL, SW_SHOW);
    Hide();
}

void StartMenuWindow::ExecuteRecommendedItem(int index) {
    if (index < 0 || index >= 7 || !m_menuItems[index].visible) return;
    CF_LOG(Info, "ExecuteRecommendedItem: " << index);
    Hide();
    switch (index) {
        case 0: ShellExecuteW(NULL, L"open",    L"control",               NULL, NULL, SW_SHOW); break;
        case 1: ShellExecuteW(NULL, L"open",    L"devmgmt.msc",           NULL, NULL, SW_SHOW); break;
        case 2: ShellExecuteW(NULL, L"open",    L"ms-settings:appsfeatures", NULL, NULL, SW_SHOW); break;
        case 3: ShellExecuteW(NULL, L"explore", L"shell:Personal",        NULL, NULL, SW_SHOW); break;
        case 4: ShellExecuteW(NULL, L"explore", L"shell:My Pictures",     NULL, NULL, SW_SHOW); break;
        case 5: ShellExecuteW(NULL, L"explore", L"shell:My Video",        NULL, NULL, SW_SHOW); break;
        case 6: ShellExecuteW(NULL, L"explore", L"shell:Recent",          NULL, NULL, SW_SHOW); break;
    }
}

// ExecuteRightItem ─────────────────────────────────────────────────────────────
// Launches the correct target for a Win7 right-column item.
// Known-folder items: resolved at click-time via SHGetKnownFolderPath so that
// folder redirection (e.g. Documents on a different drive) is respected.
// Shell-applet items: called via ShellExecuteW with verb + target.
// On any failure, a diagnostic is logged; no silent failures.
void StartMenuWindow::ExecuteRightItem(int index) {
    if (index < 0 || index >= RIGHT_ITEM_COUNT) return;
    const Win7RightItem& item = s_rightItems[index];
    if (item.isSeparator) return;

    CF_LOG(Info, "ExecuteRightItem: index=" << index);

    Hide();   // close menu before launching to avoid z-order issues

    if (item.folderId) {
        // ── Known-folder path (preferred) ───────────────────────────────────
        PWSTR  path = nullptr;
        HRESULT hr  = SHGetKnownFolderPath(*item.folderId, KF_FLAG_DEFAULT,
                                           NULL, &path);
        if (SUCCEEDED(hr) && path) {
            HINSTANCE hi = ShellExecuteW(NULL, item.verb, path, NULL, NULL, SW_SHOW);
            if (reinterpret_cast<INT_PTR>(hi) <= 32) {
                CF_LOG(Warning, "ShellExecuteW(known folder) returned "
                       << reinterpret_cast<INT_PTR>(hi));
            }
            CoTaskMemFree(path);
        } else {
            CF_LOG(Warning, "SHGetKnownFolderPath failed hr=0x"
                   << std::hex << hr << std::dec
                   << " for item " << index);
        }
    } else {
        // ── Shell applet / URI ───────────────────────────────────────────────
        HINSTANCE hi = ShellExecuteW(NULL, item.verb, item.target,
                                     item.params, NULL, SW_SHOW);
        if (reinterpret_cast<INT_PTR>(hi) <= 32) {
            CF_LOG(Warning, "ShellExecuteW(applet) returned "
                   << reinterpret_cast<INT_PTR>(hi)
                   << " for right-column item " << index);
        }
    }
}

// LaunchApItem ────────────────────────────────────────────────────────────────
// Launches the shortcut at index in CurrentApNodes().
// If the node is a folder, navigates into it instead of launching.
void StartMenuWindow::LaunchApItem(int index) {
    const auto& nodes = CurrentApNodes();
    if (index < 0 || index >= static_cast<int>(nodes.size())) return;

    const MenuNode& node = nodes[static_cast<size_t>(index)];

    if (node.isFolder) {
        CF_LOG(Info, "AP navigate into folder: " << index);
        NavigateIntoFolder(node.children);
        return;
    }

    CF_LOG(Info, "AP launch: index=" << index);
    Hide();

    if (!node.target.empty()) {
        HINSTANCE hi = ShellExecuteW(
            NULL, L"open",
            node.target.c_str(),
            node.args.empty() ? nullptr : node.args.c_str(),
            nullptr, SW_SHOW);
        if (reinterpret_cast<INT_PTR>(hi) <= 32) {
            CF_LOG(Warning, "ShellExecuteW(AP item) returned "
                   << reinterpret_cast<INT_PTR>(hi)
                   << " target=" << node.target.size() << " chars");
        }
    } else {
        CF_LOG(Warning, "AP item has no target: index=" << index);
    }
}

// ── All Programs navigation ───────────────────────────────────────────────────

const std::vector<MenuNode>& StartMenuWindow::CurrentApNodes() const {
    if (m_apNavStack.empty()) return m_programTree;
    return *m_apNavStack.back();
}

void StartMenuWindow::NavigateIntoFolder(const std::vector<MenuNode>& children) {
    m_apNavStack.push_back(&children);
    m_hoveredApIndex = -1;
    m_keySelApIndex  = -1;
    m_keySelApRow    = false;
    m_apScrollOffset = 0;
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    CF_LOG(Info, "AP drill-down: depth=" << m_apNavStack.size()
           << " nodes=" << children.size());
}

void StartMenuWindow::NavigateBack() {
    if (!m_apNavStack.empty()) {
        m_apNavStack.pop_back();
        m_hoveredApIndex = -1;
        m_keySelApIndex  = -1;
        m_keySelApRow    = false;
        m_apScrollOffset = 0;
        CF_LOG(Info, "AP navigate back: depth=" << m_apNavStack.size());
    } else {
        // Already at root All Programs level — return to Programs view
        m_viewMode         = LeftViewMode::Programs;
        m_hoveredProgIndex = -1;
        m_keySelApIndex    = -1;
        m_keySelApRow      = false;
        CF_LOG(Info, "AP navigate back to Programs view");
    }
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
}

// ── Power menu ────────────────────────────────────────────────────────────────
void StartMenuWindow::ShowPowerMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, 1, L"Sleep");
    AppendMenuW(menu, MF_STRING, 2, L"Shut down");
    AppendMenuW(menu, MF_STRING, 3, L"Restart");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Cancel");

    RECT wr = {};
    GetWindowRect(m_hwnd, &wr);
    int x = wr.right  - MARGIN - POWER_BTN_R * 2;
    int y = wr.bottom - BOTTOM_BAR_H - 4;

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                             x, y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == 4 || cmd == 0) return;
    Hide();

    switch (cmd) {
        case 1: SetSuspendState(FALSE, FALSE, FALSE); break;
        case 2: ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER); break;
        case 3: ExitWindowsEx(EWX_REBOOT   | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER); break;
    }
}

// ── Window procedure ──────────────────────────────────────────────────────────
LRESULT CALLBACK StartMenuWindow::WindowProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
    StartMenuWindow* win = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        win = reinterpret_cast<StartMenuWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(win));
    } else {
        win = reinterpret_cast<StartMenuWindow*>(
                  GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (win) return win->HandleMessage(msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT StartMenuWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PAINT:
        Paint();
        return 0;

    case WM_MOUSEMOVE: {
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize    = sizeof(tme);
            tme.dwFlags   = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            m_trackingMouse = true;
        }
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        int  nProg  = (m_viewMode == LeftViewMode::Programs)
                      ? GetProgItemAtPoint(pt) : -1;
        bool nApRow = IsOverApRow(pt);
        int  nAp    = (m_viewMode == LeftViewMode::AllPrograms)
                      ? GetApItemAtPoint(pt) : -1;
        int  nrc    = GetRightItemAtPoint(pt);
        bool npwr   = IsOverPowerButton(pt);

        // Mouse movement clears keyboard selection (modes are mutually exclusive)
        bool hadKeySel = (m_keySelProgIndex >= 0 || m_keySelApRow || m_keySelApIndex >= 0);

        if (nProg != m_hoveredProgIndex  ||
            nApRow != m_hoveredApRow     ||
            nAp    != m_hoveredApIndex   ||
            nrc    != m_hoveredRightIndex ||
            npwr   != m_hoveredPower     ||
            hadKeySel) {
            m_hoveredProgIndex  = nProg;
            m_hoveredApRow      = nApRow;
            m_hoveredApIndex    = nAp;
            m_hoveredRightIndex = nrc;
            m_hoveredPower      = npwr;
            if (hadKeySel) {
                m_keySelProgIndex = -1;
                m_keySelApRow     = false;
                m_keySelApIndex   = -1;
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse     = false;
        m_hoveredProgIndex  = -1;
        m_hoveredApRow      = false;
        m_hoveredApIndex    = -1;
        m_hoveredRightIndex = -1;
        m_hoveredPower      = false;
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Right column — Win7 shell links (checked first when x > DIVIDER_X)
        int rc = GetRightItemAtPoint(pt);
        if (rc >= 0) { ExecuteRightItem(rc); return 0; }

        // "All Programs" / "Back" row
        if (IsOverApRow(pt)) {
            if (m_viewMode == LeftViewMode::Programs) {
                m_viewMode       = LeftViewMode::AllPrograms;
                m_hoveredApIndex = -1;
                m_apNavStack.clear();
                CF_LOG(Info, "Switching to All Programs view");
            } else {
                NavigateBack();
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        // Left column — Programs view: pinned app launch
        if (m_viewMode == LeftViewMode::Programs) {
            int p = GetProgItemAtPoint(pt);
            if (p >= 0) { ExecutePinnedItem(p); return 0; }
        }

        // Left column — All Programs view: folder/item activation
        if (m_viewMode == LeftViewMode::AllPrograms) {
            int ap = GetApItemAtPoint(pt);
            if (ap >= 0) { LaunchApItem(ap); return 0; }
        }

        // Bottom bar — power button
        if (IsOverPowerButton(pt)) { ShowPowerMenu(); return 0; }

        // Search box — open Windows Search
        {
            RECT sr = { MARGIN, SEARCH_Y, DIVIDER_X - MARGIN, SEARCH_Y + SEARCH_H };
            if (PtInRect(&sr, pt))
                ShellExecuteW(NULL, L"open", L"ms-search:", NULL, NULL, SW_SHOW);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (m_viewMode == LeftViewMode::AllPrograms)
                NavigateBack();
            else
                Hide();
            return 0;
        }

        if (wParam == VK_DOWN || wParam == VK_UP) {
            bool down = (wParam == VK_DOWN);

            if (m_viewMode == LeftViewMode::Programs) {
                // Navigation range: 0..PROG_COUNT-1, then AP row
                if (down) {
                    if (m_keySelApRow) {
                        // already at bottom; clamp
                    } else if (m_keySelProgIndex < 0) {
                        m_keySelProgIndex = 0;
                    } else if (m_keySelProgIndex < PROG_COUNT - 1) {
                        ++m_keySelProgIndex;
                    } else {
                        m_keySelApRow     = true;
                        m_keySelProgIndex = -1;
                    }
                } else {
                    if (m_keySelApRow) {
                        m_keySelApRow     = false;
                        m_keySelProgIndex = PROG_COUNT - 1;
                    } else if (m_keySelProgIndex > 0) {
                        --m_keySelProgIndex;
                    } else if (m_keySelProgIndex == 0) {
                        // clamp at top
                    } else {
                        m_keySelProgIndex = 0;   // start navigation from top
                    }
                }
            } else {
                // AllPrograms view: absolute range 0..total-1, then Back row.
                // m_keySelApIndex is an absolute node index (not a visual row).
                int total = static_cast<int>(CurrentApNodes().size());
                if (down) {
                    if (m_keySelApRow) {
                        // already at bottom; clamp
                    } else if (m_keySelApIndex < 0) {
                        m_keySelApIndex = 0;
                    } else if (m_keySelApIndex < total - 1) {
                        ++m_keySelApIndex;
                    } else {
                        m_keySelApRow   = true;
                        m_keySelApIndex = -1;
                    }
                } else {
                    if (m_keySelApRow) {
                        m_keySelApRow   = false;
                        m_keySelApIndex = (total > 0) ? total - 1 : -1;
                    } else if (m_keySelApIndex > 0) {
                        --m_keySelApIndex;
                    } else if (m_keySelApIndex == 0) {
                        // clamp at top
                    } else {
                        m_keySelApIndex = 0;
                    }
                }
                // Auto-scroll so the selected item stays in the visible window.
                if (m_keySelApIndex >= 0) {
                    if (m_keySelApIndex < m_apScrollOffset)
                        m_apScrollOffset = m_keySelApIndex;
                    else if (m_keySelApIndex >= m_apScrollOffset + AP_MAX_VISIBLE)
                        m_apScrollOffset = m_keySelApIndex - AP_MAX_VISIBLE + 1;
                }
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        if (wParam == VK_RETURN) {
            if (m_viewMode == LeftViewMode::Programs) {
                if (m_keySelApRow) {
                    // Enter on "All Programs" row → switch view
                    m_keySelApRow    = false;
                    m_keySelApIndex  = -1;
                    m_viewMode       = LeftViewMode::AllPrograms;
                    m_apNavStack.clear();
                    CF_LOG(Info, "Keyboard: switch to All Programs view");
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (m_keySelProgIndex >= 0) {
                    int idx           = m_keySelProgIndex;
                    m_keySelProgIndex = -1;
                    ExecutePinnedItem(idx);
                }
            } else {
                if (m_keySelApRow) {
                    m_keySelApRow = false;
                    NavigateBack();
                } else if (m_keySelApIndex >= 0) {
                    int idx         = m_keySelApIndex;
                    m_keySelApIndex = -1;
                    LaunchApItem(idx);
                }
            }
            return 0;
        }

        return 0;

    default:
        return DefWindowProc(m_hwnd, msg, wParam, lParam);
    }
}

// ── Name helpers ──────────────────────────────────────────────────────────────
const wchar_t* StartMenuWindow::GetMenuItemName(int index) {
    if (index >= 0 && index < 7 && !m_customMenuNames[index].empty())
        return m_customMenuNames[index].c_str();
    return m_menuItems[index].name;
}

const wchar_t* StartMenuWindow::GetTitle() {
    if (!m_customTitle.empty()) return m_customTitle.c_str();
    return L"CrystalFrame";
}

// ── Persistence ───────────────────────────────────────────────────────────────
void StartMenuWindow::LoadCustomNames() {
    PWSTR lap = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &lap))) return;
    std::wstring path = std::wstring(lap) + L"\\CrystalFrame\\menu_names.json";
    CoTaskMemFree(lap);

    std::wifstream f(path);
    if (!f.is_open()) return;

    std::wstring line;
    while (std::getline(f, line)) {
        size_t pos = line.find(L"\"menu_");
        if (pos != std::wstring::npos) {
            int idx   = line[pos + 6] - L'0';
            size_t c  = line.find(L':', pos);
            size_t q1 = line.find(L'\"', c);
            size_t q2 = line.find(L'\"', q1 + 1);
            if (idx >= 0 && idx < 7 && q1 != std::wstring::npos)
                m_customMenuNames[idx] = line.substr(q1 + 1, q2 - q1 - 1);
        }
        size_t tp = line.find(L"\"title\"");
        if (tp != std::wstring::npos) {
            size_t c  = line.find(L':', tp);
            size_t q1 = line.find(L'\"', c);
            size_t q2 = line.find(L'\"', q1 + 1);
            if (q1 != std::wstring::npos)
                m_customTitle = line.substr(q1 + 1, q2 - q1 - 1);
        }
    }
}

void StartMenuWindow::SaveCustomNames() {
    PWSTR lap = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &lap))) return;
    std::wstring dir = std::wstring(lap) + L"\\CrystalFrame";
    CoTaskMemFree(lap);
    CreateDirectoryW(dir.c_str(), NULL);

    std::wofstream f(dir + L"\\menu_names.json");
    if (!f.is_open()) return;

    std::vector<std::wstring> entries;
    for (int i = 0; i < 7; ++i)
        if (!m_customMenuNames[i].empty())
            entries.push_back(L"  \"menu_" + std::to_wstring(i) +
                              L"\": \"" + m_customMenuNames[i] + L"\"");
    if (!m_customTitle.empty())
        entries.push_back(L"  \"title\": \"" + m_customTitle + L"\"");

    f << L"{\n";
    for (size_t i = 0; i < entries.size(); ++i)
        f << entries[i] << (i + 1 < entries.size() ? L",\n" : L"\n");
    f << L"}\n";
}

// ── Edit dialog (rename recommended item) ────────────────────────────────────
struct EditDialogData {
    StartMenuWindow* window;
    int              itemIndex;
    HWND             editCtrl;
};

void StartMenuWindow::ShowEditDialog(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= 7) return;

    auto* data = new EditDialogData{ this, itemIndex, nullptr };
    int dlgW = 360, dlgH = 140;
    int sx   = GetSystemMetrics(SM_CXSCREEN);
    int sy   = GetSystemMetrics(SM_CYSCREEN);

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        EDIT_DIALOG_CLASS, L"Rename item",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        (sx - dlgW) / 2, (sy - dlgH) / 2,
        dlgW, dlgH,
        m_hwnd, NULL, GetModuleHandle(NULL), data);

    if (dlg) {
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        MSG msg;
        while (IsWindow(dlg) && GetMessage(&msg, NULL, 0, 0)) {
            if (!IsDialogMessage(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
    delete data;
}

LRESULT CALLBACK StartMenuWindow::EditDialogProc(HWND hwnd, UINT msg,
                                                  WPARAM wParam, LPARAM lParam) {
    EditDialogData* data = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        data = reinterpret_cast<EditDialogData*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

        data->editCtrl = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 40, 330, 24, hwnd, (HMENU)1,
            GetModuleHandle(NULL), NULL);

        int idx            = data->itemIndex;
        const wchar_t* cur = (!data->window->m_customMenuNames[idx].empty())
            ? data->window->m_customMenuNames[idx].c_str()
            : data->window->m_menuItems[idx].name;
        SetWindowTextW(data->editCtrl, cur);

        CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            110, 80, 80, 28, hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            210, 80, 80, 28, hwnd, (HMENU)3, GetModuleHandle(NULL), NULL);
        return 0;
    }

    data = reinterpret_cast<EditDialogData*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 2 && data) {   // OK
            wchar_t buf[256] = {};
            GetWindowTextW(data->editCtrl, buf, 255);
            data->window->m_customMenuNames[data->itemIndex] = buf;
            data->window->SaveCustomNames();
            if (data->window->m_hwnd)
                InvalidateRect(data->window->m_hwnd, NULL, FALSE);
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == 3) {    // Cancel
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace CrystalFrame
