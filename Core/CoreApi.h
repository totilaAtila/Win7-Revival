#pragma once

#ifdef CRYSTALFRAME_EXPORTS
#define CRYSTALFRAME_API __declspec(dllexport)
#else
#define CRYSTALFRAME_API __declspec(dllimport)
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
CRYSTALFRAME_API bool CoreInitialize();

// Shutdown the Core engine
CRYSTALFRAME_API void CoreShutdown();

// Set taskbar opacity (0-100)
// 0 = opaque (disabled), 100 = fully transparent
CRYSTALFRAME_API void CoreSetTaskbarOpacity(int opacity);

// Set start menu opacity (0-100)
CRYSTALFRAME_API void CoreSetStartOpacity(int opacity);

// Enable/disable taskbar transparency
CRYSTALFRAME_API void CoreSetTaskbarEnabled(bool enabled);

// Enable/disable start menu transparency
CRYSTALFRAME_API void CoreSetStartEnabled(bool enabled);

// Set taskbar color (RGB 0-255)
CRYSTALFRAME_API void CoreSetTaskbarColor(int r, int g, int b);

// Enable/disable blur/acrylic effect on taskbar
CRYSTALFRAME_API void CoreSetTaskbarBlur(bool enabled);

// Enable/disable blur/acrylic effect on start menu
CRYSTALFRAME_API void CoreSetStartBlur(bool enabled);

// Enable/disable custom Start Menu hook (intercepts Windows key and Start button clicks)
CRYSTALFRAME_API void CoreSetStartMenuHookEnabled(bool enabled);

// Set custom Start Menu opacity (0-100, same semantics as taskbar)
CRYSTALFRAME_API void CoreSetStartMenuOpacity(int opacity);

// Set custom Start Menu background color (RGB as DWORD, e.g., 0x00RRGGBB)
CRYSTALFRAME_API void CoreSetStartMenuBackgroundColor(unsigned int rgb);

// Set custom Start Menu text color (RGB as DWORD, e.g., 0x00RRGGBB)
CRYSTALFRAME_API void CoreSetStartMenuTextColor(unsigned int rgb);

// Set Start Menu items visibility
CRYSTALFRAME_API void CoreSetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                                             bool documents, bool pictures, bool videos, bool recentFiles);

// Get current status
CRYSTALFRAME_API void CoreGetStatus(CoreStatus* status);

// Message pump - call this periodically from UI thread (or run in background thread)
// Returns false when shutdown is requested
CRYSTALFRAME_API bool CoreProcessMessages();

} // extern "C"
