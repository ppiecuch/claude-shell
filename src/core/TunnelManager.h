#pragma once
#include "core/TunnelProvider.h"
#include "core/ProcessHandle.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

class TunnelManager {
public:
    struct TunnelInstance {
        int id = 0;
        std::string providerName;
        int localPort = 0;
        std::string publicUrl;
        std::unique_ptr<ProcessHandle> process;
        std::string outputBuffer;
        std::string errorOutput;  // Captured on failure
        std::string fullLog;      // All stdout/stderr output accumulated across starts
        enum State { Idle, Starting, Connected, Failed, Stopped } state = Idle;
    };

    TunnelManager();

    void registerProvider(std::unique_ptr<TunnelProvider> provider);
    std::vector<std::string> allProviderNames() const;
    std::vector<std::string> availableProviders() const;
    const TunnelProvider* provider(const std::string& name) const;

    // Add a tunnel in Idle state (not started yet)
    int addTunnel(const std::string& providerName, int localPort);

    // Start/stop/restart/remove individual tunnels
    bool startTunnel(int tunnelId);
    void stopTunnel(int tunnelId);
    void restartTunnel(int tunnelId);
    void removeTunnel(int tunnelId);
    void stopAll();

    // Legacy: add + start in one call (used by IPC)
    int startTunnelDirect(const std::string& providerName, int localPort);

    // Update port on all idle/stopped tunnels (when server port changes)
    void updatePort(int newPort);

    const TunnelInstance* tunnel(int tunnelId) const;
    std::vector<const TunnelInstance*> allTunnels() const;
    std::vector<const TunnelInstance*> activeTunnels() const;

    // Poll tunnel processes for output/URL parsing. Call from event loop.
    void poll();

    using OnUrlReady = std::function<void(int tunnelId, const std::string& url)>;
    void setOnUrlReady(OnUrlReady cb) { onUrlReady_ = std::move(cb); }

    using OnStateChange = std::function<void()>;
    void setOnStateChange(OnStateChange cb) { onStateChange_ = std::move(cb); }

private:
    std::map<std::string, std::unique_ptr<TunnelProvider>> providers_;
    std::map<int, std::unique_ptr<TunnelInstance>> tunnels_;
    int nextId_ = 1;
    OnUrlReady onUrlReady_;
    OnStateChange onStateChange_;
};
