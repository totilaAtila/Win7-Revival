#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include <vector>
#include <unordered_map>
#include "XamlBridge/SharedBlurState.h"

using Microsoft::WRL::ComPtr;

namespace GlassBar {

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

    // XamlBridge: set blur amount 0-100 (0=off, 1-100=intensity hint)
    // On 22H2+ this triggers injection of GlassBar.XamlBridge.dll into explorer.exe.
    void SetTaskbarBlurAmount(int amount);

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

    // ── XamlBridge (blur injection into explorer.exe) ──────────────────────────
    HANDLE           m_hSharedMem    = nullptr;   // file mapping created by us (Core side)
    SharedBlurState* m_pSharedState  = nullptr;   // mapped view of shared memory
    HMODULE          m_hXamlBridge   = nullptr;   // GlassBar.XamlBridge.dll handle
    HHOOK            m_hInjHook      = nullptr;   // WH_CALLWNDPROC hook for injection
    bool             m_bridgeInited  = false;     // injection attempted flag
    int              m_blurAmount    = 0;         // 0-100

    void InitXamlBridge();
    void UpdateSharedState();
    void ShutdownXamlBridge();

    // Iter#7: overlay window for color tint on 25H2+ (SWCA confirmed inert)
    std::unordered_map<HWND, HWND> m_overlayWindows; // taskbar HWND -> overlay HWND
    ATOM m_overlayClassAtom = 0;

    void EnsureOverlayWindow(HWND taskbarHwnd);
    void UpdateOverlayAppearance(HWND overlayHwnd, int r, int g, int b, int opacity, bool enabled);
    void DestroyAllOverlays();
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace GlassBar
