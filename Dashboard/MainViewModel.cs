using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;

namespace CrystalFrame.Dashboard
{
    public class MainViewModel : INotifyPropertyChanged
    {
        private readonly IpcClient _ipc;
        private readonly ConfigManager _config;
        private readonly CoreProcessManager _coreManager;
        private readonly DebounceTimer _taskbarDebounce;
        private readonly DebounceTimer _startDebounce;

        public MainViewModel()
        {
            _ipc = new IpcClient();
            _config = new ConfigManager();
            _coreManager = new CoreProcessManager(_ipc);

            // 250ms debounce for slider updates
            _taskbarDebounce = new DebounceTimer(250, async () =>
                await _ipc.SendCommandAsync("SetTaskbarOpacity", new { opacity = TaskbarOpacity }));

            _startDebounce = new DebounceTimer(250, async () =>
                await _ipc.SendCommandAsync("SetStartOpacity", new { opacity = StartOpacity }));

            _ipc.StatusUpdated += OnStatusUpdated;
            _ipc.ErrorReceived += OnErrorReceived;
            _ipc.ConnectionChanged += OnConnectionChanged;
            _coreManager.CoreRunningChanged += OnCoreRunningChanged;
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

        private bool _taskbarEnabled;
        public bool TaskbarEnabled
        {
            get => _taskbarEnabled;
            set => SetProperty(ref _taskbarEnabled, value);
        }

        private bool _startEnabled;
        public bool StartEnabled
        {
            get => _startEnabled;
            set => SetProperty(ref _startEnabled, value);
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

        private string _connectionStatus = "Connecting...";
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

        // Methods
        public async Task InitializeAsync()
        {
            // Load config
            await _config.LoadAsync();

            // Apply loaded config to properties
            TaskbarOpacity = _config.TaskbarOpacity;
            StartOpacity = _config.StartOpacity;
            TaskbarEnabled = _config.TaskbarEnabled;
            StartEnabled = _config.StartEnabled;

            // Ensure Core is running (auto-launch if needed)
            CoreRunning = _coreManager.EnsureCoreRunning();

            if (!CoreRunning)
            {
                ConnectionStatus = "✗ Failed to start Core engine";
                return;
            }

            // Connect to Core via IPC
            try
            {
                await _ipc.ConnectAsync();
                ConnectionStatus = "✓ Connected to Core";
            }
            catch (Exception ex)
            {
                ConnectionStatus = $"✗ Connection failed: {ex.Message}";
            }
        }

        /// <summary>
        /// Toggle Core ON/OFF. When OFF, the overlay effects stop.
        /// When ON, Core is relaunched and IPC reconnects automatically.
        /// </summary>
        public async Task SetCoreRunningAsync(bool running)
        {
            if (running)
            {
                CoreRunning = _coreManager.StartCore();

                if (CoreRunning)
                {
                    // Give Core a moment to create the pipe
                    await Task.Delay(500);
                    
                    try
                    {
                        await _ipc.ConnectAsync();
                    }
                    catch (Exception ex)
                    {
                        ConnectionStatus = $"✗ Connection failed: {ex.Message}";
                    }
                }
            }
            else
            {
                await _coreManager.StopCoreAsync();
                CoreRunning = false;
            }
        }

        /// <summary>
        /// Called when Dashboard closes. Does NOT stop Core — effects persist.
        /// </summary>
        public void OnDashboardClosing()
        {
            _coreManager.Dispose();
            _ipc.Dispose();
        }

        public async Task SetTaskbarEnabledAsync(bool enabled)
        {
            TaskbarEnabled = enabled;
            // Config is saved automatically via property setter → ConfigManager debounced save
            await _ipc.SendCommandAsync("SetTaskbarEnabled", new { enabled });
        }

        public async Task SetStartEnabledAsync(bool enabled)
        {
            StartEnabled = enabled;
            // Config is saved automatically via property setter → ConfigManager debounced save
            await _ipc.SendCommandAsync("SetStartEnabled", new { enabled });
        }

        public void OnTaskbarOpacityChanged(int value)
        {
            TaskbarOpacity = value;
            _taskbarDebounce.Trigger();  // Debounced IPC send
        }

        public void OnStartOpacityChanged(int value)
        {
            StartOpacity = value;
            _startDebounce.Trigger();  // Debounced IPC send
        }

        private void OnStatusUpdated(object sender, StatusUpdateEventArgs e)
        {
            // Update UI properties from Core status
            TaskbarFound = e.Status.Taskbar.Found;
            StartDetected = e.Status.Start.Detected;
        }

        private void OnErrorReceived(object sender, string error)
        {
            ConnectionStatus = $"✗ Error: {error}";
        }

        private void OnConnectionChanged(object sender, bool connected)
        {
            ConnectionStatus = connected 
                ? "✓ Connected to Core" 
                : "⟳ Reconnecting to Core...";
        }

        private void OnCoreRunningChanged(object sender, bool running)
        {
            CoreRunning = running;
            if (!running)
            {
                ConnectionStatus = "● Core stopped";
            }
        }

        // INotifyPropertyChanged
        public event PropertyChangedEventHandler PropertyChanged;

        protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string propertyName = null)
        {
            if (Equals(field, value)) return false;
            field = value;
            OnPropertyChanged(propertyName);
            return true;
        }

        protected void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
