#include <jaiscript/debug/controller.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/value_text.hpp>

#include <algorithm>
#include <filesystem>

namespace jai::debug {

namespace {

// Match the canonical filename stamped by execute_file/import. This kernel runs
// only on the script thread after the filesystem grant check; denied paths remain
// exact virtual labels. Keep the connector's previous fallback behavior unchanged.
std::string normalize_bp_path(const std::string& path) {
    if (path.empty()) { return path; }
    std::error_code ec;
    auto canon = std::filesystem::canonical(std::filesystem::path(path), ec);
    if (!ec) { return canon.string(); }
    canon = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    return ec ? path : canon.string();
}

} // namespace

controller::controller(engine* eng) : engine_(eng) {}

// ---------------------------------------------------------------------------
// Control-side (transport thread)
// ---------------------------------------------------------------------------

void controller::set_enabled(bool on) {
    bool was = enabled_.exchange(on, std::memory_order_acq_rel);
    if (on == was) return;
    raise_sync();
    // The backend notices at its next debug sync point (execute entry, a sleep, the
    // 1024-tick budget sample). While its hook is armed it suspends wall-clock deadline
    // checks itself and re-arms a fresh deadline when the session ends — a script parked
    // for minutes is never killed by the budget, and nothing here touches engine state
    // cross-thread. Leaving a session must never strand a parked thread or a live step mode.
    if (!on) detach();
}

void controller::set_breakpoints(const std::string& file, std::vector<int> lines) {
    publish_breakpoints(file, std::move(lines), false);
}

void controller::set_source_breakpoints(const std::string& file, std::vector<int> lines) {
    publish_breakpoints(file, std::move(lines), true);
}

void controller::publish_breakpoints(const std::string& file, std::vector<int> lines, bool source_path) {
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    auto current = breakpoints_.load(std::memory_order_acquire);
    auto next = std::make_shared<breakpoint_snapshot>(*current);
    auto& records = source_path ? next->source : next->native;
    records[file] = breakpoint_record{std::move(lines), ++next->sequence};
    breakpoints_.store(std::move(next), std::memory_order_release);
    bp_version_.fetch_add(1, std::memory_order_release);   // after publication: sync sees an at-least-as-new snapshot
    raise_sync();
}

// The flag first, then the gate: a sync that cleared the flag before this store re-raises the gate after its own write
// (sync_hot_state), so neither a sleeping thread nor an armed statement gate misses the change
void controller::raise_sync() {
    sync_requested_.store(true);
    hot_step_or_pause_.store(true);
    { std::lock_guard<std::mutex> lk(mtx_); }   // a sleeper between its predicate check and its wait gets the notify
    cv_.notify_all();
}

void controller::set_on_source_loaded(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(sources_mtx_);
    on_source_loaded_ = std::move(cb);
}

void controller::note_source(const std::string& path) {
    if (path.empty() || path == "<script>") return;
    normalize_sources_.store(engine_->is_enabled(jai::filesystem), std::memory_order_relaxed);
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard<std::mutex> lk(sources_mtx_);
        if (!sources_.insert(path).second) return;
        cb = on_source_loaded_;
    }
    if (cb) cb(path);
}

bool controller::source_known(const std::string& path) const {
    if (path == "<script>") return true;
    const std::string normalized = normalize_sources_.load(std::memory_order_relaxed) ? normalize_bp_path(path) : path;
    std::lock_guard<std::mutex> lk(sources_mtx_);
    return sources_.contains(path) || sources_.contains(normalized);
}

void controller::clear_breakpoints() {
    auto current = breakpoints_.load(std::memory_order_acquire);
    auto next = std::make_shared<breakpoint_snapshot>();
    next->sequence = current->sequence + 1;
    next->clear_sequence = next->sequence;
    breakpoints_.store(std::move(next), std::memory_order_release);
    bp_version_.fetch_add(1, std::memory_order_release);
    raise_sync();
}

void controller::request_pause() {
    pending_pause_.store(1, std::memory_order_relaxed);
    raise_sync();
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
    sync_requested_.store(true);
    hot_step_or_pause_.store(true);
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
    // Writes through add_global, which does not enforce the int/auto type ladder for
    // existing typed locals. See internal/DEBUGGER_DESIGN.md setVariable.
    engine_->add_global(name, value);
}

