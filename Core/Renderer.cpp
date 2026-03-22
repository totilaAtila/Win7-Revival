#include "Renderer.h"
#include "Diagnostics.h"
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace GlassBar {

Renderer::Renderer() {
}

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize() {
    // Detect Windows build number via RtlGetVersion (ntdll) — more reliable than
    // GetVersionEx which is shimmed on newer Windows versions.
    typedef NTSTATUS(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(hNtdll, "RtlGetVersion"));
        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = {};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            rtlGetVersion(&osvi);
            m_buildNumber = osvi.dwBuildNumber;
        }
    }
    // Windows 11 24H2 RTM = build 26100; 25H2 observed at build 26200+
    // (Earlier guess of ~27000 was incorrect based on insider builds.)
    CF_LOG(Info, "Windows build number: " << m_buildNumber
                 << (m_buildNumber >= 26200 ? " (25H2+ — adaptive AccentFlags enabled)"
                   : m_buildNumber >= 26100 ? " (24H2)"
                   : " (pre-24H2)"));

    // Load SetWindowCompositionAttribute from user32.dll
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        hUser32 = LoadLibraryW(L"user32.dll");
    }

    if (!hUser32) {
        CF_LOG(Error, "Failed to load user32.dll");
        return false;
    }

    m_setWindowCompositionAttribute = reinterpret_cast<pfnSetWindowCompositionAttribute>(
        GetProcAddress(hUser32, "SetWindowCompositionAttribute")
    );

    if (!m_setWindowCompositionAttribute) {
        // Task 4: SWCA is undocumented and absent on some Windows 11 builds /
        // virtualised environments.  Fall back to SetLayeredWindowAttributes so
        // at least basic alpha transparency (no blur) is still applied.
        CF_LOG(Warning, "SetWindowCompositionAttribute not found in user32.dll — "
                        "using SetLayeredWindowAttributes fallback (no blur)");
        m_wcaUnavailable = true;
    }

    CF_LOG(Info, m_wcaUnavailable
        ? "Renderer initialized (fallback mode: SetLayeredWindowAttributes)"
        : "Renderer initialized (SetWindowCompositionAttribute ready)");
    return true;
}

void Renderer::Shutdown() {
    // Restore original window states
    for (HWND h : m_hwndTaskbars) {
        RestoreWindow(h);
    }
    if (m_hwndStart) {
        RestoreWindow(m_hwndStart);
    }

    CF_LOG(Info, "Renderer shutdown");
}

