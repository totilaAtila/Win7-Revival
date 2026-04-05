//
// dllmain.cpp — DLL entry, COM exports, WorkerThread, logging  [ITER #19]
//
// ITER #19 change: InjectGlassBarTAP() is now called from XamlBridgeHookProc
// (Shell_TrayWnd UI thread) instead of WorkerThread.
//
// Root cause fixed: on Windows 25H2 (build ≥ 26200) InitializeXamlDiagnosticsEx
// must be called from the XAML island's owning thread.  XamlBridgeHookProc fires
// on that thread (Shell_TrayWnd UI thread) via WH_CALLWNDPROC injection, so it is
// the correct call site.  WorkerThread retains only shared-memory monitoring and
// brush-update dispatch.
//
// Defines all module-level globals (declared extern in XamlBridgeCommon.h).
//
#include "XamlBridgeCommon.h"
#include "XamlBridge.h"
#include "TAPObject.h"
#include "TAPInjector.h"

// ---------------------------------------------------------------------------
// Module-level state  (extern-declared in XamlBridgeCommon.h / TAPObject.h)
// ---------------------------------------------------------------------------
HINSTANCE                g_hModule      = nullptr;
std::atomic<bool>        g_stopping     { false };
SharedBlurState*         g_pState       = nullptr;
std::vector<ShapeEntry>  g_knownShapes;
std::mutex               g_shapesMtx;

// Global watcher reference — keeps VisualTreeWatcher alive after AdviseThread exits
winrt::com_ptr<VisualTreeWatcher> g_visualTreeWatcher;

winrt::com_ptr<IXamlDiagnostics> g_xamlDiagnostics;
winrt::com_ptr<IVisualTreeService3> g_visualTreeService3;
winrt::Windows::Foundation::IAsyncOperation<bool> g_walkAsyncAction{ nullptr };
winrt::Windows::Foundation::IAsyncOperation<bool> g_bgFillAsyncAction{ nullptr };
winrt::Windows::Foundation::IAsyncOperation<bool> g_bgStrokeAsyncAction{ nullptr };

static HANDLE            g_hWorkerThread = nullptr;

// Set to true the first time XamlBridgeHookProc fires.
// Tells WorkerThread that the hook is active and owns injection retries,
// so the worker does not attempt a wrong-thread fallback.
static std::atomic<bool> g_tapScheduled { false };

// Tick of the last InjectGlassBarTAP() attempt from the hook proc.
// Used to rate-limit retries to at most once per 500 ms.
static std::atomic<DWORD> g_lastTapAttemptTick { 0 };

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static wchar_t    g_logPath[MAX_PATH] = {};
static std::mutex g_logMtx;

static void InitLogPath()
{
    wchar_t appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData)))
        swprintf_s(g_logPath, L"%s\\GlassBar\\XamlBridge.log", appData);
    else
        GetTempPathW(MAX_PATH, g_logPath),
        wcsncat_s(g_logPath, L"GlassBar_XamlBridge.log", MAX_PATH);
}

void XBLog(const wchar_t* msg)
{
    OutputDebugStringW(L"[GlassBar.XamlBridge] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");

    if (!g_logPath[0]) return;
    std::lock_guard<std::mutex> lk(g_logMtx);
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_logPath, L"a,ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%s\n", msg);
        fclose(f);
    }
}

