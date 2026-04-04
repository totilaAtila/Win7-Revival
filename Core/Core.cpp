#include "Core.h"
#include "Diagnostics.h"
#include <algorithm>

namespace GlassBar {

// Transparency refresh interval
constexpr UINT REFRESH_INTERVAL_MS = 100;

Core::Core() {
}

Core::~Core() {
    Shutdown();
}

bool Core::Initialize() {
    CF_LOG(Info, "=== GlassBar Core Initialization ===");

    // Create modules
    m_config = std::make_unique<ConfigManager>();
    m_locator = std::make_unique<ShellTargetLocator>();
    m_renderer = std::make_unique<Renderer>();

    // Initialize config
    if (!m_config->Initialize()) {
        CF_LOG(Error, "ConfigManager initialization failed");
        return false;
    }

    Config config = m_config->GetConfig();
    CF_LOG(Info, "Startup config snapshot: Taskbar enabled=" << (config.taskbarEnabled ? "true" : "false")
                 << " opacity=" << config.taskbarOpacity
                 << " RGB=(" << config.taskbarColorR << ", " << config.taskbarColorG << ", " << config.taskbarColorB << ")"
                 << " blur=" << (config.taskbarBlur ? "true" : "false")
                 << " amount=" << config.blurAmount
                 << " | Start enabled=" << (config.startEnabled ? "true" : "false")
                 << " opacity=" << config.startOpacity
                 << " blur=" << (config.startBlur ? "true" : "false"));

    // Store config values
    m_taskbarOpacity = config.taskbarOpacity;
    m_startOpacity = config.startOpacity;
    m_taskbarEnabled = config.taskbarEnabled;
    m_startEnabled = config.startEnabled;
    m_taskbarBlur = config.taskbarBlur;
    m_startBlur = config.startBlur;
    m_taskbarColorR = config.taskbarColorR;
    m_taskbarColorG = config.taskbarColorG;
    m_taskbarColorB = config.taskbarColorB;
    m_startBgColor = RGB(config.startBgColorR, config.startBgColorG, config.startBgColorB);
    m_startTextColor = RGB(config.startTextColorR, config.startTextColorG, config.startTextColorB);
    m_startBorderColor = RGB(config.startBorderColorR, config.startBorderColorG, config.startBorderColorB);
    m_startShowControlPanel = config.startShowControlPanel;
    m_startShowDeviceManager = config.startShowDeviceManager;
    m_startShowInstalledApps = config.startShowInstalledApps;
    m_startShowDocuments = config.startShowDocuments;
    m_startShowPictures = config.startShowPictures;
    m_startShowVideos = config.startShowVideos;
    m_startShowRecentFiles = config.startShowRecentFiles;

    // Load blur amount
    m_blurAmount = config.blurAmount;

    // Schedule hotkey registration (will be picked up on first ProcessMessages call)
    if (config.hotkeyVk != 0) {
        m_pendingHotkeyVk.store(config.hotkeyVk);
        m_pendingHotkeyMod.store(config.hotkeyModifiers);
        m_hotkeyPending.store(true);
    }

    // Initialize renderer (loads SetWindowCompositionAttribute)
    if (!m_renderer->Initialize()) {
        CF_LOG(Error, "Renderer initialization failed");
        return false;
    }

    // Apply config to renderer
    m_renderer->SetTaskbarOpacity(m_taskbarOpacity);
    m_renderer->SetStartOpacity(m_startOpacity);
    m_renderer->SetTaskbarEnabled(m_taskbarEnabled);
    m_renderer->SetStartEnabled(m_startEnabled);
    m_renderer->SetTaskbarColor(m_taskbarColorR, m_taskbarColorG, m_taskbarColorB);
    m_renderer->SetTaskbarBlur(m_taskbarBlur);
    m_renderer->SetStartBlur(m_startBlur);
    m_renderer->SetTaskbarBlurAmount(m_blurAmount);

    // Initialize shell target locator (finds taskbar/start windows)
    if (!m_locator->Initialize(this)) {
        CF_LOG(Error, "ShellTargetLocator initialization failed");
        return false;
    }

    // Initialize custom Start Menu window first (can take several seconds due to
    // SHGetFileInfoW icon loading). Hooks are installed AFTER this completes so
    // that WH_KEYBOARD_LL / WH_MOUSE_LL are never installed on a busy thread —
    // a blocked hook thread causes Windows to time out the hooks and stutter the
    // mouse cursor for the entire duration of initialization.
    m_startMenuWindow = std::make_unique<StartMenuWindow>();
    if (!m_startMenuWindow->Initialize()) {
        CF_LOG(Error, "StartMenuWindow initialization failed");
        return false;
    }
    m_startMenuWindow->SetOpacity(m_startOpacity);
    m_startMenuWindow->SetBackgroundColor(m_startBgColor);
    m_startMenuWindow->SetTextColor(m_startTextColor);
    m_startMenuWindow->SetMenuItems(m_startShowControlPanel, m_startShowDeviceManager, m_startShowInstalledApps,
                                    m_startShowDocuments, m_startShowPictures, m_startShowVideos, m_startShowRecentFiles);
    m_startMenuWindow->SetBorderColor(m_startBorderColor);
    m_startMenuWindow->SetBlur(m_startBlur);
    CF_LOG(Info, "Start Menu config applied from Core mirror: bg=0x" << std::hex << m_startBgColor
                 << " text=0x" << m_startTextColor
                 << " border=0x" << m_startBorderColor
                 << std::dec << " items=[CP=" << (m_startShowControlPanel ? "1" : "0")
                 << " DM=" << (m_startShowDeviceManager ? "1" : "0")
                 << " IA=" << (m_startShowInstalledApps ? "1" : "0")
                 << " D=" << (m_startShowDocuments ? "1" : "0")
                 << " P=" << (m_startShowPictures ? "1" : "0")
                 << " V=" << (m_startShowVideos ? "1" : "0")
                 << " RF=" << (m_startShowRecentFiles ? "1" : "0") << "]");

    // Initialize Start Menu Hook (intercepts Windows key and Start button clicks).
    // Must be installed AFTER StartMenuWindow::Initialize() so the hook thread is
    // free to service its message queue immediately upon installation.
    m_startMenuHook = std::make_unique<StartMenuHook>();
    if (!m_startMenuHook->Initialize()) {
        CF_LOG(Error, "StartMenuHook initialization failed");
        return false;
    }

    // Set callbacks for Start Menu hook.
    // IMPORTANT: these lambdas run on the low-level hook thread. They must return
    // in microseconds — any real work is posted to the UI thread via PostMessage.
    m_startMenuHook->SetShowMenuCallback([this](int x, int y) {
        HWND hwnd = m_startMenuWindow ? m_startMenuWindow->GetMenuHwnd() : nullptr;
        if (hwnd) PostMessage(hwnd, StartMenuWindow::WM_APP_SHOW_MENU,
                              static_cast<WPARAM>(x), static_cast<LPARAM>(y));
    });

    m_startMenuHook->SetHideMenuCallback([this]() {
        HWND hwnd = m_startMenuWindow ? m_startMenuWindow->GetMenuHwnd() : nullptr;
        if (hwnd) PostMessage(hwnd, StartMenuWindow::WM_APP_HIDE_MENU, 0, 0);
    });

    m_startMenuHook->SetIsMenuVisibleCallback([this]() -> bool {
        return m_startMenuWindow && m_startMenuWindow->IsVisible();
    });

    m_startMenuHook->SetGetMenuBoundsCallback([this]() -> RECT {
        if (m_startMenuWindow) {
            return m_startMenuWindow->GetWindowBounds();
        }
        return RECT{};
    });

    // Forward nav/dismiss keys (Up/Down/Enter/Esc) to the non-activating Start Menu window.
    // The window never receives keyboard focus (WS_EX_NOACTIVATE), so we PostMessage from
    // the low-level hook instead of relying on normal focus-based WM_KEYDOWN delivery.
    m_startMenuHook->SetForwardKeyCallback([this](UINT vk) {
        if (m_startMenuWindow && m_startMenuWindow->IsVisible()) {
            PostMessage(m_startMenuWindow->GetMenuHwnd(), WM_KEYDOWN, vk, 0);
        }
    });

    // Disabled by default - Dashboard will enable when configured
    m_startMenuHook->SetEnabled(false);

    m_lastRefreshTick = GetTickCount64();
    m_lastDetectTick  = GetTickCount64();

    CF_LOG(Info, "=== GlassBar Core Ready ===");

    m_running = true;
    return true;
}

bool Core::ProcessMessages() {
    if (!m_running) {
        return false;
    }

    MSG msg = {};

    // Apply pending hotkey registration on this thread (RegisterHotKey is thread-affine)
    if (m_hotkeyPending.exchange(false)) {
        UnregisterHotKey(nullptr, HOTKEY_ID);
        int vk  = m_pendingHotkeyVk.load();
        int mod = m_pendingHotkeyMod.load();
        if (vk != 0) {
            if (!RegisterHotKey(nullptr, HOTKEY_ID, static_cast<UINT>(mod), static_cast<UINT>(vk))) {
                CF_LOG(Warning, "RegisterHotKey failed: vk=0x" << std::hex << vk
                                << " mod=0x" << mod << std::dec
                                << " err=" << GetLastError());
            } else {
                CF_LOG(Info, "Hotkey registered: vk=0x" << std::hex << vk
                             << " mod=0x" << mod << std::dec);
            }
        }
    }

    // Process all available messages (non-blocking)
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_running = false;
            return false;
        }
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            // Toggle taskbar overlay on/off
            m_taskbarEnabled = !m_taskbarEnabled;
            if (m_renderer) m_renderer->SetTaskbarEnabled(m_taskbarEnabled);
            if (m_config)   m_config->SetTaskbarEnabled(m_taskbarEnabled);
            CF_LOG(Info, "Hotkey: taskbar toggled " << (m_taskbarEnabled ? "ON" : "OFF"));
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ULONGLONG now = GetTickCount64();

