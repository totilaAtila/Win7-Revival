using System;
using System.Diagnostics;
using System.Windows.Input;
using H.NotifyIcon;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Win7Revival.App.Localization;
using Win7Revival.Core.Services;

namespace Win7Revival.App
{
    /// <summary>
    /// Manager for system tray icon using H.NotifyIcon.WinUI.
    /// Provides notification area icon with context menu for controlling the app.
    /// </summary>
    public class TrayIconManager : IDisposable
    {
        private readonly CoreService _coreService;
        private readonly Window _mainWindow;
        private bool _disposed;
        private bool _isInitialized;
        private TaskbarIcon? _taskbarIcon;
        private MenuFlyoutItem? _showItem;
        private MenuFlyoutItem? _exitItem;

        /// <summary>
        /// Event raised when user requests showing the main window.
        /// </summary>
        public event EventHandler? ShowWindowRequested;

        /// <summary>
        /// Event raised when user requests app exit from tray.
        /// </summary>
        public event EventHandler? ExitRequested;

        public TrayIconManager(CoreService coreService, Window mainWindow)
        {
            _coreService = coreService;
            _mainWindow = mainWindow;
        }

        /// <summary>
        /// Initializes the tray icon with context menu.
        /// </summary>
        public void Initialize()
        {
            if (_isInitialized) return;

            try
            {
                _taskbarIcon = new TaskbarIcon
                {
                    ToolTipText = Strings.Get("TrayTooltip")
                };

                var trayIcoPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "tray.ico");
                _taskbarIcon.Icon = new System.Drawing.Icon(trayIcoPath);

                // Context menu — use Command instead of Click for PopupMenu compatibility
                var menuFlyout = new MenuFlyout();

                _showItem = new MenuFlyoutItem
                {
                    Text = Strings.Get("TrayShowSettings"),
                    Command = new RelayCommand(() => ShowWindowRequested?.Invoke(this, EventArgs.Empty))
                };
                menuFlyout.Items.Add(_showItem);

                menuFlyout.Items.Add(new MenuFlyoutSeparator());

                _exitItem = new MenuFlyoutItem
                {
                    Text = Strings.Get("TrayExit"),
                    Command = new RelayCommand(() => ExitRequested?.Invoke(this, EventArgs.Empty))
                };
                menuFlyout.Items.Add(_exitItem);

                _taskbarIcon.ContextMenuMode = H.NotifyIcon.ContextMenuMode.PopupMenu;
                _taskbarIcon.ContextFlyout = menuFlyout;

                // Double-click to restore window
                _taskbarIcon.LeftClickCommand = new RelayCommand(() =>
                    ShowWindowRequested?.Invoke(this, EventArgs.Empty));

                _taskbarIcon.ForceCreate();

                _isInitialized = true;
                Debug.WriteLine("[TrayIconManager] Initialized with H.NotifyIcon.WinUI.");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[TrayIconManager] Failed to initialize: {ex.Message}");
            }
        }

        /// <summary>
        /// Updates tray menu strings to the current language.
        /// </summary>
        public void ApplyLanguage()
        {
            if (!_isInitialized || _taskbarIcon == null) return;

            _taskbarIcon.ToolTipText = Strings.Get("TrayTooltip");
            if (_showItem != null) _showItem.Text = Strings.Get("TrayShowSettings");
            if (_exitItem != null) _exitItem.Text = Strings.Get("TrayExit");
        }

        /// <summary>
        /// Shows a balloon notification in the tray.
        /// </summary>
        public void ShowNotification(string title, string message)
        {
            if (!_isInitialized || _taskbarIcon == null) return;

            _taskbarIcon.ShowNotification(title, message);
            Debug.WriteLine($"[TrayIconManager] Notification: {title} — {message}");
        }

        /// <summary>
        /// Hides the main window and continues running from the tray.
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
        /// Restores the main window from the tray.
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

            _taskbarIcon?.Dispose();
            _taskbarIcon = null;
            Debug.WriteLine("[TrayIconManager] Disposed.");
        }

        /// <summary>
        /// Simple ICommand implementation for tray menu actions.
        /// </summary>
        private class RelayCommand : ICommand
        {
            private readonly Action _execute;
#pragma warning disable CS0067 // Required by ICommand interface
            public event EventHandler? CanExecuteChanged;
#pragma warning restore CS0067
            public RelayCommand(Action execute) => _execute = execute;
            public bool CanExecute(object? parameter) => true;
            public void Execute(object? parameter) => _execute();
        }
    }
}
