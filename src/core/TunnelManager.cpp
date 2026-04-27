#include "core/TunnelManager.h"
#include "util/Logger.h"

TunnelManager::TunnelManager() = default;

void TunnelManager::registerProvider(std::unique_ptr<TunnelProvider> provider) {
    std::string n = provider->name();
    providers_[n] = std::move(provider);
    LOG_INFO("Registered tunnel provider: %s (available: %s)",
             n.c_str(), providers_[n]->isAvailable() ? "yes" : "no");
}

std::vector<std::string> TunnelManager::allProviderNames() const {
    std::vector<std::string> result;
    for (auto& [name, p] : providers_) {
        result.push_back(name);
    }
    return result;
}

std::vector<std::string> TunnelManager::availableProviders() const {
    std::vector<std::string> result;
    for (auto& [name, p] : providers_) {
        if (p->isAvailable()) result.push_back(name);
    }
    return result;
}

const TunnelProvider* TunnelManager::provider(const std::string& name) const {
    auto it = providers_.find(name);
    return (it != providers_.end()) ? it->second.get() : nullptr;
}

int TunnelManager::addTunnel(const std::string& providerName, int localPort) {
    auto it = providers_.find(providerName);
    if (it == providers_.end()) {
        LOG_ERROR("Unknown tunnel provider: %s", providerName.c_str());
        return -1;
    }

    int id = nextId_++;
    auto instance = std::make_unique<TunnelInstance>();
    instance->id = id;
    instance->providerName = providerName;
    instance->localPort = localPort;
    instance->state = TunnelInstance::Idle;

    tunnels_[id] = std::move(instance);
    LOG_INFO("Added tunnel %d (%s) for port %d in idle state", id, providerName.c_str(), localPort);

    if (onStateChange_) onStateChange_();
    return id;
}

bool TunnelManager::startTunnel(int tunnelId) {
    auto it = tunnels_.find(tunnelId);
    if (it == tunnels_.end()) return false;

    auto& t = it->second;
    if (t->state == TunnelInstance::Starting || t->state == TunnelInstance::Connected)
        return false;  // Already running

    auto provIt = providers_.find(t->providerName);
    if (provIt == providers_.end()) return false;

    auto* prov = provIt->second.get();
    if (!prov->isAvailable()) {
        t->errorOutput = "Provider '" + t->providerName + "' is not available: "
                       + TunnelProvider::statusLabel(prov->checkStatus());
        t->fullLog += "[error] " + t->errorOutput + "\n";
        t->state = TunnelInstance::Failed;
        LOG_ERROR("Tunnel %d: %s", tunnelId, t->errorOutput.c_str());
        if (onStateChange_) onStateChange_();
        return false;
    }

    auto args = prov->buildArgs(t->localPort);
    auto process = ProcessHandle::spawn(prov->executablePath(), args);
    if (!process) {
        t->errorOutput = "Failed to spawn process for '" + t->providerName + "'";
        t->fullLog += "[error] " + t->errorOutput + "\n";
        t->state = TunnelInstance::Failed;
        LOG_ERROR("Tunnel %d: %s", tunnelId, t->errorOutput.c_str());
        if (onStateChange_) onStateChange_();
        return false;
    }

    t->process = std::move(process);
    t->state = TunnelInstance::Starting;
    t->publicUrl.clear();
    t->outputBuffer.clear();
    t->errorOutput.clear();
    t->fullLog += "--- Starting " + t->providerName + " on port "
               + std::to_string(t->localPort) + " ---\n";

    LOG_INFO("Started tunnel %d via %s for port %d", tunnelId, t->providerName.c_str(), t->localPort);
    if (onStateChange_) onStateChange_();
    return true;
}

int TunnelManager::startTunnelDirect(const std::string& providerName, int localPort) {
    int id = addTunnel(providerName, localPort);
    if (id < 0) return -1;
    if (!startTunnel(id)) {
        // Keep it in the list as failed so caller can see error
        return id;
    }
    return id;
}

