using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

namespace CrystalFrame.Dashboard
{
    /// <summary>
    /// Manages the lifecycle of CrystalFrame.Core.exe.
    /// Dashboard is the single user-facing entry point; Core runs as an
    /// independent background process that persists after Dashboard closes.
    /// </summary>
    public class CoreProcessManager : IDisposable
    {
        private const string CoreProcessName = "CrystalFrame.Core";
        private const string CoreExeName = "CrystalFrame.Core.exe";
        private const int ShutdownTimeoutMs = 3000;

        private readonly IpcClient _ipc;
        private Process _coreProcess;

        public event EventHandler<bool> CoreRunningChanged;

        public bool IsRunning => FindCoreProcess() != null;

        public CoreProcessManager(IpcClient ipc)
        {
            _ipc = ipc;
        }

        /// <summary>
        /// Ensures Core is running. If not already active, launches it.
        /// Called automatically at Dashboard startup.
        /// </summary>
        public bool EnsureCoreRunning()
        {
            var existing = FindCoreProcess();
            if (existing != null)
            {
                Debug.WriteLine("Core already running (PID: " + existing.Id + ")");
                CoreRunningChanged?.Invoke(this, true);
                return true;
            }

            return StartCore();
        }

        /// <summary>
        /// Starts Core.exe from the same directory as Dashboard.
        /// </summary>
        public bool StartCore()
        {
            try
            {
                var corePath = GetCoreExePath();
                if (!File.Exists(corePath))
                {
                    Debug.WriteLine("Core executable not found: " + corePath);
                    return false;
                }

                var startInfo = new ProcessStartInfo
                {
                    FileName = corePath,
                    WorkingDirectory = Path.GetDirectoryName(corePath),
                    UseShellExecute = false,
                    CreateNoWindow = true
                };

                _coreProcess = Process.Start(startInfo);

                if (_coreProcess == null || _coreProcess.HasExited)
                {
                    Debug.WriteLine("Failed to start Core process");
                    return false;
                }

                // Don't block Dashboard exit — Core is independent
                _coreProcess.EnableRaisingEvents = true;
                _coreProcess.Exited += (s, e) =>
                {
                    Debug.WriteLine("Core process exited");
                    CoreRunningChanged?.Invoke(this, false);
                };

                Debug.WriteLine("Core started (PID: " + _coreProcess.Id + ")");
                CoreRunningChanged?.Invoke(this, true);
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine("Failed to start Core: " + ex.Message);
                return false;
            }
        }

        /// <summary>
        /// Stops Core gracefully via IPC Shutdown command.
        /// Falls back to Process.Kill if Core doesn't respond in time.
        /// </summary>
        public async Task<bool> StopCoreAsync()
        {
            try
            {
                // Try graceful shutdown via IPC
                if (_ipc.IsConnected)
                {
                    await _ipc.SendCommandAsync("Shutdown", new { });

                    // Wait for Core to exit
                    var process = FindCoreProcess();
                    if (process != null)
                    {
                        bool exited = await Task.Run(() =>
                            process.WaitForExit(ShutdownTimeoutMs));

                        if (exited)
                        {
                            Debug.WriteLine("Core stopped gracefully");
                            CoreRunningChanged?.Invoke(this, false);
                            return true;
                        }
                    }
                    else
                    {
                        // Already gone
                        CoreRunningChanged?.Invoke(this, false);
                        return true;
                    }
                }

                // Fallback: force kill
                return ForceStopCore();
            }
            catch (Exception ex)
            {
                Debug.WriteLine("StopCore error: " + ex.Message);
                return ForceStopCore();
            }
        }

        private bool ForceStopCore()
        {
            try
            {
                var process = FindCoreProcess();
                if (process != null && !process.HasExited)
                {
                    process.Kill();
                    process.WaitForExit(2000);
                    Debug.WriteLine("Core force-stopped");
                }

                CoreRunningChanged?.Invoke(this, false);
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine("Force stop failed: " + ex.Message);
                return false;
            }
        }

        private Process FindCoreProcess()
        {
            try
            {
                var processes = Process.GetProcessesByName(CoreProcessName);
                return processes.Length > 0 ? processes[0] : null;
            }
            catch
            {
                return null;
            }
        }

        private string GetCoreExePath()
        {
            // Core.exe is in the same directory as Dashboard.exe
            var dashboardDir = AppContext.BaseDirectory;
            return Path.Combine(dashboardDir, CoreExeName);
        }

        public void Dispose()
        {
            // Do NOT stop Core on Dashboard close — Core continues independently
            _coreProcess?.Dispose();
        }
    }
}
