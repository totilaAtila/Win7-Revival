#define CRYSTALFRAME_EXPORTS
#include "CoreApi.h"
#include "Core.h"
#include "Diagnostics.h"
#include <memory>
#include <Windows.h>
#include <ShlObj.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace {
    std::unique_ptr<CrystalFrame::Core> g_core;
    wchar_t g_crashDumpDir[MAX_PATH] = {};  // set during CoreInitialize

    // ── Crash dump handler ────────────────────────────────────────────────────
    // Called by Windows for any unhandled SEH exception in this process.
    // Writes a minidump to %LOCALAPPDATA%\CrystalFrame\crash_YYYYMMDD_HHMMSS.dmp
    // and logs the exception code at Error level.
    LONG WINAPI CrashDumpHandler(EXCEPTION_POINTERS* ep) {
        // Build timestamped filename
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        wchar_t dumpPath[MAX_PATH];
        swprintf_s(dumpPath, L"%s\\crash_%04d%02d%02d_%02d%02d%02d.dmp",
                   g_crashDumpDir,
                   st.wYear, st.wMonth,  st.wDay,
                   st.wHour, st.wMinute, st.wSecond);

        // Write minidump (type: MiniDumpWithFullMemory for maximum info)
        HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei = {};
            mei.ThreadId          = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers    = FALSE;

            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                              hFile,
                              static_cast<MINIDUMP_TYPE>(
                                  MiniDumpWithFullMemory |
                                  MiniDumpWithHandleData |
                                  MiniDumpWithThreadInfo),
                              &mei, nullptr, nullptr);
            CloseHandle(hFile);
        }

        // Log the crash (lower min level to catch everything, flush to disk)
        CrystalFrame::Logger::Instance().SetMinLevel(CrystalFrame::LogLevel::Debug);
        CF_LOG(Error, "=== UNHANDLED EXCEPTION — process will terminate ===");
        if (ep) {
            CF_LOG(Error, "  ExceptionCode   = 0x"
                   << std::hex << ep->ExceptionRecord->ExceptionCode);
            CF_LOG(Error, "  ExceptionAddress = 0x"
                   << reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress));
        }
        // Convert wide path to narrow for the log stream
        char narrowPath[MAX_PATH] = {};
        WideCharToMultiByte(CP_ACP, 0, dumpPath, -1, narrowPath, MAX_PATH, nullptr, nullptr);
        CF_LOG(Error, "  MiniDump written to: " << narrowPath);

        // Return EXCEPTION_CONTINUE_SEARCH so Windows Error Reporting still runs
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

extern "C" {

CRYSTALFRAME_API bool CoreInitialize() {
    try {
        if (g_core) {
            CF_LOG(Warning, "Core already initialized");
            return true;
        }

        // Initialize logger + crash dump directory
        wchar_t localAppData[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
            std::wstring logDir = std::wstring(localAppData) + L"\\CrystalFrame";
            CreateDirectoryW(logDir.c_str(), nullptr);
            std::wstring logPath = logDir + L"\\CrystalFrame.log";
            CrystalFrame::Logger::Instance().Initialize(logPath);
            wcscpy_s(g_crashDumpDir, logDir.c_str());
        }

        // Install crash dump handler (catches SEH exceptions not caught by try/catch)
        SetUnhandledExceptionFilter(CrashDumpHandler);

        CF_LOG(Warning, "=== CoreInitialize ===");

        g_core = std::make_unique<CrystalFrame::Core>();
        bool success = g_core->Initialize();

        if (!success) {
            CF_LOG(Error, "Core initialization failed");
            g_core.reset();
            return false;
        }

        CF_LOG(Warning, "=== Core ready ===");
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
            CF_LOG(Warning, "Core API shutdown initiated");
            g_core->Shutdown();
            g_core.reset();
            CF_LOG(Warning, "Core API shutdown complete");
        }

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

    status->taskbar.found   = g_core->GetTaskbarFound();
    status->taskbar.enabled = g_core->GetTaskbarEnabled();
    status->taskbar.opacity = g_core->GetTaskbarOpacity();

    status->start.detected = g_core->GetStartDetected();
    status->start.enabled  = g_core->GetStartEnabled();
    status->start.opacity  = g_core->GetStartOpacity();
}

CRYSTALFRAME_API bool CoreProcessMessages() {
    if (!g_core) {
        return false;
    }
    return g_core->ProcessMessages();
}

} // extern "C"
