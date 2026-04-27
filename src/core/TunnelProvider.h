#pragma once
#include <string>
#include <vector>
#include "util/Platform.h"

class TunnelProvider {
public:
    virtual ~TunnelProvider() = default;

    virtual std::string name() const = 0;
    virtual std::string executableName() const = 0;
    virtual std::vector<std::string> buildArgs(int localPort) const = 0;
    virtual std::string parsePublicUrl(const std::string& output) const = 0;

    // Override in subclass to check if the user is authenticated
    virtual bool checkAuth() const { return true; }
    // Whether this provider requires authentication at all
    virtual bool requiresAuth() const { return false; }

    // Status of the provider executable
    enum class Status { Ready, NotInstalled, NotExecutable, Corrupted, NotAuthorized };

    Status checkStatus() const {
        std::string path = Platform::findExecutable(executableName());
        if (path.empty()) return Status::NotInstalled;
        if (!Platform::isExecutable(path)) return Status::NotExecutable;

        // Quick sanity check: run with --version or --help
        std::string cmd = path + " --version 2>&1 | head -1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return Status::Corrupted;
        char buf[256];
        bool gotOutput = (fgets(buf, sizeof(buf), pipe) != nullptr);
        int rc = pclose(pipe);
        if (!gotOutput && rc > 1) return Status::Corrupted;

        // Check auth if required
        if (requiresAuth() && !checkAuth()) return Status::NotAuthorized;

        return Status::Ready;
    }

    static const char* statusLabel(Status s) {
        switch (s) {
            case Status::Ready:         return "\xE2\x9C\x93"; // ✓
            case Status::NotInstalled:  return "(not installed)";
            case Status::NotExecutable: return "(not executable)";
            case Status::Corrupted:     return "(corrupted)";
            case Status::NotAuthorized: return "(not authorized)";
        }
        return "";
    }

    bool isAvailable() const { return checkStatus() == Status::Ready; }

    std::string executablePath() const {
        return Platform::findExecutable(executableName());
    }

    struct Config {
        std::string authToken;
        std::string region;
        std::string subdomain;
        std::string protocol = "http";
    };

    void setConfig(const Config& cfg) { config_ = cfg; }
    const Config& config() const { return config_; }

protected:
    Config config_;
};
