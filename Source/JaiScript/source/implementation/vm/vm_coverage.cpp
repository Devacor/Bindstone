// The execution-coverage census (vm/coverage.hpp): a diagnostic build's counters, the static classification of each
// chunk when it first runs, and the report. Nothing here exists without JAISCRIPT_VM_COVERAGE.
#ifdef JAISCRIPT_VM_COVERAGE

#include <jaiscript/vm/vm_backend.hpp>
#include <jaiscript/vm/coverage.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/detail/cast_kernel.hpp>
#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>

namespace jai::vm {

namespace {

	enum class fam : uint8_t {
		typed_arith, typed_cmp, typed_move, typed_elem,
		call, method, builtin, ret, member, element, string, alloc, arith, compare, move, loop, block, scope, decl,
		cell_arith, cell_branch, cell_mem, cell_member, cell_move, cell_math, cell_door, cell_boundary,
		other, count
	};

	const char* fam_name(fam f) {
		switch (f) {
		case fam::typed_arith: return "typed arithmetic (T_ADD..T_MOD)";
		case fam::typed_cmp: return "typed compare-jump (T_J*)";
		case fam::typed_move: return "typed move (T_DECL/T_STORE)";
		case fam::typed_elem: return "typed element (T_LOAD/T_PUSH)";
		case fam::call: return "call: function (probe, call, guard, temp)";
		case fam::method: return "call: method";
		case fam::builtin: return "call: builtin method (container/string)";
		case fam::ret: return "return / frame exit";
		case fam::member: return "member access";
		case fam::element: return "element access / iteration";
		case fam::string: return "string concatenation";
		case fam::alloc: return "allocation (new, literal, closure)";
		case fam::arith: return "untyped arithmetic / math / cast";
		case fam::compare: return "untyped compare / branch";
		case fam::move: return "untyped load / store / declaration";
		case fam::loop: return "loop control (back edge, counted for)";
		case fam::block: return "block entry / resume";
		case fam::scope: return "scope / try / throw / yield";
		case fam::decl: return "program declarations";
		case fam::cell_arith: return "cell arithmetic";
		case fam::cell_branch: return "cell compare / branch / back edge";
		case fam::cell_mem: return "cell element load / store / push";
		case fam::cell_member: return "cell member site";
		case fam::cell_move: return "cell move";
		case fam::cell_math: return "cell math intrinsic";
		case fam::cell_door: return "cell call door (probe, args, call, land, ret)";
		case fam::cell_boundary: return "cell boundary operation";
		default: return "other";
		}
	}

	fam family_of(opcode o) {
		if (o >= opcode::op_t_add_ii && o <= opcode::op_t_mod_ff) return fam::typed_arith;
		if (o >= opcode::op_t_jlt_ii && o <= opcode::op_t_jne_ff) return fam::typed_cmp;
		switch (o) {
		case opcode::op_t_decl_mov: case opcode::op_t_decl_const: case opcode::op_t_store_mov: case opcode::op_t_store_const: return fam::typed_move;
		case opcode::op_t_push: case opcode::op_t_load: return fam::typed_elem;
		case opcode::op_call: case opcode::op_probe_callee: case opcode::op_call_from_scratch: case opcode::op_call_typed: case opcode::op_call_temp:
		case opcode::op_capture_arg: case opcode::op_take_temp: case opcode::op_inline_guard: case opcode::op_math_guard:
			return fam::call;
		case opcode::op_call_method: case opcode::op_probe_member: return fam::method;
		case opcode::op_call_builtin: case opcode::op_size_temp: case opcode::op_push_fused: return fam::builtin;
		case opcode::op_return: case opcode::op_return_ident: case opcode::op_return_binary: case opcode::op_return_convert: case opcode::op_implicit_return:
		case opcode::op_halt: case opcode::op_ref_return_bind: case opcode::op_ref_return_lvalue:
			return fam::ret;
		case opcode::op_get_member: case opcode::op_get_static: case opcode::op_set_member: case opcode::op_set_static: case opcode::op_member_compound:
		case opcode::op_member_temp: case opcode::op_set_member_fused: case opcode::op_set_member_value: case opcode::op_this: case opcode::op_super:
		case opcode::op_from_this: case opcode::op_null_guard: case opcode::op_velem_load: case opcode::op_velem_store:
			return fam::member;
		case opcode::op_index: case opcode::op_index_assign: case opcode::op_index_compound: case opcode::op_index_store: case opcode::op_index_compound_fused:
		case opcode::op_index_fused: case opcode::op_index_store_fused: case opcode::op_index_fused_decl: case opcode::op_iter_init: case opcode::op_iter_next:
		case opcode::op_iter_pop: case opcode::op_destructure: case opcode::op_parallel_element: case opcode::op_deref_top:
			return fam::element;
		case opcode::op_concat_append: return fam::string;
		case opcode::op_new: case opcode::op_array: case opcode::op_map: case opcode::op_closure: return fam::alloc;
		case opcode::op_binary: case opcode::op_binary_fused: case opcode::op_binary_fused_decl: case opcode::op_binary_fused_store: case opcode::op_binary_fused_temp:
		case opcode::op_unary: case opcode::op_unary_temp: case opcode::op_math: case opcode::op_math_temp: case opcode::op_compound_store: case opcode::op_compound_fused:
		case opcode::op_incdec: case opcode::op_operand_temp: case opcode::op_cast: case opcode::op_to_bool: case opcode::op_case_eq:
			return fam::arith;
		case opcode::op_fused_cmp_jump: case opcode::op_jump: case opcode::op_jump_if_false: case opcode::op_jump_if_true: return fam::compare;
		case opcode::op_const: case opcode::op_null: case opcode::op_true: case opcode::op_false: case opcode::op_pop: case opcode::op_dup: case opcode::op_load:
		case opcode::op_store: case opcode::op_decl_var: case opcode::op_decl_ref_ident: case opcode::op_decl_ref_value: case opcode::op_decl_keep:
		case opcode::op_decl_share: case opcode::op_ref_target_store:
			return fam::move;
		case opcode::op_loop_back: case opcode::op_cfor_prep: case opcode::op_cfor_back: case opcode::op_cfor_pop: case opcode::op_parallel_advance: return fam::loop;
		case opcode::op_cells_back: case opcode::op_cells_enter: case opcode::op_cells_cfor_back: case opcode::op_cells_resume: return fam::block;
		case opcode::op_scope_push: case opcode::op_scope_pop: case opcode::op_scope_pop_n: case opcode::op_try_push: case opcode::op_try_pop: case opcode::op_catch_end:
		case opcode::op_throw: case opcode::op_yield:
			return fam::scope;
		case opcode::op_func_decl: case opcode::op_class_decl: case opcode::op_namespace_decl: case opcode::op_enum_decl: case opcode::op_extern_decl:
		case opcode::op_include: case opcode::op_import: case opcode::op_parallel_for: case opcode::op_error:
			return fam::decl;
		case opcode::op_c_arith: case opcode::op_c_arith_p: case opcode::op_c_arith_any: case opcode::op_c_arith2: case opcode::op_c_arith_load: case opcode::op_c_arith_jump:
		case opcode::op_c_add_ff: case opcode::op_c_sub_ff: case opcode::op_c_mul_ff: case opcode::op_c_add_ffk: case opcode::op_c_sub_ffk: case opcode::op_c_mul_ffk:
		case opcode::op_c_add_ii: case opcode::op_c_sub_ii: case opcode::op_c_add_iik: case opcode::op_c_sub_iik: case opcode::op_c_neg: case opcode::op_c_bnot:
		case opcode::op_c_lnot: case opcode::op_c_cmpval: case opcode::op_c_append:
			return fam::cell_arith;
		case opcode::op_c_cmp: case opcode::op_c_cmp_p: case opcode::op_c_jcmp: case opcode::op_c_jcmp_p: case opcode::op_c_jcmp2: case opcode::op_c_cmp_any:
		case opcode::op_c_jcmp_any: case opcode::op_c_jump: case opcode::op_c_back: case opcode::op_c_back_step: case opcode::op_c_cfor_back: case opcode::op_c_range:
		case opcode::op_c_exit: case opcode::op_c_bail:
			return fam::cell_branch;
		case opcode::op_c_load: case opcode::op_c_load_any: case opcode::op_c_load2: case opcode::op_c_load_jcmp: case opcode::op_c_index_store: case opcode::op_c_push:
		case opcode::op_c_size: case opcode::op_c_guard: case opcode::op_c_rebind:
			return fam::cell_mem;
		case opcode::op_c_mload: case opcode::op_c_mstore: return fam::cell_member;
		case opcode::op_c_mov: case opcode::op_c_mov_p: case opcode::op_c_mov_k: return fam::cell_move;
		case opcode::op_c_math: return fam::cell_math;
		case opcode::op_c_probe: case opcode::op_c_args: case opcode::op_c_call: case opcode::op_c_land: case opcode::op_c_ret: return fam::cell_door;
		case opcode::op_c_boundary: return fam::cell_boundary;
		default: return fam::other;
		}
	}

