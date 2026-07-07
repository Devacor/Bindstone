#include <jaiscript/debug/controller.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>

#include <algorithm>

namespace jai::debug {

controller::controller(engine* eng) : engine_(eng) {}

// ---------------------------------------------------------------------------
// Control-side (transport thread)
// ---------------------------------------------------------------------------

void controller::set_enabled(bool on) {
    bool was = enabled_.exchange(on, std::memory_order_acq_rel);
    if (on == was) return;
    // The interpreter notices at its next debug sync point (execute entry / the
    // 1024-tick budget sample). While its hook is armed it suspends wall-clock deadline
    // checks itself and re-arms a fresh deadline when the session ends — a script parked
    // for minutes is never killed by the budget, and nothing here touches engine state
    // cross-thread. Leaving a session must never strand a parked thread or a live step mode.
    if (!on) detach();
}

void controller::set_breakpoints(const std::string& file, std::vector<int> lines) {
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    auto current = breakpoints_.load(std::memory_order_acquire);
    auto next = std::make_shared<breakpoint_table>(*current);
    if (lines.empty()) next->erase(file);
    else (*next)[file] = std::move(lines);
    breakpoints_.store(std::move(next), std::memory_order_release);
    bp_version_.fetch_add(1, std::memory_order_release);   // after the table: sync sees version -> at-least-as-new table
}

void controller::clear_breakpoints() {
    breakpoints_.store(std::make_shared<breakpoint_table>(), std::memory_order_release);
    bp_version_.fetch_add(1, std::memory_order_release);
}

void controller::request_pause() {
    pending_pause_.store(1, std::memory_order_relaxed);
}

void controller::resume() {
    std::lock_guard<std::mutex> lk(mtx_);
    step_mode_.store(static_cast<int>(step_mode::none), std::memory_order_relaxed);
    resume_requested_ = true;
    cv_.notify_all();
}

void controller::step_over() {
    std::lock_guard<std::mutex> lk(mtx_);
    step_base_depth_.store(current_stop_depth_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    step_mode_.store(static_cast<int>(step_mode::over), std::memory_order_relaxed);
    resume_requested_ = true;
    cv_.notify_all();
}

void controller::step_into() {
    std::lock_guard<std::mutex> lk(mtx_);
    step_mode_.store(static_cast<int>(step_mode::into), std::memory_order_relaxed);
    resume_requested_ = true;
    cv_.notify_all();
}

void controller::step_out() {
    std::lock_guard<std::mutex> lk(mtx_);
    step_base_depth_.store(current_stop_depth_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    step_mode_.store(static_cast<int>(step_mode::out), std::memory_order_relaxed);
    resume_requested_ = true;
    cv_.notify_all();
}

void controller::post_command(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(mtx_);
    commands_.push_back(std::move(fn));
    cv_.notify_all();
}

void controller::detach() {
    std::lock_guard<std::mutex> lk(mtx_);
    step_mode_.store(static_cast<int>(step_mode::none), std::memory_order_relaxed);
    pending_pause_.store(0, std::memory_order_relaxed);
    resume_requested_ = true;
    cv_.notify_all();
}

// ---------------------------------------------------------------------------
// Inspection (parked thread only)
// ---------------------------------------------------------------------------

script_value controller::get_variable(const std::string& name) const {
    return engine_->get_variable(name);
}

void controller::set_variable(const std::string& name, const script_value& value) {
    // NOTE (phase 3.1): writes through add_global, which does not enforce the int/auto
    // type ladder for existing typed locals. Phase-4 routes edits through
    // enforce_type_compatibility before the store. See docs/DEBUGGER_DESIGN.md setVariable.
    engine_->add_global(name, value);
}

std::vector<frame_info> controller::list_frames() const {
    // Phase 3: the top frame — the stop location. Multi-frame walk of the interpreter's
    // call_stack_ is the immediate follow-up (see docs/DEBUGGER_DESIGN.md phasing).
    return { frame_info{ "script", current_stop_.file, current_stop_.line } };
}

std::vector<variable_info> controller::list_locals() const {
    std::vector<variable_info> out;
    auto* backend = engine_->get_execution_backend();
    if (!backend) return out;

    // Function locals live in the call frame's slot vector, which the environment's flat map
    // does NOT hold — ask the backend to name them from the frame's function AST. At global
    // scope (no frame) that is empty, so fall back to the environment's named locals/globals.
    auto named = backend->get_current_frame_locals();
    if (named.empty()) {
        if (auto env = backend->get_current_environment()) {
            for (const auto& [name, value] : env->get_local_variables())
                named.emplace_back(std::string(name), value);
        }
    }

    for (const auto& [name, value] : named) {
        if (value.is_function()) continue;   // callables are noise in a Locals view
        std::string type;
        if (value.is_array()) type = "array";
        else if (value.is_map()) type = "map";
        else if (value.is_object()) type = "object";
        out.push_back(variable_info{ name, value.to_string(), std::move(type) });
    }
    std::sort(out.begin(), out.end(),
              [](const variable_info& a, const variable_info& b) { return a.name < b.name; });
    return out;
}

// ---------------------------------------------------------------------------
// Hot path
// ---------------------------------------------------------------------------

// Runs only when wants_statement() passed (stepping, or the line bloom matched) — the
// snapshot is the script thread's own cache, so no atomics/locks even here. The bloom
// re-check keeps a step-over across a hot region from hashing the filename per statement.
bool controller::breakpoint_hit(const std::string& file, int line) const {
    const uint64_t* bloom = hot_line_bloom_;
    if (!bloom) return false;
    const uint32_t b = static_cast<uint32_t>(line) & (line_bloom_bits - 1);
    if (!((bloom[b >> 6] >> (b & 63u)) & 1u)) return false;
    auto it = hot_bps_->find(file);
    if (it == hot_bps_->end()) return false;
    return std::binary_search(it->second.begin(), it->second.end(), line);
}

bool controller::sync_hot_state() {
    if (!enabled_.load(std::memory_order_acquire)) {
        hot_step_or_pause_ = false;
        hot_line_bloom_ = nullptr;
        hot_line_bloom_storage_.clear();
        hot_bps_.reset();
        hot_bp_version_ = ~0ull;   // a re-armed session re-caches even an unchanged table
        return false;
    }
    hot_step_or_pause_ = pending_pause_.load(std::memory_order_relaxed) != 0
        || step_mode_.load(std::memory_order_relaxed) != static_cast<int>(step_mode::none);
    const uint64_t version = bp_version_.load(std::memory_order_acquire);
    if (version != hot_bp_version_) {
        hot_bps_ = breakpoints_.load(std::memory_order_acquire);
        bool any = false;
        for (const auto& [file, lines] : *hot_bps_) {
            if (!lines.empty()) { any = true; break; }
        }
        if (any) {
            hot_line_bloom_storage_.assign(line_bloom_bits / 64, 0);
            for (const auto& [file, lines] : *hot_bps_) {
                for (int line : lines) {
                    const uint32_t b = static_cast<uint32_t>(line) & (line_bloom_bits - 1);
                    hot_line_bloom_storage_[b >> 6] |= (uint64_t{1} << (b & 63u));
                }
            }
        } else {
            hot_line_bloom_storage_.clear();
        }
        hot_line_bloom_ = hot_line_bloom_storage_.empty() ? nullptr : hot_line_bloom_storage_.data();
        hot_bp_version_ = version;
    }
    return true;
}

bool controller::should_stop(const ast_node* node, int depth, std::string& reason_out) const {
    if (pending_pause_.load(std::memory_order_relaxed)) {
        reason_out = "pause";
        return true;
    }
    switch (static_cast<step_mode>(step_mode_.load(std::memory_order_relaxed))) {
        case step_mode::into: reason_out = "step"; return true;
        case step_mode::over:
            if (depth <= step_base_depth_.load(std::memory_order_relaxed)) { reason_out = "step"; return true; }
            break;
        case step_mode::out:
            if (depth < step_base_depth_.load(std::memory_order_relaxed)) { reason_out = "step"; return true; }
            break;
        case step_mode::none: break;
    }
    if (breakpoint_hit(node->location.filename, static_cast<int>(node->location.line))) {
        reason_out = "breakpoint";
        return true;
    }
    return false;
}

void controller::on_statement(const ast_node* node, int depth) {
    // Reentrancy: a repl/inspection closure that runs script code must not itself trip
    // breakpoints or stepping (we'd park recursively). Same thread, so a plain flag.
    if (in_command_) return;
    // A detach can race the cached gate; never park for a session that just ended.
    if (!enabled_.load(std::memory_order_relaxed)) return;
    std::string reason;
    if (!should_stop(node, depth, reason)) return;
    // Consume a one-shot pause so it fires exactly once.
    pending_pause_.store(0, std::memory_order_relaxed);
    park({reason, node->location.filename, static_cast<int>(node->location.line), depth});
}

void controller::park(const stop_info& si) {
    current_stop_ = si;   // read by list_frames/list_locals via posted closures (this thread)
    current_stop_depth_.store(si.depth, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        resume_requested_ = false;
    }
    paused_.store(true, std::memory_order_release);
    if (on_stopped_) on_stopped_(si);   // notify transport (runs on this, the script thread)

    std::unique_lock<std::mutex> lk(mtx_);
    for (;;) {
        cv_.wait(lk, [&] { return resume_requested_ || !commands_.empty(); });
        while (!commands_.empty()) {
            auto fn = std::move(commands_.front());
            commands_.pop_front();
            lk.unlock();
            in_command_ = true;
            fn();               // inspection/edit against live engine state, on this thread
            in_command_ = false;
            lk.lock();
        }
        if (resume_requested_) break;
    }
    paused_.store(false, std::memory_order_release);
    lk.unlock();
    // Park exit is a debug sync point: a step mode set by the resume side and any
    // breakpoints edited during the pause must be live from the very next statement.
    sync_hot_state();
}

} // namespace jai::debug
