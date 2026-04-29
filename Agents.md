# Agents.md — GlassBar Architecture Reference

## 1) Scopul proiectului

GlassBar este un utilitar de personalizare pentru Windows 11 care oferă:

- **Taskbar overlay** — strat semitransparent colorat peste Windows 11 Taskbar, cu control
  independent al opacității, RGB color tint și efect Blur/Acrylic. Suportă toate pozițiile
  (bottom / top / left / right), auto-hide și multi-monitor.
- **Start Menu replacement** — înlocuire completă a meniului Start nativ cu o reimplementare
  Win7 two-column, construită din zero în GDI (C++). Interceptează Win key și click pe butonul
  Start prin low-level hooks, prezintă meniul custom și suprimă meniul nativ.

Pe **22H2 / 23H2** proiectul rulează extern, fără injection — nicio modificare a fișierelor de sistem, niciun hook în procesele Explorer.

**Nota de adevăr local (2026-04-29 — ITER #22):**
- pe **24H2 / 25H2+**, implementarea curentă include o cale **experimentală XamlBridge / TAP în `explorer.exe`** pentru investigația taskbar-ului XAML
- nu se modifică fișiere de sistem, dar proiectul nu mai este strict "fără injection" în sensul documentului original pe aceste build-uri
- `WH_CALLWNDPROC` instalat pe **toate thread-urile explorer.exe** (confirmat: 9 thread-uri pe build 26200)
- `AdviseVisualTreeChange` livrează doar elementele root/container; arborele complet (inclusiv `BackgroundFill`) necesită walk activ via `VisualTreeHelper`
- Active Discovery implementat; handle-urile root sunt cached; walk-ul recursiv (`ActiveWalkElement`) nu a rulat încă cu succes pe handle-urile cached (sesiune prea scurtă / PostMessageW nu declanșează hook)

---

## 2) Principii non-negociabile

1. **Fără patching de sistem; injection doar ca excepție experimentală**
   - Obiectivul de produs rămâne fără hook/injection în Explorer sau StartMenuExperienceHost.
   - Excepția locală actuală: pe 24H2 / 25H2+ există o cale experimentală `GlassBar.XamlBridge.dll` / TAP în `explorer.exe`.
   - Nicio modificare a fișierelor de sistem; preferința arhitecturală rămâne un strat vizual extern sau o fereastră custom proprie.

2. **Fără impact pe funcționalitate**
   - Taskbar rămâne complet utilizabil (click, drag, context menus, system tray).
   - Overlay-ul Taskbar este click-through (`WS_EX_TRANSPARENT | WS_EX_LAYERED`).

3. **Performanță**
   - Țintă: CPU < 2% idle.
   - Update-uri doar la schimbări (show/hide, rect change, slider change).
   - Debounce 50ms pe slidere (~20 comenzi/s maxim).

4. **Fail-safe**
   - Core nu se prăbușește la erori; log + status în Dashboard.
   - Dacă Taskbar-ul nu este detectat la pornire, se reîncearcă automat.
   - Recovery automată după restart Explorer.

5. **Start Menu: fidelitate Win7**
   - Vizual și funcțional identic cu Windows 7 (cu excepția search bar, nefuncțional momentan).
   - Toate meniurile și submeniurile sunt 100% funcționale (niciun element UI fără acțiune).

---

## 3) Arhitectură

### Componente

```
GlassBar.Dashboard.exe  (C# .NET 8, WinUI 3)
        │
        │  P/Invoke — apeluri directe în DLL (in-process, fără IPC extern)
        │
GlassBar.Core.dll  (C++20, Win32)
```

**GlassBar.Dashboard.exe** — UI de setări
- Fereastră compactă (NavigationView) cu două panouri: **Taskbar** și **Start Menu**
- Încarcă `GlassBar.Core.dll` in-process prin P/Invoke
- Gestionează lifecycle-ul Core (start/stop toggle)
- Trimite setări în timp real (culori, opacitate, toggle-uri)
- System tray icon + autostart prin registry
- Config persistence: `%LOCALAPPDATA%\GlassBar\config.json`

**GlassBar.Core.dll** — Motor nativ
- Exportă un C API (`CoreApi.h`) consumat de Dashboard prin P/Invoke
- Rulează un message loop propriu pe un thread dedicat
- Gestionează toate window handle-urile, hook-urile și renderer-ul

### Module Core

| Modul | Fișiere | Responsabilitate |
|-------|---------|-----------------|
| `Core` | `Core.h / Core.cpp` | Orchestrator principal; lifecycle, message loop, inițializare module |
| `CoreApi` | `CoreApi.h / CoreApi.cpp` | Exporturi C API (`extern "C"`) pentru P/Invoke Dashboard |
| `Renderer` | `Renderer.h / Renderer.cpp` | Transparență Taskbar via SWCA (22H2/23H2) sau LWA_ALPHA fallback; inițializare XamlBridge |
| `StartMenuWindow` | `StartMenuWindow.h / .cpp` | Fereastră custom Win7-style (GDI painting, state, navigare, right-click menus) |
| `StartMenuHook` | `StartMenuHook.h / .cpp` | Low-level hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) pentru Win key și Start button |
| `ShellTargetLocator` | `ShellTargetLocator.h / .cpp` | Detectare Taskbar (toate edge-urile, multi-monitor), monitoring prin background thread |
| `AllProgramsEnumerator` | `AllProgramsEnumerator.h / .cpp` | Enumerare recursivă `FOLDERID_Programs`, rezolvare `.lnk` via COM `IShellLinkW` |
| `ConfigManager` | `ConfigManager.h / .cpp` | Citire/scriere `config.json`; thread-safe getters/setters |
| `Diagnostics` | `Diagnostics.h / .cpp` | Macro `CF_LOG` + scriere `%LOCALAPPDATA%\GlassBar\GlassBar.log` |

