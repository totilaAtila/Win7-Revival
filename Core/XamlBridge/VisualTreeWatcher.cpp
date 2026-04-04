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
static std::atomic<unsigned int>   g_fillPropertyIndex   { UINT_MAX };
static std::atomic<unsigned int>   g_strokePropertyIndex { UINT_MAX };

// ---------------------------------------------------------------------------
// Brush helpers
// ---------------------------------------------------------------------------

static void FreePropertyChain(unsigned int sourceCount,
    PropertyChainSource* sources,
    unsigned int propertyCount,
    PropertyChainValue* values)
{
    if (sources) {
        for (unsigned int i = 0; i < sourceCount; ++i) {
            SysFreeString(sources[i].TargetType);
            SysFreeString(sources[i].Name);
            SysFreeString(sources[i].SrcInfo.FileName);
            SysFreeString(sources[i].SrcInfo.Hash);
        }
        CoTaskMemFree(sources);
    }

    if (values) {
        for (unsigned int i = 0; i < propertyCount; ++i) {
            SysFreeString(values[i].Type);
            SysFreeString(values[i].DeclaringType);
            SysFreeString(values[i].ValueType);
            SysFreeString(values[i].ItemType);
            SysFreeString(values[i].Value);
            SysFreeString(values[i].PropertyName);
        }
        CoTaskMemFree(values);
    }
}

static bool TryResolveHandle(const wux::FrameworkElement& element, InstanceHandle& outHandle)
{
    outHandle = 0;
    if (!g_xamlDiagnostics) {
        XBLog(L"TryResolveHandle: g_xamlDiagnostics is null");
        return false;
    }

    try {
        auto obj = element.as<winrt::Windows::Foundation::IInspectable>();
        HRESULT hr = g_xamlDiagnostics->GetHandleFromIInspectable(
            reinterpret_cast<::IInspectable*>(winrt::get_abi(obj)),
            &outHandle);
        if (FAILED(hr) || outHandle == 0) {
            XBLogFmt(L"TryResolveHandle: GetHandleFromIInspectable failed hr=0x%08X", hr);
            outHandle = 0;
            return false;
        }
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"TryResolveHandle: threw 0x%08X: %s", ex.code(), ex.message().c_str());
    }
    catch (...) {
        XBLog(L"TryResolveHandle: threw unknown");
    }

    return false;
}

static bool TryGetPropertyIndex(InstanceHandle handle, BrushTargetProp prop, unsigned int& outIndex)
{
    auto& cache = (prop == BrushTargetProp::Stroke) ? g_strokePropertyIndex : g_fillPropertyIndex;
    unsigned int cached = cache.load();
    if (cached != UINT_MAX) {
        outIndex = cached;
        return true;
    }

    if (!g_visualTreeService3) {
        XBLog(L"TryGetPropertyIndex: g_visualTreeService3 is null");
        return false;
    }

    unsigned int sourceCount = 0;
    unsigned int propertyCount = 0;
    PropertyChainSource* sources = nullptr;
    PropertyChainValue* values = nullptr;
    HRESULT hr = g_visualTreeService3->GetPropertyValuesChain(
        handle, &sourceCount, &sources, &propertyCount, &values);
    if (FAILED(hr) || !values) {
        XBLogFmt(L"TryGetPropertyIndex: GetPropertyValuesChain failed hr=0x%08X handle=0x%llX",
            hr, static_cast<unsigned long long>(handle));
        FreePropertyChain(sourceCount, sources, propertyCount, values);
        return false;
    }

    const wchar_t* wanted = (prop == BrushTargetProp::Stroke) ? L"Stroke" : L"Fill";
    bool found = false;
    for (unsigned int i = 0; i < propertyCount; ++i) {
        if (values[i].PropertyName && wcscmp(values[i].PropertyName, wanted) == 0) {
            outIndex = values[i].Index;
            cache.store(outIndex);
            XBLogFmt(L"TryGetPropertyIndex: %s index=%u handle=0x%llX",
                wanted, outIndex, static_cast<unsigned long long>(handle));
            found = true;
            break;
        }
    }

    if (!found) {
        XBLogFmt(L"TryGetPropertyIndex: %s not found on handle=0x%llX (properties=%u)",
            wanted, static_cast<unsigned long long>(handle), propertyCount);
        for (unsigned int i = 0; i < propertyCount && i < 32; ++i) {
            XBLogFmt(L"  prop[%u]: index=%u name=%s type=%s valueType=%s",
                i,
                values[i].Index,
                values[i].PropertyName ? values[i].PropertyName : L"(null)",
                values[i].Type ? values[i].Type : L"(null)",
                values[i].ValueType ? values[i].ValueType : L"(null)");
        }
    }

    FreePropertyChain(sourceCount, sources, propertyCount, values);
    return found;
}

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

static void ApplyBrushParamsFallback(const wux::FrameworkElement& element, BrushTargetProp prop, const BrushParams& p)
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

