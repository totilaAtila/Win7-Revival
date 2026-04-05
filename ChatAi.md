# ChatAi.md — GlassBar Research & Session Continuity

> **Scop:** Fișier de referință tehnic pentru sesiuni cross-platform / cross-chat.
> Conține: diagnostic 25H2 taskbar, analiza WindHawk, planul de fix, starea cercetării.
> **Ultima actualizare:** 2026-04-05

---

## 1. Starea curentă a proiectului (snapshot)

| Componentă | Status | Note |
|-----------|--------|------|
| Taskbar overlay 22H2/23H2 (SWCA) | ✅ Funcțional | Full control: transparență + RGB + Blur |
| Taskbar overlay 24H2/25H2+ | ⚠️ Experimental | Calea XamlBridge/TAP — nu produce efect vizibil confirmat |
| Start Menu replacement (Win7 layout) | ✅ Funcțional | Complet |
| Global hotkey toggle | ✅ Funcțional | CoreRegisterHotkey / CoreUnregisterHotkey |
| Auto-update check | ✅ Funcțional | UpdateChecker.cs → GitHub Releases API |
| Config persistence | ✅ Funcțional | Aliniat PascalCase; fix-ul config-not-found din PR #98 |
| XamlBridge TAP injection | ⚠️ Parțial | DLL se injectează, tree walk merge, efect vizibil lipsă |

**Branch de lucru activ:** `claude/xamlbridge-knowledge-refresh-TGQRn`
**Ultimul merge în main:** PR #98 (squash, SHA `50e04cb8`) — checkpoint XamlBridge 25H2 + Codex P1 fix

---

## 2. Arhitectura XamlBridge — starea implementată

### Fișiere XamlBridge (`Core/XamlBridge/`)

| Fișier | Rol |
|--------|-----|
| `dllmain.cpp` | DLL entry; `WorkerThread`; logging; `XamlBridgeHookProc` export; COM exports |
| `TAPInjector.cpp/h` | `InjectGlassBarTAP()` — buclă `VisualDiagConnectionN` → TAP registration |
| `TAPObject.cpp/h` | COM factory (`DllGetClassObject`) + `GlassBarTAP::SetSite` + `AdviseThread` |
| `VisualTreeWatcher.cpp/h` | `IVisualTreeServiceCallback2` — detectare `TaskbarBackground`, walk, brush apply |
| `XamlBridgeCommon.h` | Globals: `g_visualTreeService3`, `g_xamlDiagnostics`, `g_knownShapes`, etc. |
| `SharedBlurState.h` | Shared memory IPC: `Local\GlassBar_XamlBridge_v1` (atomic, fără mutex) |
| `XamlBridge.def` | Exports: `XamlBridgeHookProc`, `DllGetClassObject`, `DllCanUnloadNow` |

### Flow de injecție actual (GlassBar)

```
Renderer::InitXamlBridge()                     [Core thread]
  │
  ├─ CreateFileMapping → SharedBlurState        [shared memory creat]
  ├─ LoadLibraryW(GlassBar.XamlBridge.dll)
  └─ SetWindowsHookEx(WH_CALLWNDPROC, XamlBridgeHookProc, explorerTid)
       │
       └─ SendMessageTimeout(Shell_TrayWnd, WM_NULL) → hook fires → DLL loaded in explorer.exe
            │
            └─ DllMain(DLL_PROCESS_ATTACH) → IsRunningInExplorer() → CreateThread(WorkerThread)
                 │
                 └─ WorkerThread [COINIT_APARTMENTTHREADED, thread separat]
                      ├─ OpenFileMapping(SharedBlurState) — wait up to 10s
                      ├─ InjectGlassBarTAP()              ← ⚠️ PROBLEMA (thread greșit)
                      └─ polling loop: version change → TryRunAsync brush updates
```

### Flow SetSite / AdviseVisualTreeChange

```
InitializeXamlDiagnosticsEx(VisualDiagConnectionN, pid, "", dllPath, CLSID_GlassBarTAP)
  │
  └─ DllGetClassObject(CLSID_GlassBarTAP) → GlassBarTAP COM object
       │
       └─ SetSite(pSite)                        [XAML runtime thread — !! NOT worker thread]
            ├─ QI → IVisualTreeService3 + IXamlDiagnostics
            ├─ FreeLibrary(g_hModule)            [balance implicit LoadLibrary by XAML runtime]
            └─ CreateThread(AdviseThreadProc)
                 │
                 └─ AdviseVisualTreeChange(watcher)
                      │
                      └─ OnVisualTreeChange callbacks → walk tree → ApplyBrushParams
```

---

## 3. Diagnosticul problemei pe 25H2 (build ≥ 26200)

