#include <jaiscript/vm/vm_compiler.hpp>
#include <jaiscript/vm/region_ir.hpp>   // the region IR: the operation set, the verifier, the listing, the liveness
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/body_walker.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/ref_lvalue.hpp>
#include <jaiscript/detail/string_mutator_receiver.hpp>
#include <jaiscript/detail/cast_kernel.hpp>
#include <jaiscript/detail/transient_read.hpp>
#include <jaiscript/detail/return_conv.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>

namespace jai::vm {

static bool primitive_declared_type(const type_info* t);   // defined with the quietness predicates below

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
			case opcode::op_deref_top:
			case opcode::op_capture_arg:
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
			case opcode::op_call_temp:
			case opcode::op_member_temp:
			case opcode::op_size_temp:
			case opcode::op_math_temp:
			case opcode::op_math_guard:
			case opcode::op_unary_temp:
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
			case opcode::op_cells_enter:
			case opcode::op_c_arith:
			case opcode::op_c_mload:
			case opcode::op_c_size:
			case opcode::op_c_mstore:
			case opcode::op_set_member_value:
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
			case opcode::op_c_neg:
			case opcode::op_c_bnot:
			case opcode::op_c_probe:
			case opcode::op_c_args:
			case opcode::op_c_call:
			case opcode::op_c_land:
			case opcode::op_c_lnot:
			case opcode::op_c_cmpval:
			case opcode::op_c_range:
			case opcode::op_c_back_step:
			case opcode::op_c_rebind:
			case opcode::op_c_bail:
			case opcode::op_c_exit:
			case opcode::op_c_ret:
			case opcode::op_inline_guard:
			case opcode::op_c_arith_p:
			case opcode::op_c_cmp_p:
			case opcode::op_c_jcmp_p:
			case opcode::op_c_arith2: case opcode::op_c_jcmp2: case opcode::op_c_load2: case opcode::op_c_arith_load: case opcode::op_c_load_jcmp: case opcode::op_c_arith_jump:
			case opcode::op_c_append:
			case opcode::op_c_push:
			case opcode::op_call:
			case opcode::op_probe_callee:
			case opcode::op_call_from_scratch:
			case opcode::op_call_typed:
			case opcode::op_probe_member:
			case opcode::op_return:
			case opcode::op_return_ident:
			case opcode::op_return_binary:
			case opcode::op_return_convert:
			case opcode::op_call_method:
			case opcode::op_call_builtin:
			case opcode::op_concat_append:
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
			case opcode::op_set_member_fused:
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
			case opcode::op_deref_top:
			case opcode::op_capture_arg:
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
			case opcode::op_call_temp:
			case opcode::op_member_temp:
			case opcode::op_size_temp:
			case opcode::op_math_temp:
			case opcode::op_math_guard:
			case opcode::op_unary_temp:
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
			case opcode::op_cells_enter:
			case opcode::op_c_arith:
			case opcode::op_c_mload:
			case opcode::op_c_size:
			case opcode::op_c_mstore:
			case opcode::op_set_member_value:
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
			case opcode::op_c_neg:
			case opcode::op_c_bnot:
			case opcode::op_c_probe:
			case opcode::op_c_args:
			case opcode::op_c_call:
			case opcode::op_c_land:
			case opcode::op_c_lnot:
			case opcode::op_c_cmpval:
			case opcode::op_c_range:
			case opcode::op_c_back_step:
			case opcode::op_c_rebind:
			case opcode::op_c_bail:
			case opcode::op_c_exit:
			case opcode::op_c_ret:
			case opcode::op_inline_guard:
			case opcode::op_c_arith_p:
			case opcode::op_c_cmp_p:
			case opcode::op_c_jcmp_p:
			case opcode::op_c_arith2: case opcode::op_c_jcmp2: case opcode::op_c_load2: case opcode::op_c_arith_load: case opcode::op_c_load_jcmp: case opcode::op_c_arith_jump:
			case opcode::op_c_append:
			case opcode::op_c_push:
			case opcode::op_call:
			case opcode::op_probe_callee:
			case opcode::op_call_from_scratch:
			case opcode::op_call_typed:
			case opcode::op_probe_member:
			case opcode::op_return:
			case opcode::op_return_ident:
			case opcode::op_return_binary:
			case opcode::op_return_convert:
			case opcode::op_call_method:
			case opcode::op_call_builtin:
			case opcode::op_concat_append:
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
			case opcode::op_set_member_fused:
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
void vm_compiler::emit_compound_store(uint32_t symbol_index, uint32_t slot, uint32_t kind_flags, const expression* rhs, uint8_t typed_class) {
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
				if (!loops_.empty() && typed_lowering_ && temps_active_ && chunk_->is_function_body && slot != k_invalid_u32 &&
				    (kind_flags & compound_flag_no_result) && !(kind_flags & (compound_flag_old_result | compound_flag_incdec))) {
					// inside a loop the operand lands in a temp first (the same kernel, the result into the temp) and the compound reads
					// the temp: the typed rows and the cell translation take both halves, where a compound over a binary proto stays general
					const uint32_t t0 = static_cast<uint32_t>(temp_next_);
					++temp_next_;
					if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
					vm_instruction& last = code.back();
					last.op = opcode::op_binary_fused_temp; last.b = t0;
					cp.rhs_proto = k_invalid_u32;
					cp.rhs.slot = t0; cp.rhs.symbol = temp_symbol_index(); cp.rhs.direct = true;
					cp.symbol = symbol_index; cp.slot = slot; cp.kind_flags = kind_flags; cp.typed_class = typed_class;
					chunk_->compound_fused_protos.push_back(cp);
					emit(opcode::op_compound_fused, static_cast<uint32_t>(chunk_->compound_fused_protos.size() - 1), 0, 0);
					return;
				}
				cp.rhs_proto = code.back().a;
				fuse = true;
			}
			break;
		case node_type::member_expr:
			member_tail_into_temp(rhs);
			if (code.back().op == opcode::op_load && code.size() >= 2 && code[code.size() - 2].op == opcode::op_member_temp) {
				cp.rhs_proto = k_invalid_u32;
				cp.rhs.slot = code.back().a;
				cp.rhs.symbol = code.back().b;
				cp.rhs.direct = true;
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
			cp.typed_class = typed_class;
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

static bool loop_free_body(const ast_node* n);

std::shared_ptr<chunk> vm_compiler::compile_program(const std::vector<declaration_ptr>& declarations) {
	auto result = std::make_shared<chunk>();
	chunk_ = result.get();
	chunk_->pinned_decls = declarations;
	chunk_->first_temp_slot = SIZE_MAX;   // a program chunk has no compiler slots (temps_active_ is false)
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
	chunk_->first_temp_slot = local_count;
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
	callable_body_ = body.get();
	frame_param_kinds_.clear();
	frame_eligible_ = parameters.size() <= 8 && loop_free_body(body.get());
	for (size_t i = 0; frame_eligible_ && i < parameters.size(); ++i) {
		const parameter& p = parameters[i];
		const uint8_t k = raw_scalar_kind(p.type.get());
		if (k == 0 || p.is_reference || p.ref_escaping || p.default_value || p.slot_index != i) { frame_eligible_ = false; break; }
		frame_param_kinds_.push_back(k);
	}
	for (const auto& p : parameters) {
		uint64_t id = p.symbol_id != UINT64_MAX ? p.symbol_id : symbolizer_->intern(p.name);
		callable_.declared.insert(id);
		if (p.slot_index != SIZE_MAX && (p.is_reference || p.ref_escaping)) { callable_.escaping.insert(static_cast<uint32_t>(p.slot_index)); }
		if (p.slot_index != SIZE_MAX && !p.is_reference && primitive_declared_type(p.type.get())) { callable_.primitive_slots.insert(static_cast<uint32_t>(p.slot_index)); }
#ifdef JAISCRIPT_VM_PROFILE
		if (p.slot_index != SIZE_MAX) { chunk_->profile_params.push_back({ static_cast<uint32_t>(p.slot_index), p.type.get(), p.is_reference }); }
#endif
		if (p.is_reference && p.slot_index != SIZE_MAX) { chunk_->reference_slots.push_back(static_cast<uint32_t>(p.slot_index)); }   // its load dereferences the caller's binding, possibly through a getter
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
	frame_param_kinds_.clear();
	frame_eligible_ = false;
	temps_active_ = false;
	chunk_ = nullptr;
	return result;
}

// a body the first form of a frame program covers: no loop anywhere (a switch, a try or a nested declaration counts as one: the region refuses them)
static bool loop_free_body(const ast_node* n) {
	if (!n) return true;
	switch (n->get_type()) {
	case node_type::block_stmt:
		for (const auto& d : static_cast<const block_stmt*>(n)->declarations) { if (!loop_free_body(d.get())) return false; }
		return true;
	case node_type::statement_decl: return loop_free_body(static_cast<const statement_decl*>(n)->statement.get());
	case node_type::if_stmt: { const auto* s = static_cast<const if_stmt*>(n); return loop_free_body(s->then_statement.get()) && loop_free_body(s->else_statement.get()); }
	case node_type::expression_stmt: case node_type::return_stmt: case node_type::variable_decl: case node_type::expression_decl: return true;
	default: return false;
	}
}

// a call a frame program's body makes that can be raw at run time: to the function itself, or to a loop-free function of the raw shape that forms a
// frame program itself (one of its own raw-signature calls reaches one; a cycle of such functions forms programs all round); a leaf, or a function
// whose calls reach only loops, has no frame program, so a call to one stays the general call
bool vm_compiler::raw_call_reaches_program(const function_decl* callee, const block_stmt* self_body) {
	std::vector<const function_decl*> visiting;
	return raw_call_reaches_program(callee, self_body, visiting);
}
bool vm_compiler::raw_call_reaches_program(const function_decl* callee, const block_stmt* self_body, std::vector<const function_decl*>& visiting) {
	if (!callee || !callee->body) return false;
	if (callee->body.get() == self_body) return true;
	if (!loop_free_body(callee->body.get())) return false;
	if (std::find(visiting.begin(), visiting.end(), callee) != visiting.end()) return true;
	if (visiting.size() >= 8) return false;
	visiting.push_back(callee);
	bool found = false;
	auto visit_expr = [&](auto&& self, const expression* e) -> void {
		if (!e || found) return;
		switch (e->get_type()) {
		case node_type::call_expr: {
			const auto* c = static_cast<const call_expr*>(e);
			const auto d = c->proven_callee_decl.lock();
			if (d && raw_signature(d.get()) && c->arguments.size() == d->parameters.size() && raw_call_reaches_program(d.get(), callee->body.get(), visiting)) { found = true; return; }
			for (const auto& a : c->arguments) { self(self, a.get()); }
			return;
		}
		case node_type::binary_expr: { const auto* b = static_cast<const binary_expr*>(e); self(self, b->left.get()); self(self, b->right.get()); return; }
		case node_type::unary_expr: self(self, static_cast<const unary_expr*>(e)->operand.get()); return;
		case node_type::ternary_expr: { const auto* t = static_cast<const ternary_expr*>(e); self(self, t->condition.get()); self(self, t->then_expression.get()); self(self, t->else_expression.get()); return; }
		case node_type::assignment_expr: self(self, static_cast<const assignment_expr*>(e)->value.get()); return;
		case node_type::cast_expr: self(self, static_cast<const cast_expr*>(e)->operand.get()); return;
		default: return;
		}
	};
	std::vector<const ast_node*> stack{ callee->body.get() };
	while (!stack.empty() && !found) {
		const ast_node* n = stack.back();
		stack.pop_back();
		if (!n) continue;
		switch (n->get_type()) {
		case node_type::block_stmt: for (const auto& d : static_cast<const block_stmt*>(n)->declarations) { stack.push_back(d.get()); } break;
		case node_type::statement_decl: stack.push_back(static_cast<const statement_decl*>(n)->statement.get()); break;
		case node_type::if_stmt: { const auto* s = static_cast<const if_stmt*>(n); visit_expr(visit_expr, s->condition.get()); stack.push_back(s->then_statement.get()); stack.push_back(s->else_statement.get()); break; }
		case node_type::expression_stmt: visit_expr(visit_expr, static_cast<const expression_stmt*>(n)->expression.get()); break;
		case node_type::expression_decl: visit_expr(visit_expr, static_cast<const expression_decl*>(n)->expression.get()); break;
		case node_type::return_stmt: visit_expr(visit_expr, static_cast<const return_stmt*>(n)->value.get()); break;
		case node_type::variable_decl: visit_expr(visit_expr, static_cast<const variable_decl*>(n)->initializer.get()); break;
		default: break;
		}
	}
	visiting.pop_back();
	return found;
}

uint8_t vm_compiler::raw_scalar_kind(const type_info* t) {
	if (!t) return 0;
	switch (t->base_type) {
	case script_value_type::jai_int_type: return static_cast<uint8_t>(script_value::TYPEID_INT);
	case script_value_type::jai_float_type: return static_cast<uint8_t>(script_value::TYPEID_FLOAT);
	case script_value_type::jai_bool_type: return static_cast<uint8_t>(script_value::TYPEID_BOOL);
	default: return 0;
	}
}

bool vm_compiler::raw_signature(const function_decl* fn) {
	if (!fn || !fn->body || fn->is_coroutine || fn->parameters.size() > 8 || raw_scalar_kind(fn->return_type.get()) == 0) return false;
	for (size_t i = 0; i < fn->parameters.size(); ++i) {
		const parameter& p = fn->parameters[i];
		if (raw_scalar_kind(p.type.get()) == 0 || p.is_reference || p.ref_escaping || p.default_value || p.slot_index != i) return false;
	}
	return true;
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
		if (!implicit && try_inline_expression(ed->expression)) return;
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
		emit(opcode::op_extern_decl, add_node(decl));
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
		if (try_inline_expression(e)) return;
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
		if (static_cast<return_stmt*>(stmt.get())->inline_exit) { compile_graft_exit(); return; }
		if (try_inline_return(stmt)) return;
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

// A condition in branch position is jumping code all the way down: `&&`, `||` and `!` nest as jumps, no bool is
// materialized, a leaf comparison is one fused compare-jump in the false sense or a fused comparison and its jump in
// the true sense, and the operands run in source order with C++ short-circuit (the interpreter's visit_binary is the
// oracle). false_jumps collect the edges taken when the condition is false; the true sense collects the edges taken
// when it is true and falls through when it is false.
void vm_compiler::compile_branch_condition(const expression_ptr& cond, std::vector<size_t>& false_jumps) {
	if (cond->get_type() == node_type::unary_expr) {
		auto* u = static_cast<unary_expr*>(cond.get());
		if (u->op.type == token_type::bang) { compile_branch_condition_true(u->operand, false_jumps); return; }
	}
	if (cond->get_type() == node_type::binary_expr) {
		auto* b = static_cast<binary_expr*>(cond.get());
		if (b->op.type == token_type::ampersand_ampersand) {
			compile_branch_condition(b->left, false_jumps);
			compile_branch_condition(b->right, false_jumps);
			return;
		}
		if (b->op.type == token_type::pipe_pipe) {
			std::vector<size_t> true_jumps;
			compile_branch_condition_true(b->left, true_jumps);
			compile_branch_condition(b->right, false_jumps);
			for (size_t at : true_jumps) { patch_jump(at, chunk_->code.size()); }
			return;
		}
	}
	compile_expression(cond);
	if (cond->get_type() == node_type::member_expr) { member_tail_into_temp(cond.get()); }
	false_jumps.push_back(emit_cond_jump_false(cond.get()));
}

void vm_compiler::compile_branch_condition_true(const expression_ptr& cond, std::vector<size_t>& true_jumps) {
	if (cond->get_type() == node_type::unary_expr) {
		auto* u = static_cast<unary_expr*>(cond.get());
		if (u->op.type == token_type::bang) { compile_branch_condition(u->operand, true_jumps); return; }
	}
	if (cond->get_type() == node_type::binary_expr) {
		auto* b = static_cast<binary_expr*>(cond.get());
		if (b->op.type == token_type::pipe_pipe) {
			compile_branch_condition_true(b->left, true_jumps);
			compile_branch_condition_true(b->right, true_jumps);
			return;
		}
		if (b->op.type == token_type::ampersand_ampersand) {
			std::vector<size_t> false_jumps;
			compile_branch_condition(b->left, false_jumps);
			compile_branch_condition_true(b->right, true_jumps);
			for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
			return;
		}
	}
	compile_expression(cond);
	if (cond->get_type() == node_type::member_expr) { member_tail_into_temp(cond.get()); }
	true_jumps.push_back(emit(opcode::op_jump_if_true, k_invalid_u32, expression_returns_bool(cond.get()) ? 1u : 0u));
}

void vm_compiler::compile_if(if_stmt* stmt) {
	std::vector<size_t> false_jumps;
	const size_t cond_mark = temp_next_;
	compile_branch_condition(hoisted_condition(stmt->condition), false_jumps);
	temp_next_ = cond_mark;   // the hoisted leaves' temps die at the jumps
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
	const size_t cond_mark = temp_next_;
	compile_branch_condition(hoisted_condition(stmt->condition), exit_jumps);
	temp_next_ = cond_mark;

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
		const size_t cond_mark = temp_next_;
		compile_branch_condition(hoisted_condition(stmt->condition), exit_jumps);
		temp_next_ = cond_mark;
	}

	loops_.push_back({});
	loops_.back().scope_depth = scope_depth_;

	compile_statement(stmt->body);

	const size_t continue_target = chunk_->code.size();
	for (size_t at : loops_.back().continue_patches) {
		patch_jump(at, continue_target);
	}
	if (stmt->update) { compile_hoisted_update(stmt->update); }
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

// The arms of `x = c ? a : b;` onto a slot, each a store of its own: a nested ternary recurses, a value takes the move row when it is
// an identifier or a literal, the fused-binary store when it is a fused binary, else the general store; the proofs of the whole
// assignment (a provable typed store, the same-type proof) are not claimed for an arm, so the store enforces the declared type
void vm_compiler::compile_ternary_store(identifier_expr* ident, assignment_expr* assign, const std::shared_ptr<ternary_expr>& t) {
	std::vector<size_t> false_jumps;
	compile_branch_condition(t->condition, false_jumps);
	compile_store_arm(ident, assign, t->then_expression);
	const size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
	for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
	compile_store_arm(ident, assign, t->else_expression);
	patch_jump(jump_end, chunk_->code.size());
}

void vm_compiler::compile_store_arm(identifier_expr* ident, assignment_expr* assign, const expression_ptr& value) {
	if (value->get_type() == node_type::ternary_expr) { compile_ternary_store(ident, assign, std::static_pointer_cast<ternary_expr>(value)); return; }
	if (value_temp_operand(value.get())) {
		// `x = c ? -e : y;`: a unary, math, element or member arm lands in a compiler temp and the move row stores it, as the
		// statement form does
		const uint32_t dst = identifier_slot_operand(ident);
		const size_t temp_mark = temp_next_;
		fused_operand src;
		if (dst < k_typed_slot_mask &&
		    compile_value_into_temp(value, fusable_index_expr(value.get()) ? operand_expect(static_cast<const binary_expr*>(value.get())) : 0xFF, src)) {
			const uint32_t flags = store_flag_no_result | store_flag_rhs_lvalue;
			const size_t ip = chunk_->code.size();
			chunk::typed_site& site = typed_site_at(ip);
			site.orig = vm_instruction{opcode::op_load, src.slot, src.symbol, 0};
			site.orig2 = vm_instruction{opcode::op_store, add_symbol(ident->symbol_id), dst, flags};
			emit(opcode::op_t_store_mov, dst, src.slot, flags);
			note_ident_node(ip, assign->target);
			temp_next_ = temp_mark;
			return;
		}
	}
	compile_expression(value);
	uint32_t flags = store_flag_no_result;
	if (is_lvalue_shaped(value.get())) flags |= store_flag_rhs_lvalue;
	auto& code = chunk_->code;
	if (!code.empty() && code.back().op == opcode::op_binary_fused && value->get_type() == node_type::binary_expr) {
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
		return;
	}
	if (try_lower_store_move(ident, value, flags)) return;
	const size_t store_ip = emit(opcode::op_store, add_symbol(ident->symbol_id), identifier_slot_operand(ident), flags);
	note_ident_node(store_ip, assign->target);
}

bool vm_compiler::compile_no_result_expression(const expression_ptr& expr) {
	if (expr->get_type() == node_type::assignment_expr) {
		auto* assign = static_cast<assignment_expr*>(expr.get());
		if (assign->target->get_type() == node_type::identifier_expr) {
			auto* ident = static_cast<identifier_expr*>(assign->target.get());
			if (assign->op.type == token_type::equal && callable_.active && ident->names_value_decl && !ident->global_qualified &&
			    value_temp_operand(assign->value.get())) {
				// `x = a[i];`, `x = -e;` or `x = math::f(e);` in statement position: the value lands in a compiler temp (the T_LOAD
				// row, the unary row, the math row) and the move row takes it, two destination-addressed instructions in place
				// of a push and a store
				const uint32_t dst = identifier_slot_operand(ident);
				const size_t temp_mark = temp_next_;
				fused_operand src;
				const bool landed = dst != k_invalid_u32 && dst < k_typed_slot_mask &&
				    compile_value_into_temp(assign->value, fusable_index_expr(assign->value.get()) ? operand_expect(static_cast<const binary_expr*>(assign->value.get())) : 0xFF, src);
				if (landed) {
					uint32_t flags = store_flag_no_result | store_flag_rhs_lvalue;
					if (assign->typed_store_provable) flags |= store_flag_type_provable;
					if (assign->static_store_proof == proof_same_type) flags |= store_flag_same_type;
					const size_t ip = chunk_->code.size();
					chunk::typed_site& site = typed_site_at(ip);
					site.orig = vm_instruction{opcode::op_load, src.slot, src.symbol, 0};
					site.orig2 = vm_instruction{opcode::op_store, add_symbol(ident->symbol_id), dst, flags};
					emit(opcode::op_t_store_mov, dst, src.slot, flags);
					note_ident_node(ip, assign->target);
					temp_next_ = temp_mark;   // the read's temp dies into the move
					return true;
				}
			}
			if (assign->op.type == token_type::equal && assign->value->get_type() == node_type::ternary_expr && callable_.active &&
			    ident->names_value_decl && !ident->global_qualified && identifier_slot_operand(ident) != k_invalid_u32) {
				// `x = c ? a : b;` onto a slot: the condition's jumps and one store landing per arm (the move row, the fused-binary store, else
				// the general store), so no value crosses a jump on the operand stack; the interpreter evaluates the same arm and stores once
				compile_ternary_store(ident, assign, std::static_pointer_cast<ternary_expr>(assign->value));
				return true;
			}
			if (assign->op.type != token_type::equal && !loops_.empty() && typed_lowering_ && temps_active_ && chunk_->is_function_body &&
			    !ident->global_qualified && assign->value->get_type() != node_type::member_expr &&
			    assign->static_operand_class >= operand_class_int_int && assign->static_operand_class <= operand_class_float_int && value_temp_operand(assign->value.get())) {
				// `x op= a[i];` (or a unary or math operand) inside a loop: the operand lands in a temp and the compound reads it, as
				// row 167's binary operand does, so the typed rows and the cell translation take the statement
				const uint32_t dst = identifier_slot_operand(ident);
				const size_t temp_mark = temp_next_;
				fused_operand src;
				if (dst != k_invalid_u32 && dst < k_typed_slot_mask &&
				    compile_value_into_temp(assign->value, fusable_index_expr(assign->value.get()) ? operand_expect(static_cast<const binary_expr*>(assign->value.get())) : 0xFF, src)) {
					compound_fused_proto cp;
					cp.rhs_proto = k_invalid_u32;
					cp.rhs = src;
					cp.symbol = add_symbol(ident->symbol_id); cp.slot = dst;
					cp.kind_flags = compound_kind_for(assign->op.type) | compound_flag_no_result;
					cp.typed_class = assign->static_operand_class;
					chunk_->compound_fused_protos.push_back(cp);
					emit(opcode::op_compound_fused, static_cast<uint32_t>(chunk_->compound_fused_protos.size() - 1), 0, 0);
					temp_next_ = temp_mark;   // the operand's temp dies into the compound
					return true;
				}
			}
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
				     assign->value.get(), assign->static_operand_class);
			}
			return true;
		}
		// Statement-position member store onto a frame-slot base (`p.x = v;`, `t.posx = px;`, `p.life = p.life - 1;`): the value an
		// operand (an identifier, a literal, or the temp a computed value lands in, as the subscript store takes it), one op, no push,
		// no result; a static member, an environment base or a value shape the temps decline keep the general form
		if (assign->op.type == token_type::equal && assign->target->get_type() == node_type::member_expr && callable_.active) {
			auto* member = static_cast<member_expr*>(assign->target.get());
			const uint32_t base_slot = member->is_static ? k_invalid_u32 : member_base_slot(member);
			if (base_slot != k_invalid_u32 && base_slot < k_typed_slot_mask) {
				const expression_ptr& value = assign->value;
				member_store_proto mp; mp.node = add_node(assign->target); mp.proof = assign->static_store_proof; mp.base = base_slot;
				bool value_operand = false;
				if (value->get_type() == node_type::literal_expr || (value->get_type() == node_type::identifier_expr && identifier_slot_operand(static_cast<identifier_expr*>(value.get())) != k_invalid_u32)) {
					mp.value = make_fused_operand(value); value_operand = true;
				} else if (fusable_index_expr(value.get()) && compile_subscript_into_temp(value, operand_expect(static_cast<const binary_expr*>(value.get())), mp.value)) {
					value_operand = true;
				} else if (unary_temp_operand(value.get()) || math_temp_operand(value.get()) != detail::math_fn::none || member_temp_operand(value.get())) {
					compile_value_into_temp(value, 0xFF, mp.value); value_operand = true;
				} else if (value->get_type() == node_type::binary_expr && binary_lands_fused(static_cast<binary_expr*>(value.get()))) {
					compile_expression(value);
					vm_instruction& last = chunk_->code.back();
					if (last.op == opcode::op_binary_fused) {
						const uint32_t t0 = static_cast<uint32_t>(temp_next_);
						++temp_next_;
						if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
						last.op = opcode::op_binary_fused_temp; last.b = t0;
						mp.value = fused_operand{}; mp.value.slot = t0; mp.value.symbol = temp_symbol_index(); mp.value.direct = true;
						value_operand = true;
					} else {
						// the value is on the stack: the general store consumes it
						emit(opcode::op_set_member_fused, mp.node, mp.proof, base_slot);
						emit(opcode::op_pop);
						return true;
					}
				}
				if (value_operand) {
					chunk_->member_store_protos.push_back(mp);
					emit(opcode::op_set_member_value, static_cast<uint32_t>(chunk_->member_store_protos.size() - 1));
					return true;
				}
			}
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
		std::vector<size_t> false_jumps;
		compile_branch_condition(t->condition, false_jumps);
		compile_expression(t->then_expression);
		size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
		for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
		compile_expression(t->else_expression);
		patch_jump(jump_end, chunk_->code.size());
		return;
	}
	case node_type::array_literal_expr: {
		auto* arr = static_cast<array_literal_expr*>(expr.get());
		for (size_t k = 0; k < arr->elements.size(); ++k) {
			compile_expression(arr->elements[k]);
			for (size_t n = k + 1; n < arr->elements.size(); ++n) {
				if (!detail::transient_read_marker::callout_free(arr->elements[n].get())) { deref_ahead_of(arr->elements[k].get(), arr->elements[n].get()); break; }
			}
		}
		emit(opcode::op_array, static_cast<uint32_t>(arr->elements.size()));
		return;
	}
	case node_type::map_literal_expr: {
		auto* m = static_cast<map_literal_expr*>(expr.get());
		auto later_callout = [&](size_t entry, bool from_value) -> const expression* {
			if (!from_value && !detail::transient_read_marker::callout_free(m->entries[entry].second.get())) return m->entries[entry].second.get();
			for (size_t n = entry + 1; n < m->entries.size(); ++n) {
				if (!detail::transient_read_marker::callout_free(m->entries[n].first.get())) return m->entries[n].first.get();
				if (!detail::transient_read_marker::callout_free(m->entries[n].second.get())) return m->entries[n].second.get();
			}
			return nullptr;
		};
		for (size_t k = 0; k < m->entries.size(); ++k) {
			compile_expression(m->entries[k].first);
			if (const expression* later = later_callout(k, false)) { deref_ahead_of(m->entries[k].first.get(), later); }
			compile_expression(m->entries[k].second);
			if (const expression* later = later_callout(k, true)) { deref_ahead_of(m->entries[k].second.get(), later); }
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
	auto typed_operand = [](const fused_operand& o) {
		return o.direct || o.const_index != k_invalid_u32 ||
		       (o.slot == k_invalid_u32 && o.symbol != k_invalid_u32 && !o.is_subscript() && o.member_node == k_invalid_u32 && o.load_flags == 0);   // an environment value: the checker proved its declared kind
	};
	if (!typed_operand(p.left) || !typed_operand(p.right)) return;
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
bool vm_compiler::try_lower_decl_move(const std::shared_ptr<variable_decl>& decl, const expression* init) {
	if (!callable_.active) return false;
	if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store ||
	    decl->ref_escaping || decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask) return false;
	auto& code = chunk_->code;
	if (code.empty()) return false;
	vm_instruction& last = code.back();
	const size_t ip = code.size() - 1;
	if (init->get_type() == node_type::identifier_expr && last.op == opcode::op_load &&
	    last.a != k_invalid_u32 && last.c == 0) {
		const vm_instruction orig = last;
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = orig;
		site.orig2 = vm_instruction{opcode::op_decl_var, add_node(decl), 1, 1};
		site.stamp = decl->type.get();
		last = vm_instruction{opcode::op_t_decl_mov, static_cast<uint32_t>(decl->slot_index), orig.a, site.orig2.a};
		return true;
	}
	if (init->get_type() == node_type::identifier_expr && last.op == opcode::op_load && last.a == k_invalid_u32 && last.c == 0 && last.b < chunk_->symbols.size()) {
		// `T x = G;` from an environment value: the move reads it through the LOAD's own resolution (its cache slot)
		const vm_instruction orig = last;
		fused_operand env;
		env.symbol = orig.b;
		env.ident_node = ip < chunk_->ident_nodes.size() ? chunk_->ident_nodes[ip] : k_invalid_u32;
		chunk_->typed_env_operands.push_back(env);
		const uint32_t operand = k_typed_env_bit | static_cast<uint32_t>(chunk_->typed_env_operands.size() - 1);
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = orig;
		site.orig2 = vm_instruction{opcode::op_decl_var, add_node(decl), 1, 1};
		site.stamp = decl->type.get();
		last = vm_instruction{opcode::op_t_decl_mov, static_cast<uint32_t>(decl->slot_index), operand, site.orig2.a};
		return true;
	}
	if (init->get_type() == node_type::literal_expr && last.op == opcode::op_const) {
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

// `x = y;` / `x = 5;` in statement position onto a frame slot: the LOAD or CONST becomes one move row and no STORE
// follows. The row's guard is total (the same-type payload test on the live slot, else the exact general pair replayed
// from the typed site), so it is the general form of the move and needs neither a proof nor the proofs flag; the
// proofs flag gates the arithmetic and compare rows only
bool vm_compiler::try_lower_store_move(identifier_expr* ident, const expression_ptr& value, uint32_t flags) {
	if (!callable_.active) return false;
	const uint32_t need = store_flag_no_result;
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
	// the cell emission's operand normalization: a comparison with a literal on the left is its mirror (never its negation: a NaN fails both
	// orders), a commutative typed operator swaps, and the token of a typed operator for the any-kind form
	auto mirror_tok = [&](uint8_t t) -> uint8_t { return static_cast<uint8_t>(mirrored(static_cast<token_type>(t))); };
	auto mirror_fast = [](opcode o) -> opcode {
		switch (o) {
		case opcode::op_t_jlt_ii: return opcode::op_t_jgt_ii; case opcode::op_t_jgt_ii: return opcode::op_t_jlt_ii;
		case opcode::op_t_jle_ii: return opcode::op_t_jge_ii; case opcode::op_t_jge_ii: return opcode::op_t_jle_ii;
		case opcode::op_t_jlt_ff: return opcode::op_t_jgt_ff; case opcode::op_t_jgt_ff: return opcode::op_t_jlt_ff;
		case opcode::op_t_jle_ff: return opcode::op_t_jge_ff; case opcode::op_t_jge_ff: return opcode::op_t_jle_ff;
		default: return o;
		}
	};
	auto commutative_fast = [](opcode o) {
		switch (o) {
		case opcode::op_t_add_ii: case opcode::op_t_mul_ii: case opcode::op_t_and_ii: case opcode::op_t_or_ii: case opcode::op_t_xor_ii:
		case opcode::op_t_add_ff: case opcode::op_t_mul_ff:
			return true;
		default: return false;
		}
	};
	auto arith_token_of = [](opcode o) -> uint8_t {
		switch (o) {
		case opcode::op_t_add_ii: case opcode::op_t_add_ff: return static_cast<uint8_t>(token_type::plus);
		case opcode::op_t_sub_ii: case opcode::op_t_sub_ff: return static_cast<uint8_t>(token_type::minus);
		case opcode::op_t_mul_ii: case opcode::op_t_mul_ff: return static_cast<uint8_t>(token_type::star);
		case opcode::op_t_div_ii: case opcode::op_t_div_ff: return static_cast<uint8_t>(token_type::slash);
		case opcode::op_t_mod_ii: case opcode::op_t_mod_ff: return static_cast<uint8_t>(token_type::percent);
		case opcode::op_t_and_ii: return static_cast<uint8_t>(token_type::ampersand);
		case opcode::op_t_or_ii: return static_cast<uint8_t>(token_type::pipe);
		case opcode::op_t_xor_ii: return static_cast<uint8_t>(token_type::caret);
		case opcode::op_t_shl_ii: return static_cast<uint8_t>(token_type::left_shift);
		case opcode::op_t_shr_ii: return static_cast<uint8_t>(token_type::right_shift);
		default: return 0;
		}
	};
	// left slot + right (slot or constant of the class); a literal left swaps when the op allows; an arithmetic row also takes
	// an environment value on either side, read through the replaced instruction's cache slot for that side
	auto operands = [&](const fused_binary_proto& bp, token_type& op, uint32_t& left, uint32_t& right, bool env_ok) -> bool {
		const size_t want = bp.typed_class == 1 ? script_value::TYPEID_INT : script_value::TYPEID_FLOAT;
		auto direct_slot = [&](const fused_operand& o, uint32_t& out) {
			if (o.direct && o.slot != k_invalid_u32 && o.slot < k_typed_slot_mask && o.const_index == k_invalid_u32) { out = o.slot; return true; }
			if (!env_ok || o.slot != k_invalid_u32 || o.direct || o.const_index != k_invalid_u32 || o.symbol == k_invalid_u32 || o.is_subscript() || o.member_node != k_invalid_u32 || o.load_flags != 0) return false;
			c.typed_env_operands.push_back(o);
			out = k_typed_env_bit | static_cast<uint32_t>(c.typed_env_operands.size() - 1);
			return true;
		};
		auto constant = [&](const fused_operand& o, uint32_t& out) {
			if (o.const_index == k_invalid_u32 || o.const_index >= k_typed_slot_mask) return false;
			if (c.constants[o.const_index].raw_storage_index() != want) return false;
			out = k_typed_const_bit | o.const_index; return true;
		};
		uint32_t l = 0, r = 0;
		const size_t env_mark = c.typed_env_operands.size();
		if (direct_slot(bp.left, l) && (direct_slot(bp.right, r) || constant(bp.right, r))) { left = l; right = r; return true; }
		c.typed_env_operands.resize(env_mark);
		if (constant(bp.left, r) && direct_slot(bp.right, l) && commutes(op)) {
			if (l & k_typed_env_bit) { c.typed_env_operands.resize(env_mark); return false; }   // the right side's cache slot would move to the left
			op = mirrored(op); left = l; right = r; return true;
		}
		c.typed_env_operands.resize(env_mark);
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
			if (arith_op(op, bp.typed_class) == opcode::op_halt) break;
			uint32_t left, right;
			if (!operands(bp, op, left, right, true)) break;
			const opcode t = arith_op(op, bp.typed_class);
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
			if (arith_op(op, bp.typed_class) == opcode::op_halt) break;
			const size_t env_mark = c.typed_env_operands.size();
			uint32_t left, right;
			if (!operands(bp, op, left, right, true)) break;
			const opcode t = arith_op(op, bp.typed_class);
			uint32_t dst;
			type_info* stamp = nullptr;
			if (ins.op == opcode::op_binary_fused_decl) {
				const auto* decl = static_cast<const variable_decl*>(c.nodes[dp.node_index].get());
				if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store ||
				    decl->ref_escaping || decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask) { c.typed_env_operands.resize(env_mark); break; }
				const script_value_type bt = decl->type ? decl->type->base_type : script_value_type::jai_any_type;
				const bool fits = bt == script_value_type::jai_any_type ||
				                  (bp.typed_class == 1 && bt == script_value_type::jai_int_type) ||
				                  (bp.typed_class == 2 && bt == script_value_type::jai_float_type);
				if (!fits) { c.typed_env_operands.resize(env_mark); break; }
				dst = static_cast<uint32_t>(decl->slot_index) | (k_typed_dst_decl << k_typed_dst_shift);
				stamp = decl->type.get();
			} else {

				const uint32_t need = store_flag_no_result | store_flag_same_type | store_flag_type_provable;
				if ((dp.flags & need) != need || (dp.flags & (store_flag_ref_alias | store_flag_global_only)) ||
				    dp.slot == k_invalid_u32 || dp.slot >= k_typed_slot_mask) { c.typed_env_operands.resize(env_mark); break; }
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
			// `x op= y` (bare rhs, statement position) on a direct slot: the compound lane's row of the checker's class
			// (int/int the int row, float/float the float row, unknown the int row speculated, a mixed class the general
			// compound, which converts); kind_flags beyond no_result/incdec keep the general op
			const compound_fused_proto& cp = c.compound_fused_protos[ins.a];
			if (cp.rhs_proto != k_invalid_u32 || cp.slot == k_invalid_u32 || cp.slot >= k_typed_slot_mask) break;
			if ((cp.kind_flags & ~(compound_kind_mask | compound_flag_no_result | compound_flag_incdec)) != 0) break;
			if (!(cp.kind_flags & compound_flag_no_result)) break;
			const uint8_t cls = cp.typed_class == operand_class_float_float ? 2 : (cp.typed_class == operand_class_int_int || cp.typed_class == operand_class_unknown) ? 1 : 0;
			if (cls == 0) break;
			token_type tok = token_type::plus;
			bool known = true;
			switch (cp.kind_flags & compound_kind_mask) {
			case compound_plus: tok = token_type::plus; break;
			case compound_minus: tok = token_type::minus; break;
			case compound_star: tok = token_type::star; break;
			case compound_slash: tok = token_type::slash; break;
			default: known = false; break;
			}
			const opcode t = known ? arith_op(tok, cls) : opcode::op_halt;
			if (t == opcode::op_halt) break;
			uint32_t right;
			if (cp.rhs.const_index != k_invalid_u32) {
				if (cp.rhs.const_index >= k_typed_slot_mask || c.constants[cp.rhs.const_index].raw_storage_index() != (cls == 1 ? script_value::TYPEID_INT : script_value::TYPEID_FLOAT)) break;
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
			site.guard_temp = fp.container_guard; site.guard_always = fp.guard_always;
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
			if (!operands(bp, op, left, right, false)) break;
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
	// the operation set (ir, ir_op, k_ir_env_bit), the region and its analyses: jaiscript/vm/region_ir.hpp
	auto stamp_kind = [](const type_info* t) -> uint8_t {
		if (!t) return 0;
		if (t->base_type == script_value_type::jai_int_type) return static_cast<uint8_t>(script_value::TYPEID_INT);
		if (t->base_type == script_value_type::jai_float_type) return static_cast<uint8_t>(script_value::TYPEID_FLOAT);
		if (t->base_type == script_value_type::jai_bool_type) return static_cast<uint8_t>(script_value::TYPEID_BOOL);
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
	// a container binding: a frame slot (a value or a reference the entry resolves), an environment array, or a member of a
	// frame-slot instance (a plain field the entry resolves once; a user accessor keeps the site on the general form)
	auto container_operand = [&](const fused_operand& o) {
		if (o.const_index != k_invalid_u32 || o.is_subscript() || o.load_flags != 0) return false;
		if (o.member_node != k_invalid_u32) return o.slot != k_invalid_u32 && o.slot < k_typed_slot_mask;
		return o.slot != k_invalid_u32 || o.symbol != k_invalid_u32;
	};
	const uint8_t kint = static_cast<uint8_t>(script_value::TYPEID_INT), kflt = static_cast<uint8_t>(script_value::TYPEID_FLOAT), kbool = static_cast<uint8_t>(script_value::TYPEID_BOOL);
	// straight-line regions (plan B, docs/typed_regions_design.md "Straight-line regions"): after the loops, the spans of translatable
	// instructions outside every closed loop, cut at the caller's statement boundaries, become regions of their own; a span the
	// translation refuses splits around the refusing statement
	std::vector<std::pair<uint32_t, uint32_t>> straight;   // [head, end)
	bool straight_ready = false;
	auto caller_stmt = [&](size_t at) -> const ast_node* {
		for (const chunk::inline_site& s : c.inline_sites) { if (at > s.guard_ip && at < s.join_ip) return s.call_stmt; }
		return at < c.stmt_nodes.size() ? c.stmt_nodes[at] : nullptr;
	};
	std::vector<uint8_t> excluded;   // an instruction a straight region never covers: a closed loop's general form
	std::vector<uint32_t> fallback_join;   // a graft's fallback ip: its join (the copy never runs the fallback)
	auto boundary = [&](size_t at) { return at == 0 || caller_stmt(at) != caller_stmt(at - 1); };
	// a region is scalar work only: an entry resolves no container site and the copy makes no call, so a region entered once per call pays
	// only its cells' guards and its declarations' flush (a site's resolution or a call's landing belongs to loops, which amortize them)
	auto straight_op = [](opcode o) {
		if (o >= opcode::op_t_add_ii && o <= opcode::op_t_mod_ff) return true;
		if (o >= opcode::op_t_jlt_ii && o <= opcode::op_t_jne_ff) return true;
		switch (o) {
		case opcode::op_binary_fused: case opcode::op_binary_fused_decl: case opcode::op_binary_fused_temp: case opcode::op_binary_fused_store:
		case opcode::op_fused_cmp_jump: case opcode::op_t_decl_mov: case opcode::op_t_decl_const: case opcode::op_t_store_mov: case opcode::op_t_store_const:
		case opcode::op_unary_temp: case opcode::op_math_temp: case opcode::op_call_temp: case opcode::op_math_guard: case opcode::op_inline_guard:
		case opcode::op_jump: case opcode::op_jump_if_true: case opcode::op_jump_if_false: case opcode::op_load: case opcode::op_const: case opcode::op_store:
		case opcode::op_decl_var: case opcode::op_operand_temp: case opcode::op_compound_fused: case opcode::op_incdec: case opcode::op_pop:
		case opcode::op_return: case opcode::op_return_ident: case opcode::op_return_binary: case opcode::op_null: case opcode::op_true: case opcode::op_false:
			return true;
		default: return false;
		}
	};
	auto work_op = [](opcode o) {   // an operation the region runs as a cell instruction in place of a dispatch
		if (o >= opcode::op_t_add_ii && o <= opcode::op_t_mod_ff) return true;
		if (o >= opcode::op_t_jlt_ii && o <= opcode::op_t_jne_ff) return true;
		switch (o) {
		case opcode::op_binary_fused: case opcode::op_binary_fused_decl: case opcode::op_binary_fused_temp: case opcode::op_binary_fused_store:
		case opcode::op_fused_cmp_jump: case opcode::op_t_decl_mov: case opcode::op_t_decl_const: case opcode::op_t_store_mov: case opcode::op_t_store_const:
		case opcode::op_unary_temp: case opcode::op_math_temp: case opcode::op_compound_fused:
			return true;
		default: return false;
		}
	};
	auto entry_op = [](const vm_instruction& ins) {   // an instruction the trampoline can run as the general form (no look at its successor)
		if (ins.op >= opcode::op_t_add_ii && ins.op <= opcode::op_t_mod_ff) return true;
		switch (ins.op) {
		case opcode::op_t_decl_mov: case opcode::op_t_decl_const: case opcode::op_t_store_mov: case opcode::op_t_store_const:
		case opcode::op_binary_fused_decl: case opcode::op_binary_fused_temp: case opcode::op_binary_fused_store: case opcode::op_unary_temp:
		case opcode::op_operand_temp: case opcode::op_math_guard:
			return true;
		case opcode::op_math_temp: return ins.c <= 0xFF;   // a stdlib alias steps past its CALL_TEMP
		default: return false;
		}
	};
	// a frame program (docs/typed_regions_design.md "Raw typed frames"): a body whose parameters are all by-value scalars, with no loop and at least
	// one call the checker resolved to a raw-signature function, is one region from its first instruction to its HALT; a refusal falls back to the
	// body's spans
	size_t fn_candidate = SIZE_MAX;
	auto frame_program_candidate = [&]() -> bool {
		if (!frame_eligible_ || !c.is_function_body || original_size < 2 || code[original_size - 1].op != opcode::op_halt || !c.cell_loops.empty()) return false;
		bool raw_site = false;
		for (size_t ip = 0; ip + 1 < original_size; ++ip) {
			const vm_instruction& ins = code[ip];
			if (ins.op == opcode::op_loop_back || ins.op == opcode::op_cfor_back || ins.op == opcode::op_cells_back || ins.op == opcode::op_cells_cfor_back || ins.op == opcode::op_iter_next) return false;
			if (ins.op == opcode::op_jump && ins.a <= ip) return false;
			if (ins.op == opcode::op_probe_callee && ins.a < c.call_sites.size() && c.call_sites[ins.a].call_node < c.nodes.size()) {
				const auto* ce = static_cast<const call_expr*>(c.nodes[c.call_sites[ins.a].call_node].get());
				const auto decl = ce->proven_callee_decl.lock();
				if (decl && raw_signature(decl.get()) && ce->arguments.size() == decl->parameters.size() && raw_call_reaches_program(decl.get(), callable_body_)) { raw_site = true; }
			}
		}
		if (!raw_site) return false;
		const vm_instruction& h = code[0];   // the trampoline runs it when the entry is refused: an instruction that looks at nothing after it
		return entry_op(h) || (h.op >= opcode::op_t_jlt_ii && h.op <= opcode::op_t_jne_ff) || h.op == opcode::op_fused_cmp_jump || h.op == opcode::op_probe_callee || h.op == opcode::op_const || h.op == opcode::op_load;
	};
	// the spans [s, e) inside [lo, hi) worth a region: a statement boundary at each end, an entry instruction the trampoline runs, enough work
	auto add_spans = [&](size_t lo, size_t hi) {
		size_t at = lo;
		while (at < hi) {
			while (at < hi && (!boundary(at) || excluded[at] || !straight_op(code[at].op) || !entry_op(code[at]))) { ++at; }
			if (at >= hi) break;
			size_t end = at, last_boundary = at, work = 0, work_at_boundary = 0;
			for (; end < hi && !excluded[end]; ++end) {
				if (fallback_join[end] > end && fallback_join[end] <= hi) { end = fallback_join[end] - 1; continue; }
				if (!straight_op(code[end].op)) break;
				if (end > at && boundary(end)) { last_boundary = end; work_at_boundary = work; }
				if (work_op(code[end].op)) { ++work; }
			}
			if (end >= hi || boundary(end) || code[end].op == opcode::op_halt) { last_boundary = end; work_at_boundary = work; }
			if (work_at_boundary >= k_region_min_work && last_boundary > at) { straight.emplace_back(static_cast<uint32_t>(at), static_cast<uint32_t>(last_boundary)); }
			at = std::max(end, at + 1);
		}
	};
	for (size_t cand = 0;; ++cand) {
		size_t back = cand, line_head = SIZE_MAX;
		if (cand >= original_size) {
			if (!typed_lowering_ || !ir_passes_) break;
			if (!straight_ready) {
				straight_ready = true;
				excluded.assign(original_size + 1, 0);
				fallback_join.assign(original_size + 1, 0);
				for (const chunk::inline_site& s : c.inline_sites) { if (s.fallback_ip < original_size && s.join_ip > s.fallback_ip) { fallback_join[s.fallback_ip] = s.join_ip; } }
				for (const auto& cl : c.cell_loops) {
					const size_t hi = cl.counted != k_invalid_u32 && cl.counted < c.counted_for_protos.size() ? c.counted_for_protos[cl.counted].exit_ip : cl.back + 1;
					for (size_t k = cl.head; k < hi && k < excluded.size(); ++k) { excluded[k] = 1; }
				}
				if (frame_program_candidate()) { fn_candidate = straight.size(); straight.emplace_back(0u, static_cast<uint32_t>(original_size - 1)); }   // the whole body: its spans only if the frame program is refused
				else { add_spans(0, original_size); }
			}
			if (cand - original_size >= straight.size()) break;
			line_head = straight[cand - original_size].first; back = straight[cand - original_size].second;
		}
		const bool line = line_head != SIZE_MAX;
		const bool fn_mode = line && cand >= original_size && cand - original_size == fn_candidate;   // the frame program: returns are rets, calls are doors (raw when the callee is)
		const vm_instruction be = line ? vm_instruction{opcode::op_halt, 0, 0, 0} : code[back];
		// a range-for's back edge: the jump to its ITER_NEXT, the exit jump after that, the pop after the back edge (a continue jumps
		// there too, but not from the instruction before the pop)
		uint32_t range_proto = k_invalid_u32;
		if (!line && be.op == opcode::op_jump && be.a < back && code[be.a].op == opcode::op_iter_next && be.a + 1 < back && code[be.a + 1].op == opcode::op_jump_if_false && code[be.a + 1].a == back + 1
		    && back + 1 < original_size && code[back + 1].op == opcode::op_iter_pop && code[be.a].a < c.iter_protos.size()) { range_proto = code[be.a].a; }
		if (!line && be.op != opcode::op_loop_back && be.op != opcode::op_cfor_back && range_proto == k_invalid_u32) continue;
		size_t head; vm_instruction hd{}; uint32_t counted = k_invalid_u32; size_t cmp_ip = 0;
		uint8_t refusal = chunk::cell_refusal_none; uint32_t refusal_ip = static_cast<uint32_t>(back);
		auto refuse = [&](uint8_t why, size_t at) { if (refusal == chunk::cell_refusal_none) { refusal = why; refusal_ip = static_cast<uint32_t>(at); } };
		if (line) {
			head = line_head; cmp_ip = head;
		} else if (range_proto != k_invalid_u32) {
			// the head is ITER_NEXT, the exit the jump's target (read below as hd.a); the loop variable must be a frame slot
			head = be.a; cmp_ip = head; hd = code[head + 1];
			const iter_proto& ipr = c.iter_protos[range_proto];
			if (ipr.slot == SIZE_MAX || ipr.slot >= k_typed_slot_mask) { refuse(chunk::cell_refusal_head, head); }
		} else if (be.op == opcode::op_loop_back) {
			head = be.a;
			if (head >= back) continue;
			// a counted loop's generic fallback (its op_loop_back spans the counted back edge) is the same loop, not a second one
			bool generic_of_counted = false;
			for (size_t ip = head + 1; ip < back; ++ip) {
				if (code[ip].op != opcode::op_cfor_back && code[ip].op != opcode::op_cells_cfor_back) continue;
				const uint32_t pi = code[ip].op == opcode::op_cfor_back ? code[ip].a : (code[ip].a < c.cell_loops.size() ? c.cell_loops[code[ip].a].counted : k_invalid_u32);
				if (pi < c.counted_for_protos.size() && c.counted_for_protos[pi].exit_ip > back) { generic_of_counted = true; break; }   // the counted loop's own fallback spans this back edge; an inner counted loop ends before it
			}
			if (generic_of_counted) continue;
			// the head compare: a typed compare-jump or a general one whose false edge is the exit; the instructions before it
			// (the condition's temps and reads) are the head span, translated like the body and run every iteration
			cmp_ip = head;
			for (; cmp_ip < back; ++cmp_ip) {
				const vm_instruction& ci = code[cmp_ip];
				if (ci.op >= opcode::op_t_jlt_ii && ci.op <= opcode::op_t_jne_ff && ci.c == back + 1) break;
				if (ci.op == opcode::op_fused_cmp_jump && ci.a == back + 1 && ci.b < c.fused_binary_protos.size()) break;
				// a truth test on a local as the head (`while (n)`): the load and its exit jump
				if (ci.op == opcode::op_load && (ci.a == k_invalid_u32 || ci.a < k_typed_slot_mask) && ci.c == 0 && cmp_ip + 1 < back && code[cmp_ip + 1].op == opcode::op_jump_if_false && code[cmp_ip + 1].a == back + 1) break;
			}
			if (cmp_ip >= back) { refuse(chunk::cell_refusal_head, head); cmp_ip = head; }
			hd = code[cmp_ip];
			if (hd.op == opcode::op_load) { hd = code[cmp_ip + 1]; hd.op = opcode::op_jump_if_false; }   // the exit is the jump's target (read below as hd.a)
		} else {
			if (be.a >= c.counted_for_protos.size()) continue;
			const counted_for_proto& p = c.counted_for_protos[be.a];
			if (p.body_ip == k_invalid_u32 || p.body_ip == 0 || p.body_ip > back) continue;
			head = p.body_ip - 1;
			counted = be.a;
			// the bound and the step may be environment scalars (`i < VW`): the general form reads the global through a pointer at
			// every back edge and no cell instruction writes a global, so the environment cell read once at entry is the same value
			if (!direct_slot(p.var) || !(int_const(p.end) || direct_slot(p.end) || env_scalar(p.end)) || !(int_const(p.step) || direct_slot(p.step) || env_scalar(p.step))) { refuse(chunk::cell_refusal_head, head); }
		}
		// the cells: semantic locals and compiler values by slot, environment values by symbol; kind 0 = adopted from the
		// slot at entry, a typed use pins it; a landing's contract belongs to the operation that lands (stamp, landing)
		std::vector<chunk::cell_loop::cell> cells;
		std::vector<chunk::cell_loop::env_operand_ref> env_ops;
		std::vector<chunk::cell_loop::cell_call> calls;
		std::vector<uint32_t> loop_inline_sites;   // the inlined leaves inside the region: their guards are sites of the entry and the landings
		std::vector<chunk::cell_loop::math_alias_site> loop_alias_sites;   // the stdlib aliases the block evaluates: sites of the entry and the landings
		auto inline_fallback_of = [&](size_t ip) -> size_t { for (const chunk::inline_site& s : c.inline_sites) { if (s.fallback_ip == ip && s.join_ip > ip) return s.join_ip; } return SIZE_MAX; };
		// a declaration landing on a compiler slot (a graft's parameters, locals and result; a hoisted call's temps) is a compiler value, not one of the
		// body's locals: a later temp may take the slot with another kind, and it is flushed while live like any temp
		auto decl_landing = [&](uint32_t slot) -> uint8_t { return slot >= c.first_temp_slot ? k_typed_dst_temp : k_typed_dst_decl; };   // local_count is the frame's size by the end of the compile
		uint32_t arg_temp_next = static_cast<uint32_t>(c.local_count);   // a computed argument's value: a cell with a synthetic slot above the frame's (never flushed, recomputed on a miss)
		auto find_cell = [&](uint32_t slot) -> int { for (size_t k = cells.size(); k-- > 0;) { if (cells[k].env_operand == k_invalid_u32 && cells[k].slot == slot) return static_cast<int>(k); } return -1; };   // the slot's latest definition
		std::vector<std::pair<uint32_t, uint32_t>> defs;   // (ip, cell): each compiler value's definition in program order, the emission's binding map
		auto stmt_of = [&](size_t at) -> const ast_node* { return at < c.stmt_nodes.size() ? c.stmt_nodes[at] : nullptr; };
		auto caller_stmt_of = [&](size_t at) -> const ast_node* {   // a graft's statements are the caller's statement's
			for (const chunk::inline_site& s : c.inline_sites) { if (at > s.guard_ip && at < s.join_ip) return s.call_stmt; }
			return stmt_of(at);
		};
		// the statement a compiler value's definition belongs to: inside a graft its own statement (the caller's temps reuse the graft's slots
		// after the join), the graft itself for its parameters and its result (every return defines the result the join reads)
		auto def_site_of = [&](size_t at, uint32_t slot) -> const void* {
			const chunk::inline_site* in = nullptr;
			for (const chunk::inline_site& s : c.inline_sites) { if (at > s.guard_ip && at < s.join_ip && (!in || s.guard_ip > in->guard_ip)) { in = &s; } }
			const ast_node* st = stmt_of(at);
			if (!in) return st;
			return slot == in->result_slot || st == in->call_stmt ? static_cast<const void*>(in) : static_cast<const void*>(st);
		};
		auto last_def_site = [&](int k) -> const void* { for (size_t d = defs.size(); d-- > 0;) { if (defs[d].second == static_cast<uint32_t>(k)) return def_site_of(defs[d].first, cells[k].slot); } return nullptr; };
		std::vector<uint32_t> alias_slots;   // the slots of the body's element aliases (rebinds), in order
		auto is_alias = [&](uint32_t slot) -> bool { for (const uint32_t s : alias_slots) { if (s == slot) return true; } return false; };
		auto use_slot = [&](uint32_t operand, uint8_t kind, bool is_write, uint8_t landing, type_info* stamp, size_t at) {
			if (operand & k_typed_const_bit) return;
			const uint32_t slot = operand & k_typed_slot_mask;
			int k = find_cell(slot);
			if (k < 0) {
				chunk::cell_loop::cell nc{ slot, kind, 0, !is_write, false, nullptr };
				cells.push_back(nc); k = static_cast<int>(cells.size() - 1);
			} else if (is_write && landing == k_typed_dst_temp && cells[k].temp && (cells[k].kind != kind || (kind == 0 && last_def_site(k) != def_site_of(at, slot)))) {
				// a compiler value is a definition, not a slot: a temp redefined with another kind (the compiler reuses temp
				// slots across statements) is a new cell of the same slot; reads bind to the latest definition. An untyped
				// definition in another statement is one too: a typed read there fixes that statement's cell only (the arms of
				// one statement's ternary share theirs, and an untyped landing in a fixed cell misses: cell_land)
				chunk::cell_loop::cell nc{ slot, kind, 0, false, false, nullptr };
				cells.push_back(nc); k = static_cast<int>(cells.size() - 1);
			} else if (cells[k].kind == 0) { cells[k].kind = kind; }   // an adopted kind takes the first typed use's
			else if (kind != 0 && cells[k].kind != kind) { refuse(chunk::cell_refusal_representation, at); return; }   // two kinds on one local: the general form's conversion
			if (is_write) {
				if (!cells[k].defined && !cells[k].live_in && landing == k_typed_dst_decl) { cells[k].first_decl = true; cells[k].decl_stamp = stamp; }   // declared by the body
				cells[k].defined = true; cells[k].landing = landing; cells[k].stamp = stamp;
				if (landing == k_typed_dst_temp) { cells[k].temp = true; cells[k].open_def = cells[k].open_def || kind == 0; defs.emplace_back(static_cast<uint32_t>(at), static_cast<uint32_t>(k)); }
			}
		};
		auto env_cell = [&](const fused_operand& o, uint8_t kind, uint32_t cache_slot, size_t at) -> uint32_t {
			for (size_t e = 0; e < env_ops.size(); ++e) {
				if (c.symbols[env_ops[e].operand.symbol] != c.symbols[o.symbol]) continue;   // one cell per name: the symbol table is not deduplicated
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
		// an environment value a store lands on (a global, or the member a bare name resolves to; `::name` stays general): the
		// same cell its reads share, defined by the landing; kinds at run time against the stamp read at entry
		auto env_written = [&](uint32_t symbol_index, uint8_t landing, size_t at) -> uint32_t {
			fused_operand o; o.symbol = symbol_index; o.ident_node = at < c.ident_nodes.size() ? c.ident_nodes[at] : k_invalid_u32;
			const uint32_t e = env_cell(o, 0, static_cast<uint32_t>(at * 3 + 2), at);
			for (auto& cl : cells) { if (cl.env_operand == e) { cl.defined = true; cl.landing = landing; } }
			return e;
		};
		// a value operand: an int or float literal, a frame slot (a cell of the given or adopted kind) or an environment value
		auto value = [&](ir_op& op, const fused_operand& o, uint8_t kind, uint32_t cache_slot, size_t at, uint32_t& out) -> bool {
			if (o.const_index != k_invalid_u32) {
				if (o.const_index >= c.constants.size() || o.const_index >= k_typed_slot_mask) return false;
				const size_t ci = c.constants[o.const_index].raw_storage_index();
				if (kind == kint ? ci != script_value::TYPEID_INT : kind == kflt ? (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT) : (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_BOOL && ci != script_value::TYPEID_STRING)) return false;
				out = k_typed_const_bit | o.const_index;
				return true;
			}
			if (direct_slot(o)) { use_slot(o.slot, kind, false, 0, nullptr, at); out = o.slot; if (op.nreads < 8) { op.reads[op.nreads++] = o.slot; } return true; }
			if (env_scalar(o)) { out = k_ir_env_bit | env_cell(o, kind, cache_slot, at); return true; }
			return false;
		};
		auto slot_read = [&](ir_op& op, uint32_t operand) { if (!(operand & k_typed_const_bit) && op.nreads < 8) { op.reads[op.nreads++] = operand & k_typed_slot_mask; } };
		// a site whose container is an alias reads the alias (the must-define dataflow holds it to a defined alias, the liveness keeps the
		// alias until its last site); a push receiver is resolved at entry, never rebound
		auto alias_site = [&](ir_op& op, size_t at) -> bool {
			if (op.container == k_invalid_u32 || !is_alias(op.container)) return true;
			if (op.kind == ir::push) { refuse(chunk::cell_refusal_translation, at); return false; }
			slot_read(op, op.container);
			return true;
		};
		// a truth test's operand (`if (x)`, `while (RUNNING)`): a frame slot's cell of adopted kind, or an environment value; the test is
		// the language's truthiness of an int, a float or a bool (a NaN is true, a negative zero is false, as the general test has them)
		auto truth_operand = [&](ir_op& op, const vm_instruction& ld, size_t at, uint32_t& out) -> bool {
			if (ld.a != k_invalid_u32) { if (ld.a >= k_typed_slot_mask) return false; use_slot(ld.a, 0, false, 0, nullptr, at); slot_read(op, ld.a); out = ld.a; return true; }
			if (ld.b >= c.symbols.size()) return false;
			fused_operand o; o.symbol = ld.b; o.ident_node = at < c.ident_nodes.size() ? c.ident_nodes[at] : k_invalid_u32;
			out = k_ir_env_bit | env_cell(o, 0, static_cast<uint32_t>(at * 3), at);
			return true;
		};
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
		region_ir region;   // the loop's canonical operations (region_ir.hpp): built here, verified, listed and emitted below
		region.straight = line;
		std::vector<ir_op>& ops = region.ops;
		ir_op head_op;
		const size_t first = counted != k_invalid_u32 ? head + 1 : head;
		region.head = static_cast<uint32_t>(head); region.first = static_cast<uint32_t>(first); region.back = static_cast<uint32_t>(back);
		if (refusal == chunk::cell_refusal_none && counted != k_invalid_u32) {
			const counted_for_proto& p = c.counted_for_protos[counted];
			use_slot(p.var.slot, kint, true, k_typed_dst_compound, nullptr, head);   // stepped in place by the back edge
			if (p.end.const_index == k_invalid_u32) { if (env_scalar(p.end)) { env_cell(p.end, kint, static_cast<uint32_t>(head * 3 + 1), head); } else { use_slot(p.end.slot, kint, false, 0, nullptr, head); } }
			if (p.step.const_index == k_invalid_u32) { if (env_scalar(p.step)) { env_cell(p.step, kint, static_cast<uint32_t>(head * 3 + 2), head); } else { use_slot(p.step.slot, kint, false, 0, nullptr, head); } }
			head_op.kind = ir::placeholder;
		}
		// a range-for head: the index a cell of its own (the iteration state's index at entry, stepped by the back edge, synced on a miss
		// and at the exit), the loop variable a body-declared cell bound by the head's landing (by value: the element's own kind; by
		// reference: a rebind of the element)
		if (fn_mode) { for (size_t i = 0; i < frame_param_kinds_.size(); ++i) { use_slot(static_cast<uint32_t>(i), frame_param_kinds_[i], false, 0, nullptr, head); } }   // the parameters: cells of their declared kinds, live in
		uint32_t range_index_slot = k_invalid_u32, range_var_slot = k_invalid_u32; bool range_byref = false;
		if (refusal == chunk::cell_refusal_none && range_proto != k_invalid_u32) {
			const iter_proto& ipr = c.iter_protos[range_proto];
			range_index_slot = arg_temp_next++;
			use_slot(range_index_slot, kint, true, k_typed_dst_compound, nullptr, head);
			{ const int k = find_cell(range_index_slot); if (k >= 0) { cells[k].iter_index = true; cells[k].live_in = true; } }
			range_var_slot = static_cast<uint32_t>(ipr.slot); range_byref = ipr.is_reference;
		}
		auto int_zero = [&]() -> uint32_t {   // the literal 0 a truth test compares against, minted once per chunk
			for (size_t k = 0; k < c.constants.size() && k < k_typed_slot_mask; ++k) { if (c.constants[k].raw_storage_index() == script_value::TYPEID_INT && c.constants[k].unchecked_as_int() == 0) return static_cast<uint32_t>(k); }
			c.constants.push_back(script_value(static_cast<script_int>(0), static_cast<engine*>(nullptr)));
			return static_cast<uint32_t>(c.constants.size() - 1);
		};
		auto int_one = [&]() -> uint32_t {   // the literal 1 of ++x and x--, minted once per chunk
			for (size_t k = 0; k < c.constants.size() && k < k_typed_slot_mask; ++k) { if (c.constants[k].raw_storage_index() == script_value::TYPEID_INT && c.constants[k].unchecked_as_int() == 1) return static_cast<uint32_t>(k); }
			c.constants.push_back(script_value(static_cast<script_int>(1), static_cast<engine*>(nullptr)));
			return static_cast<uint32_t>(c.constants.size() - 1);
		};
		// an inner counted loop (part 2 of nested loops): its CFOR_PREP is the head compare, its CFOR_BACK the step, the compare back to the
		// body takes the place of the first generic update instruction, and the generic cond and update code, the loop back and the pop
		// have no copy; a miss inside the loop pushes the general form's counted state first (cells_miss over inner_counted)
		uint32_t inner_skip_until = 0, inner_cmp_at = k_invalid_u32, inner_cmp_proto = k_invalid_u32;
		std::vector<uint32_t> inner_counted;
		// a bail (row 139): a statement the translation does not take (a translation-class refusal), standing inside a conditional span of
		// the body (reached only through a forward branch before it whose target lies after it), is a planned miss at its first instruction
		// instead of a refusal of the loop: the general form runs the statement whole from there with the stack it expects, so the
		// translation state is snapshotted at every statement start and restored, and the copy resumes at the end of the innermost arm
		// holding the bail (nothing between a bail and the arm's end is reachable inside the block, so none of it leaves a cell)
		// a bail inside a caller's statement (a graft's statement, or the rest of the statement after a graft's join): the compiler values the
		// statement defined before it (its spills, the grafts' parameters and results) are the general continuation's, so the bail flushes them
		std::vector<std::pair<uint32_t, std::vector<uint32_t>>> bail_held;
		auto arm_end_of = [&](size_t at) -> uint32_t {   // the nearest target after `at` of a forward branch before it: the end of the innermost arm holding `at`, invalid on the main path
			if (!line && counted == k_invalid_u32 && at <= cmp_ip) return k_invalid_u32;   // the head span and the head compare are the loop's own
			uint32_t end = k_invalid_u32;
			for (size_t j = first; j < at; ++j) {
				const vm_instruction& n = code[j];
				const uint32_t tgt = (n.op == opcode::op_jump || n.op == opcode::op_jump_if_true || n.op == opcode::op_jump_if_false || n.op == opcode::op_fused_cmp_jump) ? n.a
				                   : (n.op >= opcode::op_t_jlt_ii && n.op <= opcode::op_t_jne_ff) ? n.c : k_invalid_u32;
				if (tgt != k_invalid_u32 && tgt > at && tgt <= back && tgt < end) end = tgt;
			}
			return end;
		};
		std::vector<chunk::cell_loop::cell> snap_cells; size_t snap_env = 0, snap_defs = 0, snap_calls = 0, snap_ip = first, last_ip = first; uint32_t snap_arg = 0; size_t snap_alias = 0; const ast_node* snap_stmt = nullptr;
		size_t resume_from = first;
		for (;;) {
		for (size_t ip = resume_from; ip < back && refusal == chunk::cell_refusal_none; ++ip) {
			last_ip = ip;
			if (ip == resume_from || stmt_of(ip) != stmt_of(ip - 1)) { snap_cells = cells; snap_env = env_ops.size(); snap_defs = defs.size(); snap_calls = calls.size(); snap_arg = arg_temp_next; snap_alias = alias_slots.size(); snap_ip = ip; snap_stmt = stmt_of(ip); }
			const vm_instruction st = code[ip];
			ir_op op;
			if (range_proto != k_invalid_u32 && ip == head) {
				// the range head: the index against the container's live size, the exit on exhaustion
				op.kind = ir::range; op.head = true; op.a = range_index_slot; op.target = code[head + 1].a; slot_read(op, range_index_slot);
				ops.push_back(op);
				continue;
			}
			if (range_proto != k_invalid_u32 && ip == head + 1) {
				// the head's landing (the exit jump's place): the element into the loop variable, by value as a load of the element's own
				// kind, by reference as a rebind; a miss here re-runs the general ITER_NEXT
				if (range_byref) {
					if (find_cell(range_var_slot) >= 0 || is_alias(range_var_slot)) { refuse(chunk::cell_refusal_translation, ip); break; }
					use_slot(range_var_slot, static_cast<uint8_t>(script_value::TYPEID_REFERENCE), true, k_typed_dst_temp, nullptr, ip);
					alias_slots.push_back(range_var_slot);
					op.kind = ir::rebind;
				} else {
					use_slot(range_var_slot, 0, true, k_typed_dst_decl, nullptr, ip);
					op.kind = ir::load;
				}
				op.a = range_var_slot; op.writes = range_var_slot; op.b = range_index_slot; op.container = chunk::k_iter_container; op.origin = static_cast<uint32_t>(head);
				slot_read(op, range_index_slot);
				ops.push_back(op);
				continue;
			}
			if (range_proto != k_invalid_u32 && st.op == opcode::op_jump && st.a == head) {
				op.kind = ir::jump; op.target = static_cast<uint32_t>(back);   // a continue: the jump to ITER_NEXT lands on the block's back edge (the step and the poll)
				ops.push_back(op);
				continue;
			}
			if (ip < inner_skip_until) { op.kind = ir::skip; ops.push_back(op); continue; }   // an inner counted loop's generic cond or update code, its loop back or its pop: dead in the block
			{
				// an exit statement (a return, or a break that leaves the loop): the block ends at its first instruction and the general
				// form runs the statement whole (its pushes, region exits, scope pops and the return or jump), so any expression may
				// appear in it; the instructions it spans have no copy, and a jump to its first instruction lands on the exit
				const ast_node* sn = ip < c.stmt_nodes.size() ? c.stmt_nodes[ip] : nullptr;
				const bool is_return = sn && sn->get_type() == node_type::return_stmt && !fn_mode;   // a frame program translates its returns (ret)
				const bool is_break = sn && sn->get_type() == node_type::break_stmt;
				if (is_return || is_break) {
					size_t end = ip;
					bool terminal = false;
					for (; end < back && end < c.stmt_nodes.size() && c.stmt_nodes[end] == sn; ++end) {
						const opcode o = code[end].op;
						if (is_return && (o == opcode::op_return || o == opcode::op_return_ident || o == opcode::op_return_binary)) { terminal = true; ++end; break; }
						if (is_break && o == opcode::op_jump) { terminal = code[end].a > back; ++end; break; }   // a break out of a switch stays inside
					}
					if (terminal) {
						// the compiler values the caller's statement defined ahead of the exit (a graft's result and spills before a return's own
						// instructions) are the general continuation's: the exit flushes them
						size_t stmt_start = ip;
						while (stmt_start > first && caller_stmt_of(stmt_start - 1) == caller_stmt_of(ip)) { --stmt_start; }
						std::vector<uint32_t> held;
						for (const auto& [dip, dk] : defs) {
							if (dip < stmt_start || dip >= ip || cells[dk].slot >= static_cast<uint32_t>(c.local_count)) continue;
							std::erase_if(held, [&](uint32_t h) { return cells[h].slot == cells[dk].slot; });
							held.push_back(dk);
						}
						if (!held.empty()) { bail_held.emplace_back(static_cast<uint32_t>(ip), std::move(held)); }
						op.kind = ir::exit; op.target = static_cast<uint32_t>(is_break && end == ip + 1 ? code[ip].a : ip);   // a bare break lands on its target
						ops.push_back(op);
						for (size_t k = ip + 1; k < end; ++k) { ir_op sk; sk.kind = ir::skip; ops.push_back(sk); }
						ip = end - 1;
						continue;
					}
				}
			}
			if (fn_mode && (st.op == opcode::op_return_ident || st.op == opcode::op_return || st.op == opcode::op_return_binary ||
			                (ip + 1 < back && code[ip + 1].op == opcode::op_return && (st.op == opcode::op_binary_fused || st.op == opcode::op_const || st.op == opcode::op_load)))) {
				// a frame program's return: the value is a cell or a constant (a returned binary lands in an argument value first); a miss, or a
				// return the raw protocol does not take, re-runs the return's tail from its first instruction, so the tail's operands stay live
				ir_op ret; ret.kind = ir::ret; ret.origin = static_cast<uint32_t>(ip);
				if (st.op == opcode::op_return_ident) {
					if (st.a == k_invalid_u32 || st.a >= k_typed_slot_mask || (st.c & (load_flag_type_ctor | load_flag_global_only))) { refuse(chunk::cell_refusal_translation, ip); break; }
					use_slot(st.a, 0, false, 0, nullptr, ip); ret.a = st.a; slot_read(ret, st.a);
					ret.proto = (st.c & load_flag_return_proven) ? 1u : 0u;
					op = ret;
				} else if (st.op == opcode::op_return || st.op == opcode::op_return_binary) {
					refuse(chunk::cell_refusal_translation, ip); break;   // a value the stack carries
				} else {
					const vm_instruction& rn = code[ip + 1];
					if (rn.a != 1) { refuse(chunk::cell_refusal_translation, ip + 1); break; }
					ret.proto = rn.b;
					if (st.op == opcode::op_const) {
						const size_t ck = st.a < c.constants.size() && st.a < k_typed_slot_mask ? c.constants[st.a].raw_storage_index() : script_value::TYPEID_NULL;
						if (ck != script_value::TYPEID_INT && ck != script_value::TYPEID_FLOAT && ck != script_value::TYPEID_BOOL) { refuse(chunk::cell_refusal_representation, ip); break; }
						ret.a = k_typed_const_bit | st.a;
						op.kind = ir::skip;
					} else if (st.op == opcode::op_load) {
						if (st.a == k_invalid_u32 || st.a >= k_typed_slot_mask || st.c != 0) { refuse(chunk::cell_refusal_translation, ip); break; }
						use_slot(st.a, 0, false, 0, nullptr, ip); ret.a = st.a; slot_read(ret, st.a);
						op.kind = ir::skip;
					} else {
						// the returned binary into an argument value (a synthetic slot, never flushed): the tail's own operands are the ret's reads
						if (st.a >= c.fused_binary_protos.size()) { refuse(chunk::cell_refusal_representation, ip); break; }
						const fused_binary_proto& bp = c.fused_binary_protos[st.a];
						const bool is_cmp = cmp_tok(bp.op);
						if (!arith_tok(bp.op) && !is_cmp) { refuse(chunk::cell_refusal_translation, ip); break; }
						if (!value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.b) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.c)) { refuse(chunk::cell_refusal_representation, ip); break; }
						const uint32_t t = arg_temp_next++;
						uint8_t tk = 0;
						if ((bp.typed_class == 1 || bp.typed_class == 2) && !(op.b & k_ir_env_bit) && !(op.c & k_ir_env_bit)) {
							const opcode fast = arith_op(static_cast<token_type>(bp.op), bp.typed_class);
							if (fast != opcode::op_halt) { op.fast = fast; tk = bp.typed_class == 1 ? kint : kflt; use_slot(op.b, tk, false, 0, nullptr, ip); use_slot(op.c, tk, false, 0, nullptr, ip); }
						}
						use_slot(t, is_cmp ? kbool : tk, true, k_typed_dst_temp, nullptr, ip);
						op.kind = is_cmp ? ir::cmpval : ir::arith; op.tok = bp.op; op.a = t; op.writes = t;
						ret.a = t;
						for (uint8_t x = 0; x < op.nreads; ++x) { ret.reads[ret.nreads++] = op.reads[x]; }
					}
					ops.push_back(op);
					op = ret;
					++ip;   // the op_return is the pair's second instruction
				}
				if (!alias_site(op, ip)) break;
				ops.push_back(op);
				continue;
			}
			if (st.op == opcode::op_math_guard) {   // a stdlib alias's guard: its name holds at the entry and after every effect point, no copy instruction
				loop_alias_sites.push_back(chunk::cell_loop::math_alias_site{ st.a, static_cast<uint8_t>(st.c), static_cast<uint32_t>(ip * 3) });
				op.kind = ir::skip; ops.push_back(op); continue;
			}
			if (st.op == opcode::op_inline_guard) {   // an inlined leaf's guard: checked at the entry and after every effect point, no copy instruction
				if (std::find(loop_inline_sites.begin(), loop_inline_sites.end(), st.a) == loop_inline_sites.end()) { loop_inline_sites.push_back(st.a); }
				op.kind = ir::skip; ops.push_back(op); continue;
			}
			if (const size_t join = inline_fallback_of(ip); join != SIZE_MAX) {   // the guard's fallback: the original statement, unreachable in the block
				const size_t end = std::min(join, back);
				for (size_t k = ip; k < end; ++k) { ir_op sk; sk.kind = ir::skip; ops.push_back(sk); }
				ip = end - 1; continue;
			}
			if (ip == inner_cmp_at) {
				// the inner counted loop's compare back to its body, in the place of the first generic update instruction
				const counted_for_proto& p = c.counted_for_protos[inner_cmp_proto];
				op.kind = ir::cmp; op.tok = p.cmp; op.target = p.body_ip; op.sense = true; op.fast = cmp_op(static_cast<token_type>(p.cmp), 1);
				if (!value(op, p.var, kint, static_cast<uint32_t>(ip * 3), ip, op.a) || !value(op, p.end, kint, static_cast<uint32_t>(ip * 3 + 1), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
				inner_cmp_at = k_invalid_u32; inner_skip_until = p.exit_ip;
				ops.push_back(op); continue;
			}
			if (!line && counted == k_invalid_u32 && ip == cmp_ip) {
				// the head compare: its false edge is the loop's exit
				op.kind = ir::cmp; op.head = true;
				if (st.op == opcode::op_load) {
					// `while (n)` / `while (RUNNING)`: the truthiness of a local's or an environment value's int, float or bool; the jump has no copy
					if (!truth_operand(op, st, ip, op.a)) { refuse(chunk::cell_refusal_head, ip); break; }
					op.tok = static_cast<uint8_t>(token_type::bang); op.b = k_typed_const_bit; op.target = code[ip + 1].a;
					ops.push_back(op);
					op = ir_op{}; op.kind = ir::skip;   // the jump's place: no copy instruction
					++ip;
				} else if (st.op == opcode::op_fused_cmp_jump) {
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
				uint8_t landing = static_cast<uint8_t>(st.a >> k_typed_dst_shift);
				if (landing == k_typed_dst_decl) { landing = decl_landing(st.a & k_typed_slot_mask); }
				type_info* stamp = landing == k_typed_dst_decl && ip < c.typed_sites.size() ? c.typed_sites[ip].stamp : nullptr;
				auto row_operand = [&](uint32_t v, uint32_t side) -> uint32_t {
					if (!(v & k_typed_env_bit)) { use_slot(v, kind, false, 0, nullptr, ip); slot_read(op, v); return v; }
					return k_ir_env_bit | env_cell(c.typed_env_operands[v & k_typed_slot_mask], kind, static_cast<uint32_t>(ip * 3 + side), ip);
				};
				op.b = row_operand(st.b, 0); op.c = row_operand(st.c, 1);
				use_slot(st.a & k_typed_slot_mask, kind, true, landing, stamp, ip);
				op.kind = ir::arith; op.fast = st.op; op.a = st.a & k_typed_slot_mask; op.writes = op.a;
			} else if (st.op == opcode::op_binary_fused_decl || st.op == opcode::op_binary_fused_temp || st.op == opcode::op_binary_fused_store) {
				// arith the typed lowering left general (an environment value or an unclassed operand): kinds at run time, landing in
				// a stamped scalar decl, a compiler value or a no-result frame-slot store
				uint32_t bidx = st.a, dst = k_invalid_u32, env_dst = k_invalid_u32; uint8_t landing = k_typed_dst_temp, declared = 0; type_info* stamp = nullptr;
				if (st.op == opcode::op_binary_fused_temp) { dst = st.b; }
				else if (st.a < c.fused_binary_dst_protos.size()) {
					const fused_binary_dst_proto& dp = c.fused_binary_dst_protos[st.a];
					bidx = dp.binary_proto;
					if (st.op == opcode::op_binary_fused_decl) {
						auto* decl = dp.node_index < c.nodes.size() ? static_cast<const variable_decl*>(c.nodes[dp.node_index].get()) : nullptr;
						stamp = decl ? decl->type.get() : nullptr;
						if (!decl || (decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store || decl->ref_escaping || decl->slot_index == SIZE_MAX) { refuse(chunk::cell_refusal_representation, ip); break; }
						if (stamp && stamp_kind(stamp) == 0 && stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }   // a string, bool or class local
						dst = static_cast<uint32_t>(decl->slot_index); landing = decl_landing(dst); declared = stamp_kind(stamp);
						if (landing == k_typed_dst_temp) { stamp = nullptr; }   // a compiler value under a declared type (a graft's parameter, local or result) keeps the declaration's kind as its contract
					} else {
						if (dp.flags & store_flag_global_only) { refuse(chunk::cell_refusal_effect, ip); break; }   // `::name`: the global environment only, a binding the block does not hold
						if (dp.flags & store_flag_ref_alias) { refuse(chunk::cell_refusal_representation, ip); break; }
						if (!(dp.flags & store_flag_no_result)) { refuse(chunk::cell_refusal_translation, ip); break; }   // an assignment whose value is used
						if (dp.slot == k_invalid_u32) { env_dst = env_written(dp.symbol, k_typed_dst_store, ip); }   // an environment value written
						else { dst = dp.slot; landing = k_typed_dst_store; }
					}
				}
				if (bidx >= c.fused_binary_protos.size() || (env_dst == k_invalid_u32 && (dst == k_invalid_u32 || dst >= k_typed_slot_mask))) { refuse(chunk::cell_refusal_representation, ip); break; }
				const fused_binary_proto& bp = c.fused_binary_protos[bidx];
				const bool is_cmp = cmp_tok(bp.op);   // a comparison as a value lands a bool
				if (!arith_tok(bp.op) && !is_cmp) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.b) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.c)) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (env_dst != k_invalid_u32) { op.kind = is_cmp ? ir::cmpval : ir::arith; op.tok = bp.op; op.a = k_ir_env_bit | env_dst; }
				else { use_slot(dst, is_cmp && landing == k_typed_dst_temp ? kbool : landing == k_typed_dst_temp ? declared : stamp_kind(stamp), true, landing, stamp, ip); op.kind = is_cmp ? ir::cmpval : ir::arith; op.tok = bp.op; op.a = dst; op.writes = dst; }
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
					if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_BOOL) { refuse(chunk::cell_refusal_representation, ip); break; }
					op.b = k_typed_const_bit | st.b;
				} else if (st.op == opcode::op_t_decl_mov && (st.b & k_typed_env_bit)) {
					op.b = k_ir_env_bit | env_cell(c.typed_env_operands[st.b & k_typed_slot_mask], 0, static_cast<uint32_t>(ip * 3), ip);
				} else {
					if (st.b >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
					use_slot(st.b, 0, false, 0, nullptr, ip); op.b = st.b; slot_read(op, st.b);
				}
				if (st.a >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
				// a var or auto declaration from a constant holds the constant's kind: the cell takes it, so a later typed use of another
				// kind (the speculative rows on an unproven slot) is a disagreement the walk refuses rather than a contract the landing breaks
				uint8_t dst_kind = stamp_kind(stamp);
				if (dst_kind == 0 && from_const && decl) { const size_t ck = c.constants[st.b].raw_storage_index(); dst_kind = ck == script_value::TYPEID_INT ? kint : ck == script_value::TYPEID_FLOAT ? kflt : kbool; }
				const uint8_t mov_landing = decl ? decl_landing(st.a) : k_typed_dst_store;
				use_slot(st.a, dst_kind, true, mov_landing, mov_landing == k_typed_dst_temp ? nullptr : stamp, ip);
				op.kind = ir::mov; op.a = st.a; op.writes = st.a;
				if (decl && !from_const && !stamp) { op.tok = 1; }   // `auto x = y;` takes y's declared type: a var source makes x a var
			} else if (st.op == opcode::op_set_member_value && st.a < c.member_store_protos.size()) {
				// `base.member = operand;` onto a frame-slot instance (an alias included): a store site whose field the entry or the rebind
				// resolves; the value a cell or a constant of a scalar kind (an environment value keeps the general form)
				const member_store_proto& mp = c.member_store_protos[st.a];
				if (mp.base == k_invalid_u32 || mp.base >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (mp.value.const_index != k_invalid_u32) {
					const size_t ck = mp.value.const_index < c.constants.size() ? c.constants[mp.value.const_index].raw_storage_index() : script_value::TYPEID_NULL;
					if (ck != script_value::TYPEID_INT && ck != script_value::TYPEID_FLOAT && ck != script_value::TYPEID_BOOL) { refuse(chunk::cell_refusal_representation, ip); break; }
					op.b = k_typed_const_bit | mp.value.const_index;
				} else if (mp.value.slot != k_invalid_u32 && mp.value.slot < k_typed_slot_mask) {
					use_slot(mp.value.slot, 0, false, 0, nullptr, ip); op.b = mp.value.slot; slot_read(op, mp.value.slot);
				} else { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::store; op.container = mp.base; op.member = mp.node;
			} else if (st.op == opcode::op_size_temp && st.a < k_typed_slot_mask && st.b < c.call_sites.size()) {
				// the size of an array held in a frame slot: a site whose node the entry resolves, read live (a push inside the body grows it)
				const call_site& cs2 = c.call_sites[st.b];
				if (st.c != 2 || cs2.receiver_slot == k_invalid_u32 || cs2.receiver_slot >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(st.a, kint, true, k_typed_dst_temp, nullptr, ip);
				op.kind = ir::size; op.a = st.a; op.writes = st.a; op.container = cs2.receiver_slot;
			} else if (st.op == opcode::op_member_temp && st.c != k_invalid_u32 && st.c < k_typed_slot_mask && st.b < k_typed_slot_mask) {
				// a member read of a frame-slot instance into a compiler temp (`p.x`): a load site whose container is the base slot and whose
				// field the entry resolves once (the class, the plain field slot, no user accessor); the temp takes the field's own kind; a
				// base that is an element alias (`for (auto& prt : pool)`, `var& e = list[i]`) is the rebind's dependent, resolved per element
				use_slot(st.b, 0, true, k_typed_dst_temp, nullptr, ip);
				op.kind = ir::load; op.a = st.b; op.writes = st.b; op.container = st.c; op.member = st.a;
			} else if (st.op == opcode::op_t_load && ip < c.typed_sites.size()) {
				// load through a site the typed lowering flattened: the fold operands are int values; a scalar decl needs a stamp
				// (an unstamped decl lands the element's own tag, which a raw cell loses), a compiler value takes the element's kind
				const chunk::typed_site& site = c.typed_sites[ip];
				const bool classed = site.decl_expect == kint || site.decl_expect == kflt;
				if (site.stamp && stamp_kind(site.stamp) == 0 && site.stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }
				for (uint8_t k = 0; k <= site.fold_count && k < 4; ++k) { use_slot(site.operands[k], kint, false, 0, nullptr, ip); slot_read(op, site.operands[k]); }
				{ const uint8_t ld = site.temp ? k_typed_dst_temp : decl_landing(st.a); use_slot(st.a, classed && stamp_kind(site.stamp) == site.decl_expect ? site.decl_expect : 0, true, ld, ld == k_typed_dst_temp ? nullptr : site.stamp, ip); }
				op.kind = ir::load; op.a = st.a; op.writes = st.a; op.container = site.cont_slot;
			} else if (st.op == opcode::op_index_fused && st.a < c.fused_index_protos.size() && (st.b & index_flag_lvalue_shape) && ip + 1 < back && code[ip + 1].op == opcode::op_decl_ref_value
			           && code[ip + 1].a < c.nodes.size() && c.nodes[code[ip + 1].a] && c.nodes[code[ip + 1].a]->get_type() == node_type::variable_decl) {
				// `var& strip = STRIPS[sidx]`: an element alias declared in the body is a rebind, the source table's element becoming the container of
				// the alias's sites (the loads and stores through the slot after it), once per iteration; the alias slot is a compiler value of kind
				// reference (its payload the index) that only a flush where it is live materializes, as the element reference the declaration binds
				const fused_index_proto& fp = c.fused_index_protos[st.a];
				const auto* decl = static_cast<const variable_decl*>(c.nodes[code[ip + 1].a].get());
				if (fp.index_chain_ext.size() > 2 || !container_operand(fp.container) || !index_values(op, fp, ip)) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask || decl->ref_escaping) { refuse(chunk::cell_refusal_representation, ip); break; }
				const uint32_t aslot = static_cast<uint32_t>(decl->slot_index);
				bool taken = find_cell(aslot) >= 0 || is_alias(aslot);
				for (const ir_op& prior : ops) { if (prior.container == aslot) { taken = true; } }
				op.container = fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask ? fp.container.slot : k_invalid_u32;
				if (taken || (op.container != k_invalid_u32 && is_alias(op.container))) { refuse(chunk::cell_refusal_translation, ip); break; }   // one alias per slot, declared before its sites, its source never an alias
				use_slot(aslot, static_cast<uint8_t>(script_value::TYPEID_REFERENCE), true, k_typed_dst_temp, nullptr, ip);
				alias_slots.push_back(aslot);
				op.kind = ir::rebind; op.a = aslot; op.proto = st.a; op.writes = aslot;
				ops.push_back(op);
				op = ir_op{}; op.kind = ir::skip;   // the declaration is the pair's second instruction
				++ip;
			} else if (st.op == opcode::op_index_fused && st.a < c.fused_index_protos.size() && (st.b & index_flag_lvalue_shape) && ip + 1 < back && code[ip + 1].op == opcode::op_call_temp
			           && code[ip + 1].a >= static_cast<uint32_t>(c.first_temp_slot) && code[ip + 1].a < k_typed_slot_mask) {
				// `a[i].m`: the element into an alias temp (row 173) is an element alias's rebind, the member temp over the temp its dependent
				const fused_index_proto& fp = c.fused_index_protos[st.a];
				if (fp.index_chain_ext.size() > 2 || !container_operand(fp.container) || !index_values(op, fp, ip)) { refuse(chunk::cell_refusal_representation, ip); break; }
				const uint32_t aslot = code[ip + 1].a;
				bool taken = find_cell(aslot) >= 0 || is_alias(aslot);
				for (const ir_op& prior : ops) { if (prior.container == aslot) { taken = true; } }
				op.container = fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask ? fp.container.slot : k_invalid_u32;
				if (taken || (op.container != k_invalid_u32 && is_alias(op.container))) { refuse(chunk::cell_refusal_translation, ip); break; }
				use_slot(aslot, static_cast<uint8_t>(script_value::TYPEID_REFERENCE), true, k_typed_dst_temp, nullptr, ip);
				alias_slots.push_back(aslot);
				op.kind = ir::rebind; op.a = aslot; op.proto = st.a; op.writes = aslot;
				ops.push_back(op);
				op = ir_op{}; op.kind = ir::skip;   // the landing is the pair's second instruction
				++ip;
			} else if (st.op == opcode::op_index_fused && st.a < c.fused_index_protos.size() && ip + 1 < back && code[ip + 1].op == opcode::op_compound_store
			           && (code[ip + 1].c & compound_kind_mask) == compound_plus && (code[ip + 1].c & compound_flag_no_result) && !(code[ip + 1].c & (compound_flag_global_only | compound_flag_old_result))
			           && code[ip + 1].b != k_invalid_u32 && code[ip + 1].b < k_typed_slot_mask) {
				// `s += a[i]` on a slot the checker could not class: the element read lands in a statement temp of the element's own kind
				// (a string is borrowed), and the compound is the append with run-time kinds; the pair's origin is the read (a miss at
				// the append re-runs the push and the compound on the general form, so the temp is never flushed)
				const fused_index_proto& fp = c.fused_index_protos[st.a];
				if (fp.index_chain_ext.size() > 2 || !container_operand(fp.container) || !index_values(op, fp, ip)) { refuse(chunk::cell_refusal_representation, ip); break; }
				const uint32_t t = arg_temp_next++;
				use_slot(t, 0, true, k_typed_dst_temp, nullptr, ip);
				{ const int tk = find_cell(t); if (tk >= 0) { cells[tk].string_borrow = true; } }   // the append replays from this read on a miss, so a borrowed string may land here
				op.kind = ir::load; op.a = t; op.proto = st.a; op.writes = t;
				op.container = fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask ? fp.container.slot : k_invalid_u32;
				if (!alias_site(op, ip)) break;
				ops.push_back(op);
				const uint32_t dslot = code[ip + 1].b;
				op = ir_op{};
				use_slot(dslot, 0, false, 0, nullptr, ip + 1); use_slot(dslot, 0, true, k_typed_dst_compound, nullptr, ip + 1);
				op.kind = ir::append; op.tok = static_cast<uint8_t>(token_type::plus); op.a = dslot; op.b = dslot; op.c = t; op.writes = dslot; slot_read(op, dslot);
				op.origin = static_cast<uint32_t>(ip);
				++ip;   // the compound is this iteration's second instruction
			} else if (st.op == opcode::op_index_fused_decl && st.a < c.fused_index_protos.size()) {
				// load the typed lowering left general (an environment value in the index, an unproven container): the same site
				// row, built at emission (environment values take synthetic slots), landing in a stamped scalar decl or a compiler
				// value by the declared kind
				const fused_index_proto& fp = c.fused_index_protos[st.a];
				if (fp.decl_slot == k_invalid_u32 || fp.decl_slot >= k_typed_slot_mask || fp.index_chain_ext.size() > 2 || !container_operand(fp.container)) { refuse(chunk::cell_refusal_representation, ip); break; }
				const uint8_t dk = stamp_kind(fp.decl_type);
				if (!fp.decl_temp && dk == 0 && fp.decl_type && fp.decl_type->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (!index_values(op, fp, ip)) { refuse(chunk::cell_refusal_representation, ip); break; }
				{ const uint8_t ld = fp.decl_temp ? k_typed_dst_temp : decl_landing(fp.decl_slot); use_slot(fp.decl_slot, fp.decl_temp ? 0 : dk, true, ld, ld == k_typed_dst_temp ? nullptr : fp.decl_type, ip); }
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
			} else if (st.op == opcode::op_unary_temp && st.a != k_invalid_u32 && st.a < c.fused_binary_protos.size() && st.b < k_typed_slot_mask) {
				// neg, bnot and not on a value into a compiler value, kinds at run time (`!` is the truthiness inverted, a bool)
				const token_type tk = static_cast<token_type>(st.c);
				if (tk != token_type::minus && tk != token_type::tilde && tk != token_type::bang) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (!value(op, c.fused_binary_protos[st.a].left, 0, static_cast<uint32_t>(ip * 3), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(st.b, tk == token_type::tilde ? kint : tk == token_type::bang ? kbool : 0, true, k_typed_dst_temp, nullptr, ip);
				op.kind = tk == token_type::minus ? ir::neg : tk == token_type::tilde ? ir::bnot : ir::lnot; op.a = st.b; op.writes = st.b;
			} else if (st.op == opcode::op_math_temp && st.a != k_invalid_u32 && st.a < c.fused_binary_protos.size() && st.b < k_typed_slot_mask) {
				// math: a pure one-argument intrinsic of a value into a compiler value (the kernel's rows run in the block): itrunc
				// and ifloor land an int, abs the argument's kind (the stdlib alias a float), the float family a float; a stdlib alias
				// is the kernel while its name holds (a site of the entry and the landings) and its real call's landing has no copy
				const bool alias = st.c > 0xFF;
				const detail::math_fn fn = static_cast<detail::math_fn>(alias ? st.c & 0x3F : st.c);
				const fused_operand& src = c.fused_binary_protos[st.a].left;
				if (alias && !(ip + 1 < back && code[ip + 1].op == opcode::op_call_temp && code[ip + 1].a == st.b && detail::math_alias_kernel(fn))) { refuse(chunk::cell_refusal_translation, ip); break; }
				uint8_t rk = kflt;
				switch (fn) {
				case detail::math_fn::itrunc: case detail::math_fn::ifloor: rk = kint; break;
				case detail::math_fn::abs_: rk = alias ? kflt : 0; break;
				case detail::math_fn::floor_: case detail::math_fn::ceil_: case detail::math_fn::sqrt_: case detail::math_fn::sin_: case detail::math_fn::cos_: case detail::math_fn::tan_:
				case detail::math_fn::radians_to_degrees: case detail::math_fn::degrees_to_radians: break;
				default: refuse(chunk::cell_refusal_translation, ip); break;
				}
				if (refusal != chunk::cell_refusal_none) break;
				if (!direct_slot(src)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(src.slot, 0, false, 0, nullptr, ip);
				use_slot(st.b, rk, true, k_typed_dst_temp, nullptr, ip);
				op.kind = ir::math; op.tok = static_cast<uint8_t>(static_cast<uint32_t>(fn) | (alias ? k_cell_math_float_arg : 0u)); op.a = st.b; op.b = src.slot; op.writes = st.b; slot_read(op, src.slot);
				if (alias) {
					if (!(st.c & 0x80)) { loop_alias_sites.push_back(chunk::cell_loop::math_alias_site{ (st.c >> 8) - 1, static_cast<uint8_t>(fn), static_cast<uint32_t>(ip * 3 + 1) }); }
					else {
						// a call between the guard and the op could rebind the name after the guard resolved it: its landing's miss would reach
						// this op with no mark in the temp
						size_t guard_ip = SIZE_MAX;
						for (const auto& ms : loop_alias_sites) { if (ms.site == (st.c >> 8) - 1) { guard_ip = ms.cache_slot / 3; } }
						bool crossed = guard_ip == SIZE_MAX;
						for (const auto& cc : calls) { if (cc.probe_ip > guard_ip && cc.probe_ip < ip) { crossed = true; } }
						if (crossed) { refuse(chunk::cell_refusal_effect, ip); break; }
					}
					ops.push_back(op);
					op = ir_op{}; op.kind = ir::skip;
					++ip;
				}
			} else if (st.op >= opcode::op_t_jlt_ii && st.op <= opcode::op_t_jne_ff) {
				// cmp + test inside the body: the false edge stays inside the loop (or reaches its back edge)
				if (!(st.c > ip)) { refuse(chunk::cell_refusal_effect, ip); break; }   // an edge past the back edge exits the block
				const uint8_t kind = st.op <= opcode::op_t_jne_ii ? kint : kflt;
				use_slot(st.a, kind, false, 0, nullptr, ip); use_slot(st.b, kind, false, 0, nullptr, ip);
				op.kind = ir::cmp; op.fast = st.op; op.a = st.a; op.b = st.b; op.target = st.c; slot_read(op, st.a); slot_read(op, st.b);
			} else if (st.op == opcode::op_load && (st.a == k_invalid_u32 || st.a < k_typed_slot_mask) && st.c == 0 && ip + 1 < back && (code[ip + 1].op == opcode::op_jump_if_true || code[ip + 1].op == opcode::op_jump_if_false)) {
				// a truth test on a local or an environment value inside the body (`if (x)`, `if (!x)` through its sense, `if (NIGHTMARE)`):
				// the truthiness of its int, float or bool with the jump's sense; a slot of another kind refuses at the entry
				const vm_instruction& j = code[ip + 1];
				if (!(j.a > ip + 1)) { refuse(chunk::cell_refusal_effect, ip + 1); break; }   // an edge past the back edge exits the block
				bool targeted = false;
				for (size_t ip2 = first; ip2 < back && !targeted; ++ip2) {
					const vm_instruction& n = code[ip2];
					const uint32_t t = (n.op == opcode::op_jump || n.op == opcode::op_jump_if_true || n.op == opcode::op_jump_if_false || n.op == opcode::op_fused_cmp_jump) ? n.a
					                 : (n.op >= opcode::op_t_jlt_ii && n.op <= opcode::op_t_jne_ff) ? n.c : k_invalid_u32;
					targeted = t == ip + 1;
				}
				if (targeted) { refuse(chunk::cell_refusal_malformed, ip + 1); break; }
				if (!truth_operand(op, st, ip, op.a)) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::cmp; op.tok = static_cast<uint8_t>(token_type::bang); op.b = k_typed_const_bit; op.target = j.a; op.sense = j.op == opcode::op_jump_if_true;
				ops.push_back(op);
				op = ir_op{}; op.kind = ir::skip;   // the jump's place: no copy instruction
				++ip;
			} else if (st.op == opcode::op_binary_fused && ip + 1 < back && (code[ip + 1].op == opcode::op_jump_if_true || code[ip + 1].op == opcode::op_jump_if_false) && st.a < c.fused_binary_protos.size()) {
				// a comparison used as a value and the jump that consumes it (the || and && chains, !e): one cmp with the jump's
				// sense; the pair's origin is the value instruction (a miss re-runs it whole and the general jump follows), the
				// jump's copy is a placeholder no branch may target
				const fused_binary_proto& bp = c.fused_binary_protos[st.a];
				const vm_instruction& j = code[ip + 1];
				if (!cmp_tok(bp.op)) { refuse(chunk::cell_refusal_translation, ip); break; }   // a truth test on an arithmetic value: a gap
				if (!(j.a > ip + 1)) { refuse(chunk::cell_refusal_effect, ip + 1); break; }   // an edge past the back edge exits the block
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
				op = ir_op{}; op.kind = ir::skip;   // the jump's place: no copy instruction
				++ip;
			} else if (st.op == opcode::op_fused_cmp_jump && st.b < c.fused_binary_protos.size()) {
				if (!(st.a > ip)) { refuse(chunk::cell_refusal_effect, ip); break; }   // an edge past the back edge exits the block
				const fused_binary_proto& bp = c.fused_binary_protos[st.b];
				if (!cmp_tok(bp.op)) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!value(op, bp.left, 0, static_cast<uint32_t>(ip * 3), ip, op.a) || !value(op, bp.right, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::cmp; op.tok = bp.op; op.target = st.a;
			} else if (st.op == opcode::op_cfor_prep) {
				// an inner counted loop's head: `var cmp end`, the exit its false edge (the general form's CFOR_PREP tests once the same way); the var,
				// the end and the step are cells, constants or environment scalars as the outer counted head requires
				if (st.a >= c.counted_for_protos.size()) { refuse(chunk::cell_refusal_translation, ip); break; }
				const counted_for_proto& p = c.counted_for_protos[st.a];
				if (!(p.body_ip > ip && p.exit_ip > p.body_ip && p.exit_ip <= back && p.generic_update_ip > p.body_ip && p.generic_update_ip < p.exit_ip)) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!direct_slot(p.var)) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.kind = ir::cmp; op.tok = p.cmp; op.target = p.exit_ip; op.fast = cmp_op(static_cast<token_type>(p.cmp), 1);
				uint32_t step_sink;
				if (!value(op, p.var, kint, static_cast<uint32_t>(ip * 3), ip, op.a) || !value(op, p.end, kint, static_cast<uint32_t>(ip * 3 + 1), ip, op.b) || !value(op, p.step, kint, static_cast<uint32_t>(ip * 3 + 2), ip, step_sink)) { refuse(chunk::cell_refusal_representation, ip); break; }
				inner_counted.push_back(st.a);
				inner_skip_until = p.body_ip;   // the generic cond code
			} else if ((st.op == opcode::op_cfor_back || st.op == opcode::op_cells_cfor_back) && ip != back) {
				// an inner counted loop's back edge: the step on the var (an overflow misses onto the general back edge, whose text stands), then
				// the compare back to the body in the place of the first generic update instruction
				const uint32_t pi = st.op == opcode::op_cfor_back ? st.a : (st.a < c.cell_loops.size() ? c.cell_loops[st.a].counted : k_invalid_u32);
				if (pi == k_invalid_u32 || pi >= c.counted_for_protos.size() || std::find(inner_counted.begin(), inner_counted.end(), pi) == inner_counted.end()) { refuse(chunk::cell_refusal_translation, ip); break; }
				const counted_for_proto& p = c.counted_for_protos[pi];
				if (p.generic_update_ip != ip + 1) { refuse(chunk::cell_refusal_translation, ip); break; }
				op.kind = ir::arith; op.tok = static_cast<uint8_t>(p.step_subtract ? token_type::minus : token_type::plus); op.fast = p.step_subtract ? opcode::op_t_sub_ii : opcode::op_t_add_ii;
				uint32_t var_operand = k_invalid_u32;
				if (!value(op, p.var, kint, static_cast<uint32_t>(ip * 3), ip, var_operand) || !value(op, p.step, kint, static_cast<uint32_t>(ip * 3 + 2), ip, op.c)) { refuse(chunk::cell_refusal_representation, ip); break; }
				op.a = var_operand; op.b = var_operand; op.writes = var_operand;
				op.poll = true;   // the budget ticks ahead of the step, as the general back edge polls ahead of its own
				use_slot(p.var.slot, kint, true, k_typed_dst_compound, nullptr, ip);
				inner_cmp_at = static_cast<uint32_t>(ip + 1); inner_cmp_proto = pi;
			} else if ((st.op == opcode::op_loop_back || st.op == opcode::op_cells_back) && ip != back) {
				// an inner while loop's back edge (LOOP_BACK, or the CELLS_BACK of its own block): a backward jump to its head inside the
				// outer block, ticking the execution budget as the general back edge does (a miss on exhaustion: the general form raises on
				// the original back edge); the inner head's compare is a body cmp already, its exit a forward edge inside the body
				const uint32_t inner_head = st.op == opcode::op_loop_back ? st.a : st.b;
				if (!(inner_head >= first && inner_head < ip)) { refuse(chunk::cell_refusal_effect, ip); break; }
				op.kind = ir::jump; op.target = inner_head; op.tok = 1;   // 1: a back edge, the budget ticks
			} else if (st.op == opcode::op_jump) {
				if (!(st.a > ip && (st.a <= back || line))) { refuse(chunk::cell_refusal_effect, ip); break; }   // a break leaves the loop; a region's forward edge out is an exit
				op.kind = ir::jump; op.target = st.a;
			} else if (st.op == opcode::op_operand_temp && (st.c == 1 || st.c == 2)) {
				// guard (the plain-cell form or the always form): a snapshot definition; its read is the first load after it whose site
				// names this slot; the site itself is keyed on the original container, resolved at entry and after every effect point
				op.kind = ir::guard; op.a = st.b;
				for (size_t ip2 = ip + 1; ip2 < back; ++ip2) {
					const vm_instruction& n = code[ip2];
					const uint32_t g = n.op == opcode::op_t_load && ip2 < c.typed_sites.size() ? c.typed_sites[ip2].guard_temp
					                 : n.op == opcode::op_index_fused_decl && n.a < c.fused_index_protos.size() ? c.fused_index_protos[n.a].container_guard : k_invalid_u32;
					if (g == st.b) { op.guarded = static_cast<uint32_t>(ip2 - head); break; }
				}
			} else if (st.op == opcode::op_operand_temp && st.c == 0 && st.a < c.fused_binary_protos.size()) {
				// a value snapshot of a scalar operand (an identifier user code could rebind, read ahead of a hoisted read that may run
				// it): the block runs no user code between its operations, so the snapshot is a move into the temp
				if (st.b >= k_typed_slot_mask || !value(op, c.fused_binary_protos[st.a].left, 0, static_cast<uint32_t>(ip * 3), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
				use_slot(st.b, 0, true, k_typed_dst_temp, nullptr, ip);
				op.kind = ir::mov; op.a = st.b; op.writes = st.b;
			} else if (st.op == opcode::op_compound_fused && st.a < c.compound_fused_protos.size()) {
				// x op= y the typed lowering left general (a slot the checker could not pin, a float literal): arith with the
				// compound landing, kinds at run time (a kind change on the slot misses onto the general conversion)
				const compound_fused_proto& cp = c.compound_fused_protos[st.a];
				if (cp.slot >= k_typed_slot_mask && cp.slot != k_invalid_u32) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (cp.kind_flags & compound_flag_global_only) { refuse(chunk::cell_refusal_effect, ip); break; }   // `::name`
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
				if (cp.slot == k_invalid_u32) {
					const uint32_t e = k_ir_env_bit | env_written(cp.symbol, k_typed_dst_compound, ip);   // an environment value stepped in place
					op.kind = ir::arith; op.tok = tok; op.a = e; op.b = e;
				} else {
					use_slot(cp.slot, 0, false, 0, nullptr, ip); use_slot(cp.slot, 0, true, k_typed_dst_compound, nullptr, ip);
					op.kind = tok == static_cast<uint8_t>(token_type::plus) ? ir::append : ir::arith; op.tok = tok; op.a = cp.slot; op.b = cp.slot; op.writes = cp.slot; slot_read(op, cp.slot);   // plus on a slot: strings append in place, numbers add
				}
			} else if (st.op == opcode::op_incdec && st.b == k_invalid_u32 && !(st.c & incdec_flag_global_only) && (st.c & incdec_flag_no_result)) {
				// ++g / g-- as a statement on an environment value: arith by one landing on the environment cell
				const uint32_t e = k_ir_env_bit | env_written(st.a, k_typed_dst_compound, ip);
				op.kind = ir::arith; op.tok = static_cast<uint8_t>((st.c & incdec_flag_increment) ? token_type::plus : token_type::minus); op.a = e; op.b = e; op.c = k_typed_const_bit | int_one();
			} else if ((st.op == opcode::op_load || st.op == opcode::op_const) && ip + 1 < back && (code[ip + 1].op == opcode::op_store || code[ip + 1].op == opcode::op_decl_var) &&
			           (code[ip + 1].op == opcode::op_decl_var ? (code[ip + 1].b != 0 && (code[ip + 1].c == 0 || st.op == opcode::op_load) && code[ip + 1].a < c.nodes.size()) : ((code[ip + 1].b == k_invalid_u32 || code[ip + 1].b < k_typed_slot_mask) && !(code[ip + 1].c & (store_flag_ref_alias | store_flag_global_only)) && (code[ip + 1].c & store_flag_no_result))) &&
			           (st.op == opcode::op_const ? (st.a < c.constants.size() && st.a < k_typed_slot_mask && (c.constants[st.a].raw_storage_index() == script_value::TYPEID_INT || c.constants[st.a].raw_storage_index() == script_value::TYPEID_FLOAT || c.constants[st.a].raw_storage_index() == script_value::TYPEID_BOOL)) : (st.a != k_invalid_u32 && st.a < k_typed_slot_mask && st.c == 0))) {
				// a general move the typed rows left alone (`painted = true;`, `bool b = other;`, `g = x;` onto an environment value): the
				// pair (a push, a store or a declaration) is one move by the declared-type rule
				const vm_instruction& sn = code[ip + 1];
				if (sn.op == opcode::op_decl_var) {
					auto* decl = static_cast<const variable_decl*>(c.nodes[sn.a].get());
					type_info* stamp = decl->type.get();
					if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store || decl->ref_escaping || decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, ip + 1); break; }
					if (stamp && stamp_kind(stamp) == 0 && stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, ip + 1); break; }   // a string or class local
					{ const uint8_t ld = decl_landing(static_cast<uint32_t>(decl->slot_index)); use_slot(static_cast<uint32_t>(decl->slot_index), stamp_kind(stamp), true, ld, ld == k_typed_dst_temp ? nullptr : stamp, ip + 1); }
					op.a = static_cast<uint32_t>(decl->slot_index); op.writes = op.a;
					if (!stamp && st.op == opcode::op_load) { op.tok = 1; }   // an auto declaration from a name takes its declared type
				} else if (sn.b == k_invalid_u32) { op.a = k_ir_env_bit | env_written(sn.a, k_typed_dst_store, ip + 1); }
				else { use_slot(sn.b, 0, true, k_typed_dst_store, nullptr, ip + 1); op.a = sn.b; op.writes = sn.b; }
				if (st.op == opcode::op_const) { op.b = k_typed_const_bit | st.a; }
				else { use_slot(st.a, 0, false, 0, nullptr, ip); op.b = st.a; slot_read(op, st.a); }
				op.kind = ir::mov;
				ops.push_back(op);
				op = ir_op{}; op.kind = ir::skip;   // the store's place: no copy instruction
				++ip;
			} else if (((st.op == opcode::op_push_fused && st.a < c.push_protos.size()) || (st.op == opcode::op_t_push && ip < c.typed_sites.size() && c.typed_sites[ip].orig.a < c.push_protos.size()))
			           && ip + 1 < back && code[ip + 1].op == opcode::op_pop) {
				// `recv.push(v);` onto a frame-slot array (the fused push or its typed row, and the statement's POP): the receiver is a
				// container site, the value a cell, a constant of a pushable kind or an element of a second container at an int index (read
				// as a handle at the push, so no string is borrowed); the append is the push kernel's rows, anything they decline a miss
				const uint32_t pidx = st.op == opcode::op_push_fused ? st.a : c.typed_sites[ip].orig.a;
				const push_proto& pp = c.push_protos[pidx];
				if (!c.builtin_indexed || pp.site >= c.call_sites.size() || c.call_sites[pp.site].bi_array == builtin_method_registries::k_no_builtin) { refuse(chunk::cell_refusal_translation, ip); break; }
				if (!pp.receiver.direct || pp.receiver.slot == k_invalid_u32 || pp.receiver.slot >= k_typed_slot_mask || pp.receiver.const_index != k_invalid_u32 || pp.receiver.member_node != k_invalid_u32 || pp.receiver.is_subscript()) { refuse(chunk::cell_refusal_representation, ip); break; }
				if (pp.value.const_index != k_invalid_u32) {
					const size_t ck = pp.value.const_index < c.constants.size() && pp.value.const_index < k_typed_slot_mask ? c.constants[pp.value.const_index].raw_storage_index() : SIZE_MAX;
					if (ck != script_value::TYPEID_INT && ck != script_value::TYPEID_FLOAT && ck != script_value::TYPEID_BOOL && ck != script_value::TYPEID_STRING) { refuse(chunk::cell_refusal_representation, ip); break; }
					op.b = k_typed_const_bit | pp.value.const_index;
				} else if (pp.value_is_subscript) {
					if (pp.value.subscript_chain.size() != 1 || pp.value.member_node != k_invalid_u32 || pp.value.load_flags != 0 || (pp.value.slot == k_invalid_u32 && pp.value.symbol == k_invalid_u32)) { refuse(chunk::cell_refusal_representation, ip); break; }
					const fused_subscript_index& si = pp.value.subscript_chain[0];
					if (si.kind == fused_subscript_index::index_literal) {
						c.constants.push_back(script_value(static_cast<script_int>(si.literal), static_cast<engine*>(nullptr)));
						op.c = k_typed_const_bit | static_cast<uint32_t>(c.constants.size() - 1);
					} else {
						fused_operand io; io.slot = si.slot; io.symbol = si.symbol; io.direct = si.slot != k_invalid_u32;
						if (!value(op, io, kint, static_cast<uint32_t>(ip * 3 + 2), ip, op.c)) { refuse(chunk::cell_refusal_representation, ip); break; }
					}
					op.tok = 1;   // an element push: the source container is the copy's second site
				} else {
					if (!pp.value.direct || pp.value.slot == k_invalid_u32 || pp.value.slot >= k_typed_slot_mask || !value(op, pp.value, 0, static_cast<uint32_t>(ip * 3 + 1), ip, op.b)) { refuse(chunk::cell_refusal_representation, ip); break; }
					const int vk = find_cell(pp.value.slot);
					if (vk >= 0 && cells[vk].kind == static_cast<uint8_t>(script_value::TYPEID_STRING)) { refuse(chunk::cell_refusal_representation, ip); break; }   // a string cell is a borrow, never pushed
				}
				op.kind = ir::push; op.proto = pidx; op.container = pp.receiver.slot; op.a = pp.receiver.slot;
				ops.push_back(op);
				op = ir_op{}; op.kind = ir::skip;   // the POP's place: no copy instruction
				++ip;
			} else if (st.op == opcode::op_incdec && st.b != k_invalid_u32 && st.b < k_typed_slot_mask) {
				// ++x / x-- as a statement on a slot the typed lowering left general: arith by one with the compound landing
				if (st.c & incdec_flag_global_only) { refuse(chunk::cell_refusal_effect, ip); break; }
				if (!(st.c & incdec_flag_no_result)) { refuse(chunk::cell_refusal_translation, ip); break; }   // the value is used
				use_slot(st.b, 0, false, 0, nullptr, ip); use_slot(st.b, 0, true, k_typed_dst_compound, nullptr, ip);
				op.kind = ir::arith; op.tok = static_cast<uint8_t>((st.c & incdec_flag_increment) ? token_type::plus : token_type::minus); op.a = st.b; op.b = st.b; op.c = k_typed_const_bit | int_one(); op.writes = st.b; slot_read(op, st.b);
			} else if (st.op == opcode::op_probe_callee && st.a < c.call_sites.size()) {
				// a call (design_row118_canonical_lowering.md section 9): the probe, the argument zone, the call and its consumer are
				// one operation of the body. An argument is a value: a cell, a constant or an environment name pushed as the general
				// instruction pushes it, or a computed value (a fused binary, an element read, a math call or a unary over a slot)
				// landing in a compiler value of its own ahead of the call. The consumer lands the result by the declared-type rule
				// into a scalar declaration or a no-result store, or drops it. The call is the effect point: the slots it could bind
				// by reference are flushed ahead of it and checked after it, the environment values are re-read and the container
				// sites re-resolved after it, so nothing the callee can reach is stale in the block.
				size_t call_ip = ip + 1;
				for (const auto& z : c.call_arg_zones) { if (z.first == ip + 1) { call_ip = z.second; break; } }
				if (call_ip + 1 >= back || (code[call_ip].op != opcode::op_call_from_scratch && code[call_ip].op != opcode::op_call_typed) || code[call_ip].b != st.a || code[call_ip].c != 0) { refuse(chunk::cell_refusal_effect, ip); break; }
				const size_t consumer_ip = call_ip + 1;
				chunk::cell_loop::cell_call cc; cc.probe_ip = static_cast<uint32_t>(ip); cc.call_ip = static_cast<uint32_t>(call_ip); cc.consumer_ip = static_cast<uint32_t>(consumer_ip);
				if (c.call_sites[st.a].call_node < c.nodes.size() && code[call_ip].c == 0) {   // a tail call keeps the general splice (no raw frame grows the chain)
					const auto* ce = static_cast<const call_expr*>(c.nodes[c.call_sites[st.a].call_node].get());
					const auto decl = ce->proven_callee_decl.lock();
					if (calls.size() < 65534 && decl && raw_signature(decl.get()) && ce->arguments.size() == decl->parameters.size() && raw_call_reaches_program(decl.get(), callable_body_)) {
						cc.raw = 1;   // the arguments instruction may enter the callee's frame program raw
						for (size_t pi = 0; pi < decl->parameters.size(); ++pi) { cc.raw_kinds |= static_cast<uint32_t>(raw_scalar_kind(decl->parameters[pi].type.get())) << (4 * pi); }
					}
				}
				std::vector<ir_op> seq;   // one operation per instruction from the probe to the call
				ir_op probe_op; probe_op.kind = ir::probe; probe_op.a = static_cast<uint32_t>(calls.size()); seq.push_back(probe_op);   // the probe runs first, at its own place: its resolution may run a getter, which no argument value may precede
				ir_op call_op; call_op.kind = ir::call; call_op.a = static_cast<uint32_t>(calls.size());
				// argument evaluation stays in source order: a verbatim environment load or a reference-slot load runs the general
				// instruction at C_ARGS and may run a getter (a reference to a property), so it is an effect barrier; a computed
				// argument (its value made after the probe and ahead of the pushes) may not follow one; a slot cell boxed at C_ARGS
				// and a constant have no effect and keep their place
				bool effect_seen = false;
				for (size_t a = ip + 1; a < call_ip && refusal == chunk::cell_refusal_none; ++a) {
					const vm_instruction& ai = code[a];
					ir_op ao; ao.kind = ir::skip; ao.origin = static_cast<uint32_t>(ip + 1);   // a miss between the probe and the pushes re-runs from the first argument instruction, the callee parked
					const bool slot_load = ai.op == opcode::op_load && ai.a != k_invalid_u32 && ai.a < k_typed_slot_mask && ai.c == 0;
					const vm_instruction* next = a + 1 < call_ip ? &code[a + 1] : nullptr;
					if (slot_load && next && next->op == opcode::op_math && next->b == 1) {
						// math::fn(slot) as an argument: the intrinsic into an argument value (the math arm's rows)
						if (effect_seen) { refuse(chunk::cell_refusal_effect, a); break; }   // a computed argument after a verbatim load would run ahead of that load's getter
						uint8_t rk = kflt;
						switch (static_cast<detail::math_fn>(next->a)) {
						case detail::math_fn::itrunc: case detail::math_fn::ifloor: rk = kint; break;
						case detail::math_fn::abs_: rk = 0; break;
						case detail::math_fn::floor_: case detail::math_fn::ceil_: case detail::math_fn::sqrt_: case detail::math_fn::sin_: case detail::math_fn::cos_: case detail::math_fn::tan_:
						case detail::math_fn::radians_to_degrees: case detail::math_fn::degrees_to_radians: break;
						default: refuse(chunk::cell_refusal_translation, a + 1); break;
						}
						if (refusal != chunk::cell_refusal_none) break;
						const uint32_t t = arg_temp_next++;
						use_slot(ai.a, 0, false, 0, nullptr, a);
						use_slot(t, rk, true, k_typed_dst_temp, nullptr, a);
						ao.kind = ir::math; ao.tok = static_cast<uint8_t>(next->a); ao.a = t; ao.b = ai.a; ao.writes = t; slot_read(ao, ai.a);
						seq.push_back(ao);
						ir_op sk; sk.kind = ir::skip; seq.push_back(sk); ++a;
						cc.args.push_back(chunk::cell_loop::call_arg{ 0, t, static_cast<uint32_t>(a) });
					} else if (slot_load && next && next->op == opcode::op_unary && (static_cast<token_type>(next->a) == token_type::minus || static_cast<token_type>(next->a) == token_type::tilde)) {
						// -slot or ~slot as an argument: the unary into an argument value
						if (effect_seen) { refuse(chunk::cell_refusal_effect, a); break; }   // a computed argument after a verbatim load would run ahead of that load's getter
						const token_type tk = static_cast<token_type>(next->a);
						const uint32_t t = arg_temp_next++;
						use_slot(ai.a, 0, false, 0, nullptr, a);
						use_slot(t, tk == token_type::tilde ? kint : 0, true, k_typed_dst_temp, nullptr, a);
						ao.kind = tk == token_type::minus ? ir::neg : ir::bnot; ao.a = t; ao.b = ai.a; ao.writes = t; slot_read(ao, ai.a);
						seq.push_back(ao);
						ir_op sk; sk.kind = ir::skip; seq.push_back(sk); ++a;
						cc.args.push_back(chunk::cell_loop::call_arg{ 0, t, static_cast<uint32_t>(a) });
					} else if (slot_load) {
						// a slot: the cell the loop holds for it (the binding at the call, decided at emission), else the load runs verbatim
						if (std::find(c.reference_slots.begin(), c.reference_slots.end(), ai.a) != c.reference_slots.end()) { effect_seen = true; }   // a reference slot never holds a cell: its load runs at C_ARGS and may run a getter
						cc.args.push_back(chunk::cell_loop::call_arg{ 0, ai.a, static_cast<uint32_t>(a) });
						seq.push_back(ao);
					} else if (ai.op == opcode::op_const || (ai.op == opcode::op_load && ai.a == k_invalid_u32 && ai.c == 0)) {
						if (ai.op == opcode::op_load) { effect_seen = true; }   // the load runs at C_ARGS: nothing computed may follow it
						cc.args.push_back(chunk::cell_loop::call_arg{ 2, static_cast<uint32_t>(a), static_cast<uint32_t>(a) });   // a constant or an environment name: the general push
						seq.push_back(ao);
					} else if (ai.op == opcode::op_binary_fused && ai.a < c.fused_binary_protos.size()) {
						// a computed argument: arith into an argument value (the pinned form when the checker classed both operands)
						if (effect_seen) { refuse(chunk::cell_refusal_effect, a); break; }   // a computed argument after a verbatim load would run ahead of that load's getter
						const fused_binary_proto& bp = c.fused_binary_protos[ai.a];
						const bool arg_cmp = cmp_tok(bp.op);   // a comparison's bool is a value too
						if (!arith_tok(bp.op) && !arg_cmp) { refuse(chunk::cell_refusal_translation, a); break; }
						if (!value(ao, bp.left, 0, static_cast<uint32_t>(a * 3), a, ao.b) || !value(ao, bp.right, 0, static_cast<uint32_t>(a * 3 + 1), a, ao.c)) { refuse(chunk::cell_refusal_representation, a); break; }
						const uint32_t t = arg_temp_next++;
						uint8_t tk = 0;
						if ((bp.typed_class == 1 || bp.typed_class == 2) && !(ao.b & k_ir_env_bit) && !(ao.c & k_ir_env_bit)) {
							const opcode fast = arith_op(static_cast<token_type>(bp.op), bp.typed_class);
							if (fast != opcode::op_halt) { ao.fast = fast; tk = bp.typed_class == 1 ? kint : kflt; use_slot(ao.b, tk, false, 0, nullptr, a); use_slot(ao.c, tk, false, 0, nullptr, a); }
						}
						use_slot(t, arg_cmp ? kbool : tk, true, k_typed_dst_temp, nullptr, a);
						ao.kind = arg_cmp ? ir::cmpval : ir::arith; ao.tok = bp.op; ao.a = t; ao.writes = t;
						cc.args.push_back(chunk::cell_loop::call_arg{ 0, t, static_cast<uint32_t>(a) });
						seq.push_back(ao);
					} else if (ai.op == opcode::op_index_fused && ai.a < c.fused_index_protos.size()) {
						// an element read as an argument: the load into an argument value (the copy's own site row)
						if (effect_seen) { refuse(chunk::cell_refusal_effect, a); break; }   // a computed argument after a verbatim load would run ahead of that load's getter
						const fused_index_proto& fp = c.fused_index_protos[ai.a];
						if (fp.index_chain_ext.size() > 2 || !container_operand(fp.container) || !index_values(ao, fp, a)) { refuse(chunk::cell_refusal_representation, a); break; }
						const uint32_t t = arg_temp_next++;
						use_slot(t, 0, true, k_typed_dst_temp, nullptr, a);
						ao.kind = ir::load; ao.a = t; ao.proto = ai.a; ao.writes = t;
						ao.container = fp.container.slot != k_invalid_u32 && fp.container.slot < k_typed_slot_mask ? fp.container.slot : k_invalid_u32;
						cc.args.push_back(chunk::cell_loop::call_arg{ 0, t, static_cast<uint32_t>(a) });
						seq.push_back(ao);
					} else {
						// an argument the door does not take yet: a nested call is a value door still closed (a translation gap), the rest by class
						refuse(ai.op == opcode::op_probe_callee ? chunk::cell_refusal_translation : chunk::cell_refusal_of(ai.op), a);
						break;
					}
				}
				if (refusal != chunk::cell_refusal_none) break;
				// the consumer: a scalar declaration or a no-result frame-slot store lands by the declared-type rule, a statement drops
				const vm_instruction& cn = code[consumer_ip];
				ir_op land; land.kind = ir::land; land.a = static_cast<uint32_t>(calls.size());
				if (cn.op == opcode::op_pop) { cc.consumer = 0; }
				else if (cn.op == opcode::op_decl_var && cn.b != 0 && cn.c == 0 && cn.a < c.nodes.size()) {
					auto* decl = static_cast<const variable_decl*>(c.nodes[cn.a].get());
					type_info* stamp = decl->type.get();
					if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) != variable_decl::decl_fast_slot_store || decl->ref_escaping || decl->slot_index == SIZE_MAX || decl->slot_index >= k_typed_slot_mask) { refuse(chunk::cell_refusal_representation, consumer_ip); break; }
					if (stamp && stamp_kind(stamp) == 0 && stamp->base_type != script_value_type::jai_any_type) { refuse(chunk::cell_refusal_representation, consumer_ip); break; }   // a string, bool or class local
					cc.consumer = 1; cc.dst = static_cast<uint32_t>(decl->slot_index);
					{ const uint8_t ld = decl_landing(cc.dst); use_slot(cc.dst, stamp_kind(stamp), true, ld, ld == k_typed_dst_temp ? nullptr : stamp, consumer_ip); }
					land.writes = cc.dst;
				} else if (cn.op == opcode::op_store && cn.b != k_invalid_u32 && cn.b < k_typed_slot_mask) {
					if (cn.c & (store_flag_ref_alias | store_flag_global_only)) { refuse(chunk::cell_refusal_representation, consumer_ip); break; }
					if (!(cn.c & store_flag_no_result)) { refuse(chunk::cell_refusal_translation, consumer_ip); break; }   // an assignment whose value is used
					cc.consumer = 2; cc.dst = cn.b;
					use_slot(cn.b, 0, true, k_typed_dst_store, nullptr, consumer_ip);
					land.writes = cn.b;
				} else if (cn.op == opcode::op_store && cn.b == k_invalid_u32 && !(cn.c & (store_flag_ref_alias | store_flag_global_only)) && (cn.c & store_flag_no_result)) {
					cc.consumer = 2; cc.dst = k_ir_env_bit | env_written(cn.a, k_typed_dst_store, consumer_ip);   // an environment value takes the result
				} else if (cn.op == opcode::op_call_temp && cn.a < k_typed_slot_mask) {
					cc.consumer = 3; cc.dst = cn.a;   // the result into a compiler temp: the expression around the call reads it as a slot operand
					use_slot(cn.a, 0, true, k_typed_dst_temp, nullptr, consumer_ip);
					land.writes = cn.a;
				} else { refuse(chunk::cell_refusal_of(cn.op), consumer_ip); break; }   // the result flows into an operation without a door yet
				for (const auto& arg : cc.args) {
					// a compiler temp passed as an argument stays live to the call (a miss ahead of it re-runs the pushes from the slot)
					if (arg.kind != 0 || arg.v >= static_cast<uint32_t>(c.local_count)) continue;
					const int k = find_cell(arg.v);
					if (k < 0 || !cells[k].temp) continue;
					if (call_op.nreads == 8) { refuse(chunk::cell_refusal_translation, ip); break; }
					call_op.reads[call_op.nreads++] = arg.v;
				}
				if (refusal != chunk::cell_refusal_none) break;
				seq.push_back(call_op);
				calls.push_back(std::move(cc));
				for (const ir_op& so : seq) { ops.push_back(so); }
				op = land;
				ip = consumer_ip;   // the consumer is this iteration's instruction
			} else {
				// the class of the refusal is the first consumer's when the instruction is a pure push (a call's argument, a
				// member's receiver): the push itself says nothing about why the loop stays general
				auto pure_push = [](opcode o) { return o == opcode::op_load || o == opcode::op_const || o == opcode::op_null || o == opcode::op_true || o == opcode::op_false || o == opcode::op_dup; };
				size_t at = ip;
				while (at + 1 < back && pure_push(code[at].op)) { ++at; }
				refuse(chunk::cell_refusal_of(code[at].op), at);
				break;
			}
			if (!alias_site(op, ip)) break;
			ops.push_back(op);
		}
		if (refusal == chunk::cell_refusal_translation && snap_stmt && (snap_stmt->get_type() == node_type::expression_stmt || snap_stmt->get_type() == node_type::variable_decl || snap_stmt->get_type() == node_type::expression_decl)
		    && snap_ip >= first && (line || counted != k_invalid_u32 || snap_ip > cmp_ip) && last_ip >= snap_ip && refusal_ip >= snap_ip && ops.size() >= snap_ip - first) {
			const uint32_t arm_end = arm_end_of(snap_ip);
			if (arm_end != k_invalid_u32 && refusal_ip < arm_end && last_ip < arm_end) {
				cells = snap_cells; env_ops.resize(snap_env); defs.resize(snap_defs); calls.resize(snap_calls); arg_temp_next = snap_arg; alias_slots.resize(snap_alias);
				std::erase_if(bail_held, [&](const auto& h) { return h.first >= snap_ip; });
				ops.resize(snap_ip - first);
				size_t stmt_start = snap_ip;
				while (stmt_start > first && caller_stmt_of(stmt_start - 1) == caller_stmt_of(snap_ip)) { --stmt_start; }
				std::vector<uint32_t> held;
				for (const auto& [dip, dk] : defs) {
					if (dip < stmt_start || dip >= snap_ip || cells[dk].slot >= static_cast<uint32_t>(c.local_count)) continue;   // an argument's synthetic slot is never flushed
					std::erase_if(held, [&](uint32_t h) { return cells[h].slot == cells[dk].slot; });
					held.push_back(dk);
				}
				if (!held.empty()) { bail_held.emplace_back(static_cast<uint32_t>(snap_ip), std::move(held)); }
				ir_op bail; bail.kind = ir::bail; bail.target = static_cast<uint32_t>(snap_ip);
				ops.push_back(bail);
				for (size_t k = snap_ip + 1; k < arm_end; ++k) { ir_op sk; sk.kind = ir::skip; ops.push_back(sk); }
				refusal = chunk::cell_refusal_none; refusal_ip = static_cast<uint32_t>(back);
				resume_from = arm_end;
				continue;
			}
		}
		break;
		}
		if (refusal == chunk::cell_refusal_none && cells.empty()) { refuse(chunk::cell_refusal_no_cells, back); }
		for (size_t k = 0; refusal == chunk::cell_refusal_none && k < ops.size(); ++k) {
			// the cell forms read a comparison's left operand and a math argument as cells: a literal on the left mirrors at the emission, two
			// literals or a literal argument have no encoding
			const ir_op& o = ops[k];
			if ((o.kind == ir::cmp && (o.a & k_typed_const_bit) && (o.b & k_typed_const_bit)) || (o.kind == ir::cmpval && (o.b & k_typed_const_bit) && (o.c & k_typed_const_bit)) ||
			    (o.kind == ir::math && (o.b & k_typed_const_bit))) { refuse(chunk::cell_refusal_translation, first + k); }
		}
		if (line && refusal == chunk::cell_refusal_none && !ops.empty() && ops[0].kind == ir::exit) { continue; }   // a region that leaves at its first operation runs nothing
		for (const uint32_t si : loop_inline_sites) {   // a method graft's guard runs at the entry and after each call: its receiver stays put for the block
			const chunk::inline_site& s = c.inline_sites[si];
			if (!s.method) continue;
			bool held = !is_alias(s.receiver_slot);
			for (const ir_op& o : ops) { if (o.writes == s.receiver_slot) { held = false; } }
			if (!held) { refuse(chunk::cell_refusal_translation, s.guard_ip); }
		}
		if (refusal == chunk::cell_refusal_none && cells.size() > chunk::k_cell_limit) { refuse(chunk::cell_refusal_too_many_cells, back); }
		// legality and lifetimes (region_ir.hpp verify): a container binding's slot is never written; a slot the block enters with
		// undefined is defined on every path to each of its reads; a snapshot's slot is written only by its own guard between the
		// guard and its read. The must-define dataflow over the region's control flow graph is the rule every pass must keep
		if (refusal == chunk::cell_refusal_none) { const region_verdict v = verify(region, cells); if (v.refusal != chunk::cell_refusal_none) { refuse(v.refusal, v.at); } }
		if (fn_mode) {
			// a frame program pays its entry only for its raw calls: without one (or refused) the body keeps its ordinary spans
			bool any_raw = false;
			for (const auto& rc : calls) { if (rc.raw) { any_raw = true; } }
			if (refusal != chunk::cell_refusal_none || !any_raw) { add_spans(0, original_size); continue; }
		}
		if (refusal != chunk::cell_refusal_none) {
			if (line) {
				// the span splits around the refusing statement (the parts before and after it are spans of their own), or in halves when it
				// holds too many cells; a refusal of the whole span (no cells, a binding it rewrites) drops it
				if (refusal == chunk::cell_refusal_too_many_cells) {
					size_t mid = head + (back - head) / 2;
					while (mid > head && !boundary(mid)) { --mid; }
					if (mid > head) { add_spans(head, mid); add_spans(mid, back); }
				} else if (refusal_ip > head && refusal_ip < back) {
					size_t rs = refusal_ip, re = refusal_ip + 1;
					while (rs > head && !boundary(rs)) { --rs; }
					while (re < back && !boundary(re)) { ++re; }
					if (rs > head) { add_spans(head, rs); }
					add_spans(re, back);
				} else if (refusal_ip == head) {
					size_t re = head + 1;
					while (re < back && !boundary(re)) { ++re; }
					add_spans(re, back);
				}
				continue;
			}
			c.cell_refusals.push_back(chunk::cell_refusal{ static_cast<uint32_t>(head), static_cast<uint32_t>(back), refusal_ip, refusal, static_cast<uint8_t>(code[refusal_ip].op) });
			continue;
		}
		chunk::cell_loop loop;
		loop.head = static_cast<uint32_t>(head); loop.back = static_cast<uint32_t>(back); loop.counted = counted;
		loop.exit_ip = line ? static_cast<uint32_t>(back) : counted != k_invalid_u32 ? c.counted_for_protos[counted].exit_ip : (hd.op == opcode::op_fused_cmp_jump || hd.op == opcode::op_jump_if_false ? hd.a : hd.c);
		uint32_t max_slot = 0; for (const auto& cl : cells) { if (cl.env_operand == k_invalid_u32 && cl.slot > max_slot) max_slot = cl.slot; }
		for (auto& cl : cells) { if (cl.env_operand != k_invalid_u32) { cl.slot = ++max_slot; } }   // the environment values' synthetic slots
		loop.cells = cells;
		loop.iter_proto = range_proto; loop.iter_index_cell = range_index_slot != k_invalid_u32 && find_cell(range_index_slot) >= 0 ? static_cast<uint32_t>(find_cell(range_index_slot)) : k_invalid_u32;
		loop.env_operands = env_ops;
		loop.inner_counted = inner_counted;
		if (ir_listing_) { loop.ir_text = listing(region); }   // the listing tool's text (region_ir.hpp)
		loop.slot_to_cell.assign(max_slot + 1, k_invalid_u32);
		for (uint32_t k = 0; k < cells.size(); ++k) { if (loop.slot_to_cell[cells[k].slot] == k_invalid_u32) { loop.slot_to_cell[cells[k].slot] = k; } }   // the first cell per slot; definitions advance the binding
		for (uint32_t k = 0; k < cells.size(); ++k) { if (cells[k].temp) { loop.temp_mask.set(k); } }
		for (uint32_t k = 0; k < cells.size(); ++k) { if (cells[k].env_operand != k_invalid_u32) { loop.env_mask.set(k); } }
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
		// a pinned operand: a cell whose declared kind is the operation's (its kind byte never differs inside the block: every
		// write lands it or misses, and the verifier proved no read ahead of a first declaration), or a constant of that kind;
		// the operation then needs no per-read kind check
		auto pinned_operand = [&](uint32_t cellop, uint8_t k) -> bool {
			if (!ir_passes_) return false;   // the switch off: the checked forms only
			if (cellop & k_typed_const_bit) { const uint32_t ci = cellop & ~k_typed_const_bit; return ci < c.constants.size() && static_cast<uint8_t>(c.constants[ci].raw_storage_index()) == k; }
			return cellop < cells.size() && cells[cellop].kind == k;
		};
		auto dst_of = [&](uint32_t operand) -> uint32_t {
			if (operand & k_ir_env_bit) return cell_of(operand);   // an environment value has one cell
			if (pending_def != k_invalid_u32 && cells[pending_def].slot == (operand & k_typed_slot_mask)) return pending_def;
			return cell_of(operand);
		};
		// a proto operand as the copy's site or proto reads it: an environment value becomes its synthetic slot
		auto synthetic = [&](fused_operand& o) {
			if (!env_scalar(o)) return;
			for (size_t e = 0; e < env_ops.size(); ++e) {
				if (c.symbols[env_ops[e].operand.symbol] == c.symbols[o.symbol]) { fused_operand d; d.slot = env_slot(static_cast<uint32_t>(e)); d.symbol = o.symbol; d.direct = true; o = d; return; }
			}
		};
		auto site_operand = [&](fused_operand o, uint32_t& out) { synthetic(o); out = o.const_index != k_invalid_u32 ? (k_typed_const_bit | o.const_index) : current[o.slot]; };   // the copy's rows name cells
		auto cellify = [&](fused_operand& o) {   // a proto operand for the copy: the cell of the value it reads (an empty chain-step left stays empty)
			if (o.const_index != k_invalid_u32 || (o.slot == k_invalid_u32 && o.symbol == k_invalid_u32)) return;
			synthetic(o);
			fused_operand d; d.slot = o.slot < current.size() ? current[o.slot] : k_invalid_u32; d.symbol = o.symbol; d.direct = true; o = d;
		};
		// the copy: the cell instructions of each original instruction (one for most, none for a push the call boxes or a jump
		// a compare carries, several for a call); copy_at maps an original ip to its first copy instruction, and the branch
		// targets and the guards' read offsets are patched through it after the emission; the origin table is the runtime's map back
		loop.copy_head = static_cast<uint32_t>(code.size());
		const uint32_t loop_index = static_cast<uint32_t>(c.cell_loops.size());
		std::vector<uint32_t> copy_at(back - head + 1, k_invalid_u32);
		struct fixup { uint32_t at; uint8_t field; uint32_t target; uint32_t bits; };   // field 0 = a (a jump), 1 = c (a compare's target | its sense bit), 2 = b (a guard: alias | read offset << 8)
		std::vector<fixup> fixups;
		loop.calls = calls;
		for (const auto& cc : calls) { if (cc.raw) { loop.plans.resize(calls.size()); loop.raw_sites = true; break; } }   // a raw site's run-time plan
		loop.inline_sites = loop_inline_sites;
		loop.math_alias_sites = loop_alias_sites;
		size_t origin_override = SIZE_MAX;   // an argument's value records the probe as its origin
		struct ret_pad { uint32_t at, proof, origin; };   // a C_RET's copy ip, the return's proof mode and the general ip of its tail
		std::vector<ret_pad> ret_pads;
		auto append = [&](vm_instruction ins, size_t from_ip) {
			if (origin_override != SIZE_MAX) { from_ip = origin_override; }
			code.push_back(ins);
			c.stmt_nodes.push_back(from_ip < c.stmt_nodes.size() ? c.stmt_nodes[from_ip] : nullptr);
			loop.origin.push_back(static_cast<uint32_t>(from_ip));
		};
		auto emit = [&](const ir_op& op, size_t ip) {
			copy_at[ip - head] = static_cast<uint32_t>(code.size());
			origin_override = op.origin != k_invalid_u32 ? op.origin : SIZE_MAX;
			pending_def = next_def < defs.size() && defs[next_def].first == ip ? defs[next_def].second : k_invalid_u32;
			if (op.poll) { append(vm_instruction{opcode::op_c_jump, static_cast<uint32_t>(code.size() + 1), 0, 1}, ip); }   // the poll ahead of an inner counted step: a continue lands here too (copy_at names it)
			switch (op.kind) {
			case ir::arith: {
				uint32_t b2 = cell_of(op.b), c2 = cell_of(op.c);
				opcode fast = op.fast;
				if (fast != opcode::op_halt && (b2 & k_typed_const_bit)) {
					// a literal on the left: the pinned forms read their left operand as a cell, so a commutative operator swaps and any
					// other takes the any-kind form, which reads a constant on either side
					if (commutative_fast(fast) && !(c2 & k_typed_const_bit)) { std::swap(b2, c2); }
					else { fast = opcode::op_halt; }
				}
				if (fast != opcode::op_halt) {
					const uint8_t k = fast <= opcode::op_t_shr_ii ? static_cast<uint8_t>(script_value::TYPEID_INT) : static_cast<uint8_t>(script_value::TYPEID_FLOAT);
					append(vm_instruction{pinned_operand(b2, k) && pinned_operand(c2, k) ? opcode::op_c_arith_p : opcode::op_c_arith, (static_cast<uint32_t>(fast) << k_cell_op_shift) | dst_of(op.a), b2, c2}, ip);
				}
				else { append(vm_instruction{opcode::op_c_arith_any, (static_cast<uint32_t>(op.tok != 0 ? op.tok : arith_token_of(op.fast)) << k_cell_op_shift) | dst_of(op.a), b2, c2}, ip); }
				break;
			}
			case ir::mov: append(vm_instruction{opcode::op_c_mov, dst_of(op.a), cell_of(op.b), op.tok}, ip); break;
			case ir::cmp: {
				const uint32_t bits = op.sense ? 0x80000000u : 0u;
				if (!op.head) { fixups.push_back(fixup{ static_cast<uint32_t>(code.size()), 1, op.target, bits }); }   // an interior target: patched to its copy position
				const uint32_t target = op.head ? op.target : bits;
				uint32_t a2 = cell_of(op.a), b2 = cell_of(op.b);
				opcode fast = op.fast;
				uint8_t tok = op.tok;
				if (a2 & k_typed_const_bit) { std::swap(a2, b2); fast = mirror_fast(fast); tok = mirror_tok(tok); }   // a literal on the left: the mirrored comparison (the translation refuses two literals)
				if (fast != opcode::op_halt) {
					const uint8_t k = fast <= opcode::op_t_jne_ii ? static_cast<uint8_t>(script_value::TYPEID_INT) : static_cast<uint8_t>(script_value::TYPEID_FLOAT);
					const bool p = pinned_operand(a2, k) && pinned_operand(b2, k);
					append(vm_instruction{op.head ? (p ? opcode::op_c_cmp_p : opcode::op_c_cmp) : (p ? opcode::op_c_jcmp_p : opcode::op_c_jcmp), (static_cast<uint32_t>(fast) << k_cell_op_shift) | a2, b2, target}, ip);
				}
				else { append(vm_instruction{op.head ? opcode::op_c_cmp_any : opcode::op_c_jcmp_any, (static_cast<uint32_t>(tok) << k_cell_op_shift) | a2, b2, target}, ip); }
				break;
			}
			case ir::load: {
				if (op.member != k_invalid_u32) { append(vm_instruction{opcode::op_c_mload, dst_of(op.a), op.member, op.container}, ip); break; }   // a member read site: the field resolved at entry
				// the copy's own site row at the copy instruction's index: the general row keeps slots, this one names cells
				chunk::typed_site site;
				if (op.container == chunk::k_iter_container) {
					// a range-for's landing: the iteration state's container at the index cell, the element's own kind unless the loop variable's
					// cell is pinned by its uses (then another kind misses, and the general ITER_NEXT binds it)
					site.cont_slot = chunk::k_iter_container; site.operands[0] = current[op.b]; site.orig = code[ip];
					const uint32_t d = dst_of(op.a);
					site.decl_expect = loop.cells[d].kind == 0 ? 0xFF : loop.cells[d].kind;
				} else if (op.proto != k_invalid_u32) {
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
					site.temp = fp.decl_temp || (op.writes != k_invalid_u32 && op.writes >= static_cast<uint32_t>(c.local_count));   // an argument's value is a compiler value too
					site.speculative = fp.speculative;
					site.guard_temp = fp.container_guard; site.guard_always = fp.guard_always;
				} else {
					site = c.typed_sites[ip];   // a T_LOAD's row with its slot operands rebound to cells
					for (uint8_t k = 0; k <= site.fold_count && k < 4; ++k) { if (!(site.operands[k] & k_typed_const_bit)) { site.operands[k] = current[site.operands[k] & k_typed_slot_mask]; } }
				}
				chunk_ = &c;
				typed_site_at(code.size()) = site;
				const uint32_t dst = dst_of(op.a);   // an adopted-kind local lands through the declared-type rule; a pinned local or a compiler value lands in place
				const uint32_t miss_ip = op.origin != k_invalid_u32 ? op.origin : static_cast<uint32_t>(ip);   // a range landing misses onto the general ITER_NEXT, an argument's read onto the first argument instruction (argument values are never flushed)
				append(vm_instruction{loop.cells[dst].kind == 0 && !loop.cells[dst].temp ? opcode::op_c_load_any : opcode::op_c_load, dst, loop_index, miss_ip}, ip);
				break;
			}
			case ir::store: {
				if (op.member != k_invalid_u32) { append(vm_instruction{opcode::op_c_mstore, (op.b & k_typed_const_bit) ? op.b : current[op.b], op.member, op.container}, ip); break; }   // a member store site
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
			case ir::jump: fixups.push_back(fixup{ static_cast<uint32_t>(code.size()), 0, op.target, 0 }); append(vm_instruction{opcode::op_c_jump, 0, 0, op.tok}, ip); break;   // c = 1: an inner loop's back edge
			case ir::guard: {
				const uint32_t alias = op.a < current.size() ? current[op.a] : k_invalid_u32;
				fixups.push_back(fixup{ static_cast<uint32_t>(code.size()), 2, static_cast<uint32_t>(head + op.guarded), alias == k_invalid_u32 ? 0x1FFu : alias });   // the guarded read's copy offset
				append(vm_instruction{opcode::op_c_guard, op.a, 0, static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::placeholder: append(vm_instruction{opcode::op_c_guard, 0, k_invalid_u32, 0}, ip); break;   // a counted loop has no head compare
			case ir::neg: append(vm_instruction{opcode::op_c_neg, dst_of(op.a), cell_of(op.b), 0}, ip); break;
			case ir::bnot: append(vm_instruction{opcode::op_c_bnot, dst_of(op.a), cell_of(op.b), 0}, ip); break;
			case ir::call: {
				// the pushes and the call at the call's place (the probe has its own, ahead); a slot argument is the cell the loop holds for it here
				// (its latest definition), else its load runs verbatim; the pushes are an argument zone of their own
				chunk::cell_loop::cell_call& cc = loop.calls[op.a];
				for (auto& arg : cc.args) {
					if (arg.kind != 0) continue;
					const uint32_t cl = arg.v < current.size() ? current[arg.v] : k_invalid_u32;
					if (cl != k_invalid_u32) { arg.kind = 1; arg.v = cl; } else { arg.kind = 2; arg.v = arg.ip; }
				}
				if (cc.raw) {
					// every argument a pinned cell or a constant of its parameter's kind: the raw entry checks none (the plan compares the callee's kinds once)
					uint32_t kinds = 0;
					bool proven = true;
					for (size_t ai = 0; ai < cc.args.size() && proven; ++ai) {
						const chunk::cell_loop::call_arg& arg = cc.args[ai];
						uint8_t k = 0;
						if (arg.kind == 1) { k = arg.v < loop.cells.size() ? loop.cells[arg.v].kind : 0; }
						else if (code[arg.v].op == opcode::op_const && code[arg.v].a < c.constants.size()) {
							const size_t ck = c.constants[code[arg.v].a].raw_storage_index();
							k = ck == script_value::TYPEID_INT || ck == script_value::TYPEID_FLOAT || ck == script_value::TYPEID_BOOL ? static_cast<uint8_t>(ck) : 0;
						}
						proven = k != 0;
						kinds |= static_cast<uint32_t>(k) << (4 * ai);
					}
					cc.raw_proven = proven && kinds == cc.raw_kinds ? 1 : 0;
				}
				c.call_arg_zones.emplace_back(static_cast<uint32_t>(code.size()), static_cast<uint32_t>(code.size() + 1));
				append(vm_instruction{opcode::op_c_args, op.a, loop_index, cc.call_ip}, cc.probe_ip);
				append(vm_instruction{opcode::op_c_call, op.a, loop_index, cc.call_ip}, cc.call_ip);
				break;
			}
			case ir::land: {
				chunk::cell_loop::cell_call& cc = loop.calls[op.a];
				if (cc.consumer != 0) { cc.dst = dst_of(cc.dst); }   // the landing cell
				append(vm_instruction{opcode::op_c_land, op.a, loop_index, static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::probe: { const chunk::cell_loop::cell_call& cc = loop.calls[op.a]; append(vm_instruction{opcode::op_c_probe, op.a, loop_index, cc.probe_ip}, cc.probe_ip); break; }   // the callee resolved at its own place
			case ir::skip: break;
			case ir::exit: append(vm_instruction{opcode::op_c_exit, op.target, 0, 0}, ip); break;
			case ir::rebind: {
				// the alias's declaration: its source read's site row for the copy (the container the general operand, resolved and pinned at entry;
				// the index operands name cells) and the rebind record, whose source site and dependents are filled in with the site list
				chunk::typed_site site;
				chunk::cell_loop::rebind rb; rb.copy_ip = static_cast<uint32_t>(code.size()); rb.cell = dst_of(op.a);
				if (op.container == chunk::k_iter_container) {
					// a range-for's by-reference loop variable: the iteration state's container at the index cell
					site.cont_slot = chunk::k_iter_container; site.operands[0] = current[op.b]; site.orig = code[ip]; rb.iter = true;
					chunk_ = &c;
					typed_site_at(code.size()) = site;
					loop.rebinds.push_back(rb);
					append(vm_instruction{opcode::op_c_rebind, static_cast<uint32_t>(loop.rebinds.size() - 1), loop_index, static_cast<uint32_t>(op.origin)}, ip);   // a miss re-runs the general ITER_NEXT
					break;
				}
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
				site.guard_temp = fp.container_guard; site.guard_always = fp.guard_always;
				chunk_ = &c;
				typed_site_at(code.size()) = site;
				loop.rebinds.push_back(rb);
				append(vm_instruction{opcode::op_c_rebind, static_cast<uint32_t>(loop.rebinds.size() - 1), loop_index, static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::range: {
				// the head: its site row names the iteration state's container; the exit target is the general ITER_POP
				chunk::typed_site site; site.cont_slot = chunk::k_iter_container; site.operands[0] = current[op.a]; site.orig = code[ip];
				chunk_ = &c;
				typed_site_at(code.size()) = site;
				append(vm_instruction{opcode::op_c_range, loop_index, cell_of(op.a), op.target}, ip);
				break;
			}
			case ir::bail: append(vm_instruction{opcode::op_c_bail, op.target, 0, 0}, ip); break;   // a planned miss at its own origin
			case ir::ret: ret_pads.push_back({ static_cast<uint32_t>(code.size()), op.proto, op.origin }); append(vm_instruction{opcode::op_c_ret, cell_of(op.a), 0, op.origin}, ip); break;   // the pad is patched after the copy
			case ir::size: append(vm_instruction{opcode::op_c_size, dst_of(op.a), op.container, 0}, ip); break;   // the array site's live size
			case ir::append: {
				// a plus onto a cell pinned to int or float with an operand of the same pinned kind is the typed add (no string row can apply)
				const uint32_t b2 = cell_of(op.b), c2 = cell_of(op.c);
				const uint8_t kint2 = static_cast<uint8_t>(script_value::TYPEID_INT), kflt2 = static_cast<uint8_t>(script_value::TYPEID_FLOAT);
				if (op.tok == static_cast<uint8_t>(token_type::plus) && op.a == op.b) {
					if (pinned_operand(b2, kint2) && pinned_operand(c2, kint2)) { append(vm_instruction{opcode::op_c_arith_p, (static_cast<uint32_t>(opcode::op_t_add_ii) << k_cell_op_shift) | dst_of(op.a), b2, c2}, ip); break; }
					if (pinned_operand(b2, kflt2) && pinned_operand(c2, kflt2)) { append(vm_instruction{opcode::op_c_arith_p, (static_cast<uint32_t>(opcode::op_t_add_ff) << k_cell_op_shift) | dst_of(op.a), b2, c2}, ip); break; }
				}
				append(vm_instruction{opcode::op_c_append, (static_cast<uint32_t>(op.tok) << k_cell_op_shift) | dst_of(op.a), b2, c2}, ip);
				break;
			}
			case ir::push: {
				// the copy's proto: a clone whose value, or the index of its element, names a cell; the receiver and the source container
				// stay the general operands, resolved from the frame or the environment at entry; the original proto stays the general form's
				push_proto cp = c.push_protos[op.proto];
				if (op.tok == 1) {
					fused_subscript_index& si = cp.value.subscript_chain[0];
					if (op.c & k_typed_const_bit) { si.kind = fused_subscript_index::index_literal; si.literal = static_cast<int64_t>(c.constants[op.c & ~k_typed_const_bit].unchecked_as_int()); si.slot = k_invalid_u32; si.symbol = k_invalid_u32; }
					else { si.kind = fused_subscript_index::index_ident; si.slot = cell_of(op.c); si.symbol = k_invalid_u32; }
				} else if (!(op.b & k_typed_const_bit)) {
					fused_operand d; d.slot = cell_of(op.b); d.direct = true; cp.value = d;
				}
				c.push_protos.push_back(cp);
				append(vm_instruction{opcode::op_c_push, static_cast<uint32_t>(c.push_protos.size() - 1), loop_index, static_cast<uint32_t>(ip)}, ip);
				break;
			}
			case ir::lnot: append(vm_instruction{opcode::op_c_lnot, dst_of(op.a), cell_of(op.b), 0}, ip); break;
			case ir::cmpval: {
				uint32_t b2 = cell_of(op.b), c2 = cell_of(op.c);
				uint8_t tok = op.tok;
				if (b2 & k_typed_const_bit) { std::swap(b2, c2); tok = mirror_tok(tok); }   // a literal on the left: the mirrored comparison
				append(vm_instruction{opcode::op_c_cmpval, (static_cast<uint32_t>(tok) << k_cell_op_shift) | dst_of(op.a), b2, c2}, ip);
				break;
			}
			}
			while (next_def < defs.size() && defs[next_def].first == ip) { current[cells[defs[next_def].second].slot] = defs[next_def].second; ++next_def; }   // the definition takes effect after its instruction
		};
		if (counted != k_invalid_u32) { emit(head_op, head); }
		for (size_t k = 0; k < ops.size(); ++k) { emit(ops[k], first + k); }
		loop.copy_back = static_cast<uint32_t>(code.size());
		if (line) {
			append(vm_instruction{opcode::op_c_exit, static_cast<uint32_t>(back), 0, 0}, back);   // the end: flush, and the general form continues at back
		} else if (counted == k_invalid_u32) {
			if (range_proto != k_invalid_u32) { append(vm_instruction{opcode::op_c_back_step, loop_index, current[range_index_slot], 0}, back); }   // the index steps at the back edge
			else { append(vm_instruction{opcode::op_c_back, loop_index, 0, 0}, back); }
			code[back] = vm_instruction{opcode::op_cells_back, loop_index, static_cast<uint32_t>(head), 0};
		} else {
			const counted_for_proto& p = c.counted_for_protos[counted];
			auto int_cop = [&](fused_operand o) { if (o.const_index != k_invalid_u32) return k_typed_const_bit | o.const_index; synthetic(o); return current[o.slot]; };   // an environment bound reads its synthetic slot's cell
			append(vm_instruction{opcode::op_c_cfor_back, current[p.var.slot], int_cop(p.end), int_cop(p.step)}, back);
			code[back] = vm_instruction{opcode::op_cells_cfor_back, loop_index, static_cast<uint32_t>(head), 0};
		}
		copy_at[back - head] = loop.copy_back;
		for (uint32_t ip2 = loop.copy_head; ip2 < loop.copy_back; ++ip2) { const opcode o = code[ip2].op; if (o == opcode::op_c_load || o == opcode::op_c_load_any || o == opcode::op_c_index_store || o == opcode::op_c_push || o == opcode::op_c_rebind || o == opcode::op_c_range || o == opcode::op_c_mload || o == opcode::op_c_mstore || o == opcode::op_c_size) { loop.sites.push_back(ip2); } }
		for (uint32_t k = 0; k < loop.sites.size(); ++k) { if (code[loop.sites[k]].op == opcode::op_c_range) { loop.iter_site = k; } }
		if (!loop.rebinds.empty()) {
			// the alias's sites: every load or store whose container slot is the alias, resolved by the rebind and never by the entry
			loop.site_alias.assign(loop.sites.size(), 0);
			auto container_slot_of = [&](uint32_t cip) -> uint32_t {
				const opcode o = code[cip].op;
				if (o == opcode::op_c_load || o == opcode::op_c_load_any || o == opcode::op_c_rebind) {
					const chunk::typed_site& s = c.typed_sites[cip];
					if (s.index_proto != k_invalid_u32) { const fused_operand& co = c.fused_index_protos[s.index_proto].container; return co.const_index == k_invalid_u32 && co.slot != k_invalid_u32 && co.slot < k_typed_slot_mask ? co.slot : k_invalid_u32; }
					return s.cont_slot;
				}
				if (o == opcode::op_c_index_store) { const fused_operand& co = c.fused_index_protos[code[cip].a].container; return co.const_index == k_invalid_u32 && co.slot != k_invalid_u32 && co.slot < k_typed_slot_mask ? co.slot : k_invalid_u32; }
				if (o == opcode::op_c_mload || o == opcode::op_c_mstore) return code[cip].c;   // a member site: its base slot (an alias's member sites are its dependents)
				return k_invalid_u32;
			};
			for (chunk::cell_loop::rebind& rb : loop.rebinds) {
				const uint32_t aslot = loop.cells[rb.cell].slot;
				for (uint32_t k = 0; k < loop.sites.size(); ++k) {
					if (loop.sites[k] == rb.copy_ip) { rb.source_site = k; continue; }
					if (container_slot_of(loop.sites[k]) == aslot) { rb.dependents.push_back(k); loop.site_alias[k] = 1; }
				}
			}
		}
		loop.site_env_cache.assign(loop.sites.size(), k_invalid_u32);
		for (uint32_t k = 0; k < loop.sites.size(); ++k) {
			const uint32_t cip = loop.sites[k];
			const opcode o = code[cip].op;
			if ((o != opcode::op_c_load && o != opcode::op_c_load_any) || (k < loop.site_alias.size() && loop.site_alias[k]) || cip >= c.typed_sites.size()) continue;
			const chunk::typed_site& s = c.typed_sites[cip];
			if (s.cont_slot != k_invalid_u32) continue;
			const uint32_t pi = s.index_proto != k_invalid_u32 ? s.index_proto : s.orig.a;
			if (pi >= c.fused_index_protos.size()) continue;
			const fused_operand& co = c.fused_index_protos[pi].container;
			if (co.member_node != k_invalid_u32 || co.const_index != k_invalid_u32 || co.is_subscript() || co.load_flags != 0 || co.slot != k_invalid_u32 || co.symbol == k_invalid_u32) continue;
			loop.site_env_cache[k] = s.fold_count ? code[cip].c * 3 + 2 : code[cip].c * 3;
		}
		std::vector<std::pair<uint32_t, uint32_t>> exits;   // (general target, its exit's copy ip): a compare's edge out of the loop
		auto exit_at = [&](uint32_t target) -> uint32_t {
			for (const auto& x : exits) { if (x.first == target) return x.second; }
			const uint32_t at = static_cast<uint32_t>(code.size());
			append(vm_instruction{opcode::op_c_exit, target, 0, 0}, back);
			exits.emplace_back(target, at);
			return at;
		};
		for (const fixup& fx : fixups) {
			const uint32_t to = fx.target > back ? exit_at(fx.target) : copy_at[fx.target - head];   // a target without a copy instruction lands on the next one; one past the back edge exits
			if (fx.field == 0) { code[fx.at].a = to; }
			else if (fx.field == 1) { code[fx.at].c = to | fx.bits; }
			else { code[fx.at].b = fx.bits | ((to - loop.copy_head) << 9); }
		}
		loop.live_temps = live_temps(region, cells, loop.slot_to_cell, defs, copy_at, loop.copy_head, loop.origin.size());   // liveness of compiler values per copy instruction (region_ir.hpp)
		// an alias is a local the general continuation may read anywhere after its declaration (a return inside the body, a bail, a
		// miss), never a statement's value: live from its rebind to the end of the copy, so every flush after the rebind materializes it
		for (const chunk::cell_loop::rebind& rb : loop.rebinds) { for (size_t m = rb.copy_ip - loop.copy_head; m < loop.live_temps.size(); ++m) { loop.live_temps[m].set(rb.cell); } }
		for (const auto& [bip, held] : bail_held) {
			const size_t m = copy_at[bip - head] - loop.copy_head;
			if (m < loop.live_temps.size()) { for (const uint32_t k : held) { loop.live_temps[m].set(k); } }
		}
		// fused pairs (row 150): the block loop runs an adjacent pair of plain instructions as one dispatch; the first instruction takes the
		// pair's opcode and keeps its word, the second keeps both (a jump into it dispatches it alone); greedy left to right, a pair's second
		// instruction starts no pair; the sites, the origins, the site rows and the liveness are per instruction and stand as they are
		if (ir_passes_) {   // the middle end's switch: off, the pairs stay two instructions (jaiscript_fuzz --no-passes compares)
			for (uint32_t ip2 = loop.copy_head; ip2 + 1 < loop.copy_back; ++ip2) {
				const opcode a1 = code[ip2].op, b1 = code[ip2 + 1].op;
				opcode fused = opcode::op_halt;
				if (a1 == opcode::op_c_arith_p && b1 == opcode::op_c_arith_p) { fused = opcode::op_c_arith2; }
				else if (a1 == opcode::op_c_jcmp_p && b1 == opcode::op_c_jcmp_p) { fused = opcode::op_c_jcmp2; }
				else if (a1 == opcode::op_c_load && b1 == opcode::op_c_load) { fused = opcode::op_c_load2; }
				else if (a1 == opcode::op_c_arith_p && b1 == opcode::op_c_load) { fused = opcode::op_c_arith_load; }
				else if (a1 == opcode::op_c_load && b1 == opcode::op_c_jcmp_p) { fused = opcode::op_c_load_jcmp; }
				else if (a1 == opcode::op_c_arith_p && b1 == opcode::op_c_jump) { fused = opcode::op_c_arith_jump; }
				if (fused != opcode::op_halt) { code[ip2].op = fused; ++ip2; }
			}
		}
		if (ir_passes_) {
			// jump threading: a block jump whose target is another unconditional block jump goes straight to that jump's target and takes
			// its budget poll with it (a nested loop's end-of-body jump onto its back-edge poll); the skipped jump stays for its other entries
			for (uint32_t ip2 = loop.copy_head; ip2 < loop.copy_back; ++ip2) {
				if (code[ip2].op != opcode::op_c_jump) continue;
				for (int hop = 0; hop < 8; ++hop) {
					const uint32_t to = code[ip2].a;
					if (to < loop.copy_head || to >= loop.copy_back || to == ip2 || code[to].op != opcode::op_c_jump) break;
					code[ip2].a = code[to].a;
					code[ip2].c |= code[to].c;
				}
			}
		}
		for (uint32_t k = 0; k < loop.cells.size(); ++k) {   // the landing's lists (row 156.2): the environment cells, and the slot cells a string may sit in
			const chunk::cell_loop::cell& cl = loop.cells[k];
			if (cl.env_operand != k_invalid_u32 || cl.iter_index || !(cl.temp || cl.first_decl)) { loop.entry_cells.push_back(k); }
			if (cl.env_operand != k_invalid_u32) { loop.env_cells.push_back(k); continue; }
			if (!cl.temp && (cl.kind == 0 || cl.kind == static_cast<uint8_t>(script_value::TYPEID_STRING))) { loop.string_candidates.push_back(k); }
		}
		for (const ret_pad& rp : ret_pads) {
			// the general return of a boxed result (a frame the raw protocol does not return from): its own statement for the trace
			code[rp.at].b = static_cast<uint32_t>(code.size());
			code.push_back(vm_instruction{opcode::op_return, 1, rp.proof, 0});
			c.stmt_nodes.push_back(rp.origin < c.stmt_nodes.size() ? c.stmt_nodes[rp.origin] : nullptr);
		}
		if (fn_mode) {
			// the frame program: a raw call enters it at copy_head with the parameters in their cells when nothing else needs reading at entry
			loop.frame_program = true;
			c.frame_program = loop_index;
			bool enterable = loop.env_cells.empty() && loop.sites.empty() && loop.inline_sites.empty() && loop.math_alias_sites.empty() && loop.rebinds.empty() && loop.iter_proto == k_invalid_u32;
			for (size_t i = 0; i < frame_param_kinds_.size(); ++i) {
				const uint32_t k = i < loop.slot_to_cell.size() ? loop.slot_to_cell[i] : k_invalid_u32;
				if (k == k_invalid_u32 || loop.cells[k].kind != frame_param_kinds_[i] || loop.cells[k].temp) { enterable = false; }
				loop.param_cells.push_back(k);
				loop.param_kinds |= static_cast<uint32_t>(frame_param_kinds_[i]) << (4 * i);
			}
			for (const uint32_t k : loop.entry_cells) { if (std::find(loop.param_cells.begin(), loop.param_cells.end(), k) == loop.param_cells.end()) { enterable = false; } }
			for (const auto& cl : loop.cells) { if (cl.kind == static_cast<uint8_t>(script_value::TYPEID_STRING)) { enterable = false; } }
			loop.raw_enterable = enterable;
		}
		if (line) {
			// the entry replaces the region's first instruction, which moves to the trampoline ahead of a jump to the next one
			loop.straight = true;
			loop.trampoline = static_cast<uint32_t>(code.size());
			const ast_node* sn = head < c.stmt_nodes.size() ? c.stmt_nodes[head] : nullptr;
			const chunk::typed_site site = head < c.typed_sites.size() ? c.typed_sites[head] : chunk::typed_site{};
			const uint32_t in = head < c.ident_nodes.size() ? c.ident_nodes[head] : k_invalid_u32;
			const vm_instruction replaced = code[head];
			code.push_back(replaced); c.stmt_nodes.push_back(sn);
			code.push_back(vm_instruction{opcode::op_jump, static_cast<uint32_t>(head + 1), 0, 0}); c.stmt_nodes.push_back(sn);
			if (head < c.typed_sites.size()) { chunk_ = &c; typed_site_at(loop.trampoline) = site; }
			if (in != k_invalid_u32) { if (c.ident_nodes.size() <= loop.trampoline) { c.ident_nodes.resize(loop.trampoline + 1, k_invalid_u32); } c.ident_nodes[loop.trampoline] = in; }
			code[head] = vm_instruction{opcode::op_cells_enter, loop_index, loop.trampoline, 0};
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
	    (quiet_partner(b->left.get()) || env_identifier(b->left.get())) && fusable_index_expr(b->right.get())) return true;
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
	return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none || member_temp_operand(e) || ternary_temp_operand(e);   // a member read or a ternary is hoisted in order like a subscript
}

// an identifier that is not a frame slot resolves in the environment inside a fused op, as a lifted binary's environment
// operand does; only an undefined name could observe that it resolves after an inner read (the accepted exception)
bool vm_compiler::env_identifier(const expression* e) {
	if (!e || e->get_type() != node_type::identifier_expr) return false;
	auto* id = static_cast<identifier_expr*>(const_cast<expression*>(e));
	return !id->global_qualified && identifier_slot_operand(id) == k_invalid_u32;
}

// a declared type whose slot holds a primitive at every point: a unary or an intrinsic over such a slot runs no user operator or conversion
static bool primitive_declared_type(const type_info* t) {
	return t && (t->base_type == script_value_type::jai_int_type || t->base_type == script_value_type::jai_float_type || t->base_type == script_value_type::jai_bool_type);
}

// a frame slot no user code can rebind: a value declaration (not a reference) that no reference or capture reaches; it may
// stay an in-op operand ahead of any hoisted read (design_row118 section 3: an operand resolved inside its operation only
// when nothing evaluated after it in the source can run user code)
bool vm_compiler::private_local(const expression* e) {
	if (!e || e->get_type() != node_type::identifier_expr) return false;
	auto* id = static_cast<identifier_expr*>(const_cast<expression*>(e));
	if (id->global_qualified || !id->names_value_decl) return false;
	const uint32_t slot = identifier_slot_operand(id);
	return slot != k_invalid_u32 && callable_.escaping.count(slot) == 0;
}

// the one route by which user code reaches a private local: a call that takes it as a direct argument (a reference parameter binds the
// slot) or as a method's receiver (an in-place mutator); a math intrinsic takes its arguments by value
bool vm_compiler::slot_argument_in(const expression* e, uint32_t slot) {
	if (!e || slot == k_invalid_u32) return false;
	auto names = [&](const expression* a) {
		return a && a->get_type() == node_type::identifier_expr && identifier_slot_operand(static_cast<identifier_expr*>(const_cast<expression*>(a))) == slot;
	};
	switch (e->get_type()) {
	case node_type::binary_expr: { auto* b = static_cast<const binary_expr*>(e); return slot_argument_in(b->left.get(), slot) || slot_argument_in(b->right.get(), slot); }
	case node_type::unary_expr: return slot_argument_in(static_cast<const unary_expr*>(e)->operand.get(), slot);
	case node_type::cast_expr: return slot_argument_in(static_cast<const cast_expr*>(e)->operand.get(), slot);
	case node_type::member_expr: return slot_argument_in(static_cast<const member_expr*>(e)->object.get(), slot);
	case node_type::ternary_expr: {
		auto* t = static_cast<const ternary_expr*>(e);
		return slot_argument_in(t->condition.get(), slot) || slot_argument_in(t->then_expression.get(), slot) || slot_argument_in(t->else_expression.get(), slot);
	}
	case node_type::call_expr: {
		auto* c = static_cast<const call_expr*>(e);
		const bool method = c->callee && c->callee->get_type() == node_type::member_expr;
		const bool intrinsic = method && detail::math_intrinsic_for_call(static_cast<const member_expr*>(c->callee.get())) != detail::math_fn::none;
		if (method && !intrinsic && names(static_cast<const member_expr*>(c->callee.get())->object.get())) return true;
		for (const auto& a : c->arguments) { if ((!intrinsic && names(a.get())) || slot_argument_in(a.get(), slot)) return true; }
		return slot_argument_in(c->callee.get(), slot);
	}
	default: return false;
	}
}

// an identifier resolved inside its operation after `later` computes into temps: taken first when `later` may reach it
bool vm_compiler::read_ahead_of(const expression* operand, const expression* later) {
	if (!operand || operand->get_type() != node_type::identifier_expr) return false;
	return slot_argument_in(later, identifier_slot_operand(static_cast<identifier_expr*>(const_cast<expression*>(operand))));
}

bool vm_compiler::primitive_local(const expression* e) {
	if (!private_local(e)) return false;
	return callable_.primitive_slots.count(identifier_slot_operand(static_cast<identifier_expr*>(const_cast<expression*>(e)))) != 0;
}

// a hoisted read that cannot run user code: a subscript whose container is a private local (a reference or an environment
// name may reach a getter while the receiver resolves) and whose element class the checker proved (an array or a map, never
// a class instance with an operator[]) over a quiet index; a math intrinsic or a unary over a literal, a quiet read or a
// private local declared int, float or bool (a class in a var local could run a user conversion or operator)
bool vm_compiler::quiet_read(const expression* e) {
	if (!e) return false;
	if (fusable_index_expr(e)) {
		const auto* b = static_cast<const binary_expr*>(e);
		if (operand_expect(b) == 0xFF || !private_local(b->left.get())) return false;
		return quiet_index(b->right.get());
	}
	const expression* arg = nullptr;
	if (math_temp_operand(e) != detail::math_fn::none) {
		if (math_alias_call(e)) return false;   // the name may bind user code by run time
		arg = static_cast<const call_expr*>(e)->arguments[0].get();
	} else if (unary_temp_operand(e)) { arg = static_cast<const unary_expr*>(e)->operand.get(); }
	if (!arg) return false;
	return arg->get_type() == node_type::literal_expr || primitive_local(arg) || quiet_read(arg);
}

// a binary is never quiet: a custom numeric operator (engine::set_has_custom_numeric_operators) makes int + int user code,
// and the compiler holds no operator identity fact
bool vm_compiler::quiet_index(const expression* e) {
	if (!e) return false;
	return e->get_type() == node_type::literal_expr || private_local(e) || quiet_read(e);
}

// a subtree with a hoisted read that may run user code: the operands the language reads before it must not stay in place
bool vm_compiler::observing_hoist_in(const expression* e) {
	if (!e) return false;
	if (fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none || unary_temp_operand(e) || member_temp_operand(e) || ternary_temp_operand(e)) return !quiet_read(e);   // a ternary is never quiet: its condition may call
	if (e->get_type() != node_type::binary_expr) return false;
	const auto* b = static_cast<const binary_expr*>(e);
	return observing_hoist_in(b->left.get()) || observing_hoist_in(b->right.get());
}

// an environment name the unit does not declare as a value (another unit's global, a host name, a reference declaration at
// program scope such as `var& P = box.values`): its read may run a getter or raise, so it keeps its place ahead of any
// hoisted read; a value the unit declares dereferences a cell (the plain-reference proof's rule)
bool vm_compiler::env_reference_candidate(const expression* e) {
	if (!env_identifier(e)) return false;
	return !static_cast<identifier_expr*>(const_cast<expression*>(e))->names_known_value;
}

// a subtree with a hoisted read, intrinsic or unary of any quietness: it runs ahead of the operation's own operands
bool vm_compiler::hoist_in(const expression* e) {
	if (!e) return false;
	if (fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none || unary_temp_operand(e) || member_temp_operand(e) || ternary_temp_operand(e)) return true;
	if (e->get_type() != node_type::binary_expr) return false;
	const auto* b = static_cast<const binary_expr*>(e);
	return hoist_in(b->left.get()) || hoist_in(b->right.get());
}

// a value that lands in a compiler temp wherever a consumer wants an operand: an element read, a pure math call, a unary
bool vm_compiler::value_temp_operand(const expression* e) {
	return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none || unary_temp_operand(e) || member_temp_operand(e);
}

bool vm_compiler::compile_value_into_temp(const expression_ptr& e, uint8_t expect, fused_operand& out) {
	if (member_temp_operand(e.get())) { compile_member_into_temp(e, out); return true; }
	if (math_temp_operand(e.get()) != detail::math_fn::none) { compile_math_into_temp(e, out); return true; }
	if (unary_temp_operand(e.get())) { compile_unary_into_temp(e, out); return true; }
	if (ternary_temp_operand(e.get())) { compile_ternary_into_temp(e, out); return true; }
	return fusable_index_expr(e.get()) && compile_subscript_into_temp(e, expect, out);
}

// A call to math::itrunc, ifloor, floor, ceil, sqrt, abs, sin, cos, tan or a degree conversion with exactly one
// argument: the shared kernel evaluates it (detail/math_intrinsics.hpp) and its result lands in a compiler temp,
// where it is a direct operand of the binary around it (the checker classes the integer family int)
detail::math_fn vm_compiler::math_temp_operand(const expression* e) const {
	if (!typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body) return detail::math_fn::none;
	if (!e || e->get_type() != node_type::call_expr) return detail::math_fn::none;
	auto* call = static_cast<const call_expr*>(e);
	if (call->arguments.size() != 1 || !call->callee) return detail::math_fn::none;
	if (call->callee->get_type() == node_type::identifier_expr) return math_alias_operand(call);
	if (call->callee->get_type() != node_type::member_expr) return detail::math_fn::none;
	const detail::math_fn fn = detail::math_intrinsic_for_call(static_cast<const member_expr*>(call->callee.get()));
	switch (fn) {
	case detail::math_fn::itrunc: case detail::math_fn::ifloor: case detail::math_fn::floor_: case detail::math_fn::ceil_:
	case detail::math_fn::sqrt_: case detail::math_fn::abs_: case detail::math_fn::sin_: case detail::math_fn::cos_: case detail::math_fn::tan_:
	case detail::math_fn::radians_to_degrees: case detail::math_fn::degrees_to_radians:
		return fn;
	default: return detail::math_fn::none;
	}
}

// `sqrt(x)` naming a registered stdlib alias from outside the frame: a math temp whose op checks at run time that the name
// binds the registered builtin (the real call otherwise); an element or member argument keeps the general call, whose
// argument may bind a reference parameter of whatever the name binds
detail::math_fn vm_compiler::math_alias_operand(const call_expr* call) const {
	if (!engine_) return detail::math_fn::none;
	auto* id = static_cast<identifier_expr*>(call->callee.get());
	if (identifier_slot_operand(id) != k_invalid_u32) return detail::math_fn::none;
	const expression* arg = call->arguments[0].get();
	if (arg->get_type() != node_type::identifier_expr && jai::detail::is_ref_bindable_lvalue(arg)) return detail::math_fn::none;
	const uint8_t fn = engine_->math_alias_fn(id->symbol_id);
	return fn >= engine::k_math_alias_rows ? detail::math_fn::none : static_cast<detail::math_fn>(fn);
}

bool vm_compiler::math_alias_call(const expression* e) {
	return e && e->get_type() == node_type::call_expr && static_cast<const call_expr*>(e)->callee &&
	       static_cast<const call_expr*>(e)->callee->get_type() == node_type::identifier_expr;
}

// The intrinsic's argument as an operand (a slot, a literal, an element read or another intrinsic into a temp;
// anything else rides the stack), then op_math_temp lands the result in a fresh temp; `out` is that temp
// a unary minus, complement or not whose operand is an operand shape or lands in a temp: a definition into a compiler
// value wherever a consumer wants an operand (the lift, the flat pre-step, a math argument, a declaration, a store)
bool vm_compiler::unary_temp_operand(const expression* e) {
	if (!temps_active_ || !chunk_ || !chunk_->is_function_body) return false;
	if (!e || e->get_type() != node_type::unary_expr) return false;
	const auto* u = static_cast<const unary_expr*>(e);
	if (u->op.type != token_type::minus && u->op.type != token_type::tilde && u->op.type != token_type::bang) return false;
	const expression* o = u->operand.get();
	if (!o) return false;
	if (o->get_type() == node_type::identifier_expr || o->get_type() == node_type::literal_expr) return true;
	if (fusable_index_expr(o) || math_temp_operand(o) != detail::math_fn::none || unary_temp_operand(o)) return true;
	return o->get_type() == node_type::binary_expr && binary_lands_fused(const_cast<binary_expr*>(static_cast<const binary_expr*>(o)));
}

// `c ? a : b` as an operand (an absolute value `(g < 0.0 ? -g : g) - k`, a clamp): the condition's jumps, then each arm's value
// moved into one compiler temp, so the binary around it keeps a direct operand and a block takes the arms as moves; every arm lands
// by the same move (a computed arm into a temp of its own first), so a block holds the temp as one cell whatever the arms' kinds
bool vm_compiler::ternary_temp_operand(const expression* e) {
	if (!typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body) return false;
	if (!e || e->get_type() != node_type::ternary_expr) return false;
	const auto* t = static_cast<const ternary_expr*>(e);
	auto arm_ok = [this](const expression* a) {
		if (!a) return false;
		if (a->get_type() == node_type::literal_expr) return true;
		if (a->get_type() == node_type::identifier_expr) return !reference_slot_operand(a);
		if (a->get_type() == node_type::ternary_expr) return ternary_temp_operand(a);
		if (member_temp_operand(a) || math_temp_operand(a) != detail::math_fn::none || unary_temp_operand(a)) return true;
		return a->get_type() == node_type::binary_expr && binary_lands_fused(const_cast<binary_expr*>(static_cast<const binary_expr*>(a)));
	};
	return t->condition && arm_ok(t->then_expression.get()) && arm_ok(t->else_expression.get());
}

void vm_compiler::compile_ternary_into_temp(const expression_ptr& e, fused_operand& out) {
	const uint32_t dst = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	land_ternary_in_temp(static_cast<const ternary_expr*>(e.get()), dst);
	out = fused_operand{};
	out.slot = dst;
	out.symbol = temp_symbol_index();
	out.direct = true;
}

void vm_compiler::land_ternary_in_temp(const ternary_expr* t, uint32_t dst) {
	std::vector<size_t> false_jumps;
	const size_t mark = temp_next_;
	compile_branch_condition(t->condition, false_jumps);
	temp_next_ = mark;   // the condition's temps die at its jumps
	land_arm_in_temp(t->then_expression, dst);
	const size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
	for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
	land_arm_in_temp(t->else_expression, dst);
	patch_jump(jump_end, chunk_->code.size());
}

void vm_compiler::land_arm_in_temp(const expression_ptr& arm, uint32_t dst) {
	if (arm->get_type() == node_type::ternary_expr) { land_ternary_in_temp(static_cast<const ternary_expr*>(arm.get()), dst); return; }
	const size_t mark = temp_next_;
	fused_binary_proto p;
	if (arm->get_type() == node_type::identifier_expr || arm->get_type() == node_type::literal_expr) {
		p.left = make_fused_operand(arm);
	} else if (arm->get_type() == node_type::binary_expr) {
		compile_expression(arm);
		const uint32_t t0 = static_cast<uint32_t>(temp_next_);
		++temp_next_;
		if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
		vm_instruction& last = chunk_->code.back();
		if (last.op == opcode::op_binary_fused) { last.op = opcode::op_binary_fused_temp; last.b = t0; }
		else { emit(opcode::op_call_temp, t0); }   // unreachable by construction (binary_lands_fused held): the value is on the stack
		p.left = fused_operand{};
		p.left.slot = t0;
		p.left.symbol = temp_symbol_index();
		p.left.direct = true;
	} else {
		compile_value_into_temp(arm, 0xFF, p.left);
	}
	chunk_->fused_binary_protos.push_back(p);
	emit(opcode::op_operand_temp, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), dst, 0);
	temp_next_ = mark;
}

void vm_compiler::compile_unary_into_temp(const expression_ptr& e, fused_operand& out) {
	auto* u = static_cast<unary_expr*>(e.get());
	const expression_ptr& arg = u->operand;
	fused_binary_proto p;
	bool operand = true;
	if (arg->get_type() == node_type::identifier_expr || arg->get_type() == node_type::literal_expr) {
		p.left = make_fused_operand(arg);
	} else if (math_temp_operand(arg.get()) != detail::math_fn::none) {
		compile_math_into_temp(arg, p.left);
	} else if (unary_temp_operand(arg.get())) {
		compile_unary_into_temp(arg, p.left);
	} else if (member_temp_operand(arg.get())) {
		compile_member_into_temp(arg, p.left);
	} else if (!(fusable_index_expr(arg.get()) && compile_subscript_into_temp(arg, 0xFF, p.left))) {
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
	emit(opcode::op_unary_temp, a, t, static_cast<uint32_t>(u->op.type));
	out = fused_operand{};
	out.slot = t;
	out.symbol = temp_symbol_index();
	out.direct = true;
}

void vm_compiler::compile_math_into_temp(const expression_ptr& e, fused_operand& out) {
	auto* call = static_cast<call_expr*>(e.get());
	const detail::math_fn fn = math_temp_operand(e.get());
	const expression_ptr& arg = call->arguments[0];
	uint32_t alias_site = k_invalid_u32, alias_temp = k_invalid_u32;
	uint32_t args_begin = 0;
	if (auto pin = alias_pins_.find(call); pin != alias_pins_.end()) {   // a guard above a graft resolved the callee in its place
		alias_site = pin->second.first; alias_temp = pin->second.second;
		alias_pins_.erase(pin);
		args_begin = static_cast<uint32_t>(chunk_->code.size());
	} else if (math_alias_call(e.get())) {
		// the real call's site, as compile_call builds it; an argument evaluated ahead of the op that could raise or run user
		// code needs the callee resolved first, at the guard
		call_site site;
		site.call_node = add_node(e);
		site.callee = make_fused_operand(call->callee);
		if (arg->get_type() == node_type::identifier_expr) {
			auto* arg_ident = static_cast<identifier_expr*>(arg.get());
			site.arg_symbols.push_back(arg_ident->symbol_id);
			site.arg_slots.push_back(identifier_slot_operand(arg_ident));
			if (arg_ident->global_qualified) { site.arg_global.assign(1, 1); }
		} else {
			site.arg_symbols.push_back(UINT64_MAX);
			site.arg_slots.push_back(k_invalid_u32);
		}
		chunk_->call_sites.push_back(std::move(site));
		alias_site = static_cast<uint32_t>(chunk_->call_sites.size() - 1);
		const bool in_op = arg->get_type() == node_type::identifier_expr || arg->get_type() == node_type::literal_expr || quiet_read(arg.get());
		if (!in_op) {
			alias_temp = static_cast<uint32_t>(temp_next_++);
			if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
			emit(opcode::op_math_guard, alias_site, alias_temp, static_cast<uint32_t>(fn));
		}
		args_begin = static_cast<uint32_t>(chunk_->code.size());
	}
	fused_binary_proto p;
	bool operand = true;
	if (arg->get_type() == node_type::identifier_expr || arg->get_type() == node_type::literal_expr) {
		p.left = make_fused_operand(arg);
	} else if (math_temp_operand(arg.get()) != detail::math_fn::none) {
		compile_math_into_temp(arg, p.left);
	} else if (unary_temp_operand(arg.get())) {
		compile_unary_into_temp(arg, p.left);
	} else if (member_temp_operand(arg.get())) {
		compile_member_into_temp(arg, p.left);
	} else if (ternary_temp_operand(arg.get())) {
		compile_ternary_into_temp(arg, p.left);
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
	if (alias_site != k_invalid_u32) {
		const uint32_t t = alias_temp != k_invalid_u32 ? alias_temp : static_cast<uint32_t>(temp_next_++);
		if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
		if (chunk_->code.size() > args_begin) { chunk_->call_arg_zones.emplace_back(args_begin, static_cast<uint32_t>(chunk_->code.size())); }
		emit(opcode::op_math_temp, a, t, static_cast<uint32_t>(fn) | (alias_temp != k_invalid_u32 ? 0x80u : 0u) | ((alias_site + 1) << 8));
		emit(opcode::op_call_temp, t);
		out = fused_operand{};
		out.slot = t;
		out.symbol = temp_symbol_index();
		out.direct = true;
		return;
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

// A container ahead of a hoisted read: the guard (op_operand_temp, c = 1) materializes a reference-slot container
// into `guard` only when the slot's reference is not a plain cell, so a getter runs once and in its place; the
// always form (c = 2) takes the container's value every time, ahead of an index that may run user code which
// could rebind the container (design_row118 section 3); either way the read's container operand stays the
// original one, so a region's site is keyed on it, and the readers take the temp when the guard filled it
void vm_compiler::guard_container_temp(const expression_ptr& e, fused_operand& container, uint32_t& guard, bool always) {
	fused_binary_proto p;
	p.left = make_fused_operand(e);
	chunk_->fused_binary_protos.push_back(std::move(p));
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	emit(opcode::op_operand_temp, static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1), t, always ? 2u : 1u);
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

// A member read or store whose base is a frame-slot identifier (`p.x`, `p` a local or a parameter; never `::name`, a static, a
// null-safe access or an environment name): the base's slot, so the read lands in a compiler temp and the store reads the
// base from the slot. Function bodies with the typed lowering only, like the other temps.
uint32_t vm_compiler::member_base_slot(const expression* e) const {
	if (!typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body) return k_invalid_u32;
	if (!e || e->get_type() != node_type::member_expr) return k_invalid_u32;
	const auto* m = static_cast<const member_expr*>(e);
	if (m->is_static || m->null_safe || !m->object || m->object->get_type() != node_type::identifier_expr) return k_invalid_u32;
	auto* id = static_cast<identifier_expr*>(m->object.get());
	if (id->global_qualified) return k_invalid_u32;
	const uint32_t slot = const_cast<vm_compiler*>(this)->identifier_slot_operand(id);
	return slot != k_invalid_u32 && slot < k_typed_slot_mask ? slot : k_invalid_u32;
}
bool vm_compiler::member_temp_operand(const expression* e) const { return member_base_slot(e) != k_invalid_u32 || size_temp_operand(e) || elem_member_operand(e); }
// `a[i].m` inside a loop over an array the checker proved holds class instances, `a` a frame slot: the element lands in an alias temp
// and the member read in a member temp over it (a block takes the pair as an element alias's rebind)
// the alias temp of an element member read: above every temp the function has used so far, so no earlier temp of the loop (a head's
// size, another statement's value) holds a cell in its slot (a block takes one alias per slot)
uint32_t vm_compiler::elem_alias_temp() {
	const uint32_t at = static_cast<uint32_t>(std::max(temp_next_, temp_high_));
	temp_next_ = at + 1;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	return at;
}
bool vm_compiler::elem_member_operand(const expression* e) const {
	if (!typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body || loops_.empty()) return false;
	if (!e || e->get_type() != node_type::member_expr) return false;
	const auto* m = static_cast<const member_expr*>(e);
	if (m->is_static || m->null_safe || !m->object || m->object->get_type() != node_type::binary_expr) return false;
	const auto* b = static_cast<const binary_expr*>(m->object.get());
	if (b->op.type != token_type::left_bracket || b->static_operand_class != operand_class_object_element || !b->left || b->left->get_type() != node_type::identifier_expr) return false;
	auto* id = static_cast<identifier_expr*>(b->left.get());
	if (id->global_qualified) return false;
	const uint32_t slot = const_cast<vm_compiler*>(this)->identifier_slot_operand(id);
	return slot != k_invalid_u32 && slot < k_typed_slot_mask;
}
// s.size() / s.length() on a receiver the checker proved (a string, an array or a map) held in a frame slot: a value that lands in a temp
bool vm_compiler::size_temp_operand(const expression* e) const {
	if (!builtins_ || !typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body) return false;
	if (!e || e->get_type() != node_type::call_expr) return false;
	const auto* call = static_cast<const call_expr*>(e);
	if (call->builtin_receiver == 0 || !call->arguments.empty() || !call->callee || call->callee->get_type() != node_type::member_expr) return false;
	const auto* m = static_cast<const member_expr*>(call->callee.get());
	if (m->is_static || m->null_safe || !m->object || m->object->get_type() != node_type::identifier_expr) return false;
	if (m->member != "size" && m->member != "length") return false;
	auto* id = static_cast<identifier_expr*>(m->object.get());
	if (id->global_qualified) return false;
	const uint32_t slot = const_cast<vm_compiler*>(this)->identifier_slot_operand(id);
	return slot != k_invalid_u32 && slot < k_typed_slot_mask;
}
// The member read lands in a fresh compiler temp (op_member_temp: op_get_member's kernel, the value into the temp); `out`
// becomes the direct temp operand. The read keeps its place: it is emitted where the operand is evaluated.
void vm_compiler::compile_member_into_temp(const expression_ptr& e, fused_operand& out) {
	if (size_temp_operand(e.get())) {
		// the size call's site (a slot receiver, the builtin indexes), its value into a temp
		const auto call = std::static_pointer_cast<call_expr>(e);
		const auto member = std::static_pointer_cast<member_expr>(call->callee);
		auto* recv = static_cast<identifier_expr*>(member->object.get());
		call_site site;
		site.member_node = add_node(member);
		site.call_node = add_node(call);
		site.receiver_symbol = recv->symbol_id;
		site.receiver_slot = identifier_slot_operand(recv);
		auto builtin_index_for = [&](const std::unordered_map<uint64_t, uint16_t>& index) -> uint16_t {
			auto it = index.find(member->member_id);
			return it != index.end() ? it->second : builtin_method_registries::k_no_builtin;
		};
		site.bi_array = builtin_index_for(builtins_->array_index);
		site.bi_map = builtin_index_for(builtins_->map_index);
		site.bi_string = builtin_index_for(builtins_->string_index);
		chunk_->builtin_indexed = true;
		chunk_->call_sites.push_back(std::move(site));
		const uint32_t t = static_cast<uint32_t>(temp_next_);
		++temp_next_;
		if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
		emit(opcode::op_size_temp, t, static_cast<uint32_t>(chunk_->call_sites.size() - 1), call->builtin_receiver);
		out = fused_operand{};
		out.slot = t;
		out.symbol = temp_symbol_index();
		out.direct = true;
		return;
	}
	if (elem_member_operand(e.get())) {
		// the subscript as the member read evaluates it, the element into the alias temp, the member read over the alias
		compile_expression(std::static_pointer_cast<member_expr>(e)->object);
		const uint32_t at = elem_alias_temp();
		const uint32_t t = static_cast<uint32_t>(temp_next_++);
		if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
		emit(opcode::op_call_temp, at);
		emit(opcode::op_member_temp, add_node(e), t, at);
		out = fused_operand{};
		out.slot = t;
		out.symbol = temp_symbol_index();
		out.direct = true;
		return;
	}
	const uint32_t base = member_base_slot(e.get());
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	emit(opcode::op_member_temp, add_node(e), t, base);
	out = fused_operand{};
	out.slot = t;
	out.symbol = temp_symbol_index();
	out.direct = true;
}

// a call the expression consumes as an operand: a plain-name callee (never a member call, never the reflective or parallel
// intrinsics), its result landed in a compiler temp by op_call_temp, so the consumer takes a slot operand and a block's door lands
// the result in a temp cell. The call runs ahead of its partner, so the partner must be one the call cannot change: a literal or
// a private local (a slot no user code can rebind)
bool vm_compiler::call_temp_operand(const expression* e) const {
	if (!typed_lowering_ || !temps_active_ || !chunk_ || !chunk_->is_function_body) return false;
	if (!e || e->get_type() != node_type::call_expr) return false;
	const auto* call = static_cast<const call_expr*>(e);
	if (!call->callee || call->callee->get_type() != node_type::identifier_expr) return false;
	const auto* ident = static_cast<const identifier_expr*>(call->callee.get());
	if (ident->name == "weak_from_this" || ident->name == "shared_from_this" || ident->name == "__jai_parallel_advance" || ident->name == "__jai_parallel_element") return false;
	return call->arguments.size() <= 8;
}
bool vm_compiler::call_partner_ok(const expression* e, const expression* call) {
	if (!e) return false;
	if (e->get_type() == node_type::literal_expr) return true;
	if (frame_eligible_ && raw_call_operand(e) && raw_call_operand(call)) return true;   // two raw calls in a frame program: both land in temps in source order, and the binary reads the temps
	if (e->get_type() != node_type::identifier_expr || !private_local(e)) return false;
	// a partner the call (or a call among its arguments) takes as an argument can be bound by reference and written before the binary reads
	// it (`x + mutate(x)`, `x + f(mutate(x))`, mutate(int& v)): the plain binary's reads are left to right, so such a call keeps the stack form
	return !read_ahead_of(e, call);
}
// a call a frame program can make raw: a hoistable call whose checked callee has the raw signature
bool vm_compiler::raw_call_operand(const expression* e) const {
	if (!call_temp_operand(e)) return false;
	const auto* call = static_cast<const call_expr*>(e);
	const auto decl = call->proven_callee_decl.lock();
	return decl && raw_signature(decl.get()) && call->arguments.size() == decl->parameters.size() && raw_call_reaches_program(decl.get(), callable_body_);
}
void vm_compiler::compile_call_into_temp(const expression_ptr& e, fused_operand& out) {
	compile_call(std::static_pointer_cast<call_expr>(e));
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	if (typed_lowering_ && t < k_typed_slot_mask && !chunk_->code.empty() && chunk_->code.back().op == opcode::op_call_from_scratch && chunk_->code.back().c == 0) {
		typed_site_at(chunk_->code.size() - 1).operands[0] = t | (k_typed_dst_temp << k_typed_dst_shift);
		chunk_->code.back().op = opcode::op_call_typed;
	}
	emit(opcode::op_call_temp, t);
	out = fused_operand{};
	out.slot = t;
	out.symbol = temp_symbol_index();
	out.direct = true;
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
	    (quiet_partner(expr->left.get()) || env_identifier(expr->left.get())) && typed_lowering_ && temps_active_ && chunk_->is_function_body) {
		fused_operand inner;
		fused_operand container;
		const bool ref_container = reference_slot_operand(expr->left.get());
		// the outer binding is read before the inner read in the language's order: ahead of an inner read that may run user
		// code its value is taken (a reference too: the callback may rebind what it names), ahead of a quiet one a reference
		// is dereferenced (its getter); a private local needs neither
		const bool snap_container = (!quiet_read(expr->right.get()) && !private_local(expr->left.get())) || env_identifier(expr->left.get());   // an environment name reads first either way (its binding may be a reference to a property by run time: its getter, or its error, precedes the inner read), through the always form, whose temp is a value at its own point
		uint32_t guard = k_invalid_u32;
		if (snap_container) { guard_container_temp(expr->left, container, guard, true); }
		else if (ref_container) { guard_container_temp(expr->left, container, guard, false); }
		if (compile_subscript_into_temp(expr->right, operand_expect(static_cast<const binary_expr*>(expr->right.get())), inner)) {
			fused_index_proto p;
			p.container = (ref_container || snap_container) ? container : make_fused_operand(expr->left);
			p.container_guard = guard;
			p.guard_always = snap_container;
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
	// Computed index on an ident container, or on a member container `base.member[i op j]` (the member resolves inside the
	// op as the simple-index shape does, base then member then index): a left-spine arithmetic chain runs through the
	// fused op (raw int fold / verbatim boxed replay); the innermost node must be the flat fused-binary shape
	member_expr* computed_mc = fusable_member_container(expr->left.get());
	if (expr->left->get_type() == node_type::identifier_expr || computed_mc) {
		if (binary_expr* rb = index_binary_candidate(expr->right.get())) {
			binary_expr* spine[8];
			const int depth = index_chain_spine(rb, spine);
			if (depth > 0 && binary_shape(spine[depth - 1]) != binary_shape_none) {
				fused_index_proto p;
				if (computed_mc) { p.container = make_fused_operand(computed_mc->object); p.container.member_node = add_node(expr->left); }
				else { p.container = make_fused_operand(expr->left); }
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
			if (!computed_mc && depth == 1 && typed_lowering_ && temps_active_ && chunk_->is_function_body && quiet_partner(expr->left.get()) && index_read_partner_shape(rb)) {
				// the element-read operand lands in a temp first (left before right); the index binary then reads
				// direct operands and the outer read takes the T_LOAD row when the binary's class is proven
				fused_operand container;
				const bool ref_container = reference_slot_operand(expr->left.get());
				const bool snap_container = (observing_hoist_in(rb) && !private_local(expr->left.get())) || env_identifier(expr->left.get());   // read before the index's reads; a reference container's value too; an environment name reads first either way, through the always form
				uint32_t guard = k_invalid_u32;
				if (snap_container) { guard_container_temp(expr->left, container, guard, true); }
				else if (ref_container) { guard_container_temp(expr->left, container, guard, false); }   // its dereference precedes quiet reads
				fused_binary_proto bp;
				bp.op = static_cast<uint8_t>(rb->op.type);
				auto side = [&](const expression_ptr& e, fused_operand& out, bool ahead_of_a_read) {
					if (fusable_index_expr(e.get())) {
						if (compile_subscript_into_temp(e, operand_expect(static_cast<const binary_expr*>(e.get())), out)) { bp.elem_read_operand = true; }
					} else if (ahead_of_a_read && (reference_slot_operand(e.get()) || (e->get_type() == node_type::identifier_expr && !private_local(e.get()) && !quiet_read(rb->right.get())))) {
						materialize_operand_temp(e, out);   // its value precedes a read that may run user code
					} else {
						out = make_fused_operand(e);
					}
				};
				side(rb->left, bp.left, fusable_index_expr(rb->right.get()));
				side(rb->right, bp.right, false);
				stamp_typed_class(bp, rb);
				fused_index_proto p;
				p.container = (ref_container || snap_container) ? container : make_fused_operand(expr->left);
				p.container_guard = guard;
				p.guard_always = snap_container;
				chunk_->fused_binary_protos.push_back(bp);
				p.index_binary = static_cast<uint32_t>(chunk_->fused_binary_protos.size() - 1);
				chunk_->fused_index_protos.push_back(p);
				emit(opcode::op_index_fused, static_cast<uint32_t>(chunk_->fused_index_protos.size() - 1), flags);
				return true;
			}
			if (!computed_mc && quiet_partner(expr->left.get()) && binary_lands_fused(rb)) {
				// an arithmetic tree as the index: compile_binary emits one fused binary last, rewritten to land in a
				// fresh temp, and the read is the ident-plus-temp shape (the T_LOAD row); a reference-slot container
				// is dereferenced ahead of the index (the interpreter's container-before-index order)
				const size_t temp_mark = temp_next_;
				fused_operand container;
				const bool snap_container = !private_local(expr->left.get());   // the tree holds a binary (a custom numeric operator is user code): the container's value precedes it
				uint32_t guard = k_invalid_u32;
				if (snap_container) { guard_container_temp(expr->left, container, guard, true); }
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
				p.container = snap_container ? container : make_fused_operand(expr->left);
				p.container_guard = guard;
				p.guard_always = snap_container;
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
	       fusable_subscript_operand(e) || (typed_lowering_ && fusable_index_expr(e)) || math_temp_operand(e) != detail::math_fn::none || unary_temp_operand(e) || member_temp_operand(e) || ternary_temp_operand(e);   // a subscript only a temp carries needs the typed lowering (compile_subscript_into_temp)
}

// a subscript only a temp can carry (not a chain operand) or an intrinsic needs a partner that cannot raise first;
// the rule holds at every node of the lifted tree, the outer one included
bool vm_compiler::lift_temp_only(const expression* c) {
	return (fusable_index_expr(c) && !fusable_subscript_operand(c)) || math_temp_operand(c) != detail::math_fn::none || unary_temp_operand(c) || member_temp_operand(c) || ternary_temp_operand(c);   // a member read or a ternary lands in a temp: its partner must be quiet or lifted
}

// a partner that is itself a value binary is quiet here: the lift computes it into its temp in operand order,
// so nothing of it runs ahead of the hoisted read (the flat pre-step keeps the strict rule: it resolves in-op)
static bool lifted_partner(const expression* e) {
	if (e->get_type() != node_type::binary_expr) return false;
	const auto* b = static_cast<const binary_expr*>(e);
	return b->op.type != token_type::left_bracket && detail::transient_read_marker::is_value_binary_op(b->op.type);
}

bool vm_compiler::lift_partners_ok(const binary_expr* b) {
	return !((lift_temp_only(b->left.get()) && !quiet_partner(b->right.get()) && !lifted_partner(b->right.get()) && !env_identifier(b->right.get())) ||
	         (lift_temp_only(b->right.get()) && !quiet_partner(b->left.get()) && !lifted_partner(b->left.get()) && !env_identifier(b->left.get())));   // an environment name on the left reads into a temp first (sequence_left)
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
	auto hoists = [this](const expression* e) { return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none || unary_temp_operand(e) || member_temp_operand(e) || ternary_temp_operand(e); };
	const bool lsub = (hoists(b->left.get()) && (quiet_partner(b->right.get()) || env_identifier(b->right.get()))) || (call_temp_operand(b->left.get()) && call_partner_ok(b->right.get(), b->left.get()));
	const bool rsub = (hoists(b->right.get()) && (quiet_partner(b->left.get()) || env_identifier(b->left.get()))) || (call_temp_operand(b->right.get()) && call_partner_ok(b->left.get(), b->right.get()));   // an environment name on the left of a hoisted read reads into a temp first
	if ((lsub && (rsub || plain(b->right.get()))) || (rsub && (lsub || plain(b->left.get())))) return true;
	if (binary_shape(b) != binary_shape_none) return true;
	const int ln = lift_fusible_nodes(b->left.get());
	const int rn = ln >= 0 ? lift_fusible_nodes(b->right.get()) : -1;
	return lift_partners_ok(b) && ln >= 0 && rn >= 0 && ln + rn >= 1 && ln + rn <= 16;
}

// an element read or a call's reference result pushed as a reference whose consumer reads it only after a later operand that may run user
// code: its value is taken now, as the interpreter reads an operand before evaluating the next
void vm_compiler::deref_ahead_of(const expression* pushed, const expression* later) {
	if (!pushed || detail::transient_read_marker::callout_free(later)) return;
	if (pushed->get_type() == node_type::binary_expr) {
		auto* b = static_cast<const binary_expr*>(pushed);
		if (b->op.type != token_type::left_bracket || b->transient_element_read) return;
	} else if (pushed->get_type() == node_type::call_expr) {
		auto* c = static_cast<const call_expr*>(pushed);
		if (c->callee && c->callee->get_type() == node_type::member_expr && detail::math_intrinsic_for_call(static_cast<const member_expr*>(c->callee.get())) != detail::math_fn::none) return;
		if (auto fn = c->proven_callee_decl.lock(); fn && !(fn->return_type && fn->return_type->is_reference())) return;   // a value return
	} else {
		return;
	}
	emit(opcode::op_deref_top);
}

// argument k's value is taken now when a later argument may write what it read (the stamp's kernel); a private local proven
// primitive loads as a plain value and needs nothing
size_t vm_compiler::emit_arg_capture(const std::vector<uint8_t>& captures, size_t k, const expression* arg, uint32_t where, uint32_t form) {
	if (k >= captures.size() || captures[k] == 0 || primitive_local(arg)) return SIZE_MAX;
	return emit(opcode::op_capture_arg, where, static_cast<uint32_t>(k), captures[k] | (form << 2));
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

	if (expr->op.type == token_type::plus) {
		// a left-deep `+` chain from a string literal (a template string): the literal, then each operand and its append, the
		// order the chain evaluates in
		std::vector<const binary_expr*> chain;
		const expression* e = expr.get();
		while (e->get_type() == node_type::binary_expr && static_cast<const binary_expr*>(e)->op.type == token_type::plus) {
			chain.push_back(static_cast<const binary_expr*>(e));
			e = static_cast<const binary_expr*>(e)->left.get();
		}
		if (chain.size() >= 2 && e->get_type() == node_type::literal_expr &&
		    static_cast<const literal_expr*>(e)->value.raw_storage_index() == script_value::TYPEID_STRING) {
			compile_expression(chain.back()->left);
			for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
				compile_expression((*it)->right);
				emit(opcode::op_concat_append);
			}
			return;
		}
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
		auto hoists = [this](const expression* e) { return fusable_index_expr(e) || math_temp_operand(e) != detail::math_fn::none || unary_temp_operand(e) || member_temp_operand(e) || ternary_temp_operand(e); };
		const bool lsub = (hoists(expr->left.get()) && (quiet_partner(expr->right.get()) || env_identifier(expr->right.get()))) || (call_temp_operand(expr->left.get()) && call_partner_ok(expr->right.get(), expr->left.get()));
		const bool rsub = (hoists(expr->right.get()) && (quiet_partner(expr->left.get()) || env_identifier(expr->left.get()))) || (call_temp_operand(expr->right.get()) && call_partner_ok(expr->left.get(), expr->right.get()));
		if ((lsub && (rsub || plain(expr->right.get()))) || (rsub && (lsub || plain(expr->left.get())))) {
			const size_t temp_mark = temp_next_;
			// the binary's class when the checker proved the pair, else the read's own proven element kind
			auto expect_of = [&](const expression_ptr& sub) {
				const uint8_t e = operand_expect(expr.get());
				return e != 0xFF ? e : operand_expect(static_cast<const binary_expr*>(sub.get()));
			};
			auto hoist_into_temp = [&](const expression_ptr& side, fused_operand& out) {
				if (math_temp_operand(side.get()) != detail::math_fn::none) { compile_math_into_temp(side, out); }
				else if (unary_temp_operand(side.get())) { compile_unary_into_temp(side, out); }
				else if (member_temp_operand(side.get())) { compile_member_into_temp(side, out); }
				else if (ternary_temp_operand(side.get())) { compile_ternary_into_temp(side, out); }
				else if (call_temp_operand(side.get())) { compile_call_into_temp(side, out); }
				else { compile_subscript_into_temp(side, expect_of(side), out); }
			};
			fused_binary_proto proto;
			proto.op = static_cast<uint8_t>(expr->op.type);
			if (lsub) { hoist_into_temp(expr->left, proto.left); }
			else if (reference_slot_operand(expr->left.get()) || (rsub && expr->left->get_type() == node_type::identifier_expr && (env_identifier(expr->left.get()) || (!private_local(expr->left.get()) && !quiet_read(expr->right.get())) || read_ahead_of(expr->left.get(), expr->right.get())))) { materialize_operand_temp(expr->left, proto.left); }   // its dereference, or its value, precedes the read
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
	// the left operand is read before the right in the language's order: an in-op subscript on the right may run user code
	// (an operator[] on a class instance, a getter behind the container) that rebinds a left identifier user code can reach,
	// so that identifier's value is taken ahead of the op (a private local needs no snapshot); without temps the general
	// pushes keep the order
	const bool snap_left_in_op = shape != binary_shape_none && fusable_subscript_operand(expr->right.get()) && expr->left->get_type() == node_type::identifier_expr && (env_identifier(expr->left.get()) || (!private_local(expr->left.get()) && !quiet_read(expr->right.get())));   // an environment binding may be a reference to a property by run time: it reads first ahead of the subscript either way
	if (shape != binary_shape_none && (!snap_left_in_op || (temps_active_ && chunk_->is_function_body))) {
		fused_binary_proto proto;
		proto.op = static_cast<uint8_t>(expr->op.type);
		if (snap_left_in_op) { materialize_operand_temp(expr->left, proto.left); } else { proto.left = make_fused_operand(expr->left); }
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
				const bool snap_left = l->get_type() == node_type::identifier_expr && ((!private_local(l) && observing_hoist_in(r)) || (env_identifier(l) && hoist_in(r)) || read_ahead_of(l, r));   // its value precedes a read in the right subtree that may run user code; an environment name that may be a property reference reads ahead of any hoisted read; a call to its right may take it by reference
				if (!ref_left && !snap_left && (!lo.is_subscript() || operand_shape(r))) { return; }
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
				if (unary_temp_operand(e.get())) { compile_unary_into_temp(e, o); return o; }
				if (member_temp_operand(e.get())) { compile_member_into_temp(e, o); return o; }   // a member read lands in a temp in operand order (partners_ok admitted it)
				if (ternary_temp_operand(e.get())) { compile_ternary_into_temp(e, o); return o; }
				if (call_temp_operand(e.get()) && call_partner_ok(partner, e.get())) { compile_call_into_temp(e, o); return o; }   // a call ahead of a partner it cannot change
				if (fusable_index_expr(e.get()) && (quiet_partner(partner) || lifted_partner(partner) || env_identifier(partner))) {   // an environment partner on the left read into a temp first (sequence_left)
					const uint8_t expect = operand_expect(b) != 0xFF ? operand_expect(b) : operand_expect(static_cast<const binary_expr*>(e.get()));
					if (compile_subscript_into_temp(e, expect, o)) { return o; }
				}
				return make_fused_operand(e);
			};
			std::function<fused_operand(const expression_ptr&)> side = [&](const expression_ptr& e) -> fused_operand {
				if (member_temp_operand(e.get())) { fused_operand o; compile_member_into_temp(e, o); return o; }   // a member leaf of a lifted subtree: its temp
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
	deref_ahead_of(expr->left.get(), expr->right.get());
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
	} else if (no_result && (unary_temp_operand(value.get()) || math_temp_operand(value.get()) != detail::math_fn::none)) {
		compile_value_into_temp(value, 0xFF, p.store_value);
		value_operand = true;   // `a[i] = -e` or `a[i] = math::f(e)`: the value lands in a temp
	} else if (no_result && value->get_type() == node_type::binary_expr && binary_lands_fused(static_cast<binary_expr*>(value.get()))) {
		// `a[i] = e op f`: the value binary lands in a compiler temp (its fused binary rewritten to land there, as an operand's
		// does) and the store takes the temp as an operand; a shape the fuse declined stays on the stack
		compile_expression(value);
		value_operand = true;   // compiled either way: never twice
		vm_instruction& last = chunk_->code.back();
		if (last.op == opcode::op_binary_fused) {
			const uint32_t t0 = static_cast<uint32_t>(temp_next_);
			++temp_next_;
			if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
			last.op = opcode::op_binary_fused_temp;
			last.b = t0;
			p.store_value = fused_operand{};
			p.store_value.slot = t0;
			p.store_value.symbol = temp_symbol_index();
			p.store_value.direct = true;
		}
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

// A compound's right-hand side that can neither raise nor write: evaluating it before or after the target observes the same
bool vm_compiler::compound_rhs_inert(const expression* e) {
	if (e->get_type() == node_type::literal_expr) { return true; }
	if (e->get_type() == node_type::identifier_expr) {
		const auto* id = static_cast<const identifier_expr*>(e);
		return id->slot_index != SIZE_MAX && id->names_value_decl && !id->global_qualified;
	}
	return false;
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
			emit_compound_store(add_symbol(ident->symbol_id), identifier_slot_operand(ident), kind, expr->value.get(), expr->static_operand_class);
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
			// C++17: the value first, then the container and the index (Dev ruling 2026-09-27); an inert value keeps the
			// target-first shape, which observes the same
			const bool value_first = !compound_rhs_inert(expr->value.get());
			if (value_first) { compile_expression(expr->value); }
			compile_expression(sub->left);
			compile_expression(sub->right);
			if (!value_first) { compile_expression(expr->value); }
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
			if (value_first) kind |= compound_flag_value_first;
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
				const uint32_t base_slot = member_base_slot(member.get());
				if (base_slot != k_invalid_u32) { emit(opcode::op_set_member_fused, add_node(member), expr->static_store_proof, base_slot); }   // the base from its slot: no LOAD dispatch
				else { compile_expression(member->object); emit(opcode::op_set_member, add_node(member), expr->static_store_proof); }
			}
		} else {
			// C++17: the right-hand side is sequenced before the target (Dev ruling 2026-09-27); a right side that can
			// neither raise nor write (a literal, a value local) keeps the target-first shape, which observes the same
			const bool value_first = !compound_rhs_inert(expr->value.get());
			if (value_first) { compile_expression(expr->value); }
			if (member->is_static) {
				emit(opcode::op_get_static, add_node(member));
			} else {
				// The receiver is evaluated once: op_dup keeps it under the read for the
				// write-back (a :: target resolves its class inside op_member_compound)
				compile_expression(member->object);
				emit(opcode::op_dup);
				emit(opcode::op_get_member, add_node(member));
			}
			if (!value_first) { compile_expression(expr->value); }
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
			if (value_first) kind |= compound_flag_value_first;
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
	chunk_->call_arg_zones.emplace_back(static_cast<uint32_t>(chunk_->code.size()), static_cast<uint32_t>(chunk_->code.size() + 1));   // the fused push reads its argument itself: a native exception there converts in this frame, as the unfused call's argument zone does
	emit(opcode::op_push_fused, static_cast<uint32_t>(chunk_->push_protos.size() - 1));
	return true;
}

void vm_compiler::compile_call(const std::shared_ptr<call_expr>& expr) {
	// Consumed HERE so nested arg calls never inherit the return-position hint
	const bool tail_site = pending_tail_call_;
	pending_tail_call_ = false;
	if (alias_pins_.count(expr.get())) {   // a stdlib alias whose guard resolved the callee ahead of a graft: the math row, its value pushed
		fused_operand t;
		compile_math_into_temp(expr, t);
		emit(opcode::op_load, t.slot, t.symbol, 0);
		return;
	}
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
				std::vector<uint8_t> captures;
				detail::transient_read_marker::stamp_arg_captures(expr->arguments, captures);
				for (size_t k = 0; k < expr->arguments.size(); ++k) {
					compile_expression(expr->arguments[k]);
					emit_arg_capture(captures, k, expr->arguments[k].get(), 0, capture_form_opaque);
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
			std::vector<uint8_t> captures;
			detail::transient_read_marker::stamp_arg_captures(expr->arguments, captures);
			std::vector<size_t> capture_ips;
			const uint32_t args_begin = static_cast<uint32_t>(chunk_->code.size());
			for (size_t k = 0; k < expr->arguments.size(); ++k) {
				const auto& arg = expr->arguments[k];
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
				if (const size_t ip = emit_arg_capture(captures, k, arg.get(), 0, capture_form_method); ip != SIZE_MAX) { capture_ips.push_back(ip); }
			}
			if (!expr->arguments.empty()) {
				chunk_->call_arg_zones.emplace_back(args_begin, static_cast<uint32_t>(chunk_->code.size()));
			}
			chunk_->call_sites.push_back(std::move(site));
			if (probe_ip != SIZE_MAX) {
				chunk_->code[probe_ip].a = static_cast<uint32_t>(chunk_->call_sites.size() - 1);
			}
			for (const size_t ip : capture_ips) { chunk_->code[ip].a = static_cast<uint32_t>(chunk_->call_sites.size() - 1); }
			uint32_t builtin_kind = 0;   // the checker proved the receiver and the name is that kind's builtin
			if (builtins_ && !member->null_safe && expr->builtin_receiver != 0) {
				const call_site& cs = chunk_->call_sites.back();
				bool args_callout_free = true;   // an identifier string receiver is re-read after the arguments: none may run code
				if (member->object->get_type() == node_type::identifier_expr) {
					for (const auto& arg : expr->arguments) { args_callout_free = args_callout_free && detail::transient_read_marker::callout_free(arg.get()); }
				}
				if (expr->builtin_receiver == 1 && cs.bi_string != builtin_method_registries::k_no_builtin &&
				    builtins_->string_mutates_by_index[cs.bi_string] == 0 && args_callout_free) { builtin_kind = 1; }   // a string mutator writes the variable: the general call
				else if (expr->builtin_receiver == 2 && cs.bi_array != builtin_method_registries::k_no_builtin) { builtin_kind = 2; }
				else if (expr->builtin_receiver == 3 && cs.bi_map != builtin_method_registries::k_no_builtin) { builtin_kind = 3; }
			}
			if (builtin_kind != 0) {
				uint32_t inline_op = 0;
				const size_t n = expr->arguments.size();
				const std::string_view name = member->member;
				if (n == 0 && (name == "size" || name == "length")) inline_op = 1;
				else if (n == 0 && name == "empty") inline_op = 2;
				else if (builtin_kind == 1) {
					if (name == "find" && (n == 1 || n == 2)) inline_op = 3;
					else if (name == "contains" && n == 1) inline_op = 4;
					else if (name == "starts_with" && n == 1) inline_op = 5;
					else if (name == "ends_with" && n == 1) inline_op = 6;
					else if (name == "substr" && (n == 1 || n == 2)) inline_op = 7;
					else if (name == "at" && n == 1) inline_op = 8;
				} else if (builtin_kind == 2) {
					if (name == "pop" && n == 0) inline_op = 11;
				} else if (builtin_kind == 3) {
					if (name == "has" && n == 1) inline_op = 9;
					else if (name == "get" && n == 2) inline_op = 10;
				}
				emit(opcode::op_call_builtin, static_cast<uint32_t>(expr->arguments.size()),
				     static_cast<uint32_t>(chunk_->call_sites.size() - 1), builtin_kind | (inline_op << 4));
			} else {
				emit(opcode::op_call_method, static_cast<uint32_t>(expr->arguments.size()),
				     static_cast<uint32_t>(chunk_->call_sites.size() - 1));
			}
			if (null_guard != SIZE_MAX) {
				patch_jump(null_guard, chunk_->code.size());
			}
			return;
		}
	}

	if (expr->callee->get_type() == node_type::identifier_expr && math_temp_operand(expr.get()) != detail::math_fn::none) {
		// a stdlib alias whose value the stack takes (a return, an argument, a compound's right side): its temp, then a load of it
		const size_t mark = temp_next_;
		fused_operand o;
		compile_math_into_temp(expr, o);
		emit(opcode::op_load, o.slot, o.symbol, 0);
		temp_next_ = mark;
		return;
	}
	// Callee-first probe (Dev ruling 2026-07-08): identifier callees resolve via
	// op_probe_callee at the pre-args observation point into the pending register -
	// no value-stack materialization, byte-identical semantics. Other callee shapes
	// keep the verbatim load+op_call pair.
	const bool probe_callee = expr->callee->get_type() == node_type::identifier_expr;
	size_t probe_ip = SIZE_MAX;
	if (auto pinned = callee_probes_.find(expr.get()); probe_callee && pinned != callee_probes_.end()) {   // probed at its place, ahead of a graft in its arguments
		probe_ip = pinned->second.first;
		callee_probes_.erase(pinned);
	} else if (probe_callee) {
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
	std::vector<uint8_t> captures;
	detail::transient_read_marker::stamp_arg_captures(expr->arguments, captures);
	const uint32_t args_begin = static_cast<uint32_t>(chunk_->code.size());
	for (size_t k = 0; k < expr->arguments.size(); ++k) {
		const auto& arg = expr->arguments[k];
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
		emit_arg_capture(captures, k, arg.get(), 0, probe_callee ? capture_form_probe : capture_form_stack);
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

// A member read of a frame-slot instance that just compiled as LOAD base + GET_MEMBER becomes MEMBER_TEMP (op_get_member's
// kernel, the value into a fresh temp) + LOAD temp: the same two dispatches, rewritten in place (no jump lands between them),
// and a shape the cell translation takes (a truth test, a compound operand, a declaration's move)
void vm_compiler::member_tail_into_temp(const expression* e) {
	if (!e || !member_temp_operand(e)) return;
	auto& code = chunk_->code;
	const size_t n = code.size();
	if (elem_member_operand(e)) {
		// the subscript's code stays; its GET_MEMBER becomes the alias temp's landing, then the member temp over the alias and its load
		if (n < 1 || code[n - 1].op != opcode::op_get_member) return;
		const uint32_t node = code[n - 1].a;
		if (node >= chunk_->nodes.size() || chunk_->nodes[node].get() != e) return;
		const uint32_t at = elem_alias_temp();
		const uint32_t t = static_cast<uint32_t>(temp_next_++);
		if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
		code[n - 1] = vm_instruction{opcode::op_call_temp, at, 0, 0};
		emit(opcode::op_member_temp, node, t, at);
		emit(opcode::op_load, t, temp_symbol_index(), 0);
		return;
	}
	if (n < 2 || code[n - 1].op != opcode::op_get_member || code[n - 2].op != opcode::op_load) return;
	const uint32_t base = member_base_slot(e);
	if (code[n - 2].a != base || code[n - 2].c != 0) return;
	const uint32_t node = code[n - 1].a;
	if (node >= chunk_->nodes.size() || chunk_->nodes[node].get() != e) return;
	const uint32_t t = static_cast<uint32_t>(temp_next_);
	++temp_next_;
	if (temp_next_ > temp_high_) { temp_high_ = temp_next_; }
	code[n - 2] = vm_instruction{opcode::op_member_temp, node, t, base};
	code[n - 1] = vm_instruction{opcode::op_load, t, temp_symbol_index(), 0};
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
	std::vector<uint8_t> captures;
	detail::transient_read_marker::stamp_arg_captures(expr->arguments, captures);
	const uint32_t node = add_node(expr);
	for (size_t k = 0; k < expr->arguments.size(); ++k) {
		compile_expression(expr->arguments[k]);
		emit_arg_capture(captures, k, expr->arguments[k].get(), node, capture_form_new);
	}
	emit(opcode::op_new, node, static_cast<uint32_t>(expr->arguments.size()));
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
		proto.captures.push_back({id, cap.by_reference, cap.outer_slot, cap.initializer != nullptr});
		if (id == this_id) proto.captures_this = true;
	}
	for (const auto& cap : expr->captures) {
		if (cap.initializer) { compile_expression(cap.initializer); }   // evaluated at creation, in the enclosing scope, in capture order
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
		body_compiler.ir_listing(ir_listing_);
		body_compiler.ir_passes(ir_passes_);
		proto.body_chunk = body_compiler.compile_callable("<lambda>", expr->parameters, proto.body, expr->local_count);
	}

	chunk_->closure_protos.push_back(std::move(proto));
	emit(opcode::op_closure, static_cast<uint32_t>(chunk_->closure_protos.size() - 1));
}

// A program-scope declaration written without an initializer: a same-unit re-run keeps the variable, so the default is not
// evaluated at all (the interpreter asks the same question before its evaluation)
void vm_compiler::compile_variable_decl(const std::shared_ptr<variable_decl>& decl) {
	if (decl->default_initialized && decl->slot_index == SIZE_MAX) {
		emit(opcode::op_decl_keep, add_node(decl));
		const size_t skip = emit(opcode::op_jump_if_true, k_invalid_u32, 1);
		compile_variable_decl_value(decl);
		patch_jump(skip, chunk_->code.size());
		return;
	}
	compile_variable_decl_value(decl);
}

void vm_compiler::compile_variable_decl_value(const std::shared_ptr<variable_decl>& decl) {
	const bool is_reference = decl->type && decl->type->base_type == script_value_type::jai_reference_type;
	if (decl->ref_escaping && decl->slot_index != SIZE_MAX) { callable_.escaping.insert(static_cast<uint32_t>(decl->slot_index)); }   // a reference or a capture reaches it
	if (!is_reference && decl->slot_index != SIZE_MAX && primitive_declared_type(decl->type.get())) { callable_.primitive_slots.insert(static_cast<uint32_t>(decl->slot_index)); }   // a typed scalar slot holds that kind at every point
	if (!is_reference && try_inline_variable_decl(decl)) return;

	// Locals declared through env fallback in lambda contexts keep working because the
	// slot decision below mirrors identifier loads (declared symbols keep parser slots).
	if (is_reference) {
		if (decl->slot_index != SIZE_MAX) { chunk_->reference_slots.push_back(static_cast<uint32_t>(decl->slot_index)); }   // its load dereferences, possibly through a getter (a plain alias is taken back out below)
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
				if (plain) { callable_.plain_refs[static_cast<uint32_t>(decl->slot_index)] = decl->name_id; std::erase(chunk_->reference_slots, static_cast<uint32_t>(decl->slot_index)); }
			}
			emit(opcode::op_decl_ref_ident, add_node(decl), add_symbol(ident->symbol_id));
		} else {
			compile_expression(decl->initializer);
			emit(opcode::op_decl_ref_value, add_node(decl));
		}
		return;
	}

	if (decl->initializer) { compile_decl_value(decl, decl->initializer); return; }
	emit(opcode::op_decl_var, add_node(decl), 0, 0);
}

// The declaration's value lands by the declaration's own rule: the move row for an identifier or a literal, the fused
// binary or subscript read landing straight in the slot, else the general declaration over the pushed value. A ternary
// initializer of a slot declaration lands each arm this way under the condition's jumps, so no value crosses a jump on
// the operand stack (the block form admits the arms as moves); the interpreter evaluates the same arm and converts once.
void vm_compiler::compile_decl_value(const std::shared_ptr<variable_decl>& decl, const expression_ptr& init) {
	if (init->get_type() == node_type::ternary_expr && callable_.active &&
	    (decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store && !decl->ref_escaping &&
	    decl->slot_index != SIZE_MAX && decl->slot_index < k_typed_slot_mask) {
		auto* t = static_cast<ternary_expr*>(init.get());
		std::vector<size_t> false_jumps;
		compile_branch_condition(t->condition, false_jumps);
		compile_decl_value(decl, t->then_expression);
		const size_t jump_end = emit(opcode::op_jump, k_invalid_u32);
		for (size_t at : false_jumps) { patch_jump(at, chunk_->code.size()); }
		compile_decl_value(decl, t->else_expression);
		patch_jump(jump_end, chunk_->code.size());
		return;
	}
	if (callable_.active && (unary_temp_operand(init.get()) || math_temp_operand(init.get()) != detail::math_fn::none || (member_temp_operand(init.get()) && primitive_declared_type(decl->type.get()))) &&
	    (decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store && !decl->ref_escaping &&
	    decl->slot_index != SIZE_MAX && decl->slot_index < k_typed_slot_mask) {
		// `T v = -e;`, `T v = math::f(e);` or `T v = p.x;` with T a primitive (a whole initializer or a ternary's arm; an object or string member is an alias the declaration must copy, DECL_VAR's business): the value lands in a compiler temp and the
		// move row takes it, no push
		const size_t temp_mark = temp_next_;
		fused_operand src;
		compile_value_into_temp(init, 0xFF, src);
		const size_t ip = chunk_->code.size();
		chunk::typed_site& site = typed_site_at(ip);
		site.orig = vm_instruction{opcode::op_load, src.slot, src.symbol, 0};
		site.orig2 = vm_instruction{opcode::op_decl_var, add_node(decl), 1, 0};
		site.stamp = decl->type.get();
		emit(opcode::op_t_decl_mov, static_cast<uint32_t>(decl->slot_index), src.slot, site.orig2.a);
		temp_next_ = temp_mark;   // the unary's temp dies into the move
		return;
	}
	compile_expression(init);
	const uint32_t lvalue_init = is_lvalue_shaped(init.get()) ? 1u : 0u;
	if (try_lower_decl_move(decl, init.get())) return;
	// Dest-addressed peephole (flatstack stage 6, same in-place rewrite shape as
	// emit_compound_store): a fused-binary init lands straight in the decl - one
	// dispatch, no push. The exec falls back to op_decl_var verbatim for every
	// non-scalar decl shape, so this fuses unconditionally.
	auto& code = chunk_->code;
	if (!code.empty() && code.back().op == opcode::op_binary_fused &&
	    init->get_type() == node_type::binary_expr) {
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
	    init->get_type() == node_type::binary_expr) {
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
	if (init->get_type() == node_type::member_expr) { member_tail_into_temp(init.get()); }
	if (typed_lowering_ && init->get_type() == node_type::call_expr && !code.empty() && code.back().op == opcode::op_call_from_scratch && code.back().c == 0 &&
	    (decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store && !decl->ref_escaping &&
	    decl->slot_index != SIZE_MAX && decl->slot_index < k_typed_slot_mask && decl->type &&
	    (decl->type->base_type == script_value_type::jai_int_type || decl->type->base_type == script_value_type::jai_float_type)) {
		// `int x = f(...);` / `float x = f(...);`: a result of the declared kind lands in the slot at the return, the declaration skipped
		chunk::typed_site& site = typed_site_at(code.size() - 1);
		site.operands[0] = static_cast<uint32_t>(decl->slot_index) | (k_typed_dst_decl << k_typed_dst_shift);
		site.stamp = decl->type.get();
		code.back().op = opcode::op_call_typed;
	}
	emit(opcode::op_decl_var, add_node(decl), 1, lvalue_init);
}

void vm_compiler::compile_function_decl(const std::shared_ptr<function_decl>& decl) {
	function_proto proto;
	proto.decl = decl;
	proto.is_coroutine = decl->is_coroutine;
	if (!decl->is_coroutine) {
		proto.fn = std::make_shared<script_defined_function>(
			decl->name, decl->parameters, decl->return_type, decl->body, nullptr, decl->local_count);
		proto.fn->alias_candidate = true;
		proto.fn->decl = decl;
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
