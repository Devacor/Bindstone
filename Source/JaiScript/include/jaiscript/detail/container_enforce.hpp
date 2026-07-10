#pragma once

#include <optional>
#include <string>
#include <utility>

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/type_info.hpp>

// Typed-container enforcement kernel, shared VERBATIM by both backends and the field
// kernel (parity by construction — stage 0 of docs/typed_array_design.md). Two layers:
//  - the element predicate/converter pair push and subscript stores use (consolidated
//    from the interpreter/vm twins),
//  - the whole-value BOUNDARY check: an array/map value crossing into a concretely
//    typed container binding (decl, assign, field write) validates every element with
//    push's rules, coerces int<->float payloads, and retags the result to the declared
//    type so later pushes stay enforced. Pre-fix, the same-base fast path admitted ANY
//    array behind an array<T> tag — the hole that silently broke the parallel
//    all-primitive proof.
// Scope: concrete PRIMITIVE element targets get full validate+convert semantics;
// nested container/object element targets keep push's one-level tag-based compat.

namespace jai::detail {

	inline std::string container_base_type_name(script_value_type type) {
		switch (type) {
			case script_value_type::jai_null_type: return "null";
			case script_value_type::jai_int_type: return "int";
			case script_value_type::jai_float_type: return "float";
			case script_value_type::jai_string_type: return "string";
			case script_value_type::jai_char_type: return "char";
			case script_value_type::jai_bool_type: return "bool";
			case script_value_type::jai_array_type: return "array";
			case script_value_type::jai_map_type: return "map";
			case script_value_type::jai_object_type: return "object";
			case script_value_type::jai_function_type: return "function";
			case script_value_type::jai_reference_type: return "reference";
			case script_value_type::jai_shared_ptr_type: return "shared_ptr";
			case script_value_type::jai_weak_ptr_type: return "weak_ptr";
			case script_value_type::jai_any_type: return "any";
			case script_value_type::jai_invalid_type: return "invalid";
			default: return "unknown";
		}
	}

	inline std::string container_value_type_name(const script_value& val) {
		auto info = val.get_type_info();
		if (info && !info->type_name.empty()) {
			return info->type_name;
		}
		return container_base_type_name(val.type());
	}

	inline bool container_element_compatible(const script_value& element, const type_info_ptr& element_type) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;

		if (element_type && element_type->base_type == script_value_type::jai_any_type) {
			return true;
		}
		if (!element_type) {
			return true;
		}

		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;

