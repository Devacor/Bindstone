#include <jaiscript/vm/vm_backend.hpp>
#include <jaiscript/detail/null_member_diagnostic.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/coroutine.hpp>
#include <jaiscript/detail/integer_ops.hpp>
#include <jaiscript/detail/char_promotion.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/operator_table.hpp>
#include <jaiscript/detail/ref_lvalue.hpp>
#include <jaiscript/detail/structural_equality.hpp>
#include <jaiscript/detail/string_mutator_receiver.hpp>
#include <jaiscript/detail/capture_ladder.hpp>
#include <jaiscript/detail/cast_kernel.hpp>   // cast<T>/cast<T&>/cast<shared_ptr<T>> kernel (interp parity)
#include <jaiscript/detail/static_store.hpp>  // class-qualified static stores (interp parity)
#include <jaiscript/detail/container_enforce.hpp>   // typed-container boundary kernel (interp parity)
#include <jaiscript/detail/type_proofs.hpp>   // parse-time type proof guards (interp parity)
#include <jaiscript/detail/typed_slot.hpp>   // the one typed-slot conversion table (interp parity)
#include <jaiscript/detail/range_for_diagnostic.hpp>   // range-for texts (interp parity)
#ifdef JAISCRIPT_VM_PROFILE
#include <intrin.h>   // __rdtsc (diagnostic builds only; keep out of headers)

// Op-layer TRY twins: callees returning op_status pass through vm_check; fat-returning
// helpers route their failure into pending_error_ (see raise_from).

#include <algorithm>
#endif
#include <jaiscript/detail/parallel_transform.hpp>   // parallel_borrow_subscript_read (captured reads)
#include <jaiscript/detail/ast_serializer.hpp>   // structural_node_key (hot-reload identity)
#include <jaiscript/debug/controller.hpp>   // step-debugger statement hook (phase 5)
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

#define VM_TRY(expr) 	do { 		if (vm_check((expr)) == op_status::failed) [[unlikely]] { 			return op_status::failed; 		} 	} while(0)
#define VM_TRY_ASSIGN(var, expr) 	auto JAISCRIPT_CONCAT(__vm_result_, __LINE__) = (expr); 	if (!JAISCRIPT_CONCAT(__vm_result_, __LINE__)) [[unlikely]] { 		return raise_from(JAISCRIPT_CONCAT(__vm_result_, __LINE__)); 	} 	var = std::move(JAISCRIPT_CONCAT(__vm_result_, __LINE__).value());

#ifdef JAISCRIPT_VM_PROFILE
#define JAI_ENV_CENSUS(i) ++profile_scope_kinds_[i]
#else
#define JAI_ENV_CENSUS(i) ((void)0)
#endif
namespace jai::vm {

namespace {

	inline checked_result<void> vm_int_overflow_v(const char* msg) {
		return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
	}
	inline checked_result<script_value> vm_int_overflow_sv(const char* msg) {
		return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
	}

	// Twin-parity zero-divisor check for cpp-bound operands: index 14 skips the raw-index
	// gates of binary_fast_shape/exec_binary_fused, which would move a bound-zero divisor's
	// error from the fast-path surface onto the handlers' bare "Division by zero". Each shape
	// passes the exact (op x type-pair) coverage of its own inline zero checks so bound
	// operands keep the plain twin's error surface. Byte-parallel with interpreter.cpp's copy.
	struct bound_zero_divisor_error { runtime_error_code code; const char* text; };
	inline std::optional<bound_zero_divisor_error> bound_fastpath_zero_divisor(
			token_type op, const script_value& left, const script_value& right,
			bool float_div_covered, bool float_mod_covered) {
		if (op != token_type::slash && op != token_type::percent) return std::nullopt;
		if (left.is_int() && right.is_int()) {
			if (right.unchecked_as_int() != 0) return std::nullopt;
			return op == token_type::slash
				? bound_zero_divisor_error{runtime_error_code::division_by_zero, "Division by zero in integer operation"}
				: bound_zero_divisor_error{runtime_error_code::modulo_by_zero, "Modulo by zero in integer operation"};
		}
		if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
			if (!(op == token_type::slash ? float_div_covered : float_mod_covered)) return std::nullopt;
			const script_float rf = right.is_int() ? static_cast<script_float>(right.unchecked_as_int()) : right.unchecked_as_float();
			if (rf != 0.0) return std::nullopt;
			return op == token_type::slash
				? bound_zero_divisor_error{runtime_error_code::division_by_zero, "Division by zero in float operation"}
				: bound_zero_divisor_error{runtime_error_code::modulo_by_zero, "Modulo by zero in float operation"};
		}
		return std::nullopt;
	}

	std::string vm_type_name_of(script_value_type type) {
		switch (type) {
			case script_value_type::jai_null_type: return "null";
			case script_value_type::jai_int_type: return "int";
			case script_value_type::jai_float_type: return "float";
			case script_value_type::jai_string_type: return "string";
			case script_value_type::jai_char_type: return "char";
			case script_value_type::jai_bool_type: return "bool";
			case script_value_type::jai_array_type: return "array";
			case script_value_type::jai_map_type: return "map";
			case script_value_type::jai_object_type: return "object";
			case script_value_type::jai_function_type: return "function";
			case script_value_type::jai_reference_type: return "reference";
			case script_value_type::jai_shared_ptr_type: return "shared_ptr";
			case script_value_type::jai_weak_ptr_type: return "weak_ptr";
			case script_value_type::jai_any_type: return "any";
			case script_value_type::jai_invalid_type: return "invalid";
			default: return "unknown";
		}
	}

	std::string vm_value_type_name(const script_value& val) {
		return detail::container_value_type_name(val);
	}

	std::string vm_type_info_name(type_info_ptr info) {
		return info ? info->canonical_name() : "unknown";
	}

	// Element compatibility/conversion live in the shared kernel
	// (detail/container_enforce.hpp, used verbatim by both backends and the field
	// kernel). These forwards keep the historical local names.
	bool vm_is_element_type_compatible(const script_value& element, type_info_ptr element_type, script_value& array_owner) {
		return detail::container_element_compatible(element, element_type, array_owner.get_engine());
	}

	checked_result<script_value> vm_convert_array_element(engine* eng, const script_value& element, type_info_ptr element_type) {
		return detail::convert_container_element(eng, element, element_type);
	}

	script_value vm_clone_for_capture(const script_value& value, string_symbolizer* symbolizer) {
		auto type_info = value.get_type_info();
		if (type_info && type_info->base_type == script_value_type::jai_shared_ptr_type) {
			return value;
		}
		if (value.is_object()) {
			auto holder = const_cast<script_value&>(value).get_object_holder();
			if (holder) {
				if (!holder->is_class_instance_wrapper) {
					return value;
				}
				auto instance = std::static_pointer_cast<class_instance>(holder->data);
				if (instance && symbolizer && instance->has_field(symbolizer->intern(class_constants::CPP_OBJECT_FIELD))) {
					return value;
				}
			}
		}
		return value.clone();
	}

	// Decl-ref (auto&/T& x = y) aliasing of a value that is already a reference: SHARE
	// the holder, constraint included. RULED (2026-07-06): element/field/map-entry refs
	// keep their bind-time type constraint through a ref decl — auto& over an
	// array<int> element enforces int on store exactly like direct element assignment
	// (reverses the e43f8a6f constraint-dropping pin). Sharing also keeps container
	// re-resolution (realloc/shrink safe). KEEP BYTE-PARALLEL with decl_ref_alias.
	checked_result<script_value> vm_decl_ref_alias(const script_value& source, engine* eng) {
		(void)eng;
		auto refHolder = source.get_reference_holder();
		if (!refHolder) {
			return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference target is null");
		}
		return script_value(source);
	}

	checked_result<void> vm_validate_container_homogeneous(const script_value& container, const std::string& path,
	                                                       const detail::arity_count_symbols& counts, const string_symbolizer& symbols);

	checked_result<void> vm_validate_map_homogeneous(const script_value& map_value, const std::string& path,
	                                                 const detail::arity_count_symbols& counts, const string_symbolizer& symbols) {
		if (!map_value.is_map()) {
			return {};
		}
		const auto& map = map_value.as_map();
		if (map.size() <= 1) {
			if (map.size() == 1) {
				const script_value& first_val = map.begin()->second.is_reference() ?
					map.begin()->second.deref() : map.begin()->second;
				JAISCRIPT_TRY(vm_validate_container_homogeneous(first_val, path + "[0]", counts, symbols));
			}
			return {};
		}

		auto first_it = map.begin();
		const script_value& first_val = first_it->second.is_reference() ? first_it->second.deref() : first_it->second;
		script_value_type deduced_type = first_val.type();

		std::string deduced_class_name;
		if (deduced_type == script_value_type::jai_object_type) {
			auto type_info = first_val.get_type_info();
			if (type_info) {
				deduced_class_name = type_info->type_name;
			}
		}

		JAISCRIPT_TRY(vm_validate_container_homogeneous(first_val, path + "[first]", counts, symbols));

		size_t idx = 1;
		for (auto it = ++map.begin(); it != map.end(); ++it, ++idx) {
			const script_value& val = it->second.is_reference() ? it->second.deref() : it->second;
			script_value_type val_type = val.type();

			if (val_type != deduced_type) {
				return detail::homogeneity_error(runtime_error_code::map_value_type_mismatch,
					detail::auto_map_values, detail::auto_map_values_at, idx, counts, symbols);
			}

			if (deduced_type == script_value_type::jai_object_type && !deduced_class_name.empty()) {
				auto val_type_info = val.get_type_info();
				std::string val_class_name = val_type_info ? val_type_info->type_name : "";
				if (val_class_name != deduced_class_name) {
					return detail::homogeneity_error(runtime_error_code::map_value_type_mismatch,
						detail::auto_map_classes, detail::auto_map_classes_at, idx, counts, symbols);
				}
			}

			JAISCRIPT_TRY(vm_validate_container_homogeneous(val, path + "[" + std::to_string(idx) + "]", counts, symbols));
		}
		return {};
	}

	checked_result<void> vm_validate_array_homogeneous(const script_value& array_value, const std::string& path,
	                                                   const detail::arity_count_symbols& counts, const string_symbolizer& symbols) {
		if (!array_value.is_array()) {
			return {};
		}
		if (array_value.unchecked_array_node()->is_typed()) {
			return {};   // raw int/float buffers are homogeneous by construction
		}
		const auto& elements = array_value.as_array();
		if (elements.size() <= 1) {
			if (elements.size() == 1) {
				const script_value& first = elements[0].is_reference() ? elements[0].deref() : elements[0];
				JAISCRIPT_TRY(vm_validate_container_homogeneous(first, path + "[0]", counts, symbols));
			}
			return {};
		}

		const script_value& first = elements[0].is_reference() ? elements[0].deref() : elements[0];
		script_value_type deduced_type = first.type();

		std::string deduced_class_name;
		if (deduced_type == script_value_type::jai_object_type) {
			auto type_info = first.get_type_info();
			if (type_info) {
				deduced_class_name = type_info->type_name;
			}
		}

		JAISCRIPT_TRY(vm_validate_container_homogeneous(first, path + "[0]", counts, symbols));

		for (size_t i = 1; i < elements.size(); ++i) {
			const script_value& elem = elements[i].is_reference() ? elements[i].deref() : elements[i];
			script_value_type elem_type = elem.type();

			if (elem_type != deduced_type) {
				return detail::homogeneity_error(runtime_error_code::array_element_type_mismatch,
					detail::auto_array_elements, detail::auto_array_elements_at, i, counts, symbols);
			}

			if (deduced_type == script_value_type::jai_object_type && !deduced_class_name.empty()) {
				auto elem_type_info = elem.get_type_info();
				std::string elem_class_name = elem_type_info ? elem_type_info->type_name : "";
				if (elem_class_name != deduced_class_name) {
					return detail::homogeneity_error(runtime_error_code::array_element_type_mismatch,
						detail::auto_array_classes, detail::auto_array_classes_at, i, counts, symbols);
				}
			}

			JAISCRIPT_TRY(vm_validate_container_homogeneous(elem, path + "[" + std::to_string(i) + "]", counts, symbols));
		}
		return {};
	}

	checked_result<void> vm_validate_container_homogeneous(const script_value& container, const std::string& path,
	                                                       const detail::arity_count_symbols& counts, const string_symbolizer& symbols) {
		if (container.is_array()) {
			return vm_validate_array_homogeneous(container, path, counts, symbols);
		} else if (container.is_map()) {
			return vm_validate_map_homogeneous(container, path, counts, symbols);
		}
		return {};
	}

} // namespace

struct vm_backend::frame_guard {
	vm_backend* vm;
	size_t try_base;
	size_t iter_base;
	size_t cfor_base;
	frame_guard(vm_backend* backend, frame* f)
		: vm(backend), try_base(backend->try_records_.size()), iter_base(backend->iter_states_.size()),
		  cfor_base(backend->cfor_states_.size()) {
		vm->frames_.push_back(f);
	}
	~frame_guard() {
		vm->frames_.pop_back();
		if (vm->try_records_.size() > try_base) {
			vm->try_records_.erase(vm->try_records_.begin() + try_base, vm->try_records_.end());
		}
		if (vm->iter_states_.size() > iter_base) {
			vm->iter_states_.erase(vm->iter_states_.begin() + iter_base, vm->iter_states_.end());
		}
		if (vm->cfor_states_.size() > cfor_base) {
			vm->cfor_states_.erase(vm->cfor_states_.begin() + cfor_base, vm->cfor_states_.end());
		}
	}
};

vm_backend::vm_backend(string_symbolizer* symbolizer, std::shared_ptr<environment> global_env)
	: symbolizer_(symbolizer), env_symbolizer_(symbolizer), environment_(std::move(global_env)), compiler_(symbolizer) {
	cached_global_env_ = environment_.get();
	stack_.owner = this;
	stack_.reserve(256);
	frames_.reserve(64);
}

void vm_backend::set_engine_reference(engine* engine_ref) {
	engine_ = engine_ref;
	if (engine_) {
		symbolizer_ = engine_->get_symbolizer();
		env_symbolizer_ = symbolizer_;
		if (auto global = engine_->get_global_environment()) {
			environment_ = global;
			cached_global_env_ = global.get();   // stable for the engine's lifetime
		}
		compiler_ = vm_compiler(symbolizer_, &builtins_);   // filled below; address stable

		op_plus_id_ = symbolizer_->intern("+");
		op_minus_id_ = symbolizer_->intern("-");
		op_star_id_ = symbolizer_->intern("*");
		op_slash_id_ = symbolizer_->intern("/");
		op_percent_id_ = symbolizer_->intern("%");
		op_less_id_ = symbolizer_->intern("<");
		op_less_equal_id_ = symbolizer_->intern("<=");
		op_greater_id_ = symbolizer_->intern(">");
		op_greater_equal_id_ = symbolizer_->intern(">=");
		op_equal_equal_id_ = symbolizer_->intern("==");
		op_bang_equal_id_ = symbolizer_->intern("!=");
		op_spaceship_id_ = symbolizer_->intern("<=>");
		op_ampersand_id_ = symbolizer_->intern("&");
		op_pipe_id_ = symbolizer_->intern("|");
		op_caret_id_ = symbolizer_->intern("^");
		op_left_shift_id_ = symbolizer_->intern("<<");
		op_right_shift_id_ = symbolizer_->intern(">>");
		builtin_push_id_ = symbolizer_->intern("push");   // interned HERE: symbolizer freezes during parallel regions
		this_id_ = symbolizer_->get_this_id();
		subscript_op_id_ = symbolizer_->intern("[]");
		assign_operator_id_ = symbolizer_->intern("=");
		to_string_id_ = symbolizer_->intern("to_string");
		to_bool_id_ = symbolizer_->intern("to_bool");
		eq_method_id_ = symbolizer_->intern("==");
		cpp_object_field_id_ = symbolizer_->intern(class_constants::CPP_OBJECT_FIELD);
		same_as_id_ = symbolizer_->intern("same_as");
		coroutine_handle_type_id_ = symbolizer_->intern("coroutine_handle");
		resume_id_ = symbolizer_->intern("resume");
		done_id_ = symbolizer_->intern("done");
		class_definition_type_id_ = symbolizer_->intern("class_definition");
		hot_reload_migrate_id_ = symbolizer_->intern("hot_reload_migrate");
		pair_id_ = symbolizer_->intern("pair");
		base_type_ids_.intern(*symbolizer_);
		arity_count_ids_.intern(*symbolizer_);

		builtins_ = {};
		init_builtin_method_registries(symbolizer_, builtins_);
	}
}

script_value vm_backend::make_null() const {
	return script_value(std::monostate{}, engine_);
}

vm_backend::value_stack::~value_stack() {
	truncate(0);
	::operator delete(slots);
}

// The ONE buffer move: script_value relocates by memcpy (storage relocation runs no
// effectful ctor/dtor; the strong_ptr refcounts ride the bytes), then the sanctioned
// cross-op raw-pointer cache (counted-for fast states, invariants 2b) rebases.
void vm_backend::value_stack::relocate_to(size_t new_cap) {
	script_value* fresh = static_cast<script_value*>(::operator new(new_cap * sizeof(script_value)));
	if (slots) {
		std::memcpy(fresh, slots, count * sizeof(script_value));
		if (owner) {
			owner->rebase_window_pointers(slots, slots + count, fresh);
		}
		::operator delete(slots);
	}
	slots = fresh;
	cap = new_cap;
}

void vm_backend::value_stack::reserve(size_t want) {
	if (want > cap) {
		relocate_to(want);
	}
}

// Growth chokepoint (stage 2): the pushed value detours through the temp param so
// self-referential pushes (op_dup pushing back()) survive the relocation.
void vm_backend::value_stack::grow_push(script_value x) {
	relocate_to(cap < 2048 ? 4096 : cap * 2);
	new (slots + count) script_value(std::move(x));
	++count;
}

void vm_backend::rebase_window_pointers(const script_value* old_begin, const script_value* old_end,
                                        script_value* new_begin) {
	// Byte-offset rebase: var points at a whole slot, end/step point INSIDE a slot's
	// int payload - both preserve their offset from the buffer head.
	const char* ob = reinterpret_cast<const char*>(old_begin);
	const char* oe = reinterpret_cast<const char*>(old_end);
	char* nb = reinterpret_cast<char*>(new_begin);
	auto rebase = [&](auto*& ptr) {
		const char* pc = reinterpret_cast<const char*>(ptr);
		if (pc >= ob && pc < oe) {
			ptr = reinterpret_cast<std::remove_reference_t<decltype(ptr)>>(nb + (pc - ob));
		}
	};
	for (auto& cs : cfor_states_) {
		rebase(cs.var);
		rebase(cs.end_ptr);
		rebase(cs.step_ptr);
	}
}

detail::caller_frame_view vm_backend::caller_view(frame* f) const {
	if (!f) { return {}; }
	if (f->window_backed && f->window_live > 0) {
		return detail::caller_frame_view{f->locals, &stack_[f->window_base],
		                                 static_cast<size_t>(f->window_live)};
	}
	return detail::caller_frame_view{f->locals};
}

void vm_backend::arm_execution_deadline() {
	budget_tick_ = 0;
	budget_active_ = execution_budget_.count() > 0;
	if (budget_active_) {
		execution_deadline_ = std::chrono::steady_clock::now() + execution_budget_;
	}
}

bool vm_backend::execution_budget_exhausted() {
	if (++budget_tick_ < 1024) [[likely]] { return false; }
	budget_tick_ = 0;
	// Off-cycle debug sync (twin of the interpreter's): the only mid-run point where this
	// thread notices attach/detach/breakpoint edits — one relaxed load when no debugger
	// was ever constructed. Ticks count even for budget-0 hosts so long scripts still sync.
	if (debug_controller_.load(std::memory_order_relaxed) || debug_hook_) [[unlikely]] {
		sync_debug_hook();
	}
	// An armed debug session suspends the wall-clock deadline (a parked script must not
	// be killed by the act of debugging); sync_debug_hook re-arms fresh on session end.
	if (!budget_active_ || debug_hook_) { return false; }
	if (std::chrono::steady_clock::now() < execution_deadline_) { return false; }
	// Budget overruns are TERMINAL: no script catch may swallow a timeout
	limits_->terminal_error = true;
	return true;
}

void vm_backend::sync_debug_hook() {
	auto* dbg = debug_controller_.load(std::memory_order_acquire);
	debug::controller* next = (dbg && dbg->sync_hot_state()) ? dbg : nullptr;
	if (debug_hook_ && !next) { arm_execution_deadline(); }
	debug_hook_ = next;
}

// Statement boundary: the stamped node changed, we entered a different chunk, or a
// backward jump re-entered the same statement (ip <= the ip the hook last fired at in
// this frame — loop iterations re-fire, straight-line ops inside one statement don't).
void vm_backend::debug_statement_boundary(frame& f) {
	const ast_node* sn = f.ip < f.code->stmt_nodes.size() ? f.code->stmt_nodes[f.ip] : nullptr;
	if (!sn) { return; }
	if (sn == f.debug_stmt && f.ip > f.debug_stmt_ip) { return; }
	f.debug_stmt = sn;
	f.debug_stmt_ip = f.ip;
	if (debug_hook_->wants_statement(static_cast<uint32_t>(sn->location.line))) {
		debug_paused_frame_ = &f;
		debug_hook_->on_statement(sn,
			static_cast<int>(call_records_top_ + static_cast<size_t>(current_call_depth_)));
		debug_paused_frame_ = nullptr;
	}
}

std::vector<std::pair<std::string, script_value>> vm_backend::get_current_frame_locals() const {
	std::vector<std::pair<std::string, script_value>> out;
	const frame* f = debug_paused_frame_;
	if (!f && call_records_top_ > 0) { f = &call_records_[call_records_top_ - 1]->f; }
	if (!f || !f->locals || f->top_level || !f->code) { return out; }   // global scope: env fallback

	// slot -> name from the chunk's operands: decls carry their variable_decl node
	// (exact name + slot); load/store/incdec name any slot the body touches (parameters
	// included — a parameter the body never reads is invisible here, documented).
	std::vector<std::pair<size_t, std::string_view>> slot_names;
	for (size_t ip = 0; ip < f->code->code.size(); ++ip) {
		const vm_instruction& ins = f->code->code[ip];
		switch (ins.op) {
		case opcode::op_decl_var:
		case opcode::op_decl_ref_ident:
		case opcode::op_decl_ref_value: {
			const auto* vd = static_cast<const variable_decl*>(f->code->nodes[ins.a].get());
			if (vd && vd->slot_index != SIZE_MAX) { slot_names.emplace_back(vd->slot_index, vd->name); }
			break;
		}
		// typed moves replaced a LOAD/CONST + DECL_VAR/STORE pair: name what the pair named
		case opcode::op_t_decl_mov:
		case opcode::op_t_decl_const: {
			const auto* vd = static_cast<const variable_decl*>(f->code->nodes[ins.c].get());
			if (vd && vd->slot_index != SIZE_MAX) { slot_names.emplace_back(vd->slot_index, vd->name); }
			if (ins.op == opcode::op_t_decl_mov && ip < f->code->typed_sites.size()) {
				const vm_instruction& load = f->code->typed_sites[ip].orig;
				if (load.a != k_invalid_u32 && load.b < f->code->symbols.size()) { slot_names.emplace_back(load.a, symbolizer_->get_string(f->code->symbols[load.b])); }
			}
			break;
		}
		case opcode::op_t_load: {
			if (ip < f->code->typed_sites.size()) {
				const vm_instruction& orig = f->code->typed_sites[ip].orig;
				const auto* vd = static_cast<const variable_decl*>(f->code->nodes[orig.c].get());
				if (vd && vd->slot_index != SIZE_MAX) { slot_names.emplace_back(vd->slot_index, vd->name); }
			}
			break;
		}
		case opcode::op_t_store_mov:
		case opcode::op_t_store_const: {
			if (ip < f->code->typed_sites.size()) {
				const auto& site = f->code->typed_sites[ip];
				if (site.orig2.a < f->code->symbols.size()) { slot_names.emplace_back(ins.a, symbolizer_->get_string(f->code->symbols[site.orig2.a])); }
				if (ins.op == opcode::op_t_store_mov && site.orig.a != k_invalid_u32 && site.orig.b < f->code->symbols.size()) {
					slot_names.emplace_back(site.orig.a, symbolizer_->get_string(f->code->symbols[site.orig.b]));
				}
			}
			break;
		}
		case opcode::op_index_fused_decl: {
			const auto* vd = static_cast<const variable_decl*>(f->code->nodes[ins.c].get());
			if (vd && vd->slot_index != SIZE_MAX) { slot_names.emplace_back(vd->slot_index, vd->name); }
			break;
		}
		case opcode::op_load:
			if (ins.a != k_invalid_u32 && ins.b < f->code->symbols.size()) {
				slot_names.emplace_back(ins.a, symbolizer_->get_string(f->code->symbols[ins.b]));
			}
			break;
		case opcode::op_store:
		case opcode::op_compound_store:
		case opcode::op_incdec:
			if (ins.b != k_invalid_u32 && ins.a < f->code->symbols.size()) {
				slot_names.emplace_back(ins.b, symbolizer_->get_string(f->code->symbols[ins.a]));
			}
			break;
		default: break;
		}
	}
	// Fused superinstructions carry identifier operands in side tables, not op_loads —
	// a parameter referenced only inside `a + b` is named here.
	auto note_operand = [&](const fused_operand& o) {
		if (o.slot != k_invalid_u32 && o.symbol != k_invalid_u32 && o.symbol < f->code->symbols.size()) {
			slot_names.emplace_back(o.slot, symbolizer_->get_string(f->code->symbols[o.symbol]));
		}
	};
	for (const auto& p : f->code->fused_binary_protos) { note_operand(p.left); note_operand(p.right); }
	for (const auto& cs : f->code->call_sites) { note_operand(cs.callee); }
	for (const auto& p : f->code->counted_for_protos) { note_operand(p.var); note_operand(p.end); note_operand(p.step); }
	for (const auto& p : f->code->compound_fused_protos) {
		if (p.slot != k_invalid_u32 && p.symbol < f->code->symbols.size()) {
			slot_names.emplace_back(p.slot, symbolizer_->get_string(f->code->symbols[p.symbol]));
		}
		note_operand(p.rhs);   // bare-mode identifier rhs (binary-mode protos leave it invalid)
	}
	for (const auto& p : f->code->fused_index_protos) { note_operand(p.container); note_operand(p.index); }
	for (const auto& p : f->code->fused_binary_dst_protos) {
		// dest-addressed binaries: decl mode names via the decl node, store mode via
		// symbol+slot (their operands are fused_binary_protos entries, named above)
		if (p.node_index != k_invalid_u32) {
			const auto* vd = static_cast<const variable_decl*>(f->code->nodes[p.node_index].get());
			if (vd && vd->slot_index != SIZE_MAX) { slot_names.emplace_back(vd->slot_index, vd->name); }
		} else if (p.slot != k_invalid_u32 && p.symbol < f->code->symbols.size()) {
			slot_names.emplace_back(p.slot, symbolizer_->get_string(f->code->symbols[p.symbol]));
		}
	}
	for (const auto& p : f->code->iter_protos) {
		if (p.slot != SIZE_MAX && p.var_symbol != UINT64_MAX) {
			slot_names.emplace_back(p.slot, symbolizer_->get_string(p.var_symbol));
		}
	}
	for (const auto& p : f->code->destructure_protos) {
		for (const auto& [sym_id, slot] : p.names) {
			if (slot != SIZE_MAX) { slot_names.emplace_back(slot, symbolizer_->get_string(sym_id)); }
		}
	}
	std::sort(slot_names.begin(), slot_names.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });
	size_t last = SIZE_MAX;
	for (const auto& [slot, name] : slot_names) {
		if (slot == last) { continue; }
		last = slot;
		const script_value* v = frame_slot(*f, slot);
		if (!v) { continue; }                            // reserved capacity only: not live yet
		out.emplace_back(std::string(name), *v);
	}
	return out;
}

bool vm_backend::execution_limit_exhausted() {
	return execution_budget_exhausted() || limits_->memory_tripped();
}

error_propagator vm_backend::execution_limit_failure() {
	if (limits_->memory_tripped()) {
		return detail::raise_memory_cap(*limits_);
	}
	return error_propagator{
		make_error_code(runtime_error_code::execution_budget_exceeded),
		"Script execution budget exceeded - raise engine::execution_budget or break up the work"};
}

void vm_backend::set_execution_budget(std::chrono::nanoseconds budget) {
	execution_budget_ = budget;
	if (budget.count() <= 0) { budget_active_ = false; }
}

void vm_backend::prepare_for_execution() {
	if (!is_executing()) {
		instance_scope_saved_.clear();
		pending_instance_env_.reset();
	}
	sync_debug_hook();   // execute-entry debug sync (attach/detach/breakpoint edits)
	arm_execution_deadline();

	// Share the engine's limit state: reentrant executes see the outer run's terminal
	// latch, so a terminal error crosses the reentrant boundary uncaught
	if (engine_) {
		limits_ = &engine_->execution_limits();
	}

	// A re-entrant execute (include/import, or a host callback calling execute() mid-run)
	// stacks a fresh frame above the live ones — leave the outer frames' value/try/iter
	// slices intact so the outer execution (or a running fiber) resumes uncorrupted.
	const bool reentrant = is_executing();

	return_value_.reset();
	has_return_value_ = false;
	implicit_result_.reset();

	current_exception_.reset();
	is_unwinding_ = false;
	trace_captured_ = false;
	active_exception_value_.reset();
	current_catch_var_id_ = 0;

	if (!reentrant) {
		stack_.clear();
		try_records_.clear();
		iter_states_.clear();
		cfor_states_.clear();
		// Terminal latch (and memory allowance) reset only at a NON-reentrant entry: a
		// nested execute must not grant the outer run's unwinding error a second life
		limits_->reset_for_execute();
	}

	// Preserve a running coroutine's fiber so its next yield still finds its handle;
	// otherwise the failed yield would hand the caller a stale prior value.
	if (!active_coroutine_ || active_coroutine_->get_status() != coroutine_handle::status::running) {
		active_coroutine_ = nullptr;
		yielding_ = false;
	}

	// The outer run may be suspended inside a ctor/method scope env holding 'this';
	// run_program swaps the nested program to the global env and restores this one.
	if (reentrant) {
		return;
	}

	if (engine_) {
		if (auto global = engine_->get_global_environment()) {
			environment_ = global;
			return;
		}
	}
	while (environment_ && environment_->get_parent()) {
		environment_ = environment_->get_parent();
	}
}

void vm_backend::push_scope() {
	environment_ = std::make_shared<environment>(environment_, symbolizer_);
}

void vm_backend::pop_scope() {
	if (environment_ && environment_->get_parent()) {
		environment_ = environment_->get_parent();
	}
}

void vm_backend::define_variable(const std::string& name, const script_value& value) {
	environment_->define(name, value);
}

void vm_backend::push_instance_scope() {
	instance_scope_saved_.push_back(environment_);
	std::shared_ptr<environment> global;
	if (engine_) { global = engine_->get_global_environment(); }
	if (!global) { global = environment_; while (global && global->get_parent()) { global = global->get_parent(); } }
	environment_ = std::make_shared<environment>(global, symbolizer_);
	pending_instance_env_ = environment_;
}

void vm_backend::pop_instance_scope() {
	pending_instance_env_.reset();
	if (instance_scope_saved_.empty()) { pop_scope(); return; }
	environment_ = std::move(instance_scope_saved_.back());
	instance_scope_saved_.pop_back();
}

script_value vm_backend::get_variable(const std::string& name) const {
	auto result = environment_->get(name);
	if (!result) {
		if (!result.message().empty()) {
			auto formatted = format_error_message(result.message(),
				symbolizer_->get_string(result.symbol_id()),
				symbolizer_->get_string(result.symbol_id2()));
			throw runtime_error(formatted);
		}
		throw runtime_error(result.error().message());
	}
	return result.value().deref();
}

bool vm_backend::has_variable(const std::string& name) const {
	return environment_->contains(name);
}

void vm_backend::push_external_call_scope() {
	if (frames_.empty()) { arm_execution_deadline(); }
	external_site_stack_.push_back(pending_site_ctx_);
	pending_site_ctx_ = {};
}

void vm_backend::pop_external_call_scope() {
	if (!external_site_stack_.empty()) {
		pending_site_ctx_ = external_site_stack_.back();
		external_site_stack_.pop_back();
	}
}

// Suspended-fiber state. Invariant: op_yield only fires in the coroutine's own body frame
// (any function it called has already returned), so at every suspend the vm's live stacks
// hold ONLY this fiber's state — which is exactly what the stage-4 whole-stack swap parks
// in the storage below (see run_fiber's fiber_stacks_swap guard).
struct vm_backend::vm_coroutine_state : coroutine_backend_state {
	std::shared_ptr<chunk> body_chunk;
	call_frame locals;                      // frame-kind metadata (name, method receiver)
	frame f;                                // PERSISTENT fiber frame: window over the fiber stack,
	                                        // ip survives suspends, try_record.owner stays valid
	std::shared_ptr<environment> entry_env;
	std::shared_ptr<environment> current_env;
	// Stage 4 per-fiber stacks: the fiber OWNS its value/try/iter/cfor stacks; a resume
	// swaps them whole with the vm's live ones (O(1)) and the exit guard swaps back on
	// every path. Absolute indices inside (window_base, try stack_size, bases) are
	// fiber-stack absolute and stay valid across suspends - the old copy-out/copy-in
	// and rebase arithmetic is gone. Nested resumes nest LIFO through the same guard.
	value_stack stack_storage;              // owner stays null: the vm's stack_ keeps the rebase identity
	std::vector<try_record> try_storage;
	std::vector<iter_state> iter_storage;
	std::vector<counted_for_state> cfor_storage;
	std::vector<pending_callee> pending_storage;   // f(yield x) suspends between probe and call
	// The exception trio is fiber state too: a yield inside a catch keeps the caught
	// exception (bare throw;) and the in-catch marker across the resumer's own try/catch
	std::optional<script_exception> parked_exception;
	std::optional<script_value> parked_exception_value;
	uint64_t parked_catch_marker = 0;
	bool started = false;
};

vm_backend::vm_coroutine_state& vm_backend::coroutine_fiber_state(coroutine_handle& handle) {
	auto* state = static_cast<vm_coroutine_state*>(handle.backend_state());
	if (!state) {
		auto owned = std::make_unique<vm_coroutine_state>();
		state = owned.get();
		handle.set_backend_state(std::move(owned));
	}
	return *state;
}

checked_result<script_value> vm_backend::resume_coroutine(coroutine_handle& handle) {
	// A top-level resume (no live VM frames) arms a fresh deadline; a resume nested inside
	// a running script inherits the outer deadline (and the outer limit state).
	// Depth alone is not enough: a TOP-LEVEL script statement calling resume() also runs
	// at call depth 0, and re-arming there let `for(..){ h.resume(); }` push the deadline
	// forever (fuzz livelock seeds 3507/8285/1213/8356/8391).
	if (current_call_depth_ == 0 && frames_.empty()) {
		arm_execution_deadline();
		limits_->reset_for_execute();
	}

	const auto prev_status = handle.get_status();
	if (prev_status == coroutine_handle::status::running) {
		// Defensive twin of coroutine_handle::resume's guard (script paths never get
		// here): activating a running fiber would swap its live stacks out from under it
		return checked_result<script_value>(
			make_error_code(runtime_error_code::evaluation_failed),
			"Coroutine is already running");
	}
	// Fiber bodies recurse natively through resume, unlike ordinary in-loop calls.
	// Count setup/default conversions too, and release the count on every exit.
	if (current_call_depth_ >= limits_->vm_call_depth_limit || detail::native_stack_low()) {
		return detail::raise_call_depth();
	}
	detail::call_depth_guard depth_guard(current_call_depth_);
	auto& state = coroutine_fiber_state(handle);
	handle.set_status(coroutine_handle::status::running);

	if (prev_status == coroutine_handle::status::created) {
		auto function = handle.get_function();
		if (!function) {
			handle.set_status(coroutine_handle::status::failed);
			return checked_result<script_value>(make_error_code(runtime_error_code::internal_error),
				"Coroutine has no function to run");
		}

		{
			const size_t argc = handle.get_args().size();
			if (argc != function->parameters.size() && !arity_accepts(function->parameters, argc)) {
				handle.set_status(coroutine_handle::status::failed);
				return detail::arity_error(function->name, function->parameters.size(), argc, arity_count_ids_, *symbolizer_);
			}
		}

		auto body_chunk = chunk_for_body(function->name, function->parameters, function->body, function->local_count);
		state.body_chunk = body_chunk;
		state.locals = call_frame{};
		state.locals.function_name = function->name;

		auto closure_env = handle.get_closure_env();
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_births_[3];
#endif
		state.entry_env = std::make_shared<environment>(closure_env ? closure_env : environment_, symbolizer_);
		state.current_env = state.entry_env;

		// Method coroutine: the fiber frame is a method frame over the pinned receiver
		// ('this' also resolves via the closure env; the frame copy serves the Tier-1
		// lvalue-ref path, which reads the caller frame's this)
		if (!handle.receiver().is_null()) {
			state.locals.set_this(handle.receiver());
		}

		// The persistent fiber frame: a window over the fiber's OWN stack (base 0);
		// parameter binding happens inside run_fiber once the fiber stack is live
		state.f.code = body_chunk.get();
		state.f.pin = body_chunk;
		state.f.ip = 0;
		state.f.locals = &state.locals;
		state.f.entry_env = state.entry_env;
		state.f.window_backed = true;
		state.f.window_base = 0;
		state.f.window_live = 0;
		state.f.stack_base = 0;
		state.f.top_level = false;
		state.f.return_type = function->return_type;
		state.f.return_conv_class = static_cast<uint8_t>(detail::return_conv_of(*function));
	}

	return run_fiber(handle, state);
}

checked_result<script_value> vm_backend::run_fiber(coroutine_handle& handle, vm_coroutine_state& state) {
	coroutine_handle* prev_active = active_coroutine_;
	active_coroutine_ = &handle;

	// Stage 4: activate the fiber's OWN stacks by whole-vector swap (O(1)); the guard
	// swaps back on EVERY exit (yield, completion, error, throw), leaving the fiber's
	// state parked in its storage and the resumer's stacks live again. Nested resumes
	// nest LIFO. call_records_ and frames_ stay shared: in-fiber script calls pop
	// before any yield (op_yield asserts records_base), and stack traces span the
	// resume chain exactly as before.
	struct fiber_stacks_swap {
		vm_backend* vm;
		vm_coroutine_state* st;
		fiber_stacks_swap(vm_backend* v, vm_coroutine_state* c) : vm(v), st(c) { flip(); }
		~fiber_stacks_swap() { flip(); }
		void flip() {
			vm->stack_.swap_payload(st->stack_storage);
			std::swap(vm->try_records_, st->try_storage);
			std::swap(vm->iter_states_, st->iter_storage);
			std::swap(vm->cfor_states_, st->cfor_storage);
			std::swap(vm->pending_callees_, st->pending_storage);
		}
	} stacks_guard(this, &state);

	// The exception trio follows the stacks: the resumer's is saved here and comes back
	// on every exit (a throw escaping the fiber keeps its exception pair live for the
	// resumer's unwind_to_handler); a suspend parks the fiber's own for the next resume.
	// A fresh fiber starts with no active exception.
	struct fiber_exception_swap {
		vm_backend* vm;
		vm_coroutine_state* st;
		std::optional<script_exception> resumer_exception;
		std::optional<script_value> resumer_exception_value;
		uint64_t resumer_marker;
		bool park = false;
		fiber_exception_swap(vm_backend* v, vm_coroutine_state* c)
			: vm(v), st(c), resumer_exception(std::move(v->current_exception_)),
			  resumer_exception_value(std::move(v->active_exception_value_)), resumer_marker(v->current_catch_var_id_) {
			if (st->started) {
				vm->current_exception_ = std::move(st->parked_exception);
				vm->active_exception_value_ = std::move(st->parked_exception_value);
				vm->current_catch_var_id_ = st->parked_catch_marker;
			} else {
				vm->current_exception_.reset();
				vm->active_exception_value_.reset();
				vm->current_catch_var_id_ = 0;
			}
		}
		~fiber_exception_swap() {
			if (park) {
				st->parked_exception = std::move(vm->current_exception_);
				st->parked_exception_value = std::move(vm->active_exception_value_);
				st->parked_catch_marker = vm->current_catch_var_id_;
			}
			vm->current_catch_var_id_ = resumer_marker;
			if (park || !vm->is_unwinding_) {
				vm->current_exception_ = std::move(resumer_exception);
				vm->active_exception_value_ = std::move(resumer_exception_value);
			}
		}
	} exception_guard(this, &state);

	auto prev_env = environment_;
	environment_ = state.current_env ? state.current_env : state.entry_env;

	// First resume: bind parameters into the fiber frame's window now that the fiber
	// stack is live (defaults run their chunks here; conversions may re-enter script)
	if (!state.started) {
		auto function = handle.get_function();
		auto* body_chunk = state.body_chunk.get();
		const auto& args = handle.get_args();
		for (size_t i = 0; i < function->parameters.size(); ++i) {
			const auto& param = function->parameters[i];
			if (i >= args.size()) {
				auto default_chunk = i < body_chunk->param_default_chunks.size() ? body_chunk->param_default_chunks[i] : nullptr;
				if (!default_chunk) {
					environment_ = prev_env;
					active_coroutine_ = prev_active;
					handle.set_status(coroutine_handle::status::failed);
					return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Missing compiled default argument");
				}
				frame df;
				df.code = default_chunk.get();
				df.pin = default_chunk;
				df.ip = 0;
				df.locals = state.f.locals;
				df.window_backed = state.f.window_backed;
				df.window_base = state.f.window_base;
				df.window_live = state.f.window_live;
				df.entry_env = environment_;
				df.stack_base = stack_.size();
				df.top_level = false;
				auto dr = run(df);
				if (!dr) {
					if (stack_.size() > df.stack_base) {
						stack_.truncate(df.stack_base);
					}
					environment_ = prev_env;
					active_coroutine_ = prev_active;
					handle.set_status(coroutine_handle::status::failed);
					return dr.error_value();
				}
				if (is_unwinding_) {
					// Uncaught throw inside the default expression: bind null over a
					// possibly partial stack; the unwinding propagates to the resumer
					if (stack_.size() > df.stack_base) {
						stack_.truncate(df.stack_base);
					}
					frame_slot_set(state.f, param.slot_index, make_null());
					continue;
				}
				script_value default_val = std::move(stack_.back());
				stack_.pop_back();
				if (param.ref_escaping && !default_val.is_reference()) {
					default_val = script_value::make_cell_reference(std::move(default_val), engine_);
				}
				frame_slot_set(state.f, param.slot_index, std::move(default_val));
				continue;
			}
			auto converted_result = try_convert_for_parameter(args[i], param.type,
				detail::slot_site{detail::slot_kind::parameter, param.name, function->name});
			if (!converted_result) {
				environment_ = prev_env;
				active_coroutine_ = prev_active;
				handle.set_status(coroutine_handle::status::failed);
				return converted_result.error_value();
			}
			script_value converted_arg = std::move(converted_result.value());
			// Probe through reference wrappers for the shared_ptr marker (KEEP
			// BYTE-PARALLEL with bind_parameters / interpreter::call_function)
			bool should_share = false;
			const script_value& shared_probe = converted_arg.deref();
			if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) { should_share = true; }
			if (shared_probe.get_type_info() && shared_probe.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) { should_share = true; }
			script_value bound = should_share ? script_value(shared_probe) : converted_arg.clone();
			if (param.ref_escaping && !bound.is_reference()) {
				bound = script_value::make_cell_reference(std::move(bound), engine_);
			}
			frame_slot_set(state.f, param.slot_index, std::move(bound));
		}
		// Body slots exist behind window_live so operand temps start above the window
		{
			const size_t window_slots = std::max(function->local_count, body_chunk->local_count);
			stack_.fill_null_to(state.f.window_base + window_slots, engine_);
		}
		state.started = true;
	}

	frames_.push_back(&state.f);

	const bool prev_yielding = yielding_;
	yielding_ = false;
	const bool prev_has_return = has_return_value_;
	std::optional<script_value> prev_return = std::move(return_value_);
	has_return_value_ = false;

	std::optional<checked_result<void>> body_result;
	std::exception_ptr pending;
	try {
		body_result.emplace(run(state.f));
	} catch (...) {
		pending = std::current_exception();
	}

	// Capture while the fiber frame is still on frames_: an error raised by the
	// coroutine's own segment names the coroutine, not the resume site (a failed
	// return conversion is one - the door raised it in-frame)
	std::optional<checked_result<script_value>> completed;
	if (!pending && !yielding_) {
		if (!*body_result || is_unwinding_) {
			if (!trace_captured_) capture_stack_trace();
		} else if (has_return_value_ && return_value_.has_value()) {
			completed.emplace(std::move(return_value_.value()));   // converted at the return door
		}
	}
	frames_.pop_back();

	if (yielding_) {
		// Suspend is O(1): the fiber frame (ip included) is persistent and the guard
		// parks the whole stacks in state storage on scope exit - nothing to copy,
		// nothing to rebase.
		state.current_env = environment_;

		// Deref: a reference into the fiber's own locals would dangle once abandoned.
		script_value yielded = handle.last_value();
		if (yielded.is_reference()) {
			yielded = yielded.deref();
		}
		handle.set_status(coroutine_handle::status::suspended);

		exception_guard.park = true;
		yielding_ = prev_yielding;
		has_return_value_ = prev_has_return;
		return_value_ = std::move(prev_return);
		environment_ = prev_env;
		active_coroutine_ = prev_active;
		return yielded;
	}

	// Completed, errored, or threw: the fiber's stacks die here (window included) -
	// clear while they are still the live ones; the guard then swaps the empties back
	// into state storage.
	try_records_.clear();
	iter_states_.clear();
	cfor_states_.clear();
	pending_callees_.clear();
	stack_.clear();

	yielding_ = prev_yielding;
	has_return_value_ = prev_has_return;
	return_value_ = std::move(prev_return);
	environment_ = prev_env;
	active_coroutine_ = prev_active;

	if (pending) {
		handle.set_status(coroutine_handle::status::failed);
		std::rethrow_exception(pending);
	}
	if (!*body_result) {
		handle.set_status(coroutine_handle::status::failed);
		return body_result->error_value();
	}

	handle.set_status(coroutine_handle::status::completed);
	if (!completed) {
		// Falling off the end returns null and (like call_script_function) skips return-type
		// conversion, so a typed coroutine driven to completion by range-for does not error.
		return make_null();
	}
	return std::move(*completed);
}

void vm_backend::exec_yield(frame& f, const vm_instruction& ins) {
	script_value value = ins.a ? std::move(stack_.back()) : make_null();
	if (ins.a) { stack_.pop_back(); }
	if (active_coroutine_) {
		active_coroutine_->do_yield(std::move(value));
	}
	// Yield evaluates to null in the coroutine body; the resume continues past this op.
	stack_.push_back(make_null());
	yielding_ = true;
	++f.ip;
}

std::shared_ptr<chunk> vm_backend::compile_only(const std::vector<declaration_ptr>& declarations) {
	compiler_.typed_lowering(!engine_ || engine_->type_proofs());
	return compiler_.compile_program(declarations);
}

void vm_backend::capture_stack_trace() {
	captured_trace_.clear();
	const ast_node* top_node = nullptr;
	for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
		frame* fr = *it;
		const ast_node* node = fr->ip < fr->code->stmt_nodes.size() ? fr->code->stmt_nodes[fr->ip] : nullptr;
		if (fr->top_level) {
			top_node = node;
			continue;
		}
		captured_trace_.push_back({
			fr->code->function_name.empty() ? std::string("<anonymous>") : fr->code->function_name,
			node ? node->location.filename : std::string(),
			node ? node->location.line : 0
		});
	}
	captured_trace_.push_back({
		"<script>",
		top_node ? top_node->location.filename : std::string(),
		top_node ? top_node->location.line : 0
	});
	trace_captured_ = true;
}

std::string vm_backend::current_source_file() const {
	for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
		frame* fr = *it;
		const ast_node* node = fr->ip < fr->code->stmt_nodes.size() ? fr->code->stmt_nodes[fr->ip] : nullptr;
		if (node && !node->location.filename.empty()) return node->location.filename;
	}
	return {};
}

std::string vm_backend::format_stack_trace() const {
	std::string out;
	for (const auto& e : captured_trace_) {
		out += "  at " + e.function + " (" +
			(e.file.empty() ? std::string("<script>") : e.file) + ":" + std::to_string(e.line) + ")\n";
	}
	return out;
}

// ============================================================
// Member/object helpers (mirror the interpreter's semantics)
// ============================================================

script_value vm_backend::member_target::method(uint64_t id) const {
	if (class_def) {
		return class_def->get_method(id, false);
	}
	return script_value::make_invalid(nullptr);
}

script_value vm_backend::member_target::user_accessor(uint64_t id) const {
	if (class_def) {
		return class_def->find_user_accessor(id);
	}
	return script_value::make_invalid(nullptr);
}

bool vm_backend::member_target::has_field(uint64_t id) const {
	return instance && instance->has_field(id);
}

const script_value& vm_backend::member_target::get_field(uint64_t id) const {
	return instance->get_field(id);
}

const std::string& vm_backend::member_target::class_name() const {
	if (class_def) {
		return class_def->get_name();
	}
	if (instance) {
		return instance->get_class_name();
	}
	static const std::string no_name;
	return no_name;
}

vm_backend::member_target vm_backend::resolve_member_target(const script_value& objectValue) const {
	member_target target;
	auto holder = objectValue.get_object_holder();
	if (!holder) {
		return target;
	}
	if (holder->is_class_instance_wrapper) {
		target.instance = std::static_pointer_cast<class_instance>(holder->data);
		if (target.instance) {
			target.class_def = target.instance->get_class_definition();
			// A plain wrapper of an extended object IS the object's layer (docs/script_extensible.md
			// S1): re-point the handle itself, so every copy of it - and every receiver site,
			// getters, setters, bound methods, the caches - sees the layer from now on.
			if (target.class_def && !target.class_def->is_script_class() && target.class_def->extended_ever()) {
				if (auto hooks = target.class_def->slot_hooks()) {
					auto layer = hooks->read(target.instance->cpp_object_pointer());
					if (layer && layer != target.instance && layer->get_engine() == engine_) {
						holder->data = layer;
						target.instance = std::move(layer);
						target.class_def = target.instance->get_class_definition();
					}
				}
			}
		}
	} else if (engine_) {
		target.engine_def = holder->type_id != UINT64_MAX
			? engine_->get_class_definition(holder->type_id)
			: engine_->get_class_definition(holder->type_name);
		target.class_def = target.engine_def.get();
	}
	return target;
}

script_value vm_backend::make_bound_method_thunk(const script_value& this_val, script_value method) {
	script_callable payload;
	payload.kind = script_callable::kind_type::bound_method;
	payload.this_obj = this_val;
	payload.bound_dispatch = std::move(method);
	return script_value::make_function(script_callable_thunk{engine_, std::move(payload)}, engine_);
}

bool vm_backend::object_to_bool_via_method(const script_value& value) {
	// Class memo: no to_bool() in the chain => truthy, no target resolve / method
	// probe per test. Extended non-script wrappers keep the resolve (it re-points the
	// handle at the script layer as a side effect). KEEP BYTE-PARALLEL with the interpreter
	// and the typed-slot table's bool row (detail::convert_typed_slot_script_rows_impl).
	if (const auto* h = value.get_storage().get_if<script_value::TYPEID_OBJECT>(); h && *h) {
		auto* oh = h->get();
		if (oh->is_class_instance_wrapper && oh->data) {
			if (auto* cd = static_cast<class_instance*>(oh->data.get())->get_class_definition();
			    cd && (cd->is_script_class() || !cd->extended_ever()) && !cd->may_define_to_bool()) {
				return true;
			}
		}
	}
	auto target = resolve_member_target(value);
	if (!target) {
		return true;
	}
	auto method_val = target.method(to_bool_id_);
	if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
		return true;
	}
	script_value bound = make_bound_method(value, method_val);
	const script_function& method = bound.as_function();
	std::vector<script_value> no_args;
	auto result = method(no_args);
	if (result.has_value() && result.value().is_bool()) {
		return result.value().unchecked_as_bool();
	}
	return true;
}

bool vm_backend::is_truthy(const script_value& value) {
	switch (value.raw_storage_index()) {
		case script_value::TYPEID_BOOL: return value.unchecked_as_bool();
		case script_value::TYPEID_NULL: return false;
		case script_value::TYPEID_INT: return value.unchecked_as_int() != 0;
		case script_value::TYPEID_FLOAT: return value.unchecked_as_float() != 0.0;
		case script_value::TYPEID_STRING: return !value.unchecked_as_string().empty();
		case script_value::TYPEID_CHAR: return true;
		case script_value::TYPEID_ARRAY: {
			auto& arr = const_cast<script_value&>(value).get_array_storage();
			return arr && !arr->empty();
		}
		case script_value::TYPEID_MAP: {
			auto& map = const_cast<script_value&>(value).get_map_storage();
			return map && !map->empty();
		}
		case script_value::TYPEID_OBJECT: return object_to_bool_via_method(value);
		case script_value::TYPEID_CPP_BOUND:
			// KEEP BYTE-PARALLEL with interpreter is_truthy (detail/interpreter.hpp) - this is the VM's copy
			if (value.is_bool()) return value.unchecked_as_bool();
			if (value.is_int()) return value.unchecked_as_int() != 0;
			if (value.is_float()) return value.unchecked_as_float() != 0.0;
			if (value.is_string()) return !value.unchecked_as_string().empty();
			if (value.is_char()) return true;
			// Opaque bound: truthy while the host pointer is live (§13, 2026-07)
			return value.get_cpp_bound_ptr() != nullptr;
		case script_value::TYPEID_REFERENCE:
			// References are transparent to truthiness like every other consumer
			// (element reads reach conditions as reference wrappers; `if (a[0])`
			// on a false element was always-truthy before this case existed)
			return is_truthy(value.deref());
		default: return true;
	}
}

std::string vm_backend::value_to_string_with_method(const script_value& val) {
	if (val.type() == script_value_type::jai_object_type) {
		auto target = resolve_member_target(val);
		if (target) {
			auto method_val = target.method(to_string_id_);
			if (!method_val.is_null() && !method_val.is_invalid() && method_val.is_function()) {
				script_value bound = make_bound_method(val, method_val);
				const script_function& method = bound.as_function();
				std::vector<script_value> no_args;
				auto result = method(no_args);
				if (result.has_value() && result.value().is_string()) {
					return result.value().unchecked_as_string();
				}
			}
		}
	}
	return val.to_string();
}

bool vm_backend::object_defines_custom_equality(const script_value& v) const {
	const script_value& d = v.is_reference() ? v.deref() : v;
	if (!d.is_object()) { return false; }
	auto holder = const_cast<script_value&>(d).get_object_holder();
	if (!holder || !holder->is_class_instance_wrapper || !holder->data) { return false; }
	auto* cd = static_cast<class_instance*>(holder->data.get())->get_class_definition();
	return cd && cd->defines_method(eq_method_id_);
}

std::optional<bool> vm_backend::object_equality_via_method(const script_value& left, const script_value& right) {
	// Safe-mode workers never dispatch operator methods: the method-value copy and the
	// bound-method invocation both race shared state (increment B; handle_equal raises
	// the verdict before the structural fallback could silently diverge)
	if (parallel_worker_) [[unlikely]] { return std::nullopt; }
	auto target = resolve_member_target(left);
	if (!target) {
		return std::nullopt;
	}
	auto method_val = target.method(eq_method_id_);
	if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
		if (target.class_def && target.class_def->is_transparent_wrapper()) {
			script_value mutable_left = left;
			script_value unwrapped = target.class_def->unwrap(mutable_left);
			if (!unwrapped.is_null()) {
				return object_equality_via_method(unwrapped, right);
			}
		}
		return std::nullopt;
	}
	script_value bound = make_bound_method(left, method_val);
	const script_function& method = bound.as_function();
	std::vector<script_value> args;
	args.push_back(right);
	auto result = method(args);
	if (result.has_value() && result.value().is_bool()) {
		return result.value().unchecked_as_bool();
	}
	return std::nullopt;
}

std::optional<bool> vm_backend::object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
	// Safe-mode workers: no operator dispatch (increment B); callers' type-mismatch
	// fallback is loud, so no divergence gate is needed here
	if (parallel_worker_) [[unlikely]] { return std::nullopt; }
	auto target = resolve_member_target(left);
	if (!target) {
		return std::nullopt;
	}
	auto method_val = target.method(op_symbol_id);
	if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
		if (target.class_def && target.class_def->is_transparent_wrapper()) {
			script_value mutable_left = left;
			script_value unwrapped = target.class_def->unwrap(mutable_left);
			if (!unwrapped.is_null()) {
				return object_comparison_via_method(unwrapped, right, op_symbol_id);
			}
		}
		return std::nullopt;
	}
	script_value bound = make_bound_method(left, method_val);
	const script_function& method = bound.as_function();
	std::vector<script_value> args;
	args.push_back(right);
	auto result = method(args);
	if (result.has_value() && result.value().is_bool()) {
		return result.value().unchecked_as_bool();
	}
	return std::nullopt;
}

std::optional<script_value> vm_backend::object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
	// Safe-mode workers: no operator dispatch (increment B); callers' type-mismatch
	// fallback is loud, so no divergence gate is needed here
	if (parallel_worker_) [[unlikely]] { return std::nullopt; }
	auto target = resolve_member_target(left);
	if (!target) {
		return std::nullopt;
	}
	auto method_val = target.method(op_symbol_id);
	if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
		if (target.class_def && target.class_def->is_transparent_wrapper()) {
			script_value mutable_left = left;
			script_value unwrapped = target.class_def->unwrap(mutable_left);
			if (!unwrapped.is_null()) {
				return object_arithmetic_via_method(unwrapped, right, op_symbol_id);
			}
		}
		return std::nullopt;
	}
	script_value bound = make_bound_method(left, method_val);
	const script_function& method = bound.as_function();
	std::vector<script_value> args;
	args.push_back(right);
	auto result = method(args);
	if (result.has_value()) {
		return result.value();
	}
	return std::nullopt;
}

std::shared_ptr<environment> vm_backend::acquire_scope_env(std::shared_ptr<environment> parent) {
#ifdef JAISCRIPT_VM_PROFILE
	++profile_env_births_[0];
#endif
	if (!scope_env_pool_.empty()) {
		auto env = std::move(scope_env_pool_.back());
		scope_env_pool_.pop_back();
		env->reset(std::move(parent));
		return env;
	}
	return std::make_shared<environment>(std::move(parent), env_symbolizer_);
}

std::shared_ptr<environment> vm_backend::acquire_method_scope_env(std::shared_ptr<environment> parent, script_value this_obj, class_definition* access_ctx) {
#ifdef JAISCRIPT_VM_PROFILE
	++profile_env_births_[1];
#endif
	if (!scope_env_pool_.empty()) {
		auto env = std::move(scope_env_pool_.back());
		scope_env_pool_.pop_back();
		env->reset_as_method(std::move(parent), std::move(this_obj));
		env->set_access_context(access_ctx);
		return env;
	}
	auto env = std::make_shared<environment>(std::move(parent), env_symbolizer_, std::move(this_obj));
	env->set_access_context(access_ctx);
	return env;
}

std::shared_ptr<environment> vm_backend::acquire_static_scope_env(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def) {
	if (!scope_env_pool_.empty()) {
		auto env = std::move(scope_env_pool_.back());
		scope_env_pool_.pop_back();
		env->reset_as_static_method(std::move(parent), std::move(class_def));
		return env;
	}
#ifdef JAISCRIPT_VM_PROFILE
	++profile_env_births_[2];
#endif
	return std::make_shared<environment>(std::move(parent), env_symbolizer_, std::move(class_def));
}

void vm_backend::release_scope_env(std::shared_ptr<environment> env) {
	if (env.use_count() == 1 && scope_env_pool_.size() < 64) {
		env->reset(nullptr);
		scope_env_pool_.push_back(std::move(env));
	}
}

std::vector<script_value> vm_backend::acquire_arg_vector(size_t reserve) {
	std::vector<script_value> vec;
	if (!arg_vector_pool_.empty()) {
		vec = std::move(arg_vector_pool_.back());
		arg_vector_pool_.pop_back();
	}
	vec.reserve(reserve);
	return vec;
}

void vm_backend::release_arg_vector(std::vector<script_value> vec) {
	if (arg_vector_pool_.size() < 32) {
		vec.clear();   // drop value refs promptly, keep capacity
		arg_vector_pool_.push_back(std::move(vec));
	}
}

void vm_backend::pop_scopes_pooled(uint32_t count) {
	for (uint32_t i = 0; i < count; ++i) {
		auto parent = environment_->get_parent();
		if (!parent) break;
		release_scope_env(std::move(environment_));
		environment_ = std::move(parent);
	}
}

void vm_backend::exec_array(frame& f, const vm_instruction& ins) {
	script_value arrayValue = script_value::make_array(nullptr, engine_);
	auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());
	const size_t n = ins.a;
	array.reserve(n);
	const size_t base = stack_.size() - n;
	// All-detach ruling (2026-07, #12): literal construction is a store boundary -
	// bound primitives snapshot, and reference-wrapper reads normalize to VALUES like
	// assignment (a literal never holds a live reference into its source; deref through
	// a temp so the owning holder outlives the copy). (KEEP BYTE-PARALLEL with the
	// interpreter array literal)
	for (size_t i = 0; i < n; ++i) {
		if (stack_[base + i].is_reference()) [[unlikely]] {
			script_value derefed = stack_[base + i].deref();
			stack_[base + i] = std::move(derefed);
		}
		if (stack_[base + i].raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			stack_[base + i] = stack_[base + i].detached_for_store();
		}
		array.push_back(std::move(stack_[base + i]));
	}
	stack_.truncate(base);
	stack_.push_back(std::move(arrayValue));
}

void vm_backend::exec_map(frame& f, const vm_instruction& ins) {
	script_value mapValue = script_value::make_map(nullptr, nullptr, engine_);
	auto& map = const_cast<script_map&>(mapValue.as_map());
	const size_t n = ins.a;
	const size_t base = stack_.size() - n * 2;
	// All-detach ruling (2026-07, #12): literal construction is a store boundary -
	// bound primitives snapshot, and reference-wrapper reads normalize to VALUES like
	// assignment (see exec_array; KEEP BYTE-PARALLEL with the interpreter map literal)
	for (size_t i = 0; i < n * 2; ++i) {
		if (stack_[base + i].is_reference()) [[unlikely]] {
			script_value derefed = stack_[base + i].deref();
			stack_[base + i] = std::move(derefed);
		}
		if (stack_[base + i].raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			stack_[base + i] = stack_[base + i].detached_for_store();
		}
	}
	for (size_t i = 0; i < n; ++i) {
		map.insert_or_assign(std::move(stack_[base + i * 2]), std::move(stack_[base + i * 2 + 1]));
	}
	stack_.truncate(base);
	stack_.push_back(std::move(mapValue));
}

script_value* vm_backend::resolve_local_or_env(frame& f, uint32_t slot, uint64_t symbol_id) {
	if (slot != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* ptr = frame_slot(f, slot)) {
			return ptr;
		}
	}
	if (f.locals && !f.top_level) {
		ensure_frame_member_scope(f, environment_.get());
		if (frame_scope_active(f)) {
			return member_scope_env_lookup(f, symbol_id, SIZE_MAX);
		}
	}
	return environment_->get_value_ptr(symbol_id);
}

script_value* vm_backend::string_receiver_cell(frame& f, const call_site& site, const identifier_expr* ident) {
	if (ident->global_qualified) {
		return engine_->get_global_environment()->get_value_ptr(site.receiver_symbol);
	}
	return resolve_local_or_env(f, site.receiver_slot, site.receiver_symbol);
}

// resolve_local_or_env with the env path memoized per instruction (slot 2*ip); the
// get_value_ptr tail still runs when the storage prefix misses (this/static fallbacks).
// Member scopes consult the cache only PAST the member rungs (a cached global cell
// must never outrank a same-named member).
script_value* vm_backend::resolve_local_or_env_cached(frame& f, uint32_t slot, uint64_t symbol_id) {
	if (slot != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* ptr = frame_slot(f, slot)) {
			return ptr;
		}
	}
	if (f.locals && !f.top_level) {
		ensure_frame_member_scope(f, environment_.get());
		if (frame_scope_active(f)) {
			return member_scope_env_lookup(f, symbol_id, f.ip * 3 + 2);
		}
	}
	if (script_value* cached = env_lookup_cached(f, f.ip * 3 + 2, symbol_id)) {
		return cached;
	}
	return environment_->get_value_ptr(symbol_id);
}

script_value* vm_backend::env_arm_fast_head(env_lookup_cache_entry* fast_entry, uint64_t symbol_id) {
	// Bail tail for chains the transparency walk refuses (shadowing scope frames):
	// vm_storage_lookup from the CURRENT env resolves with real shadowing, so no
	// transparency proof is needed - the engine serial validates arbitrary chains.
	// Arms env-LOCAL cells only; 'this'/field/static fallbacks stay per-access
	// (receiver rebinds don't advance the serial). Non-null uncacheable results are
	// still THE resolution (same pointer get_value_ptr's prefix would hand back).
	if (!fast_entry) {
		return nullptr;
	}
	bool cacheable = false;
	script_value* ptr = environment_->vm_storage_lookup(symbol_id, cacheable);
	if (ptr && cacheable) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[7];
#endif
		fast_entry->fast_env = environment_.get();
		fast_entry->fast_serial = env_symbolizer_->env_epoch();
		fast_entry->fast_ptr = ptr;
	}
	return ptr;
}

script_value vm_backend::worker_borrowed_string(const script_value& borrow, const script_value& index, const script_string& source) {
	const uint64_t node_bits = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(borrow.parallel_borrow_array()));
	const uint64_t key = (node_bits * 0x9E3779B97F4A7C15ull) ^ static_cast<uint64_t>(index.deref().unchecked_as_int());
	if (worker_borrowed_strings_.size() > 8192) [[unlikely]] { worker_borrowed_strings_.clear(); }
	auto it = worker_borrowed_strings_.find(key);
	if (it != worker_borrowed_strings_.end() && it->second.unchecked_as_string() == source) {
		return it->second;
	}
	script_value own(script_string(source), engine_);
	if (it != worker_borrowed_strings_.end()) { it->second = own; }
	else { worker_borrowed_strings_.emplace(key, own); }
	return own;
}

vm_backend::worker_chunk_env_cache* vm_backend::worker_chunk_record(const chunk* code) {
	if (worker_env_mru_ < worker_env_caches_.size() &&
	    worker_env_caches_[worker_env_mru_].code == code) {
		return &worker_env_caches_[worker_env_mru_];
	}
	for (size_t i = 0; i < worker_env_caches_.size(); ++i) {
		if (worker_env_caches_[i].code == code) {
			worker_env_mru_ = i;
			return &worker_env_caches_[i];
		}
	}
	return nullptr;
}

env_lookup_cache_entry* vm_backend::worker_env_cache_slot(const chunk* code, size_t cache_slot) {
	worker_chunk_env_cache* rec = worker_chunk_record(code);
	if (!rec) { return nullptr; }
	if (rec->entries.size() < code->code.size() * 3) [[unlikely]] {
		rec->entries.resize(code->code.size() * 3);
	}
	return &rec->entries[cache_slot];
}

void vm_backend::worker_pin_env_cache(const std::shared_ptr<void>& pin, const chunk* code) {
	for (const auto& rec : worker_env_caches_) {
		if (rec.code == code) { return; }
	}
	worker_chunk_env_cache rec;
	rec.pin = pin;
	rec.code = code;
	worker_env_caches_.push_back(std::move(rec));
}

script_value* vm_backend::env_lookup_cached(frame& f, size_t cache_slot, uint64_t symbol_id) {
	// SIZE_MAX = caller has no reserved (ip, role) slot for this symbol (e.g. the index
	// of a fused subscript operand - the 3 roles/ip are taken). Entries are provenance-
	// checked by {env, epoch} but NOT by symbol, so slot reuse across symbols would
	// alias variables - skip the cache entirely instead.
	if (cache_slot == SIZE_MAX) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[0];
#endif
		return nullptr;
	}
	// Entry storage + chain target by role: the main vm memoizes in the SHARED chunk
	// cache against the engine's global env; a worker memoizes in its PRIVATE tables
	// (worker_env_cache_slot - never the chunk) against its slot root env, where every
	// capture is defined and whose epoch survives calls (in-place redefines don't
	// bump). Same entries, same validation, same arms either way.
	environment* chain_target = cached_global_env_;
	env_lookup_cache_entry* fast_entry = nullptr;
	if (cached_global_env_) {
		auto& cache = f.code->env_lookup_cache;
		if (cache.size() < f.code->code.size() * 3) [[unlikely]] {
			cache.resize(f.code->code.size() * 3);
		}
		fast_entry = &cache[cache_slot];
	} else if (parallel_worker_ && worker_root_env_) {
		fast_entry = worker_env_cache_slot(f.code, cache_slot);
		chain_target = worker_root_env_;
	}
	// Fast head: same current env + unmoved env serial (per-worker sinks are private,
	// so worker churn only invalidates that worker) proves the whole chain unmodified
	// since arming, whatever its shape - the armed cell is returned on two compares.
	if (fast_entry) {
		if (fast_entry->fast_env == environment_.get() &&
		    fast_entry->fast_serial == env_symbolizer_->env_epoch()) {
#ifdef JAISCRIPT_VM_PROFILE
			++profile_env_resolve_[6];
#endif
			return fast_entry->fast_ptr;
		}
	}
	if (!chain_target) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[1];
#endif
		return nullptr;
	}
	// Top-level frames, call frames running directly in the global env (lazy-elided
	// plain functions - fib-style recursion), or a pinned sticky method scope (persistent
	// per-dispatch env parented on the global env): those environments are stable, so
	// entries actually hit. Other call frames churn envs (binds/resets bump the epoch),
	// which made the miss-path provenance work a pure tax on call-heavy code.
	environment* resolve_env = environment_.get();
	if (!f.top_level && resolve_env != chain_target && !environment_->vm_pinned_scope()) {
		// Scope-env frames: when every env from the frame down to the chain target
		// (global env / worker root) is transparent (standard kind, zero own defines),
		// any hit provably lands in a target cell — so the site caches against the
		// TARGET's identity and epoch instead of bailing to the per-access hash walk
		// (GLOOM: 3.3M walks per 200t, 79% of them the framebuffer name). Transparency
		// is re-proved per access; a mid-body define flips it and the site falls back.
		environment* e = resolve_env;
		int hops = 0;
		while (e != chain_target) {
			if (!e->vm_transparent_for_lookup() || ++hops > 4) {
#ifdef JAISCRIPT_VM_PROFILE
				++profile_env_resolve_[1];
#endif
				return env_arm_fast_head(fast_entry, symbol_id);
			}
			e = e->parent_raw();
			if (!e) {
#ifdef JAISCRIPT_VM_PROFILE
				++profile_env_resolve_[1];
#endif
				return env_arm_fast_head(fast_entry, symbol_id);
			}
		}
		resolve_env = chain_target;
	}
	if (!fast_entry) {
		// Worker frame on a chunk never pinned here (method/coroutine entry): no
		// private row to memoize into - full walk as before
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[1];
#endif
		return nullptr;
	}
	env_lookup_cache_entry& entry = *fast_entry;
	// Per-env epoch (environment::local_epoch): unrelated scope/method env churn no
	// longer invalidates entries cached against the stable global/top-level envs
	const uint64_t epoch = resolve_env->local_epoch();
	if (entry.env == resolve_env && entry.epoch == epoch) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[2];
#endif
		if (fast_entry) {
			// Transparency to resolve_env was (re)proved this access, so the current
			// env resolves to the same cell - arm the two-compare head for next time
			fast_entry->fast_env = environment_.get();
			fast_entry->fast_serial = env_symbolizer_->env_epoch();
			fast_entry->fast_ptr = entry.ptr;
		}
		return entry.ptr;
	}
	bool cacheable = false;
	script_value* ptr = resolve_env->vm_storage_lookup(symbol_id, cacheable);
	if (ptr && cacheable && epoch != 0) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[3];
#endif
		entry.env = resolve_env;
		entry.epoch = epoch;
		entry.ptr = ptr;
		if (fast_entry) {
			fast_entry->fast_env = environment_.get();
			fast_entry->fast_serial = env_symbolizer_->env_epoch();
			fast_entry->fast_ptr = ptr;
		}
	} else {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[4];
#endif
		entry.env = nullptr;
	}
	return ptr;
}

op_status vm_backend::define_decl_value(frame& f, uint64_t name_id, size_t slot_index, script_value value, bool box_cell) {
	// Escape-marked declarations box into a cell (reference_holder cell mode): reference
	// binding then shares the handle. Reference values pass through (ref decls alias).
	if (box_cell && !value.is_reference()) {
		value = script_value::make_cell_reference(std::move(value), engine_);
	}
	if (slot_index != SIZE_MAX && f.locals && !f.top_level) {
		frame_slot_set(f, slot_index, std::move(value));
	} else {
		environment_->define(name_id, std::move(value));
	}
	return {};
}

// ============================================================
// Binary operators (mirror interpreter_dispatch.cpp handle_*)
// ============================================================

checked_result<script_value> vm_backend::handle_add(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_add(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_add(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			script_int r;
			if (!ints::try_add(a, b, r)) return vm_int_overflow_sv("Integer overflow in '+'");
			return script_value(r, engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf + rf, engine_);
		}
		if (li_raw == script_value::TYPEID_STRING || ri_raw == script_value::TYPEID_STRING) {
			script_string joined = value_to_string_with_method(left) + value_to_string_with_method(right);
			// engine::memory_cap chokepoint: deny the concat result before it exists
			if (!limits_->memory_charge(sizeof(script_value) + joined.size())) [[unlikely]] {
				return detail::raise_memory_cap(*limits_);
			}
			return script_value(std::move(joined), engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for + operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		const script_int a = unwrapped_left.unchecked_as_int(), b = unwrapped_right.unchecked_as_int();
		script_int r;
		if (!ints::try_add(a, b, r)) return vm_int_overflow_sv("Integer overflow in '+'");
		return script_value(r, engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf + rf, engine_);
	}

	if (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING) {
		script_string joined = value_to_string_with_method(unwrapped_left) + value_to_string_with_method(unwrapped_right);
		// engine::memory_cap chokepoint: deny the concat result before it exists
		if (!limits_->memory_charge(sizeof(script_value) + joined.size())) [[unlikely]] {
			return detail::raise_memory_cap(*limits_);
		}
		return script_value(std::move(joined), engine_);
	}

	auto custom_result = object_arithmetic_via_method(left, right, op_plus_id_);
	if (custom_result.has_value()) {
		return custom_result.value();
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for + operator");
}

checked_result<script_value> vm_backend::handle_subtract(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_subtract(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_subtract(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			script_int r;
			if (!ints::try_sub(a, b, r)) return vm_int_overflow_sv("Integer overflow in '-'");
			return script_value(r, engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf - rf, engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for - operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		const script_int a = unwrapped_left.unchecked_as_int(), b = unwrapped_right.unchecked_as_int();
		script_int r;
		if (!ints::try_sub(a, b, r)) return vm_int_overflow_sv("Integer overflow in '-'");
		return script_value(r, engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf - rf, engine_);
	}

	auto custom_result = object_arithmetic_via_method(left, right, op_minus_id_);
	if (custom_result.has_value()) {
		return custom_result.value();
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for - operator");
}

checked_result<script_value> vm_backend::handle_multiply(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_multiply(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_multiply(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			script_int r;
			if (!ints::try_mul(a, b, r)) return vm_int_overflow_sv("Integer overflow in '*'");
			return script_value(r, engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf * rf, engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for * operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		const script_int a = unwrapped_left.unchecked_as_int(), b = unwrapped_right.unchecked_as_int();
		script_int r;
		if (!ints::try_mul(a, b, r)) return vm_int_overflow_sv("Integer overflow in '*'");
		return script_value(r, engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf * rf, engine_);
	}

	auto custom_result = object_arithmetic_via_method(left, right, op_star_id_);
	if (custom_result.has_value()) {
		return custom_result.value();
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for * operator");
}

checked_result<script_value> vm_backend::handle_divide(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_divide(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_divide(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			if (b == 0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
			}
			script_int r;
			if (!ints::try_div(a, b, r)) return vm_int_overflow_sv("Integer overflow in '/'");
			return script_value(r, engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			if (rf == 0.0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
			}
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			return script_value(lf / rf, engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for / operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		const script_int a = unwrapped_left.unchecked_as_int(), b = unwrapped_right.unchecked_as_int();
		if (b == 0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
		}
		script_int r;
		if (!ints::try_div(a, b, r)) return vm_int_overflow_sv("Integer overflow in '/'");
		return script_value(r, engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		if (rf == 0.0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
		}
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		return script_value(lf / rf, engine_);
	}

	auto custom_result = object_arithmetic_via_method(left, right, op_slash_id_);
	if (custom_result.has_value()) {
		return custom_result.value();
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for / operator");
}

checked_result<script_value> vm_backend::handle_modulo(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_modulo(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_modulo(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int b = right.unchecked_as_int();
			if (b == 0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
			}
			return script_value(ints::mod(left.unchecked_as_int(), b), engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			if (rf == 0.0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
			}
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			return script_value(std::fmod(lf, rf), engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for % operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		const script_int b = unwrapped_right.unchecked_as_int();
		if (b == 0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
		}
		return script_value(ints::mod(unwrapped_left.unchecked_as_int(), b), engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		if (rf == 0.0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
		}
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		return script_value(std::fmod(lf, rf), engine_);
	}

	auto custom_result = object_arithmetic_via_method(left, right, op_percent_id_);
	if (custom_result.has_value()) {
		return custom_result.value();
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for % operator");
}

checked_result<script_value> vm_backend::handle_less(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_less(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands compare as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_less(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			return script_value(left.unchecked_as_int() < right.unchecked_as_int(), engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf < rf, engine_);
		}
		if (li_raw == script_value::TYPEID_STRING && ri_raw == script_value::TYPEID_STRING) {
			return script_value(left.unchecked_as_string() < right.unchecked_as_string(), engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for < operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		return script_value(unwrapped_left.unchecked_as_int() < unwrapped_right.unchecked_as_int(), engine_);
	}
	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf < rf, engine_);
	}
	if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
		return script_value(unwrapped_left.unchecked_as_string() < unwrapped_right.unchecked_as_string(), engine_);
	}

	auto custom_result = object_comparison_via_method(left, right, op_less_id_);
	if (custom_result.has_value()) {
		return script_value(custom_result.value(), engine_);
	}
	// A script throw inside the operator method is in flight: don't replace it with
	// the invalid-operands error (== parity; the value is discarded while unwinding)
	if (is_unwinding_) {
		return script_value(false, engine_);
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for < operator");
}

checked_result<script_value> vm_backend::handle_less_equal(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_less_equal(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands compare as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_less_equal(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			return script_value(left.unchecked_as_int() <= right.unchecked_as_int(), engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf <= rf, engine_);
		}
		if (li_raw == script_value::TYPEID_STRING && ri_raw == script_value::TYPEID_STRING) {
			return script_value(left.unchecked_as_string() <= right.unchecked_as_string(), engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for <= operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		return script_value(unwrapped_left.unchecked_as_int() <= unwrapped_right.unchecked_as_int(), engine_);
	}
	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf <= rf, engine_);
	}
	if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
		return script_value(unwrapped_left.unchecked_as_string() <= unwrapped_right.unchecked_as_string(), engine_);
	}

	auto custom_result = object_comparison_via_method(left, right, op_less_equal_id_);
	if (custom_result.has_value()) {
		return script_value(custom_result.value(), engine_);
	}
	// A script throw inside the operator method is in flight: don't replace it with
	// the invalid-operands error (== parity; the value is discarded while unwinding)
	if (is_unwinding_) {
		return script_value(false, engine_);
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for <= operator");
}

checked_result<script_value> vm_backend::handle_greater(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_greater(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands compare as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_greater(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			return script_value(left.unchecked_as_int() > right.unchecked_as_int(), engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf > rf, engine_);
		}
		if (li_raw == script_value::TYPEID_STRING && ri_raw == script_value::TYPEID_STRING) {
			return script_value(left.unchecked_as_string() > right.unchecked_as_string(), engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for > operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		return script_value(unwrapped_left.unchecked_as_int() > unwrapped_right.unchecked_as_int(), engine_);
	}
	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf > rf, engine_);
	}
	if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
		return script_value(unwrapped_left.unchecked_as_string() > unwrapped_right.unchecked_as_string(), engine_);
	}

	auto custom_result = object_comparison_via_method(left, right, op_greater_id_);
	if (custom_result.has_value()) {
		return script_value(custom_result.value(), engine_);
	}
	// A script throw inside the operator method is in flight: don't replace it with
	// the invalid-operands error (== parity; the value is discarded while unwinding)
	if (is_unwinding_) {
		return script_value(false, engine_);
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for > operator");
}

checked_result<script_value> vm_backend::handle_greater_equal(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();
	// S8: bound operands normalize to detached shadow-equivalent temps (re-entry depth is exactly 1)
	if (li_raw == script_value::TYPEID_CPP_BOUND || ri_raw == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return handle_greater_equal(li_raw == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
		                ri_raw == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
	// Integral promotion: char operands compare as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li_raw, ri_raw)) [[unlikely]]
		return handle_greater_equal(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			return script_value(left.unchecked_as_int() >= right.unchecked_as_int(), engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float lf = (li_raw == script_value::TYPEID_INT) ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return script_value(lf >= rf, engine_);
		}
		if (li_raw == script_value::TYPEID_STRING && ri_raw == script_value::TYPEID_STRING) {
			return script_value(left.unchecked_as_string() >= right.unchecked_as_string(), engine_);
		}
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for >= operator");
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	const size_t li = unwrapped_left.raw_storage_index();
	const size_t ri = unwrapped_right.raw_storage_index();

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		return script_value(unwrapped_left.unchecked_as_int() >= unwrapped_right.unchecked_as_int(), engine_);
	}
	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float lf = (li == script_value::TYPEID_INT) ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf >= rf, engine_);
	}
	if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
		return script_value(unwrapped_left.unchecked_as_string() >= unwrapped_right.unchecked_as_string(), engine_);
	}

	auto custom_result = object_comparison_via_method(left, right, op_greater_equal_id_);
	if (custom_result.has_value()) {
		return script_value(custom_result.value(), engine_);
	}
	// A script throw inside the operator method is in flight: don't replace it with
	// the invalid-operands error (== parity; the value is discarded while unwinding)
	if (is_unwinding_) {
		return script_value(false, engine_);
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for >= operator");
}

checked_result<script_value> vm_backend::handle_equal(const script_value& left, const script_value& right) {
	// Integral promotion: char operands compare as int64 0..255, so 'a' == 97 and the
	// binary idiom s[i] == 0x1A hold (char_promotion.hpp; was silently false pre-ruling)
	if (detail::char_operands_promote(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]]
		return handle_equal(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));
	if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
		bool is_expired = false;
		if (left.is_weak_ptr()) {
			auto weak_ptr = left.get_weak_ptr();
			is_expired = weak_ptr.expired();
		} else {
			if (right.is_weak_ptr()) {
				auto weak_ptr = right.get_weak_ptr();
				is_expired = weak_ptr.expired();
			} else if (right.get_object_holder() != nullptr) {
				auto holder = right.get_object_holder();
				is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
			} else {
				is_expired = true;
			}
		}
		return script_value(is_expired, engine_);
	}

	script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
	script_value unwrapped_right = right.try_unwrap_transparent_wrapper();

	if ((unwrapped_left.is_int() || unwrapped_left.is_float()) && (unwrapped_right.is_int() || unwrapped_right.is_float())) {
		// Exact int==int: double conversion loses precision beyond 2^53
		if (unwrapped_left.is_int() && unwrapped_right.is_int()) {
			return script_value(unwrapped_left.unchecked_as_int() == unwrapped_right.unchecked_as_int(), engine_);
		}
		script_float lf = unwrapped_left.is_int() ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
		script_float rf = unwrapped_right.is_int() ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		return script_value(lf == rf, engine_);
	}

	// Null-ness is a storage property: typed-but-uninitialized compares like null
	if (unwrapped_left.is_null() || unwrapped_right.is_null()) {
		return script_value(unwrapped_left.is_null() && unwrapped_right.is_null(), engine_);
	}

	if (unwrapped_left.type() != unwrapped_right.type()) {
		return script_value(false, engine_);
	}

	if (unwrapped_left.is_string() && unwrapped_right.is_string()) return script_value(unwrapped_left.unchecked_as_string() == unwrapped_right.unchecked_as_string(), engine_);
	if (unwrapped_left.is_bool() && unwrapped_right.is_bool()) return script_value(unwrapped_left.unchecked_as_bool() == unwrapped_right.unchecked_as_bool(), engine_);

	// Opaque host pointers (unregistered make_value(T*)): identity — equal iff both
	// alias the SAME live pointer (§13 ruling 2026-07). Bound primitives never reach
	// here (their semantic branches above answered). KEEP BYTE-PARALLEL with the interpreter.
	if (unwrapped_left.is_cpp_bound_primitive() && unwrapped_right.is_cpp_bound_primitive()) {
		return script_value(unwrapped_left.get_cpp_bound_ptr() == unwrapped_right.get_cpp_bound_ptr(), engine_);
	}

	// Arrays and maps: memberwise, like C++ containers (detail/structural_equality.hpp;
	// same_object(x, y) asks the identity question). KEEP BYTE-PARALLEL with the interpreter twin
	auto element_equal = [this](const script_value& a, const script_value& b) { return handle_equal(a, b); };
	if (left.is_array() && right.is_array()) {
		return detail::arrays_equal(left, right, engine_, structural_equality_depth_, element_equal);
	}
	if (left.is_map() && right.is_map()) {
		return detail::maps_equal(left, right, engine_, structural_equality_depth_, element_equal);
	}

	// Safe-mode workers: a custom op== would be skipped by the operator wall and the
	// structural fallback below would silently DIVERGE from serial — verdict instead.
	// KEEP BYTE-PARALLEL with the interpreter twin.
	if (parallel_worker_ &&
	    (object_defines_custom_equality(left) || object_defines_custom_equality(right))) [[unlikely]] {
		return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
			"custom operator dispatch on class instances is not admitted in a parallel body");
	}
	auto custom_result = object_equality_via_method(left, right);
	if (custom_result.has_value()) {
		return script_value(custom_result.value(), engine_);
	}

	// Script class instances: same class, fieldwise (a user operator== was consulted above)
	if (auto structural = detail::instances_equal(left, right, engine_, structural_equality_depth_, element_equal)) {
		return std::move(*structural);
	}

	auto left_holder = const_cast<script_value&>(left).get_object_holder();
	auto right_holder = const_cast<script_value&>(right).get_object_holder();
	if (left_holder && right_holder) {
		return script_value(left_holder == right_holder, engine_);
	}

	return script_value(false, engine_);
}

checked_result<script_value> vm_backend::handle_not_equal(const script_value& left, const script_value& right) {
	if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
		bool is_expired = false;
		if (left.is_weak_ptr()) {
			auto weak_ptr = left.get_weak_ptr();
			is_expired = weak_ptr.expired();
		} else {
			if (right.is_weak_ptr()) {
				auto weak_ptr = right.get_weak_ptr();
				is_expired = weak_ptr.expired();
			} else if (right.get_object_holder() != nullptr) {
				auto holder = right.get_object_holder();
				is_expired = (holder->type_name == "weak_ptr_holder" && !holder->data);
			} else {
				is_expired = true;
			}
		}
		return script_value(!is_expired, engine_);
	}

	auto eq_result = handle_equal(left, right);
	if (!eq_result) [[unlikely]] {
		return eq_result.error_value();
	}
	return script_value(!eq_result.value().unchecked_as_bool(), engine_);
}

checked_result<script_value> vm_backend::handle_spaceship(const script_value& left, const script_value& right) {
	// Integral promotion: char operands compare as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]]
		return handle_spaceship(detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));
	if (left.raw_storage_index() == script_value::TYPEID_INT && right.raw_storage_index() == script_value::TYPEID_INT) {
		auto cmp = left.unchecked_as_int() <=> right.unchecked_as_int();
		return script_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)), engine_);
	}

	if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
		script_float lf = left.is_int() ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
		script_float rf = right.is_int() ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
		auto cmp = lf <=> rf;
		return script_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)), engine_);
	}

	if (left.is_string() && right.is_string()) {
		int cmp = left.unchecked_as_string().compare(right.unchecked_as_string());
		return script_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)), engine_);
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for <=> operator");
}

checked_result<script_value> vm_backend::handle_binary_op(token_type op, const script_value& left, const script_value& right) {
	// Integral promotion for the inline bitwise cases below: char operands enter as
	// int64 0..255 (char_promotion.hpp). Arithmetic ops promote inside their handle_*.
	switch (op) {
	case token_type::ampersand:
	case token_type::pipe:
	case token_type::caret:
	case token_type::left_shift:
	case token_type::right_shift:
		if (detail::char_operands_promote(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return handle_binary_op(op, detail::char_promoted(left, engine_), detail::char_promoted(right, engine_));
		}
		break;
	default:
		break;
	}
	switch (op) {
		case token_type::plus: return handle_add(left, right);
		case token_type::minus: return handle_subtract(left, right);
		case token_type::star: return handle_multiply(left, right);
		case token_type::slash: return handle_divide(left, right);
		case token_type::percent: return handle_modulo(left, right);
		case token_type::less: return handle_less(left, right);
		case token_type::less_equal: return handle_less_equal(left, right);
		case token_type::greater: return handle_greater(left, right);
		case token_type::greater_equal: return handle_greater_equal(left, right);
		case token_type::equal_equal: return handle_equal(left, right);
		case token_type::bang_equal: return handle_not_equal(left, right);
		case token_type::spaceship: return handle_spaceship(left, right);
		case token_type::ampersand:
			if (!left.is_int() || !right.is_int()) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Bitwise & requires integer operands");
			}
			return script_value(left.unchecked_as_int() & right.unchecked_as_int(), engine_);
		case token_type::pipe:
			if (!left.is_int() || !right.is_int()) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Bitwise | requires integer operands");
			}
			return script_value(left.unchecked_as_int() | right.unchecked_as_int(), engine_);
		case token_type::caret:
			if (!left.is_int() || !right.is_int()) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Bitwise ^ requires integer operands");
			}
			return script_value(left.unchecked_as_int() ^ right.unchecked_as_int(), engine_);
		case token_type::left_shift: {
			if (!left.is_int() || !right.is_int()) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Left shift requires integer operands");
			}
			auto shift_amount = right.unchecked_as_int();
			if (shift_amount < 0 || shift_amount > 63) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Shift amount must be between 0 and 63");
			}
			return script_value(left.unchecked_as_int() << shift_amount, engine_);
		}
		case token_type::right_shift: {
			if (!left.is_int() || !right.is_int()) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Right shift requires integer operands");
			}
			auto shift_amount = right.unchecked_as_int();
			if (shift_amount < 0 || shift_amount > 63) {
				return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Shift amount must be between 0 and 63");
			}
			return script_value(left.unchecked_as_int() >> shift_amount, engine_);
		}
		default:
			return checked_result<script_value>(make_error_code(runtime_error_code::unknown_operator), "Unknown binary operator");
	}
}

checked_result<script_value> vm_backend::binary_general(token_type op, const script_value& left, const script_value& right) {
	// The interpreter's general path consults registered global operator functions
	// regardless of has_custom_numeric_ops (quirk preserved deliberately). The consult
	// reads the engine's flat operator table (detail/operator_table.hpp) - one mask
	// test + array read, no environment probe.
	if (operator_table_ && operator_table_->any()) {
		if (const script_value* opFunc = operator_table_->entry(detail::binary_op_slot(op))) {
			const script_function& func = opFunc->as_function();
			std::vector<script_value> args = {left, right};
			auto result = func(args);
			if (!result) {
				return result.error_value();
			}
			return std::move(result.value());
		}
	}

	return handle_binary_op(op, left, right);
}

bool vm_backend::binary_fast_shape(token_type op, uint32_t shape, const script_value& left, const script_value& right,
                                   std::optional<checked_result<script_value>>& out) {
	const size_t li = left.raw_storage_index();
	const size_t ri = right.raw_storage_index();

	if (shape == binary_shape_ident_ident) {
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			switch (op) {
			case token_type::plus: { script_int rr; if (!ints::try_add(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '+'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::minus: { script_int rr; if (!ints::try_sub(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '-'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::star: { script_int rr; if (!ints::try_mul(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '*'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::slash:
				if (b == 0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation")); return true; }
				{ script_int rr; if (!ints::try_div(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '/'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::percent:
				if (b == 0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation")); return true; }
				out.emplace(script_value(ints::mod(a, b), engine_)); return true;
			case token_type::less: out.emplace(script_value(a < b, engine_)); return true;
			case token_type::less_equal: out.emplace(script_value(a <= b, engine_)); return true;
			case token_type::greater: out.emplace(script_value(a > b, engine_)); return true;
			case token_type::greater_equal: out.emplace(script_value(a >= b, engine_)); return true;
			case token_type::equal_equal: out.emplace(script_value(a == b, engine_)); return true;
			case token_type::bang_equal: out.emplace(script_value(a != b, engine_)); return true;
			default: return false;
			}
		}
		if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
		    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
			script_float a = li == script_value::TYPEID_INT ? static_cast<script_float>(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float b = ri == script_value::TYPEID_INT ? static_cast<script_float>(right.unchecked_as_int()) : right.unchecked_as_float();
			switch (op) {
			case token_type::plus: out.emplace(script_value(a + b, engine_)); return true;
			case token_type::minus: out.emplace(script_value(a - b, engine_)); return true;
			case token_type::star: out.emplace(script_value(a * b, engine_)); return true;
			case token_type::slash:
				if (b == 0.0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation")); return true; }
				out.emplace(script_value(a / b, engine_)); return true;
			case token_type::percent:
				if (b == 0.0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation")); return true; }
				out.emplace(script_value(std::fmod(a, b), engine_)); return true;
			case token_type::less: out.emplace(script_value(a < b, engine_)); return true;
			case token_type::less_equal: out.emplace(script_value(a <= b, engine_)); return true;
			case token_type::greater: out.emplace(script_value(a > b, engine_)); return true;
			case token_type::greater_equal: out.emplace(script_value(a >= b, engine_)); return true;
			case token_type::equal_equal: out.emplace(script_value(a == b, engine_)); return true;
			case token_type::bang_equal: out.emplace(script_value(a != b, engine_)); return true;
			default: return false;
			}
		}
		if (op == token_type::plus && li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING &&
		    !left.is_cpp_bound() && !right.is_cpp_bound()) {
			// unchecked_as_string does NOT decode cpp_bound (unlike the int/float reads)
			// engine::memory_cap chokepoint: deny the concat result before it exists
			if (!limits_->memory_charge(sizeof(script_value) + left.unchecked_as_string().size() + right.unchecked_as_string().size())) [[unlikely]] {
				out.emplace(checked_result<script_value>(detail::raise_memory_cap(*limits_)));
				return true;
			}
			out.emplace(script_value(left.unchecked_as_string() + right.unchecked_as_string(), engine_));
			return true;
		}
		// cpp-bound operands (index 14) skip the gates above: keep the zero-divisor
		// error surface identical to this shape's plain fast path (twin parity)
		if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) {
			if (auto z = bound_fastpath_zero_divisor(op, left, right, true, true)) {
				out.emplace(checked_result<script_value>(make_error_code(z->code), z->text));
				return true;
			}
		}
		return false;
	}

	if (shape == binary_shape_ident_literal) {
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			switch (op) {
			case token_type::less: out.emplace(script_value(a < b, engine_)); return true;
			case token_type::less_equal: out.emplace(script_value(a <= b, engine_)); return true;
			case token_type::greater: out.emplace(script_value(a > b, engine_)); return true;
			case token_type::greater_equal: out.emplace(script_value(a >= b, engine_)); return true;
			case token_type::equal_equal: out.emplace(script_value(a == b, engine_)); return true;
			case token_type::bang_equal: out.emplace(script_value(a != b, engine_)); return true;
			case token_type::plus: { script_int rr; if (!ints::try_add(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '+'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::minus: { script_int rr; if (!ints::try_sub(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '-'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::star: { script_int rr; if (!ints::try_mul(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '*'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::slash:
				if (b == 0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation")); return true; }
				{ script_int rr; if (!ints::try_div(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '/'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::percent:
				if (b == 0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation")); return true; }
				out.emplace(script_value(ints::mod(a, b), engine_)); return true;
			default: return false;
			}
		}
		if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
		    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
			script_float a = li == script_value::TYPEID_INT ? static_cast<script_float>(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float b = ri == script_value::TYPEID_INT ? static_cast<script_float>(right.unchecked_as_int()) : right.unchecked_as_float();
			switch (op) {
			case token_type::less: out.emplace(script_value(a < b, engine_)); return true;
			case token_type::less_equal: out.emplace(script_value(a <= b, engine_)); return true;
			case token_type::greater: out.emplace(script_value(a > b, engine_)); return true;
			case token_type::greater_equal: out.emplace(script_value(a >= b, engine_)); return true;
			case token_type::plus: out.emplace(script_value(a + b, engine_)); return true;
			case token_type::minus: out.emplace(script_value(a - b, engine_)); return true;
			case token_type::star: out.emplace(script_value(a * b, engine_)); return true;
			case token_type::slash:
				if (b == 0.0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation")); return true; }
				out.emplace(script_value(a / b, engine_)); return true;
			case token_type::percent:
				if (b == 0.0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation")); return true; }
				out.emplace(script_value(std::fmod(a, b), engine_)); return true;
			default: return false;
			}
		}
		// twin parity for bound operands: float / and % are covered inline in this shape
		// (exec_binary_fused parity), so bound pairs keep the same error surface
		if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) {
			if (auto z = bound_fastpath_zero_divisor(op, left, right, true, true)) {
				out.emplace(checked_result<script_value>(make_error_code(z->code), z->text));
				return true;
			}
		}
		return false;
	}

	if (shape == binary_shape_literal_ident) {
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			switch (op) {
			case token_type::less: out.emplace(script_value(a < b, engine_)); return true;
			case token_type::less_equal: out.emplace(script_value(a <= b, engine_)); return true;
			case token_type::greater: out.emplace(script_value(a > b, engine_)); return true;
			case token_type::greater_equal: out.emplace(script_value(a >= b, engine_)); return true;
			case token_type::equal_equal: out.emplace(script_value(a == b, engine_)); return true;
			case token_type::bang_equal: out.emplace(script_value(a != b, engine_)); return true;
			case token_type::plus: { script_int rr; if (!ints::try_add(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '+'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::minus: { script_int rr; if (!ints::try_sub(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '-'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::star: { script_int rr; if (!ints::try_mul(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '*'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::slash:
				if (b == 0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation")); return true; }
				{ script_int rr; if (!ints::try_div(a, b, rr)) { out.emplace(vm_int_overflow_sv("Integer overflow in '/'")); return true; } out.emplace(script_value(rr, engine_)); return true; }
			case token_type::percent:
				if (b == 0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation")); return true; }
				out.emplace(script_value(ints::mod(a, b), engine_)); return true;
			default: return false;
			}
		}
		if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
		    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
			script_float a = li == script_value::TYPEID_INT ? static_cast<script_float>(left.unchecked_as_int()) : left.unchecked_as_float();
			script_float b = ri == script_value::TYPEID_INT ? static_cast<script_float>(right.unchecked_as_int()) : right.unchecked_as_float();
			switch (op) {
			case token_type::less: out.emplace(script_value(a < b, engine_)); return true;
			case token_type::less_equal: out.emplace(script_value(a <= b, engine_)); return true;
			case token_type::greater: out.emplace(script_value(a > b, engine_)); return true;
			case token_type::greater_equal: out.emplace(script_value(a >= b, engine_)); return true;
			case token_type::plus: out.emplace(script_value(a + b, engine_)); return true;
			case token_type::minus: out.emplace(script_value(a - b, engine_)); return true;
			case token_type::star: out.emplace(script_value(a * b, engine_)); return true;
			case token_type::slash:
				if (b == 0.0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation")); return true; }
				out.emplace(script_value(a / b, engine_)); return true;
			case token_type::percent:
				if (b == 0.0) { out.emplace(checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation")); return true; }
				out.emplace(script_value(std::fmod(a, b), engine_)); return true;
			default: return false;
			}
		}
		// twin parity for bound operands: float / and % are covered inline in this shape
		// (exec_binary_fused parity), so bound pairs keep the same error surface
		if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) {
			if (auto z = bound_fastpath_zero_divisor(op, left, right, true, true)) {
				out.emplace(checked_result<script_value>(make_error_code(z->code), z->text));
				return true;
			}
		}
		return false;
	}

	return false;
}

checked_result<script_value> vm_backend::evaluate_arithmetic(const script_value& left_in, token_type op, const script_value& right_in) {
	const script_value& left = left_in.deref();
	const script_value& right = right_in.deref();

	const size_t li = left.raw_storage_index();
	const size_t ri = right.raw_storage_index();

	// S8: bound operands normalize to detached temps (byte-parallel with interpreter::evaluate_arithmetic)
	if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		return evaluate_arithmetic(li == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left, op,
		                           ri == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);

	// Integral promotion: char operands enter arithmetic as int64 0..255 (char_promotion.hpp)
	if (detail::char_operands_promote(li, ri)) [[unlikely]]
		return evaluate_arithmetic(detail::char_promoted(left, engine_), op, detail::char_promoted(right, engine_));

	if (op == token_type::plus && (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING)) {
		script_string joined = value_to_string_with_method(left) + value_to_string_with_method(right);
		// engine::memory_cap chokepoint: deny the concat result before it exists
		if (!limits_->memory_charge(sizeof(script_value) + joined.size())) [[unlikely]] {
			return detail::raise_memory_cap(*limits_);
		}
		return script_value(std::move(joined), engine_);
	}

	if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
		script_int leftInt = left.unchecked_as_int();
		script_int rightInt = right.unchecked_as_int();
		script_int rr;
		switch (op) {
			case token_type::plus:
				if (!ints::try_add(leftInt, rightInt, rr)) return vm_int_overflow_sv("Integer overflow in '+'");
				return script_value(rr, engine_);
			case token_type::minus:
				if (!ints::try_sub(leftInt, rightInt, rr)) return vm_int_overflow_sv("Integer overflow in '-'");
				return script_value(rr, engine_);
			case token_type::star:
				if (!ints::try_mul(leftInt, rightInt, rr)) return vm_int_overflow_sv("Integer overflow in '*'");
				return script_value(rr, engine_);
			case token_type::slash:
				if (rightInt == 0) {
					return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
				}
				if (!ints::try_div(leftInt, rightInt, rr)) return vm_int_overflow_sv("Integer overflow in '/'");
				return script_value(rr, engine_);
			case token_type::percent:
				if (rightInt == 0) {
					return checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Division by zero");
				}
				return script_value(ints::mod(leftInt, rightInt), engine_);
			default:
				return checked_result<script_value>(make_error_code(runtime_error_code::unknown_operator), "Unknown arithmetic operator");
		}
	}

	script_float leftNum, rightNum;
	if (li == script_value::TYPEID_INT) {
		leftNum = static_cast<script_float>(left.unchecked_as_int());
	} else if (li == script_value::TYPEID_FLOAT) {
		leftNum = left.unchecked_as_float();
	} else {
		return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand), "Left operand must be numeric");
	}
	if (ri == script_value::TYPEID_INT) {
		rightNum = static_cast<script_float>(right.unchecked_as_int());
	} else if (ri == script_value::TYPEID_FLOAT) {
		rightNum = right.unchecked_as_float();
	} else {
		return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand), "Right operand must be numeric");
	}

	switch (op) {
		case token_type::plus: return script_value(leftNum + rightNum, engine_);
		case token_type::minus: return script_value(leftNum - rightNum, engine_);
		case token_type::star: return script_value(leftNum * rightNum, engine_);
		case token_type::slash:
			if (rightNum == 0.0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
			}
			return script_value(leftNum / rightNum, engine_);
		case token_type::percent:
			if (rightNum == 0.0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::modulo_by_zero), "Division by zero");
			}
			return script_value(std::fmod(leftNum, rightNum), engine_);
		default:
			return checked_result<script_value>(make_error_code(runtime_error_code::unknown_operator), "Unknown arithmetic operator");
	}
}

// ============================================================
// Type enforcement + parameter conversion (interpreter parity)
// ============================================================

// The one typed-slot table (detail::convert_for_typed_slot); this site owns the assignment
// texts. KEEP BYTE-PARALLEL with interpreter::enforce_type_compatibility.
checked_result<script_value> vm_backend::enforce_type_compatibility(script_value value, type_info_ptr target_type,
                                                                    std::string_view var_name, detail::slot_kind kind) {
	if (!target_type || target_type->base_type == script_value_type::jai_any_type) {
		return std::move(value);
	}
	{   // same-storage scalars are the table's identity row
		const script_value& seen = value.is_reference() ? value.deref() : value;
		size_t want = script_value::TYPEID_INVALID;
		switch (target_type->base_type) {
			case script_value_type::jai_int_type: want = script_value::TYPEID_INT; break;
			case script_value_type::jai_float_type: want = script_value::TYPEID_FLOAT; break;
			case script_value_type::jai_string_type: want = script_value::TYPEID_STRING; break;
			case script_value_type::jai_bool_type: want = script_value::TYPEID_BOOL; break;
			case script_value_type::jai_char_type: want = script_value::TYPEID_CHAR; break;
			default: break;
		}
		if (want != script_value::TYPEID_INVALID && seen.raw_storage_index() == want) {
			return std::move(value);
		}
	}
	auto outcome = detail::convert_for_typed_slot(value, target_type, engine_);
	if (!outcome) {
		return outcome.error_value();
	}
	using verdict = detail::slot_conversion::verdict;
	detail::slot_conversion& out = outcome.value();
	switch (out.v) {
		case verdict::identity:
		case verdict::identity_exact:
			return std::move(value);
		case verdict::converted:
			return std::move(*out.value);
		case verdict::container_mismatch: {
			auto* sym = engine_->get_symbolizer();
			return checked_result<script_value>(
				make_error_code(runtime_error_code::array_element_type_mismatch),
				detail::container_boundary_mismatch_text,
				sym->intern(out.mismatch->first), sym->intern(out.mismatch->second));
		}
		case verdict::handle_target:
			// shared_ptr<T> / weak_ptr<T> `=`: a same-kind tag stores as-is; a shared handle
			// downgrades into a weak_ptr<T> slot exactly as the declaration form does (outside the table)
			if (value.type() == target_type->base_type) {
				return std::move(value);
			}
			if (detail::weak_slot_takes_shared(value, target_type, engine_)) {
				return script_value::make_weak_ptr(value, engine_);
			}
			[[fallthrough]];
		default:
			return detail::typed_slot_rejection(value, *target_type, detail::slot_site{kind, var_name, {}}, engine_);
	}
}

// Compound store-back: locked targets convert the promoted result like '=' does; var
// targets keep the promoted result and re-tag it 'any' so the variable stays dynamic.
// KEEP BYTE-PARALLEL with interpreter::compound_typed_store_back.
op_status vm_backend::compound_typed_store_back(script_value& target, script_value promoted, std::string_view var_name) {
	type_info_ptr tag = target.get_type_info();
	if (tag && tag->base_type != script_value_type::jai_any_type) {
		auto enforced = enforce_type_compatibility(std::move(promoted), tag, var_name);
		if (!enforced) {
			return raise_from(enforced);
		}
		promoted = std::move(enforced.value());
		promoted.set_type_info(tag);
	} else if (tag) {
		promoted.set_type_info(tag);
	}
	target = std::move(promoted);
	return {};
}

// The one typed-slot table (detail::convert_for_typed_slot); this site owns the parameter /
// return texts and the identity return shapes (the arg itself; the derefed arg for exact-name
// objects - the V-C1 lane and the exact-class lane replicate them).
// KEEP BYTE-PARALLEL with interpreter::try_convert_for_parameter.
checked_result<script_value> vm_backend::try_convert_for_parameter(const script_value& arg, type_info_ptr target_type,
                                                                   const detail::slot_site& site) {
	if (!target_type) {
		return arg;
	}

	// deref() follows chains to a non-reference or throws - no residual-ref case
	const script_value& derefed_arg = arg.deref();
	const auto target_base_type = target_type->base_type;
	if (target_base_type == script_value_type::jai_any_type) return arg;
	switch (derefed_arg.raw_storage_index()) {   // same-storage scalars and handles bind as-is (the table's identity / handle rows)
		case script_value::TYPEID_INT: if (target_base_type == script_value_type::jai_int_type) return arg; break;
		case script_value::TYPEID_FLOAT: if (target_base_type == script_value_type::jai_float_type) return arg; break;
		case script_value::TYPEID_STRING: if (target_base_type == script_value_type::jai_string_type) return arg; break;
		case script_value::TYPEID_BOOL: if (target_base_type == script_value_type::jai_bool_type) return arg; break;
		case script_value::TYPEID_CHAR: if (target_base_type == script_value_type::jai_char_type) return arg; break;
		case script_value::TYPEID_SHARED_PTR: if (target_base_type == script_value_type::jai_shared_ptr_type) return arg; break;
		case script_value::TYPEID_WEAK_PTR: if (target_base_type == script_value_type::jai_weak_ptr_type) return arg; break;
		default: break;
	}

	auto outcome = detail::convert_for_typed_slot(arg, target_type, engine_);
	if (!outcome) {
		return outcome.error_value();
	}
	using verdict = detail::slot_conversion::verdict;
	detail::slot_conversion& out = outcome.value();
	switch (out.v) {
		case verdict::identity:
			return arg;
		case verdict::identity_exact:
			return derefed_arg;
		case verdict::converted:
			return std::move(*out.value);
		case verdict::container_mismatch: {
			auto* sym = engine_->get_symbolizer();
			return checked_result<script_value>(
				make_error_code(runtime_error_code::array_element_type_mismatch),
				detail::container_boundary_mismatch_text,
				sym->intern(out.mismatch->first), sym->intern(out.mismatch->second));
		}
		case verdict::handle_target: {
			// shared_ptr<T> / weak_ptr<T> parameters and returns (outside the table): a same-kind
			// payload, a matching or derived handle binds as-is
			if (derefed_arg.storage_type() == target_base_type) {
				return arg;
			}
			auto source_type_info = derefed_arg.get_type_info();
			if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type &&
			    target_base_type == script_value_type::jai_shared_ptr_type) {
				if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
					return arg;
				}
				auto resolved = resolve_member_target(derefed_arg);
				if (resolved) {
					if (resolved.class_name() == target_type->type_name) {
						return arg;
					}
					if (resolved.class_def && resolved.class_def->is_subtype_of(target_type->type_name)) {
						return arg;
					}
				}
			}
			break;
		}
		default:
			break;
	}

	if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty() &&
	    (out.v == verdict::reject || out.v == verdict::reject_class)) {
		return checked_result<script_value>(
			make_error_code(runtime_error_code::type_mismatch),
			"Cannot convert {0} to {1} (no suitable single-argument constructor)",
			symbolizer_->intern(detail::slot_source_type_name(derefed_arg, engine_)), target_type->id);
	}
	return detail::typed_slot_rejection(derefed_arg, *target_type, site, engine_);
}

// ============================================================
// Opcode handlers
// ============================================================

op_status vm_backend::exec_load(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	const size_t depth_before = stack_.size();
	const op_status st = exec_load_impl(f, ins);
	if (stack_.size() > depth_before) { const size_t k = stack_.back().deref().raw_storage_index(); ++profile_load_kinds_[k < 16 ? k : 15]; }
	return st;
#else
	return exec_load_impl(f, ins);
#endif
}

op_status vm_backend::exec_load_impl(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	if (f.ip + 1 < f.code->code.size()) { ++profile_load_next_[static_cast<uint8_t>(f.code->code[f.ip + 1].op)]; }
#endif
	const uint64_t sym = f.code->symbols[ins.b];
	if (ins.c & load_flag_global_only) {
		// ::name - GLOBAL env only (locals, captures, member scope all skipped)
		if (script_value* g = engine_->get_global_environment()->get_value_ptr(sym)) {
			stack_.push_back(g->deref());
			return {};
		}
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}'", sym);
	}
	if (ins.a != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* local = frame_slot(f, ins.a)) {
			stack_.push_back(local->deref());
			return {};
		}
	}
	if (ins.c & load_flag_type_ctor) {
		std::string_view name = symbolizer_->get_string(sym);
		size_t pos = name.find('<');
		std::string base_type(name.substr(0, pos));
		// nullable-pointer probe (checked_result stage 1): same ladder as get(), the
		// miss is a null instead of a fat error result nobody reads
		if (script_value* ctor = environment_->get_value_ptr(symbolizer_->intern(base_type));
		    ctor && ctor->is_function()) {
			stack_.push_back(*ctor);
			return {};
		}
	}
	// This-field IC: serial-guarded walk-miss proof, checked BEFORE the env cache — the
	// push copy is byte-identical to the armed fallback below (see fused_ident_value's
	// twin for the ordering argument)
	if (script_value* ic_field = this_field_ic_probe(f, f.ip * 3, sym)) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[8];
#endif
		stack_.push_back(*ic_field);
		return {};
	}
	// Member scope (detail/member_scope.hpp): C++ unqualified-lookup order - own
	// scopes, then the class's members (fields minting the same copy the old tail
	// pushed, methods minting the same bound thunk), then the cache/chain. The
	// this-field IC above stays sound: its serial-guarded walk-miss proof covers
	// the own-scope phase too. Non-scope frames keep the original ladder untouched.
	if (f.locals && !f.top_level) {
		ensure_frame_member_scope(f, environment_.get());
		if (frame_scope_active(f)) {
			if (script_value* own = detail::own_scope_lookup(environment_.get(), f.locals->scope_floor, sym)) {
				stack_.push_back(own->deref());
				return {};
			}
			// V-I1 proven bare field read: class + shape-epoch guard on the receiver, present
			// cell (== the probe's *member.value_ptr copy); a miss falls to the probe below
			if (bare_field_proof_frame(f)) {
				if (const identifier_expr* idn = ident_node_at(f); idn && idn->proven_class) {
					if (script_value* cell = detail::proven_field_cell(*f.locals->scope_this, idn->proven_class,
					                                                   idn->proven_epoch, idn->proven_slot, nullptr)) {
						stack_.push_back(*cell);
						return {};
					}
				}
			}
			auto member = detail::probe_member_scope(frame_scope_state(f), f.locals->scope_this,
			                                         f.locals->scope_static, sym, /*want_methods*/ true);
			if (member) {
				if (member.what == detail::member_probe_hit::kind::method) {
					if (member.instance && f.locals->scope_this) {
						stack_.push_back(make_bound_method_thunk(*f.locals->scope_this, std::move(member.method_value)));
					} else {
						stack_.push_back(std::move(member.method_value));
					}
					return {};
				}
				if (member.what == detail::member_probe_hit::kind::cpp_property) {
					auto read = detail::read_bare_cpp_property(*f.locals->scope_this, member.instance,
					                                           member.method_value, member.getter_id, sym);
					if (!read) {
						return raise_from(read);
					}
					stack_.push_back(std::move(read.value()));
					return {};
				}
				if (member.what == detail::member_probe_hit::kind::field && member.instance) {
					this_field_ic_arm(f, f.ip * 3, member.instance, sym);
				}
				stack_.push_back(*member.value_ptr);
				return {};
			}
			if (script_value* cached = env_lookup_cached(f, f.ip * 3, sym)) {
				stack_.push_back(cached->deref());
				return {};
			}
			if (script_value* beyond = f.locals->scope_floor
			        ? f.locals->scope_floor->get_value_ptr(sym)
			        : environment_->get_value_ptr(sym)) {
				stack_.push_back(beyond->deref());
				return {};
			}
			return raise_(make_error_code(runtime_error_code::undefined_variable),
				"Undefined variable '{0}'", sym);
		}
	}
	if (script_value* cached = env_lookup_cached(f, f.ip * 3, sym)) {
		stack_.push_back(cached->deref());
		return {};
	}
#ifdef JAISCRIPT_VM_PROFILE
	++profile_env_resolve_[5];
	++profile_env_walk_names_[std::string(symbolizer_->get_string(sym))];
#endif
	// get_value_ptr IS get_ref's exact resolution ladder minus the error wrapper
	if (script_value* env_val = environment_->get_value_ptr(sym)) {
		stack_.push_back(env_val->deref());
		return {};
	}
	if (script_value* this_ptr = current_this(f)) {
		script_value& this_val = *this_ptr;
		if (this_val.is_object()) {
			std::shared_ptr<class_instance> instance = this_val.get_class_instance();
			if (instance) {
				// A hybrid's C++ property reads through its getter (never a placeholder cell)
				if (uint64_t getter_id = detail::cpp_property_getter_id(instance.get(), sym)) {
					auto read = detail::read_bare_cpp_property(this_val, instance.get(),
					                                           instance->get_method(getter_id, false), getter_id, sym);
					if (!read) {
						return raise_from(read);
					}
					stack_.push_back(std::move(read.value()));
					return {};
				}
				if (instance->has_field(sym)) {
					this_field_ic_arm(f, f.ip * 3, instance.get(), sym);
#ifdef JAISCRIPT_VM_PROFILE
					++profile_env_resolve_[9];
#endif
					stack_.push_back(instance->get_field(sym));
					return {};
				}
				script_value method = instance->get_method(sym, false);
				if (!method.is_invalid()) {
					stack_.push_back(make_bound_method_thunk(this_val, std::move(method)));
					return {};
				}
				auto class_def = instance->get_class_definition();
				if (class_def && class_def->has_static_field(sym)) {
					stack_.push_back(class_def->get_static_field(sym));
					return {};
				}
			}
		}
	}
	return raise_(make_error_code(runtime_error_code::undefined_variable),
		"Undefined variable '{0}'", sym);
}

op_status vm_backend::exec_store(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	if (f.ip > 0) { ++profile_store_prev_[static_cast<uint8_t>(f.code->code[f.ip - 1].op)]; }
#endif
	script_value value = std::move(stack_.back());
	stack_.pop_back();
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_t0 = __rdtsc();
	profile_store_last_path_ = 7;
	const op_status stored = store_popped_value(f, ins, std::move(value));
	const uint64_t prof_dt = __rdtsc() - prof_t0;
	profile_store_path_cycles_[profile_store_last_path_] += prof_dt;
	++profile_store_path_counts_[profile_store_last_path_];
	profile_store_name_cycles_[std::string(symbolizer_->get_string(f.code->symbols[ins.a]))] += prof_dt;
	return stored;
#else
	return store_popped_value(f, ins, std::move(value));
#endif
}

// The ONE identifier-store tail (everything op_store does after consuming its value),
// shared verbatim by op_store and op_binary_fused_store (dest-addressed fused binaries)
// so the two spellings cannot drift.
op_status vm_backend::store_popped_value(frame& f, const vm_instruction& ins, script_value value) {
	const uint64_t sym = f.code->symbols[ins.a];
	const bool rhs_lvalue = (ins.c & store_flag_rhs_lvalue) != 0;

	// Element/subscript reads arrive as reference wrappers (rhs-lvalue read shape);
	// assignment consumes the VALUE - normalize like every other consumer
	// (KEEP BYTE-PARALLEL with interpreter::visit_assignment_expr). Copy out through a
	// temp: assigning value.deref() straight into value destroys the holder that OWNS
	// the deref target while the copy is still reading it.
	if (value.is_reference()) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_store_paths_[2];
#endif
		script_value derefed = value.deref();
		value = std::move(derefed);
	}

	if (ins.b != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* frameLocal = frame_slot(f, ins.b)) {
			// A cell in the slot IS the variable's storage when the name is the value
			// decl itself (escape-boxed local): write the cell with the same typed
			// enforcement a plain slot gets. Ref-alias names (ref params / auto& decls)
			// and non-cell references keep the store-through semantics.
			script_value* storage = frameLocal;
			if (frameLocal->is_reference()) {
				auto* holder = frameLocal->get_reference_holder();
				if (holder && holder->has_cell && !(ins.c & store_flag_ref_alias)) {
					storage = holder->cell();
				} else {
#ifdef JAISCRIPT_VM_PROFILE
					profile_store_last_path_ = 3;
#endif
					VM_TRY(detail::ref_store_through(*frameLocal, value, engine_, symbolizer_));
					stack_.push_back(std::move(value));
					return {};
				}
			}
			// Parse-proven typed store: an IDENTICAL interned type tag (raw pointer
			// compare - int/float tags are engine-canonical) makes enforcement a
			// provable no-op, and a plain int/float payload makes the assignment
			// kernel a plain copy. Guarded at runtime, so a mis-stamp just falls
			// through. (KEEP BYTE-PARALLEL with interpreter::visit_assignment_expr.)
			if (ins.c & store_flag_type_provable) {
				const size_t vi = value.raw_storage_index();
				if ((vi == script_value::TYPEID_INT || vi == script_value::TYPEID_FLOAT) &&
				    storage->get_type_info().get() == value.get_type_info().get()) {
#ifdef JAISCRIPT_VM_PROFILE
					++profile_store_paths_[0];
					profile_store_last_path_ = 1;
#endif
					*storage = value;
					stack_.push_back(std::move(value));
					return {};
				}
			}
#ifdef JAISCRIPT_VM_PROFILE
			++profile_store_paths_[(ins.c & store_flag_type_provable) ? 4 : 3];
			++profile_builtin_direct_names_[std::string("st:") + std::string(symbolizer_->get_string(sym))];
			profile_store_last_path_ = 2;
#endif
			// Slot locals enforce their locked type like the env path below does
			// (same-type fast guard keeps the hot store path call-free)
			type_info_ptr slot_type = storage->get_type_info();
			// V-S1: a same_type proof whose payload guard holds makes enforcement the identity
			const bool proven_identity = (ins.c & store_flag_same_type) && slot_type &&
			                             detail::same_type_payload(value, slot_type.get());
			if (!proven_identity && slot_type && (slot_type->base_type != value.type() ||
			                  slot_type->base_type == script_value_type::jai_object_type ||
			                  slot_type->base_type == script_value_type::jai_array_type ||
			                  slot_type->base_type == script_value_type::jai_map_type)) {
				auto enforced = enforce_type_compatibility(std::move(value), slot_type, symbolizer_->get_string(sym));
				if (!enforced) {
					return raise_from(enforced);
				}
				value = std::move(enforced.value());
				// var slots keep a shared_ptr-tagged rhs's marker (decl parity;
				// flattening to 'any' would detach at the copy)
				if (slot_type->base_type == script_value_type::jai_any_type &&
				    (!value.get_type_info() ||
				     value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
					value.set_type_info(slot_type);
				}
			}
			*storage = clone_for_assignment(value);
			stack_.push_back(std::move(value));
			return {};
		}
	}

	// Store-side this-field IC (bare method-field writes - ctor bodies, creature
	// state): the cell, the declared type and the no-setter fact come from the site
	// entry; the store itself is enforce_field_write + set_field_unchecked on that
	// cell, texts included (KEEP BYTE-PARALLEL with the this-field landing below).
	if (!(ins.c & store_flag_global_only) && f.locals && !f.top_level) {
		type_info* ic_declared = nullptr;
		if (script_value* cell = this_field_store_probe(f, f.ip * 3, sym, ic_declared)) {
			if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
				value = value.detached_for_store();
			}
			script_value incoming = rhs_lvalue ? clone_for_assignment(value) : std::move(value);
			type_info_ptr declared{ic_declared};
			if (!declared) {
				type_info_ptr tag = !cell->is_reference() ? cell->get_type_info() : nullptr;
				if (tag && tag->base_type != script_value_type::jai_any_type &&
				    tag->base_type != script_value_type::jai_null_type) {
					declared = tag;
				}
			}
			if (declared && declared->base_type != script_value_type::jai_any_type) {
				auto converted = detail::field_store_convert(incoming, declared, engine_, sym,
					static_cast<const class_definition*>(f.code->env_lookup_cache[f.ip * 3].this_field_class));
				if (!converted) {
					return raise_from(converted);
				}
				incoming = std::move(converted.value());
			}
			if (cell->is_reference()) {
				cell->deref() = incoming;
			} else {
				*cell = incoming;
			}
			stack_.push_back(rhs_lvalue ? std::move(value) : script_value(*cell));
			return {};
		}
	}

	// Member scope: a member (field, static, or method - shadowing is by NAME)
	// outranks anything past the function's own scopes, so an env hit that could
	// only be a global yields to the kind-aware member path below.
	bool member_wins = false;
	if (!(ins.c & store_flag_global_only) && f.locals && !f.top_level) {
		ensure_frame_member_scope(f, environment_.get());
		if (frame_scope_active(f) &&
		    !detail::own_scope_lookup(environment_.get(), f.locals->scope_floor, sym)) {
			// V-S3 proven bare this-field store: == the this-field landing below with the
			// conversion gated by the proof (KEEP BYTE-PARALLEL with interpreter I-S3)
			if (bare_field_proof_frame(f)) {
				if (const identifier_expr* idn = ident_node_at(f); idn && idn->proven_class && idn->proven_field_write) {
					if (class_instance* inst = detail::proven_field_instance(*f.locals->scope_this, idn->proven_class, idn->proven_epoch)) {
						if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
							value = value.detached_for_store();
						}
						script_value incoming = rhs_lvalue ? clone_for_assignment(value) : std::move(value);
						if ((ins.c & store_flag_same_type) && idn->proven_field_type &&
						    detail::same_type_payload(incoming, idn->proven_field_type)) {
							if (detail::retags_on_field_write(idn->proven_field_type)) {
								incoming.set_type_info(type_info_ptr{idn->proven_field_type});
							}
						} else {
							auto enforced = inst->enforce_field_write(sym, std::move(incoming));
							if (!enforced) {
								return raise_from(enforced);
							}
							incoming = std::move(enforced).value();
						}
						inst->field_slot_write(idn->proven_slot, std::move(incoming));
						stack_.push_back(rhs_lvalue ? std::move(value) : script_value(inst->get_field(sym)));
						return {};
					}
				}
			}
			member_wins = detail::probe_member_scope(frame_scope_state(f), f.locals->scope_this,
			                                         f.locals->scope_static, sym,
			                                         /*want_methods*/ false).what != detail::member_probe_hit::kind::miss ||
			              detail::member_scope_has_method(frame_scope_state(f), f.locals->scope_this,
			                                              f.locals->scope_static, sym);
		}
	}

	// ONE fallback-free walk replaces the old contains() + get_value_ptr() +
	// assign()-rewalk TRIPLE walk (non-null exactly when contains(); this/static
	// field stores keep falling through to the kind-aware path below)
	script_value* store_target = nullptr;
	if (ins.c & store_flag_global_only) {
		// ::name store - GLOBAL env only; a miss is an error, never a member/implicit define
		store_target = engine_->get_global_environment()->get_env_var_ptr(sym);
		if (!store_target) {
			return raise_(make_error_code(runtime_error_code::undefined_variable),
				"Undefined variable '{0}'", sym);
		}
	} else if (!member_wins) {
		store_target = environment_->get_env_var_ptr(sym);
	}
	if (script_value* env_store_target = store_target) {
		script_value* currentVal = env_store_target;
		if (currentVal->is_reference()) {
			// Escape-boxed variable (cell) named as itself: redirect to the cell inner and
			// run the full assignment semantics below on it; anything else stores through
			auto* holder = currentVal->get_reference_holder();
			if (holder && holder->has_cell && !(ins.c & store_flag_ref_alias)) {
				currentVal = holder->cell();
			} else {
#ifdef JAISCRIPT_VM_PROFILE
				profile_store_last_path_ = 5;
#endif
				VM_TRY(detail::ref_store_through(*currentVal, value, engine_, symbolizer_));
				stack_.push_back(std::move(value));
				return {};
			}
		}
		// Boxed storage writes the cell inner directly (an env assign would replace the
		// handle and detach every alias); non-boxed writes through the held cell —
		// env::assign's env-var arm resolves this same pointer, so the rewalk is dead
		auto store_back = [&](script_value&& v) -> checked_result<void> {
			*currentVal = std::move(v);
			return {};
		};
#ifdef JAISCRIPT_VM_PROFILE
		profile_store_last_path_ = 6;
#endif
		if (currentVal && currentVal->is_cpp_bound()) {
			currentVal->assign_through(value);
			stack_.push_back(std::move(value));
			return {};
		}
		if (currentVal && currentVal->is_weak_ptr()) {
			script_value result = value;
			if (value.is_null()) {
				auto type_info = currentVal->get_type_info();
				VM_TRY(store_back(script_value::make_empty_weak_ptr(type_info, engine_)));
			} else if (value.is_weak_ptr()) {
				VM_TRY(store_back(std::move(value)));
			} else if (value.type() == script_value_type::jai_shared_ptr_type) {
				auto weak_type_info = currentVal->get_type_info();
				auto expected_type = weak_type_info ? weak_type_info->element_type() : nullptr;
				auto value_type_info = value.get_type_info();
				if (expected_type && value_type_info &&
				    expected_type->base_type != script_value_type::jai_any_type) {
					std::string expected_class = expected_type->type_name;
					std::string actual_class = value_type_info->element_type()
						? value_type_info->element_type()->type_name
						: value_type_info->type_name;
					if (expected_class != actual_class && engine_) {
						auto actual_def = engine_->get_class_definition(actual_class);
						if (!actual_def || !actual_def->is_subtype_of(expected_class)) {
							return raise_from(detail::typed_slot_rejection(value, *weak_type_info,
								detail::slot_site{detail::slot_kind::assignment, symbolizer_->get_string(sym), {}}, engine_));
						}
					}
				}
				auto weak_result = script_value::make_weak_ptr(value, engine_);
				if (!weak_result) {
					return raise_from(weak_result);
				}
				VM_TRY(store_back(std::move(weak_result.value())));
			} else {
				// A value-semantic object or a primitive: family D's assignment shape
				// (KEEP BYTE-PARALLEL with the interpreter's assignment twin)
				const type_info* weak_tag = currentVal->get_type_info().get();
				if (!weak_tag) { weak_tag = engine_->get_type_info_weak_ptr(nullptr); }
				return raise_from(detail::typed_slot_rejection(value, *weak_tag,
					detail::slot_site{detail::slot_kind::assignment, symbolizer_->get_string(sym), {}}, engine_));
			}
			stack_.push_back(std::move(result));
			return {};
		}
		if (currentVal && currentVal->get_type_info() &&
		    currentVal->get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
			script_value result = value;
			auto ptr_type_info = currentVal->get_type_info();
			auto expected_type = ptr_type_info->element_type();
			std::string expected_type_name = expected_type ? expected_type->type_name : "";

			// Monotonic-ladder rule (Dev ruling 2026-07, refines c81c9812): a var-held
			// (dynamic_pointee) holder behaves IDENTICALLY to a typed holder wherever
			// typed is legal - a VALUE rhs of a COMPATIBLE class (same class or subclass
			// of the held pointee) assigns INTO the shared pointee (aliases see it).
			// var's ONLY extra power is rebinding on an INCOMPATIBLE value or a primitive.
			bool dynamic_value_compatible = false;
			if (ptr_type_info->dynamic_pointee &&
			    value.type() == script_value_type::jai_object_type) {
				auto src_ti = value.get_type_info();
				std::string src_name = src_ti ? src_ti->type_name : "";
				if (!src_name.empty()) {
					dynamic_value_compatible = (src_name == expected_type_name);
					if (!dynamic_value_compatible && engine_) {
						auto src_class = engine_->get_class_definition(src_name);
						dynamic_value_compatible = src_class && src_class->is_subtype_of(expected_type_name);
					}
				}
			}

			if (value.is_null()) {
				VM_TRY(store_back(std::move(value)));
			} else if (value.is_weak_ptr()) {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Cannot assign weak_ptr to shared_ptr - use weak.lock() instead");
			} else if (value.get_type_info() &&
			           value.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
				auto value_type_info = value.get_type_info();
				auto value_element = value_type_info->element_type();
				if (ptr_type_info->dynamic_pointee) {
					// var-declared holder: '=' with a handle rhs REBINDS unchecked
					// (Dev ruling 2026-07; enforcement guards copy-assign-to-underlying,
					// not re-pointing) - carry the rebindable marker
					if (engine_ && value_element && !value_type_info->dynamic_pointee) {
						value.set_type_info(engine_->get_type_info_shared_ptr_dynamic(value_element.get()));
					}
					VM_TRY(store_back(std::move(value)));
				} else {
					std::string actual_type_name = value_element ? value_element->type_name : "";

					bool compatible = (actual_type_name == expected_type_name);
					if (!compatible && !actual_type_name.empty() && engine_) {
						auto actual_class = engine_->get_class_definition(actual_type_name);
						if (actual_class && actual_class->is_subtype_of(expected_type_name)) {
							compatible = true;
						}
					}

					if (!compatible) {
						uint64_t actual_id = value_element ? value_element->id : 0;
						uint64_t expected_id = expected_type ? expected_type->id : 0;
						return raise_(make_error_code(runtime_error_code::type_mismatch),
							"Cannot assign shared_ptr<{0}> to shared_ptr<{1}>", actual_id, expected_id);
					}

					value.set_type_info(ptr_type_info);
					VM_TRY(store_back(std::move(value)));
				}
			} else if (ptr_type_info->dynamic_pointee && !dynamic_value_compatible) {
				// var-held handle receiving an INCOMPATIBLE value or a primitive: REBIND
				// (var's dynamic power where typed would error). A COMPATIBLE value falls
				// through to the auto-unwrap path below, IDENTICAL to a typed holder (Dev
				// ruling 2026-07, refines c81c9812). Replace the stored handle with a
				// value-copy of the rhs and keep the 'any' marker so the variable stays
				// dynamic for the next '='.
				script_value rebound = clone_for_assignment(value);
				if (engine_) {
					if (auto* any_ti = engine_->get_type_info_any()) {
						rebound.set_type_info(any_ti);
					}
				}
				VM_TRY(store_back(std::move(rebound)));
			} else {
				auto holder = currentVal->get_object_holder();
				if (!holder || !holder->data) {
					return raise_(make_error_code(runtime_error_code::invalid_reference),
						"Cannot assign to null shared_ptr");
				}

				auto instance = currentVal->get_class_instance();
				if (!instance) {
					auto type_info = value.get_type_info();
					uint64_t type_id = type_info ? type_info->id : 0;
					return raise_(make_error_code(runtime_error_code::type_mismatch),
						"Cannot assign {0} to shared_ptr - object is not a class instance", type_id);
				}

				auto source_type_info = value.get_type_info();
				std::string source_type_name = source_type_info ? source_type_info->type_name : "unknown";

				bool handled = false;
				if (source_type_name == expected_type_name) {
					if (value.type() == script_value_type::jai_object_type ||
					    value.type() == script_value_type::jai_shared_ptr_type) {
						auto source_holder = const_cast<script_value&>(value).get_object_holder();
						if (source_holder && source_holder->data && source_holder->is_class_instance_wrapper) {
							auto source_instance = std::static_pointer_cast<class_instance>(source_holder->data);
							if (source_instance) {
								instance->copy_fields_from(*source_instance);
								handled = true;
							}
						}
					}
				}

				if (!handled) {
					// Safe-mode workers: operator= dispatch copies shared method values and
					// escapes to the engine backend — verdict (increment B)
					if (parallel_worker_ &&
					    instance->get_class_definition() &&
					    instance->get_class_definition()->defines_method(assign_operator_id_)) [[unlikely]] {
						return raise_(make_error_code(runtime_error_code::unsupported_operation),
							"custom operator dispatch on class instances is not admitted in a parallel body");
					}
					script_value method = instance->get_method(assign_operator_id_, false);
					if (method.is_function()) {
						const script_function& func = method.as_function();
						std::vector<script_value> args;
						args.push_back(*currentVal);
						args.push_back(std::move(value));
						auto call_result = func(args);
						if (!call_result) {
							return raise_(call_result.error(), "operator= failed");
						}
					} else {
						// No operator=: family D's assignment shape names the handle variable
						// (KEEP BYTE-PARALLEL with the interpreter's assignment twin)
						return raise_from(detail::typed_slot_rejection(value, *ptr_type_info,
							detail::slot_site{detail::slot_kind::assignment, symbolizer_->get_string(sym), {}}, engine_));
					}
				}
			}
			stack_.push_back(std::move(result));
			return {};
		}

		// Regular variable assignment with strong-types enforcement
#ifdef JAISCRIPT_VM_PROFILE
		profile_store_last_path_ = 4;
#endif
		type_info_ptr target_type = currentVal ? currentVal->get_type_info() : nullptr;

		if (target_type && target_type->base_type == script_value_type::jai_object_type) {
			auto source_type_info = value.get_type_info();
			std::string source_type_name = source_type_info ? source_type_info->type_name : "unknown";
			if (source_type_name != target_type->type_name && currentVal) {
				auto instance_result = currentVal->checked_as<std::shared_ptr<class_instance>>();
				if (instance_result) {
					auto instance = instance_result.value();
					// Safe-mode workers: verdict instead of a racing operator= (increment B)
					if (parallel_worker_ &&
					    instance->get_class_definition() &&
					    instance->get_class_definition()->defines_method(assign_operator_id_)) [[unlikely]] {
						return raise_(make_error_code(runtime_error_code::unsupported_operation),
							"custom operator dispatch on class instances is not admitted in a parallel body");
					}
					script_value method = instance->get_method(assign_operator_id_, false);
					if (method.is_function()) {
						const script_function& func = method.as_function();
						std::vector<script_value> args;
						script_value result_copy = value;
						args.push_back(*currentVal);
						args.push_back(std::move(value));
						auto call_result = func(args);
						if (call_result) {
							stack_.push_back(std::move(result_copy));
							return {};
						}
						return raise_(call_result.error(), "operator= failed");
					}
				}
			}
		}

		// V-S2: same_type proof + payload guard = identity conversion
		if (!((ins.c & store_flag_same_type) && target_type &&
		      detail::same_type_payload(value, target_type.get()))) {
			auto enforced = enforce_type_compatibility(std::move(value), target_type, symbolizer_->get_string(sym));
			if (!enforced) {
				return raise_from(enforced);
			}
			value = std::move(enforced.value());
		}

		// var targets keep a shared_ptr-tagged rhs's marker (decl parity, Dev ruling
		// 2026-07; flattening to 'any' would silently detach the handle at the copy)
		if (target_type && target_type->base_type == script_value_type::jai_any_type &&
		    (!value.get_type_info() ||
		     value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
			value.set_type_info(target_type);
		} else if (target_type && target_type->base_type == script_value_type::jai_any_type &&
		           engine_ && value.get_type_info()->element_type() &&
		           !value.get_type_info()->dynamic_pointee) {
			// var target adopting a handle: mark it rebindable (Dev ruling 2026-07)
			value.set_type_info(engine_->get_type_info_shared_ptr_dynamic(
				value.get_type_info()->element_type().get()));
		}

		if (rhs_lvalue) {
			script_value assignValue = clone_for_assignment(value);
			VM_TRY(store_back(std::move(assignValue)));
			stack_.push_back(std::move(value));
		} else {
			VM_TRY(store_back(std::move(value)));
			stack_.push_back(*currentVal);
		}
		return {};
	}

	// Variable not in environment: this-field / static-field fallback, then assign
	bool assigned_to_member = false;
	script_value* frame_this = current_this(f);
	if (frame_this) {
		script_value this_val = *frame_this;
		if (this_val.is_object()) {
			auto obj_holder = this_val.get_object_holder();
			if (obj_holder && obj_holder->is_class_instance_wrapper) {
				auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
				if (instance->has_field(sym)) {
					// A hybrid's C++ property stores through its setter, exactly like
					// this.name = v (a same-named script field shadows it)
					if (uint64_t setter_id = detail::cpp_property_setter_id(instance.get(), sym)) {
						VM_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), setter_id, value));
						assigned_to_member = true;
					}
					// Typed fields enforce like locals (declared type; auto infers)
					if (!assigned_to_member) {
						// All-detach ruling (2026-07, #12): the implicit-this MOVE path
						// stores a detached snapshot like the explicit o.f = spelling
						if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
							value = value.detached_for_store();
						}
						if (rhs_lvalue) {
							auto enforced = instance->enforce_field_write(sym, clone_for_assignment(value));
							if (!enforced) {
								return raise_from(enforced);
							}
							instance->set_field_unchecked(sym, enforced.value());
						} else {
							auto enforced = instance->enforce_field_write(sym, std::move(value));
							if (!enforced) {
								return raise_from(enforced);
							}
							instance->set_field_unchecked(sym, enforced.value());
							value = instance->get_field(sym);
						}
						this_field_store_arm(f, f.ip * 3, instance.get(), sym);
						assigned_to_member = true;
					}
				} else {
					auto class_def = instance->get_class_definition();
					if (class_def && class_def->has_static_field(sym)) {
						// All-detach (#12): implicit static store snapshots too
						if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
							value = value.detached_for_store();
						}
						// Typed statics enforce like fields (shared kernel, interp parity)
						if (rhs_lvalue) {
							auto stored = detail::store_static_member(*class_def, sym, clone_for_assignment(value));
							if (!stored) {
								return raise_from(stored);
							}
							if (assignment_result_used(f)) { value = clone_for_assignment(stored.value()); }   // the stored value is the expression's
						} else {
							auto stored = detail::store_static_member(*class_def, sym, std::move(value));
							if (!stored) {
								return raise_from(stored);
							}
							value = std::move(stored.value());
						}
						assigned_to_member = true;
					}
				}
			}
		}
	}

	// Static-method scope: bare static stores have no 'this' to route through, and
	// member_wins keeps them off the env walk - land them on the scope's class
	// (same set_static_field kernel as the instance tail above).
	if (!assigned_to_member && frame_scope_state(f) == detail::member_scope_state::statics &&
	    f.locals && f.locals->scope_static) {
		if (f.locals->scope_static->has_static_field(sym)) {
			if (value.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
				value = value.detached_for_store();
			}
			if (rhs_lvalue) {
				auto stored = detail::store_static_member(*f.locals->scope_static, sym, clone_for_assignment(value));
				if (!stored) {
					return raise_from(stored);
				}
				if (assignment_result_used(f)) { value = clone_for_assignment(stored.value()); }   // the stored value is the expression's
			} else {
				auto stored = detail::store_static_member(*f.locals->scope_static, sym, std::move(value));
				if (!stored) {
					return raise_from(stored);
				}
				value = std::move(stored.value());
			}
			assigned_to_member = true;
		}
	}

	if (!assigned_to_member) {
		if (rhs_lvalue) {
			VM_TRY(environment_->assign(sym, clone_for_assignment(value)));
		} else {
			VM_TRY(environment_->assign(sym, std::move(value)));
			script_value* stored = environment_->get_value_ptr(sym);
			value = stored ? *stored : make_null();
		}
	}
	stack_.push_back(std::move(value));
	return {};
}

op_status vm_backend::exec_compound_store(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.a];
	const uint32_t kind = ins.c & compound_kind_mask;
	const bool result_needed = (ins.c & compound_flag_result_needed) != 0;
	const bool no_result = (ins.c & compound_flag_no_result) != 0;
	script_value rightValue = std::move(stack_.back());
	stack_.pop_back();

	script_value* varPtr = (ins.c & compound_flag_global_only)
		? engine_->get_global_environment()->get_value_ptr(sym)
		: resolve_local_or_env_cached(f, ins.b, sym);
	if (varPtr) {
		// Constrained element/field ref (Tier 1 bind): route through the shared helper
		// so the compound result honors the constraint like subscript compounds do. A
		// C++ PROPERTY ref takes the same route: its deref is a scratch, the write must
		// reach the setter.
		if (varPtr->is_reference()) {
			const auto* refHolder = varPtr->get_reference_holder();
			if (refHolder && (refHolder->container_element_type || refHolder->property_ref())) {
				token_type op;
				const char* opName;
				switch (kind) {
					case compound_minus: op = token_type::minus; opName = "-"; break;
					case compound_star: op = token_type::star; opName = "*"; break;
					case compound_slash: op = token_type::slash; opName = "/"; break;
					case compound_percent: op = token_type::percent; opName = "%"; break;
					default: op = token_type::plus; opName = "+"; break;
				}
				auto result = detail::ref_compound_store_constrained(*varPtr, rightValue, op, opName,
					operator_table_, engine_, symbolizer_,
					[this](const script_value& l, token_type o, const script_value& r) {
						return evaluate_arithmetic(l, o, r);
					});
				if (!result) {
					return raise_from(result);
				}
				if (!no_result) { stack_.push_back(std::move(result.value())); }
				return {};
			}
		}
		script_value& target = varPtr->deref();
		auto leftType = target.type();
		// S8: a bound rhs decodes to a detached temp (the old shadow-index operand behavior)
		if (rightValue.deref().raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			script_value decoded = rightValue.deref().bound_decoded_temp();
			rightValue = std::move(decoded);
		}
		script_value& derefRight = rightValue.deref();

		if (has_custom_numeric_ops_) [[unlikely]] {
			detail::op_slot op_slot = detail::op_slot::none;
			switch (kind) {
				case compound_plus: op_slot = detail::op_slot::plus; break;
				case compound_minus: op_slot = detail::op_slot::minus; break;
				case compound_star: op_slot = detail::op_slot::star; break;
				default: break;
			}
			if (operator_table_) {
				if (const script_value* opFunc = operator_table_->entry(op_slot)) {
					const script_function& func = opFunc->as_function();
					std::vector<script_value> args = {target.clone(), rightValue};
					auto result = func(args);
					if (!result) {
						return raise_from(result);
					}
					target = std::move(result.value());
					if (!no_result) { stack_.push_back(result_needed ? target.clone() : target); }
					return {};
				}
			}
		}

		if (leftType == script_value_type::jai_object_type) {
			uint64_t op_symbol_id = 0;
			switch (kind) {
				case compound_plus: op_symbol_id = op_plus_id_; break;
				case compound_minus: op_symbol_id = op_minus_id_; break;
				case compound_star: op_symbol_id = op_star_id_; break;
				case compound_slash: op_symbol_id = op_slash_id_; break;
				case compound_percent: op_symbol_id = op_percent_id_; break;
				default: break;
			}
			if (op_symbol_id != 0) {
				auto custom_result = object_arithmetic_via_method(target, rightValue, op_symbol_id);
				if (custom_result.has_value()) {
					target = std::move(custom_result.value());
					if (!no_result) { stack_.push_back(result_needed ? target.clone() : target); }
					return {};
				}
			}
		}

		// §12.1 write-through: compound on a bound VARIABLE reads the live value, computes,
		// and stores back through assign_through (was a silent dead-shadow no-op).
		// KEEP BYTE-PARALLEL with the interpreter identifier-compound bound branch.
		if (target.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			const size_t rIdx = derefRight.raw_storage_index();
			switch (kind) {
				case compound_plus: {
					if (target.is_int()) {
						if (rIdx == script_value::TYPEID_INT) {
							script_int rr;
							if (!ints::try_add(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '+='"));
							target.assign_through(script_value(rr, engine_));
						} else if (rIdx == script_value::TYPEID_FLOAT) {
							target.assign_through(script_value(target.unchecked_as_int() + derefRight.unchecked_as_float(), engine_));
						} else {
							return raise_(make_error_code(runtime_error_code::type_mismatch));
						}
					} else if (target.is_float()) {
						target.assign_through(script_value(target.unchecked_as_float() + derefRight.as_float(), engine_));
					} else if (target.is_string() && rIdx == script_value::TYPEID_STRING) {
						// engine::memory_cap chokepoint: deny the append before it exists
						if (!limits_->memory_charge(derefRight.unchecked_as_string().size())) [[unlikely]] {
							return raise_from(detail::raise_memory_cap(*limits_));
						}
						target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
					} else {
						return raise_(make_error_code(runtime_error_code::type_mismatch));
					}
					break;
				}
				case compound_minus: {
					if (target.is_int()) {
						if (rIdx == script_value::TYPEID_INT) {
							script_int rr;
							if (!ints::try_sub(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '-='"));
							target.assign_through(script_value(rr, engine_));
						} else if (rIdx == script_value::TYPEID_FLOAT) {
							target.assign_through(script_value(target.unchecked_as_int() - derefRight.unchecked_as_float(), engine_));
						} else {
							return raise_(make_error_code(runtime_error_code::type_mismatch));
						}
					} else if (target.is_float()) {
						target.assign_through(script_value(target.unchecked_as_float() - derefRight.as_float(), engine_));
					} else {
						return raise_(make_error_code(runtime_error_code::type_mismatch));
					}
					break;
				}
				case compound_star: {
					if (target.is_int()) {
						if (rIdx == script_value::TYPEID_INT) {
							script_int rr;
							if (!ints::try_mul(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '*='"));
							target.assign_through(script_value(rr, engine_));
						} else if (rIdx == script_value::TYPEID_FLOAT) {
							target.assign_through(script_value(target.unchecked_as_int() * derefRight.unchecked_as_float(), engine_));
						} else {
							return raise_(make_error_code(runtime_error_code::type_mismatch));
						}
					} else if (target.is_float()) {
						target.assign_through(script_value(target.unchecked_as_float() * derefRight.as_float(), engine_));
					} else {
						return raise_(make_error_code(runtime_error_code::type_mismatch));
					}
					break;
				}
				case compound_slash: {
					if (rIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
						return raise_(make_error_code(runtime_error_code::division_by_zero));
					}
					if (rIdx == script_value::TYPEID_FLOAT && derefRight.unchecked_as_float() == 0.0) {
						return raise_(make_error_code(runtime_error_code::division_by_zero));
					}
					if (target.is_int()) {
						if (rIdx == script_value::TYPEID_INT) {
							script_int rr;
							if (!ints::try_div(target.unchecked_as_int(), derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '/='"));
							target.assign_through(script_value(rr, engine_));
						} else if (rIdx == script_value::TYPEID_FLOAT) {
							target.assign_through(script_value(target.unchecked_as_int() / derefRight.unchecked_as_float(), engine_));
						} else {
							return raise_(make_error_code(runtime_error_code::type_mismatch));
						}
					} else if (target.is_float()) {
						target.assign_through(script_value(target.unchecked_as_float() / derefRight.as_float(), engine_));
					} else {
						return raise_(make_error_code(runtime_error_code::type_mismatch));
					}
					break;
				}
				default:
					// %= matches the interpreter's in-place switch, which has no percent case
					return raise_(make_error_code(runtime_error_code::unknown_operator));
			}
			if (!no_result) { stack_.push_back(result_needed ? target.clone() : target); }
			return {};
		}

		const size_t leftIdx = target.raw_storage_index();
		const size_t rightIdx = derefRight.raw_storage_index();
		// Integral promotion (char_promotion.hpp): a char operand leaves the in-place
		// ladder and takes the general arithmetic path as int64 0..255, storing back
		// like the %= case does. KEEP BYTE-PARALLEL with the interpreter's in-place switch.
		if (detail::char_operands_promote(leftIdx, rightIdx)) [[unlikely]] {
			token_type baseOp = token_type::plus;
			switch (kind) {
				case compound_plus: baseOp = token_type::plus; break;
				case compound_minus: baseOp = token_type::minus; break;
				case compound_star: baseOp = token_type::star; break;
				case compound_slash: baseOp = token_type::slash; break;
				case compound_percent: baseOp = token_type::percent; break;
				default: return raise_(make_error_code(runtime_error_code::unknown_operator));
			}
			VM_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, baseOp, derefRight));
			VM_TRY(compound_typed_store_back(target, std::move(promoted), symbolizer_->get_string(sym)));
			if (!no_result) { stack_.push_back(result_needed ? target.clone() : target); }
			return {};
		}
		const bool bothInt = leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_INT;
		switch (kind) {
			case compound_plus: {
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_add(tref, derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '+='"));
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() += derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					VM_TRY(compound_typed_store_back(target, script_value(target.unchecked_as_int() + derefRight.unchecked_as_float(), engine_), symbolizer_->get_string(sym)));
				} else if (leftIdx == script_value::TYPEID_STRING && rightIdx == script_value::TYPEID_STRING) {
					// engine::memory_cap chokepoint: deny the append before it exists
					if (!limits_->memory_charge(derefRight.unchecked_as_string().size())) [[unlikely]] {
						return raise_from(detail::raise_memory_cap(*limits_));
					}
					target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
				} else if (leftIdx == script_value::TYPEID_STRING && rightIdx == script_value::TYPEID_CHAR) {
					// string += char appends the char as TEXT (out += to_char(b),
					// the binary-writer shape) — a char is a text unit. Non-char
					// rhs keeps the Strong Types raise (string += int stays an
					// error, unlike binary +, by pinned design). KEEP
					// BYTE-PARALLEL with the interpreter's in-place switch.
					VM_TRY_ASSIGN(script_value joined, evaluate_arithmetic(target, token_type::plus, derefRight));
					VM_TRY(compound_typed_store_back(target, std::move(joined), symbolizer_->get_string(sym)));
				} else {
					return raise_(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_minus: {
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_sub(tref, derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '-='"));
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() -= derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					VM_TRY(compound_typed_store_back(target, script_value(target.unchecked_as_int() - derefRight.unchecked_as_float(), engine_), symbolizer_->get_string(sym)));
				} else {
					return raise_(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_star: {
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_mul(tref, derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '*='"));
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() *= derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					VM_TRY(compound_typed_store_back(target, script_value(target.unchecked_as_int() * derefRight.unchecked_as_float(), engine_), symbolizer_->get_string(sym)));
				} else {
					return raise_(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_slash: {
				if (rightIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
					return raise_(make_error_code(runtime_error_code::division_by_zero));
				}
				if (rightIdx == script_value::TYPEID_FLOAT && derefRight.unchecked_as_float() == 0.0) {
					return raise_(make_error_code(runtime_error_code::division_by_zero));
				}
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_div(tref, derefRight.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '/='"));
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() /= derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					VM_TRY(compound_typed_store_back(target, script_value(target.unchecked_as_int() / derefRight.unchecked_as_float(), engine_), symbolizer_->get_string(sym)));
				} else {
					return raise_(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_percent: {
				if (rightIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
					return raise_(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
				}
				VM_TRY_ASSIGN(script_value promoted, evaluate_arithmetic(target, token_type::percent, derefRight));
				VM_TRY(compound_typed_store_back(target, std::move(promoted), symbolizer_->get_string(sym)));
				break;
			}
			default:
				return raise_(make_error_code(runtime_error_code::unknown_operator));
		}

		if (!no_result) { stack_.push_back(result_needed ? target.clone() : target); }
		return {};
	}

	// Implicit this.member fallback (::name targets never reach members - a global
	// miss is an error, sequenced after the rhs like every undefined-target store)
	if (ins.c & compound_flag_global_only) {
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}'", sym);
	}
	script_value* frame_this = current_this(f);
	if (!frame_this || !frame_this->is_object()) {
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}' (no 'this' in scope)", sym);
	}

	script_value this_val = *frame_this;
	std::shared_ptr<class_instance> instance = this_val.get_class_instance();

	if (!instance || !instance->has_field(sym)) {
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}' (not a field of 'this')", sym);
	}

	// A hybrid's C++ property reads through its getter and stores through its setter
	// (KEEP BYTE-PARALLEL with the interpreter implicit-this compound tail)
	const uint64_t prop_getter = detail::cpp_property_getter_id(instance.get(), sym);
	const uint64_t prop_setter = prop_getter ? detail::cpp_property_setter_id(instance.get(), sym) : 0;
	script_value currentValue = make_null();
	if (prop_getter) {
		VM_TRY_ASSIGN(currentValue, detail::read_bare_cpp_property(this_val, instance.get(),
			instance->get_method(prop_getter, false), prop_getter, sym));
	} else {
		currentValue = instance->get_field(sym);
	}

	if (currentValue.is_object()) {
		uint64_t op_symbol_id = 0;
		switch (kind) {
			case compound_plus: op_symbol_id = op_plus_id_; break;
			case compound_minus: op_symbol_id = op_minus_id_; break;
			case compound_star: op_symbol_id = op_star_id_; break;
			case compound_slash: op_symbol_id = op_slash_id_; break;
			case compound_percent: op_symbol_id = op_percent_id_; break;
			default: break;
		}
		if (op_symbol_id != 0) {
			auto custom_result = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
			if (custom_result.has_value()) {
				if (prop_setter) {
					VM_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, custom_result.value()));
				} else {
					auto enforced = instance->enforce_field_write(sym, clone_for_assignment(custom_result.value()));
					if (!enforced) {
						return raise_from(enforced);
					}
					instance->set_field_unchecked(sym, enforced.value());
				}
				if (!no_result) { stack_.push_back(std::move(custom_result.value())); }
				return {};
			}
		}
	}

	// S8: bound-alias class fields decode-read LIVE; set_field stores the detached result (§12.5).
	// KEEP BYTE-PARALLEL with the interpreter implicit-this compound normalization.
	if (currentValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		currentValue = currentValue.bound_decoded_temp();
	if (rightValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		rightValue = rightValue.bound_decoded_temp();

	const size_t ci = currentValue.raw_storage_index();
	const size_t ri = rightValue.raw_storage_index();
	script_value resultValue = make_null();
	switch (kind) {
		case compound_plus:
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_add(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '+='"));
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf + rf, engine_);
			} else if (ci == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
				// engine::memory_cap chokepoint: deny the concat result before it exists
				if (!limits_->memory_charge(sizeof(script_value) + currentValue.unchecked_as_string().size() + rightValue.unchecked_as_string().size())) [[unlikely]] {
					return raise_from(detail::raise_memory_cap(*limits_));
				}
				resultValue = script_value(currentValue.unchecked_as_string() + rightValue.unchecked_as_string(), engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_minus:
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_sub(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '-='"));
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf - rf, engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_star:
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_mul(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '*='"));
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf * rf, engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_slash:
			if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
				return raise_(make_error_code(runtime_error_code::division_by_zero));
			}
			if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf / rf, engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_percent: {
			if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
				return raise_(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
			}
			VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
			break;
		}
		default:
			return raise_(make_error_code(runtime_error_code::unknown_operator));
	}

	// Store the enforced (declared-type converted) value; the expression result stays the
	// promoted value, matching member/element compound semantics
	if (prop_setter) {
		VM_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, resultValue));
	} else {
		auto enforced = instance->enforce_field_write(sym, clone_for_assignment(resultValue));
		if (!enforced) {
			return raise_from(enforced);
		}
		instance->set_field_unchecked(sym, enforced.value());
	}
	if (!no_result) { stack_.push_back(std::move(resultValue)); }
	return {};
}

op_status vm_backend::exec_incdec(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.a];
	const bool postfix = (ins.c & incdec_flag_postfix) != 0;
	const bool isIncrement = (ins.c & incdec_flag_increment) != 0;

	script_value* varPtr = (ins.c & incdec_flag_global_only)
		? engine_->get_global_environment()->get_value_ptr(sym)
		: resolve_local_or_env_cached(f, ins.b, sym);
	if (varPtr) {
		// Scratch-backed ref (typed element, C++ property): deref hands back the holder
		// scratch - the in-place mutation below must commit it (buffer store / setter)
		// (KEEP BYTE-PARALLEL with the interpreter ++/-- twin)
		auto* scratch_ref = varPtr->scratch_ref_holder();
		script_value& target = varPtr->deref();
		// §12.1 write-through: ++/-- on a bound VARIABLE decodes the live value and stores
		// back through assign_through (the type() switch below would mutate a dead shadow).
		// KEEP BYTE-PARALLEL with the interpreter ++/-- bound branch.
		if (target.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			if (target.is_int()) {
				const script_int oldVal = target.unchecked_as_int();
				script_int newVal;
				if (isIncrement) {
					if (!ints::try_add(oldVal, 1, newVal)) return raise_from(vm_int_overflow_v("Integer overflow in '++'"));
				} else {
					if (!ints::try_sub(oldVal, 1, newVal)) return raise_from(vm_int_overflow_v("Integer overflow in '--'"));
				}
				target.assign_through(script_value(newVal, engine_));
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			}
			if (target.is_float()) {
				const script_float oldVal = target.unchecked_as_float();
				const script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
				target.assign_through(script_value(newVal, engine_));
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			}
			return raise_(make_error_code(runtime_error_code::invalid_numeric_operand));
		}
		switch (target.type()) {
			case script_value_type::jai_int_type: {
				// Overflow policy applies to plain ++/-- like every other int op
				// (INCDEC-WRAPS-SILENTLY); wrap builds fold the check away.
				script_int& tref = target.unchecked_as_int_ref();
				const script_int oldVal = tref;
				script_int newVal;
				if (isIncrement) {
					if (!ints::try_add(oldVal, 1, newVal)) return raise_from(vm_int_overflow_v("Integer overflow in '++'"));
				} else {
					if (!ints::try_sub(oldVal, 1, newVal)) return raise_from(vm_int_overflow_v("Integer overflow in '--'"));
				}
				tref = newVal;
				if (scratch_ref) [[unlikely]] { scratch_ref->commit_scratch(); }
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			}
			case script_value_type::jai_float_type: {
				if (postfix) {
					stack_.push_back(script_value(target.unchecked_as_float(), engine_));
					if (isIncrement) target.unchecked_as_float_ref() += 1.0;
					else target.unchecked_as_float_ref() -= 1.0;
				} else {
					if (isIncrement) target.unchecked_as_float_ref() += 1.0;
					else target.unchecked_as_float_ref() -= 1.0;
					stack_.push_back(script_value(target.unchecked_as_float(), engine_));
				}
				if (scratch_ref) [[unlikely]] { scratch_ref->commit_scratch(); }
				return {};
			}
			default:
				return raise_(make_error_code(runtime_error_code::invalid_numeric_operand));
		}
	}

	// ::name targets never reach the implicit this.member fallback
	if (ins.c & incdec_flag_global_only) {
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}'", sym);
	}
	script_value* frame_this = current_this(f);
	if (frame_this && frame_this->is_object()) {
		script_value this_val = *frame_this;
		std::shared_ptr<class_instance> instance = this_val.get_class_instance();
		if (instance && instance->has_field(sym)) {
			// A hybrid's C++ property reads through its getter and stores through its setter
			// (KEEP BYTE-PARALLEL with the interpreter ++/-- this-tail)
			const uint64_t prop_getter = detail::cpp_property_getter_id(instance.get(), sym);
			const uint64_t prop_setter = prop_getter ? detail::cpp_property_setter_id(instance.get(), sym) : 0;
			script_value currentVal = make_null();
			if (prop_getter) {
				VM_TRY_ASSIGN(currentVal, detail::read_bare_cpp_property(this_val, instance.get(),
					instance->get_method(prop_getter, false), prop_getter, sym));
			} else {
				currentVal = instance->get_field(sym);
			}
			// S8: a bound-alias field decodes live; set_field stores the detached result (§12.5)
			if (currentVal.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
				currentVal = currentVal.bound_decoded_temp();
			const size_t ti = currentVal.raw_storage_index();
			if (ti == script_value::TYPEID_INT) {
				script_int oldVal = currentVal.unchecked_as_int();
				script_int newVal;
				if (isIncrement) {
					if (!ints::try_add(oldVal, 1, newVal)) return raise_from(vm_int_overflow_v("Integer overflow in '++'"));
				} else {
					if (!ints::try_sub(oldVal, 1, newVal)) return raise_from(vm_int_overflow_v("Integer overflow in '--'"));
				}
				if (prop_setter) {
					VM_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, script_value(newVal, engine_)));
				} else {
					instance->set_field(sym, script_value(newVal, engine_));
				}
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			} else if (ti == script_value::TYPEID_FLOAT) {
				script_float oldVal = currentVal.unchecked_as_float();
				script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
				if (prop_setter) {
					VM_TRY(detail::write_cpp_property(this_val, instance->get_class_definition(), prop_setter, script_value(newVal, engine_)));
				} else {
					instance->set_field(sym, script_value(newVal, engine_));
				}
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			} else {
				return raise_(make_error_code(runtime_error_code::invalid_numeric_operand));
			}
		}
	}
	return raise_(make_error_code(runtime_error_code::undefined_variable),
		"Undefined variable '{0}'", sym);
}

op_status vm_backend::exec_unary(frame& f, const vm_instruction& ins) {
	script_value operand = std::move(stack_.back());
	stack_.pop_back();

	// `!` consults truthiness BEFORE the S8 decode: is_truthy handles bound values in
	// full, and decoding an OPAQUE bound to null would lie about its truthiness (§13,
	// 2026-07). Identical result for bound primitives. Byte-parallel with the interpreter.
	if (static_cast<token_type>(ins.a) == token_type::bang) {
		stack_.push_back(script_value(!is_truthy(operand), engine_));
		return {};
	}

	size_t oi = operand.raw_storage_index();
	// S8: bound operand decodes in place to a detached temp (byte-parallel with the interpreter unary switch)
	if (oi == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
		operand = operand.bound_decoded_temp();
		oi = operand.raw_storage_index();
	}
	switch (static_cast<token_type>(ins.a)) {
		case token_type::minus: {
			if (oi == script_value::TYPEID_INT) {
				script_int neg;
				if (!ints::try_neg(operand.unchecked_as_int(), neg)) {
					return raise_from(vm_int_overflow_v("Integer overflow in unary '-'"));
				}
				stack_.push_back(script_value(neg, engine_));
			} else if (oi == script_value::TYPEID_FLOAT) {
				stack_.push_back(script_value(-operand.unchecked_as_float(), engine_));
			} else {
				return raise_(make_error_code(runtime_error_code::invalid_numeric_operand), "Unary minus requires numeric operand");
			}
			return {};
		}
		case token_type::bang:
			stack_.push_back(script_value(!is_truthy(operand), engine_));
			return {};
		case token_type::tilde:
			if (oi != script_value::TYPEID_INT) {
				return raise_(make_error_code(runtime_error_code::invalid_numeric_operand), "Bitwise NOT requires integer operand");
			}
			stack_.push_back(script_value(~operand.unchecked_as_int(), engine_));
			return {};
		default:
			return raise_(make_error_code(runtime_error_code::unknown_operator));
	}
}

op_status vm_backend::exec_binary(frame& f, const vm_instruction& ins) {
	script_value right_raw = std::move(stack_.back());
	stack_.pop_back();
	script_value left_raw = std::move(stack_.back());
	stack_.pop_back();
	const script_value& left = left_raw.deref();
	const script_value& right = right_raw.deref();
	const token_type op = static_cast<token_type>(ins.a);

	if (ins.b != binary_shape_none && !has_custom_numeric_ops_ && is_numeric_binary_op(op)) {
		std::optional<checked_result<script_value>> fast;
		if (binary_fast_shape(op, ins.b, left, right, fast)) {
			if (!*fast) {
				return raise_from(*fast);
			}
			stack_.push_back(std::move(fast->value()));
			return {};
		}
	}

	auto result = binary_general(op, left, right);
	if (!result) {
		return raise_from(result);
	}
	stack_.push_back(std::move(result.value()));
	return {};
}

const script_value* vm_backend::fused_ident_value(frame& f, const fused_operand& operand,
                                                  std::optional<script_value>& scratch,
                                                  size_t cache_slot) {
	const uint64_t sym = f.code->symbols[operand.symbol];
	if (operand.load_flags & load_flag_global_only) {
		// ::name - GLOBAL env only (KEEP BYTE-PARALLEL with exec_load's door)
		if (script_value* g = engine_->get_global_environment()->get_value_ptr(sym)) {
			return &g->deref();
		}
		raise_(make_error_code(runtime_error_code::undefined_variable), "Undefined variable '{0}'", sym);
		return nullptr;
	}
	if (operand.slot != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* local = frame_slot(f, operand.slot)) {
			return &local->deref();
		}
	}
	if (operand.load_flags & load_flag_type_ctor) {
		std::string_view name = symbolizer_->get_string(sym);
		size_t pos = name.find('<');
		std::string base_type(name.substr(0, pos));
		// nullable-pointer probe (checked_result stage 1): same ladder, null miss
		if (script_value* ctor = environment_->get_value_ptr(symbolizer_->intern(base_type));
		    ctor && ctor->is_function()) {
			scratch.emplace(*ctor);
			return &scratch.value();
		}
	}
	// This-field IC: serial-guarded walk-miss proof (see this_field_ic_probe), checked
	// BEFORE the env cache — an unchanged serial proves the whole chain unchanged since
	// a full walk missed here, so the cache could only re-miss (its hits are chain
	// cells). The scratch copy is byte-identical to the armed fallback below (get_field
	// with a live cell returns *find_field_value); a null probe (unarmed / stale serial
	// / absent cell — defaults, methods, statics) falls through to the full ladder.
	if (script_value* ic_field = this_field_ic_probe(f, cache_slot, sym)) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_resolve_[8];
#endif
		scratch.emplace(*ic_field);
		return &scratch.value();
	}
	// Member scope: exec_load's ladder verbatim in fused shape (own scopes ->
	// members -> cache/chain); member hits land in scratch exactly like the old
	// tail's copies, so downstream consumers are untouched.
	if (f.locals && !f.top_level) {
		ensure_frame_member_scope(f, environment_.get());
		if (frame_scope_active(f)) {
			if (script_value* own = detail::own_scope_lookup(environment_.get(), f.locals->scope_floor, sym)) {
				return &own->deref();
			}
			if (operand.ident_node != k_invalid_u32 && bare_field_proof_frame(f)) {
				const identifier_expr* idn = ident_node_of(*f.code, operand.ident_node);
				if (idn->proven_class) {
					if (script_value* cell = detail::proven_field_cell(*f.locals->scope_this, idn->proven_class,
					                                                   idn->proven_epoch, idn->proven_slot, nullptr)) {
						scratch.emplace(*cell);   // V-I1: == the probe's scratch copy of *member.value_ptr
						return &scratch.value();
					}
				}
			}
			auto member = detail::probe_member_scope(frame_scope_state(f), f.locals->scope_this,
			                                         f.locals->scope_static, sym, /*want_methods*/ true);
			if (member) {
				if (member.what == detail::member_probe_hit::kind::method) {
					if (member.instance && f.locals->scope_this) {
						scratch.emplace(make_bound_method_thunk(*f.locals->scope_this, std::move(member.method_value)));
					} else {
						scratch.emplace(std::move(member.method_value));
					}
					return &scratch.value();
				}
				if (member.what == detail::member_probe_hit::kind::cpp_property) {
					auto read = detail::read_bare_cpp_property(*f.locals->scope_this, member.instance,
					                                           member.method_value, member.getter_id, sym);
					if (!read) {
						raise_from(read);
						return nullptr;
					}
					scratch.emplace(std::move(read.value()));
					return &scratch.value();
				}
				if (member.what == detail::member_probe_hit::kind::field && member.instance) {
					this_field_ic_arm(f, cache_slot, member.instance, sym);
				}
				scratch.emplace(*member.value_ptr);
				return &scratch.value();
			}
			if (script_value* cached = env_lookup_cached(f, cache_slot, sym)) {
				return &cached->deref();
			}
			if (script_value* beyond = f.locals->scope_floor
			        ? f.locals->scope_floor->get_value_ptr(sym)
			        : environment_->get_value_ptr(sym)) {
				return &beyond->deref();
			}
			raise_(make_error_code(runtime_error_code::undefined_variable), "Undefined variable '{0}'", sym);
			return nullptr;
		}
	}
	if (script_value* cached = env_lookup_cached(f, cache_slot, sym)) {
		return &cached->deref();
	}
#ifdef JAISCRIPT_VM_PROFILE
	++profile_env_resolve_[5];
	++profile_env_walk_names_[std::string(symbolizer_->get_string(sym))];
#endif
	// get_value_ptr IS get_ref's exact resolution ladder minus the error wrapper;
	// env storage is deque-stable, so the pointer outlives this call like get_ref's did
	if (script_value* env_val = environment_->get_value_ptr(sym)) {
		return &env_val->deref();
	}
	if (script_value* this_ptr = current_this(f)) {
		script_value& this_val = *this_ptr;
		if (this_val.is_object()) {
			std::shared_ptr<class_instance> instance = this_val.get_class_instance();
			if (instance) {
				if (uint64_t getter_id = detail::cpp_property_getter_id(instance.get(), sym)) {
					auto read = detail::read_bare_cpp_property(this_val, instance.get(),
					                                           instance->get_method(getter_id, false), getter_id, sym);
					if (!read) {
						raise_from(read);
						return nullptr;
					}
					scratch.emplace(std::move(read.value()));
					return &scratch.value();
				}
				if (instance->has_field(sym)) {
					// The walk above just MISSED and this site resolves a field: bank
					// the proof so the next access skips the walk while the serial holds
					this_field_ic_arm(f, cache_slot, instance.get(), sym);
#ifdef JAISCRIPT_VM_PROFILE
					++profile_env_resolve_[9];
#endif
					scratch.emplace(instance->get_field(sym));
					return &scratch.value();
				}
				script_value method = instance->get_method(sym, false);
				if (!method.is_invalid()) {
					scratch.emplace(make_bound_method_thunk(this_val, std::move(method)));
					return &scratch.value();
				}
				auto class_def = instance->get_class_definition();
				if (class_def && class_def->has_static_field(sym)) {
					scratch.emplace(class_def->get_static_field(sym));
					return &scratch.value();
				}
			}
		}
	}
	raise_(make_error_code(runtime_error_code::undefined_variable), "Undefined variable '{0}'", sym);
	return nullptr;
}

// Fused subscript-read operand `base[i][j]...[k]` (docs/element_read_overhead_design.md
// Stage 3, N-level): array levels step a pointer IN PLACE - zero copies, no op_index
// dispatch. Sound because index resolution and the other operand's resolution run no
// user code (idents/literals/element reads only) and the numeric fast path consumes
// scalars immediately. Any other level (map key, string, object, borrow, non-int index,
// OOB) REPLAYS that one level through the exact unfused op_index (lvalue_shape +
// transient flags, matching what the classifier stamped on every chain node) into
// scratch, then the walk continues - semantics and error text byte-identical per level
// by construction.
const script_value* vm_backend::fused_subscript_value(frame& f, const fused_operand& operand,
                                                      std::optional<script_value>& scratch,
                                                      size_t cache_slot) {
	std::optional<script_value> base_scratch;
	const script_value* current = fused_ident_value(f, operand, base_scratch, cache_slot);
	if (!current) return nullptr;

	for (const fused_subscript_index& level : operand.subscript_chain) {
		std::optional<script_value> index_scratch;
		const script_value* index_ptr = nullptr;
		script_value index_literal_value(std::monostate{}, engine_);
		if (level.kind == fused_subscript_index::index_literal) {
			index_literal_value = script_value(static_cast<script_int>(level.literal), engine_);
			index_ptr = &index_literal_value;
		} else {
			fused_operand index_operand;
			index_operand.slot = level.slot;
			index_operand.symbol = level.symbol;
			// SIZE_MAX: no reserved cache role for indexes - a shared slot would alias
			// different index symbols in one instruction (the cache is not symbol-checked)
			index_ptr = fused_ident_value(f, index_operand, index_scratch, SIZE_MAX);
			if (!index_ptr) return nullptr;
		}

		if (current->raw_storage_index() == script_value::TYPEID_ARRAY && index_ptr->is_int()) {
			const script_int index = index_ptr->unchecked_as_int();
			const script_array* node = current->unchecked_array_node();
			if (index >= 0 && index < static_cast<script_int>(node->size())) {
				if (node->is_typed()) {
					// raw buffer read into scratch. Materialize into a LOCAL first:
					// `current` may point INTO the old scratch, and emplace destroys it
					// (which would drop the node's owning handle mid-read)
					script_value elem = node->get(static_cast<size_t>(index), engine_);
					scratch.emplace(std::move(elem));
					current = &scratch.value();
					continue;
				}
				current = &node->values()[static_cast<size_t>(index)].deref();
				continue;
			}
		}

		// Slow replay for THIS level: identical to the unfused op_index it replaced.
		// The container copy goes onto the stack before scratch is overwritten, so
		// replacing a scratch the walk was pointing into is safe.
		stack_.push_back(*current);
		stack_.push_back(*index_ptr);
		vm_instruction index_ins{opcode::op_index, index_flag_lvalue_shape | index_flag_transient_read, 0, 0};
		auto indexed = exec_index(f, index_ins);
		if (indexed == op_status::failed) return nullptr;
		script_value element = std::move(stack_.back());
		stack_.pop_back();
		scratch.emplace(std::move(element));
		current = &scratch.value().deref();
	}
	return current;
}

// Register-file sequencing producer: a subscript operand LEFT of a lifting subtree
// reads (and raises) here, before that subtree computes, and lands RAW in its temp;
// the outer op reads it back as an ordinary slot operand.
op_status vm_backend::exec_operand_temp(frame& f, const vm_instruction& ins) {
	std::optional<script_value> scratch;
	const script_value* v = fused_subscript_value(f, f.code->fused_binary_protos[ins.a].left, scratch, f.ip * 3);
	if (!v) return op_status::failed;
	script_value out = *v;
	frame_slot_set(f, ins.b, std::move(out));
	return {};
}

op_status vm_backend::exec_binary_fused(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	if (f.ip + 1 < f.code->code.size()) { ++profile_binary_fused_next_[static_cast<uint8_t>(f.code->code[f.ip + 1].op)]; }
#endif
	return binary_fused_compute(f, ins.a, [this](script_value&& v) { stack_.push_back(std::move(v)); });
}

const script_value* vm_backend::direct_operand(frame& f, const fused_operand& operand) noexcept {
	if (operand.const_index != k_invalid_u32) {
		return &f.code->constants[operand.const_index];
	}
	if (f.window_backed) {
		return operand.slot < f.window_live ? &stack_[f.window_base + operand.slot] : nullptr;
	}
	return f.locals ? f.locals->get_local(operand.slot) : nullptr;
}

// int/int and float/float rows only; every error case (overflow, a zero divisor) is left
// to the general kernel so the texts stay its own
template <typename Sink>
bool vm_backend::typed_binary_head(frame& f, const fused_binary_proto& p, Sink& sink) {
	const script_value* lp = direct_operand(f, p.left);
	const script_value* rp = direct_operand(f, p.right);
	if (!lp || !rp) return false;
	const token_type op = static_cast<token_type>(p.op);
	if (p.typed_class == 1) {
		if (lp->raw_storage_index() != script_value::TYPEID_INT || rp->raw_storage_index() != script_value::TYPEID_INT) return false;
		const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
		auto emit_int = [&](script_int v) {
			if constexpr (requires(Sink& s) { s.raw_int(script_int{}); }) { sink.raw_int(v); }
			else { sink(script_value(v, engine_)); }
		};
		script_int r;
		switch (op) {
		case token_type::plus: if (!ints::try_add(a, b, r)) return false; emit_int(r); return true;
		case token_type::minus: if (!ints::try_sub(a, b, r)) return false; emit_int(r); return true;
		case token_type::star: if (!ints::try_mul(a, b, r)) return false; emit_int(r); return true;
		case token_type::slash: if (b == 0 || !ints::try_div(a, b, r)) return false; emit_int(r); return true;
		case token_type::percent: if (b == 0) return false; emit_int(ints::mod(a, b)); return true;
		// bitwise rows: handle_binary_op's int cases verbatim (a shift outside 0..63 raises there)
		case token_type::ampersand: emit_int(a & b); return true;
		case token_type::pipe: emit_int(a | b); return true;
		case token_type::caret: emit_int(a ^ b); return true;
		case token_type::left_shift: if (b < 0 || b > 63) return false; emit_int(a << b); return true;
		case token_type::right_shift: if (b < 0 || b > 63) return false; emit_int(a >> b); return true;
		case token_type::less: sink(script_value(a < b, engine_)); return true;
		case token_type::less_equal: sink(script_value(a <= b, engine_)); return true;
		case token_type::greater: sink(script_value(a > b, engine_)); return true;
		case token_type::greater_equal: sink(script_value(a >= b, engine_)); return true;
		case token_type::equal_equal: sink(script_value(a == b, engine_)); return true;
		case token_type::bang_equal: sink(script_value(a != b, engine_)); return true;
		default: return false;
		}
	}
	if (lp->raw_storage_index() != script_value::TYPEID_FLOAT || rp->raw_storage_index() != script_value::TYPEID_FLOAT) return false;
	const script_float a = lp->unchecked_as_float(), b = rp->unchecked_as_float();
	auto emit_float = [&](script_float v) {
		if constexpr (requires(Sink& s) { s.raw_float(script_float{}); }) { sink.raw_float(v); }
		else { sink(script_value(v, engine_)); }
	};
	switch (op) {
	case token_type::plus: emit_float(a + b); return true;
	case token_type::minus: emit_float(a - b); return true;
	case token_type::star: emit_float(a * b); return true;
	case token_type::slash: if (b == 0.0) return false; emit_float(a / b); return true;
	case token_type::percent: if (b == 0.0) return false; emit_float(std::fmod(a, b)); return true;
	case token_type::less: sink(script_value(a < b, engine_)); return true;
	case token_type::less_equal: sink(script_value(a <= b, engine_)); return true;
	case token_type::greater: sink(script_value(a > b, engine_)); return true;
	case token_type::greater_equal: sink(script_value(a >= b, engine_)); return true;
	case token_type::equal_equal: sink(script_value(a == b, engine_)); return true;
	case token_type::bang_equal: sink(script_value(a != b, engine_)); return true;
	default: return false;
	}
}

bool vm_backend::typed_cmp_jump_head(frame& f, const vm_instruction& ins, const fused_binary_proto& p) noexcept {
	const script_value* lp = direct_operand(f, p.left);
	const script_value* rp = direct_operand(f, p.right);
	if (!lp || !rp) return false;
	bool truthy;
	if (p.typed_class == 1) {
		if (lp->raw_storage_index() != script_value::TYPEID_INT || rp->raw_storage_index() != script_value::TYPEID_INT) return false;
		const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
		switch (static_cast<token_type>(p.op)) {
		case token_type::less: truthy = a < b; break;
		case token_type::less_equal: truthy = a <= b; break;
		case token_type::greater: truthy = a > b; break;
		case token_type::greater_equal: truthy = a >= b; break;
		case token_type::equal_equal: truthy = a == b; break;
		case token_type::bang_equal: truthy = a != b; break;
		default: return false;
		}
	} else {
		if (lp->raw_storage_index() != script_value::TYPEID_FLOAT || rp->raw_storage_index() != script_value::TYPEID_FLOAT) return false;
		const script_float a = lp->unchecked_as_float(), b = rp->unchecked_as_float();
		switch (static_cast<token_type>(p.op)) {
		case token_type::less: truthy = a < b; break;
		case token_type::less_equal: truthy = a <= b; break;
		case token_type::greater: truthy = a > b; break;
		case token_type::greater_equal: truthy = a >= b; break;
		case token_type::equal_equal: truthy = a == b; break;
		case token_type::bang_equal: truthy = a != b; break;
		default: return false;
		}
	}
	f.ip = truthy ? f.ip + 1 : ins.a;
	return true;
}

// The ONE fused-binary computation, sink-templated (flatstack stage 6): op_binary_fused
// pushes; the dest-addressed variants land the result in a slot/decl with no push and
// no second dispatch. One body, every sink - parity by construction.
template <typename Sink>
op_status vm_backend::binary_fused_compute(frame& f, uint32_t proto_index, Sink&& sink) {
	const fused_binary_proto& p = f.code->fused_binary_protos[proto_index];
#ifdef JAISCRIPT_VM_PROFILE
	if (p.typed_class == 0) { ++profile_typed_head_[2]; }
#endif
	if (p.typed_class != 0 && !has_custom_numeric_ops_ && typed_binary_head(f, p, sink)) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_typed_head_[0];
#endif
		return {};
	}
#ifdef JAISCRIPT_VM_PROFILE
	if (p.typed_class != 0) { ++profile_typed_head_[1]; }
#endif
	const token_type op = static_cast<token_type>(p.op);

	std::optional<script_value> lscratch, rscratch;
	const script_value* lp;
	const script_value* rp;

	// Identifier operands try the nullable fast head first (the exact subset of
	// fused_ident_value's ladder that exec_fused_cmp_jump already trusts: slot cell,
	// member-scope field cell, or armed env cell); a null answer runs the full ladder,
	// so every error and side effect keeps its place.
	constexpr bool ident_fast_head = true;
	if (p.left.const_index != k_invalid_u32) {
		lp = &f.code->constants[p.left.const_index];   // engine-less template: fast path reads raw
	} else if (p.left.is_subscript()) {
		lp = fused_subscript_value(f, p.left, lscratch, f.ip * 3);
		if (!lp) return op_status::failed;
	} else if (!(ident_fast_head && (lp = fused_cmp_operand(f, p.left, f.ip * 3)) != nullptr)) {
		lp = fused_ident_value(f, p.left, lscratch, f.ip * 3);
		if (!lp) return op_status::failed;
	}
	if (p.right.const_index != k_invalid_u32) {
		rp = &f.code->constants[p.right.const_index];
	} else if (p.right.is_subscript()) {
		rp = fused_subscript_value(f, p.right, rscratch, f.ip * 3 + 1);
		if (!rp) return op_status::failed;
	} else if (!(ident_fast_head && (rp = fused_cmp_operand(f, p.right, f.ip * 3 + 1)) != nullptr)) {
		rp = fused_ident_value(f, p.right, rscratch, f.ip * 3 + 1);
		if (!rp) return op_status::failed;
	}

	// Fast path: mirrors binary_fast_shape without materializing operand loads.
	// Raw-capable sinks (slot-landing consumers) take scalar results with no value
	// mint at all — every other sink gets the identical minted value it always did.
	auto emit_int = [&](script_int v) {
		if constexpr (requires(Sink& s) { s.raw_int(script_int{}); }) { sink.raw_int(v); }
		else { sink(script_value(v, engine_)); }
	};
	auto emit_float = [&](script_float v) {
		if constexpr (requires(Sink& s) { s.raw_float(script_float{}); }) { sink.raw_float(v); }
		else { sink(script_value(v, engine_)); }
	};
	if (!has_custom_numeric_ops_ && is_numeric_binary_op(op)) {
		const size_t li = lp->raw_storage_index();
		const size_t ri = rp->raw_storage_index();
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
			const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
			switch (op) {
			case token_type::plus: { script_int rr; if (!ints::try_add(a, b, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '+'")); emit_int(rr); return {}; }
			case token_type::minus: { script_int rr; if (!ints::try_sub(a, b, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '-'")); emit_int(rr); return {}; }
			case token_type::star: { script_int rr; if (!ints::try_mul(a, b, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '*'")); emit_int(rr); return {}; }
			case token_type::slash:
				if (b == 0) return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
				{ script_int rr; if (!ints::try_div(a, b, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '/'")); emit_int(rr); return {}; }
			case token_type::percent:
				if (b == 0) return raise_(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
				emit_int(ints::mod(a, b)); return {};
			case token_type::less: sink(script_value(a < b, engine_)); return {};
			case token_type::less_equal: sink(script_value(a <= b, engine_)); return {};
			case token_type::greater: sink(script_value(a > b, engine_)); return {};
			case token_type::greater_equal: sink(script_value(a >= b, engine_)); return {};
			case token_type::equal_equal: sink(script_value(a == b, engine_)); return {};
			case token_type::bang_equal: sink(script_value(a != b, engine_)); return {};
			default: break;
			}
		} else if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
		           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
			const script_float a = li == script_value::TYPEID_INT ? static_cast<script_float>(lp->unchecked_as_int()) : lp->unchecked_as_float();
			const script_float b = ri == script_value::TYPEID_INT ? static_cast<script_float>(rp->unchecked_as_int()) : rp->unchecked_as_float();
			switch (op) {
			case token_type::plus: emit_float(a + b); return {};
			case token_type::minus: emit_float(a - b); return {};
			case token_type::star: emit_float(a * b); return {};
			case token_type::slash:
				if (b == 0.0) return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
				emit_float(a / b); return {};
			case token_type::percent:
				if (b == 0.0) return raise_(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
				emit_float(std::fmod(a, b)); return {};
			case token_type::less: sink(script_value(a < b, engine_)); return {};
			case token_type::less_equal: sink(script_value(a <= b, engine_)); return {};
			case token_type::greater: sink(script_value(a > b, engine_)); return {};
			case token_type::greater_equal: sink(script_value(a >= b, engine_)); return {};
			case token_type::equal_equal: sink(script_value(a == b, engine_)); return {};
			case token_type::bang_equal: sink(script_value(a != b, engine_)); return {};
			default: break;
			}
		} else if (op == token_type::plus && li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING &&
		           !lp->is_cpp_bound() && !rp->is_cpp_bound()) {
			// unchecked_as_string does NOT decode cpp_bound (unlike the int/float reads)
			// engine::memory_cap chokepoint: deny the concat result before it exists
			if (!limits_->memory_charge(sizeof(script_value) + lp->unchecked_as_string().size() + rp->unchecked_as_string().size())) [[unlikely]] {
				return raise_from(detail::raise_memory_cap(*limits_));
			}
			sink(script_value(lp->unchecked_as_string() + rp->unchecked_as_string(), engine_));
			return {};
		} else if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) {
			// twin parity: bound operands keep this fused shape's zero-divisor error surface
			if (auto z = bound_fastpath_zero_divisor(op, *lp, *rp, true, true)) {
				return raise_(make_error_code(z->code), z->text);
			}
		}
	}

	// Fallback: snapshot ident/const operands (isolation from mutation by nested calls;
	// constant templates get their engine ref, exactly as op_const would have).
	// Subscript-resolved operands pass through IN PLACE: their unfused twin pushed a
	// reference (no element copy), so a snapshot here would be an observable extra
	// copy inside custom operators (use_count parity). binary_general copies into args
	// before any user code runs, so the in-place pointer never outlives its validity.
	std::optional<script_value> left_snap, right_snap;
	const script_value* lv = lp;
	const script_value* rv = rp;
	if (!p.left.is_subscript()) {
		left_snap.emplace(*lp);
		if (!left_snap->has_valid_engine()) left_snap->set_engine(engine_);
		lv = &*left_snap;
	}
	if (!p.right.is_subscript()) {
		right_snap.emplace(*rp);
		if (!right_snap->has_valid_engine()) right_snap->set_engine(engine_);
		rv = &*right_snap;
	}
	auto result = binary_general(op, lv->deref(), rv->deref());
	if (!result) {
		return raise_from(result);
	}
	sink(std::move(result.value()));
	return {};
}

op_status vm_backend::exec_binary_fused_decl(frame& f, const vm_instruction& ins) {
	const fused_binary_dst_proto& dp = f.code->fused_binary_dst_protos[ins.a];
	auto* decl = static_cast<variable_decl*>(f.code->nodes[dp.node_index].get());
	// Raw-capable sink: scalar kernel results land without a value mint — in place
	// when the live slot already holds a same-tagged scalar, else the exact mint +
	// tag + frame_slot_set the boxed fast path below performs. Non-raw results
	// (bools, strings, general-tail values) box and take the original paths.
	struct bdecl_lane_sink {
		vm_backend* vm;
		frame& fr;
		variable_decl* decl;
		bool fast_ok;
		bool landed = false;
		std::optional<script_value> boxed;
		void operator()(script_value&& v) { boxed.emplace(std::move(v)); }
		void raw_int(script_int iv) {
			const script_value_type bt = fast_ok && decl->type ? decl->type->base_type
			                                                   : script_value_type::jai_any_type;
			if (fast_ok && (bt == script_value_type::jai_any_type || bt == script_value_type::jai_int_type)) {
				if (script_value* s = vm->frame_slot(fr, decl->slot_index);
				    s && s->raw_storage_index() == script_value::TYPEID_INT &&
				    s->get_type_info().get() == decl->type.get()) {
					s->unchecked_set_int_payload(iv);
					landed = true;
					return;
				}
				script_value v(iv, vm->engine_);
				if (decl->type) { v.set_type_info(decl->type); }
				vm->frame_slot_set(fr, decl->slot_index, std::move(v));
				landed = true;
				return;
			}
			if (fast_ok && bt == script_value_type::jai_float_type) {
				raw_float(static_cast<script_float>(iv));   // the decl's int -> float row, widened here
				return;
			}
			boxed.emplace(script_value(iv, vm->engine_));
		}
		void raw_float(script_float fv) {
			const script_value_type bt = fast_ok && decl->type ? decl->type->base_type
			                                                   : script_value_type::jai_any_type;
			if (fast_ok && (bt == script_value_type::jai_any_type || bt == script_value_type::jai_float_type)) {
				if (script_value* s = vm->frame_slot(fr, decl->slot_index);
				    s && s->raw_storage_index() == script_value::TYPEID_FLOAT &&
				    s->get_type_info().get() == decl->type.get()) {
					s->unchecked_set_float_payload(fv);
					landed = true;
					return;
				}
				script_value v(fv, vm->engine_);
				if (decl->type) { v.set_type_info(decl->type); }
				vm->frame_slot_set(fr, decl->slot_index, std::move(v));
				landed = true;
				return;
			}
			boxed.emplace(script_value(fv, vm->engine_));
		}
	} lane{this, f, decl,
	       (decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store &&
	       !decl->ref_escaping && decl->slot_index != SIZE_MAX && f.locals && !f.top_level};
	VM_TRY(binary_fused_compute(f, dp.binary_proto, lane));
	if (lane.landed) {
		return {};
	}
	std::optional<script_value>& computed = lane.boxed;
	if (!computed) [[unlikely]] {
		return {};   // compute unwound before its sink ran (pair parity: the decl never runs)
	}
	// Scalar and string slot decls land directly - the exec_decl_var fast-path
	// preconditions, KEPT IN SYNC with it. A fused-binary result is never a reference,
	// so the deref leg is structurally absent here.
	if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store &&
	    !decl->ref_escaping && decl->slot_index != SIZE_MAX && f.locals && !f.top_level) {
		if (variable_decl::identity_payload(computed->raw_storage_index(), decl->type.get())) {
			if (decl->type) {
				computed->set_type_info(decl->type);
			}
			frame_slot_set(f, decl->slot_index, std::move(*computed));
			return {};
		}
		if (variable_decl::widening_payload(computed->raw_storage_index(), decl->type.get())) {
			script_value widened(static_cast<script_float>(computed->unchecked_as_int()), engine_);
			widened.set_type_info(decl->type);
			frame_slot_set(f, decl->slot_index, std::move(widened));
			return {};
		}
	}
	// Every other decl shape: push and run op_decl_var verbatim (semantics by
	// construction; a fused-binary result is a temporary, so lvalue_init = 0)
	stack_.push_back(std::move(*computed));
	const vm_instruction decl_ins{opcode::op_decl_var, dp.node_index, 1, 0};
	return exec_decl_var(f, decl_ins);
}

op_status vm_backend::exec_binary_fused_store(frame& f, const vm_instruction& ins) {
	const fused_binary_dst_proto& dp = f.code->fused_binary_dst_protos[ins.a];
	std::optional<script_value> computed;
	VM_TRY(binary_fused_compute(f, dp.binary_proto, [&](script_value&& v) { computed.emplace(std::move(v)); }));
	if (!computed) [[unlikely]] {
		return {};   // compute unwound before its sink ran (pair parity: STORE never runs)
	}
	const vm_instruction store_ins{opcode::op_store, dp.symbol, dp.slot, dp.flags};
	VM_TRY(store_popped_value(f, store_ins, std::move(*computed)));
	// Statement position (the no-result door's rewrite): op_store's dispatch case pops
	// the result under this flag — mirror it here, the tail always pushes
	if (dp.flags & store_flag_no_result) {
		stack_.pop_back();
	}
	return {};
}

// Cheap-shape operand resolver for exec_fused_cmp_jump: constants, frame slots and
// (top-level) cached env locals only. nullptr = bail to the verbatim pair semantics.
// Kept OFF fused_ident_value so exec_binary_fused's inlining of it is undisturbed.
const script_value* vm_backend::fused_cmp_operand(frame& f, const fused_operand& operand, size_t cache_slot) {
	if (operand.const_index != k_invalid_u32) {
		return &f.code->constants[operand.const_index];
	}
	if (operand.is_subscript()) {
		return nullptr;   // element reads bail to the pair (exec_binary_fused resolves them)
	}
	if (operand.load_flags & (load_flag_type_ctor | load_flag_global_only)) {
		return nullptr;
	}
	if (operand.slot != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* local = frame_slot(f, operand.slot)) {
			return &local->deref();
		}
		return nullptr;
	}
	// Member scope: fields/statics outrank env cells past the function's own scopes;
	// a member-METHOD shadow answers nullptr and the caller bails to the pair.
	if (f.locals && !f.top_level) {
		ensure_frame_member_scope(f, environment_.get());
		if (frame_scope_active(f)) {
			return member_scope_env_lookup(f, f.code->symbols[operand.symbol], cache_slot,
			                               ident_node_of(*f.code, operand.ident_node));
		}
	}
	return env_lookup_cached(f, cache_slot, f.code->symbols[operand.symbol]);
}

// BINARY_FUSED(comparison)+JUMP_IF_FALSE superinstruction. Contract: on success ip is
// retargeted (fall-through = ip+1, jump = ins.a); on error or script unwinding ip is
// left on this instruction (and the unwinding case leaves the result pushed, exactly
// like the unfused pair did before its jump executed).
op_status vm_backend::exec_fused_cmp_jump(frame& f, const vm_instruction& ins) {
	const fused_binary_proto& p = f.code->fused_binary_protos[ins.b];
#ifdef JAISCRIPT_VM_PROFILE
	if (p.typed_class == 0) { ++profile_typed_head_[5]; }
#endif
	if (p.typed_class != 0 && !has_custom_numeric_ops_ && typed_cmp_jump_head(f, ins, p)) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_typed_head_[3];
#endif
		return {};
	}
#ifdef JAISCRIPT_VM_PROFILE
	if (p.typed_class != 0) { ++profile_typed_head_[4]; }
#endif
	const token_type op = static_cast<token_type>(p.op);

	// Numeric fast path: mirrors exec_binary_fused's comparison rows without the bool
	// round-trip through the value stack. Any non-trivial shape bails to the pair.
	if (!has_custom_numeric_ops_) {
#ifdef JAISCRIPT_VM_PROFILE
		const uint64_t prof_c0 = __rdtsc();
#endif
		const script_value* lp = fused_cmp_operand(f, p.left, f.ip * 3);
		const script_value* rp = lp ? fused_cmp_operand(f, p.right, f.ip * 3 + 1) : nullptr;
#ifdef JAISCRIPT_VM_PROFILE
		profile_cmp_operands_cyc_ += __rdtsc() - prof_c0; ++profile_cmp_count_;
		if (lp && rp) { const size_t lk = lp->raw_storage_index(), rk = rp->raw_storage_index(); if (lk == script_value::TYPEID_SHARED_PTR || rk == script_value::TYPEID_SHARED_PTR) ++profile_cmp_sptr_null_; }
#endif
		if (rp) {
			const size_t li = lp->raw_storage_index();
			const size_t ri = rp->raw_storage_index();
			if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
				bool truthy;
				switch (op) {
				case token_type::less: truthy = a < b; break;
				case token_type::less_equal: truthy = a <= b; break;
				case token_type::greater: truthy = a > b; break;
				case token_type::greater_equal: truthy = a >= b; break;
				case token_type::equal_equal: truthy = a == b; break;
				default: truthy = a != b; break;   // compiler fuses exactly the six comparisons
				}
				f.ip = truthy ? f.ip + 1 : ins.a;
				return {};
			}
			if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				const script_float a = li == script_value::TYPEID_INT ? static_cast<script_float>(lp->unchecked_as_int()) : lp->unchecked_as_float();
				const script_float b = ri == script_value::TYPEID_INT ? static_cast<script_float>(rp->unchecked_as_int()) : rp->unchecked_as_float();
				bool truthy;
				switch (op) {
				case token_type::less: truthy = a < b; break;
				case token_type::less_equal: truthy = a <= b; break;
				case token_type::greater: truthy = a > b; break;
				case token_type::greater_equal: truthy = a >= b; break;
				case token_type::equal_equal: truthy = a == b; break;
				default: truthy = a != b; break;
				}
				f.ip = truthy ? f.ip + 1 : ins.a;
				return {};
			}
			// Null-compare lane (`x == null` / `x != null`, the BST/gameplay guard shape):
			// handle_equal answers a null operand from storage alone - null equals null and
			// nothing else does (a transparent wrapper unwraps to a NON-null value or stays
			// itself, so an object side never reads as null; class operator== is consulted
			// only after that verdict). Kept on the general path: weak_ptr (expiry answer),
			// cpp-bound (null-semantic pointers) and raw-invalid. The custom-binary-op latch
			// guards the global `==` overload consult that precedes handle_equal.
			if ((op == token_type::equal_equal || op == token_type::bang_equal) && !has_custom_binary_ops_ &&
			    (li == script_value::TYPEID_NULL || ri == script_value::TYPEID_NULL)) {
				int verdict = -1;
				switch (li == script_value::TYPEID_NULL ? ri : li) {
				case script_value::TYPEID_NULL: verdict = 1; break;
				case script_value::TYPEID_INT: case script_value::TYPEID_FLOAT: case script_value::TYPEID_STRING:
				case script_value::TYPEID_BOOL: case script_value::TYPEID_CHAR: case script_value::TYPEID_ARRAY:
				case script_value::TYPEID_MAP: case script_value::TYPEID_FUNCTION: case script_value::TYPEID_OBJECT:
					verdict = 0; break;
				default: break;
				}
				if (verdict >= 0) {
					const bool truthy = (op == token_type::equal_equal) == (verdict == 1);
					f.ip = truthy ? f.ip + 1 : ins.a;
					return {};
				}
			}
		}
	}

	// Verbatim pair semantics: the original fused compare pushes its result, then
	// JUMP_IF_FALSE's pop/branch runs (parity by construction)
	vm_instruction fused = ins;
	fused.a = ins.b;   // exec_binary_fused reads the proto index from a
	auto result = exec_binary_fused(f, fused);
	if (result == op_status::failed) {
		return result;
	}
	if (is_unwinding_) [[unlikely]] {
		// Pair parity: result stays pushed; the dispatch case handles the unwind
		// before any jump runs
		return {};
	}
	script_value cond = std::move(stack_.back());
	stack_.pop_back();
	const bool truthy = ins.c ? cond.unchecked_as_bool() : is_truthy(cond);
	f.ip = truthy ? f.ip + 1 : ins.a;
	return {};
}

// RHS+COMPOUND_STORE superinstruction (`target op= <fused rhs | bare identifier/const>`).
// Fast path: plain int target (slot or cached top-level env local) with all-int
// cheap-shape rhs, arithmetic kinds only — error surfaces copied verbatim from the pair.
// Anything else runs the pair's two exec bodies back to back (parity by construction).
// Never touches f.ip: the dispatch case breaks, so the loop-bottom unwind check and
// ++ip run as usual.
op_status vm_backend::exec_compound_fused(frame& f, const vm_instruction& ins) {
	const compound_fused_proto& cp = f.code->compound_fused_protos[ins.a];
	const bool bare_rhs = cp.rhs_proto == k_invalid_u32;

	if (!has_custom_numeric_ops_) {
		script_value* varPtr = nullptr;
		if (cp.slot != k_invalid_u32 && f.locals && !f.top_level) {
			varPtr = frame_slot(f, cp.slot);
		}
		if (!varPtr) {
			// Member scope: a shadowed field/static IS the compound target (in-place,
			// the same pointer the interpreter's funnel mutates); a member-method
			// shadow answers nullptr and the slow pair below raises correctly.
			if (f.locals && !f.top_level) {
				ensure_frame_member_scope(f, environment_.get());
				varPtr = frame_scope_active(f)
					? member_scope_env_lookup(f, f.code->symbols[cp.symbol], f.ip * 3 + 2)
					: env_lookup_cached(f, f.ip * 3 + 2, f.code->symbols[cp.symbol]);
			} else {
				varPtr = env_lookup_cached(f, f.ip * 3 + 2, f.code->symbols[cp.symbol]);
			}
		}
		if (varPtr && varPtr->raw_storage_index() == script_value::TYPEID_INT) {
			script_int rhs = 0;
			bool arithmetic = false;
			if (bare_rhs) {
				// A bare load can't raise: any non-int/uncached shape just bails to the pair
				const script_value* vp = fused_cmp_operand(f, cp.rhs, f.ip * 3);
				if (vp && vp->raw_storage_index() == script_value::TYPEID_INT) {
					rhs = vp->unchecked_as_int();
					arithmetic = true;
				}
			} else {
				const fused_binary_proto& p = f.code->fused_binary_protos[cp.rhs_proto];
				const script_value* lp = fused_cmp_operand(f, p.left, f.ip * 3);
				const script_value* rp = lp ? fused_cmp_operand(f, p.right, f.ip * 3 + 1) : nullptr;
				if (rp && lp->raw_storage_index() == script_value::TYPEID_INT &&
				    rp->raw_storage_index() == script_value::TYPEID_INT) {
					const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
					arithmetic = true;
					switch (static_cast<token_type>(p.op)) {
					case token_type::plus:
						if (!ints::try_add(a, b, rhs)) return raise_from(vm_int_overflow_v("Integer overflow in '+'"));
						break;
					case token_type::minus:
						if (!ints::try_sub(a, b, rhs)) return raise_from(vm_int_overflow_v("Integer overflow in '-'"));
						break;
					case token_type::star:
						if (!ints::try_mul(a, b, rhs)) return raise_from(vm_int_overflow_v("Integer overflow in '*'"));
						break;
					case token_type::slash:
						if (b == 0) return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
						if (!ints::try_div(a, b, rhs)) return raise_from(vm_int_overflow_v("Integer overflow in '/'"));
						break;
					case token_type::percent:
						if (b == 0) return raise_(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
						rhs = ints::mod(a, b);
						break;
					default:
						arithmetic = false;   // comparisons etc: pair semantics below
						break;
					}
				}
			}
			if (arithmetic) {
				const uint32_t kind = cp.kind_flags & compound_kind_mask;
				script_int& tref = varPtr->unchecked_as_int_ref();
				script_int rr = 0;
				bool stored = true;
				switch (kind) {
				case compound_plus:
					if (!ints::try_add(tref, rhs, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '+='"));
					break;
				case compound_minus:
					if (!ints::try_sub(tref, rhs, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '-='"));
					break;
				case compound_star:
					if (!ints::try_mul(tref, rhs, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '*='"));
					break;
				case compound_slash:
					if (rhs == 0) return raise_(make_error_code(runtime_error_code::division_by_zero));
					if (!ints::try_div(tref, rhs, rr)) return raise_from(vm_int_overflow_v("Integer overflow in '/='"));
					break;
				default:
					stored = false;   // %=: the pair surfaces unknown_operator
					break;
				}
				if (stored) {
					tref = rr;
					if (!(cp.kind_flags & compound_flag_no_result)) {
						stack_.push_back((cp.kind_flags & compound_flag_result_needed) ? varPtr->clone() : *varPtr);
					}
					return {};
				}
			}
		}
	}

	// Verbatim pair: the rhs (fused binary / load / const) pushes its result, then the
	// compound store pops it
	if (bare_rhs) {
		if (cp.rhs.const_index != k_invalid_u32) {
			// KEEP BYTE-PARALLEL with run_dispatch's op_const case: engine-less parse-time
			// template re-materialized with the engine
			const script_value& tmpl = f.code->constants[cp.rhs.const_index];
			const auto& storage = tmpl.get_storage();
			switch (storage.index()) {
				case script_value::TYPEID_INT: stack_.push_back(script_value(storage.get<script_int>(), engine_)); break;
				case script_value::TYPEID_FLOAT: stack_.push_back(script_value(storage.get<script_float>(), engine_)); break;
				case script_value::TYPEID_STRING: stack_.push_back(literal_string_value(*f.code, cp.rhs.const_index, storage.get<strong_ptr<script_string>>())); break;
				case script_value::TYPEID_CHAR: stack_.push_back(script_value(storage.get<script_char>(), engine_)); break;
				case script_value::TYPEID_BOOL: stack_.push_back(script_value(storage.get<script_bool>(), engine_)); break;
				case script_value::TYPEID_NULL: stack_.push_back(make_null()); break;
				default: {
					script_value copy = tmpl;
					copy.set_engine(engine_);
					stack_.push_back(std::move(copy));
					break;
				}
			}
		} else {
			vm_instruction load_ins = ins;
			load_ins.a = cp.rhs.slot;
			load_ins.b = cp.rhs.symbol;
			load_ins.c = cp.rhs.load_flags;
			auto load_result = exec_load(f, load_ins);
			if (load_result == op_status::failed) {
				return load_result;
			}
		}
	} else {
		vm_instruction rhs_ins = ins;
		rhs_ins.a = cp.rhs_proto;
		auto rhs_result = exec_binary_fused(f, rhs_ins);
		if (rhs_result == op_status::failed) {
			return rhs_result;
		}
		if (is_unwinding_) [[unlikely]] {
			// Pair parity: the store never ran; result stays pushed for the loop-bottom
			// unwind handling (the dispatch case breaks into it)
			return {};
		}
	}
	vm_instruction store_ins = ins;
	store_ins.a = cp.symbol;
	store_ins.b = cp.slot;
	store_ins.c = cp.kind_flags;
	return exec_compound_store(f, store_ins);
}

namespace {
	inline bool cfor_compare(uint8_t cmp, script_int a, script_int b) {
		switch (static_cast<token_type>(cmp)) {
			case token_type::less: return a < b;
			case token_type::less_equal: return a <= b;
			case token_type::greater: return a > b;
			case token_type::greater_equal: return a >= b;
			case token_type::equal_equal: return a == b;
			case token_type::bang_equal: return a != b;
			default: return false;
		}
	}
}

bool vm_backend::resolve_cfor_int_operand(frame& f, const fused_operand& operand, script_int*& ptr, script_int& val) {
	if (operand.const_index != k_invalid_u32) {
		const script_value& tmpl = f.code->constants[operand.const_index];
		if (tmpl.raw_storage_index() != script_value::TYPEID_INT) return false;
		val = tmpl.unchecked_as_int();
		return true;
	}
	script_value* resolved = resolve_local_or_env(f, operand.slot, f.code->symbols[operand.symbol]);
	if (!resolved) return false;
	script_value& target = resolved->deref();
	if (target.raw_storage_index() != script_value::TYPEID_INT || target.is_cpp_bound()) return false;
	ptr = &target.unchecked_as_int_ref();
	return true;
}

op_status vm_backend::exec_cfor_prep(frame& f, const vm_instruction& ins) {
	const counted_for_proto& p = f.code->counted_for_protos[ins.a];
	counted_for_state st;
	st.cmp = p.cmp;
	st.subtract = p.step_subtract;
	st.incdec = p.step_incdec;

	bool fast = !has_custom_numeric_ops_;
	if (fast) {
		script_value* varPtr = resolve_local_or_env(f, p.var.slot, f.code->symbols[p.var.symbol]);
		if (varPtr) {
			script_value& target = varPtr->deref();
			if (target.raw_storage_index() == script_value::TYPEID_INT && !target.is_cpp_bound()) {
				st.var = &target;
			} else {
				fast = false;
			}
		} else {
			fast = false;
		}
	}
	if (fast) fast = resolve_cfor_int_operand(f, p.end, st.end_ptr, st.end_val);
	if (fast) fast = resolve_cfor_int_operand(f, p.step, st.step_ptr, st.step_val);
	st.fast = fast;

	if (!fast) {
		cfor_states_.push_back(st);
		f.ip = p.generic_cond_ip;
		return {};
	}

	const script_int i = st.var->unchecked_as_int();
	const script_int end = st.end_ptr ? *st.end_ptr : st.end_val;
	if (cfor_compare(st.cmp, i, end)) {
		cfor_states_.push_back(st);
		f.ip = p.body_ip;
	} else {
		f.ip = p.exit_ip;
	}
	return {};
}

op_status vm_backend::exec_cfor_back(frame& f, const vm_instruction& ins) {
	const counted_for_proto& p = f.code->counted_for_protos[ins.a];
	if (cfor_states_.empty()) {
		return raise_(make_error_code(runtime_error_code::internal_error), "counted-for state stack underflow");
	}
	counted_for_state& st = cfor_states_.back();

	if (execution_limit_exhausted()) [[unlikely]] {
		return raise_from(execution_limit_failure());
	}
	if (!st.fast) {
		f.ip = p.generic_update_ip;
		return {};
	}
	// Honor body writes to the loop variable; a type change demotes to the generic path
	if (st.var->raw_storage_index() != script_value::TYPEID_INT) [[unlikely]] {
		st.fast = false;
		f.ip = p.generic_update_ip;
		return {};
	}
	const script_int i = st.var->unchecked_as_int();
	const script_int step = st.step_ptr ? *st.step_ptr : st.step_val;
	script_int next;
	if (st.subtract) {
		if (!ints::try_sub(i, step, next)) return raise_from(vm_int_overflow_v(st.incdec ? "Integer overflow in '--'" : "Integer overflow in '-='"));
	} else {
		if (!ints::try_add(i, step, next)) return raise_from(vm_int_overflow_v(st.incdec ? "Integer overflow in '++'" : "Integer overflow in '+='"));
	}
	st.var->unchecked_as_int_ref() = next;
	const script_int end = st.end_ptr ? *st.end_ptr : st.end_val;
	if (cfor_compare(st.cmp, next, end)) {
		f.ip = p.body_ip;
	} else {
		cfor_states_.pop_back();
		f.ip = p.exit_ip;
	}
	return {};
}

op_status vm_backend::exec_index(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	const size_t depth_before = stack_.size();   // INDEX pops two and pushes one: a result means size == before - 1
	const op_status st = exec_index_impl(f, ins);
	if (stack_.size() + 1 == depth_before) { const size_t k = stack_.back().deref().raw_storage_index(); ++profile_index_kinds_[k < 16 ? k : 15]; }
	return st;
#else
	return exec_index_impl(f, ins);
#endif
}

op_status vm_backend::exec_index_impl(frame& f, const vm_instruction& ins) {
	const bool lvalue_shape = (ins.a & index_flag_lvalue_shape) != 0;
	const bool lvalue_write = (ins.a & index_flag_lvalue_write) != 0;
	// Transient read (transient_read.hpp): the consumer provably discards identity, so
	// skip the reference mint and push a shallow element copy. Overrides re-enable the
	// mint at runtime - operand copies would be observable via use_count inside them.
	const bool transient_read = (ins.a & index_flag_transient_read) != 0 && !lvalue_write && !has_custom_binary_ops_;

	script_value right = std::move(stack_.back());
	stack_.pop_back();
	right = right.deref();
	script_value left_raw = std::move(stack_.back());
	stack_.pop_back();
	script_value& left = left_raw.deref();

	if (left.raw_storage_index() == script_value::TYPEID_PARALLEL_BORROW) {
		// Parallel captured-read borrow: element reads go through the shared kernel
		// (detail/parallel_transform.hpp - parity with the interpreter twin by
		// construction); writes hit the runtime write wall inside it. No element
		// reference is ever minted into a borrowed container. A string cell comes from
		// the worker's own copy of it (same bytes the kernel would copy).
		if (!lvalue_write) {
			if (const script_string* source = detail::parallel_borrow_string_source(left, right)) {
				stack_.push_back(worker_borrowed_string(left, right, *source));
				return {};
			}
		}
		auto elem = detail::parallel_borrow_subscript_read(left, right, engine_, lvalue_write);
		if (!elem) { return raise_(elem.error(), elem.static_message()); }
		stack_.push_back(std::move(elem).value());
		return {};
	}

	if (left.is_array()) {
		if (!right.is_int()) {
			return raise_(make_error_code(runtime_error_code::invalid_index_type), "Array index must be an integer");
		}
		script_int index = right.unchecked_as_int();
		const script_array* node = left.unchecked_array_node();

		if (index < 0 || index >= static_cast<script_int>(node->size())) {
			// Numbers intern as symbols: the {0}/{1} machinery resolves symbol ids,
			// so raw counts printed garbage names (KEEP BYTE-PARALLEL with the
			// interpreter subscript path)
			return raise_(make_error_code(runtime_error_code::index_out_of_bounds),
				"Array index {0} out of bounds for array of size {1}",
				symbolizer_->intern(std::to_string(index)),
				symbolizer_->intern(std::to_string(node->size())));
		}

		if (lvalue_shape && !transient_read) {
			auto array_type_info = left.get_type_info();
			type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
			script_value ref_value = script_value::make_element_reference(
				left.get_array_storage(), static_cast<size_t>(index), engine_, element_type);
			stack_.push_back(std::move(ref_value));
		} else if (node->is_typed()) {
			stack_.push_back(node->get(static_cast<size_t>(index), engine_));   // raw buffer read
		} else {
			stack_.push_back(node->values()[index]);
		}
		return {};
	}

	if (left.is_map()) {
		try {
			auto& map = const_cast<script_map&>(left.as_map());

			if (lvalue_shape && lvalue_write) {
				// Auto-insert so the assignment has somewhere to write through
				script_value key = right;
				if (!key.has_valid_engine()) {
					key.set_engine(left.has_valid_engine() ? left.get_engine() : engine_);
				}
				// Declared key type: keys never coerce, checked BEFORE the auto-insert
				if (auto map_tag = left.get_type_info()) {
					if (type_info_ptr key_type = map_tag->key_type();
					    key_type && key_type->base_type != script_value_type::jai_any_type &&
					    key.deref().type() != key_type->base_type) {
						uint64_t got_id = symbolizer_->intern(vm_value_type_name(key.deref()));
						uint64_t want_id = symbolizer_->intern(vm_type_info_name(key_type));
						return raise_(make_error_code(runtime_error_code::map_key_type_mismatch),
							"Cannot use '{0}' as a key of type '{1}'", got_id, want_id);
					}
				}
				const size_t prior_map_size = map.size();
				script_value& value_ref = map[key];
				// engine::memory_cap: count NEW-entry growth (raised at the next back-edge)
				if (map.size() != prior_map_size) {
					limits_->memory_charge_deferred(2 * sizeof(script_value) + (key.is_string() ? key.unchecked_as_string().size() : 0));
				}
				if (!value_ref.has_valid_engine()) {
					if (!left.has_valid_engine()) {
						return raise_(make_error_code(runtime_error_code::unsupported_operation),
							"Invalid script_value: both map and new entry missing engine reference");
					}
					value_ref.set_engine(left.get_engine());
				}
				auto map_type_info = left.get_type_info();
				type_info_ptr value_type = map_type_info ? map_type_info->value_type() : nullptr;
				// Map-entry mode: pins the map + re-resolves by key (temporaries/erase safe)
				script_value ref_value = script_value::make_map_entry_reference(left.get_map_storage(), key, engine_, value_type);
				stack_.push_back(std::move(ref_value));
			} else if (lvalue_shape && !transient_read) {
				// Read: reference the existing entry, never insert
				auto it = map.find(right);
				if (it != map.end()) {
					script_value& value_ref = const_cast<script_value&>(it->second);
					if (!value_ref.has_valid_engine()) {
						value_ref.set_engine(left.has_valid_engine() ? left.get_engine() : engine_);
					}
					auto map_type_info = left.get_type_info();
					type_info_ptr value_type = map_type_info ? map_type_info->value_type() : nullptr;
					stack_.push_back(script_value::make_map_entry_reference(left.get_map_storage(), right, engine_, value_type));
				} else {
					stack_.push_back(make_null());
				}
			} else {
				auto it = map.find(right);
				if (it != map.end()) {
					script_value val = it->second;
					if (!val.has_valid_engine()) {
						val.set_engine(engine_);
					}
					stack_.push_back(std::move(val));
				} else {
					stack_.push_back(make_null());
				}
			}
		} catch (...) {
			const auto& map = left.as_map();
			auto it = map.find(right);
			if (it != map.end()) {
				script_value val = it->second;
				if (!val.has_valid_engine()) {
					val.set_engine(engine_);
				}
				stack_.push_back(std::move(val));
			} else {
				stack_.push_back(make_null());
			}
		}
		return {};
	}

	if (left.is_string()) {
		// Read-only char subscript (C++-familiar s[i]). Writes stay an error:
		// strings share storage under copy, so subscript writes would need
		// copy-on-write - future work, a clear error instead of a silent trap.
		// (KEEP BYTE-PARALLEL with the interpreter subscript path)
		if (!right.is_int()) {
			return raise_(make_error_code(runtime_error_code::invalid_index_type), "String index must be an integer");
		}
		if (lvalue_shape && lvalue_write) {
			return raise_(make_error_code(runtime_error_code::unsupported_operation),
				"Strings are read-only through subscript: use substr()/+ to build a new string");
		}
		const auto& str = left.unchecked_as_string();
		script_int index = right.unchecked_as_int();
		if (index < 0 || index >= static_cast<script_int>(str.size())) {
			return raise_(make_error_code(runtime_error_code::index_out_of_bounds),
				"String index {0} out of bounds for string of size {1}",
				symbolizer_->intern(std::to_string(index)),
				symbolizer_->intern(std::to_string(str.size())));
		}
		stack_.push_back(script_value(static_cast<script_char>(str[static_cast<size_t>(index)]), engine_));
		return {};
	}

	if (left.is_object()) {
		// Safe-mode workers: operator[]/global-operator dispatch copies shared method
		// values and escapes to the engine backend — verdict (increment B)
		if (parallel_worker_) [[unlikely]] {
			return raise_(make_error_code(runtime_error_code::unsupported_operation),
				"custom operator dispatch on class instances is not admitted in a parallel body");
		}
		auto instance_result = left.checked_as<std::shared_ptr<class_instance>>();
		if (instance_result) {
			auto instance = instance_result.value();
			script_value method = instance->get_method(subscript_op_id_, false);
			if (method.is_function()) {
				const script_function& func = method.as_function();
				std::vector<script_value> args = {left, right};
				auto result = func(args);
				if (!result) {
					return raise_from(result);
				}
				stack_.push_back(std::move(result.value()));
				return {};
			}
		}
		// Fall back to the global [] operator (flat table - no env probe)
		if (operator_table_) {
			if (const script_value* getMethod = operator_table_->entry(detail::op_slot::subscript)) {
				const script_function& func = getMethod->as_function();
				std::vector<script_value> args = {left, right};
				auto result = func(args);
				if (!result) {
					return raise_from(result);
				}
				stack_.push_back(std::move(result.value()));
				return {};
			}
		}
	}
	return raise_(make_error_code(runtime_error_code::unsupported_operation),
		"Subscript can only be used on arrays, maps, or types with [] operator");
}

op_status vm_backend::exec_index_assign(frame& f, const vm_instruction& ins) {
	script_value target_ref = std::move(stack_.back());
	stack_.pop_back();
	script_value value = std::move(stack_.back());
	stack_.pop_back();

	if (!target_ref.is_reference()) {
		return raise_(make_error_code(runtime_error_code::invalid_assignment_target), "Cannot assign to rvalue expression");
	}

	// Mode-based re-resolution (never a cached address): realloc/erase between mint
	// and write can't corrupt the heap. TYPED elements have no script_value to point
	// at: target_ptr aims at the holder scratch so the enforcement flow below stays
	// byte-identical, and the result commits into the raw buffer at the end.
	auto refHolder = target_ref.get_reference_holder();
	const bool typed_element = refHolder && refHolder->typed_element();
	script_value* target_ptr;
	if (typed_element) {
		if (refHolder->container_index >= refHolder->container->size()) {
			return raise_(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
		}
		target_ptr = const_cast<script_value*>(&refHolder->materialize_typed_element(engine_));
	} else {
		target_ptr = refHolder->resolve_target();
		if (!target_ptr) {
			return raise_(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
		}
	}

	type_info_ptr element_type = refHolder->container_element_type;
	if (element_type) {
		if (!vm_is_element_type_compatible(value, element_type, *target_ptr)) {
			std::string value_type = vm_value_type_name(value);
			std::string expected_type = vm_type_info_name(element_type);
			uint64_t value_type_id = symbolizer_->intern(value_type);
			uint64_t expected_type_id = symbolizer_->intern(expected_type);
			return raise_(
				make_error_code(runtime_error_code::array_element_type_mismatch),
				"Cannot assign '{0}' to element of type '{1}'",
				value_type_id, expected_type_id);
		}
		auto converted_result = vm_convert_array_element(engine_, value, element_type);
		if (!converted_result) { return raise_from(converted_result); }
		script_value converted = std::move(converted_result.value());
		*target_ptr = std::move(converted);
	} else {
		// No element type constraint - values deep-copy, shared_ptr handles share
		*target_ptr = clone_for_assignment(value);
	}
	if (typed_element) {
		refHolder->container->set(refHolder->container_index, *target_ptr);
	}
	stack_.push_back(assignment_result_used(f) ? clone_for_assignment(*target_ptr) : std::move(value));   // a copy of the stored element is the expression's value
	return {};
}

op_status vm_backend::exec_index_compound(frame& f, const vm_instruction& ins) {
	script_value rightValue = std::move(stack_.back());
	stack_.pop_back();
	script_value currentValue = std::move(stack_.back());
	stack_.pop_back();
	const bool old_result = (ins.a & compound_flag_old_result) != 0;
	script_value postfix_old = old_result ? script_value(currentValue.deref()) : make_null();
	if (ins.a & compound_flag_incdec) {
		// ++/-- gate: numeric targets only, the identifier path's exact error
		const size_t ci = currentValue.deref().raw_storage_index();
		if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_CPP_BOUND) {
			return raise_(make_error_code(runtime_error_code::invalid_numeric_operand));
		}
	}

	token_type op;
	switch (ins.a & compound_kind_mask) {
		case compound_plus: op = token_type::plus; break;
		case compound_minus: op = token_type::minus; break;
		case compound_star: op = token_type::star; break;
		case compound_slash: op = token_type::slash; break;
		case compound_percent: op = token_type::percent; break;
		default: op = token_type::plus; break;
	}

	script_value resultValue = make_null();
	// Custom operator consult through the flat table (was a per-store string-keyed
	// env-chain walk - the single hottest waste on the compound-store path)
	const script_value* opFunc = operator_table_ ? operator_table_->entry(detail::binary_op_slot(op)) : nullptr;
	if (opFunc) {
		const script_function& func = opFunc->as_function();
		std::vector<script_value> args = {currentValue, rightValue};
		auto result = func(args);
		if (!result) {
			return raise_from(result);
		}
		resultValue = std::move(result.value());
	} else {
		// S8 (NEW-C): a bound rhs decodes before the zero pre-checks so /= and %= keep
		// today's error codes/texts (byte-parallel with the interpreter compound path)
		if (rightValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
			rightValue = rightValue.bound_decoded_temp();
		switch (op) {
			case token_type::plus: {
				if (currentValue.is_string() || rightValue.is_string()) {
					resultValue = script_value(currentValue.to_string() + rightValue.to_string(), engine_);
				} else {
					VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue));
				}
				break;
			}
			case token_type::minus: {
				VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::minus, rightValue));
				break;
			}
			case token_type::star: {
				VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::star, rightValue));
				break;
			}
			case token_type::slash: {
				const size_t ri = rightValue.raw_storage_index();
				if ((ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) ||
				    (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0)) {
					return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
				}
				VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue));
				break;
			}
			case token_type::percent: {
				if (rightValue.raw_storage_index() == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
					return raise_(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
				}
				VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
				break;
			}
			default:
				return raise_(make_error_code(runtime_error_code::unsupported_operation),
					"Unknown compound assignment operator");
		}
	}

	if (!currentValue.is_reference()) {
		return raise_(make_error_code(runtime_error_code::invalid_assignment_target), "Cannot assign to rvalue expression");
	}
	// Mode-based re-resolution (never a cached address): realloc/erase between mint
	// and write can't corrupt the heap. TYPED elements: scratch target + buffer commit
	// (see exec_index_assign).
	auto refHolder = currentValue.get_reference_holder();
	const bool typed_element = refHolder && refHolder->typed_element();
	script_value* target_ptr;
	if (typed_element) {
		if (refHolder->container_index >= refHolder->container->size()) {
			return raise_(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
		}
		target_ptr = const_cast<script_value*>(&refHolder->materialize_typed_element(engine_));
	} else {
		target_ptr = refHolder->resolve_target();
		if (!target_ptr) {
			return raise_(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
		}
	}
	type_info_ptr element_type = refHolder->container_element_type;
	if (element_type) {
		if (!vm_is_element_type_compatible(resultValue, element_type, *target_ptr)) {
			std::string value_type = vm_value_type_name(resultValue);
			std::string expected_type = vm_type_info_name(element_type);
			uint64_t value_type_id = symbolizer_->intern(value_type);
			uint64_t expected_type_id = symbolizer_->intern(expected_type);
			return raise_(
				make_error_code(runtime_error_code::array_element_type_mismatch),
				"Cannot assign '{0}' to element of type '{1}'",
				value_type_id, expected_type_id);
		}
		auto converted_result = vm_convert_array_element(engine_, resultValue, element_type);
		if (!converted_result) { return raise_from(converted_result); }
		script_value converted = std::move(converted_result.value());
		*target_ptr = std::move(converted);
	} else {
		*target_ptr = std::move(resultValue.clone());
	}
	if (typed_element) {
		refHolder->container->set(refHolder->container_index, *target_ptr);
	}
	const bool result_needed = assignment_result_used(f);   // index compounds carry no result flag; the POP after a statement does
	stack_.push_back(old_result ? std::move(postfix_old) : result_needed ? clone_for_assignment(*target_ptr) : std::move(resultValue));
	return {};
}

// Fused a[i] = v (docs/element_read_overhead_design.md store side): the plain-array
// shape resolves container+index ONCE and writes in place - no reference_holder mint,
// no holder re-resolve. Every other shape (map/object/string/borrow/temporary base/OOB)
// replays the exact INDEX(lvalue_write)+INDEX_ASSIGN sequence, so semantics and error
// text stay byte-identical by construction.
op_status vm_backend::exec_index_store(frame& f, const vm_instruction& ins) {
	// stack: [value, container, index]. Decision peeks are read-only; committed fast
	// paths pop into locals first - conversions can run user code that grows the value
	// stack (invariant 2b: never hold references into stack_ across a possible push).
	if ((ins.a & index_flag_lvalue_shape) != 0 && stack_.size() >= 3) {
		script_value& container_peek = stack_[stack_.size() - 2].deref();
		const script_value& index_peek = stack_.back().deref();
		if (container_peek.raw_storage_index() == script_value::TYPEID_ARRAY && index_peek.is_int()) {
			const script_int index = index_peek.unchecked_as_int();
			auto storage = container_peek.get_array_storage();
			// NUMERIC raw store: an int/float buffer takes a numeric rhs with no script_value
			// element traffic; every other shape (non-numeric rhs, byte/string kinds) takes
			// the enforcing path for the exact mismatch error
			if (storage && (storage->kind() == script_array::kind_t::i64 || storage->kind() == script_array::kind_t::f64) &&
			    index >= 0 && index < static_cast<script_int>(storage->size())) {
				const script_value& rhs_peek = stack_[stack_.size() - 3].deref();
				const size_t ri = rhs_peek.raw_storage_index();
				if (ri == script_value::TYPEID_INT || (ri == script_value::TYPEID_FLOAT &&
				    (storage->kind() == script_array::kind_t::f64 || detail::float_fits_script_int(rhs_peek.unchecked_as_float())))) {
					stack_.pop_back();   // index (decoded above)
					script_value container_local = std::move(stack_.back());
					stack_.pop_back();
					script_value value = std::move(stack_.back());
					stack_.pop_back();
					storage->set(static_cast<size_t>(index), value.deref());
					stack_.push_back(assignment_result_used(f) ? storage->get(static_cast<size_t>(index), engine_) : std::move(value));   // the stored element is the expression's value
					return {};
				}
			}
			if (storage && !storage->is_typed() && index >= 0 && index < static_cast<script_int>(storage->size())) {
				auto container_type_info = container_peek.get_type_info();
				type_info_ptr element_type = container_type_info ? container_type_info->element_type() : nullptr;
				stack_.pop_back();   // index (decoded above)
				script_value container_local = std::move(stack_.back());
				stack_.pop_back();
				script_value value = std::move(stack_.back());
				stack_.pop_back();
				script_value* target_ptr = &storage->values()[static_cast<size_t>(index)];
				if (element_type) {
					if (!vm_is_element_type_compatible(value, element_type, *target_ptr)) {
						std::string value_type = vm_value_type_name(value);
						std::string expected_type = vm_type_info_name(element_type);
						uint64_t value_type_id = symbolizer_->intern(value_type);
						uint64_t expected_type_id = symbolizer_->intern(expected_type);
						return raise_(
							make_error_code(runtime_error_code::array_element_type_mismatch),
							"Cannot assign '{0}' to element of type '{1}'",
							value_type_id, expected_type_id);
					}
					auto converted_result = vm_convert_array_element(engine_, value, element_type);
					if (!converted_result) { return raise_from(converted_result); }
					script_value converted = std::move(converted_result.value());
					*target_ptr = std::move(converted);
				} else {
					// No element type constraint - values deep-copy, shared_ptr handles share
					*target_ptr = clone_for_assignment(value);
				}
				stack_.push_back(assignment_result_used(f) ? clone_for_assignment(*target_ptr) : std::move(value));   // a copy of the stored element is the expression's value
				return {};
			}
		}
	}
	// Slow replay: identical to the old two-op sequence
	vm_instruction index_ins{opcode::op_index, index_flag_lvalue_write | (ins.a & index_flag_lvalue_shape), 0, 0};
	auto indexed = exec_index(f, index_ins);
	if (indexed == op_status::failed) {
		return indexed;
	}
	return exec_index_assign(f, ins);
}

// Constant operands re-materialize with the engine before ANY stack push: chunk
// constants are engine-less parse-time templates whose string payload is the AST
// literal's own block (KEEP BYTE-PARALLEL with run_dispatch's op_const case). A raw
// copy would refcount that shared block — parallel workers touch it cross-thread,
// and the non-atomic counts corrupt (found via JAISCRIPT_DIAG_XTHREAD_RC).
// A chunk constant by index: the string row reaches the worker's per-chunk copy
script_value vm_backend::materialize_constant(const chunk& code, uint32_t const_index) {
	const script_value& tmpl = code.constants[const_index];
	if (tmpl.raw_storage_index() == script_value::TYPEID_STRING) {
		return literal_string_value(code, const_index, tmpl.get_storage().get<strong_ptr<script_string>>());
	}
	return materialize_constant(tmpl);
}

script_value vm_backend::materialize_constant(const script_value& tmpl) {
	const auto& storage = tmpl.get_storage();
	switch (storage.index()) {
		case script_value::TYPEID_INT: return script_value(storage.get<script_int>(), engine_);
		case script_value::TYPEID_FLOAT: return script_value(storage.get<script_float>(), engine_);
		case script_value::TYPEID_STRING: return literal_string_value(storage.get<strong_ptr<script_string>>());
		case script_value::TYPEID_CHAR: return script_value(storage.get<script_char>(), engine_);
		case script_value::TYPEID_BOOL: return script_value(storage.get<script_bool>(), engine_);
		case script_value::TYPEID_NULL: return make_null();
		default: {
			script_value copy = tmpl;
			copy.set_engine(engine_);
			return copy;
		}
	}
}

op_status vm_backend::raw_int_arith(token_type op, script_int a, script_int b,
                                    script_int& out, bool& handled) {
	handled = true;
	switch (op) {
	case token_type::plus:
		if (!ints::try_add(a, b, out)) return raise_from(vm_int_overflow_v("Integer overflow in '+'"));
		return {};
	case token_type::minus:
		if (!ints::try_sub(a, b, out)) return raise_from(vm_int_overflow_v("Integer overflow in '-'"));
		return {};
	case token_type::star:
		if (!ints::try_mul(a, b, out)) return raise_from(vm_int_overflow_v("Integer overflow in '*'"));
		return {};
	case token_type::slash:
		if (b == 0) return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
		if (!ints::try_div(a, b, out)) return raise_from(vm_int_overflow_v("Integer overflow in '/'"));
		return {};
	case token_type::percent:
		if (b == 0) return raise_(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
		out = ints::mod(a, b);
		return {};
	default:
		handled = false;
		return {};
	}
}

// The ONE fused subscript read, sink-templated (op_index_fused pushes; the decl variant
// lands in the declared slot — parity by construction, same shape as
// binary_fused_compute). Container+index resolve as operands (slot in-place reads - no
// LOAD dispatches, no operand pushes). The inline path is exec_index's in-bounds array
// branch VERBATIM; everything else (map/borrow/custom-[]/OOB/non-int index) pushes the
// resolved operands and replays the unfused op, so every error keeps one spelling.
// Mid-op unwinding leaves the exact unfused stack states regardless of sink.
template <bool SinkIsStack, typename Sink>
op_status vm_backend::index_fused_read(frame& f, const vm_instruction& ins, Sink&& sink) {
	const fused_index_proto& p = f.code->fused_index_protos[ins.a];
	std::optional<script_value> cscratch, mscratch, iscratch;
	const script_value* container_ptr;
	if (p.container.const_index != k_invalid_u32) {
		container_ptr = &f.code->constants[p.container.const_index];
	} else {
		// binary-index sites park the container at role 2: binary_fused_compute owns
		// roles 0/1, and cache entries are provenance-checked by {env, epoch}, not symbol
		const script_value* resolved = fused_ident_value(f, p.container, cscratch,
		                                  p.index_binary != k_invalid_u32 ? f.ip * 3 + 2 : f.ip * 3);
		if (!resolved) return op_status::failed;
		container_ptr = resolved;
		if (p.container.member_node != k_invalid_u32) {
#ifdef JAISCRIPT_VM_PROFILE
			++profile_index_read_paths_[7];
#endif
			VM_TRY(fused_member_container(f, static_cast<size_t>(&ins - f.code->code.data()),
			                              *container_ptr, p.container.member_node, mscratch, container_ptr));
			if (is_unwinding_) [[unlikely]] {
				// pair parity: GET_MEMBER's out stays pushed, INDEX never runs
				stack_.push_back(*container_ptr);
				return {};
			}
		}
	}
	const script_value* index_ptr = nullptr;
	std::optional<script_value> computed_index, csnap;
	bool have_raw_index = false;
	script_int raw_index = 0;
	if (p.index_binary != k_invalid_u32) {
		// Provably-pure shapes (raw numeric idents/lits, no custom numeric ops) run
		// no user code, so the container POINTER stays valid
		// across the compute. Int arithmetic short-circuits to a RAW index — no
		// script_value is born at all; error spellings replicate binary_fused_compute's
		// int row verbatim. Everything else snapshots the container first — exactly
		// the copy the unfused LOAD paid — keeping container-before-index order.
		const fused_binary_proto& bp = f.code->fused_binary_protos[p.index_binary];
		const token_type bop = static_cast<token_type>(bp.op);
		bool pure = false;
		if (!has_custom_numeric_ops_ && is_numeric_binary_op(bop)) {
			const script_value* pl = fused_cmp_operand(f, bp.left, f.ip * 3);
			const script_value* pr = pl ? fused_cmp_operand(f, bp.right, f.ip * 3 + 1) : nullptr;
			if (pr) {
				const size_t li = pl->raw_storage_index(), ri = pr->raw_storage_index();
				pure = (li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
				       (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT);
				if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
					VM_TRY(raw_int_arith(bop, pl->unchecked_as_int(), pr->unchecked_as_int(),
					                     raw_index, have_raw_index));
#ifdef JAISCRIPT_VM_PROFILE
					if (have_raw_index) { ++profile_index_read_paths_[6]; }
#endif
				}
			}
		}
		// Left-spine chain: fold raw while rights stay int (fused_ident_value runs no
		// user code, so the container pointer holds); the first non-int right hands
		// off to the boxed tail below at the exact accumulator position.
		const auto& chain = p.index_chain_ext;
		size_t chain_pos = 0;
		if (have_raw_index && !chain.empty()) {
			std::optional<script_value> rscratch;
			while (chain_pos < chain.size()) {
				const fused_binary_proto& sp = f.code->fused_binary_protos[chain[chain_pos]];
				const script_value* rp2;
				if (sp.right.const_index != k_invalid_u32) {
					rp2 = &f.code->constants[sp.right.const_index];
				} else {
					rscratch.reset();
					const script_value* resolved = fused_ident_value(f, sp.right, rscratch, SIZE_MAX);
					if (!resolved) return op_status::failed;
					rp2 = resolved;
				}
				if (rp2->raw_storage_index() != script_value::TYPEID_INT) break;
				bool step_handled = false;
				VM_TRY(raw_int_arith(static_cast<token_type>(sp.op), raw_index,
				                     rp2->unchecked_as_int(), raw_index, step_handled));
				if (!step_handled) break;   // compiler emits arith-only; defensive
				++chain_pos;
			}
		}
		if (!have_raw_index) {
			if (!pure) {
				csnap.emplace(*container_ptr);
				container_ptr = &*csnap;
			}
			VM_TRY(binary_fused_compute(f, p.index_binary,
			                            [&](script_value&& v) { computed_index.emplace(std::move(v)); }));
			if (is_unwinding_) [[unlikely]] {
				// pair parity: LOAD's container copy and BINARY_FUSED's result (when its
				// sink ran) stay pushed, INDEX never runs
				stack_.push_back(*container_ptr);
				if (computed_index) {
					stack_.push_back(std::move(*computed_index));
				}
				return {};
			}
			index_ptr = &*computed_index;
		}
		if (chain_pos < chain.size() || (!chain.empty() && !have_raw_index)) {
			// Boxed chain tail: the verbatim unfused stack sequence from the accumulator
			// — container copy (its push IS the pin), accumulator, then one op_binary
			// per remaining step, finished by the real op_index. Remaining steps can run
			// user code (object operands / overloads); everything they see matches the
			// unfused twin because it IS the unfused twin.
			if (p.container.const_index != k_invalid_u32) { stack_.push_back(materialize_constant(*f.code, p.container.const_index)); }
			else { stack_.push_back(*container_ptr); }
			if (computed_index) { stack_.push_back(std::move(*computed_index)); }
			else { stack_.push_back(script_value(raw_index, engine_)); }
			for (size_t i = chain_pos; i < chain.size(); ++i) {
				const fused_binary_proto& sp = f.code->fused_binary_protos[chain[i]];
				if (sp.right.const_index != k_invalid_u32) {
					stack_.push_back(materialize_constant(*f.code, sp.right.const_index));
				} else {
					std::optional<script_value> rs;
					const script_value* resolved = fused_ident_value(f, sp.right, rs, SIZE_MAX);
					if (!resolved) return op_status::failed;
					stack_.push_back(*resolved);
				}
				const vm_instruction bin_ins{opcode::op_binary, static_cast<uint32_t>(sp.op), binary_shape_none, 0};
				VM_TRY(exec_binary(f, bin_ins));
				if (is_unwinding_) [[unlikely]] {
					return {};   // pair parity: the partial stack stands, dispatch unwinds
				}
			}
			const vm_instruction index_ins{opcode::op_index, ins.b, 0, 0};
			if constexpr (SinkIsStack) {
				return exec_index(f, index_ins);
			} else {
				VM_TRY(exec_index(f, index_ins));
				if (is_unwinding_) [[unlikely]] { return {}; }   // pushed result stands (pair parity)
				script_value replayed = std::move(stack_.back());
				stack_.pop_back();
				sink(std::move(replayed));
				return {};
			}
		}
	} else if (p.index.const_index != k_invalid_u32) {
		index_ptr = &f.code->constants[p.index.const_index];
	} else {
		auto resolved = fused_ident_value(f, p.index, iscratch, f.ip * 3 + 1);
		if (!resolved) return op_status::failed;
		index_ptr = resolved;
	}

	const script_value& left = *container_ptr;      // fused_ident_value hands back deref'd storage
	bool index_is_int = have_raw_index;
	script_int index_int = raw_index;
	if (!have_raw_index) {
		const script_value& right = index_ptr->deref();
		index_is_int = right.is_int();
		if (index_is_int) { index_int = right.unchecked_as_int(); }
	}
	if (left.raw_storage_index() == script_value::TYPEID_ARRAY && index_is_int) {
		const script_int index = index_int;
		const script_array* node = left.unchecked_array_node();
		if (index >= 0 && index < static_cast<script_int>(node->size())) {
			const bool lvalue_shape = (ins.b & index_flag_lvalue_shape) != 0;
			const bool transient_read = (ins.b & index_flag_transient_read) != 0 && !has_custom_binary_ops_;
			if (lvalue_shape && !transient_read) {
#ifdef JAISCRIPT_VM_PROFILE
				++profile_index_read_paths_[3];
#endif
				auto array_type_info = left.get_type_info();
				type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
				// slot/env storage is mutable by nature; the resolver's constness is
				// interface conservatism (same shape as exec_index's map const_cast)
				sink(script_value::make_element_reference(
					const_cast<script_value&>(left).get_array_storage(), static_cast<size_t>(index), engine_, element_type));
				return {};
			}
			if (node->is_typed()) {
				// Raw-capable sinks take the buffer payload with no value mint at all
				// (the decl lane lands it straight in the slot); byte kinds box through get()
				if constexpr (requires(Sink& s, script_int iv) { s.raw_int(iv); }) {
					if (node->kind() == script_array::kind_t::i64) {
#ifdef JAISCRIPT_VM_PROFILE
						++profile_index_read_paths_[0];
#endif
						sink.raw_int(node->ints()[static_cast<size_t>(index)]);
					} else if (node->kind() == script_array::kind_t::f64) {
#ifdef JAISCRIPT_VM_PROFILE
						++profile_index_read_paths_[1];
#endif
						sink.raw_float(node->floats()[static_cast<size_t>(index)]);
					} else {
						sink(node->get(static_cast<size_t>(index), engine_));
					}
					return {};
				} else {
					sink(node->get(static_cast<size_t>(index), engine_));   // raw buffer read
					return {};
				}
			}
			// Plain int/float elements of UNTYPED arrays skip the value copy (variant
			// dispatch + tag refcounts + copy dtor) through the same raw lanes typed
			// buffers use — admitted only when the sink stamps its own tag over the
			// landing (raw_tag_safe), so the dropped element tag is unobservable.
			// Every other element shape keeps the full copy below.
			if constexpr (requires(Sink& s, script_int iv) { s.raw_int(iv); s.raw_tag_safe(); }) {
				if (sink.raw_tag_safe()) {
					const script_value& el = node->values()[index];
					const size_t ei = el.raw_storage_index();
					if (ei == script_value::TYPEID_INT) {
#ifdef JAISCRIPT_VM_PROFILE
						++profile_index_read_paths_[0];
#endif
						sink.raw_int(el.unchecked_as_int());
						return {};
					}
					if (ei == script_value::TYPEID_FLOAT) {
#ifdef JAISCRIPT_VM_PROFILE
						++profile_index_read_paths_[1];
#endif
						sink.raw_float(el.unchecked_as_float());
						return {};
					}
				}
			}
#ifdef JAISCRIPT_VM_PROFILE
			++profile_index_read_paths_[2];
#endif
			sink(script_value(node->values()[index]));
			return {};
		}
	}
	// Constant-string map READ (Dev ruling 2026-07-11): find on the raw constant
	// bytes via the transparent comparator — no per-access key materialization.
	// Plain read shape only; lvalue/write shapes replay (entry references and
	// missing-key creation keep exec_index's one spelling). Mirrors exec_index's
	// map-read branch: hit copies (engine stamped), miss sinks null.
	if (left.raw_storage_index() == script_value::TYPEID_MAP &&
	    p.index.const_index != k_invalid_u32 &&
	    !(ins.b & (index_flag_lvalue_shape | index_flag_lvalue_write))) {
		const script_value& itmpl = f.code->constants[p.index.const_index];
		if (itmpl.raw_storage_index() == script_value::TYPEID_STRING) {
			const auto& map_ptr = const_cast<script_value&>(left).get_map_storage();
			if (map_ptr) {
#ifdef JAISCRIPT_VM_PROFILE
				++profile_index_read_paths_[4];
#endif
				auto it = map_ptr->find(map_string_key_probe{
					*itmpl.get_storage().get<strong_ptr<script_string>>()});
				if (it != map_ptr->end()) {
					script_value val = it->second;
					if (!val.has_valid_engine()) {
						val.set_engine(engine_);
					}
					sink(std::move(val));
				} else {
					sink(make_null());
				}
				return {};
			}
		}
	}
#ifdef JAISCRIPT_VM_PROFILE
	++profile_index_read_paths_[5];
#endif
	if (p.container.const_index != k_invalid_u32) { stack_.push_back(materialize_constant(*f.code, p.container.const_index)); }
	else { stack_.push_back(*container_ptr); }
	if (have_raw_index) { stack_.push_back(script_value(raw_index, engine_)); }
	else if (computed_index) { stack_.push_back(std::move(*computed_index)); }
	else if (p.index.const_index != k_invalid_u32) { stack_.push_back(materialize_constant(*f.code, p.index.const_index)); }
	else { stack_.push_back(*index_ptr); }
	const vm_instruction index_ins{opcode::op_index, ins.b, 0, 0};
	if constexpr (SinkIsStack) {
		return exec_index(f, index_ins);
	} else {
		VM_TRY(exec_index(f, index_ins));
		if (is_unwinding_) [[unlikely]] { return {}; }   // pushed result stands (pair parity)
		script_value replayed = std::move(stack_.back());
		stack_.pop_back();
		sink(std::move(replayed));
		return {};
	}
}

op_status vm_backend::exec_index_fused(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	if (f.ip + 1 < f.code->code.size()) { ++profile_index_fused_next_[static_cast<uint8_t>(f.code->code[f.ip + 1].op)]; }
#endif
	return index_fused_read<true>(f, ins, [this](script_value&& v) { stack_.push_back(std::move(v)); });
}

// Subscript read landing straight in the declared slot (the 3.59M INDEX_FUSED→DECL_VAR
// pair). Lane form: the landing facts (slot, expected raw index, type tag) were proven
// at the peephole, so a matching scalar read writes the slot with zero per-exec decl
// checks. Every other shape (mismatched scalars, references, env decls) pushes and
// runs op_decl_var verbatim with lvalue_init=1 — a subscript init is lvalue-shaped.
// direct slot when the compiler proved one, a literal, else the nullable fast head (env
// cache, member scope); never the full ladder, which may run user code
const script_value* vm_backend::fast_operand(frame& f, const fused_operand& operand, size_t cache_slot) {
	if (operand.const_index != k_invalid_u32) {
		return &f.code->constants[operand.const_index];
	}
	if (operand.direct) {
		if (f.window_backed) {
			return operand.slot < f.window_live ? &stack_[f.window_base + operand.slot] : nullptr;
		}
		return f.locals ? f.locals->get_local(operand.slot) : nullptr;
	}
	return fused_cmp_operand(f, operand, cache_slot);
}

// One int row of the index fold: the arithmetic rows are raw_int_arith's (an overflow or a
// zero divisor misses, the kernel raises), the bitwise rows handle_binary_op's
static bool typed_int_index_row(token_type op, script_int a, script_int b, script_int& out) noexcept {
	switch (op) {
	case token_type::plus: return ints::try_add(a, b, out);
	case token_type::minus: return ints::try_sub(a, b, out);
	case token_type::star: return ints::try_mul(a, b, out);
	case token_type::slash: return b != 0 && ints::try_div(a, b, out);
	case token_type::percent: if (b == 0) return false; out = ints::mod(a, b); return true;
	case token_type::ampersand: out = a & b; return true;
	case token_type::pipe: out = a | b; return true;
	case token_type::caret: out = a ^ b; return true;
	case token_type::left_shift: if (b < 0 || b > 63) return false; out = a << b; return true;
	case token_type::right_shift: if (b < 0 || b > 63) return false; out = a >> b; return true;
	default: return false;
	}
}

bool vm_backend::typed_decl_land_int(frame& f, uint32_t dslot, uint8_t expect, type_info* stamp, script_int iv) {
	if (expect == static_cast<uint8_t>(script_value::TYPEID_FLOAT)) {
		return typed_decl_land_float(f, dslot, expect, stamp, static_cast<script_float>(iv));   // the decl's int -> float row
	}
	if (expect != static_cast<uint8_t>(script_value::TYPEID_INT) && expect != 0xFF) return false;
	if (script_value* slot = frame_slot(f, dslot)) {
		if (slot->raw_storage_index() == script_value::TYPEID_INT && slot->get_type_info().get() == stamp) {
			slot->unchecked_set_int_payload(iv);
			return true;
		}
	}
	script_value v(iv, engine_);
	if (stamp) { v.set_type_info(type_info_ptr{stamp}); }
	frame_slot_set(f, dslot, std::move(v));
	return true;
}

bool vm_backend::typed_decl_land_float(frame& f, uint32_t dslot, uint8_t expect, type_info* stamp, script_float fv) {
	if (expect != static_cast<uint8_t>(script_value::TYPEID_FLOAT) && expect != 0xFF) return false;
	if (script_value* slot = frame_slot(f, dslot)) {
		if (slot->raw_storage_index() == script_value::TYPEID_FLOAT && slot->get_type_info().get() == stamp) {
			slot->unchecked_set_float_payload(fv);
			return true;
		}
	}
	script_value v(fv, engine_);
	if (stamp) { v.set_type_info(type_info_ptr{stamp}); }
	frame_slot_set(f, dslot, std::move(v));
	return true;
}

const script_array* vm_backend::lane_element_source(const script_value& v, bool& borrowed) noexcept {
	const size_t idx = v.raw_storage_index();
	if (idx == script_value::TYPEID_ARRAY) { borrowed = false; return v.unchecked_array_node(); }
	if (idx == script_value::TYPEID_PARALLEL_BORROW) { borrowed = true; return v.parallel_borrow_array(); }
	return nullptr;
}

// The element at `at` into the decl slot, by the decl lane's rules for each buffer kind
// (typed_decl_read's landing, shared with op_t_load); false = the kernel's business
bool vm_backend::typed_decl_land_element(frame& f, uint32_t dslot, uint8_t expect, type_info* stamp, const script_array& node, size_t at, bool borrowed) {
	switch (node.kind()) {
	case script_array::kind_t::i64: return typed_decl_land_int(f, dslot, expect, stamp, node.ints()[at]);
	case script_array::kind_t::f64: return typed_decl_land_float(f, dslot, expect, stamp, node.floats()[at]);
	case script_array::kind_t::str: {
		if (borrowed) return false;   // the kernel copies the bytes of a captured string
		if (expect != static_cast<uint8_t>(script_value::TYPEID_STRING) && expect != 0xFF) return false;
		script_value v(node.strs()[at], engine_);   // the buffer's node, shared like every string copy
		if (stamp) { v.set_type_info(type_info_ptr{stamp}); }
		frame_slot_set(f, dslot, std::move(v));
		return true;
	}
	case script_array::kind_t::hetero: {
		const script_value& el = node.values()[at];
		const size_t ei = el.raw_storage_index();
		if (stamp) {
			if (ei == script_value::TYPEID_INT) return typed_decl_land_int(f, dslot, expect, stamp, el.unchecked_as_int());
			if (ei == script_value::TYPEID_FLOAT) return typed_decl_land_float(f, dslot, expect, stamp, el.unchecked_as_float());
		}
		if (borrowed && ei == script_value::TYPEID_STRING) return false;
		if (ei == expect ||
		    (expect == 0xFF &&
		     (ei == script_value::TYPEID_INT || ei == script_value::TYPEID_FLOAT || ei == script_value::TYPEID_BOOL ||
		      ei == script_value::TYPEID_CHAR || ei == script_value::TYPEID_STRING))) {
			script_value v(el);
			if (stamp) { v.set_type_info(type_info_ptr{stamp}); }
			frame_slot_set(f, dslot, std::move(v));
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

// op_t_load: typed_decl_read over the flattened site row (one line of memory instead of
// the proto, its operands, the fold proto and the chain vector)
JAI_FORCEINLINE bool vm_backend::typed_load_fast(frame& f, const vm_instruction& ins) {
	const chunk::typed_site& site = f.code->typed_sites[f.ip];
#ifdef JAISCRIPT_VM_PROFILE
	++profile_typed_cont_src_[site.cont_slot != k_invalid_u32 ? 0 : 1];
#endif
	if (!has_custom_numeric_ops_) {
		const script_value* cp = site.cont_slot != k_invalid_u32
			? frame_slot(f, site.cont_slot)
			: fused_cmp_operand(f, f.code->fused_index_protos[site.orig.a].container, site.fold_count ? f.ip * 3 + 2 : f.ip * 3);
		if (cp && cp->raw_storage_index() == script_value::TYPEID_REFERENCE) {   // a var& parameter
			cp = lane_deref_silent(*cp) ? &cp->deref() : nullptr;   // a getter-backed reference: the kernel reads it exactly once
		}
		bool borrowed = false;
		if (const script_array* node = cp ? lane_element_source(*cp, borrowed) : nullptr) {
			auto read_int = [&](uint32_t o, script_int& out) -> bool {
				const script_value* v = (o & k_typed_const_bit) ? &f.code->constants[o & ~k_typed_const_bit] : frame_slot(f, o);
				if (!v || v->raw_storage_index() != script_value::TYPEID_INT) return false;
				out = v->unchecked_as_int();
				return true;
			};
			script_int index = 0, r = 0;
			bool ok = read_int(site.operands[0], index);
			if (ok && site.fold_count) {
				ok = read_int(site.operands[1], r) && typed_int_index_row(static_cast<token_type>(site.fold_ops[0]), index, r, index);
				for (uint8_t k = 1; ok && k < site.fold_count; ++k) {
					ok = read_int(site.operands[k + 1], r) && typed_int_index_row(static_cast<token_type>(site.fold_ops[k]), index, r, index);
				}
			}
			if (ok && index >= 0 && static_cast<size_t>(index) < node->size() &&
			    typed_decl_land_element(f, ins.a, site.decl_expect, site.stamp, *node, static_cast<size_t>(index), borrowed)) {
				return true;
			}
		}
	}
	return false;
}

op_status vm_backend::exec_typed_load(frame& f, const vm_instruction& ins) {
	if (typed_load_fast(f, ins)) return {};
	return exec_index_fused_decl(f, f.code->typed_sites[f.ip].orig);
}

#ifdef JAISCRIPT_PILOT_CELLS
#ifdef JAISCRIPT_VM_PROFILE
#define JAI_CELLS_COUNT(k) ++profile_cells_[k]
#else
#define JAI_CELLS_COUNT(k) ((void)0)
#endif
namespace {
	inline int64_t cell_bits_of_float(script_float d) { int64_t b; std::memcpy(&b, &d, sizeof b); return b; }
	inline script_float cell_float_of_bits(int64_t b) { script_float d; std::memcpy(&d, &b, sizeof d); return d; }
}

// Pilot variant B. A cell instruction never raises: anything it cannot do exactly (a kind byte that
// does not match, overflow, a shift out of range, a divide by zero, a container guard, bounds, an
// element of another kind) is a miss, which flushes every dirty cell back to its slot through the
// landing that defined it and hands the general dispatch the ORIGINAL instruction's ip. Flushing
// reproduces the general form's slot state: payload in place when the slot already carries the
// stamp the landing would give it, the full landing otherwise.
void vm_backend::cells_flush(frame& f, const chunk::cell_loop& loop) {
	cell_state& cs = *f.cells;
	for (uint32_t k = 0; k < loop.cells.size(); ++k) {
		if (!(cs.dirty & (uint64_t(1) << k)) || cs.kind[k] == 0) continue;
		const chunk::cell_loop::cell& cl = loop.cells[k];
		script_value* s = frame_slot(f, cl.slot);
		if (cs.kind[k] == static_cast<uint8_t>(script_value::TYPEID_INT)) {
			const script_int v = static_cast<script_int>(cs.payload[k]);
			type_info* stamp = cl.landing == k_typed_dst_decl && cl.stamp ? cl.stamp : engine_->get_type_info_int();
			const bool in_place = s && s->raw_storage_index() == script_value::TYPEID_INT && s->get_engine() == engine_ &&
			                      (cl.landing == k_typed_dst_compound ? true : s->type_info_raw() == stamp);
			if (in_place) { JAI_CELLS_COUNT(8); s->unchecked_set_int_payload(v); continue; }
			JAI_CELLS_COUNT(9);
			script_value nv(v, engine_);
			if (cl.landing == k_typed_dst_decl && cl.stamp) { nv.set_type_info(type_info_ptr{cl.stamp}); }
			frame_slot_set(f, cl.slot, std::move(nv));
		} else {
			const script_float v = cell_float_of_bits(cs.payload[k]);
			type_info* stamp = cl.landing == k_typed_dst_decl && cl.stamp ? cl.stamp : engine_->get_type_info_float();
			const bool in_place = s && s->raw_storage_index() == script_value::TYPEID_FLOAT && s->get_engine() == engine_ &&
			                      (cl.landing == k_typed_dst_compound ? true : s->type_info_raw() == stamp);
			if (in_place) { JAI_CELLS_COUNT(8); s->unchecked_set_float_payload(v); continue; }
			JAI_CELLS_COUNT(9);
			script_value nv(v, engine_);
			if (cl.landing == k_typed_dst_decl && cl.stamp) { nv.set_type_info(type_info_ptr{cl.stamp}); }
			frame_slot_set(f, cl.slot, std::move(nv));
		}
	}
	cs.reset();
}

void vm_backend::cells_miss(frame& f, const chunk::cell_loop& loop, uint32_t original_ip, unsigned step) {
	JAI_CELLS_COUNT(6); JAI_CELLS_COUNT(5);
#ifdef JAISCRIPT_VM_PROFILE
	++profile_cells_miss_step_[step < 8 ? step : 7];
#else
	(void)step;
#endif
	cells_flush(f, loop);
	f.ip = original_ip;
}

// the original back edge: budget poll, then the entry guards on the live-in slots and the unboxing
op_status vm_backend::exec_cells_back(frame& f, const vm_instruction& ins) {
	if (execution_limit_exhausted()) [[unlikely]] { return raise_from(execution_limit_failure()); }
	const chunk::cell_loop& loop = f.code->cell_loops[ins.a];
	// window-backed record frames only: a fiber frame is call_frame-backed and takes the general form; a
	// frame copied into fiber state is default-constructed with a null area pointer; a record's pointer is
	// rebound to its own area at every push, so a reused record never aliases another frame's cells
	if (debug_hook_ || has_custom_numeric_ops_ || !f.cells || !f.window_backed) { f.ip = loop.head; return {}; }
	cell_state& cs = *f.cells;
	// every cell's slot, live-in or body-defined, must already hold the cell's kind with this engine's
	// canonical stamp and engine: the general form's first iteration established it, and a landing whose
	// slot holds another kind (a declared-type conversion on a store) stays on the general form
	for (uint32_t k = 0; k < loop.cells.size(); ++k) {
		const chunk::cell_loop::cell& cl = loop.cells[k];
		const script_value* s = frame_slot(f, cl.slot);
		const type_info* canonical = cl.kind == static_cast<uint8_t>(script_value::TYPEID_INT) ? engine_->get_type_info_int() : engine_->get_type_info_float();
		if (!s || s->raw_storage_index() != cl.kind || s->type_info_raw() != canonical || s->get_engine() != engine_) {
			JAI_CELLS_COUNT(1);
#ifdef JAISCRIPT_VM_PROFILE
			++profile_cells_refused_slot_[k < 64 ? k : 63];
#endif
			f.ip = loop.head;
			return {};
		}
	}
	// the load and store sites' containers, resolved once per entry: the body has no call, no rebinding and no
	// store to a container slot, so a node's identity holds until the exit or the miss (a miss re-enters through
	// this resolution); a site whose container is not a plain array node (a getter-backed reference, a typed var
	// array) keeps the loop on the general form, which runs the site's own resolution every time
	const size_t span = loop.copy_back - loop.copy_head;
	if (cs.site_node.size() < span) { cs.site_node.resize(span, nullptr); }
	for (uint32_t ip = loop.copy_head; ip < loop.copy_back; ++ip) {
		const vm_instruction& ci = f.code->code[ip];
		const script_array* node = nullptr;
		if (ci.op == opcode::op_c_load) {
			const chunk::typed_site& site = f.code->typed_sites[ci.c];
			const script_value* cp = site.cont_slot != k_invalid_u32
				? frame_slot(f, site.cont_slot)
				: fused_cmp_operand(f, f.code->fused_index_protos[site.orig.a].container, site.fold_count ? ci.c * 3 + 2 : ci.c * 3);
			if (cp && cp->raw_storage_index() == script_value::TYPEID_REFERENCE) { cp = lane_deref_silent(*cp) ? &cp->deref() : nullptr; }
			bool borrowed = false;
			node = cp ? lane_element_source(*cp, borrowed) : nullptr;
		} else if (ci.op == opcode::op_c_index_store) {
			const fused_index_proto& p = f.code->fused_index_protos[ci.a];
			const script_value* cp = fast_operand(f, p.container, p.index_binary != k_invalid_u32 ? ci.c * 3 + 2 : ci.c * 3);
			if (cp && cp->raw_storage_index() == script_value::TYPEID_REFERENCE) { cp = lane_deref_silent(*cp) ? &cp->deref() : nullptr; }
			if (cp && cp->raw_storage_index() == script_value::TYPEID_ARRAY) {
				const type_info* ti = cp->get_type_info().get();
				const script_array* n = const_cast<script_value&>(*cp).unchecked_get_array_storage().get();
				if (n && !(n->kind() == script_array::kind_t::hetero && ti && ti->element_type().get())) { node = n; }
			}
		} else { continue; }
		if (!node) { JAI_CELLS_COUNT(1); JAI_CELLS_COUNT(10); f.ip = loop.head; return {}; }
		cs.site_node[ip - loop.copy_head] = node;
	}
	if (cs.payload.size() < loop.cells.size()) {
#ifdef JAISCRIPT_VM_PROFILE
		if (cs.payload.empty()) { ++profile_cells_records_; }
#endif
		cs.payload.resize(loop.cells.size()); cs.kind.resize(loop.cells.size());
#ifdef JAISCRIPT_VM_PROFILE
		const uint64_t bytes = static_cast<uint64_t>(cs.payload.capacity()) * sizeof(int64_t) + static_cast<uint64_t>(cs.kind.capacity());
		if (bytes > profile_cells_area_bytes_max_) { profile_cells_area_bytes_max_ = bytes; }
#endif
	}
	for (uint32_t k = 0; k < loop.cells.size(); ++k) {
		const chunk::cell_loop::cell& cl = loop.cells[k];
		if (cl.live_in) {
			const script_value* s = frame_slot(f, cl.slot);
			cs.payload[k] = cl.kind == static_cast<uint8_t>(script_value::TYPEID_INT) ? static_cast<int64_t>(s->unchecked_as_int()) : cell_bits_of_float(s->unchecked_as_float());
			cs.kind[k] = cl.kind;
			JAI_CELLS_COUNT(3);
		} else {
			cs.kind[k] = 0;   // defined inside the body before any read; empty until then
		}
	}
	cs.loop = ins.a;
	cs.dirty = 0;
	JAI_CELLS_COUNT(0);
	f.ip = loop.copy_head;
#ifdef JAISCRIPT_PILOT_CLUSTER
	// variant D: the copy runs as one block from here, each cell instruction through its own handler and
	// no outer dispatch between them; an exit or a miss flushes (cs.loop clears) and leaves f.ip on the
	// general form, a raise returns its status; the budget is polled at the cell back edge as before
	for (;;) {
		const vm_instruction& ci = f.code->code[f.ip];
		op_status st = op_status::ok;
		switch (ci.op) {
		case opcode::op_c_cmp: st = exec_c_cmp(f, ci); break;
		case opcode::op_c_arith: st = exec_c_arith(f, ci); break;
		case opcode::op_c_load: st = exec_c_load(f, ci); break;
		case opcode::op_c_index_store: st = exec_c_index_store(f, ci); break;
		case opcode::op_c_back: st = exec_c_back(f, ci); break;
		default: cells_flush(f, loop); return {};   // unreachable by construction (the copy is cell ops only); materialize before handing f.ip back
		}
		if (st == op_status::failed) return st;
		if (cs.loop == k_invalid_u32) return {};   // flushed at an exit or a miss: f.ip is the general form's
	}
#else
	return {};
#endif
}
JAI_FORCEINLINE op_status vm_backend::exec_c_back(frame& f, const vm_instruction& ins) {
	const chunk::cell_loop& loop = f.code->cell_loops[ins.a];
	if (execution_limit_exhausted()) [[unlikely]] {
		cells_flush(f, loop);   // the error and the debugger see materialized slots
		f.ip = loop.back;
		return raise_from(execution_limit_failure());
	}
	JAI_CELLS_COUNT(2);
	f.ip = loop.copy_head;
	return {};
}
JAI_FORCEINLINE op_status vm_backend::exec_c_cmp(frame& f, const vm_instruction& ins) {
	JAI_CELLS_COUNT(7);
	cell_state& cs = *f.cells;
	const chunk::cell_loop& loop = f.code->cell_loops[cs.loop];
	const opcode op = static_cast<opcode>(ins.a >> k_cell_op_shift);
	const uint32_t lk = ins.a & k_cell_index_mask;
	const uint8_t kind = op <= opcode::op_t_jne_ii ? static_cast<uint8_t>(script_value::TYPEID_INT) : static_cast<uint8_t>(script_value::TYPEID_FLOAT);
	bool truthy;
	if (cs.kind[lk] != kind) { cells_miss(f, loop, loop.head, 0); return {}; }
	if (ins.b & k_typed_const_bit) {
		const script_value& rc = f.code->constants[ins.b & ~k_typed_const_bit];
		if (rc.raw_storage_index() != kind) { cells_miss(f, loop, loop.head, 0); return {}; }
		if (kind == static_cast<uint8_t>(script_value::TYPEID_INT)) {
			const script_int a = static_cast<script_int>(cs.payload[lk]), b = rc.unchecked_as_int();
			switch (op) { case opcode::op_t_jlt_ii: truthy = a < b; break; case opcode::op_t_jle_ii: truthy = a <= b; break; case opcode::op_t_jgt_ii: truthy = a > b; break; case opcode::op_t_jge_ii: truthy = a >= b; break; case opcode::op_t_jeq_ii: truthy = a == b; break; default: truthy = a != b; break; }
		} else {
			const script_float a = cell_float_of_bits(cs.payload[lk]), b = rc.unchecked_as_float();
			switch (op) { case opcode::op_t_jlt_ff: truthy = a < b; break; case opcode::op_t_jle_ff: truthy = a <= b; break; case opcode::op_t_jgt_ff: truthy = a > b; break; case opcode::op_t_jge_ff: truthy = a >= b; break; case opcode::op_t_jeq_ff: truthy = a == b; break; default: truthy = a != b; break; }
		}
	} else {
		if (cs.kind[ins.b] != kind) { cells_miss(f, loop, loop.head, 0); return {}; }
		if (kind == static_cast<uint8_t>(script_value::TYPEID_INT)) {
			const script_int a = static_cast<script_int>(cs.payload[lk]), b = static_cast<script_int>(cs.payload[ins.b]);
			switch (op) { case opcode::op_t_jlt_ii: truthy = a < b; break; case opcode::op_t_jle_ii: truthy = a <= b; break; case opcode::op_t_jgt_ii: truthy = a > b; break; case opcode::op_t_jge_ii: truthy = a >= b; break; case opcode::op_t_jeq_ii: truthy = a == b; break; default: truthy = a != b; break; }
		} else {
			const script_float a = cell_float_of_bits(cs.payload[lk]), b = cell_float_of_bits(cs.payload[ins.b]);
			switch (op) { case opcode::op_t_jlt_ff: truthy = a < b; break; case opcode::op_t_jle_ff: truthy = a <= b; break; case opcode::op_t_jgt_ff: truthy = a > b; break; case opcode::op_t_jge_ff: truthy = a >= b; break; case opcode::op_t_jeq_ff: truthy = a == b; break; default: truthy = a != b; break; }
		}
	}
	if (truthy) { ++f.ip; return {}; }
	JAI_CELLS_COUNT(4);
	cells_flush(f, loop);   // the loop's exit: materialize, then continue after the loop
	f.ip = ins.c;
	return {};
}
JAI_FORCEINLINE op_status vm_backend::exec_c_arith(frame& f, const vm_instruction& ins) {
	JAI_CELLS_COUNT(7);
	cell_state& cs = *f.cells;
	const chunk::cell_loop& loop = f.code->cell_loops[cs.loop];
	const opcode op = static_cast<opcode>(ins.a >> k_cell_op_shift);
	const uint32_t dk = ins.a & k_cell_index_mask;
	const uint32_t original_ip = static_cast<uint32_t>(loop.head + 1 + (f.ip - (loop.copy_head + 1)));
	const uint8_t kind = op <= opcode::op_t_shr_ii ? static_cast<uint8_t>(script_value::TYPEID_INT) : static_cast<uint8_t>(script_value::TYPEID_FLOAT);
	if (cs.kind[ins.b] != kind) { cells_miss(f, loop, original_ip, 1); return {}; }
	int64_t right_bits;
	if (ins.c & k_typed_const_bit) {
		const script_value& rc = f.code->constants[ins.c & ~k_typed_const_bit];
		if (rc.raw_storage_index() != kind) { cells_miss(f, loop, original_ip, 1); return {}; }
		right_bits = kind == static_cast<uint8_t>(script_value::TYPEID_INT) ? static_cast<int64_t>(rc.unchecked_as_int()) : cell_bits_of_float(rc.unchecked_as_float());
	} else {
		if (cs.kind[ins.c] != kind) { cells_miss(f, loop, original_ip, 1); return {}; }
		right_bits = cs.payload[ins.c];
	}
	if (kind == static_cast<uint8_t>(script_value::TYPEID_INT)) {
		const script_int a = static_cast<script_int>(cs.payload[ins.b]), b = static_cast<script_int>(right_bits);
		script_int r; bool ok = true;
		switch (op) {
		case opcode::op_t_add_ii: ok = ints::try_add(a, b, r); break;
		case opcode::op_t_sub_ii: ok = ints::try_sub(a, b, r); break;
		case opcode::op_t_mul_ii: ok = ints::try_mul(a, b, r); break;
		case opcode::op_t_div_ii: ok = b != 0 && ints::try_div(a, b, r); break;
		case opcode::op_t_mod_ii: ok = b != 0; if (ok) { r = ints::mod(a, b); } break;
		case opcode::op_t_and_ii: r = a & b; break;
		case opcode::op_t_or_ii: r = a | b; break;
		case opcode::op_t_xor_ii: r = a ^ b; break;
		case opcode::op_t_shl_ii: ok = b >= 0 && b <= 63; if (ok) { r = a << b; } break;
		case opcode::op_t_shr_ii: ok = b >= 0 && b <= 63; if (ok) { r = a >> b; } break;
		default: ok = false; break;
		}
		if (!ok) { cells_miss(f, loop, original_ip, 1); return {}; }
		cs.payload[dk] = static_cast<int64_t>(r);
	} else {
		const script_float a = cell_float_of_bits(cs.payload[ins.b]), b = cell_float_of_bits(right_bits);
		script_float r; bool ok = true;
		switch (op) {
		case opcode::op_t_add_ff: r = a + b; break;
		case opcode::op_t_sub_ff: r = a - b; break;
		case opcode::op_t_mul_ff: r = a * b; break;
		case opcode::op_t_div_ff: ok = b != 0.0; if (ok) { r = a / b; } break;
		case opcode::op_t_mod_ff: ok = b != 0.0; if (ok) { r = std::fmod(a, b); } break;
		default: ok = false; break;
		}
		if (!ok) { cells_miss(f, loop, original_ip, 1); return {}; }
		cs.payload[dk] = cell_bits_of_float(r);
	}
	cs.kind[dk] = kind;
	cs.dirty |= uint64_t(1) << dk;
	++f.ip;
	return {};
}

// T_LOAD's fast path landing raw: the container through the original site, every guard kept
JAI_FORCEINLINE op_status vm_backend::exec_c_load(frame& f, const vm_instruction& ins) {
	JAI_CELLS_COUNT(7);
	cell_state& cs = *f.cells;
	const chunk::cell_loop& loop = f.code->cell_loops[ins.b];
	const uint32_t original_ip = ins.c;
	const chunk::typed_site& site = f.code->typed_sites[original_ip];
	auto miss = [&]() -> op_status { cells_miss(f, loop, original_ip, 2); return {}; };
	const script_array* node = cs.site_node[f.ip - loop.copy_head];   // resolved at entry
	if (!node) return miss();
	auto read_int = [&](uint32_t o, script_int& out) -> bool {
		if (o & k_typed_const_bit) { const script_value& v = f.code->constants[o & ~k_typed_const_bit]; if (v.raw_storage_index() != script_value::TYPEID_INT) return false; out = v.unchecked_as_int(); return true; }
		const uint32_t k = loop.slot_to_cell[o & k_typed_slot_mask];
		if (k == k_invalid_u32 || cs.kind[k] != static_cast<uint8_t>(script_value::TYPEID_INT)) return false;
		out = static_cast<script_int>(cs.payload[k]);
		return true;
	};
	script_int index = 0, r = 0;
	bool ok = read_int(site.operands[0], index);
	if (ok && site.fold_count) {
		ok = read_int(site.operands[1], r) && typed_int_index_row(static_cast<token_type>(site.fold_ops[0]), index, r, index);
		for (uint8_t k = 1; ok && k < site.fold_count; ++k) {
			ok = read_int(site.operands[k + 1], r) && typed_int_index_row(static_cast<token_type>(site.fold_ops[k]), index, r, index);
		}
	}
	if (!ok || index < 0 || static_cast<size_t>(index) >= node->size()) return miss();
	const size_t at = static_cast<size_t>(index);
	const uint8_t expect = site.decl_expect;
	int64_t bits;
	switch (node->kind()) {
	case script_array::kind_t::i64:
		if (expect == static_cast<uint8_t>(script_value::TYPEID_INT)) { bits = node->ints()[at]; }
		else { bits = cell_bits_of_float(static_cast<script_float>(node->ints()[at])); }   // the decl's int -> float row
		break;
	case script_array::kind_t::f64:
		if (expect != static_cast<uint8_t>(script_value::TYPEID_FLOAT)) return miss();   // float -> int is the checked kernel's, not a raw landing
		bits = cell_bits_of_float(node->floats()[at]);
		break;
	case script_array::kind_t::hetero: {
		const script_value& el = node->values()[at];
		const size_t ei = el.raw_storage_index();
		if (ei == script_value::TYPEID_INT && expect == static_cast<uint8_t>(script_value::TYPEID_INT)) { bits = el.unchecked_as_int(); }
		else if (ei == script_value::TYPEID_INT && expect == static_cast<uint8_t>(script_value::TYPEID_FLOAT)) { bits = cell_bits_of_float(static_cast<script_float>(el.unchecked_as_int())); }
		else if (ei == script_value::TYPEID_FLOAT && expect == static_cast<uint8_t>(script_value::TYPEID_FLOAT)) { bits = cell_bits_of_float(el.unchecked_as_float()); }
		else return miss();
		break;
	}
	default: return miss();
	}
	cs.payload[ins.a] = bits;
	cs.kind[ins.a] = expect;
	cs.dirty |= uint64_t(1) << ins.a;
	++f.ip;
	return {};
}

// the typed index store lane with its index and value read from cells, every container guard kept
JAI_FORCEINLINE op_status vm_backend::exec_c_index_store(frame& f, const vm_instruction& ins) {
	JAI_CELLS_COUNT(7);
	cell_state& cs = *f.cells;
	const chunk::cell_loop& loop = f.code->cell_loops[ins.b];
	const uint32_t original_ip = ins.c;
	const fused_index_proto& p = f.code->fused_index_protos[ins.a];
	auto miss = [&]() -> op_status { cells_miss(f, loop, original_ip, 3); return {}; };
	auto cell_int = [&](const fused_operand& o, script_int& out) -> bool {
		if (o.const_index != k_invalid_u32) { const script_value& v = f.code->constants[o.const_index]; if (v.raw_storage_index() != script_value::TYPEID_INT) return false; out = v.unchecked_as_int(); return true; }
		if (o.slot == k_invalid_u32 || o.slot >= loop.slot_to_cell.size()) return false;
		const uint32_t k = loop.slot_to_cell[o.slot];
		if (k == k_invalid_u32 || cs.kind[k] != static_cast<uint8_t>(script_value::TYPEID_INT)) return false;
		out = static_cast<script_int>(cs.payload[k]);
		return true;
	};
	// the value: a cell of either kind, or a constant; its stamp and engine are what its landing gave it
	// (a var element takes the value's stamp and engine, as typed_index_store does)
	uint8_t vkind; int64_t vbits; type_info_ptr vstamp; auto* veng = engine_;
	if (p.store_value.const_index != k_invalid_u32) {
		const script_value& v = f.code->constants[p.store_value.const_index];
		vkind = static_cast<uint8_t>(v.raw_storage_index());
		if (vkind == static_cast<uint8_t>(script_value::TYPEID_INT)) { vbits = v.unchecked_as_int(); }
		else if (vkind == static_cast<uint8_t>(script_value::TYPEID_FLOAT)) { vbits = cell_bits_of_float(v.unchecked_as_float()); }
		else return miss();
		vstamp = v.get_type_info(); veng = v.get_engine();
	} else {
		if (p.store_value.slot == k_invalid_u32 || p.store_value.slot >= loop.slot_to_cell.size()) return miss();
		const uint32_t k = loop.slot_to_cell[p.store_value.slot];
		if (k == k_invalid_u32 || cs.kind[k] == 0) return miss();
		vkind = cs.kind[k]; vbits = cs.payload[k];
		const chunk::cell_loop::cell& vc = loop.cells[k];
		vstamp = type_info_ptr{vc.landing == k_typed_dst_decl && vc.stamp ? vc.stamp
			: (vkind == static_cast<uint8_t>(script_value::TYPEID_INT) ? engine_->get_type_info_int() : engine_->get_type_info_float())};
	}
	script_int index;
	if (p.index_binary != k_invalid_u32) {
		const fused_binary_proto& bp = f.code->fused_binary_protos[p.index_binary];
		script_int l, r;
		if (!cell_int(bp.left, l) || !cell_int(bp.right, r) || !typed_int_index_row(static_cast<token_type>(bp.op), l, r, index)) return miss();
		for (const uint32_t step : p.index_chain_ext) {   // ((l op r) op1 r1) op2 r2 ...: typed_fold_index's fold, cells for the rights
			const fused_binary_proto& sp = f.code->fused_binary_protos[step];
			script_int r2;
			if (!cell_int(sp.right, r2) || !typed_int_index_row(static_cast<token_type>(sp.op), index, r2, index)) return miss();
		}
	} else if (!cell_int(p.index, index)) return miss();
	script_array* node = const_cast<script_array*>(cs.site_node[f.ip - loop.copy_head]);   // resolved at entry
	if (!node || index < 0 || static_cast<size_t>(index) >= node->size()) return miss();
	const size_t at = static_cast<size_t>(index);
	switch (node->kind()) {
	case script_array::kind_t::i64:
		if (vkind == static_cast<uint8_t>(script_value::TYPEID_FLOAT)) { const script_float d = cell_float_of_bits(vbits); if (!detail::float_fits_script_int(d)) return miss(); node->ints()[at] = static_cast<script_int>(d); }
		else { node->ints()[at] = static_cast<script_int>(vbits); }
		break;
	case script_array::kind_t::f64:
		node->floats()[at] = vkind == static_cast<uint8_t>(script_value::TYPEID_INT) ? static_cast<script_float>(static_cast<script_int>(vbits)) : cell_float_of_bits(vbits);
		break;
	case script_array::kind_t::hetero: {   // a typed var array was refused at entry
		script_value& target = node->values()[at];
		if (target.raw_storage_index() != vkind) return miss();
		if (vkind == static_cast<uint8_t>(script_value::TYPEID_INT)) { target.unchecked_set_int_payload(static_cast<script_int>(vbits)); }
		else { target.unchecked_set_float_payload(cell_float_of_bits(vbits)); }
		target.set_type_info(vstamp);
		target.set_engine(veng);
		break;
	}
	default: return miss();
	}
	++f.ip;
	return {};
}
#endif


// Pilot variant C: the back edge of a closed typed loop runs the loop as one block. Each step is
// the typed op's own fast path with every guard it has today; a miss hands the exact ip back to
// the general dispatch, which runs that instruction with its fallback and continues in the
// general form (the block resumes at the next back edge). A debugger armed or a registered
// numeric operator takes the general back edge. The budget is polled once per iteration, as
// LOOP_BACK polls it. Nothing here pins a slot address: every step resolves its slots as the
// general op does, so a landing that grows the value stack needs no capacity proof.
#ifdef JAISCRIPT_PILOT_CLUSTER
#ifdef JAISCRIPT_VM_PROFILE
#define JAI_BLOCK_COUNT(k) ++profile_block_[k]
#else
#define JAI_BLOCK_COUNT(k) ((void)0)
#endif
op_status vm_backend::exec_typed_block_back(frame& f, const vm_instruction& ins) {
	const chunk::block_proto& bp = f.code->block_protos[ins.a];
	if (debug_hook_ || has_custom_numeric_ops_) {
		if (execution_limit_exhausted()) [[unlikely]] { return raise_from(execution_limit_failure()); }
		f.ip = bp.head;
		return {};
	}
	const auto& code = f.code->code;
	JAI_BLOCK_COUNT(0);
	for (;;) {
		// the back edge: one budget poll per iteration, as LOOP_BACK polls it
		if (execution_limit_exhausted()) [[unlikely]] { f.ip = bp.back; return raise_from(execution_limit_failure()); }
		f.ip = bp.head;
		JAI_BLOCK_COUNT(1);
		const int cmp = typed_cmp_jump_fast(f, code[bp.head]);
		if (cmp < 0) { JAI_BLOCK_COUNT(4); return {}; }   // f.ip == head: the general dispatch runs the compare with its fallback
		if (cmp > 1) { JAI_BLOCK_COUNT(5); return {}; }   // the loop's exit: f.ip is the exit target
		for (uint32_t ip = bp.head + 1; ip < bp.back; ++ip) {
			f.ip = ip;
			const vm_instruction& st = code[ip];
			JAI_BLOCK_COUNT(2);
			bool ok;
			if (st.op == opcode::op_t_load) { ok = typed_load_fast(f, st); }
			else if (st.op == opcode::op_index_store_fused) { ok = typed_index_store(f, f.code->fused_index_protos[st.a], true); }
			else { ok = typed_arith_fast(f, st); }
			if (!ok) { JAI_BLOCK_COUNT(4); return {}; }   // f.ip == ip: the general dispatch runs this instruction with its fallback
		}
		JAI_BLOCK_COUNT(3);
	}
}
#endif

// The proven index: a direct slot or literal, or the int fold with its chain steps (the
// operands' cache roles are the kernel's: binary left/right at ip*3 / ip*3+1)
bool vm_backend::typed_fold_index(frame& f, const fused_index_proto& p, script_int& index) {
	if (p.index_binary != k_invalid_u32) {
		const fused_binary_proto& bp = f.code->fused_binary_protos[p.index_binary];
		const script_value* lp = fast_operand(f, bp.left, f.ip * 3);
		const script_value* rp = lp ? fast_operand(f, bp.right, f.ip * 3 + 1) : nullptr;
		if (!rp || lp->raw_storage_index() != script_value::TYPEID_INT || rp->raw_storage_index() != script_value::TYPEID_INT) return false;
		if (!typed_int_index_row(static_cast<token_type>(bp.op), lp->unchecked_as_int(), rp->unchecked_as_int(), index)) return false;
		for (const uint32_t step : p.index_chain_ext) {
			const fused_binary_proto& sp = f.code->fused_binary_protos[step];
			const script_value* r2 = fast_operand(f, sp.right, SIZE_MAX);   // proven direct or literal
			if (!r2 || r2->raw_storage_index() != script_value::TYPEID_INT) return false;
			if (!typed_int_index_row(static_cast<token_type>(sp.op), index, r2->unchecked_as_int(), index)) return false;
		}
		return true;
	}
	const script_value* ip = fast_operand(f, p.index, f.ip * 3 + 1);
	if (!ip || ip->raw_storage_index() != script_value::TYPEID_INT) return false;
	index = ip->unchecked_as_int();
	return true;
}

// ---- typed scalar set (opcode.hpp op_t_*) ----

bool vm_backend::lane_deref_silent(const script_value& v) noexcept {
	const auto* h = v.get_reference_holder();
	return h && !h->property;
}

#ifdef JAISCRIPT_VM_PROFILE
#define JAI_TYPED_LAND(k) ++profile_typed_land_[k]
#define JAI_TYPED_INPLACE(k) ++profile_typed_inplace_[k]
#else
#define JAI_TYPED_LAND(k) ((void)0)
#define JAI_TYPED_INPLACE(k) ((void)0)
#endif

bool vm_backend::typed_land_int(frame& f, uint32_t a, script_int v) {
	const uint32_t slot = a & k_typed_slot_mask;
	script_value* s = frame_slot(f, slot);   // null = a window slot not yet live: the landing grows it, a store does not
	switch (a >> k_typed_dst_shift) {
	case k_typed_dst_temp:
		// a temp this engine built already carries the canonical int stamp and the engine pointer the construct path would give it
		if (s && s->raw_storage_index() == script_value::TYPEID_INT && s->type_info_raw() == engine_->get_type_info_int() && s->get_engine() == engine_) { JAI_TYPED_LAND(0); s->unchecked_set_int_payload(v); return true; }
		JAI_TYPED_LAND(1);
#ifdef JAISCRIPT_VM_PROFILE
		++profile_temp_miss_[!s ? 0 : s->raw_storage_index() != script_value::TYPEID_INT ? 1 : 2];
#endif
		frame_slot_set(f, slot, script_value(v, engine_));
		return true;
	case k_typed_dst_decl: {
		type_info* stamp = f.code->typed_sites[f.ip].stamp;
		if (s && s->raw_storage_index() == script_value::TYPEID_INT && s->get_type_info().get() == stamp) { JAI_TYPED_LAND(2); s->unchecked_set_int_payload(v); return true; }
		JAI_TYPED_LAND(3);
		script_value nv(v, engine_);
		if (stamp) { nv.set_type_info(type_info_ptr{stamp}); }
		frame_slot_set(f, slot, std::move(nv));
		return true;
	}
	case k_typed_dst_store: {
		// store_popped_value's proven-identity slot store: no reference in the slot, the
		// slot's declared class agrees (the same-type proof), then `*slot = value`
		if (!s || s->is_reference()) { JAI_TYPED_LAND(4); return false; }
		const type_info* slot_type = s->type_info_raw();
		// the slot already holds this engine's canonical int: the payload is the whole store (the
		// path below writes the same payload and re-stamps the same type; it never touches the engine)
		if (s->raw_storage_index() == script_value::TYPEID_INT && slot_type == engine_->get_type_info_int()) { JAI_TYPED_INPLACE(0); s->unchecked_set_int_payload(v); return true; }
		if (slot_type && slot_type->base_type != script_value_type::jai_int_type) { JAI_TYPED_LAND(4); return false; }
		script_value nv(v, engine_);
		if (s->raw_storage_index() == script_value::TYPEID_INT) { JAI_TYPED_LAND(5); s->unchecked_set_int_payload(v); s->set_type_info(nv.get_type_info()); return true; }
		JAI_TYPED_LAND(6);
		frame_slot_set(f, slot, std::move(nv));
		return true;
	}
	case k_typed_dst_compound:
		// exec_compound_fused's int row: the slot already holds an int (the operand read
		// proved it), the payload updates in place, tag untouched
		if (!s || s->raw_storage_index() != script_value::TYPEID_INT) { JAI_TYPED_LAND(8); return false; }
		JAI_TYPED_LAND(7);
		s->unchecked_set_int_payload(v);
		return true;
	default:
		return false;
	}
}

bool vm_backend::typed_land_float(frame& f, uint32_t a, script_float v) {
	const uint32_t slot = a & k_typed_slot_mask;
	script_value* s = frame_slot(f, slot);   // null = a window slot not yet live: the landing grows it, a store does not
	switch (a >> k_typed_dst_shift) {
	case k_typed_dst_temp:
		if (s && s->raw_storage_index() == script_value::TYPEID_FLOAT && s->type_info_raw() == engine_->get_type_info_float() && s->get_engine() == engine_) { JAI_TYPED_LAND(0); s->unchecked_set_float_payload(v); return true; }
		JAI_TYPED_LAND(1);
#ifdef JAISCRIPT_VM_PROFILE
		++profile_temp_miss_[!s ? 0 : s->raw_storage_index() != script_value::TYPEID_FLOAT ? 1 : 2];
#endif
		frame_slot_set(f, slot, script_value(v, engine_));
		return true;
	case k_typed_dst_decl: {
		type_info* stamp = f.code->typed_sites[f.ip].stamp;
		if (s && s->raw_storage_index() == script_value::TYPEID_FLOAT && s->get_type_info().get() == stamp) { JAI_TYPED_LAND(2); s->unchecked_set_float_payload(v); return true; }
		JAI_TYPED_LAND(3);
		script_value nv(v, engine_);
		if (stamp) { nv.set_type_info(type_info_ptr{stamp}); }
		frame_slot_set(f, slot, std::move(nv));
		return true;
	}
	case k_typed_dst_store: {
		if (!s || s->is_reference()) { JAI_TYPED_LAND(4); return false; }
		const type_info* slot_type = s->type_info_raw();
		if (s->raw_storage_index() == script_value::TYPEID_FLOAT && slot_type == engine_->get_type_info_float()) { JAI_TYPED_INPLACE(0); s->unchecked_set_float_payload(v); return true; }
		if (slot_type && slot_type->base_type != script_value_type::jai_float_type) { JAI_TYPED_LAND(4); return false; }
		script_value nv(v, engine_);
		if (s->raw_storage_index() == script_value::TYPEID_FLOAT) { JAI_TYPED_LAND(5); s->unchecked_set_float_payload(v); s->set_type_info(nv.get_type_info()); return true; }
		JAI_TYPED_LAND(6);
		frame_slot_set(f, slot, std::move(nv));
		return true;
	}
	default:
		return false;
	}
}

// The instruction a typed op was lowered from, run as the dispatcher would run it
op_status vm_backend::exec_typed_fallback(frame& f, const vm_instruction& orig) {
	JAI_TYPED_LAND(9);
	switch (orig.op) {
	case opcode::op_binary_fused_temp: {
		temp_lane_sink lane{this, f, orig.b};
		return binary_fused_compute(f, orig.a, lane);
	}
	case opcode::op_binary_fused_decl: return exec_binary_fused_decl(f, orig);
	case opcode::op_binary_fused_store: return exec_binary_fused_store(f, orig);
	case opcode::op_fused_cmp_jump: return exec_fused_cmp_jump(f, orig);
	case opcode::op_compound_fused: return exec_compound_fused(f, orig);
	default:
		return raise_(make_error_code(runtime_error_code::unsupported_operation), "typed op without an origin (internal)");
	}
}

JAI_FORCEINLINE bool vm_backend::typed_arith_fast(frame& f, const vm_instruction& ins) {
	// a registered global numeric operator turns every numeric row over to the general
	// path, which consults the operator table (the fused kernels' gate, kept here)
	const script_value* lp = has_custom_numeric_ops_ ? nullptr : frame_slot(f, ins.b);
	const script_value* rp = lp ? typed_right(f, ins.c) : nullptr;
	if (rp) {
		if (ins.op <= opcode::op_t_shr_ii) {
			if (lp->raw_storage_index() == script_value::TYPEID_INT && rp->raw_storage_index() == script_value::TYPEID_INT) {
				const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
				script_int r;
				bool ok = true;
				switch (ins.op) {
				case opcode::op_t_add_ii: ok = ints::try_add(a, b, r); break;
				case opcode::op_t_sub_ii: ok = ints::try_sub(a, b, r); break;
				case opcode::op_t_mul_ii: ok = ints::try_mul(a, b, r); break;
				case opcode::op_t_div_ii: ok = b != 0 && ints::try_div(a, b, r); break;
				case opcode::op_t_mod_ii: ok = b != 0; if (ok) { r = ints::mod(a, b); } break;
				case opcode::op_t_and_ii: r = a & b; break;
				case opcode::op_t_or_ii: r = a | b; break;
				case opcode::op_t_xor_ii: r = a ^ b; break;
				case opcode::op_t_shl_ii: ok = b >= 0 && b <= 63; if (ok) { r = a << b; } break;
				case opcode::op_t_shr_ii: ok = b >= 0 && b <= 63; if (ok) { r = a >> b; } break;
				default: ok = false; break;
				}
				if (ok && typed_land_int(f, ins.a, r)) return true;
			}
		} else if (lp->raw_storage_index() == script_value::TYPEID_FLOAT && rp->raw_storage_index() == script_value::TYPEID_FLOAT) {
			const script_float a = lp->unchecked_as_float(), b = rp->unchecked_as_float();
			script_float r;
			bool ok = true;
			switch (ins.op) {
			case opcode::op_t_add_ff: r = a + b; break;
			case opcode::op_t_sub_ff: r = a - b; break;
			case opcode::op_t_mul_ff: r = a * b; break;
			case opcode::op_t_div_ff: ok = b != 0.0; if (ok) { r = a / b; } break;
			case opcode::op_t_mod_ff: ok = b != 0.0; if (ok) { r = std::fmod(a, b); } break;
			default: ok = false; break;
			}
			if (ok && typed_land_float(f, ins.a, r)) return true;
		}
	}
	return false;
}

op_status vm_backend::exec_typed_arith(frame& f, const vm_instruction& ins) {
	if (typed_arith_fast(f, ins)) return {};
	return exec_typed_fallback(f, f.code->typed_sites[f.ip].orig);
}

JAI_FORCEINLINE int vm_backend::typed_cmp_jump_fast(frame& f, const vm_instruction& ins) {
	const script_value* lp = has_custom_numeric_ops_ ? nullptr : frame_slot(f, ins.a);
	const script_value* rp = lp ? typed_right(f, ins.b) : nullptr;
	if (rp) {
		bool truthy;
		if (ins.op <= opcode::op_t_jne_ii) {
			if (lp->raw_storage_index() == script_value::TYPEID_INT && rp->raw_storage_index() == script_value::TYPEID_INT) {
				const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
				switch (ins.op) {
				case opcode::op_t_jlt_ii: truthy = a < b; break;
				case opcode::op_t_jle_ii: truthy = a <= b; break;
				case opcode::op_t_jgt_ii: truthy = a > b; break;
				case opcode::op_t_jge_ii: truthy = a >= b; break;
				case opcode::op_t_jeq_ii: truthy = a == b; break;
				default: truthy = a != b; break;
				}
				f.ip = truthy ? f.ip + 1 : ins.c;
				return truthy ? 1 : 2;
			}
		} else if (lp->raw_storage_index() == script_value::TYPEID_FLOAT && rp->raw_storage_index() == script_value::TYPEID_FLOAT) {
			const script_float a = lp->unchecked_as_float(), b = rp->unchecked_as_float();
			switch (ins.op) {
			case opcode::op_t_jlt_ff: truthy = a < b; break;
			case opcode::op_t_jle_ff: truthy = a <= b; break;
			case opcode::op_t_jgt_ff: truthy = a > b; break;
			case opcode::op_t_jge_ff: truthy = a >= b; break;
			case opcode::op_t_jeq_ff: truthy = a == b; break;
			default: truthy = a != b; break;
			}
			f.ip = truthy ? f.ip + 1 : ins.c;
			return truthy ? 1 : 2;
		}
	}
	return -1;
}

op_status vm_backend::exec_typed_cmp_jump(frame& f, const vm_instruction& ins) {
	if (typed_cmp_jump_fast(f, ins) > 0) return {};
	return exec_fused_cmp_jump(f, f.code->typed_sites[f.ip].orig);
}

// `T x = y;` as one op: exec_decl_var's fast path over the slot LOAD would have pushed
op_status vm_backend::exec_typed_decl_mov(frame& f, const vm_instruction& ins) {
	const chunk::typed_site& site = f.code->typed_sites[f.ip];
	const script_value* src = frame_slot(f, ins.b);
	if (src && src->raw_storage_index() == script_value::TYPEID_REFERENCE && !lane_deref_silent(*src)) { src = nullptr; }
	if (src) {
		const script_value* seen = src;
		if (seen->raw_storage_index() == script_value::TYPEID_REFERENCE) { seen = &seen->deref(); }   // LOAD pushes the referent
		const size_t vi = seen->raw_storage_index();
		if (variable_decl::identity_payload(vi, site.stamp)) {
			script_value v(*seen);
			if (site.stamp) { v.set_type_info(type_info_ptr{site.stamp}); }
			frame_slot_set(f, ins.a, std::move(v));
			return {};
		}
		if (variable_decl::widening_payload(vi, site.stamp)) {
			script_value w(static_cast<script_float>(seen->unchecked_as_int()), engine_);
			w.set_type_info(type_info_ptr{site.stamp});
			frame_slot_set(f, ins.a, std::move(w));
			return {};
		}
	}
	VM_TRY(exec_load(f, site.orig));
	return exec_decl_var(f, site.orig2);
}

op_status vm_backend::exec_typed_decl_const(frame& f, const vm_instruction& ins) {
	const chunk::typed_site& site = f.code->typed_sites[f.ip];
	script_value v = materialize_constant(*f.code, ins.b);
	const size_t vi = v.raw_storage_index();
	if (variable_decl::identity_payload(vi, site.stamp)) {
		if (site.stamp) { v.set_type_info(type_info_ptr{site.stamp}); }
		frame_slot_set(f, ins.a, std::move(v));
		return {};
	}
	if (variable_decl::widening_payload(vi, site.stamp)) {
		script_value w(static_cast<script_float>(v.unchecked_as_int()), engine_);
		w.set_type_info(type_info_ptr{site.stamp});
		frame_slot_set(f, ins.a, std::move(w));
		return {};
	}
	stack_.push_back(std::move(v));
	return exec_decl_var(f, site.orig2);
}

// `x = y;` as one op: store_popped_value's proven-identity slot store over the LOAD's value
op_status vm_backend::exec_typed_store_mov(frame& f, const vm_instruction& ins) {
	const chunk::typed_site& site = f.code->typed_sites[f.ip];
	script_value* dst = frame_slot(f, ins.a);
	const script_value* src = dst ? frame_slot(f, ins.b) : nullptr;
	if (src && src->raw_storage_index() == script_value::TYPEID_REFERENCE && !lane_deref_silent(*src)) { src = nullptr; }
	if (src && !dst->is_reference()) {
		const script_value& v = src->deref();
		const size_t vi = v.raw_storage_index();
		if (vi == script_value::TYPEID_INT || vi == script_value::TYPEID_FLOAT || vi == script_value::TYPEID_BOOL ||
		    vi == script_value::TYPEID_CHAR || vi == script_value::TYPEID_STRING) {
			const type_info* slot_type = dst->type_info_raw();
			if (!slot_type || detail::same_type_payload(v, slot_type)) {
				JAI_TYPED_INPLACE(2);
				*dst = v;
				return {};
			}
		}
	}
	JAI_TYPED_INPLACE(3);
	VM_TRY(exec_load(f, site.orig));
	VM_TRY(exec_store(f, site.orig2));
	if (site.orig2.c & store_flag_no_result) { stack_.pop_back(); }
	return {};
}

op_status vm_backend::exec_typed_store_const(frame& f, const vm_instruction& ins) {
	const chunk::typed_site& site = f.code->typed_sites[f.ip];
	script_value v = materialize_constant(*f.code, ins.b);
	if (script_value* dst = frame_slot(f, ins.a); dst && !dst->is_reference()) {
		const type_info* slot_type = dst->type_info_raw();
		if (!slot_type || detail::same_type_payload(v, slot_type)) {
			JAI_TYPED_INPLACE(2);
			*dst = std::move(v);
			return {};
		}
	}
	JAI_TYPED_INPLACE(3);
	stack_.push_back(std::move(v));
	VM_TRY(exec_store(f, site.orig2));
	if (site.orig2.c & store_flag_no_result) { stack_.pop_back(); }
	return {};
}

bool vm_backend::typed_decl_read(frame& f, const fused_index_proto& p, uint32_t) {
	// The read's lvalue/transient flags do not matter here: for the payloads this lane lands
	// (raw scalars, strings) the kernel's element reference is consumed by the same decl one
	// op later as exactly this copy; every other element falls to the kernel untouched
	const bool folded = p.index_binary != k_invalid_u32;
	const script_value* cp = fast_operand(f, p.container, folded ? f.ip * 3 + 2 : f.ip * 3);
	if (!cp) return false;
	if (cp->raw_storage_index() == script_value::TYPEID_REFERENCE) {   // a var& parameter
		if (!lane_deref_silent(*cp)) return false;   // a getter-backed reference: the kernel reads it exactly once
		cp = &cp->deref();
	}
	bool borrowed = false;
	const script_array* node = lane_element_source(*cp, borrowed);
	if (!node) return false;
	script_int index;
	if (!typed_fold_index(f, p, index)) return false;
	if (index < 0 || static_cast<size_t>(index) >= node->size()) return false;   // the kernel raises
	return typed_decl_land_element(f, p.decl_slot, p.decl_expect, p.decl_type, *node, static_cast<size_t>(index), borrowed);
}

bool vm_backend::typed_index_store(frame& f, const fused_index_proto& p, bool no_result) {
#ifdef JAISCRIPT_VM_PROFILE
	++profile_typed_cont_src_[p.container.slot != k_invalid_u32 ? 2 : 3];
#endif
	const bool folded = p.index_binary != k_invalid_u32;
	const bool value_operand = p.store_value.const_index != k_invalid_u32 || p.store_value.symbol != k_invalid_u32;
	// the value first (the kernel resolves it first, so an undefined name keeps its position)
	const script_value* vp;
	if (value_operand) {
		vp = fast_operand(f, p.store_value, folded ? SIZE_MAX : f.ip * 3 + 2);
		if (!vp) return false;
	} else {
		if (stack_.empty()) return false;
		vp = &stack_.back();
	}
	if (vp->raw_storage_index() == script_value::TYPEID_REFERENCE && !lane_deref_silent(*vp)) return false;
	const script_value& v = vp->deref();
	const size_t vi = v.raw_storage_index();
	if (vi != script_value::TYPEID_INT && vi != script_value::TYPEID_FLOAT) return false;
	const script_value* cp = fast_operand(f, p.container, folded ? f.ip * 3 + 2 : f.ip * 3);
	if (!cp) return false;
	if (cp->raw_storage_index() == script_value::TYPEID_REFERENCE) {   // a var& parameter
		if (!lane_deref_silent(*cp)) return false;
		cp = &cp->deref();
	}
	if (cp->raw_storage_index() != script_value::TYPEID_ARRAY) return false;
	script_int index;
	if (!typed_fold_index(f, p, index)) return false;
	script_array* node = const_cast<script_value&>(*cp).unchecked_get_array_storage().get();
	if (!node || index < 0 || static_cast<size_t>(index) >= node->size()) return false;   // the kernel raises
	const size_t at = static_cast<size_t>(index);
	switch (node->kind()) {
	case script_array::kind_t::i64:
		// set()'s exact cast; a float outside the int range runs the kernel's checked text
		if (vi == script_value::TYPEID_FLOAT && !detail::float_fits_script_int(v.unchecked_as_float())) return false;
		node->ints()[at] = vi == script_value::TYPEID_FLOAT ? static_cast<script_int>(v.unchecked_as_float()) : v.unchecked_as_int();
		break;
	case script_array::kind_t::f64:
		node->floats()[at] = vi == script_value::TYPEID_INT ? static_cast<script_float>(v.unchecked_as_int()) : v.unchecked_as_float();
		break;
	case script_array::kind_t::hetero: {
		// the kernel's plain-scalar in-place row: no element type, same storage kind
		const type_info* ti = cp->get_type_info().get();
		if (ti && ti->element_type().get()) return false;
		script_value& target = node->values()[at];
		if (target.raw_storage_index() != vi) return false;
		if (vi == script_value::TYPEID_INT) { target.unchecked_set_int_payload(v.unchecked_as_int()); }
		else { target.unchecked_set_float_payload(v.unchecked_as_float()); }
		target.set_type_info(v.get_type_info());
		target.set_engine(v.get_engine());
		break;
	}
	default:
		return false;
	}
	// the kernel's result discipline: an operand value pushes when a result is wanted; a
	// stack value pops when it is not (when it is, the pending value IS the result)
	if (value_operand) {
		if (!no_result) { stack_.push_back(assignment_result_used(f) ? node->get(at, engine_) : *vp); }   // the stored element is the expression's value
	} else if (no_result) {
		stack_.pop_back();
	} else if (assignment_result_used(f)) {
		stack_.back() = node->get(at, engine_);   // the pending rhs becomes the stored element
	}
	return true;
}

op_status vm_backend::exec_index_fused_decl(frame& f, const vm_instruction& ins) {
	const fused_index_proto& p = f.code->fused_index_protos[ins.a];
	if (p.typed_lane && !has_custom_numeric_ops_ && f.locals && !f.top_level) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_typed_decl_read_[0];
#endif
		if (typed_decl_read(f, p, ins.b)) {
#ifdef JAISCRIPT_VM_PROFILE
			++profile_typed_decl_read_[1];
#endif
			return {};
		}
	}
	if (p.decl_slot != k_invalid_u32 && f.locals && !f.top_level) [[likely]] {
		// Raw-capable lane sink: typed-array payloads arrive as raw int64/double and
		// land in the slot with exactly ONE value mint; boxed arrivals keep the
		// expect-check + general-landing split
		struct decl_lane_sink {
			vm_backend* vm;
			frame& fr;
			const fused_index_proto& proto;
			bool landed = false;
			// Untyped-element raw shortcut admission: land() overwrites the tag with
			// decl_type, so dropping the element's own tag is unobservable ONLY when
			// decl_type is set (null decl_type keeps the incoming tag — must copy)
			bool raw_tag_safe() const { return proto.decl_type != nullptr; }
			void land(script_value&& v) {
				if (proto.decl_type) {
					v.set_type_info(type_info_ptr{proto.decl_type});
				}
				vm->frame_slot_set(fr, proto.decl_slot, std::move(v));
				landed = true;
			}
			void operator()(script_value&& v) {
				const size_t vi = v.raw_storage_index();
				if (vi == proto.decl_expect ||
				    (proto.decl_expect == 0xFF &&
				     (vi == script_value::TYPEID_INT || vi == script_value::TYPEID_FLOAT ||
				      vi == script_value::TYPEID_BOOL || vi == script_value::TYPEID_CHAR ||
				      vi == script_value::TYPEID_STRING))) {
					land(std::move(v));
				} else {
					vm->stack_.push_back(std::move(v));   // general landing below
				}
			}
			void raw_int(script_int iv) {
				if (proto.decl_expect == static_cast<uint8_t>(script_value::TYPEID_INT) ||
				    proto.decl_expect == 0xFF) {
					// Live slot already holding a same-tagged int: payload write in place
					// (no value mint, no tag refcounts, no old-value dtor — byte-identical
					// post-state to the mint+land path)
					if (script_value* slot = vm->frame_slot(fr, proto.decl_slot)) {
						if (slot->raw_storage_index() == script_value::TYPEID_INT &&
						    slot->get_type_info().get() == proto.decl_type) {
							slot->unchecked_set_int_payload(iv);
							landed = true;
							return;
						}
					}
					land(script_value(iv, vm->engine_));
				} else if (proto.decl_expect == static_cast<uint8_t>(script_value::TYPEID_FLOAT)) {
					raw_float(static_cast<script_float>(iv));   // the decl's int -> float row
				} else {
					(*this)(script_value(iv, vm->engine_));   // e.g. bool decl: op_decl_var converts
				}
			}
			void raw_float(script_float fv) {
				if (proto.decl_expect == static_cast<uint8_t>(script_value::TYPEID_FLOAT) ||
				    proto.decl_expect == 0xFF) {
					if (script_value* slot = vm->frame_slot(fr, proto.decl_slot)) {
						if (slot->raw_storage_index() == script_value::TYPEID_FLOAT &&
						    slot->get_type_info().get() == proto.decl_type) {
							slot->unchecked_set_float_payload(fv);
							landed = true;
							return;
						}
					}
					land(script_value(fv, vm->engine_));
				} else {
					(*this)(script_value(fv, vm->engine_));
				}
			}
		} sink{this, f, p};
		VM_TRY(index_fused_read<false>(f, ins, sink));
		if (sink.landed || is_unwinding_) {
			return {};   // unwound: parity states stand, the decl never runs
		}
		const vm_instruction decl_ins{opcode::op_decl_var, ins.c, 1, 1};
		return exec_decl_var(f, decl_ins);
	}
	std::optional<script_value> computed;
	VM_TRY(index_fused_read<false>(f, ins, [&](script_value&& v) { computed.emplace(std::move(v)); }));
	if (!computed) [[unlikely]] {
		return {};   // the read unwound; the decl never runs (pair parity)
	}
	stack_.push_back(std::move(*computed));
	const vm_instruction decl_ins{opcode::op_decl_var, ins.c, 1, 1};
	return exec_decl_var(f, decl_ins);
}

// Fused a[i] = v: container+index resolve as operands; the value rides the stack, or —
// statement position (ins.c=1, result push elided) — as an ident/literal operand. The
// committed paths mirror exec_index_store's fast paths verbatim; every other shape
// pushes the operands above the value and replays the unfused sequence.
op_status vm_backend::exec_index_store_fused(frame& f, const vm_instruction& ins) {
	const fused_index_proto& p = f.code->fused_index_protos[ins.a];
	const bool no_result = ins.c != 0;
	if (p.typed_lane && !has_custom_numeric_ops_) {
#ifdef JAISCRIPT_VM_PROFILE
		++profile_typed_store_[0];
#endif
		if (typed_index_store(f, p, no_result)) {
#ifdef JAISCRIPT_VM_PROFILE
			++profile_typed_store_[1];
#endif
			return {};
		}
	}
	std::optional<script_value> cscratch, mscratch, iscratch, vscratch;
	const bool value_operand = p.store_value.const_index != k_invalid_u32 ||
	                           p.store_value.symbol != k_invalid_u32;
	const script_value* value_ptr = nullptr;
	if (value_operand) {
		// Value resolves FIRST — the unfused sequence evaluated it before the container,
		// so undefined-variable errors keep their position
		if (p.store_value.const_index != k_invalid_u32) {
			vscratch.emplace(materialize_constant(*f.code, p.store_value.const_index));
			value_ptr = &*vscratch;
		} else {
			const script_value* resolved = fused_ident_value(f, p.store_value, vscratch,
			                                  p.index_binary != k_invalid_u32 ? SIZE_MAX : f.ip * 3 + 2);
			if (!resolved) return op_status::failed;
			value_ptr = resolved;
		}
		// Member getters / boxed index steps can run user code before the value is
		// consumed — and value_ptr may target a frame slot, which stack growth moves
		// (invariant 2b). Snapshot exactly like the unfused push did. Pure ident/lit
		// index sites (the hot shape) keep the zero-copy pointer.
		if (!vscratch && (p.container.member_node != k_invalid_u32 || p.index_binary != k_invalid_u32)) {
			vscratch.emplace(*value_ptr);
			value_ptr = &*vscratch;
		}
	}
	const script_value* container_ptr;
	if (p.container.const_index != k_invalid_u32) {
		container_ptr = &f.code->constants[p.container.const_index];
	} else {
		auto resolved = fused_ident_value(f, p.container, cscratch,
		                                  p.index_binary != k_invalid_u32 ? f.ip * 3 + 2 : f.ip * 3);
		if (!resolved) return op_status::failed;
		container_ptr = resolved;
		if (p.container.member_node != k_invalid_u32) {
			VM_TRY(fused_member_container(f, static_cast<size_t>(&ins - f.code->code.data()),
			                              *container_ptr, p.container.member_node, mscratch, container_ptr));
			if (is_unwinding_) [[unlikely]] {
				// pair parity: [value, GET_MEMBER's out] stay pushed, INDEX_STORE never runs
				if (value_operand) { stack_.push_back(*value_ptr); }
				stack_.push_back(*container_ptr);
				return {};
			}
		}
	}
	const script_value* index_ptr = nullptr;
	std::optional<script_value> computed_index, csnap;
	bool have_raw_index = false;
	script_int raw_index = 0;
	if (p.index_binary != k_invalid_u32) {
		// Same purity/snapshot/raw-int/chain discipline as exec_index_fused (the value
		// sits below on the stack; the boxed tail pushes above it)
		const fused_binary_proto& bp = f.code->fused_binary_protos[p.index_binary];
		const token_type bop = static_cast<token_type>(bp.op);
		bool pure = false;
		if (!has_custom_numeric_ops_ && is_numeric_binary_op(bop)) {
			const script_value* pl = fused_cmp_operand(f, bp.left, f.ip * 3);
			const script_value* pr = pl ? fused_cmp_operand(f, bp.right, f.ip * 3 + 1) : nullptr;
			if (pr) {
				const size_t li = pl->raw_storage_index(), ri = pr->raw_storage_index();
				pure = (li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
				       (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT);
				if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
					VM_TRY(raw_int_arith(bop, pl->unchecked_as_int(), pr->unchecked_as_int(),
					                     raw_index, have_raw_index));
				}
			}
		}
		const auto& chain = p.index_chain_ext;
		size_t chain_pos = 0;
		if (have_raw_index && !chain.empty()) {
			std::optional<script_value> rscratch;
			while (chain_pos < chain.size()) {
				const fused_binary_proto& sp = f.code->fused_binary_protos[chain[chain_pos]];
				const script_value* rp2;
				if (sp.right.const_index != k_invalid_u32) {
					rp2 = &f.code->constants[sp.right.const_index];
				} else {
					rscratch.reset();
					const script_value* resolved = fused_ident_value(f, sp.right, rscratch, SIZE_MAX);
					if (!resolved) return op_status::failed;
					rp2 = resolved;
				}
				if (rp2->raw_storage_index() != script_value::TYPEID_INT) break;
				bool step_handled = false;
				VM_TRY(raw_int_arith(static_cast<token_type>(sp.op), raw_index,
				                     rp2->unchecked_as_int(), raw_index, step_handled));
				if (!step_handled) break;
				++chain_pos;
			}
		}
		if (!have_raw_index) {
			if (!pure) {
				csnap.emplace(*container_ptr);
				container_ptr = &*csnap;
			}
			VM_TRY(binary_fused_compute(f, p.index_binary,
			                            [&](script_value&& v) { computed_index.emplace(std::move(v)); }));
			if (is_unwinding_) [[unlikely]] {
				if (value_operand) { stack_.push_back(*value_ptr); }
				stack_.push_back(*container_ptr);
				if (computed_index) {
					stack_.push_back(std::move(*computed_index));
				}
				return {};
			}
			index_ptr = &*computed_index;
		}
		if (chain_pos < chain.size() || (!chain.empty() && !have_raw_index)) {
			// Boxed chain tail above the pending value: [value, container, acc] then one
			// op_binary per remaining step, finished by the real op_index_store
			if (value_operand) { stack_.push_back(*value_ptr); }
			if (p.container.const_index != k_invalid_u32) { stack_.push_back(materialize_constant(*f.code, p.container.const_index)); }
			else { stack_.push_back(*container_ptr); }
			if (computed_index) { stack_.push_back(std::move(*computed_index)); }
			else { stack_.push_back(script_value(raw_index, engine_)); }
			for (size_t i = chain_pos; i < chain.size(); ++i) {
				const fused_binary_proto& sp = f.code->fused_binary_protos[chain[i]];
				if (sp.right.const_index != k_invalid_u32) {
					stack_.push_back(materialize_constant(*f.code, sp.right.const_index));
				} else {
					std::optional<script_value> rs;
					const script_value* resolved = fused_ident_value(f, sp.right, rs, SIZE_MAX);
					if (!resolved) return op_status::failed;
					stack_.push_back(*resolved);
				}
				const vm_instruction bin_ins{opcode::op_binary, static_cast<uint32_t>(sp.op), binary_shape_none, 0};
				VM_TRY(exec_binary(f, bin_ins));
				if (is_unwinding_) [[unlikely]] {
					return {};
				}
			}
			const vm_instruction store_ins{opcode::op_index_store, ins.b, 0, 0};
			const op_status st = exec_index_store(f, store_ins);
			if (st == op_status::failed) { return st; }
			if (no_result && !is_unwinding_) { stack_.pop_back(); }
			return st;
		}
	} else if (p.index.const_index != k_invalid_u32) {
		index_ptr = &f.code->constants[p.index.const_index];
	} else {
		auto resolved = fused_ident_value(f, p.index, iscratch, f.ip * 3 + 1);
		if (!resolved) return op_status::failed;
		index_ptr = resolved;
	}

	script_value& container = const_cast<script_value&>(*container_ptr);
	bool index_is_int = have_raw_index;
	script_int index_int = raw_index;
	if (!have_raw_index) {
		const script_value& index_v = index_ptr->deref();
		index_is_int = index_v.is_int();
		if (index_is_int) { index_int = index_v.unchecked_as_int(); }
	}
	if ((value_operand || !stack_.empty()) && container.raw_storage_index() == script_value::TYPEID_ARRAY && index_is_int) {
		const script_int index = index_int;
		script_array* node = container.unchecked_get_array_storage().get();
		// TYPED raw store (mirrors exec_index_store): numeric rhs coerces straight into
		// the buffer with set()'s exact casts — no handle copy, no value copy (the raw
		// node stands in for the strong_ptr because no user code runs here); non-numeric
		// rhs replays for the exact mismatch error.
		if (node && (node->kind() == script_array::kind_t::i64 || node->kind() == script_array::kind_t::f64) &&
		    index >= 0 && index < static_cast<script_int>(node->size())) {
			const script_value& rhs_peek = value_operand ? value_ptr->deref() : stack_.back().deref();
			const size_t ri = rhs_peek.raw_storage_index();
			if (ri == script_value::TYPEID_INT || (ri == script_value::TYPEID_FLOAT &&
			    (node->kind() == script_array::kind_t::f64 || detail::float_fits_script_int(rhs_peek.unchecked_as_float())))) {
				if (node->kind() == script_array::kind_t::i64) {
					node->ints()[static_cast<size_t>(index)] = ri == script_value::TYPEID_FLOAT
						? static_cast<script_int>(rhs_peek.unchecked_as_float()) : rhs_peek.unchecked_as_int();
				} else {
					node->floats()[static_cast<size_t>(index)] = ri == script_value::TYPEID_INT
						? static_cast<script_float>(rhs_peek.unchecked_as_int()) : rhs_peek.unchecked_as_float();
				}
				if (value_operand) {
					if (!no_result) { stack_.push_back(assignment_result_used(f) ? node->get(static_cast<size_t>(index), engine_) : *value_ptr); }   // the stored element is the expression's value
				} else {
					script_value value = std::move(stack_.back());
					stack_.pop_back();
					if (!no_result) { stack_.push_back(assignment_result_used(f) ? node->get(static_cast<size_t>(index), engine_) : std::move(value)); }
				}
				return {};
			}
		}
		if (node && !node->is_typed() && index >= 0 && index < static_cast<script_int>(node->size())) {
			auto container_type_info = container.get_type_info();
			type_info_ptr element_type = container_type_info ? container_type_info->element_type() : nullptr;
			// Plain-scalar in-place lane: no element type -> no conversion -> NO user code
			// can run, so the handle pin and the value pre-copy below are dead hedges.
			// Same-storage-kind scalar rhs writes the element payload in place and carries
			// tag+engine exactly like clone_for_assignment's copy did (identical post-state).
			if (!element_type) {
				const script_value& rhs_peek = value_operand ? value_ptr->deref() : stack_.back().deref();
				const size_t ri = rhs_peek.raw_storage_index();
				if (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT) {
					script_value& target = node->values()[static_cast<size_t>(index)];
					if (target.raw_storage_index() == ri) {
						if (ri == script_value::TYPEID_INT) {
							target.unchecked_set_int_payload(rhs_peek.unchecked_as_int());
						} else {
							target.unchecked_set_float_payload(rhs_peek.unchecked_as_float());
						}
						target.set_type_info(rhs_peek.get_type_info());
						target.set_engine(rhs_peek.get_engine());
						if (value_operand) {
							if (!no_result) { stack_.push_back(*value_ptr); }
						} else if (no_result) {
							stack_.pop_back();
						}   // stack rhs + result wanted: the pending value IS the result
						return {};
					}
				}
			}
			// The HANDLE COPY pins the node: element-type conversion can run user code,
			// which could reassign the container variable out from under a raw pointer
			strong_ptr<script_array> storage = container.unchecked_get_array_storage();
			// Element-type conversion can run user code: pop/copy the value into a local
			// first (invariant 2b — value_ptr may target a frame slot the stack can move)
			script_value value = value_operand ? script_value(*value_ptr) : std::move(stack_.back());
			if (!value_operand) { stack_.pop_back(); }
			script_value* target_ptr = &storage->values()[static_cast<size_t>(index)];
			if (element_type) {
				if (!vm_is_element_type_compatible(value, element_type, *target_ptr)) {
					std::string value_type = vm_value_type_name(value);
					std::string expected_type = vm_type_info_name(element_type);
					uint64_t value_type_id = symbolizer_->intern(value_type);
					uint64_t expected_type_id = symbolizer_->intern(expected_type);
					return raise_(
						make_error_code(runtime_error_code::array_element_type_mismatch),
						"Cannot assign '{0}' to element of type '{1}'",
						value_type_id, expected_type_id);
				}
				auto converted_result = vm_convert_array_element(engine_, value, element_type);
				if (!converted_result) { return raise_from(converted_result); }
				script_value converted = std::move(converted_result.value());
				*target_ptr = std::move(converted);
			} else {
				// No element type constraint - values deep-copy, shared_ptr handles share
				*target_ptr = clone_for_assignment(value);
			}
			if (!no_result) { stack_.push_back(assignment_result_used(f) ? clone_for_assignment(*target_ptr) : std::move(value)); }   // a copy of the stored element is the expression's value
			return {};
		}
	}
	if (value_operand) { stack_.push_back(*value_ptr); }
	if (p.container.const_index != k_invalid_u32) { stack_.push_back(materialize_constant(*f.code, p.container.const_index)); }
	else { stack_.push_back(*container_ptr); }
	if (have_raw_index) { stack_.push_back(script_value(raw_index, engine_)); }
	else if (computed_index) { stack_.push_back(std::move(*computed_index)); }
	else if (p.index.const_index != k_invalid_u32) { stack_.push_back(materialize_constant(*f.code, p.index.const_index)); }
	else { stack_.push_back(*index_ptr); }
	const vm_instruction store_ins{opcode::op_index_store, ins.b, 0, 0};
	const op_status st = exec_index_store(f, store_ins);
	if (st == op_status::failed) { return st; }
	if (no_result && !is_unwinding_) { stack_.pop_back(); }
	return st;
}

// Fused a[i] op= v: the numeric plain-array shape (int/float element, raw int/float rhs,
// no registered operator override) reads, computes, and writes in place. Everything else
// replays INDEX(shape)+INDEX_COMPOUND byte-identically (string concat routing, S8 bound
// decode, object operator methods, rvalue-target errors).
op_status vm_backend::exec_index_compound_fused(frame& f, const vm_instruction& ins) {
	// stack: [container, index, rhs]. Numeric fast path: all peeks are read-only and
	// evaluate_arithmetic on raw int/float runs no user code, so in-place refs are safe.
	if ((ins.b & index_flag_lvalue_shape) != 0 && stack_.size() >= 3 &&
	    (ins.a & compound_flag_old_result) == 0 &&
	    !(operator_table_ && operator_table_->any())) {
		const script_value& rhs = stack_.back();
		const size_t rhs_idx = rhs.raw_storage_index();
		script_value& container = stack_[stack_.size() - 3].deref();
		const script_value& index_v = stack_[stack_.size() - 2].deref();
		if ((rhs_idx == script_value::TYPEID_INT || rhs_idx == script_value::TYPEID_FLOAT) &&
		    container.raw_storage_index() == script_value::TYPEID_ARRAY && index_v.is_int()) {
			const script_int index = index_v.unchecked_as_int();
			auto storage = container.get_array_storage();
			// NUMERIC raw load-op-store: int/float buffers coerce the (always numeric)
			// result back to the element kind on store; byte/string kinds take the
			// enforcing path
			if (storage && (storage->kind() == script_array::kind_t::i64 || storage->kind() == script_array::kind_t::f64) &&
			    index >= 0 && index < static_cast<script_int>(storage->size())) {
				token_type op;
				switch (ins.a & compound_kind_mask) {
					case compound_plus: op = token_type::plus; break;
					case compound_minus: op = token_type::minus; break;
					case compound_star: op = token_type::star; break;
					case compound_slash: op = token_type::slash; break;
					case compound_percent: op = token_type::percent; break;
					default: op = token_type::plus; break;
				}
				// Zero pre-checks keep INDEX_COMPOUND's exact error codes/texts
				if (op == token_type::slash &&
				    ((rhs_idx == script_value::TYPEID_INT && rhs.unchecked_as_int() == 0) ||
				     (rhs_idx == script_value::TYPEID_FLOAT && rhs.unchecked_as_float() == 0.0))) {
					return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
				}
				if (op == token_type::percent && rhs_idx == script_value::TYPEID_INT && rhs.unchecked_as_int() == 0) {
					return raise_(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
				}
				const script_value current = storage->get(static_cast<size_t>(index), engine_);
				script_value resultValue = make_null();
				VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(current, op, rhs));
				if (storage->kind() == script_array::kind_t::i64 && resultValue.raw_storage_index() == script_value::TYPEID_FLOAT &&
				    !detail::float_fits_script_int(resultValue.unchecked_as_float())) {
					return raise_(make_error_code(runtime_error_code::invalid_numeric_operand), detail::float_to_integer_error);
				}
				storage->set(static_cast<size_t>(index), resultValue);
				stack_.pop_back();   // rhs
				stack_.pop_back();   // index
				stack_.back() = assignment_result_used(f) ? storage->get(static_cast<size_t>(index), engine_) : std::move(resultValue);   // container slot becomes the result: the stored element
				return {};
			}
			if (storage && !storage->is_typed() && index >= 0 && index < static_cast<script_int>(storage->size())) {
				script_value* target_ptr = &storage->values()[static_cast<size_t>(index)];
				const size_t elem_idx = target_ptr->raw_storage_index();
				if (elem_idx == script_value::TYPEID_INT || elem_idx == script_value::TYPEID_FLOAT) {
					token_type op;
					switch (ins.a & compound_kind_mask) {
						case compound_plus: op = token_type::plus; break;
						case compound_minus: op = token_type::minus; break;
						case compound_star: op = token_type::star; break;
						case compound_slash: op = token_type::slash; break;
						case compound_percent: op = token_type::percent; break;
						default: op = token_type::plus; break;
					}
					// Zero pre-checks keep INDEX_COMPOUND's exact error codes/texts
					if (op == token_type::slash &&
					    ((rhs_idx == script_value::TYPEID_INT && rhs.unchecked_as_int() == 0) ||
					     (rhs_idx == script_value::TYPEID_FLOAT && rhs.unchecked_as_float() == 0.0))) {
						return raise_(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
					}
					if (op == token_type::percent && rhs_idx == script_value::TYPEID_INT && rhs.unchecked_as_int() == 0) {
						return raise_(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
					}
					script_value resultValue = make_null();
					VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(*target_ptr, op, rhs));
					auto container_type_info = container.get_type_info();
					type_info_ptr element_type = container_type_info ? container_type_info->element_type() : nullptr;
					if (element_type) {
						if (!vm_is_element_type_compatible(resultValue, element_type, *target_ptr)) {
							std::string value_type = vm_value_type_name(resultValue);
							std::string expected_type = vm_type_info_name(element_type);
							uint64_t value_type_id = symbolizer_->intern(value_type);
							uint64_t expected_type_id = symbolizer_->intern(expected_type);
							return raise_(
								make_error_code(runtime_error_code::array_element_type_mismatch),
								"Cannot assign '{0}' to element of type '{1}'",
								value_type_id, expected_type_id);
						}
						// Numeric-to-numeric conversion: no user code (int/float only)
						auto converted_result = vm_convert_array_element(engine_, resultValue, element_type);
						if (!converted_result) { return raise_from(converted_result); }
						script_value converted = std::move(converted_result.value());
						*target_ptr = std::move(converted);
					} else {
						*target_ptr = std::move(resultValue.clone());
					}
					stack_.pop_back();   // rhs
					stack_.pop_back();   // index
					stack_.back() = assignment_result_used(f) ? clone_for_assignment(*target_ptr) : std::move(resultValue);   // container slot becomes the result: a copy of the stored element
					return {};
				}
			}
		}
	}
	// Slow replay: identical to the old two-op sequence
	script_value rhs = std::move(stack_.back());
	stack_.pop_back();
	vm_instruction index_ins{opcode::op_index, (ins.b & index_flag_lvalue_shape), 0, 0};
	auto indexed = exec_index(f, index_ins);
	if (indexed == op_status::failed) {
		return indexed;
	}
	stack_.push_back(std::move(rhs));
	vm_instruction compound_ins{opcode::op_index_compound, ins.a, 0, 0};
	return exec_index_compound(f, compound_ins);
}

// math:: language intrinsic: pops argc args, evaluates through the shared kernel
// (detail/math_intrinsics.hpp - parity with the interpreter by construction), pushes
// the result. Arg pointers are deref'd in place; the kernel copies nothing.
op_status vm_backend::exec_math(frame& f, const vm_instruction& ins) {
	const size_t argc = ins.b;
	// the emitter bounds a math:: call at eight arguments (compile_call); the buffer below
	// is the only fixed-size stack array in this file, so the bound holds here too
	if (argc > 8 || argc > stack_.size()) [[unlikely]] {
		return raise_(make_error_code(runtime_error_code::unsupported_operation), "math:: intrinsic arity exceeds the compiled bound");
	}
	const size_t base = stack_.size() - argc;
	const script_value* argp[8] = {};
	for (size_t i = 0; i < argc; ++i) {
		argp[i] = &stack_[base + i].deref();
	}
	auto result = detail::eval_math_intrinsic(static_cast<detail::math_fn>(ins.a), argp, argc,
	                                          engine_, engine_->math_rng());
	if (!result) {
		return raise_from(result);
	}
	script_value out = std::move(result.value());
	stack_.truncate(base);
	stack_.push_back(std::move(out));
	return {};
}

op_status vm_backend::exec_decl_var(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	uint64_t prof_d0 = __rdtsc();
#define JAI_DECL_SUB(idx) { const uint64_t prof_d1 = __rdtsc(); profile_decl_sub_[idx] += prof_d1 - prof_d0; prof_d0 = prof_d1; }
#else
#define JAI_DECL_SUB(idx)
#endif
	auto* decl = static_cast<variable_decl*>(f.code->nodes[ins.a].get());
	JAI_DECL_SUB(0);
#ifdef JAISCRIPT_VM_PROFILE
	if (f.ip > 0) { ++profile_decl_var_prev_[static_cast<uint8_t>(f.code->code[f.ip - 1].op)]; }
	if (ins.b != 0 && !stack_.empty()) { const size_t k = stack_.back().deref().raw_storage_index(); ++profile_decl_var_kinds_[k < 16 ? k : 15]; }
#endif
	const bool has_init = ins.b != 0;
	const bool lvalue_init = ins.c != 0;
	type_info_ptr effective_decl_type = decl->type;   // explicit, or inferred from a uniform literal

	// Decl fast path (construction-stamped flags): a scalar or string payload whose
	// storage index already matches the declared type stores straight into the slot -
	// enforce/clone/homogeneity are provably identity there (a scalar clones as a copy,
	// a string as its shared node, so lvalue_init is irrelevant). Anything else -
	// references, mismatched payloads, env-resident decls, escape-boxed decls - falls
	// through to the untouched full path below.
	// (KEEP BYTE-PARALLEL with interpreter::visit_variable_decl)
	if ((decl->decl_fast_flags & variable_decl::decl_fast_slot_store) == variable_decl::decl_fast_slot_store &&
	    has_init && !decl->ref_escaping &&
	    decl->slot_index != SIZE_MAX && f.locals && !f.top_level) {
		script_value& top = stack_.back();
		size_t vi = top.raw_storage_index();
		// Subscript-initialized decls (`float d = ch[ci]`) arrive as rhs-lvalue
		// reference wrappers - the profiled DOMINANT shape (63% of GLOOM decls). The
		// decl consumes the VALUE (the full path derefs identically), so a matching
		// pointee takes the same identity store: copy out BEFORE the pop (the stack
		// holder OWNS the deref target).
		const script_value* seen = &top;
		if (vi == script_value::TYPEID_REFERENCE) {
			seen = &top.deref();
			vi = seen->raw_storage_index();
		}
		if (variable_decl::identity_payload(vi, decl->type.get())) {
#ifdef JAISCRIPT_VM_PROFILE
			++profile_decl_paths_[0];
#endif
			JAI_DECL_SUB(1);
			script_value value = seen == &top ? std::move(top) : script_value(*seen);
			stack_.pop_back();
			JAI_DECL_SUB(2);
			if (decl->type) {
				value.set_type_info(decl->type);
			}
			frame_slot_set(f, decl->slot_index, std::move(value));
			JAI_DECL_SUB(3);
			return {};
		}
		if (variable_decl::widening_payload(vi, decl->type.get())) {
#ifdef JAISCRIPT_VM_PROFILE
			++profile_decl_paths_[0];
#endif
			script_value widened(static_cast<script_float>(seen->unchecked_as_int()), engine_);
			stack_.pop_back();
			widened.set_type_info(decl->type);
			frame_slot_set(f, decl->slot_index, std::move(widened));
			return {};
		}
#ifdef JAISCRIPT_VM_PROFILE
		++profile_decl_paths_[3];
#endif
	}
#ifdef JAISCRIPT_VM_PROFILE
	else { ++profile_decl_paths_[1]; }
#endif

	const bool is_weak_ptr = decl->type && decl->type->base_type == script_value_type::jai_weak_ptr_type;
	const bool is_shared_ptr = decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type;

	if (is_weak_ptr) {
		if (!has_init) {
			return define_decl_value(f, decl->name_id, decl->slot_index,
				script_value::make_empty_weak_ptr(decl->type, engine_));
		}
		script_value value = std::move(stack_.back());
		stack_.pop_back();
		// Element/subscript reads arrive as reference wrappers (rhs-lvalue read
		// shape); declarations consume the VALUE - normalize like assignment does
		// (KEEP BYTE-PARALLEL with interpreter::visit_variable_decl)
		if (value.is_reference()) {
			script_value derefed = value.deref();
			value = std::move(derefed);
		}
		if (value.is_null()) {
			return define_decl_value(f, decl->name_id, decl->slot_index,
				script_value::make_empty_weak_ptr(decl->type, engine_));
		}
		if (value.is_weak_ptr()) {
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value), decl->ref_escaping);
		}
		if (value.type() == script_value_type::jai_shared_ptr_type) {
			auto expected_type = decl->type ? decl->type->element_type() : nullptr;
			auto value_type_info = value.get_type_info();
			if (expected_type && value_type_info &&
			    expected_type->base_type != script_value_type::jai_any_type) {
				std::string expected_class = expected_type->type_name;
				std::string actual_class = value_type_info->element_type()
					? value_type_info->element_type()->type_name
					: value_type_info->type_name;
				if (expected_class != actual_class && engine_) {
					auto actual_def = engine_->get_class_definition(actual_class);
					if (!actual_def || !actual_def->is_subtype_of(expected_class)) {
						return raise_from(detail::typed_slot_rejection(value, *decl->type,
							detail::slot_site{detail::slot_kind::declaration, decl->name, {}}, engine_));
					}
				}
			}
			auto weak_result = script_value::make_weak_ptr(value, engine_);
			if (!weak_result) {
				return raise_from(weak_result);
			}
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(weak_result.value()), decl->ref_escaping);
		}
		// A value-semantic object or a primitive: family D's declaration shape
		// (KEEP BYTE-PARALLEL with the interpreter's visit_var_decl)
		return raise_from(detail::typed_slot_rejection(value, *decl->type,
			detail::slot_site{detail::slot_kind::declaration, decl->name, {}}, engine_));
	}

	if (is_shared_ptr) {
		// shared_ptr<auto> (element_type null) INFERS the pointee from the initializer's
		// exact class, then enforces it exactly like the explicit spelling (Dev ruling
		// 2026-07); a null/missing initializer has nothing to infer.
		const bool infer_pointee = decl->type && !decl->type->element_type();
		if (!has_init) {
			if (infer_pointee) {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Cannot infer shared_ptr<auto> pointee without an initializer - use var, or an explicit shared_ptr<T>");
			}
			script_value null_ptr = make_null();
			null_ptr.set_type_info(decl->type);
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(null_ptr), decl->ref_escaping);
		}
		script_value value = std::move(stack_.back());
		stack_.pop_back();
		// Element/subscript reads arrive as reference wrappers (rhs-lvalue read
		// shape); `shared_ptr<T> t = arr[i]` must see the element, not 'reference'
		// (KEEP BYTE-PARALLEL with interpreter::visit_variable_decl)
		if (value.is_reference()) {
			script_value derefed = value.deref();
			value = std::move(derefed);
		}
		if (value.is_null()) {
			if (infer_pointee) {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Cannot infer shared_ptr<auto> pointee from null - use var, or an explicit shared_ptr<T>");
			}
			value.set_type_info(decl->type);
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value), decl->ref_escaping);
		}
		if (value.is_weak_ptr()) {
			return raise_(make_error_code(runtime_error_code::invalid_weak_ptr_conversion), "Cannot initialize shared_ptr directly from weak_ptr");
		}
		if (value.type() == script_value_type::jai_object_type ||
		    value.type() == script_value_type::jai_shared_ptr_type) {
			if (infer_pointee) {
				// Take the initializer's exact class; thereafter the tag behaves
				// exactly like the explicit spelling (reassignment enforcement
				// reads it) - construct-and-share included.
				std::string actual_class;
				if (auto instance = value.get_class_instance()) {
					actual_class = instance->get_class_name();
				}
				type_info* inferred = nullptr;
				if (!actual_class.empty() && engine_) {
					if (auto* pointee_ti = engine_->get_type_info_object(symbolizer_->intern(actual_class))) {
						inferred = engine_->get_type_info_shared_ptr(pointee_ti);
					}
				}
				if (!inferred) {
					return raise_(make_error_code(runtime_error_code::type_mismatch),
						"Cannot infer shared_ptr<auto> pointee from this initializer - use var, or an explicit shared_ptr<T>");
				}
				value.set_type_info(inferred);
				return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value), decl->ref_escaping);
			}
			// Dev ruling (2026-07): typed shared_ptr declarations ENFORCE their
			// pointee class (wrong-class init used to alias silently). Same class
			// and derived->base (script chains + host upcasts) stay legal;
			// unresolvable classes stay lenient (opaque host flows).
			auto expected_type = decl->type->element_type();
			if (expected_type && !expected_type->type_name.empty() &&
			    expected_type->base_type != script_value_type::jai_any_type) {
				const std::string& expected_class = expected_type->type_name;
				std::string actual_class;
				if (auto instance = value.get_class_instance()) {
					actual_class = instance->get_class_name();
				}
				if (!actual_class.empty() && actual_class != expected_class) {
					bool is_subtype = false;
					if (auto eng = engine_) {
						auto actual_def = eng->get_class_definition(actual_class);
						is_subtype = actual_def && actual_def->is_subtype_of(expected_class);
					}
					if (!is_subtype) {
						return raise_(make_error_code(runtime_error_code::type_mismatch),
							"Cannot initialize shared_ptr<{0}> with {1}: type must match or be a subclass",
							expected_type->id, symbolizer_->intern(actual_class));
					}
				}
			}
			value.set_type_info(decl->type);
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value), decl->ref_escaping);
		}
		return raise_(make_error_code(runtime_error_code::invalid_shared_ptr_conversion), "Cannot initialize shared_ptr with this type");
	}

	script_value value = make_null();
	if (has_init) {
		value = std::move(stack_.back());
		stack_.pop_back();

		// Element/subscript reads arrive as reference wrappers (rhs-lvalue read
		// shape) - and so do ternaries over them, which the compile-time lvalue_init
		// flag can't see. Normalize to the VALUE and treat the read as an lvalue
		// read, or the typed enforcement below sees 'reference' and the stored local
		// silently ALIASES the element (KEEP BYTE-PARALLEL with
		// interpreter::visit_variable_decl)
		const bool reference_init = value.is_reference();
		if (reference_init) {
			script_value derefed = value.deref();
			value = std::move(derefed);
		}

		// Lvalue-read initializers copy through the kernel (values deep-copy,
		// shared_ptr handles share); temporaries move
		if (lvalue_init || reference_init) {
			value = clone_for_assignment(value);
		}

		if (value.is_array() || value.is_map()) {
			bool requires_homogeneity = false;
			if (!decl->type) {
				requires_homogeneity = true;
			} else if (decl->type->is_array()) {
				auto element_type = decl->type->element_type();
				if (!element_type) {
					requires_homogeneity = true;
				} else if (element_type->base_type == script_value_type::jai_any_type) {
					requires_homogeneity = false;
				}
			} else if (decl->type->is_map()) {
				auto value_type = decl->type->value_type();
				if (!value_type) {
					requires_homogeneity = true;
				} else if (value_type->base_type == script_value_type::jai_any_type) {
					requires_homogeneity = false;
				}
			} else if (decl->type->base_type == script_value_type::jai_any_type) {
				requires_homogeneity = false;
			}

			if (requires_homogeneity) {
				VM_TRY(vm_validate_container_homogeneous(value, "", arity_count_ids_, *symbolizer_));
				// A uniform primitive literal infers the element type and takes its
				// representation (KEEP BYTE-PARALLEL with the interpreter's decl)
				if (value.is_array()) {
					if (type_info_ptr inferred = detail::infer_uniform_array_type(value, engine_)) {
						effective_decl_type = inferred;
					}
				}
			}
			// Map twin (KEEP BYTE-PARALLEL with the interpreter's decl)
			if (value.is_map()) {
				int mixed_part = 0;
				if (type_info_ptr inferred = detail::infer_uniform_map_type(value, decl->type, engine_, mixed_part)) {
					effective_decl_type = inferred;
				} else if (mixed_part != 0) {
					return raise_(
						make_error_code(mixed_part == 1 ? runtime_error_code::map_key_type_mismatch : runtime_error_code::map_value_type_mismatch),
						mixed_part == 1 ? "Map 'auto' requires homogeneous keys" : "Map 'auto' requires homogeneous values");
				}
			}
		}
	}

	if (effective_decl_type) {
		// Typed declarations convert their initializer exactly like assignment does
		// (int d = 4.7 truncates instead of storing a mistagged float payload).
		// Exception: a 'var' decl keeps a shared_ptr-tagged initializer's marker —
		// `var p = new P()` IS `shared_ptr<P> p = P()` (Dev ruling 2026-07), so the
		// reference-semantics tag must survive the decl instead of flattening to 'any'
		if (has_init && !(decl->static_init_proof == proof_same_type &&
		                  detail::same_type_payload(value, effective_decl_type.get()))) {   // V-D1
			auto enforced = enforce_type_compatibility(std::move(value), effective_decl_type, decl->name, detail::slot_kind::declaration);
			if (!enforced) {
				return raise_from(enforced);
			}
			value = std::move(enforced.value());
		}
		if (effective_decl_type->base_type != script_value_type::jai_any_type ||
		    !value.get_type_info() ||
		    value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type) {
			value.set_type_info(effective_decl_type);
		} else if (engine_ && value.get_type_info()->element_type() &&
		           !value.get_type_info()->dynamic_pointee) {
			// var-held handle: '=' with a handle rhs REBINDS unchecked (Dev ruling
			// 2026-07; enforcement guards copy-assign-to-underlying, not re-pointing)
			value.set_type_info(engine_->get_type_info_shared_ptr_dynamic(
				value.get_type_info()->element_type().get()));
		}
	} else if (has_init && value.get_type_info() &&
	           value.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
	           value.get_type_info()->dynamic_pointee && engine_ &&
	           value.get_type_info()->element_type()) {
		// auto decl from a var-held handle: the rebindable marker does NOT transfer -
		// auto is infer-then-enforce (re-tag with the plain spelling)
		value.set_type_info(engine_->get_type_info_shared_ptr(
			value.get_type_info()->element_type().get()));
	}

	return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value), decl->ref_escaping);
}

// `env` = a CALLER env at a ref-arg bind (environment_ already points at the callee's chain)
std::optional<checked_result<script_value>> vm_backend::bind_bare_cpp_property_ref(frame& f, uint64_t sym, environment* env) {
	script_value* this_ptr = (f.locals && f.locals->is_method && f.locals->this_object_ptr)
		? &f.locals->get_this()
		: (env ? env : environment_.get())->get_value_ptr(this_id_);
	if (!this_ptr || !this_ptr->is_object() || this_ptr->is_null()) {
		return std::nullopt;
	}
	auto instance = this_ptr->get_class_instance();
	if (!instance || !detail::cpp_property_getter_id(instance.get(), sym)) {
		return std::nullopt;
	}
	return detail::bind_cpp_property_ref(*this_ptr, sym, engine_, symbolizer_);
}

op_status vm_backend::exec_decl_ref_ident(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<variable_decl*>(f.code->nodes[ins.a].get());
	const uint64_t target_sym = f.code->symbols[ins.b];

	// SLOT-first resolution (Dev ruling 2026-07-09; KEEP BYTE-PARALLEL with the
	// interpreter's ref-decl branch): plain function locals live in frame slots -
	// the escape marker cell-boxes the SLOT, so an env-only lookup made
	// `auto& r = x` error "undefined variable" for every slot-resident local.
	script_value* targetPtr = nullptr;
	bool global_only = false;
	if (decl->initializer && decl->initializer->get_type() == node_type::identifier_expr) {
		auto* target_ident = static_cast<identifier_expr*>(decl->initializer.get());
		global_only = target_ident->global_qualified;
		if (global_only) {
			// ::name - GLOBAL env only (resolve_local_or_env's door in the interpreter)
			targetPtr = engine_->get_global_environment()->get_value_ptr(target_sym);
		} else if (target_ident->slot_index != SIZE_MAX && f.locals && !f.top_level) {
			targetPtr = frame_slot(f, target_ident->slot_index);
		}
	}
	if (!targetPtr && !global_only) {
		targetPtr = environment_->get_value_ptr(target_sym);
	}
	if (!targetPtr && !global_only) {
		targetPtr = frame_this_member_ptr(f, target_sym);   // method-lazy: bare field targets
	}
	if (!targetPtr) {
		// A hybrid's bare C++ property: the member itself / its accessors
		// (KEEP BYTE-PARALLEL with the interpreter's ref-decl branch; ::name never mints)
		std::optional<checked_result<script_value>> minted;
		if (!global_only) { auto b = bind_bare_cpp_property_ref(f, target_sym); if (b) { minted.emplace(std::move(*b)); } }
		if (minted) {
			if (!*minted) {
				return raise_from(*minted);
			}
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(minted->value()));
		}
		return raise_(make_error_code(runtime_error_code::undefined_variable), "Cannot take reference of undefined variable", target_sym);
	}

	if (targetPtr->is_reference()) {
		auto aliased = vm_decl_ref_alias(*targetPtr, engine_);
		if (!aliased) {
			return raise_from(aliased);
		}
		return define_decl_value(f, decl->name_id, decl->slot_index, std::move(aliased.value()));
	}
	// Box the variable's storage into a cell and alias it (escape-legal: the ref
	// keeps the cell alive after the env dies)
	return define_decl_value(f, decl->name_id, decl->slot_index, share_env_ref(*targetPtr));
}

op_status vm_backend::exec_decl_ref_value(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<variable_decl*>(f.code->nodes[ins.a].get());
	script_value result = std::move(stack_.back());
	stack_.pop_back();

	if (result.is_reference()) {
		auto aliased = vm_decl_ref_alias(result, engine_);
		if (!aliased) {
			return raise_from(aliased);
		}
		return define_decl_value(f, decl->name_id, decl->slot_index, std::move(aliased.value()));
	}
	if (decl->initializer && detail::is_member_final_ref_lvalue(decl->initializer.get())) {
		// Member-final initializer (auto& p = G.player): field reads evaluate to COPIES,
		// so the evaluated result can't alias - resolve the chain through the shared
		// kernel into an owner-pinned FIELD reference instead (route-independence with
		// ref params/returns; Dev ruling 2026-07-12). Access/removed-field errors
		// surface as-is; the kernel's generic non-lvalue verdict (computed script
		// property, non-object receiver) keeps the decl's own error text.
		// KEEP BYTE-PARALLEL with the interpreter's ref-decl branch
		ensure_frame_member_scope(f, environment_.get());   // kernel reads the frame's member scope
		auto resolved = detail::resolve_ref_lvalue(decl->initializer.get(), caller_view(&f),
		                                           environment_.get(), engine_, symbolizer_);
		if (!resolved && resolved.error() != make_error_code(runtime_error_code::invalid_reference)) {
			return raise_from(resolved);
		}
		if (resolved) {
			auto aliased = vm_decl_ref_alias(resolved.value(), engine_);
			if (!aliased) {
				return raise_from(aliased);
			}
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(aliased.value()));
		}
	}
	// KEEP BYTE-PARALLEL with the interpreter's ref-decl branch
	return raise_(make_error_code(runtime_error_code::invalid_reference),
		"Cannot take reference of non-lvalue expression (var&/auto& declarations bind locals, "
		"subscript chains like arr[i] or grid[y][x], map entries, object fields like obj.field and C++ properties; "
		"computed script properties are not ref-bindable)");
}

op_status vm_backend::exec_destructure(frame& f, const vm_instruction& ins) {
	script_value source = std::move(stack_.back());
	stack_.pop_back();

	if (!source.is_array()) {
		return raise_(make_error_code(runtime_error_code::type_mismatch),
			"Destructuring requires an array on the right-hand side");
	}

	const auto& node = *source.get_array_storage();
	const destructure_proto& proto = f.code->destructure_protos[ins.a];

	for (size_t i = 0; i < proto.names.size(); ++i) {
		script_value val = i < node.size()
			? (node.is_typed() ? node.get(i, engine_) : clone_for_assignment(node.values()[i]))
			: make_null();
		VM_TRY(define_decl_value(f, proto.names[i].first, proto.names[i].second, std::move(val)));
	}
	return {};
}

// Callee-first probe (Dev ruling 2026-07-08: callee-before-args IS the language,
// matching C++17): resolve the identifier callee exactly where op_load did (same
// resolution ladder via fused_ident_value, same errors) and park it in the pending
// register stack - no value-stack copy. The not-callable check ALSO fires here, at
// the ruled observation point (the interpreter always checked before args; the vm's
// old post-args check was a latent divergence, pinned by callee_first_* tests).
op_status vm_backend::raise_not_callable(uint64_t name_id, const script_value& callee) {
	const script_value& v = callee.deref();
	auto target = resolve_member_target(v);
	auto text = not_callable_diagnostic(name_id, v.type(), base_type_ids_[v.type()],
	                                    target.class_def ? std::string_view(target.class_def->get_name()) : std::string_view{},
	                                    target.class_def ? target.class_def->get_type_id() : 0);
	return raise_(make_error_code(runtime_error_code::not_a_function), text.message, text.sym0, text.sym1);
}

op_status vm_backend::exec_probe_callee(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	uint64_t prof_p0 = __rdtsc();
	++profile_probe_count_;
#define JAI_PROBE_SECTION(idx) { const uint64_t prof_p1 = __rdtsc(); profile_probe_cyc_[idx] += prof_p1 - prof_p0; prof_p0 = prof_p1; }
#else
#define JAI_PROBE_SECTION(idx)
#endif
	const call_site& site = f.code->call_sites[ins.a];
	const size_t argc = ins.b;
	std::optional<script_value> scratch;
	const script_value* resolved = fused_ident_value(f, site.callee, scratch, f.ip * 3);
	if (!resolved) return op_status::failed;
	const script_value& calleeVal = *resolved;
	if (!calleeVal.is_function()) {
		uint64_t name_id = site.callee.symbol != k_invalid_u32 ? f.code->symbols[site.callee.symbol] : 0;
		if (name_id == 0 && site.call_node != k_invalid_u32) {
			name_id = callee_name_symbol(static_cast<call_expr*>(f.code->nodes[site.call_node].get())->callee.get());
		}
		return raise_not_callable(name_id, calleeVal);
	}
	JAI_PROBE_SECTION(0);
	pending_callee pc;
	if (cached_global_env_ &&
	    calleeVal.raw_storage_index() == script_value::TYPEID_FUNCTION &&
	    &calleeVal.unchecked_as_function() == site.ic_identity &&
	    argc == site.ic_argc) {
		pc.fn = site.ic_fn;
		pc.pin = site.ic_pin;
	} else {
		bool parked_direct = false;
		if (calleeVal.raw_storage_index() == script_value::TYPEID_FUNCTION) {
			const auto* thunk = calleeVal.unchecked_as_function().target<script_callable_thunk>();
			if (thunk && thunk->eng == engine_ &&
			    thunk->payload.kind == script_callable::kind_type::function && thunk->payload.fn &&
			    argc == thunk->payload.fn->parameters().size()) {
				pc.fn = thunk->payload.fn.get();
				pc.pin = *calleeVal.get_storage().get_if<script_value::TYPEID_FUNCTION>();
				parked_direct = true;
				if (cached_global_env_ && !thunk->payload.fn->closure_env) {
					site.ic_pin = pc.pin;
					site.ic_identity = &calleeVal.unchecked_as_function();
					site.ic_fn = pc.fn;
					site.ic_argc = static_cast<uint32_t>(argc);
				}
			}
		}
		if (!parked_direct) {
			// Opaque callables, refs-to-function, arity-window (default-arg) calls:
			// park the materialized value - the same single copy op_load made
			pc.value = calleeVal;
		}
	}
	pending_callees_.push_back(std::move(pc));
	JAI_PROBE_SECTION(1);
#undef JAI_PROBE_SECTION
	return {};
}

// Callee-first probe for member callees (the op_probe_callee ruling extended to
// `recv.m(args)`): the lookup errors exec_call_method's ladder raises post-args fire
// here, at the pre-args observation point; every other outcome leaves the stack
// untouched and the call op runs its full ladder unchanged. Lookup only - no getter
// runs (a member with a real getter defers to the call op), no bound method is minted.
// b=1 is the computed-callee form: the loaded callee's callable check.
op_status vm_backend::exec_probe_member(frame& f, const vm_instruction& ins) {
	if (ins.b == 1) {
		if (!stack_.back().is_function()) {
			return raise_not_callable(0, stack_.back());
		}
		return {};
	}
	const call_site& site = f.code->call_sites[ins.a];
	auto* member = static_cast<member_expr*>(f.code->nodes[site.member_node].get());
	const script_value& objectValue = stack_.back().deref();
	const uint64_t member_id = member->member_id;
	if (member->null_safe && objectValue.is_null()) {
		return {};
	}
	// The call op's callable check on a member that resolves to a plain value
	auto callable = [&](const script_value& v) -> op_status {
		if (v.is_function() || (v.is_null() && member->null_safe)) { return {}; }
		return raise_not_callable(member_id, v);
	};

	if (member->object && member->object->get_type() == node_type::super_expr) {
		std::shared_ptr<class_definition> parent_def;
		if (objectValue.is_object()) {
			if (auto objHolder = objectValue.get_object_holder(); objHolder && objHolder->data) {
				if (auto* class_def = std::static_pointer_cast<class_instance>(objHolder->data)->get_class_definition()) {
					parent_def = class_def->get_parent();
				}
			}
		}
		if (!parent_def) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		if (parent_def->chain_has_nonpublic()) [[unlikely]] {
			VM_TRY(detail::enforce_member_access(parent_def.get(), member_id, frame_access_context(f)));
		}
		try {
			if (parent_def->get_method(member_id).is_null()) {
				return raise_(make_error_code(runtime_error_code::member_not_found),
					"parent class {0} has no member '{1}'", parent_def->get_type_id(), member_id);
			}
		} catch (const runtime_error& e) {
			raise_script_exception(e.what(), member->location);
		}
		return {};
	}

	const size_t receiver_type = objectValue.raw_storage_index();
	if (receiver_type == script_value::TYPEID_OBJECT || receiver_type == script_value::TYPEID_SHARED_PTR) {
		const auto holder = objectValue.get_object_holder();
		if (holder && holder->type_id == coroutine_handle_type_id_) {
			if (member_id == resume_id_ || member_id == done_id_) { return {}; }
			return raise_(make_error_code(runtime_error_code::member_not_found),
				"coroutine_handle has no member '{0}'", member_id);
		}
		if (holder && holder->is_class_instance_wrapper && holder->data && member_id != same_as_id_) {
			auto* inst = static_cast<class_instance*>(holder->data.get());
			class_definition* cd = inst->get_class_definition();
			if (parallel_worker_) [[unlikely]] {
				// Worker verdict (parallel-method-admission ruling): the call op's text; a pin
				// answers in unsafe mode too (no shared count or cache), unpinned is unsafe's business
				const detail::parallel_method_pin* pin = parallel_method_pins_ && cd
					? parallel_method_pins_->find(cd, member_id) : nullptr;
				if (pin) { return {}; }
				{
					return raise_(make_error_code(runtime_error_code::unsupported_operation),
						"method '{0}' was not admitted in this parallel body (admitted: single-overload "
						"public methods on the element's own class touching only element, local, or "
						"captured-read state)", member_id);
				}
			}
			if (cached_global_env_ && cd) {
				const uint64_t epoch = cd->method_epoch();
				if ((site.mic_cd == cd && site.mic_epoch == epoch && !mic_field_shadowed(site, *inst, member_id)) ||
				    (site.probe_cd == cd && site.probe_epoch == epoch && !probe_field_shadowed(site, *inst, member_id))) {
					return {};
				}
			}
		}
	}

	// member_access_value's precedence, lookup only (KEEP the texts BYTE-PARALLEL with it)
	if (objectValue.is_string()) {
		const bool known = f.code->builtin_indexed
			? site.bi_string != builtin_method_registries::k_no_builtin
			: builtins_.string_methods.find(member_id) != builtins_.string_methods.end();
		if (!known) {
			raise_script_exception("string has no member '" + std::string(member->member) + "'", member->location);
		}
		return {};
	}
	if (objectValue.is_array()) {
		const bool known = f.code->builtin_indexed
			? site.bi_array != builtin_method_registries::k_no_builtin
			: builtins_.array_methods.find(member_id) != builtins_.array_methods.end();
		if (!known) {
			raise_script_exception("array has no member '" + std::string(member->member) + "'", member->location);
		}
		return {};
	}
	if (objectValue.is_map()) {
		const bool known = f.code->builtin_indexed
			? site.bi_map != builtin_method_registries::k_no_builtin
			: builtins_.map_methods.find(member_id) != builtins_.map_methods.end();
		if (known) { return {}; }
		auto& map_storage = const_cast<script_value&>(objectValue).get_map_storage();
		if (map_storage) {
			auto found = map_storage->find(script_value(std::string(member->member), engine_));
			if (found != map_storage->end()) { return callable(found->second); }
		}
		return raise_(make_error_code(runtime_error_code::member_not_found),
			"map has no member or key '{0}'", member_id);
	}
	if (objectValue.is_weak_ptr()) {
		if (builtins_.weak_ptr_methods.find(member_id) != builtins_.weak_ptr_methods.end()) { return {}; }
		return raise_(make_error_code(runtime_error_code::member_not_found),
			"weak_ptr has no member '{0}'", member_id);
	}
	if (objectValue.get_type_info() &&
	    objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
	    builtins_.shared_ptr_methods.find(member_id) != builtins_.shared_ptr_methods.end()) {
		bool user_shadows = false;
		if (auto ci = const_cast<script_value&>(objectValue).get_class_instance()) {
			if (auto* cd = ci->get_class_definition()) {
				user_shadows = cd->defines_method(member_id);
			}
		}
		if (!user_shadows) { return {}; }
	}
	if (member_id == same_as_id_) {
		return {};
	}
	if (!objectValue.is_object()) {
		if (objectValue.is_null()) {
			return raise_(make_error_code(runtime_error_code::member_access_on_null), null_member_message(member->object.get(), true), member_id, null_member_receiver_symbol(member->object.get()));
		}
		return raise_(make_error_code(runtime_error_code::member_not_found),
			scalar_member_message, base_type_ids_[objectValue.type()], member_id);
	}

	auto target = resolve_member_target(objectValue);
	if (!target) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}
	if (target.class_def && target.class_def->chain_has_nonpublic()) [[unlikely]] {
		VM_TRY(detail::enforce_member_access(target.class_def, member_id, frame_access_context(f)));
	}
	if (target.class_def && target.class_def->has_property_getters()) {
		uint64_t getter_id = member->getter_id;
		if (getter_id == UINT64_MAX) {
			auto [id, _] = symbolizer_->get_getter_id_with_view(member_id);
			getter_id = id;
			member->getter_id = getter_id;
		}
		script_value getter = target.method(getter_id);
		if (!getter.is_null() && !getter.is_invalid() && getter.is_function()) {
			// An auto accessor reads the backing field; a custom getter runs user code
			// and defers to the call op (which runs it once, post-args)
			if (target.class_def->is_auto_accessor(getter_id) && target.has_field(member_id)) {
				return callable(target.get_field(member_id));
			}
			return {};
		}
	}
	if (target.has_field(member_id)) {
		return callable(target.get_field(member_id));
	}
	script_value method = target.method(member_id);
	if (!method.is_null() && !method.is_invalid()) {
		if (cached_global_env_ && target.instance && target.class_def && !target.class_def->chain_has_nonpublic()) {
			if (class_definition* cd = target.instance->get_class_definition()) {
				site.probe_cd = cd;
				site.probe_epoch = cd->method_epoch();
				site.probe_field_free = !cd->chain_declares_field(member_id) &&
				                        cd->field_slot(member_id) == class_definition::k_no_field_slot;
			}
		}
		return {};
	}
	if (target.class_def && target.class_def->is_transparent_wrapper()) {
		script_value mutable_obj = objectValue;
		script_value unwrapped = target.class_def->unwrap(mutable_obj);
		if (!unwrapped.is_null() && unwrapped.is_object()) {
			if (auto unwrapped_target = resolve_member_target(unwrapped)) {
				if (unwrapped_target.has_field(member_id)) {
					return callable(unwrapped_target.get_field(member_id));
				}
				script_value unwrapped_method = unwrapped_target.method(member_id);
				if (!unwrapped_method.is_null() && !unwrapped_method.is_invalid()) {
					return {};
				}
			}
		}
	}
	{
		const std::string& cls = target.class_name();
		raise_script_exception((cls.empty() ? "object" : cls) + " has no member '" + std::string(member->member) + "'", member->location);
	}
	return {};
}

op_status vm_backend::exec_call_from_scratch(frame& f, const vm_instruction& ins) {
	const size_t argc = ins.a;
	const call_site& site = f.code->call_sites[ins.b];
	pending_callee pc = std::move(pending_callees_.back());
	pending_callees_.pop_back();

#ifdef JAISCRIPT_VM_PROFILE
	// Path-bucketed self-cost (see profile_cfs_cycles_): dtor charges the bucket the
	// taken path selected; the opaque tail keeps the default and splits by callee name
	struct cfs_probe {
		vm_backend* vm;
		int bucket = 2;
		std::string opaque_name;
		uint64_t t0 = __rdtsc();
		~cfs_probe() {
			const uint64_t dt = __rdtsc() - t0;
			vm->profile_cfs_cycles_[bucket] += dt;
			++vm->profile_cfs_counts_[bucket];
			if (bucket == 2 && !opaque_name.empty()) { vm->profile_cfs_opaque_cycles_[opaque_name] += dt; }
		}
	} cfs_probe_{this};
	if (site.callee.symbol != k_invalid_u32) {
		cfs_probe_.opaque_name = std::string(symbolizer_->get_string(f.code->symbols[site.callee.symbol]));
	}
#endif

	if (pc.fn) {
#ifdef JAISCRIPT_VM_PROFILE
		cfs_probe_.bucket = 0;
		if (!cfs_probe_.opaque_name.empty()) { profile_cfs_inloop_names_[cfs_probe_.opaque_name]++; }
#endif
		const size_t args_base = stack_.size() - argc;
		try {
			// Real TCO (Dev ruling: no artificial depth limit): a tail-flagged bare call
			// resolving to the CURRENTLY EXECUTING chunk splices the live record instead
			// of pushing. Gates — top record frame (fibers and non-record frames decline),
			// self chunk (a mid-recursion global rebind resolves to a different chunk and
			// declines, preserving the call-time-resolution contract), env-untouched
			// activation (needs_frame_env false => environment_ provably never moved, so
			// there is nothing to release or install), idempotent return-conv class (the
			// dying frame's skipped epilogue and the spliced activation's are the same
			// pass-through), no live try records (return-inside-try must keep the frame
			// to catch callee throws), clean pending register. The budget check keeps
			// infinite tail loops interruptible exactly like real pushes.
			if (ins.c != 0 && call_records_top_ > 0) {
				call_record& rec = *call_records_[call_records_top_ - 1];
				const auto conv = static_cast<return_conv>(rec.return_conv_class);
				if (&f == &rec.f && static_cast<chunk*>(pc.fn->backend_body_cache.get()) == f.code &&
				    rec.env_untouched &&
				    (conv == return_conv::none ||
				     (conv >= return_conv::prim_int && conv <= return_conv::prim_string)) &&
				    try_records_.size() == rec.try_base &&
				    pending_callees_.size() == rec.pending_base) {
					if (execution_limit_exhausted()) [[unlikely]] {
						return raise_from(execution_limit_failure());
					}
					return tail_splice_frame(rec, *pc.fn, std::move(pc.pin), args_base, argc, &site);
				}
			}
			return push_script_frame_pinned(f, *pc.fn, std::move(pc.pin), args_base, argc, &site);
		} catch (const script_exception& e) {
			// pre-record throws leave the args on the stack; drop them like exec_call
			stack_.truncate(args_base);
			active_exception_value_ = script_value(std::string(e.what()), engine_);
			current_exception_ = e;
			is_unwinding_ = true;
			stack_.push_back(make_null());
			return {};
		} catch (const std::exception& e) {
			stack_.truncate(args_base);
			active_exception_value_ = script_value(std::string(e.what()), engine_);
			current_exception_ = script_exception(e.what());
			is_unwinding_ = true;
			stack_.push_back(make_null());
			return {};
		}
	}

	// Bound-method thunk (bare sibling calls): SLICE-enter before any collection —
	// the args on the stack bind in place, the receiver rides in from the payload,
	// resolution reads the slice via the span overload. This is the brains-bench
	// heat: 2a covered mic/pin sites; this is where the arg-vector traffic dies.
	if (pc.value.raw_storage_index() == script_value::TYPEID_FUNCTION) {
		const auto* bthunk = pc.value.unchecked_as_function().target<script_callable_thunk>();
		if (bthunk && bthunk->eng == engine_ &&
		    bthunk->payload.kind == script_callable::kind_type::bound_method &&
		    bthunk->payload.this_obj && bthunk->payload.bound_dispatch &&
		    !parallel_worker_) {
			const auto* dispatch = bthunk->payload.bound_dispatch->as_function().target<script_method_dispatch>();
			if (dispatch && dispatch->eng == engine_) {
				const size_t slice_base = stack_.size() - argc;
				auto resolved = dispatch->cls->resolve_method_overload_ref(dispatch->name_id,
					stack_.data() + slice_base, argc);
				if (resolved && (*resolved.value())->body && !(*resolved.value())->is_coroutine) {
#ifdef JAISCRIPT_VM_PROFILE
					cfs_probe_.bucket = 1;
#endif
					script_value method_val = *bthunk->payload.bound_dispatch;
					script_value receiver = *bthunk->payload.this_obj;
					return enter_script_method_sliced(f, std::move(method_val), *dispatch,
					                                  *resolved.value(), &receiver, slice_base,
					                                  slice_base, argc, site);
				}
			}
		}
	}

	// Opaque / arity-window path: today's pooled invoke with the parked value
#ifdef JAISCRIPT_VM_PROFILE
	// Who misses the in-loop fast path: count native-boundary callees by site symbol.
	// Bound-method thunks are NOT misses (invoke_callee enters them in-loop below).
	{
		const auto* parked_thunk = pc.value.is_function()
			? pc.value.as_function().target<script_callable_thunk>() : nullptr;
		const bool parked_bound = parked_thunk &&
			parked_thunk->payload.kind == script_callable::kind_type::bound_method;
		if (!parked_bound) {
			if (ins.b < f.code->call_sites.size() && site.callee.symbol != k_invalid_u32) {
				profile_native_callees_[std::string(symbolizer_->get_string(f.code->symbols[site.callee.symbol]))]++;
			} else {
				profile_native_callees_["<unnamed-site>"]++;
			}
		}
	}
#endif
	auto arguments = acquire_arg_vector(argc);
	arg_vector_return arg_return{this, &arguments};
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		arguments.push_back(std::move(stack_[base + i]));
	}
	stack_.truncate(base);
	return invoke_callee(f, std::move(pc.value), arguments, site);
}

// Zero-copy frame push for probe-called frames: no callee value below the window,
// the record's direct_pin holds the callable (hot reload cannot kill the executing
// function; the pin releases at pop exactly where the callee slot died).
op_status vm_backend::push_script_frame_pinned(frame& caller,
                                                          const script_defined_function& function,
                                                          strong_ptr<script_function> pin,
                                                          size_t args_base, size_t argc,
                                                          const call_site* site) {
#ifdef JAISCRIPT_VM_PROFILE
	uint64_t prof_p0 = __rdtsc();
	++profile_call_push_count_;
#define JAI_CALL_PUSH_SECTION(idx) { const uint64_t prof_p1 = __rdtsc(); profile_call_push_cyc_[idx] += prof_p1 - prof_p0; prof_p0 = prof_p1; }
#else
#define JAI_CALL_PUSH_SECTION(idx)
#endif
	if (current_call_depth_ >= limits_->vm_call_depth_limit) {
		return raise_from(detail::raise_call_depth());
	}
	if (execution_limit_exhausted()) [[unlikely]] {
		return raise_from(execution_limit_failure());
	}
	assert(!has_return_value_);

	chunk* body_chunk;
	{
		body_chunk = static_cast<chunk*>(function.backend_body_cache.get());
		if (!body_chunk) {
			auto compiled = chunk_for_body(function.name, function.parameters(), function.body, function.local_count);
			function.backend_body_cache = compiled;
			body_chunk = compiled.get();
		}
		if (parallel_worker_) {
			// The owning handle is in hand exactly here: pin the chunk so this
			// worker's private env-lookup rows stay identity-sound for its life
			worker_pin_env_cache(function.backend_body_cache, body_chunk);
		}
		if (call_records_top_ == call_records_.size()) {
			call_records_.push_back(std::make_unique<call_record>());
		}
	}
	JAI_CALL_PUSH_SECTION(0);

	call_record& rec = *call_records_[call_records_top_];
	++call_records_top_;
#ifdef JAISCRIPT_VM_PROFILE
	rec.profile_entry_tsc = __rdtsc();
#endif
	rec.caller = &caller;
	rec.return_type = function.return_type;
	if (function.backend_return_conv == 0) {
		function.backend_return_conv = static_cast<uint8_t>(classify_return_conv(function.return_type));
	}
	rec.return_conv_class = function.backend_return_conv;
	rec.direct_pin = std::move(pin);
	rec.try_base = try_records_.size();
	rec.iter_base = iter_states_.size();
	rec.cfor_base = cfor_states_.size();
	rec.pending_base = pending_callees_.size();
	rec.locals.function_name = function.name;
	rec.locals.reset_member_scope();
#ifdef JAISCRIPT_PILOT_CELLS
	rec.cells.reset();
	rec.f.cells = &rec.cells;
#endif
	JAI_CALL_PUSH_SECTION(1);
	rec.env_lazy = !body_chunk->needs_frame_env &&
	               (!function.closure_env ||
	                (!function.closure_env->is_method_env() && !function.closure_env->is_static_method_env()));
	if (rec.env_lazy && !function.closure_env) {
		rec.env_untouched = true;
	} else {
		rec.env_untouched = false;
		rec.prev_env = std::move(environment_);
		try {
			if (rec.env_lazy) {
				environment_ = function.closure_env;
			} else {
				setup_callee_env(function, rec.locals, rec.prev_env);
			}
		} catch (...) {
			rec.direct_pin = {};
			rec.return_type = nullptr;
			environment_ = std::move(rec.prev_env);
			--call_records_top_;
			throw;
		}
	}
	JAI_CALL_PUSH_SECTION(2);
	++current_call_depth_;
	rec.f.code = body_chunk;
	rec.f.ip = 0;
	rec.f.locals = &rec.locals;
	// entry_env doubles as the lexical-context marker: null ONLY for env-untouched
	// frames (plain non-closure functions are lexically top-level - no class context,
	// no closure chain); closure-lazy frames carry their closure env (chain is correct)
	rec.f.entry_env = rec.env_untouched ? nullptr : environment_;
	rec.f.window_backed = true;
	rec.f.window_base = args_base;
	rec.f.window_live = static_cast<uint32_t>(argc);
	rec.f.stack_base = args_base;   // no callee slot below the window
	rec.f.top_level = false;
	frames_.push_back(&rec.f);
	JAI_CALL_PUSH_SECTION(3);

	op_status bound{};
	try {
		bound = bind_parameters(function.parameters(), stack_, args_base, argc, rec.f, *rec.f.code,
		                        rec.env_untouched ? environment_ : rec.prev_env, site, &caller, caller.code,
		                        site_arg_proofs(site, caller.code, function.body.get(), function.parameters().size(), argc), &function);
	} catch (...) {
		pop_script_frame_core(rec);
		throw;
	}
	if (bound == op_status::failed) {
		pop_script_frame_core(rec);
		return bound;
	}
	JAI_CALL_PUSH_SECTION(4);
	assert(stack_.size() == args_base + argc);
	const size_t window_slots = std::max(function.local_count, static_cast<size_t>(body_chunk->local_count));
	stack_.fill_null_to(rec.f.window_base + window_slots, engine_);
	switch_to_ = &rec.f;
	JAI_CALL_PUSH_SECTION(5);
#undef JAI_CALL_PUSH_SECTION
	return {};
}

// Tail-call splice (real TCO). Caller gates prove: self chunk, top record frame,
// env-untouched, idempotent return conv, no live trys, exact arity (the pc.fn path
// invariant — no default binding). The new activation binds at the TOP slice while
// the dying window is still live below it (ref args cell-share against dying slots
// BEFORE they die), then relocates down over the old window — the ranges are
// provably disjoint (same function => the args sat above the old full window).
// rec.caller / stack_base / try-iter-cfor bases / depth all stay: the spliced
// activation returns directly to the ORIGINAL caller through the unchanged pop.
op_status vm_backend::tail_splice_frame(call_record& rec, const script_defined_function& function,
                                        strong_ptr<script_function> pin,
                                        size_t args_base, size_t argc, const call_site* site) {
	chunk* body_chunk = rec.f.code;
	frame tf;
	tf.code = body_chunk;
	tf.locals = &rec.locals;
	tf.stack_base = args_base;
	tf.window_backed = true;
	tf.window_base = args_base;
	tf.window_live = static_cast<uint32_t>(argc);
	tf.top_level = false;
	frames_.push_back(&tf);   // ref-param frames_ scan parity with the real push
	op_status bound{};
	try {
		bound = bind_parameters(function.parameters(), stack_, args_base, argc, tf, *body_chunk,
		                        environment_, site, &rec.f, rec.f.code,
		                        site_arg_proofs(site, rec.f.code, function.body.get(), function.parameters().size(), argc), &function);
	} catch (...) {
		frames_.pop_back();
		throw;
	}
	frames_.pop_back();
	if (bound == op_status::failed) {
		return bound;
	}
	assert(stack_.size() == args_base + argc);
	const size_t window_slots = std::max(function.local_count, static_cast<size_t>(body_chunk->local_count));
	stack_.fill_null_to(args_base + window_slots, engine_);
	const size_t window_extent = stack_.size() - args_base;
	// The dying frame's loop state dies at the boundary pop would have killed it
	// (cfor fast-state pointers into the old window die BEFORE the window is reused)
	if (iter_states_.size() > rec.iter_base) {
		iter_states_.erase(iter_states_.begin() + rec.iter_base, iter_states_.end());
	}
	if (cfor_states_.size() > rec.cfor_base) {
		cfor_states_.erase(cfor_states_.begin() + rec.cfor_base, cfor_states_.end());
	}
	const size_t window_base = rec.f.window_base;
	if (args_base != window_base) {
		for (size_t i = 0; i < window_extent; ++i) {
			stack_[window_base + i] = std::move(stack_[args_base + i]);
		}
	}
	stack_.truncate(window_base + window_extent);
	rec.direct_pin = std::move(pin);
	rec.f.ip = 0;
	rec.f.window_live = tf.window_live;
	switch_to_ = &rec.f;
	return {};
}

op_status vm_backend::exec_call(frame& f, const vm_instruction& ins) {
	const size_t argc = ins.a;
	const call_site& site = f.code->call_sites[ins.b];

	// In-loop fast path peeks the callee below the args (Squirrel's OP_CALL→StartCall
	// shape): exact-arity direct callees bind straight from the stack_ slice — no pooled
	// arg vector, no per-arg move. Default-arg and arity-error calls fall through.
	if (stack_.size() > argc && stack_[stack_.size() - argc - 1].is_function()) {
		const size_t args_base = stack_.size() - argc;
		const script_value& calleeVal = stack_[args_base - 1];
		const script_defined_function* direct_fn = nullptr;
		// Monomorphic callee cache hit: same pinned script_function payload at the same
		// arity skips the target<>() RTTI validate (see call_site in chunk.hpp). The raw
		// TYPEID gate keeps reference callees on the validate path (is_function derefs);
		// the cached_global_env_ gate keeps workers out of chunk caches entirely (reads
		// AND writes), mirroring env_lookup_cached.
		if (cached_global_env_ &&
		    calleeVal.raw_storage_index() == script_value::TYPEID_FUNCTION &&
		    &calleeVal.unchecked_as_function() == site.ic_identity &&
		    argc == site.ic_argc) {
			direct_fn = site.ic_fn;
		} else {
			const auto* thunk = calleeVal.as_function().target<script_callable_thunk>();
			if (thunk && thunk->eng == engine_ &&
			    thunk->payload.kind == script_callable::kind_type::function && thunk->payload.fn &&
			    argc == thunk->payload.fn->parameters().size()) {
				direct_fn = thunk->payload.fn.get();
				// Fill gated exactly like the env-lookup caches: parallel workers share
				// chunks with the main engine and must never write them. Closure callees
				// (closure_env set) are NEVER cached: ic_pin holding one would pin its
				// capture env — captured objects would outlive their scope, which is
				// script-observable (alive counts, script destructor timing). Plain
				// functions pin only engine-internal state (body AST, params).
				if (cached_global_env_ && !thunk->payload.fn->closure_env &&
				    calleeVal.raw_storage_index() == script_value::TYPEID_FUNCTION) {
					site.ic_pin = *calleeVal.get_storage().get_if<script_value::TYPEID_FUNCTION>();
					site.ic_identity = &calleeVal.unchecked_as_function();
					site.ic_fn = direct_fn;
					site.ic_argc = static_cast<uint32_t>(argc);
				}
			}
		}
		// Bound-method thunk at a stack call site: slice-enter while callee + args are
		// still in place — the callee slot is the frame territory (destroying the thunk
		// at pop, which pins receiver + dispatcher transitively until then).
		if (!direct_fn && calleeVal.raw_storage_index() == script_value::TYPEID_FUNCTION) {
			const auto* bthunk = calleeVal.unchecked_as_function().target<script_callable_thunk>();
			if (bthunk && bthunk->eng == engine_ &&
			    bthunk->payload.kind == script_callable::kind_type::bound_method &&
			    bthunk->payload.this_obj && bthunk->payload.bound_dispatch &&
			    !parallel_worker_) {
				const auto* mdispatch = bthunk->payload.bound_dispatch->as_function().target<script_method_dispatch>();
				if (mdispatch && mdispatch->eng == engine_) {
					auto resolved = mdispatch->cls->resolve_method_overload_ref(mdispatch->name_id,
						stack_.data() + args_base, argc);
					if (resolved && (*resolved.value())->body && !(*resolved.value())->is_coroutine) {
						script_value method_val = *bthunk->payload.bound_dispatch;
						script_value receiver = *bthunk->payload.this_obj;
						return enter_script_method_sliced(f, std::move(method_val), *mdispatch,
						                                  *resolved.value(), &receiver, args_base - 1,
						                                  args_base, argc, site);
					}
				}
			}
		}
		if (direct_fn) {
			// Stateless ref binding: the call site travels as an argument; bind_parameters
			// resolves ref args against the caller frame/env directly (no metadata channel)
			try {
				return push_script_frame(f, std::move(stack_[args_base - 1]), *direct_fn,
				                         stack_, args_base, argc, &site);
			} catch (const script_exception& e) {
				// pre-record throws leave callee+args on the stack; drop them like the
				// pooled path already had (bind-time throws pop-core'd them already)
				stack_.truncate(args_base - 1);
				active_exception_value_ = script_value(std::string(e.what()), engine_);
				current_exception_ = e;
				is_unwinding_ = true;
				stack_.push_back(make_null());
				return {};
			} catch (const std::exception& e) {
				stack_.truncate(args_base - 1);
				active_exception_value_ = script_value(std::string(e.what()), engine_);
				current_exception_ = script_exception(e.what());
				is_unwinding_ = true;
				stack_.push_back(make_null());
				return {};
			}
		}
	}

	auto arguments = acquire_arg_vector(argc);
	arg_vector_return arg_return{this, &arguments};
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		arguments.push_back(std::move(stack_[base + i]));
	}
	stack_.truncate(base);

	script_value callee = std::move(stack_.back());
	stack_.pop_back();

	if (!callee.is_function()) {
		const uint64_t name_id = site.call_node != k_invalid_u32
			? callee_name_symbol(static_cast<call_expr*>(f.code->nodes[site.call_node].get())->callee.get()) : 0;
		return raise_not_callable(name_id, callee);
	}

	// Inline the invoke tail: an extra callee frame per script call would blow the
	// native stack before JAI_MAX_CALL_DEPTH is reached in Debug builds
	const script_function& func = callee.as_function();
	const auto* thunk = func.target<script_callable_thunk>();
	const bool direct = thunk && thunk->eng == engine_ &&
	                    thunk->payload.kind == script_callable::kind_type::function && thunk->payload.fn;
	// Bound-method thunk fallback (args already collected — the slice entry lives in
	// this op's PEEK section above; this vector path serves arity-window shapes)
	if (thunk && thunk->eng == engine_ &&
	    thunk->payload.kind == script_callable::kind_type::bound_method &&
	    thunk->payload.this_obj && thunk->payload.bound_dispatch &&
	    !parallel_worker_) {
#ifdef JAISCRIPT_VM_PROFILE
		profile_bound_method_paths_[0]++;
#endif
		const auto* dispatch = thunk->payload.bound_dispatch->as_function().target<script_method_dispatch>();
		if (dispatch && dispatch->eng == engine_) {
			auto resolved = dispatch->cls->resolve_method_overload(dispatch->name_id, arguments);
			if (resolved && resolved.value()->body && !resolved.value()->is_coroutine) {
#ifdef JAISCRIPT_VM_PROFILE
				profile_bound_method_paths_[1]++;
#endif
				script_value method_val = *thunk->payload.bound_dispatch;
				script_value receiver = *thunk->payload.this_obj;
				return enter_script_method(f, std::move(method_val), *dispatch,
				                           resolved.value(), std::move(receiver), arguments, site);
			}
		}
	}
#ifdef JAISCRIPT_VM_PROFILE
	if (thunk && thunk->payload.kind == script_callable::kind_type::bound_method) {
		profile_bound_method_paths_[2]++;
	} else if (!direct) {
		profile_bound_method_paths_[3]++;
	}
#endif
	std::optional<checked_result<script_value>> callOutcome;
	try {
		if (direct) {
			callOutcome.emplace(call_script_function(*thunk->payload.fn, arguments, &site, &f, f.code));
		} else {
			// Opaque invoke: arm the pending call-site context for the callee's bind
			pending_call_site saved_pending = pending_site_ctx_;
			pending_site_ctx_ = arguments.empty() ? saved_pending
			                                      : pending_call_site{&site, &f, f.code};
			struct pending_restore {
				vm_backend* vm; pending_call_site saved;
				~pending_restore() { vm->pending_site_ctx_ = saved; }
			} restore_pending{this, saved_pending};
			callOutcome.emplace(func(arguments));
		}
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
	checked_result<script_value>& result_checked = *callOutcome;

	if (!result_checked) {
		return raise_from(result_checked);
	}

	stack_.push_back(std::move(result_checked.value()));
	return {};
}

template <typename Call>
op_status vm_backend::guarded_native_call(Call&& call) {
	try {
		auto result = call();
		if (!result) {
			return raise_from(result);
		}
		stack_.push_back(std::move(result.value()));
		return {};
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
}

op_status vm_backend::invoke_callee(frame& f, script_value&& callee, std::vector<script_value>& arguments, const call_site& site) {
	const size_t argc = arguments.size();

	const script_function& func = callee.as_function();
	// Own-trampoline fast path: dispatch straight into the call machinery (Squirrel's
	// OP_CALL→StartCall shape), skipping std::function + backend lookup + virtual hop
	const auto* thunk = func.target<script_callable_thunk>();
	const bool direct = thunk && thunk->eng == engine_ &&
	                    thunk->payload.kind == script_callable::kind_type::function && thunk->payload.fn;
	const bool in_loop = direct && arguments.size() == thunk->payload.fn->parameters().size();

	// Bound-method thunk (the env method-fallback mint for bare sibling calls): enter
	// the method IN-LOOP through the same machinery as op_call_method — no lambda hop,
	// no per-call payload build, no execute_callable env acquire + native re-entry.
	// The frame machinery replicates make_bound_method's keep-alive anchor
	// (call_record::method_result_anchor). Coroutine methods, foreign engines, and
	// parallel workers fall to the opaque path (identical behavior; the worker rules
	// live there). A resolution decline falls through for the identical error.
	if (thunk && thunk->eng == engine_ &&
	    thunk->payload.kind == script_callable::kind_type::bound_method &&
	    thunk->payload.this_obj && thunk->payload.bound_dispatch &&
	    !parallel_worker_) {
		const auto* dispatch = thunk->payload.bound_dispatch->as_function().target<script_method_dispatch>();
		if (dispatch && dispatch->eng == engine_) {
			auto resolved = dispatch->cls->resolve_method_overload(dispatch->name_id, arguments);
			if (resolved && resolved.value()->body && !resolved.value()->is_coroutine) {
#ifdef JAISCRIPT_VM_PROFILE
				profile_bound_method_paths_[1]++;
#endif
				script_value method_val = *thunk->payload.bound_dispatch;
				script_value receiver = *thunk->payload.this_obj;
				return enter_script_method(f, std::move(method_val), *dispatch,
				                           resolved.value(), std::move(receiver), arguments, site);
			}
		}
	}
	// Constructor thunk (class-name calls: Shot(x)): construction protocol native,
	// ctor body in-loop - the same door as op_new. Workers stay opaque (their rules
	// live in execute_callable); coroutine ctors take the native tail inside.
	if (thunk && thunk->eng == engine_ &&
	    thunk->payload.kind == script_callable::kind_type::constructor &&
	    !parallel_worker_) {
		auto script_cls = thunk->payload.cls && thunk->payload.cls->is_script_class()
			? std::static_pointer_cast<script_class_definition>(thunk->payload.cls)
			: std::shared_ptr<script_class_definition>();
		if (script_cls) {
			if (script_cls->get_constructor_asts().empty()) {
				auto result = construct_default_instance(script_cls, arguments);
				if (!result) {
					return raise_from(result);
				}
				stack_.push_back(std::move(result.value()));
				return {};
			}
			if (thunk->payload.definition_env) {
				return enter_constructor_in_loop(f, script_cls, thunk->payload.definition_env,
				                                 arguments, nullptr);
			}
		}
	}

#ifdef JAISCRIPT_VM_PROFILE
	if (thunk && thunk->payload.kind == script_callable::kind_type::bound_method) {
		profile_bound_method_paths_[2]++;
	} else if (!direct) {
		profile_bound_method_paths_[3]++;
	}
#endif

	if (in_loop) {
		// Same in-loop entry as exec_call; op_call_method's dispatch case consumes switch_to_
		try {
			return push_script_frame(f, std::move(callee), *thunk->payload.fn,
			                         arguments, 0, argc, &site);
		} catch (const script_exception& e) {
			active_exception_value_ = script_value(std::string(e.what()), engine_);
			current_exception_ = e;
			is_unwinding_ = true;
			stack_.push_back(make_null());
			return {};
		} catch (const std::exception& e) {
			active_exception_value_ = script_value(std::string(e.what()), engine_);
			current_exception_ = script_exception(e.what());
			is_unwinding_ = true;
			stack_.push_back(make_null());
			return {};
		}
	}
	std::optional<checked_result<script_value>> callOutcome;
	try {
		if (direct) {
			callOutcome.emplace(call_script_function(*thunk->payload.fn, arguments, &site, &f, f.code));
		} else {
			// Opaque invoke: arm the pending call-site context for the callee's bind
			pending_call_site saved_pending = pending_site_ctx_;
			pending_site_ctx_ = arguments.empty() ? saved_pending
			                                      : pending_call_site{&site, &f, f.code};
			struct pending_restore {
				vm_backend* vm; pending_call_site saved;
				~pending_restore() { vm->pending_site_ctx_ = saved; }
			} restore_pending{this, saved_pending};
			callOutcome.emplace(func(arguments));
		}
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
	checked_result<script_value>& result_checked = *callOutcome;

	if (!result_checked) {
		return raise_from(result_checked);
	}

	stack_.push_back(std::move(result_checked.value()));
	return {};
}

op_status vm_backend::exec_func_decl(frame& f, const vm_instruction& ins) {
	const function_proto& proto = f.code->function_protos[ins.a];
	engine* eng = engine_;

	if (proto.is_coroutine) {
		// Calling a coroutine function mints a handle (same interned identity as the
		// interpreter) and does NOT run the body; resume drives it.
		auto func_decl_ptr = proto.decl;
		auto closure_env = environment_;
		// Nested (function-local) coroutine: the resumed body runs on its own frame,
		// so enclosing-frame slot locals are snapshot by value into a capture env
		// (closure-style; the chain also pins the declaration env against pool reuse)
		if (!proto.outer_slot_plan.empty() && f.locals && !f.top_level) {
			const size_t outer_slot_count = frame_slot_count(f);
			std::shared_ptr<environment> captureEnv;
			for (const auto& [sym, slot] : proto.outer_slot_plan) {
				if (slot < outer_slot_count) {
					if (script_value* slot_val = frame_slot(f, slot)) {
						if (!captureEnv) {
							// Parent = global (interpreter parity + the lambda-documented
							// pooled-env recycle hazard for escaping handles)
							auto global = engine_ ? engine_->get_global_environment() : nullptr;
							captureEnv = std::make_shared<environment>(global, symbolizer_);
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_births_[4];
#endif
						}
						captureEnv->define(sym, vm_clone_for_capture(slot_val->deref(), symbolizer_));
					}
				}
			}
			if (captureEnv) {
				closure_env = captureEnv;
			}
		}
		uint64_t coroutine_type_id = coroutine_handle_type_id_;
		script_value functionValue = script_value::make_function(
			[eng, coroutine_type_id, func_decl_ptr, closure_env](const std::vector<script_value>& args) -> checked_result<script_value> {
				auto handle = std::make_shared<coroutine_handle>(eng);
				handle->set_function(func_decl_ptr, args, closure_env);
				return script_value::make_coroutine_handle(coroutine_type_id, std::static_pointer_cast<void>(handle), eng);
			}, engine_);
		environment_->define(proto.decl->name_id, functionValue);
		return {};
	}

	script_callable payload;
	payload.kind = script_callable::kind_type::function;
	payload.fn = proto.fn;
	script_value functionValue = script_value::make_function(
		script_function(script_callable_thunk{eng, std::move(payload)}), engine_);

	environment_->define(proto.decl->name_id, functionValue);
	engine_->bump_definition_epoch();
	return {};
}

op_status vm_backend::exec_closure(frame& f, const vm_instruction& ins) {
	const closure_proto& proto = f.code->closure_protos[ins.a];
	auto closure_env = environment_;

	// Runtime filter over the compile-time plan (parity: only live outer-frame slots capture)
	std::vector<std::pair<uint64_t, size_t>> outer_refs;
	if (f.locals && !f.top_level) {
		const size_t outer_slot_count = frame_slot_count(f);
		for (const auto& [sym, slot] : proto.outer_slot_plan) {
			if (slot < outer_slot_count) {
				outer_refs.emplace_back(sym, slot);
			}
		}
	}

	bool has_explicit_captures = !proto.captures.empty();
	const bool has_default_capture = proto.default_capture != static_cast<uint8_t>(lambda_expr::capture_default::none);
	const bool capture_by_ref_default = proto.default_capture == static_cast<uint8_t>(lambda_expr::capture_default::by_reference);

	if (!outer_refs.empty()) {
		has_explicit_captures = true;
	}

	const bool needs_capture_env = has_explicit_captures || (has_default_capture && !proto.used_variables.empty());

	std::shared_ptr<environment> final_closure_env;

	if (needs_capture_env) {
		auto global = engine_ ? engine_->get_global_environment() : nullptr;
		auto captureEnv = std::make_shared<environment>(global, symbolizer_);
#ifdef JAISCRIPT_VM_PROFILE
		++profile_env_births_[4];
#endif

		if (has_default_capture && !proto.used_variables.empty()) {
			for (uint64_t var_id : proto.used_variables) {
				bool is_overridden = false;
				for (const auto& capture : proto.captures) {
					if (capture.symbol_id == var_id) {
						is_overridden = true;
						break;
					}
				}
				if (is_overridden) continue;

				// The capture source in the runtime's order (detail/capture_ladder.hpp): the
				// outer frame's slot, the member scope, then the env chain - a same-named
				// global never wins over a local or a field. [&] cell-shares the source
				// (escape-legal; share_env_ref also demotes counted-for cached pointers),
				// [=] snapshots it; a hybrid's C++ property follows the field rule through
				// its accessors (reference_model.md CPP-MEMBER / PROPERTY). KEEP BYTE-PARALLEL
				// with the interpreter's visit_lambda_expr default-capture loop.
				script_value* slot_val = nullptr;
				detail::member_scope_ctx scope;
				if (f.locals && !f.top_level) {
					for (const auto& [sym, slot] : outer_refs) {
						if (sym == var_id) {
							slot_val = frame_slot(f, slot);
							break;
						}
					}
					ensure_frame_member_scope(f, environment_.get());
					scope = detail::frame_member_scope(*f.locals);
				}
				auto src = detail::resolve_capture_source(slot_val, environment_.get(), scope, var_id);
				if (src.cell) {
					if (capture_by_ref_default) {
						captureEnv->define(var_id, share_env_ref(*src.cell));
					} else {
						captureEnv->define(var_id, vm_clone_for_capture(src.cell->deref(), symbolizer_));
					}
				} else if (src.what == detail::capture_source::kind::cpp_property) {
					auto read = capture_by_ref_default
						? detail::bind_cpp_property_ref(*f.locals->scope_this, var_id, engine_, symbolizer_)
						: detail::read_bare_cpp_property(*f.locals->scope_this, src.property.instance,
						                                 src.property.method_value, src.property.getter_id, var_id);
					if (!read) {
						return raise_from(read);
					}
					captureEnv->define(var_id, capture_by_ref_default ? std::move(read.value())
					                                                  : vm_clone_for_capture(read.value(), symbolizer_));
				}
			}
		}

		for (const auto& capture : proto.captures) {
			script_value* slot_val = nullptr;
			if (f.locals && !f.top_level) {
				if (capture.outer_slot != SIZE_MAX && capture.outer_slot < frame_slot_count(f)) {
					slot_val = frame_slot(f, capture.outer_slot);
				}
				if (!slot_val) {
					for (const auto& [sym, slot] : outer_refs) {
						if (sym == capture.symbol_id) {
							slot_val = frame_slot(f, slot);
							break;
						}
					}
				}
			}
			if (!slot_val && capture.symbol_id == this_id_) {
				// 'this' - the method env provides it via get() even though contains()
				// might return false
				if (!environment_->contains(this_id_) && !environment_->get(this_id_)) {
					return raise_(make_error_code(runtime_error_code::capture_undefined_variable),
						"Cannot capture undefined variable '{0}'", capture.symbol_id);
				}
				if (capture.by_reference) {
					script_value* targetPtr = environment_->get_value_ptr(this_id_);
					if (!targetPtr) {
						return raise_(make_error_code(runtime_error_code::capture_reference_failed),
							"Cannot capture variable '{0}' by reference", capture.symbol_id);
					}
					captureEnv->define(this_id_, share_env_ref(*targetPtr));
				} else {
					auto capture_result = environment_->get(this_id_);
					if (!capture_result) return raise_from(capture_result);
					captureEnv->define(this_id_, vm_clone_for_capture(capture_result.value(), symbolizer_));
				}
				continue;
			}
			// The capture source in the runtime's order (detail/capture_ladder.hpp): the
			// outer frame's slot, the function's own scopes, then the env chain - a member
			// shadows the chain and is not a capturable variable
			detail::member_scope_ctx scope;
			if (f.locals && !f.top_level) {
				ensure_frame_member_scope(f, environment_.get());
				scope = detail::frame_member_scope(*f.locals);
			}
			auto src = detail::resolve_capture_source(slot_val, environment_.get(), scope, capture.symbol_id);
			if (!src.is_variable()) {
				return raise_(make_error_code(runtime_error_code::capture_undefined_variable),
					"Cannot capture undefined variable '{0}'", capture.symbol_id);
			}
			if (capture.by_reference) {
				// Cell share (escape-legal, C++ [&x] semantics): the storage boxes in
				// place; the closure and the original share the cell
				captureEnv->define(capture.symbol_id, share_env_ref(*src.cell));
			} else {
				captureEnv->define(capture.symbol_id, vm_clone_for_capture(src.cell->deref(), symbolizer_));
			}
		}

		if (f.locals && !f.top_level) {
			for (const auto& [sym, slot] : outer_refs) {
				// Already captured above - OWN storage only, a same-named global behind
				// the parent is not a capture
				if (captureEnv->get_own_value_ptr(sym)) {
					continue;
				}
				script_value* slot_val = frame_slot(f, slot);
				if (slot_val) {
					if (capture_by_ref_default) {
						// C++ [&] semantics: share the cell (slot boxes in place)
						captureEnv->define(sym, share_env_ref(*slot_val));
					} else {
						// Automatic / [=] capture of a local: by value
						captureEnv->define(sym, vm_clone_for_capture(slot_val->deref(), symbolizer_));
					}
				}
			}
		}

		if (proto.captures_this) {
			auto this_result = captureEnv->get(this_id_);
			if (!this_result) {
				return raise_(make_error_code(runtime_error_code::capture_reference_failed),
					"Failed to capture 'this' reference");
			}
			script_value this_obj = std::move(this_result.value());

			auto method_env = std::make_shared<environment>(closure_env, symbolizer_, this_obj);
			method_env->define(this_id_, this_obj);

			// The capture env's OWN storage only, never a global behind its parent
			for (const auto& capture : proto.captures) {
				if (capture.symbol_id == this_id_) continue;
				if (script_value* captured = captureEnv->get_own_value_ptr(capture.symbol_id)) {
					method_env->define(capture.symbol_id, *captured);
				}
			}

			if (has_default_capture) {
				for (uint64_t var_id : proto.used_variables) {
					if (var_id == this_id_) continue;
					if (script_value* captured = captureEnv->get_own_value_ptr(var_id)) {
						method_env->define(var_id, *captured);
					}
				}
			}

			final_closure_env = method_env;
		} else {
			final_closure_env = captureEnv;
		}
	} else {
		final_closure_env = closure_env;
	}

	// Capture-free closures mint ONCE per node (closure_env = nullptr each time);
	// capturing ones share the node's parameter storage
	std::shared_ptr<script_defined_function> lambdaFunc = needs_capture_env ? nullptr : proto.lambda->mint_cache;
	if (!lambdaFunc) {
		lambdaFunc = std::make_shared<script_defined_function>(
			"<lambda>",
			shared_parameters_for(proto.lambda.get()),
			proto.lambda->return_type,
			proto.body,
			needs_capture_env ? final_closure_env : nullptr);
		if (!needs_capture_env) { proto.lambda->mint_cache = lambdaFunc; }
	}

	// Pre-compiled body chunk enters the cache so execute_callable finds it by body identity
	auto cache_it = chunk_cache_.find(proto.body.get());
	if (cache_it == chunk_cache_.end()) {
		chunk_cache_.emplace(proto.body.get(), chunk_cache_entry{proto.body_chunk, proto.body});
	}

	script_callable payload;
	payload.kind = script_callable::kind_type::function;
	payload.fn = lambdaFunc;
	script_function funcWrapper = script_callable_thunk{engine_, std::move(payload)};

	stack_.push_back(script_value::make_function(funcWrapper, engine_));
	return {};
}

// ============================================================
// Members, methods, classes (mirror interpreter semantics)
// ============================================================

void vm_backend::raise_script_exception(const std::string& message, const source_location& location) {
	active_exception_value_ = script_value(message, engine_);
	current_exception_ = script_exception(message, location);
	is_unwinding_ = true;
}

checked_result<script_value> vm_backend::eval_expression(const expression_ptr& expr, const std::shared_ptr<environment>& env) {
	std::shared_ptr<chunk> compiled;
	auto it = expr_chunk_cache_.find(expr.get());
	if (it != expr_chunk_cache_.end()) {
		compiled = it->second.compiled;
	} else {
		compiled = compiler_.compile_standalone_expression(expr);
		expr_chunk_cache_.emplace(expr.get(), expr_chunk_cache_entry{compiled, expr});
	}

	auto saved_env = environment_;
	if (env) {
		environment_ = env;
	}

	frame f;
	f.code = compiled.get();
	f.pin = compiled;
	f.ip = 0;
	f.locals = nullptr;
	f.entry_env = environment_;
	f.stack_base = stack_.size();
	f.top_level = true;

	struct expression_scope_restore {
		vm_backend* self;
		std::shared_ptr<environment>& previous_env;
		size_t stack_base;
		~expression_scope_restore() {
			if (self->stack_.size() > stack_base) { self->stack_.truncate(stack_base); }
			self->environment_ = previous_env;
		}
	} scope_restore{this, saved_env, f.stack_base};

	auto outcome = [&]() -> checked_result<script_value> {
		frame_guard guard(this, &f);
		auto r = run(f);
		if (!r) {
			return r.error_value();
		}
		if (stack_.size() > f.stack_base) {
			return std::move(stack_.back());
		}
		return make_null();
	}();

	return outcome;
}

op_status vm_backend::exec_this(frame& f, const vm_instruction& ins) {
	if (script_value* tp = current_this(f)) {
		stack_.push_back(*tp);
		return {};
	}
	return raise_(make_error_code(runtime_error_code::this_outside_method),
		"'this' can only be used inside methods");
}

op_status vm_backend::exec_super(frame& f, const vm_instruction& ins) {
	script_value* tp = current_this(f);
	if (!tp) {
		return raise_(make_error_code(runtime_error_code::super_outside_method),
			"'super' can only be used inside methods");
	}
	script_value this_value = *tp;
	if (this_value.is_null()) {
		return raise_(make_error_code(runtime_error_code::super_outside_method),
			"'super' can only be used inside methods");
	}
	stack_.push_back(std::move(this_value));
	return {};
}

op_status vm_backend::exec_from_this(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.b];
	script_value* tp = current_this(f);
	if (!tp) {
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"{0}() can only be called from within a method", sym);
	}
	script_value this_val = *tp;
	if (!this_val.is_object()) {
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"{0}() can only be called from within a method", sym);
	}
	if (ins.a) {
		auto weak_result = script_value::make_weak_ptr(this_val, engine_);
		if (!weak_result) {
			return raise_from(weak_result);
		}
		stack_.push_back(std::move(weak_result.value()));
	} else {
		stack_.push_back(std::move(this_val));
	}
	return {};
}

op_status vm_backend::member_access_value(frame& f, const script_value& raw_object, member_expr* expr, script_value& out,
                                          bool as_callee) {
	script_value objectValue = raw_object.deref();

	if (expr->null_safe && objectValue.is_null()) {
		out = make_null();
		return {};
	}

	// Worker direct-field read (parallel_for v1): probing getters/methods COPIES
	// shared method values out of class_definition::methods_ - a cross-worker count
	// race on the non-atomic strong_ptr. Fields live on the (exclusively owned)
	// instance, so read them directly; anything that is not a backing field would
	// need method machinery - wall it (unsafe mode included: the count is the runtime's).
	if (parallel_worker_ && objectValue.raw_storage_index() == script_value::TYPEID_OBJECT) [[unlikely]] {
		auto& holder = objectValue.unchecked_get_object_storage();
		if (holder && holder->is_class_instance_wrapper && holder->data) {
			auto* inst = static_cast<class_instance*>(holder->data.get());
			if (script_value* field = inst->find_field_value(expr->member_id)) {
				out = field->is_reference() ? field->deref() : *field;
				return {};
			}
			return raise_(make_error_code(runtime_error_code::unsupported_operation),
				"only direct field access on class instances is allowed in a parallel body");
		}
	}

	if (expr->object && expr->object->get_type() == node_type::super_expr) {
		if (!objectValue.is_object()) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		auto objHolder = objectValue.get_object_holder();
		if (!objHolder || !objHolder->data) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
		if (!instance) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		auto class_def = instance->get_class_definition();
		if (!class_def) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		auto parent_def = class_def->get_parent();
		if (!parent_def) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		if (parent_def->chain_has_nonpublic()) [[unlikely]] {
			VM_TRY(detail::enforce_member_access(parent_def.get(), expr->member_id,
			                                            frame_access_context(f)));
		}
		script_value method = make_null();
		try {
			method = parent_def->get_method(expr->member_id);
		} catch (const runtime_error& e) {
			raise_script_exception(e.what(), expr->location);
			out = make_null();
			return {};
		}
		if (method.is_null()) {
			return raise_(make_error_code(runtime_error_code::member_not_found),
				"parent class {0} has no member '{1}'", parent_def->get_type_id(), expr->member_id);
		}
		out = make_bound_method(objectValue, method);
		return {};
	}

	if (objectValue.is_object()) {
		auto objHolder = objectValue.get_object_holder();
		if (objHolder && objHolder->type_id == coroutine_handle_type_id_) {
			auto handle = std::static_pointer_cast<coroutine_handle>(objHolder->data);
			if (expr->member_id == resume_id_) {
				auto captured_handle = handle;
				auto* eng = engine_;
				script_function resume_method = [captured_handle, eng](const std::vector<script_value>&) -> checked_result<script_value> {
					return captured_handle->resume(eng);
				};
				out = script_value::make_function(resume_method, engine_);
				return {};
			}
			if (expr->member_id == done_id_) {
				auto captured_handle = handle;
				auto* eng = engine_;
				script_function done_method = [captured_handle, eng](const std::vector<script_value>&) -> checked_result<script_value> {
					return script_value(captured_handle->done(), eng);
				};
				out = script_value::make_function(done_method, engine_);
				return {};
			}
			return raise_(make_error_code(runtime_error_code::member_not_found),
				"coroutine_handle has no member '{0}'", expr->member_id);
		}
	}

	if (objectValue.is_string()) {
		auto methodIt = builtins_.string_methods.find(expr->member_id);
		if (methodIt != builtins_.string_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				std::vector<script_value> scratch;
				return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		std::string member_str(expr->member);
		raise_script_exception("string has no member '" + member_str + "'", expr->location);
		out = make_null();
		return {};
	}

	if (objectValue.is_array()) {
		auto methodIt = builtins_.array_methods.find(expr->member_id);
		if (methodIt != builtins_.array_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				std::vector<script_value> scratch;
				return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		std::string member_str(expr->member);
		raise_script_exception("array has no member '" + member_str + "'", expr->location);
		out = make_null();
		return {};
	}

	if (objectValue.is_map()) {
		auto methodIt = builtins_.map_methods.find(expr->member_id);
		if (methodIt != builtins_.map_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				std::vector<script_value> scratch;
				return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		auto key = script_value(std::string(expr->member), engine_);
		auto& map_storage = const_cast<script_value&>(objectValue).get_map_storage();
		if (map_storage) {
			auto found = map_storage->find(key);
			if (found != map_storage->end()) {
				out = found->second;
				return {};
			}
		}
		return raise_(make_error_code(runtime_error_code::member_not_found),
			"map has no member or key '{0}'", expr->member_id);
	}

	if (objectValue.is_weak_ptr()) {
		auto methodIt = builtins_.weak_ptr_methods.find(expr->member_id);
		if (methodIt != builtins_.weak_ptr_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				std::vector<script_value> scratch;
				return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		return raise_(make_error_code(runtime_error_code::member_not_found),
			"weak_ptr has no member '{0}'", expr->member_id);
	}

	if (objectValue.get_type_info() &&
	    objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
		auto methodIt = builtins_.shared_ptr_methods.find(expr->member_id);
		if (methodIt != builtins_.shared_ptr_methods.end()) {
			// A user class method WINS over a same-named builtin handle method (Dev ruling
			// 2026-07): only mint the builtin when the underlying class does NOT define it.
			bool user_shadows = false;
			if (auto ci = objectValue.get_class_instance()) {
				if (auto* cd = ci->get_class_definition()) {
					user_shadows = cd->defines_method(expr->member_id);
				}
			}
			if (!user_shadows) {
				const builtin_method& method = methodIt->second;
				script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
					std::vector<script_value> scratch;
					return method(ctx, capturedValue, detail::deref_builtin_args(args, scratch));
				};
				out = script_value::make_function(boundMethod, engine_);
				return {};
			}
		}
	}

	if (expr->member_id == same_as_id_) {
		engine* eng = engine_;
		script_function same_as_method = [eng, capturedValue = objectValue](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
			if (args.size() != 1) {
				return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "same_as() takes exactly 1 argument");
			}

			const script_value& other_raw = args[0];
			script_value self_val = capturedValue.deref();
			script_value other_val = const_cast<script_value&>(other_raw).deref();

			// S8: bound values decode-compare like the old shadow indices (two aliases of one
			// C++ int stay same_as-true). KEEP BYTE-PARALLEL with the interpreter same_as lambda.
			if (self_val.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
				self_val = self_val.bound_decoded_temp();
			if (other_val.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
				other_val = other_val.bound_decoded_temp();

			if (self_val.is_null() && other_val.is_null()) {
				return script_value(true, eng);
			}
			if (self_val.is_null() || other_val.is_null()) {
				return script_value(false, eng);
			}

			auto self_idx = self_val.raw_storage_index();
			auto other_idx = other_val.raw_storage_index();
			if (self_idx != other_idx) {
				return script_value(false, eng);
			}

			switch (self_idx) {
				case script_value::TYPEID_INT:
					return script_value(self_val.unchecked_as_int() == other_val.unchecked_as_int(), eng);
				case script_value::TYPEID_FLOAT:
					return script_value(self_val.unchecked_as_float() == other_val.unchecked_as_float(), eng);
				case script_value::TYPEID_BOOL:
					return script_value(self_val.unchecked_as_bool() == other_val.unchecked_as_bool(), eng);
				case script_value::TYPEID_STRING:
					return script_value(self_val.unchecked_as_string() == other_val.unchecked_as_string(), eng);
				case script_value::TYPEID_OBJECT:
				case script_value::TYPEID_SHARED_PTR:
					break;
				default:
					return script_value(false, eng);
			}

			auto self_holder = self_val.get_object_holder();
			auto other_holder = other_val.get_object_holder();
			return script_value(self_holder.get() == other_holder.get(), eng);
		};
		out = script_value::make_function(same_as_method, engine_);
		return {};
	}

	if (!objectValue.is_object()) {
		if (objectValue.is_null()) {
			return raise_(make_error_code(runtime_error_code::member_access_on_null), null_member_message(expr->object.get(), as_callee), expr->member_id, null_member_receiver_symbol(expr->object.get()));
		}
		return raise_(make_error_code(runtime_error_code::member_not_found),
			scalar_member_message, base_type_ids_[objectValue.type()], expr->member_id);
	}

	auto target = resolve_member_target(objectValue);
	if (!target) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	uint64_t member_id = expr->member_id;

	if (target.class_def && target.class_def->chain_has_nonpublic()) [[unlikely]] {
		VM_TRY(detail::enforce_member_access(target.class_def, member_id,
		                                            frame_access_context(f)));
	}

	// The nearest USER _get_ in the chain wins over the field; the synthesized accessor is
	// skipped - its body IS the field rung below (KEEP BYTE-PARALLEL with the interpreter)
	if (target.class_def && target.class_def->has_property_getters()) {
		try {
			uint64_t getter_id = expr->getter_id;
			if (getter_id == UINT64_MAX) {
				auto [id, _] = symbolizer_->get_getter_id_with_view(member_id);
				getter_id = id;
				expr->getter_id = getter_id;
			}
			script_value getter = target.user_accessor(getter_id);
			if (!getter.is_null() && !getter.is_invalid() && getter.is_function()) {
				// The shared read (retired-layer report, receiver pin): the bare-name rung
				// of detail/member_scope.hpp runs the identical kernel
				auto result = detail::read_cpp_property(objectValue, target.class_def, target.instance.get(),
				                                        getter, getter_id);
				if (!result) {
					return raise_from(result);
				}
				out = std::move(result.value());
				return {};
			}
		} catch (const std::exception&) {
			if (target.instance && target.instance->script_layer_retired()) { throw; }
		}
	}

	if (target.has_field(member_id)) {
		out = target.get_field(member_id);
		return {};
	}

	script_value method = target.method(member_id);
	if (!method.is_null() && !method.is_invalid()) {
		out = make_bound_method(objectValue, method);
		return {};
	}

	if (target.class_def && target.class_def->is_transparent_wrapper()) {
		script_value mutable_obj = objectValue;
		script_value unwrapped = target.class_def->unwrap(mutable_obj);
		if (!unwrapped.is_null() && unwrapped.is_object()) {
			auto unwrapped_target = resolve_member_target(unwrapped);
			if (unwrapped_target) {
				if (unwrapped_target.has_field(member_id)) {
					out = unwrapped_target.get_field(member_id);
					return {};
				}
				script_value unwrapped_method = unwrapped_target.method(member_id);
				if (!unwrapped_method.is_null() && !unwrapped_method.is_invalid()) {
					out = make_bound_method(unwrapped, unwrapped_method);
					return {};
				}
			}
		}
	}

	{
		const std::string& cls = target.class_name();
		raise_script_exception((cls.empty() ? "object" : cls) + " has no member '" + std::string(expr->member) + "'", expr->location);
	}
	out = make_null();
	return {};
}

op_status vm_backend::static_member_value(frame& f, member_expr* expr, script_value& out) {
	identifier_expr* ident_expr = nullptr;
	if (expr->object->get_type() == node_type::identifier_expr) {
		ident_expr = static_cast<identifier_expr*>(expr->object.get());
	}
	std::string name;
	uint64_t name_id;

	if (ident_expr) {
		name = ident_expr->name;
		name_id = ident_expr->symbol_id;
		if (name_id == UINT64_MAX) {
			name_id = symbolizer_->intern(name);
			ident_expr->symbol_id = name_id;
		}
	} else if (expr->object->get_type() == node_type::member_expr) {
		std::function<std::string(expression*)> build_namespace_path = [&](expression* e) -> std::string {
			if (e->get_type() == node_type::identifier_expr) {
				return std::string(static_cast<identifier_expr*>(e)->name);
			} else if (e->get_type() == node_type::member_expr) {
				auto* member = static_cast<member_expr*>(e);
				if (member->is_static) {
					return build_namespace_path(member->object.get()) + "::" + std::string(member->member);
				}
			}
			return "";
		};
		name = build_namespace_path(expr->object.get());
		if (name.empty()) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		name_id = symbolizer_->intern(name);
	} else {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	auto& namespaces = engine_->script_namespaces();
	auto ns_it = namespaces.find(name_id);
	const bool is_namespace = (ns_it != namespaces.end());

	if (!is_namespace && name.find("::") != std::string::npos) {
		size_t last_colon = name.rfind("::");
		std::string potential_ns = name.substr(0, last_colon);
		std::string potential_class = name.substr(last_colon + 2);

		uint64_t ns_id = symbolizer_->intern(potential_ns);
		uint64_t class_id = symbolizer_->intern(potential_class);

		auto ns_check = namespaces.find(ns_id);
		if (ns_check != namespaces.end()) {
			auto class_check = ns_check->second->classes.find(class_id);
			if (class_check != ns_check->second->classes.end()) {
				auto class_def = class_check->second;
				script_value static_method = class_def->get_static_method(expr->member_id, false);
				if (!static_method.is_null()) {
					out = static_method;
					return {};
				}
			}
		}
	}

	if (is_namespace) {
		auto& ns_data = ns_it->second;

		auto func_it = ns_data->functions.find(expr->member_id);
		if (func_it != ns_data->functions.end()) {
			auto overloads = func_it->second;

			std::shared_ptr<class_definition> fallback_class;
			auto [fallback_class_var_id, fallback_class_var_name] = symbolizer_->get_class_var_id_with_view(name_id);
			auto class_var_result = environment_->get(fallback_class_var_id);
			if (class_var_result) {
				script_value class_var = std::move(class_var_result.value());
				if (class_var.is_object()) {
					auto objHolder = class_var.get_object_holder();
					if (objHolder && objHolder->type_name == "class_definition") {
						fallback_class = std::static_pointer_cast<class_definition>(objHolder->data);
					}
				}
			}

			uint64_t namespace_id = name_id;
			uint64_t member_id = expr->member_id;
			engine* eng = engine_;
			auto mint_env = f.entry_env ? environment_ : engine_->get_global_environment();
			script_function namespace_func = [eng, mint_env, overloads, namespace_id, fallback_class, member_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				execution_backend* backend = eng ? eng->get_execution_backend() : nullptr;
				if (!backend) {
					return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
				}

				// Trailing defaults open the window; the candidate using fewest defaults wins
				std::shared_ptr<function_decl> ns_pick;
				size_t ns_defaults = SIZE_MAX;
				for (const auto& fd : overloads) {
					if (arity_accepts(fd->parameters, args.size()) &&
					    fd->parameters.size() - args.size() < ns_defaults) {
						ns_pick = fd;
						ns_defaults = fd->parameters.size() - args.size();
					}
				}
				{
					const auto& func_decl = ns_pick;
					if (func_decl) {
						auto ns_env = std::make_shared<environment>(mint_env, eng->get_symbolizer());

						auto& namespaces = eng->script_namespaces();
						auto ns_it = namespaces.find(namespace_id);
						if (ns_it != namespaces.end()) {
							for (const auto& [var_id, var_value] : ns_it->second->variables) {
								ns_env->define(var_id, var_value);
							}
						}

						script_callable payload;
						payload.kind = script_callable::kind_type::function;
						payload.fn = std::make_shared<script_defined_function>(
							func_decl->name,
							shared_parameters_for(func_decl.get()),
							func_decl->return_type,
							func_decl->body,
							ns_env
						);
						return backend->execute_callable(payload, args);
					}
				}

				if (fallback_class) {
					script_value static_method = fallback_class->get_static_method(member_id, false);
					if (!static_method.is_null() && static_method.is_function()) {
						auto func = static_method.as_function();
						return func(args);
					}
				}

				(void)namespace_id;
				return detail::overload_miss(make_error_code(runtime_error_code::not_a_function),
					member_id, args.data(), args.size(), eng);
			};

			out = script_value::make_function(namespace_func, engine_);
			return {};
		}

		auto var_it = ns_data->variables.find(expr->member_id);
		if (var_it != ns_data->variables.end()) {
			out = var_it->second;
			return {};
		}

		auto class_it = ns_data->classes.find(expr->member_id);
		if (class_it != ns_data->classes.end()) {
			auto ctor_result = environment_->get(expr->member_id);
			if (!ctor_result) {
				return raise_from(ctor_result);
			}
			out = std::move(ctor_result.value());
			return {};
		}
	}

	auto [class_var_id, class_var_view] = symbolizer_->get_class_var_id_with_view(name_id);
	auto class_var_result = environment_->get(class_var_id);
	if (!class_var_result) {
		return raise_from(class_var_result);
	}
	script_value class_var = std::move(class_var_result.value());

	if (!class_var.is_object()) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	auto objHolder = class_var.get_object_holder();
	if (!objHolder || objHolder->type_name != "class_definition") {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

	if (class_def->chain_has_nonpublic()) [[unlikely]] {
		VM_TRY(detail::enforce_member_access(class_def.get(), expr->member_id,
		                                            frame_access_context(f)));
	}

	script_value static_method = class_def->get_static_method(expr->member_id, false);
	if (!static_method.is_null()) {
		out = static_method;
		return {};
	}

	if (const script_value* static_value = class_def->get_static_field_ptr(expr->member_id)) {
		out = *static_value;
		return {};
	}

	auto [getter_id, getter_view] = symbolizer_->get_getter_id_with_view(expr->member_id);
	script_value getter_method = class_def->get_static_method(getter_id, false);
	if (!getter_method.is_null() && getter_method.is_function()) {
		auto func = getter_method.as_function();
		std::vector<script_value> no_args;
		auto result = func(no_args);
		if (!result) {
			return raise_from(result);
		}
		out = std::move(result.value());
		return {};
	}

	return raise_(make_error_code(runtime_error_code::static_member_not_found),
		"class {0} has no static member '{1}'", name_id, expr->member_id);
}

uint32_t vm_backend::member_read_slot_probe(class_definition* cd, member_expr* expr) {
	if (expr->member_id == same_as_id_ || cd->chain_has_nonpublic() ||
	    builtins_.shared_ptr_methods.find(expr->member_id) != builtins_.shared_ptr_methods.end()) {
		return UINT32_MAX;
	}
	const uint32_t fslot = cd->field_slot(expr->member_id);
	if (fslot == class_definition::k_no_field_slot) {
		return UINT32_MAX;
	}
	if (cd->has_property_getters()) {
		uint64_t getter_id = expr->getter_id;
		if (getter_id == UINT64_MAX) {
			auto [gid, _] = symbolizer_->get_getter_id_with_view(expr->member_id);
			getter_id = gid;
			expr->getter_id = getter_id;
		}
		// The SYNTHESIZED accessor is verbatim get_field; a USER _get_/_set_ anywhere in the
		// chain takes the field off the fast lanes (the ladder dispatches to it)
		if (cd->user_accessor(getter_id) ||
		    cd->user_accessor(symbolizer_->get_setter_id_with_view(expr->member_id).first)) {
			return UINT32_MAX;
		}
	}
	return fslot;
}

// Mirrors exec_get_member's structure EXACTLY (site IC hit serves the raw cell, the
// fill round resolves through the ladder once, absent cells fall to the ladder for
// chain-default materialization) at the fused op's own member_ic row — a fused ip is
// never a real GET_MEMBER ip, so the row is free. `out` lands deref'd like
// fused_ident_value's contract; a getter/map/cpp_bound result is scratch-owned.
op_status vm_backend::fused_member_container(frame& f, size_t site, const script_value& base,
                                             uint32_t member_node, std::optional<script_value>& scratch,
                                             const script_value*& out) {
	auto* expr = static_cast<member_expr*>(f.code->nodes[member_node].get());
	if (cached_global_env_) {
		const script_value& objd = base.deref();
		if (objd.raw_storage_index() == script_value::TYPEID_OBJECT) {
			auto& holder = const_cast<script_value&>(objd).unchecked_get_object_storage();
			if (holder && holder->is_class_instance_wrapper && holder->data) {
				auto* inst = static_cast<class_instance*>(holder->data.get());
				if (class_definition* cd = inst->get_class_definition()) {
					auto& code = *f.code;
					if (code.member_ic.empty()) {
						code.member_ic.resize(code.code.size());
					}
					auto& entry = code.member_ic[site];
					if (entry.cd == cd && entry.epoch == cd->method_epoch()) {
						if (entry.slot != UINT32_MAX) {
							if (script_value* v = inst->field_slot_value(entry.slot)) {
								out = &v->deref();
								return {};
							}
						}
					} else {
						const uint32_t slot = member_read_slot_probe(cd, expr);
						entry.cd = cd;
						entry.epoch = cd->method_epoch();
						entry.slot = slot;
						// resolve through the ladder this once; hits serve from here on
					}
				}
			}
		}
	}
	script_value member_out = make_null();
	VM_TRY(member_access_value(f, base, expr, member_out));
	scratch = std::move(member_out);
	out = &scratch->deref();
	return {};
}

op_status vm_backend::exec_get_member(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value object = std::move(stack_.back());
	stack_.pop_back();
	// Field-read site IC (flat classes tier-1 stage B): epoch-validated {class, slot}
	// per ip replaces the resolve ladder with one slot load. The fill probes mirror
	// member_access_value's precedence EXACTLY (same_as, access, sp-builtin-name
	// branch, per-member getter shadow — all epoch-covered); negative entries pin
	// non-cacheable sites off the probes. A hit pushes the RAW cell copy (reference
	// fields stay references, the ladder's field spelling); an absent cell falls to
	// the ladder for chain-default materialization and errors.
	// V-M1 proven field read: node-borne class + shape-epoch guard, present cell (== the
	// member_ic hit's raw cell copy); a miss falls to the IC / ladder below
	if (expr->proven_class && cached_global_env_) {
		if (script_value* cell = detail::proven_field_cell(object, expr->proven_class, expr->proven_epoch,
		                                                   expr->proven_slot, nullptr)) {
			stack_.push_back(*cell);
			return {};
		}
	}
	if (cached_global_env_) {
		script_value& objd = object.deref();
		if (objd.raw_storage_index() == script_value::TYPEID_OBJECT) {
			auto& holder = objd.unchecked_get_object_storage();
			if (holder && holder->is_class_instance_wrapper && holder->data) {
				auto* inst = static_cast<class_instance*>(holder->data.get());
				if (class_definition* cd = inst->get_class_definition()) {
					auto& code = *f.code;
					if (code.member_ic.empty()) {
						code.member_ic.resize(code.code.size());
					}
					auto& entry = code.member_ic[static_cast<size_t>(&ins - code.code.data())];
					if (entry.cd == cd && entry.epoch == cd->method_epoch()) {
						if (entry.slot != UINT32_MAX) {
							if (script_value* v = inst->field_slot_value(entry.slot)) {
#ifdef JAISCRIPT_VM_PROFILE
								profile_get_member_paths_[0]++;
#endif
								stack_.push_back(*v);
								return {};
							}
						}
#ifdef JAISCRIPT_VM_PROFILE
						profile_get_member_paths_[1]++;
#endif
					} else {
						const uint32_t slot = member_read_slot_probe(cd, expr);
						entry.cd = cd;
						entry.epoch = cd->method_epoch();
						entry.slot = slot;
						// resolve through the ladder this once; hits serve from here on
					}
				}
			}
		}
	}
#ifdef JAISCRIPT_VM_PROFILE
	profile_get_member_paths_[2]++;
#endif
	script_value out = make_null();
	VM_TRY(member_access_value(f, object, expr, out));
	stack_.push_back(std::move(out));
	return {};
}

op_status vm_backend::exec_get_static(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value out = make_null();
	VM_TRY(static_member_value(f, expr, out));
	stack_.push_back(std::move(out));
	return {};
}

op_status vm_backend::assign_member(frame& f, const script_value& object_value, member_expr* member, const script_value& value) {
	// Worker direct-field store (parallel_for v1): the setter probe below COPIES a
	// shared method value (cross-worker count race); a backing field's enforce+set is
	// exactly what the auto-setter does, on exclusively-owned instance storage. Any
	// non-field member store needs method machinery - wall it.
	if (parallel_worker_ && object_value.raw_storage_index() == script_value::TYPEID_OBJECT) [[unlikely]] {
		auto& holder = const_cast<script_value&>(object_value).unchecked_get_object_storage();
		if (holder && holder->is_class_instance_wrapper && holder->data) {
			auto* inst = static_cast<class_instance*>(holder->data.get());
			if (member->member_id != UINT64_MAX && inst->find_field_value(member->member_id)) {
				auto enforced = inst->enforce_field_write(member->member_id, clone_for_assignment(value));
				if (!enforced) {
					return raise_from(enforced);
				}
				inst->set_field_unchecked(member->member_id, std::move(enforced).value());
				return {};
			}
			return raise_(make_error_code(runtime_error_code::unsupported_operation),
				"only direct field stores on class instances are allowed in a parallel body");
		}
	}
	auto target = resolve_member_target(object_value);
	if (!target) {
		return raise_(make_error_code(runtime_error_code::type_mismatch),
			"Cannot assign property to non-object value");
	}

	uint64_t member_id = member->member_id != UINT64_MAX
		? member->member_id
		: symbolizer_->intern(member->member);

	if (target.class_def && target.class_def->chain_has_nonpublic()) [[unlikely]] {
		VM_TRY(detail::enforce_member_access(target.class_def, member_id,
		                                            frame_access_context(f)));
	}

	auto [setter_id, setter_view] = symbolizer_->get_setter_id_with_view(member_id);
	script_value setter = target.user_accessor(setter_id);   // a synthesized setter takes the field rung
	if (!setter.is_null() && !setter.is_invalid() && setter.is_function()) {
		const script_function& func = setter.as_function();
		std::vector<script_value> args = {object_value, clone_for_assignment(value)};
		auto result = func(args);
		if (!result) {
			return raise_from(result);
		}
	} else if (target.has_field(member_id)) {
		// Typed fields enforce like locals
		auto enforced = target.instance->enforce_field_write(member_id, clone_for_assignment(value));
		if (!enforced) {
			return raise_from(enforced);
		}
		target.instance->set_field_unchecked(member_id, enforced.value());
	} else {
		const std::string& cls = target.class_name();
		raise_script_exception("Cannot assign to '" + std::string(member->member) + "': " + (cls.empty() ? "object" : cls) + " has no such member", member->location);
		return {};
	}
	return {};
}

op_status vm_backend::exec_set_member(frame& f, const vm_instruction& ins) {
	auto* member = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value objectValue = std::move(stack_.back());
	stack_.pop_back();
	script_value value = std::move(stack_.back());
	stack_.pop_back();

	script_value dereferenced = objectValue.deref();
	if (!dereferenced.is_object()) {
		raise_script_exception("Cannot assign to member of non-object type", member->location);
		stack_.push_back(make_null());
		return {};
	}

	if (auto landed = member_field_write_fast(f, static_cast<size_t>(&ins - f.code->code.data()), ins.b, member, dereferenced, value)) {
		if (*landed == op_status::failed) { return op_status::failed; }
		return land_member_store_result(f, dereferenced, member, std::move(value));
	}

	VM_TRY(assign_member(f, dereferenced, member, value));
	if (is_unwinding_) {
		stack_.push_back(make_null());
		return {};
	}
	return land_member_store_result(f, dereferenced, member, std::move(value));
}

op_status vm_backend::land_member_store_result(frame& f, const script_value& object, member_expr* member, script_value&& rhs) {
	if (!assignment_result_used(f) ||
	    !jai::detail::member_reads_back(object, member->member_id, symbolizer_->get_getter_id_with_view(member->member_id).first, engine_)) {
		stack_.push_back(std::move(rhs));
		return {};
	}
	script_value stored = make_null();
	VM_TRY(member_access_value(f, object, member, stored));
	if (is_unwinding_) {
		stack_.push_back(make_null());
		return {};
	}
	stack_.push_back(clone_for_assignment(stored.deref()));   // C++ copy-init from the lvalue
	return {};
}

std::optional<op_status> vm_backend::member_field_write_fast(frame& f, size_t ins_index, uint32_t proof, member_expr* member,
                                                             const script_value& dereferenced, const script_value& value) {
	// V-M2 proven field write: == the member_ic hit with the conversion gated by the
	// proof (KEEP BYTE-PARALLEL with interpreter I-M2)
	if (member->proven_class && member->proven_field_write && cached_global_env_) {
		if (class_instance* inst = detail::proven_field_instance(dereferenced, member->proven_class, member->proven_epoch)) {
			script_value incoming = clone_for_assignment(value);
			if (proof == proof_same_type && member->proven_field_type &&
			    detail::same_type_payload(incoming, member->proven_field_type)) {
				if (detail::retags_on_field_write(member->proven_field_type)) {
					incoming.set_type_info(type_info_ptr{member->proven_field_type});
				}
			} else {
				auto enforced = inst->enforce_field_write(member->member_id, std::move(incoming));
				if (!enforced) {
					return raise_from(enforced);
				}
				incoming = std::move(enforced).value();
			}
			inst->field_slot_write(member->proven_slot, std::move(incoming));
			return op_status{};
		}
	}
	// Field-store site IC (write-side twin of exec_get_member's): epoch-validated
	// {class, slot} skips the resolve ladder AND the synthesized setter closure —
	// the auto-setter body IS enforce_field_write + set_field_unchecked, replayed
	// here verbatim through the slot. A user _get_/_set_ in the chain keeps the site negative;
	// declared-but-absent cells materialize exactly like set_field_unchecked.
	if (cached_global_env_ &&
	    dereferenced.raw_storage_index() == script_value::TYPEID_OBJECT) {
		auto& holder = const_cast<script_value&>(dereferenced).unchecked_get_object_storage();
		if (holder && holder->is_class_instance_wrapper && holder->data) {
			auto* inst = static_cast<class_instance*>(holder->data.get());
			if (class_definition* cd = inst->get_class_definition()) {
				auto& code = *f.code;
				if (code.member_ic.empty()) {
					code.member_ic.resize(code.code.size());
				}
				auto& entry = code.member_ic[ins_index];
				if (entry.cd == cd && entry.epoch == cd->method_epoch()) {
					if (entry.slot != UINT32_MAX) {
						auto enforced = inst->enforce_field_write(member->member_id,
						                                          clone_for_assignment(value));
						if (!enforced) {
							return raise_from(enforced);
						}
						inst->field_slot_write(entry.slot, std::move(enforced).value());
						return op_status{};
					}
				} else {
					uint32_t slot = UINT32_MAX;
					if (member->member_id != UINT64_MAX && !cd->chain_has_nonpublic()) {
						const uint32_t fslot = cd->field_slot(member->member_id);
						if (fslot != class_definition::k_no_field_slot) {
							auto [setter_id, setter_view] =
								symbolizer_->get_setter_id_with_view(member->member_id);
							if (!cd->user_accessor(setter_id) &&
							    !cd->user_accessor(symbolizer_->get_getter_id_with_view(member->member_id).first)) {
								slot = fslot;   // no user accessor in the chain: the slot IS the ladder's answer
							}
						}
					}
					entry.cd = cd;
					entry.epoch = cd->method_epoch();
					entry.slot = slot;
					// resolve through the ladder this once; hits serve from here on
				}
			}
		}
	}
	return std::nullopt;
}

op_status vm_backend::store_static_target(frame& f, member_expr* member, const script_value& result, script_value* stored_out) {
	auto cls = detail::resolve_static_store_class(member, *environment_, *symbolizer_, frame_access_context(f));
	if (!cls) {
		return raise_from(cls);
	}
	auto stored = detail::store_static_member(*cls.value(), member->member_id, clone_for_assignment(result));
	if (!stored) {
		return raise_from(stored);
	}
	if (stored_out) { *stored_out = stored.value(); }
	return {};
}

op_status vm_backend::exec_set_static(frame& f, const vm_instruction& ins) {
	auto* member = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value value = std::move(stack_.back());
	stack_.pop_back();

	auto cls = detail::resolve_static_store_class(member, *environment_, *symbolizer_, frame_access_context(f));
	if (!cls) {
		return raise_from(cls);
	}
	auto stored = detail::store_static_member(*cls.value(), member->member_id, clone_for_assignment(value));
	if (!stored) {
		return raise_from(stored);
	}
	stack_.push_back(assignment_result_used(f) ? clone_for_assignment(stored.value()) : std::move(stored.value()));   // a consumed static store is a copy of the stored value
	return {};
}

op_status vm_backend::exec_member_compound(frame& f, const vm_instruction& ins) {
	auto* member = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	const uint32_t kind = ins.c & compound_kind_mask;
	const bool old_result = (ins.c & compound_flag_old_result) != 0;
	const bool result_needed = assignment_result_used(f);   // member compounds carry no result flag; the POP after a statement does

	script_value rightValue = std::move(stack_.back());
	stack_.pop_back();
	script_value currentValue = std::move(stack_.back());
	stack_.pop_back();
	// The receiver was evaluated once (op_dup under the read); a :: target carries no
	// object operand (its class resolves in the write-back)
	script_value objectValueRaw = member->is_static ? make_null() : std::move(stack_.back());
	if (!member->is_static) {
		stack_.pop_back();
	}
	currentValue = currentValue.deref();
	script_value postfix_old = old_result ? currentValue : make_null();
	if (ins.c & compound_flag_incdec) {
		const size_t ci = currentValue.raw_storage_index();
		if (ci != script_value::TYPEID_INT && ci != script_value::TYPEID_FLOAT && ci != script_value::TYPEID_CPP_BOUND) {
			return raise_(make_error_code(runtime_error_code::invalid_numeric_operand));
		}
	}

	if (currentValue.is_object()) {
		uint64_t op_symbol_id = 0;
		switch (kind) {
			case compound_plus: op_symbol_id = op_plus_id_; break;
			case compound_minus: op_symbol_id = op_minus_id_; break;
			case compound_star: op_symbol_id = op_star_id_; break;
			case compound_slash: op_symbol_id = op_slash_id_; break;
			case compound_percent: op_symbol_id = op_percent_id_; break;
			default: break;
		}
		if (op_symbol_id != 0) {
			auto custom_result = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
			if (custom_result.has_value()) {
				if (member->is_static) {
					script_value stored_static = make_null();
					VM_TRY(store_static_target(f, member, custom_result.value(), &stored_static));
					stack_.push_back(result_needed ? clone_for_assignment(stored_static) : std::move(custom_result.value()));
					return {};
				}
				script_value objectValue = objectValueRaw.deref();
				if (objectValue.is_object()) {
					VM_TRY(assign_member(f, objectValue, member, custom_result.value()));
					if (is_unwinding_) {
						stack_.push_back(make_null());
						return {};
					}
					if (result_needed) { return land_member_store_result(f, objectValue, member, std::move(custom_result.value())); }
					stack_.push_back(std::move(custom_result.value()));
					return {};
				}
			}
		}
	}

	// S8: a bound-alias member decode-reads LIVE and assign_member stores the detached
	// result (§12.5). KEEP BYTE-PARALLEL with the interpreter member-compound normalization.
	if (currentValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		currentValue = currentValue.bound_decoded_temp();
	if (rightValue.raw_storage_index() == script_value::TYPEID_CPP_BOUND) [[unlikely]]
		rightValue = rightValue.bound_decoded_temp();

	const size_t ci = currentValue.raw_storage_index();
	const size_t ri = rightValue.raw_storage_index();
	script_value resultValue = make_null();

	switch (kind) {
		case compound_plus: {
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_add(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '+='"));
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf + rf, engine_);
			} else if (ci == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
				// engine::memory_cap chokepoint: deny the concat result before it exists
				if (!limits_->memory_charge(sizeof(script_value) + currentValue.unchecked_as_string().size() + rightValue.unchecked_as_string().size())) [[unlikely]] {
					return raise_from(detail::raise_memory_cap(*limits_));
				}
				resultValue = script_value(currentValue.unchecked_as_string() + rightValue.unchecked_as_string(), engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for +=");
			}
			break;
		}
		case compound_minus: {
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_sub(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '-='"));
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf - rf, engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for -=");
			}
			break;
		}
		case compound_star: {
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_mul(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return raise_from(vm_int_overflow_v("Integer overflow in '*='"));
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf * rf, engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for *=");
			}
			break;
		}
		case compound_slash: {
			if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
				return raise_(make_error_code(runtime_error_code::division_by_zero),
					"Division by zero");
			} else if (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0) {
				return raise_(make_error_code(runtime_error_code::division_by_zero),
					"Division by zero");
			}
			if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf / rf, engine_);
			} else {
				return raise_(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for /=");
			}
			break;
		}
		case compound_percent: {
			if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
				return raise_(make_error_code(runtime_error_code::division_by_zero),
					"Modulo by zero");
			}
			VM_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
			break;
		}
		default:
			return raise_(make_error_code(runtime_error_code::unsupported_operation),
				"Unsupported compound assignment operator");
	}

	if (member->is_static) {
		script_value stored_static = make_null();
		VM_TRY(store_static_target(f, member, resultValue, &stored_static));
		stack_.push_back(old_result ? std::move(postfix_old) : result_needed ? clone_for_assignment(stored_static) : std::move(resultValue));
		return {};
	}

	const script_value& objectValue = objectValueRaw.deref();
	if (!objectValue.is_object()) {
		raise_script_exception("Cannot assign to member of non-object type", member->location);
		stack_.push_back(make_null());
		return {};
	}

	if (auto landed = member_field_write_fast(f, static_cast<size_t>(&ins - f.code->code.data()), ins.b, member, objectValue, resultValue)) {
		if (*landed == op_status::failed) { return op_status::failed; }
	} else {
		VM_TRY(assign_member(f, objectValue, member, resultValue));
		if (is_unwinding_) {
			stack_.push_back(make_null());
			return {};
		}
	}

	if (result_needed && !old_result) { return land_member_store_result(f, objectValue, member, std::move(resultValue)); }
	stack_.push_back(old_result ? std::move(postfix_old) : std::move(resultValue));
	return {};
}

// Payloads the buffer takes as-is (the slice kernel's compat + convert + push collapse to
// one append): a null/scalar/string into a var array is its own shared copy; an int into an
// int buffer, an int or float into a float buffer, a string into a string buffer. The memory
// charge is the kernel's exact formula, so the cap trips at the same push. Everything else
// (typed classes, nested containers, a float into an int buffer) is declined to the kernel.
vm_backend::push_append vm_backend::typed_push_append(const script_value& objd, const script_value& pv) {
	const size_t vi = pv.raw_storage_index();
	script_array* node = const_cast<script_value&>(objd).unchecked_get_array_storage().get();
	if (!node) return push_append::declined;
	auto charge = [&](size_t bytes) -> bool { return limits_->memory_charge(bytes); };
	switch (node->kind()) {
	case script_array::kind_t::hetero: {
		const type_info* ti = objd.get_type_info().get();
		const type_info* et = ti ? ti->element_type().get() : nullptr;
		if ((et && et->base_type != script_value_type::jai_any_type) ||
		    !(vi == script_value::TYPEID_NULL || vi == script_value::TYPEID_INT || vi == script_value::TYPEID_FLOAT ||
		      vi == script_value::TYPEID_BOOL || vi == script_value::TYPEID_CHAR || vi == script_value::TYPEID_STRING)) {
			return push_append::declined;
		}
		const size_t bytes = sizeof(script_value) + (vi == script_value::TYPEID_STRING ? pv.unchecked_as_string().size() : 0);
		if (!charge(bytes)) [[unlikely]] { (void)raise_from(detail::raise_memory_cap(*limits_)); return push_append::failed; }
		node->values().push_back(pv);
		return push_append::appended;
	}
	case script_array::kind_t::i64:
		if (vi != script_value::TYPEID_INT) return push_append::declined;
		if (!charge(sizeof(script_value))) [[unlikely]] { (void)raise_from(detail::raise_memory_cap(*limits_)); return push_append::failed; }
		node->ints().push_back(pv.unchecked_as_int());
		return push_append::appended;
	case script_array::kind_t::f64:
		if (vi != script_value::TYPEID_FLOAT && vi != script_value::TYPEID_INT) return push_append::declined;
		if (!charge(sizeof(script_value))) [[unlikely]] { (void)raise_from(detail::raise_memory_cap(*limits_)); return push_append::failed; }
		node->floats().push_back(vi == script_value::TYPEID_FLOAT ? pv.unchecked_as_float() : static_cast<script_float>(pv.unchecked_as_int()));
		return push_append::appended;
	case script_array::kind_t::str:
		if (vi != script_value::TYPEID_STRING) return push_append::declined;
		if (!charge(sizeof(script_value) + pv.unchecked_as_string().size())) [[unlikely]] { (void)raise_from(detail::raise_memory_cap(*limits_)); return push_append::failed; }
		node->strs().push_back(pv.unchecked_string_handle());
		return push_append::appended;
	default:
		return push_append::declined;
	}
}

// op_t_push: the append rows over a slot receiver and a slot or literal value; anything
// else (a non-array receiver, a declined payload) is the original op_push_fused
op_status vm_backend::exec_typed_push(frame& f, const vm_instruction& ins) {
	const script_value* recv = frame_slot(f, ins.a);
	if (recv && (recv->raw_storage_index() != script_value::TYPEID_REFERENCE || lane_deref_silent(*recv))) {
		const script_value& objd = recv->deref();
		if (objd.raw_storage_index() == script_value::TYPEID_ARRAY) {
			std::optional<script_value> vconst;
			const script_value* vp;
			if (ins.b & k_typed_const_bit) {
				vconst.emplace(materialize_constant(*f.code, ins.b & ~k_typed_const_bit));
				vp = &*vconst;
			} else {
				vp = frame_slot(f, ins.b);
				if (vp && vp->raw_storage_index() == script_value::TYPEID_REFERENCE && !lane_deref_silent(*vp)) { vp = nullptr; }
			}
			if (vp) {
				const push_append lane = typed_push_append(objd, vp->deref());
				if (lane == push_append::failed) return op_status::failed;
				if (lane == push_append::appended) {
					if (next_op_discards_result(f)) { ++f.ip; }
					else { stack_.push_back(make_null()); }
					return {};
				}
			}
		}
	}
	return exec_push_fused(f, f.code->typed_sites[f.ip].orig);
}

// `recv.push(v);` fused (op_push_fused): the receiver resolves first, then the value, both
// through the LOAD-identical operand ladders; an array receiver with the builtin push runs
// the sliced push kernel VERBATIM (compat text, element conversion, memory charge, append)
// and pushes the call result (null) for the statement's discard; every other receiver
// (null, map, string, an instance with its own push, an unwarmed worker) replays the exact
// unfused sequence: receiver copy, value, CALL_METHOD through the same call site.
op_status vm_backend::exec_push_fused(frame& f, const vm_instruction& ins) {
	const push_proto& p = f.code->push_protos[ins.a];
	const call_site& site = f.code->call_sites[p.site];
	std::optional<script_value> rscratch, vscratch;
	const script_value* recv = fused_cmp_operand(f, p.receiver, f.ip * 3);
	if (!recv) {
		recv = fused_ident_value(f, p.receiver, rscratch, f.ip * 3);
		if (!recv) return op_status::failed;
	}
	// The value: a literal materializes, an identifier resolves through the LOAD ladder, a
	// subscript resolves AFTER the receiver is known to be an array (the general sequence
	// probes the member before the argument runs, and an array always has push)
	const script_value* val = nullptr;
	std::optional<script_value> vconst;
	auto resolve_value = [&]() -> bool {
		if (p.value.const_index != k_invalid_u32) {
			vconst.emplace(materialize_constant(*f.code, p.value.const_index));
			val = &*vconst;
			return true;
		}
		if (p.value_is_subscript) {
			val = fused_subscript_value(f, p.value, vscratch, f.ip * 3 + 1);
			return val != nullptr;
		}
		val = fused_cmp_operand(f, p.value, f.ip * 3 + 1);
		if (!val) { val = fused_ident_value(f, p.value, vscratch, f.ip * 3 + 1); }
		return val != nullptr;
	};
	// The statement discards the call's result: the unfused pair pushed a null and the
	// POP that follows consumed it; every path below lands it the same way
	auto land_discarded = [&]() {
		if (next_op_discards_result(f)) { ++f.ip; }
		else { stack_.push_back(make_null()); }
	};
	const script_value& objd = recv->deref();
	if (objd.raw_storage_index() == script_value::TYPEID_ARRAY && f.code->builtin_indexed &&
	    site.bi_array != builtin_method_registries::k_no_builtin) {
		// The site is `.push` by construction (try_emit_fused_push) and the receiver is an
		// array, so the builtin is push: no member-node walk, no id compare
		if (!resolve_value()) return op_status::failed;
		{
			const push_append lane = typed_push_append(objd, val->deref());
			if (lane == push_append::failed) return op_status::failed;
			if (lane == push_append::appended) {
#ifdef JAISCRIPT_VM_PROFILE
				++profile_push_fused_lane_[0];
#endif
				land_discarded();
				return {};
			}
		}
		{
#ifdef JAISCRIPT_VM_PROFILE
			++profile_push_fused_lane_[1];
#endif
			auto array_type_info = objd.get_type_info();
			type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
			if (!vm_is_element_type_compatible(*val, element_type, const_cast<script_value&>(objd))) {
				std::string value_type = vm_value_type_name(val->deref());
				std::string expected_type = vm_type_info_name(element_type);
				uint64_t value_type_id = symbolizer_->intern(value_type);
				uint64_t expected_type_id = symbolizer_->intern(expected_type);
				return raise_(
					make_error_code(runtime_error_code::array_element_type_mismatch),
					"Cannot push '{0}' to array<{1}>",
					value_type_id, expected_type_id);
			}
			// The unfused pair pinned the node through the receiver's stack copy; a
			// conversion can run user code, so pin it the same way before converting
			strong_ptr<script_array> node = const_cast<script_value&>(objd).unchecked_get_array_storage();
			auto converted_result = vm_convert_array_element(engine_, *val, element_type);
			if (!converted_result) { return raise_from(converted_result); }
			script_value converted = std::move(converted_result.value());
			const size_t bytes = sizeof(script_value) +
				(converted.is_string() ? converted.unchecked_as_string().size() : 0);
			if (!limits_->memory_charge(bytes)) [[unlikely]] {
				return raise_from(detail::raise_memory_cap(*limits_));
			}
			node->push(std::move(converted));
			land_discarded();
			return {};
		}
	}
#ifdef JAISCRIPT_VM_PROFILE
	++profile_push_fused_lane_[2];
#endif
	// replay: exactly what the unfused sequence leaves on the stack, in its order: the
	// receiver (LOAD pushes the cell's referent), the member probe when the argument owed
	// one, the argument (an element read as its lvalue-shaped op_index), then the call
	stack_.push_back(recv->deref());
	if (p.value_is_subscript) {
		const vm_instruction probe_ins{opcode::op_probe_member, p.site, 0, 0};
		VM_TRY(exec_probe_member(f, probe_ins));
		if (is_unwinding_) [[unlikely]] { return {}; }
		fused_operand base = p.value;
		base.subscript_chain.clear();
		std::optional<script_value> bscratch;
		const script_value* bp = fused_cmp_operand(f, base, f.ip * 3 + 1);
		if (!bp) {
			bp = fused_ident_value(f, base, bscratch, f.ip * 3 + 1);
			if (!bp) return op_status::failed;
		}
		stack_.push_back(bp->deref());
		const fused_subscript_index& level = p.value.subscript_chain[0];
		if (level.kind == fused_subscript_index::index_literal) {
			stack_.push_back(script_value(level.literal, engine_));
		} else {
			fused_operand index_operand;
			index_operand.slot = level.slot;
			index_operand.symbol = level.symbol;
			std::optional<script_value> iscratch;
			const script_value* ip = fused_cmp_operand(f, index_operand, f.ip * 3 + 2);
			if (!ip) {
				ip = fused_ident_value(f, index_operand, iscratch, f.ip * 3 + 2);
				if (!ip) return op_status::failed;
			}
			stack_.push_back(ip->deref());
		}
		const vm_instruction index_ins{opcode::op_index, p.value_index_flags, 0, 0};
		VM_TRY(exec_index(f, index_ins));
		if (is_unwinding_) [[unlikely]] { return {}; }
	} else {
		if (!resolve_value()) return op_status::failed;
		stack_.push_back(val->deref());
	}
	const vm_instruction call_ins{opcode::op_call_method, 1, p.site, 0};
	VM_TRY(exec_call_method(f, call_ins));
	// an inline (native) call left its result on the stack: op_call_method's dispatch
	// discard, verbatim; a script frame lands its own result (land_call_result)
	if (!switch_to_ && !is_unwinding_ && next_op_discards_result(f)) { stack_.pop_back(); ++f.ip; }
	return {};
}

op_status vm_backend::exec_call_method(frame& f, const vm_instruction& ins) {
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t cm_entry = __rdtsc();
#endif
	const size_t argc = ins.a;
	const call_site& site = f.code->call_sites[ins.b];
	auto* member = static_cast<member_expr*>(f.code->nodes[site.member_node].get());
	const size_t args_base = stack_.size() - argc;

	// ===== Call-arg windows (increment 2): the receiver at stack_[args_base-1] and
	// the args above it bind IN PLACE for pin-path and method-IC dispatches — no
	// pooled vector, no per-arg move, the receiver slot is the pin. Guarded by the
	// string-builtin pre-gate: identifier-receiver sites whose NAME is a string
	// builtin keep today's probe-first order exactly (the variable, not the
	// stack value, decides — receiver-mutating arg expressions depend on it).
	// string_recv = the resolved string VARIABLE cell (the builtin mutates it in
	// place). Valid across the arg-vector move below: the stack only shrinks
	// between here and the bind (invariant 2b), env cells and field cells are
	// stable, and no user code runs in between.
	script_value* string_recv = nullptr;
	{
		// The pre-gate probes the VARIABLE, not just the name: a method NAME that
		// collides with a string builtin (insert/count/find/...) must not exile
		// instance dispatch to the vector path when the variable isn't a string.
		// The probe runs post-args (exec_call_method fires with receiver+args already
		// pushed) through the identifier ladder — locals, members, globals — the same
		// cell an op_load of the receiver reads.
		bool maybe_string_builtin = site.receiver_symbol != UINT64_MAX &&
			(f.code->builtin_indexed
				? site.bi_string != builtin_method_registries::k_no_builtin
				: builtins_.string_methods.find(member->member_id) != builtins_.string_methods.end());
		if (maybe_string_builtin) {
			script_value* recv_cell = string_receiver_cell(f, site, static_cast<identifier_expr*>(member->object.get()));
			if (recv_cell && recv_cell->is_string()) {
				string_recv = recv_cell;
			}
			maybe_string_builtin = string_recv != nullptr;
		}
		if (!maybe_string_builtin && member->member_id != same_as_id_ &&
		    !(member->object && member->object->get_type() == node_type::super_expr)) {
			script_value& objd = stack_[args_base - 1].deref();
			const size_t receiver_type = objd.raw_storage_index();
			if (receiver_type == script_value::TYPEID_OBJECT ||
			    receiver_type == script_value::TYPEID_SHARED_PTR) {
				auto holder = objd.get_object_holder();
				if (holder && holder->is_class_instance_wrapper && holder->data &&
				    holder->type_id != coroutine_handle_type_id_) {
#ifdef JAISCRIPT_VM_PROFILE
					++profile_slice_gate_[0];   // reached instance receiver
#endif
					auto* inst = static_cast<class_instance*>(holder->data.get());
					class_definition* cd = inst->get_class_definition();
					// Worker pin (pre-resolved at the barrier): slice-enter directly
					if (parallel_worker_ && cd) [[unlikely]] {
						const detail::parallel_method_pin* pin = parallel_method_pins_
							? parallel_method_pins_->find(cd, member->member_id) : nullptr;
						if (pin) {
							script_value method_val = pin->method_value;
							const auto* dispatch = method_val.as_function().target<script_method_dispatch>();
							return enter_script_method_sliced(f, std::move(method_val), *dispatch,
							                                  pin->resolved, nullptr, args_base - 1,
							                                  args_base, argc, site);
						}
						// unpinned falls to the vector path below for the identical verdict
					} else if (cached_global_env_ && site.mic_cd && cd == site.mic_cd &&
					           cd->method_epoch() == site.mic_epoch &&
					           !mic_field_shadowed(site, *inst, member->member_id)) {
#ifdef JAISCRIPT_VM_PROFILE
						++profile_slice_gate_[1];   // mic guard passed
#endif
						if (site.mic_static) {
							script_value method_val = site.mic_method;
							return enter_script_method_sliced(f, std::move(method_val), *site.mic_dispatch,
							                                  site.mic_resolved, nullptr, args_base - 1,
							                                  args_base, argc, site);
						}
						auto resolved = site.mic_dispatch->cls->resolve_method_overload_ref(
							site.mic_dispatch->name_id, stack_.data() + args_base, argc);
						if (resolved && (*resolved.value())->body && !(*resolved.value())->is_coroutine) {
							script_value method_val = site.mic_method;
							return enter_script_method_sliced(f, std::move(method_val), *site.mic_dispatch,
							                                  *resolved.value(), nullptr, args_base - 1,
							                                  args_base, argc, site);
						}
						// resolution declined (arg types): the vector ladder reports identically
#ifdef JAISCRIPT_VM_PROFILE
						++profile_slice_gate_[2];   // resolution declined in the slice branch
#endif
					}
#ifdef JAISCRIPT_VM_PROFILE
					else { ++profile_slice_gate_[3]; }   // mic guard failed (unarmed/epoch/class/field)
#endif
				}
			} else if (receiver_type == script_value::TYPEID_ARRAY && argc == 1 &&
			           f.code->builtin_indexed &&
			           site.bi_array != builtin_method_registries::k_no_builtin) {
				// ARRAY push, slice edition: the receiver stays IN PLACE at args_base-1
				// (no copy, no refcount pair) and the single arg is consumed off the
				// stack — no pooled vector, no boundary armor (the builtin's body
				// VERBATIM, same spellings/chokepoints as the direct-dispatch inline).
				// member_id warms lazily on the MAIN thread only (workers get
				// region-prewarmed ids; unwarmed on a worker falls to the vector path).
				uint64_t push_member_id = member->member_id;
				if (push_member_id == UINT64_MAX && !parallel_worker_) {
					push_member_id = symbolizer_->intern(member->member);
					member->member_id = push_member_id;
				}
				if (push_member_id == builtin_push_id_) {
#ifdef JAISCRIPT_VM_PROFILE
					++profile_builtin_direct_names_["<push-sliced>"];
					const uint64_t push_t0 = __rdtsc();
					profile_push_prologue_ += push_t0 - cm_entry;
					{
						const script_array* pn = objd.unchecked_get_array_storage().get();
						if (pn->is_typed()) { ++profile_push_sections_[0]; }
						else { ++profile_push_sections_[1]; if (pn->values().size() == pn->values().capacity()) { ++profile_push_sections_[2]; } }
					}
#endif
					auto array_type_info = objd.get_type_info();
					type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
					if (!vm_is_element_type_compatible(stack_[args_base], element_type, objd)) {
						std::string value_type = vm_value_type_name(stack_[args_base].deref());   // an element-reference argument names its referent
						std::string expected_type = vm_type_info_name(element_type);
						uint64_t value_type_id = symbolizer_->intern(value_type);
						uint64_t expected_type_id = symbolizer_->intern(expected_type);
						return raise_(
							make_error_code(runtime_error_code::array_element_type_mismatch),
							"Cannot push '{0}' to array<{1}>",
							value_type_id, expected_type_id);
					}
#ifdef JAISCRIPT_VM_PROFILE
					const uint64_t push_t1 = __rdtsc(); profile_push_sections_[3] += push_t1 - push_t0;
#endif
					auto converted_result = vm_convert_array_element(engine_, stack_[args_base], element_type);
					if (!converted_result) { return raise_from(converted_result); }
					script_value converted = std::move(converted_result.value());
#ifdef JAISCRIPT_VM_PROFILE
					const uint64_t push_t2 = __rdtsc(); profile_push_sections_[4] += push_t2 - push_t1;
#endif
					const size_t bytes = sizeof(script_value) +
						(converted.is_string() ? converted.unchecked_as_string().size() : 0);
					if (!limits_->memory_charge(bytes)) [[unlikely]] {
						return raise_from(detail::raise_memory_cap(*limits_));
					}
#ifdef JAISCRIPT_VM_PROFILE
					const uint64_t push_t3 = __rdtsc(); profile_push_sections_[5] += push_t3 - push_t2;
#endif
					// Conversions can run user code and the stack can move (invariant
					// 2b): re-resolve the receiver by INDEX before the append
					script_value& recv_after = stack_[args_base - 1].deref();
					recv_after.unchecked_get_array_storage()->push(std::move(converted));
#ifdef JAISCRIPT_VM_PROFILE
					const uint64_t push_t4 = __rdtsc(); profile_push_sections_[6] += push_t4 - push_t3;
#endif
					stack_.pop_back();                    // the arg
					stack_.back() = make_null();          // receiver slot becomes the call result
#ifdef JAISCRIPT_VM_PROFILE
					profile_push_sections_[7] += __rdtsc() - push_t4;
#endif
					return {};
				}
			}
		}
	}

	auto arguments = acquire_arg_vector(argc);
	arg_vector_return arg_return{this, &arguments};
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		arguments.push_back(std::move(stack_[base + i]));
	}
	stack_.truncate(base);

	if (string_recv) {
		// The pre-gate proved the name is a registered string builtin AND the variable
		// holds a string (indexed chunks bake bi_string; raw-compiler chunks find)
		const builtin_method* string_builtin = f.code->builtin_indexed
			? builtins_.string_by_index[site.bi_string]
			: &builtins_.string_methods.find(member->member_id)->second;
		// Resolve through references (ref decls/cells): string builtins read self's
		// storage RAW, and in-place mutation must land in the target; a scratch-backed
		// alias (typed element, C++ property) writes back after. The LOADed receiver
		// copy is dropped first - alive, it would only make the write gate copy the
		// node (KEEP BYTE-PARALLEL with the interpreter's identifier-receiver path)
		stack_.pop_back();
		script_value& var_ref = string_recv->deref();
		detail::deref_builtin_args_in_place(arguments);
#ifdef JAISCRIPT_VM_PROFILE
		profile_call_method_paths_[3]++;
#endif
		auto result = (*string_builtin)(builtin_ctx(), var_ref, arguments);
		if (!result) {
			return raise_from(result);
		}
		if (auto* scratch_ref = string_recv->scratch_ref_holder()) [[unlikely]] { scratch_ref->commit_scratch(); }
		stack_.push_back(std::move(result.value()));
		return {};
	}

	script_value object = std::move(stack_.back());
	stack_.pop_back();

	// Script-class instance methods enter the dispatch loop directly. The gate mirrors
	// member_access_value's precedence exactly (super/coroutine/builtin/same_as/getter/
	// field all bail to the native bound-method path), so behavior is byte-identical.
	if (member->member_id != same_as_id_ &&
	    !(member->object && member->object->get_type() == node_type::super_expr)) {
		script_value objv = object.deref();
		// ONE type query, integer compares after (each is_x re-derefs + re-reads the
		// variant index): OBJECT + SHARED_PTR is exactly is_object()'s answer.
		const size_t receiver_type = objv.raw_storage_index();
		if (receiver_type == script_value::TYPEID_OBJECT ||
		    receiver_type == script_value::TYPEID_SHARED_PTR) {
			auto holder = objv.get_object_holder();
			// Worker method wall → admission verdict (parallel-method-admission ruling):
			// barrier-admitted methods dispatch on THIS worker's backend through pinned
			// worker-private state (fresh dispatcher value, pre-resolved overload,
			// pre-compiled chunk — no shared count or cache is ever touched); anything
			// unpinned keeps the wall error. Trusted scripts still opt out entirely.
			const detail::parallel_method_pin* worker_pin = nullptr;
			if (parallel_worker_ && holder && holder->is_class_instance_wrapper && holder->data) [[unlikely]] {
				auto* cd = static_cast<class_instance*>(holder->data.get())->get_class_definition();
				worker_pin = parallel_method_pins_ && cd ? parallel_method_pins_->find(cd, member->member_id) : nullptr;
				if (!worker_pin) {
					return raise_(make_error_code(runtime_error_code::unsupported_operation),
						"method '{0}' was not admitted in this parallel body (admitted: single-overload "
						"public methods on the element's own class touching only element, local, or "
						"captured-read state)", member->member_id);
				}
			}
			if (worker_pin) [[unlikely]] {
				const detail::parallel_method_pin* pin = worker_pin;
				script_value method_val = pin->method_value;   // pin-private control block
				const auto* dispatch = method_val.as_function().target<script_method_dispatch>();
#ifdef JAISCRIPT_VM_PROFILE
				profile_call_method_paths_[4]++;
#endif
				return enter_script_method(f, std::move(method_val), *dispatch, pin->resolved,
				                           std::move(objv), arguments, site);
			}
			// Monomorphic method IC hit (call_site in chunk.hpp): same receiver class at
			// an unchanged method_epoch replays the cached ladder outcome. has_field is
			// re-probed every hit (a runtime-added field shadows the method); non-static
			// sites re-run single-overload resolution (typed params reject by arg type,
			// and a decline falls to the full ladder for the identical error).
			if (cached_global_env_ && site.mic_cd && holder && holder->is_class_instance_wrapper &&
			    holder->data) {
				auto* inst = static_cast<class_instance*>(holder->data.get());
				if (inst->get_class_definition() == site.mic_cd &&
				    site.mic_cd->method_epoch() == site.mic_epoch &&
				    !mic_field_shadowed(site, *inst, member->member_id)) {
					if (site.mic_static) {
						script_value method_val = site.mic_method;
#ifdef JAISCRIPT_VM_PROFILE
						profile_call_method_paths_[0]++;
#endif
						return enter_script_method(f, std::move(method_val), *site.mic_dispatch,
						                           site.mic_resolved, std::move(objv), arguments, site);
					}
					auto resolved = site.mic_dispatch->cls->resolve_method_overload(
						site.mic_dispatch->name_id, arguments);
					if (resolved && resolved.value()->body && !resolved.value()->is_coroutine) {
						script_value method_val = site.mic_method;
#ifdef JAISCRIPT_VM_PROFILE
						profile_call_method_paths_[1]++;
#endif
						return enter_script_method(f, std::move(method_val), *site.mic_dispatch,
						                           resolved.value(), std::move(objv), arguments, site);
					}
				}
			}
			// Coroutine resume()/done() dispatch directly: the native branch mints a fresh
			// closure + function value PER RESUME just to forward to the handle. Same
			// handle call, same checked_result propagation; the minted lambdas ignore
			// their arguments, so this drops them identically. Any other member falls
			// through for member_access_value's coroutine error spelling.
			if (holder && holder->type_id == coroutine_handle_type_id_ && holder->data &&
			    (member->member_id == resume_id_ || member->member_id == done_id_)) {
				auto handle = std::static_pointer_cast<coroutine_handle>(holder->data);
				if (member->member_id == resume_id_) {
					return guarded_native_call([&] { return handle->resume(engine_); });
				}
				stack_.push_back(script_value(handle->done(), engine_));
				return {};
			}
			// A user class method WINS over a same-named builtin handle method (Dev ruling
			// 2026-07): the sp-builtin only applies when the class does NOT define it.
			bool sp_class_defines = false;
			if (holder && holder->is_class_instance_wrapper && holder->data) {
				auto* cd = static_cast<class_instance*>(holder->data.get())->get_class_definition();
				sp_class_defines = cd && cd->defines_method(member->member_id);
			}
			const bool shared_ptr_builtin = objv.get_type_info() &&
				objv.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
				builtins_.shared_ptr_methods.find(member->member_id) != builtins_.shared_ptr_methods.end() &&
				!sp_class_defines;
			if (holder && holder->type_id != coroutine_handle_type_id_ && !shared_ptr_builtin) {
				auto target = resolve_member_target(objv);
				if (target && target.class_def && target.class_def->chain_has_nonpublic()) [[unlikely]] {
					VM_TRY(detail::enforce_member_access(target.class_def, member->member_id,
					                                            frame_access_context(f)));
				}
				if (target && target.class_def && !target.has_field(member->member_id)) {
					// Per-member getter probe (stage 5a, method-cost agent handoff): every
					// field-bearing script class auto-registers _get_/_set_ accessors, so
					// the old blanket has_property_getters() bail kept essentially every
					// real class off this in-loop path (full bound-method mint per call,
					// ~2.4 µs). Only an ACTUAL _get_<member> method forces the native path
					// now — mirrors member_access_value's getter-before-field precedence
					// (auto-getters exist only for fields, which has_field already
					// excluded; the probe covers custom property getters).
					uint64_t getter_id = member->getter_id;
					if (getter_id == UINT64_MAX) {
						auto [id, _] = symbolizer_->get_getter_id_with_view(member->member_id);
						getter_id = id;
						member->getter_id = getter_id;
					}
					const script_value getter_probe = target.method(getter_id);
					const bool has_member_getter =
						!getter_probe.is_null() && !getter_probe.is_invalid() && getter_probe.is_function();
					if (!has_member_getter) {
						script_value method_val = target.method(member->member_id);
						if (method_val.is_function()) {
							const auto* dispatch = method_val.as_function().target<script_method_dispatch>();
							if (dispatch && dispatch->eng == engine_) {
								auto resolved = dispatch->cls->resolve_method_overload(dispatch->name_id, arguments);
								// Resolution failures (and coroutine methods) fall through to the
								// native path, which re-resolves and reports the identical error
								if (resolved && resolved.value()->body && !resolved.value()->is_coroutine) {
									// IC fill (call_site in chunk.hpp): single-overload methods on
									// all-public chains cache this outcome; mic_static marks sites
									// whose resolution can't depend on arg types (every arg-bound
									// param untyped — pick_best_overload's has_explicit_type test).
									if (cached_global_env_ && target.instance &&
									    target.instance->get_class_definition() &&
									    !target.class_def->chain_has_nonpublic() &&
									    dispatch->cls->single_method_overload(dispatch->name_id)) {
										site.mic_cd = target.instance->get_class_definition();
										site.mic_epoch = site.mic_cd->method_epoch();
										site.mic_field_free = !site.mic_cd->chain_declares_field(member->member_id) &&
										                      site.mic_cd->field_slot(member->member_id) == class_definition::k_no_field_slot;
										site.mic_method = method_val;
										site.mic_dispatch = site.mic_method.as_function().target<script_method_dispatch>();
										site.mic_resolved = resolved.value();
										bool arg_independent = true;
										const auto& params = resolved.value()->parameters;
										for (size_t i = 0; i < arguments.size() && arg_independent; ++i) {
											const auto& p = params[i];
											arg_independent = !(p.type && !p.type->type_name.empty() &&
											                    p.type->type_name != "any" &&
											                    p.type->base_type != script_value_type::jai_any_type);
										}
										site.mic_static = arg_independent;
									}
#ifdef JAISCRIPT_VM_PROFILE
									profile_call_method_paths_[2]++;
#endif
									return enter_script_method(f, std::move(method_val), *dispatch,
									                           resolved.value(), std::move(objv), arguments, site);
								}
							}
						}
					}
				}
			}
		} else if (f.code->builtin_indexed) {
			// Builtin methods dispatch DIRECTLY by one indexed load (bi_* baked at
			// compile time, name-sorted = engine-independent): the native path below
			// mints a bound-method function value per call just to invoke it once.
			// Same registry fn, same receiver value it would have captured, same
			// checked_result propagation. A miss (including exotic receivers like
			// cpp_bound strings, which this integer switch deliberately excludes)
			// falls through so unknown members, the map KEY-sugar spelling, and
			// raw-compiler chunks keep member_access_value's one voice.
			const builtin_method* direct = nullptr;
			bool string_mutator = false;
			switch (receiver_type) {
			case script_value::TYPEID_ARRAY:
				if (site.bi_array != builtin_method_registries::k_no_builtin) {
					direct = builtins_.array_by_index[site.bi_array];
				}
				break;
			case script_value::TYPEID_MAP:
				if (site.bi_map != builtin_method_registries::k_no_builtin) {
					direct = builtins_.map_by_index[site.bi_map];
				}
				break;
			case script_value::TYPEID_STRING:
				// Only non-identifier string receivers reach here (identifier receivers
				// whose variable is a string returned through the env path above).
				if (site.bi_string != builtin_method_registries::k_no_builtin) {
					direct = builtins_.string_by_index[site.bi_string];
					string_mutator = builtins_.string_mutates_by_index[site.bi_string] != 0;
				}
				break;
			default:
				break;
			}
			if (direct) {
#ifdef JAISCRIPT_VM_PROFILE
				profile_call_method_paths_[6]++;
				++profile_builtin_direct_names_[std::string(member->member)];
#endif
				// ARRAY push inline (95% of builtin-direct calls in array-heavy code):
				// the builtin's body VERBATIM — same kernels, same error text, same
				// memory-cap chokepoint — minus the pending-site arming and boundary
				// armor (an append runs no user code). bi_array already proved the
				// site is a registered array builtin; push's name id pins WHICH.
				// member_id warms lazily (interp idiom) — main thread only: workers get
				// pre-warmed ids from region setup and must never intern (frozen tables).
				uint64_t direct_member_id = member->member_id;
				if (direct_member_id == UINT64_MAX && !parallel_worker_) {
					direct_member_id = symbolizer_->intern(member->member);
					member->member_id = direct_member_id;
				}
				if (receiver_type == script_value::TYPEID_ARRAY &&
				    direct_member_id == builtin_push_id_ && arguments.size() == 1) {
#ifdef JAISCRIPT_VM_PROFILE
					++profile_builtin_direct_names_["<push-inline>"];
#endif
					auto array_type_info = objv.get_type_info();
					type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
					if (!vm_is_element_type_compatible(arguments[0], element_type, objv)) {
						std::string value_type = vm_value_type_name(arguments[0].deref());
						std::string expected_type = vm_type_info_name(element_type);
						uint64_t value_type_id = symbolizer_->intern(value_type);
						uint64_t expected_type_id = symbolizer_->intern(expected_type);
						return raise_(
							make_error_code(runtime_error_code::array_element_type_mismatch),
							"Cannot push '{0}' to array<{1}>",
							value_type_id, expected_type_id);
					}
					auto converted_result = vm_convert_array_element(engine_, arguments[0], element_type);
					if (!converted_result) { return raise_from(converted_result); }
					script_value converted = std::move(converted_result.value());
					const size_t bytes = sizeof(script_value) +
						(converted.is_string() ? converted.unchecked_as_string().size() : 0);
					if (!limits_->memory_charge(bytes)) [[unlikely]] {
						return raise_from(detail::raise_memory_cap(*limits_));
					}
					objv.unchecked_get_array_storage()->push(std::move(converted));
					stack_.push_back(make_null());
					return {};
				}
				// objv IS the deref'd copy member_access_value captures (line one of its
				// body): builtins take self unchecked. Pending-site arming exactly as
				// invoke_callee's opaque branch (callback builtins re-enter the vm); the
				// boundary armor is guarded_native_call.
				detail::deref_builtin_args_in_place(arguments);
				pending_call_site saved_pending = pending_site_ctx_;
				pending_site_ctx_ = arguments.empty() ? saved_pending
				                                      : pending_call_site{&site, &f, f.code};
				struct pending_restore {
					vm_backend* vm; pending_call_site saved;
					~pending_restore() { vm->pending_site_ctx_ = saved; }
				} restore_pending{this, saved_pending};
				if (string_mutator) {
					// A string MUTATOR binds the receiver LVALUE (detail/string_mutator_receiver.hpp;
					// C++ `string& to_upper()`): the popped reference, the resolved field /
					// property, or the chain root whose node the receiver still is (an
					// identifier root through the baked chain_root cell, the identifier
					// ladder). Unbound = a temporary: the builtin mutates the copy alone. The
					// spare copies are dropped first - alive, they would only make the write
					// gate copy the node. (KEEP BYTE-PARALLEL with interpreter::member_read_on)
					if (!object.is_reference()) {
						ensure_frame_member_scope(f, environment_.get());   // the kernel reads the frame's member scope
					}
					detail::string_mutator_binding binding = detail::bind_string_mutator_receiver(
						object, member->object.get(), caller_view(&f), environment_.get(), engine_, symbolizer_);
					return guarded_native_call([&] {
						script_value* root_cell = nullptr;
						if (binding.root_ident) {
							root_cell = binding.root_ident->global_qualified
								? engine_->get_global_environment()->get_value_ptr(site.chain_root_symbol)
								: resolve_local_or_env(f, site.chain_root_slot, site.chain_root_symbol);
						}
						script_value* self = detail::string_mutator_self(binding, root_cell, objv);
						object = make_null();   // a reference moved into the pin; a copy only inflates the count
						if (self) {
							objv = make_null();
						} else {
							self = &objv;
						}
						auto result = (*direct)(builtin_ctx(), *self, arguments);
						binding.commit();
						return result;
					});
				}
				return guarded_native_call([&] { return (*direct)(builtin_ctx(), objv, arguments); });
			}
		}
	}

	script_value callee = make_null();
	VM_TRY(member_access_value(f, object, member, callee, true));
	if (is_unwinding_) {
		stack_.push_back(make_null());
		return {};
	}
	if (callee.is_null() && member->null_safe) {
		stack_.push_back(make_null());
		return {};
	}
	if (!callee.is_function()) {
		return raise_not_callable(member->member_id, callee);
	}
#ifdef JAISCRIPT_VM_PROFILE
	profile_call_method_paths_[5]++;
#endif
	return invoke_callee(f, std::move(callee), arguments, site);
}

op_status vm_backend::exec_new(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<new_expr*>(f.code->nodes[ins.a].get());
	const size_t argc = ins.b;
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_new0 = __rdtsc();
#endif

	std::vector<script_value> args;
	args.reserve(argc);
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		args.push_back(std::move(stack_[base + i]));
	}
	stack_.truncate(base);

	if (!expr->type) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	if (expr->type->base_type == script_value_type::jai_array_type) {
		auto element_type = expr->type->element_type();
		if (!element_type && engine_) {
			element_type = engine_->get_type_info_int();
		}
		stack_.push_back(script_value::make_array(element_type, engine_));
		return {};
	}

	if (expr->type->base_type == script_value_type::jai_map_type) {
		auto key_type = expr->type->key_type();
		auto value_type = expr->type->value_type();
		if (engine_) {
			if (!key_type) key_type = engine_->get_type_info_string();
			if (!value_type) value_type = engine_->get_type_info_int();
		}
		stack_.push_back(script_value::make_map(key_type, value_type, engine_));
		return {};
	}

	if (expr->type->base_type == script_value_type::jai_weak_ptr_type) {
		if (args.empty()) {
			stack_.push_back(script_value::make_empty_weak_ptr(expr->type, engine_));
			return {};
		}
		script_value obj = std::move(args[0]);
		if (obj.is_null()) {
			stack_.push_back(script_value::make_empty_weak_ptr(expr->type, engine_));
			return {};
		}
		if (obj.is_weak_ptr()) {
			stack_.push_back(std::move(obj));
			return {};
		}
		if (obj.type() != script_value_type::jai_shared_ptr_type) {
			uint64_t expected_id = (expr->type && !expr->type->type_params.empty())
				? expr->type->type_params[0]->id : 0;
			if (obj.type() == script_value_type::jai_object_type) {
				return raise_(
					make_error_code(runtime_error_code::type_mismatch),
					"Cannot create weak_ptr from value-semantic object. Use shared_ptr<T>.",
					expected_id);
			}
			auto type_info = obj.get_type_info();
			uint64_t actual_id = type_info ? type_info->id : 0;
			return raise_(
				make_error_code(runtime_error_code::type_mismatch),
				"Cannot create weak_ptr from non-shared_ptr type. Use shared_ptr<T>.",
				expected_id, actual_id);
		}

		auto expected_type = expr->type->element_type();
		auto obj_type_info = obj.get_type_info();
		if (expected_type && obj_type_info &&
		    expected_type->base_type != script_value_type::jai_any_type) {
			std::string expected_class = expected_type->type_name;
			std::string actual_class = obj_type_info->element_type()
				? obj_type_info->element_type()->type_name
				: obj_type_info->type_name;
			if (expected_class != actual_class && engine_) {
				auto actual_def = engine_->get_class_definition(actual_class);
				if (!actual_def || !actual_def->is_subtype_of(expected_class)) {
					uint64_t expected_id = expected_type->id;
					uint64_t actual_id = obj_type_info->element_type()
						? obj_type_info->element_type()->id
						: obj_type_info->id;
					return raise_(
						make_error_code(runtime_error_code::type_mismatch),
						"weak_ptr type mismatch: type must match or be a subclass",
						expected_id, actual_id);
				}
			}
		}

		auto weak_result = script_value::make_weak_ptr(obj, engine_);
		if (!weak_result) {
			return raise_from(weak_result);
		}
		stack_.push_back(std::move(weak_result.value()));
		return {};
	}

	if (expr->type->base_type == script_value_type::jai_shared_ptr_type) {
		auto inner_type = expr->type->element_type();
		if (args.empty()) {
			if (!inner_type) {
				stack_.push_back(make_null());
				return {};
			}
			script_value* ctor_cell = environment_->get_value_ptr(symbolizer_->intern(inner_type->type_name));
			if (!ctor_cell || !ctor_cell->is_function()) {
				return raise_(make_error_code(runtime_error_code::undefined_variable),
					"No constructor found for class '{0}'", inner_type->id);
			}
			script_value constructorFunc = ctor_cell->deref();
			const script_function& func = constructorFunc.as_function();
			auto result = func({});
			if (!result) {
				return raise_from(result);
			}
			script_value value = std::move(result.value());
			if (value.type() == script_value_type::jai_object_type) {
				value.set_type_info(expr->type);
			}
			stack_.push_back(std::move(value));
			return {};
		}

		if (!inner_type) {
			return raise_(make_error_code(runtime_error_code::type_mismatch),
				"shared_ptr requires a type parameter when called with arguments");
		}
		script_value* ctor_cell = environment_->get_value_ptr(symbolizer_->intern(inner_type->type_name));
		if (!ctor_cell || !ctor_cell->is_function()) {
			return raise_(make_error_code(runtime_error_code::undefined_variable),
				"No constructor found for class '{0}'", inner_type->id);
		}
		script_value constructorFunc = ctor_cell->deref();
		const script_function& func = constructorFunc.as_function();
		// Typed ctor value: in-loop body entry with the shared_ptr type stamped onto
		// the completed frame's object result (the exact post-call set_type_info below)
		const auto* ctor_thunk = func.target<script_callable_thunk>();
		if (ctor_thunk && ctor_thunk->eng == engine_ &&
		    ctor_thunk->payload.kind == script_callable::kind_type::constructor &&
		    !parallel_worker_) {
			auto script_cls = ctor_thunk->payload.cls && ctor_thunk->payload.cls->is_script_class()
				? std::static_pointer_cast<script_class_definition>(ctor_thunk->payload.cls)
				: std::shared_ptr<script_class_definition>();
			if (script_cls) {
				if (script_cls->get_constructor_asts().empty()) {
					auto result = construct_default_instance(script_cls, args);
					if (!result) {
						return raise_from(result);
					}
					script_value value = std::move(result.value());
					if (value.type() == script_value_type::jai_object_type) {
						value.set_type_info(expr->type);
					}
					stack_.push_back(std::move(value));
					return {};
				}
				if (ctor_thunk->payload.definition_env) {
					return enter_constructor_in_loop(f, script_cls,
						ctor_thunk->payload.definition_env, args, expr->type.get());
				}
			}
		}
#ifdef JAISCRIPT_VM_PROFILE
		const uint64_t prof_sp1 = __rdtsc();
#endif
		auto result = func(args);
#ifdef JAISCRIPT_VM_PROFILE
		++profile_new_count_;
		profile_new_resolve_cyc_ += prof_sp1 - prof_new0;
		profile_new_invoke_cyc_ += __rdtsc() - prof_sp1;
#endif
		if (!result) {
			return raise_from(result);
		}
		script_value value = std::move(result.value());
		if (value.type() == script_value_type::jai_object_type) {
			value.set_type_info(expr->type);
		}
		stack_.push_back(std::move(value));
		return {};
	}

	std::string className = expr->type->type_name;
	auto ctor_result = environment_->get(className);
	if (ctor_result && ctor_result.value().is_function()) {
		script_value constructorFunc = std::move(ctor_result.value());
		const script_function& func = constructorFunc.as_function();
		// Typed ctor value: construction protocol native, ctor body IN-LOOP (workers
		// stay opaque - their rules live in execute_callable; a null definition_env
		// with ctor asts is a shape the opaque arm defines, fall to it)
		const auto* ctor_thunk = func.target<script_callable_thunk>();
		if (ctor_thunk && ctor_thunk->eng == engine_ &&
		    ctor_thunk->payload.kind == script_callable::kind_type::constructor &&
		    !parallel_worker_) {
			auto script_cls = ctor_thunk->payload.cls && ctor_thunk->payload.cls->is_script_class()
				? std::static_pointer_cast<script_class_definition>(ctor_thunk->payload.cls)
				: std::shared_ptr<script_class_definition>();
			if (script_cls) {
				if (script_cls->get_constructor_asts().empty()) {
					auto result = construct_default_instance(script_cls, args);
					if (!result) {
						return raise_from(result);
					}
					stack_.push_back(std::move(result.value()));
					return {};
				}
				if (ctor_thunk->payload.definition_env) {
					return enter_constructor_in_loop(f, script_cls,
						ctor_thunk->payload.definition_env, args, nullptr);
				}
			}
		}
#ifdef JAISCRIPT_VM_PROFILE
		const uint64_t prof_new1 = __rdtsc();
#endif
		auto result = func(args);
#ifdef JAISCRIPT_VM_PROFILE
		++profile_new_count_;
		profile_new_resolve_cyc_ += prof_new1 - prof_new0;
		profile_new_invoke_cyc_ += __rdtsc() - prof_new1;
#endif
		if (!result) {
			return raise_from(result);
		}
		stack_.push_back(std::move(result.value()));
		return {};
	}

	return raise_(make_error_code(runtime_error_code::undefined_variable),
		"No constructor found for class '{0}'", expr->type->id);
}

op_status vm_backend::exec_enum_decl(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<enum_decl*>(f.code->nodes[ins.a].get());
	auto enum_map = script_value::make_map(
		engine_->get_type_info_string(),
		engine_->get_type_info_int(),
		engine_);
	auto& map_ref = const_cast<script_map&>(enum_map.as_map());

	for (size_t i = 0; i < decl->values.size(); ++i) {
		auto key = script_value(std::string(decl->values[i].first), engine_);
		map_ref[std::move(key)] = script_value(static_cast<script_int>(i), engine_);
	}

	environment_->define(decl->name_id, std::move(enum_map));
	return {};
}

op_status vm_backend::exec_parallel_for(frame& f, const vm_instruction& ins) {
	auto* stmt = static_cast<parallel_for_stmt*>(f.code->nodes[ins.a].get());
	const size_t arity = ins.b == k_invalid_u32 || ins.b == 0 ? 1 : ins.b;   // zip: the containers sit on the stack left to right
	std::vector<script_value> containers;
	containers.reserve(arity);
	for (size_t k = stack_.size() - arity; k < stack_.size(); ++k) { containers.push_back(std::move(stack_[k])); }
	for (size_t k = 0; k < arity; ++k) { stack_.pop_back(); }
	const std::function<const script_value*(size_t)> read_frame_slot = [this, &f](size_t slot) -> const script_value* { return frame_slot(f, slot); };
	return vm_check(detail::run_parallel_for(*engine_, stmt, containers, environment_, read_frame_slot));
}

op_status vm_backend::exec_class_decl(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<class_decl*>(f.code->nodes[ins.a].get());
	// Class machinery throws (override validation, field-init failures); the interpreter
	// converts those to unwinding at its statement boundary - do the same here
	try {
		return exec_class_decl_node(decl);
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		return {};
	} catch (const std::runtime_error& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		return {};
	}
}

op_status vm_backend::exec_class_decl_node(class_decl* decl) {
	std::shared_ptr<script_class_definition> class_def;
	bool is_redefinition = false;

	auto [class_var_id, class_var_name_view] = symbolizer_->get_class_var_id_with_view(decl->name_id);

	auto global_env = engine_ ? engine_->get_global_environment() : nullptr;
	if (!global_env) {
		return raise_(make_error_code(runtime_error_code::engine_destroyed));
	}
	auto existing_result = global_env->get(class_var_id);
	if (existing_result) {
		script_value existing = std::move(existing_result.value());
		if (!existing.is_null() && existing.is_object()) {
			auto objHolder = existing.get_object_holder();
			if (objHolder && objHolder->type_id == class_definition_type_id_) {
				class_def = std::static_pointer_cast<script_class_definition>(objHolder->data);
				is_redefinition = true;
			}
		}
	}

	if (!class_def) {
		uint64_t type_id = (decl->name_id != UINT64_MAX) ? decl->name_id : symbolizer_->intern(decl->name);
		class_def = std::make_shared<script_class_definition>(decl->name, type_id, engine_);
	} else if (is_redefinition) {
		class_def->clear_asts();
		class_def->clear_instance_method_overloads();
		class_def->clear_static_method_overloads();
	}

	class_def->nocopy(decl->nocopy);

	std::unordered_map<uint64_t, script_value> new_field_defaults;
	std::unordered_map<uint64_t, type_info_ptr> new_field_types; // declared types (typed fields enforce like locals)
	std::unordered_map<uint64_t, type_info_ptr> new_static_types; // declared static types ('auto' = null entry)
	std::unordered_map<uint64_t, script_value> new_methods;
	std::unordered_map<uint64_t, script_value> new_static_methods;
	std::set<uint64_t> new_static_field_ids;

	if (!decl->members.empty()) {
		new_field_defaults.reserve(decl->members.size());
		new_methods.reserve(decl->members.size());
		new_static_methods.reserve(decl->members.size());
	}

	if (!decl->base_classes.empty()) {
		std::vector<std::shared_ptr<class_definition>> parent_defs;
		parent_defs.reserve(decl->base_classes.size());

		for (std::string_view base_name : decl->base_classes) {
			uint64_t base_name_id = symbolizer_->intern(base_name);
			auto [base_class_var_id, base_class_var_name] = symbolizer_->get_class_var_id_with_view(base_name_id);
			script_value base_class_var = make_null();
			auto base_result = environment_->get(base_class_var_id);
			if (base_result) {
				base_class_var = std::move(base_result.value());
			}

			std::shared_ptr<class_definition> base_class_def;

			if (!base_class_var.is_null() && base_class_var.is_object()) {
				auto objHolder = base_class_var.get_object_holder();
				if (objHolder && objHolder->type_id == class_definition_type_id_) {
					base_class_def = std::static_pointer_cast<class_definition>(objHolder->data);
				} else {
					return raise_(make_error_code(runtime_error_code::type_mismatch));
				}
			} else {
				if (class_lookup_) {
					auto cpp_class_def = class_lookup_(std::string(base_name));
					if (cpp_class_def) {
						base_class_def = cpp_class_def;
					} else if (environment_->contains(base_name_id)) {
						return raise_(make_error_code(runtime_error_code::type_mismatch),
							"Constructor found for '{0}' but no class definition available", base_name_id);
					} else {
						return raise_(make_error_code(runtime_error_code::class_not_found),
							"Base class '{0}' not found", base_name_id);
					}
				} else {
					if (environment_->contains(base_name_id)) {
						return raise_(make_error_code(runtime_error_code::type_mismatch),
							"Script class inheriting from C++ class '{0}' requires engine integration", base_name_id);
					} else {
						return raise_(make_error_code(runtime_error_code::class_not_found),
							"Base class '{0}' not found", base_name_id);
					}
				}
			}

			if (base_class_def) {
				parent_defs.push_back(base_class_def);

				auto script_class = std::dynamic_pointer_cast<script_class_definition>(base_class_def);
				if (!script_class && parent_defs.size() == 1) {
					class_def->set_cpp_base_class(base_class_def);
				}
			}
		}

		if (!parent_defs.empty()) {
			if (!class_def->set_parents(parent_defs)) {
				return raise_(make_error_code(runtime_error_code::type_mismatch));
			}
		}
	}

	// Record private:/protected: labels for runtime access enforcement
	apply_member_access_labels(*class_def, *decl, *symbolizer_);

	std::unordered_set<uint64_t> derived_field_names;
	for (const auto& member : decl->members) {
		if (member.declaration->get_type() == node_type::variable_decl) {
			auto* var_decl = static_cast<variable_decl*>(member.declaration.get());
			if (!var_decl->is_static) {
				derived_field_names.insert(var_decl->name_id);
			}
		}
	}

	if (!decl->base_classes.empty() && decl->base_classes.size() > 1) {
		const auto& parent_classes = class_def->get_parent_classes();
		std::unordered_map<uint64_t, std::vector<std::string>> field_sources;

		for (const auto& parent : parent_classes) {
			const auto& parent_fields = parent->get_all_field_defaults();
			for (const auto& [field_id, unused_default] : parent_fields) {
				if (derived_field_names.find(field_id) == derived_field_names.end()) {
					field_sources[field_id].push_back(parent->get_name());
				}
			}
		}

		for (const auto& [field_id, sources] : field_sources) {
			if (sources.size() > 1) {
				return raise_(make_error_code(runtime_error_code::multiple_inheritance),
					"Field inherited from multiple parents", field_id, decl->name_id);
			}
		}
	}

	bool found_constructor = false;

	// Reflection determinism (twin: interpreter class-decl builder, same commit): field
	// DECLARATION order retained per class — instance fields then statics, source order
	std::vector<uint64_t> reflect_field_order_instance;
	std::vector<uint64_t> reflect_field_order_static;
	for (const auto& member : decl->members) {
		auto* var_decl = member.declaration->get_type() == node_type::variable_decl
			? static_cast<variable_decl*>(member.declaration.get()) : nullptr;
		auto* func_decl = member.declaration->get_type() == node_type::function_decl
			? static_cast<function_decl*>(member.declaration.get()) : nullptr;

		if (var_decl) {
			script_value default_val(std::monostate{}, engine_);
			uint64_t field_id = var_decl->name_id;
			expression_ptr initializer_ast = nullptr;

			if (var_decl->initializer) {
				auto* assign_expr = var_decl->initializer->get_type() == node_type::assignment_expr
					? static_cast<assignment_expr*>(var_decl->initializer.get()) : nullptr;
				if (assign_expr) {
					if (assign_expr->target->get_type() == node_type::identifier_expr) {
						auto* ident_expr = static_cast<identifier_expr*>(assign_expr->target.get());
						field_id = ident_expr->symbol_id;
					}
					initializer_ast = assign_expr->value;
				} else {
					initializer_ast = var_decl->initializer;
				}
			}

			if (var_decl->is_static) {
				if (field_id != 0) {
					new_static_field_ids.insert(field_id);
					reflect_field_order_static.push_back(field_id);
				}
				// A reload preserves a static's runtime value; only NEW statics run their initializer
				bool preserve_existing = is_redefinition && field_id != 0 && class_def->has_static_field(field_id);

				if (initializer_ast && !preserve_existing) {
					auto init_result = eval_expression(initializer_ast, nullptr);
					if (!init_result) {
						return raise_from(init_result);
					}
					default_val = initializer_store_value(std::move(init_result.value()), initializer_ast.get());
					if (!default_val.has_valid_engine() && engine_) {
						default_val.set_engine(engine_);
					}
				}
				// Declared type ('auto' parses as null and infers; 'var' stores the any tag);
				// the initializer converts through the field table like an instance initializer
				if (field_id != 0) {
					new_static_types[field_id] = var_decl->type;
				}
				if (!preserve_existing && var_decl->type && var_decl->type->base_type != script_value_type::jai_any_type) {
					if (default_val.is_null()) {
						default_val.set_type_info(var_decl->type);
					} else {
						auto converted = detail::field_store_convert(default_val, var_decl->type, engine_, field_id, class_def.get(), true);
						if (!converted) {
							return raise_from(converted);
						}
						default_val = std::move(converted.value());
					}
				}

				if (field_id != 0 && !preserve_existing) {
					class_def->add_static_field(field_id, default_val);
				}
			} else {
				if (field_id != 0) {
					reflect_field_order_instance.push_back(field_id);
					if (initializer_ast) {
						class_def->add_field_initializer_ast(field_id, initializer_ast);
					}
					new_field_defaults[field_id] = default_val;
					// Declared type ('auto' parses as null and infers; 'var' stores the any tag)
					if (var_decl->type) {
						new_field_types[field_id] = var_decl->type;
					}
				}
			}

		} else if (func_decl) {
			auto method_name = func_decl->name;

			if (method_name == decl->name) {
				found_constructor = true;
				class_def->add_constructor_from_ast(
					std::static_pointer_cast<function_decl>(member.declaration)
				);
			} else if (method_name.size() > 0 && method_name[0] == '~') {
				class_def->add_destructor_from_ast(
					std::static_pointer_cast<function_decl>(member.declaration),
					environment_
				);
			} else {
				uint64_t method_id = func_decl->name_id;
				auto method_ast = std::static_pointer_cast<function_decl>(member.declaration);
				auto definition_env = environment_;

				if (is_redefinition) {
					if (method_ast->is_static) {
						class_def->add_static_script_method(method_name, method_ast, definition_env);
						new_static_methods[method_id] = class_def->get_static_method(method_id, false);
					} else {
						class_def->add_script_method(method_name, method_ast, definition_env);
						new_methods[method_id] = class_def->get_method(method_id, false);
					}
				} else {
					try {
						if (method_ast->is_static) {
							class_def->add_static_script_method(method_name, method_ast, environment_);
						} else {
							class_def->add_method_from_ast(method_name, method_ast, environment_, is_redefinition);
						}
					} catch (const runtime_error& e) {
						std::string error_msg = e.what();
						if (error_msg.find("Undefined variable") == std::string::npos) {
							throw;
						}
					}
				}
			}
		}
	}

	{
		// TYPED ctor value (the bound-method playbook): exec_new / the call paths
		// recover the payload via target<script_callable_thunk>() and run construction
		// + the ctor body without the opaque native re-entry; every other caller goes
		// through operator() (execute_callable with the standard host-boundary rule).
		script_callable ctor_payload;
		ctor_payload.kind = script_callable::kind_type::constructor;
		ctor_payload.cls = class_def;
		if (found_constructor) {
			ctor_payload.definition_env = global_env;
		}
		global_env->define(decl->name_id,
			script_value::make_function(script_callable_thunk{engine_, std::move(ctor_payload)}, engine_));
	}

	reflect_field_order_instance.insert(reflect_field_order_instance.end(),
	                                    reflect_field_order_static.begin(),
	                                    reflect_field_order_static.end());
	class_def->set_field_order(std::move(reflect_field_order_instance));

	if (is_redefinition) {
		std::unordered_map<uint64_t, script_value> field_defaults_with_engine;
		field_defaults_with_engine.reserve(new_field_defaults.size());

		for (const auto& [field_id, value] : new_field_defaults) {
			auto initializer_ast = class_def->get_field_initializer_ast(field_id);
			script_value evaluated_value = value;

			if (initializer_ast) {
				auto init_result = eval_expression(initializer_ast, nullptr);
				if (!init_result) {
					return raise_from(init_result);
				}
				evaluated_value = initializer_store_value(std::move(init_result.value()), initializer_ast.get());
			}

			if (!evaluated_value.has_valid_engine() && engine_) {
				evaluated_value.set_engine(engine_);
			}

			field_defaults_with_engine[field_id] = evaluated_value;
		}

		std::unordered_set<uint64_t> synthesized_accessor_ids;   // a user _get_/_set_ keeps its id
		for (const auto& [field_id, default_val] : field_defaults_with_engine) {
			auto getter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				if (args.empty()) {
					return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
						"Property getter requires 'this' object");
				}
				auto instance = args[0].as<std::shared_ptr<class_instance>>();
				return instance->get_field(field_id);
			};
			auto [getter_id, getter_view] = symbolizer_->get_getter_id_with_view(field_id);
			if (!new_methods.count(getter_id)) {
				new_methods[getter_id] = script_value::make_function(getter, engine_);
				synthesized_accessor_ids.insert(getter_id);
			}

			auto setter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				if (args.size() != 2) {
					return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
						"Property setter requires 'this' and value");
				}
				auto instance = args[0].as<std::shared_ptr<class_instance>>();
				// Typed fields enforce like locals (declared type; auto infers from the
				// initialized value); the converted value is what gets stored and returned
				auto enforced = instance->enforce_field_write(field_id, args[1]);
				if (!enforced) {
					return enforced;
				}
				instance->set_field_unchecked(field_id, enforced.value());
				return std::move(enforced.value());
			};
			auto [setter_id, setter_view] = symbolizer_->get_setter_id_with_view(field_id);
			if (!new_methods.count(setter_id)) {
				new_methods[setter_id] = script_value::make_function(setter, engine_);
				synthesized_accessor_ids.insert(setter_id);
			}
		}

		// Declared types install first so redefine_class flags a retype as fields_changed
		// and migrate_fields converts against the NEW types (retype ruling)
		class_def->replace_field_declared_types(std::move(new_field_types), true);
		class_def->replace_static_declared_types(std::move(new_static_types));

		// Structural identity (position-insensitive AST key): identical reload adopts the
		// freshly minted ASTs but skips per-instance migration. Interpreter twin:
		// interpreter::visit_class_decl.
		std::vector<uint8_t> structural_key;
		bool structurally_identical = false;
		if (class_def->structural_key_shape_matches(decl->members.size(), decl->base_classes.size())) {
			try {
				structural_key = detail::structural_node_key(decl, *symbolizer_);
				structurally_identical = (structural_key == class_def->structural_key());
			} catch (...) {
				structural_key.clear();
			}
		}

		class_def->redefine_class(field_defaults_with_engine, new_methods, new_static_methods, engine_,
		                          structurally_identical);
		class_def->set_auto_accessor_ids(std::move(synthesized_accessor_ids));

		if (structural_key.empty()) {
			try {
				structural_key = detail::structural_node_key(decl, *symbolizer_);
			} catch (...) {
				structural_key.clear();
			}
		}
		if (!structural_key.empty()) {
			class_def->set_structural_key(std::move(structural_key), decl->members.size(), decl->base_classes.size());
		}

		class_def->retain_static_fields(new_static_field_ids);

		// A migrate hook that errored must not surface as this declaration's result
		if (is_unwinding_) {
			is_unwinding_ = false;
			trace_captured_ = false;
			current_exception_.reset();
			active_exception_value_.reset();
		}

		environment_->clear_all_parent_caches();
	} else {
		class_def->replace_field_declared_types(std::move(new_field_types), false);
		class_def->replace_static_declared_types(std::move(new_static_types));
		for (const auto& [field_id, default_val] : new_field_defaults) {
			std::string field_name(symbolizer_->get_string(field_id));
			class_def->add_field(field_name, default_val);

			auto getter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				if (args.empty()) {
					return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
						"Property getter requires 'this' object");
				}
				auto instance = args[0].as<std::shared_ptr<class_instance>>();
				return instance->get_field(field_id);
			};
			auto [getter_id, getter_view] = symbolizer_->get_getter_id_with_view(field_id);
			if (!class_def->has_method(getter_id)) { class_def->add_method_by_id(getter_id, getter, true); }   // a user _get_ keeps its id

			auto setter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				if (args.size() != 2) {
					return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
						"Property setter requires 'this' and value");
				}
				auto instance = args[0].as<std::shared_ptr<class_instance>>();
				// Typed fields enforce like locals (declared type; auto infers from the
				// initialized value); the converted value is what gets stored and returned
				auto enforced = instance->enforce_field_write(field_id, args[1]);
				if (!enforced) {
					return enforced;
				}
				instance->set_field_unchecked(field_id, enforced.value());
				return std::move(enforced.value());
			};
			auto [setter_id, setter_view] = symbolizer_->get_setter_id_with_view(field_id);
			if (!class_def->has_method(setter_id)) { class_def->add_method_by_id(setter_id, setter, true); }
		}
	}

	if (!engine_) {
		return raise_(make_error_code(runtime_error_code::engine_destroyed));
	}
	auto register_result = engine_->get_class_registry().register_script_class(class_def);
	if (!register_result) {
		return raise_from(register_result);
	}
	engine_->bump_class_definition_epoch();
	detail::warn_shadowed_handle_builtins(*engine_, *class_def);

	global_env->define(class_var_id, script_value::make_object("class_definition", class_definition_type_id_, class_def, engine_, false));

	return {};
}

op_status vm_backend::exec_namespace_decl(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<namespace_decl*>(f.code->nodes[ins.a].get());
	try {
		return exec_namespace_decl_node(decl);
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		return {};
	} catch (const std::runtime_error& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		return {};
	}
}

op_status vm_backend::exec_namespace_decl_node(namespace_decl* decl) {
	if (decl->name_id == UINT64_MAX) {
		decl->name_id = symbolizer_->intern(decl->name);
	}

	// Engine-owned namespaces cannot be supplied or reopened by script.
	if (is_reserved_namespace_name(decl->name)) {
		return raise_from(reserved_namespace_error(decl->name_id));
	}

	if (!engine_) {
		return raise_(make_error_code(runtime_error_code::engine_destroyed));
	}
	auto& ns_data = engine_->script_namespaces()[decl->name_id];
	if (!ns_data) {
		ns_data = std::make_shared<script_namespace_data>();
	}

	for (const auto& member_decl : decl->declarations) {
		if (member_decl->get_type() == node_type::function_decl) {
			auto* func_decl = static_cast<function_decl*>(member_decl.get());
			if (func_decl->name_id == UINT64_MAX) {
				func_decl->name_id = symbolizer_->intern(func_decl->name);
			}

			auto& overloads = ns_data->functions[func_decl->name_id];
			for (auto it = overloads.begin(); it != overloads.end(); ++it) {
				if ((*it)->parameters.size() == func_decl->parameters.size()) {
					if (!func_decl->is_override) {
						return raise_(make_error_code(runtime_error_code::type_mismatch),
							"Function '{0}' already exists in namespace '{1}'. Use 'override' keyword to replace it.",
							func_decl->name_id, decl->name_id);
					}
					overloads.erase(it);
					break;
				}
			}

			if (!func_decl->is_override) {
				auto [class_var_id, class_var_name] = symbolizer_->get_class_var_id_with_view(decl->name_id);
				auto class_result = environment_->get(class_var_id);
				if (class_result && class_result.value().is_object()) {
					script_value class_var = std::move(class_result.value());
					auto obj_holder = class_var.get_object_holder();
					if (obj_holder && obj_holder->type_id == class_definition_type_id_) {
						auto class_def = std::static_pointer_cast<class_definition>(obj_holder->data);
						if (class_def->has_static_method_with_arity(func_decl->name_id, func_decl->parameters.size())) {
							return raise_(make_error_code(runtime_error_code::type_mismatch),
								"Function '{0}' in namespace '{1}' collides with static method. Use 'override' to override.",
								func_decl->name_id, decl->name_id);
						}
					}
				}
			}

			overloads.emplace_back(std::make_shared<function_decl>(*func_decl));

		} else if (member_decl->get_type() == node_type::variable_decl) {
			auto* var_decl = static_cast<variable_decl*>(member_decl.get());
			if (var_decl->name_id == UINT64_MAX) {
				var_decl->name_id = symbolizer_->intern(var_decl->name);
			}

			if (var_decl->initializer) {
				auto init_result = eval_expression(var_decl->initializer, nullptr);
				if (!init_result) {
					return raise_from(init_result);
				}
				ns_data->variables[var_decl->name_id] = std::move(init_result.value());
			} else {
				ns_data->variables[var_decl->name_id] = make_null();
			}

		} else if (member_decl->get_type() == node_type::class_decl) {
			auto* class_decl_ptr = static_cast<class_decl*>(member_decl.get());
			if (class_decl_ptr->name_id == UINT64_MAX) {
				class_decl_ptr->name_id = symbolizer_->intern(class_decl_ptr->name);
			}

			VM_TRY(exec_class_decl_node(class_decl_ptr));

			auto [class_var_id, class_var_name] = symbolizer_->get_class_var_id_with_view(class_decl_ptr->name_id);
			if (auto* class_def_var = environment_->get_value_ptr(class_var_id)) {
				if (class_def_var->is_object()) {
					auto obj_holder = class_def_var->get_object_holder();
					if (obj_holder && obj_holder->type_id == class_definition_type_id_) {
						auto class_def = std::static_pointer_cast<class_definition>(obj_holder->data);
						ns_data->classes[class_decl_ptr->name_id] = class_def;
					}
				}
			}

		} else {
			auto compiled = compiler_.compile_standalone_declaration(member_decl);
			frame df;
			df.code = compiled.get();
			df.pin = compiled;
			df.ip = 0;
			df.locals = nullptr;
			df.entry_env = environment_;
			df.stack_base = stack_.size();
			df.top_level = true;
			{
				frame_guard guard(this, &df);
				auto r = run(df);
				if (stack_.size() > df.stack_base) {
					stack_.truncate(df.stack_base);
				}
				if (!r) {
					return raise_from(r);
				}
			}
			if (is_unwinding_) {
				return {};
			}
		}
	}

	return {};
}

// ============================================================
// Script class construction (mirrors interpreter construct_instance)
// ============================================================

checked_result<script_value> vm_backend::execute_method_ast(const std::shared_ptr<function_decl>& ast,
                                                            std::shared_ptr<environment> method_env,
                                                            const std::vector<script_value>& args) {
	// Coroutine methods mint a handle instead of executing (free-coroutine parity).
	// The handle pins its OWN method env carrying 'this' — the caller recycles
	// method_env back to the pool right after this returns — plus the receiver, so
	// the instance stays alive while suspended and 'this' resolves identically
	// across resumes. KEEP BYTE-PARALLEL with interpreter::execute_method_ast.
	if (ast->is_coroutine) {
		script_value this_obj = make_null();
		if (auto this_result = method_env->get(this_id_)) {
			this_obj = std::move(this_result.value());
		}
		auto static_class = method_env->get_class_definition();
		auto coro_env = static_class
			? std::make_shared<environment>(method_env->get_parent(), symbolizer_, static_class)
			: std::make_shared<environment>(method_env->get_parent(), symbolizer_, this_obj);
		coro_env->set_access_context(method_env->get_access_context());
		coro_env->define(this_id_, this_obj);
		auto handle = std::make_shared<coroutine_handle>(engine_);
		handle->set_function(ast, args, coro_env);
		handle->set_receiver(std::move(this_obj));
		return script_value::make_coroutine_handle(coroutine_handle_type_id_, std::static_pointer_cast<void>(handle), engine_);
	}

	script_defined_function script_func(
		ast->name,
		shared_parameters_for(ast.get()),
		ast->return_type,
		ast->body,
		std::move(method_env),
		ast->local_count);
	return call_script_function(script_func, args);
}

checked_result<void> vm_backend::evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                             std::shared_ptr<script_class_definition> class_def,
                                             std::shared_ptr<environment> init_env,
                                             bool skip_parent_recursion) {
	if (!skip_parent_recursion) {
		for (const auto& parent : class_def->get_parent_classes()) {
			auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent);
			if (parent_script_class) {
				auto initialized = evaluate_field_initializers(instance, parent_script_class, init_env, false);
				if (!initialized) { return initialized.error_value(); }
			}
		}
	}

	const auto& field_initializers = class_def->get_field_initializer_asts();
	for (const auto& [field_id, initializer_ast] : field_initializers) {
		if (initializer_ast) {
			// Scalar-literal initializers stamp the literal through materialize_constant
			// (op_const's exact kernel, so the value carries the same engine + type
			// stamps the eval below would produce - auto-field INFERENCE reads them) at
			// the SAME stamp point and order (a super-ctor body that reads this field
			// beforehand sees null either way), minus the whole eval_expression
			// machinery. Scalars only: no shared nodes to alias.
			if (initializer_ast->get_type() == node_type::literal_expr) {
				const auto* lit = static_cast<const literal_expr*>(initializer_ast.get());
				const size_t li = lit->value.raw_storage_index();
				if (li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT ||
				    li == script_value::TYPEID_BOOL || li == script_value::TYPEID_CHAR ||
				    li == script_value::TYPEID_NULL) {
					instance->set_field(field_id, materialize_constant(lit->value));
					continue;
				}
			}
			auto result = eval_expression(initializer_ast, init_env);
			if (!result) {
				if (result.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
					return result.error_value();
				}
				throw runtime_error("Failed to evaluate field initializer for '" + std::string(symbolizer_->get_string(field_id)) + "'");
			}
			script_value field_value = initializer_store_value(std::move(result.value()), initializer_ast.get());
			if (!field_value.has_valid_engine() && engine_) {
				field_value.set_engine(engine_);
			}
			instance->set_field(field_id, field_value);
		}
	}
	return {};
}

// Literal-only twin of evaluate_field_initializers for chains chain_initializers_all_literal
// proved: same order (parents first, then own), same set_field stamps, no env, no eval
void vm_backend::stamp_literal_field_initializers(const std::shared_ptr<class_instance>& instance,
                                                  const std::shared_ptr<script_class_definition>& class_def,
                                                  bool skip_parent_recursion) {
	if (!skip_parent_recursion) {
		for (const auto& parent : class_def->get_parent_classes()) {
			if (parent && parent->is_script_class()) {
				stamp_literal_field_initializers(instance, std::static_pointer_cast<script_class_definition>(parent), false);
			}
		}
	}
	for (const auto& [field_id, initializer_ast] : class_def->get_field_initializer_asts()) {
		if (initializer_ast) {
			instance->set_field(field_id, materialize_constant(static_cast<const literal_expr*>(initializer_ast.get())->value));
		}
	}
}

void vm_backend::apply_field_initializers(const std::shared_ptr<class_instance>& instance,
                                          const std::shared_ptr<class_definition>& class_def) {
	auto script_def = std::dynamic_pointer_cast<script_class_definition>(class_def);
	if (!script_def || !instance) { return; }
	auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);
	auto init_env = std::make_shared<environment>(engine_ ? engine_->get_global_environment() : environment_, symbolizer_);
	init_env->define(symbolizer_->get_this_id(), this_value);
	init_env->set_access_context(class_def.get());
	auto initialized = evaluate_field_initializers(instance, script_def, init_env, false);
	if (!initialized) { throw runtime_error(format_error(initialized, *symbolizer_)); }
}

// Sections 0-3 of construction (overload resolve, instance + init_env + param binds,
// initializer chains, field inits): everything BEFORE the ctor body runs, shared by
// the native path and the in-loop ctor-body entry. Success value = the new instance
// ('this'); the picked overload rides the out-param. Extracted verbatim from
// construct_instance - every error spelling unchanged.
checked_result<script_value> vm_backend::construct_instance_pre_body(std::shared_ptr<script_class_definition> class_def,
                                                            const std::shared_ptr<environment>& definition_env,
                                                            const std::vector<script_value>& args,
                                                            std::shared_ptr<function_decl>& matching_ctor_out) {
	// This native phase owns the constructor depth only until a body takes over.
	if (current_call_depth_ >= limits_->vm_call_depth_limit || detail::native_stack_low()) {
		return detail::raise_call_depth();
	}
	std::optional<detail::call_depth_guard> initializer_depth;
	initializer_depth.emplace(current_call_depth_);
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_c0 = __rdtsc();
#endif
	const auto& ctor_asts = class_def->get_constructor_asts();

	// Trailing defaults open the arity window; within a tier the ctor using FEWER defaults wins
	std::shared_ptr<function_decl> exact_match_ctor;
	std::shared_ptr<function_decl> convertible_match_ctor;
	std::shared_ptr<function_decl> arity_match_ctor;
	size_t exact_defaults = SIZE_MAX, convertible_defaults = SIZE_MAX, arity_defaults = SIZE_MAX;

	for (const auto& ctor_ast : ctor_asts) {
		if (!arity_accepts(ctor_ast->parameters, args.size())) {
			continue;
		}
		const size_t defaults_used = ctor_ast->parameters.size() - args.size();

		if (defaults_used < arity_defaults) {
			arity_match_ctor = ctor_ast;
			arity_defaults = defaults_used;
		}

		bool exact_match = true;
		bool convertible_match = true;
		for (size_t i = 0; i < args.size() && (exact_match || convertible_match); ++i) {
			const auto& param = ctor_ast->parameters[i];
			if (param.type && !param.type->type_name.empty()) {
				auto arg_type = args[i].type();
				if (arg_type == script_value_type::jai_object_type) {
					auto resolved = resolve_member_target(args[i]);
					if (resolved) {
						if (resolved.class_name() != param.type->type_name) {
							exact_match = false;
							convertible_match = false;
						}
					} else {
						exact_match = false;
						convertible_match = false;
					}
				} else {
					if (arg_type != param.type->base_type) {
						exact_match = false;
						bool is_numeric_conversion =
							(arg_type == script_value_type::jai_int_type &&
							 param.type->base_type == script_value_type::jai_float_type) ||
							(arg_type == script_value_type::jai_float_type &&
							 param.type->base_type == script_value_type::jai_int_type);
						if (!is_numeric_conversion) {
							convertible_match = false;
						}
					}
				}
			}
		}

		if (exact_match && defaults_used < exact_defaults) {
			exact_match_ctor = ctor_ast;
			exact_defaults = defaults_used;
		}
		if (convertible_match && defaults_used < convertible_defaults) {
			convertible_match_ctor = ctor_ast;
			convertible_defaults = defaults_used;
		}
	}

	std::shared_ptr<function_decl> matching_ctor = exact_match_ctor;
	if (!matching_ctor) {
		matching_ctor = convertible_match_ctor;
	}
	if (!matching_ctor) {
		matching_ctor = arity_match_ctor;
	}

	if (!matching_ctor) {
		return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
			class_def->get_type_id(), args.data(), args.size(), engine_, true);
	}

#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_c1 = __rdtsc();
#endif
	auto instance = class_def->create_instance();

	auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);

	// (binding evaluates trailing defaults for omitted args; omitted params are simply
	// absent here, like C++ field-inits that can't see ctor params)
	if (!arity_accepts(matching_ctor->parameters, args.size())) {
		return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
			"Constructor parameter count mismatch");
	}
	// The init env serves initializer-list arguments and non-literal field
	// initializers; a literal-only chain with no initializer list never reads it
	std::shared_ptr<environment> init_env;
	if (!matching_ctor->initializers.empty() || !class_def->chain_initializers_all_literal()) {
		init_env = std::make_shared<environment>(definition_env, symbolizer_);
		init_env->define(symbolizer_->get_this_id(), this_value);
		init_env->set_access_context(class_def.get());   // field initializers are class-body code
	}
	for (size_t i = 0; i < matching_ctor->parameters.size() && i < args.size(); ++i) {
		const auto& param = matching_ctor->parameters[i];
		if (param.symbol_id == UINT64_MAX) { param.symbol_id = symbolizer_->intern(param.name); }
		if (init_env) { init_env->define(param.symbol_id, args[i]); }
	}

	bool handled_parent_init = false;
	// C++ delegating-ctor semantics: the target ctor constructs the members; the
	// delegating ctor must not re-run field initializers afterward
	bool delegated_to_this = false;

#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_c2 = __rdtsc();
#endif
	for (const auto& initializer : matching_ctor->initializers) {
		if (initializer.target == "super") {
			if (class_def->get_parent()) {
				std::vector<script_value> init_args;
				init_args.reserve(initializer.arguments.size());

				for (const auto& arg_expr : initializer.arguments) {
					auto result = eval_expression(arg_expr, init_env);
					if (!result) {
						if (result.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
							return result.error_value();
						}
						return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
							"Failed to evaluate constructor initializer argument");
					}
					init_args.push_back(std::move(result.value()));
				}

				auto parent_class = class_def->get_parent();
				if (parent_class) {
					auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent_class);
					if (parent_script_class) {
						// fewest defaults used wins
						const auto& parent_ctor_asts = parent_script_class->get_constructor_asts();
						std::shared_ptr<function_decl> parent_ctor;
						size_t parent_defaults = SIZE_MAX;
						for (const auto& ctor_ast : parent_ctor_asts) {
							if (arity_accepts(ctor_ast->parameters, init_args.size()) &&
							    ctor_ast->parameters.size() - init_args.size() < parent_defaults) {
								parent_ctor = ctor_ast;
								parent_defaults = ctor_ast->parameters.size() - init_args.size();
							}
						}

						if (parent_ctor) {
							struct ctor_chain_entry {
								std::shared_ptr<script_class_definition> script_class;
								std::shared_ptr<function_decl> ctor;
								std::vector<script_value> args;
							};

							std::vector<ctor_chain_entry> ctor_chain;
							ctor_chain.push_back({parent_script_class, parent_ctor, init_args});

							size_t chain_idx = 0;
							while (chain_idx < ctor_chain.size()) {
								auto& entry = ctor_chain[chain_idx];

								auto level_env = std::make_shared<environment>(definition_env, symbolizer_);
								level_env->define("this", this_value);
								level_env->set_access_context(entry.script_class.get());
								for (size_t pi = 0; pi < entry.ctor->parameters.size() && pi < entry.args.size(); ++pi) {
									level_env->define(std::string(entry.ctor->parameters[pi].name), entry.args[pi]);
								}

								for (const auto& init : entry.ctor->initializers) {
									if (init.target == "super") {
										auto ancestor = entry.script_class->get_parent();
										if (ancestor) {
											std::vector<script_value> ancestor_args;
											for (const auto& arg_expr : init.arguments) {
												auto r = eval_expression(arg_expr, level_env);
												if (!r) {
													if (r.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
														return r.error_value();
													}
													return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
														"Failed to evaluate super() argument");
												}
												ancestor_args.push_back(std::move(r.value()));
											}

											auto ancestor_script = std::dynamic_pointer_cast<script_class_definition>(ancestor);
											if (ancestor_script) {
												// fewest defaults used wins
												const auto& ancestor_ctors = ancestor_script->get_constructor_asts();
												std::shared_ptr<function_decl> ancestor_ctor;
												size_t ancestor_defaults = SIZE_MAX;
												for (const auto& ac : ancestor_ctors) {
													if (arity_accepts(ac->parameters, ancestor_args.size()) &&
													    ac->parameters.size() - ancestor_args.size() < ancestor_defaults) {
														ancestor_ctor = ac;
														ancestor_defaults = ac->parameters.size() - ancestor_args.size();
													}
												}
												if (ancestor_ctor) {
													ctor_chain.push_back({ancestor_script, ancestor_ctor, std::move(ancestor_args)});
												} else if (!ancestor_ctors.empty()) {
													return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
														ancestor_script->get_type_id(), ancestor_args.data(), ancestor_args.size(), engine_, true);
												}
											} else {
												auto cpp_name = ancestor->get_name();
												auto cpp_ctor_result = environment_->get(cpp_name);
												if (cpp_ctor_result && cpp_ctor_result.value().is_function()) {
													auto cpp_result = cpp_ctor_result.value().as_function()(ancestor_args);
													if (!cpp_result) {
														return checked_result<script_value>(make_error_code(runtime_error_code::constructor_failed),
															"Failed to call C++ ancestor constructor");
													}
													script_value cpp_obj = std::move(cpp_result.value());

													if (cpp_obj.is_object()) {
														auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
														if (cpp_instance) {
															uint64_t src_id = cpp_instance->get_cpp_object_field_id();
															uint64_t dst_id = instance->get_cpp_object_field_id();
															if (cpp_instance->has_field(src_id)) {
																instance->set_field(dst_id, cpp_instance->get_field(src_id));
															}
														}
													}
												}
											}
										}
										break;
									}
								}
								chain_idx++;
							}

							// Field initializers then ctor bodies, root to leaf
							for (auto it = ctor_chain.rbegin(); it != ctor_chain.rend(); ++it) {
								auto level_init_env = std::make_shared<environment>(definition_env, symbolizer_);
								level_init_env->define("this", this_value);
								level_init_env->set_access_context(it->script_class.get());
								for (size_t pi = 0; pi < it->ctor->parameters.size() && pi < it->args.size(); ++pi) {
									level_init_env->define(std::string(it->ctor->parameters[pi].name), it->args[pi]);
								}

								const auto& field_initializers = it->script_class->get_field_initializer_asts();
								for (const auto& [field_id, initializer_ast] : field_initializers) {
									if (initializer_ast) {
										auto r = eval_expression(initializer_ast, level_init_env);
										if (r) {
											instance->set_field(field_id, initializer_store_value(std::move(r.value()), initializer_ast.get()));
										} else if (r.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
											return r.error_value();
										}
									}
								}

								auto method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
								method_env->set_access_context(it->script_class.get());
								method_env->define("this", this_value);
								initializer_depth.reset();
								auto ctor_result = execute_method_ast(it->ctor, method_env, it->args);
								if (!ctor_result) return ctor_result.error_value();
								initializer_depth.emplace(current_call_depth_);
							}
							handled_parent_init = true;
						} else if (parent_ctor_asts.empty() && init_args.empty()) {
							// Parent only has the default constructor - nothing to do
						} else {
							return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
								"No matching parent constructor found for super()");
						}
					} else {
						try {
							auto parent_name = parent_class->get_name();
							auto ctor_result = environment_->get(parent_name);
							if (ctor_result && ctor_result.value().is_function()) {
								script_value cpp_ctor = std::move(ctor_result.value());
								auto result = cpp_ctor.as_function()(init_args);
								if (!result) {
									return result.error_value();
								}
								script_value cpp_obj = std::move(result.value());

								if (cpp_obj.is_object()) {
									auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
									if (cpp_instance) {
										uint64_t src_field_id = cpp_instance->get_cpp_object_field_id();
										uint64_t dst_field_id = instance->get_cpp_object_field_id();
										if (cpp_instance->has_field(src_field_id)) {
											auto src_value = cpp_instance->get_field(src_field_id);
											instance->set_field(dst_field_id, src_value);
										}
									}
								}
							}
						} catch (const runtime_error&) {
							return checked_result<script_value>(make_error_code(runtime_error_code::constructor_failed),
								"Failed to call C++ parent constructor");
						}
					}
				}
			} else {
				return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
					"Cannot call super() - class has no base class");
			}
		} else if (initializer.target == "this") {
			std::vector<script_value> init_args;
			init_args.reserve(initializer.arguments.size());

			for (const auto& arg_expr : initializer.arguments) {
				auto result = eval_expression(arg_expr, init_env);
				if (!result) {
					if (result.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
						return result.error_value();
					}
					return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
						"Failed to evaluate constructor initializer argument");
				}
				init_args.push_back(std::move(result.value()));
			}

			// fewest defaults used wins
			const auto& all_ctor_asts = class_def->get_constructor_asts();
			std::shared_ptr<function_decl> target_ctor;
			size_t target_defaults = SIZE_MAX;
			for (const auto& ctor_ast : all_ctor_asts) {
				if (ctor_ast != matching_ctor && arity_accepts(ctor_ast->parameters, init_args.size()) &&
				    ctor_ast->parameters.size() - init_args.size() < target_defaults) {
					target_ctor = ctor_ast;
					target_defaults = ctor_ast->parameters.size() - init_args.size();
				}
			}

			if (!target_ctor) {
				return detail::overload_miss(make_error_code(runtime_error_code::no_constructor_found),
					class_def->get_type_id(), init_args.data(), init_args.size(), engine_, true);
			}

			// Declared defaults BEFORE the delegated-to ctor body so the target can override
			auto initialized = evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
			if (!initialized) { return initialized.error_value(); }
			delegated_to_this = true;

			auto target_method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
			target_method_env->set_access_context(class_def.get());
			target_method_env->define("this", this_value);

			initializer_depth.reset();
			auto target_result = execute_method_ast(target_ctor, target_method_env, init_args);
			if (!target_result) return target_result.error_value();
			initializer_depth.emplace(current_call_depth_);
		}
	}

#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_c3 = __rdtsc();
#endif
	if (!delegated_to_this) {
		if (init_env) {
			auto initialized = evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
			if (!initialized) { return initialized.error_value(); }
		} else if (!instance->seeded_from_plan() || handled_parent_init) {
			// A literal-only chain's initializers ARE the defaults create_instance's plan just
			// seeded (post-conversion); the re-stamp only matters after a parent ctor body ran
			stamp_literal_field_initializers(instance, class_def, handled_parent_init);
		}
	}

#ifdef JAISCRIPT_VM_PROFILE
	{
		++profile_ctor_count_;
		profile_ctor_cyc_[0] += prof_c1 - prof_c0;
		profile_ctor_cyc_[1] += prof_c2 - prof_c1;
		profile_ctor_cyc_[2] += prof_c3 - prof_c2;
		profile_ctor_cyc_[3] += __rdtsc() - prof_c3;
	}
#endif
	matching_ctor_out = matching_ctor;
	return this_value;
}

checked_result<script_value> vm_backend::construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                            std::shared_ptr<environment> definition_env,
                                                            const std::vector<script_value>& args) {
	std::shared_ptr<function_decl> matching_ctor;
	auto pre = construct_instance_pre_body(class_def, definition_env, args, matching_ctor);
	if (!pre) {
		return pre;
	}
	script_value this_value = std::move(pre.value());
#ifdef JAISCRIPT_VM_PROFILE
	const uint64_t prof_c4 = __rdtsc();
#endif
	auto method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
	method_env->set_access_context(class_def.get());
	method_env->define(symbolizer_->get_this_id(), this_value);

	auto result = execute_method_ast(matching_ctor, method_env, args);

#ifdef JAISCRIPT_VM_PROFILE
	profile_ctor_cyc_[4] += __rdtsc() - prof_c4;
#endif
	return result;
}

// In-loop constructor entry (op_new + direct ctor calls): the construction protocol
// runs natively exactly as before, then the ctor BODY enters the dispatch loop as a
// method-env-closured frame. push_script_frame is call_script_function's in-loop twin,
// so explicit-return / fall-off (implicit this) / unwind semantics replicate through
// the same machinery execute_method_ast delegated to. result_stamp = the shared_ptr
// arm's post-call type stamp, applied at the frame completion doors
// (call_record::ctor_result_stamp). Coroutine ctors keep the native tail verbatim.
op_status vm_backend::enter_constructor_in_loop(frame& f,
                                                const std::shared_ptr<script_class_definition>& class_def,
                                                const std::shared_ptr<environment>& definition_env,
                                                const std::vector<script_value>& args,
                                                type_info* result_stamp) {
	std::shared_ptr<function_decl> matching_ctor;
	auto pre = construct_instance_pre_body(class_def, definition_env, args, matching_ctor);
	if (!pre) {
		return raise_from(pre);
	}
	script_value this_value = std::move(pre.value());
	if (matching_ctor->is_coroutine) [[unlikely]] {
		auto method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
		method_env->set_access_context(class_def.get());
		method_env->define(symbolizer_->get_this_id(), this_value);
		auto result = execute_method_ast(matching_ctor, method_env, args);
		if (!result) {
			return raise_from(result);
		}
		script_value value = std::move(result.value());
		if (result_stamp && value.type() == script_value_type::jai_object_type) {
			value.set_type_info(type_info_ptr{result_stamp});
		}
		stack_.push_back(std::move(value));
		return {};
	}
	// Transient function object: push_script_frame copies/derives everything the frame
	// needs (name into the record, chunk from the body-keyed cache, env installed during
	// setup) and the parameter vector is the decl-owned shared instance - nothing points
	// back into this local after the push returns.
	// The closure env is a CARRIER: push_script_frame's eager setup copies its parent,
	// this and access context into a pooled scope env and never chains through it, so
	// it comes from the pool and goes back right after the push
	script_defined_function ctor_func(matching_ctor->name, shared_parameters_for(matching_ctor.get()),
	                                  matching_ctor->return_type, matching_ctor->body,
	                                  acquire_method_scope_env(definition_env, this_value, class_def.get()),
	                                  matching_ctor->local_count);
	struct carrier_release {
		vm_backend* vm;
		std::shared_ptr<environment>& env;
		~carrier_release() { if (env) { vm->release_scope_env(std::move(env)); } }
	} carrier{this, ctor_func.closure_env};
	op_status pushed;
	try {
		pushed = push_script_frame(f, make_null(), ctor_func, args, 0, args.size(), nullptr);
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
	if (pushed == op_status::failed) {
		return pushed;
	}
	if (switch_to_) {
		call_records_[call_records_top_ - 1]->ctor_result_stamp = result_stamp;
	}
	return {};
}

checked_result<script_value> vm_backend::construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                    const std::vector<script_value>& args) {
	if (!args.empty()) {
		return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
			"Default constructor takes no arguments");
	}

	if (current_call_depth_ >= limits_->vm_call_depth_limit || detail::native_stack_low()) {
		return detail::raise_call_depth();
	}
	detail::call_depth_guard initializer_depth(current_call_depth_);

	auto instance = class_def->create_instance();

	auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);

	if (class_def->chain_initializers_all_literal()) {
		if (!instance->seeded_from_plan()) { stamp_literal_field_initializers(instance, class_def, false); }
		return this_value;
	}
	// Chain root (the global env, or a worker's root): a bounded walk, no visited set
	auto current_env = environment_;
	for (int hops = 0; current_env && current_env->get_parent() && hops < 65536; ++hops) {
		current_env = current_env->get_parent();
	}
	auto global_env = current_env ? current_env : environment_;

	auto init_env = std::make_shared<environment>(global_env, symbolizer_);
	init_env->define(symbolizer_->get_this_id(), this_value);
	init_env->set_access_context(class_def.get());
	auto initialized = evaluate_field_initializers(instance, class_def, init_env);
	if (!initialized) { return initialized.error_value(); }

	return this_value;
}

// ============================================================
// Exceptions, switch dispatch, range-for iteration
// ============================================================

op_status vm_backend::exec_throw(frame& f, const vm_instruction& ins) {
	const ast_node* node = ins.b != k_invalid_u32 ? f.code->nodes[ins.b].get() : nullptr;
	if (ins.a) {
		script_value val = std::move(stack_.back());
		stack_.pop_back();
		active_exception_value_ = val;
		std::string message = val.to_string();
		current_exception_ = script_exception(message, node ? node->location : source_location{});
	} else {
		// Re-throw protocol parity: a bare rethrow with no active exception is a real
		// C++ throw (caught at the nearest call boundary, exactly like the interpreter)
		if (!current_exception_) {
			throw script_exception("No exception to re-throw", node ? node->location : source_location{});
		}
	}
	is_unwinding_ = true;
	return {};
}

op_status vm_backend::exec_try_push(frame& f, const vm_instruction& ins) {
	try_record rec;
	rec.owner = &f;
	rec.handler_ip = ins.a;
	rec.catch_var = f.code->symbols[ins.b];
	rec.catch_slot = ins.c;
	rec.saved_exception = current_exception_;
	rec.saved_exception_value = active_exception_value_;
	rec.saved_catch_var_id = current_catch_var_id_;
	rec.saved_unwinding = is_unwinding_;
	rec.stack_size = stack_.size();
	rec.iter_size = iter_states_.size();
	rec.cfor_size = cfor_states_.size();
	rec.pending_size = pending_callees_.size();
	rec.entry_env = environment_;

	// Inside a catch block the exception state survives so a bare rethrow works
	if (current_catch_var_id_ == 0) {
		current_exception_.reset();
		active_exception_value_ = make_null();
	}
	is_unwinding_ = false;
	trace_captured_ = false;
	current_catch_var_id_ = 0;

	try_records_.push_back(std::move(rec));
	return {};
}

op_status vm_backend::exec_try_pop(frame&, const vm_instruction&) {
	if (try_records_.empty()) {
		return raise_(make_error_code(runtime_error_code::internal_error), "try record stack underflow");
	}
	current_catch_var_id_ = try_records_.back().saved_catch_var_id;
	try_records_.pop_back();
	return {};
}

op_status vm_backend::exec_catch_end(frame&, const vm_instruction&) {
	if (try_records_.empty()) {
		return raise_(make_error_code(runtime_error_code::internal_error), "try record stack underflow");
	}
	try_record rec = std::move(try_records_.back());
	try_records_.pop_back();
	if (rec.catch_env_pushed) {
		pop_scopes_pooled(1);
	}
	current_catch_var_id_ = 0;
	if (!is_unwinding_) {
		current_exception_.reset();
		active_exception_value_ = make_null();
	} else if (rec.saved_unwinding) {
		current_exception_ = rec.saved_exception;
		active_exception_value_ = rec.saved_exception_value;
	}
	current_catch_var_id_ = rec.saved_catch_var_id;
	return {};
}

bool vm_backend::unwind_to_handler(frame& f, const error_propagator* failure) {
	// Terminal errors (budget overrun, escalated memory cap) can NEVER be caught from
	// script: pop this frame's records and keep unwinding to the host boundary
	// (KEEP BYTE-PARALLEL with the interpreter visit_try_stmt terminal skip).
	if (limits_->terminal_error) [[unlikely]] {
		while (!try_records_.empty() && try_records_.back().owner == &f) {
			current_catch_var_id_ = try_records_.back().saved_catch_var_id;
			try_records_.pop_back();
		}
		return false;
	}
	while (!try_records_.empty()) {
		try_record& rec = try_records_.back();
		if (rec.owner != &f) {
			return false;
		}
		if (rec.in_catch) {
			// Error/throw escaping a catch block: restore the catch var and keep unwinding.
			// A rethrow-preserved outer exception is restored only on the flag path.
			if (!failure && is_unwinding_ && rec.saved_unwinding) {
				current_exception_ = rec.saved_exception;
				active_exception_value_ = rec.saved_exception_value;
			}
			current_catch_var_id_ = rec.saved_catch_var_id;
			try_records_.pop_back();
			continue;
		}
		if (!failure && !(is_unwinding_ && current_exception_)) {
			current_catch_var_id_ = rec.saved_catch_var_id;
			try_records_.pop_back();
			continue;
		}
		if (failure) {
			std::string message = format_error(*failure, *symbolizer_);
			active_exception_value_ = script_value(message, engine_);
			current_exception_ = script_exception(message);
		}
		is_unwinding_ = false;
		trace_captured_ = false;
		current_catch_var_id_ = rec.catch_var;
		if (stack_.size() > rec.stack_size) {
			stack_.truncate(rec.stack_size);
		}
		if (iter_states_.size() > rec.iter_size) {
			release_iter_slots(f, rec.iter_size);
			iter_states_.erase(iter_states_.begin() + rec.iter_size, iter_states_.end());
		}
		if (cfor_states_.size() > rec.cfor_size) {
			cfor_states_.erase(cfor_states_.begin() + rec.cfor_size, cfor_states_.end());
		}
		if (pending_callees_.size() > rec.pending_size) {
			pending_callees_.erase(pending_callees_.begin() + rec.pending_size, pending_callees_.end());
		}
		environment_ = rec.entry_env;
		// Bind the catch variable as a local of the catch body (KEEP BYTE-PARALLEL with
		// the interpreter's visit_try_stmt bind): frame slot, or a scope env the matching
		// op_catch_end pops (exec_iter_init's env_pushed shape)
		if (rec.catch_var != 0 && !symbolizer_->get_string(rec.catch_var).empty()) {
			script_value caught = active_exception_value_ ? *active_exception_value_ : make_null();
			if (rec.catch_slot != k_invalid_u32 && f.locals && !f.top_level) {
				frame_slot_set(f, rec.catch_slot, std::move(caught));
			} else {
				JAI_ENV_CENSUS(0); environment_ = acquire_scope_env(environment_);
				environment_->define(rec.catch_var, std::move(caught));
				rec.catch_env_pushed = true;
			}
		}
		f.ip = rec.handler_ip;
		rec.in_catch = true;
		return true;
	}
	return false;
}

bool vm_backend::handle_op_error(frame*& fp, size_t records_base) {
	error_propagator failure = pending_error_;
	for (;;) {
		if (unwind_to_handler(*fp, &failure)) {
			return true;
		}
		if (call_records_top_ == records_base) {
			return false;
		}
		if (!trace_captured_) {
			capture_stack_trace();
		}
		call_record& rec = *call_records_[call_records_top_ - 1];
		pop_script_frame_core(rec);
		fp = rec.caller;
	}
}

bool vm_backend::handle_throw_unwind(frame*& fp, size_t records_base) {
	for (;;) {
		if (unwind_to_handler(*fp, nullptr)) {
			return true;
		}
		if (call_records_top_ == records_base) {
			return false;
		}
		if (is_unwinding_ && !trace_captured_) {
			capture_stack_trace();
		}
		// An unwinding callee "completes" with its implicit this-return, conversion
		// skipped — the native path's cleanup + exec_call result push, frame by frame
		call_record& rec = *call_records_[call_records_top_ - 1];
		script_value result = implicit_result_for_record(rec);
		pop_script_frame_core(rec);
		fp = rec.caller;
		stack_.push_back(std::move(result));
	}
}

op_status vm_backend::exec_case_eq(frame&, const vm_instruction&) {
	script_value case_value = std::move(stack_.back());
	stack_.pop_back();
	script_value switch_value = std::move(stack_.back());
	stack_.pop_back();
	bool matches = false;
	try {
		matches = (switch_value == case_value);
	} catch (const std::exception&) {
		matches = false;
	}
	stack_.push_back(script_value(matches, engine_));
	return {};
}

op_status vm_backend::exec_iter_init(frame& f, const vm_instruction& ins) {
	script_value container = std::move(stack_.back());
	stack_.pop_back();
	// Nested-read sources (for (x : m["a"])) arrive as reference wrappers (rhs-lvalue
	// read shape) - iterate the TARGET like every other consumer normalizes. The
	// deref'd handle shares the live node, so by-ref element mutation still lands in
	// the original container. Copy out through a temp: assigning deref() straight in
	// destroys the holder that owns the deref target. (KEEP BYTE-PARALLEL with the
	// interpreter's visit_range_for_stmt)
	if (container.is_reference()) [[unlikely]] {
		script_value derefed = container.deref();
		container = std::move(derefed);
	}

	const iter_proto& proto = f.code->iter_protos[ins.a];
	iter_state state;
	state.env_pushed = proto.slot == SIZE_MAX || !f.locals || f.top_level;
	if (state.env_pushed) {
		JAI_ENV_CENSUS(0); environment_ = acquire_scope_env(environment_);
	} else if (proto.is_reference) {
		state.slot = proto.slot;
		state.owner = &f;
	}
	if (container.is_array()) {
		state.container.emplace(std::move(container));
		state.index = 0;
		state.is_map = false;
	} else if (container.is_string()) {
		// By value only, one char per element like s[i]; a char is not a cell, so the
		// by-reference binding raises (KEEP BYTE-PARALLEL with the interpreter's visit_range_for_stmt)
		if (proto.is_reference) {
			if (state.env_pushed) pop_scope();
			return raise_(make_error_code(runtime_error_code::invalid_reference), detail::string_range_for_by_ref_text);
		}
		state.container.emplace(std::move(container));
		state.index = 0;
		state.is_string = true;
	} else if (container.is_map()) {
		state.container.emplace(std::move(container));
		state.is_map = true;
		auto& map_storage = state.container->get_map_storage();
		state.map_it = map_storage->begin();
		if (!map_storage->empty()) {
			auto pair_result = environment_->get_ref(pair_id_);
			if (!pair_result) {
				if (state.env_pushed) pop_scope();
				return raise_from(pair_result);
			}
			const script_value& pair_ctor = pair_result.value().get();
			if (!pair_ctor.is_function()) {
				if (state.env_pushed) pop_scope();
				return raise_(make_error_code(runtime_error_code::stdlib_not_loaded),
					"'pair' type not registered - make sure stdlib is loaded");
			}
			state.pair_ctor.emplace(pair_ctor);
		}
	} else if (container.is_object()) {
		auto holder = container.get_object_holder();
		if (holder && holder->type_id == coroutine_handle_type_id_) {
			state.coroutine = std::static_pointer_cast<coroutine_handle>(holder->data);
			iter_states_.push_back(std::move(state));
			return {};
		}
		if (state.env_pushed) pop_scope();
		return raise_from(detail::not_iterable(container, engine_));
	} else {
		if (state.env_pushed) pop_scope();
		return raise_from(detail::not_iterable(container, engine_));
	}
	iter_states_.push_back(std::move(state));
	return {};
}

op_status vm_backend::exec_iter_next(frame& f, const vm_instruction& ins) {
	const iter_proto& proto = f.code->iter_protos[ins.a];
	if (iter_states_.empty()) {
		return raise_(make_error_code(runtime_error_code::internal_error), "iteration state stack underflow");
	}
	iter_state& state = iter_states_.back();

	if (state.coroutine) {
		// Drive the fiber: resume, stop (discard value) once it completes, else the
		// yielded value is the loop element (mirrors the interpreter's while(!done) loop).
		auto handle = state.coroutine;   // copy; resume may reallocate iter_states_
		if (handle->done()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		auto resume_result = handle->resume(engine_);
		if (!resume_result) {
			return raise_from(resume_result);
		}
		if (handle->done()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		script_value value = std::move(resume_result.value());
		if (proto.slot != SIZE_MAX && f.locals && !f.top_level) {
			frame_slot_set(f, proto.slot, std::move(value));
		} else {
			environment_->define(proto.var_symbol, std::move(value));
		}
		stack_.push_back(script_value(true, engine_));
		return {};
	}

	script_value element = make_null();
	if (state.is_string) {
		// Live size each iteration: the body can alias and shrink the shared string
		const script_string& str = state.container->unchecked_as_string();
		if (state.index >= str.size()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		if (execution_limit_exhausted()) [[unlikely]] {
			return raise_from(execution_limit_failure());
		}
		element = script_value(static_cast<script_char>(str[state.index]), engine_);
		++state.index;
	} else if (!state.is_map) {
		// Live size each iteration: the body can alias and shrink the shared array
		auto& array_storage = state.container->get_array_storage();
		if (state.index >= array_storage->size()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		if (execution_limit_exhausted()) [[unlikely]] {
			return raise_from(execution_limit_failure());
		}
		if (proto.is_reference) {
			// Reallocation-safe container+index reference, never a raw element pointer.
			// RULED (2026-07-06): the ref carries the container's declared element type —
			// for (auto& x : intArr) x = "s" errors like intArr[i] = "s"; var (any-tagged)
			// containers stay unconstrained. KEEP BYTE-PARALLEL with the interpreter twin.
			type_info_ptr element_constraint = nullptr;
			if (auto tag = state.container->get_type_info(); tag && tag->base_type == script_value_type::jai_array_type) {
				type_info_ptr et = tag->element_type();
				if (et && et->base_type != script_value_type::jai_any_type &&
				    et->base_type != script_value_type::jai_null_type) {
					element_constraint = et;
				}
			}
			// Cursor (frame-slot loops only; top-level loops keep the per-iteration mint so
			// destructor timing stays byte-parallel with the interpreter twin): the slot's
			// previous holder, when nothing retained it, moves to this index
			if (proto.slot != SIZE_MAX && f.locals && !f.top_level) {
				if (script_value* cur = frame_slot(f, proto.slot)) {
					if (auto* cursor = cur->element_cursor_holder();
					    cursor && cursor->container.get() == array_storage.get() &&
					    cursor->container_element_type == element_constraint) {
						cursor->container_index = state.index;
						++state.index;
						stack_.push_back(script_value(true, engine_));
						return {};
					}
				}
			}
			element = script_value::make_element_reference(array_storage, state.index, engine_, element_constraint);
		} else if (array_storage->kind() == script_array::kind_t::obj) {
			element = clone_for_assignment(array_storage->get(state.index, engine_));   // object slots deep-copy like boxed elements
		} else if (array_storage->is_typed()) {
			element = array_storage->get(state.index, engine_);   // raw buffer read
		} else {
			// Copy binding: values deep-copy per iteration, shared_ptr elements share
			element = clone_for_assignment(array_storage->values()[state.index]);
		}
		++state.index;
	} else {
		auto& map_storage = state.container->get_map_storage();
		if (state.map_it == map_storage->end()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		if (execution_limit_exhausted()) [[unlikely]] {
			return raise_from(execution_limit_failure());
		}
		std::vector<script_value> args;
		if (proto.is_reference) {
			args.push_back(state.map_it->first);
			args.push_back(script_value::make_map_entry_reference(state.container->get_map_storage(),
			                                                      state.map_it->first, engine_, nullptr));
		} else {
			// String keys share their node (the copy-on-write gate detaches any write -
			// invariants.md 11), other key kinds snapshot; values deep-copy, shared_ptr share
			args.push_back(state.map_it->first.is_string() ? state.map_it->first : state.map_it->first.clone());
			args.push_back(clone_for_assignment(state.map_it->second));
		}
		const script_function& pair_func = state.pair_ctor->as_function();
		auto pair_result = pair_func(args);
		if (!pair_result) {
			return raise_from(pair_result);
		}
		element = std::move(pair_result.value());
		++state.map_it;
	}

	if (proto.slot != SIZE_MAX && f.locals && !f.top_level) {
		frame_slot_set(f, proto.slot, std::move(element));
	} else {
		environment_->define(proto.var_symbol, std::move(element));
	}
	stack_.push_back(script_value(true, engine_));
	return {};
}

// KEEP BYTE-PARALLEL with the interpreter's visit_range_for_stmt slot release
void vm_backend::release_iter_slots(frame& f, size_t from) {
	for (size_t i = from; i < iter_states_.size(); ++i) {
		const iter_state& st = iter_states_[i];
		if (st.slot == SIZE_MAX || st.owner != &f) { continue; }
		if (script_value* cell = frame_slot(f, st.slot); cell && cell->is_reference()) {
			*cell = script_value(std::monostate{}, engine_);
		}
	}
}

op_status vm_backend::exec_iter_pop(frame& f, const vm_instruction&) {
	bool env_pushed = true;
	if (!iter_states_.empty()) {
		env_pushed = iter_states_.back().env_pushed;
		release_iter_slots(f, iter_states_.size() - 1);
		iter_states_.pop_back();
	}
	if (env_pushed) {
		pop_scopes_pooled(1);
	}
	return {};
}

op_status vm_backend::exec_include(frame& f, const vm_instruction& ins) {
	std::string path;
	if (ins.b) {
		script_value path_value = std::move(stack_.back());
		stack_.pop_back();
		const script_value& resolved = path_value.deref();
		if (resolved.type() != script_value_type::jai_string_type) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		path = resolved.as<std::string>();
	} else {
		path = std::string(f.code->messages[ins.a]);
	}
	if (!engine_) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	auto resolve_result = jai::resolve_include_path(path, engine_);
	if (!resolve_result) {
		return raise_from(resolve_result);
	}
	std::string resolved_path = std::move(resolve_result.value());

	std::ifstream file(resolved_path);
	if (!file.is_open()) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string content = buffer.str();

	// Re-entrant into this backend's execute(); prepare_for_execution preserves any
	// running-coroutine state. An error in the included file surfaces as unwinding.
	// Pass the resolved path so included nodes carry their real filename.
	try {
		script_value result = engine_->execute_source(content, jai::instance_variables{}, resolved_path);
		stack_.push_back(std::move(result));
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
	}
	return {};
}

op_status vm_backend::exec_import(frame& f, const vm_instruction& ins) {
	std::string path;
	if (ins.b) {
		script_value path_value = std::move(stack_.back());
		stack_.pop_back();
		const script_value& resolved = path_value.deref();
		if (resolved.type() != script_value_type::jai_string_type) {
			return raise_(make_error_code(runtime_error_code::type_mismatch));
		}
		path = resolved.as<std::string>();
	} else {
		path = std::string(f.code->messages[ins.a]);
	}
	if (!engine_) {
		return raise_(make_error_code(runtime_error_code::type_mismatch));
	}

	auto resolve_result = jai::resolve_include_path(path, engine_);
	if (!resolve_result) {
		return raise_from(resolve_result);
	}
	std::string resolved_path = std::move(resolve_result.value());

	try {
		script_value result = engine_->execute_import(resolved_path);
		stack_.push_back(std::move(result));
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
	}
	return {};
}

// Ref-return producers (return_stmt::binds_reference): the return expression binds
// as a reference instead of evaluating to a copy; convert_return_value passes the
// handle through. KEEP semantics parallel with the interpreter's visit_return_stmt.
op_status vm_backend::exec_ref_return_bind(frame& f, const vm_instruction& ins) {
	const uint64_t symbol_id = f.code->symbols[ins.b];
	if (ins.c & load_flag_global_only) {
		// ::name - GLOBAL env only (slots, captures, member scope all skipped)
		if (script_value* g = engine_->get_global_environment()->get_value_ptr(symbol_id)) {
			stack_.push_back(share_env_ref(*g));
			return {};
		}
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Cannot take reference of undefined variable", symbol_id);
	}
	script_value* storage = nullptr;
	if (ins.a != k_invalid_u32 && f.locals) {
		storage = frame_slot(f, ins.a);
	}
	if (!storage && environment_) {
		storage = environment_->get_value_ptr(symbol_id);
	}
	if (!storage) {
		storage = frame_this_member_ptr(f, symbol_id);   // method-lazy: bare field targets
	}
	if (!storage) {
		// A hybrid's bare C++ property (KEEP BYTE-PARALLEL with the interpreter's visit_return_stmt)
		if (auto minted = bind_bare_cpp_property_ref(f, symbol_id)) {
			if (!*minted) {
				return raise_from(*minted);
			}
			stack_.push_back(std::move(minted->value()));
			return {};
		}
		return raise_(make_error_code(runtime_error_code::undefined_variable),
			"Cannot take reference of undefined variable", symbol_id);
	}
	stack_.push_back(share_env_ref(*storage));
	return {};
}

op_status vm_backend::exec_ref_return_lvalue(frame& f, const vm_instruction& ins) {
	ensure_frame_member_scope(f, environment_.get());   // kernel reads the frame's member scope
	auto resolved = detail::resolve_ref_lvalue(
		static_cast<const expression*>(f.code->nodes[ins.a].get()),
		caller_view(&f), environment_.get(), engine_, symbolizer_);
	if (!resolved) {
		return raise_from(resolved);
	}
	stack_.push_back(std::move(resolved.value()));
	return {};
}

// cast<T>(e) / cast<T&>(e) / cast<shared_ptr<T>>(e): operand shape, class verdict and
// class-target outcomes come from detail/cast_kernel.hpp; primitive/container targets
// convert through the assignment table. KEEP BYTE-PARALLEL with interpreter::visit_cast_expr.
op_status vm_backend::exec_cast(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<cast_expr*>(f.code->nodes[ins.a].get());
	script_value operand = make_null();
	switch (static_cast<detail::cast_operand_mode>(ins.b)) {
		case detail::cast_operand_mode::identifier: {
			// The ref-decl identifier arm (exec_decl_ref_ident): slot-first, then env,
			// then method-lazy bare fields
			const uint64_t target_sym = f.code->symbols[ins.c];
			script_value* targetPtr = nullptr;
			auto* target_ident = static_cast<identifier_expr*>(expr->operand.get());
			const bool global_only = target_ident->global_qualified;
			if (global_only) {
				// ::name - GLOBAL env only (resolve_local_or_env's door in the interpreter)
				targetPtr = engine_->get_global_environment()->get_value_ptr(target_sym);
			} else if (target_ident->slot_index != SIZE_MAX && f.locals && !f.top_level) {
				targetPtr = frame_slot(f, target_ident->slot_index);
			}
			if (!targetPtr && !global_only) {
				targetPtr = environment_->get_value_ptr(target_sym);
			}
			if (!targetPtr && !global_only) {
				targetPtr = frame_this_member_ptr(f, target_sym);
			}
			if (!targetPtr) {
				std::optional<checked_result<script_value>> minted;
				if (!global_only) { auto b = bind_bare_cpp_property_ref(f, target_sym); if (b) { minted.emplace(std::move(*b)); } }
				if (!minted) {
					return raise_(make_error_code(runtime_error_code::undefined_variable), "Cannot take reference of undefined variable", target_sym);
				}
				if (!*minted) {
					return raise_from(*minted);
				}
				operand = std::move(minted->value());
				break;
			}
			if (targetPtr->is_reference()) {
				auto aliased = vm_decl_ref_alias(*targetPtr, engine_);
				if (!aliased) {
					return raise_from(aliased);
				}
				operand = std::move(aliased.value());
			} else {
				operand = share_env_ref(*targetPtr);
			}
			break;
		}
		case detail::cast_operand_mode::lvalue_chain: {
			ensure_frame_member_scope(f, environment_.get());   // kernel reads the frame's member scope
			auto resolved = detail::resolve_ref_lvalue(expr->operand.get(), caller_view(&f),
			                                           environment_.get(), engine_, symbolizer_);
			if (!resolved) {
				if (resolved.error() != make_error_code(runtime_error_code::invalid_reference)) {
					return raise_from(resolved);
				}
				return raise_(make_error_code(runtime_error_code::invalid_reference), detail::cast_non_lvalue_text);
			}
			operand = std::move(resolved.value());
			break;
		}
		case detail::cast_operand_mode::value:
			operand = std::move(stack_.back());
			stack_.pop_back();
			break;
	}

	if (expr->by_reference) {
		auto out = detail::cast_reference(detail::cast_pin_temporary(std::move(operand), engine_), *expr, engine_, symbolizer_);
		if (!out) {
			return raise_from(out);
		}
		stack_.push_back(std::move(out.value()));
		return {};
	}

	const bool lvalue_source = operand.is_reference() || detail::cast_lvalue_source(expr->operand.get());
	if (operand.is_reference()) {
		script_value derefed = operand.deref();
		operand = std::move(derefed);
	}
	if (detail::cast_target_is_class(*expr->target)) {
		auto out = detail::cast_class_value(std::move(operand), *expr, lvalue_source, engine_, symbolizer_);
		if (!out) {
			return raise_from(out);
		}
		stack_.push_back(std::move(out.value()));
		return {};
	}
	if (detail::cast_char_rule_applies(operand, *expr->target)) {
		auto out = detail::cast_char_value(operand, *expr, engine_, symbolizer_);
		if (!out) {
			return raise_from(out);
		}
		script_value converted = std::move(out.value());
		converted.set_type_info(expr->target);
		stack_.push_back(std::move(converted));
		return {};
	}
	if (lvalue_source) {
		script_value copy = clone_for_assignment(operand);
		operand = std::move(copy);
	}
	auto enforced = enforce_type_compatibility(operand, expr->target);
	if (!enforced) {
		if (enforced.error() == make_error_code(runtime_error_code::invalid_numeric_operand)) {
			return raise_from(enforced);
		}
		return raise_from(detail::cast_error(operand, *expr, symbolizer_));
	}
	script_value out = std::move(enforced.value());
	out.set_type_info(expr->target);
	stack_.push_back(std::move(out));
	return {};
}

// ============================================================
// Dispatch loop
// ============================================================

op_status vm_backend::exec_extended(frame& f, const vm_instruction& ins) {
	switch (ins.op) {
		case opcode::op_this: return exec_this(f, ins);
		case opcode::op_super: return exec_super(f, ins);
		case opcode::op_from_this: return exec_from_this(f, ins);
		case opcode::op_get_member: return exec_get_member(f, ins);
		case opcode::op_get_static: return exec_get_static(f, ins);
		case opcode::op_set_member: return exec_set_member(f, ins);
		case opcode::op_set_static: return exec_set_static(f, ins);
		case opcode::op_member_compound: return exec_member_compound(f, ins);
		case opcode::op_call_method: return exec_call_method(f, ins);
		case opcode::op_push_fused: return exec_push_fused(f, ins);
		case opcode::op_new: return exec_new(f, ins);
		case opcode::op_class_decl: return exec_class_decl(f, ins);
		case opcode::op_namespace_decl: return exec_namespace_decl(f, ins);
		case opcode::op_enum_decl: return exec_enum_decl(f, ins);
		case opcode::op_throw: return exec_throw(f, ins);
		case opcode::op_try_push: return exec_try_push(f, ins);
		case opcode::op_try_pop: return exec_try_pop(f, ins);
		case opcode::op_catch_end: return exec_catch_end(f, ins);
		case opcode::op_return_convert: return exec_return_convert(f, ins);
		case opcode::op_case_eq: return exec_case_eq(f, ins);
		case opcode::op_iter_init: return exec_iter_init(f, ins);
		case opcode::op_iter_next: return exec_iter_next(f, ins);
		case opcode::op_iter_pop: return exec_iter_pop(f, ins);
		case opcode::op_include: return exec_include(f, ins);
		case opcode::op_import: return exec_import(f, ins);
		case opcode::op_ref_return_bind: return exec_ref_return_bind(f, ins);
		case opcode::op_ref_return_lvalue: return exec_ref_return_lvalue(f, ins);
		case opcode::op_index_store: return exec_index_store(f, ins);
		case opcode::op_index_compound_fused: return exec_index_compound_fused(f, ins);
		case opcode::op_math: return exec_math(f, ins);
		case opcode::op_parallel_for: return exec_parallel_for(f, ins);
		case opcode::op_binary_fused_decl: return exec_binary_fused_decl(f, ins);
		case opcode::op_binary_fused_store: return exec_binary_fused_store(f, ins);
		case opcode::op_index_fused: return exec_index_fused(f, ins);
		case opcode::op_index_store_fused: return exec_index_store_fused(f, ins);
		case opcode::op_index_fused_decl: return exec_index_fused_decl(f, ins);
		case opcode::op_cast: return exec_cast(f, ins);
		default: return {};
	}
}

// Failed ops dispatch to an enclosing script catch handler in this frame (which
// resets f.ip) before propagating; `continue` re-enters the loop at the handler.
// Same single-temp shape as JAISCRIPT_TRY so run()'s Debug frame stays flat.
#define VM_TRY_OP(expr) \
	{ if (vm_check((expr)) == op_status::failed) [[unlikely]] { \
	      if (!handle_op_error(fp, records_base)) return op_status::failed; \
	      continue; } }

// Shared-slot variant for cases added after the frame-size ceiling was reached:
// reuses one function-scope temp so run()'s Debug frame stays flat (see the
// grouped exec_extended dispatch note below).
#define VM_TRY_OP_SHARED(expr) VM_TRY_OP(expr)

// Thin exception boundary: C++ exceptions thrown while in-loop frames are live get
// converted to script unwinding AT the failing logical frame (the native path converts
// them in exec_call at each recursion level); with no in-loop frames they rethrow
// byte-identically. Kept separate so its EH funclets never touch run_dispatch's frame.
checked_result<void> vm_backend::run(frame& entry) {
	frame* fp = &entry;
	const size_t records_base = call_records_top_;
#ifdef JAISCRIPT_VM_PROFILE
	// host-entered frames (execute, execute_callable, a fiber's body) never go through the
	// in-loop push and pop, so their inclusive span is taken here
	struct entry_inclusive {
		vm_backend* vm; const chunk* code; uint64_t t0;
		~entry_inclusive() {
			if (vm->parallel_worker_ || !code) return;
			auto& st = vm->profile_stats_for(*code);
			st.inclusive_cycles += __rdtsc() - t0;
			++st.entries;
		}
	} entry_span{this, entry.code, __rdtsc()};
#endif
	for (;;) {
		try {
			if (run_dispatch(fp, records_base) == op_status::failed) {
				return checked_result<void>(pending_error_);
			}
			return {};
		} catch (const script_exception& e) {
			if (ip_in_call_arg_zone(*fp)) {
				convert_cpp_exception_in_frame(e);
			} else if (call_records_top_ == records_base) {
				throw;
			} else {
				convert_cpp_exception_at_frame(fp, e);
			}
			if (!handle_throw_unwind(fp, records_base)) { return {}; }
		} catch (const std::exception& e) {
			if (ip_in_call_arg_zone(*fp)) {
				convert_cpp_exception_in_frame(script_exception(e.what()));
			} else if (call_records_top_ == records_base) {
				throw;
			} else {
				convert_cpp_exception_at_frame(fp, script_exception(e.what()));
			}
			if (!handle_throw_unwind(fp, records_base)) { return {}; }
		} catch (...) {
			pop_records_to(records_base, fp);
			throw;
		}
	}
}

#ifdef JAISCRIPT_VM_PROFILE
// Diagnostic builds only (define JAISCRIPT_VM_PROFILE in this TU): sorted opcode
// self-time histogram on backend teardown. Percentages are of total dispatched cycles.
#ifdef JAISCRIPT_VM_PROFILE
namespace {
	enum : uint8_t { census_use_typed = 1, census_use_read = 2, census_use_write = 4, census_use_ref = 8, census_use_capture = 16, census_use_unproven = 32 };
	enum : uint8_t { census_decl_none = 0, census_decl_scalar = 1, census_decl_other = 2 };

	bool census_scalar_type(const type_info* t) {
		if (!t) { return false; }
		switch (t->base_type) {
		case script_value_type::jai_int_type: case script_value_type::jai_float_type:
		case script_value_type::jai_bool_type: case script_value_type::jai_char_type: return true;
		default: return false;
		}
	}

	vm_backend::slot_census census_chunk(const chunk& c, const string_symbolizer* symbolizer) {
		vm_backend::slot_census r;
		const size_t n = c.local_count;
		r.use.assign(n, 0);
		r.decl_kind.assign(n, census_decl_none);
		r.decl_type.assign(n, std::string());
		r.first_general_ip.assign(n, UINT32_MAX);
		r.first_reject_ip.assign(n, UINT32_MAX);
		r.slot_names.assign(n, std::string());
		r.read_slots_at_ip.resize(c.code.size());
		r.write_slots_at_ip.resize(c.code.size());
		r.typed_slots_at_ip.resize(c.code.size());
		r.code = c.code;
		auto name_slot = [&](uint32_t slot, std::string_view name) {
			if (slot != k_invalid_u32 && slot < n && r.slot_names[slot].empty() && !name.empty()) { r.slot_names[slot] = std::string(name); }
		};
		auto sym_name = [&](uint32_t sym_idx) -> std::string_view {
			return symbolizer && sym_idx < c.symbols.size() ? symbolizer->get_string(c.symbols[sym_idx]) : std::string_view();
		};
		auto declare = [&](uint32_t slot, const type_info* t, bool is_reference) {
			if (slot == k_invalid_u32 || slot >= n || r.decl_kind[slot] != census_decl_none) { return; }
			if (is_reference) { r.decl_kind[slot] = census_decl_other; r.decl_type[slot] = "reference"; return; }
			if (census_scalar_type(t)) { r.decl_kind[slot] = census_decl_scalar; r.decl_type[slot] = std::string(t->canonical_name()); return; }
			r.decl_kind[slot] = census_decl_other;
			r.decl_type[slot] = t ? std::string(t->canonical_name()) : std::string("var/auto");
		};
		auto mark = [&](uint32_t slot, uint8_t bits, uint32_t ip) {
			if (slot == k_invalid_u32 || slot >= n) { return; }
			r.use[slot] |= bits;
			if (bits & (census_use_ref | census_use_capture | census_use_unproven)) {
				if (r.first_reject_ip[slot] == UINT32_MAX && ip != UINT32_MAX) { r.first_reject_ip[slot] = ip; }
			}
			if (ip == UINT32_MAX) { return; }
			if (bits & census_use_read) { r.read_slots_at_ip[ip].push_back(slot); }
			if (bits & census_use_write) { r.write_slots_at_ip[ip].push_back(slot); }
			if ((bits & (census_use_read | census_use_write)) && r.first_general_ip[slot] == UINT32_MAX) { r.first_general_ip[slot] = ip; }
			if (bits & census_use_typed) { r.typed_slots_at_ip[ip].push_back(slot); }
		};
		auto operand = [&](const fused_operand& o, uint32_t ip) {
			if (o.slot == k_invalid_u32) { return; }
			mark(o.slot, census_use_read, ip);
			if (o.symbol != k_invalid_u32) { name_slot(o.slot, sym_name(o.symbol)); }
		};
		auto typed_slot = [&](uint32_t v, uint32_t ip) { if (!(v & k_typed_const_bit)) { mark(v & k_typed_slot_mask, census_use_typed, ip); } };
		auto decl_of = [&](uint32_t node) -> uint32_t {
			if (node >= c.nodes.size() || !c.nodes[node] || c.nodes[node]->get_type() != node_type::variable_decl) { return k_invalid_u32; }
			const auto* vd = static_cast<const variable_decl*>(c.nodes[node].get());
			if (vd->slot_index == SIZE_MAX) { return k_invalid_u32; }
			const uint32_t slot = static_cast<uint32_t>(vd->slot_index);
			name_slot(slot, vd->name);
			declare(slot, vd->type.get(), false);   // reference declarations are their own opcodes (marked at the op)
			if (vd->ref_escaping) { mark(slot, census_use_ref, UINT32_MAX); }   // boxed into a cell from birth
			return slot;
		};
		for (const auto& pp : c.profile_params) { declare(pp.slot, pp.type, pp.is_reference); if (pp.is_reference) { mark(pp.slot, census_use_ref, UINT32_MAX); } }
		auto binary_proto = [&](uint32_t idx, uint32_t ip) {
			if (idx < c.fused_binary_protos.size()) { operand(c.fused_binary_protos[idx].left, ip); operand(c.fused_binary_protos[idx].right, ip); }
		};
		auto typed_operand = [&](const fused_operand& o, uint32_t ip) {
			if (o.slot == k_invalid_u32) { return; }
			mark(o.slot, census_use_typed, ip);
			if (o.symbol != k_invalid_u32) { name_slot(o.slot, sym_name(o.symbol)); }
		};
		auto typed_binary_proto = [&](uint32_t idx, uint32_t ip) {
			if (idx < c.fused_binary_protos.size()) { typed_operand(c.fused_binary_protos[idx].left, ip); typed_operand(c.fused_binary_protos[idx].right, ip); }
		};
		// a store lane reads its index and value operands as raw payloads (typed traffic); the
		// container is a handle read; a general index op reads everything as values
		auto index_proto = [&](uint32_t idx, uint32_t ip, bool raw_lane) {
			if (idx >= c.fused_index_protos.size()) { return; }
			const auto& p = c.fused_index_protos[idx];
			operand(p.container, ip);
			if (raw_lane) { typed_operand(p.index, ip); typed_operand(p.store_value, ip); typed_binary_proto(p.index_binary, ip); }
			else { operand(p.index, ip); operand(p.store_value, ip); binary_proto(p.index_binary, ip); }
			mark(p.decl_slot, census_use_write, ip);
		};
		for (uint32_t ip = 0; ip < c.code.size(); ++ip) {
			const vm_instruction& ins = c.code[ip];
			const opcode op = ins.op;
			if (op >= opcode::op_t_add_ii && op <= opcode::op_t_mod_ff) {
				mark(ins.a & k_typed_slot_mask, census_use_typed, ip); typed_slot(ins.b, ip); typed_slot(ins.c, ip);
				if ((ins.a >> k_typed_dst_shift) == k_typed_dst_decl && ip < c.typed_sites.size()) {
					const vm_instruction& orig = c.typed_sites[ip].orig;
					if (orig.op == opcode::op_binary_fused_decl && orig.a < c.fused_binary_dst_protos.size()) { decl_of(c.fused_binary_dst_protos[orig.a].node_index); }
				}
				continue;
			}
			if (op >= opcode::op_t_jlt_ii && op <= opcode::op_t_jne_ff) { typed_slot(ins.a, ip); typed_slot(ins.b, ip); continue; }
			switch (op) {
			case opcode::op_t_decl_mov: mark(ins.a, census_use_typed, ip); mark(ins.b, census_use_typed, ip); decl_of(ins.c); break;
			case opcode::op_t_decl_const: mark(ins.a, census_use_typed, ip); decl_of(ins.c); break;
			case opcode::op_t_store_mov:
			case opcode::op_t_store_const: {
				mark(ins.a, census_use_typed, ip);
				if (op == opcode::op_t_store_mov) { mark(ins.b, census_use_typed, ip); }
				if (ip < c.typed_sites.size()) {
					const auto& site = c.typed_sites[ip];
					name_slot(ins.a, sym_name(site.orig2.a));
					if (op == opcode::op_t_store_mov && site.orig.a != k_invalid_u32) { name_slot(site.orig.a, sym_name(site.orig.b)); }
				}
				break;
			}
			case opcode::op_t_push: mark(ins.a, census_use_read, ip); typed_slot(ins.b, ip); break;
			case opcode::op_t_load: {
				mark(ins.a, census_use_typed, ip);
				if (ip < c.typed_sites.size()) {
					const auto& site = c.typed_sites[ip];
					mark(site.cont_slot, census_use_read, ip);
					for (uint8_t k = 0; k <= site.fold_count && k < 4; ++k) { typed_slot(site.operands[k], ip); }
					decl_of(site.orig.c);
					if (site.orig.a < c.fused_index_protos.size()) { operand(c.fused_index_protos[site.orig.a].container, UINT32_MAX); }
				}
				break;
			}
			case opcode::op_load: mark(ins.a, census_use_read, ip); name_slot(ins.a, sym_name(ins.b)); break;
			case opcode::op_return_ident: mark(ins.a, census_use_read, ip); name_slot(ins.a, sym_name(ins.b)); break;
			case opcode::op_ref_return_bind: mark(ins.a, census_use_read | census_use_ref, ip); name_slot(ins.a, sym_name(ins.b)); break;
			case opcode::op_store: mark(ins.b, census_use_write, ip); name_slot(ins.b, sym_name(ins.a)); break;
			case opcode::op_compound_store:
			case opcode::op_incdec: mark(ins.b, census_use_read | census_use_write, ip); name_slot(ins.b, sym_name(ins.a)); break;
			case opcode::op_decl_var: mark(decl_of(ins.a), census_use_write, ip); break;
			case opcode::op_decl_ref_ident:
			case opcode::op_decl_ref_value: mark(decl_of(ins.a), census_use_write | census_use_ref, ip); break;
			case opcode::op_index_fused_decl: mark(decl_of(ins.c), census_use_write, ip); index_proto(ins.a, ip, false); break;
			case opcode::op_index_fused: index_proto(ins.a, ip, false); break;
			case opcode::op_index_store_fused: index_proto(ins.a, ip, ins.a < c.fused_index_protos.size() && c.fused_index_protos[ins.a].typed_lane); break;
			case opcode::op_binary_fused: binary_proto(ins.a, ip); break;
			case opcode::op_binary_fused_temp: {
				binary_proto(ins.a, ip);
				// the general kernel lands in the temp: a proven class is a boundary definition, no proof is a rejection
				const bool proven = ins.a < c.fused_binary_protos.size() && c.fused_binary_protos[ins.a].typed_class != 0;
				mark(ins.b, static_cast<uint8_t>(census_use_write | (proven ? 0 : census_use_unproven)), ip);
				break;
			}
			case opcode::op_fused_cmp_jump: binary_proto(ins.b, ip); break;
			case opcode::op_compound_fused: {
				if (ins.a < c.compound_fused_protos.size()) {
					const auto& cp = c.compound_fused_protos[ins.a];
					mark(cp.slot, census_use_read | census_use_write, ip); name_slot(cp.slot, sym_name(cp.symbol));
					operand(cp.rhs, ip);
					if (cp.rhs_proto != k_invalid_u32) { binary_proto(cp.rhs_proto, ip); }
				}
				break;
			}
			case opcode::op_binary_fused_decl:
			case opcode::op_binary_fused_store: {
				if (ins.a < c.fused_binary_dst_protos.size()) {
					const auto& dp = c.fused_binary_dst_protos[ins.a];
					mark(dp.slot, census_use_write, ip);
					mark(decl_of(dp.node_index), census_use_write, ip);
					binary_proto(dp.binary_proto, ip);
				}
				break;
			}
			case opcode::op_push_fused: {
				if (ins.a < c.push_protos.size()) { operand(c.push_protos[ins.a].receiver, ip); operand(c.push_protos[ins.a].value, ip); }
				break;
			}
			case opcode::op_call:
			case opcode::op_call_method:
			case opcode::op_call_from_scratch:
				r.call_ips.push_back(ip);
				if (ins.b < c.call_sites.size()) {
					const auto& cs = c.call_sites[ins.b];
					operand(cs.callee, ip);
					mark(cs.receiver_slot, census_use_read, ip); mark(cs.chain_root_slot, census_use_read, ip);
				}
				break;
			case opcode::op_closure:
				if (ins.a < c.closure_protos.size()) {
					for (const auto& [sym, slot] : c.closure_protos[ins.a].outer_slot_plan) {
						if (slot != SIZE_MAX) { mark(static_cast<uint32_t>(slot), census_use_read | census_use_capture, ip); if (symbolizer) { name_slot(static_cast<uint32_t>(slot), symbolizer->get_string(sym)); } }
					}
				}
				break;
			case opcode::op_iter_init:
			case opcode::op_iter_next:
			case opcode::op_iter_pop:
				if (ins.a < c.iter_protos.size() && c.iter_protos[ins.a].slot != SIZE_MAX) {
					const auto& it = c.iter_protos[ins.a];
					const uint32_t slot = static_cast<uint32_t>(it.slot);
					if (symbolizer && it.var_symbol != UINT64_MAX) { name_slot(slot, symbolizer->get_string(it.var_symbol)); }
					if (slot < n && r.decl_kind[slot] == census_decl_none) { r.decl_kind[slot] = census_decl_other; r.decl_type[slot] = it.is_reference ? "range-for binding (auto&)" : "range-for binding"; }
					if (op == opcode::op_iter_next) { mark(slot, static_cast<uint8_t>(census_use_write | (it.is_reference ? census_use_ref : 0)), ip); }
				}
				break;
			case opcode::op_try_push:
				if (ins.c != k_invalid_u32 && ins.c < n) {
					name_slot(ins.c, sym_name(ins.b));
					if (r.decl_kind[ins.c] == census_decl_none) { r.decl_kind[ins.c] = census_decl_other; r.decl_type[ins.c] = "catch variable"; }
					mark(ins.c, census_use_write, ip);
				}
				break;
			case opcode::op_loop_back: r.loops.emplace_back(ins.a, ip); break;
			case opcode::op_yield: r.has_yield = true; break;
			default: ++r.opaque_ops; break;
			}
		}
		for (const auto& p : c.counted_for_protos) { operand(p.var, UINT32_MAX); operand(p.end, UINT32_MAX); operand(p.step, UINT32_MAX); }
		return r;
	}

	// candidate = a proven-scalar declaration, or a temp with only proven definitions, not escaped
	bool census_candidate(const vm_backend::slot_census& cs, size_t slot) {
		const uint8_t u = cs.use[slot];
		if (u & (census_use_ref | census_use_capture | census_use_unproven)) { return false; }
		if (cs.decl_kind[slot] == census_decl_other) { return false; }
		if (cs.decl_kind[slot] == census_decl_none && !(u & census_use_typed)) { return false; }   // an undeclared binding, not a compiler temp
		return u != 0;
	}
	const char* census_reject_reason(const vm_backend::slot_census& cs, size_t slot) {
		const uint8_t u = cs.use[slot];
		if (u & census_use_ref) { return "address taken"; }
		if (u & census_use_capture) { return "captured by a closure"; }
		if (cs.decl_kind[slot] == census_decl_other) { return "declared"; }
		if (u & census_use_unproven) { return "unproven kernel definition"; }
		if (cs.decl_kind[slot] == census_decl_none) { return "undeclared binding"; }
		return "";
	}
}
#endif

vm_backend::profile_chunk_stats& vm_backend::profile_stats_for(const chunk& code) {
	auto& st = profile_chunks_[&code];
	// a recycled chunk address with a different body starts a fresh row
	if (st.lines.size() != code.code.size() || st.name != code.function_name) {
		st = profile_chunk_stats{};
		st.name = code.function_name;
		st.census = census_chunk(code, symbolizer_);
		st.lines.assign(code.code.size(), 0);
		for (size_t ip = 0; ip < code.code.size(); ++ip) {
			const ast_node* stmt = ip < code.stmt_nodes.size() ? code.stmt_nodes[ip] : nullptr;
			if (stmt) {
				st.lines[ip] = static_cast<uint32_t>(stmt->location.line);
				if (st.file.empty()) { st.file = stmt->location.filename; }
			}
		}
		st.ip_cycles.assign(code.code.size(), 0);
		st.ip_counts.assign(code.code.size(), 0);
	}
	return st;
}

void vm_backend::profile_reset() {
	// Every table, the opcode rows and the auxiliary taxonomies alike: a --profile-reset-tick
	// capture reports the measured span only (bootstrap and warmup work differ, no divisor recovers a rate)
	std::fill(std::begin(profile_cycles_), std::end(profile_cycles_), 0ull);
	std::fill(std::begin(profile_counts_), std::end(profile_counts_), 0ull);
	std::fill(std::begin(profile_decl_paths_), std::end(profile_decl_paths_), 0ull);
	std::fill(std::begin(profile_store_paths_), std::end(profile_store_paths_), 0ull);
	std::fill(std::begin(profile_store_path_cycles_), std::end(profile_store_path_cycles_), 0ull);
	std::fill(std::begin(profile_store_path_counts_), std::end(profile_store_path_counts_), 0ull);
	std::fill(std::begin(profile_env_births_), std::end(profile_env_births_), 0ull);
	std::fill(std::begin(profile_scope_kinds_), std::end(profile_scope_kinds_), 0ull);
	std::fill(std::begin(profile_call_method_paths_), std::end(profile_call_method_paths_), 0ull);
	std::fill(std::begin(profile_get_member_paths_), std::end(profile_get_member_paths_), 0ull);
	std::fill(std::begin(profile_bound_method_paths_), std::end(profile_bound_method_paths_), 0ull);
	std::fill(std::begin(profile_env_resolve_), std::end(profile_env_resolve_), 0ull);
	std::fill(std::begin(profile_cfs_cycles_), std::end(profile_cfs_cycles_), 0ull);
	std::fill(std::begin(profile_cfs_counts_), std::end(profile_cfs_counts_), 0ull);
	std::fill(std::begin(profile_call_push_cyc_), std::end(profile_call_push_cyc_), 0ull);
	std::fill(std::begin(profile_probe_cyc_), std::end(profile_probe_cyc_), 0ull);
	std::fill(std::begin(profile_call_ret_cyc_), std::end(profile_call_ret_cyc_), 0ull);
	std::fill(std::begin(profile_mpush_cyc_), std::end(profile_mpush_cyc_), 0ull);
	std::fill(std::begin(profile_mpush_sub_), std::end(profile_mpush_sub_), 0ull);
	std::fill(std::begin(profile_pop_sub_), std::end(profile_pop_sub_), 0ull);
	std::fill(std::begin(profile_slice_gate_), std::end(profile_slice_gate_), 0ull);
	std::fill(std::begin(profile_push_sections_), std::end(profile_push_sections_), 0ull);
	std::fill(std::begin(profile_typed_head_), std::end(profile_typed_head_), 0ull);
	std::fill(std::begin(profile_push_fused_lane_), std::end(profile_push_fused_lane_), 0ull);
	std::fill(std::begin(profile_typed_decl_read_), std::end(profile_typed_decl_read_), 0ull);
	std::fill(std::begin(profile_typed_store_), std::end(profile_typed_store_), 0ull);
	std::fill(std::begin(profile_typed_land_), std::end(profile_typed_land_), 0ull);
	std::fill(std::begin(profile_temp_miss_), std::end(profile_temp_miss_), 0ull);
	std::fill(std::begin(profile_typed_inplace_), std::end(profile_typed_inplace_), 0ull);
	std::fill(std::begin(profile_block_), std::end(profile_block_), 0ull);
	std::fill(std::begin(profile_cells_), std::end(profile_cells_), 0ull);
	std::fill(std::begin(profile_cells_refused_slot_), std::end(profile_cells_refused_slot_), 0ull);
	std::fill(std::begin(profile_cells_miss_step_), std::end(profile_cells_miss_step_), 0ull);
	profile_cells_area_bytes_max_ = 0; profile_cells_records_ = 0;
	std::fill(std::begin(profile_typed_cont_src_), std::end(profile_typed_cont_src_), 0ull);
	std::fill(std::begin(profile_load_kinds_), std::end(profile_load_kinds_), 0ull);
	std::fill(std::begin(profile_index_kinds_), std::end(profile_index_kinds_), 0ull);
	std::fill(std::begin(profile_bind_paths_), std::end(profile_bind_paths_), 0ull);
	std::fill(std::begin(profile_ctor_cyc_), std::end(profile_ctor_cyc_), 0ull);
	std::fill(std::begin(profile_binary_fused_next_), std::end(profile_binary_fused_next_), 0ull);
	std::fill(std::begin(profile_load_next_), std::end(profile_load_next_), 0ull);
	std::fill(std::begin(profile_index_fused_next_), std::end(profile_index_fused_next_), 0ull);
	std::fill(std::begin(profile_decl_var_prev_), std::end(profile_decl_var_prev_), 0ull);
	std::fill(std::begin(profile_store_prev_), std::end(profile_store_prev_), 0ull);
	std::fill(std::begin(profile_decl_var_kinds_), std::end(profile_decl_var_kinds_), 0ull);
	std::fill(std::begin(profile_index_read_paths_), std::end(profile_index_read_paths_), 0ull);
	std::fill(std::begin(profile_decl_sub_), std::end(profile_decl_sub_), 0ull);
	profile_call_push_count_ = 0;
	profile_probe_count_ = 0;
	profile_call_ret_count_ = 0;
	profile_cmp_operands_cyc_ = 0;
	profile_cmp_count_ = 0;
	profile_cmp_sptr_null_ = 0;
	profile_mpush_count_ = 0;
	profile_pop_sub_count_ = 0;
	profile_push_prologue_ = 0;
	profile_new_count_ = 0;
	profile_new_resolve_cyc_ = 0;
	profile_new_invoke_cyc_ = 0;
	profile_ctor_count_ = 0;
	profile_nested_cycles_ = 0;
	profile_native_callees_.clear();
	profile_store_name_cycles_.clear();
	profile_cfs_inloop_names_.clear();
	profile_cfs_opaque_cycles_.clear();
	profile_env_walk_names_.clear();
	profile_builtin_direct_names_.clear();
	profile_store_last_path_ = 7;
	for (auto& [code, st] : profile_chunks_) {
		std::fill(st.ip_cycles.begin(), st.ip_cycles.end(), 0ull);
		std::fill(st.ip_counts.begin(), st.ip_counts.end(), 0ull);
		st.inclusive_cycles = 0;
		st.entries = 0;
	}
}


void vm_backend::dump_slot_census() const {
	struct row { const profile_chunk_stats* st; uint64_t excl; };
	std::vector<row> rows;
	for (const auto& [code, st] : profile_chunks_) {
		uint64_t excl = 0;
		for (uint64_t cyc : st.ip_cycles) { excl += cyc; }
		if (excl && !st.census.use.empty()) { rows.push_back({&st, excl}); }
	}
	if (rows.empty()) { return; }
	std::sort(rows.begin(), rows.end(), [](const row& a, const row& b) { return a.excl > b.excl; });
	fprintf(stderr, "[vm-profile] slot census (measured span; top functions by exclusive cycles). candidate = declared proven scalar or proven temp, not escaped; boundary = a general op on a candidate (reads box, definitions unbox), executions; rejected = address taken / captured / declared non-scalar or dynamic / unproven kernel definition:\n");
	const size_t shown = rows.size() < 14 ? rows.size() : 14;
	for (size_t i = 0; i < shown; ++i) {
		const profile_chunk_stats& st = *rows[i].st;
		const slot_census& cs = st.census;
		const size_t n = cs.use.size();
		size_t cand_typed_only = 0, cand_boundary = 0, cand_no_typed = 0, rej_ref = 0, rej_capture = 0, rej_declared = 0, rej_unproven = 0, rej_undeclared = 0, unused = 0, auto_with_typed = 0;
		std::vector<uint64_t> bread(n, 0), bwrite(n, 0), rej_exec(n, 0);
		for (size_t ip = 0; ip < st.ip_counts.size() && ip < cs.read_slots_at_ip.size(); ++ip) {
			for (uint32_t slot : cs.read_slots_at_ip[ip]) { if (census_candidate(cs, slot)) { bread[slot] += st.ip_counts[ip]; } else { rej_exec[slot] += st.ip_counts[ip]; } }
			for (uint32_t slot : cs.write_slots_at_ip[ip]) { if (census_candidate(cs, slot)) { bwrite[slot] += st.ip_counts[ip]; } else { rej_exec[slot] += st.ip_counts[ip]; } }
		}
		uint64_t bread_total = 0, bwrite_total = 0;
		for (size_t slot = 0; slot < n; ++slot) {
			const uint8_t u = cs.use[slot];
			if (!u) { ++unused; continue; }
			if (census_candidate(cs, slot)) {
				const bool general = (u & (census_use_read | census_use_write)) != 0;
				const bool typed = (u & census_use_typed) != 0;
				if (typed && !general) { ++cand_typed_only; } else if (typed) { ++cand_boundary; } else { ++cand_no_typed; }
				bread_total += bread[slot]; bwrite_total += bwrite[slot];
			} else {
				if (u & census_use_ref) { ++rej_ref; } else if (u & census_use_capture) { ++rej_capture; }
				else if (cs.decl_kind[slot] == census_decl_other) { ++rej_declared; if ((u & census_use_typed) && cs.decl_type[slot] == "var/auto") { ++auto_with_typed; } }
				else if (u & census_use_unproven) { ++rej_unproven; }
				else { ++rej_undeclared; }
			}
		}
		fprintf(stderr, "  %-26s slots %zu: candidates typed-only %zu | with boundaries %zu | no typed use %zu; rejected: address %zu | capture %zu | declared %zu (var/auto with typed uses %zu) | unproven %zu | undeclared %zu; unused %zu; opaque ops %u; yield %s; boundary executions: reads %llu writes %llu\n",
			st.name.empty() ? "<top level>" : st.name.c_str(), n, cand_typed_only, cand_boundary, cand_no_typed, rej_ref, rej_capture, rej_declared, auto_with_typed, rej_unproven, rej_undeclared, unused, cs.opaque_ops, cs.has_yield ? "yes" : "no",
			(unsigned long long)bread_total, (unsigned long long)bwrite_total);
		std::vector<size_t> order;
		for (size_t slot = 0; slot < n; ++slot) { if (bread[slot] + bwrite[slot] + rej_exec[slot]) { order.push_back(slot); } }
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bread[a] + bwrite[a] + rej_exec[a] > bread[b] + bwrite[b] + rej_exec[b]; });
		for (size_t k = 0; k < order.size() && k < 8; ++k) {
			const size_t slot = order[k];
			const std::string nm = cs.slot_names[slot].empty() ? "slot" + std::to_string(slot) : cs.slot_names[slot];
			if (census_candidate(cs, slot)) {
				const uint32_t fip = cs.first_general_ip[slot];
				fprintf(stderr, "      boundary %-14s %-8s reads %llu writes %llu   first %s at L%u\n", nm.c_str(), cs.decl_type[slot].empty() ? "temp" : cs.decl_type[slot].c_str(),
					(unsigned long long)bread[slot], (unsigned long long)bwrite[slot],
					fip < cs.code.size() ? std::string(opcode_name(cs.code[fip].op)).c_str() : "-", fip < st.lines.size() ? st.lines[fip] : 0u);
			} else {
				const uint32_t fip = cs.first_reject_ip[slot] != UINT32_MAX ? cs.first_reject_ip[slot] : cs.first_general_ip[slot];
				fprintf(stderr, "      rejected %-14s %s %s   uses %llu   first %s at L%u\n", nm.c_str(), census_reject_reason(cs, slot), cs.decl_type[slot].c_str(),
					(unsigned long long)rej_exec[slot],
					fip < cs.code.size() ? std::string(opcode_name(cs.code[fip].op)).c_str() : "-", fip < st.lines.size() ? st.lines[fip] : 0u);
			}
		}
		for (const auto& [target, back] : cs.loops) {
			if (back >= st.ip_counts.size() || target > back) { continue; }
			const uint64_t iterations = st.ip_counts[back];
			if (!iterations) { continue; }
			uint64_t typed_exec = 0, boundary_exec = 0, rejected_exec = 0, call_exec = 0, other_exec = 0;
			std::vector<uint8_t> touched(n, 0);
			for (uint32_t ip = target; ip <= back; ++ip) {
				const uint64_t cnt = st.ip_counts[ip];
				bool typed = !cs.typed_slots_at_ip[ip].empty(), boundary = false, rejected = false;
				for (uint32_t slot : cs.read_slots_at_ip[ip]) { touched[slot] = 1; (census_candidate(cs, slot) ? boundary : rejected) = true; }
				for (uint32_t slot : cs.write_slots_at_ip[ip]) { touched[slot] = 1; (census_candidate(cs, slot) ? boundary : rejected) = true; }
				for (uint32_t slot : cs.typed_slots_at_ip[ip]) { touched[slot] = 1; }
				if (typed) { typed_exec += cnt; }
				if (boundary) { boundary_exec += cnt; }
				if (rejected) { rejected_exec += cnt; }
				if (!typed && !boundary && !rejected) { other_exec += cnt; }
			}
			for (uint32_t cip : cs.call_ips) { if (cip >= target && cip <= back) { call_exec += st.ip_counts[cip]; } }
			std::string bnames, rnames;
			for (size_t slot = 0; slot < n; ++slot) {
				if (!touched[slot]) { continue; }
				const std::string nm = cs.slot_names[slot].empty() ? "slot" + std::to_string(slot) : cs.slot_names[slot];
				if (census_candidate(cs, slot)) { if (cs.use[slot] & (census_use_read | census_use_write)) { if (!bnames.empty()) { bnames += ","; } bnames += nm; } }
				else { if (!rnames.empty()) { rnames += ","; } rnames += nm; }
			}
			fprintf(stderr, "      loop L%u..L%u [ip %u..%u] iterations %llu: per iteration typed %.2f | boundary %.2f | rejected-slot %.2f | calls %.2f | other %.2f; boundary slots {%s} rejected slots {%s}\n",
				target < st.lines.size() ? st.lines[target] : 0u, st.lines[back], target, back, (unsigned long long)iterations,
				(double)typed_exec / (double)iterations, (double)boundary_exec / (double)iterations, (double)rejected_exec / (double)iterations,
				(double)call_exec / (double)iterations, (double)other_exec / (double)iterations, bnames.empty() ? "none" : bnames.c_str(), rnames.empty() ? "none" : rnames.c_str());
			if (back - target < 48) {
				std::string listing;
				for (uint32_t ip = target; ip <= back && ip < cs.code.size(); ++ip) {
					const vm_instruction& li = cs.code[ip];
					char buf[96];
					snprintf(buf, sizeof(buf), "%s%u:%s(%u,%u,%u)x%llu", listing.empty() ? "" : " ", ip, std::string(opcode_name(li.op)).c_str(), li.a, li.b, li.c, (unsigned long long)st.ip_counts[ip]);
					listing += buf;
				}
				fprintf(stderr, "        %s\n", listing.c_str());
			}
		}
	}
}

void vm_backend::dump_opcode_profile() const {
	{
		// Script functions by exclusive opcode self time, then the hot lines of the top rows
		struct row { const profile_chunk_stats* st; uint64_t excl; uint64_t ops; };
		std::vector<row> rows;
		uint64_t total = 0;
		for (const auto& [code, st] : profile_chunks_) {
			uint64_t excl = 0, ops = 0;
			for (size_t i = 0; i < st.ip_cycles.size(); ++i) { excl += st.ip_cycles[i]; ops += st.ip_counts[i]; }
			if (excl) { rows.push_back({&st, excl, ops}); total += excl; }
		}
		if (!rows.empty()) {
			std::sort(rows.begin(), rows.end(), [](const row& a, const row& b) { return a.excl > b.excl; });
			fprintf(stderr, "\n[vm-profile] script functions (main engine; exclusive = opcode self time in rdtsc cycles, callee bodies excluded, call/return machinery included; inclusive = frame entry to exit, recursion counts inner activations again; total exclusive %llu Mcycles)\n",
				(unsigned long long)(total / 1000000));
			fprintf(stderr, "%-32s %-28s %12s %7s %12s %12s %10s\n", "function", "file", "excl Mcyc", "pct", "ops", "incl Mcyc", "entries");
			size_t shown = 0;
			for (const row& r : rows) {
				std::string file = r.st->file;
				if (const size_t cut = file.find_last_of("/\\"); cut != std::string::npos) { file = file.substr(cut + 1); }
				fprintf(stderr, "%-32.32s %-28.28s %12.1f %6.2f%% %12llu %12.1f %10llu\n",
					r.st->name.empty() ? "<top level>" : r.st->name.c_str(), file.c_str(),
					r.excl / 1e6, 100.0 * r.excl / total, (unsigned long long)r.ops,
					r.st->inclusive_cycles / 1e6, (unsigned long long)r.st->entries);
				if (++shown >= 30) break;
			}
			fprintf(stderr, "[vm-profile] hot lines of the top functions (exclusive Mcyc, ops):\n");
			shown = 0;
			for (const row& r : rows) {
				std::map<uint32_t, std::pair<uint64_t, uint64_t>> by_line;
				for (size_t i = 0; i < r.st->ip_cycles.size(); ++i) {
					if (!r.st->ip_cycles[i]) continue;
					auto& e = by_line[r.st->lines[i]];
					e.first += r.st->ip_cycles[i];
					e.second += r.st->ip_counts[i];
				}
				std::vector<std::pair<uint32_t, std::pair<uint64_t, uint64_t>>> lines(by_line.begin(), by_line.end());
				std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
				fprintf(stderr, "  %s\n", r.st->name.empty() ? "<top level>" : r.st->name.c_str());
				size_t ln = 0;
				for (const auto& [line, e] : lines) {
					fprintf(stderr, "    line %-6u %10.1f %12llu\n", line, e.first / 1e6, (unsigned long long)e.second);
					if (++ln >= 6) break;
				}
				if (++shown >= 12) break;
			}
		}
	}
	fprintf(stderr, "[vm-profile] scope kinds: iter %llu | scope_push %llu | call_closure %llu | call_plain %llu\n", (unsigned long long)profile_scope_kinds_[0], (unsigned long long)profile_scope_kinds_[1], (unsigned long long)profile_scope_kinds_[2], (unsigned long long)profile_scope_kinds_[3]);
	fprintf(stderr, "[vm-profile] env births: scope %llu | method %llu | static %llu | coroutine %llu | capture %llu\n", (unsigned long long)profile_env_births_[0], (unsigned long long)profile_env_births_[1], (unsigned long long)profile_env_births_[2], (unsigned long long)profile_env_births_[3], (unsigned long long)profile_env_births_[4]);
	const uint64_t cm_total = profile_call_method_paths_[0] + profile_call_method_paths_[1] + profile_call_method_paths_[2] +
	                          profile_call_method_paths_[3] + profile_call_method_paths_[4] + profile_call_method_paths_[5] +
	                          profile_call_method_paths_[6];
	if (cm_total) {
		fprintf(stderr, "[vm-profile] CALL_METHOD paths: mic-static %llu | mic-resolve %llu | ladder-fill %llu | string-builtin %llu | worker-pin %llu | native %llu | builtin-direct %llu\n",
			(unsigned long long)profile_call_method_paths_[0], (unsigned long long)profile_call_method_paths_[1],
			(unsigned long long)profile_call_method_paths_[2], (unsigned long long)profile_call_method_paths_[3],
			(unsigned long long)profile_call_method_paths_[4], (unsigned long long)profile_call_method_paths_[5],
			(unsigned long long)profile_call_method_paths_[6]);
	}
	const uint64_t gm_total = profile_get_member_paths_[0] + profile_get_member_paths_[1] + profile_get_member_paths_[2];
	if (gm_total) {
		fprintf(stderr, "[vm-profile] GET_MEMBER paths: ic-hit %llu | negative/absent %llu | non-instance %llu\n",
			(unsigned long long)profile_get_member_paths_[0], (unsigned long long)profile_get_member_paths_[1],
			(unsigned long long)profile_get_member_paths_[2]);
	}
	const uint64_t bm_total = profile_bound_method_paths_[0] + profile_bound_method_paths_[2] + profile_bound_method_paths_[3];
	if (bm_total) {
		fprintf(stderr, "[vm-profile] exec_call bound-method: entered %llu | in-loop %llu | bound-opaque %llu | other-opaque %llu\n",
			(unsigned long long)profile_bound_method_paths_[0], (unsigned long long)profile_bound_method_paths_[1],
			(unsigned long long)profile_bound_method_paths_[2], (unsigned long long)profile_bound_method_paths_[3]);
	}
	const uint64_t er_total = profile_env_resolve_[0] + profile_env_resolve_[1] + profile_env_resolve_[2] +
	                          profile_env_resolve_[3] + profile_env_resolve_[4] + profile_env_resolve_[6];
	if (er_total) {
		fprintf(stderr, "[vm-profile] ENV resolve: no-slot %llu | frame-ineligible %llu | cache-hit %llu | fill %llu | fill-uncacheable %llu | full-walks %llu | fast-hit %llu | fast-arm %llu | this-field-hit %llu | this-field-arm %llu\n",
			(unsigned long long)profile_env_resolve_[0], (unsigned long long)profile_env_resolve_[1],
			(unsigned long long)profile_env_resolve_[2], (unsigned long long)profile_env_resolve_[3],
			(unsigned long long)profile_env_resolve_[4], (unsigned long long)profile_env_resolve_[5],
			(unsigned long long)profile_env_resolve_[6], (unsigned long long)profile_env_resolve_[7],
			(unsigned long long)profile_env_resolve_[8], (unsigned long long)profile_env_resolve_[9]);
	}
	if (!profile_env_walk_names_.empty()) {
		std::vector<std::pair<std::string, uint64_t>> walks(profile_env_walk_names_.begin(), profile_env_walk_names_.end());
		std::sort(walks.begin(), walks.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		fprintf(stderr, "[vm-profile] ENV full-walk symbols:\n");
		size_t shown = 0;
		for (const auto& [name, count] : walks) {
			fprintf(stderr, "  %-32s %10llu\n", name.c_str(), (unsigned long long)count);
			if (++shown >= 15) break;
		}
	}
	auto dump_next_histogram = [](const char* label, const uint64_t (&hist)[256]) {
		std::vector<std::pair<int, uint64_t>> rows;
		for (int i = 0; i < 256; ++i) if (hist[i]) rows.push_back({i, hist[i]});
		if (rows.empty()) return;
		std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		fprintf(stderr, "[vm-profile] %s result consumers (next opcode):\n", label);
		size_t shown = 0;
		for (const auto& [op, count] : rows) {
			fprintf(stderr, "  %-24.*s %10llu\n",
				(int)opcode_name(static_cast<opcode>(op)).size(), opcode_name(static_cast<opcode>(op)).data(),
				(unsigned long long)count);
			if (++shown >= 12) break;
		}
	};
	dump_next_histogram("BINARY_FUSED", profile_binary_fused_next_);
	dump_slot_census();
	dump_next_histogram("LOAD", profile_load_next_);
	dump_next_histogram("INDEX_FUSED", profile_index_fused_next_);
	dump_next_histogram("DECL_VAR producers (previous opcode, not consumers)", profile_decl_var_prev_);
	dump_next_histogram("STORE producers (previous opcode, not consumers)", profile_store_prev_);
	fprintf(stderr, "[vm-profile] DECL_VAR initializer kinds: null %llu int %llu float %llu string %llu char %llu bool %llu array %llu map %llu object %llu function %llu reference %llu shared %llu weak %llu other %llu\n",
		(unsigned long long)profile_decl_var_kinds_[0], (unsigned long long)profile_decl_var_kinds_[1], (unsigned long long)profile_decl_var_kinds_[2], (unsigned long long)profile_decl_var_kinds_[3],
		(unsigned long long)profile_decl_var_kinds_[4], (unsigned long long)profile_decl_var_kinds_[5], (unsigned long long)profile_decl_var_kinds_[6], (unsigned long long)profile_decl_var_kinds_[7],
		(unsigned long long)profile_decl_var_kinds_[8], (unsigned long long)profile_decl_var_kinds_[9], (unsigned long long)profile_decl_var_kinds_[10], (unsigned long long)profile_decl_var_kinds_[11],
		(unsigned long long)profile_decl_var_kinds_[12], (unsigned long long)profile_decl_var_kinds_[13] + profile_decl_var_kinds_[14] + profile_decl_var_kinds_[15]);
	fprintf(stderr, "[vm-profile] typed decl read: tried %llu hit %llu | typed store: tried %llu hit %llu\n",
		(unsigned long long)profile_typed_decl_read_[0], (unsigned long long)profile_typed_decl_read_[1],
		(unsigned long long)profile_typed_store_[0], (unsigned long long)profile_typed_store_[1]);
	fprintf(stderr, "[vm-profile] push_fused lanes: append %llu | kernel %llu | replay %llu\n",
		(unsigned long long)profile_push_fused_lane_[0], (unsigned long long)profile_push_fused_lane_[1], (unsigned long long)profile_push_fused_lane_[2]);
	fprintf(stderr, "[vm-profile] typed heads: binary hit %llu miss %llu unstamped %llu | cmp_jump hit %llu miss %llu unstamped %llu\n",
		(unsigned long long)profile_typed_head_[0], (unsigned long long)profile_typed_head_[1], (unsigned long long)profile_typed_head_[2],
		(unsigned long long)profile_typed_head_[3], (unsigned long long)profile_typed_head_[4], (unsigned long long)profile_typed_head_[5]);
	if (profile_push_sections_[0] + profile_push_sections_[1]) {
		const double n = double(profile_push_sections_[0] + profile_push_sections_[1]);
		fprintf(stderr, "[vm-profile] push slice: prologue %.1f | typed %llu | hetero %llu (realloc %llu) | avg cyc: compat %.1f convert %.1f charge %.1f append %.1f tail %.1f\n",
			profile_push_prologue_ / n, (unsigned long long)profile_push_sections_[0], (unsigned long long)profile_push_sections_[1], (unsigned long long)profile_push_sections_[2],
			profile_push_sections_[3] / n, profile_push_sections_[4] / n, profile_push_sections_[5] / n, profile_push_sections_[6] / n, profile_push_sections_[7] / n);
	}
	if (!profile_builtin_direct_names_.empty()) {
		std::vector<std::pair<std::string, uint64_t>> bns(profile_builtin_direct_names_.begin(), profile_builtin_direct_names_.end());
		std::sort(bns.begin(), bns.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		fprintf(stderr, "[vm-profile] builtin-direct calls by name:\n");
		size_t shown = 0;
		for (const auto& [name, count] : bns) {
			fprintf(stderr, "  %-24s %10llu\n", name.c_str(), (unsigned long long)count);
			if (++shown >= 10) break;
		}
	}
	uint64_t total = 0;
	for (int i = 0; i < 256; ++i) total += profile_cycles_[i];
	if (total == 0) return;
	std::vector<int> order;
	for (int i = 0; i < 256; ++i) if (profile_counts_[i]) order.push_back(i);
	std::sort(order.begin(), order.end(), [&](int a, int b) { return profile_cycles_[a] > profile_cycles_[b]; });
	fprintf(stderr, "\n[vm-profile] opcode self-time (total %llu Mcycles)\n", (unsigned long long)(total / 1000000));
	fprintf(stderr, "%-24s %10s %14s %8s %7s\n", "opcode", "count", "cycles(M)", "avg", "pct");
	for (int i : order) {
		fprintf(stderr, "%-24.*s %10llu %14.1f %8.1f %6.2f%%\n",
			(int)opcode_name(static_cast<opcode>(i)).size(), opcode_name(static_cast<opcode>(i)).data(),
			(unsigned long long)profile_counts_[i],
			profile_cycles_[i] / 1e6,
			(double)profile_cycles_[i] / (double)profile_counts_[i],
			100.0 * (double)profile_cycles_[i] / (double)total);
	}
	if (profile_new_count_) {
		fprintf(stderr, "[vm-profile] exec_new class path: %llu ctors | resolve avg %.0f cyc | invoke avg %.0f cyc\n",
			(unsigned long long)profile_new_count_,
			(double)profile_new_resolve_cyc_ / (double)profile_new_count_,
			(double)profile_new_invoke_cyc_ / (double)profile_new_count_);
	}
	if (profile_ctor_count_) {
		fprintf(stderr, "[vm-profile] construct_instance sections (%llu ctors, avg cyc): overload %.0f | instance+env+binds %.0f | init-chains %.0f | field-inits %.0f | ctor-body %.0f\n",
			(unsigned long long)profile_ctor_count_,
			(double)profile_ctor_cyc_[0] / (double)profile_ctor_count_,
			(double)profile_ctor_cyc_[1] / (double)profile_ctor_count_,
			(double)profile_ctor_cyc_[2] / (double)profile_ctor_count_,
			(double)profile_ctor_cyc_[3] / (double)profile_ctor_count_,
			(double)profile_ctor_cyc_[4] / (double)profile_ctor_count_);
	}
	if (profile_cfs_counts_[0] + profile_cfs_counts_[1] + profile_cfs_counts_[2]) {
		fprintf(stderr, "[vm-profile] CALL_FROM_SCRATCH exec decomposition (cycles exclude in-loop callee bodies; opaque INCLUDES the native execution):\n");
		static constexpr const char* cfs_names[3] = { "pinned in-loop", "sliced bound-method", "opaque invoke" };
		for (int i = 0; i < 3; ++i) {
			if (!profile_cfs_counts_[i]) continue;
			fprintf(stderr, "  %-20s %10llu calls %12.1f Mcyc %10.1f avg\n", cfs_names[i],
				(unsigned long long)profile_cfs_counts_[i], profile_cfs_cycles_[i] / 1e6,
				(double)profile_cfs_cycles_[i] / (double)profile_cfs_counts_[i]);
		}
	}
	if (profile_call_push_count_) {
		fprintf(stderr, "[vm-profile] call-floor push sections (%llu pushes, avg cyc, ~10-15 probe baseline each): entry+chunk+rec %.0f | rec-init %.0f | env %.0f | frame-init %.0f | bind %.0f | window+stage %.0f\n",
			(unsigned long long)profile_call_push_count_,
			(double)profile_call_push_cyc_[0] / (double)profile_call_push_count_,
			(double)profile_call_push_cyc_[1] / (double)profile_call_push_count_,
			(double)profile_call_push_cyc_[2] / (double)profile_call_push_count_,
			(double)profile_call_push_cyc_[3] / (double)profile_call_push_count_,
			(double)profile_call_push_cyc_[4] / (double)profile_call_push_count_,
			(double)profile_call_push_cyc_[5] / (double)profile_call_push_count_);
	}
	if (profile_probe_count_) {
		fprintf(stderr, "[vm-profile] probe-callee sections (%llu probes, avg cyc): resolve %.0f | ic+park %.0f\n",
			(unsigned long long)profile_probe_count_,
			(double)profile_probe_cyc_[0] / (double)profile_probe_count_,
			(double)profile_probe_cyc_[1] / (double)profile_probe_count_);
	}
	if (profile_call_ret_count_) {
		fprintf(stderr, "[vm-profile] call-floor return sections (%llu returns, avg cyc): conv %.0f | pop-core %.0f | swap+push %.0f\n",
			(unsigned long long)profile_call_ret_count_,
			(double)profile_call_ret_cyc_[0] / (double)profile_call_ret_count_,
			(double)profile_call_ret_cyc_[1] / (double)profile_call_ret_count_,
			(double)profile_call_ret_cyc_[2] / (double)profile_call_ret_count_);
	}
	if (profile_pop_sub_count_) {
		fprintf(stderr, "[vm-profile] pop-core sub-split (%llu pops, avg cyc, ~10-15 probe baseline each): this+envswap %.0f | window-erase %.0f | aux-truncates %.0f | env-release %.0f | scrub %.0f\n",
			(unsigned long long)profile_pop_sub_count_,
			(double)profile_pop_sub_[0] / (double)profile_pop_sub_count_,
			(double)profile_pop_sub_[1] / (double)profile_pop_sub_count_,
			(double)profile_pop_sub_[2] / (double)profile_pop_sub_count_,
			(double)profile_pop_sub_[3] / (double)profile_pop_sub_count_,
			(double)profile_pop_sub_[4] / (double)profile_pop_sub_count_);
	}
	if (profile_slice_gate_[0] + profile_slice_gate_[3]) {
		fprintf(stderr, "[vm-profile] slice-gate: instance-receiver %llu | mic-pass %llu | slice-declined %llu | mic-fail %llu\n",
			(unsigned long long)profile_slice_gate_[0], (unsigned long long)profile_slice_gate_[1],
			(unsigned long long)profile_slice_gate_[2], (unsigned long long)profile_slice_gate_[3]);
	}
	{
		uint64_t bind_total = 0;
		for (int i = 0; i < 7; ++i) bind_total += profile_bind_paths_[i];
		if (bind_total) {
			fprintf(stderr, "[vm-profile] bind paths: in-place %llu | prim-slot %llu | exact-class %llu | convert %llu | ref %llu | default %llu | escape-boxed %llu\n",
				(unsigned long long)profile_bind_paths_[0], (unsigned long long)profile_bind_paths_[1],
				(unsigned long long)profile_bind_paths_[2], (unsigned long long)profile_bind_paths_[3],
				(unsigned long long)profile_bind_paths_[4], (unsigned long long)profile_bind_paths_[5],
				(unsigned long long)profile_bind_paths_[6]);
		}
	}
	if (profile_mpush_count_) {
		fprintf(stderr, "[vm-profile] method-push receiver+env sub-split (avg cyc): set_this %.0f | env-acquire/rebind %.0f\n",
			(double)profile_mpush_sub_[0] / (double)profile_mpush_count_,
			(double)profile_mpush_sub_[1] / (double)profile_mpush_count_);
		fprintf(stderr, "[vm-profile] method-push prologue split (avg cyc from entry): depth+limit %.0f | +chunk-cache %.0f | +records %.0f\n",
			(double)profile_mpush_pro_[0] / (double)profile_mpush_count_, (double)profile_mpush_pro_[1] / (double)profile_mpush_count_, (double)profile_mpush_pro_[2] / (double)profile_mpush_count_);
		fprintf(stderr, "[vm-profile] decl fast-path sub-sections (avg cyc): flags+top %.0f | move+pop %.0f | tag+slot %.0f (%llu fast decls)\n",
			profile_decl_paths_[0] ? (double)profile_decl_sub_[1] / (double)profile_decl_paths_[0] : 0.0,
			profile_decl_paths_[0] ? (double)profile_decl_sub_[2] / (double)profile_decl_paths_[0] : 0.0, profile_decl_paths_[0] ? (double)profile_decl_sub_[3] / (double)profile_decl_paths_[0] : 0.0, (unsigned long long)profile_decl_paths_[0]);
		fprintf(stderr, "[vm-profile] fused_cmp_jump: operand resolution %.0f avg cyc over %llu compares; %llu had a shared_ptr side\n",
			profile_cmp_count_ ? (double)profile_cmp_operands_cyc_ / (double)profile_cmp_count_ : 0.0, (unsigned long long)profile_cmp_count_, (unsigned long long)profile_cmp_sptr_null_);
		fprintf(stderr, "[vm-profile] method-push sections (%llu pushes, avg cyc): entry+chunk+rec %.0f | rec-init+pins %.0f | receiver+env %.0f | frame-init %.0f | bind %.0f | window+stage %.0f\n",
			(unsigned long long)profile_mpush_count_,
			(double)profile_mpush_cyc_[0] / (double)profile_mpush_count_,
			(double)profile_mpush_cyc_[1] / (double)profile_mpush_count_,
			(double)profile_mpush_cyc_[2] / (double)profile_mpush_count_,
			(double)profile_mpush_cyc_[3] / (double)profile_mpush_count_,
			(double)profile_mpush_cyc_[4] / (double)profile_mpush_count_,
			(double)profile_mpush_cyc_[5] / (double)profile_mpush_count_);
	}
	if (!profile_cfs_inloop_names_.empty()) {
		std::vector<std::pair<std::string, uint64_t>> callees(profile_cfs_inloop_names_.begin(), profile_cfs_inloop_names_.end());
		std::sort(callees.begin(), callees.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		fprintf(stderr, "[vm-profile] call_from_scratch pinned in-loop callees:\n");
		size_t shown = 0;
		for (const auto& [name, count] : callees) {
			fprintf(stderr, "  %-32s %10llu\n", name.c_str(), (unsigned long long)count);
			if (++shown >= 12) break;
		}
	}
	if (!profile_cfs_opaque_cycles_.empty()) {
		std::vector<std::pair<std::string, uint64_t>> callees(profile_cfs_opaque_cycles_.begin(), profile_cfs_opaque_cycles_.end());
		std::sort(callees.begin(), callees.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		fprintf(stderr, "[vm-profile] call_from_scratch opaque cycles by callee:\n");
		for (const auto& [name, cyc] : callees) {
			fprintf(stderr, "  %-32s %12.1f Mcyc\n", name.c_str(), cyc / 1e6);
		}
	}
	if (!profile_native_callees_.empty()) {
		std::vector<std::pair<std::string, uint64_t>> callees(profile_native_callees_.begin(), profile_native_callees_.end());
		std::sort(callees.begin(), callees.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		fprintf(stderr, "[vm-profile] call_from_scratch in-loop MISSES by callee (native/opaque boundary):\n");
		size_t shown = 0;
		for (const auto& [name, count] : callees) {
			fprintf(stderr, "  %-32s %10llu\n", name.c_str(), (unsigned long long)count);
			if (++shown >= 20) break;
		}
	}
	const uint64_t decl_total = profile_decl_paths_[0] + profile_decl_paths_[1] + profile_decl_paths_[2] +
	                            profile_decl_paths_[3] + profile_decl_paths_[4];
	if (decl_total) {
		fprintf(stderr, "[vm-profile] DECL_VAR paths: hit %llu | flags/slot %llu | ref-top %llu | storage %llu | type %llu\n",
			(unsigned long long)profile_decl_paths_[0], (unsigned long long)profile_decl_paths_[1],
			(unsigned long long)profile_decl_paths_[2], (unsigned long long)profile_decl_paths_[3],
			(unsigned long long)profile_decl_paths_[4]);
	}
	const uint64_t store_total = profile_store_paths_[0] + profile_store_paths_[3] + profile_store_paths_[4];
	if (store_total) {
		fprintf(stderr, "[vm-profile] STORE paths: provable-hit %llu | unproven-slot %llu | proven-missed %llu | ref-top %llu\n",
			(unsigned long long)profile_store_paths_[0], (unsigned long long)profile_store_paths_[3],
			(unsigned long long)profile_store_paths_[4], (unsigned long long)profile_store_paths_[2]);
	}
	{
		static const char* const store_path_names[8] = {"?", "slot-provable", "slot-other", "slot-ref-through", "env-cell", "env-ref-through", "env-handle-ladder", "member/other"};
		bool any = false;
		for (int i = 1; i < 8; ++i) { any = any || profile_store_path_counts_[i]; }
		if (any) {
			fprintf(stderr, "[vm-profile] STORE cycles by landing path (count / avg cyc):");
			for (int i = 1; i < 8; ++i) {
				if (profile_store_path_counts_[i]) {
					fprintf(stderr, " %s %llu/%.0f |", store_path_names[i], (unsigned long long)profile_store_path_counts_[i],
						(double)profile_store_path_cycles_[i] / (double)profile_store_path_counts_[i]);
				}
			}
			fprintf(stderr, "\n");
			std::vector<std::pair<std::string, uint64_t>> by_name(profile_store_name_cycles_.begin(), profile_store_name_cycles_.end());
			std::sort(by_name.begin(), by_name.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			fprintf(stderr, "[vm-profile] STORE cycles by name (top 14):\n");
			for (size_t i = 0; i < by_name.size() && i < 14; ++i) {
				fprintf(stderr, "  %-28s %10.1f Mcyc\n", by_name[i].first.c_str(), by_name[i].second / 1e6);
			}
		}
	}
	{
		auto kinds_line = [](const char* label, const uint64_t* k) {
			fprintf(stderr, "[vm-profile] %s (measured span): null %llu int %llu float %llu string %llu char %llu bool %llu array %llu map %llu object %llu function %llu reference %llu shared %llu weak %llu other %llu\n", label,
				(unsigned long long)k[0], (unsigned long long)k[1], (unsigned long long)k[2], (unsigned long long)k[3],
				(unsigned long long)k[4], (unsigned long long)k[5], (unsigned long long)k[6], (unsigned long long)k[7],
				(unsigned long long)k[8], (unsigned long long)k[9], (unsigned long long)k[10], (unsigned long long)k[11],
				(unsigned long long)k[12], (unsigned long long)(k[13] + k[14] + k[15]));
		};
		fprintf(stderr, "[vm-profile] typed landings (measured span): temp in-place %llu | temp construct %llu | decl stamp-hit %llu | decl construct %llu | store refused %llu | store payload+stamp %llu | store construct %llu | compound in-place %llu | compound refused %llu | fallback entries %llu\n",
			(unsigned long long)profile_typed_land_[0], (unsigned long long)profile_typed_land_[1], (unsigned long long)profile_typed_land_[2],
			(unsigned long long)profile_typed_land_[3], (unsigned long long)profile_typed_land_[4], (unsigned long long)profile_typed_land_[5],
			(unsigned long long)profile_typed_land_[6], (unsigned long long)profile_typed_land_[7], (unsigned long long)profile_typed_land_[8],
			(unsigned long long)profile_typed_land_[9]);
		fprintf(stderr, "[vm-profile] typed temp misses (measured span): no live slot %llu | wrong payload or reference %llu | other stamp %llu\n",
			(unsigned long long)profile_temp_miss_[0], (unsigned long long)profile_temp_miss_[1], (unsigned long long)profile_temp_miss_[2]);
		fprintf(stderr, "[vm-profile] typed store and move landings (measured span): store in-place %llu | move in-place %llu (lane removed in row 93) | move assign %llu | move fallback %llu\n",
			(unsigned long long)profile_typed_inplace_[0], (unsigned long long)profile_typed_inplace_[1], (unsigned long long)profile_typed_inplace_[2], (unsigned long long)profile_typed_inplace_[3]);
		fprintf(stderr, "[vm-profile] closed typed loop blocks (measured span): entries %llu | head compares %llu | body steps %llu | back edges %llu | misses %llu | exits %llu\n",
			(unsigned long long)profile_block_[0], (unsigned long long)profile_block_[1], (unsigned long long)profile_block_[2], (unsigned long long)profile_block_[3], (unsigned long long)profile_block_[4], (unsigned long long)profile_block_[5]);
		fprintf(stderr, "[vm-profile] cell loops (measured span): entries %llu | refused %llu (by a container site %llu) | cell iterations %llu | unboxes %llu | exit events %llu | miss events %llu | misses %llu (cmp %llu arith %llu load %llu store %llu) | cell instructions %llu (head compares plus body ops; entry and back edge excluded) | cells flushed in place %llu, fully replaced %llu | raw area: records %llu, largest %llu bytes (8 payload + 1 kind per cell, capacity; the boxed slots stay allocated)\n",
			(unsigned long long)profile_cells_[0], (unsigned long long)profile_cells_[1], (unsigned long long)profile_cells_[10], (unsigned long long)profile_cells_[2], (unsigned long long)profile_cells_[3], (unsigned long long)profile_cells_[4], (unsigned long long)profile_cells_[5], (unsigned long long)profile_cells_[6],
			(unsigned long long)profile_cells_miss_step_[0], (unsigned long long)profile_cells_miss_step_[1], (unsigned long long)profile_cells_miss_step_[2], (unsigned long long)profile_cells_miss_step_[3], (unsigned long long)profile_cells_[7],
			(unsigned long long)profile_cells_[8], (unsigned long long)profile_cells_[9], (unsigned long long)profile_cells_records_, (unsigned long long)profile_cells_area_bytes_max_);
		{
			// the areas resident at dump time (a warm-up allocates them before the reset): every call record's
			// payload plus kind capacity, and the bookkeeping the raw area rides on
			uint64_t resident = 0, bytes_sum = 0, bytes_max = 0;
			for (const auto& r : call_records_) {
				if (!r || r->cells.payload.empty()) continue;
				++resident;
				const uint64_t b = static_cast<uint64_t>(r->cells.payload.capacity()) * sizeof(int64_t) + static_cast<uint64_t>(r->cells.kind.capacity());
				bytes_sum += b; if (b > bytes_max) { bytes_max = b; }
			}
			fprintf(stderr, "[vm-profile] cell areas resident at dump: records with an area %llu of %llu allocated records | payload+kind capacity sum %llu bytes, max %llu bytes | sizeof(call_record) %llu, sizeof(frame) %llu, sizeof(cell_state) %llu, sizeof(script_value) %llu\n",
				(unsigned long long)resident, (unsigned long long)call_records_.size(), (unsigned long long)bytes_sum, (unsigned long long)bytes_max,
				(unsigned long long)sizeof(call_record), (unsigned long long)sizeof(frame), (unsigned long long)sizeof(cell_state), (unsigned long long)sizeof(script_value));
		}
		fprintf(stderr, "[vm-profile] typed container source (measured span): T_LOAD slot %llu env %llu | INDEX_STORE_FUSED lane slot %llu env %llu\n",
			(unsigned long long)profile_typed_cont_src_[0], (unsigned long long)profile_typed_cont_src_[1],
			(unsigned long long)profile_typed_cont_src_[2], (unsigned long long)profile_typed_cont_src_[3]);
		kinds_line("LOAD result kinds", profile_load_kinds_);
		kinds_line("INDEX result kinds", profile_index_kinds_);
	}
	{
		uint64_t idx_total = 0;
		for (int i = 0; i < 8; ++i) { idx_total += profile_index_read_paths_[i]; }
		if (idx_total) {
			fprintf(stderr, "[vm-profile] INDEX-read paths: raw-int %llu | raw-float %llu | untyped-elem %llu | elem-ref %llu | conststr-map %llu | replay %llu | raw-idx-fold %llu | member-container %llu\n",
				(unsigned long long)profile_index_read_paths_[0], (unsigned long long)profile_index_read_paths_[1],
				(unsigned long long)profile_index_read_paths_[2], (unsigned long long)profile_index_read_paths_[3],
				(unsigned long long)profile_index_read_paths_[4], (unsigned long long)profile_index_read_paths_[5],
				(unsigned long long)profile_index_read_paths_[6], (unsigned long long)profile_index_read_paths_[7]);
		}
	}
}
#endif

#ifdef JAISCRIPT_VM_PROFILE
op_status vm_backend::run_dispatch(frame*& fp, const size_t records_base) {
	// Published as (counter at entry) + (this loop's whole span): a span already contains
	// every deeper loop, so an outer loop's delta since its own mark is exactly the one
	// nested span beneath its opcode, at any depth (Astra's depth-two control); the guard
	// publishes on an exception exit too
	struct span_publisher {
		vm_backend* vm; uint64_t mark; uint64_t start;
		~span_publisher() { vm->profile_nested_cycles_ = mark + (__rdtsc() - start); }
	} publish{this, profile_nested_cycles_, __rdtsc()};
	return run_dispatch_profiled(fp, records_base);
}
op_status vm_backend::run_dispatch_profiled(frame*& fp, const size_t records_base) {
#else
op_status vm_backend::run_dispatch(frame*& fp, const size_t records_base) {
#endif
	checked_result<void> shared_op_result_;
#ifdef JAISCRIPT_VM_PROFILE
	// Self-time opcode histogram (diagnostic builds only; dumped by ~vm_backend).
	// Elapsed cycles attribute to the PREVIOUS op at the next fetch, so call ops
	// charge only their own machinery, not their callee's body.
	uint64_t prof_t0 = __rdtsc();
	unsigned prof_prev = 256;
	const chunk* prof_chunk = nullptr;
	profile_chunk_stats* prof_stats = nullptr;
	size_t prof_ip = 0;
	uint64_t prof_nested_mark = profile_nested_cycles_;
#endif
	for (;;) {
		// Rebound every iteration: frame switches write fp and `continue`
		frame& f = *fp;
		const auto& code = f.code->code;
		// Every chunk compiler ends its code with op_halt and no jump target passes it,
		// so the halt case IS the end-of-chunk exit (Debug asserts the invariant)
		assert(f.ip < code.size());
		if (debug_hook_) [[unlikely]] { debug_statement_boundary(f); }
		const vm_instruction& ins = code[f.ip];
#ifdef JAISCRIPT_VM_PROFILE
		{
			const uint64_t prof_now = __rdtsc();
			if (prof_prev < 256) {
				// the previous opcode's own span, less any dispatch loop that ran inside it
				const uint64_t nested = profile_nested_cycles_ - prof_nested_mark;
				const uint64_t raw = prof_now - prof_t0;
				const uint64_t dt = raw > nested ? raw - nested : 0;
				profile_cycles_[prof_prev] += dt;
				++profile_counts_[prof_prev];
				if (prof_stats && prof_ip < prof_stats->ip_cycles.size()) {
					prof_stats->ip_cycles[prof_ip] += dt;
					++prof_stats->ip_counts[prof_ip];
				}
			}
			prof_t0 = prof_now;
			prof_nested_mark = profile_nested_cycles_;
			prof_prev = static_cast<unsigned>(ins.op);
			prof_ip = f.ip;
			if (f.code != prof_chunk) {
				prof_chunk = f.code;
				prof_stats = parallel_worker_ ? nullptr : &profile_stats_for(*f.code);
			}
		}
#endif
		switch (ins.op) {
			case opcode::op_halt:
				if (call_records_top_ == records_base) {
					return {};
				}
				// Function-body chunks end in op_halt: this is an in-loop callee's
				// implicit-return exit. Script unwinding latched by a bind-time conversion
				// (a converting ctor that threw) falls to the loop bottom while the caller's
				// try record is still live - `continue` would run its op_try_pop first
				VM_TRY_OP_SHARED(fall_off_script_frame(fp));
				if (is_unwinding_) [[unlikely]] { break; }
				continue;

			case opcode::op_const: {
				// Literals are engine-less parse-time templates; re-materialize with the engine
				const script_value& tmpl = f.code->constants[ins.a];
				const auto& storage = tmpl.get_storage();
				switch (storage.index()) {
					case script_value::TYPEID_INT: stack_.push_back(script_value(storage.get<script_int>(), engine_)); break;
					case script_value::TYPEID_FLOAT: stack_.push_back(script_value(storage.get<script_float>(), engine_)); break;
					case script_value::TYPEID_STRING: stack_.push_back(literal_string_value(*f.code, ins.a, storage.get<strong_ptr<script_string>>())); break;
					case script_value::TYPEID_CHAR: stack_.push_back(script_value(storage.get<script_char>(), engine_)); break;
					case script_value::TYPEID_BOOL: stack_.push_back(script_value(storage.get<script_bool>(), engine_)); break;
					case script_value::TYPEID_NULL: stack_.push_back(make_null()); break;
					default: {
						script_value copy = tmpl;
						copy.set_engine(engine_);
						stack_.push_back(std::move(copy));
						break;
					}
				}
				break;
			}

			case opcode::op_null: stack_.push_back(make_null()); break;
			case opcode::op_true: stack_.push_back(script_value(true, engine_)); break;
			case opcode::op_false: stack_.push_back(script_value(false, engine_)); break;
			case opcode::op_pop:
				if (!stack_.empty()) { stack_.pop_back(); }
				break;
			case opcode::op_dup: stack_.push_back(stack_.back()); break;
			case opcode::op_parallel_advance: {
				if (!parallel_cursors_ || stack_.empty()) {
					VM_TRY_OP(checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
						std::string_view("__jai_parallel_advance is only valid inside a parallel_for chunk")));
					break;
				}
				for (auto& cursor : *parallel_cursors_) { cursor.element_cursor_advance(parallel_block_, parallel_stride_blocks_); }   // the zip's arrays step together
				script_value& counter = stack_.back();
				counter = script_value(counter.deref().as_int() + 1, engine_);
				break;
			}
			case opcode::op_parallel_element:
				if (!parallel_cursors_ || ins.a >= parallel_cursors_->size()) {
					VM_TRY_OP(checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
						std::string_view("__jai_parallel_element is only valid inside a parallel_for chunk")));
					break;
				}
				stack_.push_back((*parallel_cursors_)[ins.a]);
				break;

			case opcode::op_to_bool: {
				script_value v = std::move(stack_.back());
				stack_.pop_back();
				stack_.push_back(script_value(is_truthy(v), engine_));
				break;
			}

			case opcode::op_load: VM_TRY_OP(exec_load(f, ins)); break;
			case opcode::op_store:
				VM_TRY_OP(exec_store(f, ins));
				if (ins.c & store_flag_no_result) { stack_.pop_back(); }
				break;
			case opcode::op_compound_store: VM_TRY_OP(exec_compound_store(f, ins)); break;
			case opcode::op_compound_fused: VM_TRY_OP_SHARED(exec_compound_fused(f, ins)); break;
			case opcode::op_incdec:
				VM_TRY_OP(exec_incdec(f, ins));
				if (ins.c & incdec_flag_no_result) { stack_.pop_back(); }
				break;
			case opcode::op_decl_var: VM_TRY_OP(exec_decl_var(f, ins)); break;
			case opcode::op_decl_ref_ident: VM_TRY_OP(exec_decl_ref_ident(f, ins)); break;
			case opcode::op_decl_ref_value: VM_TRY_OP(exec_decl_ref_value(f, ins)); break;
			case opcode::op_destructure: VM_TRY_OP(exec_destructure(f, ins)); break;
			case opcode::op_binary: VM_TRY_OP(exec_binary(f, ins)); break;
			case opcode::op_binary_fused: VM_TRY_OP_SHARED(exec_binary_fused(f, ins)); break;
			case opcode::op_binary_fused_temp: {
				// Register-file producer: the fused kernel's result lands RAW in a
				// compiler temp slot (inside the window; no store enforcement - temps
				// are compiler-owned and consumed by the next fused operand read)
				temp_lane_sink lane{this, f, ins.b};
				VM_TRY_OP_SHARED(binary_fused_compute(f, ins.a, lane));
				break;
			}
			// Typed scalar set: arithmetic lands in a temp, decl or store slot and may run
			// the general kernel on a miss (which can enter a script frame: operator
			// overloads), compare-jumps retarget ip, moves land or replay their pair
			case opcode::op_t_add_ii: case opcode::op_t_sub_ii: case opcode::op_t_mul_ii: case opcode::op_t_div_ii: case opcode::op_t_mod_ii:
			case opcode::op_t_and_ii: case opcode::op_t_or_ii: case opcode::op_t_xor_ii: case opcode::op_t_shl_ii: case opcode::op_t_shr_ii:
			case opcode::op_t_add_ff: case opcode::op_t_sub_ff: case opcode::op_t_mul_ff: case opcode::op_t_div_ff: case opcode::op_t_mod_ff:
				VM_TRY_OP_SHARED(exec_typed_arith(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_t_jlt_ii: case opcode::op_t_jle_ii: case opcode::op_t_jgt_ii: case opcode::op_t_jge_ii: case opcode::op_t_jeq_ii: case opcode::op_t_jne_ii:
			case opcode::op_t_jlt_ff: case opcode::op_t_jle_ff: case opcode::op_t_jgt_ff: case opcode::op_t_jge_ff: case opcode::op_t_jeq_ff: case opcode::op_t_jne_ff:
				VM_TRY_OP_SHARED(exec_typed_cmp_jump(f, ins));
				if (is_unwinding_) [[unlikely]] {
					if (!handle_throw_unwind(fp, records_base)) {
						return {};
					}
				}
				continue;
			case opcode::op_t_decl_mov: VM_TRY_OP_SHARED(exec_typed_decl_mov(f, ins)); break;
			case opcode::op_t_decl_const: VM_TRY_OP_SHARED(exec_typed_decl_const(f, ins)); break;
			case opcode::op_t_store_mov: VM_TRY_OP_SHARED(exec_typed_store_mov(f, ins)); break;
			case opcode::op_t_store_const: VM_TRY_OP_SHARED(exec_typed_store_const(f, ins)); break;
			case opcode::op_operand_temp: VM_TRY_OP_SHARED(exec_operand_temp(f, ins)); break;

			// Both always retarget f.ip
			case opcode::op_cfor_prep: VM_TRY_OP_SHARED(exec_cfor_prep(f, ins)); continue;
			case opcode::op_cfor_back: VM_TRY_OP_SHARED(exec_cfor_back(f, ins)); continue;

			// Retargets f.ip on success; on script unwinding (custom comparison threw)
			// ip stays parked here and the loop-bottom unwind handling is replicated
			case opcode::op_fused_cmp_jump:
				VM_TRY_OP_SHARED(exec_fused_cmp_jump(f, ins));
				if (is_unwinding_) [[unlikely]] {
					if (!handle_throw_unwind(fp, records_base)) {
						return {};
					}
				}
				continue;
			case opcode::op_cfor_pop:
				if (!cfor_states_.empty()) { cfor_states_.pop_back(); }
				break;
			case opcode::op_index: VM_TRY_OP(exec_index(f, ins)); break;
			case opcode::op_index_assign: VM_TRY_OP(exec_index_assign(f, ins)); break;
			case opcode::op_index_compound: VM_TRY_OP(exec_index_compound(f, ins)); break;
			case opcode::op_unary: VM_TRY_OP(exec_unary(f, ins)); break;
			case opcode::op_probe_callee:
				VM_TRY_OP_SHARED(exec_probe_callee(f, ins));
				break;
			case opcode::op_probe_member:
				VM_TRY_OP_SHARED(exec_probe_member(f, ins));
				break;
			case opcode::op_call_from_scratch:
				VM_TRY_OP_SHARED(exec_call_from_scratch(f, ins));
				if (switch_to_) {
					// Enter the pushed callee; the suspended caller's ip parks on the
					// call op (stack-trace parity with op_call)
					fp = switch_to_;
					switch_to_ = nullptr;
					continue;
				}
				// Native/opaque callee already landed its result: a statement-position
				// call drops it here with the pop dispatch (unwinding keeps the pushed
				// null for the loop-bottom handler)
				if (!is_unwinding_ && next_op_discards_result(f)) { stack_.pop_back(); ++f.ip; }
				break;
			case opcode::op_call:
				VM_TRY_OP(exec_call(f, ins));
				if (switch_to_) {
					// Enter the pushed callee; skipping ++f.ip parks the suspended
					// caller's ip on the call op (stack-trace parity)
					fp = switch_to_;
					switch_to_ = nullptr;
					continue;
				}
				if (!is_unwinding_ && next_op_discards_result(f)) { stack_.pop_back(); ++f.ip; }
				break;
			case opcode::op_func_decl: VM_TRY_OP(exec_func_decl(f, ins)); break;
			case opcode::op_closure: VM_TRY_OP(exec_closure(f, ins)); break;

			// The five hottest fused ops (7.6M/200t on GLOOM) skip exec_extended's
			// second switch: direct dispatch on the shared temp (frame stays flat),
			// keeping the group's switch_to_ tail (operator replays may park callees).
			// PLACED ABOVE the grouped labels — inserting them mid-group orphans the
			// earlier labels onto the first direct body (caught by Debug's OOB assert)
			case opcode::op_binary_fused_decl:
				VM_TRY_OP_SHARED(exec_binary_fused_decl(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_binary_fused_store:
				VM_TRY_OP_SHARED(exec_binary_fused_store(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_index_fused:
				VM_TRY_OP_SHARED(exec_index_fused(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_index_store_fused:
				VM_TRY_OP_SHARED(exec_index_store_fused(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_index_fused_decl:
				VM_TRY_OP_SHARED(exec_index_fused_decl(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			// The member trio (GET_MEMBER 477k / CALL_METHOD 983k per 200 GLOOM ticks)
			// dispatch directly too; statement-position stores and calls drop their
			// dead result together with the pop that follows
			case opcode::op_get_member:
				VM_TRY_OP_SHARED(exec_get_member(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_set_member:
				VM_TRY_OP_SHARED(exec_set_member(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				if (!is_unwinding_ && next_op_discards_result(f)) { stack_.pop_back(); ++f.ip; }
				break;
			case opcode::op_call_method:
				VM_TRY_OP_SHARED(exec_call_method(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				if (!is_unwinding_ && next_op_discards_result(f)) { stack_.pop_back(); ++f.ip; }
				break;
			case opcode::op_push_fused:
				VM_TRY_OP_SHARED(exec_push_fused(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_t_push:
				VM_TRY_OP_SHARED(exec_typed_push(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;
			case opcode::op_t_load:
				VM_TRY_OP_SHARED(exec_typed_load(f, ins));
				if (switch_to_) { fp = switch_to_; switch_to_ = nullptr; continue; }
				break;

			// One grouped dispatch: a VM_TRY_OP temp per case would bloat run()'s
			// Debug frame, which sits on the JAI_MAX_CALL_DEPTH recursion path
			case opcode::op_this:
			case opcode::op_super:
			case opcode::op_from_this:
			case opcode::op_get_static:
			case opcode::op_set_static:
			case opcode::op_member_compound:
			case opcode::op_new:
			case opcode::op_class_decl:
			case opcode::op_namespace_decl:
			case opcode::op_enum_decl:
			case opcode::op_throw:
			case opcode::op_try_push:
			case opcode::op_try_pop:
			case opcode::op_catch_end:
			case opcode::op_case_eq:
			case opcode::op_iter_init:
			case opcode::op_iter_next:
			case opcode::op_iter_pop:
			case opcode::op_include:
			case opcode::op_import:
			case opcode::op_ref_return_bind:
			case opcode::op_ref_return_lvalue:
			case opcode::op_index_store:
			case opcode::op_index_compound_fused:
			case opcode::op_math:
			case opcode::op_parallel_for:
			case opcode::op_cast:
			case opcode::op_return_convert:
				VM_TRY_OP(exec_extended(f, ins));
				if (switch_to_) {
					// op_call_method pushed an in-loop callee (flattened method or
					// direct field-function); park the caller's ip on the call op
					fp = switch_to_;
					switch_to_ = nullptr;
					continue;
				}
				break;

			// Suspends the fiber: exec_yield sets yielding_ and advances ip past the yield,
			// then run() returns cleanly so run_fiber can snapshot and hand back the value.
			// Parser guarantees op_yield only in coroutine body chunks, which are only ever
			// run_fiber entry frames — in-loop callees are always popped before this op.
			case opcode::op_yield:
				assert(call_records_top_ == records_base);
				exec_yield(f, ins);
				return {};

			case opcode::op_null_guard:
				if (stack_.back().deref().is_null()) {
					stack_.back() = make_null();
					f.ip = ins.a;
					continue;
				}
				break;

			case opcode::op_array:
				exec_array(f, ins);
				break;

			case opcode::op_map:
				exec_map(f, ins);
				break;

			case opcode::op_jump:
				f.ip = ins.a;
				continue;

			case opcode::op_jump_if_false: {
				script_value cond = std::move(stack_.back());
				stack_.pop_back();
				const bool truthy = ins.b ? cond.unchecked_as_bool() : is_truthy(cond);
				if (!truthy) {
					f.ip = ins.a;
					continue;
				}
				break;
			}

			case opcode::op_jump_if_true: {
				script_value cond = std::move(stack_.back());
				stack_.pop_back();
				const bool truthy = ins.b ? cond.unchecked_as_bool() : is_truthy(cond);
				if (truthy) {
					f.ip = ins.a;
					continue;
				}
				break;
			}

			case opcode::op_loop_back:
				if (execution_limit_exhausted()) [[unlikely]] {
					VM_TRY_OP(checked_result<void>(execution_limit_failure()));
				}
				f.ip = ins.a;
				continue;
#ifdef JAISCRIPT_PILOT_CLUSTER
			case opcode::op_t_block_back:
				VM_TRY_OP_SHARED(exec_typed_block_back(f, ins));
				continue;
#endif
#ifdef JAISCRIPT_PILOT_CELLS
			case opcode::op_cells_back: VM_TRY_OP_SHARED(exec_cells_back(f, ins)); continue;
			case opcode::op_c_arith: VM_TRY_OP_SHARED(exec_c_arith(f, ins)); continue;
			case opcode::op_c_cmp: VM_TRY_OP_SHARED(exec_c_cmp(f, ins)); continue;
			case opcode::op_c_load: VM_TRY_OP_SHARED(exec_c_load(f, ins)); continue;
			case opcode::op_c_index_store: VM_TRY_OP_SHARED(exec_c_index_store(f, ins)); continue;
			case opcode::op_c_back: VM_TRY_OP_SHARED(exec_c_back(f, ins)); continue;
#endif

		case opcode::op_return: {
				if (call_records_top_ == records_base) {
					script_value v = ins.a ? std::move(stack_.back()) : make_null();
					if (ins.a) {
						stack_.pop_back();
						if (ins.c && !detail::return_value_plain(v)) { materialize_return(f, v, ins.c, k_invalid_u32); }
					}
					VM_TRY_OP_SHARED(entry_return_finish(f, std::move(v), ins.b));
					return {};
				}
				VM_TRY_OP_SHARED(return_from_script_frame(fp, ins));
				// A return conversion that set script unwinding falls to the loop bottom while
				// the caller's try record is still live (see op_halt)
				if (is_unwinding_) [[unlikely]] { break; }
				continue;
			}

			// Return superinstructions (stage 6): entry frames mirror op_return's entry
			// branch; record frames return without the stack round trip. A compute that
			// set script unwinding falls to the loop bottom exactly like op_binary would.
			case opcode::op_return_ident: {
				if (call_records_top_ == records_base) {
					VM_TRY_OP_SHARED(exec_return_ident_entry(f, ins));
					return {};
				}
				VM_TRY_OP_SHARED(exec_return_ident(fp, ins));
				if (is_unwinding_) [[unlikely]] { break; }
				continue;
			}
			case opcode::op_return_binary: {
				if (call_records_top_ == records_base) {
					VM_TRY_OP_SHARED(exec_return_binary_entry(f, ins));
					if (is_unwinding_) [[unlikely]] { break; }
					return {};
				}
				VM_TRY_OP_SHARED(exec_return_binary(fp, ins));
				if (is_unwinding_) [[unlikely]] { break; }
				continue;
			}

			case opcode::op_implicit_return: {
				assert(call_records_top_ == records_base);   // top-level chunks only
				script_value v = std::move(stack_.back());
				stack_.pop_back();
				implicit_result_ = v.deref();
				break;
			}

			case opcode::op_scope_push:
				JAI_ENV_CENSUS(1); environment_ = acquire_scope_env(environment_);
				break;

			case opcode::op_scope_pop:
				pop_scopes_pooled(1);
				break;

			case opcode::op_scope_pop_n:
				pop_scopes_pooled(ins.a);
				break;

			case opcode::op_error: {
				const auto code_value = static_cast<runtime_error_code>(ins.a);
				const uint64_t sym = ins.c != k_invalid_u32 ? f.code->symbols[ins.c] : 0;
				if (ins.b != k_invalid_u32) {
					VM_TRY_OP(checked_result<void>(make_error_code(code_value),
						std::string_view(f.code->messages[ins.b]), sym));
				} else {
					VM_TRY_OP(checked_result<void>(make_error_code(code_value)));
				}
				break;
			}
		}

		if (is_unwinding_) [[unlikely]] {
			if (!handle_throw_unwind(fp, records_base)) {
				return {};
			}
			continue;
		}
		++f.ip;
	}
}

#undef VM_TRY_OP
#undef VM_TRY_OP_SHARED

// ============================================================
// Execution entry points
// ============================================================

script_value vm_backend::execute(const std::vector<declaration_ptr>& declarations) {
	compiler_.typed_lowering(!engine_ || engine_->type_proofs());
	return run_program(compiler_.compile_program(declarations));
}

script_value vm_backend::execute(const std::vector<declaration_ptr>& declarations, std::shared_ptr<void>& compiled_slot) {
	auto program = std::static_pointer_cast<chunk>(compiled_slot);
	if (!program) {
		compiler_.typed_lowering(!engine_ || engine_->type_proofs());
		program = compiler_.compile_program(declarations);
		compiled_slot = program;
	}
	return run_program(std::move(program));
}

script_value vm_backend::run_program(std::shared_ptr<chunk> program) {
	std::optional<detail::call_depth_guard> reentry_depth;
	checked_result<void> entry_result;
	if (is_executing()) {
		if (current_call_depth_ >= limits_->vm_call_depth_limit || detail::native_stack_low()) {
			entry_result = detail::raise_call_depth();
		} else {
			reentry_depth.emplace(current_call_depth_);
		}
	}

	// Reentrant execute (include/import, or a host callback firing mid-script, e.g. hot
	// reload): the nested program runs at TOP LEVEL - global environment - and the
	// suspended outer run's environment (a ctor/method scope env carrying 'this') is
	// restored afterwards. Mirrors interpreter::execute's reentry_isolation.
	struct reentry_isolation {
		vm_backend* self;
		bool reentrant;
		std::shared_ptr<environment> saved_env;
		explicit reentry_isolation(vm_backend* backend) : self(backend), reentrant(backend->is_executing()) {
			if (!reentrant) { self->pending_instance_env_.reset(); return; }
			saved_env = self->environment_;
			if (self->pending_instance_env_) { self->pending_instance_env_.reset(); return; }
			if (self->engine_) {
				if (auto global = self->engine_->get_global_environment()) {
					self->environment_ = std::move(global);
					return;
				}
			}
			while (self->environment_ && self->environment_->get_parent()) {
				self->environment_ = self->environment_->get_parent();
			}
		}
		~reentry_isolation() {
			if (reentrant) {
				self->environment_ = std::move(saved_env);
			}
		}
	} isolation(this);

	implicit_result_.reset();
	has_return_value_ = false;
	return_value_.reset();
	captured_trace_.clear();
	trace_captured_ = false;

	const int entry_call_depth = current_call_depth_;
	(void)entry_call_depth;

	call_frame top_locals;
	frame f;
	f.code = program.get();
	f.pin = program;
	f.ip = 0;
	f.locals = &top_locals;
	f.entry_env = environment_;
	f.stack_base = stack_.size();
	f.top_level = true;

	frame_guard guard(this, &f);
	checked_result<void> result = entry_result;
	try {
		if (result) { result = run(f); }
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
	} catch (const std::runtime_error& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
	}

	if (!result) {
		if (!trace_captured_) capture_stack_trace();
		// Interpreter boundary parity: checked errors become std::system_error text,
		// which engine::execute then surfaces as a script_exception
		std::string what;
		if (!result.message().empty()) {
			auto formatted = format_error_message(result.message(),
				symbolizer_->get_string(result.symbol_id()),
				symbolizer_->get_string(result.symbol_id2()));
			what = std::system_error(result.error(), formatted).what();
		} else {
			what = std::system_error(result.error()).what();
		}
		active_exception_value_ = script_value(what, engine_);
		current_exception_ = script_exception(what);
		is_unwinding_ = true;
	}

	if (is_unwinding_ && !trace_captured_) {
		capture_stack_trace();
	}

	if (stack_.size() > f.stack_base) {
		stack_.truncate(f.stack_base);
	}
	environment_ = f.entry_env;

	// Every in-loop pop path must decrement exactly once (no RAII on that path)
	assert(current_call_depth_ == entry_call_depth);

	if (has_return_value_) {
		script_value out = std::move(return_value_.value());
		return_value_.reset();
		has_return_value_ = false;
		return out;
	}
	if (implicit_result_.has_value()) {
		script_value out = std::move(implicit_result_.value());
		implicit_result_.reset();
		return out;
	}
	return make_null();
}

void vm_backend::precompile_parallel_method(const function_decl& decl) {
	chunk_for_body(decl.name, decl.parameters, decl.body, decl.local_count);
}

std::shared_ptr<chunk> vm_backend::chunk_for_body(std::string_view name,
                                                  const std::vector<parameter>& params,
                                                  const std::shared_ptr<block_stmt>& body,
                                                  size_t local_count) {
	auto it = chunk_cache_.find(body.get());
	if (it != chunk_cache_.end()) {
		return it->second.compiled;
	}
	// In-region compiles (unsafe-mode dispatch; admitted paths precompile at the
	// barrier) serialize engine-wide: compilation copies AST literal strong_ptrs
	// whose counts are non-atomic
	std::unique_lock<std::mutex> region_compile_guard;
	if (engine_ && engine_->parallel_region_active()) [[unlikely]] {
		region_compile_guard = std::unique_lock<std::mutex>(engine_->parallel_compile_mutex());
	}
	compiler_.typed_lowering(!engine_ || engine_->type_proofs());
	auto compiled = compiler_.compile_callable(name, params, body, local_count);
	chunk_cache_.emplace(body.get(), chunk_cache_entry{compiled, body});
	return compiled;
}

checked_result<script_value> vm_backend::execute_callable(const script_callable& payload, const std::vector<script_value>& args) {
	switch (payload.kind) {
		case script_callable::kind_type::function:
			return call_script_function(*payload.fn, args);
		case script_callable::kind_type::method: {
			if (!payload.this_obj) {
				return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Method payload without 'this' receiver");
			}
			auto method_env = acquire_method_scope_env(payload.definition_env, *payload.this_obj, payload.cls.get());
			method_env->define(this_id_, *payload.this_obj);
			auto result = execute_method_ast(payload.ast, method_env, args);
			release_scope_env(std::move(method_env));
			return result;
		}
		case script_callable::kind_type::static_method: {
			auto static_env = acquire_static_scope_env(payload.definition_env, payload.cls);
			auto result = execute_method_ast(payload.ast, static_env, args);
			release_scope_env(std::move(static_env));
			return result;
		}
		case script_callable::kind_type::constructor: {
			auto script_cls = std::dynamic_pointer_cast<script_class_definition>(payload.cls);
			if (!script_cls) {
				return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Constructor payload without script class definition");
			}
			if (script_cls->get_constructor_asts().empty()) {
				return construct_default_instance(script_cls, args);
			}
			return construct_instance(script_cls, payload.definition_env, args);
		}
	}
	return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Unknown callable kind");
}

template <class ArgsT>
op_status vm_backend::push_script_frame(frame& caller, script_value&& callee,
                                                   const script_defined_function& function,
                                                   const ArgsT& args,
                                                   size_t args_base, size_t argc,
                                                   const call_site* site) {
	if (current_call_depth_ >= limits_->vm_call_depth_limit) {
		return raise_from(detail::raise_call_depth());
	}
	if (execution_limit_exhausted()) [[unlikely]] {
		return raise_from(execution_limit_failure());
	}
	// In-loop dispatch invariant: results travel on stack_, never through return_value_
	assert(!has_return_value_);

	// Raw chunk*: every body chunk is pinned for the backend's lifetime by chunk_cache_
	// (chunk_for_body inserts, nothing erases), so the per-call shared_ptr pin was
	// two redundant atomic refcounts per call
	chunk* body_chunk;
	{
		body_chunk = static_cast<chunk*>(function.backend_body_cache.get());
		if (!body_chunk) {
			auto compiled = chunk_for_body(function.name, function.parameters(), function.body, function.local_count);
			function.backend_body_cache = compiled;
			body_chunk = compiled.get();
		}
		if (parallel_worker_) {
			// The owning handle is in hand exactly here: pin the chunk so this
			// worker's private env-lookup rows stay identity-sound for its life
			worker_pin_env_cache(function.backend_body_cache, body_chunk);
		}
		if (call_records_top_ == call_records_.size()) {
			call_records_.push_back(std::make_unique<call_record>());
		}
	}

	call_record& rec = *call_records_[call_records_top_];
	++call_records_top_;
#ifdef JAISCRIPT_VM_PROFILE
	rec.profile_entry_tsc = __rdtsc();
#endif
	rec.caller = &caller;
	rec.return_type = function.return_type;
	if (function.backend_return_conv == 0) {
		function.backend_return_conv = static_cast<uint8_t>(classify_return_conv(function.return_type));
	}
	rec.return_conv_class = function.backend_return_conv;
	// Stage 2: stack callees stay in their slot below the window — the slot IS the pin
	// (hot reload can't kill the executing function; pop truncation releases it).
	// Pooled-vector callees still pin through the record.
	const bool args_on_stack = args_are_stack(args);
	if (!args_on_stack) {
		rec.callee_pin = std::move(callee);
	}
	rec.try_base = try_records_.size();
	rec.iter_base = iter_states_.size();
	rec.cfor_base = cfor_states_.size();
	rec.pending_base = pending_callees_.size();
	rec.locals.function_name = function.name;
	rec.locals.reset_member_scope();
#ifdef JAISCRIPT_PILOT_CELLS
	rec.cells.reset();
	rec.f.cells = &rec.cells;
#endif
	// Compile-time lazy elision: plain callees whose bodies provably never touch the
	// per-call scope env skip creating it (methods/statics never elide — env kind
	// fallbacks gate field-vs-shadowing precedence)
	rec.env_lazy = !body_chunk->needs_frame_env &&
	               (!function.closure_env ||
	                (!function.closure_env->is_method_env() && !function.closure_env->is_static_method_env()));
	if (rec.env_lazy && !function.closure_env) {
		// Stage 3: the callee runs in the caller's env and the env stack does not move
		// AT ALL — nothing saved (prev_env stays null), nothing restored at pop
		rec.env_untouched = true;
	} else {
		rec.env_untouched = false;
		// Moved, not copied: both branches below overwrite environment_; the catch
		// restores it on setup failure
		rec.prev_env = std::move(environment_);
		try {
			if (rec.env_lazy) {
				environment_ = function.closure_env;
			} else {
				setup_callee_env(function, rec.locals, rec.prev_env);
			}
		} catch (...) {
			rec.callee_pin = make_null();
			rec.return_type = nullptr;
			environment_ = std::move(rec.prev_env);   // restore the caller env (moved at entry)
			--call_records_top_;
			throw;
		}
	}
	++current_call_depth_;
	rec.f.code = body_chunk;
	rec.f.ip = 0;
	rec.f.locals = &rec.locals;   // frame-kind metadata (closure_env/this); slots live in the window
	// entry_env doubles as the lexical-context marker: null ONLY for env-untouched
	// frames (plain non-closure functions are lexically top-level - no class context,
	// no closure chain); closure-lazy frames carry their closure env (chain is correct)
	rec.f.entry_env = rec.env_untouched ? nullptr : environment_;
	// Frame window (stage 2): zero-copy callers' args ARE slots 0..argc-1 in place;
	// pooled-vector callers build the window at the stack top during binding. stack_base
	// is the callee slot (zero-copy) / the window base (pooled), so every pop/unwind
	// truncation destroys the window (+pin) exactly where the old locals clear did.
	rec.f.window_backed = true;
	rec.f.window_base = args_on_stack ? args_base : stack_.size();
	rec.f.window_live = args_on_stack ? static_cast<uint32_t>(argc) : 0;
	rec.f.stack_base = args_on_stack ? args_base - 1 : stack_.size();
	rec.f.top_level = false;
	frames_.push_back(&rec.f);   // before binding: the ref-param frames_ scan must see this frame

	op_status bound{};
	try {
		bound = bind_parameters(function.parameters(), args, args_base, argc, rec.f, *rec.f.code,
		                        rec.env_untouched ? environment_ : rec.prev_env, site, &caller, caller.code,
		                        site_arg_proofs(site, caller.code, function.body.get(), function.parameters().size(), argc), &function);
	} catch (...) {
		pop_script_frame_core(rec);
		throw;
	}
	if (bound == op_status::failed) {
		pop_script_frame_core(rec);
		return bound;
	}
	if (args_on_stack) {
		// Conversions during binding push and pop above the args, so the slice is intact
		assert(stack_.size() == args_base + argc);
	}
	// Body slots up to local_count exist behind window_live (null placeholders), so
	// operand temps start above the full window
	const size_t window_slots = std::max(function.local_count, static_cast<size_t>(body_chunk->local_count));
	stack_.fill_null_to(rec.f.window_base + window_slots, engine_);
	switch_to_ = &rec.f;
	return {};
}

op_status vm_backend::enter_script_method(frame& caller, script_value&& method_val,
                                                     const script_method_dispatch& dispatch,
                                                     const std::shared_ptr<function_decl>& ast,
                                                     script_value&& receiver,
                                                     const std::vector<script_value>& arguments,
                                                     const call_site& site) {
	// Stateless ref binding: the call site travels as an argument
	try {
		return push_method_frame(caller, std::move(method_val), dispatch, ast, std::move(receiver),
		                         arguments, &site);
	} catch (const script_exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
}

op_status vm_backend::enter_script_method_sliced(frame& caller, script_value&& method_val,
                                                 const script_method_dispatch& dispatch,
                                                 const std::shared_ptr<function_decl>& ast,
                                                 script_value* receiver_owned, size_t frame_base,
                                                 size_t args_base, size_t argc,
                                                 const call_site& site) {
	try {
		return push_method_frame_sliced(caller, std::move(method_val), dispatch, ast,
		                                receiver_owned, frame_base, args_base, argc, &site);
	} catch (const script_exception& e) {
		// The frame's stack territory is still live in slice mode: drop it like the
		// plain slice path's pre-record throw handling (exec_call's in-loop catch)
		stack_.truncate(frame_base);
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		stack_.truncate(frame_base);
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
}

// KEEP BYTE-PARALLEL with push_method_frame below: identical record/env/lazy/sticky
// setup — the ONLY differences are the slice-window mechanics. Args bind in place as
// slots 0..argc-1. receiver_owned: null = the receiver sits in the stack slot at
// args_base-1 (read + COPIED into this/env bindings; the slot stays as the pin);
// non-null = the receiver rides in from a thunk payload (moved into env, pinned via
// rec.locals this). frame_base = where this frame's stack territory begins (the
// receiver/callee slot below the window, or args_base when nothing sits below) — pop
// truncation destroys territory + pin exactly like stage 2's callee slot.
op_status vm_backend::push_method_frame_sliced(frame& caller, script_value&& method_val,
                                               const script_method_dispatch& dispatch,
                                               const std::shared_ptr<function_decl>& ast,
                                               script_value* receiver_owned, size_t frame_base,
                                               size_t args_base, size_t argc,
                                               const call_site* site) {
#ifdef JAISCRIPT_VM_PROFILE
	uint64_t prof_p0 = __rdtsc();
	++profile_mpush_count_;
#define JAI_MPUSH_SUB(idx) { const uint64_t prof_q1 = __rdtsc(); profile_mpush_pro_[idx] += prof_q1 - prof_p0; }
#define JAI_MPUSH_SECTION(idx) { const uint64_t prof_p1 = __rdtsc(); profile_mpush_cyc_[idx] += prof_p1 - prof_p0; prof_p0 = prof_p1; }
#else
#define JAI_MPUSH_SECTION(idx)
#define JAI_MPUSH_SUB(idx)
#endif
	if (current_call_depth_ >= limits_->vm_call_depth_limit) {
		return raise_from(detail::raise_call_depth());
	}
	if (execution_limit_exhausted()) [[unlikely]] {
		return raise_from(execution_limit_failure());
	}
	assert(!has_return_value_);
	JAI_MPUSH_SUB(0);

	chunk* body_chunk;
	{
		if (dispatch.body_cache_key == ast->body.get()) {
			body_chunk = static_cast<chunk*>(dispatch.body_cache.get());
		} else {
			auto compiled = chunk_for_body(ast->name, ast->parameters, ast->body, ast->local_count);
			dispatch.body_cache = compiled;
			dispatch.body_cache_key = ast->body.get();
			body_chunk = compiled.get();
		}
		JAI_MPUSH_SUB(1);
		if (call_records_top_ == call_records_.size()) {
			call_records_.push_back(std::make_unique<call_record>());
		}
	}
	JAI_MPUSH_SUB(2);
	JAI_MPUSH_SECTION(0);

	call_record& rec = *call_records_[call_records_top_];
	++call_records_top_;
#ifdef JAISCRIPT_VM_PROFILE
	rec.profile_entry_tsc = __rdtsc();
#endif
	rec.caller = &caller;
	rec.return_type = ast->return_type;
	if (ast->backend_return_conv == 0) {
		ast->backend_return_conv = static_cast<uint8_t>(classify_return_conv(ast->return_type));
	}
	rec.return_conv_class = ast->backend_return_conv;   // typed method returns take the same memoized epilogue as functions
	if (rec.ast_pin.get() != ast.get()) [[unlikely]] {
		rec.ast_pin = ast;   // the resolved overload must outlive a mid-call hot reload; slot
	}                        // reuse with the SAME overload (recursion) skips the atomic pair
	rec.method_result_anchor = true;
	rec.callee_pin = std::move(method_val);   // pins the dispatcher and, through it, the class
	rec.try_base = try_records_.size();
	rec.iter_base = iter_states_.size();
	rec.cfor_base = cfor_states_.size();
	rec.pending_base = pending_callees_.size();
	rec.locals.function_name = ast->name;
	rec.locals.reset_member_scope();
#ifdef JAISCRIPT_PILOT_CELLS
	rec.cells.reset();
	rec.f.cells = &rec.cells;
#endif
	rec.env_lazy = !body_chunk->needs_frame_env && dispatch.definition_env &&
	               dispatch.cls && !dispatch.cls->chain_has_nonpublic();
	rec.env_untouched = false;
	JAI_MPUSH_SECTION(1);
	try {
		// Slot receivers stay put (the slot is the pin) and this/env bindings COPY the
		// deref'd value — the vector path's caller made that same copy itself (objv =
		// object.deref()), so net copies match. Owned receivers (thunk payloads) move
		// into the env like the vector path moved.
		const script_value& receiver_v = receiver_owned ? receiver_owned->deref()
		                                                : stack_[args_base - 1].deref();
		rec.locals.set_this(receiver_v);
#ifdef JAISCRIPT_VM_PROFILE
		{ const uint64_t prof_s = __rdtsc(); profile_mpush_sub_[0] += prof_s - prof_p0; prof_p0 = prof_s; }
#endif
		if (rec.env_lazy && environment_.get() == dispatch.definition_env.get()) {
			// Recursion/sibling calls within one class: the resolution env is already
			// the definition env — skip the atomic swap pair entirely and leave nothing
			// to restore at pop (env-untouched; entry_env stays null for lazy methods,
			// 'this' rides rec.locals, public-only chains need no access context)
			rec.env_untouched = true;
		} else if (rec.env_lazy) {
			rec.prev_env = std::move(environment_);
			environment_ = dispatch.definition_env;
		} else
		if (body_chunk->method_env_reusable &&
		    dispatch.definition_env.get() == cached_global_env_) {
			rec.prev_env = std::move(environment_);
			auto& scope = dispatch.backend_scope_env;
			if (scope && scope.use_count() == 1) {
				scope->rebind_method_this(receiver_owned ? std::move(*receiver_owned) : script_value(receiver_v));
				environment_ = scope;
			} else if (!scope) {
				scope = std::make_shared<environment>(dispatch.definition_env, env_symbolizer_,
					receiver_owned ? std::move(*receiver_owned) : script_value(receiver_v));
				scope->set_access_context(dispatch.cls.get());
				scope->set_vm_pinned_scope(true);
				environment_ = scope;
			} else {
				environment_ = acquire_method_scope_env(dispatch.definition_env,
					receiver_owned ? std::move(*receiver_owned) : script_value(receiver_v), dispatch.cls.get());
			}
		} else {
			rec.prev_env = std::move(environment_);
			environment_ = acquire_method_scope_env(dispatch.definition_env,
				receiver_owned ? std::move(*receiver_owned) : script_value(receiver_v), dispatch.cls.get());
		}
	} catch (...) {
		rec.callee_pin = make_null();
		rec.return_type = nullptr;
		rec.ast_pin.reset();
		rec.method_result_anchor = false;
		rec.locals.this_object_ptr.reset();
		rec.locals.is_method = false;
		if (!rec.env_untouched && rec.prev_env) {
			environment_ = std::move(rec.prev_env);   // restore the caller env (moved above)
		}
		--call_records_top_;
		throw;
	}
#ifdef JAISCRIPT_VM_PROFILE
	{ const uint64_t prof_s = __rdtsc(); profile_mpush_sub_[1] += prof_s - prof_p0; prof_p0 = prof_s; }
#endif
	JAI_MPUSH_SECTION(2);
	++current_call_depth_;
	rec.f.code = body_chunk;
	rec.f.ip = 0;
	rec.f.locals = &rec.locals;
	rec.f.entry_env = rec.env_lazy ? nullptr : environment_;
	// Slice window: the caller's args ARE slots 0..argc-1 in place; frame_base marks
	// this frame's stack territory (receiver/callee slot when one sits below), so
	// pop/unwind truncation destroys window + pin together
	rec.f.window_backed = true;
	rec.f.window_base = args_base;
	rec.f.window_live = static_cast<uint32_t>(argc);
	rec.f.stack_base = frame_base;
	rec.f.top_level = false;
	frames_.push_back(&rec.f);   // before binding: the ref-param frames_ scan must see this frame
	JAI_MPUSH_SECTION(3);

	op_status bound{};
	try {
		bound = bind_parameters(rec.ast_pin->parameters, stack_, args_base, argc, rec.f, *rec.f.code,
		                        rec.env_untouched ? environment_ : rec.prev_env, site, &caller, caller.code,
		                        site_arg_proofs(site, caller.code, rec.ast_pin->body.get(), rec.ast_pin->parameters.size(), argc));
	} catch (...) {
		pop_script_frame_core(rec);
		throw;
	}
	if (bound == op_status::failed) {
		pop_script_frame_core(rec);
		return bound;
	}
	JAI_MPUSH_SECTION(4);
	// Conversions during binding push and pop above the args, so the slice is intact
	assert(stack_.size() == args_base + argc);
	{
		const size_t window_slots = std::max(rec.ast_pin->local_count, static_cast<size_t>(body_chunk->local_count));
		stack_.fill_null_to(rec.f.window_base + window_slots, engine_);
	}
	switch_to_ = &rec.f;
	JAI_MPUSH_SECTION(5);
#undef JAI_MPUSH_SECTION
	return {};
}

op_status vm_backend::push_method_frame(frame& caller, script_value&& method_val,
                                                   const script_method_dispatch& dispatch,
                                                   const std::shared_ptr<function_decl>& ast,
                                                   script_value&& receiver,
                                                   const std::vector<script_value>& arguments,
                                                   const call_site* site) {
	if (current_call_depth_ >= limits_->vm_call_depth_limit) {
		return raise_from(detail::raise_call_depth());
	}
	if (execution_limit_exhausted()) [[unlikely]] {
		return raise_from(execution_limit_failure());
	}
	assert(!has_return_value_);

	// Raw chunk*: pinned for the backend's lifetime by chunk_cache_ (see push_script_frame)
	chunk* body_chunk;
	{
		if (dispatch.body_cache_key == ast->body.get()) {
			body_chunk = static_cast<chunk*>(dispatch.body_cache.get());
		} else {
			auto compiled = chunk_for_body(ast->name, ast->parameters, ast->body, ast->local_count);
			dispatch.body_cache = compiled;
			dispatch.body_cache_key = ast->body.get();
			body_chunk = compiled.get();
		}
		if (call_records_top_ == call_records_.size()) {
			call_records_.push_back(std::make_unique<call_record>());
		}
	}

	call_record& rec = *call_records_[call_records_top_];
	++call_records_top_;
#ifdef JAISCRIPT_VM_PROFILE
	rec.profile_entry_tsc = __rdtsc();
#endif
	rec.caller = &caller;
	rec.return_type = ast->return_type;
	if (ast->backend_return_conv == 0) {
		ast->backend_return_conv = static_cast<uint8_t>(classify_return_conv(ast->return_type));
	}
	rec.return_conv_class = ast->backend_return_conv;   // typed method returns take the same memoized epilogue as functions
	if (rec.ast_pin.get() != ast.get()) [[unlikely]] {
		rec.ast_pin = ast;   // the resolved overload must outlive a mid-call hot reload; slot
	}                        // reuse with the SAME overload (recursion) skips the atomic pair
	rec.method_result_anchor = true;
	rec.callee_pin = std::move(method_val);   // pins the dispatcher and, through it, the class
	rec.try_base = try_records_.size();
	rec.iter_base = iter_states_.size();
	rec.cfor_base = cfor_states_.size();
	rec.pending_base = pending_callees_.size();
	rec.locals.function_name = ast->name;
	rec.locals.reset_member_scope();
#ifdef JAISCRIPT_PILOT_CELLS
	rec.cells.reset();
	rec.f.cells = &rec.cells;
#endif
	// Method-lazy (flatstack stage 5, this-in-frame): a body whose ops never park
	// state in an env runs with environment_ = the DEFINITION env (globals chain,
	// lexically correct) and NO method env at all — 'this', fields, and the access
	// context all resolve through the frame (current_this / frame_access_context).
	// Nonpublic chains stay eager: their access enforcement needs the DECLARING
	// class, which only the env-kind context carries.
	rec.env_lazy = !body_chunk->needs_frame_env && dispatch.definition_env &&
	               dispatch.cls && !dispatch.cls->chain_has_nonpublic();
	rec.env_untouched = false;
	try {
		// Net effect of the native wrapper-env round trip: a method scope parented on
		// definition_env with the receiver bound; the wrapper env and its define(this)
		// are bypassed dead weight
		rec.locals.set_this(receiver);
		if (rec.env_lazy && environment_.get() == dispatch.definition_env.get()) {
			// Resolution env already correct (recursion/sibling calls within one
			// class): skip the atomic swap pair; nothing to restore at pop
			// (KEEP BYTE-PARALLEL with push_method_frame_sliced)
			rec.env_untouched = true;
		} else if (rec.env_lazy) {
			rec.prev_env = std::move(environment_);
			environment_ = dispatch.definition_env;
		} else
		// Sticky method scope: bodies that provably never park state in the env
		// (chunk::method_env_reusable) run in ONE persistent env per dispatcher, re-bound
		// to the receiver each call - no pool round trip, no reset/epoch churn, and the
		// env's lookup caches stay warm across calls. Gated to global-parented dispatchers
		// (global redefinition is in-place into stable deque storage, so cached pointers
		// never go stale; workers null cached_global_env_ and never match) and an idle env
		// (use_count > 1 = recursion/reentry within this dispatcher - pooled fallback).
		// The pop path needs no special case: clear_this_on_frame_exit still nulls the
		// receiver, and release_scope_env's use_count guard skips pooling (the dispatcher
		// still holds the env).
		if (body_chunk->method_env_reusable &&
		    dispatch.definition_env.get() == cached_global_env_) {
			rec.prev_env = std::move(environment_);
			auto& scope = dispatch.backend_scope_env;
			if (scope && scope.use_count() == 1) {
				scope->rebind_method_this(std::move(receiver));
				environment_ = scope;
			} else if (!scope) {
				scope = std::make_shared<environment>(dispatch.definition_env, env_symbolizer_, std::move(receiver));
				scope->set_access_context(dispatch.cls.get());
				scope->set_vm_pinned_scope(true);
				environment_ = scope;
			} else {
				environment_ = acquire_method_scope_env(dispatch.definition_env, std::move(receiver), dispatch.cls.get());
			}
		} else {
			rec.prev_env = std::move(environment_);
			environment_ = acquire_method_scope_env(dispatch.definition_env, std::move(receiver), dispatch.cls.get());
		}
	} catch (...) {
		rec.callee_pin = make_null();
		rec.return_type = nullptr;
		rec.ast_pin.reset();
		rec.method_result_anchor = false;
		rec.locals.this_object_ptr.reset();
		rec.locals.is_method = false;
		if (!rec.env_untouched && rec.prev_env) {
			environment_ = std::move(rec.prev_env);   // restore the caller env (moved above)
		}
		--call_records_top_;
		throw;
	}
	++current_call_depth_;
	rec.f.code = body_chunk;
	rec.f.ip = 0;
	rec.f.locals = &rec.locals;   // frame-kind metadata (this/closure_env); slots live in the window
	// null entry_env = lazy marker: this/fields/access-context resolve from the frame
	rec.f.entry_env = rec.env_lazy ? nullptr : environment_;
	// Frame window built at the stack top during binding (pooled-vector args)
	rec.f.window_backed = true;
	rec.f.window_base = stack_.size();
	rec.f.window_live = 0;
	rec.f.stack_base = stack_.size();
	rec.f.top_level = false;
	frames_.push_back(&rec.f);   // before binding: the ref-param frames_ scan must see this frame

	op_status bound{};
	try {
		bound = bind_parameters(ast->parameters, arguments, 0, arguments.size(), rec.f, *rec.f.code,
		                        rec.env_untouched ? environment_ : rec.prev_env, site, &caller, caller.code,
		                        site_arg_proofs(site, caller.code, ast->body.get(), ast->parameters.size(), arguments.size()));
	} catch (...) {
		pop_script_frame_core(rec);
		throw;
	}
	if (bound == op_status::failed) {
		pop_script_frame_core(rec);
		return bound;
	}
	// Body slots exist behind window_live so operand temps start above the full window
	{
		const size_t window_slots = std::max(ast->local_count, static_cast<size_t>(body_chunk->local_count));
		stack_.fill_null_to(rec.f.window_base + window_slots, engine_);
	}
	switch_to_ = &rec.f;
	return {};
}

// Mirrors make_bound_method's keep-alive fix-up: a NON-OWNING method result (the C++
// chaining idiom surfacing through a script method) gets anchored to the receiver so a
// temporary receiver survives the expression. No-op for owning results and implicit
// this-returns, so only return_from_script_frame applies it.
void vm_backend::anchor_method_result(script_value& result, script_value& receiver) {
	if (!receiver.is_object() || !result.is_non_owning_object()) {
		return;
	}
	auto rv_holder = result.get_object_holder();
	auto recv_holder = receiver.get_object_holder();
	if (rv_holder && recv_holder) {
		rv_holder->keep_alive = recv_holder->data ? recv_holder->data
		                                          : recv_holder->keep_alive;
	}
}

// Exact cleanup order of call_script_function's epilogue (cleanup lambda + frame_guard
// + depth guard). Destroys the callee's script_value state NOW — deferring destruction
// is observable.
void vm_backend::pop_script_frame_core(call_record& rec) {
#ifdef JAISCRIPT_VM_PROFILE
	uint64_t prof_pc0 = __rdtsc();
	++profile_pop_sub_count_;
	if (!parallel_worker_ && rec.f.code && rec.profile_entry_tsc) {
		auto& st = profile_stats_for(*rec.f.code);
		st.inclusive_cycles += prof_pc0 - rec.profile_entry_tsc;
		++st.entries;
	}
#define JAI_POP_SUB(idx) { const uint64_t prof_pc1 = __rdtsc(); profile_pop_sub_[idx] += prof_pc1 - prof_pc0; prof_pc0 = prof_pc1; }
#else
#define JAI_POP_SUB(idx)
#endif
	if (!rec.env_lazy) {
		// Lazy frames never created an env (environment_ is the CALLER's), so there is
		// nothing to clear; clearing it poisoned a method caller's this-binding.
		clear_this_on_frame_exit(rec.f.entry_env);
	}
	if (!rec.env_untouched) {
		// env-untouched frames never moved environment_ (prev_env is null — assigning
		// it would CLOBBER the live caller env)
		environment_ = std::move(rec.prev_env);
	}
	JAI_POP_SUB(0);
	if (stack_.size() > rec.f.stack_base) {
		// Window + operand temps + (zero-copy) the callee pin die HERE — the same
		// boundary the old locals clear destroyed callee state at
		stack_.truncate(rec.f.stack_base);
	}
	JAI_POP_SUB(1);
	if (try_records_.size() > rec.try_base) {
		try_records_.erase(try_records_.begin() + rec.try_base, try_records_.end());
	}
	if (iter_states_.size() > rec.iter_base) {
		iter_states_.erase(iter_states_.begin() + rec.iter_base, iter_states_.end());
	}
	if (cfor_states_.size() > rec.cfor_base) {
		cfor_states_.erase(cfor_states_.begin() + rec.cfor_base, cfor_states_.end());
	}
	if (pending_callees_.size() > rec.pending_base) {
		pending_callees_.erase(pending_callees_.begin() + rec.pending_base, pending_callees_.end());
	}
	JAI_POP_SUB(2);
	if (!rec.env_lazy) {
		// Moved out first so the pool's use_count()==1 guard sees today's count
		release_scope_env(std::move(rec.f.entry_env));
	} else if (rec.f.entry_env) {
		rec.f.entry_env = nullptr;
	}
	JAI_POP_SUB(3);
	// Scrub only what this frame actually dirtied (function frames leave method/static
	// metadata untouched, and their callee pin lives on the stack, not the record)
	if (rec.locals.is_method) {
		// Null the receiver but KEEP the allocation: records are pooled and set_this
		// reuses it (is_method stays the validity gate for get_this consumers)
		*rec.locals.this_object_ptr = make_null();
		rec.locals.is_method = false;
	}
	if (rec.locals.static_class_def) {
		rec.locals.static_class_def = nullptr;
		rec.locals.is_static_method = false;
	}
	if (rec.callee_pin.raw_storage_index() != script_value::TYPEID_NULL) {
		rec.callee_pin = make_null();
	}
	if (rec.direct_pin) {
		rec.direct_pin = {};
	}
	rec.return_type = nullptr;
	// ast_pin DEFERS to slot reuse (never reset here): the pool slot keeps the decl
	// alive until a DIFFERENT overload claims it (push assigns only on change), so
	// recursion re-pins the same method for zero atomic refcounts. A held decl past
	// pop is not script-observable — AST decls have no destructor semantics; this is
	// deferred memory release bounded by pool depth (same argument as direct_pin).
	rec.method_result_anchor = false;
	rec.ctor_result_stamp = nullptr;
	rec.f.code = nullptr;   // record frames borrow the chunk (chunk_cache_ pins it), no f.pin
	--current_call_depth_;
	frames_.pop_back();
	--call_records_top_;
	JAI_POP_SUB(4);
#undef JAI_POP_SUB
}

// Cold bodies of the return doors (kept out of the hot epilogues and the dispatch loop)
void vm_backend::copy_reference_result(script_value& result) {
	detail::materialize_return_value(result, false);
}

void vm_backend::materialize_return(frame& f, script_value& result, uint32_t operand_class, uint32_t own_slot) {
	if (detail::return_value_plain(result) || f.top_level || operand_class == detail::return_operand_unclassified) return;
	if (operand_class == detail::return_operand_ident && own_slot != k_invalid_u32 && f.locals) {
		if (script_value* payload = detail::return_own_slot_payload(frame_slot(f, own_slot), result)) {
			*payload = make_null();
			return;
		}
	}
	detail::materialize_return_value(result, operand_class != detail::return_operand_temporary);
}

op_status vm_backend::return_from_script_frame(frame*& fp, const vm_instruction& ins) {
	script_value result = ins.a ? std::move(stack_.back()) : make_null();
	if (ins.a) {
		stack_.pop_back();
		if (ins.c && !detail::return_value_plain(result)) { materialize_return(*fp, result, ins.c, k_invalid_u32); }
	}
	return return_with_result(fp, std::move(result), ins.b);
}

op_status vm_backend::return_with_result(frame*& fp, script_value result, uint32_t mode) {
#ifdef JAISCRIPT_VM_PROFILE
	uint64_t prof_p0 = __rdtsc();
	++profile_call_ret_count_;
#define JAI_CALL_RET_SECTION(idx) { const uint64_t prof_p1 = __rdtsc(); profile_call_ret_cyc_[idx] += prof_p1 - prof_p0; prof_p0 = prof_p1; }
#else
#define JAI_CALL_RET_SECTION(idx)
#endif
	call_record& rec = *call_records_[call_records_top_ - 1];
	// The record's cached classification picks the epilogue without re-deriving it from
	// type_info per return. none = convert_return_value's no-op route inline (deref only);
	// prim_* pass a storage-matching plain result through verbatim — the identical value
	// the full path's identity try_convert yields for primitives (raw-index gate excludes
	// bound values and references; mismatches fall to the full route below, so conversion
	// and error behavior are byte-identical). unclassified (method frames) keeps the
	// legacy decision.
	bool deref_only = false;
	switch (static_cast<return_conv>(rec.return_conv_class)) {
		case return_conv::none:
			deref_only = true;
			break;
		case return_conv::unclassified:
			deref_only = !rec.return_type || rec.return_type->base_type == script_value_type::jai_any_type;
			break;
		case return_conv::prim_int:
			deref_only = result.raw_storage_index() == script_value::TYPEID_INT;
			break;
		case return_conv::prim_float:
			deref_only = result.raw_storage_index() == script_value::TYPEID_FLOAT;
			break;
		case return_conv::prim_bool:
			deref_only = result.raw_storage_index() == script_value::TYPEID_BOOL;
			break;
		case return_conv::prim_char:
			deref_only = result.raw_storage_index() == script_value::TYPEID_CHAR;
			break;
		case return_conv::prim_string:
			deref_only = result.raw_storage_index() == script_value::TYPEID_STRING;
			break;
		default:   // ref / check: full route
			break;
	}
	// V-R1: a proven return whose payload guard holds is the identity - the deref-only
	// route yields exactly what convert_return_value's identity try_convert would; so is
	// a value already converted at its in-region return site (return_mode_converted)
	if (!deref_only && mode != 0 && rec.return_type &&
	    rec.return_type->base_type != script_value_type::jai_reference_type) {
		if (mode == return_mode_converted) {
			deref_only = true;
		} else {
			const script_value& probe = result.is_reference() ? result.deref() : result;
			deref_only = detail::same_type_payload(probe, rec.return_type.get());
		}
	}
	if (deref_only) {
		if (result.is_reference()) [[unlikely]] {
			copy_reference_result(result);   // a reference result copies its target
		}
		if (rec.method_result_anchor) {
			anchor_method_result(result, rec.locals.get_this());
		}
		if (rec.ctor_result_stamp && result.type() == script_value_type::jai_object_type) [[unlikely]] {
			result.set_type_info(type_info_ptr{rec.ctor_result_stamp});
		}
		JAI_CALL_RET_SECTION(0);
		pop_script_frame_core(rec);
		JAI_CALL_RET_SECTION(1);
		fp = rec.caller;
		land_call_result(*fp, std::move(result));
		JAI_CALL_RET_SECTION(2);
		return {};
	}
	// Deref + conversion run while the callee's env/frame are still live (native order);
	// a reference return validated at its in-region site passes its handle through
	auto conv = mode == return_mode_converted
		? checked_result<script_value>(std::move(result))
		: convert_return_value(std::move(result), rec.return_type, rec.f.code->function_name);
	if (!conv) {
		// The callee is still the top frame: the trace names the return site
		if (!trace_captured_) capture_stack_trace();
		pop_script_frame_core(rec);
		fp = rec.caller;
		return raise_from(conv);
	}
	if (rec.method_result_anchor) {
		anchor_method_result(conv.value(), rec.locals.get_this());
	}
	if (rec.ctor_result_stamp && conv.value().type() == script_value_type::jai_object_type) [[unlikely]] {
		conv.value().set_type_info(type_info_ptr{rec.ctor_result_stamp});
	}
	JAI_CALL_RET_SECTION(0);
	pop_script_frame_core(rec);
	JAI_CALL_RET_SECTION(1);
	fp = rec.caller;
	land_call_result(*fp, std::move(conv.value()));
	JAI_CALL_RET_SECTION(2);
#undef JAI_CALL_RET_SECTION
	return {};
}

// Fused `return <ident>;` (stage 6): resolution mirrors op_load byte-for-byte via
// fused_ident_value (same errors, same order - the fusion is adjacent, nothing is
// reordered); the value takes ONE copy exactly like op_load's push did, then the
// by-value materialization moves the own slot out or deep-copies another lvalue.
op_status vm_backend::exec_return_ident(frame*& fp, const vm_instruction& ins) {
	frame& f = *fp;
	fused_operand operand;
	operand.slot = ins.a;
	operand.symbol = ins.b;
	operand.load_flags = ins.c;
	operand.ident_node = f.code->ident_nodes.size() > f.ip ? f.code->ident_nodes[f.ip] : k_invalid_u32;
	std::optional<script_value> scratch;
	const script_value* resolved = fused_ident_value(f, operand, scratch, f.ip * 3);
	if (!resolved) return op_status::failed;
	if (detail::return_value_plain(*resolved)) {
		return return_with_result(fp, script_value(*resolved), (ins.c & load_flag_return_proven) ? 1u : 0u);
	}
	script_value value(*resolved);
	materialize_return(f, value, detail::return_operand_ident, ins.a);
	return return_with_result(fp, std::move(value), (ins.c & load_flag_return_proven) ? 1u : 0u);
}

op_status vm_backend::exec_return_ident_entry(frame& f, const vm_instruction& ins) {
	fused_operand operand;
	operand.slot = ins.a;
	operand.symbol = ins.b;
	operand.load_flags = ins.c;
	operand.ident_node = f.code->ident_nodes.size() > f.ip ? f.code->ident_nodes[f.ip] : k_invalid_u32;
	std::optional<script_value> scratch;
	const script_value* resolved = fused_ident_value(f, operand, scratch, f.ip * 3);
	if (!resolved) return op_status::failed;
	if (detail::return_value_plain(*resolved)) {
		return entry_return_finish(f, script_value(*resolved), (ins.c & load_flag_return_proven) ? 1u : 0u);
	}
	script_value value(*resolved);
	materialize_return(f, value, detail::return_operand_ident, ins.a);
	return entry_return_finish(f, std::move(value), (ins.c & load_flag_return_proven) ? 1u : 0u);
}

// Fused `return <a op b>;` (stage 6): computes exactly like exec_binary (same fast
// shapes, same general tail incl. custom operator dispatch), then returns without the
// push+pop round trip. A custom operator that THROWS leaves the result pushed and the
// ip parked here so the dispatch loop's unwind handling sees the same state op_binary
// would have produced one op earlier (same statement, same trace line).
op_status vm_backend::exec_return_binary(frame*& fp, const vm_instruction& ins) {
	frame& f = *fp;
	script_value right_raw = std::move(stack_.back());
	stack_.pop_back();
	script_value left_raw = std::move(stack_.back());
	stack_.pop_back();
	const script_value& left = left_raw.deref();
	const script_value& right = right_raw.deref();
	const token_type op = static_cast<token_type>(ins.a);

	if (ins.b != binary_shape_none && !has_custom_numeric_ops_ && is_numeric_binary_op(op)) {
		std::optional<checked_result<script_value>> fast;
		if (binary_fast_shape(op, ins.b, left, right, fast)) {
			if (!*fast) {
				return raise_from(*fast);
			}
			return return_with_result(fp, std::move(fast->value()), ins.c);
		}
	}

	auto result = binary_general(op, left, right);
	if (!result) {
		return raise_from(result);
	}
	if (is_unwinding_) [[unlikely]] {
		// Custom-op script throw mid-compute: reproduce op_binary's post-op state
		// (result pushed, frame NOT popped) and let the loop bottom unwind
		stack_.push_back(std::move(result.value()));
		return {};
	}
	return return_with_result(fp, std::move(result.value()), ins.c);
}

op_status vm_backend::exec_return_binary_entry(frame& f, const vm_instruction& ins) {
	script_value right_raw = std::move(stack_.back());
	stack_.pop_back();
	script_value left_raw = std::move(stack_.back());
	stack_.pop_back();
	const script_value& left = left_raw.deref();
	const script_value& right = right_raw.deref();
	const token_type op = static_cast<token_type>(ins.a);

	if (ins.b != binary_shape_none && !has_custom_numeric_ops_ && is_numeric_binary_op(op)) {
		std::optional<checked_result<script_value>> fast;
		if (binary_fast_shape(op, ins.b, left, right, fast)) {
			if (!*fast) {
				return raise_from(*fast);
			}
			return entry_return_finish(f, std::move(fast->value()), ins.c);
		}
	}

	auto result = binary_general(op, left, right);
	if (!result) {
		return raise_from(result);
	}
	if (is_unwinding_) [[unlikely]] {
		stack_.push_back(std::move(result.value()));
		return {};
	}
	return entry_return_finish(f, std::move(result.value()), ins.c);
}

op_status vm_backend::fall_off_script_frame(frame*& fp) {
	call_record& rec = *call_records_[call_records_top_ - 1];
	script_value result = implicit_result_for_record(rec);   // conversion skipped: fall-off parity
	if (rec.ctor_result_stamp && result.type() == script_value_type::jai_object_type) [[unlikely]] {
		result.set_type_info(rec.ctor_result_stamp);
	}
	pop_script_frame_core(rec);
	fp = rec.caller;
	land_call_result(*fp, std::move(result));
	return {};
}

bool vm_backend::ip_in_call_arg_zone(const frame& f) const {
	for (const auto& zone : f.code->call_arg_zones) {
		if (f.ip >= zone.first && f.ip < zone.second) {
			return true;
		}
	}
	return false;
}

// Interpreter parity: visit_call converts C++ throws during argument evaluation in the
// frame making the call (no pop, no result push - the call never happened), so the same
// frame's try/catch can fire; handle_throw_unwind takes it from here.
void vm_backend::convert_cpp_exception_in_frame(const script_exception& e) {
	active_exception_value_ = script_value(std::string(e.what()), engine_);
	current_exception_ = e;
	is_unwinding_ = true;
}

void vm_backend::convert_cpp_exception_at_frame(frame*& fp, const script_exception& e) {
	call_record& rec = *call_records_[call_records_top_ - 1];
	assert(fp == &rec.f);
	// The throwing frame's cleanup runs while is_unwinding_ is still false (no trace),
	// like the native catch(...) path; conversion then happens at the caller
	pop_script_frame_core(rec);
	fp = rec.caller;
	active_exception_value_ = script_value(std::string(e.what()), engine_);
	current_exception_ = e;
	is_unwinding_ = true;
	stack_.push_back(make_null());
}

void vm_backend::pop_records_to(size_t records_base, frame*& fp) {
	while (call_records_top_ > records_base) {
		call_record& rec = *call_records_[call_records_top_ - 1];
		pop_script_frame_core(rec);
		fp = rec.caller;
	}
}

void vm_backend::setup_callee_env(const script_defined_function& function, call_frame& locals,
                                  const std::shared_ptr<environment>& prev_env) {
	if (function.closure_env) {
		if (function.closure_env->is_method_env()) {
			auto this_obj = function.closure_env->get_this_object();
			locals.set_this(this_obj);
			environment_ = acquire_method_scope_env(function.closure_env->get_parent(), std::move(this_obj),
			                                        function.closure_env->get_access_context());
		} else if (function.closure_env->is_static_method_env()) {
			locals.static_class_def = function.closure_env->get_class_definition();
			locals.is_static_method = true;
			environment_ = acquire_static_scope_env(
				function.closure_env->get_parent(), function.closure_env->get_class_definition());
		} else {
			JAI_ENV_CENSUS(2); environment_ = acquire_scope_env(function.closure_env);
		}
	} else {
		JAI_ENV_CENSUS(3); environment_ = acquire_scope_env(prev_env);
	}
}

// Pooled-env hygiene on frame exit: clear this on the FRAME'S OWN method scope env
// only. The old environment_-parent walk also cleared a CALLER method scope's this
// when the exiting frame was a plain function called from a method body (demoreel
// finding 1); regression coverage: "free_call_in_method_keeps_*".
void vm_backend::clear_this_on_frame_exit(const std::shared_ptr<environment>& entry_env) {
	if (entry_env && entry_env->is_method_env()) {
		entry_env->clear_this_reference();
	}
}

// Record-aware implicit result: lazy frames apply the single-level rule against their
// effective env (prev_env / closure_env); eager frames keep the two-level native walk
script_value vm_backend::implicit_result_for_record(call_record& rec) {
	if (rec.env_lazy) {
		if (environment_->is_method_env()) {
			return environment_->get_this_object();
		}
		return make_null();
	}
	return implicit_this_result(rec.locals);
}

script_value vm_backend::implicit_this_result(call_frame& locals) {
	if (locals.is_method) {
		return locals.get_this();
	}
	auto function_env = environment_;
	if (function_env->is_method_env()) {
		return function_env->get_this_object();
	}
	if (function_env->get_parent() && function_env->get_parent()->is_method_env()) {
		return function_env->get_parent()->get_this_object();
	}
	return make_null();
}

// Return-site conversion, one routine for every door (op_return_convert inside try
// regions, the native-entry / fiber completion door): none derefs, ref validates the
// handle through the pass-through kernel, a storage-matching prim_* or a proven same-type
// payload is the identity, everything else runs the one table. KEEP BYTE-PARALLEL with
// the interpreter's visit_return_stmt.
checked_result<script_value> vm_backend::convert_return_at_site(script_value result, const type_info_ptr& return_type,
                                                                return_conv conv, bool proven, std::string_view callee) {
	if (conv == return_conv::unclassified) {
		conv = classify_return_conv(return_type);
	}
	if (conv == return_conv::ref) {
		return detail::ref_return_pass_through(std::move(result), return_type, symbolizer_);
	}
	if (result.is_reference()) {
		result = result.deref();
	}
	if (conv == return_conv::none || detail::return_conv_identity(conv, result) ||
	    (proven && detail::same_type_payload(result, return_type.get()))) {
		return result;
	}
	auto converted = try_convert_for_parameter(result, return_type,
		detail::slot_site{detail::slot_kind::return_value, {}, callee});
	if (!converted) {
		return converted.error_value();
	}
	return std::move(converted.value());
}

// In-region `return` (a try/catch is open): the conversion runs HERE, before the region
// exits pop this frame's try records, so a catch around the failing return sees it; the
// op_return after the exits lands the value as return_mode_converted
op_status vm_backend::exec_return_convert(frame& f, const vm_instruction& ins) {
	const type_info_ptr* return_type = &f.return_type;
	auto conv = static_cast<return_conv>(f.return_conv_class);
	if (call_records_top_ > 0) {
		call_record& rec = *call_records_[call_records_top_ - 1];
		if (&rec.f == &f) {
			return_type = &rec.return_type;
			conv = static_cast<return_conv>(rec.return_conv_class);
		}
	}
	if (ins.b && !detail::return_value_plain(stack_.back())) { materialize_return(f, stack_.back(), ins.b, ins.c); }
	if (conv == return_conv::none || (conv == return_conv::unclassified && !*return_type)) {
		return {};
	}
	// Moved off the stack: a converting ctor / to_X() may grow it (the slot stays reserved)
	script_value value = std::move(stack_.back());
	auto converted = convert_return_at_site(std::move(value), *return_type, conv, ins.a != 0, f.code->function_name);
	if (!converted) {
		return raise_from(converted);
	}
	if (is_unwinding_) [[unlikely]] {
		return {};   // the conversion threw: the loop bottom unwinds with this frame's record live
	}
	stack_.back() = std::move(converted.value());
	return {};
}

// Native-entry / fiber completion door: converts with the frame live (a failure raises
// in-frame, so the trace names the return line) unless the value was converted at an
// in-region return site (return_mode_converted); class 0 = no declared type on this frame
op_status vm_backend::entry_return_finish(frame& f, script_value result, uint32_t mode) {
	if (result.is_reference() && !f.top_level &&
	    !(f.return_type && f.return_type->base_type == script_value_type::jai_reference_type)) [[unlikely]] {
		copy_reference_result(result);   // a reference result copies its target
	}
	if (mode != return_mode_converted && f.return_conv_class != 0) {
		auto converted = convert_return_at_site(std::move(result), f.return_type,
		                                        static_cast<return_conv>(f.return_conv_class), mode != 0, f.code->function_name);
		if (!converted) {
			return raise_from(converted);
		}
		if (is_unwinding_) [[unlikely]] {
			return {};   // a converting ctor / to_X() threw: the throw wins, no result lands
		}
		result = std::move(converted.value());
	}
	return_value_ = std::move(result);
	has_return_value_ = true;
	return {};
}

checked_result<script_value> vm_backend::convert_return_value(script_value result, const type_info_ptr& return_type,
                                                              std::string_view callee) {
	if (return_type && return_type->base_type == script_value_type::jai_reference_type) {
		// Reference return (int& f()): the HANDLE passes through - the holder's
		// owner pin is the lifetime, so escaping the frame is legal. KEEP BYTE-
		// PARALLEL with the interpreter's call_function epilogue ref branch.
		return detail::ref_return_pass_through(std::move(result), return_type, symbolizer_);
	}
	if (result.is_reference()) {
		// Value returns flatten: a plain function's result is a copy, never an alias
		copy_reference_result(result);
	}
	// base_type first: the common any/auto return rejects on one enum compare
	// instead of three string compares (same conjunction, reordered)
	if (return_type && return_type->base_type != script_value_type::jai_any_type &&
	    !return_type->type_name.empty() &&
	    return_type->type_name != "void" &&
	    return_type->type_name != "auto") {
		auto conv = try_convert_for_parameter(result, return_type,
			detail::slot_site{detail::slot_kind::return_value, {}, callee});
		if (!conv) {
			return conv.error_value();
		}
		result = std::move(conv.value());
	}
	return result;
}

// Box-in-place + share: non-ref storage moves into a cell and the storage becomes the
// handle (all read/store paths handle the boxed form); ref storage just shares its
// holder. The one way env-variable storage is ever bound by reference — decl refs,
// [&] captures, ref returns. Cached counted-for pointers may alias the old payload
// bytes, so boxing demotes active fast loops to their generic paths (cold path: first
// ref bind only). KEEP BYTE-PARALLEL with the interpreter's share_boxed_env_storage.
script_value vm_backend::share_env_ref(script_value& storage) {
	if (!storage.is_reference()) {
		script_value inner = std::move(storage);
		storage = script_value::make_cell_reference(std::move(inner), engine_);
		for (auto& cs : cfor_states_) { cs.fast = false; }
	}
	return script_value(storage);
}

op_status vm_backend::bind_reference_to_storage(script_value& storage, frame& callee, size_t param_slot) {
	if (storage.is_reference() && !storage.get_reference_holder()) {
		return raise_(
			make_error_code(runtime_error_code::invalid_reference),
			"Reference target is null");
	}
	// Share the holder (zero alloc; cells alias the same box, element/field refs keep
	// their container/instance re-resolution), boxing on demand when the variable
	// predates its escape mark (cross-execute global, C++ define, dynamic shape)
	frame_slot_set(callee, param_slot, share_env_ref(storage));
	return {};
}

template <class ArgsT>
op_status vm_backend::bind_parameters(const std::vector<parameter>& parameters,
                                                 const ArgsT& args,
                                                 size_t args_base, size_t argc,
                                                 frame& callee, chunk& body_chunk,
                                                 const std::shared_ptr<environment>& caller_env,
                                                 const call_site* site, frame* caller_frame,
                                                 chunk* caller_code, const uint8_t* arg_proofs,
                                                 const script_defined_function* alias_fn) {
	// Stage-2 in-place binding: for zero-copy callers the args slice IS the window
	// (window_base == args_base and param i sits in slot i), so a matching-type
	// primitive argument is ALREADY its own slot - the bind copy is elided outright
	// (Ruling 2 elision proof: the copy removed is the stack->locals transfer whose
	// source died at the old post-bind erase before any script could observe it;
	// the transient-count audit is stationary). All other branches write their
	// converted/cloned/boxed value INTO the slot, replacing the raw argument.
	const bool in_place = callee.window_backed && args_are_stack(args) &&
	                      callee.window_base == args_base;
	for (size_t i = 0; i < parameters.size(); ++i) {
		const auto& param = parameters[i];

		if (i >= argc) {
			if (param.default_value) {
				auto default_chunk = i < body_chunk.param_default_chunks.size() ? body_chunk.param_default_chunks[i] : nullptr;
				if (!default_chunk) {
					return raise_(make_error_code(runtime_error_code::internal_error), "Missing compiled default argument");
				}
				frame df;
				df.code = default_chunk.get();
				df.pin = default_chunk;
				df.ip = 0;
				// The default expression reads earlier params: share the callee frame's
				// slot home (window fields for window frames, call_frame for fiber frames)
				df.locals = callee.locals;
				df.window_backed = callee.window_backed;
				df.window_base = callee.window_base;
				df.window_live = callee.window_live;
				df.entry_env = environment_;
				df.stack_base = stack_.size();
				df.top_level = false;
				auto dr = run(df);
				if (!dr) {
					return raise_from(dr);
				}
				if (is_unwinding_) {
					// Uncaught throw inside the default expression: the stack may hold
					// partial values; bind null and let the frame unwind before its first op
					if (stack_.size() > df.stack_base) {
						stack_.truncate(df.stack_base);
					}
					frame_slot_set(callee, param.slot_index, make_null());
					continue;
				}
				script_value default_val = std::move(stack_.back());
				stack_.pop_back();
				if (param.ref_escaping && !default_val.is_reference()) {
					default_val = script_value::make_cell_reference(std::move(default_val), engine_);
				}
				frame_slot_set(callee, param.slot_index, std::move(default_val));
				continue;
			}
		}

		// Fresh element access per parameter: args may alias stack_, which nested
		// conversions can reallocate between iterations
		const auto& arg = args[args_base + i];

		if (param.is_reference) {
			if (site) {
				// Stateless ref binding from the compile-time call site: identifier args
				// share the caller storage's cell handle (boxing on demand when the
				// variable predates its escape mark); field/subscript lvalue args resolve
				// to owner-pinned identity refs via the shared kernel.
				const uint64_t symbol_id = i < site->arg_symbols.size() ? site->arg_symbols[i] : UINT64_MAX;
				const uint32_t slot = i < site->arg_slots.size() ? site->arg_slots[i] : k_invalid_u32;

				// Caller frame-slot local (env lookup can't see slot locals and would
				// walk to an unrelated same-named outer variable)
				if (symbol_id != UINT64_MAX && slot != k_invalid_u32 && caller_frame) {
					if (script_value* slotPtr = frame_slot(*caller_frame, slot)) {
						VM_TRY(bind_reference_to_storage(*slotPtr, callee, param.slot_index));
						continue;
					}
				}

				// Field/subscript/chain lvalue argument: resolve through the shared
				// helper into an owner-pinned reference (Tier 1)
				const uint32_t lvalue_node = i < site->arg_lvalue_nodes.size() ? site->arg_lvalue_nodes[i] : k_invalid_u32;
				if (lvalue_node != k_invalid_u32 && caller_code) {
					if (caller_frame) {
						// the kernel resolves in the CALLER's member scope (environment_
						// already points at the callee's chain mid-push)
						ensure_frame_member_scope(*caller_frame, caller_env.get());
					}
					auto resolved = detail::resolve_ref_lvalue(
						static_cast<const expression*>(caller_code->nodes[lvalue_node].get()),
						caller_view(caller_frame), caller_env.get(), engine_, symbolizer_);
					if (resolved) {
						frame_slot_set(callee, param.slot_index, std::move(resolved.value()));
						continue;
					}
					// The kernel's generic non-lvalue verdict falls through to Tier 2
					// (identifier-keyed map entries classify Tier 1 but only evaluation
					// can resolve them); access enforcement and other specific errors
					// propagate. (KEEP BYTE-PARALLEL with bind_reference_parameter)
					if (resolved.error() != make_error_code(runtime_error_code::invalid_reference)) {
						return raise_from(resolved);
					}
				}

				if (symbol_id != UINT64_MAX && caller_env) {
					// ::name ref args bind against the GLOBAL env only
					if (i < site->arg_global.size() && site->arg_global[i] != 0) {
						script_value* g = engine_->get_global_environment()->get_value_ptr(symbol_id);
						if (!g) {
							return raise_(
								make_error_code(runtime_error_code::undefined_variable),
								"Cannot take reference of undefined variable");
						}
						VM_TRY(bind_reference_to_storage(*g, callee, param.slot_index));
						continue;
					}
					// Caller member scope: a field/static outranks a same-named global
					// as a ref target (bound as raw storage, exactly like the old
					// fallback bound the same pointer when no global collided).
					// (KEEP BYTE-PARALLEL with interpreter::bind_reference_parameter.)
					script_value* argPtr = nullptr;
					bool member_shadowed = false;
					bool property_bound = false;
					if (caller_frame && caller_frame->locals) {
						ensure_frame_member_scope(*caller_frame, caller_env.get());
						if (frame_scope_active(*caller_frame)) {
							const auto caller_scope = frame_scope_state(*caller_frame);
							argPtr = detail::own_scope_lookup(caller_env.get(),
							                                  caller_frame->locals->scope_floor, symbol_id);
							if (!argPtr) {
								auto member = detail::probe_member_scope(caller_scope,
								                                         caller_frame->locals->scope_this,
								                                         caller_frame->locals->scope_static,
								                                         symbol_id, /*want_methods*/ false);
								argPtr = member.value_ptr;
								// A C++ property binds the member itself / its accessors (reference_model.md)
								if (member.what == detail::member_probe_hit::kind::cpp_property) {
									auto minted = detail::bind_cpp_property_ref(*caller_frame->locals->scope_this,
									                                            symbol_id, engine_, symbolizer_);
									if (!minted) {
										return raise_from(minted);
									}
									frame_slot_set(callee, param.slot_index, std::move(minted.value()));
									property_bound = true;
								}
							}
							if (!argPtr && !property_bound) {
								if (detail::member_scope_has_method(caller_scope,
								                                    caller_frame->locals->scope_this,
								                                    caller_frame->locals->scope_static, symbol_id)) {
									member_shadowed = true;
								} else {
									argPtr = caller_frame->locals->scope_floor
										? caller_frame->locals->scope_floor->get_value_ptr(symbol_id)
										: caller_env->get_value_ptr(symbol_id);
								}
							}
						}
					}
					if (property_bound) {
						continue;
					}
					if (!argPtr && !member_shadowed &&
					    !(caller_frame && caller_frame->locals && frame_scope_active(*caller_frame))) {
						argPtr = caller_env->get_value_ptr(symbol_id);
						if (!argPtr && caller_frame) {
							argPtr = frame_this_member_ptr(*caller_frame, symbol_id);   // method-lazy caller: bare field args
						}
					}
					if (!argPtr) {
						// A hybrid's C++ property reached through the env's 'this' (lambdas made
						// in methods); KEEP BYTE-PARALLEL with interpreter::bind_reference_parameter
						if (auto minted = caller_frame && !member_shadowed
						        ? bind_bare_cpp_property_ref(*caller_frame, symbol_id, caller_env.get()) : std::nullopt) {
							if (!*minted) {
								return raise_from(*minted);
							}
							frame_slot_set(callee, param.slot_index, std::move(minted->value()));
							continue;
						}
						return raise_(
							make_error_code(runtime_error_code::undefined_variable),
							"Cannot take reference of undefined variable");
					}
					VM_TRY(bind_reference_to_storage(*argPtr, callee, param.slot_index));
				} else if (arg.is_reference() && arg.get_reference_holder()) {
					// Tier 2 (general lvalues; Dev ruling 2026-07-12): the argument already
					// evaluated in normal left-to-right order, and subscript/map reads over
					// lvalue bases mint owner-pinned references with full index generality -
					// computed indices (grid[y+1][x]), member-expr indices (arr[o.idx]), map
					// keys (m["k"]). A reference VALUE is an lvalue: share its holder, exactly
					// like the external-invocation path below always has. Nothing runs at the
					// bind point, so the fast-path constraint holds.
					// (KEEP BYTE-PARALLEL with the interpreter's bind_reference_parameter)
					frame_slot_set(callee, param.slot_index, script_value(arg));
				} else {
					return raise_(
						make_error_code(runtime_error_code::invalid_reference),
						"Cannot pass non-lvalue to reference parameter");
				}
			} else {
				// External (C++) invocation: object values are handles, shallow copy aliases.
				// A reference VALUE is already an lvalue: copying it shares the holder
				// (parallel_for binds each owned element this way).
				auto arg_type = arg.current_type();
				if (arg.is_reference()) {
					frame_slot_set(callee, param.slot_index, script_value(arg));
				} else if (arg_type == script_value_type::jai_object_type ||
				    arg_type == script_value_type::jai_shared_ptr_type) {
					frame_slot_set(callee, param.slot_index, script_value(arg));
				} else {
					return raise_(
						make_error_code(runtime_error_code::invalid_reference),
						"Cannot pass non-lvalue to reference parameter");
				}
			}
		} else {
			// Escape-marked value params box into a cell so downstream ref binds share it
			auto boxed_param = [&](script_value&& v) -> script_value {
				if (param.ref_escaping && !v.is_reference()) {
					return script_value::make_cell_reference(std::move(v), engine_);
				}
				return std::move(v);
			};

			// auto/var parameter + primitive argument, or a typed primitive parameter whose
			// storage already matches: copy IS clone for primitives and carries the arg's
			// type_info verbatim - exactly what the full path (identity try_convert + clone)
			// produces, so the conversion machinery has nothing to do. Mismatched-storage
			// typed parameters take the full conversion path.
			const size_t ri = arg.raw_storage_index();
			if (!arg.is_cpp_bound() &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT ||
			     ri == script_value::TYPEID_BOOL || ri == script_value::TYPEID_CHAR) &&
			    (!param.type || param.type->base_type == script_value_type::jai_any_type ||
			     (param.type->base_type == script_value_type::jai_int_type && ri == script_value::TYPEID_INT) ||
			     (param.type->base_type == script_value_type::jai_float_type && ri == script_value::TYPEID_FLOAT) ||
			     (param.type->base_type == script_value_type::jai_bool_type && ri == script_value::TYPEID_BOOL) ||
			     (param.type->base_type == script_value_type::jai_char_type && ri == script_value::TYPEID_CHAR))) {
				// In-place window arg: the pushed copy already IS the slot value with
				// the arg's type_info verbatim (copy IS clone for primitives) - nothing
				// to do unless the escape mark boxes it
				if (in_place && param.slot_index == i) {
#ifdef JAISCRIPT_VM_PROFILE
					++profile_bind_paths_[param.ref_escaping ? 6 : 0];
#endif
					if (param.ref_escaping) {
						script_value inner = std::move(stack_[args_base + i]);
						stack_[args_base + i] = script_value::make_cell_reference(std::move(inner), engine_);
					}
					continue;
				}
#ifdef JAISCRIPT_VM_PROFILE
				++profile_bind_paths_[1];
#endif
				frame_slot_set(callee, param.slot_index, boxed_param(script_value(arg)));
				continue;
			}

#ifdef JAISCRIPT_VM_PROFILE
			++profile_bind_paths_[3];
#endif
			// Shallow copy shields the conversion: it can reenter script (conversion
			// ctors / to_* methods) and reallocate stack_ under an on-stack arg
			script_value arg_shield(arg);

			// Exact-class match: a pointer-identical interned type_info (with the
			// non-empty name the identity branch requires) replicates try_convert's
			// exact-name branch (which returns the derefed arg) without the conversion
			// machinery; should_share is false for objects, so the clone matches the
			// full path
			{
				const script_value& derefed = arg_shield.deref();
				if (param.type && param.type->base_type == script_value_type::jai_object_type &&
				    derefed.storage_type() == script_value_type::jai_object_type &&
				    derefed.get_type_info().get() == param.type.get() && !param.type->type_name.empty()) {
					// Read-only parameter elision: a proven parameter shares the argument's handle
					// (KEEP BYTE-PARALLEL with interpreter::call_function)
					if (alias_fn && !param.ref_escaping && detail::parameter_alias_allows(derefed) && detail::readonly_parameter_alias(*engine_, *alias_fn, i, !parallel_worker_)) {
						if (!parallel_worker_) { engine_->note_parameter_alias_bind(); }
						frame_slot_set(callee, param.slot_index, script_value(derefed));
						continue;
					}
					frame_slot_set(callee, param.slot_index, boxed_param(derefed.clone()));
					continue;
				}
			}

			// V-C1: a proven identity bind takes try_convert_for_parameter's identity returns
			// (the arg itself; the derefed arg for exact-name objects), dereferencing exactly
			// where it does (typed params only) (KEEP BYTE-PARALLEL with interpreter I-C1)
			script_value converted_arg = make_null();
			const bool untyped = !param.type || param.type->base_type == script_value_type::jai_any_type;
			bool proven_identity = arg_proofs && arg_proofs[i] == proof_same_type;
			if (proven_identity && param.type) {
				const script_value& probe = arg_shield.deref();
				proven_identity = untyped || detail::same_type_payload(probe, param.type.get());
			}
			if (proven_identity) {
				converted_arg = (!untyped && param.type->base_type == script_value_type::jai_object_type)
				                ? script_value(arg_shield.deref()) : script_value(arg_shield);
			} else {
				auto converted_result = try_convert_for_parameter(arg_shield, param.type,
					detail::slot_site{detail::slot_kind::parameter, param.name, body_chunk.function_name});
				if (!converted_result) {
					return raise_from(converted_result);
				}
				converted_arg = std::move(converted_result.value());
			}

			// Element-read args arrive as reference wrappers - probe the referent
			// (deref() returns *this for plain values) so the shared_ptr marker is
			// visible (KEEP BYTE-PARALLEL with interpreter::call_function)
			bool should_share = false;
			const script_value& shared_probe = converted_arg.deref();
			if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) {
				should_share = true;
			}
			if (shared_probe.get_type_info() && shared_probe.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
				should_share = true;
			}

			if (should_share) {
				frame_slot_set(callee, param.slot_index, boxed_param(script_value(shared_probe)));
			} else {
				const bool heavy = shared_probe.is_object() || shared_probe.is_array() || shared_probe.is_map();
				if (heavy && alias_fn && !param.ref_escaping && detail::parameter_alias_allows(shared_probe) && detail::readonly_parameter_alias(*engine_, *alias_fn, i, !parallel_worker_)) {
					if (!parallel_worker_) { engine_->note_parameter_alias_bind(); }
					frame_slot_set(callee, param.slot_index, script_value(shared_probe));
					continue;
				}
				frame_slot_set(callee, param.slot_index, boxed_param(converted_arg.clone()));
			}
		}
	}
	return {};
}

checked_result<script_value> vm_backend::call_script_function(const script_defined_function& function, const std::vector<script_value>& args,
                                                              const call_site* site, frame* caller_frame, chunk* caller_code) {
	// Opaque entry (bound methods, constructors, function values): the arming invoke
	// left the call-site context pending - consume it exactly once
	if (!site) {
		site = pending_site_ctx_.site;
		caller_frame = pending_site_ctx_.caller_frame;
		caller_code = pending_site_ctx_.caller_code;
	}
	pending_site_ctx_ = {};
	if (current_call_depth_ >= limits_->vm_call_depth_limit) {
		return detail::raise_call_depth();
	}
	// The native entry path (constructor chains especially) stacks wide frames per
	// level and can exhaust the native stack long before the depth cap - fail
	// catchably instead of dying on 0xC00000FD (in-loop frames never recurse natively).
	// Same message as the depth cap: which limit fires first differs per backend, so a
	// distinct text here breaks backend error parity.
	if (detail::native_stack_low()) [[unlikely]] {
		return detail::raise_call_depth();
	}

	if (execution_limit_exhausted()) [[unlikely]] {
		return execution_limit_failure();
	}

	detail::call_depth_guard depth_guard(current_call_depth_);

	if (args.size() != function.parameters().size() && !arity_accepts(function.parameters(), args.size())) {
		return detail::arity_error(function.name, function.parameters().size(), args.size(), arity_count_ids_, *symbolizer_);
	}

	auto body_chunk = std::static_pointer_cast<chunk>(function.backend_body_cache);
	if (!body_chunk) {
		body_chunk = chunk_for_body(function.name, function.parameters(), function.body, function.local_count);
		function.backend_body_cache = body_chunk;
	}
	if (parallel_worker_) {
		// The region BODY enters through here (run_worker -> execute_callable), not
		// the in-loop call sites: pin it too or the hottest chunk never gets a row
		worker_pin_env_cache(function.backend_body_cache, body_chunk.get());
	}

	call_frame locals;   // frame-kind metadata only (this/static/closure_env/name); slots live in the window
	locals.function_name = function.name;

	auto previousEnv = environment_;

	setup_callee_env(function, locals, previousEnv);

	bool previousHasReturn = has_return_value_;
	std::optional<script_value> previousReturn = std::move(return_value_);
	has_return_value_ = false;

	frame f;
	f.code = body_chunk.get();
	f.pin = body_chunk;
	f.ip = 0;
	f.locals = &locals;
	f.entry_env = environment_;
	// Native-entry frame window built at the current stack top during binding
	f.window_backed = true;
	f.window_base = stack_.size();
	f.window_live = 0;
	f.stack_base = stack_.size();
	f.top_level = false;
	f.return_type = function.return_type;
	f.return_conv_class = static_cast<uint8_t>(detail::return_conv_of(function));

	frame_guard guard(this, &f);

	const size_t entry_pending = pending_callees_.size();
	auto cleanup = [&]() {
		if (is_unwinding_ && !trace_captured_) capture_stack_trace();
		if (pending_callees_.size() > entry_pending) {
			pending_callees_.erase(pending_callees_.begin() + entry_pending, pending_callees_.end());
		}
		clear_this_on_frame_exit(f.entry_env);
		environment_ = previousEnv;
		has_return_value_ = previousHasReturn;
		return_value_ = std::move(previousReturn);
		if (stack_.size() > f.stack_base) {
			stack_.truncate(f.stack_base);
		}
		release_scope_env(std::move(f.entry_env));
	};

	{
		auto bind_result = bind_parameters(function.parameters(), args, 0, args.size(), f, *body_chunk,
	                                   previousEnv, site, caller_frame, caller_code,
	                                   site_arg_proofs(site, caller_code, function.body.get(), function.parameters().size(), args.size()), &function);
		if (bind_result == op_status::failed) {
			cleanup();
			return pending_error_;
		}
	}
	// Body slots exist behind window_live so operand temps start above the full window
	{
		const size_t window_slots = std::max(function.local_count, static_cast<size_t>(body_chunk->local_count));
		stack_.fill_null_to(f.window_base + window_slots, engine_);
	}

	checked_result<void> body_result;
	try {
		body_result = run(f);
	} catch (...) {
		cleanup();
		throw;
	}
	if (!body_result) {
		if (!trace_captured_) capture_stack_trace();
		cleanup();
		return body_result.error_value();
	}

	script_value result = make_null();

	if (has_return_value_) {
		result = std::move(return_value_.value());   // converted at the return door (entry_return_finish)
	} else {
		result = implicit_this_result(locals);
	}

	cleanup();
	return result;
}

} // namespace jai::vm

namespace jai {

std::unique_ptr<execution_backend> create_vm_backend() {
	return std::make_unique<vm::vm_backend>(nullptr, nullptr);
}

} // namespace jai
