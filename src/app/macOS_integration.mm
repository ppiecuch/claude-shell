#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>
#include <FL/Fl.H>
#include "util/incbin.h"

INCBIN_EXTERN(MenuIcon);
INCBIN_EXTERN(KomsoftLogo);

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.1"
#endif
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 1
#endif

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

// ── Callbacks set by Application ──

static void (*g_onStartServer)() = nullptr;
static void (*g_onStopServer)() = nullptr;
static bool (*g_isServerRunning)() = nullptr;
static void (*g_onShowWindow)() = nullptr;

// ── Logo view for the top of the status menu ──

@interface KomsoftLogoView : NSView
@property (nonatomic, strong) NSImage *image;
@end

@implementation KomsoftLogoView
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)dirtyRect {
    if (!self.image) return;
    NSSize imgSize = self.image.size;
    if (imgSize.width <= 0 || imgSize.height <= 0) return;
    NSRect avail = NSInsetRect(self.bounds, 10, 5);
    CGFloat scale = MIN(avail.size.width / imgSize.width,
                        avail.size.height / imgSize.height);
    CGFloat dw = imgSize.width * scale;
    CGFloat dh = imgSize.height * scale;
    NSRect dst = NSMakeRect(avail.origin.x + (avail.size.width  - dw) / 2,
                            avail.origin.y + (avail.size.height - dh) / 2,
                            dw, dh);
    [self.image drawInRect:dst fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1.0 respectFlipped:YES hints:nil];
}
@end

// ── Strong references (prevent ARC release) ──

static NSStatusItem *g_statusItem = nil;
static NSMenu *g_statusMenu = nil;
static NSMenuItem *g_serverMenuItem = nil;

// ── Menu Delegate (instance-based, like Diagon) ──

@interface ClaudeShellMenuDelegate : NSObject <NSMenuDelegate>
@property (nonatomic, strong) NSStatusItem *statusItem;
@end

@implementation ClaudeShellMenuDelegate

- (void)showWindow:(id)sender {
    // Same pattern as Diagon: show() -> activate -> awake()
    if (g_onShowWindow) {
        g_onShowWindow();
    }
    [NSApp activateIgnoringOtherApps:YES];
    Fl::awake();
}

- (void)toggleServer:(id)sender {
    bool running = g_isServerRunning ? g_isServerRunning() : false;
    if (running && g_onStopServer) {
        Fl::awake([](void*) { g_onStopServer(); }, nullptr);
    } else if (!running && g_onStartServer) {
        Fl::awake([](void*) { g_onStartServer(); }, nullptr);
    }
}

- (void)toggleStartAtLogin:(id)sender {
    if (@available(macOS 13.0, *)) {
        SMAppService *service = SMAppService.mainAppService;
        NSError *error = nil;
        if (service.status == SMAppServiceStatusEnabled) {
            [service unregisterAndReturnError:&error];
        } else {
            [service registerAndReturnError:&error];
        }
        if (error) NSLog(@"Login item toggle failed: %@", error.localizedDescription);
        ((NSMenuItem*)sender).state = (service.status == SMAppServiceStatusEnabled)
            ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

- (void)quitApp:(id)sender {
    // Stop server cleanly if running
    if (g_onStopServer && g_isServerRunning && g_isServerRunning()) {
        g_onStopServer();
    }
    exit(0);
}

- (void)statusItemClicked:(id)sender {
    if (g_statusMenu && self.statusItem.button) {
        self.statusItem.menu = g_statusMenu;
        [self.statusItem.button performClick:nil];
        // Don't reset here — menuDidClose: handles it after the user dismisses
    }
}

- (void)menuDidClose:(NSMenu *)menu {
    // Defer reset so the selected item's action fires before menu detaches
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusItem.menu = nil;
    });
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (g_serverMenuItem) {
        bool running = g_isServerRunning ? g_isServerRunning() : false;
        g_serverMenuItem.title = running ? @"Stop Server" : @"Start Server";
        NSString *symbol = running ? @"stop.fill" : @"play.fill";
        g_serverMenuItem.image = [NSImage imageWithSystemSymbolName:symbol
                                            accessibilityDescription:nil];
    }
}

@end

static ClaudeShellMenuDelegate *g_menuDelegate = nil;

// ── Install Status Bar ──

