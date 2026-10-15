#include <jaiscript/detail/construction.hpp>
#include <jaiscript/core/engine.hpp>
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

namespace {
	std::shared_ptr<environment> construction_member_environment(construction_host& host, const std::shared_ptr<environment>& parent,
	                                                             const script_value& this_value, class_definition* access) {
		auto result = std::make_shared<environment>(parent, host.symbols);
		result->define(host.symbols->get_this_id(), this_value);
		result->set_access_context(access);
		return result;
	}

	std::shared_ptr<environment> construction_level_environment(construction_host& host, const std::shared_ptr<environment>& definition_env,
	                                                            const script_value& this_value, script_class_definition* level,
	                                                            const function_decl& ctor, const std::vector<script_value>& args) {
		auto result = construction_member_environment(host, definition_env, this_value, level);
		for (size_t i = 0; i < ctor.parameters.size() && i < args.size(); ++i) {
			result->define(ctor.parameters[i].name, args[i]);
		}
		return result;
	}

	// an initializer list's arguments, each taken as it evaluates; a failure other than the depth limit raises a_failure
	checked_result<void> evaluate_initializer_arguments(construction_host& host, const constructor_initializer& initializer, const std::shared_ptr<environment>& env,
	                                                    bool fields_run_code, const class_definition* binding_class, const char* a_failure,
	                                                    std::vector<script_value>& result) {
		result.reserve(initializer.arguments.size());
		for (const auto& arg_expr : initializer.arguments) {
			auto evaluated = host.evaluate(arg_expr, env);
			if (!evaluated) {
				if (evaluated.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
					return evaluated.error_value();
				}
				return checked_result<void>(make_error_code(runtime_error_code::evaluation_failed), a_failure);
			}
			result.push_back(std::move(evaluated.value()));
			capture_initializer_argument(initializer, result.size() - 1, fields_run_code, result.back(),
			                             [&] { return constructor_binding(binding_class, result.size() - 1); });
			if (host.unwinding) { return {}; }
		}
		return {};
	}

	// a base constructor through the one script-overload scorer, else the implicit default constructor a base needing construction has
	checked_result<std::shared_ptr<function_decl>> select_base_constructor(script_class_definition& base, const std::vector<script_value>& args, engine* eng) {
		const auto& ctors = base.get_constructor_asts();
		auto picked = select_declaration(ctors, args, base.get_type_id(), eng);
		if (!picked) { return picked.error_value(); }
		if (!picked.value() && ctors.empty() && args.empty() && base.base_needs_construction()) {
			return base.implicit_default_constructor();
		}
		return picked;
	}

	void adopt_host_base_object(class_instance& instance, const script_value& host_object) {
		if (!host_object.is_object()) { return; }
		auto host_instance = host_object.as<std::shared_ptr<class_instance>>();
		if (!host_instance) { return; }
		if (const script_value* native = host_instance->native()) {
			instance.set_native(*native);
		}
	}

	// A C++ base's constructor makes the object the instance adopts; a_ancestor marks a base above the direct one
	checked_result<void> construct_host_base(construction_host& host, class_instance& instance, const class_definition& base,
	                                         const std::vector<script_value>& args, bool a_ancestor) {
		const std::string& name = base.get_name();
		auto ctor = host.current_environment()->get(name);
		if (args.empty() && (!ctor || !ctor.value().is_function() || host_lacks_default_constructor(host.eng, name))) {
			return no_default_constructor(base.get_type_id());
		}
		if (!ctor || !ctor.value().is_function()) { return {}; }
		script_value host_ctor = std::move(ctor.value());
		auto constructed = host_ctor.as_function()(args);
		if (!constructed) {
			if (args.empty() && is_constructor_miss(constructed.error())) {
				return no_default_constructor(base.get_type_id());
			}
			if (a_ancestor) {
				return checked_result<void>(make_error_code(runtime_error_code::constructor_failed), "Failed to call C++ ancestor constructor");
			}
			return constructed.error_value();
		}
		adopt_host_base_object(instance, constructed.value());
		return {};
	}

	struct constructor_chain_entry {
		std::shared_ptr<script_class_definition> script_class;
		std::shared_ptr<function_decl> ctor;
		std::vector<script_value> args;
	};

