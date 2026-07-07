// jai::debug::debug_connector — DAP-over-TCP transport. Compiled only when
// JAISCRIPT_ENABLE_DEBUGGER is defined; otherwise this TU is empty and constructing a
// debug_connector is a link error by design (mobile / shipping builds ship no socket code).
// See docs/DEBUGGER_DESIGN.md (Transport internals, DAP lifecycle & request routing).

#ifdef JAISCRIPT_ENABLE_DEBUGGER

// Platform sockets FIRST — winsock2 must precede anything that might drag in windows.h.
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
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <poll.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

#include <jaiscript/debug/connector.hpp>
#include <jaiscript/core/engine.hpp>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace jai::debug {

namespace {

// VS Code sends a breakpoint's source.path in the OS's own spelling — on Windows the drive-
// letter case and separators can differ from what the engine stamped. Breakpoints are matched
// by EXACT string against each node's location.filename, and the engine stamps
// std::filesystem::canonical() paths (execute_file / import), so normalize the incoming path
// the same way or nothing binds. Fall back to the raw string for a virtual / not-yet-real path.
std::string normalize_bp_path(const std::string& path) {
    if (path.empty()) { return path; }
    std::error_code ec;
    auto canon = std::filesystem::canonical(std::filesystem::path(path), ec);
    if (!ec) { return canon.string(); }
    canon = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    return ec ? path : canon.string();
}

// ---- platform socket shims -------------------------------------------------
#ifdef _WIN32
    using sock_t = SOCKET;
    using pollfd_t = WSAPOLLFD;
    constexpr sock_t kInvalid = INVALID_SOCKET;
    inline int close_sock(sock_t s) { return ::closesocket(s); }
    inline int last_err() { return ::WSAGetLastError(); }
    inline bool would_block(int e) { return e == WSAEWOULDBLOCK; }
    inline int poll_sock(pollfd_t* fds, int n, int ms) { return ::WSAPoll(fds, static_cast<ULONG>(n), ms); }
    inline void set_nonblocking(sock_t s) { u_long m = 1; ::ioctlsocket(s, FIONBIO, &m); }
#else
    using sock_t = int;
    using pollfd_t = struct pollfd;
    constexpr sock_t kInvalid = -1;
    inline int close_sock(sock_t s) { return ::close(s); }
    inline int last_err() { return errno; }
    inline bool would_block(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
    inline int poll_sock(pollfd_t* fds, int n, int ms) { return ::poll(fds, static_cast<nfds_t>(n), ms); }
    inline void set_nonblocking(sock_t s) { int f = ::fcntl(s, F_GETFL, 0); ::fcntl(s, F_SETFL, f | O_NONBLOCK); }
#endif

inline void set_nodelay(sock_t s) { int yes = 1; ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)); }

// A self-connected loopback socket pair — identical on Windows/Linux/macOS, and (unlike a
// pipe) pollable by WSAPoll. Used to wake the poll loop for shutdown and outbound writes.
bool make_wake_pair(sock_t& read_end, sock_t& write_end) {
    sock_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalid) return false;
    int yes = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_sock(listener); return false; }
    if (::listen(listener, 1) != 0) { close_sock(listener); return false; }
    socklen_t len = sizeof(addr);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) { close_sock(listener); return false; }
    sock_t c = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == kInvalid) { close_sock(listener); return false; }
    if (::connect(c, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_sock(listener); close_sock(c); return false; }
    sock_t a = ::accept(listener, nullptr, nullptr);
    close_sock(listener);
    if (a == kInvalid) { close_sock(c); return false; }
    set_nodelay(a); set_nodelay(c);
    set_nonblocking(a); set_nonblocking(c);
    read_end = a;
    write_end = c;
    return true;
}

// ---- minimal JSON (incoming DAP requests) ----------------------------------
struct jval {
    enum class t { null, boolean, number, string, array, object };
    t kind = t::null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<jval> arr;
    std::vector<std::pair<std::string, jval>> obj;

