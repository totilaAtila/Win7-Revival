# CLAUDE.md — Win7Revival Project Guide

This file provides context for AI assistants working on this codebase.

## Project Overview

Win7Revival is a modular Windows 11 customization toolkit that restores Windows 7 visual elements. Built with C# .NET 8 and WinUI 3 (Windows App SDK 1.5).

## Build & Test

```bash
dotnet build Win7Revival.sln
dotnet test Win7Revival.Core.Tests/
```

No CI/CD pipeline yet. Tests are xUnit-based, covering CoreService and SettingsService.

The App project (`Win7Revival.App`) targets `net8.0-windows10.0.19041.0` and requires Windows to build. Core and Tests target `net8.0`.

## Architecture

```
Win7Revival.Core          → IModule interface, CoreService (lifecycle), SettingsService (JSON persistence), AutoStartService (registry)
Win7Revival.Modules.Taskbar → TaskbarModule (orchestrator), TaskbarDetector (multi-monitor), OverlayWindow (accent policy), Win32Interop (P/Invoke)
Win7Revival.App           → WinUI 3 UI (MainWindow), App lifecycle, TrayIconManager, WindowIconHelper, Assets (icons, flags)
Win7Revival.Core.Tests    → xUnit tests for CoreService + SettingsService
```

### Key Patterns

- **Module lifecycle**: `InitializeAsync` → `EnableAsync` → `DisableAsync` → `SaveSettingsAsync`. All modules implement `IModule` + `INotifyPropertyChanged`.
- **Thread-safety**: `CoreService` uses `lock` on all `_modules` operations with snapshot-based iteration.
- **IDisposable cascade**: App.OnMainWindowClosed → CoreService.Dispose → TaskbarModule.Dispose → OverlayWindow.Dispose (restores ACCENT_DISABLED).
- **Memory safety**: All `Marshal.AllocHGlobal` calls wrapped in `try/finally` with `Marshal.FreeHGlobal`.
- **Settings**: JSON files in `%AppData%/Win7Revival/`. Module names sanitized via regex to prevent path traversal.
- **UI guard**: `_isInitializing` flag prevents toggle event handlers from firing during UI setup.
- **Exception propagation**: `CoreService.EnableModuleAsync` re-throws after cleanup so UI can show error dialogs and revert toggles.
- **CancellationToken**: All `IModule` async methods accept optional `CancellationToken`.

### Win32 Interop

The project uses `SetWindowCompositionAttribute` with `ACCENT_POLICY` to apply transparency effects directly to taskbar window handles (`Shell_TrayWnd` + `Shell_SecondaryTrayWnd`). No overlay windows are created — effects are applied in-place.

Key P/Invoke surface:
- `user32.dll`: FindWindow, FindWindowEx, SetWindowCompositionAttribute, EnumDisplayMonitors, GetMonitorInfo, IsWindow, GetWindowRect, ShowWindow, SetForegroundWindow, SendMessage, LoadImage
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
- **TrayIconManager** uses H.NotifyIcon.WinUI with custom `tray.ico`. MinimizeToTray gracefully degrades to SW_MINIMIZE if initialization fails.
- **EffectType.None** maps to `ACCENT_DISABLED` (no effect applied)
- Settings files must survive corruption (LoadSettings returns defaults on invalid JSON)

### Assets

```
Win7Revival.App/Assets/
  app.ico         → Window icon (taskbar, title bar) — multi-size ICO (16, 24, 32, 48, 256)
  tray.ico        → System tray icon — multi-size ICO (16, 20, 24, 32)
  Flags/
    en.png        → English flag for language selector
    ro.png        → Romanian flag for language selector
```

- `WindowIconHelper` sets the window icon via `WM_SETICON` + `LoadImage` P/Invoke (WinUI 3 has no native API for this).
- `app.manifest` with `requestedExecutionLevel="asInvoker"` prevents UAC elevation prompt.
- Language selector uses flag images only (no text labels) in the ComboBox.
- TabView uses `IsClosable="False"` and `IsAddTabButtonVisible="False"` — tabs are for navigation, not closable.

## Known Future Work

- Explorer restart resilience (`RegisterWindowMessage("TaskbarCreated")`)
- TaskbarModule unit tests (needs `ITaskbarDetector` interface for mocking)
- Classic Start Menu module (Sprint 2)
- Theme Engine module (Sprint 3)
- CI/CD with GitHub Actions
