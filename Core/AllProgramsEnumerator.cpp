#include "AllProgramsEnumerator.h"
#include "Diagnostics.h"

#include <shlobj.h>       // SHGetKnownFolderPath, FOLDERID_*, IShellLinkW, IPersistFile
#include <shobjidl.h>     // CLSID_ShellLink (via shlobj.h on MSVC, but explicit is safer)
#include <objbase.h>      // CoCreateInstance, IID_*
#include <algorithm>
#include <cwctype>        // towlower

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace CrystalFrame {

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Case-insensitive file extension check (includes the dot, e.g. L".lnk").
static std::wstring GetExtLower(const std::wstring& filename) {
    auto dot = filename.rfind(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring ext = filename.substr(dot);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext;
}

/// Alphabetical, case-insensitive node comparator: folders first, then shortcuts.
static bool NodeLess(const MenuNode& a, const MenuNode& b) {
    if (a.isFolder != b.isFolder) return a.isFolder > b.isFolder;   // folders first
    return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
}

// ── ResolveShortcutTarget ─────────────────────────────────────────────────────

bool ResolveShortcutTarget(const std::wstring& path,
                           std::wstring&       outTarget,
                           std::wstring&       outArgs) {
    outTarget.clear();
    outArgs.clear();

    const std::wstring ext = GetExtLower(path);

    // ── .lnk — resolve via IShellLinkW ───────────────────────────────────────
    if (ext == L".lnk") {
        IShellLinkW* psl = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW,
                                      reinterpret_cast<void**>(&psl));
        if (FAILED(hr)) {
            CF_LOG(Warning, "ResolveShortcutTarget: CoCreateInstance(ShellLink) failed hr=0x"
                   << std::hex << hr << " for " << path.size() << "-char path");
            return false;
        }

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (FAILED(hr)) {
            CF_LOG(Warning, "ResolveShortcutTarget: QueryInterface(IPersistFile) failed hr=0x"
                   << std::hex << hr);
            psl->Release();
            return false;
        }

        hr = ppf->Load(path.c_str(), STGM_READ);
        ppf->Release();

        if (FAILED(hr)) {
            CF_LOG(Warning, "ResolveShortcutTarget: IPersistFile::Load failed hr=0x"
                   << std::hex << hr);
            psl->Release();
            return false;
        }

        // Resolve the link (SLR_NO_UI suppresses any UI on broken shortcuts)
        psl->Resolve(nullptr, SLR_NO_UI | SLR_UPDATE);

        wchar_t targetBuf[MAX_PATH] = {};
        hr = psl->GetPath(targetBuf, MAX_PATH, nullptr, SLGP_RAWPATH);
        if (SUCCEEDED(hr) && targetBuf[0] != L'\0')
            outTarget = targetBuf;

        wchar_t argsBuf[INFOTIPSIZE] = {};
        if (SUCCEEDED(psl->GetArguments(argsBuf, INFOTIPSIZE)) && argsBuf[0] != L'\0')
            outArgs = argsBuf;

        psl->Release();

        if (outTarget.empty()) {
            CF_LOG(Warning, "ResolveShortcutTarget: empty target after GetPath");
            return false;
        }
        return true;
    }

    // ── .url — parse [InternetShortcut] URL= ─────────────────────────────────
    if (ext == L".url") {
        wchar_t urlBuf[2048] = {};
        DWORD got = GetPrivateProfileStringW(L"InternetShortcut", L"URL",
                                             L"", urlBuf,
                                             static_cast<DWORD>(std::size(urlBuf)),
                                             path.c_str());
        if (got == 0) {
            CF_LOG(Warning, "ResolveShortcutTarget: no URL= key in .url file");
            return false;
        }
        outTarget = urlBuf;
        return true;
    }

    CF_LOG(Warning, "ResolveShortcutTarget: unsupported extension");
    return false;
}

// ── Internal tree-building helpers ────────────────────────────────────────────

/// Recursively scan a directory and return a MenuNode for it.
/// name is set by the caller (the node's display name).
static MenuNode ScanFolder(const std::wstring& folderPath) {
    MenuNode folder;
    folder.isFolder   = true;
    folder.folderPath = folderPath;

    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = folderPath + L"\\*";
    HANDLE hf = FindFirstFileW(pattern.c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return folder;

    do {
        // Skip hidden/system entries and . / ..
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        const std::wstring entryName = fd.cFileName;
        if (entryName == L"." || entryName == L"..") continue;

        const std::wstring fullPath = folderPath + L"\\" + entryName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            MenuNode child = ScanFolder(fullPath);
            child.name = entryName;
            folder.children.push_back(std::move(child));
        } else {
            const std::wstring ext = GetExtLower(entryName);
            if (ext != L".lnk" && ext != L".url") continue;  // skip unrecognised files

            MenuNode item;
            item.isFolder = false;
            // Strip extension for display
            auto dot = entryName.rfind(L'.');
            item.name = (dot != std::wstring::npos) ? entryName.substr(0, dot) : entryName;

            if (!ResolveShortcutTarget(fullPath, item.target, item.args)) {
                CF_LOG(Warning, "ScanFolder: could not resolve shortcut, skipping entry");
                continue;  // omit unresolvable shortcuts — no dead UI
            }
            folder.children.push_back(std::move(item));
        }
    } while (FindNextFileW(hf, &fd));

    FindClose(hf);

    std::sort(folder.children.begin(), folder.children.end(), NodeLess);
    return folder;
}

/// Merge overlay tree INTO base (user-profile overlays common programs).
/// Same-name folder → recursive merge; same-name shortcut → overlay wins.
static void MergeTree(std::vector<MenuNode>& base, std::vector<MenuNode>&& overlay) {
    for (auto& oNode : overlay) {
        auto it = std::find_if(base.begin(), base.end(), [&](const MenuNode& b) {
            return b.isFolder == oNode.isFolder &&
                   _wcsicmp(b.name.c_str(), oNode.name.c_str()) == 0;
        });

        if (it == base.end()) {
            base.push_back(std::move(oNode));
        } else if (oNode.isFolder && it->isFolder) {
            MergeTree(it->children, std::move(oNode.children));
        } else {
            *it = std::move(oNode);  // user shortcut wins
        }
    }
    std::sort(base.begin(), base.end(), NodeLess);
}

// ── BuildAllProgramsTree ──────────────────────────────────────────────────────

std::vector<MenuNode> BuildAllProgramsTree() {
    // Ensure COM is initialised on this thread so that IShellLinkW can be
    // created for every .lnk we encounter.  We tolerate:
    //   S_OK              — we initialised it; we must CoUninitialize() later.
    //   S_FALSE           — already initialised in the same apartment; leave it.
    //   RPC_E_CHANGED_MODE — another apartment type is active; CoCreateInstance
    //                        still works for in-process servers, so proceed.
    // Any other failure means COM is unusable on this thread — bail out.
    HRESULT hrCom    = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool    weInited = (hrCom == S_OK);

    if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE) {
        CF_LOG(Warning, "BuildAllProgramsTree: CoInitializeEx failed hr=0x"
               << std::hex << hrCom << " — aborting tree build");
        return {};
    }

    // RAII guard: uninitialise only if we were the one to initialise.
    struct ComGuard {
        bool active;
        ~ComGuard() { if (active) CoUninitialize(); }
    } comGuard{ weInited };

    std::vector<MenuNode> tree;

    // 1. Common programs (%ProgramData%\Microsoft\Windows\Start Menu\Programs)
    {
        PWSTR commonPath = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_CommonPrograms,
                                          KF_FLAG_DEFAULT, nullptr, &commonPath);
        if (SUCCEEDED(hr) && commonPath) {
            MenuNode root = ScanFolder(commonPath);
            tree = std::move(root.children);
            CoTaskMemFree(commonPath);
        } else {
            CF_LOG(Warning, "BuildAllProgramsTree: FOLDERID_CommonPrograms unavailable hr=0x"
                   << std::hex << hr);
        }
    }

    // 2. User programs (%AppData%\Microsoft\Windows\Start Menu\Programs) — overlaid on top
    {
        PWSTR userPath = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_Programs,
                                          KF_FLAG_DEFAULT, nullptr, &userPath);
        if (SUCCEEDED(hr) && userPath) {
            MenuNode root = ScanFolder(userPath);
            MergeTree(tree, std::move(root.children));
            CoTaskMemFree(userPath);
        } else {
            CF_LOG(Warning, "BuildAllProgramsTree: FOLDERID_Programs unavailable hr=0x"
                   << std::hex << hr);
        }
    }

    CF_LOG(Info, "BuildAllProgramsTree: " << tree.size() << " top-level nodes");
    return tree;
}

} // namespace CrystalFrame
