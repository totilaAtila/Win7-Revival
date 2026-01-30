using System;
using System.ComponentModel;
using System.Diagnostics;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Modulul principal pentru transparența taskbar-ului.
    /// Orchestrează TaskbarDetector (discovery) și OverlayWindow (efecte).
    /// Suportă opacity configurabilă, multiple tipuri de efecte, multi-monitor.
    /// </summary>
    public class TaskbarModule : IModule, IDisposable
    {
        private const string ModuleName = "Taskbar Transparent";
        private readonly SettingsService _settingsService;
        private ModuleSettings _settings = new();
        private TaskbarDetector? _detector;
        private OverlayWindow? _overlay;
        private bool _disposed;

        public event PropertyChangedEventHandler? PropertyChanged;

        public string Name => ModuleName;
        public string Description => "Efecte de transparență, blur, acrylic sau mica pe Taskbar-ul Windows 11.";
        public string Version => "1.1.0";

        public bool IsEnabled => _settings.IsEnabled;

        /// <summary>
        /// Setările curente ale modulului (opacity, effect type).
        /// Expuse public pentru UI binding.
        /// </summary>
        public ModuleSettings CurrentSettings => _settings;

        /// <summary>
        /// Detectorul de taskbar — expus pentru UI diagnostics.
        /// </summary>
        public TaskbarDetector? Detector => _detector;

        public TaskbarModule(SettingsService settingsService)
        {
            _settingsService = settingsService;
        }

        public async Task InitializeAsync()
        {
            _settings = await _settingsService.LoadSettingsAsync<ModuleSettings>(Name);
            _settings.Name = Name;

            // Setează defaults dacă e prima rulare
            if (_settings.Effect == EffectType.None && !_settings.IsEnabled)
            {
                _settings.Effect = EffectType.Blur;
                _settings.Opacity = 80;
            }

            _detector = new TaskbarDetector();
            _detector.Detect();

            if (_detector.PrimaryHandle == IntPtr.Zero)
            {
                Debug.WriteLine("[TaskbarModule] Taskbar-ul nu a fost găsit la inițializare.");
            }
            else
            {
                Debug.WriteLine($"[TaskbarModule] Inițializat. Primary: 0x{_detector.PrimaryHandle:X}, " +
                    $"Secondary: {_detector.SecondaryHandles.Count}, " +
                    $"Monitors: {_detector.Monitors.Count}");
            }
        }

        public async Task EnableAsync()
        {
            if (_detector == null)
                throw new InvalidOperationException("Modulul nu a fost inițializat. Apelați InitializeAsync() mai întâi.");

            // Re-detectează dacă handle-ul e invalid
            if (_detector.PrimaryHandle == IntPtr.Zero || !_detector.IsHandleValid(_detector.PrimaryHandle))
            {
                _detector.Refresh();
                if (_detector.PrimaryHandle == IntPtr.Zero)
                {
                    throw new InvalidOperationException("Nu s-a putut găsi Taskbar-ul pentru a aplica efectul.");
                }
            }

            // Creează overlay-ul și aplică efectele
            _overlay?.Dispose();
            _overlay = new OverlayWindow(_detector, _settings);
            _overlay.Apply();

            _settings.IsEnabled = true;
            OnPropertyChanged(nameof(IsEnabled));
            await SaveSettingsAsync();
        }

        public async Task DisableAsync()
        {
            _overlay?.Remove();
            _overlay?.Dispose();
            _overlay = null;

            _settings.IsEnabled = false;
            OnPropertyChanged(nameof(IsEnabled));
            await SaveSettingsAsync();
        }

        /// <summary>
        /// Actualizează setările (opacity, effect) live, fără disable/enable.
        /// Apelat din UI la schimbarea slider-ului sau combobox-ului.
        /// </summary>
        public void UpdateSettings(int opacity, EffectType effect)
        {
            _settings.Opacity = Math.Clamp(opacity, 0, 100);
            _settings.Effect = effect;

            OnPropertyChanged(nameof(CurrentSettings));

            // Re-aplică efectele dacă modulul e activ
            _overlay?.UpdateSettings(_settings);

            Debug.WriteLine($"[TaskbarModule] Settings updated: Opacity={opacity}%, Effect={effect}");
        }

        public Task SaveSettingsAsync()
        {
            return _settingsService.SaveSettingsAsync(Name, _settings);
        }

        private void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            try
            {
                _overlay?.Dispose();
                _overlay = null;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[TaskbarModule] Eroare la cleanup: {ex.Message}");
            }
        }
    }
}
