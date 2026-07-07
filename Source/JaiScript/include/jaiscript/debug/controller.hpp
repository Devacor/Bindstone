#pragma once

// JaiScript step-debugger core. Transport-agnostic and protocol-agnostic: it owns the
// execution hook, the breakpoint table, the step state machine, and the park/pump loop
// that blocks the script thread at a stop while a *different* thread (a DAP connector,
// or a test) drives resume/step and inspects state via posted closures. Pure std + an
// engine*; no sockets, no DAP. See docs/DEBUGGER_DESIGN.md (Concurrency & lifecycle).

#include <jaiscript/core/value.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace jai {
    class engine;
    class ast_node;

namespace debug {

enum class step_mode : int { none = 0, into, over, out };

struct stop_info {
    std::string reason;   // "breakpoint" | "step" | "pause"
    std::string file;
    int line = 0;
    int depth = 0;
};

// file path -> the lines that carry a breakpoint (sorted, deduped)
using breakpoint_table = std::unordered_map<std::string, std::vector<int>>;

// One stack frame at a stop (parked-thread inspection result).
struct frame_info {
    std::string name;   // display name (phase 3: "script")
    std::string file;
    int line = 0;
};

// One in-scope variable at a stop (name + rendered value + coarse type).
struct variable_info {
    std::string name;
    std::string value;
    std::string type;   // "array" | "map" | "object" | "" (scalar)
};

// Optional transport an engine drives its controller through (a DAP socket server, a
// test harness, ...). Kept abstract in core so the always-built library never links a
// socket: jai::debug::debug_connector (optional, JAISCRIPT_ENABLE_DEBUGGER) derives from it.
class transport {
public:
    virtual ~transport() = default;
    virtual void start(engine* eng) = 0;   // begin serving; wire to eng->debugger()
    virtual void stop() = 0;               // detach the controller, join, release resources
};

// One controller per engine (engine::debugger()). All "control-side" methods are safe to
// call from a thread other than the one running the script; inspection methods must run
// on the parked script thread (post them via post_command()).
class controller {
public:
    explicit controller(engine* eng);

    // Cheap hot-path gate read by the interpreter hook every statement.
    bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
    bool is_paused() const noexcept { return paused_.load(std::memory_order_acquire); }

    // ---- control-side (transport thread) ----
    // Arming also suspends the execution budget (a paused script must not trip the
    // wall-clock cap) and restores it on disable.
    void set_enabled(bool on);
    void set_breakpoints(const std::string& file, std::vector<int> lines);
    void clear_breakpoints();
    void request_pause();                              // stop at the next statement
    void resume();                                     // continue
    void step_over();
    void step_into();
    void step_out();
    void post_command(std::function<void()> fn);       // run on the parked thread; wakes it
    void detach();                                     // force-resume + disarm (disconnect/shutdown)
    void set_on_stopped(std::function<void(const stop_info&)> cb) { on_stopped_ = std::move(cb); }

    // ---- inspection: call ONLY on the parked script thread (via post_command) ----
    script_value get_variable(const std::string& name) const;   // live scope at the stop
    void set_variable(const std::string& name, const script_value& value);
    std::vector<frame_info> list_frames() const;   // stack at the stop (phase 3: top frame only)
    std::vector<variable_info> list_locals() const;// in-scope locals at the stop (callables hidden)

    // ---- script/hot path: the interpreter statement hook ----
    void on_statement(const ast_node* node, int depth);

private:
    void park(const stop_info& si);
    bool should_stop(const ast_node* node, int depth, std::string& reason_out) const;
    bool breakpoint_hit(const std::string& file, int line) const;

    engine* engine_;

    // Read on the hot path without the mutex.
    std::atomic<bool> enabled_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> pending_pause_{0};
    std::atomic<int> step_mode_{static_cast<int>(step_mode::none)};
    std::atomic<int> step_base_depth_{0};
    std::atomic<int> current_stop_depth_{0};   // depth of the current stop (step over/out base)
    // Immutable snapshot swapped atomically: the hot path loads it lock-free while the
    // transport thread publishes a fresh table on setBreakpoints.
    std::atomic<std::shared_ptr<const breakpoint_table>> breakpoints_{std::make_shared<breakpoint_table>()};

    // park/pump handshake — one mutex, one cv, predicate wait (no lost wakeups).
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool resume_requested_ = false;
    std::deque<std::function<void()>> commands_;
    bool in_command_ = false;   // reentrancy guard (touched only on the parked thread)

    std::function<void(const stop_info&)> on_stopped_;
    double saved_budget_ = 0.0;
    stop_info current_stop_;   // last stop; read only on the parked script thread
};

} // namespace debug
} // namespace jai