		if (elem_type == target_type) {
			if (target_type == script_value_type::jai_object_type) {
				auto elem_type_info = actual_element.get_type_info();
				if (elem_type_info && !element_type->type_name.empty()) {
					return elem_type_info->type_name == element_type->type_name;
				}
			}
			return true;
		}

		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return true;
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return true;
		}

		// A shared_ptr-tagged instance stores into an object-typed element slot exactly
		// like field writes accept it (shared, not cloned — convert_container_element's
		// shared_ptr branch). Same exact-class-name rule the object==object branch uses.
		if (target_type == script_value_type::jai_object_type &&
		    elem_type == script_value_type::jai_shared_ptr_type) {
			if (element_type->type_name.empty()) {
				return true;
			}
			auto elem_type_info = actual_element.get_type_info();
			return elem_type_info && elem_type_info->type_name == element_type->type_name;
		}

		if (target_type == script_value_type::jai_array_type && elem_type == script_value_type::jai_array_type) {
			auto inner_target = element_type->element_type();
			auto elem_type_info = actual_element.get_type_info();
			auto inner_elem = elem_type_info ? elem_type_info->element_type() : nullptr;
			if (inner_target && inner_elem) {
				return inner_target->base_type == inner_elem->base_type;
			}
			if (inner_target && !inner_elem) {
				return false;
			}
			return true;
		}

		if (target_type == script_value_type::jai_map_type && elem_type == script_value_type::jai_map_type) {
			auto target_key = element_type->key_type();
			auto target_val = element_type->value_type();
			auto elem_type_info = actual_element.get_type_info();
			auto elem_key = elem_type_info ? elem_type_info->key_type() : nullptr;
			auto elem_val = elem_type_info ? elem_type_info->value_type() : nullptr;
			if (target_key && elem_key && target_key->base_type != elem_key->base_type) {
				return false;
			}
			if (target_val && elem_val && target_val->base_type != elem_val->base_type) {
				return false;
			}
			return true;
		}

		return false;
	}

	inline script_value convert_container_element(engine* eng, const script_value& element, const type_info_ptr& element_type) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;

		auto actual_type_info = actual_element.get_type_info();
		if (actual_type_info && actual_type_info->base_type == script_value_type::jai_shared_ptr_type) {
			return actual_element;
		}

		if (!element_type || element_type->base_type == script_value_type::jai_any_type) {
			return actual_element.clone();
		}

		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;

		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return script_value(static_cast<script_int>(actual_element.unchecked_as_float()), eng);
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return script_value(static_cast<script_float>(actual_element.unchecked_as_int()), eng);
		}

		return actual_element.clone();
	}

	// The one message both backends raise at a failed boundary (byte-identical errors).
	inline constexpr const char* container_boundary_mismatch_text = "Cannot store '{0}' in '{1}'";

	struct container_boundary_outcome {
		std::optional<script_value> value;  // engaged on success
		std::string offending;              // failure: the mismatched element/key/value type
		std::string expected;               // failure: the target container's canonical name
	};

	inline container_boundary_outcome enforce_container_boundary(script_value value, const type_info_ptr& target_type, engine* eng) {
		container_boundary_outcome out;
		if (!target_type) {
			out.value = std::move(value);
			return out;
		}

		const auto target_base = target_type->base_type;
		if (target_base == script_value_type::jai_array_type &&
		    value.raw_storage_index() == script_value::TYPEID_ARRAY) {
			type_info_ptr element_type = target_type->element_type();
			if (!element_type || element_type->base_type == script_value_type::jai_any_type) {
				// open element type: allow-all, and KEEP the value's own (possibly
				// tighter) tag — array<int> into array<var> stays enforced as int
				out.value = std::move(value);
				return out;
			}
			const auto target_elem_base = element_type->base_type;
			const auto& arr = value.unchecked_as_array();
			bool needs_conversion = false;
			for (const auto& element : arr) {
				const script_value& actual = element.is_reference() ? element.deref() : element;
				if (!container_element_compatible(actual, element_type)) {
					out.offending = container_value_type_name(actual);
					out.expected = target_type->canonical_name();
					return out;
				}
				if (actual.type() != target_elem_base &&
				    (target_elem_base == script_value_type::jai_int_type ||
				     target_elem_base == script_value_type::jai_float_type)) {
					needs_conversion = true;
				}
			}
			if (!needs_conversion) {
				value.set_type_info(target_type);
				out.value = std::move(value);
				return out;
			}
			script_value rebuilt = script_value::make_array(element_type, eng);
			auto& dest = rebuilt.get_array_storage()->values();
			dest.reserve(arr.size());
			for (const auto& element : arr) {
				dest.push_back(convert_container_element(eng, element, element_type));
			}
			rebuilt.set_type_info(target_type);
			out.value = std::move(rebuilt);
			return out;
		}

		if (target_base == script_value_type::jai_map_type &&
		    value.raw_storage_index() == script_value::TYPEID_MAP) {
			type_info_ptr key_type = target_type->key_type();
			type_info_ptr value_type = target_type->value_type();
			const bool key_open = !key_type || key_type->base_type == script_value_type::jai_any_type;
			const bool value_open = !value_type || value_type->base_type == script_value_type::jai_any_type;
			if (key_open && value_open) {
				out.value = std::move(value);
				return out;
			}
			const auto& entries = value.unchecked_as_map();
			bool needs_conversion = false;
			for (const auto& entry : entries) {
				if (!key_open) {
					// keys never coerce: converting 1.5 -> 1 could merge entries
					const script_value& actual_key = entry.first.is_reference() ? entry.first.deref() : entry.first;
					if (actual_key.type() != key_type->base_type) {
						out.offending = container_value_type_name(actual_key);
						out.expected = target_type->canonical_name();
						return out;
					}
				}
				if (!value_open) {
					const script_value& actual = entry.second.is_reference() ? entry.second.deref() : entry.second;
					if (!container_element_compatible(actual, value_type)) {
						out.offending = container_value_type_name(actual);
						out.expected = target_type->canonical_name();
						return out;
					}
					const auto value_base = value_type->base_type;
					if (actual.type() != value_base &&
					    (value_base == script_value_type::jai_int_type ||
					     value_base == script_value_type::jai_float_type)) {
						needs_conversion = true;
					}
				}
			}
			if (!needs_conversion) {
				value.set_type_info(target_type);
				out.value = std::move(value);
				return out;
			}
			script_value rebuilt = script_value::make_map(key_type, value_type, eng);
			auto& dest = rebuilt.get_map_storage();
			for (const auto& entry : entries) {
				const script_value& actual_key = entry.first.is_reference() ? entry.first.deref() : entry.first;
				dest->emplace(actual_key.clone(), convert_container_element(eng, entry.second, value_type));
			}
			rebuilt.set_type_info(target_type);
			out.value = std::move(rebuilt);
			return out;
		}

		// not the shapes this kernel guards (incl. reference/borrow-backed values):
		// unchanged, exactly today's behavior
		out.value = std::move(value);
		return out;
	}

} // namespace jai::detail
