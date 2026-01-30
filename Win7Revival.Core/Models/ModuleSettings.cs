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

        [JsonPropertyName("opacity")]
        public int Opacity { get; set; } = 80;

        [JsonPropertyName("effect")]
        public EffectType Effect { get; set; } = EffectType.None;

        [JsonPropertyName("tintR")]
        public byte TintR { get; set; } = 0;

        [JsonPropertyName("tintG")]
        public byte TintG { get; set; } = 0;

        [JsonPropertyName("tintB")]
        public byte TintB { get; set; } = 0;
    }

    /// <summary>
    /// Tipuri de efecte vizuale disponibile pentru taskbar.
    /// Mapează direct pe ACCENT_STATE din Win32.
    /// </summary>
    [JsonConverter(typeof(JsonStringEnumConverter))]
    public enum EffectType
    {
        None = 0,
        Blur = 1,
        Acrylic = 2,
        Mica = 3
    }
}
