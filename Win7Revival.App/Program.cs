using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;

namespace Win7Revival.App
{
    public static class Program
    {
        [DllImport("Microsoft.ui.xaml.dll")]
        private static extern void XamlCheckProcessRequirements();

        [STAThread]
        public static void Main(string[] args)
        {
            // Global crash handlers — must be registered before anything else
            AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            {
                WriteCrashLog("AppDomain.UnhandledException", e.ExceptionObject as Exception);
            };

            TaskScheduler.UnobservedTaskException += (_, e) =>
            {
                WriteCrashLog("TaskScheduler.UnobservedTaskException", e.Exception);
                e.SetObserved();
            };

            try
            {
                XamlCheckProcessRequirements();

                WinRT.ComWrappersSupport.InitializeComWrappers();
                Application.Start(p =>
                {
                    var context = new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(
                        Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread());
                    System.Threading.SynchronizationContext.SetSynchronizationContext(context);

                    new App();
                });
            }
            catch (Exception ex)
            {
                WriteCrashLog("Main entrypoint", ex);
                throw;
            }
        }

        private static void WriteCrashLog(string source, Exception? ex)
        {
            try
            {
                var fileName = $"crash_{DateTime.Now:yyyyMMdd_HHmmss}.log";

                var content = $"""
                    Win7Revival Crash Report
                    ========================
                    Time: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}
                    Source: {source}
                    OS: {Environment.OSVersion}
                    64-bit: {Environment.Is64BitProcess}
                    CLR: {Environment.Version}
                    Admin: {IsAdmin()}
                    CommandLine: {Environment.CommandLine}
                    BaseDirectory: {AppContext.BaseDirectory}

                    Exception:
                    {ex ?? (object)"(null)"}
                    """;

                // Try multiple locations in order of preference
                string[] paths =
                [
                    Path.Combine(AppContext.BaseDirectory, fileName),
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                        "Win7Revival", "logs", fileName),
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Desktop), fileName)
                ];

                foreach (var logPath in paths)
                {
                    try
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
                        File.WriteAllText(logPath, content);
                        break; // Written successfully
                    }
                    catch
                    {
                        // Try next location
                    }
                }

                // Also try AppLogger if it's available
                try
                {
                    AppLogger.LogException(ex!, source);
                }
                catch
                {
                    // AppLogger may not be initialized yet
                }
            }
            catch
            {
                // Last resort — nothing we can do
            }
        }

        private static string IsAdmin()
        {
            try
            {
                using var identity = System.Security.Principal.WindowsIdentity.GetCurrent();
                var principal = new System.Security.Principal.WindowsPrincipal(identity);
                return principal.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator).ToString();
            }
            catch
            {
                return "unknown";
            }
        }
    }
}
