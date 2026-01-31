using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Threading;
using Win7Revival.Modules.StartMenu.Interop;

namespace Win7Revival.Modules.StartMenu
{
    /// <summary>
    /// Interceptează apăsarea tastei Win (bare press, fără combinații).
    /// Folosește SetWindowsHookEx(WH_KEYBOARD_LL) — API public Win32, nu DLL injection.
    /// Hook-ul rulează pe un thread dedicat cu message pump.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public class WinKeyInterceptor : IDisposable
    {
        private IntPtr _kbHookId = IntPtr.Zero;
        private IntPtr _mouseHookId = IntPtr.Zero;
        private Thread? _hookThread;
        private uint _hookThreadId;
        private bool _disposed;
        private bool _winKeyDown;
        private bool _otherKeyPressed;

        // Prevent GC collection of the delegates
        private StartMenuInterop.LowLevelKeyboardProc? _kbHookProc;
        private StartMenuInterop.LowLevelMouseProc? _mouseHookProc;

        /// <summary>
        /// When true, keyboard hook intercepts Win key. When false, only mouse hook runs.
        /// </summary>
        public bool InterceptKeyboard { get; set; } = true;

        /// <summary>
        /// Fired when the user presses and releases Win key without other keys,
        /// or clicks on the Start button.
        /// </summary>
        public event EventHandler? WinKeyPressed;

        /// <summary>
        /// Installs hooks on a dedicated thread.
        /// </summary>
        public void Install()
        {
            if (_hookThread != null) return;

            _hookThread = new Thread(HookThreadLoop)
            {
                Name = "Win7Revival.WinKeyHook",
                IsBackground = true
            };
            _hookThread.SetApartmentState(ApartmentState.STA);
            _hookThread.Start();

            Debug.WriteLine("[WinKeyInterceptor] Hook thread started.");
        }

        /// <summary>
        /// Uninstalls the keyboard hook and stops the thread.
        /// </summary>
        public void Uninstall()
        {
            if (_hookThread == null) return;

            if (_hookThreadId != 0)
            {
                StartMenuInterop.PostThreadMessage(_hookThreadId, StartMenuInterop.WM_QUIT, IntPtr.Zero, IntPtr.Zero);
            }

            _hookThread.Join(2000);
            _hookThread = null;
            _hookThreadId = 0;

            Debug.WriteLine("[WinKeyInterceptor] Hook thread stopped.");
        }

        private void HookThreadLoop()
        {
            _hookThreadId = StartMenuInterop.GetCurrentThreadId();
            var hModule = StartMenuInterop.GetModuleHandle(null);

            // Install keyboard hook (only if InterceptKeyboard is true)
            if (InterceptKeyboard)
            {
                _kbHookProc = KeyboardHookCallback;
                _kbHookId = StartMenuInterop.SetWindowsHookEx(
                    StartMenuInterop.WH_KEYBOARD_LL, _kbHookProc, hModule, 0);

                if (_kbHookId == IntPtr.Zero)
                    Debug.WriteLine("[WinKeyInterceptor] Failed to install keyboard hook.");
                else
                    Debug.WriteLine($"[WinKeyInterceptor] Keyboard hook installed, ID=0x{_kbHookId:X}");
            }
            else
            {
                Debug.WriteLine("[WinKeyInterceptor] Keyboard hook skipped (InterceptKeyboard=false).");
            }

            // Install mouse hook (to catch Start button clicks)
            _mouseHookProc = MouseHookCallback;
            _mouseHookId = StartMenuInterop.SetWindowsHookEx(
                StartMenuInterop.WH_MOUSE_LL, _mouseHookProc, hModule, 0);

            if (_mouseHookId == IntPtr.Zero)
                Debug.WriteLine("[WinKeyInterceptor] Failed to install mouse hook.");
            else
                Debug.WriteLine($"[WinKeyInterceptor] Mouse hook installed, ID=0x{_mouseHookId:X}");

            // Message pump — required for LL hooks
            while (StartMenuInterop.GetMessage(out var msg, IntPtr.Zero, 0, 0))
            {
                StartMenuInterop.TranslateMessage(ref msg);
                StartMenuInterop.DispatchMessage(ref msg);
            }

            // Cleanup hooks
            if (_kbHookId != IntPtr.Zero)
            {
                StartMenuInterop.UnhookWindowsHookEx(_kbHookId);
                _kbHookId = IntPtr.Zero;
            }
            if (_mouseHookId != IntPtr.Zero)
            {
                StartMenuInterop.UnhookWindowsHookEx(_mouseHookId);
                _mouseHookId = IntPtr.Zero;
            }

            Debug.WriteLine("[WinKeyInterceptor] Hook thread message loop exited.");
        }

        private IntPtr KeyboardHookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0)
            {
                var hookStruct = Marshal.PtrToStructure<StartMenuInterop.KBDLLHOOKSTRUCT>(lParam);
                int msg = (int)wParam;
                int vk = (int)hookStruct.vkCode;

                if (vk == StartMenuInterop.VK_LWIN || vk == StartMenuInterop.VK_RWIN)
                {
                    if (msg == StartMenuInterop.WM_KEYDOWN || msg == StartMenuInterop.WM_SYSKEYDOWN)
                    {
                        if (!_winKeyDown)
                        {
                            _winKeyDown = true;
                            _otherKeyPressed = false;
                        }
                        // Suppress key-down to prevent Win11 Start Menu from activating
                        return (IntPtr)1;
                    }
                    else if (msg == StartMenuInterop.WM_KEYUP || msg == StartMenuInterop.WM_SYSKEYUP)
                    {
                        bool wasBarePress = _winKeyDown && !_otherKeyPressed;
                        _winKeyDown = false;

                        if (wasBarePress)
                        {
                            Debug.WriteLine("[WinKeyInterceptor] Bare Win key press detected.");
                            WinKeyPressed?.Invoke(this, EventArgs.Empty);
                        }
                        // Always suppress key-up to fully block Win11 Start
                        return (IntPtr)1;
                    }
                }
                else if (_winKeyDown)
                {
                    // Another key pressed while Win is held — it's a combo (Win+D, Win+E, etc.)
                    // Let the combo through but mark it so we don't trigger our menu
                    _otherKeyPressed = true;
                }
            }

