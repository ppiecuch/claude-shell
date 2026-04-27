#pragma once
#include <FL/Fl_Group.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Button.H>
#include <string>

#include "util/Logger.h"

class LogPanel : public Fl_Group {
public:
    LogPanel(int x, int y, int w, int h);
    ~LogPanel();

    void appendLog(LogLevel level, const std::string& msg);
    void clear();

private:
    Fl_Text_Display* display_;
    Fl_Text_Buffer* textBuf_;
    Fl_Text_Buffer* styleBuf_;
    Fl_Button* clearBtn_;
    Fl_Button* copyBtn_;
    Fl_Button* saveBtn_;
    bool autoScroll_ = true;

    static void onClearClick(Fl_Widget*, void* data);
    static void onCopyClick(Fl_Widget*, void* data);
    static void onSaveClick(Fl_Widget*, void* data);

    // Style table for color-coded log levels
    static Fl_Text_Display::Style_Table_Entry styleTable_[];
};
