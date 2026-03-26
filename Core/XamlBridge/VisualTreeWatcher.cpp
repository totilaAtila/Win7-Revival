//
// VisualTreeWatcher.cpp — Tree monitoring + brush application  [ITER #12]
//
// ITER #12 changes vs ITER #11:
//   - isTaskbarBg: resolves Taskbar.TaskbarBackground IInspectable (was skipped before)
//   - VTH walk: when TaskbarBackground arrives, actively walk its descendants with
//     VisualTreeHelper::GetChild to find Rectangle#BackgroundFill/BackgroundStroke
//     (bypasses incomplete AdviseVisualTreeChange replay that stops at TaskbarBackground)
//   - RegisterAndApplyShape helper: dedup + apply, supports both handle-tracked and
//     VTH-walked (handle=0) shapes
//
#include "VisualTreeWatcher.h"

// ---------------------------------------------------------------------------
// Diagnostic state (shared across all watcher instances)
// ---------------------------------------------------------------------------
static std::atomic<int>            g_loggedElements    { 0 };
static constexpr int               kMaxLoggedElements  = 10000; // full dump
static std::atomic<InstanceHandle> g_taskbarBgHandle   { 0 };   // handle of Taskbar.TaskbarBackground

// ---------------------------------------------------------------------------
// Brush helpers
// ---------------------------------------------------------------------------

BrushParams ReadBrushParams(const SharedBlurState* s)
{
    BrushParams p = {};
    if (!s) return p;
    LONG en  = InterlockedCompareExchange(const_cast<volatile LONG*>(&s->blurEnabled), 0, 0);
    LONG op  = InterlockedCompareExchange(const_cast<volatile LONG*>(&s->opacityPct),  75, 75);
    LONG amt = InterlockedCompareExchange(const_cast<volatile LONG*>(&s->blurAmount),   0, 0);
    p.enabled = (en != 0);
    p.useBlur = (amt > 0);
    p.alpha   = static_cast<BYTE>(((100 - op) * 255) / 100);
    p.r       = static_cast<BYTE>(InterlockedCompareExchange(const_cast<volatile LONG*>(&s->colorR), 0, 0));
    p.g       = static_cast<BYTE>(InterlockedCompareExchange(const_cast<volatile LONG*>(&s->colorG), 0, 0));
    p.b       = static_cast<BYTE>(InterlockedCompareExchange(const_cast<volatile LONG*>(&s->colorB), 0, 0));
    return p;
}

// Must be called on the XAML UI thread (OnVisualTreeChange or RunAsync callback)
void ApplyBrushParams(const wuxs::Shape& shape, const BrushParams& p)
{
    if (!p.enabled) {
        shape.Fill(wuxm::SolidColorBrush{ wu::Colors::Transparent() });
        return;
    }
    wu::Color color{ p.alpha, p.r, p.g, p.b };
    if (p.useBlur) {
        try {
            wuxm::AcrylicBrush acrylic{};
            acrylic.BackgroundSource(wuxm::AcrylicBackgroundSource::HostBackdrop);
            acrylic.TintColor(color);
            acrylic.TintOpacity(static_cast<double>(p.alpha) / 255.0);
            shape.Fill(acrylic);
            return;
        }
        catch (...) {}
    }
    wuxm::SolidColorBrush brush{};
    brush.Color(color);
    shape.Fill(brush);
}

// ---------------------------------------------------------------------------
// RegisterAndApplyShape
// handle == 0 for VTH-walked shapes (no diagnostic handle available);
// dedup by handle (if != 0) or by raw COM pointer (if handle == 0).
// ---------------------------------------------------------------------------
static void RegisterAndApplyShape(InstanceHandle handle, const wuxs::Shape& shape)
{
    {
        std::lock_guard<std::mutex> lk(g_shapesMtx);
        bool found = false;
        if (handle != 0) {
            found = std::any_of(g_knownShapes.begin(), g_knownShapes.end(),
                [handle](const ShapeEntry& e) { return e.handle == handle; });
        } else {
            // VTH-walked shape: compare by COM identity (C++/WinRT operator==)
            found = std::any_of(g_knownShapes.begin(), g_knownShapes.end(),
                [&shape](const ShapeEntry& e) { return e.shape == shape; });
        }
        if (!found)
            g_knownShapes.emplace_back(handle, shape);
    }
    BrushParams p = ReadBrushParams(g_pState);
    ApplyBrushParams(shape, p);
}

