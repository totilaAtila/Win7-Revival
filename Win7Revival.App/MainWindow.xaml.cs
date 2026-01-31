using System;
using System.Diagnostics;
using System.Security.Principal;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar;

namespace Win7Revival.App
{
    public sealed partial class MainWindow : Window
    {
        private readonly CoreService _coreService;
        private TrayIconManager? _trayIconManager;
        private TaskbarModule? _taskbarModule;
        private bool _isInitializing = true;

        public MainWindow(CoreService coreService)
        {
            this.InitializeComponent();
            _coreService = coreService;

            LanguageComboBox.SelectedIndex = 0;
            CheckAdminStatus();
            LoadTaskbarModule();
            LoadAutoStartState();

            _isInitializing = false;
        }

        /// <summary>
        /// Injectează TrayIconManager din App.xaml.cs după construcție.
        /// </summary>
        public void SetTrayIconManager(TrayIconManager trayManager)
        {
            _trayIconManager = trayManager;
        }

        private void CheckAdminStatus()
        {
            try
            {
                using var identity = WindowsIdentity.GetCurrent();
                var principal = new WindowsPrincipal(identity);
                if (!principal.IsInRole(WindowsBuiltInRole.Administrator))
                {
                    AdminWarning.IsOpen = true;
                }
            }
            catch
            {
                AdminWarning.IsOpen = true;
            }
        }

        private void LoadTaskbarModule()
        {
            // Caută modulul Taskbar în lista de module înregistrate
            foreach (var module in _coreService.Modules)
            {
                if (module is TaskbarModule tm)
                {
                    _taskbarModule = tm;
                    break;
                }
            }

            if (_taskbarModule == null)
            {
                Debug.WriteLine("[MainWindow] TaskbarModule not found in CoreService.");
                return;
            }

            // Setează starea inițială a UI-ului din settings-urile modulului
            var settings = _taskbarModule.CurrentSettings;

            TaskbarToggle.IsOn = _taskbarModule.IsEnabled;
            TaskbarVersionBadge.Text = $"v{_taskbarModule.Version}";

            // Opacity
            OpacitySlider.Value = settings.Opacity;
            OpacityValueText.Text = $"{settings.Opacity}%";

            // Effect type
            EffectComboBox.SelectedIndex = settings.Effect switch
            {
                EffectType.Blur => 0,
                EffectType.Acrylic => 1,
                EffectType.Mica => 2,
                EffectType.Glass => 3,
                _ => 0
            };

            // Color tint
            TintRSlider.Value = settings.TintR;
            TintGSlider.Value = settings.TintG;
            TintBSlider.Value = settings.TintB;
            TintRValue.Text = settings.TintR.ToString();
            TintGValue.Text = settings.TintG.ToString();
            TintBValue.Text = settings.TintB.ToString();
            UpdateColorPreview();

            // Diagnostics
            UpdateDiagnostics();
        }

        private void UpdateDiagnostics()
        {
            if (_taskbarModule?.Detector == null)
            {
                DiagnosticsText.Text = "Taskbar: not detected";
                MonitorInfoText.Text = "Monitors: unknown";
                return;
            }

            var det = _taskbarModule.Detector;
            var primary = det.PrimaryHandle;
            var secondaryCount = det.SecondaryHandles.Count;
            var position = det.GetTaskbarPosition();
            var autoHide = det.IsAutoHideEnabled();

            DiagnosticsText.Text = $"Taskbar: 0x{primary:X} | Position: {position} | AutoHide: {autoHide} | Secondary: {secondaryCount}";

            var monitors = det.Monitors;
            if (monitors.Count > 0)
            {
                var parts = new string[monitors.Count];
                for (int i = 0; i < monitors.Count; i++)
                {
                    var m = monitors[i];
                    parts[i] = $"{m.Bounds.Width}x{m.Bounds.Height}{(m.IsPrimary ? "*" : "")}";
                }
                MonitorInfoText.Text = $"Monitors: {monitors.Count} — {string.Join(", ", parts)}";
            }
            else
            {
                MonitorInfoText.Text = "Monitors: none detected";
            }
        }

        private void LoadAutoStartState()
        {
            AutoStartToggle.IsOn = AutoStartService.IsEnabled();
        }

        // ================================================================
        // Event Handlers
        // ================================================================

