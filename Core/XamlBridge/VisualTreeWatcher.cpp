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
static std::mutex                  g_timersMtx;
static std::vector<wust::ThreadPoolTimer> g_pendingTimers;

static void RegisterAndApplyTarget(InstanceHandle handle, const wux::FrameworkElement& element, BrushTargetProp prop);
static bool QueueDelayedApply(
    InstanceHandle handle,
    const wux::FrameworkElement& element,
    BrushTargetProp prop,
    const wchar_t* logTag,
    const wchar_t* elementName,
    int delayMs) noexcept;

static void FreeCollectionElements(unsigned int count, CollectionElementValue* values)
{
    if (!values) return;
    for (unsigned int i = 0; i < count; ++i) {
        SysFreeString(values[i].ValueType);
        SysFreeString(values[i].Value);
    }
    CoTaskMemFree(values);
}

static bool TryParseHandleString(const wchar_t* text, InstanceHandle& outHandle)
{
    outHandle = 0;
    if (!text || !*text)
        return false;

    wchar_t* end = nullptr;
    unsigned long long value = wcstoull(text, &end, 0);
    if (end == text || (end && *end != L'\0'))
        return false;

    outHandle = static_cast<InstanceHandle>(value);
    return outHandle != 0;
}

static bool TryGetObjectPropertyHandle(
    InstanceHandle objectHandle,
    const wchar_t* propertyName,
    InstanceHandle& outValueHandle)
{
    outValueHandle = 0;
    if (!g_visualTreeService3) {
        XBLogFmt(L"TryGetObjectPropertyHandle(%s): g_visualTreeService3 is null", propertyName);
        return false;
    }

    unsigned int propertyIndex = UINT_MAX;
    HRESULT hr = g_visualTreeService3->GetPropertyIndex(objectHandle, propertyName, &propertyIndex);
    if (FAILED(hr)) {
        XBLogFmt(L"TryGetObjectPropertyHandle(%s): GetPropertyIndex failed hr=0x%08X object=0x%llX",
            propertyName, hr, static_cast<unsigned long long>(objectHandle));
        return false;
    }

    hr = g_visualTreeService3->GetProperty(objectHandle, propertyIndex, &outValueHandle);
    XBLogFmt(L"TryGetObjectPropertyHandle(%s): GetProperty hr=0x%08X object=0x%llX index=%u value=0x%llX",
        propertyName,
        hr,
        static_cast<unsigned long long>(objectHandle),
        propertyIndex,
        static_cast<unsigned long long>(outValueHandle));
    if (FAILED(hr) || outValueHandle == 0) {
        outValueHandle = 0;
        return false;
    }

    return true;
}

static bool TryGetCollectionPropertyHandles(
    InstanceHandle objectHandle,
    const wchar_t* propertyName,
    std::vector<InstanceHandle>& outHandles)
{
    outHandles.clear();

    InstanceHandle collectionHandle = 0;
    if (!TryGetObjectPropertyHandle(objectHandle, propertyName, collectionHandle))
        return false;

    unsigned int count = 0;
    HRESULT hr = g_visualTreeService3->GetCollectionCount(collectionHandle, &count);
    XBLogFmt(L"TryGetCollectionPropertyHandles(%s): GetCollectionCount hr=0x%08X collection=0x%llX count=%u",
        propertyName, hr, static_cast<unsigned long long>(collectionHandle), count);
    if (FAILED(hr) || count == 0)
        return false;

    unsigned int fetched = count;
    CollectionElementValue* values = nullptr;
    hr = g_visualTreeService3->GetCollectionElements(collectionHandle, 0, &fetched, &values);
    XBLogFmt(L"TryGetCollectionPropertyHandles(%s): GetCollectionElements hr=0x%08X fetched=%u",
        propertyName, hr, fetched);
    if (FAILED(hr) || !values) {
        FreeCollectionElements(fetched, values);
        return false;
    }

    for (unsigned int i = 0; i < fetched; ++i) {
        InstanceHandle childHandle = 0;
        bool parsed = TryParseHandleString(values[i].Value, childHandle);
        XBLogFmt(L"  collection[%u]: value=%s parsed=%d handle=0x%llX bits=0x%llX valueType=%s",
            i,
            values[i].Value ? values[i].Value : L"(null)",
            parsed ? 1 : 0,
            static_cast<unsigned long long>(childHandle),
            static_cast<unsigned long long>(values[i].MetadataBits),
            values[i].ValueType ? values[i].ValueType : L"(null)");
        if (parsed && childHandle != 0)
            outHandles.push_back(childHandle);
    }

    FreeCollectionElements(fetched, values);
    return !outHandles.empty();
}

