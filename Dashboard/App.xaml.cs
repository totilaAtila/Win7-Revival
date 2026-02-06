using Microsoft.UI.Xaml;
using System;
using System.Threading;

namespace CrystalFrame.Dashboard
{
    public partial class App : Application
    {
        private Window _window;
        private Mutex _mutex;

        public App()
        {
            InitializeComponent();
        }

        protected override void OnLaunched(LaunchActivatedEventArgs args)
        {
            // Single instance check
            bool createdNew;
            _mutex = new Mutex(true, "CrystalFrame.Dashboard", out createdNew);

            if (!createdNew)
            {
                // Another instance is running - exit
                Environment.Exit(0);
                return;
            }

            _window = new MainWindow();
            _window.Activate();
        }
    }
}
