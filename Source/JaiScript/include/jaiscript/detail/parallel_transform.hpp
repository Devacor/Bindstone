#pragma once

#ifndef __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__
#define __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__

// parallel_transform v0 (internal/parallel_design.md, internal/parallel_prove_or_serial.md §5).
// Contract A: a body either satisfies the parallel contract or ERRORS — never silent-serial.
// Everything here constructs lazily inside the first parallel_transform call; worker
// contexts persist in an engine-owned slot pool and are RESET per call (reuse
// — rebuild only on a provisioning-fingerprint change). Outside an active region
// the engine carries exactly one extra null-pointer test on the (cold, allocation-path)
// engine::execution_limits() accessor — the zero-concurrency-cost-outside-parallel
// invariant; sequential paths never touch any of this.

#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <jaiscript/detail/fault_injection.hpp>
#include <jaiscript/detail/thread_pool.hpp>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace jai {
    class class_instance;
    class ast_node;
    class engine;
    class environment;
    class function_decl;
    class identifier_expr;
    class statement;
    struct parameter;
    class parallel_for_stmt;
    class type_info;
    struct script_defined_function;
    class variable_decl;
    struct local_alias_home;
}

namespace jai::detail {

    // Thread -> per-worker limits table, installed on the engine only while a region's
    // workers are running (read-only during the region; built/torn down single-threaded).
    // engine::execution_limits() consults it so EVERY charge site — backends, shared
    // builtin methods, value clone/cell factories, environment growth — redirects to the
    // calling worker's own accounting without any per-site changes.
    struct parallel_region_table {
        struct entry {
            std::thread::id id;
            execution_limits* limits = nullptr;
            // Pool workers only (null for main's own chunk): instances this worker constructs
            // or copies register HERE instead of in the class registry - no lock on the
            // worker; the join registers the survivors, a copy that died meanwhile just expires
            std::vector<std::weak_ptr<class_instance>>* pending_registrations = nullptr;
        };
        std::vector<entry> entries;

        execution_limits* find(std::thread::id tid) const noexcept {
            for (const auto& e : entries) {
                if (e.id == tid) { return e.limits; }
            }
            return nullptr;
        }
        std::vector<std::weak_ptr<class_instance>>* find_pending(std::thread::id tid) const noexcept {
            for (const auto& e : entries) {
                if (e.id == tid) { return e.pending_registrations; }
            }
            return nullptr;
        }
    };

    // How a captured enclosing name is provisioned into the workers, decided at the
    // barrier from the LIVE value (admission only collects the touches):
    //   scalar   - plain per-worker copy (null/int/float/char/bool; bound primitives decode)
    //   string   - per-worker detached copy (string storage is shared under plain copy)
    //   borrow   - zero-copy view of the shared container (all-primitive content AND
    //              every body touch is a subscript read - the raw-read fast tier)
    //   snapshot - one per-worker detached deep copy at the barrier (everything else:
    //              non-primitive content or non-subscript uses such as iteration,
    //              read-only methods, whole-value reads)
    enum class parallel_capture_kind : int { scalar = 0, string = 1, borrow = 2, snapshot = 3 };

    // Amortized admission verdict for one function body (keyed by body block pointer).
    // The graph snapshot records what the body needs provisioned per worker; entries are
    // re-verified against the live global environment at every region entry (hot reload
    // swaps function values; body-pointer equality is the staleness check).
    struct parallel_admission {
        bool admitted = false;
        bool inplace_root = false;   // parallel_for body (element bound in place) vs a transform fn
        std::error_code code{};
        std::string message;   // full "parallel_transform: ..." text (owned)

        // Transitive script call targets (global name id -> admission-time payload),
        // fn itself included when named.
        std::vector<std::pair<uint64_t, std::shared_ptr<script_defined_function>>> script_functions;
        // Whitelisted host functions the bodies reference (name id + registered name).
        std::vector<std::pair<uint64_t, std::string>> host_functions;
        // Parse-time type_infos referenced by the bodies (pre-warm set: reference-of and
        // component shapes are interned at the barrier so workers never intern).
        std::vector<type_info*> referenced_types;
        // Enclosing names the bodies TOUCH in read positions (captured reads). Admission
        // collects the touches and rejects the statically-visible WRITE shapes; value
        // classification happens per region entry at the barrier (parallel_capture_kind).
        // Keyed by the interned symbol id; `name` is a zero-alloc view into the
        // symbolizer's permanent storage, used only when composing an error message.
        struct capture_entry {
            uint64_t name_id = 0;
            std::string_view name;
            bool borrow_eligible = true;   // every touch was a subscript read root
        };
        std::vector<capture_entry> captures;

