#pragma once
#include <string>
#include <cstdint>

struct SessionInfo {
    int pid = 0;
    std::string sessionId;
    std::string cwd;
    int64_t startedAt = 0;
    std::string kind;
    std::string entrypoint;
    std::string name;
    bool alive = false;
};
