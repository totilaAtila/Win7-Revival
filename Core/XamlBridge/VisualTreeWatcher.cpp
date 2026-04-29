//
// VisualTreeWatcher.cpp - Tree monitoring + brush application  [ITER #18]
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
static std::mutex                  g_rootHandlesMtx;
static std::vector<InstanceHandle> g_rootHandles;
static std::atomic<LONG>           g_activeDiscoveryPass { 0 };

static bool IsInterestingRootCandidate(const VisualElement& element, const ParentChildRelation& relation)
{
    if (element.Handle == 0)
        return false;

    if (relation.Parent == 0)
        return true;

    const wchar_t* type = element.Type ? element.Type : L"";
    const wchar_t* name = element.Name ? element.Name : L"";
    return wcsstr(type, L"RootScrollViewer") ||
           wcsstr(type, L"ScrollContentPresenter") ||
           wcsstr(type, L"TaskbarFrame") ||
           wcsstr(type, L"TaskbarBackground") ||
           wcscmp(name, L"RootGrid") == 0 ||
           wcscmp(name, L"TaskbarFrame") == 0 ||
           wcscmp(name, L"BackgroundControl") == 0;
}

static bool CacheRootHandle(InstanceHandle handle)
{
    if (handle == 0)
        return false;

    std::lock_guard<std::mutex> lk(g_rootHandlesMtx);
    if (std::find(g_rootHandles.begin(), g_rootHandles.end(), handle) == g_rootHandles.end()) {
        g_rootHandles.push_back(handle);
        XBLogFmt(L"ActiveDiscovery: cached root/container handle=0x%llX",
            static_cast<unsigned long long>(handle));
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// DispatchApplyToBgElement — Apply brush to a BackgroundFill/Stroke element.
//
// [ITER #21] Design change: WalkTaskbarBgTree is now always called from the
// XAML UI thread (OnVisualTreeChange fires on that thread on 25H2, confirmed
// by the direct walk succeeding).  We therefore apply the brush SYNCHRONOUSLY
// here — no TryRunAsync needed, and none of the "dispatcher not pumping" issues.
//
// g_knownShapes is also updated so WorkerThread can re-apply on settings change
// via SendNotifyMessageW → hookproc → fresh tree replay.
// ---------------------------------------------------------------------------
static void DispatchApplyToBgElement(
    InstanceHandle handle,
    const wux::FrameworkElement& fe,
    BrushTargetProp prop,
    const wchar_t* logTag)
{
    try {
        // Track in g_knownShapes so WorkerThread can re-apply on settings change
        {
            std::lock_guard<std::mutex> lk(g_shapesMtx);
            bool found = std::any_of(g_knownShapes.begin(), g_knownShapes.end(),
                [handle, &fe, prop](const ShapeEntry& e) {
                    if (handle != 0 && e.handle == handle)
                        return true;
                    return e.prop == prop && e.element == fe;
                });
            if (!found)
                g_knownShapes.emplace_back(handle, fe, prop);
        }

        // Apply directly — we are on the XAML UI thread (OnVisualTreeChange fires there).
        if (!g_pState) {
            XBLogFmt(L"%s DirectApply: g_pState null — brush deferred to WorkerThread", logTag);
            return;
        }

        auto p = ReadBrushParams(g_pState);
        XBLogFmt(L"%s DirectApply: enabled=%d alpha=%d rgb=(%d,%d,%d)",
            logTag, p.enabled ? 1 : 0, p.alpha, p.r, p.g, p.b);

        auto targetProp = (prop == BrushTargetProp::Stroke)
            ? wuxs::Shape::StrokeProperty()
            : wuxs::Shape::FillProperty();

        if (!p.enabled) {
            fe.ClearValue(targetProp);
            XBLogFmt(L"%s DirectApply: ClearValue (enabled=0)", logTag);
            return;
        }

        wu::Color color{ p.alpha, p.r, p.g, p.b };
        wuxm::SolidColorBrush brush{};
        brush.Color(color);

        if (prop == BrushTargetProp::Fill) {
            auto disp = fe.Dispatcher();
            if (disp) {
                auto asyncOp = disp.TryRunAsync(wuc::CoreDispatcherPriority::High,
                    [fe, targetProp, color, logTag]() noexcept {
                        try {
                            wuxm::SolidColorBrush delayedBrush{};
                            delayedBrush.Color(color);
                            fe.SetValue(targetProp, delayedBrush);
                            XBLogFmt(L"%s DirectApply: delayed BackgroundFill SetValue DONE alpha=%d rgb=(%d,%d,%d)",
                                logTag, color.A, color.R, color.G, color.B);
                        }
                        catch (const winrt::hresult_error& ex) {
                            XBLogFmt(L"%s DirectApply: delayed BackgroundFill SetValue threw 0x%08X: %s",
                                logTag, ex.code(), ex.message().c_str());
                        }
                        catch (...) {
                            XBLogFmt(L"%s DirectApply: delayed BackgroundFill SetValue threw unknown", logTag);
                        }
                    });
                if (asyncOp) {
                    g_bgFillAsyncAction = asyncOp;
                    XBLogFmt(L"%s DirectApply: queued delayed BackgroundFill SetValue", logTag);
                    return;
                }
            }
        }

        fe.SetValue(targetProp, brush);
        XBLogFmt(L"%s DirectApply: SetValue DONE alpha=%d rgb=(%d,%d,%d)",
            logTag, p.alpha, p.r, p.g, p.b);
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s DispatchApply threw 0x%08X: %s", logTag, ex.code(), ex.message().c_str());
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"%s DispatchApply std::exception: %S", logTag, ex.what());
    }
    catch (...) {
        XBLogFmt(L"%s DispatchApply threw unknown exception", logTag);
    }
}

struct ActiveWalkContext {
    int visited = 0;
    int maxDepth = 48;
    int maxNodes = 4096;
    bool foundAny = false;
};

static std::wstring SafeClassName(const winrt::Windows::Foundation::IInspectable& obj)
{
    try {
        return winrt::get_class_name(obj).c_str();
    }
    catch (...) {
        return L"";
    }
}

static std::wstring SafeElementName(const wux::FrameworkElement& fe)
{
    try {
        return fe.Name().c_str();
    }
    catch (...) {
        return L"";
    }
}

static bool IsTaskbarBackgroundTarget(
    const winrt::Windows::Foundation::IInspectable& obj,
    const wux::FrameworkElement& fe,
    BrushTargetProp& prop)
{
    auto name = SafeElementName(fe);
    if (name != L"BackgroundFill" && name != L"BackgroundStroke")
        return false;

    auto className = SafeClassName(obj);
    if (className.find(L"Rectangle") == std::wstring::npos)
        return false;

    prop = (name == L"BackgroundStroke") ? BrushTargetProp::Stroke : BrushTargetProp::Fill;
    return true;
}

static bool HasTaskbarBackgroundAncestor(const winrt::Windows::Foundation::IInspectable& obj) noexcept
{
    try {
        auto current = obj.try_as<wux::DependencyObject>();
        for (int depth = 0; current && depth < 32; ++depth) {
            auto parent = wuxm::VisualTreeHelper::GetParent(current);
            if (!parent)
                return false;

            auto parentInspectable = parent.as<winrt::Windows::Foundation::IInspectable>();
            auto parentClass = SafeClassName(parentInspectable);
            auto parentFe = parent.try_as<wux::FrameworkElement>();
            auto parentName = parentFe ? SafeElementName(parentFe) : std::wstring{};

            if (parentClass.find(L"TaskbarBackground") != std::wstring::npos ||
                parentName == L"BackgroundControl") {
                return true;
            }

            current = parent;
        }
    }
    catch (...) {
    }

    return false;
}

static bool ActiveWalkElement(
    const winrt::Windows::Foundation::IInspectable& obj,
    int depth,
    bool seenTaskbarFrame,
    bool seenRootGrid,
    bool seenTaskbarBg,
    ActiveWalkContext& ctx) noexcept
{
    if (!obj || depth > ctx.maxDepth || ctx.visited >= ctx.maxNodes)
        return false;

    ++ctx.visited;

    try {
        auto fe = obj.try_as<wux::FrameworkElement>();
        if (fe) {
            auto name = SafeElementName(fe);
            auto className = SafeClassName(obj);

            bool nextSeenTaskbarFrame = seenTaskbarFrame ||
                name == L"TaskbarFrame" ||
                className.find(L"TaskbarFrame") != std::wstring::npos;
            bool nextSeenRootGrid = seenRootGrid || name == L"RootGrid";
            bool nextSeenTaskbarBg = seenTaskbarBg ||
                name == L"BackgroundControl" ||
                className.find(L"TaskbarBackground") != std::wstring::npos;

            BrushTargetProp prop = BrushTargetProp::Fill;
            if (IsTaskbarBackgroundTarget(obj, fe, prop)) {
                bool preferredPath = nextSeenTaskbarFrame || nextSeenRootGrid || nextSeenTaskbarBg;
                bool targetPath = nextSeenTaskbarBg || HasTaskbarBackgroundAncestor(obj);
                InstanceHandle handle = 0;
                if (g_xamlDiagnostics) {
                    auto inspectable = obj.as<winrt::Windows::Foundation::IInspectable>();
                    HRESULT hr = g_xamlDiagnostics->GetHandleFromIInspectable(
                        reinterpret_cast<::IInspectable*>(winrt::get_abi(inspectable)),
                        &handle);
                    XBLogFmt(L"ActiveDiscovery: found %s depth=%d preferredPath=%d targetPath=%d handleHr=0x%08X handle=0x%llX",
                        name.c_str(), depth, preferredPath ? 1 : 0, targetPath ? 1 : 0, hr,
                        static_cast<unsigned long long>(handle));
                }
                else {
                    XBLogFmt(L"ActiveDiscovery: found %s depth=%d preferredPath=%d targetPath=%d without diagnostics",
                        name.c_str(), depth, preferredPath ? 1 : 0, targetPath ? 1 : 0);
                }

                if (targetPath) {
                    DispatchApplyToBgElement(handle, fe, prop, L"[ActiveDiscovery]");
                    ctx.foundAny = true;
                }
            }

            seenTaskbarFrame = nextSeenTaskbarFrame;
            seenRootGrid = nextSeenRootGrid;
            seenTaskbarBg = nextSeenTaskbarBg;
        }

        auto depObj = obj.try_as<wux::DependencyObject>();
        if (!depObj)
            return ctx.foundAny;

        int childCount = 0;
        try {
            childCount = wuxm::VisualTreeHelper::GetChildrenCount(depObj);
        }
        catch (const winrt::hresult_error& ex) {
            if (depth <= 2) {
                XBLogFmt(L"ActiveDiscovery: GetChildrenCount depth=%d threw 0x%08X: %s",
                    depth, ex.code(), ex.message().c_str());
            }
            return ctx.foundAny;
        }

        for (int i = 0; i < childCount && ctx.visited < ctx.maxNodes; ++i) {
            winrt::Windows::Foundation::IInspectable child;
            try {
                child = wuxm::VisualTreeHelper::GetChild(depObj, i);
            }
            catch (const winrt::hresult_error& ex) {
                if (depth <= 2)
                    XBLogFmt(L"ActiveDiscovery: GetChild depth=%d index=%d threw 0x%08X", depth, i, ex.code());
                continue;
            }
            catch (...) {
                if (depth <= 2)
                    XBLogFmt(L"ActiveDiscovery: GetChild depth=%d index=%d threw unknown", depth, i);
                continue;
            }

            ActiveWalkElement(child, depth + 1, seenTaskbarFrame, seenRootGrid, seenTaskbarBg, ctx);
        }
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"ActiveDiscovery: walk element threw 0x%08X: %s", ex.code(), ex.message().c_str());
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"ActiveDiscovery: walk element std::exception: %S", ex.what());
    }
    catch (...) {
        XBLog(L"ActiveDiscovery: walk element threw unknown");
    }

    return ctx.foundAny;
}

