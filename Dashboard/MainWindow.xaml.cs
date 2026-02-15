using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Windows.Graphics;
using Windows.UI;
using WinRT.Interop;

namespace CrystalFrame.Dashboard
{
    public sealed partial class MainWindow : Window
    {
        private MainViewModel _viewModel;
        private bool _isInitialized = false;
        private string _activePanel = "";
        private AppWindow _appWindow;
        private DetailWindow? _detailWindow;


        public MainWindow()
        {
            InitializeComponent();

            // Setup fereastră
            var hwnd     = WindowNative.GetWindowHandle(this);
            var windowId = Win32Interop.GetWindowIdFromWindow(hwnd);
            _appWindow   = AppWindow.GetFromWindowId(windowId);
            _appWindow.Resize(new SizeInt32(1, 1)); // placeholder; Loaded va seta dimensiunea reală

            // Blocat redimensionare manuală
            if (_appWindow.Presenter is OverlappedPresenter presenter)
            {
                presenter.IsResizable   = false;
                presenter.IsMaximizable = false;
            }

            // Icoană
            var iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "app.ico");
            if (System.IO.File.Exists(iconPath))
                _appWindow.SetIcon(iconPath);

            _viewModel = new MainViewModel();
            _viewModel.PropertyChanged += ViewModel_PropertyChanged;

            RootGrid.DataContext = _viewModel;
            RootGrid.Loaded += (s, e) => MeasureAndResize();

            this.Closed += OnWindowClosed;

            _ = InitializeAsync();
        }

        private void MeasureAndResize()
        {
            RootGrid.Measure(new Windows.Foundation.Size(double.PositiveInfinity, double.PositiveInfinity));
            int w = (int)Math.Ceiling(RootGrid.DesiredSize.Width);
            int h = (int)Math.Ceiling(RootGrid.DesiredSize.Height);
            if (w > 0 && h > 0)
                _appWindow.Resize(new SizeInt32(w, h + 34)); // +34 titlebar
        }

        private async void OnWindowClosed(object sender, WindowEventArgs e)
        {
            CloseDetailWindow();
            try   { await _viewModel.OnDashboardClosingAsync(); }
            catch (Exception ex) { Debug.WriteLine($"Error during close: {ex.Message}"); }
        }

        // ── Inițializare asincronă ────────────────────────────────────────────────

        private async Task InitializeAsync()
        {
            try
            {
                var success = await _viewModel.InitializeAsync();

                if (!success && !string.IsNullOrEmpty(_viewModel.ExtractionError))
                    await ShowExtractionErrorDialogAsync(_viewModel.ExtractionError);

                try
                {
                    CoreRunningToggle.IsOn  = _viewModel.CoreRunning;
                    RunAtStartupToggle.IsOn = _viewModel.RunAtStartup;

                    UpdateStatus();
                    UpdateCoreStatus();
                    UpdateNavHighlight();
                }
                catch (Exception uiEx)
                {
                    Debug.WriteLine($"UI update failed: {uiEx.Message}, continuing...");
                }

                _isInitialized = true;
                Debug.WriteLine($"[INIT] Done, _isInitialized={_isInitialized}");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Init failed: {ex.Message}\n{ex.StackTrace}");
                ConnectionStatusText.Text = $"✗ Init failed: {ex.Message}";
                _isInitialized = true;
            }
        }

        private async Task ShowExtractionErrorDialogAsync(string errorMessage)
        {
            if (this.Content.XamlRoot == null)
            {
                var tcs = new TaskCompletionSource<bool>();
                if (this.Content is FrameworkElement fe)
                    fe.Loaded += (s, e) => tcs.TrySetResult(true);
                else
                    tcs.TrySetResult(true);
                await tcs.Task;
            }

            var dialog = new ContentDialog
            {
                Title           = "Core Engine Extraction Failed",
                Content         = $"Could not extract the Core engine:\n\n{errorMessage}\n\nPlease try reinstalling CrystalFrame or check your antivirus settings.",
                CloseButtonText = "OK",
                XamlRoot        = this.Content.XamlRoot
            };
            await dialog.ShowAsync();
        }

