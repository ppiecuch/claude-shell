#pragma once
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>
#include "util/qrcodegen.hpp"
#include <string>

// Simple FLTK widget that renders a QR code from a text string
class QrCodeWidget : public Fl_Widget {
public:
    QrCodeWidget(int x, int y, int w, int h)
        : Fl_Widget(x, y, w, h) {}

    void setText(const std::string& text) {
        text_ = text;
        valid_ = false;
        if (!text.empty()) {
            try {
                qr_ = qrcodegen::QrCode::encodeText(
                    text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
                valid_ = true;
            } catch (...) {
                valid_ = false;
            }
        }
        redraw();
    }

protected:
    void draw() override {
        // Clear background
        fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), FL_WHITE);

        if (!valid_ || text_.empty()) {
            fl_color(FL_INACTIVE_COLOR);
            fl_font(FL_HELVETICA, 12);
            fl_draw("No QR code", x(), y(), w(), h(), FL_ALIGN_CENTER);
            return;
        }

        int qrSize = qr_.getSize();
        int quietZone = 2;  // modules of white border
        int totalModules = qrSize + quietZone * 2;

        // Scale to fit widget, keeping square
        int side = std::min(w(), h());
        int moduleSize = side / totalModules;
        if (moduleSize < 2) moduleSize = 2;

        int totalPx = moduleSize * totalModules;
        int ox = x() + (w() - totalPx) / 2;
        int oy = y() + (h() - totalPx) / 2;

        // Draw modules
        for (int my = 0; my < qrSize; my++) {
            for (int mx = 0; mx < qrSize; mx++) {
                if (qr_.getModule(mx, my)) {
                    int px = ox + (mx + quietZone) * moduleSize;
                    int py = oy + (my + quietZone) * moduleSize;
                    fl_color(FL_BLACK);
                    fl_rectf(px, py, moduleSize, moduleSize);
                }
            }
        }
    }

private:
    std::string text_;
    qrcodegen::QrCode qr_ = qrcodegen::QrCode::encodeText("", qrcodegen::QrCode::Ecc::LOW);
    bool valid_ = false;
};
