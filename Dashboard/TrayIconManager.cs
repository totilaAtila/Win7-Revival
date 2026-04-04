using System;
using System.Runtime.InteropServices;

namespace GlassBar.Dashboard
{
    /// <summary>
    /// Manages a system tray icon via Win32 Shell_NotifyIcon + window subclassing.
    /// No external dependencies — pure P/Invoke consistent with App.xaml.cs patterns.
    /// </summary>
    internal sealed class TrayIconManager : IDisposable
    {
        // ── Win32 structures ─────────────────────────────────────────────────────

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct NOTIFYICONDATA
        {
            public uint   cbSize;
            public IntPtr hWnd;
            public uint   uID;
            public uint   uFlags;
            public uint   uCallbackMessage;
            public IntPtr hIcon;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            public string szTip;
            public uint   dwState;
            public uint   dwStateMask;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string szInfo;
            public uint   uVersion;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
            public string szInfoTitle;
            public uint   dwInfoFlags;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT { public int X; public int Y; }

        // ── Win32 constants ──────────────────────────────────────────────────────

        private const uint NIM_ADD    = 0;
        private const uint NIM_DELETE = 2;

        private const uint NIF_MESSAGE = 0x01;
        private const uint NIF_ICON    = 0x02;
        private const uint NIF_TIP     = 0x04;

        private const uint WM_LBUTTONDBLCLK = 0x0203;
        private const uint WM_RBUTTONUP     = 0x0205;
        private const uint WM_APP_TRAY      = 0x8001; // WM_APP + 1

        private const uint IMAGE_ICON      = 1;
        private const uint LR_LOADFROMFILE = 0x0010;

        private const uint MF_STRING    = 0x0000;
        private const uint MF_SEPARATOR = 0x0800;

        private const uint TPM_RETURNCMD   = 0x0100;
        private const uint TPM_NONOTIFY    = 0x0080;
        private const uint TPM_RIGHTBUTTON = 0x0002;

        private const int GWLP_WNDPROC = -4;

        private const int CMD_SHOW = 1;
        private const int CMD_EXIT = 2;
        private static readonly IntPtr IDI_APPLICATION = new(32512);

        // ── P/Invoke declarations ────────────────────────────────────────────────

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern bool Shell_NotifyIconW(uint dwMessage, ref NOTIFYICONDATA lpData);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadImage(IntPtr hInst, string name, uint type,
                                               int cx, int cy, uint fuLoad);

        [DllImport("user32.dll")]
        private static extern bool DestroyIcon(IntPtr hIcon);

        [DllImport("user32.dll")]
        private static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong);

        [DllImport("user32.dll")]
        private static extern IntPtr CallWindowProc(IntPtr lpPrevWndFunc, IntPtr hWnd,
                                                    uint Msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern IntPtr CreatePopupMenu();

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool AppendMenuW(IntPtr hMenu, uint uFlags,
                                               IntPtr uIDNewItem, string lpNewItem);

        [DllImport("user32.dll")]
        private static extern uint TrackPopupMenu(IntPtr hMenu, uint uFlags,
                                                  int x, int y, int nReserved,
                                                  IntPtr hWnd, IntPtr prcRect);

        [DllImport("user32.dll")]
        private static extern bool DestroyMenu(IntPtr hMenu);

        [DllImport("user32.dll")]
        private static extern bool GetCursorPos(out POINT lpPoint);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern uint RegisterWindowMessage(string lpString);

        [DllImport("user32.dll")]
        private static extern IntPtr LoadIcon(IntPtr hInstance, IntPtr lpIconName);

        // ── WndProc delegate ─────────────────────────────────────────────────────

        private delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg,
                                                IntPtr wParam, IntPtr lParam);

        // ── State ────────────────────────────────────────────────────────────────

        private readonly IntPtr        _hwnd;
        private readonly Action        _onShow;
        private readonly Action        _onExit;
        private readonly uint          _taskbarCreatedMessage;
        private          IntPtr        _oldWndProc;
        private readonly WndProcDelegate _newWndProc; // must stay referenced to prevent GC
        private          IntPtr        _hIcon;
        private          bool          _added;
        private          bool          _disposed;
        private const    uint          IconId = 1;