namespace {

// A Variables row shows the structural display text (no to_string() runs on a parked thread), cut short past this many bytes
constexpr size_t k_variable_text_limit = 1000;

// callables are noise in a Variables view
std::vector<variable_info> rendered_variables(const std::vector<std::pair<std::string, script_value>>& named) {
    std::vector<variable_info> out;
    for (const auto& [name, value] : named) {
        if (value.is_function()) continue;
        std::string type;
        if (value.is_array()) type = "array";
        else if (value.is_map()) type = "map";
        else if (value.is_object()) type = "object";
        else if (value.is_math()) type = math_type_name(value.deref().current_type());
        std::string text = detail::plain_text_value(value) ? value.to_string() : detail::value_text(value, {}, k_variable_text_limit).value();
        out.push_back(variable_info{ name, std::move(text), std::move(type) });
    }
    std::sort(out.begin(), out.end(),
              [](const variable_info& a, const variable_info& b) { return a.name < b.name; });
    return out;
}

// The scopes between the innermost environment and the program scope, an inner name hiding an outer one
std::vector<std::pair<std::string, script_value>> block_scope_variables(const environment* env, const environment* program) {
    std::vector<std::pair<std::string, script_value>> out;
    std::unordered_set<std::string_view> seen;
    for (; env && env != program; env = env->get_parent().get()) {
        for (const auto& [name, value] : env->get_local_variables()) {
            if (seen.insert(name).second) { out.emplace_back(std::string(name), value); }
        }
    }
    return out;
}

} // namespace

std::vector<frame_info> controller::list_frames() const {
    std::vector<frame_info> out;
    if (auto* backend = engine_->get_execution_backend()) {
        for (const auto& f : backend->debug_frames()) {
            out.push_back(frame_info{ f.function, f.file, static_cast<int>(f.line) });
        }
    }
    if (out.empty()) { out.push_back(frame_info{ "<script>", {}, 0 }); }
    // the innermost frame stands where it stopped (a statement that holds the next one stops first)
    out.front().file = current_stop_.file;
    out.front().line = current_stop_.line;
    return out;
}

std::vector<variable_info> controller::list_locals(size_t frame) const {
    auto* backend = engine_->get_execution_backend();
    if (!backend) return {};
    // Function locals live in the call frame's slot vector, which the environment's flat map
    // does NOT hold — ask the backend to name them from the frame's function AST. The top
    // level's block scopes are environments, reachable only while it is the innermost frame.
    const auto frames = backend->debug_frames();
    const bool top_level = frames.empty() || (frame + 1 == frames.size() && frames.back().function == "<script>");
    if (!top_level) return rendered_variables(backend->get_frame_locals(frame));
    if (frame != 0) return {};
    const auto env = backend->get_current_environment();
    const auto program = engine_->get_global_environment();
    auto named = block_scope_variables(env.get(), program.get());
    for (auto& slot_local : backend->get_frame_locals(frame)) { named.push_back(std::move(slot_local)); }   // the VM's program frame keeps block-scoped locals in slots
    return rendered_variables(named);
}

