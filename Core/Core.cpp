#include "Core.h"
#include "Diagnostics.h"

namespace CrystalFrame {

CrystalFrameCore::CrystalFrameCore(HINSTANCE hInstance)
    : m_hInstance(hInstance) {
}

CrystalFrameCore::~CrystalFrameCore() {
    Shutdown();
}

bool CrystalFrameCore::Initialize() {
    CF_LOG(Info, "=== CrystalFrame Core Initialization ===");
    
    // Create modules
    m_config = std::make_unique<ConfigManager>();
    m_locator = std::make_unique<ShellTargetLocator>();
    m_overlayHost = std::make_unique<OverlayHost>();
    m_renderer = std::make_unique<Renderer>();
    m_ipc = std::make_unique<IpcBridge>();
    
    // Initialize config
    if (!m_config->Initialize()) {
        CF_LOG(Error, "ConfigManager initialization failed");
        return false;
    }
    
    Config config = m_config->GetConfig();
    CF_LOG(Info, "Config loaded: Taskbar=" << config.taskbarOpacity 
                 << "%, Start=" << config.startOpacity << "%");
    
    // Initialize overlay host
    if (!m_overlayHost->Initialize(m_hInstance)) {
        CF_LOG(Error, "OverlayHost initialization failed");
        return false;
    }
    
    // Initialize renderer
    if (!m_renderer->Initialize(
            m_overlayHost->GetTaskbarOverlayWindow(),
            m_overlayHost->GetStartOverlayWindow())) {
        CF_LOG(Error, "Renderer initialization failed");
        return false;
    }
    
    // Apply config to renderer
    m_renderer->SetTaskbarOpacity(config.taskbarOpacity / 100.0f);
    m_renderer->SetStartOpacity(config.startOpacity / 100.0f);
    m_renderer->SetTaskbarEnabled(config.taskbarEnabled);
    m_renderer->SetStartEnabled(config.startEnabled);
    
    // Initialize shell target locator
    if (!m_locator->Initialize(this)) {
        CF_LOG(Error, "ShellTargetLocator initialization failed");
        return false;
    }
    
    // Initialize IPC
    if (!m_ipc->Initialize(this)) {
        CF_LOG(Error, "IpcBridge initialization failed");
        return false;
    }
    
    // Send initial status
    m_ipc->SendStatusUpdate(GetCurrentStatus());
    
    CF_LOG(Info, "=== CrystalFrame Core Ready ===");
    
    return true;
}

void CrystalFrameCore::Run() {
    m_running = true;
    
    CF_LOG(Info, "Entering message loop");
    
    MSG msg = {};
    while (m_running && GetMessage(&msg, nullptr, 0, 0)) {
        // Handle deferred DirectComposition commits inline
        if (msg.message == WM_DCOMP_COMMIT && m_renderer) {
            m_renderer->OnDeferredCommit();
            continue;
        }
        
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    CF_LOG(Info, "Exited message loop");
}

void CrystalFrameCore::Shutdown() {
    CF_LOG(Info, "=== CrystalFrame Core Shutdown ===");
    
    m_running = false;
    
    // Shutdown in reverse order
    m_ipc.reset();
    m_renderer.reset();
    m_locator.reset();
    m_overlayHost.reset();
    m_config.reset();
}

// ========== IShellTargetCallback Implementation ==========

void CrystalFrameCore::OnTaskbarChanged(const TaskbarInfo& info) {
    CF_LOG(Info, "Taskbar changed - updating overlay");
    
    if (m_overlayHost) {
        m_overlayHost->UpdateTaskbarOverlay(info);
    }
    
    // Send status update to Dashboard
    if (m_ipc) {
        m_ipc->SendStatusUpdate(GetCurrentStatus());
    }
}

void CrystalFrameCore::OnStartShown(const StartInfo& info) {
    CF_LOG(Info, "Start menu shown");
    
    if (m_overlayHost) {
        m_overlayHost->ShowStartOverlay(info);
    }
    
    if (m_ipc) {
        m_ipc->SendStatusUpdate(GetCurrentStatus());
    }
}

void CrystalFrameCore::OnStartHidden() {
    CF_LOG(Info, "Start menu hidden");
    
    if (m_overlayHost) {
        m_overlayHost->HideStartOverlay();
    }
    
    if (m_ipc) {
        m_ipc->SendStatusUpdate(GetCurrentStatus());
    }
}

void CrystalFrameCore::OnStartDetectionFailed() {
    CF_LOG(Warning, "Start menu detection failed - disabling Start overlay");
    
    if (m_renderer) {
        m_renderer->SetStartEnabled(false);
    }
    
    if (m_overlayHost) {
        m_overlayHost->HideStartOverlay();
    }
    
    if (m_ipc) {
        m_ipc->SendError("Start menu detection unreliable", "START_DETECTION_FAILED");
        m_ipc->SendStatusUpdate(GetCurrentStatus());
    }
}

// ========== IIpcCallback Implementation ==========

void CrystalFrameCore::OnSetTaskbarOpacity(int opacity) {
    CF_LOG(Info, "IPC: SetTaskbarOpacity(" << opacity << ")");
    
    if (m_renderer) {
        m_renderer->SetTaskbarOpacity(opacity / 100.0f);
    }
    
    if (m_config) {
        m_config->SetTaskbarOpacity(opacity);
        m_config->Save();
    }
}

void CrystalFrameCore::OnSetStartOpacity(int opacity) {
    CF_LOG(Info, "IPC: SetStartOpacity(" << opacity << ")");
    
    if (m_renderer) {
        m_renderer->SetStartOpacity(opacity / 100.0f);
    }
    
    if (m_config) {
        m_config->SetStartOpacity(opacity);
        m_config->Save();
    }
}

void CrystalFrameCore::OnSetTaskbarEnabled(bool enabled) {
    CF_LOG(Info, "IPC: SetTaskbarEnabled(" << enabled << ")");
    
    if (m_renderer) {
        m_renderer->SetTaskbarEnabled(enabled);
    }
    
    if (m_config) {
        m_config->SetTaskbarEnabled(enabled);
        m_config->Save();
    }
}

void CrystalFrameCore::OnSetStartEnabled(bool enabled) {
    CF_LOG(Info, "IPC: SetStartEnabled(" << enabled << ")");
    
    if (m_renderer) {
        m_renderer->SetStartEnabled(enabled);
    }
    
    if (m_config) {
        m_config->SetStartEnabled(enabled);
        m_config->Save();
    }
}

void CrystalFrameCore::OnGetStatus() {
    CF_LOG(Debug, "IPC: GetStatus");
    
    if (m_ipc) {
        m_ipc->SendStatusUpdate(GetCurrentStatus());
    }
}

void CrystalFrameCore::OnShutdown() {
    CF_LOG(Info, "IPC: Shutdown requested by Dashboard");
    
    // Send acknowledgment before shutting down
    if (m_ipc) {
        m_ipc->SendStatusUpdate(GetCurrentStatus());
    }
    
    // Post quit message to exit the message loop gracefully
    m_running = false;
    PostQuitMessage(0);
}

// ========== Helper Methods ==========

StatusData CrystalFrameCore::GetCurrentStatus() {
    StatusData status = {};
    
    if (m_locator) {
        TaskbarInfo taskbarInfo = m_locator->GetTaskbarInfo();
        status.taskbar.found = taskbarInfo.found;
        
        // Proper wide-to-narrow string conversion
        const wchar_t* edgeStr = EdgeToString(taskbarInfo.edge);
        int len = WideCharToMultiByte(CP_UTF8, 0, edgeStr, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            status.taskbar.edge.resize(len - 1);  // exclude null terminator
            WideCharToMultiByte(CP_UTF8, 0, edgeStr, -1, &status.taskbar.edge[0], len, nullptr, nullptr);
        }
        
        status.taskbar.autoHide = taskbarInfo.autoHide;
        
        StartInfo startInfo = m_locator->GetStartInfo();
        status.start.detected = startInfo.detected;
        status.start.isOpen = startInfo.isOpen;
        status.start.confidence = startInfo.confidence;
    }
    
    if (m_config) {
        Config config = m_config->GetConfig();
        status.taskbar.enabled = config.taskbarEnabled;
        status.taskbar.opacity = config.taskbarOpacity;
        status.start.enabled = config.startEnabled;
        status.start.opacity = config.startOpacity;
    }
    
    return status;
}

} // namespace CrystalFrame
