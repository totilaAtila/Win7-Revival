using System;
using System.Runtime.InteropServices;

namespace GlassBar.Dashboard
{
    /// <summary>
    /// P/Invoke declarations for GlassBar.Core.dll
    /// Direct native calls - no IPC needed
    /// </summary>
    public static class CoreNative
    {
        private const string DllName = "GlassBar.Core.dll";

        [StructLayout(LayoutKind.Sequential)]
        public struct CoreStatus
        {
            [StructLayout(LayoutKind.Sequential)]
            public struct TaskbarInfo
            {
                [MarshalAs(UnmanagedType.Bool)]
                public bool Found;

                [MarshalAs(UnmanagedType.Bool)]
                public bool Enabled;

                public int Opacity;
            }

            [StructLayout(LayoutKind.Sequential)]
            public struct StartInfo
            {
                [MarshalAs(UnmanagedType.Bool)]
                public bool Detected;

                [MarshalAs(UnmanagedType.Bool)]
                public bool Enabled;

                public int Opacity;
            }

            public TaskbarInfo Taskbar;
            public StartInfo Start;
        }

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CoreInitialize();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreShutdown();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetTaskbarOpacity(int opacity);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartOpacity(int opacity);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetTaskbarEnabled([MarshalAs(UnmanagedType.Bool)] bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartEnabled([MarshalAs(UnmanagedType.Bool)] bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuHookEnabled([MarshalAs(UnmanagedType.Bool)] bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetTaskbarColor(int r, int g, int b);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetTaskbarBlur([MarshalAs(UnmanagedType.Bool)] bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartBlur([MarshalAs(UnmanagedType.Bool)] bool enabled);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuOpacity(int opacity);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuBackgroundColor(uint rgb);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuTextColor(uint rgb);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuItems(
            [MarshalAs(UnmanagedType.Bool)] bool controlPanel,
            [MarshalAs(UnmanagedType.Bool)] bool deviceManager,
            [MarshalAs(UnmanagedType.Bool)] bool installedApps,
            [MarshalAs(UnmanagedType.Bool)] bool documents,
            [MarshalAs(UnmanagedType.Bool)] bool pictures,
            [MarshalAs(UnmanagedType.Bool)] bool videos,
            [MarshalAs(UnmanagedType.Bool)] bool recentFiles);

        // S-B: pin Start Menu open for Dashboard preview
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuPinned([MarshalAs(UnmanagedType.Bool)] bool pinned);

        // S-E: explicit border/accent color (0x00RRGGBB)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetStartMenuBorderColor(uint rgb);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreGetStatus(ref CoreStatus status);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CoreProcessMessages();

        // Set XamlBridge blur amount (0=off, 1-100=intensity).
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreSetTaskbarBlurAmount(int amount);

        // Register a global hotkey to toggle the taskbar overlay.
        // vk: virtual-key code; modifiers: MOD_CONTROL=0x2, MOD_ALT=0x1, MOD_SHIFT=0x4, MOD_WIN=0x8
        // Pass vk=0 to disable.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreRegisterHotkey(int vk, int modifiers);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CoreUnregisterHotkey();
    }
}
