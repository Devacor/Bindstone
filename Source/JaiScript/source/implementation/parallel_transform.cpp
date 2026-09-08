// parallel_transform v0 (docs/parallel_design.md; docs/parallel_prove_or_serial.md §5,
// RULED contract A). One builtin, three phases, all charged to the call itself:
//
//   ADMISSION (amortized per body): a fail-closed static walk over fn's AST — every node
//   kind is whitelisted or the call errors ("parallel_transform: <reason> at line:col").
//   The walk also snapshots the transitive call graph (script functions + whitelisted
//   host functions) and every parse-time type the bodies reference.
//
//   BARRIER (single-threaded): per-worker execution contexts are acquired — REUSED from
//   the engine's slot pool when the provisioning fingerprint still matches (reset:
//   limits re-slice, budget re-arm, residual-state clear, host-copy refresh), rebuilt
//   otherwise. A fresh build gives each worker its own backend
//   instance (engine's configured type), root environment with parent = nullptr (the
//   hard partition: a lookup that would escape fails as undefined, it cannot race),
//   private env-epoch sink, own execution_limits, per-worker copies of every callable
//   the body needs (fresh strong_ptrs — invoking them bumps no shared refcount). The
//   source elements pass the fork's element proof (prove_elements: all-primitive by
//   construction, object-kind by declared type, everything else by one normalizing walk)
//   and workers then read the source LIVE — the by-value binding makes each private
//   copy. Type shapes are pre-warmed and the symbolizer/type tables freeze.
//
//   REGION: static contiguous chunks (n/thread_count(), boundary-shifted by the optional
//   weight hint evaluated at the barrier), pinned pool workers + the calling thread,
//   disjoint writes into the pre-sized result. engine::execution_limits() resolves to
//   the calling worker's accounting through the installed region table. First error in
//   ITERATION order wins at the join; a terminal worker failure (budget) latches the
//   engine's terminal rail.

#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/parallel_transform.hpp>
#include <jaiscript/detail/interpreter_backend.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/ast_serializer.hpp>
#include <jaiscript/detail/return_conv.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/vm/vm_backend.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_set>

#ifdef JAISCRIPT_VM_PROFILE
#include <intrin.h>   // __rdtsc: region phase probes (diagnostic builds only)
#include <cstdio>
#endif

namespace jai::detail {

	// Per-call worker failure record (first error in the slot's chunk)
	struct parallel_worker_error {
		size_t iteration = 0;
		std::error_code code{};
		std::string message;
		bool terminal = false;
	};

	// One reusable worker execution context. Built inside a region, RESET per call
	// afterwards; rebuilt only when the fingerprint stops matching. Owned by
	// parallel_engine_state::worker_slots (engine lifetime).
	struct parallel_worker_slot {
		~parallel_worker_slot() {
			// Private callables close over this environment. Break that cycle before
			// the backend and its epoch sink die, including partially built slots.
			if (root_env) { root_env->clear_values(); }
		}

		std::unique_ptr<string_symbolizer> env_epoch_sink;
		std::shared_ptr<environment> root_env;
		execution_limits limits;
		std::unique_ptr<execution_backend> backend;
		vm::vm_backend* vm = nullptr;                     // set when the engine backend is the vm
		interpreter_backend* interp = nullptr;            // set otherwise
		script_callable fn_payload;
		// parallel_for: the per-worker chunk-loop wrapper (k_parallel_chunk_source bound to this
		// worker's root_env) and the cursor holder its advance intrinsic re-points
		script_callable chunk_payload;
		// Instances this worker constructed or copied during the region (pool workers only;
		// registered by the join, expired entries skipped)
		std::vector<std::weak_ptr<class_instance>> pending_instances;
		std::vector<script_value> cursors;   // [0] = the range's chunk cursor, then the zip's arrays
		std::vector<script_value> call_args; // prepared at the barrier; no worker-entry allocation
		// Live input vector (Dev ruling 2026-07-09 "safe AND free"): workers read the
		// caller's vector directly - all-primitive elements (typed proof or one dynamic
		// scan) mint worker-local copies with zero refcount traffic, and elements the
		// fork's element proof made exclusive lend a handle only their chunk's worker
		// touches. Non-null only DURING a region (points into the run call's locals; the
		// join guard nulls it).
		const std::vector<script_value>* raw_input = nullptr;
		// By-value input on an object-kind typed node (both forms): each iteration boxes
		// its slot (a handle copy of the chunk-exclusive instance) and the by-value binding
		// clones it. Non-null only DURING a region, like raw_input.
		const script_array* typed_input = nullptr;
		// In-place region (parallel_for): pre-minted element references for this slot's
		// chunk (index i - begin), bound to the body's by-ref parameter per iteration.
		// Minted single-threaded at the barrier (each holder pins the container handle);
		// cleared at the join - a dormant slot must never pin the caller's array.
		std::vector<script_value> element_refs;
		size_t begin = 0;
		size_t end = 0;
		std::optional<parallel_worker_error> error;

		// Captured-read provisioning for THIS call: name ids defined into root_env
		// (nulled at the join - a dormant slot must hold no borrow raw pointers), and
		// the heavy nodes of every snapshot/string capture (a result that reaches into
		// one of these must detach, or two iterations on one worker could alias while
		// two on different workers don't - a worker-count-visible difference)
		std::vector<uint64_t> capture_name_ids;
		std::unordered_set<const void*> provisioned_nodes;

		// Snapshot-capture reuse across regions (the transform-barrier heat: GLOOM's
		// string tables were re-detached per worker per region, 3.28M of 3.57M setup
		// cyc). The cached DETACHED COPY is itself the validity reference: a hit
		// requires the live source to content-match it under flat_snapshot_equal, and
		// a record is only stored when that predicate can prove the shape (flat
		// string/scalar tables) - store-condition == hit-condition. Cache values pin
		// only worker-owned detached nodes, never caller state (dormant-slot safe);
		// the join's define-nulling is unchanged.
		struct snapshot_capture_cache_entry {
			uint64_t name_id = 0;
			uint64_t built_serial = 0;           // region_use_counter at store (validation memo key)
			script_value value{ std::monostate{}, static_cast<engine*>(nullptr) };
			std::vector<const void*> nodes;      // provisioned_nodes contribution
			std::vector<type_info*> types;       // prewarm (value_types) contribution
		};
		std::vector<snapshot_capture_cache_entry> snapshot_cache;

		// Barrier-admitted class methods for THIS call: worker-private minted dispatcher
		// values + pre-resolved overloads (parallel-method-admission ruling). Rebuilt per
		// call (element classes are a runtime fact); cleared at the join.
		detail::parallel_method_pin_table method_pins;

		// Provisioning fingerprint: reuse is valid only while these match the current
		// call. Script bodies are identity-compared (hot reload swaps bodies -> rebuild);
		// host copies are refreshed on every reuse instead of fingerprinted.
		bool fingerprint_use_vm = false;
		const void* fingerprint_fn_body = nullptr;
		std::vector<std::pair<uint64_t, const void*>> fingerprint_script_bodies;
		// Read-only parameter proofs travel from the live function to its worker copy: the
		// barrier (single-threaded) proves the source and restamps every copy per region entry,
		// so a worker never proves (interning, global lookups) and never runs stale
		std::vector<std::pair<std::shared_ptr<script_defined_function>, std::shared_ptr<script_defined_function>>> alias_stamps;
		std::vector<std::pair<uint64_t, std::string>> host_functions;   // refreshed each reuse

#ifdef JAISCRIPT_VM_PROFILE
		// Worker thread stamps chunk entry/exit; main reads after wait_idle (the pool
		// mutex/CV sequence orders the accesses; invariant TSC compares across cores)
		uint64_t prof_start_tsc = 0;
		uint64_t prof_end_tsc = 0;
#endif
	};

	parallel_engine_state::parallel_engine_state() = default;
#ifdef JAISCRIPT_VM_PROFILE
	parallel_engine_state::~parallel_engine_state() {
		auto dump = [](const char* name, const region_phase_prof& p) {
			if (!p.regions) return;
			fprintf(stderr,
				"[region-profile] %s: %llu regions | per region: setup %.0f (captures %.0f, %llu/region) | submit %.0f | main-chunk %.0f | wait %.0f | join %.0f cyc\n"
				"  wake latency avg %.0f max %llu cyc (%llu samples) | pool-worker span avg %.0f cyc\n",
				name, (unsigned long long)p.regions,
				(double)p.setup_cyc / p.regions, (double)p.capture_cyc / p.regions,
				(unsigned long long)(p.capture_count / p.regions),
				(double)p.submit_cyc / p.regions,
				(double)p.main_chunk_cyc / p.regions, (double)p.wait_cyc / p.regions,
				(double)p.join_cyc / p.regions,
				p.wake_samples ? (double)p.wake_lat_sum / p.wake_samples : 0.0,
				(unsigned long long)p.wake_lat_max, (unsigned long long)p.wake_samples,
				p.wake_samples ? (double)p.worker_span_sum / p.wake_samples : 0.0);
			if (p.cache_hits + p.cache_misses + p.cache_unprovable) {
				fprintf(stderr, "  snapshot cache: hits %llu | misses %llu | stores %llu | unprovable %llu\n",
					(unsigned long long)p.cache_hits, (unsigned long long)p.cache_misses,
					(unsigned long long)p.cache_stores, (unsigned long long)p.cache_unprovable);
				for (const auto& [nm, cnt] : p.cache_miss_names) {
					const auto cyc_it = p.cache_miss_cyc.find(nm);
					fprintf(stderr, "    miss %-24s %8llu x %10.2f Mcyc detach\n", nm.c_str(),
						(unsigned long long)cnt,
						cyc_it != p.cache_miss_cyc.end() ? cyc_it->second / 1e6 : 0.0);
				}
			}
		};
		dump("parallel_transform", prof_transform);
		dump("parallel_for", prof_for);
	}
#else
	parallel_engine_state::~parallel_engine_state() = default;
#endif

namespace {

	// One drain for the normal join and exceptional-exit retry. Erasing only the
	// consumed prefix prevents duplicate registration while preserving live objects
	// that escaped through in-place stores. weak_ptr movement/erase does not allocate.
	void drain_pending_instances(parallel_engine_state& state, parallel_worker_slot& slot) {
		auto& pending = slot.pending_instances;
		size_t consumed = 0;
		try {
			for (; consumed < pending.size(); ++consumed) {
				auto& weak = pending[consumed];
				if (auto live = weak.lock()) {
					state.faults.checkpoint("region.instance_register");
					live->get_class_definition()->register_instance_after_region(weak, &state.faults);
				}
			}
		} catch (...) {
			if (consumed) { pending.erase(pending.begin(), pending.begin() + consumed); }
			throw;
		}
		pending.clear();
	}

	constexpr size_t k_parallel_small_n = 2;   // one element has nothing to split; from two on, the measured-cost cap below decides
	                                           // (a fixed 16 kept GLOOM's 15 wall chunks, 27% of its frame, on the calling thread)
	// A pool worker earns its keep when its chunk is worth k_parallel_chunk_overhead_ratio times
	// the fixed cost of waking and joining it - both sides measured per statement on the machine
	// at hand (no constants from one box). Chunking never changes results (iteration order wins
	// at the join), so the count is a pure cost knob. The first region of a statement has no
	// estimate: every thread, even split (the baseline).
	constexpr double k_parallel_chunk_overhead_ratio = 3.0;
	constexpr double k_parallel_min_overhead_ns = 2000.0;   // floor for a noisy first sample
	// Body control flow. Inline mode (the body grafted into the worker's counter loop): a
	// top-level `continue` reaches the loop's advance on its own and a top-level `return`
	// (any value) becomes `continue` - both mean the next element; a `return` inside a NESTED
	// loop cannot mean that in a grafted body, so such a body takes call mode instead (one
	// call per element, where a top-level `continue` becomes a bare `return`). A loop-level
	// `break` has no deterministic meaning (iterations are independent) and is rejected in
	// both modes; inside a nested loop or switch break/continue keep their C++ meaning.
	struct body_control_scan {
		const statement* bad_break = nullptr;
		bool nested_return = false;
	};
	void scan_parallel_body_control(const statement_ptr& slot, size_t loop_depth, size_t switch_depth, body_control_scan& out) {
		if (!slot || out.bad_break) { return; }
		switch (slot->get_type()) {
		case node_type::return_stmt:
			if (loop_depth > 0) { out.nested_return = true; }
			return;
		case node_type::break_stmt:
			if (loop_depth == 0 && switch_depth == 0) { out.bad_break = slot.get(); }
			return;
		case node_type::block_stmt:
			for (auto& d : static_cast<block_stmt*>(slot.get())->declarations) {
				if (d && d->get_type() == node_type::statement_decl) {
					scan_parallel_body_control(static_cast<statement_decl*>(d.get())->statement, loop_depth, switch_depth, out);
				}
			}
			return;
		case node_type::if_stmt: {
			auto* n = static_cast<if_stmt*>(slot.get());
			scan_parallel_body_control(n->then_statement, loop_depth, switch_depth, out);
			scan_parallel_body_control(n->else_statement, loop_depth, switch_depth, out);
			return;
		}
		case node_type::while_stmt:
			scan_parallel_body_control(static_cast<while_stmt*>(slot.get())->body, loop_depth + 1, switch_depth, out); return;
		case node_type::for_stmt:
			scan_parallel_body_control(static_cast<for_stmt*>(slot.get())->body, loop_depth + 1, switch_depth, out); return;
		case node_type::range_for_stmt:
			scan_parallel_body_control(static_cast<range_for_stmt*>(slot.get())->body, loop_depth + 1, switch_depth, out); return;
		case node_type::try_stmt: {
			auto* n = static_cast<try_stmt*>(slot.get());
			scan_parallel_body_control(n->try_block, loop_depth, switch_depth, out);
			scan_parallel_body_control(n->catch_block, loop_depth, switch_depth, out);
			return;
		}
		case node_type::switch_stmt: {
			auto* n = static_cast<switch_stmt*>(slot.get());
			for (auto& c : n->cases) { for (auto& st : c->body) { scan_parallel_body_control(st, loop_depth, switch_depth + 1, out); } }
			if (n->default_case) { for (auto& st : n->default_case->body) { scan_parallel_body_control(st, loop_depth, switch_depth + 1, out); } }
			return;
		}
		default:
			return;
		}
	}
	// Top-level control rewrite (loop depth 0 only; nested loops keep theirs): inline mode
	// turns `return` into `continue`, call mode turns `continue` into a bare `return`.
	void rewrite_parallel_body_control(statement_ptr& slot, size_t loop_depth, bool inline_mode) {
		if (!slot) { return; }
		switch (slot->get_type()) {
		case node_type::return_stmt:
			if (inline_mode && loop_depth == 0) { slot = std::make_shared<continue_stmt>(slot->location); }
			return;
		case node_type::continue_stmt:
			if (!inline_mode && loop_depth == 0) { slot = std::make_shared<return_stmt>(slot->location); }
			return;
		case node_type::block_stmt:
			for (auto& d : static_cast<block_stmt*>(slot.get())->declarations) {
				if (d && d->get_type() == node_type::statement_decl) {
					rewrite_parallel_body_control(static_cast<statement_decl*>(d.get())->statement, loop_depth, inline_mode);
				}
			}
			return;
		case node_type::if_stmt: {
			auto* n = static_cast<if_stmt*>(slot.get());
			rewrite_parallel_body_control(n->then_statement, loop_depth, inline_mode);
			rewrite_parallel_body_control(n->else_statement, loop_depth, inline_mode);
			return;
		}
		case node_type::while_stmt:
			rewrite_parallel_body_control(static_cast<while_stmt*>(slot.get())->body, loop_depth + 1, inline_mode); return;
		case node_type::for_stmt:
			rewrite_parallel_body_control(static_cast<for_stmt*>(slot.get())->body, loop_depth + 1, inline_mode); return;
		case node_type::range_for_stmt:
			rewrite_parallel_body_control(static_cast<range_for_stmt*>(slot.get())->body, loop_depth + 1, inline_mode); return;
		case node_type::try_stmt: {
			auto* n = static_cast<try_stmt*>(slot.get());
			rewrite_parallel_body_control(n->try_block, loop_depth, inline_mode);
			rewrite_parallel_body_control(n->catch_block, loop_depth, inline_mode);
			return;
		}
		case node_type::switch_stmt: {
			auto* n = static_cast<switch_stmt*>(slot.get());
			for (auto& c : n->cases) { for (auto& st : c->body) { rewrite_parallel_body_control(st, loop_depth, inline_mode); } }
			if (n->default_case) { for (auto& st : n->default_case->body) { rewrite_parallel_body_control(st, loop_depth, inline_mode); } }
			return;
		}
		default:
			return;
		}
	}

	inline size_t cap_workers_by_cost(size_t worker_count, size_t n, const parallel_engine_state& state, const parallel_engine_state::parallel_region_pool& pool) {
		if (pool.ns_per_element <= 0.0 || state.worker_overhead_ns <= 0.0) { return worker_count; }
		const double min_chunk_ns = k_parallel_chunk_overhead_ratio * std::max(state.worker_overhead_ns, k_parallel_min_overhead_ns);
		const double affordable = (static_cast<double>(n) * pool.ns_per_element) / min_chunk_ns;
		const size_t by_cost = affordable >= 1.0 ? static_cast<size_t>(affordable) : 1;
		return std::min(worker_count, by_cost);
	}

	// This REGION's persistent slot pool by fingerprint (fn body + backend): distinct regions
	// cycling in one frame each keep their own provisioned contexts. LRU-evict beyond the cap;
	// selected before any slot reference is taken (the pools vector must not reallocate for
	// the rest of the call).
	parallel_engine_state::parallel_region_pool& acquire_region_pool(parallel_engine_state& state, const void* pool_key, bool use_vm) {
		parallel_engine_state::parallel_region_pool* pool = nullptr;
		for (auto& candidate : state.region_pools) {
			if (candidate.fn_body == pool_key && candidate.use_vm == use_vm) {
				pool = &candidate;
				break;
			}
		}
		if (!pool) {
			if (state.region_pools.size() >= parallel_engine_state::max_region_pools) {
				auto evict = state.region_pools.begin();
				for (auto it = state.region_pools.begin(); it != state.region_pools.end(); ++it) {
					if (it->last_use < evict->last_use) { evict = it; }
				}
				*evict = parallel_engine_state::parallel_region_pool{};
				pool = &*evict;
			} else {
				state.region_pools.emplace_back();
				pool = &state.region_pools.back();
			}
			pool->fn_body = pool_key;
			pool->use_vm = use_vm;
		}
		pool->last_use = ++state.region_use_counter;
		return *pool;
	}

	// Main's own chunk, timed: the element cost as the worker path runs it feeds the next region
	inline void note_region_cost(parallel_engine_state::parallel_region_pool& pool, std::chrono::steady_clock::time_point chunk_t0, size_t main_elements) {
		if (!main_elements) { return; }
		const auto chunk_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - chunk_t0).count();
		const double sample = static_cast<double>(chunk_ns) / static_cast<double>(main_elements);
		pool.ns_per_element = pool.ns_per_element > 0.0 ? (pool.ns_per_element + sample) * 0.5 : sample;   // half-life of one region: no flapping on a noisy sample
	}

	// Once per pool build: what a pool worker costs to submit, wake and join on this machine
	// (no-op tasks, three round trips, the minimum - the first trip also warms the threads).
	// Paid once by the first fan-out region, ~100 us; results never depend on it.
	inline void calibrate_pool_overhead(parallel_engine_state& state) {
		const size_t workers = state.pool->worker_count();
		if (!workers) { return; }
		std::vector<std::pair<size_t, jai::thread_pool::task_type>> tasks;
		state.faults.checkpoint("calibration.task_storage");
		tasks.reserve(workers);
		for (size_t k = 0; k < workers; ++k) {
			state.faults.checkpoint("calibration.task_build", k);
			tasks.emplace_back(k, [] {});
		}
		double best = 0.0;
		for (int round = 0; round < 3; ++round) {
			const auto t0 = std::chrono::steady_clock::now();
			state.pool->submit_pinned_batch(tasks.data(), tasks.size(), &state.faults);
			state.pool->wait_idle();
			const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
			const double per_worker = ns / static_cast<double>(workers);
			best = best > 0.0 ? std::min(best, per_worker) : per_worker;
		}
		state.worker_overhead_ns = best;
	}

	// v0 host whitelist: pure math (stdlib/math.hpp minus the random family) + to_string.
	// Everything else — print, io, json, user bindings — errors at admission until the
	// parallel_for annotation surface (parallel_design.md §5) exists.
	bool parallel_host_whitelisted(std::string_view name) {
		static constexpr std::string_view k_whitelist[] = {
			"abs", "sign", "min", "max", "clamp",
			"floor", "ceil", "round", "trunc",
			"sqrt", "cbrt", "pow", "exp", "exp2", "log", "log2", "log10",
			"sin", "cos", "tan", "asin", "acos", "atan", "atan2",
			"sinh", "cosh", "tanh",
			"degrees", "radians", "fmod", "hypot",
			"lerp", "mix", "unmix", "mix_in", "mix_out", "mix_in_out", "mix_out_in",
			"unmix_in", "unmix_out", "unmix_in_out", "unmix_out_in",
			"saturate", "wrap", "remap", "is_nan", "is_inf",
			"to_string",
		};
		for (auto w : k_whitelist) {
			if (name == w) { return true; }
		}
		return false;
	}

