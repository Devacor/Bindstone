#include <jaiscript/vm/vm_backend.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/coroutine.hpp>
#include <jaiscript/detail/integer_ops.hpp>
#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

#ifndef JAI_MAX_CALL_DEPTH
#define JAI_MAX_CALL_DEPTH 10000
#endif

namespace jai::vm {

namespace {

	inline checked_result<void> vm_int_overflow_v(const char* msg) {
		return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
	}
	inline checked_result<script_value> vm_int_overflow_sv(const char* msg) {
		return checked_result<script_value>(make_error_code(runtime_error_code::invalid_numeric_operand), msg);
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
		auto type_info = val.get_type_info();
		if (type_info && !type_info->type_name.empty()) {
			return type_info->type_name;
		}
		return vm_type_name_of(val.type());
	}

	std::string vm_type_info_name(type_info_ptr info) {
		if (info && !info->type_name.empty()) {
			return info->type_name;
		}
		if (info) {
			return vm_type_name_of(info->base_type);
		}
		return "unknown";
	}

	bool vm_is_element_type_compatible(const script_value& element, type_info_ptr element_type, script_value& /*array_owner*/) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;

		if (element_type && element_type->base_type == script_value_type::jai_any_type) {
			return true;
		}
		if (!element_type) {
			return true;
		}

		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;

		if (elem_type == target_type) {
			if (target_type == script_value_type::jai_object_type) {
				auto elem_type_info = actual_element.get_type_info();
				if (elem_type_info && !element_type->type_name.empty()) {
					return elem_type_info->type_name == element_type->type_name;
				}
			}
			return true;
		}

		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return true;
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return true;
		}

		if (target_type == script_value_type::jai_array_type && elem_type == script_value_type::jai_array_type) {
			auto inner_target = element_type->element_type();
			auto elem_type_info = actual_element.get_type_info();
			auto inner_elem = elem_type_info ? elem_type_info->element_type() : nullptr;
			if (inner_target && inner_elem) {
				return inner_target->base_type == inner_elem->base_type;
			}
			if (inner_target && !inner_elem) {
				return false;
			}
			return true;
		}

		if (target_type == script_value_type::jai_map_type && elem_type == script_value_type::jai_map_type) {
			auto target_key = element_type->key_type();
			auto target_val = element_type->value_type();
			auto elem_type_info = actual_element.get_type_info();
			auto elem_key = elem_type_info ? elem_type_info->key_type() : nullptr;
			auto elem_val = elem_type_info ? elem_type_info->value_type() : nullptr;
			if (target_key && elem_key && target_key->base_type != elem_key->base_type) {
				return false;
			}
			if (target_val && elem_val && target_val->base_type != elem_val->base_type) {
				return false;
			}
			return true;
		}

