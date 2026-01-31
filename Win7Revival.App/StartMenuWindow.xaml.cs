using System;
using System.Collections.Generic;
using System.Diagnostics;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Win7Revival.Core.Models;
using Win7Revival.Modules.StartMenu;
using Win7Revival.Modules.StartMenu.Interop;
using Win7Revival.Modules.StartMenu.Services;
using WinRT.Interop;

namespace Win7Revival.App
{
    /// <summary>
    /// Fereastra borderless pentru Classic Start Menu.
    /// Layout Win7: două coloane, search, shutdown.
    /// Folosește WinUI 3 SystemBackdrop nativ (Acrylic/Mica) pentru transparență.
    /// </summary>
    public sealed partial class StartMenuWindow : Window
    {
        private readonly StartMenuModule _module;
        private readonly AppWindow _appWindow;
        private bool _isVisible;
        private bool _controlPanelExpanded;

        // Current effect type for backdrop tracking
        private EffectType _currentEffect = EffectType.None;

        public StartMenuWindow(StartMenuModule module)
        {
            this.InitializeComponent();
            _module = module;

            // Get AppWindow for borderless configuration
            var hwnd = WindowNative.GetWindowHandle(this);
            var windowId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(hwnd);
            _appWindow = AppWindow.GetFromWindowId(windowId);

            ConfigureBorderless();
            LoadContent();
        }

        private void ConfigureBorderless()
        {
            if (_appWindow.Presenter is OverlappedPresenter presenter)
            {
                presenter.SetBorderAndTitleBar(false, false);
                presenter.IsResizable = false;
                presenter.IsMaximizable = false;
                presenter.IsMinimizable = false;
                presenter.IsAlwaysOnTop = true;
            }

            _appWindow.Resize(new Windows.Graphics.SizeInt32(500, 600));
            this.Activated += OnActivated;
        }

        private void LoadContent()
        {
            // User info
            UserNameText.Text = Environment.UserName;

            // Populate Control Panel items
            ControlPanelList.ItemsSource = ControlPanelItems.GetAll();

            // Populate programs
            var allPrograms = ProgramDiscovery.GetAllPrograms();
            var pinned = allPrograms.Count > 8 ? allPrograms.GetRange(0, 8) : allPrograms;
            PinnedProgramsList.ItemsSource = pinned;

            // Recent is empty initially
            RecentProgramsList.ItemsSource = new List<ProgramEntry>();
        }

        /// <summary>
        /// Light dismiss.
        /// </summary>
        private void OnActivated(object sender, WindowActivatedEventArgs args)
        {
            if (args.WindowActivationState == WindowActivationState.Deactivated && _isVisible)
            {
                Hide();
            }
        }

        public void Toggle()
        {
            if (_isVisible)
                Hide();
            else
                ShowMenu();
        }

        public void ShowMenu()
        {
            PositionNearTaskbar();
            ApplyBackdrop();

            var hwnd = WindowNative.GetWindowHandle(this);
            StartMenuInterop.ShowWindow(hwnd, 5); // SW_SHOW
            StartMenuInterop.SetForegroundWindow(hwnd);

            // Reset state
            _controlPanelExpanded = false;
            ControlPanelSubMenu.Visibility = Visibility.Collapsed;
            ControlPanelChevron.Glyph = "\uE76C";
            SearchBox.Text = "";

            _isVisible = true;
            _module.IsMenuVisible = true;
            Debug.WriteLine("[StartMenuWindow] Shown.");
        }

        public void Hide()
        {
            var hwnd = WindowNative.GetWindowHandle(this);
            StartMenuInterop.ShowWindow(hwnd, 0); // SW_HIDE

            _isVisible = false;
            _module.IsMenuVisible = false;
            Debug.WriteLine("[StartMenuWindow] Hidden.");
        }

        // ================================================================
        // WinUI 3 Native Backdrop (using SystemBackdrop property)
        // ================================================================

        private void ApplyBackdrop()
        {
            var effect = _module.CurrentSettings.Effect;

            if (effect == EffectType.None)
            {
                RootGrid.Background = new SolidColorBrush(Windows.UI.Color.FromArgb(0xE6, 0x20, 0x20, 0x20));
                this.SystemBackdrop = null;
                _currentEffect = effect;
                Debug.WriteLine("[StartMenuWindow] Backdrop: None (opaque).");
                return;
            }

            // Semi-transparent XAML background so system backdrop shows through
            byte alpha = (byte)(_module.CurrentSettings.Opacity / 100.0 * 180);
            byte r = (byte)_module.CurrentSettings.TintR;
            byte g = (byte)_module.CurrentSettings.TintG;
            byte b = (byte)_module.CurrentSettings.TintB;
            RootGrid.Background = new SolidColorBrush(Windows.UI.Color.FromArgb(alpha, r, g, b));

            // Use WinUI 3's built-in SystemBackdrop property
            switch (effect)
            {
                case EffectType.Mica:
                    this.SystemBackdrop = new Microsoft.UI.Xaml.Media.MicaBackdrop { Kind = Microsoft.UI.Composition.SystemBackdrops.MicaKind.Base };
                    Debug.WriteLine("[StartMenuWindow] Backdrop: Mica.");
                    break;

                case EffectType.Glass:
                case EffectType.Blur:
                case EffectType.Acrylic:
                default:
                    this.SystemBackdrop = new DesktopAcrylicBackdrop();
                    Debug.WriteLine($"[StartMenuWindow] Backdrop: {effect} (DesktopAcrylic).");
                    break;
            }

            _currentEffect = effect;
        }

