using System;
using System.Diagnostics;
using System.Runtime.Versioning;
using Win7Revival.Modules.StartMenu.Interop;

namespace Win7Revival.Modules.StartMenu.Services
{
    /// <summary>
    /// Lansează resurse shell (foldere, Control Panel, shutdown, etc.)
    /// folosind doar API-uri publice Windows.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public static class ShellLauncher
    {
        public static void OpenDocuments() =>
            LaunchExplorer(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments));

        public static void OpenPictures() =>
            LaunchExplorer(Environment.GetFolderPath(Environment.SpecialFolder.MyPictures));

        public static void OpenMusic() =>
            LaunchExplorer(Environment.GetFolderPath(Environment.SpecialFolder.MyMusic));

        public static void OpenVideos() =>
            LaunchExplorer(Environment.GetFolderPath(Environment.SpecialFolder.MyVideos));

        public static void OpenDownloads() =>
            LaunchExplorer(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads"));

        public static void OpenComputer() =>
            LaunchExplorer("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");

        public static void OpenControlPanel() =>
            Launch("control.exe", "");

        public static void OpenControlPanelItem(string controlName) =>
            Launch("control.exe", $"/name {controlName}");

        public static void OpenDevicesAndPrinters() =>
            LaunchExplorer("shell:::{A8A91A66-3A7D-4424-8D24-04E180695C7A}");

        public static void OpenDefaultPrograms() =>
            Launch("ms-settings:defaultapps", "");

        public static void Shutdown() =>
            Launch("shutdown", "/s /t 0");

        public static void Restart() =>
            Launch("shutdown", "/r /t 0");

        public static void Sleep()
        {
            try
            {
                StartMenuInterop.SetSuspendState(false, true, true);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ShellLauncher] Sleep failed: {ex.Message}");
            }
        }

        public static void Lock()
        {
            try
            {
                StartMenuInterop.LockWorkStation();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ShellLauncher] Lock failed: {ex.Message}");
            }
        }

        public static void LogOff()
        {
            try
            {
                StartMenuInterop.ExitWindowsEx(StartMenuInterop.EWX_LOGOFF, 0);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ShellLauncher] Log off failed: {ex.Message}");
            }
        }

        public static void LaunchProgram(string path)
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = path,
                    UseShellExecute = true
                });
                Debug.WriteLine($"[ShellLauncher] Launched: {path}");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ShellLauncher] Failed to launch {path}: {ex.Message}");
            }
        }

        private static void LaunchExplorer(string argument)
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    Arguments = argument,
                    UseShellExecute = true
                });
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ShellLauncher] Explorer launch failed: {ex.Message}");
            }
        }

        private static void Launch(string fileName, string arguments)
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = fileName,
                    Arguments = arguments,
                    UseShellExecute = true
                });
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ShellLauncher] Launch failed ({fileName}): {ex.Message}");
            }
        }
    }
}
