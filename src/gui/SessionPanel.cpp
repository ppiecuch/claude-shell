#include "gui/SessionPanel.h"
#include <FL/Fl.H>
#include <ctime>

// --- SessionTable ---

SessionPanel::SessionTable::SessionTable(int x, int y, int w, int h)
    : Fl_Table_Row(x, y, w, h)
{
    cols(NUM_COLS);
    col_header(1);
    col_resize(1);
    row_header(0);
    type(SELECT_SINGLE);
    autoFitColumns();
    end();
}

void SessionPanel::SessionTable::autoFitColumns() {
    int totalW = w() - 4;
    for (int i = 0; i < NUM_COLS; i++)
        col_width(i, (int)(totalW * colRatios_[i]));
}

void SessionPanel::SessionTable::resize(int rx, int ry, int rw, int rh) {
    Fl_Table_Row::resize(rx, ry, rw, rh);
    autoFitColumns();
}

void SessionPanel::SessionTable::setSessions(const std::vector<SessionInfo>& sessions) {
    sessions_ = sessions;
    rows((int)sessions_.size());
    redraw();
}

void SessionPanel::SessionTable::draw_cell(TableContext context, int R, int C,
                                            int X, int Y, int W, int H) {
    static const char* headers[] = {"PID", "Name", "CWD", "Started", "Status"};

    switch (context) {
        case CONTEXT_COL_HEADER:
            fl_push_clip(X, Y, W, H);
            fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, col_header_color());
            fl_color(FL_BLACK);
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_draw(headers[C], X + 4, Y, W - 8, H, FL_ALIGN_LEFT);
            fl_pop_clip();
            break;

        case CONTEXT_CELL: {
            if (R < 0 || R >= (int)sessions_.size()) break;
            auto& s = sessions_[R];

            Fl_Color bg = row_selected(R) ? FL_SELECTION_COLOR : FL_WHITE;
            fl_push_clip(X, Y, W, H);
            fl_draw_box(FL_FLAT_BOX, X, Y, W, H, bg);
            fl_color(row_selected(R) ? FL_WHITE : FL_BLACK);
            fl_font(FL_HELVETICA, 12);

            std::string text;
            switch (C) {
                case 0: text = std::to_string(s.pid); break;
                case 1: text = s.name.empty() ? "(unnamed)" : s.name; break;
                case 2: text = s.cwd; break;
                case 3: {
                    time_t t = s.startedAt / 1000;
                    struct tm tm;
                    localtime_r(&t, &tm);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%d/%m %H:%M", &tm);
                    text = buf;
                    break;
                }
                case 4:
                    text = s.alive ? "alive" : "dead";
                    if (!s.alive) fl_color(FL_RED);
                    break;
            }

            fl_draw(text.c_str(), X + 4, Y, W - 8, H, FL_ALIGN_LEFT);
            fl_pop_clip();
            break;
        }

        default:
            break;
    }
}

// --- SessionPanel ---

SessionPanel::SessionPanel(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    int btnH = 30;
    int btnY = y + h - btnH - 5;

    table_ = new SessionTable(x + 5, y + 5, w - 10, h - btnH - 15);

    refreshBtn_ = new Fl_Button(x + 5, btnY, 110, btnH, "@reload  Refresh");
    refreshBtn_->callback(onRefreshClick, this);

    end();
    resizable(table_);
}

void SessionPanel::updateSessions(const std::vector<SessionInfo>& sessions) {
    table_->setSessions(sessions);
}

void SessionPanel::onRefreshClick(Fl_Widget*, void* data) {
    auto* panel = (SessionPanel*)data;
    if (panel->onRefresh_) panel->onRefresh_();
}
