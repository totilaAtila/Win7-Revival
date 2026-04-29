//
// TAPInjector.cpp — In-process XAML Diagnostics injection loop  [ITER #19]
//
// ITER #19: InjectGlassBarTAP() is now called from XamlBridgeHookProc
// (Shell_TrayWnd UI thread) rather than WorkerThread.  This ensures
// InitializeXamlDiagnosticsEx runs on the correct apartment thread,
// which is required on Windows 25H2 (build ≥ 26200).
//
// Uses GetCurrentProcessId() — the DLL is already inside explorer.exe
// (injected via WH_CALLWNDPROC hook from Renderer.cpp).
// NOT cross-process; FindExplorerPid() is NOT used here.
//
#include "XamlBridgeCommon.h"
#include "TAPInjector.h"
#include "TAPObject.h"   // for CLSID_GlassBarTAP

using pfnInitXamlDiagEx = HRESULT (WINAPI*)(
    LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);

static std::atomic<bool> g_tapInited { false };

void InjectGlassBarTAP()
{
    // NOTE: no early-return guard here. Each call re-scans all connection slots so
    // that XAML islands that appear AFTER the first injection (e.g. the taskbar
    // island on 25H2 which registers later than generic DesktopWindowXamlSource)
    // are also picked up. The hook proc rate-limits calls to ≤ 1 per 500 ms.

    // LoadLibraryW is safe even if already loaded — just increments refcount
    HMODULE hXaml = LoadLibraryW(L"Windows.UI.Xaml.dll");
    XBLogFmt(L"InjectGlassBarTAP: Windows.UI.Xaml.dll handle=0x%p", hXaml);
    if (!hXaml) {
        XBLog(L"InjectGlassBarTAP: XAML DLL not loaded — will retry on next call");
        return;
    }

    auto pfn = reinterpret_cast<pfnInitXamlDiagEx>(
        GetProcAddress(hXaml, "InitializeXamlDiagnosticsEx"));
    XBLogFmt(L"InjectGlassBarTAP: InitializeXamlDiagnosticsEx ptr=0x%p", pfn);
    if (!pfn) {
        XBLog(L"InjectGlassBarTAP: InitializeXamlDiagnosticsEx not exported — will retry on next call");
        return;
    }

    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
    XBLogFmt(L"InjectGlassBarTAP: TAP DLL path = %s", dllPath);

    // Win11 taskbar has multiple XAML islands (task area, taskbar frame, system tray…).
    // Each island exposes its own VisualDiagConnectionN slot.
    // We must register with ALL of them so OnVisualTreeChange fires for every island.
    // Stop after 20 consecutive failures (ERROR_NOT_FOUND = slot doesn't exist).
    int successCount     = 0;
    int consecutiveFails = 0;

    for (int i = 1; i <= 10000 && !g_stopping.load(); ++i) {
        wchar_t conn[64];
        swprintf_s(conn, L"VisualDiagConnection%d", i);
        HRESULT hr = pfn(conn, GetCurrentProcessId(),
                         L"", dllPath, CLSID_GlassBarTAP, nullptr);
        if (SUCCEEDED(hr)) {
            XBLogFmt(L"InjectGlassBarTAP: SUCCESS via %s (hr=0x%08X)", conn, hr);
            ++successCount;
            consecutiveFails = 0;
        } else {
            if (i <= 5 || consecutiveFails == 0) {
                XBLogFmt(L"InjectGlassBarTAP: %s failed hr=0x%08X", conn, hr);
            }
            if (++consecutiveFails >= 20) break;
        }
    }
    XBLogFmt(L"InjectGlassBarTAP: done — %d new island(s) registered this pass", successCount);
    // g_tapInited is intentionally NOT set here. The hook proc in dllmain.cpp stops
    // calling InjectGlassBarTAP only when g_knownShapes is non-empty (actual taskbar
    // BackgroundFill elements found), not simply because a connection count > 0.
    // This allows us to pick up the taskbar XAML island even when it registers later
    // than other generic DesktopWindowXamlSource islands (common on Windows 25H2).
}

bool IsTapInited()
{
    return g_tapInited.load();
}
