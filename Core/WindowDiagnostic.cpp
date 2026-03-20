// WindowDiagnostic.cpp - Standalone tool to diagnose Start Menu windows
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

struct WindowInfo {
    HWND hwnd;
    std::wstring className;
    std::wstring title;
    std::wstring processName;
    bool visible;
    bool topLevel;
    DWORD processId;
    RECT rect;
};

std::vector<WindowInfo> g_capturedWindows;
bool g_capturing = false;

std::wstring GetProcessName(DWORD processId) {
    wchar_t processName[MAX_PATH] = L"<unknown>";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess) {
        HMODULE hMod;
        DWORD cbNeeded;
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseNameW(hProcess, hMod, processName, sizeof(processName) / sizeof(wchar_t));
        }
        CloseHandle(hProcess);
    }
    return processName;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!g_capturing) return TRUE;

    WindowInfo info;
    info.hwnd = hwnd;

    // Get class name
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);
    info.className = className;

    // Get window title
    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    info.title = title;

    // Get visibility
    info.visible = IsWindowVisible(hwnd);

    // Check if top-level
    info.topLevel = (GetParent(hwnd) == nullptr);

    // Get process
    GetWindowThreadProcessId(hwnd, &info.processId);
    info.processName = GetProcessName(info.processId);

    // Get rect
    GetWindowRect(hwnd, &info.rect);

    g_capturedWindows.push_back(info);

    return TRUE;
}

void CaptureWindows() {
    g_capturedWindows.clear();
    g_capturing = true;
    EnumWindows(EnumWindowsProc, 0);
    g_capturing = false;
}

void PrintWindows(const std::wstring& filter = L"") {
    std::wcout << L"\n========================================\n";
    std::wcout << L"Total windows captured: " << g_capturedWindows.size() << L"\n";
    std::wcout << L"========================================\n\n";

    int count = 0;
    for (const auto& win : g_capturedWindows) {
        // Apply filter if specified
        if (!filter.empty()) {
            bool matches = false;
            if (win.className.find(filter) != std::wstring::npos) matches = true;
            if (win.title.find(filter) != std::wstring::npos) matches = true;
            if (win.processName.find(filter) != std::wstring::npos) matches = true;
            if (!matches) continue;
        }

        // Skip invisible windows unless they might be Start-related
        if (!win.visible) {
            if (win.className.find(L"Windows.UI") == std::wstring::npos &&
                win.className.find(L"Xaml") == std::wstring::npos &&
                win.processName.find(L"SearchHost") == std::wstring::npos &&
                win.processName.find(L"StartMenuExperienceHost") == std::wstring::npos) {
                continue;
            }
        }

        count++;
        std::wcout << L"[" << count << L"] ===================================\n";
        std::wcout << L"  HWND:        0x" << std::hex << reinterpret_cast<uintptr_t>(win.hwnd) << std::dec << L"\n";
        std::wcout << L"  Class:       " << win.className << L"\n";
        std::wcout << L"  Title:       \"" << win.title << L"\"\n";
        std::wcout << L"  Process:     " << win.processName << L" (PID: " << win.processId << L")\n";
        std::wcout << L"  Visible:     " << (win.visible ? L"YES" : L"NO") << L"\n";
        std::wcout << L"  Top-level:   " << (win.topLevel ? L"YES" : L"NO") << L"\n";
        std::wcout << L"  Position:    (" << win.rect.left << L", " << win.rect.top << L") - ("
                   << win.rect.right << L", " << win.rect.bottom << L")\n";
        std::wcout << L"  Size:        " << (win.rect.right - win.rect.left) << L" x "
                   << (win.rect.bottom - win.rect.top) << L"\n";
        std::wcout << L"\n";
    }

    if (count == 0) {
        std::wcout << L"No windows found matching filter.\n";
    }
}

