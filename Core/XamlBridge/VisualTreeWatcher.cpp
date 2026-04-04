//
// VisualTreeWatcher.cpp - Tree monitoring + brush application  [ITER #16]
//
// Apply path aligned with Windhawk: TryRunAsync(High) + store IAsyncOperation<bool>
// to prevent cancellation. Reads SharedBlurState inside the dispatcher callback.
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
static void RegisterAndApplyTarget(InstanceHandle handle, const wux::FrameworkElement& element, BrushTargetProp prop);

// ---------------------------------------------------------------------------
// DispatchApplyToBgElement — INSTANT SYNCHRONOUS apply for BackgroundFill/Stroke
// Applies transparency effect IMMEDIATELY inline (no dispatcher async) for Windhawk-style instant UX
// ---------------------------------------------------------------------------
static void DispatchApplyToBgElement(
    const wux::FrameworkElement& fe,
    BrushTargetProp prop,
    const wchar_t* logTag)
{
    try {
        XBLogFmt(L"%s DispatchApply: applying SYNCHRONOUSLY for instant effect", logTag);
        
        // Get handle from FrameworkElement
        InstanceHandle handle = 0;
        if (g_xamlDiagnostics) {
            auto feObj = fe.as<winrt::Windows::Foundation::IInspectable>();
            HRESULT hr = g_xamlDiagnostics->GetHandleFromIInspectable(
                reinterpret_cast<::IInspectable*>(winrt::get_abi(feObj)),
                &handle);
            XBLogFmt(L"%s DispatchApply: GetHandleFromIInspectable hr=0x%08X handle=0x%llX",
                logTag, hr, static_cast<unsigned long long>(handle));
        }
        
        if (handle == 0) {
            XBLogFmt(L"%s DispatchApply: cannot get handle from element - using handle=0", logTag);
        }
        
        XBLogFmt(L"%s DispatchApply: calling RegisterAndApplyTarget with handle=0x%llX", 
            logTag, static_cast<unsigned long long>(handle));
        
        RegisterAndApplyTarget(handle, fe, prop);
        
        XBLogFmt(L"%s DispatchApply: COMPLETED synchronously - effect should be INSTANT", logTag);
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s DispatchApply threw 0x%08X: %s",
            logTag, ex.code(), ex.message().c_str());
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"%s DispatchApply std::exception: %S", logTag, ex.what());
    }
    catch (...) {
        XBLogFmt(L"%s DispatchApply threw unknown exception", logTag);
    }
}

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
    XBLogFmt(L">>> ApplyBrushParams ENTERED: handle=0x%llX prop=%s enabled=%d alpha=%d rgb=(%d,%d,%d) useBlur=%d",
        static_cast<unsigned long long>(handle), propName, p.enabled ? 1 : 0,
        p.alpha, p.r, p.g, p.b, p.useBlur ? 1 : 0);

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
    XBLogFmt(L"RegisterAndApplyTarget: ENTER handle=0x%llX prop=%s",
        static_cast<unsigned long long>(handle),
        prop == BrushTargetProp::Stroke ? L"Stroke" : L"Fill");
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
    XBLogFmt(L"RegisterAndApplyTarget: calling ApplyBrushParams handle=0x%llX prop=%s",
        static_cast<unsigned long long>(handle),
        prop == BrushTargetProp::Stroke ? L"Stroke" : L"Fill");
    ApplyBrushParams(handle, element, prop, p);
    XBLogFmt(L"RegisterAndApplyTarget: ApplyBrushParams returned handle=0x%llX prop=%s",
        static_cast<unsigned long long>(handle),
        prop == BrushTargetProp::Stroke ? L"Stroke" : L"Fill");
}

