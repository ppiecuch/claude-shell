#pragma once
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl.H>
#include "gui/QrCodeWidget.h"
#include <string>
#include <functional>

// Modal dialog showing tunnel details: URL, QR code, and process logs
class TunnelDetailsDialog {
public:
    struct TunnelInfo {
        std::string url;
        std::string state;
        std::string logs;
        int port;
    };

    using RefreshFn = std::function<TunnelInfo()>;

    static void show(const std::string& provider, const TunnelInfo& info,
                     RefreshFn refreshFn) {
        struct DialogData {
            RefreshFn refreshFn;
            Fl_Double_Window* win;
            Fl_Box* statusBox;
            Fl_Output* urlOutput;
            Fl_Button* copyUrlBtn;
            QrCodeWidget* qrWidget;
            Fl_Text_Buffer* logBuf;
            Fl_Text_Display* logDisplay;
            Fl_Tabs* tabs;
            Fl_Group* qrGroup;
            Fl_Group* logGroup;
            std::string currentUrl;
            std::string currentLogs;
            int port;

            void applyInfo(const TunnelInfo& ti) {
                port = ti.port;
                currentUrl = ti.url;
                currentLogs = ti.logs;
                bool hasUrl = !ti.url.empty();

                // Status
                std::string statusText = "Status: " + ti.state
                    + "  |  Port: " + std::to_string(ti.port);
                statusBox->copy_label(statusText.c_str());

                // URL
                if (hasUrl) {
                    urlOutput->value(ti.url.c_str());
                    urlOutput->textcolor(FL_FOREGROUND_COLOR);
                    copyUrlBtn->activate();
                } else {
                    urlOutput->value("(not available)");
                    urlOutput->textcolor(FL_INACTIVE_COLOR);
                    copyUrlBtn->deactivate();
                }

                // QR code
                if (hasUrl) {
                    qrWidget->setText(ti.url);
                } else {
                    qrWidget->setText("");
                }

                // Logs
                logBuf->text(ti.logs.c_str());
                int totalLines = logBuf->count_lines(0, logBuf->length());
                logDisplay->scroll(totalLines, 0);

                // Switch to appropriate default tab if state changed
                // (only auto-switch if currently on a tab that became irrelevant)
                if (!hasUrl && tabs->value() == qrGroup) {
                    tabs->value(logGroup);
                }

                win->redraw();
            }
        };

        auto* data = new DialogData();
        data->refreshFn = std::move(refreshFn);
        data->currentUrl = info.url;
        data->currentLogs = info.logs;
        data->port = info.port;

        bool hasUrl = !info.url.empty();

        std::string title = provider + " Tunnel Details";
        auto* win = new Fl_Double_Window(450, 595, "");
        win->copy_label(title.c_str());
        win->set_modal();
        data->win = win;

        int ly = 10;

        // Status line
        data->statusBox = new Fl_Box(10, ly, 430, 25);
        std::string statusText = "Status: " + info.state
            + "  |  Port: " + std::to_string(info.port);
        data->statusBox->copy_label(statusText.c_str());
        data->statusBox->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        data->statusBox->labelfont(FL_HELVETICA);
        data->statusBox->labelsize(12);
        ly += 28;

        // URL row
        new Fl_Box(10, ly, 35, 25, "URL:");
        data->urlOutput = new Fl_Output(48, ly, 345, 25);
        data->urlOutput->textfont(FL_COURIER);
        data->urlOutput->textsize(12);
        if (hasUrl) {
            data->urlOutput->value(info.url.c_str());
        } else {
            data->urlOutput->value("(not available)");
            data->urlOutput->textcolor(FL_INACTIVE_COLOR);
        }

        data->copyUrlBtn = new Fl_Button(398, ly, 42, 25, "Copy");
        data->copyUrlBtn->callback([](Fl_Widget*, void* d) {
            auto* dd = (DialogData*)d;
            if (!dd->currentUrl.empty())
                Fl::copy(dd->currentUrl.c_str(), (int)dd->currentUrl.size(), 1);
        }, data);
        if (!hasUrl) data->copyUrlBtn->deactivate();
        ly += 30;

        // Tabs
        data->tabs = new Fl_Tabs(10, ly, 430, 480);
        int tabContentY = ly + 25;
        int tabContentH = 480 - 25;

        // --- QR Code tab ---
        data->qrGroup = new Fl_Group(10, tabContentY, 430, tabContentH, "QR Code");
        {
            int qrSize = std::min(380, tabContentH - 10);
            int qrX = 10 + (430 - qrSize) / 2;
            int qrY = tabContentY + 5;
            data->qrWidget = new QrCodeWidget(qrX, qrY, qrSize, qrSize);
            if (hasUrl) data->qrWidget->setText(info.url);
        }
        data->qrGroup->end();

        // --- Logs tab ---
        data->logGroup = new Fl_Group(10, tabContentY, 430, tabContentH, "Logs");
        {
            data->logBuf = new Fl_Text_Buffer();
            data->logBuf->text(info.logs.c_str());

            data->logDisplay = new Fl_Text_Display(15, tabContentY + 5, 420, tabContentH - 10);
            data->logDisplay->buffer(data->logBuf);
            data->logDisplay->textfont(FL_COURIER);
            data->logDisplay->textsize(11);
            data->logDisplay->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

            int totalLines = data->logBuf->count_lines(0, data->logBuf->length());
            data->logDisplay->scroll(totalLines, 0);
        }
        data->logGroup->end();

        data->tabs->value(hasUrl ? data->qrGroup : data->logGroup);
        data->tabs->end();
        ly += 480;

        // Bottom buttons: Copy Logs | Refresh | Close
        int btnY = ly + 5;
        auto* copyLogsBtn = new Fl_Button(80, btnY, 100, 28, "Copy Logs");
        copyLogsBtn->callback([](Fl_Widget*, void* d) {
            auto* dd = (DialogData*)d;
            Fl::copy(dd->currentLogs.c_str(), (int)dd->currentLogs.size(), 1);
        }, data);

        auto* refreshBtn = new Fl_Button(185, btnY, 80, 28, "@reload  Refresh");
        refreshBtn->callback([](Fl_Widget*, void* d) {
            auto* dd = (DialogData*)d;
            if (dd->refreshFn) {
                auto fresh = dd->refreshFn();
                dd->applyInfo(fresh);
            }
        }, data);

        auto* closeBtn = new Fl_Return_Button(270, btnY, 100, 28, "Close");
        closeBtn->callback([](Fl_Widget*, void* d) {
            auto* dd = (DialogData*)d;
            dd->win->hide();
            delete dd;
        }, data);

        win->end();
        win->resizable(data->tabs);
        win->size_range(350, 400);
        win->show();
    }
};
