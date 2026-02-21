#pragma once
#include <Windows.h>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "AllProgramsEnumerator.h"   // MenuNode, BuildAllProgramsTree

namespace CrystalFrame {

/// <summary>
/// Submenu item (kept for API compatibility)
/// </summary>
struct SubMenuItem {
    const wchar_t* name;
    const wchar_t* command;
};

/// <summary>
/// Recommended section item configuration (kept for Dashboard API compatibility;
/// not rendered in Win7 mode — Win7 left column has no "Recommended" section).
/// </summary>
struct MenuItem {
    const wchar_t* name;
    const wchar_t* icon;
    bool visible;
    SubMenuItem* submenuItems;
    int submenuCount;
};

/// <summary>
/// Pinned app shown in the left-column vertical list (Win7 style).
/// </summary>
struct PinnedItem {
    const wchar_t* name;       // Full app name (shown to the right of the icon)
    const wchar_t* shortName;  // Short label drawn inside icon square
    const wchar_t* command;    // Shell command / URI to execute
    COLORREF iconColor;        // Icon square background color
};

/// <summary>
/// Win7-style right-column link item.
/// Uses SHGetKnownFolderPath when folderId is non-null (preferred);
/// falls back to ShellExecuteW with target/params for shell applets.
/// isSeparator == true → draw a horizontal separator, no click.
/// </summary>
struct Win7RightItem {
    const wchar_t*  label;          // display text; nullptr for separators
    bool            isSeparator;    // if true, all other fields are unused
    const GUID*     folderId;       // non-null → resolve path via SHGetKnownFolderPath
    const wchar_t*  verb;           // ShellExecute verb (L"explore" / L"open")
    const wchar_t*  target;         // ShellExecute target (when folderId == nullptr)
    const wchar_t*  params;         // ShellExecute lpParameters (may be nullptr)
};

/// Left-column view mode:
///   Programs     — vertical list of pinned apps + "All Programs" entry
///   AllPrograms  — "All Programs" tree from AllProgramsEnumerator
enum class LeftViewMode { Programs, AllPrograms };

/// <summary>
/// Custom Start Menu window — Windows 7-style two-column layout.
///
/// Left column  : [Pinned programs — vertical list]
///                [All Programs / Back row]
///                [Search box]
/// Right column : Win7 shell links (Documents, Pictures, Music, …, Control Panel, …)
/// Bottom bar   : User avatar + name | Power button
/// </summary>
class StartMenuWindow {
public:
    StartMenuWindow();
    ~StartMenuWindow();

    /// Initialize window classes (call once at startup)
    bool Initialize();

    /// Show the menu; x/y are hint coords (actual position is calculated from taskbar)
    void Show(int x, int y);

    /// Hide the menu
    void Hide();

    /// Set background transparency (0 = opaque, 100 = fully transparent)
    void SetOpacity(int opacity);

    /// Set background color (COLORREF)
    void SetBackgroundColor(COLORREF color);

    /// Set text color (COLORREF)
    void SetTextColor(COLORREF color);

    /// Set which recommended items are visible (API kept for Dashboard compat)
    void SetMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                      bool documents, bool pictures, bool videos, bool recentFiles);

    bool IsVisible() const { return m_visible; }

    /// Get current window bounds in screen coordinates (empty RECT if hidden)
    RECT GetWindowBounds() const;

    /// Get underlying HWND (used by StartMenuHook to PostMessage nav keys)
    HWND GetMenuHwnd() const { return m_hwnd; }

    /// Cleanup all windows and hooks
    void Shutdown();

private:
    // ── State ───────────────────────────────────────────────────────────────
    HWND m_hwnd    = nullptr;
    bool m_visible = false;

    int      m_opacity   = 30;
    COLORREF m_bgColor   = RGB(32, 32, 36);
    COLORREF m_textColor = RGB(255, 255, 255);

    // Recommended section items (visibility controlled by Dashboard; not painted in Win7 mode)
    MenuItem m_menuItems[7] = {
        {L"Control Panel",  L"", true,  nullptr, 0},
        {L"Device Manager", L"", true,  nullptr, 0},
        {L"Installed Apps", L"", true,  nullptr, 0},
        {L"Documents",      L"", true,  nullptr, 0},
        {L"Pictures",       L"", true,  nullptr, 0},
        {L"Videos",         L"", true,  nullptr, 0},
        {L"Recent Files",   L"", true,  nullptr, 0}
    };

    // Persisted custom names for recommended items
    std::wstring m_customMenuNames[7];
    std::wstring m_customTitle;

    // ── Left-column view mode ───────────────────────────────────────────────
    LeftViewMode m_viewMode = LeftViewMode::Programs;

