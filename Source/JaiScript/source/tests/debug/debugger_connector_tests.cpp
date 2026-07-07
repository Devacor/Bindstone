// End-to-end DAP attach test: a raw TCP client speaks the Debug Adapter Protocol to a live
// jai::debug::debug_connector and walks the full loop — handshake, setBreakpoints, run,
// stopped, stackTrace, scopes, variables (read-only), continue. Proves the transport +
// routing + park/pump wiring without needing VS Code. Interpreter backend only (the hook).

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

#ifdef JAISCRIPT_ENABLE_DEBUGGER

#include <jaiscript/debug/connector.hpp>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace jai::foundry;

namespace jai::foundry::tests {

namespace {

#ifdef _WIN32
    using sock_t = SOCKET;
    constexpr sock_t kBad = INVALID_SOCKET;
    void close_client(sock_t s) { ::closesocket(s); }
#else
    using sock_t = int;
    constexpr sock_t kBad = -1;
    void close_client(sock_t s) { ::close(s); }
#endif

void wsa_up() {
#ifdef _WIN32
    WSADATA w;
    ::WSAStartup(MAKEWORD(2, 2), &w);
#endif
}

sock_t connect_client(int port) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != kBad) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                // 100 ms recv timeout so wait_for() polls toward its own deadline.
#ifdef _WIN32
                DWORD tv = 100;
                ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
                timeval tv{0, 100000};
                ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
                return s;
            }
            close_client(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return kBad;
}