void ApplyBrushParams(InstanceHandle handle, const wux::FrameworkElement& element, BrushTargetProp prop, const BrushParams& p)
{
    const wchar_t* propName = (prop == BrushTargetProp::Stroke) ? L"Stroke" : L"Fill";

    if (handle == 0) {
        InstanceHandle resolvedHandle = 0;
        if (TryResolveHandle(element, resolvedHandle))
            handle = resolvedHandle;
    }

    if (!g_visualTreeService3 || handle == 0) {
        XBLogFmt(L"ApplyBrushParams: native path unavailable for %s (svc3=%d handle=0x%llX) -> fallback",
            propName, g_visualTreeService3 ? 1 : 0, static_cast<unsigned long long>(handle));
        ApplyBrushParamsFallback(element, prop, p);
        return;
    }

    unsigned int propertyIndex = UINT_MAX;
    if (!TryGetPropertyIndex(handle, prop, propertyIndex)) {
        XBLogFmt(L"ApplyBrushParams: property index lookup failed for %s -> fallback", propName);
        ApplyBrushParamsFallback(element, prop, p);
        return;
    }

    if (!p.enabled) {
        HRESULT hr = g_visualTreeService3->ClearProperty(handle, propertyIndex);
        XBLogFmt(L"ApplyBrushParams: native ClearProperty(%s) hr=0x%08X handle=0x%llX index=%u",
            propName, hr, static_cast<unsigned long long>(handle), propertyIndex);
        if (SUCCEEDED(hr))
            return;

        XBLogFmt(L"ApplyBrushParams: native ClearProperty failed for %s -> fallback", propName);
        ApplyBrushParamsFallback(element, prop, p);
        return;
    }

    try {
        wu::Color color{ p.alpha, p.r, p.g, p.b };
        wuxm::SolidColorBrush brush{};
        brush.Color(color);

        InstanceHandle brushHandle = 0;
        auto brushObj = brush.as<winrt::Windows::Foundation::IInspectable>();
        HRESULT hr = g_xamlDiagnostics
            ? g_xamlDiagnostics->GetHandleFromIInspectable(
                reinterpret_cast<::IInspectable*>(winrt::get_abi(brushObj)),
                &brushHandle)
            : E_POINTER;
        XBLogFmt(L"ApplyBrushParams: brush handle resolve for %s hr=0x%08X brushHandle=0x%llX",
            propName, hr, static_cast<unsigned long long>(brushHandle));
        if (FAILED(hr) || brushHandle == 0) {
            XBLogFmt(L"ApplyBrushParams: brush handle resolve failed for %s -> fallback", propName);
            ApplyBrushParamsFallback(element, prop, p);
            return;
        }

        hr = g_visualTreeService3->SetProperty(handle, brushHandle, propertyIndex);
        XBLogFmt(L"ApplyBrushParams: native SetProperty(%s) hr=0x%08X target=0x%llX value=0x%llX index=%u",
            propName, hr,
            static_cast<unsigned long long>(handle),
            static_cast<unsigned long long>(brushHandle),
            propertyIndex);
        if (SUCCEEDED(hr))
            return;
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"ApplyBrushParams: native path threw 0x%08X on %s: %s",
            ex.code(), propName, ex.message().c_str());
    }
    catch (...) {
        XBLogFmt(L"ApplyBrushParams: native path threw unknown on %s", propName);
    }

    XBLogFmt(L"ApplyBrushParams: native SetProperty failed for %s -> fallback", propName);
    ApplyBrushParamsFallback(element, prop, p);
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
    ApplyBrushParams(handle, element, prop, p);
}

static bool WalkTaskbarBgTree(const wux::FrameworkElement& fe, IXamlDiagnostics* diag, const wchar_t* logTag) noexcept
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

                InstanceHandle grandHandle = 0;
                HRESULT hr = diag
                    ? diag->GetHandleFromIInspectable(
                        reinterpret_cast<::IInspectable*>(winrt::get_abi(grand)),
                        &grandHandle)
                    : E_POINTER;
                XBLogFmt(L"%s found %s - handle resolve hr=0x%08X handle=0x%llX",
                    logTag, grandName.c_str(), hr, static_cast<unsigned long long>(grandHandle));

                BrushTargetProp prop = isBgStroke ? BrushTargetProp::Stroke : BrushTargetProp::Fill;
                RegisterAndApplyTarget(grandHandle, grandFE, prop);
                XBLogFmt(L"%s RegisterAndApplyTarget finished for %s", logTag, grandName.c_str());
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
    if (!WalkTaskbarBgTree(fe, g_walkDiagnostics.get(), L"[PostReplay]"))
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
            if (!WalkTaskbarBgTree(fe, m_diagnostics.get(), L"[Inline]")) {
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
        XBLogFmt(L"  >>> Applying brush to Name=%s via native/managed bridge", runtimeName.c_str());
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
