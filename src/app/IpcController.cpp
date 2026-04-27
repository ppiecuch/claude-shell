#include "app/IpcController.h"
#include "util/Logger.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>

IpcController::IpcController() = default;

IpcController::~IpcController() {
    if (listenFd_ >= 0) {
        close(listenFd_);
        unlink(socketPath().c_str());
    }
}

std::string IpcController::socketPath() {
    std::string path = "/tmp/claude-shell-";
    if (const char* user = getenv("USER")) path += user;
    path += ".sock";
    return path;
}

bool IpcController::tryListen() {
    std::string path = socketPath();

    // Try connecting first to see if another instance is running
    int testFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (testFd >= 0) {
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(testFd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(testFd);
            return false; // Another instance is running
        }
        close(testFd);
    }

    // Remove stale socket
    unlink(path.c_str());

    listenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) return false;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (listen(listenFd_, 5) < 0) {
        close(listenFd_);
        unlink(path.c_str());
        listenFd_ = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(listenFd_, F_GETFL, 0);
    if (flags >= 0) fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);

    return true;
}

std::string IpcController::sendCommand(const std::string& command, const std::string& args) {
    std::string path = socketPath();

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "error: socket failed";

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return "error: no running instance found";
    }

    // Send: "command\nargs\n"
    std::string msg = command + "\n" + args + "\n";
    write(fd, msg.data(), msg.size());

    // Shutdown write side
    shutdown(fd, SHUT_WR);

    // Read response
    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        response.append(buf, n);
    }

    close(fd);
    return response;
}

void IpcController::service() {
    if (listenFd_ < 0) return;

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    int clientFd = accept(listenFd_, (struct sockaddr*)&addr, &len);
    if (clientFd < 0) return;

    handleClient(clientFd);
}

void IpcController::handleClient(int clientFd) {
    // Read command
    std::string data;
    char buf[4096];
    ssize_t n;

    // Set a short timeout for reading
    struct timeval tv = {1, 0};
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while ((n = read(clientFd, buf, sizeof(buf))) > 0) {
        data.append(buf, n);
    }

    // Parse "command\nargs\n"
    std::string command, args;
    size_t pos = data.find('\n');
    if (pos != std::string::npos) {
        command = data.substr(0, pos);
        args = data.substr(pos + 1);
        // Trim trailing newline from args
        while (!args.empty() && args.back() == '\n') args.pop_back();
    } else {
        command = data;
    }

    // Find and execute handler
    std::string response;
    auto it = handlers_.find(command);
    if (it != handlers_.end()) {
        response = it->second(args);
    } else {
        response = "error: unknown command '" + command + "'";
    }

    // Send response
    write(clientFd, response.data(), response.size());
    close(clientFd);
}

void IpcController::registerCommand(const std::string& name, Handler handler) {
    handlers_[name] = std::move(handler);
}
