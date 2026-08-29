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

}
