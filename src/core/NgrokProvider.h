#pragma once
#include "core/TunnelProvider.h"
#include "util/Platform.h"
#include <nlohmann/json.hpp>

class NgrokProvider : public TunnelProvider {
public:
    std::string name() const override { return "ngrok"; }
    std::string executableName() const override { return "ngrok"; }

    bool requiresAuth() const override { return true; }
    bool checkAuth() const override {
        // ngrok config check exits 0 if authtoken is set
        std::string path = executablePath();
        if (path.empty()) return false;
        std::string cmd = path + " config check 2>&1";
        int rc = system(cmd.c_str());
        return (rc == 0);
    }

    std::vector<std::string> buildArgs(int localPort) const override {
        std::vector<std::string> args = {
            "http", std::to_string(localPort),
            "--log=stdout", "--log-format=json"
        };
        if (!config_.authToken.empty()) {
            // ngrok uses config file for auth, but can also use --authtoken
        }
        if (!config_.region.empty()) {
            args.push_back("--region=" + config_.region);
        }
        return args;
    }

    std::string parsePublicUrl(const std::string& output) const override {
        // ngrok outputs JSON log lines, look for URL
        try {
            auto j = nlohmann::json::parse(output);
            if (j.contains("url")) {
                return j["url"].get<std::string>();
            }
            // Also check msg field for the URL announcement
            if (j.contains("msg") && j["msg"] == "started tunnel") {
                if (j.contains("url"))
                    return j["url"].get<std::string>();
            }
        } catch (...) {}
        return "";
    }
};
