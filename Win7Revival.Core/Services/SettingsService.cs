using System.IO;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Win7Revival.Core.Services
{
    /// <summary>
    /// Serviciu pentru persistența setărilor modulelor folosind JSON.
    /// Stochează fișierele în %AppData%/Win7Revival/ conform standardului Windows.
    /// </summary>
    public class SettingsService
    {
        private readonly string _basePath;

        public SettingsService()
        {
            _basePath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Win7Revival");
            Directory.CreateDirectory(_basePath);
        }

        /// <summary>
        /// Constructor pentru teste — permite specificarea explicită a directorului.
        /// </summary>
        public SettingsService(string basePath)
        {
            _basePath = basePath;
            Directory.CreateDirectory(_basePath);
        }

        private string GetFilePath(string moduleName)
        {
            if (string.IsNullOrWhiteSpace(moduleName))
                throw new ArgumentException("Numele modulului nu poate fi null sau gol.", nameof(moduleName));

            string sanitized = Regex.Replace(moduleName, @"[^\w\s\-]", "_");
            sanitized = sanitized.Trim();

            if (string.IsNullOrEmpty(sanitized))
                throw new ArgumentException("Numele modulului conține doar caractere invalide.", nameof(moduleName));

            return Path.Combine(_basePath, $"{sanitized}.json");
        }

        /// <summary>
        /// Încarcă setările pentru un modul.
        /// </summary>
        public async Task<T> LoadSettingsAsync<T>(string moduleName) where T : new()
        {
            string filePath = GetFilePath(moduleName);
            if (!File.Exists(filePath))
            {
                return new T();
            }

            try
            {
                string jsonString = await File.ReadAllTextAsync(filePath);
                return JsonSerializer.Deserialize<T>(jsonString) ?? new T();
            }
            catch (JsonException)
            {
                return new T();
            }
            catch (IOException)
            {
                return new T();
            }
        }

        /// <summary>
        /// Salvează setările pentru un modul.
        /// </summary>
        public async Task SaveSettingsAsync<T>(string moduleName, T settings)
        {
            string filePath = GetFilePath(moduleName);
            var options = new JsonSerializerOptions { WriteIndented = true };
            string jsonString = JsonSerializer.Serialize(settings, options);
            await File.WriteAllTextAsync(filePath, jsonString);
        }
    }
}
