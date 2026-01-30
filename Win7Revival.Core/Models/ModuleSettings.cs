using System.Text.Json.Serialization;

namespace Win7Revival.Core.Models
{
    /// <summary>
    /// Model de date pentru a persista starea unui modul.
    /// </summary>
    public class ModuleSettings
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = string.Empty;

        [JsonPropertyName("isEnabled")]
        public bool IsEnabled { get; set; } = false;
    }
}
