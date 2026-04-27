#pragma once
#include <FL/Fl_Group.H>
#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>
#include <string>
#include <vector>
#include <functional>

class TunnelManager;

class TunnelPanel : public Fl_Group {
public:
    TunnelPanel(int x, int y, int w, int h);

    void setTunnelManager(TunnelManager* tm);
    void updateTunnels();
    void setServerPort(int port);
    int getPort() const;

    // Callbacks set by Application
    using ActionCb = std::function<void(int tunnelId)>;
    using AddCb = std::function<void(const std::string& provider, int port)>;
    using CopyUrlCb = std::function<void(const std::string& url)>;

    void setOnAdd(AddCb cb) { onAdd_ = std::move(cb); }
    void setOnStart(ActionCb cb) { onStart_ = std::move(cb); }
    void setOnStop(ActionCb cb) { onStop_ = std::move(cb); }
    void setOnRestart(ActionCb cb) { onRestart_ = std::move(cb); }
    void setOnRemove(ActionCb cb) { onRemove_ = std::move(cb); }
    void setOnDetails(ActionCb cb) { onDetails_ = std::move(cb); }
    void setOnCopyUrl(CopyUrlCb cb) { onCopyUrl_ = std::move(cb); }

private:
    class TunnelTable : public Fl_Table_Row {
    public:
        TunnelTable(int x, int y, int w, int h, TunnelPanel* panel);

        struct TunnelRow {
            std::string provider;
            int localPort;
            std::string publicUrl;
            std::string state;
            int tunnelId;
            bool canStart;    // Idle or Stopped or Failed
            bool canStop;     // Starting or Connected
        };

        void setData(const std::vector<TunnelRow>& data);
        const std::vector<TunnelRow>& data() const { return rows_; }
        void autoFitColumns();

    protected:
        void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) override;
        void resize(int x, int y, int w, int h) override;
        int handle(int event) override;

    private:
        std::vector<TunnelRow> rows_;
        TunnelPanel* panel_;
        static constexpr int NUM_COLS = 5;
        // Provider, Port, URL, Status, Actions
        static constexpr float colRatios_[NUM_COLS] = {0.15f, 0.08f, 0.40f, 0.12f, 0.25f};

        // Button hit testing in actions column
        enum ActionBtn { BtnNone, BtnStart, BtnStop, BtnRestart, BtnRemove, BtnDetails };
        ActionBtn hitTestAction(int R, int X, int Y, int cellX, int cellY, int cellW, int cellH) const;
        void drawActionButtons(int R, int X, int Y, int W, int H);
    };

    TunnelTable* table_;
    Fl_Choice* providerChoice_;
    Fl_Input* portInput_;
    Fl_Button* addBtn_;
    TunnelManager* tunnelMgr_ = nullptr;

    AddCb onAdd_;
    ActionCb onStart_;
    ActionCb onStop_;
    ActionCb onRestart_;
    ActionCb onRemove_;
    ActionCb onDetails_;
    CopyUrlCb onCopyUrl_;

    void refreshProviders();

    static void onAddClick(Fl_Widget*, void* data);
    static void onInfoClick(Fl_Widget*, void* data);
    static void onRefreshClick(Fl_Widget*, void* data);
};
