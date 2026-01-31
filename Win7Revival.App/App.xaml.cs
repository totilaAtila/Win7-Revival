using System;
using System.Diagnostics;
using System.Linq;
using Microsoft.UI.Xaml;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar;

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

        public App()
        {
            this.InitializeComponent();
            this.UnhandledException += OnUnhandledException;
        }

        protected override async void OnLaunched(LaunchActivatedEventArgs args)
        {
            // 1. Inițializare servicii
            var settingsService = new SettingsService();
            _coreService = new CoreService(settingsService);

            // 2. Înregistrare module (Sprint 1)
            _coreService.RegisterModule(new TaskbarModule(settingsService));
            // TODO Sprint 2: _coreService.RegisterModule(new StartMenuModule(settingsService));

            // 3. Inițializare module (încarcă settings, detectează taskbar)
            try
            {
                await _coreService.InitializeModulesAsync();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[App] Eroare la inițializarea modulelor: {ex.Message}");
            }

            // 4. Creare fereastră principală
            _mainWindow = new MainWindow(_coreService, settingsService);
            _mainWindow.Closed += OnMainWindowClosed;

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

            // 6. Activare fereastră
            // Dacă a fost lansat cu --minimized (auto-start la boot), pornește în tray
            bool startMinimized = Environment.GetCommandLineArgs()
                .Any(arg => arg.Equals("--minimized", StringComparison.OrdinalIgnoreCase));

            _mainWindow.Activate();

            if (startMinimized)
            {
                _trayIconManager.MinimizeToTray();
                Debug.WriteLine("[App] Started minimized to tray (auto-start).");
            }
        }

        private void OnMainWindowClosed(object sender, WindowEventArgs args)
        {
            _trayIconManager?.Dispose();
            _coreService.Dispose();
        }

        private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
        {
            Debug.WriteLine($"[App] Excepție neprinsă: {e.Exception}");
            e.Handled = true;
            _trayIconManager?.Dispose();
            _coreService.Dispose();
        }
    }
}
