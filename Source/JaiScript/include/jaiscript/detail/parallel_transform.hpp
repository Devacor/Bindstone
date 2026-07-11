#pragma once

#ifndef __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__
#define __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__

// parallel_transform v0 (docs/parallel_design.md, docs/parallel_prove_or_serial.md §5).
// Contract A: a body either satisfies the parallel contract or ERRORS — never silent-serial.
// Everything here constructs lazily inside the first parallel_transform call; worker
// contexts persist in an engine-owned slot pool and are RESET per call (Dev-greenlit
// reuse — rebuild only on a provisioning-fingerprint change). Outside an active region
// the engine carries exactly one extra null-pointer test on the (cold, allocation-path)
// engine::execution_limits() accessor — the ruled zero-concurrency-cost-outside-parallel
// invariant; sequential paths never touch any of this.

#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <jaiscript/detail/thread_pool.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace jai {
    class ast_node;
    class engine;
    class environment;
    class function_decl;
    class parallel_for_stmt;
    class type_info;
    struct script_defined_function;
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
        };
        std::vector<entry> entries;

        execution_limits* find(std::thread::id tid) const noexcept {
            for (const auto& e : entries) {
                if (e.id == tid) { return e.limits; }
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
        bool region_running = false;                       // reentrancy guard (v0: no nesting)
        bool allow_unsafe = false;                         // engine::allow_unsafe_parallel(bool)
        size_t thread_count_override = 0;                  // engine::parallel_thread_count(n)
        std::unordered_map<const void*, parallel_admission> admission_cache;
        std::string error_text;                            // owns the message a re-raise views
        std::vector<size_t> last_chunk_bounds;             // instrumentation (tests/bench)
        // Instrumentation: how the most recent region provisioned each captured name
        // (engine::last_parallel_captures) - pins borrow-vs-snapshot classification.
        // INTERNED symbol ids, never std::string (no per-region allocation).
        std::vector<std::pair<uint64_t, parallel_capture_kind>> last_captures;

        // Worker-context reuse (Dev-greenlit): slots built inside a region persist and
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
            std::vector<std::unique_ptr<parallel_worker_slot>> worker_slots;
        };
        static constexpr size_t max_region_pools = 8;
        std::vector<parallel_region_pool> region_pools;
        uint64_t region_use_counter = 0;

        parallel_engine_state();
        ~parallel_engine_state();   // out of line: worker_slot is incomplete here
    };

    // The builtin bodies (registered by the engine constructor).
    checked_result<script_value> run_parallel_transform(engine& eng, const std::vector<script_value>& args);

    // parallel_for (auto& x : arr) { body } — the in-place fork-join statement
    // (parallel_for v1). Both backends call this after evaluating the container in the
    // enclosing scope: `container` must hold (or reference) the LIVE array handle so
    // element writes land in the script-visible storage. Admission runs the same
    // fail-closed walk in "in-place root" mode (member access/stores rooted at locals
    // admitted, by-ref element required, no valued returns); the barrier proves every
    // element safe for exclusive in-place mutation (primitives, typed primitive
    // containers, flat-value-class instances — engine::allow_unsafe_parallel skips the
    // proof) and pre-mints the element references single-threaded. capture_env is the
    // statement's enclosing environment: captured reads resolve through its chain.
    checked_result<void> run_parallel_for(engine& eng, parallel_for_stmt* stmt,
                                          const script_value& container,
                                          const std::shared_ptr<environment>& capture_env);

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

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__
