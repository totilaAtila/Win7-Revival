using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Threading;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Models;
using Win7Revival.Core.Services;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Modulul principal pentru transparența taskbar-ului.
    /// Orchestrează TaskbarDetector (discovery) și OverlayWindow (efecte).
    /// Suportă opacity configurabilă, multiple tipuri de efecte, multi-monitor.
    /// Resilient la Explorer.exe restart via TaskbarCreated message.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public class TaskbarModule : IModule, IDisposable
    {
        private const string ModuleName = "Taskbar Transparent";
        private readonly SettingsService _settingsService;
        private ModuleSettings _settings = new();
        private TaskbarDetector? _detector;
        private OverlayWindow? _overlay;
        private bool _disposed;

        // Explorer restart monitoring
        private Thread? _messageThread;
        private IntPtr _messageWindowHandle;
        private uint _taskbarCreatedMessageId;
        private Win32Interop.WndProc? _wndProcDelegate; // prevent GC collection

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

        public async Task InitializeAsync(CancellationToken cancellationToken = default)
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

        public Task EnableAsync(CancellationToken cancellationToken = default)
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

            StartExplorerMonitor();

            _settings.IsEnabled = true;
            OnPropertyChanged(nameof(IsEnabled));
            return Task.CompletedTask;
        }

        public Task DisableAsync(CancellationToken cancellationToken = default)
        {
            StopExplorerMonitor();

            _overlay?.Remove();
            _overlay?.Dispose();
            _overlay = null;

            _settings.IsEnabled = false;
            OnPropertyChanged(nameof(IsEnabled));
            return Task.CompletedTask;
        }

        // ================================================================
        // Explorer Restart Resilience
        // ================================================================

        private void StartExplorerMonitor()
        {
            if (_messageThread != null) return;

            _taskbarCreatedMessageId = Win32Interop.RegisterWindowMessage("TaskbarCreated");
            if (_taskbarCreatedMessageId == 0)
            {
                Debug.WriteLine("[TaskbarModule] Failed to register TaskbarCreated message.");
                return;
            }

            _messageThread = new Thread(ExplorerMonitorLoop)
            {
                Name = "Win7Revival.ExplorerMonitor",
                IsBackground = true
            };
            _messageThread.SetApartmentState(ApartmentState.STA);
            _messageThread.Start();
        }

        private void StopExplorerMonitor()
        {
            if (_messageThread == null) return;

            if (_messageWindowHandle != IntPtr.Zero)
            {
                Win32Interop.PostMessage(_messageWindowHandle, Win32Interop.WM_QUIT, IntPtr.Zero, IntPtr.Zero);
            }

            _messageThread.Join(2000);
            _messageThread = null;
            _messageWindowHandle = IntPtr.Zero;
        }

        private void ExplorerMonitorLoop()
        {
            const string className = "Win7Revival_ExplorerMonitor";

            _wndProcDelegate = (IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam) =>
            {
                if (msg == _taskbarCreatedMessageId)
                {
                    Debug.WriteLine("[TaskbarModule] TaskbarCreated received — Explorer restarted.");
                    OnExplorerRestarted();
                    return IntPtr.Zero;
                }
                return Win32Interop.DefWindowProc(hwnd, msg, wParam, lParam);
            };

            var wndClass = new Win32Interop.WNDCLASS
            {
                lpfnWndProc = _wndProcDelegate,
                hInstance = Win32Interop.GetModuleHandle(null),
                lpszClassName = className
            };

            var atom = Win32Interop.RegisterClass(ref wndClass);
            if (atom == 0)
            {
                Debug.WriteLine("[TaskbarModule] Failed to register window class for Explorer monitor.");
                return;
            }

            _messageWindowHandle = Win32Interop.CreateWindowEx(
                0, className, "Win7Revival Explorer Monitor",
                0, 0, 0, 0, 0,
                Win32Interop.HWND_MESSAGE, IntPtr.Zero,
                wndClass.hInstance, IntPtr.Zero);

            if (_messageWindowHandle == IntPtr.Zero)
            {
                Debug.WriteLine("[TaskbarModule] Failed to create message-only window.");
                return;
            }

            Debug.WriteLine($"[TaskbarModule] Explorer monitor started, HWND=0x{_messageWindowHandle:X}");

            while (Win32Interop.GetMessage(out var msg, IntPtr.Zero, 0, 0))
            {
                Win32Interop.TranslateMessage(ref msg);
                Win32Interop.DispatchMessage(ref msg);
            }

            Win32Interop.DestroyWindow(_messageWindowHandle);
            Debug.WriteLine("[TaskbarModule] Explorer monitor stopped.");
        }

        private void OnExplorerRestarted()
        {
            try
            {
                // Give Explorer a moment to fully recreate the taskbar
                Thread.Sleep(1000);

                _detector?.Refresh();

                if (_detector?.PrimaryHandle == IntPtr.Zero)
                {
                    Debug.WriteLine("[TaskbarModule] Taskbar not found after Explorer restart, retrying...");
                    Thread.Sleep(2000);
                    _detector?.Refresh();
                }

                if (_detector?.PrimaryHandle != IntPtr.Zero && _overlay != null)
                {
                    _overlay.OnExplorerRestarted();
                    Debug.WriteLine("[TaskbarModule] Effects reapplied after Explorer restart.");
                }
                else
                {
                    Debug.WriteLine("[TaskbarModule] Could not find taskbar after Explorer restart.");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[TaskbarModule] Error handling Explorer restart: {ex.Message}");
            }
        }

        /// <summary>
        /// Updates settings (opacity, effect, tint color) live without disable/enable.
        /// Called from UI when slider, combobox, or color controls change.
        /// </summary>
        public void UpdateSettings(int opacity, EffectType effect, byte tintR = 0, byte tintG = 0, byte tintB = 0, RenderMode? renderMode = null)
        {
            _settings.Opacity = Math.Clamp(opacity, 0, 100);
            _settings.Effect = effect;
            _settings.TintR = tintR;
            _settings.TintG = tintG;
            _settings.TintB = tintB;
            if (renderMode.HasValue) _settings.RenderMode = renderMode.Value;

            OnPropertyChanged(nameof(CurrentSettings));

            _overlay?.UpdateSettings(_settings);

            Debug.WriteLine($"[TaskbarModule] Settings updated: Opacity={opacity}%, Effect={effect}, Tint=({tintR},{tintG},{tintB})");
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
                StopExplorerMonitor();
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
