#include "gui/TunnelPanel.h"
#include "core/TunnelManager.h"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Help_View.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Output.H>
#include <FL/fl_ask.H>

// --- TunnelTable ---

TunnelPanel::TunnelTable::TunnelTable(int x, int y, int w, int h, TunnelPanel* panel)
    : Fl_Table_Row(x, y, w, h), panel_(panel)
{
    cols(NUM_COLS);
    col_header(1);
    col_resize(1);
    row_header(0);
    type(SELECT_SINGLE);
    autoFitColumns();
    end();
}

void TunnelPanel::TunnelTable::autoFitColumns() {
    int totalW = w() - 4;
    for (int i = 0; i < NUM_COLS; i++)
        col_width(i, (int)(totalW * colRatios_[i]));
}

void TunnelPanel::TunnelTable::resize(int rx, int ry, int rw, int rh) {
    Fl_Table_Row::resize(rx, ry, rw, rh);
    autoFitColumns();
}

void TunnelPanel::TunnelTable::setData(const std::vector<TunnelRow>& d) {
    rows_ = d;
    Fl_Table_Row::rows((int)rows_.size());
    redraw();
}

// Action button layout constants
static constexpr int BTN_W = 28;
static constexpr int BTN_H = 20;
static constexpr int BTN_GAP = 3;
static constexpr int BTN_PAD = 4;

TunnelPanel::TunnelTable::ActionBtn
TunnelPanel::TunnelTable::hitTestAction(int R, int mx, int my,
                                         int cellX, int cellY, int cellW, int cellH) const {
    if (R < 0 || R >= (int)rows_.size()) return BtnNone;

    auto& r = rows_[R];
    int bx = cellX + BTN_PAD;
    int by = cellY + (cellH - BTN_H) / 2;

    // Layout: Start/Stop | Restart | Remove | Details
    // Start or Stop (mutually exclusive)
    if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) {
        return r.canStart ? BtnStart : (r.canStop ? BtnStop : BtnNone);
    }
    bx += BTN_W + BTN_GAP;

    // Restart (only when running or failed)
    if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) {
        return (r.canStop || r.state == "failed") ? BtnRestart : BtnNone;
    }
    bx += BTN_W + BTN_GAP;

    // Remove
    if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) {
        return BtnRemove;
    }
    bx += BTN_W + BTN_GAP;

    // Details (always available — shows logs even without URL)
    if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) {
        return BtnDetails;
    }

    return BtnNone;
}

void TunnelPanel::TunnelTable::drawActionButtons(int R, int X, int Y, int W, int H) {
    if (R < 0 || R >= (int)rows_.size()) return;
    auto& r = rows_[R];

    int bx = X + BTN_PAD;
    int by = Y + (H - BTN_H) / 2;

    auto drawBtn = [&](const char* label, Fl_Color bg, bool enabled) {
        if (enabled) {
            fl_draw_box(FL_THIN_UP_BOX, bx, by, BTN_W, BTN_H, bg);
            fl_color(FL_BLACK);
        } else {
            fl_draw_box(FL_THIN_UP_BOX, bx, by, BTN_W, BTN_H, FL_BACKGROUND_COLOR);
            fl_color(FL_INACTIVE_COLOR);
        }
        fl_font(FL_HELVETICA, 10);
        fl_draw(label, bx, by, BTN_W, BTN_H, FL_ALIGN_CENTER);
        bx += BTN_W + BTN_GAP;
    };

    // Start or Stop
    if (r.canStart) {
        drawBtn("@>", fl_rgb_color(200, 240, 200), true);   // Green-ish
    } else if (r.canStop) {
        drawBtn("@square", fl_rgb_color(240, 200, 200), true); // Red-ish
    } else {
        drawBtn("-", FL_BACKGROUND_COLOR, false);
    }

    // Restart
    drawBtn("@reload", FL_BACKGROUND_COLOR, r.canStop || r.state == "failed");

    // Remove
    drawBtn("@1+", FL_BACKGROUND_COLOR, true);

    // Details (always enabled — shows URL + logs)
    drawBtn("@menu", FL_BACKGROUND_COLOR, true);
}

