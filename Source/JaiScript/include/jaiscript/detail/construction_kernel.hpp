#pragma once

// The construction kernel both backends run, a template over the backend's host (its final lender), so every hook call binds
// statically (construction.hpp declares the host and the scope)
#include <jaiscript/detail/construction.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/aggregate_init.hpp>
#include <jaiscript/detail/arg_capture.hpp>

namespace jai::detail {

namespace construction_kernel {
	// a level's scope, opened where its code starts and closed when the construction leaves the level
	template <class Host>
	struct level_scope {
		Host& host;
		construction_scope scope;
		level_scope(Host& h, const std::shared_ptr<environment>& definition_env, const script_value& this_value, script_class_definition* level,
		            const function_decl* ctor = nullptr, const std::vector<script_value>* args = nullptr, bool bind_for_parameters = false) : host(h) {
			host.open_scope(scope, definition_env, this_value, level, ctor, args, bind_for_parameters);
		}
		explicit level_scope(Host& h) : host(h) {}
		level_scope(const level_scope&) = delete;
		level_scope& operator=(const level_scope&) = delete;
		~level_scope() { if (scope) { host.close_scope(scope); } }
	};

	// an initializer list's arguments, each taken as it evaluates (one a T& may bind carries the place it names); a failure
	// propagates as raised
	template <class Host>
	checked_result<void> evaluate_initializer_arguments(Host& host, const constructor_initializer& initializer, construction_scope& scope,
	                                                    bool fields_run_code, const class_definition* binding_class, std::vector<script_value>& result) {
		result.reserve(initializer.arguments.size());
		const bool may_bind_places = constructors_take_references(binding_class);
		for (const auto& arg_expr : initializer.arguments) {
			auto evaluated = host.evaluate_in(scope, arg_expr);
			if (!evaluated) { return evaluated.error_value(); }
			result.push_back(std::move(evaluated.value()));
			capture_initializer_argument(host.eng, initializer, result.size() - 1, fields_run_code, result.back(),
			                             [&] { return constructor_binding(binding_class, result.size() - 1); });
			if (host.unwinding) { return {}; }
			if (may_bind_places && !early_bound_reference(result.back())) {
				const arg_binding binding = constructor_binding(binding_class, result.size() - 1);
				if (!binds_reference(binding)) { continue; }
				script_value minted(std::monostate{}, host.eng);
				auto placed = host.initializer_place(scope, arg_expr.get(), minted);
				if (!placed) { return placed.error_value(); }
				if (placed.value()) { hold_minted_reference(host.eng, result.back(), binding, std::move(minted)); }
			}
		}
		return {};
	}

	// a base constructor through the one script-overload scorer, else the implicit default constructor a base needing construction has
	inline checked_result<std::shared_ptr<function_decl>> select_base_constructor(script_class_definition& base, const std::vector<script_value>& args, engine* eng) {
		const auto& ctors = base.get_constructor_asts();
		auto picked = select_declaration(ctors, args, base.get_type_id(), eng);
		if (!picked) { return picked.error_value(); }
		if (!picked.value() && ctors.empty() && args.empty() && base.base_needs_construction()) {
			return base.implicit_default_constructor();
		}
		return picked;
	}

	inline void adopt_host_base_object(class_instance& instance, const script_value& host_object) {
		if (!host_object.is_object()) { return; }
		auto host_instance = host_object.as<strong_ptr<class_instance>>();
		if (!host_instance) { return; }
		if (const script_value* native = host_instance->native()) {
			instance.set_native(*native);
		}
		instance.region_tag(host_instance->region_tag());
	}

