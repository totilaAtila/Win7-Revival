using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Services;

namespace Win7Revival.App
{
    public sealed partial class MainWindow : Window
    {
        private readonly CoreService _coreService;

        public MainWindow(CoreService coreService)
        {
            this.InitializeComponent();
            this.Title = "Win7Revival - Settings";
            _coreService = coreService;

            // Populează lista de module
            ModulesList.ItemsSource = _coreService.Modules;
        }

        private async void ToggleSwitch_Toggled(object sender, RoutedEventArgs e)
        {
            if (sender is ToggleSwitch toggleSwitch)
            {
                // Obține modulul asociat din DataContext
                if (toggleSwitch.DataContext is IModule module)
                {
                    // Logica de activare/dezactivare
                    if (toggleSwitch.IsOn)
                    {
                        await _coreService.EnableModuleAsync(module.Name);
                    }
                    else
                    {
                        await _coreService.DisableModuleAsync(module.Name);
                    }
                }
            }
        }
    }
}
