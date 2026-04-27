#include "net/WebSocketServer.h"
#include "util/Logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <sstream>

// SHA-1 for WebSocket handshake (minimal implementation)
namespace {

// Minimal correct SHA-1 (RFC 3174) for WebSocket handshake
struct SHA1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t block[64];
    size_t blockLen = 0;
    uint64_t totalLen = 0;

    void update(const std::string& s) { update((const uint8_t*)s.data(), s.size()); }

    void update(const uint8_t* data, size_t len) {
        totalLen += len;
        for (size_t i = 0; i < len; i++) {
            block[blockLen++] = data[i];
            if (blockLen == 64) { processBlock(); blockLen = 0; }
        }
    }

    std::string final_hex() {
        uint64_t bits = totalLen * 8;
        // Pad: append 0x80
        block[blockLen++] = 0x80;
        // If not enough room for 8-byte length, fill and process
        if (blockLen > 56) {
            while (blockLen < 64) block[blockLen++] = 0;
            processBlock(); blockLen = 0;
        }
        // Fill with zeros up to length field
        while (blockLen < 56) block[blockLen++] = 0;
        // Append length in big-endian
        for (int i = 7; i >= 0; i--)
            block[blockLen++] = (bits >> (i * 8)) & 0xFF;
        processBlock();
        // Output
        std::string result(20, '\0');
        for (int i = 0; i < 5; i++) {
            result[i*4]   = (h[i] >> 24) & 0xFF;
            result[i*4+1] = (h[i] >> 16) & 0xFF;
            result[i*4+2] = (h[i] >> 8) & 0xFF;
            result[i*4+3] = h[i] & 0xFF;
        }
        return result;
    }

    void processBlock() {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
                   ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
        for (int i = 16; i < 80; i++) {
            uint32_t t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (t << 1) | (t >> 31);
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b&c)|((~b)&d); k = 0x5A827999; }
            else if (i < 40) { f = b^c^d;           k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b&c)|(b&d)|(c&d); k = 0x8F1BBCDC; }
            else              { f = b^c^d;           k = 0xCA62C1D6; }
            uint32_t t = ((a << 5)|(a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30)|(b >> 2); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
};

