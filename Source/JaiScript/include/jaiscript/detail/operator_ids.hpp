#pragma once

#ifndef __JAISCRIPT_DETAIL_OPERATOR_IDS_HPP__
#define __JAISCRIPT_DETAIL_OPERATOR_IDS_HPP__

#include <jaiscript/detail/string_symbolizer.hpp>
#include <cstdint>

namespace jai::detail {

	// A class's operator method names, interned once per backend at construction (a parallel worker never interns)
	struct operator_ids {
		uint64_t plus = 0, minus = 0, star = 0, slash = 0, percent = 0;
		uint64_t less = 0, less_equal = 0, greater = 0, greater_equal = 0;
		uint64_t equal_equal = 0, bang_equal = 0, spaceship = 0;
		uint64_t ampersand = 0, pipe = 0, caret = 0, left_shift = 0, right_shift = 0;
		uint64_t tilde = 0, call = 0;

		void intern(string_symbolizer& symbols) {
			plus = symbols.intern("+");
			minus = symbols.intern("-");
			star = symbols.intern("*");
			slash = symbols.intern("/");
			percent = symbols.intern("%");
			less = symbols.intern("<");
			less_equal = symbols.intern("<=");
			greater = symbols.intern(">");
			greater_equal = symbols.intern(">=");
			equal_equal = symbols.intern("==");
			bang_equal = symbols.intern("!=");
			spaceship = symbols.intern("<=>");
			ampersand = symbols.intern("&");
			pipe = symbols.intern("|");
			caret = symbols.intern("^");
			left_shift = symbols.intern("<<");
			right_shift = symbols.intern(">>");
			tilde = symbols.intern("~");
			call = symbols.intern("()");
		}
	};

}

#endif
