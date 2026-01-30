using System;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Win7Revival.Core.Services;

namespace Win7Revival.App
{
    /// <summary>
    /// Manager pentru system tray icon.
    /// Permite controlul aplicației din notification area (tray).
    /// Folosește H.NotifyIcon.WinUI pentru integrare cu WinUI 3.
    /// </summary>
    public class TrayIconManager : IDisposable
    {
        private readonly CoreService _coreService;
        private readonly Window _mainWindow;
        private bool _disposed;
        private bool _isInitialized;

        /// <summary>
        /// Event declanșat când utilizatorul cere deschiderea ferestrei principale.
        /// </summary>
        public event EventHandler? ShowWindowRequested;

        /// <summary>
        /// Event declanșat când utilizatorul cere închiderea aplicației din tray.
        /// </summary>
        public event EventHandler? ExitRequested;

        public TrayIconManager(CoreService coreService, Window mainWindow)
        {
            _coreService = coreService;
            _mainWindow = mainWindow;
        }

        /// <summary>
        /// Inițializează tray icon-ul cu meniu context.
        /// </summary>
        public void Initialize()
        {
            if (_isInitialized) return;

            try
            {
                // TODO: Implementare H.NotifyIcon.WinUI
                // Pași necesari:
                // 1. Creează TaskbarIcon din H.NotifyIcon.WinUI
                // 2. Setează icon (Assets/tray.ico)
                // 3. Setează tooltip "Win7 Revival"
                // 4. Adaugă context menu:
                //    - "Show Settings" → ShowWindowRequested
                //    - "Toggle Taskbar Transparency" → Enable/Disable module
                //    - Separator
                //    - "Exit" → ExitRequested
                // 5. Double-click → ShowWindowRequested
                //
                // Exemplu cu H.NotifyIcon.WinUI:
                // _taskbarIcon = new TaskbarIcon();
                // _taskbarIcon.IconSource = new BitmapIconSource { UriSource = new Uri("ms-appx:///Assets/tray.ico") };
                // _taskbarIcon.ToolTipText = "Win7 Revival";

                // NOTE: _isInitialized remains false until a real tray icon is created.
                // This prevents MinimizeToTray from hiding the window with no way to restore it.
                Debug.WriteLine("[TrayIconManager] Initialize called but no tray icon created yet (H.NotifyIcon integration pending).");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[TrayIconManager] Failed to initialize: {ex.Message}");
            }
        }

        /// <summary>
        /// Afișează un balloon notification în tray.
        /// </summary>
        public void ShowNotification(string title, string message)
        {
            if (!_isInitialized) return;

            // TODO: _taskbarIcon.ShowNotification(title, message);
            Debug.WriteLine($"[TrayIconManager] Notification: {title} — {message}");
        }

        /// <summary>
        /// Ascunde fereastra principală și continuă rularea din tray.
        /// </summary>
        public void MinimizeToTray()
        {
            if (!_isInitialized)
            {
                // Tray icon not available — minimize to taskbar instead of hiding the window
                // with no way to restore it.
                Debug.WriteLine("[TrayIconManager] Tray icon not initialized, minimizing to taskbar instead.");
                try
                {
                    var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(_mainWindow);
                    ShowWindow(hwnd, 6); // SW_MINIMIZE = 6
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[TrayIconManager] Minimize failed: {ex.Message}");
                }
                return;
            }

            try
            {
                var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(_mainWindow);
                ShowWindow(hwnd, 0); // SW_HIDE = 0
                Debug.WriteLine("[TrayIconManager] Window minimized to tray.");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[TrayIconManager] MinimizeToTray failed: {ex.Message}");
            }
        }

        /// <summary>
        /// Restaurează fereastra principală din tray.
        /// </summary>
        public void RestoreFromTray()
        {
            try
            {
                var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(_mainWindow);
                ShowWindow(hwnd, 9); // SW_RESTORE = 9
                SetForegroundWindow(hwnd);
                Debug.WriteLine("[TrayIconManager] Window restored from tray.");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[TrayIconManager] RestoreFromTray failed: {ex.Message}");
            }
        }

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            // TODO: _taskbarIcon?.Dispose();
            Debug.WriteLine("[TrayIconManager] Disposed.");
        }
    }
}
