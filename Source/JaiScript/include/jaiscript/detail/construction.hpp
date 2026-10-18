#pragma once

#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace jai::detail {
	struct aggregate_plan;

	// What a backend lends the one construction kernel both backends run: its evaluator, its constructor-body call, its field
	// initializers and its depth and unwinding state. The kernel's common paths call none of the hooks.
	class construction_host {
	public:
		construction_host(engine* a_eng, string_symbolizer* a_symbols, int& a_callDepth, int a_depthGate, const bool& a_unwinding) :
			eng(a_eng),
			symbols(a_symbols),
			call_depth(a_callDepth),
			depth_gate(a_depthGate),
			unwinding(a_unwinding) {
		}

		engine* const eng;
		string_symbolizer* const symbols;
		int& call_depth;
		const int depth_gate;   // a call depth at or past it asks pass_depth_gate
		const bool& unwinding;

		virtual checked_result<script_value> evaluate(const expression_ptr& expr, const std::shared_ptr<environment>& env) = 0;
		virtual checked_result<script_value> run_constructor_body(const std::shared_ptr<function_decl>& ctor, const std::shared_ptr<environment>& definition_env,
		                                                          const script_value& this_value, class_definition* access, const std::vector<script_value>& args) = 0;
		virtual checked_result<void> evaluate_field_initializers(const std::shared_ptr<class_instance>& instance, const std::shared_ptr<script_class_definition>& cls,
		                                                         const std::shared_ptr<environment>& init_env, bool skip_parent_recursion, const aggregate_plan* aggregate) = 0;
		virtual std::optional<error_propagator> pass_depth_gate() = 0;
		virtual std::shared_ptr<environment> current_environment() = 0;
		virtual std::shared_ptr<environment> global_environment() = 0;
		virtual const class_definition* caller_access_context() = 0;   // asked only for a private constructor

	protected:
		~construction_host() = default;
	};

	// A constructor's member initialization on an instance under construction: its initializer list (base constructors and their
	// bodies, or the whole delegated-to constructor), then the field initializers; the body is the caller's. captured_out (the
	// outermost constructor only) receives the arguments taken ahead of that code, when it runs any.
	checked_result<script_value> initialize_members(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                                const std::shared_ptr<environment>& definition_env, const std::shared_ptr<class_instance>& instance,
	                                                const script_value& this_value, const std::shared_ptr<function_decl>& ctor,
	                                                const std::vector<script_value>& args, std::optional<call_depth_guard>& initializer_depth,
	                                                size_t delegation_depth, std::vector<script_value>* captured_out);

	// Everything ahead of a constructor's body: the overload, the instance and its member initialization. Success yields the
	// instance; the picked constructor and the arguments the body binds (when they were taken ahead of it) ride the
	// out-parameters. Inline, so the hot pick and allocation compile in each backend's own translation unit.
	inline checked_result<script_value> begin_construction(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                                       const std::shared_ptr<environment>& definition_env, const std::vector<script_value>& args,
	                                                       std::optional<call_depth_guard>& initializer_depth, std::shared_ptr<function_decl>& ctor_out,
	                                                       std::vector<script_value>& captured_out, bool check_access) {
		if (host.call_depth >= host.depth_gate) {
			if (auto blocked = host.pass_depth_gate()) { return *blocked; }
		}
		if (native_stack_low()) [[unlikely]] {
			return raise_native_stack();
		}
		initializer_depth.emplace(host.call_depth);
		// the constructor overloads through the one script-overload scorer (typed exact > conversion > var); with several and none
		// viable the sole arity match binds (its parameter raises its own text), else the overload miss; a tie raises
		const auto& ctors = cls->get_constructor_asts();
		std::shared_ptr<function_decl> ctor;
		if (ctors.size() == 1) {
			if (arity_accepts(ctors[0]->parameters, args.size())) { ctor = ctors[0]; }
		} else {
			auto picked = select_declaration(ctors, args, cls->get_type_id(), host.eng);
			if (!picked) { return picked.error_value(); }
			ctor = std::move(picked.value());
		}
		if (!ctor) {
			return overload_miss(make_error_code(runtime_error_code::no_constructor_found), cls->get_type_id(), args.data(), args.size(), host.eng, true);
		}
		if (check_access) {
			if (auto access = enforce_constructor_access(*cls, ctor.get(), [&]() { return host.caller_access_context(); }); !access) {
				return access.error_value();
			}
		}
		auto instance = cls->create_instance();
		instance->construction_pending(true);
		auto this_value = script_value::make_class_object(*cls, instance, host.eng);
		auto members = initialize_members(host, cls, definition_env, instance, this_value, ctor, args, initializer_depth, 0, &captured_out);
		if (!members) { return members; }
		ctor_out = std::move(ctor);
		return this_value;
	}

	// A class without a constructor: a copy, aggregate initialization, or the implicit default constructor
	checked_result<script_value> construct_default_instance(construction_host& host, const std::shared_ptr<script_class_definition>& cls,
	                                                        const std::vector<script_value>& args);

	// A literal-only chain's field initializers stamped without an environment: parents first, then the class's own
	void stamp_literal_field_initializers(class_instance& instance, const script_class_definition& cls, bool skip_parent_recursion, engine* eng);

	// The common default construction (no arguments, no base to construct, a literal-only chain) inline at the call; nullopt
	// leaves the call to construct_default_instance
	inline std::optional<script_value> construct_literal_default(const std::shared_ptr<script_class_definition>& cls, const std::vector<script_value>& args,
	                                                             engine* eng, int& call_depth, int depth_gate) {
		if (!args.empty() || call_depth >= depth_gate || cls->base_needs_construction() || !cls->chain_initializers_all_literal() || native_stack_low()) {
			return std::nullopt;
		}
		call_depth_guard depth(call_depth);
		auto instance = cls->create_instance();
		auto result = script_value::make_class_object(*cls, instance, eng);
		if (!instance->seeded_from_plan()) { stamp_literal_field_initializers(*instance, *cls, false, eng); }
		return result;
	}

	// A call that resolved nowhere on an object under construction or destruction, to a method only a class derived from the level
	// answering for it declares (C++ [class.cdtor]): the refusal both backends raise in place of the miss they would raise, and the
	// strict checker's text for the same call when it sees one
	inline constexpr std::string_view k_call_under_construction = "Cannot call '{0}' while '{1}' is under construction: only a class derived from it declares it";
	inline constexpr std::string_view k_call_under_destruction = "Cannot call '{0}' while '{1}' is under destruction: only a class derived from it declares it";
	std::optional<error_propagator> refuse_cdtor_call(class_instance* instance, uint64_t name_id);

	// A constructor that completed arms its instance's destructor
	inline void construction_completed(const script_value& instance_value) {
		const auto holder = instance_value.get_object_holder();
		if (holder && holder->is_class_instance_wrapper && holder->data) { static_cast<class_instance*>(holder->data.get())->construction_pending(false); }
	}
}
