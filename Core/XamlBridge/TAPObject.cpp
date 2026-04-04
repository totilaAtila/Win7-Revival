//
// TAPObject.cpp — AdviseThread + GlassBarTAP::SetSite implementation  [ITER #11]
//
// NOTE: __try cannot be in a function that has C++ objects requiring unwinding.
// Solution: split into AdviseThreadBody (C++ objects, no __try) called from
//           AdviseThreadProc (SEH wrapper, no C++ objects in its frame).
//
#include "TAPObject.h"

// ---------------------------------------------------------------------------
// AdviseThreadBody — C++ objects live here; no __try allowed
// ---------------------------------------------------------------------------
static void AdviseThreadBody(AdviseArgs* a)
{
    // No CoInitializeEx here — XAML Diagnostics callbacks arrive on the XAML UI thread,
    // not on this thread. COINIT_MULTITHREADED was previously here but could interfere
    // with STA marshaling. Follow Windhawk approach: no explicit COM init on advise thread.

    XBLog(L"AdviseThread: creating watcher...");
    auto watcher = winrt::make_self<VisualTreeWatcher>();
    watcher->m_diagnostics = a->diag;

    // Keep global ref so watcher lives beyond this thread frame
    g_visualTreeWatcher = watcher;

    // Prefer IVisualTreeService3 (same QI target as Windhawk) — may give more complete replay
    HRESULT hr = E_NOINTERFACE;
    if (auto svc3 = a->diag.try_as<IVisualTreeService3>()) {
        XBLog(L"AdviseThread: AdviseVisualTreeChange via IVisualTreeService3...");
        hr = svc3->AdviseVisualTreeChange(watcher.as<IVisualTreeServiceCallback>().get());
        XBLogFmt(L"AdviseVisualTreeChange (svc3): hr=0x%08X", hr);
    }
    if (FAILED(hr)) {
        XBLog(L"AdviseThread: AdviseVisualTreeChange via IVisualTreeService (fallback)...");
        hr = a->svc->AdviseVisualTreeChange(watcher.as<IVisualTreeServiceCallback>().get());
        XBLogFmt(L"AdviseVisualTreeChange (svc): hr=0x%08X", hr);
    }

    // Note: AdviseVisualTreeChange does not return during normal operation —
    // it stays registered for live mutations.  The tree walk is triggered from
    // OnVisualTreeChange (via a spawned thread) when TaskbarBackground is seen.

    delete a;
}

// ---------------------------------------------------------------------------
// AdviseThreadProc — SEH wrapper; no C++ objects with destructors in this frame
// ---------------------------------------------------------------------------
static DWORD WINAPI AdviseThreadProc(LPVOID p)
{
    __try {
        AdviseThreadBody(static_cast<AdviseArgs*>(p));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        XBLogFmt(L"AdviseThread: SEH exception 0x%08X", GetExceptionCode());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// GlassBarTAP::SetSite
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE GlassBarTAP::SetSite(IUnknown* pSite) noexcept
{
    m_svc  = nullptr;
    m_diag = nullptr;
    if (!pSite) { XBLog(L"SetSite(nullptr)"); return S_OK; }

    HRESULT hrSvc  = pSite->QueryInterface(IID_PPV_ARGS(m_svc.put()));
    HRESULT hrDiag = pSite->QueryInterface(IID_PPV_ARGS(m_diag.put()));
    XBLogFmt(L"SetSite: QI IVisualTreeService hr=0x%08X  IXamlDiagnostics hr=0x%08X",
        hrSvc, hrDiag);

    if (!m_svc || !m_diag) return E_NOINTERFACE;

    // Balance the implicit LoadLibrary that InitializeXamlDiagnosticsEx performs on our DLL
    FreeLibrary(g_hModule);

    auto* args = new AdviseArgs{ m_svc, m_diag };

    HANDLE h = CreateThread(nullptr, 0, AdviseThreadProc, args, 0, nullptr);
    if (h) { XBLog(L"SetSite: advise thread created"); CloseHandle(h); }
    else   { XBLogFmt(L"SetSite: CreateThread FAILED e=%u", GetLastError()); delete args; }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE GlassBarTAP::GetSite(REFIID riid, void** ppv) noexcept
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    return m_svc ? m_svc->QueryInterface(riid, ppv) : E_FAIL;
}
