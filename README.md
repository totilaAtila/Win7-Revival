# 📘 Win7 Revival

**A modular Windows 11 customization toolkit designed to bring back the visual style and usability of Windows 7 — clean, stable, and non-intrusive.**

---

## ✨ Overview

Win7 Revival is a modular desktop enhancement application for Windows 11.  
Its goal is to restore classic Windows 7 elements while maintaining full system stability and compatibility with modern Windows builds.

The project is built around a strict modular architecture:
- Each feature is a standalone module
- Modules can be enabled/disabled independently
- No system patching, no DLL injection, no Explorer.exe modification

The first two modules under development:
- **Transparent Taskbar** (Sprint 1)
- **Classic Start Menu** (Sprint 2)

---

## 🧱 Architecture

The solution is divided into four main projects:

```
Win7Revival/
├── Win7Revival.Core/              # Core service, module lifecycle, settings
├── Win7Revival.Modules.Taskbar/   # Transparent Taskbar module
├── Win7Revival.Modules.StartMenu/ # Classic Start Menu module (future)
└── Win7Revival.App/               # WinUI 3 desktop app + tray icon
```

### Core Principles

- .NET 8 + WinUI 3 (Windows App SDK)
- Strict module interface (`IModule` + `INotifyPropertyChanged`)
- JSON-based settings stored in `%AppData%/Win7Revival/`
- No Explorer.exe hooking or injection
- Fail-safe module behavior (auto-disable on error)
- DPI-aware and multi-monitor ready via `TaskbarDetector` + `DpiHelper`
- Thread-safe `CoreService` with `IDisposable` cascade cleanup

---

## 🚀 Current Status

**Sprint 1 – In Progress**

Deliverables:
- Core architecture
- Module system
- Transparent Taskbar (POC)
- Minimal Settings UI
- Tray icon

---

## 📦 Modules

### Transparent Taskbar (Sprint 1)
- `TaskbarDetector`: multi-monitor detection (primary + secondary taskbars), position, auto-hide query
- `OverlayWindow`: applies blur/acrylic/mica via `SetWindowCompositionAttribute` on all taskbars
- `DpiHelper`: per-monitor DPI scaling for correct overlay positioning
- Configurable opacity (0-100%) and effect type (Blur, Acrylic, Mica)
- Live update: slider/combobox changes apply instantly without disable/enable
- Safe P/Invoke: `try/finally` on all `Marshal.AllocHGlobal` calls
- `INotifyPropertyChanged` for reactive UI binding
- `IDisposable` cascade: module cleanup restores original taskbar state
- Rich WinUI 3 Settings UI: Expander, Slider, ComboBox, diagnostics panel
- System tray icon skeleton (`TrayIconManager`) with minimize-to-tray support

### Classic Start Menu (Sprint 2)
- Custom WinUI 3 menu
- Win key interception (optional)
- App indexing + search
- Windows 7-style layout

---

## 🛠️ Tech Stack

- **Language:** C# (.NET 8)
- **UI:** WinUI 3
- **Interop:** Win32 API (FindWindow, GetWindowRect, etc.)
- **Packaging:** MSIX / Installer (future)
- **Version Control:** GitHub (private)

---

## 📄 License

This project is licensed under the MIT License.  
See the LICENSE file for details.

---

## 🤝 Contributing

This repository is currently private and under active development.  
Contributions are limited to the internal development team.

---

## 📬 Contact

For coordination, architecture decisions, or module integration questions, please contact the project coordinators.

---

## 📁 Structura Repository

```
Win7Revival/
├── Win7Revival.Core/
│   ├── Interfaces/
│   │   └── IModule.cs              # IModule + INotifyPropertyChanged + Version
│   ├── Models/
│   │   └── ModuleSettings.cs       # Name, IsEnabled, Opacity, EffectType
│   ├── Services/
│   │   ├── CoreService.cs          # Thread-safe module lifecycle + IDisposable
│   │   └── SettingsService.cs      # %AppData% JSON persistence + sanitization
│   └── Win7Revival.Core.csproj
│
├── Win7Revival.Modules.Taskbar/
│   ├── Interop/
│   │   └── Win32Interop.cs         # P/Invoke: composition, window, monitor, DPI, appbar
│   ├── TaskbarDetector.cs           # Multi-monitor taskbar discovery + position/auto-hide
│   ├── DpiHelper.cs                 # Per-monitor DPI scaling utilities
│   ├── OverlayWindow.cs            # Accent policy application (blur/acrylic/mica)
│   ├── TaskbarModule.cs            # Orchestrator: Detector + Overlay + Settings
│   └── Win7Revival.Modules.Taskbar.csproj
│
├── Win7Revival.Modules.StartMenu/
│   └── Win7Revival.Modules.StartMenu.csproj  (Sprint 2)
│
├── Win7Revival.App/
│   ├── App.xaml / App.xaml.cs       # Entry point, lifecycle, tray integration
│   ├── MainWindow.xaml / .xaml.cs   # Rich settings UI (Expander, Slider, ComboBox)
│   ├── TrayIconManager.cs          # System tray icon + minimize/restore
│   └── Win7Revival.App.csproj
│
├── Win7Revival.Core.Tests/
│   ├── CoreServiceTests.cs          # 7 tests: register, enable, disable, failure cleanup
│   ├── SettingsServiceTests.cs      # 8 tests: round-trip, corrupt, sanitize, opacity/effect
│   └── Win7Revival.Core.Tests.csproj
│
├── .gitignore
├── LICENSE
├── README.md
└── Win7Revival.sln
```

---

**Last Updated:** January 2026  
**Project Status:** Active Development
