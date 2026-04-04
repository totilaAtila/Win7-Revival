//
// VisualTreeWatcher.cpp - Tree monitoring + brush application  [ITER #14]
//
// ITER #14 changes vs ITER #13:
//   - TaskbarBackground now resolves and walks inline on the callback that sees it.
//   - WorkerThread walk remains as a fallback when inline resolve/walk fails.
//   - Shared walk logic lives in WalkTaskbarBgTree() so both paths stay identical.
// ITER #15 diagnostic:
//   - Force SolidColorBrush / Transparent for XamlBridge taskbar path.
//   - Temporarily bypass AcrylicBrush so we can prove the Fill pipeline first.
//
#include "VisualTreeWatcher.h"
#include "TAPObject.h"

// ---------------------------------------------------------------------------
// Diagnostic state (shared across all watcher instances)
// ---------------------------------------------------------------------------
static std::atomic<int>            g_loggedElements   { 0 };
static constexpr int               kMaxLoggedElements = 10000;
static std::atomic<InstanceHandle> g_taskbarBgHandle  { 0 };

// ---------------------------------------------------------------------------
// Brush helpers
// ---------------------------------------------------------------------------

BrushParams ReadBrushParams(const SharedBlurState* s)
{
    BrushParams p = {};
    if (!s) return p;
    LONG en  = InterlockedCompareExchange(const_cast<volatile LONG*>(&s->blurEnabled), 0, 0);
    LONG op  = InterlockedCompareExchange(const_cast<volatile LONG*>(&s->opacityPct), 75, 75);
    LONG amt = InterlockedCompareExchange(const_cast<volatile LONG*>(&s->blurAmount), 0, 0);
    p.enabled = (en != 0);
    p.useBlur = (amt > 0);
    p.alpha   = static_cast<BYTE>(((100 - op) * 255) / 100);
    p.r       = static_cast<BYTE>(InterlockedCompareExchange(const_cast<volatile LONG*>(&s->colorR), 0, 0));
    p.g       = static_cast<BYTE>(InterlockedCompareExchange(const_cast<volatile LONG*>(&s->colorG), 0, 0));
    p.b       = static_cast<BYTE>(InterlockedCompareExchange(const_cast<volatile LONG*>(&s->colorB), 0, 0));
    return p;
}

// Must be called on the XAML UI thread (OnVisualTreeChange or dispatcher callback).
void ApplyBrushParams(const wux::FrameworkElement& element, BrushTargetProp prop, const BrushParams& p)
{
    auto targetProp = (prop == BrushTargetProp::Stroke)
        ? wuxs::Shape::StrokeProperty()
        : wuxs::Shape::FillProperty();
    const wchar_t* propName = (prop == BrushTargetProp::Stroke) ? L"Stroke" : L"Fill";

    if (!p.enabled) {
        XBLogFmt(L"ApplyBrushParams: enabled=0 -> ClearValue on %s", propName);
        element.ClearValue(targetProp);
        XBLogFmt(L"ApplyBrushParams: cleared local %s value", propName);
        return;
    }

    wu::Color color{ p.alpha, p.r, p.g, p.b };

    wuxm::SolidColorBrush brush{};
    brush.Color(color);
    XBLogFmt(L"ApplyBrushParams: using SolidColorBrush on %s alpha=%d rgb=(%d,%d,%d) blurRequested=%d",
        propName, p.alpha, p.r, p.g, p.b, p.useBlur ? 1 : 0);
    element.SetValue(targetProp, brush);
    XBLogFmt(L"ApplyBrushParams: solid %s applied", propName);
}