void TunnelPanel::TunnelTable::draw_cell(TableContext context, int R, int C,
                                          int X, int Y, int W, int H) {
    static const char* headers[] = {"Provider", "Port", "Public URL", "Status", "Actions"};

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
            if (R < 0 || R >= (int)rows_.size()) break;
            auto& r = rows_[R];

            Fl_Color bg = row_selected(R) ? FL_SELECTION_COLOR : FL_WHITE;
            fl_push_clip(X, Y, W, H);

            if (C == 4) {
                // Actions column — draw buttons
                fl_draw_box(FL_FLAT_BOX, X, Y, W, H, bg);
                drawActionButtons(R, X, Y, W, H);
            } else {
                fl_draw_box(FL_FLAT_BOX, X, Y, W, H, bg);
                fl_font(FL_HELVETICA, 12);

                // Status column gets color coding
                if (C == 3) {
                    if (r.state == "connected")
                        fl_color(fl_rgb_color(0, 140, 0));
                    else if (r.state == "failed")
                        fl_color(FL_RED);
                    else if (r.state == "starting")
                        fl_color(fl_rgb_color(200, 150, 0));
                    else
                        fl_color(row_selected(R) ? FL_WHITE : fl_rgb_color(100, 100, 100));
                } else {
                    fl_color(row_selected(R) ? FL_WHITE : FL_BLACK);
                }

                std::string text;
                switch (C) {
                    case 0: text = r.provider; break;
                    case 1: text = std::to_string(r.localPort); break;
                    case 2: text = r.publicUrl.empty() ? "" : r.publicUrl; break;
                    case 3: text = r.state; break;
                }
                fl_draw(text.c_str(), X + 4, Y, W - 8, H, FL_ALIGN_LEFT);
            }

            fl_pop_clip();
            break;
        }

        default:
            break;
    }
}

int TunnelPanel::TunnelTable::handle(int event) {
    if (event == FL_PUSH) {
        // Check if click is on an action button
        int R, C;
        ResizeFlag rf;
        TableContext ctx = cursor2rowcol(R, C, rf);
        if (ctx == CONTEXT_CELL && C == 4 && R >= 0 && R < (int)rows_.size()) {
            int X, Y, W, H;
            find_cell(CONTEXT_CELL, R, C, X, Y, W, H);
            ActionBtn btn = hitTestAction(R, Fl::event_x(), Fl::event_y(), X, Y, W, H);

            int tid = rows_[R].tunnelId;
            switch (btn) {
                case BtnStart:
                    if (panel_->onStart_) panel_->onStart_(tid);
                    return 1;
                case BtnStop:
                    if (panel_->onStop_) panel_->onStop_(tid);
                    return 1;
                case BtnRestart:
                    if (panel_->onRestart_) panel_->onRestart_(tid);
                    return 1;
                case BtnRemove:
                    if (panel_->onRemove_) panel_->onRemove_(tid);
                    return 1;
                case BtnDetails:
                    if (panel_->onDetails_) panel_->onDetails_(tid);
                    return 1;
                case BtnNone:
                    break;
            }
        }
    }
    return Fl_Table_Row::handle(event);
}

// --- TunnelPanel ---

