using System;
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;

namespace GlassBar.Dashboard
{
    public class Config
    {
        public int TaskbarOpacity { get; set; } = 75;
        public int StartOpacity { get; set; } = 50;
        public bool TaskbarEnabled { get; set; } = true;
        public bool StartEnabled { get; set; } = false;
        public bool CoreEnabled { get; set; } = true;  // Default ON - Core runs in background after Dashboard closes
        public int TaskbarColorR { get; set; } = 0;
        public int TaskbarColorG { get; set; } = 0;
        public int TaskbarColorB { get; set; } = 0;

        // Start Menu Background Color
        public int StartBgColorR { get; set; } = 40;
        public int StartBgColorG { get; set; } = 40;
        public int StartBgColorB { get; set; } = 45;

        // Start Menu Text Color
        public int StartTextColorR { get; set; } = 255;
        public int StartTextColorG { get; set; } = 255;
        public int StartTextColorB { get; set; } = 255;

        // Start Menu Items
        public bool StartShowControlPanel { get; set; } = true;
        public bool StartShowDeviceManager { get; set; } = true;
        public bool StartShowInstalledApps { get; set; } = true;
        public bool StartShowDocuments { get; set; } = true;
        public bool StartShowPictures { get; set; } = true;
        public bool StartShowVideos { get; set; } = true;
        public bool StartShowRecentFiles { get; set; } = true;
        public bool StartShowPlaceholder1 { get; set; } = false;
        public bool StartShowPlaceholder2 { get; set; } = false;
        public bool StartShowPlaceholder3 { get; set; } = false;
        public bool StartShowPlaceholder4 { get; set; } = false;
        public bool StartShowPlaceholder5 { get; set; } = false;
        // Start Menu Border/Accent Color (S-E)
        public int StartBorderColorR { get; set; } = 60;
        public int StartBorderColorG { get; set; } = 60;
        public int StartBorderColorB { get; set; } = 65;

        public bool TaskbarBlur { get; set; } = false;
        public bool StartBlur { get; set; } = false;
        // True until the first successful launch completes — triggers safe-mode defaults.
        // Default is false so that existing config files (upgrade scenario) are not treated
        // as first-run when the field is absent from JSON.
        public bool IsFirstRun { get; set; } = false;

        // Global hotkey toggle (0 = disabled)
        public int HotkeyVk        { get; set; } = 0;
        public int HotkeyModifiers { get; set; } = 0;

        // XamlBridge blur amount (0 = off, 1-100 = intensity)
        public int BlurAmount { get; set; } = 0;
    }

    public class ConfigManager
    {
        private readonly string _configPath;
        private Config _config = new Config();
        private readonly DebounceTimer _saveDebounce;
        private int _suppressAutoSaveCount;

        public ConfigManager()
        {
            var appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var folder = Path.Combine(appData, "GlassBar");
            Directory.CreateDirectory(folder);
            _configPath = Path.Combine(folder, "config.json");

            // Debounce saves to avoid excessive disk I/O
            _saveDebounce = new DebounceTimer(250, async () => await SaveNowAsync());
        }

        public int TaskbarOpacity
        {
            get => _config.TaskbarOpacity;
            set
            {
                _config.TaskbarOpacity = value;
                _ = SaveAsync();
            }
        }

        public int StartOpacity
        {
            get => _config.StartOpacity;
            set
            {
                _config.StartOpacity = value;
                _ = SaveAsync();
            }
        }

        public bool TaskbarEnabled
        {
            get => _config.TaskbarEnabled;
            set
            {
                _config.TaskbarEnabled = value;
                _ = SaveAsync();
            }
        }

        public bool StartEnabled
        {
            get => _config.StartEnabled;
            set
            {
                _config.StartEnabled = value;
                _ = SaveAsync();
            }
        }

        public bool CoreEnabled
        {
            get => _config.CoreEnabled;
            set
            {
                _config.CoreEnabled = value;
                _ = SaveAsync();
            }
        }

        public int TaskbarColorR
        {
            get => _config.TaskbarColorR;
            set
            {
                _config.TaskbarColorR = value;
                _ = SaveAsync();
            }
        }

        public int TaskbarColorG
        {
            get => _config.TaskbarColorG;
            set
            {
                _config.TaskbarColorG = value;
                _ = SaveAsync();
            }
        }

        public int TaskbarColorB
        {
            get => _config.TaskbarColorB;
            set
            {
                _config.TaskbarColorB = value;
                _ = SaveAsync();
            }
        }

        public int StartBgColorR { get => _config.StartBgColorR; set { _config.StartBgColorR = value; _ = SaveAsync(); } }
        public int StartBgColorG { get => _config.StartBgColorG; set { _config.StartBgColorG = value; _ = SaveAsync(); } }
        public int StartBgColorB { get => _config.StartBgColorB; set { _config.StartBgColorB = value; _ = SaveAsync(); } }

