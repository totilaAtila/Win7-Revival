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
    bool startEnabled = true;
    bool taskbarBlur = false;
    bool startBlur = false;
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
    void SetHotkey(int vk, int modifiers);
    
private:
    std::wstring m_configPath;
    Config m_config;
    mutable std::mutex m_mutex;
    
    std::wstring GetConfigDirectory();
    bool EnsureDirectoryExists(const std::wstring& path);
};

} // namespace GlassBar
