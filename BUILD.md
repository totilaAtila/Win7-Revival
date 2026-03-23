# Build Instructions — GlassBar

Pași detaliați pentru compilarea componentelor Core (C++) și Dashboard (C#).

---

## Prerequisites Checklist

- [ ] Windows 11 (22H2 or later)
- [ ] Visual Studio 2022 (Community / Professional / Enterprise)
  - [ ] Workload: "Desktop development with C++"
  - [ ] Component: "Windows 11 SDK (10.0.22621.0)"
  - [ ] Component: "C++ CMake tools for Windows"
- [ ] .NET 8 SDK ([Download](https://dotnet.microsoft.com/download/dotnet/8.0))
- [ ] CMake 3.20+ ([Download](https://cmake.org/download/))
- [ ] Git (optional, for version control)

---

## Building Core (C++20)

Core compilează ca **DLL nativ** (`GlassBar.Core.dll`). Nu este un executabil standalone — este
încărcat direct de Dashboard prin P/Invoke.

### Option 1: Visual Studio 2022

1. **Open CMake Project:**
   - Launch Visual Studio 2022
   - `File` → `Open` → `CMake`
   - Select `Core/CMakeLists.txt`

2. **Configure:**
   - Select configuration: `x64-Release`
   - Wait for CMake configuration to complete

3. **Build:**
   - `Build` → `Build All`
   - OR press `Ctrl+Shift+B`

4. **Output:**
   - `Core/build/bin/Release/GlassBar.Core.dll`

### Option 2: Command Line (CMake)

```cmd
cd Core

cmake -B build -A x64
cmake --build build --config Release
```

**Output:**
- Release: `Core/build/bin/Release/GlassBar.Core.dll`
- Debug:   `Core/build/bin/Debug/GlassBar.Core.dll`

### Option 3: VSCode (with CMake Tools)

See `VSCODE-SETUP.md` for detailed VSCode instructions.

---

## Building Dashboard (C# .NET 8)

### Option 1: Visual Studio 2022

1. **Open Solution:**
   - Launch Visual Studio 2022
   - `File` → `Open` → `Project/Solution`
   - Select `Dashboard/GlassBar.Dashboard.csproj`

2. **Restore NuGet Packages:**
   - `Build` → `Restore NuGet Packages`

3. **Build:**
   - Select `Release` configuration
   - `Build` → `Build Solution` (or `Ctrl+Shift+B`)

4. **Output:**
   - `Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/GlassBar.Dashboard.exe`

### Option 2: Command Line (dotnet CLI)

```cmd
cd Dashboard

dotnet restore
dotnet build -r win-x64 --no-self-contained --configuration Release
```

**Output:**
- `Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/GlassBar.Dashboard.exe`

Publish (self-contained, optional):
```cmd
dotnet publish -r win-x64 --self-contained --configuration Release
```

### Option 3: VSCode

See `VSCODE-SETUP.md` for VSCode build tasks.

---

## Build Configurations

### Core (C++)

| Configuration | Optimizations | Debug Info | Use Case |
|--------------|---------------|------------|----------|
| Debug        | Disabled (/Od) | Full (/Zi) | Development, debugging |
| Release      | Enabled (/O2)  | None       | Production, testing |

### Dashboard (C#)

| Configuration | Optimizations | Debug Info | Use Case |
|--------------|---------------|------------|----------|
| Debug        | Disabled | Full | Development |
| Release      | Enabled  | None | Production |

---

## Running GlassBar

**Nu trebuie să pornești Core separat.** `GlassBar.Core.dll` este încărcat automat de Dashboard
prin P/Invoke când dai click pe butonul **Core → ON** din interfață.

```cmd
Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\GlassBar.Dashboard.exe
```

**Expected behavior:**
- Fereastra Dashboard se deschide cu două taburi: Taskbar / Start Menu
- Indicatorul de status din header este gri (Core oprit)
- Click pe toggleul **Core** → ON → indicatorul devine verde
- Overlay-ul de Taskbar apare; hook-ul pentru Start Menu se activează

**Log file:**
```
%LOCALAPPDATA%\GlassBar\GlassBar.log
```

Expected entries after startup:
```
[INFO] GlassBar Core Starting
[INFO] Taskbar found: edge=bottom
[INFO] Config loaded
```

---

## Clean Build

### Core

```cmd
cd Core
rmdir /s /q build
cmake -B build -A x64
cmake --build build --config Release
```

### Dashboard

```cmd
cd Dashboard
dotnet clean
dotnet build -r win-x64 --no-self-contained --configuration Release
```

---

## Troubleshooting

### Core Build Issues

**Error: "CMake not found"**
```cmd
"C:\Program Files\CMake\bin\cmake.exe" -B build -A x64
```
Or add CMake to PATH and restart terminal.

**Error: "Cannot open compiler-generated file"**
- Run as standard user (not Administrator) OR disable antivirus temporarily.

**Error: "Windows SDK not found"**
- Open Visual Studio Installer → Modify → add "Windows 11 SDK (10.0.22621.0)".

### Dashboard Build Issues

**Error: "SDK not found"**
```cmd
dotnet --list-sdks
```
If missing, reinstall .NET 8 SDK.

**Error: "WindowsAppSDK package not found"**
```cmd
dotnet nuget locals all --clear
dotnet restore
```

**Error: "VCRUNTIME140.dll missing" at runtime**
- Core is built with static CRT (`/MT`), so this should not occur.
  If it does, install "Visual C++ Redistributable 2015–2022".

---

## Build Artifacts

After a successful build:

```
GlassBar/
├── Core/
│   └── build/
│       └── bin/
│           └── Release/
│               └── GlassBar.Core.dll          ← Native DLL (loaded by Dashboard)
│
└── Dashboard/
    └── bin/
        └── Release/
            └── net8.0-windows10.0.22621.0/
                └── win-x64/
                    ├── GlassBar.Dashboard.exe  ← Entry point
                    ├── GlassBar.Core.dll       ← Copied here automatically by MSBuild
                    └── *.dll                   ← WinAppSDK / .NET dependencies
```

> The Dashboard `.csproj` contains a post-build step that copies `GlassBar.Core.dll`
> into the Dashboard output directory automatically.

---

## Packaging (Optional)

To create a distributable ZIP:

```cmd
mkdir GlassBar-v2.2

copy Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\GlassBar.Dashboard.exe GlassBar-v2.2\
copy Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\GlassBar.Core.dll      GlassBar-v2.2\
copy Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\*.dll                  GlassBar-v2.2\
copy README.md GlassBar-v2.2\
```

Then right-click folder → Send to → Compressed (zipped) folder.

For automated packaging, use the provided PowerShell scripts:
```powershell
.\create-release.ps1 -Version "2.2"
```

---

## Next Steps

After a successful build:
1. **Test:** Follow `TESTING.md` for validation scenarios
2. **Run:** Start `GlassBar.Dashboard.exe`, enable Core from the header toggle
3. **Debug:** Use Debug builds with Visual Studio or VSCode debugger (see `VSCODE-SETUP.md`)

---

**For VSCode-specific build instructions, see `VSCODE-SETUP.md`**
