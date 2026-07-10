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
//   the body needs (fresh strong_ptrs — invoking them bumps no shared refcount), and a
//   detached input slice (script_value::parallel_detached_copy — the value-semantic
//   tier's alias walk is exactly "clone everything", so exclusivity holds by
//   construction). Type shapes are pre-warmed and the symbolizer/type tables freeze.
//
//   REGION: static contiguous chunks (n/thread_count(), boundary-shifted by the optional
//   weight hint evaluated at the barrier), pinned pool workers + the calling thread,
//   disjoint writes into the pre-sized result. engine::execution_limits() resolves to
//   the calling worker's accounting through the installed region table. First error in
//   ITERATION order wins at the join; a terminal worker failure (budget) latches the
//   engine's terminal rail.

#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/parallel_transform.hpp>
#include <jaiscript/detail/interpreter_backend.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/vm/vm_backend.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

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
		std::unique_ptr<string_symbolizer> env_epoch_sink;
		std::shared_ptr<environment> root_env;
		execution_limits limits;
		std::unique_ptr<execution_backend> backend;
		vm::vm_backend* vm = nullptr;                     // set when the engine backend is the vm
		interpreter_backend* interp = nullptr;            // set otherwise
		script_callable fn_payload;
		std::vector<script_value> input;
		// Raw in-place input (Dev ruling 2026-07-09 "safe AND free"): when the source's
		// elements are all-primitive (typed proof or one dynamic scan), workers read the
		// caller's vector directly - zero per-element detach copies, zero refcount
		// traffic (primitive copies touch no counts). Non-null only DURING a region
		// (points into the run call's locals; the join guard nulls it).
		const std::vector<script_value>* raw_input = nullptr;
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

		// Provisioning fingerprint: reuse is valid only while these match the current
		// call. Script bodies are identity-compared (hot reload swaps bodies -> rebuild);
		// host copies are refreshed on every reuse instead of fingerprinted.
		bool fingerprint_use_vm = false;
		const void* fingerprint_fn_body = nullptr;
		std::vector<std::pair<uint64_t, const void*>> fingerprint_script_bodies;
		std::vector<std::pair<uint64_t, std::string>> host_functions;   // refreshed each reuse
	};

	parallel_engine_state::parallel_engine_state() = default;
	parallel_engine_state::~parallel_engine_state() = default;

namespace {

	constexpr size_t k_parallel_small_n = 16;   // below this, one context on the calling thread

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
		// parallel_for bodies (in-place root mode): member access and member STORES are
		// admitted when the chain roots at a LOCAL identifier - the owned element
		// (chunk-exclusive) or a worker-private local. Enclosing-rooted member access
		// stays rejected (capture classification cannot provision objects). Applies to
		// the whole call graph: helper functions do member work on their own params.
		bool inplace_root = false;
		const char* prefix = "parallel_transform";

		admission_builder(engine& e) : eng(e), sym(*e.get_symbolizer()) { adm.admitted = false; }

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