**Module XamlBridge** (`Core/XamlBridge/` — `GlassBar.XamlBridge.dll`, injectat în `explorer.exe`):

| Modul | Fișiere | Responsabilitate |
|-------|---------|-----------------|
| `dllmain` | `dllmain.cpp` | DLL entry point; `WorkerThread`; logging; `XamlBridgeHookProc` export; COM exports |
| `TAPInjector` | `TAPInjector.h / .cpp` | Înregistrare `InitializeXamlDiagnosticsEx` pe toate `VisualDiagConnectionN` islands; retry după succes confirmat |
| `VisualTreeWatcher` | `VisualTreeWatcher.h / .cpp` | `IVisualTreeServiceCallback2` — detectare `TaskbarBackground`, walk tree, aplicare brush via `ClearProperty` / `SetProperty` |
| `TAPObject` | `TAPObject.h / .cpp` | COM factory (`DllGetClassObject`) + TAP class consumat de XAML diagnostics |
| `XamlBridgeCommon` | `XamlBridgeCommon.h` | Globals comuni (`g_visualTreeService3`, `g_xamlDiagnostics`, `g_knownShapes`, etc.) |
| `SharedBlurState` | `SharedBlurState.h` | Structură shared memory (`Local\GlassBar_XamlBridge_v1`) pentru IPC atomic Core ↔ XamlBridge |

### Module Dashboard (C#)

| Fișier | Responsabilitate |
|--------|-----------------|
| `App.xaml.cs` | Single-instance (Mutex), `/autostart` flag, unhandled exception handler |
| `MainWindow.xaml.cs` | UI principal, NavigationView tabs, debounce slidere, sync culori, auto-update infobar |
| `MainViewModel.cs` | State management (INotifyPropertyChanged), forwardare setări la Core |
| `CoreManager.cs` | P/Invoke wrapper high-level, message pump thread, lifecycle Core |
| `CoreNative.cs` | Declarații P/Invoke pentru toate exporturile Core |
| `ConfigManager.cs` | JSON persistence cu debounce save (250ms) |
| `TrayIconManager.cs` | System tray icon via `Shell_NotifyIcon`, context menu (Show / Exit) |
| `StartupManager.cs` | Autostart via `HKCU\...\Run` cu flag `/autostart` |
| `UpdateChecker.cs` | Polling GitHub Releases API; notificare în UI când e disponibil un release nou |
| `CoreExtractor.cs` | Extrage DLL-urile native din publish folder la prima rulare |

