
# WORKLOG — Win7-Revival / CrystalFrame
Last updated: 2026-03-05 (session 17 — fluidity + pinned list + ClearType + right-click context menu fix + _wtoul portability)

## 0) Ground truth (docs to treat as canonical)
- Product overview + current capabilities: README.md
- Non-negotiables + architecture/roles: Agents.md
- Manual test suites + milestones: TESTING.md
- Start menu fixes plan (existing work): PLAN.md

---

## 1) Current status summary
### ✅ Taskbar: DONE (feature-complete)
Taskbar overlay is considered finished for the current scope:
- External overlay (no injection, no system file modifications) and click-through behavior.
- Opacity control + RGB tint control + real-time updates.
- Auto-hide + all edge positions supported.
- Explorer restart recovery implemented.
- Tray + autostart behavior supported (starts hidden in tray when launched via autostart flag).
Reference: README.md (Done section) + TESTING.md (M1/M2/M4 test cases).

### ✅ Start Menu: DONE (S1+S2+S3+S4+S5 complete — merged to main, 2026-02-21)
Current Start Menu implementation:
- **Phase S1 #3 DONE (2026-02-21):** Win7 two-column layout established. Right column functional via `SHGetKnownFolderPath` (Documents, Pictures, Music, Downloads) and shell target for the virtual Computer folder (`shell:MyComputerFolder`); remaining applets use `ShellExecuteW` (Control Panel, Devices & Printers, Default Programs, Help and Support).
  `Win7RightItem` struct introduced; hover/click handlers wired; separator drawn between
  folder links and system applets. No dead UI remains in the right column.
- **Codex P2 fix (2026-02-21):** `StartMenuWindow` constructor — `GetEnvironmentVariableW`
  buffer-length validation corrected. Previously any nonzero return was treated as success;
  when username ≥ 64 chars the buffer was unterminated and `DrawTextW(...,-1,...)` could
  overread. Now only `0 < ret < len` is accepted as success; buffer is zeroed before the
  `GetUserNameW` fallback, which also validates its result before use.

- **Phase S2 foundation DONE (2026-02-21):** New module `Core/AllProgramsEnumerator.h/.cpp`
  introduced (pure data layer, no UI). Implements:
  - `ResolveShortcutTarget(path, outTarget, outArgs)` — resolves `.lnk` via `IShellLinkW` /
    `IPersistFile`, and `.url` via `GetPrivateProfileStringW`; returns false + CF_LOG on failure.
  - `BuildAllProgramsTree()` — enumerates `FOLDERID_CommonPrograms` + `FOLDERID_Programs`,
    merges recursively (user-profile wins on same-name conflict), sorts folders-first alpha.
  - `MenuNode` struct: `name`, `isFolder`, `target`, `args`, `folderPath`, `children`.
  - Stub in `StartMenuWindow`: `m_programTree` member cached in `Initialize()`; TODO comment
    placed in `WM_LBUTTONDOWN` for Phase S2 "All Programs" click.
  - `ole32.lib` added to `CMakeLists.txt` for `CoCreateInstance`.
  - COM guard note (corrected): `CoUninitialize()` is called when `CoInitializeEx` succeeds
    (`S_OK` **or** `S_FALSE`; both increment the COM reference count per MSDN).
    `RPC_E_CHANGED_MODE` is tolerated and not balanced.

- **Phase S1 left-column Win7 alignment + Phase S2 All Programs UI DONE (2026-02-21):**
  - Left column completely redesigned to Win7 layout:
    - **Programs list** (vertical rows, icon + name) replaces the Win11-style 2×3 pinned grid.
    - **"All Programs ›" / "◄ Back" row** sits just above the search box; single click toggles view.
    - **Search box moved to bottom** of left column (Win7 position), directly above the bottom bar.
    - "Recommended" section removed (Win11 concept, not present in Win7).
    - `PaintProgramsList`, `PaintAllProgramsView`, `PaintApRow`, `PaintWin7SearchBox` replace the old paint methods.
  - All Programs view fully wired (Phase S2 UI):
    - `LeftViewMode` enum (`Programs` / `AllPrograms`) controls left-column rendering.
    - Navigation stack (`m_apNavStack`) enables unlimited folder drill-down; each folder click pushes children pointer.
    - `NavigateIntoFolder` / `NavigateBack` + ESC key navigate the tree.
    - `LaunchApItem`: folder → drill in; shortcut → `ShellExecuteW` + log on failure.
    - Hover highlights wired for programs list, AP row, and AP item list.
    - `Hide()` resets view to `Programs` and clears nav stack on every close.
    - AP_MAX_VISIBLE (~16 items) limits visible window; mouse-wheel scroll + keyboard auto-scroll added in S3; "▲ scroll…" / "▼ more…" hints shown at boundaries.
  - No dead UI: every visible clickable element has a real action.

Remaining for Phase S1 DoD:
- Pixel/layout screenshot validation.

New requirement (non-negotiable, §10):
- **Start Menu must be visually AND functionally identical to Windows 7**
- **All menus and submenus must be 100% functional** (no placeholders, no fake UI)

### Session 9 note (2026-02-28) — Crash diagnostic + stabilitate DLL + root cause găsit

**Context:** Silent crash persistent — aplicația dispărea fără log, fără fereastră, fără nimic în `%LOCALAPPDATA%\CrystalFrame\`.

**Investigare și fix-uri aplicate (PRs #52, #53, #54 — merged to main):**

- **PR #52** — `Dashboard/MainViewModel.cs` + `Dashboard/CoreManager.cs`:
  - `OnCoreRunningChanged` seta proprietăți XAML-bound direct din `MessagePumpThread` (background thread) → `COMException 0x8001010E` (cross-thread UI access) → excepție necatchuită → crash `0xE0434352` (managed CLR unhandled).
  - Fix: `OnCoreRunningChanged` wrapped în `_dispatcherQueue.TryEnqueue()`.
  - `CoreRunningChanged?.Invoke(this, false)` era în afara try/catch din `MessagePumpThread` — mutat în bloc try/catch.
  - Guard `StartEnabled` adăugat la activarea hook-ului Start Menu (`if (!isFirstRun && StartEnabled)`).

- **PR #53** — `Core/CMakeLists.txt`:
  - `VCRUNTIME140.dll` lipsea din publish folder (publish self-contained nu includea CRT nativ).
  - Fix: `MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"` — static CRT embedding.
  - **Important:** fix-ul e activ abia după rebuild C++ cu CMake.

- **PR #54** — `Dashboard/App.xaml.cs` (handler de bază `Application.UnhandledException`):
  - Adăugat handler care prinde excepțiile managed scăpate din toate try/catch-urile.