    // Refresh transparency every ~100ms (called on background thread — fixes thread-affinity
    // issue with the old SetTimer approach which fired on UI thread but was processed here)
    if (now - m_lastRefreshTick >= REFRESH_INTERVAL_MS) {
        m_lastRefreshTick = now;
        RefreshTransparency();
    }

    // Re-detect taskbar every 2s to catch alignment / position changes that reset SWCA
    constexpr ULONGLONG REDETECT_INTERVAL_MS = 2000;
    if (now - m_lastDetectTick >= REDETECT_INTERVAL_MS) {
        m_lastDetectTick = now;
        if (m_locator) m_locator->RefreshTaskbar();
    }

    return true;
}

void Core::Shutdown() {
    if (!m_running) {
        return; // Already shut down
    }

    CF_LOG(Info, "Core shutdown initiated");

    m_running = false;

    // Reset all modules - destructors call their own Shutdown()
    m_startMenuWindow.reset();
    m_startMenuHook.reset();
    m_locator.reset();
    m_renderer.reset();
    m_config.reset();

    CF_LOG(Info, "Core shutdown complete");
}

// IShellTargetCallback interface implementation
void Core::OnTaskbarChanged(const TaskbarInfo& info) {
    // Delegated to by OnTaskbarsChanged default impl — handle single-monitor case directly
    CF_LOG(Info, "Taskbar found (single) - HWND: 0x"
                 << std::hex << reinterpret_cast<uintptr_t>(info.hwnd) << std::dec);

    m_taskbarFound = info.found;
    if (m_renderer && info.hwnd) {
        m_renderer->SetTaskbarWindow(info.hwnd);
    }
}

