#pragma once
#include <Windows.h>
#include <string>
#include <atomic>
#include <mutex>

namespace CrystalFrame {

struct Config {
    int taskbarOpacity = 75;      // 0-100
    int startOpacity = 50;         // 0-100
    bool taskbarEnabled = true;
    bool startEnabled = true;
    bool taskbarBlur = false;
    bool startBlur = false;
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
    
private:
    std::wstring m_configPath;
    Config m_config;
    mutable std::mutex m_mutex;
    
    std::wstring GetConfigDirectory();
    bool EnsureDirectoryExists(const std::wstring& path);
};

} // namespace CrystalFrame