	bool typed_op(opcode o) { const fam f = family_of(o); return f == fam::typed_arith || f == fam::typed_cmp || f == fam::typed_move || f == fam::typed_elem; }

	// the region builder's operations (vm_compiler::lower_typed_ops straight_op): a general instruction of this set outside every span
	// stands in a fragment with no statement boundary or entry the builder takes
	bool straight_op(opcode o) {
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
	}

	std::string sym_name(const chunk& c, const string_symbolizer* sym, uint32_t idx) {
		if (!sym || idx >= c.symbols.size()) return {};
		return std::string(sym->get_string(c.symbols[idx]));
	}

	std::string node_name(const chunk& c, uint32_t node) {
		if (node >= c.nodes.size() || !c.nodes[node]) return {};
		const ast_node* n = c.nodes[node].get();
		switch (n->get_type()) {
		case node_type::member_expr: return std::string(static_cast<const member_expr*>(n)->member);
		case node_type::variable_decl: return std::string(static_cast<const variable_decl*>(n)->name);
		case node_type::new_expr: return "new";
		default: return {};
		}
	}

	uint32_t call_site_of(const vm_instruction& ins) {
		switch (ins.op) {
		case opcode::op_probe_callee: return ins.a;
		case opcode::op_call_from_scratch: case opcode::op_call_typed: case opcode::op_call_method: case opcode::op_call_builtin: case opcode::op_call: return ins.b;
		case opcode::op_size_temp: return ins.b;
		case opcode::op_probe_member: return ins.a;
		default: return k_invalid_u32;
		}
	}

	// the instruction's name in a reason's detail: its opcode and the name it touches (a callee, a member, a variable)
	std::string instruction_detail(const chunk& c, const string_symbolizer* sym, uint32_t ip) {
		if (ip >= c.code.size()) return "?";
		const vm_instruction& ins = c.code[ip];
		std::string out(opcode_name(ins.op));
		std::string name;
		const uint32_t site = call_site_of(ins);
		if (site != k_invalid_u32 && site < c.call_sites.size()) {
			const call_site& cs = c.call_sites[site];
			if (cs.member_node != k_invalid_u32) name = node_name(c, cs.member_node);
			else if (cs.callee.symbol != k_invalid_u32) name = sym_name(c, sym, cs.callee.symbol);
		} else {
			switch (ins.op) {
			case opcode::op_get_member: case opcode::op_get_static: case opcode::op_set_member: case opcode::op_set_static: case opcode::op_member_compound:
			case opcode::op_member_temp: case opcode::op_set_member_fused: case opcode::op_decl_var: case opcode::op_decl_ref_ident: case opcode::op_decl_ref_value:
			case opcode::op_new: case opcode::op_cast:
				name = node_name(c, ins.a); break;
			case opcode::op_set_member_value: if (ins.a < c.member_store_protos.size()) { name = node_name(c, c.member_store_protos[ins.a].node); } break;
			case opcode::op_load: name = sym_name(c, sym, ins.b); break;
			case opcode::op_store: case opcode::op_compound_store: case opcode::op_incdec: name = sym_name(c, sym, ins.a); break;
			case opcode::op_index_fused: case opcode::op_index_store_fused: case opcode::op_index_fused_decl:
				if (ins.a < c.fused_index_protos.size()) {
					const fused_index_proto& fp = c.fused_index_protos[ins.a];
					name = sym_name(c, sym, fp.container.symbol);
					if (fp.receiver == 1) name += " (string)"; else if (fp.receiver == 3) name += " (map)";
				}
				break;
			default: break;
			}
		}
		if (!name.empty()) { out += " "; out += name; }
		return out;
	}

	// why the instruction at ip keeps a loop (or a straight-line span) off the cells: the refusal class and the instruction's kind
	std::string refusal_category(const chunk& c, uint32_t ip, uint8_t cls) {
		switch (cls) {
		case chunk::cell_refusal_head: return "loop head form";
		case chunk::cell_refusal_no_cells: return "no scalar work (no cells)";
		case chunk::cell_refusal_too_many_cells: return "too many cells";
		case chunk::cell_refusal_malformed: return "malformed region (lifetime rule)";
		default: break;
		}
		if (ip >= c.code.size()) return "unsupported instruction";
		const vm_instruction& ins = c.code[ip];
		switch (ins.op) {
		case opcode::op_decl_ref_ident: case opcode::op_decl_ref_value: case opcode::op_ref_return_bind: case opcode::op_ref_return_lvalue: case opcode::op_ref_target_store:
			return "reference / alias";
		case opcode::op_concat_append: return "string operation";
		default: break;
		}
		switch (family_of(ins.op)) {
		case fam::call: return "call boundary (function)";
		case fam::method: return "call boundary (method)";
		case fam::builtin: return "call boundary (builtin method)";
		case fam::member: return "member access";
		case fam::element: case fam::typed_elem: {
			if (cls == chunk::cell_refusal_effect) return "effect (call-argument order, an edge out, a global-only name)";
			uint32_t proto = k_invalid_u32;
			if (ins.op == opcode::op_index_fused || ins.op == opcode::op_index_store_fused || ins.op == opcode::op_index_fused_decl) proto = ins.a;
			if (proto < c.fused_index_protos.size()) {
				if (c.fused_index_protos[proto].receiver == 1) return "string operation";
				if (c.fused_index_protos[proto].receiver == 3) return "map operation";
			}
			return "container element";
		}
		case fam::alloc: return "allocation";
		case fam::ret: return "exit (return)";
		case fam::scope: return "scope / try / throw / yield";
		case fam::decl: return "declaration";
		case fam::compare: case fam::loop:
			return cls == chunk::cell_refusal_effect ? "exit (break / an edge out)" : cls == chunk::cell_refusal_translation ? "translation gap" : "non-scalar or untyped operand";
		case fam::arith: case fam::move: case fam::typed_arith: case fam::typed_cmp: case fam::typed_move:
			return cls == chunk::cell_refusal_translation ? "translation gap" : cls == chunk::cell_refusal_effect ? "effect (call-argument order, an edge out, a global-only name)" : "non-scalar or untyped operand";
		default: return "unsupported instruction";
		}
	}