	bool primitive_ctor_name(std::string_view name) {
		return name == "int" || name == "int64" || name == "float" || name == "double" ||
		       name == "string" || name == "char" || name == "bool";
	}

	// Read-only builtin container/string methods admissible on a CAPTURED receiver
	// (union of the array/map/string registries' non-mutating entries). Captured method
	// receivers always provision as per-worker snapshots, so even element-copying
	// methods (keys/values/slice/front/...) only touch worker-private structure. A
	// method outside this list may mutate the receiver - that write would land on the
	// worker's private snapshot and silently diverge across worker counts, so admission
	// rejects it (the statically-free half of the write wall).
	bool parallel_readonly_method(std::string_view name) {
		static constexpr std::string_view k_readonly[] = {
			// array
			"size", "empty", "front", "back", "index_of", "has", "contains",
			"first", "last", "length", "slice", "join",
			// map
			"keys", "values", "get", "to_array",
			// string
			"at", "substr", "find", "rfind", "find_first_of", "find_last_of",
			"find_first_not_of", "find_last_not_of",
		};
		for (auto m : k_readonly) {
			if (name == m) { return true; }
		}
		return false;
	}

	const script_callable* payload_from_function_value(const script_value& v) {
		if (!v.is_function()) { return nullptr; }
		const script_function& f = v.as_function();
		if (const auto* thunk = f.target<script_callable_thunk>()) { return &thunk->payload; }
		return nullptr;
	}

	// ============================== ADMISSION ==============================

	struct admission_builder {
		engine& eng;
		string_symbolizer& sym;
		parallel_admission adm;
		std::unordered_set<const void*> visited_bodies;
		std::unordered_set<uint64_t> collected_fn_ids;
		std::unordered_set<uint64_t> collected_host_ids;
		std::unordered_set<type_info*> collected_types;
		std::unordered_map<uint64_t, size_t> capture_index;   // name id -> adm.captures slot
		std::vector<std::unordered_set<uint64_t>> scopes;   // current function's scopes
		bool failed = false;
		// parallel_for bodies (in-place root mode): the element may bind by reference and
		// diagnostics carry the parallel_for prefix. Member access and member STORES are
		// admitted in BOTH forms when the chain roots at a LOCAL identifier - the element
		// (parallel_for's chunk-exclusive owned element, the transform's private by-value
		// copy) or a worker-private local. Enclosing-rooted member access stays rejected
		// (capture classification cannot provision objects). Applies to the whole call
		// graph: helper functions do member work on their own params.
		bool inplace_root = false;
		uint64_t element_symbol = UINT64_MAX;   // the body's first element parameter / loop variable
		std::vector<uint64_t> element_symbols;   // every element root: the zip's further loop variables too
		bool is_element_symbol(uint64_t id) const {
			if (id == element_symbol) { return true; }
			for (uint64_t e : element_symbols) { if (e == id) { return true; } }
			return false;
		}
		const char* prefix = "parallel_transform";

		// Read-only parameter proof (prove_readonly_parameters): the root is a by-value parameter
		// of a serial callee. The parallel rules already reject every enclosing write; on top,
		// any identity-observing use of the root escapes it - a bare use (comparison, argument,
		// return, alias, range source), a member/subscript chain whose static terminal is not a
		// primitive or string (objects and arrays compare by identity), a user method or
		// accessor on it, a compound assignment or ++/-- on it (user operators). Rebinding the
		// parameter itself (`p = x`) is not an escape: it replaces the slot, never the referent.
		bool proof_mode = false;
		bool root_escaped = false;
		type_info_ptr root_type;

		admission_builder(engine& e) : eng(e), sym(*e.get_symbolizer()) { adm.admitted = false; }

		static bool primitive_or_string(const type_info_ptr& t) {
			if (!t) { return false; }
			switch (t->base_type) {
			case script_value_type::jai_int_type: case script_value_type::jai_float_type:
			case script_value_type::jai_bool_type: case script_value_type::jai_char_type:
			case script_value_type::jai_string_type:
				return true;
			default:
				return false;
			}
		}
		// Static type of a member/subscript chain rooted at the proof root (null = unknown or
		// a step that can run user code: a user accessor, a field no class declares)
		type_info_ptr proof_chain_type(const expression* e) {
			if (!e) { return nullptr; }
			if (e->get_type() == node_type::identifier_expr) {
				return is_element_symbol(static_cast<const identifier_expr*>(e)->symbol_id) ? root_type : nullptr;
			}
			if (e->get_type() == node_type::member_expr) {
				auto* m = const_cast<member_expr*>(static_cast<const member_expr*>(e));
				type_info_ptr base = proof_chain_type(m->object.get());
				if (!base || base->base_type != script_value_type::jai_object_type || base->type_name.empty()) { return nullptr; }
				auto cls = eng.get_class_registry().find_script_class(std::string(base->type_name));
				if (!cls) { return nullptr; }
				if (m->member_id == UINT64_MAX) { m->member_id = sym.intern(m->member); }
				if (cls->user_accessor(m->member_id) != nullptr) { return nullptr; }
				if (!cls->find_field_declaring_class(m->member_id)) { return nullptr; }
				return cls->get_field_declared_type(m->member_id);
			}
			if (e->get_type() == node_type::binary_expr) {
				auto* b = static_cast<const binary_expr*>(e);
				if (b->op.type != token_type::left_bracket) { return nullptr; }
				type_info_ptr base = proof_chain_type(b->left.get());
				if (!base) { return nullptr; }
				if (base->base_type == script_value_type::jai_array_type) { return base->element_type(); }
				if (base->base_type == script_value_type::jai_map_type) { return base->value_type(); }
				if (base->base_type == script_value_type::jai_string_type) { return base; }   // a char: no identity, stands in as its string type
				return nullptr;
			}
			return nullptr;
		}
		// Proof mode: a chain rooted at the parameter, read as a VALUE. Index expressions walk
		// under the normal rules; the terminal must be statically primitive or string
		bool proof_chain_value(const expression_ptr& e) {
			if (!walk_chain_indexes(e.get())) { return false; }
			if (!primitive_or_string(proof_chain_type(e.get()))) { root_escaped = true; }
			return true;
		}
		bool walk_chain_indexes(const expression* e) {
			while (e) {
				if (e->get_type() == node_type::member_expr) { e = static_cast<const member_expr*>(e)->object.get(); continue; }
				if (e->get_type() == node_type::binary_expr) {
					auto* b = static_cast<const binary_expr*>(e);
					if (!walk_expr(b->right)) { return false; }
					e = b->left.get();
					continue;
				}
				break;
			}
			return true;
		}

		// Captured-read touch collection (the CHEAP half of the two-layer design: the
		// walk only records which enclosing names the body touches and whether every
		// touch is a subscript read; enforcement of no-writes is the write wall plus
		// the statically-free rejections below). subscript_root keeps the name eligible
		// for the borrow (raw-read) tier; any other read demotes it to snapshot.
		void touch_capture(identifier_expr* n, bool subscript_root) {
			auto [it, inserted] = capture_index.try_emplace(n->symbol_id, adm.captures.size());
			if (inserted) {
				adm.captures.push_back({ n->symbol_id, n->name, subscript_root });   // name view = symbolizer storage (permanent)
			} else if (!subscript_root) {
				adm.captures[it->second].borrow_eligible = false;
			}
		}

		// Set while walking a script-class method body at the barrier: bare names that are
		// the class's own fields root at the ELEMENT (chunk-exclusive — reads AND writes
		// admitted, exactly as the same access spelled inline in the loop body would be).
		const class_definition* method_class = nullptr;

		bool self_field(uint64_t id) const {
			return method_class && method_class->find_field_declaring_class(id) != nullptr;
		}

		void collect_element_method(const member_expr* m) {
			for (const auto& rec : adm.element_method_calls) {
				if (rec.name_id == m->member_id) { return; }
			}
			adm.element_method_calls.push_back({ m->member_id, m->member, m });
		}

		// In-place mode member access (read or store position): admitted iff the chain
		// roots at a LOCAL identifier - the owned element or a worker-private local.
		// Index expressions inside the chain walk under the normal rules. Every member
		// node's interned ids (member/getter/setter) warm HERE, single-threaded: worker
		// field access probes them against the frozen symbolizer.
		void note_reached_member(uint64_t id) {
			auto& v = adm.element_reached_members;
			if (std::find(v.begin(), v.end(), id) == v.end()) { v.push_back(id); }
		}
		// The element used as a VALUE (not the root of a member chain): every field is reachable
		void note_element_whole() { adm.element_reach_all = true; }
		bool chain_root_is_element(const expression_ptr& e) const {
			const expression* cur = e.get();
			while (cur) {
				if (cur->get_type() == node_type::member_expr) { cur = static_cast<const member_expr*>(cur)->object.get(); continue; }
				if (cur->get_type() == node_type::binary_expr && static_cast<const binary_expr*>(cur)->op.type == token_type::left_bracket) {
					cur = static_cast<const binary_expr*>(cur)->left.get(); continue;
				}
				break;
			}
			return cur && cur->get_type() == node_type::identifier_expr && is_element_symbol(static_cast<const identifier_expr*>(cur)->symbol_id);
		}
		// A chain terminal read as a value (copied: every handle below it) or replaced (the old
		// subtree released): its whole subtree is reachable, not just the members named below it
		void note_reached_whole(uint64_t id) {
			auto& v = adm.element_reached_whole;
			if (std::find(v.begin(), v.end(), id) == v.end()) { v.push_back(id); }
		}
		bool walk_local_member_chain(const member_expr* m, const char* what) {
			std::vector<const expression_ptr*> indexes;
			const expression* cur = m;
			identifier_expr* root = nullptr;
			bool rooted_at_self = false;
			while (cur) {
				if (cur->get_type() == node_type::identifier_expr) {
					root = const_cast<identifier_expr*>(static_cast<const identifier_expr*>(cur));
					break;
				}
				if (cur->get_type() == node_type::this_expr) {
					rooted_at_self = true;   // method-body chain on the element itself
					break;
				}
				if (cur->get_type() == node_type::member_expr) {
					auto* node = const_cast<member_expr*>(static_cast<const member_expr*>(cur));
					if (node->member_id == UINT64_MAX) { node->member_id = sym.intern(node->member); }
					note_reached_member(node->member_id);
					if (cur == m) { note_reached_whole(node->member_id); }
					node->getter_id = sym.get_getter_id_with_view(node->member_id).first;
					(void)sym.get_setter_id_with_view(node->member_id);
					cur = node->object.get();
					continue;
				}
				if (cur->get_type() == node_type::binary_expr) {
					auto* b = static_cast<const binary_expr*>(cur);
					if (b->op.type != token_type::left_bracket) { break; }
					indexes.push_back(&b->right);
					cur = b->left.get();
					continue;
				}
				break;
			}
			if (rooted_at_self && !method_class) {
				return fail(m, "'this' is not allowed in a parallel body");
			}
			if (!root && !rooted_at_self) {
				return fail(m, std::string(what) + " must root at a local variable in a parallel body");
			}
			if (root && !is_local(root->symbol_id) && !self_field(root->symbol_id)) {
				return fail(m, std::string(what) + " reaches captured state '" + std::string(root->name) +
				               "' (captured reads are read-only, and objects cannot be captured)");
			}
			for (const expression_ptr* idx : indexes) {
				if (!walk_expr(*idx)) { return false; }
			}
			return true;
		}

		// The enclosing identifier at the root of a bare name or a [ ]-chain
		// (nullptr when the expression roots elsewhere or at a local)
		identifier_expr* enclosing_root(const expression_ptr& e) const {
			const expression* cur = e.get();
			while (cur) {
				if (cur->get_type() == node_type::identifier_expr) {
					auto* ident = static_cast<identifier_expr*>(const_cast<expression*>(cur));
					// Self fields are element-rooted, never enclosing state
					return (is_local(ident->symbol_id) || self_field(ident->symbol_id)) ? nullptr : ident;
				}
				if (cur->get_type() == node_type::binary_expr &&
				    static_cast<const binary_expr*>(cur)->op.type == token_type::left_bracket) {
					cur = static_cast<const binary_expr*>(cur)->left.get();
					continue;
				}
				return nullptr;
			}
			return nullptr;
		}

		bool fail(const ast_node* node, std::string reason) {
			if (!failed) {
				failed = true;
				adm.admitted = false;
				adm.code = make_error_code(runtime_error_code::unsupported_operation);
				adm.message = std::string(prefix) + ": " + std::move(reason);
				if (node) {
					adm.message += " at " + std::to_string(node->location.line) + ":" +
					               std::to_string(node->location.column);
				}
			}
			return false;
		}

		void collect_type(type_info_ptr t) {
			if (t.get() && collected_types.insert(t.get()).second) {
				adm.referenced_types.push_back(t.get());
			}
		}

		void declare(uint64_t id) { scopes.back().insert(id); }
		bool is_local(uint64_t id) const {
			for (const auto& s : scopes) {
				if (s.count(id)) { return true; }
			}
			return false;
		}

		// Root-parameter contract for the region's entry body: transform requires a
		// by-value unary param; parallel_for takes either (auto& mutates in place,
		// plain auto is a side-effect-free worker copy - Dev ruling)
		enum class root_param { none, unary_value, unary };

		bool admit_function(const std::shared_ptr<script_defined_function>& fn, uint64_t global_name_id,
		                    root_param param_mode, const ast_node* call_node) {
			if (!fn || !fn->body) {
				return fail(call_node, "fn has no body known at admission time");
			}
			if (param_mode != root_param::none) {
				if (fn->parameters().empty() || (param_mode == root_param::unary_value && fn->parameters().size() != 1)) {
					return fail(fn->body.get(), "fn must take exactly one parameter");
				}
				if (param_mode == root_param::unary_value && fn->parameters()[0].is_reference) {
					return fail(fn->body.get(), "fn may not take its element by reference (elements are detached copies)");
				}
			}
			// Collect the graph entry before the visited check so recursion still records it
			if (global_name_id != UINT64_MAX) {
				if (collected_fn_ids.insert(global_name_id).second) {
					adm.script_functions.emplace_back(global_name_id, fn);
				}
			} else {
				adm.script_functions.emplace_back(global_name_id, fn);
			}
			if (!visited_bodies.insert(fn->body.get()).second) {
				return true;   // already walked (recursion / shared target)
			}

			// Fresh scope stack per function
			std::vector<std::unordered_set<uint64_t>> saved;
			saved.swap(scopes);
			scopes.emplace_back();
			bool ok = true;
			for (const auto& p : fn->parameters()) {
				if (p.symbol_id == UINT64_MAX) { p.symbol_id = sym.intern(p.name); }   // warm (idempotent lazy fill)
				declare(p.symbol_id);
				if (param_mode != root_param::none) {
					if (element_symbol == UINT64_MAX) { element_symbol = p.symbol_id; } else { element_symbols.push_back(p.symbol_id); }
				}
				collect_type(p.type);
				if (p.default_value && !walk_expr(p.default_value)) { ok = false; break; }
			}
			if (ok) {
				collect_type(fn->return_type);
				for (const auto& d : fn->body->declarations) {
					if (!walk_stmt(d)) { ok = false; break; }
				}
			}
			scopes.swap(saved);
			return ok;
		}

		// ===== Barrier-time class-method admission (parallel-method-admission ruling) =====
		// A method on the chunk-exclusive element is exactly as safe as the same accesses
		// spelled inline in the loop body — the walk proves it: self fields read/write OK,
		// captured global reads collect as ordinary captures, everything shared-mutating
		// gets a verdict naming the violation. v1 admits single-overload non-coroutine
		// methods defined on the element's own class.
		struct admitted_method {
			const class_definition* cls = nullptr;
			uint64_t name_id = 0;
			std::string_view name;   // symbolizer storage (permanent)
			std::shared_ptr<function_decl> decl;
		};
		std::vector<admitted_method> admitted_methods;
		std::unordered_set<const function_decl*> visited_methods;

		bool admit_class_method(const class_definition* cls, uint64_t name_id, std::string_view name, const ast_node* site) {
			auto decl = cls->single_method_overload_ptr(name_id);
			if (!decl) {
				return fail(site, "method '" + std::string(name) + "' on class '" + cls->get_name() +
				               "' is not admitted in a parallel body (only single-overload methods defined on the element's own class)");
			}
			if (decl->is_coroutine) {
				return fail(site, "coroutine method '" + std::string(name) + "' is not allowed in a parallel body");
			}
			if (!decl->body) {
				return fail(site, "method '" + std::string(name) + "' has no body known at admission time");
			}
			// The pinned worker dispatch bypasses the getter-precedence probe and the
			// access-context check — the barrier proves both moot instead
			if (cls->find_field_declaring_class(name_id) != nullptr) {
				return fail(site, "'" + std::string(name) + "' names both a field and a method on class '" +
				               cls->get_name() + "' — not admitted in a parallel body (v1)");
			}
			if (cls->defines_method(sym.get_getter_id_with_view(name_id).first)) {
				return fail(site, "method '" + std::string(name) + "' is shadowed by a property getter on class '" +
				               cls->get_name() + "' — not admitted in a parallel body (v1)");
			}
			if (cls->chain_has_nonpublic()) {
				return fail(site, "class '" + cls->get_name() +
				               "' has non-public members — its methods are not admitted in a parallel body (v1)");
			}
			if (!visited_methods.insert(decl.get()).second) { return true; }   // recursion / already admitted
			// Pre-mint the shared parameter storage (a mutable lazy cache on the AST
			// node): workers dispatching this method concurrently must only ever READ it
			if (!decl->shared_params_cache) {
				decl->shared_params_cache = std::make_shared<const std::vector<parameter>>(decl->parameters);
			}
			admitted_methods.push_back({ cls, name_id, name, decl });

			const class_definition* saved_class = method_class;
			method_class = cls;
			std::vector<std::unordered_set<uint64_t>> saved;
			saved.swap(scopes);
			scopes.emplace_back();
			bool ok = true;
			for (const auto& p : decl->parameters) {
				if (p.symbol_id == UINT64_MAX) { p.symbol_id = sym.intern(p.name); }   // warm (idempotent lazy fill)
				declare(p.symbol_id);
				collect_type(p.type);
				if (p.default_value && !walk_expr(p.default_value)) { ok = false; break; }
			}
			if (ok) {
				collect_type(decl->return_type);
				for (const auto& d : decl->body->declarations) {
					if (!walk_stmt(d)) { ok = false; break; }
				}
			}
			scopes.swap(saved);
			method_class = saved_class;
			return ok;
		}

		// Barrier entry: resolve the body's collected element-method names against the
		// live element classes. Names no class defines are builtins — ignored (runtime
		// precedence is unchanged for them).
		bool admit_element_methods(const std::vector<const class_definition*>& classes) {
			// Index walk: admitting a method body collects ITS this.x() calls into the
			// same worklist (transitive admission), so the vector grows under us
			for (size_t i = 0; i < adm.element_method_calls.size(); ++i) {
				const auto callrec = adm.element_method_calls[i];   // copy: push_back may reallocate
				for (const class_definition* cls : classes) {
					if (!cls->defines_method(callrec.name_id)) { continue; }
					if (!admit_class_method(cls, callrec.name_id, callrec.name, callrec.site)) { return false; }
				}
			}
			return true;
		}