bool XamlBridgeTryActiveTreeDiscovery()
{
    std::vector<InstanceHandle> handles;
    {
        std::lock_guard<std::mutex> lk(g_rootHandlesMtx);
        handles = g_rootHandles;
    }

    if (handles.empty()) {
        XBLog(L"ActiveDiscovery: skipped - no cached root/container handles yet");
        return false;
    }

    if (!g_xamlDiagnostics) {
        XBLog(L"ActiveDiscovery: skipped - g_xamlDiagnostics is null");
        return false;
    }

    LONG pass = g_activeDiscoveryPass.fetch_add(1) + 1;
    XBLogFmt(L"ActiveDiscovery: pass %d starting from %u cached handle(s)",
        pass, static_cast<unsigned int>(handles.size()));

    bool foundAny = false;
    for (InstanceHandle handle : handles) {
        winrt::Windows::Foundation::IInspectable root;
        HRESULT hr = g_xamlDiagnostics->GetIInspectableFromHandle(
            handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(root)));
        XBLogFmt(L"ActiveDiscovery: root handle=0x%llX GetIInspectable hr=0x%08X obj=%s",
            static_cast<unsigned long long>(handle), hr, root ? L"OK" : L"NULL");
        if (FAILED(hr) || !root)
            continue;

        ActiveWalkContext ctx;
        ActiveWalkElement(root, 0, false, false, false, ctx);
        XBLogFmt(L"ActiveDiscovery: root handle=0x%llX visited=%d found=%d",
            static_cast<unsigned long long>(handle), ctx.visited, ctx.foundAny ? 1 : 0);
        foundAny = foundAny || ctx.foundAny;
        if (foundAny)
            break;
    }

    XBLogFmt(L"ActiveDiscovery: pass %d complete found=%d", pass, foundAny ? 1 : 0);
    return foundAny;
}

