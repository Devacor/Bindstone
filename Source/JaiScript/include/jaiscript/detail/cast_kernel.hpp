#pragma once

#ifndef __JAISCRIPT_DETAIL_CAST_KERNEL_HPP__
#define __JAISCRIPT_DETAIL_CAST_KERNEL_HPP__

// cast<T>(e) / cast<T&>(e) / cast<shared_ptr<T>>(e) semantics shared VERBATIM by the vm
// and the interpreter (parity by construction): operand-shape classification, the
// class-derivation verdict and its error text, the class-target value/handle/reference
// outcomes, and the char rules (the one primitive pair the assignment table refuses).
// Other primitive and container targets convert through the typed-slot table
// (detail::convert_for_typed_slot via enforce_type_compatibility) and only borrow the error text here.

#include <jaiscript/detail/ref_lvalue.hpp>
#include <jaiscript/detail/container_enforce.hpp>
#include <string>
#include <string_view>

namespace jai::detail {

	// How a cast evaluates its operand: value = ordinary evaluation (the reference form
	// aliases an evaluated reference or pins a temporary), identifier = bind the named
	// storage by reference (ref-decl identifier arm), lvalue_chain = member-final chain
	// resolved through resolve_ref_lvalue (ref-decl member-final arm). Subscript-final
	// operands stay on the value path: their reads already mint owner-pinned references.
	enum class cast_operand_mode : uint8_t { value = 0, identifier = 1, lvalue_chain = 2 };

	inline cast_operand_mode cast_operand_mode_of(const cast_expr* c) {
		if (!c->by_reference || !c->operand) {
			return cast_operand_mode::value;
		}
		if (c->operand->get_type() == node_type::identifier_expr) {
			return cast_operand_mode::identifier;
		}
		if (is_member_final_ref_lvalue(c->operand.get())) {
			return cast_operand_mode::lvalue_chain;
		}
		return cast_operand_mode::value;
	}

	// Copy-on-cast gate for the value form: identifier/member/subscript operands are
	// existing storage (a typed declaration clones those), everything else is a temporary.
	inline bool cast_lvalue_source(const expression* e) {
		if (!e) {
			return false;
		}
		const node_type t = e->get_type();
		if (t == node_type::identifier_expr || t == node_type::member_expr) {
			return true;
		}
		return t == node_type::binary_expr && static_cast<const binary_expr*>(e)->op.type == token_type::left_bracket;
	}

	inline constexpr std::string_view cast_error_text = "Cannot cast '{0}' to '{1}'";
	inline constexpr std::string_view cast_non_lvalue_text =
		"Cannot take reference of non-lvalue expression in cast<T&> (computed properties, "
		"C++-backed members and non-object receivers are not ref-bindable)";

	inline bool cast_target_is_class(const type_info& t) {
		return t.base_type == script_value_type::jai_object_type ||
		       t.base_type == script_value_type::jai_shared_ptr_type;
	}

	// The class a class target names: T itself, or shared_ptr<T>'s pointee
	inline const type_info* cast_target_class(const type_info& t) {
		if (t.base_type == script_value_type::jai_shared_ptr_type) {
			return t.element_type().get();
		}
		return &t;
	}

	inline std::string cast_target_spelling(const cast_expr& c) {
		std::string out;
		if (c.target->base_type == script_value_type::jai_shared_ptr_type) {
			const type_info* pointee = c.target->element_type().get();
			out = "shared_ptr<" + (pointee ? pointee->canonical_name() : std::string("auto")) + ">";
		} else {
			out = c.target->canonical_name();
		}
		if (c.by_reference) {
			out += "&";
		}
		return out;
	}

	// The operand's DYNAMIC class (the instance, never the value tag: a T stored in an
	// S-typed field carries S's tag and is still a T)
	inline const class_definition* cast_dynamic_class(const script_value& seen) {
		auto holder = seen.get_object_holder();
		if (!holder || !holder->is_class_instance_wrapper) {
			return nullptr;
		}
		auto* instance = static_cast<class_instance*>(holder->data.get());
		return instance ? instance->get_class_definition() : nullptr;
	}

	inline std::string cast_source_spelling(const script_value& seen) {
		if (seen.is_null()) {
			return "null";
		}
		if (const class_definition* def = cast_dynamic_class(seen)) {
			return def->get_name();
		}
		const type_info_ptr tag = seen.get_type_info();
		if (tag && (tag->base_type == script_value_type::jai_object_type ||
		            tag->base_type == script_value_type::jai_shared_ptr_type) && !tag->type_name.empty()) {
			return tag->type_name;
		}
		return container_base_type_name(seen.type());
	}

	template <typename T = script_value>
	inline checked_result<T> cast_error(const script_value& seen, const cast_expr& c, string_symbolizer* sym) {
		return checked_result<T>(make_error_code(runtime_error_code::type_mismatch), cast_error_text,
		                         sym->intern(cast_source_spelling(seen)), sym->intern(cast_target_spelling(c)));
	}

	// Class target on a non-null operand: an object or handle whose dynamic class is T or
	// derives from T (exact name first, registry parent walk otherwise)
	inline bool cast_class_admits(const script_value& seen, const type_info& target, engine* eng) {
		const type_info* cls = cast_target_class(target);
		if (!cls) {
			return false;
		}
		const class_definition* def = cast_dynamic_class(seen);
		if (!def) {
			return false;
		}
		return def->get_name() == cls->type_name || class_derives_from(eng, def->get_type_id(), cls->id);
	}

