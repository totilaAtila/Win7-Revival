#pragma once
#include <Windows.h>
#include <string>
#include <atomic>
#include <mutex>

namespace GlassBar {

struct Config {
    int taskbarOpacity = 75;      // 0-100
    int startOpacity = 50;         // 0-100
    bool taskbarEnabled = true;
    bool startEnabled = false;
    bool coreEnabled = true;
    bool taskbarBlur = false;
    bool startBlur = false;
    bool isFirstRun = false;
    int taskbarColorR = 0;
    int taskbarColorG = 0;
    int taskbarColorB = 0;
    int startBgColorR = 40;
    int startBgColorG = 40;
    int startBgColorB = 45;
    int startTextColorR = 255;
    int startTextColorG = 255;
    int startTextColorB = 255;
    int startBorderColorR = 60;
    int startBorderColorG = 60;
    int startBorderColorB = 65;
    bool startShowControlPanel = true;
    bool startShowDeviceManager = true;
    bool startShowInstalledApps = true;
    bool startShowDocuments = true;
    bool startShowPictures = true;
    bool startShowVideos = true;
    bool startShowRecentFiles = true;
    int hotkeyVk        = 0;      // 0 = disabled; virtual-key code (e.g. 'G' = 0x47)
    int hotkeyModifiers = 0;      // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    int blurAmount      = 0;      // 0 = off; 1-100 = XamlBridge blur intensity
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    bool Initialize();
    bool Load();
    bool Save();
    
    Config GetConfig() const;
    void UpdateConfig(const Config& newConfig);
    
    // Individual setters (thread-safe)
    void SetTaskbarOpacity(int opacity);
    void SetStartOpacity(int opacity);
    void SetTaskbarEnabled(bool enabled);
    void SetStartEnabled(bool enabled);
    void SetTaskbarBlur(bool blur);
    void SetStartBlur(bool blur);
    void SetTaskbarColor(int r, int g, int b);
    void SetBlurAmount(int amount);
    void SetStartMenuBackgroundColor(int r, int g, int b);
    void SetStartMenuTextColor(int r, int g, int b);
    void SetStartMenuBorderColor(int r, int g, int b);
    void SetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                           bool documents, bool pictures, bool videos, bool recentFiles);
    void SetHotkey(int vk, int modifiers);
    
private:
    std::wstring m_configPath;
    Config m_config;
    mutable std::mutex m_mutex;
    bool m_lastLoadSawFile = false;
    bool m_lastLoadOpenFailed = false;
    
    std::wstring GetConfigDirectory();
    bool EnsureDirectoryExists(const std::wstring& path);
};

} // namespace GlassBar