static void installStatusBar() {
    g_menuDelegate = [[ClaudeShellMenuDelegate alloc] init];

    g_statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    g_menuDelegate.statusItem = g_statusItem;

    // Icon from embedded PNG
    NSData *iconData = [NSData dataWithBytes:gMenuIconData length:gMenuIconSize];
    NSImage *icon = [[NSImage alloc] initWithData:iconData];
    [icon setSize:NSMakeSize(18, 18)];
    [icon setTemplate:YES];
    g_statusItem.button.image = icon;
    g_statusItem.button.toolTip = @"Claude Shell";

    // Action-based click — FLTK owns the event loop; direct menu assignment
    // causes Cocoa's menu-tracking run loop to fight with Fl::wait() and makes
    // the status item disappear. We show the menu manually from the action and
    // detach it in menuDidClose: so item actions still fire correctly.
    g_statusItem.button.target = g_menuDelegate;
    g_statusItem.button.action = @selector(statusItemClicked:);
    [g_statusItem.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];

    // Build menu
    g_statusMenu = [[NSMenu alloc] init];
    g_statusMenu.delegate = g_menuDelegate;

    // Logo item
    NSData *logoData = [NSData dataWithBytes:gKomsoftLogoData length:gKomsoftLogoSize];
    NSImage *logoImage = [[NSImage alloc] initWithData:logoData];
    CGFloat logoH = 44;
    CGFloat logoW = logoImage
        ? logoH * (logoImage.size.width / logoImage.size.height)
        : 200;
    if (logoW < 180) logoW = 180;
    KomsoftLogoView *logoView = [[KomsoftLogoView alloc]
        initWithFrame:NSMakeRect(0, 0, logoW, logoH)];
    logoView.image = logoImage;
    NSMenuItem *logoItem = [[NSMenuItem alloc] init];
    [logoItem setView:logoView];
    [logoItem setEnabled:NO];
    [g_statusMenu addItem:logoItem];
    [g_statusMenu addItem:[NSMenuItem separatorItem]];

    // Version (disabled)
    NSString *verStr = [NSString stringWithFormat:@"Claude Shell %s (build %s)",
                        BUILD_VERSION, STRINGIFY(BUILD_NUMBER)];
    NSMenuItem *verItem = [[NSMenuItem alloc] initWithTitle:verStr action:nil keyEquivalent:@""];
    [verItem setEnabled:NO];
    [g_statusMenu addItem:verItem];
    [g_statusMenu addItem:[NSMenuItem separatorItem]];

    // Show Window
    NSMenuItem *showItem = [[NSMenuItem alloc] initWithTitle:@"Show Window"
                                                     action:@selector(showWindow:)
                                              keyEquivalent:@""];
    showItem.target = g_menuDelegate;
    [g_statusMenu addItem:showItem];
    [g_statusMenu addItem:[NSMenuItem separatorItem]];

    // Start/Stop Server
    g_serverMenuItem = [[NSMenuItem alloc] initWithTitle:@"Start Server"
                                                 action:@selector(toggleServer:)
                                          keyEquivalent:@""];
    g_serverMenuItem.target = g_menuDelegate;
    [g_statusMenu addItem:g_serverMenuItem];
    [g_statusMenu addItem:[NSMenuItem separatorItem]];

    // Start at Login
    NSMenuItem *loginItem = [[NSMenuItem alloc] initWithTitle:@"Start at Login"
                                                      action:@selector(toggleStartAtLogin:)
                                               keyEquivalent:@""];
    loginItem.target = g_menuDelegate;
    if (@available(macOS 13.0, *)) {
        loginItem.state = (SMAppService.mainAppService.status == SMAppServiceStatusEnabled)
            ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [g_statusMenu addItem:loginItem];
    [g_statusMenu addItem:[NSMenuItem separatorItem]];

    // Quit
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit Claude Shell"
                                                      action:@selector(quitApp:)
                                               keyEquivalent:@"q"];
    quitItem.target = g_menuDelegate;
    [g_statusMenu addItem:quitItem];
}

// ── About Panel Override ──

static void installAboutOverride() {
    NSMenu *mainMenu = [NSApp mainMenu];
    if (!mainMenu || mainMenu.numberOfItems == 0) return;

    NSMenuItem *appMenuItem = [mainMenu itemAtIndex:0];
    NSMenu *appMenu = appMenuItem.submenu;
    if (!appMenu) return;

    for (NSMenuItem *item in appMenu.itemArray) {
        if ([item.title hasPrefix:@"About"]) {
            // Replace action with native about panel
            item.target = nil;
            item.action = @selector(orderFrontStandardAboutPanel:);
            break;
        }
    }
}

// ── Help Menu ──

static void installHelpMenu() {
    NSMenu *mainMenu = [NSApp mainMenu];
    if (!mainMenu) return;

    [[NSHelpManager sharedHelpManager] registerBooksInBundle:[NSBundle mainBundle]];

    NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
    NSMenuItem *helpItem = [[NSMenuItem alloc] initWithTitle:@"Claude Shell Help"
                                                      action:@selector(showHelp:)
                                               keyEquivalent:@"?"];
    [helpMenu addItem:helpItem];

    NSMenuItem *helpMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    helpMenuItem.submenu = helpMenu;
    [mainMenu addItem:helpMenuItem];
    [NSApp setHelpMenu:helpMenu];
}

// ── Reopen handler (Dock click / `open` command while already running) ──

@interface ClaudeShellAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation ClaudeShellAppDelegate
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag {
    if (!flag && g_onShowWindow) {
        g_onShowWindow();
        [NSApp activateIgnoringOtherApps:YES];
        Fl::awake();
    }
    return YES;
}
@end

static ClaudeShellAppDelegate *g_appDelegate = nil;

// ── Public API ──

static void installAll(void*) {
    installAboutOverride();
    installHelpMenu();
    installStatusBar();

    // Install reopen handler
    g_appDelegate = [[ClaudeShellAppDelegate alloc] init];
    [NSApp setDelegate:g_appDelegate];
}

extern "C" void macOS_setup() {
    Fl::add_timeout(0.2, installAll, nullptr);
}

extern "C" void macOS_set_callbacks(
    void (*onStartServer)(),
    void (*onStopServer)(),
    bool (*isServerRunning)(),
    void (*onShowWindow)())
{
    g_onStartServer = onStartServer;
    g_onStopServer = onStopServer;
    g_isServerRunning = isServerRunning;
    g_onShowWindow = onShowWindow;
}