void TunnelManager::stopTunnel(int tunnelId) {
    auto it = tunnels_.find(tunnelId);
    if (it == tunnels_.end()) return;

    it->second->process.reset();
    it->second->publicUrl.clear();
    it->second->state = TunnelInstance::Stopped;
    it->second->fullLog += "--- Stopped ---\n";

    LOG_INFO("Stopped tunnel %d", tunnelId);
    if (onStateChange_) onStateChange_();
}

void TunnelManager::restartTunnel(int tunnelId) {
    auto it = tunnels_.find(tunnelId);
    if (it == tunnels_.end()) return;

    // Stop first
    it->second->process.reset();
    it->second->publicUrl.clear();
    it->second->outputBuffer.clear();
    it->second->errorOutput.clear();

    // Then start
    startTunnel(tunnelId);
}

void TunnelManager::removeTunnel(int tunnelId) {
    auto it = tunnels_.find(tunnelId);
    if (it == tunnels_.end()) return;

    it->second->process.reset();
    tunnels_.erase(it);

    LOG_INFO("Removed tunnel %d", tunnelId);
    if (onStateChange_) onStateChange_();
}

void TunnelManager::stopAll() {
    for (auto& [id, t] : tunnels_) {
        t->process.reset();
        t->publicUrl.clear();
        t->state = TunnelInstance::Stopped;
    }
    if (onStateChange_) onStateChange_();
}

void TunnelManager::updatePort(int newPort) {
    for (auto& [id, t] : tunnels_) {
        if (t->state == TunnelInstance::Idle || t->state == TunnelInstance::Stopped)
            t->localPort = newPort;
    }
}

const TunnelManager::TunnelInstance* TunnelManager::tunnel(int tunnelId) const {
    auto it = tunnels_.find(tunnelId);
    return (it != tunnels_.end()) ? it->second.get() : nullptr;
}

std::vector<const TunnelManager::TunnelInstance*> TunnelManager::allTunnels() const {
    std::vector<const TunnelInstance*> result;
    for (auto& [id, t] : tunnels_) {
        result.push_back(t.get());
    }
    return result;
}

std::vector<const TunnelManager::TunnelInstance*> TunnelManager::activeTunnels() const {
    std::vector<const TunnelInstance*> result;
    for (auto& [id, t] : tunnels_) {
        if (t->state == TunnelInstance::Starting || t->state == TunnelInstance::Connected)
            result.push_back(t.get());
    }
    return result;
}

void TunnelManager::poll() {
    char buf[4096];
    for (auto& [id, t] : tunnels_) {
        if (!t->process || t->state == TunnelInstance::Stopped || t->state == TunnelInstance::Idle)
            continue;

        // Check if process died
        if (!t->process->isRunning()) {
            if (t->state != TunnelInstance::Connected) {
                t->state = TunnelInstance::Failed;
                t->errorOutput = t->outputBuffer;
                t->fullLog += "[error] Process exited unexpectedly\n";
                LOG_ERROR("Tunnel %d (%s) process exited unexpectedly", id, t->providerName.c_str());
                if (onStateChange_) onStateChange_();
            }
            continue;
        }

        // Read stdout and stderr for URL parsing
        bool gotData = false;
        ssize_t n;

        while ((n = t->process->readStdout(buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            t->outputBuffer += buf;
            t->fullLog += buf;
            gotData = true;
        }
        while ((n = t->process->readStderr(buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            t->outputBuffer += buf;
            t->fullLog += buf;
            gotData = true;
        }

        if (gotData && t->state == TunnelInstance::Starting) {
            // Try to parse URL from output
            auto* prov = providers_[t->providerName].get();

            // Try each line
            size_t pos = 0;
            while ((pos = t->outputBuffer.find('\n')) != std::string::npos) {
                std::string line = t->outputBuffer.substr(0, pos);
                t->outputBuffer.erase(0, pos + 1);

                std::string url = prov->parsePublicUrl(line);
                if (!url.empty()) {
                    t->publicUrl = url;
                    t->state = TunnelInstance::Connected;
                    LOG_INFO("Tunnel %d URL: %s", id, url.c_str());
                    if (onUrlReady_) onUrlReady_(id, url);
                    if (onStateChange_) onStateChange_();
                    break;
                }
            }
        }
    }
}
