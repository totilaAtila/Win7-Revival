//
// GlassBar.XamlBridge.dll
//
// Injected into explorer.exe via WH_CALLWNDPROC hook.
// Once inside explorer, opens shared memory from GlassBar.Core and applies
// SWCA (SetWindowCompositionAttribute) on Shell_TrayWnd from the owner
// process, which is the key difference from calling it cross-process.
//
// On Windows 11 22H2+ SWCA blur/acrylic calls that originate from *outside*
// explorer.exe are silently ignored by DWM. Calling the same API from within
// explorer's own process context restores the intended effect.
//

#define XAMLBRIDGE_EXPORTS
#include "XamlBridge.h"
#include "SharedBlurState.h"
#include <string>
#include <atomic>

// ---------------------------------------------------------------------------
// SWCA types (undocumented, mirrored from Renderer.h)
// ---------------------------------------------------------------------------

enum XB_ACCENT_STATE {
    XB_ACCENT_DISABLED               = 0,
    XB_ACCENT_ENABLE_GRADIENT        = 1,
    XB_ACCENT_ENABLE_TRANSPARENTGRAD = 2,
    XB_ACCENT_ENABLE_BLURBEHIND      = 3,
    XB_ACCENT_ENABLE_ACRYLICBLUR     = 4,
};

struct XB_ACCENT_POLICY {
    XB_ACCENT_STATE AccentState;
    DWORD           AccentFlags;
    DWORD           GradientColor;  // ABGR
    DWORD           AnimationId;
};

struct XB_WINDOWCOMPOSITIONATTRIBDATA {
    DWORD  Attrib;
    PVOID  pvData;
    SIZE_T cbData;
};

constexpr DWORD XB_WCA_ACCENT_POLICY = 19;

typedef BOOL (WINAPI* pfnSWCA)(HWND, XB_WINDOWCOMPOSITIONATTRIBDATA*);

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

static HINSTANCE            g_hModule       = nullptr;
static HANDLE               g_hSharedMem    = nullptr;
static SharedBlurState*     g_pState        = nullptr;
static std::atomic<bool>    g_stopping      { false };
static HANDLE               g_hWorkerThread = nullptr;
static pfnSWCA              g_pfnSWCA       = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool IsRunningInExplorer()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // Accept both "explorer.exe" and any path ending in explorer.exe
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        return _wcsicmp(slash + 1, L"explorer.exe") == 0;
    return _wcsicmp(path, L"explorer.exe") == 0;
}

static void ApplyAccentPolicy(HWND hwnd, const SharedBlurState* s)
{
    if (!hwnd || !IsWindow(hwnd) || !g_pfnSWCA) return;

    XB_ACCENT_POLICY accent = {};

    LONG enabled = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&s->blurEnabled), 0, 0);
    LONG opPct   = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&s->opacityPct), 75, 75);
    LONG r       = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&s->colorR), 0, 0);
    LONG g2      = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&s->colorG), 0, 0);
    LONG b       = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&s->colorB), 0, 0);

    if (enabled) {
        accent.AccentState = XB_ACCENT_ENABLE_ACRYLICBLUR;
        BYTE alpha = static_cast<BYTE>(((100 - opPct) * 255) / 100);
        // GradientColor: ABGR format
        accent.GradientColor = (static_cast<DWORD>(alpha) << 24)
                             | (static_cast<DWORD>(b) << 16)
                             | (static_cast<DWORD>(g2) << 8)
                             |  static_cast<DWORD>(r);
        accent.AccentFlags = 2;
        accent.AnimationId = 0;
    } else {
        accent.AccentState   = XB_ACCENT_DISABLED;
        accent.GradientColor = 0;
        accent.AccentFlags   = 0;
    }

    XB_WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib  = XB_WCA_ACCENT_POLICY;
    data.pvData  = &accent;
    data.cbData  = sizeof(accent);

    g_pfnSWCA(hwnd, &data);
}

// ---------------------------------------------------------------------------
// Worker thread — runs inside explorer.exe
// ---------------------------------------------------------------------------

static DWORD WINAPI WorkerThread(LPVOID)
{
    // Wait for shared memory to become available (Core may not have created it yet)
    for (int tries = 0; tries < 100 && !g_stopping.load(); ++tries) {
        g_hSharedMem = OpenFileMappingW(FILE_MAP_READ, FALSE, SharedBlurState::kName);
        if (g_hSharedMem) break;
        Sleep(100);
    }

    if (!g_hSharedMem) {
        // Core never created the mapping — nothing to do
        return 0;
    }

    g_pState = reinterpret_cast<SharedBlurState*>(
        MapViewOfFile(g_hSharedMem, FILE_MAP_READ, 0, 0, sizeof(SharedBlurState)));

    if (!g_pState) {
        CloseHandle(g_hSharedMem);
        g_hSharedMem = nullptr;
        return 0;
    }

    // Load SWCA function pointer from user32 (already loaded in this process)
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    g_pfnSWCA = hUser32
        ? reinterpret_cast<pfnSWCA>(GetProcAddress(hUser32, "SetWindowCompositionAttribute"))
        : nullptr;

    LONG lastVersion = -1;

    while (!g_stopping.load()) {
        // Check shutdown request from Core
        LONG shutdownReq = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&g_pState->shutdownRequest), 0, 0);
        if (shutdownReq) break;

        LONG curVersion = InterlockedCompareExchange(
            const_cast<volatile LONG*>(&g_pState->version), 0, 0);

        if (curVersion != lastVersion) {
            lastVersion = curVersion;

            // Apply to primary taskbar
            HWND hwndTray = FindWindowW(L"Shell_TrayWnd", nullptr);
            if (hwndTray) ApplyAccentPolicy(hwndTray, g_pState);

            // Apply to secondary taskbars (multi-monitor)
            HWND hwndSecondary = nullptr;
            while ((hwndSecondary = FindWindowExW(nullptr, hwndSecondary,
                                                   L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
                ApplyAccentPolicy(hwndSecondary, g_pState);
            }
        }

        Sleep(150);
    }

    // Restore default appearance on shutdown
    {
        HWND hwndTray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (hwndTray && g_pfnSWCA) {
            XB_ACCENT_POLICY restore = {};
            restore.AccentState = XB_ACCENT_DISABLED;
            XB_WINDOWCOMPOSITIONATTRIBDATA data = { XB_WCA_ACCENT_POLICY, &restore, sizeof(restore) };
            g_pfnSWCA(hwndTray, &data);
        }
    }

    UnmapViewOfFile(g_pState);
    g_pState = nullptr;
    CloseHandle(g_hSharedMem);
    g_hSharedMem = nullptr;
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

        // Only activate worker when loaded inside explorer.exe
        if (IsRunningInExplorer()) {
            g_stopping.store(false);
            g_hWorkerThread = CreateThread(
                nullptr, 0, WorkerThread, nullptr,
                0, nullptr);
        }
        break;

    case DLL_PROCESS_DETACH:
        g_stopping.store(true);
        if (g_hWorkerThread) {
            // Give worker up to 1 s to clean up; don't block if it's stuck
            WaitForSingleObject(g_hWorkerThread, 1000);
            CloseHandle(g_hWorkerThread);
            g_hWorkerThread = nullptr;
        }
        break;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Exported hook proc — passthrough, sole purpose is to trigger DLL injection
// ---------------------------------------------------------------------------

extern "C" LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
