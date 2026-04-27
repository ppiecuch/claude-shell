#pragma once
#include "core/TunnelProvider.h"
#include "util/Platform.h"
#include <string>
#include <regex>

class DevTunnelProvider : public TunnelProvider {
public:
    std::string name() const override { return "devtunnel"; }
    std::string executableName() const override { return "devtunnel"; }

    bool requiresAuth() const override { return true; }
    bool checkAuth() const override {
        // devtunnel user show exits 0 if logged in
        std::string path = executablePath();
        if (path.empty()) return false;
        std::string cmd = path + " user show >/dev/null 2>&1";
        int rc = system(cmd.c_str());
        return (rc == 0);
    }

    std::vector<std::string> buildArgs(int localPort) const override {
        return {"host", "-p", std::to_string(localPort), "--allow-anonymous"};
    }

    std::string parsePublicUrl(const std::string& output) const override {
        // devtunnel outputs "Connect via browser: https://xxxxx.devtunnels.ms"
        std::regex urlRegex(R"(https?://\S+\.devtunnels\.\S+)");
        std::smatch match;
        if (std::regex_search(output, match, urlRegex)) {
            return match[0].str();
        }
        return "";
    }
};
