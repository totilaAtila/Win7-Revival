#pragma once
#include <Windows.h>
#include <string>
#include <vector>

namespace CrystalFrame {

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
/// COM: this function initialises COM on the calling thread if needed
///      (via CoInitializeEx / COINIT_APARTMENTTHREADED) and uninitialises it
///      on exit only when it was the one that initialised it.
///      RPC_E_CHANGED_MODE (another apartment already active) is tolerated.
/// </summary>
std::vector<MenuNode> BuildAllProgramsTree();

} // namespace CrystalFrame
