#include "Renderer.h"
#include "Diagnostics.h"
#include <algorithm>
#include <set>
#include <dwmapi.h>
#include <shlwapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

namespace GlassBar {

static std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), size, nullptr, nullptr);
    result.pop_back(); // drop the UTF-8 terminator written by WideCharToMultiByte
    return result;
}

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
    CF_LOG(Info, "Windows build number: " << m_buildNumber
                 << (m_buildNumber >= 26200 ? " (25H2+ -- overlay approach, SWCA inert)"
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
    ShutdownXamlBridge();
    DestroyAllOverlays();
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
    // Fix#3: if HWNDs unchanged, skip overlay destruction and just re-apply appearance.
    if (hwnds == m_hwndTaskbars) {
        for (HWND h : m_hwndTaskbars) {
            if (h) ApplyTransparency(h, m_taskbarOpacity, m_taskbarEnabled, m_taskbarBlur);
        }
        CF_LOG(Info, "Taskbar windows set (unchanged): count=" << m_hwndTaskbars.size());
        return;
    }
    DestroyAllOverlays();
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
    if (m_pSharedState) UpdateSharedState();
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
    if (m_pSharedState) UpdateSharedState();
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
    if (m_pSharedState) UpdateSharedState();
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
    if (m_pSharedState) UpdateSharedState();
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
    // falls through to the overlay + SWCA path below.
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

    // Iter#10: For 25H2+ (build >= 26200), the taskbar background is XAML-rendered.
    // All Win32/DWM approaches (LWA_ALPHA, DwmBlurBehind, SWCA cross- or in-process)
    // cannot affect the XAML Rectangle#BackgroundFill that paints over the DWM backdrop.
    //
    // XamlBridge.dll (injected into explorer.exe) uses InitializeXamlDiagnosticsEx to
    // register as a TAP (Taskbar Appearance Provider), receiving OnVisualTreeChange
    // callbacks and setting Fill directly on Rectangle#BackgroundFill from within the
    // XAML runtime — the only path that actually works (same mechanism as Windhawk).
    //
    // No overlay window needed: color/opacity are encoded in the SolidColorBrush ARGB,
    // and blur is handled via AcrylicBrush. Icons are unaffected (only Fill changes).
    if (!isStartMenu && m_buildNumber >= 26200) {
        // Only inject XamlBridge when transparency is actually being enabled.
        // Injecting on disable/restore calls is unnecessary and risks crashing Explorer
        // during shutdown or early startup before XAML islands are initialized.
        if (enabled && opacity > 0) {
            if (!m_bridgeInited) InitXamlBridge();
        }
        if (m_bridgeInited) UpdateSharedState();  // XamlBridge TAP reads shared state
        CF_LOG(Info, "[TASKBAR] Win25H2+ delegated to XamlBridge TAP (bridgeInited=" << m_bridgeInited << ")");
        return;
    }

    ACCENT_POLICY accent = {};

    if (enabled && opacity > 0) {
        if (m_buildNumber >= 26200 && !isStartMenu) {
            // On 25H2+ SWCA is a no-op for taskbar; ACRYLICBLURBEHIND avoids
            // TRANSPARENTGRADIENT returning result=1 with zero visual effect.
            accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            CF_LOG(Debug, "[" << windowType << "] Win25H2+ ACRYLICBLURBEHIND (diagnostic-only, SWCA inert)");
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

    SetLastError(0); // Fix#2: clear error from previous DWM calls so GetLastError reflects SWCA only
    BOOL result = m_setWindowCompositionAttribute(hwnd, &data);
    DWORD lastErr = GetLastError();
    CF_LOG(Info, "[" << windowType << "] SWCA result=" << result
                 << " HWND=0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec
                 << " GetLastError=" << std::dec << lastErr);
}

// ---------------------------------------------------------------------------
// Iter#7: Overlay window — color tint for 25H2+ taskbar
// ---------------------------------------------------------------------------

/*static*/ LRESULT CALLBACK Renderer::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_ERASEBKGND: {
            COLORREF color = (COLORREF)(ULONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, brush);
            DeleteObject(brush);
            return 1;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void Renderer::EnsureOverlayWindow(HWND taskbarHwnd) {
    // Register window class once per Renderer lifetime
    if (!m_overlayClassAtom) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GlassBarTaskbarOverlay25H2";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        m_overlayClassAtom = RegisterClassExW(&wc);
        if (!m_overlayClassAtom) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                CF_LOG(Error, "[TASKBAR] Overlay RegisterClassExW failed: " << err);
                return;
            }
            m_overlayClassAtom = 1; // already registered
        }
    }

    // Reposition existing valid overlay (TOPMOST above Shell_TrayWnd for color tint)
    auto it = m_overlayWindows.find(taskbarHwnd);
    if (it != m_overlayWindows.end()) {
        if (IsWindow(it->second)) {
            RECT rc;
            GetWindowRect(taskbarHwnd, &rc);
            SetWindowPos(it->second, HWND_TOPMOST,
                rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                SWP_NOACTIVATE);
            return;
        }
        m_overlayWindows.erase(it);
    }

    // Create overlay — TOPMOST above Shell_TrayWnd, click-through, for color tint
    RECT rc;
    GetWindowRect(taskbarHwnd, &rc);
    HWND overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"GlassBarTaskbarOverlay25H2",
        nullptr,
        WS_POPUP,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    if (!overlay) {
        CF_LOG(Error, "[TASKBAR] Overlay CreateWindowExW failed: " << GetLastError());
        return;
    }
    m_overlayWindows[taskbarHwnd] = overlay;
    SetWindowPos(overlay, HWND_TOPMOST,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        SWP_NOACTIVATE);
    CF_LOG(Info, "[TASKBAR] Overlay created HWND=0x" << std::hex
                 << reinterpret_cast<uintptr_t>(overlay)
                 << " for taskbar 0x" << reinterpret_cast<uintptr_t>(taskbarHwnd) << std::dec);
}

void Renderer::UpdateOverlayAppearance(HWND overlayHwnd, int r, int g, int b, int opacity, bool enabled) {
    if (!overlayHwnd || !IsWindow(overlayHwnd)) return;

    if (!enabled || opacity == 0 || (r == 0 && g == 0 && b == 0)) {
        ShowWindow(overlayHwnd, SW_HIDE);
        return;
    }

    SetWindowLongPtrW(overlayHwnd, GWLP_USERDATA, (LONG_PTR)RGB(r, g, b));
    // opacity=100 (fully transparent) -> colorAlpha=0 (no tint, just transparent bg).
    // opacity=50 -> colorAlpha=127 (semi-visible color tint above Shell_TrayWnd).
    BYTE colorAlpha = static_cast<BYTE>(((100 - opacity) * 255) / 100);
    SetLayeredWindowAttributes(overlayHwnd, 0, colorAlpha, LWA_ALPHA);
    ShowWindow(overlayHwnd, SW_SHOWNA);
    InvalidateRect(overlayHwnd, nullptr, TRUE);
    UpdateWindow(overlayHwnd);
    CF_LOG(Info, "[TASKBAR] Overlay color RGB(" << r << "," << g << "," << b
                 << ") alpha=" << (int)colorAlpha);
}

void Renderer::DestroyAllOverlays() {
    for (auto& [taskbar, overlay] : m_overlayWindows) {
        if (overlay && IsWindow(overlay))
            DestroyWindow(overlay);
    }
    m_overlayWindows.clear();
    CF_LOG(Info, "[TASKBAR] All overlays destroyed");
}

// ---------------------------------------------------------------------------

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

// ── XamlBridge integration ────────────────────────────────────────────────────

void Renderer::SetTaskbarBlurAmount(int amount) {
    amount = std::clamp(amount, 0, 100);
    m_blurAmount = amount;

    // On 22H2+ (22621+), activate XamlBridge injection for in-process SWCA.
    // On older builds SWCA works fine cross-process; no bridge needed.
    if (m_buildNumber >= 22621) {
        if (!m_bridgeInited) {
            InitXamlBridge();
        }
    }

    UpdateSharedState();
    CF_LOG(Info, "XamlBridge blur amount: " << amount);
}

void Renderer::InitXamlBridge() {
    if (m_bridgeInited) return;
    // NOTE: do NOT set m_bridgeInited here — only mark success after hook is installed.
    // Setting it early causes silent failure: subsequent calls skip the bridge but it
    // was never actually initialized. Instead each early-return below leaves it false
    // so the next ApplyTransparency call can retry.

    CF_LOG(Info, "XamlBridge: initializing (build " << m_buildNumber << ")");

    // ── 1. Create shared memory ──────────────────────────────────────────
    m_hSharedMem = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(SharedBlurState),
        SharedBlurState::kName);

    if (!m_hSharedMem) {
        CF_LOG(Error, "XamlBridge: CreateFileMapping failed: " << GetLastError());
        return;
    }

    m_pSharedState = reinterpret_cast<SharedBlurState*>(
        MapViewOfFile(m_hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlurState)));

    if (!m_pSharedState) {
        CF_LOG(Error, "XamlBridge: MapViewOfFile failed: " << GetLastError());
        CloseHandle(m_hSharedMem);
        m_hSharedMem = nullptr;
        return;
    }

    ZeroMemory(m_pSharedState, sizeof(SharedBlurState));

    // ── 2. Load GlassBar.XamlBridge.dll from same directory as Core ────
    wchar_t corePath[MAX_PATH] = {};
    GetModuleFileNameW(
        GetModuleHandleW(L"GlassBar.Core.dll"), corePath, MAX_PATH);
    PathRemoveFileSpecW(corePath);
    PathAppendW(corePath, L"GlassBar.XamlBridge.dll");

    m_hXamlBridge = LoadLibraryExW(corePath, nullptr, 0);
    if (!m_hXamlBridge) {
        CF_LOG(Error, "XamlBridge: LoadLibrary failed for '" << WideToUtf8(corePath)
                      << "' err=" << GetLastError());
        return;
    }

    // ── 3. Find exported hook proc ───────────────────────────────────────
    auto hookProc = reinterpret_cast<HOOKPROC>(
        GetProcAddress(m_hXamlBridge, "XamlBridgeHookProc"));

    if (!hookProc) {
        CF_LOG(Error, "XamlBridge: XamlBridgeHookProc export not found");
        return;
    }

    // ── 4. Install WH_CALLWNDPROC hooks on ALL explorer UI threads ──────
    //
    // On Windows 25H2, the taskbar XAML island containing BackgroundFill may be
    // owned by a thread OTHER than Shell_TrayWnd's thread.  InitializeXamlDiagnosticsEx
    // must be called from the island's owning thread, so we install one hook per
    // unique explorer thread — each hook fires on its own thread when that thread
    // receives a sent message.
    HWND hwndTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hwndTray) {
        CF_LOG(Warning, "XamlBridge: Shell_TrayWnd not found — injection deferred");
        return;
    }

    DWORD explorerPid = 0;
    GetWindowThreadProcessId(hwndTray, &explorerPid);

    struct EnumCtx {
        DWORD     pid;
        HOOKPROC  proc;
        HMODULE   mod;
        std::set<DWORD>    seen;
        std::vector<HHOOK> hooks;
    };
    EnumCtx ctx{ explorerPid, hookProc, m_hXamlBridge, {}, {} };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<EnumCtx*>(lp);
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        if (pid == c->pid && c->seen.insert(tid).second) {
            HHOOK h = SetWindowsHookExW(WH_CALLWNDPROC, c->proc, c->mod, tid);
            if (h) c->hooks.push_back(h);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    m_hInjHooks = std::move(ctx.hooks);

    if (m_hInjHooks.empty()) {
        CF_LOG(Error, "XamlBridge: no hooks installed (SetWindowsHookEx failed for all explorer threads)");
        return;
    }

    // ── 5. Mark bridge as initialized — hooks installed successfully ──────
    m_bridgeInited = true;

    CF_LOG(Info, "XamlBridge: hook installed (" << m_hInjHooks.size()
                 << " explorer thread(s)) — will activate on next sent message");
}

