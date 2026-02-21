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

// ── Pinned apps (2 × 3 grid, left column) ────────────────────────────────────
const PinnedItem StartMenuWindow::s_pinnedItems[StartMenuWindow::PINNED_COUNT] = {
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
        m_visible                 = false;
        m_hoveredPinnedIndex      = -1;
        m_hoveredRecommendedIndex = -1;
        m_hoveredRightIndex       = -1;
        m_hoveredPower            = false;
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
    if (m_visible) InvalidateRect(m_hwnd, NULL, FALSE);
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
    RoundRect(hdc, x1, y1, x2, y2, 10, 10);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(icoBr);

    if (label && label[0]) {
        HFONT font = CreateFontW(
            14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
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

// ── PaintSearchBox (left column) ──────────────────────────────────────────────
void StartMenuWindow::PaintSearchBox(HDC hdc, const RECT& cr) {
    // Clip search box to left column
    int bx1 = MARGIN, by1 = SEARCH_Y;
    int bx2 = DIVIDER_X - MARGIN, by2 = SEARCH_Y + SEARCH_H;
    (void)cr; // right edge determined by DIVIDER_X, not cr.right

    HBRUSH srBr  = CreateSolidBrush(CalculateSubtleColor());
    HPEN   srPen = CreatePen(PS_SOLID, 1, CalculateBorderColor());
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, srBr);
    HPEN   oldPn = (HPEN)SelectObject(hdc, srPen);
    RoundRect(hdc, bx1, by1, bx2, by2, 8, 8);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(srBr);
    DeleteObject(srPen);

    // Magnifier icon
    int icoX = bx1 + 22, icoY = (by1 + by2) / 2, icoR = 7;
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
    HFONT ph = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, ph);
    ::SetTextColor(hdc, RGB(135, 135, 145));
    SetBkMode(hdc, TRANSPARENT);
    RECT tr = { bx1 + 42, by1, bx2 - 10, by2 };
    DrawTextW(hdc, L"Search programs and files",
              -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldF);
    DeleteObject(ph);
}

// ── PaintPinnedSection (left column) ─────────────────────────────────────────
void StartMenuWindow::PaintPinnedSection(HDC hdc, const RECT& cr) {
    (void)cr;
    SetBkMode(hdc, TRANSPARENT);

    HFONT hdrFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT lblFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // "Pinned" header — left half of left column
    HFONT oldF = (HFONT)SelectObject(hdc, hdrFont);
    ::SetTextColor(hdc, m_textColor);
    RECT hdrL = { MARGIN, PINNED_HEADER_Y, DIVIDER_X / 2, PINNED_HEADER_Y + 22 };
    DrawTextW(hdc, L"Pinned", -1, &hdrL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // "All apps ›" — right half of left column
    ::SetTextColor(hdc, RGB(130, 155, 200));
    RECT hdrR = { DIVIDER_X / 2, PINNED_HEADER_Y,
                  DIVIDER_X - MARGIN, PINNED_HEADER_Y + 22 };
    DrawTextW(hdc, L"All apps  \u203a", -1, &hdrR,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Grid items
    for (int i = 0; i < PINNED_COUNT; ++i) {
        int col   = i % PINNED_COLS;
        int row   = i / PINNED_COLS;
        int cellX = MARGIN + col * PINNED_CELL_W;
        int cellY = PINNED_GRID_Y + row * PINNED_CELL_H;
        int cx    = cellX + PINNED_CELL_W / 2;
        int cy    = cellY + PINNED_ICON_SZ / 2 + 8;

        if (i == m_hoveredPinnedIndex) {
            HBRUSH hBr  = CreateSolidBrush(CalculateHoverColor());
            HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op   = (HPEN)SelectObject(hdc, noPn);
            RoundRect(hdc, cellX + 4, cellY + 4,
                      cellX + PINNED_CELL_W - 4, cellY + PINNED_CELL_H - 4,
                      10, 10);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hBr);
        }

        DrawIconSquare(hdc, cx, cy, PINNED_ICON_SZ,
                       s_pinnedItems[i].iconColor,
                       s_pinnedItems[i].shortName);

        SelectObject(hdc, lblFont);
        ::SetTextColor(hdc, m_textColor);
        int labelY = cellY + PINNED_ICON_SZ + 16;
        RECT nr = { cellX + 4, labelY,
                    cellX + PINNED_CELL_W - 4, labelY + 18 };
        DrawTextW(hdc, s_pinnedItems[i].name, -1, &nr,
                  DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldF);
    DeleteObject(hdrFont);
    DeleteObject(lblFont);

    // Divider below grid (left column only)
    DrawSeparator(hdc, PINNED_GRID_END + 6, MARGIN, DIVIDER_X - MARGIN);
}

// ── PaintRecommendedSection (left column) ────────────────────────────────────
void StartMenuWindow::PaintRecommendedSection(HDC hdc, const RECT& cr) {
    (void)cr;
    SetBkMode(hdc, TRANSPARENT);

    HFONT hdrFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT itmFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // "Recommended" header — left half of left column
    HFONT oldF = (HFONT)SelectObject(hdc, hdrFont);
    ::SetTextColor(hdc, m_textColor);
    RECT hl = { MARGIN, REC_HEADER_Y, DIVIDER_X / 2, REC_HEADER_Y + 22 };
    DrawTextW(hdc, L"Recommended", -1, &hl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ::SetTextColor(hdc, RGB(130, 155, 200));
    RECT hr = { DIVIDER_X / 2, REC_HEADER_Y,
                DIVIDER_X - MARGIN, REC_HEADER_Y + 22 };
    DrawTextW(hdc, L"More  \u203a", -1, &hr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    static const COLORREF recColors[7] = {
        RGB(  0, 120, 215), RGB(  0, 150, 130), RGB( 70, 130, 180),
        RGB(200, 100,  20), RGB(100, 180,  50), RGB(180,  50, 100),
        RGB(150, 100, 200)
    };

    // Two-column layout inside left column
    int colW   = (DIVIDER_X - 2 * MARGIN) / 2;
    int visIdx = 0;

    for (int i = 0; i < 7 && visIdx < 4; ++i) {
        if (!m_menuItems[i].visible) continue;

        int col   = visIdx % 2;
        int row   = visIdx / 2;
        int itemX = MARGIN + col * colW;
        int itemY = REC_START_Y + row * REC_ITEM_H;

        if (i == m_hoveredRecommendedIndex) {
            HBRUSH hBr  = CreateSolidBrush(CalculateHoverColor());
            HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op   = (HPEN)SelectObject(hdc, noPn);
            RoundRect(hdc, itemX + 2, itemY + 2,
                      itemX + colW - 2, itemY + REC_ITEM_H - 2,
                      8, 8);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hBr);
        }

        int iconCX = itemX + 20;
        int iconCY = itemY + REC_ITEM_H / 2;
        DrawIconSquare(hdc, iconCX, iconCY, 24, recColors[i], L"");

        SelectObject(hdc, itmFont);
        ::SetTextColor(hdc, m_textColor);
        RECT nr = { itemX + 36, itemY,
                    itemX + colW - 6, itemY + REC_ITEM_H };
        DrawTextW(hdc, GetMenuItemName(i), -1, &nr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        ++visIdx;
    }

    SelectObject(hdc, oldF);
    DeleteObject(hdrFont);
    DeleteObject(itmFont);

    // Divider above bottom bar (full width)
    DrawSeparator(hdc, BOTTOM_BAR_Y - 1, MARGIN, WIDTH - MARGIN);
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

    int barCY = BOTTOM_BAR_Y + (cr.bottom - BOTTOM_BAR_Y) / 2;

    // ── Avatar circle (left side) ──
    int avCX = MARGIN + 18, avR = 16;
    HBRUSH avBr  = CreateSolidBrush(RGB(0, 103, 192));
    HPEN   noPen = (HPEN)GetStockObject(NULL_PEN);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, avBr);
    HPEN   oldPn = (HPEN)SelectObject(hdc, noPen);
    Ellipse(hdc, avCX - avR, barCY - avR, avCX + avR, barCY + avR);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(avBr);

    HFONT initF = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, initF);
    ::SetTextColor(hdc, RGB(255, 255, 255));
    wchar_t initial[2] = { m_username[0] ? towupper(m_username[0]) : L'U', 0 };
    RECT avTR = { avCX - avR, barCY - avR, avCX + avR, barCY + avR };
    DrawTextW(hdc, initial, 1, &avTR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ── "User" label ──
    HFONT nmF = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, nmF);
    ::SetTextColor(hdc, m_textColor);
    RECT nmR = { avCX + avR + 8, BOTTOM_BAR_Y, DIVIDER_X - MARGIN, cr.bottom };
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
    int ar = POWER_BTN_R - 4;
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

    // Background (left column area)
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

    PaintSearchBox(hdc, cr);
    PaintPinnedSection(hdc, cr);
    PaintRecommendedSection(hdc, cr);
    PaintWin7RightColumn(hdc, cr);   // Win7 right column (drawn after left sections)
    PaintBottomBar(hdc, cr);

    EndPaint(m_hwnd, &ps);
}

// ── Hit testing ───────────────────────────────────────────────────────────────
int StartMenuWindow::GetPinnedItemAtPoint(POINT pt) {
    for (int i = 0; i < PINNED_COUNT; ++i) {
        int col   = i % PINNED_COLS;
        int row   = i / PINNED_COLS;
        int cellX = MARGIN + col * PINNED_CELL_W;
        int cellY = PINNED_GRID_Y + row * PINNED_CELL_H;
        RECT r = { cellX + 4, cellY + 4,
                   cellX + PINNED_CELL_W - 4,
                   cellY + PINNED_CELL_H - 4 };
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

int StartMenuWindow::GetRecommendedItemAtPoint(POINT pt) {
    if (pt.x >= DIVIDER_X) return -1;  // right column — not recommended
    int colW   = (DIVIDER_X - 2 * MARGIN) / 2;
    int visIdx = 0;
    for (int i = 0; i < 7 && visIdx < 4; ++i) {
        if (!m_menuItems[i].visible) continue;
        int col   = visIdx % 2;
        int row   = visIdx / 2;
        int itemX = MARGIN + col * colW;
        int itemY = REC_START_Y + row * REC_ITEM_H;
        RECT r = { itemX + 2, itemY + 2,
                   itemX + colW - 2, itemY + REC_ITEM_H - 2 };
        if (PtInRect(&r, pt)) return i;
        ++visIdx;
    }
    return -1;
}

bool StartMenuWindow::IsOverPowerButton(POINT pt) {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    int pwrCX = cr.right - MARGIN - POWER_BTN_R;
    int barCY = BOTTOM_BAR_Y + (cr.bottom - BOTTOM_BAR_Y) / 2;
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
    if (index < 0 || index >= PINNED_COUNT) return;
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
    int y = wr.bottom - 62;

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
        int  np   = GetPinnedItemAtPoint(pt);
        int  nr   = GetRecommendedItemAtPoint(pt);
        int  nrc  = GetRightItemAtPoint(pt);
        bool npwr = IsOverPowerButton(pt);
        if (np   != m_hoveredPinnedIndex      ||
            nr   != m_hoveredRecommendedIndex  ||
            nrc  != m_hoveredRightIndex        ||
            npwr != m_hoveredPower) {
            m_hoveredPinnedIndex      = np;
            m_hoveredRecommendedIndex = nr;
            m_hoveredRightIndex       = nrc;
            m_hoveredPower            = npwr;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse           = false;
        m_hoveredPinnedIndex      = -1;
        m_hoveredRecommendedIndex = -1;
        m_hoveredRightIndex       = -1;
        m_hoveredPower            = false;
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Right column — Win7 shell links (checked first when x > DIVIDER_X)
        int rc = GetRightItemAtPoint(pt);
        if (rc >= 0) { ExecuteRightItem(rc); return 0; }

        // Left column — pinned apps
        int p = GetPinnedItemAtPoint(pt);
        if (p >= 0) { ExecutePinnedItem(p); return 0; }

        // Left column — recommended items
        int r = GetRecommendedItemAtPoint(pt);
        if (r >= 0) { ExecuteRecommendedItem(r); return 0; }

        // Bottom bar — power button
        if (IsOverPowerButton(pt)) { ShowPowerMenu(); }

        // TODO Phase S2: detect click on "All Programs" label and switch left column
        // to the All Programs tree view using m_programTree.


        // Search box — open Windows Search
        {
            int bx1 = MARGIN, by1 = SEARCH_Y;
            int bx2 = DIVIDER_X - MARGIN, by2 = SEARCH_Y + SEARCH_H;
            RECT sr = { bx1, by1, bx2, by2 };
            if (PtInRect(&sr, pt))
                ShellExecuteW(NULL, L"open", L"ms-search:", NULL, NULL, SW_SHOW);
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int r = GetRecommendedItemAtPoint(pt);
        if (r >= 0) ShowEditDialog(r);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) Hide();
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
