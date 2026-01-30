using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Modulul care implementează efectul de transparență pe Taskbar.
    /// </summary>
    public class TaskbarModule : IModule
    {
        private const string ModuleName = "Taskbar Transparent";
        private readonly SettingsService _settingsService;
        private ModuleSettings _settings;
        private IntPtr _taskbarHandle = IntPtr.Zero;

        public string Name => ModuleName;
        public string Description => "Aplică efect de transparență/blur pe Taskbar-ul Windows 11.";
        public bool IsEnabled => _settings?.IsEnabled ?? false;

        public TaskbarModule(SettingsService settingsService)
        {
            _settingsService = settingsService;
        }

        public async Task InitializeAsync()
        {
            _settings = await _settingsService.LoadSettingsAsync<ModuleSettings>(Name);
            // Caută handle-ul Taskbar-ului o singură dată la inițializare
            _taskbarHandle = Win32Interop.FindWindow(Win32Interop.TaskbarClassName, null);
            if (_taskbarHandle == IntPtr.Zero)
            {
                Console.WriteLine("Avertisment: Taskbar-ul nu a fost găsit.");
            }
        }

        public async Task EnableAsync()
        {
            if (_taskbarHandle == IntPtr.Zero)
            {
                // Încearcă din nou să găsească Taskbar-ul
                _taskbarHandle = Win32Interop.FindWindow(Win32Interop.TaskbarClassName, null);
                if (_taskbarHandle == IntPtr.Zero)
                {
                    throw new InvalidOperationException("Nu s-a putut găsi Taskbar-ul pentru a aplica efectul.");
                }
            }

            // Aplică efectul de blur (similar cu Aero Glass/Mica)
            SetTaskbarAccent(Win32Interop.ACCENT_STATE.ACCENT_ENABLE_BLURBEHIND);
            
            _settings.IsEnabled = true;
            await SaveSettingsAsync();
        }

        public async Task DisableAsync()
        {
            if (_taskbarHandle != IntPtr.Zero)
            {
                // Dezactivează efectul de accent
                SetTaskbarAccent(Win32Interop.ACCENT_STATE.ACCENT_DISABLED);
            }

            _settings.IsEnabled = false;
            await SaveSettingsAsync();
        }

        public Task SaveSettingsAsync()
        {
            return _settingsService.SaveSettingsAsync(Name, _settings);
        }

        /// <summary>
        /// Funcție helper pentru a seta politica de accent a ferestrei.
        /// </summary>
        /// <param name="state">Starea de accent dorită.</param>
        private void SetTaskbarAccent(Win32Interop.ACCENT_STATE state)
        {
            var accent = new Win32Interop.ACCENT_POLICY
            {
                AccentState = state,
                AccentFlags = 2, // Flag-uri suplimentare (poate varia)
                GradientColor = 0,
                AnimationId = 0
            };

            int accentStructSize = Marshal.SizeOf(accent);
            IntPtr accentPtr = Marshal.AllocHGlobal(accentStructSize);
            Marshal.StructureToPtr(accent, accentPtr, false);

            var data = new Win32Interop.WINDOWCOMPOSITIONATTRIB_DATA
            {
                Attrib = Win32Interop.WINDOWCOMPOSITIONATTRIB.WCA_ACCENT_POLICY,
                SizeOfData = accentStructSize,
                Data = accentPtr
            };

            Win32Interop.SetWindowCompositionAttribute(_taskbarHandle, ref data);

            Marshal.FreeHGlobal(accentPtr);
        }
    }
}