		bool walk_stmt(const statement_ptr& s) {
			if (!s) { return true; }
			switch (s->get_type()) {
			case node_type::expression_stmt:
				return walk_expr(static_cast<expression_stmt*>(s.get())->expression);
			case node_type::expression_decl:
				return walk_expr(static_cast<expression_decl*>(s.get())->expression);
			case node_type::statement_decl:
				return walk_stmt(static_cast<statement_decl*>(s.get())->statement);
			case node_type::block_stmt: {
				auto* b = static_cast<block_stmt*>(s.get());
				scopes.emplace_back();
				for (const auto& d : b->declarations) {
					if (!walk_stmt(d)) { scopes.pop_back(); return false; }
				}
				scopes.pop_back();
				return true;
			}
			case node_type::if_stmt: {
				auto* n = static_cast<if_stmt*>(s.get());
				return walk_expr(n->condition) && walk_stmt(n->then_statement) && walk_stmt(n->else_statement);
			}
			case node_type::while_stmt: {
				auto* n = static_cast<while_stmt*>(s.get());
				return walk_expr(n->condition) && walk_stmt(n->body);
			}
			case node_type::for_stmt: {
				auto* n = static_cast<for_stmt*>(s.get());
				scopes.emplace_back();
				bool ok = walk_stmt(n->initializer) && walk_expr(n->condition) &&
				          walk_expr(n->update) && walk_stmt(n->body);
				scopes.pop_back();
				return ok;
			}
			case node_type::range_for_stmt: {
				auto* n = static_cast<range_for_stmt*>(s.get());
				if (n->is_reference) {
					// By-ref iteration mutates elements in place - on captured state
					// that is a write (and on a snapshot it would silently diverge)
					if (identifier_expr* root = enclosing_root(n->container)) {
						return fail(n, "cannot iterate captured state '" + std::string(root->name) +
						               "' by reference in a parallel body (captured reads are read-only)");
					}
				}
				if (!walk_expr(n->container)) { return false; }
				if (n->variable_name_id == UINT64_MAX) { n->variable_name_id = sym.intern(n->variable_name); }
				collect_type(n->element_type);
				scopes.emplace_back();
				declare(n->variable_name_id);
				bool ok = walk_stmt(n->body);
				scopes.pop_back();
				return ok;
			}
			case node_type::return_stmt:
				return walk_expr(static_cast<return_stmt*>(s.get())->value);
			case node_type::break_stmt:
			case node_type::continue_stmt:
			case node_type::fallthrough_stmt:
				return true;
			case node_type::switch_stmt: {
				auto* n = static_cast<switch_stmt*>(s.get());
				if (!walk_expr(n->condition)) { return false; }
				for (const auto& c : n->cases) {
					if (!walk_expr(c->value)) { return false; }
					scopes.emplace_back();
					for (const auto& st : c->body) {
						if (!walk_stmt(st)) { scopes.pop_back(); return false; }
					}
					scopes.pop_back();
				}
				if (n->default_case) {
					scopes.emplace_back();
					for (const auto& st : n->default_case->body) {
						if (!walk_stmt(st)) { scopes.pop_back(); return false; }
					}
					scopes.pop_back();
				}
				return true;
			}
			case node_type::try_stmt: {
				auto* n = static_cast<try_stmt*>(s.get());
				if (!walk_stmt(n->try_block)) { return false; }
				scopes.emplace_back();
				if (!n->catch_var.empty()) { declare(sym.intern(n->catch_var)); }
				bool ok = walk_stmt(n->catch_block);
				scopes.pop_back();
				return ok;
			}
			case node_type::variable_decl: {
				auto* n = static_cast<variable_decl*>(s.get());
				if (n->name_id == UINT64_MAX) { n->name_id = sym.intern(n->name); }
				collect_type(n->type);
				bool ok;
				if (n->type.get() && n->type->is_reference()) {
					// Reference declaration: the alias must root at worker-local storage
					ok = walk_target(n->initializer, "reference declaration", /*aliasing*/ true);
				} else {
					ok = walk_expr(n->initializer);
				}
				if (!ok) { return false; }
				declare(n->name_id);
				return true;
			}
			case node_type::destructuring_decl: {
				auto* n = static_cast<destructuring_decl*>(s.get());
				if (!walk_expr(n->initializer)) { return false; }
				for (const auto& [name, id] : n->names) { declare(id); }
				return true;
			}
			case node_type::function_decl:
				return fail(s.get(), "function declarations are not allowed in a parallel body (v0)");
			case node_type::class_decl:
				return fail(s.get(), "class declarations are not allowed in a parallel body");
			case node_type::namespace_decl:
				return fail(s.get(), "namespace declarations are not allowed in a parallel body");
			case node_type::enum_decl:
				return fail(s.get(), "enum declarations are not allowed in a parallel body");
			case node_type::include_decl:
			case node_type::import_decl:
				return fail(s.get(), "include/import is not allowed in a parallel body");
			default:
				return fail(s.get(), "statement is not allowed in a parallel body (v0)");
			}
		}

		bool walk_expr(const expression_ptr& e) {
			if (!e) { return true; }
			switch (e->get_type()) {
			case node_type::literal_expr:
				return true;
			case node_type::identifier_expr: {
				auto* n = static_cast<identifier_expr*>(e.get());
				if (is_element_symbol(n->symbol_id)) { note_element_whole(); if (proof_mode) { root_escaped = true; } }
				if (is_local(n->symbol_id)) { return true; }
				if (self_field(n->symbol_id)) { return true; }   // element-rooted field read
				if (method_class && method_class->defines_method(n->symbol_id)) {
					return fail(n, "method '" + std::string(n->name) + "' cannot be captured as a value in a parallel body");
				}
				// Captured read (whole-value use): the barrier classifies and provisions
				// it per worker; writes to it are rejected at walk_target / the wall
				touch_capture(n, false);
				return true;
			}
			case node_type::binary_expr: {
				auto* n = static_cast<binary_expr*>(e.get());
				if (n->op.type == token_type::left_bracket) {
					// Subscript READ rooted at an enclosing name: the borrow-eligible
					// capture shape (raw element reads through the region borrow)
					if (n->left && n->left->get_type() == node_type::identifier_expr) {
						auto* root = static_cast<identifier_expr*>(n->left.get());
						if (!is_local(root->symbol_id)) {
							touch_capture(root, true);
							return walk_expr(n->right);
					}
					}
					if (proof_mode && chain_root_is_element(e)) { return proof_chain_value(e); }
				}
				return walk_expr(n->left) && walk_expr(n->right);
			}
			case node_type::unary_expr: {
				auto* n = static_cast<unary_expr*>(e.get());
				if (n->op.type == token_type::plus_plus || n->op.type == token_type::minus_minus) {
					if (proof_mode && chain_root_is_element(n->operand)) { root_escaped = true; }   // a user operator could run on the referent
					return walk_target(n->operand, "increment/decrement");
				}
				return walk_expr(n->operand);
			}
			case node_type::assignment_expr: {
				auto* n = static_cast<assignment_expr*>(e.get());
				if (proof_mode && n->op.type != token_type::equal && chain_root_is_element(n->target)) { root_escaped = true; }
				return walk_target(n->target, "assignment") && walk_expr(n->value);
			}
			case node_type::call_expr:
				return walk_call(static_cast<call_expr*>(e.get()));
			case node_type::member_expr:
				if (proof_mode && chain_root_is_element(e)) { return proof_chain_value(e); }
				return walk_local_member_chain(static_cast<member_expr*>(e.get()), "member access");
			case node_type::lambda_expr:
				return fail(e.get(), "lambdas are not allowed in a parallel body (v0)");
			case node_type::new_expr:
				return fail(e.get(), "'new' is not allowed in a parallel body");
			case node_type::this_expr:
				if (method_class) { return true; }   // self IS the chunk-exclusive element
				return fail(e.get(), "'this'/'super' is not allowed in a parallel body");
			case node_type::super_expr:
				return fail(e.get(), "'this'/'super' is not allowed in a parallel body");
			case node_type::ternary_expr: {
				auto* n = static_cast<ternary_expr*>(e.get());
				return walk_expr(n->condition) && walk_expr(n->then_expression) && walk_expr(n->else_expression);
			}
			case node_type::array_literal_expr: {
				auto* n = static_cast<array_literal_expr*>(e.get());
				for (const auto& el : n->elements) {
					if (!walk_expr(el)) { return false; }
				}
				return true;
			}
			case node_type::map_literal_expr: {
				auto* n = static_cast<map_literal_expr*>(e.get());
				for (const auto& kv : n->entries) {
					if (!walk_expr(kv.first) || !walk_expr(kv.second)) { return false; }
				}
				return true;
			}
			case node_type::throw_expr:
				return walk_expr(static_cast<throw_expr*>(e.get())->value);
			case node_type::cast_expr: {
				auto* n = static_cast<cast_expr*>(e.get());
				if (n->by_reference) { return fail(e.get(), "cast<T&> is not allowed in a parallel body"); }
				return walk_expr(n->operand);
			}
			case node_type::yield_expr:
				return fail(e.get(), "yield is not allowed in a parallel body");
			case node_type::include_expr:
				return fail(e.get(), "include is not allowed in a parallel body");
			default:
				return fail(e.get(), "expression is not allowed in a parallel body (v0)");
			}
		}

		// Stores and reference bindings must root at worker-local storage. These are the
		// statically-FREE write rejections (early diagnostics for the runtime write
		// wall): a store or alias rooted at an enclosing name errors here with position.
		bool walk_target(const expression_ptr& t, const char* what, bool aliasing = false) {
			if (!t) { return fail(nullptr, std::string(what) + " target missing"); }
			switch (t->get_type()) {
			case node_type::identifier_expr: {
				auto* n = static_cast<identifier_expr*>(t.get());
				if (is_element_symbol(n->symbol_id)) { note_element_whole(); if (proof_mode && aliasing) { root_escaped = true; } }   // replaced: the old element releases every handle
				if (is_local(n->symbol_id)) { return true; }
				if (!aliasing && self_field(n->symbol_id)) { return true; }   // element field store (chunk-exclusive)
				if (aliasing) {
					return fail(n, std::string(what) + " would alias enclosing state '" + std::string(n->name) +
					               "' (captured reads are read-only)");
				}
				return fail(n, "body writes enclosing state '" + std::string(n->name) + "'");
			}
			case node_type::binary_expr: {
				auto* n = static_cast<binary_expr*>(t.get());
				if (n->op.type == token_type::left_bracket) {
					if (proof_mode && chain_root_is_element(t)) { adm.element_mutated = true; }
					return walk_target(n->left, what, aliasing) && walk_expr(n->right);
				}
				return fail(t.get(), std::string(what) + " target is not allowed in a parallel body");
			}
			case node_type::member_expr:
				if (!aliasing) {
					// Own-element / worker-local member store (parallel_for's output channel;
					// a store into the transform's private copy is legal and invisible);
					// enclosing-rooted chains still fail inside
					if (chain_root_is_element(t)) { adm.element_mutated = true; }
					return walk_local_member_chain(static_cast<member_expr*>(t.get()), "member store");
				}
				return fail(t.get(), "member stores are not allowed in a parallel body (v0)");
			default:
				return fail(t.get(), std::string(what) + " target is not allowed in a parallel body");
			}
		}

		bool walk_call(call_expr* call) {
			// Builtin container/string METHOD call: receiver expression walks under the
			// normal rules (so it roots at worker-local or captured values); the method
			// itself comes from the shared registry both backends use.
			if (call->callee && call->callee->get_type() == node_type::member_expr) {
				auto* m = static_cast<member_expr*>(call->callee.get());
				if (m->is_static) {
					// math:: language intrinsics are pure value functions - admitted.
					// The random trio stays rejected with its own message: it mutates
					// the ENGINE-OWNED rng (worker race + worker-count nondeterminism).
					const jai::detail::math_fn fn = jai::detail::math_intrinsic_for_call(m);
					if (fn == jai::detail::math_fn::random_ || fn == jai::detail::math_fn::random_range ||
					    fn == jai::detail::math_fn::random_seed) {
						return fail(m, "math::random/random_seed are not allowed in a parallel body (engine-owned rng state)");
					}
					if (fn != jai::detail::math_fn::none) {
						for (const auto& a : call->arguments) {
							if (!walk_expr(a)) { return false; }
						}
						return true;
					}
					return fail(m, "static member calls are not allowed in a parallel body");
				}
				if (m->member_id == UINT64_MAX) { m->member_id = sym.intern(m->member); }   // warm
				// A method on captured state must be a known read-only builtin: anything
				// else may mutate the (worker-private snapshot of the) receiver, which
				// would silently diverge across worker counts
				if (identifier_expr* root = enclosing_root(m->object)) {
					if (!parallel_readonly_method(m->member)) {
						return fail(m, "method '" + std::string(m->member) + "' may mutate captured state '" +
						               std::string(root->name) + "'");
					}
					if (m->object->get_type() == node_type::identifier_expr) {
						touch_capture(root, false);   // whole-container method receiver
					} else if (!walk_expr(m->object)) {
						return false;   // subscript-chain receiver: subscript walk records the touch
					}
				} else {
					// Element / worker-local receiver: record the name for the barrier's
					// class-method admission (the receiver's class is a runtime fact; names
					// no element class defines are builtins and simply never match)
					collect_element_method(m);
					if (chain_root_is_element(m->object) && !parallel_readonly_method(m->member)) { adm.element_mutated = true; }
					if (proof_mode && chain_root_is_element(m->object)) {
						// A read-only builtin on a receiver that is statically a container or string is a
						// read; a receiver of unknown or class type could run a user method on the referent
						if (!walk_chain_indexes(m->object.get())) { return false; }
						type_info_ptr rt = proof_chain_type(m->object.get());
						const bool container_or_string = rt && (rt->base_type == script_value_type::jai_array_type ||
						                                        rt->base_type == script_value_type::jai_map_type ||
						                                        rt->base_type == script_value_type::jai_string_type);
						if (!container_or_string || !parallel_readonly_method(m->member)) { root_escaped = true; }
					} else if (!walk_expr(m->object)) {
						return false;
					}
				}
				for (const auto& a : call->arguments) {
					if (!walk_expr(a)) { return false; }
				}
				return true;
			}
			if (!call->callee || call->callee->get_type() != node_type::identifier_expr) {
				return fail(call, "call target is not statically resolvable in a parallel body (v0)");
			}
			auto* callee = static_cast<identifier_expr*>(call->callee.get());
			const std::string callee_name(callee->name);
			if (is_local(callee->symbol_id)) {
				return fail(callee, "call target '" + callee_name + "' is not statically resolvable in a parallel body (v0)");
			}
			// Bare sibling-method calls resolve through the env→bound-method seam at
			// runtime (the engine-backend escape the pins exist to prevent); the
			// 'this.' spelling routes through the pinned dispatch op instead
			if (method_class && method_class->defines_method(callee->symbol_id)) {
				return fail(callee, "method '" + callee_name + "' must be called as 'this." + callee_name +
				               "()' in a parallel body (bare sibling calls are not admitted, v1)");
			}
			// Method bodies stay within the element: global-function graphs from methods
			// would mutate the cached admission per call — not admitted (v1)
			if (method_class && !primitive_ctor_name(callee->name)) {
				return fail(callee, "global function call '" + callee_name +
				               "' from a parallel method body is not admitted (v1)");
			}
			// Resolve the callee BEFORE walking arguments so the caller sees the most
			// useful violation first (e.g. a nested parallel_transform names itself,
			// not its function-valued argument)
			bool callee_ok = false;
			if (primitive_ctor_name(callee->name)) {
				callee_ok = true;   // int(x)/float(x)/... conversions - backend-resolved, pure
			} else {
				auto global = eng.get_global_environment()->get(callee->symbol_id);
				if (!global || !global.value().is_function()) {
					return fail(callee, "call target '" + callee_name + "' is not a function known at admission time");
				}
				if (const script_callable* payload = payload_from_function_value(global.value())) {
					if (payload->kind != script_callable::kind_type::function || !payload->fn) {
						return fail(callee, "call target '" + callee_name + "' is not a plain function (v0)");
					}
					if (!admit_function(payload->fn, callee->symbol_id, root_param::none, callee)) { return false; }
					// A by-ref parameter receiving captured state is a write channel
					// (the callee may store through it) - reject the alias statically
					const auto& params = payload->fn->parameters();
					for (size_t i = 0; i < call->arguments.size() && i < params.size(); ++i) {
						if (!params[i].is_reference) { continue; }
						if (identifier_expr* root = enclosing_root(call->arguments[i])) {
							return fail(root, "argument passes enclosing state '" + std::string(root->name) +
							                  "' by reference (captured reads are read-only)");
						}
					}
					callee_ok = true;
				} else if (parallel_host_whitelisted(callee->name)) {
					// Host binding (typed dispatcher / variadic): whitelist or reject
					if (collected_host_ids.insert(callee->symbol_id).second) {
						adm.host_functions.emplace_back(callee->symbol_id, callee_name);
					}
					callee_ok = true;
				} else {
					return fail(callee, "host function '" + callee_name + "' is not callable in a parallel body (v0)");
				}
			}
			for (const auto& a : call->arguments) {
				if (!walk_expr(a)) { return false; }
			}
			return callee_ok;
		}
	};

	bool prove_readonly_parameter(engine& eng, const std::shared_ptr<script_defined_function>& fn, size_t index) {
		const parameter& p = fn->parameters()[index];
		if (p.is_reference || p.ref_escaping) { return false; }
		admission_builder builder(eng);
		builder.proof_mode = true;
		builder.prefix = "parameter proof";
		if (p.symbol_id == UINT64_MAX) { p.symbol_id = builder.sym.intern(p.name); }
		builder.element_symbol = p.symbol_id;
		builder.root_type = p.type;
		const bool walked = builder.admit_function(fn, UINT64_MAX, admission_builder::root_param::none, nullptr);
		return walked && !builder.failed && !builder.adm.element_mutated && !builder.root_escaped;
	}

	parallel_admission build_admission(engine& eng, const std::shared_ptr<script_defined_function>& fn,
	                                   bool inplace_root = false) {
		admission_builder builder(eng);
		if (inplace_root) {
			builder.inplace_root = true;
			builder.adm.inplace_root = true;
			builder.prefix = "parallel_for";
		} else {
			builder.adm.transform_cursor = true;
		}
		if (builder.admit_function(fn, UINT64_MAX,
		                           inplace_root ? admission_builder::root_param::unary
		                                        : admission_builder::root_param::unary_value,
		                           nullptr)) {
			builder.adm.admitted = true;
		}
		return std::move(builder.adm);
	}

	bool admission_graph_current(engine& eng, const parallel_admission& adm) {
		for (const auto& [id, sfn] : adm.script_functions) {
			if (id == UINT64_MAX) { continue; }   // the root fn - caller-supplied, always current
			auto cur = eng.get_global_environment()->get(id);
			if (!cur || !cur.value().is_function()) { return false; }
			const script_callable* p = payload_from_function_value(cur.value());
			if (!p || p->kind != script_callable::kind_type::function || !p->fn || p->fn->body != sfn->body) {
				return false;
			}
		}
		return true;
	}

	const parallel_admission* admit_cached(engine& eng, parallel_engine_state& state,
	                                       const std::shared_ptr<script_defined_function>& fn,
	                                       bool inplace_root = false) {
		const void* key = fn->body.get();
		auto it = state.admission_cache.find(key);
		if (it != state.admission_cache.end() && admission_graph_current(eng, it->second)) {
			return &it->second;
		}
		auto [pos, inserted] = state.admission_cache.insert_or_assign(key, build_admission(eng, fn, inplace_root));
		(void)inserted;
		return &pos->second;
	}

	// ============================== WORKERS ==============================

	// Re-slice the worker's memory allowance from the CURRENT outer accounting
	void slice_worker_limits(engine& eng, parallel_worker_slot& slot) {
		const auto& outer = eng.execution_limits();
		slot.limits.interpreter_call_depth_limit = outer.interpreter_call_depth_limit;
		slot.limits.vm_call_depth_limit = outer.vm_call_depth_limit;
		slot.limits.terminal_error = false;
		slot.limits.memory_used = 0;
		slot.limits.memory_raises = 0;
		slot.limits.memory_cap = outer.memory_cap;
		slot.limits.memory_cap_symbol_id = outer.memory_cap_symbol_id;
		slot.limits.memory_limit = outer.memory_limit;
		if (outer.memory_limit != SIZE_MAX) {
			const size_t already = std::min(outer.memory_used, outer.memory_limit);
			slot.limits.memory_limit = outer.memory_limit - already;
		}
	}

	// True when a persisted slot was provisioned for exactly this call shape: same
	// backend type, same fn body, same transitive script bodies under the same names.
	// (Host copies are refreshed on reuse rather than fingerprinted; the admission graph
	// itself was already verified fresh against the live global environment.)
	bool slot_matches(const parallel_worker_slot& slot, const parallel_admission& adm,
	                  const script_defined_function& fn, bool use_vm) {
		if (slot.fingerprint_use_vm != use_vm) { return false; }
		if (slot.fingerprint_fn_body != static_cast<const void*>(fn.body.get())) { return false; }
		if (slot.fingerprint_script_bodies.size() != adm.script_functions.size()) { return false; }
		for (size_t i = 0; i < adm.script_functions.size(); ++i) {
			if (slot.fingerprint_script_bodies[i].first != adm.script_functions[i].first ||
			    slot.fingerprint_script_bodies[i].second != static_cast<const void*>(adm.script_functions[i].second->body.get())) {
				return false;
			}
		}
		return true;
	}

	// Reuse a persisted slot: re-slice limits, re-arm the budget clock, clear residual
	// execution state (a prior call may have ended mid-chunk on an error), refresh the
	// host-function copies (registration may have changed - reconstruction is cheaper
	// than detecting it). Provisioned script-function copies stay - that is the point.
	// Main thread, per region entry: prove (or re-validate) each admitted function's read-only
	// parameters on the live function and copy the verdict onto the worker's private copy
	void restamp_alias_proofs(engine& eng, parallel_worker_slot& slot) {
		for (auto& [source, copy] : slot.alias_stamps) {
			if (!source->parameters().empty()) { (void)readonly_parameter_alias(eng, *source, 0, /*may_prove*/ true); }
			copy->alias_proof_epoch = source->alias_proof_epoch;
			copy->alias_proof_mask = source->alias_proof_mask;
		}
	}

	void reset_worker_slot(engine& eng, parallel_worker_slot& slot, std::chrono::nanoseconds budget) {
		slice_worker_limits(eng, slot);
		if (slot.vm) {
			slot.vm->configure_parallel_worker(slot.root_env, slot.env_epoch_sink.get(), &slot.limits, budget);
		} else {
			slot.interp->get_interpreter()->configure_parallel_worker(slot.root_env, slot.env_epoch_sink.get(),
			                                                          &slot.limits, budget);
		}
		for (const auto& [name_id, name] : slot.host_functions) {
			slot.root_env->define(name_id, eng.make_parallel_host_function_copy(name));
		}
		slot.error.reset();
	}

