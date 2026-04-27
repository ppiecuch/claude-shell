#pragma once
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Box.H>

#include "gui/InfoHeader.h"
#include "gui/SessionPanel.h"
#include "gui/ProxyPanel.h"
#include "gui/TunnelPanel.h"
#include "gui/LogPanel.h"

class SessionManager;
class ProxyServer;
class TunnelManager;

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char* title);

    void init(SessionManager* sm, ProxyServer* ps, TunnelManager* tm);

    SessionPanel* sessionPanel() { return sessionPanel_; }
    ProxyPanel*   proxyPanel()   { return proxyPanel_; }
    TunnelPanel*  tunnelPanel()  { return tunnelPanel_; }
    LogPanel*     logPanel()     { return logPanel_; }

    void updateStatusBar();
    void switchToTab(int index);

private:
    Fl_Sys_Menu_Bar* menuBar_;
    InfoHeader* infoHeader_;
    Fl_Tabs* tabs_;
    SessionPanel* sessionPanel_;
    ProxyPanel*   proxyPanel_;
    TunnelPanel*  tunnelPanel_;
    LogPanel*     logPanel_;
    Fl_Box* statusBar_;

    SessionManager* sessionMgr_ = nullptr;
    ProxyServer* proxyServer_ = nullptr;
    TunnelManager* tunnelMgr_ = nullptr;

    void buildMenuBar();

    static void onCloseWindow(Fl_Widget*, void*);
    static void onRefreshSessions(Fl_Widget*, void*);
    static void onStopAllTunnels(Fl_Widget*, void*);
};