		return false;
	}

	script_value vm_convert_array_element(engine* eng, const script_value& element, type_info_ptr element_type) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;

		auto actual_type_info = actual_element.get_type_info();
		if (actual_type_info && actual_type_info->base_type == script_value_type::jai_shared_ptr_type) {
			return actual_element;
		}

		if (!element_type || element_type->base_type == script_value_type::jai_any_type) {
			return actual_element.clone();
		}

		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;

		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return script_value(static_cast<script_int>(actual_element.unchecked_as_float()), eng);
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return script_value(static_cast<script_float>(actual_element.unchecked_as_int()), eng);
		}

		return actual_element.clone();
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

	checked_result<void> vm_validate_container_homogeneous(const script_value& container, const std::string& path);

	checked_result<void> vm_validate_map_homogeneous(const script_value& map_value, const std::string& path) {
		if (!map_value.is_map()) {
			return {};
		}
		const auto& map = map_value.as_map();
		if (map.size() <= 1) {
			if (map.size() == 1) {
				const script_value& first_val = map.begin()->second.is_reference() ?
					map.begin()->second.deref() : map.begin()->second;
				JAISCRIPT_TRY(vm_validate_container_homogeneous(first_val, path + "[0]"));
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

		JAISCRIPT_TRY(vm_validate_container_homogeneous(first_val, path + "[first]"));

		size_t idx = 1;
		for (auto it = ++map.begin(); it != map.end(); ++it, ++idx) {
			const script_value& val = it->second.is_reference() ? it->second.deref() : it->second;
			script_value_type val_type = val.type();

			if (val_type != deduced_type) {
				return checked_result<void>(
					make_error_code(runtime_error_code::map_value_type_mismatch),
					"Map 'auto' requires homogeneous values - type mismatch at index {0}",
					static_cast<uint64_t>(idx), val.type_id());
			}

			if (deduced_type == script_value_type::jai_object_type && !deduced_class_name.empty()) {
				auto val_type_info = val.get_type_info();
				std::string val_class_name = val_type_info ? val_type_info->type_name : "";
				if (val_class_name != deduced_class_name) {
					return checked_result<void>(
						make_error_code(runtime_error_code::map_value_type_mismatch),
						"Map 'auto' requires homogeneous class types - mismatch at index {0}",
						static_cast<uint64_t>(idx), val.type_id());
				}
			}

			JAISCRIPT_TRY(vm_validate_container_homogeneous(val, path + "[" + std::to_string(idx) + "]"));
		}
		return {};
	}

	checked_result<void> vm_validate_array_homogeneous(const script_value& array_value, const std::string& path) {
		if (!array_value.is_array()) {
			return {};
		}
		const auto& elements = array_value.as_array();
		if (elements.size() <= 1) {
			if (elements.size() == 1) {
				const script_value& first = elements[0].is_reference() ? elements[0].deref() : elements[0];
				JAISCRIPT_TRY(vm_validate_container_homogeneous(first, path + "[0]"));
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

		JAISCRIPT_TRY(vm_validate_container_homogeneous(first, path + "[0]"));

		for (size_t i = 1; i < elements.size(); ++i) {
			const script_value& elem = elements[i].is_reference() ? elements[i].deref() : elements[i];
			script_value_type elem_type = elem.type();

			if (elem_type != deduced_type) {
				return checked_result<void>(
					make_error_code(runtime_error_code::array_element_type_mismatch),
					"Array 'auto' requires homogeneous elements - type mismatch at index {0}",
					static_cast<uint64_t>(i), elem.type_id());
			}

			if (deduced_type == script_value_type::jai_object_type && !deduced_class_name.empty()) {
				auto elem_type_info = elem.get_type_info();
				std::string elem_class_name = elem_type_info ? elem_type_info->type_name : "";
				if (elem_class_name != deduced_class_name) {
					return checked_result<void>(
						make_error_code(runtime_error_code::array_element_type_mismatch),
						"Array 'auto' requires homogeneous class types - mismatch at index {0}",
						static_cast<uint64_t>(i), elem.type_id());
				}
			}

			JAISCRIPT_TRY(vm_validate_container_homogeneous(elem, path + "[" + std::to_string(i) + "]"));
		}
		return {};
	}

	checked_result<void> vm_validate_container_homogeneous(const script_value& container, const std::string& path) {
		if (container.is_array()) {
			return vm_validate_array_homogeneous(container, path);
		} else if (container.is_map()) {
			return vm_validate_map_homogeneous(container, path);
		}
		return {};
	}

	bool is_numeric_binary_op(token_type op) {
		switch (op) {
			case token_type::plus:
			case token_type::minus:
			case token_type::star:
			case token_type::slash:
			case token_type::percent:
			case token_type::less:
			case token_type::less_equal:
			case token_type::greater:
			case token_type::greater_equal:
			case token_type::equal_equal:
			case token_type::bang_equal:
			case token_type::spaceship:
				return true;
			default:
				return false;
		}
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
	: symbolizer_(symbolizer), environment_(std::move(global_env)), compiler_(symbolizer) {
	stack_.reserve(64);
	frames_.reserve(64);
}

void vm_backend::set_engine_reference(engine* engine_ref) {
	engine_ = engine_ref;
	if (engine_) {
		symbolizer_ = engine_->get_symbolizer();
		if (auto global = engine_->get_global_environment()) {
			environment_ = global;
		}
		compiler_ = vm_compiler(symbolizer_);

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

		builtins_ = {};
		init_builtin_method_registries(symbolizer_, builtins_);
	}
}

script_value vm_backend::make_null() const {
	return script_value(std::monostate{}, engine_);
}

void vm_backend::arm_execution_deadline() {
	budget_tick_ = 0;
	budget_active_ = execution_budget_.count() > 0;
	if (budget_active_) {
		execution_deadline_ = std::chrono::steady_clock::now() + execution_budget_;
	}
}

bool vm_backend::execution_budget_exhausted() {
	if (!budget_active_ || ++budget_tick_ < 1024) { return false; }
	budget_tick_ = 0;
	return std::chrono::steady_clock::now() >= execution_deadline_;
}

void vm_backend::set_execution_budget(std::chrono::nanoseconds budget) {
	execution_budget_ = budget;
	if (budget.count() <= 0) { budget_active_ = false; }
}

void vm_backend::prepare_for_execution() {
	arm_execution_deadline();

	// A re-entrant execute (include/import, or a host callback calling execute() mid-run)
	// stacks a fresh frame above the live ones — leave the outer frames' value/try/iter
	// slices intact so the outer execution (or a running fiber) resumes uncorrupted.
	const bool reentrant = !frames_.empty();

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
	}

	// Preserve a running coroutine's fiber so its next yield still finds its handle;
	// otherwise the failed yield would hand the caller a stale prior value.
	if (!active_coroutine_ || active_coroutine_->get_status() != coroutine_handle::status::running) {
		active_coroutine_ = nullptr;
		yielding_ = false;
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
	external_metadata_stack_.push_back(std::move(current_arg_metadata_));
	current_arg_metadata_.clear();
}

void vm_backend::pop_external_call_scope() {
	if (!external_metadata_stack_.empty()) {
		current_arg_metadata_ = std::move(external_metadata_stack_.back());
		external_metadata_stack_.pop_back();
	}
}

// Suspended-fiber snapshot. Invariant: op_yield only fires in the coroutine's own body frame
// (any function it called has already returned), so this frame is always the topmost VM frame
// and its stack/try/iter slices sit at the top of the shared vectors — saved relative to the
// fiber's bases, re-based on the next resume.
struct vm_backend::vm_coroutine_state : coroutine_backend_state {
	std::shared_ptr<chunk> body_chunk;
	call_frame locals;
	size_t ip = 0;
	std::shared_ptr<environment> entry_env;
	std::shared_ptr<environment> current_env;
	std::vector<script_value> saved_stack;
	std::vector<try_record> saved_try;      // stack_size/iter_size stored relative to fiber bases
	std::vector<iter_state> saved_iter;
	std::vector<counted_for_state> saved_cfor;
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
	// A top-level resume (VM call-depth 0) arms a fresh deadline; a resume nested inside a
	// running script inherits the outer deadline.
	if (current_call_depth_ == 0) {
		arm_execution_deadline();
	}

	auto& state = coroutine_fiber_state(handle);
	const auto prev_status = handle.get_status();
	handle.set_status(coroutine_handle::status::running);

	if (prev_status == coroutine_handle::status::created) {
		auto function = handle.get_function();
		if (!function) {
			handle.set_status(coroutine_handle::status::failed);
			return checked_result<script_value>(make_error_code(runtime_error_code::internal_error),
				"Coroutine has no function to run");
		}

		{
			size_t required_params = 0;
			for (const auto& p : function->parameters) {
				if (!p.default_value) { ++required_params; } else { break; }
			}
			const auto& args = handle.get_args();
			if (args.size() < required_params || args.size() > function->parameters.size()) {
				handle.set_status(coroutine_handle::status::failed);
				return checked_result<script_value>(
					make_error_code(runtime_error_code::argument_count_mismatch),
					"Function expected {0} arguments but got {1}",
					static_cast<uint64_t>(function->parameters.size()), static_cast<uint64_t>(args.size()));
			}
		}

		auto body_chunk = chunk_for_body(function->name, function->parameters, function->body, function->local_count);
		state.body_chunk = body_chunk;
		state.locals = call_frame{};
		state.locals.function_name = function->name;
		state.locals.reserve_locals(std::max(function->local_count, body_chunk->local_count));

		auto closure_env = handle.get_closure_env();
		state.locals.closure_env = closure_env ? closure_env : environment_;
		state.entry_env = std::make_shared<environment>(state.locals.closure_env, symbolizer_);
		state.current_env = state.entry_env;
		state.ip = 0;

		const auto& args = handle.get_args();
		auto prev_env = environment_;
		environment_ = state.entry_env;
		for (size_t i = 0; i < function->parameters.size(); ++i) {
			const auto& param = function->parameters[i];
			if (i >= args.size()) {
				auto default_chunk = i < body_chunk->param_default_chunks.size() ? body_chunk->param_default_chunks[i] : nullptr;
				if (!default_chunk) {
					environment_ = prev_env;
					handle.set_status(coroutine_handle::status::failed);
					return checked_result<script_value>(make_error_code(runtime_error_code::internal_error), "Missing compiled default argument");
				}
				frame df;
				df.code = default_chunk.get();
				df.pin = default_chunk;
				df.ip = 0;
				df.locals = &state.locals;
				df.entry_env = environment_;
				df.stack_base = stack_.size();
				df.top_level = false;
				auto dr = run(df);
				if (!dr) {
					stack_.erase(stack_.begin() + df.stack_base, stack_.end());
					environment_ = prev_env;
					handle.set_status(coroutine_handle::status::failed);
					return dr.error_value();
				}
				script_value default_val = std::move(stack_.back());
				stack_.pop_back();
				state.locals.set_local(param.slot_index, std::move(default_val));
				continue;
			}
			auto converted_result = try_convert_for_parameter(args[i], param.type);
			if (!converted_result) {
				environment_ = prev_env;
				handle.set_status(coroutine_handle::status::failed);
				return converted_result.error_value();
			}
			script_value converted_arg = std::move(converted_result.value());
			bool should_share = false;
			if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) { should_share = true; }
			if (converted_arg.get_type_info() && converted_arg.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) { should_share = true; }
			if (should_share) {
				state.locals.set_local(param.slot_index, converted_arg);
			} else {
				state.locals.set_local(param.slot_index, converted_arg.clone());
			}
		}
		environment_ = prev_env;
		state.started = true;
	}

	return run_fiber(handle, state);
}

checked_result<script_value> vm_backend::run_fiber(coroutine_handle& handle, vm_coroutine_state& state) {
	coroutine_handle* prev_active = active_coroutine_;
	active_coroutine_ = &handle;

	frame f;
	f.code = state.body_chunk.get();
	f.pin = state.body_chunk;
	f.ip = state.ip;
	f.locals = &state.locals;
	f.entry_env = state.entry_env;
	f.top_level = false;

	auto prev_env = environment_;
	environment_ = state.current_env ? state.current_env : state.entry_env;

	// Restore the fiber's stack / iter / try slices above the caller's current tops.
	const size_t stack_base = stack_.size();
	f.stack_base = stack_base;
	for (auto& v : state.saved_stack) { stack_.push_back(std::move(v)); }
	state.saved_stack.clear();

	const size_t iter_base = iter_states_.size();
	for (auto& is : state.saved_iter) { iter_states_.push_back(std::move(is)); }
	state.saved_iter.clear();

	const size_t cfor_base = cfor_states_.size();
	for (auto& cs : state.saved_cfor) { cfor_states_.push_back(cs); }
	state.saved_cfor.clear();

	const size_t try_base = try_records_.size();
	for (auto& tr : state.saved_try) {
		tr.owner = &f;
		tr.stack_size += stack_base;
		tr.iter_size += iter_base;
		tr.cfor_size += cfor_base;
		try_records_.push_back(std::move(tr));
	}
	state.saved_try.clear();

	frames_.push_back(&f);

	const bool prev_yielding = yielding_;
	yielding_ = false;
	const bool prev_has_return = has_return_value_;
	std::optional<script_value> prev_return = std::move(return_value_);
	has_return_value_ = false;
	++current_call_depth_;

	std::optional<checked_result<void>> body_result;
	std::exception_ptr pending;
	try {
		body_result.emplace(run(f));
	} catch (...) {
		pending = std::current_exception();
	}

	--current_call_depth_;
	frames_.pop_back();

	if (yielding_) {
		// Suspend: snapshot the fiber's frame slices relative to its bases, then truncate.
		state.ip = f.ip;
		state.current_env = environment_;
		for (size_t i = stack_base; i < stack_.size(); ++i) { state.saved_stack.push_back(std::move(stack_[i])); }
		stack_.erase(stack_.begin() + stack_base, stack_.end());
		for (size_t i = try_base; i < try_records_.size(); ++i) {
			try_record tr = std::move(try_records_[i]);
			tr.owner = nullptr;
			tr.stack_size -= stack_base;
			tr.iter_size -= iter_base;
			tr.cfor_size -= cfor_base;
			state.saved_try.push_back(std::move(tr));
		}
		try_records_.erase(try_records_.begin() + try_base, try_records_.end());
		for (size_t i = iter_base; i < iter_states_.size(); ++i) { state.saved_iter.push_back(std::move(iter_states_[i])); }
		iter_states_.erase(iter_states_.begin() + iter_base, iter_states_.end());
		for (size_t i = cfor_base; i < cfor_states_.size(); ++i) { state.saved_cfor.push_back(cfor_states_[i]); }
		cfor_states_.erase(cfor_states_.begin() + cfor_base, cfor_states_.end());

		// Deref: a reference into the fiber's own locals would dangle once abandoned.
		script_value yielded = handle.last_value();
		if (yielded.is_reference()) {
			yielded = yielded.deref();
		}
		handle.set_status(coroutine_handle::status::suspended);

		yielding_ = prev_yielding;
		has_return_value_ = prev_has_return;
		return_value_ = std::move(prev_return);
		environment_ = prev_env;
		active_coroutine_ = prev_active;
		return yielded;
	}

	// Completed, errored, or threw: discard any leftover fiber frame slices.
	if (try_records_.size() > try_base) { try_records_.erase(try_records_.begin() + try_base, try_records_.end()); }
	if (iter_states_.size() > iter_base) { iter_states_.erase(iter_states_.begin() + iter_base, iter_states_.end()); }
	if (cfor_states_.size() > cfor_base) { cfor_states_.erase(cfor_states_.begin() + cfor_base, cfor_states_.end()); }
	if (stack_.size() > stack_base) { stack_.erase(stack_.begin() + stack_base, stack_.end()); }

	const bool completed_has_return = has_return_value_;
	std::optional<script_value> completed_return = std::move(return_value_);
	auto function = handle.get_function();

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
	if (!completed_has_return || !completed_return.has_value()) {
		// Falling off the end returns null and (like call_script_function) skips return-type
		// conversion, so a typed coroutine driven to completion by range-for does not error.
		return make_null();
	}
	script_value result = std::move(completed_return.value());
	if (result.is_reference()) {
		result = result.deref();
	}
	if (function && function->return_type && !function->return_type->type_name.empty() &&
	    function->return_type->type_name != "void" &&
	    function->return_type->type_name != "auto" &&
	    function->return_type->base_type != script_value_type::jai_any_type) {
		auto conv = try_convert_for_parameter(result, function->return_type);
		if (!conv) {
			return conv.error_value();
		}
		result = std::move(conv.value());
	}
	return result;
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
		}
	} else if (engine_) {
		target.engine_def = holder->type_id != UINT64_MAX
			? engine_->get_class_definition(holder->type_id)
			: engine_->get_class_definition(holder->type_name);
		target.class_def = target.engine_def.get();
	}
	return target;
}

bool vm_backend::object_to_bool_via_method(const script_value& value) {
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

std::optional<bool> vm_backend::object_equality_via_method(const script_value& left, const script_value& right) {
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
	if (!scope_env_pool_.empty()) {
		auto env = std::move(scope_env_pool_.back());
		scope_env_pool_.pop_back();
		env->reset(std::move(parent));
		return env;
	}
	return std::make_shared<environment>(std::move(parent), symbolizer_);
}

std::shared_ptr<environment> vm_backend::acquire_method_scope_env(std::shared_ptr<environment> parent, script_value this_obj) {
	if (!scope_env_pool_.empty()) {
		auto env = std::move(scope_env_pool_.back());
		scope_env_pool_.pop_back();
		env->reset_as_method(std::move(parent), std::move(this_obj));
		return env;
	}
	return std::make_shared<environment>(std::move(parent), symbolizer_, std::move(this_obj));
}

std::shared_ptr<environment> vm_backend::acquire_static_scope_env(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def) {
	if (!scope_env_pool_.empty()) {
		auto env = std::move(scope_env_pool_.back());
		scope_env_pool_.pop_back();
		env->reset_as_static_method(std::move(parent), std::move(class_def));
		return env;
	}
	return std::make_shared<environment>(std::move(parent), symbolizer_, std::move(class_def));
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
	for (size_t i = 0; i < n; ++i) {
		array.push_back(std::move(stack_[base + i]));
	}
	stack_.erase(stack_.begin() + base, stack_.end());
	stack_.push_back(std::move(arrayValue));
}

void vm_backend::exec_map(frame& f, const vm_instruction& ins) {
	script_value mapValue = script_value::make_map(nullptr, nullptr, engine_);
	auto& map = const_cast<std::map<script_value, script_value>&>(mapValue.as_map());
	const size_t n = ins.a;
	const size_t base = stack_.size() - n * 2;
	for (size_t i = 0; i < n; ++i) {
		map.insert_or_assign(std::move(stack_[base + i * 2]), std::move(stack_[base + i * 2 + 1]));
	}
	stack_.erase(stack_.begin() + base, stack_.end());
	stack_.push_back(std::move(mapValue));
}

script_value* vm_backend::resolve_local_or_env(frame& f, uint32_t slot, uint64_t symbol_id) {
	if (slot != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* ptr = f.locals->get_local(slot)) {
			return ptr;
		}
	}
	return environment_->get_value_ptr(symbol_id);
}

checked_result<void> vm_backend::define_decl_value(frame& f, uint64_t name_id, size_t slot_index, script_value value) {
	if (slot_index != SIZE_MAX && f.locals && !f.top_level) {
		f.locals->set_local(slot_index, std::move(value));
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
			return script_value(value_to_string_with_method(left) + value_to_string_with_method(right), engine_);
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
		return script_value(value_to_string_with_method(unwrapped_left) + value_to_string_with_method(unwrapped_right), engine_);
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

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
			if (b == 0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
			}
			script_int r;
			if (!ints::try_div(a, b, r)) return vm_int_overflow_sv("Integer overflow in '/'");
			return script_value(r, engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			if (rf == 0.0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
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
			return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
		}
		script_int r;
		if (!ints::try_div(a, b, r)) return vm_int_overflow_sv("Integer overflow in '/'");
		return script_value(r, engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		if (rf == 0.0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
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

	if (li_raw != script_value::TYPEID_OBJECT && li_raw != script_value::TYPEID_SHARED_PTR &&
	    ri_raw != script_value::TYPEID_OBJECT && ri_raw != script_value::TYPEID_SHARED_PTR) {
		if (li_raw == script_value::TYPEID_INT && ri_raw == script_value::TYPEID_INT) {
			const script_int b = right.unchecked_as_int();
			if (b == 0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
			}
			return script_value(ints::mod(left.unchecked_as_int(), b), engine_);
		}
		if ((li_raw == script_value::TYPEID_INT || li_raw == script_value::TYPEID_FLOAT) &&
		    (ri_raw == script_value::TYPEID_INT || ri_raw == script_value::TYPEID_FLOAT)) {
			script_float rf = (ri_raw == script_value::TYPEID_INT) ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			if (rf == 0.0) {
				return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
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
			return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
		}
		return script_value(ints::mod(unwrapped_left.unchecked_as_int(), b), engine_);
	}

	if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
	    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
		script_float rf = (ri == script_value::TYPEID_INT) ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
		if (rf == 0.0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
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

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for < operator");
}

checked_result<script_value> vm_backend::handle_less_equal(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();

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

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for <= operator");
}

checked_result<script_value> vm_backend::handle_greater(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();

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

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for > operator");
}

checked_result<script_value> vm_backend::handle_greater_equal(const script_value& left, const script_value& right) {
	const size_t li_raw = left.raw_storage_index();
	const size_t ri_raw = right.raw_storage_index();

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

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Invalid operands for >= operator");
}

checked_result<script_value> vm_backend::handle_equal(const script_value& left, const script_value& right) {
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
	if (unwrapped_left.is_char() && unwrapped_right.is_char()) return script_value(unwrapped_left.unchecked_as_char() == unwrapped_right.unchecked_as_char(), engine_);

	if (left.is_array() && right.is_array()) {
		auto& left_arr = const_cast<script_value&>(left).get_array_storage();
		auto& right_arr = const_cast<script_value&>(right).get_array_storage();
		return script_value(left_arr.get() == right_arr.get(), engine_);
	}

	if (left.is_map() && right.is_map()) {
		auto& left_map = const_cast<script_value&>(left).get_map_storage();
		auto& right_map = const_cast<script_value&>(right).get_map_storage();
		return script_value(left_map.get() == right_map.get(), engine_);
	}

	auto custom_result = object_equality_via_method(left, right);
	if (custom_result.has_value()) {
		return script_value(custom_result.value(), engine_);
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
	uint64_t op_symbol_id = 0;
	switch (op) {
		case token_type::plus: op_symbol_id = op_plus_id_; break;
		case token_type::minus: op_symbol_id = op_minus_id_; break;
		case token_type::star: op_symbol_id = op_star_id_; break;
		case token_type::slash: op_symbol_id = op_slash_id_; break;
		case token_type::percent: op_symbol_id = op_percent_id_; break;
		case token_type::less: op_symbol_id = op_less_id_; break;
		case token_type::less_equal: op_symbol_id = op_less_equal_id_; break;
		case token_type::greater: op_symbol_id = op_greater_id_; break;
		case token_type::greater_equal: op_symbol_id = op_greater_equal_id_; break;
		case token_type::equal_equal: op_symbol_id = op_equal_equal_id_; break;
		case token_type::bang_equal: op_symbol_id = op_bang_equal_id_; break;
		case token_type::spaceship: op_symbol_id = op_spaceship_id_; break;
		case token_type::ampersand: op_symbol_id = op_ampersand_id_; break;
		case token_type::pipe: op_symbol_id = op_pipe_id_; break;
		case token_type::caret: op_symbol_id = op_caret_id_; break;
		case token_type::left_shift: op_symbol_id = op_left_shift_id_; break;
		case token_type::right_shift: op_symbol_id = op_right_shift_id_; break;
		default: break;
	}

	// The interpreter's general path consults registered global operator functions
	// regardless of has_custom_numeric_ops (quirk preserved deliberately)
	if (op_symbol_id != 0 && environment_ && environment_->contains(op_symbol_id)) {
		auto op_result = environment_->get(op_symbol_id);
		if (op_result && op_result.value().is_function()) {
			script_value opFunc = std::move(op_result.value());
			const script_function& func = opFunc.as_function();
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
			out.emplace(script_value(left.unchecked_as_string() + right.unchecked_as_string(), engine_));
			return true;
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
			default: return false;
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
		return false;
	}

	return false;
}

checked_result<script_value> vm_backend::evaluate_arithmetic(const script_value& left_in, token_type op, const script_value& right_in) {
	const script_value& left = left_in.deref();
	const script_value& right = right_in.deref();

	const size_t li = left.raw_storage_index();
	const size_t ri = right.raw_storage_index();

	if (op == token_type::plus && (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING)) {
		return script_value(value_to_string_with_method(left) + value_to_string_with_method(right), engine_);
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

checked_result<script_value> vm_backend::enforce_type_compatibility(script_value value, type_info_ptr target_type) {
	if (!target_type) {
		return std::move(value);
	}
	if (target_type->base_type == script_value_type::jai_any_type) {
		return std::move(value);
	}

	auto source_type = value.type();
	auto target = target_type->base_type;

	if (source_type == target && target != script_value_type::jai_object_type) {
		return std::move(value);
	}

	if (target == script_value_type::jai_int_type) {
		if (source_type == script_value_type::jai_float_type) {
			return script_value(static_cast<script_int>(value.unchecked_as_float()), engine_);
		}
		if (source_type == script_value_type::jai_bool_type) {
			return script_value(static_cast<script_int>(value.unchecked_as_bool() ? 1 : 0), engine_);
		}
	}

	if (target == script_value_type::jai_float_type) {
		if (source_type == script_value_type::jai_int_type) {
			return script_value(static_cast<script_float>(value.unchecked_as_int()), engine_);
		}
		if (source_type == script_value_type::jai_bool_type) {
			return script_value(static_cast<script_float>(value.unchecked_as_bool() ? 1.0 : 0.0), engine_);
		}
	}

	if (target == script_value_type::jai_bool_type) {
		return script_value(is_truthy(value), engine_);
	}

	if (target == script_value_type::jai_string_type) {
		return script_value(value.to_string(), engine_);
	}

	if (source_type == script_value_type::jai_null_type) {
		if (target == script_value_type::jai_object_type ||
		    target == script_value_type::jai_shared_ptr_type ||
		    target == script_value_type::jai_weak_ptr_type) {
			script_value null_val = make_null();
			null_val.set_type_info(target_type);
			return null_val;
		}
	}

	if (target == script_value_type::jai_object_type && source_type == script_value_type::jai_object_type) {
		auto source_type_info = value.get_type_info();
		if (source_type_info && target_type) {
			const std::string& source_class_name = source_type_info->type_name;
			const std::string& target_class_name = target_type->type_name;

			if (source_class_name == target_class_name) {
				return std::move(value);
			}

			try {
				auto instance = value.as<std::shared_ptr<class_instance>>();
				if (instance) {
					class_definition* class_def = instance->get_class_definition();
					if (class_def) {
						class_definition* current = class_def;
						while (current) {
							for (const auto& parent : current->get_parent_classes()) {
								if (parent && parent->get_name() == target_class_name) {
									return std::move(value);
								}
							}
							current = current->get_parent().get();
						}
					}
				}
			} catch (...) {
			}

			auto ctor_result = environment_->get(target_class_name);
			if (ctor_result && ctor_result.value().is_function()) {
				const script_function& ctor = ctor_result.value().as_function();
				std::vector<script_value> ctor_args;
				ctor_args.push_back(value);
				try {
					auto result = ctor(ctor_args);
					if (result.has_value()) {
						return std::move(result.value());
					}
				} catch (const runtime_error& e) {
					std::string error_msg = e.what();
					if (error_msg.find("No constructor found") == std::string::npos) {
						return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Constructor conversion failed", target_type->id);
					}
				}
			}

			return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Incompatible class types", target_type->id);
		}
	}

	if (target == script_value_type::jai_object_type && target_type && !target_type->type_name.empty()) {
		bool is_primitive = (source_type == script_value_type::jai_int_type ||
		                     source_type == script_value_type::jai_float_type ||
		                     source_type == script_value_type::jai_string_type ||
		                     source_type == script_value_type::jai_bool_type ||
		                     source_type == script_value_type::jai_char_type);
		if (is_primitive) {
			auto ctor_result = environment_->get(target_type->type_name);
			if (ctor_result && ctor_result.value().is_function()) {
				const script_function& ctor = ctor_result.value().as_function();
				std::vector<script_value> ctor_args;
				ctor_args.push_back(value);
				try {
					auto result = ctor(ctor_args);
					if (result.has_value()) {
						return std::move(result.value());
					}
				} catch (const runtime_error& e) {
					std::string error_msg = e.what();
					if (error_msg.find("No constructor found") == std::string::npos) {
						return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Primitive conversion failed", target_type->id);
					}
				}
			}
		}
	}

	return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "Type mismatch in assignment", target_type ? target_type->id : 0);
}

checked_result<script_value> vm_backend::try_convert_for_parameter(const script_value& arg, type_info_ptr target_type) {
	if (!target_type) {
		return arg;
	}

	const script_value& derefed_arg = arg.deref();
	auto source_type = derefed_arg.storage_type();
	if (source_type == script_value_type::jai_reference_type) {
		auto ref_holder = derefed_arg.get_reference_holder();
		if (ref_holder && ref_holder->target) {
			source_type = ref_holder->target->storage_type();
		}
	}
	auto target_base_type = target_type->base_type;

	if (target_base_type == script_value_type::jai_any_type) return arg;

	if (source_type == target_base_type) {
		if (source_type == script_value_type::jai_object_type) {
			auto source_type_info = derefed_arg.get_type_info();
			if (source_type_info && !source_type_info->type_name.empty() &&
			    source_type_info->type_name == target_type->type_name) {
				return derefed_arg;
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
		} else {
			return arg;
		}
	}

	if (source_type == script_value_type::jai_null_type &&
	    target_base_type == script_value_type::jai_object_type) {
		return arg;
	}

	auto source_type_info = derefed_arg.get_type_info();
	if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type) {
		if (target_base_type == script_value_type::jai_shared_ptr_type) {
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
		if (target_base_type == script_value_type::jai_object_type) {
			auto resolved = resolve_member_target(derefed_arg);
			if (resolved) {
				if (resolved.class_name() == target_type->type_name) {
					return arg;
				}
				if (resolved.class_def && resolved.class_def->is_subtype_of(target_type->type_name)) {
					return arg;
				}
			}
			if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
				return arg;
			}
		}
	}

	if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty()) {
		const std::string& target_class_name = target_type->type_name;

		if (engine_) {
			auto class_def = engine_->get_class_definition(target_class_name);
			if (class_def) {
				auto script_class = std::dynamic_pointer_cast<script_class_definition>(class_def);
				if (script_class) {
					const auto& ctor_asts = script_class->get_constructor_asts();
					bool has_matching_ctor = false;

					std::string source_type_name;
					class_definition* source_class_def = nullptr;
					if (source_type == script_value_type::jai_object_type) {
						auto resolved = resolve_member_target(derefed_arg);
						if (resolved) {
							source_type_name = resolved.class_name();
							source_class_def = resolved.class_def;
						}
					}

					for (const auto& ctor_ast : ctor_asts) {
						if (ctor_ast->parameters.size() != 1) continue;
						const auto& param = ctor_ast->parameters[0];
						if (!param.type || param.type->type_name.empty()) {
							has_matching_ctor = true;
							break;
						}
						if (source_type == script_value_type::jai_object_type) {
							if (param.type->type_name == source_type_name) {
								has_matching_ctor = true;
								break;
							}
							if (source_class_def && source_class_def->is_subtype_of(param.type->type_name)) {
								has_matching_ctor = true;
								break;
							}
						} else {
							if (param.type->base_type == source_type ||
							    (source_type == script_value_type::jai_int_type &&
							     param.type->base_type == script_value_type::jai_float_type) ||
							    (source_type == script_value_type::jai_float_type &&
							     param.type->base_type == script_value_type::jai_int_type)) {
								has_matching_ctor = true;
								break;
							}
						}
					}

					if (!has_matching_ctor) {
						return checked_result<script_value>(
							make_error_code(runtime_error_code::type_mismatch),
							"Cannot convert {0} to {1} (no suitable single-argument constructor)",
							derefed_arg.type_id(), target_type->id);
					}
				}
			}
		}

		auto global = engine_ ? engine_->get_global_environment() : nullptr;
		auto ctor_result = global ? global->get(target_class_name) : environment_->get(target_class_name);
		if (ctor_result && ctor_result.value().is_function()) {
			const script_function& ctor = ctor_result.value().as_function();
			std::vector<script_value> ctor_args;
			ctor_args.push_back(arg);
			auto result = ctor(ctor_args);
			if (result.has_value()) {
				return std::move(result.value());
			}
		}

		return checked_result<script_value>(
			make_error_code(runtime_error_code::type_mismatch),
			"Cannot convert {0} to {1} (no suitable single-argument constructor)",
			derefed_arg.type_id(), target_type->id);
	}

	if (source_type == script_value_type::jai_int_type &&
	    target_base_type == script_value_type::jai_float_type) {
		return script_value(static_cast<script_float>(derefed_arg.unchecked_as_int()), engine_);
	}
	if (source_type == script_value_type::jai_float_type &&
	    target_base_type == script_value_type::jai_int_type) {
		return script_value(static_cast<script_int>(derefed_arg.unchecked_as_float()), engine_);
	}

	if (source_type == script_value_type::jai_object_type) {
		std::string method_name;
		if (target_base_type == script_value_type::jai_int_type) {
			method_name = "to_int";
		} else if (target_base_type == script_value_type::jai_float_type) {
			method_name = "to_float";
		} else if (target_base_type == script_value_type::jai_string_type) {
			method_name = "to_string";
		} else if (target_base_type == script_value_type::jai_bool_type) {
			method_name = "to_bool";
		} else if (target_base_type == script_value_type::jai_char_type) {
			method_name = "to_char";
		}

		if (!method_name.empty()) {
			auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
			if (instance) {
				auto method_id = symbolizer_->intern(method_name);
				auto method_val = instance->get_method(method_id, false);
				if (!method_val.is_null() && !method_val.is_invalid() && method_val.is_function()) {
					script_value bound = make_bound_method(arg, method_val);
					const script_function& method = bound.as_function();
					std::vector<script_value> no_args;
					auto result = method(no_args);
					if (result.has_value()) {
						return std::move(result.value());
					}
				}
			}
		}
	}

	return checked_result<script_value>(
		make_error_code(runtime_error_code::type_mismatch),
		"Type mismatch: expected {0} but got {1}",
		target_type->id, derefed_arg.type_id());
}

// ============================================================
// Opcode handlers
// ============================================================

checked_result<void> vm_backend::exec_load(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.b];
	if (current_catch_var_id_ != 0 && sym == current_catch_var_id_) {
		stack_.push_back(active_exception_value_.has_value() ? active_exception_value_.value() : make_null());
		return {};
	}
	if (ins.a != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* local = f.locals->get_local(ins.a)) {
			stack_.push_back(local->deref());
			return {};
		}
	}
	if (ins.c & load_flag_type_ctor) {
		std::string_view name = symbolizer_->get_string(sym);
		size_t pos = name.find('<');
		std::string base_type(name.substr(0, pos));
		auto ctor_result = environment_->get(base_type);
		if (ctor_result && ctor_result.value().is_function()) {
			stack_.push_back(std::move(ctor_result.value()));
			return {};
		}
	}
	auto ref_result = environment_->get_ref(sym);
	if (ref_result) {
		const script_value& val = ref_result.value().get();
		stack_.push_back(val.deref());
		return {};
	}
	auto this_result = environment_->get(this_id_);
	if (this_result) {
		script_value this_val = std::move(this_result.value());
		if (this_val.is_object()) {
			std::shared_ptr<class_instance> instance = this_val.get_class_instance();
			if (instance) {
				if (instance->has_field(sym)) {
					stack_.push_back(instance->get_field(sym));
					return {};
				}
				script_value method = instance->get_method(sym, false);
				if (!method.is_invalid()) {
					stack_.push_back(make_bound_method(this_val, method));
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
	return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
		"Undefined variable '{0}'", sym);
}

checked_result<void> vm_backend::exec_store(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.a];
	const bool rhs_lvalue = (ins.c & store_flag_rhs_lvalue) != 0;
	script_value value = std::move(stack_.back());
	stack_.pop_back();

	if (ins.b != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* frameLocal = f.locals->get_local(ins.b)) {
			if (frameLocal->is_reference()) {
				frameLocal->deref() = std::move(value.deref().clone());
			} else {
				*frameLocal = std::move(value.clone());
			}
			stack_.push_back(std::move(value));
			return {};
		}
	}

	if (environment_->contains(sym)) {
		script_value* currentVal = environment_->get_value_ptr(sym);
		if (currentVal && currentVal->is_reference()) {
			currentVal->deref() = std::move(value.deref().clone());
			stack_.push_back(std::move(value));
			return {};
		}
		if (currentVal && currentVal->is_cpp_bound()) {
			currentVal->assign_through(value);
			stack_.push_back(std::move(value));
			return {};
		}
		if (currentVal && currentVal->is_weak_ptr()) {
			script_value result = value;
			if (value.is_null()) {
				auto type_info = currentVal->get_type_info();
				JAISCRIPT_TRY(environment_->assign(sym, script_value::make_empty_weak_ptr(type_info, engine_)));
			} else if (value.is_weak_ptr()) {
				JAISCRIPT_TRY(environment_->assign(sym, std::move(value)));
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
							uint64_t expected_id = expected_type->id;
							uint64_t actual_id = value_type_info->element_type()
								? value_type_info->element_type()->id
								: value_type_info->id;
							return checked_result<void>(
								make_error_code(runtime_error_code::type_mismatch),
								"Cannot assign shared_ptr<{}> to weak_ptr<{}>: type must match or be a subclass",
								actual_id, expected_id);
						}
					}
				}
				auto weak_result = script_value::make_weak_ptr(value, engine_);
				if (!weak_result) {
					return weak_result.error_value();
				}
				JAISCRIPT_TRY(environment_->assign(sym, std::move(weak_result.value())));
			} else if (value.type() == script_value_type::jai_object_type) {
				auto type_info = currentVal->get_type_info();
				uint64_t weak_type_id = (type_info && !type_info->type_params.empty())
					? type_info->type_params[0]->id : 0;
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Cannot assign value-semantic object to weak_ptr<{}>: use shared_ptr<T>",
					weak_type_id);
			} else {
				auto type_info = value.get_type_info();
				uint64_t actual_type_id = type_info ? type_info->id : 0;
				auto weak_type_info = currentVal->get_type_info();
				uint64_t weak_type_id = (weak_type_info && !weak_type_info->type_params.empty())
					? weak_type_info->type_params[0]->id : 0;
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Cannot assign {} to weak_ptr<{}>: use shared_ptr<T>",
					actual_type_id, weak_type_id);
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

			if (value.is_null()) {
				JAISCRIPT_TRY(environment_->assign(sym, std::move(value)));
			} else if (value.is_weak_ptr()) {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Cannot assign weak_ptr to shared_ptr - use weak.lock() instead");
			} else if (value.get_type_info() &&
			           value.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
				auto value_type_info = value.get_type_info();
				auto value_element = value_type_info->element_type();
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
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
						"Cannot assign shared_ptr<{0}> to shared_ptr<{1}>", actual_id, expected_id);
				}

				value.set_type_info(ptr_type_info);
				JAISCRIPT_TRY(environment_->assign(sym, std::move(value)));
			} else {
				auto holder = currentVal->get_object_holder();
				if (!holder || !holder->data) {
					return checked_result<void>(make_error_code(runtime_error_code::invalid_reference),
						"Cannot assign to null shared_ptr");
				}

				auto instance = currentVal->get_class_instance();
				if (!instance) {
					auto type_info = value.get_type_info();
					uint64_t type_id = type_info ? type_info->id : 0;
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
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
					script_value method = instance->get_method(assign_operator_id_, false);
					if (method.is_function()) {
						const script_function& func = method.as_function();
						std::vector<script_value> args;
						args.push_back(*currentVal);
						args.push_back(std::move(value));
						auto call_result = func(args);
						if (!call_result) {
							return checked_result<void>(call_result.error(), "operator= failed");
						}
					} else {
						auto type_info = value.get_type_info();
						uint64_t type_id = type_info ? type_info->id : 0;
						uint64_t expected_id = expected_type ? expected_type->id : 0;
						return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
							"Cannot assign {0} to shared_ptr<{1}>: no operator=({0}) defined", type_id, expected_id);
					}
				}
			}
			stack_.push_back(std::move(result));
			return {};
		}

		// Regular variable assignment with strong-types enforcement
		type_info_ptr target_type = currentVal ? currentVal->get_type_info() : nullptr;

		if (target_type && target_type->base_type == script_value_type::jai_object_type) {
			auto source_type_info = value.get_type_info();
			std::string source_type_name = source_type_info ? source_type_info->type_name : "unknown";
			if (source_type_name != target_type->type_name && currentVal) {
				auto instance_result = currentVal->checked_as<std::shared_ptr<class_instance>>();
				if (instance_result) {
					auto instance = instance_result.value();
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
						return checked_result<void>(call_result.error(), "operator= failed");
					}
				}
			}
		}

		auto enforced = enforce_type_compatibility(std::move(value), target_type);
		if (!enforced) {
			return enforced.error_value();
		}
		value = std::move(enforced.value());

		if (target_type && target_type->base_type == script_value_type::jai_any_type) {
			value.set_type_info(target_type);
		}

		if (rhs_lvalue) {
			script_value assignValue = value.clone();
			JAISCRIPT_TRY(environment_->assign(sym, std::move(assignValue)));
			stack_.push_back(std::move(value));
		} else {
			JAISCRIPT_TRY(environment_->assign(sym, std::move(value)));
			script_value* stored = environment_->get_value_ptr(sym);
			stack_.push_back(stored ? *stored : make_null());
		}
		return {};
	}

	// Variable not in environment: this-field / static-field fallback, then assign
	bool assigned_to_member = false;
	auto this_result = environment_->get(this_id_);
	if (this_result) {
		script_value this_val = std::move(this_result.value());
		if (this_val.is_object()) {
			auto obj_holder = this_val.get_object_holder();
			if (obj_holder && obj_holder->is_class_instance_wrapper) {
				auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
				if (instance->has_field(sym)) {
					auto class_def = instance->get_class_definition();
					if (class_def) {
						auto cpp_base = class_def->get_cpp_base_class();
						if (cpp_base) {
							uint64_t setter_id = cpp_base->get_property_setter_id(sym);
							if (setter_id != 0) {
								auto setter = cpp_base->get_method(setter_id, false);
								if (setter.is_function()) {
									std::vector<script_value> args = {this_val, value};
									auto result = setter.as_function()(args);
									if (!result) {
										return result.error_value();
									}
									assigned_to_member = true;
								}
							}
						}
					}
					if (!assigned_to_member) {
						if (rhs_lvalue) {
							instance->set_field(sym, clone_for_assignment(value));
						} else {
							instance->set_field(sym, std::move(value));
							value = instance->get_field(sym);
						}
						assigned_to_member = true;
					}
				} else {
					auto class_def = instance->get_class_definition();
					if (class_def) {
						if (rhs_lvalue) {
							if (class_def->set_static_field(sym, value.clone())) {
								assigned_to_member = true;
							}
						} else {
							if (class_def->set_static_field(sym, std::move(value))) {
								value = class_def->get_static_field(sym);
								assigned_to_member = true;
							}
						}
					}
				}
			}
		}
	}

	if (!assigned_to_member) {
		if (rhs_lvalue) {
			JAISCRIPT_TRY(environment_->assign(sym, value.clone()));
		} else {
			JAISCRIPT_TRY(environment_->assign(sym, std::move(value)));
			script_value* stored = environment_->get_value_ptr(sym);
			value = stored ? *stored : make_null();
		}
	}
	stack_.push_back(std::move(value));
	return {};
}

checked_result<void> vm_backend::exec_compound_store(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.a];
	const uint32_t kind = ins.c & compound_kind_mask;
	const bool result_needed = (ins.c & compound_flag_result_needed) != 0;
	const bool no_result = (ins.c & compound_flag_no_result) != 0;
	script_value rightValue = std::move(stack_.back());
	stack_.pop_back();

	script_value* varPtr = resolve_local_or_env(f, ins.b, sym);
	if (varPtr) {
		script_value& target = varPtr->deref();
		auto leftType = target.type();
		script_value& derefRight = rightValue.deref();

		if (has_custom_numeric_ops_) [[unlikely]] {
			const char* opName = nullptr;
			switch (kind) {
				case compound_plus: opName = "+"; break;
				case compound_minus: opName = "-"; break;
				case compound_star: opName = "*"; break;
				default: break;
			}
			if (opName && environment_->contains(opName)) {
				auto op_result = environment_->get(opName);
				if (op_result && op_result.value().is_function()) {
					script_value opFunc = std::move(op_result.value());
					const script_function& func = opFunc.as_function();
					std::vector<script_value> args = {target.clone(), rightValue};
					auto result = func(args);
					if (!result) {
						return result.error_value();
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

		const size_t leftIdx = target.raw_storage_index();
		const size_t rightIdx = derefRight.raw_storage_index();
		const bool bothInt = leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_INT;
		switch (kind) {
			case compound_plus: {
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_add(tref, derefRight.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '+='");
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() += derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					target = script_value(target.unchecked_as_int() + derefRight.unchecked_as_float(), engine_);
				} else if (leftIdx == script_value::TYPEID_STRING && rightIdx == script_value::TYPEID_STRING) {
					target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
				} else {
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_minus: {
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_sub(tref, derefRight.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '-='");
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() -= derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					target = script_value(target.unchecked_as_int() - derefRight.unchecked_as_float(), engine_);
				} else {
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_star: {
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_mul(tref, derefRight.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '*='");
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() *= derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					target = script_value(target.unchecked_as_int() * derefRight.unchecked_as_float(), engine_);
				} else {
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			case compound_slash: {
				if (rightIdx == script_value::TYPEID_INT && derefRight.unchecked_as_int() == 0) {
					return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
				}
				if (rightIdx == script_value::TYPEID_FLOAT && derefRight.unchecked_as_float() == 0.0) {
					return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
				}
				if (bothInt) {
					script_int& tref = target.unchecked_as_int_ref();
					script_int rr;
					if (!ints::try_div(tref, derefRight.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '/='");
					tref = rr;
				} else if (leftIdx == script_value::TYPEID_FLOAT) {
					target.unchecked_as_float_ref() /= derefRight.as_float();
				} else if (leftIdx == script_value::TYPEID_INT && rightIdx == script_value::TYPEID_FLOAT) {
					target = script_value(target.unchecked_as_int() / derefRight.unchecked_as_float(), engine_);
				} else {
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
				}
				break;
			}
			default:
				// %= matches the interpreter's in-place switch, which has no percent case
				return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
		}

		if (!no_result) { stack_.push_back(result_needed ? target.clone() : target); }
		return {};
	}

	// Implicit this.member fallback
	auto this_result = environment_->get(this_id_);
	if (!this_result || !this_result.value().is_object()) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}' (no 'this' in scope)", sym);
	}

	script_value this_val = std::move(this_result.value());
	std::shared_ptr<class_instance> instance = this_val.get_class_instance();

	if (!instance || !instance->has_field(sym)) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
			"Undefined variable '{0}' (not a field of 'this')", sym);
	}

	script_value currentValue = instance->get_field(sym);

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
				instance->set_field(sym, clone_for_assignment(custom_result.value()));
				if (!no_result) { stack_.push_back(std::move(custom_result.value())); }
				return {};
			}
		}
	}

	const size_t ci = currentValue.raw_storage_index();
	const size_t ri = rightValue.raw_storage_index();
	script_value resultValue = make_null();
	switch (kind) {
		case compound_plus:
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_add(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '+='");
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf + rf, engine_);
			} else if (ci == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
				resultValue = script_value(currentValue.unchecked_as_string() + rightValue.unchecked_as_string(), engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_minus:
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_sub(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '-='");
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf - rf, engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_star:
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_mul(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '*='");
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf * rf, engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		case compound_slash:
			if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
				return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
			}
			if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf / rf, engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
			}
			break;
		default:
			return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
	}

	instance->set_field(sym, clone_for_assignment(resultValue));
	if (!no_result) { stack_.push_back(std::move(resultValue)); }
	return {};
}

checked_result<void> vm_backend::exec_incdec(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.a];
	const bool postfix = (ins.c & incdec_flag_postfix) != 0;
	const bool isIncrement = (ins.c & incdec_flag_increment) != 0;

	script_value* varPtr = resolve_local_or_env(f, ins.b, sym);
	if (varPtr) {
		script_value& target = varPtr->deref();
		switch (target.type()) {
			case script_value_type::jai_int_type: {
				if (postfix) {
					stack_.push_back(script_value(target.unchecked_as_int(), engine_));
					if (isIncrement) ++target.unchecked_as_int_ref();
					else --target.unchecked_as_int_ref();
				} else {
					if (isIncrement) ++target.unchecked_as_int_ref();
					else --target.unchecked_as_int_ref();
					stack_.push_back(script_value(target.unchecked_as_int(), engine_));
				}
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
				return {};
			}
			default:
				return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
		}
	}

	auto this_result = environment_->get(this_id_);
	if (this_result && this_result.value().is_object()) {
		script_value this_val = std::move(this_result.value());
		std::shared_ptr<class_instance> instance = this_val.get_class_instance();
		if (instance && instance->has_field(sym)) {
			script_value currentVal = instance->get_field(sym);
			const size_t ti = currentVal.raw_storage_index();
			if (ti == script_value::TYPEID_INT) {
				script_int oldVal = currentVal.unchecked_as_int();
				script_int newVal = isIncrement ? oldVal + 1 : oldVal - 1;
				instance->set_field(sym, script_value(newVal, engine_));
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			} else if (ti == script_value::TYPEID_FLOAT) {
				script_float oldVal = currentVal.unchecked_as_float();
				script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
				instance->set_field(sym, script_value(newVal, engine_));
				stack_.push_back(script_value(postfix ? oldVal : newVal, engine_));
				return {};
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
			}
		}
	}
	return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
		"Undefined variable '{0}'", sym);
}

checked_result<void> vm_backend::exec_unary(frame& f, const vm_instruction& ins) {
	script_value operand = std::move(stack_.back());
	stack_.pop_back();

	const size_t oi = operand.raw_storage_index();
	switch (static_cast<token_type>(ins.a)) {
		case token_type::minus: {
			if (oi == script_value::TYPEID_INT) {
				script_int neg;
				if (!ints::try_neg(operand.unchecked_as_int(), neg)) {
					return vm_int_overflow_v("Integer overflow in unary '-'");
				}
				stack_.push_back(script_value(neg, engine_));
			} else if (oi == script_value::TYPEID_FLOAT) {
				stack_.push_back(script_value(-operand.unchecked_as_float(), engine_));
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), "Unary minus requires numeric operand");
			}
			return {};
		}
		case token_type::bang:
			stack_.push_back(script_value(!is_truthy(operand), engine_));
			return {};
		case token_type::tilde:
			if (oi != script_value::TYPEID_INT) {
				return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand), "Bitwise NOT requires integer operand");
			}
			stack_.push_back(script_value(~operand.unchecked_as_int(), engine_));
			return {};
		default:
			return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
	}
}

