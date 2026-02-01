using System;
using System.Diagnostics;
using System.IO;

namespace Win7Revival.App
{
    /// <summary>
    /// Static logger that writes to %LocalAppData%/Win7Revival/logs/ and Debug output.
    /// </summary>
    public static class AppLogger
    {
        private static readonly string _logFilePath;
        private static readonly object _lock = new();

        static AppLogger()
        {
            var logDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Win7Revival", "logs");

            Directory.CreateDirectory(logDir);

            _logFilePath = Path.Combine(logDir, $"app_{DateTime.Now:yyyyMMdd_HHmmss}.log");
        }

        public static void Log(string message)
        {
            var line = $"[{DateTime.Now:HH:mm:ss.fff}] {message}";
            Debug.WriteLine($"[AppLogger] {message}");

            lock (_lock)
            {
                try
                {
                    File.AppendAllText(_logFilePath, line + Environment.NewLine);
                }
                catch
                {
                    // Best-effort logging — don't crash the app
                }
            }
        }

        public static void LogException(Exception ex, string context = "")
        {
            var prefix = string.IsNullOrEmpty(context) ? "" : $"{context}: ";
            Log($"EXCEPTION {prefix}{ex.GetType().Name}: {ex.Message}");
            Log($"  StackTrace: {ex.StackTrace}");
        }
    }
}
