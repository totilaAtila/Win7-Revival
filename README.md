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
- Strict module interface (`IModule`)
- JSON-based settings stored in `%AppData%/Win7Revival/`
- No Explorer.exe hooking or injection
- Fail-safe module behavior (auto-disable on error)
- DPI-aware and multi-monitor ready (future sprints)

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
- Win32-based taskbar detection (`FindWindow` / `Shell_TrayWnd`)
- Blur/transparency via `SetWindowCompositionAttribute` (Aero Glass, Acrylic, Mica Alt)
- Safe P/Invoke cu try/finally pe memorie nemanaged
- INotifyPropertyChanged pentru reactive UI binding
- Settings persistente în `%AppData%/Win7Revival/`

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
│   │   └── IModule.cs
│   ├── Models/
│   │   └── ModuleSettings.cs
│   ├── Services/
│   │   ├── CoreService.cs
│   │   └── SettingsService.cs
│   └── Win7Revival.Core.csproj
│
├── Win7Revival.Modules.Taskbar/
│   ├── Interop/
│   │   └── Win32Interop.cs
│   ├── TaskbarModule.cs
│   └── Win7Revival.Modules.Taskbar.csproj
│
├── Win7Revival.Modules.StartMenu/
│   └── Win7Revival.Modules.StartMenu.csproj  (Sprint 2)
│
├── Win7Revival.App/
│   ├── App.xaml / App.xaml.cs
│   ├── MainWindow.xaml / MainWindow.xaml.cs
│   └── Win7Revival.App.csproj
│
├── Win7Revival.Core.Tests/
│   └── (xUnit unit tests)
│
├── .gitignore
├── LICENSE
├── README.md
└── Win7Revival.sln
```

---

**Last Updated:** January 2026  
**Project Status:** Active Development
