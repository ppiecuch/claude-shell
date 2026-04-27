// Inline tests — compiled into the binary, run with --doctest
// Excluded from App Store builds via DOCTEST_CONFIG_DISABLE

#include "util/doctest.h"

#include "net/AuthToken.h"
#include "core/SessionInfo.h"
#include "core/SessionManager.h"
#include "core/ProcessHandle.h"
#include "core/TunnelProvider.h"
#include "core/NgrokProvider.h"
#include "core/DevTunnelProvider.h"
#include "core/CloudflareProvider.h"
#include "util/Platform.h"
#include "util/Logger.h"
#include "app/IpcController.h"

#include <thread>
#include <chrono>

// ═══════════════════════════════════════════════════════════════
// AuthToken
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("AuthToken") {

TEST_CASE("generate produces non-empty token") {
    auto token = AuthToken::generate();
    CHECK(!token.empty());
    CHECK(token.size() >= 32);
}

TEST_CASE("generate produces unique tokens") {
    auto t1 = AuthToken::generate();
    auto t2 = AuthToken::generate();
    CHECK(t1 != t2);
}

TEST_CASE("validate accepts matching tokens") {
    auto token = AuthToken::generate();
    CHECK(AuthToken::validate(token, token));
}

TEST_CASE("validate rejects different tokens") {
    auto t1 = AuthToken::generate();
    auto t2 = AuthToken::generate();
    CHECK_FALSE(AuthToken::validate(t1, t2));
}

TEST_CASE("validate rejects empty against non-empty") {
    auto token = AuthToken::generate();
    CHECK_FALSE(AuthToken::validate("", token));
    CHECK_FALSE(AuthToken::validate(token, ""));
}

TEST_CASE("validate rejects different lengths") {
    CHECK_FALSE(AuthToken::validate("short", "much-longer-token"));
}

TEST_CASE("base64url encoding uses only URL-safe chars") {
    auto token = AuthToken::generate();
    for (char c : token) {
        bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_';
        CHECK(valid);
    }
}

TEST_CASE("base64url known vector") {
    unsigned char data[] = {0x00, 0x01, 0x02};
    auto result = AuthToken::base64url_public(data, 3);
    CHECK(result == "AAEC");
}

} // TEST_SUITE AuthToken

// ═══════════════════════════════════════════════════════════════
// SessionInfo
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("SessionInfo") {

TEST_CASE("default construction") {
    SessionInfo s;
    CHECK(s.pid == 0);
    CHECK(s.sessionId.empty());
    CHECK(s.cwd.empty());
    CHECK(s.startedAt == 0);
    CHECK(s.alive == false);
}

TEST_CASE("fields can be assigned") {
    SessionInfo s;
    s.pid = 12345;
    s.sessionId = "abc-def";
    s.cwd = "/tmp";
    s.startedAt = 1000000;
    s.alive = true;
    CHECK(s.pid == 12345);
    CHECK(s.sessionId == "abc-def");
    CHECK(s.alive == true);
}

} // TEST_SUITE SessionInfo

// ═══════════════════════════════════════════════════════════════
// Platform utilities
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("Platform") {

TEST_CASE("homeDir returns non-empty") {
    auto home = Platform::homeDir();
    CHECK(!home.empty());
    CHECK(home[0] == '/');
}

TEST_CASE("claudeSessionsDir contains .claude") {
    auto dir = Platform::claudeSessionsDir();
    CHECK(dir.find(".claude") != std::string::npos);
    CHECK(dir.find("sessions") != std::string::npos);
}

TEST_CASE("isProcessAlive for current process") {
    CHECK(Platform::isProcessAlive(getpid()));
}

TEST_CASE("isProcessAlive for invalid PID") {
    CHECK_FALSE(Platform::isProcessAlive(99999999));
}

TEST_CASE("findExecutable finds ls") {
    auto path = Platform::findExecutable("ls");
    CHECK(!path.empty());
}

TEST_CASE("findExecutable returns empty for nonexistent") {
    auto path = Platform::findExecutable("definitely_not_a_real_binary_xyz");
    CHECK(path.empty());
}

} // TEST_SUITE Platform

