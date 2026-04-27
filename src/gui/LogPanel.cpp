#include "gui/LogPanel.h"
#include <FL/Fl.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <ctime>
#include <fstream>

// Style table: A=Debug(grey), B=Info(black), C=Warn(yellow/dark), D=Error(red)
Fl_Text_Display::Style_Table_Entry LogPanel::styleTable_[] = {
    { FL_DARK3,       FL_COURIER, 12 },  // A - Debug
    { FL_BLACK,       FL_COURIER, 12 },  // B - Info
    { FL_DARK_YELLOW, FL_COURIER, 12 },  // C - Warn
    { FL_RED,         FL_COURIER, 12 },  // D - Error
};

LogPanel::LogPanel(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    int btnH = 30;
    int btnY = y + h - btnH - 5;

    textBuf_ = new Fl_Text_Buffer();
    styleBuf_ = new Fl_Text_Buffer();

    display_ = new Fl_Text_Display(x + 5, y + 5, w - 10, h - btnH - 15);
    display_->buffer(textBuf_);
    display_->highlight_data(styleBuf_, styleTable_, 4, 'A', nullptr, nullptr);
    display_->textfont(FL_COURIER);
    display_->textsize(12);

    clearBtn_ = new Fl_Button(x + 5, btnY, 90, btnH, "@1+  Clear");
    clearBtn_->callback(onClearClick, this);

    copyBtn_ = new Fl_Button(x + 100, btnY, 90, btnH, "@menu  Copy");
    copyBtn_->callback(onCopyClick, this);

    saveBtn_ = new Fl_Button(x + 195, btnY, 90, btnH, "@filesave  Save");
    saveBtn_->callback(onSaveClick, this);

    end();
    resizable(display_);
}

LogPanel::~LogPanel() {
}

void LogPanel::appendLog(LogLevel level, const std::string& msg) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    const char* levelStr = "";
    char styleChar = 'B';
    switch (level) {
        case LogLevel::Debug: levelStr = "DBG"; styleChar = 'A'; break;
        case LogLevel::Info:  levelStr = "INF"; styleChar = 'B'; break;
        case LogLevel::Warn:  levelStr = "WRN"; styleChar = 'C'; break;
        case LogLevel::Error: levelStr = "ERR"; styleChar = 'D'; break;
    }

    std::string line = std::string(ts) + " [" + levelStr + "] " + msg + "\n";
    std::string style(line.size(), styleChar);

    textBuf_->append(line.c_str());
    styleBuf_->append(style.c_str());

    if (autoScroll_) {
        display_->scroll(textBuf_->count_lines(0, textBuf_->length()), 0);
    }
}

void LogPanel::clear() {
    textBuf_->text("");
    styleBuf_->text("");
}

void LogPanel::onClearClick(Fl_Widget*, void* data) {
    ((LogPanel*)data)->clear();
}

void LogPanel::onCopyClick(Fl_Widget*, void* data) {
    auto* panel = (LogPanel*)data;
    const char* text = panel->textBuf_->text();
    if (text && text[0]) {
        Fl::copy(text, (int)strlen(text), 1);
    }
    free((void*)text);
}

void LogPanel::onSaveClick(Fl_Widget*, void* data) {
    auto* panel = (LogPanel*)data;

    Fl_Native_File_Chooser chooser;
    chooser.title("Save Log");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.filter("Log Files\t*.log\nText Files\t*.txt");
    chooser.preset_file("claude-shell.log");

    if (chooser.show() == 0) {
        const char* path = chooser.filename();
        const char* text = panel->textBuf_->text();
        if (path && text) {
            std::ofstream f(path);
            if (f.is_open()) {
                f << text;
                f.close();
            }
        }
        free((void*)text);
    }
}