void Renderer::SetTaskbarWindow(HWND hwnd) {
    m_hwndTaskbars = hwnd ? std::vector<HWND>{hwnd} : std::vector<HWND>{};
    if (hwnd) {
        ApplyTransparency(hwnd, m_taskbarOpacity, m_taskbarEnabled, m_taskbarBlur);
        CF_LOG(Info, "Taskbar window set: 0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd));
    }
}

void Renderer::SetTaskbarWindows(const std::vector<HWND>& hwnds) {
    m_hwndTaskbars = hwnds;
    for (HWND h : m_hwndTaskbars) {
        if (h) {
            ApplyTransparency(h, m_taskbarOpacity, m_taskbarEnabled, m_taskbarBlur);
        }
    }
    CF_LOG(Info, "Taskbar windows set: count=" << m_hwndTaskbars.size());
}

void Renderer::SetStartWindow(HWND hwnd) {
    m_hwndStart = hwnd;
    if (hwnd) {
        ApplyTransparency(hwnd, m_startOpacity, m_startEnabled, m_startBlur);
        CF_LOG(Info, "Start window set: 0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd));
    }
}

void Renderer::SetTaskbarOpacity(int opacity) {
    opacity = std::clamp(opacity, 0, 100);
    m_taskbarOpacity = opacity;

    if (m_taskbarEnabled) {
        for (HWND h : m_hwndTaskbars) {
            ApplyTransparencyWithColor(h, opacity, true,
                m_taskbarColorR, m_taskbarColorG, m_taskbarColorB, m_taskbarBlur);
        }
        CF_LOG(Debug, "Taskbar opacity set to " << opacity << "%");
    }
}

void Renderer::SetStartOpacity(int opacity) {
    opacity = std::clamp(opacity, 0, 100);
    m_startOpacity = opacity;

    CF_LOG(Info, "SetStartOpacity called: opacity=" << opacity
                 << ", m_hwndStart=" << m_hwndStart
                 << ", m_startEnabled=" << m_startEnabled);

    if (m_hwndStart && m_startEnabled) {
        if (!IsWindow(m_hwndStart)) {
            CF_LOG(Warning, "Start window handle is no longer valid!");
            m_hwndStart = nullptr;
            return;
        }
        CF_LOG(Info, "Applying transparency to Start Menu window");
        ApplyTransparency(m_hwndStart, opacity, true, m_startBlur);
        CF_LOG(Info, "Start opacity set to " << opacity << "%");
    } else {
        if (!m_hwndStart) CF_LOG(Warning, "Start window handle is NULL - cannot apply opacity");
        if (!m_startEnabled) CF_LOG(Info, "Start transparency is disabled");
    }
}

void Renderer::SetTaskbarColor(int r, int g, int b) {
    m_taskbarColorR = std::clamp(r, 0, 255);
    m_taskbarColorG = std::clamp(g, 0, 255);
    m_taskbarColorB = std::clamp(b, 0, 255);

    if (m_taskbarEnabled) {
        for (HWND h : m_hwndTaskbars) {
            ApplyTransparencyWithColor(h, m_taskbarOpacity, true,
                m_taskbarColorR, m_taskbarColorG, m_taskbarColorB, m_taskbarBlur);
        }
    }
}

void Renderer::SetTaskbarEnabled(bool enabled) {
    m_taskbarEnabled = enabled;

    for (HWND h : m_hwndTaskbars) {
        if (enabled) {
            ApplyTransparency(h, m_taskbarOpacity, true, m_taskbarBlur);
        } else {
            RestoreWindow(h);
        }
    }
    CF_LOG(Info, "Taskbar transparency " << (enabled ? "enabled" : "disabled"));
}

void Renderer::SetStartEnabled(bool enabled) {
    m_startEnabled = enabled;

    if (m_hwndStart) {
        if (enabled) {
            ApplyTransparency(m_hwndStart, m_startOpacity, true, m_startBlur);
        } else {
            RestoreWindow(m_hwndStart);
        }
        CF_LOG(Info, "Start transparency " << (enabled ? "enabled" : "disabled"));
    }
}

void Renderer::SetTaskbarBlur(bool useBlur) {
    m_taskbarBlur = useBlur;
    if (m_taskbarEnabled) {
        for (HWND h : m_hwndTaskbars) {
            ApplyTransparencyWithColor(h, m_taskbarOpacity, true,
                m_taskbarColorR, m_taskbarColorG, m_taskbarColorB, useBlur);
        }
    }
    CF_LOG(Info, "Taskbar blur " << (useBlur ? "enabled" : "disabled"));
}

void Renderer::SetStartBlur(bool useBlur) {
    m_startBlur = useBlur;
    if (m_hwndStart && m_startEnabled) {
        ApplyTransparency(m_hwndStart, m_startOpacity, true, useBlur);
    }
    CF_LOG(Info, "Start blur " << (useBlur ? "enabled" : "disabled"));
}

void Renderer::ApplyTransparency(HWND hwnd, int opacity, bool enabled, bool useBlur) {
    bool isStart = (hwnd == m_hwndStart);
    if (isStart) {
        ApplyTransparencyWithColor(hwnd, opacity, enabled, 0, 0, 0, useBlur);
    } else {
        ApplyTransparencyWithColor(hwnd, opacity, enabled,
            m_taskbarColorR, m_taskbarColorG, m_taskbarColorB, useBlur);
    }
}

void Renderer::ApplyTransparencyWithColor(HWND hwnd, int opacity, bool enabled,
                                          int r, int g, int b, bool useBlur) {
    bool isStartMenu = (hwnd == m_hwndStart);
    const char* windowType = isStartMenu ? "START MENU" : "TASKBAR";

    CF_LOG(Debug, "[" << windowType << "] ApplyTransparencyWithColor: HWND=0x"
                 << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec
                 << ", opacity=" << opacity << ", enabled=" << enabled
                 << ", RGB=(" << r << "," << g << "," << b << ")"
                 << ", blur=" << useBlur);

    if (!hwnd || !IsWindow(hwnd)) {
        CF_LOG(Warning, "[" << windowType << "] ApplyTransparencyWithColor early return — invalid hwnd");
        return;
    }

    // Task 4: SWCA unavailable — apply basic alpha via SetLayeredWindowAttributes.
    if (m_wcaUnavailable || !m_setWindowCompositionAttribute) {
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (enabled && opacity > 0) {
            if (!(exStyle & WS_EX_LAYERED))
                SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            // Match the opacity inversion used by the SWCA path.
            BYTE alpha = static_cast<BYTE>(((100 - opacity) * 255) / 100);
            SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
            CF_LOG(Debug, "[" << windowType << "] Fallback: SetLayeredWindowAttributes alpha=" << (int)alpha);
        } else {
            // Restore: remove layered style if transparency was disabled.
            if (exStyle & WS_EX_LAYERED)
                SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
            CF_LOG(Debug, "[" << windowType << "] Fallback: transparency disabled");
        }
        return;
    }

    // For taskbar windows on Windows 22H2+ (build >= 22621): disable system backdrop
    // (Mica / Mica Alt) that DWM applies when "Transparency effects" is ON in Settings.
    // Without this, Mica takes priority over SWCA and the taskbar appears opaque.
    // DWMWA_SYSTEMBACKDROP_TYPE = 38 (attribute added in Windows 11 22H2).
    // DWMSBT_NONE = 1 — no system backdrop.
    // Called cross-process on Shell_TrayWnd; DWM may accept or silently reject — harmless.
    if (!isStartMenu && m_buildNumber >= 26000) {
        DWORD backdropNone = 1;
        DwmSetWindowAttribute(hwnd, 38, &backdropNone, sizeof(backdropNone));
        CF_LOG(Debug, "[TASKBAR] DwmSetWindowAttribute DWMWA_SYSTEMBACKDROP_TYPE=NONE attempted (build " << m_buildNumber << ")");
    }

    // For Windows 24H2 taskbar (builds 26000–26199): SWCA is silently ignored on
    // Shell_TrayWnd. Fall back to SetLayeredWindowAttributes (alpha-only, no color/blur).
    // NOTE: This guard is scoped to < 26200 so that 25H2+ (builds >= 26200)
    // falls through to the SWCA path below with AccentFlags=0x20.
    if (!isStartMenu && m_buildNumber >= 26000 && m_buildNumber < 26200) {
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (enabled && opacity > 0) {
            if (!(exStyle & WS_EX_LAYERED))
                SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            BYTE alpha = static_cast<BYTE>(((100 - opacity) * 255) / 100);
            SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
            CF_LOG(Debug, "[TASKBAR] Win24H2 LWA_ALPHA fallback: alpha=" << (int)alpha);
        } else {
            if (exStyle & WS_EX_LAYERED)
                SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
            CF_LOG(Debug, "[TASKBAR] Win24H2 disabled");
        }
        return;
    }

    // Iter#5 hypothesis: WS_EX_LAYERED applied before SWCA locks the window into
    // legacy layered compositing on 25H2, causing DWM to silently ignore SWCA.
    // Strategy: clear WS_EX_LAYERED first, attempt SWCA, fall back to LWA_ALPHA
    // only if SWCA returns 0 (failure).
    if (!isStartMenu && m_buildNumber >= 26200) {
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_LAYERED) {
            SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
            CF_LOG(Info, "[TASKBAR] Win25H2+ cleared WS_EX_LAYERED before SWCA");
        }
        // SWCA attempted below; LWA_ALPHA applied after if SWCA fails.
    }

    ACCENT_POLICY accent = {};

    if (enabled && opacity > 0) {
        if (m_buildNumber >= 26200 && !isStartMenu) {
            // On 25H2+ (build 26200+) TRANSPARENTGRADIENT is silently ignored for
            // the taskbar; ACRYLICBLURBEHIND uses the modern Acrylic pipeline and
            // honours GradientColor. Honor useBlur: when blur is off, try
            // TRANSPARENTGRADIENT anyway — it may be ignored, but colour+alpha
            // still reaches the taskbar via LWA_ALPHA applied above.
            accent.AccentState = useBlur ? ACCENT_ENABLE_ACRYLICBLURBEHIND
                                         : ACCENT_ENABLE_TRANSPARENTGRADIENT;
            CF_LOG(Debug, "[" << windowType << "] Win25H2+ "
                         << (useBlur ? "ACRYLICBLURBEHIND" : "TRANSPARENTGRADIENT (blur off)"));
        } else if (useBlur) {
            // Acrylic blur (Windows 10 1803+ / Windows 11)
            accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        } else {
            accent.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT;
        }

        // Alpha: 0% opacity slider = fully opaque (alpha 255); 100% = fully transparent (alpha 0)
        BYTE alpha = static_cast<BYTE>(((100 - opacity) * 255) / 100);

        // GradientColor format: ABGR
        DWORD gradientColor = (alpha << 24) | (b << 16) | (g << 8) | r;
        accent.GradientColor = gradientColor;

        // AccentFlags strategy varies by Windows build.
        // On Windows 25H2+ (build >= 26200) AccentFlags = 2 may be silently ignored;
        // 0x20 (draw gradient on entire client area) has better compatibility.
        // On older builds AccentFlags = 2 is the correct value.
        // Iter#4 hypothesis: AccentFlags=0x20 caused SWCA to be silently ignored on 26200.
        // Revert to AccentFlags=2 (standard value, worked on pre-25H2 builds) for all paths.
        accent.AccentFlags = 2;
        CF_LOG(Info, "[" << windowType << "] AccentFlags=2 (build " << m_buildNumber << ")");

        CF_LOG(Info, "[" << windowType << "] SWCA call: AccentState=" << accent.AccentState
                     << " AccentFlags=0x" << std::hex << accent.AccentFlags
                     << " GradientColor=0x" << gradientColor << std::dec
                     << " opacity=" << opacity << "%");
    } else {
        accent.AccentState = ACCENT_DISABLED;
        accent.GradientColor = 0;
        accent.AccentFlags = 0;

        CF_LOG(Info, "[" << windowType << "] Disabling transparency");
    }

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);

    BOOL result = m_setWindowCompositionAttribute(hwnd, &data);
    DWORD lastErr = GetLastError();
    CF_LOG(Info, "[" << windowType << "] SWCA result=" << result
                 << " HWND=0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec
                 << " GetLastError=" << std::dec << lastErr);

    // Iter#5: if SWCA failed and this is the 25H2+ taskbar path, fall back to LWA_ALPHA.
    if (!result && !isStartMenu && m_buildNumber >= 26200) {
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        if (accent.AccentState != ACCENT_DISABLED) {
            if (!(exStyle & WS_EX_LAYERED))
                SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            BYTE alpha = static_cast<BYTE>(((100 - opacity) * 255) / 100);
            SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
            CF_LOG(Info, "[TASKBAR] Win25H2+ SWCA failed — LWA_ALPHA fallback: alpha=" << (int)alpha);
        } else {
            if (exStyle & WS_EX_LAYERED)
                SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
        }
    }
}

