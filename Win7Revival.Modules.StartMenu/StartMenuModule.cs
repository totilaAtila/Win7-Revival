using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Threading;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;
using Win7Revival.Modules.StartMenu.Interop;
using Win7Revival.Modules.StartMenu.Models;

namespace Win7Revival.Modules.StartMenu
{
    /// <summary>
    /// Modulul Classic Start Menu.
    /// Interceptează Win key, gestionează afișarea/ascunderea meniului,
    /// și aplică efecte de transparență pe fereastra meniului.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public class StartMenuModule : IModule, IDisposable
    {
        private const string ModuleName = "Classic Start Menu";
        private readonly SettingsService _settingsService;
        private StartMenuSettings _settings = new();
        private WinKeyInterceptor? _interceptor;
        private bool _disposed;
        private bool _menuVisible;

        public event PropertyChangedEventHandler? PropertyChanged;

        /// <summary>
        /// Fired when the menu should toggle visibility.
        /// The App project handles this by showing/hiding StartMenuWindow.
        /// </summary>
        public event EventHandler? ToggleMenuRequested;

        public string Name => ModuleName;
        public string Description => "Meniu Start clasic în stilul Windows 7 cu acces la Control Panel, foldere și programe.";
        public string Version => "1.0.0";

        public bool IsEnabled => _settings.IsEnabled;

        public StartMenuSettings CurrentSettings => _settings;

        public bool IsMenuVisible
        {
            get => _menuVisible;
            set
            {
                if (_menuVisible != value)
                {
                    _menuVisible = value;
                    OnPropertyChanged(nameof(IsMenuVisible));
                }
            }
        }

        public StartMenuModule(SettingsService settingsService)
        {
            _settingsService = settingsService;
        }

        public async Task InitializeAsync(CancellationToken cancellationToken = default)
        {
            _settings = await _settingsService.LoadSettingsAsync<StartMenuSettings>(Name);
            _settings.Name = Name;

            // Defaults pe prima rulare
            if (_settings.Effect == EffectType.None && !_settings.IsEnabled)
            {
                _settings.Effect = EffectType.Blur;
                _settings.Opacity = 85;
            }

            Debug.WriteLine($"[StartMenuModule] Inițializat. InterceptWinKey={_settings.InterceptWinKey}");
        }

        public Task EnableAsync(CancellationToken cancellationToken = default)
        {
            // Always install interceptor (mouse hook for Start button click).
            // Keyboard hook (Win key) is controlled by InterceptWinKey setting.
            _interceptor?.Dispose();
            _interceptor = new WinKeyInterceptor();
            _interceptor.InterceptKeyboard = _settings.InterceptWinKey;
            _interceptor.WinKeyPressed += OnWinKeyPressed;
            _interceptor.Install();

            _settings.IsEnabled = true;
            OnPropertyChanged(nameof(IsEnabled));
            Debug.WriteLine("[StartMenuModule] Enabled.");
            return Task.CompletedTask;
        }

        public Task DisableAsync(CancellationToken cancellationToken = default)
        {
            _interceptor?.Dispose();
            _interceptor = null;

            // Ascunde meniul dacă e vizibil
            if (_menuVisible)
            {
                _menuVisible = false;
                OnPropertyChanged(nameof(IsMenuVisible));
            }

            _settings.IsEnabled = false;
            OnPropertyChanged(nameof(IsEnabled));
            Debug.WriteLine("[StartMenuModule] Disabled.");
            return Task.CompletedTask;
        }

        private void OnWinKeyPressed(object? sender, EventArgs e)
        {
            Debug.WriteLine("[StartMenuModule] Win key → toggle menu.");
            ToggleMenuRequested?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>
        /// Aplică efecte de transparență pe o fereastră (HWND).
        /// Apelat din App project pe StartMenuWindow.
        /// </summary>
        public void ApplyTransparency(IntPtr hwnd)
        {
            if (hwnd == IntPtr.Zero) return;

            int accentState = _settings.Effect switch
            {
                EffectType.Glass => StartMenuInterop.ACCENT_ENABLE_TRANSPARENTGRADIENT,
                EffectType.Blur => StartMenuInterop.ACCENT_ENABLE_BLURBEHIND,
                EffectType.Acrylic => StartMenuInterop.ACCENT_ENABLE_ACRYLICBLURBEHIND,
                EffectType.Mica => StartMenuInterop.ACCENT_ENABLE_HOSTBACKDROP,
                EffectType.None => StartMenuInterop.ACCENT_DISABLED,
                _ => StartMenuInterop.ACCENT_ENABLE_BLURBEHIND
            };

            int alpha = (int)(_settings.Opacity / 100.0 * 255);
            int gradientColor = (alpha << 24) | (_settings.TintB << 16) | (_settings.TintG << 8) | _settings.TintR;

            var accent = new StartMenuInterop.ACCENT_POLICY
            {
                AccentState = accentState,
                AccentFlags = 2,
                GradientColor = gradientColor,
                AnimationId = 0
            };

            int accentSize = Marshal.SizeOf(accent);
            IntPtr accentPtr = Marshal.AllocHGlobal(accentSize);
            try
            {
                Marshal.StructureToPtr(accent, accentPtr, false);
                var data = new StartMenuInterop.WINDOWCOMPOSITIONATTRIB_DATA
                {
                    Attrib = StartMenuInterop.WCA_ACCENT_POLICY,
                    Data = accentPtr,
                    SizeOfData = accentSize
                };
                StartMenuInterop.SetWindowCompositionAttribute(hwnd, ref data);
            }
            finally
            {
                Marshal.FreeHGlobal(accentPtr);
            }

            Debug.WriteLine($"[StartMenuModule] Transparency applied: Effect={_settings.Effect}, Opacity={_settings.Opacity}%");
        }

        /// <summary>
        /// Updates settings live from UI controls.
        /// </summary>
        public void UpdateSettings(int opacity, EffectType effect, bool interceptWinKey,
            byte tintR = 0, byte tintG = 0, byte tintB = 0)
        {
            _settings.Opacity = Math.Clamp(opacity, 0, 100);
            _settings.Effect = effect;
            _settings.InterceptWinKey = interceptWinKey;
            _settings.TintR = tintR;
            _settings.TintG = tintG;
            _settings.TintB = tintB;

            // Re-configure interceptor when keyboard setting changes
            if (_settings.IsEnabled && _interceptor != null)
            {
                bool needsReinstall = _interceptor.InterceptKeyboard != interceptWinKey;
                if (needsReinstall)
                {
                    _interceptor.Dispose();
                    _interceptor = new WinKeyInterceptor();
                    _interceptor.InterceptKeyboard = interceptWinKey;
                    _interceptor.WinKeyPressed += OnWinKeyPressed;
                    _interceptor.Install();
                }
            }

            OnPropertyChanged(nameof(CurrentSettings));
            Debug.WriteLine($"[StartMenuModule] Settings updated: Opacity={opacity}%, Effect={effect}, WinKey={interceptWinKey}");
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
                _interceptor?.Dispose();
                _interceptor = null;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[StartMenuModule] Eroare la cleanup: {ex.Message}");
            }
        }
    }
}
