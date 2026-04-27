#include "app/Application.h"
#include "core/NgrokProvider.h"
#include "core/DevTunnelProvider.h"
#include "core/CloudflareProvider.h"
#include "gui/TunnelDetailsDialog.h"
#include <FL/Fl.H>
#include <FL/fl_ask.H>

#ifdef __APPLE__
extern "C" void macOS_setup();
extern "C" void macOS_set_callbacks(
    void (*onStartServer)(),
    void (*onStopServer)(),
    bool (*isServerRunning)(),
    void (*onShowWindow)());

static Application* g_app = nullptr;
#endif

Application::Application() = default;
Application::~Application() = default;

int Application::run(int argc, char** argv) {
    // Single instance check
    if (!ipc_.tryListen()) {
        LOG_ERROR("Another instance is already running. Sending 'show' command.");
        IpcController::sendCommand("show");
        return 1;
    }

    window_ = std::make_unique<MainWindow>(900, 650, "Claude Shell");
    window_->init(&sessionMgr_, &proxyServer_, &tunnelMgr_);

    window_->proxyPanel()->setFrontendPath(SOURCE_DIR "/frontend");

    registerTunnelProviders();
    registerIpcCommands();

    Logger::instance().setCallback([this](LogLevel level, const std::string& msg) {
        if (window_ && window_->logPanel()) {
            window_->logPanel()->appendLog(level, msg);
        }
    });

    setupCallbacks();

    // Register IPC socket with FLTK event loop
    Fl::add_fd(ipc_.listenFd(), FL_READ,
               [](int, void* data) {
                   auto* app = (Application*)data;
                   app->ipc_.service();
               }, this);

    sessionMgr_.refresh();
    window_->sessionPanel()->updateSessions(sessionMgr_.sessions());
    window_->updateStatusBar();

    Fl::add_timeout(3.0, refreshTimerCb, this);

    // Window close hides instead of quitting (app stays in menu bar)
    window_->callback([](Fl_Widget* w, void*) {
        w->hide();
    });

    window_->show(argc, argv);

#ifdef __APPLE__
    g_app = this;
    macOS_set_callbacks(
        []() { // onStartServer
            if (!g_app) return;
            if (g_app->proxyServer_.state() != ProxyServer::State::Running) {
                int port = g_app->window_->tunnelPanel()->getPort();
                g_app->proxyServer_.setClaudeBinary(g_app->window_->proxyPanel()->claudeBinaryPath());
                if (g_app->proxyServer_.start(port)) {
                    g_app->proxyServer_.updateSessionList(g_app->sessionMgr_.sessions());
                    g_app->registerClaudeFds();
                    g_app->window_->tunnelPanel()->setServerPort(g_app->proxyServer_.port());
                    g_app->window_->proxyPanel()->updateStatus();
                    g_app->window_->updateStatusBar();
                }
            }
        },
        []() { // onStopServer
            if (!g_app) return;
            g_app->unregisterClaudeFds();
            g_app->proxyServer_.stop();
            g_app->window_->tunnelPanel()->setServerPort(0);
            g_app->window_->proxyPanel()->updateStatus();
            g_app->window_->updateStatusBar();
        },
        []() -> bool { // isServerRunning
            return g_app && g_app->proxyServer_.state() == ProxyServer::State::Running;
        },
        []() { // onShowWindow
            if (g_app && g_app->window_) {
                g_app->window_->show();
            }
        }
    );
    macOS_setup();
#endif

    LOG_INFO("Claude Shell started");

    // Use Fl::wait() loop instead of Fl::run() so the app keeps running
    // even when the window is hidden (menu bar icon stays active)
    while (true) {
        if (Fl::wait(1.0) < 0) break;
    }
    return 0;
}

