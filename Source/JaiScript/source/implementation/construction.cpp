#include <jaiscript/detail/construction.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/aggregate_init.hpp>
#include <jaiscript/detail/arg_capture.hpp>

namespace jai::detail {

void stamp_literal_field_initializers(class_instance& instance, const script_class_definition& cls, bool skip_parent_recursion, engine* eng) {
	if (!skip_parent_recursion) {
		for (const auto& parent : cls.get_parent_classes()) {
			if (parent && parent->is_script_class()) {
				stamp_literal_field_initializers(instance, static_cast<const script_class_definition&>(*parent), false, eng);
			}
		}
	}
	for (const auto& [field_id, initializer_ast] : cls.get_field_initializer_asts()) {
		if (!initializer_ast) { continue; }
		const auto& storage = static_cast<const literal_expr*>(initializer_ast.get())->value.get_storage();
		switch (storage.index()) {
			case script_value::TYPEID_INT: instance.set_field(field_id, script_value(storage.get<script_int>(), eng)); break;
			case script_value::TYPEID_FLOAT: instance.set_field(field_id, script_value(storage.get<script_float>(), eng)); break;
			case script_value::TYPEID_CHAR: instance.set_field(field_id, script_value(storage.get<script_char>(), eng)); break;
			case script_value::TYPEID_BOOL: instance.set_field(field_id, script_value(storage.get<script_bool>(), eng)); break;
			default: instance.set_field(field_id, script_value(std::monostate{}, eng)); break;
		}
	}
}

checked_result<void> construction_host::initialize_level_fields(class_instance& instance, const std::shared_ptr<script_class_definition>& level,
                                                                construction_scope& scope) {
	for (const auto& [field_id, initializer_ast] : level->get_field_initializer_asts()) {
		if (!initializer_ast) { continue; }
		auto evaluated = evaluate_in(scope, initializer_ast);
		if (unwinding) { return {}; }
		if (!evaluated) { return evaluated.error_value(); }
		instance.set_field(field_id, initializer_store_value(std::move(evaluated.value()), initializer_ast.get()));
	}
	return {};
}

void construction_host::open_scope(construction_scope& scope, const std::shared_ptr<environment>& definition_env, const script_value& this_value,
                                   script_class_definition* level, const function_decl* ctor, const std::vector<script_value>* args, bool bind_for_parameters) {
	scope.env = std::make_shared<environment>(definition_env, env_symbols);
	scope.env->define(symbols->get_this_id(), this_value);
	scope.env->set_access_context(level);
	if (!ctor || !args) { return; }
	for (size_t i = 0; i < ctor->parameters.size() && i < args->size(); ++i) {
		const parameter& param = ctor->parameters[i];
		if (param.symbol_id == UINT64_MAX) { param.symbol_id = symbols->intern(param.name); }
		scope.env->define(param.symbol_id, bind_for_parameters ? argument_for_binding((*args)[i], parameter_binding(param)) : (*args)[i]);
	}
}

JAI_NOINLINE std::optional<error_propagator> refuse_cdtor_call(class_instance* instance, uint64_t name_id) {
	if (!instance) { return std::nullopt; }
	auto phase = instance->dispatch_phase();
	if (phase == class_instance::cdtor_phase::none && instance->construction_pending()) { phase = class_instance::cdtor_phase::construction; }
	const class_definition* level = instance->get_class_definition();
	engine* eng = instance->get_engine();
	if (phase == class_instance::cdtor_phase::none || !level || !eng || !level->get_method(name_id, false).is_invalid()) { return std::nullopt; }
	for (const auto& [name, cls] : eng->get_class_registry().script_classes_) {
		if (cls.get() == level || !cls->derives_from(level) || cls->get_method(name_id, false).is_invalid()) { continue; }
		return error_propagator{make_error_code(runtime_error_code::member_not_found),
			phase == class_instance::cdtor_phase::construction ? k_call_under_construction : k_call_under_destruction,
			name_id, level->get_type_id()};
	}
	return std::nullopt;
}

}