    const jval* find(const std::string& k) const {
        if (kind != t::object) return nullptr;
        for (const auto& p : obj) if (p.first == k) return &p.second;
        return nullptr;
    }
    std::string as_str() const { return kind == t::string ? str : std::string(); }
    int as_int() const { return kind == t::number ? static_cast<int>(num) : 0; }
    double as_num() const { return kind == t::number ? num : 0; }
    std::string get_str(const std::string& k) const { auto* v = find(k); return v ? v->as_str() : std::string(); }
    double get_num(const std::string& k) const { auto* v = find(k); return v ? v->as_num() : 0; }
};

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

bool parse_string(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++]; cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                        else return false;
                    }
                    append_utf8(out, cp);   // BMP only; surrogate pairs are rare in DAP text
                    break;
                }
                default: out += e; break;
            }
        } else {
            out += c;
        }
    }
    return false;
}

bool parse_value(const std::string& s, size_t& i, jval& out, int depth) {
    if (depth > 100) return false;
    skip_ws(s, i);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') { out.kind = jval::t::string; return parse_string(s, i, out.str); }
    if (c == '{') {
        ++i; out.kind = jval::t::object;
        skip_ws(s, i);
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        for (;;) {
            skip_ws(s, i);
            std::string key;
            if (!parse_string(s, i, key)) return false;
            skip_ws(s, i);
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            jval v;
            if (!parse_value(s, i, v, depth + 1)) return false;
            out.obj.emplace_back(std::move(key), std::move(v));
            skip_ws(s, i);
            if (i >= s.size()) return false;
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; return true; }
            return false;
        }
    }
    if (c == '[') {
        ++i; out.kind = jval::t::array;
        skip_ws(s, i);
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        for (;;) {
            jval v;
            if (!parse_value(s, i, v, depth + 1)) return false;
            out.arr.push_back(std::move(v));
            skip_ws(s, i);
            if (i >= s.size()) return false;
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; return true; }
            return false;
        }
    }
    if (c == 't') { if (s.compare(i, 4, "true") == 0) { i += 4; out.kind = jval::t::boolean; out.b = true; return true; } return false; }
    if (c == 'f') { if (s.compare(i, 5, "false") == 0) { i += 5; out.kind = jval::t::boolean; out.b = false; return true; } return false; }
    if (c == 'n') { if (s.compare(i, 4, "null") == 0) { i += 4; out.kind = jval::t::null; return true; } return false; }
    // number
    size_t start = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
    if (i == start) return false;
    out.kind = jval::t::number;
    out.num = std::strtod(s.substr(start, i - start).c_str(), nullptr);
    return true;
}

bool parse_json(const std::string& s, jval& out) {
    size_t i = 0;
    return parse_value(s, i, out, 0);
}

// ---- outgoing JSON (built by hand; DAP messages are small & fixed-shape) ----
std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

