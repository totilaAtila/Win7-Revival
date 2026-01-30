using System;
using System.Runtime.InteropServices;

namespace Win7Revival.Modules.Taskbar.Interop
{
    /// <summary>
    /// Clasa care conține declarațiile P/Invoke pentru funcțiile Win32.
    /// Acestea sunt necesare pentru a interacționa cu Taskbar-ul Windows.
    /// </summary>
    public static class Win32Interop
    {
        // Numele clasei ferestrei Taskbar-ului
        public const string TaskbarClassName = "Shell_TrayWnd";

        // Structura necesară pentru SetWindowCompositionAttribute
        [StructLayout(LayoutKind.Sequential)]
        public struct WINDOWCOMPOSITIONATTRIB_DATA
        {
            public WINDOWCOMPOSITIONATTRIB Attrib;
            public IntPtr Data;
            public int SizeOfData;
        }

        // Enum pentru tipul de atribut de compoziție
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
            WCA_ACCENT_POLICY = 19 // Acesta este cel mai important pentru efecte de blur/transparenta
        }

        // Structura pentru setarea politicii de accent (transparenta/blur)
        [StructLayout(LayoutKind.Sequential)]
        public struct ACCENT_POLICY
        {
            public ACCENT_STATE AccentState;
            public int AccentFlags;
            public int GradientColor;
            public int AnimationId;
        }

        // Enum pentru starea de accent
        public enum ACCENT_STATE
        {
            ACCENT_DISABLED = 0,
            ACCENT_ENABLE_GRADIENT = 1,
            ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
            ACCENT_ENABLE_BLURBEHIND = 3, // Aero Glass / Mica effect
            ACCENT_ENABLE_ACRYLICBLURBEHIND = 4, // Acrylic effect
            ACCENT_ENABLE_HOSTBACKDROP = 5, // Mica Alt / Tabbed
            ACCENT_INVALID_STATE = 6
        }

        /// <summary>
        /// Găsește fereastra Taskbar-ului.
        /// </summary>
        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

        /// <summary>
        /// Setează atributele de compoziție ale ferestrei (pentru transparenta/blur).
        /// </summary>
        [DllImport("user32.dll")]
        public static extern int SetWindowCompositionAttribute(IntPtr hwnd, ref WINDOWCOMPOSITIONATTRIB_DATA data);
    }
}