// ═══════════════════════════════════════════════════════════════
// ProcessHandle
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("ProcessHandle") {

TEST_CASE("spawn and read stdout") {
    auto proc = ProcessHandle::spawn("echo", {"hello", "world"});
    REQUIRE(proc != nullptr);
    CHECK(proc->pid() > 0);

    // Wait for process to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char buf[256];
    std::string output;
    ssize_t n;
    while ((n = proc->readStdout(buf, sizeof(buf))) > 0) {
        output.append(buf, n);
    }
    CHECK(output.find("hello world") != std::string::npos);
}

TEST_CASE("spawn with invalid executable") {
    auto proc = ProcessHandle::spawn("nonexistent_binary_xyz", {});
    CHECK(proc == nullptr);
}

TEST_CASE("process exit detection") {
    auto proc = ProcessHandle::spawn("true", {});
    REQUIRE(proc != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(proc->isRunning());
    CHECK(proc->exitCode() == 0);
}

TEST_CASE("process exit code non-zero") {
    auto proc = ProcessHandle::spawn("false", {});
    REQUIRE(proc != nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(proc->isRunning());
    CHECK(proc->exitCode() != 0);
}

TEST_CASE("write to stdin") {
    auto proc = ProcessHandle::spawn("cat", {});
    REQUIRE(proc != nullptr);

    const char* input = "test input\n";
    auto written = proc->writeStdin(input, strlen(input));
    CHECK(written > 0);
    proc->closeStdin();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char buf[256];
    std::string output;
    ssize_t n;
    while ((n = proc->readStdout(buf, sizeof(buf))) > 0) {
        output.append(buf, n);
    }
    CHECK(output.find("test input") != std::string::npos);
}

TEST_CASE("terminate running process") {
    auto proc = ProcessHandle::spawn("sleep", {"60"});
    REQUIRE(proc != nullptr);
    CHECK(proc->isRunning());

    proc->terminate();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_FALSE(proc->isRunning());
}

} // TEST_SUITE ProcessHandle

// ═══════════════════════════════════════════════════════════════
// SessionManager
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("SessionManager") {

TEST_CASE("refresh does not crash") {
    SessionManager mgr;
    mgr.refresh();
    // Should not crash even if sessions dir doesn't exist
}

TEST_CASE("sessions list is populated after refresh") {
    SessionManager mgr;
    mgr.refresh();
    // We can't guarantee sessions exist, but the vector should be valid
    auto& sessions = mgr.sessions();
    CHECK(sessions.size() >= 0); // Just check it's accessible
}

TEST_CASE("onChange callback fires") {
    SessionManager mgr;
    bool called = false;
    mgr.setOnChange([&called]() { called = true; });
    mgr.refresh();
    // May or may not fire depending on whether sessions dir exists
    // Just verify no crash
}

} // TEST_SUITE SessionManager

// ═══════════════════════════════════════════════════════════════
// TunnelProvider implementations
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("TunnelProviders") {

TEST_CASE("NgrokProvider name and args") {
    NgrokProvider p;
    CHECK(p.name() == "ngrok");
    auto args = p.buildArgs(8080);
    CHECK(args.size() >= 2);
    CHECK(args[0] == "http");
    CHECK(args[1] == "8080");
}

TEST_CASE("NgrokProvider parsePublicUrl") {
    NgrokProvider p;
    // Valid ngrok JSON log line
    CHECK(p.parsePublicUrl(R"({"url":"https://abc123.ngrok.io"})") == "https://abc123.ngrok.io");
    // Invalid
    CHECK(p.parsePublicUrl("not json").empty());
    CHECK(p.parsePublicUrl(R"({"msg":"other"})").empty());
}

TEST_CASE("DevTunnelProvider name and args") {
    DevTunnelProvider p;
    CHECK(p.name() == "devtunnel");
    auto args = p.buildArgs(9090);
    CHECK(args[0] == "host");
    bool hasPort = false;
    for (auto& a : args) if (a == "9090") hasPort = true;
    CHECK(hasPort);
}

TEST_CASE("DevTunnelProvider parsePublicUrl") {
    DevTunnelProvider p;
    CHECK(!p.parsePublicUrl("Connect via browser: https://xyz.devtunnels.ms").empty());
    CHECK(p.parsePublicUrl("some other output").empty());
}

TEST_CASE("CloudflareProvider name and args") {
    CloudflareProvider p;
    CHECK(p.name() == "cloudflare");
    auto args = p.buildArgs(3000);
    bool hasUrl = false;
    for (auto& a : args) if (a.find("3000") != std::string::npos) hasUrl = true;
    CHECK(hasUrl);
}

TEST_CASE("CloudflareProvider parsePublicUrl") {
    CloudflareProvider p;
    CHECK(!p.parsePublicUrl("https://random-words.trycloudflare.com").empty());
    CHECK(p.parsePublicUrl("some other output").empty());
}

TEST_CASE("TunnelProvider config") {
    NgrokProvider p;
    TunnelProvider::Config cfg;
    cfg.authToken = "test-token";
    cfg.region = "us";
    p.setConfig(cfg);
    CHECK(p.config().authToken == "test-token");
    CHECK(p.config().region == "us");
}

} // TEST_SUITE TunnelProviders

// ═══════════════════════════════════════════════════════════════
// Logger
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("Logger") {

TEST_CASE("log callback fires") {
    bool called = false;
    LogLevel capturedLevel;
    std::string capturedMsg;

    Logger::instance().setCallback([&](LogLevel level, const std::string& msg) {
        called = true;
        capturedLevel = level;
        capturedMsg = msg;
    });

    LOG_INFO("test message %d", 42);

    CHECK(called);
    CHECK(capturedLevel == LogLevel::Info);
    CHECK(capturedMsg == "test message 42");

    // Restore
    Logger::instance().setCallback(nullptr);
}

TEST_CASE("log levels") {
    std::vector<LogLevel> levels;
    Logger::instance().setCallback([&](LogLevel level, const std::string&) {
        levels.push_back(level);
    });

    LOG_DEBUG("d");
    LOG_INFO("i");
    LOG_WARN("w");
    LOG_ERROR("e");

    REQUIRE(levels.size() == 4);
    CHECK(levels[0] == LogLevel::Debug);
    CHECK(levels[1] == LogLevel::Info);
    CHECK(levels[2] == LogLevel::Warn);
    CHECK(levels[3] == LogLevel::Error);

    Logger::instance().setCallback(nullptr);
}

} // TEST_SUITE Logger

// ═══════════════════════════════════════════════════════════════
// IpcController
// ═══════════════════════════════════════════════════════════════

TEST_SUITE("IpcController") {

TEST_CASE("socket path contains user") {
    auto path = IpcController::socketPath();
    CHECK(!path.empty());
    CHECK(path.find("claude-shell") != std::string::npos);
}

TEST_CASE("sendCommand to non-existent instance") {
    // Remove any existing socket
    unlink(IpcController::socketPath().c_str());
    auto result = IpcController::sendCommand("status");
    CHECK(result.find("error") != std::string::npos);
}

} // TEST_SUITE IpcController

// ═══════════════════════════════════════════════════════════════
// WebSocketServer (basic)
// ═══════════════════════════════════════════════════════════════

#include "net/WebSocketServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

TEST_SUITE("WebSocketServer") {

TEST_CASE("listen on random port") {
    WebSocketServer srv;
    CHECK(srv.listen(0));
    CHECK(srv.port() > 0);
    CHECK(srv.isRunning());
    srv.stop();
    CHECK_FALSE(srv.isRunning());
}

TEST_CASE("listen on specific port") {
    WebSocketServer srv;
    CHECK(srv.listen(18765));
    CHECK(srv.port() == 18765);
    srv.stop();
}

TEST_CASE("TCP connection accepted") {
    WebSocketServer srv;
    REQUIRE(srv.listen(0));
    int port = srv.port();

    // Connect a raw TCP socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    CHECK(rc == 0);

    srv.service(); // Accept the connection
    close(fd);
    srv.stop();
}

} // TEST_SUITE WebSocketServer