	std::string class_word(uint8_t cls) { return chunk::cell_refusal_name(cls); }

	std::string operand_shape(const fused_operand& o) {
		if (o.const_index != k_invalid_u32) return "literal";
		if (o.is_subscript()) return "element";
		if (o.member_node != k_invalid_u32) return "member";
		if (o.slot == k_invalid_u32) return "global";
		return o.direct ? "local" : "slot";
	}
	const char* token_text(uint8_t t) {
		switch (static_cast<token_type>(t)) {
		case token_type::plus: return "+"; case token_type::minus: return "-"; case token_type::star: return "*"; case token_type::slash: return "/";
		case token_type::percent: return "%"; case token_type::ampersand: return "&"; case token_type::pipe: return "|"; case token_type::caret: return "^";
		case token_type::left_shift: return "<<"; case token_type::right_shift: return ">>"; case token_type::less: return "<"; case token_type::less_equal: return "<=";
		case token_type::greater: return ">"; case token_type::greater_equal: return ">="; case token_type::equal_equal: return "=="; case token_type::bang_equal: return "!=";
		case token_type::ampersand_ampersand: return "&&"; case token_type::pipe_pipe: return "||";
		default: return "?";
		}
	}
	// a fused binary's operator, its operands' shapes and the checker's class (0 unproven, 1 int, 2 float)
	std::string binary_detail(const chunk& c, uint32_t pi) {
		if (pi >= c.fused_binary_protos.size()) return {};
		const fused_binary_proto& bp = c.fused_binary_protos[pi];
		return std::string(" `") + operand_shape(bp.left) + " " + token_text(bp.op) + " " + operand_shape(bp.right) + "` class " + std::to_string(bp.typed_class);
	}
	std::string refusal_detail(const chunk& c, const string_symbolizer* sym, uint32_t ip, uint8_t cls, uint32_t head, uint32_t back) {
		std::string out = instruction_detail(c, sym, ip);
		if (ip < c.code.size()) {
			const vm_instruction& ins = c.code[ip];
			switch (ins.op) {
			case opcode::op_binary_fused: case opcode::op_binary_fused_temp: out += binary_detail(c, ins.a); break;
			case opcode::op_fused_cmp_jump: out += binary_detail(c, ins.b); break;
			case opcode::op_binary_fused_decl: case opcode::op_binary_fused_store: if (ins.a < c.fused_binary_dst_protos.size()) { out += binary_detail(c, c.fused_binary_dst_protos[ins.a].binary_proto); } break;
			default: break;
			}
		}
		if (cls == chunk::cell_refusal_head) {   // the head's instructions up to its first branch: what the head form did not take
			out += " head:";
			for (uint32_t k = head, shown = 0; k < back && k < c.code.size() && shown < 8; ++k, ++shown) {
				const vm_instruction& h = c.code[k];
				out += " "; out += instruction_detail(c, sym, k);
				if (h.op == opcode::op_fused_cmp_jump) out += binary_detail(c, h.b);
				if (h.op == opcode::op_jump_if_false || h.op == opcode::op_jump_if_true || h.op == opcode::op_fused_cmp_jump || (h.op >= opcode::op_t_jlt_ii && h.op <= opcode::op_t_jne_ff) || h.op == opcode::op_iter_next) break;
			}
		}
		return out;
	}

	struct span { uint32_t lo, hi; coverage_region kind; uint32_t index; };