checked_result<void> vm_backend::exec_binary(frame& f, const vm_instruction& ins) {
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
				return fast->error_value();
			}
			stack_.push_back(std::move(fast->value()));
			return {};
		}
	}

	auto result = binary_general(op, left, right);
	if (!result) {
		return result.error_value();
	}
	stack_.push_back(std::move(result.value()));
	return {};
}

checked_result<const script_value*> vm_backend::fused_ident_value(frame& f, const fused_operand& operand,
                                                                  std::optional<script_value>& scratch) {
	const uint64_t sym = f.code->symbols[operand.symbol];
	if (current_catch_var_id_ != 0 && sym == current_catch_var_id_) {
		scratch.emplace(active_exception_value_.has_value() ? active_exception_value_.value() : make_null());
		return &scratch.value();
	}
	if (operand.slot != k_invalid_u32 && f.locals && !f.top_level) {
		if (auto* local = f.locals->get_local(operand.slot)) {
			return &local->deref();
		}
	}
	if (operand.load_flags & load_flag_type_ctor) {
		std::string_view name = symbolizer_->get_string(sym);
		size_t pos = name.find('<');
		std::string base_type(name.substr(0, pos));
		auto ctor_result = environment_->get(base_type);
		if (ctor_result && ctor_result.value().is_function()) {
			scratch.emplace(std::move(ctor_result.value()));
			return &scratch.value();
		}
	}
	auto ref_result = environment_->get_ref(sym);
	if (ref_result) {
		return &ref_result.value().get().deref();
	}
	auto this_result = environment_->get(this_id_);
	if (this_result) {
		script_value this_val = std::move(this_result.value());
		if (this_val.is_object()) {
			std::shared_ptr<class_instance> instance = this_val.get_class_instance();
			if (instance) {
				if (instance->has_field(sym)) {
					scratch.emplace(instance->get_field(sym));
					return &scratch.value();
				}
				script_value method = instance->get_method(sym, false);
				if (!method.is_invalid()) {
					scratch.emplace(make_bound_method(this_val, method));
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
	return checked_result<const script_value*>(make_error_code(runtime_error_code::undefined_variable),
		"Undefined variable '{0}'", sym);
}

checked_result<void> vm_backend::exec_binary_fused(frame& f, const vm_instruction& ins) {
	const fused_binary_proto& p = f.code->fused_binary_protos[ins.a];
	const token_type op = static_cast<token_type>(p.op);

	std::optional<script_value> lscratch, rscratch;
	const script_value* lp;
	const script_value* rp;

	if (p.left.const_index != k_invalid_u32) {
		lp = &f.code->constants[p.left.const_index];   // engine-less template: fast path reads raw
	} else {
		auto resolved = fused_ident_value(f, p.left, lscratch);
		if (!resolved) return resolved.error_value();
		lp = resolved.value();
	}
	if (p.right.const_index != k_invalid_u32) {
		rp = &f.code->constants[p.right.const_index];
	} else {
		auto resolved = fused_ident_value(f, p.right, rscratch);
		if (!resolved) return resolved.error_value();
		rp = resolved.value();
	}

	// Fast path: mirrors binary_fast_shape without materializing operand loads
	if (!has_custom_numeric_ops_ && is_numeric_binary_op(op)) {
		const size_t li = lp->raw_storage_index();
		const size_t ri = rp->raw_storage_index();
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
			const script_int a = lp->unchecked_as_int(), b = rp->unchecked_as_int();
			switch (op) {
			case token_type::plus: { script_int rr; if (!ints::try_add(a, b, rr)) return vm_int_overflow_v("Integer overflow in '+'"); stack_.push_back(script_value(rr, engine_)); return {}; }
			case token_type::minus: { script_int rr; if (!ints::try_sub(a, b, rr)) return vm_int_overflow_v("Integer overflow in '-'"); stack_.push_back(script_value(rr, engine_)); return {}; }
			case token_type::star: { script_int rr; if (!ints::try_mul(a, b, rr)) return vm_int_overflow_v("Integer overflow in '*'"); stack_.push_back(script_value(rr, engine_)); return {}; }
			case token_type::slash:
				if (b == 0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in integer operation");
				{ script_int rr; if (!ints::try_div(a, b, rr)) return vm_int_overflow_v("Integer overflow in '/'"); stack_.push_back(script_value(rr, engine_)); return {}; }
			case token_type::percent:
				if (b == 0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in integer operation");
				stack_.push_back(script_value(ints::mod(a, b), engine_)); return {};
			case token_type::less: stack_.push_back(script_value(a < b, engine_)); return {};
			case token_type::less_equal: stack_.push_back(script_value(a <= b, engine_)); return {};
			case token_type::greater: stack_.push_back(script_value(a > b, engine_)); return {};
			case token_type::greater_equal: stack_.push_back(script_value(a >= b, engine_)); return {};
			case token_type::equal_equal: stack_.push_back(script_value(a == b, engine_)); return {};
			case token_type::bang_equal: stack_.push_back(script_value(a != b, engine_)); return {};
			default: break;
			}
		} else if ((li == script_value::TYPEID_INT || li == script_value::TYPEID_FLOAT) &&
		           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
			const script_float a = li == script_value::TYPEID_INT ? static_cast<script_float>(lp->unchecked_as_int()) : lp->unchecked_as_float();
			const script_float b = ri == script_value::TYPEID_INT ? static_cast<script_float>(rp->unchecked_as_int()) : rp->unchecked_as_float();
			switch (op) {
			case token_type::plus: stack_.push_back(script_value(a + b, engine_)); return {};
			case token_type::minus: stack_.push_back(script_value(a - b, engine_)); return {};
			case token_type::star: stack_.push_back(script_value(a * b, engine_)); return {};
			case token_type::slash:
				if (b == 0.0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero in float operation");
				stack_.push_back(script_value(a / b, engine_)); return {};
			case token_type::percent:
				if (b == 0.0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero), "Modulo by zero in float operation");
				stack_.push_back(script_value(std::fmod(a, b), engine_)); return {};
			case token_type::less: stack_.push_back(script_value(a < b, engine_)); return {};
			case token_type::less_equal: stack_.push_back(script_value(a <= b, engine_)); return {};
			case token_type::greater: stack_.push_back(script_value(a > b, engine_)); return {};
			case token_type::greater_equal: stack_.push_back(script_value(a >= b, engine_)); return {};
			case token_type::equal_equal: stack_.push_back(script_value(a == b, engine_)); return {};
			case token_type::bang_equal: stack_.push_back(script_value(a != b, engine_)); return {};
			default: break;
			}
		} else if (op == token_type::plus && li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING &&
		           !lp->is_cpp_bound() && !rp->is_cpp_bound()) {
			// unchecked_as_string does NOT decode cpp_bound (unlike the int/float reads)
			stack_.push_back(script_value(lp->unchecked_as_string() + rp->unchecked_as_string(), engine_));
			return {};
		}
	}

	// Fallback: snapshot the operands (isolation from mutation by nested calls) and
	// give constant templates their engine ref, exactly as op_const would have
	script_value left = *lp;
	if (!left.has_valid_engine()) left.set_engine(engine_);
	script_value right = *rp;
	if (!right.has_valid_engine()) right.set_engine(engine_);
	auto result = binary_general(op, left.deref(), right.deref());
	if (!result) {
		return result.error_value();
	}
	stack_.push_back(std::move(result.value()));
	return {};
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

checked_result<void> vm_backend::exec_cfor_prep(frame& f, const vm_instruction& ins) {
	const counted_for_proto& p = f.code->counted_for_protos[ins.a];
	counted_for_state st;
	st.cmp = p.cmp;
	st.subtract = p.step_subtract;

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

checked_result<void> vm_backend::exec_cfor_back(frame& f, const vm_instruction& ins) {
	const counted_for_proto& p = f.code->counted_for_protos[ins.a];
	if (cfor_states_.empty()) {
		return checked_result<void>(make_error_code(runtime_error_code::internal_error), "counted-for state stack underflow");
	}
	counted_for_state& st = cfor_states_.back();

	if (execution_budget_exhausted()) [[unlikely]] {
		return budget_exceeded_error();
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
		if (!ints::try_sub(i, step, next)) return vm_int_overflow_v("Integer overflow in '-='");
	} else {
		if (!ints::try_add(i, step, next)) return vm_int_overflow_v("Integer overflow in '+='");
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

checked_result<void> vm_backend::exec_index(frame& f, const vm_instruction& ins) {
	const bool lvalue_shape = (ins.a & index_flag_lvalue_shape) != 0;
	const bool lvalue_write = (ins.a & index_flag_lvalue_write) != 0;

	script_value right = std::move(stack_.back());
	stack_.pop_back();
	right = right.deref();
	script_value left_raw = std::move(stack_.back());
	stack_.pop_back();
	script_value& left = left_raw.deref();

	if (left.is_array()) {
		if (!right.is_int()) {
			return checked_result<void>(make_error_code(runtime_error_code::invalid_index_type), "Array index must be an integer");
		}
		script_int index = right.unchecked_as_int();
		const auto& array = left.unchecked_as_array();

		if (index < 0 || index >= static_cast<script_int>(array.size())) {
			return checked_result<void>(make_error_code(runtime_error_code::index_out_of_bounds),
				"Array index {0} out of bounds for array of size {1}",
				static_cast<uint64_t>(index), static_cast<uint64_t>(array.size()));
		}

		if (lvalue_shape) {
			auto array_type_info = left.get_type_info();
			type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
			script_value ref_value = script_value::make_element_reference(
				left.get_array_storage(), static_cast<size_t>(index), environment_, engine_, element_type);
			stack_.push_back(std::move(ref_value));
		} else {
			stack_.push_back(array[index]);
		}
		return {};
	}

	if (left.is_map()) {
		try {
			auto& map = const_cast<std::map<script_value, script_value>&>(left.as_map());

			if (lvalue_shape && lvalue_write) {
				// Auto-insert so the assignment has somewhere to write through
				script_value key = right;
				if (!key.has_valid_engine()) {
					key.set_engine(left.has_valid_engine() ? left.get_engine() : engine_);
				}
				script_value& value_ref = map[key];
				if (!value_ref.has_valid_engine()) {
					if (!left.has_valid_engine()) {
						return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
							"Invalid script_value: both map and new entry missing engine reference");
					}
					value_ref.set_engine(left.get_engine());
				}
				script_value* element_ptr = &value_ref;
				auto map_type_info = left.get_type_info();
				type_info_ptr value_type = map_type_info ? map_type_info->value_type() : nullptr;
				script_value ref_value = script_value::make_reference(element_ptr, environment_, engine_, value_type);
				stack_.push_back(std::move(ref_value));
			} else if (lvalue_shape) {
				// Read: reference the existing entry, never insert
				auto it = map.find(right);
				if (it != map.end()) {
					script_value& value_ref = const_cast<script_value&>(it->second);
					if (!value_ref.has_valid_engine()) {
						value_ref.set_engine(left.has_valid_engine() ? left.get_engine() : engine_);
					}
					auto map_type_info = left.get_type_info();
					type_info_ptr value_type = map_type_info ? map_type_info->value_type() : nullptr;
					stack_.push_back(script_value::make_reference(&value_ref, environment_, engine_, value_type));
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

	if (left.is_object()) {
		auto instance_result = left.checked_as<std::shared_ptr<class_instance>>();
		if (instance_result) {
			auto instance = instance_result.value();
			script_value method = instance->get_method(subscript_op_id_, false);
			if (method.is_function()) {
				const script_function& func = method.as_function();
				std::vector<script_value> args = {left, right};
				auto result = func(args);
				if (!result) {
					return result.error_value();
				}
				stack_.push_back(std::move(result.value()));
				return {};
			}
		}
		auto method_result = environment_->get("[]");
		if (method_result && method_result.value().is_function()) {
			script_value getMethod = std::move(method_result.value());
			const script_function& func = getMethod.as_function();
			std::vector<script_value> args = {left, right};
			auto result = func(args);
			if (!result) {
				return result.error_value();
			}
			stack_.push_back(std::move(result.value()));
			return {};
		}
	}
	return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
		"Subscript can only be used on arrays, maps, or types with [] operator");
}

checked_result<void> vm_backend::exec_index_assign(frame& f, const vm_instruction& ins) {
	script_value target_ref = std::move(stack_.back());
	stack_.pop_back();
	script_value value = std::move(stack_.back());
	stack_.pop_back();

	if (!target_ref.is_reference()) {
		return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target), "Cannot assign to rvalue expression");
	}

	auto refHolder = target_ref.get_reference_holder();
	script_value* target_ptr = refHolder->target;
	if (!target_ptr) {
		return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
	}

	type_info_ptr element_type = refHolder->container_element_type;
	if (element_type) {
		if (!vm_is_element_type_compatible(value, element_type, *target_ptr)) {
			std::string value_type = vm_value_type_name(value);
			std::string expected_type = vm_type_info_name(element_type);
			uint64_t value_type_id = symbolizer_->intern(value_type);
			uint64_t expected_type_id = symbolizer_->intern(expected_type);
			return checked_result<void>(
				make_error_code(runtime_error_code::array_element_type_mismatch),
				"Cannot assign '{0}' to element of type '{1}'",
				value_type_id, expected_type_id);
		}
		script_value converted = vm_convert_array_element(engine_, value, element_type);
		*target_ptr = std::move(converted);
	} else {
		*target_ptr = std::move(value.clone());
	}
	stack_.push_back(std::move(value));
	return {};
}

checked_result<void> vm_backend::exec_index_compound(frame& f, const vm_instruction& ins) {
	script_value rightValue = std::move(stack_.back());
	stack_.pop_back();
	script_value currentValue = std::move(stack_.back());
	stack_.pop_back();

	token_type op;
	const char* opName;
	switch (ins.a & compound_kind_mask) {
		case compound_plus: op = token_type::plus; opName = "+"; break;
		case compound_minus: op = token_type::minus; opName = "-"; break;
		case compound_star: op = token_type::star; opName = "*"; break;
		case compound_slash: op = token_type::slash; opName = "/"; break;
		case compound_percent: op = token_type::percent; opName = "%"; break;
		default: op = token_type::plus; opName = "+"; break;
	}

	script_value resultValue = make_null();
	auto op_result = environment_->get(opName);
	if (op_result && op_result.value().is_function()) {
		script_value opFunc = std::move(op_result.value());
		const script_function& func = opFunc.as_function();
		std::vector<script_value> args = {currentValue, rightValue};
		auto result = func(args);
		if (!result) {
			return result.error_value();
		}
		resultValue = std::move(result.value());
	} else {
		switch (op) {
			case token_type::plus: {
				if (currentValue.is_string() || rightValue.is_string()) {
					resultValue = script_value(currentValue.to_string() + rightValue.to_string(), engine_);
				} else {
					JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue));
				}
				break;
			}
			case token_type::minus: {
				JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::minus, rightValue));
				break;
			}
			case token_type::star: {
				JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::star, rightValue));
				break;
			}
			case token_type::slash: {
				const size_t ri = rightValue.raw_storage_index();
				if ((ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) ||
				    (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0)) {
					return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
				}
				JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue));
				break;
			}
			case token_type::percent: {
				if (rightValue.raw_storage_index() == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
					return checked_result<void>(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
				}
				JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
				break;
			}
			default:
				return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
					"Unknown compound assignment operator");
		}
	}

	if (!currentValue.is_reference()) {
		return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target), "Cannot assign to rvalue expression");
	}
	auto refHolder = currentValue.get_reference_holder();
	script_value* target_ptr = refHolder->target;
	if (!target_ptr) {
		return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Invalid reference in assignment");
	}
	type_info_ptr element_type = refHolder->container_element_type;
	if (element_type) {
		if (!vm_is_element_type_compatible(resultValue, element_type, *target_ptr)) {
			std::string value_type = vm_value_type_name(resultValue);
			std::string expected_type = vm_type_info_name(element_type);
			uint64_t value_type_id = symbolizer_->intern(value_type);
			uint64_t expected_type_id = symbolizer_->intern(expected_type);
			return checked_result<void>(
				make_error_code(runtime_error_code::array_element_type_mismatch),
				"Cannot assign '{0}' to element of type '{1}'",
				value_type_id, expected_type_id);
		}
		script_value converted = vm_convert_array_element(engine_, resultValue, element_type);
		*target_ptr = std::move(converted);
	} else {
		*target_ptr = std::move(resultValue.clone());
	}
	stack_.push_back(std::move(resultValue));
	return {};
}

