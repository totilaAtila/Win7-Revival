using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Microsoft.UI.Dispatching;

namespace GlassBar.Dashboard
{
    public class MainViewModel : INotifyPropertyChanged
    {
        private readonly CoreManager _core;
        private readonly ConfigManager _config;
        private readonly DispatcherQueue? _dispatcherQueue;

        public MainViewModel()
        {
            _core = new CoreManager();
            _config = new ConfigManager();
            _dispatcherQueue = DispatcherQueue.GetForCurrentThread();

            _core.CoreRunningChanged += OnCoreRunningChanged;
            _core.StatusUpdated += OnStatusUpdated;
        }

        // Properties
        private int _taskbarOpacity;
        public int TaskbarOpacity
        {
            get => _taskbarOpacity;
            set
            {
                if (SetProperty(ref _taskbarOpacity, value))
                {
                    _config.TaskbarOpacity = value;
                }
            }
        }

        private int _startOpacity;
        public int StartOpacity
        {
            get => _startOpacity;
            set
            {
                if (SetProperty(ref _startOpacity, value))
                {
                    _config.StartOpacity = value;
                }
            }
        }

        private int _taskbarColorR;
        public int TaskbarColorR
        {
            get => _taskbarColorR;
            set
            {
                if (SetProperty(ref _taskbarColorR, value))
                {
                    _config.TaskbarColorR = value;
                }
            }
        }

        private int _taskbarColorG;
        public int TaskbarColorG
        {
            get => _taskbarColorG;
            set
            {
                if (SetProperty(ref _taskbarColorG, value))
                {
                    _config.TaskbarColorG = value;
                }
            }
        }

        private int _taskbarColorB;
        public int TaskbarColorB
        {
            get => _taskbarColorB;
            set
            {
                if (SetProperty(ref _taskbarColorB, value))
                {
                    _config.TaskbarColorB = value;
                }
            }
        }

        // Start Menu Background Color
        private int _startBgColorR;
        public int StartBgColorR { get => _startBgColorR; set { if (SetProperty(ref _startBgColorR, value)) _config.StartBgColorR = value; } }

        private int _startBgColorG;
        public int StartBgColorG { get => _startBgColorG; set { if (SetProperty(ref _startBgColorG, value)) _config.StartBgColorG = value; } }

        private int _startBgColorB;
        public int StartBgColorB { get => _startBgColorB; set { if (SetProperty(ref _startBgColorB, value)) _config.StartBgColorB = value; } }

        // Start Menu Text Color
        private int _startTextColorR;
        public int StartTextColorR { get => _startTextColorR; set { if (SetProperty(ref _startTextColorR, value)) _config.StartTextColorR = value; } }

        private int _startTextColorG;
        public int StartTextColorG { get => _startTextColorG; set { if (SetProperty(ref _startTextColorG, value)) _config.StartTextColorG = value; } }

        private int _startTextColorB;
        public int StartTextColorB { get => _startTextColorB; set { if (SetProperty(ref _startTextColorB, value)) _config.StartTextColorB = value; } }

        // Start Menu Items
        private bool _startShowControlPanel;
        public bool StartShowControlPanel { get => _startShowControlPanel; set { if (SetProperty(ref _startShowControlPanel, value)) _config.StartShowControlPanel = value; } }

        private bool _startShowDeviceManager;
        public bool StartShowDeviceManager { get => _startShowDeviceManager; set { if (SetProperty(ref _startShowDeviceManager, value)) _config.StartShowDeviceManager = value; } }

        private bool _startShowInstalledApps;
        public bool StartShowInstalledApps { get => _startShowInstalledApps; set { if (SetProperty(ref _startShowInstalledApps, value)) _config.StartShowInstalledApps = value; } }

        private bool _startShowDocuments;
        public bool StartShowDocuments { get => _startShowDocuments; set { if (SetProperty(ref _startShowDocuments, value)) _config.StartShowDocuments = value; } }

        private bool _startShowPictures;
        public bool StartShowPictures { get => _startShowPictures; set { if (SetProperty(ref _startShowPictures, value)) _config.StartShowPictures = value; } }

        private bool _startShowVideos;
        public bool StartShowVideos { get => _startShowVideos; set { if (SetProperty(ref _startShowVideos, value)) _config.StartShowVideos = value; } }

        private bool _startShowRecentFiles;
        public bool StartShowRecentFiles { get => _startShowRecentFiles; set { if (SetProperty(ref _startShowRecentFiles, value)) _config.StartShowRecentFiles = value; } }

