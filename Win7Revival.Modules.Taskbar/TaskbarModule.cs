using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Modulul care implementează efectul de transparență pe Taskbar.
    /// Implementează IDisposable pentru cleanup corect al resurselor Win32.
    /// </summary>
    public class TaskbarModule : IModule, IDisposable
    {
        private const string ModuleName = "Taskbar Transparent";
        private readonly SettingsService _settingsService;
        private ModuleSettings _settings = new();
        private IntPtr _taskbarHandle = IntPtr.Zero;
        private bool _disposed;

        public event PropertyChangedEventHandler? PropertyChanged;

        public string Name => ModuleName;
        public string Description => "Aplică efect de transparență/blur pe Taskbar-ul Windows 11.";

        public bool IsEnabled => _settings.IsEnabled;

        public TaskbarModule(SettingsService settingsService)
        {
            _settingsService = settingsService;
        }

        public async Task InitializeAsync()
        {
            _settings = await _settingsService.LoadSettingsAsync<ModuleSettings>(Name);
            _settings.Name = Name;
            _taskbarHandle = Win32Interop.FindWindow(Win32Interop.TaskbarClassName, null);
            if (_taskbarHandle == IntPtr.Zero)
            {
                Debug.WriteLine("[TaskbarModule] Avertisment: Taskbar-ul nu a fost găsit.");
            }
        }

        public async Task EnableAsync()
        {
            if (_taskbarHandle == IntPtr.Zero)
            {
                _taskbarHandle = Win32Interop.FindWindow(Win32Interop.TaskbarClassName, null);
                if (_taskbarHandle == IntPtr.Zero)
                {
                    throw new InvalidOperationException("Nu s-a putut găsi Taskbar-ul pentru a aplica efectul.");
                }
            }

            SetTaskbarAccent(Win32Interop.ACCENT_STATE.ACCENT_ENABLE_BLURBEHIND);

            _settings.IsEnabled = true;
            OnPropertyChanged(nameof(IsEnabled));
            await SaveSettingsAsync();
        }

        public async Task DisableAsync()
        {
            if (_taskbarHandle != IntPtr.Zero)
            {
                SetTaskbarAccent(Win32Interop.ACCENT_STATE.ACCENT_DISABLED);
            }

            _settings.IsEnabled = false;
            OnPropertyChanged(nameof(IsEnabled));
            await SaveSettingsAsync();
        }

        public Task SaveSettingsAsync()
        {
            return _settingsService.SaveSettingsAsync(Name, _settings);
        }

        private void SetTaskbarAccent(Win32Interop.ACCENT_STATE state)
        {
            var accent = new Win32Interop.ACCENT_POLICY
            {
                AccentState = state,
                AccentFlags = 2,
                GradientColor = 0,
                AnimationId = 0
            };

            int accentStructSize = Marshal.SizeOf(accent);
            IntPtr accentPtr = Marshal.AllocHGlobal(accentStructSize);
            try
            {
                Marshal.StructureToPtr(accent, accentPtr, false);

                var data = new Win32Interop.WINDOWCOMPOSITIONATTRIB_DATA
                {
                    Attrib = Win32Interop.WINDOWCOMPOSITIONATTRIB.WCA_ACCENT_POLICY,
                    Data = accentPtr,
                    SizeOfData = accentStructSize
                };

                Win32Interop.SetWindowCompositionAttribute(_taskbarHandle, ref data);
            }
            finally
            {
                Marshal.FreeHGlobal(accentPtr);
            }
        }

        private void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            if (_taskbarHandle != IntPtr.Zero && _settings.IsEnabled)
            {
                try
                {
                    SetTaskbarAccent(Win32Interop.ACCENT_STATE.ACCENT_DISABLED);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[TaskbarModule] Eroare la cleanup: {ex.Message}");
                }
            }
        }
    }
}