checked_result<void> vm_backend::exec_decl_var(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<variable_decl*>(f.code->nodes[ins.a].get());
	const bool has_init = ins.b != 0;
	const bool lvalue_init = ins.c != 0;

	const bool is_weak_ptr = decl->type && decl->type->base_type == script_value_type::jai_weak_ptr_type;
	const bool is_shared_ptr = decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type;

	if (is_weak_ptr) {
		if (!has_init) {
			return define_decl_value(f, decl->name_id, decl->slot_index,
				script_value::make_empty_weak_ptr(decl->type, engine_));
		}
		script_value value = std::move(stack_.back());
		stack_.pop_back();
		if (value.is_null()) {
			return define_decl_value(f, decl->name_id, decl->slot_index,
				script_value::make_empty_weak_ptr(decl->type, engine_));
		}
		if (value.is_weak_ptr()) {
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value));
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
						uint64_t expected_id = expected_type->id;
						uint64_t actual_id = value_type_info->element_type()
							? value_type_info->element_type()->id
							: value_type_info->id;
						return checked_result<void>(
							make_error_code(runtime_error_code::type_mismatch),
							"Cannot initialize weak_ptr<{}> from shared_ptr<{}>: type must match or be a subclass",
							expected_id, actual_id);
					}
				}
			}
			auto weak_result = script_value::make_weak_ptr(value, engine_);
			if (!weak_result) {
				return weak_result.error_value();
			}
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(weak_result.value()));
		}
		if (value.type() == script_value_type::jai_object_type) {
			auto type_info = decl->type;
			uint64_t weak_type_id = (type_info && !type_info->type_params.empty())
				? type_info->type_params[0]->id : 0;
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
				"Cannot initialize weak_ptr<{}> from value-semantic object: use shared_ptr<T>",
				weak_type_id);
		}
		auto type_info = value.get_type_info();
		uint64_t actual_type_id = type_info ? type_info->id : 0;
		auto weak_type_info = decl->type;
		uint64_t weak_type_id = (weak_type_info && !weak_type_info->type_params.empty())
			? weak_type_info->type_params[0]->id : 0;
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
			"Cannot initialize weak_ptr<{}> with {}: use shared_ptr<T>",
			weak_type_id, actual_type_id);
	}

	if (is_shared_ptr) {
		if (!has_init) {
			script_value null_ptr = make_null();
			null_ptr.set_type_info(decl->type);
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(null_ptr));
		}
		script_value value = std::move(stack_.back());
		stack_.pop_back();
		if (value.is_null()) {
			value.set_type_info(decl->type);
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value));
		}
		if (value.is_weak_ptr()) {
			return checked_result<void>(make_error_code(runtime_error_code::invalid_weak_ptr_conversion), "Cannot initialize shared_ptr directly from weak_ptr");
		}
		if (value.type() == script_value_type::jai_object_type ||
		    value.type() == script_value_type::jai_shared_ptr_type) {
			value.set_type_info(decl->type);
			return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value));
		}
		return checked_result<void>(make_error_code(runtime_error_code::invalid_shared_ptr_conversion), "Cannot initialize shared_ptr with this type");
	}

	script_value value = make_null();
	if (has_init) {
		value = std::move(stack_.back());
		stack_.pop_back();

		if (lvalue_init &&
		    (!value.get_type_info() || value.get_type_info()->base_type != script_value_type::jai_shared_ptr_type)) {
			value = value.clone();
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
				JAISCRIPT_TRY(vm_validate_container_homogeneous(value, ""));
			}
		}
	}

	if (decl->type) {
		value.set_type_info(decl->type);
	}

	return define_decl_value(f, decl->name_id, decl->slot_index, std::move(value));
}

checked_result<void> vm_backend::exec_decl_ref_ident(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<variable_decl*>(f.code->nodes[ins.a].get());
	const uint64_t target_sym = f.code->symbols[ins.b];

	// Env-only lookup (interpreter parity: reference decls never consult frame slots)
	script_value* targetPtr = environment_->get_value_ptr(target_sym);
	if (!targetPtr) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), "Cannot take reference of undefined variable", target_sym);
	}

	if (targetPtr->is_reference()) {
		auto refHolder = targetPtr->get_reference_holder();
		targetPtr = refHolder->target;
		auto target_env = refHolder->sourceEnv.lock();
		if (!target_env) {
			return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Reference target environment has been destroyed");
		}
		script_value refValue = script_value::make_reference(targetPtr, target_env);
		return define_decl_value(f, decl->name_id, decl->slot_index, std::move(refValue));
	}
	script_value refValue = script_value::make_reference(targetPtr, environment_);
	return define_decl_value(f, decl->name_id, decl->slot_index, std::move(refValue));
}

checked_result<void> vm_backend::exec_decl_ref_value(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<variable_decl*>(f.code->nodes[ins.a].get());
	script_value result = std::move(stack_.back());
	stack_.pop_back();

	if (result.is_reference()) {
		auto refHolder = result.get_reference_holder();
		script_value* targetPtr = refHolder->target;
		auto target_env = refHolder->sourceEnv.lock();
		if (!target_env) {
			return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Reference target environment has been destroyed");
		}
		script_value refValue = script_value::make_reference(targetPtr, target_env);
		return define_decl_value(f, decl->name_id, decl->slot_index, std::move(refValue));
	}
	return checked_result<void>(make_error_code(runtime_error_code::invalid_reference), "Cannot take reference of non-lvalue expression");
}

checked_result<void> vm_backend::exec_destructure(frame& f, const vm_instruction& ins) {
	script_value source = std::move(stack_.back());
	stack_.pop_back();

	if (!source.is_array()) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
			"Destructuring requires an array on the right-hand side");
	}

	auto& arr = *source.get_array_storage();
	const destructure_proto& proto = f.code->destructure_protos[ins.a];

	for (size_t i = 0; i < proto.names.size(); ++i) {
		script_value val = (i < arr.size()) ? arr[i].clone() : make_null();
		JAISCRIPT_TRY(define_decl_value(f, proto.names[i].first, proto.names[i].second, std::move(val)));
	}
	return {};
}

checked_result<void> vm_backend::exec_call(frame& f, const vm_instruction& ins) {
	const size_t argc = ins.a;
	const call_site& site = f.code->call_sites[ins.b];

	auto arguments = acquire_arg_vector(argc);
	arg_vector_return arg_return{this, &arguments};
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		arguments.push_back(std::move(stack_[base + i]));
	}
	stack_.erase(stack_.begin() + base, stack_.end());

	script_value callee = std::move(stack_.back());
	stack_.pop_back();

	if (!callee.is_function()) {
		return checked_result<void>(make_error_code(runtime_error_code::not_a_function));
	}

	const script_function& func = callee.as_function();
	// Own-trampoline fast path: dispatch straight into the call machinery (Squirrel's
	// OP_CALL→StartCall shape), skipping std::function + backend lookup + virtual hop
	const auto* thunk = func.target<script_callable_thunk>();
	const bool direct = thunk && thunk->eng == engine_ &&
	                    thunk->payload.kind == script_callable::kind_type::function && thunk->payload.fn;
	const bool in_loop = direct && arguments.size() == thunk->payload.fn->parameters.size();

	// Inline the invoke tail: an extra callee frame per script call would blow the
	// native stack before JAI_MAX_CALL_DEPTH is reached in Debug builds.
	// Ref-free in-loop callees never read arg metadata, so skip the build entirely.
	const bool has_args = argc > 0 && !(in_loop && !thunk->payload.fn->has_reference_parameters);
	std::vector<std::pair<uint64_t, environment*>> saved_metadata;
	if (has_args) {
		saved_metadata = std::move(current_arg_metadata_);
		current_arg_metadata_.clear();
		current_arg_metadata_.reserve(argc);
		for (size_t i = 0; i < argc; ++i) {
			uint64_t symbol_id = i < site.arg_symbols.size() ? site.arg_symbols[i] : UINT64_MAX;
			if (symbol_id != UINT64_MAX) {
				current_arg_metadata_.emplace_back(symbol_id, environment_.get());
			} else {
				current_arg_metadata_.emplace_back(UINT64_MAX, nullptr);
			}
		}
	}

	if (in_loop) {
		// Exact-arity direct callees stay inside the dispatch loop (Squirrel EnterFrame
		// shape); default-arg and arity-error calls keep the native path below.
		// push_script_frame owns saved_metadata restoration on every exit path.
		try {
			return push_script_frame(f, std::move(callee), *thunk->payload.fn, arguments, saved_metadata, has_args);
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
			callOutcome.emplace(call_script_function(*thunk->payload.fn, arguments));
		} else {
			callOutcome.emplace(func(arguments));
		}
	} catch (const script_exception& e) {
		if (has_args) {
			current_arg_metadata_ = std::move(saved_metadata);
		}
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		if (has_args) {
			current_arg_metadata_ = std::move(saved_metadata);
		}
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
	checked_result<script_value>& result_checked = *callOutcome;

	if (has_args) {
		current_arg_metadata_ = std::move(saved_metadata);
	}

	if (!result_checked) {
		return result_checked.error_value();
	}

	stack_.push_back(std::move(result_checked.value()));
	return {};
}

