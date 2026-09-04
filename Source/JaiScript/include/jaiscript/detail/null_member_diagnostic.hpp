#pragma once

#include <jaiscript/detail/ast.hpp>

namespace jai {

// The symbol naming a member-access receiver: `obj.frob()` -> obj, `game().creature(id).dot()` ->
// creature. Zero when the receiver has no name (a literal, an index, a ternary).
inline uint64_t null_member_receiver_symbol(const expression* receiver) {
    if (!receiver) { return 0; }
    switch (receiver->get_type()) {
        case node_type::identifier_expr: return static_cast<const identifier_expr*>(receiver)->symbol_id;
        case node_type::member_expr: return static_cast<const member_expr*>(receiver)->member_id;
        case node_type::call_expr: return null_member_receiver_symbol(static_cast<const call_expr*>(receiver)->callee.get());
        default: return 0;
    }
}

// {0} = the member, {1} = the receiver symbol (only used by the shapes that have one)
inline std::string_view null_member_message(const expression* receiver) {
    if (null_member_receiver_symbol(receiver) == 0) { return "Member '{0}' accessed on null value"; }
    return receiver->get_type() == node_type::call_expr
        ? "Member '{0}' accessed on null: '{1}(...)' returned null"
        : "Member '{0}' accessed on null: '{1}' is null";
}

// A non-null receiver with no member table at all (int, float, bool, char, function, ...):
// {0} = the receiver's base type spelling, {1} = the member
inline constexpr std::string_view scalar_member_message = "{0} has no member '{1}'";

inline constexpr std::string_view base_type_spelling(script_value_type type) {
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

// Interned base-type spellings, filled once at backend init: error paths never intern
// (parallel workers run against a frozen symbolizer)
struct base_type_symbols {
    static constexpr size_t count = static_cast<size_t>(script_value_type::jai_invalid_type) + 1;
    uint64_t ids[count] = {};
    template <typename Symbolizer>
    void intern(const Symbolizer& symbolizer) {
        for (size_t i = 0; i < count; ++i) {
            ids[i] = symbolizer.intern(base_type_spelling(static_cast<script_value_type>(i)));
        }
    }
    uint64_t operator[](script_value_type type) const {
        const size_t i = static_cast<size_t>(type);
        return i < count ? ids[i] : ids[count - 1];
    }
};

}
