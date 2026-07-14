#pragma once

#ifndef __JAISCRIPT_DETAIL_CHAR_PROMOTION_HPP__
#define __JAISCRIPT_DETAIL_CHAR_PROMOTION_HPP__

// C++-style integral promotion for the char domain, shared VERBATIM by the vm and the
// interpreter (parity by construction). A char operand enters ARITHMETIC and BITWISE
// binary operators as an int64 in 0..255 — char is unsigned by language spec; signed
// char is a binary-data footgun the language opts out of.
//
// Strictly additive by construction: promotion fires only when both operands are
// numeric-or-char, so every previously-working shape keeps its meaning — char+string
// still concatenates text, object custom operators still receive the raw char, and
// comparison operators keep their native char rows. There is no implicit int->char.

#include <jaiscript/core/value.hpp>

namespace jai::detail {

	inline bool char_operands_promote(size_t left_index, size_t right_index) {
		if (left_index != script_value::TYPEID_CHAR && right_index != script_value::TYPEID_CHAR) {
			return false;
		}
		auto numeric_or_char = [](size_t index) {
			return index == script_value::TYPEID_INT || index == script_value::TYPEID_FLOAT ||
				index == script_value::TYPEID_CHAR;
		};
		return numeric_or_char(left_index) && numeric_or_char(right_index);
	}

	inline script_value char_promoted(const script_value& value, engine* eng) {
		if (value.raw_storage_index() == script_value::TYPEID_CHAR) {
			return script_value(static_cast<script_int>(static_cast<unsigned char>(value.unchecked_as_char())), eng);
		}
		return value;
	}

}

#endif
