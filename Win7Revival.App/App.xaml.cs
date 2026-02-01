using System;
using System.Diagnostics;
using System.Linq;
using Microsoft.UI.Xaml;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar;
using Win7Revival.Modules.StartMenu;

namespace Win7Revival.App
{
    /// <summary>
    /// Entry point al aplicației WinUI 3.
    /// Gestionează lifecycle-ul: servicii, module, tray icon, fereastră.
    /// </summary>
    public partial class App : Application
    {
        private MainWindow? _mainWindow;
        private CoreService _coreService = null!;
        private TrayIconManager? _trayIconManager;
        private StartMenuWindow? _startMenuWindow;
        private StartMenuModule? _startMenuModule;

        public App()
        {
            this.InitializeComponent();
            this.UnhandledException += OnUnhandledException;
        }

        protected override async void OnLaunched(LaunchActivatedEventArgs args)
        {
            AppLogger.Log("OnLaunched started");

            // 1. Inițializare servicii
            var settingsService = new SettingsService();
            _coreService = new CoreService(settingsService);
            AppLogger.Log("Services initialized");

            // 2. Înregistrare module (Sprint 1)
            _coreService.RegisterModule(new TaskbarModule(settingsService));
            _startMenuModule = new StartMenuModule(settingsService);
            _coreService.RegisterModule(_startMenuModule);
            AppLogger.Log("Modules registered");

            // 3. Inițializare module (încarcă settings, detectează taskbar)
            try
            {
                await _coreService.InitializeModulesAsync();
                AppLogger.Log("Modules initialized successfully");
            }
            catch (Exception ex)
            {
                AppLogger.LogException(ex, "Module initialization");
                Debug.WriteLine($"[App] Eroare la inițializarea modulelor: {ex.Message}");
            }

            // 4. Creare fereastră principală
            AppLogger.Log("Creating MainWindow");
            _mainWindow = new MainWindow(_coreService, settingsService);
            _mainWindow.Closed += OnMainWindowClosed;

            // Set window icon
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(_mainWindow);
            var iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico");
            Win7Revival.App.WindowIconHelper.SetWindowIcon(hwnd, iconPath);

            // 5. Creare și injectare TrayIconManager
            _trayIconManager = new TrayIconManager(_coreService, _mainWindow);
            _trayIconManager.Initialize();
            _trayIconManager.ShowWindowRequested += (_, _) =>
            {
                _trayIconManager.RestoreFromTray();
            };
            _trayIconManager.ExitRequested += (_, _) =>
            {
                _mainWindow.Close();
            };
            _mainWindow.SetTrayIconManager(_trayIconManager);

            // 6. Creare StartMenuWindow și conectare la modul
            if (_startMenuModule != null)
            {
                _startMenuWindow = new StartMenuWindow(_startMenuModule);
                _startMenuModule.ToggleMenuRequested += (_, _) =>
                {
                    // ToggleMenuRequested fires from the hook thread — must dispatch to UI thread
                    _mainWindow!.DispatcherQueue.TryEnqueue(() =>
                    {
                        _startMenuWindow.Toggle();
                    });
                };
            }

            AppLogger.Log("TrayIcon and StartMenuWindow configured");

            // 7. Activare fereastră
            // Dacă a fost lansat cu --minimized (auto-start la boot), pornește în tray
            bool startMinimized = Environment.GetCommandLineArgs()
                .Any(arg => arg.Equals("--minimized", StringComparison.OrdinalIgnoreCase));

            _mainWindow.Activate();
            AppLogger.Log("MainWindow activated");

            if (startMinimized)
            {
                _trayIconManager.MinimizeToTray();
                AppLogger.Log("Started minimized to tray (auto-start)");
            }
        }

        private void OnMainWindowClosed(object sender, WindowEventArgs args)
        {
            _startMenuWindow?.Close();
            _trayIconManager?.Dispose();
            _coreService.Dispose();
        }

        private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
        {
            AppLogger.LogException(e.Exception, "Unhandled exception");
            Debug.WriteLine($"[App] Excepție neprinsă: {e.Exception}");
            e.Handled = true;
            _trayIconManager?.Dispose();
            _coreService.Dispose();
        }
    }
}
