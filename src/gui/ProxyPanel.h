#pragma once
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>
#include <string>
#include <functional>

class ProxyServer;

class ProxyPanel : public Fl_Group {
public:
    ProxyPanel(int x, int y, int w, int h);

    void setProxyServer(ProxyServer* server) { server_ = server; }
    void setFrontendPath(const std::string& path) { frontendPath_ = path; }
    void updateStatus();

    std::string claudeBinaryPath() const;

    using ToggleServerCb = std::function<void()>;
    void setOnToggleServer(ToggleServerCb cb) { onToggleServer_ = std::move(cb); }

    using CopyUrlCb = std::function<void(const std::string& url)>;
    void setOnCopyUrl(CopyUrlCb cb) { onCopyUrl_ = std::move(cb); }

private:
    ProxyServer* server_ = nullptr;
    std::string frontendPath_;
    bool alwaysUseSafari_ = false;

    Fl_Button* toggleBtn_;
    Fl_Button* copyUrlBtn_;
    Fl_Button* openTestBtn_;
    Fl_Output* urlOutput_;
    Fl_Input* claudePathInput_;
    Fl_Box* statusLabel_;
    std::string autoDetectedPath_;

    void loadClaudePath();
    void saveClaudePath();

    ToggleServerCb onToggleServer_;
    CopyUrlCb onCopyUrl_;

    static void onToggleClick(Fl_Widget*, void* data);
    static void onCopyUrlClick(Fl_Widget*, void* data);
    static void onOpenTestClick(Fl_Widget*, void* data);
};
