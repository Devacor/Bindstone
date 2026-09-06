#pragma once

#include <jaiscript/core/types.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <type_traits>

namespace jai::detail {

inline constexpr std::string_view float_to_integer_error =
	"Cannot convert float to integer: non-finite or out of range";

// The checked script boundary, independent of the integer-arithmetic wrap policy.
// The uint64_t specialization is only for RNG seeds; script ints remain int64_t.
template<typename Integer = script_int>
inline checked_result<Integer> checked_float_to_integer(script_float value) noexcept {
	static_assert(std::is_same_v<Integer, script_int> || std::is_same_v<Integer, uint64_t>);
	// Ordered comparisons reject NaN and infinities too. For binary64 there is no
	// fractional value adjacent to either int64 limit: checking before truncation
	// is equivalent, without an extra floor/trunc call. The upper bound is exclusive
	// because double(INT64_MAX/UINT64_MAX) rounds UP to 2^63/2^64.
	// Unsigned conversion also accepts (-1, 0), which truncates to zero.
	const bool in_range = std::is_signed_v<Integer>
		? value >= -0x1p63 && value < 0x1p63
		: value > -1.0 && value < 0x1p64;
	if (!in_range) {
		return checked_result<Integer>(make_error_code(runtime_error_code::invalid_numeric_operand), float_to_integer_error);
	}
	return static_cast<Integer>(value);
}

// The range half of checked_float_to_integer on its own: the typed-buffer store lanes admit
// a float rhs only when this holds and send everything else through the enforcing path,
// which raises the kernel's error.
inline bool float_fits_script_int(script_float value) noexcept {
	return value >= -0x1p63 && value < 0x1p63;
}

} // namespace jai::detail
