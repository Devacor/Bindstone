#pragma once

#include <optional>
#include <string>
#include <utility>

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/numeric_conversion.hpp>
#include <jaiscript/detail/arity_diagnostic.hpp>

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
			case script_value_type::jai_utf8_string_type: return "utf8_string";
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

	// engine.cpp: the class rows of the element boundary judge the instance behind the value's
	// tag (a var-held Animal, a shared_ptr<Dog> handle) with the scalar table's own admission
	bool object_element_admits(const script_value& v, const type_info& element_type, engine* eng);
	// The class a tag names for the subclass walk: a handle tag's pointee, else the tag itself
	inline uint64_t class_tag_id(const type_info& t) {
		const bool handle = t.base_type == script_value_type::jai_shared_ptr_type || t.base_type == script_value_type::jai_weak_ptr_type;
		return handle && t.element_type() ? t.element_type()->id : t.id;
	}
	// engine.cpp: the instance's own class tag, for a copy stored under a var tag (null = none)
	type_info_ptr object_element_own_tag(const script_value& v, engine* eng);

	// The source spelling of every element and key text: the payload's type, never a var tag
	inline std::string container_value_type_name(const script_value& val) {
		const script_value& v = val.is_reference() ? val.deref() : val;
		auto info = v.get_type_info();
		if (info && !info->type_name.empty() && info->base_type != script_value_type::jai_any_type) {
			return info->type_name;
		}
		if (auto holder = v.get_object_holder(); holder && !holder->type_name.empty()) {
			return std::string(holder->type_name);
		}
		return container_base_type_name(v.current_type());
	}

	// The target spelling of every element, key and slot text: the declared type in full
	inline std::string type_info_display_name(const type_info_ptr& info) {
		return info ? info->canonical_name() : "unknown";
	}

	inline bool container_element_compatible(const script_value& element, const type_info_ptr& element_type, engine* eng = nullptr) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;

		if (element_type && element_type->base_type == script_value_type::jai_any_type) {
			return true;
		}
		if (!element_type) {
			return true;
		}

		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;

		// Dev ruling 2026-09-08: a handle-typed element takes null (C++: a vector of handles
		// holds empty handles); by-value elements stay non-nullable like std::vector<T>
		if (actual_element.raw_storage_index() == script_value::TYPEID_NULL) {
			return target_type == script_value_type::jai_shared_ptr_type || target_type == script_value_type::jai_weak_ptr_type;
		}

		if (elem_type == target_type) {
			if (target_type == script_value_type::jai_object_type) {
				auto elem_type_info = actual_element.get_type_info();
				if (elem_type_info && !element_type->type_name.empty()) {
					// exact class by interned id (one compare), else the instance's class (a var
					// tag says nothing about it) and the subclass walk
					return elem_type_info->id == element_type->id ||
					       elem_type_info->type_name == element_type->type_name ||
					       object_element_admits(actual_element, *element_type, eng);
				}
			}
			if (target_type == script_value_type::jai_shared_ptr_type && !element_type->type_name.empty()) {
				// a handle into a shared_ptr<T> element: the pointee's class, like a shared_ptr<T> field
				return object_element_admits(actual_element, *element_type, eng);
			}
			return true;
		}
		// Dev ruling 2026-09-08: a live instance stores into a shared_ptr<T> element the way it
		// stores into a shared_ptr<T> variable or field (the slot owns the handle)
		if (target_type == script_value_type::jai_shared_ptr_type && elem_type == script_value_type::jai_object_type) {
			return element_type->type_name.empty() || object_element_admits(actual_element, *element_type, eng);
		}

		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return true;
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return true;
		}
		// Dev ruling 2026-09-09: bool and int convert both ways at the element boundary, like the
		// scalar slots (a string element still takes only a string: to_string / cast<string> spell it)
		if ((target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_bool_type) ||
		    (target_type == script_value_type::jai_bool_type && elem_type == script_value_type::jai_int_type)) {
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
			return elem_type_info && (elem_type_info->type_name == element_type->type_name ||
			                          object_element_admits(actual_element, *element_type, eng));
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

	inline constexpr const char* container_boundary_mismatch_text = "Cannot store {0} in {1}";
	inline constexpr const char* uint8_range_text = "Cannot store {0} in array<uint8> (0..255)";
	inline constexpr const char* int8_range_text = "Cannot store {0} in array<int8> (-128..127)";
	// a parallel worker cannot intern the value: the same text without it
	inline constexpr const char* uint8_range_text_slotless = "Cannot store a value outside 0..255 in array<uint8>";
	inline constexpr const char* int8_range_text_slotless = "Cannot store a value outside -128..127 in array<int8>";

	inline bool byte_element_fits(bool is_signed, script_int v) noexcept {
		return is_signed ? (v >= -128 && v <= 127) : (v >= 0 && v <= 255);
	}

	// The byte range kernel's verdict (array<uint8> / array<int8> elements), the one text every store path raises
	inline error_propagator byte_range_error(engine* eng, bool is_signed, script_int v) {
		auto* sym = eng ? eng->get_symbolizer() : nullptr;
		const auto code = make_error_code(runtime_error_code::array_element_type_mismatch);
		if (!sym || sym->frozen()) {
			return error_propagator{ code, is_signed ? int8_range_text_slotless : uint8_range_text_slotless, 0, 0 };
		}
		return error_propagator{ code, is_signed ? int8_range_text : uint8_range_text, sym->intern(std::to_string(v)), 0 };
	}

	struct container_boundary_outcome {
		std::optional<script_value> value;      // engaged on success
		std::string offending;                  // failure: the mismatched element/key/value type
		std::string expected;                   // failure: the target container's canonical name
		std::optional<error_propagator> error;  // failure already worded by a kernel: a float outside the int range, a nested boundary's verdict
	};

	inline container_boundary_outcome enforce_container_boundary(script_value value, const type_info_ptr& target_type, engine* eng);

	// The one text for an element that does not belong, interned the way the backends do
	inline error_propagator container_mismatch_error(engine* eng, const std::string& offending, const std::string& expected) {
		auto* sym = eng ? eng->get_symbolizer() : nullptr;
		return error_propagator{ make_error_code(runtime_error_code::array_element_type_mismatch), container_boundary_mismatch_text,
		                         sym ? sym->intern(offending) : 0, sym ? sym->intern(expected) : 0 };
	}

	// The element boundary's numeric rows are the scalar slot's rows: a float into an int
	// element runs the same checked kernel (`int x = 1e100` and `array<int> x = [1e100]`
	// reject with one text), an int into a float element widens.
	inline checked_result<script_value> convert_container_element(engine* eng, const script_value& element, const type_info_ptr& element_type) {
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

		if (target_type == script_value_type::jai_array_type || target_type == script_value_type::jai_map_type) {
			// a nested container element: the same boundary one level down, reading the
			// source in place. A rebuild (inner literal -> typed node) IS the store's copy;
			// a node that already fits comes back as the same handle and gets the one copy
			// value semantics owe the store, exactly like a flat element.
			if (elem_type != target_type) {
				return container_mismatch_error(eng, container_value_type_name(actual_element), element_type->canonical_name());
			}
			auto inner = enforce_container_boundary(actual_element, element_type, eng);
			if (inner.error) { return *inner.error; }
			if (!inner.value) { return container_mismatch_error(eng, inner.offending, inner.expected); }
			const bool rebuilt = inner.value->raw_storage_index() != actual_element.raw_storage_index() ||
			                     (target_type == script_value_type::jai_array_type
			                          ? inner.value->unchecked_array_node() != actual_element.unchecked_array_node()
			                          : &inner.value->unchecked_as_map() != &actual_element.unchecked_as_map());
			return rebuilt ? std::move(*inner.value) : inner.value->clone_element();
		}

		if (target_type == script_value_type::jai_int_type && element_type->is_byte_element()) {
			// the int rows, then the kind's range
			script_int v = 0;
			if (elem_type == script_value_type::jai_int_type) {
				v = actual_element.unchecked_as_int();
			} else if (elem_type == script_value_type::jai_float_type) {
				auto converted = checked_float_to_integer(actual_element.unchecked_as_float());
				if (!converted) { return converted.error_value(); }
				v = converted.value();
			} else if (elem_type == script_value_type::jai_bool_type) {
				v = actual_element.unchecked_as_bool() ? 1 : 0;
			} else {
				return container_mismatch_error(eng, container_value_type_name(actual_element), "array<" + element_type->canonical_name() + ">");
			}
			if (!byte_element_fits(element_type->is_signed, v)) { return byte_range_error(eng, element_type->is_signed, v); }
			return script_value(v, eng);
		}
		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			auto converted = checked_float_to_integer(actual_element.unchecked_as_float());
			if (!converted) { return converted.error_value(); }
			return script_value(converted.value(), eng);
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return script_value(static_cast<script_float>(actual_element.unchecked_as_int()), eng);
		}
		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_bool_type) {
			return script_value(static_cast<script_int>(actual_element.unchecked_as_bool() ? 1 : 0), eng);
		}
		if (target_type == script_value_type::jai_bool_type && elem_type == script_value_type::jai_int_type) {
			return script_value(actual_element.unchecked_as_int() != 0, eng);
		}
		if (target_type == script_value_type::jai_shared_ptr_type && elem_type == script_value_type::jai_object_type) {
			return actual_element;   // the handle rows' store: the object travels as the handle, its own tag
		}
		if (target_type == script_value_type::jai_object_type && elem_type == script_value_type::jai_object_type) {
			// the stored copy carries its class, not the var tag it arrived under
			script_value out = actual_element.clone_element();
			const type_info* tag = out.get_type_info().get();
			if (!tag || tag->base_type == script_value_type::jai_any_type) {
				if (auto own = object_element_own_tag(actual_element, eng)) { out.set_type_info(std::move(own)); }
			}
			return out;
		}

		return actual_element.clone_element();
	}

	// engine.cpp: subclass admission walk (Dev ruling 2026-09-02)
	bool class_derives_from(engine* eng, uint64_t derived_id, uint64_t base_id);

	inline bool byte_kind(script_array::kind_t k) noexcept {
		return k == script_array::kind_t::u8 || k == script_array::kind_t::i8;
	}

	// Kind <-> primitive element type (the representation rule's one table)
	inline script_array::kind_t kind_for_element(const type_info& element) {
		switch (element.base_type) {
			case script_value_type::jai_int_type:
				return !element.is_byte_element() ? script_array::kind_t::i64 : element.is_signed ? script_array::kind_t::i8 : script_array::kind_t::u8;
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
			case script_array::kind_t::i64:
			case script_array::kind_t::u8:
			case script_array::kind_t::i8: return script_value_type::jai_int_type;
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
	inline bool same_object_class(const type_info_ptr& array_tag, const type_info_ptr& element_type, engine* eng = nullptr) {
		type_info_ptr src_elem = array_tag ? array_tag->element_type() : nullptr;
		return src_elem && element_type && src_elem->base_type == script_value_type::jai_object_type &&
		       (src_elem->type_name == element_type->type_name ||
		        class_derives_from(eng, class_tag_id(*src_elem), class_tag_id(*element_type)));
	}
	inline type_info* typed_kind_element_type(script_array::kind_t kind, engine* eng) {
		switch (kind) {
			case script_array::kind_t::i64: return eng->get_type_info_int();
			case script_array::kind_t::f64: return eng->get_type_info_float();
			case script_array::kind_t::b8: return eng->get_type_info_bool();
			case script_array::kind_t::c8: return eng->get_type_info_char();
			case script_array::kind_t::u8: return eng->get_type_info_uint8();
			case script_array::kind_t::i8: return eng->get_type_info_int8();
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

	// A byte buffer on either side of an int, float or byte buffer: the element rows (int into a byte range-checked,
	// float through the checked float kernel first, a byte widening into int or float) on the raw payloads; any other
	// pair is the boxed loop's
	inline std::optional<container_boundary_outcome> convert_byte_buffer(const script_array& src, script_array::kind_t expected,
	                                                                    const type_info_ptr& element_type, const type_info_ptr& target_type, engine* eng) {
		using k = script_array::kind_t;
		const k sk = src.kind();
		const bool numeric_src = sk == k::i64 || sk == k::f64 || byte_kind(sk);
		const bool numeric_dst = expected == k::i64 || expected == k::f64 || byte_kind(expected);
		if (!numeric_src || !numeric_dst || sk == expected) { return std::nullopt; }
		const size_t count = src.size();
		container_boundary_outcome out;
		script_value rebuilt = script_value::make_array(element_type, eng);
		script_array& dest = *rebuilt.get_array_storage();
		auto byte_at = [&](size_t i) -> script_int {
			return sk == k::u8 ? static_cast<script_int>(src.bytes()[i]) : static_cast<script_int>(static_cast<int8_t>(src.bytes()[i]));
		};
		if (byte_kind(expected)) {
			const bool is_signed = expected == k::i8;
			auto& bytes = dest.bytes();
			bytes.resize(count);
			for (size_t i = 0; i < count; ++i) {
				script_int v = 0;
				if (sk == k::i64) {
					v = src.ints()[i];
				} else if (sk == k::f64) {
					auto converted = checked_float_to_integer(src.floats()[i]);
					if (!converted) { out.error = converted.error_value(); return out; }
					v = converted.value();
				} else {
					v = byte_at(i);
				}
				if (!byte_element_fits(is_signed, v)) { out.error = byte_range_error(eng, is_signed, v); return out; }
				bytes[i] = static_cast<uint8_t>(v);
			}
		} else if (expected == k::i64) {
			auto& ints = dest.ints();
			ints.resize(count);
			for (size_t i = 0; i < count; ++i) { ints[i] = byte_at(i); }
		} else {
			auto& floats = dest.floats();
			floats.resize(count);
			for (size_t i = 0; i < count; ++i) { floats[i] = static_cast<script_float>(byte_at(i)); }
		}
		rebuilt.set_type_info(target_type);
		out.value = std::move(rebuilt);
		return out;
	}

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
			const script_array* src = value.unchecked_array_node();
			if (!element_type || element_type->base_type == script_value_type::jai_any_type) {
				// open element type: allow-all. array<var> IS the script_value-vector form
				// (the representation rule), so a typed source reboxes into a fresh node
				// under the declared tag - its own buffer is untouched; a hetero source
				// passes through and keeps its own (possibly tighter) tag
				if (!element_type || !src->is_typed()) {
					out.value = std::move(value);
					return out;
				}
				const size_t count = src->size();
				script_value rebuilt = script_value::make_array(element_type, eng);
				auto& dest = *rebuilt.get_array_storage();
				dest.reserve(count);
				for (size_t i = 0; i < count; ++i) {
					dest.push(src->get(i, eng));
				}
				rebuilt.set_type_info(target_type);
				out.value = std::move(rebuilt);
				return out;
			}
			const auto target_elem_base = element_type->base_type;

			// The node kind this binding wants: raw buffers for the primitive element
			// types (the boundary kernel is THE typed-node producer)
			const script_array::kind_t expected_kind = kind_for_element(*element_type);

			// Proof by kind: a same-kind typed node cannot hold a mismatched element
			// (object buffers also need the same class: the kind alone says "objects")
			if (src->is_typed() && src->kind() == expected_kind &&
			    (expected_kind != script_array::kind_t::obj || same_object_class(value.get_type_info(), element_type, eng))) {
				value.set_type_info(target_type);
				out.value = std::move(value);
				return out;
			}

			if (byte_kind(expected_kind) || byte_kind(src->kind())) {
				if (auto direct = convert_byte_buffer(*src, expected_kind, element_type, target_type, eng)) { return std::move(*direct); }
			}

			bool needs_conversion = src->is_typed() || src->kind() != expected_kind;
			const size_t count = src->size();
			if (src->is_typed() && src->kind() != script_array::kind_t::obj) {
				// typed source elements are one primitive by construction: int/float cross-
				// convert into numeric targets, everything else is a mismatch
				const script_value_type src_elem = typed_kind_element_base(src->kind());
				const bool numeric_pair = (src_elem == script_value_type::jai_int_type || src_elem == script_value_type::jai_float_type) &&
				                          (target_elem_base == script_value_type::jai_int_type || target_elem_base == script_value_type::jai_float_type);
				const bool bool_int_pair = (src_elem == script_value_type::jai_bool_type && target_elem_base == script_value_type::jai_int_type) ||
				                           (src_elem == script_value_type::jai_int_type && target_elem_base == script_value_type::jai_bool_type);
				if (!numeric_pair && !bool_int_pair && src_elem != target_elem_base) {
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
					if (!container_element_compatible(actual, element_type, eng)) {
						out.offending = container_value_type_name(actual);
						out.expected = target_type->canonical_name();
						return out;
					}
					if (actual.type() != target_elem_base &&
					    (target_elem_base == script_value_type::jai_int_type ||
					     target_elem_base == script_value_type::jai_float_type ||
					     target_elem_base == script_value_type::jai_bool_type)) {
						needs_conversion = true;
					}
					if (target_elem_base == script_value_type::jai_array_type || target_elem_base == script_value_type::jai_map_type) {
						// nested: the inner tag decides; an untagged or differently tagged inner
						// node is rebuilt through its own boundary
						if (actual.get_type_info().get() != element_type.get()) { needs_conversion = true; }
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
				if (expected_kind == script_array::kind_t::obj) {
					dest.push(src->get(i, eng));
					continue;
				}
				auto converted = convert_container_element(eng, src->get(i, eng), element_type);
				if (!converted) {
					out.error = converted.error_value();
					return out;
				}
				dest.push(std::move(converted.value()));
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
					if (!container_element_compatible(actual, value_type, eng)) {
						out.offending = container_value_type_name(actual);
						out.expected = target_type->canonical_name();
						return out;
					}
					const auto value_base = value_type->base_type;
					if (actual.type() != value_base &&
					    (value_base == script_value_type::jai_int_type ||
					     value_base == script_value_type::jai_float_type ||
					     value_base == script_value_type::jai_bool_type)) {
						needs_conversion = true;
					}
					if ((value_base == script_value_type::jai_array_type || value_base == script_value_type::jai_map_type) &&
					    actual.get_type_info().get() != value_type.get()) {
						needs_conversion = true;   // nested value: rebuilt through its own boundary below
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
				auto converted = convert_container_element(eng, entry.second, value_type);
				if (!converted) {
					out.error = converted.error_value();
					return out;
				}
				dest->emplace(actual_key.clone(), std::move(converted.value()));
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

	template <class Symbolizer>
	checked_result<void> validate_container_homogeneous(const script_value& container, const arity_count_symbols& counts, const Symbolizer& symbols);

	// The entries of an auto container literal hold one kind (and one class) at every depth; a raw buffer is homogeneous
	// by construction
	template <bool Map, class Entries, class Symbolizer>
	checked_result<void> homogeneous_entries(const Entries& entries, const arity_count_symbols& counts, const Symbolizer& symbols) {
		const runtime_error_code code = Map ? runtime_error_code::map_value_type_mismatch : runtime_error_code::array_element_type_mismatch;
		script_value_type kind = script_value_type::jai_null_type;
		std::string class_name;
		size_t index = 0;
		for (const auto& entry : entries) {
			const script_value* raw;
			if constexpr (Map) { raw = &entry.second; } else { raw = &entry; }
			const script_value& value = raw->is_reference() ? raw->deref() : *raw;
			if (index == 0) {
				kind = value.type();
				if (kind == script_value_type::jai_object_type) {
					if (auto info = value.get_type_info()) { class_name = info->type_name; }
				}
			} else {
				if (value.type() != kind) {
					return homogeneity_error(code, Map ? auto_map_values : auto_array_elements, Map ? auto_map_values_at : auto_array_elements_at, index, counts, symbols);
				}
				if (kind == script_value_type::jai_object_type && !class_name.empty()) {
					auto info = value.get_type_info();
					if ((info ? std::string(info->type_name) : std::string()) != class_name) {
						return homogeneity_error(code, Map ? auto_map_classes : auto_array_classes, Map ? auto_map_classes_at : auto_array_classes_at, index, counts, symbols);
					}
				}
			}
			if (auto nested = validate_container_homogeneous(value, counts, symbols); !nested) { return nested; }
			++index;
		}
		return {};
	}

	template <class Symbolizer>
	checked_result<void> validate_container_homogeneous(const script_value& container, const arity_count_symbols& counts, const Symbolizer& symbols) {
		if (container.is_array()) {
			if (container.unchecked_array_node()->is_typed()) { return {}; }
			return homogeneous_entries<false>(container.as_array(), counts, symbols);
		}
		if (container.is_map()) {
			return homogeneous_entries<true>(container.as_map(), counts, symbols);
		}
		return {};
	}

} // namespace jai::detail
