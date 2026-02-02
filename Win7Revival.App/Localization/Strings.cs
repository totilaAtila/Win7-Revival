using System.Collections.Generic;

namespace Win7Revival.App.Localization
{
    /// <summary>
    /// Supported UI languages.
    /// </summary>
    public enum AppLanguage
    {
        English = 0,
        Română = 1
    }

    /// <summary>
    /// Simple string table for UI localization (EN / RO).
    /// Keys are stable identifiers; values are display strings per language.
    /// </summary>
    public static class Strings
    {
        private static readonly Dictionary<string, string> En = new()
        {
            // Header
            ["AppTitle"] = "Win7 Revival",
            ["AppSubtitle"] = "Windows 11 Customization Toolkit",
            ["AdminWarningTitle"] = "Non-Admin",
            ["AdminWarningMessage"] = "Limited functionality without administrator rights.",

            // Taskbar module
            ["TaskbarTitle"] = "Taskbar Transparent",
            ["TaskbarDescription"] = "Transparency, blur, acrylic, mica or glass effects on the Taskbar.",
            ["EffectType"] = "Effect Type",
            ["EffectBlur"] = "Blur (Aero Glass)",
            ["EffectAcrylic"] = "Acrylic",
            ["EffectMica"] = "Mica (Host Backdrop)",
            ["EffectGlass"] = "Glass (No Blur)",
            ["RenderMode"] = "Render Mode",
            ["RenderModeAuto"] = "Auto (recommended)",
            ["RenderModeOverlay"] = "Overlay (DWM)",
            ["RenderModeLegacy"] = "Legacy (in-place)",
            ["Opacity"] = "Opacity",
            ["ColorTint"] = "Color Tint",
            ["DiagTaskbarDetecting"] = "Taskbar: detecting...",
            ["DiagMonitorsDetecting"] = "Monitors: detecting...",
            ["DiagTaskbarNotDetected"] = "Taskbar: not detected",
            ["DiagMonitorsUnknown"] = "Monitors: unknown",
            ["DiagMonitorsNone"] = "Monitors: none detected",

            // Start Menu module
            ["StartMenuTitle"] = "Classic Start Menu",
            ["StartMenuDescription"] = "Windows 7-style start menu with Control Panel access, search, and Win key interception.",
            ["InterceptWinKey"] = "Intercept Win Key",
            ["InterceptWinKeyDescription"] = "Press Win key to open Classic Start Menu instead of Windows 11 Start.",
            ["ErrorToggleStartMenu"] = "Failed to toggle Classic Start Menu",
            ["ThemeEngineTitle"] = "Theme Engine (Planned)",
            ["ThemeEngineDescription"] = "Color schemes, icon packs, fonts, and sound schemes will be configurable here.",

            // General
            ["General"] = "General",
            ["Language"] = "Language",
            ["LanguageDescription"] = "Select the display language for the application.",
            ["AutoStart"] = "Start with Windows",
            ["AutoStartDescription"] = "Automatically start the application at boot (minimized to tray).",

            // Tab headers
            ["TabTaskbar"] = "Taskbar",
            ["TabStartMenu"] = "Start Menu",
            ["TabThemeEngine"] = "Theme Engine",
            ["TabGeneral"] = "General",
            ["TabHelp"] = "Help / About",

            // Help tab
            ["HelpAbout"] = "A modular toolkit to bring back the Windows 7 look & usability on Windows 11.",
            ["HelpModules"] = "Modules: Taskbar (live), Classic Start Menu (live), Theme Engine (planned).",
            ["HelpSupport"] = "Support: Internal dev team. For issues, create a ticket on the repo.",

            // Footer / Buttons
            ["ApplyAndSave"] = "Apply & Save",
            ["Reset"] = "Reset",
            ["MinimizeToTray"] = "Minimize to Tray",

            // Dialogs
            ["SettingsSavedTitle"] = "Settings Saved",
            ["SettingsSavedMessage"] = "Settings have been saved successfully.",
            ["ErrorTitle"] = "Error",
            ["ErrorToggleTaskbar"] = "Failed to toggle Taskbar Transparent",
            ["ErrorAutoStart"] = "Failed to change auto-start setting. Check your permissions.",
            ["OK"] = "OK",

            // Tray
            ["TrayTooltip"] = "Win7 Revival",
            ["TrayShowSettings"] = "Show Settings",
            ["TrayExit"] = "Exit",
        };

