#pragma once

#ifndef __JAISCRIPT_DETAIL_REF_LVALUE_HPP__
#define __JAISCRIPT_DETAIL_REF_LVALUE_HPP__

// Reference-argument lvalue machinery shared VERBATIM by the vm and the interpreter
// (parity by construction): classification of ref-bindable argument shapes
// (f(obj.field), f(arr[i]), chains), bind-time resolution to an owner-pinned
// field/element reference, and constrained store-through enforcement.

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <vector>

namespace jai::detail {

	// Structural classifier for ref-bindable lvalue arguments: chains of plain member
	// steps (no ::, ?., ->) and subscripts with int-literal or identifier indices over
	// an identifier or `this` base, at least one step deep. Bare identifiers keep the
	// existing slot/env metadata path; everything else keeps the non-lvalue error.
	inline bool is_ref_bindable_lvalue(const expression* e) {
		bool has_step = false;
		while (e) {
			const node_type t = e->get_type();
			if (t == node_type::member_expr) {
				const auto* m = static_cast<const member_expr*>(e);
				if (m->is_static || m->null_safe || m->is_arrow) {
					return false;
				}
				e = m->object.get();
				has_step = true;
			} else if (t == node_type::binary_expr) {
				const auto* b = static_cast<const binary_expr*>(e);
				if (b->op.type != token_type::left_bracket || !b->right) {
					return false;
				}
				const node_type it = b->right->get_type();
				if (it == node_type::literal_expr) {
					if (static_cast<const literal_expr*>(b->right.get())->value.raw_storage_index() != script_value::TYPEID_INT) {
						return false;
					}
				} else if (it != node_type::identifier_expr) {
					return false;
				}
				e = b->left.get();
				has_step = true;
			} else if (t == node_type::identifier_expr || t == node_type::this_expr) {
				return has_step;
			} else {
				return false;
			}
		}
		return false;
	}

	inline checked_result<script_value> ref_non_lvalue_error() {
		return checked_result<script_value>(
			make_error_code(runtime_error_code::invalid_reference),
			"Cannot pass non-lvalue to reference parameter");
	}

	// A fields_ node is the member's TRUTH only for script-declared fields. C++
	// .property() members (dynamic_binder) keep a dead null placeholder in fields_ -
	// reads/writes route through the bound accessors - so a ref bound to the node
	// would silently swallow writes. Instance-local fields with no class declarer
	// have no accessor to bypass and stay bindable.
	inline bool ref_field_is_script_backed(const std::shared_ptr<class_instance>& instance, uint64_t member_id) {
		const class_definition* class_def = instance->get_class_definition();
		if (!class_def) {
			return true;
		}
		const class_definition* declarer = class_def->find_field_declaring_class(member_id);
		return !declarer || !declarer->is_cpp_class();
	}

	// Mirrors script_value::deref() with checked errors instead of throws (bind-time
	// resolution must never throw). Returns a shallow copy of the dereferenced value.
	inline checked_result<script_value> ref_checked_deref(const script_value& v) {
		const script_value* cur = &v;
		while (cur->is_reference()) {
			const auto* holder = cur->get_reference_holder();
			if (!holder) {
				return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Null reference");
			}
			if (holder->container) {
				if (holder->container_index >= holder->container->size()) {
					return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference to a removed array element");
				}
				cur = &(*holder->container)[holder->container_index];
				continue;
			}
			if (holder->owner_instance) {
				const script_value* field_value = holder->owner_instance->find_field_value(holder->field_id);
				if (!field_value) {
					return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference to a removed field");
				}
				cur = field_value;
				continue;
			}
			if (!holder->target) {
				return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Null reference");
			}
			if (holder->sourceEnv.expired()) {
				return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference target environment has been destroyed");
			}
			cur = holder->target;
		}
		return script_value(*cur);
	}

