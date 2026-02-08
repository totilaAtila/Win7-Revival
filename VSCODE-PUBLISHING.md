# 🎯 VSCode Publishing Guide - Ctrl+Shift+P

Ghid vizual pas-cu-pas pentru publishing din Visual Studio Code.

## 🚀 Quick Start - 3 Pași

### **Pasul 1: Deschide Command Palette**
```
Ctrl + Shift + P
```
sau
```
F1
```

### **Pasul 2: Tastează "Run Task"**
```
> Tasks: Run Task
```
Apasă **Enter**

### **Pasul 3: Alege task-ul dorit**
Vei vedea:
- 🏗️ **Build (Full)** - Build complet (C++ + C#)
- 📦 **Publish - Framework Dependent** - Package mic (~15 MB)
- 📦 **Publish - Standalone** - Package standalone (~80 MB)
- 🚀 **Create GitHub Release** - Creează ambele packages
- 🧹 **Clean All** - Șterge toate build-urile
- ▶️ **Run Dashboard** - Build și run aplicația
- 🎯 **Quick Publish (1.0.0)** - Publish rapid versiunea 1.0.0

---

## 📋 Task-uri Disponibile

### 🏗️ Build (Full)
**Ce face:** Build complet C++ Core + C# Dashboard
**Când:** Înainte de testare sau publish
**Scurtătură:** `Ctrl+Shift+B` (build default)

```
Ctrl+Shift+P → Tasks: Run Build Task
```

---

### 📦 Publish - Framework Dependent
**Ce face:** Creează package de ~15-20 MB (necesită .NET 8)
**Când:** Pentru distribuție publică pe GitHub
**Prompt:** Te întreabă versiunea (ex: `1.0.0`)

```
Ctrl+Shift+P → Tasks: Run Task → 📦 Publish - Framework Dependent
> Enter version: 1.0.0
```

**Output:** `.\publish\CrystalFrame-v1.0.0.zip`

---

### 📦 Publish - Standalone
**Ce face:** Creează package de ~70-90 MB (include .NET runtime)
**Când:** Pentru utilizatori non-tehnici (zero dependencies)
**Prompt:** Te întreabă versiunea

```
Ctrl+Shift+P → Tasks: Run Task → 📦 Publish - Standalone
> Enter version: 1.0.0
```

**Output:** `.\publish\CrystalFrame-v1.0.0-Standalone.zip`

---

### 🚀 Create GitHub Release
**Ce face:** Creează AMBELE packages + release notes
**Când:** Când vrei să faci release oficial pe GitHub
**Prompt:** Te întreabă versiunea

```
Ctrl+Shift+P → Tasks: Run Task → 🚀 Create GitHub Release
> Enter version: 1.0.0
```

**Output:**
- `CrystalFrame-v1.0.0.zip` (framework-dependent)
- `CrystalFrame-v1.0.0-Standalone.zip` (standalone)
- `RELEASE_NOTES_v1.0.0.md` (pentru GitHub)

După ce se termină, folderul `.\publish` se deschide automat! 🎉

---

### 🧹 Clean All
**Ce face:** Șterge toate build-urile și publish-urile
**Când:** Când vrei fresh build sau ai probleme

```
Ctrl+Shift+P → Tasks: Run Task → 🧹 Clean All
```

---

### ▶️ Run Dashboard
**Ce face:** Build + Run aplicația
**Când:** Pentru testare rapidă

```
Ctrl+Shift+P → Tasks: Run Task → ▶️ Run Dashboard
```

Sau folosește **F5** pentru debugging.

---

### 🎯 Quick Publish (1.0.0)
**Ce face:** Publish rapid cu versiunea 1.0.0 (fără prompt)
**Când:** Pentru publishing rapid în development

```
Ctrl+Shift+P → Tasks: Run Task → 🎯 Quick Publish (1.0.0)
```

---

## 🎬 Workflow Complet - Exemplu Real

### **Scenariul 1: Quick Test**
```
1. Ctrl+Shift+B           → Build
2. F5                      → Debug/Run
```

### **Scenariul 2: Create Release Package**
```
1. Ctrl+Shift+P
2. Tasks: Run Task
3. 🚀 Create GitHub Release
4. Enter version: 1.0.0
5. Așteaptă...
6. Folderul publish se deschide automat
7. Upload ZIP-urile pe GitHub
```

### **Scenariul 3: Publish Rapid doar Framework-Dependent**
```
1. Ctrl+Shift+P
2. Tasks: Run Task
3. 📦 Publish - Framework Dependent
4. Enter version: 1.0.0
5. Done! ZIP creat în .\publish\
```

### **Scenariul 4: Clean Build**
```
1. Ctrl+Shift+P
2. Tasks: Run Task
3. 🧹 Clean All
4. Ctrl+Shift+B           → Fresh build
```

---

## ⌨️ Scurtături Utile

| Scurtătură | Acțiune |
|-----------|---------|
| `Ctrl+Shift+P` | Command Palette |
| `Ctrl+Shift+B` | Build (default task) |
| `F5` | Start Debugging |
| `Ctrl+F5` | Run Without Debugging |
| `Ctrl+Shift+T` | Reopen Closed Terminal |
| `Ctrl+`` | Toggle Terminal |

---

## 🎨 Customizare Task-uri

Vrei să modifici task-urile? Editează `.vscode/tasks.json`:

```
Ctrl+Shift+P → Preferences: Open User Settings (JSON)
```

Sau editează direct:
```
.vscode/tasks.json
```

### Exemplu: Schimbă versiunea default la Quick Publish
```json
{
    "label": "🎯 Quick Publish (1.0.0)",
    ...
    "args": [
        "-Version",
        "2.0.0"  // ← Schimbă aici
    ]
}
```

---

## 📺 Tutorial Video (Text)

### **Cum fac publish prima dată:**

1. **Deschide VSCode**
   - Deschide folderul `Win7-Revival`

2. **Ctrl+Shift+P**
   - Se deschide Command Palette (căsuța de sus)

3. **Tastează: "run task"**
   - Vei vedea: `Tasks: Run Task`
   - Apasă Enter

4. **Alege: "🚀 Create GitHub Release"**
   - Scroll până găsești task-ul
   - Sau tastează "github" pentru filtrare
   - Apasă Enter

5. **Introdu versiunea**
   - Terminal-ul te întreabă: `Enter version (e.g., 1.0.0):`
   - Tastează: `1.0.0`
   - Apasă Enter

6. **Așteaptă build-ul**
   - Vei vedea în terminal:
     ```
     [1/5] Building Release version...
     [2/5] Creating publish directory...
     [3/5] Copying Dashboard files...
     [4/5] Copying Core.dll...
     [5/5] Creating documentation...
     ```

7. **Gata!**
   - Folderul `publish` se deschide automat
   - Vei vedea cele 2 ZIP files
   - Upload-le pe GitHub → Releases

---

## 🐛 Troubleshooting

### ❌ "Task not found"
**Soluție:** Reîncarcă window-ul VSCode
```
Ctrl+Shift+P → Developer: Reload Window
```

### ❌ "Build failed"
**Soluție:** Clean și rebuild
```
Ctrl+Shift+P → Tasks: Run Task → 🧹 Clean All
Ctrl+Shift+B
```

### ❌ "Cannot find dotnet"
**Soluție:** Instalează .NET 8 SDK
```powershell
winget install Microsoft.DotNet.SDK.8
```
Apoi restart VSCode.

### ❌ "CMake not found"
**Soluție:** Instalează CMake
```powershell
winget install Kitware.CMake
```
Apoi restart VSCode.

### ❌ Terminal-ul nu afișează emoji-uri
**Soluție:** Folosește Windows Terminal
```
Ctrl+Shift+P → Terminal: Select Default Profile → Windows Terminal
```

---

## 📖 Vezi și

- [PUBLISHING.md](PUBLISHING.md) - Ghid complet publishing
- [BUILD.md](BUILD.md) - Ghid build process
- [TESTING.md](TESTING.md) - Ghid testing

---

## ✨ Pro Tips

1. **Custom Keyboard Shortcuts:**
   ```
   Ctrl+Shift+P → Preferences: Open Keyboard Shortcuts (JSON)
   ```
   Adaugă:
   ```json
   {
       "key": "ctrl+shift+r",
       "command": "workbench.action.tasks.runTask",
       "args": "🚀 Create GitHub Release"
   }
   ```

2. **Auto-Open Publish Folder:**
   Task-urile deschid automat folderul `publish` după ce se termină!

3. **Terminal Output:**
   Click pe `OUTPUT` în loc de `TERMINAL` pentru log-uri mai curate.

4. **Quick Access:**
   Adaugă în `.vscode/settings.json`:
   ```json
   {
       "task.quickOpen.skip": false,
       "task.quickOpen.showAll": true
   }
   ```

---

**Acum știi tot ce trebuie! 🎉**

**Quick reminder:**
```
Ctrl+Shift+P → Tasks: Run Task → 🚀 Create GitHub Release
```

**Enjoy publishing! 🚀**
