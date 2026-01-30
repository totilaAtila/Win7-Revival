using System.Security.Principal;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Services;

namespace Win7Revival.App
{
    public sealed partial class MainWindow : Window
    {
        private readonly CoreService _coreService;
        private bool _isInitializing = true;

        public MainWindow(CoreService coreService)
        {
            this.InitializeComponent();
            this.Title = "Win7Revival - Settings";
            _coreService = coreService;

            CheckAdminStatus();
            ModulesList.ItemsSource = _coreService.Modules;

            // Marcăm că inițializarea s-a terminat după ce UI-ul s-a încărcat
            this.Content.Loaded += (_, _) => _isInitializing = false;
        }

        private void CheckAdminStatus()
        {
            try
            {
                using var identity = WindowsIdentity.GetCurrent();
                var principal = new WindowsPrincipal(identity);
                if (!principal.IsInRole(WindowsBuiltInRole.Administrator))
                {
                    AdminWarning.Visibility = Visibility.Visible;
                }
            }
            catch
            {
                AdminWarning.Visibility = Visibility.Visible;
            }
        }

        private async void ToggleSwitch_Toggled(object sender, RoutedEventArgs e)
        {
            // Ignoră evenimentele care vin din binding la load
            if (_isInitializing) return;

            if (sender is ToggleSwitch toggleSwitch && toggleSwitch.DataContext is IModule module)
            {
                if (toggleSwitch.IsOn && !module.IsEnabled)
                {
                    await _coreService.EnableModuleAsync(module.Name);
                }
                else if (!toggleSwitch.IsOn && module.IsEnabled)
                {
                    await _coreService.DisableModuleAsync(module.Name);
                }
            }
        }
    }
}