            return StartMenuInterop.CallNextHookEx(_kbHookId, nCode, wParam, lParam);
        }

        private IntPtr MouseHookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0 && (int)wParam == StartMenuInterop.WM_LBUTTONDOWN)
            {
                var mouseStruct = Marshal.PtrToStructure<StartMenuInterop.MSLLHOOKSTRUCT>(lParam);
                var hwndUnderCursor = StartMenuInterop.WindowFromPoint(mouseStruct.pt);

                if (hwndUnderCursor != IntPtr.Zero && IsStartButtonWindow(hwndUnderCursor))
                {
                    Debug.WriteLine("[WinKeyInterceptor] Start button clicked.");
                    WinKeyPressed?.Invoke(this, EventArgs.Empty);
                    return (IntPtr)1; // Suppress — block Win11 Start Menu
                }
            }

            return StartMenuInterop.CallNextHookEx(_mouseHookId, nCode, wParam, lParam);
        }

        /// <summary>
        /// Checks if the window (or any ancestor up to the taskbar) is the Start button.
        /// On Win11, clicks may land on XAML composition layers overlaying the actual Start window.
        /// </summary>
        private static bool IsStartButtonWindow(IntPtr hwnd)
        {
            // Walk up the window hierarchy (max 10 levels to avoid infinite loops)
            var current = hwnd;
            for (int i = 0; i < 10 && current != IntPtr.Zero; i++)
            {
                var className = new System.Text.StringBuilder(256);
                StartMenuInterop.GetClassName(current, className, 256);
                var cls = className.ToString();

                if (cls == "Start")
                    return true;

                // Stop walking if we've reached the taskbar
                if (cls == "Shell_TrayWnd" || cls == "Shell_SecondaryTrayWnd")
                    break;

                current = StartMenuInterop.GetParent(current);
            }

            // Also check if the root owner of the clicked window is the Start button
            var root = StartMenuInterop.GetAncestor(hwnd, StartMenuInterop.GA_ROOT);
            if (root != IntPtr.Zero)
            {
                var rootClass = new System.Text.StringBuilder(256);
                StartMenuInterop.GetClassName(root, rootClass, 256);
                if (rootClass.ToString() == "Shell_TrayWnd")
                {
                    // We're inside the taskbar — check if the Start button HWND contains our click point
                    var startHwnd = StartMenuInterop.FindWindow("Start", null);
                    if (startHwnd != IntPtr.Zero)
                    {
                        StartMenuInterop.GetWindowRect(startHwnd, out var startRect);
                        // Get cursor position (more reliable than hook point for RECT check)
                        StartMenuInterop.GetCursorPos(out var cursorPt);
                        if (cursorPt.X >= startRect.Left && cursorPt.X <= startRect.Right &&
                            cursorPt.Y >= startRect.Top && cursorPt.Y <= startRect.Bottom)
                        {
                            return true;
                        }
                    }
                }
            }

            return false;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Uninstall();
        }
    }
}