	// A C++ base's constructor makes the object the instance adopts; a_ancestor marks a base above the direct one
	template <class Host>
	checked_result<void> construct_host_base(Host& host, class_instance& instance, const class_definition& base,
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

	// The initializer of `initializers` that constructs base `index` of `level`: super(...) or the base's name for the first, the name
	// for any other; null when none names it
	inline const constructor_initializer* base_initializer(const std::vector<constructor_initializer>& initializers, const class_definition& level, size_t index) {
		const auto& parents = level.get_parent_classes();
		const class_definition* base = index < parents.size() ? parents[index].get() : (index == 0 ? level.parent_ptr() : nullptr);
		for (const auto& init : initializers) {
			if ((index == 0 && init.target == "super") || (base && initializer_names(init.target, base->get_name()))) { return &init; }
		}
		return nullptr;
	}

	// what every level of one construction shares
	struct construction_context {
		const std::shared_ptr<script_class_definition>& cls;
		const std::shared_ptr<environment>& definition_env;
		class_instance& instance;
		const script_value& this_value;
		std::optional<call_depth_guard>& initializer_depth;
		const bool all_literal;
		bool code_ran = false;   // a literal default create_instance seeded may have been overwritten since
	};

	template <class Host>
	checked_result<void> construct_level(Host& host, construction_context& cx, const std::shared_ptr<script_class_definition>& level,
	                                     const std::shared_ptr<function_decl>& ctor, const std::vector<script_value>& args,
	                                     std::vector<script_value>* body_args, bool run_body, size_t delegation_depth);

	// A base its derived constructor leaves to its default and that has no constructor whose own bases run none: its fields and its
	// bases' are all it initializes, bases first
	template <class Host>
	checked_result<void> initialize_plain_base(Host& host, construction_context& cx, const std::shared_ptr<script_class_definition>& base) {
		if (base->chain_initializers_all_literal()) {
			if (cx.code_ran || !cx.instance.seeded_from_plan()) { stamp_literal_field_initializers(cx.instance, *base, false, host.eng); }
			return {};
		}
		cx.code_ran = true;
		level_scope<Host> env(host, cx.definition_env, cx.this_value, base.get());
		return host.evaluate_field_initializers(cx.instance.strong_from_this(), base, env.scope, false, nullptr);
	}

	// Base `index` of `level` (C++ [class.base.init]): with the arguments the level's initializer list names it with, else
	// default-constructed
	template <class Host>
	checked_result<void> construct_base(Host& host, construction_context& cx, const std::shared_ptr<script_class_definition>& level,
	                                    const std::shared_ptr<class_definition>& base, size_t index, const constructor_initializer* init,
	                                    construction_scope& scope) {
		std::vector<script_value> base_args;
		if (init) {
			auto evaluated = evaluate_initializer_arguments(host, *init, scope, !cx.all_literal, base.get(), base_args);
			if (!evaluated || host.unwinding) { return evaluated; }
		}
		if (!base->is_script_class()) {
			if (index > 0) { return {}; }
			cx.code_ran = true;
			if (level.get() != cx.cls.get()) { return construct_host_base(host, cx.instance, *base, base_args, true); }
			try {
				return construct_host_base(host, cx.instance, *base, base_args, false);
			} catch (const runtime_error&) {
				return checked_result<void>(make_error_code(runtime_error_code::constructor_failed), "Failed to call C++ parent constructor");
			}
		}
		auto script_base = std::static_pointer_cast<script_class_definition>(base);
		auto base_ctor = select_base_constructor(*script_base, base_args, host.eng);
		if (!base_ctor) { return base_ctor.error_value(); }
		if (base_ctor.value()) {
			return construct_level(host, cx, script_base, base_ctor.value(), base_args, &base_args, true, 0);
		}
		const bool declares_constructors = !script_base->get_constructor_asts().empty();
		if (index == 0) {
			if (declares_constructors) {
				if (base_args.empty()) { return no_default_constructor(script_base->get_type_id()); }
				return overload_miss(make_error_code(runtime_error_code::no_constructor_found), script_base->get_type_id(), base_args.data(), base_args.size(), host.eng, true);
			}
			if (!base_args.empty()) {
				return checked_result<void>(make_error_code(runtime_error_code::no_constructor_found), "No matching parent constructor found for super()");
			}
		} else if (init && (declares_constructors || !base_args.empty())) {
			return overload_miss(make_error_code(runtime_error_code::no_constructor_found), script_base->get_type_id(), base_args.data(), base_args.size(), host.eng, true);
		} else if (declares_constructors) {
			return checked_result<void>(make_error_code(runtime_error_code::no_constructor_found), k_base_needs_naming, level->get_type_id(), script_base->get_type_id());
		}
		return initialize_plain_base(host, cx, script_base);
	}

	// One constructor's activation on its level (C++: one per constructor): its parameters bound, then a delegation's whole target,
	// else each base in declaration order and the level's own fields; a base level's body runs here, the class's is the caller's.
	// body_args receives the arguments the body binds, as the level's code left its parameters (at the class: when that code ran)
	template <class Host>
	checked_result<void> construct_level(Host& host, construction_context& cx, const std::shared_ptr<script_class_definition>& level,
	                                     const std::shared_ptr<function_decl>& ctor, const std::vector<script_value>& args,
	                                     std::vector<script_value>* body_args, bool run_body, size_t delegation_depth) {
		if (delegation_depth > k_max_constructor_delegation_depth) {
			return raise_call_depth();   // a delegation cycle (ill-formed in C++) raises instead of recursing without end
		}
		if (native_stack_low()) [[unlikely]] {
			return raise_native_stack();
		}
		if (!arity_accepts(ctor->parameters, args.size())) {
			return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch), "Constructor parameter count mismatch");
		}
		for (const auto& param : ctor->parameters) {
			if (param.symbol_id == UINT64_MAX) { param.symbol_id = host.symbols->intern(param.name); }
		}
		level_scope<Host> scope(host);
		if (run_body) {
			host.open_scope(scope.scope, cx.definition_env, cx.this_value, level.get(), ctor.get(), &args, true);
			cx.code_ran = true;
		} else if (construction_runs_code(*level, *ctor)) {
			// the call's arguments are taken ahead of that code, as the body's parameters
			const std::vector<script_value>* argv = &args;
			if (body_args && later_capture_candidate(0, args.size(), [&](size_t j) -> const script_value& { return args[j]; })) {
				*body_args = args;
				for (size_t j = 0; j < body_args->size(); ++j) {
					capture_argument(host.eng, (*body_args)[j], transient_read_marker::arg_capture_full, parameters_binding(ctor->parameters, j));
				}
				argv = body_args;
			}
			host.open_scope(scope.scope, cx.definition_env, cx.this_value, level.get(), ctor.get(), argv, true);
			cx.code_ran = true;
		}
		const auto& initializers = ctor->initializers;
		const auto delegation = std::find_if(initializers.begin(), initializers.end(), [](const constructor_initializer& init) { return init.target == "this"; });
		if (delegation != initializers.end()) {
			// C++ delegation: the target constructor constructs the level (its bases, its members and its body) ahead of this body
			std::vector<script_value> target_args;
			auto evaluated = evaluate_initializer_arguments(host, *delegation, scope.scope, !cx.all_literal, level.get(), target_args);
			if (!evaluated || host.unwinding) { return evaluated; }
			std::vector<std::shared_ptr<function_decl>> other_ctors;
			for (const auto& candidate : level->get_constructor_asts()) { if (candidate != ctor) { other_ctors.push_back(candidate); } }
			auto target_pick = select_declaration(other_ctors, target_args, level->get_type_id(), host.eng);
			if (!target_pick) { return target_pick.error_value(); }
			if (!target_pick.value()) {
				return overload_miss(make_error_code(runtime_error_code::no_constructor_found), level->get_type_id(), target_args.data(), target_args.size(), host.eng, true);
			}
			auto constructed = construct_level(host, cx, level, target_pick.value(), target_args, &target_args, true, delegation_depth + 1);
			if (!constructed || host.unwinding) { return constructed; }
			cx.instance.constructed_levels(cx.cls->constructed_through(level.get()));   // C++: the level is constructed once its target finishes
		} else {
			const auto& parents = level->get_parent_classes();
			if (parents.empty() && std::any_of(initializers.begin(), initializers.end(), [](const constructor_initializer& init) { return init.target == "super"; })) {
				return checked_result<void>(make_error_code(runtime_error_code::no_constructor_found), "Cannot call super() - class has no base class");
			}
			for (size_t i = 0; i < parents.size(); ++i) {
				if (!parents[i]) { continue; }
				auto constructed = construct_base(host, cx, level, parents[i], i, base_initializer(initializers, *level, i), scope.scope);
				if (!constructed || host.unwinding) { return constructed; }
				cx.instance.constructed_levels(cx.cls->constructed_through(parents[i].get()));
			}
		}
		std::optional<dispatch_level_scope> level_dispatch;
		if (level.get() != cx.cls.get()) { level_dispatch.emplace(cx.instance, level.get(), class_instance::cdtor_phase::construction); }
		if (delegation == initializers.end()) {
			if (cx.all_literal) {
				if (cx.code_ran || !cx.instance.seeded_from_plan()) { stamp_literal_field_initializers(cx.instance, *level, true, host.eng); }
			} else if (!level->get_field_initializer_asts().empty()) {
				if (!scope.scope) {
					host.open_scope(scope.scope, cx.definition_env, cx.this_value, level.get(), ctor.get(), &args, true);
					cx.code_ran = true;
				}
				auto initialized = host.evaluate_field_initializers(cx.instance.strong_from_this(), level, scope.scope, true, nullptr);
				if (host.unwinding) { return {}; }
				if (!initialized) { return initialized; }
			}
		}
		if (scope.scope && body_args) {
			if (body_args->empty()) { *body_args = args; }
			host.read_back_parameters(scope.scope, ctor.get(), *body_args);
		}
		if (!run_body) { return {}; }
		cx.initializer_depth.reset();
		auto body = host.run_constructor_body(ctor, cx.definition_env, cx.this_value, level.get(), *body_args);
		if (!body) { return body.error_value(); }
		if (host.unwinding) { return {}; }
		cx.initializer_depth.emplace(host.call_depth);
		return {};
	}
}