size_t parse_content_length(const std::string& headers) {
    // Case-sensitive "Content-Length:" per DAP.
    size_t p = headers.find("Content-Length:");
    if (p == std::string::npos) return 0;
    p += 15;
    while (p < headers.size() && (headers[p] == ' ' || headers[p] == '\t')) ++p;
    size_t v = 0;
    bool any = false;
    while (p < headers.size() && std::isdigit(static_cast<unsigned char>(headers[p]))) { v = v * 10 + (headers[p] - '0'); ++p; any = true; }
    return any ? v : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// impl
// ---------------------------------------------------------------------------
struct debug_connector::impl {
    int requested_port = 0;
    std::string host;

    engine* engine_ = nullptr;
    controller* ctrl_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};
    bool started_ = false;

    sock_t listener_ = kInvalid;
    sock_t client_ = kInvalid;
    sock_t wake_r_ = kInvalid;
    sock_t wake_w_ = kInvalid;
    int bound_port_ = 0;

    bool session_active_ = false;
    bool want_close_client_ = false;
    std::string in_buf_;
    std::string send_buf_;

    std::mutex out_mtx_;
    std::deque<std::string> out_q_;
    std::atomic<int> out_seq_{1};
    std::atomic<int> gen_{0};   // suspension generation; encoded in frame/scope handles

#ifdef _WIN32
    bool wsa_ready_ = false;
#endif

    explicit impl(int port, std::string h) : requested_port(port), host(std::move(h)) {
#ifdef _WIN32
        WSADATA wsa;
        wsa_ready_ = (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    }

    ~impl() {
#ifdef _WIN32
        if (wsa_ready_) ::WSACleanup();
#endif
    }

    int next_seq() { return out_seq_.fetch_add(1, std::memory_order_relaxed); }

    void poke_wake() {
        if (wake_w_ == kInvalid) return;
        char b = 1;
        ::send(wake_w_, &b, 1, 0);   // 1 byte; a pending byte already wakes the loop
    }

    void drain_wake() {
        char buf[64];
        for (;;) {
            int n = ::recv(wake_r_, buf, sizeof(buf), 0);
            if (n > 0) continue;
            break;
        }
    }

    // Frame a JSON object and queue it. Thread-safe: callable from the script thread
    // (stopped events, posted-closure replies) and the connector thread alike.
    void enqueue(const std::string& json_object) {
        std::string frame = "Content-Length: " + std::to_string(json_object.size()) + "\r\n\r\n" + json_object;
        {
            std::lock_guard<std::mutex> lk(out_mtx_);
            out_q_.push_back(std::move(frame));
        }
        poke_wake();
    }

    std::string make_response(int req_seq, const std::string& cmd, bool success,
                              const std::string& body, const std::string& message = "") {
        std::string s = "{\"seq\":" + std::to_string(next_seq())
            + ",\"type\":\"response\",\"request_seq\":" + std::to_string(req_seq)
            + ",\"success\":" + (success ? "true" : "false")
            + ",\"command\":\"" + json_escape(cmd) + "\"";
        if (!message.empty()) s += ",\"message\":\"" + json_escape(message) + "\"";
        if (!body.empty()) s += ",\"body\":" + body;
        s += "}";
        return s;
    }

    std::string make_event(const std::string& event, const std::string& body) {
        std::string s = "{\"seq\":" + std::to_string(next_seq())
            + ",\"type\":\"event\",\"event\":\"" + json_escape(event) + "\"";
        if (!body.empty()) s += ",\"body\":" + body;
        s += "}";
        return s;
    }

    // ---- lifecycle ----
    bool open_listener() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == kInvalid) return false;
        int yes = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(requested_port));
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close_sock(listener_); listener_ = kInvalid; return false; }
        if (::listen(listener_, 1) != 0) { close_sock(listener_); listener_ = kInvalid; return false; }
        socklen_t len = sizeof(addr);
        if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len) == 0)
            bound_port_ = ntohs(addr.sin_port);
        set_nonblocking(listener_);
        return true;
    }

    void begin(engine* eng) {
        engine_ = eng;
        ctrl_ = &eng->debugger();
        ctrl_->set_on_stopped([this](const stop_info& si) { on_stop(si); });
        if (!open_listener()) return;
        if (!make_wake_pair(wake_r_, wake_w_)) { close_sock(listener_); listener_ = kInvalid; return; }
        running_.store(true, std::memory_order_release);
        started_ = true;
        thread_ = std::thread([this] { run(); });
    }

    void end() {
        // Wake any parked script thread and disarm, so nothing stays blocked in the hook
        // once the engine (and everything the hook touches) starts to die.
        if (ctrl_ && session_active_) { ctrl_->set_enabled(false); session_active_ = false; }
        running_.store(false, std::memory_order_release);
        poke_wake();
        if (thread_.joinable()) thread_.join();
        started_ = false;
    }

    void end_session() {
        if (session_active_) { ctrl_->set_enabled(false); session_active_ = false; }
    }

    void on_stop(const stop_info& si) {
        if (!running_.load(std::memory_order_acquire)) return;
        gen_.fetch_add(1, std::memory_order_acq_rel);
        std::string reason = si.reason.empty() ? "breakpoint" : si.reason;
        std::string body = "{\"reason\":\"" + json_escape(reason) + "\",\"threadId\":1,\"allThreadsStopped\":true}";
        enqueue(make_event("stopped", body));
    }

    // ---- connector thread ----
    void run() {
        while (running_.load(std::memory_order_acquire)) {
            drain_out_into_send_buf();

            pollfd_t fds[3];
            int n = 0, idx_wake = -1, idx_listener = -1, idx_client = -1;
            fds[n].fd = wake_r_; fds[n].events = POLLIN; fds[n].revents = 0; idx_wake = n; ++n;
            if (client_ == kInvalid) {
                fds[n].fd = listener_; fds[n].events = POLLIN; fds[n].revents = 0; idx_listener = n; ++n;
            } else {
                short ev = POLLIN;
                if (!send_buf_.empty()) ev |= POLLOUT;
                fds[n].fd = client_; fds[n].events = ev; fds[n].revents = 0; idx_client = n; ++n;
            }

            int r = poll_sock(fds, n, 1000);
            if (r < 0) { if (would_block(last_err())) continue; break; }
            if (!running_.load(std::memory_order_acquire)) break;

            if (idx_wake >= 0 && (fds[idx_wake].revents & POLLIN)) drain_wake();
            if (idx_listener >= 0 && (fds[idx_listener].revents & POLLIN)) accept_client();
            if (idx_client >= 0) {
                short re = fds[idx_client].revents;
                if (re & POLLIN) read_client();
                if (client_ != kInvalid && (re & POLLOUT)) try_send();
                if (client_ != kInvalid && (re & (POLLERR | POLLHUP | POLLNVAL))) drop_client(true);
            }
            if (want_close_client_) { drop_client(true); want_close_client_ = false; }

            drain_out_into_send_buf();
            if (client_ != kInvalid) try_send();
        }

        if (client_ != kInvalid) { close_sock(client_); client_ = kInvalid; }
        if (listener_ != kInvalid) { close_sock(listener_); listener_ = kInvalid; }
        if (wake_r_ != kInvalid) { close_sock(wake_r_); wake_r_ = kInvalid; }
        if (wake_w_ != kInvalid) { close_sock(wake_w_); wake_w_ = kInvalid; }
    }

    void accept_client() {
        sock_t c = ::accept(listener_, nullptr, nullptr);
        if (c == kInvalid) return;
        if (client_ != kInvalid) { close_sock(c); return; }   // single client
        set_nonblocking(c);
        set_nodelay(c);
        client_ = c;
        in_buf_.clear();
        send_buf_.clear();
        std::lock_guard<std::mutex> lk(out_mtx_);
        out_q_.clear();
    }

    void drop_client(bool detach) {
        if (client_ == kInvalid) return;
        if (detach) end_session();   // set_enabled(false) -> detach: a parked thread wakes
        close_sock(client_);
        client_ = kInvalid;
        in_buf_.clear();
        send_buf_.clear();
        std::lock_guard<std::mutex> lk(out_mtx_);
        out_q_.clear();
    }

    void drain_out_into_send_buf() {
        std::lock_guard<std::mutex> lk(out_mtx_);
        while (!out_q_.empty()) { send_buf_ += out_q_.front(); out_q_.pop_front(); }
    }

    void try_send() {
        while (!send_buf_.empty()) {
            int n = ::send(client_, send_buf_.data(), static_cast<int>(send_buf_.size()), 0);
            if (n > 0) { send_buf_.erase(0, static_cast<size_t>(n)); continue; }
            if (would_block(last_err())) break;
            drop_client(true);
            return;
        }
    }

    void read_client() {
        char buf[4096];
        for (;;) {
            int n = ::recv(client_, buf, sizeof(buf), 0);
            if (n > 0) { in_buf_.append(buf, buf + n); continue; }
            if (n == 0) { drop_client(true); return; }   // EOF
            if (would_block(last_err())) break;
            drop_client(true);
            return;
        }
        process_in_buf();
    }

    void process_in_buf() {
        for (;;) {
            size_t hdr_end = in_buf_.find("\r\n\r\n");
            if (hdr_end == std::string::npos) break;
            size_t cl = parse_content_length(in_buf_.substr(0, hdr_end));
            size_t body_start = hdr_end + 4;
            if (in_buf_.size() < body_start + cl) break;   // wait for the rest of the body
            std::string body = in_buf_.substr(body_start, cl);
            in_buf_.erase(0, body_start + cl);
            jval msg;
            if (parse_json(body, msg)) dispatch(msg);
        }
    }

    // ---- DAP request routing ----
    void dispatch(const jval& msg) {
        const jval* cmdv = msg.find("command");
        if (!cmdv) return;
        std::string cmd = cmdv->as_str();
        int req_seq = static_cast<int>(msg.get_num("seq"));
        const jval* args = msg.find("arguments");

        if (cmd == "initialize") {
            enqueue(make_response(req_seq, cmd, true, "{\"supportsConfigurationDoneRequest\":true}"));
            enqueue(make_event("initialized", ""));   // tell VS Code to send breakpoints + configurationDone
        } else if (cmd == "attach" || cmd == "launch") {
            ctrl_->set_enabled(true);   // arm breakpoints; suspends the wall-clock budget
            session_active_ = true;
            enqueue(make_response(req_seq, cmd, true, ""));
        } else if (cmd == "configurationDone") {
            enqueue(make_response(req_seq, cmd, true, ""));
        } else if (cmd == "setBreakpoints") {
            handle_set_breakpoints(req_seq, args);
        } else if (cmd == "setExceptionBreakpoints") {
            enqueue(make_response(req_seq, cmd, true, "{\"breakpoints\":[]}"));
        } else if (cmd == "threads") {
            enqueue(make_response(req_seq, cmd, true, "{\"threads\":[{\"id\":1,\"name\":\"script\"}]}"));
        } else if (cmd == "stackTrace") {
            post_park_only(req_seq, cmd, [this, req_seq] { reply_stack_trace(req_seq); });
        } else if (cmd == "scopes") {
            if (!generation_matches(args, "frameId")) { enqueue(make_response(req_seq, cmd, false, "", "stale frame")); return; }
            post_park_only(req_seq, cmd, [this, req_seq] { reply_scopes(req_seq); });
        } else if (cmd == "variables") {
            if (!generation_matches(args, "variablesReference")) { enqueue(make_response(req_seq, cmd, false, "", "stale reference")); return; }
            post_park_only(req_seq, cmd, [this, req_seq] { reply_variables(req_seq); });
        } else if (cmd == "continue") {
            ctrl_->resume();
            enqueue(make_response(req_seq, cmd, true, "{\"allThreadsContinued\":true}"));
        } else if (cmd == "next") {
            ctrl_->step_over();
            enqueue(make_response(req_seq, cmd, true, ""));
        } else if (cmd == "stepIn") {
            ctrl_->step_into();
            enqueue(make_response(req_seq, cmd, true, ""));
        } else if (cmd == "stepOut") {
            ctrl_->step_out();
            enqueue(make_response(req_seq, cmd, true, ""));
        } else if (cmd == "pause") {
            ctrl_->request_pause();
            enqueue(make_response(req_seq, cmd, true, ""));
        } else if (cmd == "evaluate") {
            // Phase 4 does real evaluation; answer benignly so watch/hover don't error-spam.
            enqueue(make_response(req_seq, cmd, true, "{\"result\":\"\",\"variablesReference\":0}"));
        } else if (cmd == "disconnect") {
            enqueue(make_response(req_seq, cmd, true, ""));
            want_close_client_ = true;   // detach + drop after this reply flushes; listener re-accepts
        } else {
            enqueue(make_response(req_seq, cmd, false, "", "unsupported request: " + cmd));
        }
    }

    // A park-only request needs the parked script thread to read live engine state. If not
    // stopped, answer immediately with an error rather than silently queueing a closure.
    void post_park_only(int req_seq, const std::string& cmd, std::function<void()> reply) {
        if (!ctrl_->is_paused()) { enqueue(make_response(req_seq, cmd, false, "", "not stopped")); return; }
        ctrl_->post_command(std::move(reply));
    }

    bool generation_matches(const jval* args, const char* key) const {
        if (!args) return false;
        const jval* v = args->find(key);
        if (!v) return false;
        return (v->as_int() >> 8) == gen_.load(std::memory_order_acquire);
    }

    void handle_set_breakpoints(int req_seq, const jval* args) {
        std::string path;
        std::vector<int> lines;
        if (args) {
            if (const jval* src = args->find("source"))
                if (const jval* p = src->find("path")) path = p->as_str();
            if (const jval* bps = args->find("breakpoints"); bps && bps->kind == jval::t::array)
                for (const auto& bp : bps->arr)
                    if (const jval* ln = bp.find("line")) lines.push_back(ln->as_int());
            if (lines.empty())
                if (const jval* lv = args->find("lines"); lv && lv->kind == jval::t::array)
                    for (const auto& l : lv->arr) lines.push_back(l.as_int());
        }
        ctrl_->set_breakpoints(normalize_bp_path(path), lines);
        std::string arr = "[";
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) arr += ",";
            arr += "{\"verified\":true,\"line\":" + std::to_string(lines[i]) + "}";
        }
        arr += "]";
        enqueue(make_response(req_seq, "setBreakpoints", true, "{\"breakpoints\":" + arr + "}"));
    }

    // ---- park-only replies (run on the parked script thread) ----
    void reply_stack_trace(int req_seq) {
        auto frames = ctrl_->list_frames();
        int gen = gen_.load(std::memory_order_acquire);
        std::string arr = "[";
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i) arr += ",";
            int frame_id = (gen << 8) | static_cast<int>(i + 1);
            arr += "{\"id\":" + std::to_string(frame_id)
                + ",\"name\":\"" + json_escape(frames[i].name) + "\""
                + ",\"line\":" + std::to_string(frames[i].line)
                + ",\"column\":1"
                + ",\"source\":{\"name\":\"" + json_escape(frames[i].file) + "\",\"path\":\"" + json_escape(frames[i].file) + "\"}}";
        }
        arr += "]";
        enqueue(make_response(req_seq, "stackTrace", true,
                              "{\"stackFrames\":" + arr + ",\"totalFrames\":" + std::to_string(frames.size()) + "}"));
    }

    void reply_scopes(int req_seq) {
        int gen = gen_.load(std::memory_order_acquire);
        int locals_ref = (gen << 8) | 1;
        std::string body = "{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":"
            + std::to_string(locals_ref) + ",\"expensive\":false}]}";
        enqueue(make_response(req_seq, "scopes", true, body));
    }

    void reply_variables(int req_seq) {
        auto vars = ctrl_->list_locals();
        std::string arr = "[";
        for (size_t i = 0; i < vars.size(); ++i) {
            if (i) arr += ",";
            arr += "{\"name\":\"" + json_escape(vars[i].name) + "\""
                + ",\"value\":\"" + json_escape(vars[i].value) + "\"";
            if (!vars[i].type.empty()) arr += ",\"type\":\"" + json_escape(vars[i].type) + "\"";
            arr += ",\"variablesReference\":0}";
        }
        arr += "]";
        enqueue(make_response(req_seq, "variables", true, "{\"variables\":" + arr + "}"));
    }
};

// ---------------------------------------------------------------------------
// debug_connector
// ---------------------------------------------------------------------------
debug_connector::debug_connector(int port, std::string host)
    : impl_(std::make_unique<impl>(port, std::move(host))) {}

debug_connector::~debug_connector() {
    if (impl_ && impl_->started_) impl_->end();
}

void debug_connector::start(engine* eng) {
    impl_->begin(eng);
}

void debug_connector::stop() {
    if (impl_->started_) impl_->end();
}

int debug_connector::port() const {
    return impl_->bound_port_;
}

std::shared_ptr<debug_connector> listen(engine& eng, int port, std::string host) {
    auto conn = std::make_shared<debug_connector>(port, std::move(host));
    eng.set_debug_connector(conn);
    return conn;
}

} // namespace jai::debug

#endif // JAISCRIPT_ENABLE_DEBUGGER
