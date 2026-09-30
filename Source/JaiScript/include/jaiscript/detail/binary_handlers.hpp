#pragma once

#ifndef __JAISCRIPT_DETAIL_BINARY_HANDLERS_HPP__
#define __JAISCRIPT_DETAIL_BINARY_HANDLERS_HPP__

// The general binary operators, run once a backend's fast lanes decline: one body per operator family, shared by both
// backends (docs/invariants.md 4 and 6). Each entry normalizes a cpp-bound operand, a utf8_string operand and a char pair
// first, then answers numbers and strings, then (only when an operand holds an object) unwraps transparent wrappers,
// answers again and asks the class's operator method. The host supplies:
//   script_value make_int(script_int), make_float(script_float), make_bool(bool); engine* engine()
//   checked_result<script_value> concat(const script_value&, const script_value&)   // string +, memory-charged
//   std::optional<checked_result<script_value>> object_arithmetic(l, r, uint64_t method_id)
//   std::optional<checked_result<bool>> object_comparison(l, r, uint64_t method_id)
//   std::optional<checked_result<bool>> object_equality(l, r); bool defines_custom_equality(const script_value&)
//   bool unwinding(); bool parallel_worker(); size_t& equality_depth()
//   checked_result<script_value> equal(l, r)   // the backend's == entry, for memberwise element comparison

#include <jaiscript/core/value.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/scalar_ops.hpp>
#include <jaiscript/detail/char_promotion.hpp>
#include <jaiscript/detail/utf8_operands.hpp>
#include <jaiscript/detail/structural_equality.hpp>
#include <cmath>
#include <compare>
#include <optional>

namespace jai::detail {

	enum class arith_op : uint8_t { add, sub, mul, div, mod };
	enum class order_op : uint8_t { less, less_equal, greater, greater_equal };
	enum class bit_op : uint8_t { bit_and, bit_or, bit_xor, shift_left, shift_right };

