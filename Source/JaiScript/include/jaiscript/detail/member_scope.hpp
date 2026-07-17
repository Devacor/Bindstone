#pragma once

// Member-shadowing kernel: C++ unqualified-lookup order for bare identifiers inside
// methods - locals and enclosing function scopes first, then the class's members,
// then the definition/global environments. Shared VERBATIM by the interpreter and
// the vm (ref_lvalue.hpp discipline) so the two backends cannot drift.
//
// Each backend stamps a per-frame member scope onto call_frame (scope_state/
// scope_floor/scope_this/scope_static): the interpreter at frame push, where the
// call branch knows the callee kind; the vm lazily from entry_env/locals. A site
// resolving a bare identifier then runs:
//
//   1. slots / catch var (unchanged)
//   2. own_scope_lookup: the env chain from environment_ up to (exclusive)
//      scope_floor, OWN storage only - block/capture cells keep beating members
//   3. probe_member_scope: fields -> methods -> statics (instance scopes),
//      static fields -> static methods (static scopes)
//   4. the walk from scope_floor onward (definition env -> global), caches intact
//
// Frames with scope_state none keep their original ladder byte-for-byte.

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/detail/environment.hpp>

namespace jai::detail {

enum class member_scope_state : uint8_t {
    uncomputed = 0,
    none,      // not a member scope - sites keep their original ladder
    instance,  // instance method / method coroutine: probe scope_this's members
    statics,   // static method: probe scope_static's statics
};

// The interpreter's current-frame member scope (the vm carries the same fields on
// call_frame): saved/restored by call_function around every call and stamped by the
// branch that knows the callee kind, plus the env-swap-without-frame sites (field
// initializers). state none = original resolution ladder, byte-for-byte.
struct member_scope_ctx {
    member_scope_state state = member_scope_state::none;
    environment* floor = nullptr;
    script_value* this_storage = nullptr;
    class_definition* statics = nullptr;

    bool active() const {
        return state == member_scope_state::instance || state == member_scope_state::statics;
    }
};

struct member_probe_hit {
    enum class kind : uint8_t { miss, field, static_field, method };
    kind what = kind::miss;
    script_value* value_ptr = nullptr;   // field/static_field: stable storage
    // method: the stored (unbound) value (null-engine monostate until a hit fills it)
    script_value method_value{std::monostate{}, static_cast<engine*>(nullptr)};
    class_instance* instance = nullptr;  // field: owning instance (write kernels)
    class_definition* class_def = nullptr;

