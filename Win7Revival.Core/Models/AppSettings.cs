using System.Text.Json.Serialization;

namespace Win7Revival.Core.Models
{
    /// <summary>
    /// Setări globale ale aplicației (limbă, preferințe UI etc.).
    /// Persistat ca JSON prin SettingsService.
    /// </summary>
    public class AppSettings
    {
        [JsonPropertyName("language")]
        public string Language { get; set; } = "English";
    }
}