        private bool _taskbarBlur;
        public bool TaskbarBlur
        {
            get => _taskbarBlur;
            set { if (SetProperty(ref _taskbarBlur, value)) _config.TaskbarBlur = value; }
        }

        private bool _startBlur;
        public bool StartBlur
        {
            get => _startBlur;
            set { if (SetProperty(ref _startBlur, value)) _config.StartBlur = value; }
        }

        private int _blurAmount;
        public int BlurAmount
        {
            get => _blurAmount;
            set
            {
                if (SetProperty(ref _blurAmount, value))
                {
                    _config.BlurAmount = value;
                    _core.SetTaskbarBlurAmount(value);
                }
            }
        }

        // S-B: Keep Start Menu open for Dashboard preview (not persisted)
        private bool _startMenuPinned;
        public bool StartMenuPinned
        {
            get => _startMenuPinned;
            set => SetProperty(ref _startMenuPinned, value);
        }

        // S-E: Border/accent color
        private int _startBorderColorR;
        public int StartBorderColorR { get => _startBorderColorR; set { if (SetProperty(ref _startBorderColorR, value)) _config.StartBorderColorR = value; } }

        private int _startBorderColorG;
        public int StartBorderColorG { get => _startBorderColorG; set { if (SetProperty(ref _startBorderColorG, value)) _config.StartBorderColorG = value; } }

        private int _startBorderColorB;
        public int StartBorderColorB { get => _startBorderColorB; set { if (SetProperty(ref _startBorderColorB, value)) _config.StartBorderColorB = value; } }

        private bool _taskbarEnabled;
        public bool TaskbarEnabled
        {
            get => _taskbarEnabled;
            set { if (SetProperty(ref _taskbarEnabled, value)) _config.TaskbarEnabled = value; }
        }

        private bool _startEnabled;
        public bool StartEnabled
        {
            get => _startEnabled;
            set { if (SetProperty(ref _startEnabled, value)) _config.StartEnabled = value; }
        }

        private bool _taskbarFound;
        public bool TaskbarFound
        {
            get => _taskbarFound;
            set => SetProperty(ref _taskbarFound, value);
        }

        private bool _startDetected;
        public bool StartDetected
        {
            get => _startDetected;
            set => SetProperty(ref _startDetected, value);
        }

        private string _connectionStatus = "Initializing...";
        public string ConnectionStatus
        {
            get => _connectionStatus;
            set => SetProperty(ref _connectionStatus, value);
        }

        private bool _coreRunning;
        public bool CoreRunning
        {
            get => _coreRunning;
            set => SetProperty(ref _coreRunning, value);
        }

        private bool _coreEnabled = true;
        public bool CoreEnabled
        {
            get => _coreEnabled;
            set
            {
                if (SetProperty(ref _coreEnabled, value))
                {
                    _config.CoreEnabled = value;
                }
            }
        }

        private string _extractionError = string.Empty;
        public string ExtractionError
        {
            get => _extractionError;
            set => SetProperty(ref _extractionError, value);
        }

        private bool _runAtStartup;
        public bool RunAtStartup
        {
            get => _runAtStartup;
            set
            {
                if (SetProperty(ref _runAtStartup, value))
                    StartupManager.SetEnabled(value);
            }
        }