    explicit operator bool() const { return what != kind::miss; }
};

// The class-scope boundary in a lexical chain: the first method/static env.
inline environment* find_member_boundary(environment* from) {
    for (environment* e = from; e; e = e->parent_raw()) {
        if (e->is_method_env() || e->is_static_method_env()) return e;
    }
    return nullptr;
}

inline environment* chain_root(environment* from) {
    environment* e = from;
    while (e && e->parent_raw()) e = e->parent_raw();
    return e;
}

// Phase 2 of the ladder: the function's own scopes - [current .. floor) OWN storage
// only. A null floor (member probe belongs before the whole walk) yields no hits.
inline script_value* own_scope_lookup(environment* current, const environment* floor, uint64_t id) {
    for (environment* e = current; e && e != floor; e = e->parent_raw()) {
        if (script_value* own = e->get_own_value_ptr(id)) return own;
    }
    return nullptr;
}

// Phase 3: the boundary probe. Field -> method -> static (the same surface and
// relative order the old post-walk fallbacks answered). want_methods=false lets
// pointer-only resolvers (fast binary operands, store targets) skip the mint rung;
// their callers bail to a full-probing path when the name could be a method.
inline member_probe_hit probe_member_scope(member_scope_state state,
                                           script_value* scope_this,
                                           class_definition* scope_static,
                                           uint64_t sym,
                                           bool want_methods) {
    member_probe_hit hit;
    if (state == member_scope_state::instance && scope_this) {
        script_value& this_val = *scope_this;
        if (this_val.is_null()) return hit;
        const auto this_type = this_val.type();
        if (this_type != script_value_type::jai_object_type &&
            this_type != script_value_type::jai_shared_ptr_type) return hit;
        auto instance = this_val.get_class_instance();
        if (!instance) return hit;
        script_value& field_ref = instance->get_field(sym, false);
        if (!field_ref.is_invalid()) {
            hit.what = member_probe_hit::kind::field;
            hit.value_ptr = &field_ref;
            hit.instance = instance.get();
            hit.class_def = instance->get_class_definition();
            return hit;
        }
        if (want_methods) {
            script_value method = instance->get_method(sym, false);
            if (!method.is_invalid()) {
                hit.what = member_probe_hit::kind::method;
                hit.method_value = std::move(method);
                hit.instance = instance.get();
                hit.class_def = instance->get_class_definition();
                return hit;
            }
        }
        if (class_definition* class_def = instance->get_class_definition()) {
            if (script_value* static_ptr = class_def->get_static_field_ptr(sym)) {
                hit.what = member_probe_hit::kind::static_field;
                hit.value_ptr = static_ptr;
                hit.class_def = class_def;
                return hit;
            }
            if (want_methods) {
                auto& statics = class_def->static_method_values();
                auto it = statics.find(sym);
                if (it != statics.end()) {
                    hit.what = member_probe_hit::kind::method;
                    hit.method_value = it->second;
                    hit.class_def = class_def;
                    return hit;
                }
            }
        }
        return hit;
    }
    if (state == member_scope_state::statics && scope_static) {
        if (script_value* static_ptr = scope_static->get_static_field_ptr(sym)) {
            hit.what = member_probe_hit::kind::static_field;
            hit.value_ptr = static_ptr;
            hit.class_def = scope_static;
            return hit;
        }
        if (want_methods) {
            auto& statics = scope_static->static_method_values();
            auto it = statics.find(sym);
            if (it != statics.end()) {
                hit.what = member_probe_hit::kind::method;
                hit.method_value = it->second;
                hit.class_def = scope_static;
                return hit;
            }
        }
    }
    return hit;
}

// A name that could be a member METHOD under this scope (fields/statics answer via
// pointer rungs). Pointer-only resolvers use this to bail to their minting path.
inline bool member_scope_has_method(member_scope_state state,
                                    script_value* scope_this,
                                    class_definition* scope_static,
                                    uint64_t sym) {
    if (state == member_scope_state::instance && scope_this) {
        const auto this_type = scope_this->type();
        if (scope_this->is_null() ||
            (this_type != script_value_type::jai_object_type &&
             this_type != script_value_type::jai_shared_ptr_type)) return false;
        auto instance = scope_this->get_class_instance();
        if (!instance) return false;
        script_value method = instance->get_method(sym, false);
        if (!method.is_invalid()) return true;
        if (class_definition* class_def = instance->get_class_definition()) {
            return class_def->static_method_values().count(sym) > 0;
        }
        return false;
    }
    if (state == member_scope_state::statics && scope_static) {
        return scope_static->static_method_values().count(sym) > 0;
    }
    return false;
}

// Lazy scope compute from frame ingredients (the vm path; the interpreter stamps at
// push). entry_env carries the lexical chain: null = lexically top-level UNLESS the
// frame itself is a method frame (lazy method bodies run directly on the definition
// env - current_env at compute time IS the floor, lazy bodies never move it).
inline void compute_member_scope(call_frame& locals,
                                 environment* entry_env,
                                 environment* current_env) {
    locals.scope_state = static_cast<uint8_t>(member_scope_state::none);
    if (locals.is_method && locals.this_object_ptr) {
        locals.scope_this = locals.this_object_ptr.get();
        locals.scope_state = static_cast<uint8_t>(member_scope_state::instance);
        if (!entry_env) {
            locals.scope_floor = current_env;                       // lazy method body
        } else if (entry_env->is_method_env()) {
            locals.scope_floor = entry_env->parent_raw();           // env-backed method
        } else if (environment* boundary = find_member_boundary(entry_env)) {
            locals.scope_floor = (boundary == entry_env) ? boundary->parent_raw() : boundary;
        } else {
            locals.scope_floor = chain_root(current_env);           // receiver-pinned fiber:
        }                                                           // members beat globals only
        return;
    }
    if (locals.is_static_method && locals.static_class_def) {
        locals.scope_static = locals.static_class_def.get();
        locals.scope_state = static_cast<uint8_t>(member_scope_state::statics);
        locals.scope_floor = entry_env
            ? (entry_env->is_static_method_env() ? entry_env->parent_raw()
                                                 : entry_env)
            : current_env;
        return;
    }
    if (!entry_env) return;                                         // plain function
    environment* boundary = find_member_boundary(entry_env);
    if (!boundary) return;                                          // no class scope in chain
    environment* floor = (boundary == entry_env) ? boundary->parent_raw() : boundary;
    if (boundary->is_method_env()) {
        if (script_value* this_storage = boundary->method_this_storage()) {
            locals.scope_this = this_storage;
            locals.scope_floor = floor;
            locals.scope_state = static_cast<uint8_t>(member_scope_state::instance);
        }
        return;
    }
    if (auto class_def = boundary->get_class_definition()) {
        locals.scope_static = class_def.get();
        locals.scope_floor = floor;
        locals.scope_state = static_cast<uint8_t>(member_scope_state::statics);
    }
}

} // namespace jai::detail
