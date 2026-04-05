#include "ConfigManager.h"
#include "Diagnostics.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace GlassBar {

namespace {

std::string NarrowForLog(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

bool ParseIntLine(const std::string& line, const char* key, int& target, int minValue, int maxValue) {
    const std::string needle = std::string("\"") + key + "\":";
    if (line.find(needle) == std::string::npos) {
        return false;
    }

    size_t pos = line.find(':');
    if (pos == std::string::npos) {
        return true;
    }

    std::string value = line.substr(pos + 1);
    value.erase(std::remove(value.begin(), value.end(), ','), value.end());
    try {
        target = std::clamp(std::stoi(value), minValue, maxValue);
    } catch (const std::exception&) {
        CF_LOG(Warning, "Invalid value for " << key << " in config, using default");
    }
    return true;
}

bool ParseBoolLine(const std::string& line, const char* key, bool& target) {
    const std::string needle = std::string("\"") + key + "\":";
    if (line.find(needle) == std::string::npos) {
        return false;
    }

    target = (line.find("true") != std::string::npos);
    return true;
}

} // namespace

ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
    Save();
}

bool ConfigManager::Initialize() {
    m_configPath = GetConfigDirectory();

    if (!EnsureDirectoryExists(m_configPath)) {
        CF_LOG(Error, "Failed to create config directory");
        return false;
    }

    m_configPath += L"\\config.json";
    CF_LOG(Info, "Config path resolved to: " << NarrowForLog(m_configPath));

    if (!Load()) {
        if (m_lastLoadSawFile && m_lastLoadOpenFailed) {
            CF_LOG(Warning, "Config file exists but could not be opened; using in-memory defaults");
        } else if (!m_lastLoadSawFile) {
            CF_LOG(Info, "Config file missing, using defaults");
            Save();
        }
    }

    return true;
}

bool ConfigManager::Load() {
    DWORD attrs = GetFileAttributesW(m_configPath.c_str());
    m_lastLoadSawFile = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    m_lastLoadOpenFailed = false;

    CF_LOG(Info, "Config file exists: " << (m_lastLoadSawFile ? "yes" : "no"));
    if (!m_lastLoadSawFile) {
        return false;
    }

    std::ifstream file(m_configPath);
    if (!file.is_open()) {
        m_lastLoadOpenFailed = true;
        return false;
    }

    Config tempConfig;
    std::string line;
    while (std::getline(file, line)) {
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

        if (ParseIntLine(line, "TaskbarOpacity", tempConfig.taskbarOpacity, 0, 100)) continue;
        if (ParseIntLine(line, "StartOpacity", tempConfig.startOpacity, 0, 100)) continue;
        if (ParseBoolLine(line, "TaskbarEnabled", tempConfig.taskbarEnabled)) continue;
        if (ParseBoolLine(line, "StartEnabled", tempConfig.startEnabled)) continue;
        if (ParseBoolLine(line, "CoreEnabled", tempConfig.coreEnabled)) continue;
        if (ParseBoolLine(line, "TaskbarBlur", tempConfig.taskbarBlur)) continue;
        if (ParseBoolLine(line, "StartBlur", tempConfig.startBlur)) continue;
        if (ParseBoolLine(line, "IsFirstRun", tempConfig.isFirstRun)) continue;

        if (ParseIntLine(line, "TaskbarColorR", tempConfig.taskbarColorR, 0, 255)) continue;
        if (ParseIntLine(line, "TaskbarColorG", tempConfig.taskbarColorG, 0, 255)) continue;
        if (ParseIntLine(line, "TaskbarColorB", tempConfig.taskbarColorB, 0, 255)) continue;

        if (ParseIntLine(line, "StartBgColorR", tempConfig.startBgColorR, 0, 255)) continue;
        if (ParseIntLine(line, "StartBgColorG", tempConfig.startBgColorG, 0, 255)) continue;
        if (ParseIntLine(line, "StartBgColorB", tempConfig.startBgColorB, 0, 255)) continue;
        if (ParseIntLine(line, "StartTextColorR", tempConfig.startTextColorR, 0, 255)) continue;
        if (ParseIntLine(line, "StartTextColorG", tempConfig.startTextColorG, 0, 255)) continue;
        if (ParseIntLine(line, "StartTextColorB", tempConfig.startTextColorB, 0, 255)) continue;
        if (ParseIntLine(line, "StartBorderColorR", tempConfig.startBorderColorR, 0, 255)) continue;
        if (ParseIntLine(line, "StartBorderColorG", tempConfig.startBorderColorG, 0, 255)) continue;
        if (ParseIntLine(line, "StartBorderColorB", tempConfig.startBorderColorB, 0, 255)) continue;

        if (ParseBoolLine(line, "StartShowControlPanel", tempConfig.startShowControlPanel)) continue;
        if (ParseBoolLine(line, "StartShowDeviceManager", tempConfig.startShowDeviceManager)) continue;
        if (ParseBoolLine(line, "StartShowInstalledApps", tempConfig.startShowInstalledApps)) continue;
        if (ParseBoolLine(line, "StartShowDocuments", tempConfig.startShowDocuments)) continue;
        if (ParseBoolLine(line, "StartShowPictures", tempConfig.startShowPictures)) continue;
        if (ParseBoolLine(line, "StartShowVideos", tempConfig.startShowVideos)) continue;
        if (ParseBoolLine(line, "StartShowRecentFiles", tempConfig.startShowRecentFiles)) continue;
        if (ParseIntLine(line, "HotkeyVk", tempConfig.hotkeyVk, 0, 0xFF)) continue;
        if (ParseIntLine(line, "HotkeyModifiers", tempConfig.hotkeyModifiers, 0, 0xFFFF)) continue;
        if (ParseIntLine(line, "BlurAmount", tempConfig.blurAmount, 0, 100)) continue;
    }

    file.close();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = tempConfig;
    }

    CF_LOG(Info, "Config loaded successfully: Taskbar="
                 << m_config.taskbarOpacity
                 << "% enabled=" << (m_config.taskbarEnabled ? "true" : "false")
                 << " RGB=(" << m_config.taskbarColorR << "," << m_config.taskbarColorG << "," << m_config.taskbarColorB << ")"
                 << " blur=" << (m_config.taskbarBlur ? "true" : "false")
                 << " amount=" << m_config.blurAmount
                 << " | Start=" << m_config.startOpacity
                 << "% enabled=" << (m_config.startEnabled ? "true" : "false")
                 << " blur=" << (m_config.startBlur ? "true" : "false"));

    return true;
}

