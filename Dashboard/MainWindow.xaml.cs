using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Windows.Graphics;
using Windows.UI;
using WinRT.Interop;

namespace GlassBar.Dashboard
{
    public sealed partial class MainWindow : Window
    {
        private MainViewModel _viewModel;
        private bool _isInitialized       = false;  // core init guard
        private bool _isDetailInitialized = false;  // detail controls guard
        private string _activeTab         = "Taskbar";
        private AppWindow _appWindow;
        private TrayIconManager? _trayIconManager;
        private bool _exitRequested = false;
        private string? _updateUrl  = null;

        // ── Debounce tokens for opacity/color sliders (Task 2) ────────────────
        // Each slider group shares one CancellationTokenSource.  Moving a slider
        // cancels the previous pending send and schedules a new one 50 ms later,
        // so Core receives at most ~20 commands/s instead of hundreds.
        private CancellationTokenSource _cts_taskbarOpacity = new();
        private CancellationTokenSource _cts_taskbarColor   = new();
        private CancellationTokenSource _cts_startOpacity   = new();
        private CancellationTokenSource _cts_startBgColor   = new();
        private CancellationTokenSource _cts_startTextColor = new();
        private CancellationTokenSource _cts_startBorderColor = new();

        private const int SliderDebounceMs = 50;

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        public MainWindow(bool startHidden = false)
        {
            InitializeComponent();

            var hwnd     = WindowNative.GetWindowHandle(this);
            var windowId = Win32Interop.GetWindowIdFromWindow(hwnd);
            _appWindow   = AppWindow.GetFromWindowId(windowId);

            // Dimensiune fixă — NavigationView lateral necesită lățime mai mare
            _appWindow.Resize(new SizeInt32(500, 590));

            if (_appWindow.Presenter is OverlappedPresenter presenter)
            {
                presenter.IsResizable   = false;
                presenter.IsMaximizable = false;
            }

            var iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "app.ico");
            if (System.IO.File.Exists(iconPath))
                _appWindow.SetIcon(iconPath);

            _viewModel = new MainViewModel();
            _viewModel.PropertyChanged += ViewModel_PropertyChanged;

            RootGrid.DataContext = _viewModel;
            RootGrid.Loaded += (s, e) => { NavView.SelectedItem = NavTaskbar; };

            _appWindow.Closing += OnAppWindowClosing;

            _trayIconManager = new TrayIconManager(hwnd, iconPath,
                onShow: ShowFromTray,
                onExit: ExitFromTray);

            if (startHidden)
                DispatcherQueue.TryEnqueue(() => _appWindow.Hide());

            _ = InitializeAsync();
        }

        // ── Window lifecycle ──────────────────────────────────────────────────────

        private void OnAppWindowClosing(AppWindow sender, AppWindowClosingEventArgs args)
        {
            if (_exitRequested) return;
            args.Cancel = true;
            _trayIconManager?.EnsureAdded();
            _appWindow.Hide();
        }

        private void ShowFromTray()
        {
            _appWindow.Show();
            _appWindow.SetPresenter(AppWindowPresenterKind.Default);
            SetForegroundWindow(WindowNative.GetWindowHandle(this));
        }

        private async void ExitFromTray()
        {
            _exitRequested = true;
            _trayIconManager?.Remove();
            try   { await _viewModel.OnDashboardClosingAsync(forceShutdown: true); }
            catch (Exception ex) { Debug.WriteLine($"Error during exit: {ex.Message}"); }
            _appWindow.Destroy();
            Environment.Exit(0);
        }

        // ── Inițializare ──────────────────────────────────────────────────────────

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