checked_result<void> vm_backend::invoke_callee(frame& f, script_value&& callee, std::vector<script_value>& arguments, const call_site& site) {
	const size_t argc = arguments.size();

	const script_function& func = callee.as_function();
	// Own-trampoline fast path: dispatch straight into the call machinery (Squirrel's
	// OP_CALL→StartCall shape), skipping std::function + backend lookup + virtual hop
	const auto* thunk = func.target<script_callable_thunk>();
	const bool direct = thunk && thunk->eng == engine_ &&
	                    thunk->payload.kind == script_callable::kind_type::function && thunk->payload.fn;
	const bool in_loop = direct && arguments.size() == thunk->payload.fn->parameters.size();

	// Ref-free in-loop callees never read arg metadata, so skip the build entirely
	const bool has_args = argc > 0 && !(in_loop && !thunk->payload.fn->has_reference_parameters);
	std::vector<std::pair<uint64_t, environment*>> saved_metadata;
	if (has_args) {
		saved_metadata = std::move(current_arg_metadata_);
		current_arg_metadata_.clear();
		current_arg_metadata_.reserve(argc);
		for (size_t i = 0; i < argc; ++i) {
			uint64_t symbol_id = i < site.arg_symbols.size() ? site.arg_symbols[i] : UINT64_MAX;
			if (symbol_id != UINT64_MAX) {
				current_arg_metadata_.emplace_back(symbol_id, environment_.get());
			} else {
				current_arg_metadata_.emplace_back(UINT64_MAX, nullptr);
			}
		}
	}

	if (in_loop) {
		// Same in-loop entry as exec_call; op_call_method's dispatch case consumes
		// switch_to_. push_script_frame owns saved_metadata restoration on every exit.
		try {
			return push_script_frame(f, std::move(callee), *thunk->payload.fn, arguments, saved_metadata, has_args);
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
			callOutcome.emplace(call_script_function(*thunk->payload.fn, arguments));
		} else {
			callOutcome.emplace(func(arguments));
		}
	} catch (const script_exception& e) {
		if (has_args) {
			current_arg_metadata_ = std::move(saved_metadata);
		}
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = e;
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	} catch (const std::exception& e) {
		if (has_args) {
			current_arg_metadata_ = std::move(saved_metadata);
		}
		active_exception_value_ = script_value(std::string(e.what()), engine_);
		current_exception_ = script_exception(e.what());
		is_unwinding_ = true;
		stack_.push_back(make_null());
		return {};
	}
	checked_result<script_value>& result_checked = *callOutcome;

	if (has_args) {
		current_arg_metadata_ = std::move(saved_metadata);
	}

	if (!result_checked) {
		return result_checked.error_value();
	}

	stack_.push_back(std::move(result_checked.value()));
	return {};
}

checked_result<void> vm_backend::exec_func_decl(frame& f, const vm_instruction& ins) {
	const function_proto& proto = f.code->function_protos[ins.a];
	engine* eng = engine_;

	if (proto.is_coroutine) {
		// Calling a coroutine function mints a handle (same interned identity as the
		// interpreter) and does NOT run the body; resume drives it.
		auto func_decl_ptr = proto.decl;
		auto closure_env = environment_;
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
	return {};
}

checked_result<void> vm_backend::exec_closure(frame& f, const vm_instruction& ins) {
	const closure_proto& proto = f.code->closure_protos[ins.a];
	auto closure_env = environment_;

	// Runtime filter over the compile-time plan (parity: only live outer-frame slots capture)
	std::vector<std::pair<uint64_t, size_t>> outer_refs;
	if (f.locals && !f.top_level) {
		const size_t outer_slot_count = f.locals->local_count();
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

				if (environment_->contains(var_id)) {
					if (capture_by_ref_default) {
						script_value* targetPtr = environment_->get_value_ptr(var_id);
						if (targetPtr) {
							script_value refValue = script_value::make_reference(targetPtr, environment_);
							captureEnv->define(var_id, std::move(refValue));
						}
					} else {
						auto capture_result = environment_->get(var_id);
						if (capture_result) {
							captureEnv->define(var_id, vm_clone_for_capture(capture_result.value(), symbolizer_));
						}
					}
				} else if (f.locals && !f.top_level) {
					for (const auto& [sym, slot] : outer_refs) {
						if (sym == var_id) {
							script_value* slot_val = f.locals->get_local(slot);
							if (slot_val) {
								// By-ref of a stack slot is lifetime-unsafe for escaping closures
								captureEnv->define(var_id, vm_clone_for_capture(slot_val->deref(), symbolizer_));
							}
							break;
						}
					}
				}
			}
		}

		for (const auto& capture : proto.captures) {
			bool can_capture = environment_->contains(capture.symbol_id);
			bool capture_from_slot = false;
			script_value* slot_val = nullptr;

			if (!can_capture && capture.symbol_id == this_id_) {
				auto this_test_result = environment_->get(this_id_);
				if (this_test_result) can_capture = true;
			}

			if (!can_capture && f.locals && !f.top_level) {
				for (const auto& [sym, slot] : outer_refs) {
					if (sym == capture.symbol_id) {
						slot_val = f.locals->get_local(slot);
						if (slot_val) { can_capture = true; capture_from_slot = true; }
						break;
					}
				}
			}

			if (!can_capture) {
				return checked_result<void>(make_error_code(runtime_error_code::capture_undefined_variable),
					"Cannot capture undefined variable '{0}'", capture.symbol_id);
			}

			if (capture_from_slot) {
				captureEnv->define(capture.symbol_id, vm_clone_for_capture(slot_val->deref(), symbolizer_));
			} else if (capture.by_reference) {
				script_value* targetPtr = environment_->get_value_ptr(capture.symbol_id);
				if (targetPtr) {
					script_value refValue = script_value::make_reference(targetPtr, environment_);
					captureEnv->define(capture.symbol_id, std::move(refValue));
				} else {
					return checked_result<void>(make_error_code(runtime_error_code::capture_reference_failed),
						"Cannot capture variable '{0}' by reference", capture.symbol_id);
				}
			} else {
				auto capture_result = environment_->get(capture.symbol_id);
				if (!capture_result) return capture_result.error_value();
				captureEnv->define(capture.symbol_id, vm_clone_for_capture(capture_result.value(), symbolizer_));
			}
		}

		if (f.locals && !f.top_level) {
			for (const auto& [sym, slot] : outer_refs) {
				if (captureEnv->contains(sym)) {
					continue;
				}
				script_value* slot_val = f.locals->get_local(slot);
				if (slot_val) {
					captureEnv->define(sym, vm_clone_for_capture(slot_val->deref(), symbolizer_));
				}
			}
		}

		if (proto.captures_this) {
			auto this_result = captureEnv->get(this_id_);
			if (!this_result) {
				return checked_result<void>(make_error_code(runtime_error_code::capture_reference_failed),
					"Failed to capture 'this' reference");
			}
			script_value this_obj = std::move(this_result.value());

			auto method_env = std::make_shared<environment>(closure_env, symbolizer_, this_obj);
			method_env->define(this_id_, this_obj);

			for (const auto& capture : proto.captures) {
				if (capture.symbol_id != this_id_ && captureEnv->contains(capture.symbol_id)) {
					auto capture_result = captureEnv->get(capture.symbol_id);
					if (capture_result) {
						method_env->define(capture.symbol_id, std::move(capture_result.value()));
					}
				}
			}

			if (has_default_capture) {
				for (uint64_t var_id : proto.used_variables) {
					if (var_id != this_id_ && captureEnv->contains(var_id)) {
						auto var_result = captureEnv->get(var_id);
						if (var_result) {
							method_env->define(var_id, std::move(var_result.value()));
						}
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

	auto lambdaFunc = std::make_shared<script_defined_function>(
		"<lambda>",
		proto.lambda->parameters,
		proto.lambda->return_type,
		proto.body,
		needs_capture_env ? final_closure_env : nullptr);

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

	if (stack_.size() > f.stack_base) {
		stack_.erase(stack_.begin() + f.stack_base, stack_.end());
	}
	environment_ = saved_env;
	return outcome;
}

checked_result<void> vm_backend::exec_this(frame& f, const vm_instruction& ins) {
	auto this_result = environment_->get(symbolizer_->get_this_id());
	if (!this_result) {
		return checked_result<void>(make_error_code(runtime_error_code::this_outside_method),
			"'this' can only be used inside methods");
	}
	stack_.push_back(std::move(this_result.value()));
	return {};
}

checked_result<void> vm_backend::exec_super(frame& f, const vm_instruction& ins) {
	auto this_result = environment_->get(symbolizer_->get_this_id());
	if (!this_result) {
		return checked_result<void>(make_error_code(runtime_error_code::super_outside_method),
			"'super' can only be used inside methods");
	}
	script_value this_value = std::move(this_result.value());
	if (this_value.is_null()) {
		return checked_result<void>(make_error_code(runtime_error_code::super_outside_method),
			"'super' can only be used inside methods");
	}
	stack_.push_back(std::move(this_value));
	return {};
}

checked_result<void> vm_backend::exec_from_this(frame& f, const vm_instruction& ins) {
	const uint64_t sym = f.code->symbols[ins.b];
	auto this_result = environment_->get(symbolizer_->get_this_id());
	if (!this_result) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
			"{0}() can only be called from within a method", sym);
	}
	script_value this_val = std::move(this_result.value());
	if (!this_val.is_object()) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
			"{0}() can only be called from within a method", sym);
	}
	if (ins.a) {
		auto weak_result = script_value::make_weak_ptr(this_val, engine_);
		if (!weak_result) {
			return weak_result.error_value();
		}
		stack_.push_back(std::move(weak_result.value()));
	} else {
		stack_.push_back(std::move(this_val));
	}
	return {};
}

checked_result<void> vm_backend::member_access_value(const script_value& raw_object, member_expr* expr, script_value& out) {
	script_value objectValue = raw_object.deref();

	if (expr->null_safe && objectValue.is_null()) {
		out = make_null();
		return {};
	}

	if (expr->object && expr->object->get_type() == node_type::super_expr) {
		if (!objectValue.is_object()) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		auto objHolder = objectValue.get_object_holder();
		if (!objHolder || !objHolder->data) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
		if (!instance) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		auto class_def = instance->get_class_definition();
		if (!class_def) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		auto parent_def = class_def->get_parent();
		if (!parent_def) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
			return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
				"Parent class has no method '{0}'", expr->member_id);
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
			return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
				"coroutine_handle has no member '{0}'", expr->member_id);
		}
	}

	if (objectValue.is_string()) {
		auto methodIt = builtins_.string_methods.find(expr->member_id);
		if (methodIt != builtins_.string_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				return method(ctx, capturedValue, args);
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		std::string member_str(expr->member);
		raise_script_exception("String has no method '" + member_str + "'", expr->location);
		out = make_null();
		return {};
	}

	if (objectValue.is_array()) {
		auto methodIt = builtins_.array_methods.find(expr->member_id);
		if (methodIt != builtins_.array_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				return method(ctx, capturedValue, args);
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		std::string member_str(expr->member);
		raise_script_exception("Array has no method '" + member_str + "'", expr->location);
		out = make_null();
		return {};
	}

	if (objectValue.is_map()) {
		auto methodIt = builtins_.map_methods.find(expr->member_id);
		if (methodIt != builtins_.map_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				return method(ctx, capturedValue, args);
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
		return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
			"Map has no method or key '{0}'", expr->member_id);
	}

	if (objectValue.is_weak_ptr()) {
		auto methodIt = builtins_.weak_ptr_methods.find(expr->member_id);
		if (methodIt != builtins_.weak_ptr_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				return method(ctx, capturedValue, args);
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
		}
		return checked_result<void>(make_error_code(runtime_error_code::member_not_found),
			"weak_ptr has no method '{0}'", expr->member_id);
	}

	if (objectValue.get_type_info() &&
	    objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
		auto methodIt = builtins_.shared_ptr_methods.find(expr->member_id);
		if (methodIt != builtins_.shared_ptr_methods.end()) {
			const builtin_method& method = methodIt->second;
			script_function boundMethod = [ctx = builtin_ctx(), capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
				return method(ctx, capturedValue, args);
			};
			out = script_value::make_function(boundMethod, engine_);
			return {};
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
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}

	auto target = resolve_member_target(objectValue);
	if (!target) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}

	uint64_t member_id = expr->member_id;

	if (target.class_def && target.class_def->has_property_getters()) {
		try {
			uint64_t getter_id = expr->getter_id;
			if (getter_id == UINT64_MAX) {
				auto [id, _] = symbolizer_->get_getter_id_with_view(member_id);
				getter_id = id;
				expr->getter_id = getter_id;
			}
			script_value getter = target.method(getter_id);
			if (!getter.is_null() && !getter.is_invalid() && getter.is_function()) {
				const script_function& func = getter.as_function();
				std::vector<script_value> args = {objectValue};
				auto result = func(args);
				if (!result) {
					return result.error_value();
				}
				// Getter results referencing the receiver must keep the receiver's data alive
				script_value& getterResult = result.value();
				if (getterResult.is_non_owning_object()) {
					auto resultHolder = getterResult.get_object_holder();
					auto receiverHolder = objectValue.get_object_holder();
					if (resultHolder && receiverHolder) {
						resultHolder->keep_alive = receiverHolder->data ? receiverHolder->data
						                                                : receiverHolder->keep_alive;
					}
				}
				out = std::move(result.value());
				return {};
			}
		} catch (const std::exception&) {
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

	std::string member_str(expr->member);
	raise_script_exception("Object has no member '" + member_str + "'", expr->location);
	out = make_null();
	return {};
}

checked_result<void> vm_backend::static_member_value(member_expr* expr, script_value& out) {
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
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		name_id = symbolizer_->intern(name);
	} else {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
			auto mint_env = environment_;
			script_function namespace_func = [eng, mint_env, overloads, namespace_id, fallback_class, member_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				execution_backend* backend = eng ? eng->get_execution_backend() : nullptr;
				if (!backend) {
					return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
				}

				for (const auto& func_decl : overloads) {
					if (func_decl->parameters.size() == args.size()) {
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
							func_decl->parameters,
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

				return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
					"No matching overload in namespace '{0}' for {1} arguments",
					namespace_id, static_cast<uint64_t>(args.size()));
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
				return ctor_result.error_value();
			}
			out = std::move(ctor_result.value());
			return {};
		}
	}

	auto [class_var_id, class_var_view] = symbolizer_->get_class_var_id_with_view(name_id);
	auto class_var_result = environment_->get(class_var_id);
	if (!class_var_result) {
		return class_var_result.error_value();
	}
	script_value class_var = std::move(class_var_result.value());

	if (!class_var.is_object()) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}

	auto objHolder = class_var.get_object_holder();
	if (!objHolder || objHolder->type_name != "class_definition") {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}

	auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

	script_value static_method = class_def->get_static_method(expr->member_id, false);
	if (!static_method.is_null()) {
		out = static_method;
		return {};
	}

	script_value static_value = class_def->get_static_field(expr->member_id);
	if (!static_value.is_null()) {
		out = static_value;
		return {};
	}

	auto [getter_id, getter_view] = symbolizer_->get_getter_id_with_view(expr->member_id);
	script_value getter_method = class_def->get_static_method(getter_id, false);
	if (!getter_method.is_null() && getter_method.is_function()) {
		auto func = getter_method.as_function();
		std::vector<script_value> no_args;
		auto result = func(no_args);
		if (!result) {
			return result.error_value();
		}
		out = std::move(result.value());
		return {};
	}

	return checked_result<void>(make_error_code(runtime_error_code::static_member_not_found),
		"Class '{0}' has no static member '{1}'", name_id, expr->member_id);
}

checked_result<void> vm_backend::exec_get_member(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value object = std::move(stack_.back());
	stack_.pop_back();
	script_value out = make_null();
	JAISCRIPT_TRY(member_access_value(object, expr, out));
	stack_.push_back(std::move(out));
	return {};
}

checked_result<void> vm_backend::exec_get_static(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value out = make_null();
	JAISCRIPT_TRY(static_member_value(expr, out));
	stack_.push_back(std::move(out));
	return {};
}

checked_result<void> vm_backend::assign_member(const script_value& object_value, member_expr* member, const script_value& value) {
	auto target = resolve_member_target(object_value);
	if (!target) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
			"Cannot assign property to non-object value");
	}

	uint64_t member_id = member->member_id != UINT64_MAX
		? member->member_id
		: symbolizer_->intern(member->member);

	auto [setter_id, setter_view] = symbolizer_->get_setter_id_with_view(member_id);
	script_value setter = target.method(setter_id);
	if (!setter.is_null() && !setter.is_invalid() && setter.is_function()) {
		const script_function& func = setter.as_function();
		std::vector<script_value> args = {object_value, value.clone()};
		auto result = func(args);
		if (!result) {
			return result.error_value();
		}
	} else if (target.has_field(member_id)) {
		target.instance->set_field(member_id, clone_for_assignment(value));
	} else {
		std::string member_str(member->member);
		raise_script_exception("Cannot assign to non-existent member '" + member_str + "'", member->location);
		return {};
	}
	return {};
}

checked_result<void> vm_backend::exec_set_member(frame& f, const vm_instruction& ins) {
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

	JAISCRIPT_TRY(assign_member(dereferenced, member, value));
	if (is_unwinding_) {
		stack_.push_back(make_null());
		return {};
	}
	stack_.push_back(std::move(value));
	return {};
}

checked_result<void> vm_backend::exec_set_static(frame& f, const vm_instruction& ins) {
	auto* member = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	script_value value = std::move(stack_.back());
	stack_.pop_back();

	if (member->object->get_type() != node_type::identifier_expr) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
			"Static member assignment requires a class name");
	}
	auto* ident_expr = static_cast<identifier_expr*>(member->object.get());

	uint64_t class_name_id = ident_expr->symbol_id;
	auto [class_var_id, class_var_name] = symbolizer_->get_class_var_id_with_view(class_name_id);
	auto class_result = environment_->get(class_var_id);
	if (!class_result) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
			"Class '{0}' not found", class_name_id);
	}
	script_value class_var = std::move(class_result.value());

	if (!class_var.is_object()) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
			"'{0}' is not a class", class_name_id);
	}

	auto objHolder = class_var.get_object_holder();
	if (!objHolder || objHolder->type_name != "class_definition") {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
			"'{0}' is not a valid class", class_name_id);
	}

	auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

	if (!class_def->set_static_field(member->member_id, value.clone())) {
		return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
			"Cannot assign to static member: field '{0}' not found", member->member_id);
	}

	stack_.push_back(std::move(value));
	return {};
}

