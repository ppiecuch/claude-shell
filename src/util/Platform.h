#pragma once
#include <string>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#endif

namespace Platform {

inline std::string homeDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path) == S_OK)
        return std::string(path);
    return std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "C:\\";
#else
    const char* home = std::getenv("HOME");
    if (home) return home;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
#endif
}

inline std::string claudeSessionsDir() {
    return homeDir() + "/.claude/sessions";
}

inline bool isProcessAlive(int pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exitCode;
    bool alive = GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return kill(pid, 0) == 0;
#endif
}

// Check if path is a regular file and executable
inline bool isExecutable(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;    // must be a regular file
    return access(path.c_str(), X_OK) == 0;     // must be executable
}

inline std::string findExecutable(const std::string& name) {
    std::string home = homeDir();
    std::vector<std::string> extraDirs = {
        home + "/bin",
        home + "/.local/bin",
        "/usr/local/bin",
        "/opt/homebrew/bin",
        "/opt/local/bin",
    };

    // Check extra dirs first (app bundles may have limited PATH)
    for (auto& dir : extraDirs) {
        std::string full = dir + "/" + name;
        if (isExecutable(full)) return full;
    }

    // Fall back to which/where
    std::string cmd;
#ifdef _WIN32
    cmd = "where " + name + " 2>nul";
#else
    cmd = "which " + name + " 2>/dev/null";
#endif
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[512];
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(pipe);

    // Validate the result from which/where
    if (!isExecutable(result)) return "";
    return result;
}

} // namespace Platform