static std::wstring DescribeHandleObject(IXamlDiagnostics* diag, InstanceHandle handle)
{
    if (!diag || handle == 0)
        return L"(diag=null or handle=0)";

    try {
        winrt::Windows::Foundation::IInspectable obj;
        HRESULT hr = diag->GetIInspectableFromHandle(
            handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
        if (FAILED(hr) || !obj)
            return L"GetIInspectableFromHandle failed";

        std::wstring className;
        try {
            className = winrt::get_class_name(obj).c_str();
        }
        catch (...) {
            className = L"(class unknown)";
        }

        auto fe = obj.try_as<wux::FrameworkElement>();
        std::wstring name = fe ? std::wstring(fe.Name().c_str()) : L"";
        return className + L" Name='" + name + L"'";
    }
    catch (...) {
        return L"(describe threw)";
    }
}

static bool TryQueueTargetByHandle(
    InstanceHandle handle,
    IXamlDiagnostics* diag,
    const wchar_t* logTag,
    const wchar_t* stageLabel)
{
    if (!diag || handle == 0)
        return false;

    try {
        winrt::Windows::Foundation::IInspectable obj;
        HRESULT hr = diag->GetIInspectableFromHandle(
            handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
        XBLogFmt(L"%s %s: GetIInspectableFromHandle hr=0x%08X handle=0x%llX",
            logTag, stageLabel, hr, static_cast<unsigned long long>(handle));
        if (FAILED(hr) || !obj)
            return false;

        auto fe = obj.try_as<wux::FrameworkElement>();
        if (!fe) {
            XBLogFmt(L"%s %s: handle=0x%llX is not FrameworkElement",
                logTag, stageLabel, static_cast<unsigned long long>(handle));
            return false;
        }

        auto runtimeName = fe.Name();
        std::wstring className;
        try {
            className = winrt::get_class_name(obj).c_str();
        }
        catch (...) {
            className = L"(class unknown)";
        }

        XBLogFmt(L"%s %s: handle=0x%llX class=%s name=%s",
            logTag,
            stageLabel,
            static_cast<unsigned long long>(handle),
            className.c_str(),
            runtimeName.c_str());

        bool isFill = (runtimeName == L"BackgroundFill");
        bool isStroke = (runtimeName == L"BackgroundStroke");
        if (!isFill && !isStroke)
            return false;

        BrushTargetProp prop = isStroke ? BrushTargetProp::Stroke : BrushTargetProp::Fill;
        bool queuedNow = QueueDelayedApply(handle, fe, prop, logTag, runtimeName.c_str(), 0);
        XBLogFmt(L"%s %s: QueueDelayedApply %s (0 ms) => %d",
            logTag, stageLabel, runtimeName.c_str(), queuedNow ? 1 : 0);
        if (isFill) {
            bool queuedRetry = QueueDelayedApply(handle, fe, prop, logTag, runtimeName.c_str(), 500);
            XBLogFmt(L"%s %s: QueueDelayedApply %s (500 ms) => %d",
                logTag, stageLabel, runtimeName.c_str(), queuedRetry ? 1 : 0);
        }
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s %s: TryQueueTargetByHandle threw 0x%08X: %s",
            logTag, stageLabel, ex.code(), ex.message().c_str());
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"%s %s: TryQueueTargetByHandle std::exception: %S",
            logTag, stageLabel, ex.what());
    }
    catch (...) {
        XBLogFmt(L"%s %s: TryQueueTargetByHandle threw unknown", logTag, stageLabel);
    }

    return false;
}

