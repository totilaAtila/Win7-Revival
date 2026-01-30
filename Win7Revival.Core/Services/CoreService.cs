using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Win7Revival.Core.Interfaces;

namespace Win7Revival.Core.Services
{
    /// <summary>
    /// Serviciu central care gestionează încărcarea și ciclul de viață al modulelor.
    /// </summary>
    public class CoreService
    {
        private readonly List<IModule> _modules = new List<IModule>();
        private readonly SettingsService _settingsService;

        public IReadOnlyList<IModule> Modules => _modules.AsReadOnly();

        public CoreService(SettingsService settingsService)
        {
            _settingsService = settingsService;
        }

        /// <summary>
        /// Înregistrează un modul în sistem.
        /// </summary>
        public void RegisterModule(IModule module)
        {
            if (!_modules.Any(m => m.Name == module.Name))
            {
                _modules.Add(module);
            }
        }

        /// <summary>
        /// Inițializează toate modulele înregistrate.
        /// </summary>
        public async Task InitializeModulesAsync()
        {
            foreach (var module in _modules)
            {
                try
                {
                    await module.InitializeAsync();
                    // Load initial state from settings and enable if needed
                    var settings = await _settingsService.LoadSettingsAsync<Models.ModuleSettings>(module.Name);
                    if (settings.IsEnabled)
                    {
                        // Note: A proper implementation would need to pass the loaded settings to the module
                        // and call EnableAsync only if the module's internal state (which should be updated by settings) 
                        // indicates it's not already enabled. For this basic structure, we assume the module handles its state.
                        await EnableModuleAsync(module.Name);
                    }
                }
                catch (Exception ex)
                {
                    // Log error: Failed to initialize module
                    Console.WriteLine($"Eroare la inițializarea modulului {module.Name}: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Activează un modul.
        /// </summary>
        public async Task EnableModuleAsync(string moduleName)
        {
            var module = _modules.FirstOrDefault(m => m.Name == moduleName);
            if (module != null && !module.IsEnabled)
            {
                try
                {
                    await module.EnableAsync();
                    // Save state
                    await module.SaveSettingsAsync();
                }
                catch (Exception ex)
                {
                    // Log error: Failed to enable module
                    Console.WriteLine($"Eroare la activarea modulului {module.Name}: {ex.Message}");
                    // Attempt to disable to clean up
                    await DisableModuleAsync(moduleName);
                }
            }
        }

        /// <summary>
        /// Dezactivează un modul.
        /// </summary>
        public async Task DisableModuleAsync(string moduleName)
        {
            var module = _modules.FirstOrDefault(m => m.Name == moduleName);
            if (module != null && module.IsEnabled)
            {
                try
                {
                    await module.DisableAsync();
                    // Save state
                    await module.SaveSettingsAsync();
                }
                catch (Exception ex)
                {
                    // Log error: Failed to disable module
                    Console.WriteLine($"Eroare la dezactivarea modulului {module.Name}: {ex.Message}");
                }
            }
        }
    }
}