void Application::registerIpcCommands() {
    ipc_.registerCommand("status", [this](const std::string&) -> std::string {
        std::string result;
        if (proxyServer_.state() == ProxyServer::State::Running) {
            result = "Server: running on port " + std::to_string(proxyServer_.port()) +
                     "\nClients: " + std::to_string(proxyServer_.clientCount()) +
                     "\nURL: " + proxyServer_.connectionString();
        } else {
            result = "Server: stopped";
        }
        result += "\nSessions: " + std::to_string(sessionMgr_.sessions().size());
        auto tunnels = tunnelMgr_.activeTunnels();
        if (!tunnels.empty()) {
            result += "\nTunnels:";
            for (auto* t : tunnels) {
                result += "\n  " + t->providerName + " -> " +
                          (t->publicUrl.empty() ? "(starting...)" : t->publicUrl);
            }
        }
        return result;
    });

    ipc_.registerCommand("sessions", [this](const std::string&) -> std::string {
        sessionMgr_.refresh();
        auto& sessions = sessionMgr_.sessions();
        if (sessions.empty()) return "No Claude sessions found.";
        std::string result;
        for (auto& s : sessions) {
            result += (s.alive ? "\033[32m●\033[0m " : "\033[31m●\033[0m ");
            result += s.sessionId.substr(0, 8);
            if (!s.name.empty()) result += " (" + s.name + ")";
            result += "  PID " + std::to_string(s.pid);
            result += "  " + s.cwd + "\n";
        }
        return result;
    });

    ipc_.registerCommand("start", [this](const std::string& args) -> std::string {
        if (proxyServer_.state() == ProxyServer::State::Running)
            return "Server already running on port " + std::to_string(proxyServer_.port());
        int port = args.empty() ? window_->tunnelPanel()->getPort() : std::atoi(args.c_str());
        if (proxyServer_.start(port)) {
            proxyServer_.updateSessionList(sessionMgr_.sessions());
            registerClaudeFds();
            window_->tunnelPanel()->setServerPort(proxyServer_.port());
            window_->proxyPanel()->updateStatus();
            window_->updateStatusBar();
            return "Server started on port " + std::to_string(proxyServer_.port()) +
                   "\nURL: " + proxyServer_.connectionString();
        }
        return "error: failed to start server";
    });

    ipc_.registerCommand("stop", [this](const std::string&) -> std::string {
        if (proxyServer_.state() != ProxyServer::State::Running)
            return "Server is not running.";
        unregisterClaudeFds();
        proxyServer_.stop();
        window_->tunnelPanel()->setServerPort(0);
        window_->proxyPanel()->updateStatus();
        window_->updateStatusBar();
        return "Server stopped.";
    });

    ipc_.registerCommand("url", [this](const std::string&) -> std::string {
        if (proxyServer_.state() != ProxyServer::State::Running)
            return "error: server is not running";
        return proxyServer_.connectionString();
    });

    ipc_.registerCommand("tunnel", [this](const std::string& args) -> std::string {
        std::string provider = args.empty() ? "" : args;
        if (provider.empty()) {
            auto avail = tunnelMgr_.availableProviders();
            if (avail.empty()) return "error: no tunnel providers installed";
            provider = avail[0];
        }
        int port = (proxyServer_.state() == ProxyServer::State::Running)
                 ? proxyServer_.port()
                 : window_->tunnelPanel()->getPort();
        int tid = tunnelMgr_.startTunnelDirect(provider, port);
        if (tid < 0) return "error: failed to start tunnel with " + provider;
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
        return "Tunnel started via " + provider + " (id=" + std::to_string(tid) + ")";
    });

    ipc_.registerCommand("show", [this](const std::string&) -> std::string {
        if (window_) {
            window_->show();
            Fl::awake();
        }
        return "ok";
    });
}

void Application::registerTunnelProviders() {
    tunnelMgr_.registerProvider(std::make_unique<NgrokProvider>());
    tunnelMgr_.registerProvider(std::make_unique<DevTunnelProvider>());
    tunnelMgr_.registerProvider(std::make_unique<CloudflareProvider>());
    window_->tunnelPanel()->setTunnelManager(&tunnelMgr_);
}

void Application::registerClaudeFds() {
    // Unregister old fds first
    unregisterClaudeFds();

    for (auto& as : proxyServer_.activeSessions()) {
        std::string sid = as.sessionId;
        Fl::add_fd(as.stdoutFd, FL_READ,
                   [](int, void* data) {
                       auto* app = (Application*)data;
                       app->proxyServer_.wsServer().service();
                       // Read all active sessions
                       for (auto& s : app->proxyServer_.activeSessions()) {
                           app->proxyServer_.onClaudeDataReady(s.sessionId);
                       }
                   }, this);
        registeredFds_.push_back(as.stdoutFd);
    }

    // WS server listen fd is serviced by the wsServiceCb timer (50ms)
    // No need for Fl::add_fd on it — avoids double-service issues
}

void Application::unregisterClaudeFds() {
    for (int fd : registeredFds_) {
        Fl::remove_fd(fd);
    }
    registeredFds_.clear();
}

