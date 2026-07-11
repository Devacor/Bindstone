#pragma once

#ifndef __JAISCRIPT_VM_VM_BACKEND_HPP__
#define __JAISCRIPT_VM_VM_BACKEND_HPP__

#include "chunk.hpp"
#include "vm_compiler.hpp"
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace jai {
    class script_class_definition;
    namespace detail { struct caller_frame_view; }
}

namespace jai::vm {

    class vm_backend : public execution_backend {
    public:
        vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env);
#ifdef JAISCRIPT_VM_PROFILE
        ~vm_backend() override { dump_opcode_profile(); }
        void dump_opcode_profile() const;
        uint64_t profile_cycles_[256] = {};
        uint64_t profile_counts_[256] = {};
        std::map<std::string, uint64_t> profile_native_callees_;   // in-loop fast-path misses by name
        // exec_decl_var / exec_store fast-path miss taxonomy (why the hot rows average
        // hundreds of cycles): [0]=hit [1]=flags/slot gate [2]=reference top [3]=storage
        // kind [4]=declared-type mismatch
        uint64_t profile_decl_paths_[5] = {};
        uint64_t profile_store_paths_[5] = {};
#else
        ~vm_backend() override = default;
#endif

        script_value execute(const std::vector<declaration_ptr>& declarations) override;
        script_value execute(const std::vector<declaration_ptr>& declarations, std::shared_ptr<void>& compiled_slot) override;
        void prepare_for_execution() override;
        checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args) override;

        script_value get_variable(const std::string& name) const override;
        void push_external_call_scope() override;
        void pop_external_call_scope() override;
        bool has_variable(const std::string& name) const override;

        void push_scope() override;
        void pop_scope() override;
        void define_variable(const std::string& name, const script_value& value) override;

        void set_execution_budget(std::chrono::nanoseconds budget) override;
        void set_has_custom_numeric_ops(bool value) override { has_custom_numeric_ops_ = value; }
        void set_has_custom_binary_ops(bool value) override { has_custom_binary_ops_ = value; }
        void set_operator_table(const detail::engine_operator_table* table) override { operator_table_ = table; }
        void set_subscript_resolver(std::function<checked_result<script_value>(const std::vector<script_value>&)> resolver) override {
            subscript_resolver_ = std::move(resolver);
        }
        void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) override {
            class_lookup_ = std::move(callback);
        }
        void set_engine_reference(engine* engine_ref) override;

        // Phase 5 (live): run_dispatch detects statement boundaries via chunk::stmt_nodes
        // and drives the SAME controller as the interpreter. The per-op path tests one
        // plain cached pointer (debug_hook_); transport state is pulled in only at the
        // debug sync points (prepare_for_execution, the 1024-tick budget sample, park exit).
        void set_debug_controller(debug::controller* controller) override {
            debug_controller_.store(controller, std::memory_order_release);
        }

        // Innermost live environment (debugger locals fallback / globals scope).
        std::shared_ptr<environment> get_current_environment() const override { return environment_; }

        // Named slot locals of the parked frame, reconstructed lazily from the chunk's
        // decl/load/store operands (no name table baked into release chunks — Q6 ruling).
        std::vector<std::pair<std::string, script_value>> get_current_frame_locals() const override;

        bool is_unwinding() const override { return is_unwinding_; }
        const script_exception& get_current_exception() const override { return current_exception_.value(); }

        // Host-boundary discriminator (same rule as resume_coroutine's host-level
        // detection): false only when no frames are live and depth is zero
        bool is_executing() const override { return current_call_depth_ != 0 || !frames_.empty(); }

        std::vector<stack_frame> last_stack_trace() const override { return captured_trace_; }
        std::string format_stack_trace() const override;


        checked_result<script_value> resume_coroutine(coroutine_handle& handle) override;

        std::string get_backend_name() const override { return "vm"; }

        // Compiles without executing (disassembler/tests)
        std::shared_ptr<chunk> compile_only(const std::vector<declaration_ptr>& declarations);

        // === Parallel worker setup (parallel_transform v0) ===
        // Pins this instance to a worker context built at the region barrier: root
        // environment (parent = nullptr, the hard partition), a private env-epoch sink
        // (worker env churn never bumps the shared engine epoch), the worker's own
        // limits, and an armed budget clock. cached_global_env_ goes null so the per-ip
        // env-lookup caches never engage (workers never run top-level frames either) -
        // no epoch reads, no cache writes into chunks. Workers never call
        // prepare_for_execution, so nothing here is ever repointed at shared state.
        void configure_parallel_worker(std::shared_ptr<environment> root_env,
                                       string_symbolizer* env_epoch_sink,
                                       detail::execution_limits* worker_limits,
                                       std::chrono::nanoseconds budget) {
            environment_ = std::move(root_env);
            cached_global_env_ = nullptr;
            env_symbolizer_ = env_epoch_sink;
            limits_ = worker_limits;
            scope_env_pool_.clear();   // pooled envs minted earlier carry the shared sink
            // Slot REUSE reset (worker contexts persist across regions): clear every
            // piece of residual execution state from a prior region's chunk.
            stack_.clear();
            frames_.clear();
            call_records_top_ = 0;
            try_records_.clear();
            iter_states_.clear();
            cfor_states_.clear();
            pending_callees_.clear();
            current_call_depth_ = 0;
            is_unwinding_ = false;
            current_exception_.reset();
            active_exception_value_.reset();
            trace_captured_ = false;
            captured_trace_.clear();
            return_value_.reset();
            has_return_value_ = false;
            implicit_result_.reset();
            yielding_ = false;
            switch_to_ = nullptr;
            pending_site_ctx_ = {};
            external_site_stack_.clear();
            execution_budget_ = budget;
            parallel_worker_ = true;
            arm_execution_deadline();
        }

        // True on worker-configured instances only: the script-class method dispatch
        // wall (methods aren't provisioned per worker - their shared body caches and
        // arbitrary env reach are unsafe off-thread) checks this, gated by
        // engine::allow_unsafe_parallel. Never set on the engine's own backend.
        bool parallel_worker_ = false;

        // Compile a per-worker function copy's body on the MAIN thread at the region
        // barrier (compilation interns; workers must only ever hit) and pin the chunk on
        // the copy's own backend_body_cache.
        void precompile_parallel_function(const script_defined_function& fn) {
            fn.backend_body_cache = chunk_for_body(fn.name, fn.parameters(), fn.body, fn.local_count);
        }

    private:
        struct frame {
            chunk* code = nullptr;
            std::shared_ptr<chunk> pin;
            size_t ip = 0;
            call_frame* locals = nullptr;
            std::shared_ptr<environment> entry_env;
            size_t stack_base = 0;
            bool top_level = false;
            // Stage-2 frame window: slot k lives at stack_[window_base + k] when
            // window_backed (record frames + native entries). window_live replicates
            // call_frame's grow-on-declare semantics: reads at/above it answer nullptr
            // ("reserved capacity only: not live yet"). Fiber frames stay call_frame-
            // backed (window_backed false) until per-fiber stacks land (stage 4).
            size_t window_base = 0;
            uint32_t window_live = 0;
            bool window_backed = false;
            // Debug statement-boundary edge state (touched only while a session is armed).
            // Stale values across record reuse are harmless: a fresh frame enters at ip 0,
            // and ip <= debug_stmt_ip re-fires the boundary (also the loop back-edge case).
            const ast_node* debug_stmt = nullptr;
            size_t debug_stmt_ip = 0;
        };

        struct frame_guard;

        // Flat value stack with a growth CHOKEPOINT (stage 2): the vm caches raw
        // script_value* into frame windows across dispatch iterations (counted-for fast
        // states — the ONE sanctioned cross-op raw-pointer cache, invariants §2b), so
        // every reallocation rebases them (rebase_window_pointers). Deliberately flat,
        // NOT segmented: operand push/pop is the hot-loop path and must stay pure index
        // arithmetic. push_back self-alias (op_dup pushing back()) is grow-safe: the
        // value detours through a temp on the grow branch only.
        struct value_stack {
            std::vector<script_value> v;
            vm_backend* owner = nullptr;
            void push_back(const script_value& x) {
                if (v.size() == v.capacity()) [[unlikely]] { grow_push(script_value(x)); return; }
                v.push_back(x);
            }
            void push_back(script_value&& x) {
                if (v.size() == v.capacity()) [[unlikely]] { grow_push(std::move(x)); return; }
                v.push_back(std::move(x));
            }
            void grow_push(script_value x);   // out of line: reserve + rebase + push
            script_value& back() noexcept { return v.back(); }
            const script_value& back() const noexcept { return v.back(); }
            void pop_back() noexcept { v.pop_back(); }
            size_t size() const noexcept { return v.size(); }
            bool empty() const noexcept { return v.empty(); }
            void clear() noexcept { v.clear(); }
            void reserve(size_t n) { v.reserve(n); }   // startup only (no rebase: nothing live)
            script_value& operator[](size_t i) noexcept { return v[i]; }
            const script_value& operator[](size_t i) const noexcept { return v[i]; }
            std::vector<script_value>::iterator begin() noexcept { return v.begin(); }
            std::vector<script_value>::iterator end() noexcept { return v.end(); }
            void erase(std::vector<script_value>::iterator first, std::vector<script_value>::iterator last) {
                v.erase(first, last);   // truncations only; never grows
            }
            std::vector<script_value>& vec() noexcept { return v; }
            const std::vector<script_value>& vec() const noexcept { return v; }
        };
        // Rebase every sanctioned raw pointer into the old stack buffer (counted-for
        // fast states) onto the new one; called only from value_stack::grow_push.
        void rebase_window_pointers(const script_value* old_begin, const script_value* old_end,
                                    script_value* new_begin);

        // === Slot-accessor funnel (stage 2a) — stage-2 window implementation ===
        // EVERY read/write of a frame's slot-local storage in this backend goes through
        // these. Window-backed frames address stack_[window_base + slot]; legacy frames
        // (fibers) forward to call_frame. Callers keep their own slot-validity/top_level
        // guards — these assume a live locals home.
        script_value* frame_slot(frame& f, size_t slot) noexcept {
            if (f.window_backed) {
                return slot < f.window_live ? &stack_[f.window_base + slot] : nullptr;
            }
            return f.locals->get_local(slot);
        }
        const script_value* frame_slot(const frame& f, size_t slot) const noexcept {
            if (f.window_backed) {
                return slot < f.window_live ? &stack_[f.window_base + slot] : nullptr;
            }
            return static_cast<const call_frame*>(f.locals)->get_local(slot);
        }
        void frame_slot_set(frame& f, size_t slot, script_value value) {
            if (f.window_backed) {
                const size_t idx = f.window_base + slot;
                if (idx < stack_.size()) {
                    stack_[idx] = std::move(value);
                } else {
                    // Build-up writes (pooled-arg binding, defaults): fill the gap with
                    // live nulls exactly like call_frame::set_local's slot>size path
                    while (stack_.size() < idx) { stack_.push_back(script_value(std::monostate{}, value.get_engine())); }
                    stack_.push_back(std::move(value));
                }
                if (slot >= f.window_live) { f.window_live = static_cast<uint32_t>(slot + 1); }
                return;
            }
            f.locals->set_local(slot, std::move(value));
        }
        size_t frame_slot_count(const frame& f) const noexcept {
            return f.window_backed ? f.window_live : f.locals->local_count();
        }
        // Storage-handle twins for paths that hold a call_frame slot home directly
        // (coroutine fiber state, the caller side of ref-param binds on fiber frames).
        static script_value* frame_slot(call_frame& locals, size_t slot) noexcept {
            return locals.get_local(slot);
        }
        static void frame_slot_set(call_frame& locals, size_t slot, script_value value) {
            locals.set_local(slot, std::move(value));
        }

        // Return-epilogue classification, cached per script_defined_function
        // (backend_return_conv) and stamped on the record at push. unclassified routes
        // through the legacy decision (method frames); the prim_* classes pass a
        // storage-matching result through verbatim — exactly what the full path's
        // identity try_convert produces for primitives.
        enum class return_conv : uint8_t {
            unclassified = 0,
            none,          // untyped / any / void / auto: deref-only epilogue
            ref,           // reference return: pass-through kernel (full route)
            prim_int,
            prim_float,
            prim_bool,
            prim_char,
            prim_string,
            check,         // typed: full convert_return_value route
        };
        static return_conv classify_return_conv(const type_info_ptr& t);

        // One logical script->script activation executed inside the current native run()
        // (Squirrel EnterFrame shape). Records live behind unique_ptr in a grow-only pool
        // so &rec.f stays address-stable (frames_ entries, try_record.owner and rec.caller
        // hold raw frame*).
        struct call_record {
            frame f;                                   // frames_ gets &f; f.locals = &locals
            call_frame locals;                         // record-owned slot storage; vector capacity reused across calls
            script_value callee_pin{std::monostate{}, nullptr};   // moved-off-stack callee value: pins the callable
            strong_ptr<script_function> direct_pin;    // probe-called frames: pin without a callee value/slot
            type_info_ptr return_type;                 // pop drivers convert the result against this
            uint8_t return_conv_class = 0;             // return_conv stamped at push (0 = legacy decision)
            std::shared_ptr<function_decl> ast_pin;    // method frames: resolved overload outlives a mid-call hot reload
            bool method_result_anchor = false;         // method frames: replicate make_bound_method's keep-alive fix-up
            frame* caller = nullptr;                   // resume target: native entry frame or another record's f
            std::shared_ptr<environment> prev_env;
            bool env_lazy = false;
            // Stage 3: lazy no-closure frames never touch environment_ (nothing saved,
            // nothing restored) — the biggest per-call env cost on the fib path gone
            bool env_untouched = false;
            size_t try_base = 0;
            size_t iter_base = 0;
            size_t cfor_base = 0;
            size_t pending_base = 0;
        };
        std::vector<std::unique_ptr<call_record>> call_records_;  // grows, never shrinks mid-run
        size_t call_records_top_ = 0;                  // records [0, top) are live
        frame* switch_to_ = nullptr;                   // set by the push path, consumed by op_call's case

        // Pending call-site context armed around OPAQUE invokes (std::function values:
        // bound methods, constructors, function values) and consumed once by the next
        // call_script_function entered - ref params bind against the caller's variables
        // through it. Save/restored as a single value by nested opaque calls; shelved
        // across external (C++) invocations by the external_call_guard overrides.
        struct pending_call_site {
            const call_site* site = nullptr;
            frame* caller_frame = nullptr;
            chunk* caller_code = nullptr;
        };
        pending_call_site pending_site_ctx_{};
        std::vector<pending_call_site> external_site_stack_;

        string_symbolizer* symbolizer_;
        // Epoch sink handed to every environment this vm constructs (== symbolizer_
        // everywhere except a parallel worker; see configure_parallel_worker)
        string_symbolizer* env_symbolizer_ = nullptr;
        std::shared_ptr<environment> environment_;
        engine* engine_ = nullptr;
        // Step-debugger controller (engine-owned, shared with the interpreter backend).
        // Atomic: engine::debugger() may wire it from another thread mid-run; NOT read per
        // op — sync_debug_hook mirrors it into the plain gate below at the sync points only.
        std::atomic<debug::controller*> debug_controller_{nullptr};
        // Script-thread cached statement-hook gate: non-null only while a session is
        // enabled; run_dispatch tests this one plain pointer per dispatch iteration.
        debug::controller* debug_hook_ = nullptr;
        // The frame parked at the current stop (set around on_statement) — exact locals
        // source for get_current_frame_locals while the script thread is parked.
        frame* debug_paused_frame_ = nullptr;
        environment* cached_global_env_ = nullptr;   // env_lookup_cached gate (never dereferenced)

        vm_compiler compiler_;
        struct chunk_cache_entry {
            std::shared_ptr<chunk> compiled;
            std::shared_ptr<block_stmt> pin;
        };
        std::unordered_map<const block_stmt*, chunk_cache_entry> chunk_cache_;

        value_stack stack_;
        std::vector<frame*> frames_;

        // Pending-callee register stack (callee-first ruling, 2026-07-08): identifier
        // callees resolve at the pre-args observation point WITHOUT materializing on
        // the value stack. LIFO across nested arg calls; per-FIBER (run_fiber swaps it
        // with the other execution stacks - `f(yield x)` suspends between probe and
        // call); truncated on every unwind exactly like the value stack (try records,
        // record pops, native-entry cleanups).
        struct pending_callee {
            script_value value{std::monostate{}, nullptr};   // opaque/default-arg path
            const script_defined_function* fn = nullptr;     // direct path
            strong_ptr<script_function> pin;
        };
        std::vector<pending_callee> pending_callees_;

        std::optional<script_value> return_value_;
        bool has_return_value_ = false;
        std::optional<script_value> implicit_result_;

        std::optional<script_exception> current_exception_;
        std::optional<script_value> active_exception_value_;
        bool is_unwinding_ = false;
        bool trace_captured_ = false;
        std::vector<stack_frame> captured_trace_;
        uint64_t current_catch_var_id_ = 0;

        // One record per active try/catch region, innermost last (dual error model:
        // catches checked_result failures AND throw-flag unwinding, like the interpreter)
        struct try_record {
            frame* owner = nullptr;
            size_t handler_ip = 0;
            uint64_t catch_var = 0;
            std::optional<script_exception> saved_exception;
            std::optional<script_value> saved_exception_value;
            uint64_t saved_catch_var_id = 0;
            bool saved_unwinding = false;
            bool in_catch = false;
            size_t stack_size = 0;
            size_t iter_size = 0;
            size_t cfor_size = 0;
            size_t pending_size = 0;
            std::shared_ptr<environment> entry_env;
        };
        std::vector<try_record> try_records_;

        struct iter_state {
            std::optional<script_value> container;
            std::optional<script_value> pair_ctor;
            size_t index = 0;
            script_map::iterator map_it{};
            bool is_map = false;
            std::shared_ptr<coroutine_handle> coroutine;   // set = coroutine-driven range-for
        };
        std::vector<iter_state> iter_states_;

        // Counted-for runtime state (op_cfor_prep/back/pop). Pointers are cached into
        // environment/frame storage — stable for the loop's lifetime (deque-backed envs,
        // and fibers pin their env chains across suspension).
        struct counted_for_state {
            script_value* var = nullptr;
            script_int* end_ptr = nullptr;
            script_int end_val = 0;
            script_int* step_ptr = nullptr;
            script_int step_val = 1;
            uint8_t cmp = 0;
            bool subtract = false;
            bool incdec = false; // ++/-- update: overflow error names the real op
            bool fast = false;   // false: run the loop's generic cond/update bytecode
        };
        std::vector<counted_for_state> cfor_states_;

        // Suspended-fiber state stored opaquely on a coroutine_handle (frames+ip+stack model,
        // not the interpreter's AST continuation replay). Defined in vm_backend.cpp.
        struct vm_coroutine_state;
        coroutine_handle* active_coroutine_ = nullptr;
        bool yielding_ = false;


        bool has_custom_numeric_ops_ = false;
        bool has_custom_binary_ops_ = false;
        const detail::engine_operator_table* operator_table_ = nullptr;   // engine-owned flat dispatch
        std::function<checked_result<script_value>(const std::vector<script_value>&)> subscript_resolver_;
        std::function<std::shared_ptr<class_definition>(const std::string&)> class_lookup_;

        std::chrono::nanoseconds execution_budget_{std::chrono::seconds(1)};
        std::chrono::steady_clock::time_point execution_deadline_{};
        uint32_t budget_tick_ = 0;
        bool budget_active_ = false;
        int current_call_depth_ = 0;

        // Execution-limit state (terminal-error latch). Engine-less fallback storage;
        // prepare_for_execution repoints limits_ at the engine's per-engine instance so
        // reentrant executes share it and terminal errors cross the reentrant boundary.
        detail::execution_limits local_limits_;
        detail::execution_limits* limits_ = &local_limits_;

        uint64_t op_plus_id_ = 0, op_minus_id_ = 0, op_star_id_ = 0, op_slash_id_ = 0, op_percent_id_ = 0;
        uint64_t op_less_id_ = 0, op_less_equal_id_ = 0, op_greater_id_ = 0, op_greater_equal_id_ = 0;
        uint64_t op_equal_equal_id_ = 0, op_bang_equal_id_ = 0, op_spaceship_id_ = 0;
        uint64_t op_ampersand_id_ = 0, op_pipe_id_ = 0, op_caret_id_ = 0, op_left_shift_id_ = 0, op_right_shift_id_ = 0;
        uint64_t this_id_ = 0;
        uint64_t subscript_op_id_ = 0;
        uint64_t assign_operator_id_ = 0;
        uint64_t to_string_id_ = 0;
        uint64_t to_bool_id_ = 0;
        uint64_t eq_method_id_ = 0;
        uint64_t cpp_object_field_id_ = 0;
        uint64_t same_as_id_ = 0;
        uint64_t coroutine_handle_type_id_ = 0;
        uint64_t resume_id_ = 0;
        uint64_t done_id_ = 0;
        uint64_t class_definition_type_id_ = 0;
        uint64_t hot_reload_migrate_id_ = 0;
        uint64_t pair_id_ = 0;

        builtin_method_registries builtins_;

        struct expr_chunk_cache_entry {
            std::shared_ptr<chunk> compiled;
            expression_ptr pin;
        };
        std::unordered_map<const expression*, expr_chunk_cache_entry> expr_chunk_cache_;

        void arm_execution_deadline();
        bool execution_budget_exhausted();
        // Combined limit check at loop back-edges / call entry (budget tick + memory
        // high-water) and its cold raise twin. KEEP BYTE-PARALLEL with the interpreter.
        bool execution_limit_exhausted();
        error_propagator execution_limit_failure();
        // Debug sync point (execute entry + every 1024 budget ticks + park exit via the
        // controller): mirrors debug_controller_ into the plain per-op gate and hands a
        // session-end a fresh budget deadline. Twin of interpreter::sync_debug_hook.
        void sync_debug_hook();
        // Statement-boundary edge detection + hook fire; out of line so run_dispatch's
        // Debug frame stays flat (JAI_MAX_CALL_DEPTH path). Called only when armed.
        void debug_statement_boundary(frame& f);

        std::shared_ptr<chunk> chunk_for_body(std::string_view name,
                                              const std::vector<parameter>& params,
                                              const std::shared_ptr<block_stmt>& body,
                                              size_t local_count);

        checked_result<void> run(frame& entry);
        op_status run_dispatch(frame*& fp, const size_t records_base);
        // In-loop call machinery: push/pop of call_records_ without native recursion.
        // args may alias stack_ (zero-copy slice [args_base, args_base+argc) with the
        // callee slot at args_base-1); the pooled-vector path passes (vec, 0, vec.size()).
        op_status push_script_frame(frame& caller, script_value&& callee,
                                               const script_defined_function& function,
                                               const std::vector<script_value>& args,
                                               size_t args_base, size_t argc,
                                               const call_site* site);
        // Method-call flattening: a script-class instance method enters the dispatch loop
        // directly (no bound-method mint, no arg re-copies, no native run() recursion)
        op_status enter_script_method(frame& caller, script_value&& method_val,
                                                 const script_method_dispatch& dispatch,
                                                 const std::shared_ptr<function_decl>& ast,
                                                 script_value&& receiver,
                                                 const std::vector<script_value>& arguments,
                                                 const call_site& site);
        op_status push_method_frame(frame& caller, script_value&& method_val,
                                               const script_method_dispatch& dispatch,
                                               const std::shared_ptr<function_decl>& ast,
                                               script_value&& receiver,
                                               const std::vector<script_value>& arguments,
                                               const call_site* site);
        void anchor_method_result(script_value& result, script_value& receiver);
        void pop_script_frame_core(call_record& rec);
        op_status return_from_script_frame(frame*& fp, const vm_instruction& ins);
        // Shared return epilogue (classification switch + pop + result push); the
        // return superinstructions feed it a result they resolved without the stack
        // round trip. KEEP the exact order: convert while the callee frame is live.
        op_status return_with_result(frame*& fp, script_value result);
        // Fused `return <ident>;` / `return <a op b>;` (stage 6): record-frame and
        // native-entry variants (the entry twins set return_value_ like op_return's
        // entry branch)
        op_status exec_return_ident(frame*& fp, const vm_instruction& ins);
        op_status exec_return_ident_entry(frame& f, const vm_instruction& ins);
        op_status exec_return_binary(frame*& fp, const vm_instruction& ins);
        op_status exec_return_binary_entry(frame& f, const vm_instruction& ins);
        op_status fall_off_script_frame(frame*& fp);
        void convert_cpp_exception_at_frame(frame*& fp, const script_exception& e);
        // Interpreter parity: arg-eval throws convert at the frame MAKING the call
        bool ip_in_call_arg_zone(const frame& f) const;
        void convert_cpp_exception_in_frame(const script_exception& e);
        void pop_records_to(size_t records_base, frame*& fp);
        script_value run_program(std::shared_ptr<chunk> program);
        checked_result<script_value> call_script_function(const script_defined_function& function,
                                                          const std::vector<script_value>& args,
                                                          const call_site* site = nullptr,
                                                          frame* caller_frame = nullptr,
                                                          chunk* caller_code = nullptr);
        // Shared between call_script_function and the in-loop call path (single source of truth)
        void setup_callee_env(const script_defined_function& function, call_frame& locals,
                              const std::shared_ptr<environment>& prev_env);
        op_status bind_parameters(const std::vector<parameter>& parameters,
                                             const std::vector<script_value>& args,
                                             size_t args_base, size_t argc,
                                             frame& callee, chunk& body_chunk,
                                             const std::shared_ptr<environment>& caller_env,
                                             const call_site* site, frame* caller_frame,
                                             chunk* caller_code);
        // Ref-param bind against the caller's variable storage: shares an existing
        // reference holder (cells alias), or boxes the value on demand (cell) when the
        // variable predates its escape mark
        op_status bind_reference_to_storage(script_value& storage, frame& callee, size_t param_slot);
        // Kernel caller-view over a frame: window frames hand the resolver their raw
        // window span (lawful: resolve_ref_lvalue never runs script, so the stack
        // cannot grow mid-resolve); fiber/legacy frames keep the call_frame read.
        // Defined in vm_backend.cpp (ref_lvalue.hpp stays out of this header).
        detail::caller_frame_view caller_view(frame* f) const;
        // Box-in-place + share for env-variable storage (decl refs, [&] captures, ref
        // returns); boxing demotes active counted-for fast states
        script_value share_env_ref(script_value& storage);
        void clear_this_on_frame_exit(const std::shared_ptr<environment>& entry_env);
        script_value implicit_this_result(call_frame& locals);
        script_value implicit_result_for_record(call_record& rec);
        checked_result<script_value> convert_return_value(script_value result, const type_info_ptr& return_type);

        void capture_stack_trace();

        script_value make_null() const;
        bool is_truthy(const script_value& value);
        bool object_to_bool_via_method(const script_value& value);
        std::string value_to_string_with_method(const script_value& val);
        std::optional<bool> object_equality_via_method(const script_value& left, const script_value& right);
        std::optional<bool> object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);
        std::optional<script_value> object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id);

        struct member_target {
            std::shared_ptr<class_instance> instance;
            std::shared_ptr<class_definition> engine_def;
            class_definition* class_def = nullptr;
            explicit operator bool() const { return instance != nullptr || class_def != nullptr; }
            script_value method(uint64_t id) const;
            bool has_field(uint64_t id) const;
            const script_value& get_field(uint64_t id) const;
            const std::string& class_name() const;
        };
        member_target resolve_member_target(const script_value& objectValue) const;

        // Scope-environment free list for op_scope_push/pop (mirrors the interpreter's
        // environment pool). release only recycles unshared scopes, so anything captured
        // (closure env, coroutine snapshot, reference source) keeps its identity.
        std::vector<std::shared_ptr<environment>> scope_env_pool_;
        std::shared_ptr<environment> acquire_scope_env(std::shared_ptr<environment> parent);
        std::shared_ptr<environment> acquire_method_scope_env(std::shared_ptr<environment> parent, script_value this_obj, class_definition* access_ctx = nullptr);
        std::shared_ptr<environment> acquire_static_scope_env(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def);
        void release_scope_env(std::shared_ptr<environment> env);
        void pop_scopes_pooled(uint32_t count);

        // Argument-vector free list for script calls (mirrors the interpreter's argument_pool_)
        std::vector<std::vector<script_value>> arg_vector_pool_;
        std::vector<script_value> acquire_arg_vector(size_t reserve);
        void release_arg_vector(std::vector<script_value> vec);
        // RAII: returns the vector to the pool on every exit path
        struct arg_vector_return {
            vm_backend* vm;
            std::vector<script_value>* vec;
            ~arg_vector_return() { vm->release_arg_vector(std::move(*vec)); }
        };

        // Bodies live outside run() to keep its Debug frame flat (JAI_MAX_CALL_DEPTH path)
        void exec_array(frame& f, const vm_instruction& ins);
        void exec_map(frame& f, const vm_instruction& ins);

        script_value* resolve_local_or_env(frame& f, uint32_t slot, uint64_t symbol_id);
        script_value* resolve_local_or_env_cached(frame& f, uint32_t slot, uint64_t symbol_id);
        // Env-path lookup memoized per instruction (cache_slot = 2*ip [+1 for fused right]).
        // Returns the same pointer environment::get_ref/get_value_ptr's storage prefix
        // would; nullptr = caller must run its original full lookup (fallback tails).
        script_value* env_lookup_cached(frame& f, size_t cache_slot, uint64_t symbol_id);
        // box_cell: escape-marked decls wrap the value into a cell (see reference_holder)
        op_status define_decl_value(frame& f, uint64_t name_id, size_t slot_index, script_value value, bool box_cell = false);

        checked_result<script_value> enforce_type_compatibility(script_value value, type_info_ptr target_type);
        // Compound-assignment store-back (x op= rhs ≡ x = T(x op rhs)); interpreter twin:
        // interpreter::compound_typed_store_back
        op_status compound_typed_store_back(script_value& target, script_value promoted);
        checked_result<script_value> try_convert_for_parameter(const script_value& arg, type_info_ptr target_type);
        checked_result<script_value> evaluate_arithmetic(const script_value& left_in, token_type op, const script_value& right_in);

        checked_result<script_value> binary_general(token_type op, const script_value& left, const script_value& right);
        bool binary_fast_shape(token_type op, uint32_t shape, const script_value& left, const script_value& right,
                               std::optional<checked_result<script_value>>& out);
        checked_result<script_value> handle_binary_op(token_type op, const script_value& left, const script_value& right);
        checked_result<script_value> handle_add(const script_value& left, const script_value& right);
        checked_result<script_value> handle_subtract(const script_value& left, const script_value& right);
        checked_result<script_value> handle_multiply(const script_value& left, const script_value& right);
        checked_result<script_value> handle_divide(const script_value& left, const script_value& right);
        checked_result<script_value> handle_modulo(const script_value& left, const script_value& right);
        checked_result<script_value> handle_less(const script_value& left, const script_value& right);
        checked_result<script_value> handle_less_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_greater(const script_value& left, const script_value& right);
        checked_result<script_value> handle_greater_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_not_equal(const script_value& left, const script_value& right);
        checked_result<script_value> handle_spaceship(const script_value& left, const script_value& right);

        builtin_method_context builtin_ctx() const { return {engine_, symbolizer_}; }

        void raise_script_exception(const std::string& message, const source_location& location);

        // Runs a standalone expression chunk (node-keyed cache) in the given environment
        checked_result<script_value> eval_expression(const expression_ptr& expr, const std::shared_ptr<environment>& env);

        checked_result<script_value> execute_method_ast(const std::shared_ptr<function_decl>& ast,
                                                        std::shared_ptr<environment> method_env,
                                                        const std::vector<script_value>& args);

        checked_result<script_value> construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                        std::shared_ptr<environment> definition_env,
                                                        const std::vector<script_value>& args);
        checked_result<script_value> construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                const std::vector<script_value>& args);
        void evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                         std::shared_ptr<script_class_definition> class_def,
                                         std::shared_ptr<environment> init_env,
                                         bool skip_parent_recursion = false);

        // Mirrors visit_member_expr's non-static path; unwinding cases leave out null
        op_status member_access_value(const script_value& raw_object, member_expr* expr, script_value& out);
        op_status static_member_value(member_expr* expr, script_value& out);
        op_status assign_member(const script_value& object_value, member_expr* member, const script_value& value);
        op_status invoke_callee(frame& f, script_value&& callee, std::vector<script_value>& arguments, const call_site& site);

        op_status exec_class_decl_node(class_decl* decl);
        op_status exec_namespace_decl_node(namespace_decl* decl);

        // Op failures write details here (only via raise_); returns carry op_status.
        error_propagator pending_error_{};
