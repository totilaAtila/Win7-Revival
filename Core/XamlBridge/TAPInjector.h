//
// TAPInjector.h — InjectGlassBarTAP declaration  [ITER #11]
//
#pragma once

// Loops through VisualDiagConnection1..N, calling InitializeXamlDiagnosticsEx
// for each available XAML island in the current process (explorer.exe).
// Must be called from within the target process (in-process injection).
void InjectGlassBarTAP();
