using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using Win7Revival.Core.Models;
using Win7Revival.Modules.Taskbar.Interop;

namespace Win7Revival.Modules.Taskbar
{
    /// <summary>
    /// Gestionează aplicarea efectelor de transparență/blur direct pe taskbar-ul Windows.
    /// Folosește SetWindowCompositionAttribute pentru a modifica accent policy-ul taskbar-ului.
    /// Suportă: opacity configurabilă, mai multe tipuri de efecte, și multi-monitor.
    /// </summary>
    public class OverlayWindow : IDisposable
    {
        private readonly TaskbarDetector _detector;
        private readonly object _timerLock = new();
        private ModuleSettings _settings;
        private bool _isActive;
        private bool _disposed;
        private Timer? _reapplyTimer;

        public bool IsActive => _isActive;

        public OverlayWindow(TaskbarDetector detector, ModuleSettings settings)
        {
            _detector = detector;
            _settings = settings;
        }

        /// <summary>
        /// Aplică efectul de transparență pe toate taskbar-urile detectate.
        /// </summary>
        public void Apply()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(OverlayWindow));

            var accentState = MapEffectToAccentState(_settings.Effect);
            int gradientColor = CalculateGradientColor(_settings.Opacity, _settings.TintR, _settings.TintG, _settings.TintB);

            foreach (var handle in _detector.AllHandles)
            {
                if (!_detector.IsHandleValid(handle))
                {
                    Debug.WriteLine($"[OverlayWindow] Handle invalid: 0x{handle:X}, skipping.");
                    continue;
                }

                ApplyAccentPolicy(handle, accentState, gradientColor);
            }

            _isActive = true;

            // Start periodic re-apply timer to counteract Windows resetting
            // the accent policy (e.g. when opening Start Menu)
            lock (_timerLock)
            {
                _reapplyTimer ??= new Timer(_ => ReapplyEffect(), null, 100, 100);
            }

            Debug.WriteLine($"[OverlayWindow] Effect applied: {_settings.Effect}, Opacity: {_settings.Opacity}%");
        }

        /// <summary>
        /// Re-aplică efectul periodic. Apelat de timer pe un thread pool.
        /// Lightweight — doar SetWindowCompositionAttribute P/Invoke.
        /// </summary>
        private void ReapplyEffect()
        {
            lock (_timerLock)
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
                    {
                        ApplyAccentPolicy(handle, accentState, gradientColor);
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[OverlayWindow] ReapplyEffect error: {ex.Message}");
            }
        }

        /// <summary>
        /// Restaurează taskbar-ul la starea originală (fără efecte).
        /// </summary>
        public void Remove()
        {
            if (_disposed) return;

            // Stop timer callbacks before altering accent state to avoid late reapply
            _isActive = false;
            Timer? timerToDispose;
            lock (_timerLock)
            {
                timerToDispose = _reapplyTimer;
                _reapplyTimer = null;
            }
            if (timerToDispose != null)
            {
                using var mre = new ManualResetEvent(false);
                if (timerToDispose.Dispose(mre))
                {
                    mre.WaitOne();
                }
            }

            foreach (var handle in _detector.AllHandles)
            {
                if (!_detector.IsHandleValid(handle)) continue;
                ApplyAccentPolicy(handle, Win32Interop.ACCENT_STATE.ACCENT_DISABLED, 0);
            }

            Debug.WriteLine("[OverlayWindow] Effects removed, taskbar restored.");
        }

        /// <summary>
        /// Actualizează setările și re-aplică efectele.
        /// </summary>
        public void UpdateSettings(ModuleSettings newSettings)
        {
            _settings = newSettings;
            if (_isActive)
            {
                Apply();
            }
        }

        /// <summary>
        /// Mapează EffectType (din Core) pe Win32 ACCENT_STATE.
        /// </summary>
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

        /// <summary>
        /// Builds GradientColor in ABGR format from opacity + RGB tint.
        /// ACCENT_POLICY.GradientColor uses ABGR byte order: 0xAABBGGRR.
        /// </summary>
        private static int CalculateGradientColor(int opacityPercent, byte r, byte g, byte b)
        {
            int alpha = (int)(opacityPercent / 100.0 * 255);
            alpha = Math.Clamp(alpha, 0, 255);
            return (alpha << 24) | (b << 16) | (g << 8) | r;
        }

        /// <summary>
        /// Aplică ACCENT_POLICY pe un handle specific, cu try/finally pe memoria nemanaged.
        /// </summary>
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
    }
}