    // Navigation stack for All Programs drill-down.
    // Each entry points to the children vector of the folder we entered.
    // Empty stack = root of m_programTree.
    std::vector<const std::vector<MenuNode>*> m_apNavStack;

    // Hover state
    int  m_hoveredProgIndex    = -1;   // Programs list (pinned items)
    bool m_hoveredApRow        = false; // "All Programs" / "Back" row
    int  m_hoveredApIndex      = -1;   // All Programs tree item
    int  m_hoveredRightIndex   = -1;   // Win7 right column hover
    bool m_hoveredShutdown     = false;   // over "Shut down" text button
    bool m_hoveredArrow        = false;   // over the arrow dropdown button
    bool m_trackingMouse       = false;

    // Keyboard selection state (distinct from hover; cleared by mouse movement)
    int  m_keySelProgIndex     = -1;   // keyboard-focused item in Programs list
    bool m_keySelApRow         = false; // keyboard focus on "All Programs"/"Back" row
    int  m_keySelApIndex       = -1;   // keyboard-focused item in AllPrograms list (absolute)

    // Scroll offset for AllPrograms list (first visible node index); keyboard-driven.
    int  m_apScrollOffset      = 0;

    // Hover-to-open submenu state (S3.3)
    UINT_PTR m_hoverTimer        = 0;   // SetTimer handle; 0 = no pending timer
    int      m_hoverCandidate    = -1;  // absolute AP node idx waiting for delay
    bool     m_subMenuOpen       = false;
    int      m_subMenuNodeIdx    = -1;  // absolute AP node that opened the submenu
    int      m_subMenuHoveredIdx = -1;  // visual index in submenu list (-1 = none)

    static constexpr UINT_PTR HOVER_TIMER_ID  = 1;
    static constexpr UINT     HOVER_DELAY_MS  = 400;

    // Cached Windows login name for the right-column header
    wchar_t m_username[64] = {};

    // Phase S2: All Programs tree pre-cached at Initialize().
    std::vector<MenuNode> m_programTree;


    // ── Layout constants (Windows 7 style) ──────────────────────────────────
    static constexpr int WIDTH  = 400;   // 450 - 50 (left panel narrowed ~7 chars)
    static constexpr int HEIGHT = 535;   // 460 + 75 (~2 cm taller at 96 DPI)
    static constexpr int MARGIN = 12;

    // ── Win7 two-column divider ─────────────────────────────────────────────
    // Left column  : x ∈ [0, DIVIDER_X)
    // Divider line : x == DIVIDER_X
    // Right column : x ∈ (DIVIDER_X, WIDTH)
    // Left panel 248px, right panel 152px.
    static constexpr int DIVIDER_X  = 248;   // was 298; narrowed ~50px (~7 chars)
    static constexpr int RC_X       = DIVIDER_X + 4;   // right col content left edge

    // Right column row metrics
    static constexpr int RC_HDR_H   = 64;    // username header height
    static constexpr int RC_ITEM_H  = 36;    // clickable item row height
    static constexpr int RC_SEP_H   = 14;    // separator row height

    // Total entries in s_rightItems (includes separators)
    static constexpr int RIGHT_ITEM_COUNT = 10;

    // ── Bottom bar (Win7 Shut-down button + arrow dropdown) ─────────────────
    static constexpr int BOTTOM_BAR_H    = 40;
    static constexpr int BOTTOM_BAR_Y    = HEIGHT - BOTTOM_BAR_H;   // 495
    // "Shut down" rectangular button (Win7 style)
    static constexpr int SHUT_BTN_W      = 88;   // width of "Shut down" label area
    static constexpr int SHUT_BTN_H      = 26;   // height of the button
    static constexpr int SHUT_ARROW_W    = 18;   // width of the arrow dropdown button

    // ── Search box — REMOVED (search box hidden per design decision; Windows
    //    search is still accessible via Win+S; kept as constant for hit-test
    //    legacy code that may reference it). Do NOT re-add to Paint().
    static constexpr int SEARCH_H        = 34;
    static constexpr int SEARCH_Y        = BOTTOM_BAR_Y - SEARCH_H - 2;  // 459

    // ── "All Programs" / "Back" row — just above bottom bar (search removed) ─
    static constexpr int AP_ROW_H        = 28;
    static constexpr int AP_ROW_Y        = BOTTOM_BAR_Y - AP_ROW_H - 2;  // 465

    // ── Programs list — vertical pinned-app rows, fills top of left column ──
    static constexpr int PROG_Y          = 8;
    static constexpr int PROG_ITEM_H     = 36;
    static constexpr int PROG_ICON_SZ    = 24;
    static constexpr int PROG_COUNT      = 6;   // must match s_pinnedItems length

    // Max items visible in All Programs list (without scroll)
    static constexpr int AP_MAX_VISIBLE  = (AP_ROW_Y - PROG_Y) / PROG_ITEM_H; // 12