bool ConfigManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ofstream file(m_configPath);
    if (!file.is_open()) {
        CF_LOG(Error, "Failed to save config");
        return false;
    }

    file << "{\n";
    file << "  \"TaskbarOpacity\": " << m_config.taskbarOpacity << ",\n";
    file << "  \"StartOpacity\": " << m_config.startOpacity << ",\n";
    file << "  \"TaskbarEnabled\": " << (m_config.taskbarEnabled ? "true" : "false") << ",\n";
    file << "  \"StartEnabled\": " << (m_config.startEnabled ? "true" : "false") << ",\n";
    file << "  \"CoreEnabled\": " << (m_config.coreEnabled ? "true" : "false") << ",\n";
    file << "  \"TaskbarColorR\": " << m_config.taskbarColorR << ",\n";
    file << "  \"TaskbarColorG\": " << m_config.taskbarColorG << ",\n";
    file << "  \"TaskbarColorB\": " << m_config.taskbarColorB << ",\n";
    file << "  \"StartBgColorR\": " << m_config.startBgColorR << ",\n";
    file << "  \"StartBgColorG\": " << m_config.startBgColorG << ",\n";
    file << "  \"StartBgColorB\": " << m_config.startBgColorB << ",\n";
    file << "  \"StartTextColorR\": " << m_config.startTextColorR << ",\n";
    file << "  \"StartTextColorG\": " << m_config.startTextColorG << ",\n";
    file << "  \"StartTextColorB\": " << m_config.startTextColorB << ",\n";
    file << "  \"StartShowControlPanel\": " << (m_config.startShowControlPanel ? "true" : "false") << ",\n";
    file << "  \"StartShowDeviceManager\": " << (m_config.startShowDeviceManager ? "true" : "false") << ",\n";
    file << "  \"StartShowInstalledApps\": " << (m_config.startShowInstalledApps ? "true" : "false") << ",\n";
    file << "  \"StartShowDocuments\": " << (m_config.startShowDocuments ? "true" : "false") << ",\n";
    file << "  \"StartShowPictures\": " << (m_config.startShowPictures ? "true" : "false") << ",\n";
    file << "  \"StartShowVideos\": " << (m_config.startShowVideos ? "true" : "false") << ",\n";
    file << "  \"StartShowRecentFiles\": " << (m_config.startShowRecentFiles ? "true" : "false") << ",\n";
    file << "  \"StartBorderColorR\": " << m_config.startBorderColorR << ",\n";
    file << "  \"StartBorderColorG\": " << m_config.startBorderColorG << ",\n";
    file << "  \"StartBorderColorB\": " << m_config.startBorderColorB << ",\n";
    file << "  \"TaskbarBlur\": " << (m_config.taskbarBlur ? "true" : "false") << ",\n";
    file << "  \"StartBlur\": " << (m_config.startBlur ? "true" : "false") << ",\n";
    file << "  \"IsFirstRun\": " << (m_config.isFirstRun ? "true" : "false") << ",\n";
    file << "  \"HotkeyVk\": " << m_config.hotkeyVk << ",\n";
    file << "  \"HotkeyModifiers\": " << m_config.hotkeyModifiers << ",\n";
    file << "  \"BlurAmount\": " << m_config.blurAmount << "\n";
    file << "}\n";

    file.close();

    CF_LOG(Debug, "Config saved");
    return true;
}

