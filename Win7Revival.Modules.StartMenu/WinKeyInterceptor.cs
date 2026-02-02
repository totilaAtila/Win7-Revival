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
        private bool _startButtonDown;

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
            if (nCode >= 0)
            {
                int msg = (int)wParam;

                if (msg == StartMenuInterop.WM_LBUTTONDOWN)
                {
                    var mouseStruct = Marshal.PtrToStructure<StartMenuInterop.MSLLHOOKSTRUCT>(lParam);

                    if (IsClickOnStartButton(mouseStruct.pt))
                    {
                        Debug.WriteLine("[WinKeyInterceptor] Start button clicked (down).");
                        _startButtonDown = true;
                        WinKeyPressed?.Invoke(this, EventArgs.Empty);
                        return (IntPtr)1; // Suppress down — block Win11 Start Menu
                    }
                }
                else if (msg == StartMenuInterop.WM_LBUTTONUP && _startButtonDown)
                {
                    _startButtonDown = false;
                    Debug.WriteLine("[WinKeyInterceptor] Start button click (up) suppressed.");
                    return (IntPtr)1;
                }
            }

            return StartMenuInterop.CallNextHookEx(_mouseHookId, nCode, wParam, lParam);
        }

        /// <summary>
        /// Detects if a click lands on the Start button area using RECT-based hit testing.
        /// Works regardless of Start button position (left-aligned or centered on Win11).
        /// Checks both primary taskbar (Shell_TrayWnd) and secondary taskbars.
        /// </summary>
        private static bool IsClickOnStartButton(StartMenuInterop.POINT clickPt)
        {
            // Strategy 1: Find the "Start" window class directly and check its RECT
            var startHwnd = StartMenuInterop.FindWindowEx(IntPtr.Zero, IntPtr.Zero, "Start", null);
            while (startHwnd != IntPtr.Zero)
            {
                if (IsPointInWindowRect(startHwnd, clickPt, padding: 4))
                    return true;

                startHwnd = StartMenuInterop.FindWindowEx(IntPtr.Zero, startHwnd, "Start", null);
            }

            // Strategy 2: Look inside Shell_TrayWnd for child windows in the Start button area
            var trayWnd = StartMenuInterop.FindWindow("Shell_TrayWnd", null);
            if (trayWnd != IntPtr.Zero && IsPointInTaskbarStartZone(trayWnd, clickPt))
                return true;

            // Strategy 3: Check secondary taskbars (multi-monitor)
            var secondaryTray = StartMenuInterop.FindWindowEx(IntPtr.Zero, IntPtr.Zero, "Shell_SecondaryTrayWnd", null);
            while (secondaryTray != IntPtr.Zero)
            {
                if (IsPointInTaskbarStartZone(secondaryTray, clickPt))
                    return true;

                secondaryTray = StartMenuInterop.FindWindowEx(IntPtr.Zero, secondaryTray, "Shell_SecondaryTrayWnd", null);
            }

            return false;
        }

        /// <summary>
        /// Checks if click point is within the Start button zone of a taskbar.
        /// Finds the "Start" child window within the taskbar, or falls back to
        /// checking the first ~60px of the taskbar (where Start lives when left-aligned).
        /// </summary>
        private static bool IsPointInTaskbarStartZone(IntPtr taskbarHwnd, StartMenuInterop.POINT pt)
        {
            // First check if the click is even on this taskbar
            if (!StartMenuInterop.GetWindowRect(taskbarHwnd, out var taskbarRect))
                return false;

            if (pt.X < taskbarRect.Left || pt.X > taskbarRect.Right ||
                pt.Y < taskbarRect.Top || pt.Y > taskbarRect.Bottom)
                return false;

            // Look for a "Start" child within this taskbar
            var startChild = StartMenuInterop.FindWindowEx(taskbarHwnd, IntPtr.Zero, "Start", null);
            if (startChild != IntPtr.Zero)
                return IsPointInWindowRect(startChild, pt, padding: 4);

            // Walk all children looking for "Start" class (may be nested)
            var child = StartMenuInterop.FindWindowEx(taskbarHwnd, IntPtr.Zero, null, null);
            while (child != IntPtr.Zero)
            {
                var nested = StartMenuInterop.FindWindowEx(child, IntPtr.Zero, "Start", null);
                if (nested != IntPtr.Zero)
                    return IsPointInWindowRect(nested, pt, padding: 4);

                child = StartMenuInterop.FindWindowEx(taskbarHwnd, child, null, null);
            }

            return false;
        }

        private static bool IsPointInWindowRect(IntPtr hwnd, StartMenuInterop.POINT pt, int padding = 0)
        {
            if (!StartMenuInterop.GetWindowRect(hwnd, out var rect))
                return false;

            return pt.X >= rect.Left - padding && pt.X <= rect.Right + padding &&
                   pt.Y >= rect.Top - padding && pt.Y <= rect.Bottom + padding;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Uninstall();
        }
    }
}
