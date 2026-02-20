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
/// Pinned app shown in the Win11-style grid
/// </summary>
struct PinnedItem {
    const wchar_t* name;       // Full app name (shown below icon square)
    const wchar_t* shortName;  // Short label drawn inside icon square
    const wchar_t* command;    // Shell command / URI to execute
    COLORREF iconColor;        // Icon square background color
};

/// <summary>
/// Custom Start Menu window — Windows 11-style layout
/// Sections: Pinned grid (3x3) | Recommended list | Bottom bar
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

    int      m_opacity  = 30;
    COLORREF m_bgColor  = RGB(32, 32, 36);
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
    bool m_hoveredPower            = false;
    bool m_hoveredUser             = false;
    bool m_trackingMouse           = false;

    // ── Layout constants ────────────────────────────────────────────────────
    static constexpr int WIDTH  = 580;
    static constexpr int HEIGHT = 700;

    static constexpr int MARGIN          = 20;

    // "Pinned" header row
    static constexpr int PINNED_HEADER_Y = 14;

    // Pinned apps grid
    static constexpr int PINNED_GRID_Y   = 48;
    static constexpr int PINNED_COLS     = 3;
    static constexpr int PINNED_ROWS     = 3;
    static constexpr int PINNED_COUNT    = PINNED_COLS * PINNED_ROWS;   // 9
    static constexpr int PINNED_CELL_W   = (WIDTH - 2 * MARGIN) / PINNED_COLS; // 180
    static constexpr int PINNED_CELL_H   = 88;
    static constexpr int PINNED_ICON_SZ  = 44;
    static constexpr int PINNED_GRID_END = PINNED_GRID_Y + PINNED_ROWS * PINNED_CELL_H; // 378

    // "Recommended" header row
    static constexpr int REC_HEADER_Y    = PINNED_GRID_END + 12;   // 390
    // Recommended items list
    static constexpr int REC_START_Y     = REC_HEADER_Y + 34;      // 424
    static constexpr int REC_ITEM_H      = 42;

    // Bottom bar
    static constexpr int BOTTOM_BAR_Y    = HEIGHT - 60;            // 640
    static constexpr int POWER_BTN_R     = 16;   // radius of power button circle

    // ── Window class names ──────────────────────────────────────────────────
    static constexpr wchar_t WINDOW_CLASS[]      = L"CrystalFrame_StartMenu";
    static constexpr wchar_t EDIT_DIALOG_CLASS[] = L"CrystalFrame_EditDialog";

    // ── Static pinned apps data ─────────────────────────────────────────────
    static const PinnedItem s_pinnedItems[PINNED_COUNT];

    // ── Win32 plumbing ──────────────────────────────────────────────────────
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMenuWindow();
    void ApplyTransparency();

    // ── Painting ────────────────────────────────────────────────────────────
    void Paint();
    void PaintPinnedSection(HDC hdc, const RECT& cr);
    void PaintRecommendedSection(HDC hdc, const RECT& cr);
    void PaintBottomBar(HDC hdc, const RECT& cr);

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
    bool IsOverUserArea(POINT pt);

    // ── Execution ───────────────────────────────────────────────────────────
    void ExecutePinnedItem(int index);
    void ExecuteRecommendedItem(int index);

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