        // Method names called on element/worker-local receivers. Admission only collects
        // (the receiver's class is a runtime fact); the barrier resolves each name against
        // the live element classes and admits or rejects the (class, method) pair — see
        // admit_element_methods. Builtin names that no element class defines are ignored.
        struct element_method_call {
            uint64_t name_id = 0;
            std::string_view name;
            const ast_node* site = nullptr;   // pinned transitively via script_functions
        };
        std::vector<element_method_call> element_method_calls;
        // Member ids the body names on chains rooted at the element (its own fields and their
        // members, any depth), recorded for both forms. parallel_for's barrier normalization
        // descends an object only through fields the body can reach: a field no worker can
        // name is never copied by one, so its shared strings may stay shared (the transform
        // reaches every element whole - its by-value binding clones the entire element).
        // element_reach_all = the element is used whole somewhere (bare read, argument,
        // method receiver, store), so every field counts.
        // element_reached_whole = chain terminals used as a VALUE or REPLACED (a copy or a
        // release touches every handle below them), so their whole subtree counts.
        std::vector<uint64_t> element_reached_members;
        std::vector<uint64_t> element_reached_whole;
        bool element_reach_all = false;
        // A store or a mutating method call through the element (member store, element
        // replacement, a container/string mutator on an element-rooted chain, an alias of it).
        // A by-value element parameter whose body neither writes through it nor uses it whole
        // (element_reach_all) binds the element IN PLACE on the worker: every remaining use is
        // a member read, so the copy is unobservable (a copy we can prove away goes).
        bool element_mutated = false;
        bool transform_cursor = false;   // the transform delivers elements through a chunk cursor (set once its chunk loop lands)
    };

    // One barrier-admitted script-class method, provisioned per worker. The method value
    // is a worker-private copy (its strong_ptr counts never cross threads), the overload
    // is pre-resolved (workers never touch shared resolution caches), and the body chunk
    // is precompiled on the worker's own backend at the barrier (no in-region compiles).
    struct parallel_method_pin {
        script_value method_value{std::monostate{}, nullptr};
        std::shared_ptr<function_decl> resolved;
    };

    // (class_definition*, method name id) -> pin. Consulted by both backends' worker
    // method walls: a hit dispatches on the pinned worker-private state; a miss keeps
    // the wall error (now an admission verdict). Exact linear table: a region admits a
    // handful of methods, and the key must never collide (wrong-method dispatch).
    struct parallel_method_pin_table {
        struct entry {
            const void* cls = nullptr;
            uint64_t name_id = 0;
            parallel_method_pin pin;
        };
        std::vector<entry> entries;
        const parallel_method_pin* find(const void* cls, uint64_t name_id) const noexcept {
            for (const auto& e : entries) {
                if (e.cls == cls && e.name_id == name_id) { return &e.pin; }
            }
            return nullptr;
        }
    };

    // A reusable worker execution context (definition private to parallel_transform.cpp:
    // backend instance, root env, epoch sink, limits, provisioned callables).
    struct parallel_worker_slot;