checked_result<void> vm_backend::exec_member_compound(frame& f, const vm_instruction& ins) {
	auto* member = static_cast<member_expr*>(f.code->nodes[ins.a].get());
	const uint32_t kind = ins.c & compound_kind_mask;

	script_value objectValueRaw = std::move(stack_.back());
	stack_.pop_back();
	script_value rightValue = std::move(stack_.back());
	stack_.pop_back();
	script_value currentValue = std::move(stack_.back());
	stack_.pop_back();
	currentValue = currentValue.deref();

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
				script_value objectValue = objectValueRaw.deref();
				if (objectValue.is_object()) {
					JAISCRIPT_TRY(assign_member(objectValue, member, custom_result.value()));
					if (is_unwinding_) {
						stack_.push_back(make_null());
						return {};
					}
					stack_.push_back(std::move(custom_result.value()));
					return {};
				}
			}
		}
	}

	const size_t ci = currentValue.raw_storage_index();
	const size_t ri = rightValue.raw_storage_index();
	script_value resultValue = make_null();

	switch (kind) {
		case compound_plus: {
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_add(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '+='");
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf + rf, engine_);
			} else if (ci == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) {
				resultValue = script_value(currentValue.unchecked_as_string() + rightValue.unchecked_as_string(), engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for +=");
			}
			break;
		}
		case compound_minus: {
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_sub(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '-='");
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf - rf, engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for -=");
			}
			break;
		}
		case compound_star: {
			if (ci == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
				script_int rr;
				if (!ints::try_mul(currentValue.unchecked_as_int(), rightValue.unchecked_as_int(), rr)) return vm_int_overflow_v("Integer overflow in '*='");
				resultValue = script_value(rr, engine_);
			} else if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			           (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf * rf, engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for *=");
			}
			break;
		}
		case compound_slash: {
			if (ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
				return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
					"Division by zero");
			} else if (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0) {
				return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
					"Division by zero");
			}
			if ((ci == script_value::TYPEID_INT || ci == script_value::TYPEID_FLOAT) &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT)) {
				script_float cf = (ci == script_value::TYPEID_INT) ? script_float(currentValue.unchecked_as_int()) : currentValue.unchecked_as_float();
				script_float rf = (ri == script_value::TYPEID_INT) ? script_float(rightValue.unchecked_as_int()) : rightValue.unchecked_as_float();
				resultValue = script_value(cf / rf, engine_);
			} else {
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
					"Invalid operands for /=");
			}
			break;
		}
		default:
			return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
				"Unsupported compound assignment operator");
	}

	script_value objectValue = objectValueRaw.deref();
	if (!objectValue.is_object()) {
		raise_script_exception("Cannot assign to member of non-object type", member->location);
		stack_.push_back(make_null());
		return {};
	}

	JAISCRIPT_TRY(assign_member(objectValue, member, resultValue));
	if (is_unwinding_) {
		stack_.push_back(make_null());
		return {};
	}

	stack_.push_back(std::move(resultValue));
	return {};
}

checked_result<void> vm_backend::exec_call_method(frame& f, const vm_instruction& ins) {
	const size_t argc = ins.a;
	const call_site& site = f.code->call_sites[ins.b];
	auto* member = static_cast<member_expr*>(f.code->nodes[site.member_node].get());

	auto arguments = acquire_arg_vector(argc);
	arg_vector_return arg_return{this, &arguments};
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		arguments.push_back(std::move(stack_[base + i]));
	}
	stack_.erase(stack_.begin() + base, stack_.end());

	script_value object = std::move(stack_.back());
	stack_.pop_back();

	if (site.receiver_symbol != UINT64_MAX) {
		auto ref_result = environment_->get_ref(site.receiver_symbol);
		if (ref_result && ref_result.value().get().is_string()) {
			auto methodIt = builtins_.string_methods.find(member->member_id);
			if (methodIt != builtins_.string_methods.end()) {
				script_value& var_ref = ref_result.value().get();
				auto result = methodIt->second(builtin_ctx(), var_ref, arguments);
				if (!result) {
					return result.error_value();
				}
				stack_.push_back(std::move(result.value()));
				return {};
			}
		}
	}

	// Script-class instance methods enter the dispatch loop directly. The gate mirrors
	// member_access_value's precedence exactly (super/coroutine/builtin/same_as/getter/
	// field all bail to the native bound-method path), so behavior is byte-identical.
	if (member->member_id != same_as_id_ &&
	    !(member->object && member->object->get_type() == node_type::super_expr)) {
		script_value objv = object.deref();
		if (objv.is_object()) {
			auto holder = objv.get_object_holder();
			const bool shared_ptr_builtin = objv.get_type_info() &&
				objv.get_type_info()->base_type == script_value_type::jai_shared_ptr_type &&
				builtins_.shared_ptr_methods.find(member->member_id) != builtins_.shared_ptr_methods.end();
			if (holder && holder->type_id != coroutine_handle_type_id_ && !shared_ptr_builtin) {
				auto target = resolve_member_target(objv);
				if (target && target.class_def && !target.class_def->has_property_getters() &&
				    !target.has_field(member->member_id)) {
					script_value method_val = target.method(member->member_id);
					if (method_val.is_function()) {
						const auto* dispatch = method_val.as_function().target<script_method_dispatch>();
						if (dispatch && dispatch->eng == engine_) {
							auto resolved = dispatch->cls->resolve_method_overload(dispatch->name_id, arguments);
							// Resolution failures (and coroutine methods) fall through to the
							// native path, which re-resolves and reports the identical error
							if (resolved && resolved.value()->body && !resolved.value()->is_coroutine) {
								return enter_script_method(f, std::move(method_val), *dispatch,
								                           resolved.value(), std::move(objv), arguments, site);
							}
						}
					}
				}
			}
		}
	}

	script_value callee = make_null();
	JAISCRIPT_TRY(member_access_value(object, member, callee));
	if (is_unwinding_) {
		stack_.push_back(make_null());
		return {};
	}
	if (callee.is_null() && member->null_safe) {
		stack_.push_back(make_null());
		return {};
	}
	if (!callee.is_function()) {
		return checked_result<void>(make_error_code(runtime_error_code::not_a_function));
	}
	return invoke_callee(f, std::move(callee), arguments, site);
}

checked_result<void> vm_backend::exec_new(frame& f, const vm_instruction& ins) {
	auto* expr = static_cast<new_expr*>(f.code->nodes[ins.a].get());
	const size_t argc = ins.b;

	std::vector<script_value> args;
	args.reserve(argc);
	const size_t base = stack_.size() - argc;
	for (size_t i = 0; i < argc; ++i) {
		args.push_back(std::move(stack_[base + i]));
	}
	stack_.erase(stack_.begin() + base, stack_.end());

	if (!expr->type) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
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
				return checked_result<void>(
					make_error_code(runtime_error_code::type_mismatch),
					"Cannot create weak_ptr from value-semantic object. Use shared_ptr<T>.",
					expected_id);
			}
			auto type_info = obj.get_type_info();
			uint64_t actual_id = type_info ? type_info->id : 0;
			return checked_result<void>(
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
					return checked_result<void>(
						make_error_code(runtime_error_code::type_mismatch),
						"weak_ptr type mismatch: type must match or be a subclass",
						expected_id, actual_id);
				}
			}
		}

		auto weak_result = script_value::make_weak_ptr(obj, engine_);
		if (!weak_result) {
			return weak_result.error_value();
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
			std::string innerTypeName = inner_type->type_name;
			auto ctor_result = environment_->get(innerTypeName);
			if (!ctor_result || !ctor_result.value().is_function()) {
				return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
					"No constructor found for class '{0}'", inner_type->id);
			}
			script_value constructorFunc = std::move(ctor_result.value());
			const script_function& func = constructorFunc.as_function();
			auto result = func({});
			if (!result) {
				return result.error_value();
			}
			script_value value = std::move(result.value());
			if (value.type() == script_value_type::jai_object_type) {
				value.set_type_info(expr->type);
			}
			stack_.push_back(std::move(value));
			return {};
		}

		if (!inner_type) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
				"shared_ptr requires a type parameter when called with arguments");
		}
		std::string innerTypeName = inner_type->type_name;
		auto ctor_result = environment_->get(innerTypeName);
		if (!ctor_result || !ctor_result.value().is_function()) {
			return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
				"No constructor found for class '{0}'", inner_type->id);
		}
		script_value constructorFunc = std::move(ctor_result.value());
		const script_function& func = constructorFunc.as_function();
		auto result = func(args);
		if (!result) {
			return result.error_value();
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
		auto result = func(args);
		if (!result) {
			return result.error_value();
		}
		stack_.push_back(std::move(result.value()));
		return {};
	}

	return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
		"No constructor found for class '{0}'", expr->type->id);
}

checked_result<void> vm_backend::exec_enum_decl(frame& f, const vm_instruction& ins) {
	auto* decl = static_cast<enum_decl*>(f.code->nodes[ins.a].get());
	auto enum_map = script_value::make_map(
		engine_->get_type_info_string(),
		engine_->get_type_info_int(),
		engine_);
	auto& map_ref = const_cast<std::map<script_value, script_value>&>(enum_map.as_map());

	for (size_t i = 0; i < decl->values.size(); ++i) {
		auto key = script_value(std::string(decl->values[i].first), engine_);
		map_ref[std::move(key)] = script_value(static_cast<script_int>(i), engine_);
	}

	environment_->define(decl->name_id, std::move(enum_map));
	return {};
}

checked_result<void> vm_backend::exec_class_decl(frame& f, const vm_instruction& ins) {
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

checked_result<void> vm_backend::exec_class_decl_node(class_decl* decl) {
	std::shared_ptr<script_class_definition> class_def;
	bool is_redefinition = false;

	auto [class_var_id, class_var_name_view] = symbolizer_->get_class_var_id_with_view(decl->name_id);

	auto global_env = engine_ ? engine_->get_global_environment() : nullptr;
	if (!global_env) {
		return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
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

	std::unordered_map<uint64_t, script_value> new_field_defaults;
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
					return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
				}
			} else {
				if (class_lookup_) {
					auto cpp_class_def = class_lookup_(std::string(base_name));
					if (cpp_class_def) {
						base_class_def = cpp_class_def;
					} else if (environment_->contains(base_name_id)) {
						return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
							"Constructor found for '{0}' but no class definition available", base_name_id);
					} else {
						return checked_result<void>(make_error_code(runtime_error_code::class_not_found),
							"Base class '{0}' not found", base_name_id);
					}
				} else {
					if (environment_->contains(base_name_id)) {
						return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
							"Script class inheriting from C++ class '{0}' requires engine integration", base_name_id);
					} else {
						return checked_result<void>(make_error_code(runtime_error_code::class_not_found),
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
				return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
			}
		}
	}

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
				return checked_result<void>(make_error_code(runtime_error_code::multiple_inheritance),
					"Field inherited from multiple parents", field_id, decl->name_id);
			}
		}
	}

	bool found_constructor = false;

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
				}
				// A reload preserves a static's runtime value; only NEW statics run their initializer
				bool preserve_existing = is_redefinition && field_id != 0 && class_def->has_static_field(field_id);

				if (initializer_ast && !preserve_existing) {
					auto init_result = eval_expression(initializer_ast, nullptr);
					if (!init_result) {
						return init_result.error_value();
					}
					default_val = std::move(init_result.value());
					if (!default_val.has_valid_engine() && engine_) {
						default_val.set_engine(engine_);
					}
				}

				if (field_id != 0 && !preserve_existing) {
					class_def->add_static_field(field_id, default_val);
				}
			} else {
				if (field_id != 0) {
					if (initializer_ast) {
						class_def->add_field_initializer_ast(field_id, initializer_ast);
					}
					new_field_defaults[field_id] = default_val;
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

	if (found_constructor) {
		auto definition_env = global_env;

		script_callable ctor_payload;
		ctor_payload.kind = script_callable::kind_type::constructor;
		ctor_payload.cls = class_def;
		ctor_payload.definition_env = definition_env;
		engine* ctor_eng = engine_;
		auto ctor_dispatcher = [ctor_eng, ctor_payload](const std::vector<script_value>& args) -> checked_result<script_value> {
			execution_backend* backend = ctor_eng ? ctor_eng->get_execution_backend() : nullptr;
			if (!backend) {
				return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Interpreter was destroyed before constructor call");
			}
			return backend->execute_callable(ctor_payload, args);
		};

		global_env->define(decl->name_id, script_value::make_function(ctor_dispatcher, engine_));
	} else {
		script_callable ctor_payload;
		ctor_payload.kind = script_callable::kind_type::constructor;
		ctor_payload.cls = class_def;
		engine* ctor_eng = engine_;
		auto default_ctor_func = [ctor_eng, ctor_payload](const std::vector<script_value>& args) -> checked_result<script_value> {
			execution_backend* backend = ctor_eng ? ctor_eng->get_execution_backend() : nullptr;
			if (!backend) {
				return checked_result<script_value>(make_error_code(runtime_error_code::internal_error),
					"Interpreter was destroyed before constructor call");
			}
			return backend->execute_callable(ctor_payload, args);
		};

		global_env->define(decl->name_id, script_value::make_function(default_ctor_func, engine_));
	}

	if (is_redefinition) {
		std::unordered_map<uint64_t, script_value> field_defaults_with_engine;
		field_defaults_with_engine.reserve(new_field_defaults.size());

		for (const auto& [field_id, value] : new_field_defaults) {
			auto initializer_ast = class_def->get_field_initializer_ast(field_id);
			script_value evaluated_value = value;

			if (initializer_ast) {
				auto init_result = eval_expression(initializer_ast, nullptr);
				if (!init_result) {
					return init_result.error_value();
				}
				evaluated_value = std::move(init_result.value());
			}

			if (!evaluated_value.has_valid_engine() && engine_) {
				evaluated_value.set_engine(engine_);
			}

			field_defaults_with_engine[field_id] = evaluated_value;
		}

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
			new_methods[getter_id] = script_value::make_function(getter, engine_);

			auto setter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				if (args.size() != 2) {
					return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
						"Property setter requires 'this' and value");
				}
				auto instance = args[0].as<std::shared_ptr<class_instance>>();
				instance->set_field(field_id, args[1]);
				return args[1];
			};
			auto [setter_id, setter_view] = symbolizer_->get_setter_id_with_view(field_id);
			new_methods[setter_id] = script_value::make_function(setter, engine_);
		}

		class_def->redefine_class(field_defaults_with_engine, new_methods, new_static_methods, engine_);

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
			class_def->add_method_by_id(getter_id, getter, true);

			auto setter = [field_id](const std::vector<script_value>& args) -> checked_result<script_value> {
				if (args.size() != 2) {
					return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
						"Property setter requires 'this' and value");
				}
				auto instance = args[0].as<std::shared_ptr<class_instance>>();
				instance->set_field(field_id, args[1]);
				return args[1];
			};
			auto [setter_id, setter_view] = symbolizer_->get_setter_id_with_view(field_id);
			class_def->add_method_by_id(setter_id, setter);
		}
		class_def->initialize_fingerprint();
	}

	if (!engine_) {
		return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
	}
	auto register_result = engine_->get_class_registry().register_script_class(class_def);
	if (!register_result) {
		return register_result;
	}

	global_env->define(class_var_id, script_value::make_object("class_definition", class_definition_type_id_, class_def, engine_, false));

	return {};
}

checked_result<void> vm_backend::exec_namespace_decl(frame& f, const vm_instruction& ins) {
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

checked_result<void> vm_backend::exec_namespace_decl_node(namespace_decl* decl) {
	if (decl->name_id == UINT64_MAX) {
		decl->name_id = symbolizer_->intern(decl->name);
	}

	if (!engine_) {
		return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
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
						return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
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
							return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
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
					return init_result.error_value();
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

			JAISCRIPT_TRY(exec_class_decl_node(class_decl_ptr));

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
					stack_.erase(stack_.begin() + df.stack_base, stack_.end());
				}
				if (!r) {
					return r.error_value();
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
	script_defined_function script_func(
		ast->name,
		ast->parameters,
		ast->return_type,
		ast->body,
		std::move(method_env));
	return call_script_function(script_func, args);
}

void vm_backend::evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                             std::shared_ptr<script_class_definition> class_def,
                                             std::shared_ptr<environment> init_env,
                                             bool skip_parent_recursion) {
	if (!skip_parent_recursion) {
		for (const auto& parent : class_def->get_parent_classes()) {
			auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent);
			if (parent_script_class) {
				evaluate_field_initializers(instance, parent_script_class, init_env, false);
			}
		}
	}

	const auto& field_initializers = class_def->get_field_initializer_asts();
	for (const auto& [field_id, initializer_ast] : field_initializers) {
		if (initializer_ast) {
			auto result = eval_expression(initializer_ast, init_env);
			if (!result) {
				throw runtime_error("Failed to evaluate field initializer for '" + std::string(symbolizer_->get_string(field_id)) + "'");
			}
			script_value field_value = std::move(result.value());
			if (!field_value.has_valid_engine() && engine_) {
				field_value.set_engine(engine_);
			}
			instance->set_field(field_id, field_value);
		}
	}
}

checked_result<script_value> vm_backend::construct_instance(std::shared_ptr<script_class_definition> class_def,
                                                            std::shared_ptr<environment> definition_env,
                                                            const std::vector<script_value>& args) {
	const auto& ctor_asts = class_def->get_constructor_asts();

	std::shared_ptr<function_decl> exact_match_ctor;
	std::shared_ptr<function_decl> convertible_match_ctor;
	std::shared_ptr<function_decl> arity_match_ctor;

	for (const auto& ctor_ast : ctor_asts) {
		if (ctor_ast->parameters.size() != args.size()) {
			continue;
		}

		if (!arity_match_ctor) {
			arity_match_ctor = ctor_ast;
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

		if (exact_match && !exact_match_ctor) {
			exact_match_ctor = ctor_ast;
		}
		if (convertible_match && !convertible_match_ctor) {
			convertible_match_ctor = ctor_ast;
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
		return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
			"No constructor found with matching arguments");
	}

	auto instance = class_def->create_instance();

	auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);

	auto init_env = std::make_shared<environment>(definition_env, symbolizer_);
	init_env->define("this", this_value);

	if (matching_ctor->parameters.size() != args.size()) {
		return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
			"Constructor parameter count mismatch");
	}
	for (size_t i = 0; i < matching_ctor->parameters.size(); ++i) {
		init_env->define(std::string(matching_ctor->parameters[i].name), args[i]);
	}

	bool handled_parent_init = false;
	// C++ delegating-ctor semantics: the target ctor constructs the members; the
	// delegating ctor must not re-run field initializers afterward
	bool delegated_to_this = false;

	for (const auto& initializer : matching_ctor->initializers) {
		if (initializer.target == "super") {
			if (class_def->get_parent()) {
				std::vector<script_value> init_args;
				init_args.reserve(initializer.arguments.size());

				for (const auto& arg_expr : initializer.arguments) {
					auto result = eval_expression(arg_expr, init_env);
					if (!result) {
						return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
							"Failed to evaluate constructor initializer argument");
					}
					init_args.push_back(std::move(result.value()));
				}

				auto parent_class = class_def->get_parent();
				if (parent_class) {
					auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent_class);
					if (parent_script_class) {
						const auto& parent_ctor_asts = parent_script_class->get_constructor_asts();
						std::shared_ptr<function_decl> parent_ctor;
						for (const auto& ctor_ast : parent_ctor_asts) {
							if (ctor_ast->parameters.size() == init_args.size()) {
								parent_ctor = ctor_ast;
								break;
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
													return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
														"Failed to evaluate super() argument");
												}
												ancestor_args.push_back(std::move(r.value()));
											}

											auto ancestor_script = std::dynamic_pointer_cast<script_class_definition>(ancestor);
											if (ancestor_script) {
												const auto& ancestor_ctors = ancestor_script->get_constructor_asts();
												std::shared_ptr<function_decl> ancestor_ctor;
												for (const auto& ac : ancestor_ctors) {
													if (ac->parameters.size() == ancestor_args.size()) {
														ancestor_ctor = ac;
														break;
													}
												}
												if (ancestor_ctor) {
													ctor_chain.push_back({ancestor_script, ancestor_ctor, std::move(ancestor_args)});
												} else if (!ancestor_ctors.empty()) {
													return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
														"No matching constructor for ancestor class");
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
								for (size_t pi = 0; pi < it->ctor->parameters.size() && pi < it->args.size(); ++pi) {
									level_init_env->define(std::string(it->ctor->parameters[pi].name), it->args[pi]);
								}

								const auto& field_initializers = it->script_class->get_field_initializer_asts();
								for (const auto& [field_id, initializer_ast] : field_initializers) {
									if (initializer_ast) {
										auto r = eval_expression(initializer_ast, level_init_env);
										if (r) {
											instance->set_field(field_id, std::move(r.value()));
										}
									}
								}

								auto method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
								method_env->define("this", this_value);
								auto ctor_result = execute_method_ast(it->ctor, method_env, it->args);
								if (!ctor_result) return ctor_result.error_value();
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
					return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed),
						"Failed to evaluate constructor initializer argument");
				}
				init_args.push_back(std::move(result.value()));
			}

			const auto& all_ctor_asts = class_def->get_constructor_asts();
			std::shared_ptr<function_decl> target_ctor;
			for (const auto& ctor_ast : all_ctor_asts) {
				if (ctor_ast->parameters.size() == init_args.size() && ctor_ast != matching_ctor) {
					target_ctor = ctor_ast;
					break;
				}
			}

			if (!target_ctor) {
				return checked_result<script_value>(make_error_code(runtime_error_code::no_constructor_found),
					"No matching constructor found for this() delegation");
			}

			// Declared defaults BEFORE the delegated-to ctor body so the target can override
			evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
			delegated_to_this = true;

			auto target_method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
			target_method_env->define("this", this_value);

			auto target_result = execute_method_ast(target_ctor, target_method_env, init_args);
			if (!target_result) return target_result.error_value();
		}
	}

	if (!delegated_to_this) {
		evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);
	}

	auto method_env = std::make_shared<environment>(definition_env, symbolizer_, this_value);
	method_env->define("this", this_value);

	auto result = execute_method_ast(matching_ctor, method_env, args);

	return result;
}

