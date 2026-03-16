#pragma once
#include <Windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <vector>
#include "AllProgramsEnumerator.h"   // MenuNode, BuildAllProgramsTree, IconCache

namespace CrystalFrame {

/// <summary>
/// Recently used program, populated from the Windows UserAssist registry at startup.
/// Shown below pinned items in the left column (Win7 §2.2 parity).
/// </summary>
struct RecentItem {
    std::wstring exePath;    // Full path to .exe or .lnk (for ShellExecute)
    std::wstring name;       // Display name (from SHGetFileInfoW SHGFI_DISPLAYNAME)
    HICON        hIcon;      // 32×32 icon (nullptr → DrawIconSquare fallback)
    FILETIME     ftLastRun;  // Last execution time (for sorting; most recent first)
    DWORD        runCount;   // Usage count from UserAssist
};

/// <summary>
/// Pinned app shown in the left-column vertical list (Win7 style).
/// </summary>
struct PinnedItem {
    const wchar_t* name;       // Full app name (shown to the right of the icon)
    const wchar_t* shortName;  // Short label drawn inside icon square
    const wchar_t* command;    // Shell command / URI to execute
    COLORREF iconColor;        // Icon square background color
};

/// <summary>
/// Win7-style right-column link item.
/// Uses SHGetKnownFolderPath when folderId is non-null (preferred);
/// falls back to ShellExecuteW with target/params for shell applets.
/// isSeparator == true → draw a horizontal separator, no click.
/// </summary>
struct Win7RightItem {
    const wchar_t*  label;          // display text; nullptr for separators
    bool            isSeparator;    // if true, all other fields are unused
    const GUID*     folderId;       // non-null → resolve path via SHGetKnownFolderPath
    const wchar_t*  verb;           // ShellExecute verb (L"explore" / L"open")
    const wchar_t*  target;         // ShellExecute target (when folderId == nullptr)
    const wchar_t*  params;         // ShellExecute lpParameters (may be nullptr)
};

/// Left-column view mode:
///   Programs     — vertical list of pinned apps + "All Programs" entry
///   AllPrograms  — "All Programs" tree from AllProgramsEnumerator
enum class LeftViewMode { Programs, AllPrograms };

/// Right-column recommended/shortcut menu item (visibility + display name).
struct RecommendedMenuItem {
    const wchar_t* name;
    bool           visible = true;
};

/// <summary>
/// Custom Start Menu window — Windows 7-style two-column layout.
///
/// Left column  : [Pinned programs — vertical list]
///                [All Programs / Back row]
///                [Search box]
/// Right column : Win7 shell links (Documents, Pictures, Music, …, Control Panel, …)
/// Bottom bar   : User avatar + name | Power button
/// </summary>
// Dynamic pinned item — persisted to JSON, replaces the static s_pinnedItems at runtime.
struct DynamicPinnedItem {
    std::wstring name;
    std::wstring shortName;
    std::wstring command;
    COLORREF     iconColor = RGB(64, 64, 68);
    HICON        hIcon     = nullptr;  // loaded async via IconCache; cache-owned
    // Custom icon set via "Select custom icon" context menu; owned (not in cache).
    std::wstring customIconPath;       // DLL/EXE path; empty = no custom icon
    int          customIconIndex = -1; // icon index inside the file
    HICON        hCustomIcon = nullptr;// extracted icon; DestroyIcon() when removed
};

class StartMenuWindow {
public:
    StartMenuWindow();
    ~StartMenuWindow();

    // Posted by Core's hook callbacks to Show/Hide on the UI thread (no work on hook thread).
    static constexpr UINT WM_APP_SHOW_MENU = WM_USER + 103;
    static constexpr UINT WM_APP_HIDE_MENU = WM_USER + 104;

    /// Initialize window classes (call once at startup)
    bool Initialize();

    /// Show the menu; x/y are hint coords (actual position is calculated from taskbar)
    void Show(int x, int y);

    /// Hide the menu
    void Hide();

    /// Set background transparency (0 = opaque, 100 = fully transparent)
    void SetOpacity(int opacity);

    /// Set background color (COLORREF)
    void SetBackgroundColor(COLORREF color);

    /// Set text color (COLORREF)
    void SetTextColor(COLORREF color);

    /// Set which recommended items are visible (API kept for Dashboard compat)
    void SetMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                      bool documents, bool pictures, bool videos, bool recentFiles);

    bool IsVisible() const { return m_visible; }

