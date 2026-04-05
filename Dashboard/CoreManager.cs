using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace GlassBar.Dashboard
{
    /// <summary>
    /// Manages GlassBar Core engine via direct native calls.
    /// Core runs in-process as a native DLL.
    /// </summary>
    public class CoreManager : IDisposable
    {
        private Thread? _messageThread;
        private volatile bool _running;
        private bool _disposed;

        public event EventHandler<bool>? CoreRunningChanged;
        public event EventHandler<CoreNative.CoreStatus>? StatusUpdated;

        public bool IsRunning => _running;

        /// <summary>
        /// Initialize and start the Core engine
        /// </summary>
        public bool Initialize()
        {
            if (_running)
            {
                Debug.WriteLine("[CoreManager] Already initialized");
                return true;
            }

            try
            {
                Debug.WriteLine("[CoreManager] Initializing Core engine...");

                if (!CoreNative.CoreInitialize())
                {
                    Debug.WriteLine("[CoreManager] CoreInitialize() failed");
                    return false;
                }

                _running = true;

                // Start message processing thread
                _messageThread = new Thread(MessagePumpThread)
                {
                    IsBackground = true,
                    Name = "Core Message Pump"
                };
                _messageThread.Start();

                CoreRunningChanged?.Invoke(this, true);

                Debug.WriteLine("[CoreManager] Core engine initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[CoreManager] Initialize exception: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Shutdown the Core engine
        /// </summary>
        public void Shutdown()
        {
            if (!_running)
                return;

            Debug.WriteLine("[CoreManager] Shutting down Core engine...");

            _running = false;

            // Wait for message thread to exit
            if (_messageThread != null && _messageThread.IsAlive)
            {
                bool joined = _messageThread.Join(2000);
                if (!joined)
                {
                    Debug.WriteLine("[CoreManager] Message thread did not exit in time, forcing shutdown");
                }
            }

            CoreNative.CoreShutdown();

            CoreRunningChanged?.Invoke(this, false);

            Debug.WriteLine("[CoreManager] Core engine shutdown complete");
        }

        /// <summary>
        /// Set taskbar opacity (0-100)
        /// </summary>
        public void SetTaskbarOpacity(int opacity)
        {
            if (!_running)
            {
                Debug.WriteLine("[CoreManager] SetTaskbarOpacity: Core not running");
                return;
            }

            Debug.WriteLine($"[CoreManager] SetTaskbarOpacity({opacity})");
            CoreNative.CoreSetTaskbarOpacity(opacity);
        }

        /// <summary>
        /// Set start menu opacity (0-100)
        /// </summary>
        public void SetStartOpacity(int opacity)
        {
            if (!_running)
                return;

            CoreNative.CoreSetStartOpacity(opacity);
        }

        /// <summary>
        /// Enable/disable taskbar transparency
        /// </summary>
        public void SetTaskbarEnabled(bool enabled)
        {
            if (!_running)
            {
                Debug.WriteLine("[CoreManager] SetTaskbarEnabled: Core not running");
                return;
            }

            Debug.WriteLine($"[CoreManager] SetTaskbarEnabled({enabled})");
            CoreNative.CoreSetTaskbarEnabled(enabled);
        }

        /// <summary>
        /// Enable/disable start menu transparency
        /// </summary>
        public void SetStartEnabled(bool enabled)
        {
            if (!_running)
                return;

            CoreNative.CoreSetStartEnabled(enabled);
        }

        /// <summary>
        /// Set taskbar color (RGB 0-255)
        /// </summary>
        public void SetTaskbarColor(int r, int g, int b)
        {
            if (!_running)
            {
                Debug.WriteLine("[CoreManager] SetTaskbarColor: Core not running");
                return;
            }

            Debug.WriteLine($"[CoreManager] SetTaskbarColor({r}, {g}, {b})");
            CoreNative.CoreSetTaskbarColor(r, g, b);
        }

        /// <summary>
        /// Set Start Menu window opacity (0-100)
        /// </summary>
        public void SetStartMenuOpacity(int opacity)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetStartMenuOpacity({opacity})");
            CoreNative.CoreSetStartMenuOpacity(opacity);
        }

        /// <summary>
        /// Set Start Menu background color (RGB 0-255)
        /// </summary>
        public void SetStartMenuBackgroundColor(int r, int g, int b)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetStartMenuBackgroundColor({r}, {g}, {b})");
            uint rgb = (uint)((r) | (g << 8) | (b << 16));
            CoreNative.CoreSetStartMenuBackgroundColor(rgb);
        }

        /// <summary>
        /// Set Start Menu text color (RGB 0-255)
        /// </summary>
        public void SetStartMenuTextColor(int r, int g, int b)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetStartMenuTextColor({r}, {g}, {b})");
            uint rgb = (uint)((r) | (g << 8) | (b << 16));
            CoreNative.CoreSetStartMenuTextColor(rgb);
        }

        /// <summary>
        /// Set Start Menu items visibility
        /// </summary>
        public void SetStartMenuItems(bool controlPanel, bool deviceManager, bool installedApps,
                                      bool documents, bool pictures, bool videos, bool recentFiles)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetStartMenuItems(CP:{controlPanel}, DM:{deviceManager}, IA:{installedApps}, D:{documents}, P:{pictures}, V:{videos}, RF:{recentFiles})");
            CoreNative.CoreSetStartMenuItems(controlPanel, deviceManager, installedApps, documents, pictures, videos, recentFiles);
        }

        /// <summary>S-B: Pin Start Menu open for Dashboard preview</summary>
        public void SetStartMenuPinned(bool pinned)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetStartMenuPinned({pinned})");
            CoreNative.CoreSetStartMenuPinned(pinned);
        }

        /// <summary>
        /// Set XamlBridge blur intensity (0 = off, 1-100 = intensity).
        /// On Win11 22H2+ this injects GlassBar.XamlBridge.dll into explorer.exe
        /// and applies acrylic blur from within the owner process.
        /// </summary>
        public void SetTaskbarBlurAmount(int amount)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetTaskbarBlurAmount({amount})");
            CoreNative.CoreSetTaskbarBlurAmount(amount);
        }

        /// <summary>Register a global hotkey that toggles the taskbar overlay.</summary>
        /// <param name="vk">Virtual-key code (e.g. (int)'G' = 0x47). Pass 0 to disable.</param>
        /// <param name="modifiers">MOD_CONTROL=2, MOD_ALT=1, MOD_SHIFT=4, MOD_WIN=8 (combinable).</param>
        public void RegisterHotkey(int vk, int modifiers)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] RegisterHotkey(vk=0x{vk:X2}, mod=0x{modifiers:X})");
            CoreNative.CoreRegisterHotkey(vk, modifiers);
        }

        /// <summary>Remove the global hotkey and clear it from config.</summary>
        public void UnregisterHotkey()
        {
            if (!_running) return;
            Debug.WriteLine("[CoreManager] UnregisterHotkey");
            CoreNative.CoreUnregisterHotkey();
        }

        /// <summary>S-E: Set explicit border/accent color</summary>
        public void SetStartMenuBorderColor(int r, int g, int b)
        {
            if (!_running) return;
            uint rgb = (uint)(r | (g << 8) | (b << 16));
            Debug.WriteLine($"[CoreManager] SetStartMenuBorderColor({r},{g},{b}) = 0x{rgb:X6}");
            CoreNative.CoreSetStartMenuBorderColor(rgb);
        }

        /// <summary>
        /// Enable/disable blur/acrylic effect on taskbar
        /// </summary>
        public void SetTaskbarBlur(bool enabled)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetTaskbarBlur({enabled})");
            CoreNative.CoreSetTaskbarBlur(enabled);
        }

        /// <summary>
        /// Enable/disable blur/acrylic effect on start menu
        /// </summary>
        public void SetStartBlur(bool enabled)
        {
            if (!_running) return;
            Debug.WriteLine($"[CoreManager] SetStartBlur({enabled})");
            CoreNative.CoreSetStartBlur(enabled);
        }

        /// <summary>
        /// Enable/disable custom Start Menu hook (intercepts Windows key and Start button clicks)
        /// </summary>
        public void SetStartMenuHookEnabled(bool enabled)
        {
            if (!_running)
            {
                Debug.WriteLine("[CoreManager] SetStartMenuHookEnabled: Core not running");
                return;
            }

            Debug.WriteLine($"[CoreManager] SetStartMenuHookEnabled({enabled})");
            CoreNative.CoreSetStartMenuHookEnabled(enabled);
        }

        /// <summary>
        /// Get current Core status
        /// </summary>
        public CoreNative.CoreStatus GetStatus()
        {
            var status = new CoreNative.CoreStatus();

            if (_running)
            {
                CoreNative.CoreGetStatus(ref status);
            }

            return status;
        }

        /// <summary>
        /// Background thread that processes Core messages
        /// </summary>
        private void MessagePumpThread()
        {
            Debug.WriteLine("[CoreManager] Message pump thread started");

            try
            {
                while (_running)
                {
                    // Process messages (non-blocking)
                    if (!CoreNative.CoreProcessMessages())
                    {
                        Debug.WriteLine("[CoreManager] CoreProcessMessages returned false, exiting");
                        break;
                    }

                    // Periodically notify status
                    var status = GetStatus();
                    StatusUpdated?.Invoke(this, status);

                    // Small sleep to prevent CPU spinning
                    Thread.Sleep(100);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[CoreManager] Message pump exception: {ex.Message}");
            }

            // If the thread exited on its own (CoreProcessMessages returned false),
            // we must still call CoreShutdown to clean up the native engine.
            if (_running)
            {
                try
                {
                    _running = false;
                    CoreNative.CoreShutdown();
                    CoreRunningChanged?.Invoke(this, false);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[CoreManager] Cleanup after unexpected exit failed: {ex.Message}");
                }
            }

            Debug.WriteLine("[CoreManager] Message pump thread exited");
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Shutdown();
            GC.SuppressFinalize(this);
        }
    }
}
