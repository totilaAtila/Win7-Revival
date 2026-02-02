using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace Win7Revival.Modules.StartMenu.Services
{
    /// <summary>
    /// Model pentru un program descoperit din Start Menu shortcuts.
    /// </summary>
    public class ProgramEntry
    {
        public string DisplayName { get; set; } = string.Empty;
        public string Path { get; set; } = string.Empty;
    }

    /// <summary>
    /// Descoperă programele instalate scanând folderele Start Menu
    /// pentru fișiere .lnk (shortcuts).
    /// </summary>
    public static class ProgramDiscovery
    {
        private static List<ProgramEntry>? _cachedPrograms;

        /// <summary>
        /// Scanează și returnează toate programele din Start Menu.
        /// </summary>
        public static List<ProgramEntry> GetAllPrograms()
        {
            if (_cachedPrograms != null) return _cachedPrograms;

            var programs = new Dictionary<string, ProgramEntry>(StringComparer.OrdinalIgnoreCase);

            // Scan common Start Menu (all users)
            var commonPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu),
                "Programs");
            ScanDirectory(commonPath, programs);

            // Scan user Start Menu
            var userPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),
                "Programs");
            ScanDirectory(userPath, programs);

            _cachedPrograms = programs.Values
                .OrderBy(p => p.DisplayName, StringComparer.OrdinalIgnoreCase)
                .ToList();

            Debug.WriteLine($"[ProgramDiscovery] Found {_cachedPrograms.Count} programs.");
            return _cachedPrograms;
        }

        /// <summary>
        /// Filtreaza programele dupa un termen de cautare.
        /// </summary>
        public static List<ProgramEntry> Search(string query)
        {
            if (string.IsNullOrWhiteSpace(query)) return GetAllPrograms();

            return GetAllPrograms()
                .Where(p => p.DisplayName.Contains(query, StringComparison.OrdinalIgnoreCase))
                .ToList();
        }

        /// <summary>
        /// Invalideaza cache-ul (e.g., dupa install/uninstall program).
        /// </summary>
        public static void RefreshCache()
        {
            _cachedPrograms = null;
        }

        private static void ScanDirectory(string path, Dictionary<string, ProgramEntry> programs)
        {
            if (!Directory.Exists(path)) return;

            try
            {
                foreach (var file in Directory.EnumerateFiles(path, "*.lnk", SearchOption.AllDirectories))
                {
                    var name = System.IO.Path.GetFileNameWithoutExtension(file);

                    // Exclude common uninstaller and help entries
                    if (name.Contains("Uninstall", StringComparison.OrdinalIgnoreCase) ||
                        name.Contains("Help", StringComparison.OrdinalIgnoreCase) ||
                        name.Contains("Documentation", StringComparison.OrdinalIgnoreCase) ||
                        name.Contains("Readme", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    if (!programs.ContainsKey(name))
                    {
                        programs[name] = new ProgramEntry
                        {
                            DisplayName = name,
                            Path = file
                        };
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ProgramDiscovery] Error scanning {path}: {ex.Message}");
            }
        }
    }
}
