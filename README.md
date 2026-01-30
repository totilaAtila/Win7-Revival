# Win7 Revival

**A modular Windows 11 customization toolkit designed to bring back the visual style and usability of Windows 7 — clean, stable, and non-intrusive.**

---

## Overview

Win7 Revival is a modular desktop enhancement application for Windows 11.
Its goal is to restore classic Windows 7 elements while maintaining full system stability and compatibility with modern Windows builds.

The project is built around a strict modular architecture:
- Each feature is a standalone module implementing `IModule`
- Modules can be enabled/disabled independently with live settings updates
- No system patching, no DLL injection, no Explorer.exe modification
- Thread-safe lifecycle management with `IDisposable` cascade cleanup

---

## Architecture

```
Win7Revival/
├── Win7Revival.Core/              # Core services, module lifecycle, settings
├── Win7Revival.Modules.Taskbar/   # Transparent Taskbar module
├── Win7Revival.App/               # WinUI 3 desktop app + tray icon
└── Win7Revival.Core.Tests/        # xUnit tests (15 tests)
```

### Core Principles

- .NET 8 + WinUI 3 (Windows App SDK 1.5)
- Strict module interface (`IModule` + `INotifyPropertyChanged` + `CancellationToken`)
- JSON-based settings stored in `%AppData%/Win7Revival/` with path traversal sanitization
- Thread-safe `CoreService` with lock-based synchronization and snapshot iteration
- `IDisposable` cascade: App → CoreService → TaskbarModule → OverlayWindow
- Memory safety: `try/finally` on all `Marshal.AllocHGlobal` calls
- Exception propagation: `EnableModuleAsync` re-throws after cleanup for UI error handling
- Auto-start at Windows boot via HKCU registry (no admin required)

---

## 🚀 Current Status

**Sprint 1 – Complete**

Deliverables:
- Core architecture (module lifecycle, settings persistence, thread-safe CoreService)
- Module system (IModule interface with CancellationToken support)
- Transparent Taskbar module (blur/acrylic/mica, multi-monitor, live updates)
- Custom RGB color tint (user-configurable R/G/B sliders with live preview)
- Explorer restart resilience (TaskbarCreated message listener, auto-reapply)
- WinUI 3 Settings UI (Expander, Slider, ComboBox, color tint, diagnostics)
- System tray icon (H.NotifyIcon.WinUI with context menu and restore)
- Auto-start at boot with `--minimized` tray support
- 15 unit tests (CoreService + SettingsService)

---

## Modules

### Transparent Taskbar (Sprint 1 — Complete)
- **TaskbarDetector**: Multi-monitor discovery (primary `Shell_TrayWnd` + secondary `Shell_SecondaryTrayWnd`), position query, auto-hide detection, monitor enumeration via `EnumDisplayMonitors`
- **OverlayWindow**: Applies accent policy (Blur/Acrylic/Mica/None) via `SetWindowCompositionAttribute` on all taskbar handles with configurable opacity (0-100%) and custom RGB color tint
- **TaskbarModule**: Orchestrator — coordinates Detector + Overlay + Settings with live `UpdateSettings()` support
- **Explorer restart resilience**: Background STA thread listens for `TaskbarCreated` window message, auto-refreshes handles and reapplies effects when Explorer.exe restarts
- Safe handle snapshots prevent iterator invalidation during Explorer restarts
- `IDisposable` cleanup restores original taskbar state (`ACCENT_DISABLED`)

### Classic Start Menu (Planned — Sprint 2)
- Custom WinUI 3 menu with Windows 7-style layout
- Win key interception (optional)
- App indexing + search

### Theme Engine (Planned — Sprint 3)
- Color schemes, accent color override, icon packs, sound schemes

---

## Features

- **Rich WinUI 3 UI**: Expander, Slider, ComboBox, RGB color tint, diagnostics panel, admin warning
- **System tray**: H.NotifyIcon.WinUI with context menu, double-click restore, balloon notifications
- **Explorer resilience**: Taskbar effects auto-reapply after Explorer.exe crash/restart
- **Auto-start**: Registry-based start with Windows, launches minimized to tray
- **Settings persistence**: JSON in `%AppData%`, survives corrupt files
- **Multi-monitor**: Detects and applies effects to all taskbars
- **Live preview**: Opacity slider, effect type, and color tint changes apply instantly

---

## Tech Stack

- **Language:** C# (.NET 8)
- **UI:** WinUI 3 (Windows App SDK 1.5)
- **Interop:** Win32 API (25+ P/Invoke declarations)
- **Tray Icon:** H.NotifyIcon.WinUI
- **Testing:** xUnit (15 tests — CoreService + SettingsService)
- **Settings:** System.Text.Json
- **Version Control:** GitHub

---

## Repository Structure

```
Win7Revival/
├── Win7Revival.Core/
│   ├── Interfaces/
│   │   └── IModule.cs              # IModule + INotifyPropertyChanged + CancellationToken
│   ├── Models/
│   │   └── ModuleSettings.cs       # Name, IsEnabled, Opacity, EffectType, TintR/G/B
│   ├── Services/
│   │   ├── CoreService.cs          # Thread-safe module lifecycle + IDisposable
│   │   ├── SettingsService.cs      # %AppData% JSON persistence + sanitization
│   │   └── AutoStartService.cs     # HKCU registry auto-start + --minimized
│   └── Win7Revival.Core.csproj
│
├── Win7Revival.Modules.Taskbar/
│   ├── Interop/
│   │   └── Win32Interop.cs         # P/Invoke: composition, window, monitor, DPI, appbar, messages
│   ├── TaskbarDetector.cs          # Multi-monitor taskbar discovery + position/auto-hide
│   ├── OverlayWindow.cs            # Accent policy application (blur/acrylic/mica) + RGB tint
│   ├── TaskbarModule.cs            # Orchestrator: Detector + Overlay + Settings + Explorer monitor
│   └── Win7Revival.Modules.Taskbar.csproj
│
├── Win7Revival.App/
│   ├── App.xaml / App.xaml.cs       # Entry point, lifecycle, --minimized support
│   ├── MainWindow.xaml / .xaml.cs   # Rich settings UI (Expander, Slider, ComboBox, RGB tint)
│   ├── TrayIconManager.cs          # H.NotifyIcon.WinUI tray icon + context menu
│   └── Win7Revival.App.csproj
│
├── Win7Revival.Core.Tests/
│   ├── CoreServiceTests.cs          # 7 tests: register, enable, disable, failure cleanup
│   ├── SettingsServiceTests.cs      # 8 tests: round-trip, corrupt, sanitize, validation
│   └── Win7Revival.Core.Tests.csproj
│
├── .gitignore
├── CLAUDE.md
├── README.md
└── Win7Revival.sln
```

---

## License

This project is licensed under the MIT License.

---

## Contributing

This repository is currently under active development.
Contributions are limited to the internal development team.

---

**Last Updated:** January 2026
**Project Status:** Active Development
