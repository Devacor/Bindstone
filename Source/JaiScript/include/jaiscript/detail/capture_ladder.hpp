#pragma once

// Capture-source ladder shared VERBATIM by the interpreter (visit_lambda_expr) and the vm
// (exec_closure): the SOURCE a captured name binds to, resolved in the runtime's own
// order - the enclosing frame's slot (locals and parameters), the member scope when
// active (the function's own env scopes below the floor, then fields / static fields /
// a hybrid's C++ property; a method name shadows and is not a cell), then the environment
// chain from the creating env (definition env -> global). A same-named global never wins
// over a slot or a member. The capture KIND is the caller's: [&]/[&x] cell-share the
// source, [=]/[x]/bare [] snapshot it, a C++ property binds through
// bind_cpp_property_ref / read_bare_cpp_property. An explicit [x]/[&x] takes variables
// only (slot, own scope, chain): a member is not a capturable variable (C++: ill-formed),
// so [field] keeps the undefined-capture text whether or not a global shares the name.

#include <jaiscript/detail/member_scope.hpp>

namespace jai::detail {

struct capture_source {
    enum class kind : uint8_t { miss, slot, own_scope, member, cpp_property, shadow, chain };
    kind what = kind::miss;
    script_value* cell = nullptr;   // slot / own_scope / member / chain: stable storage
    member_probe_hit property;      // cpp_property: the probe (instance, _get_ accessor, getter id)

    // Default captures take every cell; explicit captures take variables only
    bool is_variable() const {
        return what == kind::slot || what == kind::own_scope || what == kind::chain;
    }
};

inline capture_source resolve_capture_source(script_value* frame_slot_cell,
                                             environment* current,
                                             const member_scope_ctx& scope,
                                             uint64_t sym) {
    capture_source src;
    if (frame_slot_cell) {
        src.what = capture_source::kind::slot;
        src.cell = frame_slot_cell;
        return src;
    }
    if (scope.active()) {
        if (script_value* own = own_scope_lookup(current, scope.floor, sym)) {
            src.what = capture_source::kind::own_scope;
            src.cell = own;
            return src;
        }
        auto hit = probe_member_scope(scope.state, scope.this_storage, scope.statics, sym, /*want_methods*/ true);
        switch (hit.what) {
        case member_probe_hit::kind::field:
        case member_probe_hit::kind::static_field:
            src.what = capture_source::kind::member;
            src.cell = hit.value_ptr;
            return src;
        case member_probe_hit::kind::cpp_property:
            src.what = capture_source::kind::cpp_property;
            src.property = std::move(hit);
            return src;
        case member_probe_hit::kind::method:
            src.what = capture_source::kind::shadow;
            return src;
        case member_probe_hit::kind::miss:
            break;
        }
        src.cell = scope.floor ? scope.floor->get_value_ptr(sym) : current->get_value_ptr(sym);
    } else {
        src.cell = current->get_value_ptr(sym);
    }
    if (src.cell) src.what = capture_source::kind::chain;
    return src;
}

// The vm keeps its member scope on the frame (call_frame scope_*; the interpreter passes
// member_scope_ itself). An uncomputed frame reads as no scope.
inline member_scope_ctx frame_member_scope(const call_frame& locals) {
    member_scope_ctx ctx;
    ctx.state = static_cast<member_scope_state>(locals.scope_state);
    ctx.floor = locals.scope_floor;
    ctx.this_storage = locals.scope_this;
    ctx.statics = locals.scope_static;
    return ctx;
}

} // namespace jai::detail
