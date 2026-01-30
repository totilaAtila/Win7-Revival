using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Detectează taskbar-ul Windows și monitorizează proprietățile sale.
    /// Suportă multi-monitor: găsește primary + secondary taskbars.
    /// </summary>
    public class TaskbarDetector
    {
        private IntPtr _primaryHandle = IntPtr.Zero;
        private readonly List<IntPtr> _secondaryHandles = new();
        private readonly List<MonitorInfo> _monitors = new();

        /// <summary>
        /// Handle-ul taskbar-ului principal (Shell_TrayWnd).
        /// </summary>
        public IntPtr PrimaryHandle => _primaryHandle;

        /// <summary>
        /// Handle-urile taskbar-urilor secundare (multi-monitor).
        /// </summary>
        public IReadOnlyList<IntPtr> SecondaryHandles => _secondaryHandles.AsReadOnly();

        /// <summary>
        /// Toate handle-urile (primary + secondary).
        /// </summary>
        public IReadOnlyList<IntPtr> AllHandles
        {
            get
            {
                var list = new List<IntPtr>();
                if (_primaryHandle != IntPtr.Zero)
                    list.Add(_primaryHandle);
                list.AddRange(_secondaryHandles);
                return list;
            }
        }

        /// <summary>
        /// Lista monitoarelor detectate.
        /// </summary>
        public IReadOnlyList<MonitorInfo> Monitors => _monitors.AsReadOnly();

        /// <summary>
        /// Detectează taskbar-urile și monitoarele. Apelat la InitializeAsync.
        /// </summary>
        public void Detect()
        {
            DetectTaskbarHandles();
            DetectMonitors();
        }

        /// <summary>
        /// Re-detectează handle-urile (util dacă Explorer.exe repornește).
        /// </summary>
        public void Refresh()
        {
            _secondaryHandles.Clear();
            _monitors.Clear();
            Detect();
        }

        private void DetectTaskbarHandles()
        {
            _primaryHandle = Win32Interop.FindWindow(Win32Interop.TaskbarClassName, null);
            if (_primaryHandle == IntPtr.Zero)
            {
                Debug.WriteLine("[TaskbarDetector] Primary taskbar not found.");
                return;
            }
            Debug.WriteLine($"[TaskbarDetector] Primary taskbar: 0x{_primaryHandle:X}");

            // Caută taskbar-uri secundare (pe monitoare adiționale)
            _secondaryHandles.Clear();
            IntPtr found = IntPtr.Zero;
            while (true)
            {
                found = Win32Interop.FindWindowEx(IntPtr.Zero, found,
                    Win32Interop.SecondaryTaskbarClassName, null);
                if (found == IntPtr.Zero) break;
                _secondaryHandles.Add(found);
                Debug.WriteLine($"[TaskbarDetector] Secondary taskbar: 0x{found:X}");
            }
        }

        private void DetectMonitors()
        {
            _monitors.Clear();
            Win32Interop.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero,
                (IntPtr hMonitor, IntPtr hdcMonitor, ref Win32Interop.RECT lprcMonitor, IntPtr dwData) =>
                {
                    var info = new Win32Interop.MONITORINFOEX();
                    info.cbSize = Marshal.SizeOf(info);
                    if (Win32Interop.GetMonitorInfo(hMonitor, ref info))
                    {
                        _monitors.Add(new MonitorInfo
                        {
                            Handle = hMonitor,
                            Bounds = info.rcMonitor,
                            WorkArea = info.rcWork,
                            IsPrimary = (info.dwFlags & Win32Interop.MONITORINFOF_PRIMARY) != 0,
                            DeviceName = info.szDevice
                        });
                    }
                    return true;
                }, IntPtr.Zero);

            Debug.WriteLine($"[TaskbarDetector] Detected {_monitors.Count} monitor(s).");
        }

        /// <summary>
        /// Obține RECT-ul pentru un taskbar handle dat.
        /// </summary>
        public Win32Interop.RECT GetTaskbarRect(IntPtr taskbarHandle)
        {
            Win32Interop.GetWindowRect(taskbarHandle, out var rect);
            return rect;
        }

        /// <summary>
        /// Determină poziția taskbar-ului pe ecran (Bottom, Top, Left, Right).
        /// </summary>
        public TaskbarPosition GetTaskbarPosition()
        {
            var abd = new Win32Interop.APPBARDATA();
            abd.cbSize = Marshal.SizeOf(abd);
            abd.hWnd = _primaryHandle;
            Win32Interop.SHAppBarMessage(Win32Interop.ABM_GETTASKBARPOS, ref abd);

            return abd.uEdge switch
            {
                Win32Interop.ABE_TOP => TaskbarPosition.Top,
                Win32Interop.ABE_LEFT => TaskbarPosition.Left,
                Win32Interop.ABE_RIGHT => TaskbarPosition.Right,
                _ => TaskbarPosition.Bottom
            };
        }

        /// <summary>
        /// Verifică dacă taskbar-ul e setat pe auto-hide.
        /// </summary>
        public bool IsAutoHideEnabled()
        {
            var abd = new Win32Interop.APPBARDATA();
            abd.cbSize = Marshal.SizeOf(abd);
            abd.hWnd = _primaryHandle;
            var result = Win32Interop.SHAppBarMessage(Win32Interop.ABM_GETSTATE, ref abd);
            return ((int)result & Win32Interop.ABS_AUTOHIDE) != 0;
        }

        /// <summary>
        /// Verifică dacă un handle este încă valid.
        /// </summary>
        public bool IsHandleValid(IntPtr handle) => Win32Interop.IsWindow(handle);
    }

    /// <summary>
    /// Poziția taskbar-ului pe ecran.
    /// </summary>
    public enum TaskbarPosition
    {
        Bottom,
        Top,
        Left,
        Right
    }

    /// <summary>
    /// Informații despre un monitor detectat.
    /// </summary>
    public class MonitorInfo
    {
        public IntPtr Handle { get; set; }
        public Win32Interop.RECT Bounds { get; set; }
        public Win32Interop.RECT WorkArea { get; set; }
        public bool IsPrimary { get; set; }
        public string DeviceName { get; set; } = string.Empty;

        public override string ToString() =>
            $"{DeviceName} {Bounds.Width}x{Bounds.Height}{(IsPrimary ? " [Primary]" : "")}";
    }
}