    // Engine-owned parallel machinery. Lazily created at the first parallel_transform /
    // thread_count touch; the pool's parked workers impose no cost on any sequential
    // script path (no engine lock exists outside an active region).
    struct parallel_engine_state {
        std::unique_ptr<jai::thread_pool> pool;
        parallel_region_table* active_region = nullptr;   // non-null only while workers run
        std::mutex instance_registry_mutex;                // class_definition instance registry, region-time only
        bool region_running = false;                       // reentrancy guard (v0: no nesting)
        bool pending_registration_retry = false;           // exceptional join: drain before cache mutation or class reload
        fault_injection faults;                            // host/test checkpoints; disabled by default
        bool allow_unsafe = false;                         // engine::allow_unsafe_parallel(bool)
        size_t thread_count_override = 0;                  // engine::parallel_thread_count(n)
        size_t spin_wait_us = 1000;                        // engine::parallel_spin_wait_us(us): the pool's spin-before-park budget
        std::unordered_map<const void*, parallel_admission> admission_cache;
        // parallel_transform's chunk loop (k_parallel_chunk_transform), parsed once per engine
        // and provisioned per worker: one script entry per chunk, fn called in-loop per element,
        // results emitted into the worker's output slice
        std::shared_ptr<function_decl> transform_chunk_wrapper;
        // Per fn (keyed by its body): fn's body grafted into the chunk loop - `out = fn(in)` with
        // zero calls per element - when fn only reads its parameter and returns only from its
        // top level; otherwise the call-form skeleton above. Hot reload mints a fresh body.
        std::unordered_map<const void*, std::shared_ptr<function_decl>> transform_inline_wrappers;
        std::string error_text;                            // owns the message a re-raise views
        std::vector<size_t> last_chunk_bounds;             // instrumentation (tests/bench)
        // Instrumentation: how the most recent region provisioned each captured name
        // (engine::last_parallel_captures) - pins borrow-vs-snapshot classification.
        // INTERNED symbol ids, never std::string (no per-region allocation).
        std::vector<std::pair<uint64_t, parallel_capture_kind>> last_captures;

        // Worker-context reuse: slots built inside a region persist and
        // are RESET per call (limits re-slice, budget re-arm, residual-state clear,
        // host-copy refresh) instead of rebuilt; a slot rebuilds only when its
        // provisioning fingerprint (backend type + admission-graph bodies) changes.
        // Slot-sets are POOLED PER REGION FINGERPRINT (fn body + backend): a frame that
        // cycles several distinct regions (GLOOM: rays, transform, glyph rows) keeps a
        // persistent context set for each instead of rebuilding every slot every call
        // (measured: 10k backend constructions per 600 ticks with the single-set pool).
        // Small LRU cap; sequential paths never see any of this - the state itself is
        // created lazily inside the first region.
        struct parallel_region_pool {
            const void* fn_body = nullptr;
            bool use_vm = false;
            uint64_t last_use = 0;
            // Last region of this statement: ns per element as the WORKER path runs it (main's
            // own chunk, timed). 0 = no region yet: the first region fans out to every thread
            // with an even split (the baseline); later regions wake only the pool workers whose
            // chunk is worth ~3x the pool's measured wake + join cost (worker_overhead_ns).
            // Chunking never changes results, so this is a pure cost knob.
            double ns_per_element = 0.0;
            std::vector<std::unique_ptr<parallel_worker_slot>> worker_slots;
        };
        static constexpr size_t max_region_pools = 8;
        std::vector<parallel_region_pool> region_pools;
        // Measured once when the pool is built (three no-op round trips, the minimum): the
        // fixed ns a pool worker costs to submit, wake and join on THIS machine. 0 = not yet.
        double worker_overhead_ns = 0.0;
        uint64_t region_use_counter = 0;

#ifdef JAISCRIPT_VM_PROFILE
        // Fork-join phase decomposition (profiling builds; dumped by the dtor): where
        // main's blocked cycles go per region. wait = the straggler window after main's
        // own chunk; wake = submit-end -> worker-chunk-start per pool worker (kernel
        // wake latency), clamped at 0 for workers that started mid-submit-loop.
        struct region_phase_prof {
            uint64_t regions = 0;
            uint64_t setup_cyc = 0;        // entry -> submit loop (admission/slots/captures/prewarm)
            uint64_t submit_cyc = 0;       // the submit_to loop
            uint64_t main_chunk_cyc = 0;   // main's own chunk 0
            uint64_t wait_cyc = 0;         // wait_idle tail
            uint64_t join_cyc = 0;         // accounting + error scan + assembly (success path)
            uint64_t wake_lat_sum = 0;
            uint64_t wake_lat_max = 0;
            uint64_t wake_samples = 0;
            uint64_t worker_span_sum = 0;  // pool-worker compute spans
            uint64_t region_span_max_sum = 0;   // per region: the slowest pool worker's span (the critical path), summed
            uint64_t region_span_min_sum = 0;   // per region: the fastest pool worker's span, summed
            uint64_t capture_cyc = 0;      // slice of setup: capture provisioning (all slots)
            uint64_t capture_count = 0;    // captures provisioned
            uint64_t setup_admit_cyc = 0;    // setup slice: method admission (after normalize)
            uint64_t setup_slots_cyc = 0;    // setup slice: slot reset/provision, alias restamp, cursors
            uint64_t setup_caps_cyc = 0;     // setup slice: capture provisioning + node rebuild
            uint64_t setup_pins_cyc = 0;     // setup slice: method pins
            uint64_t setup_prewarm_cyc = 0;  // setup slice: type prewarm
            uint64_t setup_rest_cyc = 0;     // setup slice: table, pool, activation
            uint64_t slot_reset_cyc = 0;     // slots slice: slot_matches + reset_worker_slot / provision_worker
            uint64_t slot_restamp_cyc = 0;   // slots slice: restamp_alias_proofs
            uint64_t slot_cursor_cyc = 0;    // slots slice: cursor mint + call_args
            uint64_t admit_pool_cyc = 0;     // admit slice: thread count, pool acquire, cost cap, partition
            uint64_t admit_guard_cyc = 0;    // admit slice: region guard, type metadata
            uint64_t cache_hits = 0;       // snapshot cache: content-matched reuses
            uint64_t cache_stores = 0;     // snapshot cache: records (re)stored
            uint64_t cache_misses = 0;     // snapshot cache: validate failed or no record
            uint64_t cache_unprovable = 0; // snapshot cache: shape not flat -> never cached
            std::map<std::string, uint64_t> cache_miss_names;   // miss counts by capture name
            std::map<std::string, uint64_t> cache_miss_cyc;     // detach cycles by capture name
        };
        region_phase_prof prof_transform;
        region_phase_prof prof_for;
#endif