    /// S15 — Enable/disable blur/acrylic background on the Start Menu window
    void SetBlur(bool useBlur);

    /// S-B — Pin Start Menu open for Dashboard preview (Hide() becomes no-op when pinned)
    void SetPinned(bool pinned);

    /// S-B — Force-hide even when pinned (called when pinned toggle is turned off)
    void ForceHide();

    /// S-E — Set explicit border/accent color (overrides auto-calculated value)
    void SetBorderColor(COLORREF color);

    /// Get current window bounds in screen coordinates (empty RECT if hidden)
    RECT GetWindowBounds() const;

    /// Get underlying HWND (used by StartMenuHook to PostMessage nav keys)
    HWND GetMenuHwnd() const { return m_hwnd; }

    /// Cleanup all windows and hooks
    void Shutdown();

private:
    // ── State ───────────────────────────────────────────────────────────────
    HWND m_hwnd    = nullptr;
    bool m_visible = false;

    int      m_opacity   = 30;
    COLORREF m_bgColor   = RGB(32, 32, 36);
    COLORREF m_textColor = RGB(255, 255, 255);

    // ── Left-column view mode ───────────────────────────────────────────────
    LeftViewMode m_viewMode = LeftViewMode::Programs;

    // Navigation stack for All Programs drill-down.
    // Each entry points to the children vector of the folder we entered.
    // Empty stack = root of m_programTree.
    std::vector<const std::vector<MenuNode>*> m_apNavStack;

    // Hover state
    int  m_hoveredProgIndex    = -1;   // Programs list (pinned items)
    bool m_hoveredApRow        = false; // "All Programs" / "Back" row
    int  m_hoveredApIndex      = -1;   // All Programs tree item
    int  m_hoveredRightIndex   = -1;   // Win7 right column hover
    bool m_hoveredShutdown     = false;   // over "Shut down" text button
    bool m_hoveredArrow        = false;   // over the arrow dropdown button
    bool m_trackingMouse       = false;

    // Keyboard selection state (distinct from hover; cleared by mouse movement)
    int  m_keySelProgIndex     = -1;   // keyboard-focused item in Programs list
    bool m_keySelApRow         = false; // keyboard focus on "All Programs"/"Back" row
    int  m_keySelApIndex       = -1;   // keyboard-focused item in AllPrograms list (absolute)

    // Scroll offset for AllPrograms list (first visible node index); keyboard-driven.
    int  m_apScrollOffset      = 0;

    // Hover-to-open submenu state (S3.3)
    UINT_PTR m_hoverTimer        = 0;   // SetTimer handle; 0 = no pending timer
    int      m_hoverCandidate    = -1;  // absolute AP node idx waiting for delay
    bool     m_subMenuOpen       = false;
    int      m_subMenuNodeIdx    = -1;  // absolute AP node that opened the submenu
    int      m_subMenuHoveredIdx = -1;  // visual index in submenu list (-1 = none)

    static constexpr UINT_PTR HOVER_TIMER_ID      = 1;
    static constexpr UINT     HOVER_DELAY_MS      = 50;   // was 400 — snappy submenu opening
    static constexpr UINT_PTR HOVER_ANIM_TIMER_ID = 2;    // S-C: hover fade-in animation
    static constexpr UINT_PTR FADE_TIMER_ID       = 3;    // Show() window fade-in

    // Cached Windows login name for the right-column header
    wchar_t m_username[64] = {};

    // Phase S2: All Programs tree pre-cached at Initialize().
    std::vector<MenuNode> m_programTree;

    // S7 — recently used programs, loaded from UserAssist at Initialize().
    // Shown below pinned items; max RECENT_COUNT entries, sorted by last-run time.
    std::vector<RecentItem> m_recentItems;

    // Paths explicitly removed by the user via right-click "Remove from list".
    // Persisted to %LOCALAPPDATA%\CrystalFrame\recent_excluded.json.
    std::set<std::wstring> m_recentExcluded;

    // Dynamic pinned list (replaces static s_pinnedItems at runtime).
    // Loaded from JSON at Initialize(); saved on every pin/unpin.
    std::vector<DynamicPinnedItem> m_dynamicPinnedItems;

    // Recommended/shortcut items for the right column (visibility + name).
    // Indices: 0=ControlPanel 1=DeviceManager 2=InstalledApps
    //          3=Documents 4=Pictures 5=Videos 6=RecentFiles
    RecommendedMenuItem m_menuItems[7] = {
        { L"Control Panel",   true },
        { L"Device Manager",  true },
        { L"Installed Apps",  true },
        { L"Documents",       true },
        { L"Pictures",        true },
        { L"Videos",          true },
        { L"Recent Files",    true },
    };

