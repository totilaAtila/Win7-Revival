using System.Text.Json.Serialization;
using Win7Revival.Core.Models;

namespace Win7Revival.Modules.StartMenu.Models
{
    /// <summary>
    /// Setările modulului Classic Start Menu.
    /// Persistate în %AppData%/Win7Revival/Classic_Start_Menu.json.
    /// </summary>
    public class StartMenuSettings
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = string.Empty;

        [JsonPropertyName("isEnabled")]
        public bool IsEnabled { get; set; }

        [JsonPropertyName("interceptWinKey")]
        public bool InterceptWinKey { get; set; } = true;

        [JsonPropertyName("effect")]
        public EffectType Effect { get; set; } = EffectType.Blur;

        [JsonPropertyName("opacity")]
        public int Opacity { get; set; } = 85;

        [JsonPropertyName("tintR")]
        public byte TintR { get; set; }

        [JsonPropertyName("tintG")]
        public byte TintG { get; set; }

        [JsonPropertyName("tintB")]
        public byte TintB { get; set; }

        [JsonPropertyName("pinnedPrograms")]
        public List<string> PinnedPrograms { get; set; } = new();

        [JsonPropertyName("maxRecentPrograms")]
        public int MaxRecentPrograms { get; set; } = 10;
    }
}
