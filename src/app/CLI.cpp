#include "app/CLI.h"
#include "app/IpcController.h"
#include "util/doctest.h"
#include <FL/Fl.H>
#include <cstdio>
#include <string>

namespace CLI {

static const char* LOGO = R"(
   _____ _                 _        _____ _          _ _
  / ____| |               | |      / ____| |        | | |
 | |    | | __ _ _   _  __| | ___ | (___ | |__   ___| | |
 | |    | |/ _` | | | |/ _` |/ _ \ \___ \| '_ \ / _ \ | |
 | |____| | (_| | |_| | (_| |  __/ ____) | | | |  __/ | |
  \_____|_|\__,_|\__,_|\__,_|\___||_____/|_| |_|\___|_|_|
)";

void printLogo() {
    printf("\033[36m%s\033[0m", LOGO);
    printf("  Claude Session Sharing Manager\n");
    printf("  ver. %s (build %d)\n\n", BUILD_VERSION, BUILD_NUMBER);
}

void printVersion() {
    printf("claude-shell %s (build %d)\n", BUILD_VERSION, BUILD_NUMBER);
}

void printHelp() {
    printLogo();
    printf("Usage: claude-shell [command] [options]\n\n");
    printf("Commands:\n");
    printf("  (no args)          Launch GUI application\n");
    printf("  status             Show server status\n");
    printf("  sessions           List discovered Claude sessions\n");
    printf("  start [port]       Start the proxy server\n");
    printf("  stop               Stop the proxy server\n");
    printf("  url                Print the connection URL\n");
    printf("  tunnel <provider>  Start a tunnel (ngrok, devtunnel, cloudflare)\n");
    printf("  show               Bring GUI window to front\n");
    printf("  --help, -h         Show this help\n");
    printf("  --version, -v      Show version\n");
#ifndef DOCTEST_CONFIG_DISABLE
    printf("  --doctest          Run built-in tests\n");
#endif
    printf("\nExamples:\n");
    printf("  claude-shell                    # Launch GUI\n");
    printf("  claude-shell start              # Start proxy server\n");
    printf("  claude-shell start 8080         # Start on specific port\n");
    printf("  claude-shell sessions           # List Claude sessions\n");
    printf("  claude-shell tunnel ngrok       # Start ngrok tunnel\n");
    printf("  claude-shell url                # Print connection URL\n");
    printf("  claude-shell url | pbcopy       # Copy URL to clipboard\n");
    printf("\nUse --help-fltk for FLTK GUI options.\n\n");
}

bool handleArgs(int argc, char** argv) {
    if (argc < 2) return false; // No args — launch GUI

    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") {
        printHelp();
        return true;
    }

    if (cmd == "--help-fltk") {
        printLogo();
        printf("FLTK GUI options:\n%s\n", Fl::help);
        return true;
    }

    if (cmd == "--version" || cmd == "-v") {
        printVersion();
        return true;
    }

    // --doctest is handled in main.cpp, not via IPC
    if (cmd == "--doctest") return false;

    // All other commands are sent to the running instance via IPC
    std::string args;
    for (int i = 2; i < argc; i++) {
        if (i > 2) args += " ";
        args += argv[i];
    }

    std::string response = IpcController::sendCommand(cmd, args);

    if (response.find("error: no running instance") != std::string::npos) {
        fprintf(stderr, "\033[31mError:\033[0m No running Claude Shell instance found.\n");
        fprintf(stderr, "Start the GUI first: claude-shell\n");
        return true;
    }

    printf("%s", response.c_str());
    if (!response.empty() && response.back() != '\n') printf("\n");
    return true;
}

} // namespace CLI