std::string base64Encode(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out += table[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6) out += table[((val << 8) >> (valb + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}

void setNonBlockingFd(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace

WebSocketServer::WebSocketServer() = default;

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::listen(int port) {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("socket() failed: %s", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlockingFd(listenFd_);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed: %s", strerror(errno));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::listen(listenFd_, 16) < 0) {
        LOG_ERROR("listen() failed: %s", strerror(errno));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    // Get actual port
    socklen_t len = sizeof(addr);
    getsockname(listenFd_, (struct sockaddr*)&addr, &len);
    port_ = ntohs(addr.sin_port);

    LOG_INFO("WebSocket server listening on port %d", port_);
    return true;
}

void WebSocketServer::stop() {
    for (auto& [fd, client] : clients_) {
        close(fd);
    }
    clients_.clear();
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
    port_ = 0;
}

void WebSocketServer::service() {
    if (listenFd_ < 0) return;

    // Build poll set
    std::vector<struct pollfd> fds;
    fds.push_back({listenFd_, POLLIN, 0});
    for (auto& [fd, client] : clients_) {
        short events = POLLIN;
        if (!client->sendBuffer.empty()) events |= POLLOUT;
        fds.push_back({fd, events, 0});
    }

    int ret = poll(fds.data(), fds.size(), 0);
    if (ret <= 0) return;

    // Check listen socket
    if (fds[0].revents & POLLIN) {
        acceptNewClient();
    }

    // Check client sockets
    for (size_t i = 1; i < fds.size(); i++) {
        if (fds[i].revents == 0) continue;

        // POLLHUP with POLLIN means data available before hangup — read first
        if (fds[i].revents & POLLIN) {
            auto it = clients_.find(fds[i].fd);
            if (it != clients_.end()) handleClientData(fds[i].fd);
        }
        if (fds[i].revents & POLLOUT) {
            auto it = clients_.find(fds[i].fd);
            if (it != clients_.end()) flushSendBuffer(it->second.get());
        }
        // Only disconnect on error/hangup if no data was available
        if ((fds[i].revents & (POLLERR | POLLNVAL)) ||
            ((fds[i].revents & POLLHUP) && !(fds[i].revents & POLLIN))) {
            removeClient(fds[i].fd);
        }
    }
}

void WebSocketServer::acceptNewClient() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd = accept(listenFd_, (struct sockaddr*)&addr, &len);
    if (fd < 0) return;

    // Socket options for reliable WebSocket connections
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    setNonBlockingFd(fd);

    auto client = std::make_unique<WsClient>();
    client->fd = fd;
    clients_[fd] = std::move(client);
    LOG_DEBUG("New connection fd=%d", fd);
}

void WebSocketServer::handleClientData(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    auto* client = it->second.get();

    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return; // No data yet
        LOG_DEBUG("Read error fd=%d errno=%d", fd, errno);
        removeClient(fd);
        return;
    }
    if (n == 0) {
        LOG_DEBUG("EOF fd=%d (peer closed)", fd);
        removeClient(fd);
        return;
    }
    LOG_DEBUG("Read %zd bytes from fd=%d, handshake=%d", n, fd, client->handshakeComplete);

    client->recvBuffer.append(buf, n);

    if (!client->handshakeComplete) {
        processHandshake(client);
    } else {
        processWebSocketFrame(client);
    }
}

void WebSocketServer::processHandshake(WsClient* client) {
    auto& buf = client->recvBuffer;
    size_t headerEnd = buf.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return; // Need more data

    std::string header = buf.substr(0, headerEnd);
    buf.erase(0, headerEnd + 4);

    // Parse request line
    std::string requestLine = header.substr(0, header.find("\r\n"));
    std::string method, fullPath, version;
    std::istringstream rls(requestLine);
    rls >> method >> fullPath >> version;

    // Strip query string for routing
    std::string path = fullPath;
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);

    // Check for WebSocket upgrade (case-insensitive header matching)
    std::string wsKey;
    bool isUpgrade = false;
    std::istringstream hs(header);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Case-insensitive check for Upgrade header
        std::string lower = line;
        for (auto& c : lower) c = tolower(c);
        if (lower.find("upgrade:") != std::string::npos &&
            lower.find("websocket") != std::string::npos)
            isUpgrade = true;
        if (lower.find("sec-websocket-key:") != std::string::npos) {
            wsKey = line.substr(line.find(':') + 1);
            while (!wsKey.empty() && wsKey.front() == ' ') wsKey.erase(0, 1);
            while (!wsKey.empty() && wsKey.back() == ' ') wsKey.pop_back();
        }
    }

    LOG_DEBUG("Handshake: upgrade=%d wsKey=%s path=%s (fd=%d)\nHeaders:\n%s",
              isUpgrade, wsKey.empty() ? "(none)" : wsKey.substr(0,8).c_str(),
              path.c_str(), client->fd, header.c_str());

    if (!isUpgrade || wsKey.empty()) {
        // Regular HTTP request
        LOG_DEBUG("HTTP %s %s (fd=%d)", method.c_str(), path.c_str(), client->fd);
        if (onHttpRequest_) {
            std::string response = onHttpRequest_(path);
            client->sendBuffer += response;
            flushSendBuffer(client);
        } else {
            sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found");
        }
        // Close after send buffer is flushed
        if (client->sendBuffer.empty()) {
            removeClient(client->fd);
        } else {
            client->closeAfterSend = true;
        }
        return;
    }

    // WebSocket upgrade
    std::string acceptKey = computeAcceptKey(wsKey);
    LOG_DEBUG("WS upgrade: key=%s accept=%s (fd=%d)", wsKey.c_str(), acceptKey.c_str(), client->fd);
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    LOG_DEBUG("WS 101 response (%zu bytes): [%s] (fd=%d)",
              response.size(), response.c_str(), client->fd);
    client->sendBuffer += response;
    flushSendBuffer(client);
    client->handshakeComplete = true;
    LOG_DEBUG("WS handshake complete, sendBuf remaining=%zu (fd=%d)",
              client->sendBuffer.size(), client->fd);

    if (onConnect_) onConnect_(client);
}

void WebSocketServer::processWebSocketFrame(WsClient* client) {
    auto& buf = client->recvBuffer;

    while (buf.size() >= 2) {
        uint8_t b0 = (uint8_t)buf[0];
        uint8_t b1 = (uint8_t)buf[1];
        uint8_t opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        uint64_t payloadLen = b1 & 0x7F;

        size_t headerLen = 2;
        if (payloadLen == 126) {
            if (buf.size() < 4) return;
            payloadLen = ((uint8_t)buf[2] << 8) | (uint8_t)buf[3];
            headerLen = 4;
        } else if (payloadLen == 127) {
            if (buf.size() < 10) return;
            payloadLen = 0;
            for (int i = 0; i < 8; i++)
                payloadLen = (payloadLen << 8) | (uint8_t)buf[2 + i];
            headerLen = 10;
        }

        if (masked) headerLen += 4;
        if (buf.size() < headerLen + payloadLen) return;

        std::string payload(payloadLen, '\0');
        if (masked) {
            uint8_t mask[4];
            memcpy(mask, buf.data() + headerLen - 4, 4);
            for (uint64_t i = 0; i < payloadLen; i++)
                payload[i] = buf[headerLen + i] ^ mask[i % 4];
        } else {
            memcpy(&payload[0], buf.data() + headerLen, payloadLen);
        }

        buf.erase(0, headerLen + payloadLen);

        if (opcode == 0x08) {
            // Close frame
            removeClient(client->fd);
            return;
        } else if (opcode == 0x09) {
            // Ping -> Pong
            sendRawFrame(client, payload, 0x0A);
        } else if (opcode == 0x01) {
            // Text frame
            if (onMessage_) onMessage_(client, payload);
        }
    }
}

void WebSocketServer::send(WsClient* client, const std::string& data) {
    sendRawFrame(client, data, 0x01);
}

void WebSocketServer::broadcast(const std::string& data) {
    for (auto& [fd, client] : clients_) {
        if (client->handshakeComplete) {
            sendRawFrame(client.get(), data, 0x01);
        }
    }
}

void WebSocketServer::disconnect(WsClient* client) {
    if (client) removeClient(client->fd);
}

std::vector<int> WebSocketServer::clientFds() const {
    std::vector<int> result;
    for (auto& [fd, _] : clients_) result.push_back(fd);
    return result;
}

void WebSocketServer::sendRawFrame(WsClient* client, const std::string& payload, uint8_t opcode) {
    std::string frame;
    frame += (char)(0x80 | opcode); // FIN + opcode

    if (payload.size() < 126) {
        frame += (char)payload.size();
    } else if (payload.size() < 65536) {
        frame += (char)126;
        frame += (char)((payload.size() >> 8) & 0xFF);
        frame += (char)(payload.size() & 0xFF);
    } else {
        frame += (char)127;
        for (int i = 7; i >= 0; i--)
            frame += (char)((payload.size() >> (i * 8)) & 0xFF);
    }
    frame += payload;

    client->sendBuffer += frame;
    flushSendBuffer(client);
}

void WebSocketServer::sendHttpResponse(WsClient* client, const std::string& status,
                                        const std::string& contentType, const std::string& body) {
    std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: " + contentType + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;
    client->sendBuffer += response;
    flushSendBuffer(client);
}

void WebSocketServer::removeClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    auto* client = it->second.get();
    if (client->handshakeComplete && onDisconnect_) {
        onDisconnect_(client);
    }

    close(fd);
    clients_.erase(it);
    LOG_DEBUG("Client disconnected fd=%d", fd);
}

void WebSocketServer::flushSendBuffer(WsClient* client) {
    while (!client->sendBuffer.empty()) {
        ssize_t n = write(client->fd, client->sendBuffer.data(), client->sendBuffer.size());
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            removeClient(client->fd);
            return;
        }
        client->sendBuffer.erase(0, n);
    }
    // Close connection after HTTP response is fully sent
    if (client->closeAfterSend) {
        removeClient(client->fd);
    }
}

std::string WebSocketServer::computeAcceptKey(const std::string& clientKey) {
    std::string combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1 sha;
    sha.update(combined);
    return base64Encode(sha.final_hex());
}