### Simptome observate (din logs sesiunile anterioare)

- DLL se injectează cu succes în `explorer.exe`
- `InjectGlassBarTAP` raportează SUCCESS pe N islands (`VisualDiagConnectionN`)
- `SetSite` este apelat (logat)
- `AdviseVisualTreeChange` returnează S_OK
- `OnVisualTreeChange` **NU** se declanșează SAU se declanșează dar `ApplyBrushParams` nu produce efect vizibil
- Tree walk găsește `TaskbarBackground` și `BackgroundFill` (logat)
- Dar `SetProperty` / `ClearProperty` nu produce schimbare vizibilă

### Cauza rădăcină identificată: thread context greșit

**`InitializeXamlDiagnosticsEx` este apelat de pe worker thread (apartament separat),
nu de pe thread-ul XAML island-ului care gestionează `Shell_TrayWnd`.**

Pe Windows 22H2/23H2: XAML diagnostics accepta registrarea de pe orice thread.
Pe Windows 25H2 (build 26200+): necesită apelul de pe thread-ul corect al XAML island-ului.

---

## 4. Analiza WindHawk `windows-11-taskbar-styler` (funcționează pe 25H2)

### Sursă analizată

Mod WindHawk: `windows-11-taskbar-styler.wh.cpp` (~467KB)
Repository: `ramensoftware/windhawk-mods`

### Diferențele față de GlassBar

| Aspect | GlassBar (actual) | WindHawk (funcțional) |
|--------|------------------|----------------------|
| Mecanism injecție | `WH_CALLWNDPROC` hook passthrough | Hook `CreateWindowExW` + `LoadLibraryExW` |
| Thread TAP injection | Worker thread separat | Thread-ul ferestrei XAML (`RunFromWindowThread`) |
| Detectare XAML host | Nu detectează explicit | Caută `Windows.UI.Composition.DesktopWindowContentBridge` child al `Shell_TrayWnd` |
| `InitializeXamlDiagnosticsEx` | Apelat manual din worker | Interceptat (hook) + re-apelat pe thread-ul corect |
| TAP registration | Bucla 1..10000, continuă | Break la primul non-`ERROR_NOT_FOUND` (un island per apel) |

### Cum găsește WindHawk fereastra XAML corectă

```cpp
// Găsește DesktopWindowContentBridge ca child al Shell_TrayWnd
HWND GetTaskbarUiWnd() {
    HWND hTaskbarWnd = FindCurrentProcessTaskbarWnd(); // Shell_TrayWnd
    return FindWindowEx(hTaskbarWnd, nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
}
```

### Cum WindHawk rulează pe thread-ul corect

```cpp
// Wh_ModAfterInit:
HWND hTaskbarUiWnd = GetTaskbarUiWnd();
if (hTaskbarUiWnd) {
    RunFromWindowThread(hTaskbarUiWnd, [](PVOID) {
        InitializeForCurrentThread(); // → InjectWindhawkTAP()
    }, nullptr);
}

// RunFromWindowThread internals:
// 1. SetWindowsHookExW(WH_CALLWNDPROC, ..., targetThreadId)
// 2. SendMessage la fereastra target → hook fires pe thread-ul corect
// 3. Callback rulează pe acel thread → InjectWindhawkTAP() apelat de pe thread-ul XAML
// 4. UnhookWindowsHookEx
```

### Brushes și XAML elements (WindHawk, funcționale pe 25H2)

**Elemente XAML targetate (căi confirmate):**
```
Taskbar.TaskbarFrame
Taskbar.TaskbarFrame > Grid#RootGrid
Taskbar.TaskbarBackground
Rectangle#BackgroundFill        ← principala țintă pentru culoare/transparență
Rectangle#BackgroundStroke
Grid#SystemTrayFrameGrid
```

**Brush types care funcționează:**
```xml
<!-- Transparent complet -->
<SolidColorBrush Color="Transparent"/>

<!-- Culoare solidă cu alpha -->
<SolidColorBrush Color="#CC000000"/>

<!-- AcrylicBrush (WinUI 2.x) -->
<AcrylicBrush TintColor="#80000000" TintOpacity="0.8" FallbackColor="#CC000000"/>

<!-- WindhawkBlur (composition effect custom) -->
<WindhawkBlur BlurAmount="15" TintColor="#25323232" TintOpacity="0.8"/>
```

**Notă:** WindhawkBlur folosește `Windows.Graphics.Effects` + `Windows.UI.Composition`
(D2D GaussianBlur effect pe CompositionEffectBrush) — NU SetWindowCompositionAttribute.

### APIs folosite de WindHawk (nu SWCA!)