void Core::OnTaskbarsChanged(const std::vector<TaskbarInfo>& infos) {
    CF_LOG(Info, "Taskbars changed: count=" << infos.size());

    if (infos.empty()) {
        m_taskbarFound = false;
        return;
    }

    m_taskbarFound = true;

    // Collect valid HWNDs from all detected taskbars
    std::vector<HWND> hwnds;
    for (const auto& info : infos) {
        if (info.hwnd && info.found) {
            hwnds.push_back(info.hwnd);
        }
    }

    if (m_renderer && !hwnds.empty()) {
        m_renderer->SetTaskbarWindows(hwnds);
    }
}

void Core::OnStartShown(const StartInfo& info) {
    CF_LOG(Debug, "Start menu shown - HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(info.hwnd) << std::dec);

    m_startDetected = true;
    m_renderer->SetStartWindow(info.hwnd);
}

void Core::OnStartHidden() {
    CF_LOG(Debug, "Start menu hidden");
}

void Core::OnStartDetectionFailed() {
    CF_LOG(Warning, "Start menu detection failed - disabling Start transparency");
    m_startDetected = false;
}

// Public API implementation
void Core::SetTaskbarOpacity(int opacity) {
    m_taskbarOpacity = opacity;

    if (m_renderer) {
        m_renderer->SetTaskbarOpacity(opacity);
    }

    if (m_config) {
        m_config->SetTaskbarOpacity(opacity);
    }
    CF_LOG(Info, "Taskbar opacity set to " << opacity << "%");
}

void Core::SetStartOpacity(int opacity) {
    m_startOpacity = opacity;

    if (m_renderer) {
        m_renderer->SetStartOpacity(opacity);
    }

    if (m_config) {
        m_config->SetStartOpacity(opacity);
    }
    CF_LOG(Info, "Start opacity set to " << opacity << "%");
}

void Core::SetTaskbarEnabled(bool enabled) {
    m_taskbarEnabled = enabled;

    if (m_renderer) {
        m_renderer->SetTaskbarEnabled(enabled);
    }

    if (m_config) {
        m_config->SetTaskbarEnabled(enabled);
    }
    CF_LOG(Info, "Taskbar transparency " << (enabled ? "enabled" : "disabled"));
}

void Core::SetStartEnabled(bool enabled) {
    m_startEnabled = enabled;

    if (m_renderer) {
        m_renderer->SetStartEnabled(enabled);
    }

    if (m_config) {
        m_config->SetStartEnabled(enabled);
    }
    CF_LOG(Info, "Start transparency " << (enabled ? "enabled" : "disabled"));
}

void Core::SetTaskbarColor(int r, int g, int b) {
    m_taskbarColorR = std::clamp(r, 0, 255);
    m_taskbarColorG = std::clamp(g, 0, 255);
    m_taskbarColorB = std::clamp(b, 0, 255);
    if (m_renderer) {
        m_renderer->SetTaskbarColor(m_taskbarColorR, m_taskbarColorG, m_taskbarColorB);
    }
    if (m_config) {
        m_config->SetTaskbarColor(m_taskbarColorR, m_taskbarColorG, m_taskbarColorB);
    }
    CF_LOG(Info, "Taskbar color set to RGB(" << m_taskbarColorR << ", " << m_taskbarColorG << ", " << m_taskbarColorB << ")");
}

void Core::SetTaskbarBlur(bool enabled) {
    m_taskbarBlur = enabled;
    if (m_renderer) {
        m_renderer->SetTaskbarBlur(enabled);
    }
    if (m_config) {
        m_config->SetTaskbarBlur(enabled);
    }
    CF_LOG(Info, "Taskbar blur " << (enabled ? "enabled" : "disabled"));
}

void Core::SetStartBlur(bool enabled) {
    m_startBlur = enabled;
    if (m_renderer) {
        m_renderer->SetStartBlur(enabled);
    }
    if (m_config) {
        m_config->SetStartBlur(enabled);
    }
    // S15 blur fix: also apply to the custom StartMenuWindow (was missing before)
    if (m_startMenuWindow) {
        m_startMenuWindow->SetBlur(enabled);
    }
    CF_LOG(Info, "Start blur " << (enabled ? "enabled" : "disabled"));
}

void Core::SetStartMenuHookEnabled(bool enabled) {
    if (m_startMenuHook) {
        m_startMenuHook->SetEnabled(enabled);
        CF_LOG(Info, "Custom Start Menu hook " << (enabled ? "ENABLED" : "DISABLED"));
    }
}

void Core::SetStartMenuOpacity(int opacity) {
    m_startOpacity = opacity;
    if (m_config) {
        m_config->SetStartOpacity(opacity);
    }
    if (m_startMenuWindow) {
        m_startMenuWindow->SetOpacity(opacity);
        CF_LOG(Info, "Start Menu opacity set to " << opacity << "%");
    }
}

void Core::SetStartMenuBackgroundColor(DWORD rgb) {
    m_startBgColor = static_cast<COLORREF>(rgb);
    if (m_config) {
        m_config->SetStartMenuBackgroundColor(GetRValue(m_startBgColor), GetGValue(m_startBgColor), GetBValue(m_startBgColor));
    }
    if (m_startMenuWindow) {
        m_startMenuWindow->SetBackgroundColor(m_startBgColor);
        CF_LOG(Info, "Start Menu background color set to 0x" << std::hex << rgb << std::dec);
    }
}

void Core::SetStartMenuTextColor(DWORD rgb) {
    m_startTextColor = static_cast<COLORREF>(rgb);
    if (m_config) {
        m_config->SetStartMenuTextColor(GetRValue(m_startTextColor), GetGValue(m_startTextColor), GetBValue(m_startTextColor));
    }
    if (m_startMenuWindow) {
        m_startMenuWindow->SetTextColor(m_startTextColor);
        CF_LOG(Info, "Start Menu text color set to 0x" << std::hex << rgb << std::dec);
    }
}

void Core::SetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                             bool documents, bool pictures, bool videos, bool recentFiles) {
    m_startShowControlPanel = controlPanel;
    m_startShowDeviceManager = deviceManager;
    m_startShowInstalledApps = installedApps;
    m_startShowDocuments = documents;
    m_startShowPictures = pictures;
    m_startShowVideos = videos;
    m_startShowRecentFiles = recentFiles;
    if (m_config) {
        m_config->SetStartMenuItems(controlPanel, deviceManager, installedApps, documents, pictures, videos, recentFiles);
    }
    if (m_startMenuWindow) {
        m_startMenuWindow->SetMenuItems(controlPanel, deviceManager, installedApps,
                                        documents, pictures, videos, recentFiles);
        CF_LOG(Info, "Start Menu items updated: CP=" << (controlPanel ? "1" : "0")
                     << " DM=" << (deviceManager ? "1" : "0")
                     << " IA=" << (installedApps ? "1" : "0")
                     << " D=" << (documents ? "1" : "0")
                     << " P=" << (pictures ? "1" : "0")
                     << " V=" << (videos ? "1" : "0")
                     << " RF=" << (recentFiles ? "1" : "0"));
    }
}

