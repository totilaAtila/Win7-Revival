using System;
using System.Diagnostics;
using System.Security.Principal;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Win7Revival.App.Localization;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar;

namespace Win7Revival.App
{
    public sealed partial class MainWindow : Window
    {
        private readonly CoreService _coreService;
        private readonly SettingsService _settingsService;
        private TrayIconManager? _trayIconManager;
        private TaskbarModule? _taskbarModule;
        private bool _isInitializing = true;

        public MainWindow(CoreService coreService, SettingsService settingsService)
        {
            this.InitializeComponent();
            _coreService = coreService;
            _settingsService = settingsService;

            LanguageComboBox.SelectedIndex = 0;
            CheckAdminStatus();
            LoadLanguageAsync();
            LoadTaskbarModule();
            LoadAutoStartState();

            _isInitializing = false;
        }

        private async void LoadLanguageAsync()
        {
            var appSettings = await _settingsService.LoadSettingsAsync<AppSettings>("App");
            var lang = appSettings.Language == "Română" ? AppLanguage.Română : AppLanguage.English;
            Strings.SetLanguage(lang);

            LanguageComboBox.SelectedIndex = lang == AppLanguage.Română ? 1 : 0;
            ApplyLanguage();
        }

        /// <summary>
        /// Aplică toate string-urile localizate pe elementele UI.
        /// </summary>
        private void ApplyLanguage()
        {
            // Header
            HeaderTitle.Text = Strings.Get("AppTitle");
            HeaderSubtitle.Text = Strings.Get("AppSubtitle");
            HeaderLanguageLabel.Text = Strings.Get("Language");
            AdminWarning.Title = Strings.Get("AdminWarningTitle");
            AdminWarning.Message = Strings.Get("AdminWarningMessage");

            // Tab headers
            TaskbarTab.Header = Strings.Get("TabTaskbar");
            StartMenuTab.Header = Strings.Get("TabStartMenu");
            ThemeEngineTab.Header = Strings.Get("TabThemeEngine");
            GeneralTab.Header = Strings.Get("TabGeneral");
            HelpTab.Header = Strings.Get("TabHelp");

            // Taskbar module
            TaskbarTitle.Text = Strings.Get("TaskbarTitle");
            TaskbarDescription.Text = Strings.Get("TaskbarDescription");
            EffectTypeLabel.Text = Strings.Get("EffectType");
            EffectBlurItem.Content = Strings.Get("EffectBlur");
            EffectAcrylicItem.Content = Strings.Get("EffectAcrylic");
            EffectMicaItem.Content = Strings.Get("EffectMica");
            EffectGlassItem.Content = Strings.Get("EffectGlass");
            OpacityLabel.Text = Strings.Get("Opacity");
            ColorTintLabel.Text = Strings.Get("ColorTint");

            // Coming soon tabs
            StartMenuTitle.Text = Strings.Get("StartMenuTitle");
            StartMenuDescription.Text = Strings.Get("StartMenuDescription");
            ThemeEngineTitle.Text = Strings.Get("ThemeEngineTitle");
            ThemeEngineDescription.Text = Strings.Get("ThemeEngineDescription");

            // General
            GeneralLabel.Text = Strings.Get("General");
            AutoStartLabel.Text = Strings.Get("AutoStart");
            AutoStartDescription.Text = Strings.Get("AutoStartDescription");

            // Help tab
            HelpTitle.Text = Strings.Get("AppTitle");
            HelpAbout.Text = Strings.Get("HelpAbout");
            HelpModules.Text = Strings.Get("HelpModules");
            HelpSupport.Text = Strings.Get("HelpSupport");

            // Footer buttons
            ApplyButton.Content = Strings.Get("ApplyAndSave");
            ResetButton.Content = Strings.Get("Reset");
            MinimizeToTrayButton.Content = Strings.Get("MinimizeToTray");

            // Tray icon
            _trayIconManager?.ApplyLanguage();
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
                DiagnosticsText.Text = Strings.Get("DiagTaskbarNotDetected");
                MonitorInfoText.Text = Strings.Get("DiagMonitorsUnknown");
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
                MonitorInfoText.Text = Strings.Get("DiagMonitorsNone");
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
                await ShowErrorDialog($"{Strings.Get("ErrorToggleTaskbar")}: {ex.Message}");
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
                Title = Strings.Get("SettingsSavedTitle"),
                Content = Strings.Get("SettingsSavedMessage"),
                CloseButtonText = Strings.Get("OK"),
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

        private async void LanguageComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_isInitializing) return;

            var lang = LanguageComboBox.SelectedIndex == 1 ? AppLanguage.Română : AppLanguage.English;
            Strings.SetLanguage(lang);
            ApplyLanguage();

            // Persist language choice
            try
            {
                var appSettings = new AppSettings { Language = lang == AppLanguage.Română ? "Română" : "English" };
                await _settingsService.SaveSettingsAsync("App", appSettings);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[MainWindow] Failed to save language preference: {ex.Message}");
            }
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
                await ShowErrorDialog(Strings.Get("ErrorAutoStart"));
                _isInitializing = true;
                AutoStartToggle.IsOn = AutoStartService.IsEnabled();
                _isInitializing = false;
            }
        }

        private void MinimizeToTrayButton_Click(object sender, RoutedEventArgs e)
        {
            _trayIconManager?.MinimizeToTray();
        }

        private async Task ShowErrorDialog(string message)
        {
            try
            {
                var dialog = new ContentDialog
                {
                    Title = Strings.Get("ErrorTitle"),
                    Content = message,
                    CloseButtonText = Strings.Get("OK"),
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
