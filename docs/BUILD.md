# Build Instructions - CrystalFrame Engine

Detailed steps for building both Core (C++) and Dashboard (C#) components.

---

## Prerequisites Checklist

- [ ] Windows 11 (22H2 or later)
- [ ] Visual Studio 2022 (Community/Professional/Enterprise)
  - [ ] "Desktop development with C++" workload
  - [ ] "Windows 11 SDK (10.0.22621.0)"
  - [ ] "C++ CMake tools for Windows"
- [ ] .NET 8 SDK ([Download](https://dotnet.microsoft.com/download/dotnet/8.0))
- [ ] CMake 3.20+ ([Download](https://cmake.org/download/))
- [ ] Git (optional, for version control)

---

## Building Core (C++20)

### Option 1: Visual Studio 2022

1. **Open CMake Project:**
   - Launch Visual Studio 2022
   - `File` → `Open` → `CMake`
   - Select `CrystalFrame/Core/CMakeLists.txt`

2. **Configure:**
   - Select configuration: `x64-Release`
   - Wait for CMake configuration to complete

3. **Build:**
   - `Build` → `Build All`
   - OR press `Ctrl+Shift+B`

4. **Output:**
   - `CrystalFrame/Core/out/build/x64-Release/CrystalFrame.Core.exe`

### Option 2: Command Line (CMake)

```cmd
cd CrystalFrame/Core

# Create build directory
mkdir build
cd build

# Configure
cmake .. -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build . --config Release

# Build Debug (optional)
cmake --build . --config Debug
```

**Output:**
- Release: `Core/build/bin/Release/CrystalFrame.Core.exe`
- Debug: `Core/build/bin/Debug/CrystalFrame.Core.exe`

### Option 3: VSCode (with CMake Tools)

See `docs/VSCODE-SETUP.md` for detailed VSCode instructions.

---

## Building Dashboard (C# .NET 8)

### Option 1: Visual Studio 2022

1. **Open Solution/Project:**
   - Launch Visual Studio 2022
   - `File` → `Open` → `Project/Solution`
   - Select `CrystalFrame/Dashboard/CrystalFrame.Dashboard.csproj`

2. **Restore NuGet Packages:**
   - `Build` → `Restore NuGet Packages`
   - Wait for completion

3. **Build:**
   - Select `Release` configuration
   - `Build` → `Build Solution`
   - OR press `Ctrl+Shift+B`

4. **Output:**
   - `Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/CrystalFrame.Dashboard.exe`

### Option 2: Command Line (dotnet CLI)

```cmd
cd CrystalFrame/Dashboard

# Restore dependencies
dotnet restore

# Build Release
dotnet build --configuration Release

# Build Debug (optional)
dotnet build --configuration Debug

# Publish (self-contained, optional)
dotnet publish --configuration Release --self-contained --runtime win-x64
```

**Output:**
- Build: `Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/`
- Publish: `Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/publish/`

### Option 3: VSCode

See `docs/VSCODE-SETUP.md` for VSCode build tasks.

---

## Build Configurations

### Core (C++)

| Configuration | Optimizations | Debug Info | Use Case |
|--------------|---------------|------------|----------|
| Debug | Disabled (/Od) | Full (/Zi) | Development, debugging |
| Release | Enabled (/O2) | None | Production, testing |

### Dashboard (C#)

| Configuration | Optimizations | Debug Info | Use Case |
|--------------|---------------|------------|----------|
| Debug | Disabled | Full | Development |
| Release | Enabled | None | Production |

---

## Verification

### Core

```cmd
# Run Core
cd Core/build/bin/Release
CrystalFrame.Core.exe

# Check log
type %LOCALAPPDATA%\CrystalFrame\CrystalFrame.log
```

Expected output in log:
```
[INFO] CrystalFrame Core Starting
[INFO] Taskbar found: edge=bottom
[INFO] IPC pipe created
```

### Dashboard

```cmd
# Run Dashboard
cd Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64
CrystalFrame.Dashboard.exe
```

Expected:
- Window opens with sliders
- Status shows "✓ Connected to Core"
- Sliders are functional

---

## Clean Build

### Core

```cmd
cd CrystalFrame/Core
rmdir /s /q build
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Dashboard

```cmd
cd CrystalFrame/Dashboard
dotnet clean
dotnet build --configuration Release
```

---

## Troubleshooting

### Core Build Issues

**Error: "CMake not found"**
```cmd
# Add CMake to PATH or use full path
"C:\Program Files\CMake\bin\cmake.exe" ..
```

**Error: "Cannot open compiler-generated file"**
- Solution: Run as Administrator OR disable antivirus temporarily

**Error: "d2d1.lib not found"**
- Solution: Install Windows 11 SDK via Visual Studio Installer

### Dashboard Build Issues

**Error: "SDK not found"**
```cmd
# Verify .NET SDK installation
dotnet --list-sdks

# If missing, reinstall .NET 8 SDK
```

**Error: "WindowsAppSDK package not found"**
```cmd
# Clear NuGet cache
dotnet nuget locals all --clear

# Restore again
dotnet restore
```

**Error: "MSVCR140.dll missing"**
- Solution: Install "Visual C++ Redistributable 2015-2022"

---

## Build Artifacts

After successful build, you should have:

```
CrystalFrame/
├── Core/
│   └── build/
│       └── bin/
│           └── Release/
│               └── CrystalFrame.Core.exe       ← Core executable
│
└── Dashboard/
    └── bin/
        └── Release/
            └── net8.0-windows10.0.22621.0/
                └── win-x64/
                    ├── CrystalFrame.Dashboard.exe    ← Dashboard executable
                    └── *.dll                          ← Dependencies
```

---

## Packaging (Optional)

To create a distributable package:

1. **Create folder:**
   ```cmd
   mkdir CrystalFrame-v1.0
   ```

2. **Copy executables:**
   ```cmd
   copy Core\build\bin\Release\CrystalFrame.Core.exe CrystalFrame-v1.0\
   copy Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\*.* CrystalFrame-v1.0\
   ```

3. **Add README:**
   ```cmd
   copy README.md CrystalFrame-v1.0\
   ```

4. **Zip:**
   - Right-click folder → Send to → Compressed (zipped) folder

---

## Next Steps

After successful build:
1. **Test:** Follow `docs/TESTING.md`
2. **Run:** Start Core, then Dashboard
3. **Debug:** Use Debug builds with Visual Studio debugger

---

**For VSCode-specific build instructions, see `docs/VSCODE-SETUP.md`**
