#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/body_walker.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/ref_lvalue.hpp>
#include <jaiscript/detail/string_mutator_receiver.hpp>
#include <jaiscript/detail/cast_kernel.hpp>
#include <jaiscript/detail/transient_read.hpp>
#include <jaiscript/detail/return_conv.hpp>
#include <algorithm>
#include <cassert>
#include <functional>

namespace jai::vm {

using jai::detail::body_walker;

namespace {

	inline bool is_lvalue_shaped(const expression* e) { return is_lvalue_shaped_expr(e); }

	inline std::shared_ptr<block_stmt> as_block(const statement_ptr& body, const source_location& loc) {
		if (auto block = std::dynamic_pointer_cast<block_stmt>(body)) {
			return block;
		}
		std::vector<declaration_ptr> stmts;
		if (body) {
			stmts.push_back(std::make_shared<statement_decl>(loc, body));
		}
		return std::make_shared<block_stmt>(loc, std::move(stmts));
	}

	// True when executing the node can define a name directly into the CURRENT scope's
	// ENVIRONMENT, so an enclosing block must own a real one. Blocks whose statements
	// all report false compile without op_scope_push/pop (hot for loop bodies).
	// slot_decls_env_free: inside a callable body, slot-resident variable decls write
	// the frame slot and never touch the environment (define_decl_value), so they
	// don't need the scope either - and the scope they used to force is not free:
	// every env push/reset bumps the GLOBAL env epoch, poisoning every vm lookup
	// cache in the engine (measured: ~6.5k scope cycles/tick in GLOOM for scopes
	// holding nothing). Top-level decls define into the environment even when
	// slotted, so top-level blocks keep their scopes.
	bool declares_in_current_scope(const ast_node* node, bool slot_decls_env_free) {
		if (!node) return false;
		switch (node->get_type()) {
		case node_type::expression_stmt:
		case node_type::expression_decl:
		case node_type::return_stmt:
		case node_type::break_stmt:
		case node_type::continue_stmt:
		case node_type::fallthrough_stmt:
		case node_type::block_stmt:       // owns its own scope
		case node_type::for_stmt:         // compile_for pushes its own scope
		case node_type::range_for_stmt:   // loop variable lives in the iteration scope
		case node_type::parallel_for_stmt: // body is a separate worker function
			return false;
		case node_type::statement_decl:
			return declares_in_current_scope(static_cast<const statement_decl*>(node)->statement.get(), slot_decls_env_free);
		case node_type::if_stmt: {
			auto* branch = static_cast<const if_stmt*>(node);
			return declares_in_current_scope(branch->then_statement.get(), slot_decls_env_free) ||
			       declares_in_current_scope(branch->else_statement.get(), slot_decls_env_free);
		}
		case node_type::while_stmt:
			return declares_in_current_scope(static_cast<const while_stmt*>(node)->body.get(), slot_decls_env_free);
		case node_type::variable_decl:
			return !slot_decls_env_free ||
			       static_cast<const variable_decl*>(node)->slot_index == SIZE_MAX;
		case node_type::switch_stmt: {
			auto* sw = static_cast<const switch_stmt*>(node);
			for (const auto& c : sw->cases) {
				for (const auto& st : c->body) {
					if (declares_in_current_scope(st.get(), slot_decls_env_free)) { return true; }
				}
			}
			if (sw->default_case) {
				for (const auto& st : sw->default_case->body) {
					if (declares_in_current_scope(st.get(), slot_decls_env_free)) { return true; }
				}
			}
			return false;
		}
		default:
			return true;   // class/function decls, include/import, try: keep the scope
		}
	}

	// Lazy-env gate for function-body chunks: false only when EVERY op is on the explicit
	// safe list (fail-closed: anything that can capture the frame env's identity — refs,
	// closures, try records, scope pushes, decls into the env — keeps the eager env).
#ifdef JAISCRIPT_VM_PROFILE
	// Profile builds name each chunk's FIRST tripping opcode (compiles are ~once per
	// function, so this is a bounded census, not per-call spam)
	#define JAI_ENV_TRIP(op) fprintf(stderr, "[env-trip] %s: %.*s\n", \
		body.function_name.c_str(), (int)opcode_name(op).size(), opcode_name(op).data())
#else
	#define JAI_ENV_TRIP(op)
#endif
	bool body_needs_frame_env(const chunk& body) {
		for (const auto& ins : body.code) {
			switch (ins.op) {
			case opcode::op_const:
			case opcode::op_null:
			case opcode::op_true:
			case opcode::op_false:
			case opcode::op_pop:
			case opcode::op_dup:
			case opcode::op_to_bool:
			case opcode::op_load:
			case opcode::op_store:
			case opcode::op_compound_store:
			case opcode::op_incdec:
			case opcode::op_binary:
			case opcode::op_binary_fused:
			case opcode::op_binary_fused_temp:
			case opcode::op_t_decl_mov:
			case opcode::op_t_decl_const:
			case opcode::op_t_store_mov:
			case opcode::op_t_store_const:
			case opcode::op_operand_temp:
			case opcode::op_math_temp:
			case opcode::op_index:
			case opcode::op_index_assign:
			case opcode::op_index_compound:
			case opcode::op_index_store:
			case opcode::op_index_compound_fused:
			case opcode::op_math:
			case opcode::op_unary:
			case opcode::op_array:
			case opcode::op_map:
			case opcode::op_jump:
			case opcode::op_jump_if_false:
			case opcode::op_jump_if_true:
			case opcode::op_fused_cmp_jump:
			case opcode::op_compound_fused:
			case opcode::op_loop_back:
			case opcode::op_cells_back:
			case opcode::op_c_arith:
			case opcode::op_c_cmp:
			case opcode::op_c_load:
			case opcode::op_c_load_any:
			case opcode::op_c_index_store:
			case opcode::op_c_back:
			case opcode::op_c_jcmp:
			case opcode::op_c_jump:
			case opcode::op_c_guard:
			case opcode::op_c_arith_any:
			case opcode::op_c_mov:
			case opcode::op_c_math:
			case opcode::op_cells_cfor_back:
			case opcode::op_c_cfor_back:
			case opcode::op_c_cmp_any:
			case opcode::op_c_jcmp_any:
			case opcode::op_call:
			case opcode::op_probe_callee:
			case opcode::op_call_from_scratch:
			case opcode::op_probe_member:
			case opcode::op_return:
			case opcode::op_return_ident:
			case opcode::op_return_binary:
			case opcode::op_return_convert:
			case opcode::op_call_method:
			case opcode::op_push_fused:
			case opcode::op_this:
			case opcode::op_super:
			case opcode::op_from_this:
			case opcode::op_null_guard:
			case opcode::op_throw:
			case opcode::op_case_eq:
			case opcode::op_cfor_prep:
			case opcode::op_cfor_back:
			case opcode::op_cfor_pop:
			case opcode::op_halt:
			// Member/static access + construction consult the env only for the lexical
			// access context, which frame_access_context derives from entry_env (null
			// for env-untouched frames = no class context, exactly a plain function's
			// lexical truth); exec_new never touches environment_
			case opcode::op_get_member:
			case opcode::op_set_member:
			case opcode::op_member_compound:
			case opcode::op_get_static:
			case opcode::op_set_static:
			case opcode::op_new:
				break;
			case opcode::op_decl_var: {
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.a].get());
				assert(decl->slot_index != SIZE_MAX && "parser slots every function-body decl");
				if (decl->slot_index == SIZE_MAX) { JAI_ENV_TRIP(ins.op); return true; }
				break;
			}
			case opcode::op_binary_fused_decl: {
				const auto& dp = body.fused_binary_dst_protos[ins.a];
				auto* decl = static_cast<const variable_decl*>(body.nodes[dp.node_index].get());
				if (decl->slot_index == SIZE_MAX) { JAI_ENV_TRIP(ins.op); return true; }
				break;
			}
			case opcode::op_index_fused_decl: {
				if (ins.c == k_invalid_u32) break;   // a compiler temp landing: a frame slot, never the env
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.c].get());
				if (decl->slot_index == SIZE_MAX) { JAI_ENV_TRIP(ins.op); return true; }
				break;
			}
			case opcode::op_decl_ref_ident:
			case opcode::op_decl_ref_value: {
				// the reference lands in the alias's frame slot; the referent resolves slot-first, then through
				// the chain the frame already runs in (a global's cell, a this-field) or is the value the body
				// just computed (an element, a member of an evaluated object): never this frame's env
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.a].get());
				if (decl->slot_index == SIZE_MAX) { JAI_ENV_TRIP(ins.op); return true; }
				break;
			}
			case opcode::op_cast:
				// value forms never touch the env; the reference arms resolve storage through it
				if (ins.b != 0) { JAI_ENV_TRIP(ins.op); return true; }
				break;
			case opcode::op_binary_fused_store:
			case opcode::op_index_fused:
			case opcode::op_index_store_fused:
				break;
			case opcode::op_iter_init:
			case opcode::op_iter_next:
			case opcode::op_iter_pop: {
				// range-for is frame-env-free when the loop var is slot-resident (the
				// iter's own scope env covers block scoping); iter_next carries the
				// proto index in a
				if (ins.op == opcode::op_iter_next && body.iter_protos[ins.a].slot == SIZE_MAX) { JAI_ENV_TRIP(ins.op); return true; }
				break;
			}
			case opcode::op_destructure: {
				for (const auto& name : body.destructure_protos[ins.a].names) {
					if (name.second == SIZE_MAX) { JAI_ENV_TRIP(ins.op); return true; }
				}
				break;
			}
			default:
				JAI_ENV_TRIP(ins.op);
				return true;
			}
		}
		return false;
	}

	// Sticky-method-scope gate (chunk::method_env_reusable): the env stays semantically a
	// real per-call method env, so ops may CONSULT it freely — what disqualifies a body is
	// anything that parks state in it (unslotted decls, scope pushes, includes/class decls)
	// or captures its identity beyond the call (closures, ref decls, try records, yields).
	// Fail-closed like body_needs_frame_env; member-access ops are safe here because 'this'
	// and the access context are re-stamped on every call.
	bool method_body_env_reusable(const chunk& body) {
		for (const auto& ins : body.code) {
			switch (ins.op) {
			case opcode::op_const:
			case opcode::op_null:
			case opcode::op_true:
			case opcode::op_false:
			case opcode::op_pop:
			case opcode::op_dup:
			case opcode::op_to_bool:
			case opcode::op_load:
			case opcode::op_store:
			case opcode::op_compound_store:
			case opcode::op_incdec:
			case opcode::op_binary:
			case opcode::op_binary_fused:
			case opcode::op_binary_fused_temp:
			case opcode::op_t_decl_mov:
			case opcode::op_t_decl_const:
			case opcode::op_t_store_mov:
			case opcode::op_t_store_const:
			case opcode::op_operand_temp:
			case opcode::op_math_temp:
			case opcode::op_index:
			case opcode::op_index_assign:
			case opcode::op_index_compound:
			case opcode::op_index_store:
			case opcode::op_index_compound_fused:
			case opcode::op_math:
			case opcode::op_unary:
			case opcode::op_array:
			case opcode::op_map:
			case opcode::op_jump:
			case opcode::op_jump_if_false:
			case opcode::op_jump_if_true:
			case opcode::op_fused_cmp_jump:
			case opcode::op_compound_fused:
			case opcode::op_loop_back:
			case opcode::op_cells_back:
			case opcode::op_c_arith:
			case opcode::op_c_cmp:
			case opcode::op_c_load:
			case opcode::op_c_load_any:
			case opcode::op_c_index_store:
			case opcode::op_c_back:
			case opcode::op_c_jcmp:
			case opcode::op_c_jump:
			case opcode::op_c_guard:
			case opcode::op_c_arith_any:
			case opcode::op_c_mov:
			case opcode::op_c_math:
			case opcode::op_cells_cfor_back:
			case opcode::op_c_cfor_back:
			case opcode::op_c_cmp_any:
			case opcode::op_c_jcmp_any:
			case opcode::op_call:
			case opcode::op_probe_callee:
			case opcode::op_call_from_scratch:
			case opcode::op_probe_member:
			case opcode::op_return:
			case opcode::op_return_ident:
			case opcode::op_return_binary:
			case opcode::op_return_convert:
			case opcode::op_call_method:
			case opcode::op_push_fused:
			case opcode::op_this:
			case opcode::op_super:
			case opcode::op_from_this:
			case opcode::op_null_guard:
			case opcode::op_throw:
			case opcode::op_case_eq:
			case opcode::op_cfor_prep:
			case opcode::op_cfor_back:
			case opcode::op_cfor_pop:
			case opcode::op_halt:
			case opcode::op_get_member:
			case opcode::op_get_static:
			case opcode::op_set_member:
			case opcode::op_set_static:
			case opcode::op_member_compound:
			case opcode::op_new:
				break;
			case opcode::op_decl_var: {
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.a].get());
				if (decl->slot_index == SIZE_MAX) return false;
				break;
			}
			case opcode::op_binary_fused_decl: {
				const auto& dp = body.fused_binary_dst_protos[ins.a];
				auto* decl = static_cast<const variable_decl*>(body.nodes[dp.node_index].get());
				if (decl->slot_index == SIZE_MAX) return false;
				break;
			}
			case opcode::op_index_fused_decl: {
				if (ins.c == k_invalid_u32) break;   // a compiler temp landing: a frame slot, never the env
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.c].get());
				if (decl->slot_index == SIZE_MAX) return false;
				break;
			}
			case opcode::op_decl_ref_ident:
			case opcode::op_decl_ref_value: {
				auto* decl = static_cast<const variable_decl*>(body.nodes[ins.a].get());
				if (decl->slot_index == SIZE_MAX) return false;
				break;
			}
			case opcode::op_cast:
				if (ins.b != 0) return false;   // reference arms box/alias env storage like ref decls
				break;
			case opcode::op_binary_fused_store:
			case opcode::op_index_fused:
			case opcode::op_index_store_fused:
				break;
			case opcode::op_iter_init:
			case opcode::op_iter_next:
			case opcode::op_iter_pop:
				if (ins.op == opcode::op_iter_next && body.iter_protos[ins.a].slot == SIZE_MAX) return false;
				break;
			case opcode::op_destructure: {
				for (const auto& name : body.destructure_protos[ins.a].names) {
					if (name.second == SIZE_MAX) return false;
				}
				break;
			}
			default:
				return false;
			}
		}
		return true;
	}

	inline uint32_t compound_kind_for(token_type op) {
		switch (op) {
		case token_type::plus_equal: return compound_plus;
		case token_type::minus_equal: return compound_minus;
		case token_type::star_equal: return compound_star;
		case token_type::slash_equal: return compound_slash;
		case token_type::percent_equal: return compound_percent;
		default: return compound_plus;
		}
	}

} // namespace

size_t vm_compiler::emit(opcode op, uint32_t a, uint32_t b, uint32_t c) {
	chunk_->code.push_back({op, a, b, c});
	chunk_->stmt_nodes.push_back(current_stmt_);
	return chunk_->code.size() - 1;
}

// Conditional exit jump for an already-compiled condition. When the condition's
// result comes from a fused comparison, the pair collapses into op_fused_cmp_jump
// (one dispatch, no bool through the stack). Returns the instruction to patch_jump.
// Gated on the condition node's own shape: the collapse rewrites code.back() in place,
// and a ternary condition's else-arm also leaves a fused comparison there — but the
// then-arm's exit jump targets one PAST it, so rewriting would let the then path skip
// the test and fall into the body with its value stranded on the stack.
size_t vm_compiler::emit_cond_jump_false(const expression* cond_expr) {
	const uint32_t proved = expression_returns_bool(cond_expr) ? 1u : 0u;
	auto& code = chunk_->code;
	if (!code.empty() && code.back().op == opcode::op_binary_fused &&
	    cond_expr && cond_expr->get_type() == node_type::binary_expr) {
		switch (static_cast<token_type>(chunk_->fused_binary_protos[code.back().a].op)) {
			case token_type::less:
			case token_type::less_equal:
			case token_type::greater:
			case token_type::greater_equal:
			case token_type::equal_equal:
			case token_type::bang_equal: {
				vm_instruction& ins = code.back();
				ins.op = opcode::op_fused_cmp_jump;
				ins.b = ins.a;            // fused proto index
				ins.a = k_invalid_u32;    // patched by the caller
				ins.c = proved;
				return code.size() - 1;
			}
			default: break;
		}
	}
	return emit(opcode::op_jump_if_false, k_invalid_u32, proved);
}

// Compound store to an identifier whose RHS was just compiled. When the RHS's last
// instruction produces the whole RHS value — a fused binary (`sum += i * 2`), a bare
// identifier load (`sum += i`), or a constant (`sum += 1`) — the pair collapses into
// op_compound_fused. Gated on the RHS node's own shape: the peephole rewrites
// code.back() in place, and a ternary RHS's else-arm also leaves a fusable tail there —
// but the then-arm's exit jump targets one PAST it, so rewriting that tail would let
// the then path skip the store entirely.
void vm_compiler::emit_compound_store(uint32_t symbol_index, uint32_t slot, uint32_t kind_flags, const expression* rhs) {
	auto& code = chunk_->code;
	// ::name targets stay on the plain op (compound_fused's fast path resolves its
	// target through the frame/env ladder; the global-only door lives in
	// exec_compound_store and this path is cold by definition)
	if (!code.empty() && rhs && !(kind_flags & compound_flag_global_only)) {
		compound_fused_proto cp;
		bool fuse = false;
		switch (rhs->get_type()) {
		case node_type::binary_expr:
			if (code.back().op == opcode::op_binary_fused) {
				cp.rhs_proto = code.back().a;
				fuse = true;
			}
			break;
		case node_type::identifier_expr:
			if (code.back().op == opcode::op_load) {
				cp.rhs_proto = k_invalid_u32;
				cp.rhs.slot = code.back().a;
				cp.rhs.symbol = code.back().b;
				cp.rhs.load_flags = code.back().c;
				if (chunk_->ident_nodes.size() == code.size()) { cp.rhs.ident_node = chunk_->ident_nodes.back(); }
				fuse = true;
			}
			break;
		case node_type::literal_expr:
			if (code.back().op == opcode::op_const) {
				cp.rhs_proto = k_invalid_u32;
				cp.rhs.const_index = code.back().a;
				fuse = true;
			}
			break;
		default:
			break;
		}
		if (fuse) {
			cp.symbol = symbol_index;
			cp.slot = slot;
			cp.kind_flags = kind_flags;
			chunk_->compound_fused_protos.push_back(cp);
			vm_instruction& ins = code.back();
			ins.op = opcode::op_compound_fused;
			ins.a = static_cast<uint32_t>(chunk_->compound_fused_protos.size() - 1);
			ins.b = 0;
			ins.c = 0;
			return;
		}
	}
	emit(opcode::op_compound_store, symbol_index, slot, kind_flags);
}

void vm_compiler::patch_jump(size_t at, size_t target) {
	chunk_->code[at].a = static_cast<uint32_t>(target);
}

uint32_t vm_compiler::add_constant(const script_value& v) {
	chunk_->constants.push_back(v);
	return static_cast<uint32_t>(chunk_->constants.size() - 1);
}

uint32_t vm_compiler::add_symbol(uint64_t id) {
	chunk_->symbols.push_back(id);
	return static_cast<uint32_t>(chunk_->symbols.size() - 1);
}

uint32_t vm_compiler::add_message(std::string msg) {
	chunk_->messages.push_back(std::move(msg));
	return static_cast<uint32_t>(chunk_->messages.size() - 1);
}

uint32_t vm_compiler::add_node(std::shared_ptr<ast_node> node) {
	chunk_->nodes.push_back(std::move(node));
	return static_cast<uint32_t>(chunk_->nodes.size() - 1);
}

void vm_compiler::emit_unsupported(const char* construct, const char* stage) {
	std::string msg = std::string("vm backend: ") + construct + " not implemented yet (stage " + stage + ")";
	emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::unsupported_operation),
	     add_message(std::move(msg)), k_invalid_u32);
	// Keep the value-stack shape consistent for expression positions
	emit(opcode::op_null);
}

uint32_t vm_compiler::identifier_slot_operand(identifier_expr* ident) const {
	if (ident->slot_index == SIZE_MAX) return k_invalid_u32;
	if (callable_.active && callable_.declared.find(ident->symbol_id) == callable_.declared.end()) {
		// Slot belongs to an enclosing function's frame (or interpreter-patched state):
		// resolve through the environment instead of a foreign slot.
		return k_invalid_u32;
	}
	if (!callable_.active) return k_invalid_u32;
	return static_cast<uint32_t>(ident->slot_index);
}

bool vm_compiler::symbol_is_env_only(uint64_t symbol_id) const {
	return !callable_.active || callable_.declared.find(symbol_id) == callable_.declared.end();
}

// Callee-first (C++17 [expr.call]): the callee's lookup error must beat any argument
// error or side effect. Literals, this and frame-slot locals can do neither, so a call
// whose arguments are all of that shape keeps its exact old code; anything else gets
// the callee probed before the arguments.
bool vm_compiler::call_args_need_probe(const call_expr& call) const {
	for (const auto& arg : call.arguments) {
		switch (arg->get_type()) {
			case node_type::literal_expr:
			case node_type::this_expr:
				break;
			case node_type::identifier_expr: {
				auto* ident = static_cast<identifier_expr*>(arg.get());
				if (ident->global_qualified || identifier_slot_operand(ident) == k_invalid_u32) return true;
				break;
			}
			default:
				return true;
		}
	}
	return false;
}

// A subscript chain fusable as a binary operand: `base[i][j]...[k]` where base is an
// identifier, every index is an identifier or int literal, and the classifier marked
// every level transient (the element-elision ruling applies; the runtime replays the
// full op_index semantics per level for any non-array shape). Truly N-level - the
// depth cap is a sanity bound, not a semantic limit.
static bool fusable_subscript_index(const expression* e) {
	if (e->get_type() == node_type::identifier_expr) {
		// ::name indexes can't ride fused levels (fused_subscript_index has no flags)
		return !static_cast<const identifier_expr*>(e)->global_qualified;
	}
	return e->get_type() == node_type::literal_expr &&
	       static_cast<const literal_expr*>(e)->value.raw_storage_index() == script_value::TYPEID_INT;
}

static constexpr int k_max_fused_subscript_depth = 8;

static bool fusable_subscript_operand(const expression* e) {
	int levels = 0;
	const expression* cur = e;
	while (cur->get_type() == node_type::binary_expr) {
		const auto* sub = static_cast<const binary_expr*>(cur);
		if (sub->op.type != token_type::left_bracket || !sub->transient_element_read) return false;
		if (!fusable_subscript_index(sub->right.get())) return false;
		if (++levels > k_max_fused_subscript_depth) return false;
		cur = sub->left.get();
	}
	if (levels == 0 || cur->get_type() != node_type::identifier_expr) return false;
	// ::name bases/indexes stay off the fused-subscript path (fused_subscript_index
	// carries no load flags; the generic op_load/op_index pair honors the qualifier)
	if (static_cast<const identifier_expr*>(cur)->global_qualified) return false;
	return true;
}

uint32_t vm_compiler::binary_shape(binary_expr* expr) {
	const auto lt = expr->left->get_type();
	const auto rt = expr->right->get_type();
	if (lt == node_type::identifier_expr && rt == node_type::identifier_expr) return binary_shape_ident_ident;
	if (lt == node_type::identifier_expr && rt == node_type::literal_expr) return binary_shape_ident_literal;
	if (lt == node_type::literal_expr && rt == node_type::identifier_expr) return binary_shape_literal_ident;
	const bool ls = fusable_subscript_operand(expr->left.get());
	const bool rs = fusable_subscript_operand(expr->right.get());
	if ((ls || rs) &&
	    (ls || lt == node_type::identifier_expr || lt == node_type::literal_expr) &&
	    (rs || rt == node_type::identifier_expr || rt == node_type::literal_expr)) {
		return binary_shape_subscript;
	}
	return binary_shape_none;
}

std::shared_ptr<chunk> vm_compiler::compile_program(const std::vector<declaration_ptr>& declarations) {
	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	chunk_->pinned_decls = declarations;
	loops_.clear();
	callable_ = {};
	scope_depth_ = 0;
	current_stmt_ = nullptr;
	temps_active_ = false;
	temp_symbol_index_ = k_invalid_u32;

	for (const auto& decl : declarations) {
		compile_declaration(decl, true);
	}
	emit(opcode::op_halt);
	chunk_ = nullptr;
	return result;
}

