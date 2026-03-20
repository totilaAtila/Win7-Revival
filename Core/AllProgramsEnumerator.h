#pragma once
#include <Windows.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <shellapi.h>  // HICON (via Windows.h already, but explicit for clarity)

namespace GlassBar {

/// <summary>
/// IconCache — deduplicated HICON lifetime manager.
///
/// Maps canonical file paths to HICON handles so that every unique path is
/// loaded exactly once. DestroyIcon is called for all handles on ReleaseAll().
/// Not thread-safe: call exclusively from the icon-loading thread, then call
/// ReleaseAll() from the UI thread only after the loading thread has exited.
/// </summary>
class IconCache {
public:
    /// Return a cached HICON for |path|, or load+cache it via SHGetFileInfoW.
    /// Returns nullptr on failure (file not found, SHGetFileInfoW fails, etc.).
    HICON GetIcon(const std::wstring& path, bool smallIcon = false) {
        auto key = path + (smallIcon ? L"|S" : L"|L");
        auto it = m_cache.find(key);
        if (it != m_cache.end())
            return it->second;

        SHFILEINFOW sfi = {};
        UINT flags = SHGFI_ICON | (smallIcon ? SHGFI_SMALLICON : SHGFI_LARGEICON);
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), flags) && sfi.hIcon) {
            m_cache[key] = sfi.hIcon;
            return sfi.hIcon;
        }
        return nullptr;
    }

    /// Return a cached stock icon, loading it if not yet cached.
    HICON GetStockIcon(SHSTOCKICONID id, bool smallIcon = false) {
        int key = static_cast<int>(id) * 2 + (smallIcon ? 1 : 0);
        auto it = m_stock.find(key);
        if (it != m_stock.end())
            return it->second;

        SHSTOCKICONINFO sii = {};
        sii.cbSize = sizeof(sii);
        UINT flags = SHGSI_ICON | (smallIcon ? SHGSI_SMALLICON : SHGSI_LARGEICON);
        if (SUCCEEDED(SHGetStockIconInfo(id, flags, &sii)) && sii.hIcon) {
            m_stock[key] = sii.hIcon;
            return sii.hIcon;
        }
        return nullptr;
    }

    /// Call DestroyIcon on every cached handle and clear the maps.
    void ReleaseAll() {
        for (auto& kv : m_cache) {
            if (kv.second) DestroyIcon(kv.second);
        }
        m_cache.clear();
        for (auto& kv : m_stock) {
            if (kv.second) DestroyIcon(kv.second);
        }
        m_stock.clear();
    }

    ~IconCache() { ReleaseAll(); }

private:
    std::unordered_map<std::wstring, HICON> m_cache;
    std::unordered_map<int, HICON>          m_stock;
};



/// <summary>
/// One node in the All Programs tree.
/// Leaf nodes (isFolder == false) hold a resolved launch target.
/// Folder nodes have their children populated after BuildAllProgramsTree().
/// </summary>
struct MenuNode {
    std::wstring          name;        // Display name (no file extension)
    bool                  isFolder;    // true  → children populated; target/args unused
    std::wstring          target;      // Resolved exe / URL (empty for folders)
    std::wstring          args;        // Command-line arguments from .lnk (may be empty)
    std::wstring          folderPath;  // Absolute filesystem path (folders only)
    std::wstring          lnkPath;     // S6.5: original .lnk/.url file path (shortcuts only)
    HICON                 hIcon = nullptr; // S6.5: loaded by StartMenuWindow::Initialize();
                                           // never set during BuildAllProgramsTree() so
                                           // MergeTree/sort operate safely on null handles.
    std::vector<MenuNode> children;    // Sub-items (folders first, then shortcuts, alpha)
};

/// <summary>
/// Resolve a .lnk or .url file to a launchable target and optional arguments.
///
/// Precondition: COM must be initialised on the calling thread when resolving
///               .lnk files (CoCreateInstance is used).  BuildAllProgramsTree()
///               initialises COM itself, so this precondition is satisfied when
///               called from there.  Direct callers must ensure COM is ready.
///
/// .lnk  → IShellLinkW::GetPath / GetArguments
/// .url  → GetPrivateProfileStringW "URL=" key from [InternetShortcut]
///
/// Returns true when outTarget is non-empty; false on any failure (logged internally).
/// </summary>
bool ResolveShortcutTarget(const std::wstring& path,
                           std::wstring&       outTarget,
                           std::wstring&       outArgs);

/// <summary>
/// Enumerate the two Windows Start Menu Programs folders
///   • FOLDERID_CommonPrograms  (%ProgramData%\...\Programs)
///   • FOLDERID_Programs        (%AppData%\...\Programs)
/// and return a merged, sorted top-level tree.
///
/// Merge rules:
///   • Folders with the same name (case-insensitive) are merged recursively.
///   • Same-name shortcuts: the user-profile version wins.
///   • Sort order: folders first (alpha), then shortcuts (alpha), both case-insensitive.
///
/// COM: this function calls CoInitializeEx(COINIT_APARTMENTTHREADED) and
///      always balances it with CoUninitialize() on exit for any successful
///      return (S_OK or S_FALSE — both increment the reference count per MSDN).
///      RPC_E_CHANGED_MODE (different apartment already active) is tolerated
///      and requires no balancing call.
/// </summary>
std::vector<MenuNode> BuildAllProgramsTree();

} // namespace GlassBar
