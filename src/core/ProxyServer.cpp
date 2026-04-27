#include "core/ProxyServer.h"
#include "core/TunnelManager.h"
#include "net/AuthToken.h"
#include "util/Logger.h"
#include "util/incbin.h"
#include <nlohmann/json.hpp>
#include <cstring>

// Embedded frontend files (defined in resources.cpp via INCTXT)
INCTXT_EXTERN(FrontendHtml);
INCTXT_EXTERN(FrontendCss);
INCTXT_EXTERN(FrontendJs);

using json = nlohmann::json;

ProxyServer::ProxyServer() {
    authToken_ = AuthToken::generate();
}

ProxyServer::~ProxyServer() {
    stop();
}

bool ProxyServer::start(int port) {
    wsServer_.setOnConnect([this](WsClient* c) { onWsConnect(c); });
    wsServer_.setOnMessage([this](WsClient* c, const std::string& m) { onWsMessage(c, m); });
    wsServer_.setOnDisconnect([this](WsClient* c) { onWsDisconnect(c); });
    wsServer_.setOnHttpRequest([this](const std::string& p) { return handleHttpRequest(p); });

    if (!wsServer_.listen(port)) {
        state_ = State::Error;
        return false;
    }

    state_ = State::Running;
    LOG_INFO("Proxy server started on port %d", wsServer_.port());
    return true;
}

void ProxyServer::stop() {
    // Clean up all claude processes
    claudeSessions_.clear();
    clientStates_.clear();
    wsServer_.stop();
    state_ = State::Stopped;
    LOG_INFO("Proxy server stopped");
}

int ProxyServer::port() const {
    return wsServer_.port();
}

void ProxyServer::updateSessionList(const std::vector<SessionInfo>& sessions) {
    sessions_ = sessions;
}

size_t ProxyServer::clientCount() const {
    return clientStates_.size();
}

std::string ProxyServer::connectionString() const {
    return "ws://localhost:" + std::to_string(port()) + "/ws?token=" + authToken_;
}

std::vector<ProxyServer::ActiveSession> ProxyServer::activeSessions() const {
    std::vector<ActiveSession> result;
    for (auto& [id, cs] : claudeSessions_) {
        if (cs->process && cs->process->isRunning()) {
            result.push_back({id, cs->process->stdoutFd()});
        }
    }
    return result;
}

// --- Claude I/O ---

void ProxyServer::onClaudeDataReady(const std::string& sessionId) {
    auto it = claudeSessions_.find(sessionId);
    if (it == claudeSessions_.end()) return;

    auto* cs = it->second.get();
    if (!cs->process) return;

    char buf[8192];
    ssize_t n;
    while ((n = cs->process->readStdout(buf, sizeof(buf))) > 0) {
        cs->stdoutBuffer.append(buf, n);

        size_t pos;
        while ((pos = cs->stdoutBuffer.find('\n')) != std::string::npos) {
            std::string line = cs->stdoutBuffer.substr(0, pos);
            cs->stdoutBuffer.erase(0, pos + 1);
            if (!line.empty()) {
                processClaudeLine(sessionId, line);
            }
        }
    }

    if (!cs->process->isRunning()) {
        LOG_WARN("Claude process exited for session %s (exit code %d)",
                 sessionId.c_str(), cs->process->exitCode());
        json status = {
            {"type", "session_ended"},
            {"sessionId", sessionId},
            {"exitCode", cs->process->exitCode()}
        };
        broadcastToSession(sessionId, status.dump());
    }
}

void ProxyServer::processClaudeLine(const std::string& sessionId, const std::string& line) {
    auto it = claudeSessions_.find(sessionId);
    if (it == claudeSessions_.end()) return;

    try {
        json event = json::parse(line);
        json envelope = {
            {"type", "claude_event"},
            {"sessionId", sessionId},
            {"event", event}
        };
        std::string msg = envelope.dump();

        auto* cs = it->second.get();
        cs->history.push_back(msg);
        if (cs->history.size() > ClaudeSession::MAX_HISTORY)
            cs->history.pop_front();

        broadcastToSession(sessionId, msg);
    } catch (const json::parse_error& e) {
        LOG_WARN("Failed to parse claude output: %s", e.what());
    }
}