TunnelPanel::TunnelPanel(int x, int y, int w, int h)
    : Fl_Group(x, y, w, h)
{
    int configH = 35;
    int configY = y + 5;

    // Config row: Provider [dropdown] [refresh] [info] Port: [input] [+]
    int cx = x + 5;
    new Fl_Box(cx, configY + 4, 60, 25, "Provider:");
    cx += 65;
    providerChoice_ = new Fl_Choice(cx, configY + 4, 250, 25);
    cx += 255;
    auto* refreshBtn = new Fl_Button(cx, configY + 4, 25, 25, "@reload");
    refreshBtn->callback(onRefreshClick, this);
    refreshBtn->tooltip("Refresh available providers");
    cx += 30;
    auto* infoLink = new Fl_Button(cx, configY + 4, 100, 25, "\xE2\x93\x98 Setup guide");
    infoLink->box(FL_NO_BOX);
    infoLink->labelfont(FL_HELVETICA);
    infoLink->labelsize(12);
    infoLink->labelcolor(fl_rgb_color(80, 130, 200));
    infoLink->callback(onInfoClick, this);
    infoLink->tooltip("How to install and configure this tunnel provider");
    cx += 105;
    new Fl_Box(cx, configY + 4, 35, 25, "Port:");
    cx += 40;
    portInput_ = new Fl_Input(cx, configY + 4, 60, 25);
    srand((unsigned)time(nullptr));
    int defaultPort = 10000 + (rand() % 50000);
    portInput_->value(std::to_string(defaultPort).c_str());
    portInput_->tooltip("Port for proxy server (auto-set when server starts)");
    cx += 65;
    addBtn_ = new Fl_Button(cx, configY + 4, 30, 25, "+");
    addBtn_->labelfont(FL_HELVETICA_BOLD);
    addBtn_->labelsize(16);
    addBtn_->callback(onAddClick, this);
    addBtn_->tooltip("Add this provider to the tunnel list");

    // Table (fills remaining space)
    int tableY = configY + configH + 5;
    int tableH = h - configH - 15;
    table_ = new TunnelTable(x + 5, tableY, w - 10, tableH, this);

    end();
    resizable(table_);
}

void TunnelPanel::setTunnelManager(TunnelManager* tm) {
    tunnelMgr_ = tm;
    refreshProviders();
}

void TunnelPanel::refreshProviders() {
    if (!tunnelMgr_) return;

    providerChoice_->clear();
    auto allNames = tunnelMgr_->allProviderNames();
    for (auto& name : allNames) {
        auto* prov = tunnelMgr_->provider(name);
        std::string label = name;
        if (prov) {
            auto status = prov->checkStatus();
            label += std::string(" ") + TunnelProvider::statusLabel(status);
        } else {
            label += " (unknown)";
        }
        providerChoice_->add(label.c_str());
    }
    if (allNames.empty()) {
        providerChoice_->add("(no providers)");
    }
    providerChoice_->value(0);
}

void TunnelPanel::onRefreshClick(Fl_Widget*, void* data) {
    auto* panel = (TunnelPanel*)data;
    panel->refreshProviders();
}

void TunnelPanel::onAddClick(Fl_Widget*, void* data) {
    auto* panel = (TunnelPanel*)data;
    if (!panel->tunnelMgr_) return;

    int idx = panel->providerChoice_->value();
    if (idx < 0) return;

    auto names = panel->tunnelMgr_->allProviderNames();
    if (idx >= (int)names.size()) return;

    int port = std::atoi(panel->portInput_->value());
    if (port <= 0) {
        fl_alert("Invalid port number.");
        return;
    }

    if (panel->onAdd_) {
        panel->onAdd_(names[idx], port);
    }
}

void TunnelPanel::updateTunnels() {
    if (!tunnelMgr_) return;

    std::vector<TunnelTable::TunnelRow> rows;
    for (auto* t : tunnelMgr_->allTunnels()) {
        TunnelTable::TunnelRow row;
        row.provider = t->providerName;
        row.localPort = t->localPort;
        row.publicUrl = t->publicUrl;
        row.tunnelId = t->id;
        row.canStart = false;
        row.canStop = false;
        switch (t->state) {
            case TunnelManager::TunnelInstance::Idle:
                row.state = "idle";
                row.canStart = true;
                break;
            case TunnelManager::TunnelInstance::Starting:
                row.state = "starting";
                row.canStop = true;
                break;
            case TunnelManager::TunnelInstance::Connected:
                row.state = "connected";
                row.canStop = true;
                break;
            case TunnelManager::TunnelInstance::Failed:
                row.state = "failed";
                row.canStart = true;
                break;
            case TunnelManager::TunnelInstance::Stopped:
                row.state = "stopped";
                row.canStart = true;
                break;
        }
        rows.push_back(std::move(row));
    }
    table_->setData(rows);
}

int TunnelPanel::getPort() const {
    const char* val = portInput_->value();
    return val ? std::atoi(val) : 0;
}

