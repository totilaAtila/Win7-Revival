
# WORKLOG — Win7-Revival / CrystalFrame
Last updated: 2026-02-21 (session 6)

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

### ✅ Start Menu: DONE (S1+S2+S3+S4+S5+S6.0 complete — PR #49 open, 2026-02-21)
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

#### S6.0 — Logging reform + crash dump handler (2026-02-21, branch `claude/win11-start-menu-redesign-T0m6X`)

**Context**: utilizatorul a raportat un "crash silențios" la pornire. Investigația logurilor a arătat că
aplicația face shutdown ordonat (nu crash) după <1 secundă; cauza probabilă: excepție C# în Dashboard
`InitializeAsync()`. Logul mai arăta și spam masiv: ~27 scrieri/secundă în fișier.

**Investigație:**
- T5352 (message pump): 3 linii `DEBUG` la fiecare ~110ms → `Renderer::RefreshTransparency()` la fiecare poll.
- T2316 (monitoring thread): `INFO FOUND MATCH` pentru fiecare fereastră scanată de `EnumWindows`, constant.
- Ultimele linii din log: `Core API shutdown initiated` → `Core shutdown initiated` → `StartMenuHook::Shutdown`
  pe thread T11960 (Dashboard UI thread apelând `CoreManager.Shutdown()`). Acesta este **shutdown ordonat**,
  NU crash C++. "Crash-ul" la pornire este probabil o excepție C# în Dashboard, nu în DLL-ul C++.

**Modificări implementate:**

1. **`Core/Diagnostics.h` — filtrare nivel minim de logare:**
   - Adăugat `std::atomic<int> m_minLevel{ static_cast<int>(LogLevel::Warning) }` — default: Warning.
   - Adăugat `SetMinLevel(LogLevel)`, `GetMinLevel()`, `IsEnabled(LogLevel)` în clasa `Logger`.
   - Macro `CF_LOG` actualizat: verificare lock-free a nivelului ÎNAINTE de `ostringstream` → zero overhead
     când mesajul este sub prag. Sintaxă: `CF_LOG(Info, "x=" << x)` rămâne identică.

2. **`Core/ShellTargetLocator.cpp` — oprire spam FOUND MATCH:**
   - `CF_LOG(Info, "FOUND MATCH: class=...")` → `CF_LOG(Debug, "FOUND MATCH: class=...")`
   - Cu filtrul de Warning (default), această linie nu mai apare în log în condiții normale.

3. **`Core/StartMenuWindow.cpp` — power action logs la Warning:**
   - Logurile pentru Shut down / Restart / Log off (acțiuni critice) urcate de la `Info` → `Warning`
     → apar în log chiar și cu filtrul default, pentru audit.

4. **`Core/CoreApi.cpp` — crash dump handler (MiniDumpWriteDump):**
   - Adăugat `#include <dbghelp.h>` + `#pragma comment(lib, "dbghelp.lib")`.
   - Global `wchar_t g_crashDumpDir[MAX_PATH]` stocat la `CoreInitialize()`.
   - Handler `CrashDumpHandler(EXCEPTION_POINTERS* ep)`:
     - Scrie `%LOCALAPPDATA%\CrystalFrame\crash_YYYYMMDD_HHMMSS.dmp` cu `MiniDumpWithFullMemory`.
     - Setează `SetMinLevel(Debug)` înainte de a loga — asigură că logul de crash este complet.
     - Loghează `ExceptionCode` + `ExceptionAddress` + calea fișierului `.dmp` la nivel `Error`.
     - Returnează `EXCEPTION_CONTINUE_SEARCH` → Windows Error Reporting rulează în continuare.
   - `SetUnhandledExceptionFilter(CrashDumpHandler)` instalat în `CoreInitialize()`.
   - Logurile de lifecycle (`=== CoreInitialize ===`, `=== Core ready ===`, shutdown) urcate la `Warning`.

5. **`Core/CMakeLists.txt`:** adăugat `dbghelp.lib` în `target_link_libraries`.

**Rezultat:** log normal (Warning+) va conține doar evenimente semnificative (init, shutdown, erori, power
actions). La crash real: fișier `.dmp` + log complet (Debug+) generate automat în `%LOCALAPPDATA%\CrystalFrame`.

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
