#include "StartMenuWindow.h"
#include "Diagnostics.h"
#include "Renderer.h" // For ACCENT_POLICY / WINDOWCOMPOSITIONATTRIBDATA
#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <powrprof.h>
#include <cwctype>   // towupper

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "advapi32.lib")

namespace CrystalFrame {

// ── File-scope helpers ────────────────────────────────────────────────────────

/// S6.2: Search the All-Programs tree (case-insensitive) for a shortcut node
/// whose display name matches |name|. Returns its lnkPath, or empty string.
/// Used to find a .lnk file for pinned UWP apps (ms-settings:, calc.exe, etc.)
/// whose command string is not a filesystem path that SHGetFileInfoW can resolve.
static std::wstring FindLnkPathByName(const std::vector<MenuNode>& nodes,
                                       const std::wstring& name) {
    for (const auto& node : nodes) {
        if (!node.isFolder) {
            if (_wcsicmp(node.name.c_str(), name.c_str()) == 0 && !node.lnkPath.empty())
                return node.lnkPath;
        } else {
            auto found = FindLnkPathByName(node.children, name);
            if (!found.empty()) return found;
        }
    }
    return {};
}

/// Enables SE_SHUTDOWN_NAME in the current process token so that
/// ExitWindowsEx succeeds without UAC elevation on standard user accounts.
/// Returns true if the privilege was successfully enabled.
static bool EnableShutdownPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tkp = {};
    tkp.PrivilegeCount   = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME,
                                    &tkp.Privileges[0].Luid) != FALSE;
    if (ok) {
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, nullptr);
        ok = (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
    }
    CloseHandle(hToken);
    return ok;
}

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

    // S6 — load real system icons.
    // SHGetFileInfoW / SHGetStockIconInfo do not require explicit COM init;
    // the WinUI 3 host already initialises COM on the main thread.

    // S6.1 — Pinned app icons (32×32, scaled to PROG_ICON_SZ at paint time).
    // First pass: direct SHGetFileInfoW on the command string (works for plain .exe names).
    for (int i = 0; i < PROG_COUNT; ++i) {
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(s_pinnedItems[i].command, 0, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
            m_pinnedIcons[i] = sfi.hIcon;
    }

    // S6.2 — UWP fallback: for pinned apps whose command is a URI (ms-settings:)
    // or a stub name that the shell cannot resolve directly, search the All Programs
    // tree for a shortcut with the same display name and use its .lnk path instead.
    // This covers Settings, Calculator, Edge, and any other UWP app in the list.
    for (int i = 0; i < PROG_COUNT; ++i) {
        if (m_pinnedIcons[i]) continue;  // already resolved in S6.1
        std::wstring lnkPath = FindLnkPathByName(m_programTree, s_pinnedItems[i].name);
        if (lnkPath.empty()) continue;
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(lnkPath.c_str(), 0, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
            m_pinnedIcons[i] = sfi.hIcon;
    }

    // S6.5 — All Programs tree icons (loaded recursively into MenuNode::hIcon).
    LoadNodeIcons(m_programTree);

    // S6.4 — Right-column icons (16×16).
    for (int i = 0; i < RIGHT_ITEM_COUNT; ++i) {
        const Win7RightItem& ri = s_rightItems[i];
        if (ri.isSeparator) continue;

        std::wstring iconPath;
        if (ri.folderId) {
            // Resolve KNOWNFOLDERID to a filesystem path for icon extraction.
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(*ri.folderId, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
                iconPath = p;
                CoTaskMemFree(p);
            }
        } else if (ri.target) {
            iconPath = ri.target;  // exe name, shell: URI, or ms-settings: URI
        }

        if (!iconPath.empty()) {
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(iconPath.c_str(), 0, &sfi, sizeof(sfi),
                               SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon)
                m_rightIcons[i] = sfi.hIcon;
        }
    }

    // S7 — recently used programs from UserAssist (loaded after program tree
    // so pinned-list deduplication can use m_programTree if needed in future).
    LoadRecentPrograms();

    CF_LOG(Info, "StartMenuWindow initialized successfully");
    return true;
}

void StartMenuWindow::Shutdown() {
    // S6 — release icon handles before window destruction.
    for (int i = 0; i < PROG_COUNT; ++i) {
        if (m_pinnedIcons[i]) { DestroyIcon(m_pinnedIcons[i]); m_pinnedIcons[i] = nullptr; }
    }
    for (int i = 0; i < RIGHT_ITEM_COUNT; ++i) {
        if (m_rightIcons[i]) { DestroyIcon(m_rightIcons[i]); m_rightIcons[i] = nullptr; }
    }
    FreeNodeIcons(m_programTree);

    // S7 — release recently used program icons
    for (auto& ri : m_recentItems) {
        if (ri.hIcon) { DestroyIcon(ri.hIcon); ri.hIcon = nullptr; }
    }
    m_recentItems.clear();

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
        m_hoveredShutdown   = false;
        m_hoveredArrow      = false;
        m_keySelProgIndex   = -1;
        m_keySelApRow       = false;
        m_keySelApIndex     = -1;
        m_apScrollOffset    = 0;
        if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; }
        m_hoverCandidate    = -1;
        m_subMenuOpen       = false;
        m_subMenuNodeIdx    = -1;
        m_subMenuHoveredIdx = -1;
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

// ── S6 icon helpers ───────────────────────────────────────────────────────────
// Walk the All-Programs tree and load a shell icon for every node.
// Folders get the standard SIID_FOLDER stock icon; shortcuts get the icon
// embedded in (or pointed to by) their .lnk / .url file.
// Called once from Initialize(), after BuildAllProgramsTree() has returned
// the final, sorted, merged tree — so MergeTree/std::sort never see live HICONs.
void StartMenuWindow::LoadNodeIcons(std::vector<MenuNode>& nodes) {
    for (auto& node : nodes) {
        if (node.isFolder) {
            SHSTOCKICONINFO sii = {};
            sii.cbSize = sizeof(sii);
            if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER,
                                             SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon)
                node.hIcon = sii.hIcon;
            LoadNodeIcons(node.children);   // recurse
        } else if (!node.lnkPath.empty()) {
            // SHGetFileInfoW on the .lnk file returns the target app's icon.
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(node.lnkPath.c_str(), 0, &sfi, sizeof(sfi),
                               SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
                node.hIcon = sfi.hIcon;
        }
    }
}

// Walk the tree and release every HICON handle, then clear it to nullptr.
void StartMenuWindow::FreeNodeIcons(std::vector<MenuNode>& nodes) {
    for (auto& node : nodes) {
        if (node.hIcon) { DestroyIcon(node.hIcon); node.hIcon = nullptr; }
        if (node.isFolder) FreeNodeIcons(node.children);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// S7 — LoadRecentPrograms
// Reads Windows UserAssist registry to find the most recently used programs.
// UserAssist stores ROT13-encoded paths with run count + FILETIME last run.
// We load both the Applications GUID and the Shortcut Links GUID, decode,
// filter to .exe/.lnk, sort by last-run time, deduplicate against pinned list,
// and keep the top RECENT_COUNT entries.
//
// Data format (Windows Vista+ — 24+ bytes per value):
//   offset 0  : DWORD — unknown / session count
//   offset 4  : DWORD — run count (0 = never counted)
//   offset 8  : DWORD — focus count
//   offset 12 : DWORD — focus time (ms)
//   offset 16 : FILETIME (8 bytes) — last execution time
// ─────────────────────────────────────────────────────────────────────────────
void StartMenuWindow::LoadRecentPrograms() {
    m_recentItems.clear();

    // ROT13 decode: shift alphabetic chars by 13 positions.
    auto rot13 = [](std::wstring s) -> std::wstring {
        for (wchar_t& c : s) {
            if      (c >= L'a' && c <= L'z') c = L'a' + (c - L'a' + 13) % 26;
            else if (c >= L'A' && c <= L'Z') c = L'A' + (c - L'A' + 13) % 26;
        }
        return s;
    };

    struct UAEntry {
        std::wstring path;
        DWORD        count;
        FILETIME     lastRun;
    };
    std::vector<UAEntry> entries;

    // Two UserAssist GUIDs: executables + shortcut links
    static const wchar_t* kGuids[] = {
        L"{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}",  // Applications (.exe)
        L"{F4E57C4B-2036-45F0-A9AB-443BCFE33D9F}",  // Shortcut links (.lnk)
    };

    for (const auto* guid : kGuids) {
        std::wstring keyPath =
            std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                         L"Explorer\\UserAssist\\") + guid + L"\\Count";

        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0,
                          KEY_READ, &hKey) != ERROR_SUCCESS)
            continue;

        DWORD valueCount = 0, maxNameLen = 0, maxDataLen = 0;
        RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, &valueCount, &maxNameLen, &maxDataLen,
                         nullptr, nullptr);
        ++maxNameLen; // include null terminator

        std::vector<wchar_t> nameBuf(maxNameLen + 1);
        std::vector<BYTE>    dataBuf(max(maxDataLen, (DWORD)72));

        for (DWORD idx = 0; idx < valueCount; ++idx) {
            DWORD nameLen = maxNameLen;
            DWORD dataLen = static_cast<DWORD>(dataBuf.size());
            DWORD type    = 0;

            if (RegEnumValueW(hKey, idx, nameBuf.data(), &nameLen, nullptr,
                              &type, dataBuf.data(), &dataLen) != ERROR_SUCCESS)
                continue;
            if (type != REG_BINARY || dataLen < 20) continue;

            // Decode ROT13 — UserAssist encodes value names to deter peeking
            std::wstring decoded = rot13(std::wstring(nameBuf.data(), nameLen));

            // Truncate at embedded NUL (some entries append window title after \0)
            auto nul = decoded.find(L'\0');
            if (nul != std::wstring::npos) decoded.resize(nul);
            if (decoded.empty()) continue;

            // Skip UserAssist metadata entries (UEME_*)
            if (decoded.size() >= 5 && decoded.substr(0, 5) == L"UEME_") continue;

            // Extract run count (offset 4) and last-run FILETIME (offset 16)
            DWORD    count   = 0;
            FILETIME lastRun = {};
            memcpy(&count,   dataBuf.data() + 4,  sizeof(DWORD));
            memcpy(&lastRun, dataBuf.data() + 16, sizeof(FILETIME));
            if (count == 0) continue;

            // Accept only .exe and .lnk paths
            auto lower = decoded;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            bool isExe = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0;
            bool isLnk = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".lnk") == 0;
            if (!isExe && !isLnk) continue;

            // Expand environment variables (%windir%, %appdata%, etc.)
            wchar_t expanded[MAX_PATH] = {};
            DWORD expLen = ExpandEnvironmentStringsW(decoded.c_str(), expanded,
                                                     static_cast<DWORD>(std::size(expanded)));
            std::wstring path = (expLen > 1 && expLen <= MAX_PATH) ? expanded : decoded;

            entries.push_back({path, count, lastRun});
        }

        RegCloseKey(hKey);
    }

    // Sort descending by last-run FILETIME (most recent first)
    std::sort(entries.begin(), entries.end(), [](const UAEntry& a, const UAEntry& b) {
        if (a.lastRun.dwHighDateTime != b.lastRun.dwHighDateTime)
            return a.lastRun.dwHighDateTime > b.lastRun.dwHighDateTime;
        return a.lastRun.dwLowDateTime > b.lastRun.dwLowDateTime;
    });

    // Collect up to RECENT_COUNT unique entries not already in the pinned list
    for (const auto& e : entries) {
        if (static_cast<int>(m_recentItems.size()) >= RECENT_COUNT) break;

        // Get display name via shell (handles .lnk resolution + UWP app names)
        SHFILEINFOW sfi = {};
        if (!SHGetFileInfoW(e.path.c_str(), 0, &sfi, sizeof(sfi),
                            SHGFI_DISPLAYNAME))
            continue;                      // path doesn't exist / not accessible
        std::wstring displayName = sfi.szDisplayName;
        if (displayName.empty()) continue;

        // Remove trailing ".lnk" / ".exe" that some shells leave in display name
        auto lowerName = displayName;
        for (auto& c : lowerName) c = static_cast<wchar_t>(towlower(c));
        if (lowerName.size() > 4) {
            auto ext = lowerName.substr(lowerName.size() - 4);
            if (ext == L".lnk" || ext == L".exe")
                displayName.resize(displayName.size() - 4);
        }

        // Skip if already in pinned list (case-insensitive name match)
        bool alreadyPinned = false;
        for (int pi = 0; pi < PROG_COUNT; ++pi) {
            auto pn = std::wstring(s_pinnedItems[pi].name);
            auto dn = displayName;
            for (auto& c : pn) c = static_cast<wchar_t>(towlower(c));
            for (auto& c : dn) c = static_cast<wchar_t>(towlower(c));
            if (pn == dn) { alreadyPinned = true; break; }
        }
        if (alreadyPinned) continue;

        // Skip duplicate display names already added to recent list
        bool dupName = false;
        for (const auto& ri : m_recentItems) {
            auto a = ri.name, b = displayName;
            for (auto& c : a) c = static_cast<wchar_t>(towlower(c));
            for (auto& c : b) c = static_cast<wchar_t>(towlower(c));
            if (a == b) { dupName = true; break; }
        }
        if (dupName) continue;

        // Load icon (reuse the SHGetFileInfoW call with SHGFI_ICON this time)
        HICON hIcon = nullptr;
        SHFILEINFOW sfiIcon = {};
        if (SHGetFileInfoW(e.path.c_str(), 0, &sfiIcon, sizeof(sfiIcon),
                           SHGFI_ICON | SHGFI_LARGEICON) && sfiIcon.hIcon)
            hIcon = sfiIcon.hIcon;

        m_recentItems.push_back({e.path, displayName, hIcon, e.lastRun, e.count});
    }

    CF_LOG(Info, "LoadRecentPrograms: " << m_recentItems.size() << " entries loaded");
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

        // Icon — real system icon when available, colored square fallback
        int iconCX = MARGIN + PROG_ICON_SZ / 2 + 4;
        int iconCY = itemY + PROG_ITEM_H / 2;
        if (m_pinnedIcons[i]) {
            DrawIconEx(hdc, iconCX - PROG_ICON_SZ / 2, iconCY - PROG_ICON_SZ / 2,
                       m_pinnedIcons[i], PROG_ICON_SZ, PROG_ICON_SZ, 0, nullptr, DI_NORMAL);
        } else {
            DrawIconSquare(hdc, iconCX, iconCY, PROG_ICON_SZ,
                           s_pinnedItems[i].iconColor, s_pinnedItems[i].shortName);
        }

        // App name
        ::SetTextColor(hdc, m_textColor);
        RECT nr = { MARGIN + PROG_ICON_SZ + 12, itemY,
                    DIVIDER_X - MARGIN,          itemY + PROG_ITEM_H };
        DrawTextW(hdc, s_pinnedItems[i].name, -1, &nr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // ── S7: Recently used programs — below pinned items ──────────────────────
    int recentCount  = static_cast<int>(m_recentItems.size());
    int recentStartY = PROG_Y + PROG_COUNT * PROG_ITEM_H + 12; // 12px gap (4 sep + 8 pad)

    for (int i = 0; i < recentCount; ++i) {
        int itemIdx = PROG_COUNT + i;
        int itemY   = recentStartY + i * PROG_ITEM_H;

        // Hover / keyboard-selection highlight
        bool isKeySel = (itemIdx == m_keySelProgIndex);
        bool isHover  = (itemIdx == m_hoveredProgIndex) && !isKeySel;
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

        // Icon
        int iconCX = MARGIN + PROG_ICON_SZ / 2 + 4;
        int iconCY = itemY + PROG_ITEM_H / 2;
        if (m_recentItems[i].hIcon) {
            DrawIconEx(hdc, iconCX - PROG_ICON_SZ / 2, iconCY - PROG_ICON_SZ / 2,
                       m_recentItems[i].hIcon, PROG_ICON_SZ, PROG_ICON_SZ,
                       0, nullptr, DI_NORMAL);
        } else {
            // Fallback: gray square with first 3 chars of name
            std::wstring fb = m_recentItems[i].name.size() > 3
                              ? m_recentItems[i].name.substr(0, 3)
                              : m_recentItems[i].name;
            DrawIconSquare(hdc, iconCX, iconCY, PROG_ICON_SZ,
                           RGB(90, 90, 90), fb.c_str());
        }

        // Name (slightly dimmed to distinguish from pinned — Win7 uses same color,
        // but we keep it consistent with m_textColor)
        ::SetTextColor(hdc, m_textColor);
        RECT nr = { MARGIN + PROG_ICON_SZ + 12, itemY,
                    DIVIDER_X - MARGIN,          itemY + PROG_ITEM_H };
        SelectObject(hdc, nameFont);
        DrawTextW(hdc, m_recentItems[i].name.c_str(), -1, &nr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldF);
    DeleteObject(nameFont);

    // Thin separator below pinned list (above recent items)
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

        if (node.hIcon) {
            // Real system icon from shell
            DrawIconEx(hdc, iconCX - PROG_ICON_SZ / 2, iconCY - PROG_ICON_SZ / 2,
                       node.hIcon, PROG_ICON_SZ, PROG_ICON_SZ, 0, nullptr, DI_NORMAL);
            SelectObject(hdc, node.isFolder ? boldFont : nameFont);
        } else if (node.isFolder) {
            // Folder fallback: amber square with "›" glyph
            DrawIconSquare(hdc, iconCX, iconCY, PROG_ICON_SZ,
                           RGB(210, 150, 20), L"\u203a");
            SelectObject(hdc, boldFont);
        } else {
            // Shortcut fallback: teal square with "»" glyph
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

            // Item icon (16×16) — drawn at left edge of item row
            if (m_rightIcons[i]) {
                int iconX = RC_X + 4;
                int iconY = y + (RC_ITEM_H - 16) / 2;
                DrawIconEx(hdc, iconX, iconY, m_rightIcons[i], 16, 16, 0, nullptr, DI_NORMAL);
            }

            // Item label — always indented by 24 px to leave room for the icon slot
            ::SetTextColor(hdc, m_textColor);
            RECT tr = { RC_X + 24, y, cr.right - 8, y + RC_ITEM_H };
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

    // ── Win7 "Shut down" button + arrow (right side) ────────────────────────
    // Layout (right-aligned): [MARGIN][Shut down SHUT_BTN_W][1px gap][arrow SHUT_ARROW_W][MARGIN]
    int btnBot = BOTTOM_BAR_Y + (BOTTOM_BAR_H + SHUT_BTN_H) / 2;
    int btnTop = btnBot - SHUT_BTN_H;
    int btnR   = cr.right - MARGIN;
    int arrL   = btnR - SHUT_ARROW_W;          // arrow left edge
    int arrR   = btnR;                          // arrow right edge
    int sdR    = arrL - 1;                      // shut-down button right edge
    int sdL    = sdR  - SHUT_BTN_W;            // shut-down button left edge

    // Helper lambda for button fill colour
    auto btnFill = [&](bool hov) -> COLORREF {
        return hov ? RGB(185, 210, 240) : RGB(160, 190, 225);
    };

    // Draw "Shut down" text button
    {
        HBRUSH br = CreateSolidBrush(btnFill(m_hoveredShutdown));
        RECT   rc = { sdL, btnTop, sdR, btnBot };
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        // Border
        HPEN  pe = CreatePen(PS_SOLID, 1, RGB(100, 140, 190));
        HPEN  op = (HPEN)SelectObject(hdc, pe);
        HBRUSH nb3 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, sdL, btnTop, sdR, btnBot);
        SelectObject(hdc, op);
        SelectObject(hdc, nb3);
        DeleteObject(pe);
        // Text
        HFONT  sdF = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT  oldSdF = (HFONT)SelectObject(hdc, sdF);
        ::SetTextColor(hdc, RGB(10, 10, 10));
        RECT  tr = { sdL, btnTop, sdR, btnBot };
        DrawTextW(hdc, L"Shut down", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldSdF);
        DeleteObject(sdF);
    }

    // Draw arrow dropdown button
    {
        HBRUSH br = CreateSolidBrush(btnFill(m_hoveredArrow));
        RECT   rc = { arrL, btnTop, arrR, btnBot };
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        // Border (share left border with shut-down button)
        HPEN  pe = CreatePen(PS_SOLID, 1, RGB(100, 140, 190));
        HPEN  op = (HPEN)SelectObject(hdc, pe);
        HBRUSH nb4 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, arrL, btnTop, arrR, btnBot);
        SelectObject(hdc, op);
        SelectObject(hdc, nb4);
        DeleteObject(pe);
        // Arrow glyph (▼) centred
        HFONT  arF = CreateFontW(10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Marlett");
        HFONT  oldArF = (HFONT)SelectObject(hdc, arF);
        ::SetTextColor(hdc, RGB(10, 10, 10));
        RECT  tr = { arrL, btnTop, arrR, btnBot };
        DrawTextW(hdc, L"6", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldArF);
        DeleteObject(arF);
    }

    SelectObject(hdc, oldF);
    DeleteObject(initF);
    DeleteObject(nmF);
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
    // PaintWin7SearchBox intentionally omitted — search box removed from UI.

    // Right column: draw normal links, or overlay with submenu when open
    PaintWin7RightColumn(hdc, cr);
    if (m_subMenuOpen)
        PaintSubMenu(hdc, cr);
    PaintBottomBar(hdc, cr);

    EndPaint(m_hwnd, &ps);
}

// ── Hit testing ───────────────────────────────────────────────────────────────

// Returns the pinned item index at pt (Programs view), or -1.
int StartMenuWindow::GetProgItemAtPoint(POINT pt) {
    if (pt.x < MARGIN || pt.x >= DIVIDER_X - MARGIN) return -1;

    // Pinned zone: indices 0..PROG_COUNT-1
    if (pt.y >= PROG_Y && pt.y < PROG_Y + PROG_COUNT * PROG_ITEM_H) {
        int idx = (pt.y - PROG_Y) / PROG_ITEM_H;
        if (idx >= 0 && idx < PROG_COUNT) return idx;
    }

    // S7 — Recent zone: indices PROG_COUNT..PROG_COUNT+recentCount-1
    int recentCount  = static_cast<int>(m_recentItems.size());
    int recentStartY = PROG_Y + PROG_COUNT * PROG_ITEM_H + 12;
    if (recentCount > 0 && pt.y >= recentStartY &&
        pt.y < recentStartY + recentCount * PROG_ITEM_H) {
        int idx = (pt.y - recentStartY) / PROG_ITEM_H;
        if (idx >= 0 && idx < recentCount) return PROG_COUNT + idx;
    }

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

bool StartMenuWindow::IsOverShutdownButton(POINT pt) {
    RECT cr; GetClientRect(m_hwnd, &cr);
    int btnBot = BOTTOM_BAR_Y + (BOTTOM_BAR_H + SHUT_BTN_H) / 2;
    int btnTop = btnBot - SHUT_BTN_H;
    int btnR   = cr.right - MARGIN;
    int arrL   = btnR - SHUT_ARROW_W;
    int sdR    = arrL - 1;
    int sdL    = sdR  - SHUT_BTN_W;
    RECT r = { sdL, btnTop, sdR, btnBot };
    return PtInRect(&r, pt) != FALSE;
}

bool StartMenuWindow::IsOverArrowButton(POINT pt) {
    RECT cr; GetClientRect(m_hwnd, &cr);
    int btnBot = BOTTOM_BAR_Y + (BOTTOM_BAR_H + SHUT_BTN_H) / 2;
    int btnTop = btnBot - SHUT_BTN_H;
    int btnR   = cr.right - MARGIN;
    int arrL   = btnR - SHUT_ARROW_W;
    RECT r = { arrL, btnTop, btnR, btnBot };
    return PtInRect(&r, pt) != FALSE;
}

// GetRightItemAtPoint ─────────────────────────────────────────────────────────
// Returns the index into s_rightItems that the cursor is over, or -1.
// Separators and the username header are never returned (not clickable).
int StartMenuWindow::GetRightItemAtPoint(POINT pt) {
    if (m_subMenuOpen) return -1;          // submenu covers the right column
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
    if (index < 0) return;

    if (index < PROG_COUNT) {
        // Pinned item
        CF_LOG(Info, "ExecutePinnedItem: " << index);
        ShellExecuteW(NULL, L"open", s_pinnedItems[index].command, NULL, NULL, SW_SHOW);
    } else {
        // S7 — Recently used item (index == PROG_COUNT + recentIdx)
        int ri = index - PROG_COUNT;
        if (ri >= static_cast<int>(m_recentItems.size())) return;
        CF_LOG(Info, "ExecuteRecentItem: " << ri
               << " (" << m_recentItems[ri].name.c_str() << ")");
        ShellExecuteW(NULL, L"open", m_recentItems[ri].exePath.c_str(),
                      NULL, NULL, SW_SHOW);
    }
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
    if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; }
    m_hoverCandidate    = -1;
    m_subMenuOpen       = false;
    m_subMenuNodeIdx    = -1;
    m_subMenuHoveredIdx = -1;
    m_apNavStack.push_back(&children);
    m_hoveredApIndex    = -1;
    m_keySelApIndex     = -1;
    m_keySelApRow       = false;
    m_apScrollOffset    = 0;
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
    CF_LOG(Info, "AP drill-down: depth=" << m_apNavStack.size()
           << " nodes=" << children.size());
}

void StartMenuWindow::NavigateBack() {
    if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; }
    m_hoverCandidate    = -1;
    m_subMenuOpen       = false;
    m_subMenuNodeIdx    = -1;
    m_subMenuHoveredIdx = -1;

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

// ── Hover-to-open lateral submenu (S3.3) ─────────────────────────────────────
void StartMenuWindow::OpenSubMenu(int apNodeIdx) {
    const auto& nodes = CurrentApNodes();
    if (apNodeIdx < 0 || apNodeIdx >= static_cast<int>(nodes.size())) return;
    if (!nodes[static_cast<size_t>(apNodeIdx)].isFolder) return;

    if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; }
    m_hoverCandidate    = -1;
    m_subMenuOpen       = true;
    m_subMenuNodeIdx    = apNodeIdx;
    m_subMenuHoveredIdx = -1;
    CF_LOG(Info, "SubMenu opened for AP node " << apNodeIdx);
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
}

