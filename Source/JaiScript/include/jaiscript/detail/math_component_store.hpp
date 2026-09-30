#pragma once

#ifndef __JAISCRIPT_DETAIL_MATH_COMPONENT_STORE_HPP__
#define __JAISCRIPT_DETAIL_MATH_COMPONENT_STORE_HPP__

// `base.x = v` on a math value (docs/math_types_design.md 5.5), shared VERBATIM by both backends: a math value is
// a value, so the store lands in the storage the base NAMES, never in the evaluated copy. The base resolves by
// shape: an evaluated reference (a subscript read, a ref alias) writes through; an identifier is its variable's
// cell (the backend's own ladder: slot, member scope, environment); a pure lvalue path (members and literal or
// identifier subscripts over an identifier or this) binds through the ref-lvalue kernel exactly like `f(obj.pos)`
// binds a reference parameter - pure resolution, so nothing the base names runs twice; anything else is a
// temporary, which C++ rejects ([expr.ass]: assignment to a member of a class prvalue).

#include <jaiscript/detail/math_values.hpp>
#include <jaiscript/detail/namespace_scope.hpp>
#include <jaiscript/detail/ref_lvalue.hpp>
#include <jaiscript/detail/static_store.hpp>

namespace jai::detail::math {

	// An arithmetic or compound assignment token as its operator row (only + - * / % and their compounds reach here)
	inline bop bop_of(token_type t) noexcept {
		switch (t) {
			case token_type::minus: case token_type::minus_equal: return bop::sub;
			case token_type::star: case token_type::star_equal: return bop::mul;
			case token_type::slash: case token_type::slash_equal: return bop::div;
			case token_type::percent: case token_type::percent_equal: return bop::mod;
			default: return bop::add;
		}
	}

	struct component_target {
		script_value pin;                 // the reference pinning the storage (null for an identifier's cell)
		script_value* storage = nullptr;  // the math value the store writes (null = a temporary)

		explicit component_target(engine* eng) : pin(std::monostate{}, eng) {}
		// Scratch-backed pins (a typed element, a PROPERTY reference) write the updated scratch back
		void commit() {
			if (auto* sr = pin.scratch_ref_holder()) { sr->commit_scratch(); }
		}
	};

	inline script_value* final_referent(script_value& v) {
		script_value* p = &v.deref();
		while (p->is_reference()) { p = &p->deref(); }
		return p;
	}

	// base: the evaluated base as the store site holds it (a reference for subscript reads and aliases);
	// ident_cell: the backend's identifier ladder (returns the variable's storage or null)
	template <typename IdentCell>
	inline component_target resolve_component_target(script_value& base, const expression* base_expr, IdentCell&& ident_cell,
	                                                 const caller_frame_view& caller, environment* caller_env,
	                                                 engine* eng, string_symbolizer* symbolizer) {
		component_target t(eng);
		if (base.is_reference()) {
			t.pin = base;
			t.storage = final_referent(t.pin);
		} else if (base_expr && base_expr->get_type() == node_type::identifier_expr) {
			if (script_value* cell = ident_cell(static_cast<const identifier_expr*>(base_expr))) {
				if (cell->scratch_ref_holder()) {   // an alias of a typed element or a property: the store lands in its scratch, then commits
					t.pin = *cell;
					t.storage = final_referent(t.pin);
				} else {
					t.storage = &cell->deref();
				}
			}
		} else if (base_expr && is_ref_bindable_lvalue(base_expr)) {
			auto resolved = resolve_ref_lvalue(base_expr, caller, caller_env, eng, symbolizer);
			if (resolved && resolved.value().is_reference()) {
				t.pin = std::move(resolved.value());
				t.storage = final_referent(t.pin);
			}
		}
		if (t.storage && !is_kind(t.storage->raw_storage_index())) { t.storage = nullptr; }
		return t;
	}

	// A static base (`C::s.x = 1`, `ns::v.x = 1`): the namespace scope's cell or the class's static field, by the ladder
	// and texts the whole-value stores take
	inline checked_result<script_value*> static_component_cell(const member_expr* m, engine& eng, const environment& env,
	                                                           const string_symbolizer& symbols, const class_definition* access_context) {
		if (auto ns = resolve_namespace_store(eng, m, symbols)) {
			if (!ns->cell) {
				return checked_result<script_value*>(make_error_code(runtime_error_code::type_mismatch), "Cannot assign to '{0}': it is read-only", m->member_id);
			}
			return ns->cell;
		}
		auto cls = resolve_static_store_class(m, env, symbols, access_context);
		if (!cls) {
			return cls.error_value();
		}
		script_value* cell = cls.value()->get_static_field_ptr(m->member_id);
		if (!cell) {
			return checked_result<script_value*>(make_error_code(runtime_error_code::undefined_variable),
				"Cannot assign to static member: field '{0}' not found", m->member_id);
		}
		return cell;
	}

