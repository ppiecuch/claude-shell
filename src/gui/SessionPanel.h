#pragma once
#include <FL/Fl_Group.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>
#include <string>
#include <vector>
#include <functional>

#include "core/SessionInfo.h"

class SessionPanel : public Fl_Group {
public:
    SessionPanel(int x, int y, int w, int h);

    void updateSessions(const std::vector<SessionInfo>& sessions);

    using RefreshCb = std::function<void()>;
    void setOnRefresh(RefreshCb cb) { onRefresh_ = std::move(cb); }

private:
    class SessionTable : public Fl_Table_Row {
    public:
        SessionTable(int x, int y, int w, int h);
        void setSessions(const std::vector<SessionInfo>& sessions);
        const std::vector<SessionInfo>& sessions() const { return sessions_; }
        void autoFitColumns();

    protected:
        void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) override;
        void resize(int x, int y, int w, int h) override;

    private:
        std::vector<SessionInfo> sessions_;
        static constexpr int NUM_COLS = 5;
        static constexpr float colRatios_[NUM_COLS] = {0.08f, 0.17f, 0.45f, 0.15f, 0.15f};
    };

    SessionTable* table_;
    Fl_Button* refreshBtn_;
    RefreshCb onRefresh_;

    static void onRefreshClick(Fl_Widget*, void* data);
};
