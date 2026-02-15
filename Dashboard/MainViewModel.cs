using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Microsoft.UI.Dispatching;

namespace CrystalFrame.Dashboard
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
                await _config.LoadAsync();
                Debug.WriteLine("Config loaded successfully");

                // Apply loaded config to properties
                TaskbarOpacity = _config.TaskbarOpacity;
                StartOpacity = _config.StartOpacity;
                TaskbarEnabled = _config.TaskbarEnabled;
                StartEnabled = _config.StartEnabled;
                TaskbarBlur = _config.TaskbarBlur;
                StartBlur = _config.StartBlur;
                CoreEnabled = _config.CoreEnabled;
                TaskbarColorR = _config.TaskbarColorR;
                TaskbarColorG = _config.TaskbarColorG;
                TaskbarColorB = _config.TaskbarColorB;

                StartBgColorR = _config.StartBgColorR;
                StartBgColorG = _config.StartBgColorG;
                StartBgColorB = _config.StartBgColorB;

                StartTextColorR = _config.StartTextColorR;
                StartTextColorG = _config.StartTextColorG;
                StartTextColorB = _config.StartTextColorB;

                StartShowControlPanel = _config.StartShowControlPanel;
                StartShowDeviceManager = _config.StartShowDeviceManager;
                StartShowInstalledApps = _config.StartShowInstalledApps;
                StartShowDocuments = _config.StartShowDocuments;
                StartShowPictures = _config.StartShowPictures;
                StartShowVideos = _config.StartShowVideos;
                StartShowRecentFiles = _config.StartShowRecentFiles;

                // Read startup state from registry (not stored in config.json)
                _runAtStartup = StartupManager.IsEnabled();
                PropertyChanged?.Invoke(this, new System.ComponentModel.PropertyChangedEventArgs(nameof(RunAtStartup)));

                // Initialize Core engine (native DLL)
                bool success = _core.Initialize();

                if (!success)
                {
                    ConnectionStatus = "✗ Failed to initialize Core engine";
                    Debug.WriteLine("Core initialization failed");
                    return false;
                }

                CoreRunning = true;
                ConnectionStatus = "✓ Core engine running";

                // Apply initial settings to Core
                _core.SetTaskbarOpacity(TaskbarOpacity);
                _core.SetStartOpacity(StartOpacity);
                _core.SetTaskbarEnabled(TaskbarEnabled);
                _core.SetStartEnabled(StartEnabled);
                _core.SetTaskbarColor(TaskbarColorR, TaskbarColorG, TaskbarColorB);
                _core.SetTaskbarBlur(TaskbarBlur);
                _core.SetStartBlur(StartBlur);

                // Apply Start Menu customization
                _core.SetStartMenuOpacity(StartOpacity);
                _core.SetStartMenuBackgroundColor(StartBgColorR, StartBgColorG, StartBgColorB);
                _core.SetStartMenuTextColor(StartTextColorR, StartTextColorG, StartTextColorB);
                _core.SetStartMenuItems(StartShowControlPanel, StartShowDeviceManager, StartShowInstalledApps,
                                        StartShowDocuments, StartShowPictures, StartShowVideos, StartShowRecentFiles);

                // TESTING: Enable Start Menu hook to intercept Windows key and Start button
                _core.SetStartMenuHookEnabled(true);
                Debug.WriteLine("[TEST] Start Menu hook ENABLED");

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

                    // TESTING: Re-enable Start Menu hook
                    _core.SetStartMenuHookEnabled(true);
                    Debug.WriteLine("[TEST] Start Menu hook re-enabled after restart");

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
            CoreRunning = running;
            ConnectionStatus = running ? "✓ Core engine running" : "Core engine stopped";
        }

        /// <summary>
        /// Called when Dashboard closes
        /// </summary>
        public async Task OnDashboardClosingAsync()
        {
            if (!CoreEnabled)
            {
                Debug.WriteLine("CoreEnabled=false, shutting down Core");
                _core.Dispose(); // Dispose calls Shutdown internally
            }
            else
            {
                Debug.WriteLine("CoreEnabled=true, leaving Core running in background");
                // Do NOT dispose - Core stays active in background
            }

            await Task.CompletedTask;
        }

        // INotifyPropertyChanged implementation
        public event PropertyChangedEventHandler? PropertyChanged;

        protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (Equals(field, value)) return false;

            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            return true;
        }
    }
}
