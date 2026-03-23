# VSCode Setup Guide — GlassBar

Ghid pentru configurarea Visual Studio Code pentru lucrul cu proiectul GlassBar.

---

## Cerințe preliminare

### 1. Visual Studio 2022 (Community Edition este gratuită)
- **De ce?** Pentru compilatorul C++ (MSVC) și Windows SDK
- **Link:** https://visualstudio.microsoft.com/downloads/
- **Componente necesare:**
  - "Desktop development with C++"
  - "C++ CMake tools for Windows"
  - "Windows 11 SDK (10.0.22621.0)"

> Chiar dacă folosești VSCode pentru editare, ai nevoie de Visual Studio pentru compilator (cl.exe)!

### 2. .NET 8 SDK
- **De ce?** Pentru compilarea Dashboard-ului C#
- **Link:** https://dotnet.microsoft.com/download/dotnet/8.0
- Descarcă **SDK x64** (nu doar Runtime)
- Verificare:
  ```cmd
  dotnet --version
  ```
  Trebuie să vezi: `8.0.x` sau mai nou

### 3. CMake 3.20+
- **Link:** https://cmake.org/download/
- Descarcă "Windows x64 Installer"
- **LA INSTALARE:** bifează "Add CMake to system PATH"
- Verificare:
  ```cmd
  cmake --version
  ```

---

## Extensii VSCode recomandate

### Pentru C++ (Core):
- **C/C++** — `ms-vscode.cpptools` (IntelliSense, debugging)
- **CMake Tools** — `ms-vscode.cmake-tools` (integrare CMake)

### Pentru C# (Dashboard):
- **C# Dev Kit** — `ms-dotnettools.csdevkit` (IntelliSense, debugging)

---

## Deschidere proiect în VSCode

### Opțiunea 1: Workspace multi-root (RECOMANDAT)

Salvează conținutul următor în `GlassBar.code-workspace` la rădăcina proiectului:

```json
{
    "folders": [
        {
            "name": "Core (C++)",
            "path": "Core"
        },
        {
            "name": "Dashboard (C#)",
            "path": "Dashboard"
        },
        {
            "name": "Root",
            "path": "."
        }
    ],
    "settings": {
        "files.exclude": {
            "**/bin": true,
            "**/obj": true,
            "**/build": true
        }
    }
}
```

Deschide: `File` → `Open Workspace from File` → selectează `GlassBar.code-workspace`

### Opțiunea 2: Folder simplu

Deschide folderul root `GlassBar/` în VSCode.

---

## Compilare Core (C++)

Core produce un **DLL** (`GlassBar.Core.dll`), nu un executabil. Nu se rulează standalone.

### Metoda 1: CMake Tools (în VSCode)

1. `Ctrl+Shift+P` → `CMake: Configure`
2. Alege compilatorul: "Visual Studio Community 2022 Release - amd64"
3. `Ctrl+Shift+P` → `CMake: Build`
   SAU click pe butonul "Build" din status bar (jos)

**Output:** `Core/build/bin/Release/GlassBar.Core.dll`

### Metoda 2: Terminal

```cmd
cd Core
cmake -B build -A x64
cmake --build build --config Release
```

---

## Compilare Dashboard (C#)

### În terminal VSCode:

```cmd
cd Dashboard
dotnet restore
dotnet build -r win-x64 --no-self-contained --configuration Release
```

**Output:** `Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/GlassBar.Dashboard.exe`

> MSBuild copiază automat `GlassBar.Core.dll` în directorul Dashboard după build.

### Task automation (.vscode/tasks.json)

Creează sau actualizează `.vscode/tasks.json`:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build Core (C++)",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "Core/build", "--config", "Release"],
            "group": "build",
            "problemMatcher": "$msCompile"
        },
        {
            "label": "Build Dashboard (C#)",
            "type": "shell",
            "command": "dotnet",
            "args": ["build", "Dashboard", "-r", "win-x64", "--no-self-contained", "--configuration", "Release"],
            "group": "build",
            "problemMatcher": "$msCompile"
        },
        {
            "label": "Build All",
            "dependsOn": ["Build Core (C++)", "Build Dashboard (C#)"],
            "group": {
                "kind": "build",
                "isDefault": true
            }
        }
    ]
}
```

Rulare: `Ctrl+Shift+B` → "Build All"

---

## Rulare GlassBar

**Nu trebuie să pornești Core separat.** `GlassBar.Core.dll` este încărcat automat de Dashboard
prin P/Invoke. Rulezi direct Dashboard-ul:

```cmd
Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\GlassBar.Dashboard.exe
```

**Verificare:**
- Fereastra Dashboard se deschide
- Click pe toggleul **Core** → ON → indicator verde = Core pornit
- Overlay Taskbar apare; Win key activează Start Menu-ul custom

**Log file:** `%LOCALAPPDATA%\GlassBar\GlassBar.log`

---

## Debugging în VSCode

### Debug Dashboard (C#):

Creează `.vscode/launch.json`:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Dashboard (C#)",
            "type": "coreclr",
            "request": "launch",
            "preLaunchTask": "Build Dashboard (C#)",
            "program": "${workspaceFolder}/Dashboard/bin/Debug/net8.0-windows10.0.22621.0/win-x64/GlassBar.Dashboard.exe",
            "args": [],
            "cwd": "${workspaceFolder}/Dashboard",
            "console": "internalConsole",
            "stopAtEntry": false
        }
    ]
}
```

Apasă F5 pentru a porni debugging.

> Pentru debug Core (C++), folosește Visual Studio 2022 cu `Attach to Process` pe
> `GlassBar.Dashboard.exe` (Core rulează in-process ca DLL).

---

## Verificare instalare

```cmd
# Verifică Visual Studio compiler
where cl.exe

# Verifică .NET SDK
dotnet --version

# Verifică CMake
cmake --version

# Verifică Git (optional)
git --version
```

Toate comenzile trebuie să returneze versiuni, nu erori.

---

## Probleme comune

### "cl.exe not found"
- Asigură-te că Visual Studio 2022 este instalat cu "Desktop development with C++"
- Deschide "Developer Command Prompt for VS 2022" și rulează compilarea de acolo

### "CMake not found"
- Reinstalează CMake cu opțiunea "Add to PATH"
- Sau adaugă manual `C:\Program Files\CMake\bin` la PATH

### "dotnet not found"
- Reinstalează .NET 8 SDK (nu Runtime!)
- Restart VSCode după instalare

### Overlay nu apare
1. Verifică că Core toggle este ON (indicator verde în Dashboard header)
2. Verifică log: `%LOCALAPPDATA%\GlassBar\GlassBar.log`
3. Caută linia `Taskbar found` — dacă lipsește, restartează Windows Explorer

### GlassBar.Core.dll lipsește din directorul Dashboard
- Asigură-te că ai compilat **Core** înainte de Dashboard
- Sau copiază manual: `copy Core\build\bin\Release\GlassBar.Core.dll Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\`

---

## Next Steps

1. **Testare:** Citește `TESTING.md` pentru scenarii de validare
2. **Modificări:** Editează cod în VSCode, rebuild, test
3. **Publishing:** Citește `PUBLISHING.md` pentru ghid de distribuire

---

**Dacă întâmpini probleme, verifică logurile în `%LOCALAPPDATA%\GlassBar\GlassBar.log`**
