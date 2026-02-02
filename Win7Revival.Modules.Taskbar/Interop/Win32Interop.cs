using System;
using System.Runtime.InteropServices;

namespace Win7Revival.Modules.Taskbar.Interop
{
    /// <summary>
    /// Declarații P/Invoke pentru funcțiile Win32 necesare modulului Taskbar.
    /// Acoperă: window discovery, composition attributes, overlay window management,
    /// monitor enumeration, DPI scaling și appbar queries.
    /// </summary>
    public static class Win32Interop
    {
        // ================================================================
        // Window Class Names
        // ================================================================
        public const string TaskbarClassName = "Shell_TrayWnd";
        public const string SecondaryTaskbarClassName = "Shell_SecondaryTrayWnd";

        // ================================================================
        // Composition Attribute Structs & Enums
        // ================================================================
        [StructLayout(LayoutKind.Sequential)]
        public struct WINDOWCOMPOSITIONATTRIB_DATA
        {
            public WINDOWCOMPOSITIONATTRIB Attrib;
            public IntPtr Data;
            public int SizeOfData;
        }

        public enum WINDOWCOMPOSITIONATTRIB
        {
            WCA_UNDEFINED = 0,
            WCA_NCRENDERING_ENABLED = 1,
            WCA_NCRENDERING_POLICY = 2,
            WCA_TRANSITIONS_FORCEDISABLED = 3,
            WCA_ALLOW_NCPAINT = 4,
            WCA_CAPTION_BUTTON_BOUNDS = 5,
            WCA_NONCLIENT_RTL_LAYOUT = 6,
            WCA_FORCE_ICONIC_REPRESENTATION = 7,
            WCA_EXTENDED_FRAME_BOUNDS = 9,
            WCA_COLOR_PREFERENCE = 10,
            WCA_ROUNDED_CORNERS = 11,
            WCA_COLOR_ATTRIBUTE = 12,
            WCA_ACCENT_POLICY = 19
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ACCENT_POLICY
        {
            public ACCENT_STATE AccentState;
            public int AccentFlags;
            public int GradientColor;
            public int AnimationId;
        }

        public enum ACCENT_STATE
        {
            ACCENT_DISABLED = 0,
            ACCENT_ENABLE_GRADIENT = 1,
            ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
            ACCENT_ENABLE_BLURBEHIND = 3,
            ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
            ACCENT_ENABLE_HOSTBACKDROP = 5,
            ACCENT_INVALID_STATE = 6
        }

        // ================================================================
        // RECT — shared between window, monitor, and appbar APIs
        // ================================================================
        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;

            public int Width => Right - Left;
            public int Height => Bottom - Top;

            public override string ToString() => $"[{Left},{Top} {Width}x{Height}]";
        }

        // ================================================================
        // Monitor Enumeration
        // ================================================================
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
        public struct MONITORINFOEX
        {
            public int cbSize;
            public RECT rcMonitor;
            public RECT rcWork;
            public uint dwFlags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string szDevice;
        }

        public const uint MONITORINFOF_PRIMARY = 1;

        public delegate bool MonitorEnumProc(IntPtr hMonitor, IntPtr hdcMonitor,
            ref RECT lprcMonitor, IntPtr dwData);

        // ================================================================
        // Window Styles (for overlay click-through)
        // ================================================================
        public const int GWL_EXSTYLE = -20;

        public const int WS_EX_LAYERED = 0x00080000;
        public const int WS_EX_TRANSPARENT = 0x00000020;
        public const int WS_EX_TOOLWINDOW = 0x00000080;
        public const int WS_EX_TOPMOST = 0x00000008;
        public const int WS_EX_NOACTIVATE = 0x08000000;

        public const uint LWA_ALPHA = 0x00000002;

        public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
        public const uint SWP_SHOWWINDOW = 0x0040;
        public const uint SWP_NOACTIVATE = 0x0010;

        // ================================================================
        // AppBar (taskbar position & auto-hide)
        // ================================================================
        [StructLayout(LayoutKind.Sequential)]
        public struct APPBARDATA
        {
            public int cbSize;
            public IntPtr hWnd;
            public uint uCallbackMessage;
            public uint uEdge;
            public RECT rc;
            public IntPtr lParam;
        }

        public const int ABM_GETSTATE = 0x04;
        public const int ABM_GETTASKBARPOS = 0x05;
        public const int ABS_AUTOHIDE = 0x01;

        public const uint ABE_LEFT = 0;
        public const uint ABE_TOP = 1;
        public const uint ABE_RIGHT = 2;
        public const uint ABE_BOTTOM = 3;

        // ================================================================
        // P/Invoke — Window
        // ================================================================
        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr FindWindow(string lpClassName, string? lpWindowName);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr FindWindowEx(IntPtr hWndParent, IntPtr hWndChildAfter,
            string lpszClass, string? lpszWindow);

        [DllImport("user32.dll")]
        public static extern int SetWindowCompositionAttribute(IntPtr hwnd,
            ref WINDOWCOMPOSITIONATTRIB_DATA data);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        public static extern int GetWindowLong(IntPtr hWnd, int nIndex);

        [DllImport("user32.dll")]
        public static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter,
            int X, int Y, int cx, int cy, uint uFlags);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetLayeredWindowAttributes(IntPtr hwnd, uint crKey,
            byte bAlpha, uint dwFlags);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWindow(IntPtr hWnd);