    // Per-item custom display names (empty = use default from m_menuItems[i].name).
    std::wstring m_customMenuNames[7];

    // Optional custom title override (empty = "CrystalFrame").
    std::wstring m_customTitle;

    // Cached menu position — computed once at Initialize() and refreshed on
    // WM_SETTINGCHANGE / WM_DISPLAYCHANGE so Show() never calls FindWindowW.
    int  m_cachedMenuX      = 0;
    int  m_cachedMenuY      = 0;

    // Window fade-in state — ramps SetLayeredWindowAttributes 0→255 over ~80ms.
    BYTE     m_fadeAlpha    = 255;
    UINT_PTR m_fadeTimer    = 0;

    // Transparency applied flag — ApplyTransparency() is skipped on Show() if
    // already applied; only re-applied when blur/color/opacity config changes.
    bool m_transparencyApplied = false;

    // ── Thread safety ──────────────────────────────────────────────────────────
    // Protects m_programTree between the background icon/watcher threads and the
    // UI (paint) thread.  Lock held only for the duration of individual reads or
    // the full tree-rebuild cycle so contention is negligible.
    mutable std::mutex   m_treeMutex;

    // Background icon loading (S6/S7): icons are loaded on a worker thread so
    // that Initialize() returns quickly and the hook thread is not blocked.
    // m_iconsLoaded becomes true (release) after all icon writes are complete;
    // paint code reads it (acquire) before using any icon handle.
    std::thread          m_iconThread;
    std::atomic<bool>    m_iconsLoaded{false};

    // ── Shared icon cache (Task 7) ─────────────────────────────────────────────
    // Owned exclusively by LoadIconsAsync; ReleaseAll() is called on the UI
    // thread inside RefreshProgramTree() after m_iconThread has been joined.
    IconCache            m_iconCache;

    // ── File-system watcher (Task 5) ──────────────────────────────────────────
    // Watches %ProgramData% and %AppData% Start Menu folders.  Posts
    // WM_APP_REFRESH_TREE to m_hwnd when a change is detected so the UI thread
    // can rebuild the tree without touching the watcher thread's resources.
    std::thread          m_watcherThread;
    std::atomic<bool>    m_watcherRunning{false};
    // Manual-reset event signalled by StopFolderWatcher() so the watcher can
    // wake immediately from an INFINITE WaitForMultipleObjects call.
    HANDLE               m_watcherStopEvent = nullptr;

    // Posted to m_hwnd by the icon thread when loading is done → triggers repaint.
    static constexpr UINT WM_ICONS_LOADED    = WM_USER + 101;
    static constexpr UINT WM_AVATAR_LOADED   = WM_USER + 102; // S-G: avatar thread → UI
    // Posted by the file-system watcher when a Start Menu folder change is detected.
    static constexpr UINT WM_APP_REFRESH_TREE = WM_USER + 105;

    // S15 — blur switch
    bool m_blur = false;

    // S-B — keep Start Menu visible for Dashboard preview
    bool m_pinned = false;

    // S-C — hover fade-in animation
    int      m_hoverAnimAlpha = 255;  // 0 = transparent, 255 = full hover color
    UINT_PTR m_hoverAnimTimer = 0;    // 0 = no animation running

    // S-E — border/accent color override
    COLORREF m_borderColor         = RGB(60, 60, 65);
    bool     m_borderColorOverride = false;

    // S-G — real user avatar loaded from Windows account picture
    HBITMAP     m_avatarBitmap = nullptr;   // 96×96 DIB; nullptr = use initials fallback
    std::thread m_avatarThread;             // background loader thread

    // ── Layout constants (Windows 7 style) ──────────────────────────────────
    static constexpr int WIDTH  = 400;   // 450 - 50 (left panel narrowed ~7 chars)
    static constexpr int HEIGHT = 535;   // 460 + 75 (~2 cm taller at 96 DPI)
    static constexpr int MARGIN = 12;

    // ── Win7 two-column divider ─────────────────────────────────────────────
    // Left column  : x ∈ [0, DIVIDER_X)
    // Divider line : x == DIVIDER_X
    // Right column : x ∈ (DIVIDER_X, WIDTH)
    // Left panel 248px, right panel 152px.
    static constexpr int DIVIDER_X  = 248;   // was 298; narrowed ~50px (~7 chars)
    static constexpr int RC_X       = DIVIDER_X + 4;   // right col content left edge

