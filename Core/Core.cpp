#include "Core.h"
#include "Diagnostics.h"

namespace CrystalFrame {

// Transparency refresh interval
constexpr UINT REFRESH_INTERVAL_MS = 100;

Core::Core() {
}

Core::~Core() {
    Shutdown();
}

bool Core::Initialize() {
    CF_LOG(Info, "=== CrystalFrame Core Initialization ===");

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
    CF_LOG(Info, "Config loaded: Taskbar=" << config.taskbarOpacity
                 << "%, Start=" << config.startOpacity << "%");

    // Store config values
    m_taskbarOpacity = config.taskbarOpacity;
    m_startOpacity = config.startOpacity;
    m_taskbarEnabled = config.taskbarEnabled;
    m_startEnabled = config.startEnabled;
    m_taskbarBlur = config.taskbarBlur;
    m_startBlur = config.startBlur;

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
    m_renderer->SetTaskbarBlur(m_taskbarBlur);
    m_renderer->SetStartBlur(m_startBlur);

    // Initialize shell target locator (finds taskbar/start windows)
    if (!m_locator->Initialize(this)) {
        CF_LOG(Error, "ShellTargetLocator initialization failed");
        return false;
    }

    // Initialize Start Menu Hook (intercepts Windows key and Start button clicks)
    m_startMenuHook = std::make_unique<StartMenuHook>();
    if (!m_startMenuHook->Initialize()) {
        CF_LOG(Error, "StartMenuHook initialization failed");
        return false;
    }

    // Set callbacks for Start Menu hook
    m_startMenuHook->SetShowMenuCallback([this](int x, int y) {
        OnCustomStartMenuRequested(x, y);
    });

    m_startMenuHook->SetHideMenuCallback([this]() {
        if (m_startMenuWindow) {
            m_startMenuWindow->Hide();
        }
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

    // Disabled by default - Dashboard will enable when configured
    m_startMenuHook->SetEnabled(false);

    // Initialize custom Start Menu window
    m_startMenuWindow = std::make_unique<StartMenuWindow>();
    if (!m_startMenuWindow->Initialize()) {
        CF_LOG(Error, "StartMenuWindow initialization failed");
        return false;
    }

    // Create refresh timer (store ID so it can be killed in Shutdown)
    m_timerId = SetTimer(nullptr, 0, REFRESH_INTERVAL_MS, nullptr);
    CF_LOG(Info, "Refresh timer created with ID: " << m_timerId);

    CF_LOG(Info, "=== CrystalFrame Core Ready ===");

    m_running = true;
    return true;
}

bool Core::ProcessMessages() {
    if (!m_running) {
        return false;
    }

    MSG msg = {};

    // Process all available messages (non-blocking)
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_running = false;
            return false;
        }

        if (msg.message == WM_TIMER && msg.hwnd == nullptr) {
            // Thread timer (no window) - handle manually, skip DispatchMessage
            RefreshTransparency();
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}

void Core::Shutdown() {
    if (!m_running && !m_timerId) {
        return; // Already shut down
    }

    CF_LOG(Info, "Core shutdown initiated");

    m_running = false;

    // Kill the refresh timer first
    if (m_timerId) {
        KillTimer(nullptr, m_timerId);
        m_timerId = 0;
    }

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
}

void Core::SetStartOpacity(int opacity) {
    m_startOpacity = opacity;

    if (m_renderer) {
        m_renderer->SetStartOpacity(opacity);
    }

    if (m_config) {
        m_config->SetStartOpacity(opacity);
    }
}

void Core::SetTaskbarEnabled(bool enabled) {
    m_taskbarEnabled = enabled;

    if (m_renderer) {
        m_renderer->SetTaskbarEnabled(enabled);
    }

    if (m_config) {
        m_config->SetTaskbarEnabled(enabled);
    }
}

void Core::SetStartEnabled(bool enabled) {
    m_startEnabled = enabled;

    if (m_renderer) {
        m_renderer->SetStartEnabled(enabled);
    }

    if (m_config) {
        m_config->SetStartEnabled(enabled);
    }
}

void Core::SetTaskbarColor(int r, int g, int b) {
    if (m_renderer) {
        m_renderer->SetTaskbarColor(r, g, b);
    }
    CF_LOG(Info, "Taskbar color set to RGB(" << r << ", " << g << ", " << b << ")");
}

void Core::SetTaskbarBlur(bool enabled) {
    m_taskbarBlur = enabled;
    if (m_renderer) {
        m_renderer->SetTaskbarBlur(enabled);
    }
    if (m_config) {
        m_config->SetTaskbarBlur(enabled);
    }
}

void Core::SetStartBlur(bool enabled) {
    m_startBlur = enabled;
    if (m_renderer) {
        m_renderer->SetStartBlur(enabled);
    }
    if (m_config) {
        m_config->SetStartBlur(enabled);
    }
}

void Core::SetStartMenuHookEnabled(bool enabled) {
    if (m_startMenuHook) {
        m_startMenuHook->SetEnabled(enabled);
        CF_LOG(Info, "Custom Start Menu hook " << (enabled ? "ENABLED" : "DISABLED"));
    }
}

void Core::SetStartMenuOpacity(int opacity) {
    if (m_startMenuWindow) {
        m_startMenuWindow->SetOpacity(opacity);
        CF_LOG(Info, "Start Menu opacity set to " << opacity << "%");
    }
}

void Core::SetStartMenuBackgroundColor(DWORD rgb) {
    if (m_startMenuWindow) {
        m_startMenuWindow->SetBackgroundColor(static_cast<COLORREF>(rgb));
        CF_LOG(Info, "Start Menu background color set to 0x" << std::hex << rgb << std::dec);
    }
}

void Core::SetStartMenuTextColor(DWORD rgb) {
    if (m_startMenuWindow) {
        m_startMenuWindow->SetTextColor(static_cast<COLORREF>(rgb));
        CF_LOG(Info, "Start Menu text color set to 0x" << std::hex << rgb << std::dec);
    }
}

void Core::SetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                             bool documents, bool pictures, bool videos, bool recentFiles) {
    if (m_startMenuWindow) {
        m_startMenuWindow->SetMenuItems(controlPanel, deviceManager, installedApps,
                                        documents, pictures, videos, recentFiles);
        CF_LOG(Info, "Start Menu items updated");
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

} // namespace CrystalFrame
