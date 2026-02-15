using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System;
using System.Diagnostics;
using Windows.Graphics;
using Windows.UI;
using WinRT.Interop;

namespace CrystalFrame.Dashboard
{
    public sealed partial class DetailWindow : Window
    {
        private readonly MainViewModel _viewModel;
        private bool _isInitialized = false;
        private AppWindow _appWindow;
        private bool _sizeSettled = false;

        public DetailWindow(MainViewModel viewModel, string panelName)
        {
            _viewModel = viewModel;
            InitializeComponent();

            // Setup AppWindow
            var hwnd     = WindowNative.GetWindowHandle(this);
            var windowId = Win32Interop.GetWindowIdFromWindow(hwnd);
            _appWindow   = AppWindow.GetFromWindowId(windowId);

            // Initial size — height will be corrected by RootBorder.SizeChanged
            _appWindow.Resize(new SizeInt32(340, 700));

            if (_appWindow.Presenter is OverlappedPresenter presenter)
            {
                presenter.IsResizable   = false;
                presenter.IsMaximizable = false;
                presenter.IsMinimizable = false;
            }

            SwitchPanel(panelName);
            Title = panelName == "Taskbar" ? "Taskbar Settings" : "Start Menu Settings";

            InitializeFromViewModel();

            _viewModel.PropertyChanged += ViewModel_PropertyChanged;
            this.Closed += (s, e) => _viewModel.PropertyChanged -= ViewModel_PropertyChanged;

            RootBorder.SizeChanged += RootBorder_SizeChanged;
        }

        // ── Auto-size via SizeChanged ─────────────────────────────────────────────

        private void RootBorder_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            if (_sizeSettled) return;
            int h = (int)Math.Ceiling(e.NewSize.Height);
            if (h > 50)
            {
                _sizeSettled = true;
                ResizeToContent(h);
            }
        }

        // Redimensionează fereastra la înălțimea conținutului, respectând work area
        // (fereastra nu depășește în jos limita superioară a taskbar-ului Windows).
        private void ResizeToContent(int contentHeight)
        {
            const int TitleBarHeight = 34;
            int totalHeight = contentHeight + TitleBarHeight;

            // Obținem work area = ecranul fără taskbar Windows
            var hwnd        = WindowNative.GetWindowHandle(this);
            var windowId    = Win32Interop.GetWindowIdFromWindow(hwnd);
            var displayArea = DisplayArea.GetFromWindowId(windowId, DisplayAreaFallback.Nearest);
            var workArea    = displayArea.WorkArea; // RectInt32

            // Limităm înălțimea la work area
            int finalHeight = Math.Min(totalHeight, workArea.Height);

            // Ajustăm Y: marginea inferioară a ferestrei ≤ marginea inferioară a work area
            int x             = _appWindow.Position.X;
            int y             = _appWindow.Position.Y;
            int workAreaBottom = workArea.Y + workArea.Height;

            if (y + finalHeight > workAreaBottom)
                y = workAreaBottom - finalHeight;
            if (y < workArea.Y)
                y = workArea.Y;

            _appWindow.Resize(new SizeInt32(340, finalHeight));
            _appWindow.Move(new PointInt32(x, y));
        }

        // ── Panel switching ───────────────────────────────────────────────────────

        public void SwitchPanel(string panelName)
        {
            _sizeSettled = false;
            TaskbarPanel.Visibility   = panelName == "Taskbar"   ? Visibility.Visible : Visibility.Collapsed;
            StartMenuPanel.Visibility = panelName == "StartMenu" ? Visibility.Visible : Visibility.Collapsed;
            Title = panelName == "Taskbar" ? "Taskbar Settings" : "Start Menu Settings";
        }

        // ── Positioning ───────────────────────────────────────────────────────────

        public void PositionNextTo(int x, int y) => _appWindow.Move(new PointInt32(x, y));

        // ── ViewModel PropertyChanged (background thread → UI thread) ─────────────