	// The chunk loop a worker runs in-script: one host entry per region, zero calls per element
	// in inline mode. __c is the loop variable (slot 0 = the body's own parameter slot; the
	// by-ref form binds the chunk's cursor reference once and follows it, the by-value form
	// copies the cursor's element into __c per iteration), __n the chunk length, __k the
	// counter; __jai_parallel_advance re-points the cursor and yields __k + 1 (an opcode on the
	// vm, a native on the interpreter), so a `continue` in the body reaches it through the
	// for-update. The placeholder call is replaced by the body block; __n/__k are renumbered
	// past the body's frame slots.
	// Built per arity (the zip walks m arrays with m loop variables): params `var& __c0 .. __c{m-1},
	// int __n`; a by-value loop variable that keeps its copy gets `__cj = __jai_parallel_element(j);`
	// at the top of each iteration; inline mode grafts the body in place of the placeholder, call
	// mode calls the body function per element (a `return` inside a nested loop keeps its meaning).
	std::string chunk_skeleton_source(size_t arity, const std::vector<bool>& copy_per_iteration, bool call_mode) {
		std::string src = "function __jai_parallel_chunk(";
		for (size_t j = 0; j < arity; ++j) { src += "var& __c" + std::to_string(j) + ", "; }
		src += "int __n) { for (int __k = 0; __k < __n; __k = __jai_parallel_advance(__k)) { ";
		if (call_mode) {
			src += "__jai_parallel_body(";
			for (size_t j = 0; j < arity; ++j) { src += (j ? ", __c" : "__c") + std::to_string(j); }
			src += "); ";
		} else {
			for (size_t j = 0; j < arity; ++j) {
				if (copy_per_iteration[j]) { src += "__c" + std::to_string(j) + " = __jai_parallel_element(" + std::to_string(j) + "); "; }
			}
			src += "__jai_parallel_placeholder(); ";
		}
		src += "} }";
		return src;
	}

	// parallel_transform's chunk loop = the zip `o = fn(x)`: fn per element in-loop (its by-value
	// parameter binds the element in place when the admission proved it read-only, else copies as
	// it always did), the call result a temporary that moves into the output slot through the
	// second cursor - nothing else per element
	constexpr const char* k_parallel_chunk_transform =
		"function __jai_parallel_tchunk(var& __c0, var& __c1, int __n) { for (int __k = 0; __k < __n; __k = __jai_parallel_advance(__k)) { __c1 = __jai_parallel_fn(__c0); } }";

	std::shared_ptr<function_decl> parse_chunk_skeleton(engine& eng, const char* source) {
		const auto templates = eng.get_registered_template_types();
		lexer lex(source, eng.get_symbolizer(), templates);
		auto tokens = lex.tokenize();
		parser parse(tokens, eng.get_symbolizer(), &eng, templates, "<parallel_for>");
		auto decls = parse.parse();
		if (!decls || decls.value().empty() || decls.value().front()->get_type() != node_type::function_decl) {
			throw runtime_error("parallel_for: chunk wrapper failed to parse (internal)");
		}
		return std::static_pointer_cast<function_decl>(decls.value().front());
	}

	// Renumber the skeleton's own slots (__n, __k) past the body's frame and rename __cj to loop
	// parameter j (its permanent name and symbol; the vm resolves frame locals by declared name)
	struct chunk_names {
		std::vector<std::pair<std::string_view, uint64_t>> params;   // (name, symbol) per loop variable
		std::vector<size_t> param_slots;                             // empty: loop variable j sits in slot j
		size_t n_slot = 0, k_slot = 0;
		size_t slot_of(size_t j) const { return j < param_slots.size() ? param_slots[j] : j; }
	};
	void renumber_chunk_slot(expression_ptr& e, const chunk_names& names) {
		if (!e) { return; }
		switch (e->get_type()) {
		case node_type::identifier_expr: {
			auto* id = static_cast<identifier_expr*>(e.get());
			if (id->name == "__n") { id->slot_index = names.n_slot; }
			else if (id->name == "__k") { id->slot_index = names.k_slot; }
			else if (id->name.size() > 3 && id->name.substr(0, 3) == "__c") {
				const size_t j = static_cast<size_t>(std::stoul(std::string(id->name.substr(3))));
				if (j < names.params.size()) { id->name = names.params[j].first; id->symbol_id = names.params[j].second; id->slot_index = names.slot_of(j); }
			}
			return;
		}
		case node_type::binary_expr: {
			auto* b = static_cast<binary_expr*>(e.get());
			renumber_chunk_slot(b->left, names); renumber_chunk_slot(b->right, names);
			return;
		}
		case node_type::assignment_expr: {
			auto* a = static_cast<assignment_expr*>(e.get());
			renumber_chunk_slot(a->target, names); renumber_chunk_slot(a->value, names);
			return;
		}
		case node_type::call_expr: {
			auto* c = static_cast<call_expr*>(e.get());
			for (auto& arg : c->arguments) { renumber_chunk_slot(arg, names); }
			return;
		}
		default:
			return;
		}
	}

	// Builds (once per statement) the chunk wrapper: skeleton by mode, the loop parameter
	// itself as parameter 0 (the body's slot 0), __n/__k past the body's frame, the body block
	// grafted in place of the placeholder (inline modes).
	const std::shared_ptr<function_decl>& build_chunk_wrapper(engine& eng, parallel_for_stmt* stmt, const parallel_admission& adm) {
		if (stmt->chunk_decl_cache) { return stmt->chunk_decl_cache; }
		const bool inline_mode = stmt->chunk_mode == 1;
		std::vector<parameter> loop_params;
		loop_params.push_back(stmt->loop_param);
		for (const auto& extra : stmt->extra_params) { loop_params.push_back(extra); }
		const size_t arity = loop_params.size();
		// A by-value loop variable the body only reads binds its element in place (same reads,
		// no per-element copy); one it writes or uses whole keeps the copy
		const bool reads_only = !adm.element_reach_all && !adm.element_mutated;
		std::vector<bool> copy_per_iteration(arity, false);
		for (size_t j = 0; j < arity; ++j) { copy_per_iteration[j] = !loop_params[j].is_reference && !reads_only; }
		auto decl = parse_chunk_skeleton(eng, chunk_skeleton_source(arity, copy_per_iteration, !inline_mode).c_str());
		chunk_names names;
		names.n_slot = stmt->local_count; names.k_slot = stmt->local_count + 1;
		for (size_t j = 0; j < arity; ++j) {
			parameter lp = loop_params[j];
			lp.slot_index = j;
			if (lp.symbol_id == UINT64_MAX) { lp.symbol_id = eng.symbolize(lp.name); }
			if (inline_mode) { lp.is_reference = lp.is_reference || reads_only; }
			names.params.emplace_back(eng.get_symbolizer()->get_string(lp.symbol_id), lp.symbol_id);
			if (inline_mode) { decl->parameters[j] = lp; }
		}
		if (inline_mode) {
			decl->parameters[arity].slot_index = names.n_slot;
			auto* body_block = static_cast<block_stmt*>(decl->body.get());
			for (auto& d : body_block->declarations) {
				if (d->get_type() != node_type::statement_decl) { continue; }
				auto& st = static_cast<statement_decl*>(d.get())->statement;
				if (!st || st->get_type() != node_type::for_stmt) { continue; }
				auto* loop = static_cast<for_stmt*>(st.get());
				if (loop->initializer && loop->initializer->get_type() == node_type::variable_decl) {
					static_cast<variable_decl*>(loop->initializer.get())->slot_index = names.k_slot;
				}
				renumber_chunk_slot(loop->condition, names);
				renumber_chunk_slot(loop->update, names);
				auto* inner = static_cast<block_stmt*>(loop->body.get());
				for (auto& bd : inner->declarations) {
					if (bd->get_type() != node_type::expression_decl) { continue; }
					auto* ed = static_cast<expression_decl*>(bd.get());
					if (ed->expression && ed->expression->get_type() == node_type::assignment_expr) {
						renumber_chunk_slot(ed->expression, names);   // `__cj = __jai_parallel_element(j)` -> the loop variable
						continue;
					}
					if (ed->expression && ed->expression->get_type() == node_type::call_expr) {
						auto* call = static_cast<call_expr*>(ed->expression.get());
						if (call->callee->get_type() == node_type::identifier_expr &&
						    static_cast<identifier_expr*>(call->callee.get())->name == "__jai_parallel_placeholder") {
							bd = std::make_shared<statement_decl>(bd->location, stmt->body);
						}
					}
				}
			}
			decl->local_count = stmt->local_count + 2;
		}
		stmt->chunk_decl_cache = std::move(decl);
		return stmt->chunk_decl_cache;
	}

	// An exact deep copy of a statement subtree (the serializer knows every node): fn's body is
	// shared with its ordinary callers, so the graft below works on a copy
	statement_ptr clone_statement(engine& eng, const statement_ptr& stmt, const script_defined_function& fn) {
		std::vector<declaration_ptr> decls;
		decls.push_back(std::make_shared<statement_decl>(stmt->location, stmt));
		const std::vector<uint8_t> bytes = serialize_jaibite(decls, *eng.get_symbolizer(), 0);
		uint64_t fingerprint = 0;
		const ast_frame_context context{fn.local_count, fn.parameters()};
		auto back = deserialize_jaibite(bytes.data(), bytes.size(), &eng, fingerprint, nullptr, &context);
		if (back.size() != 1 || back[0]->get_type() != node_type::statement_decl) {
			throw runtime_error("parallel_transform: body clone failed (internal)");
		}
		return static_cast<statement_decl*>(back[0].get())->statement;
	}

	// Top-level `return e;` of the grafted fn body becomes `out = e; continue;` (`return;` just
	// continues: the output slot keeps its null) - the continue reaches the loop's advance. fn's
	// declared return type converts through cast<T>, the conversion the typed return applied.
	struct transform_return_ctx {
		engine* eng = nullptr;
		std::string_view out_name;
		uint64_t out_symbol = UINT64_MAX;
		size_t out_slot = 0;
		type_info_ptr cast_to;   // null: no conversion (auto / var / reference return)
		// The checker's return proofs read off the ORIGINAL body in traversal order (the clone
		// carries no runtime stamps; both trees have the same shape): a proven same-type
		// return needs no cast, exactly as the typed return skipped its conversion
		std::vector<uint8_t> return_proofs;
		size_t next_return = 0;
	};
	void collect_return_proofs(const statement_ptr& slot, std::vector<uint8_t>& out) {
		if (!slot) { return; }
		switch (slot->get_type()) {
		case node_type::return_stmt:
			out.push_back(static_cast<const return_stmt*>(slot.get())->static_return_proof);
			return;
		case node_type::block_stmt:
			for (auto& d : static_cast<block_stmt*>(slot.get())->declarations) {
				if (d && d->get_type() == node_type::statement_decl) { collect_return_proofs(static_cast<statement_decl*>(d.get())->statement, out); }
			}
			return;
		case node_type::if_stmt: {
			auto* n = static_cast<if_stmt*>(slot.get());
			collect_return_proofs(n->then_statement, out); collect_return_proofs(n->else_statement, out);
			return;
		}
		case node_type::while_stmt: collect_return_proofs(static_cast<while_stmt*>(slot.get())->body, out); return;
		case node_type::for_stmt: collect_return_proofs(static_cast<for_stmt*>(slot.get())->body, out); return;
		case node_type::range_for_stmt: collect_return_proofs(static_cast<range_for_stmt*>(slot.get())->body, out); return;
		case node_type::try_stmt: {
			auto* n = static_cast<try_stmt*>(slot.get());
			collect_return_proofs(n->try_block, out); collect_return_proofs(n->catch_block, out);
			return;
		}
		case node_type::switch_stmt: {
			auto* n = static_cast<switch_stmt*>(slot.get());
			for (auto& c : n->cases) { for (auto& st : c->body) { collect_return_proofs(st, out); } }
			if (n->default_case) { for (auto& st : n->default_case->body) { collect_return_proofs(st, out); } }
			return;
		}
		default:
			return;
		}
	}
	void rewrite_transform_returns(statement_ptr& slot, size_t loop_depth, transform_return_ctx& ctx) {
		if (!slot) { return; }
		switch (slot->get_type()) {
		case node_type::return_stmt: {
			if (loop_depth != 0) { ++ctx.next_return; return; }   // never reached: nested returns select call mode
			auto* r = static_cast<return_stmt*>(slot.get());
			const uint8_t proof = ctx.next_return < ctx.return_proofs.size() ? ctx.return_proofs[ctx.next_return] : proof_none;
			++ctx.next_return;
			std::vector<declaration_ptr> decls;
			if (r->value) {
				auto target = std::make_shared<identifier_expr>(r->location, ctx.out_name, ctx.out_symbol);
				target->slot_index = ctx.out_slot;
				expression_ptr value = r->value;
				if (ctx.cast_to && proof != proof_same_type) { value = std::make_shared<cast_expr>(r->location, ctx.cast_to, false, value); }
				token eq(token_type::equal, std::string_view("="), r->location);
				decls.push_back(std::make_shared<expression_decl>(r->location, std::make_shared<assignment_expr>(r->location, target, eq, value)));
			}
			decls.push_back(std::make_shared<statement_decl>(r->location, std::make_shared<continue_stmt>(r->location)));
			slot = std::make_shared<block_stmt>(r->location, std::move(decls));
			return;
		}
		case node_type::block_stmt:
			for (auto& d : static_cast<block_stmt*>(slot.get())->declarations) {
				if (d && d->get_type() == node_type::statement_decl) {
					rewrite_transform_returns(static_cast<statement_decl*>(d.get())->statement, loop_depth, ctx);
				}
			}
			return;
		case node_type::if_stmt: {
			auto* n = static_cast<if_stmt*>(slot.get());
			rewrite_transform_returns(n->then_statement, loop_depth, ctx);
			rewrite_transform_returns(n->else_statement, loop_depth, ctx);
			return;
		}
		case node_type::while_stmt:
			rewrite_transform_returns(static_cast<while_stmt*>(slot.get())->body, loop_depth + 1, ctx); return;
		case node_type::for_stmt:
			rewrite_transform_returns(static_cast<for_stmt*>(slot.get())->body, loop_depth + 1, ctx); return;
		case node_type::range_for_stmt:
			rewrite_transform_returns(static_cast<range_for_stmt*>(slot.get())->body, loop_depth + 1, ctx); return;
		case node_type::try_stmt: {
			auto* n = static_cast<try_stmt*>(slot.get());
			rewrite_transform_returns(n->try_block, loop_depth, ctx);
			rewrite_transform_returns(n->catch_block, loop_depth, ctx);
			return;
		}
		case node_type::switch_stmt: {
			auto* n = static_cast<switch_stmt*>(slot.get());
			for (auto& c : n->cases) { for (auto& st : c->body) { rewrite_transform_returns(st, loop_depth, ctx); } }
			if (n->default_case) { for (auto& st : n->default_case->body) { rewrite_transform_returns(st, loop_depth, ctx); } }
			return;
		}
		default:
			return;
		}
	}

	// The transform's chunk wrapper for fn: fn's body grafted in - its parameter bound to the input
	// cursor in place, its returns stored through the output cursor - when fn only reads its
	// parameter (no store through it, no mutating method, no whole use: the copy it would have
	// made is unobservable) and returns only from its top level; otherwise the call form, which
	// copies for a mutating fn exactly as before. Built once per fn body.
	constexpr const char* k_parallel_chunk_transform_inline =
		"function __jai_parallel_tchunk(var& __c0, var& __c1, int __n) { for (int __k = 0; __k < __n; __k = __jai_parallel_advance(__k)) { __jai_parallel_placeholder(); } }";

	const std::shared_ptr<function_decl>& build_transform_wrapper(engine& eng, parallel_engine_state& state,
	                                                              const std::shared_ptr<script_defined_function>& fn,
	                                                              const parallel_admission& adm) {
		auto found = state.transform_inline_wrappers.find(static_cast<const void*>(fn->body.get()));
		if (found != state.transform_inline_wrappers.end()) { return found->second; }
		body_control_scan scan;
		statement_ptr body_view = fn->body;
		scan_parallel_body_control(body_view, 0, 0, scan);
		const bool reads_only = !adm.element_reach_all && !adm.element_mutated;
		const bool inline_ok = reads_only && !scan.nested_return && !scan.bad_break && fn->parameters().size() == 1;
		if (!inline_ok) {
			if (!state.transform_chunk_wrapper) { state.transform_chunk_wrapper = parse_chunk_skeleton(eng, k_parallel_chunk_transform); }
			return state.transform_inline_wrappers.emplace(static_cast<const void*>(fn->body.get()), state.transform_chunk_wrapper).first->second;
		}
		auto decl = parse_chunk_skeleton(eng, k_parallel_chunk_transform_inline);
		const size_t n_locals = fn->local_count;   // fn's frame: parameter 0 + its locals
		parameter in_param = fn->parameters()[0];
		in_param.slot_index = 0;
		in_param.is_reference = true;   // in place: the proof above makes the copy unobservable
		if (in_param.symbol_id == UINT64_MAX) { in_param.symbol_id = eng.symbolize(in_param.name); }
		chunk_names names;
		names.params.emplace_back(eng.get_symbolizer()->get_string(in_param.symbol_id), in_param.symbol_id);
		const uint64_t out_symbol = eng.symbolize("__c1");
		names.params.emplace_back(eng.get_symbolizer()->get_string(out_symbol), out_symbol);
		names.param_slots = { 0, n_locals };
		names.n_slot = n_locals + 1; names.k_slot = n_locals + 2;
		decl->parameters[0] = in_param;
		decl->parameters[1].slot_index = n_locals;
		decl->parameters[2].slot_index = names.n_slot;
		transform_return_ctx rctx;
		rctx.eng = &eng;
		rctx.out_name = names.params[1].first;
		rctx.out_symbol = out_symbol;
		rctx.out_slot = n_locals;
		const return_conv conv = classify_return_conv(fn->return_type.get());
		if (conv != return_conv::none && conv != return_conv::ref) { rctx.cast_to = fn->return_type; }
		collect_return_proofs(body_view, rctx.return_proofs);
		statement_ptr grafted = clone_statement(eng, body_view, *fn);
		rewrite_transform_returns(grafted, 0, rctx);
		auto* body_block = static_cast<block_stmt*>(decl->body.get());
		for (auto& d : body_block->declarations) {
			if (d->get_type() != node_type::statement_decl) { continue; }
			auto& st = static_cast<statement_decl*>(d.get())->statement;
			if (!st || st->get_type() != node_type::for_stmt) { continue; }
			auto* loop = static_cast<for_stmt*>(st.get());
			if (loop->initializer && loop->initializer->get_type() == node_type::variable_decl) {
				static_cast<variable_decl*>(loop->initializer.get())->slot_index = names.k_slot;
			}
			renumber_chunk_slot(loop->condition, names);
			renumber_chunk_slot(loop->update, names);
			auto* inner = static_cast<block_stmt*>(loop->body.get());
			for (auto& bd : inner->declarations) {
				if (bd->get_type() != node_type::expression_decl) { continue; }
				auto* ed = static_cast<expression_decl*>(bd.get());
				if (ed->expression && ed->expression->get_type() == node_type::call_expr) {
					auto* call = static_cast<call_expr*>(ed->expression.get());
					if (call->callee->get_type() == node_type::identifier_expr &&
					    static_cast<identifier_expr*>(call->callee.get())->name == "__jai_parallel_placeholder") {
						bd = std::make_shared<statement_decl>(bd->location, grafted);
					}
				}
			}
		}
		decl->local_count = n_locals + 3;
		return state.transform_inline_wrappers.emplace(static_cast<const void*>(fn->body.get()), std::move(decl)).first->second;
	}