**Branch curent nemerged (`claude/reset-working-commit-o1Ia3`):**
- `Core/CoreApi.cpp`: `CrystalFrameExceptionFilter` (SEH handler nativ) mutat din `main.cpp` (cod mort, exclus din CMakeLists) în `CoreApi.cpp` și instalat ca primul pas în `CoreInitialize()`. Produce minidump + log entry în `%LOCALAPPDATA%\CrystalFrame\` la orice crash nativ.
- `Dashboard/App.xaml.cs`: handler îmbunătățit — scrie excepția în `CrystalFrame.log` și lasă `e.Handled = false` (WER generează minidump; `e.Handled = true` din main era greșit — înghițea excepțiile silențios).

**Root cause final descoperit de utilizator:**
- **4 versiuni de Windows SDK instalate simultan pe PC.** La build/run, Windows încărca o versiune incompatibilă de runtime față de cea compilată în proiect (`WindowsAppSDKSelfContained=true`, `WindowsPackageType=None`). Rezultatul: crash imediat la pornire, înainte de orice log.
- **Rezolvare:** dezinstalate 3 versiuni SDK. Aplicația funcționează.
- **Lecție:** pe mașina de dev, păstrați o singură versiune de Windows App SDK / Windows SDK instalată, sau folosiți publish self-contained care include runtime-urile corect indiferent de ce e instalat pe sistem.

**Fișiere modificate în această sesiune:**
- `Dashboard/MainViewModel.cs` (PR #52 + #54, merged)
- `Dashboard/CoreManager.cs` (PR #52, merged)
- `Dashboard/App.xaml.cs` (PR #54 merged + branch curent îmbunătățit)
- `Core/CMakeLists.txt` (PR #53, merged)
- `Core/CoreApi.cpp` (branch curent, nemerged — crash handler)

**Next steps:**
1. Merge branch `claude/reset-working-commit-o1Ia3` → main (PR existent pe GitHub).
2. Rebuild `CrystalFrame.Core.dll` cu CMake (pentru static CRT + crash handler activ).
3. Test publish → verifică că `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log` apare la prima pornire.
4. ✅ S6 implementat în această sesiune — iconițe reale din sistem (detalii în Session 9 S6 note de mai jos).

---

### Session 17 — Fluidity overhaul + dynamic pinned list + right-click pin/unpin (2026-03-05)

**Branch:** `claude/update-worklog-MUEFZ`

**Motivație:** eliminarea sacadărilor mouse cauzate de hook-uri blocate, afișare instantanee a meniului, listă pinned editabilă de utilizator.

**Fișiere modificate:** `Core/Core.cpp`, `Core/StartMenuWindow.h`, `Core/StartMenuWindow.cpp`

---

#### Fix text quality — ClearType (PR #69, 2026-03-05)

**Context:** textul din meniu apărea vizual "bold" din cauza a două efecte cumulate:
1. `FW_SEMIBOLD` folosit pe foldere (All Programs), username, submeniu — corect și intenționat (parity Win7)
2. `ANTIALIASED_QUALITY` pe toate fonturile — greyscale antialiasing îngroașă gliful la dimensiuni mici (13-15px)

**Fix:** înlocuit `ANTIALIASED_QUALITY` → `CLEARTYPE_QUALITY` în toate cele **17** apeluri `CreateFontW` din `StartMenuWindow.cpp`.
ClearType folosește sub-pixel rendering (R/G/B pe pixel), rezultând text mai fin și mai crisp la aceeași greutate.

---

#### Fix portabilitate `_wtoul` → `wcstoul` (commit 532a19e, 2026-03-05)

**Context:** `_wtoul` este extensie non-standard MSVC, indisponibilă pe compilatoare MinGW/Clang și pe builds cu `/Za`. Build local eșua cu eroare la linkare.

**Fix:** înlocuit toate apelurile `_wtoul` cu echivalentul standard `wcstoul` (bază 10). Schimbare push-ată direct pe `main` de utilizator.

---

#### Fix right-click context menus — `AttachThreadInput` (PR #71, 2026-03-05)

**Problemă:** click dreapta pe orice element din Start Menu nu producea niciun efect.

**Root cause:** fereastra e creată cu `WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE` → nu devine niciodată foreground window. `SetForegroundWindow(m_hwnd)` eșua tăcut (Windows interzice apelul dacă procesul nu e foreground). `TrackPopupMenu` afișa popup-ul și îl închidea instantaneu.

**Fix aplicat** (`StartMenuWindow.cpp`):
- `ActivateForPopup(hwnd)` helper: `AttachThreadInput(fgTid, myTid, TRUE)` → `SetForegroundWindow` → `AttachThreadInput(FALSE)` — forțează thread-ul nostru în foreground input queue indiferent de stilul ferestrei.
- `PostMessage(m_hwnd, WM_NULL, 0, 0)` după `TrackPopupMenu` — dismissal curat la Esc / click în afară.
- Adăugat opțiunea **"Pin to Taskbar"** în meniul din All Programs: verbul shell `TaskbarPin` aplicat pe `MenuNode::lnkPath`.

**Fișiere modificate:** `Core/StartMenuWindow.cpp`

---

#### Fluidity — 5 îmbunătățiri de performanță

**1. PostMessage în hook callbacks (`Core.cpp:85-94`)**
- `SetShowMenuCallback` și `SetHideMenuCallback` acum apelează `PostMessage(hwnd, WM_APP_SHOW_MENU/WM_APP_HIDE_MENU)` în loc să cheme `Show()`/`Hide()` direct.
- Hook thread returnează în **<1μs** — zero risc de timeout `WH_MOUSE_LL` / `WH_KEYBOARD_LL` și sacadare cursor.
- Constante noi publice: `StartMenuWindow::WM_APP_SHOW_MENU = WM_USER+103`, `WM_APP_HIDE_MENU = WM_USER+104`.
- `HandleMessage` gestionează ambele mesaje: `WM_APP_SHOW_MENU` face toggle (dacă vizibil → Hide, altfel Show); `WM_APP_HIDE_MENU` ascunde direct.

**2. Fereastră pre-creată la `Initialize()` (`StartMenuWindow.cpp:205-211`)**
- `CreateMenuWindow()` mutat din `Show()` în `Initialize()` — fereastra există (hidden) de la pornire.
- `Show()` nu mai face `CreateMenuWindow()`, elimină latența de creare la prima deschidere.

**3. `CacheMenuPosition()` — poziție cacheată (`StartMenuWindow.cpp:2510-2551`)**
- Metodă nouă care face `FindWindowW` + `FindWindowExW` + `GetWindowRect` + calculează `menuX/Y`.
- Apelată la `Initialize()` și la `WM_SETTINGCHANGE` / `WM_DISPLAYCHANGE`.
- `Show()` folosește `m_cachedMenuX/Y` direct — un singur `SetWindowPos`, fără descoperire taskbar la fiecare apăsare Win key.

**4. `ApplyTransparency()` lazy (`StartMenuWindow.cpp:554-559`)**
- Flag nou `m_transparencyApplied` — SWCA e aplicat o singură dată la prima afișare.
- Re-aplicat automat când se schimbă blur/culoare/opacitate (settere: `SetOpacity`, `SetBackgroundColor`, `SetBlur`).
- Elimină apelul `SetWindowCompositionAttribute` inutil la fiecare `Show()`.

**5. Fade-in la `Show()` — `FADE_TIMER_ID = 3` (`StartMenuWindow.cpp:562-566`, `WM_TIMER`)**
- La `Show()`: `SetLayeredWindowAttributes(..., 0, LWA_ALPHA)` → pornește timer 16ms.
- Fiecare tick: `m_fadeAlpha += 51` → `SetLayeredWindowAttributes` cu noul alpha.
- 5 ticks × 16ms ≈ **80ms** rampă 0→255 — fără "pop" brusc la deschidere.
- La `Hide()`: timerul e oprit, alpha resetat la 255.
- Funcționează împreună cu `WS_EX_LAYERED` existent (deja pe fereastră) și SWCA — se compun corect.

---

#### Dynamic pinned list

**`DynamicPinnedItem` struct (`StartMenuWindow.h:85-91`)**
- `std::wstring name, shortName, command` + `COLORREF iconColor` + `HICON hIcon`.
- Înlocuiește `s_pinnedItems` static la runtime; `s_pinnedItems` rămâne ca sursă de default.

**`m_dynamicPinnedItems` (`vector<DynamicPinnedItem>`)**
- Populat la `Initialize()` via `LoadPinnedItems()`.
- `m_pinnedIcons[PROG_COUNT]` array eliminat — iconele sunt acum în `DynamicPinnedItem::hIcon`.
- `LoadIconsAsync()` iterează `m_dynamicPinnedItems` în loc de `s_pinnedItems`.

**`LoadPinnedItems()` / `SavePinnedItems()` (`StartMenuWindow.cpp:2553-2620`)**
- JSON la `%LOCALAPPDATA%\CrystalFrame\pinned_apps.json`.
- Format: `[{"name":"…","short":"…","cmd":"…","color":DWORD}, …]`
- Fallback automat la lista built-in (`s_pinnedItems`) dacă fișierul nu există.

**Referințe actualizate** (toate pun `m_dynamicPinnedItems.size()` în loc de `PROG_COUNT`):
- `PaintProgramsList()`: iterează `m_dynamicPinnedItems`; `recentStartY` calculat dinamic.
- `GetProgItemAtPoint()`: zone pinned/recent calculate pe baza `pinnedCount` dinamic.
- `ExecutePinnedItem()`: apelează `item.command` din vector.
- Recently-used dedup (`LoadRecentPrograms()`): compară cu `m_dynamicPinnedItems`.
- Keyboard nav (`WM_KEYDOWN`): `totalProgItems` calculat dinamic.
- `Shutdown()`: eliberează `hIcon` din fiecare `DynamicPinnedItem`.

---

#### Right-click context menus

**`WM_RBUTTONDOWN` handler (`StartMenuWindow.cpp:2444-2462`)**
- Programs view: click dreapta pe indice `0..pinnedCount-1` → `ShowPinnedContextMenu(index, screenPt)`.
- All Programs view: click dreapta pe item non-folder → `ShowAllProgramsContextMenu(apIndex, screenPt)`.

**`ShowPinnedContextMenu()` → `UnpinItem()` (`StartMenuWindow.cpp:2622-2632`)**
- `TrackPopupMenu` cu opțiunea „Unpin from Start Menu".
- `UnpinItem(index)`: `DestroyIcon`, `erase` din vector, `SavePinnedItems()`, `InvalidateRect`.
- Dezactivat dacă `m_iconsLoaded == false` (icons thread încă rulează).

**`ShowAllProgramsContextMenu()` → `PinItemFromAllPrograms()` (`StartMenuWindow.cpp:2634-2700`)**
- „Pin to Start Menu" apare la click dreapta pe orice aplicație din All Programs.
- Deduplicare case-insensitivă — nu adaugă același item de două ori.
- Refolosește `node.hIcon` cu `CopyIcon()` — fără I/O suplimentar în cazul fericit.
- Dacă iconul lipsea: thread detached cu `SHGetFileInfoW` + `PostMessage(WM_ICONS_LOADED)`.

---

### Session 16 — Blur fix + text quality + UX sprint (2026-03-03)

**Branch:** `claude/update-worklog-MUEFZ`

**Funcționalități implementate în această sesiune:**

#### S15 — Blur switch funcțional + text ANTIALIASED + shadow
- **Defect A fix:** `ApplyTransparency()` acum folosește `ACCENT_ENABLE_ACRYLICBLURBEHIND` când `m_blur=true`; anterior era mereu `TRANSPARENTGRADIENT`.
- **Defect B fix:** `Core::SetStartBlur()` acum apelează și `m_startMenuWindow->SetBlur(enabled)` (anterior trimitea efectul doar pe HWND-ul native Windows Start Menu, nu pe fereastra custom).
- **Text quality:** toate `CreateFontW` au `CLEARTYPE_QUALITY` → `ANTIALIASED_QUALITY` (15 apeluri) — text neted pe orice fundal transparent/blur.
- **Text shadow:** helper `DrawShadowText()` adăugat + aplicat pe toate textele vizibile (pinned apps, recent apps, All Programs, right column items, username, submenu title + items).

#### S-A — Submenu hover delay 400ms → 50ms
- `HOVER_DELAY_MS` scăzut de la 400ms la 50ms. Submeniurile din All Programs se deschid aproape instantaneu la hover — feel mult mai responsiv.

#### S-B — Keep Start Menu Open (preview toggle)
- Toggle nou în Dashboard → Start Menu Settings: **"Keep Start Menu Open"**.
- Când e activ: `StartMenuWindow::Hide()` ignoră cererile de ascundere (click extern, Windows key etc.) — Start Menu rămâne vizibil pentru preview în timp real al sliderelor de efecte.
- `ForceHide()` adăugat pentru ascundere explicită (dezactivare toggle, închidere app).
- API: `CoreSetStartMenuPinned(bool)` → `Core::SetStartMenuPinned()` → `StartMenuWindow::SetPinned()`.

#### S-C — Hover cu tranziție animată (80ms fade-in)
- `m_hoverAnimAlpha` (0–255) + `HOVER_ANIM_TIMER_ID` (10ms ticks, +50/tick = ~5 ticks ≈ 50ms).
- La fiecare schimbare de hover index, alpha se resetează la 0 și crește gradual spre 255.
- `AnimatedHoverColor()` interpolează `bgColor → hoverColor` pe baza alpha-ului curent.
- Toate zonele hover (pinned, recent, AP, right column) folosesc culoarea animată.

#### S-D — Glow inner highlight pe coloana dreaptă
- Deasupra rect-ului de hover pe itemele din coloana dreaptă se desenează o linie de 1px mai luminoasă (`hoverColor + 60` lum) — efect de "glass top" similar Win7 Aero.

#### S-E — Culoare border/accent separabilă
- `m_borderColor` + `m_borderColorOverride` în `StartMenuWindow` — când override e activ, `CalculateBorderColor()` returnează culoarea explicit setată (nu mai calculat din bg).
- API: `CoreSetStartMenuBorderColor(uint rgb)` → `Core::SetStartMenuBorderColor()` → `StartMenuWindow::SetBorderColor()`.
- Dashboard: secțiune nouă **"Border Color"** cu slidere R/G/B + preview swatch.
- Persistat în `config.json` (câmpuri `StartBorderColorR/G/B`).

#### S-F — Preset-uri de temă (3 one-click presets)
- 3 butoane în Dashboard → Start Menu: **Classic Win7**, **Aero Glass**, **Dark**.
  - Classic Win7: BG `(20, 60, 120)`, text alb, border `(80, 130, 190)`, opacitate 85%, blur OFF.
  - Aero Glass: BG `(20, 40, 80)`, text alb, border `(60, 100, 160)`, opacitate 55%, blur ON.
  - Dark: BG `(18, 18, 22)`, text `(200, 200, 200)`, border `(60, 60, 65)`, opacitate 90%, blur OFF.
- Fiecare preset setează simultan: bg color, text color, border color, opacity, blur.

#### S-G — Avatar real din contul Windows
- `LoadAvatarAsync()` rulează pe thread separat, încearcă:
  1. Registry: `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\AccountPicture` → `Image96`.
  2. Fallback: `%ProgramData%\Microsoft\User Account Pictures\{username}.png`.
- Decode PNG via WIC (`IWICImagingFactory`) → HBITMAP DIB 32-bit BGRA.
- `DrawAvatarCircle()` renderează bitmap-ul decupat circular (`CreateEllipticRgn` + `SelectClipRgn` + `StretchBlt`).
- Dacă avatarul nu e găsit, fallback existent (cerc albastru cu inițiala).
- `WM_AVATAR_LOADED = WM_USER + 102` notifică UI thread-ul să repaint.

#### S-H — Power submenu complet
- Deja implementat în sesiunile anterioare (sesiunea 9). Confirmat funcțional:
  - Switch User → `LockWorkStation()` (afișează lock screen + opțiune Switch user)
  - Log Off → `ExitWindowsEx(EWX_LOGOFF)`
  - Lock → `LockWorkStation()`
  - Restart → `ExitWindowsEx(EWX_REBOOT)`
  - Sleep → `SetSuspendState(FALSE, ...)`
  - Hibernate → `SetSuspendState(TRUE, ...)`
  - Shut down → `ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF)`

**Fișiere modificate:**
- `Core/StartMenuWindow.h` — noi câmpuri, metode, constante
- `Core/StartMenuWindow.cpp` — blur, shadow text, hover anim, glow, avatar, border override
- `Core/Core.h/.cpp` — `SetStartMenuPinned()`, `SetStartMenuBorderColor()`, blur forward
- `Core/CoreApi.h/.cpp` — `CoreSetStartMenuPinned()`, `CoreSetStartMenuBorderColor()`
- `Dashboard/CoreNative.cs` — P/Invoke declarations
- `Dashboard/CoreManager.cs` — wrapper methods
- `Dashboard/ConfigManager.cs` — `StartBorderColorR/G/B`
- `Dashboard/MainViewModel.cs` — `StartBorderColorR/G/B`, `StartMenuPinned`, `ApplyPreset()`
- `Dashboard/DetailWindow.xaml` — toggle keep-open, border sliders, preset buttons
- `Dashboard/DetailWindow.xaml.cs` — event handlers

---

### Session 15 — Fix Start Menu flickering (2026-03-03) — issue #63

**Branch:** `claude/update-worklog-MUEFZ`

**Problema (issue #63):** Start Menu-ul prezenta flickering vizibil la fiecare hover și la repaint în general.

**Root cause (două cauze combinate):**

1. **`WM_ERASEBKGND` negestionat** — Clasa de fereastră (`WNDCLASSEXW`) are `hbrBackground = BLACK_BRUSH`. Când `InvalidateRect` marchează fereastra ca invalidă, Windows trimite `WM_ERASEBKGND` înainte de `WM_PAINT`. Fără un handler explicit, `DefWindowProc` șterge fereastra cu `BLACK_BRUSH` → flash negru vizibil între fiecare repaint.

2. **Fără double buffering** — `Paint()` desena direct pe screen DC. Operațiile succesive (`FillRect` fundal → `RoundRect` border → `PaintProgramsList` → `PaintWin7RightColumn` → etc.) erau vizibile pe ecran pe rând → scintilare la fiecare hover change.

   Notă: `InvalidateRect(hwnd, NULL, FALSE)` (bErase=FALSE) suprimă erasure-ul _dacă_ nu există alte invalidări cu bErase=TRUE în update region. La prima afișare sau la `SetWindowPos`, Windows poate marca fereastra cu erasure=TRUE — suficient pentru a declanșa flash-ul negru.

**Fix-uri aplicate (`Core/StartMenuWindow.cpp`):**

1. **Handler `WM_ERASEBKGND`** adăugat în `HandleMessage`:
   ```cpp
   case WM_ERASEBKGND:
       return 1;  // suprimă erasure — Paint() acoperă tot prin buffer
   ```

2. **Double buffering în `Paint()`:**
   - `BeginPaint` returnează `screenDC` (DC-ul ecranului).
   - Se creează `memDC = CreateCompatibleDC(screenDC)` + `memBmp = CreateCompatibleBitmap(screenDC, w, h)`.
   - Toate apelurile de paint (`FillRect`, `RoundRect`, `PaintProgramsList`, `PaintAllProgramsView`, `PaintApRow`, `PaintWin7RightColumn`, `PaintSubMenu`, `PaintBottomBar`) desenează pe `memDC`.
   - La final: `BitBlt(screenDC, 0, 0, w, h, memDC, 0, 0, SRCCOPY)` — frame complet transferat pe ecran într-o singură operație atomică.
   - Cleanup: `SelectObject`, `DeleteObject(memBmp)`, `DeleteDC(memDC)` înainte de `EndPaint`.

**Comportament rezultat:**
- Niciun flash negru la hover sau la orice alt repaint.
- Toate funcțiile de paint rămân nemodificate — schimbarea este transparentă pentru restul codului.

**Fișiere modificate:** `Core/StartMenuWindow.cpp`

---

### Session 14 — Dashboard UI redesign + screenshot (2026-03-02)

**Autor:** totila6 (commits directe pe `master`)

**Commits:**
- `5d042d9` — Schimbare Design Panou De Control si cele doua panouri adiacente
- `e881191` — Adaugare fisier CrystalFrame.png in assets si referire la el in README.md

#### Dashboard MainWindow redesign (`Dashboard/MainWindow.xaml`)

- Eliminat blocul de comentarii introductive (păstrat codul curat).
- Butoanele de navigare `NavTaskbar` / `NavStartMenu`: înălțime minimă crescută 44→50px, margini 3→4px, font 13→14px + `FontWeight="Medium"` (mai vizibil și mai accesibil).
- `Padding` ScrollViewer: 12→16px (mai generos pe laterale).
- `MaxWidth` StackPanel interior: 480→500px (ușoară creștere pentru conținut mai lat).
- `Spacing` StackPanel: 6→10px; `Margin` bottom: 8→12px (mai mult spațiu respirabil).
- Proprietăți `HorizontalContentAlignment`, `VerticalContentAlignment`, `BorderThickness`, `CornerRadius` eliminate de pe butoanele nav (revenire la stilul implicit WinUI 3 — mai consistent cu tema sistemului).

#### Dashboard DetailWindow redesign (`Dashboard/DetailWindow.xaml`)

- Întreg conținutul înfășurat în `ScrollViewer` (vertical, fără scroll orizontal) — panoul de setări devine scrollabil la ferestre mici.
- `RootBorder`: `HorizontalAlignment="Left"` → `"Stretch"`; `Padding` drept 12→16px, bottom 16→24px.
- `Grid` interior: `MaxWidth="500" HorizontalAlignment="Center"` — conținut centrat și lizibil pe ferestre mari.
- Spacing-ul `StackPanel` Taskbar/StartMenu: 12→16px.
- **Panel Taskbar:** coloane slider simplificate (24px → 28px label, 36px → 40px value); eliminat `MinWidth="250"` pe secțiuni individuale (acoperit de `MaxWidth` global).
- **Panel Start Menu:**
  - Eliminat setările de culoare text (R/G/B sliders + preview border) — funcționalitate înlăturată.
  - Eliminat secțiunea "Menu Items" (checkboxuri Control Panel, etc.) — simplificare UI.
  - Adăugat toggle `StartBlurToggle` pentru efectul blur/acrylic (nou).
  - Layout coloane slider Start Menu: 24/*/36 → 28/*/40 (consistent cu Taskbar).
  - `TextWrapping="Wrap"` adăugat pe `StartStatusText` și pe label-uri (prevenire trunchiere).

#### README + assets

- `README.md`: adăugat `![CrystalFrame Screenshot](assets/Crystal%20Frame.png)` imediat după badge-uri — screenshot vizibil pe pagina GitHub.
- `assets/Crystal Frame.png`: fișier nou (939 KB) — screenshot al aplicației CrystalFrame.

**Fișiere modificate:**
- `Dashboard/MainWindow.xaml`
- `Dashboard/DetailWindow.xaml`
- `README.md`
- `assets/Crystal Frame.png` (nou)

---

### Session 13 — Dashboard layout best-mix refactor (2026-03-01)

**Branch:** `claude/refactor-dashboard-layout-6XyNx`

**Obiectiv:** Refactorizare `Dashboard/MainWindow.xaml` — layout adaptiv pentru orice dimensiune de
ecran (compact 360px → TV ultra-wide), pornind de la analiza a 3 variante și integrând elementele
optime din fiecare.

**Decizii de design — sursa fiecărui element:**

| Element | Luat din | Motiv |
|---|---|---|
| `HorizontalAlignment="Stretch"` pe RootGrid | Varianta C | Umple corect fereastra la orice dimensiune |
| Structură 3 rânduri: Header (Auto) / ScrollViewer (`*`) / Footer (Auto) | Varianta C | Header și footer **mereu ancorate** pe ecran |
| `MinHeight="420"` pe Window | versiunea anterioară | Fereastra nu poate fi redusă până dispare conținut |
| `MinWidth="360"` | Varianta C | Mai generos decât 320; mai sigur pe display-uri mici |
| `MaxWidth="480" + HorizontalAlignment="Center"` pe StackPanel din ScrollViewer | versiunea anterioară | Lizibil pe TV/ultra-wide; centrat pe ferestre mari |
| `MinHeight` (nu `Height` fix) pe butoane și carduri | Varianta C | Se extind dacă textul de status dinamic e mai lung |
| `TextWrapping="Wrap"` pe `CoreStatusDetail` | versiunea anterioară | Textul de status nu mai e tăiat (`TextTrimming` eliminat) |
| `UniformGrid` pe butoanele nav Taskbar/Start Menu | versiunea anterioară | Butoanele egale ca lățime indiferent de conținut |
| `MaxWidth="280"` eliminat de pe `ConnectionStatusText` | versiunea anterioară | Constrâns uniform de `MaxWidth=480` al părintelui |

**Comportament rezultat:**
- La fereastră mică (360×420): header + footer ancorate, conținutul scrollabil.
- La fereastră mare / TV 4K: conținut centrat cu MaxWidth=480, header și footer rămân ancorate.
- `WindowStartupLocation="CenterScreen"` asigură poziționare inițială corectă.

**Fișiere modificate:**
- `Dashboard/MainWindow.xaml` (refactorizat complet — 145 linii noi vs 148 anterioare)

---

### Session 12 — Startup freeze fix (2026-03-01) — background icon loading

**Simptom raportat:** ~8 secunde de freeze al mouse-ului la fiecare pornire/repornire a aplicației,
în timp ce Dashboard-ul nu era complet afișat.

**Root cause (două cauze în lanț):**
1. **Hooks instalate pe thread blocat:** În `Core::Initialize()`, `StartMenuHook::Initialize()`
   instala `WH_KEYBOARD_LL` + `WH_MOUSE_LL` ÎNAINTE de `StartMenuWindow::Initialize()`.
   Hook-urile low-level trebuie servite de message loop-ul thread-ului instalator. Dar acel
   thread se bloca imediat ~8 secunde în `StartMenuWindow::Initialize()` → Windows dădea
   timeout hook-urilor (~300ms-1s) → mouse freeze pe toată durata inițializării.
2. **Icon loading sincron (~8s):** `StartMenuWindow::Initialize()` apela `SHGetFileInfoW`
   de sute de ori sincron (`LoadNodeIcons` pe tot tree-ul All Programs + pinned + right col + recent).

**Fix 1 — `Core/Core.cpp`:** Reordonat blocul `StartMenuHook::Initialize()` să apară
DUPĂ `StartMenuWindow::Initialize()`. Hook-urile sunt acum instalate abia la finalul inițializării,
când thread-ul este liber să proceseze mesaje imediat.

**Fix 2 — `Core/StartMenuWindow.h` + `Core/StartMenuWindow.cpp`:** Tot codul de icon loading
(S6.1, S6.2, S6.4, S6.5, S7) mutat din `Initialize()` în noua metodă `LoadIconsAsync()`,
lansată pe `std::thread m_iconThread` la finalul `Initialize()`. `BuildAllProgramsTree()`
rămâne sincron (date necesare pentru navigare). Icoanele se afișează ca pătrate colorate
(fallback existent) până când thread-ul termină, după care `PostMessage(WM_ICONS_LOADED)`
declanșează un repaint. `Shutdown()` face `join()` pe thread înainte de a elibera resursele.

**Thread safety:** `m_iconsLoaded` este `std::atomic<bool>` cu release/acquire ordering.
`m_recentItems` este citit în paint/hit-test/keyboard-nav doar după `m_iconsLoaded==true`.
`m_pinnedIcons[]`, `m_rightIcons[]`, `node.hIcon` sunt pointeri — scrierile sunt atomice
pe x86-64 și protejate de garantia happens-before a store-release/load-acquire.

**Fișiere modificate:**
- `Core/Core.cpp` (reordonare hooks)
- `Core/StartMenuWindow.h` (+ `<atomic>`, `<thread>`, `m_iconThread`, `m_iconsLoaded`, `WM_ICONS_LOADED`, `LoadIconsAsync()`)
- `Core/StartMenuWindow.cpp` (+ `LoadIconsAsync`, `WM_ICONS_LOADED` handler, guards în paint/hit-test/keyboard-nav)

---

### Session 11 — S7 build fix (2026-03-01) — MSVC compatibility

**Context:** Build GitHub Actions "Build C++ Core" a eșuat după merge-ul PR #60 (S7 UserAssist).
Log-urile nu au putut fi accesate direct (Azure blob redirect blocat prin proxy); fix-urile
au fost aplicate proactiv pe baza analizei compatibilității MSVC C++20 + `/permissive-`:

**Fix-uri aplicate în `Core/StartMenuWindow.cpp`:**
1. **ROT13 narrowing** — `c = L'a' + expr` → `c = static_cast<wchar_t>(L'a' + expr)`:
   Suprima C4244 "possible loss of data from int to wchar_t" (wchar_t aritmetică → int, reatribuire la wchar_t).
2. **`max()` macro eliminat** — `max(maxDataLen, (DWORD)72)` → ternary explicit:
   Evită potențiale conflicte cu macrourile Windows.h după includerea `<algorithm>`.
3. **`std::size(expanded)` → `MAX_PATH`** — `expanded` este `wchar_t[MAX_PATH]`, valoare identică,
   fără dependință implicită pe `std::size` pentru array brut (deși disponibil prin `<algorithm>` → `<iterator>`).
4. **CF_LOG cu `const wchar_t*`** — eliminat streaming-ul `m_recentItems[ri].name.c_str()` în
   `std::ostringstream` (stream îngust): rimâne doar indexul, evitând comportament non-portabil.

**Fișiere modificate:** `Core/StartMenuWindow.cpp`

---

### Session 10 — S7 recently used programs (2026-02-28) — UserAssist registry

**Obiectiv:** afișarea programelor recent folosite în coloana stângă, sub itemii pinned — parity cu Windows 7 autentic (§2.2: "Pinned programs + recently used programs").

**Sursă date:** `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\UserAssist`
- GUID aplicații: `{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}\Count`
- GUID shortcut-uri: `{F4E57C4B-2036-45F0-A9AB-443BCFE33D9F}\Count`
- Valorile sunt codificate ROT13; datele conțin DWORD run count la offset 4 și FILETIME last run la offset 16 (format Windows Vista+, 24+ bytes)

**`LoadRecentPrograms()` — algoritm:**
1. Deschide ambele GUID-uri din registry
2. Enumeră valorile, decodifică ROT13, filtrează:
   - Sare `UEME_` (metadata UserAssist)
   - Acceptă doar `.exe` și `.lnk`
   - Sare entries cu count == 0
   - Expandează variabile de mediu (`%windir%` etc.)
3. Sortează descrescător după FILETIME (cel mai recent primul)
4. Ia primele `RECENT_COUNT = 5` care nu sunt deja în lista pinned
5. Pentru fiecare: `SHGetFileInfoW` cu `SHGFI_DISPLAYNAME` (display name) + `SHGFI_ICON | SHGFI_LARGEICON` (icon)

**Modificări UI (`PaintProgramsList`):**
- Pinned items: neschimbat (6 itemi)
- Separator existent rămâne după pinned
- Recent items: painted imediat sub separator, același stil (icon 24px + text)
- Hover/keyboard selection extinsă la range `0..PROG_COUNT+recentCount-1`

**Modificări structurale:**
- `struct RecentItem { std::wstring exePath; std::wstring name; HICON hIcon; FILETIME ftLastRun; DWORD runCount; }` adăugat în `.h`
- `static constexpr int RECENT_COUNT = 5` (max itemi recenți afișați)
- `std::vector<RecentItem> m_recentItems` în `.h`
- `GetProgItemAtPoint()`: extins să returneze `PROG_COUNT + i` pentru zona recent
- `ExecutePinnedItem(int index)`: index `>= PROG_COUNT` → lansează `m_recentItems[index-PROG_COUNT].exePath`
- Keyboard nav: upper bound extins la `PROG_COUNT + recentCount - 1`
- `Shutdown()`: `DestroyIcon` pentru fiecare `m_recentItems[i].hIcon`

**Fișiere modificate:** `Core/StartMenuWindow.h`, `Core/StartMenuWindow.cpp`

---

### Session 9 — S6.2 + Shutdown note (2026-02-28) — UWP icon fallback + Power button fix

**Branch:** `claude/s6-2-uwp-icons-shutdown-o1Ia3`

**Fișiere modificate:** `Core/StartMenuWindow.cpp` (singur fișier — 80 linii adăugate)

#### S6.2 — Fallback icon UWP pentru aplicații pinned

**Problema:** Aplicații UWP pinned (Settings, Calculator, Edge) au `command` de tip URI (`ms-settings:`) sau stub EXE (`calc.exe` redirecționat spre UWP) — `SHGetFileInfoW` pe aceste stringuri eșuează → fallback la pătrat colorat.

**Soluția — `FindLnkPathByName()`:**
- Funcție statică în `StartMenuWindow.cpp`, caută recursiv în `m_programTree` un nod cu `name` identic (case-insensitive) cu `s_pinnedItems[i].name`.
- Returnează `lnkPath`-ul găsit (ex.: `C:\ProgramData\...\Settings.lnk`).
- `SHGetFileInfoW` pe fișierul `.lnk` returnează iconița UWP (shell-ul o rezolvă din AppUserModelId).

**Flux în `Initialize()`:**
1. **S6.1** (pass 1): `SHGetFileInfoW(command, SHGFI_LARGEICON)` — funcționează pentru EXE-uri tradiționale (explorer.exe, notepad.exe, taskmgr.exe).
2. **S6.2** (pass 2 — fallback): pentru intrările cu `m_pinnedIcons[i] == nullptr` → `FindLnkPathByName(m_programTree, name)` → `SHGetFileInfoW(lnkPath)`.

#### Shutdown / Power menu — fix privilege

**Problema:** `ExitWindowsEx` eșua silențios — fără `SE_SHUTDOWN_NAME` privilege activat, apelul returnează `FALSE` pe conturi standard (și uneori chiar pe Administrator fără privilege explicit).

**`EnableShutdownPrivilege()`:**
```cpp
OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, nullptr);
```
Apelat înainte de orice `ExitWindowsEx` (Logoff, Restart, Shutdown).

**Alte fix-uri:**
- `EWX_SHUTDOWN` → `EWX_SHUTDOWN | EWX_POWEROFF` (flag explicit pentru oprire completă a alimentării)
- `SHTDN_REASON_MAJOR_OTHER` → `SHTDN_REASON_MAJOR_APPLICATION` (reason code mai corect)
- Sleep / Hibernate: `SetSuspendState` nu necesită privilege — neschimbat
- Switch User / Lock: `LockWorkStation()` — comportament corect pe Windows 11 (afișează lock screen cu opțiunea "Switch user")
- Adăugat `#pragma comment(lib, "advapi32.lib")` pentru `AdjustTokenPrivileges`