std::shared_ptr<chunk> vm_compiler::compile_callable(std::string_view name,
                                                     const std::vector<parameter>& parameters,
                                                     const std::shared_ptr<block_stmt>& body,
                                                     size_t local_count) {
	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	chunk_->function_name = std::string(name);
	chunk_->local_count = local_count;
	chunk_->is_function_body = true;
	loops_.clear();
	scope_depth_ = 0;
	current_stmt_ = nullptr;
	temps_active_ = true;
	temp_next_ = local_count;
	temp_high_ = local_count;
	temp_symbol_index_ = k_invalid_u32;

	callable_ = {};
	callable_.active = true;
	for (const auto& p : parameters) {
		uint64_t id = p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name);
		callable_.declared.insert(id);
#ifdef JAISCRIPT_VM_PROFILE
		if (p.slot_index != SIZE_MAX) { chunk_->profile_params.push_back({ static_cast<uint32_t>(p.slot_index), p.type.get(), p.is_reference }); }
#endif
	}
	{
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_declared = [this](uint64_t id) { callable_.declared.insert(id); };
		walker.stmt(body);
	}

	// Default arguments compile as standalone expression chunks evaluated at call time
	chunk_->param_default_chunks.resize(parameters.size());
	for (size_t i = 0; i < parameters.size(); ++i) {
		if (parameters[i].default_value) {
			auto saved_chunk = chunk_;
			auto saved_stmt = current_stmt_;
			auto default_chunk = std::make_shared<chunk>();
			chunk_ = default_chunk.get();
			compile_expression(parameters[i].default_value);
			emit(opcode::op_halt);
			chunk_ = saved_chunk;
			current_stmt_ = saved_stmt;
			chunk_->param_default_chunks[i] = std::move(default_chunk);
		}
	}

	for (const auto& decl : body->declarations) {
		compile_declaration(decl, false);
	}
	emit(opcode::op_halt);
	result->needs_frame_env = body_needs_frame_env(*result);
	// The lazy safe list is a strict subset of the sticky list, so env-free bodies qualify
	result->method_env_reusable = !result->needs_frame_env || method_body_env_reusable(*result);
	// Register-file temps live above local_count: raise it to the watermark so the
	// window null-fill (and fiber set_local growth) covers every temp slot
	if (temp_high_ > result->local_count) {
		result->local_count = temp_high_;
	}
	lower_typed_ops(*result);
	temps_active_ = false;
	chunk_ = nullptr;
	return result;
}

std::shared_ptr<chunk> vm_compiler::compile_standalone_expression(const expression_ptr& expr) {
	auto saved_chunk = chunk_;
	auto saved_stmt = current_stmt_;
	auto saved_callable = std::move(callable_);
	auto saved_loops = std::move(loops_);
	auto saved_depth = scope_depth_;

	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	callable_ = {};
	loops_.clear();
	scope_depth_ = 0;
	current_stmt_ = expr.get();
	chunk_->nodes.push_back(expr);
	compile_expression(expr);
	emit(opcode::op_halt);

	chunk_ = saved_chunk;
	current_stmt_ = saved_stmt;
	callable_ = std::move(saved_callable);
	loops_ = std::move(saved_loops);
	scope_depth_ = saved_depth;
	return result;
}

std::shared_ptr<chunk> vm_compiler::compile_standalone_declaration(const declaration_ptr& decl) {
	auto saved_chunk = chunk_;
	auto saved_stmt = current_stmt_;
	auto saved_callable = std::move(callable_);
	auto saved_loops = std::move(loops_);
	auto saved_depth = scope_depth_;

	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	callable_ = {};
	loops_.clear();
	scope_depth_ = 0;
	current_stmt_ = decl.get();
	chunk_->nodes.push_back(decl);
	compile_declaration(decl, false);
	emit(opcode::op_halt);

	chunk_ = saved_chunk;
	current_stmt_ = saved_stmt;
	callable_ = std::move(saved_callable);
	loops_ = std::move(saved_loops);
	scope_depth_ = saved_depth;
	return result;
}

void vm_compiler::compile_declaration(const declaration_ptr& decl, bool top_level) {
	current_stmt_ = decl.get();
	switch (decl->get_type()) {
	case node_type::variable_decl:
		compile_variable_decl(std::static_pointer_cast<variable_decl>(decl));
		return;
	case node_type::function_decl:
		compile_function_decl(std::static_pointer_cast<function_decl>(decl));
		return;
	case node_type::expression_decl: {
		auto* ed = static_cast<expression_decl*>(decl.get());
		const bool implicit = top_level && ed->implicit_return;
		if (!implicit && compile_no_result_expression(ed->expression)) return;
		compile_expression(ed->expression, !implicit);
		emit(implicit ? opcode::op_implicit_return : opcode::op_pop);
		return;
	}
	case node_type::statement_decl:
		compile_statement(static_cast<statement_decl*>(decl.get())->statement);
		return;
	case node_type::destructuring_decl: {
		auto* dd = static_cast<destructuring_decl*>(decl.get());
		compile_expression(dd->initializer);
		destructure_proto proto;
		for (size_t i = 0; i < dd->names.size(); ++i) {
			size_t slot = i < dd->slot_indices.size() ? dd->slot_indices[i] : SIZE_MAX;
			if (slot != SIZE_MAX && symbol_is_env_only(dd->names[i].second)) {
				slot = SIZE_MAX;
			}
			proto.names.emplace_back(dd->names[i].second, slot);
		}
		chunk_->destructure_protos.push_back(std::move(proto));
		emit(opcode::op_destructure, static_cast<uint32_t>(chunk_->destructure_protos.size() - 1));
		return;
	}
	case node_type::class_decl:
		emit(opcode::op_class_decl, add_node(decl));
		return;
	case node_type::namespace_decl:
		emit(opcode::op_namespace_decl, add_node(decl));
		return;
	case node_type::enum_decl:
		emit(opcode::op_enum_decl, add_node(decl));
		return;
	case node_type::extern_decl:
		return;
	case node_type::include_decl: {
		auto* inc = static_cast<include_decl*>(decl.get());
		uint32_t path_msg = k_invalid_u32;
		uint32_t is_expr = 0;
		if (inc->path_expr) {
			compile_expression(inc->path_expr);
			is_expr = 1;
		} else {
			path_msg = add_message(inc->path);
		}
		emit(opcode::op_include, path_msg, is_expr);
		emit(opcode::op_pop);
		return;
	}
	case node_type::import_decl: {
		auto* imp = static_cast<import_decl*>(decl.get());
		uint32_t path_msg = k_invalid_u32;
		uint32_t is_expr = 0;
		if (imp->path_expr) {
			compile_expression(imp->path_expr);
			is_expr = 1;
		} else {
			path_msg = add_message(imp->path);
		}
		emit(opcode::op_import, path_msg, is_expr);
		emit(opcode::op_pop);
		return;
	}
	default:
		compile_statement(std::static_pointer_cast<statement>(decl));
		return;
	}
}

void vm_compiler::compile_statement(const statement_ptr& stmt) {
	if (!stmt) return;
	current_stmt_ = stmt.get();
	switch (stmt->get_type()) {
	case node_type::expression_stmt: {
		const auto& e = static_cast<expression_stmt*>(stmt.get())->expression;
		if (compile_no_result_expression(e)) return;
		compile_expression(e, true);
		emit(opcode::op_pop);
		return;
	}
	case node_type::block_stmt:
		compile_block(static_cast<block_stmt*>(stmt.get()));
		return;
	case node_type::if_stmt:
		compile_if(static_cast<if_stmt*>(stmt.get()));
		return;
	case node_type::while_stmt:
		compile_while(static_cast<while_stmt*>(stmt.get()));
		return;
	case node_type::for_stmt:
		compile_for(static_cast<for_stmt*>(stmt.get()));
		return;
	case node_type::return_stmt: {
		auto* rs = static_cast<return_stmt*>(stmt.get());
		// Return-site fusion is legal only when the region exits emit NOTHING for a
		// return (try/catch regions emit op_try_pop/op_catch_end between value and
		// return; loops/switches emit nothing on the return path)
		bool clean_exit = true;
		for (const auto& region : loops_) {
			if (region.kind == region_kind::try_block || region.kind == region_kind::catch_block) {
				clean_exit = false;
				break;
			}
		}
		if (rs->value) {
			if (rs->binds_reference && rs->value->get_type() == node_type::identifier_expr) {
				// Ref-return producer: bind the named storage as a reference (share the
				// cell, boxing on demand) instead of loading a copy
				auto* ident = static_cast<identifier_expr*>(rs->value.get());
				if (ident->symbol_id == UINT64_MAX) {
					ident->symbol_id = symbolizer_->intern(ident->name);
				}
				emit(opcode::op_ref_return_bind, identifier_slot_operand(ident), add_symbol(ident->symbol_id),
				     ident->global_qualified ? load_flag_global_only : 0u);
			} else if (rs->binds_reference && detail::is_ref_bindable_lvalue(rs->value.get())) {
				// Field/subscript/chain operand: owner-pinned reference via the shared kernel
				emit(opcode::op_ref_return_lvalue, add_node(rs->value));
			} else if (clean_exit && rs->value->get_type() == node_type::identifier_expr) {
				// Structural LOAD+RETURN fusion (`return n;` - half of fib's dynamic
				// returns): whole-node gate, resolution mirrors op_load exactly at runtime
				auto* ident = static_cast<identifier_expr*>(rs->value.get());
				if (ident->symbol_id == UINT64_MAX) {
					ident->symbol_id = symbolizer_->intern(ident->name);
				}
				uint32_t flags = 0;
				if (ident->global_qualified) flags |= load_flag_global_only;
				if (!ident->name.empty() && (ident->name.front() == 'w' || ident->name.front() == 's') &&
				    (ident->name.find("weak_ptr<") == 0 || ident->name.find("shared_ptr<") == 0)) {
					flags |= load_flag_type_ctor;
				}
				if (rs->static_return_proof == proof_same_type) flags |= load_flag_return_proven;
				const size_t ret_ip = emit(opcode::op_return_ident, identifier_slot_operand(ident), add_symbol(ident->symbol_id), flags);
				note_ident_node(ret_ip, rs->value);
				return;
			} else {
				// Calls that already yield a reference pass through; anything else
				// rejects in the ref-return conversion (return site / completion door)
				if (clean_exit && rs->value->get_type() == node_type::call_expr &&
				    static_cast<call_expr*>(rs->value.get())->callee->get_type() == node_type::identifier_expr) {
					pending_tail_call_ = true;
				}
				compile_expression(rs->value);
				// BINARY+RETURN tail fusion (2191c59b double gate: the return VALUE
				// node's own shape AND the tail opcode must both agree - a logical
				// && / || root ends in an arm's op, never rewrite those). Non-logical
				// binary roots always emit their own op_binary LAST.
				if (clean_exit && rs->value->get_type() == node_type::binary_expr && !chunk_->code.empty()) {
					const auto* bin = static_cast<const binary_expr*>(rs->value.get());
					const token_type bop = bin->op.type;
					const bool logical = (bop == token_type::ampersand_ampersand || bop == token_type::pipe_pipe);
					if (!logical && chunk_->code.back().op == opcode::op_binary) {
						chunk_->code.back().op = opcode::op_return_binary;
						chunk_->code.back().c = rs->static_return_proof;   // op_binary uses only a/b
						return;
					}
				}
			}
		} else {
			emit(opcode::op_null);
		}
		// By-value materialization operand (detail/return_conv.hpp): ref returns and a
		// bare `return;` carry nothing; an identifier names its own-frame slot for the move
		uint32_t operand_class = detail::return_operand_unclassified;
		uint32_t own_slot = k_invalid_u32;
		if (rs->value && !rs->binds_reference) {
			operand_class = detail::classify_return_operand(rs->value.get());
			if (operand_class == detail::return_operand_ident) {
				own_slot = identifier_slot_operand(static_cast<identifier_expr*>(rs->value.get()));
			}
		}
		if (!clean_exit) {
			// The typed-return conversion runs at the site, inside this frame's try
			// records (a catch around the return sees it); the op_return after the
			// exits lands the converted value without converting or materializing again
			emit(opcode::op_return_convert, rs->static_return_proof, operand_class, own_slot);
			operand_class = detail::return_operand_unclassified;
		}
		emit_region_exits(SIZE_MAX);
		emit(opcode::op_return, 1, clean_exit ? rs->static_return_proof : return_mode_converted, operand_class);
		return;
	}
	case node_type::break_stmt:
		compile_break();
		return;
	case node_type::continue_stmt:
		compile_continue();
		return;
	case node_type::try_stmt:
		compile_try(static_cast<try_stmt*>(stmt.get()));
		return;
	case node_type::switch_stmt:
		compile_switch(static_cast<switch_stmt*>(stmt.get()));
		return;
	case node_type::range_for_stmt:
		compile_range_for(static_cast<range_for_stmt*>(stmt.get()));
		return;
	case node_type::parallel_for_stmt: {
		// Container onto the stack in the enclosing scope; the exec case hands the
		// popped value plus the pinned statement node to detail::run_parallel_for
		// (the body never compiles into THIS chunk - workers compile their own copies)
		auto* pf = static_cast<parallel_for_stmt*>(stmt.get());
		compile_expression(pf->container);
		for (const auto& extra : pf->extra_containers) { compile_expression(extra); }   // zip: left to right
		emit(opcode::op_parallel_for, add_node(stmt), static_cast<uint32_t>(pf->arity()));
		return;
	}
	case node_type::fallthrough_stmt:
		// Sets the runtime fallthrough flag: the [value, ft] pair of the innermost
		// switch sits on top of the value stack at every statement boundary
		emit(opcode::op_pop);
		emit(opcode::op_true);
		return;
	case node_type::case_stmt:
	case node_type::default_stmt:
		emit_unsupported("switch", "B3");
		emit(opcode::op_pop);
		return;
	default:
		if (auto decl = std::dynamic_pointer_cast<declaration>(stmt)) {
			compile_declaration(decl, false);
			return;
		}
		emit_unsupported("statement", "B2");
		emit(opcode::op_pop);
		return;
	}
}

void vm_compiler::compile_block(block_stmt* block) {
	bool needs_scope = false;
	for (const auto& decl : block->declarations) {
		if (declares_in_current_scope(decl.get(), callable_.active)) {
			needs_scope = true;
			break;
		}
	}
	if (!needs_scope) {
		for (const auto& decl : block->declarations) {
			compile_declaration(decl, false);
		}
		return;
	}
	emit(opcode::op_scope_push);
	++scope_depth_;
	for (const auto& decl : block->declarations) {
		compile_declaration(decl, false);
	}
	--scope_depth_;
	emit(opcode::op_scope_pop);
}

void vm_compiler::compile_branch_condition(const expression_ptr& cond, std::vector<size_t>& false_jumps) {
	if (cond->get_type() == node_type::unary_expr) {
		auto* u = static_cast<unary_expr*>(cond.get());
		if (u->op.type == token_type::bang) {
			// `!e` in branch position: branch on e's truthiness directly - the same
			// is_truthy the unary consulted, minus one op and one bool
			compile_expression(u->operand);
			false_jumps.push_back(emit(opcode::op_jump_if_true, k_invalid_u32,
			                           expression_returns_bool(u->operand.get()) ? 1u : 0u));
			return;
		}
	}
	if (cond->get_type() == node_type::binary_expr) {
		auto* b = static_cast<binary_expr*>(cond.get());
		if (b->op.type == token_type::ampersand_ampersand) {
			compile_branch_condition(b->left, false_jumps);
			compile_branch_condition(b->right, false_jumps);
			return;
		}
		if (b->op.type == token_type::pipe_pipe) {
			compile_expression(b->left);
			const uint32_t proved = expression_returns_bool(b->left.get()) ? 1u : 0u;
			size_t jump_true = emit(opcode::op_jump_if_true, k_invalid_u32, proved);
			compile_branch_condition(b->right, false_jumps);
			patch_jump(jump_true, chunk_->code.size());
			return;
		}
	}
	compile_expression(cond);
	false_jumps.push_back(emit_cond_jump_false(cond.get()));
}

void vm_compiler::compile_if(if_stmt* stmt) {
	std::vector<size_t> false_jumps;
	compile_branch_condition(stmt->condition, false_jumps);
	compile_statement(stmt->then_statement);
	if (stmt->else_statement) {
		size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
		for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
		compile_statement(stmt->else_statement);
		patch_jump(jump_end, chunk_->code.size());
	} else {
		for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
	}
}

void vm_compiler::compile_while(while_stmt* stmt) {
	const size_t top = chunk_->code.size();
	std::vector<size_t> exit_jumps;
	compile_branch_condition(stmt->condition, exit_jumps);

	loops_.push_back({});
	loops_.back().continue_target = top;
	loops_.back().continue_is_loop_back = true;
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);
	emit(opcode::op_loop_back, static_cast<uint32_t>(top));

	const size_t end = chunk_->code.size();
	for (size_t at : exit_jumps) {
		patch_jump(at, end);
	}
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, end);
	}
	loops_.pop_back();
}

void vm_compiler::compile_for(for_stmt* stmt) {
	if (compile_counted_for(stmt)) {
		return;
	}

	const bool loop_scope = declares_in_current_scope(stmt->initializer.get(), callable_.active);
	if (loop_scope) {
		emit(opcode::op_scope_push);
		++scope_depth_;
	}

	if (stmt->initializer) {
		compile_declaration(stmt->initializer, false);
	}

	const size_t top = chunk_->code.size();
	std::vector<size_t> exit_jumps;
	if (stmt->condition) {
		compile_branch_condition(stmt->condition, exit_jumps);
	}

	loops_.push_back({});
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);

	const size_t continue_target = chunk_->code.size();
	for (size_t at : loops_.back().continue_patches) {
		patch_jump(at, continue_target);
	}
	if (stmt->update) {
		if (!compile_no_result_expression(stmt->update)) {
			compile_expression(stmt->update, true);
			emit(opcode::op_pop);
		}
	}
	emit(opcode::op_loop_back, static_cast<uint32_t>(top));

	const size_t end = chunk_->code.size();
	for (size_t at : exit_jumps) {
		patch_jump(at, end);
	}
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, end);
	}
	loops_.pop_back();

	if (loop_scope) {
		--scope_depth_;
		emit(opcode::op_scope_pop);
	}
}

// Counting-loop codegen (mirrors the interpreter's visit_for_stmt fast path):
//   for (var/auto/int i = <int literal>; i cmp <int literal|ident>; i ++/--/+=/-= <int literal|ident>)
// Fast iterations run through op_cfor_prep/op_cfor_back with cached int pointers; when
// runtime types don't cooperate the SAME loop runs its generic cond/update bytecode.
bool vm_compiler::compile_counted_for(for_stmt* stmt) {
	if (!stmt->initializer || !stmt->condition || !stmt->update) return false;

	// Initializer: variable_decl with an int-literal initializer
	if (stmt->initializer->get_type() != node_type::variable_decl) return false;
	auto* init_var = static_cast<variable_decl*>(stmt->initializer.get());
	if (!init_var->initializer || init_var->initializer->get_type() != node_type::literal_expr) return false;
	auto* init_lit = static_cast<literal_expr*>(init_var->initializer.get());
	if (init_lit->value.raw_storage_index() != script_value::TYPEID_INT) return false;
	const uint64_t var_id = init_var->name_id;
	if (var_id == UINT64_MAX) return false;

	// Condition: var cmp (int literal | identifier)
	if (stmt->condition->get_type() != node_type::binary_expr) return false;
	auto* cond = static_cast<binary_expr*>(stmt->condition.get());
	switch (cond->op.type) {
	case token_type::less: case token_type::less_equal:
	case token_type::greater: case token_type::greater_equal:
	case token_type::equal_equal: case token_type::bang_equal:
		break;
	default:
		return false;
	}
	if (cond->left->get_type() != node_type::identifier_expr) return false;
	auto* cond_var = static_cast<identifier_expr*>(cond->left.get());
	if (cond_var->symbol_id != var_id) return false;
	if (cond->right->get_type() == node_type::literal_expr) {
		if (static_cast<literal_expr*>(cond->right.get())->value.raw_storage_index() != script_value::TYPEID_INT) return false;
	} else if (cond->right->get_type() != node_type::identifier_expr) {
		return false;
	}

	// Update: ++var / --var / var += x / var -= x  (x = int literal | identifier)
	bool subtract = false;
	bool incdec = false;
	expression_ptr step_expr;   // null = literal 1
	if (stmt->update->get_type() == node_type::unary_expr) {
		auto* un = static_cast<unary_expr*>(stmt->update.get());
		if (un->op.type != token_type::plus_plus && un->op.type != token_type::minus_minus) return false;
		if (un->operand->get_type() != node_type::identifier_expr) return false;
		if (static_cast<identifier_expr*>(un->operand.get())->symbol_id != var_id) return false;
		subtract = un->op.type == token_type::minus_minus;
		incdec = true;
	} else if (stmt->update->get_type() == node_type::assignment_expr) {
		auto* assign = static_cast<assignment_expr*>(stmt->update.get());
		if (assign->op.type != token_type::plus_equal && assign->op.type != token_type::minus_equal) return false;
		if (assign->target->get_type() != node_type::identifier_expr) return false;
		if (static_cast<identifier_expr*>(assign->target.get())->symbol_id != var_id) return false;
		subtract = assign->op.type == token_type::minus_equal;
		if (assign->value->get_type() == node_type::literal_expr) {
			if (static_cast<literal_expr*>(assign->value.get())->value.raw_storage_index() != script_value::TYPEID_INT) return false;
		} else if (assign->value->get_type() != node_type::identifier_expr) {
			return false;
		}
		step_expr = assign->value;
	} else {
		return false;
	}

	// === Pattern matched: emit the counted form ===
	const bool loop_scope = declares_in_current_scope(stmt->initializer.get(), callable_.active);
	if (loop_scope) {
		emit(opcode::op_scope_push);
		++scope_depth_;
	}

	compile_declaration(stmt->initializer, false);

	counted_for_proto proto;
	proto.var = make_fused_operand(cond->left);
	proto.end = make_fused_operand(cond->right);
	if (step_expr) {
		proto.step = make_fused_operand(step_expr);
	} else {
		proto.step.const_index = add_constant(script_value(static_cast<script_int>(1), static_cast<engine*>(nullptr)));
	}
	proto.cmp = static_cast<uint8_t>(cond->op.type);
	proto.step_subtract = subtract;
	proto.step_incdec = incdec;
	chunk_->counted_for_protos.push_back(proto);
	const uint32_t proto_idx = static_cast<uint32_t>(chunk_->counted_for_protos.size() - 1);

	emit(opcode::op_cfor_prep, proto_idx);

	const size_t generic_cond = chunk_->code.size();
	{
		compile_expression(stmt->condition);
		emit_cond_jump_false(stmt->condition.get());   // patched to exit_pop below
	}
	const size_t generic_exit_jump = chunk_->code.size() - 1;

	const size_t body_start = chunk_->code.size();

	loops_.push_back({});
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);

	const size_t continue_target = chunk_->code.size();
	for (size_t at : loops_.back().continue_patches) {
		patch_jump(at, continue_target);
	}
	emit(opcode::op_cfor_back, proto_idx);

	const size_t generic_update = chunk_->code.size();
	if (!compile_no_result_expression(stmt->update)) {
		compile_expression(stmt->update, true);
		emit(opcode::op_pop);
	}
	emit(opcode::op_loop_back, static_cast<uint32_t>(generic_cond));

	const size_t exit_pop = chunk_->code.size();
	emit(opcode::op_cfor_pop);
	const size_t end = chunk_->code.size();

	patch_jump(generic_exit_jump, exit_pop);
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, exit_pop);
	}
	loops_.pop_back();

	auto& stored = chunk_->counted_for_protos[proto_idx];
	stored.body_ip = static_cast<uint32_t>(body_start);
	stored.exit_ip = static_cast<uint32_t>(end);
	stored.generic_cond_ip = static_cast<uint32_t>(generic_cond);
	stored.generic_update_ip = static_cast<uint32_t>(generic_update);

	if (loop_scope) {
		--scope_depth_;
		emit(opcode::op_scope_pop);
	}
	return true;
}

void vm_compiler::emit_region_exits(size_t target_region) {
	for (size_t idx = loops_.size(); idx-- > 0;) {
		if (target_region != SIZE_MAX && idx <= target_region) break;
		switch (loops_[idx].kind) {
		case region_kind::try_block:
			emit(opcode::op_try_pop);
			break;
		case region_kind::catch_block:
			emit(opcode::op_catch_end);
			break;
		case region_kind::switch_ctx:
			// Discard the switch's [value, ft] stack pair only for jumps that stay in
			// the frame; return relies on frame teardown truncating the value stack
			if (target_region != SIZE_MAX) {
				emit(opcode::op_pop);
				emit(opcode::op_pop);
			}
			break;
		default:
			break;
		}
	}
}

void vm_compiler::compile_break() {
	size_t target = SIZE_MAX;
	for (size_t idx = loops_.size(); idx-- > 0;) {
		const auto k = loops_[idx].kind;
		if (k == region_kind::loop || k == region_kind::range_for || k == region_kind::switch_ctx) {
			target = idx;
			break;
		}
	}
	if (target == SIZE_MAX) {
		emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::break_outside_loop), k_invalid_u32, k_invalid_u32);
		return;
	}
	emit_region_exits(target);
	auto& region = loops_[target];
	if (scope_depth_ > region.scope_depth) {
		emit(opcode::op_scope_pop_n, static_cast<uint32_t>(scope_depth_ - region.scope_depth));
	}
	if (region.kind == region_kind::switch_ctx) {
		emit(opcode::op_pop);
		emit(opcode::op_pop);
	}
	region.break_patches.push_back(emit(opcode::op_jump, k_invalid_u32));
}

void vm_compiler::compile_continue() {
	size_t target = SIZE_MAX;
	for (size_t idx = loops_.size(); idx-- > 0;) {
		const auto k = loops_[idx].kind;
		if (k == region_kind::loop || k == region_kind::range_for) {
			target = idx;
			break;
		}
	}
	if (target == SIZE_MAX) {
		emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::continue_outside_loop), k_invalid_u32, k_invalid_u32);
		return;
	}
	emit_region_exits(target);
	auto& region = loops_[target];
	if (scope_depth_ > region.scope_depth) {
		emit(opcode::op_scope_pop_n, static_cast<uint32_t>(scope_depth_ - region.scope_depth));
	}
	if (region.kind == region_kind::range_for) {
		emit(opcode::op_jump, static_cast<uint32_t>(region.continue_target));
	} else if (region.continue_is_loop_back) {
		emit(opcode::op_loop_back, static_cast<uint32_t>(region.continue_target));
	} else {
		region.continue_patches.push_back(emit(opcode::op_jump, k_invalid_u32));
	}
}

