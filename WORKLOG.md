
# WORKLOG — Win7-Revival / CrystalFrame
Last updated: 2026-02-20

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

### 🚧 Start Menu: IN PROGRESS (major redesign required)
Current Start Menu implementation is *not* Windows 7 identical:
- `Core/StartMenuWindow.*` explicitly describes a Windows 11-style layout (search + pinned grid + recommended + bottom bar).
- `PLAN.md` documents fixes for anchoring and click handlers in the current design (search + user area + autostart-to-tray).
New requirement changes the target entirely:
- **Start Menu must be visually AND functionally identical to Windows 7**
- **All menus and submenus must be 100% functional** (no placeholders, no fake UI)

This implies the Start Menu UI/interaction model must be redesigned.

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
- Current layout is Win11-style:
  - Search box + pinned grid + recommended list + bottom bar
- Executes items via ShellExecuteW / ms-settings URIs / shell: folders.

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

---

## 6) Concrete next steps (pick one per session)
1) **Design spec**: freeze Win7 layout spec (dimensions, regions, list items, right column set).
2) **Refactor StartMenuWindow**:
   - Split rendering into: Frame, LeftColumn, RightColumn, Search, Power
   - Introduce a menu model (items, folders, actions) separate from paint code.
3) **Shell link correctness**:
   - Implement right-column links via known folder IDs / shell verbs (not hard-coded strings where possible).
4) **All Programs enumerator**:
   - Implement folder scan + .lnk resolution strategy; build a tree structure.
5) **Keyboard navigation skeleton**:
   - ESC closes
   - Arrow keys move selection
   - Enter launches
   - Right arrow opens submenu

---

## 7) Known risks / constraints
- Win7 parity on Windows 11 is inherently “emulation”; some classic targets (e.g., Games, HomeGroup) may not exist. If missing, must define a deterministic fallback (hide item vs open nearest equivalent).
- Recent items parity is hard (Jump Lists / Destinations). If strict parity is mandatory, plan time for it.
- Global hooks must remain safe: no suppression of WM_MOUSEMOVE in a way that freezes cursor (already addressed in StartMenuHook.cpp).

---

## 8) Where to look in code (entry points)
- Start trigger & suppression: `Core/StartMenuHook.cpp`
- Start UI window: `Core/StartMenuWindow.h/.cpp`
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