    // Right column row metrics
    static constexpr int RC_HDR_H   = 64;    // username header height
    static constexpr int RC_ITEM_H  = 36;    // clickable item row height
    static constexpr int RC_SEP_H   = 14;    // separator row height

    // Total entries in s_rightItems (includes separators)
    static constexpr int RIGHT_ITEM_COUNT = 10;

    // ── Bottom bar (Win7 Shut-down button + arrow dropdown) ─────────────────
    static constexpr int BOTTOM_BAR_H    = 40;
    static constexpr int BOTTOM_BAR_Y    = HEIGHT - BOTTOM_BAR_H;   // 420
    // "Shut down" rectangular button (Win7 style)
    static constexpr int SHUT_BTN_W      = 88;   // width of "Shut down" label area
    static constexpr int SHUT_BTN_H      = 26;   // height of the button
    static constexpr int SHUT_ARROW_W    = 18;   // width of the arrow dropdown button

    // ── Search box — REMOVED (search box hidden per design decision; Windows
    //    search is still accessible via Win+S; kept as constant for hit-test
    //    legacy code that may reference it). Do NOT re-add to Paint().
    static constexpr int SEARCH_H        = 34;
    static constexpr int SEARCH_Y        = BOTTOM_BAR_Y - SEARCH_H - 2;  // 624

    // ── "All Programs" / "Back" row — just above bottom bar (search removed) ─
    static constexpr int AP_ROW_H        = 28;
    static constexpr int AP_ROW_Y        = BOTTOM_BAR_Y - AP_ROW_H - 2;  // 630

    // ── Programs list — vertical pinned-app rows, fills top of left column ──
    static constexpr int PROG_Y          = 8;
    static constexpr int PROG_ITEM_H     = 36;
    static constexpr int PROG_ICON_SZ    = 24;
    static constexpr int PROG_COUNT      = 6;   // must match s_pinnedItems length
    static constexpr int RECENT_COUNT    = 5;   // max recently-used items shown below pinned

    // Max items visible in All Programs list (without scroll)
    static constexpr int AP_MAX_VISIBLE  = (AP_ROW_Y - PROG_Y) / PROG_ITEM_H; // ~16

    // ── Hover submenu panel layout (S3.3) ───────────────────────────────────
    static constexpr int SM_X       = DIVIDER_X + 4;
    static constexpr int SM_TITLE_H = 32;
    static constexpr int SM_ITEM_H  = 36;
    static constexpr int SM_MAX_VIS = (BOTTOM_BAR_Y - SM_TITLE_H) / SM_ITEM_H;

    // ── Window class names ──────────────────────────────────────────────────
    static constexpr wchar_t WINDOW_CLASS[]      = L"CrystalFrame_StartMenu";
    static constexpr wchar_t EDIT_DIALOG_CLASS[] = L"CrystalFrame_EditDialog";

    // ── Static data ─────────────────────────────────────────────────────────
    static const PinnedItem    s_pinnedItems[PROG_COUNT];
    static const Win7RightItem s_rightItems[RIGHT_ITEM_COUNT];

    // S6 — real system icons for the right column (16×16).
    // Pinned app icons are now stored inside DynamicPinnedItem::hIcon.
    HICON m_rightIcons[RIGHT_ITEM_COUNT] = {};  // 16×16 for right-column items

    // ── Win32 plumbing ──────────────────────────────────────────────────────
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMenuWindow();
    void ApplyTransparency();

    // ── Painting ────────────────────────────────────────────────────────────
    void Paint();

    // Left column — Programs view
    void PaintProgramsList(HDC hdc, const RECT& cr);

    // Left column — All Programs view (tree from AllProgramsEnumerator)
    void PaintAllProgramsView(HDC hdc, const RECT& cr);

    // Left column — "All Programs" / "Back" row (shared by both views)
    void PaintApRow(HDC hdc, const RECT& cr);

    // Left column — search box (Win7: at the bottom of the left column)
    void PaintWin7SearchBox(HDC hdc, const RECT& cr);

    // Right column
    void PaintWin7RightColumn(HDC hdc, const RECT& cr);

    // Bottom bar
    void PaintBottomBar(HDC hdc, const RECT& cr);

    // Draw a colored rounded icon square with a short label inside (S6 fallback)
    void DrawIconSquare(HDC hdc, int cx, int cy, int sz,
                        COLORREF bgColor, const wchar_t* label,
                        COLORREF textColor = RGB(255, 255, 255));

