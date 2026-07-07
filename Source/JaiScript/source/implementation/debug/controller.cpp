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
    if (on) {
        // A script parked at a breakpoint must not be killed by the wall-clock budget.
        // Suspend it for the whole session; restore on disable. (Phase-4 will replace
        // this blunt suspend with a per-resume re-arm; see docs/DEBUGGER_DESIGN.md.)
        saved_budget_ = engine_->execution_budget();
        engine_->execution_budget(0.0);
    } else {
        engine_->execution_budget(saved_budget_);
        // Leaving a session must never strand a parked thread or a live step mode.
        detach();
    }
}

void controller::set_breakpoints(const std::string& file, std::vector<int> lines) {
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    auto current = breakpoints_.load(std::memory_order_acquire);
    auto next = std::make_shared<breakpoint_table>(*current);
    if (lines.empty()) next->erase(file);
    else (*next)[file] = std::move(lines);
    breakpoints_.store(std::move(next), std::memory_order_release);
}

void controller::clear_breakpoints() {
    breakpoints_.store(std::make_shared<breakpoint_table>(), std::memory_order_release);
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

bool controller::breakpoint_hit(const std::string& file, int line) const {
    auto table = breakpoints_.load(std::memory_order_acquire);
    auto it = table->find(file);
    if (it == table->end()) return false;
    return std::binary_search(it->second.begin(), it->second.end(), line);
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
}

} // namespace jai::debug
