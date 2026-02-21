
# WORKLOG — Win7-Revival / CrystalFrame
Last updated: 2026-02-21 (session 4)

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

### 🚧 Start Menu: IN PROGRESS (S1+S2 implemented; S3 remaining)
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
    - AP_MAX_VISIBLE (~16 items) limits display without scroll; "▼ more…" hint shown if truncated.
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
- Correct search box position and styling.
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
- Layout: **Win7 two-column** (580 × 700 px):
  - `DIVIDER_X = 330` separates left (programs) from right (shell links).
  - Left column: Programs list (vertical) | "All Programs / Back" row | Search box | Bottom bar.
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
  - Hover/click wired for all 10 right-column entries, programs list, AP list, AP row, power button.
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
- No stuck hooks, no cursor freeze, no "native Start leaks".
- Stress: open/close spam test passes (>= 50 cycles).

#### S3.1 — Keyboard navigation (Up/Down/Enter) — IN PROGRESS (branch `claude/s3-keyboard-nav-T0m6X`)
- `m_keySelProgIndex` / `m_keySelApIndex` / `m_keySelApRow`: keyboard-focus state distinct from hover.
- `CalculateSelectionColor()`: fixed blue accent (RGB 0,96,180) drawn as rect fill — clearly distinct from mouse-hover gray.
- VK_DOWN / VK_UP: cycle through programs list or AP list; last item → AP row; clamp at boundaries.
- VK_RETURN: pinned → `ExecutePinnedItem`; AP item → `LaunchApItem`; AP row → toggle view / NavigateBack.
- Mouse movement clears keyboard selection (mouse and keyboard modes are mutually exclusive).
- `NavigateIntoFolder` / `NavigateBack` / `Hide`: reset keyboard selection state.
- ESC unchanged.

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
5) 🔧 **Keyboard navigation (arrow keys)** *(partial — ESC done; arrows/Enter/Tab pending)*:
   - ESC: ✅ done (navigates back or hides).
   - Arrow up/down move selection in programs list or AP list: **TODO Phase S3**.
   - Enter launches selected item: **TODO Phase S3**.
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
- Start Menu: visually + functional identical to Windows 7.
- All menus/submenus: 100% functional (no dead UI).
- No injection / no patching system components.
- Clear logs + fail-safe behavior, no silent failures.
- This WORKLOG.md file has to be updated BEFORE any commit and push
