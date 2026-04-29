
# WORKLOG — GlassBar
Last updated: 2026-04-29 (ITER #22 — active tree discovery + multi-thread hooks + re-toggle fix)

---

## CURRENT FOCUS: 24H2/25H2+ Taskbar Transparency (XamlBridge / TAP)

### Stare actuală (2026-04-29)
- SWCA (`SetWindowCompositionAttribute`) este ignorat pe `Shell_TrayWnd` pe build ≥ 26000.
- Soluția experimentală: `GlassBar.XamlBridge.dll` injectat în `explorer.exe` via `WH_CALLWNDPROC`.
- TAP injection funcțional; `AdviseVisualTreeChange` livrează numai 3-6 elemente root.
- Active Discovery implementat; handle-urile root sunt cached; walk-ul recursiv nu a produs efect vizibil încă.
- **Bug deschis:** `PostMessageW` (folosit în `QueueActiveDiscoveryFromHookProc`) nu declanșează `WH_CALLWNDPROC` → walk nu rulează din `OnVisualTreeChange`.

### Calea completă spre `BackgroundFill` (confirmat UWPSpy, build 26200)
```
Connection1 → DesktopWindowXamlSource
  └─ RootScrollViewer
       └─ ScrollContentPresenter
            └─ Border
                 └─ Grid
                      └─ Taskbar.TaskbarFrame#TaskbarFrame
                           └─ Grid#RootGrid
                                └─ Taskbar.TaskbarBackground#BackgroundControl
                                     └─ Grid
                                          ├─ Rectangle#BackgroundFill   ← Fill: AcrylicBrush
                                          └─ Rectangle#BackgroundStroke
```

---

## SESSION 2026-04-29 — ITER #22: Active Discovery + Multi-thread Hooks

### Descoperiri cheie

1. **`AdviseVisualTreeChange` truncat** — livrează doar elementele infra root/container
   (`DesktopWindowXamlSource → PopupRoot → RootScrollViewer → ScrollContentPresenter → Border → Grid`).
   `TaskbarFrame`, `TaskbarBackground`, `BackgroundFill` NU sunt livrate. Walk activ necesar.

2. **BackgroundFill în aceeași insulă (Connection1)** — UWPSpy dump confirmă. Adâncime: 9 niveluri.
   Fill nativ: `AcrylicBrush` deja aplicat de Windows.

3. **Windhawk (`windows-11-taskbar-styler`):** `AdviseVisualTreeChange` trebuie apelat de pe thread nou
   (evită hang `Advising::RunOnUIThread`). Walk via `VisualTreeHelper.GetChild()` funcționează pe UI thread.
   Target Windhawk: `L"Taskbar.TaskbarFrame > Grid#RootGrid > Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill"`,
   stil `L"Fill=Transparent"`.

4. **Doar Connection1 accesibil** — Connection2-22 returnează `ERROR_NOT_FOUND` pe build 26200.

### Fix-uri implementate

**Fix 1 — `m_bridgeInited = false` în `ShutdownXamlBridge()` (`Core/Renderer.cpp`)**
- Bug: toggle off/on Core sărea `InitXamlBridge()` complet (flag necurățat).

**Fix 2 — Type matching flexibil (`Core/XamlBridge/VisualTreeWatcher.cpp`)**
- `wcscmp` exact → `wcsstr` partial pentru `TaskbarBackground`.

**Fix 3 — Multi-thread hooks via `EnumWindows` (`Core/Renderer.h` + `Renderer.cpp`)**
- `HHOOK m_hInjHook` → `std::vector<HHOOK> m_hInjHooks`.
- Hook instalat pe toate thread-urile unice `explorer.exe`.
- Confirmat: **9 explorer thread(s)** pe build 26200.

**Fix 4 — Active Tree Discovery (`VisualTreeWatcher.cpp` + `dllmain.cpp`)**
- `IsInterestingRootCandidate`: filtrează elementele relevante.
- `CacheRootHandle` / `g_rootHandles`: cache handle-uri root la sosire via `AdviseVisualTreeChange`.
- `ActiveWalkElement`: walk recursiv (depth ≤ 48, maxNodes ≤ 4096) via `VisualTreeHelper.GetChild()`.
- `XamlBridgeTryActiveTreeDiscovery`: iterează `g_rootHandles`, apelează `ActiveWalkElement`.
- `XamlBridgeHookProc` Path A: după `InjectGlassBarTAP()`, apelează `XamlBridgeTryActiveTreeDiscovery()`.

### Rezultate test (2026-04-29 13:41:34)
- ✅ 9 hook-uri instalate
- ✅ Handle-uri cached: `0x311B9C0` (DesktopWindowXamlSource), `0x103AB9D8` (RootScrollViewer)
- ✅ `QueueActiveDiscoveryFromHookProc` declanșat
- ❌ `XamlBridgeTryActiveTreeDiscovery` cu handle-urile cached: **nu a rulat** (sesiune prea scurtă / PostMessageW bug)

### Bug deschis — PostMessageW nu declanșează hookproc
`QueueActiveDiscoveryFromHookProc` → `PostMessageW(hwndTray, WM_NULL, ...)` — **greșit**.
`WH_CALLWNDPROC` se declanșează NUMAI la mesaje trimise (`SendMessage` / `SendNotifyMessage`), NU la cele postate.
WorkerThread trimite `SendNotifyMessageW` la fiecare ~600ms — dacă sesiunea durează suficient, walk-ul poate rula.
**Fix necesar:** apel direct `XamlBridgeTryActiveTreeDiscovery()` din `OnVisualTreeChange` după `CacheRootHandle`.

### Fișiere modificate
- `Core/Renderer.cpp`, `Core/Renderer.h`
- `Core/XamlBridge/VisualTreeWatcher.cpp`, `dllmain.cpp`, `TAPInjector.cpp`
- `Dashboard/MainViewModel.cs`

---

## SESSION 2026-04-05 — ITER #19: TAP pe UI thread corect

### Context
`InitializeXamlDiagnosticsEx` pe 25H2 trebuie apelat de pe thread-ul XAML island (`Shell_TrayWnd` UI thread).
GlassBar îl apela din `WorkerThread` separat → eșec.

### Fix ITER #19 (`Core/XamlBridge/dllmain.cpp`, `TAPInjector.cpp`)
- `InjectGlassBarTAP()` mutat din `WorkerThread` → `XamlBridgeHookProc` (se execută pe Shell_TrayWnd thread).
- `g_tapScheduled` atomic flag adăugat.
- `WorkerThread` așteaptă `g_tapScheduled` max 2s, continuă cu polling loop.
- Fallback: dacă hook nu s-a declanșat, worker încearcă injecția (scenariu anormal, pe 25H2 poate eșua).
- PR #98 merged în main (SHA `50e04cb8`).

---

## XamlBridge — Căi testate și rezultate

| Tentativă | Rezultat | Notă |
|-----------|----------|------|
| `InitializeXamlDiagnosticsEx` din WorkerThread | ❌ Eșec pe 25H2+ | Thread greșit; mutat în ITER #19 |
| `AdviseVisualTreeChange` — așteptare `TaskbarBackground` | ❌ Nu vine | Livrează doar 3-6 elemente infra |
| `wcscmp` exact pentru `TaskbarBackground` | ❌ Fragil | Înlocuit cu `wcsstr` în ITER #22 |
| Hook pe un singur thread (`Shell_TrayWnd`) | ❌ Insuficient | Înlocuit cu `EnumWindows` 9 thread-uri |
| `PostMessageW` pentru triggering hookproc | ❌ Nu declanșează WH_CALLWNDPROC | Bug identificat; fix necesar |
| Core toggle off/on fără `m_bridgeInited = false` | ❌ Skip `InitXamlBridge` | Fix aplicat ITER #22 |
| `TryRunAsync` pe XAML CoreDispatcher | ❌ Nu funcționează în Win32-hosted islands 25H2 | Abandonat; replaced cu hookproc ping |
| Walk recursiv `ActiveWalkElement` via WorkerThread | ❌ Thread greșit | Trebuie apelat pe XAML UI thread |
| `SendNotifyMessageW` ping din WorkerThread | ✅ Declanșează hookproc | Folosit pentru settings re-apply |
| Active Discovery din `XamlBridgeHookProc` (Path A) | 🔄 În desfășurare | Implementat, nu a produs efect vizibil încă |

**Concluzie:** Tot lucrul XAML (walk, `SetValue`, `ClearValue`) trebuie efectuat pe `Shell_TrayWnd` UI thread (`XamlBridgeHookProc`). WorkerThread coordonează exclusiv prin `SendNotifyMessageW` ping.

---

## Feature History (Start Menu + Core — complet)

### Start Menu (toate fazele complete — merged main)

| Sesiune | Feature | Status |
|---------|---------|--------|
| S1 | Win7 two-column layout (GDI), right column links funcționale | ✅ |
| S2 | All Programs tree (enumerare `FOLDERID_Programs`, drill-down, hover submenus) | ✅ |
| S3.1 | Keyboard nav (Up/Down/Enter/Esc) în Programs + AllPrograms | ✅ |
| S3.2 | Mouse wheel scroll în AllPrograms | ✅ |
| S3.3 | Hover-to-open submenu lateral (400→50ms delay) | ✅ |
| S4 | Layout polish (search box eliminat, coloane echilibrate) | ✅ |
| S5 | Window size refinement | ✅ |
| S6 | Iconițe reale din shell (pinned, All Programs, right column) | ✅ |
| S7 | Recently used programs (UserAssist registry, ROT13 decode) | ✅ |
| S8 | First-run safe mode (IsFirstRun flag, TaskbarEnabled/StartEnabled=false implicit) | ✅ |
| S9 | Power submenu complet (Shutdown/Restart/Sleep/Hibernate/LogOff/Lock) | ✅ |
| S10 | UWP icon fallback (FindLnkPathByName), shutdown privilege fix | ✅ |
| S15 | Flickering fix: WM_ERASEBKGND handler + double buffering GDI | ✅ |
| S16 | Blur switch funcțional, text shadow, hover animație, glow border, avatar real | ✅ |
| S17 | Fluidity: PostMessage în hooks, fereastră pre-creată, CacheMenuPosition, fade-in 80ms | ✅ |
| S17 | Dynamic pinned list (JSON persistence), right-click pin/unpin context menus | ✅ |
| S18 | GDI leak fix (IconCache), debounce slidere, thread safety, fallback renderer | ✅ |
| S21 | CPU optimization (poll 250→500ms), Win key combos fix (state machine), AeroGlass preset | ✅ |

### Core / Taskbar (complete)

| Sesiune | Feature | Status |
|---------|---------|--------|
| — | Taskbar overlay extern (SWCA, 22H2/23H2), click-through, multi-monitor | ✅ |
| — | Explorer restart recovery, auto-hide support | ✅ |
| — | System tray + autostart (registry + `/autostart` flag) | ✅ |
| — | Config persistence JSON (`%LOCALAPPDATA%\GlassBar\config.json`) | ✅ |
| — | Global hotkey toggle overlay | ✅ |
| — | Auto-update check (GitHub Releases API) | ✅ |
| — | Theme presets sidebar (Win7 Aero / Dark) | ✅ |
| — | 24H2/25H2+ overlay window approach (WS_POPUP TOPMOST, LWA_ALPHA=0 pe Shell_TrayWnd) | ✅ |
| ITER #19 | XamlBridge TAP injection pe UI thread corect | ✅ |
| ITER #22 | Multi-thread hooks (9 thread-uri), re-toggle fix, Active Discovery infrastructure | ✅ (fără efect vizibil încă) |

### Bug-uri semnificative rezolvate (note tehnice)

- **Silent crash la startup:** 4 versiuni Windows SDK simultan → runtime incompatibil. Fix: dezinstalat 3 versiuni.
- **Cross-thread UI crash (0x8001010E):** `OnCoreRunningChanged` apelat din background thread. Fix: `DispatcherQueue.TryEnqueue()`.
- **Startup freeze 8s:** `SHGetFileInfoW` sincron în `Initialize()`. Fix: `LoadIconsAsync()` pe thread separat.
- **Mouse freeze la pornire:** hooks instalate înainte de message loop activ. Fix: reordonare în `Core::Initialize()`.
- **Flickering Start Menu:** `WM_ERASEBKGND` nesuprimat + paint direct pe screen DC. Fix: handler + double buffer GDI.
- **Win key combos nefuncționale:** hook suprima toate evenimentele Win key. Fix: state machine `m_winDown`+`m_winCombo`.
- **Right-click menus instant dismiss:** `TrackPopupMenu` pe fereastră non-foreground. Fix: `AttachThreadInput` + `SetForegroundWindow`.
- **`Application.RequestedTheme` setter crash:** nu există în WinUI 3 la runtime (spre deosebire de UWP). Fix: eliminat; folosit `{ThemeResource}` în XAML.

---

## Arhitectură Ground Truth

- **22H2 / 23H2:** taskbar overlay extern stabil (SWCA + overlay window `WS_POPUP|WS_EX_TOPMOST`).
- **24H2 / 25H2+:** SWCA ignorat pe `Shell_TrayWnd`. Cale experimentală: `GlassBar.XamlBridge.dll` injectat în `explorer.exe`.
- Comunicare Core ↔ XamlBridge: shared memory `Local\GlassBar_XamlBridge_v1` (`SharedBlurState`).
- Comunicare Dashboard ↔ Core: P/Invoke direct in-process (fără IPC extern).
- Config: `%LOCALAPPDATA%\GlassBar\config.json`; log: `%LOCALAPPDATA%\GlassBar\GlassBar.log` + `XamlBridge.log`.

**Documente canonice:**
- Arhitectură / non-negociabile: `Agents.md`
- Test suite: `TESTING.md`
- Product overview: `README.md`

---

## Non-negociabile (rezumat)

1. **Fără patching de sistem** — injection doar excepție experimentală (XamlBridge pe 24H2+).
2. **Taskbar utilizabil** — overlay click-through (`WS_EX_TRANSPARENT | WS_EX_LAYERED`).
3. **CPU < 2% idle** — debounce 50ms pe slidere, poll 500ms.
4. **Fail-safe** — Core nu se prăbușește; log + status în Dashboard; recovery automată după restart Explorer.
5. **Start Menu fidelitate Win7** — vizual și funcțional identic; niciun element UI fără acțiune.
