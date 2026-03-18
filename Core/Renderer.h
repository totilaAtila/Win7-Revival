#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace CrystalFrame {

// Accent state for SetWindowCompositionAttribute
enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5,
    ACCENT_INVALID_STATE = 6
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;  // ABGR format
    DWORD AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

// Window Composition Attribute constants
constexpr DWORD WCA_ACCENT_POLICY = 19;

// Function pointer for undocumented API
typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize();
    void Shutdown();

    // Set target windows (single = primary; multi = all monitors)
    void SetTaskbarWindow(HWND hwnd);
    void SetTaskbarWindows(const std::vector<HWND>& hwnds);
    void SetStartWindow(HWND hwnd);

    // Opacity control (0 = fully transparent, 100 = opaque)
    void SetTaskbarOpacity(int opacity);
    void SetStartOpacity(int opacity);

    // Color control (RGB 0-255)
    void SetTaskbarColor(int r, int g, int b);

    // Enable/disable transparency
    void SetTaskbarEnabled(bool enabled);
    void SetStartEnabled(bool enabled);

    // Enable/disable blur/acrylic effect
    void SetTaskbarBlur(bool useBlur);
    void SetStartBlur(bool useBlur);

    // Reapply transparency (call periodically to maintain effect)
    void RefreshTransparency();

private:
    pfnSetWindowCompositionAttribute m_setWindowCompositionAttribute = nullptr;
    // Task 4: when SWCA is unavailable (some Win11 builds / VMs), fall back to
    // SetLayeredWindowAttributes for basic alpha transparency without blur.
    bool m_wcaUnavailable = false;
    // Windows build number detected at Initialize() via RtlGetVersion.
    // Used to select the correct AccentFlags strategy per OS generation.
    DWORD m_buildNumber = 0;

    std::vector<HWND> m_hwndTaskbars;
    HWND m_hwndStart = nullptr;

    int m_taskbarOpacity = 75;
    int m_startOpacity = 50;
    bool m_taskbarEnabled = true;
    bool m_startEnabled = true;
    bool m_taskbarBlur = false;
    bool m_startBlur = false;

    int m_taskbarColorR = 0;
    int m_taskbarColorG = 0;
    int m_taskbarColorB = 0;

    void ApplyTransparency(HWND hwnd, int opacity, bool enabled, bool useBlur);
    void ApplyTransparencyWithColor(HWND hwnd, int opacity, bool enabled, int r, int g, int b, bool useBlur);
    void RestoreWindow(HWND hwnd);
};

} // namespace CrystalFrame