```cpp
// Nu folosește SetWindowCompositionAttribute pe 25H2.
// Aplică direct pe proprietățile WinUI3 XAML ale elementelor din visual tree:

IVisualTreeService3::SetProperty(elementHandle, brushHandle, propertyIndex)
IVisualTreeService3::ClearProperty(elementHandle, propertyIndex)
IXamlDiagnostics::GetHandleFromIInspectable(brushObj, &brushHandle)

// Pentru blur custom:
Windows::UI::Composition::Compositor
Windows::Graphics::Effects::GaussianBlurEffect (D2D)
CompositionEffectBrush cu BackdropBrush source
```

---

## 5. Planul de fix (ITER #19 propus)

### Abordarea: muta `InjectGlassBarTAP()` pe thread-ul corect

**Modificare minimă — un singur fișier: `Core/XamlBridge/dllmain.cpp`**

**Pasul 1:** Adaugă atomic flag `g_tapScheduled` și funcție helper `FindContentBridgeWnd`.

**Pasul 2:** Modifică `XamlBridgeHookProc` (actualmente passthrough) să apeleze
`InjectGlassBarTAP()` la prima declanșare. Hook proc rulează deja pe thread-ul
`Shell_TrayWnd` — același thread care găzduiește XAML islands pe 25H2.

**Pasul 3:** Elimină apelul `InjectGlassBarTAP()` din `WorkerThread`.

```cpp
// ÎNAINTE (dllmain.cpp:244-247):
extern "C" LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    return CallNextHookEx(nullptr, nCode, wParam, lParam);  // passthrough pur
}

// DUPĂ:
static std::atomic<bool> g_tapScheduled { false };

extern "C" LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && !g_tapScheduled.exchange(true)) {
        // Rulăm pe thread-ul Shell_TrayWnd — corect pentru XAML islands pe 25H2
        XBLog(L"XamlBridgeHookProc: scheduling TAP injection on Shell_TrayWnd thread");
        InjectGlassBarTAP();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
```

**Pasul 4 (opțional, WindHawk-aligned):** Găsește `DesktopWindowContentBridge` și
rulează pe thread-ul acelei ferestre (mai precis decât `Shell_TrayWnd`):

```cpp
// În WorkerThread sau hook proc, înainte de InjectGlassBarTAP:
HWND hContentBridge = FindWindowEx(
    FindWindowW(L"Shell_TrayWnd", nullptr), nullptr,
    L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
// Dacă găsit, folosi SendMessage cu custom message pe thread-ul lui
```

### Riscuri

| Risc | Probabilitate | Mitigare |
|------|--------------|----------|
| Hook proc apelat prea devreme (XAML nu e gata) | Medie | Retry cu `Sleep(500)` + re-check în worker thread |
| `InjectGlassBarTAP` blochează hook proc prea mult | Scăzută | Muta în `PostMessage`/thread separat cu sync |
| `Shell_TrayWnd` thread ≠ XAML island thread pe 25H2 | Medie | Fallback la `DesktopWindowContentBridge` thread |

### Alternativă (mai robustă, mai complexă): Hookuri funcție

Dacă muta în hook proc nu rezolvă, abordarea WindHawk completă:
1. `LoadLibraryExW` hook → detectare `Windows.UI.Xaml.dll` load
2. `CreateWindowExW` hook → detectare `DesktopWindowContentBridge` creation
3. La detecție → `RunFromWindowThread` + `InjectGlassBarTAP`
4. Necesită o librărie de hooking (MinHook / Detours) sau inline hook manual

---

## 6. XAML Visual Tree pe Windows 25H2 — structura confirmată

```
Shell_TrayWnd
└── Windows.UI.Composition.DesktopWindowContentBridge
    └── [XAML Island]
        └── Taskbar.TaskbarFrame
            └── Grid#RootGrid
                └── Taskbar.TaskbarBackground
                    └── Grid
                        ├── Rectangle#BackgroundFill    ← ȚINTA PRINCIPALĂ (Fill/Stroke)
                        └── Rectangle#BackgroundStroke
```

**Notă critică:** Pe 25H2, `Shell_TrayWnd` NU mai primește direct `SetWindowCompositionAttribute`.
Efectul trebuie aplicat pe `Rectangle#BackgroundFill` din interiorul XAML tree, via
`IVisualTreeService3::SetProperty` cu un brush corespunzător.

---

## 7. APIs de referință

### IVisualTreeService3 (calea corectă pe 25H2)