void Core::OnCustomStartMenuRequested(int x, int y) {
    CF_LOG(Info, "Custom Start Menu requested at (" << x << ", " << y << ")");

    if (m_startMenuWindow) {
        if (m_startMenuWindow->IsVisible()) {
            // Already visible - toggle off
            m_startMenuWindow->Hide();
        } else {
            // Show the custom Start Menu
            m_startMenuWindow->Show(x, y);
        }
    }
}

void Core::RefreshTransparency() {
    if (m_renderer) {
        m_renderer->RefreshTransparency();
    }
}

void Core::SetStartMenuPinned(bool pinned) {
    if (m_startMenuWindow) {
        m_startMenuWindow->SetPinned(pinned);
        CF_LOG(Info, "Start Menu " << (pinned ? "pinned open" : "unpinned"));
    }
}

void Core::SetStartMenuBorderColor(DWORD rgb) {
    m_startBorderColor = static_cast<COLORREF>(rgb);
    if (m_config) {
        m_config->SetStartMenuBorderColor(GetRValue(m_startBorderColor), GetGValue(m_startBorderColor), GetBValue(m_startBorderColor));
    }
    if (m_startMenuWindow) {
        m_startMenuWindow->SetBorderColor(m_startBorderColor);
        CF_LOG(Info, "Start Menu border color set to 0x" << std::hex << rgb << std::dec);
    }
}

