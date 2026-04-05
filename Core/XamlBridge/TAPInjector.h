//
// TAPInjector.h — InjectGlassBarTAP declaration  [ITER #19]
//
#pragma once

// Loops through VisualDiagConnection1..N, calling InitializeXamlDiagnosticsEx
// for each available XAML island in the current process (explorer.exe).
// Must be called from within the target process (in-process injection).
// Returns immediately if already successfully initialised (g_tapInited=true).
// Leaves g_tapInited=false if no islands registered, allowing retry by the caller.
void InjectGlassBarTAP();

// Returns true once at least one island has been successfully registered.
// Safe to call from any thread.
bool IsTapInited();
