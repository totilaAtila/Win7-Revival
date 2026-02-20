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

#include <powrprof.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "powrprof.lib")

namespace CrystalFrame {

// ── Pinned apps (3 × 3 grid) ────────────────────────────────────────────────
const PinnedItem StartMenuWindow::s_pinnedItems[StartMenuWindow::PINNED_COUNT] = {
    { L"Settings",       L"Set",  L"ms-settings:",     RGB(  0, 103, 192) },
    { L"File Explorer",  L"Exp",  L"explorer.exe",     RGB(255, 185,   0) },
    { L"Edge",           L"Edge", L"msedge.exe",       RGB(  0, 120, 215) },
    { L"Calculator",     L"Calc", L"calc.exe",         RGB( 16, 110, 190) },
    { L"Notepad",        L"Note", L"notepad.exe",      RGB( 40, 160,  40) },
    { L"Task Manager",   L"Task", L"taskmgr.exe",      RGB(196,  43,  28) },
    { L"Control Panel",  L"CP",   L"control",          RGB(  0, 150, 130) },
    { L"Command Prompt", L"CMD",  L"cmd.exe",          RGB( 12,  12,  12) },
    { L"Snipping Tool",  L"Snip", L"snippingtool.exe", RGB(118,   0, 197) },
};

// ── Constructor / Destructor ─────────────────────────────────────────────────
StartMenuWindow::StartMenuWindow() {
    LoadCustomNames();
}

StartMenuWindow::~StartMenuWindow() {
    Shutdown();
}

// ── Initialization ───────────────────────────────────────────────────────────
bool StartMenuWindow::Initialize() {
    CF_LOG(Info, "StartMenuWindow::Initialize (Win11 layout)");

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

    CF_LOG(Info, "Start Menu window created (Win11 style) HWND=0x"
                 << std::hex << reinterpret_cast<uintptr_t>(m_hwnd) << std::dec);
    return true;
}

// ── Show / Hide ──────────────────────────────────────────────────────────────
void StartMenuWindow::Show(int /*x*/, int /*y*/) {
    CF_LOG(Info, "StartMenuWindow::Show");

    if (!m_hwnd && !CreateMenuWindow()) return;

    // Position: 1 px above taskbar top edge
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT tbRect  = {};
    int  menuY   = 0;

    if (taskbar && GetWindowRect(taskbar, &tbRect)) {
        menuY = tbRect.top - HEIGHT - 1;
    } else {
        menuY = GetSystemMetrics(SM_CYSCREEN) - HEIGHT - 48;
    }
    if (menuY < 0) menuY = 0;

    // Center horizontally on primary monitor (Win11 style)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int menuX   = (screenW - WIDTH) / 2;
    if (menuX < 0) menuX = 0;

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
        ApplyTransparency();          // sync DWM accent/tint with new color
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

    // GradientColor is ABGR
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

// ── PaintPinnedSection ────────────────────────────────────────────────────────
void StartMenuWindow::PaintPinnedSection(HDC hdc, const RECT& cr) {
    SetBkMode(hdc, TRANSPARENT);

    HFONT hdrFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT lblFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // "Pinned" header
    HFONT oldF = (HFONT)SelectObject(hdc, hdrFont);
    ::SetTextColor(hdc, m_textColor);
    RECT hdrL = { MARGIN, PINNED_HEADER_Y, cr.right / 2, PINNED_HEADER_Y + 22 };
    DrawTextW(hdc, L"Pinned", -1, &hdrL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // "All apps ›" — right side
    ::SetTextColor(hdc, RGB(130, 155, 200));
    RECT hdrR = { cr.right / 2, PINNED_HEADER_Y,
                  cr.right - MARGIN, PINNED_HEADER_Y + 22 };
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

        // Hover highlight
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

        // Icon
        DrawIconSquare(hdc, cx, cy, PINNED_ICON_SZ,
                       s_pinnedItems[i].iconColor,
                       s_pinnedItems[i].shortName);

        // App name
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

    // Divider below grid
    DrawSeparator(hdc, PINNED_GRID_END + 6, MARGIN, cr.right - MARGIN);
}

// ── PaintRecommendedSection ───────────────────────────────────────────────────
void StartMenuWindow::PaintRecommendedSection(HDC hdc, const RECT& cr) {
    SetBkMode(hdc, TRANSPARENT);

    HFONT hdrFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT itmFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    // "Recommended" header
    HFONT oldF = (HFONT)SelectObject(hdc, hdrFont);
    ::SetTextColor(hdc, m_textColor);
    RECT hl = { MARGIN, REC_HEADER_Y, cr.right / 2, REC_HEADER_Y + 22 };
    DrawTextW(hdc, L"Recommended", -1, &hl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    ::SetTextColor(hdc, RGB(130, 155, 200));
    RECT hr = { cr.right / 2, REC_HEADER_Y,
                cr.right - MARGIN, REC_HEADER_Y + 22 };
    DrawTextW(hdc, L"More  \u203a", -1, &hr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Two-column layout, all 7 visible items
    static const COLORREF recColors[7] = {
        RGB(  0, 120, 215), RGB(  0, 150, 130), RGB( 70, 130, 180),
        RGB(200, 100,  20), RGB(100, 180,  50), RGB(180,  50, 100),
        RGB(150, 100, 200)
    };

    int colW   = (cr.right - 2 * MARGIN) / 2;
    int visIdx = 0;

    for (int i = 0; i < 7 && visIdx < 7; ++i) {
        if (!m_menuItems[i].visible) continue;

        int col   = visIdx % 2;
        int row   = visIdx / 2;
        int itemX = MARGIN + col * colW;
        int itemY = REC_START_Y + row * REC_ITEM_H;

        // Hover highlight
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

        // Small icon square
        int iconCX = itemX + 20;
        int iconCY = itemY + REC_ITEM_H / 2;
        DrawIconSquare(hdc, iconCX, iconCY, 24, recColors[i], L"");

        // Label
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

    // Divider above bottom bar
    DrawSeparator(hdc, BOTTOM_BAR_Y - 1, MARGIN, cr.right - MARGIN);
}

// ── PaintBottomBar ────────────────────────────────────────────────────────────
void StartMenuWindow::PaintBottomBar(HDC hdc, const RECT& cr) {
    SetBkMode(hdc, TRANSPARENT);

    // Slightly different background
    HBRUSH bbBr = CreateSolidBrush(CalculateSubtleColor());
    RECT   bbR  = { 0, BOTTOM_BAR_Y, cr.right, cr.bottom };
    FillRect(hdc, &bbR, bbBr);
    DeleteObject(bbBr);

    int barCY = BOTTOM_BAR_Y + (cr.bottom - BOTTOM_BAR_Y) / 2;

    // ── User area hover highlight ──
    if (m_hoveredUser) {
        HBRUSH hBr  = CreateSolidBrush(CalculateHoverColor());
        HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
        HPEN   op   = (HPEN)SelectObject(hdc, noPn);
        RoundRect(hdc, MARGIN - 4, BOTTOM_BAR_Y + 4,
                  WIDTH / 2, cr.bottom - 4, 8, 8);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(hBr);
    }

    // ── Avatar circle ──
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
    RECT avTR = { avCX - avR, barCY - avR, avCX + avR, barCY + avR };
    DrawTextW(hdc, L"U", -1, &avTR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ── "User" label ──
    HFONT nmF = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(hdc, nmF);
    ::SetTextColor(hdc, m_textColor);
    RECT nmR = { avCX + avR + 8, BOTTOM_BAR_Y, cr.right / 2, cr.bottom };
    DrawTextW(hdc, L"User", -1, &nmR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // ── Power button ──
    int pwrCX = cr.right - MARGIN - POWER_BTN_R;
    int pwrCY = barCY;

    // Hover background ring
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

    // Power symbol: open arc at top + vertical tick
    COLORREF pwrCol = m_hoveredPower ? RGB(255, 255, 255) : RGB(175, 175, 185);
    HPEN pwrPen  = CreatePen(PS_SOLID, 2, pwrCol);
    HPEN oldPen2 = (HPEN)SelectObject(hdc, pwrPen);
    HBRUSH nb2   = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    int ar = POWER_BTN_R - 4;
    // Arc from ~315° to ~225° going clockwise (gap at top)
    Arc(hdc,
        pwrCX - ar, pwrCY - ar, pwrCX + ar, pwrCY + ar,
        pwrCX - ar + 3, pwrCY - ar,    // start point (upper-left)
        pwrCX + ar - 3, pwrCY - ar);   // end point   (upper-right)
    // Vertical line (power tick)
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

    // Background
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

    PaintPinnedSection(hdc, cr);
    PaintRecommendedSection(hdc, cr);
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
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    int colW   = (cr.right - 2 * MARGIN) / 2;
    int visIdx = 0;
    for (int i = 0; i < 7 && visIdx < 7; ++i) {
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

bool StartMenuWindow::IsOverUserArea(POINT pt) {
    // Avatar circle center: (MARGIN + 18, barCY), r=16
    // "User" label extends to WIDTH/2
    // Hit zone: left half of bottom bar
    if (pt.y < BOTTOM_BAR_Y) return false;
    return pt.x >= MARGIN && pt.x <= WIDTH / 2;
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

// ── Power menu ────────────────────────────────────────────────────────────────
void StartMenuWindow::ShowPowerMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, 1, L"Sleep");
    AppendMenuW(menu, MF_STRING, 2, L"Shut down");
    AppendMenuW(menu, MF_STRING, 3, L"Restart");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Cancel");

    // Show menu near the power button (bottom-right corner of the Start Menu)
    RECT wr = {};
    GetWindowRect(m_hwnd, &wr);
    int x = wr.right  - MARGIN - POWER_BTN_R * 2;
    int y = wr.bottom - 62;

    // Menu must be shown in the foreground window context
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                             x, y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == 4 || cmd == 0) return;  // Cancel or dismissed

    Hide();

    switch (cmd) {
        case 1:  // Sleep
            SetSuspendState(FALSE, FALSE, FALSE);
            break;
        case 2:  // Shut down
            ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER);
            break;
        case 3:  // Restart
            ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER);
            break;
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
        int np = GetPinnedItemAtPoint(pt);
        int nr = GetRecommendedItemAtPoint(pt);
        bool npwr  = IsOverPowerButton(pt);
        bool nusr  = IsOverUserArea(pt);
        if (np != m_hoveredPinnedIndex ||
            nr != m_hoveredRecommendedIndex ||
            npwr != m_hoveredPower ||
            nusr != m_hoveredUser) {
            m_hoveredPinnedIndex      = np;
            m_hoveredRecommendedIndex = nr;
            m_hoveredPower            = npwr;
            m_hoveredUser             = nusr;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse           = false;
        m_hoveredPinnedIndex      = -1;
        m_hoveredRecommendedIndex = -1;
        m_hoveredPower            = false;
        m_hoveredUser             = false;
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int p = GetPinnedItemAtPoint(pt);
        if (p >= 0) { ExecutePinnedItem(p); return 0; }
        int r = GetRecommendedItemAtPoint(pt);
        if (r >= 0) { ExecuteRecommendedItem(r); return 0; }
        if (IsOverPowerButton(pt)) { ShowPowerMenu(); return 0; }
        if (IsOverUserArea(pt)) {
            Hide();
            ShellExecuteW(NULL, L"open", L"ms-settings:accounts", NULL, NULL, SW_SHOW);
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
        // "menu_X": "Name"
        size_t pos = line.find(L"\"menu_");
        if (pos != std::wstring::npos) {
            int idx   = line[pos + 6] - L'0';
            size_t c  = line.find(L':', pos);
            size_t q1 = line.find(L'\"', c);
            size_t q2 = line.find(L'\"', q1 + 1);
            if (idx >= 0 && idx < 7 && q1 != std::wstring::npos)
                m_customMenuNames[idx] = line.substr(q1 + 1, q2 - q1 - 1);
        }
        // "title": "Name"
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

    f << L"{\n";
    for (int i = 0; i < 7; ++i)
        if (!m_customMenuNames[i].empty())
            f << L"  \"menu_" << i << L"\": \"" << m_customMenuNames[i] << L"\",\n";
    if (!m_customTitle.empty())
        f << L"  \"title\": \"" << m_customTitle << L"\"\n";
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

        // Pre-fill current name
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
        // Do NOT call PostQuitMessage here — this is a child dialog, not the
        // main window.  PostQuitMessage would inject WM_QUIT into the shared
        // message queue and cause the host application to exit unexpectedly
        // after the dialog closes.  The ShowEditDialog() loop already
        // terminates when IsWindow(dlg) becomes FALSE, so no quit signal
        // is needed.
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace CrystalFrame