        // Methods
        public async Task<bool> InitializeAsync()
        {
            try
            {
                // Load config
                using var suppressAutoSave = _config.SuppressAutoSave();
                await _config.LoadAsync();
                Debug.WriteLine("Config loaded successfully");

                // First-run safety: on the very first launch (or after a crash before the flag
                // was cleared) disable all effects so the app starts in a known-good state.
                // The user can enable features from the Dashboard after confirming startup works.
                // NOTE: IsFirstRun is only cleared AFTER _core.Initialize() succeeds, so that
                // a crash during init causes the next launch to enter safe mode again.
                bool isFirstRun = _config.IsFirstRun;
                if (isFirstRun)
                {
                    Debug.WriteLine("[FIRST RUN] First launch detected — all effects disabled for safe startup");
                    _config.TaskbarEnabled = false;
                    _config.StartEnabled   = false;
                    // Do NOT clear IsFirstRun here — wait until init succeeds below.
                }

                // Apply loaded config to properties without triggering save-on-load.
                HydrateViewModelFromConfig();

                // Read startup state from registry (not stored in config.json)
                _runAtStartup = StartupManager.IsEnabled();
                PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(nameof(RunAtStartup)));

                // Initialize Core engine (native DLL)
                bool success = _core.Initialize();
                suppressAutoSave.Dispose();

                if (!success)
                {
                    ConnectionStatus = "✗ Failed to initialize Core engine";
                    Debug.WriteLine("Core initialization failed");
                    return false;
                }

                // Init succeeded — now safe to clear the first-run flag so next launch is normal.
                if (isFirstRun)
                {
                    _config.IsFirstRun = false;
                    _ = _config.SaveAsync();
                    Debug.WriteLine("[FIRST RUN] Init succeeded — IsFirstRun cleared");
                }

                CoreRunning = true;
                ConnectionStatus = "✓ Core engine running";

                // Apply initial settings to Core
                LogStartupSnapshot("InitializeAsync startup batch");
                _core.SetTaskbarOpacity(TaskbarOpacity);
                _core.SetStartOpacity(StartOpacity);
                _core.SetTaskbarEnabled(TaskbarEnabled);
                _core.SetStartEnabled(StartEnabled);
                _core.SetTaskbarColor(TaskbarColorR, TaskbarColorG, TaskbarColorB);
                _core.SetTaskbarBlur(TaskbarBlur);
                _core.SetStartBlur(StartBlur);
                if (_blurAmount > 0) _core.SetTaskbarBlurAmount(_blurAmount);

                // Apply Start Menu customization
                _core.SetStartMenuOpacity(StartOpacity);
                _core.SetStartMenuBackgroundColor(StartBgColorR, StartBgColorG, StartBgColorB);
                _core.SetStartMenuTextColor(StartTextColorR, StartTextColorG, StartTextColorB);
                _core.SetStartMenuBorderColor(StartBorderColorR, StartBorderColorG, StartBorderColorB);
                _core.SetStartMenuItems(StartShowControlPanel, StartShowDeviceManager, StartShowInstalledApps,
                                        StartShowDocuments, StartShowPictures, StartShowVideos, StartShowRecentFiles);

                // Start Menu hook: skip on first run, and only when Start is actually enabled.
                if (!isFirstRun && StartEnabled)
                {
                    _core.SetStartMenuHookEnabled(true);
                    Debug.WriteLine("Start Menu hook ENABLED");
                }

                // Register global hotkey if configured.
                if (_config.HotkeyVk != 0)
                {
                    _core.RegisterHotkey(_config.HotkeyVk, _config.HotkeyModifiers);
                    Debug.WriteLine($"Global hotkey restored: vk=0x{_config.HotkeyVk:X2} mod=0x{_config.HotkeyModifiers:X}");
                }

                Debug.WriteLine("ViewModel initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"ViewModel.InitializeAsync exception: {ex.Message}\n{ex.StackTrace}");
                ConnectionStatus = $"✗ Initialization error: {ex.Message}";
                throw;
            }
        }

        /// <summary>
        /// Toggle Core ON/OFF
        /// </summary>
        public async Task SetCoreRunningAsync(bool running)
        {
            // Update the CoreEnabled preference
            CoreEnabled = running;

            if (running)
            {
                // Ensure any previous instance is stopped before re-initializing
                if (_core.IsRunning)
                {
                    _core.Shutdown();
                }

                bool success = _core.Initialize();
                CoreRunning = success;

                if (success)
                {
                    await Task.Delay(100); // Brief delay for Core to settle

                    // Reapply settings
                    LogStartupSnapshot("SetCoreRunningAsync restart batch");
                    _core.SetTaskbarOpacity(TaskbarOpacity);
                    _core.SetStartOpacity(StartOpacity);
                    _core.SetTaskbarEnabled(TaskbarEnabled);
                    _core.SetStartEnabled(StartEnabled);
                    _core.SetTaskbarColor(TaskbarColorR, TaskbarColorG, TaskbarColorB);
                    _core.SetTaskbarBlur(TaskbarBlur);
                    _core.SetStartBlur(StartBlur);

                    // Reapply Start Menu customization
                    _core.SetStartMenuOpacity(StartOpacity);
                    _core.SetStartMenuBackgroundColor(StartBgColorR, StartBgColorG, StartBgColorB);
                    _core.SetStartMenuTextColor(StartTextColorR, StartTextColorG, StartTextColorB);
                    _core.SetStartMenuItems(StartShowControlPanel, StartShowDeviceManager, StartShowInstalledApps,
                                            StartShowDocuments, StartShowPictures, StartShowVideos, StartShowRecentFiles);

                    _core.SetStartMenuHookEnabled(StartEnabled);
                    Debug.WriteLine($"Start Menu hook {(StartEnabled ? "ENABLED" : "DISABLED")} after Core restart");

                    ConnectionStatus = "✓ Core engine running";
                }
                else
                {
                    ConnectionStatus = "✗ Failed to start Core";
                }
            }
            else
            {
                _core.Shutdown();
                CoreRunning = false;
                ConnectionStatus = "Core engine stopped";
            }
        }