---

### Session 9 — S6 note (2026-02-28) — Iconițe reale din sistem

**Obiectiv:** înlocuire `DrawIconSquare` (pătrate colorate cu text glyph) cu iconițe reale extrase din shell via `SHGetFileInfoW` / `SHGetStockIconInfo`. Fallback la pătrat colorat dacă iconița nu e disponibilă.

**Fișiere modificate:**

- **`Core/AllProgramsEnumerator.h`**
  - `MenuNode` — adăugate câmpurile `lnkPath` (cale fișier `.lnk`/`.url` original) și `hIcon` (HICON, implicit `nullptr`).
  - `hIcon` rămâne `nullptr` pe toată durata `BuildAllProgramsTree()` (inclusiv `MergeTree` / `std::sort`) — iconițele sunt încărcate ulterior, în `Initialize()`.

- **`Core/AllProgramsEnumerator.cpp`**
  - `ScanFolder()` — setează `node.lnkPath = fullPath` pentru fiecare shortcut `.lnk`/`.url`.

- **`Core/StartMenuWindow.h`**
  - `m_pinnedIcons[PROG_COUNT]` — array HICON 32×32 pentru aplicațiile pinned.
  - `m_rightIcons[RIGHT_ITEM_COUNT]` — array HICON 16×16 pentru coloana dreaptă.
  - Declarații `LoadNodeIcons()` și `FreeNodeIcons()` (recursive tree walkers).

