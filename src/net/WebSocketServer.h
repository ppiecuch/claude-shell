#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>

// Minimal RFC 6455 WebSocket + HTTP server (no external deps)
// Supports text frames only, no extensions, no TLS (tunnel provides TLS)

struct WsClient {
    int fd = -1;
    bool handshakeComplete = false;
    bool authenticated = false;
    bool closeAfterSend = false;
    std::string role;           // "controller" or "viewer"
    std::string recvBuffer;
    std::string sendBuffer;
    void* userData = nullptr;
};

class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    bool listen(int port = 0);
    int port() const { return port_; }
    void stop();
    bool isRunning() const { return listenFd_ >= 0; }

    // Service pending I/O. Call from event loop.
    void service();

    // Callbacks
    using ConnectCb    = std::function<void(WsClient*)>;
    using MessageCb    = std::function<void(WsClient*, const std::string&)>;
    using DisconnectCb = std::function<void(WsClient*)>;
    using HttpRequestCb = std::function<std::string(const std::string& path)>;

    void setOnConnect(ConnectCb cb)       { onConnect_ = std::move(cb); }
    void setOnMessage(MessageCb cb)       { onMessage_ = std::move(cb); }
    void setOnDisconnect(DisconnectCb cb) { onDisconnect_ = std::move(cb); }
    void setOnHttpRequest(HttpRequestCb cb) { onHttpRequest_ = std::move(cb); }

    void send(WsClient* client, const std::string& data);
    void broadcast(const std::string& data);
    void disconnect(WsClient* client);

    int listenFd() const { return listenFd_; }
    std::vector<int> clientFds() const;

private:
    int listenFd_ = -1;
    int port_ = 0;
    std::map<int, std::unique_ptr<WsClient>> clients_;

    ConnectCb onConnect_;
    MessageCb onMessage_;
    DisconnectCb onDisconnect_;
    HttpRequestCb onHttpRequest_;

    void acceptNewClient();
    void handleClientData(int fd);
    void processHandshake(WsClient* client);
    void processWebSocketFrame(WsClient* client);
    void sendRawFrame(WsClient* client, const std::string& payload, uint8_t opcode = 0x01);
    void sendHttpResponse(WsClient* client, const std::string& status,
                          const std::string& contentType, const std::string& body);
    void removeClient(int fd);
    void flushSendBuffer(WsClient* client);

    static std::string computeAcceptKey(const std::string& clientKey);
};