ProxyServer::ClaudeSession* ProxyServer::getOrSpawnSession(const std::string& sessionId) {
    auto it = claudeSessions_.find(sessionId);
    if (it != claudeSessions_.end() && it->second->process && it->second->process->isRunning()) {
        return it->second.get();
    }

    // Find session info
    const SessionInfo* info = nullptr;
    for (auto& s : sessions_) {
        if (s.sessionId == sessionId) { info = &s; break; }
    }
    if (!info) return nullptr;

    // Spawn claude process
    std::vector<std::string> args = {
        "--print",
        "--resume", sessionId,
        "--output-format", "stream-json",
        "--input-format", "stream-json",
        "--include-partial-messages",
        "--replay-user-messages"
    };

    auto process = ProcessHandle::spawn(claudeBinary_, args, info->cwd);
    if (!process) {
        LOG_ERROR("Failed to spawn claude for session %s", sessionId.c_str());
        return nullptr;
    }

    auto cs = std::make_unique<ClaudeSession>();
    cs->info = *info;
    cs->process = std::move(process);

    auto* ptr = cs.get();
    claudeSessions_[sessionId] = std::move(cs);

    LOG_INFO("Spawned claude process for session %s (PID %d)",
             sessionId.c_str(), ptr->process->pid());

    if (onStateChange_) onStateChange_();
    return ptr;
}

// --- WebSocket callbacks ---

void ProxyServer::onWsConnect(WsClient* client) {
    clientStates_[client] = ClientState{};
}

void ProxyServer::onWsDisconnect(WsClient* client) {
    clientStates_.erase(client);
    if (onStateChange_) onStateChange_();
}

void ProxyServer::onWsMessage(WsClient* client, const std::string& msg) {
    try {
        json j = json::parse(msg);
        std::string type = j.value("type", "");

        auto it = clientStates_.find(client);
        if (it == clientStates_.end()) return;
        auto& state = it->second;

        if (!state.authenticated) {
            if (type == "auth") {
                handleAuth(client, j);
            } else {
                sendError(client, "auth_required", "Must authenticate first");
            }
            return;
        }

        if (type == "ping") {
            sendToClient(client, json({{"type", "pong"}}).dump());
        } else if (type == "list_sessions") {
            handleListSessions(client);
        } else if (type == "attach") {
            handleAttach(client, j);
        } else if (type == "detach") {
            handleDetach(client);
        } else if (type == "user_message") {
            handleUserMessage(client, j);
        } else if (type == "tool_response") {
            handleUserMessage(client, j); // Forward as-is
        } else if (type == "control") {
            handleControl(client, j);
        } else if (type == "close_tunnel") {
            handleCloseTunnel(client, j);
        } else if (type == "history_request") {
            if (!state.attachedSession.empty()) {
                auto csIt = claudeSessions_.find(state.attachedSession);
                if (csIt != claudeSessions_.end()) {
                    json historyMsg = {{"type", "history"}, {"events", json::array()}};
                    for (auto& ev : csIt->second->history) {
                        try { historyMsg["events"].push_back(json::parse(ev)); } catch (...) {}
                    }
                    sendToClient(client, historyMsg.dump());
                }
            }
        }

    } catch (const json::parse_error&) {
        sendError(client, "invalid_json", "Failed to parse message");
    }
}

// --- Message handlers ---

void ProxyServer::handleAuth(WsClient* client, const json& j) {
    std::string token = j.value("token", "");
    std::string role = j.value("role", "viewer");

    if (!AuthToken::validate(token, authToken_)) {
        sendError(client, "auth_failed", "Invalid token");
        wsServer_.disconnect(client);
        return;
    }

    auto& state = clientStates_[client];
    state.authenticated = true;
    state.role = role;

    json result = {
        {"type", "auth_result"},
        {"success", true},
        {"role", role}
    };
    sendToClient(client, result.dump());

    // Send session list immediately after auth
    handleListSessions(client);
    LOG_INFO("Client authenticated as %s", role.c_str());
}

