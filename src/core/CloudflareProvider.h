#pragma once
#include "core/TunnelProvider.h"
#include "util/Platform.h"
#include <string>
#include <regex>

class CloudflareProvider : public TunnelProvider {
public:
    std::string name() const override { return "cloudflare"; }
    std::string executableName() const override { return "cloudflared"; }

    std::vector<std::string> buildArgs(int localPort) const override {
        std::vector<std::string> args = {
            "tunnel", "--url", "http://localhost:" + std::to_string(localPort)
        };
        return args;
    }

    std::string parsePublicUrl(const std::string& output) const override {
        // cloudflared outputs URL on stderr: "https://xxxxx.trycloudflare.com"
        std::regex urlRegex(R"(https?://\S+\.trycloudflare\.com)");
        std::smatch match;
        if (std::regex_search(output, match, urlRegex)) {
            return match[0].str();
        }
        return "";
    }
};
