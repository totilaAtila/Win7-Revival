# CLAUDE.md — Win7Revival Project Guide

This file provides context for AI assistants working on this codebase.

## Project Overview

Win7Revival is a modular Windows 11 customization toolkit that restores Windows 7 visual elements. Built with C# .NET 8 and WinUI 3 (Windows App SDK 1.5).

## Build & Test

```bash
dotnet build Win7Revival.sln
dotnet test Win7Revival.Core.Tests/
```

CI runs via GitHub Actions (`dotnet-desktop.yml` for build/test, `codeql.yml` for security scanning). Tests are xUnit-based, covering CoreService and SettingsService.

The App project (`Win7Revival.App`) targets `net8.0-windows10.0.19041.0` and requires Windows to build. Core and Tests target `net8.0`.

## Architecture

```
Win7Revival.Core            → IModule interface, CoreService (lifecycle), SettingsService (JSON persistence), AutoStartService (registry)
Win7Revival.Modules.Taskbar → TaskbarModule (orchestrator), TaskbarDetector (multi-monitor), OverlayWindow (dual-mode rendering), Win32Interop (P/Invoke)
Win7Revival.App             → WinUI 3 UI (MainWindow with TabView), Localization (EN/RO), App lifecycle, TrayIconManager
Win7Revival.Core.Tests      → xUnit tests for CoreService + SettingsService
```

### Key Patterns

- **Module lifecycle**: `InitializeAsync` → `EnableAsync` → `DisableAsync` → `SaveSettingsAsync`. All modules implement `IModule` + `INotifyPropertyChanged`.
- **Thread-safety**: `CoreService` uses `lock` on all `_modules` operations with snapshot-based iteration.
- **IDisposable cascade**: App.OnMainWindowClosed → CoreService.Dispose → TaskbarModule.Dispose → OverlayWindow.Dispose (restores ACCENT_DISABLED or destroys overlay windows).
- **Memory safety**: All `Marshal.AllocHGlobal` calls wrapped in `try/finally` with `Marshal.FreeHGlobal`.
- **Settings**: JSON files in `%AppData%/Win7Revival/`. Module names sanitized via regex to prevent path traversal.
- **UI guard**: `_isInitializing` flag prevents toggle event handlers from firing during UI setup.
- **Exception propagation**: `CoreService.EnableModuleAsync` re-throws after cleanup so UI can show error dialogs and revert toggles.
- **CancellationToken**: All `IModule` async methods accept optional `CancellationToken`.
- **Localization**: Static EN/RO string dictionaries in `Localization/Strings.cs`, runtime switching via `ApplyLanguage()`. Language preference persisted in `AppSettings.cs`.

### Dual-Mode Rendering (OverlayWindow)

The taskbar module supports two rendering modes, selectable via `RenderMode` enum (Auto/Overlay/Legacy):

- **Overlay mode** (documented DWM APIs — update-proof):
  - Creates transparent `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE` popup windows positioned over each taskbar.
  - Applies effects via `DwmExtendFrameIntoClientArea`, `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` (build ≥22621), or `DwmEnableBlurBehindWindow` (build ≥22000).
  - Runs on a dedicated STA thread with Win32 message pump (`GetMessage`/`DispatchMessage`).
  - `SetTimer` at 100ms for repositioning, auto-hide detection (≤2px), z-order re-assertion, new monitor detection.

- **Legacy mode** (undocumented — may break on Windows updates):
  - Uses `SetWindowCompositionAttribute` with `ACCENT_POLICY` applied in-place to taskbar window handles (`Shell_TrayWnd` + `Shell_SecondaryTrayWnd`).
  - 100ms timer re-applies accent to resist Windows resetting the effect.

- **Auto mode**: selects overlay on build ≥22000, legacy on older builds.

### Win32 Interop

Key P/Invoke surface in `Win32Interop.cs`:
- `user32.dll`: FindWindow, FindWindowEx, SetWindowCompositionAttribute, EnumDisplayMonitors, GetMonitorInfo, IsWindow, GetWindowRect, ShowWindow, SetForegroundWindow, CreateWindowEx, RegisterClass, DefWindowProc, GetMessage, TranslateMessage, DispatchMessage, PostMessage, SetWindowPos, SetTimer, KillTimer, DestroyWindow, UnregisterClass
- `dwmapi.dll`: DwmExtendFrameIntoClientArea, DwmSetWindowAttribute, DwmEnableBlurBehindWindow
- `shell32.dll`: SHAppBarMessage (taskbar position, auto-hide)
- `shcore.dll`: GetDpiForMonitor

## Code Conventions

- Language: C# with nullable enabled, implicit usings
- Naming: PascalCase for public members, _camelCase for private fields
- Comments: XML doc on public APIs, inline comments in Romanian or English
- Debug output: `Debug.WriteLine` with `[ClassName]` prefix tags
- Error handling: try/catch with Debug.WriteLine logging, never silently swallow exceptions in public APIs
- No `Console.WriteLine` — this is a WinUI 3 app without a console

## Important Constraints

- **No Explorer.exe hooking or DLL injection** — only public Win32 APIs
- **No admin required** for core functionality (auto-start uses HKCU, not HKLM)
- **TrayIconManager** is a skeleton — `_isInitialized` stays false until H.NotifyIcon.WinUI is integrated. MinimizeToTray gracefully degrades to SW_MINIMIZE.
- **EffectType.None** maps to `ACCENT_DISABLED` (legacy) or no DWM effect (overlay)
- **RenderMode.Auto** is the default — users only need to change it if they have specific compatibility needs
- Settings files must survive corruption (LoadSettings returns defaults on invalid JSON)

## Known Future Work

- H.NotifyIcon.WinUI integration for real system tray icon
- TaskbarModule unit tests (needs `ITaskbarDetector` interface for mocking)
- Classic Start Menu module (Sprint 2)
- Theme Engine module (Sprint 3)
- Additional localization languages
- Taskbar color tint improvements (gradient, per-monitor tint)
