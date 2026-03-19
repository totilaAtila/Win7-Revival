#define CRYSTALFRAME_EXPORTS
#include "CoreApi.h"
#include "Core.h"
#include "Diagnostics.h"
#include <memory>
#include <Windows.h>
#include <ShlObj.h>
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")

namespace {
    std::unique_ptr<GlassBar::Core> g_core;
}

// ---------------------------------------------------------------------------
// Unhandled-exception crash handler
// Captures any SEH fault (access violation, stack overflow, heap corruption,
// etc.) that escapes all try/catch blocks, writes a minidump and a plain-text
// crash entry so post-mortem analysis is always possible.
// Installed by CoreInitialize() so it is active for the lifetime of the DLL.
// ---------------------------------------------------------------------------
static LONG WINAPI GlassBarExceptionFilter(EXCEPTION_POINTERS* pei)
{
    wchar_t appDataBuf[MAX_PATH] = {};
    std::wstring crashDir;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appDataBuf))) {
        crashDir = std::wstring(appDataBuf) + L"\\GlassBar";
    } else {
        crashDir = L".";
    }
    CreateDirectoryW(crashDir.c_str(), nullptr);

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t ts[32] = {};
    swprintf_s(ts, L"%04u%02u%02u_%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    // --- Minidump --------------------------------------------------------
    std::wstring dmpPath = crashDir + L"\\crash_" + ts + L".dmp";
    HANDLE hDmp = CreateFileW(dmpPath.c_str(), GENERIC_WRITE, 0,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hDmp != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei = {};
        mdei.ThreadId          = GetCurrentThreadId();
        mdei.ExceptionPointers = pei;
        mdei.ClientPointers    = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hDmp, MiniDumpWithThreadInfo, &mdei, nullptr, nullptr);
        CloseHandle(hDmp);
    }

    // --- Crash log entry (raw Win32 — works even if Logger never initialised) ---
    std::wstring logPath = crashDir + L"\\GlassBar.log";
    HANDLE hLog = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hLog != INVALID_HANDLE_VALUE) {
        DWORD code = pei->ExceptionRecord->ExceptionCode;
        void* addr = pei->ExceptionRecord->ExceptionAddress;

        wchar_t entry[768] = {};
        swprintf_s(entry,
            L"\n[%04u-%02u-%02u %02u:%02u:%02u][-----][ERROR] "
            L"UNHANDLED EXCEPTION — Code=0x%08X  Addr=%p\n"
            L"[%04u-%02u-%02u %02u:%02u:%02u][-----][ERROR] "
            L"Minidump written to: %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            code, addr,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            dmpPath.c_str());

        DWORD written = 0;
        int needed = WideCharToMultiByte(CP_ACP, 0, entry, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 0) {
            std::string narrow(static_cast<size_t>(needed), '\0');
            WideCharToMultiByte(CP_ACP, 0, entry, -1, narrow.data(), needed, nullptr, nullptr);
            WriteFile(hLog, narrow.c_str(), static_cast<DWORD>(narrow.size() - 1), &written, nullptr);
        }
        CloseHandle(hLog);
    }

    // Pass control back to Windows so WER can also record the crash.
    return EXCEPTION_CONTINUE_SEARCH;
}

extern "C" {

CRYSTALFRAME_API bool CoreInitialize() {
    try {
        if (g_core) {
            CF_LOG(Warning, "Core already initialized");
            return true;
        }

        // Install crash handler before any other subsystem so even early
        // faults produce a minidump and log entry.
        SetUnhandledExceptionFilter(GlassBarExceptionFilter);

        // Initialize logger first
        wchar_t localAppData[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
            std::wstring logDir = std::wstring(localAppData) + L"\\GlassBar";
            CreateDirectoryW(logDir.c_str(), nullptr); // Create dir if not exists
            std::wstring logPath = logDir + L"\\GlassBar.log";
            GlassBar::Logger::Instance().Initialize(logPath);
        }

        CF_LOG(Info, "=== CoreInitialize called ===");

        g_core = std::make_unique<GlassBar::Core>();
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
        GlassBar::Logger::Instance().Shutdown();
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

CRYSTALFRAME_API void CoreSetStartMenuPinned(bool pinned) {
    if (!g_core) return;
    g_core->SetStartMenuPinned(pinned);
}

CRYSTALFRAME_API void CoreSetStartMenuBorderColor(unsigned int rgb) {
    if (!g_core) return;
    g_core->SetStartMenuBorderColor(static_cast<DWORD>(rgb));
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