void Application::setupCallbacks() {
    // Session panel: Refresh
    window_->sessionPanel()->setOnRefresh([this]() {
        sessionMgr_.refresh();
        window_->sessionPanel()->updateSessions(sessionMgr_.sessions());
        proxyServer_.updateSessionList(sessionMgr_.sessions());
        window_->updateStatusBar();
    });

    // Proxy panel: Toggle server
    window_->proxyPanel()->setOnToggleServer([this]() {
        if (proxyServer_.state() == ProxyServer::State::Running) {
            unregisterClaudeFds();
            proxyServer_.stop();
            window_->tunnelPanel()->setServerPort(0);
            LOG_INFO("Proxy server stopped");
        } else {
            int port = window_->tunnelPanel()->getPort();
            proxyServer_.setClaudeBinary(window_->proxyPanel()->claudeBinaryPath());
            if (proxyServer_.start(port)) {
                proxyServer_.updateSessionList(sessionMgr_.sessions());
                registerClaudeFds();
                window_->tunnelPanel()->setServerPort(proxyServer_.port());
                tunnelMgr_.updatePort(proxyServer_.port());
                window_->tunnelPanel()->updateTunnels();
                LOG_INFO("Proxy server started on port %d", proxyServer_.port());
                LOG_INFO("Claude binary: %s", proxyServer_.claudeBinary().c_str());
                LOG_INFO("Connection string: %s", proxyServer_.connectionString().c_str());
            }
        }
        window_->proxyPanel()->updateStatus();
        window_->updateStatusBar();
    });

    // Proxy panel: Copy URL
    window_->proxyPanel()->setOnCopyUrl([](const std::string& url) {
        Fl::copy(url.c_str(), (int)url.size(), 1);
        LOG_INFO("Copied to clipboard: %s", url.c_str());
    });

    // Tunnel panel: Add tunnel (in idle state)
    window_->tunnelPanel()->setOnAdd([this](const std::string& provider, int port) {
        tunnelMgr_.addTunnel(provider, port);
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    // Tunnel panel: Start tunnel
    window_->tunnelPanel()->setOnStart([this](int tunnelId) {
        if (!tunnelMgr_.startTunnel(tunnelId)) {
            auto* inst = tunnelMgr_.tunnel(tunnelId);
            if (inst && !inst->errorOutput.empty()) {
                fl_alert("Failed to start tunnel:\n%s", inst->errorOutput.c_str());
            }
        }
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    // Tunnel panel: Stop tunnel
    window_->tunnelPanel()->setOnStop([this](int tunnelId) {
        tunnelMgr_.stopTunnel(tunnelId);
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    // Tunnel panel: Restart tunnel
    window_->tunnelPanel()->setOnRestart([this](int tunnelId) {
        tunnelMgr_.restartTunnel(tunnelId);
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    // Tunnel panel: Remove tunnel
    window_->tunnelPanel()->setOnRemove([this](int tunnelId) {
        tunnelMgr_.removeTunnel(tunnelId);
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    // Tunnel panel: Details popup
    window_->tunnelPanel()->setOnDetails([this](int tunnelId) {
        auto* t = tunnelMgr_.tunnel(tunnelId);
        if (!t) return;

        auto makeInfo = [this, tunnelId]() -> TunnelDetailsDialog::TunnelInfo {
            auto* ti = tunnelMgr_.tunnel(tunnelId);
            if (!ti) return {"", "removed", "", 0};
            std::string stateStr;
            switch (ti->state) {
                case TunnelManager::TunnelInstance::Idle:      stateStr = "idle"; break;
                case TunnelManager::TunnelInstance::Starting:  stateStr = "starting"; break;
                case TunnelManager::TunnelInstance::Connected: stateStr = "connected"; break;
                case TunnelManager::TunnelInstance::Failed:    stateStr = "failed"; break;
                case TunnelManager::TunnelInstance::Stopped:   stateStr = "stopped"; break;
            }
            return {ti->publicUrl, stateStr, ti->fullLog, ti->localPort};
        };

        auto info = makeInfo();
        TunnelDetailsDialog::show(t->providerName, info, makeInfo);
    });

    // Tunnel panel: Copy URL
    window_->tunnelPanel()->setOnCopyUrl([](const std::string& url) {
        Fl::copy(url.c_str(), (int)url.size(), 1);
        LOG_INFO("Copied to clipboard: %s", url.c_str());
    });

    tunnelMgr_.setOnUrlReady([this](int tunnelId, const std::string& url) {
        LOG_INFO("Tunnel %d ready: %s", tunnelId, url.c_str());
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    tunnelMgr_.setOnStateChange([this]() {
        window_->tunnelPanel()->updateTunnels();
        window_->updateStatusBar();
    });

    // Session manager change
    sessionMgr_.setOnChange([this]() {
        window_->sessionPanel()->updateSessions(sessionMgr_.sessions());
        proxyServer_.updateSessionList(sessionMgr_.sessions());
        window_->updateStatusBar();
    });

    // Proxy server state change — re-register fds when sessions spawn
    proxyServer_.setOnStateChange([this]() {
        registerClaudeFds();
        window_->proxyPanel()->updateStatus();
        window_->updateStatusBar();
    });

    Fl::add_timeout(0.5, tunnelPollCb, this);
    Fl::add_timeout(0.05, wsServiceCb, this);
}

void Application::refreshTimerCb(void* data) {
    auto* app = (Application*)data;
    app->sessionMgr_.refresh();
    app->proxyServer_.updateSessionList(app->sessionMgr_.sessions());

    if (app->proxyServer_.state() == ProxyServer::State::Running) {
        app->window_->proxyPanel()->updateStatus();
    }

    app->window_->updateStatusBar();
    Fl::repeat_timeout(3.0, refreshTimerCb, data);
}

void Application::tunnelPollCb(void* data) {
    auto* app = (Application*)data;
    app->tunnelMgr_.poll();
    Fl::repeat_timeout(0.5, tunnelPollCb, data);
}

void Application::wsServiceCb(void* data) {
    auto* app = (Application*)data;
    if (app->proxyServer_.state() == ProxyServer::State::Running) {
        app->proxyServer_.wsServer().service();
    }
    Fl::repeat_timeout(0.05, wsServiceCb, data); // 50ms — responsive HTTP serving
}