```cpp
// Header: <windows.ui.xaml.hosting.desktopwindowxamlsource.h> sau diagnostics IDL
HRESULT AdviseVisualTreeChange(IVisualTreeServiceCallback* pCallback);
HRESULT UnadviseVisualTreeChange(IVisualTreeServiceCallback* pCallback);
HRESULT GetPropertyIndex(InstanceHandle object, LPCWSTR propertyName, unsigned int* pIndex);
HRESULT GetProperty(InstanceHandle object, unsigned int index, InstanceHandle* pValue);
HRESULT SetProperty(InstanceHandle object, InstanceHandle value, unsigned int index);
HRESULT ClearProperty(InstanceHandle object, unsigned int index);
```

### IXamlDiagnostics

```cpp
HRESULT GetHandleFromIInspectable(IInspectable* pObject, InstanceHandle* pHandle);
HRESULT GetIInspectableFromHandle(InstanceHandle instanceHandle, IInspectable** ppInstance);
HRESULT GetApplication(InstanceHandle* pHandle);
HRESULT GetUiLayer(InstanceHandle* pHandle);
HRESULT GetDispatcher(IInspectable** ppDispatcher);
```

### InitializeXamlDiagnosticsEx

```cpp
// Din Windows.UI.Xaml.dll
using PFN_INITIALIZE_XAML_DIAGNOSTICS_EX = HRESULT (WINAPI*)(
    LPCWSTR endPointName,           // e.g., L"VisualDiagConnection1"
    DWORD   pid,                    // GetCurrentProcessId()
    LPCWSTR wszDllXamlDiagnostics,  // L"" (empty — use already-loaded)
    LPCWSTR wszTAPDllName,          // path to GlassBar.XamlBridge.dll
    CLSID   tapClsid,               // CLSID_GlassBarTAP
    LPCWSTR wszInitializationData   // nullptr
);
```

---

## 8. Resurse externe analizate

| Resursă | Utilitate | Concluzie |
|---------|-----------|-----------|
| `ramensoftware/windhawk-mods` → `windows-11-taskbar-styler.wh.cpp` | ⭐⭐⭐⭐⭐ | Referință principală — funcționează pe 25H2; sursa diagnosticului thread |
| `Osprey00/windows-11-taskbar-styling-guide` | ⭐⭐ | Doar themes user-facing pentru WindHawk mod; fără cod low-level |
| XAML Diagnostics API (docs.microsoft.com) | ⭐⭐⭐⭐ | Documentație oficială pentru IVisualTreeService3 |

---

## 9. Log sesiuni anterioare (rezumat)

| Sesiune | Branch | Ce s-a lucrat | Rezultat |
|---------|--------|--------------|---------|
| Session 24 | `main` | Faza 3 XamlBridge (shared memory + WH_CALLWNDPROC) | DLL injectat, brush nu produce efect |
| Session 29 | checkpoint branch | ITER #1-#18: TAP/VisualTreeWatcher iterații | Tree walk merge, efect vizibil lipsă |
| 2026-04-04 | `checkpoint/xamlbridge-25h2-2026-04-04` | PR #98 checkpoint + Codex review | Merged în main (SHA 50e04cb8) |
| 2026-04-05 | `claude/xamlbridge-knowledge-refresh-TGQRn` | Dead code cleanup, MD sync, WindHawk research | Prezent — fix propus ITER #19 |

---

## 10. Instrucțiuni pentru agentul următor

**Dacă continui investigația XamlBridge pe 25H2:**

1. Citește acest fișier primul.
2. Starea actuală: TAP se injectează, `SetSite` e apelat, `AdviseVisualTreeChange` OK, dar efect vizibil absent.
3. **Fix propus (ITER #19):** Muta `InjectGlassBarTAP()` în `XamlBridgeHookProc` (thread Shell_TrayWnd).
4. Fișiere cheie: `Core/XamlBridge/dllmain.cpp`, `TAPInjector.cpp`, `TAPObject.cpp`, `VisualTreeWatcher.cpp`.
5. Referință primară: WindHawk `windows-11-taskbar-styler.wh.cpp` (secțiunea 4 din acest fișier).
6. Dacă ITER #19 eșuează: implementează hookuri `CreateWindowExW` + `LoadLibraryExW` (abordarea completă WindHawk).

**Dacă lucrezi pe altceva (Start Menu, Dashboard, etc.):**

- Branch de dezvoltare: `claude/xamlbridge-knowledge-refresh-TGQRn`
- Dead code eliminat: `main.cpp`, `IpcBridge.*`, `CoreProcessManager.cs`, `IpcClient.cs`
- Config: cheile sunt PascalCase (`TaskbarEnabled`, `StartOpacity`, etc.)
- API exports: `GLASSBAR_API CoreInitialize()`, `CoreSetTaskbar*()`, `CoreRegisterHotkey()`
