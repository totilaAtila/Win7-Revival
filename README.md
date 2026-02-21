# CrystalFrame

**Windows 11 Overlay Utility** — Transparent, color-customizable overlays for the Taskbar and Start Menu without modifying system files.

![Version](https://img.shields.io/badge/version-2.1-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2011-brightgreen)
![License](https://img.shields.io/badge/license-MIT-green)
![Build](https://github.com/totilaAtila/Win7-Revival/actions/workflows/build.yml/badge.svg)

---

## Features

- **Taskbar Overlay** — Semi-transparent color overlay over the Windows 11 Taskbar
- **Start Menu Overlay** — Overlay activates only when the Start Menu is open
- **RGB Color Control** — Independent R/G/B sliders for background and text color per panel
- **Opacity Control** — 0–100% adjustable per panel
- **Enable/Disable per panel** — Toggle Taskbar and Start Menu overlays independently
- **Run at Startup** — Optional auto-start via Windows registry; starts silently in System Tray
- **System Tray icon** — Minimize to tray; right-click menu (Open / Exit); double-click to restore
- **System Theme Support** — Follows Windows light/dark theme automatically
- **Click-Through** — Full Taskbar and Start Menu functionality preserved
- **Explorer Restart Recovery** — Auto re-detects Taskbar/Start after Explorer crashes
- **No Injection** — External overlay only; no system file modifications

---

## Architecture

```
CrystalFrame.Dashboard.exe  (C# .NET 8, WinUI 3)
        │  P/Invoke (direct calls)
        ▼
CrystalFrame.Core.dll  (C++20, Win32)
```

**CrystalFrame.Core** — Native DLL (C++20)
- DirectComposition rendering (hardware-accelerated)
- Shell target detection (Taskbar / Start Menu via UI Automation)
- Overlay window management (click-through, layered)
- Configuration persistence (`%LOCALAPPDATA%\CrystalFrame\config.json`)
- Explorer restart recovery (WinEvent hook)

**CrystalFrame.Dashboard** — Settings UI (C# .NET 8, WinUI 3)
- Compact main window: Core on/off toggle, Run at Startup, panel navigation
- Separate DetailWindow per panel (Taskbar / Start Menu settings)
- Dynamic window sizing (SizeToContent via `UIElement.Measure`)
- Real-time status indicators

### Technology Stack

| Layer | Technology |
|-------|-----------|
| Core engine | C++20, DirectComposition, Direct2D, Win32 API |
| Dashboard UI | C# .NET 8, WinUI 3, XAML |
| Core↔Dashboard | P/Invoke (direct DLL calls, no IPC) |
| Build | CMake (Core), dotnet CLI / MSBuild (Dashboard) |

---

## Quick Start

### Prerequisites

- **Windows 11** (22H2 or later recommended)
- **Visual Studio 2022** with C++ Desktop workload (for Core)
- **.NET 8 SDK**
- **CMake 3.20+**

### Build

**Core (C++):**
```cmd
cd Core
cmake -B build -A x64
cmake --build build --config Release
```

**Dashboard (C#):**
```cmd
cd Dashboard
dotnet build -p:Platform=x64 --configuration Release
```

### Run

```cmd
Dashboard\bin\x64\Release\net8.0-windows10.0.22621.0\CrystalFrame.Dashboard.exe
```

The Dashboard automatically locates and loads `CrystalFrame.Core.dll` from the same directory.
Click **Core Engine** toggle → ON to start the overlay engine.

---

## Usage

### Main Window

| Control | Action |
|---------|--------|
| **Taskbar** button | Open Taskbar settings panel |
| **Start Menu** button | Open Start Menu settings panel |
| Core Engine toggle | Start / stop the overlay engine |
| Run at startup toggle | Enable / disable Windows registry autostart |

### Detail Window (per panel)

| Control | Action |
|---------|--------|
| Panel enable toggle | Enable / disable this overlay |
| Transparency slider | 0–100% opacity |
| R / G / B sliders | Background color (and Text Color for Start Menu) |
| Color preview bar | Live preview of the selected color |
| Menu Items checkboxes | Choose which items appear in Start Menu *(Start Menu panel only)* |

---

## Troubleshooting

**Overlay doesn't appear after enabling Core**
- Wait 1–2 seconds for detection; status indicator turns green when found
- If Taskbar not detected: restart Windows Explorer (Task Manager → Windows Explorer → Restart)
- Logs: `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log`

**Start Menu not detected**
- Expected on some Windows builds; Taskbar overlay continues to work normally
- Start overlay enables automatically when detection succeeds

**DetailWindow appears off-screen**
- Fixed in v2.1 — window is now clamped to the display work area on all edges

---

## Performance

| Metric | Target | Measured |
|--------|--------|----------|
| CPU (idle) | < 2% | ~0.5% |
| Memory | < 50 MB | ~30 MB |
| Startup | < 2 s | ~1 s |
| Opacity change latency | < 50 ms | ~16 ms |

---

## Roadmap

### Done
- Taskbar overlay (all edges + auto-hide support)
- Start Menu overlay (auto-detect open/close)
- Per-channel RGB color control
- Config persistence (JSON)
- Explorer restart recovery
- Run at Startup (registry) — starts hidden in System Tray when launched at boot
- System theme (light/dark) support
- Separate DetailWindow with dynamic sizing
- System tray icon — minimize to tray; right-click menu (Open / Exit); double-click to restore
- Win7-style Start Menu — two-column layout (programs list + system links), All Programs hierarchical tree with folder drill-down, keyboard navigation (Up/Down/Enter/Esc), mouse-wheel scroll, hover-to-open lateral submenus, search box, power menu

### Planned
- **Multi-monitor support** — overlay on non-primary displays
- **Material effects** — blur / acrylic behind overlays
- **Global hotkey** — toggle overlays without opening Dashboard
- **Color presets** — quick-select common themes (Aero Glass, Dark, etc.)
- **Auto-update check** — notify when a new GitHub release is available

---

## Contributing

Contributions welcome via pull requests. Please open an issue first for larger changes.

---

## License

MIT — see [LICENSE](LICENSE) for details.

---

**Made for Windows 11 customization enthusiasts**
