#pragma once
#include <Windows.h>
#include <functional>

namespace GlassBar {

/// <summary>
/// Hooks Windows key and Start button clicks to trigger custom Start Menu
/// </summary>
class StartMenuHook {
public:
    StartMenuHook();
    ~StartMenuHook();

    /// <summary>
    /// Initialize the hook (installs keyboard and mouse hooks)
    /// </summary>
    bool Initialize();

    /// <summary>
    /// Shutdown and remove hooks
    /// </summary>
    void Shutdown();

    /// <summary>
    /// Enable/disable the hook
    /// </summary>
    void SetEnabled(bool enabled);

    /// <summary>
    /// Callback when Start Menu should be shown
    /// Parameters: x, y position where to show the menu
    /// </summary>
    using ShowMenuCallback = std::function<void(int x, int y)>;
    void SetShowMenuCallback(ShowMenuCallback callback);

    /// <summary>
    /// Callback when Start Menu should be hidden (ESC or click outside)
    /// </summary>
    using HideMenuCallback = std::function<void()>;
    void SetHideMenuCallback(HideMenuCallback callback);

    /// <summary>
    /// Callback to check if Start Menu is currently visible
    /// Returns: true if visible, false otherwise
    /// </summary>
    using IsMenuVisibleCallback = std::function<bool()>;
    void SetIsMenuVisibleCallback(IsMenuVisibleCallback callback);

    /// <summary>
    /// Callback to get Start Menu window bounds
    /// Returns: RECT with menu bounds (or empty RECT if not visible)
    /// </summary>
    using GetMenuBoundsCallback = std::function<RECT()>;
    void SetGetMenuBoundsCallback(GetMenuBoundsCallback callback);

    /// <summary>
    /// Callback to forward a virtual key to the Start Menu window.
    /// Called for VK_UP, VK_DOWN, VK_RETURN, and VK_ESCAPE when the menu is visible.
    /// Implementation should PostMessage(menuHwnd, WM_KEYDOWN, vk, 0).
    /// </summary>
    using ForwardKeyCallback = std::function<void(UINT vk)>;
    void SetForwardKeyCallback(ForwardKeyCallback callback);

private:
    bool m_enabled = false;
    HHOOK m_keyboardHook = nullptr;
    HHOOK m_mouseHook = nullptr;
    ShowMenuCallback m_showMenuCallback;
    HideMenuCallback m_hideMenuCallback;
    IsMenuVisibleCallback m_isMenuVisibleCallback;
    GetMenuBoundsCallback m_getMenuBoundsCallback;
    ForwardKeyCallback m_forwardKeyCallback;
    HWND m_startButtonHwnd = nullptr;

    // Hook procedures (must be static)
    static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Instance pointer for static callbacks
    static StartMenuHook* s_instance;

    // Win key state machine — deferred suppression for combo detection
    bool m_winDown  = false;  // Win key is currently held
    bool m_winCombo = false;  // another key was pressed while Win was held

    // Helper methods
    void FindStartButton();
    bool IsClickOnStartButton(POINT pt);
    void ShowStartMenu(int x, int y);
};

} // namespace GlassBar
