using System;
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;

namespace CrystalFrame.Dashboard
{
    public class Config
    {
        public int TaskbarOpacity { get; set; } = 75;
        public int StartOpacity { get; set; } = 50;
        public bool TaskbarEnabled { get; set; } = true;
        public bool StartEnabled { get; set; } = true;
    }

    public class ConfigManager
    {
        private readonly string _configPath;
        private Config _config;
        private readonly DebounceTimer _saveDebounce;

        public ConfigManager()
        {
            var appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            var folder = Path.Combine(appData, "CrystalFrame");
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

        public async Task LoadAsync()
        {
            try
            {
                if (File.Exists(_configPath))
                {
                    var json = await File.ReadAllTextAsync(_configPath);
                    _config = JsonSerializer.Deserialize<Config>(json) ?? new Config();
                }
                else
                {
                    _config = new Config();
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Config load error: {ex.Message}");
                _config = new Config();
            }
        }

        public async Task SaveAsync()
        {
            _saveDebounce.Trigger();
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
    }
}
