#pragma once
#include <Windows.h>
#include <functional>
#include <string>
#include <map>

namespace CrystalFrame {

/// <summary>
/// Submenu item (appears in flyout)
/// </summary>
struct SubMenuItem {
    const wchar_t* name;
    const wchar_t* command;  // Shell command or path to execute
};

/// <summary>
/// Menu item configuration
/// </summary>
struct MenuItem {
    const wchar_t* name;
    const wchar_t* icon;  // Unicode icon/emoji
    bool visible;
    SubMenuItem* submenuItems;  // Array of submenu items
    int submenuCount;  // Number of submenu items
};

/// <summary>
/// Custom Start Menu window - Win32 layered window with full transparency control
/// </summary>
class StartMenuWindow {
public:
    StartMenuWindow();
    ~StartMenuWindow();

    /// <summary>
    /// Initialize window class (call once at startup)
    /// </summary>
    bool Initialize();

    /// <summary>
    /// Show the Start Menu at specified position
    /// </summary>
    void Show(int x, int y);

    /// <summary>
    /// Hide/close the Start Menu
    /// </summary>
    void Hide();

    /// <summary>
    /// Set transparency (0-100, same semantics as taskbar)
    /// 0 = opaque, 100 = fully transparent
    /// </summary>
    void SetOpacity(int opacity);

    /// <summary>
    /// Set background color (RGB)
    /// </summary>
    void SetBackgroundColor(COLORREF color);

    /// <summary>
    /// Set text color (RGB)
    /// </summary>
    void SetTextColor(COLORREF color);

    /// <summary>
    /// Set menu items visibility
    /// </summary>
    void SetMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                      bool documents, bool pictures, bool videos, bool recentFiles);

    /// <summary>
    /// Check if window is currently visible
    /// </summary>
    bool IsVisible() const { return m_visible; }

    /// <summary>
    /// Get window bounds (screen coordinates)
    /// Returns empty RECT if window not visible or invalid
    /// </summary>
    RECT GetWindowBounds() const;

    /// <summary>
    /// Cleanup
    /// </summary>
    void Shutdown();

private:
    HWND m_hwnd = nullptr;
    bool m_visible = false;
    int m_opacity = 30; // Default 30% transparency (more opaque)
    COLORREF m_bgColor = RGB(40, 40, 45); // Dark blue-gray default
    COLORREF m_textColor = RGB(255, 255, 255); // White text default

    // Menu items configuration
    // Using simple Unicode symbols that render correctly in GDI
    MenuItem m_menuItems[7] = {
        {L"Control Panel", L"▶", true},
        {L"Device Manager", L"▶", true},
        {L"Installed Apps", L"▶", true},
        {L"Documents", L"▶", true},
        {L"Pictures", L"▶", true},
        {L"Videos", L"▶", true},
        {L"Recent Files", L"▶", true}
    };

    // Custom menu names (persisted in JSON)
    std::wstring m_customMenuNames[7];  // Custom names for main menu items
    std::map<int, std::map<int, std::wstring>> m_customSubmenuNames;  // [mainIndex][subIndex] = custom name
    std::wstring m_customTitle;  // Custom title for Start Menu

    // Hover/interaction tracking
    int m_hoveredItemIndex = -1;  // Currently hovered item (-1 = none)
    bool m_trackingMouse = false;  // Mouse tracking active
    UINT_PTR m_hoverTimer = 0;  // Timer for hover delay before showing flyout

    // Flyout submenu window
    HWND m_hwndFlyout = nullptr;
    bool m_flyoutVisible = false;
    int m_flyoutForItemIndex = -1;  // Which main item the flyout is for
    int m_hoveredSubmenuIndex = -1;  // Hovered item in flyout (-1 = none)
    bool m_trackingMouseFlyout = false;

    // Window dimensions
    static constexpr int WIDTH = 300;
    static constexpr int HEIGHT = 700;
    static constexpr int FLYOUT_WIDTH = 250;
    static constexpr int FLYOUT_ITEM_HEIGHT = 30;

    // Window class names
    static constexpr wchar_t WINDOW_CLASS[] = L"CrystalFrame_StartMenu";
    static constexpr wchar_t FLYOUT_CLASS[] = L"CrystalFrame_StartMenuFlyout";
    static constexpr wchar_t EDIT_DIALOG_CLASS[] = L"CrystalFrame_EditDialog";

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Instance method for handling messages
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Create the window
    bool CreateMenuWindow();

    // Apply transparency effect
    void ApplyTransparency();

    // Paint the window
    void Paint();

    // Helper methods for interaction
    int GetItemAtPoint(POINT pt);  // Returns item index at point, or -1
    void ExecuteMenuItem(int index);  // Execute action for item
    RECT GetItemRect(int index);  // Get screen rect for item
    COLORREF CalculateHoverColor();  // Calculate hover color based on background
    void ShowEditDialog(int itemIndex, bool isSubmenu, int submenuIndex);  // Show edit dialog for item
    void LoadCustomNames();  // Load custom names from JSON
    void SaveCustomNames();  // Save custom names to JSON
    const wchar_t* GetMenuItemName(int index);  // Get name (custom or default)
    const wchar_t* GetSubmenuItemName(int mainIndex, int subIndex);  // Get submenu name
    const wchar_t* GetTitle();  // Get custom or default title
    void ShowTitleEditDialog();  // Edit title specifically

    // Flyout methods
    bool CreateFlyoutWindow();  // Create flyout window
    void ShowFlyout(int itemIndex);  // Show flyout for item
    void HideFlyout();  // Hide flyout
    void ApplyFlyoutTransparency();  // Apply transparency to flyout
    void PaintFlyout();  // Paint flyout content
    int GetSubmenuItemAtPoint(POINT pt);  // Returns submenu index at point in flyout
    void ExecuteSubmenuItem(int mainIndex, int subIndex);  // Execute submenu action
    RECT GetSubmenuItemRect(int index);  // Get rect for submenu item

    // Window procedures
    static LRESULT CALLBACK FlyoutWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleFlyoutMessage(UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace CrystalFrame
