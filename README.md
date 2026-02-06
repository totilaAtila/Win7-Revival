# CrystalFrame Engine

**Windows 11 Overlay Utility** - Apply customizable transparent overlays over Taskbar and Start Menu without modifying system files.

![Version](https://img.shields.io/badge/version-1.0.0-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2011-brightgreen)
![License](https://img.shields.io/badge/license-MIT-green)

---

## 🎯 Features

- ✅ **Taskbar Overlay** - Semi-transparent overlay over Windows 11 Taskbar
- ✅ **Start Menu Overlay** - Overlay appears only when Start Menu is open
- ✅ **Opacity Control** - 0-100% adjustable opacity via sliders
- ✅ **Auto-Hide Support** - Detects and tracks auto-hide taskbar
- ✅ **All Edges** - Works with taskbar on bottom, top, left, or right
- ✅ **Click-Through** - Full taskbar/start functionality preserved
- ✅ **Explorer Restart Recovery** - Automatically re-detects after Explorer crashes
- ✅ **Performance Optimized** - CPU usage < 2% idle
- ✅ **No Injection** - External overlay only, no system modifications

---

## 📁 Architecture

### Components

**CrystalFrame.Core** (C++20)
- DirectComposition rendering
- Shell target detection (Taskbar/Start)
- Overlay window management
- IPC server (Named Pipes)
- Configuration persistence

**CrystalFrame.Dashboard** (C# .NET 8, WinUI 3)
- Settings UI (sliders, toggles)
- IPC client
- Real-time status display
- Config management

### Technology Stack

- **Core:** C++20, DirectComposition, Direct2D, Win32 API
- **Dashboard:** .NET 8, WinUI 3, XAML
- **IPC:** Named Pipes (JSON messages)
- **Build:** CMake (Core), dotnet CLI (Dashboard)

---

## 🚀 Quick Start

### Prerequisites

- **Windows 11** (22H2 or later)
- **Visual Studio 2022** (for C++ compiler)
- **.NET 8 SDK**
- **CMake** (3.20+)

### Build

See `docs/BUILD.md` for detailed build instructions.

**Core (C++):**
```cmd
cd Core
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

**Dashboard (C#):**
```cmd
cd Dashboard
dotnet build --configuration Release
```

### Run

1. **Start Core:**
   ```cmd
   Core/build/bin/Release/CrystalFrame.Core.exe
   ```

2. **Start Dashboard:**
   ```cmd
   Dashboard/bin/Release/net8.0-windows/CrystalFrame.Dashboard.exe
   ```

3. **Adjust opacity sliders** - Changes apply in real-time!

---

## 📖 Documentation

- **[VSCode Setup Guide](docs/VSCODE-SETUP.md)** - Complete setup for Visual Studio Code
- **[Build Instructions](docs/BUILD.md)** - Detailed build steps
- **[Testing Guide](docs/TESTING.md)** - Test scenarios and validation
- **[Agent Architecture](docs/Agents.md)** - Technical architecture document

---

## 🎮 Usage

### Dashboard Controls

**Taskbar Overlay:**
- Toggle: Enable/Disable overlay
- Slider: 0-100% opacity

**Start Menu Overlay:**
- Toggle: Enable/Disable overlay
- Slider: 0-100% opacity

**Status Indicators:**
- ✓ Taskbar found / ⚠ Not detected
- ✓ Start detected / ⚠ Not detected
- ✓ Connected to Core / ✗ Connection failed

### Keyboard Shortcuts (Future)
- Currently no hotkeys implemented
- Roadmap includes global hotkey toggle

---

## 🔍 Troubleshooting

### Overlay doesn't appear
- Ensure Core is running (check Task Manager)
- Check logs: `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log`
- Verify Windows 11 (not Windows 10)

### Dashboard can't connect
- Core must be running first
- Check firewall isn't blocking Named Pipes
- Restart both Core and Dashboard

### Start Menu not detected
- This is expected on some Windows builds
- Start overlay automatically disables if detection fails
- Taskbar overlay continues to work

### Performance issues
- Check CPU usage in Task Manager
- Should be < 2% when idle
- Verify DirectComposition is hardware accelerated

---

## 📊 Performance Targets

| Metric | Target | Actual |
|--------|--------|--------|
| CPU (Idle) | < 2% | ~0.5% |
| Memory | < 50 MB | ~30 MB |
| Startup Time | < 2 sec | ~1 sec |
| Opacity Change | < 50 ms | ~16 ms |

---

## 🛣️ Roadmap

### Completed (v1.0)
- ✅ Taskbar overlay (all edges)
- ✅ Auto-hide support
- ✅ Start Menu overlay
- ✅ Config persistence
- ✅ IPC communication
- ✅ Explorer restart recovery

### Planned (v1.1+)
- ⏳ Material effects (blur)
- ⏳ Hotkey toggle
- ⏳ Opacity presets (0/25/50/75/100)
- ⏳ Multi-monitor support
- ⏳ Auto-start on boot
- ⏳ System tray icon

---

## 🤝 Contributing

Currently a personal project. Contributions welcome via pull requests!

### Development Setup
1. Read `docs/VSCODE-SETUP.md`
2. Install prerequisites
3. Build both Core and Dashboard
4. Run tests from `docs/TESTING.md`

---

## 📄 License

MIT License - See LICENSE file for details.

---

## 🙏 Acknowledgments

- **DirectComposition API** - Microsoft Windows composition engine
- **WinUI 3** - Modern Windows UI framework
- **CMake** - Cross-platform build system

---

## 📞 Contact

For bugs or feature requests, open an issue on GitHub.

---

**Made with ❤️ for Windows 11 customization enthusiasts**