// ---------------------------------------------------------------------------
// RegisterAndApplyShape
// handle == 0 for VTH-walked shapes (no diagnostic handle available);
// dedup by handle (if != 0) or by raw COM pointer (if handle == 0).
// ---------------------------------------------------------------------------
static void RegisterAndApplyTarget(InstanceHandle handle, const wux::FrameworkElement& element, BrushTargetProp prop)
{
    {
        std::lock_guard<std::mutex> lk(g_shapesMtx);
        bool found = false;
        if (handle != 0) {
            found = std::any_of(g_knownShapes.begin(), g_knownShapes.end(),
                [handle](const ShapeEntry& e) { return e.handle == handle; });
        } else {
            found = std::any_of(g_knownShapes.begin(), g_knownShapes.end(),
                [&element, prop](const ShapeEntry& e) { return e.prop == prop && e.element == element; });
        }
        if (!found)
            g_knownShapes.emplace_back(handle, element, prop);
    }

    BrushParams p = ReadBrushParams(g_pState);
    ApplyBrushParams(element, prop, p);
}

static bool QueueShapeApply(
    InstanceHandle handle,
    const wux::FrameworkElement& element,
    const wchar_t* elementName,
    const wchar_t* logTag) noexcept
{
    try {
        auto disp = element.Dispatcher();
        if (!disp) {
            XBLogFmt(L"%s %s: Dispatcher() returned null", logTag, elementName);
            return false;
        }

        XBLogFmt(L"%s %s: queueing apply on Dispatcher", logTag, elementName);
        auto action = disp.RunAsync(wuc::CoreDispatcherPriority::Normal,
            [handle, element, tag = std::wstring(logTag), name = std::wstring(elementName)]() noexcept {
                try {
                    XBLogFmt(L"%s %s: dispatcher callback entered", tag.c_str(), name.c_str());
                    BrushTargetProp prop = (name == L"BackgroundStroke")
                        ? BrushTargetProp::Stroke
                        : BrushTargetProp::Fill;
                    XBLogFmt(L"%s %s: applying via DependencyProperty on Dispatcher", tag.c_str(), name.c_str());
                    RegisterAndApplyTarget(handle, element, prop);
                    XBLogFmt(L"%s %s: RegisterAndApplyTarget finished", tag.c_str(), name.c_str());
                }
                catch (const winrt::hresult_error& ex) {
                    XBLogFmt(L"%s %s: dispatcher callback threw 0x%08X: %s",
                        tag.c_str(), name.c_str(), ex.code(), ex.message().c_str());
                }
                catch (...) {
                    XBLogFmt(L"%s %s: dispatcher callback threw unknown", tag.c_str(), name.c_str());
                }
            });

        XBLogFmt(L"%s %s: Dispatcher.RunAsync returned %s", logTag, elementName, action ? L"action" : L"null");
        return static_cast<bool>(action);
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s %s: queueing threw 0x%08X: %s", logTag, elementName, ex.code(), ex.message().c_str());
    }
    catch (...) {
        XBLogFmt(L"%s %s: queueing threw unknown", logTag, elementName);
    }

    return false;
}