void vm_compiler::compile_range_for(range_for_stmt* stmt) {
	compile_expression(stmt->container);

	iter_proto proto;
	proto.var_symbol = stmt->variable_name_id != UINT64_MAX
		? stmt->variable_name_id
		: symbolizer_->intern(std::string(stmt->variable_name));
	if (stmt->variable_slot_index != SIZE_MAX && callable_.active &&
	    callable_.declared.find(proto.var_symbol) != callable_.declared.end()) {
		proto.slot = stmt->variable_slot_index;
	}
	proto.is_reference = stmt->is_reference;
	chunk_->iter_protos.push_back(proto);
	const uint32_t proto_idx = static_cast<uint32_t>(chunk_->iter_protos.size() - 1);

	emit(opcode::op_iter_init, proto_idx);
	++scope_depth_;                               // loop-variable scope pushed by op_iter_init

	const size_t loop_top = chunk_->code.size();
	emit(opcode::op_iter_next, proto_idx);        // budget check lives here (one per iteration)
	size_t exit_jump = emit(opcode::op_jump_if_false, k_invalid_u32, 1);

	loops_.push_back({});
	loops_.back().kind = region_kind::range_for;
	loops_.back().continue_target = loop_top;
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);
	emit(opcode::op_jump, static_cast<uint32_t>(loop_top));

	const size_t exit_ip = chunk_->code.size();
	patch_jump(exit_jump, exit_ip);
	for (size_t at : loops_.back().break_patches) {
		patch_jump(at, exit_ip);
	}
	loops_.pop_back();

	emit(opcode::op_iter_pop);
	--scope_depth_;
}

void vm_compiler::compile_try(try_stmt* stmt) {
	const uint32_t catch_sym = add_symbol(symbolizer_->intern(stmt->catch_var));
	// The catch variable is a frame-slot local of the catch body (compile_range_for's
	// slot test); a top-level catch binds it in a scope env pushed at handler entry
	uint32_t catch_slot = k_invalid_u32;
	if (stmt->catch_slot_index != SIZE_MAX && stmt->catch_var_id != UINT64_MAX && callable_.active &&
	    callable_.declared.find(stmt->catch_var_id) != callable_.declared.end()) {
		catch_slot = static_cast<uint32_t>(stmt->catch_slot_index);
	}
	size_t try_push = emit(opcode::op_try_push, k_invalid_u32, catch_sym, catch_slot);

	loops_.push_back({});
	loops_.back().kind = region_kind::try_block;
	loops_.back().scope_depth = scope_depth_;
	compile_statement(stmt->try_block);
	loops_.pop_back();

	emit(opcode::op_try_pop);
	size_t jump_end = emit(opcode::op_jump, k_invalid_u32);

	patch_jump(try_push, chunk_->code.size());
	loops_.push_back({});
	loops_.back().kind = region_kind::catch_block;
	loops_.back().scope_depth = scope_depth_;
	compile_statement(stmt->catch_block);
	loops_.pop_back();

	emit(opcode::op_catch_end);
	patch_jump(jump_end, chunk_->code.size());
}

void vm_compiler::compile_switch(switch_stmt* stmt) {
	compile_expression(stmt->condition);

	loops_.push_back({});
	loops_.back().kind = region_kind::switch_ctx;
	loops_.back().scope_depth = scope_depth_;
	const size_t region_index = loops_.size() - 1;

	// Mirrors the interpreter's case loop: value evaluated per case (side effects
	// included on the fallthrough entry path), == failure counts as a non-match,
	// break-by-default, runtime fallthrough flag, default checked after all cases
	std::vector<size_t> end_jumps;
	size_t prev_test_fail = SIZE_MAX;
	size_t prev_ft_jump = SIZE_MAX;

	for (size_t i = 0; i < stmt->cases.size(); ++i) {
		const auto& case_node = stmt->cases[i];
		current_stmt_ = case_node.get();

		size_t ft_to_body = SIZE_MAX;
		if (prev_ft_jump != SIZE_MAX) {
			patch_jump(prev_ft_jump, chunk_->code.size());
			compile_expression(case_node->value);
			emit(opcode::op_pop);
			emit(opcode::op_false);
			ft_to_body = emit(opcode::op_jump, k_invalid_u32);
			prev_ft_jump = SIZE_MAX;
		}

		if (prev_test_fail != SIZE_MAX) {
			patch_jump(prev_test_fail, chunk_->code.size());
		}
		emit(opcode::op_dup);
		compile_expression(case_node->value);
		emit(opcode::op_case_eq);
		prev_test_fail = emit(opcode::op_jump_if_false, k_invalid_u32, 1);
		emit(opcode::op_false);

		if (ft_to_body != SIZE_MAX) {
			patch_jump(ft_to_body, chunk_->code.size());
		}
		bool case_scope = false;
		for (const auto& body_stmt : case_node->body) {
			if (declares_in_current_scope(body_stmt.get(), callable_.active)) { case_scope = true; break; }
		}
		if (case_scope) {
			emit(opcode::op_scope_push);
			++scope_depth_;
		}
		for (const auto& body_stmt : case_node->body) {
			compile_statement(body_stmt);
		}
		if (case_scope) {
			--scope_depth_;
			emit(opcode::op_scope_pop);
		}

		current_stmt_ = case_node.get();
		prev_ft_jump = emit(opcode::op_jump_if_true, k_invalid_u32, 1);
		emit(opcode::op_pop);
		end_jumps.push_back(emit(opcode::op_jump, k_invalid_u32));
	}

	current_stmt_ = stmt;
	if (stmt->default_case) {
		const size_t default_entry = chunk_->code.size();
		if (prev_test_fail != SIZE_MAX) patch_jump(prev_test_fail, default_entry);
		if (prev_ft_jump != SIZE_MAX) patch_jump(prev_ft_jump, default_entry);
		emit(opcode::op_false);
		bool default_scope = false;
		for (const auto& body_stmt : stmt->default_case->body) {
			if (declares_in_current_scope(body_stmt.get(), callable_.active)) { default_scope = true; break; }
		}
		if (default_scope) {
			emit(opcode::op_scope_push);
			++scope_depth_;
		}
		for (const auto& body_stmt : stmt->default_case->body) {
			compile_statement(body_stmt);
		}
		if (default_scope) {
			--scope_depth_;
			emit(opcode::op_scope_pop);
		}
		current_stmt_ = stmt;
		emit(opcode::op_pop);
		emit(opcode::op_pop);
	} else {
		const size_t exit_pop = chunk_->code.size();
		if (prev_test_fail != SIZE_MAX) patch_jump(prev_test_fail, exit_pop);
		if (prev_ft_jump != SIZE_MAX) patch_jump(prev_ft_jump, exit_pop);
		emit(opcode::op_pop);
	}

	const size_t end = chunk_->code.size();
	for (size_t at : end_jumps) {
		patch_jump(at, end);
	}
	for (size_t at : loops_[region_index].break_patches) {
		patch_jump(at, end);
	}
	loops_.pop_back();
}

bool vm_compiler::compile_no_result_expression(const expression_ptr& expr) {
	if (expr->get_type() == node_type::assignment_expr) {
		auto* assign = static_cast<assignment_expr*>(expr.get());
		if (assign->target->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(assign->target.get());
			compile_expression(assign->value);
			if (assign->op.type == token_type::equal) {
				uint32_t flags = store_flag_no_result;
				if (is_lvalue_shaped(assign->value.get())) flags |= store_flag_rhs_lvalue;
				if (!ident->names_value_decl) flags |= store_flag_ref_alias;
				else if (assign->typed_store_provable) flags |= store_flag_type_provable;
				if (ident->global_qualified) flags |= store_flag_global_only;
				if (assign->static_store_proof == proof_same_type) flags |= store_flag_same_type;
				// Dest-addressed peephole, no-result-door edition (compile_assignment's
				// twin): a fused-binary RHS runs op_store's exact post-pop tail with no
				// push/pop round-trip. In-place rewrite, jump targets never shift.
				auto& code = chunk_->code;
				if (!code.empty() && code.back().op == opcode::op_binary_fused &&
				    assign->value->get_type() == node_type::binary_expr) {
					fused_binary_dst_proto dp;
					dp.binary_proto = code.back().a;
					dp.symbol = add_symbol(ident->symbol_id);
					dp.slot = identifier_slot_operand(ident);
					dp.flags = flags;
					chunk_->fused_binary_dst_protos.push_back(dp);
					vm_instruction& ins = code.back();
					ins.op = opcode::op_binary_fused_store;
					ins.a = static_cast<uint32_t>(chunk_->fused_binary_dst_protos.size() - 1);
					ins.b = 0;
					ins.c = 0;
					note_ident_node(code.size() - 1, assign->target);
					return true;
				}
				if (try_lower_store_move(ident, assign->value, flags)) return true;
				const size_t store_ip = emit(opcode::op_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
				note_ident_node(store_ip, assign->target);
			} else {
				emit_compound_store(add_symbol(ident->symbol_id), identifier_slot_operand(ident),
				     compound_kind_for(assign->op.type) | compound_flag_no_result |
				         (ident->global_qualified ? compound_flag_global_only : 0u),
				     assign->value.get());
			}
			return true;
		}
		// Statement-position subscript store: the fused op elides the result push
		// (c=1) and an ident/literal RHS rides as an operand — no LOAD, no pushes
		if (assign->op.type == token_type::equal &&
		    assign->target->get_type() == node_type::binary_expr) {
			auto* sub = static_cast<binary_expr*>(assign->target.get());
			if (sub->op.type == token_type::left_bracket &&
			    try_emit_fused_index_store(sub, assign->value, true)) {
				return true;
			}
		}
		return false;
	}
	if (expr->get_type() == node_type::unary_expr) {
		auto* un = static_cast<unary_expr*>(expr.get());
		if ((un->op.type == token_type::plus_plus || un->op.type == token_type::minus_minus) &&
		    un->operand->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(un->operand.get());
			uint32_t flags = incdec_flag_no_result;
			if (un->is_postfix) flags |= incdec_flag_postfix;
			if (un->op.type == token_type::plus_plus) flags |= incdec_flag_increment;
			if (ident->global_qualified) flags |= incdec_flag_global_only;
			emit(opcode::op_incdec, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
			return true;
		}
	}
	return false;
}

void vm_compiler::compile_expression(const expression_ptr& expr, bool as_statement) {
	switch (expr->get_type()) {
	case node_type::literal_expr:
		emit(opcode::op_const, add_constant(static_cast<literal_expr*>(expr.get())->value));
		return;
	case node_type::identifier_expr:
		compile_identifier_load(expr);
		return;
	case node_type::binary_expr:
		compile_binary(std::static_pointer_cast<binary_expr>(expr));
		return;
	case node_type::unary_expr:
		compile_unary(std::static_pointer_cast<unary_expr>(expr));
		return;
	case node_type::assignment_expr:
		compile_assignment(std::static_pointer_cast<assignment_expr>(expr), as_statement);
		return;
	case node_type::call_expr:
		if (as_statement && try_emit_fused_push(std::static_pointer_cast<call_expr>(expr))) return;
		compile_call(std::static_pointer_cast<call_expr>(expr));
		return;
	case node_type::lambda_expr:
		compile_lambda(std::static_pointer_cast<lambda_expr>(expr));
		return;
	case node_type::ternary_expr: {
		auto* t = static_cast<ternary_expr*>(expr.get());
		compile_expression(t->condition);
		size_t jump_false = emit_cond_jump_false(t->condition.get());
		compile_expression(t->then_expression);
		size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
		patch_jump(jump_false, chunk_->code.size());
		compile_expression(t->else_expression);
		patch_jump(jump_end, chunk_->code.size());
		return;
	}
	case node_type::array_literal_expr: {
		auto* arr = static_cast<array_literal_expr*>(expr.get());
		for (const auto& el : arr->elements) {
			compile_expression(el);
		}
		emit(opcode::op_array, static_cast<uint32_t>(arr->elements.size()));
		return;
	}
	case node_type::map_literal_expr: {
		auto* m = static_cast<map_literal_expr*>(expr.get());
		for (const auto& kv : m->entries) {
			compile_expression(kv.first);
			compile_expression(kv.second);
		}
		emit(opcode::op_map, static_cast<uint32_t>(m->entries.size()));
		return;
	}
	case node_type::member_expr:
		compile_member(std::static_pointer_cast<member_expr>(expr));
		return;
	case node_type::new_expr:
		compile_new(std::static_pointer_cast<new_expr>(expr));
		return;
	case node_type::cast_expr:
		compile_cast(std::static_pointer_cast<cast_expr>(expr));
		return;
	case node_type::include_expr: {
		// Expression-position include: same op_include as the statement form, minus the
		// trailing op_pop — the included file's result IS the expression value
		auto* inc = static_cast<include_expr*>(expr.get());
		uint32_t path_msg = k_invalid_u32;
		uint32_t is_expr = 0;
		if (inc->path_expr) {
			compile_expression(inc->path_expr);
			is_expr = 1;
		} else {
			path_msg = add_message(inc->path);
		}
		emit(opcode::op_include, path_msg, is_expr);
		return;
	}
	case node_type::this_expr:
		emit(opcode::op_this);
		return;
	case node_type::super_expr:
		emit(opcode::op_super);
		return;
	case node_type::throw_expr: {
		auto* te = static_cast<throw_expr*>(expr.get());
		uint32_t has_value = 0;
		if (te->value) {
			compile_expression(te->value);
			has_value = 1;
		}
		emit(opcode::op_throw, has_value, add_node(expr));
		return;
	}
	case node_type::yield_expr: {
		auto* ye = static_cast<yield_expr*>(expr.get());
		uint32_t has_value = 0;
		if (ye->value) {
			compile_expression(ye->value);
			has_value = 1;
		}
		emit(opcode::op_yield, has_value);
		return;
	}
	default:
		emit_unsupported("expression", "B2");
		return;
	}
}

void vm_compiler::compile_identifier_load(const expression_ptr& expr) {
	auto* ident = static_cast<identifier_expr*>(expr.get());
	uint32_t flags = 0;
	if (ident->global_qualified) {
		flags |= load_flag_global_only;
	}
	if (!ident->name.empty() && (ident->name.front() == 'w' || ident->name.front() == 's') &&
	    (ident->name.find("weak_ptr<") == 0 || ident->name.find("shared_ptr<") == 0)) {
		flags |= load_flag_type_ctor;
	}
	const size_t ip = emit(opcode::op_load, identifier_slot_operand(ident), add_symbol(ident->symbol_id), flags);
	note_ident_node(ip, expr);
}

void vm_compiler::note_ident_node(size_t ip, const expression_ptr& ident) {
	if (identifier_slot_operand(static_cast<identifier_expr*>(ident.get())) != k_invalid_u32) {
		return;
	}
	auto& table = chunk_->ident_nodes;
	if (table.size() <= ip) {
		table.resize(ip + 1, k_invalid_u32);
	}
	table[ip] = add_node(ident);
}

fused_operand vm_compiler::make_fused_operand(const expression_ptr& e) {
	fused_operand out = make_fused_operand(e.get());
	if (e->get_type() == node_type::identifier_expr && out.slot == k_invalid_u32) {
		out.ident_node = add_node(e);
	}
	return out;
}

// INDEX-fusion container `base.member[...]`: plain non-static member off an identifier
// (the exec resolves it base→member→index, matching the unfused dispatch order)
static member_expr* fusable_member_container(expression* e) {
	if (e->get_type() != node_type::member_expr) return nullptr;
	auto* m = static_cast<member_expr*>(e);
	if (m->is_static || m->object->get_type() != node_type::identifier_expr) return nullptr;
	return m;
}

// INDEX-fusion computed index candidate: a binary whose op keeps no jump semantics
// (short-circuit) and is not itself a subscript; the caller still vets binary_shape
static binary_expr* index_binary_candidate(expression* e) {
	if (e->get_type() != node_type::binary_expr) return nullptr;
	auto* b = static_cast<binary_expr*>(e);
	if (b->op.type == token_type::left_bracket ||
	    b->op.type == token_type::ampersand_ampersand ||
	    b->op.type == token_type::pipe_pipe) {
		return nullptr;
	}
	return b;
}

static bool is_index_chain_arith(token_type op) {
	switch (op) {
	case token_type::plus:
	case token_type::minus:
	case token_type::star:
	case token_type::slash:
	case token_type::percent:
		return true;
	default:
		return false;
	}
}

// Left-spine walk for chained computed indexes (((a op b) op c) op d …): collects the
// spine outermost-first. Descends while the step is arithmetic with an ident/literal
// right and the left is another candidate binary; spine[depth-1] is the innermost node
// (the caller vets its binary_shape). 0 = shape violation or too deep.
static int index_chain_spine(binary_expr* rb, binary_expr* (&spine)[8]) {
	int depth = 0;
	binary_expr* cur = rb;
	while (depth < 8) {
		spine[depth++] = cur;
		binary_expr* l = index_binary_candidate(cur->left.get());
		if (!l) {
			return depth;   // innermost reached (left is ident/lit/subscript-chain)
		}
		// descending: this node is a chain STEP — arithmetic, ident/literal right
		if (!is_index_chain_arith(cur->op.type) ||
		    (cur->right->get_type() != node_type::identifier_expr &&
		     cur->right->get_type() != node_type::literal_expr)) {
			return 0;
		}
		cur = l;
	}
	return 0;
}

// Proof-grade operand class onto a fused proto (typed heads): the checker stamped the
// node's operand kinds in every mode (cleared when proofs are off); both operands must
// address directly (frame slot, temp or literal) and the op must be one the heads compute.
static void stamp_typed_class(fused_binary_proto& p, const binary_expr* node) {
	auto direct_or_literal = [](const fused_operand& o) { return o.direct || o.const_index != k_invalid_u32; };
	if (!direct_or_literal(p.left) || !direct_or_literal(p.right)) return;
	switch (static_cast<token_type>(p.op)) {
		case token_type::plus: case token_type::minus: case token_type::star: case token_type::slash: case token_type::percent:
		case token_type::less: case token_type::less_equal: case token_type::greater: case token_type::greater_equal:
		case token_type::equal_equal: case token_type::bang_equal:
			break;
		case token_type::ampersand: case token_type::pipe: case token_type::caret:
		case token_type::left_shift: case token_type::right_shift:
			// integer-only rows: the float head has no bitwise row, so the float class stays unstamped
			if (node->static_operand_class == operand_class_int_int) { p.typed_class = 1; }
			return;
		default:
			return;
	}
	if (node->static_operand_class == operand_class_int_int) { p.typed_class = 1; }
	else if (node->static_operand_class == operand_class_float_float) { p.typed_class = 2; }
}

chunk::typed_site& vm_compiler::typed_site_at(size_t ip) {
	auto& sites = chunk_->typed_sites;
	if (sites.size() <= ip) { sites.resize(ip + 1); }
	return sites[ip];
}

// A scalar-store decl (int/float/bool/char/string/var/auto in a frame slot, not escape-boxed)
// initialized from a plain slot identifier or a scalar literal: the LOAD or CONST just
// emitted becomes one typed move and no DECL_VAR follows. The op's guard is the decl fast
// path's admission; its fallback is the pair it replaced.
bool vm_compiler::try_lower_decl_move(const std::shared_ptr<variable_decl>& decl) {
	if (!typed_lowering_ || !callable_.active) return false;
	if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store ||
	    decl->ref_escaping || decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask) return false;
	auto& code = chunk_->code;
	if (code.empty()) return false;
	vm_instruction& last = code.back();
	const size_t ip = code.size() - 1;
	if (decl->initializer->get_type() == node_type::identifier_expr && last.op == opcode::op_load &&
	    last.a != k_invalid_u32 && last.c == 0) {
		const vm_instruction orig = last;
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = orig;
		site.orig2 = vm_instruction{opcode::op_decl_var, add_node(decl), 1, 1};
		site.stamp = decl->type.get();
		last = vm_instruction{opcode::op_t_decl_mov, static_cast<uint32_t>(decl->slot_index), orig.a, site.orig2.a};
		return true;
	}
	if (decl->initializer->get_type() == node_type::literal_expr && last.op == opcode::op_const) {
		const size_t ci = chunk_->constants[last.a].raw_storage_index();
		if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_BOOL &&
		    ci != script_value::TYPEID_CHAR && ci != script_value::TYPEID_STRING) return false;
		const vm_instruction orig = last;
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = orig;
		site.orig2 = vm_instruction{opcode::op_decl_var, add_node(decl), 1, 0};
		site.stamp = decl->type.get();
		last = vm_instruction{opcode::op_t_decl_const, static_cast<uint32_t>(decl->slot_index), orig.a, site.orig2.a};
		return true;
	}
	return false;
}

// `x = y;` / `x = 5;` in statement position onto a parse-proven typed value slot with a
// same-type proof: the LOAD or CONST becomes one typed move and no STORE follows
bool vm_compiler::try_lower_store_move(identifier_expr* ident, const expression_ptr& value, uint32_t flags) {
	if (!typed_lowering_ || !callable_.active) return false;
	const uint32_t need = store_flag_no_result | store_flag_same_type | store_flag_type_provable;
	if ((flags & need) != need || (flags & (store_flag_ref_alias | store_flag_global_only))) return false;
	const uint32_t dst = identifier_slot_operand(ident);
	if (dst == k_invalid_u32 || dst >= k_typed_slot_mask) return false;
	auto& code = chunk_->code;
	if (code.empty()) return false;
	vm_instruction& last = code.back();
	const size_t ip = code.size() - 1;
	if (value->get_type() == node_type::identifier_expr && last.op == opcode::op_load &&
	    last.a != k_invalid_u32 && last.c == 0) {
		const vm_instruction orig = last;
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = orig;
		site.orig2 = vm_instruction{opcode::op_store, add_symbol(ident->symbol_id), dst, flags};
		last = vm_instruction{opcode::op_t_store_mov, dst, orig.a, flags};
		return true;
	}
	if (value->get_type() == node_type::literal_expr && last.op == opcode::op_const) {
		const size_t ci = chunk_->constants[last.a].raw_storage_index();
		if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_BOOL &&
		    ci != script_value::TYPEID_CHAR && ci != script_value::TYPEID_STRING) return false;
		const vm_instruction orig = last;
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = orig;
		site.orig2 = vm_instruction{opcode::op_store, add_symbol(ident->symbol_id), dst, flags};
		last = vm_instruction{opcode::op_t_store_const, dst, orig.a, flags};
		return true;
	}
	return false;
}

// The typed scalar lowering (docs/typed_regions_design.md, stage 1): a fused binary or
// compare-jump whose proto carries the checker's int/int or float/float class and whose
// operands are direct slots, temps or literals becomes one op_t_* with the slots in the
// instruction word; the original instruction waits in typed_sites for any guard miss.
// In place, after every jump target is final, so nothing shifts.
static bool index_proto_typed_lane_shape(const chunk& code, const fused_index_proto& p);   // defined with the index lanes below
static bool index_proto_speculative_shape(const chunk& code, const fused_index_proto& p);

