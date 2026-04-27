#include "core/SessionManager.h"
#include "util/Platform.h"
#include "util/Logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

SessionManager::SessionManager()
    : sessionsDir_(Platform::claudeSessionsDir())
{
    LOG_INFO("SessionManager scanning: %s", sessionsDir_.c_str());
}

void SessionManager::refresh() {
    std::vector<SessionInfo> newSessions;

    if (!fs::exists(sessionsDir_) || !fs::is_directory(sessionsDir_)) {
        if (!sessions_.empty() && onChange_) {
            sessions_.clear();
            onChange_();
        }
        return;
    }

    for (auto& entry : fs::directory_iterator(sessionsDir_)) {
        if (entry.path().extension() != ".json") continue;
        try {
            auto info = parseSessionFile(entry.path().string());
            if (info.pid > 0) {
                info.alive = Platform::isProcessAlive(info.pid);
                newSessions.push_back(std::move(info));
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to parse session file %s: %s",
                     entry.path().string().c_str(), e.what());
        }
    }

    std::sort(newSessions.begin(), newSessions.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.startedAt > b.startedAt;
              });

    // Check if anything changed
    bool changed = newSessions.size() != sessions_.size();
    if (!changed) {
        for (size_t i = 0; i < newSessions.size(); i++) {
            if (newSessions[i].sessionId != sessions_[i].sessionId ||
                newSessions[i].alive != sessions_[i].alive) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        sessions_ = std::move(newSessions);
        LOG_INFO("Found %zu sessions", sessions_.size());
        if (onChange_) onChange_();
    }
}

SessionInfo SessionManager::parseSessionFile(const std::string& path) const {
    std::ifstream f(path);
    if (!f.is_open()) return {};

    nlohmann::json j;
    f >> j;

    SessionInfo info;
    info.pid = j.value("pid", 0);
    info.sessionId = j.value("sessionId", "");
    info.cwd = j.value("cwd", "");
    info.startedAt = j.value("startedAt", int64_t(0));
    info.kind = j.value("kind", "");
    info.entrypoint = j.value("entrypoint", "");
    info.name = j.value("name", "");
    return info;
}
