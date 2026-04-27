#include "gui/ProxyPanel.h"
#include "core/ProxyServer.h"
#include "util/Platform.h"
#include <FL/Fl.H>
#include <FL/Fl_Preferences.H>
#include <FL/fl_ask.H>
#include <cstdlib>

#ifdef __APPLE__
#endif

ProxyPanel::ProxyPanel(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    int btnH = 30;
    int ly = y + 20;

    // Server status label
    statusLabel_ = new Fl_Box(x + 10, ly, w - 20, 25);
    statusLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    statusLabel_->labelfont(FL_HELVETICA);
    statusLabel_->labelsize(13);
    statusLabel_->copy_label("Server: stopped");
    ly += 35;

    // URL display
    new Fl_Box(x + 10, ly, 30, 25, "URL:");
    urlOutput_ = new Fl_Output(x + 45, ly, w - 55, 25);
    urlOutput_->textfont(FL_COURIER);
    urlOutput_->textsize(12);
    ly += 30;

    // Claude binary path
    new Fl_Box(x + 10, ly, 45, 25, "Claude:");
    claudePathInput_ = new Fl_Input(x + 60, ly, w - 70, 25);
    claudePathInput_->textfont(FL_COURIER);
    claudePathInput_->textsize(12);
    claudePathInput_->tooltip("Path to claude CLI binary (auto-detected, editable)");
    claudePathInput_->callback([](Fl_Widget*, void* data) {
        ((ProxyPanel*)data)->saveClaudePath();
    }, this);
    claudePathInput_->when(FL_WHEN_CHANGED);

    // Auto-detect and load saved preference
    autoDetectedPath_ = Platform::findExecutable("claude");
    loadClaudePath();
    ly += 35;

    // Buttons
    int bx = x + 10;
    toggleBtn_ = new Fl_Button(bx, ly, 130, btnH, "@> Start Server");
    toggleBtn_->callback(onToggleClick, this);
    bx += 135;

    copyUrlBtn_ = new Fl_Button(bx, ly, 130, btnH, "@menu  Copy URL");
    copyUrlBtn_->callback(onCopyUrlClick, this);
    bx += 135;

    openTestBtn_ = new Fl_Button(bx, ly, 155, btnH, "@->  Open Test Client");
    openTestBtn_->callback(onOpenTestClick, this);

    // Disable server-dependent buttons initially
    copyUrlBtn_->deactivate();
    openTestBtn_->deactivate();

    end();
}

void ProxyPanel::updateStatus() {
    if (!server_) {
        statusLabel_->copy_label("Server: not initialized");
        urlOutput_->value("");
        toggleBtn_->copy_label("@>  Start Server");
        openTestBtn_->deactivate();
        copyUrlBtn_->deactivate();
        return;
    }

    auto state = server_->state();
    if (state == ProxyServer::State::Running) {
        static char buf[128];
        snprintf(buf, sizeof(buf), "Server: running on port %d  |  %zu client(s) connected",
                 server_->port(), server_->clientCount());
        statusLabel_->copy_label(buf);
        urlOutput_->value(server_->connectionString().c_str());
        toggleBtn_->copy_label("@square  Stop Server");
        openTestBtn_->activate();
        copyUrlBtn_->activate();
    } else {
        statusLabel_->copy_label("Server: stopped");
        urlOutput_->value("");
        toggleBtn_->copy_label("@>  Start Server");
        openTestBtn_->deactivate();
        copyUrlBtn_->deactivate();
    }
}

void ProxyPanel::onToggleClick(Fl_Widget*, void* data) {
    auto* panel = (ProxyPanel*)data;
    if (panel->onToggleServer_) panel->onToggleServer_();
}

void ProxyPanel::onCopyUrlClick(Fl_Widget*, void* data) {
    auto* panel = (ProxyPanel*)data;
    if (!panel->server_ || panel->server_->state() != ProxyServer::State::Running) return;
    if (panel->onCopyUrl_) panel->onCopyUrl_(panel->server_->connectionString());
}

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>

static bool isDefaultBrowserSafari() {
    CFURLRef url = CFURLCreateWithString(nullptr, CFSTR("https://example.com"), nullptr);
    if (!url) return false;
    CFURLRef browserUrl = LSCopyDefaultApplicationURLForURL(url, kLSRolesViewer, nullptr);
    CFRelease(url);
    if (!browserUrl) return false;

    CFStringRef path = CFURLCopyPath(browserUrl);
    CFRelease(browserUrl);
    if (!path) return false;

    char buf[512];
    CFStringGetCString(path, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(path);

    return std::string(buf).find("Safari") != std::string::npos;
}

static void openUrlWithSafari(const std::string& url) {
    std::string cmd = "open -a Safari '" + url + "'";
    system(cmd.c_str());
}

static void openUrlDefault(const std::string& url) {
    std::string cmd = "open '" + url + "'";
    system(cmd.c_str());
}
#endif

void ProxyPanel::onOpenTestClick(Fl_Widget*, void* data) {
    auto* panel = (ProxyPanel*)data;
    if (!panel->server_ || panel->server_->state() != ProxyServer::State::Running) {
        fl_alert("Start the server first.");
        return;
    }

    std::string frontendUrl = "http://localhost:" + std::to_string(panel->server_->port())
        + "/?token=" + panel->server_->authToken();

#ifdef __APPLE__
    if (!isDefaultBrowserSafari() && !panel->alwaysUseSafari_) {
        int choice = fl_choice(
            "Your default browser is not Safari.\n"
            "Open with Safari instead of default browser?",
            "Default Browser", "Safari", "Always Safari");
        if (choice == 2) {
            panel->alwaysUseSafari_ = true;
            openUrlWithSafari(frontendUrl);
            return;
        } else if (choice == 1) {
            openUrlWithSafari(frontendUrl);
            return;
        }
    }
    if (panel->alwaysUseSafari_) {
        openUrlWithSafari(frontendUrl);
    } else {
        openUrlDefault(frontendUrl);
    }
#else
    std::string cmd = "xdg-open '" + frontendUrl + "' 2>/dev/null || start '" + frontendUrl + "'";
    system(cmd.c_str());
#endif
}

std::string ProxyPanel::claudeBinaryPath() const {
    const char* val = claudePathInput_->value();
    return (val && val[0]) ? std::string(val) : autoDetectedPath_;
}

void ProxyPanel::loadClaudePath() {
    Fl_Preferences prefs(Fl_Preferences::USER_L, "com.komsoft.claudeshell", "settings");
    char buf[1024] = {};
    prefs.get("claude_binary_path", buf, "", sizeof(buf));

    if (buf[0] && std::string(buf) != autoDetectedPath_) {
        // User had a custom path saved
        claudePathInput_->value(buf);
    } else {
        claudePathInput_->value(autoDetectedPath_.c_str());
    }
}

void ProxyPanel::saveClaudePath() {
    std::string current = claudePathInput_->value();
    Fl_Preferences prefs(Fl_Preferences::USER_L, "com.komsoft.claudeshell", "settings");

    if (current != autoDetectedPath_ && !current.empty()) {
        prefs.set("claude_binary_path", current.c_str());
    } else {
        prefs.deleteEntry("claude_binary_path");
    }
}