---

## 4) API Core ↔ Dashboard (P/Invoke)

Comunicarea se face exclusiv prin apeluri directe în DLL (in-process). Nu există Named Pipes,
sockets sau orice alt canal IPC extern.

**Exporturi principale (din `CoreApi.h`):**

```cpp
// Lifecycle
GLASSBAR_API bool CoreInitialize();
GLASSBAR_API void CoreShutdown();

// Taskbar overlay
GLASSBAR_API void CoreSetTaskbarEnabled(bool enabled);
GLASSBAR_API void CoreSetTaskbarOpacity(int opacity);       // 0–100
GLASSBAR_API void CoreSetTaskbarColor(int r, int g, int b);
GLASSBAR_API void CoreSetTaskbarBlur(bool enabled);
GLASSBAR_API void CoreSetTaskbarBlurAmount(int amount);     // 0–100 (XamlBridge intensity)

// Start Menu
GLASSBAR_API void CoreSetStartEnabled(bool enabled);
GLASSBAR_API void CoreSetStartMenuOpacity(int opacity);     // 0–100
GLASSBAR_API void CoreSetStartMenuBackgroundColor(unsigned int rgb);
GLASSBAR_API void CoreSetStartMenuTextColor(unsigned int rgb);
GLASSBAR_API void CoreSetStartMenuBorderColor(unsigned int rgb);
GLASSBAR_API void CoreSetStartMenuBlur(bool enabled);
GLASSBAR_API void CoreSetStartMenuHookEnabled(bool enabled);
GLASSBAR_API void CoreSetStartMenuPinned(bool pinned);
GLASSBAR_API void CoreSetStartMenuItems(bool cp, bool dm, bool ia, bool docs, bool pics, bool vids, bool recent);

// Global hotkey
GLASSBAR_API void CoreRegisterHotkey(int vk, int modifiers);
GLASSBAR_API void CoreUnregisterHotkey();

// Status / message pump
GLASSBAR_API bool CoreProcessMessages();
GLASSBAR_API void CoreGetStatus(CoreStatus* status);
```

---

## 5) Tehnologii utilizate

### Core (C++)
- **Limbaj:** C++20, MSVC
- **API-uri Win32:** `user32`, `gdi32`, `shell32`, `ole32`, `advapi32`, `dwmapi`
- **Transparență (22H2/23H2):** `SetWindowCompositionAttribute` (SWCA) — `ACCENT_ENABLE_ACRYLICBLURBEHIND`
- **Transparență (24H2/25H2+):** investigație experimentală `GlassBar.XamlBridge.dll` + XAML diagnostics / TAP în `explorer.exe`
- **Start Menu painting:** GDI (`SelectObject`, `DrawTextW`, `BitBlt`, `DrawIconEx`)
- **Hooks:** `WH_KEYBOARD_LL`, `WH_MOUSE_LL`
- **Shell:** `SHGetKnownFolderPath`, `SHGetFileInfoW`, `IShellLinkW`, `ShellExecuteW`
- **Build:** CMake 3.20+, output: DLL cu static CRT (`/MT`)
- **Logging:** macro `CF_LOG(level, message)` + thread id + timestamp

### Dashboard (C#)
- **.NET 8**, WinUI 3, XAML
- **P/Invoke** pentru toate apelurile la Core
- **JSON:** `System.Text.Json`
- **Single-instance:** `Mutex`
- **Thread safety:** `DispatcherQueue.TryEnqueue()` pentru update-uri UI cross-thread

---

## 6) Configurație persistentă

Config salvată în: `%LOCALAPPDATA%\GlassBar\config.json`

