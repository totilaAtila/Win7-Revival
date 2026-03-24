#pragma once
#include <Windows.h>

// ---------------------------------------------------------------------------
// GlassBar.XamlBridge.dll — public surface
//
// This DLL is injected into explorer.exe via SetWindowsHookEx(WH_CALLWNDPROC).
// Once resident in explorer's address space it:
//   1. Opens the shared memory block written by GlassBar.Core.dll
//   2. Starts a background thread that periodically applies the requested
//      blur / acrylic effect to Shell_TrayWnd from *inside* the owner process
//      (bypassing the cross-process restriction that makes SWCA a no-op when
//      called externally on Windows 11 22H2+).
//
// The hook proc itself is a trivial passthrough; its sole purpose is to force
// Windows to load this DLL into explorer.exe's address space.
// ---------------------------------------------------------------------------

#ifdef XAMLBRIDGE_EXPORTS
#define XBRIDGE_API __declspec(dllexport)
#else
#define XBRIDGE_API __declspec(dllimport)
#endif

extern "C" {
    // Passthrough hook proc — used only to trigger DLL injection.
    XBRIDGE_API LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam);
}
