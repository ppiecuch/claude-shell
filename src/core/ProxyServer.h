#pragma once
#include "core/SessionInfo.h"
#include "core/ProcessHandle.h"
#include "net/WebSocketServer.h"
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <functional>
#include <nlohmann/json_fwd.hpp>

class TunnelManager;

// Single proxy server that manages multiple claude sessions.
// Remote clients connect, list sessions, attach to one, and interact.
class ProxyServer {
public:
    enum class State { Stopped, Running, Error };

    ProxyServer();
    ~ProxyServer();

    bool start(int port = 0);
    void stop();

    State state() const { return state_; }
    int port() const;
    const std::string& authToken() const { return authToken_; }

    WebSocketServer& wsServer() { return wsServer_; }

    // Set path to claude binary
    void setTunnelManager(TunnelManager* tm) { tunnelMgr_ = tm; }
    void setClaudeBinary(const std::string& path) { claudeBinary_ = path; }
    const std::string& claudeBinary() const { return claudeBinary_; }

    // Provide session list (called by Application on refresh)
    void updateSessionList(const std::vector<SessionInfo>& sessions);

    // Called when data arrives on a claude process stdout fd
    void onClaudeDataReady(const std::string& sessionId);

    // Get all active claude process fds for FLTK registration
    struct ActiveSession {
        std::string sessionId;
        int stdoutFd;
    };
    std::vector<ActiveSession> activeSessions() const;

    // Connection info
    size_t clientCount() const;
    std::string connectionString() const;

    // Build HTTP response for non-WS requests
    std::string handleHttpRequest(const std::string& path);

    using OnStateChange = std::function<void()>;
    void setOnStateChange(OnStateChange cb) { onStateChange_ = std::move(cb); }

private:
    State state_ = State::Stopped;
    std::string authToken_;
    std::string claudeBinary_ = "claude";
    TunnelManager* tunnelMgr_ = nullptr;
    WebSocketServer wsServer_;

    // Available sessions (updated from SessionManager)
    std::vector<SessionInfo> sessions_;

    // Active claude processes (spawned on attach)
    struct ClaudeSession {
        SessionInfo info;
        std::unique_ptr<ProcessHandle> process;
        std::string stdoutBuffer;
        std::deque<std::string> history;
        static constexpr size_t MAX_HISTORY = 10000;
    };
    std::map<std::string, std::unique_ptr<ClaudeSession>> claudeSessions_;

    // Client state
    struct ClientState {
        bool authenticated = false;
        std::string role;           // "controller" or "viewer"
        std::string attachedSession; // empty = not attached
    };
    std::map<WsClient*, ClientState> clientStates_;

    OnStateChange onStateChange_;

    // WS callbacks
    void onWsConnect(WsClient* client);
    void onWsMessage(WsClient* client, const std::string& msg);
    void onWsDisconnect(WsClient* client);

    // Message handlers
    void handleAuth(WsClient* client, const nlohmann::json& j);
    void handleListSessions(WsClient* client);
    void handleAttach(WsClient* client, const nlohmann::json& j);
    void handleDetach(WsClient* client);
    void handleUserMessage(WsClient* client, const nlohmann::json& j);
    void handleControl(WsClient* client, const nlohmann::json& j);
    void handleCloseTunnel(WsClient* client, const nlohmann::json& j);

    // Helpers
    ClaudeSession* getOrSpawnSession(const std::string& sessionId);
    void processClaudeLine(const std::string& sessionId, const std::string& line);
    void broadcastToSession(const std::string& sessionId, const std::string& json);
    void sendToClient(WsClient* client, const std::string& json);
    void sendError(WsClient* client, const std::string& code, const std::string& msg);
    void sendSessionList(WsClient* client);
    void sendStatus(WsClient* client);
    bool isController(WsClient* client, const std::string& sessionId);
};