	std::unique_ptr<parallel_worker_slot> provision_worker(engine& eng, const parallel_admission& adm,
	                                                 std::chrono::nanoseconds budget,
	                                                 bool use_vm,
	                                                 const std::shared_ptr<function_decl>* chunk_wrapper = nullptr) {
		auto ctx = std::make_unique<parallel_worker_slot>();
		ctx->env_epoch_sink = std::make_unique<string_symbolizer>();
		ctx->root_env = std::make_shared<environment>(ctx->env_epoch_sink.get());

		slice_worker_limits(eng, *ctx);

		if (use_vm) {
			auto backend = std::make_unique<vm::vm_backend>(eng.get_symbolizer(), ctx->root_env);
			backend->set_engine_reference(&eng);
			backend->configure_parallel_worker(ctx->root_env, ctx->env_epoch_sink.get(), &ctx->limits, budget);
			ctx->vm = backend.get();
			ctx->backend = std::move(backend);
		} else {
			auto backend = std::make_unique<interpreter_backend>(eng.get_symbolizer(), ctx->root_env);
			backend->set_engine_reference(&eng);
			backend->get_interpreter()->configure_parallel_worker(ctx->root_env, ctx->env_epoch_sink.get(),
			                                                      &ctx->limits, budget);
			ctx->interp = backend.get();
			ctx->backend = std::move(backend);
		}

		// Provisioning fingerprint for reuse decisions on later calls
		ctx->fingerprint_use_vm = use_vm;
		ctx->fingerprint_script_bodies.reserve(adm.script_functions.size());
		for (const auto& [name_id, source_fn] : adm.script_functions) {
			ctx->fingerprint_script_bodies.emplace_back(name_id, static_cast<const void*>(source_fn->body.get()));
		}
		if (!adm.script_functions.empty()) {
			ctx->fingerprint_fn_body = static_cast<const void*>(adm.script_functions.front().second->body.get());
		}
		ctx->host_functions = adm.host_functions;

		// Per-worker copies of every callable the body needs: fresh script_defined_function
		// objects (own parameters vector, own backend_body_cache, closure_env = the worker
		// root) minted as script_callable_thunks - the backends' call paths RECOVER the
		// payload (target<script_callable_thunk>) and dispatch in-loop on the CALLING
		// worker's own backend, exactly like any script function. (The old anonymous
		// lambda was opaque: every worker call crossed the from-scratch native boundary,
		// ~8.3k cycles per element vs an in-loop frame push - 62% of worker time in the
		// particles region.) The thunk's operator() fallback would route to the ENGINE's
		// backend - unreachable from admitted bodies: only direct calls to statically-
		// known functions are admitted, and function VALUES cannot be captured or stored.
		bool first_entry = true;
		for (const auto& [name_id, source_fn] : adm.script_functions) {
			// Deep parameter copy ON PURPOSE: parameter's mutable symbol_id/slot_index
			// lazy fills stay worker-private (the shared-storage mints are main-thread)
			std::vector<parameter> params(source_fn->parameters());
			if (first_entry && adm.transform_cursor && !params.empty() && !params[0].is_reference &&
			    !adm.element_reach_all && !adm.element_mutated) {
				params[0].is_reference = true;   // read-only by-value element: bind the chunk's element in place, no copy
			}
			auto copy = std::make_shared<script_defined_function>(
				source_fn->name, std::move(params), source_fn->return_type,
				source_fn->body, ctx->root_env, source_fn->local_count);
			copy->alias_candidate = source_fn->alias_candidate;
			if (copy->alias_candidate) { ctx->alias_stamps.emplace_back(source_fn, copy); }
			if (ctx->vm) { ctx->vm->precompile_parallel_function(*copy); }
			script_callable payload;
			payload.kind = script_callable::kind_type::function;
			payload.fn = copy;
			if (first_entry) {
				ctx->fn_payload = payload;   // the root fn is always pushed first
				first_entry = false;
			}
			if (name_id != UINT64_MAX) {
				ctx->root_env->define(name_id,
					script_value::make_function(script_callable_thunk{ &eng, payload }, &eng));
			}
		}
		for (const auto& [name_id, name] : adm.host_functions) {
			ctx->root_env->define(name_id, eng.make_parallel_host_function_copy(name));
		}
		if (chunk_wrapper && *chunk_wrapper) {
			const std::shared_ptr<function_decl>& wrapper = *chunk_wrapper;
			ctx->root_env->define(eng.symbolize(adm.inplace_root ? "__jai_parallel_body" : "__jai_parallel_fn"),
				script_value::make_function(script_callable_thunk{ &eng, ctx->fn_payload }, &eng));
			parallel_worker_slot* slot = ctx.get();   // slots live in the pool's unique_ptrs: stable
			// The interpreter calls these natives; the vm compiles the names to opcodes
			ctx->root_env->define(eng.symbolize("__jai_parallel_advance"),
				script_value::make_function([slot, eng_ptr = &eng](const std::vector<script_value>& args) -> checked_result<script_value> {
					for (auto& cursor : slot->cursors) { cursor.element_cursor_advance(); }
					const script_int k = args.empty() ? 0 : args[0].deref().as_int();
					return script_value(k + 1, eng_ptr);
				}, &eng));
			ctx->root_env->define(eng.symbolize("__jai_parallel_element"),
				script_value::make_function([slot, eng_ptr = &eng](const std::vector<script_value>& args) -> checked_result<script_value> {
					const size_t which = args.empty() ? 0 : static_cast<size_t>(args[0].deref().as_int());
					if (which >= slot->cursors.size()) { return script_value(std::monostate{}, eng_ptr); }
					return slot->cursors[which];
				}, &eng));
			auto chunk_fn = std::make_shared<script_defined_function>(
				wrapper->name, std::vector<parameter>(wrapper->parameters), wrapper->return_type,
				wrapper->body, ctx->root_env, wrapper->local_count);
			if (ctx->vm) { ctx->vm->precompile_parallel_function(*chunk_fn); }
			ctx->chunk_payload.kind = script_callable::kind_type::function;
			ctx->chunk_payload.fn = chunk_fn;
		}
		return ctx;
	}

	// In-place worker loop (parallel_for): by-ref bodies bind each iteration to the
	// pre-minted element reference - the body mutates the caller's element directly
	// under chunk exclusivity. Plain-auto bodies read the element through raw_input
	// (the by-value binding clones per value semantics - zero side effects). No
	// output, results discarded. Every refcount touched is element-internal
	// (normalization made elements exclusive) or worker-private: single-threaded.
	void run_worker_chunk(parallel_worker_slot& ctx, engine& eng) {
		if (ctx.end <= ctx.begin || ctx.element_refs.empty()) { return; }
		// ONE script entry per chunk: the wrapper loops in-script, calling the body per element
		// with the chunk's cursor reference and advancing it through the worker's intrinsic. The
		// failing element, when any, is the cursor's index (the advance runs after a body call).
		ctx.cursors = std::move(ctx.element_refs);   // [0] = the range, then the zip's arrays
		ctx.element_refs.clear();
		if (ctx.vm) { ctx.vm->set_parallel_cursors(&ctx.cursors); }
		struct cursor_reset {
			parallel_worker_slot& c;
			~cursor_reset() {
				if (c.vm) {
					c.vm->set_parallel_cursors(nullptr);
					c.vm->release_region_strings();   // published nodes belong to the main thread from here
				}
			}
		} cursor_guard{ ctx };
		auto failing = [&ctx]() { const size_t i = ctx.cursors.front().element_cursor_index(); return i == SIZE_MAX ? ctx.begin : std::min(i, ctx.end - 1); };
		try {
			auto r = ctx.backend->execute_callable(ctx.chunk_payload, ctx.call_args);
			if (ctx.backend->is_unwinding()) {
				ctx.error = parallel_worker_error{ failing(),
					make_error_code(runtime_error_code::evaluation_failed),
					ctx.backend->get_current_exception().what(),
					ctx.limits.terminal_error };
			} else if (!r) {
				ctx.error = parallel_worker_error{ failing(), r.error(),
					format_error(r, *eng.get_symbolizer()), ctx.limits.terminal_error };
			}
		} catch (const std::exception& e) {
			ctx.error = parallel_worker_error{ failing(), make_error_code(runtime_error_code::cpp_exception),
				e.what(), ctx.limits.terminal_error };
		}
	}

	// ============================== BARRIER HELPERS ==============================

	// Interns every shape a worker can mint from the seed before the tables freeze: each
	// type's T& twin and container components, and - when a type names a registered script
	// class - the class's field ladder (declared types, accessor ids) plus its registered
	// subclasses (a nested typed array's slot tag is whatever subclass sits there), so a class
	// reached only through a typed container warms exactly like the element's own. Memoized
	// per fork; deny_type_intern_in_region is the backstop for a shape this misses.
	void prewarm_region_types(engine& eng, const std::vector<type_info*>& seed) {
		auto& sym = *eng.get_symbolizer();
		(void)sym.intern("pair");   // map range-for pair name (hit for stdlib engines)
		auto& registry = eng.get_class_registry();
		std::vector<type_info*> pending(seed);
		pending.push_back(eng.get_type_info_int());
		pending.push_back(eng.get_type_info_float());
		pending.push_back(eng.get_type_info_string());
		pending.push_back(eng.get_type_info_bool());
		pending.push_back(eng.get_type_info_char());
		pending.push_back(eng.get_type_info_void());
		pending.push_back(eng.get_type_info_array(nullptr));
		pending.push_back(eng.get_type_info_map(nullptr, nullptr));
		eng.get_type_info_reference(nullptr);
		std::unordered_set<type_info*> seen;
		std::vector<const class_definition*> classes;
		std::vector<const class_definition*> pending_classes;
		while (!pending.empty() || !pending_classes.empty()) {
			if (!pending_classes.empty()) {
				const class_definition* cd = pending_classes.back();
				pending_classes.pop_back();
				if (!cd || std::find(classes.begin(), classes.end(), cd) != classes.end()) { continue; }
				classes.push_back(cd);
				pending.push_back(cd->get_type_info().get());
				const uint32_t n = cd->field_slot_count();
				for (uint32_t slot = 0; slot < n; ++slot) {
					const uint64_t field_id = cd->slot_field(slot);
					(void)sym.get_getter_id_with_view(field_id);
					(void)sym.get_setter_id_with_view(field_id);
					pending.push_back(cd->get_field_declared_type(field_id).get());
				}
				for (const auto& [name, def] : registry.script_classes_) {
					(void)name;
					if (def && def.get() != cd && def->is_subtype_of(cd->get_name())) { pending_classes.push_back(def.get()); }
				}
				continue;
			}
			type_info* t = pending.back();
			pending.pop_back();
			if (!t || !seen.insert(t).second) { continue; }
			eng.get_type_info_reference(t);
			pending.push_back(t->element_type().get());
			pending.push_back(t->key_type().get());
			pending.push_back(t->value_type().get());
			if (t->base_type == script_value_type::jai_object_type && !t->type_name.empty()) {
				pending_classes.push_back(registry.find_script_class(std::string(t->type_name)).get());
			}
		}
	}

	// The classes a body can reach when the barrier holds no slot tag (a trusted or untyped
	// source): every registered script class declaring a member the body names on an
	// element-rooted chain seeds the prewarm (over-approximate, warming is free).
	void seed_classes_by_member(engine& eng, const std::vector<uint64_t>& members, std::vector<type_info*>& seed) {
		if (members.empty()) { return; }
		for (const auto& [name, def] : eng.get_class_registry().script_classes_) {
			(void)name;
			if (!def) { continue; }
			for (uint64_t id : members) {
				if (def->chain_declares_field(id)) { seed.push_back(def->get_type_info().get()); break; }
			}
		}
	}

	// STATIC all-primitive proof (Dev ruling 2026-07-09 "safety AND free"): a TYPED
	// container's element types are store-enforced invariants, so array<int> etc. is
	// all-primitive by construction - no O(n) content scan needed, ever. Untyped
	// containers fall back to the dynamic scan below.
	bool statically_all_primitive(const script_value& v) {
		const type_info_ptr t = v.get_type_info();
		if (!t) {
			return false;
		}
		auto primitive_type = [](const type_info_ptr& p) {
			if (!p) return false;
			switch (p->base_type) {
				case script_value_type::jai_int_type:
				case script_value_type::jai_float_type:
				case script_value_type::jai_bool_type:
				case script_value_type::jai_char_type:
				case script_value_type::jai_string_type:   // a borrowed string read mints the bytes, never the handle
					return true;
				default:
					return false;
			}
		};
		if (t->base_type == script_value_type::jai_array_type) {
			return primitive_type(t->element_type());
		}
		if (t->base_type == script_value_type::jai_map_type) {
			return primitive_type(t->key_type()) && primitive_type(t->value_type());
		}
		return false;
	}

	// In-place element PROOF + NORMALIZATION (parallel_for; Dev rulings 2026-07-09):
	// after this pass returns true, mutating the element under chunk exclusivity can
	// never touch a refcount another thread can see. Value semantics carry most of the
	// argument (script stores deep-copy, so elements cannot share structure); the one
	// value shape whose copies DO share storage - strings - is handled by DETACHING
	// here instead of banning: a string node with use_count > 1 (assigned from a
	// shared source, or aliased cross-element) is replaced in place with a fresh copy,
	// single-threaded at the barrier, semantically invisible. Steady-state this walk
	// detaches nothing (worker-minted strings from the previous region are already
	// exclusive). Containers/objects whose HANDLE is shared (engine-internal aliasing;
	// value semantics makes it rare) detach wholesale the same way. What still fails:
	// shared_ptr/weak_ptr (aliasing IS their semantics), functions, bound host values,
	// references, and instances of non-flat classes.
	// reach: what the body can reach below the element - a field no worker can name is never
	// copied by one, so its shared handles need no detach.
#ifdef JAISCRIPT_VM_PROFILE
	inline uint64_t prof_norm_nodes_[16] = {};
	inline uint64_t prof_norm_detach_ = 0;
	inline uint64_t prof_obj_sec_[4] = {};
#endif
	// The member ids the body names on element-rooted chains (null = every field) and the
	// chain terminals it uses whole (copied or replaced - every handle in that subtree is
	// touched, so the filter opens below them); nested objects are walked with the same filter.
	struct reach_filter {
		const std::vector<uint64_t>* members = nullptr;
		const std::vector<uint64_t>* whole = nullptr;
		bool names(uint64_t id) const {
			return !members || std::find(members->begin(), members->end(), id) != members->end();
		}
		bool whole_read(uint64_t id) const {
			return !members || (whole && std::find(whole->begin(), whole->end(), id) != whole->end());
		}
		reach_filter below(uint64_t id) const { return whole_read(id) ? reach_filter{} : *this; }
	};

	// Static per-class verdict for the reference-free fast path: can a body whose element
	// reach is `reach` touch a shared string buffer through an instance of `cd`? A named
	// string-reachable slot that is itself a string (or container, or read whole) says yes;
	// a named object field asks its own class; nothing named says no. Memoized per (class,
	// admission) by the caller - the walk then skips every instance whose class answers no.
	bool class_reach_touches_string(const class_definition* cd, const reach_filter& reach, engine& eng, int depth = 0) {
		if (!cd || depth > 8) { return true; }
		for (const uint32_t slot : cd->string_reachable_slots()) {
			const uint64_t id = cd->slot_field(slot);
			if (!reach.names(id)) { continue; }
			if (reach.whole_read(id)) { return true; }
			const type_info_ptr t = cd->get_field_declared_type(id);
			if (!t || t->base_type != script_value_type::jai_object_type || t->type_name.empty()) { return true; }
			auto nested = eng.get_class_registry().find_script_class(std::string(t->type_name));
			if (!nested || class_reach_touches_string(nested.get(), reach, eng, depth + 1)) { return true; }
		}
		return false;
	}

	// Declared-type exclusivity (Dev ruling: a typed array's element type is a proven fact
	// about every element): true when every instance a slot of class `cd` can hold - `cd`
	// and, when asked, its registered subclasses - is value-closed and no field the body
	// reaches can touch a shared string; a reached object field asks its own class the same
	// question (subclasses included: a field declared `Inner` holds any Inner). Holds a
	// whole fork without one element read when the array carries no live reference.
	bool declared_class_exclusive(const class_definition* cd, const reach_filter& reach, engine& eng,
	                              bool with_subclasses, std::vector<const class_definition*>& visited, int depth = 0) {
		if (!cd || depth > 8) { return false; }
		if (std::find(visited.begin(), visited.end(), cd) != visited.end()) { return true; }
		visited.push_back(cd);
		auto exclusive = [&](const class_definition* k) {
			if (!k->flat_value_semantics()) { return false; }
			const auto& string_slots = k->string_reachable_slots();
			for (const uint32_t slot : k->handle_bearing_slots()) {
				const uint64_t id = k->slot_field(slot);
				if (!reach.names(id)) { continue; }
				const bool holds_string = std::find(string_slots.begin(), string_slots.end(), slot) != string_slots.end();
				if (holds_string && reach.whole_read(id)) { return false; }
				const type_info_ptr t = k->get_field_declared_type(id);
				if (t && t->base_type == script_value_type::jai_object_type && !t->type_name.empty()) {
					auto nested = eng.get_class_registry().find_script_class(std::string(t->type_name));
					if (!nested || !declared_class_exclusive(nested.get(), reach.below(id), eng, true, visited, depth + 1)) { return false; }
					continue;
				}
				if (holds_string) { return false; }   // a string, or a container that can hold one: the walk detaches
			}
			return true;
		};
		if (!exclusive(cd)) { return false; }
		if (with_subclasses) {
			for (const auto& [name, def] : eng.get_class_registry().script_classes_) {
				(void)name;
				if (def && def.get() != cd && def->is_subtype_of(cd->get_name()) && !exclusive(def.get())) { return false; }
			}
		}
		return true;
	}

	// types: every visited node's shape (the transform's prewarm seed - its workers clone whole elements)
	bool normalize_element_inplace(script_value& v, engine& eng, reach_filter reach = {}, bool unreferenced_array = false, const class_definition* string_free_class = nullptr, std::vector<type_info*>* types = nullptr) {
		if (types && v.get_type_info().get()) { types->push_back(v.get_type_info().get()); }
#ifdef JAISCRIPT_VM_PROFILE
		++prof_norm_nodes_[v.raw_storage_index() < 16 ? v.raw_storage_index() : 15];
#endif
		switch (v.raw_storage_index()) {
		case script_value::TYPEID_NULL:
		case script_value::TYPEID_INT:
		case script_value::TYPEID_FLOAT:
		case script_value::TYPEID_CHAR:
		case script_value::TYPEID_BOOL:
			return true;
		case script_value::TYPEID_STRING: {
			auto& handle = v.unchecked_get_string_storage();
			if (handle && handle.use_count() > 1) {
				eng.execution_limits().memory_charge_deferred(handle->size() + sizeof(script_string));
				handle = make_strong<script_string>(*handle);
#ifdef JAISCRIPT_VM_PROFILE
				++prof_norm_detach_;
#endif
			}
			return true;
		}
		case script_value::TYPEID_ARRAY: {
			if (statically_all_primitive(v)) { return true; }
			auto& handle = v.unchecked_get_array_storage();
			if (!handle) { return true; }
			if (handle.use_count() > 1) {
				v = v.parallel_detached_copy();
				return normalize_element_inplace(v, eng, reach, unreferenced_array, nullptr, types);
			}
			if (handle->is_typed()) { return true; }   // raw buffer: all-primitive by construction
			for (auto& elem : handle->values()) {
				if (!normalize_element_inplace(elem, eng, reach, unreferenced_array, nullptr, types)) { return false; }
			}
			return true;
		}
		case script_value::TYPEID_MAP: {
			if (statically_all_primitive(v)) { return true; }
			auto& handle = v.unchecked_get_map_storage();
			if (!handle) { return true; }
			if (handle.use_count() > 1) {
				v = v.parallel_detached_copy();
				return normalize_element_inplace(v, eng, reach, unreferenced_array, nullptr, types);
			}
			// A shared KEY string (a literal every map built from the same source holds)
			// detaches in place through the node handle: same bytes, so the ordering the
			// comparator sees is unchanged and the map's values stay where they are. A
			// structured key detaches the map wholesale, then the values re-walk.
			bool structured_key = false;
			for (auto it = handle->begin(); it != handle->end();) {
				const size_t ki = it->first.raw_storage_index();
				if (ki == script_value::TYPEID_ARRAY || ki == script_value::TYPEID_MAP || ki == script_value::TYPEID_OBJECT) {
					structured_key = true;
					break;
				}
				if (ki == script_value::TYPEID_STRING &&
				    const_cast<script_value&>(it->first).unchecked_get_string_storage().use_count() > 1) {
					auto next = std::next(it);
					auto entry = handle->extract(it);
					auto& key_handle = entry.key().unchecked_get_string_storage();
					eng.execution_limits().memory_charge_deferred(key_handle->size() + sizeof(script_string));
					key_handle = make_strong<script_string>(*key_handle);
					handle->insert(next, std::move(entry));
					it = next;
					continue;
				}
				++it;
			}
			if (structured_key) {
				v = v.parallel_detached_copy();
				return normalize_element_inplace(v, eng, reach, unreferenced_array, nullptr, types);
			}
			for (auto& [key, val] : *handle) {
				(void)key;
				if (!normalize_element_inplace(val, eng, reach, unreferenced_array, nullptr, types)) { return false; }
			}
			return true;
		}
		case script_value::TYPEID_OBJECT: {
#ifdef JAISCRIPT_VM_PROFILE
			uint64_t po0 = __rdtsc();
#define JAI_OBJ_SEC(i) { const uint64_t po1 = __rdtsc(); prof_obj_sec_[i] += po1 - po0; po0 = po1; }
#else
#define JAI_OBJ_SEC(i)
#endif
			auto& handle = v.unchecked_get_object_storage();
			if (!handle || !handle->is_class_instance_wrapper || !handle->data) {
				return false;   // bound host object wrapper
			}
			auto* inst = static_cast<class_instance*>(handle->data.get());
			const class_definition* cd = inst->get_class_definition();
			if (!cd || !cd->flat_value_semantics()) {
				return false;
			}
			JAI_OBJ_SEC(0);
			// A shared_ptr-tagged instance aliases by script-visible design: a live alias (the count
			// says so) defeats the value-semantics premise below and cannot be cloned apart without
			// diverging from the serial loop
			if (v.is_shared_ptr_type() && (handle.use_count() > 1 || handle->data.use_count() > 1)) { return false; }
			// Reference-free fast path: no live field reference pins this instance, and the caller
			// proved no live element reference pins the array (unreferenced_array). Value semantics
			// then make the instance and its object/container subtree exclusive by construction;
			// the only alias left is a SHARED STRING BUFFER (a literal several instances hold), so
			// the walk narrows to the string slots the body can reach.
			if (unreferenced_array && inst->live_field_refs() == 0 && !inst->has_user_overflow_fields()) {
				if (cd == string_free_class) { return true; }   // proved once per fork: no reachable shared string
				for (const uint32_t slot : cd->string_reachable_slots()) {
					const uint64_t id = cd->slot_field(slot);
					if (!reach.names(id)) { continue; }
					if (script_value* field = inst->field_slot_value(slot)) {
						if (!normalize_element_inplace(*field, eng, reach.below(id), unreferenced_array, nullptr, types)) { return false; }
					}
				}
				return true;
			}
			if (handle.use_count() > 1 || handle->data.use_count() > 1) {
				v = v.clone();   // engine-internal aliasing: fresh exclusive instance
				auto& fresh = v.unchecked_get_object_storage();
				inst = static_cast<class_instance*>(fresh->data.get());
#ifdef JAISCRIPT_VM_PROFILE
				++prof_obj_sec_[3];
#endif
			}
			JAI_OBJ_SEC(1);
			// Only slots whose declared type can reach a refcount need the walk (strings detach,
			// containers and nested objects alias-check); scalar slots are exclusive by type
			for (const uint32_t slot : cd->handle_bearing_slots()) {
				const uint64_t id = cd->slot_field(slot);
				if (!reach.names(id)) { continue; }
				if (script_value* field = inst->field_slot_value(slot)) {
					if (!normalize_element_inplace(*field, eng, reach.below(id), unreferenced_array, nullptr, types)) { return false; }
				}
			}
			JAI_OBJ_SEC(2);
			if (inst->has_user_overflow_fields()) {   // runtime-added fields carry no declared type: walk them
				bool ok = true;
				inst->for_each_field([&](uint64_t id, script_value& field) {
					if (ok && cd->field_slot(id) == class_definition::k_no_field_slot && !normalize_element_inplace(field, eng, reach, unreferenced_array, nullptr, types)) { ok = false; }
				});
				if (!ok) { return false; }
			}
			return true;
		}
		default:
			return false;
		}
	}

