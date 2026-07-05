#include <jaiscript/vm/disassembler.hpp>
#include <sstream>

namespace jai::vm {

namespace {

	std::string constant_text(const script_value& v) {
		switch (v.raw_storage_index()) {
			case script_value::TYPEID_NULL: return "null";
			case script_value::TYPEID_INT: return std::to_string(v.unchecked_as_int());
			case script_value::TYPEID_FLOAT: return std::to_string(v.unchecked_as_float());
			case script_value::TYPEID_STRING: return "\"" + std::string(v.unchecked_as_string()) + "\"";
			case script_value::TYPEID_BOOL: return v.unchecked_as_bool() ? "true" : "false";
			case script_value::TYPEID_CHAR: return std::string("'") + v.unchecked_as_char() + "'";
			case script_value::TYPEID_CPP_BOUND: return "<cpp_bound>";  // constants are never bound; label for safety
			default: return "<value>";
		}
	}

} // namespace

std::string disassemble(const chunk& ch, const string_symbolizer* symbolizer) {
	std::ostringstream out;
	out << "== " << (ch.function_name.empty() ? "<script>" : ch.function_name)
	    << " (" << ch.code.size() << " instructions, " << ch.constants.size() << " constants) ==\n";

	for (size_t i = 0; i < ch.code.size(); ++i) {
		const vm_instruction& ins = ch.code[i];
		const ast_node* node = i < ch.stmt_nodes.size() ? ch.stmt_nodes[i] : nullptr;

		out << (i < 10 ? "000" : (i < 100 ? "00" : (i < 1000 ? "0" : ""))) << i << "  ";
		if (node) {
			out << "L" << node->location.line << "\t";
		} else {
			out << "L?\t";
		}
		out << opcode_name(ins.op);

		switch (ins.op) {
			case opcode::op_const:
				out << " " << ins.a;
				if (ins.a < ch.constants.size()) {
					out << " (" << constant_text(ch.constants[ins.a]) << ")";
				}
				break;
			case opcode::op_load:
			case opcode::op_incdec: {
				const uint32_t sym_idx = ins.op == opcode::op_load ? ins.b : ins.a;
				const uint32_t slot = ins.op == opcode::op_load ? ins.a : ins.b;
				out << " slot=";
				if (slot == k_invalid_u32) out << "env";
				else out << slot;
				if (sym_idx < ch.symbols.size() && symbolizer) {
					out << " '" << symbolizer->get_string(ch.symbols[sym_idx]) << "'";
				}
				if (ins.c) out << " flags=" << ins.c;
				break;
			}
			case opcode::op_store:
			case opcode::op_compound_store: {
				out << " slot=";
				if (ins.b == k_invalid_u32) out << "env";
				else out << ins.b;
				if (ins.a < ch.symbols.size() && symbolizer) {
					out << " '" << symbolizer->get_string(ch.symbols[ins.a]) << "'";
				}
				out << " flags=" << ins.c;
				break;
			}
			case opcode::op_binary:
				out << " op=" << ins.a << " shape=" << ins.b;
				break;
			case opcode::op_binary_fused:
				if (ins.a < ch.fused_binary_protos.size()) {
					out << " op=" << static_cast<uint32_t>(ch.fused_binary_protos[ins.a].op);
				}
				out << " proto=" << ins.a << " shape=" << ins.b;
				break;
			case opcode::op_unary:
				out << " op=" << ins.a;
				break;
			case opcode::op_fused_cmp_jump:
				if (ins.b < ch.fused_binary_protos.size()) {
					out << " op=" << static_cast<uint32_t>(ch.fused_binary_protos[ins.b].op);
				}
				out << " proto=" << ins.b << " -> " << ins.a << " proved=" << ins.c;
				break;
			case opcode::op_jump:
			case opcode::op_jump_if_false:
			case opcode::op_jump_if_true:
			case opcode::op_loop_back:
				out << " -> " << ins.a;
				break;
			case opcode::op_call:
			case opcode::op_call_method:
				out << " argc=" << ins.a << " site=" << ins.b;
				break;
			case opcode::op_get_member:
			case opcode::op_get_static:
			case opcode::op_set_member:
			case opcode::op_set_static:
			case opcode::op_member_compound:
			case opcode::op_new:
			case opcode::op_class_decl:
			case opcode::op_namespace_decl:
			case opcode::op_enum_decl:
				out << " node=" << ins.a;
				break;
			case opcode::op_null_guard:
				out << " -> " << ins.a;
				break;
			case opcode::op_try_push:
				out << " handler=" << ins.a;
				if (ins.b < ch.symbols.size() && symbolizer) {
					out << " catch='" << symbolizer->get_string(ch.symbols[ins.b]) << "'";
				}
				break;
			case opcode::op_throw:
				out << (ins.a ? " value" : " rethrow");
				break;
			case opcode::op_yield:
				out << (ins.a ? " value" : " void");
				break;
			case opcode::op_include:
			case opcode::op_import:
				if (ins.b) {
					out << " <expr>";
				} else if (ins.a != k_invalid_u32 && ins.a < ch.messages.size()) {
					out << " \"" << ch.messages[ins.a] << "\"";
				}
				break;
			case opcode::op_iter_init:
			case opcode::op_iter_next:
				out << " proto=" << ins.a;
				if (ins.a < ch.iter_protos.size() && symbolizer) {
					out << " '" << symbolizer->get_string(ch.iter_protos[ins.a].var_symbol) << "'";
					if (ch.iter_protos[ins.a].is_reference) out << " byref";
				}
				break;
			case opcode::op_from_this:
				out << (ins.a ? " weak" : " shared");
				break;
			case opcode::op_func_decl:
				out << " proto=" << ins.a;
				if (ins.a < ch.function_protos.size() && ch.function_protos[ins.a].decl) {
					out << " '" << ch.function_protos[ins.a].decl->name << "'";
				}
				break;
			case opcode::op_closure:
				out << " proto=" << ins.a;
				break;
			case opcode::op_array:
			case opcode::op_map:
			case opcode::op_scope_pop_n:
			case opcode::op_return:
				out << " " << ins.a;
				break;
			case opcode::op_error:
				out << " code=" << ins.a;
				if (ins.b != k_invalid_u32 && ins.b < ch.messages.size()) {
					out << " \"" << ch.messages[ins.b] << "\"";
				}
				break;
			default:
				break;
		}
		out << "\n";
	}

	for (size_t i = 0; i < ch.closure_protos.size(); ++i) {
		if (ch.closure_protos[i].body_chunk) {
			out << "\n-- closure proto " << i << " --\n";
			out << disassemble(*ch.closure_protos[i].body_chunk, symbolizer);
		}
	}
	return out.str();
}

} // namespace jai::vm
