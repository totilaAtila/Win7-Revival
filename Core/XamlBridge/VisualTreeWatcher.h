//
// VisualTreeWatcher.h — IVisualTreeServiceCallback2 implementation  [ITER #11]
//
// Uses <xamlom.h> (SDK 10.0.26100.0) for correct vtable layout.
// Do NOT use custom interface definitions — they produce wrong vtable offsets.
//
#pragma once
#include "XamlBridgeCommon.h"

struct VisualTreeWatcher : winrt::implements<VisualTreeWatcher,
    IVisualTreeServiceCallback2, IVisualTreeServiceCallback, winrt::non_agile>
{
    winrt::com_ptr<IXamlDiagnostics> m_diagnostics;

    // Post-replay walk: called from AdviseThread after AdviseVisualTreeChange returns.
    // Tree is fully connected at that point — no dispatcher needed.
    void WalkTaskbarBgPostReplay() noexcept;

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation  relation,
        VisualElement        element,
        VisualMutationType   mutationType) noexcept override;

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle      element,
        VisualElementState  elementState,
        LPCWSTR             context) noexcept override;
};
