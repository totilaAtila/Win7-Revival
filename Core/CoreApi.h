#pragma once

#ifdef GLASSBAR_EXPORTS
#define GLASSBAR_API __declspec(dllexport)
#else
#define GLASSBAR_API __declspec(dllimport)
#endif

extern "C" {

// Status structure for Dashboard
struct CoreStatus {
    struct {
        bool found;
        bool enabled;
        int opacity;
    } taskbar;

    struct {
        bool detected;
        bool enabled;
        int opacity;
    } start;
};

// Initialize the Core engine
// Returns true on success, false on failure
GLASSBAR_API bool CoreInitialize();

// Shutdown the Core engine
GLASSBAR_API void CoreShutdown();

// Set taskbar opacity (0-100)
// 0 = opaque (disabled), 100 = fully transparent
GLASSBAR_API void CoreSetTaskbarOpacity(int opacity);

// Set start menu opacity (0-100)
GLASSBAR_API void CoreSetStartOpacity(int opacity);

// Enable/disable taskbar transparency
GLASSBAR_API void CoreSetTaskbarEnabled(bool enabled);

// Enable/disable start menu transparency
GLASSBAR_API void CoreSetStartEnabled(bool enabled);

// Set taskbar color (RGB 0-255)
GLASSBAR_API void CoreSetTaskbarColor(int r, int g, int b);

// Enable/disable blur/acrylic effect on taskbar
GLASSBAR_API void CoreSetTaskbarBlur(bool enabled);

// Enable/disable blur/acrylic effect on start menu
GLASSBAR_API void CoreSetStartBlur(bool enabled);

// Enable/disable custom Start Menu hook (intercepts Windows key and Start button clicks)
GLASSBAR_API void CoreSetStartMenuHookEnabled(bool enabled);

// Set custom Start Menu opacity (0-100, same semantics as taskbar)
GLASSBAR_API void CoreSetStartMenuOpacity(int opacity);

// Set custom Start Menu background color (RGB as DWORD, e.g., 0x00RRGGBB)
GLASSBAR_API void CoreSetStartMenuBackgroundColor(unsigned int rgb);

// Set custom Start Menu text color (RGB as DWORD, e.g., 0x00RRGGBB)
GLASSBAR_API void CoreSetStartMenuTextColor(unsigned int rgb);

// Set Start Menu items visibility
GLASSBAR_API void CoreSetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                                             bool documents, bool pictures, bool videos, bool recentFiles);

// S-B: Pin Start Menu open for Dashboard preview (true=pinned/visible, false=unpin+hide)
GLASSBAR_API void CoreSetStartMenuPinned(bool pinned);

// S-E: Set explicit border/accent color (RGB as DWORD, 0x00RRGGBB)
GLASSBAR_API void CoreSetStartMenuBorderColor(unsigned int rgb);

// Get current status
GLASSBAR_API void CoreGetStatus(CoreStatus* status);

// Message pump - call this periodically from UI thread (or run in background thread)
// Returns false when shutdown is requested
GLASSBAR_API bool CoreProcessMessages();

// Set XamlBridge blur amount (0 = off, 1-100 = intensity).
// On 22H2+ this triggers injection of GlassBar.XamlBridge.dll into explorer.exe
// and applies acrylic blur from within the owner process.
GLASSBAR_API void CoreSetTaskbarBlurAmount(int amount);

// Register a global hotkey that toggles the taskbar overlay.
// vk: virtual-key code (e.g. 'G' = 0x47); modifiers: MOD_CONTROL | MOD_ALT etc.
// Pass vk=0 to disable the hotkey.
GLASSBAR_API void CoreRegisterHotkey(int vk, int modifiers);

// Unregister the global hotkey and clear it from config.
GLASSBAR_API void CoreUnregisterHotkey();

} // extern "C"
