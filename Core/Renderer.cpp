#include "Renderer.h"
#include "Diagnostics.h"
#include <cmath>

namespace CrystalFrame {

Renderer::Renderer() {
}

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(HWND hwndTaskbar, HWND hwndStart) {
    // Store host window for deferred commit messages
    m_hwndHost = hwndTaskbar;
    
    // Create DirectComposition device
    HRESULT hr = DCompositionCreateDevice(
        nullptr,
        __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(m_dcompDevice.GetAddressOf())
    );
    
    if (FAILED(hr)) {
        CF_LOG(Error, "DCompositionCreateDevice failed: 0x" << std::hex << hr);
        return false;
    }
    
    CF_LOG(Info, "DirectComposition device created");
    
    // Create composition target for Taskbar overlay
    hr = m_dcompDevice->CreateTargetForHwnd(hwndTaskbar, TRUE, m_targetTaskbar.GetAddressOf());
    if (FAILED(hr)) {
        CF_LOG(Error, "CreateTargetForHwnd (Taskbar) failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Create composition target for Start overlay
    hr = m_dcompDevice->CreateTargetForHwnd(hwndStart, TRUE, m_targetStart.GetAddressOf());
    if (FAILED(hr)) {
        CF_LOG(Error, "CreateTargetForHwnd (Start) failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Create root visuals
    hr = m_dcompDevice->CreateVisual(m_visualTaskbar.GetAddressOf());
    if (FAILED(hr)) {
        CF_LOG(Error, "CreateVisual (Taskbar) failed: 0x" << std::hex << hr);
        return false;
    }
    
    hr = m_dcompDevice->CreateVisual(m_visualStart.GetAddressOf());
    if (FAILED(hr)) {
        CF_LOG(Error, "CreateVisual (Start) failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Set root visuals to targets
    m_targetTaskbar->SetRoot(m_visualTaskbar.Get());
    m_targetStart->SetRoot(m_visualStart.Get());
    
    // Set initial opacity
    m_visualTaskbar->SetOpacity(m_taskbarOpacity);
    m_visualStart->SetOpacity(m_startOpacity);
    
    // Create simple overlay surfaces (semi-transparent black)
    // These will just add a tint over the taskbar/start menu
    RECT taskbarRect;
    GetClientRect(hwndTaskbar, &taskbarRect);
    CreateSolidColorSurface(m_visualTaskbar.Get(), 
                           D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
                           taskbarRect.right - taskbarRect.left,
                           taskbarRect.bottom - taskbarRect.top);
    
    RECT startRect;
    GetClientRect(hwndStart, &startRect);
    CreateSolidColorSurface(m_visualStart.Get(),
                           D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
                           startRect.right - startRect.left,
                           startRect.bottom - startRect.top);
    
    // Initial commit
    CommitNow();
    
    CF_LOG(Info, "Renderer initialized successfully");
    
    return true;
}

void Renderer::Shutdown() {
    // Release COM objects in reverse order
    m_visualStart.Reset();
    m_visualTaskbar.Reset();
    m_targetStart.Reset();
    m_targetTaskbar.Reset();
    m_dcompDevice.Reset();
    
    CF_LOG(Info, "Renderer shutdown");
}

void Renderer::SetTaskbarOpacity(float opacity) {
    // Clamp to valid range
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    
    // Check if actually changed (avoid unnecessary updates)
    if (std::abs(m_taskbarOpacity - opacity) < 0.01f) {
        return;
    }
    
    m_taskbarOpacity = opacity;
    
    if (m_visualTaskbar && m_taskbarEnabled) {
        HRESULT hr = m_visualTaskbar->SetOpacity(opacity);
        if (FAILED(hr)) {
            CF_LOG(Error, "SetOpacity (Taskbar) failed: 0x" << std::hex << hr);
            return;
        }
        
        ScheduleCommit();
        CF_LOG(Debug, "Taskbar opacity set to " << opacity);
    }
}

void Renderer::SetStartOpacity(float opacity) {
    // Clamp to valid range
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    
    // Check if actually changed
    if (std::abs(m_startOpacity - opacity) < 0.01f) {
        return;
    }
    
    m_startOpacity = opacity;
    
    if (m_visualStart && m_startEnabled) {
        HRESULT hr = m_visualStart->SetOpacity(opacity);
        if (FAILED(hr)) {
            CF_LOG(Error, "SetOpacity (Start) failed: 0x" << std::hex << hr);
            return;
        }
        
        ScheduleCommit();
        CF_LOG(Debug, "Start opacity set to " << opacity);
    }
}

void Renderer::SetTaskbarEnabled(bool enabled) {
    m_taskbarEnabled = enabled;
    
    if (m_visualTaskbar) {
        float opacity = enabled ? m_taskbarOpacity : 0.0f;
        m_visualTaskbar->SetOpacity(opacity);
        ScheduleCommit();
        CF_LOG(Info, "Taskbar overlay " << (enabled ? "enabled" : "disabled"));
    }
}

void Renderer::SetStartEnabled(bool enabled) {
    m_startEnabled = enabled;
    
    if (m_visualStart) {
        float opacity = enabled ? m_startOpacity : 0.0f;
        m_visualStart->SetOpacity(opacity);
        ScheduleCommit();
        CF_LOG(Info, "Start overlay " << (enabled ? "enabled" : "disabled"));
    }
}

void Renderer::ScheduleCommit() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCommit);
    
    // Throttle commits to ~60 FPS (16ms)
    if (elapsed.count() >= 16) {
        CommitNow();
    } else if (!m_pendingCommit.exchange(true)) {
        // Post a deferred commit to the message loop — processed on the next iteration
        if (m_hwndHost) {
            PostMessage(m_hwndHost, WM_DCOMP_COMMIT, 0, 0);
        }
    }
}

void Renderer::OnDeferredCommit() {
    if (m_pendingCommit) {
        CommitNow();
    }
}

void Renderer::CommitNow() {
    if (!m_dcompDevice) {
        return;
    }
    
    HRESULT hr = m_dcompDevice->Commit();
    if (FAILED(hr)) {
        CF_LOG(Error, "Commit failed: 0x" << std::hex << hr);
    }
    
    m_lastCommit = std::chrono::steady_clock::now();
    m_pendingCommit = false;
}

bool Renderer::CreateSolidColorSurface(IDCompositionVisual* visual, D2D1_COLOR_F color, int width, int height) {
    if (!visual || width <= 0 || height <= 0) {
        return false;
    }
    
    // Create D2D factory
    ComPtr<ID2D1Factory> d2dFactory;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
    if (FAILED(hr)) {
        CF_LOG(Error, "D2D1CreateFactory failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Create DirectComposition surface
    ComPtr<IDCompositionSurface> surface;
    hr = m_dcompDevice->CreateSurface(
        width, height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED,
        surface.GetAddressOf()
    );
    
    if (FAILED(hr)) {
        CF_LOG(Error, "CreateSurface failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Begin draw on surface
    ComPtr<ID2D1DeviceContext> dc;
    POINT offset = {};
    hr = surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext),
                           reinterpret_cast<void**>(dc.GetAddressOf()), &offset);
    
    if (FAILED(hr)) {
        CF_LOG(Error, "BeginDraw failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Clear with color
    dc->Clear(color);
    
    // End draw
    hr = surface->EndDraw();
    if (FAILED(hr)) {
        CF_LOG(Error, "EndDraw failed: 0x" << std::hex << hr);
        return false;
    }
    
    // Set content to visual
    hr = visual->SetContent(surface.Get());
    if (FAILED(hr)) {
        CF_LOG(Error, "SetContent failed: 0x" << std::hex << hr);
        return false;
    }
    
    return true;
}

} // namespace CrystalFrame
