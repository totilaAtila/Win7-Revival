#define CRYSTALFRAME_EXPORTS
#include "CoreApi.h"
#include "Core.h"
#include "Diagnostics.h"
#include <memory>
#include <Windows.h>
#include <ShlObj.h>

namespace {
    std::unique_ptr<CrystalFrame::Core> g_core;
}

extern "C" {

CRYSTALFRAME_API bool CoreInitialize() {
    try {
        if (g_core) {
            CF_LOG(Warning, "Core already initialized");
            return true;
        }

        // Initialize logger first
        wchar_t localAppData[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
            std::wstring logDir = std::wstring(localAppData) + L"\\CrystalFrame";
            CreateDirectoryW(logDir.c_str(), nullptr); // Create dir if not exists
            std::wstring logPath = logDir + L"\\CrystalFrame.log";
            CrystalFrame::Logger::Instance().Initialize(logPath);
        }

        CF_LOG(Info, "=== CoreInitialize called ===");

        g_core = std::make_unique<CrystalFrame::Core>();
        bool success = g_core->Initialize();

        if (!success) {
            CF_LOG(Error, "Core initialization failed");
            g_core.reset();
            return false;
        }

        CF_LOG(Info, "Core API initialized successfully");
        return true;
    }
    catch (const std::exception& ex) {
        CF_LOG(Error, "Core initialization exception: " << ex.what());
        g_core.reset();
        return false;
    }
}

CRYSTALFRAME_API void CoreShutdown() {
    try {
        if (g_core) {
            CF_LOG(Info, "Core API shutdown initiated");
            g_core->Shutdown();
            g_core.reset();
            CF_LOG(Info, "Core API shutdown complete");
        }

        // Shutdown logger
        CrystalFrame::Logger::Instance().Shutdown();
    }
    catch (const std::exception& ex) {
        CF_LOG(Error, "Core shutdown exception: " << ex.what());
    }
}

CRYSTALFRAME_API void CoreSetTaskbarOpacity(int opacity) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetTaskbarOpacity: Core not initialized");
        return;
    }
    g_core->SetTaskbarOpacity(opacity);
}

CRYSTALFRAME_API void CoreSetStartOpacity(int opacity) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartOpacity: Core not initialized");
        return;
    }
    g_core->SetStartOpacity(opacity);
}

CRYSTALFRAME_API void CoreSetTaskbarEnabled(bool enabled) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetTaskbarEnabled: Core not initialized");
        return;
    }
    g_core->SetTaskbarEnabled(enabled);
}

CRYSTALFRAME_API void CoreSetStartEnabled(bool enabled) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartEnabled: Core not initialized");
        return;
    }
    g_core->SetStartEnabled(enabled);
}

CRYSTALFRAME_API void CoreSetTaskbarColor(int r, int g, int b) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetTaskbarColor: Core not initialized");
        return;
    }
    g_core->SetTaskbarColor(r, g, b);
}

CRYSTALFRAME_API void CoreSetTaskbarBlur(bool enabled) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetTaskbarBlur: Core not initialized");
        return;
    }
    g_core->SetTaskbarBlur(enabled);
}

CRYSTALFRAME_API void CoreSetStartBlur(bool enabled) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartBlur: Core not initialized");
        return;
    }
    g_core->SetStartBlur(enabled);
}

CRYSTALFRAME_API void CoreSetStartMenuHookEnabled(bool enabled) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartMenuHookEnabled: Core not initialized");
        return;
    }
    g_core->SetStartMenuHookEnabled(enabled);
}

CRYSTALFRAME_API void CoreSetStartMenuOpacity(int opacity) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartMenuOpacity: Core not initialized");
        return;
    }
    g_core->SetStartMenuOpacity(opacity);
}

CRYSTALFRAME_API void CoreSetStartMenuBackgroundColor(unsigned int rgb) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartMenuBackgroundColor: Core not initialized");
        return;
    }
    g_core->SetStartMenuBackgroundColor(rgb);
}

CRYSTALFRAME_API void CoreSetStartMenuTextColor(unsigned int rgb) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartMenuTextColor: Core not initialized");
        return;
    }
    g_core->SetStartMenuTextColor(rgb);
}

CRYSTALFRAME_API void CoreSetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                                             bool documents, bool pictures, bool videos, bool recentFiles) {
    if (!g_core) {
        CF_LOG(Warning, "CoreSetStartMenuItems: Core not initialized");
        return;
    }
    g_core->SetStartMenuItems(controlPanel, deviceManager, installedApps, documents, pictures, videos, recentFiles);
}

CRYSTALFRAME_API void CoreGetStatus(CoreStatus* status) {
    if (!g_core || !status) {
        if (status) {
            memset(status, 0, sizeof(CoreStatus));
        }
        return;
    }

    // Get status from Core and populate the structure
    status->taskbar.found = g_core->GetTaskbarFound();
    status->taskbar.enabled = g_core->GetTaskbarEnabled();
    status->taskbar.opacity = g_core->GetTaskbarOpacity();

    status->start.detected = g_core->GetStartDetected();
    status->start.enabled = g_core->GetStartEnabled();
    status->start.opacity = g_core->GetStartOpacity();
}

CRYSTALFRAME_API bool CoreProcessMessages() {
    if (!g_core) {
        return false;
    }
    return g_core->ProcessMessages();
}

} // extern "C"