	coverage_chunk classify(const chunk& c, const string_symbolizer* sym) {
		coverage_chunk r;
		r.name = c.function_name.empty() ? "<top level>" : c.function_name;
		r.identity = c.code.data();
		r.size = c.code.size();
		const size_t n = c.code.size();
		r.general.assign(n, 0); r.cells.assign(n, 0); r.line.assign(n, 0); r.file.assign(n, 0); r.op.resize(n);
		r.files.push_back("");
		r.region.assign(n, coverage_region::none); r.region_index.assign(n, k_invalid_u32); r.site.assign(n, k_invalid_u32);
		for (size_t ip = 0; ip < n; ++ip) {
			r.op[ip] = static_cast<uint8_t>(c.code[ip].op);
			const ast_node* st = ip < c.stmt_nodes.size() ? c.stmt_nodes[ip] : nullptr;
			if (st) {
				r.line[ip] = static_cast<uint32_t>(st->location.line);
				std::string f = st->location.filename;
				const size_t cut = f.find_last_of("/\\");
				if (cut != std::string::npos) f = f.substr(cut + 1);
				auto it = std::find(r.files.begin(), r.files.end(), f);
				if (it == r.files.end()) { r.files.push_back(f); it = r.files.end() - 1; }
				r.file[ip] = static_cast<uint16_t>(it - r.files.begin());
			}
		}
		auto line_of = [&](uint32_t ip) -> uint32_t { return ip < n ? r.line[ip] : 0; };
		auto add_site = [&](uint32_t at, std::string reason, std::string detail) -> uint32_t {
			r.sites.push_back(coverage_chunk::site_row{ line_of(at), at < n ? r.file[at] : uint16_t(0), std::move(reason), std::move(detail) });
			return static_cast<uint32_t>(r.sites.size() - 1);
		};
		size_t orig = n;
		for (const auto& L : c.cell_loops) { if (L.copy_head < orig) orig = L.copy_head; }
		r.loops.resize(c.cell_loops.size());
		std::vector<span> spans;
		for (uint32_t li = 0; li < c.cell_loops.size(); ++li) {
			const auto& L = c.cell_loops[li];
			auto& lr = r.loops[li];
			lr.head_line = line_of(L.head); lr.back_line = line_of(L.back > 0 && L.straight ? L.back - 1 : L.back);
			lr.straight = L.straight; lr.frame_program = L.frame_program; lr.head_file = L.head < n ? r.file[L.head] : uint16_t(0);
			const coverage_region kind = L.frame_program ? coverage_region::frame_program : L.straight ? coverage_region::straight : coverage_region::closed;
			for (size_t k = 0; k < L.origin.size(); ++k) {   // the copy: each instruction stands for its origin
				const size_t cip = L.copy_head + k;
				if (cip >= n) break;
				r.line[cip] = line_of(L.origin[k]);
				r.file[cip] = L.origin[k] < n ? r.file[L.origin[k]] : uint16_t(0);
				r.region[cip] = kind; r.region_index[cip] = li;
			}
			uint32_t hi = L.straight ? L.back : L.back + 1;
			if (L.frame_program) hi = L.back + 1;
			else if (!L.straight && L.counted != k_invalid_u32 && L.counted < c.counted_for_protos.size()) hi = std::max<uint32_t>(hi, c.counted_for_protos[L.counted].exit_ip);
			spans.push_back(span{ L.head, hi, kind, li });
		}
		std::vector<uint32_t> loop_site(c.cell_loops.size(), k_invalid_u32);
		for (uint32_t li = 0; li < c.cell_loops.size(); ++li) {
			const auto& L = c.cell_loops[li];
			const std::string lines = "lines " + std::to_string(r.loops[li].head_line) + "-" + std::to_string(r.loops[li].back_line);
			if (L.frame_program) loop_site[li] = add_site(L.head, "frame program: general form", lines);
			else if (L.straight) loop_site[li] = add_site(L.head, "straight region: general form (entry refused or a miss)", lines);
			else loop_site[li] = add_site(L.head, "closed loop: general form", lines);
		}
		for (const auto& rf : c.cell_refusals) {
			const std::string detail = refusal_detail(c, sym, rf.ip, rf.reason, rf.head, rf.back) + " [" + class_word(rf.reason) + "] loop lines " + std::to_string(line_of(rf.head)) + "-" + std::to_string(line_of(rf.back));
			const uint32_t s = add_site(rf.ip, "loop refused: " + refusal_category(c, rf.ip, rf.reason), detail);
			spans.push_back(span{ rf.head, rf.back + 1, coverage_region::refused, s });
		}
		// loops the translation never attempted: a back edge no closed or refused loop stands for
		for (uint32_t ip = 0; ip < orig; ++ip) {
			const vm_instruction& ins = c.code[ip];
			uint32_t lo = k_invalid_u32;
			if (ins.op == opcode::op_loop_back && ins.a <= ip) lo = ins.a;
			else if (ins.op == opcode::op_jump && ins.a <= ip) lo = ins.a;
			else if (ins.op == opcode::op_cfor_back && ins.a < c.counted_for_protos.size() && c.counted_for_protos[ins.a].body_ip != k_invalid_u32 && c.counted_for_protos[ins.a].body_ip > 0) lo = c.counted_for_protos[ins.a].body_ip - 1;
			if (lo == k_invalid_u32) continue;
			bool covered = false;
			for (const span& s : spans) {
				if (s.kind == coverage_region::refused && s.hi == ip + 1) { covered = true; break; }
				if ((s.kind == coverage_region::closed) && s.lo <= lo && s.hi >= ip + 1) { covered = true; break; }
			}
			if (covered) continue;
			std::string form = std::string(opcode_name(ins.op));
			const uint32_t s = add_site(ip, std::string("loop not attempted (back edge ") + form + ")", "lines " + std::to_string(line_of(lo)) + "-" + std::to_string(line_of(ip)));
			spans.push_back(span{ lo, ip + 1, coverage_region::unattempted, s });
		}
		// the straight-line spans the region builder dropped
		std::vector<span> notes;
		for (const auto& sn : c.straight_notes) {
			std::string reason, detail;
			if (sn.reason == chunk::cell_refusal_none) {
				reason = sn.work < 16 ? "straight-line: span below the region minimum" : "straight-line: span whose statement boundaries close too little work";
				detail = std::to_string(sn.work) + " scalar ops, " + std::to_string(sn.bounded) + " to the last statement boundary";
			}
			else { reason = "straight-line: span refused (" + refusal_category(c, sn.ip, sn.reason) + ")"; detail = refusal_detail(c, sym, sn.ip, sn.reason, sn.head, sn.end) + " [" + class_word(sn.reason) + "]"; }
			const uint32_t s = add_site(sn.ip, std::move(reason), std::move(detail));
			notes.push_back(span{ sn.head, std::max(sn.end, sn.head + 1), coverage_region::none, s });
		}
		std::map<std::pair<uint64_t, std::string>, uint32_t> plain_sites;   // (file << 32 | line, reason)
		auto plain_key = [&](uint32_t at, std::string reason) { return std::make_pair((static_cast<uint64_t>(at < n ? r.file[at] : 0) << 32) | line_of(at), std::move(reason)); };
		for (uint32_t ip = 0; ip < n; ++ip) {
			if (r.region_index[ip] != k_invalid_u32 && ip >= orig) continue;   // a copy instruction
			if (ip >= orig) {
				r.region[ip] = coverage_region::trampoline;
				auto key = plain_key(ip, "region trampoline");
				auto it = plain_sites.find(key);
				r.site[ip] = it != plain_sites.end() ? it->second : (plain_sites[key] = add_site(ip, key.second, ""));
				continue;
			}
			const span* best = nullptr;
			for (const span& s : spans) {
				if (ip < s.lo || ip >= s.hi) continue;
				if (!best || (s.hi - s.lo) < (best->hi - best->lo)) best = &s;
			}
			if (best) {
				r.region[ip] = best->kind;
				r.region_index[ip] = best->index;
				if (best->kind == coverage_region::refused || best->kind == coverage_region::unattempted) { r.site[ip] = best->index; r.region_index[ip] = k_invalid_u32; }
				else {
					r.site[ip] = loop_site[best->index];
					if (best->kind == coverage_region::closed) {
						const auto& L = c.cell_loops[best->index];
						for (size_t b = 0; b < L.boundaries.size(); ++b) {
							if (ip >= L.boundaries[b].start && ip < L.boundaries[b].end) {
								r.region[ip] = coverage_region::boundary;
								auto key = plain_key(L.boundaries[b].start, "closed loop: boundary statement");
								auto it = plain_sites.find(key);
								r.site[ip] = it != plain_sites.end() ? it->second : (plain_sites[key] = add_site(L.boundaries[b].start, key.second, instruction_detail(c, sym, L.boundaries[b].start)));
								break;
							}
						}
					}
				}
				continue;
			}
			const span* note = nullptr;
			for (const span& s : notes) {
				if (ip < s.lo || ip >= s.hi) continue;
				if (!note || (s.hi - s.lo) < (note->hi - note->lo)) note = &s;
			}
			if (note) { r.site[ip] = note->index; continue; }
			const opcode op = c.code[ip].op;
			std::string reason = straight_op(op) && family_of(op) != fam::ret && family_of(op) != fam::compare && family_of(op) != fam::move
				? "straight-line: scalar op, no span (no statement boundary or entry op)" : std::string("straight-line: ") + fam_name(family_of(op));
			auto key = plain_key(ip, reason);
			auto it = plain_sites.find(key);
			r.site[ip] = it != plain_sites.end() ? it->second : (plain_sites[key] = add_site(ip, reason, ""));
		}
		for (uint32_t ip = 0; ip < orig; ++ip) {
			switch (family_of(c.code[ip].op)) {
			case fam::member: case fam::method: case fam::builtin: case fam::arith: case fam::compare: case fam::string: case fam::call: {
				std::string d = instruction_detail(c, sym, ip);
				if (family_of(c.code[ip].op) == fam::call) {
					for (const auto& kr : c.inline_refusals) {
						if (kr.line == line_of(ip) && d.find(kr.callee) != std::string::npos) { d += std::string(" [call kept: ") + (kr.reason ? kr.reason : "?") + "]"; break; }
					}
				}
				const vm_instruction& ins = c.code[ip];
				uint32_t pi = k_invalid_u32;
				if (ins.op == opcode::op_binary_fused || ins.op == opcode::op_binary_fused_temp) pi = ins.a;
				else if (ins.op == opcode::op_fused_cmp_jump) pi = ins.b;
				else if ((ins.op == opcode::op_binary_fused_decl || ins.op == opcode::op_binary_fused_store) && ins.a < c.fused_binary_dst_protos.size()) pi = c.fused_binary_dst_protos[ins.a].binary_proto;
				if (pi < c.fused_binary_protos.size()) { d += binary_detail(c, pi); r.binary_class.emplace(ip, c.fused_binary_protos[pi].typed_class); }
				r.details.emplace(ip, std::move(d));
				break;
			}
			default: break;
			}
		}
		// the calls the inliner kept: their rule joins the detail of the sites on their line
		for (auto& s : r.sites) {
			for (const auto& kr : c.inline_refusals) {
				if (kr.line != s.line || s.detail.find(kr.callee) == std::string::npos) continue;
				s.detail += std::string(" [call kept: ") + (kr.reason ? kr.reason : "?") + "]";
				break;
			}
		}
		return r;
	}

