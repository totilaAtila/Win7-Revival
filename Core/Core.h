#pragma once
#include <Windows.h>
#include <atomic>
#include <memory>
#include "ConfigManager.h"
#include "ShellTargetLocator.h"
#include "Renderer.h"
#include "StartMenuHook.h"
#include "StartMenuWindow.h"

namespace GlassBar {

class Core : public IShellTargetCallback {
public:
    Core();
    ~Core();

    bool Initialize();
    bool ProcessMessages();  // Process one message, return false to quit
    void Shutdown();

    // IShellTargetCallback interface
    void OnTaskbarChanged(const TaskbarInfo& info) override;
    void OnTaskbarsChanged(const std::vector<TaskbarInfo>& infos) override;
    void OnStartShown(const StartInfo& info) override;
    void OnStartHidden() override;
    void OnStartDetectionFailed() override;

    // Public API for Dashboard
    void SetTaskbarOpacity(int opacity);
    void SetStartOpacity(int opacity);
    void SetTaskbarEnabled(bool enabled);
    void SetStartEnabled(bool enabled);
    void SetTaskbarColor(int r, int g, int b);
    void SetTaskbarBlur(bool enabled);
    void SetStartBlur(bool enabled);

    // XamlBridge: blur amount 0-100 (0 = off, 1-100 = intensity)
    void SetTaskbarBlurAmount(int amount);
    void SetStartMenuHookEnabled(bool enabled);
    void SetStartMenuOpacity(int opacity);
    void SetStartMenuBackgroundColor(DWORD rgb);
    void SetStartMenuTextColor(DWORD rgb);
    void SetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                           bool documents, bool pictures, bool videos, bool recentFiles);

    // S-B: keep Start Menu visible while Dashboard preview toggle is active
    void SetStartMenuPinned(bool pinned);

    // S-E: explicit border/accent color
    void SetStartMenuBorderColor(DWORD rgb);

    // Global hotkey toggle (thread-safe: actual RegisterHotKey done on message pump thread)
    void RegisterHotkey(int vk, int modifiers);
    void UnregisterHotkey();

    // Getters for status
    bool GetTaskbarFound() const { return m_taskbarFound; }
    bool GetTaskbarEnabled() const { return m_taskbarEnabled; }
    int GetTaskbarOpacity() const { return m_taskbarOpacity; }

    bool GetStartDetected() const { return m_startDetected; }
    bool GetStartEnabled() const { return m_startEnabled; }
    int GetStartOpacity() const { return m_startOpacity; }

private:
    static constexpr int HOTKEY_ID = 42;   // arbitrary ID for WM_HOTKEY

    // Hotkey registration is deferred to the message-pump thread via atomics.
    std::atomic<bool> m_hotkeyPending{false};
    std::atomic<int>  m_pendingHotkeyVk{0};
    std::atomic<int>  m_pendingHotkeyMod{0};

    bool m_running = false;
    ULONGLONG m_lastRefreshTick = 0;
    ULONGLONG m_lastDetectTick  = 0;
    bool m_taskbarFound = false;
    bool m_startDetected = false;
    bool m_taskbarEnabled = true;
    bool m_startEnabled = true;
    bool m_taskbarBlur = false;
    bool m_startBlur = false;
    int  m_blurAmount = 0;
    int m_taskbarOpacity = 75;
    int m_startOpacity = 50;

    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<ShellTargetLocator> m_locator;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<StartMenuHook> m_startMenuHook;
    std::unique_ptr<StartMenuWindow> m_startMenuWindow;

    void RefreshTransparency();
    void OnCustomStartMenuRequested(int x, int y);
};

} // namespace GlassBar