checked_result<script_value> vm_backend::construct_default_instance(std::shared_ptr<script_class_definition> class_def,
                                                                    const std::vector<script_value>& args) {
	if (!args.empty()) {
		return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
			"Default constructor takes no arguments");
	}

	auto instance = class_def->create_instance();

	auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, engine_, true);

	std::unordered_set<environment*> visited;
	auto current_env = environment_;
	while (current_env && current_env->get_parent()) {
		if (visited.count(current_env.get()) > 0) {
			break;
		}
		visited.insert(current_env.get());
		current_env = current_env->get_parent();
	}
	auto global_env = current_env ? current_env : environment_;

	auto init_env = std::make_shared<environment>(global_env, symbolizer_);
	init_env->define("this", this_value);
	evaluate_field_initializers(instance, class_def, init_env);

	return this_value;
}

// ============================================================
// Exceptions, switch dispatch, range-for iteration
// ============================================================

checked_result<void> vm_backend::budget_exceeded_error() const {
	return checked_result<void>(
		make_error_code(runtime_error_code::execution_budget_exceeded),
		"Script execution budget exceeded - raise engine::execution_budget or break up the work");
}

checked_result<void> vm_backend::exec_throw(frame& f, const vm_instruction& ins) {
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

checked_result<void> vm_backend::exec_try_push(frame& f, const vm_instruction& ins) {
	try_record rec;
	rec.owner = &f;
	rec.handler_ip = ins.a;
	rec.catch_var = f.code->symbols[ins.b];
	rec.saved_exception = current_exception_;
	rec.saved_exception_value = active_exception_value_;
	rec.saved_catch_var_id = current_catch_var_id_;
	rec.saved_unwinding = is_unwinding_;
	rec.stack_size = stack_.size();
	rec.iter_size = iter_states_.size();
	rec.cfor_size = cfor_states_.size();
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

checked_result<void> vm_backend::exec_try_pop(frame&, const vm_instruction&) {
	if (try_records_.empty()) {
		return checked_result<void>(make_error_code(runtime_error_code::internal_error), "try record stack underflow");
	}
	current_catch_var_id_ = try_records_.back().saved_catch_var_id;
	try_records_.pop_back();
	return {};
}

checked_result<void> vm_backend::exec_catch_end(frame&, const vm_instruction&) {
	if (try_records_.empty()) {
		return checked_result<void>(make_error_code(runtime_error_code::internal_error), "try record stack underflow");
	}
	try_record rec = std::move(try_records_.back());
	try_records_.pop_back();
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
			stack_.erase(stack_.begin() + rec.stack_size, stack_.end());
		}
		if (iter_states_.size() > rec.iter_size) {
			iter_states_.erase(iter_states_.begin() + rec.iter_size, iter_states_.end());
		}
		if (cfor_states_.size() > rec.cfor_size) {
			cfor_states_.erase(cfor_states_.begin() + rec.cfor_size, cfor_states_.end());
		}
		environment_ = rec.entry_env;
		f.ip = rec.handler_ip;
		rec.in_catch = true;
		return true;
	}
	return false;
}