	// The word the transform's rejection names for the first non-value-semantic leaf below
	// v (the text parallel_detached_copy raised before objects were admitted); null when
	// every leaf is value-semantic
	const char* non_value_semantic_what(const script_value& v) {
		switch (v.raw_storage_index()) {
		case script_value::TYPEID_NULL:
		case script_value::TYPEID_INT:
		case script_value::TYPEID_FLOAT:
		case script_value::TYPEID_CHAR:
		case script_value::TYPEID_BOOL:
		case script_value::TYPEID_STRING:
			return nullptr;
		case script_value::TYPEID_ARRAY: {
			const script_array* node = v.unchecked_array_node();
			if (!node) { return nullptr; }
			if (node->kind() == script_array::kind_t::obj) {
				for (size_t i = 0; i < node->size(); ++i) {
					if (const char* w = non_value_semantic_what(node->get(i, v.get_engine()))) { return w; }
				}
				return nullptr;
			}
			if (node->is_typed()) { return nullptr; }
			for (const auto& elem : node->values()) {
				if (const char* w = non_value_semantic_what(elem)) { return w; }
			}
			return nullptr;
		}
		case script_value::TYPEID_MAP:
			for (const auto& [key, val] : v.unchecked_as_map()) {
				if (const char* w = non_value_semantic_what(key)) { return w; }
				if (const char* w = non_value_semantic_what(val)) { return w; }
			}
			return nullptr;
		case script_value::TYPEID_OBJECT: {
			auto holder = v.get_object_holder();
			if (!holder || !holder->is_class_instance_wrapper || !holder->data) { return "bound host object"; }
			if (v.is_shared_ptr_type()) { return "shared_ptr"; }
			const auto* inst = static_cast<const class_instance*>(holder->data.get());
			const class_definition* cd = inst->get_class_definition();
			if (!cd || !cd->flat_value_semantics()) { return "object"; }
			const char* found = nullptr;
			inst->for_each_field([&](uint64_t, const script_value& field) { if (!found) { found = non_value_semantic_what(field); } });
			return found ? found : "object";
		}
		case script_value::TYPEID_CPP_BOUND:
			return v.bound_semantic_index() != script_value::TYPEID_NULL || v.is_null() ? "bound host value" : "bound host object";
		case script_value::TYPEID_FUNCTION: return "function";
		case script_value::TYPEID_SHARED_PTR: return "shared_ptr";
		case script_value::TYPEID_WEAK_PTR: return "weak_ptr";
		default: return "unsupported type";
		}
	}

	// The fork's element proof, shared by both forms: after it returns without a failing
	// index, an element can be handed to its chunk's worker without touching a refcount
	// another thread can see - parallel_for mutates it in place through a reference, the
	// transform's by-value binding clones it (so the transform reaches every element WHOLE
	// and passes an open reach). An object-kind source takes the declared-type verdict first
	// (one scan of the 16-byte slot tags, no instance read; Dev ruling: the declared type is
	// a proven fact about every element) and walks per element only when it fails; every
	// other source that is not statically all-primitive walks (normalize_element_inplace).
	// engine::allow_unsafe_parallel skips it all (trusted scripts).
	struct element_proof {
		bool typed_obj = false;
		std::vector<type_info*> tags;                          // distinct slot tags of an object-kind source
		std::vector<const class_definition*> element_classes;  // live element classes (method admission, prewarm)
		size_t failed = SIZE_MAX;                              // first failing element; SIZE_MAX = proven
		std::string what;                                      // the walk's exception text (empty for a verdict)
	};

	// elements: the boxed view walked for a non-object kind (the node's own vector, or the
	// caller's materialized copy of a typed primitive/string buffer)
	element_proof prove_elements(engine& eng, const script_value& array_value, script_array& node,
	                             std::vector<script_value>& elements, const reach_filter& reach,
	                             const char* prefix, std::vector<type_info*>* types = nullptr) {
		element_proof proof;
		proof.typed_obj = node.is_typed() && node.kind() == script_array::kind_t::obj;
		auto& slots = node.objs();
		const size_t n = proof.typed_obj ? node.size() : elements.size();
		const class_definition* string_free_class = nullptr;
		bool declared_exclusive = false;
		if (proof.typed_obj) {
			type_info* last = nullptr;
			for (const obj_slot& slot : slots) {
				type_info* tag = slot.tag.get();
				if (tag == last) { continue; }
				last = tag;
				if (std::find(proof.tags.begin(), proof.tags.end(), tag) == proof.tags.end()) { proof.tags.push_back(tag); }
			}
		}
		if (!eng.allow_unsafe_parallel() && proof.typed_obj && n > 0) {
			declared_exclusive = node.live_elem_refs == 0;
			std::vector<const class_definition*> visited;
			for (type_info* tag : proof.tags) {
				const class_definition* cd = (tag && tag->base_type == script_value_type::jai_object_type)
					? eng.get_class_registry().find_script_class(std::string(tag->type_name)).get() : nullptr;
				if (!cd) { declared_exclusive = false; continue; }   // shared_ptr-tagged or host slots: the walk decides
				if (std::find(proof.element_classes.begin(), proof.element_classes.end(), cd) == proof.element_classes.end()) { proof.element_classes.push_back(cd); }
				if (!declared_class_exclusive(cd, reach, eng, /*with_subclasses*/ false, visited)) { declared_exclusive = false; }
				if (proof.tags.size() == 1 && cd->flat_value_semantics() && !class_reach_touches_string(cd, reach, eng)) { string_free_class = cd; }
			}
#ifdef JAISCRIPT_VM_PROFILE
			fprintf(stderr, "[region-profile] %s verdict: typed-obj array, %zu class(es), first %s, reach_all=%d reached=%zu whole=%zu live_elem_refs=%u -> %s\n",
			        prefix, proof.tags.size(), proof.element_classes.empty() ? "?" : proof.element_classes[0]->get_name().c_str(), (int)!reach.members,
			        reach.members ? reach.members->size() : (size_t)0, reach.whole ? reach.whole->size() : (size_t)0, node.live_elem_refs,
			        declared_exclusive ? "declared-type proof, no element walk" : "per-element walk");
#endif
		}
#ifdef JAISCRIPT_VM_PROFILE
		fprintf(stderr, "[region-profile] %s source: typed=%d kind=%d live_elem_refs=%u n=%zu\n", prefix, (int)node.is_typed(), (int)node.kind(), node.live_elem_refs, n);
#else
		(void)prefix;
#endif
		if (eng.allow_unsafe_parallel() || declared_exclusive || statically_all_primitive(array_value)) {
			return proof;
		}
		// An object slot lends its handle to a boxed value for the walk (the count stays 1, so
		// the alias check sees the truth) and takes back whatever the walk left - the same
		// handle or a detached clone
		struct slot_box {
			obj_slot& slot;
			script_value v;
			slot_box(obj_slot& s, engine* e) : slot(s), v(std::move(s.handle), s.tag, e) {}
			~slot_box() { slot.handle = std::move(v.unchecked_get_object_storage()); slot.tag = v.get_type_info(); }
		};
		for (size_t i = 0; i < n; ++i) {
			bool ok = false;
			try {
				if (proof.typed_obj) {
					slot_box box(slots[i], &eng);
					ok = normalize_element_inplace(box.v, eng, reach, node.live_elem_refs == 0, string_free_class, types);
				} else {
					ok = normalize_element_inplace(elements[i], eng, reach, node.live_elem_refs == 0, string_free_class, types);
				}
			} catch (const std::exception& e) {
				proof.failed = i;
				proof.what = e.what();
				return proof;
			}
			if (!ok) {
				proof.failed = i;
				return proof;
			}
			if (!proof.typed_obj && elements[i].is_object()) {
				auto holder = elements[i].get_object_holder();
				if (holder && holder->is_class_instance_wrapper && holder->data) {
					auto* cd = static_cast<class_instance*>(holder->data.get())->get_class_definition();
					if (cd && (proof.element_classes.empty() || proof.element_classes.back() != cd) &&
					    std::find(proof.element_classes.begin(), proof.element_classes.end(), cd) == proof.element_classes.end()) {
						proof.element_classes.push_back(cd);
					}
				}
			}
		}
		return proof;
	}

	// Borrow admission (content half): TRUE when every element of the container is a plain
	// primitive or a string - element reads through the borrow are then raw const reads
	// minting worker-local values (a string read copies the BYTES, never the handle), safe
	// against the non-atomic strong counts by construction. Const walk, no handle copies.
	bool parallel_content_all_primitive(const script_value& v) {
		switch (v.raw_storage_index()) {
		case script_value::TYPEID_ARRAY: {
			if (const script_array* node = v.unchecked_array_node(); node->is_typed()) {
				return node->kind() != script_array::kind_t::obj;   // handles never borrow raw
			}
			for (const auto& elem : v.unchecked_as_array()) {
				switch (elem.raw_storage_index()) {
				case script_value::TYPEID_NULL:
				case script_value::TYPEID_INT:
				case script_value::TYPEID_FLOAT:
				case script_value::TYPEID_CHAR:
				case script_value::TYPEID_BOOL:
				case script_value::TYPEID_STRING:
					break;
				default:
					return false;
				}
			}
			return true;
		}
		case script_value::TYPEID_MAP: {
			for (const auto& [key, val] : v.unchecked_as_map()) {
				const size_t ki = key.raw_storage_index();
				const size_t vi = val.raw_storage_index();
				auto primitive = [](size_t idx) {
					return idx == script_value::TYPEID_NULL || idx == script_value::TYPEID_INT ||
					       idx == script_value::TYPEID_FLOAT || idx == script_value::TYPEID_CHAR ||
					       idx == script_value::TYPEID_BOOL || idx == script_value::TYPEID_STRING;
				};
				if (!primitive(ki) || !primitive(vi)) { return false; }
			}
			return true;
		}
		default:
			return false;
		}
	}

	// One captured name, resolved at the barrier from the LIVE global value
	struct resolved_capture {
		const parallel_admission::capture_entry* entry = nullptr;
		std::optional<script_value> source;  // deref'd live value; for a borrow this handle
		                                     // IS the region anchor (held until the join)
		parallel_capture_kind kind = parallel_capture_kind::scalar;
	};

	std::vector<size_t> partition_flat(size_t n, size_t chunks) {
		std::vector<size_t> bounds(chunks + 1);
		for (size_t k = 0; k <= chunks; ++k) {
			bounds[k] = n * k / chunks;
		}
		return bounds;
	}

	std::vector<size_t> partition_weighted(const std::vector<double>& weights, size_t chunks) {
		double total = 0.0;
		for (double w : weights) { total += w; }
		if (!(total > 0.0)) { return partition_flat(weights.size(), chunks); }
		std::vector<size_t> bounds(chunks + 1);
		bounds[0] = 0;
		bounds[chunks] = weights.size();
		double prefix = 0.0;
		size_t k = 1;
		for (size_t i = 0; i < weights.size() && k < chunks; ++i) {
			if (prefix >= total * static_cast<double>(k) / static_cast<double>(chunks)) {
				bounds[k++] = i;
			}
			prefix += weights[i];
		}
		while (k < chunks) { bounds[k++] = weights.size(); }
		return bounds;
	}

	// Flat-table content equality for the snapshot-capture cache: top-level string, or
	// a hetero array (same type stamp) whose elements are strings / raw scalars / null.
	// Live elements compare through deref; detached copies hold no references. Any
	// deeper shape returns false - the caller re-detaches rather than risks.
	bool flat_snapshot_equal(const script_value& live_in, const script_value& cached) {
		const script_value& live = live_in.deref();
		const size_t li = live.raw_storage_index();
		if (li != cached.raw_storage_index()) { return false; }
		if (li == script_value::TYPEID_STRING) {
			return *live.get_storage().get<strong_ptr<script_string>>() ==
			       *cached.get_storage().get<strong_ptr<script_string>>();
		}
		if (li != script_value::TYPEID_ARRAY) { return false; }
		const script_array* ln = live.unchecked_array_node();
		const script_array* cn = cached.unchecked_array_node();
		if (ln->is_typed() || cn->is_typed()) { return false; }
		if (live.get_type_info().get() != cached.get_type_info().get()) { return false; }
		const auto& lv = ln->values();
		const auto& cv = cn->values();
		if (lv.size() != cv.size()) { return false; }
		for (size_t i = 0; i < lv.size(); ++i) {
			const script_value& a = lv[i].deref();
			const script_value& b = cv[i];
			const size_t ai = a.raw_storage_index();
			if (ai != b.raw_storage_index()) { return false; }
			switch (ai) {
			case script_value::TYPEID_STRING:
				if (*a.get_storage().get<strong_ptr<script_string>>() !=
				    *b.get_storage().get<strong_ptr<script_string>>()) { return false; }
				break;
			case script_value::TYPEID_INT:
				if (a.unchecked_as_int() != b.unchecked_as_int()) { return false; }
				break;
			case script_value::TYPEID_FLOAT: {
				// Bitwise: NaN-stable (== would force a re-detach every region)
				const script_float af = a.unchecked_as_float();
				const script_float bf = b.unchecked_as_float();
				if (std::memcmp(&af, &bf, sizeof(af)) != 0) { return false; }
				break;
			}
			case script_value::TYPEID_BOOL:
				if (a.unchecked_as_bool() != b.unchecked_as_bool()) { return false; }
				break;
			case script_value::TYPEID_CHAR:
				if (a.unchecked_as_char() != b.unchecked_as_char()) { return false; }
				break;
			case script_value::TYPEID_NULL:
				break;
			default:
				return false;
			}
		}
		return true;
	}

	// One snapshot/string capture, cache-aware: a content-matched cached copy is reused
	// outright (types re-registered; provisioned_nodes are the caller's rebuild-on-miss
	// concern); a miss detaches as before and (re)stores the record only when
	// flat_snapshot_equal can prove the shape. `validated` memoizes ONE content proof
	// per region per name: the first slot compares against the live source, and
	// siblings whose record carries the same store serial hold identical content by
	// construction (stored single-threaded at one barrier from one source), so they
	// skip the compare. Throws propagate to the caller's existing per-capture handler.
	void provision_snapshot_capture(parallel_worker_slot& slot, uint64_t name_id,
	                                const script_value& source, engine& eng,
	                                std::vector<type_info*>& value_types,
	                                script_value& provisioned,
	                                uint64_t region_serial,
	                                std::unordered_map<uint64_t, uint64_t>& validated,
	                                std::vector<const void*>& fresh_nodes,
	                                bool& any_snapshot_miss
#ifdef JAISCRIPT_VM_PROFILE
	                                , parallel_engine_state::region_phase_prof& prof
	                                , std::string_view prof_name
#endif
	                                ) {
		parallel_worker_slot::snapshot_capture_cache_entry* cached = nullptr;
		for (auto& e : slot.snapshot_cache) {
			if (e.name_id == name_id) { cached = &e; break; }
		}
		if (cached) {
			const auto memo = validated.find(name_id);
			const bool proven = memo != validated.end() && memo->second == cached->built_serial;
			if (proven || flat_snapshot_equal(source, cached->value)) {
				if (!proven) { validated[name_id] = cached->built_serial; }
#ifdef JAISCRIPT_VM_PROFILE
				++prof.cache_hits;
#endif
				value_types.insert(value_types.end(), cached->types.begin(), cached->types.end());
				provisioned = cached->value;
				return;
			}
		}
		any_snapshot_miss = true;
#ifdef JAISCRIPT_VM_PROFILE
		++prof.cache_misses;
		++prof.cache_miss_names[std::string(prof_name)];
		const uint64_t prof_detach0 = __rdtsc();
#endif
		std::vector<const void*> capture_nodes;
		std::vector<type_info*> fresh_types;
		provisioned = source.parallel_detached_copy(&fresh_types, &capture_nodes);
#ifdef JAISCRIPT_VM_PROFILE
		prof.cache_miss_cyc[std::string(prof_name)] += __rdtsc() - prof_detach0;
#endif
		value_types.insert(value_types.end(), fresh_types.begin(), fresh_types.end());
		fresh_nodes.insert(fresh_nodes.end(), capture_nodes.begin(), capture_nodes.end());
		if (flat_snapshot_equal(source, provisioned)) {
#ifdef JAISCRIPT_VM_PROFILE
			++prof.cache_stores;
#endif
			parallel_worker_slot::snapshot_capture_cache_entry rec;
			rec.name_id = name_id;
			rec.built_serial = region_serial;
			rec.value = provisioned;
			rec.nodes = std::move(capture_nodes);
			rec.types = std::move(fresh_types);
			if (cached) { *cached = std::move(rec); }
			else { slot.snapshot_cache.push_back(std::move(rec)); }
		} else {
#ifdef JAISCRIPT_VM_PROFILE
			++prof.cache_unprovable;
#endif
			if (cached) {
				// Shape stopped being provable: the record can never hit again - drop its pins
				cached->name_id = 0;
				cached->built_serial = 0;
				cached->value = script_value(std::monostate{}, &eng);
				cached->nodes.clear();
				cached->types.clear();
			}
		}
	}

	// provisioned_nodes maintenance for the cross-region cache: on an all-hit region the
	// set from the previous region already covers every cached node (stale extras from
	// dead fresh nodes can only cause a spurious result-detach - content-neutral); any
	// miss rebuilds it exactly from the surviving records plus this region's fresh nodes.
	void rebuild_provisioned_nodes(parallel_worker_slot& slot, bool any_snapshot_miss,
	                               const std::vector<const void*>& fresh_nodes) {
		if (!any_snapshot_miss) { return; }
		slot.provisioned_nodes.clear();
		for (const auto& rec : slot.snapshot_cache) {
			slot.provisioned_nodes.insert(rec.nodes.begin(), rec.nodes.end());
		}
		slot.provisioned_nodes.insert(fresh_nodes.begin(), fresh_nodes.end());
	}

} // namespace

// ============================== THE REGION (one fork for both forms) ==============================
void recover_pending_registrations(parallel_engine_state& state) {
	if (!state.pending_registration_retry || state.active_region) { return; }
	// Recover every retained pool before eviction, slot replacement or a class
	// reload could discard pending copies or omit them from a migration snapshot.
	for (auto& pool : state.region_pools) {
		for (auto& slot : pool.worker_slots) {
			if (slot) { drain_pending_instances(state, *slot); }
		}
	}
	state.pending_registration_retry = false;
}

