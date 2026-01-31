using System.Collections.Generic;

namespace Win7Revival.Modules.StartMenu.Services
{
    /// <summary>
    /// Lista hardcodată de categorii Control Panel cu display name, glyph Segoe MDL2, și argument control.exe.
    /// </summary>
    public class ControlPanelItem
    {
        public string DisplayName { get; set; } = string.Empty;
        public string Glyph { get; set; } = "\uE713";
        public string ControlName { get; set; } = string.Empty;
    }

    public static class ControlPanelItems
    {
        public static List<ControlPanelItem> GetAll() => new()
        {
            new() { DisplayName = "System", Glyph = "\uE7F8", ControlName = "Microsoft.System" },
            new() { DisplayName = "Network and Sharing", Glyph = "\uE968", ControlName = "Microsoft.NetworkAndSharingCenter" },
            new() { DisplayName = "Sound", Glyph = "\uE767", ControlName = "Microsoft.Sound" },
            new() { DisplayName = "Display", Glyph = "\uE7F4", ControlName = "Microsoft.Display" },
            new() { DisplayName = "Power Options", Glyph = "\uE945", ControlName = "Microsoft.PowerOptions" },
            new() { DisplayName = "Programs and Features", Glyph = "\uE74C", ControlName = "Microsoft.ProgramsAndFeatures" },
            new() { DisplayName = "User Accounts", Glyph = "\uE77B", ControlName = "Microsoft.UserAccounts" },
            new() { DisplayName = "Windows Firewall", Glyph = "\uE72E", ControlName = "Microsoft.WindowsFirewall" },
            new() { DisplayName = "Device Manager", Glyph = "\uE772", ControlName = "Microsoft.DeviceManager" },
            new() { DisplayName = "Mouse", Glyph = "\uE962", ControlName = "Microsoft.Mouse" },
            new() { DisplayName = "Keyboard", Glyph = "\uE765", ControlName = "Microsoft.Keyboard" },
            new() { DisplayName = "Region", Glyph = "\uE909", ControlName = "Microsoft.RegionAndLanguage" },
            new() { DisplayName = "Date and Time", Glyph = "\uE787", ControlName = "Microsoft.DateAndTime" },
            new() { DisplayName = "Fonts", Glyph = "\uE8D2", ControlName = "Microsoft.Fonts" },
        };
    }
}