// Global flag for hotkey
volatile bool g_shouldCapture = false;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        // F5 key
        if (pKbd->vkCode == VK_F5) {
            g_shouldCapture = true;
            return 1; // Consume the key
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    std::wcout << L"========================================\n";
    std::wcout << L"  GlassBar Window Diagnostic Tool\n";
    std::wcout << L"========================================\n\n";

    std::wcout << L"This tool will help identify the Start Menu window.\n\n";
    std::wcout << L"IMPORTANT: Uses F5 hotkey so Start Menu stays open!\n\n";
    std::wcout << L"Instructions:\n";
    std::wcout << L"1. Press F5 to capture baseline windows\n";
    std::wcout << L"2. Open Start Menu (press Windows key)\n";
    std::wcout << L"3. While Start Menu is OPEN, press F5 again\n";
    std::wcout << L"4. Tool will show the differences\n\n";

    // Install keyboard hook
    HHOOK hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (!hook) {
        std::wcout << L"Failed to install keyboard hook!\n";
        return 1;
    }

    std::wcout << L"Press F5 to capture baseline...\n";

    // Wait for F5
    while (!g_shouldCapture) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
    g_shouldCapture = false;

    CaptureWindows();
    std::vector<WindowInfo> baseline = g_capturedWindows;
    std::wcout << L"✓ Captured " << baseline.size() << L" windows (baseline)\n\n";

    std::wcout << L"Now OPEN START MENU and press F5 while it's open...\n";

    // Wait for second F5
    while (!g_shouldCapture) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
    g_shouldCapture = false;

    CaptureWindows();
    std::vector<WindowInfo> withStart = g_capturedWindows;
    UnhookWindowsHookEx(hook);
    std::wcout << L"✓ Captured " << withStart.size() << L" windows\n\n";

    // Find new windows
    std::wcout << L"\n========================================\n";
    std::wcout << L"  NEW WINDOWS (likely Start Menu)\n";
    std::wcout << L"========================================\n\n";

    int newCount = 0;
    for (const auto& win : withStart) {
        bool isNew = true;
        for (const auto& base : baseline) {
            if (win.hwnd == base.hwnd) {
                isNew = false;
                break;
            }
        }

        if (isNew) {
            newCount++;
            std::wcout << L"[" << newCount << L"] NEW WINDOW DETECTED:\n";
            std::wcout << L"  HWND:        0x" << std::hex << reinterpret_cast<uintptr_t>(win.hwnd) << std::dec << L"\n";
            std::wcout << L"  Class:       " << win.className << L"\n";
            std::wcout << L"  Title:       \"" << win.title << L"\"\n";
            std::wcout << L"  Process:     " << win.processName << L"\n";
            std::wcout << L"  Visible:     " << (win.visible ? L"YES" : L"NO") << L"\n";
            std::wcout << L"  Position:    (" << win.rect.left << L", " << win.rect.top << L") - ("
                       << win.rect.right << L", " << win.rect.bottom << L")\n";
            std::wcout << L"  Size:        " << (win.rect.right - win.rect.left) << L" x "
                       << (win.rect.bottom - win.rect.top) << L"\n";
            std::wcout << L"\n";
        }
    }

    if (newCount == 0) {
        std::wcout << L"No new windows detected!\n";
        std::wcout << L"Start Menu might be using an existing window.\n\n";
        std::wcout << L"Showing windows with 'Windows.UI', 'Xaml', or 'Start' in name:\n\n";

        for (const auto& win : withStart) {
            if (win.className.find(L"Windows.UI") != std::wstring::npos ||
                win.className.find(L"Xaml") != std::wstring::npos ||
                win.title.find(L"Start") != std::wstring::npos ||
                win.processName.find(L"SearchHost") != std::wstring::npos ||
                win.processName.find(L"StartMenuExperienceHost") != std::wstring::npos) {

                std::wcout << L"Possible candidate:\n";
                std::wcout << L"  HWND:        0x" << std::hex << reinterpret_cast<uintptr_t>(win.hwnd) << std::dec << L"\n";
                std::wcout << L"  Class:       " << win.className << L"\n";
                std::wcout << L"  Title:       \"" << win.title << L"\"\n";
                std::wcout << L"  Process:     " << win.processName << L"\n";
                std::wcout << L"  Visible:     " << (win.visible ? L"YES" : L"NO") << L"\n\n";
            }
        }
    }

    std::wcout << L"\nPress ENTER to exit...";
    std::wcin.get();

    return 0;
}
