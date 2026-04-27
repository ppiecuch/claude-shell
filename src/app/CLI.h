#pragma once
#include <string>
#include <vector>

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.1"
#endif
#ifndef BUILD_NUMBER
#define BUILD_NUMBER 1
#endif

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

namespace CLI {

// Returns true if the arguments were handled as CLI commands (no GUI needed).
// Returns false if the GUI should be launched.
bool handleArgs(int argc, char** argv);

void printLogo();
void printHelp();
void printVersion();

} // namespace CLI
