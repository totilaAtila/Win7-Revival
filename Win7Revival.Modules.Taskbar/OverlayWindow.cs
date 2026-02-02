using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using Win7Revival.Core.Models;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Gestionează efectele de transparență/blur pe taskbar.
    /// Dual-mode:
    ///   Overlay — ferestre proprii cu DWM documented APIs (stabil la Windows updates).
    ///   Legacy — SetWindowCompositionAttribute direct pe Shell_TrayWnd (nedocumentat, fallback Win10).
    /// Modul se selectează automat din RenderMode (Auto detectează build-ul OS).
    /// </summary>
    public class OverlayWindow : IDisposable
    {
        private readonly TaskbarDetector _detector;
        private readonly object _lock = new();
        private ModuleSettings _settings;
        private bool _useOverlayMode;
        private bool _isActive;
        private bool _disposed;

        // --- Overlay mode state ---
        private Thread? _overlayThread;
        private IntPtr _timerOwnerHwnd;
        private readonly List<OverlayInfo> _overlays = new();
        private Win32Interop.WndProc? _wndProcDelegate; // prevent GC
        private ushort _classAtom;
        private const string OverlayClassName = "Win7Revival_TaskbarOverlay";
        private volatile bool _threadReady;

        // --- Legacy mode state ---
        private Timer? _reapplyTimer;

        public bool IsActive => _isActive;
        public bool IsOverlayMode => _useOverlayMode;

        /// <summary>Minimum Windows build that supports DWMWA_SYSTEMBACKDROP_TYPE.</summary>
        private const int MinBuildForBackdrop = 22621; // Win11 22H2

        /// <summary>Minimum Windows build for Win11 (basic DWM blur).</summary>
        private const int MinBuildForWin11 = 22000;

        public OverlayWindow(TaskbarDetector detector, ModuleSettings settings)
        {
            _detector = detector;
            _settings = settings;
            _useOverlayMode = ResolveRenderMode(settings.RenderMode);
        }

        private static bool ResolveRenderMode(RenderMode mode)
        {
            if (mode == RenderMode.Legacy) return false;
            if (mode == RenderMode.Overlay) return true;

            // Auto: overlay pe Win11+, legacy pe Win10
            int build = Environment.OSVersion.Version.Build;
            return build >= MinBuildForWin11;
        }

        // ================================================================
        // Public API (identic pentru ambele moduri)
        // ================================================================

        public void Apply()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(OverlayWindow));

            if (_useOverlayMode)
                ApplyOverlayMode();
            else
                ApplyLegacyMode();

            _isActive = true;
        }

        public void Remove()
        {
            if (_disposed) return;
            _isActive = false;

            if (_useOverlayMode)
                RemoveOverlayMode();
            else
                RemoveLegacyMode();
        }

        public void UpdateSettings(ModuleSettings newSettings)
        {
            _settings = newSettings;

            bool newOverlayMode = ResolveRenderMode(newSettings.RenderMode);
            if (newOverlayMode != _useOverlayMode && _isActive)
            {
                Remove();
                _useOverlayMode = newOverlayMode;
                Apply();
                return;
            }
            _useOverlayMode = newOverlayMode;

            if (!_isActive) return;

            if (_useOverlayMode)
                UpdateOverlayEffects();
            else
                ApplyLegacyMode();
        }

        /// <summary>
        /// Called from TaskbarModule when Explorer restarts.
        /// </summary>
        public void OnExplorerRestarted()
        {
            if (!_isActive) return;

            if (_useOverlayMode)
            {
                DestroyAllOverlays();
                CreateOverlaysForAllTaskbars();
            }
            else
            {
                ApplyLegacyMode();
            }
        }

        // ================================================================
        // OVERLAY MODE — ferestre proprii cu DWM
        // ================================================================

        private void ApplyOverlayMode()
        {
            _overlayThread = new Thread(OverlayThreadLoop)
            {
                Name = "Win7Revival.OverlayThread",
                IsBackground = true
            };
            _overlayThread.SetApartmentState(ApartmentState.STA);
            _overlayThread.Start();

            var sw = Stopwatch.StartNew();
            while (!_threadReady && sw.ElapsedMilliseconds < 3000)
                Thread.Sleep(10);

            Debug.WriteLine($"[OverlayWindow] Overlay mode started, thread ready: {_threadReady}");
        }

        private void RemoveOverlayMode()
        {
            if (_timerOwnerHwnd != IntPtr.Zero)
            {
                Win32Interop.PostMessage(_timerOwnerHwnd, Win32Interop.WM_QUIT, IntPtr.Zero, IntPtr.Zero);
            }
            _overlayThread?.Join(3000);
            _overlayThread = null;
            _threadReady = false;

            Debug.WriteLine("[OverlayWindow] Overlay mode stopped.");
        }

        private void OverlayThreadLoop()
        {
            var hInstance = Win32Interop.GetModuleHandle(null);

            _wndProcDelegate = OverlayWndProc;

            var wndClass = new Win32Interop.WNDCLASS
            {
                lpfnWndProc = _wndProcDelegate,
                hInstance = hInstance,
                lpszClassName = OverlayClassName
            };

            _classAtom = Win32Interop.RegisterClass(ref wndClass);
            if (_classAtom == 0)
            {
                Debug.WriteLine("[OverlayWindow] Failed to register overlay window class.");
                return;
            }

            _timerOwnerHwnd = Win32Interop.CreateWindowEx(
                0, OverlayClassName, "Win7Revival_TimerOwner",
                0, 0, 0, 0, 0,
                Win32Interop.HWND_MESSAGE, IntPtr.Zero, hInstance, IntPtr.Zero);

            if (_timerOwnerHwnd == IntPtr.Zero)
            {
                Debug.WriteLine("[OverlayWindow] Failed to create timer owner window.");
                return;
            }

            CreateOverlaysForAllTaskbars();

            Win32Interop.SetTimer(_timerOwnerHwnd, (UIntPtr)1, 100, IntPtr.Zero);

            _threadReady = true;

            while (Win32Interop.GetMessage(out var msg, IntPtr.Zero, 0, 0))
            {
                Win32Interop.TranslateMessage(ref msg);
                Win32Interop.DispatchMessage(ref msg);
            }

            // Cleanup
            Win32Interop.KillTimer(_timerOwnerHwnd, (UIntPtr)1);
            DestroyAllOverlays();
            Win32Interop.DestroyWindow(_timerOwnerHwnd);
            _timerOwnerHwnd = IntPtr.Zero;
            Win32Interop.UnregisterClass(OverlayClassName, hInstance);
        }

        private IntPtr OverlayWndProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam)
        {
            switch (msg)
            {
                case Win32Interop.WM_TIMER:
                    RepositionOverlays();
                    return IntPtr.Zero;

                case Win32Interop.WM_DISPLAYCHANGE:
                    Debug.WriteLine("[OverlayWindow] WM_DISPLAYCHANGE — rebuilding overlays.");
                    _detector.Refresh();
                    DestroyAllOverlays();
                    CreateOverlaysForAllTaskbars();
                    return IntPtr.Zero;
            }

            return Win32Interop.DefWindowProc(hwnd, msg, wParam, lParam);
        }

        private void CreateOverlaysForAllTaskbars()
        {
            foreach (var taskbarHandle in _detector.AllHandles)
            {
                if (!_detector.IsHandleValid(taskbarHandle)) continue;
                CreateSingleOverlay(taskbarHandle);
            }

            Debug.WriteLine($"[OverlayWindow] Created {_overlays.Count} overlay window(s).");
        }

        private void CreateSingleOverlay(IntPtr taskbarHandle)
        {
            if (!Win32Interop.GetWindowRect(taskbarHandle, out var rect)) return;

            int exStyle = Win32Interop.WS_EX_LAYERED | Win32Interop.WS_EX_TRANSPARENT |
                          Win32Interop.WS_EX_TOOLWINDOW | Win32Interop.WS_EX_TOPMOST |
                          Win32Interop.WS_EX_NOACTIVATE;

            var hInstance = Win32Interop.GetModuleHandle(null);

            var overlayHwnd = Win32Interop.CreateWindowEx(
                exStyle,
                OverlayClassName,
                "Win7Revival Overlay",
                Win32Interop.WS_POPUP,
                rect.Left, rect.Top, rect.Width, rect.Height,
                IntPtr.Zero, IntPtr.Zero, hInstance, IntPtr.Zero);

            if (overlayHwnd == IntPtr.Zero)
            {
                Debug.WriteLine($"[OverlayWindow] Failed to create overlay for taskbar 0x{taskbarHandle:X}");
                return;
            }

            // Apply DWM effects
            ApplyDwmEffect(overlayHwnd);

            // Apply opacity via layered alpha
            byte alpha = (byte)Math.Clamp((int)(_settings.Opacity / 100.0 * 255), 0, 255);
            Win32Interop.SetLayeredWindowAttributes(overlayHwnd, 0, alpha, Win32Interop.LWA_ALPHA);

            // Show without activating
            Win32Interop.ShowWindow(overlayHwnd, Win32Interop.SW_SHOWNOACTIVATE);

            Win32Interop.SetWindowPos(overlayHwnd, Win32Interop.HWND_TOPMOST,
                rect.Left, rect.Top, rect.Width, rect.Height,
                Win32Interop.SWP_NOACTIVATE | Win32Interop.SWP_SHOWWINDOW);

            _overlays.Add(new OverlayInfo
            {
                TaskbarHandle = taskbarHandle,
                OverlayHandle = overlayHwnd,
                LastRect = rect,
                IsVisible = true
            });
        }

        private void ApplyDwmEffect(IntPtr overlayHwnd)
        {
            // Extend DWM frame into entire client area
            var margins = new Win32Interop.MARGINS
            {
                cxLeftWidth = -1,
                cxRightWidth = -1,
                cyTopHeight = -1,
                cyBottomHeight = -1
            };
            Win32Interop.DwmExtendFrameIntoClientArea(overlayHwnd, ref margins);

            // Enable dark mode (required for backdrop)
            int darkMode = 1;
            Win32Interop.DwmSetWindowAttribute(overlayHwnd,
                Win32Interop.DWMWA_USE_IMMERSIVE_DARK_MODE,
                ref darkMode, sizeof(int));

            int build = Environment.OSVersion.Version.Build;

            if (build >= MinBuildForBackdrop)
            {
                // DWMWA_SYSTEMBACKDROP_TYPE — documented since Win11 22H2
                int backdropType = _settings.Effect switch
                {
                    EffectType.Mica => Win32Interop.DWMSBT_MAINWINDOW,
                    EffectType.Blur => Win32Interop.DWMSBT_TRANSIENTWINDOW,
                    EffectType.Acrylic => Win32Interop.DWMSBT_TRANSIENTWINDOW,
                    EffectType.Glass => Win32Interop.DWMSBT_TRANSIENTWINDOW,
                    EffectType.None => Win32Interop.DWMSBT_NONE,
                    _ => Win32Interop.DWMSBT_TRANSIENTWINDOW
                };

                int hr = Win32Interop.DwmSetWindowAttribute(overlayHwnd,
                    Win32Interop.DWMWA_SYSTEMBACKDROP_TYPE,
                    ref backdropType, sizeof(int));

                if (hr >= 0)
                {
                    Debug.WriteLine($"[OverlayWindow] Backdrop type {backdropType} applied.");
                    return;
                }

                Debug.WriteLine($"[OverlayWindow] Backdrop failed (HR=0x{hr:X}), falling back to DwmEnableBlurBehindWindow.");
            }

            // Fallback: DwmEnableBlurBehindWindow (documented, Win10+)
            var bb = new Win32Interop.DWM_BLURBEHIND
            {
                dwFlags = Win32Interop.DWM_BB_ENABLE,
                fEnable = _settings.Effect != EffectType.None,
                hRgnBlur = IntPtr.Zero,
                fTransitionOnMaximized = false
            };
            Win32Interop.DwmEnableBlurBehindWindow(overlayHwnd, ref bb);
            Debug.WriteLine($"[OverlayWindow] DwmEnableBlurBehindWindow applied, effect={_settings.Effect}");
        }

        private void UpdateOverlayEffects()
        {
            foreach (var info in _overlays)
            {
                if (!Win32Interop.IsWindow(info.OverlayHandle)) continue;

                ApplyDwmEffect(info.OverlayHandle);

                byte alpha = (byte)Math.Clamp((int)(_settings.Opacity / 100.0 * 255), 0, 255);
                Win32Interop.SetLayeredWindowAttributes(info.OverlayHandle, 0, alpha, Win32Interop.LWA_ALPHA);
            }
        }

        /// <summary>
        /// Called every 100ms. Repositions overlays, handles auto-hide, re-asserts z-order.
        /// </summary>
        private void RepositionOverlays()
        {
            for (int i = _overlays.Count - 1; i >= 0; i--)
            {
                var info = _overlays[i];

                if (!Win32Interop.IsWindow(info.TaskbarHandle))
                {
                    Win32Interop.DestroyWindow(info.OverlayHandle);
                    _overlays.RemoveAt(i);
                    continue;
                }

                if (!Win32Interop.GetWindowRect(info.TaskbarHandle, out var rect)) continue;

                // Auto-hide: taskbar collapses to ≤2px when hidden
                bool taskbarHidden = rect.Width <= 2 || rect.Height <= 2;

                if (taskbarHidden && info.IsVisible)
                {
                    Win32Interop.ShowWindow(info.OverlayHandle, Win32Interop.SW_HIDE);
                    info.IsVisible = false;
                }
                else if (!taskbarHidden && !info.IsVisible)
                {
                    Win32Interop.ShowWindow(info.OverlayHandle, Win32Interop.SW_SHOWNOACTIVATE);
                    info.IsVisible = true;
                }

                if (taskbarHidden) continue;

                if (rect.Left != info.LastRect.Left || rect.Top != info.LastRect.Top ||
                    rect.Width != info.LastRect.Width || rect.Height != info.LastRect.Height)
                {
                    Win32Interop.SetWindowPos(info.OverlayHandle, Win32Interop.HWND_TOPMOST,
                        rect.Left, rect.Top, rect.Width, rect.Height,
                        Win32Interop.SWP_NOACTIVATE | Win32Interop.SWP_SHOWWINDOW);
                    info.LastRect = rect;
                }
                else
                {
                    Win32Interop.SetWindowPos(info.OverlayHandle, Win32Interop.HWND_TOPMOST,
                        0, 0, 0, 0,
                        Win32Interop.SWP_NOMOVE | Win32Interop.SWP_NOSIZE | Win32Interop.SWP_NOACTIVATE);
                }
            }

            // Check for new taskbar handles (new monitor connected)
            foreach (var taskbarHandle in _detector.AllHandles)
            {
                if (!_detector.IsHandleValid(taskbarHandle)) continue;

                bool alreadyTracked = false;
                foreach (var info in _overlays)
                {
                    if (info.TaskbarHandle == taskbarHandle)
                    {
                        alreadyTracked = true;
                        break;
                    }
                }

                if (!alreadyTracked)
                    CreateSingleOverlay(taskbarHandle);
            }
        }

        private void DestroyAllOverlays()
        {
            foreach (var info in _overlays)
            {
                if (Win32Interop.IsWindow(info.OverlayHandle))
                    Win32Interop.DestroyWindow(info.OverlayHandle);
            }
            _overlays.Clear();
        }

        // ================================================================
        // LEGACY MODE — SetWindowCompositionAttribute pe Shell_TrayWnd
        // ================================================================

        private void ApplyLegacyMode()
        {
            var accentState = MapEffectToAccentState(_settings.Effect);
            int gradientColor = CalculateGradientColor(_settings.Opacity, _settings.TintR, _settings.TintG, _settings.TintB);

            foreach (var handle in _detector.AllHandles)
            {
                if (!_detector.IsHandleValid(handle)) continue;
                ApplyAccentPolicy(handle, accentState, gradientColor);
            }

            lock (_lock)
            {
                _reapplyTimer ??= new Timer(_ => ReapplyLegacyEffect(), null, 100, 100);
            }

            Debug.WriteLine($"[OverlayWindow] Legacy mode: {_settings.Effect}, Opacity: {_settings.Opacity}%");
        }

        private void ReapplyLegacyEffect()
        {
            lock (_lock)
            {
                if (!_isActive || _disposed || _reapplyTimer == null) return;
            }

            try
            {
                var accentState = MapEffectToAccentState(_settings.Effect);
                int gradientColor = CalculateGradientColor(_settings.Opacity, _settings.TintR, _settings.TintG, _settings.TintB);

                foreach (var handle in _detector.AllHandles)
                {
                    if (_detector.IsHandleValid(handle))
                        ApplyAccentPolicy(handle, accentState, gradientColor);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[OverlayWindow] ReapplyLegacyEffect error: {ex.Message}");
            }
        }

        private void RemoveLegacyMode()
        {
            Timer? timerToDispose;
            lock (_lock)
            {
                timerToDispose = _reapplyTimer;
                _reapplyTimer = null;
            }
            if (timerToDispose != null)
            {
                using var mre = new ManualResetEvent(false);
                if (timerToDispose.Dispose(mre))
                    mre.WaitOne();
            }

            foreach (var handle in _detector.AllHandles)
            {
                if (!_detector.IsHandleValid(handle)) continue;
                ApplyAccentPolicy(handle, Win32Interop.ACCENT_STATE.ACCENT_DISABLED, 0);
            }

            Debug.WriteLine("[OverlayWindow] Legacy effects removed, taskbar restored.");
        }

        // ================================================================
        // Legacy helpers
        // ================================================================

        private static Win32Interop.ACCENT_STATE MapEffectToAccentState(EffectType effect)
        {
            return effect switch
            {
                EffectType.Glass => Win32Interop.ACCENT_STATE.ACCENT_ENABLE_TRANSPARENTGRADIENT,
                EffectType.Blur => Win32Interop.ACCENT_STATE.ACCENT_ENABLE_BLURBEHIND,
                EffectType.Acrylic => Win32Interop.ACCENT_STATE.ACCENT_ENABLE_ACRYLICBLURBEHIND,
                EffectType.Mica => Win32Interop.ACCENT_STATE.ACCENT_ENABLE_HOSTBACKDROP,
                EffectType.None => Win32Interop.ACCENT_STATE.ACCENT_DISABLED,
                _ => Win32Interop.ACCENT_STATE.ACCENT_ENABLE_BLURBEHIND
            };
        }

        private static int CalculateGradientColor(int opacityPercent, byte r, byte g, byte b)
        {
            int alpha = (int)(opacityPercent / 100.0 * 255);
            alpha = Math.Clamp(alpha, 0, 255);
            return (alpha << 24) | (b << 16) | (g << 8) | r;
        }

        private static void ApplyAccentPolicy(IntPtr hwnd, Win32Interop.ACCENT_STATE state, int gradientColor)
        {
            var accent = new Win32Interop.ACCENT_POLICY
            {
                AccentState = state,
                AccentFlags = 2,
                GradientColor = gradientColor,
                AnimationId = 0
            };

            int accentSize = Marshal.SizeOf(accent);
            IntPtr accentPtr = Marshal.AllocHGlobal(accentSize);
            try
            {
                Marshal.StructureToPtr(accent, accentPtr, false);

                var data = new Win32Interop.WINDOWCOMPOSITIONATTRIB_DATA
                {
                    Attrib = Win32Interop.WINDOWCOMPOSITIONATTRIB.WCA_ACCENT_POLICY,
                    Data = accentPtr,
                    SizeOfData = accentSize
                };

                Win32Interop.SetWindowCompositionAttribute(hwnd, ref data);
            }
            finally
            {
                Marshal.FreeHGlobal(accentPtr);
            }
        }

        // ================================================================
        // Dispose
        // ================================================================

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            try { Remove(); }
            catch (Exception ex)
            {
                Debug.WriteLine($"[OverlayWindow] Dispose cleanup error: {ex.Message}");
            }
        }

        private class OverlayInfo
        {
            public IntPtr TaskbarHandle;
            public IntPtr OverlayHandle;
            public Win32Interop.RECT LastRect;
            public bool IsVisible;
        }
    }
}
