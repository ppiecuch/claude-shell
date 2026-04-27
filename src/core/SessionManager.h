#pragma once
#include "core/SessionInfo.h"
#include <vector>
#include <string>
#include <functional>

class SessionManager {
public:
    SessionManager();

    void refresh();
    const std::vector<SessionInfo>& sessions() const { return sessions_; }

    using OnChangeCallback = std::function<void()>;
    void setOnChange(OnChangeCallback cb) { onChange_ = std::move(cb); }

private:
    std::string sessionsDir_;
    std::vector<SessionInfo> sessions_;
    OnChangeCallback onChange_;

    SessionInfo parseSessionFile(const std::string& path) const;
};
