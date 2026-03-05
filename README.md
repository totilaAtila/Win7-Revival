# CrystalFrame

**Windows 11 Overlay Utility** — Transparent, color-customizable overlays for the Taskbar and Start Menu without modifying system files.

![Version](https://img.shields.io/badge/version-2.1-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2011-brightgreen)
![License](https://img.shields.io/badge/license-MIT-green)
![Build](https://github.com/totilaAtila/Win7-Revival/actions/workflows/build.yml/badge.svg)

![CrystalFrame Screenshot](assets/Crystal%20Frame.png)

---

## Features

- **Taskbar Overlay** — Semi-transparent color overlay over the Windows 11 Taskbar
- **Start Menu Overlay** — Overlay activates only when the Start Menu is open
- **RGB Color Control** — Independent R/G/B sliders for background and text color per panel
- **Opacity Control** — 0–100% adjustable per panel
- **Enable/Disable per panel** — Toggle Taskbar and Start Menu overlays independently
- **Blur / Acrylic effect** — Optional acrylic blur behind the overlay (per panel)
- **Theme Presets** — One-click presets: Classic Win7, Aero Glass, Dark
- **Border / Accent Color** — Separate color control for the Start Menu border
- **Menu Items** — Show or hide individual items in the Start Menu right column
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
- Single compact window (460×560 px), fixed size — no scroll on the window itself
- Slim header: Core Engine status, Core ON/OFF toggle, Run at Startup toggle
- Tab strip: **Taskbar** | **Start Menu** — all settings in-window, scrollable per tab
- Real-time status indicators (detection dot, connection status)

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

### Header (always visible)

| Control | Action |
|---------|--------|
| Core status dot | Green = engine running, Gray = stopped |
| Core Engine toggle | Start / stop the overlay engine |
| Startup toggle | Enable / disable Windows registry autostart |

### Taskbar tab

| Control | Action |
|---------|--------|
| Taskbar Overlay toggle | Enable / disable the Taskbar overlay |
| Transparency slider | 0–100% opacity |
| R / G / B sliders | Background color |
| Color preview bar | Live preview of the selected color |
| Blur (acrylic) toggle | Enable acrylic blur behind the overlay |

### Start Menu tab

| Control | Action |
|---------|--------|
| Start Menu Overlay toggle | Enable / disable the Start Menu overlay |
| Transparency slider | 0–100% opacity |
| Blur (acrylic) toggle | Enable acrylic blur behind the overlay |
| Background Color sliders | R / G / B for the menu background |
| Text Color sliders | R / G / B for menu text |
| Menu Items checkboxes | Show / hide individual right-column items |
| Keep Start Menu Open | Pin the menu open to preview effects in real time |
| Border / Accent Color | R / G / B for the menu border |
| Theme Presets | Classic Win7 / Aero Glass / Dark — one-click apply |

---

## Troubleshooting

**Overlay doesn't appear after enabling Core**
- Wait 1–2 seconds for detection; status indicator turns green when found
- If Taskbar not detected: restart Windows Explorer (Task Manager → Windows Explorer → Restart)
- Logs: `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log`

**Start Menu not detected**
- Expected on some Windows builds; Taskbar overlay continues to work normally
- Start overlay enables automatically when detection succeeds

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
- Compact single-window settings panel (460×560 px) with tab strip (Taskbar / Start Menu)
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