Config ConfigManager::GetConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::UpdateConfig(const Config& newConfig) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = newConfig;
}

void ConfigManager::SetTaskbarOpacity(int opacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.taskbarOpacity = std::clamp(opacity, 0, 100);
}

void ConfigManager::SetStartOpacity(int opacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startOpacity = std::clamp(opacity, 0, 100);
}

void ConfigManager::SetTaskbarEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.taskbarEnabled = enabled;
}

void ConfigManager::SetStartEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startEnabled = enabled;
}

void ConfigManager::SetTaskbarBlur(bool blur) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.taskbarBlur = blur;
}

void ConfigManager::SetStartBlur(bool blur) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startBlur = blur;
}

void ConfigManager::SetTaskbarColor(int r, int g, int b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.taskbarColorR = std::clamp(r, 0, 255);
    m_config.taskbarColorG = std::clamp(g, 0, 255);
    m_config.taskbarColorB = std::clamp(b, 0, 255);
}

void ConfigManager::SetBlurAmount(int amount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.blurAmount = std::clamp(amount, 0, 100);
}

void ConfigManager::SetStartMenuBackgroundColor(int r, int g, int b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startBgColorR = std::clamp(r, 0, 255);
    m_config.startBgColorG = std::clamp(g, 0, 255);
    m_config.startBgColorB = std::clamp(b, 0, 255);
}

void ConfigManager::SetStartMenuTextColor(int r, int g, int b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startTextColorR = std::clamp(r, 0, 255);
    m_config.startTextColorG = std::clamp(g, 0, 255);
    m_config.startTextColorB = std::clamp(b, 0, 255);
}

void ConfigManager::SetStartMenuBorderColor(int r, int g, int b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startBorderColorR = std::clamp(r, 0, 255);
    m_config.startBorderColorG = std::clamp(g, 0, 255);
    m_config.startBorderColorB = std::clamp(b, 0, 255);
}

void ConfigManager::SetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                                      bool documents, bool pictures, bool videos, bool recentFiles) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.startShowControlPanel = controlPanel;
    m_config.startShowDeviceManager = deviceManager;
    m_config.startShowInstalledApps = installedApps;
    m_config.startShowDocuments = documents;
    m_config.startShowPictures = pictures;
    m_config.startShowVideos = videos;
    m_config.startShowRecentFiles = recentFiles;
}

void ConfigManager::SetHotkey(int vk, int modifiers) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.hotkeyVk = vk;
    m_config.hotkeyModifiers = modifiers;
}

std::wstring ConfigManager::GetConfigDirectory() {
    wchar_t path[MAX_PATH];

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        std::wstring configDir(path);
        configDir += L"\\GlassBar";
        return configDir;
    }

    return L".\\GlassBar";
}

bool ConfigManager::EnsureDirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());

    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(path.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }
    } else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    return true;
}

} // namespace GlassBar