                    UpdateCoreStatus();
                    UpdateConnectionStatus();
                    InitializeDetailFromViewModel();
                    UpdateTabHighlight();
                }
                catch (Exception uiEx)
                {
                    Debug.WriteLine($"UI update failed: {uiEx.Message}");
                }

                _isInitialized = true;

                // Check for updates silently in the background after init.
                _ = CheckForUpdateAsync();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Init failed: {ex.Message}\n{ex.StackTrace}");
                ConnectionStatusText.Text = $"✗ Init failed: {ex.Message}";
                _isInitialized = true;
            }
        }

        private async Task CheckForUpdateAsync()
        {
            try
            {
                var tag = await UpdateChecker.CheckForUpdateAsync();
                if (tag == null) return;

                _updateUrl = $"https://github.com/totilaAtila/GlassBar/releases/tag/{tag}";
                DispatcherQueue.TryEnqueue(() =>
                {
                    UpdateInfoBar.Message = $"Version {tag} is available.";
                    UpdateInfoBar.IsOpen  = true;
                });
            }
            catch { /* never surface update errors */ }
        }

        private void UpdateLink_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
        {
            if (_updateUrl != null)
                Process.Start(new ProcessStartInfo(_updateUrl) { UseShellExecute = true });
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
                Content         = $"Could not extract the Core engine:\n\n{errorMessage}\n\nPlease try reinstalling GlassBar or check your antivirus settings.",
                CloseButtonText = "OK",
                XamlRoot        = this.Content.XamlRoot
            };
            await dialog.ShowAsync();
        }

        private void ViewModel_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                UpdateConnectionStatus();
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
                if (e.PropertyName is nameof(MainViewModel.TaskbarFound) or nameof(MainViewModel.StartDetected))
                    UpdateDetailStatus();
            });
        }

        // ── NavigationView ────────────────────────────────────────────────────────

        private void NavView_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
        {
            if (args.SelectedItem is NavigationViewItem item)
                SwitchTab(item.Tag?.ToString() ?? "Taskbar");
        }

        private void SwitchTab(string tab)
        {
            _activeTab = tab;
            TaskbarPanel.Visibility   = tab == "Taskbar"   ? Visibility.Visible : Visibility.Collapsed;
            StartMenuPanel.Visibility = tab == "StartMenu" ? Visibility.Visible : Visibility.Collapsed;
        }

        private void UpdateTabHighlight() { /* NavigationView handles selection highlighting */ }

        // ── Core toggles ──────────────────────────────────────────────────────────

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

        // ── Status helpers ────────────────────────────────────────────────────────

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

        private void UpdateConnectionStatus()
        {
            ConnectionStatusText.Text = _viewModel.ConnectionStatus;
        }

        // ── Detail: initialize controls from viewmodel ────────────────────────────

        private void InitializeDetailFromViewModel()
        {
            _isDetailInitialized = false;
            try
            {
                TaskbarOpacitySlider.Value  = _viewModel.TaskbarOpacity;
                TaskbarEnabledToggle.IsOn   = _viewModel.TaskbarEnabled;
                TaskbarBlurToggle.IsOn      = _viewModel.TaskbarBlur;
                BlurAmountSlider.Value      = _viewModel.BlurAmount;
                BlurAmountValue.Text        = _viewModel.BlurAmount.ToString();
                TaskbarColorRSlider.Value   = _viewModel.TaskbarColorR;
                TaskbarColorGSlider.Value   = _viewModel.TaskbarColorG;
                TaskbarColorBSlider.Value   = _viewModel.TaskbarColorB;

                StartOpacitySlider.Value    = _viewModel.StartOpacity;
                StartEnabledToggle.IsOn     = _viewModel.StartEnabled;
                StartBlurToggle.IsOn        = _viewModel.StartBlur;
                StartBgColorRSlider.Value   = _viewModel.StartBgColorR;
                StartBgColorGSlider.Value   = _viewModel.StartBgColorG;
                StartBgColorBSlider.Value   = _viewModel.StartBgColorB;
                StartTextColorRSlider.Value = _viewModel.StartTextColorR;
                StartTextColorGSlider.Value = _viewModel.StartTextColorG;
                StartTextColorBSlider.Value = _viewModel.StartTextColorB;

                StartShowControlPanel.IsChecked  = _viewModel.StartShowControlPanel;
                StartShowDeviceManager.IsChecked = _viewModel.StartShowDeviceManager;
                StartShowInstalledApps.IsChecked = _viewModel.StartShowInstalledApps;
                StartShowDocuments.IsChecked     = _viewModel.StartShowDocuments;
                StartShowPictures.IsChecked      = _viewModel.StartShowPictures;
                StartShowVideos.IsChecked        = _viewModel.StartShowVideos;
                StartShowRecentFiles.IsChecked   = _viewModel.StartShowRecentFiles;

                StartBorderColorRSlider.Value = _viewModel.StartBorderColorR;
                StartBorderColorGSlider.Value = _viewModel.StartBorderColorG;
                StartBorderColorBSlider.Value = _viewModel.StartBorderColorB;

                StartMenuPinnedToggle.IsOn = _viewModel.StartMenuPinned;

                // Hotkey display
                UpdateHotkeyDisplay(_viewModel.LoadedHotkeyVk, _viewModel.LoadedHotkeyModifiers);

                UpdateOpacityText();
                UpdateTaskbarColorPreview();
                UpdateStartBgColorPreview();
                UpdateStartTextColorPreview();
                UpdateStartBorderColorPreview();
                UpdateDetailStatus();
            }
            catch (Exception ex) { Debug.WriteLine($"[MainWindow] InitializeDetailFromViewModel failed: {ex.Message}"); }
            finally { _isDetailInitialized = true; }
        }

        private void UpdateDetailStatus()
        {
            if (_viewModel.TaskbarFound)
            {
                TaskbarStatusText.Text       = "✓ Taskbar found";
                TaskbarStatusText.Foreground = new SolidColorBrush(Colors.LimeGreen);
            }
            else
            {
                TaskbarStatusText.Text       = "⚠ Taskbar not detected";
                TaskbarStatusText.Foreground = new SolidColorBrush(Colors.Orange);
            }

            if (_viewModel.StartDetected)
            {
                StartStatusText.Text       = "✓ Start menu detected";
                StartStatusText.Foreground = new SolidColorBrush(Colors.LimeGreen);
            }
            else
            {
                StartStatusText.Text       = "⚠ Start menu not detected";
                StartStatusText.Foreground = new SolidColorBrush(Colors.Orange);
            }
        }

        // ── Hotkey helpers ────────────────────────────────────────────────────────

        // Win32 modifier constants (must match RegisterHotKey)
        private const int MOD_ALT     = 0x0001;
        private const int MOD_CONTROL = 0x0002;
        private const int MOD_SHIFT   = 0x0004;
        private const int MOD_WIN     = 0x0008;

        private void UpdateHotkeyDisplay(int vk, int modifiers)
        {
            if (vk == 0)
            {
                HotkeyDisplayText.Text    = "None";
                HotkeyClearButton.IsEnabled = false;
                return;
            }

            var parts = new System.Collections.Generic.List<string>();
            if ((modifiers & MOD_CONTROL) != 0) parts.Add("Ctrl");
            if ((modifiers & MOD_ALT)     != 0) parts.Add("Alt");
            if ((modifiers & MOD_SHIFT)   != 0) parts.Add("Shift");
            if ((modifiers & MOD_WIN)     != 0) parts.Add("Win");
            parts.Add(((Windows.System.VirtualKey)vk).ToString());

            HotkeyDisplayText.Text      = string.Join("+", parts);
            HotkeyClearButton.IsEnabled = true;
        }

        private async void HotkeyRecord_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
        {
            var dialog = new ContentDialog
            {
                Title            = "Set Toggle Hotkey",
                CloseButtonText  = "Cancel",
                DefaultButton    = ContentDialogButton.None,
                XamlRoot         = RootGrid.XamlRoot
            };

            var instructionText = new TextBlock
            {
                Text        = "Press any key combination (e.g. Ctrl+Alt+G).\nRelease modifier keys last.",
                TextWrapping = Microsoft.UI.Xaml.TextWrapping.Wrap,
                Margin      = new Microsoft.UI.Xaml.Thickness(0, 0, 0, 8),
                FontSize    = 13
            };

            var capturedText = new TextBlock
            {
                Text      = "Waiting for key press...",
                FontSize  = 14,
                FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                HorizontalAlignment = Microsoft.UI.Xaml.HorizontalAlignment.Center
            };

            // Invisible TextBox to receive keyboard focus and key events
            var keyCapture = new Microsoft.UI.Xaml.Controls.TextBox
            {
                IsReadOnly = true,
                Opacity    = 0,
                Width      = 1,
                Height     = 1
            };

            int capturedVk  = 0;
            int capturedMod = 0;

            keyCapture.KeyDown += (s, args) =>
            {
                var key = args.Key;
                // Ignore standalone modifier presses
                if (key == Windows.System.VirtualKey.Control ||
                    key == Windows.System.VirtualKey.Shift    ||
                    key == Windows.System.VirtualKey.Menu     ||
                    key == Windows.System.VirtualKey.LeftWindows ||
                    key == Windows.System.VirtualKey.RightWindows)
                    return;

                int mods = 0;
                if (Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(Windows.System.VirtualKey.Control)
                    .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down)) mods |= MOD_CONTROL;
                if (Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(Windows.System.VirtualKey.Menu)
                    .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down))    mods |= MOD_ALT;
                if (Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(Windows.System.VirtualKey.Shift)
                    .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down))   mods |= MOD_SHIFT;
                if (Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(Windows.System.VirtualKey.LeftWindows)
                    .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down) ||
                    Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(Windows.System.VirtualKey.RightWindows)
                    .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down)) mods |= MOD_WIN;

                capturedVk  = (int)key;
                capturedMod = mods;
                args.Handled = true;

                var parts = new System.Collections.Generic.List<string>();
                if ((mods & MOD_CONTROL) != 0) parts.Add("Ctrl");
                if ((mods & MOD_ALT)     != 0) parts.Add("Alt");
                if ((mods & MOD_SHIFT)   != 0) parts.Add("Shift");
                if ((mods & MOD_WIN)     != 0) parts.Add("Win");
                parts.Add(key.ToString());
                capturedText.Text = string.Join("+", parts);
                dialog.PrimaryButtonText = "Apply";
            };

            var panel = new StackPanel { Spacing = 4 };
            panel.Children.Add(instructionText);
            panel.Children.Add(capturedText);
            panel.Children.Add(keyCapture);
            dialog.Content = panel;

            dialog.Opened += (s, args) => keyCapture.Focus(Microsoft.UI.Xaml.FocusState.Programmatic);

            var result = await dialog.ShowAsync();
            if (result == ContentDialogResult.Primary && capturedVk != 0)
            {
                _viewModel.ApplyHotkey(capturedVk, capturedMod);
                UpdateHotkeyDisplay(capturedVk, capturedMod);
            }
        }

        private void HotkeyClear_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
        {
            _viewModel.ClearHotkey();
            UpdateHotkeyDisplay(0, 0);
        }

        private void UpdateOpacityText()
        {
            TaskbarOpacityValue.Text = ((int)TaskbarOpacitySlider.Value).ToString();
            StartOpacityValue.Text   = ((int)StartOpacitySlider.Value).ToString();
        }

        private void UpdateTaskbarColorPreview()
        {
            TaskbarColorPreview.Background = new SolidColorBrush(Color.FromArgb(255,
                (byte)(int)TaskbarColorRSlider.Value,
                (byte)(int)TaskbarColorGSlider.Value,
                (byte)(int)TaskbarColorBSlider.Value));
        }

        private void UpdateStartBgColorPreview()
        {
            StartBgColorPreview.Background = new SolidColorBrush(Color.FromArgb(255,
                (byte)(int)StartBgColorRSlider.Value,
                (byte)(int)StartBgColorGSlider.Value,
                (byte)(int)StartBgColorBSlider.Value));
        }

        private void UpdateStartTextColorPreview()
        {
            StartTextColorPreview.Background = new SolidColorBrush(Color.FromArgb(255,
                (byte)(int)StartTextColorRSlider.Value,
                (byte)(int)StartTextColorGSlider.Value,
                (byte)(int)StartTextColorBSlider.Value));
        }

        private void UpdateStartBorderColorPreview()
        {
            StartBorderColorPreview.Background = new SolidColorBrush(Color.FromArgb(255,
                (byte)(int)StartBorderColorRSlider.Value,
                (byte)(int)StartBorderColorGSlider.Value,
                (byte)(int)StartBorderColorBSlider.Value));
        }

        // ── Taskbar handlers ──────────────────────────────────────────────────────

        private async void TaskbarEnabled_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            try   { await _viewModel.SetTaskbarEnabledAsync(TaskbarEnabledToggle.IsOn); }
            catch (Exception ex) { Debug.WriteLine($"Failed to toggle taskbar: {ex.Message}"); }
        }

        private void TaskbarBlur_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.OnTaskbarBlurChanged(TaskbarBlurToggle.IsOn);
        }

        private CancellationTokenSource _cts_blurAmount = new();

        private void BlurAmount_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            BlurAmountValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            DebounceSlider(ref _cts_blurAmount, () => _viewModel.BlurAmount = value);
        }

        // ── Generic debounce helper ───────────────────────────────────────────
        // Cancels any in-flight delay for the given CTS, then waits SliderDebounceMs
        // before invoking |action|.  If the slider moves again within the window
        // the previous Task is cancelled and a fresh one is started.
        //
        // NOTE: C# async methods cannot have ref parameters, so the helper is
        // synchronous — it cancels/replaces the CTS and fires an async local
        // function (which captures the token by value, not by ref).
        private void DebounceSlider(ref CancellationTokenSource cts, Action action)
        {
            cts.Cancel();
            cts = new CancellationTokenSource();
            var token = cts.Token;
            var captured = action;

            async Task RunDebounced()
            {
                try
                {
                    await Task.Delay(SliderDebounceMs, token);
                    if (!token.IsCancellationRequested) captured();
                }
                catch (TaskCanceledException) { /* slider moved again — ignore */ }
            }

            _ = RunDebounced();
        }

        private void TaskbarOpacity_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarOpacityValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            DebounceSlider(ref _cts_taskbarOpacity, () => _viewModel.OnTaskbarOpacityChanged(value));
        }

        private void TaskbarColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarColorRValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateTaskbarColorPreview();
            DebounceSlider(ref _cts_taskbarColor, () =>
                _viewModel.OnTaskbarColorChanged(value,
                    (int)TaskbarColorGSlider.Value, (int)TaskbarColorBSlider.Value));
        }

        private void TaskbarColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarColorGValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateTaskbarColorPreview();
            DebounceSlider(ref _cts_taskbarColor, () =>
                _viewModel.OnTaskbarColorChanged(
                    (int)TaskbarColorRSlider.Value, value, (int)TaskbarColorBSlider.Value));
        }

        private void TaskbarColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarColorBValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateTaskbarColorPreview();
            DebounceSlider(ref _cts_taskbarColor, () =>
                _viewModel.OnTaskbarColorChanged(
                    (int)TaskbarColorRSlider.Value, (int)TaskbarColorGSlider.Value, value));
        }

        // ── Start Menu handlers ───────────────────────────────────────────────────

        private async void StartEnabled_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            try   { await _viewModel.SetStartEnabledAsync(StartEnabledToggle.IsOn); }
            catch (Exception ex) { Debug.WriteLine($"Failed to toggle start: {ex.Message}"); }
        }

        private void StartBlur_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.OnStartBlurChanged(StartBlurToggle.IsOn);
        }

        private void StartOpacity_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartOpacityValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            DebounceSlider(ref _cts_startOpacity, () => _viewModel.OnStartOpacityChanged(value));
        }

        private void StartBgColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBgColorRValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartBgColorPreview();
            DebounceSlider(ref _cts_startBgColor, () =>
                _viewModel.OnStartBgColorChanged(value,
                    (int)StartBgColorGSlider.Value, (int)StartBgColorBSlider.Value));
        }

        private void StartBgColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBgColorGValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartBgColorPreview();
            DebounceSlider(ref _cts_startBgColor, () =>
                _viewModel.OnStartBgColorChanged(
                    (int)StartBgColorRSlider.Value, value, (int)StartBgColorBSlider.Value));
        }

        private void StartBgColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBgColorBValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartBgColorPreview();
            DebounceSlider(ref _cts_startBgColor, () =>
                _viewModel.OnStartBgColorChanged(
                    (int)StartBgColorRSlider.Value, (int)StartBgColorGSlider.Value, value));
        }

        private void StartTextColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartTextColorRValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartTextColorPreview();
            DebounceSlider(ref _cts_startTextColor, () =>
                _viewModel.OnStartTextColorChanged(value,
                    (int)StartTextColorGSlider.Value, (int)StartTextColorBSlider.Value));
        }

        private void StartTextColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartTextColorGValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartTextColorPreview();
            DebounceSlider(ref _cts_startTextColor, () =>
                _viewModel.OnStartTextColorChanged(
                    (int)StartTextColorRSlider.Value, value, (int)StartTextColorBSlider.Value));
        }

        private void StartTextColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartTextColorBValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartTextColorPreview();
            DebounceSlider(ref _cts_startTextColor, () =>
                _viewModel.OnStartTextColorChanged(
                    (int)StartTextColorRSlider.Value, (int)StartTextColorGSlider.Value, value));
        }

        private void StartMenuItem_Changed(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            if (sender is not CheckBox checkbox) return;

            bool isChecked = checkbox.IsChecked ?? false;
            string itemName = checkbox.Name switch
            {
                nameof(StartShowControlPanel)  => "ControlPanel",
                nameof(StartShowDeviceManager) => "DeviceManager",
                nameof(StartShowInstalledApps) => "InstalledApps",
                nameof(StartShowDocuments)     => "Documents",
                nameof(StartShowPictures)      => "Pictures",
                nameof(StartShowVideos)        => "Videos",
                nameof(StartShowRecentFiles)   => "RecentFiles",
                _                              => ""
            };

            if (!string.IsNullOrEmpty(itemName))
                _viewModel.OnStartMenuItemChanged(itemName, isChecked);
        }

        private void StartMenuPinned_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.OnStartMenuPinnedChanged(StartMenuPinnedToggle.IsOn);
        }

        private void StartBorderColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBorderColorRValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartBorderColorPreview();
            DebounceSlider(ref _cts_startBorderColor, () =>
                _viewModel.OnStartBorderColorChanged(value,
                    (int)StartBorderColorGSlider.Value, (int)StartBorderColorBSlider.Value));
        }

        private void StartBorderColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBorderColorGValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartBorderColorPreview();
            DebounceSlider(ref _cts_startBorderColor, () =>
                _viewModel.OnStartBorderColorChanged(
                    (int)StartBorderColorRSlider.Value, value, (int)StartBorderColorBSlider.Value));
        }

        private void StartBorderColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBorderColorBValue.Text = value.ToString();
            if (!_isDetailInitialized) return;
            UpdateStartBorderColorPreview();
            DebounceSlider(ref _cts_startBorderColor, () =>
                _viewModel.OnStartBorderColorChanged(
                    (int)StartBorderColorRSlider.Value, (int)StartBorderColorGSlider.Value, value));
        }

        // ── Theme presets ─────────────────────────────────────────────────────────

        private void PresetClassicWin7_Click(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.ApplyPreset("ClassicWin7");
            SyncSlidersFromViewModel();
        }

        private void PresetAeroGlass_Click(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.ApplyPreset("AeroGlass");
            SyncSlidersFromViewModel();
        }

        private void PresetDark_Click(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.ApplyPreset("Dark");
            SyncSlidersFromViewModel();
        }

        private void SyncSlidersFromViewModel()
        {
            _isDetailInitialized = false;
            try
            {
                StartOpacitySlider.Value      = _viewModel.StartOpacity;
                StartBlurToggle.IsOn          = _viewModel.StartBlur;
                StartBgColorRSlider.Value     = _viewModel.StartBgColorR;
                StartBgColorGSlider.Value     = _viewModel.StartBgColorG;
                StartBgColorBSlider.Value     = _viewModel.StartBgColorB;
                StartTextColorRSlider.Value   = _viewModel.StartTextColorR;
                StartTextColorGSlider.Value   = _viewModel.StartTextColorG;
                StartTextColorBSlider.Value   = _viewModel.StartTextColorB;
                StartBorderColorRSlider.Value = _viewModel.StartBorderColorR;
                StartBorderColorGSlider.Value = _viewModel.StartBorderColorG;
                StartBorderColorBSlider.Value = _viewModel.StartBorderColorB;
                UpdateOpacityText();
                UpdateStartBgColorPreview();
                UpdateStartTextColorPreview();
                UpdateStartBorderColorPreview();
            }
            finally { _isDetailInitialized = true; }
        }

        // ── Global theme presets (Taskbar panel) ──────────────────────────────────

        private void TaskbarPresetWin7Aero_Click(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.ApplyGlobalTheme("Win7Aero");
            SyncAllSlidersFromViewModel();
        }

        private void TaskbarPresetDark_Click(object sender, RoutedEventArgs e)
        {
            if (!_isDetailInitialized) return;
            _viewModel.ApplyGlobalTheme("Dark");
            SyncAllSlidersFromViewModel();
        }

        /// <summary>
        /// Syncs all UI sliders (Taskbar + Start Menu) from the ViewModel after a global
        /// theme is applied. Suppresses ValueChanged events during the update via the
        /// _isDetailInitialized guard.
        /// </summary>
        private void SyncAllSlidersFromViewModel()
        {
            _isDetailInitialized = false;
            try
            {
                // Overlay enable toggles
                TaskbarEnabledToggle.IsOn = _viewModel.TaskbarEnabled;
                StartEnabledToggle.IsOn   = _viewModel.StartEnabled;
                // Taskbar
                TaskbarOpacitySlider.Value = _viewModel.TaskbarOpacity;
                TaskbarColorRSlider.Value  = _viewModel.TaskbarColorR;
                TaskbarColorGSlider.Value  = _viewModel.TaskbarColorG;
                TaskbarColorBSlider.Value  = _viewModel.TaskbarColorB;
                // Start Menu
                StartOpacitySlider.Value      = _viewModel.StartOpacity;
                StartBlurToggle.IsOn          = _viewModel.StartBlur;
                StartBgColorRSlider.Value     = _viewModel.StartBgColorR;
                StartBgColorGSlider.Value     = _viewModel.StartBgColorG;
                StartBgColorBSlider.Value     = _viewModel.StartBgColorB;
                StartTextColorRSlider.Value   = _viewModel.StartTextColorR;
                StartTextColorGSlider.Value   = _viewModel.StartTextColorG;
                StartTextColorBSlider.Value   = _viewModel.StartTextColorB;
                StartBorderColorRSlider.Value = _viewModel.StartBorderColorR;
                StartBorderColorGSlider.Value = _viewModel.StartBorderColorG;
                StartBorderColorBSlider.Value = _viewModel.StartBorderColorB;
                UpdateOpacityText();
                UpdateTaskbarColorPreview();
                UpdateStartBgColorPreview();
                UpdateStartTextColorPreview();
                UpdateStartBorderColorPreview();
            }
            finally { _isDetailInitialized = true; }
        }
    }
}