void StartMenuWindow::CloseSubMenu() {
    if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; }
    m_hoverCandidate    = -1;
    m_subMenuOpen       = false;
    m_subMenuNodeIdx    = -1;
    m_subMenuHoveredIdx = -1;
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
}

bool StartMenuWindow::IsOverSubMenu(POINT pt) const {
    if (!m_subMenuOpen) return false;
    return pt.x >= DIVIDER_X && pt.x < WIDTH && pt.y >= 0 && pt.y < BOTTOM_BAR_Y;
}

int StartMenuWindow::GetSubMenuItemAtPoint(POINT pt) {
    if (!m_subMenuOpen) return -1;
    if (pt.x < SM_X || pt.x >= WIDTH - 2) return -1;
    int relY = pt.y - SM_TITLE_H;
    if (relY < 0) return -1;
    int vis = relY / SM_ITEM_H;

    const auto& nodes   = CurrentApNodes();
    const auto& folder  = nodes[static_cast<size_t>(m_subMenuNodeIdx)];
    int count = min(SM_MAX_VIS, static_cast<int>(folder.children.size()));
    if (vis >= 0 && vis < count) return vis;
    return -1;
}

void StartMenuWindow::PaintSubMenu(HDC hdc, const RECT& cr) {
    if (!m_subMenuOpen) return;
    const auto& nodes  = CurrentApNodes();
    const auto& folder = nodes[static_cast<size_t>(m_subMenuNodeIdx)];
    int count = min(SM_MAX_VIS, static_cast<int>(folder.children.size()));

    // Background panel
    COLORREF panelColor = CalculateSubtleColor();
    HBRUSH   panelBr    = CreateSolidBrush(panelColor);
    RECT     panelR     = { DIVIDER_X, 0, cr.right, BOTTOM_BAR_Y };
    FillRect(hdc, &panelR, panelBr);
    DeleteObject(panelBr);

    // Left border of panel
    HPEN bdrPen = CreatePen(PS_SOLID, 1, CalculateBorderColor());
    HPEN oldPen = (HPEN)SelectObject(hdc, bdrPen);
    MoveToEx(hdc, DIVIDER_X, 0, NULL);
    LineTo(hdc, DIVIDER_X, BOTTOM_BAR_Y);
    SelectObject(hdc, oldPen);
    DeleteObject(bdrPen);

    SetBkMode(hdc, TRANSPARENT);

    // Title — folder name
    HFONT titleF = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, titleF);
    ::SetTextColor(hdc, m_textColor);
    RECT tr = { SM_X, 0, cr.right - 4, SM_TITLE_H };
    DrawTextW(hdc, folder.name.c_str(), -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawSeparator(hdc, SM_TITLE_H, SM_X, cr.right - 4);

    // Items
    HFONT itemF = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT boldF = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    for (int i = 0; i < count; ++i) {
        const MenuNode& child = folder.children[static_cast<size_t>(i)];
        int itemY = SM_TITLE_H + i * SM_ITEM_H;

        if (i == m_subMenuHoveredIdx) {
            HBRUSH hBr  = CreateSolidBrush(CalculateHoverColor());
            HPEN   noPn = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH ob   = (HBRUSH)SelectObject(hdc, hBr);
            HPEN   op   = (HPEN)SelectObject(hdc, noPn);
            RoundRect(hdc, SM_X, itemY + 2, cr.right - 4, itemY + SM_ITEM_H - 2, 6, 6);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(hBr);
        }

        // Small icon (20×20 slot)
        static constexpr int SM_ICON_SZ = 20;
        int iconCX = SM_X + 14;
        int iconCY = itemY + SM_ITEM_H / 2;
        if (child.hIcon) {
            DrawIconEx(hdc, iconCX - SM_ICON_SZ / 2, iconCY - SM_ICON_SZ / 2,
                       child.hIcon, SM_ICON_SZ, SM_ICON_SZ, 0, nullptr, DI_NORMAL);
            SelectObject(hdc, child.isFolder ? boldF : itemF);
        } else if (child.isFolder) {
            DrawIconSquare(hdc, iconCX, iconCY, SM_ICON_SZ, RGB(210, 150, 20), L"\u203a");
            SelectObject(hdc, boldF);
        } else {
            DrawIconSquare(hdc, iconCX, iconCY, SM_ICON_SZ, RGB(30, 140, 130), L"\u00bb");
            SelectObject(hdc, itemF);
        }

        ::SetTextColor(hdc, m_textColor);
        RECT nr = { SM_X + 32, itemY, cr.right - 4, itemY + SM_ITEM_H };
        DrawTextW(hdc, child.name.c_str(), -1, &nr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (count == 0) {
        SelectObject(hdc, itemF);
        ::SetTextColor(hdc, CalculateBorderColor());
        RECT er = { SM_X, SM_TITLE_H + 8, cr.right - 4, SM_TITLE_H + SM_ITEM_H };
        DrawTextW(hdc, L"(empty)", -1, &er, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    if (static_cast<int>(folder.children.size()) > SM_MAX_VIS) {
        int lastY = SM_TITLE_H + count * SM_ITEM_H;
        SelectObject(hdc, itemF);
        ::SetTextColor(hdc, CalculateBorderColor());
        RECT mr = { SM_X, lastY, cr.right - 4, lastY + SM_ITEM_H };
        DrawTextW(hdc, L"\u25bc  more\u2026", -1, &mr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldF);
    DeleteObject(titleF);
    DeleteObject(itemF);
    DeleteObject(boldF);
}

void StartMenuWindow::ExecuteSubMenuItem(int visualIdx) {
    const auto& nodes  = CurrentApNodes();
    const auto& folder = nodes[static_cast<size_t>(m_subMenuNodeIdx)];
    if (visualIdx < 0 || visualIdx >= static_cast<int>(folder.children.size())) return;

    const MenuNode& child = folder.children[static_cast<size_t>(visualIdx)];
    if (child.isFolder) {
        // Drill into sub-folder: close submenu, navigate main AP list into folder
        CloseSubMenu();
        NavigateIntoFolder(child.children);
    } else {
        CF_LOG(Info, "SubMenu launch: " << child.target.size() << " char target");
        CloseSubMenu();
        Hide();
        if (!child.target.empty()) {
            HINSTANCE hi = ShellExecuteW(NULL, L"open",
                child.target.c_str(),
                child.args.empty() ? nullptr : child.args.c_str(),
                nullptr, SW_SHOW);
            if (reinterpret_cast<INT_PTR>(hi) <= 32)
                CF_LOG(Warning, "SubMenu ShellExecuteW returned " << reinterpret_cast<INT_PTR>(hi));
        }
    }
}

// ── Power menu ────────────────────────────────────────────────────────────────
void StartMenuWindow::ShowPowerMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    // Win7-style dropdown: power actions ordered as in original Start Menu
    AppendMenuW(menu, MF_STRING, 1, L"Switch User");
    AppendMenuW(menu, MF_STRING, 2, L"Log Off");
    AppendMenuW(menu, MF_STRING, 3, L"Lock");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Restart");
    AppendMenuW(menu, MF_STRING, 5, L"Sleep");
    AppendMenuW(menu, MF_STRING, 6, L"Hibernate");
    AppendMenuW(menu, MF_STRING, 7, L"Shut down");

    // Anchor to the right edge of the arrow button, just above the bottom bar
    RECT wr = {};
    GetWindowRect(m_hwnd, &wr);
    int x = wr.right - MARGIN;
    int y = wr.bottom - BOTTOM_BAR_H;

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                             x, y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == 0) return;
    Hide();

    // Actions that call ExitWindowsEx require SE_SHUTDOWN_NAME privilege;
    // enable it once here before the switch (no-op if already held).
    switch (cmd) {
        case 2: // Log Off
        case 4: // Restart
        case 7: // Shut down
            EnableShutdownPrivilege();
            break;
        default:
            break;
    }

    switch (cmd) {
        case 1: LockWorkStation(); break;   // Switch User — shows lock screen (Win11 approach)
        case 2: ExitWindowsEx(EWX_LOGOFF   | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION); break;
        case 3: LockWorkStation(); break;
        case 4: ExitWindowsEx(EWX_REBOOT   | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION); break;
        case 5: SetSuspendState(FALSE, FALSE, FALSE); break;   // Sleep
        case 6: SetSuspendState(TRUE,  FALSE, FALSE); break;   // Hibernate
        case 7: ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF | EWX_FORCEIFHUNG,
                              SHTDN_REASON_MAJOR_APPLICATION); break;
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
        bool nshut  = IsOverShutdownButton(pt);
        bool narrow = IsOverArrowButton(pt);

        // ── Hover-timer management (S3.3) ────────────────────────────────────
        if (m_viewMode == LeftViewMode::AllPrograms) {
            bool overFolder = false;
            if (nAp >= 0) {
                const auto& nodes = CurrentApNodes();
                overFolder = nodes[static_cast<size_t>(nAp)].isFolder;
            }
            if (overFolder) {
                if (m_subMenuOpen && m_subMenuNodeIdx == nAp) {
                    // Same folder as open submenu — just update submenu hover
                    int smHov = GetSubMenuItemAtPoint(pt);
                    if (smHov != m_subMenuHoveredIdx) {
                        m_subMenuHoveredIdx = smHov;
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                } else if (nAp != m_hoverCandidate) {
                    // Different folder — start a new switch timer but do NOT close
                    // the current submenu immediately. The submenu switches only when
                    // the timer fires (mouse settled on new folder for HOVER_DELAY_MS).
                    // This lets the user move diagonally from a folder to its submenu
                    // without the submenu vanishing as the cursor briefly crosses an
                    // adjacent row.
                    if (m_hoverTimer) KillTimer(m_hwnd, HOVER_TIMER_ID);
                    m_hoverCandidate = nAp;
                    m_hoverTimer     = SetTimer(m_hwnd, HOVER_TIMER_ID, HOVER_DELAY_MS, NULL);
                }
            } else if (m_subMenuOpen && IsOverSubMenu(pt)) {
                // Mouse is in the submenu panel — cancel any pending switch timer so
                // a briefly-passed folder row doesn't hijack the open submenu.
                if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; m_hoverCandidate = -1; }
                // Update submenu item hover
                int smHov = GetSubMenuItemAtPoint(pt);
                if (smHov != m_subMenuHoveredIdx) {
                    m_subMenuHoveredIdx = smHov;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            } else {
                // Not over a folder and not in submenu.
                // Keep submenu alive while mouse crosses the gap between the left
                // panel's item edge (DIVIDER_X - MARGIN) and the submenu panel
                // (x >= DIVIDER_X). Without this the submenu closes the moment the
                // cursor leaves the folder row on its way to the child items.
                bool inTransitGap = m_subMenuOpen
                                    && (pt.x >= DIVIDER_X - MARGIN)
                                    && (pt.x <  DIVIDER_X);
                if (m_hoverTimer && nAp != m_hoverCandidate) {
                    KillTimer(m_hwnd, HOVER_TIMER_ID);
                    m_hoverTimer     = 0;
                    m_hoverCandidate = -1;
                }
                if (m_subMenuOpen && !IsOverSubMenu(pt) && !inTransitGap) {
                    CloseSubMenu();
                }
            }
        } else if (m_viewMode == LeftViewMode::Programs) {
            // Programs view — ensure any lingering submenu/timer is cleared
            if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; m_hoverCandidate = -1; }
            if (m_subMenuOpen) CloseSubMenu();
        }

        // Mouse movement clears keyboard selection (modes are mutually exclusive)
        bool hadKeySel = (m_keySelProgIndex >= 0 || m_keySelApRow || m_keySelApIndex >= 0);

        if (nProg  != m_hoveredProgIndex   ||
            nApRow != m_hoveredApRow      ||
            nAp    != m_hoveredApIndex    ||
            nrc    != m_hoveredRightIndex ||
            nshut  != m_hoveredShutdown   ||
            narrow != m_hoveredArrow      ||
            hadKeySel) {
            m_hoveredProgIndex  = nProg;
            m_hoveredApRow      = nApRow;
            m_hoveredApIndex    = nAp;
            m_hoveredRightIndex = nrc;
            m_hoveredShutdown   = nshut;
            m_hoveredArrow      = narrow;
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
        m_hoveredShutdown   = false;
        m_hoveredArrow      = false;
        if (m_hoverTimer) { KillTimer(m_hwnd, HOVER_TIMER_ID); m_hoverTimer = 0; m_hoverCandidate = -1; }
        if (m_subMenuOpen)  CloseSubMenu();
        else InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Submenu click (when open, intercepts the right column area)
        if (m_subMenuOpen && IsOverSubMenu(pt)) {
            int smIdx = GetSubMenuItemAtPoint(pt);
            if (smIdx >= 0) ExecuteSubMenuItem(smIdx);
            else CloseSubMenu();   // click in panel but not on an item → close
            return 0;
        }

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

        // Bottom bar — Shut down button (direct action) and arrow (dropdown)
        if (IsOverShutdownButton(pt)) {
            Hide();
            EnableShutdownPrivilege();
            ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF | EWX_FORCEIFHUNG,
                          SHTDN_REASON_MAJOR_APPLICATION);
            return 0;
        }
        if (IsOverArrowButton(pt)) { ShowPowerMenu(); return 0; }

        // Search box removed — no click handler.
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (m_subMenuOpen)
                CloseSubMenu();
            else if (m_viewMode == LeftViewMode::AllPrograms)
                NavigateBack();
            else
                Hide();
            return 0;
        }

        if (wParam == VK_DOWN || wParam == VK_UP) {
            bool down = (wParam == VK_DOWN);

            if (m_viewMode == LeftViewMode::Programs) {
                // Navigation range: 0..PROG_COUNT-1, then AP row
                // S7: total navigable items = pinned + recently used
                int totalProgItems = PROG_COUNT + static_cast<int>(m_recentItems.size());
                if (down) {
                    if (m_keySelApRow) {
                        // already at bottom; clamp
                    } else if (m_keySelProgIndex < 0) {
                        m_keySelProgIndex = 0;
                    } else if (m_keySelProgIndex < totalProgItems - 1) {
                        ++m_keySelProgIndex;
                    } else {
                        m_keySelApRow     = true;
                        m_keySelProgIndex = -1;
                    }
                } else {
                    if (m_keySelApRow) {
                        m_keySelApRow     = false;
                        m_keySelProgIndex = totalProgItems - 1;
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

    case WM_TIMER:
        if (wParam == HOVER_TIMER_ID) {
            KillTimer(m_hwnd, HOVER_TIMER_ID);
            m_hoverTimer = 0;
            if (m_hoverCandidate >= 0 && m_viewMode == LeftViewMode::AllPrograms)
                OpenSubMenu(m_hoverCandidate);
            m_hoverCandidate = -1;
        }
        return 0;

    case WM_MOUSEWHEEL: {
        // Only scroll in AllPrograms view, over the left column.
        if (m_viewMode != LeftViewMode::AllPrograms) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hwnd, &pt);
        if (pt.x >= DIVIDER_X || pt.y >= AP_ROW_Y) return 0;

        int delta  = GET_WHEEL_DELTA_WPARAM(wParam);
        int total  = static_cast<int>(CurrentApNodes().size());
        int maxOff = max(0, total - AP_MAX_VISIBLE);
        int step   = 3;   // items per wheel notch

        if (delta < 0)
            m_apScrollOffset = min(m_apScrollOffset + step, maxOff);
        else
            m_apScrollOffset = max(m_apScrollOffset - step, 0);

        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;
    }

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