void Renderer::UpdateSharedState() {
    if (!m_pSharedState) return;

    // Bump version to signal change to worker thread
    InterlockedIncrement(const_cast<volatile LONG*>(&m_pSharedState->version));

    // Signal XamlBridge to apply transparency whenever taskbar effect is active.
    // blurAmount=0 → XamlBridge uses TRANSPARENTGRADIENT; blurAmount>0 → ACRYLICBLUR.
    bool transOn = m_taskbarEnabled && (m_taskbarOpacity > 0);

    InterlockedExchange(const_cast<volatile LONG*>(&m_pSharedState->blurEnabled),
                        transOn ? 1 : 0);
    InterlockedExchange(const_cast<volatile LONG*>(&m_pSharedState->opacityPct),
                        static_cast<LONG>(m_taskbarOpacity));
    InterlockedExchange(const_cast<volatile LONG*>(&m_pSharedState->colorR),
                        static_cast<LONG>(m_taskbarColorR));
    InterlockedExchange(const_cast<volatile LONG*>(&m_pSharedState->colorG),
                        static_cast<LONG>(m_taskbarColorG));
    InterlockedExchange(const_cast<volatile LONG*>(&m_pSharedState->colorB),
                        static_cast<LONG>(m_taskbarColorB));
    InterlockedExchange(const_cast<volatile LONG*>(&m_pSharedState->blurAmount),
                        static_cast<LONG>(m_blurAmount));

    InterlockedIncrement(const_cast<volatile LONG*>(&m_pSharedState->version));
}

void Renderer::ShutdownXamlBridge() {
    if (m_pSharedState) {
        // Signal worker thread to stop
        InterlockedExchange(
            const_cast<volatile LONG*>(&m_pSharedState->shutdownRequest), 1);
        // Give it time to restore taskbar appearance
        Sleep(300);
    }

    for (HHOOK h : m_hInjHooks) {
        if (h) UnhookWindowsHookEx(h);
    }
    m_hInjHooks.clear();

    if (m_pSharedState) {
        UnmapViewOfFile(m_pSharedState);
        m_pSharedState = nullptr;
    }

    if (m_hSharedMem) {
        CloseHandle(m_hSharedMem);
        m_hSharedMem = nullptr;
    }

    // Note: m_hXamlBridge is intentionally NOT freed with FreeLibrary here.
    // The DLL is still loaded in explorer.exe's address space. The worker thread
    // in explorer will shut down after reading shutdownRequest=1 from shared mem
    // and the hook being removed. Calling FreeLibrary on our side only removes
    // our reference; explorer's reference persists until explorer restarts.
    m_hXamlBridge = nullptr;

    m_bridgeInited = false;

    CF_LOG(Info, "XamlBridge shutdown complete");
}

} // namespace GlassBar