	// A T& parameter binds an lvalue that holds a T (Dev ruling 2026-09-28, C++): a primitive its exact kind, a class T or a
	// subclass (a typed null of T or a subclass too); var& and auto& bind any lvalue, container and handle types stay lenient
	inline bool reference_binding_admits(const type_info* declared, const script_value& current_in, engine* eng) {
		if (!declared) { return true; }
		const script_value* cur = &current_in;
		for (int depth = 0; depth < 8 && cur->is_reference(); ++depth) { cur = &cur->deref(); }   // a property's value can be a reference
		std::optional<script_value> decoded;
		if (cur->raw_storage_index() == script_value::TYPEID_CPP_BOUND) { decoded.emplace(cur->bound_decoded_temp()); cur = &*decoded; }
		const script_value& current = *cur;
		switch (declared->base_type) {
			case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type:
			case script_value_type::jai_char_type: case script_value_type::jai_string_type: case script_value_type::jai_utf8_string_type:
				return current.type() == declared->base_type;
			case script_value_type::jai_object_type: {
				if (declared->type_name.empty()) { return true; }
				if (current.is_null()) {
					const type_info* t = current.get_type_info().get();
					if (!t || t->base_type != script_value_type::jai_object_type || t->type_name.empty()) { return true; }
					return t->type_name == declared->type_name || class_derives_from(eng, t->id, declared->id);
				}
				if (!current.is_object()) { return false; }
				const class_definition* def = cast_dynamic_class(current);
				if (!def) { return true; }
				return def->get_name() == declared->type_name || class_derives_from(eng, def->get_type_id(), declared->id);
			}
			default:
				return true;
		}
	}

	// Value and shared_ptr forms with a CLASS target: null passes tagged; an admitted object is
	// tagged - the value form copies like a typed declaration (existing storage clones, a handle
	// deep-copies into a value, a temporary moves), the shared_ptr form shares the instance.
	inline checked_result<script_value> cast_class_value(script_value seen, const cast_expr& c, bool lvalue_source,
	                                                     engine* eng, string_symbolizer* sym) {
		if (seen.is_null()) {
			script_value out(std::monostate{}, eng);
			out.set_type_info(c.target);
			return out;
		}
		if (!cast_class_admits(seen, *c.target, eng)) {
			return cast_error(seen, c, sym);
		}
		if (c.target->base_type != script_value_type::jai_shared_ptr_type) {
			const type_info_ptr tag = seen.get_type_info();
			if (tag && tag->base_type == script_value_type::jai_shared_ptr_type) {
				script_value copy = seen.clone();
				seen = std::move(copy);
			} else if (lvalue_source) {
				script_value copy = clone_for_assignment(seen);
				seen = std::move(copy);
			}
		}
		seen.set_type_info(c.target);
		return seen;
	}

	// Char rules exist in the cast alone (`char c = 65` stays a type mismatch): a char casts
	// to int/float as its promoted code point (0..255) and to string as one text unit; an
	// int casts to char when it lies in 0..255, a string when it is exactly one character.
	inline bool cast_char_rule_applies(const script_value& seen, const type_info& target) {
		const script_value_type src = seen.type();
		const script_value_type dst = target.base_type;
		if (src == script_value_type::jai_char_type) {
			return dst == script_value_type::jai_int_type || dst == script_value_type::jai_float_type ||
			       dst == script_value_type::jai_string_type;
		}
		return dst == script_value_type::jai_char_type &&
		       (src == script_value_type::jai_int_type || src == script_value_type::jai_string_type);
	}

	inline checked_result<script_value> cast_char_value(const script_value& seen, const cast_expr& c, engine* eng,
	                                                    string_symbolizer* sym) {
		const script_value_type dst = c.target->base_type;
		if (seen.type() == script_value_type::jai_char_type) {
			const script_char ch = seen.unchecked_as_char();
			if (dst == script_value_type::jai_string_type) {
				return script_value(script_string(1, ch), eng);
			}
			const script_int code = static_cast<script_int>(static_cast<unsigned char>(ch));
			if (dst == script_value_type::jai_float_type) {
				return script_value(static_cast<script_float>(code), eng);
			}
			return script_value(code, eng);
		}
		if (seen.type() == script_value_type::jai_int_type) {
			const script_int code = seen.unchecked_as_int();
			if (code < 0 || code > 255) {
				return cast_error(seen, c, sym);
			}
			return script_value(static_cast<script_char>(static_cast<unsigned char>(code)), eng);
		}
		const script_string& text = seen.unchecked_as_string();
		if (text.size() != 1) {
			return cast_error(seen, c, sym);
		}
		return script_value(static_cast<script_char>(text[0]), eng);
	}

	// Reference form: `ref` is the bound reference (or a pinned temporary); the verdict is on
	// its referent. Class targets take the derivation rule, every other target requires the
	// referent's base type (a reference never converts). The reference itself is the result -
	// its bind-time store constraint stays untouched.
	inline checked_result<script_value> cast_reference(script_value ref, const cast_expr& c, engine* eng,
	                                                   string_symbolizer* sym) {
		const script_value& seen = ref.deref();
		if (seen.is_null()) {
			return cast_error(seen, c, sym);
		}
		const bool ok = cast_target_is_class(*c.target)
			? cast_class_admits(seen, *c.target, eng)
			: seen.type() == c.target->base_type;
		if (!ok) {
			return cast_error(seen, c, sym);
		}
		return ref;
	}

	// A temporary bound by cast<T&>: the reference holder owns it
	inline script_value cast_pin_temporary(script_value value, engine* eng) {
		if (value.is_reference()) {
			return value;
		}
		return script_value::make_cell_reference(std::move(value), eng);
	}

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_CAST_KERNEL_HPP__