void Renderer::RestoreWindow(HWND hwnd) {
    CF_LOG(Info, "RestoreWindow called for HWND 0x"
                 << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec);

    if (!hwnd || !IsWindow(hwnd)) {
        if (hwnd) CF_LOG(Warning, "Window handle is no longer valid, cannot restore");
        return;
    }

    // Task 4: fallback path — just remove the layered style.
    if (m_wcaUnavailable || !m_setWindowCompositionAttribute) {
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
        CF_LOG(Info, "RestoreWindow (fallback): removed WS_EX_LAYERED");
        return;
    }

    ACCENT_POLICY accent = {};
    accent.AccentState = ACCENT_DISABLED;
    accent.AccentFlags = 0;
    accent.GradientColor = 0;
    accent.AnimationId = 0;

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);

    BOOL result = m_setWindowCompositionAttribute(hwnd, &data);
    CF_LOG(Info, "RestoreWindow result: " << result
                 << " for HWND 0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec);

    // Clean up WS_EX_LAYERED that may have been added by the 24H2+ path.
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED)
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
}

void Renderer::RefreshTransparency() {
    // Reapply transparency to all taskbar windows to maintain the effect
    if (m_taskbarEnabled) {
        for (HWND h : m_hwndTaskbars) {
            if (h && IsWindow(h)) {
                ApplyTransparency(h, m_taskbarOpacity, true, m_taskbarBlur);
            }
        }
    }
    // Skip refreshing Start menu - Windows handles it; refreshing causes flicker
}

} // namespace GlassBar