        // ── Constructor ──────────────────────────────────────────────────────────

        public TrayIconManager(IntPtr hwnd, string iconPath, Action onShow, Action onExit)
        {
            _hwnd   = hwnd;
            _onShow = onShow;
            _onExit = onExit;
            _taskbarCreatedMessage = RegisterWindowMessage("TaskbarCreated");

            // Keep delegate alive — GC must not collect it while subclassing is active
            _newWndProc = TrayWndProc;
            _oldWndProc = SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                              Marshal.GetFunctionPointerForDelegate(_newWndProc));

            if (System.IO.File.Exists(iconPath))
                _hIcon = LoadImage(IntPtr.Zero, iconPath, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
            if (_hIcon == IntPtr.Zero)
                _hIcon = LoadIcon(IntPtr.Zero, IDI_APPLICATION);

            AddTrayIcon();
        }

        // ── Public API ───────────────────────────────────────────────────────────

        public void Remove()
        {
            if (!_added) return;

            var nid = BuildNid();
            Shell_NotifyIconW(NIM_DELETE, ref nid);
            _added = false;

            // Unsubclass window
            if (_oldWndProc != IntPtr.Zero)
            {
                SetWindowLongPtr(_hwnd, GWLP_WNDPROC, _oldWndProc);
                _oldWndProc = IntPtr.Zero;
            }

            if (_hIcon != IntPtr.Zero)
            {
                DestroyIcon(_hIcon);
                _hIcon = IntPtr.Zero;
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Remove();
        }

        public void EnsureAdded()
        {
            if (_disposed || _added) return;
            AddTrayIcon();
        }

        // ── Private helpers ──────────────────────────────────────────────────────

        private void AddTrayIcon()
        {
            if (_disposed || _added) return;

            var nid = BuildNid();
            nid.uFlags = NIF_MESSAGE | NIF_TIP;
            if (_hIcon != IntPtr.Zero) nid.uFlags |= NIF_ICON;
            nid.szTip  = "GlassBar";
            _added = Shell_NotifyIconW(NIM_ADD, ref nid);
        }

        private NOTIFYICONDATA BuildNid()
        {
            var nid = new NOTIFYICONDATA();
            nid.cbSize          = (uint)Marshal.SizeOf<NOTIFYICONDATA>();
            nid.hWnd            = _hwnd;
            nid.uID             = IconId;
            nid.uCallbackMessage = WM_APP_TRAY;
            nid.hIcon           = _hIcon;
            return nid;
        }

        private IntPtr TrayWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
        {
            if (msg == _taskbarCreatedMessage)
            {
                _added = false;
                AddTrayIcon();
                return IntPtr.Zero;
            }

            if (msg == WM_APP_TRAY)
            {
                uint evt = (uint)(lParam.ToInt64() & 0xFFFF);
                switch (evt)
                {
                    case WM_LBUTTONDBLCLK:
                        _onShow();
                        return IntPtr.Zero;
                    case WM_RBUTTONUP:
                        ShowContextMenu(hWnd);
                        return IntPtr.Zero;
                }
                return IntPtr.Zero;
            }

            return CallWindowProc(_oldWndProc, hWnd, msg, wParam, lParam);
        }

        private void ShowContextMenu(IntPtr hWnd)
        {
            GetCursorPos(out POINT pt);
            IntPtr hMenu = CreatePopupMenu();
            if (hMenu == IntPtr.Zero) return;

            try
            {
                AppendMenuW(hMenu, MF_STRING,    (IntPtr)CMD_SHOW, "Open GlassBar");
                AppendMenuW(hMenu, MF_SEPARATOR, IntPtr.Zero,      null!);
                AppendMenuW(hMenu, MF_STRING,    (IntPtr)CMD_EXIT, "Exit");

                // Required so the menu dismisses correctly when clicking elsewhere
                SetForegroundWindow(hWnd);

                uint cmd = TrackPopupMenu(hMenu,
                    TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                    pt.X, pt.Y, 0, hWnd, IntPtr.Zero);

                switch (cmd)
                {
                    case CMD_SHOW: _onShow(); break;
                    case CMD_EXIT: _onExit(); break;
                }
            }
            finally
            {
                DestroyMenu(hMenu);
            }
        }
    }
}
