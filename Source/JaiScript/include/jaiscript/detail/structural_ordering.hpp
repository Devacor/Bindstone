#pragma once
#include <jaiscript/detail/structural_equality.hpp>
#include <jaiscript/detail/math_values.hpp>
#include <algorithm>
#include <compare>
#include <cmath>
#include <bit>
#include <optional>

namespace jai::detail {
	inline script_int float_order(script_float a_left, script_float a_right) noexcept {
		// Natural finite order; signed zeros equivalent; all NaNs form the final group.
		if (a_left < a_right) { return -1; }
		if (a_left > a_right) { return 1; }
		if (a_left == a_right) { return 0; }
		return script_int(std::isnan(a_left)) - script_int(std::isnan(a_right));
	}
	template<class T> inline script_int order_sign(const T& a_order) noexcept {
		return a_order < 0 ? -1 : a_order > 0 ? 1 : 0;
	}
	inline std::optional<bool> builtin_identity_equal(const script_value& a_left_in, const script_value& a_right_in) {
		const auto& a_left = a_left_in.deref(); const auto& a_right = a_right_in.deref();
		if (a_left.is_function() && a_right.is_function()) { return (a_left <=> a_right) == 0; }
		if (a_left.is_shared_ptr_type() && a_right.is_shared_ptr_type()) { return (a_left <=> a_right) == 0; }
		if (a_left.is_weak_ptr() && a_right.is_weak_ptr()) {
			const auto left = a_left.get_weak_ptr(), right = a_right.get_weak_ptr();
			return !left.owner_before(right) && !right.owner_before(left);
		}
		return std::nullopt;
	}
	template<class Compare, class Unwinding>
	std::optional<checked_result<script_value>> builtin_order(const script_value& a_left_in, const script_value& a_right_in,
		engine* a_engine, size_t& a_depth, Compare&& a_compare, Unwinding&& a_unwinding) {
		const auto& a_left = a_left_in.deref(); const auto& a_right = a_right_in.deref();
		auto result = [&](script_int a_order) { return script_value(a_order, a_engine); };
		if (a_left.is_null() && a_right.is_null()) { return result(0); }
		if (a_left.is_bool() && a_right.is_bool()) { return result(script_int(a_left.unchecked_as_bool()) - script_int(a_right.unchecked_as_bool())); }
		if (a_left.is_function() && a_right.is_function()) { return result(order_sign(a_left <=> a_right)); }
		if (a_left.is_object() && a_right.is_object()) {
			const auto left = a_left.get_object_holder(), right = a_right.get_object_holder();
			if (left && right && left->is_coroutine_handle && right->is_coroutine_handle) { return result(order_sign(a_left <=> a_right)); }
		}
		if (a_left.is_shared_ptr_type() && a_right.is_shared_ptr_type()) { return result(order_sign(a_left <=> a_right)); }
		if (a_left.is_weak_ptr() && a_right.is_weak_ptr()) {
			const auto left = a_left.get_weak_ptr(), right = a_right.get_weak_ptr();
			return result(left.owner_before(right) ? -1 : right.owner_before(left) ? 1 : 0);
		}
		if (math::is_kind(a_left.raw_storage_index()) && math::is_kind(a_right.raw_storage_index())) {
			const auto left = a_left.unchecked_vec_kind(), right = a_right.unchecked_vec_kind();
			if (math::is_color(left) != math::is_color(right)) { return std::nullopt; }
			const size_t ln = math::dims(left), rn = math::dims(right);
			for (size_t i = 0; i < std::min(ln, rn); ++i) {
				const auto lv = math::integral(left) ? script_float(math::i32_at(a_left, i)) : script_float(math::f32_at(a_left, i));
				const auto rv = math::integral(right) ? script_float(math::i32_at(a_right, i)) : script_float(math::f32_at(a_right, i));
				if (const auto cmp = float_order(lv, rv)) { return result(cmp); }
			}
			return result(ln < rn ? -1 : ln > rn ? 1 : 0);
		}
		if (!((a_left.is_array() && a_right.is_array()) || (a_left.is_map() && a_right.is_map()))) { return std::nullopt; }
		if (a_depth >= k_structural_equality_max_depth) {
			return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation), structural_equality_depth_text);
		}
		equality_depth_scope depth(a_depth);
		auto element_order = [&](const script_value& a, const script_value& b) -> checked_result<script_value> {
			auto answer = a_compare(a, b);
			if (!answer) { return answer.error_value(); }
			if (a_unwinding()) { return result(0); }
			const auto& value = answer.value().deref();
			if (value.is_int()) { return result(order_sign(value.unchecked_as_int())); }
			if (value.is_float()) { return result(order_sign(value.unchecked_as_float())); }
			return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "operator<=> must return a numeric ordering");
		};
		if (a_left.is_array()) {
			const auto left = a_left.unchecked_get_array_storage(), right = a_right.unchecked_get_array_storage();
			if (left->kind() == right->kind()) {
				switch (left->kind()) {
					case script_array::kind_t::i64: return result(order_sign(std::lexicographical_compare_three_way(left->ints().begin(), left->ints().end(), right->ints().begin(), right->ints().end())));
					case script_array::kind_t::f64: return result(order_sign(std::lexicographical_compare_three_way(left->floats().begin(), left->floats().end(), right->floats().begin(), right->floats().end(), [](auto a, auto b) { const auto cmp = float_order(a,b); return cmp < 0 ? std::weak_ordering::less : cmp > 0 ? std::weak_ordering::greater : std::weak_ordering::equivalent; })));
					case script_array::kind_t::b8: case script_array::kind_t::c8: case script_array::kind_t::u8:
						return result(order_sign(std::lexicographical_compare_three_way(left->bytes().begin(), left->bytes().end(), right->bytes().begin(), right->bytes().end())));
					case script_array::kind_t::i8:
						return result(order_sign(std::lexicographical_compare_three_way(left->bytes().begin(), left->bytes().end(), right->bytes().begin(), right->bytes().end(), [](uint8_t a, uint8_t b) { return static_cast<int8_t>(a) <=> static_cast<int8_t>(b); })));
					case script_array::kind_t::str:
						return result(order_sign(std::lexicographical_compare_three_way(left->strs().begin(), left->strs().end(), right->strs().begin(), right->strs().end(), [](const auto& a, const auto& b) { return *a <=> *b; })));
					case script_array::kind_t::v2: case script_array::kind_t::iv2: case script_array::kind_t::v3: case script_array::kind_t::iv3: case script_array::kind_t::v4: case script_array::kind_t::rgba: {
						const size_t count = std::min(left->size(), right->size()), stride = script_array::vec_stride(left->kind());
						for (size_t i = 0; i < count; ++i) {
							for (size_t lane = 0; lane < stride; ++lane) {
								const uint32_t lw = left->vec_word(i,lane), rw = right->vec_word(i,lane);
								const auto cmp = script_array::vec_integral(left->kind()) ? order_sign(std::bit_cast<int32_t>(lw) <=> std::bit_cast<int32_t>(rw)) : float_order(std::bit_cast<float>(lw), std::bit_cast<float>(rw));
								if (cmp) { return result(cmp); }
							}
						}
						return result(left->size() < right->size() ? -1 : left->size() > right->size() ? 1 : 0);
					}
					default: break;
				}
			}
			const auto leftValues = left->materialize_values(a_engine), rightValues = right->materialize_values(a_engine);
			const auto ln = leftValues.size(), rn = rightValues.size();
			for (size_t i = 0; i < std::min(ln, rn); ++i) {
				auto answer = element_order(leftValues[i], rightValues[i]);
				if (!answer || a_unwinding() || answer.value().unchecked_as_int() != 0) { return answer; }
			}
			return result(ln < rn ? -1 : ln > rn ? 1 : 0);
		}
		const auto left = a_left.unchecked_get_map_storage(), right = a_right.unchecked_get_map_storage();
		using Entry = std::pair<script_value, script_value>;
		std::vector<Entry> leftEntries(left->begin(), left->end()), rightEntries(right->begin(), right->end());
		for (size_t i = 0; i < std::min(leftEntries.size(), rightEntries.size()); ++i) {
			const auto& li = leftEntries[i]; const auto& ri = rightEntries[i];
			// Keys retain map lookup/iteration identity semantics; values use language ordering.
			if (const auto cmp = order_sign(li.first <=> ri.first)) { return result(cmp); }
			auto answer = element_order(li.second, ri.second);
			if (!answer || a_unwinding() || answer.value().unchecked_as_int() != 0) { return answer; }
		}
		return result(leftEntries.size() < rightEntries.size() ? -1 : leftEntries.size() > rightEntries.size() ? 1 : 0);
	}
}