        private void ViewModel_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                if (e.PropertyName is nameof(MainViewModel.TaskbarFound) or nameof(MainViewModel.StartDetected))
                    UpdateStatus();
            });
        }

        // ── Initialize controls from viewmodel ────────────────────────────────────

        private void InitializeFromViewModel()
        {
            try
            {
                TaskbarOpacitySlider.Value  = _viewModel.TaskbarOpacity;
                TaskbarEnabledToggle.IsOn   = _viewModel.TaskbarEnabled;

                TaskbarColorRSlider.Value   = _viewModel.TaskbarColorR;
                TaskbarColorGSlider.Value   = _viewModel.TaskbarColorG;
                TaskbarColorBSlider.Value   = _viewModel.TaskbarColorB;

                StartOpacitySlider.Value    = _viewModel.StartOpacity;
                StartEnabledToggle.IsOn     = _viewModel.StartEnabled;

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

                UpdateOpacityText();
                UpdateTaskbarColorPreview();
                UpdateStartBgColorPreview();
                UpdateStartTextColorPreview();
                UpdateStatus();

                _isInitialized = true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[DetailWindow] InitializeFromViewModel failed: {ex.Message}");
                _isInitialized = true;
            }
        }

        // ── Status helpers ────────────────────────────────────────────────────────

        private void UpdateOpacityText()
        {
            TaskbarOpacityValue.Text = ((int)TaskbarOpacitySlider.Value).ToString();
            StartOpacityValue.Text   = ((int)StartOpacitySlider.Value).ToString();
        }

        private void UpdateStatus()
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

        // ── Toggle handlers ───────────────────────────────────────────────────────

        private async void TaskbarEnabled_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;
            try   { await _viewModel.SetTaskbarEnabledAsync(TaskbarEnabledToggle.IsOn); }
            catch (Exception ex) { Debug.WriteLine($"Failed to toggle taskbar: {ex.Message}"); }
        }

        private async void StartEnabled_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;
            try   { await _viewModel.SetStartEnabledAsync(StartEnabledToggle.IsOn); }
            catch (Exception ex) { Debug.WriteLine($"Failed to toggle start: {ex.Message}"); }
        }

        // ── Opacity sliders ───────────────────────────────────────────────────────

        private void TaskbarOpacity_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarOpacityValue.Text = value.ToString();
            if (!_isInitialized) return;
            try   { _viewModel.OnTaskbarOpacityChanged(value); }
            catch (Exception ex) { Debug.WriteLine($"[DetailWindow] TaskbarOpacity error: {ex.Message}"); }
        }

        private void StartOpacity_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartOpacityValue.Text = value.ToString();
            if (!_isInitialized) return;
            _viewModel.OnStartOpacityChanged(value);
        }

        // ── Taskbar color sliders ─────────────────────────────────────────────────

        private void TaskbarColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarColorRValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateTaskbarColorPreview();
            _viewModel.OnTaskbarColorChanged(value, (int)TaskbarColorGSlider.Value, (int)TaskbarColorBSlider.Value);
        }

        private void TaskbarColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarColorGValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateTaskbarColorPreview();
            _viewModel.OnTaskbarColorChanged((int)TaskbarColorRSlider.Value, value, (int)TaskbarColorBSlider.Value);
        }

        private void TaskbarColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            TaskbarColorBValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateTaskbarColorPreview();
            _viewModel.OnTaskbarColorChanged((int)TaskbarColorRSlider.Value, (int)TaskbarColorGSlider.Value, value);
        }

        // ── Start Menu background color sliders ───────────────────────────────────

        private void StartBgColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBgColorRValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateStartBgColorPreview();
            _viewModel.OnStartBgColorChanged(value, (int)StartBgColorGSlider.Value, (int)StartBgColorBSlider.Value);
        }

        private void StartBgColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBgColorGValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateStartBgColorPreview();
            _viewModel.OnStartBgColorChanged((int)StartBgColorRSlider.Value, value, (int)StartBgColorBSlider.Value);
        }

        private void StartBgColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartBgColorBValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateStartBgColorPreview();
            _viewModel.OnStartBgColorChanged((int)StartBgColorRSlider.Value, (int)StartBgColorGSlider.Value, value);
        }

        // ── Start Menu text color sliders ─────────────────────────────────────────

        private void StartTextColorR_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartTextColorRValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateStartTextColorPreview();
            _viewModel.OnStartTextColorChanged(value, (int)StartTextColorGSlider.Value, (int)StartTextColorBSlider.Value);
        }

        private void StartTextColorG_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartTextColorGValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateStartTextColorPreview();
            _viewModel.OnStartTextColorChanged((int)StartTextColorRSlider.Value, value, (int)StartTextColorBSlider.Value);
        }

        private void StartTextColorB_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            int value = (int)e.NewValue;
            StartTextColorBValue.Text = value.ToString();
            if (!_isInitialized) return;
            UpdateStartTextColorPreview();
            _viewModel.OnStartTextColorChanged((int)StartTextColorRSlider.Value, (int)StartTextColorGSlider.Value, value);
        }

        // ── Start Menu item checkboxes ────────────────────────────────────────────

        private void StartMenuItem_Changed(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;
            var checkbox = sender as CheckBox;
            if (checkbox == null) return;

            bool isChecked = checkbox.IsChecked ?? false;
            string itemName = "";

            if      (checkbox == StartShowControlPanel)  itemName = "ControlPanel";
            else if (checkbox == StartShowDeviceManager) itemName = "DeviceManager";
            else if (checkbox == StartShowInstalledApps) itemName = "InstalledApps";
            else if (checkbox == StartShowDocuments)     itemName = "Documents";
            else if (checkbox == StartShowPictures)      itemName = "Pictures";
            else if (checkbox == StartShowVideos)        itemName = "Videos";
            else if (checkbox == StartShowRecentFiles)   itemName = "RecentFiles";

            if (!string.IsNullOrEmpty(itemName))
            {
                _viewModel.OnStartMenuItemChanged(itemName, isChecked);
                Debug.WriteLine($"[StartMenuItem] {itemName} = {isChecked}");
            }
        }
    }
}
