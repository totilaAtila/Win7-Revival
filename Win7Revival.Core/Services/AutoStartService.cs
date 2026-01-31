using System;
using System.Diagnostics;
using System.Runtime.Versioning;
using Microsoft.Win32;

namespace Win7Revival.Core.Services
{
    /// <summary>
    /// Gestionează pornirea automată a aplicației la boot-ul Windows.
    /// Folosește HKCU\Software\Microsoft\Windows\CurrentVersion\Run
    /// (nu necesită drepturi de administrator).
    /// </summary>
    [SupportedOSPlatform("windows")]
    public static class AutoStartService
    {
        private const string RegistryKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
        private const string AppName = "Win7Revival";

        /// <summary>
        /// Verifică dacă auto-start este activat.
        /// </summary>
        public static bool IsEnabled()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RegistryKeyPath, false);
                return key?.GetValue(AppName) != null;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[AutoStartService] Failed to read registry: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Activează pornirea automată. Scrie calea executabilului în registry.
        /// </summary>
        public static bool Enable()
        {
            try
            {
                string exePath = Environment.ProcessPath
                    ?? Process.GetCurrentProcess().MainModule?.FileName
                    ?? throw new InvalidOperationException("Cannot determine executable path.");

                using var key = Registry.CurrentUser.OpenSubKey(RegistryKeyPath, true)
                    ?? throw new InvalidOperationException("Cannot open Run registry key for writing.");

                key.SetValue(AppName, $"\"{exePath}\" --minimized");
                Debug.WriteLine($"[AutoStartService] Enabled. Path: {exePath}");
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[AutoStartService] Failed to enable: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Dezactivează pornirea automată. Șterge intrarea din registry.
        /// </summary>
        public static bool Disable()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RegistryKeyPath, true);
                if (key?.GetValue(AppName) != null)
                {
                    key.DeleteValue(AppName, false);
                    Debug.WriteLine("[AutoStartService] Disabled.");
                }
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[AutoStartService] Failed to disable: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Toggle: activează sau dezactivează pe baza stării curente.
        /// Returnează noua stare.
        /// </summary>
        public static bool Toggle()
        {
            if (IsEnabled())
            {
                if (!Disable()) return true; // failed to disable, still enabled
                return false;
            }
            else
            {
                if (!Enable()) return false; // failed to enable, still disabled
                return true;
            }
        }
    }
}
