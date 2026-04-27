#pragma once
#include "core/SessionManager.h"
#include "core/ProxyServer.h"
#include "core/TunnelManager.h"
#include "app/IpcController.h"
#include "gui/MainWindow.h"
#include "util/Logger.h"
#include <memory>

class Application {
public:
    Application();
    ~Application();

    int run(int argc, char** argv);

private:
    std::unique_ptr<MainWindow> window_;
    SessionManager sessionMgr_;
    ProxyServer proxyServer_;
    TunnelManager tunnelMgr_;
    IpcController ipc_;

    void setupCallbacks();
    void registerTunnelProviders();
    void registerIpcCommands();
    void registerClaudeFds();
    void unregisterClaudeFds();

    // FLTK timer/idle callbacks
    static void refreshTimerCb(void* data);
    static void tunnelPollCb(void* data);
    static void wsServiceCb(void* data);

    // Tracked fds for cleanup
    std::vector<int> registeredFds_;
};