        private static readonly Dictionary<string, string> Ro = new()
        {
            // Header
            ["AppTitle"] = "Win7 Revival",
            ["AppSubtitle"] = "Toolkit de personalizare Windows 11",
            ["AdminWarningTitle"] = "Non-Admin",
            ["AdminWarningMessage"] = "Funcționalitate limitată fără drepturi de administrator.",

            // Taskbar module
            ["TaskbarTitle"] = "Taskbar Transparent",
            ["TaskbarDescription"] = "Efecte de transparență, blur, acrylic, mica sau glass pe Taskbar.",
            ["EffectType"] = "Tip efect",
            ["EffectBlur"] = "Blur (Aero Glass)",
            ["EffectAcrylic"] = "Acrylic",
            ["EffectMica"] = "Mica (Host Backdrop)",
            ["EffectGlass"] = "Glass (Fără Blur)",
            ["RenderMode"] = "Mod de randare",
            ["RenderModeAuto"] = "Auto (recomandat)",
            ["RenderModeOverlay"] = "Overlay (DWM)",
            ["RenderModeLegacy"] = "Legacy (in-place)",
            ["Opacity"] = "Opacitate",
            ["ColorTint"] = "Nuanță culoare",
            ["DiagTaskbarDetecting"] = "Taskbar: se detectează...",
            ["DiagMonitorsDetecting"] = "Monitoare: se detectează...",
            ["DiagTaskbarNotDetected"] = "Taskbar: nedetectat",
            ["DiagMonitorsUnknown"] = "Monitoare: necunoscut",
            ["DiagMonitorsNone"] = "Monitoare: nedetectate",

            // Start Menu module
            ["StartMenuTitle"] = "Meniu Start Clasic",
            ["StartMenuDescription"] = "Meniu Start în stil Windows 7 cu acces la Control Panel, căutare și interceptare tastă Win.",
            ["InterceptWinKey"] = "Interceptare tastă Win",
            ["InterceptWinKeyDescription"] = "Apasă tasta Win pentru a deschide Meniul Start Clasic în loc de cel din Windows 11.",
            ["ErrorToggleStartMenu"] = "Nu s-a putut comuta Meniul Start Clasic",
            ["ThemeEngineTitle"] = "Motor de teme (Planificat)",
            ["ThemeEngineDescription"] = "Scheme de culori, pachete de iconuri, fonturi și scheme de sunet vor fi configurabile aici.",

            // General
            ["General"] = "General",
            ["Language"] = "Limbă",
            ["LanguageDescription"] = "Selectează limba de afișare a aplicației.",
            ["AutoStart"] = "Pornire cu Windows",
            ["AutoStartDescription"] = "Pornește automat aplicația la boot (minimizat în tray).",

            // Tab headers
            ["TabTaskbar"] = "Taskbar",
            ["TabStartMenu"] = "Meniu Start",
            ["TabThemeEngine"] = "Motor de teme",
            ["TabGeneral"] = "General",
            ["TabHelp"] = "Ajutor / Despre",

            // Help tab
            ["HelpAbout"] = "Un toolkit modular pentru a readuce aspectul și funcționalitatea Windows 7 pe Windows 11.",
            ["HelpModules"] = "Module: Taskbar (activ), Meniu Start Clasic (activ), Motor de teme (planificat).",
            ["HelpSupport"] = "Suport: Echipă internă de dezvoltare. Pentru probleme, creează un tichet pe repo.",

            // Footer / Buttons
            ["ApplyAndSave"] = "Aplică și salvează",
            ["Reset"] = "Resetează",
            ["MinimizeToTray"] = "Minimizează în Tray",

            // Dialogs
            ["SettingsSavedTitle"] = "Setări salvate",
            ["SettingsSavedMessage"] = "Setările au fost salvate cu succes.",
            ["ErrorTitle"] = "Eroare",
            ["ErrorToggleTaskbar"] = "Nu s-a putut comuta Taskbar Transparent",
            ["ErrorAutoStart"] = "Nu s-a putut modifica pornirea automată. Verifică permisiunile.",
            ["OK"] = "OK",

            // Tray
            ["TrayTooltip"] = "Win7 Revival",
            ["TrayShowSettings"] = "Afișează setările",
            ["TrayExit"] = "Ieșire",
        };

        private static Dictionary<string, string> _current = En;

        /// <summary>
        /// Gets or sets the active language. Changing this updates all future Get() calls.
        /// </summary>
        public static AppLanguage Current { get; private set; } = AppLanguage.English;

        /// <summary>
        /// Switches the active language.
        /// </summary>
        public static void SetLanguage(AppLanguage language)
        {
            Current = language;
            _current = language switch
            {
                AppLanguage.Română => Ro,
                _ => En
            };
        }

        /// <summary>
        /// Gets a localized string by key. Returns the key itself if not found.
        /// </summary>
        public static string Get(string key)
        {
            return _current.TryGetValue(key, out var value) ? value : key;
        }
    }
}