void Core::SetTaskbarBlurAmount(int amount) {
    m_blurAmount = std::clamp(amount, 0, 100);
    if (m_renderer) m_renderer->SetTaskbarBlurAmount(m_blurAmount);
    if (m_config) m_config->SetBlurAmount(m_blurAmount);
    CF_LOG(Info, "Taskbar blur amount set to " << m_blurAmount);
}

void Core::RegisterHotkey(int vk, int modifiers) {
    m_pendingHotkeyVk.store(vk);
    m_pendingHotkeyMod.store(modifiers);
    m_hotkeyPending.store(true);  // picked up by ProcessMessages on pump thread
    if (m_config) {
        m_config->SetHotkey(vk, modifiers);
        m_config->Save();
    }
    CF_LOG(Info, "Hotkey scheduled: vk=0x" << std::hex << vk
                 << " mod=0x" << modifiers << std::dec);
}

void Core::UnregisterHotkey() {
    m_pendingHotkeyVk.store(0);
    m_pendingHotkeyMod.store(0);
    m_hotkeyPending.store(true);  // vk=0 → UnregisterHotKey only
    if (m_config) {
        m_config->SetHotkey(0, 0);
        m_config->Save();
    }
    CF_LOG(Info, "Hotkey unregistration scheduled");
}

} // namespace GlassBar
