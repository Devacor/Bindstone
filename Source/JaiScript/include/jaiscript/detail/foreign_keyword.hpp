#pragma once

#include <string_view>

namespace jai::detail {

// Words and spellings other languages use that JaiScript does not: the parser names the JaiScript spelling instead of
// a generic parse error (docs/diagnostics.md "Foreign keywords"). Each is recognized only where it would otherwise fail
// to parse, so a script that uses one of these words as a name keeps working.
enum class foreign_word : unsigned char {
	const_declaration,
	const_parameter,
	virtual_method,
	delete_statement,
	new_array,
	new_without_arguments,
	goto_statement,
	address_of,
	typed_catch,
	extends_base,
	strict_equal,
	strict_not_equal,
	generator_function,
};

inline constexpr std::string_view foreign_word_text(foreign_word w) noexcept {
	switch (w) {
		case foreign_word::const_declaration:
			return "JaiScript has no 'const': declare the variable with its type or auto (a reference parameter the function never writes through is already const)";
		case foreign_word::const_parameter:
			return "JaiScript has no 'const': a reference parameter the function never writes through is already const, so write T& name";
		case foreign_word::virtual_method:
			return "JaiScript has no 'virtual': every method dispatches dynamically; mark the subclass's method 'override'";
		case foreign_word::delete_statement:
			return "JaiScript has no 'delete': an object is freed when its last owner lets go; set the handle to null to release it";
		case foreign_word::new_array:
			return "'new' makes one shared object (new T(args)); an array is a value: array<T> a = [];";
		case foreign_word::new_without_arguments:
			return "'new T' needs its constructor's parentheses: new T() or new T(args)";
		case foreign_word::goto_statement:
			return "JaiScript has no 'goto': use break, continue, return or a flag";
		case foreign_word::address_of:
			return "JaiScript has no address-of '&x': bind a reference with auto& r = x, or pass x to a T& parameter";
		case foreign_word::typed_catch:
			return "catch takes one name and no type: catch (e) { ... }; e.value is what was thrown and type_of(e.value) its type";
		case foreign_word::extends_base:
			return "JaiScript has no 'extends': derive with class Name : Base";
		case foreign_word::strict_equal:
			return "JaiScript has no '===': == never converts between types, so a == b already compares without coercion";
		case foreign_word::strict_not_equal:
			return "JaiScript has no '!==': != never converts between types, so a != b already compares without coercion";
		case foreign_word::generator_function:
			return "JaiScript has no 'function*': a generator is a coroutine, coroutine name(...) { yield v; }";
	}
	return {};
}

} // namespace jai::detail