void XBLogFmt(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap; va_start(ap, fmt);
    vswprintf_s(buf, fmt, ap);
    va_end(ap);
    XBLog(buf);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool IsRunningInExplorer()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        return _wcsicmp(slash + 1, L"explorer.exe") == 0;
    return _wcsicmp(path, L"explorer.exe") == 0;
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
static DWORD WINAPI WorkerThread(LPVOID)
{
    InitLogPath();
    XBLogFmt(L"WorkerThread started — log: %s", g_logPath);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Wait up to 10 s for shared memory to be created by GlassBar.Core
    HANDLE hSharedMem = nullptr;
    for (int tries = 0; tries < 100 && !g_stopping.load(); ++tries) {
        hSharedMem = OpenFileMappingW(FILE_MAP_READ, FALSE, SharedBlurState::kName);
        if (hSharedMem) break;
        Sleep(100);
    }

    if (!hSharedMem) {
        XBLog(L"WorkerThread: shared memory not found — exiting");
        CoUninitialize();
        return 0;
    }

    g_pState = reinterpret_cast<SharedBlurState*>(
        MapViewOfFile(hSharedMem, FILE_MAP_READ, 0, 0, sizeof(SharedBlurState)));

    if (!g_pState) {
        XBLog(L"WorkerThread: MapViewOfFile failed");
        CloseHandle(hSharedMem);
        CoUninitialize();
        return 0;
    }

    // TAP injection is driven by XamlBridgeHookProc on Shell_TrayWnd's UI thread.
    // The hook retries automatically (rate-limited) until IsTapInited() returns true,
    // so the worker only needs to wait for the hook to have fired at least once.
    // If the hook never fires (hook not installed / wrong process), log and continue —
    // brush dispatch will simply find g_knownShapes empty until TAP succeeds.
    XBLog(L"WorkerThread: waiting for XamlBridgeHookProc to fire on Shell_TrayWnd thread...");
    for (int wait = 0; wait < 20 && !g_tapScheduled.load() && !g_stopping.load(); ++wait)
        Sleep(100);

    if (g_tapScheduled.load()) {
        XBLog(L"WorkerThread: hook proc is active — TAP injection handled on Shell_TrayWnd thread");
    } else {
        // Hook did not fire: DLL may have been loaded without a hook (unusual).
        // Attempting injection from worker thread as last resort.
        // On 25H2 this may not succeed but is better than silent failure.
        XBLog(L"WorkerThread: hook proc did not fire — last-resort injection from worker thread");
        InjectGlassBarTAP();
    }

    LONG lastVersion = -1;
    // Kept alive across Sleep so TryRunAsync callbacks are not cancelled before firing
    std::vector<winrt::Windows::Foundation::IAsyncOperation<bool>> s_pendingUpdateOps;

    while (!g_stopping.load()) {
        LONG shutdownReq = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&g_pState->shutdownRequest), 0, 0);
        if (shutdownReq) break;

        LONG curVersion = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&g_pState->version), 0, 0);

        if (curVersion != lastVersion) {
            lastVersion = curVersion;
            BrushParams params = ReadBrushParams(g_pState);
            XBLogFmt(L"WorkerThread: version=%d  enabled=%d  useBlur=%d  alpha=%d  R=%d G=%d B=%d",
                curVersion, params.enabled ? 1 : 0, params.useBlur ? 1 : 0,
                params.alpha, params.r, params.g, params.b);

            s_pendingUpdateOps.clear();
            std::lock_guard<std::mutex> lk(g_shapesMtx);
            XBLogFmt(L"WorkerThread: dispatching to %zu known shapes", g_knownShapes.size());
            for (const auto& entry : g_knownShapes) {
                try {
                    auto disp = entry.element.Dispatcher();
                    if (!disp) continue;
                    auto op = disp.TryRunAsync(wuc::CoreDispatcherPriority::High,
                        [handle = entry.handle, element = entry.element, prop = entry.prop, params]() noexcept {
                            try { ApplyBrushParams(handle, element, prop, params); }
                            catch (...) {}
                        });
                    if (op) s_pendingUpdateOps.push_back(op);
                }
                catch (...) {}
            }
        }

        Sleep(150);
    }

    // Restore original-ish color on shutdown (dark grey — default Win11 taskbar feel)
    {
        std::lock_guard<std::mutex> lk(g_shapesMtx);
        for (const auto& entry : g_knownShapes) {
            try {
                auto disp = entry.element.Dispatcher();
                if (!disp) continue;
                disp.RunAsync(wuc::CoreDispatcherPriority::High,
                    [handle = entry.handle, element = entry.element, prop = entry.prop]() noexcept {
                        try {
                            BrushParams p{};
                            p.enabled = false;
                            p.useBlur = false;
                            ApplyBrushParams(handle, element, prop, p);
                        }
                        catch (...) {}
                    });
            }
            catch (...) {}
        }
        g_knownShapes.clear();
    }

    g_visualTreeWatcher = nullptr;  // release before CoUninitialize
    g_visualTreeService3 = nullptr;
    g_xamlDiagnostics = nullptr;
    g_walkAsyncAction = nullptr;

    UnmapViewOfFile(g_pState);
    g_pState = nullptr;
    CloseHandle(hSharedMem);
    CoUninitialize();
    XBLog(L"WorkerThread: exited cleanly");
    return 0;
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_hModule = hInst;
        DisableThreadLibraryCalls(hInst);
        if (IsRunningInExplorer()) {
            g_stopping.store(false);
            g_hWorkerThread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        }
        break;

    case DLL_PROCESS_DETACH:
        g_stopping.store(true);
        if (g_hWorkerThread) {
            WaitForSingleObject(g_hWorkerThread, 1500);
            CloseHandle(g_hWorkerThread);
            g_hWorkerThread = nullptr;
        }
        break;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// COM exports
// ---------------------------------------------------------------------------
extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    XBLogFmt(L"DllGetClassObject called (match=%d)", rclsid == CLSID_GlassBarTAP ? 1 : 0);
    if (rclsid == CLSID_GlassBarTAP)
        return winrt::make<SimpleFactory<GlassBarTAP>>()->QueryInterface(riid, ppv);
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    return S_FALSE;
}

// ---------------------------------------------------------------------------
// Exported hook proc (used by SetWindowsHookEx injection in Renderer.cpp)
//
// Fires on Shell_TrayWnd's UI thread — the correct apartment for
// InitializeXamlDiagnosticsEx on Windows 25H2 (build ≥ 26200).
//
// Retries InjectGlassBarTAP() on every call until IsTapInited() returns true,
// rate-limited to at most one attempt per 500 ms.  This handles the startup
// timing case where XAML islands are not yet registered on the first callback.
// ---------------------------------------------------------------------------
extern "C" LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && !IsTapInited()) {
        // Mark hook as active so WorkerThread skips its wrong-thread fallback.
        g_tapScheduled.store(true);

        // Rate-limit attempts to avoid hammering InitializeXamlDiagnosticsEx
        // on every window message.  500 ms is enough to catch islands starting up.
        DWORD now  = GetTickCount();
        DWORD last = g_lastTapAttemptTick.load(std::memory_order_relaxed);
        if (now - last >= 500u) {
            g_lastTapAttemptTick.store(now, std::memory_order_relaxed);
            if (!g_logPath[0]) InitLogPath();
            XBLog(L"XamlBridgeHookProc: attempting TAP injection on Shell_TrayWnd thread [ITER #19]");
            InjectGlassBarTAP();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