	void zero_row(coverage_chunk& r) {
		std::fill(r.general.begin(), r.general.end(), 0);
		std::fill(r.cells.begin(), r.cells.end(), 0);
		for (auto& l : r.loops) { l.entries = 0; l.misses = 0; for (auto& x : l.refused) x = 0; }
		r.receivers.clear();
		r.operands.clear();
	}

	bool pair_first_falls(opcode o) {
		return o == opcode::op_c_arith2 || o == opcode::op_c_load2 || o == opcode::op_c_arith_load || o == opcode::op_c_load_jcmp || o == opcode::op_c_arith_jump;
	}

} // namespace

coverage_chunk& coverage_tables::bind(const chunk& c, const string_symbolizer* symbolizer) {
	auto it = chunks.find(&c);
	if (it != chunks.end() && (it->second.identity != static_cast<const void*>(c.code.data()) || it->second.size != c.code.size())) {
		retired.push_back(std::move(it->second));
		chunks.erase(it);
		it = chunks.end();
	}
	if (it == chunks.end()) { it = chunks.emplace(&c, classify(c, symbolizer)).first; }
	last = &c;
	last_row = &it->second;
	return it->second;
}

void coverage_tables::reset() {
	for (auto& [k, r] : chunks) { zero_row(r); }
	retired.clear();
	cell_dispatches = 0;
}

coverage_chunk& vm_backend::coverage_row(const chunk& c) {
	coverage_tables& t = *coverage_;
	if (t.last == &c && t.last_row->identity == static_cast<const void*>(c.code.data()) && t.last_row->size == c.code.size()) return *t.last_row;
	return t.bind(c, symbolizer_);
}

void vm_backend::coverage_general(frame& f) {
	coverage_chunk& row = coverage_row(*f.code);
	if (f.ip >= row.general.size()) return;
	++row.general[f.ip];
	const opcode op = f.code->code[f.ip].op;
	switch (family_of(op)) {
	case fam::member: case fam::method: case fam::builtin: case fam::arith: case fam::compare: case fam::string:
		coverage_observe(f, f.code->code[f.ip], row);
		break;
	default: break;
	}
}

void vm_backend::coverage_cell(const frame& f, size_t ip, opcode op) {
	coverage_chunk& row = coverage_row(*f.code);
	++coverage_->cell_dispatches;
	if (ip >= row.cells.size()) return;
	++row.cells[ip];
	if (pair_first_falls(op) && ip + 1 < row.cells.size()) { ++row.cells[ip + 1]; }
}

void vm_backend::coverage_cell_pair(const frame& f, size_t ip, const vm_instruction& ci, size_t next) {
	// C_JCMP_P2: the second compare ran when the first fell through
	const size_t first_target = ci.c & 0x7FFFFFFFu;
	if (next == first_target && first_target != ip + 1) return;
	coverage_chunk& row = coverage_row(*f.code);
	if (ip + 1 < row.cells.size()) { ++row.cells[ip + 1]; }
}

void vm_backend::coverage_entry(const frame& f, uint32_t loop, bool taken) {
	coverage_chunk& row = coverage_row(*f.code);
	if (loop >= row.loops.size()) return;
	auto& lr = row.loops[loop];
	if (taken) { ++lr.entries; return; }
	if (debug_hook_ || has_custom_numeric_ops_) ++lr.refused[0];
	else if (!f.cells || !f.window_backed) ++lr.refused[1];
	else if (loop < 64 && ((f.cells->refused_loops >> loop) & 1)) ++lr.refused[2];
	else ++lr.refused[3];
}

void vm_backend::coverage_miss(const frame& f) {
	if (!f.cells) return;
	coverage_chunk& row = coverage_row(*f.code);
	if (f.cells->loop < row.loops.size()) { ++row.loops[f.cells->loop].misses; }
}

namespace {
	const script_value* deref_view(const script_value* v, script_value& tmp) {
		if (!v) return nullptr;
		if (v->raw_storage_index() == script_value::TYPEID_REFERENCE) { tmp = v->deref(); return &tmp; }
		return v;
	}
	unsigned receiver_kind(const script_value& v, const class_definition*& cd) {
		cd = nullptr;
		switch (v.raw_storage_index()) {
		case script_value::TYPEID_OBJECT: case script_value::TYPEID_SHARED_PTR: {
			cd = detail::cast_dynamic_class(v);
			if (cd && cd->is_script_class()) return k_cov_recv_instance;
			return k_cov_recv_host;
		}
		case script_value::TYPEID_CPP_BOUND: return k_cov_recv_host;
		case script_value::TYPEID_ARRAY: return k_cov_recv_array;
		case script_value::TYPEID_MAP: return k_cov_recv_map;
		case script_value::TYPEID_STRING: case script_value::TYPEID_UTF8_STRING: case script_value::TYPEID_CHAR: return k_cov_recv_string;
		case script_value::TYPEID_NULL: return k_cov_recv_null;
		default: return k_cov_recv_other;
		}
	}
	unsigned operand_kind(const script_value* v) {
		if (!v) return k_cov_opnd_unknown;
		switch (v->raw_storage_index()) {
		case script_value::TYPEID_INT: return k_cov_opnd_int;
		case script_value::TYPEID_FLOAT: return k_cov_opnd_float;
		case script_value::TYPEID_BOOL: return k_cov_opnd_bool;
		case script_value::TYPEID_CHAR: case script_value::TYPEID_STRING: case script_value::TYPEID_UTF8_STRING: return k_cov_opnd_string;
		case script_value::TYPEID_OBJECT: case script_value::TYPEID_SHARED_PTR: case script_value::TYPEID_CPP_BOUND: return k_cov_opnd_object;
		case script_value::TYPEID_ARRAY: case script_value::TYPEID_MAP: return k_cov_opnd_container;
		default: return k_cov_opnd_unknown;
		}
	}
	unsigned combine_kinds(unsigned a, unsigned b) {
		if (a == k_cov_opnd_string || b == k_cov_opnd_string) return k_cov_opnd_string;
		if (a == k_cov_opnd_object || b == k_cov_opnd_object) return k_cov_opnd_object;
		if (a == k_cov_opnd_container || b == k_cov_opnd_container) return k_cov_opnd_container;
		if (a == k_cov_opnd_unknown || b == k_cov_opnd_unknown) return a == k_cov_opnd_unknown ? b : a;
		if (a == b) return a;
		if ((a == k_cov_opnd_int && b == k_cov_opnd_float) || (a == k_cov_opnd_float && b == k_cov_opnd_int)) return k_cov_opnd_mixed;
		return k_cov_opnd_mixed;
	}
}

void vm_backend::coverage_observe(frame& f, const vm_instruction& ins, coverage_chunk& row) {
	const chunk& c = *f.code;
	const size_t depth = stack_.count;
	auto stack_at = [&](size_t from_top) -> const script_value* { return from_top < depth ? &stack_.slots[depth - 1 - from_top] : nullptr; };
	auto slot_at = [&](uint32_t slot) -> const script_value* { return slot == k_invalid_u32 ? nullptr : frame_slot(static_cast<const frame&>(f), slot); };
	auto operand_value = [&](const fused_operand& o) -> const script_value* {
		if (o.const_index != k_invalid_u32) return o.const_index < c.constants.size() ? &c.constants[o.const_index] : nullptr;
		if (o.direct && o.slot != k_invalid_u32 && !o.is_subscript() && o.member_node == k_invalid_u32) return slot_at(o.slot);
		return nullptr;
	};
	script_value tmp{std::monostate{}, nullptr}, tmp2{std::monostate{}, nullptr};
	const script_value* recv = nullptr;
	bool member = true;
	switch (ins.op) {
	case opcode::op_get_member: case opcode::op_set_member: case opcode::op_probe_member: recv = stack_at(0); break;
	case opcode::op_member_temp: case opcode::op_set_member_fused: recv = slot_at(ins.c); break;
	case opcode::op_set_member_value: if (ins.a < c.member_store_protos.size()) { recv = slot_at(c.member_store_protos[ins.a].base); } break;
	case opcode::op_member_compound: recv = stack_at(2); break;
	case opcode::op_call_method: case opcode::op_call_builtin: recv = stack_at(ins.a); break;
	case opcode::op_size_temp: if (ins.b < c.call_sites.size()) { recv = slot_at(c.call_sites[ins.b].receiver_slot); } break;
	default: member = false; break;
	}
	if (member) {
		recv = deref_view(recv, tmp);
		if (!recv) return;
		const class_definition* cd = nullptr;
		const unsigned k = receiver_kind(*recv, cd);
		auto& rr = row.receivers[static_cast<uint32_t>(f.ip)];
		++rr.kinds[k];
		if (cd) {
			bool found = false;
			for (auto& [p, n] : rr.classes) { if (p == cd) { ++n; found = true; break; } }
			if (!found && rr.classes.size() < 8) rr.classes.emplace_back(cd, 1);
		}
		return;
	}
	const script_value* l = nullptr; const script_value* r = nullptr;
	bool binary = true;
	auto from_proto = [&](uint32_t pi) { if (pi < c.fused_binary_protos.size()) { l = operand_value(c.fused_binary_protos[pi].left); r = operand_value(c.fused_binary_protos[pi].right); } };
	switch (ins.op) {
	case opcode::op_binary: case opcode::op_concat_append: l = stack_at(1); r = stack_at(0); break;
	case opcode::op_binary_fused: case opcode::op_binary_fused_temp: from_proto(ins.a); break;
	case opcode::op_binary_fused_decl: case opcode::op_binary_fused_store: if (ins.a < c.fused_binary_dst_protos.size()) { from_proto(c.fused_binary_dst_protos[ins.a].binary_proto); } break;
	case opcode::op_fused_cmp_jump: from_proto(ins.b); break;
	case opcode::op_compound_fused:
		if (ins.a < c.compound_fused_protos.size()) {
			const compound_fused_proto& cp = c.compound_fused_protos[ins.a];
			l = slot_at(cp.slot);
			if (cp.rhs_proto == k_invalid_u32) r = operand_value(cp.rhs); else if (cp.rhs_proto < c.fused_binary_protos.size()) r = operand_value(c.fused_binary_protos[cp.rhs_proto].left);
		}
		break;
	case opcode::op_unary: l = stack_at(0); r = l; break;
	case opcode::op_unary_temp: if (ins.a < c.fused_binary_protos.size()) { l = operand_value(c.fused_binary_protos[ins.a].left); r = l; } break;
	case opcode::op_compound_store: l = stack_at(0); r = l; break;
	default: binary = false; break;
	}
	if (!binary) return;
	l = deref_view(l, tmp); r = deref_view(r, tmp2);
	const unsigned k = combine_kinds(operand_kind(l), operand_kind(r));
	++row.operands[static_cast<uint32_t>(f.ip)][k];
}

namespace {
	struct agg_reason {
		uint64_t typed = 0, untyped = 0;
		uint64_t fams[static_cast<size_t>(fam::count)] = {};
		std::map<std::string, uint64_t> sites;
	};
	std::string pct(uint64_t part, uint64_t whole) {
		char b[32];
		std::snprintf(b, sizeof b, "%5.1f%%", whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0);
		return b;
	}
	std::string num(uint64_t v) {
		char b[32];
		if (v >= 10000000ull) std::snprintf(b, sizeof b, "%.2fM", static_cast<double>(v) / 1e6);
		else std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(v));
		return b;
	}
	std::string pad(std::string s, size_t w) { if (s.size() < w) s.append(w - s.size(), ' '); return s; }
	std::string lpad(std::string s, size_t w) { if (s.size() < w) s.insert(0, w - s.size(), ' '); return s; }
	std::string instruction_detail_row(const coverage_chunk& r, size_t ip) {
		auto it = r.details.find(static_cast<uint32_t>(ip));
		return it != r.details.end() ? it->second : std::string(opcode_name(static_cast<opcode>(r.op[ip])));
	}
	std::string where(const coverage_chunk& r, uint16_t file, uint32_t line) {
		const std::string& f = file < r.files.size() ? r.files[file] : r.files.front();
		return r.name + " " + (f.empty() ? std::string() : f + ":") + "L" + std::to_string(line);
	}
	std::string class_shape(const class_definition* cd) {
		if (!cd) return "?";
		if (!cd->is_script_class()) return "host class";
		if (cd->flat_value_semantics()) return cd->handle_bearing_slots().empty() ? "flat scalar record" : "value class (strings / containers)";
		return "class with var / auto / handle fields";
	}
	const char* refused_name(int k) {
		switch (k) { case 0: return "debugger or numeric operator"; case 1: return "no cell area (fiber or native frame)"; case 2: return "refusal valve"; default: return "entry guard or container site"; }
	}
}