static bool QueryTaskbarBackgroundByHandle(InstanceHandle bgHandle, IXamlDiagnostics* diag, const wchar_t* logTag)
{
    if (!bgHandle || !diag || !g_visualTreeService3) {
        XBLogFmt(L"%s native query unavailable bg=0x%llX diag=%d svc3=%d",
            logTag,
            static_cast<unsigned long long>(bgHandle),
            diag ? 1 : 0,
            g_visualTreeService3 ? 1 : 0);
        return false;
    }

    bool foundAny = false;
    std::vector<InstanceHandle> level1;
    std::vector<InstanceHandle> level2;

    const wchar_t* directCandidates[] = { L"Child", L"Content", L"ContentTemplateRoot" };
    for (const wchar_t* propName : directCandidates) {
        InstanceHandle childHandle = 0;
        if (TryGetObjectPropertyHandle(bgHandle, propName, childHandle)) {
            XBLogFmt(L"%s bg.%s => handle=0x%llX %s",
                logTag,
                propName,
                static_cast<unsigned long long>(childHandle),
                DescribeHandleObject(diag, childHandle).c_str());
            level1.push_back(childHandle);
        }
    }

    const wchar_t* collectionCandidates[] = { L"Children", L"Items" };
    for (const wchar_t* propName : collectionCandidates) {
        std::vector<InstanceHandle> handles;
        if (TryGetCollectionPropertyHandles(bgHandle, propName, handles)) {
            XBLogFmt(L"%s bg.%s => %u handles", logTag, propName, static_cast<unsigned int>(handles.size()));
            for (auto handle : handles) {
                XBLogFmt(L"%s bg.%s child handle=0x%llX %s",
                    logTag,
                    propName,
                    static_cast<unsigned long long>(handle),
                    DescribeHandleObject(diag, handle).c_str());
                level1.push_back(handle);
            }
        }
    }

    if (level1.empty()) {
        XBLogFmt(L"%s no level1 handles discovered for TaskbarBackground handle=0x%llX",
            logTag, static_cast<unsigned long long>(bgHandle));
        return false;
    }

    for (auto handle : level1) {
        if (TryQueueTargetByHandle(handle, diag, logTag, L"[L1]"))
            foundAny = true;

        for (const wchar_t* propName : directCandidates) {
            InstanceHandle childHandle = 0;
            if (TryGetObjectPropertyHandle(handle, propName, childHandle)) {
                XBLogFmt(L"%s level1.%s => handle=0x%llX %s",
                    logTag,
                    propName,
                    static_cast<unsigned long long>(childHandle),
                    DescribeHandleObject(diag, childHandle).c_str());
                level2.push_back(childHandle);
            }
        }

        for (const wchar_t* propName : collectionCandidates) {
            std::vector<InstanceHandle> handles;
            if (TryGetCollectionPropertyHandles(handle, propName, handles)) {
                XBLogFmt(L"%s level1.%s => %u handles", logTag, propName, static_cast<unsigned int>(handles.size()));
                for (auto grandHandle : handles) {
                    XBLogFmt(L"%s level1.%s child handle=0x%llX %s",
                        logTag,
                        propName,
                        static_cast<unsigned long long>(grandHandle),
                        DescribeHandleObject(diag, grandHandle).c_str());
                    level2.push_back(grandHandle);
                }
            }
        }
    }

    for (auto handle : level2) {
        if (TryQueueTargetByHandle(handle, diag, logTag, L"[L2]"))
            foundAny = true;
    }

    if (!foundAny) {
        XBLogFmt(L"%s native query completed but did not find BackgroundFill/BackgroundStroke",
            logTag);
    }

    return foundAny;
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

static bool QueueDelayedApply(
    InstanceHandle handle,
    const wux::FrameworkElement& element,
    BrushTargetProp prop,
    const wchar_t* logTag,
    const wchar_t* elementName,
    int delayMs) noexcept
{
    try {
        auto disp = element.Dispatcher();
        if (!disp) {
            XBLogFmt(L"%s %s: Dispatcher() returned null for delayed apply (%d ms)",
                logTag, elementName, delayMs);
            return false;
        }

        auto runApply = [handle, element, prop,
                         tag = std::wstring(logTag),
                         name = std::wstring(elementName),
                         delayMs]() noexcept {
            try {
                XBLogFmt(L"%s [%dms] dispatcher apply entered for %s handle=0x%llX",
                    tag.c_str(), delayMs, name.c_str(), static_cast<unsigned long long>(handle));
                RegisterAndApplyTarget(handle, element, prop);
                XBLogFmt(L"%s [%dms] RegisterAndApplyTarget finished for %s",
                    tag.c_str(), delayMs, name.c_str());
            }
            catch (const winrt::hresult_error& ex) {
                XBLogFmt(L"%s [%dms] dispatcher apply threw 0x%08X for %s: %s",
                    tag.c_str(), delayMs, ex.code(), name.c_str(), ex.message().c_str());
            }
            catch (const std::exception& ex) {
                XBLogFmt(L"%s [%dms] dispatcher apply std::exception for %s: %S",
                    tag.c_str(), delayMs, name.c_str(), ex.what());
            }
            catch (...) {
                XBLogFmt(L"%s [%dms] dispatcher apply threw unknown for %s",
                    tag.c_str(), delayMs, name.c_str());
            }
        };

        if (delayMs <= 0) {
            XBLogFmt(L"%s %s: queueing dispatcher apply (%d ms)", logTag, elementName, delayMs);
            auto action = disp.RunAsync(wuc::CoreDispatcherPriority::Normal, runApply);
            XBLogFmt(L"%s %s: Dispatcher.RunAsync returned %s for %d ms",
                logTag, elementName, action ? L"action" : L"null", delayMs);
            return static_cast<bool>(action);
        }

        auto timer = wust::ThreadPoolTimer::CreateTimer(
            [disp, runApply, tag = std::wstring(logTag), name = std::wstring(elementName), delayMs]
            (wust::ThreadPoolTimer const&) noexcept {
                try {
                    XBLogFmt(L"%s %s: timer fired after %d ms", tag.c_str(), name.c_str(), delayMs);
                    disp.RunAsync(wuc::CoreDispatcherPriority::High, runApply);
                }
                catch (const winrt::hresult_error& ex) {
                    XBLogFmt(L"%s %s: timer callback threw 0x%08X after %d ms: %s",
                        tag.c_str(), name.c_str(), ex.code(), delayMs, ex.message().c_str());
                }
                catch (...) {
                    XBLogFmt(L"%s %s: timer callback threw unknown after %d ms",
                        tag.c_str(), name.c_str(), delayMs);
                }
            },
            std::chrono::milliseconds(delayMs));

        {
            std::lock_guard<std::mutex> lk(g_timersMtx);
            g_pendingTimers.push_back(timer);
        }
        XBLogFmt(L"%s %s: scheduled delayed apply timer (%d ms)", logTag, elementName, delayMs);
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        XBLogFmt(L"%s %s: QueueDelayedApply threw 0x%08X for %d ms: %s",
            logTag, elementName, ex.code(), delayMs, ex.message().c_str());
    }
    catch (const std::exception& ex) {
        XBLogFmt(L"%s %s: QueueDelayedApply std::exception for %d ms: %S",
            logTag, elementName, delayMs, ex.what());
    }
    catch (...) {
        XBLogFmt(L"%s %s: QueueDelayedApply threw unknown for %d ms",
            logTag, elementName, delayMs);
    }

    return false;
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
                XBLogFmt(L"%s queueing delayed apply for %s (0 ms)", logTag, grandName.c_str());
                bool queuedNow = QueueDelayedApply(grandHandle, grandFE, prop, logTag, grandName.c_str(), 0);
                XBLogFmt(L"%s delayed apply queue result for %s (0 ms): %d", logTag, grandName.c_str(), queuedNow ? 1 : 0);
                if (isBgFill) {
                    XBLogFmt(L"%s queueing delayed retry for %s (500 ms)", logTag, grandName.c_str());
                    bool queuedRetry = QueueDelayedApply(grandHandle, grandFE, prop, logTag, grandName.c_str(), 500);
                    XBLogFmt(L"%s delayed apply queue result for %s (500 ms): %d", logTag, grandName.c_str(), queuedRetry ? 1 : 0);
                }
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
            XBLogFmt(L"  *** TaskbarBackground handle=0x%llX - native handle query starting ***",
                static_cast<unsigned long long>(element.Handle));
            bool foundViaNativeQuery = QueryTaskbarBackgroundByHandle(element.Handle, m_diagnostics.get(), L"[NativeQuery]");
            XBLogFmt(L"  *** TaskbarBackground native query result: found=%d ***", foundViaNativeQuery ? 1 : 0);
            if (!foundViaNativeQuery) {
                XBLog(L"  *** Native query did not find BackgroundFill / BackgroundStroke; still watching TAP child notifications ***");
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
            bool queuedNow = QueueDelayedApply(element.Handle, fe, prop, L"[TAP]", runtimeName.c_str(), 0);
            XBLogFmt(L"  >>> QueueDelayedApply %s (0 ms) => %d", runtimeName.c_str(), queuedNow ? 1 : 0);
            if (runtimeName == L"BackgroundFill") {
                bool queuedRetry = QueueDelayedApply(element.Handle, fe, prop, L"[TAP]", runtimeName.c_str(), 500);
                XBLogFmt(L"  >>> QueueDelayedApply %s (500 ms) => %d", runtimeName.c_str(), queuedRetry ? 1 : 0);
            }
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
