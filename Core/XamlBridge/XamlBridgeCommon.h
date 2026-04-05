//
// XamlBridgeCommon.h — Shared includes, types and extern declarations
//                      for all GlassBar.XamlBridge modules  [ITER #11]
//
#pragma once

// <unknwn.h> MUST come before any C++/WinRT header when implementing
// classic COM interfaces (IClassFactory, IObjectWithSite, IVisualTreeServiceCallback2).
// winrt/base.h asserts this at compile time (C2338).
#include <unknwn.h>

#pragma warning(push)
#pragma warning(disable:4002)   // GetCurrentTime macro conflict in animation headers
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#pragma warning(pop)

#include <xamlom.h>
#include <shlobj.h>
#include <Windows.h>
#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <exception>
#include <cstdlib>

#include "SharedBlurState.h"

// ---------------------------------------------------------------------------
// Namespace aliases
// ---------------------------------------------------------------------------
namespace wu   = winrt::Windows::UI;
namespace wuc  = winrt::Windows::UI::Core;
namespace wust = winrt::Windows::System::Threading;
namespace wux  = winrt::Windows::UI::Xaml;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;

// ---------------------------------------------------------------------------
// Shared types
// ---------------------------------------------------------------------------

enum class BrushTargetProp {
    Fill,
    Stroke
};

struct ShapeEntry {
    InstanceHandle  handle;
    wux::FrameworkElement element;
    BrushTargetProp prop;
    ShapeEntry(InstanceHandle h, wux::FrameworkElement e, BrushTargetProp p)
        : handle(h), element(std::move(e)), prop(p) {}
};

struct BrushParams {
    bool enabled;
    bool useBlur;
    BYTE alpha;
    BYTE r, g, b;
};

// ---------------------------------------------------------------------------
// Global state (defined in dllmain.cpp)
// ---------------------------------------------------------------------------
extern HINSTANCE                g_hModule;
extern std::atomic<bool>        g_stopping;
extern SharedBlurState*         g_pState;
extern std::vector<ShapeEntry>  g_knownShapes;
extern std::mutex               g_shapesMtx;
extern winrt::com_ptr<IXamlDiagnostics> g_xamlDiagnostics;
extern winrt::com_ptr<IVisualTreeService3> g_visualTreeService3;
extern winrt::Windows::Foundation::IAsyncOperation<bool> g_walkAsyncAction;
extern winrt::Windows::Foundation::IAsyncOperation<bool> g_bgFillAsyncAction;
extern winrt::Windows::Foundation::IAsyncOperation<bool> g_bgStrokeAsyncAction;

// ---------------------------------------------------------------------------
// Logging (defined in dllmain.cpp)
// ---------------------------------------------------------------------------
void XBLog(const wchar_t* msg);
void XBLogFmt(const wchar_t* fmt, ...);

// ---------------------------------------------------------------------------
// Brush helpers (defined in VisualTreeWatcher.cpp)
// ---------------------------------------------------------------------------
BrushParams ReadBrushParams(const SharedBlurState* s);
void        ApplyBrushParams(InstanceHandle handle, const wux::FrameworkElement& element, BrushTargetProp prop, const BrushParams& p);