        public Task SetTaskbarEnabledAsync(bool enabled)
        {
            TaskbarEnabled = enabled;
            _core.SetTaskbarEnabled(enabled);
            return Task.CompletedTask;
        }

        public Task SetStartEnabledAsync(bool enabled)
        {
            StartEnabled = enabled;
            _core.SetStartEnabled(enabled);
            // Sync hook state: when Start is enabled (including on first-run where hook was
            // skipped), activate the Win key hook; when disabled, deactivate it.
            _core.SetStartMenuHookEnabled(enabled);
            return Task.CompletedTask;
        }

        public void OnTaskbarOpacityChanged(int value)
        {
            TaskbarOpacity = value;
            _core.SetTaskbarOpacity(value);
        }

        public void OnStartOpacityChanged(int value)
        {
            StartOpacity = value;
            _core.SetStartOpacity(value); // Windows Start Menu (native)
            _core.SetStartMenuOpacity(value); // Custom Start Menu window
        }

        public void OnTaskbarBlurChanged(bool enabled)
        {
            TaskbarBlur = enabled;
            _core.SetTaskbarBlur(enabled);
        }

        public void OnStartBlurChanged(bool enabled)
        {
            StartBlur = enabled;
            _core.SetStartBlur(enabled);
        }

        public void OnTaskbarColorChanged(int r, int g, int b)
        {
            TaskbarColorR = r;
            TaskbarColorG = g;
            TaskbarColorB = b;
            _core.SetTaskbarColor(r, g, b);
        }

        public void OnStartBgColorChanged(int r, int g, int b)
        {
            StartBgColorR = r;
            StartBgColorG = g;
            StartBgColorB = b;
            _core.SetStartMenuBackgroundColor(r, g, b);
        }

        public void OnStartTextColorChanged(int r, int g, int b)
        {
            StartTextColorR = r;
            StartTextColorG = g;
            StartTextColorB = b;
            _core.SetStartMenuTextColor(r, g, b);
        }

        public void OnStartMenuItemChanged(string itemName, bool visible)
        {
            switch (itemName)
            {
                case "ControlPanel":
                    StartShowControlPanel = visible;
                    break;
                case "DeviceManager":
                    StartShowDeviceManager = visible;
                    break;
                case "InstalledApps":
                    StartShowInstalledApps = visible;
                    break;
                case "Documents":
                    StartShowDocuments = visible;
                    break;
                case "Pictures":
                    StartShowPictures = visible;
                    break;
                case "Videos":
                    StartShowVideos = visible;
                    break;
                case "RecentFiles":
                    StartShowRecentFiles = visible;
                    break;
            }

            // Apply to Core
            _core.SetStartMenuItems(StartShowControlPanel, StartShowDeviceManager, StartShowInstalledApps,
                                    StartShowDocuments, StartShowPictures, StartShowVideos, StartShowRecentFiles);
        }

        // S-B: toggle Keep-Open preview
        public void OnStartMenuPinnedChanged(bool pinned)
        {
            StartMenuPinned = pinned;
            _core.SetStartMenuPinned(pinned);
        }

        // S-E: border color sliders
        public void OnStartBorderColorChanged(int r, int g, int b)
        {
            StartBorderColorR = r;
            StartBorderColorG = g;
            StartBorderColorB = b;
            _core.SetStartMenuBorderColor(r, g, b);
        }

