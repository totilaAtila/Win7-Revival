# VSCode Setup Guide pentru CrystalFrame Engine

Acest ghid te va ajuta să configurezi **Visual Studio Code** pentru a lucra cu proiectul CrystalFrame Engine.

---

## 📋 Cerințe preliminare

Înainte de a configura VSCode, trebuie să instalezi următoarele:

### 1. **Visual Studio 2022** (Community Edition este gratuită)
   - **De ce?** Pentru compilatorul C++ (MSVC) și Windows SDK
   - **Link:** https://visualstudio.microsoft.com/downloads/
   - **Componente necesare la instalare:**
     - ✅ "Desktop development with C++"
     - ✅ "C++ CMake tools for Windows"
     - ✅ "Windows 11 SDK (10.0.22621.0)"
   
   **IMPORTANT:** Chiar dacă folosești VSCode pentru editare, ai nevoie de Visual Studio pentru compilator!

### 2. **.NET 8 SDK**
   - **De ce?** Pentru compilarea Dashboard-ului C#
   - **Link:** https://dotnet.microsoft.com/download/dotnet/8.0
   - Descarcă "SDK x64" (nu doar Runtime)
   - Verifică instalarea:
     ```cmd
     dotnet --version
     ```
     Trebuie să vezi: `8.0.x` sau mai nou

### 3. **CMake**
   - **De ce?** Pentru build system-ul Core C++
   - **Link:** https://cmake.org/download/
   - Descarcă "Windows x64 Installer"
   - **LA INSTALARE:** bifează "Add CMake to system PATH"
   - Verifică instalarea:
     ```cmd
     cmake --version
     ```

---

## 🔧 Instalare extensii VSCode

Deschide VSCode și instalează următoarele extensii:

### Pentru C++ (Core):
1. **C/C++** (de la Microsoft)
   - ID: `ms-vscode.cpptools`
   - Provides IntelliSense, debugging, code browsing

2. **CMake Tools** (de la Microsoft)
   - ID: `ms-vscode.cmake-tools`
   - CMake integration

### Pentru C# (Dashboard):
3. **C# Dev Kit** (de la Microsoft)
   - ID: `ms-dotnettools.csdevkit`
   - Includes C#, IntelliSense, debugging

Cum se instalează:
- Click pe icon-ul Extensions (Ctrl+Shift+X)
- Caută fiecare extensie
- Click "Install"

---

## 📂 Deschidere proiect în VSCode

### Opțiunea 1: Workspace multi-root (RECOMANDAT)

1. Salvează acest conținut într-un fișier `CrystalFrame.code-workspace`:

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

2. Deschide workspace-ul: `File` → `Open Workspace from File` → selectează `CrystalFrame.code-workspace`

### Opțiunea 2: Folder simplu

Doar deschide folderul root `CrystalFrame/` în VSCode.

---

## 🏗️ Compilare Core (C++)

### Metoda 1: Cu CMake Tools (în VSCode)

1. **Configurare CMake:**
   - Apasă `Ctrl+Shift+P`
   - Tastează: `CMake: Configure`
   - Alege compilatorul: "Visual Studio Community 2022 Release - amd64"

2. **Build:**
   - Apasă `Ctrl+Shift+P`
   - Tastează: `CMake: Build`
   - SAU click pe butonul "Build" din status bar (jos)

3. **Executabil rezultat:**
   - `CrystalFrame/Core/build/bin/CrystalFrame.Core.exe`

### Metoda 2: Terminal (command line)

