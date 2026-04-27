#pragma once
#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/filename.H>

#include <algorithm>

#include "util/incbin.h"

INCBIN_EXTERN(AppLogo);
INCBIN_EXTERN(KomsoftLogo);

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.1"
#endif
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 1
#endif

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

// Simple widget that draws app logo + version info + komsoft logo.
class InfoHeader : public Fl_Widget {
public:
    static constexpr int HEIGHT = 60;

    InfoHeader(int x, int y, int w)
        : Fl_Widget(x, y, w, HEIGHT)
    {
        // App logo (left side)
        auto* raw = new Fl_PNG_Image("logo.png", gAppLogoData, (int)gAppLogoSize);
        if (!raw->fail()) {
            Fl_RGB_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
            appLogo_ = (Fl_PNG_Image*)raw->copy(50, 50);
            Fl_RGB_Image::RGB_scaling(FL_RGB_SCALING_NEAREST);
        }
        delete raw;

        // Komsoft logo (right side) — scale to fit header
        auto* ksRaw = new Fl_PNG_Image("komsoft.png", gKomsoftLogoData, (int)gKomsoftLogoSize);
        if (!ksRaw->fail()) {
            int targetH = 40;
            int targetW = (int)((float)ksRaw->w() / ksRaw->h() * targetH);
            Fl_RGB_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
            komsoftLogo_ = (Fl_PNG_Image*)ksRaw->copy(targetW, targetH);
            Fl_RGB_Image::RGB_scaling(FL_RGB_SCALING_NEAREST);
        }
        delete ksRaw;
    }

    ~InfoHeader() {
        delete appLogo_;
        delete komsoftLogo_;
    }

protected:
    int handle(int event) override {
        switch (event) {
            case FL_PUSH: {
                int hit = hitTest(Fl::event_x(), Fl::event_y());
                if (hit) { armed_ = hit; return 1; }
                return 0;
            }
            case FL_RELEASE: {
                int hit = hitTest(Fl::event_x(), Fl::event_y());
                if (armed_ && armed_ == hit) {
                    char err[512] = {0};
                    fl_open_uri(armed_ == 1 ? kRepoUrl : kIssuesUrl, err, sizeof(err));
                }
                armed_ = 0;
                return 1;
            }
            case FL_ENTER:
                return 1;
            case FL_MOVE: {
                fl_cursor(hitTest(Fl::event_x(), Fl::event_y()) ? FL_CURSOR_HAND : FL_CURSOR_DEFAULT);
                return 1;
            }
            case FL_LEAVE:
                fl_cursor(FL_CURSOR_DEFAULT);
                return 1;
        }
        return Fl_Widget::handle(event);
    }

    void draw() override {
        fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), FL_BACKGROUND_COLOR);

        int lx = x() + 10;
        int ly = y() + 3;

        // App logo (left)
        if (appLogo_) {
            appLogo_->draw(lx, ly);
            lx += 58;
        }

        // Title + version
        fl_font(FL_HELVETICA_BOLD, 12);
        fl_color(FL_FOREGROUND_COLOR);
        const char* title = "Claude Shell ver. " BUILD_VERSION " (build " STRINGIFY(BUILD_NUMBER) ")";
        fl_draw(title, lx, ly + 14);
        int titleW = (int)fl_width(title);

        // Description
        fl_font(FL_HELVETICA, 11);
        fl_color(FL_DARK3);
        const char* desc = "Claude session sharing manager";
        fl_draw(desc, lx, ly + 30);
        int descW = (int)fl_width(desc);

        // Copyright
        fl_font(FL_HELVETICA, 10);
        fl_color(FL_DARK3);
        const char* copy = "\xC2\xA9 2026 KomSoft Oprogramowanie";
        fl_draw(copy, lx, ly + 44);
        int copyW = (int)fl_width(copy);

        // URLs (middle column, between text block and komsoft logo)
        int leftMax = std::max({titleW, descW, copyW});
        int ux = lx + leftMax + 30;

        // Header line
        fl_font(FL_HELVETICA_BOLD, 11);
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw("Visit us on Github:", ux, ly + 14);

        fl_font(FL_HELVETICA, 11);
        const char* repoLabel = "Repository: ";
        fl_color(FL_DARK3);
        fl_draw(repoLabel, ux, ly + 30);
        int repoLabelW = (int)fl_width(repoLabel);
        int repoUrlW = (int)fl_width(kRepoUrl);
        fl_color(FL_BLUE);
        fl_draw(kRepoUrl, ux + repoLabelW, ly + 30);
        fl_line(ux + repoLabelW, ly + 32, ux + repoLabelW + repoUrlW, ly + 32);
        repoRect_ = {ux + repoLabelW, ly + 18, repoUrlW, 16};

        const char* issuesLabel = "Issues: ";
        fl_color(FL_DARK3);
        fl_draw(issuesLabel, ux, ly + 44);
        int issuesLabelW = (int)fl_width(issuesLabel);
        int issuesUrlW = (int)fl_width(kIssuesUrl);
        fl_color(FL_BLUE);
        fl_draw(kIssuesUrl, ux + issuesLabelW, ly + 44);
        fl_line(ux + issuesLabelW, ly + 46, ux + issuesLabelW + issuesUrlW, ly + 46);
        issuesRect_ = {ux + issuesLabelW, ly + 32, issuesUrlW, 16};

        // Komsoft logo (right)
        if (komsoftLogo_) {
            int rx = x() + w() - komsoftLogo_->w() - 10;
            int ry = y() + (HEIGHT - komsoftLogo_->h()) / 2;
            komsoftLogo_->draw(rx, ry);
        }

        // Bottom divider line
        fl_color(FL_DARK3);
        fl_line(x(), y() + h() - 1, x() + w(), y() + h() - 1);
    }

private:
    struct Rect { int x, y, w, h; };

    int hitTest(int mx, int my) const {
        if (inside(mx, my, repoRect_)) return 1;
        if (inside(mx, my, issuesRect_)) return 2;
        return 0;
    }
    static bool inside(int mx, int my, const Rect& r) {
        return r.w > 0 && mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }

    static constexpr const char* kRepoUrl   = "https://github.com/ppiecuch/claude-shell";
    static constexpr const char* kIssuesUrl = "https://github.com/ppiecuch/claude-shell/issues";

    Fl_PNG_Image* appLogo_ = nullptr;
    Fl_PNG_Image* komsoftLogo_ = nullptr;
    Rect repoRect_{0, 0, 0, 0};
    Rect issuesRect_{0, 0, 0, 0};
    int armed_ = 0;
};
