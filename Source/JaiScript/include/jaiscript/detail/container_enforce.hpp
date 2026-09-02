#pragma once

#include <optional>
#include <string>
#include <utility>

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/engine.hpp>

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
			return actual_element.clone_element();
		}

		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;

		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return script_value(static_cast<script_int>(actual_element.unchecked_as_float()), eng);
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return script_value(static_cast<script_float>(actual_element.unchecked_as_int()), eng);
		}

		return actual_element.clone_element();
	}

	// Kind <-> primitive element type (the representation rule's one table)
	inline script_array::kind_t kind_for_element_base(script_value_type base) {
		switch (base) {
			case script_value_type::jai_int_type: return script_array::kind_t::i64;
			case script_value_type::jai_float_type: return script_array::kind_t::f64;
			case script_value_type::jai_bool_type: return script_array::kind_t::b8;
			case script_value_type::jai_char_type: return script_array::kind_t::c8;
			case script_value_type::jai_string_type: return script_array::kind_t::str;
			case script_value_type::jai_object_type: return script_array::kind_t::obj;
			default: return script_array::kind_t::hetero;
		}
	}
	inline script_value_type typed_kind_element_base(script_array::kind_t kind) {
		switch (kind) {
			case script_array::kind_t::i64: return script_value_type::jai_int_type;
			case script_array::kind_t::f64: return script_value_type::jai_float_type;
			case script_array::kind_t::b8: return script_value_type::jai_bool_type;
			case script_array::kind_t::c8: return script_value_type::jai_char_type;
			case script_array::kind_t::str: return script_value_type::jai_string_type;
			case script_array::kind_t::obj: return script_value_type::jai_object_type;
			default: return script_value_type::jai_any_type;
		}
	}
	// An object buffer is homogeneous by EXACT class (the element rule today); the class
	// lives in the array value's tag, not in the kind
	inline bool same_object_class(const type_info_ptr& array_tag, const type_info_ptr& element_type) {
		type_info_ptr src_elem = array_tag ? array_tag->element_type() : nullptr;
		return src_elem && element_type && src_elem->base_type == script_value_type::jai_object_type &&
		       src_elem->type_name == element_type->type_name;
	}
	inline type_info* typed_kind_element_type(script_array::kind_t kind, engine* eng) {
		switch (kind) {
			case script_array::kind_t::i64: return eng->get_type_info_int();
			case script_array::kind_t::f64: return eng->get_type_info_float();
			case script_array::kind_t::b8: return eng->get_type_info_bool();
			case script_array::kind_t::c8: return eng->get_type_info_char();
			default: return eng->get_type_info_string();
		}
	}

	// Uniform-literal inference (Dev ruling 2026-09-02): `auto a = [1, 2, 3]` and
	// `array<auto> a = [...]` infer their element type from a homogeneous primitive
	// literal and take that type's representation (array<int>/array<float> = raw buffer,
	// array<string>/bool/char = the locked tag). Empty, nested, object, or bound-primitive
	// literals answer null and stay dynamic. Callers have already validated homogeneity.
	inline type_info_ptr infer_uniform_array_type(const script_value& array_value, engine* eng) {
		if (!eng || array_value.raw_storage_index() != script_value::TYPEID_ARRAY) {
			return nullptr;
		}
		const script_array* node = array_value.unchecked_array_node();
		if (node->is_typed()) {
			if (node->kind() == script_array::kind_t::obj) { return array_value.get_type_info(); }   // the tag names the class
			return type_info_ptr{eng->get_type_info_array(typed_kind_element_type(node->kind(), eng))};
		}
		const auto& elems = node->values();
		if (elems.empty()) {
			return nullptr;
		}
		const size_t first = elems[0].deref().raw_storage_index();
		switch (first) {
			case script_value::TYPEID_INT:
			case script_value::TYPEID_FLOAT:
			case script_value::TYPEID_STRING:
			case script_value::TYPEID_BOOL:
			case script_value::TYPEID_CHAR:
				break;
			default:
				return nullptr;
		}
		for (const script_value& e : elems) {
			if (e.deref().raw_storage_index() != first) {
				return nullptr;
			}
		}
		type_info* element = nullptr;
		switch (first) {
			case script_value::TYPEID_INT: element = eng->get_type_info_int(); break;
			case script_value::TYPEID_FLOAT: element = eng->get_type_info_float(); break;
			case script_value::TYPEID_STRING: element = eng->get_type_info_string(); break;
			case script_value::TYPEID_BOOL: element = eng->get_type_info_bool(); break;
			default: element = eng->get_type_info_char(); break;
		}
		return type_info_ptr{eng->get_type_info_array(element)};
	}

	// Map twin of the array rule (Dev ruling 2026-09-02): an `auto` key or value slot
	// (the parser leaves it null: plain `auto m`, `map<auto,V>`, `map<K,auto>`) infers its
	// type from a uniform primitive literal; an empty literal stays dynamic; mixed kinds
	// answer null with mixed_part set (1 = keys, 2 = values) so the caller raises. Spelled
	// `var` slots (base any) are never inferred. Callers already validated value
	// homogeneity for auto-value declarations.
	inline type_info_ptr infer_uniform_map_type(const script_value& map_value, type_info_ptr declared, engine* eng, int& mixed_part) {
		mixed_part = 0;
		if (!eng || map_value.raw_storage_index() != script_value::TYPEID_MAP) {
			return nullptr;
		}
		if (declared && !declared->is_map()) {
			return nullptr;   // `var m = {...}` (any) is not an auto slot: fully dynamic
		}
		const bool key_auto = !declared || !declared->key_type();
		const bool value_auto = !declared || !declared->value_type();
		if (!key_auto && !value_auto) {
			return nullptr;
		}
		const auto& entries = map_value.unchecked_as_map();
		if (entries.empty()) {
			return nullptr;
		}
		auto primitive_of = [&](const script_value& v, size_t& kind) -> bool {
			kind = v.deref().raw_storage_index();
			return kind == script_value::TYPEID_INT || kind == script_value::TYPEID_FLOAT || kind == script_value::TYPEID_STRING ||
			       kind == script_value::TYPEID_BOOL || kind == script_value::TYPEID_CHAR;
		};
		auto type_of_kind = [&](size_t kind) -> type_info* {
			switch (kind) {
				case script_value::TYPEID_INT: return eng->get_type_info_int();
				case script_value::TYPEID_FLOAT: return eng->get_type_info_float();
				case script_value::TYPEID_STRING: return eng->get_type_info_string();
				case script_value::TYPEID_BOOL: return eng->get_type_info_bool();
				default: return eng->get_type_info_char();
			}
		};
		type_info* key_type = key_auto ? nullptr : declared->key_type().get();
		type_info* value_type = value_auto ? nullptr : declared->value_type().get();
		if (key_auto) {
			size_t first = 0;
			if (primitive_of(entries.begin()->first, first)) {
				bool uniform = true;
				for (const auto& entry : entries) {
					size_t k = 0;
					if (!primitive_of(entry.first, k) || k != first) { uniform = false; break; }
				}
				if (!uniform) { mixed_part = 1; return nullptr; }
				key_type = type_of_kind(first);
			}
		}
		if (value_auto) {
			size_t first = 0;
			if (primitive_of(entries.begin()->second, first)) {
				bool uniform = true;
				for (const auto& entry : entries) {
					size_t k = 0;
					if (!primitive_of(entry.second, k) || k != first) { uniform = false; break; }
				}
				if (!uniform) { mixed_part = 2; return nullptr; }
				value_type = type_of_kind(first);
			}
		}
		if (!key_type && !value_type) {
			return nullptr;   // nothing primitive to infer: stays as declared (dynamic)
		}
		return type_info_ptr{eng->get_type_info_map(key_type ? key_type : eng->get_type_info_any(),
		                                            value_type ? value_type : eng->get_type_info_any())};
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
			const script_array* src = value.unchecked_array_node();

			// The node kind this binding wants: raw buffers for the primitive element
			// types (the boundary kernel is THE typed-node producer)
			const script_array::kind_t expected_kind = kind_for_element_base(target_elem_base);

			// Proof by kind: a same-kind typed node cannot hold a mismatched element
			// (object buffers also need the same class: the kind alone says "objects")
			if (src->is_typed() && src->kind() == expected_kind &&
			    (expected_kind != script_array::kind_t::obj || same_object_class(value.get_type_info(), element_type))) {
				value.set_type_info(target_type);
				out.value = std::move(value);
				return out;
			}

			bool needs_conversion = src->is_typed() || src->kind() != expected_kind;
			const size_t count = src->size();
			if (src->is_typed() && src->kind() != script_array::kind_t::obj) {
				// typed source elements are one primitive by construction: int/float cross-
				// convert into numeric targets, everything else is a mismatch
				const script_value_type src_elem = typed_kind_element_base(src->kind());
				const bool numeric_pair = (src_elem == script_value_type::jai_int_type || src_elem == script_value_type::jai_float_type) &&
				                          (target_elem_base == script_value_type::jai_int_type || target_elem_base == script_value_type::jai_float_type);
				if (!numeric_pair && src_elem != target_elem_base) {
					out.offending = container_base_type_name(src_elem);
					out.expected = target_type->canonical_name();
					return out;
				}
			} else {
				// hetero elements by reference, object slots boxed: the same per-element check
				for (size_t i = 0; i < count; ++i) {
					script_value boxed(std::monostate{}, eng);
					const script_value* elem = nullptr;
					if (src->is_typed()) { boxed = src->get(i, eng); elem = &boxed; } else { elem = &src->values()[i]; }
					const script_value& element = *elem;
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
			}
			if (!needs_conversion) {
				value.set_type_info(target_type);
				out.value = std::move(value);
				return out;
			}
			script_value rebuilt = script_value::make_array(element_type, eng);
			auto& dest = *rebuilt.get_array_storage();
			dest.reserve(count);
			for (size_t i = 0; i < count; ++i) {
				// representation change only: object handles copy as-is (ownership - clone or
				// share - is decided by the assignment or binding site, exactly as before)
				dest.push(expected_kind == script_array::kind_t::obj ? src->get(i, eng)
				                                                       : convert_container_element(eng, src->get(i, eng), element_type));
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
