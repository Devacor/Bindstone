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
#include <thread>
#include <unordered_map>
#include <vector>

namespace jai {
    class engine;
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
        size_t thread_count_override = 0;                  // engine::parallel_thread_count(n)
        std::unordered_map<const void*, parallel_admission> admission_cache;
        std::string error_text;                            // owns the message a re-raise views
        std::vector<size_t> last_chunk_bounds;             // instrumentation (tests/bench)

        // Worker-context reuse (Dev-greenlit): slots built inside the first region
        // persist and are RESET per call (limits re-slice, budget re-arm, residual-state
        // clear, host-copy refresh) instead of rebuilt; a slot rebuilds only when its
        // provisioning fingerprint (backend type + admission-graph bodies) changes.
        // Sequential paths never see any of this - the state itself is created lazily
        // inside the first region.
        std::vector<std::unique_ptr<parallel_worker_slot>> worker_slots;

        parallel_engine_state();
        ~parallel_engine_state();   // out of line: worker_slot is incomplete here
    };

    // The builtin bodies (registered by the engine constructor).
    checked_result<script_value> run_parallel_transform(engine& eng, const std::vector<script_value>& args);

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_PARALLEL_TRANSFORM_HPP__