        private async void TaskbarToggle_Toggled(object sender, RoutedEventArgs e)
        {
            if (_isInitializing || _taskbarModule == null) return;

            try
            {
                if (TaskbarToggle.IsOn && !_taskbarModule.IsEnabled)
                {
                    await _coreService.EnableModuleAsync(_taskbarModule.Name);
                    await _taskbarModule.SaveSettingsAsync();
                }
                else if (!TaskbarToggle.IsOn && _taskbarModule.IsEnabled)
                {
                    await _coreService.DisableModuleAsync(_taskbarModule.Name);
                    await _taskbarModule.SaveSettingsAsync();
                }
                UpdateDiagnostics();
            }
            catch (Exception ex)
            {
                await ShowErrorDialog($"Failed to toggle Taskbar Transparent: {ex.Message}");
                // Revert toggle fără re-trigger
                _isInitializing = true;
                TaskbarToggle.IsOn = _taskbarModule.IsEnabled;
                _isInitializing = false;
            }
        }

        private EffectType GetSelectedEffect() => EffectComboBox.SelectedIndex switch
        {
            0 => EffectType.Blur,
            1 => EffectType.Acrylic,
            2 => EffectType.Mica,
            3 => EffectType.Glass,
            _ => EffectType.Blur
        };

        private void ApplyCurrentSettings()
        {
            if (_isInitializing || _taskbarModule == null) return;
            _taskbarModule.UpdateSettings(
                (int)OpacitySlider.Value, GetSelectedEffect(),
                (byte)TintRSlider.Value, (byte)TintGSlider.Value, (byte)TintBSlider.Value);
        }

        private void UpdateColorPreview()
        {
            if (ColorPreviewBrush == null) return;
            ColorPreviewBrush.Color = Windows.UI.Color.FromArgb(255,
                (byte)TintRSlider.Value, (byte)TintGSlider.Value, (byte)TintBSlider.Value);
        }

        private void EffectComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            ApplyCurrentSettings();
        }

        private void OpacitySlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
        {
            if (OpacityValueText == null) return;
            OpacityValueText.Text = $"{(int)e.NewValue}%";
            ApplyCurrentSettings();
        }

        private void TintSlider_ValueChanged(object sender, RangeBaseValueChangedEventArgs e)
        {
            if (TintRValue == null) return;
            TintRValue.Text = ((int)TintRSlider.Value).ToString();
            TintGValue.Text = ((int)TintGSlider.Value).ToString();
            TintBValue.Text = ((int)TintBSlider.Value).ToString();
            UpdateColorPreview();
            ApplyCurrentSettings();
        }

        private async void ApplyButton_Click(object sender, RoutedEventArgs e)
        {
            if (_taskbarModule != null)
            {
                await _taskbarModule.SaveSettingsAsync();
            }

            var dialog = new ContentDialog
            {
                Title = "Settings Saved",
                Content = "Settings have been saved successfully.",
                CloseButtonText = "OK",
                XamlRoot = this.Content.XamlRoot
            };
            await dialog.ShowAsync();
        }

        private void ResetButton_Click(object sender, RoutedEventArgs e)
        {
            if (_taskbarModule == null) return;

            _isInitializing = true;
            OpacitySlider.Value = 80;
            OpacityValueText.Text = "80%";
            EffectComboBox.SelectedIndex = 0;
            TintRSlider.Value = 0;
            TintGSlider.Value = 0;
            TintBSlider.Value = 0;
            TintRValue.Text = "0";
            TintGValue.Text = "0";
            TintBValue.Text = "0";
            UpdateColorPreview();
            _isInitializing = false;

            _taskbarModule.UpdateSettings(80, EffectType.Blur, 0, 0, 0);
        }

        private async void AutoStartToggle_Toggled(object sender, RoutedEventArgs e)
        {
            if (_isInitializing) return;

            bool success;
            if (AutoStartToggle.IsOn)
            {
                success = AutoStartService.Enable();
            }
            else
            {
                success = AutoStartService.Disable();
            }

            if (!success)
            {
                await ShowErrorDialog("Failed to change auto-start setting. Check your permissions.");
                _isInitializing = true;
                AutoStartToggle.IsOn = AutoStartService.IsEnabled();
                _isInitializing = false;
            }
        }

        private void MinimizeToTrayButton_Click(object sender, RoutedEventArgs e)
        {
            _trayIconManager?.MinimizeToTray();
        }

        private void LanguageComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_isInitializing) return;
            if (LanguageComboBox.SelectedItem is ComboBoxItem item && item.Tag is string tag)
            {
                Debug.WriteLine($"[MainWindow] Language selected: {tag}");
                // TODO: hook into localization pipeline once available.
            }
        }

        private async Task ShowErrorDialog(string message)
        {
            try
            {
                var dialog = new ContentDialog
                {
                    Title = "Error",
                    Content = message,
                    CloseButtonText = "OK",
                    XamlRoot = this.Content.XamlRoot
                };
                await dialog.ShowAsync();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[MainWindow] Failed to show error dialog: {ex.Message}");
            }
        }
    }
}