    // S6 — icon lifecycle helpers (walk m_programTree recursively)
    void LoadNodeIcons(std::vector<MenuNode>& nodes);
    void FreeNodeIcons(std::vector<MenuNode>& nodes);

    // S7 — populate m_recentItems from Windows UserAssist registry
    void LoadRecentPrograms();

    // Background thread entry point: loads all system icons (S6.1/S6.2/S6.4/S6.5/S7),
    // sets m_iconsLoaded = true, then posts WM_ICONS_LOADED to m_hwnd if it exists.
    void LoadIconsAsync();

    // Draw a subtle horizontal separator line
    void DrawSeparator(HDC hdc, int y, int x1, int x2);

    // ── Hit testing ─────────────────────────────────────────────────────────
    int  GetProgItemAtPoint(POINT pt);      // pinned list; -1 if none
    bool IsOverApRow(POINT pt);             // "All Programs" / "Back" row
    int  GetApItemAtPoint(POINT pt);        // All Programs list item; -1 if none
    bool IsOverShutdownButton(POINT pt);  // "Shut down" text button
    bool IsOverArrowButton(POINT pt);     // dropdown arrow button
    int  GetRightItemAtPoint(POINT pt);     // -1 if none / separator

    // ── Execution ───────────────────────────────────────────────────────────
    void ExecutePinnedItem(int index);
    void ExecuteRecommendedItem(int index); // kept for backward compat
    void ExecuteRightItem(int index);       // Win7 right column launch
    void LaunchApItem(int index);           // launch item from current AP node list

    // ── All Programs navigation ──────────────────────────────────────────────
    const std::vector<MenuNode>& CurrentApNodes() const;
    void NavigateIntoFolder(const std::vector<MenuNode>& children);
    void NavigateBack();

    // ── Hover-to-open lateral submenu (S3.3) ─────────────────────────────────
    void OpenSubMenu(int apNodeIdx);       // show submenu for folder at apNodeIdx
    void CloseSubMenu();                   // hide submenu + reset state
    bool IsOverSubMenu(POINT pt) const;    // true if pt is in the submenu panel
    int  GetSubMenuItemAtPoint(POINT pt);  // visual index in submenu; -1 if none
    void PaintSubMenu(HDC hdc, const RECT& cr);
    void ExecuteSubMenuItem(int visualIdx);

    // S-G — avatar background loading
    void LoadAvatarAsync();
    void DrawAvatarCircle(HDC hdc, int cx, int cy, int r);

    // Task 5 — file-system watcher for Start Menu folders
    void StartFolderWatcher();
    void StopFolderWatcher();

    // Task 3/5 — rebuild program tree on the UI thread after watcher fires
    void RefreshProgramTree();

    // ── Color helpers ────────────────────────────────────────────────────────
    COLORREF CalculateHoverColor();
    COLORREF CalculateSubtleColor();      // slightly lighter/darker than bg
    COLORREF CalculateBorderColor();      // subtle border (S-E: may be overridden)
    COLORREF CalculateSelectionColor();   // keyboard-focus accent (fixed blue)
    COLORREF AnimatedHoverColor();        // S-C: blend bg→hover using m_hoverAnimAlpha

    // ── Name helpers ─────────────────────────────────────────────────────────
    const wchar_t* GetMenuItemName(int index);
    const wchar_t* GetTitle();

    // ── Pinned list — dynamic, persisted ─────────────────────────────────────
    void LoadPinnedItems();
    void SavePinnedItems();
    void UnpinItem(int index);
    void PinItemFromAllPrograms(int apIndex);
    void ShowPinnedContextMenu(int pinnedIndex, POINT screenPt);
    void ShowAllProgramsContextMenu(int apIndex, POINT screenPt);
    void ShowRecentContextMenu(int recentIndex, POINT screenPt);
    void RemoveRecentItem(int recentIndex);
    void SelectCustomIconForPinnedItem(int index);

    // ── Position cache ────────────────────────────────────────────────────────
    void CacheMenuPosition();

    // ── Persistence ──────────────────────────────────────────────────────────
    void LoadCustomNames();
    void SaveCustomNames();
    void LoadRecentExcluded();
    void SaveRecentExcluded();

    // ── Power menu (Sleep / Shut down / Restart popup) ───────────────────────
    void ShowPowerMenu();

    // ── Edit dialog (rename recommended item) ────────────────────────────────
    void ShowEditDialog(int itemIndex);
    static LRESULT CALLBACK EditDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace CrystalFrame
