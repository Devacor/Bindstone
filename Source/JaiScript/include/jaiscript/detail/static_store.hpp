#pragma once

#ifndef __JAISCRIPT_DETAIL_STATIC_STORE_HPP__
#define __JAISCRIPT_DETAIL_STATIC_STORE_HPP__

// Class-qualified static stores (C::s = v, C::s op= v, ++C::s) shared VERBATIM by the vm
// and the interpreter (parity by construction): the class-resolution ladder with its
// texts, then the instance-field store table over the static's declared type
// (class_definition::enforce_static_write). Bare static stores inside methods reach
// store_static_member through their own scope resolution.

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/detail/namespace_scope.hpp>
#include <memory>
#include <optional>
#include <utility>

namespace jai::detail {

	inline checked_result<std::shared_ptr<class_definition>> resolve_static_store_class(const member_expr* member, const environment& env,
	                                                                                    const string_symbolizer& symbols,
	                                                                                    const class_definition* access_context) {
		using result = checked_result<std::shared_ptr<class_definition>>;
		if (member->object->get_type() != node_type::identifier_expr) {
			return result(make_error_code(runtime_error_code::type_mismatch),
				"Static member assignment requires a class name");
		}
		const uint64_t class_name_id = static_cast<const identifier_expr*>(member->object.get())->symbol_id;
		auto [class_var_id, class_var_name] = symbols.get_class_var_id_with_view(class_name_id);
		auto class_result = env.get(class_var_id);
		if (!class_result) {
			return result(make_error_code(runtime_error_code::undefined_variable),
				"Class '{0}' not found", class_name_id);
		}
		const script_value& class_var = class_result.value();
		if (!class_var.is_object()) {
			return result(make_error_code(runtime_error_code::type_mismatch),
				"'{0}' is not a class", class_name_id);
		}
		auto holder = class_var.get_object_holder();
		if (!holder || holder->type_name != "class_definition") {
			return result(make_error_code(runtime_error_code::type_mismatch),
				"'{0}' is not a valid class", class_name_id);
		}
		auto class_def = std::static_pointer_cast<class_definition>(holder->data);
		if (class_def->chain_has_nonpublic()) [[unlikely]] {
			auto access = enforce_member_access(class_def.get(), member->member_id, access_context);
			if (!access) {
				return access.error_value();
			}
		}
		return class_def;
	}

	// A ::-qualified static (C::s) or namespace variable (ns::x) as a reference argument: its storage, a C++ lvalue. nullopt
	// when the spelling names neither (the caller's other ladders decide); an error for a host entry (read-only) or a denied
	// private static
	inline std::optional<checked_result<script_value*>> static_member_lvalue(engine& eng, const member_expr* member, const environment& env,
	                                                                         const string_symbolizer& symbols,
	                                                                         const class_definition* access_context) {
		using result = checked_result<script_value*>;
		if (!member || !member->is_static) { return std::nullopt; }
		if (auto ns = resolve_namespace_store(eng, member, symbols)) {
			if (!ns->cell) {
				return result(make_error_code(runtime_error_code::invalid_reference), "Cannot pass non-lvalue to reference parameter");
			}
			return result(ns->cell);
		}
		if (member->object->get_type() != node_type::identifier_expr) { return std::nullopt; }
		auto cls = resolve_static_store_class(member, env, symbols, access_context);
		if (!cls) {
			if (cls.error() == make_error_code(runtime_error_code::access_violation)) { return result(cls.error_value()); }
			return std::nullopt;
		}
		if (script_value* cell = cls.value()->get_static_field_ptr(member->member_id)) { return result(cell); }
		return std::nullopt;
	}

	// Presence, the field ladder, the write; the stored value is the expression result.
	inline checked_result<script_value> store_static_member(class_definition& cls, uint64_t member_id, script_value incoming) {
		if (!cls.has_static_field(member_id)) {
			return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable),
				"Cannot assign to static member: field '{0}' not found", member_id);
		}
		auto enforced = cls.enforce_static_write(member_id, std::move(incoming));
		if (!enforced) {
			return enforced.error_value();
		}
		script_value stored = enforced.value();
		(void)cls.set_static_field(member_id, std::move(enforced.value()));
		return stored;
	}

} // namespace jai::detail

#endif
