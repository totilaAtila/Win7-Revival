using Microsoft.UI.Xaml;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar;

namespace Win7Revival.App
{
    public partial class App : Application
    {
        private Window m_window;
        private CoreService _coreService;

        public App()
        {
            this.InitializeComponent();
            
            // Initialize Core Services
            var settingsService = new SettingsService();
            _coreService = new CoreService(settingsService);

            // Register Modules (Sprint 1)
            _coreService.RegisterModule(new TaskbarModule(settingsService));
        }

        protected override async void OnLaunched(Microsoft.UI.Xaml.LaunchActivatedEventArgs args)
        {
            // Initialize all modules
            await _coreService.InitializeModulesAsync();

            // Create main window (Settings UI)
            m_window = new MainWindow(_coreService);
            m_window.Activate();
        }
    }
}