	// The whole store: the component named, the target resolved, the value narrowed into place. Returns the stored
	// component (the value a consumed assignment expression yields)
	template <typename IdentCell, typename StaticCell>
	inline checked_result<script_value> assign_component(script_value& base, const member_expr* member, const script_value& value,
	                                                     IdentCell&& ident_cell, StaticCell&& static_cell, const caller_frame_view& caller,
	                                                     environment* caller_env, engine* eng, string_symbolizer* symbolizer) {
		const vk k = base.deref().unchecked_vec_kind();
		const int c = component_of(k, member->member);
		if (c < 0) {
			return checked_result<script_value>(make_error_code(runtime_error_code::member_not_found), no_member_text(k), member->member_id);
		}
		const expression* base_expr = member->object.get();
		component_target t(eng);
		if (!base.is_reference() && base_expr->get_type() == node_type::member_expr && static_cast<const member_expr*>(base_expr)->is_static) {
			auto cell = static_cell(static_cast<const member_expr*>(base_expr));
			if (!cell) {
				return cell.error_value();
			}
			t.storage = final_referent(*cell.value());
			if (!is_kind(t.storage->raw_storage_index())) { t.storage = nullptr; }
		} else {
			t = resolve_component_target(base, base_expr, ident_cell, caller, caller_env, eng, symbolizer);
		}
		if (!t.storage) {
			return checked_result<script_value>(make_error_code(runtime_error_code::invalid_assignment_target), temporary_member_text);
		}
		const script_value& rhs = value.deref();
		auto stored = rhs.raw_storage_index() == script_value::TYPEID_CPP_BOUND
			? store_component(*t.storage, c, rhs.bound_decoded_temp(), eng)
			: store_component(*t.storage, c, rhs, eng);
		if (stored) { t.commit(); }
		return stored;
	}

	// A component argument (`h(v.x)` for a host T&, core/function_binder.hpp): a reference to the math value the
	// component is part of, and in c the component (-1 and a non-reference when the argument is none). The base resolves
	// as a component store's does; share turns an identifier's or a static's storage into a reference that outlives the call.
	template <typename IdentCell, typename StaticCell, typename Share>
	inline checked_result<script_value> component_argument(const member_expr* m, int& c, IdentCell&& ident_cell, StaticCell&& static_cell,
	                                                       Share&& share, const caller_frame_view& caller, environment* caller_env,
	                                                       engine* eng, string_symbolizer* symbolizer) {
		c = -1;
		script_value none(std::monostate{}, eng);
		const expression* base_expr = m ? m->object.get() : nullptr;
		if (!base_expr || m->is_static || m->null_safe || m->is_arrow || !any_component_name(m->member)) {
			return none;
		}
		script_value base(std::monostate{}, eng);
		if (base_expr->get_type() == node_type::member_expr && static_cast<const member_expr*>(base_expr)->is_static) {
			auto cell = static_cell(static_cast<const member_expr*>(base_expr));
			if (!cell) {
				return cell.error_value();
			}
			base = share(*cell.value());
		} else if (base_expr->get_type() == node_type::identifier_expr) {
			script_value* cell = ident_cell(static_cast<const identifier_expr*>(base_expr));
			if (!cell) {
				return none;
			}
			base = share(*cell);
		} else if (is_ref_bindable_lvalue(base_expr)) {
			auto resolved = resolve_ref_lvalue(base_expr, caller, caller_env, eng, symbolizer);
			if (!resolved) {
				return resolved.error() == make_error_code(runtime_error_code::invalid_reference) ? checked_result<script_value>(none) : resolved;
			}
			base = std::move(resolved.value());
		}
		if (!base.is_reference()) {
			return none;
		}
		auto value = ref_checked_deref(base);
		if (!value || !is_kind(value.value().raw_storage_index())) {
			return none;
		}
		c = component_of(value.value().unchecked_vec_kind(), m->member);
		return c < 0 ? none : base;
	}

} // namespace jai::detail::math

#endif // __JAISCRIPT_DETAIL_MATH_COMPONENT_STORE_HPP__
