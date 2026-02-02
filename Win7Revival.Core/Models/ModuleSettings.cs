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

        [JsonPropertyName("renderMode")]
        public RenderMode RenderMode { get; set; } = RenderMode.Auto;
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
        Mica = 3,
        Glass = 4
    }

    /// <summary>
    /// Modul de randare al efectelor pe taskbar.
    /// Auto detectează build-ul Windows și alege automat.
    /// </summary>
    [JsonConverter(typeof(JsonStringEnumConverter))]
    public enum RenderMode
    {
        /// <summary>Detectare automată: Overlay pe Win11 22H2+, Legacy pe Win10.</summary>
        Auto = 0,
        /// <summary>Overlay propriu cu API-uri DWM documentate (stabil la Windows updates).</summary>
        Overlay = 1,
        /// <summary>SetWindowCompositionAttribute direct pe Shell_TrayWnd (nedocumentat).</summary>
        Legacy = 2
    }
}