        // ================================================================
        // Positioning
        // ================================================================

        private void PositionNearTaskbar()
        {
            var taskbarHwnd = StartMenuInterop.FindWindow("Shell_TrayWnd", null);
            if (taskbarHwnd == IntPtr.Zero) return;

            StartMenuInterop.GetWindowRect(taskbarHwnd, out var taskbarRect);

            var abd = new StartMenuInterop.APPBARDATA
            {
                cbSize = System.Runtime.InteropServices.Marshal.SizeOf<StartMenuInterop.APPBARDATA>()
            };
            abd.hWnd = taskbarHwnd;
            StartMenuInterop.SHAppBarMessage(StartMenuInterop.ABM_GETTASKBARPOS, ref abd);

            int menuWidth = 500;
            int menuHeight = 600;
            int x, y;

            switch (abd.uEdge)
            {
                case StartMenuInterop.ABE_TOP:
                    x = taskbarRect.Left;
                    y = taskbarRect.Bottom;
                    break;
                case StartMenuInterop.ABE_LEFT:
                    x = taskbarRect.Right;
                    y = taskbarRect.Bottom - menuHeight;
                    break;
                case StartMenuInterop.ABE_RIGHT:
                    x = taskbarRect.Left - menuWidth;
                    y = taskbarRect.Bottom - menuHeight;
                    break;
                default: // ABE_BOTTOM
                    x = taskbarRect.Left;
                    y = taskbarRect.Top - menuHeight;
                    break;
            }

            _appWindow.Move(new Windows.Graphics.PointInt32(x, y));
        }

        // ================================================================
        // Event Handlers — Shell Links (right panel)
        // ================================================================

        private void ShellLink_Click(object sender, RoutedEventArgs e)
        {
            if (sender is not Button btn || btn.Tag is not string tag) return;
            Hide();

            switch (tag)
            {
                case "Documents": ShellLauncher.OpenDocuments(); break;
                case "Pictures": ShellLauncher.OpenPictures(); break;
                case "Music": ShellLauncher.OpenMusic(); break;
                case "Videos": ShellLauncher.OpenVideos(); break;
                case "Downloads": ShellLauncher.OpenDownloads(); break;
                case "Computer": ShellLauncher.OpenComputer(); break;
                case "Devices": ShellLauncher.OpenDevicesAndPrinters(); break;
                case "DefaultPrograms": ShellLauncher.OpenDefaultPrograms(); break;
            }
        }

        // ================================================================
        // Control Panel
        // ================================================================

        private void ControlPanel_Click(object sender, RoutedEventArgs e)
        {
            _controlPanelExpanded = !_controlPanelExpanded;
            ControlPanelSubMenu.Visibility = _controlPanelExpanded ? Visibility.Visible : Visibility.Collapsed;
            ControlPanelChevron.Glyph = _controlPanelExpanded ? "\uE70E" : "\uE76C";
        }

        private void ControlPanelItem_Click(object sender, ItemClickEventArgs e)
        {
            if (e.ClickedItem is ControlPanelItem item)
            {
                Hide();
                ShellLauncher.OpenControlPanelItem(item.ControlName);
            }
        }

        // ================================================================
        // Programs
        // ================================================================

        private void ProgramItem_Click(object sender, ItemClickEventArgs e)
        {
            if (e.ClickedItem is ProgramEntry program)
            {
                Hide();
                ShellLauncher.LaunchProgram(program.Path);
            }
        }

        // ================================================================
        // Search
        // ================================================================

        private void SearchBox_TextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
        {
            if (args.Reason == AutoSuggestionBoxTextChangeReason.UserInput)
            {
                var results = ProgramDiscovery.Search(sender.Text);
                PinnedProgramsList.ItemsSource = results;
            }
        }

        private void SearchBox_QuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
        {
            var results = ProgramDiscovery.Search(args.QueryText);
            if (results.Count > 0)
            {
                Hide();
                ShellLauncher.LaunchProgram(results[0].Path);
            }
        }

        // ================================================================
        // Shutdown
        // ================================================================

        private void Shutdown_Click(object sender, RoutedEventArgs e)
        {
            Hide();
            ShellLauncher.Shutdown();
        }

        private void ShutdownMenu_Click(object sender, RoutedEventArgs e)
        {
            var flyout = new MenuFlyout();

            var restart = new MenuFlyoutItem { Text = "Restart" };
            restart.Click += (_, _) => { Hide(); ShellLauncher.Restart(); };
            flyout.Items.Add(restart);

            var sleep = new MenuFlyoutItem { Text = "Sleep" };
            sleep.Click += (_, _) => { Hide(); ShellLauncher.Sleep(); };
            flyout.Items.Add(sleep);

            var lockItem = new MenuFlyoutItem { Text = "Lock" };
            lockItem.Click += (_, _) => { Hide(); ShellLauncher.Lock(); };
            flyout.Items.Add(lockItem);

            flyout.Items.Add(new MenuFlyoutSeparator());

            var logOff = new MenuFlyoutItem { Text = "Log off" };
            logOff.Click += (_, _) => { Hide(); ShellLauncher.LogOff(); };
            flyout.Items.Add(logOff);

            flyout.ShowAt(BtnShutdownMenu);
        }
    }
}
