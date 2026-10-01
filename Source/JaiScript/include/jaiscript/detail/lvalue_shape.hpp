#pragma once

#ifndef __JAISCRIPT_DETAIL_LVALUE_SHAPE_HPP__
#define __JAISCRIPT_DETAIL_LVALUE_SHAPE_HPP__

// The structural lvalue shapes a reference binds from the AST alone (detail/ref_lvalue.hpp resolves them); kept apart so
// the parse-time passes classify without the runtime kernel's headers.

#include <jaiscript/detail/ast.hpp>

namespace jai::detail {

	// Structural classifier for ref-bindable lvalue arguments: chains of plain member
	// steps (no ::, ?., ->) and subscripts with int-literal or identifier indices over
	// an identifier or `this` base, at least one step deep. Bare identifiers keep the
	// existing slot/env metadata path; everything else keeps the non-lvalue error.
	inline bool is_ref_bindable_lvalue(const expression* e) {
		bool has_step = false;
		while (e) {
			const node_type t = e->get_type();
			if (t == node_type::member_expr) {
				const auto* m = static_cast<const member_expr*>(e);
				if (m->is_static || m->null_safe || m->is_arrow) {
					return false;
				}
				e = m->object.get();
				has_step = true;
			} else if (t == node_type::binary_expr) {
				const auto* b = static_cast<const binary_expr*>(e);
				if (b->op.type != token_type::left_bracket || !b->right) {
					return false;
				}
				const node_type it = b->right->get_type();
				if (it == node_type::literal_expr) {
					if (static_cast<const literal_expr*>(b->right.get())->value.raw_storage_index() != script_value::TYPEID_INT) {
						return false;
					}
				} else if (it != node_type::identifier_expr) {
					return false;
				}
				e = b->left.get();
				has_step = true;
			} else if (t == node_type::identifier_expr || t == node_type::this_expr) {
				return has_step;
			} else {
				return false;
			}
		}
		return false;
	}

	// Member-FINAL subset of the classifier: the ref-DECL gate (var&/auto& = obj.field
	// chains). Field reads evaluate to COPIES, so member-final initializers need the
	// kernel resolve; subscript-final initializers stay on the evaluation path - their
	// rhs-lvalue reads already mint owner-pinned references with full index generality,
	// and routing them through the kernel would change inner-reference-element aliasing
	// (see the element-share branch in resolve_ref_lvalue).
	inline bool is_member_final_ref_lvalue(const expression* e) {
		return e && e->get_type() == node_type::member_expr && is_ref_bindable_lvalue(e);
	}

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_LVALUE_SHAPE_HPP__