	// A script base with a constructor: walk the super() chain up to a C++ base (constructed on the way), then run each level's
	// field initializers and constructor body root to leaf
	checked_result<void> construct_script_bases(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                            const std::shared_ptr<environment>& definition_env, class_instance& instance,
	                                            const script_value& this_value, std::vector<constructor_chain_entry>& chain,
	                                            std::optional<call_depth_guard>& initializer_depth) {
		const bool fields_run_code = !cls->chain_initializers_all_literal();
		for (size_t level = 0; level < chain.size(); ++level) {
			auto& entry = chain[level];
			for (const auto& init : entry.script_class->effective_initializers(*entry.ctor)) {
				if (init.target != "super") { continue; }
				auto ancestor = entry.script_class->get_parent();
				if (ancestor) {
					auto level_env = construction_level_environment(host, definition_env, this_value, entry.script_class.get(), *entry.ctor, entry.args);
					std::vector<script_value> ancestor_args;
					auto evaluated = evaluate_initializer_arguments(host, init, level_env, fields_run_code, ancestor.get(), "Failed to evaluate super() argument", ancestor_args);
					if (!evaluated || host.unwinding) { return evaluated; }
					if (auto ancestor_script = std::dynamic_pointer_cast<script_class_definition>(ancestor)) {
						auto ancestor_ctor = select_base_constructor(*ancestor_script, ancestor_args, host.eng);
						if (!ancestor_ctor) { return ancestor_ctor.error_value(); }
						if (ancestor_ctor.value()) {
							chain.push_back({ancestor_script, std::move(ancestor_ctor.value()), std::move(ancestor_args)});
						} else if (!ancestor_script->get_constructor_asts().empty()) {
							if (ancestor_args.empty()) { return no_default_constructor(ancestor_script->get_type_id()); }
							return overload_miss(make_error_code(runtime_error_code::no_constructor_found),
								ancestor_script->get_type_id(), ancestor_args.data(), ancestor_args.size(), host.eng, true);
						}
					} else {
						auto constructed = construct_host_base(host, instance, *ancestor, ancestor_args, true);
						if (!constructed || host.unwinding) { return constructed; }
					}
				}
				break;
			}
		}
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			auto level_env = construction_level_environment(host, definition_env, this_value, it->script_class.get(), *it->ctor, it->args);
			for (const auto& [field_id, initializer_ast] : it->script_class->get_field_initializer_asts()) {
				if (!initializer_ast) { continue; }
				auto evaluated = host.evaluate(initializer_ast, level_env);
				if (host.unwinding) { return {}; }
				if (evaluated) {
					instance.set_field(field_id, initializer_store_value(std::move(evaluated.value()), initializer_ast.get()));
				} else if (evaluated.error() == make_error_code(runtime_error_code::max_recursion_depth)) {
					return evaluated.error_value();
				}
			}
			initializer_depth.reset();
			auto body = host.run_constructor_body(it->ctor, definition_env, this_value, it->script_class.get(), it->args);
			if (!body) { return body.error_value(); }
			if (host.unwinding) { return {}; }
			initializer_depth.emplace(host.call_depth);
		}
		return {};
	}

	checked_result<void> construct_base(construction_host& host, const std::shared_ptr<script_class_definition>& cls, const constructor_initializer& initializer,
	                                    const std::shared_ptr<environment>& definition_env, const std::shared_ptr<environment>& init_env,
	                                    class_instance& instance, const script_value& this_value,
	                                    std::optional<call_depth_guard>& initializer_depth, bool& handled_parent_init) {
		auto parent = cls->get_parent();
		if (!parent) {
			return checked_result<void>(make_error_code(runtime_error_code::no_constructor_found), "Cannot call super() - class has no base class");
		}
		std::vector<script_value> init_args;
		auto evaluated = evaluate_initializer_arguments(host, initializer, init_env, !cls->chain_initializers_all_literal(), parent.get(),
		                                                "Failed to evaluate constructor initializer argument", init_args);
		if (!evaluated || host.unwinding) { return evaluated; }
		auto parent_script = std::dynamic_pointer_cast<script_class_definition>(parent);
		if (!parent_script) {
			try {
				return construct_host_base(host, instance, *parent, init_args, false);
			} catch (const runtime_error&) {
				return checked_result<void>(make_error_code(runtime_error_code::constructor_failed), "Failed to call C++ parent constructor");
			}
		}
		auto parent_ctor = select_base_constructor(*parent_script, init_args, host.eng);
		if (!parent_ctor) { return parent_ctor.error_value(); }
		const bool parent_declares_constructors = !parent_script->get_constructor_asts().empty();
		if (parent_ctor.value()) {
			std::vector<constructor_chain_entry> chain;
			chain.push_back({parent_script, std::move(parent_ctor.value()), std::move(init_args)});
			auto constructed = construct_script_bases(host, cls, definition_env, instance, this_value, chain, initializer_depth);
			if (!constructed) { return constructed; }
			handled_parent_init = true;
			return {};
		}
		if (!parent_declares_constructors && init_args.empty()) {
			return {};
		}
		if (parent_declares_constructors && !init_args.empty()) {
			return overload_miss(make_error_code(runtime_error_code::no_constructor_found),
				parent_script->get_type_id(), init_args.data(), init_args.size(), host.eng, true);
		}
		if (init_args.empty()) { return no_default_constructor(parent_script->get_type_id()); }
		return checked_result<void>(make_error_code(runtime_error_code::no_constructor_found), "No matching parent constructor found for super()");
	}
}