void vm_compiler::lower_typed_ops(chunk& c) {
	if (!typed_lowering_) return;
	auto arith_op = [](token_type t, uint8_t cls) -> opcode {
		if (cls == 1) {
			switch (t) {
			case token_type::plus: return opcode::op_t_add_ii;
			case token_type::minus: return opcode::op_t_sub_ii;
			case token_type::star: return opcode::op_t_mul_ii;
			case token_type::slash: return opcode::op_t_div_ii;
			case token_type::percent: return opcode::op_t_mod_ii;
			case token_type::ampersand: return opcode::op_t_and_ii;
			case token_type::pipe: return opcode::op_t_or_ii;
			case token_type::caret: return opcode::op_t_xor_ii;
			case token_type::left_shift: return opcode::op_t_shl_ii;
			case token_type::right_shift: return opcode::op_t_shr_ii;
			default: return opcode::op_halt;
			}
		}
		switch (t) {
		case token_type::plus: return opcode::op_t_add_ff;
		case token_type::minus: return opcode::op_t_sub_ff;
		case token_type::star: return opcode::op_t_mul_ff;
		case token_type::slash: return opcode::op_t_div_ff;
		case token_type::percent: return opcode::op_t_mod_ff;
		default: return opcode::op_halt;
		}
	};
	auto cmp_op = [](token_type t, uint8_t cls) -> opcode {
		if (cls == 1) {
			switch (t) {
			case token_type::less: return opcode::op_t_jlt_ii;
			case token_type::less_equal: return opcode::op_t_jle_ii;
			case token_type::greater: return opcode::op_t_jgt_ii;
			case token_type::greater_equal: return opcode::op_t_jge_ii;
			case token_type::equal_equal: return opcode::op_t_jeq_ii;
			case token_type::bang_equal: return opcode::op_t_jne_ii;
			default: return opcode::op_halt;
			}
		}
		switch (t) {
		case token_type::less: return opcode::op_t_jlt_ff;
		case token_type::less_equal: return opcode::op_t_jle_ff;
		case token_type::greater: return opcode::op_t_jgt_ff;
		case token_type::greater_equal: return opcode::op_t_jge_ff;
		case token_type::equal_equal: return opcode::op_t_jeq_ff;
		case token_type::bang_equal: return opcode::op_t_jne_ff;
		default: return opcode::op_halt;
		}
	};
	auto mirrored = [](token_type t) -> token_type {
		switch (t) {
		case token_type::less: return token_type::greater;
		case token_type::less_equal: return token_type::greater_equal;
		case token_type::greater: return token_type::less;
		case token_type::greater_equal: return token_type::less_equal;
		default: return t;   // + * & | ^ == != commute
		}
	};
	auto commutes = [](token_type t) {
		switch (t) {
		case token_type::plus: case token_type::star: case token_type::ampersand: case token_type::pipe: case token_type::caret:
		case token_type::equal_equal: case token_type::bang_equal:
		case token_type::less: case token_type::less_equal: case token_type::greater: case token_type::greater_equal:
			return true;
		default: return false;
		}
	};
	// left slot + right (slot or constant of the class); a literal left swaps when the op allows
	auto operands = [&](const fused_binary_proto& bp, token_type& op, uint32_t& left, uint32_t& right) -> bool {
		const size_t want = bp.typed_class == 1 ? script_value::TYPEID_INT : script_value::TYPEID_FLOAT;
		auto direct_slot = [&](const fused_operand& o, uint32_t& out) {
			if (!o.direct || o.slot == k_invalid_u32 || o.slot >= k_typed_slot_mask || o.const_index != k_invalid_u32) return false;
			out = o.slot; return true;
		};
		auto constant = [&](const fused_operand& o, uint32_t& out) {
			if (o.const_index == k_invalid_u32 || o.const_index >= k_typed_slot_mask) return false;
			if (c.constants[o.const_index].raw_storage_index() != want) return false;
			out = k_typed_const_bit | o.const_index; return true;
		};
		uint32_t l = 0, r = 0;
		if (direct_slot(bp.left, l) && (direct_slot(bp.right, r) || constant(bp.right, r))) { left = l; right = r; return true; }
		if (constant(bp.left, r) && direct_slot(bp.right, l) && commutes(op)) { op = mirrored(op); left = l; right = r; return true; }
		return false;
	};
	uint32_t one_const = k_invalid_u32;   // the increment's operand, minted once per chunk
	auto& code = c.code;
	for (size_t ip = 0; ip < code.size(); ++ip) {
		vm_instruction& ins = code[ip];
		switch (ins.op) {
		case opcode::op_binary_fused_temp: {
			const fused_binary_proto& bp = c.fused_binary_protos[ins.a];
			if (bp.typed_class == 0 || ins.b >= k_typed_slot_mask) break;
			token_type op = static_cast<token_type>(bp.op);
			uint32_t left, right;
			if (!operands(bp, op, left, right)) break;
			const opcode t = arith_op(op, bp.typed_class);
			if (t == opcode::op_halt) break;
			chunk_ = &c;
			typed_site_at(ip).orig = ins;
			ins = vm_instruction{t, ins.b | (k_typed_dst_temp << k_typed_dst_shift), left, right};
			break;
		}
		case opcode::op_binary_fused_decl:
		case opcode::op_binary_fused_store: {
			const fused_binary_dst_proto& dp = c.fused_binary_dst_protos[ins.a];
			const fused_binary_proto& bp = c.fused_binary_protos[dp.binary_proto];
			if (bp.typed_class == 0) break;
			token_type op = static_cast<token_type>(bp.op);
			uint32_t left, right;
			if (!operands(bp, op, left, right)) break;
			const opcode t = arith_op(op, bp.typed_class);
			if (t == opcode::op_halt) break;
			uint32_t dst;
			type_info* stamp = nullptr;
			if (ins.op == opcode::op_binary_fused_decl) {
				const auto* decl = static_cast<const variable_decl*>(c.nodes[dp.node_index].get());
				if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store ||
				    decl->ref_escaping || decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask) break;
				const script_value_type bt = decl->type ? decl->type->base_type : script_value_type::jai_any_type;
				const bool fits = bt == script_value_type::jai_any_type ||
				                  (bp.typed_class == 1 && bt == script_value_type::jai_int_type) ||
				                  (bp.typed_class == 2 && bt == script_value_type::jai_float_type);
				if (!fits) break;
				dst = static_cast<uint32_t>(decl->slot_index) | (k_typed_dst_decl << k_typed_dst_shift);
				stamp = decl->type.get();
			} else {

				const uint32_t need = store_flag_no_result | store_flag_same_type | store_flag_type_provable;
				if ((dp.flags & need) != need || (dp.flags & (store_flag_ref_alias | store_flag_global_only))) break;
				if (dp.slot == k_invalid_u32 || dp.slot >= k_typed_slot_mask) break;
				dst = dp.slot | (k_typed_dst_store << k_typed_dst_shift);
			}
			chunk_ = &c;
			chunk::typed_site& site = typed_site_at(ip);
			site.orig = ins;
			site.stamp = stamp;
			ins = vm_instruction{t, dst, left, right};
			break;
		}
		case opcode::op_incdec: {
			// `++x` / `x--` in statement position on a direct slot: the int compound row in place (the
			// landing `x += 1` takes); the slot's tag is the guard, a miss runs the general op
			if (!(ins.c & incdec_flag_no_result) || (ins.c & incdec_flag_global_only) || ins.b == k_invalid_u32 || ins.b >= k_typed_slot_mask) break;
			chunk_ = &c;
			if (one_const == k_invalid_u32) { one_const = add_constant(script_value(static_cast<script_int>(1), static_cast<engine*>(nullptr))); }
			if (one_const >= k_typed_slot_mask) break;
			typed_site_at(ip).orig = ins;
			ins = vm_instruction{(ins.c & incdec_flag_increment) ? opcode::op_t_add_ii : opcode::op_t_sub_ii,
				ins.b | (k_typed_dst_compound << k_typed_dst_shift), ins.b, k_typed_const_bit | one_const};
			break;
		}
		case opcode::op_compound_fused: {
			// `x op= y` (bare rhs, statement position) on a direct slot: the compound lane's int
			// row in place; kind_flags beyond no_result/incdec keep the general op
			const compound_fused_proto& cp = c.compound_fused_protos[ins.a];
			if (cp.rhs_proto != k_invalid_u32 || cp.slot == k_invalid_u32 || cp.slot >= k_typed_slot_mask) break;
			if ((cp.kind_flags & ~(compound_kind_mask | compound_flag_no_result | compound_flag_incdec)) != 0) break;
			if (!(cp.kind_flags & compound_flag_no_result)) break;
			opcode t;
			switch (cp.kind_flags & compound_kind_mask) {
			case compound_plus: t = opcode::op_t_add_ii; break;
			case compound_minus: t = opcode::op_t_sub_ii; break;
			case compound_star: t = opcode::op_t_mul_ii; break;
			case compound_slash: t = opcode::op_t_div_ii; break;
			default: t = opcode::op_halt; break;
			}
			if (t == opcode::op_halt) break;
			uint32_t right;
			if (cp.rhs.const_index != k_invalid_u32) {
				if (cp.rhs.const_index >= k_typed_slot_mask || c.constants[cp.rhs.const_index].raw_storage_index() != script_value::TYPEID_INT) break;
				right = k_typed_const_bit | cp.rhs.const_index;
			} else if (cp.rhs.slot != k_invalid_u32 && cp.rhs.load_flags == 0 && cp.rhs.slot < k_typed_slot_mask) {
				right = cp.rhs.slot;
			} else {
				break;
			}
			chunk_ = &c;
			typed_site_at(ip).orig = ins;
			ins = vm_instruction{t, cp.slot | (k_typed_dst_compound << k_typed_dst_shift), cp.slot, right};
			break;
		}
		case opcode::op_index_fused_decl: {
			// the proven element read landing in a decl (typed_decl_read's shape) with at most
			// two chain steps: flatten the proto into the site row
			const fused_index_proto& fp = c.fused_index_protos[ins.a];
			if (!fp.typed_lane || fp.decl_slot == k_invalid_u32 || fp.decl_slot >= k_typed_slot_mask) break;
			if (fp.index_chain_ext.size() > 2) break;
			auto operand_of = [&](const fused_operand& o, uint32_t& out) -> bool {
				if (o.const_index != k_invalid_u32) {
					if (o.const_index >= k_typed_slot_mask) return false;
					out = k_typed_const_bit | o.const_index;
					return true;
				}
				if (!o.direct || o.slot == k_invalid_u32 || o.slot >= k_typed_slot_mask) return false;
				out = o.slot;
				return true;
			};
			chunk::typed_site site;
			site.cont_slot = fp.container.direct && fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask
				? fp.container.slot : k_invalid_u32;
			bool ok = true;
			if (fp.index_binary == k_invalid_u32) {
				ok = operand_of(fp.index, site.operands[0]);
			} else {
				const fused_binary_proto& bp = c.fused_binary_protos[fp.index_binary];
				ok = operand_of(bp.left, site.operands[0]) && operand_of(bp.right, site.operands[1]);
				site.fold_ops[0] = bp.op;
				site.fold_count = 1;
				for (size_t k = 0; ok && k < fp.index_chain_ext.size(); ++k) {
					const fused_binary_proto& sp = c.fused_binary_protos[fp.index_chain_ext[k]];
					ok = operand_of(sp.right, site.operands[k + 2]);
					site.fold_ops[k + 1] = sp.op;
					site.fold_count = static_cast<uint8_t>(k + 2);
				}
			}
			if (!ok) break;
			site.orig = ins;
			site.stamp = fp.decl_type;
			site.decl_expect = fp.decl_expect;
			site.temp = fp.decl_temp;
			site.speculative = fp.speculative;
			site.guard_temp = fp.container_guard;
			chunk_ = &c;
			typed_site_at(ip) = site;
			ins = vm_instruction{opcode::op_t_load, fp.decl_slot, 0, 0};
			break;
		}
		case opcode::op_push_fused: {
			// a slot receiver and a slot or literal value on a site the builtin index knows:
			// the append rows of exec_push_fused with nothing to decode
			const push_proto& pp = c.push_protos[ins.a];
			if (!c.builtin_indexed || c.call_sites[pp.site].bi_array == builtin_method_registries::k_no_builtin) break;
			if (!pp.receiver.direct || pp.receiver.slot == k_invalid_u32 || pp.receiver.slot >= k_typed_slot_mask || pp.receiver.const_index != k_invalid_u32) break;
			uint32_t value;
			if (pp.value.const_index != k_invalid_u32) {
				if (pp.value.const_index >= k_typed_slot_mask) break;
				value = k_typed_const_bit | pp.value.const_index;
			} else if (!pp.value_is_subscript && pp.value.direct && pp.value.slot != k_invalid_u32 && pp.value.slot < k_typed_slot_mask) {
				value = pp.value.slot;
			} else {
				break;
			}
			chunk_ = &c;
			typed_site_at(ip).orig = ins;
			ins = vm_instruction{opcode::op_t_push, pp.receiver.slot, value, ins.a};
			break;
		}
		case opcode::op_fused_cmp_jump: {
			const fused_binary_proto& bp = c.fused_binary_protos[ins.b];
			if (bp.typed_class == 0) break;
			token_type op = static_cast<token_type>(bp.op);
			uint32_t left, right;
			if (!operands(bp, op, left, right)) break;
			const opcode t = cmp_op(op, bp.typed_class);
			if (t == opcode::op_halt) break;
			chunk_ = &c;
			typed_site_at(ip).orig = ins;
			ins = vm_instruction{t, left, right, ins.a};
			break;
		}
		default:
			break;
		}
	}
	// Cell blocks (design: out/fable/design_row116_cell_ir.md, docs/typed_regions_design.md "The contract"). A loop closes
	// when every instruction of its body translates into a canonical operation (arith, mov, cmp with its test, load, store,
	// math, jump, guard, the back edge) over admitted values (semantic locals, compiler values, environment values,
	// container bindings, snapshots, literals) and the legality pass accepts the result; anything else is a refusal with a
	// class and the first offending instruction (chunk::cell_refusals: the profile's open-loop table, the disassembly).
	// Translation runs here, once per chunk after the typed lowering; the runtime executes the copy and translates
	// nothing. Row 116a's encoding is one cell instruction per original instruction with an explicit origin table.
	const size_t original_size = code.size();
	enum class ir : uint8_t { arith, mov, cmp, load, store, math, jump, guard, placeholder };
	struct ir_op {
		ir kind = ir::placeholder;
		uint32_t a = 0, b = 0, c = 0;            // operands: a slot, k_typed_const_bit | constant, or k_ir_env_bit | env value position
		opcode fast = opcode::op_halt;           // the pinned-kind form (op_t_*) when the typed lowering produced it, else the any-kind form
		uint8_t tok = 0;                         // the token for arith and cmp, the math_fn for math
		uint32_t target = 0;                     // cmp and jump: the original target ip (the exit for a head)
		uint32_t proto = k_invalid_u32;          // load: a general read's fused_index_protos index (k_invalid_u32 = a T_LOAD's site); store: the proto
		uint32_t guarded = 0xFFFFFFu;            // guard: the guarded read's offset from the head
		uint32_t reads[8] = {}; uint8_t nreads = 0;   // the slots the operation reads (the verifier's use list)
		uint32_t writes = k_invalid_u32;         // the slot the operation writes (a local or a compiler value)
		uint32_t container = k_invalid_u32;      // load and store: the container slot when the binding is a frame slot
		bool head = false;
		bool sense = false;                      // cmp: jump when true (a comparison value followed by JUMP_IF_TRUE)
	};
	constexpr uint32_t k_ir_env_bit = 0x40000000u;
	auto stamp_kind = [](const type_info* t) -> uint8_t {
		if (!t) return 0;
		if (t->base_type == script_value_type::jai_int_type) return static_cast<uint8_t>(script_value::TYPEID_INT);
		if (t->base_type == script_value_type::jai_float_type) return static_cast<uint8_t>(script_value::TYPEID_FLOAT);
		return 0;
	};
	auto arith_tok = [](uint8_t op) {
		switch (static_cast<token_type>(op)) {
		case token_type::plus: case token_type::minus: case token_type::star: case token_type::slash: case token_type::percent:
		case token_type::ampersand: case token_type::pipe: case token_type::caret: case token_type::left_shift: case token_type::right_shift:
			return true;
		default: return false;
		}
	};
	auto cmp_tok = [](uint8_t op) {
		switch (static_cast<token_type>(op)) {
		case token_type::less: case token_type::less_equal: case token_type::greater: case token_type::greater_equal:
		case token_type::equal_equal: case token_type::bang_equal:
			return true;
		default: return false;
		}
	};
	auto int_const = [&](const fused_operand& o) {
		return o.const_index != k_invalid_u32 && o.const_index < c.constants.size() && o.const_index < k_typed_slot_mask && c.constants[o.const_index].raw_storage_index() == script_value::TYPEID_INT;
	};
	auto direct_slot = [](const fused_operand& o) {
		return o.const_index == k_invalid_u32 && o.direct && o.slot != k_invalid_u32 && o.slot < k_typed_slot_mask && !o.is_subscript() && o.member_node == k_invalid_u32;
	};
	// an environment value (a global such as a view width): a body has no call and no store to a global, so the entry
	// reads it once into a read-only value with a synthetic slot above the frame's
	auto env_scalar = [](const fused_operand& o) {
		return o.slot == k_invalid_u32 && !o.direct && o.const_index == k_invalid_u32 && o.symbol != k_invalid_u32 &&
		       !o.is_subscript() && o.member_node == k_invalid_u32 && o.load_flags == 0;
	};
	// a container binding: a frame slot (a value or a reference the entry resolves) or an environment array
	auto container_operand = [&](const fused_operand& o) {
		if (o.const_index != k_invalid_u32 || o.member_node != k_invalid_u32 || o.is_subscript() || o.load_flags != 0) return false;
		return o.slot != k_invalid_u32 || o.symbol != k_invalid_u32;
	};
	const uint8_t kint = static_cast<uint8_t>(script_value::TYPEID_INT), kflt = static_cast<uint8_t>(script_value::TYPEID_FLOAT);
	for (size_t back = 0; back < original_size; ++back) {
		const vm_instruction be = code[back];
		if (be.op != opcode::op_loop_back && be.op != opcode::op_cfor_back) continue;
		size_t head; vm_instruction hd{}; uint32_t counted = k_invalid_u32; size_t cmp_ip = 0;
		uint8_t refusal = chunk::cell_refusal_none; uint32_t refusal_ip = static_cast<uint32_t>(back);
		auto refuse = [&](uint8_t why, size_t at) { if (refusal == chunk::cell_refusal_none) { refusal = why; refusal_ip = static_cast<uint32_t>(at); } };
		if (be.op == opcode::op_loop_back) {
			head = be.a;
			if (head >= back) continue;
			// a counted loop's generic fallback (its op_loop_back spans the counted back edge) is the same loop, not a second one
			bool generic_of_counted = false;
			for (size_t ip = head + 1; ip < back; ++ip) { if (code[ip].op == opcode::op_cfor_back || code[ip].op == opcode::op_cells_cfor_back) { generic_of_counted = true; break; } }
			if (generic_of_counted) continue;
			// the head compare: a typed compare-jump or a general one whose false edge is the exit; the instructions before it
			// (the condition's temps and reads) are the head span, translated like the body and run every iteration
			cmp_ip = head;
			for (; cmp_ip < back; ++cmp_ip) {
				const vm_instruction& ci = code[cmp_ip];
				if (ci.op >= opcode::op_t_jlt_ii && ci.op <= opcode::op_t_jne_ff && ci.c == back + 1) break;
				if (ci.op == opcode::op_fused_cmp_jump && ci.a == back + 1 && ci.b < c.fused_binary_protos.size()) break;
			}
			if (cmp_ip >= back) { refuse(chunk::cell_refusal_head, head); cmp_ip = head; }
			hd = code[cmp_ip];
		} else {
			if (be.a >= c.counted_for_protos.size()) continue;
			const counted_for_proto& p = c.counted_for_protos[be.a];
			if (p.body_ip == k_invalid_u32 || p.body_ip == 0 || p.body_ip > back) continue;
			head = p.body_ip - 1;
			counted = be.a;
			if (!direct_slot(p.var) || !(int_const(p.end) || direct_slot(p.end)) || !(int_const(p.step) || direct_slot(p.step))) { refuse(chunk::cell_refusal_head, head); }
		}
		// the cells: semantic locals and compiler values by slot, environment values by symbol; kind 0 = adopted from the
		// slot at entry, a typed use pins it; a landing's contract belongs to the operation that lands (stamp, landing)
		std::vector<chunk::cell_loop::cell> cells;
		std::vector<chunk::cell_loop::env_operand_ref> env_ops;
		auto find_cell = [&](uint32_t slot) -> int { for (size_t k = cells.size(); k-- > 0;) { if (cells[k].env_operand == k_invalid_u32 && cells[k].slot == slot) return static_cast<int>(k); } return -1; };   // the slot's latest definition
		std::vector<std::pair<uint32_t, uint32_t>> defs;   // (ip, cell): each compiler value's definition in program order, the emission's binding map
		auto use_slot = [&](uint32_t operand, uint8_t kind, bool is_write, uint8_t landing, type_info* stamp, size_t at) {
			if (operand & k_typed_const_bit) return;
			const uint32_t slot = operand & k_typed_slot_mask;
			int k = find_cell(slot);
			if (k < 0) {
				chunk::cell_loop::cell nc{ slot, kind, 0, !is_write, false, nullptr };
				cells.push_back(nc); k = static_cast<int>(cells.size() - 1);
			} else if (is_write && landing == k_typed_dst_temp && cells[k].temp && cells[k].kind != kind) {
				// a compiler value is a definition, not a slot: a temp redefined with another kind (the compiler reuses temp
				// slots across statements) is a new cell of the same slot; reads bind to the latest definition
				chunk::cell_loop::cell nc{ slot, kind, 0, false, false, nullptr };
				cells.push_back(nc); k = static_cast<int>(cells.size() - 1);
			} else if (cells[k].kind == 0) { cells[k].kind = kind; }   // an adopted kind takes the first typed use's
			else if (kind != 0 && cells[k].kind != kind) { refuse(chunk::cell_refusal_representation, at); return; }   // two kinds on one local: the general form's conversion
			if (is_write) {
				cells[k].defined = true; cells[k].landing = landing; cells[k].stamp = stamp;
				if (landing == k_typed_dst_temp) { cells[k].temp = true; defs.emplace_back(static_cast<uint32_t>(at), static_cast<uint32_t>(k)); }
			}
		};
		auto env_cell = [&](const fused_operand& o, uint8_t kind, uint32_t cache_slot, size_t at) -> uint32_t {
			for (size_t e = 0; e < env_ops.size(); ++e) {
				if (env_ops[e].operand.symbol != o.symbol) continue;
				for (auto& cl : cells) {
					if (cl.env_operand == e) { if (cl.kind == 0) { cl.kind = kind; } else if (kind != 0 && cl.kind != kind) { refuse(chunk::cell_refusal_representation, at); } }
				}
				return static_cast<uint32_t>(e);
			}
			chunk::cell_loop::cell nc{ k_invalid_u32, kind, 0, true, false, nullptr, static_cast<uint32_t>(env_ops.size()) };
			cells.push_back(nc);
			env_ops.push_back(chunk::cell_loop::env_operand_ref{ o, cache_slot });
			return static_cast<uint32_t>(env_ops.size() - 1);
		};
		// a value operand: an int or float literal, a frame slot (a cell of the given or adopted kind) or an environment value
		auto value = [&](ir_op& op, const fused_operand& o, uint8_t kind, uint32_t cache_slot, size_t at, uint32_t& out) -> bool {
			if (o.const_index != k_invalid_u32) {
				if (o.const_index >= c.constants.size() || o.const_index >= k_typed_slot_mask) return false;
				const size_t ci = c.constants[o.const_index].raw_storage_index();
				if (kind == kint ? ci != script_value::TYPEID_INT : (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT)) return false;
				out = k_typed_const_bit | o.const_index;
				return true;
			}
			if (direct_slot(o)) { use_slot(o.slot, kind, false, 0, nullptr, at); out = o.slot; if (op.nreads < 8) { op.reads[op.nreads++] = o.slot; } return true; }
			if (env_scalar(o)) { out = k_ir_env_bit | env_cell(o, kind, cache_slot, at); return true; }
			return false;
		};
		auto slot_read = [&](ir_op& op, uint32_t operand) { if (!(operand & k_typed_const_bit) && op.nreads < 8) { op.reads[op.nreads++] = operand & k_typed_slot_mask; } };
		// the index of a load or store: a value or the fold over values; every operand an int
		auto index_values = [&](ir_op& op, const fused_index_proto& fp, size_t ip) -> bool {
			uint32_t sink;
			if (fp.index_binary == k_invalid_u32) return value(op, fp.index, kint, static_cast<uint32_t>(ip * 3), ip, sink);
			if (fp.index_binary >= c.fused_binary_protos.size()) return false;
			const fused_binary_proto& bp = c.fused_binary_protos[fp.index_binary];
			bool ok = is_index_chain_arith(static_cast<token_type>(bp.op)) && value(op, bp.left, kint, static_cast<uint32_t>(ip * 3), ip, sink) && value(op, bp.right, kint, static_cast<uint32_t>(ip * 3 + 1), ip, sink);
			for (const uint32_t step : fp.index_chain_ext) {
				ok = ok && step < c.fused_binary_protos.size() && is_index_chain_arith(static_cast<token_type>(c.fused_binary_protos[step].op)) && value(op, c.fused_binary_protos[step].right, kint, static_cast<uint32_t>(ip * 3 + 1), ip, sink);
			}
			return ok;
		};
		// one operation per instruction of the loop span: a compare-headed loop from its head (the head span, then the
		// compare, then the body), a counted loop from its body behind a placeholder
		std::vector<ir_op> ops;
		ir_op head_op;
		const size_t first = counted != k_invalid_u32 ? head + 1 : head;
		if (refusal == chunk::cell_refusal_none && counted != k_invalid_u32) {
			const counted_for_proto& p = c.counted_for_protos[counted];
			use_slot(p.var.slot, kint, true, k_typed_dst_compound, nullptr, head);   // stepped in place by the back edge
			if (p.end.const_index == k_invalid_u32) { use_slot(p.end.slot, kint, false, 0, nullptr, head); }
			if (p.step.const_index == k_invalid_u32) { use_slot(p.step.slot, kint, false, 0, nullptr, head); }
			head_op.kind = ir::placeholder;
		}
		auto int_one = [&]() -> uint32_t {   // the literal 1 of ++x and x--, minted once per chunk
			for (size_t k = 0; k < c.constants.size() && k < k_typed_slot_mask; ++k) { if (c.constants[k].raw_storage_index() == script_value::TYPEID_INT && c.constants[k].unchecked_as_int() == 1) return static_cast<uint32_t>(k); }
			c.constants.push_back(script_value(static_cast<script_int>(1), static_cast<engine*>(nullptr)));
			return static_cast<uint32_t>(c.constants.size() - 1);
		};
		for (size_t ip = first; ip < back && refusal == chunk::cell_refusal_none; ++ip) {
			const vm_instruction st = code[ip];
			ir_op op;
			if (counted == k_invalid_u32 && ip == cmp_ip) {
				// the head compare: its false edge is the loop's exit
				op.kind = ir::cmp; op.head = true;
				if (st.op == opcode::op_fused_cmp_jump) {
					const fused_binary_proto& bp = c.fused_binary_protos[st.b];
					op.tok = bp.op; op.target = st.a;
					if (!cmp_tok(bp.op) || !value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.a) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.b)) { refuse(chunk::cell_refusal_head, ip); break; }
				} else {
					const uint8_t head_kind = st.op <= opcode::op_t_jne_ii ? kint : kflt;
					use_slot(st.a, head_kind, false, 0, nullptr, ip); use_slot(st.b, head_kind, false, 0, nullptr, ip);
					op.fast = st.op; op.a = st.a; op.b = st.b; op.target = st.c; slot_read(op, st.a); slot_read(op, st.b);
				}
			} else if (st.op >= opcode::op_t_add_ii && st.op <= opcode::op_t_mod_ff) {
				const uint8_t kind = st.op <= opcode::op_t_shr_ii ? kint : kflt;
				const uint8_t landing = static_cast<uint8_t>(st.a >> k_typed_dst_shift);
				type_info* stamp = landing == k_typed_dst_decl && ip < c.typed_sites.size() ? c.typed_sites[ip].stamp : nullptr;
				use_slot(st.b, kind, false, 0, nullptr, ip); use_slot(st.c, kind, false, 0, nullptr, ip);
				use_slot(st.a & k_typed_slot_mask, kind, true, landing, stamp, ip);
				op.kind = ir::arith; op.fast = st.op; op.a = st.a & k_typed_slot_mask; op.b = st.b; op.c = st.c; op.writes = op.a; slot_read(op, st.b); slot_read(op, st.c);
			} else if (st.op == opcode::op_binary_fused_decl || st.op == opcode::op_binary_fused_temp || st.op == opcode::op_binary_fused_store) {
				// arith the typed lowering left general (an environment value or an unclassed operand): kinds at run time, landing in
				// a stamped scalar decl, a compiler value or a no-result frame-slot store
				uint32_t bidx = st.a, dst = k_invalid_u32; uint8_t landing = k_typed_dst_temp; type_info* stamp = nullptr;
				if (st.op == opcode::op_binary_fused_temp) { dst = st.b; }
				else if (st.a < c.fused_binary_dst_protos.size()) {
					const fused_binary_dst_proto& dp = c.fused_binary_dst_protos[st.a];
					bidx = dp.binary_proto;
					if (st.op == opcode::op_binary_fused_decl) {
						auto* decl = dp.node_index < c.nodes.size() ? static_cast<const variable_decl*>(c.nodes[dp.node_index].get()) : nullptr;
						stamp = decl ? decl->type.get() : nullptr;
						if (!decl || (decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store || decl->ref_escaping || decl->slot_index == SIZE_MAX) { refuse(chunk::cell_refusal_representation, ip); break; }
						if (stamp && stamp_kind(stamp) == 0 && stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }   // a string, bool or class local
						dst = static_cast<uint32_t>(decl->slot_index); landing = k_typed_dst_decl;
					} else {
						if (dp.slot == k_invalid_u32 || (dp.flags & store_flag_global_only)) { refuse(chunk::cell_refusal_effect, ip); break; }   // a global written: a binding write
						if (dp.flags & store_flag_ref_alias) { refuse(chunk::cell_refusal_representation, ip); break; }
						if (!(dp.flags & store_flag_no_result)) { refuse(chunk::cell_refusal_translation, ip); break; }   // an assignment whose value is used
						dst = dp.slot; landing = k_typed_dst_store;
					}
				}
				if (bidx >= c.fused_binary_protos.size() || dst == k_invalid_u32 || dst >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
				const fused_binary_proto& bp = c.fused_binary_protos[bidx];
				if (!arith_tok(bp.op)) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.b) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.c)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(dst, stamp_kind(stamp), true, landing, stamp, ip);
				op.kind = ir::arith; op.tok = bp.op; op.a = dst; op.writes = dst;
			} else if (st.op == opcode::op_t_store_const || st.op == opcode::op_t_store_mov || st.op == opcode::op_t_decl_mov || st.op == opcode::op_t_decl_const) {
				// mov: the source's payload into a stamped scalar decl or a no-result frame-slot store
				const bool decl = st.op == opcode::op_t_decl_mov || st.op == opcode::op_t_decl_const;
				const bool from_const = st.op == opcode::op_t_store_const || st.op == opcode::op_t_decl_const;
				if (!decl && (st.c & (store_flag_ref_alias | store_flag_global_only))) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (!decl && !(st.c & store_flag_no_result)) { refuse(chunk::cell_refusal_translation, ip); break; }
				type_info* stamp = decl && ip < c.typed_sites.size() ? c.typed_sites[ip].stamp : nullptr;
				if (decl && stamp && stamp_kind(stamp) == 0 && stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (from_const) {
					const size_t ci = st.b < c.constants.size() && st.b < k_typed_slot_mask ? c.constants[st.b].raw_storage_index() : SIZE_MAX;
					if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT) { refuse(chunk::cell_refusal_representation, ip); break; }
					op.b = k_typed_const_bit | st.b;
				} else {
					if (st.b >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
					use_slot(st.b, 0, false, 0, nullptr, ip); op.b = st.b; slot_read(op, st.b);
				}
				if (st.a >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(st.a, stamp_kind(stamp), true, decl ? k_typed_dst_decl : k_typed_dst_store, stamp, ip);
				op.kind = ir::mov; op.a = st.a; op.writes = st.a;
			} else if (st.op == opcode::op_t_load && ip < c.typed_sites.size()) {
				// load through a site the typed lowering flattened: the fold operands are int values; a scalar decl needs a stamp
				// (an unstamped decl lands the element's own tag, which a raw cell loses), a compiler value takes the element's kind
				const chunk::typed_site& site = c.typed_sites[ip];
				const bool classed = site.decl_expect == kint || site.decl_expect == kflt;
				if (site.stamp && stamp_kind(site.stamp) == 0 && site.stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }
				for (uint8_t k = 0; k <= site.fold_count && k < 4; ++k) { use_slot(site.operands[k], kint, false, 0, nullptr, ip); slot_read(op, site.operands[k]); }
				use_slot(st.a, classed && stamp_kind(site.stamp) == site.decl_expect ? site.decl_expect : 0, true, site.temp ? k_typed_dst_temp : k_typed_dst_decl, site.temp ? nullptr : site.stamp, ip);
				op.kind = ir::load; op.a = st.a; op.writes = st.a; op.container = site.cont_slot;
			} else if (st.op == opcode::op_index_fused_decl && st.a < c.fused_index_protos.size()) {
				// load the typed lowering left general (an environment value in the index, an unproven container): the same site
				// row, built at emission (environment values take synthetic slots), landing in a stamped scalar decl or a compiler
				// value by the declared kind
				const fused_index_proto& fp = c.fused_index_protos[st.a];
				if (fp.decl_slot == k_invalid_u32 || fp.decl_slot >= k_typed_slot_mask || fp.index_chain_ext.size() > 2 || !container_operand(fp.container)) { refuse(chunk::cell_refusal_representation, ip); break; }
				const uint8_t dk = stamp_kind(fp.decl_type);
				if (!fp.decl_temp && dk == 0 && fp.decl_type && fp.decl_type->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (!index_values(op, fp, ip)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(fp.decl_slot, fp.decl_temp ? 0 : dk, true, fp.decl_temp ? k_typed_dst_temp : k_typed_dst_decl, fp.decl_temp ? nullptr : fp.decl_type, ip);
				op.kind = ir::load; op.a = fp.decl_slot; op.proto = st.a; op.writes = fp.decl_slot;
				op.container = direct_slot(fp.container) || (fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask) ? fp.container.slot : k_invalid_u32;
			} else if (st.op == opcode::op_index_store_fused && st.a < c.fused_index_protos.size()) {
				// store: the container resolved once at entry, the index operands int values, the value a cell of its own (or adopted)
				// kind or a literal; the copy reads environment values through synthetic slots
				const fused_index_proto& fp = c.fused_index_protos[st.a];
				if (!container_operand(fp.container)) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (st.c == 0) { refuse(chunk::cell_refusal_translation, ip); break; }   // an element store whose value is used
				uint32_t sink;
				if (!index_values(op, fp, ip) || (fp.store_value.const_index == k_invalid_u32 && !value(op, fp.store_value, 0, static_cast<uint32_t>(ip * 3 + 2), ip, sink))) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::store; op.proto = st.a;
				op.container = fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask ? fp.container.slot : k_invalid_u32;
			} else if (st.op == opcode::op_math_temp && st.a != k_invalid_u32 && st.a < c.fused_binary_protos.size() && st.b < k_typed_slot_mask) {
				// math: a pure one-argument intrinsic of a value into a compiler value (the kernel's rows run in the block): itrunc
				// and ifloor land an int, abs the argument's kind, the float family a float
				const detail::math_fn fn = static_cast<detail::math_fn>(st.c);
				const fused_operand& src = c.fused_binary_protos[st.a].left;
				uint8_t rk = kflt;
				switch (fn) {
				case detail::math_fn::itrunc: case detail::math_fn::ifloor: rk = kint; break;
				case detail::math_fn::abs_: rk = 0; break;
				case detail::math_fn::floor_: case detail::math_fn::ceil_: case detail::math_fn::sqrt_: case detail::math_fn::sin_: case detail::math_fn::cos_: case detail::math_fn::tan_:
				case detail::math_fn::radians_to_degrees: case detail::math_fn::degrees_to_radians: break;
				default: refuse(chunk::cell_refusal_translation, ip); break;
				}
				if (refusal != chunk::cell_refusal_none) break;
				if (!direct_slot(src)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(src.slot, 0, false, 0, nullptr, ip);
				use_slot(st.b, rk, true, k_typed_dst_temp, nullptr, ip);
				op.kind = ir::math; op.tok = st.c; op.a = st.b; op.b = src.slot; op.writes = st.b; slot_read(op, src.slot);
			} else if (st.op >= opcode::op_t_jlt_ii && st.op <= opcode::op_t_jne_ff) {
				// cmp + test inside the body: the false edge stays inside the loop (or reaches its back edge)
				if (!(st.c > ip && st.c <= back)) { refuse(chunk::cell_refusal_effect, ip); break; }
				const uint8_t kind = st.op <= opcode::op_t_jne_ii ? kint : kflt;
				use_slot(st.a, kind, false, 0, nullptr, ip); use_slot(st.b, kind, false, 0, nullptr, ip);
				op.kind = ir::cmp; op.fast = st.op; op.a = st.a; op.b = st.b; op.target = st.c; slot_read(op, st.a); slot_read(op, st.b);
			} else if (st.op == opcode::op_binary_fused && ip + 1 < back && (code[ip + 1].op == opcode::op_jump_if_true || code[ip + 1].op == opcode::op_jump_if_false) && st.a < c.fused_binary_protos.size()) {
				// a comparison used as a value and the jump that consumes it (the || and && chains, !e): one cmp with the jump's
				// sense; the pair's origin is the value instruction (a miss re-runs it whole and the general jump follows), the
				// jump's copy is a placeholder no branch may target
				const fused_binary_proto& bp = c.fused_binary_protos[st.a];
				const vm_instruction& j = code[ip + 1];
				if (!cmp_tok(bp.op)) { refuse(chunk::cell_refusal_translation, ip); break; }   // a truth test on an arithmetic value: a gap
				if (!(j.a > ip + 1 && j.a <= back)) { refuse(chunk::cell_refusal_effect, ip + 1); break; }
				bool targeted = false;
				for (size_t ip2 = first; ip2 < back && !targeted; ++ip2) {
					const vm_instruction& n = code[ip2];
					const uint32_t t = (n.op == opcode::op_jump || n.op == opcode::op_jump_if_true || n.op == opcode::op_jump_if_false || n.op == opcode::op_fused_cmp_jump) ? n.a
					                 : (n.op >= opcode::op_t_jlt_ii && n.op <= opcode::op_t_jne_ff) ? n.c : k_invalid_u32;
					targeted = t == ip + 1;
				}
				if (targeted) { refuse(chunk::cell_refusal_malformed, ip + 1); break; }
				if (!value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.a) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::cmp; op.tok = bp.op; op.target = j.a; op.sense = j.op == opcode::op_jump_if_true;
				if ((bp.typed_class == 1 || bp.typed_class == 2) && !(op.a & k_ir_env_bit) && !(op.b & k_ir_env_bit)) {
					const opcode t = cmp_op(static_cast<token_type>(bp.op), bp.typed_class);   // the pinned form when the checker classed both operands
					if (t != opcode::op_halt) { op.fast = t; use_slot(op.a, bp.typed_class == 1 ? kint : kflt, false, 0, nullptr, ip); use_slot(op.b, bp.typed_class == 1 ? kint : kflt, false, 0, nullptr, ip); }
				}
				ops.push_back(op);
				op = ir_op{}; op.kind = ir::placeholder;   // the jump's place
				++ip;
			} else if (st.op == opcode::op_fused_cmp_jump && st.b < c.fused_binary_protos.size()) {
				if (!(st.a > ip && st.a <= back)) { refuse(chunk::cell_refusal_effect, ip); break; }
				const fused_binary_proto& bp = c.fused_binary_protos[st.b];
				if (!cmp_tok(bp.op)) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.a) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::cmp; op.tok = bp.op; op.target = st.a;
			} else if (st.op == opcode::op_jump) {
				if (!(st.a > ip && st.a <= back)) { refuse(chunk::cell_refusal_effect, ip); break; }   // a break leaves the loop
				op.kind = ir::jump; op.target = st.a;
			} else if (st.op == opcode::op_operand_temp && st.c == 1) {
				// guard: a snapshot definition; its read is the first load after it whose site names this slot
				op.kind = ir::guard; op.a = st.b;
				for (size_t ip2 = ip + 1; ip2 < back; ++ip2) {
					const vm_instruction& n = code[ip2];
					const uint32_t g = n.op == opcode::op_t_load && ip2 < c.typed_sites.size() ? c.typed_sites[ip2].guard_temp
					                 : n.op == opcode::op_index_fused_decl && n.a < c.fused_index_protos.size() ? c.fused_index_protos[n.a].container_guard : k_invalid_u32;
					if (g == st.b) { op.guarded = static_cast<uint32_t>(ip2 - head); break; }
				}
			} else if (st.op == opcode::op_compound_fused && st.a < c.compound_fused_protos.size()) {
				// x op= y the typed lowering left general (a slot the checker could not pin, a float literal): arith with the
				// compound landing, kinds at run time (a kind change on the slot misses onto the general conversion)
				const compound_fused_proto& cp = c.compound_fused_protos[st.a];
				if (cp.slot == k_invalid_u32 || cp.slot >= k_typed_slot_mask || (cp.kind_flags & compound_flag_global_only)) { refuse(chunk::cell_refusal_effect, ip); break; }
				if (!(cp.kind_flags & compound_flag_no_result) || cp.rhs_proto != k_invalid_u32 || (cp.kind_flags & compound_flag_old_result)) { refuse(chunk::cell_refusal_translation, ip); break; }
				uint8_t tok;
				switch (cp.kind_flags & compound_kind_mask) {
				case compound_plus: tok = static_cast<uint8_t>(token_type::plus); break;
				case compound_minus: tok = static_cast<uint8_t>(token_type::minus); break;
				case compound_star: tok = static_cast<uint8_t>(token_type::star); break;
				case compound_slash: tok = static_cast<uint8_t>(token_type::slash); break;
				case compound_percent: tok = static_cast<uint8_t>(token_type::percent); break;
				default: tok = 0; break;
				}
				if (tok == 0) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!value(op, cp.rhs, 0, static_cast<uint32_t>(ip * 3), ip, op.c)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(cp.slot, 0, false, 0, nullptr, ip); use_slot(cp.slot, 0, true, k_typed_dst_compound, nullptr, ip);
				op.kind = ir::arith; op.tok = tok; op.a = cp.slot; op.b = cp.slot; op.writes = cp.slot; slot_read(op, cp.slot);
			} else if (st.op == opcode::op_incdec && st.b != k_invalid_u32 && st.b < k_typed_slot_mask) {
				// ++x / x-- as a statement on a slot the typed lowering left general: arith by one with the compound landing
				if (st.c & incdec_flag_global_only) { refuse(chunk::cell_refusal_effect, ip); break; }
				if (!(st.c & incdec_flag_no_result)) { refuse(chunk::cell_refusal_translation, ip); break; }   // the value is used
				use_slot(st.b, 0, false, 0, nullptr, ip); use_slot(st.b, 0, true, k_typed_dst_compound, nullptr, ip);
				op.kind = ir::arith; op.tok = static_cast<uint8_t>((st.c & incdec_flag_increment) ? token_type::plus : token_type::minus); op.a = st.b; op.b = st.b; op.c = k_typed_const_bit | int_one(); op.writes = st.b; slot_read(op, st.b);
			} else {
				// the class of the refusal is the first consumer's when the instruction is a pure push (a call's argument, a
				// member's receiver): the push itself says nothing about why the loop stays general
				auto pure_push = [](opcode o) { return o == opcode::op_load || o == opcode::op_const || o == opcode::op_null || o == opcode::op_true || o == opcode::op_false || o == opcode::op_dup; };
				size_t at = ip;
				while (at + 1 < back && pure_push(code[at].op)) { ++at; }
				refuse(chunk::cell_refusal_of(code[at].op), at);
				break;
			}
			ops.push_back(op);
		}
		if (refusal == chunk::cell_refusal_none && cells.empty()) { refuse(chunk::cell_refusal_no_cells, back); }
		if (refusal == chunk::cell_refusal_none && cells.size() > 64) { refuse(chunk::cell_refusal_too_many_cells, back); }
		// legality and lifetimes: a container binding's slot is never written; a compiler value is defined earlier in the body
		// than it is read (a temp is undefined at entry, so a read ahead of every write in the body would only ever miss); a
		// snapshot's slot is written only by its own guard between the guard and its read (temps are reused across statements,
		// so the slot may carry other values outside that span)
		if (refusal == chunk::cell_refusal_none) {
			auto is_temp = [&](uint32_t slot) { const int k = find_cell(slot); return k >= 0 && cells[k].temp; };
			std::vector<uint32_t> defined;
			std::vector<std::pair<uint32_t, uint32_t>> snapshots;   // slot, the read's ip (the guard is open until it)
			for (size_t k = 0; k < ops.size() && refusal == chunk::cell_refusal_none; ++k) {
				const size_t ip = first + k;
				const ir_op& op = ops[k];
				for (uint8_t r = 0; r < op.nreads; ++r) {
					if (is_temp(op.reads[r]) && std::find(defined.begin(), defined.end(), op.reads[r]) == defined.end()) { refuse(chunk::cell_refusal_malformed, ip); }
				}
				if (op.writes != k_invalid_u32) {
					if (is_temp(op.writes)) { defined.push_back(op.writes); }
					for (const auto& s : snapshots) { if (s.first == op.writes && ip < s.second) { refuse(chunk::cell_refusal_malformed, ip); } }
				}
				if (op.kind == ir::guard) {
					for (const auto& s : snapshots) { if (s.first == op.a && ip < s.second) { refuse(chunk::cell_refusal_malformed, ip); } }
					if (op.guarded == 0xFFFFFFu) { refuse(chunk::cell_refusal_malformed, ip); }
					snapshots.emplace_back(op.a, static_cast<uint32_t>(head + op.guarded));
				}
			}
			for (const auto& op : ops) {
				if (op.container == k_invalid_u32) continue;
				for (const auto& w : ops) { if (w.writes == op.container) { refuse(chunk::cell_refusal_effect, head); } }   // a container binding rewritten inside the body
			}
		}
		if (refusal != chunk::cell_refusal_none) {
			c.cell_refusals.push_back(chunk::cell_refusal{ static_cast<uint32_t>(head), static_cast<uint32_t>(back), refusal_ip, refusal, static_cast<uint8_t>(code[refusal_ip].op) });
			continue;
		}
		chunk::cell_loop loop;
		loop.head = static_cast<uint32_t>(head); loop.back = static_cast<uint32_t>(back); loop.counted = counted;
		loop.exit_ip = counted != k_invalid_u32 ? c.counted_for_protos[counted].exit_ip : (hd.op == opcode::op_fused_cmp_jump ? hd.a : hd.c);
		uint32_t max_slot = 0; for (const auto& cl : cells) { if (cl.env_operand == k_invalid_u32 && cl.slot > max_slot) max_slot = cl.slot; }
		for (auto& cl : cells) { if (cl.env_operand != k_invalid_u32) { cl.slot = ++max_slot; } }   // the environment values' synthetic slots
		loop.cells = cells;
		loop.env_operands = env_ops;
		loop.slot_to_cell.assign(max_slot + 1, k_invalid_u32);
		for (uint32_t k = 0; k < cells.size(); ++k) { if (loop.slot_to_cell[cells[k].slot] == k_invalid_u32) { loop.slot_to_cell[cells[k].slot] = k; } }   // the first cell per slot; definitions advance the binding
		for (uint32_t k = 0; k < cells.size(); ++k) { if (cells[k].temp) { loop.temp_mask |= uint64_t(1) << k; } }
		auto env_slot = [&](uint32_t e) -> uint32_t { for (const auto& cl : cells) { if (cl.env_operand == e) return cl.slot; } return k_invalid_u32; };
		// the binding at the instruction being emitted: a slot's current definition (a temp slot advances to its next
		// definition once the emission passes the defining instruction; a local or an environment value has one cell); an
		// instruction's own definition is the destination it writes, its sources are the definitions before it
		std::vector<uint32_t> current = loop.slot_to_cell;
		size_t next_def = 0;
		uint32_t pending_def = k_invalid_u32;
		auto cell_of = [&](uint32_t operand) -> uint32_t {
			if (operand & k_typed_const_bit) return operand;
			if (operand & k_ir_env_bit) return current[env_slot(operand & ~k_ir_env_bit)];
			return current[operand & k_typed_slot_mask];
		};
		auto dst_of = [&](uint32_t operand) -> uint32_t {
			if (pending_def != k_invalid_u32 && cells[pending_def].slot == (operand & k_typed_slot_mask)) return pending_def;
			return cell_of(operand);
		};
		// a proto operand as the copy's site or proto reads it: an environment value becomes its synthetic slot
		auto synthetic = [&](fused_operand& o) {
			if (!env_scalar(o)) return;
			for (size_t e = 0; e < env_ops.size(); ++e) {
				if (env_ops[e].operand.symbol == o.symbol) { fused_operand d; d.slot = env_slot(static_cast<uint32_t>(e)); d.symbol = o.symbol; d.direct = true; o = d; return; }
			}
		};
		auto site_operand = [&](fused_operand o, uint32_t& out) { synthetic(o); out = o.const_index != k_invalid_u32 ? (k_typed_const_bit | o.const_index) : current[o.slot]; };   // the copy's rows name cells
		auto cellify = [&](fused_operand& o) {   // a proto operand for the copy: the cell of the value it reads (an empty chain-step left stays empty)
			if (o.const_index != k_invalid_u32 || (o.slot == k_invalid_u32 && o.symbol == k_invalid_u32)) return;
			synthetic(o);
			fused_operand d; d.slot = o.slot < current.size() ? current[o.slot] : k_invalid_u32; d.symbol = o.symbol; d.direct = true; o = d;
		};
		// the copy: one cell instruction per original instruction; the origin table is the runtime's map back
		loop.copy_head = static_cast<uint32_t>(code.size());
		const uint32_t loop_index = static_cast<uint32_t>(c.cell_loops.size());
		const uint32_t shift = loop.copy_head - loop.head;
		auto append = [&](vm_instruction ins, size_t from_ip) {
			code.push_back(ins);
			c.stmt_nodes.push_back(from_ip < c.stmt_nodes.size() ? c.stmt_nodes[from_ip] : nullptr);
			loop.origin.push_back(static_cast<uint32_t>(from_ip));
		};
		auto emit = [&](const ir_op& op, size_t ip) {
			pending_def = next_def < defs.size() && defs[next_def].first == ip ? defs[next_def].second : k_invalid_u32;
			switch (op.kind) {
			case ir::arith:
				if (op.fast != opcode::op_halt) { append(vm_instruction{opcode::op_c_arith, (static_cast<uint32_t>(op.fast) << k_cell_op_shift) | dst_of(op.a), cell_of(op.b), cell_of(op.c)}, ip); }
				else { append(vm_instruction{opcode::op_c_arith_any, (static_cast<uint32_t>(op.tok) << k_cell_op_shift) | dst_of(op.a), cell_of(op.b), cell_of(op.c)}, ip); }
				break;
			case ir::mov: append(vm_instruction{opcode::op_c_mov, dst_of(op.a), cell_of(op.b), 0}, ip); break;
			case ir::cmp: {
				const uint32_t target = (op.head ? op.target : op.target + shift) | (op.sense ? 0x80000000u : 0u);
				if (op.fast != opcode::op_halt) { append(vm_instruction{op.head ? opcode::op_c_cmp : opcode::op_c_jcmp, (static_cast<uint32_t>(op.fast) << k_cell_op_shift) | cell_of(op.a), cell_of(op.b), target}, ip); }
				else { append(vm_instruction{op.head ? opcode::op_c_cmp_any : opcode::op_c_jcmp_any, (static_cast<uint32_t>(op.tok) << k_cell_op_shift) | cell_of(op.a), cell_of(op.b), target}, ip); }
				break;
			}
			case ir::load: {
				// the copy's own site row at the copy instruction's index: the general row keeps slots, this one names cells
				chunk::typed_site site;
				if (op.proto != k_invalid_u32) {
					// a general read's site row, built for the copy only (the general instruction keeps its proto)
					const fused_index_proto& fp = c.fused_index_protos[op.proto];
					site.cont_slot = fp.container.direct && fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask ? fp.container.slot : k_invalid_u32;
					if (fp.index_binary == k_invalid_u32) { site_operand(fp.index, site.operands[0]); }
					else {
						const fused_binary_proto& bp = c.fused_binary_protos[fp.index_binary];
						site_operand(bp.left, site.operands[0]); site_operand(bp.right, site.operands[1]);
						site.fold_ops[0] = bp.op; site.fold_count = 1;
						for (size_t k = 0; k < fp.index_chain_ext.size(); ++k) {
							const fused_binary_proto& sp = c.fused_binary_protos[fp.index_chain_ext[k]];
							site_operand(sp.right, site.operands[k + 2]); site.fold_ops[k + 1] = sp.op; site.fold_count = static_cast<uint8_t>(k + 2);
						}
					}
					site.orig = code[ip];
					site.index_proto = op.proto;
					site.stamp = fp.decl_temp ? nullptr : fp.decl_type;
					site.decl_expect = fp.decl_temp || stamp_kind(fp.decl_type) == 0 ? 0xFF : stamp_kind(fp.decl_type);   // a var or auto decl lands the element's own kind
					site.temp = fp.decl_temp;
					site.speculative = fp.speculative;
					site.guard_temp = fp.container_guard;
				} else {
					site = c.typed_sites[ip];   // a T_LOAD's row with its slot operands rebound to cells
					for (uint8_t k = 0; k <= site.fold_count && k < 4; ++k) { if (!(site.operands[k] & k_typed_const_bit)) { site.operands[k] = current[site.operands[k] & k_typed_slot_mask]; } }
				}
				chunk_ = &c;
				typed_site_at(code.size()) = site;
				const uint32_t dst = dst_of(op.a);   // an adopted-kind local lands through the declared-type rule; a pinned local or a compiler value lands in place
				append(vm_instruction{loop.cells[dst].kind == 0 && !loop.cells[dst].temp ? opcode::op_c_load_any : opcode::op_c_load, dst, loop_index, static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::store: {
				// the copy reads environment values through synthetic-slot operands: a clone of the proto (and of its index binaries)
				// with each environment value replaced; the original proto stays the general form's
				// the copy's proto: a clone whose index and value operands name cells (the container stays the general operand,
				// resolved from the frame at entry); the original proto stays the general form's
				fused_index_proto cp = c.fused_index_protos[op.proto];
				auto clone_binary = [&](uint32_t& idx) {
					fused_binary_proto bp = c.fused_binary_protos[idx];
					cellify(bp.left); cellify(bp.right);
					c.fused_binary_protos.push_back(bp); idx = static_cast<uint32_t>(c.fused_binary_protos.size() - 1);
				};
				if (cp.index_binary != k_invalid_u32) { clone_binary(cp.index_binary); for (auto& step : cp.index_chain_ext) { clone_binary(step); } }
				else { cellify(cp.index); }
				cellify(cp.store_value);
				c.fused_index_protos.push_back(cp);
				append(vm_instruction{opcode::op_c_index_store, static_cast<uint32_t>(c.fused_index_protos.size() - 1), loop_index, static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::math: append(vm_instruction{opcode::op_c_math, (static_cast<uint32_t>(op.tok) << k_cell_op_shift) | dst_of(op.a), cell_of(op.b), 0}, ip); break;
			case ir::jump: append(vm_instruction{opcode::op_c_jump, op.target + shift, 0, 0}, ip); break;
			case ir::guard: {
				const uint32_t alias = op.a < current.size() ? current[op.a] : k_invalid_u32;
				append(vm_instruction{opcode::op_c_guard, op.a, (alias == k_invalid_u32 ? 0xFFu : alias) | (op.guarded << 8), static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::placeholder: append(vm_instruction{opcode::op_c_guard, 0, k_invalid_u32, 0}, ip); break;   // a counted loop has no head compare
			}
			while (next_def < defs.size() && defs[next_def].first == ip) { current[cells[defs[next_def].second].slot] = defs[next_def].second; ++next_def; }   // the definition takes effect after its instruction
		};
		if (counted != k_invalid_u32) { emit(head_op, head); }
		for (size_t k = 0; k < ops.size(); ++k) { emit(ops[k], first + k); }
		loop.copy_back = static_cast<uint32_t>(code.size());
		if (counted == k_invalid_u32) {
			append(vm_instruction{opcode::op_c_back, loop_index, 0, 0}, back);
			code[back] = vm_instruction{opcode::op_cells_back, loop_index, static_cast<uint32_t>(head), 0};
		} else {
			const counted_for_proto& p = c.counted_for_protos[counted];
			auto int_cop = [&](const fused_operand& o) { return o.const_index != k_invalid_u32 ? (k_typed_const_bit | o.const_index) : current[o.slot]; };
			append(vm_instruction{opcode::op_c_cfor_back, current[p.var.slot], int_cop(p.end), int_cop(p.step)}, back);
			code[back] = vm_instruction{opcode::op_cells_cfor_back, loop_index, static_cast<uint32_t>(head), 0};
		}
		// liveness of compiler values per copy instruction: a definition is live from the instruction after it through its
		// last read (a miss there re-runs the read on the general form, which needs the slot to hold this value); a dead
		// definition is never flushed, so it cannot overwrite the live one of the same slot
		loop.live_temps.assign(loop.origin.size(), 0);
		{
			std::vector<uint32_t> cur = loop.slot_to_cell;
			std::vector<size_t> def_at(cells.size(), 0);
			size_t nd = 0;
			const size_t base = counted != k_invalid_u32 ? 1 : 0;   // the counted head placeholder precedes the ops
			for (size_t k = 0; k < ops.size(); ++k) {
				const ir_op& op = ops[k];
				const size_t here = base + k, ip = first + k;
				for (uint8_t r = 0; r < op.nreads; ++r) {
					const uint32_t cl = op.reads[r] < cur.size() ? cur[op.reads[r]] : k_invalid_u32;
					if (cl == k_invalid_u32 || !cells[cl].temp) continue;
					for (size_t m = def_at[cl] + 1; m <= here && m < loop.live_temps.size(); ++m) { loop.live_temps[m] |= uint64_t(1) << cl; }
				}
				while (nd < defs.size() && defs[nd].first == ip) { cur[cells[defs[nd].second].slot] = defs[nd].second; def_at[defs[nd].second] = here; ++nd; }
			}
		}
		c.cell_loops.push_back(std::move(loop));
	}
	if (!c.typed_sites.empty() && c.typed_sites.size() < code.size()) { c.typed_sites.resize(code.size()); }
}

// An index-chain STEP's proto keeps an empty left (the accumulator stands in): the step is
// int/int when the checker proved the node so and its right addresses directly
static void stamp_chain_step_typed_class(fused_binary_proto& sp, const binary_expr* node) {
	if (!sp.right.direct && sp.right.const_index == k_invalid_u32) return;
	if (node->static_operand_class == operand_class_int_int) { sp.typed_class = 1; }
}

// The typed element-read lane's shape proof (fused_index_proto::typed_lane)
static bool index_proto_typed_lane_shape(const chunk& code, const fused_index_proto& p) {
	if (p.container.const_index != k_invalid_u32 || p.container.member_node != k_invalid_u32 ||
	    p.container.is_subscript() || p.container.load_flags != 0) return false;
	if (p.index_binary == k_invalid_u32) {
		return p.index.direct || p.index.const_index != k_invalid_u32;
	}
	if (code.fused_binary_protos[p.index_binary].typed_class != 1) return false;
	for (uint32_t step : p.index_chain_ext) {
		if (code.fused_binary_protos[step].typed_class != 1) return false;
	}
	return true;
}

// The lane without a proof: a flat index binary with an element-read temp operand and direct or int-literal
// operands (the checker cannot class a read through an unproven container); typed_load_fast's guards decide
static bool index_proto_speculative_shape(const chunk& code, const fused_index_proto& p) {
	if (p.container.const_index != k_invalid_u32 || p.container.member_node != k_invalid_u32 ||
	    p.container.is_subscript() || p.container.load_flags != 0) return false;
	if (p.index_binary == k_invalid_u32 || !p.index_chain_ext.empty()) return false;
	const fused_binary_proto& bp = code.fused_binary_protos[p.index_binary];
	if (!bp.elem_read_operand || bp.typed_class != 0) return false;
	auto int_operand = [&](const fused_operand& o) {
		if (o.const_index != k_invalid_u32) return code.constants[o.const_index].raw_storage_index() == script_value::TYPEID_INT;
		return o.direct && o.slot != k_invalid_u32;
	};
	switch (static_cast<token_type>(bp.op)) {
	case token_type::plus: case token_type::minus: case token_type::star: case token_type::slash: case token_type::percent:
	case token_type::ampersand: case token_type::pipe: case token_type::caret: case token_type::left_shift: case token_type::right_shift:
		return int_operand(bp.left) && int_operand(bp.right);
	default: return false;
	}
}

// A subscript expression the fused index op reads in one instruction: an identifier container with an
// identifier or literal index, a member-base container, or an identifier container with a computed index
// chain whose innermost node has a flat fused shape (the three cases try_emit_fused_index emits)
bool vm_compiler::fusable_index_expr(const expression* e) {
	if (!e || e->get_type() != node_type::binary_expr) return false;
	auto* b = static_cast<binary_expr*>(const_cast<expression*>(e));
	if (b->op.type != token_type::left_bracket) return false;
	const bool operand_index = b->right->get_type() == node_type::identifier_expr || b->right->get_type() == node_type::literal_expr;
	if (b->left->get_type() == node_type::identifier_expr && operand_index) return true;
	if (fusable_member_container(b->left.get()) && operand_index) return true;
	// a[b[i]]: the nested arm lands the inner read in a temp first (a slot container, so nothing raises ahead of it)
	if (b->left->get_type() == node_type::identifier_expr && typed_lowering_ && temps_active_ && chunk_ && chunk_->is_function_body &&
	    quiet_partner(b->left.get()) && fusable_index_expr(b->right.get())) return true;
	if (b->left->get_type() == node_type::identifier_expr) {
		if (binary_expr* rb = index_binary_candidate(b->right.get())) {
			binary_expr* spine[8];
			const int depth = index_chain_spine(rb, spine);
			if (depth > 0 && binary_shape(spine[depth - 1]) != binary_shape_none) return true;
			if (depth == 1 && quiet_partner(b->left.get()) && index_read_partner_shape(rb)) return true;
			return quiet_partner(b->left.get()) && binary_lands_fused(rb);
		}
	}
	return false;
}

// `a[x op b[j]]`: a flat index binary whose one operand is an element read only a temp can carry (its own index
// computed) and whose other operand cannot raise ahead of it (a slot, a literal or another such read, read left
// before right): the read lands in a temp first and the index binary keeps direct operands; the caller holds the
// container to a frame slot (quiet_partner), so nothing raises ahead of the hoisted read there either
bool vm_compiler::index_read_partner_shape(const binary_expr* rb) {
	if (!detail::transient_read_marker::is_value_binary_op(rb->op.type)) return false;
	auto plain = [](const expression* e) { return e->get_type() == node_type::identifier_expr || e->get_type() == node_type::literal_expr; };
	const bool lsub = fusable_index_expr(rb->left.get()) && quiet_partner(rb->right.get());
	const bool rsub = fusable_index_expr(rb->right.get()) && quiet_partner(rb->left.get());
	return (lsub && (rsub || plain(rb->right.get()))) || (rsub && (lsub || plain(rb->left.get())));
}

// An operand that cannot raise before a sibling's element read (the interpreter's left-to-right order
// stays unobservable): a literal, an identifier in a frame slot, or another subscript (read in order)
bool vm_compiler::quiet_partner(const expression* e) {
	if (e->get_type() == node_type::literal_expr) return true;
	if (e->get_type() == node_type::identifier_expr) {
		auto* id = static_cast<identifier_expr*>(const_cast<expression*>(e));
		return !id->global_qualified && identifier_slot_operand(id) != k_invalid_u32;
	}
	return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none;
}

// A call to math::itrunc, ifloor, floor, ceil, sqrt, abs, sin, cos, tan or a degree conversion with exactly one
// argument: the shared kernel evaluates it (detail/math_intrinsics.hpp) and its result lands in a compiler temp,
// where it is a direct operand of the binary around it (the checker classes the integer family int)
detail::math_fn vm_compiler::math_temp_operand(const expression* e) const {
	if (!typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body) return detail::math_fn::none;
	if (!e || e->get_type() != node_type::call_expr) return detail::math_fn::none;
	auto* call = static_cast<const call_expr*>(e);
	if (call->arguments.size() != 1 || !call->callee || call->callee->get_type() != node_type::member_expr) return detail::math_fn::none;
	const detail::math_fn fn = detail::math_intrinsic_for_call(static_cast<const member_expr*>(call->callee.get()));
	switch (fn) {
	case detail::math_fn::itrunc: case detail::math_fn::ifloor: case detail::math_fn::floor_: case detail::math_fn::ceil_:
	case detail::math_fn::sqrt_: case detail::math_fn::abs_: case detail::math_fn::sin_: case detail::math_fn::cos_: case detail::math_fn::tan_:
	case detail::math_fn::radians_to_degrees: case detail::math_fn::degrees_to_radians:
		return fn;
	default: return detail::math_fn::none;
	}
}

// The intrinsic's argument as an operand (a slot, a literal, an element read or another intrinsic into a temp;
// anything else rides the stack), then op_math_temp lands the result in a fresh temp; `out` is that temp
void vm_compiler::compile_math_into_temp(const expression_ptr& e, fused_operand& out) {
	auto* call = static_cast<call_expr*>(e.get());
	const detail::math_fn fn = math_temp_operand(e.get());
	const expression_ptr& arg = call->arguments[0];
	fused_binary_proto p;
	bool operand = true;
	if (arg->get_type() == node_type::identifier_expr || arg->get_type() == node_type::literal_expr) {
		p.left = make_fused_operand(arg);
	} else if (math_temp_operand(arg.get()) != detail::math_fn::none) {
		compile_math_into_temp(arg, p.left);
	} else if (!(fusable_index_expr(arg.get()) && compile_subscript_into_temp(arg, 0xFF, p.left))) {
		// a binary argument that lands as one fused binary computes into a temp instead of the stack
		const bool lands = arg->get_type() == node_type::binary_expr && binary_lands_fused(static_cast<binary_expr*>(arg.get()));
		compile_expression(arg);
		vm_instruction& last = chunk_->code.back();
		if (lands && last.op == opcode::op_binary_fused) {
			const uint32_t t0 = static_cast<uint32_t>(temp_next_);
			++temp_next_;
			if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
			last.op = opcode::op_binary_fused_temp;
			last.b = t0;
			p.left = fused_operand{};
			p.left.slot = t0;
			p.left.symbol = temp_symbol_index();
			p.left.direct = true;
		} else {
			operand = false;
		}
	}
	uint32_t a = k_invalid_u32;
	if (operand) {
		chunk_->fused_binary_protos.push_back(std::move(p));
		a = static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1);
	}
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	emit(opcode::op_math_temp, a, t, static_cast<uint32_t>(fn));
	out = fused_operand{};
	out.slot = t;
	out.symbol = temp_symbol_index();
	out.direct = true;
}

// A frame-slot identifier that names a reference declaration (var&, T&, a by-reference parameter): its
// dereference may run a property getter, so it is not silent when the interpreter evaluates it ahead of a
// hoisted read; a reference the callable recorded as plain (bound to a value the unit declares) is silent
bool vm_compiler::reference_slot_operand(const expression* e) {
	if (e->get_type() != node_type::identifier_expr) return false;
	auto* id = static_cast<identifier_expr*>(const_cast<expression*>(e));
	if (id->global_qualified || id->names_value_decl) return false;
	const uint32_t slot = identifier_slot_operand(id);
	if (slot == k_invalid_u32) return false;
	const auto plain = callable_.plain_refs.find(slot);   // a reference to a value this unit declares: a cell, never a getter
	return plain == callable_.plain_refs.end() || plain->second != id->symbol_id;
}

// The operand's dereferenced value lands in a fresh temp now (op_operand_temp), so an operand the interpreter
// evaluates before a hoisted read keeps its place; `out` becomes the direct temp operand
void vm_compiler::materialize_operand_temp(const expression_ptr& e, fused_operand& out) {
	fused_binary_proto p;
	p.left = make_fused_operand(e);
	chunk_->fused_binary_protos.push_back(std::move(p));
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	emit(opcode::op_operand_temp, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), t);
	out = fused_operand{};
	out.slot = t;
	out.symbol = temp_symbol_index();
	out.direct = true;
}

// A reference-slot container ahead of a hoisted read: the guard (op_operand_temp, c = 1) materializes the
// container into `guard` only when the slot's reference is not a plain cell, so a getter runs once and in its
// place; the read's container operand stays the slot and the readers take the temp when the guard filled it
void vm_compiler::guard_container_temp(const expression_ptr& e, fused_operand& container, uint32_t& guard) {
	fused_binary_proto p;
	p.left = make_fused_operand(e);
	chunk_->fused_binary_protos.push_back(std::move(p));
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	emit(opcode::op_operand_temp, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), t, 1);
	container = make_fused_operand(e);
	guard = t;
}

// The kind a typed-proven binary's element read lands: int or float when the checker proved the pair,
// any scalar (0xFF, the runtime kind) otherwise
uint8_t vm_compiler::operand_expect(const binary_expr* node) {
	if (node->static_operand_class == operand_class_int_int) return static_cast<uint8_t>(script_value::TYPEID_INT);
	if (node->static_operand_class == operand_class_float_float) return static_cast<uint8_t>(script_value::TYPEID_FLOAT);
	return 0xFF;
}

// The fused index read of `e` lands in a fresh compiler temp: op_index_fused_decl with decl_temp (its general
// form is the read plus a slot store, exec_index_fused_decl's temp arm); the typed lowering gives it the
// T_LOAD row when the shape allows. `out` becomes the direct temp operand. Function bodies only.
bool vm_compiler::compile_subscript_into_temp(const expression_ptr& e, uint8_t expect, fused_operand& out) {
	if (!typed_lowering_ || !temps_active_ || !chunk_->is_function_body) return false;
	if (!fusable_index_expr(e.get())) return false;
	auto* b = static_cast<binary_expr*>(e.get());
	uint32_t flags = is_lvalue_shaped(b->left.get()) ? index_flag_lvalue_shape : 0;
	if (b->transient_element_read) { flags |= index_flag_transient_read; }
	if (!try_emit_fused_index(b, flags)) return false;
	auto& code = chunk_->code;
	vm_instruction& ins = code.back();
	fused_index_proto& proto = chunk_->fused_index_protos[ins.a];
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	ins.op = opcode::op_index_fused_decl;
	ins.c = k_invalid_u32;
	proto.decl_slot = t;
	proto.decl_type = nullptr;
	proto.decl_expect = expect;
	proto.decl_temp = true;
	proto.typed_lane = index_proto_typed_lane_shape(*chunk_, proto);
	if (!proto.typed_lane && index_proto_speculative_shape(*chunk_, proto)) { proto.typed_lane = true; proto.speculative = true; }
	out = fused_operand{};
	out.slot = t;
	out.symbol = temp_symbol_index();
	out.direct = true;
	return true;
}

// The fused index shapes (the read-site twin of the store path): true when one instruction was emitted
bool vm_compiler::try_emit_fused_index(binary_expr* expr, uint32_t flags) {
	// Nested index `a[b[i]]` on a slot container: the inner read lands in a temp first (container a is a
	// slot, so nothing that could raise precedes the inner read), then the outer read takes the ident/temp shape
	if (expr->left->get_type() == node_type::identifier_expr && fusable_index_expr(expr->right.get()) &&
	    quiet_partner(expr->left.get()) && typed_lowering_ && temps_active_ && chunk_->is_function_body) {
		fused_operand inner;
		fused_operand container;
		const bool ref_container = reference_slot_operand(expr->left.get());
		uint32_t guard = k_invalid_u32;
		if (ref_container) { guard_container_temp(expr->left, container, guard); }   // its dereference precedes the inner read
		if (compile_subscript_into_temp(expr->right, operand_expect(static_cast<const binary_expr*>(expr->right.get())), inner)) {
			fused_index_proto p;
			p.container = ref_container ? container : make_fused_operand(expr->left);
			p.container_guard = guard;
			p.index = inner;
			chunk_->fused_index_protos.push_back(p);
			emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
			return true;
		}
	}
	// Fused shape: ident container + ident/literal index resolve as operands in-op
	// (kills both LOAD dispatches; subscript-chain containers stay unfused in v1)
	const bool operand_index = expr->right->get_type() == node_type::identifier_expr ||
	                           expr->right->get_type() == node_type::literal_expr;
	if (expr->left->get_type() == node_type::identifier_expr && operand_index) {
		fused_index_proto p;
		p.container = make_fused_operand(expr->left);
		p.index = make_fused_operand(expr->right);
		chunk_->fused_index_protos.push_back(p);
		emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
		return true;
	}
	// Member-base container `base.member[i]`: the GET_MEMBER resolves inside the op
	// through its own member_ic row (ladder replay for every non-slot shape)
	if (member_expr* mc = fusable_member_container(expr->left.get()); mc && operand_index) {
		fused_index_proto p;
		p.container = make_fused_operand(mc->object);
		p.container.member_node = add_node(expr->left);
		p.index = make_fused_operand(expr->right);
		chunk_->fused_index_protos.push_back(p);
		emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
		return true;
	}
	// Computed index on an ident container: a left-spine arithmetic chain runs
	// through the fused op (raw int fold / verbatim boxed replay); the innermost
	// node must be the flat fused-binary shape
	if (expr->left->get_type() == node_type::identifier_expr) {
		if (binary_expr* rb = index_binary_candidate(expr->right.get())) {
			binary_expr* spine[8];
			const int depth = index_chain_spine(rb, spine);
			if (depth > 0 && binary_shape(spine[depth - 1]) != binary_shape_none) {
				fused_index_proto p;
				p.container = make_fused_operand(expr->left);
				binary_expr* inner = spine[depth - 1];
				fused_binary_proto bp;
				bp.op = static_cast<uint8_t>(inner->op.type);
				bp.left = make_fused_operand(inner->left);
				bp.right = make_fused_operand(inner->right);
				stamp_typed_class(bp, inner);
				chunk_->fused_binary_protos.push_back(bp);
				p.index_binary = static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1);
				for (int i = depth - 2; i >= 0; --i) {
					fused_binary_proto sp;   // left stays EMPTY: the accumulator stands in
					sp.op = static_cast<uint8_t>(spine[i]->op.type);
					sp.right = make_fused_operand(spine[i]->right);
					stamp_chain_step_typed_class(sp, spine[i]);
					chunk_->fused_binary_protos.push_back(sp);
					p.index_chain_ext.push_back(static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1));
				}
				chunk_->fused_index_protos.push_back(p);
				emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
				return true;
			}
			if (depth == 1 && typed_lowering_ && temps_active_ && chunk_->is_function_body && quiet_partner(expr->left.get()) && index_read_partner_shape(rb)) {
				// the element-read operand lands in a temp first (left before right); the index binary then reads
				// direct operands and the outer read takes the T_LOAD row when the binary's class is proven
				fused_operand container;
				const bool ref_container = reference_slot_operand(expr->left.get());
				uint32_t guard = k_invalid_u32;
				if (ref_container) { guard_container_temp(expr->left, container, guard); }   // its dereference precedes the reads
				fused_binary_proto bp;
				bp.op = static_cast<uint8_t>(rb->op.type);
				auto side = [&](const expression_ptr& e, fused_operand& out, bool ahead_of_a_read) {
					if (fusable_index_expr(e.get())) {
						if (compile_subscript_into_temp(e, operand_expect(static_cast<const binary_expr*>(e.get())), out)) { bp.elem_read_operand = true; }
					} else if (ahead_of_a_read && reference_slot_operand(e.get())) {
						materialize_operand_temp(e, out);
					} else {
						out = make_fused_operand(e);
					}
				};
				side(rb->left, bp.left, fusable_index_expr(rb->right.get()));
				side(rb->right, bp.right, false);
				stamp_typed_class(bp, rb);
				fused_index_proto p;
				p.container = ref_container ? container : make_fused_operand(expr->left);
				p.container_guard = guard;
				chunk_->fused_binary_protos.push_back(bp);
				p.index_binary = static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1);
				chunk_->fused_index_protos.push_back(p);
				emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
				return true;
			}
			if (quiet_partner(expr->left.get()) && binary_lands_fused(rb)) {
				// an arithmetic tree as the index: compile_binary emits one fused binary last, rewritten to land in a
				// fresh temp, and the read is the ident-plus-temp shape (the T_LOAD row); a reference-slot container
				// is dereferenced ahead of the index (the interpreter's container-before-index order)
				const size_t temp_mark = temp_next_;
				fused_operand container;
				const bool ref_container = reference_slot_operand(expr->left.get());
				uint32_t guard = k_invalid_u32;
				if (ref_container) { guard_container_temp(expr->left, container, guard); }
				compile_expression(expr->right);
				vm_instruction& last = chunk_->code.back();
				if (last.op != opcode::op_binary_fused) {
					// unreachable by construction (binary_lands_fused mirrors compile_binary's decisions); the index is on the stack
					emit_unsupported("an index tree that did not land fused", "row 110");
					temp_next_ = temp_mark;
					return true;
				}
				const uint32_t t = static_cast<uint32_t>(temp_next_);
				++temp_next_;
				if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
				last.op = opcode::op_binary_fused_temp;
				last.b = t;
				fused_index_proto p;
				p.container = ref_container ? container : make_fused_operand(expr->left);
				p.container_guard = guard;
				p.index = fused_operand{};
				p.index.slot = t;
				p.index.symbol = temp_symbol_index();
				p.index.direct = true;
				chunk_->fused_index_protos.push_back(p);
				emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
				temp_next_ = temp_mark;   // the index temp and a container snapshot die into the read
				return true;
			}
		}
	}
	return false;
}

bool vm_compiler::lift_operand_shape(const expression* e) {
	return e->get_type() == node_type::identifier_expr || e->get_type() == node_type::literal_expr ||
	       fusable_subscript_operand(e) || fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none;
}

// a subscript only a temp can carry (not a chain operand) or an intrinsic needs a partner that cannot raise first;
// the rule holds at every node of the lifted tree, the outer one included
bool vm_compiler::lift_temp_only(const expression* c) {
	return (fusable_index_expr(c) && !fusable_subscript_operand(c)) || math_temp_operand(c) != detail::math_fn::none;
}

// a partner that is itself a value binary is quiet here: the lift computes it into its temp in operand order,
// so nothing of it runs ahead of the hoisted read (the flat pre-step keeps the strict rule: it resolves in-op)
static bool lifted_partner(const expression* e) {
	if (e->get_type() != node_type::binary_expr) return false;
	const auto* b = static_cast<const binary_expr*>(e);
	return b->op.type != token_type::left_bracket && detail::transient_read_marker::is_value_binary_op(b->op.type);
}

bool vm_compiler::lift_partners_ok(const binary_expr* b) {
	return !((lift_temp_only(b->left.get()) && !quiet_partner(b->right.get()) && !lifted_partner(b->right.get())) ||
	         (lift_temp_only(b->right.get()) && !quiet_partner(b->left.get()) && !lifted_partner(b->left.get())));
}

// Whole-tree classification: operands and pure value-op binaries all the way down. Returns the binary-node
// count (the conservative temp bound), -1 = not fusible. literal-op-literal nodes stay UNFUSED, mirroring
// binary_shape's refusal: the interpreter's fast paths never fire for them, and the general paths' error
// spellings differ from the fused kernel's ("Division by zero" vs "... in float operation"; fuzz seed 115)
int vm_compiler::lift_fusible_nodes(const expression* e) {
	if (lift_operand_shape(e)) { return 0; }
	if (e->get_type() != node_type::binary_expr) { return -1; }
	const auto* b = static_cast<const binary_expr*>(e);
	if (b->op.type == token_type::left_bracket) { return -1; }
	if (!detail::transient_read_marker::is_value_binary_op(b->op.type)) { return -1; }
	if (b->left->get_type() == node_type::literal_expr && b->right->get_type() == node_type::literal_expr) { return -1; }
	if (!lift_partners_ok(b)) { return -1; }
	const int l = lift_fusible_nodes(b->left.get());
	if (l < 0) { return -1; }
	const int r = lift_fusible_nodes(b->right.get());
	if (r < 0) { return -1; }
	return l + r + 1;
}

// A value binary compile_binary emits as one fused binary last (the flat pre-step, the flat fuse or the lift), so
// a fresh temp can take its result: the same decisions, made without emitting
bool vm_compiler::binary_lands_fused(binary_expr* b) {
	if (!typed_lowering_ || !temps_active_ || !chunk_->is_function_body) return false;
	if (b->op.type == token_type::left_bracket || b->op.type == token_type::ampersand_ampersand || b->op.type == token_type::pipe_pipe) return false;
	if (!detail::transient_read_marker::is_value_binary_op(b->op.type)) return false;
	auto plain = [](const expression* e) { return e->get_type() == node_type::identifier_expr || e->get_type() == node_type::literal_expr; };
	auto hoists = [this](const expression* e) { return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none; };
	const bool lsub = hoists(b->left.get()) && quiet_partner(b->right.get());
	const bool rsub = hoists(b->right.get()) && quiet_partner(b->left.get());
	if ((lsub && (rsub || plain(b->right.get()))) || (rsub && (lsub || plain(b->left.get())))) return true;
	if (binary_shape(b) != binary_shape_none) return true;
	const int ln = lift_fusible_nodes(b->left.get());
	const int rn = ln >= 0 ? lift_fusible_nodes(b->right.get()) : -1;
	return lift_partners_ok(b) && ln >= 0 && rn >= 0 && ln + rn >= 1 && ln + rn <= 16;
}

void vm_compiler::compile_binary(const std::shared_ptr<binary_expr>& expr) {
	if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
		compile_expression(expr->left);
		if (expr->op.type == token_type::ampersand_ampersand) {
			size_t jump_false = emit(opcode::op_jump_if_false, k_invalid_u32, 0);
			compile_expression(expr->right);
			emit(opcode::op_to_bool);
			size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
			patch_jump(jump_false, chunk_->code.size());
			emit(opcode::op_false);
			patch_jump(jump_end, chunk_->code.size());
		} else {
			size_t jump_true = emit(opcode::op_jump_if_true, k_invalid_u32, 0);
			compile_expression(expr->right);
			emit(opcode::op_to_bool);
			size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
			patch_jump(jump_true, chunk_->code.size());
			emit(opcode::op_true);
			patch_jump(jump_end, chunk_->code.size());
		}
		return;
	}

	if (expr->op.type == token_type::left_bracket) {
		uint32_t flags = is_lvalue_shaped(expr->left.get()) ? index_flag_lvalue_shape : 0;
		if (expr->transient_element_read) {
			flags |= index_flag_transient_read;
		}
		if (try_emit_fused_index(expr.get(), flags)) return;
		compile_expression(expr->left);
		compile_expression(expr->right);
		emit(opcode::op_index, flags);
		return;
	}

	// A subscript operand the fused index op can read lands in a temp first (left before right) when its
	// partner is a slot, a literal or another such read (nothing can raise ahead of the read); the binary
	// then has direct operands, the typed lowering gives the read the T_LOAD row and the binary its typed row
	if (typed_lowering_ && temps_active_ && chunk_->is_function_body &&
	    detail::transient_read_marker::is_value_binary_op(expr->op.type)) {
		auto plain = [](const expression* e) { return e->get_type() == node_type::identifier_expr || e->get_type() == node_type::literal_expr; };
		auto hoists = [this](const expression* e) { return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none; };
		const bool lsub = hoists(expr->left.get()) && quiet_partner(expr->right.get());
		const bool rsub = hoists(expr->right.get()) && quiet_partner(expr->left.get());
		if ((lsub && (rsub || plain(expr->right.get()))) || (rsub && (lsub || plain(expr->left.get())))) {
			const size_t temp_mark = temp_next_;
			// the binary's class when the checker proved the pair, else the read's own proven element kind
			auto expect_of = [&](const expression_ptr& sub) {
				const uint8_t e = operand_expect(expr.get());
				return e != 0xFF ? e : operand_expect(static_cast<const binary_expr*>(sub.get()));
			};
			auto hoist_into_temp = [&](const expression_ptr& side, fused_operand& out) {
				if (math_temp_operand(side.get()) != detail::math_fn::none) { compile_math_into_temp(side, out); }
				else { compile_subscript_into_temp(side, expect_of(side), out); }
			};
			fused_binary_proto proto;
			proto.op = static_cast<uint8_t>(expr->op.type);
			if (lsub) { hoist_into_temp(expr->left, proto.left); }
			else if (reference_slot_operand(expr->left.get())) { materialize_operand_temp(expr->left, proto.left); }   // its dereference precedes the read
			else { proto.left = make_fused_operand(expr->left); }
			if (rsub) { hoist_into_temp(expr->right, proto.right); } else { proto.right = make_fused_operand(expr->right); }
			stamp_typed_class(proto, expr.get());
			chunk_->fused_binary_protos.push_back(proto);
			emit(opcode::op_binary_fused, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), binary_shape_ident_ident);
			temp_next_ = temp_mark;   // the read temps die into the binary
			return;
		}
	}

	const uint32_t shape = binary_shape(expr.get());
	if (shape != binary_shape_none) {
		fused_binary_proto proto;
		proto.op = static_cast<uint8_t>(expr->op.type);
		proto.left = make_fused_operand(expr->left);
		proto.right = make_fused_operand(expr->right);
		stamp_typed_class(proto, expr.get());
		chunk_->fused_binary_protos.push_back(proto);
		emit(opcode::op_binary_fused, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), shape);
		return;
	}

	// Register-file lift (wave W1+W2): pure fusible sub-binaries on EITHER side compute
	// into compiler temps (frame slots above local_count) and the outer binary rides
	// the fused kernel with temps as ordinary slot operands — no pushes, no unfused
	// op_binary. Sub-lifts emit left-then-right (subtree evaluation order preserved);
	// a subscript operand LEFT of a lifting right subtree materializes into a temp first
	// (op_operand_temp) so its read - and any index error - keeps the interpreter's
	// left-to-right place (fuzz seed 100991); an identifier or literal there still
	// resolves at op time (only an undefined name or a getter-backed field could observe
	// that, and the shape is the method-body hot path). Gated to real function-body
	// chunks (top level and default-arg chunks have no window of their own).
	if (temps_active_ && chunk_->is_function_body) {
		auto operand_shape = [this](const expression* e) -> bool { return lift_operand_shape(e); };
		// a subscript only a temp can carry (not a chain operand) needs a partner that cannot raise first;
		// the rule holds at every node of the lifted tree, the outer one included
		auto temp_only = [this](const expression* c) { return lift_temp_only(c); };
		auto partners_ok = [this](const binary_expr* b) { return lift_partners_ok(b); };
		// Whole-tree classification: operands and pure value-op binaries all the way
		// down. Returns the binary-node count (the conservative temp bound), -1 = not
		// fusible. Capped so a pathological expression can't bloat the window every call.
		auto fusible_nodes = [this](const expression* e) -> int { return lift_fusible_nodes(e); };
		const int ln = fusible_nodes(expr->left.get());
		const int rn = ln >= 0 ? fusible_nodes(expr->right.get()) : -1;
		// At least one side must actually LIFT (ln+rn >= 1): a zero-lift arrival means
		// the flat fuse above DECLINED this shape (e.g. literal-op-literal), and
		// re-emitting it fused would re-pair the error spellings the decline preserves.
		// Cap total temps at 16 so pathological expressions can't bloat the window.
		if (partners_ok(expr.get()) && ln >= 0 && rn >= 0 && ln + rn >= 1 && ln + rn <= 16) {
			// Depth-first emission: children compute into temps before their parent,
			// left subtree before right (subtree evaluation order preserved; the temp
			// dst may alias a released child slot — the kernel resolves both operands
			// before the sink writes).
			auto sequence_left = [&](fused_operand& lo, const expression* l, const expression* r) {
				const bool ref_left = reference_slot_operand(l) && (temp_only(r) || !operand_shape(r));
				if (!ref_left && (!lo.is_subscript() || operand_shape(r))) { return; }
				fused_binary_proto p;
				p.left = std::move(lo);
				chunk_->fused_binary_protos.push_back(std::move(p));
				const uint32_t t = static_cast<uint32_t>(temp_next_);
				++temp_next_;
				if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
				emit(opcode::op_operand_temp, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), t);
				lo = fused_operand{};
				lo.slot = t;
				lo.symbol = temp_symbol_index();
				lo.direct = true;
			};
			// a subscript operand of node `b` reads into a temp when its partner cannot raise ahead of it; the read's
			// landing kind is the binary's class when the checker proved the pair, else the read's own proven element
			// kind, read from the subscript node only (an identifier or literal operand has no class byte to read)
			auto operand_of = [&](const binary_expr* b, const expression_ptr& e, const expression* partner) -> fused_operand {
				fused_operand o;
				if (math_temp_operand(e.get()) != detail::math_fn::none) { compile_math_into_temp(e, o); return o; }   // partners_ok admitted it
				if (fusable_index_expr(e.get()) && (quiet_partner(partner) || lifted_partner(partner))) {
					const uint8_t expect = operand_expect(b) != 0xFF ? operand_expect(b) : operand_expect(static_cast<const binary_expr*>(e.get()));
					if (compile_subscript_into_temp(e, expect, o)) { return o; }
				}
				return make_fused_operand(e);
			};
			std::function<fused_operand(const expression_ptr&)> side = [&](const expression_ptr& e) -> fused_operand {
				if (operand_shape(e.get())) { return make_fused_operand(e); }
				const auto* b = static_cast<const binary_expr*>(e.get());
				fused_binary_proto p;
				p.op = static_cast<uint8_t>(b->op.type);
				const size_t mark = temp_next_;
				p.left = operand_shape(b->left.get()) ? operand_of(b, b->left, b->right.get()) : side(b->left);
				sequence_left(p.left, b->left.get(), b->right.get());
				p.right = operand_shape(b->right.get()) ? operand_of(b, b->right, b->left.get()) : side(b->right);
				stamp_typed_class(p, b);
				chunk_->fused_binary_protos.push_back(p);
				const uint32_t idx = static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1);
				temp_next_ = mark;   // children die into this op
				const uint32_t t = static_cast<uint32_t>(temp_next_);
				++temp_next_;
				if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
				emit(opcode::op_binary_fused_temp, idx, t);
				fused_operand out;
				out.slot = t;
				out.symbol = temp_symbol_index();
				out.direct = true;
				return out;
			};
			const size_t temp_mark = temp_next_;
			fused_binary_proto outer;
			outer.op = static_cast<uint8_t>(expr->op.type);
			outer.left = operand_shape(expr->left.get()) ? operand_of(expr.get(), expr->left, expr->right.get()) : side(expr->left);
			sequence_left(outer.left, expr->left.get(), expr->right.get());
			outer.right = operand_shape(expr->right.get()) ? operand_of(expr.get(), expr->right, expr->left.get()) : side(expr->right);
			stamp_typed_class(outer, expr.get());
			chunk_->fused_binary_protos.push_back(outer);
			emit(opcode::op_binary_fused, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), binary_shape_ident_ident);
			temp_next_ = temp_mark;   // LIFO release: temps die once the outer op consumed them
			return;
		}
	}

	compile_expression(expr->left);
	compile_expression(expr->right);
	emit(opcode::op_binary, static_cast<uint32_t>(expr->op.type), binary_shape_none);
}