// parallel_for and parallel_transform run the SAME region: containers[0] is the range (the
// transform's input), further containers are the zip's arrays (the transform's output is one),
// every chunk gets one cursor per array, and each worker enters script once to run its chunk
// loop. The plan carries the only differences: the prefix of every text, the environment
// captured reads resolve through, which arrays the body may write (the transform never writes
// its input, so borrowing it is legal and its typed primitive form is boxed once instead of
// demoted in place), the chunk partition (the transform's weight hint), and whether element
// methods are admitted (the in-place statement). parallel_transform therefore costs exactly what
// the equivalent zip statement costs plus the output array it hands back.
namespace {

struct region_plan {
	const char* prefix = "parallel_for";
	std::vector<script_value>* containers = nullptr;                 // [0] = the range, then the zip's arrays
	std::shared_ptr<script_defined_function> root_fn;                // the body function / fn (admission root, pool key)
	const parallel_admission* adm = nullptr;
	const std::shared_ptr<function_decl>* chunk_wrapper = nullptr;
	std::shared_ptr<environment> capture_env;                        // null: the global environment
	bool inplace = false;                                            // parallel_for: the range is written, element methods admitted
	std::vector<size_t> bounds;                                      // empty: the flat partition
	const parallel_admission* extra_seed_adm = nullptr;              // weight fn types (transform)
	bool zip_arrays_fresh = false;                                   // the zip's arrays were allocated by the caller this call (the transform's output): exclusive by construction
#ifdef JAISCRIPT_VM_PROFILE
	parallel_engine_state::region_phase_prof* prof = nullptr;
#endif
};

// The zip's arrays and the range: an array, the range's length, distinct from the range
checked_result<void> run_region(engine& eng, region_plan& plan) {
	parallel_engine_state& state = eng.parallel_state();
	recover_pending_registrations(state);
	const std::string pfx = std::string(plan.prefix) + ": ";
	auto raise = [&state](std::error_code code, std::string message) {
		state.error_text = std::move(message);
		return checked_result<void>(code, std::string_view(state.error_text));
	};
	auto usage = [&raise, &pfx](std::string message) {
		return raise(make_error_code(runtime_error_code::unsupported_operation), pfx + std::move(message));
	};
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_t0 = __rdtsc();
#endif
	std::vector<script_value>& containers = *plan.containers;
	const parallel_admission* adm = plan.adm;
	const script_value& array_value = containers.front().is_reference() ? containers.front().deref() : containers.front();
	const script_array* source_node = array_value.unchecked_array_node();

	// CAPTURED READS resolve through the statement's environment chain (its walk reaches the
	// global env; slot-resident function locals are not env-visible and correctly error as
	// undefined) or, for the builtin, the global env. A BORROW of an array the body WRITES
	// would read racing content (borrow indexes any element, writes own chunks only) -
	// contract A errors instead of silently demoting; snapshots of it stay legal.
	std::vector<resolved_capture> captures;
	state.last_captures.clear();
	state.faults.checkpoint("region.capture_storage");
	captures.reserve(adm->captures.size());
	auto written_container = [&](const script_value& v) {
		if (v.raw_storage_index() != script_value::TYPEID_ARRAY) { return false; }
		for (size_t j = plan.inplace ? 0 : 1; j < containers.size(); ++j) {
			const script_value& cj = containers[j].is_reference() ? containers[j].deref() : containers[j];
			if (cj.is_array() && cj.unchecked_array_node() == v.unchecked_array_node()) { return true; }
		}
		return false;
	};
	auto classify_and_add = [&](const parallel_admission::capture_entry& entry) -> checked_result<void> {
		auto global = plan.capture_env ? plan.capture_env->get(entry.name_id)
		                               : eng.get_global_environment()->get(entry.name_id);
		if (!global) {
			return usage("captured name '" + std::string(entry.name) + "' is not defined at the region barrier");
		}
		resolved_capture cap;
		cap.entry = &entry;
		cap.source = global.value().deref();
		switch (cap.source->raw_storage_index()) {
		case script_value::TYPEID_NULL:
		case script_value::TYPEID_INT:
		case script_value::TYPEID_FLOAT:
		case script_value::TYPEID_CHAR:
		case script_value::TYPEID_BOOL:
			cap.kind = parallel_capture_kind::scalar;
			break;
		case script_value::TYPEID_CPP_BOUND: {
			const size_t sem = cap.source->bound_semantic_index();
			if (sem == script_value::TYPEID_NULL && !cap.source->is_null()) {
				return usage("captured name '" + std::string(entry.name) + "' is not value-semantic (bound host object)");
			}
			cap.source = cap.source->clone();
			cap.kind = sem == script_value::TYPEID_STRING
				? parallel_capture_kind::string : parallel_capture_kind::scalar;
			break;
		}
		case script_value::TYPEID_STRING:
			cap.kind = parallel_capture_kind::string;
			break;
		case script_value::TYPEID_ARRAY:
		case script_value::TYPEID_MAP: {
			const bool same_container = written_container(*cap.source);
			const bool borrowable = entry.borrow_eligible && !same_container &&
			                        (statically_all_primitive(*cap.source) || parallel_content_all_primitive(*cap.source));
			if (same_container && entry.borrow_eligible) {
				return usage("captured name '" + std::string(entry.name) +
				             "' is the container being mutated (reads through it would race the element writes)");
			}
			cap.kind = borrowable ? parallel_capture_kind::borrow : parallel_capture_kind::snapshot;
			break;
		}
		case script_value::TYPEID_FUNCTION:
			return usage("captured name '" + std::string(entry.name) +
			             "' is a function value (only direct calls to statically-known functions are allowed in a parallel body)");
		default:
			return usage("captured name '" + std::string(entry.name) + "' is not value-semantic (object/shared_ptr/weak_ptr)");
		}
		state.faults.checkpoint("region.capture_metadata", captures.size());
		state.last_captures.emplace_back(entry.name_id, cap.kind);
		state.faults.checkpoint("region.capture_collect", captures.size());
		captures.push_back(std::move(cap));
		return {};
	};
	for (const auto& entry : adm->captures) {
		auto classified = classify_and_add(entry);
		if (!classified) { return classified; }
	}

	// THE RANGE. A primitive-kind typed range the body may write demotes IN PLACE at the
	// barrier (STAGE-2a BRIDGE, typed_array_design.md - node identity preserved) so the
	// hetero kernel below stays correct until stage 3's raw typed worker reads/writes land;
	// one the body only reads (the transform's input) is boxed ONCE into a private node the
	// cursor can address instead. An OBJECT-kind range keeps its slots: element references
	// materialize the slot's handle per touch and the declared class carries the exclusivity
	// proof (Dev ruling: the declared type of a typed array is a proven fact about every element).
	const bool typed_obj = source_node->is_typed() && source_node->kind() == script_array::kind_t::obj;
	script_value boxed_range(std::monostate{}, &eng);
	state.faults.checkpoint("region.input_storage");
	if (source_node->is_typed() && !typed_obj) {
		if (plan.inplace) {
			const_cast<script_array*>(source_node)->demote_to_hetero(&eng);
		} else {
			boxed_range = script_value::make_array(nullptr, &eng);
			boxed_range.unchecked_get_array_storage()->values() = const_cast<script_array*>(source_node)->materialize_values(&eng);
		}
	}
	script_array& node = boxed_range.is_array() ? *boxed_range.unchecked_get_array_storage() : *const_cast<script_array*>(source_node);
	auto& source = node.values();
	const size_t n = node.size();

	// ELEMENT PROOF + NORMALIZATION (prove_elements): every element must end this pass
	// exclusively-mutable - shared strings detach in place, the declared class of an
	// object-kind source proves it without one element read (the body's element reach
	// narrows both). A body that binds its element in place (in-place statement, or a
	// read-only by-value parameter) reaches only the members it names; a body that copies the
	// element (a mutating by-value parameter clones every field) reaches it whole. The
	// builtin's all-primitive input is exclusive by construction (typed proof, zero scans;
	// untyped pay ONE content scan). engine::allow_unsafe_parallel skips the walk entirely.
	const bool binds_in_place = plan.inplace || (!adm->element_reach_all && !adm->element_mutated);
	const reach_filter reach = binds_in_place
		? reach_filter{ adm->element_reach_all ? nullptr : &adm->element_reached_members, &adm->element_reached_whole }
		: reach_filter{};
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_norm0 = __rdtsc();
#endif
	std::vector<type_info*> value_types;
	element_proof proof;
	const bool skip_proof = !plan.inplace && (statically_all_primitive(array_value) || parallel_content_all_primitive(array_value));
	if (!skip_proof) {
		state.faults.checkpoint("region.element_proof");
		proof = prove_elements(eng, array_value, node, source, reach, plan.prefix, plan.inplace ? nullptr : &value_types);
		if (proof.failed != SIZE_MAX) {
			if (!proof.what.empty()) {
				return usage("element " + std::to_string(proof.failed) + ": " + proof.what);
			}
			if (plan.inplace) {
				return usage("element " + std::to_string(proof.failed) +
				             " cannot be mutated in place (only value-semantic shapes - primitives, strings,"
				             " value containers, flat value-class instances - are provably exclusive;"
				             " engine::allow_unsafe_parallel(true) overrides)");
			}
			const script_value failed_element = typed_obj ? node.get(proof.failed, &eng) : source[proof.failed];
			const char* what = non_value_semantic_what(failed_element);
			return usage("element " + std::to_string(proof.failed) +
			             ": value is not value-semantic (" + (what ? what : "object") + ")");
		}
	}
	const std::vector<type_info*>& tags = proof.tags;
	std::vector<const class_definition*>& element_classes = proof.element_classes;

	// THE ZIP'S ARRAYS: arrays of the range's length, each proven exclusive like the range
	// (whole reach: the body may write anything through their loop variables; a typed primitive
	// kind demoted like the range, an object kind addressed in place)
	std::vector<script_array*> zip_nodes;
	std::vector<type_info*> zip_tags;
	for (size_t j = 1; j < containers.size(); ++j) {
		const script_value& zv = containers[j].is_reference() ? containers[j].deref() : containers[j];
		if (!zv.is_array()) {
			return usage("zipped array " + std::to_string(j + 1) + " is not an array");
		}
		state.faults.checkpoint("region.zip_storage", j);
		script_array* znode = const_cast<script_array*>(zv.unchecked_array_node());
		if (znode->size() != n) {
			return usage("zipped array " + std::to_string(j + 1) + " has " + std::to_string(znode->size()) +
			             " elements, the range has " + std::to_string(n) + " (zipped arrays must have the same length)");
		}
		if (znode == source_node || znode == &node) {
			return usage("zipped array " + std::to_string(j + 1) + " is the range itself");
		}
		if (znode->is_typed() && znode->kind() != script_array::kind_t::obj) { znode->demote_to_hetero(&eng); }
		if (!plan.zip_arrays_fresh) {
			element_proof zproof = prove_elements(eng, zv, *znode, znode->values(), reach_filter{}, plan.prefix);
			if (zproof.failed != SIZE_MAX) {
				return usage("zipped array " + std::to_string(j + 1) + " element " + std::to_string(zproof.failed) +
				             (zproof.what.empty() ? std::string(" cannot be mutated in place") : ": " + zproof.what));
			}
			zip_tags.insert(zip_tags.end(), zproof.tags.begin(), zproof.tags.end());
			for (const class_definition* cd : zproof.element_classes) {
				if (std::find(element_classes.begin(), element_classes.end(), cd) == element_classes.end()) { element_classes.push_back(cd); }
			}
		}
		zip_nodes.push_back(znode);
	}
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_norm1 = __rdtsc();
#endif
	if (n == 0) {
		return {};
	}

	// ELEMENT METHODS (in-place statement): admitted at the barrier against the element
	// classes the proof found; their captures classify like the body's
	admission_builder method_adm(eng);
	method_adm.inplace_root = true;
	method_adm.prefix = plan.prefix;
	size_t method_capture_base = 0;
	state.faults.checkpoint("region.method_admission");
	if (plan.inplace && !element_classes.empty()) {
		method_adm.adm.element_method_calls = adm->element_method_calls;
		for (const auto& entry : adm->captures) {
			method_adm.capture_index.emplace(entry.name_id, method_adm.adm.captures.size());
			method_adm.adm.captures.push_back(entry);
		}
		method_capture_base = method_adm.adm.captures.size();
		if (!method_adm.admit_element_methods(element_classes)) {
			state.error_text = method_adm.adm.message;
			return checked_result<void>(method_adm.adm.code, std::string_view(state.error_text));
		}
		for (size_t ci = method_capture_base; ci < method_adm.adm.captures.size(); ++ci) {
			auto classified = classify_and_add(method_adm.adm.captures[ci]);
			if (!classified) { return classified; }
		}
	}

	// THREAD COUNT: the override or min(hardware_concurrency - 1, 6), one context below
	// k_parallel_small_n, then the measured-cost cap (parallel_design.md section 3)
	size_t worker_count = eng.parallel_thread_count();
	if (worker_count < 1) { worker_count = 1; }
	if (worker_count > n) { worker_count = n; }
	if (n < k_parallel_small_n) { worker_count = 1; }
	const std::chrono::nanoseconds budget =
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(eng.execution_budget()));
	const backend_type engine_backend = eng.get_backend_type();
	if (engine_backend == backend_type::custom) {
		return usage("custom execution backends are not supported");
	}
	const bool use_vm = engine_backend == backend_type::vm;
	state.faults.checkpoint("region.pool_storage");
	parallel_engine_state::parallel_region_pool* pool = &acquire_region_pool(state, static_cast<const void*>(plan.root_fn->body.get()), use_vm);
	worker_count = cap_workers_by_cost(worker_count, n, state, *pool);
	std::vector<size_t> bounds;
	state.faults.checkpoint("region.bounds");
	if (!plan.bounds.empty() && plan.bounds.size() >= 2 && worker_count > 1) {
		bounds = plan.bounds;                       // the caller's weighted partition
		worker_count = bounds.size() - 1;
	} else {
		bounds = partition_flat(n, worker_count);
	}
#ifdef JAISCRIPT_VM_PROFILE
	fprintf(stderr, "[region-profile] %s n=%zu workers=%zu ns/elem=%.0f overhead/worker=%.0f\n", plan.prefix, n, worker_count, pool->ns_per_element, state.worker_overhead_ns);
#endif
	state.faults.checkpoint("region.bounds_metadata");
	state.last_chunk_bounds = bounds;
	if (pool->worker_slots.size() < worker_count) {
		state.faults.checkpoint("region.worker_storage");
		pool->worker_slots.resize(worker_count);
	}
	std::vector<parallel_worker_slot*> contexts;
	state.faults.checkpoint("region.context_storage");
	contexts.reserve(worker_count);

	// One owner for activation, the thread table, join obligations and borrowed storage.
	// A failed setup discards partial reusable slots; a completed setup keeps their caches.
	struct region_guard_t {
		engine& eng;
		parallel_engine_state& state;
		std::vector<std::unique_ptr<parallel_worker_slot>>& slots;
		size_t count;
		parallel_region_table table;
		parallel_region_table* previous_region;
		bool previous_frozen;
		jai::thread_pool* pending_pool = nullptr;
		bool active = false;
		bool prepared = false;

		region_guard_t(engine& e, parallel_engine_state& s,
		               std::vector<std::unique_ptr<parallel_worker_slot>>& workers, size_t n)
			: eng(e), state(s), slots(workers), count(n),
			  previous_region(s.active_region), previous_frozen(e.get_symbolizer()->frozen()) {}
		region_guard_t(const region_guard_t&) = delete;
		region_guard_t& operator=(const region_guard_t&) = delete;

		void activate(bool freeze_symbols) noexcept {
			// Accepted work can create live copies even if a later submit throws.
			// Only a complete post-join drain clears this obligation.
			state.pending_registration_retry = true;
			if (freeze_symbols) { eng.get_symbolizer()->set_frozen(true); }
			state.active_region = &table;
			active = true;
		}
		void join() {
			if (pending_pool) {
				pending_pool->wait_idle();
				pending_pool = nullptr;
			}
		}
		void restore() noexcept {
			if (active) {
				state.active_region = previous_region;
				eng.get_symbolizer()->set_frozen(previous_frozen);
				active = false;
			}
		}
		~region_guard_t() {
			join();                 // accepted work must finish before table/cursor destruction
			restore();
			for (size_t k = 0; k < count && k < slots.size(); ++k) {
				if (!prepared) {
					slots[k].reset();    // includes an environment whose publication only partly succeeded
					continue;
				}
				parallel_worker_slot* slot = slots[k].get();
				if (!slot) { continue; }
				for (uint64_t id : slot->capture_name_ids) {
					if (script_value* value = slot->root_env->get_own_value_ptr(id)) {
						*value = script_value(std::monostate{}, &eng);   // existing storage, no allocation
					}
				}
				slot->capture_name_ids.clear();
				slot->element_refs.clear();
				slot->cursors.clear();
				slot->call_args.clear();
				slot->raw_input = nullptr;
				slot->typed_input = nullptr;
				slot->backend->set_parallel_method_pins(nullptr);
				slot->method_pins.entries.clear();
			}
		}
	} region_guard{ eng, state, pool->worker_slots, worker_count };
	std::unordered_map<uint64_t, uint64_t> snapshot_validated;

	auto& range_handle = boxed_range.is_array() ? boxed_range.unchecked_get_array_storage()
	                                            : const_cast<script_value&>(array_value).unchecked_get_array_storage();
	type_info_ptr element_type = array_value.get_type_info().get() ? array_value.get_type_info()->element_type() : type_info_ptr(nullptr);
	if (type_info* t = array_value.get_type_info().get()) { value_types.push_back(t); }
	state.faults.checkpoint("region.type_metadata");
	value_types.insert(value_types.end(), tags.begin(), tags.end());   // subclass slots carry their own tags
	value_types.insert(value_types.end(), zip_tags.begin(), zip_tags.end());
	for (size_t j = 1; j < containers.size(); ++j) {
		const script_value& zv = containers[j].is_reference() ? containers[j].deref() : containers[j];
		if (type_info* t = zv.get_type_info().get()) { value_types.push_back(t); }
	}
	for (size_t k = 0; k < worker_count; ++k) {
		auto& slot = pool->worker_slots[k];
		state.faults.checkpoint("region.worker_slot", k);
		if (slot && slot_matches(*slot, *adm, *plan.root_fn, use_vm)) {
			reset_worker_slot(eng, *slot, budget);
		} else {
			slot = provision_worker(eng, *adm, budget, use_vm, plan.chunk_wrapper);
		}
		restamp_alias_proofs(eng, *slot);
		slot->begin = bounds[k];
		slot->end = bounds[k + 1];
		slot->element_refs.clear();
		slot->raw_input = nullptr;
		slot->typed_input = nullptr;
		// ONE cursor reference per array per chunk, minted here (the array pin and the
		// live_elem_refs count are main-thread facts) and re-pointed per element by the worker's
		// chunk loop: a by-ref loop variable binds it, a by-value one copies through it exactly
		// as it would the element (normalization made every element exclusive, so those copies
		// touch no cross-thread counts). An admitted body cannot retain it past its iteration
		// (no lambdas, no 'new', stores deref, captured state is read-only, the result is discarded).
		state.faults.checkpoint("region.cursor_storage", k);
		if (slot->end > slot->begin) {
			slot->element_refs.push_back(script_value::make_element_reference(range_handle, slot->begin, &eng, element_type));
			for (size_t j = 0; j < zip_nodes.size(); ++j) {
				script_value& zc = containers[j + 1].is_reference() ? containers[j + 1].deref() : containers[j + 1];
				auto& zhandle = zc.unchecked_get_array_storage();
				type_info_ptr zet = zc.get_type_info().get() ? zc.get_type_info()->element_type() : type_info_ptr(nullptr);
				slot->element_refs.push_back(script_value::make_element_reference(zhandle, slot->begin, &eng, zet));
			}
		}
		// Argument storage also belongs to the region guard. Build it before fan-out so an
		// allocation failure cannot escape the worker's entry and be swallowed by the pool.
		slot->call_args.clear();
		state.faults.checkpoint("region.worker_arguments", slot->begin);
		slot->call_args.reserve(slot->element_refs.size() + 1);
		for (const auto& cursor : slot->element_refs) { slot->call_args.push_back(cursor); }
		slot->call_args.emplace_back(static_cast<script_int>(slot->end - slot->begin), &eng);

		// Provision the captured reads into THIS worker's root env (single-threaded, charged
		// to the enclosing execute): scalars copy, strings/snapshots detach OR reuse the slot's
		// cross-region cache (content-proven against the live source), borrows mint the
		// zero-copy view. Defines are per call and nulled at the join.
		slot->capture_name_ids.clear();
		bool any_snapshot_miss = false;
		std::vector<const void*> fresh_nodes;
		for (const auto& cap : captures) {
			state.faults.checkpoint("region.capture_value", k);
			script_value provisioned(std::monostate{}, &eng);
			try {
				switch (cap.kind) {
				case parallel_capture_kind::scalar:
					provisioned = *cap.source;
					break;
				case parallel_capture_kind::string:
				case parallel_capture_kind::snapshot:
					provision_snapshot_capture(*slot, cap.entry->name_id, *cap.source, eng,
					                           value_types, provisioned,
					                           state.region_use_counter, snapshot_validated,
					                           fresh_nodes, any_snapshot_miss
#ifdef JAISCRIPT_VM_PROFILE
					                           , *plan.prof, cap.entry->name
#endif
					                           );
					break;
				case parallel_capture_kind::borrow:
					if (type_info* t = cap.source->get_type_info().get()) { value_types.push_back(t); }
					provisioned = script_value::make_parallel_borrow(*cap.source, &eng);
					break;
				}
			} catch (const std::exception& e) {
				return usage("captured name '" + std::string(cap.entry->name) + "': " + e.what());
			}
			state.faults.checkpoint("region.capture_track", k);
			slot->capture_name_ids.push_back(cap.entry->name_id);
			state.faults.checkpoint("region.capture_publish", k);
			slot->root_env->define(cap.entry->name_id, std::move(provisioned));
		}
		state.faults.checkpoint("region.snapshot_metadata", k);
		rebuild_provisioned_nodes(*slot, any_snapshot_miss, fresh_nodes);
		// Method pins: each worker gets a dispatcher copy per admitted element method
		slot->method_pins.entries.clear();
		for (const auto& am : method_adm.admitted_methods) {
			state.faults.checkpoint("region.method_pin", k);
			const script_value shared_method = am.cls->get_method(am.name_id, false);
			const script_method_dispatch* src = shared_method.is_function()
				? shared_method.as_function().target<script_method_dispatch>() : nullptr;
			if (!src) {
				return usage("method '" + std::string(am.name) + "' has no script dispatcher (internal)");
			}
			script_method_dispatch wd;
			wd.eng = src->eng;
			wd.cls = src->cls;
			wd.name_id = src->name_id;
			wd.definition_env = src->definition_env;
			detail::parallel_method_pin pin;
			pin.method_value = script_value::make_function(std::move(wd), &eng);
			pin.resolved = am.decl;
			slot->method_pins.entries.push_back({ am.cls, am.name_id, std::move(pin) });
			if (slot->vm) { slot->vm->precompile_parallel_method(*am.decl); }
		}
		slot->backend->set_parallel_method_pins(slot->method_pins.entries.empty() ? nullptr : &slot->method_pins);
		contexts.push_back(slot.get());
	}