```cmd
cd CrystalFrame/Core
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## 🏗️ Compilare Dashboard (C#)

### În VSCode:

1. **Deschide Terminal în VSCode:**
   - `Terminal` → `New Terminal` (sau Ctrl+ù)

2. **Navighează la Dashboard:**
   ```cmd
   cd Dashboard
   ```

3. **Restore dependencies:**
   ```cmd
   dotnet restore
   ```

4. **Build:**
   ```cmd
   dotnet build --configuration Release
   ```

5. **Executabil rezultat:**
   - `CrystalFrame/Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64/CrystalFrame.Dashboard.exe`

### Shortcut: Task automation

Creează fișier `.vscode/tasks.json` în root:

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
            "args": ["build", "Dashboard", "--configuration", "Release"],
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

Apoi: `Ctrl+Shift+B` → "Build All"

---

## ▶️ Rulare proiect

### Pas 1: Pornește Core

În terminal VSCode:
```cmd
cd Core/build/bin
CrystalFrame.Core.exe
```

SAU double-click pe `CrystalFrame.Core.exe`

**Verificare:** Ar trebui să vezi în `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log` mesaje de tip:
```
[INFO] CrystalFrame Core Starting
[INFO] Taskbar found
[INFO] IPC pipe created
```

### Pas 2: Pornește Dashboard

În alt terminal VSCode:
```cmd
cd Dashboard/bin/Release/net8.0-windows10.0.22621.0/win-x64
CrystalFrame.Dashboard.exe
```

SAU double-click pe `CrystalFrame.Dashboard.exe`

**Verificare:** Dashboard-ul ar trebui să arate:
- ✓ Connected to Core
- ✓ Taskbar found
- Slidere funcționale

---

## 🐛 Debugging în VSCode

### Debug Core (C++):

Creează `.vscode/launch.json`:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Core (C++)",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "${workspaceFolder}/Core/build/bin/Debug/CrystalFrame.Core.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}/Core",
            "environment": [],
            "console": "externalTerminal"
        },
        {
            "name": "Debug Dashboard (C#)",
            "type": "coreclr",
            "request": "launch",
            "preLaunchTask": "Build Dashboard (C#)",
            "program": "${workspaceFolder}/Dashboard/bin/Debug/net8.0-windows10.0.22621.0/win-x64/CrystalFrame.Dashboard.exe",
            "args": [],
            "cwd": "${workspaceFolder}/Dashboard",
            "console": "internalConsole",
            "stopAtEntry": false
        }
    ]
}
```

Apoi: F5 pentru a porni debugging!

---

## ✅ Verificare instalare corectă

Rulează în terminal:

```cmd
# Verifică Visual Studio compiler
where cl.exe

# Verifică .NET SDK
dotnet --version

# Verifică CMake
cmake --version

# Verifică Git (optional, pentru version control)
git --version
```

Toate ar trebui să returneze versiuni, nu erori!

---

## ❓ Probleme comune

### ❌ "cl.exe not found"
**Soluție:** 
- Asigură-te că Visual Studio 2022 este instalat cu "Desktop development with C++"
- Deschide "Developer Command Prompt for VS 2022" și rulează compilarea de acolo

### ❌ "CMake not found"
**Soluție:**
- Reinstalează CMake cu opțiunea "Add to PATH"
- Sau adaugă manual: `C:\Program Files\CMake\bin` la PATH

### ❌ "dotnet not found"
**Soluție:**
- Reinstalează .NET 8 SDK (nu Runtime!)
- Restart VSCode după instalare

### ❌ Dashboard nu se conectează la Core
**Soluție:**
1. Verifică că `CrystalFrame.Core.exe` rulează
2. Verifică logul: `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log`
3. Caută linia "IPC pipe created"

### ❌ Overlay nu apare
**Soluție:**
1. Verifică că rulezi pe Windows 11
2. Core trebuie să ruleze cu permisiuni normale (nu Administrator)
3. Verifică log pentru erori DirectComposition

---

## 🚀 Next Steps

După ce ai reușit să compilezi și să rulezi:

1. **Testare:** Citește `docs/TESTING.md` pentru scenarii de test
2. **Modificări:** Editează cod în VSCode, rebuild, test
3. **Git:** Inițializează repo:
   ```cmd
   git init
   git add .
   git commit -m "Initial commit - CrystalFrame Engine"
   ```

---

## 📚 Resurse utile

- **VSCode Docs:** https://code.visualstudio.com/docs
- **CMake Tutorial:** https://cmake.org/cmake/help/latest/guide/tutorial/
- **.NET CLI:** https://learn.microsoft.com/en-us/dotnet/core/tools/
- **C++ in VSCode:** https://code.visualstudio.com/docs/languages/cpp

---

**Succes! Dacă întâmpini probleme, verifică logurile în `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log`** 🎯
