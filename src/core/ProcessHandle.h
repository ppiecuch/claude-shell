#pragma once
#include <string>
#include <vector>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

class ProcessHandle {
public:
    static std::unique_ptr<ProcessHandle> spawn(
        const std::string& executable,
        const std::vector<std::string>& args,
        const std::string& workingDir = "");

    ~ProcessHandle();

    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

#ifdef _WIN32
    using PidType = DWORD;
#else
    using PidType = pid_t;
#endif

    int stdinFd() const { return stdinFd_; }
    int stdoutFd() const { return stdoutFd_; }
    int stderrFd() const { return stderrFd_; }
    PidType pid() const { return pid_; }

    bool isRunning() const;
    int exitCode() const;

    ssize_t readStdout(char* buf, size_t maxLen);
    ssize_t readStderr(char* buf, size_t maxLen);
    ssize_t writeStdin(const char* data, size_t len);

    void closeStdin();
    void terminate();
    void kill();

private:
    ProcessHandle() = default;

    PidType pid_ = 0;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
    int stderrFd_ = -1;
    mutable int exitCode_ = -1;
    mutable bool exited_ = false;
};
