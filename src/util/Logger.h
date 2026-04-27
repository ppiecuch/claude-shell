#pragma once
#include <string>
#include <functional>
#include <deque>
#include <mutex>
#include <cstdarg>
#include <cstdio>

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    using Callback = std::function<void(LogLevel, const std::string&)>;
    void setCallback(Callback cb) { callback_ = std::move(cb); }

    void log(LogLevel level, const char* fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        std::string msg(buf);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            history_.push_back({level, msg});
            if (history_.size() > 5000) history_.pop_front();
        }
        if (callback_) callback_(level, msg);
    }

    const std::deque<std::pair<LogLevel, std::string>>& history() const { return history_; }

private:
    Logger() = default;
    Callback callback_;
    std::deque<std::pair<LogLevel, std::string>> history_;
    std::mutex mutex_;
};

#define LOG_DEBUG(...) Logger::instance().log(LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  Logger::instance().log(LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  Logger::instance().log(LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) Logger::instance().log(LogLevel::Error, __VA_ARGS__)