- **`Core/StartMenuWindow.cpp`**
  - **`Initialize()`** — după `BuildAllProgramsTree()`:
    - S6.1: `SHGetFileInfoW(command, SHGFI_ICON | SHGFI_LARGEICON)` pentru fiecare pinned app.
    - S6.5: `LoadNodeIcons(m_programTree)` — walk recursiv: foldere → `SHGetStockIconInfo(SIID_FOLDER)`, shortcuts → `SHGetFileInfoW(lnkPath, SHGFI_LARGEICON)`.
    - S6.4: pentru fiecare item din coloana dreaptă — `SHGetKnownFolderPath` (dacă folderId) sau target direct → `SHGetFileInfoW(path, SHGFI_SMALLICON)`.
  - **`Shutdown()`** — `DestroyIcon` pentru `m_pinnedIcons[]`, `m_rightIcons[]`, apoi `FreeNodeIcons(m_programTree)`.
  - **`LoadNodeIcons()`** / **`FreeNodeIcons()`** — recursive tree walkers.
  - **`PaintProgramsList()`** — `DrawIconEx` când `m_pinnedIcons[i] != nullptr`, altfel fallback.
  - **`PaintAllProgramsView()`** — `DrawIconEx` când `node.hIcon != nullptr`, altfel fallback.
  - **`PaintSubMenu()`** — idem, icon 20×20.
  - **`PaintWin7RightColumn()`** — icon 16×16 la stânga textului; text întotdeauna indentat cu 24px (slot consistent chiar și fără icon).