uint32_t vm_compiler::temp_symbol_index() {
	// One shared symbols entry per chunk: fused_ident_value reads symbols[operand.symbol]
	// unconditionally at entry, so temps need a REAL index. "\x01tmp" cannot lex as a
	// script identifier (no collision with catch vars or user names), and the frame-slot
	// hit returns before the symbol is ever consulted for resolution.
	if (temp_symbol_index_ == k_invalid_u32) {
		temp_symbol_index_ = add_symbol(symbolizer_->intern("\x01tmp"));
	}
	return temp_symbol_index_;
}

fused_operand vm_compiler::make_fused_operand(const expression* e) {
	fused_operand out;
	if (e->get_type() == node_type::identifier_expr) {
		auto* ident = const_cast<identifier_expr*>(static_cast<const identifier_expr*>(e));
		out.slot = identifier_slot_operand(ident);
		out.symbol = add_symbol(ident->symbol_id);
		if (ident->global_qualified) {
			out.load_flags |= load_flag_global_only;
		}
		if (!ident->name.empty() && (ident->name.front() == 'w' || ident->name.front() == 's') &&
		    (ident->name.find("weak_ptr<") == 0 || ident->name.find("shared_ptr<") == 0)) {
			out.load_flags |= load_flag_type_ctor;
		}
		out.direct = out.slot != k_invalid_u32 && out.load_flags == 0;
	} else if (e->get_type() == node_type::binary_expr) {
		// fusable_subscript_operand-vetted chain: walk to the base identifier collecting
		// indexes innermost-first (application order), base rides the ident fields
		const expression* cur = e;
		while (cur->get_type() == node_type::binary_expr) {
			const auto* sub = static_cast<const binary_expr*>(cur);
			fused_subscript_index level;
			if (sub->right->get_type() == node_type::identifier_expr) {
				auto* idx = const_cast<identifier_expr*>(static_cast<const identifier_expr*>(sub->right.get()));
				level.kind = fused_subscript_index::index_ident;
				level.slot = identifier_slot_operand(idx);
				level.symbol = add_symbol(idx->symbol_id);
			} else {
				level.kind = fused_subscript_index::index_literal;
				level.literal = static_cast<const literal_expr*>(sub->right.get())->value.unchecked_as_int();
			}
			out.subscript_chain.insert(out.subscript_chain.begin(), level);
			cur = sub->left.get();
		}
		auto* base = const_cast<identifier_expr*>(static_cast<const identifier_expr*>(cur));
		out.slot = identifier_slot_operand(base);
		out.symbol = add_symbol(base->symbol_id);
		if (base->global_qualified) {
			out.load_flags |= load_flag_global_only;
		}
	} else {
		out.const_index = add_constant(static_cast<const literal_expr*>(e)->value);
	}
	return out;
}