	// PRE-WARM: every type shape a worker can mint, interned before the tables freeze
	state.faults.checkpoint("region.prewarm");
	prewarm_region_types(eng, [&] {
		std::vector<type_info*> seed = adm->referenced_types;
		if (plan.extra_seed_adm) { seed.insert(seed.end(), plan.extra_seed_adm->referenced_types.begin(), plan.extra_seed_adm->referenced_types.end()); }
		seed.insert(seed.end(), value_types.begin(), value_types.end());
		seed.push_back(array_value.get_type_info().get());
		seed.push_back(element_type.get());
		for (const class_definition* cd : element_classes) { seed.push_back(cd->get_type_info().get()); }
		seed_classes_by_member(eng, adm->element_reached_members, seed);
		seed_classes_by_member(eng, method_adm.adm.element_reached_members, seed);
		return seed;
	}());

	// REGION: pinned pool workers for chunks 1..W-1 (submit_to fixes each chunk's thread so
	// the limits table can be built before fan-out), the calling thread runs chunk 0. The pool
	// is built once per engine at the engine's worker count (calibrated then).
	auto& table = region_guard.table;
	state.faults.checkpoint("region.table_entry", 0);
	table.entries.push_back({ std::this_thread::get_id(), &contexts[0]->limits });
	if (worker_count > 1) {
		const size_t pool_workers_needed = worker_count - 1;
		if (!state.pool || state.pool->worker_count() < pool_workers_needed) {
			state.faults.checkpoint("region.pool_create");
			state.pool = std::make_unique<jai::thread_pool>(
				std::max(pool_workers_needed, eng.parallel_thread_count() > 1 ? eng.parallel_thread_count() - 1 : size_t(1)), &state.faults);
			region_guard.pending_pool = state.pool.get();
			calibrate_pool_overhead(state);
			region_guard.pending_pool = nullptr;   // calibration joined each completed batch
		}
		for (size_t k = 1; k < worker_count; ++k) {
			state.faults.checkpoint("region.table_entry", k);
			table.entries.push_back({ state.pool->worker_thread_id(k - 1), &contexts[k]->limits, &contexts[k]->pending_instances });
		}
	}
	region_guard.prepared = true;
	region_guard.activate(!eng.allow_unsafe_parallel());
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_t1 = __rdtsc();
	fprintf(stderr, "[region-profile] %s setup split: normalize %llu | provision+rest %llu cyc\n", plan.prefix, (unsigned long long)(prof_norm1 - prof_norm0), (unsigned long long)(prof_t1 - prof_norm1));
#endif
	std::vector<std::pair<size_t, jai::thread_pool::task_type>> region_tasks;
	state.faults.checkpoint("region.tasks_reserve");
	region_tasks.reserve(worker_count > 1 ? worker_count - 1 : 0);
	for (size_t k = 1; k < worker_count; ++k) {
		parallel_worker_slot* ctx = contexts[k];
		engine* eng_ptr = &eng;
		state.faults.checkpoint("region.task_build", k);
		region_tasks.emplace_back(k - 1, [ctx, eng_ptr] {
#ifdef JAISCRIPT_VM_PROFILE
			ctx->prof_start_tsc = __rdtsc();
#endif
			run_worker_chunk(*ctx, *eng_ptr);
#ifdef JAISCRIPT_VM_PROFILE
			ctx->prof_end_tsc = __rdtsc();
#endif
		});
	}
	if (!region_tasks.empty()) {
		region_guard.pending_pool = state.pool.get();   // BEFORE a possibly partial submission
		state.pool->submit_pinned_batch(region_tasks.data(), region_tasks.size(), &state.faults);
	}
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_t2 = __rdtsc();
#endif
	const auto main_chunk_t0 = std::chrono::steady_clock::now();
	run_worker_chunk(*contexts[0], eng);
	note_region_cost(*pool, main_chunk_t0, bounds[1] - bounds[0]);
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_t3 = __rdtsc();
#endif
	region_guard.join();
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_t4 = __rdtsc();
	if (plan.prof) {
		auto& p = *plan.prof;
		++p.regions;
		p.setup_cyc += prof_t1 - prof_t0;
		p.submit_cyc += prof_t2 - prof_t1;
		p.main_chunk_cyc += prof_t3 - prof_t2;
		p.wait_cyc += prof_t4 - prof_t3;
		for (size_t k = 1; k < worker_count; ++k) {
			const parallel_worker_slot* ctx = contexts[k];
			if (!ctx->prof_start_tsc) continue;
			p.wake_lat_sum += ctx->prof_start_tsc > prof_t2 ? ctx->prof_start_tsc - prof_t2 : 0;
			p.wake_lat_max = std::max(p.wake_lat_max,
				ctx->prof_start_tsc > prof_t2 ? ctx->prof_start_tsc - prof_t2 : 0);
			p.worker_span_sum += ctx->prof_end_tsc - ctx->prof_start_tsc;
			++p.wake_samples;
		}
	}
#endif
	region_guard.restore();   // registration below can allocate or throw
	for (auto* ctx : contexts) {   // worker-created instances join the class registries now, unlocked
		drain_pending_instances(state, *ctx);
	}
	state.pending_registration_retry = false;
	// JOIN: memory roll-up, then ITERATION order picks the error a region reports
	size_t rolled_up = 0;
	for (const auto& ctx : contexts) {
		rolled_up += ctx->limits.memory_used;
	}
	if (rolled_up) {
		eng.execution_limits().memory_charge_deferred(rolled_up);
	}
	const parallel_worker_error* winner = nullptr;
	for (const auto& ctx : contexts) {
		if (ctx->error && (!winner || ctx->error->iteration < winner->iteration)) {
			winner = &*ctx->error;
		}
	}
	if (winner) {
		if (winner->terminal) {
			eng.execution_limits().terminal_error = true;
		}
		std::error_code code = winner->code ? winner->code
			: make_error_code(runtime_error_code::unsupported_operation);
		return raise(code, winner->message.empty() ? pfx + "worker failed" : winner->message);
	}
#ifdef JAISCRIPT_VM_PROFILE
	if (plan.prof) { plan.prof->join_cyc += __rdtsc() - prof_t4; }
#endif
	return {};
}

} // namespace

// parallel_transform(arr, fn[, weight_fn]) IS the zip `parallel_for (auto x, auto& o : arr, out)
// { o = fn(x); }` with the output array allocated here: fn's by-value parameter binds the
// element in place when the admission walk proved it read-only, the call result is a
// temporary that moves into the output slot (the return-site rule guarantees it aliases
// nothing fn could name), and nothing else runs per element.
checked_result<script_value> run_parallel_transform(engine& eng, const std::vector<script_value>& args) {
	parallel_engine_state& state = eng.parallel_state();
	auto raise = [&state](std::error_code code, std::string message) {
		state.error_text = std::move(message);
		return checked_result<script_value>(code, std::string_view(state.error_text));
	};
	auto usage = [&raise](std::string message) {
		return raise(make_error_code(runtime_error_code::unsupported_operation), std::move(message));
	};
	if (state.region_running) {
		return usage("parallel_transform: nested parallel regions are not supported (v0)");
	}
	if (args.size() < 2 || args.size() > 3) {
		return usage("parallel_transform: expected (array, fn) or (array, fn, weight_fn)");
	}
	const script_value& array_value = args[0].is_reference() ? args[0].deref() : args[0];
	if (!array_value.is_array()) {
		return usage("parallel_transform: first argument must be an array");
	}
	const script_callable* fn_payload = payload_from_function_value(args[1]);
	if (!fn_payload || fn_payload->kind != script_callable::kind_type::function || !fn_payload->fn || !fn_payload->fn->body) {
		return usage("parallel_transform: fn must be a script-defined function");
	}
	const script_callable* weight_payload = nullptr;
	if (args.size() == 3) {
		weight_payload = payload_from_function_value(args[2]);
		if (!weight_payload || weight_payload->kind != script_callable::kind_type::function ||
		    !weight_payload->fn || !weight_payload->fn->body) {
			return usage("parallel_transform: weight_fn must be a script-defined function");
		}
	}
	struct running_guard {
		parallel_engine_state& s;
		running_guard(parallel_engine_state& state_ref) : s(state_ref) { s.region_running = true; }
		~running_guard() { s.region_running = false; }
	} guard(state);

	const parallel_admission* adm = admit_cached(eng, state, fn_payload->fn);
	if (!adm->admitted) {
		return raise(adm->code, adm->message);
	}
	const std::shared_ptr<function_decl>& transform_wrapper = build_transform_wrapper(eng, state, fn_payload->fn, *adm);
	const parallel_admission* weight_adm = nullptr;
	if (weight_payload) {
		weight_adm = admit_cached(eng, state, weight_payload->fn);
		if (!weight_adm->admitted) {
			return raise(weight_adm->code, weight_adm->message);
		}
		if (weight_payload->fn->parameters().size() != 1 || weight_payload->fn->parameters()[0].is_reference) {
			return usage("parallel_transform: weight_fn must take exactly one parameter by value");
		}
	}

	const script_array* input_node = array_value.unchecked_array_node();
	const size_t n = input_node->size();
	// The output: the zip's second array, one null per element, handed back as the result
	script_value out = script_value::make_array(nullptr, &eng);
	out.unchecked_get_array_storage()->values().assign(n, script_value(std::monostate{}, &eng));

	// Optional weight hint: evaluated per element at the barrier, single-threaded, on the
	// engine's own backend (ruled section 11 Q1). Chunk boundaries equalize cumulative weight.
	region_plan plan;
	if (weight_payload && n >= k_parallel_small_n) {
		size_t worker_count = eng.parallel_thread_count();
		if (worker_count < 1) { worker_count = 1; }
		if (worker_count > n) { worker_count = n; }
		if (worker_count > 1) {
			std::vector<double> weights(n, 0.0);
			engine::external_call_guard call_guard(&eng);
			std::vector<script_value> weight_args;
			weight_args.emplace_back(std::monostate{}, &eng);
			const bool typed_obj = input_node->is_typed() && input_node->kind() == script_array::kind_t::obj;
			for (size_t i = 0; i < n; ++i) {
				weight_args[0] = input_node->is_typed() ? input_node->get(i, &eng) : input_node->values()[i];
				(void)typed_obj;
				auto r = eng.get_execution_backend()->execute_callable(*weight_payload, weight_args);
				if (!r) {
					return raise(r.error(), "parallel_transform: weight_fn failed for element " +
						std::to_string(i) + ": " + format_error(r, *eng.get_symbolizer()));
				}
				double w;
				const script_value& wv = r.value().is_reference() ? r.value().deref() : r.value();
				if (wv.is_int()) { w = static_cast<double>(wv.unchecked_as_int()); }
				else if (wv.is_float()) { w = wv.unchecked_as_float(); }
				else {
					return usage("parallel_transform: weight_fn must return a number (element " + std::to_string(i) + ")");
				}
				if (!std::isfinite(w) || w < 0.0) {
					return usage("parallel_transform: weight_fn must return a finite non-negative number (element " + std::to_string(i) + ")");
				}
				weights[i] = w;
			}
			plan.bounds = partition_weighted(weights, worker_count);
		}
	}

	std::vector<script_value> containers;
	containers.reserve(2);
	containers.push_back(array_value);
	containers.push_back(out);
	plan.prefix = "parallel_transform";
	plan.containers = &containers;
	plan.root_fn = fn_payload->fn;
	plan.adm = adm;
	plan.chunk_wrapper = &transform_wrapper;
	plan.inplace = false;
	plan.zip_arrays_fresh = true;
	plan.extra_seed_adm = weight_adm;
#ifdef JAISCRIPT_VM_PROFILE
	plan.prof = &state.prof_transform;
#endif
	auto ran = run_region(eng, plan);
	if (!ran) {
		return checked_result<script_value>(ran.error(), std::string_view(state.error_text));
	}
	return out;
}

// parallel_for (see parallel_transform.hpp): the in-place fork-join statement over one array
// or the zip of several, the body run in place on every thread's segment.
checked_result<void> run_parallel_for(engine& eng, parallel_for_stmt* stmt,
                                      std::vector<script_value>& containers,
                                      const std::shared_ptr<environment>& capture_env) {
	parallel_engine_state& state = eng.parallel_state();
	auto raise = [&state](std::error_code code, std::string message) {
		state.error_text = std::move(message);
		return checked_result<void>(code, std::string_view(state.error_text));
	};
	auto usage = [&raise](std::string message) {
		return raise(make_error_code(runtime_error_code::unsupported_operation), std::move(message));
	};
	if (state.region_running) {
		return usage("parallel_for: nested parallel regions are not supported (v0)");
	}
	const script_value& array_value = containers.front().is_reference() ? containers.front().deref() : containers.front();
	if (!array_value.is_array()) {
		return usage("parallel_for: can only iterate an array (v1)");
	}

	// The shared body function (admission + pool key + per-worker provisioning source). Hot
	// reload reparses into a fresh statement node, so this cache cannot go stale. Built once
	// per statement together with the body's control-flow mode.
	if (!stmt->body_fn_cache) {
		body_control_scan scan;
		scan_parallel_body_control(stmt->body, 0, 0, scan);
		if (scan.bad_break) {
			return usage("parallel_for: 'break' is not allowed in a parallel body (iterations are independent; use 'continue' for the next element) at " +
			             std::to_string(scan.bad_break->location.line) + ":" + std::to_string(scan.bad_break->location.column));
		}
		stmt->chunk_mode = scan.nested_return ? 2 : 1;
		rewrite_parallel_body_control(stmt->body, 0, stmt->chunk_mode == 1);
		auto body_block = std::dynamic_pointer_cast<block_stmt>(stmt->body);
		if (!body_block) {
			return usage("parallel_for: body is not a block (internal)");
		}
		std::vector<parameter> loop_params{ stmt->loop_param };
		for (const auto& extra : stmt->extra_params) { loop_params.push_back(extra); }
		stmt->body_fn_cache = std::make_shared<script_defined_function>(
			std::string_view("<parallel_for>"), std::move(loop_params),
			type_info_ptr(nullptr), std::move(body_block), nullptr, stmt->local_count);
	}
	const std::shared_ptr<script_defined_function>& body_fn = stmt->body_fn_cache;

	struct running_guard {
		parallel_engine_state& s;
		running_guard(parallel_engine_state& state_ref) : s(state_ref) { s.region_running = true; }
		~running_guard() { s.region_running = false; }
	} guard(state);

	const parallel_admission* adm = admit_cached(eng, state, body_fn, /*inplace_root*/ true);
	if (!adm->admitted) {
		return raise(adm->code, adm->message);
	}
	const std::shared_ptr<function_decl>& chunk_wrapper = build_chunk_wrapper(eng, stmt, *adm);

	region_plan plan;
	plan.prefix = "parallel_for";
	plan.containers = &containers;
	plan.root_fn = body_fn;
	plan.adm = adm;
	plan.chunk_wrapper = &chunk_wrapper;
	plan.capture_env = capture_env;
	plan.inplace = true;
#ifdef JAISCRIPT_VM_PROFILE
	plan.prof = &state.prof_for;
#endif
	return run_region(eng, plan);
}

// Tier-1 raw read through a region borrow (see parallel_transform.hpp). Runs on worker
// threads: every message below is a static literal (checked_result never allocates) and
// NOTHING here interns against the frozen symbolizer or copies a shared handle -
// primitive elements mint plain values, anything heavier rides the silent deep clone.
bool parameter_alias_allows(const script_value& derefed) {
	switch (derefed.raw_storage_index()) {
	case script_value::TYPEID_ARRAY:
	case script_value::TYPEID_MAP:
		return true;
	case script_value::TYPEID_OBJECT: {
		const auto& holder = derefed.unchecked_object_handle();
		if (!holder || !holder->is_class_instance_wrapper || !holder->data) { return false; }
		const auto* inst = static_cast<const class_instance*>(holder->data.get());
		const class_definition* cls = inst->get_class_definition();
		return cls && cls->is_script_class() && !cls->nocopy();
	}
	default:
		return false;
	}
}

uint32_t prove_readonly_parameters(engine& eng, const script_defined_function& fn) {
	if (!fn.body) { return 0; }
	// Non-owning view: the builder pins call targets for its own lifetime only
	std::shared_ptr<script_defined_function> view(std::shared_ptr<void>{}, const_cast<script_defined_function*>(&fn));
	uint32_t mask = 0;
	const size_t n = std::min<size_t>(fn.parameters().size(), 32);
	for (size_t i = 0; i < n; ++i) {
		try {
			if (prove_readonly_parameter(eng, view, i)) { mask |= (1u << i); }
		} catch (...) {
			// an unresolvable callee or a frozen symbolizer means no proof, never an error
		}
	}
	return mask;
}

const script_string* parallel_borrow_string_source(const script_value& borrow, const script_value& index_raw) noexcept {
	const script_value& index = index_raw.deref();
	const auto* arr = borrow.parallel_borrow_array();
	if (!arr || !index.is_int()) { return nullptr; }
	const script_int i = index.unchecked_as_int();
	if (i < 0 || i >= static_cast<script_int>(arr->size())) { return nullptr; }
	if (arr->is_typed()) {
		if (arr->kind() != script_array::kind_t::str) { return nullptr; }
		return arr->strs()[static_cast<size_t>(i)].get();
	}
	const script_value& elem = arr->values()[static_cast<size_t>(i)];
	if (elem.raw_storage_index() != script_value::TYPEID_STRING) { return nullptr; }
	return elem.unchecked_string_handle().get();
}

checked_result<script_value> parallel_borrow_subscript_read(const script_value& borrow,
                                                            const script_value& index_raw,
                                                            engine* eng,
                                                            bool lvalue_write) {
	if (lvalue_write) {
		// The runtime write wall: admission catches the visible shapes with positions;
		// this is the chokepoint that makes the borrow sound regardless
		return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
			"cannot write enclosing state in a parallel body (captured containers are read-only)");
	}
	const script_value& index = index_raw.deref();
	if (const auto* arr = borrow.parallel_borrow_array()) {
		if (!index.is_int()) {
			return checked_result<script_value>(make_error_code(runtime_error_code::invalid_index_type),
				"Array index must be an integer");
		}
		const script_int i = index.unchecked_as_int();
		if (i < 0 || i >= static_cast<script_int>(arr->size())) {
			// Static text (no interned numbers): worker threads may not intern, and the
			// twins' numeric form would - the captured-array read names the condition
			return checked_result<script_value>(make_error_code(runtime_error_code::index_out_of_bounds),
				"Array index out of bounds reading a captured array in a parallel body");
		}
		if (arr->is_typed()) {
			if (arr->kind() == script_array::kind_t::str) {
				// the bytes, never the handle: the live string's count belongs to the main thread
				const auto& str = arr->strs()[static_cast<size_t>(i)];
				return str ? script_value(script_string(*str), eng) : script_value(script_string(), eng);
			}
			// raw buffer read -> worker-local mint, no script_value even exists to copy
			return arr->get(static_cast<size_t>(i), eng);
		}
		const script_value& elem = arr->values()[static_cast<size_t>(i)];
		switch (elem.raw_storage_index()) {
		case script_value::TYPEID_NULL:
		case script_value::TYPEID_INT:
		case script_value::TYPEID_FLOAT:
		case script_value::TYPEID_CHAR:
		case script_value::TYPEID_BOOL:
			return script_value(elem);   // raw const read -> worker-local mint, zero refcount traffic
		case script_value::TYPEID_STRING:
			return script_value(script_string(*elem.unchecked_string_handle()), eng);   // bytes, not the handle
		default:
			return elem.parallel_detached_copy();   // tier 3 (defensive: borrows are primitives and strings)
		}
	}
	if (const auto* map = borrow.parallel_borrow_map()) {
		auto it = map->find(index);
		if (it == map->end()) {
			return script_value(std::monostate{}, eng);   // read misses yield null (twin semantics)
		}
		const script_value& elem = it->second;
		switch (elem.raw_storage_index()) {
		case script_value::TYPEID_NULL:
		case script_value::TYPEID_INT:
		case script_value::TYPEID_FLOAT:
		case script_value::TYPEID_CHAR:
		case script_value::TYPEID_BOOL:
			return script_value(elem);
		case script_value::TYPEID_STRING:
			return script_value(script_string(*elem.unchecked_string_handle()), eng);   // bytes, not the handle
		default:
			return elem.parallel_detached_copy();
		}
	}
	return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
		"parallel borrow subscript on a non-container value (internal)");
}

} // namespace jai::detail