        // ================================================================
        // P/Invoke — Monitor
        // ================================================================
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr lprcClip,
            MonitorEnumProc lpfnEnum, IntPtr dwData);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFOEX lpmi);

        [DllImport("user32.dll")]
        public static extern int GetSystemMetrics(int nIndex);

        public const int SM_CXSCREEN = 0;
        public const int SM_CYSCREEN = 1;

        // ================================================================
        // P/Invoke — DPI
        // ================================================================
        [DllImport("shcore.dll")]
        public static extern int GetDpiForMonitor(IntPtr hmonitor, int dpiType,
            out uint dpiX, out uint dpiY);

        public const int MDT_EFFECTIVE_DPI = 0;

        // ================================================================
        // P/Invoke — AppBar (taskbar)
        // ================================================================
        [DllImport("shell32.dll")]
        public static extern IntPtr SHAppBarMessage(int dwMessage, ref APPBARDATA pData);

        // ================================================================
        // P/Invoke — Window Messages (Explorer restart detection)
        // ================================================================
        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern uint RegisterWindowMessage(string lpString);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr CreateWindowEx(
            int dwExStyle, string lpClassName, string lpWindowName,
            int dwStyle, int x, int y, int nWidth, int nHeight,
            IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

        [DllImport("user32.dll")]
        public static extern IntPtr DefWindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool DestroyWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);

        [DllImport("user32.dll")]
        public static extern IntPtr DispatchMessage(ref MSG lpmsg);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool TranslateMessage(ref MSG lpMsg);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern ushort RegisterClass(ref WNDCLASS lpWndClass);

        [DllImport("kernel32.dll")]
        public static extern IntPtr GetModuleHandle(string? lpModuleName);

        public delegate IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential)]
        public struct MSG
        {
            public IntPtr hwnd;
            public uint message;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public int pt_x;
            public int pt_y;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
        public struct WNDCLASS
        {
            public uint style;
            public WndProc lpfnWndProc;
            public int cbClsExtra;
            public int cbWndExtra;
            public IntPtr hInstance;
            public IntPtr hIcon;
            public IntPtr hCursor;
            public IntPtr hbrBackground;
            public string? lpszMenuName;
            public string lpszClassName;
        }

        public static readonly IntPtr HWND_MESSAGE = new IntPtr(-3);
        public const uint WM_QUIT = 0x0012;

        // ================================================================
        // Window Styles — popup overlay windows
        // ================================================================
        public const int WS_POPUP = unchecked((int)0x80000000);

        public const uint SWP_NOMOVE = 0x0002;
        public const uint SWP_NOSIZE = 0x0001;

        public const int SW_HIDE = 0;
        public const int SW_SHOWNOACTIVATE = 4;

        public const uint WM_TIMER = 0x0113;
        public const uint WM_DISPLAYCHANGE = 0x007E;
        public const uint WM_DPICHANGED = 0x02E0;

        // ================================================================
        // DWM — Documented Desktop Window Manager APIs
        // ================================================================
        [StructLayout(LayoutKind.Sequential)]
        public struct MARGINS
        {
            public int cxLeftWidth;
            public int cxRightWidth;
            public int cyTopHeight;
            public int cyBottomHeight;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct DWM_BLURBEHIND
        {
            public uint dwFlags;
            [MarshalAs(UnmanagedType.Bool)]
            public bool fEnable;
            public IntPtr hRgnBlur;
            [MarshalAs(UnmanagedType.Bool)]
            public bool fTransitionOnMaximized;
        }

        public const uint DWM_BB_ENABLE = 0x00000001;

        /// <summary>DWMWA_USE_IMMERSIVE_DARK_MODE — required for backdrop effects.</summary>
        public const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;

        /// <summary>DWMWA_SYSTEMBACKDROP_TYPE — documented since Win11 22H2 (build 22621).</summary>
        public const int DWMWA_SYSTEMBACKDROP_TYPE = 38;

        // DWMSBT values for DWMWA_SYSTEMBACKDROP_TYPE
        public const int DWMSBT_NONE = 1;
        public const int DWMSBT_MAINWINDOW = 2;       // Mica
        public const int DWMSBT_TRANSIENTWINDOW = 3;   // Acrylic
        public const int DWMSBT_TABBEDWINDOW = 4;      // Mica Alt

        [DllImport("dwmapi.dll")]
        public static extern int DwmExtendFrameIntoClientArea(IntPtr hwnd, ref MARGINS pMarInset);

        [DllImport("dwmapi.dll")]
        public static extern int DwmSetWindowAttribute(IntPtr hwnd, int dwAttribute,
            ref int pvAttribute, int cbAttribute);

        [DllImport("dwmapi.dll")]
        public static extern int DwmEnableBlurBehindWindow(IntPtr hwnd, ref DWM_BLURBEHIND pBlurBehind);

        // ================================================================
        // P/Invoke — ShowWindow, SetTimer, KillTimer, UnregisterClass
        // ================================================================
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [DllImport("user32.dll")]
        public static extern UIntPtr SetTimer(IntPtr hWnd, UIntPtr nIDEvent,
            uint uElapse, IntPtr lpTimerFunc);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool KillTimer(IntPtr hWnd, UIntPtr uIDEvent);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool UnregisterClass(string lpClassName, IntPtr hInstance);
    }
}
