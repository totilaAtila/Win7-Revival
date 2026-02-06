#include "ConfigManager.h"
#include "Diagnostics.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace CrystalFrame {

ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
    Save();
}

bool ConfigManager::Initialize() {
    // Get AppData\Local\CrystalFrame path
    m_configPath = GetConfigDirectory();
    
    if (!EnsureDirectoryExists(m_configPath)) {
        CF_LOG(Error, "Failed to create config directory");
        return false;
    }
    
    m_configPath += L"\\config.json";
    
    // Try to load existing config
    if (!Load()) {
        CF_LOG(Info, "Config not found, using defaults");
        // Save default config
        Save();
    }
    
    return true;
}

bool ConfigManager::Load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::ifstream file(m_configPath);
    if (!file.is_open()) {
        return false;
    }
    
    // Simple JSON parsing (manual, no dependencies)
    std::string line;
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
        
        if (line.find("\"taskbarOpacity\":") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string value = line.substr(pos + 1);
                value.erase(std::remove(value.begin(), value.end(), ','), value.end());
                m_config.taskbarOpacity = std::stoi(value);
            }
        }
        else if (line.find("\"startOpacity\":") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string value = line.substr(pos + 1);
                value.erase(std::remove(value.begin(), value.end(), ','), value.end());
                m_config.startOpacity = std::stoi(value);
            }
        }
        else if (line.find("\"taskbarEnabled\":") != std::string::npos) {
            m_config.taskbarEnabled = (line.find("true") != std::string::npos);
        }
        else if (line.find("\"startEnabled\":") != std::string::npos) {
            m_config.startEnabled = (line.find("true") != std::string::npos);
        }
    }
    
    file.close();
    
    CF_LOG(Info, "Config loaded: Taskbar=" << m_config.taskbarOpacity 
                 << ", Start=" << m_config.startOpacity);
    
    return true;
}

bool ConfigManager::Save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::ofstream file(m_configPath);
    if (!file.is_open()) {
        CF_LOG(Error, "Failed to save config");
        return false;
    }
    
    // Write JSON manually (formatted)
    file << "{\n";
    file << "  \"taskbarOpacity\": " << m_config.taskbarOpacity << ",\n";
    file << "  \"startOpacity\": " << m_config.startOpacity << ",\n";
    file << "  \"taskbarEnabled\": " << (m_config.taskbarEnabled ? "true" : "false") << ",\n";
    file << "  \"startEnabled\": " << (m_config.startEnabled ? "true" : "false") << "\n";
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

std::wstring ConfigManager::GetConfigDirectory() {
    wchar_t path[MAX_PATH];
    
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        std::wstring configDir(path);
        configDir += L"\\CrystalFrame";
        return configDir;
    }
    
    return L".\\CrystalFrame";
}

bool ConfigManager::EnsureDirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // Directory doesn't exist, create it
        if (!CreateDirectoryW(path.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }
    }
    else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        // Path exists but is not a directory
        return false;
    }
    
    return true;
}

} // namespace CrystalFrame