	// Binds a field/subscript/chain lvalue argument to a reference parameter. Resolution
	// is pure pointer/value chasing over already-evaluated state - it never runs script,
	// never throws, and never mutates the instance. Bindability = an INSTANCE fields_
	// node (find_field_value, never has_field/get_field: those consult class defaults
	// and lazy-insert) whose declaring class is script-side (ref_field_is_script_backed:
	// dynamic_binder .property() members pre-initialize a dead placeholder node whose
	// truth lives in the C++ object). Script-class fields qualify - their synthesized
	// accessors transparently wrap the node - while computed properties and C++
	// .property() members keep the non-lvalue error. Field refs pin the instance,
	// element refs pin the vector; typed fields/elements carry their current tag as the
	// assign-through constraint.
	inline checked_result<script_value> resolve_ref_lvalue(const expression* arg, call_frame* caller_locals,
	                                                       environment* caller_env,
	                                                       const std::shared_ptr<environment>& caller_env_shared,
	                                                       engine* eng, string_symbolizer* symbolizer) {
		// Collect steps outermost-first; applied base-first (reverse)
		const expression* inline_steps[8];
		std::vector<const expression*> heap_steps;
		size_t step_count = 0;
		const expression* base = arg;
		while (base) {
			const node_type t = base->get_type();
			if (t == node_type::member_expr) {
				if (step_count < 8) { inline_steps[step_count] = base; } else { heap_steps.push_back(base); }
				++step_count;
				base = static_cast<const member_expr*>(base)->object.get();
			} else if (t == node_type::binary_expr) {
				if (step_count < 8) { inline_steps[step_count] = base; } else { heap_steps.push_back(base); }
				++step_count;
				base = static_cast<const binary_expr*>(base)->left.get();
			} else {
				break;
			}
		}
		if (!base || step_count == 0) {
			return ref_non_lvalue_error();
		}
		auto step_at = [&](size_t i) -> const expression* {
			return i < 8 ? inline_steps[i] : heap_steps[i - 8];
		};

		auto resolve_identifier_ptr = [&](const identifier_expr* ident) -> const script_value* {
			if (caller_locals && ident->slot_index != SIZE_MAX) {
				if (const script_value* slot_ptr = caller_locals->get_local(ident->slot_index)) {
					return slot_ptr;
				}
			}
			if (caller_env) {
				uint64_t symbol_id = ident->symbol_id;
				if (symbol_id == UINT64_MAX) {
					symbol_id = symbolizer->intern(ident->name);
					const_cast<identifier_expr*>(ident)->symbol_id = symbol_id;
				}
				return caller_env->get_value_ptr(symbol_id);
			}
			return nullptr;
		};

		script_value cur(std::monostate{}, eng);
		if (base->get_type() == node_type::identifier_expr) {
			const script_value* base_ptr = resolve_identifier_ptr(static_cast<const identifier_expr*>(base));
			if (!base_ptr) {
				return ref_non_lvalue_error();
			}
			auto derefed = ref_checked_deref(*base_ptr);
			if (!derefed) { return derefed; }
			cur = std::move(derefed.value());
		} else {   // this_expr
			const script_value* this_ptr = nullptr;
			if (caller_locals && caller_locals->is_method) {
				this_ptr = &caller_locals->get_this();
			} else if (caller_env) {
				this_ptr = caller_env->get_value_ptr(symbolizer->get_this_id());
			}
			if (!this_ptr) {
				return ref_non_lvalue_error();
			}
			auto derefed = ref_checked_deref(*this_ptr);
			if (!derefed) { return derefed; }
			cur = std::move(derefed.value());
		}

		for (size_t i = step_count; i-- > 0;) {
			const bool is_final = (i == 0);
			const expression* step = step_at(i);
			if (step->get_type() == node_type::member_expr) {
				const auto* m = static_cast<const member_expr*>(step);
				auto holder = cur.get_object_holder();
				if (!holder || !holder->is_class_instance_wrapper || !holder->data) {
					return ref_non_lvalue_error();   // C++ objects, wrappers, non-objects: not supported
				}
				auto instance = std::static_pointer_cast<class_instance>(holder->data);
				const uint64_t member_id = m->member_id != UINT64_MAX ? m->member_id : symbolizer->intern(m->member);
				script_value* field_value = instance->find_field_value(member_id);
				if (!field_value || !ref_field_is_script_backed(instance, member_id)) {
					return ref_non_lvalue_error();
				}
				if (is_final) {
					if (field_value->is_reference()) {
						// Field already holds a reference: share its holder (matches
						// set_field's write-through semantics)
						if (!field_value->get_reference_holder()) {
							return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference target is null");
						}
						return script_value(*field_value);
					}
					type_info_ptr tag = field_value->get_type_info();
					type_info_ptr constraint = nullptr;
					if (tag && tag->base_type != script_value_type::jai_any_type &&
					    tag->base_type != script_value_type::jai_null_type) {
						constraint = tag;
					}
					return script_value::make_field_reference(instance, member_id, eng, constraint);
				}
				auto derefed = ref_checked_deref(*field_value);
				if (!derefed) { return derefed; }
				cur = std::move(derefed.value());
			} else {
				const auto* b = static_cast<const binary_expr*>(step);
				script_int index = 0;
				if (b->right->get_type() == node_type::literal_expr) {
					index = static_cast<const literal_expr*>(b->right.get())->value.unchecked_as_int();
				} else {
					const script_value* index_ptr = resolve_identifier_ptr(static_cast<const identifier_expr*>(b->right.get()));
					if (!index_ptr) {
						return ref_non_lvalue_error();
					}
					auto index_derefed = ref_checked_deref(*index_ptr);
					if (!index_derefed) { return index_derefed; }
					// is_int (not raw index) so a bound-int index stays a valid lvalue index (§7.27)
					if (!index_derefed.value().is_int()) {
						return ref_non_lvalue_error();
					}
					index = index_derefed.value().unchecked_as_int();
				}
				if (!cur.is_array()) {
					return ref_non_lvalue_error();   // maps/strings keep the non-lvalue error
				}
				auto storage = cur.get_array_storage();
				if (index < 0 || static_cast<size_t>(index) >= storage->size()) {
					return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference to a removed array element");
				}
				if (is_final) {
					script_value& element = (*storage)[static_cast<size_t>(index)];
					if (element.is_reference()) {
						if (!element.get_reference_holder()) {
							return checked_result<script_value>(make_error_code(runtime_error_code::invalid_reference), "Reference target is null");
						}
						return script_value(element);
					}
					auto array_type_info = cur.get_type_info();
					type_info_ptr element_type = array_type_info ? array_type_info->element_type() : nullptr;
					return script_value::make_element_reference(storage, static_cast<size_t>(index), caller_env_shared, eng, element_type);
				}
				auto derefed = ref_checked_deref((*storage)[static_cast<size_t>(index)]);
				if (!derefed) { return derefed; }
				cur = std::move(derefed.value());
			}
		}
		return ref_non_lvalue_error();   // unreachable: step_count >= 1
	}