		// In-place mode member access (read or store position): admitted iff the chain
		// roots at a LOCAL identifier - the owned element or a worker-private local.
		// Index expressions inside the chain walk under the normal rules. Every member
		// node's interned ids (member/getter/setter) warm HERE, single-threaded: worker
		// field access probes them against the frozen symbolizer.
		bool walk_local_member_chain(const member_expr* m, const char* what) {
			std::vector<const expression_ptr*> indexes;
			const expression* cur = m;
			identifier_expr* root = nullptr;
			while (cur) {
				if (cur->get_type() == node_type::identifier_expr) {
					root = const_cast<identifier_expr*>(static_cast<const identifier_expr*>(cur));
					break;
				}
				if (cur->get_type() == node_type::member_expr) {
					auto* node = const_cast<member_expr*>(static_cast<const member_expr*>(cur));
					if (node->member_id == UINT64_MAX) { node->member_id = sym.intern(node->member); }
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
			if (!root) {
				return fail(m, std::string(what) + " must root at a local variable in a parallel body");
			}
			if (!is_local(root->symbol_id)) {
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
					return is_local(ident->symbol_id) ? nullptr : ident;
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
				if (fn->parameters().size() != 1) {
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
				if (is_local(n->symbol_id)) { return true; }
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
				}
				return walk_expr(n->left) && walk_expr(n->right);
			}
			case node_type::unary_expr: {
				auto* n = static_cast<unary_expr*>(e.get());
				if (n->op.type == token_type::plus_plus || n->op.type == token_type::minus_minus) {
					return walk_target(n->operand, "increment/decrement");
				}
				return walk_expr(n->operand);
			}
			case node_type::assignment_expr: {
				auto* n = static_cast<assignment_expr*>(e.get());
				return walk_target(n->target, "assignment") && walk_expr(n->value);
			}
			case node_type::call_expr:
				return walk_call(static_cast<call_expr*>(e.get()));
			case node_type::member_expr:
				if (inplace_root) {
					return walk_local_member_chain(static_cast<member_expr*>(e.get()), "member access");
				}
				return fail(e.get(), "member access is not allowed in a parallel body (v0)");
			case node_type::lambda_expr:
				return fail(e.get(), "lambdas are not allowed in a parallel body (v0)");
			case node_type::new_expr:
				return fail(e.get(), "'new' is not allowed in a parallel body");
			case node_type::this_expr:
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
				if (is_local(n->symbol_id)) { return true; }
				if (aliasing) {
					return fail(n, std::string(what) + " would alias enclosing state '" + std::string(n->name) +
					               "' (captured reads are read-only)");
				}
				return fail(n, "body writes enclosing state '" + std::string(n->name) + "'");
			}
			case node_type::binary_expr: {
				auto* n = static_cast<binary_expr*>(t.get());
				if (n->op.type == token_type::left_bracket) {
					return walk_target(n->left, what, aliasing) && walk_expr(n->right);
				}
				return fail(t.get(), std::string(what) + " target is not allowed in a parallel body");
			}
			case node_type::member_expr:
				if (inplace_root && !aliasing) {
					// Own-element / worker-local member store (the parallel_for output
					// channel); enclosing-rooted chains still fail inside
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
				} else if (!walk_expr(m->object)) {
					return false;
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

	parallel_admission build_admission(engine& eng, const std::shared_ptr<script_defined_function>& fn,
	                                   bool inplace_root = false) {
		admission_builder builder(eng);
		if (inplace_root) {
			builder.inplace_root = true;
			builder.prefix = "parallel_for";
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


	// Worker-side value-semantic check for fn results (read-only const walk - no handle
	// copies, so it never touches a refcount). Also detects results that reach into the
	// worker's provisioned captures (snapshot nodes / a live borrow): the caller
	// materializes those through the silent deep clone so nothing worker-owned or
	// worker-count-dependent escapes the region.
	bool result_value_semantic(const script_value& raw, const char** what,
	                           const std::unordered_set<const void*>* worker_nodes = nullptr,
	                           bool* aliases_worker_state = nullptr) {
		const script_value& v = raw.is_reference() ? raw.deref() : raw;
		switch (v.raw_storage_index()) {
		case script_value::TYPEID_NULL:
		case script_value::TYPEID_INT:
		case script_value::TYPEID_FLOAT:
		case script_value::TYPEID_CHAR:
		case script_value::TYPEID_BOOL:
			return true;
		case script_value::TYPEID_STRING:
			if (worker_nodes && aliases_worker_state && worker_nodes->count(&v.unchecked_as_string())) {
				*aliases_worker_state = true;
			}
			return true;
		case script_value::TYPEID_CPP_BOUND:
			// ONE cached semantic-index read (repeated is_* probes each re-deref)
			if (v.bound_semantic_index() != script_value::TYPEID_NULL || v.is_null()) {
				return true;   // bound primitive - decodes to a value-semantic read
			}
			*what = "bound host object";
			return false;
		case script_value::TYPEID_PARALLEL_BORROW:
			// A borrow escaping into the result: legal shape, but it must materialize
			if (aliases_worker_state) { *aliases_worker_state = true; }
			return true;
		case script_value::TYPEID_ARRAY: {
			if (worker_nodes && aliases_worker_state && worker_nodes->count(v.unchecked_array_node())) {
				*aliases_worker_state = true;
			}
			for (const auto& elem : v.unchecked_as_array()) {
				if (!result_value_semantic(elem, what, worker_nodes, aliases_worker_state)) { return false; }
			}
			return true;
		}
		case script_value::TYPEID_MAP: {
			if (worker_nodes && aliases_worker_state && worker_nodes->count(&v.unchecked_as_map())) {
				*aliases_worker_state = true;
			}
			for (const auto& [key, val] : v.unchecked_as_map()) {
				if (!result_value_semantic(key, what, worker_nodes, aliases_worker_state) ||
				    !result_value_semantic(val, what, worker_nodes, aliases_worker_state)) { return false; }
			}
			return true;
		}
		case script_value::TYPEID_OBJECT: *what = "object"; return false;
		case script_value::TYPEID_FUNCTION: *what = "function"; return false;
		case script_value::TYPEID_SHARED_PTR: *what = "shared_ptr"; return false;
		case script_value::TYPEID_WEAK_PTR: *what = "weak_ptr"; return false;
		default: *what = "unsupported type"; return false;
		}
	}

	// Re-slice the worker's memory allowance from the CURRENT outer accounting
	void slice_worker_limits(engine& eng, parallel_worker_slot& slot) {
		const auto& outer = eng.execution_limits();
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
		slot.input.clear();
	}

	std::unique_ptr<parallel_worker_slot> provision_worker(engine& eng, const parallel_admission& adm,
	                                                 std::chrono::nanoseconds budget,
	                                                 bool use_vm) {
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
			auto copy = std::make_shared<script_defined_function>(
				source_fn->name, std::vector<parameter>(source_fn->parameters()), source_fn->return_type,
				source_fn->body, ctx->root_env, source_fn->local_count);
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
		return ctx;
	}

	// In-place worker loop (parallel_for): by-ref bodies bind each iteration to the
	// pre-minted element reference - the body mutates the caller's element directly
	// under chunk exclusivity. Plain-auto bodies read the element through raw_input
	// (the by-value binding clones per value semantics - zero side effects). No
	// output, results discarded. Every refcount touched is element-internal
	// (normalization made elements exclusive) or worker-private: single-threaded.
	void run_worker_inplace(parallel_worker_slot& ctx, engine& eng) {
		std::vector<script_value> call_args;
		call_args.emplace_back(std::monostate{}, &eng);
		size_t i = ctx.begin;
		try {
			for (; i < ctx.end; ++i) {
				call_args[0] = ctx.raw_input ? (*ctx.raw_input)[i] : ctx.element_refs[i - ctx.begin];
				auto r = ctx.backend->execute_callable(ctx.fn_payload, call_args);
				if (ctx.backend->is_unwinding()) {
					ctx.error = parallel_worker_error{ i,
						make_error_code(runtime_error_code::evaluation_failed),
						ctx.backend->get_current_exception().what(),
						ctx.limits.terminal_error };
					return;
				}
				if (!r) {
					ctx.error = parallel_worker_error{ i, r.error(),
						format_error(r, *eng.get_symbolizer()), ctx.limits.terminal_error };
					return;
				}
			}
		} catch (const std::exception& e) {
			ctx.error = parallel_worker_error{ i, make_error_code(runtime_error_code::cpp_exception),
				e.what(), ctx.limits.terminal_error };
		}
	}

	void run_worker(parallel_worker_slot& ctx, std::vector<script_value>& out, engine& eng) {
		std::vector<script_value> call_args;
		call_args.emplace_back(std::monostate{}, &eng);
		size_t i = ctx.begin;
		try {
			for (; i < ctx.end; ++i) {
				if (ctx.raw_input) {
					// All-primitive source: plain copy from the caller's vector is the
					// worker-local mint (no refcounts anywhere in a primitive value)
					call_args[0] = (*ctx.raw_input)[i];
				} else {
					call_args[0] = std::move(ctx.input[i - ctx.begin]);
				}
				auto r = ctx.backend->execute_callable(ctx.fn_payload, call_args);
				if (ctx.backend->is_unwinding()) {
					// Uncaught script throw: surfaces as backend unwinding state, not a
					// checked_result failure. The thrown value flattens to its message
					// text across the join (v0).
					ctx.error = parallel_worker_error{ i,
						make_error_code(runtime_error_code::evaluation_failed),
						ctx.backend->get_current_exception().what(),
						ctx.limits.terminal_error };
					return;
				}
				if (!r) {
					ctx.error = parallel_worker_error{ i, r.error(),
						format_error(r, *eng.get_symbolizer()), ctx.limits.terminal_error };
					return;
				}
				const char* what = nullptr;
				bool aliases_worker_state = false;
				if (!result_value_semantic(r.value(), &what,
				                           ctx.provisioned_nodes.empty() ? nullptr : &ctx.provisioned_nodes,
				                           &aliases_worker_state)) {
					ctx.error = parallel_worker_error{ i,
						make_error_code(runtime_error_code::unsupported_operation),
						std::string("parallel_transform: fn returned a non-value-semantic result (") + what +
							") for element " + std::to_string(i),
						ctx.limits.terminal_error };
					return;
				}
				// A result reaching into this worker's captures (snapshot nodes or a
				// borrow) materializes via the silent deep clone: iteration results must
				// never share structure in a worker-count-dependent way
				out[i] = aliases_worker_state ? r.value().parallel_detached_copy() : std::move(r).value();
			}
		} catch (const std::exception& e) {
			ctx.error = parallel_worker_error{ i, make_error_code(runtime_error_code::cpp_exception),
				e.what(), ctx.limits.terminal_error };
		}
	}

	// ============================== BARRIER HELPERS ==============================

	void prewarm_region_types(engine& eng, const std::vector<type_info*>& seed) {
		(void)eng.symbolize("pair");   // map range-for pair name (hit for stdlib engines)
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
		while (!pending.empty()) {
			type_info* t = pending.back();
			pending.pop_back();
			if (!t || !seen.insert(t).second) { continue; }
			eng.get_type_info_reference(t);
			if (type_info* e = t->element_type().get()) { pending.push_back(e); }
			if (type_info* k = t->key_type().get()) { pending.push_back(k); }
			if (type_info* v = t->value_type().get()) { pending.push_back(v); }
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
	bool normalize_element_inplace(script_value& v, engine& eng) {
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
			}
			return true;
		}
		case script_value::TYPEID_ARRAY: {
			if (statically_all_primitive(v)) { return true; }
			auto& handle = v.unchecked_get_array_storage();
			if (!handle) { return true; }
			if (handle.use_count() > 1) {
				v = v.parallel_detached_copy();
				return normalize_element_inplace(v, eng);
			}
			if (handle->is_typed()) { return true; }   // raw buffer: all-primitive by construction
			for (auto& elem : handle->values()) {
				if (!normalize_element_inplace(elem, eng)) { return false; }
			}
			return true;
		}
		case script_value::TYPEID_MAP: {
			if (statically_all_primitive(v)) { return true; }
			auto& handle = v.unchecked_get_map_storage();
			if (!handle) { return true; }
			if (handle.use_count() > 1) {
				v = v.parallel_detached_copy();
				return normalize_element_inplace(v, eng);
			}
			// Keys are const in the map node; a shared KEY string forces a whole-map
			// detach (fresh keys). Values normalize in place.
			bool shared_key = false;
			for (const auto& [key, val] : *handle) {
				(void)val;
				if (key.raw_storage_index() == script_value::TYPEID_STRING &&
				    const_cast<script_value&>(key).unchecked_get_string_storage().use_count() > 1) {
					shared_key = true;
					break;
				}
				if (key.raw_storage_index() == script_value::TYPEID_ARRAY ||
				    key.raw_storage_index() == script_value::TYPEID_MAP ||
				    key.raw_storage_index() == script_value::TYPEID_OBJECT) {
					shared_key = true;   // structured keys: detach wholesale, then values re-walk
					break;
				}
			}
			if (shared_key) {
				v = v.parallel_detached_copy();
				return normalize_element_inplace(v, eng);
			}
			for (auto& [key, val] : *handle) {
				(void)key;
				if (!normalize_element_inplace(val, eng)) { return false; }
			}
			return true;
		}
		case script_value::TYPEID_OBJECT: {
			auto& handle = v.unchecked_get_object_storage();
			if (!handle || !handle->is_class_instance_wrapper || !handle->data) {
				return false;   // bound host object wrapper
			}
			auto* inst = static_cast<class_instance*>(handle->data.get());
			const class_definition* cd = inst->get_class_definition();
			if (!cd || !cd->flat_value_semantics()) {
				return false;
			}
			if (handle.use_count() > 1 || handle->data.use_count() > 1) {
				v = v.clone();   // engine-internal aliasing: fresh exclusive instance
				auto& fresh = v.unchecked_get_object_storage();
				inst = static_cast<class_instance*>(fresh->data.get());
			}
			for (auto& [field_id, field] : inst->get_fields_mutable()) {
				(void)field_id;
				if (!normalize_element_inplace(field, eng)) { return false; }
			}
			return true;
		}
		default:
			return false;
		}
	}

	// Borrow admission (content half): TRUE when every element of the container is a
	// plain primitive, i.e. nothing reachable carries a refcount - element reads through
	// the borrow are then raw const reads minting worker-local values, safe against the
	// non-atomic strong counts by construction. Const walk, no handle copies.
	bool parallel_content_all_primitive(const script_value& v) {
		switch (v.raw_storage_index()) {
		case script_value::TYPEID_ARRAY: {
			for (const auto& elem : v.unchecked_as_array()) {
				switch (elem.raw_storage_index()) {
				case script_value::TYPEID_NULL:
				case script_value::TYPEID_INT:
				case script_value::TYPEID_FLOAT:
				case script_value::TYPEID_CHAR:
				case script_value::TYPEID_BOOL:
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
					       idx == script_value::TYPEID_BOOL;
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

} // namespace

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

	// ADMISSION (contract A: violations error, never silent-serial)
	const parallel_admission* adm = admit_cached(eng, state, fn_payload->fn);
	if (!adm->admitted) {
		return raise(adm->code, adm->message);
	}
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

	// CAPTURED READS: resolve every touched enclosing name against the LIVE global
	// environment and classify how it provisions (parallel_capture_kind). The borrow
	// tier NEVER shares a refcount with a worker: the source handle held here is the
	// region anchor (counts stationary - the write wall stops every decrement, this
	// anchor plus the global cell outlive the region) and workers only ever copy the
	// raw borrow view. Everything else is copied INTO each worker at this barrier
	// (single-threaded) - direct cross-thread reads of shared containers are never
	// legal, even read-only ones: non-atomic strong_ptr counts make an element handle
	// copy a data race.
	std::vector<resolved_capture> captures;
	state.last_captures.clear();
	captures.reserve(adm->captures.size());
	for (const auto& entry : adm->captures) {
		auto global = eng.get_global_environment()->get(entry.name_id);
		if (!global) {
			return usage("parallel_transform: captured name '" + std::string(entry.name) + "' is not defined at the region barrier");
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
			// ONE cached semantic-index read, then compare (never repeated is_* probes)
			const size_t sem = cap.source->bound_semantic_index();
			if (sem == script_value::TYPEID_NULL && !cap.source->is_null()) {
				return usage("parallel_transform: captured name '" + std::string(entry.name) + "' is not value-semantic (bound host object)");
			}
			// Decode ONCE at the barrier (clone detaches a bound primitive/string):
			// every worker sees the same barrier-time value
			cap.source = cap.source->clone();
			cap.kind = sem == script_value::TYPEID_STRING
				? parallel_capture_kind::string : parallel_capture_kind::scalar;
			break;
		}
		case script_value::TYPEID_STRING:
			cap.kind = parallel_capture_kind::string;
			break;
		case script_value::TYPEID_ARRAY:
		case script_value::TYPEID_MAP:
			// Typed containers prove all-primitive statically (no O(n) scan per region)
			cap.kind = (entry.borrow_eligible &&
			            (statically_all_primitive(*cap.source) || parallel_content_all_primitive(*cap.source)))
				? parallel_capture_kind::borrow : parallel_capture_kind::snapshot;
			break;
		case script_value::TYPEID_FUNCTION:
			return usage("parallel_transform: captured name '" + std::string(entry.name) +
			             "' is a function value (only direct calls to statically-known functions are allowed in a parallel body)");
		default:
			return usage("parallel_transform: captured name '" + std::string(entry.name) + "' is not value-semantic (object/shared_ptr/weak_ptr)");
		}
		state.last_captures.emplace_back(entry.name_id, cap.kind);
		captures.push_back(std::move(cap));
	}

	// STAGE-2a BRIDGE (typed_array_design.md): typed-node input demotes in place at the
	// barrier (main thread, node identity preserved) so the hetero kernel stays correct;
	// stage 3 reads typed buffers directly.
	if (const script_array* input_node = array_value.unchecked_array_node(); input_node->is_typed()) {
		const_cast<script_array*>(input_node)->demote_to_hetero(&eng);
	}

	const auto& source = array_value.as_array();
	const size_t n = source.size();
	std::vector<script_value> out(n, script_value(std::monostate{}, &eng));

	size_t worker_count = eng.parallel_thread_count();
	if (worker_count < 1) { worker_count = 1; }
	if (worker_count > n) { worker_count = n ? n : 1; }
	if (n < k_parallel_small_n) { worker_count = 1; }   // the ruled small-n knob: same
	                                                    // semantics, one context, no fan-out

	// Optional weight hint: evaluated per element at the barrier, single-threaded, on the
	// engine's own backend (ruled §11 Q1). Chunk boundaries equalize cumulative weight.
	std::vector<size_t> bounds;
	if (weight_payload && worker_count > 1) {
		std::vector<double> weights(n, 0.0);
		engine::external_call_guard call_guard(&eng);
		std::vector<script_value> weight_args;
		weight_args.emplace_back(std::monostate{}, &eng);
		for (size_t i = 0; i < n; ++i) {
			weight_args[0] = source[i];
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
				return usage("parallel_transform: weight_fn returned a negative or non-finite weight (element " +
					std::to_string(i) + ")");
			}
			weights[i] = w;
		}
		bounds = partition_weighted(weights, worker_count);
	} else {
		bounds = partition_flat(n, worker_count);
	}
	state.last_chunk_bounds = bounds;

	// BARRIER: build the per-worker contexts, detach input slices, pre-warm type shapes.
	const std::chrono::nanoseconds budget =
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(eng.execution_budget()));
	const backend_type engine_backend = eng.get_backend_type();
	if (engine_backend == backend_type::custom) {
		return usage("parallel_transform: custom execution backends are not supported");
	}
	const bool use_vm = engine_backend == backend_type::vm;

	// Select this REGION's persistent slot pool by fingerprint (fn body + backend):
	// distinct regions cycling in one frame each keep their own provisioned contexts.
	// LRU-evict beyond the cap; pool selection happens before any slot reference is
	// taken (the pools vector must not reallocate for the rest of this call).
	const void* pool_key = static_cast<const void*>(fn_payload->fn->body.get());
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

	// Acquire the first worker_count persistent slots: RESET matching ones (limits
	// re-slice + budget re-arm + residual-state clear + host refresh), rebuild the rest.
	std::vector<type_info*> value_types;
	if (pool->worker_slots.size() < worker_count) {
		pool->worker_slots.resize(worker_count);
	}
	std::vector<parallel_worker_slot*> contexts;
	contexts.reserve(worker_count);

	// Whatever way this call exits, no capture define survives it: a dormant slot must
	// never hold a borrow's raw pointer (the viewed container may die with its global),
	// and snapshots release with the region rather than idling in slot envs.
	struct capture_define_guard {
		engine& eng;
		std::vector<std::unique_ptr<parallel_worker_slot>>& slots;
		size_t count;
		~capture_define_guard() {
			for (size_t k = 0; k < count && k < slots.size(); ++k) {
				parallel_worker_slot* slot = slots[k].get();
				if (!slot) { continue; }
				for (uint64_t id : slot->capture_name_ids) {
					slot->root_env->define(id, script_value(std::monostate{}, &eng));
				}
				slot->capture_name_ids.clear();
				slot->provisioned_nodes.clear();
				slot->raw_input = nullptr;   // points into this call's locals - never dormant
			}
		}
	} capture_guard{ eng, pool->worker_slots, worker_count };

	// Raw in-place input (Dev ruling: safe AND free): all-primitive sources are read
	// directly from the caller's vector by every worker - no per-element detach. Typed
	// primitive containers prove it statically (zero scans); untyped pay ONE content
	// scan (vs N detach constructions). The array value is anchored by this call for
	// the whole region and workers never touch its structure (elements arrive by value
	// through the body parameter; the container itself is not visible to the body
	// unless separately captured, where borrow/snapshot rules apply).
	const bool raw_input_ok = statically_all_primitive(array_value) ||
	                          parallel_content_all_primitive(array_value);
	if (raw_input_ok) {
		// The detach loop used to feed per-element types into the prewarm; the raw path
		// only needs the container's own shape (primitives are always pre-interned)
		if (type_info* t = array_value.get_type_info().get()) { value_types.push_back(t); }
	}

	for (size_t k = 0; k < worker_count; ++k) {
		auto& slot = pool->worker_slots[k];
		if (slot && slot_matches(*slot, *adm, *fn_payload->fn, use_vm)) {
			reset_worker_slot(eng, *slot, budget);
		} else {
			slot = provision_worker(eng, *adm, budget, use_vm);
		}
		slot->begin = bounds[k];
		slot->end = bounds[k + 1];
		slot->input.clear();
		if (raw_input_ok) {
			slot->raw_input = &source;
		} else {
			slot->raw_input = nullptr;
			slot->input.reserve(slot->end - slot->begin);
			for (size_t i = slot->begin; i < slot->end; ++i) {
				try {
					slot->input.push_back(source[i].parallel_detached_copy(&value_types));
				} catch (const std::exception& e) {
					return usage("parallel_transform: element " + std::to_string(i) + ": " + e.what());
				}
			}
		}
		// Provision the captured reads into THIS worker's root env (single-threaded,
		// charged to the enclosing execute): scalars copy, strings/snapshots detach
		// (memory_cap-charged inside parallel_detached_copy), borrows mint the
		// zero-copy view. Defines are per call - content may have changed since the
		// last region - and are nulled again at the join.
		slot->capture_name_ids.clear();
		slot->provisioned_nodes.clear();
		std::vector<const void*> capture_nodes;
		for (const auto& cap : captures) {
			script_value provisioned(std::monostate{}, &eng);
			try {
				switch (cap.kind) {
				case parallel_capture_kind::scalar:
					provisioned = *cap.source;
					break;
				case parallel_capture_kind::string:
				case parallel_capture_kind::snapshot:
					capture_nodes.clear();
					provisioned = cap.source->parallel_detached_copy(&value_types, &capture_nodes);
					slot->provisioned_nodes.insert(capture_nodes.begin(), capture_nodes.end());
					break;
				case parallel_capture_kind::borrow:
					if (type_info* t = cap.source->get_type_info().get()) { value_types.push_back(t); }
					provisioned = script_value::make_parallel_borrow(*cap.source, &eng);
					break;
				}
			} catch (const std::exception& e) {
				return usage("parallel_transform: captured name '" + std::string(cap.entry->name) + "': " + e.what());
			}
			slot->root_env->define(cap.entry->name_id, std::move(provisioned));
			slot->capture_name_ids.push_back(cap.entry->name_id);
		}
		contexts.push_back(slot.get());
	}
	prewarm_region_types(eng, [&] {
		std::vector<type_info*> seed = adm->referenced_types;
		if (weight_adm) { seed.insert(seed.end(), weight_adm->referenced_types.begin(), weight_adm->referenced_types.end()); }
		seed.insert(seed.end(), value_types.begin(), value_types.end());
		return seed;
	}());

	// REGION: pinned pool workers for chunks 1..W-1 (submit_to fixes each chunk's thread
	// so the limits table can be built before fan-out), the calling thread runs chunk 0.
	parallel_region_table table;
	table.entries.push_back({ std::this_thread::get_id(), &contexts[0]->limits });
	if (worker_count > 1) {
		const size_t pool_workers_needed = worker_count - 1;
		if (!state.pool || state.pool->worker_count() < pool_workers_needed) {
			state.pool = std::make_unique<jai::thread_pool>(
				std::max(pool_workers_needed, jai::thread_pool::default_worker_count()));
		}
		for (size_t k = 1; k < worker_count; ++k) {
			table.entries.push_back({ state.pool->worker_thread_id(k - 1), &contexts[k]->limits });
		}
	}

	eng.get_symbolizer()->set_frozen(true);
	state.active_region = &table;
	for (size_t k = 1; k < worker_count; ++k) {
		parallel_worker_slot* ctx = contexts[k];
		std::vector<script_value>* out_ptr = &out;
		engine* eng_ptr = &eng;
		state.pool->submit_to(k - 1, [ctx, out_ptr, eng_ptr] { run_worker(*ctx, *out_ptr, *eng_ptr); });
	}
	run_worker(*contexts[0], out, eng);
	if (worker_count > 1) {
		state.pool->wait_idle();
	}
	state.active_region = nullptr;
	eng.get_symbolizer()->set_frozen(false);

	// JOIN: roll worker accounting up into the enclosing execute, then the first error in
	// ITERATION order wins (deterministic under static chunks).
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
		return raise(code, winner->message.empty() ? std::string("parallel_transform: worker failed")
		                                           : winner->message);
	}
	script_value result = script_value::make_array(nullptr, &eng);
	result.as_array() = std::move(out);
	return result;
}

// parallel_for (see parallel_transform.hpp): the in-place fork-join statement. Shares
// the transform machinery wholesale - admission cache (in-place root mode), region
// pools, worker slots, capture provisioning, the join rules - and differs at exactly
// three points: captured reads resolve through the STATEMENT's environment chain (not
// just globals), the barrier proves elements in-place-mutation-safe and pre-mints one
// element reference per iteration (single-threaded; holders pin the live container),
// and workers run the by-ref body over their chunk with no output array.
checked_result<void> run_parallel_for(engine& eng, parallel_for_stmt* stmt,
                                      const script_value& container,
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
	const script_value& array_value = container.is_reference() ? container.deref() : container;
	if (!array_value.is_array()) {
		return usage("parallel_for: can only iterate an array (v1)");
	}

	// The shared body function (admission + pool key + per-worker provisioning source).
	// Hot reload reparses into a fresh statement node, so this cache cannot go stale.
	if (!stmt->body_fn_cache) {
		auto body_block = std::dynamic_pointer_cast<block_stmt>(stmt->body);
		if (!body_block) {
			return usage("parallel_for: body is not a block (internal)");
		}
		stmt->body_fn_cache = std::make_shared<script_defined_function>(
			std::string_view("<parallel_for>"), std::vector<parameter>{ stmt->loop_param },
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

	// CAPTURED READS resolve through the statement's enclosing environment chain (its
	// walk reaches the global env; slot-resident function locals are not env-visible
	// and correctly error as undefined). A BORROW of the very container being mutated
	// would read racing content (borrow indexes any element, writes own chunks only) -
	// contract A errors instead of silently demoting; snapshots of it stay legal
	// (barrier-frozen copy, deterministic).
	const script_array* source_node = array_value.unchecked_array_node();
	std::vector<resolved_capture> captures;
	state.last_captures.clear();
	captures.reserve(adm->captures.size());
	for (const auto& entry : adm->captures) {
		auto global = capture_env ? capture_env->get(entry.name_id)
		                          : eng.get_global_environment()->get(entry.name_id);
		if (!global) {
			return usage("parallel_for: captured name '" + std::string(entry.name) + "' is not defined at the region barrier");
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
				return usage("parallel_for: captured name '" + std::string(entry.name) + "' is not value-semantic (bound host object)");
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
			const bool same_container = cap.source->raw_storage_index() == script_value::TYPEID_ARRAY &&
			                            cap.source->unchecked_array_node() == source_node;
			const bool borrowable = entry.borrow_eligible && !same_container &&
			                        (statically_all_primitive(*cap.source) || parallel_content_all_primitive(*cap.source));
			if (same_container && entry.borrow_eligible) {
				return usage("parallel_for: captured name '" + std::string(entry.name) +
				             "' is the container being mutated (reads through it would race the element writes)");
			}
			cap.kind = borrowable ? parallel_capture_kind::borrow : parallel_capture_kind::snapshot;
			break;
		}
		case script_value::TYPEID_FUNCTION:
			return usage("parallel_for: captured name '" + std::string(entry.name) +
			             "' is a function value (only direct calls to statically-known functions are allowed in a parallel body)");
		default:
			return usage("parallel_for: captured name '" + std::string(entry.name) + "' is not value-semantic (object/shared_ptr/weak_ptr)");
		}
		state.last_captures.emplace_back(entry.name_id, cap.kind);
		captures.push_back(std::move(cap));
	}

	// STAGE-2a BRIDGE (typed_array_design.md): a typed-node source demotes IN PLACE at
	// the barrier (main thread, node identity preserved) so the hetero kernel below
	// stays correct. Stage 3 replaces this with raw typed-buffer worker reads/writes —
	// the demotion is the correctness bridge, not the destination.
	if (source_node->is_typed()) {
		const_cast<script_array*>(source_node)->demote_to_hetero(&eng);
	}

	// ELEMENT PROOF + NORMALIZATION: every element must end this pass exclusively-
	// mutable (shared strings detach in place - see normalize_element_inplace). Typed
	// all-primitive containers prove statically; engine::allow_unsafe_parallel skips
	// the walk entirely (trusted scripts).
	auto& source = const_cast<script_array*>(source_node)->values();
	const size_t n = source.size();
	if (!eng.allow_unsafe_parallel() && !statically_all_primitive(array_value)) {
		for (size_t i = 0; i < n; ++i) {
			bool ok = false;
			try {
				ok = normalize_element_inplace(source[i], eng);
			} catch (const std::exception& e) {
				return usage("parallel_for: element " + std::to_string(i) + ": " + e.what());
			}
			if (!ok) {
				return usage("parallel_for: element " + std::to_string(i) +
				             " cannot be mutated in place (only value-semantic shapes - primitives, strings,"
				             " value containers, flat value-class instances - are provably exclusive;"
				             " engine::allow_unsafe_parallel(true) overrides)");
			}
		}
	}
	if (n == 0) {
		return {};
	}

	size_t worker_count = eng.parallel_thread_count();
	if (worker_count < 1) { worker_count = 1; }
	if (worker_count > n) { worker_count = n; }
	if (n < k_parallel_small_n) { worker_count = 1; }
	std::vector<size_t> bounds = partition_flat(n, worker_count);
	state.last_chunk_bounds = bounds;

	const std::chrono::nanoseconds budget =
		std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(eng.execution_budget()));
	const backend_type engine_backend = eng.get_backend_type();
	if (engine_backend == backend_type::custom) {
		return usage("parallel_for: custom execution backends are not supported");
	}
	const bool use_vm = engine_backend == backend_type::vm;

	// Region pool by fingerprint (body + backend), LRU-evicted - same policy as transform
	const void* pool_key = static_cast<const void*>(body_fn->body.get());
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

	std::vector<type_info*> value_types;
	if (pool->worker_slots.size() < worker_count) {
		pool->worker_slots.resize(worker_count);
	}
	std::vector<parallel_worker_slot*> contexts;
	contexts.reserve(worker_count);

	// Join guard: no capture define and no element reference survives this call - the
	// holders pin the caller's array, and a dormant slot must never do that.
	struct inplace_join_guard {
		engine& eng;
		std::vector<std::unique_ptr<parallel_worker_slot>>& slots;
		size_t count;
		~inplace_join_guard() {
			for (size_t k = 0; k < count && k < slots.size(); ++k) {
				parallel_worker_slot* slot = slots[k].get();
				if (!slot) { continue; }
				for (uint64_t id : slot->capture_name_ids) {
					slot->root_env->define(id, script_value(std::monostate{}, &eng));
				}
				slot->capture_name_ids.clear();
				slot->provisioned_nodes.clear();
				slot->element_refs.clear();
				slot->raw_input = nullptr;
			}
		}
	} join_guard{ eng, pool->worker_slots, worker_count };

	// The container handle for element references: holders re-deref container+index on
	// every access (reallocation-safe), and each mint here bumps the handle count once,
	// single-threaded, released by the guard above.
	const bool by_ref_element = stmt->loop_param.is_reference;
	auto& array_handle = const_cast<script_value&>(array_value).unchecked_get_array_storage();
	type_info_ptr element_type = array_value.get_type_info().get() ? array_value.get_type_info()->element_type() : type_info_ptr(nullptr);
	if (type_info* t = array_value.get_type_info().get()) { value_types.push_back(t); }

	for (size_t k = 0; k < worker_count; ++k) {
		auto& slot = pool->worker_slots[k];
		if (slot && slot_matches(*slot, *adm, *body_fn, use_vm)) {
			reset_worker_slot(eng, *slot, budget);
		} else {
			slot = provision_worker(eng, *adm, budget, use_vm);
		}
		slot->begin = bounds[k];
		slot->end = bounds[k + 1];
		slot->input.clear();
		slot->element_refs.clear();
		if (by_ref_element) {
			slot->raw_input = nullptr;
			slot->element_refs.reserve(slot->end - slot->begin);
			for (size_t i = slot->begin; i < slot->end; ++i) {
				slot->element_refs.push_back(script_value::make_element_reference(array_handle, i, &eng, element_type));
			}
		} else {
			// Plain-auto element: workers read the live vector directly; the by-value
			// binding clones (value semantics), and normalization made every element
			// exclusive, so those clones touch no cross-thread counts
			slot->raw_input = &source;
		}
		slot->capture_name_ids.clear();
		slot->provisioned_nodes.clear();
		std::vector<const void*> capture_nodes;
		for (const auto& cap : captures) {
			script_value provisioned(std::monostate{}, &eng);
			try {
				switch (cap.kind) {
				case parallel_capture_kind::scalar:
					provisioned = *cap.source;
					break;
				case parallel_capture_kind::string:
				case parallel_capture_kind::snapshot:
					capture_nodes.clear();
					provisioned = cap.source->parallel_detached_copy(&value_types, &capture_nodes);
					slot->provisioned_nodes.insert(capture_nodes.begin(), capture_nodes.end());
					break;
				case parallel_capture_kind::borrow:
					if (type_info* t = cap.source->get_type_info().get()) { value_types.push_back(t); }
					provisioned = script_value::make_parallel_borrow(*cap.source, &eng);
					break;
				}
			} catch (const std::exception& e) {
				return usage("parallel_for: captured name '" + std::string(cap.entry->name) + "': " + e.what());
			}
			slot->root_env->define(cap.entry->name_id, std::move(provisioned));
			slot->capture_name_ids.push_back(cap.entry->name_id);
		}
		contexts.push_back(slot.get());
	}
	prewarm_region_types(eng, [&] {
		std::vector<type_info*> seed = adm->referenced_types;
		seed.insert(seed.end(), value_types.begin(), value_types.end());
		if (element_type.get()) { seed.push_back(element_type.get()); }
		return seed;
	}());

	parallel_region_table table;
	table.entries.push_back({ std::this_thread::get_id(), &contexts[0]->limits });
	if (worker_count > 1) {
		const size_t pool_workers_needed = worker_count - 1;
		if (!state.pool || state.pool->worker_count() < pool_workers_needed) {
			state.pool = std::make_unique<jai::thread_pool>(
				std::max(pool_workers_needed, jai::thread_pool::default_worker_count()));
		}
		for (size_t k = 1; k < worker_count; ++k) {
			table.entries.push_back({ state.pool->worker_thread_id(k - 1), &contexts[k]->limits });
		}
	}

	// Safe mode freezes the symbolizer (worker interns are bugs, caught loudly).
	// Trusted mode leaves it live: script-class method dispatch legitimately warms
	// getter/setter ids mid-region - the user vouched for the concurrency.
	const bool freeze_symbols = !eng.allow_unsafe_parallel();
	if (freeze_symbols) { eng.get_symbolizer()->set_frozen(true); }
	state.active_region = &table;
	for (size_t k = 1; k < worker_count; ++k) {
		parallel_worker_slot* ctx = contexts[k];
		engine* eng_ptr = &eng;
		state.pool->submit_to(k - 1, [ctx, eng_ptr] { run_worker_inplace(*ctx, *eng_ptr); });
	}
	run_worker_inplace(*contexts[0], eng);
	if (worker_count > 1) {
		state.pool->wait_idle();
	}
	state.active_region = nullptr;
	if (freeze_symbols) { eng.get_symbolizer()->set_frozen(false); }

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
		return raise(code, winner->message.empty() ? std::string("parallel_for: worker failed")
		                                           : winner->message);
	}
	return {};
}

// Tier-1 raw read through a region borrow (see parallel_transform.hpp). Runs on worker
// threads: every message below is a static literal (checked_result never allocates) and
// NOTHING here interns against the frozen symbolizer or copies a shared handle -
// primitive elements mint plain values, anything heavier rides the silent deep clone.
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
		default:
			return elem.parallel_detached_copy();   // tier 3 (defensive: borrows are all-primitive)
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
		default:
			return elem.parallel_detached_copy();
		}
	}
	return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
		"parallel borrow subscript on a non-container value (internal)");
}

} // namespace jai::detail