std::vector<variable_info> controller::list_globals() const {
    std::vector<std::pair<std::string, script_value>> named;
    if (const auto program = engine_->get_global_environment()) {
        for (const auto& [name, value] : program->get_local_variables()) { named.emplace_back(std::string(name), value); }
    }
    return rendered_variables(named);
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
    sync_requested_.exchange(false);   // before the reads: a change raised after it syncs again
    if (!enabled_.load(std::memory_order_acquire)) {
        hot_step_or_pause_.store(false, std::memory_order_relaxed);   // the gate is not consulted while disarmed
        hot_line_bloom_ = nullptr;
        hot_line_bloom_storage_.clear();
        // Disarming hides the hot cache, not the installed table or pending edits.
        hot_bp_version_ = ~0ull;   // a re-armed session rebuilds even an unchanged bloom
        return false;
    }
    hot_step_or_pause_.store(pending_pause_.load(std::memory_order_relaxed) != 0
        || step_mode_.load(std::memory_order_relaxed) != static_cast<int>(step_mode::none));
    if (sync_requested_.load()) { hot_step_or_pause_.store(true); }   // raised between the exchange and the store above
    normalize_sources_.store(engine_->is_enabled(jai::filesystem), std::memory_order_relaxed);
    const uint64_t version = bp_version_.load(std::memory_order_acquire);
    if (version != hot_bp_version_) {
        auto snapshot = breakpoints_.load(std::memory_order_acquire);
        auto installed = hot_bps_;
        if (!installed || snapshot->sequence != applied_bp_sequence_) {
            auto next = installed ? std::make_shared<breakpoint_table>(*installed)
                                  : std::make_shared<breakpoint_table>();
            if (snapshot->clear_sequence > applied_bp_sequence_) next->clear();

            struct pending_record {
                const std::string* file;
                const breakpoint_record* record;
                bool source_path;
            };
            std::vector<pending_record> changed;
            auto collect = [&](const auto& records, bool source_path) {
                for (const auto& [file, record] : records) {
                    if (record.sequence > applied_bp_sequence_) {
                        changed.push_back({&file, &record, source_path});
                    }
                }
            };
            collect(snapshot->native, false);
            collect(snapshot->source, true);
            std::sort(changed.begin(), changed.end(), [](const auto& a, const auto& b) {
                return a.record->sequence < b.record->sequence;
            });
            for (const auto& change : changed) {
                const std::string file = change.source_path && engine_->is_enabled(jai::filesystem)
                    ? normalize_bp_path(*change.file) : *change.file;
                if (change.record->lines.empty()) next->erase(file);
                else (*next)[file] = change.record->lines;
            }
            installed = std::move(next);
        }

        // Commit table, bloom and sequence together after all fallible work. A
        // failed rebuild leaves the previous installed table available for retry.
        std::vector<uint64_t> bloom;
        if (!installed->empty()) {
            bloom.assign(line_bloom_bits / 64, 0);
            for (const auto& [file, lines] : *installed) {
                for (int line : lines) {
                    const uint32_t b = static_cast<uint32_t>(line) & (line_bloom_bits - 1);
                    bloom[b >> 6] |= (uint64_t{1} << (b & 63u));
                }
            }
        }
        hot_bps_ = std::move(installed);
        hot_line_bloom_storage_.swap(bloom);
        hot_line_bloom_ = hot_line_bloom_storage_.empty() ? nullptr : hot_line_bloom_storage_.data();
        applied_bp_sequence_ = snapshot->sequence;
        hot_bp_version_ = version;
    }
    return true;
}

void controller::swap_step_state(int& mode, int& base_depth, int& stop_depth) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    mode = step_mode_.exchange(mode, std::memory_order_relaxed);
    base_depth = step_base_depth_.exchange(base_depth, std::memory_order_relaxed);
    stop_depth = current_stop_depth_.exchange(stop_depth, std::memory_order_relaxed);
    hot_step_or_pause_.store(enabled_.load(std::memory_order_acquire) && (pending_pause_.load(std::memory_order_relaxed) != 0
        || step_mode_.load(std::memory_order_relaxed) != static_cast<int>(step_mode::none) || sync_requested_.load()), std::memory_order_relaxed);
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

bool controller::stops_at(const ast_node* node) noexcept {
    switch (node->get_type()) {
        case node_type::block_stmt:
        case node_type::case_stmt:
        case node_type::default_stmt:
        case node_type::statement_decl:
            return false;
        default:
            return true;
    }
}

void controller::on_statement(const ast_node* node, int depth) {
    // Reentrancy: a repl/inspection closure that runs script code must not itself trip
    // breakpoints or stepping (we'd park recursively). Same thread, so a plain flag.
    if (in_command_) return;
    // An armed session's next statement applies a pending breakpoint edit, pause or detach
    if (sync_requested_.load(std::memory_order_relaxed)) [[unlikely]] { engine_->get_execution_backend()->sync_debug_hook(); }
    if (!stops_at(node)) return;
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
    // breakpoints edited during the pause must be live from the very next statement,
    // and a detach during the pause disarms the backend's gate at once.
    engine_->get_execution_backend()->sync_debug_hook();
}

void controller::service() {
    if (!sync_requested_.load(std::memory_order_relaxed)) { return; }
    sleep_for(std::chrono::nanoseconds{0});
}

void controller::sleep_for(std::chrono::nanoseconds duration) {
    using clock = std::chrono::steady_clock;
    auto until = clock::now() + duration;
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!cv_.wait_until(lk, until, [&] { return sync_requested_.load(); })) return;
        }
        auto* backend = engine_->get_execution_backend();
        backend->sync_debug_hook();
        if (in_command_ || !enabled_.load(std::memory_order_relaxed) || !backend->is_executing()) continue;
        if (!pending_pause_.exchange(0, std::memory_order_relaxed)) continue;
        const auto remaining = until - clock::now();
        const auto frames = backend->debug_frames();
        park({"pause", frames.empty() ? std::string() : frames.front().file,
              frames.empty() ? 0 : static_cast<int>(frames.front().line), backend->debug_depth()});
        until = clock::now() + remaining;   // the pause does not eat into the sleep
    }
}

} // namespace jai::debug
