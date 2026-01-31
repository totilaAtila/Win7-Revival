# Win7 Revival

Modular Windows 11 customization to bring back the Windows 7 look-and-feel without hacks, patching, or instability.

---

## Overview

Win7 Revival is a WinUI 3 desktop app built around a strict module system. Each feature is an `IModule` that can be enabled, disabled, or updated live without touching Explorer.exe or injecting DLLs. Settings are JSON-backed, thread-safe, and resilient to corruption.

---

## Architecture

```
Win7Revival/
|-- Win7Revival.Core/            Core services, module lifecycle, settings
|-- Win7Revival.Modules.Taskbar/ Transparent Taskbar module
|-- Win7Revival.App/             WinUI 3 desktop app + tray icon
|-- Win7Revival.Core.Tests/      xUnit tests (CoreService, SettingsService)
```

Core principles:
- .NET 8 + WinUI 3 (Windows App SDK 1.5)
- Modules implement `IModule`, `INotifyPropertyChanged`, and accept `CancellationToken`
- JSON settings stored in `%AppData%/Win7Revival/` with path sanitization
- Thread-safe `CoreService` with lock-based lifecycle + `IDisposable` cascade
- No system patching, no Explorer.exe modification, no DLL injection
- Auto-start via HKCU Run key (no admin)

---

## Current Status

Sprint 1 shipped (transparent taskbar, settings UI, tray, auto-start, 15 tests).  
January 31, 2026 stabilization patch:
- Taskbar overlay now re-applies its accent policy on a 100 ms timer to resist Windows resetting the effect (e.g., opening Start menu).
- Tray icon uses `PopupMenu`/`ICommand` for better Windows 11 compatibility; left-click restores the window.
- Windows App SDK bumped to 1.5.240627; WinUI app publishes self-contained `win-x64` (no MSIX).
- Solution adds x64 configs; VS Code tasks added for build/test/publish.

---

## Modules

### Transparent Taskbar (Sprint 1)
- `TaskbarDetector`: finds all taskbar handles (primary + secondary), positions, auto-hide state.
- `OverlayWindow`: applies blur/acrylic/mica/none with opacity (0–100%) and custom RGB tint; now auto re-applies the effect periodically to stay active.
- `TaskbarModule`: orchestrates detector + overlay + settings; resilient to Explorer.exe restarts via `TaskbarCreated` listener; marked with `[SupportedOSPlatform("windows")]`.

### Classic Start Menu (Planned – Sprint 2)
- WinUI 3 menu in Windows 7 layout, optional Win key interception, search/indexing.

### Theme Engine (Planned – Sprint 3)
- Color schemes, icon packs, sound schemes, accent overrides.

---

## Features

- WinUI 3 settings UI: Expander, sliders, effect picker, RGB tint, diagnostics.
- System tray: H.NotifyIcon.WinUI popup menu (Show Settings / Exit), left-click restore.
- Explorer resilience: re-detects taskbars and re-applies effects after Explorer restarts.
- Auto-start: HKCU Run with `--minimized` support.
- Settings persistence: JSON in `%AppData%`, survives corrupt files.
- Multi-monitor: applies effects to all taskbars with safe handle snapshots.
- Live preview: opacity/effect/tint changes apply instantly.

---

## Build and Run

Prerequisites: .NET 8 SDK, Windows 11 (tested on 23H2+).

- Restore: `dotnet restore Win7Revival.sln`
- Build (Debug): `dotnet build Win7Revival.sln -c Debug -p:Platform=x64`
- Run (Debug): `dotnet run --project Win7Revival.App -- -?`
- Tests: `dotnet test Win7Revival.Core.Tests`

VS Code tasks mirror these (`.vscode/tasks.json`): `build`, `test`, `publish`, `clean`, `restore`.

---

## Publish (self-contained)

Produces a standalone `Win7Revival.App.exe` (no MSIX, no external runtime):

```
dotnet publish Win7Revival.App -c Release -r win-x64 -p:Platform=x64 --output publish
```

Result is in `publish/` with all WinAppSDK dependencies for offline install.

---

## Repository Structure

```
Win7Revival/
|-- Win7Revival.Core/
|   |-- Interfaces/IModule.cs
|   |-- Models/ModuleSettings.cs
|   |-- Services/CoreService.cs, SettingsService.cs, AutoStartService.cs
|-- Win7Revival.Modules.Taskbar/
|   |-- Interop/Win32Interop.cs
|   |-- TaskbarDetector.cs, OverlayWindow.cs, TaskbarModule.cs
|-- Win7Revival.App/
|   |-- App.xaml(.cs), MainWindow.xaml(.cs), TrayIconManager.cs
|-- Win7Revival.Core.Tests/
|   |-- CoreServiceTests.cs, SettingsServiceTests.cs
|-- .vscode/ (build/test/publish tasks + debug launch)
|-- publish/ (self-contained output when you run the publish task)
```

---

## License and Contributing

MIT License.  
Contributions are currently limited to the internal team.

---

Last updated: January 31, 2026  
Project status: Active development