bool vm_backend::handle_op_error(frame*& fp, size_t records_base, const checked_result<void>& result) {
	error_propagator failure = result.error_value();
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

checked_result<void> vm_backend::exec_case_eq(frame&, const vm_instruction&) {
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

checked_result<void> vm_backend::exec_iter_init(frame&, const vm_instruction&) {
	script_value container = std::move(stack_.back());
	stack_.pop_back();

	environment_ = acquire_scope_env(environment_);

	iter_state state;
	if (container.is_array()) {
		state.container.emplace(std::move(container));
		state.index = 0;
		state.is_map = false;
	} else if (container.is_map()) {
		state.container.emplace(std::move(container));
		state.is_map = true;
		auto& map_storage = state.container->get_map_storage();
		state.map_it = map_storage->begin();
		if (!map_storage->empty()) {
			auto pair_result = environment_->get_ref(pair_id_);
			if (!pair_result) {
				pop_scope();
				return pair_result.error_value();
			}
			const script_value& pair_ctor = pair_result.value().get();
			if (!pair_ctor.is_function()) {
				pop_scope();
				return checked_result<void>(make_error_code(runtime_error_code::stdlib_not_loaded),
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
		pop_scope();
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	} else {
		pop_scope();
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}
	iter_states_.push_back(std::move(state));
	return {};
}

checked_result<void> vm_backend::exec_iter_next(frame& f, const vm_instruction& ins) {
	const iter_proto& proto = f.code->iter_protos[ins.a];
	if (iter_states_.empty()) {
		return checked_result<void>(make_error_code(runtime_error_code::internal_error), "iteration state stack underflow");
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
			return resume_result.error_value();
		}
		if (handle->done()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		script_value value = std::move(resume_result.value());
		if (proto.slot != SIZE_MAX && f.locals && !f.top_level) {
			f.locals->set_local(proto.slot, std::move(value));
		} else {
			environment_->define(proto.var_symbol, std::move(value));
		}
		stack_.push_back(script_value(true, engine_));
		return {};
	}

	script_value element = make_null();
	if (!state.is_map) {
		// Live size each iteration: the body can alias and shrink the shared array
		auto& array_storage = state.container->get_array_storage();
		if (state.index >= array_storage->size()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		if (execution_budget_exhausted()) [[unlikely]] {
			return budget_exceeded_error();
		}
		if (proto.is_reference) {
			// Reallocation-safe container+index reference, never a raw element pointer
			element = script_value::make_element_reference(array_storage, state.index, environment_, engine_, nullptr);
		} else {
			element = (*array_storage)[state.index].clone();
		}
		++state.index;
	} else {
		auto& map_storage = state.container->get_map_storage();
		if (state.map_it == map_storage->end()) {
			stack_.push_back(script_value(false, engine_));
			return {};
		}
		if (execution_budget_exhausted()) [[unlikely]] {
			return budget_exceeded_error();
		}
		std::vector<script_value> args;
		if (proto.is_reference) {
			script_value* value_ptr = const_cast<script_value*>(&state.map_it->second);
			args.push_back(state.map_it->first);
			args.push_back(script_value::make_reference(value_ptr, environment_, engine_));
		} else {
			args.push_back(state.map_it->first.clone());
			args.push_back(state.map_it->second.clone());
		}
		const script_function& pair_func = state.pair_ctor->as_function();
		auto pair_result = pair_func(args);
		if (!pair_result) {
			return pair_result.error_value();
		}
		element = std::move(pair_result.value());
		++state.map_it;
	}

	if (proto.slot != SIZE_MAX && f.locals && !f.top_level) {
		f.locals->set_local(proto.slot, std::move(element));
	} else {
		environment_->define(proto.var_symbol, std::move(element));
	}
	stack_.push_back(script_value(true, engine_));
	return {};
}

checked_result<void> vm_backend::exec_iter_pop(frame&, const vm_instruction&) {
	if (!iter_states_.empty()) {
		iter_states_.pop_back();
	}
	pop_scopes_pooled(1);
	return {};
}

checked_result<void> vm_backend::exec_include(frame& f, const vm_instruction& ins) {
	std::string path;
	if (ins.b) {
		script_value path_value = std::move(stack_.back());
		stack_.pop_back();
		const script_value& resolved = path_value.deref();
		if (resolved.type() != script_value_type::jai_string_type) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		path = resolved.as<std::string>();
	} else {
		path = std::string(f.code->messages[ins.a]);
	}
	if (!engine_) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}

	auto resolve_result = jai::resolve_include_path(path, engine_);
	if (!resolve_result) {
		return resolve_result.error_value();
	}
	std::string resolved_path = std::move(resolve_result.value());

	std::ifstream file(resolved_path);
	if (!file.is_open()) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string content = buffer.str();

	// Re-entrant into this backend's execute(); prepare_for_execution preserves any
	// running-coroutine state. An error in the included file surfaces as unwinding.
	try {
		script_value result = engine_->execute(content);
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

checked_result<void> vm_backend::exec_import(frame& f, const vm_instruction& ins) {
	std::string path;
	if (ins.b) {
		script_value path_value = std::move(stack_.back());
		stack_.pop_back();
		const script_value& resolved = path_value.deref();
		if (resolved.type() != script_value_type::jai_string_type) {
			return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
		}
		path = resolved.as<std::string>();
	} else {
		path = std::string(f.code->messages[ins.a]);
	}
	if (!engine_) {
		return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
	}

	auto resolve_result = jai::resolve_include_path(path, engine_);
	if (!resolve_result) {
		return resolve_result.error_value();
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

// ============================================================
// Dispatch loop
// ============================================================

checked_result<void> vm_backend::exec_extended(frame& f, const vm_instruction& ins) {
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
		case opcode::op_new: return exec_new(f, ins);
		case opcode::op_class_decl: return exec_class_decl(f, ins);
		case opcode::op_namespace_decl: return exec_namespace_decl(f, ins);
		case opcode::op_enum_decl: return exec_enum_decl(f, ins);
		case opcode::op_throw: return exec_throw(f, ins);
		case opcode::op_try_push: return exec_try_push(f, ins);
		case opcode::op_try_pop: return exec_try_pop(f, ins);
		case opcode::op_catch_end: return exec_catch_end(f, ins);
		case opcode::op_case_eq: return exec_case_eq(f, ins);
		case opcode::op_iter_init: return exec_iter_init(f, ins);
		case opcode::op_iter_next: return exec_iter_next(f, ins);
		case opcode::op_iter_pop: return exec_iter_pop(f, ins);
		case opcode::op_include: return exec_include(f, ins);
		case opcode::op_import: return exec_import(f, ins);
		default: return {};
	}
}

// Failed ops dispatch to an enclosing script catch handler in this frame (which
// resets f.ip) before propagating; `continue` re-enters the loop at the handler.
// Same single-temp shape as JAISCRIPT_TRY so run()'s Debug frame stays flat.
#define VM_TRY_OP(expr) \
	{ auto __result = (expr); \
	  if (!__result) [[unlikely]] { \
	      if (!handle_op_error(fp, records_base, __result)) return __result.error_value(); \
	      continue; } }

// Shared-slot variant for cases added after the frame-size ceiling was reached:
// reuses one function-scope temp so run()'s Debug frame stays flat (see the
// grouped exec_extended dispatch note below).
#define VM_TRY_OP_SHARED(expr) \
	{ shared_op_result_ = (expr); \
	  if (!shared_op_result_) [[unlikely]] { \
	      if (!handle_op_error(fp, records_base, shared_op_result_)) return shared_op_result_.error_value(); \
	      continue; } }

// Thin exception boundary: C++ exceptions thrown while in-loop frames are live get
// converted to script unwinding AT the failing logical frame (the native path converts
// them in exec_call at each recursion level); with no in-loop frames they rethrow
// byte-identically. Kept separate so its EH funclets never touch run_dispatch's frame.
checked_result<void> vm_backend::run(frame& entry) {
	frame* fp = &entry;
	const size_t records_base = call_records_top_;
	for (;;) {
		try {
			return run_dispatch(fp, records_base);
		} catch (const script_exception& e) {
			if (call_records_top_ == records_base) { throw; }
			convert_cpp_exception_at_frame(fp, e);
			if (!handle_throw_unwind(fp, records_base)) { return {}; }
		} catch (const std::exception& e) {
			if (call_records_top_ == records_base) { throw; }
			convert_cpp_exception_at_frame(fp, script_exception(e.what()));
			if (!handle_throw_unwind(fp, records_base)) { return {}; }
		} catch (...) {
			pop_records_to(records_base, fp);
			throw;
		}
	}
}

checked_result<void> vm_backend::run_dispatch(frame*& fp, const size_t records_base) {
	checked_result<void> shared_op_result_;
	for (;;) {
		// Rebound every iteration: frame switches write fp and `continue`
		frame& f = *fp;
		const auto& code = f.code->code;
		if (f.ip >= code.size()) {
			if (call_records_top_ == records_base) {
				return {};
			}
			VM_TRY_OP_SHARED(fall_off_script_frame(fp));
			continue;
		}
		const vm_instruction& ins = code[f.ip];
		switch (ins.op) {
			case opcode::op_halt:
				if (call_records_top_ == records_base) {
					return {};
				}
				// Function-body chunks end in op_halt: this is an in-loop callee's
				// implicit-return exit
				VM_TRY_OP_SHARED(fall_off_script_frame(fp));
				continue;

			case opcode::op_const: {
				// Literals are engine-less parse-time templates; re-materialize with the engine
				const script_value& tmpl = f.code->constants[ins.a];
				const auto& storage = tmpl.get_storage();
				switch (storage.index()) {
					case script_value::TYPEID_INT: stack_.push_back(script_value(std::get<script_int>(storage), engine_)); break;
					case script_value::TYPEID_FLOAT: stack_.push_back(script_value(std::get<script_float>(storage), engine_)); break;
					case script_value::TYPEID_STRING: stack_.push_back(script_value(*std::get<strong_ptr<script_string>>(storage), engine_)); break;
					case script_value::TYPEID_CHAR: stack_.push_back(script_value(std::get<script_char>(storage), engine_)); break;
					case script_value::TYPEID_BOOL: stack_.push_back(script_value(std::get<script_bool>(storage), engine_)); break;
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

			// Both always retarget f.ip
			case opcode::op_cfor_prep: VM_TRY_OP_SHARED(exec_cfor_prep(f, ins)); continue;
			case opcode::op_cfor_back: VM_TRY_OP_SHARED(exec_cfor_back(f, ins)); continue;
			case opcode::op_cfor_pop:
				if (!cfor_states_.empty()) { cfor_states_.pop_back(); }
				break;
			case opcode::op_index: VM_TRY_OP(exec_index(f, ins)); break;
			case opcode::op_index_assign: VM_TRY_OP(exec_index_assign(f, ins)); break;
			case opcode::op_index_compound: VM_TRY_OP(exec_index_compound(f, ins)); break;
			case opcode::op_unary: VM_TRY_OP(exec_unary(f, ins)); break;
			case opcode::op_call:
				VM_TRY_OP(exec_call(f, ins));
				if (switch_to_) {
					// Enter the pushed callee; skipping ++f.ip parks the suspended
					// caller's ip on the call op (stack-trace parity)
					fp = switch_to_;
					switch_to_ = nullptr;
					continue;
				}
				break;
			case opcode::op_func_decl: VM_TRY_OP(exec_func_decl(f, ins)); break;
			case opcode::op_closure: VM_TRY_OP(exec_closure(f, ins)); break;

			// One grouped dispatch: a VM_TRY_OP temp per case would bloat run()'s
			// Debug frame, which sits on the JAI_MAX_CALL_DEPTH recursion path
			case opcode::op_this:
			case opcode::op_super:
			case opcode::op_from_this:
			case opcode::op_get_member:
			case opcode::op_get_static:
			case opcode::op_set_member:
			case opcode::op_set_static:
			case opcode::op_member_compound:
			case opcode::op_call_method:
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
				if (execution_budget_exhausted()) [[unlikely]] {
					VM_TRY_OP(budget_exceeded_error());
				}
				f.ip = ins.a;
				continue;

			case opcode::op_return: {
				if (call_records_top_ == records_base) {
					if (ins.a) {
						return_value_ = std::move(stack_.back());
						stack_.pop_back();
					} else {
						return_value_ = make_null();
					}
					has_return_value_ = true;
					return {};
				}
				VM_TRY_OP_SHARED(return_from_script_frame(fp, ins));
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
				environment_ = acquire_scope_env(environment_);
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
	return run_program(compiler_.compile_program(declarations));
}

script_value vm_backend::execute(const std::vector<declaration_ptr>& declarations, std::shared_ptr<void>& compiled_slot) {
	auto program = std::static_pointer_cast<chunk>(compiled_slot);
	if (!program) {
		program = compiler_.compile_program(declarations);
		compiled_slot = program;
	}
	return run_program(std::move(program));
}

script_value vm_backend::run_program(std::shared_ptr<chunk> program) {
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
	checked_result<void> result;
	try {
		result = run(f);
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
		stack_.erase(stack_.begin() + f.stack_base, stack_.end());
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

std::shared_ptr<chunk> vm_backend::chunk_for_body(std::string_view name,
                                                  const std::vector<parameter>& params,
                                                  const std::shared_ptr<block_stmt>& body,
                                                  size_t local_count) {
	auto it = chunk_cache_.find(body.get());
	if (it != chunk_cache_.end()) {
		return it->second.compiled;
	}
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
			auto method_env = acquire_method_scope_env(payload.definition_env, *payload.this_obj);
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

checked_result<void> vm_backend::push_script_frame(frame& caller, script_value&& callee,
                                                   const script_defined_function& function,
                                                   const std::vector<script_value>& arguments,
                                                   std::vector<std::pair<uint64_t, environment*>>& saved_metadata,
                                                   bool has_args) {
	if (current_call_depth_ >= JAI_MAX_CALL_DEPTH) {
		if (has_args) { current_arg_metadata_ = std::move(saved_metadata); }
		return checked_result<void>(
			make_error_code(runtime_error_code::max_recursion_depth),
			"Maximum recursion depth ({0}) exceeded - possible infinite recursion",
			static_cast<uint64_t>(JAI_MAX_CALL_DEPTH));
	}
	if (execution_budget_exhausted()) [[unlikely]] {
		if (has_args) { current_arg_metadata_ = std::move(saved_metadata); }
		return checked_result<void>(
			make_error_code(runtime_error_code::execution_budget_exceeded),
			"Script execution budget exceeded - raise engine::execution_budget or break up the work");
	}
	// In-loop dispatch invariant: results travel on stack_, never through return_value_
	assert(!has_return_value_);

	std::shared_ptr<chunk> body_chunk;
	try {
		body_chunk = std::static_pointer_cast<chunk>(function.backend_body_cache);
		if (!body_chunk) {
			body_chunk = chunk_for_body(function.name, function.parameters, function.body, function.local_count);
			function.backend_body_cache = body_chunk;
		}
		if (call_records_top_ == call_records_.size()) {
			call_records_.push_back(std::make_unique<call_record>());
		}
	} catch (...) {
		if (has_args) { current_arg_metadata_ = std::move(saved_metadata); }
		throw;
	}

	call_record& rec = *call_records_[call_records_top_];
	++call_records_top_;
	rec.caller = &caller;
	rec.return_type = function.return_type;
	rec.callee_pin = std::move(callee);
	rec.prev_env = environment_;
	rec.try_base = try_records_.size();
	rec.iter_base = iter_states_.size();
	rec.cfor_base = cfor_states_.size();
	if (has_args) {
		std::swap(rec.saved_metadata, saved_metadata);   // swap reuses both capacities
		rec.metadata_saved = true;
	}
	rec.locals.function_name = function.name;
	rec.locals.reserve_locals(std::max(function.local_count, body_chunk->local_count));
	// Compile-time lazy elision: plain callees whose bodies provably never touch the
	// per-call scope env skip creating it (methods/statics never elide — env kind
	// fallbacks gate field-vs-shadowing precedence)
	rec.env_lazy = !body_chunk->needs_frame_env &&
	               (!function.closure_env ||
	                (!function.closure_env->is_method_env() && !function.closure_env->is_static_method_env()));
	try {
		if (rec.env_lazy) {
			if (function.closure_env) {
				rec.locals.closure_env = function.closure_env;
				environment_ = function.closure_env;
			} else {
				rec.locals.closure_env = rec.prev_env;   // ref-param frames_ scan needs it
			}
		} else {
			setup_callee_env(function, rec.locals, rec.prev_env);
		}
	} catch (...) {
		if (rec.metadata_saved) {
			current_arg_metadata_ = std::move(rec.saved_metadata);
			rec.metadata_saved = false;
		}
		rec.callee_pin = make_null();
		rec.return_type = nullptr;
		rec.prev_env = nullptr;
		--call_records_top_;
		throw;
	}
	++current_call_depth_;
	rec.f.code = body_chunk.get();
	rec.f.pin = std::move(body_chunk);
	rec.f.ip = 0;
	rec.f.locals = &rec.locals;
	if (rec.env_lazy) {
		rec.f.entry_env = nullptr;
	} else {
		rec.f.entry_env = environment_;
	}
	rec.f.stack_base = stack_.size();
	rec.f.top_level = false;
	frames_.push_back(&rec.f);   // before binding: the ref-param frames_ scan must see this frame

	checked_result<void> bound;
	try {
		bound = bind_parameters(function.parameters, arguments, rec.locals, *rec.f.code);
	} catch (...) {
		pop_script_frame_core(rec);
		throw;
	}
	if (!bound) {
		pop_script_frame_core(rec);
		return bound;
	}
	switch_to_ = &rec.f;
	return {};
}

checked_result<void> vm_backend::enter_script_method(frame& caller, script_value&& method_val,
                                                     const script_method_dispatch& dispatch,
                                                     const std::shared_ptr<function_decl>& ast,
                                                     script_value&& receiver,
                                                     const std::vector<script_value>& arguments,
                                                     const call_site& site) {
	const size_t argc = arguments.size();
	const bool has_args = argc > 0;
	std::vector<std::pair<uint64_t, environment*>> saved_metadata;
	if (has_args) {
		saved_metadata = std::move(current_arg_metadata_);
		current_arg_metadata_.clear();
		current_arg_metadata_.reserve(argc);
		for (size_t i = 0; i < argc; ++i) {
			uint64_t symbol_id = i < site.arg_symbols.size() ? site.arg_symbols[i] : UINT64_MAX;
			if (symbol_id != UINT64_MAX) {
				current_arg_metadata_.emplace_back(symbol_id, environment_.get());
			} else {
				current_arg_metadata_.emplace_back(UINT64_MAX, nullptr);
			}
		}
	}
	// push_method_frame owns saved_metadata restoration on every exit path
	try {
		return push_method_frame(caller, std::move(method_val), dispatch, ast, std::move(receiver),
		                         arguments, saved_metadata, has_args);
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

checked_result<void> vm_backend::push_method_frame(frame& caller, script_value&& method_val,
                                                   const script_method_dispatch& dispatch,
                                                   const std::shared_ptr<function_decl>& ast,
                                                   script_value&& receiver,
                                                   const std::vector<script_value>& arguments,
                                                   std::vector<std::pair<uint64_t, environment*>>& saved_metadata,
                                                   bool has_args) {
	if (current_call_depth_ >= JAI_MAX_CALL_DEPTH) {
		if (has_args) { current_arg_metadata_ = std::move(saved_metadata); }
		return checked_result<void>(
			make_error_code(runtime_error_code::max_recursion_depth),
			"Maximum recursion depth ({0}) exceeded - possible infinite recursion",
			static_cast<uint64_t>(JAI_MAX_CALL_DEPTH));
	}
	if (execution_budget_exhausted()) [[unlikely]] {
		if (has_args) { current_arg_metadata_ = std::move(saved_metadata); }
		return checked_result<void>(
			make_error_code(runtime_error_code::execution_budget_exceeded),
			"Script execution budget exceeded - raise engine::execution_budget or break up the work");
	}
	assert(!has_return_value_);

	std::shared_ptr<chunk> body_chunk;
	try {
		if (dispatch.body_cache_key == ast->body.get()) {
			body_chunk = std::static_pointer_cast<chunk>(dispatch.body_cache);
		} else {
			// Native parity: execute_method_ast's temp function carries local_count 0
			body_chunk = chunk_for_body(ast->name, ast->parameters, ast->body, 0);
			dispatch.body_cache = body_chunk;
			dispatch.body_cache_key = ast->body.get();
		}
		if (call_records_top_ == call_records_.size()) {
			call_records_.push_back(std::make_unique<call_record>());
		}
	} catch (...) {
		if (has_args) { current_arg_metadata_ = std::move(saved_metadata); }
		throw;
	}

	call_record& rec = *call_records_[call_records_top_];
	++call_records_top_;
	rec.caller = &caller;
	rec.return_type = ast->return_type;
	rec.ast_pin = ast;   // the resolved overload must outlive a mid-call hot reload
	rec.method_result_anchor = true;
	rec.callee_pin = std::move(method_val);   // pins the dispatcher and, through it, the class
	rec.prev_env = environment_;
	rec.try_base = try_records_.size();
	rec.iter_base = iter_states_.size();
	rec.cfor_base = cfor_states_.size();
	if (has_args) {
		std::swap(rec.saved_metadata, saved_metadata);
		rec.metadata_saved = true;
	}
	rec.locals.function_name = ast->name;
	rec.locals.reserve_locals(body_chunk->local_count);
	rec.env_lazy = false;   // method envs never elide (env-kind fallbacks gate precedence)
	try {
		// Net effect of the native wrapper-env round trip: a method scope parented on
		// definition_env with the receiver bound; the wrapper env and its define(this)
		// are bypassed dead weight
		rec.locals.set_this(receiver);
		rec.locals.closure_env = dispatch.definition_env;
		environment_ = acquire_method_scope_env(dispatch.definition_env, std::move(receiver));
	} catch (...) {
		if (rec.metadata_saved) {
			current_arg_metadata_ = std::move(rec.saved_metadata);
			rec.metadata_saved = false;
		}
		rec.callee_pin = make_null();
		rec.return_type = nullptr;
		rec.ast_pin.reset();
		rec.method_result_anchor = false;
		rec.locals.this_object_ptr.reset();
		rec.locals.is_method = false;
		rec.locals.closure_env = nullptr;
		rec.prev_env = nullptr;
		--call_records_top_;
		throw;
	}
	++current_call_depth_;
	rec.f.code = body_chunk.get();
	rec.f.pin = std::move(body_chunk);
	rec.f.ip = 0;
	rec.f.locals = &rec.locals;
	rec.f.entry_env = environment_;
	rec.f.stack_base = stack_.size();
	rec.f.top_level = false;
	frames_.push_back(&rec.f);   // before binding: the ref-param frames_ scan must see this frame

	checked_result<void> bound;
	try {
		bound = bind_parameters(ast->parameters, arguments, rec.locals, *rec.f.code);
	} catch (...) {
		pop_script_frame_core(rec);
		throw;
	}
	if (!bound) {
		pop_script_frame_core(rec);
		return bound;
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
	if (rec.env_lazy) {
		// Single-level rule: prev_env plays the never-created entry env's parent, so only
		// the immediate env is checked (reproduces the pinned caller-this-clear quirk;
		// a parent check would look two levels up and diverge for block-scope callers)
		if (environment_->is_method_env()) {
			environment_->clear_this_reference();
		}
	} else {
		clear_this_on_frame_exit();
	}
	environment_ = std::move(rec.prev_env);
	if (stack_.size() > rec.f.stack_base) {
		stack_.erase(stack_.begin() + rec.f.stack_base, stack_.end());
	}
	if (try_records_.size() > rec.try_base) {
		try_records_.erase(try_records_.begin() + rec.try_base, try_records_.end());
	}
	if (iter_states_.size() > rec.iter_base) {
		iter_states_.erase(iter_states_.begin() + rec.iter_base, iter_states_.end());
	}
	if (cfor_states_.size() > rec.cfor_base) {
		cfor_states_.erase(cfor_states_.begin() + rec.cfor_base, cfor_states_.end());
	}
	if (!rec.env_lazy) {
		// Moved out first so the pool's use_count()==1 guard sees today's count
		release_scope_env(std::move(rec.f.entry_env));
	} else {
		rec.f.entry_env = nullptr;
	}
	rec.locals.locals.clear();   // destroy callee locals now, keep capacity
	rec.locals.closure_env = nullptr;
	rec.locals.this_object_ptr.reset();
	rec.locals.is_method = false;
	rec.locals.static_class_def = nullptr;
	rec.locals.is_static_method = false;
	rec.callee_pin = make_null();
	rec.return_type = nullptr;
	rec.ast_pin.reset();
	rec.method_result_anchor = false;
	rec.f.pin.reset();
	rec.f.code = nullptr;
	if (rec.metadata_saved) {
		current_arg_metadata_ = std::move(rec.saved_metadata);
		rec.metadata_saved = false;
	}
	--current_call_depth_;
	frames_.pop_back();
	--call_records_top_;
}

checked_result<void> vm_backend::return_from_script_frame(frame*& fp, const vm_instruction& ins) {
	call_record& rec = *call_records_[call_records_top_ - 1];
	script_value result = make_null();
	if (ins.a) {
		result = std::move(stack_.back());
		stack_.pop_back();
	}
	// Deref + conversion run while the callee's env/frame are still live (native order)
	auto conv = convert_return_value(std::move(result), rec.return_type);
	if (!conv) {
		pop_script_frame_core(rec);
		fp = rec.caller;
		return conv.error_value();
	}
	if (rec.method_result_anchor) {
		anchor_method_result(conv.value(), rec.locals.get_this());
	}
	pop_script_frame_core(rec);
	fp = rec.caller;
	stack_.push_back(std::move(conv.value()));
	++fp->ip;
	return {};
}

checked_result<void> vm_backend::fall_off_script_frame(frame*& fp) {
	call_record& rec = *call_records_[call_records_top_ - 1];
	script_value result = implicit_result_for_record(rec);   // conversion skipped: fall-off parity
	pop_script_frame_core(rec);
	fp = rec.caller;
	stack_.push_back(std::move(result));
	++fp->ip;
	return {};
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
			locals.closure_env = function.closure_env->get_parent();
			environment_ = acquire_method_scope_env(function.closure_env->get_parent(), std::move(this_obj));
		} else if (function.closure_env->is_static_method_env()) {
			locals.static_class_def = function.closure_env->get_class_definition();
			locals.is_static_method = true;
			locals.closure_env = function.closure_env->get_parent();
			environment_ = acquire_static_scope_env(
				function.closure_env->get_parent(), function.closure_env->get_class_definition());
		} else {
			locals.closure_env = function.closure_env;
			environment_ = acquire_scope_env(function.closure_env);
		}
	} else {
		locals.closure_env = prev_env;
		environment_ = acquire_scope_env(prev_env);
	}
}

// The parent branch also clears a CALLER method scope's this-binding when the exiting
// frame is a plain function called from a method body — a parity-locked quirk both
// backends share (pinned by "pinned_quirk_callee_clears_caller_this")
void vm_backend::clear_this_on_frame_exit() {
	auto function_env = environment_;
	if (function_env->is_method_env()) {
		function_env->clear_this_reference();
	} else if (function_env->get_parent() && function_env->get_parent()->is_method_env()) {
		function_env->get_parent()->clear_this_reference();
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

checked_result<script_value> vm_backend::convert_return_value(script_value result, const type_info_ptr& return_type) {
	if (result.is_reference()) {
		// References into this call frame would dangle once the frame dies
		result = result.deref();
	}
	if (return_type && !return_type->type_name.empty() &&
	    return_type->type_name != "void" &&
	    return_type->type_name != "auto" &&
	    return_type->base_type != script_value_type::jai_any_type) {
		auto conv = try_convert_for_parameter(result, return_type);
		if (!conv) {
			return conv.error_value();
		}
		result = std::move(conv.value());
	}
	return result;
}

checked_result<void> vm_backend::bind_parameters(const std::vector<parameter>& parameters,
                                                 const std::vector<script_value>& args,
                                                 call_frame& locals, chunk& body_chunk) {
	for (size_t i = 0; i < parameters.size(); ++i) {
		const auto& param = parameters[i];

		if (i >= args.size()) {
			if (param.default_value) {
				auto default_chunk = i < body_chunk.param_default_chunks.size() ? body_chunk.param_default_chunks[i] : nullptr;
				if (!default_chunk) {
					return checked_result<void>(make_error_code(runtime_error_code::internal_error), "Missing compiled default argument");
				}
				frame df;
				df.code = default_chunk.get();
				df.pin = default_chunk;
				df.ip = 0;
				df.locals = &locals;
				df.entry_env = environment_;
				df.stack_base = stack_.size();
				df.top_level = false;
				auto dr = run(df);
				if (!dr) {
					return dr;
				}
				script_value default_val = std::move(stack_.back());
				stack_.pop_back();
				locals.set_local(param.slot_index, std::move(default_val));
				continue;
			}
		}

		const auto& arg = args[i];

		if (param.is_reference) {
			if (!current_arg_metadata_.empty() && i < current_arg_metadata_.size()) {
				auto symbol_id = current_arg_metadata_[i].first;
				auto env = current_arg_metadata_[i].second;

				if (symbol_id != UINT64_MAX && env != nullptr) {
					script_value* argPtr = env->get_value_ptr(symbol_id);
					if (!argPtr) {
						return checked_result<void>(
							make_error_code(runtime_error_code::undefined_variable),
							"Cannot take reference of undefined variable");
					}

					if (argPtr->is_reference()) {
						auto refHolder = argPtr->get_reference_holder();
						if (!refHolder || !refHolder->target) {
							return checked_result<void>(
								make_error_code(runtime_error_code::invalid_reference),
								"Reference target is null");
						}
						script_value refValue = script_value::make_reference(refHolder->target, refHolder->sourceEnv.lock());
						locals.set_local(param.slot_index, std::move(refValue));
					} else {
						std::shared_ptr<environment> env_shared;
						if (env == environment_.get()) {
							env_shared = environment_;
						} else {
							for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
								frame* fr = *it;
								if (fr->locals && fr->locals->closure_env.get() == env) {
									env_shared = fr->locals->closure_env;
									break;
								}
							}
							if (!env_shared && engine_) {
								auto global_env = engine_->get_global_environment();
								if (global_env.get() == env) {
									env_shared = global_env;
								}
							}
						}
						script_value refValue = script_value::make_reference(argPtr, env_shared);
						locals.set_local(param.slot_index, std::move(refValue));
					}
				} else {
					return checked_result<void>(
						make_error_code(runtime_error_code::invalid_reference),
						"Cannot pass non-lvalue to reference parameter");
				}
			} else {
				// External (C++) invocation: object values are handles, shallow copy aliases
				auto arg_type = arg.current_type();
				if (arg_type == script_value_type::jai_object_type ||
				    arg_type == script_value_type::jai_shared_ptr_type) {
					locals.set_local(param.slot_index, script_value(arg));
				} else {
					return checked_result<void>(
						make_error_code(runtime_error_code::invalid_reference),
						"Cannot pass non-lvalue to reference parameter");
				}
			}
		} else {
			// auto (inferred) parameter + primitive argument: copy IS clone for
			// primitives and carries the same type_info the inference locks onto,
			// so the conversion machinery has nothing to do. var (any-typed) and
			// explicitly typed parameters take the full path.
			const size_t ri = arg.raw_storage_index();
			if (!param.type && !arg.is_cpp_bound() &&
			    (ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT ||
			     ri == script_value::TYPEID_BOOL || ri == script_value::TYPEID_CHAR)) {
				locals.set_local(param.slot_index, script_value(arg));
				continue;
			}

			auto converted_result = try_convert_for_parameter(arg, param.type);
			if (!converted_result) {
				return converted_result.error_value();
			}
			script_value converted_arg = std::move(converted_result.value());

			bool should_share = false;
			if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) {
				should_share = true;
			}
			if (converted_arg.get_type_info() && converted_arg.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
				should_share = true;
			}

			if (should_share) {
				locals.set_local(param.slot_index, converted_arg);
			} else {
				locals.set_local(param.slot_index, converted_arg.clone());
			}
		}
	}
	return {};
}

checked_result<script_value> vm_backend::call_script_function(const script_defined_function& function, const std::vector<script_value>& args) {
	if (current_call_depth_ >= JAI_MAX_CALL_DEPTH) {
		return checked_result<script_value>(
			make_error_code(runtime_error_code::max_recursion_depth),
			"Maximum recursion depth ({0}) exceeded - possible infinite recursion",
			static_cast<uint64_t>(JAI_MAX_CALL_DEPTH));
	}

	if (execution_budget_exhausted()) [[unlikely]] {
		return checked_result<script_value>(
			make_error_code(runtime_error_code::execution_budget_exceeded),
			"Script execution budget exceeded - raise engine::execution_budget or break up the work");
	}

	struct call_depth_guard {
		int& depth;
		call_depth_guard(int& d) : depth(d) { ++depth; }
		~call_depth_guard() { --depth; }
	} depth_guard(current_call_depth_);

	if (args.size() != function.parameters.size()) {   // exact arity: no default-arg scan needed
		size_t required_params = 0;
		for (const auto& p : function.parameters) {
			if (!p.default_value) {
				++required_params;
			} else {
				break;
			}
		}
		if (args.size() < required_params || args.size() > function.parameters.size()) {
			return checked_result<script_value>(
				make_error_code(runtime_error_code::argument_count_mismatch),
				"Function expected {0} arguments but got {1}",
				static_cast<uint64_t>(function.parameters.size()), static_cast<uint64_t>(args.size()));
		}
	}

	auto body_chunk = std::static_pointer_cast<chunk>(function.backend_body_cache);
	if (!body_chunk) {
		body_chunk = chunk_for_body(function.name, function.parameters, function.body, function.local_count);
		function.backend_body_cache = body_chunk;
	}

	call_frame locals;
	locals.function_name = function.name;
	locals.locals = acquire_arg_vector(std::max(function.local_count, body_chunk->local_count));

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
	f.stack_base = stack_.size();
	f.top_level = false;

	frame_guard guard(this, &f);

	auto cleanup = [&]() {
		if (is_unwinding_ && !trace_captured_) capture_stack_trace();
		clear_this_on_frame_exit();
		environment_ = previousEnv;
		has_return_value_ = previousHasReturn;
		return_value_ = std::move(previousReturn);
		if (stack_.size() > f.stack_base) {
			stack_.erase(stack_.begin() + f.stack_base, stack_.end());
		}
		release_scope_env(std::move(f.entry_env));
		release_arg_vector(std::move(locals.locals));
	};

	{
		auto bind_result = bind_parameters(function.parameters, args, locals, *body_chunk);
		if (!bind_result) {
			cleanup();
			return bind_result.error_value();
		}
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
		auto conv = convert_return_value(std::move(return_value_.value()), function.return_type);
		if (!conv) {
			cleanup();
			return conv.error_value();
		}
		result = std::move(conv.value());
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