**Detalii tehnice:**
- `SHGetFileInfoW` pe fișierul `.lnk` returnează iconița target-ului (shell o rezolvă), inclusiv pentru aplicații UWP dacă shortcut-ul e în Start Menu Programs.
- Aplicații pinned tip URI (`ms-settings:`) — `SHGetFileInfoW` poate eșua pentru URI-uri → fallback la pătrat colorat (acceptabil pentru sprint 1).
- DPI: iconițe SHGFI_LARGEICON (32px) scalate la 24px cu `DrawIconEx` — calitate acceptabilă la 100% DPI; sprint viitor poate adăuga SHIL_EXTRALARGE pentru 125%+.
- Nicio schimbare de comportament funcțional — toate acțiunile (click, hover, keyboard nav) rămân identice.

**Ordine implementare respectată (conform plan S6 din WORKLOG §5):**
S6.1 (pinned) → S6.5 (All Programs) → S6.4 (right column)

**Următori pași posibili:**
- S6.2: fallback mai bun pentru aplicații UWP pinned (căutare .lnk în Start Menu tree)
- S6.3: suport DPI 125%+ cu SHIL_EXTRALARGE + IImageList::GetIcon
- Testare: verificare iconițe pe instalare curată (fără VS pe PC)

---

### Session 8 note (2026-02-27) — First-run safe mode + diagnostic session

**Context:** Test pe PC s-a terminat cu "silent crash" — aplicația nu apare, cursor busy câteva secunde, proces dispare. Fereastra nu a apărut deloc.