// ---------------------------------------------------------------------------
// VisualTreeWatcher::OnVisualTreeChange
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE VisualTreeWatcher::OnVisualTreeChange(
    ParentChildRelation  relation,
    VisualElement        element,
    VisualMutationType   mutationType) noexcept
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

        // --- Full diagnostic dump (parent handle + type + name) -----------------
        int idx = g_loggedElements.fetch_add(1);
        if (idx < kMaxLoggedElements) {
            XBLogFmt(L"  [%d] parent=0x%llX  Type=%-60s  Name=%s",
                idx,
                static_cast<unsigned long long>(relation.Parent),
                element.Type ? element.Type : L"(null)",
                element.Name ? element.Name : L"(null)");
        }

        // Detect Taskbar.TaskbarBackground — store handle + will VTH-walk below
        bool isTaskbarBg = (element.Type &&
            wcscmp(element.Type, L"Taskbar.TaskbarBackground") == 0);
        if (isTaskbarBg) {
            g_taskbarBgHandle.store(element.Handle);
            XBLogFmt(L"  *** TaskbarBackground handle=0x%llX stored — VTH walk pending ***",
                static_cast<unsigned long long>(element.Handle));
        }

        // --- Path 1: direct child of TaskbarBackground — try ALL Shapes ----------
        InstanceHandle bgHandle = g_taskbarBgHandle.load();
        bool isChildOfBg = (bgHandle != 0 && relation.Parent == bgHandle);

        if (isChildOfBg) {
            XBLogFmt(L"  *** TaskbarBackground CHILD: Type=%s  Name=%s ***",
                element.Type ? element.Type : L"(null)",
                element.Name ? element.Name : L"(null)");
        }

        // --- Path 2: any Rectangle anywhere in the tree -------------------------
        bool isRectangle = element.Type && wcsstr(element.Type, L"Rectangle") != nullptr;

        // isTaskbarBg is now included so we proceed to resolve IInspectable for it
        if (!isChildOfBg && !isRectangle && !isTaskbarBg)
            return S_OK;

        // Resolve handle → IInspectable
        winrt::Windows::Foundation::IInspectable obj;
        HRESULT hr = m_diagnostics->GetIInspectableFromHandle(
            element.Handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj)));
        if (FAILED(hr) || !obj) {
            if (isChildOfBg || isTaskbarBg)
                XBLogFmt(L"  *** GetIInspectableFromHandle FAILED hr=0x%08X ***", hr);
            return S_OK;
        }

        auto fe = obj.try_as<wux::FrameworkElement>();
        if (!fe) return S_OK;

        // --- VTH walk: TaskbarBackground → Grid(s) → Rectangle#BackgroundFill ---
        if (isTaskbarBg) {
            try {
                int bgChildCount = wuxm::VisualTreeHelper::GetChildrenCount(fe);
                XBLogFmt(L"  *** TaskbarBg VTH child count: %d ***", bgChildCount);
                for (int ci = 0; ci < bgChildCount; ++ci) {
                    auto intermediate = wuxm::VisualTreeHelper::GetChild(fe, ci);
                    if (!intermediate) continue;
                    auto intermFE = intermediate.try_as<wux::FrameworkElement>();
                    if (!intermFE) continue;
                    int grandCount = wuxm::VisualTreeHelper::GetChildrenCount(intermFE);
                    XBLogFmt(L"  TaskbarBg VTH child[%d] Name='%s' grandchildren=%d",
                        ci, intermFE.Name().c_str(), grandCount);
                    for (int gi = 0; gi < grandCount; ++gi) {
                        auto grand = wuxm::VisualTreeHelper::GetChild(intermFE, gi);
                        if (!grand) continue;
                        auto grandFE = grand.try_as<wux::FrameworkElement>();
                        if (!grandFE) continue;
                        auto grandName = grandFE.Name();
                        XBLogFmt(L"    TaskbarBg VTH grandchild[%d][%d] Name='%s'",
                            ci, gi, grandName.c_str());
                        if (grandName != L"BackgroundFill" && grandName != L"BackgroundStroke")
                            continue;
                        auto shape = grandFE.try_as<wuxs::Shape>();
                        if (!shape) continue;
                        XBLogFmt(L"  >>> VTH walk: found %s — registering + applying brush",
                            grandName.c_str());
                        RegisterAndApplyShape(0, shape);
                    }
                }
            }
            catch (...) {}
            return S_OK;
        }

        auto runtimeName = fe.Name();

        if (isRectangle) {
            bool isBackground = (runtimeName == L"BackgroundFill");
            bool isStroke     = (runtimeName == L"BackgroundStroke");
            XBLogFmt(L"  Rectangle: Name=%s  isBackground=%d  isStroke=%d  childOfBg=%d",
                runtimeName.c_str(), isBackground ? 1 : 0, isStroke ? 1 : 0, isChildOfBg ? 1 : 0);
            if (!isBackground && !isStroke && !isChildOfBg)
                return S_OK;
        }

        auto shape = fe.try_as<wuxs::Shape>();
        if (!shape) {
            if (isChildOfBg)
                XBLogFmt(L"  *** TaskbarBackground child is not a Shape (no cast) ***");
            return S_OK;
        }

        XBLogFmt(L"  >>> Applying brush to Name=%s", runtimeName.c_str());
        RegisterAndApplyShape(element.Handle, shape);
    }
    catch (...) {}
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VisualTreeWatcher::OnElementStateChanged(
    InstanceHandle      /*element*/,
    VisualElementState  /*elementState*/,
    LPCWSTR             /*context*/) noexcept
{
    return S_OK;
}
