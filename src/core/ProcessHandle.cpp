#include "core/ProcessHandle.h"
#include "util/Logger.h"

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <cstring>

extern char** environ;

static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

std::unique_ptr<ProcessHandle> ProcessHandle::spawn(
    const std::string& executable,
    const std::vector<std::string>& args,
    const std::string& workingDir)
{
#ifndef _WIN32
    int stdinPipe[2], stdoutPipe[2], stderrPipe[2];

    if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
        LOG_ERROR("Failed to create pipes: %s", strerror(errno));
        return nullptr;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdinPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdinPipe[1]);
    posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdoutPipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stderrPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stderrPipe[0]);

    // Build argv
    std::vector<const char*> argv;
    argv.push_back(executable.c_str());
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    // Save and change working directory if needed
    std::string savedCwd;
    if (!workingDir.empty()) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) savedCwd = buf;
        chdir(workingDir.c_str());
    }

    pid_t pid;
    int err = posix_spawnp(&pid, executable.c_str(), &actions, nullptr,
                           const_cast<char* const*>(argv.data()), environ);

    // Restore working directory
    if (!savedCwd.empty()) chdir(savedCwd.c_str());

    posix_spawn_file_actions_destroy(&actions);

    if (err != 0) {
        LOG_ERROR("posix_spawnp failed for '%s': %s", executable.c_str(), strerror(err));
        close(stdinPipe[0]); close(stdinPipe[1]);
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        close(stderrPipe[0]); close(stderrPipe[1]);
        return nullptr;
    }

    // Close child ends in parent
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    // Set non-blocking on read ends
    setNonBlocking(stdoutPipe[0]);
    setNonBlocking(stderrPipe[0]);

    auto handle = std::unique_ptr<ProcessHandle>(new ProcessHandle());
    handle->pid_ = pid;
    handle->stdinFd_ = stdinPipe[1];
    handle->stdoutFd_ = stdoutPipe[0];
    handle->stderrFd_ = stderrPipe[0];

    LOG_INFO("Spawned process '%s' with PID %d", executable.c_str(), pid);
    return handle;
#else
    // Windows implementation placeholder
    LOG_ERROR("ProcessHandle::spawn not implemented on Windows");
    return nullptr;
#endif
}

ProcessHandle::~ProcessHandle() {
    if (pid_ > 0 && isRunning()) {
        terminate();
        // Wait briefly for graceful exit
        for (int i = 0; i < 20 && isRunning(); i++) {
            usleep(100000); // 100ms
        }
        if (isRunning()) {
            kill();
        }
    }
    if (stdinFd_ >= 0) close(stdinFd_);
    if (stdoutFd_ >= 0) close(stdoutFd_);
    if (stderrFd_ >= 0) close(stderrFd_);
}

bool ProcessHandle::isRunning() const {
#ifndef _WIN32
    if (exited_) return false;
    if (pid_ <= 0) return false;
    int status;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) return true; // Still running
    if (result == pid_) {
        exited_ = true;
        if (WIFEXITED(status)) exitCode_ = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exitCode_ = 128 + WTERMSIG(status);
        return false;
    }
    return false;
#else
    return false;
#endif
}

int ProcessHandle::exitCode() const {
    if (!exited_) isRunning(); // Update state
    return exitCode_;
}

ssize_t ProcessHandle::readStdout(char* buf, size_t maxLen) {
    if (stdoutFd_ < 0) return -1;
    ssize_t n = read(stdoutFd_, buf, maxLen);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    return n;
}

ssize_t ProcessHandle::readStderr(char* buf, size_t maxLen) {
    if (stderrFd_ < 0) return -1;
    ssize_t n = read(stderrFd_, buf, maxLen);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    return n;
}

ssize_t ProcessHandle::writeStdin(const char* data, size_t len) {
    if (stdinFd_ < 0) return -1;
    return write(stdinFd_, data, len);
}

void ProcessHandle::closeStdin() {
    if (stdinFd_ >= 0) {
        close(stdinFd_);
        stdinFd_ = -1;
    }
}

void ProcessHandle::terminate() {
#ifndef _WIN32
    if (pid_ > 0) {
        LOG_INFO("Sending SIGTERM to PID %d", pid_);
        ::kill(pid_, SIGTERM);
    }
#endif
}

void ProcessHandle::kill() {
#ifndef _WIN32
    if (pid_ > 0) {
        LOG_WARN("Sending SIGKILL to PID %d", pid_);
        ::kill(pid_, SIGKILL);
    }
#endif
}
