# ChatAi.md — GlassBar Research & Session Continuity

> **Scop:** Fișier de referință tehnic pentru sesiuni cross-platform / cross-chat.
> Conține: diagnostic 25H2 taskbar, analiza WindHawk, planul de fix, starea cercetării.
> **Ultima actualizare:** 2026-04-29 — revizuire stare proiect + sincronizare documentație

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
| XamlBridge TAP injection | 🔄 ITER #19 | Fix thread context: InjectGlassBarTAP mutat în XamlBridgeHookProc (Shell_TrayWnd thread) |

**Branch de lucru activ:** `claude/update-project-docs-TEhqU`
**Ultimul merge în main:** PR #99 (squash, SHA `967ad2b`) — ITER #19: TAP injection pe Shell_TrayWnd thread + Codex P1 fix retry
**ITER #19 commit:** `967ad2b` (PR #99) — InjectGlassBarTAP mutat în XamlBridgeHookProc (Shell_TrayWnd UI thread)

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

## 5. Fix implementat — ITER #19 ✅

### Ce s-a schimbat

**Fișiere modificate:** `Core/XamlBridge/dllmain.cpp`, `Core/XamlBridge/TAPInjector.cpp`

```
[ÎNAINTE — ITER #11-#18, NU funcționa pe 25H2]
WH_CALLWNDPROC hook → passthrough pur
Worker thread (COINIT_APARTMENTTHREADED, thread separat) → InjectGlassBarTAP() ← THREAD GREȘIT

[DUPĂ — ITER #19 ✅]
WH_CALLWNDPROC hook (Shell_TrayWnd UI thread) → prima apelare → InjectGlassBarTAP() ← THREAD CORECT
Worker thread → wait g_tapScheduled (max 2s) → monitorizare shared memory + dispatch brushes
```

**Codul implementat:**
```cpp
// dllmain.cpp — g_tapScheduled flag
static std::atomic<bool> g_tapScheduled { false };

// XamlBridgeHookProc — prima apelare pe Shell_TrayWnd thread
extern "C" LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && !g_tapScheduled.exchange(true)) {
        if (!g_logPath[0]) InitLogPath();
        XBLog(L"XamlBridgeHookProc: first call on Shell_TrayWnd thread — injecting TAP [ITER #19]");
        InjectGlassBarTAP();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// WorkerThread — wait + fallback
for (int wait = 0; wait < 20 && !g_tapScheduled.load() && !g_stopping.load(); ++wait)
    Sleep(100);
if (g_tapScheduled.load()) {
    XBLog(L"WorkerThread: TAP injection scheduled from Shell_TrayWnd thread — OK");
} else {
    XBLog(L"WorkerThread: hook proc did not fire — fallback injection from worker thread");
    InjectGlassBarTAP();
}
```

### Codex P1 fix aplicat (commit cedaf15)

Codex a semnalat că versiunea inițială ITER #19 folosea `g_tapScheduled.exchange(true)` — o poartă one-shot. Dacă prima apelare a hook proc-ului are loc înainte ca XAML islands să fie înregistrate, injecția eșua silențios și nicio reîncercare nu mai era posibilă.

**Fix (commit cedaf15):** înlocuit one-shot cu `IsTapInited()` check + retry rate-limited 500ms.

```cpp
// TAPInjector.h — nou export
bool IsTapInited();  // returnează g_tapInited.load()

// TAPInjector.cpp — implementare
bool IsTapInited() { return g_tapInited.load(); }
// g_tapInited.store(true) setat DOAR după successCount > 0

// dllmain.cpp — hook proc cu retry
static std::atomic<bool>  g_tapScheduled      { false };
static std::atomic<DWORD> g_lastTapAttemptTick { 0 };

extern "C" LRESULT CALLBACK XamlBridgeHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && !IsTapInited()) {
        g_tapScheduled.store(true);            // semnal "hook e activ" (nu "deja încercat")
        DWORD now  = GetTickCount();
        DWORD last = g_lastTapAttemptTick.load(std::memory_order_relaxed);
        if (now - last >= 500u) {
            g_lastTapAttemptTick.store(now, std::memory_order_relaxed);
            XBLog(L"XamlBridgeHookProc: attempting TAP injection on Shell_TrayWnd thread [ITER #19]");
            InjectGlassBarTAP();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
```

---

## 6. Analiză comparativă: WindHawk vs GlassBar

### 6.1 Mecanism de injecție

| Aspect | WindHawk TaskbarStyler | GlassBar XamlBridge |
|--------|----------------------|---------------------|
| Cum intră în explorer.exe | Engine WindHawk injectează automat mod DLL | `WH_CALLWNDPROC` via `SetWindowsHookEx` pe `Shell_TrayWnd` |
| Scheduling pe thread corect | `RunFromWindowThread(hwnd, fn)` — postează task în message queue | Hook proc **este deja** pe thread-ul target prin natura WH_CALLWNDPROC |
| Trigger re-injecție | Hook `CreateWindowExW` — reacție exactă la apariția island-ului | Rate-limit 500ms pe orice mesaj WH_CALLWNDPROC |

### 6.2 Găsirea XAML island window

**WindHawk:** enumerează child windows ale `Shell_TrayWnd`, caută clasa `Windows.UI.Composition.DesktopWindowContentBridge`, extrage `HWND` și îl pasează la `InitializeXamlDiagnosticsEx`.

**GlassBar:** loopează pipe names `VisualDiagConnection1..N` fără a ști HWND-ul — pasează `GetCurrentProcessId()` și lasă API-ul să localizeze.