template <class Host>
checked_result<script_value> initialize_members(Host& host, const std::shared_ptr<script_class_definition>& cls,
                                                const std::shared_ptr<environment>& definition_env, const strong_ptr<class_instance>& instance,
                                                const script_value& this_value, const std::shared_ptr<function_decl>& ctor,
                                                const std::vector<script_value>& args, std::optional<call_depth_guard>& initializer_depth,
                                                std::vector<script_value>* captured_out) {
	construction_kernel::construction_context cx{cls, definition_env, *instance, this_value, initializer_depth, cls->chain_initializers_all_literal()};
	auto constructed = construction_kernel::construct_level(host, cx, cls, ctor, args, captured_out, false, 0);
	if (!constructed) { return constructed.error_value(); }
	return this_value;
}

template <class Host>
checked_result<script_value> construct_default_instance(Host& host, const std::shared_ptr<script_class_definition>& cls,
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
		                                  args, member_depth, nullptr);
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
	const auto scope_parent = sandbox ? sandbox : environment_chain_root(host.current_environment());
	construction_kernel::level_scope<Host> init_env(host);
	if (aggregate.fills()) {
		host.construction_host::open_scope(init_env.scope, scope_parent, this_value, cls.get(), nullptr, nullptr, false);   // aggregate filling reads the environment
	} else {
		host.open_scope(init_env.scope, scope_parent, this_value, cls.get(), nullptr, nullptr, false);
	}
	auto initialized = host.evaluate_field_initializers(instance, cls, init_env.scope, false, aggregate.fills() ? &aggregate : nullptr);
	if (!initialized) { return initialized.error_value(); }
	return this_value;
}


}