void send_dap(sock_t s, const std::string& json) {
    std::string frame = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
    size_t sent = 0;
    while (sent < frame.size()) {
        int n = ::send(s, frame.data() + sent, static_cast<int>(frame.size() - sent), 0);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

// Recv into an accumulating buffer until it contains `needle` or the deadline passes.
bool wait_for(sock_t s, std::string& buf, const std::string& needle, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    if (buf.find(needle) != std::string::npos) return true;
    char tmp[4096];
    while (std::chrono::steady_clock::now() < deadline) {
        int n = ::recv(s, tmp, sizeof(tmp), 0);
        if (n > 0) {
            buf.append(tmp, tmp + n);
            if (buf.find(needle) != std::string::npos) return true;
        }
        // n <= 0 is a timeout (SO_RCVTIMEO) or EOF; loop until the deadline either way.
    }
    return false;
}

// Pull the integer value following "key":  out of a JSON blob (small, well-formed DAP).
int extract_int(const std::string& buf, const std::string& key) {
    size_t p = buf.find("\"" + key + "\":");
    if (p == std::string::npos) return -1;
    p += key.size() + 3;
    while (p < buf.size() && (buf[p] == ' ')) ++p;
    int sign = 1;
    if (p < buf.size() && buf[p] == '-') { sign = -1; ++p; }
    int v = 0;
    bool any = false;
    while (p < buf.size() && buf[p] >= '0' && buf[p] <= '9') { v = v * 10 + (buf[p] - '0'); ++p; any = true; }
    return any ? sign * v : -1;
}

} // namespace

class debugger_connector_tests : public suite {
public:
    debugger_connector_tests() : suite("Debugger Connector") {}

    void forge_tests() override {
        test("dap_attach_breakpoint_inspect_continue", [this]() {
            wsa_up();

            auto eng = jai::engine::make();   // interpreter backend (the one with the hook)
            auto conn = std::make_shared<jai::debug::debug_connector>(0);   // ephemeral port
            eng->set_debug_connector(conn);
            int port = conn->port();
            check_gt(port, 0);

            sock_t cli = connect_client(port);
            check_true(cli != kBad);
            if (cli == kBad) return;

            std::string buf;

            // --- handshake ---
            send_dap(cli, R"({"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"jaiscript"}})");
            check_true(wait_for(cli, buf, "\"command\":\"initialize\""));
            check_true(wait_for(cli, buf, "\"event\":\"initialized\""));

            send_dap(cli, R"({"seq":2,"type":"request","command":"attach","arguments":{}})");
            check_true(wait_for(cli, buf, "\"command\":\"attach\""));

            // breakpoint on line 1, keyed to "<script>" (what execute() stamps)
            send_dap(cli, R"({"seq":3,"type":"request","command":"setBreakpoints","arguments":{"source":{"path":"<script>"},"breakpoints":[{"line":1}]}})");
            check_true(wait_for(cli, buf, "\"command\":\"setBreakpoints\""));
            check_true(wait_for(cli, buf, "\"verified\":true"));

            send_dap(cli, R"({"seq":4,"type":"request","command":"configurationDone"})");
            check_true(wait_for(cli, buf, "\"command\":\"configurationDone\""));

            // --- run the script on its own thread; it parks at line 1 ---
            std::atomic<bool> done{false};
            std::thread script([&] {
                eng->execute("var x = 1;\nvar y = 2;\nvar z = x + y;\n");
                done = true;
            });

            check_true(wait_for(cli, buf, "\"event\":\"stopped\""));
            check_true(buf.find("\"reason\":\"breakpoint\"") != std::string::npos);

            send_dap(cli, R"({"seq":5,"type":"request","command":"threads"})");
            check_true(wait_for(cli, buf, "\"name\":\"script\""));

            // --- step over the wire: stepIn advances to line 2 (reason "step") ---
            std::string step_buf;
            send_dap(cli, R"({"seq":6,"type":"request","command":"stepIn","arguments":{"threadId":1}})");
            check_true(wait_for(cli, step_buf, "\"event\":\"stopped\""));
            check_true(step_buf.find("\"reason\":\"step\"") != std::string::npos);

            send_dap(cli, R"({"seq":7,"type":"request","command":"stackTrace","arguments":{"threadId":1}})");
            check_true(wait_for(cli, step_buf, "\"stackFrames\""));
            check_true(wait_for(cli, step_buf, "\"line\":2"));
            int frame_id = extract_int(step_buf, "id");   // generation-scoped; changes per stop
            check_gt(frame_id, 0);

            // scopes -> Locals; x==1 now (line 1 ran), y/z not declared yet
            std::string scopes_buf;
            std::string sreq = R"({"seq":8,"type":"request","command":"scopes","arguments":{"frameId":)"
                + std::to_string(frame_id) + "}}";
            send_dap(cli, sreq);
            check_true(wait_for(cli, scopes_buf, "\"name\":\"Locals\""));
            int locals_ref = extract_int(scopes_buf, "variablesReference");
            check_gt(locals_ref, 0);

            std::string vars_buf;
            std::string vreq = R"({"seq":9,"type":"request","command":"variables","arguments":{"variablesReference":)"
                + std::to_string(locals_ref) + "}}";
            send_dap(cli, vreq);
            check_true(wait_for(cli, vars_buf, "\"command\":\"variables\""));
            check_true(vars_buf.find("\"name\":\"x\"") != std::string::npos);
            check_true(vars_buf.find("\"value\":\"1\"") != std::string::npos);
            check_true(vars_buf.find("\"name\":\"y\"") == std::string::npos);

            // --- next (step over) advances to line 3 ---
            std::string next_buf;
            send_dap(cli, R"({"seq":10,"type":"request","command":"next","arguments":{"threadId":1}})");
            check_true(wait_for(cli, next_buf, "\"event\":\"stopped\""));
            send_dap(cli, R"({"seq":11,"type":"request","command":"stackTrace","arguments":{"threadId":1}})");
            check_true(wait_for(cli, next_buf, "\"line\":3"));

            // --- continue to completion ---
            std::string cont_buf;
            send_dap(cli, R"({"seq":12,"type":"request","command":"continue","arguments":{"threadId":1}})");
            check_true(wait_for(cli, cont_buf, "\"command\":\"continue\""));

            script.join();
            check_true(done.load());
            check_eq(static_cast<int64_t>(3), eng->get_variable("z").as_int());

            send_dap(cli, R"({"seq":13,"type":"request","command":"disconnect"})");
            wait_for(cli, cont_buf, "\"command\":\"disconnect\"", 1000);
            close_client(cli);
            // eng's destructor stops the connector (joins its thread, detaches the controller).
        });

        test("park_only_request_while_running_errors", [this]() {
            // A stackTrace sent before any stop must get an immediate "not stopped" error,
            // never a silently-queued closure that hangs the client.
            wsa_up();
            auto eng = jai::engine::make();
            auto conn = std::make_shared<jai::debug::debug_connector>(0);
            eng->set_debug_connector(conn);
            sock_t cli = connect_client(conn->port());
            check_true(cli != kBad);
            if (cli == kBad) return;

            std::string buf;
            send_dap(cli, R"({"seq":1,"type":"request","command":"initialize"})");
            check_true(wait_for(cli, buf, "\"event\":\"initialized\""));
            send_dap(cli, R"({"seq":2,"type":"request","command":"attach"})");
            check_true(wait_for(cli, buf, "\"command\":\"attach\""));

            send_dap(cli, R"({"seq":3,"type":"request","command":"stackTrace","arguments":{"threadId":1}})");
            check_true(wait_for(cli, buf, "\"command\":\"stackTrace\""));
            check_true(buf.find("\"success\":false") != std::string::npos);

            close_client(cli);
        });
    }
};

} // namespace jai::foundry::tests

using debugger_connector_tests = jai::foundry::tests::debugger_connector_tests;
FOUNDRY_REGISTER(debugger_connector_tests)

#endif // JAISCRIPT_ENABLE_DEBUGGER