**Risc GlassBar:** Dacă Microsoft redenumește pipe-urile `VisualDiagConnectionN` (posibil în builds viitoare), abordarea noastră se rupe. Abordarea WindHawk via class name este mai rezistentă.

### 6.3 Parametrii `InitializeXamlDiagnosticsEx`

**WindHawk:**
```cpp
InitializeXamlDiagnosticsEx(
    L"VisualDiagConnection1",
    GetCurrentProcessId(),
    nullptr,
    szModulePath,         // calea către modul styler
    CLSID_TabletInputPanel, // CLSID dummy de sistem — trucul cheie!
    nullptr
);
```

**GlassBar:**
```cpp
InitializeXamlDiagnosticsEx(
    connectionName,       // VisualDiagConnectionN iterație
    GetCurrentProcessId(),
    nullptr,
    dllPath,
    CLSID_GlassBarTAP,   // propriul nostru CLSID
    nullptr
);
```

**Diferența critică:** WindHawk reutilizează `CLSID_TabletInputPanel` (CLSID de sistem) ca dummy — nu are nevoie de un TAP object real. Scopul lui e doar să obțină canalul de diagnostics și `IVisualTreeService3`. GlassBar înregistrează `CLSID_GlassBarTAP` propriu, care necesită `DllGetClassObject` funcțional și activare COM înainte ca `IVisualTreeService3` să fie ready.

**Suprafață de eșec suplimentară în GlassBar:** dacă activarea COM eșuează, pierdem accesul la `IVisualTreeService3` chiar dacă diagnostics channel e disponibil.

### 6.4 Strategie retry

| | WindHawk | GlassBar ITER #19 (post Codex P1) |
|--|---------|----------------------------------|
| Trigger | `CreateWindowExW` hook — event-driven | `WH_CALLWNDPROC` + rate-limit 500ms |
| Overhead | Zero (reacție exactă la creare island) | Mic (un check `IsTapInited()` per mesaj) |
| Acuratețe timing | Perfect — rulează exact când island apare | Aproximativ — orice mesaj declanșează |

### 6.5 Aplicarea efectului (brush-uri)

Ambele folosesc `IVisualTreeService3::SetProperty`. Structura XAML tree țintită este identică:
- `Rectangle#BackgroundFill` → `AcrylicBrush` / `SolidColorBrush`
- `Rectangle#BackgroundStroke` → același
- Sub `Taskbar.TaskbarBackground` → sub `Windows.UI.Composition.DesktopWindowContentBridge`

Nicio diferență funcțională aici.

### 6.6 Ce putem împrumuta de la WindHawk (ITER #20 candidat)

1. **Skip custom CLSID** — reutilizează `CLSID_TabletInputPanel` sau alt CLSID de sistem ca dummy, elimină dependința de DllGetClassObject/COM factory (suprafață mai mică de eșec)
2. **Hook `CreateWindowExW`** pentru re-injecție event-driven în loc de polling via mesaje — timing perfect, overhead zero
3. **Fallback enumeration by window class** (`DesktopWindowContentBridge`) dacă pipe-urile `VisualDiagConnectionN` nu sunt găsite

### Dacă ITER #19 nu rezolvă: Next step (WindHawk-aligned)

Dacă `Shell_TrayWnd` thread ≠ thread-ul XAML island pe 25H2, abordarea completă:
1. Hook `LoadLibraryExW` → detectare `Windows.UI.Xaml.dll` load
2. Hook `CreateWindowExW` → detectare `Windows.UI.Composition.DesktopWindowContentBridge` sub `Shell_TrayWnd`
3. La detecție → `RunFromWindowThread(hContentBridge)` + `InjectGlassBarTAP`
4. Necesită MinHook / Detours sau inline hook manual (fără dependință externă în GlassBar)

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
| 2026-04-05 | `claude/xamlbridge-knowledge-refresh-TGQRn` | Dead code cleanup, MD sync, WindHawk research, ITER #19 + Codex P1 fix | PR #99 — build in progress |

---

## 10. Instrucțiuni pentru agentul următor

**Dacă continui investigația XamlBridge pe 25H2:**

1. Citește **tot** acest fișier prima dată.
2. **Stare curentă (2026-04-05):** ITER #19 implementat (commit `236a629`) + Codex P1 fix (commit `cedaf15`, PR #99 in progress).
3. `InjectGlassBarTAP()` rulează acum pe Shell_TrayWnd thread via `XamlBridgeHookProc`. Retry rate-limited 500ms până `IsTapInited()` = true.
4. **Dacă ITER #19 nu produce efect vizibil pe 25H2:** aplică ITER #20 (secțiunea 6.6) — reutilizează CLSID dummy + hook `CreateWindowExW`.
5. Fișiere cheie: `Core/XamlBridge/dllmain.cpp`, `TAPInjector.cpp`, `TAPObject.cpp`, `VisualTreeWatcher.cpp`.
6. Comparație completă WindHawk vs GlassBar: **secțiunea 6** din acest fișier.

**Dacă lucrezi pe altceva (Start Menu, Dashboard, etc.):**

- Branch de dezvoltare: `claude/xamlbridge-knowledge-refresh-TGQRn`
- Dead code eliminat: `main.cpp`, `IpcBridge.*`, `CoreProcessManager.cs`, `IpcClient.cs`
- Config: cheile sunt PascalCase (`TaskbarEnabled`, `StartOpacity`, etc.)
- API exports: `GLASSBAR_API CoreInitialize()`, `CoreSetTaskbar*()`, `CoreRegisterHotkey()`