**Chei principale (schema Dashboard / local truth):**
- `TaskbarEnabled`, `TaskbarOpacity`, `TaskbarColorR/G/B`, `TaskbarBlur`, `BlurAmount`
- `StartEnabled`, `StartOpacity`, `StartBgColorR/G/B`, `StartTextColorR/G/B`, `StartBorderColorR/G/B`, `StartBlur`
- `CoreEnabled`, `IsFirstRun`, `HotkeyVk`, `HotkeyModifiers`
- `rightColumnItems` — visibility per item (Documents, Pictures, Music, Downloads, Control Panel etc.)
- `pinnedApps` — lista de aplicații pinned în Start Menu (separat în `pinned_apps.json`)

---

## 7) Starea proiectului

| Componentă | Status |
|-----------|--------|
| Taskbar overlay (toate edge-urile, multi-monitor, auto-hide) | ✅ Done |
| Renderer 22H2/23H2 (SWCA — transparență + RGB + Blur) | ✅ Done |
| Renderer 24H2/25H2+ (`GlassBar.XamlBridge.dll` / TAP path) | ⚠️ In Progress — active discovery implementat, BackgroundFill confirmat, efect vizibil nerealizat încă |
| Explorer restart recovery | ✅ Done |
| Start Menu replacement (Win7 two-column layout) | ✅ Done |
| All Programs tree (folder drill-down, hover submenus, keyboard nav) | ✅ Done |
| Pinned items (pin/unpin, custom icon picker) | ✅ Done |
| Recent items (UserAssist, right-click remove, exclusion list) | ✅ Done |
| Right-column items (system links, visibility toggles) | ✅ Done |
| Theme presets (Classic Win7 / Aero Glass / Dark) | ✅ Done |
| Power/session submenu (Sleep, Shut down, Restart) | ✅ Done |
| Config persistence (JSON) | ✅ Done |
| System tray + autostart | ✅ Done |
| Single-instance Dashboard | ✅ Done |
| Search box | ⚠️ Vizibil, nefuncțional (placeholder) |
| Global hotkey toggle overlay | ✅ Done |
| Auto-update check (GitHub releases) | ✅ Done |

---

## 8) Observabilitate (logging)

**Log obligatoriu (`GlassBar.log`):**
- Startup summary (versiune, init ok, config loaded, Windows build number)
- Taskbar found/lost + rect + edge
- Explorer restart detectat + recovery
- Erori `HRESULT` + context (file/line)

**Log path:** `%LOCALAPPDATA%\GlassBar\GlassBar.log`

---

## 9) Riscuri cunoscute

- **Taskbar pe 24H2/25H2+:** `AdviseVisualTreeChange` livrează doar 3-6 elemente root/container (nu livrează `TaskbarBackground` / `BackgroundFill`).
  `BackgroundFill` confirmat în island-ul Connection1, la adâncime 9 niveluri de la root, via UWPSpy.
  Active Discovery (walk recursiv via `VisualTreeHelper`) implementat și handle-urile root sunt cached; walk-ul nu a produs încă efect vizibil.
  **Bug cunoscut nerezolvat:** `QueueActiveDiscoveryFromHookProc` folosește `PostMessageW` care NU declanșează `WH_CALLWNDPROC` — discovery-ul din `OnVisualTreeChange` nu se execută niciodată.
  Fix necesar: înlocuire cu `SendNotifyMessageW` sau apel direct `XamlBridgeTryActiveTreeDiscovery()` din `OnVisualTreeChange`.
- **Hook multi-thread:** 9 thread-uri `explorer.exe` sunt hook-ate (confirmat pe build 26200) — fix implementat în ITER #22.
- **Re-toggle Core:** bug rezolvat în ITER #22 (`m_bridgeInited` nu era resetat la `ShutdownXamlBridge`); toggle off/on funcționează corect acum.
- **Config/startup:** există o regresie separată în care `GlassBar.log` poate raporta `Config not found, using defaults`
  chiar dacă `%LOCALAPPDATA%\GlassBar\config.json` există și conține valori persistate; startup-ul rămâne lent.
- **Search box:** nefuncțional momentan (placeholder vizual); nu afectează restul meniului.
- **DPI 200%+:** testat până la 150%; pot apărea artefacte vizuale la scaling foarte mare.