	// ---- Constrained store-through enforcement (typed element/field refs) ----
	// ELEMENT refs mirror the backends' direct subscript-assign twins VERBATIM
	// (vm_is_element_type_compatible / vm_value_type_name are the source of truth; keep
	// in sync): the same store into the same typed array must pass or fail with the
	// same text whether spelled a[i] = v or written through a bound ref. FIELD refs
	// have no direct enforcement counterpart (direct field writes accept anything), so
	// their constraint is looser: actual-class resolution, subtypes, and raw instances
	// into shared_ptr-tagged fields are accepted.

	inline const char* ref_type_name_of(script_value_type type) {
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

	inline std::string ref_value_type_name(const script_value& val) {
		const script_value& actual = val.is_reference() ? val.deref() : val;
		auto type_info = actual.get_type_info();
		if (type_info && !type_info->type_name.empty() &&
		    type_info->base_type != script_value_type::jai_any_type) {
			return type_info->type_name;
		}
		// Dynamic (var) tags say 'any': name the actual instance class instead
		if (auto holder = actual.get_object_holder(); holder && !holder->type_name.empty()) {
			return holder->type_name;
		}
		return ref_type_name_of(actual.current_type());
	}

	inline std::string ref_type_info_name(type_info_ptr info) {
		if (info && !info->type_name.empty()) {
			return info->type_name;
		}
		if (info) {
			return ref_type_name_of(info->base_type);
		}
		return "unknown";
	}

	// Element errors name {0} exactly like the direct subscript-assign error does
	// (vm_value_type_name / get_value_type_name: tag name, no deref, 'any' for var tags)
	inline std::string ref_element_value_type_name(const script_value& val) {
		auto type_info = val.get_type_info();
		if (type_info && !type_info->type_name.empty()) {
			return type_info->type_name;
		}
		return ref_type_name_of(val.type());
	}

	// Field constraints render shared_ptr tags in full: their type_name alone is the
	// pointee class, which made "Cannot assign 'S' to field of type 'S'" possible
	inline std::string ref_field_constraint_name(type_info_ptr info) {
		if (info && info->base_type == script_value_type::jai_shared_ptr_type && !info->type_name.empty()) {
			return "shared_ptr<" + info->type_name + ">";
		}
		return ref_type_info_name(info);
	}

	// Object constraints resolve the ACTUAL instance class (a var local's tag is 'any';
	// shared_ptr values wrap the same object_holder) and accept subtypes - mirrors
	// try_convert_for_parameter's acceptance for typed class parameters.
	inline bool ref_object_constraint_accepts(const script_value& actual, type_info_ptr constraint, engine* eng) {
		auto holder = actual.get_object_holder();
		if (!holder) {
			return false;
		}
		if (constraint->type_name.empty()) {
			return true;
		}
		const class_definition* class_def = nullptr;
		std::shared_ptr<class_definition> engine_def;
		if (holder->is_class_instance_wrapper && holder->data) {
			auto instance = std::static_pointer_cast<class_instance>(holder->data);
			class_def = instance->get_class_definition();
		} else if (eng) {
			engine_def = holder->type_id != UINT64_MAX ? eng->get_class_definition(holder->type_id)
			                                           : eng->get_class_definition(holder->type_name);
			class_def = engine_def.get();
		}
		if (class_def && class_def->is_subtype_of(constraint->type_name)) {
			return true;
		}
		if (holder->type_name == constraint->type_name) {
			return true;
		}
		auto tag = actual.get_type_info();
		return tag && tag->base_type != script_value_type::jai_any_type &&
		       tag->type_name == constraint->type_name;
	}

	inline bool ref_container_kind_compatible(const script_value& actual_element, type_info_ptr element_type,
	                                          script_value_type elem_type, script_value_type target_type) {
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

	// Verbatim mirror of vm_is_element_type_compatible / is_element_type_compatible
	// (route-independence with direct subscript assign, including its exact-name object
	// rule and rejection of 'any'-tagged values)
	inline bool ref_element_constraint_compatible(const script_value& element, type_info_ptr element_type) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;
		if (!element_type || element_type->base_type == script_value_type::jai_any_type) {
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
		return ref_container_kind_compatible(actual_element, element_type, elem_type, target_type);
	}

	inline bool ref_field_constraint_compatible(const script_value& element, type_info_ptr element_type, engine* eng) {
		const script_value& actual_element = element.is_reference() ? element.deref() : element;
		if (!element_type || element_type->base_type == script_value_type::jai_any_type) {
			return true;
		}
		auto elem_type = actual_element.type();
		auto target_type = element_type->base_type;
		if (target_type == script_value_type::jai_object_type) {
			if (ref_object_constraint_accepts(actual_element, element_type, eng)) {
				return true;
			}
			if (elem_type == target_type) {
				// Preserve the permissive fallback for untagged/unnamed values
				auto elem_type_info = actual_element.get_type_info();
				return !elem_type_info || element_type->type_name.empty();
			}
			return false;
		}
		if (elem_type == target_type) {
			return true;
		}
		if (target_type == script_value_type::jai_shared_ptr_type) {
			// Dynamic (var) tags hide the shared_ptr marker behind current_type();
			// the holder is the ground truth (name laxity matches the tagged path)
			auto tag = actual_element.get_type_info();
			if ((!tag || tag->base_type == script_value_type::jai_any_type) &&
			    actual_element.get_object_holder() != nullptr) {
				return true;
			}
			// A raw same-class/subtype instance is accepted into a shared_ptr-tagged
			// field: every direct field-write path accepts it (it clones in as object)
			if (ref_object_constraint_accepts(actual_element, element_type, eng)) {
				return true;
			}
		}
		if (target_type == script_value_type::jai_int_type && elem_type == script_value_type::jai_float_type) {
			return true;
		}
		if (target_type == script_value_type::jai_float_type && elem_type == script_value_type::jai_int_type) {
			return true;
		}
		return ref_container_kind_compatible(actual_element, element_type, elem_type, target_type);
	}

	inline script_value ref_convert_for_constraint(engine* eng, const script_value& element, type_info_ptr element_type) {
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

	inline bool ref_constraint_compatible(const script_value& value, type_info_ptr constraint,
	                                      engine* eng, bool is_field) {
		return is_field ? ref_field_constraint_compatible(value, constraint, eng)
		                : ref_element_constraint_compatible(value, constraint);
	}

	inline checked_result<void> ref_constraint_error(string_symbolizer* symbolizer, const script_value& value,
	                                                 type_info_ptr constraint, bool is_field) {
		uint64_t value_type_id = symbolizer->intern(is_field ? ref_value_type_name(value)
		                                                     : ref_element_value_type_name(value));
		uint64_t expected_type_id = symbolizer->intern(is_field ? ref_field_constraint_name(constraint)
		                                                        : ref_type_info_name(constraint));
		return checked_result<void>(
			make_error_code(is_field ? runtime_error_code::type_mismatch
			                         : runtime_error_code::array_element_type_mismatch),
			is_field ? "Cannot assign '{0}' to field of type '{1}'"
			         : "Cannot assign '{0}' to element of type '{1}'",
			value_type_id, expected_type_id);
	}

	// Typed OBJECT fields stay nullable through a reference: the declaration idiom is
	// `TreeNode left = null`, so null must round-trip (elements keep their stricter
	// existing semantics - this branch is field-only).
	inline bool ref_field_accepts_null(const script_value& value, type_info_ptr constraint) {
		return value.deref().is_null() &&
		       (constraint->base_type == script_value_type::jai_object_type ||
		        constraint->base_type == script_value_type::jai_shared_ptr_type);
	}

	// Identifier-assignment through a reference local: unconstrained refs keep today's
	// exact clone-through-deref line; constrained element/field refs enforce like the
	// subscript-assign paths do.
	inline checked_result<void> ref_store_through(script_value& refLocal, script_value& value,
	                                              engine* eng, string_symbolizer* symbolizer) {
		const auto* holder = refLocal.get_reference_holder();
		if (!holder || !holder->container_element_type) {
			refLocal.deref() = std::move(value.deref().clone());
			return {};
		}
		type_info_ptr constraint = holder->container_element_type;
		const bool is_field = holder->owner_instance != nullptr;
		if (is_field && ref_field_accepts_null(value, constraint)) {
			refLocal.deref() = std::move(value.deref().clone());
			return {};
		}
		if (!ref_constraint_compatible(value, constraint, eng, is_field)) {
			return ref_constraint_error(symbolizer, value, constraint, is_field);
		}
		script_value converted = ref_convert_for_constraint(eng, value, constraint);
		refLocal.deref() = std::move(converted);
		return {};
	}

	// Constrained-ref compound assignment (x op= v where x is bound to a typed element
	// or field): mirrors the subscript-compound semantics - custom operator first, then
	// arithmetic, then constraint check + convert + write-through. `arith` is the
	// backend's evaluate_arithmetic. Returns the (pre-conversion) expression result.
	// Operator symbols are only definable in the global env (C++ add_function), so the
	// consult takes the GLOBAL env - a caller-chain walk is O(depth) in deep recursion.
	template <typename Arith>
	inline checked_result<script_value> ref_compound_store_constrained(
			script_value& refLocal, const script_value& rightArg, token_type op, const char* opName,
			environment* global_env, engine* eng, string_symbolizer* symbolizer, Arith&& arith) {
		// Current-value read raw-derefs exactly like the unconstrained compound path: a
		// removed element/field surfaces as the same caller-frame-catchable throw whether
		// or not the bound container was typed
		script_value currentValue = refLocal.deref();
		const script_value& rightDeref = rightArg.deref();
		// S8 (NEW-C): a bound rhs decodes up front so the /= and %= zero pre-checks below keep
		// today's error codes/texts (shared by both backends; `arith` normalizes bound lhs)
		const bool rightBound = rightDeref.raw_storage_index() == script_value::TYPEID_CPP_BOUND;
		const script_value rightNorm = rightBound ? rightDeref.bound_decoded_temp() : script_value(std::monostate{}, eng);
		const script_value& rightValue = rightBound ? rightNorm : rightDeref;
		script_value resultValue(std::monostate{}, eng);
		auto op_result = global_env->get(opName);
		if (op_result && op_result.value().is_function()) {
			script_value opFunc = std::move(op_result.value());
			const script_function& func = opFunc.as_function();
			std::vector<script_value> args = {currentValue, rightValue};
			auto result = func(args);
			if (!result) {
				return result;
			}
			resultValue = std::move(result.value());
		} else {
			switch (op) {
				case token_type::plus: {
					if (currentValue.is_string() || rightValue.is_string()) {
						resultValue = script_value(currentValue.to_string() + rightValue.to_string(), eng);
					} else {
						auto r = arith(currentValue, token_type::plus, rightValue);
						if (!r) { return r; }
						resultValue = std::move(r.value());
					}
					break;
				}
				case token_type::minus:
				case token_type::star: {
					auto r = arith(currentValue, op, rightValue);
					if (!r) { return r; }
					resultValue = std::move(r.value());
					break;
				}
				case token_type::slash: {
					const size_t ri = rightValue.raw_storage_index();
					if ((ri == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) ||
					    (ri == script_value::TYPEID_FLOAT && rightValue.unchecked_as_float() == 0.0)) {
						return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Division by zero");
					}
					auto r = arith(currentValue, token_type::slash, rightValue);
					if (!r) { return r; }
					resultValue = std::move(r.value());
					break;
				}
				case token_type::percent: {
					if (rightValue.raw_storage_index() == script_value::TYPEID_INT && rightValue.unchecked_as_int() == 0) {
						return checked_result<script_value>(make_error_code(runtime_error_code::division_by_zero), "Modulo by zero");
					}
					auto r = arith(currentValue, token_type::percent, rightValue);
					if (!r) { return r; }
					resultValue = std::move(r.value());
					break;
				}
				default:
					return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
						"Unknown compound assignment operator");
			}
		}
		const auto* holder = refLocal.get_reference_holder();
		type_info_ptr constraint = holder ? holder->container_element_type : nullptr;
		const bool is_field = holder && holder->owner_instance != nullptr;
		if (!ref_constraint_compatible(resultValue, constraint, eng, is_field)) {
			auto err = ref_constraint_error(symbolizer, resultValue, constraint, is_field);
			return err.error_value();
		}
		script_value converted = ref_convert_for_constraint(eng, resultValue, constraint);
		refLocal.deref() = std::move(converted);
		return resultValue;
	}

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_REF_LVALUE_HPP__
