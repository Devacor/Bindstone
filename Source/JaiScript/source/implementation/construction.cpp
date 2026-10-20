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

namespace {
	std::shared_ptr<environment> construction_member_environment(construction_host& host, const std::shared_ptr<environment>& parent,
	                                                             const script_value& this_value, class_definition* access) {
		auto result = std::make_shared<environment>(parent, host.env_symbols);
		result->define(host.symbols->get_this_id(), this_value);
		result->set_access_context(access);
		return result;
	}

	std::shared_ptr<environment> construction_level_environment(construction_host& host, const std::shared_ptr<environment>& definition_env,
	                                                            const script_value& this_value, script_class_definition* level,
	                                                            const function_decl& ctor, const std::vector<script_value>& args) {
		auto result = construction_member_environment(host, definition_env, this_value, level);
		for (size_t i = 0; i < ctor.parameters.size() && i < args.size(); ++i) {
			const parameter& param = ctor.parameters[i];
			if (param.symbol_id == UINT64_MAX) { param.symbol_id = host.symbols->intern(param.name); }
			result->define(param.symbol_id, args[i]);
		}
		return result;
	}

	// an initializer list's arguments, each taken as it evaluates; a failure propagates as raised
	checked_result<void> evaluate_initializer_arguments(construction_host& host, const constructor_initializer& initializer, const std::shared_ptr<environment>& env,
	                                                    bool fields_run_code, const class_definition* binding_class, std::vector<script_value>& result) {
		result.reserve(initializer.arguments.size());
		for (const auto& arg_expr : initializer.arguments) {
			auto evaluated = host.evaluate(arg_expr, env);
			if (!evaluated) { return evaluated.error_value(); }
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
		instance.region_tag(host_instance->region_tag());
	}

	// A C++ base's constructor makes the object the instance adopts; a_ancestor marks a base above the direct one
	checked_result<void> construct_host_base(construction_host& host, class_instance& instance, const class_definition& base,
	                                         const std::vector<script_value>& args, bool a_ancestor) {
		const std::string& name = base.get_name();
		auto ctor = host.current_environment()->get(name, *host.symbols);
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

	// The initializer of `initializers` that constructs base `index` of `level`: super(...) or the base's name for the first, the name
	// for any other; null when none names it
	const constructor_initializer* base_initializer(const std::vector<constructor_initializer>& initializers, const class_definition& level, size_t index) {
		const auto& parents = level.get_parent_classes();
		const class_definition* base = index < parents.size() ? parents[index].get() : (index == 0 ? level.parent_ptr() : nullptr);
		for (const auto& init : initializers) {
			if ((index == 0 && init.target == "super") || (base && initializer_names(init.target, base->get_name()))) { return &init; }   // a base-less super() raises where it runs
		}
		return nullptr;
	}

	// The first base runs code when it is default-constructed: a C++ base, or a script base with a constructor or such a base of its own
	bool first_base_needs_construction(const script_class_definition& level) {
		const class_definition* parent = level.parent_ptr();
		if (!parent) { return false; }
		if (!parent->is_script_class()) { return true; }
		const auto& script_parent = static_cast<const script_class_definition&>(*parent);
		return !script_parent.get_constructor_asts().empty() || script_parent.base_needs_construction();
	}

	checked_result<void> construct_further_bases(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                             const script_class_definition& level, const function_decl* ctor, const std::shared_ptr<environment>& level_env,
	                                             const std::shared_ptr<environment>& definition_env, class_instance& instance, const script_value& this_value,
	                                             std::optional<call_depth_guard>& initializer_depth);

	// A base without a constructor of its own whose bases run none: its field initializers, its bases' first (each in declaration order)
	checked_result<void> initialize_plain_base(construction_host& host, const std::shared_ptr<script_class_definition>& base,
	                                           const std::shared_ptr<environment>& definition_env, class_instance& instance, const script_value& this_value) {
		if (base->chain_initializers_all_literal()) {
			stamp_literal_field_initializers(instance, *base, false, host.eng);
			return {};
		}
		auto env = construction_member_environment(host, definition_env, this_value, base.get());
		return host.evaluate_field_initializers(instance.shared_from_this(), base, env, false, nullptr);
	}

	// A script base with a constructor: walk the super() chain up to a C++ base (constructed on the way), then run each level's
	// further bases, field initializers and constructor body root to leaf
	checked_result<void> construct_script_bases(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                            const std::shared_ptr<environment>& definition_env, class_instance& instance,
	                                            const script_value& this_value, std::vector<constructor_chain_entry>& chain,
	                                            std::optional<call_depth_guard>& initializer_depth, size_t delegation_depth = 0) {
		if (delegation_depth > k_max_constructor_delegation_depth) {
			return raise_call_depth();   // a delegation cycle (ill-formed in C++) raises instead of recursing without end
		}
		const bool fields_run_code = !cls->chain_initializers_all_literal();
		for (size_t level = 0; level < chain.size(); ++level) {
			auto& entry = chain[level];
			const auto& initializers = entry.script_class->effective_initializers(*entry.ctor);
			if (std::any_of(initializers.begin(), initializers.end(), [](const constructor_initializer& init) { return init.target == "this"; })) { continue; }
			const constructor_initializer* init = base_initializer(initializers, *entry.script_class, 0);
			if (!init && !first_base_needs_construction(*entry.script_class)) { continue; }
			{
				auto ancestor = entry.script_class->get_parent();
				if (ancestor) {
					std::vector<script_value> ancestor_args;
					if (init) {
						auto level_env = construction_level_environment(host, definition_env, this_value, entry.script_class.get(), *entry.ctor, entry.args);
						auto evaluated = evaluate_initializer_arguments(host, *init, level_env, fields_run_code, ancestor.get(), ancestor_args);
						if (!evaluated || host.unwinding) { return evaluated; }
					}
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
			}
		}
		for (size_t below = chain.size(); below > 0; --below) {
			auto* it = &chain[below - 1];
			auto level_env = construction_level_environment(host, definition_env, this_value, it->script_class.get(), *it->ctor, it->args);
			const auto& level_initializers = it->script_class->effective_initializers(*it->ctor);
			const auto delegation = std::find_if(level_initializers.begin(), level_initializers.end(), [](const constructor_initializer& init) { return init.target == "this"; });
			if (delegation != level_initializers.end()) {
				// C++ delegation: the target constructor constructs the level (its bases, its members and its body) ahead of this body
				std::vector<script_value> target_args;
				auto evaluated = evaluate_initializer_arguments(host, *delegation, level_env, fields_run_code, it->script_class.get(), target_args);
				if (!evaluated || host.unwinding) { return evaluated; }
				std::vector<std::shared_ptr<function_decl>> other_ctors;
				for (const auto& candidate : it->script_class->get_constructor_asts()) { if (candidate != it->ctor) { other_ctors.push_back(candidate); } }
				auto target_pick = select_declaration(other_ctors, target_args, it->script_class->get_type_id(), host.eng);
				if (!target_pick) { return target_pick.error_value(); }
				if (!target_pick.value()) {
					return overload_miss(make_error_code(runtime_error_code::no_constructor_found), it->script_class->get_type_id(), target_args.data(),
					                     target_args.size(), host.eng, true);
				}
				std::vector<constructor_chain_entry> target_chain;
				target_chain.push_back({it->script_class, std::move(target_pick.value()), std::move(target_args)});
				auto constructed = construct_script_bases(host, cls, definition_env, instance, this_value, target_chain, initializer_depth, delegation_depth + 1);
				if (!constructed || host.unwinding) { return constructed; }
				it = &chain[below - 1];
			}
			if (delegation == level_initializers.end()) {
				auto further = construct_further_bases(host, cls, *it->script_class, it->ctor.get(), level_env, definition_env, instance, this_value, initializer_depth);
				if (!further || host.unwinding) { return further; }
				it = &chain[below - 1];
			}
			dispatch_level_scope level_dispatch(instance, it->script_class.get(), class_instance::cdtor_phase::construction);
			if (delegation == level_initializers.end()) {
				for (const auto& [field_id, initializer_ast] : it->script_class->get_field_initializer_asts()) {
					if (!initializer_ast) { continue; }
					auto evaluated = host.evaluate(initializer_ast, level_env);
					if (host.unwinding) { return {}; }
					if (!evaluated) { return evaluated.error_value(); }
					instance.set_field(field_id, initializer_store_value(std::move(evaluated.value()), initializer_ast.get()));
				}
			}
			initializer_depth.reset();
			auto body = host.run_constructor_body(it->ctor, definition_env, this_value, it->script_class.get(), it->args);
			if (!body) { return body.error_value(); }
			if (host.unwinding) { return {}; }
			instance.constructed_levels(cls->constructed_through(it->script_class.get()));
			initializer_depth.emplace(host.call_depth);
		}
		return {};
	}

	// The bases after the first, in declaration order, once the first is constructed and before the level's own members (C++): each
	// with the arguments the level's initializer list names it with, else default-constructed
	checked_result<void> construct_further_bases(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                             const script_class_definition& level, const function_decl* ctor, const std::shared_ptr<environment>& level_env,
	                                             const std::shared_ptr<environment>& definition_env, class_instance& instance, const script_value& this_value,
	                                             std::optional<call_depth_guard>& initializer_depth) {
		const auto& parents = level.get_parent_classes();
		for (size_t i = 1; i < parents.size(); ++i) {
			auto base = std::dynamic_pointer_cast<script_class_definition>(parents[i]);
			if (!base) { continue; }
			const constructor_initializer* init = ctor ? base_initializer(ctor->initializers, level, i) : nullptr;
			std::vector<script_value> base_args;
			if (init) {
				auto evaluated = evaluate_initializer_arguments(host, *init, level_env, !cls->chain_initializers_all_literal(), base.get(), base_args);
				if (!evaluated || host.unwinding) { return evaluated; }
			}
			auto base_ctor = select_base_constructor(*base, base_args, host.eng);
			if (!base_ctor) { return base_ctor.error_value(); }
			if (base_ctor.value()) {
				std::vector<constructor_chain_entry> chain;
				chain.push_back({base, std::move(base_ctor.value()), std::move(base_args)});
				auto constructed = construct_script_bases(host, cls, definition_env, instance, this_value, chain, initializer_depth);
				if (!constructed || host.unwinding) { return constructed; }
			} else if (init && (!base->get_constructor_asts().empty() || !base_args.empty())) {
				return overload_miss(make_error_code(runtime_error_code::no_constructor_found), base->get_type_id(), base_args.data(), base_args.size(), host.eng, true);
			} else if (!base->get_constructor_asts().empty()) {
				return checked_result<void>(make_error_code(runtime_error_code::no_constructor_found), k_base_needs_naming, level.get_type_id(), base->get_type_id());
			} else {
				auto initialized = initialize_plain_base(host, base, definition_env, instance, this_value);
				if (!initialized || host.unwinding) { return initialized; }
				instance.constructed_levels(cls->constructed_through(base.get()));
			}
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
		auto evaluated = evaluate_initializer_arguments(host, initializer, init_env, !cls->chain_initializers_all_literal(), parent.get(), init_args);
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
	const constructor_initializer* first_base = base_initializer(initializers, *cls, 0);
	const bool delegates = std::any_of(initializers.begin(), initializers.end(), [](const constructor_initializer& init) { return init.target == "this"; });
	if (!first_base && !delegates && !initializers.empty() && first_base_needs_construction(*cls)) {
		const constructor_initializer implicit_super{"super", {}};
		auto constructed = construct_base(host, cls, implicit_super, definition_env, init_env, *instance, this_value, initializer_depth, handled_parent_init);
		if (!constructed) { return constructed.error_value(); }
		if (host.unwinding) { return this_value; }
	}
	for (const auto& initializer : initializers) {
		if (&initializer == first_base) {
			auto constructed = construct_base(host, cls, initializer, definition_env, init_env, *instance, this_value, initializer_depth, handled_parent_init);
			if (!constructed) { return constructed.error_value(); }
			if (host.unwinding) { return this_value; }
		} else if (initializer.target == "this") {
			std::vector<script_value> init_args;
			auto evaluated = evaluate_initializer_arguments(host, initializer, init_env, !all_literal, cls.get(), init_args);
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
			instance->constructed_levels(static_cast<uint16_t>(cls->construction_order().size()));   // C++: the object is constructed once its target constructor finishes
			initializer_depth.emplace(host.call_depth);
		}
	}

	if (!delegated_to_this) {
		if (cls->get_parent_classes().size() > 1) {
			if (!handled_parent_init) {
				if (auto first = std::dynamic_pointer_cast<script_class_definition>(cls->get_parent_classes()[0])) {
					auto initialized = initialize_plain_base(host, first, definition_env, *instance, this_value);
					if (!initialized || host.unwinding) { return initialized.error_value(); }
				}
				handled_parent_init = true;
			}
			auto further = construct_further_bases(host, cls, *cls, ctor.get(), init_env, definition_env, *instance, this_value, initializer_depth);
			if (!further) { return further.error_value(); }
			if (host.unwinding) { return this_value; }
		}
		if (cls->parent_ptr()) { instance->constructed_levels(static_cast<uint16_t>(cls->construction_order().size() - 1)); }
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
		auto sandbox = host.parallel_sandbox();
		auto members = initialize_members(host, cls, sandbox ? sandbox : host.global_environment(), instance, this_value, cls->implicit_default_constructor(),
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
	auto sandbox = host.parallel_sandbox();
	auto init_env = construction_member_environment(host, sandbox ? sandbox : environment_chain_root(host.current_environment()), this_value, cls.get());
	auto initialized = host.evaluate_field_initializers(instance, cls, init_env, false, aggregate.fills() ? &aggregate : nullptr);
	if (!initialized) { return initialized.error_value(); }
	return this_value;
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