        public int StartTextColorR { get => _config.StartTextColorR; set { _config.StartTextColorR = value; _ = SaveAsync(); } }
        public int StartTextColorG { get => _config.StartTextColorG; set { _config.StartTextColorG = value; _ = SaveAsync(); } }
        public int StartTextColorB { get => _config.StartTextColorB; set { _config.StartTextColorB = value; _ = SaveAsync(); } }

        public bool StartShowControlPanel { get => _config.StartShowControlPanel; set { _config.StartShowControlPanel = value; _ = SaveAsync(); } }
        public bool StartShowDeviceManager { get => _config.StartShowDeviceManager; set { _config.StartShowDeviceManager = value; _ = SaveAsync(); } }
        public bool StartShowInstalledApps { get => _config.StartShowInstalledApps; set { _config.StartShowInstalledApps = value; _ = SaveAsync(); } }
        public bool StartShowDocuments { get => _config.StartShowDocuments; set { _config.StartShowDocuments = value; _ = SaveAsync(); } }
        public bool StartShowPictures { get => _config.StartShowPictures; set { _config.StartShowPictures = value; _ = SaveAsync(); } }
        public bool StartShowVideos { get => _config.StartShowVideos; set { _config.StartShowVideos = value; _ = SaveAsync(); } }
        public bool StartShowRecentFiles { get => _config.StartShowRecentFiles; set { _config.StartShowRecentFiles = value; _ = SaveAsync(); } }
        public bool StartShowPlaceholder1 { get => _config.StartShowPlaceholder1; set { _config.StartShowPlaceholder1 = value; _ = SaveAsync(); } }
        public bool StartShowPlaceholder2 { get => _config.StartShowPlaceholder2; set { _config.StartShowPlaceholder2 = value; _ = SaveAsync(); } }
        public bool StartShowPlaceholder3 { get => _config.StartShowPlaceholder3; set { _config.StartShowPlaceholder3 = value; _ = SaveAsync(); } }
        public bool StartShowPlaceholder4 { get => _config.StartShowPlaceholder4; set { _config.StartShowPlaceholder4 = value; _ = SaveAsync(); } }
        public bool StartShowPlaceholder5 { get => _config.StartShowPlaceholder5; set { _config.StartShowPlaceholder5 = value; _ = SaveAsync(); } }
        public int  StartBorderColorR { get => _config.StartBorderColorR; set { _config.StartBorderColorR = value; _ = SaveAsync(); } }
        public int  StartBorderColorG { get => _config.StartBorderColorG; set { _config.StartBorderColorG = value; _ = SaveAsync(); } }
        public int  StartBorderColorB { get => _config.StartBorderColorB; set { _config.StartBorderColorB = value; _ = SaveAsync(); } }
        public bool TaskbarBlur { get => _config.TaskbarBlur; set { _config.TaskbarBlur = value; _ = SaveAsync(); } }
        public bool StartBlur   { get => _config.StartBlur;   set { _config.StartBlur   = value; _ = SaveAsync(); } }
        public bool IsFirstRun  { get => _config.IsFirstRun;  set { _config.IsFirstRun  = value; _ = SaveAsync(); } }
        public int  HotkeyVk        { get => _config.HotkeyVk;        set { _config.HotkeyVk        = value; _ = SaveAsync(); } }
        public int  HotkeyModifiers { get => _config.HotkeyModifiers; set { _config.HotkeyModifiers = value; _ = SaveAsync(); } }
        public int  BlurAmount      { get => _config.BlurAmount;      set { _config.BlurAmount      = value; _ = SaveAsync(); } }

        public async Task LoadAsync()
        {
            try
            {
                if (File.Exists(_configPath))
                {
                    var json = await File.ReadAllTextAsync(_configPath);
                    _config = JsonSerializer.Deserialize<Config>(json) ?? new Config();
                    // Existing file: IsFirstRun defaults to false in Config class,
                    // so absent field in JSON = upgrade scenario = normal run. Correct.
                }
                else
                {
                    // No file at all = genuine first install.
                    _config = new Config { IsFirstRun = true };
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Config load error: {ex.Message}");
                _config = new Config();
            }
        }

        public Task SaveAsync()
        {
            if (_suppressAutoSaveCount > 0)
            {
                return Task.CompletedTask;
            }
            _saveDebounce.Trigger();
            return Task.CompletedTask;
        }

        public IDisposable SuppressAutoSave()
        {
            _suppressAutoSaveCount++;
            return new AutoSaveScope(this);
        }

        private async Task SaveNowAsync()
        {
            try
            {
                var options = new JsonSerializerOptions { WriteIndented = true };
                var json = JsonSerializer.Serialize(_config, options);
                await File.WriteAllTextAsync(_configPath, json);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Config save error: {ex.Message}");
            }
        }

        private sealed class AutoSaveScope : IDisposable
        {
            private ConfigManager? _owner;

            public AutoSaveScope(ConfigManager owner)
            {
                _owner = owner;
            }

            public void Dispose()
            {
                if (_owner != null)
                {
                    _owner._suppressAutoSaveCount = Math.Max(0, _owner._suppressAutoSaveCount - 1);
                    _owner = null;
                }
            }
        }
    }
}
