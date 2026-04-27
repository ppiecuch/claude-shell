#pragma once
#include <string>
#include <functional>
#include <map>

// Unix domain socket IPC for single-instance control.
// The GUI instance listens; CLI commands connect, send a command, get a response.

class IpcController {
public:
    IpcController();
    ~IpcController();

    // Try to become the server (first instance). Returns false if another instance is running.
    bool tryListen();

    // Connect to running instance, send command, return response.
    static std::string sendCommand(const std::string& command, const std::string& args = "");

    // Service pending IPC connections. Call from FLTK event loop.
    void service();

    int listenFd() const { return listenFd_; }

    // Register command handler
    using Handler = std::function<std::string(const std::string& args)>;
    void registerCommand(const std::string& name, Handler handler);

    static std::string socketPath();

private:
    int listenFd_ = -1;
    std::map<std::string, Handler> handlers_;

    void handleClient(int clientFd);
};