        // Global theme: applies matching colors to both Taskbar and Start Menu, opacity 50.
        public void ApplyGlobalTheme(string name)
        {
            switch (name)
            {
                case "Win7Aero":  // Aero Glass blue — translucent, opacity 50
                    TaskbarEnabled = true;
                    _core.SetTaskbarEnabled(true);
                    StartEnabled = true;
                    _core.SetStartEnabled(true);
                    OnTaskbarColorChanged(20, 40, 80);
                    OnTaskbarOpacityChanged(50);
                    OnStartBgColorChanged(20, 40, 80);
                    OnStartTextColorChanged(255, 255, 255);
                    OnStartBorderColorChanged(60, 100, 160);
                    OnStartOpacityChanged(17);
                    OnStartBlurChanged(false);
                    break;
                case "Dark":      // Dark charcoal — modern dark theme, opacity 50
                    TaskbarEnabled = true;
                    _core.SetTaskbarEnabled(true);
                    StartEnabled = true;
                    _core.SetStartEnabled(true);
                    OnTaskbarColorChanged(18, 18, 22);
                    OnTaskbarOpacityChanged(50);
                    OnStartBgColorChanged(18, 18, 22);
                    OnStartTextColorChanged(200, 200, 200);
                    OnStartBorderColorChanged(60, 60, 65);
                    OnStartOpacityChanged(17);
                    OnStartBlurChanged(false);
                    break;
            }
        }

        // S-F: apply theme preset
        public void ApplyPreset(string name)
        {
            switch (name)
            {
                case "ClassicWin7":
                    OnStartBgColorChanged(20, 60, 120);
                    OnStartTextColorChanged(255, 255, 255);
                    OnStartBorderColorChanged(80, 130, 190);
                    OnStartOpacityChanged(85);
                    OnStartBlurChanged(true);
                    break;
                case "AeroGlass":
                    OnStartBgColorChanged(20, 40, 80);
                    OnStartTextColorChanged(255, 255, 255);
                    OnStartBorderColorChanged(60, 100, 160);
                    OnStartOpacityChanged(16);
                    OnStartBlurChanged(false);
                    break;
                case "Dark":
                    OnStartBgColorChanged(18, 18, 22);
                    OnStartTextColorChanged(200, 200, 200);
                    OnStartBorderColorChanged(60, 60, 65);
                    OnStartOpacityChanged(21);
                    OnStartBlurChanged(false);
                    break;
            }
        }

        private void OnStatusUpdated(object sender, CoreNative.CoreStatus status)
        {
            // Status is updated from background thread - dispatch to UI thread
            _dispatcherQueue?.TryEnqueue(() =>
            {
                TaskbarFound = status.Taskbar.Found;
                StartDetected = status.Start.Detected;
            });
        }

        private void OnCoreRunningChanged(object sender, bool running)
        {
            _dispatcherQueue?.TryEnqueue(() =>
            {
                CoreRunning = running;
                ConnectionStatus = running ? "✓ Core engine running" : "Core engine stopped";
            });
        }

        /// <summary>
        /// Called when Dashboard closes.
        /// forceShutdown=true (Exit din tray) → Core se oprește întotdeauna, indiferent de CoreEnabled.
        /// forceShutdown=false (comportament vechi) → Core rămâne activ dacă CoreEnabled=true.
        /// </summary>
        public async Task OnDashboardClosingAsync(bool forceShutdown = false)
        {
            if (forceShutdown || !CoreEnabled)
            {
                Debug.WriteLine($"Shutting down Core (forceShutdown={forceShutdown}, CoreEnabled={CoreEnabled})");
                _core.Dispose(); // Dispose calls Shutdown → RestoreWindow on all taskbars
            }
            else
            {
                Debug.WriteLine("CoreEnabled=true, leaving Core running in background");
                // Do NOT dispose - Core stays active in background
            }

            await Task.CompletedTask;
        }

        // ── Global hotkey ────────────────────────────────────────────────────────

        /// <summary>
        /// Register (or update) the global hotkey that toggles the taskbar overlay.
        /// Persists the choice to config.json via ConfigManager.
        /// </summary>
        public void ApplyHotkey(int vk, int modifiers)
        {
            _config.HotkeyVk        = vk;
            _config.HotkeyModifiers = modifiers;
            _ = _config.SaveAsync();
            _core.RegisterHotkey(vk, modifiers);
        }

        /// <summary>
        /// Remove the global hotkey and clear the persisted setting.
        /// </summary>
        public void ClearHotkey()
        {
            _config.HotkeyVk        = 0;
            _config.HotkeyModifiers = 0;
            _ = _config.SaveAsync();
            _core.UnregisterHotkey();
        }

        /// <summary>VK code loaded from config (0 = none).</summary>
        public int LoadedHotkeyVk        => _config.HotkeyVk;

        /// <summary>Modifier mask loaded from config.</summary>
        public int LoadedHotkeyModifiers => _config.HotkeyModifiers;

