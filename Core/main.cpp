#include <Windows.h>
#include "Core.h"
#include "Diagnostics.h"
#include <shlobj.h>
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")

using namespace GlassBar;

// ---------------------------------------------------------------------------
// Unhandled-exception crash handler
// Captures any SEH fault (access violation, stack overflow, heap corruption,
// etc.) that escapes all try/catch blocks, writes a minidump and a plain-text
// crash entry so post-mortem analysis is always possible.
// ---------------------------------------------------------------------------
static LONG WINAPI GlassBarExceptionFilter(EXCEPTION_POINTERS* pei)
{
    // Resolve the GlassBar data directory (same folder used by the logger).
    wchar_t appDataBuf[MAX_PATH] = {};
    std::wstring crashDir;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appDataBuf))) {
        crashDir = std::wstring(appDataBuf) + L"\\GlassBar";
    } else {
        crashDir = L".";
    }
    CreateDirectoryW(crashDir.c_str(), nullptr);

    // Build a timestamp string used for both file names.
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

    // --- Crash log entry -------------------------------------------------
    // Written with raw Win32 so it works even if Logger never initialised.
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
        // The Logger writes through wofstream (system locale, no BOM on append).
        // Match that by converting to the ANSI code page here.
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

// Get log file path in AppData\Local\GlassBar
std::wstring GetLogFilePath() {
    wchar_t path[MAX_PATH];
    
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        std::wstring logPath(path);
        logPath += L"\\GlassBar";
        
        // Create directory if it doesn't exist
        CreateDirectoryW(logPath.c_str(), NULL);
        
        logPath += L"\\GlassBar.log";
        return logPath;
    }
    
    return L".\\GlassBar.log";
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);
    
    // Install crash handler as early as possible — before Logger, COM, or any
    // subsystem that could itself fault on a bad machine state.
    SetUnhandledExceptionFilter(GlassBarExceptionFilter);

    // Set DPI awareness for accurate positioning
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    
    // Initialize logger
    Logger::Instance().Initialize(GetLogFilePath());
    CF_LOG(Info, "===================================");
    CF_LOG(Info, "  GlassBar Engine v1.0");
    CF_LOG(Info, "  Windows 11 Overlay Utility");
    CF_LOG(Info, "===================================");
    
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        CF_LOG(Error, "CoInitializeEx failed: 0x" << std::hex << hr);
        MessageBoxW(nullptr, 
                   L"Failed to initialize COM. Application will exit.",
                   L"GlassBar Error",
                   MB_OK | MB_ICONERROR);
        return 1;
    }
    
    int exitCode = 0;
    
    // Create and run core
    {
        auto core = std::make_unique<GlassBarCore>(hInstance);
        
        if (!core->Initialize()) {
            CF_LOG(Error, "Core initialization failed");
            MessageBoxW(nullptr,
                       L"GlassBar failed to initialize. Check GlassBar.log for details.",
                       L"GlassBar Error",
                       MB_OK | MB_ICONERROR);
            exitCode = 1;
        } else {
            // Run message loop
            core->Run();
        }
        
        // Shutdown
        core->Shutdown();
    }
    
    // Uninitialize COM
    CoUninitialize();
    
    CF_LOG(Info, "===================================");
    CF_LOG(Info, "  GlassBar Engine Exited");
    CF_LOG(Info, "  Exit Code: " << exitCode);
    CF_LOG(Info, "===================================");
    
    Logger::Instance().Shutdown();
    
    return exitCode;
}
