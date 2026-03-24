#pragma once
#include <Windows.h>

// ---------------------------------------------------------------------------
// SharedBlurState — cross-process shared memory used for communication
// between GlassBar.Core.dll (Dashboard process) and GlassBar.XamlBridge.dll
// (injected into explorer.exe).
//
// Layout is POD with volatile interlocked fields so both processes can read/
// write safely without a mutex (we only need eventual consistency here).
// ---------------------------------------------------------------------------

struct alignas(8) SharedBlurState
{
    // Well-known name for CreateFileMappingW / OpenFileMappingW
    static constexpr wchar_t kName[] = L"Local\\GlassBar_XamlBridge_v1";

    // Monotonically increasing version — writer bumps before & after change.
    // Reader can detect partial writes by comparing before/after reads.
    volatile LONG version;

    // 1 = blur/acrylic effect requested; 0 = restore default
    volatile LONG blurEnabled;

    // Opacity 0-100 (0 = fully transparent, 100 = opaque)
    volatile LONG opacityPct;

    // RGB color tint (0-255 each)
    volatile LONG colorR;
    volatile LONG colorG;
    volatile LONG colorB;

    // Blur amount (0-100; maps to AnimationId hint for SWCA acrylic)
    volatile LONG blurAmount;

    // Set to 1 by Core when it wants XamlBridge to shut down its worker thread
    volatile LONG shutdownRequest;

    // Padding to 64 bytes
    BYTE reserved[64 - 8 * sizeof(LONG)];
};
static_assert(sizeof(SharedBlurState) == 64, "SharedBlurState layout changed");