	inline bool numeric_storage(size_t index) noexcept {
		return index == script_value::TYPEID_INT || index == script_value::TYPEID_FLOAT;
	}
	inline bool object_storage(size_t index) noexcept {
		return index == script_value::TYPEID_OBJECT || index == script_value::TYPEID_SHARED_PTR;
	}
	inline script_float as_script_float(const script_value& v, size_t index) noexcept {
		return index == script_value::TYPEID_INT ? script_float(v.unchecked_as_int()) : v.unchecked_as_float();
	}
	inline checked_result<script_value> type_mismatch_error(const char* text) {
		return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), text);
	}

	// A cpp-bound operand (index 14) skips a fast lane's raw-index gates; its zero divisor keeps the lane's error text.
	// Each lane passes the (op x type-pair) coverage of its own inline zero checks.
	struct bound_zero_divisor_error { runtime_error_code code; const char* text; };
	inline std::optional<bound_zero_divisor_error> bound_fastpath_zero_divisor(token_type op, const script_value& left, const script_value& right,
	                                                                           bool float_div_covered, bool float_mod_covered) {
		if (op != token_type::slash && op != token_type::percent) { return std::nullopt; }
		if (left.is_int() && right.is_int()) {
			if (right.unchecked_as_int() != 0) { return std::nullopt; }
			return op == token_type::slash
				? bound_zero_divisor_error{runtime_error_code::division_by_zero, "Division by zero in integer operation"}
				: bound_zero_divisor_error{runtime_error_code::modulo_by_zero, "Modulo by zero in integer operation"};
		}
		if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
			if (!(op == token_type::slash ? float_div_covered : float_mod_covered)) { return std::nullopt; }
			const script_float rf = right.is_int() ? static_cast<script_float>(right.unchecked_as_int()) : right.unchecked_as_float();
			if (rf != 0.0) { return std::nullopt; }
			return op == token_type::slash
				? bound_zero_divisor_error{runtime_error_code::division_by_zero, "Division by zero in float operation"}
				: bound_zero_divisor_error{runtime_error_code::modulo_by_zero, "Modulo by zero in float operation"};
		}
		return std::nullopt;
	}

	template <arith_op Op>
	constexpr const char* arith_invalid_text() {
		if constexpr (Op == arith_op::add) { return "Invalid operands for + operator"; }
		else if constexpr (Op == arith_op::sub) { return "Invalid operands for - operator"; }
		else if constexpr (Op == arith_op::mul) { return "Invalid operands for * operator"; }
		else if constexpr (Op == arith_op::div) { return "Invalid operands for / operator"; }
		else { return "Invalid operands for % operator"; }
	}
	template <arith_op Op>
	constexpr token_type arith_token() {
		if constexpr (Op == arith_op::add) { return token_type::plus; }
		else if constexpr (Op == arith_op::sub) { return token_type::minus; }
		else if constexpr (Op == arith_op::mul) { return token_type::star; }
		else if constexpr (Op == arith_op::div) { return token_type::slash; }
		else { return token_type::percent; }
	}

	// Numbers (and a string on either side of +); nullopt for anything else
	template <arith_op Op, class Host>
	inline std::optional<checked_result<script_value>> arithmetic_on_values(Host& h, const script_value& l, const script_value& r, size_t li, size_t ri) {
		constexpr token_type op = arith_token<Op>();
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) {
			script_int out;
			if (const auto fault = int_arith(op, l.unchecked_as_int(), r.unchecked_as_int(), out); fault != scalar_fault::none) {
				return scalar_fault_error<checked_result<script_value>>(op, fault, false);
			}
			return h.make_int(out);
		}
		if (numeric_storage(li) && numeric_storage(ri)) {
			script_float out;
			if (const auto fault = float_arith(op, as_script_float(l, li), as_script_float(r, ri), out); fault != scalar_fault::none) {
				return scalar_fault_error<checked_result<script_value>>(op, fault, true);
			}
			return h.make_float(out);
		}
		if constexpr (Op == arith_op::add) {
			if (li == script_value::TYPEID_STRING || ri == script_value::TYPEID_STRING) { return h.concat(l, r); }
		}
		return std::nullopt;
	}

	template <arith_op Op, class Host>
	checked_result<script_value> binary_arithmetic(Host& h, uint64_t method_id, const script_value& left, const script_value& right) {
		const size_t li = left.raw_storage_index();
		const size_t ri = right.raw_storage_index();
		// invariant 4: a bound operand decodes to a detached temp (re-entry depth is exactly 1)
		if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			return binary_arithmetic<Op>(h, method_id, li == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
			                             ri == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
		}
		if (utf8_operands(li, ri)) [[unlikely]] {
			return utf8_rewrapped(binary_arithmetic<Op>(h, method_id, utf8_unwrapped(left, h.engine()), utf8_unwrapped(right, h.engine())), h.engine());
		}
		if (char_operands_promote(li, ri)) [[unlikely]] {
			return binary_arithmetic<Op>(h, method_id, char_promoted(left, h.engine()), char_promoted(right, h.engine()));
		}
		if (!object_storage(li) && !object_storage(ri)) {
			if (auto result = arithmetic_on_values<Op>(h, left, right, li, ri)) { return std::move(*result); }
			return type_mismatch_error(arith_invalid_text<Op>());
		}
		const script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
		const script_value unwrapped_right = right.try_unwrap_transparent_wrapper();
		if (auto result = arithmetic_on_values<Op>(h, unwrapped_left, unwrapped_right, unwrapped_left.raw_storage_index(), unwrapped_right.raw_storage_index())) {
			return std::move(*result);
		}
		if (auto custom = h.object_arithmetic(left, right, method_id)) { return std::move(*custom); }
		return type_mismatch_error(arith_invalid_text<Op>());
	}

	template <order_op Op>
	constexpr const char* order_invalid_text() {
		if constexpr (Op == order_op::less) { return "Invalid operands for < operator"; }
		else if constexpr (Op == order_op::less_equal) { return "Invalid operands for <= operator"; }
		else if constexpr (Op == order_op::greater) { return "Invalid operands for > operator"; }
		else { return "Invalid operands for >= operator"; }
	}
	template <order_op Op, class T>
	constexpr bool ordered(const T& a, const T& b) {
		if constexpr (Op == order_op::less) { return a < b; }
		else if constexpr (Op == order_op::less_equal) { return a <= b; }
		else if constexpr (Op == order_op::greater) { return a > b; }
		else { return a >= b; }
	}

	template <order_op Op, class Host>
	inline std::optional<script_value> ordering_on_values(Host& h, const script_value& l, const script_value& r, size_t li, size_t ri) {
		if (li == script_value::TYPEID_INT && ri == script_value::TYPEID_INT) { return h.make_bool(ordered<Op>(l.unchecked_as_int(), r.unchecked_as_int())); }
		if (numeric_storage(li) && numeric_storage(ri)) { return h.make_bool(ordered<Op>(as_script_float(l, li), as_script_float(r, ri))); }
		if (li == script_value::TYPEID_STRING && ri == script_value::TYPEID_STRING) { return h.make_bool(ordered<Op>(l.unchecked_as_string(), r.unchecked_as_string())); }
		return std::nullopt;
	}

	template <order_op Op, class Host>
	checked_result<script_value> binary_ordering(Host& h, uint64_t method_id, const script_value& left, const script_value& right) {
		const size_t li = left.raw_storage_index();
		const size_t ri = right.raw_storage_index();
		if (li == script_value::TYPEID_CPP_BOUND || ri == script_value::TYPEID_CPP_BOUND) [[unlikely]] {
			return binary_ordering<Op>(h, method_id, li == script_value::TYPEID_CPP_BOUND ? left.bound_decoded_temp() : left,
			                           ri == script_value::TYPEID_CPP_BOUND ? right.bound_decoded_temp() : right);
		}
		if (utf8_operands(li, ri)) [[unlikely]] {
			return utf8_rewrapped(binary_ordering<Op>(h, method_id, utf8_unwrapped(left, h.engine()), utf8_unwrapped(right, h.engine())), h.engine());
		}
		if (char_operands_promote(li, ri)) [[unlikely]] {
			return binary_ordering<Op>(h, method_id, char_promoted(left, h.engine()), char_promoted(right, h.engine()));
		}
		if (!object_storage(li) && !object_storage(ri)) {
			if (auto result = ordering_on_values<Op>(h, left, right, li, ri)) { return std::move(*result); }
			return type_mismatch_error(order_invalid_text<Op>());
		}
		const script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
		const script_value unwrapped_right = right.try_unwrap_transparent_wrapper();
		if (auto result = ordering_on_values<Op>(h, unwrapped_left, unwrapped_right, unwrapped_left.raw_storage_index(), unwrapped_right.raw_storage_index())) {
			return std::move(*result);
		}
		if (auto custom = h.object_comparison(left, right, method_id)) {
			if (!*custom) { return custom->error_value(); }
			return h.make_bool(custom->value());
		}
		// a script throw inside the operator method is in flight: it leaves, not the invalid-operands error
		if (h.unwinding()) { return h.make_bool(false); }
		return type_mismatch_error(order_invalid_text<Op>());
	}

	// weak_ptr against null: equal when the handle expired (a default-constructed one included)
	inline std::optional<bool> weak_equals_null(const script_value& left, const script_value& right) {
		if (!((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr()))) { return std::nullopt; }
		if (left.is_weak_ptr()) { return left.get_weak_ptr().expired(); }
		if (right.is_weak_ptr()) { return right.get_weak_ptr().expired(); }
		if (auto holder = right.get_object_holder()) { return holder->type_name == "weak_ptr_holder" && !holder->data; }
		return true;
	}

	// == : memberwise on heavy values (detail/structural_equality.hpp), identity for handles and host objects. A bound
	// operand is not decoded: an opaque host pointer compares by identity.
	template <class Host>
	checked_result<script_value> binary_equal(Host& h, const script_value& left, const script_value& right) {
		if (utf8_operands(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return utf8_rewrapped(binary_equal(h, utf8_unwrapped(left, h.engine()), utf8_unwrapped(right, h.engine())), h.engine());
		}
		if (char_operands_promote(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return binary_equal(h, char_promoted(left, h.engine()), char_promoted(right, h.engine()));
		}
		if (auto expired = weak_equals_null(left, right)) { return h.make_bool(*expired); }

		const script_value unwrapped_left = left.try_unwrap_transparent_wrapper();
		const script_value unwrapped_right = right.try_unwrap_transparent_wrapper();
		if ((unwrapped_left.is_int() || unwrapped_left.is_float()) && (unwrapped_right.is_int() || unwrapped_right.is_float())) {
			// two ints compare exactly: a double loses precision beyond 2^53
			if (unwrapped_left.is_int() && unwrapped_right.is_int()) { return h.make_bool(unwrapped_left.unchecked_as_int() == unwrapped_right.unchecked_as_int()); }
			const script_float lf = unwrapped_left.is_int() ? script_float(unwrapped_left.unchecked_as_int()) : unwrapped_left.unchecked_as_float();
			const script_float rf = unwrapped_right.is_int() ? script_float(unwrapped_right.unchecked_as_int()) : unwrapped_right.unchecked_as_float();
			return h.make_bool(lf == rf);
		}
		// null-ness is a storage property: a typed-but-uninitialized slot compares like null
		if (unwrapped_left.is_null() || unwrapped_right.is_null()) { return h.make_bool(unwrapped_left.is_null() && unwrapped_right.is_null()); }
		if (unwrapped_left.type() != unwrapped_right.type()) { return h.make_bool(false); }
		if (unwrapped_left.is_string() && unwrapped_right.is_string()) { return h.make_bool(unwrapped_left.unchecked_as_string() == unwrapped_right.unchecked_as_string()); }
		if (unwrapped_left.is_bool() && unwrapped_right.is_bool()) { return h.make_bool(unwrapped_left.unchecked_as_bool() == unwrapped_right.unchecked_as_bool()); }
		// opaque host pointers (an unregistered make_value(T*)): the same live pointer
		if (unwrapped_left.is_cpp_bound_primitive() && unwrapped_right.is_cpp_bound_primitive()) {
			return h.make_bool(unwrapped_left.get_cpp_bound_ptr() == unwrapped_right.get_cpp_bound_ptr());
		}

		auto element_equal = [&h](const script_value& a, const script_value& b) { return h.equal(a, b); };
		if (left.is_array() && right.is_array()) { return arrays_equal(left, right, h.engine(), h.equality_depth(), element_equal); }
		if (left.is_map() && right.is_map()) { return maps_equal(left, right, h.engine(), h.equality_depth(), element_equal); }

		// safe-mode workers: the operator wall would skip a custom operator== and the structural answer would diverge
		// from the serial loop
		if (h.parallel_worker() && (h.defines_custom_equality(left) || h.defines_custom_equality(right))) [[unlikely]] {
			return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
				"custom operator dispatch on class instances is not admitted in a parallel body");
		}
		if (auto custom = h.object_equality(left, right)) {
			if (!*custom) { return custom->error_value(); }
			return h.make_bool(custom->value());
		}
		// script class instances: same class, fieldwise
		if (auto structural = instances_equal(left, right, h.engine(), h.equality_depth(), element_equal)) { return std::move(*structural); }
		// host objects and other holders: the same object_holder
		auto left_holder = const_cast<script_value&>(left).get_object_holder();
		auto right_holder = const_cast<script_value&>(right).get_object_holder();
		if (left_holder && right_holder) { return h.make_bool(left_holder == right_holder); }
		return h.make_bool(false);
	}

	template <class Host>
	checked_result<script_value> binary_not_equal(Host& h, const script_value& left, const script_value& right) {
		if (auto expired = weak_equals_null(left, right)) { return h.make_bool(!*expired); }
		auto equal = h.equal(left, right);
		if (!equal) [[unlikely]] { return equal.error_value(); }
		return h.make_bool(!equal.value().unchecked_as_bool());
	}

	template <class Host>
	checked_result<script_value> binary_spaceship(Host& h, const script_value& left, const script_value& right) {
		if (utf8_operands(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return utf8_rewrapped(binary_spaceship(h, utf8_unwrapped(left, h.engine()), utf8_unwrapped(right, h.engine())), h.engine());
		}
		if (char_operands_promote(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return binary_spaceship(h, char_promoted(left, h.engine()), char_promoted(right, h.engine()));
		}
		auto sign = [&h](auto cmp) { return h.make_int(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0))); };
		if (left.raw_storage_index() == script_value::TYPEID_INT && right.raw_storage_index() == script_value::TYPEID_INT) {
			return sign(left.unchecked_as_int() <=> right.unchecked_as_int());
		}
		if ((left.is_int() || left.is_float()) && (right.is_int() || right.is_float())) {
			const script_float lf = left.is_int() ? script_float(left.unchecked_as_int()) : left.unchecked_as_float();
			const script_float rf = right.is_int() ? script_float(right.unchecked_as_int()) : right.unchecked_as_float();
			return sign(lf <=> rf);
		}
		if (left.is_string() && right.is_string()) { return sign(left.unchecked_as_string().compare(right.unchecked_as_string())); }
		return type_mismatch_error("Invalid operands for <=> operator");
	}

	template <bit_op Op>
	constexpr const char* bit_invalid_text() {
		if constexpr (Op == bit_op::bit_and) { return "Bitwise & requires integer operands"; }
		else if constexpr (Op == bit_op::bit_or) { return "Bitwise | requires integer operands"; }
		else if constexpr (Op == bit_op::bit_xor) { return "Bitwise ^ requires integer operands"; }
		else if constexpr (Op == bit_op::shift_left) { return "Left shift requires integer operands"; }
		else { return "Right shift requires integer operands"; }
	}

	// Shifts wrap mod 2^64 by contract (docs/integer_semantics.md); the amount must be 0..63
	template <bit_op Op, class Host>
	checked_result<script_value> binary_bitwise(Host& h, const script_value& left, const script_value& right) {
		if (utf8_operands(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return utf8_rewrapped(binary_bitwise<Op>(h, utf8_unwrapped(left, h.engine()), utf8_unwrapped(right, h.engine())), h.engine());
		}
		if (char_operands_promote(left.raw_storage_index(), right.raw_storage_index())) [[unlikely]] {
			return binary_bitwise<Op>(h, char_promoted(left, h.engine()), char_promoted(right, h.engine()));
		}
		if (!left.is_int() || !right.is_int()) { return type_mismatch_error(bit_invalid_text<Op>()); }
		const script_int a = left.unchecked_as_int(), b = right.unchecked_as_int();
		if constexpr (Op == bit_op::bit_and) { return h.make_int(a & b); }
		else if constexpr (Op == bit_op::bit_or) { return h.make_int(a | b); }
		else if constexpr (Op == bit_op::bit_xor) { return h.make_int(a ^ b); }
		else {
			if (b < 0 || b > 63) { return type_mismatch_error("Shift amount must be between 0 and 63"); }
			if constexpr (Op == bit_op::shift_left) { return h.make_int(a << b); }
			else { return h.make_int(a >> b); }
		}
	}

}

#endif