**Analiză cod (fără acces la stack trace / Event Viewer):**
- `BuildAllProgramsTree()` rulează sincron pe UI thread în `StartMenuWindow::Initialize()`, apelat din `CoreInitialize()`. Dacă `config.json` nu există (prima rulare), `LoadAsync()` se completează sincron → `_core.Initialize()` rulează sincron în constructorul `MainWindow`, înainte ca `_window.Activate()` să poată fi procesat de message loop.
- Hook-ul de tastatură/mouse (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) era activat hardcodat (`[TEST]`) la fiecare pornire, indiferent de starea efectelor.
- Crash-ul exact nu a putut fi determinat fără log / Event Viewer — se recomandă verificarea `%LOCALAPPDATA%\CrystalFrame\CrystalFrame.log` și Windows Event Viewer → Application la testul următor.

**Fix implementat — First-run safe mode (`claude/reset-working-commit-o1Ia3`):**
- `Config.IsFirstRun = true` (default) — câmp nou în `config.json`.
- `Config.TaskbarEnabled` și `Config.StartEnabled` default = `false` (erau `true`).
- La pornire: dacă `IsFirstRun == true`, se forțează `TaskbarEnabled=false`, `StartEnabled=false`, se setează `IsFirstRun=false` și se salvează config. Hook-ul Start Menu nu se activează.
- La porniri ulterioare (după primul launch reușit): setările salvate de utilizator se aplică normal.
- **Beneficiu diagnostic:** dacă aplicația a crashat înainte de a salva `IsFirstRun=false`, la repornire va intra din nou în safe mode — reducând la minimum riscul de crash și permițând activarea treptată a funcționalităților.
- Eliminat `[TEST]` hardcoding pentru `SetStartMenuHookEnabled(true)`.

**Fișiere modificate:** `Dashboard/ConfigManager.cs`, `Dashboard/MainViewModel.cs`

---

### Session 7 note (2026-02-26) — Reset main la baseline stabil
- Commit-urile S6.0 (`f280310`, `4847839`: logging reform + MiniDump crash handler) au produs erori și au fost **abandonate**.
- `main` a fost resetat forțat la `d3274ca` (S1–S5 complet, fără S6.0).
- Toate branch-urile extra au fost șterse; pe remote rămâne **doar `main`**.
- Punct de plecare pentru sesiunile viitoare: `d3274ca` (WIDTH=400, HEIGHT=535, DIVIDER_X=248).
- S6.0 (iconițe reale, logging reform) poate fi reimplementat curat într-un sprint viitor dacă este necesar.

---

## 2) MUST requirements (non-negotiable)
### 2.1 Visual parity (Windows 7)
Start Menu must match Windows 7 look & layout:
- Two-column layout (left programs list + right system links).
- Correct header/user area (user picture/name) in the right location.
- No Search Bar needed
- Correct “All Programs” entry and navigation experience.
- Correct bottom bar with Shut down button + arrow (power options).
- Correct typography, spacing, hover highlights, separators, and corner/edge styling.

### 2.2 Functional parity (Windows 7)
Everything clickable in Start Menu must work, including:
- Left column:
  - Pinned programs + recently used programs (launch)
  - All Programs → hierarchical folders → nested submenus → launch
- Right column standard links (each must open correct target):
  - Documents, Pictures, Music, Games (optional if not in scope), Computer, Control Panel
  - Devices and Printers, Default Programs, Help and Support
  - (Optionally) Network / HomeGroup depending on OS availability
- Search:
  - Opens Windows search results and/or resolves apps/settings/docs like Win7 behavior
  - At minimum: clicking search launches system search reliably
- Power:
  - Shut down + submenu (Sleep / Restart / Log off / Switch user, etc.) consistent with Win7

### 2.3 Reliability / behavior rules
- Click-through rule applies to overlays; but **Start Menu itself must be interactive** (it is the UI).
- No breaking Explorer/StartMenuExperienceHost.
- No “silent failure”: log errors + fail-safe behavior.
- Must not trap mouse/keyboard globally in a way that breaks normal use.
- Performance: keep CPU idle low; avoid constant polling.

---

## 3) Current implementation map (what exists today)
### Start Menu trigger & suppression
File: `Core/StartMenuHook.cpp`
- Uses low-level hooks:
  - WH_KEYBOARD_LL: suppress Windows key and show custom Start
  - WH_MOUSE_LL: intercept Start button click and toggle custom Start
- Provides callbacks:
  - Show/hide menu
  - Is menu visible
  - Get menu bounds (for click-outside-to-close)

### Current Start Menu UI window
Files: `Core/StartMenuWindow.h/.cpp`
- Window class: `CrystalFrame_StartMenu`
- Custom Win32 painting (GDI) and hit testing.
- Layout: **Win7 two-column** (400 × 535 px — updated S5.2):
  - `DIVIDER_X = 248` separates left (248 px) from right (152 px).
  - Left column: Programs list (vertical) | "All Programs / Back" row | Bottom bar. *(search box removed S4.1)*
  - Right column: Username header + `Win7RightItem` list (folders + separator + applets).
- Left-column view mode (`LeftViewMode` enum):
  - `Programs` — vertical list of 6 pinned apps (icon + name rows) + "All Programs ›" row.
  - `AllPrograms` — tree view from `m_programTree`; folders drill-in; shortcuts launch; "◄ Back" row.
- Navigation stack (`m_apNavStack`) for unlimited folder drill-down; cleared on `Hide()`.
- Executes items via:
  - `SHGetKnownFolderPath` for personal folders (Documents, Pictures, Music, Downloads).
  - `shell:MyComputerFolder` shell target for Computer (virtual folder — no filesystem path).
  - `ShellExecuteW` for shell applets (control, CLSID shell links, ms-settings:, HelpPane.exe).
  - `ShellExecuteW` for All Programs shortcuts (resolved target + args from MenuNode).
  - Hover/click wired for all 10 right-column entries, programs list, AP list, AP row, Shut down + arrow buttons.
- Username displayed in right-column header (from `GetEnvironmentVariableW("USERNAME")` / `GetUserNameW` fallback).
- ESC key: in AllPrograms view → navigate back one level / to Programs; in Programs view → Hide.

### All Programs data module (Phase S2 — complete)
Files: `Core/AllProgramsEnumerator.h/.cpp`
- Pure data/model layer — no GDI, no UI.
- `MenuNode` struct: display name, isFolder flag, resolved target, args, folderPath, children.
- `ResolveShortcutTarget()`: resolves `.lnk` via `IShellLinkW`/`IPersistFile`; `.url` via INI parse.
- `BuildAllProgramsTree()`: merges FOLDERID_CommonPrograms + FOLDERID_Programs; recursive; sorted.
  Self-manages COM (`CoInitializeEx/COINIT_APARTMENTTHREADED`); tolerates `RPC_E_CHANGED_MODE`;
  `CoUninitialize()` called when `CoInitializeEx` succeeds (S_OK **or** S_FALSE — both increment
  the COM refcount per MSDN); `RPC_E_CHANGED_MODE` (FAILED) is tolerated and not balanced.
- `ole32.lib` linked for `CoCreateInstance`.
- Integrated as `m_programTree` (cached at `StartMenuWindow::Initialize()`, before HWND creation).
- Rendered in `PaintAllProgramsView`; navigation via `NavigateIntoFolder` / `NavigateBack`.

### Existing plan document (applies to current Win11-style menu)
File: `PLAN.md`
- Anchoring logic to Start button for all taskbar edges.
- Search click opens `ms-search:`.
- User area opens `ms-settings:accounts`.
- Autostart -> starts hidden to tray (/autostart).
- Explicit note “Ce NU facem” was tied to keeping current architecture intact.

**IMPORTANT:** With the new Win7-identical requirement, PLAN.md becomes historical for the old Win11-style design. Parts of anchoring and autostart remain useful; UI layout/behavior does not.

---

## 4) Gap analysis (what must change)
### 4.1 Replace Win11-style UI with Win7-style UI
- `StartMenuWindow` layout constants and paint routines will need a full redesign.
- Current pinned grid/recommended concepts must be replaced by Win7 model:
  - Pinned + recent list (left)
  - Right column of shell links
  - “All Programs” navigation tree with nested submenus

### 4.2 Implement real menu/submenu model
Win7-style All Programs requires:
- Program groups/folders tree
- Hover-to-open submenus with correct delays
- Keyboard navigation (arrows, Enter, Esc) to match Win7 feel
- Correct focus, mouse capture, and closing rules

### 4.3 Data sources for real functionality
Need a deterministic way to build program lists and links:
- Common Start Menu folders:
  - `%ProgramData%\Microsoft\Windows\Start Menu\Programs`
  - `%AppData%\Microsoft\Windows\Start Menu\Programs`
- Pinned items:
  - Either explicit configuration (config.json) OR
  - Import from user pinned start menu shortcuts (if feasible)
- Recent items:
  - Windows Recent / automatic destinations (complex); acceptable alternative:
    - start with a minimal “Recent” implementation (optional), but requirement says “identic” so plan for real behavior eventually.

---

## 5) Start Menu implementation plan (new)
### Phase S1 — Win7 visual shell + core interactions (no All Programs yet)
Deliver:
- Win7-like window frame, layout, typography, hover states.
- Left column: fixed pinned list (config-driven) + optional recent placeholder OFF by default (do not fake).
- Right column: implement links that open correct shell targets.
- Search box click opens Windows search reliably.
- Shut down button opens power menu.