void ProxyServer::handleListSessions(WsClient* client) {
    sendSessionList(client);
}

void ProxyServer::handleAttach(WsClient* client, const json& j) {
    std::string sessionId = j.value("sessionId", "");
    if (sessionId.empty()) {
        sendError(client, "invalid_session", "sessionId required");
        return;
    }

    // Check session exists
    bool found = false;
    for (auto& s : sessions_) {
        if (s.sessionId == sessionId) { found = true; break; }
    }
    if (!found) {
        sendError(client, "session_not_found", "Session not found");
        return;
    }

    // Spawn or reuse claude process
    auto* cs = getOrSpawnSession(sessionId);
    if (!cs) {
        sendError(client, "spawn_failed", "Failed to start claude process");
        return;
    }

    auto& state = clientStates_[client];
    state.attachedSession = sessionId;

    json result = {
        {"type", "attached"},
        {"sessionId", sessionId},
        {"sessionInfo", {
            {"name", cs->info.name},
            {"cwd", cs->info.cwd},
            {"pid", cs->info.pid},
            {"startedAt", cs->info.startedAt}
        }}
    };
    sendToClient(client, result.dump());
    sendStatus(client);

    LOG_INFO("Client attached to session %s", sessionId.c_str());
    if (onStateChange_) onStateChange_();
}

void ProxyServer::handleDetach(WsClient* client) {
    auto& state = clientStates_[client];
    std::string prevSession = state.attachedSession;
    state.attachedSession.clear();

    json result = {{"type", "detached"}};
    sendToClient(client, result.dump());
    sendSessionList(client);

    LOG_INFO("Client detached from session %s", prevSession.c_str());
    if (onStateChange_) onStateChange_();
}

void ProxyServer::handleUserMessage(WsClient* client, const json& j) {
    auto& state = clientStates_[client];
    if (state.attachedSession.empty()) {
        sendError(client, "not_attached", "Must attach to a session first");
        return;
    }
    if (!isController(client, state.attachedSession)) {
        sendError(client, "forbidden", "Only controller can send messages");
        return;
    }

    auto it = claudeSessions_.find(state.attachedSession);
    if (it == claudeSessions_.end() || !it->second->process) return;

    std::string line = j.dump() + "\n";
    it->second->process->writeStdin(line.data(), line.size());

    // Echo back
    if (j.contains("content")) {
        json echo = {{"type", "input_echo"}, {"content", j["content"]}};
        broadcastToSession(state.attachedSession, echo.dump());
    }
}

void ProxyServer::handleControl(WsClient* client, const json& j) {
    auto& state = clientStates_[client];
    if (state.attachedSession.empty()) return;
    if (!isController(client, state.attachedSession)) {
        sendError(client, "forbidden", "Only controller can send control messages");
        return;
    }

    std::string action = j.value("action", "");
    if (action == "abort") {
        auto it = claudeSessions_.find(state.attachedSession);
        if (it != claudeSessions_.end() && it->second->process) {
            it->second->process->terminate();
            LOG_INFO("Abort sent to session %s", state.attachedSession.c_str());
        }
    }
}

void ProxyServer::handleCloseTunnel(WsClient* client, const json& j) {
    if (!tunnelMgr_) {
        sendError(client, "not_available", "Tunnel management not available");
        return;
    }

    // Client sends its host (e.g. "abc123.trycloudflare.com")
    std::string host = j.value("host", "");
    if (host.empty()) {
        sendError(client, "missing_host", "Host field required");
        return;
    }

    // Find tunnel whose publicUrl contains this host
    for (auto* t : tunnelMgr_->activeTunnels()) {
        if (t->publicUrl.find(host) != std::string::npos) {
            LOG_INFO("Remote client requested tunnel close (id=%d, host=%s)",
                     t->id, host.c_str());
            tunnelMgr_->stopTunnel(t->id);
            sendToClient(client, json({
                {"type", "tunnel_closed"},
                {"message", "Tunnel closed successfully"}
            }).dump());
            return;
        }
    }

    sendError(client, "tunnel_not_found", "No active tunnel matches this connection");
}