static bool WalkTaskbarBgTree(const wux::FrameworkElement& fe, IXamlDiagnostics* diag, const wchar_t* logTag) noexcept
{
    bool foundAny = false;
    try {
        int bgChildCount = wuxm::VisualTreeHelper::GetChildrenCount(fe);
        XBLogFmt(L"%s child count: %d", logTag, bgChildCount);
        for (int ci = 0; ci < bgChildCount; ++ci) {
            XBLogFmt(L"%s child loop enter: ci=%d", logTag, ci);

            winrt::Windows::Foundation::IInspectable intermediate;
            try {
                intermediate = wuxm::VisualTreeHelper::GetChild(fe, ci);
                XBLogFmt(L"%s GetChild(fe,%d) returned %s", logTag, ci, intermediate ? L"valid" : L"nullptr");
            }
            catch (const winrt::hresult_error& ex) {
                XBLogFmt(L"%s GetChild(fe,%d) threw 0x%08X: %s", logTag, ci, ex.code(), ex.message().c_str());
                continue;
            }
            catch (const std::exception& ex) {
                XBLogFmt(L"%s GetChild(fe,%d) std::exception: %S", logTag, ci, ex.what());
                continue;
            }
            catch (...) {
                XBLogFmt(L"%s GetChild(fe,%d) threw unknown", logTag, ci);
                continue;
            }

            if (!intermediate) {
                XBLogFmt(L"%s child[%d] = nullptr", logTag, ci);
                continue;
            }

            wux::FrameworkElement intermFE{ nullptr };
            try {
                intermFE = intermediate.try_as<wux::FrameworkElement>();
                XBLogFmt(L"%s child[%d] try_as<FrameworkElement> => %s", logTag, ci, intermFE ? L"OK" : L"NULL");
            }
            catch (const winrt::hresult_error& ex) {
                XBLogFmt(L"%s child[%d] try_as<FrameworkElement> threw 0x%08X: %s",
                    logTag, ci, ex.code(), ex.message().c_str());
                continue;
            }
            catch (const std::exception& ex) {
                XBLogFmt(L"%s child[%d] try_as<FrameworkElement> std::exception: %S", logTag, ci, ex.what());
                continue;
            }
            catch (...) {
                XBLogFmt(L"%s child[%d] try_as<FrameworkElement> threw unknown", logTag, ci);
                continue;
            }

            if (!intermFE) continue;

            std::wstring intermName;
            try {
                intermName = intermFE.Name().c_str();
                XBLogFmt(L"%s child[%d] Name() => '%s'", logTag, ci, intermName.c_str());
            }
            catch (const winrt::hresult_error& ex) {
                XBLogFmt(L"%s child[%d] Name() threw 0x%08X: %s",
                    logTag, ci, ex.code(), ex.message().c_str());
                continue;
            }
            catch (const std::exception& ex) {
                XBLogFmt(L"%s child[%d] Name() std::exception: %S", logTag, ci, ex.what());
                continue;
            }
            catch (...) {
                XBLogFmt(L"%s child[%d] Name() threw unknown", logTag, ci);
                continue;
            }

            int grandCount = 0;
            try {
                grandCount = wuxm::VisualTreeHelper::GetChildrenCount(intermFE);
                XBLogFmt(L"%s child[%d] Name='%s' grandchildren=%d",
                    logTag, ci, intermName.c_str(), grandCount);
            }
            catch (const winrt::hresult_error& ex) {
                XBLogFmt(L"%s GetChildrenCount(intermFE,%d) threw 0x%08X: %s",
                    logTag, ci, ex.code(), ex.message().c_str());
                continue;
            }
            catch (const std::exception& ex) {
                XBLogFmt(L"%s GetChildrenCount(intermFE,%d) std::exception: %S",
                    logTag, ci, ex.what());
                continue;
            }
            catch (...) {
                XBLogFmt(L"%s GetChildrenCount(intermFE,%d) threw unknown", logTag, ci);
                continue;
            }

            XBLogFmt(L"%s ABOUT TO ENTER grandchildren loop: gi < %d", logTag, grandCount);

            for (int gi = 0; gi < grandCount; ++gi) {
                XBLogFmt(L"%s Loop iteration START: gi=%d", logTag, gi);
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
                XBLogFmt(L"%s dispatching apply for %s", logTag, grandName.c_str());
                DispatchApplyToBgElement(grandFE, prop, logTag);
                foundAny = true;
            }
        }
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s walk threw winrt::hresult_error 0x%08X: %s", logTag, ex.code(), ex.message().c_str());
        return false;
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"%s walk threw std::exception: %S", logTag, ex.what());
        return false;
    }
    catch (...) {
        XBLogFmt(L"%s walk threw UNKNOWN exception", logTag);
        return false;
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
            XBLogFmt(L"  *** TaskbarBackground handle=0x%llX - applying INLINE for instant effect ***",
                static_cast<unsigned long long>(element.Handle));
            
            // Get FrameworkElement immediately
            winrt::Windows::Foundation::IInspectable obj;
            HRESULT hr = m_diagnostics->GetIInspectableFromHandle(
                element.Handle,
                reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
            
            if (SUCCEEDED(hr) && obj) {
                auto fe = obj.try_as<wux::FrameworkElement>();
                if (fe) {
                    XBLog(L"  >>> Walking TaskbarBackground INLINE for instant apply...");
                    bool found = WalkTaskbarBgTree(fe, m_diagnostics.get(), L"[Inline]");
                    if (found) {
                        XBLog(L"  >>> BackgroundFill/Stroke found and applied INLINE");
                    } else {
                        XBLog(L"  >>> Walk completed but BackgroundFill not found");
                    }
                } else {
                    XBLog(L"  >>> try_as<FrameworkElement> failed - cannot walk");
                }
            } else {
                XBLogFmt(L"  >>> GetIInspectableFromHandle failed hr=0x%08X - cannot walk", hr);
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
            if (!isBackground && !isStroke)
                return S_OK;
        }

        BrushTargetProp prop = (runtimeName == L"BackgroundStroke")
            ? BrushTargetProp::Stroke
            : BrushTargetProp::Fill;
        if (isChildOfBg && (runtimeName == L"BackgroundFill" || runtimeName == L"BackgroundStroke")) {
            XBLogFmt(L"  >>> FOUND %s via TAP child notification <<<", runtimeName.c_str());
            DispatchApplyToBgElement(fe, prop, L"[TAP]");
            return S_OK;
        }

        XBLogFmt(L"  >>> Applying brush to Name=%s via native/managed bridge", runtimeName.c_str());
        RegisterAndApplyTarget(element.Handle, fe, prop);
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"OnVisualTreeChange: winrt::hresult_error 0x%08X: %s",
            ex.code(), ex.message().c_str());
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"OnVisualTreeChange: std::exception: %S", ex.what());
    }
    catch (...) {
        XBLog(L"OnVisualTreeChange: UNKNOWN exception type");
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE VisualTreeWatcher::OnElementStateChanged(
    InstanceHandle /*element*/,
    VisualElementState /*elementState*/,
    LPCWSTR /*context*/) noexcept
{
    return S_OK;
}
