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
|-- Win7Revival.Modules.Taskbar/ Transparent Taskbar module (dual-mode rendering)
|-- Win7Revival.App/             WinUI 3 desktop app, localization, tray icon
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

**Sprint 1 complete** — transparent taskbar, settings UI, tray, auto-start, 15 tests.

Latest changes (February 2026):
- **Dual-mode overlay rendering**: documented DWM APIs (overlay mode, update-proof) + `SetWindowCompositionAttribute` (legacy fallback). RenderMode selector in UI (Auto/Overlay/Legacy).
- **Localization**: Romanian and English with runtime language switching.
- **Tabbed settings UI**: TabView with Taskbar, Start Menu, Theme Engine, General, Help/About tabs.
- **CI/CD**: GitHub Actions with CodeQL security scanning and Dependabot for NuGet/GitHub Actions.
- **Repository security**: SECURITY.md, CODEOWNERS, branch protection guidance.

---

## Modules

### Transparent Taskbar (Sprint 1 — Live)
- `TaskbarDetector`: finds all taskbar handles (primary + secondary), positions, auto-hide state.
- `OverlayWindow`: dual-mode rendering:
  - **Overlay mode** (Win11 22H2+): creates transparent overlay windows using documented DWM APIs (`DwmExtendFrameIntoClientArea`, `DWMWA_SYSTEMBACKDROP_TYPE`). Update-proof — survives Windows feature updates.
  - **Legacy mode** (Win10/older Win11): applies blur/acrylic/mica/none via `SetWindowCompositionAttribute` in-place on taskbar handles.
  - **Auto mode**: selects overlay on build ≥22000, legacy on older builds.
- `TaskbarModule`: orchestrates detector + overlay + settings; resilient to Explorer.exe restarts via `TaskbarCreated` listener.
- Supports: blur, acrylic, mica, glass effects with opacity (0–100%) and custom RGB tint.
- Multi-monitor support with auto-hide detection.

### Classic Start Menu (Planned — Sprint 2)
- WinUI 3 menu in Windows 7 layout, optional Win key interception, search/indexing.

### Theme Engine (Planned — Sprint 3)
- Color schemes, icon packs, sound schemes, accent overrides.

---

## Features

- **Tabbed settings UI**: WinUI 3 TabView with per-module tabs, sliders, effect picker, RGB tint, render mode, diagnostics.
- **Localization**: English and Romanian with runtime language switching (ComboBox in header).
- **System tray**: H.NotifyIcon.WinUI popup menu (Show Settings / Exit), left-click restore.
- **Explorer resilience**: re-detects taskbars and re-applies effects after Explorer restarts.
- **Auto-start**: HKCU Run with `--minimized` support.
- **Settings persistence**: JSON in `%AppData%`, survives corrupt files.
- **Multi-monitor**: applies effects to all taskbars with safe handle snapshots.
- **Live preview**: opacity/effect/tint/render mode changes apply instantly.

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
|   |-- Models/ModuleSettings.cs (EffectType, RenderMode enums)
|   |-- Services/CoreService.cs, SettingsService.cs, AutoStartService.cs
|-- Win7Revival.Modules.Taskbar/
|   |-- Interop/Win32Interop.cs (user32, dwmapi, shell32, shcore P/Invoke)
|   |-- TaskbarDetector.cs, OverlayWindow.cs, TaskbarModule.cs
|-- Win7Revival.App/
|   |-- App.xaml(.cs), MainWindow.xaml(.cs), TrayIconManager.cs
|   |-- Localization/Strings.cs (EN/RO dictionaries)
|   |-- Models/AppSettings.cs (language preference)
|-- Win7Revival.Core.Tests/
|   |-- CoreServiceTests.cs, SettingsServiceTests.cs
|-- .github/
|   |-- workflows/ (dotnet-desktop.yml, codeql.yml)
|   |-- CODEOWNERS, dependabot.yml
|-- .vscode/ (build/test/publish tasks + debug launch)
|-- publish/ (self-contained output when you run the publish task)
```

---

## Security

See [SECURITY.md](SECURITY.md) for vulnerability reporting. The project uses:
- CodeQL static analysis via GitHub Actions
- Dependabot for NuGet and GitHub Actions dependency updates
- `TreatWarningsAsErrors` and `NuGetAudit` in `Directory.Build.props`

---

## License and Contributing

MIT License.
Contributions are currently limited to the internal team.

---

Last updated: February 2, 2026
Project status: Active development
