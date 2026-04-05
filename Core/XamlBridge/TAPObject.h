//
// TAPObject.h — COM TAP object: CLSID, AdviseArgs, GlassBarTAP, SimpleFactory
//               [ITER #11]
//
#pragma once
#include "XamlBridgeCommon.h"
#include "VisualTreeWatcher.h"

// ---------------------------------------------------------------------------
// CLSID for GlassBar TAP  —  {E5A89B6C-3F2D-4A8E-B91F-7A56D3E4C80F}
// ---------------------------------------------------------------------------
static const CLSID CLSID_GlassBarTAP = {
    0xe5a89b6c, 0x3f2d, 0x4a8e,
    { 0xb9, 0x1f, 0x7a, 0x56, 0xd3, 0xe4, 0xc8, 0x0f }
};

// ---------------------------------------------------------------------------
// AdviseArgs — passed to the AdviseThread to avoid lifetime issues with
//              GlassBarTAP members (which may be released after SetSite returns)
// ---------------------------------------------------------------------------
struct AdviseArgs {
    winrt::com_ptr<IVisualTreeService> svc;
    winrt::com_ptr<IXamlDiagnostics>   diag;
};

// ---------------------------------------------------------------------------
// SimpleFactory<T> — minimal IClassFactory
// ---------------------------------------------------------------------------
template <typename T>
struct SimpleFactory : winrt::implements<SimpleFactory<T>, IClassFactory>
{
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown* outer, REFIID riid, void** ppv) noexcept override
    {
        if (outer) { *ppv = nullptr; return CLASS_E_NOAGGREGATION; }
        return winrt::make<T>()->QueryInterface(riid, ppv);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};

// ---------------------------------------------------------------------------
// Global watcher reference — keeps VisualTreeWatcher alive after AdviseThread exits
// (defined in dllmain.cpp, set in AdviseThreadBody)
// ---------------------------------------------------------------------------
extern winrt::com_ptr<VisualTreeWatcher> g_visualTreeWatcher;

// ---------------------------------------------------------------------------
// GlassBarTAP : IObjectWithSite
// XAML runtime creates one instance per connected island and calls SetSite
// with the diagnostic service.  SetSite spawns the AdviseThread.
// ---------------------------------------------------------------------------
struct GlassBarTAP : winrt::implements<GlassBarTAP, IObjectWithSite>
{
    winrt::com_ptr<IVisualTreeService> m_svc;
    winrt::com_ptr<IXamlDiagnostics>  m_diag;

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pSite) noexcept override;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppv) noexcept override;
};