DoD:
- Pixel/layout approximation validated by screenshot comparison.
- Every visible clickable item launches something correct (no dead UI).

### Phase S2 — “All Programs” + nested submenus (must be real)
Deliver:
- Enumerate Start Menu Programs folders.
- Build hierarchical tree (folders + shortcuts).
- Render All Programs view and open submenus on hover.
- Launch shortcut targets correctly (lnk resolution).

DoD:
- “All Programs” navigation works with mouse + keyboard.
- Submenus fully functional and stable.

### Phase S3 — Behavior parity hardening
Deliver:
- Close rules, focus behavior, keyboard navigation parity.
- DPI scaling (100–150% at minimum).
- Edge cases: taskbar left/right/top/bottom; multi-monitor later.

DoD:
- No stuck hooks, no cursor freeze, no “native Start leaks”.
- Stress: open/close spam test passes (>= 50 cycles).

#### S3.1 — Keyboard navigation (Up/Down/Enter) — DONE (PR #40, branch `claude/s3-keyboard-nav-T0m6X`)
- `m_keySelProgIndex` / `m_keySelApIndex` / `m_keySelApRow`: keyboard-focus state distinct from hover.
- `CalculateSelectionColor()`: fixed blue accent (RGB 0,96,180) drawn as rect fill — clearly distinct from mouse-hover gray.
- VK_DOWN / VK_UP: cycle through programs list or AP list; last item → AP row; clamp at boundaries.
- VK_RETURN: pinned → `ExecutePinnedItem`; AP item → `LaunchApItem`; AP row → toggle view / NavigateBack.
- Mouse movement clears keyboard selection (mouse and keyboard modes are mutually exclusive).
- `NavigateIntoFolder` / `NavigateBack` / `Hide`: reset keyboard selection state.
- ESC unchanged.
- **P1 fix (review):** nav keys now routed via `StartMenuHook::ForwardKeyCallback` before the hook can suppress them — fixes VK_UP/VK_DOWN not reaching the menu while hook is active.
- **P1 fix (review):** `GetApItemAtPoint` returned a clamped visual index (0..AP_MAX_VISIBLE-1) instead of an absolute node index; rewrote to return `m_apScrollOffset + visual_idx`; keyboard nav now covers the full list [0..total-1] with auto-scroll to keep selection in the visible window.

#### S3.2 — All Programs mouse-wheel scroll — DONE (PR #41, branch `claude/s3-ap-scroll-T0m6X`)
- `m_apScrollOffset`: absolute first-visible-node index; clamped each paint (handles list-shrink after NavigateBack).
- `PaintAllProgramsView`: renders `nodes[offset..offset+count-1]`; hover/key selection compared as absolute nodeIdx; "▲ scroll…" hint at top when offset > 0; "▼ more…" hint at bottom when items remain below.
- `GetApItemAtPoint`: returns `m_apScrollOffset + visual_idx` (absolute); hover and click use consistent absolute index.
- `WM_MOUSEWHEEL`: 3 items per wheel notch; clamp `[0, total − AP_MAX_VISIBLE]`; active only in AllPrograms view over left column; `InvalidateRect` (no flicker).
- Keyboard nav unchanged from S3.1: full absolute range `[0, total-1]` with auto-scroll — wheel and keyboard are independent scroll triggers.
- `NavigateIntoFolder` / `NavigateBack` / `Hide`: reset `m_apScrollOffset = 0`.

#### S3.3 — Hover-to-open submenu lateral (folders in AllPrograms) — DONE (PR #42, branch `claude/s3-hover-submenu-T0m6X`)
- `HOVER_TIMER_ID` / `HOVER_DELAY_MS` (400 ms): `SetTimer` on folder hover; killed on unhover/navigation/hide.
- `m_hoverCandidate`: absolute AP node index pending the 400 ms timer.
- `m_subMenuOpen` / `m_subMenuNodeIdx` / `m_subMenuHoveredIdx`: state for the lateral submenu panel drawn over the right-column area.
- `OpenSubMenu(idx)` / `CloseSubMenu()`: show/hide submenu; reset hover candidate.
- `PaintSubMenu(hdc)`: draws folder title + children list in right-column area (same HWND, no z-order issues, no cursor freeze risk).
- `GetSubMenuItemAtPoint(pt)` / `IsOverSubMenu(pt)`: hit testing for submenu panel.
- `ExecuteSubMenuItem(idx)`: folder → `NavigateIntoFolder` (close submenu first); shortcut → `LaunchApItem`.
- WM_MOUSEMOVE: if hovering same folder as open submenu → keep open; different folder → close + restart timer; non-folder → close + cancel timer.
- WM_TIMER HOVER_TIMER_ID: `OpenSubMenu(m_hoverCandidate)`.
- WM_MOUSELEAVE: kill timer + `CloseSubMenu`.
- WM_KEYDOWN ESC: if submenu open → `CloseSubMenu`; else existing behavior.
- `GetRightItemAtPoint`: returns -1 while submenu is open (right column blocked).
- No global hooks, no separate HWND → no cursor freeze, no input blocking.

#### S4 — Layout & UX polish (2026-02-21, branch `claude/win11-start-menu-redesign-T0m6X`)

**S4.1 — Search box removed from UI (PERMANENT)**
- `PaintWin7SearchBox` nu mai este apelat din `Paint()`. Box-ul NU va fi readăugat.
- `AP_ROW_Y` recalculat: `BOTTOM_BAR_Y - AP_ROW_H - 2 = 630` (câștig de ~36px spațiu liber).
- `AP_MAX_VISIBLE` crește automat: `(630 - 8) / 36 = 17 items` vizibile (față de 16).
- Constantele `SEARCH_H` / `SEARCH_Y` rămân în `.h` ca referință istorică; NU se pictează.
- Rațiune: search box nativ Windows există întotdeauna prin Win+S; duplicarea în meniu adăugă zgomot fără valoare.

**S4.2 — Echilibrare lățimi coloane**
- `DIVIDER_X`: 330 → 298. Rezultat: left=298px, right=278px, diferență=20px≈5mm.
- Coloana stângă și cea dreaptă sunt acum aproape egale, cu stânga ușor mai lată (≈5mm).
- `SM_X = DIVIDER_X + 4 = 302` (submenu panel ajustat automat).

**S4.3 — Fix gap hover submenu (folderele din All Programs)**
- **Bug**: trecând cu mouse-ul de pe un dosar (panoul stâng, max x = DIVIDER_X-MARGIN=286)
  spre submenu-ul lateral (panoul drept, min x = DIVIDER_X=298), mouse-ul traversa un gap
  de ~12px în care nici `overFolder` nici `IsOverSubMenu` nu erau `true` → submenu se închidea.
- **Fix**: în `WM_MOUSEMOVE`, ramura `else` (nici folder, nici submenu): se detectează
  `inTransitGap = (pt.x >= DIVIDER_X - MARGIN) && (pt.x < DIVIDER_X)` și submenu-ul
  NU este închis cât timp mouse-ul traversează această zonă.
- Fișier: `Core/StartMenuWindow.cpp` (WM_MOUSEMOVE hover-timer block).