void vm_compiler::compile_unary(const std::shared_ptr<unary_expr>& expr) {
	if (expr->op.type == token_type::plus_plus || expr->op.type == token_type::minus_minus) {
		if (expr->operand->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(expr->operand.get());
			uint32_t flags = 0;
			if (expr->is_postfix) flags |= incdec_flag_postfix;
			if (expr->op.type == token_type::plus_plus) flags |= incdec_flag_increment;
			if (ident->global_qualified) flags |= incdec_flag_global_only;
			emit(opcode::op_incdec, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
		} else {
			compile_expression(expr->operand);
			emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::invalid_assignment_target), k_invalid_u32, k_invalid_u32);
		}
		return;
	}
	compile_expression(expr->operand);
	emit(opcode::op_unary, static_cast<uint32_t>(expr->op.type));
}

bool vm_compiler::try_emit_fused_index_store(binary_expr* sub, const expression_ptr& value, bool no_result) {
	const uint32_t shape = is_lvalue_shaped(sub->left.get()) ? index_flag_lvalue_shape : 0;
	const bool operand_index = sub->right->get_type() == node_type::identifier_expr ||
	                           sub->right->get_type() == node_type::literal_expr;
	const bool ident_container = sub->left->get_type() == node_type::identifier_expr;
	member_expr* mc = fusable_member_container(sub->left.get());

	fused_index_proto p;
	bool fused = false;
	if (ident_container && operand_index) {
		p.container = make_fused_operand(sub->left);
		p.index = make_fused_operand(sub->right);
		fused = true;
	} else if (mc && operand_index) {
		// Member-base container `base.member[i] = v` (see the read-site twin)
		p.container = make_fused_operand(mc->object);
		p.container.member_node = add_node(sub->left);
		p.index = make_fused_operand(sub->right);
		fused = true;
	} else if (binary_expr* rb = ident_container ? index_binary_candidate(sub->right.get()) : nullptr) {
		// Computed-index chain on an ident container (see the read-site twin)
		binary_expr* spine[8];
		const int depth = index_chain_spine(rb, spine);
		if (depth > 0 && binary_shape(spine[depth - 1]) != binary_shape_none) {
			p.container = make_fused_operand(sub->left);
			binary_expr* inner = spine[depth - 1];
			fused_binary_proto bp;
			bp.op = static_cast<uint8_t>(inner->op.type);
			bp.left = make_fused_operand(inner->left);
			bp.right = make_fused_operand(inner->right);
			stamp_typed_class(bp, inner);
			chunk_->fused_binary_protos.push_back(bp);
			p.index_binary = static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1);
			for (int i = depth - 2; i >= 0; --i) {
				fused_binary_proto sp;   // left stays EMPTY: the accumulator stands in
				sp.op = static_cast<uint8_t>(spine[i]->op.type);
				sp.right = make_fused_operand(spine[i]->right);
				stamp_chain_step_typed_class(sp, spine[i]);
				chunk_->fused_binary_protos.push_back(sp);
				p.index_chain_ext.push_back(static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1));
			}
			fused = true;
		}
	}
	if (!fused) return false;

	// Statement position rides an ident/literal RHS as an operand (no LOAD dispatch,
	// no pushes); every other shape keeps the value on the stack
	bool value_operand = false;
	if (no_result && (value->get_type() == node_type::identifier_expr ||
	                  value->get_type() == node_type::literal_expr)) {
		p.store_value = make_fused_operand(value);
		value_operand = true;
	} else if (no_result && fusable_index_expr(value.get()) &&
	           compile_subscript_into_temp(value, operand_expect(static_cast<const binary_expr*>(value.get())), p.store_value)) {
		value_operand = true;   // `a[i] = b[j]`: the element read lands in a temp, the store takes it as an operand
	}
	if (!value_operand) {
		compile_expression(value);
	}
	// the typed store lane's shape proof (typed_index_store): the value rides the stack or
	// resolves as an operand; container and index as the read lane requires
	p.typed_lane = index_proto_typed_lane_shape(*chunk_, p);
	chunk_->fused_index_protos.push_back(p);
	emit(opcode::op_index_store_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), shape,
	     no_result ? 1u : 0u);
	return true;
}