#if defined(_MSC_VER)
        __declspec(noinline)
#endif
        op_status raise_(std::error_code ec, std::string_view msg = {}, uint64_t sym = 0, uint64_t sym2 = 0) {
            pending_error_ = error_propagator{ec, msg, sym, sym2};
            return op_status::failed;
        }
        op_status raise_from(const error_propagator& prop) {
            pending_error_ = prop;
            return op_status::failed;
        }
        template <typename T>
        op_status raise_from(const checked_result<T>& r) {
            pending_error_ = r.error_value();
            return op_status::failed;
        }
        op_status vm_check(op_status s) { return s; }
        op_status vm_check(const checked_result<void>& r) { return r ? op_status::ok : raise_from(r); }

        op_status exec_load(frame& f, const vm_instruction& ins);
        op_status exec_store(frame& f, const vm_instruction& ins);
        op_status exec_compound_store(frame& f, const vm_instruction& ins);
        op_status exec_incdec(frame& f, const vm_instruction& ins);
        op_status exec_decl_var(frame& f, const vm_instruction& ins);
        op_status exec_decl_ref_ident(frame& f, const vm_instruction& ins);
        op_status exec_decl_ref_value(frame& f, const vm_instruction& ins);
        op_status exec_binary(frame& f, const vm_instruction& ins);
        op_status exec_binary_fused(frame& f, const vm_instruction& ins);
        // The ONE fused-binary computation, sink-templated (flatstack stage 6 dest-
        // addressing): op_binary_fused pushes, the _decl/_store variants land the
        // result without a push or a second dispatch. Defined in vm_backend.cpp.
        template <typename Sink>
        op_status binary_fused_compute(frame& f, uint32_t proto_index, Sink&& sink);
        op_status exec_binary_fused_decl(frame& f, const vm_instruction& ins);
        op_status exec_binary_fused_store(frame& f, const vm_instruction& ins);
        // Fused subscript read/store: container+index as operands (no LOAD dispatches);
        // non-array shapes replay the verbatim unfused ops
        op_status exec_index_fused(frame& f, const vm_instruction& ins);
        op_status exec_index_store_fused(frame& f, const vm_instruction& ins);
        // op_store's exact post-pop tail, shared with op_binary_fused_store
        op_status store_popped_value(frame& f, const vm_instruction& ins, script_value value);
        // Superinstruction: fused comparison + jump_if_false (always retargets f.ip on success)
        op_status exec_fused_cmp_jump(frame& f, const vm_instruction& ins);
        const script_value* fused_cmp_operand(frame& f, const fused_operand& operand, size_t cache_slot);
        // Superinstruction: fused binary RHS + identifier compound store
        op_status exec_compound_fused(frame& f, const vm_instruction& ins);
        // Counted-for ops: both always retarget f.ip (run() must `continue` after them)
        op_status exec_cfor_prep(frame& f, const vm_instruction& ins);
        op_status exec_cfor_back(frame& f, const vm_instruction& ins);
        bool resolve_cfor_int_operand(frame& f, const fused_operand& operand, script_int*& ptr, script_int& val);
        // Resolves an identifier operand exactly like exec_load (catch var, frame slot,
        // type-ctor names, env, this-field/bound-method/static) without pushing;
        // values that must be materialized land in scratch.
        checked_result<const script_value*> fused_ident_value(frame& f, const fused_operand& operand,
                                                              std::optional<script_value>& scratch,
                                                              size_t cache_slot);
        op_status exec_index(frame& f, const vm_instruction& ins);
        op_status exec_index_assign(frame& f, const vm_instruction& ins);
        op_status exec_index_compound(frame& f, const vm_instruction& ins);
        op_status exec_index_store(frame& f, const vm_instruction& ins);
        op_status exec_index_compound_fused(frame& f, const vm_instruction& ins);
        op_status exec_math(frame& f, const vm_instruction& ins);
        op_status exec_parallel_for(frame& f, const vm_instruction& ins);
        checked_result<const script_value*> fused_subscript_value(frame& f, const fused_operand& operand,
                                                                  std::optional<script_value>& scratch, size_t cache_slot);
        op_status exec_unary(frame& f, const vm_instruction& ins);
        op_status exec_call(frame& f, const vm_instruction& ins);
        // Callee-first probe pair (op_probe_callee / op_call_from_scratch)
        op_status exec_probe_callee(frame& f, const vm_instruction& ins);
        op_status exec_call_from_scratch(frame& f, const vm_instruction& ins);
        op_status push_script_frame_pinned(frame& caller,
                                               const script_defined_function& function,
                                               strong_ptr<script_function> pin,
                                               size_t args_base, size_t argc,
                                               const call_site* site);
        op_status exec_func_decl(frame& f, const vm_instruction& ins);
        op_status exec_closure(frame& f, const vm_instruction& ins);
        op_status exec_destructure(frame& f, const vm_instruction& ins);
        op_status exec_extended(frame& f, const vm_instruction& ins);
        op_status exec_throw(frame& f, const vm_instruction& ins);
        op_status exec_try_push(frame& f, const vm_instruction& ins);
        op_status exec_try_pop(frame& f, const vm_instruction& ins);
        op_status exec_catch_end(frame& f, const vm_instruction& ins);
        op_status exec_case_eq(frame& f, const vm_instruction& ins);
        op_status exec_iter_init(frame& f, const vm_instruction& ins);
        op_status exec_iter_next(frame& f, const vm_instruction& ins);
        op_status exec_iter_pop(frame& f, const vm_instruction& ins);
        void exec_yield(frame& f, const vm_instruction& ins);
        op_status exec_include(frame& f, const vm_instruction& ins);
        op_status exec_import(frame& f, const vm_instruction& ins);
        op_status exec_ref_return_bind(frame& f, const vm_instruction& ins);
        op_status exec_ref_return_lvalue(frame& f, const vm_instruction& ins);

        vm_coroutine_state& coroutine_fiber_state(coroutine_handle& handle);
        // Runs (or continues) the fiber; returns the yield value, the final return value on the
        // completing resume, or an error (handle marked failed) — never a stale prior yield.
        checked_result<script_value> run_fiber(coroutine_handle& handle, vm_coroutine_state& state);
        // Walks try records innermost-out: false = no handler in this frame, propagate
        bool unwind_to_handler(frame& f, const error_propagator* failure);
        // Both walk outward over in-loop frames down to records_base, popping as they go
        bool handle_op_error(frame*& fp, size_t records_base);
        bool handle_throw_unwind(frame*& fp, size_t records_base);
        op_status exec_this(frame& f, const vm_instruction& ins);
        op_status exec_super(frame& f, const vm_instruction& ins);
        op_status exec_from_this(frame& f, const vm_instruction& ins);
        op_status exec_get_member(frame& f, const vm_instruction& ins);
        op_status exec_get_static(frame& f, const vm_instruction& ins);
        op_status exec_set_member(frame& f, const vm_instruction& ins);
        op_status exec_set_static(frame& f, const vm_instruction& ins);
        op_status exec_member_compound(frame& f, const vm_instruction& ins);
        op_status exec_call_method(frame& f, const vm_instruction& ins);
        op_status exec_new(frame& f, const vm_instruction& ins);
        op_status exec_class_decl(frame& f, const vm_instruction& ins);
        op_status exec_namespace_decl(frame& f, const vm_instruction& ins);
        op_status exec_enum_decl(frame& f, const vm_instruction& ins);
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_VM_BACKEND_HPP__