**S4.3b — Fix submenu close la hover diagonal (2026-02-21, PR #48)**
- **Bug**: la mișcarea diagonală dinspre folder spre submenu, cursorul trecea peste
  rânduri de folder adiacente. Codul apela `CloseSubMenu()` **imediat** la orice folder
  diferit (`else if (nAp != m_hoverCandidate)` → `if (m_subMenuOpen) CloseSubMenu()`).
- **Fix 1**: eliminat `CloseSubMenu()` imediat din ramura "folder diferit". Submenu-ul
  se schimbă doar când timer-ul de 400ms expiră (mouse-ul s-a stabilit pe folderul nou).
  Dacă mouse-ul ajunge la submenu înainte de 400ms, timer-ul e anulat — submenu-ul original
  rămâne deschis.
- **Fix 2**: când mouse-ul intră în panoul submenu (`IsOverSubMenu`), orice timer pending
  pentru un folder diferit este anulat imediat → previne schimbarea submenu-ului după
  ce utilizatorul a ajuns deja la itemii copil.
- Fișier: `Core/StartMenuWindow.cpp` (WM_MOUSEMOVE + IsOverSubMenu branch).

#### S5 — Proporții Win7 + Buton Shutdown (2026-02-21, PR #48)

**S5.1 — Lățime panou drept corectată + buton Win7 Shut down**
- `WIDTH` 580 → 450 → 400 (iterativ); panoul drept: 282px → 152px (~corect față de Win7).
- `HEIGHT` 700 → 460 → 535 (iterativ; +75px ≈ 2 cm la 96 DPI).
- `DIVIDER_X` 298 → 248 (panoul stâng îngustat cu ~50px ≈ 7 caractere Segoe UI).
- Buton Power Win11 (cerc) eliminat. Înlocuit cu:
  - `[Shut down]` (88px) + `[▼]` (18px) aliniate dreapta în bottom bar.
  - Clic pe `Shut down` → `ExitWindowsEx(EWX_SHUTDOWN)` direct.
  - Clic pe `▼` → `ShowPowerMenu()`: Switch User / Log Off / Lock / Restart / Sleep / Hibernate / Shut down.
  - Hover independent pe fiecare buton (`m_hoveredShutdown` / `m_hoveredArrow`).
  - `IsOverShutdownButton(pt)` / `IsOverArrowButton(pt)` înlocuiesc `IsOverPowerButton(pt)`.
  - Glif săgeată: font Marlett, caracter `"6"` (▼ standard Win32).
- Fișiere: `Core/StartMenuWindow.h` (constante + membri noi), `Core/StartMenuWindow.cpp`.

**S5.2 — Iconițe sistem (întrebare utilizator — plan viitor)**
- Iconițele colorate actuale (Win11-style `DrawIconSquare`) pot fi înlocuite cu iconițe reale
  extrase din sistem via `SHGetFileInfoW` (SHGFI_ICON) sau `SHGetStockIconInfo`.
- **Nu există piedici tehnice sau de resurse** — shell-ul caching-ează, `DrawIconEx` e rapid.
- **Complexitate**: aplicații UWP (Settings, Edge, Calculator) nu au `.exe` clasic;
  necesită parsarea shortcut-urilor `.lnk` din Start Menu sau `IPackageManager` COM.
  DPI: necesită `SHGetImageList(SHIL_EXTRALARGE)` la scări > 100%.
- **Plan**: la `Initialize()`, resolve path exe per PinnedItem → `SHGetFileInfoW` → stochează
  `HICON m_pinnedIcons[PROG_COUNT]`; `PaintProgramsList` folosește `DrawIconEx`;
  `Shutdown()` apelează `DestroyIcon` per icon. Sprint separat.

---

### Phase S6 — Iconițe reale din sistem (plan detaliat)

**Obiectiv**: înlocuiește pătratele colorate `DrawIconSquare` cu iconițele reale ale aplicațiilor,
extrase din sistemul de operare (shell/teme), exact cum arată în Windows 7 Start Menu.

#### S6.1 — Iconițe pentru aplicații pinned (PinnedItem)

Fiecare `PinnedItem` are un `target` (cale exe sau shell target). Planul:

1. **`Initialize()`** — după ce `m_programTree` este populat, pentru fiecare `PinnedItem`:
   ```
   SHFILEINFOW sfi = {};
   SHGetFileInfoW(item.target, 0, &sfi, sizeof(sfi),
                  SHGFI_ICON | SHGFI_LARGEICON);   // 32×32
   m_pinnedIcons[i] = sfi.hIcon;  // NULL dacă eșuează — fallback la DrawIconSquare
   ```
2. **`HICON m_pinnedIcons[PROG_COUNT]`** — array nou în `.h`, inițializat `{}` (NULL).
3. **`PaintProgramsList()`** — dacă `m_pinnedIcons[i] != NULL`:
   ```
   DrawIconEx(hdc, iconX, iconY, m_pinnedIcons[i], 32, 32, 0, NULL, DI_NORMAL);
   ```
   Altfel → fallback existent `DrawIconSquare`.
4. **`Shutdown()`** — `DestroyIcon(m_pinnedIcons[i])` pentru fiecare non-NULL.

#### S6.2 — Aplicații UWP (Settings, Edge, Calculator)

Aplicațiile UWP nu au `.exe` accesibil direct. Soluție:
- Shortcut-urile `.lnk` din `%AppData%\Microsoft\Windows\Start Menu\Programs` sau
  `%ProgramData%\...` conțin iconița embeddată sau un AppUserModelId.
- `SHGetFileInfoW` pe fișierul `.lnk` returnează iconița corectă (shell o rezolvă).
- **Plan**: în `PinnedItem`, stochează fie calea `.exe` fie calea `.lnk`;
  `SHGetFileInfoW` funcționează pe ambele.

#### S6.3 — DPI scaling

- La 100% (96 DPI): `SHGFI_LARGEICON` = 32×32 ✓
- La 125%+: necesită `SHGetImageList(SHIL_EXTRALARGE, IID_IImageList, ...)` + `IImageList::GetIcon(idx, ILD_NORMAL, &hIcon)`.
- **Plan simplificat**: folosim `SHGFI_LARGEICON` (32px) și scalăm manual în `DrawIconEx`
  la `PROG_ICON_SZ` (deja există această constantă). La DPI > 100%, iconița poate apărea
  ușor neclară — acceptabil pentru sprint 1; sprint 2 adaugă `SHIL_EXTRALARGE`.

#### S6.4 — Iconițe pentru panoul drept (Win7RightItem)

Similar cu S6.1, dar pentru itemii din coloana dreaptă (Documents, Computer, etc.):
- `SHGetFileInfoW` pe KNOWNFOLDERID path (din `SHGetKnownFolderPath`) pentru foldere.
- `SHGetStockIconInfo(SIID_FOLDER, SHGFI_ICON | SHGFI_SMALLICON, &sii)` pentru iconița standard folder.
- Afișat în `PaintWin7RightColumn` la stânga textului (16×16).

#### S6.5 — Iconițe pentru All Programs list

- Fiecare `MenuNode` din `AllProgramsEnumerator` are `target` (cale `.lnk` sau folder).
- Folderele: `SHGetStockIconInfo(SIID_FOLDER, ...)`.
- Shortcut-urile: `SHGetFileInfoW(node.lnkPath, ...)` pe fișierul `.lnk` original.
- Caching: `MenuNode` stochează `HICON` extras la `BuildAllProgramsTree()`.
- `DestroyIcon` la `NavigateBack` → pop / `Hide()` → full clear.

#### Ordine implementare recomandată:
1. **S6.1** (pinned apps) — impact vizual imediat, simplu
2. **S6.4** (right column) — 16px icons, simplu
3. **S6.5** (All Programs) — mai complex (caching per node)
4. **S6.2** (UWP edge cases) — testare pe fiecare app pinnat
5. **S6.3** (DPI) — ultimul, testat la 125%+

---

## 6) Concrete next steps (pick one per session)
1) **Design spec**: freeze Win7 layout spec (dimensions, regions, list items, right column set).
2) **Refactor StartMenuWindow**:
   - Split rendering into: Frame, LeftColumn, RightColumn, Search, Power
   - Introduce a menu model (items, folders, actions) separate from paint code.
3) ✅ **Shell link correctness** *(DONE 2026-02-21 — branch `claude/right-column-win7`)*:
   - Implemented `Win7RightItem` struct + `s_rightItems[10]` with KNOWNFOLDERID entries and shell applet fallbacks.
   - `PaintWin7RightColumn()`, `GetRightItemAtPoint()`, `ExecuteRightItem()` implemented.
   - `DIVIDER_X = 330` two-column split established in layout constants.
   - Files: `Core/StartMenuWindow.h`, `Core/StartMenuWindow.cpp`.
4) ✅ **All Programs enumerator + UI** *(DONE 2026-02-21 — branch `claude/win11-start-menu-redesign-T0m6X`)*:
   - `Core/AllProgramsEnumerator.h/.cpp`: `MenuNode`, `ResolveShortcutTarget`, `BuildAllProgramsTree`.
   - `PaintAllProgramsView`, `PaintApRow`, navigation stack, `LaunchApItem` all implemented.
   - ESC navigates back in All Programs view; folder click drills in; shortcut click launches.
5) ✅ **Keyboard navigation (arrow keys)** *(DONE — PR #40, S3.1)*:
   - ESC: ✅ navigates back or hides.
   - VK_UP / VK_DOWN: ✅ move selection in programs list and AP list (full range, auto-scroll).
   - VK_RETURN: ✅ launches selected item (pinned → ExecutePinnedItem; AP → LaunchApItem).
6) ✅ **Left column Win7 alignment** *(DONE 2026-02-21)*:
   - Programs list replaced 2×3 pinned grid: vertical rows, icon + name.
   - "All Programs ›" entry added at bottom of list (above search box).
   - Search box moved to Win7 position (bottom of left column, above bottom bar).
   - "Recommended" section removed (Win11 concept only).

---

## 7) Known risks / constraints
- Win7 parity on Windows 11 is inherently “emulation”; some classic targets (e.g., Games, HomeGroup) may not exist. If missing, must define a deterministic fallback (hide item vs open nearest equivalent).
- Recent items parity is hard (Jump Lists / Destinations). If strict parity is mandatory, plan time for it.
- Global hooks must remain safe: no suppression of WM_MOUSEMOVE in a way that freezes cursor (already addressed in StartMenuHook.cpp).

---

## 8) Where to look in code (entry points)
- Start trigger & suppression: `Core/StartMenuHook.cpp`
- Start UI window: `Core/StartMenuWindow.h/.cpp`
- All Programs data model: `Core/AllProgramsEnumerator.h/.cpp`
- Old plan (Win11-style menu fixes): `PLAN.md`
- QA checklist: `TESTING.md`

---

## 9) Session prompt template (for Claude / any assistant)
Context:
- Repo: https://github.com/totilaAtila/Win7-Revival
- Taskbar is DONE. Work ONLY on Start Menu (Win7 identical).

Request:
- Goal (one sentence):
- Files to touch:
- Expected Win7 behavior (bullet list):
- Current behavior / bug:
- Acceptance criteria (measurable):
- Tests to run (from TESTING.md + new ones):
## 10) Non-negotiables (repeat here so they are never “forgotten”)
- Start Menu: visually + functional identical to Windows 7. (everything but Search Bar. Not needed)
- All menus/submenus: 100% functional (no dead UI).
- No injection / no patching system components.
- Clear logs + fail-safe behavior, no silent failures.
- This WORKLOG.md file has to be updated BEFORE any commit and push
