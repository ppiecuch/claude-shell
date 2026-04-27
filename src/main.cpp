#define DOCTEST_CONFIG_IMPLEMENT
#include "util/doctest.h"

#include "app/Application.h"
#include "app/CLI.h"

int main(int argc, char** argv) {
    // Handle CLI commands (--help, --version, or IPC commands to running instance)
    if (CLI::handleArgs(argc, argv)) {
        return 0;
    }

#ifndef DOCTEST_CONFIG_DISABLE
    // Run tests if --doctest is passed
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--doctest") {
            CLI::printLogo();
            printf("Running tests...\n\n");
            doctest::Context ctx;
            ctx.applyCommandLine(argc, argv);
            return ctx.run();
        }
    }
#endif

    // Launch GUI
    Application app;
    return app.run(argc, argv);
}
