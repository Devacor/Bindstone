#pragma once

#ifndef __JAISCRIPT_DETAIL_RETURN_CONV_HPP__
#define __JAISCRIPT_DETAIL_RETURN_CONV_HPP__

// Typed-return classification shared by both backends: which route a `return` takes
// through the one typed-slot table (typed_slot.hpp). Derived from the declared return
// type alone (engine-independent), memoized per function through return_conv_of.
// none = untyped/any/void/auto (deref only); ref = the pass-through kernel
// (ref_lvalue.hpp ref_return_pass_through); prim_* = a storage-matching plain result is
// the identity (exactly what the table's identity row yields); check = the full table.

#include <jaiscript/core/type_info.hpp>
#include <jaiscript/detail/environment.hpp>

namespace jai::detail {

    enum class return_conv : uint8_t {
        unclassified = 0,
        none,
        ref,
        prim_int,
        prim_float,
        prim_bool,
        prim_char,
        prim_string,
        check,
    };

    inline return_conv classify_return_conv(const type_info* t) {
        if (!t || t->base_type == script_value_type::jai_any_type) {
            return return_conv::none;
        }
        if (t->base_type == script_value_type::jai_reference_type) {
            return return_conv::ref;
        }
        if (t->type_name.empty() || t->type_name == "void" || t->type_name == "auto") {
            return return_conv::none;
        }
        switch (t->base_type) {
            case script_value_type::jai_int_type: return return_conv::prim_int;
            case script_value_type::jai_float_type: return return_conv::prim_float;
            case script_value_type::jai_bool_type: return return_conv::prim_bool;
            case script_value_type::jai_char_type: return return_conv::prim_char;
            case script_value_type::jai_string_type: return return_conv::prim_string;
            default: return return_conv::check;
        }
    }

    // Memo on script_defined_function / function_decl::backend_return_conv (mutable; the
    // idempotent write is benign - parallel workers hold private copies)
    template <typename F>
    inline return_conv return_conv_of(const F& fn) {
        if (fn.backend_return_conv == 0) {
            fn.backend_return_conv = static_cast<uint8_t>(classify_return_conv(fn.return_type.get()));
        }
        return static_cast<return_conv>(fn.backend_return_conv);
    }

    // prim_* lanes: a storage-matching plain result needs no conversion (the raw index
    // excludes bound values and references, which take the full route)
    inline bool return_conv_identity(return_conv conv, const script_value& v) noexcept {
        switch (conv) {
            case return_conv::prim_int: return v.raw_storage_index() == script_value::TYPEID_INT;
            case return_conv::prim_float: return v.raw_storage_index() == script_value::TYPEID_FLOAT;
            case return_conv::prim_bool: return v.raw_storage_index() == script_value::TYPEID_BOOL;
            case return_conv::prim_char: return v.raw_storage_index() == script_value::TYPEID_CHAR;
            case return_conv::prim_string: return v.raw_storage_index() == script_value::TYPEID_STRING;
            default: return false;
        }
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_RETURN_CONV_HPP__
