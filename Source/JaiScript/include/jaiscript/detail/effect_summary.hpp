#pragma once

#ifndef __JAISCRIPT_DETAIL_EFFECT_SUMMARY_HPP__
#define __JAISCRIPT_DETAIL_EFFECT_SUMMARY_HPP__

// One answer to "may evaluating this expression run user code", under a named policy, for every consumer of both
// backends (the call-argument capture stamp, the transient-read marks, the compiler's operand ordering and graft guards):
//   gated            a builtin value operator or subscript counts as free: a global operator or "[]" registration flips
//                    the engine's runtime gate (has_custom_binary_ops) and the consumer honors it
//   gated_intrinsics gated, and a math:: intrinsic call over free arguments counts as free (both backends run the kernel)
//   strict           nothing trusts the runtime gate: a value operator is free only over operands the checker proved
//                    primitive, a subscript only into a container whose elements the checker proved (a class operator or
//                    "[]" method runs user code whatever the gate says)
// The children an expression evaluates come from one visitor, in evaluation order (the callee before its arguments, an
// assignment's value before its target: docs/invariants.md 12 and 13).

#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <cstdint>

namespace jai::detail {

	template <class F>
	void for_each_evaluated_child(const expression* e, F&& f) {
		if (!e) { return; }
		switch (e->get_type()) {
			case node_type::binary_expr: {
				auto* b = static_cast<const binary_expr*>(e);
				f(b->left.get());
				f(b->right.get());
				return;
			}
			case node_type::unary_expr:
				f(static_cast<const unary_expr*>(e)->operand.get());
				return;
			case node_type::ternary_expr: {
				auto* t = static_cast<const ternary_expr*>(e);
				f(t->condition.get());
				f(t->then_expression.get());
				f(t->else_expression.get());
				return;
			}
			case node_type::call_expr: {
				auto* c = static_cast<const call_expr*>(e);
				f(c->callee.get());
				for (const auto& a : c->arguments) { f(a.get()); }
				return;
			}
			case node_type::member_expr:
				f(static_cast<const member_expr*>(e)->object.get());
				return;
			case node_type::assignment_expr: {
				auto* a = static_cast<const assignment_expr*>(e);
				f(a->value.get());
				f(a->target.get());
				return;
			}
			case node_type::array_literal_expr:
				for (const auto& el : static_cast<const array_literal_expr*>(e)->elements) { f(el.get()); }
				return;
			case node_type::map_literal_expr:
				for (const auto& kv : static_cast<const map_literal_expr*>(e)->entries) { f(kv.first.get()); f(kv.second.get()); }
				return;
			case node_type::new_expr:
				for (const auto& a : static_cast<const new_expr*>(e)->arguments) { f(a.get()); }
				return;
			case node_type::cast_expr:
				f(static_cast<const cast_expr*>(e)->operand.get());
				return;
			case node_type::throw_expr:
				f(static_cast<const throw_expr*>(e)->value.get());
				return;
			case node_type::yield_expr:
				f(static_cast<const yield_expr*>(e)->value.get());
				return;
			case node_type::include_expr:
				f(static_cast<const include_expr*>(e)->path_expr.get());
				return;
			case node_type::import_expr:
				for (const auto& a : static_cast<const import_expr*>(e)->path_exprs) { f(a.get()); }
				return;
			case node_type::lambda_expr:
				for (const auto& cap : static_cast<const lambda_expr*>(e)->captures) { f(cap.initializer.get()); }
				return;
			default:
				return;
		}
	}

	enum class callout_policy : uint8_t { gated, gated_intrinsics, strict };

	inline bool builtin_value_operator(token_type t) noexcept {
		switch (t) {
			case token_type::plus: case token_type::minus: case token_type::star: case token_type::slash: case token_type::percent:
			case token_type::less: case token_type::less_equal: case token_type::greater: case token_type::greater_equal:
			case token_type::equal_equal: case token_type::bang_equal: case token_type::spaceship:
			case token_type::ampersand: case token_type::pipe: case token_type::caret: case token_type::left_shift: case token_type::right_shift:
				return true;
			default:
				return false;
		}
	}

	// strict: the checker proved the operator's operands, or the subscript's container elements, builtin
	inline bool proven_builtin_binary(const binary_expr* b) noexcept {
		const uint8_t cls = b->static_operand_class;
		if (b->op.type == token_type::left_bracket) {
			return cls == operand_class_object_element || cls == operand_class_int_int || cls == operand_class_float_float;
		}
		return cls == operand_class_int_int || cls == operand_class_float_float || cls == operand_class_int_float ||
		       cls == operand_class_float_int || cls == operand_class_string_string || cls == operand_class_bool_bool;
	}

	template <callout_policy P>
	bool may_run_user_code(const expression* e) {
		if (!e) { return false; }
		switch (e->get_type()) {
			case node_type::literal_expr:
			case node_type::identifier_expr:
			case node_type::this_expr:
			case node_type::super_expr:
				return false;
			case node_type::binary_expr: {
				auto* b = static_cast<const binary_expr*>(e);
				const bool logical = b->op.type == token_type::ampersand_ampersand || b->op.type == token_type::pipe_pipe;
				const bool builtin = b->op.type == token_type::left_bracket || builtin_value_operator(b->op.type);
				if (!logical && !builtin) { return true; }
				if constexpr (P == callout_policy::strict) {
					if (builtin && !proven_builtin_binary(b)) { return true; }
				}
				break;
			}
			case node_type::unary_expr: {
				const token_type t = static_cast<const unary_expr*>(e)->op.type;
				if (t != token_type::bang && t != token_type::minus && t != token_type::plus && t != token_type::tilde) { return true; }   // ++/-- write
				break;
			}
			case node_type::ternary_expr:
				break;
			case node_type::call_expr: {
				if constexpr (P == callout_policy::gated_intrinsics) {
					auto* c = static_cast<const call_expr*>(e);
					if (!c->callee || c->callee->get_type() != node_type::member_expr || c->arguments.size() > 8 ||
					    math_intrinsic_for_call(static_cast<const member_expr*>(c->callee.get())) == math_fn::none) {
						return true;
					}
					for (const auto& a : c->arguments) {
						if (may_run_user_code<P>(a.get())) { return true; }
					}
					return false;   // the callee names the intrinsic; it evaluates nothing
				}
				return true;
			}
			default:
				return true;   // member (a getter), call, assignment, new, lambda, container literals, casts, ...
		}
		bool runs = false;
		for_each_evaluated_child(e, [&](const expression* child) { if (!runs && may_run_user_code<P>(child)) { runs = true; } });
		return runs;
	}

}

#endif