void TunnelPanel::setServerPort(int port) {
    if (port > 0) {
        portInput_->value(std::to_string(port).c_str());
        portInput_->readonly(1);
        portInput_->color(FL_BACKGROUND_COLOR);
    } else {
        portInput_->readonly(0);
        portInput_->color(FL_BACKGROUND2_COLOR);
    }
}

static void showGuideDialog(const std::string& title, const std::string& html) {
    auto* win = new Fl_Double_Window(500, 420, title.c_str());
    win->set_modal();

    auto* view = new Fl_Help_View(10, 10, 480, 370);
    view->value(html.c_str());
    view->textfont(FL_HELVETICA);
    view->textsize(13);

    auto* closeBtn = new Fl_Return_Button(200, 385, 100, 28, "Close");
    closeBtn->callback([](Fl_Widget*, void* w) {
        ((Fl_Double_Window*)w)->hide();
    }, win);

    win->end();
    win->show();
}

void TunnelPanel::onInfoClick(Fl_Widget*, void* data) {
    auto* panel = (TunnelPanel*)data;
    int idx = panel->providerChoice_->value();
    if (idx < 0 || !panel->tunnelMgr_) return;

    auto names = panel->tunnelMgr_->allProviderNames();
    if (idx >= (int)names.size()) return;
    const std::string& name = names[idx];

    std::string portStr = panel->portInput_->value();
    if (portStr.empty() || portStr == "0") portStr = "&lt;port&gt;";

    std::string html;

    if (name == "ngrok") {
        html =
            "<h2>ngrok &mdash; Secure tunnels to localhost</h2>"
            "<h3>Install (pick one):</h3>"
            "<ul>"
            "<li><code>brew install ngrok</code></li>"
            "<li>Download from: <a href=\"https://ngrok.com/download\">ngrok.com/download</a></li>"
            "</ul>"
            "<h3>Setup:</h3>"
            "<ol>"
            "<li>Sign up at <a href=\"https://ngrok.com\">ngrok.com</a></li>"
            "<li>Copy your auth token from the dashboard</li>"
            "<li>Run: <code>ngrok config add-authtoken YOUR_TOKEN</code></li>"
            "</ol>"
            "<h3>Usage:</h3>"
            "<p><code>ngrok http " + portStr + "</code></p>"
            "<p><i>The tunnel URL will be detected automatically.</i></p>";
    } else if (name == "devtunnel") {
        html =
            "<h2>Microsoft Dev Tunnels</h2>"
            "<h3>Install (pick one):</h3>"
            "<ul>"
            "<li><code>brew install --cask devtunnel</code></li>"
            "<li>Download from: <a href=\"https://aka.ms/devtunnels/cli\">aka.ms/devtunnels/cli</a></li>"
            "</ul>"
            "<h3>Setup:</h3>"
            "<ol>"
            "<li>Run: <code>devtunnel login</code></li>"
            "<li>Sign in with Microsoft or GitHub account</li>"
            "</ol>"
            "<h3>Usage:</h3>"
            "<p><code>devtunnel host -p " + portStr + " --allow-anonymous</code></p>"
            "<p><i>The tunnel URL will be detected automatically.</i></p>";
    } else if (name == "cloudflare") {
        html =
            "<h2>Cloudflare Tunnel (cloudflared)</h2>"
            "<h3>Install (pick one):</h3>"
            "<ul>"
            "<li><code>brew install cloudflared</code></li>"
            "<li>Download from: <a href=\"https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/downloads/\">"
            "Cloudflare Downloads</a></li>"
            "</ul>"
            "<h3>Usage (no account needed for quick tunnels):</h3>"
            "<p><code>cloudflared tunnel --url http://localhost:" + portStr + "</code></p>"
            "<h3>For persistent tunnels:</h3>"
            "<ol>"
            "<li>Sign up at <a href=\"https://dash.cloudflare.com\">dash.cloudflare.com</a></li>"
            "<li>Run: <code>cloudflared tunnel login</code></li>"
            "<li>Create tunnel: <code>cloudflared tunnel create &lt;name&gt;</code></li>"
            "</ol>"
            "<p><i>The tunnel URL will be detected automatically.</i></p>";
    } else {
        html = "<p>No installation guide available for this provider.</p>";
    }

    showGuideDialog(name + " Setup Guide", html);
}
