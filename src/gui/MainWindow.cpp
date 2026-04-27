#include "gui/MainWindow.h"
#include "core/SessionManager.h"
#include "core/ProxyServer.h"
#include "core/TunnelManager.h"
#include <FL/fl_ask.H>
#include <cstdio>

MainWindow::MainWindow(int w, int h, const char*)
    : Fl_Double_Window(w, h)
{
    static char titleBuf[128];
    snprintf(titleBuf, sizeof(titleBuf), "Claude Shell (ver. %s, build %d)",
             BUILD_VERSION, BUILD_NUMBER);
    copy_label(titleBuf);

    int hdrH = InfoHeader::HEIGHT;

    menuBar_ = new Fl_Sys_Menu_Bar(0, 0, 0, 0);
    buildMenuBar();

    infoHeader_ = new InfoHeader(0, 0, w);

    int tabsY = hdrH;
    tabs_ = new Fl_Tabs(0, tabsY, w, h - tabsY - 25);
    {
        int panelY = tabsY + 25;
        int panelH = h - tabsY - 50;

        sessionPanel_ = new SessionPanel(0, panelY, w, panelH);
        sessionPanel_->label("Sessions");

        proxyPanel_ = new ProxyPanel(0, panelY, w, panelH);
        proxyPanel_->label("Server");

        tunnelPanel_ = new TunnelPanel(0, panelY, w, panelH);
        tunnelPanel_->label("Tunnels");

        logPanel_ = new LogPanel(0, panelY, w, panelH);
        logPanel_->label("Logs");
    }
    tabs_->end();

    statusBar_ = new Fl_Box(0, h - 25, w, 25);
    statusBar_->box(FL_THIN_UP_BOX);
    statusBar_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    statusBar_->labelfont(FL_HELVETICA);
    statusBar_->labelsize(11);
    statusBar_->copy_label("Ready");

    end();
    resizable(tabs_);
    size_range(700, 450);
}

void MainWindow::init(SessionManager* sm, ProxyServer* ps, TunnelManager* tm) {
    sessionMgr_ = sm;
    proxyServer_ = ps;
    tunnelMgr_ = tm;

    proxyPanel_->setProxyServer(ps);
    ps->setTunnelManager(tm);
    tunnelPanel_->setTunnelManager(tm);
}

void MainWindow::buildMenuBar() {
    menuBar_->add("File/Close Window", FL_COMMAND + 'w', onCloseWindow, this);
    menuBar_->add("Session/Refresh Sessions", FL_COMMAND + 'r', onRefreshSessions, this);
    menuBar_->add("Tunnel/Stop All Tunnels", 0, onStopAllTunnels, this);
}

void MainWindow::updateStatusBar() {
    static char buf[256];
    int sessions = sessionMgr_ ? (int)sessionMgr_->sessions().size() : 0;
    const char* serverState = "off";
    int clients = 0;
    if (proxyServer_ && proxyServer_->state() == ProxyServer::State::Running) {
        serverState = "running";
        clients = (int)proxyServer_->clientCount();
    }
    int tunnels = tunnelMgr_ ? (int)tunnelMgr_->activeTunnels().size() : 0;

    snprintf(buf, sizeof(buf), "  %d sessions | server: %s (%d clients) | %d tunnels",
             sessions, serverState, clients, tunnels);
    statusBar_->copy_label(buf);
}

void MainWindow::switchToTab(int index) {
    Fl_Widget* children[] = { sessionPanel_, proxyPanel_, tunnelPanel_, logPanel_ };
    if (index >= 0 && index < 4) {
        tabs_->value(children[index]);
    }
}

void MainWindow::onCloseWindow(Fl_Widget*, void* data) {
    auto* win = (MainWindow*)data;
    win->hide();
}

void MainWindow::onRefreshSessions(Fl_Widget*, void* data) {
    auto* win = (MainWindow*)data;
    if (win->sessionMgr_) win->sessionMgr_->refresh();
}

void MainWindow::onStopAllTunnels(Fl_Widget*, void* data) {
    auto* win = (MainWindow*)data;
    if (win->tunnelMgr_) win->tunnelMgr_->stopAll();
}