// --- Helpers ---

bool ProxyServer::isController(WsClient* client, const std::string& sessionId) {
    auto it = clientStates_.find(client);
    if (it == clientStates_.end()) return false;
    if (it->second.role != "controller") return false;

    // First controller attached to this session wins
    for (auto& [c, s] : clientStates_) {
        if (c == client) return true;
        if (s.role == "controller" && s.attachedSession == sessionId) return false;
    }
    return true;
}

void ProxyServer::broadcastToSession(const std::string& sessionId, const std::string& jsonStr) {
    for (auto& [client, state] : clientStates_) {
        if (state.authenticated && state.attachedSession == sessionId) {
            wsServer_.send(client, jsonStr);
        }
    }
}

void ProxyServer::sendToClient(WsClient* client, const std::string& jsonStr) {
    wsServer_.send(client, jsonStr);
}

void ProxyServer::sendError(WsClient* client, const std::string& code, const std::string& msg) {
    json err = {{"type", "error"}, {"code", code}, {"message", msg}};
    wsServer_.send(client, err.dump());
}

void ProxyServer::sendSessionList(WsClient* client) {
    json list = {{"type", "session_list"}, {"sessions", json::array()}};
    for (auto& s : sessions_) {
        bool hasProcess = claudeSessions_.count(s.sessionId) > 0 &&
                          claudeSessions_[s.sessionId]->process &&
                          claudeSessions_[s.sessionId]->process->isRunning();
        list["sessions"].push_back({
            {"sessionId", s.sessionId},
            {"name", s.name},
            {"cwd", s.cwd},
            {"pid", s.pid},
            {"startedAt", s.startedAt},
            {"alive", s.alive},
            {"proxied", hasProcess}
        });
    }
    wsServer_.send(client, list.dump());
}

void ProxyServer::sendStatus(WsClient* client) {
    auto& state = clientStates_[client];
    int viewers = 0, controllers = 0;
    for (auto& [c, s] : clientStates_) {
        if (s.attachedSession == state.attachedSession) {
            if (s.role == "controller") controllers++;
            else viewers++;
        }
    }

    json status = {
        {"type", "status"},
        {"sessionId", state.attachedSession},
        {"viewers", viewers},
        {"controllers", controllers},
        {"claudeProcessAlive", claudeSessions_.count(state.attachedSession) > 0}
    };
    wsServer_.send(client, status.dump());
}

static std::string httpResponse(const std::string& status, const std::string& contentType,
                                const char* body, size_t bodyLen) {
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + std::to_string(bodyLen) + "\r\n"
           "\r\n" + std::string(body, bodyLen);
}

std::string ProxyServer::handleHttpRequest(const std::string& path) {
    LOG_DEBUG("HTTP request: %s", path.c_str());

    if (path == "/health") {
        json health = {
            {"status", "ok"},
            {"clients", (int)clientStates_.size()},
            {"sessions", (int)sessions_.size()},
            {"activeProcesses", (int)claudeSessions_.size()}
        };
        std::string body = health.dump();
        return "HTTP/1.1 200 OK\r\n"
               "Content-Type: application/json\r\n"
               "Content-Length: " + std::to_string(body.size()) + "\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Connection: close\r\n"
               "\r\n" + body;
    }

    // Serve embedded frontend (INCTXT size includes null terminator, use strlen)
    if (path == "/" || path == "/index.html") {
        return httpResponse("200 OK", "text/html; charset=utf-8",
                            gFrontendHtmlData, strlen(gFrontendHtmlData));
    }
    if (path == "/style.css") {
        return httpResponse("200 OK", "text/css; charset=utf-8",
                            gFrontendCssData, strlen(gFrontendCssData));
    }
    if (path == "/app.js") {
        return httpResponse("200 OK", "application/javascript; charset=utf-8",
                            gFrontendJsData, strlen(gFrontendJsData));
    }

    std::string body = "Not Found";
    return httpResponse("404 Not Found", "text/plain", body.c_str(), body.size());
}