void vm_compiler::compile_assignment(const std::shared_ptr<assignment_expr>& expr, bool as_statement) {
	const bool compound = expr->op.type != token_type::equal;
	if (expr->target->get_type() == node_type::identifier_expr) {
		auto* ident = static_cast<identifier_expr*>(expr->target.get());
		compile_expression(expr->value);
		if (!compound) {
			uint32_t flags = is_lvalue_shaped(expr->value.get()) ? store_flag_rhs_lvalue : 0;
			if (!ident->names_value_decl) flags |= store_flag_ref_alias;
			else if (expr->typed_store_provable) flags |= store_flag_type_provable;
			if (ident->global_qualified) flags |= store_flag_global_only;
			if (expr->static_store_proof == proof_same_type) flags |= store_flag_same_type;
			// Dest-addressed peephole (flatstack stage 6): a fused-binary rhs runs
			// op_store's exact post-pop tail with no push/pop round-trip. In-place
			// rewrite like emit_compound_store, so jump targets never shift.
			auto& code = chunk_->code;
			if (!code.empty() && code.back().op == opcode::op_binary_fused &&
			    expr->value->get_type() == node_type::binary_expr) {
				fused_binary_dst_proto dp;
				dp.binary_proto = code.back().a;
				dp.symbol = add_symbol(ident->symbol_id);
				dp.slot = identifier_slot_operand(ident);
				dp.flags = flags;
				chunk_->fused_binary_dst_protos.push_back(dp);
				vm_instruction& ins = code.back();
				ins.op = opcode::op_binary_fused_store;
				ins.a = static_cast<uint32_t>(chunk_->fused_binary_dst_protos.size() - 1);
				ins.b = 0;
				ins.c = 0;
				note_ident_node(code.size() - 1, expr->target);
				return;
			}
			if (try_lower_store_move(ident, expr->value, flags)) return;
			const size_t store_ip = emit(opcode::op_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
			note_ident_node(store_ip, expr->target);
		} else {
			uint32_t kind;
			switch (expr->op.type) {
			case token_type::plus_equal: kind = compound_plus; break;
			case token_type::minus_equal: kind = compound_minus; break;
			case token_type::star_equal: kind = compound_star; break;
			case token_type::slash_equal: kind = compound_slash; break;
			case token_type::percent_equal: kind = compound_percent; break;
			default: kind = compound_plus; break;
			}
			if (!as_statement) kind |= compound_flag_result_needed;
			if (ident->global_qualified) kind |= compound_flag_global_only;
			emit_compound_store(add_symbol(ident->symbol_id), identifier_slot_operand(ident), kind, expr->value.get());
		}
		return;
	}

	if (expr->target->get_type() == node_type::binary_expr &&
	    static_cast<binary_expr*>(expr->target.get())->op.type == token_type::left_bracket) {
		auto* sub = static_cast<binary_expr*>(expr->target.get());
		uint32_t shape = is_lvalue_shaped(sub->left.get()) ? index_flag_lvalue_shape : 0;
		if (!compound) {
			// Fused store: the array fast path writes in place (no reference mint);
			// other shapes replay the INDEX(lvalue_write)+INDEX_ASSIGN semantics inside
			if (!try_emit_fused_index_store(sub, expr->value, false)) {
				compile_expression(expr->value);
				compile_expression(sub->left);
				compile_expression(sub->right);
				emit(opcode::op_index_store, shape);
			}
		} else {
			compile_expression(sub->left);
			compile_expression(sub->right);
			compile_expression(expr->value);
			uint32_t kind;
			switch (expr->op.type) {
			case token_type::plus_equal: kind = compound_plus; break;
			case token_type::minus_equal: kind = compound_minus; break;
			case token_type::star_equal: kind = compound_star; break;
			case token_type::slash_equal: kind = compound_slash; break;
			case token_type::percent_equal: kind = compound_percent; break;
			default: kind = compound_plus; break;
			}
			if (expr->postfix_old_result) kind |= compound_flag_old_result;
			if (expr->incdec_desugar) kind |= compound_flag_incdec;
			emit(opcode::op_index_compound_fused, kind, shape);
		}
		return;
	}

	if (expr->target->get_type() == node_type::member_expr) {
		auto member = std::static_pointer_cast<member_expr>(expr->target);
		if (!compound) {
			compile_expression(expr->value);
			if (member->is_static) {
				emit(opcode::op_set_static, add_node(member));
			} else {
				compile_expression(member->object);
				emit(opcode::op_set_member, add_node(member), expr->static_store_proof);
			}
		} else {
			if (member->is_static) {
				emit(opcode::op_get_static, add_node(member));
			} else {
				// The receiver is evaluated once: op_dup keeps it under the read for the
				// write-back (a :: target resolves its class inside op_member_compound)
				compile_expression(member->object);
				emit(opcode::op_dup);
				emit(opcode::op_get_member, add_node(member));
			}
			compile_expression(expr->value);
			uint32_t kind;
			switch (expr->op.type) {
			case token_type::plus_equal: kind = compound_plus; break;
			case token_type::minus_equal: kind = compound_minus; break;
			case token_type::star_equal: kind = compound_star; break;
			case token_type::slash_equal: kind = compound_slash; break;
			case token_type::percent_equal: kind = compound_percent; break;
			default: kind = compound_plus; break;
			}
			if (expr->postfix_old_result) kind |= compound_flag_old_result;
			if (expr->incdec_desugar) kind |= compound_flag_incdec;
			emit(opcode::op_member_compound, add_node(member), expr->static_store_proof, kind);
		}
		return;
	}

	emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::unsupported_operation),
	     add_message("Complex assignment targets not yet implemented"), k_invalid_u32);
	emit(opcode::op_null);
}