checked_result<script_value> initialize_members(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
                                                const std::shared_ptr<environment>& definition_env, const std::shared_ptr<class_instance>& instance,
                                                const script_value& this_value, const std::shared_ptr<function_decl>& ctor,
                                                const std::vector<script_value>& args, std::optional<call_depth_guard>& initializer_depth,
                                                size_t delegation_depth, std::vector<script_value>* captured_out) {
	if (delegation_depth > k_max_constructor_delegation_depth) {
		return raise_call_depth();   // a delegation cycle (ill-formed in C++) raises instead of recursing without end
	}
	if (native_stack_low()) [[unlikely]] {
		return raise_native_stack();
	}
	// omitted parameters stay absent here, as C++ field initializers cannot see constructor parameters
	if (!arity_accepts(ctor->parameters, args.size())) {
		return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "Constructor parameter count mismatch");
	}
	// the parameters' ids, interned once: the body's binding reads them
	for (const auto& param : ctor->parameters) {
		if (param.symbol_id == UINT64_MAX) { param.symbol_id = host.symbols->intern(param.name); }
	}
	const auto& initializers = cls->effective_initializers(*ctor);
	const bool all_literal = cls->chain_initializers_all_literal();
	std::shared_ptr<environment> init_env;
	if (!initializers.empty() || !all_literal) {
		init_env = construction_member_environment(host, definition_env, this_value, cls.get());
		// the construction runs code ahead of the body: the call's arguments are taken first, as the body's parameters; a
		// delegated-to constructor's arguments were taken as they evaluated
		const std::vector<script_value>* argv = &args;
		if (captured_out && later_capture_candidate(0, args.size(), [&](size_t j) -> const script_value& { return args[j]; })) {
			*captured_out = args;
			for (size_t j = 0; j < captured_out->size(); ++j) {
				capture_argument((*captured_out)[j], transient_read_marker::arg_capture_full, parameters_binding(ctor->parameters, j));
			}
			argv = captured_out;
		}
		for (size_t i = 0; i < ctor->parameters.size() && i < argv->size(); ++i) {
			const auto& param = ctor->parameters[i];
			init_env->define(param.symbol_id, argument_for_binding((*argv)[i], parameter_binding(param)));
		}
	}

	bool handled_parent_init = false;
	// C++ delegation: the target constructor constructs the members, so the delegating one re-runs no field initializer
	bool delegated_to_this = false;
	for (const auto& initializer : initializers) {
		if (initializer.target == "super") {
			auto constructed = construct_base(host, cls, initializer, definition_env, init_env, *instance, this_value, initializer_depth, handled_parent_init);
			if (!constructed) { return constructed.error_value(); }
			if (host.unwinding) { return this_value; }
		} else if (initializer.target == "this") {
			std::vector<script_value> init_args;
			auto evaluated = evaluate_initializer_arguments(host, initializer, init_env, !all_literal, cls.get(),
			                                                "Failed to evaluate constructor initializer argument", init_args);
			if (!evaluated) { return evaluated.error_value(); }
			if (host.unwinding) { return this_value; }
			std::vector<std::shared_ptr<function_decl>> other_ctors;
			for (const auto& candidate : cls->get_constructor_asts()) { if (candidate != ctor) { other_ctors.push_back(candidate); } }
			auto target_pick = select_declaration(other_ctors, init_args, cls->get_type_id(), host.eng);
			if (!target_pick) { return target_pick.error_value(); }
			std::shared_ptr<function_decl> target = std::move(target_pick.value());
			if (!target) {
				return overload_miss(make_error_code(runtime_error_code::no_constructor_found),
					cls->get_type_id(), init_args.data(), init_args.size(), host.eng, true);
			}
			// the target runs whole: its own initializer list, the members, then its body
			auto target_members = initialize_members(host, cls, definition_env, instance, this_value, target, init_args,
			                                         initializer_depth, delegation_depth + 1, nullptr);
			if (!target_members || host.unwinding) { return target_members; }
			delegated_to_this = true;
			initializer_depth.reset();
			auto target_body = host.run_constructor_body(target, definition_env, this_value, cls.get(), init_args);
			if (!target_body) { return target_body.error_value(); }
			if (host.unwinding) { return this_value; }
			initializer_depth.emplace(host.call_depth);
		}
	}

	if (!delegated_to_this) {
		if (!all_literal) {
			auto initialized = host.evaluate_field_initializers(instance, cls, init_env, handled_parent_init, nullptr);
			if (!initialized) { return initialized.error_value(); }
		} else if (init_env || !instance->seeded_from_plan() || handled_parent_init) {
			// a literal-only chain's initializers are the defaults create_instance's plan seeded; code that ran ahead of them
			// (an initializer list, a base body) is followed by the stamp again
			stamp_literal_field_initializers(*instance, *cls, handled_parent_init, host.eng);
		}
	}
	return this_value;
}