        private void HydrateViewModelFromConfig()
        {
            _taskbarOpacity = _config.TaskbarOpacity;
            _startOpacity = _config.StartOpacity;
            _taskbarEnabled = _config.TaskbarEnabled;
            _startEnabled = _config.StartEnabled;
            _taskbarBlur = _config.TaskbarBlur;
            _startBlur = _config.StartBlur;
            _blurAmount = _config.BlurAmount;
            _coreEnabled = _config.CoreEnabled;
            _taskbarColorR = _config.TaskbarColorR;
            _taskbarColorG = _config.TaskbarColorG;
            _taskbarColorB = _config.TaskbarColorB;

            _startBgColorR = _config.StartBgColorR;
            _startBgColorG = _config.StartBgColorG;
            _startBgColorB = _config.StartBgColorB;
            _startTextColorR = _config.StartTextColorR;
            _startTextColorG = _config.StartTextColorG;
            _startTextColorB = _config.StartTextColorB;

            _startShowControlPanel = _config.StartShowControlPanel;
            _startShowDeviceManager = _config.StartShowDeviceManager;
            _startShowInstalledApps = _config.StartShowInstalledApps;
            _startShowDocuments = _config.StartShowDocuments;
            _startShowPictures = _config.StartShowPictures;
            _startShowVideos = _config.StartShowVideos;
            _startShowRecentFiles = _config.StartShowRecentFiles;

            _startBorderColorR = _config.StartBorderColorR;
            _startBorderColorG = _config.StartBorderColorG;
            _startBorderColorB = _config.StartBorderColorB;

            RaisePropertyChanged(nameof(TaskbarOpacity));
            RaisePropertyChanged(nameof(StartOpacity));
            RaisePropertyChanged(nameof(TaskbarEnabled));
            RaisePropertyChanged(nameof(StartEnabled));
            RaisePropertyChanged(nameof(TaskbarBlur));
            RaisePropertyChanged(nameof(StartBlur));
            RaisePropertyChanged(nameof(BlurAmount));
            RaisePropertyChanged(nameof(CoreEnabled));
            RaisePropertyChanged(nameof(TaskbarColorR));
            RaisePropertyChanged(nameof(TaskbarColorG));
            RaisePropertyChanged(nameof(TaskbarColorB));
            RaisePropertyChanged(nameof(StartBgColorR));
            RaisePropertyChanged(nameof(StartBgColorG));
            RaisePropertyChanged(nameof(StartBgColorB));
            RaisePropertyChanged(nameof(StartTextColorR));
            RaisePropertyChanged(nameof(StartTextColorG));
            RaisePropertyChanged(nameof(StartTextColorB));
            RaisePropertyChanged(nameof(StartShowControlPanel));
            RaisePropertyChanged(nameof(StartShowDeviceManager));
            RaisePropertyChanged(nameof(StartShowInstalledApps));
            RaisePropertyChanged(nameof(StartShowDocuments));
            RaisePropertyChanged(nameof(StartShowPictures));
            RaisePropertyChanged(nameof(StartShowVideos));
            RaisePropertyChanged(nameof(StartShowRecentFiles));
            RaisePropertyChanged(nameof(StartBorderColorR));
            RaisePropertyChanged(nameof(StartBorderColorG));
            RaisePropertyChanged(nameof(StartBorderColorB));
        }

        private void LogStartupSnapshot(string label)
        {
            Debug.WriteLine($"[Startup] {label}: Taskbar enabled={TaskbarEnabled} opacity={TaskbarOpacity} RGB=({TaskbarColorR},{TaskbarColorG},{TaskbarColorB}) blur={TaskbarBlur} amount={BlurAmount}");
            Debug.WriteLine($"[Startup] {label}: Start enabled={StartEnabled} opacity={StartOpacity} blur={StartBlur} bg=({StartBgColorR},{StartBgColorG},{StartBgColorB}) text=({StartTextColorR},{StartTextColorG},{StartTextColorB}) border=({StartBorderColorR},{StartBorderColorG},{StartBorderColorB})");
            Debug.WriteLine($"[Startup] {label}: Start items CP={StartShowControlPanel} DM={StartShowDeviceManager} IA={StartShowInstalledApps} D={StartShowDocuments} P={StartShowPictures} V={StartShowVideos} RF={StartShowRecentFiles}");
            Debug.WriteLine($"[Startup] {label}: Hotkey vk=0x{_config.HotkeyVk:X2} mod=0x{_config.HotkeyModifiers:X}");
        }

        // INotifyPropertyChanged implementation
        public event PropertyChangedEventHandler? PropertyChanged;

        private void RaisePropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (Equals(field, value)) return false;

            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            return true;
        }
    }
}
