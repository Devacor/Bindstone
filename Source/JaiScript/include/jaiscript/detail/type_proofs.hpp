#pragma once

#ifndef __JAISCRIPT_DETAIL_TYPE_PROOFS_HPP__
#define __JAISCRIPT_DETAIL_TYPE_PROOFS_HPP__

// Runtime guards for parse-time type proofs (docs/type_proofs_design.md). A proof selects a
// lane; these TOTAL guards make the lane sound: TRUE only when the skipped conversion is
// provably the identity on the value actually in hand, or when the class object, its
// field-shape epoch and a present cell all match. Guard failure = the existing code runs.
// Both backends call these verbatim - never re-implement them inline.

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <cstdint>

namespace jai::detail {

    static_assert(script_value::TYPEID_INT == 1 && script_value::TYPEID_FLOAT == 2 && script_value::TYPEID_STRING == 3 &&
                  script_value::TYPEID_CHAR == 4 && script_value::TYPEID_BOOL == 5 && script_value::TYPEID_OBJECT == 8,
                  "type_proofs: storage indices moved - re-audit same_type_payload");

    // TRUE iff the unproven conversion for `target` is the identity on `v` (v deref-normalized):
    // mirrors the typed-slot table's identity rows (detail::convert_for_typed_slot_core: same-base
    // scalars/strings, the exact class-name tag) as every site consumes them - the field store
    // RETAGS scalars/strings on top (see retags_on_field_write). tag_agrees reproduces
    // defined_type(): a non-any tag must name the base.
    inline bool same_type_payload(const script_value& v, const type_info* target) noexcept {
        if (!target) return false;
        const size_t ri = v.raw_storage_index();
        const type_info* tag = v.get_type_info().get();
        const bool tag_agrees = !tag || tag->base_type == script_value_type::jai_any_type ||
                                tag->base_type == target->base_type;
        switch (target->base_type) {
            case script_value_type::jai_int_type:    return ri == script_value::TYPEID_INT && tag_agrees;
            case script_value_type::jai_float_type:  return ri == script_value::TYPEID_FLOAT && tag_agrees;
            case script_value_type::jai_bool_type:   return ri == script_value::TYPEID_BOOL && tag_agrees;
            case script_value_type::jai_char_type:   return ri == script_value::TYPEID_CHAR && tag_agrees;
            case script_value_type::jai_string_type: return ri == script_value::TYPEID_STRING && tag_agrees;
            case script_value_type::jai_object_type: return ri == script_value::TYPEID_OBJECT &&
                                                            tag == target && !target->type_name.empty();
            default: return false;   // any/null/array/map/shared_ptr/weak_ptr/reference/function: never
        }
    }

    // detail::field_store_value stamps set_type_info(declared) on scalar/string fields and leaves
    // same-class objects untouched.
    inline bool retags_on_field_write(const type_info* declared) noexcept {
        switch (declared->base_type) {
            case script_value_type::jai_int_type: case script_value_type::jai_float_type:
            case script_value_type::jai_bool_type: case script_value_type::jai_char_type:
            case script_value_type::jai_string_type: return true;
            default: return false;
        }
    }

    // Receiver guard: class_instance wrapper whose class object IS `cls` and whose field shape has
    // not moved since the stamp. Null otherwise. Never dereferences a non-class holder.
    inline class_instance* proven_field_instance(const script_value& receiver_in, const class_definition* cls,
                                                 uint64_t epoch) noexcept {
        const script_value& r = receiver_in.is_reference() ? receiver_in.deref() : receiver_in;
        if (r.raw_storage_index() != script_value::TYPEID_OBJECT) return nullptr;
        auto& holder = const_cast<script_value&>(r).unchecked_get_object_storage();
        if (!holder || !holder->is_class_instance_wrapper || !holder->data) return nullptr;
        auto* inst = static_cast<class_instance*>(holder->data.get());
        class_definition* cd = inst->get_class_definition();
        if (cd != cls || cd->field_shape_epoch() != epoch) return nullptr;
        return inst;
    }

    // Read guard: instance guard + present cell (absent/raw-invalid -> null -> the ladder runs,
    // which materializes defaults on the bare path / reads class defaults on the member path).
    inline script_value* proven_field_cell(const script_value& receiver, const class_definition* cls,
                                           uint64_t epoch, uint32_t slot, class_instance** inst_out) noexcept {
        class_instance* inst = proven_field_instance(receiver, cls, epoch);
        if (!inst) return nullptr;
        if (inst_out) *inst_out = inst;
        return inst->field_slot_value(slot);
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_TYPE_PROOFS_HPP__
