#pragma once
#include <Windows.h>
#include <functional>
#include <string>
#include <map>

namespace CrystalFrame {

/// <summary>
/// Submenu item (kept for API compatibility)
/// </summary>
struct SubMenuItem {
    const wchar_t* name;
    const wchar_t* command;
};

/// <summary>
/// Recommended section item configuration
/// </summary>
struct MenuItem {
    const wchar_t* name;
    const wchar_t* icon;
    bool visible;
    SubMenuItem* submenuItems;
    int submenuCount;
};

/// <summary>
/// Pinned app shown in the left-column grid
/// </summary>
struct PinnedItem {
    const wchar_t* name;       // Full app name (shown below icon square)
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

/// <summary>
/// Custom Start Menu window — Windows 7-style two-column layout.
/// Left column : Search box | Pinned grid (2×3) | Recommended list
/// Right column: Win7 shell links (Documents, Pictures, Music, …, Control Panel, …)
/// Bottom bar  : User avatar + name | Power button
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

    /// Set which recommended items are visible
    void SetMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                      bool documents, bool pictures, bool videos, bool recentFiles);

    bool IsVisible() const { return m_visible; }

    /// Get current window bounds in screen coordinates (empty RECT if hidden)
    RECT GetWindowBounds() const;

    /// Cleanup all windows and hooks
    void Shutdown();

private:
    // ── State ───────────────────────────────────────────────────────────────
    HWND m_hwnd    = nullptr;
    bool m_visible = false;

    int      m_opacity   = 30;
    COLORREF m_bgColor   = RGB(32, 32, 36);
    COLORREF m_textColor = RGB(255, 255, 255);

    // Recommended section items (visibility controlled by Dashboard)
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

    // Hover state
    int  m_hoveredPinnedIndex      = -1;
    int  m_hoveredRecommendedIndex = -1;
    int  m_hoveredRightIndex       = -1;   // Win7 right column hover
    bool m_hoveredPower            = false;
    bool m_trackingMouse           = false;

    // Cached Windows login name for the right-column header
    wchar_t m_username[64] = {};

    // ── Layout constants ────────────────────────────────────────────────────
    static constexpr int WIDTH  = 580;
    static constexpr int HEIGHT = 700;
    static constexpr int MARGIN = 20;

    // ── Win7 two-column divider ─────────────────────────────────────────────
    // Left column  : x ∈ [0, DIVIDER_X)
    // Divider line : x == DIVIDER_X
    // Right column : x ∈ (DIVIDER_X, WIDTH)
    static constexpr int DIVIDER_X  = 330;
    static constexpr int RC_X       = DIVIDER_X + 4;   // right col content left edge

    // Right column row metrics
    static constexpr int RC_HDR_H   = 64;    // username header height
    static constexpr int RC_ITEM_H  = 36;    // clickable item row height
    static constexpr int RC_SEP_H   = 14;    // separator row height

    // Total entries in s_rightItems (includes separators)
    static constexpr int RIGHT_ITEM_COUNT = 10;

    // Search box (left column)
    static constexpr int SEARCH_Y        = 16;
    static constexpr int SEARCH_H        = 46;

    // "Pinned" header row (left column)
    static constexpr int PINNED_HEADER_Y = 80;

    // Pinned apps grid (left column, 2 columns × 3 rows = 6 items)
    static constexpr int PINNED_GRID_Y   = 114;
    static constexpr int PINNED_COLS     = 2;
    static constexpr int PINNED_ROWS     = 3;
    static constexpr int PINNED_COUNT    = PINNED_COLS * PINNED_ROWS;          // 6
    static constexpr int PINNED_CELL_W   = (DIVIDER_X - 2 * MARGIN) / PINNED_COLS; // 145
    static constexpr int PINNED_CELL_H   = 88;
    static constexpr int PINNED_ICON_SZ  = 44;
    static constexpr int PINNED_GRID_END = PINNED_GRID_Y + PINNED_ROWS * PINNED_CELL_H; // 378

    // "Recommended" header row (left column)
    static constexpr int REC_HEADER_Y    = PINNED_GRID_END + 12;   // 390
    static constexpr int REC_START_Y     = REC_HEADER_Y + 34;      // 424
    static constexpr int REC_ITEM_H      = 42;

    // Bottom bar (full width)
    static constexpr int BOTTOM_BAR_Y    = HEIGHT - 60;            // 640
    static constexpr int POWER_BTN_R     = 16;   // radius of power button circle

    // ── Window class names ──────────────────────────────────────────────────
    static constexpr wchar_t WINDOW_CLASS[]      = L"CrystalFrame_StartMenu";
    static constexpr wchar_t EDIT_DIALOG_CLASS[] = L"CrystalFrame_EditDialog";

    // ── Static data ─────────────────────────────────────────────────────────
    static const PinnedItem    s_pinnedItems[PINNED_COUNT];
    static const Win7RightItem s_rightItems[RIGHT_ITEM_COUNT];

    // ── Win32 plumbing ──────────────────────────────────────────────────────
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMenuWindow();
    void ApplyTransparency();

    // ── Painting ────────────────────────────────────────────────────────────
    void Paint();
    void PaintSearchBox(HDC hdc, const RECT& cr);
    void PaintPinnedSection(HDC hdc, const RECT& cr);
    void PaintRecommendedSection(HDC hdc, const RECT& cr);
    void PaintBottomBar(HDC hdc, const RECT& cr);
    void PaintWin7RightColumn(HDC hdc, const RECT& cr);   // Win7 right column

    // Draw a colored rounded icon square with a short label inside
    void DrawIconSquare(HDC hdc, int cx, int cy, int sz,
                        COLORREF bgColor, const wchar_t* label,
                        COLORREF textColor = RGB(255, 255, 255));

    // Draw a subtle horizontal separator line
    void DrawSeparator(HDC hdc, int y, int x1, int x2);

    // ── Hit testing ─────────────────────────────────────────────────────────
    int  GetPinnedItemAtPoint(POINT pt);        // -1 if none
    int  GetRecommendedItemAtPoint(POINT pt);   // -1 if none
    bool IsOverPowerButton(POINT pt);
    int  GetRightItemAtPoint(POINT pt);         // -1 if none / separator

    // ── Execution ───────────────────────────────────────────────────────────
    void ExecutePinnedItem(int index);
    void ExecuteRecommendedItem(int index);
    void ExecuteRightItem(int index);           // Win7 right column launch

    // ── Color helpers ────────────────────────────────────────────────────────
    COLORREF CalculateHoverColor();
    COLORREF CalculateSubtleColor();   // slightly lighter/darker than bg
    COLORREF CalculateBorderColor();   // subtle border

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
