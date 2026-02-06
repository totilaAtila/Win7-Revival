using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Windows.UI;

namespace CrystalFrame.Dashboard
{
    public sealed partial class MainWindow : Window
    {
        private MainViewModel _viewModel;
        private bool _isInitialized = false;

        public MainWindow()
        {
            InitializeComponent();

            _viewModel = new MainViewModel();
            _viewModel.PropertyChanged += ViewModel_PropertyChanged;

            // Don't stop Core when Dashboard closes
            this.Closed += (s, e) => _viewModel.OnDashboardClosing();

            _ = InitializeAsync();
        }

        private async Task InitializeAsync()
        {
            try
            {
                await _viewModel.InitializeAsync();

                // Update UI with loaded config
                TaskbarOpacitySlider.Value = _viewModel.TaskbarOpacity;
                StartOpacitySlider.Value = _viewModel.StartOpacity;
                TaskbarEnabledToggle.IsOn = _viewModel.TaskbarEnabled;
                StartEnabledToggle.IsOn = _viewModel.StartEnabled;
                CoreRunningToggle.IsOn = _viewModel.CoreRunning;

                UpdateOpacityText();
                UpdateStatus();
                UpdateCoreStatus();

                _isInitialized = true;

                Debug.WriteLine("Dashboard initialized successfully");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Dashboard initialization failed: {ex.Message}");
                ConnectionStatusText.Text = $"✗ Initialization failed: {ex.Message}";
            }
        }

        private void ViewModel_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            DispatcherQueue.TryEnqueue(() =>
            {
                UpdateStatus();
                
                if (e.PropertyName == nameof(MainViewModel.CoreRunning))
                {
                    UpdateCoreStatus();
                    // Sync toggle without re-triggering event
                    if (CoreRunningToggle.IsOn != _viewModel.CoreRunning)
                    {
                        _isInitialized = false;
                        CoreRunningToggle.IsOn = _viewModel.CoreRunning;
                        _isInitialized = true;
                    }
                }
            });
        }

        private async void CoreRunning_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;

            try
            {
                await _viewModel.SetCoreRunningAsync(CoreRunningToggle.IsOn);
                UpdateCoreStatus();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to toggle Core: {ex.Message}");
            }
        }

        private async void TaskbarEnabled_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;

            try
            {
                await _viewModel.SetTaskbarEnabledAsync(TaskbarEnabledToggle.IsOn);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to toggle taskbar: {ex.Message}");
            }
        }

        private void TaskbarOpacity_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            if (!_isInitialized) return;

            int value = (int)e.NewValue;
            TaskbarOpacityValue.Text = value.ToString();

            _viewModel.OnTaskbarOpacityChanged(value);
        }

        private async void StartEnabled_Toggled(object sender, RoutedEventArgs e)
        {
            if (!_isInitialized) return;

            try
            {
                await _viewModel.SetStartEnabledAsync(StartEnabledToggle.IsOn);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to toggle start: {ex.Message}");
            }
        }

        private void StartOpacity_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
        {
            if (!_isInitialized) return;

            int value = (int)e.NewValue;
            StartOpacityValue.Text = value.ToString();

            _viewModel.OnStartOpacityChanged(value);
        }

        private void UpdateOpacityText()
        {
            TaskbarOpacityValue.Text = ((int)TaskbarOpacitySlider.Value).ToString();
            StartOpacityValue.Text = ((int)StartOpacitySlider.Value).ToString();
        }

        private void UpdateStatus()
        {
            // Taskbar status
            if (_viewModel.TaskbarFound)
            {
                TaskbarStatusText.Text = "✓ Taskbar found";
                TaskbarStatusText.Foreground = new SolidColorBrush(Colors.Green);
            }
            else
            {
                TaskbarStatusText.Text = "⚠ Taskbar not detected";
                TaskbarStatusText.Foreground = new SolidColorBrush(Colors.Orange);
            }

            // Start status
            if (_viewModel.StartDetected)
            {
                StartStatusText.Text = "✓ Start menu detected";
                StartStatusText.Foreground = new SolidColorBrush(Colors.Green);
            }
            else
            {
                StartStatusText.Text = "⚠ Start menu not detected";
                StartStatusText.Foreground = new SolidColorBrush(Colors.Orange);
            }

            // Connection status
            ConnectionStatusText.Text = _viewModel.ConnectionStatus;
        }

        private void UpdateCoreStatus()
        {
            if (_viewModel.CoreRunning)
            {
                CoreStatusDot.Fill = new SolidColorBrush(Colors.LimeGreen);
                CoreStatusDetail.Text = "Running — overlay effects active";
            }
            else
            {
                CoreStatusDot.Fill = new SolidColorBrush(Colors.Gray);
                CoreStatusDetail.Text = "Stopped — no overlay effects";
            }
        }
    }
}