static void QueueActiveDiscoveryFromHookProc(const wchar_t* reason) noexcept
{
    HWND hwndTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hwndTray) {
        XBLogFmt(L"ActiveDiscovery: %s could not queue hookproc ping - Shell_TrayWnd not found", reason);
        return;
    }

    // Post, do not send: OnVisualTreeChange may be running on the same UI thread.
    // A synchronous send can re-enter XAML diagnostics while it is replaying the tree.
    if (PostMessageW(hwndTray, WM_NULL, 0, 0)) {
        XBLogFmt(L"ActiveDiscovery: %s queued hookproc ping", reason);
    } else {
        XBLogFmt(L"ActiveDiscovery: %s PostMessageW failed e=%u", reason, GetLastError());
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

            XBLogFmt(L"%s child[%d] BEFORE Name() call", logTag, ci);
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

            XBLogFmt(L"%s child[%d] AFTER Name() call, checking grandchildren", logTag, ci);

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
                DispatchApplyToBgElement(grandHandle, grandFE, prop, logTag);
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
// VisualTreeWatcher::OnVisualTreeChange
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE VisualTreeWatcher::OnVisualTreeChange(
    ParentChildRelation relation,
    VisualElement element,
    VisualMutationType mutationType) noexcept
{
    try {
        if (mutationType == Remove) {
            {
                std::lock_guard<std::mutex> rootsLock(g_rootHandlesMtx);
                auto rootIt = std::remove(g_rootHandles.begin(), g_rootHandles.end(), element.Handle);
                if (rootIt != g_rootHandles.end())
                    g_rootHandles.erase(rootIt, g_rootHandles.end());
            }
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

        if (IsInterestingRootCandidate(element, relation)) {
            bool cachedNewRoot = CacheRootHandle(element.Handle);
            if (cachedNewRoot) {
                bool shapesFound = false;
                {
                    std::lock_guard<std::mutex> lk(g_shapesMtx);
                    shapesFound = !g_knownShapes.empty();
                }

                if (!shapesFound) {
                    QueueActiveDiscoveryFromHookProc(L"OnVisualTreeChange");
                }
            }
        }

        // Partial match: catches "Taskbar.TaskbarBackground", "TaskbarFrame.TaskbarBackground", etc.
        bool isTaskbarBg = (element.Type && wcsstr(element.Type, L"TaskbarBackground") != nullptr);
        if (element.Type && wcsstr(element.Type, L"Background") != nullptr && !isTaskbarBg) {
            XBLogFmt(L"  [Background-type] handle=0x%llX  Type=%s  Name=%s",
                static_cast<unsigned long long>(element.Handle),
                element.Type,
                element.Name ? element.Name : L"(null)");
        }
        if (isTaskbarBg) {
            g_taskbarBgHandle.store(element.Handle);

            winrt::Windows::Foundation::IInspectable bgObj;
            HRESULT hr = m_diagnostics->GetIInspectableFromHandle(
                element.Handle,
                reinterpret_cast<::IInspectable**>(winrt::put_abi(bgObj)));
            XBLogFmt(L"  *** TaskbarBackground 0x%llX - GetIInspectable hr=0x%08X obj=%s ***",
                static_cast<unsigned long long>(element.Handle), hr, bgObj ? L"OK" : L"NULL");
            if (SUCCEEDED(hr) && bgObj) {
                auto bgFe = bgObj.try_as<wux::FrameworkElement>();
                if (bgFe) {
                    // ── Attempt 1: synchronous walk (works if already on XAML UI thread) ──
                    // On 25H2 OnVisualTreeChange fires on the Shell_TrayWnd/XAML thread,
                    // so VisualTreeHelper is legal here without TryRunAsync.
                    bool directWalkOk = false;
                    try {
                        int childCount = wuxm::VisualTreeHelper::GetChildrenCount(bgFe);
                        XBLogFmt(L"  *** [ITER#21] Direct walk OK: TaskbarBg childCount=%d ***",
                            childCount);
                        directWalkOk = true;
                        if (!WalkTaskbarBgTree(bgFe, m_diagnostics.get(), L"[ITER#21-Direct]"))
                            XBLog(L"  *** [ITER#21] Direct walk: no BackgroundFill/Stroke found ***");
                    }
                    catch (const winrt::hresult_error& ex) {
                        XBLogFmt(L"  *** [ITER#21] Direct walk threw 0x%08X — falling back to "
                                 L"TryRunAsync ***", ex.code());
                    }
                    catch (...) {
                        XBLog(L"  *** [ITER#21] Direct walk threw unknown — falling back to "
                              L"TryRunAsync ***");
                    }

                    // ── Attempt 2: async walk via Dispatcher (fallback for wrong-thread case) ──
                    if (!directWalkOk) {
                        auto disp = bgFe.Dispatcher();
                        if (disp) {
                            auto asyncOp = disp.TryRunAsync(wuc::CoreDispatcherPriority::High,
                                [bgFe, diag = m_diagnostics]() noexcept {
                                    XBLog(L"[ITER#21-Async] walk started (UI thread)");
                                    if (!WalkTaskbarBgTree(bgFe, diag.get(), L"[ITER#21-Async]"))
                                        XBLog(L"[ITER#21-Async] no BackgroundFill/Stroke found");
                                });
                            g_walkAsyncAction = asyncOp;
                            XBLogFmt(L"  *** TaskbarBackground - async walk queued asyncOp=%s ***",
                                asyncOp ? L"valid" : L"null");
                        } else {
                            XBLog(L"  *** TaskbarBackground - no Dispatcher, cannot walk ***");
                        }
                    }
                    return S_OK;
                }
            }
            XBLogFmt(L"  *** TaskbarBackground - walk FAILED hr=0x%08X ***", hr);
            return S_OK;
        }

        // Catch BackgroundFill/BackgroundStroke if TAP delivers them as live Add notifications.
        // Also log ALL Rectangle elements so we can discover name changes on newer 25H2 builds.
        bool isRectangle = element.Type && wcsstr(element.Type, L"Rectangle") != nullptr;
        if (!isRectangle)
            return S_OK;

        winrt::Windows::Foundation::IInspectable obj;
        HRESULT hr = m_diagnostics->GetIInspectableFromHandle(
            element.Handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
        if (FAILED(hr) || !obj)
            return S_OK;

        auto fe = obj.try_as<wux::FrameworkElement>();
        if (!fe) return S_OK;

        auto runtimeName = fe.Name();
        // Log ALL rectangles so we can see the real element names on 25H2 [ITER#21]
        XBLogFmt(L"  *** Rectangle live: Name='%s' handle=0x%llX ***",
            runtimeName.c_str(), static_cast<unsigned long long>(element.Handle));

        bool isBgFill   = (runtimeName == L"BackgroundFill");
        bool isBgStroke = (runtimeName == L"BackgroundStroke");
        if (isBgFill || isBgStroke) {
            BrushTargetProp prop = isBgStroke ? BrushTargetProp::Stroke : BrushTargetProp::Fill;
            XBLogFmt(L"  *** FOUND %s via TAP live notification ***", runtimeName.c_str());
            DispatchApplyToBgElement(element.Handle, fe, prop, L"[TAP-live]");
        }

        return S_OK;
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