checked_result<script_value> construct_default_instance(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
                                                        const std::vector<script_value>& args) {
	// arguments to a class without a constructor: a copy, or aggregate initialization
	aggregate_plan aggregate;
	if (!args.empty()) {
		aggregate = plan_aggregate(*cls, args);
		if (!aggregate.fills()) { return aggregate_answer(aggregate, *cls, args, host.eng); }
	}
	if (host.call_depth >= host.depth_gate) {
		if (auto blocked = host.pass_depth_gate()) { return *blocked; }
	}
	if (native_stack_low()) [[unlikely]] {
		return raise_native_stack();
	}
	if (!aggregate.fills() && cls->base_needs_construction()) {
		// C++: the implicit default constructor constructs the base ahead of the fields
		std::optional<call_depth_guard> member_depth;
		member_depth.emplace(host.call_depth);
		auto instance = cls->create_instance();
		instance->construction_pending(true);
		auto this_value = script_value::make_class_object(*cls, instance, host.eng);
		auto members = initialize_members(host, cls, host.global_environment(), instance, this_value, cls->implicit_default_constructor(),
		                                  args, member_depth, 0, nullptr);
		if (!members) { return members; }
		if (!host.unwinding) { instance->construction_pending(false); }
		return this_value;
	}
	call_depth_guard initializer_depth(host.call_depth);
	auto instance = cls->create_instance();
	auto this_value = script_value::make_class_object(*cls, instance, host.eng);
	if (!aggregate.fills() && cls->chain_initializers_all_literal()) {
		if (!instance->seeded_from_plan()) { stamp_literal_field_initializers(*instance, *cls, false, host.eng); }
		return this_value;
	}
	// the chain's root: the global environment, or a worker's root
	auto root = host.current_environment();
	for (int hops = 0; root && root->get_parent() && hops < 65536; ++hops) {
		root = root->get_parent();
	}
	auto init_env = construction_member_environment(host, root, this_value, cls.get());
	auto initialized = host.evaluate_field_initializers(instance, cls, init_env, false, aggregate.fills() ? &aggregate : nullptr);
	if (!initialized) { return initialized.error_value(); }
	return this_value;
}

}