// `recv.push(v);` as a statement (GLOOM profile: 95% of method calls, ~440 cycles as
// LOAD, LOAD, CALL_METHOD, POP): receiver an identifier, value a slotted identifier or a
// literal, so no probe is owed (call_args_need_probe's exact admission) and the receiver
// resolves before the value exactly as the unfused pair reads them. The call site is built
// as compile_call builds it: the exec replays through it on any guard miss.
bool vm_compiler::try_emit_fused_push(const std::shared_ptr<call_expr>& expr) {
	if (expr->callee->get_type() != node_type::member_expr || expr->arguments.size() != 1) return false;
	auto member = std::static_pointer_cast<member_expr>(expr->callee);
	if (member->is_static || member->null_safe || member->member != "push") return false;
	if (member->object->get_type() != node_type::identifier_expr) return false;
	const auto& arg = expr->arguments[0];
	bool value_is_subscript = false;
	if (arg->get_type() == node_type::identifier_expr) {
		auto* arg_ident = static_cast<identifier_expr*>(arg.get());
		if (arg_ident->global_qualified || identifier_slot_operand(arg_ident) == k_invalid_u32) return false;
	} else if (arg->get_type() == node_type::binary_expr) {
		// `recv.push(table[i])`: one level, identifier base, ident-or-literal index. Not a
		// transient read (an argument may bind a T& parameter), so the replay reads it as
		// the lvalue-shaped op_index the general path emits; the builtin lane reads the
		// element by value, which is what push consumes either way
		const auto* sub = static_cast<const binary_expr*>(arg.get());
		if (sub->op.type != token_type::left_bracket) return false;
		if (sub->left->get_type() != node_type::identifier_expr ||
		    static_cast<const identifier_expr*>(sub->left.get())->global_qualified) return false;
		if (!fusable_subscript_index(sub->right.get())) return false;
		value_is_subscript = true;
	} else if (arg->get_type() != node_type::literal_expr) {
		return false;
	}
	call_site site;
	site.member_node = add_node(member);
	site.call_node = add_node(expr);
	auto* recv_ident = static_cast<identifier_expr*>(member->object.get());
	site.receiver_symbol = recv_ident->symbol_id;
	site.receiver_slot = identifier_slot_operand(recv_ident);
	if (builtins_) {
		auto builtin_index_for = [&](const std::unordered_map<uint64_t, uint16_t>& index) -> uint16_t {
			auto it = index.find(member->member_id);
			return it != index.end() ? it->second : builtin_method_registries::k_no_builtin;
		};
		site.bi_array = builtin_index_for(builtins_->array_index);
		site.bi_map = builtin_index_for(builtins_->map_index);
		site.bi_string = builtin_index_for(builtins_->string_index);
		chunk_->builtin_indexed = true;
	}
	if (arg->get_type() == node_type::identifier_expr) {
		auto* arg_ident = static_cast<identifier_expr*>(arg.get());
		site.arg_symbols.push_back(arg_ident->symbol_id);
		site.arg_slots.push_back(identifier_slot_operand(arg_ident));
	} else {
		site.arg_symbols.push_back(UINT64_MAX);
		site.arg_slots.push_back(k_invalid_u32);
	}
	push_proto proto;
	proto.receiver = make_fused_operand(member->object);
	proto.value = make_fused_operand(arg);
	proto.value_is_subscript = value_is_subscript;
	if (value_is_subscript) {
		proto.value_index_flags = is_lvalue_shaped(static_cast<const binary_expr*>(arg.get())->left.get()) ? index_flag_lvalue_shape : 0;
	}
	chunk_->call_sites.push_back(std::move(site));
	proto.site = static_cast<uint32_t>(chunk_->call_sites.size() - 1);
	chunk_->push_protos.push_back(std::move(proto));
	emit(opcode::op_push_fused, static_cast<uint32_t>(chunk_->push_protos.size() - 1));
	return true;
}

void vm_compiler::compile_call(const std::shared_ptr<call_expr>& expr) {
	// Consumed HERE so nested arg calls never inherit the return-position hint
	const bool tail_site = pending_tail_call_;
	pending_tail_call_ = false;
	if (expr->callee->get_type() == node_type::identifier_expr) {
		auto* ident = static_cast<identifier_expr*>(expr->callee.get());
		if (ident->name == "weak_from_this" || ident->name == "shared_from_this") {
			if (!expr->arguments.empty()) {
				emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::argument_count_mismatch),
				     add_message("{0}() takes no arguments"), add_symbol(ident->symbol_id));
				emit(opcode::op_null);
				return;
			}
			emit(opcode::op_from_this, ident->name == "weak_from_this" ? 1u : 0u, add_symbol(ident->symbol_id));
			return;
		}
		// parallel_for chunk-loop intrinsics (detail/parallel_transform.cpp k_parallel_chunk_*):
		// reserved names, one opcode each instead of a native call per element
		if (ident->name == "__jai_parallel_advance" && expr->arguments.size() == 1) {
			compile_expression(expr->arguments[0]);
			emit(opcode::op_parallel_advance);
			return;
		}
		if (ident->name == "__jai_parallel_element" && expr->arguments.size() <= 1) {
			uint32_t which = 0;   // the zip array index, a literal in the generated skeleton
			if (!expr->arguments.empty() && expr->arguments[0]->get_type() == node_type::literal_expr) {
				const auto& lit = static_cast<const literal_expr*>(expr->arguments[0].get())->value;
				if (lit.is_int()) { which = static_cast<uint32_t>(lit.unchecked_as_int()); }
			}
			emit(opcode::op_parallel_element, which);
			return;
		}
	}
	if (expr->callee->get_type() == node_type::member_expr) {
		auto member = std::static_pointer_cast<member_expr>(expr->callee);
		// math:: language intrinsics (detail/math_intrinsics.hpp): args + one op_math,
		// no namespace lookup, no call machinery. Arity errors surface from the kernel.
		if (member->is_static) {
			const detail::math_fn fn = detail::math_intrinsic_for_call(member.get());
			if (fn != detail::math_fn::none && expr->arguments.size() <= 8) {
				for (const auto& arg : expr->arguments) {
					compile_expression(arg);
				}
				emit(opcode::op_math, static_cast<uint32_t>(fn), static_cast<uint32_t>(expr->arguments.size()));
				return;
			}
		}
		if (!member->is_static) {
			compile_expression(member->object);
			size_t null_guard = SIZE_MAX;
			if (member->null_safe) {
				null_guard = emit(opcode::op_null_guard, k_invalid_u32);
			}
			// Probe ip sits outside the call_arg_zone (site index patched after push_back)
			size_t probe_ip = SIZE_MAX;
			if (call_args_need_probe(*expr)) {
				probe_ip = emit(opcode::op_probe_member, 0, 0);
			}
			call_site site;
			site.member_node = add_node(member);
			site.call_node = add_node(expr);
			if (member->object->get_type() == node_type::identifier_expr) {
				auto* recv_ident = static_cast<identifier_expr*>(member->object.get());
				site.receiver_symbol = recv_ident->symbol_id;
				site.receiver_slot = identifier_slot_operand(recv_ident);   // the receiver's own op_load slot
			} else if (const expression* root = detail::string_mutator_chain_root(member->object.get());
			           root && root->get_type() == node_type::identifier_expr) {
				auto* root_ident = static_cast<identifier_expr*>(const_cast<expression*>(root));
				site.chain_root_symbol = root_ident->symbol_id;
				site.chain_root_slot = identifier_slot_operand(root_ident);
			}
			if (builtins_) {
				auto builtin_index_for = [&](const std::unordered_map<uint64_t, uint16_t>& index) -> uint16_t {
					auto it = index.find(member->member_id);
					return it != index.end() ? it->second : builtin_method_registries::k_no_builtin;
				};
				site.bi_array = builtin_index_for(builtins_->array_index);
				site.bi_map = builtin_index_for(builtins_->map_index);
				site.bi_string = builtin_index_for(builtins_->string_index);
				chunk_->builtin_indexed = true;
			}
			site.arg_symbols.reserve(expr->arguments.size());
			site.arg_slots.reserve(expr->arguments.size());
			const uint32_t args_begin = static_cast<uint32_t>(chunk_->code.size());
			for (const auto& arg : expr->arguments) {
				if (arg->get_type() == node_type::identifier_expr) {
					auto* arg_ident = static_cast<identifier_expr*>(arg.get());
					site.arg_symbols.push_back(arg_ident->symbol_id);
					site.arg_slots.push_back(identifier_slot_operand(arg_ident));
					if (arg_ident->global_qualified) {
						site.arg_global.resize(expr->arguments.size(), 0);
						site.arg_global[site.arg_symbols.size() - 1] = 1;
					}
				} else {
					site.arg_symbols.push_back(UINT64_MAX);
					site.arg_slots.push_back(k_invalid_u32);
					if (jai::detail::is_ref_bindable_lvalue(arg.get())) {
						site.arg_lvalue_nodes.resize(expr->arguments.size(), k_invalid_u32);
						site.arg_lvalue_nodes[site.arg_symbols.size() - 1] = add_node(arg);
					}
				}
				compile_expression(arg);
			}
			if (!expr->arguments.empty()) {
				chunk_->call_arg_zones.emplace_back(args_begin, static_cast<uint32_t>(chunk_->code.size()));
			}
			chunk_->call_sites.push_back(std::move(site));
			if (probe_ip != SIZE_MAX) {
				chunk_->code[probe_ip].a = static_cast<uint32_t>(chunk_->call_sites.size() - 1);
			}
			emit(opcode::op_call_method, static_cast<uint32_t>(expr->arguments.size()),
			     static_cast<uint32_t>(chunk_->call_sites.size() - 1));
			if (null_guard != SIZE_MAX) {
				patch_jump(null_guard, chunk_->code.size());
			}
			return;
		}
	}

	// Callee-first probe (Dev ruling 2026-07-08): identifier callees resolve via
	// op_probe_callee at the pre-args observation point into the pending register -
	// no value-stack materialization, byte-identical semantics. Other callee shapes
	// keep the verbatim load+op_call pair.
	const bool probe_callee = expr->callee->get_type() == node_type::identifier_expr;
	size_t probe_ip = SIZE_MAX;
	if (probe_callee) {
		probe_ip = emit(opcode::op_probe_callee, 0, static_cast<uint32_t>(expr->arguments.size()));
	} else {
		compile_expression(expr->callee);
		// Computed callees (index/call/static-member results) take the loaded value's
		// callable check at the same observation point
		if (call_args_need_probe(*expr)) {
			emit(opcode::op_probe_member, k_invalid_u32, 1);
		}
	}
	call_site site;
	site.call_node = add_node(expr);
	if (probe_callee) {
		site.callee = make_fused_operand(expr->callee);
	}
	site.arg_symbols.reserve(expr->arguments.size());
	site.arg_slots.reserve(expr->arguments.size());
	const uint32_t args_begin = static_cast<uint32_t>(chunk_->code.size());
	for (const auto& arg : expr->arguments) {
		if (arg->get_type() == node_type::identifier_expr) {
			auto* arg_ident = static_cast<identifier_expr*>(arg.get());
			site.arg_symbols.push_back(arg_ident->symbol_id);
			site.arg_slots.push_back(identifier_slot_operand(arg_ident));
			if (arg_ident->global_qualified) {
				site.arg_global.resize(expr->arguments.size(), 0);
				site.arg_global[site.arg_symbols.size() - 1] = 1;
			}
		} else {
			site.arg_symbols.push_back(UINT64_MAX);
			site.arg_slots.push_back(k_invalid_u32);
			if (jai::detail::is_ref_bindable_lvalue(arg.get())) {
				site.arg_lvalue_nodes.resize(expr->arguments.size(), k_invalid_u32);
				site.arg_lvalue_nodes[site.arg_symbols.size() - 1] = add_node(arg);
			}
		}
		compile_expression(arg);
	}
	if (!expr->arguments.empty()) {
		chunk_->call_arg_zones.emplace_back(args_begin, static_cast<uint32_t>(chunk_->code.size()));
	}
	chunk_->call_sites.push_back(std::move(site));
	if (probe_callee) {
		chunk_->code[probe_ip].a = static_cast<uint32_t>(chunk_->call_sites.size() - 1);
		emit(opcode::op_call_from_scratch, static_cast<uint32_t>(expr->arguments.size()),
		     static_cast<uint32_t>(chunk_->call_sites.size() - 1), tail_site ? 1u : 0u);
	} else {
		emit(opcode::op_call, static_cast<uint32_t>(expr->arguments.size()),
		     static_cast<uint32_t>(chunk_->call_sites.size() - 1));
	}
}

void vm_compiler::compile_member(const std::shared_ptr<member_expr>& expr) {
	if (expr->is_static) {
		emit(opcode::op_get_static, add_node(expr));
		return;
	}
	compile_expression(expr->object);
	emit(opcode::op_get_member, add_node(expr));
}

void vm_compiler::compile_new(const std::shared_ptr<new_expr>& expr) {
	const auto base = expr->type ? expr->type->base_type : script_value_type::jai_invalid_type;
	const bool container = base == script_value_type::jai_array_type || base == script_value_type::jai_map_type;
	if ((container && !expr->arguments.empty()) ||
	    (base == script_value_type::jai_weak_ptr_type && expr->arguments.size() > 1)) {
		// Interpreter rejects these argument counts before evaluating any argument
		emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::argument_count_mismatch), k_invalid_u32, k_invalid_u32);
		emit(opcode::op_null);
		return;
	}
	for (const auto& arg : expr->arguments) {
		compile_expression(arg);
	}
	emit(opcode::op_new, add_node(expr), static_cast<uint32_t>(expr->arguments.size()));
}

// Operand shape decided by the shared kernel (the interpreter takes the same branch at
// visit time): value forms evaluate the operand onto the stack, the reference arms hand
// exec_cast the node and resolve storage there.
void vm_compiler::compile_cast(const std::shared_ptr<cast_expr>& expr) {
	const auto mode = detail::cast_operand_mode_of(expr.get());
	uint32_t sym = k_invalid_u32;
	if (mode == detail::cast_operand_mode::identifier) {
		auto* ident = static_cast<identifier_expr*>(expr->operand.get());
		if (ident->symbol_id == UINT64_MAX) {
			ident->symbol_id = symbolizer_->intern(ident->name);
		}
		sym = add_symbol(ident->symbol_id);
	} else if (mode == detail::cast_operand_mode::value) {
		compile_expression(expr->operand);
	}
	emit(opcode::op_cast, add_node(expr), static_cast<uint32_t>(mode), sym);
}

void vm_compiler::compile_lambda(const std::shared_ptr<lambda_expr>& expr) {
	closure_proto proto;
	proto.lambda = expr;
	proto.body = as_block(expr->body, expr->location);
	proto.default_capture = static_cast<uint8_t>(expr->default_capture);

	uint64_t this_id = symbolizer_->get_this_id();
	for (const auto& cap : expr->captures) {
		uint64_t id = cap.symbol_id != UINT64_MAX ? cap.symbol_id : symbolizer_->intern(cap.name);
		proto.captures.push_back({id, cap.by_reference, cap.outer_slot});
		if (id == this_id) proto.captures_this = true;
	}

	std::unordered_set<uint64_t> declared;
	for (const auto& p : expr->parameters) {
		uint64_t id = p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name);
		declared.insert(id);
	}
	{
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_declared = [&declared](uint64_t id) { declared.insert(id); };
		walker.stmt(proto.body);
	}

	if (expr->outer_slot_plan_built) {
		// Interpreter execution patches captured identifiers to env lookups; the
		// persisted plan is then the only record of their enclosing-frame slots.
		proto.outer_slot_plan = expr->outer_slot_plan;
	} else {
		std::unordered_set<uint64_t> seen;
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_identifier = [&](identifier_expr* ident) {
			if (ident->slot_index == SIZE_MAX) return;
			if (declared.count(ident->symbol_id)) return;
			if (seen.insert(ident->symbol_id).second) {
				proto.outer_slot_plan.emplace_back(ident->symbol_id, ident->slot_index);
			}
		};
		walker.stmt(proto.body);
		for (const auto& [sym, s] : expr->chained_outer_slots) {
			if (seen.insert(sym).second) proto.outer_slot_plan.emplace_back(sym, s);
		}
	}

	if (expr->default_capture != lambda_expr::capture_default::none) {
		std::unordered_set<uint64_t> param_ids;
		for (const auto& p : expr->parameters) {
			param_ids.insert(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name));
		}
		std::unordered_set<uint64_t> seen;
		body_walker walker;
		walker.symbolizer = symbolizer_;
		walker.on_identifier = [&](identifier_expr* ident) {
			if (param_ids.count(ident->symbol_id)) return;
			if (seen.insert(ident->symbol_id).second) {
				proto.used_variables.push_back(ident->symbol_id);
			}
		};
		walker.stmt(proto.body);
		for (uint64_t sym : expr->chained_outer_names) {
			if (!param_ids.count(sym) && seen.insert(sym).second) proto.used_variables.push_back(sym);
		}
	}

	{
		vm_compiler body_compiler(symbolizer_, builtins_);
		body_compiler.typed_lowering(typed_lowering_);
		proto.body_chunk = body_compiler.compile_callable("<lambda>", expr->parameters, proto.body, expr->local_count);
	}

	chunk_->closure_protos.push_back(std::move(proto));
	emit(opcode::op_closure, static_cast<uint32_t>(chunk_->closure_protos.size() - 1));
}

void vm_compiler::compile_variable_decl(const std::shared_ptr<variable_decl>& decl) {
	const bool is_reference = decl->type && decl->type->base_type == script_value_type::jai_reference_type;

	// Locals declared through env fallback in lambda contexts keep working because the
	// slot decision below mirrors identifier loads (declared symbols keep parser slots).
	if (is_reference) {
		if (!decl->initializer) {
			emit(opcode::op_error, static_cast<uint32_t>(runtime_error_code::uninitialized_reference),
			     add_message("Reference variable must be initialized"), add_symbol(decl->name_id));
			return;
		}
		if (decl->initializer->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(decl->initializer.get());
			// bound to a value the unit declares (a value slot, an alias already plain, a program-level value
			// declaration): the alias dereferences a cell, never a getter, so hoisted reads may run ahead of it
			if (decl->slot_index != SIZE_MAX && !ident->global_qualified) {
				const uint32_t referent = identifier_slot_operand(ident);
				const bool plain = referent != k_invalid_u32
					? (ident->names_value_decl || callable_.plain_refs.count(referent) != 0)
					: ident->names_known_value;
				if (plain) { callable_.plain_refs[static_cast<uint32_t>(decl->slot_index)] = decl->name_id; }
			}
			emit(opcode::op_decl_ref_ident, add_node(decl), add_symbol(ident->symbol_id));
		} else {
			compile_expression(decl->initializer);
			emit(opcode::op_decl_ref_value, add_node(decl));
		}
		return;
	}

	uint32_t has_init = 0;
	uint32_t lvalue_init = 0;
	if (decl->initializer) {
		compile_expression(decl->initializer);
		has_init = 1;
		lvalue_init = is_lvalue_shaped(decl->initializer.get()) ? 1u : 0u;
		if (try_lower_decl_move(decl)) return;
		// Dest-addressed peephole (flatstack stage 6, same in-place rewrite shape as
		// emit_compound_store): a fused-binary init lands straight in the decl - one
		// dispatch, no push. The exec falls back to op_decl_var verbatim for every
		// non-scalar decl shape, so this fuses unconditionally.
		auto& code = chunk_->code;
		if (!code.empty() && code.back().op == opcode::op_binary_fused &&
		    decl->initializer->get_type() == node_type::binary_expr) {
			fused_binary_dst_proto dp;
			dp.binary_proto = code.back().a;
			dp.node_index = add_node(decl);
			chunk_->fused_binary_dst_protos.push_back(dp);
			vm_instruction& ins = code.back();
			ins.op = opcode::op_binary_fused_decl;
			ins.a = static_cast<uint32_t>(chunk_->fused_binary_dst_protos.size() - 1);
			ins.b = 0;
			ins.c = 0;
			return;
		}
		// Subscript-init twin (the INDEX_FUSED→DECL_VAR pair): the read lands straight
		// in the decl — a/b (proto, flags) stay, c carries the decl node. Lane facts
		// hoist the exec-time landing checks to HERE (decl_fast slot decls land the
		// scalar read directly; everything else replays op_decl_var).
		if (!code.empty() && code.back().op == opcode::op_index_fused &&
		    decl->initializer->get_type() == node_type::binary_expr) {
			vm_instruction& ins = code.back();
			ins.op = opcode::op_index_fused_decl;
			ins.c = add_node(decl);
			if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store &&
			    !decl->ref_escaping && decl->slot_index != SIZE_MAX) {
				auto& proto = chunk_->fused_index_protos[ins.a];
				proto.decl_slot = static_cast<uint32_t>(decl->slot_index);
				proto.decl_type = decl->type.get();
				const script_value_type bt = decl->type ? decl->type->base_type : script_value_type::jai_any_type;
				switch (bt) {
				case script_value_type::jai_int_type: proto.decl_expect = static_cast<uint8_t>(script_value::TYPEID_INT); break;
				case script_value_type::jai_float_type: proto.decl_expect = static_cast<uint8_t>(script_value::TYPEID_FLOAT); break;
				case script_value_type::jai_bool_type: proto.decl_expect = static_cast<uint8_t>(script_value::TYPEID_BOOL); break;
				case script_value_type::jai_char_type: proto.decl_expect = static_cast<uint8_t>(script_value::TYPEID_CHAR); break;
				case script_value_type::jai_string_type: proto.decl_expect = static_cast<uint8_t>(script_value::TYPEID_STRING); break;
				case script_value_type::jai_any_type: proto.decl_expect = 0xFF; break;
				default: proto.decl_slot = k_invalid_u32; proto.decl_type = nullptr; break;   // container/object decls: general landing
				}
				if (proto.decl_slot != k_invalid_u32 && index_proto_typed_lane_shape(*chunk_, proto)) {
					proto.typed_lane = true;
				}
			}
			return;
		}
	}
	emit(opcode::op_decl_var, add_node(decl), has_init, lvalue_init);
}

void vm_compiler::compile_function_decl(const std::shared_ptr<function_decl>& decl) {
	function_proto proto;
	proto.decl = decl;
	proto.is_coroutine = decl->is_coroutine;
	if (!decl->is_coroutine) {
		proto.fn = std::make_shared<script_defined_function>(
			decl->name, decl->parameters, decl->return_type, decl->body, nullptr, decl->local_count);
		proto.fn->alias_candidate = true;
	} else if (decl->outer_slot_plan_built) {
		// Saved interpreter state has already replaced these identifiers' slots.
		proto.outer_slot_plan = decl->outer_slot_plan;
	} else {
		// Nested coroutine bodies reference enclosing-frame slot locals through the
		// env chain (foreign slots never compile to slot loads), so op_func_decl
		// snapshots them into a capture env - same plan shape as closures
		std::unordered_set<uint64_t> declared;
		for (const auto& p : decl->parameters) {
			declared.insert(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name));
		}
		collect_declared_symbols(decl->body, declared);
		std::unordered_set<uint64_t> seen;
		collect_outer_slot_candidates(decl->body, declared, proto.outer_slot_plan, seen);
	}
	chunk_->function_protos.push_back(std::move(proto));
	emit(opcode::op_func_decl, static_cast<uint32_t>(chunk_->function_protos.size() - 1));
}

void vm_compiler::collect_declared_symbols(const statement_ptr& stmt, std::unordered_set<uint64_t>& out) const {
	body_walker walker;
	walker.symbolizer = symbolizer_;
	walker.on_declared = [&out](uint64_t id) { out.insert(id); };
	walker.stmt(stmt);
}

void vm_compiler::collect_used_identifiers(const statement_ptr& stmt, const lambda_expr* lambda,
                                           std::vector<uint64_t>& out, std::unordered_set<uint64_t>& seen) const {
	std::unordered_set<uint64_t> param_ids;
	for (const auto& p : lambda->parameters) {
		param_ids.insert(p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name));
	}
	body_walker walker;
	walker.symbolizer = symbolizer_;
	walker.on_identifier = [&](identifier_expr* ident) {
		if (param_ids.count(ident->symbol_id)) return;
		if (seen.insert(ident->symbol_id).second) out.push_back(ident->symbol_id);
	};
	walker.stmt(stmt);
}

void vm_compiler::collect_outer_slot_candidates(const statement_ptr& stmt,
                                                const std::unordered_set<uint64_t>& declared,
                                                std::vector<std::pair<uint64_t, size_t>>& out,
                                                std::unordered_set<uint64_t>& seen) const {
	body_walker walker;
	walker.symbolizer = symbolizer_;
	walker.on_identifier = [&](identifier_expr* ident) {
		if (ident->slot_index == SIZE_MAX) return;
		if (declared.count(ident->symbol_id)) return;
		if (seen.insert(ident->symbol_id).second) out.emplace_back(ident->symbol_id, ident->slot_index);
	};
	walker.stmt(stmt);
}

} // namespace jai::vm
