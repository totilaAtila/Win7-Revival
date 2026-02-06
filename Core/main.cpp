#include <Windows.h>
#include "Core.h"
#include "Diagnostics.h"
#include <shlobj.h>

using namespace CrystalFrame;

// Get log file path in AppData\Local\CrystalFrame
std::wstring GetLogFilePath() {
    wchar_t path[MAX_PATH];
    
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        std::wstring logPath(path);
        logPath += L"\\CrystalFrame";
        
        // Create directory if it doesn't exist
        CreateDirectoryW(logPath.c_str(), NULL);
        
        logPath += L"\\CrystalFrame.log";
        return logPath;
    }
    
    return L".\\CrystalFrame.log";
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
    
    // Set DPI awareness for accurate positioning
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    
    // Initialize logger
    Logger::Instance().Initialize(GetLogFilePath());
    CF_LOG(Info, "===================================");
    CF_LOG(Info, "  CrystalFrame Engine v1.0");
    CF_LOG(Info, "  Windows 11 Overlay Utility");
    CF_LOG(Info, "===================================");
    
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        CF_LOG(Error, "CoInitializeEx failed: 0x" << std::hex << hr);
        MessageBoxW(nullptr, 
                   L"Failed to initialize COM. Application will exit.",
                   L"CrystalFrame Error",
                   MB_OK | MB_ICONERROR);
        return 1;
    }
    
    int exitCode = 0;
    
    // Create and run core
    {
        auto core = std::make_unique<CrystalFrameCore>(hInstance);
        
        if (!core->Initialize()) {
            CF_LOG(Error, "Core initialization failed");
            MessageBoxW(nullptr,
                       L"CrystalFrame failed to initialize. Check CrystalFrame.log for details.",
                       L"CrystalFrame Error",
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
    CF_LOG(Info, "  CrystalFrame Engine Exited");
    CF_LOG(Info, "  Exit Code: " << exitCode);
    CF_LOG(Info, "===================================");
    
    Logger::Instance().Shutdown();
    
    return exitCode;
}