        parallel_engine_state();
        ~parallel_engine_state();   // out of line: worker_slot is incomplete here
    };

    // Main-thread exceptional-join recovery, shared by region entry and class reload.
    void recover_pending_registrations(parallel_engine_state& state);

    // The builtin bodies (registered by the engine constructor).
    checked_result<script_value> run_parallel_transform(engine& eng, const std::vector<script_value>& args);

    // parallel_for (auto& x : arr) { body } — the in-place fork-join statement
    // (parallel_for v1). Both backends call this after evaluating the container in the
    // enclosing scope: `container` must hold (or reference) the LIVE array handle so
    // element writes land in the script-visible storage. Admission runs the same
    // fail-closed walk in "in-place root" mode (the element may bind by reference; member
    // access/stores rooted at locals are admitted in both forms); the barrier proves every
    // element safe for exclusive in-place mutation (primitives, typed primitive
    // containers, flat-value-class instances — engine::allow_unsafe_parallel skips the
    // proof) and pre-mints the element references single-threaded. capture_env is the
    // statement's enclosing environment: captured reads resolve through its chain, except
    // the enclosing frame's own locals (parallel_for_stmt::frame_captures), which frame_slot
    // reads from the executing frame (null when the slot is not live).
    checked_result<void> run_parallel_for(engine& eng, parallel_for_stmt* stmt,
                                          std::vector<script_value>& containers,   // [0] = the range, then the zip's arrays
                                          const std::shared_ptr<environment>& capture_env,
                                          const std::function<const script_value*(size_t)>& frame_slot);

