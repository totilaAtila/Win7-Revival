using System.IO;
using System.Text.Json;
using System.Threading.Tasks;

namespace Win7Revival.Core.Services
{
    /// <summary>
    /// Serviciu pentru persistența setărilor modulelor folosind JSON.
    /// </summary>
    public class SettingsService
    {
        private readonly string _basePath = "Win7RevivalSettings"; // Placeholder for a base directory on Windows

        public SettingsService()
        {
            // In a real Windows app, this would use Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)
            // For now, we ensure the directory exists relative to the execution path (for testing purposes).
            Directory.CreateDirectory(_basePath);
        }

        private string GetFilePath(string moduleName)
        {
            return Path.Combine(_basePath, $"{moduleName}.json");
        }

        /// <summary>
        /// Încarcă setările pentru un modul.
        /// </summary>
        /// <typeparam name="T">Tipul modelului de setări.</typeparam>
        /// <param name="moduleName">Numele modulului.</param>
        /// <returns>Setările încărcate sau o instanță nouă dacă fișierul nu există.</returns>
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
                // Log error: Corrupt settings file
                return new T();
            }
        }

        /// <summary>
        /// Salvează setările pentru un modul.
        /// </summary>
        /// <typeparam name="T">Tipul modelului de setări.</typeparam>
        /// <param name="moduleName">Numele modulului.</param>
        /// <param name="settings">Obiectul de setări de salvat.</param>
        public async Task SaveSettingsAsync<T>(string moduleName, T settings)
        {
            string filePath = GetFilePath(moduleName);
            var options = new JsonSerializerOptions { WriteIndented = true };
            string jsonString = JsonSerializer.Serialize(settings, options);
            await File.WriteAllTextAsync(filePath, jsonString);
        }
    }
}