std::string coverage_report(const std::vector<std::shared_ptr<coverage_tables>>& tables, size_t top) {
	std::vector<const coverage_chunk*> rows;
	uint64_t cell_dispatches = 0;
	for (const auto& t : tables) {
		if (!t) continue;
		cell_dispatches += t->cell_dispatches;
		for (const auto& [k, r] : t->chunks) rows.push_back(&r);
		for (const auto& r : t->retired) rows.push_back(&r);
	}
	uint64_t cells_ops = 0, block_entry = 0, typed = 0, untyped = 0;
	uint64_t cell_fams[static_cast<size_t>(fam::count)] = {};
	uint64_t cell_kinds[8] = {};   // by block kind: closed loop, straight region, frame program
	uint64_t gen_fams_typed[static_cast<size_t>(fam::count)] = {}, gen_fams_untyped[static_cast<size_t>(fam::count)] = {};
	std::map<std::string, agg_reason> reasons;
	std::map<std::string, uint64_t> cell_sites;
	struct loop_agg { std::string where; uint64_t general = 0, cells = 0, entries = 0, misses = 0, refused[4] = {}; bool straight = false, fp = false; };
	std::vector<loop_agg> loops;
	uint64_t recv_kinds[8] = {};
	std::map<std::string, uint64_t> recv_classes;   // "Class (shape)" -> ops
	std::map<std::string, uint64_t> recv_shapes;
	std::map<std::string, uint64_t> recv_sites;
	uint64_t opnd[8] = {};
	std::map<std::string, uint64_t> untyped_scalar_sites, proven_scalar_sites, string_sites;
	uint64_t scalar_proven = 0, scalar_unproven = 0;
	std::map<std::string, uint64_t> general_calls;   // calls made on the general form, by callee (and the inliner's rule when it kept the call)
	uint64_t general_call_count = 0;
	for (const coverage_chunk* rp : rows) {
		const coverage_chunk& r = *rp;
		std::vector<uint64_t> loop_general(r.loops.size(), 0), loop_cells(r.loops.size(), 0);
		for (size_t ip = 0; ip < r.general.size(); ++ip) {
			const opcode op = static_cast<opcode>(r.op[ip]);
			if (r.cells[ip]) {
				const uint64_t n = r.cells[ip];
				cells_ops += n;
				cell_fams[static_cast<size_t>(family_of(op))] += n;
				const coverage_region k = r.region[ip];
				cell_kinds[k == coverage_region::frame_program ? 2 : k == coverage_region::straight ? 1 : 0] += n;
				cell_sites[where(r, r.file[ip], r.line[ip])] += n;
				if (r.region_index[ip] < loop_cells.size()) loop_cells[r.region_index[ip]] += n;
			}
			if (!r.general[ip]) continue;
			const uint64_t n = r.general[ip];
			const fam f = family_of(op);
			if (f == fam::block || f >= fam::cell_arith) { block_entry += n; continue; }   // a cell instruction on the dispatcher: a door's landing after its callee
			const bool t = typed_op(op);
			(t ? typed : untyped) += n;
			(t ? gen_fams_typed : gen_fams_untyped)[static_cast<size_t>(f)] += n;
			std::string reason = "?", detail;
			uint32_t site_line = r.line[ip];
			uint16_t site_file = r.file[ip];
			if (r.site[ip] < r.sites.size()) { const auto& s = r.sites[r.site[ip]]; reason = s.reason; detail = s.detail; site_line = s.line; site_file = s.file; }
			const coverage_region rg = r.region[ip];
			if ((rg == coverage_region::closed || rg == coverage_region::boundary || rg == coverage_region::straight || rg == coverage_region::frame_program) && r.region_index[ip] < r.loops.size()) {
				loop_general[r.region_index[ip]] += n;
				if (rg == coverage_region::closed) {
					const auto& lr = r.loops[r.region_index[ip]];
					uint64_t refused = 0; int top_k = 3;
					for (int k = 0; k < 4; ++k) { refused += lr.refused[k]; if (lr.refused[k] > lr.refused[top_k]) top_k = k; }
					if (refused > lr.entries) reason = std::string("closed loop: block entry refused at run time (") + refused_name(top_k) + ")";
					else if (lr.misses * 2 > lr.entries && lr.misses > 0) reason = "closed loop: misses back to the general form";
					else reason = "closed loop: iterations before the block entry";
				}
			}
			if (op == opcode::op_call_from_scratch || op == opcode::op_call_typed || op == opcode::op_call || op == opcode::op_call_method) {
				general_call_count += n;
				std::string d = instruction_detail_row(r, ip);
				const size_t sp = d.find(' ');
				general_calls[sp == std::string::npos ? d : d.substr(sp + 1)] += n;
			}
			auto& ag = reasons[reason];
			(t ? ag.typed : ag.untyped) += n;
			ag.fams[static_cast<size_t>(f)] += n;
			ag.sites[where(r, site_file, site_line) + (detail.empty() ? std::string() : "  " + detail)] += n;
			auto ri = r.receivers.find(static_cast<uint32_t>(ip));
			if (ri != r.receivers.end()) {
				for (int k = 0; k < 8; ++k) recv_kinds[k] += ri->second.kinds[k];
				for (const auto& [cd, cnt] : ri->second.classes) {
					if (!cd || !cd->is_script_class()) continue;
					const std::string shape = class_shape(cd);
					recv_classes[cd->get_name() + " (" + shape + ")"] += cnt;
					recv_shapes[shape] += cnt;
				}
				recv_sites[where(r, r.file[ip], r.line[ip]) + "  " + instruction_detail_row(r, ip)] += n;
			}
			auto oi = r.operands.find(static_cast<uint32_t>(ip));
			if (oi != r.operands.end() && !t) {
				uint64_t scalar = 0, str = 0;
				for (int k = 0; k < 8; ++k) opnd[k] += oi->second[k];
				scalar = oi->second[k_cov_opnd_int] + oi->second[k_cov_opnd_float] + oi->second[k_cov_opnd_mixed] + oi->second[k_cov_opnd_bool];
				str = oi->second[k_cov_opnd_string];
				auto bc = r.binary_class.find(static_cast<uint32_t>(ip));
				const bool proven = bc != r.binary_class.end() && bc->second != 0;
				(proven ? scalar_proven : scalar_unproven) += scalar;
				if (scalar) (proven ? proven_scalar_sites : untyped_scalar_sites)[where(r, r.file[ip], r.line[ip]) + "  " + instruction_detail_row(r, ip)] += scalar;
				if (str) string_sites[where(r, r.file[ip], r.line[ip]) + "  " + instruction_detail_row(r, ip)] += str;
			}
		}
		for (size_t li = 0; li < r.loops.size(); ++li) {
			const auto& lr = r.loops[li];
			if (!loop_general[li] && !loop_cells[li] && !lr.entries) continue;
			loop_agg a;
			a.where = where(r, lr.head_file, lr.head_line) + "-" + std::to_string(lr.back_line);
			a.general = loop_general[li]; a.cells = loop_cells[li]; a.entries = lr.entries; a.misses = lr.misses; a.straight = lr.straight; a.fp = lr.frame_program;
			for (int k = 0; k < 4; ++k) a.refused[k] = lr.refused[k];
			loops.push_back(std::move(a));
		}
	}
	const uint64_t total = cells_ops + block_entry + typed + untyped;
	std::ostringstream o;
	o << "== VM execution coverage (operations: each cell instruction stands for one general instruction; a fused pair counts both halves)\n";
	o << "executed operations " << num(total) << " | cell dispatches " << num(cell_dispatches) << " | chunks " << rows.size() << "\n\n";
	o << "-- classes\n";
	o << pad("(a) inside cell blocks", 52) << lpad(num(cells_ops), 12) << "  " << pct(cells_ops, total) << "\n";
	o << pad("      closed loops", 52) << lpad(num(cell_kinds[0]), 12) << "  " << pct(cell_kinds[0], total) << "\n";
	o << pad("      straight-line regions", 52) << lpad(num(cell_kinds[1]), 12) << "  " << pct(cell_kinds[1], total) << "\n";
	o << pad("      frame programs", 52) << lpad(num(cell_kinds[2]), 12) << "  " << pct(cell_kinds[2], total) << "\n";
	o << pad("(a') block entries / resumes on the dispatcher", 52) << lpad(num(block_entry), 12) << "  " << pct(block_entry, total) << "\n";
	o << pad("(b) typed op_t_* on the general form", 52) << lpad(num(typed), 12) << "  " << pct(typed, total) << "\n";
	o << pad("(c) general form, untyped", 52) << lpad(num(untyped), 12) << "  " << pct(untyped, total) << "\n\n";
	o << "-- cell operations by family\n";
	{
		std::vector<std::pair<uint64_t, size_t>> v;
		for (size_t k = 0; k < static_cast<size_t>(fam::count); ++k) if (cell_fams[k]) v.emplace_back(cell_fams[k], k);
		std::sort(v.rbegin(), v.rend());
		for (const auto& [n, k] : v) o << "  " << pad(fam_name(static_cast<fam>(k)), 50) << lpad(num(n), 12) << "  " << pct(n, total) << "\n";
	}
	o << "\n-- general-form operations by instruction family (b typed | c untyped)\n";
	{
		std::vector<std::pair<uint64_t, size_t>> v;
		for (size_t k = 0; k < static_cast<size_t>(fam::count); ++k) if (gen_fams_typed[k] + gen_fams_untyped[k]) v.emplace_back(gen_fams_typed[k] + gen_fams_untyped[k], k);
		std::sort(v.rbegin(), v.rend());
		for (const auto& [n, k] : v) o << "  " << pad(fam_name(static_cast<fam>(k)), 50) << lpad(num(n), 12) << "  " << pct(n, total) << "   b " << num(gen_fams_typed[k]) << " | c " << num(gen_fams_untyped[k]) << "\n";
	}
	std::vector<std::pair<uint64_t, std::string>> ranked;
	for (const auto& [k, a] : reasons) ranked.emplace_back(a.typed + a.untyped, k);
	std::sort(ranked.rbegin(), ranked.rend());
	o << "\n-- general-form operations by reason (share of all operations; b typed | c untyped; top families)\n";
	for (const auto& [n, k] : ranked) {
		const agg_reason& a = reasons[k];
		std::vector<std::pair<uint64_t, size_t>> fv;
		for (size_t f = 0; f < static_cast<size_t>(fam::count); ++f) if (a.fams[f]) fv.emplace_back(a.fams[f], f);
		std::sort(fv.rbegin(), fv.rend());
		o << "  " << pad(k, 72) << lpad(num(n), 12) << "  " << pct(n, total) << "   b " << num(a.typed) << " | c " << num(a.untyped) << "\n";
		o << "      ";
		for (size_t f = 0; f < fv.size() && f < 3; ++f) o << (f ? "; " : "") << fam_name(static_cast<fam>(fv[f].second)) << " " << pct(fv[f].first, n);
		o << "\n";
	}
	o << "\n-- top sites per reason (function file:line of the attributed site, its detail)\n";
	for (const auto& [n, k] : ranked) {
		const agg_reason& a = reasons[k];
		if (n * 1000 < total) continue;   // reasons under 0.1% of the run list no sites
		o << "  " << k << "  (" << num(n) << ", " << pct(n, total) << ")\n";
		std::vector<std::pair<uint64_t, std::string>> sv;
		for (const auto& [s, c] : a.sites) sv.emplace_back(c, s);
		std::sort(sv.rbegin(), sv.rend());
		for (size_t i = 0; i < sv.size() && i < top; ++i) o << "      " << lpad(num(sv[i].first), 10) << "  " << pct(sv[i].first, total) << "  " << sv[i].second << "\n";
	}
	o << "\n-- member and method operations on the general form by receiver\n";
	{
		static const char* const kn[] = { "script class instance", "host object", "array", "map", "string", "null", "other", "-" };
		uint64_t all = 0; for (int k = 0; k < 7; ++k) all += recv_kinds[k];
		for (int k = 0; k < 7; ++k) if (recv_kinds[k]) o << "  " << pad(kn[k], 50) << lpad(num(recv_kinds[k]), 12) << "  " << pct(recv_kinds[k], all) << " of member ops\n";
		o << "  instance receivers by class shape:\n";
		std::vector<std::pair<uint64_t, std::string>> v;
		for (const auto& [s, c] : recv_shapes) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		for (const auto& [c, s] : v) o << "    " << pad(s, 48) << lpad(num(c), 12) << "  " << pct(c, all) << "\n";
		o << "  top classes:\n";
		v.clear();
		for (const auto& [s, c] : recv_classes) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << pad(v[i].second, 60) << lpad(num(v[i].first), 12) << "  " << pct(v[i].first, total) << "\n";
		o << "  top member sites:\n";
		v.clear();
		for (const auto& [s, c] : recv_sites) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << lpad(num(v[i].first), 10) << "  " << pct(v[i].first, total) << "  " << v[i].second << "\n";
	}
	o << "\n-- untyped arithmetic and comparisons on the general form by operand kinds\n";
	{
		static const char* const kn[] = { "int / int", "float / float", "int / float mixed", "bool", "string", "object (operator overload)", "container", "unknown (environment or element operand)" };
		uint64_t all = 0; for (int k = 0; k < 8; ++k) all += opnd[k];
		for (int k = 0; k < 8; ++k) if (opnd[k]) o << "  " << pad(kn[k], 50) << lpad(num(opnd[k]), 12) << "  " << pct(opnd[k], all) << "\n";
		o << "  scalar operands the checker proved (class 1/2), left general by the lowering's shape rules: " << num(scalar_proven) << "\n";
		o << "  scalar operands the checker left unproven (class 0, BINARY, UNARY): " << num(scalar_unproven) << "\n";
		std::vector<std::pair<uint64_t, std::string>> v;
		for (const auto& [s, c] : proven_scalar_sites) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		o << "  top proven scalar sites left general:\n";
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << lpad(num(v[i].first), 10) << "  " << pct(v[i].first, total) << "  " << v[i].second << "\n";
		v.clear();
		for (const auto& [s, c] : untyped_scalar_sites) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		o << "  top scalar sites the checker left unproven:\n";
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << lpad(num(v[i].first), 10) << "  " << pct(v[i].first, total) << "  " << v[i].second << "\n";
		v.clear();
		for (const auto& [s, c] : string_sites) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		o << "  top string sites:\n";
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << lpad(num(v[i].first), 10) << "  " << pct(v[i].first, total) << "  " << v[i].second << "\n";
	}
	o << "\n-- calls made on the general form by callee (" << num(general_call_count) << " calls; the callee's own instructions count under their own reasons)\n";
	{
		std::vector<std::pair<uint64_t, std::string>> v;
		for (const auto& [s, c] : general_calls) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << lpad(num(v[i].first), 10) << "  " << v[i].second << "\n";
	}
	o << "\n-- blocks by general-form operations inside their span (cells = operations in the block)\n";
	{
		std::sort(loops.begin(), loops.end(), [](const loop_agg& a, const loop_agg& b) { return a.general + a.cells / 64 > b.general + b.cells / 64; });
		for (size_t i = 0; i < loops.size() && i < top; ++i) {
			const loop_agg& a = loops[i];
			o << "  " << pad(a.where + (a.fp ? " [frame program]" : a.straight ? " [region]" : ""), 60) << " general " << lpad(num(a.general), 10) << "  cells " << lpad(num(a.cells), 10)
			  << "  entries " << num(a.entries) << "  misses " << num(a.misses) << "  refused " << num(a.refused[0] + a.refused[1] + a.refused[2] + a.refused[3]);
			if (a.refused[0] + a.refused[1] + a.refused[2] + a.refused[3]) o << " (" << num(a.refused[0]) << "/" << num(a.refused[1]) << "/" << num(a.refused[2]) << "/" << num(a.refused[3]) << ")";
			o << "\n";
		}
	}
	o << "\n-- top cell sites (operations in blocks, by origin line)\n";
	{
		std::vector<std::pair<uint64_t, std::string>> v;
		for (const auto& [s, c] : cell_sites) v.emplace_back(c, s);
		std::sort(v.rbegin(), v.rend());
		for (size_t i = 0; i < v.size() && i < top; ++i) o << "    " << lpad(num(v[i].first), 10) << "  " << pct(v[i].first, total) << "  " << v[i].second << "\n";
	}
	return o.str();
}

} // namespace jai::vm

#endif // JAISCRIPT_VM_COVERAGE