    // ── Hover submenu panel layout (S3.3) ───────────────────────────────────
    static constexpr int SM_X       = DIVIDER_X + 4;
    static constexpr int SM_TITLE_H = 32;
    static constexpr int SM_ITEM_H  = 36;
    static constexpr int SM_MAX_VIS = (BOTTOM_BAR_Y - SM_TITLE_H) / SM_ITEM_H;

    // ── Window class names ──────────────────────────────────────────────────
    static constexpr wchar_t WINDOW_CLASS[]      = L"CrystalFrame_StartMenu";
    static constexpr wchar_t EDIT_DIALOG_CLASS[] = L"CrystalFrame_EditDialog";

    // ── Static data ─────────────────────────────────────────────────────────
    static const PinnedItem    s_pinnedItems[PROG_COUNT];
    static const Win7RightItem s_rightItems[RIGHT_ITEM_COUNT];

    // ── Win32 plumbing ──────────────────────────────────────────────────────
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMenuWindow();
    void ApplyTransparency();

    // ── Painting ────────────────────────────────────────────────────────────
    void Paint();

    // Left column — Programs view
    void PaintProgramsList(HDC hdc, const RECT& cr);

    // Left column — All Programs view (tree from AllProgramsEnumerator)
    void PaintAllProgramsView(HDC hdc, const RECT& cr);

    // Left column — "All Programs" / "Back" row (shared by both views)
    void PaintApRow(HDC hdc, const RECT& cr);

    // Left column — search box (Win7: at the bottom of the left column)
    void PaintWin7SearchBox(HDC hdc, const RECT& cr);

    // Right column
    void PaintWin7RightColumn(HDC hdc, const RECT& cr);

    // Bottom bar
    void PaintBottomBar(HDC hdc, const RECT& cr);

    // Draw a colored rounded icon square with a short label inside
    void DrawIconSquare(HDC hdc, int cx, int cy, int sz,
                        COLORREF bgColor, const wchar_t* label,
                        COLORREF textColor = RGB(255, 255, 255));

    // Draw a subtle horizontal separator line
    void DrawSeparator(HDC hdc, int y, int x1, int x2);

    // ── Hit testing ─────────────────────────────────────────────────────────
    int  GetProgItemAtPoint(POINT pt);      // pinned list; -1 if none
    bool IsOverApRow(POINT pt);             // "All Programs" / "Back" row
    int  GetApItemAtPoint(POINT pt);        // All Programs list item; -1 if none
    bool IsOverShutdownButton(POINT pt);  // "Shut down" text button
    bool IsOverArrowButton(POINT pt);     // dropdown arrow button
    int  GetRightItemAtPoint(POINT pt);     // -1 if none / separator

    // ── Execution ───────────────────────────────────────────────────────────
    void ExecutePinnedItem(int index);
    void ExecuteRecommendedItem(int index); // kept for backward compat
    void ExecuteRightItem(int index);       // Win7 right column launch
    void LaunchApItem(int index);           // launch item from current AP node list

    // ── All Programs navigation ──────────────────────────────────────────────
    const std::vector<MenuNode>& CurrentApNodes() const;
    void NavigateIntoFolder(const std::vector<MenuNode>& children);
    void NavigateBack();

    // ── Hover-to-open lateral submenu (S3.3) ─────────────────────────────────
    void OpenSubMenu(int apNodeIdx);       // show submenu for folder at apNodeIdx
    void CloseSubMenu();                   // hide submenu + reset state
    bool IsOverSubMenu(POINT pt) const;    // true if pt is in the submenu panel
    int  GetSubMenuItemAtPoint(POINT pt);  // visual index in submenu; -1 if none
    void PaintSubMenu(HDC hdc, const RECT& cr);
    void ExecuteSubMenuItem(int visualIdx);

    // ── Color helpers ────────────────────────────────────────────────────────
    COLORREF CalculateHoverColor();
    COLORREF CalculateSubtleColor();      // slightly lighter/darker than bg
    COLORREF CalculateBorderColor();      // subtle border
    COLORREF CalculateSelectionColor();   // keyboard-focus accent (fixed blue)

    // ── Name helpers ─────────────────────────────────────────────────────────
    const wchar_t* GetMenuItemName(int index);
    const wchar_t* GetTitle();

    // ── Persistence ──────────────────────────────────────────────────────────
    void LoadCustomNames();
    void SaveCustomNames();

    // ── Power menu (Sleep / Shut down / Restart popup) ───────────────────────
    void ShowPowerMenu();

    // ── Edit dialog (rename recommended item) ────────────────────────────────
    void ShowEditDialog(int itemIndex);
    static LRESULT CALLBACK EditDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace CrystalFrame