static bool WalkTaskbarBgTree(const wux::FrameworkElement& fe, const wchar_t* logTag) noexcept
{
    bool foundAny = false;
    try {
        int bgChildCount = wuxm::VisualTreeHelper::GetChildrenCount(fe);
        XBLogFmt(L"%s child count: %d", logTag, bgChildCount);
        for (int ci = 0; ci < bgChildCount; ++ci) {
            auto intermediate = wuxm::VisualTreeHelper::GetChild(fe, ci);
            if (!intermediate) {
                XBLogFmt(L"%s child[%d] = nullptr", logTag, ci);
                continue;
            }

            auto intermFE = intermediate.try_as<wux::FrameworkElement>();
            if (!intermFE) continue;

            int grandCount = wuxm::VisualTreeHelper::GetChildrenCount(intermFE);
            XBLogFmt(L"%s child[%d] Name='%s' grandchildren=%d",
                logTag, ci, intermFE.Name().c_str(), grandCount);

            for (int gi = 0; gi < grandCount; ++gi) {
                winrt::Windows::Foundation::IInspectable grand;
                try {
                    grand = wuxm::VisualTreeHelper::GetChild(intermFE, gi);
                    XBLogFmt(L"%s GetChild[%d][%d] = %s",
                        logTag, ci, gi, grand ? L"valid" : L"nullptr");
                }
                catch (const winrt::hresult_error& ex) {
                    XBLogFmt(L"%s GetChild[%d][%d] threw 0x%08X", logTag, ci, gi, ex.code());
                    continue;
                }
                catch (...) {
                    XBLogFmt(L"%s GetChild[%d][%d] threw unknown", logTag, ci, gi);
                    continue;
                }

                if (!grand) continue;

                auto grandFE = grand.try_as<wux::FrameworkElement>();
                if (!grandFE) {
                    XBLogFmt(L"%s grandchild[%d][%d] not FrameworkElement", logTag, ci, gi);
                    continue;
                }

                auto grandName = grandFE.Name();
                XBLogFmt(L"%s grandchild[%d][%d] Name='%s'", logTag, ci, gi, grandName.c_str());
                bool isBgFill   = (grandName == L"BackgroundFill");
                bool isBgStroke = (grandName == L"BackgroundStroke");
                if (!isBgFill && !isBgStroke) continue;

                XBLogFmt(L"%s found %s - queueing dispatcher apply", logTag, grandName.c_str());
                if (QueueShapeApply(0, grandFE, grandName.c_str(), logTag))
                    foundAny = true;
            }
        }
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s walk threw 0x%08X: %s", logTag, ex.code(), ex.message().c_str());
    }
    catch (...) {
        XBLogFmt(L"%s walk threw unknown exception", logTag);
    }

    return foundAny;
}

