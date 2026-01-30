using System.Diagnostics;
using Microsoft.UI.Xaml;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar;

namespace Win7Revival.App
{
    public partial class App : Application
    {
        private Window? m_window;
        private CoreService _coreService;

        public App()
        {
            this.InitializeComponent();
            this.UnhandledException += OnUnhandledException;

            var settingsService = new SettingsService();
            _coreService = new CoreService(settingsService);

            // Register Modules (Sprint 1)
            _coreService.RegisterModule(new TaskbarModule(settingsService));
        }

        protected override async void OnLaunched(LaunchActivatedEventArgs args)
        {
            try
            {
                await _coreService.InitializeModulesAsync();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[App] Eroare la inițializarea modulelor: {ex.Message}");
            }

            m_window = new MainWindow(_coreService);
            m_window.Closed += OnMainWindowClosed;
            m_window.Activate();
        }

        private void OnMainWindowClosed(object sender, WindowEventArgs args)
        {
            _coreService.Dispose();
        }

        private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
        {
            Debug.WriteLine($"[App] Excepție neprinsă: {e.Exception}");
            _coreService.Dispose();
        }
    }
}