        private void ViewModel_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                UpdateStatus();
                if (e.PropertyName == nameof(MainViewModel.CoreRunning))
                {
                    UpdateCoreStatus();
                    if (CoreRunningToggle.IsOn != _viewModel.CoreRunning)
                    {
                        _isInitialized = false;
                        CoreRunningToggle.IsOn = _viewModel.CoreRunning;
                        _isInitialized = true;
                    }
                }
            });
        }

        // ── Navigare ─────────────────────────────────────────────────────────────

        private void Nav_Taskbar_Click(object sender, RoutedEventArgs e) => TogglePanel("Taskbar");
        private void Nav_Start_Click(object sender, RoutedEventArgs e)   => TogglePanel("StartMenu");

        private void TogglePanel(string panelName)
        {
            if (_activePanel == panelName)
            {
                _activePanel = "";
                CloseDetailWindow();
            }
            else if (_detailWindow != null)
            {
                _activePanel = panelName;
                _detailWindow.SwitchPanel(panelName);
            }
            else
            {
                _activePanel = panelName;
                OpenDetailWindow(panelName);
            }

            UpdateNavHighlight();
        }

        private void OpenDetailWindow(string panelName)
        {
            _detailWindow = new DetailWindow(_viewModel, panelName);
            _detailWindow.PositionNextTo(
                _appWindow.Position.X + _appWindow.Size.Width,
                _appWindow.Position.Y);
            _detailWindow.Closed += OnDetailWindowClosed;
            _detailWindow.Activate();
        }

        private void OnDetailWindowClosed(object sender, WindowEventArgs e)
        {
            _detailWindow = null;
            _activePanel  = "";
            UpdateNavHighlight();
        }

        private void CloseDetailWindow()
        {
            if (_detailWindow != null)
            {
                var dw = _detailWindow;
                _detailWindow = null;
                _activePanel  = "";
                dw.Closed -= OnDetailWindowClosed;
                dw.Close();
            }
        }

        // ── Toggle handlers (MainWindow — solo Core + RunAtStartup) ──────────────

        private async void CoreRunning_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;
            try
            {
                await _viewModel.SetCoreRunningAsync(CoreRunningToggle.IsOn);
                UpdateCoreStatus();
            }
            catch (Exception ex) { Debug.WriteLine($"Failed to toggle Core: {ex.Message}"); }
        }

        private void RunAtStartup_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;
            _viewModel.RunAtStartup = RunAtStartupToggle.IsOn;
        }

        // ── Highlight nav button ──────────────────────────────────────────────────

        private void UpdateNavHighlight()
        {
            var accentBrush = new SolidColorBrush(Color.FromArgb(255, 0, 120, 212));

            bool isDark = Application.Current.RequestedTheme == ApplicationTheme.Dark;
            var inactiveBrush = isDark
                ? new SolidColorBrush(Color.FromArgb(255, 26, 26, 26))
                : new SolidColorBrush(Color.FromArgb(255, 240, 240, 240));

            NavTaskbar.Background   = _activePanel == "Taskbar"   ? accentBrush : inactiveBrush;
            NavStartMenu.Background = _activePanel == "StartMenu" ? accentBrush : inactiveBrush;
        }

        // ── Status helpers ────────────────────────────────────────────────────────

        private void UpdateStatus()
        {
            ConnectionStatusText.Text = _viewModel.ConnectionStatus;
        }

        private void UpdateCoreStatus()
        {
            if (_viewModel.CoreRunning)
            {
                CoreStatusDot.Fill    = new SolidColorBrush(Colors.LimeGreen);
                CoreStatusDetail.Text = "Running";
            }
            else
            {
                CoreStatusDot.Fill    = new SolidColorBrush(Colors.Gray);
                CoreStatusDetail.Text = "Stopped";
            }
        }
    }
}