// ---------------------------------------------------------------------------
// WalkTaskbarBgPostReplay
// Called from WorkerThread fallback path when inline resolve/walk could not finish.
// ---------------------------------------------------------------------------
void VisualTreeWatcher::WalkTaskbarBgPostReplay() noexcept
{
    XBLog(L"[WalkPostReplay] entered");
    InstanceHandle bgHandle = g_taskbarBgHandle.load();
    XBLogFmt(L"[WalkPostReplay] bgHandle=0x%llX  g_walkDiagnostics=%s",
        static_cast<unsigned long long>(bgHandle),
        g_walkDiagnostics ? L"OK" : L"NULL");
    if (bgHandle == 0) {
        XBLog(L"WalkTaskbarBgPostReplay: no handle stored - TaskbarBackground not seen in replay");
        return;
    }
    if (!g_walkDiagnostics) {
        XBLog(L"WalkTaskbarBgPostReplay: g_walkDiagnostics is null");
        return;
    }

    winrt::Windows::Foundation::IInspectable obj;
    HRESULT hr = g_walkDiagnostics->GetIInspectableFromHandle(
        bgHandle,
        reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
    if (FAILED(hr) || !obj) {
        XBLogFmt(L"WalkTaskbarBgPostReplay: GetIInspectableFromHandle failed hr=0x%08X", hr);
        return;
    }

    auto fe = obj.try_as<wux::FrameworkElement>();
    if (!fe) {
        XBLog(L"WalkTaskbarBgPostReplay: try_as<FrameworkElement> failed");
        return;
    }

    XBLog(L"[PostReplay] walk started");
    if (!WalkTaskbarBgTree(fe, L"[PostReplay]"))
        XBLog(L"[PostReplay] no BackgroundFill/BackgroundStroke found");
}

// ---------------------------------------------------------------------------
// VisualTreeWatcher::OnVisualTreeChange
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE VisualTreeWatcher::OnVisualTreeChange(
    ParentChildRelation relation,
    VisualElement element,
    VisualMutationType mutationType) noexcept
{
    try {
        if (mutationType == Remove) {
            std::lock_guard<std::mutex> lk(g_shapesMtx);
            auto it = std::find_if(g_knownShapes.begin(), g_knownShapes.end(),
                [h = element.Handle](const ShapeEntry& e) { return e.handle == h; });
            if (it != g_knownShapes.end())
                g_knownShapes.erase(it);
            return S_OK;
        }

        int idx = g_loggedElements.fetch_add(1);
        if (idx < kMaxLoggedElements) {
            XBLogFmt(L"  [%d] parent=0x%llX  Type=%-60s  Name=%s",
                idx,
                static_cast<unsigned long long>(relation.Parent),
                element.Type ? element.Type : L"(null)",
                element.Name ? element.Name : L"(null)");
        }

        bool isTaskbarBg = (element.Type &&
            wcscmp(element.Type, L"Taskbar.TaskbarBackground") == 0);
        if (isTaskbarBg) {
            g_taskbarBgHandle.store(element.Handle);
            XBLogFmt(L"  *** TaskbarBackground handle=0x%llX - resolving and walking inline from callback ***",
                static_cast<unsigned long long>(element.Handle));

            winrt::Windows::Foundation::IInspectable obj;
            HRESULT hr = m_diagnostics->GetIInspectableFromHandle(
                element.Handle,
                reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
            if (FAILED(hr) || !obj) {
                g_walkDiagnostics = m_diagnostics;
                g_walkNeeded.store(true);
                XBLogFmt(L"  *** Inline resolve FAILED hr=0x%08X - falling back to WorkerThread walk ***", hr);
                return S_OK;
            }

            auto fe = obj.try_as<wux::FrameworkElement>();
            if (!fe) {
                g_walkDiagnostics = m_diagnostics;
                g_walkNeeded.store(true);
                XBLog(L"  *** Inline try_as<FrameworkElement> FAILED - falling back to WorkerThread walk ***");
                return S_OK;
            }

            XBLog(L"[Inline] walk started");
            if (!WalkTaskbarBgTree(fe, L"[Inline]")) {
                g_walkDiagnostics = m_diagnostics;
                g_walkNeeded.store(true);
                XBLog(L"[Inline] no BackgroundFill/BackgroundStroke found - WorkerThread fallback requested");
            }
            return S_OK;
        }

        InstanceHandle bgHandle = g_taskbarBgHandle.load();
        bool isChildOfBg = (bgHandle != 0 && relation.Parent == bgHandle);
        if (isChildOfBg) {
            XBLogFmt(L"  *** TaskbarBackground CHILD: Type=%s  Name=%s ***",
                element.Type ? element.Type : L"(null)",
                element.Name ? element.Name : L"(null)");
        }

        bool isRectangle = element.Type && wcsstr(element.Type, L"Rectangle") != nullptr;
        if (!isChildOfBg && !isRectangle)
            return S_OK;

        winrt::Windows::Foundation::IInspectable obj;
        HRESULT hr = m_diagnostics->GetIInspectableFromHandle(
            element.Handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
        if (FAILED(hr) || !obj) {
            if (isChildOfBg)
                XBLogFmt(L"  *** GetIInspectableFromHandle FAILED hr=0x%08X ***", hr);
            return S_OK;
        }

        auto fe = obj.try_as<wux::FrameworkElement>();
        if (!fe) return S_OK;

        auto runtimeName = fe.Name();
        if (isRectangle) {
            bool isBackground = (runtimeName == L"BackgroundFill");
            bool isStroke     = (runtimeName == L"BackgroundStroke");
            XBLogFmt(L"  Rectangle: Name=%s  isBackground=%d  isStroke=%d  childOfBg=%d",
                runtimeName.c_str(), isBackground ? 1 : 0, isStroke ? 1 : 0, isChildOfBg ? 1 : 0);
            if (!isBackground && !isStroke && !isChildOfBg)
                return S_OK;
        }

        BrushTargetProp prop = (runtimeName == L"BackgroundStroke")
            ? BrushTargetProp::Stroke
            : BrushTargetProp::Fill;
        XBLogFmt(L"  >>> Applying brush to Name=%s via DependencyProperty", runtimeName.c_str());
        RegisterAndApplyTarget(element.Handle, fe, prop);
    }
    catch (...) {}

    return S_OK;
}

HRESULT STDMETHODCALLTYPE VisualTreeWatcher::OnElementStateChanged(
    InstanceHandle /*element*/,
    VisualElementState /*elementState*/,
    LPCWSTR /*context*/) noexcept
{
    return S_OK;
}
