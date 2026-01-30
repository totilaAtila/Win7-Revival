using System;
using System.Diagnostics;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Helper pentru DPI scaling pe sisteme multi-monitor.
    /// Asigură că overlay-ul este dimensionat și poziționat corect
    /// indiferent de DPI-ul fiecărui monitor.
    /// </summary>
    public static class DpiHelper
    {
        /// <summary>
        /// DPI-ul implicit Windows (100% scaling).
        /// </summary>
        public const uint DefaultDpi = 96;

        /// <summary>
        /// Obține DPI-ul efectiv pentru un monitor specific.
        /// Returnează 96 (default) dacă apelul eșuează.
        /// </summary>
        public static uint GetDpiForMonitor(IntPtr hMonitor)
        {
            try
            {
                int hr = Win32Interop.GetDpiForMonitor(hMonitor,
                    Win32Interop.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
                if (hr == 0 && dpiX > 0)
                    return dpiX;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[DpiHelper] GetDpiForMonitor failed: {ex.Message}");
            }
            return DefaultDpi;
        }

        /// <summary>
        /// Calculează factorul de scalare (ex: 1.0, 1.25, 1.5, 2.0).
        /// </summary>
        public static double GetScaleFactor(IntPtr hMonitor)
        {
            return GetDpiForMonitor(hMonitor) / (double)DefaultDpi;
        }

        /// <summary>
        /// Convertește o valoare logică (design-time) în pixeli fizici
        /// pentru un monitor specific.
        /// </summary>
        public static int LogicalToPhysical(int logicalValue, IntPtr hMonitor)
        {
            double scale = GetScaleFactor(hMonitor);
            return (int)Math.Round(logicalValue * scale);
        }

        /// <summary>
        /// Convertește pixeli fizici în valoare logică.
        /// </summary>
        public static int PhysicalToLogical(int physicalValue, IntPtr hMonitor)
        {
            double scale = GetScaleFactor(hMonitor);
            return (int)Math.Round(physicalValue / scale);
        }

        /// <summary>
        /// Convertește coordonate între două monitoare cu DPI diferit.
        /// </summary>
        public static int ConvertBetweenMonitors(int value, IntPtr sourceMonitor, IntPtr targetMonitor)
        {
            uint sourceDpi = GetDpiForMonitor(sourceMonitor);
            uint targetDpi = GetDpiForMonitor(targetMonitor);
            if (sourceDpi == targetDpi) return value;
            return (int)Math.Round(value * (targetDpi / (double)sourceDpi));
        }
    }
}
