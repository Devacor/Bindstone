#pragma once

#ifndef __JAISCRIPT_DETAIL_GENERICS_HPP__
#define __JAISCRIPT_DETAIL_GENERICS_HPP__

// Generics (docs/site/guide/21-generics.html). A generic is a declaration with type parameters, kept as its tokens; an instance
// is those tokens with each parameter replaced by its argument, parsed as ordinary program text and declared in the unit that
// uses it. Both backends see ordinary classes and functions: the one rule they add is the stamp (generic_instance_current).

#include "lexer.hpp"
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/types.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jai {
	class class_decl;
	class generic_decl;
	class string_symbolizer;
}

namespace jai::detail {

	// instantiation nests at most this deep (class A<T> { A<array<T>> next; } is an error, never a hang)
	inline constexpr size_t k_generic_depth_limit = 64;

	// A type parameter: its name and, for `T : Base`, the bound's tokens
	struct generic_parameter {
		std::string_view name;   // symbolizer storage
		uint64_t name_id = 0;
		std::vector<token> bound;
		source_location location;
	};

	// A generic function's value parameter as its declaration spells it: a type parameter reads as a class of its name
	struct generic_value_parameter {
		type_info_ptr type;   // null for auto
		bool is_reference = false;
		bool has_default = false;
	};

	enum class generic_kind : uint8_t { class_type, function, method };

	struct generic_definition {
		generic_kind kind = generic_kind::class_type;
		std::string_view name;   // as declared: Stack, largest, json
		std::string space;       // the namespace that declares it: "" at program scope, else "a::b"
		std::string owner;       // a method's class, by its qualified name
		bool is_static = false;  // a static method
		uint8_t visibility = 0;  // a method's: class_decl::member_visibility
		std::vector<generic_parameter> parameters;
		std::vector<token> tokens;   // the declaration, from its head to its closing brace
		uint32_t name_token = 0;     // tokens[name_token] is the name; tokens[name_token + 1 .. list_end) its parameter list, brackets included
		uint32_t list_end = 0;
		uint64_t stamp = 0;          // what an instance's definition is compared by: the tokens and where they stand
		std::vector<generic_value_parameter> values;   // a function's or a method's parameters
		source_location location;    // the name's

		// "Stack", "shop::Stack", "wire::decode", "Request::json"
		std::string qualified_name() const {
			std::string result = kind == generic_kind::method ? owner : space;
			if (!result.empty()) { result += "::"; }
			result += name;
			return result;
		}
		size_t required_values() const {
			size_t n = 0;
			for (const auto& v : values) { if (!v.has_default) { ++n; } }
			return n;
		}
	};
	using generic_definition_ptr = std::shared_ptr<const generic_definition>;

	// An instance a check found the unit to need (check_options::generic_requests): the engine adds its request to the unit's others
	struct generic_request {
		std::string name;      // the instance's qualified name
		std::string generic;   // the generic's
		std::vector<type_info_ptr> arguments;
		generic_definition_ptr definition;
		source_location at;
		std::string within;    // the instance whose text asked
	};

	// An instance the engine holds that a unit's declaration of its generic cannot make again (docs/site/guide/21-generics.html
	// "Declaring it again"): the instance keeps the declaration it has and the unit's check warns, naming it and the reason
	struct generic_kept_instance {
		std::string name;      // the instance's qualified name
		std::string generic;   // the generic's
		std::string reason;    // the error that keeps the declaration from making it
		source_location at;    // the declaration
	};

	// What a name that takes no type arguments is: Name<Args> on it fails the load once, wherever a type can stand
	enum class plain_type_kind : uint8_t { none, script_class, host_class, enumeration, builtin };
	// int, float, string, bool, char, var, utf8_string, double and the math types (math::vec2)
	bool builtin_type_name(std::string_view name);
	// "'Hero' takes no type arguments: it is not a generic class", and for a host class "it is a host class, not a host
	// template", for an enum "it is an enum", for a builtin "it is a builtin type"
	std::string no_type_arguments_text(std::string_view name, plain_type_kind kind);

	// Two declarations of one generic: the same kind, as many type parameters, and value parameters of the same types (a type
	// parameter matching the one at its place: largest<T>(array<T>) and largest<U>(array<U>) are one declaration)
	bool same_generic_signature(const generic_definition& a, const generic_definition& b);

	// The stamp of a definition's tokens: kinds, spellings, literal values, lines and columns
	uint64_t generic_stamp(const std::vector<token>& tokens);

	// A type as the tokens that spell it where a type parameter stood (each at `at`): `shop::Stack<int>` is `shop` `::` and the one
	// name `Stack<int>`, the class an instance declares. False for a type no argument may be (auto, void, a reference).
	bool generic_argument_tokens(const type_info& type, const source_location& at, string_symbolizer& symbols, std::vector<token>& out);

	// The instance's name: template_instance_name over the generic's qualified name and each argument's canonical spelling
	std::string generic_instance_name(std::string_view generic, const std::vector<type_info_ptr>& arguments);

	// An instance's text: the definition's tokens with each parameter replaced by its argument and the declared name by
	// `instance` (the name without its namespace), wrapped in the definition's namespace or, for a method, its class; eof ends it
	std::vector<token> generic_instance_tokens(const generic_definition& definition, const std::vector<type_info_ptr>& arguments,
	                                           std::string_view instance, string_symbolizer& symbols);

	// What a generic's declaration runs (generic_decl::marker): made once, null for a method's generic
	std::shared_ptr<class_decl> generic_marker(const generic_decl& declared, string_symbolizer& symbols);

	// `T : Base` with the arguments in place: the bound's tokens (empty when the parameter has none)
	std::vector<token> generic_bound_tokens(const generic_definition& definition, size_t parameter, const std::vector<type_info_ptr>& arguments,
	                                        string_symbolizer& symbols);

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_GENERICS_HPP__
