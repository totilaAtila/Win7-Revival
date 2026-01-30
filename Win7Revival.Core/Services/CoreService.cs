using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using Win7Revival.Core.Interfaces;

namespace Win7Revival.Core.Services
{
    /// <summary>
    /// Serviciu central care gestionează încărcarea și ciclul de viață al modulelor.
    /// Thread-safe prin lock pe operațiile de colecție.
    /// </summary>
    public class CoreService : IDisposable
    {
        private readonly object _lock = new();
        private readonly List<IModule> _modules = new();
        private readonly SettingsService _settingsService;
        private IReadOnlyList<IModule>? _modulesSnapshot;

        public IReadOnlyList<IModule> Modules
        {
            get { lock (_lock) { return _modulesSnapshot ??= _modules.ToList().AsReadOnly(); } }
        }

        public CoreService(SettingsService settingsService)
        {
            _settingsService = settingsService;
        }

        public void RegisterModule(IModule module)
        {
            lock (_lock)
            {
                if (!_modules.Any(m => m.Name == module.Name))
                {
                    _modules.Add(module);
                    _modulesSnapshot = null;
                }
            }
        }

        public async Task InitializeModulesAsync()
        {
            List<IModule> snapshot;
            lock (_lock) { snapshot = _modules.ToList(); }

            foreach (var module in snapshot)
            {
                try
                {
                    await module.InitializeAsync();
                    if (module.IsEnabled)
                    {
                        // Modulul și-a încărcat deja starea din settings în InitializeAsync.
                        // Activăm doar dacă settings zice enabled dar modulul nu e încă efectiv activ.
                        await module.EnableAsync();
                    }
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[CoreService] Eroare la inițializarea modulului {module.Name}: {ex.Message}");
                }
            }
        }

        public async Task EnableModuleAsync(string moduleName)
        {
            var module = FindModule(moduleName);
            if (module != null && !module.IsEnabled)
            {
                try
                {
                    await module.EnableAsync();
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[CoreService] Eroare la activarea modulului {module.Name}: {ex.Message}");
                    try { await module.DisableAsync(); }
                    catch (Exception cleanupEx)
                    {
                        Debug.WriteLine($"[CoreService] Eroare la cleanup după activare eșuată: {cleanupEx.Message}");
                    }
                    throw;
                }
            }
        }

        public async Task DisableModuleAsync(string moduleName)
        {
            var module = FindModule(moduleName);
            if (module != null && module.IsEnabled)
            {
                try
                {
                    await module.DisableAsync();
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[CoreService] Eroare la dezactivarea modulului {module.Name}: {ex.Message}");
                }
            }
        }

        private IModule? FindModule(string moduleName)
        {
            lock (_lock) { return _modules.FirstOrDefault(m => m.Name == moduleName); }
        }

        public void Dispose()
        {
            List<IModule> snapshot;
            lock (_lock) { snapshot = _modules.ToList(); }

            foreach (var module in snapshot)
            {
                if (module is IDisposable disposable)
                {
                    try { disposable.Dispose(); }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"[CoreService] Eroare la dispose pentru {module.Name}: {ex.Message}");
                    }
                }
            }
        }
    }
}