    // Subscript READ through a parallel borrow - the tier-1 raw-read path. Called by
    // BOTH backends' subscript twins when the (deref'd) receiver is a parallel borrow
    // (parity by construction; error text matches the twins' byte-for-byte, pre-formatted
    // so no worker-side intern happens against the frozen symbolizer). Primitive elements
    // mint plain worker-local values (zero refcount traffic); non-primitive elements
    // materialize through the refcount-silent deep clone (tier 3 - defensive: the barrier
    // only borrows all-primitive content). lvalue_write is the runtime write wall.
    checked_result<script_value> parallel_borrow_subscript_read(const script_value& borrow,
                                                                const script_value& index,
                                                                engine* eng,
                                                                bool lvalue_write);
    // The string a borrowed-array read would copy: the element's live bytes (a hetero string
    // element or a string-buffer slot) when the index is an int in range, else null and the
    // read runs the kernel above. Lets a worker serve repeated reads of the same table cell
    // from its own copy (validated against these bytes) instead of allocating per read.
    const script_string* parallel_borrow_string_source(const script_value& borrow, const script_value& index) noexcept;

    // Read-only by-value parameter elision. Bit i of the result: the body of `fn` (and every
    // script function it calls) would be admitted in a parallel body - so it writes nothing
    // but its own locals - AND parameter i is only ever read through member chains and
    // subscripts whose static terminal is a primitive or a string, or as the receiver of a
    // read-only builtin method. Such a parameter can bind the argument by handle: no store
    // can reach the caller's value through it and no identity-observing use (comparison,
    // whole-value pass, alias, return) exists. Main thread only (interns, resolves globals).
    uint32_t prove_readonly_parameters(engine& eng, const script_defined_function& fn);
    // The argument side of the elision: a script class instance that is not nocopy (a nocopy
    // by-value pass must keep raising whatever the callee does), an array or a map. Host
    // objects keep the clone.
    bool parameter_alias_allows(const script_value& derefed);

    // Read-only by-value local elision: a function's `T x = lvalue;` declarations whose proof (the parameter proof's walk
    // over the statements after the declaration, plus: no write in that range may reach the source's object) holds bind
    // the source's object instead of a clone. stamp_local_alias_homes marks a plain function's candidate declarations
    // when it is minted; prove_local_aliases stamps every verdict of one home (main thread, no parallel region).
    void stamp_local_alias_homes(const script_defined_function& fn);
    void prove_local_aliases(engine& eng, const local_alias_home& home);
    // The declaration may bind `value` (its initializer's derefed value) without the clone: proven for the current
    // definition epoch, the value's type is exactly the declared one, and the argument-side gate admits it
    inline bool local_alias_binds(engine& eng, const variable_decl& d, const script_value& value, bool may_prove) {
        const local_alias_home* home = d.alias_home.get();
        if (!home || !eng.local_alias()) { return false; }
        const uint64_t epoch = eng.definition_epoch();
        if (home->epoch != epoch) {
            if (!may_prove) { return false; }
            prove_local_aliases(eng, *home);
            home->epoch = epoch;
        }
        return d.alias_index < home->verdicts.size() && home->verdicts[d.alias_index] && value.get_type_info().get() == d.type.get() &&
               parameter_alias_allows(value);
    }

    // Inferred-const reference parameters (detail/ref_param_writes.hpp): the body never
    // writes through reference parameter i, callees resolved through the global
    // environment. Main thread only (resolves globals).
    // A temporary argument may bind reference parameter i (the verdict cached on the
    // parameter; a parallel worker consumes only a verdict the main thread already derived)
    bool ref_parameter_takes_temporary(engine& eng, const std::vector<parameter>& params, const statement* body, size_t i, bool may_prove);
    // A callee name that is a declared script function in the global environment (the
    // backends' resolution for the kernel; the checker falls back to it)
    struct ref_write_callee;
    bool resolve_global_ref_write_callee(engine& eng, const identifier_expr& id, std::vector<ref_write_callee>& out);

    // The cached verdict for parameter i (recomputed when the definition epoch moved; a
    // parallel worker may only consume a verdict the main thread already stamped).
    inline bool readonly_parameter_alias(engine& eng, const script_defined_function& fn, size_t i, bool may_prove) {
        if (!fn.alias_candidate || i >= 32 || !eng.parameter_alias()) { return false; }
        const uint64_t epoch = eng.definition_epoch();
        if (fn.alias_proof_epoch != epoch) {
            if (!may_prove) { return false; }
            fn.alias_proof_mask = prove_readonly_parameters(eng, fn);
            fn.alias_proof_epoch = epoch;
        }
        return ((fn.alias_proof_mask >> i) & 1u) != 0;
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__
