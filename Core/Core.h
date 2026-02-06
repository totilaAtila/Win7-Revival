#pragma once
#include <Windows.h>
#include <memory>
#include "ConfigManager.h"
#include "ShellTargetLocator.h"
#include "OverlayHost.h"
#include "Renderer.h"
#include "IpcBridge.h"

namespace CrystalFrame {

class CrystalFrameCore : public IShellTargetCallback, public IIpcCallback {
public:
    explicit CrystalFrameCore(HINSTANCE hInstance);
    ~CrystalFrameCore();
    
    bool Initialize();
    void Run();  // Message loop
    void Shutdown();
    
    // IShellTargetCallback interface
    void OnTaskbarChanged(const TaskbarInfo& info) override;
    void OnStartShown(const StartInfo& info) override;
    void OnStartHidden() override;
    void OnStartDetectionFailed() override;
    
    // IIpcCallback interface
    void OnSetTaskbarOpacity(int opacity) override;
    void OnSetStartOpacity(int opacity) override;
    void OnSetTaskbarEnabled(bool enabled) override;
    void OnSetStartEnabled(bool enabled) override;
    void OnGetStatus() override;
    void OnShutdown() override;
    
private:
    HINSTANCE m_hInstance;
    bool m_running = false;
    
    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<ShellTargetLocator> m_locator;
    std::unique_ptr<OverlayHost> m_overlayHost;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<IpcBridge> m_ipc;
    
    StatusData GetCurrentStatus();
};

} // namespace CrystalFrame
