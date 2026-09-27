#pragma once

#ifndef __JAISCRIPT_DETAIL_ARG_CAPTURE_HPP__
#define __JAISCRIPT_DETAIL_ARG_CAPTURE_HPP__

// A by-value argument takes its value when it is evaluated. The parse-time stamp
// (transient_read_marker::stamp_arg_captures) names the arguments a later argument may write; each
// backend captures such an argument right after evaluating it, by how the resolved callee binds that
// position. Shared verbatim by both backends.

#include <jaiscript/core/script_class.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/transient_read.hpp>

namespace jai::detail {

    // copy: a by-value parameter the binding copies; share: a handle parameter (shared_ptr/weak_ptr) the binding shares;
    // reference: the lvalue binds, nothing is taken; opaque: a host callable, whose C++ parameter decides (the value is
    // dereferenced, a heavy handle stays the object the argument named)
    enum class arg_binding : uint8_t { copy, share, reference, opaque };

    inline arg_binding parameter_binding(const parameter& p) {
        if (p.is_reference) return arg_binding::reference;
        if (p.type && (p.type->base_type == script_value_type::jai_shared_ptr_type || p.type->base_type == script_value_type::jai_weak_ptr_type)) return arg_binding::share;
        return arg_binding::copy;
    }

    inline arg_binding parameters_binding(const std::vector<parameter>& params, size_t i) {
        return i < params.size() ? parameter_binding(params[i]) : arg_binding::copy;
    }

    // an overload set binds a position the way every candidate does; a mix takes the least a candidate allows
    inline arg_binding overloads_binding(const std::vector<std::shared_ptr<function_decl>>& overloads, size_t i) {
        if (overloads.empty()) return arg_binding::opaque;
        arg_binding out = arg_binding::copy;
        for (const auto& o : overloads) {
            if (!o) continue;
            const arg_binding b = parameters_binding(o->parameters, i);
            if (b == arg_binding::reference) return b;
            if (b == arg_binding::share) out = b;
        }
        return out;
    }

    inline arg_binding method_binding(const class_definition* cls, uint64_t name_id, size_t i) {
        if (!cls) return arg_binding::opaque;
        const auto& table = cls->script_method_overloads();
        auto it = table.find(name_id);
        return it == table.end() ? arg_binding::opaque : overloads_binding(it->second, i);
    }

    inline arg_binding constructor_binding(const class_definition* cls, size_t i) {
        if (!cls || !cls->is_script_class()) return arg_binding::opaque;
        const auto& ctors = static_cast<const script_class_definition*>(cls)->get_constructor_asts();
        return ctors.empty() ? arg_binding::copy : overloads_binding(ctors, i);
    }

    inline arg_binding callee_binding(const script_value& callee, size_t i, bool through_bound = false) {
        const script_value& c = callee.deref();
        if (c.raw_storage_index() != script_value::TYPEID_FUNCTION) return arg_binding::opaque;
        const script_function& fn = c.unchecked_as_function();
        if (const auto* thunk = fn.target<script_callable_thunk>()) {
            const script_callable& p = thunk->payload;
            switch (p.kind) {
                case script_callable::kind_type::function: return p.fn ? parameters_binding(p.fn->parameters(), i) : arg_binding::opaque;
                case script_callable::kind_type::method:
                case script_callable::kind_type::static_method: return p.ast ? parameters_binding(p.ast->parameters, i) : arg_binding::opaque;
                case script_callable::kind_type::constructor: return constructor_binding(p.cls.get(), i);
                case script_callable::kind_type::bound_method: return p.bound_dispatch && !through_bound ? callee_binding(*p.bound_dispatch, i, true) : arg_binding::opaque;
            }
        }
        if (const auto* d = fn.target<script_method_dispatch>()) return method_binding(d->cls.get(), d->name_id, i);
        if (const auto* bm = fn.target<bound_method_fn>(); bm && bm->method.raw_storage_index() == script_value::TYPEID_FUNCTION) {
            if (const auto* d = bm->method.unchecked_as_function().target<script_method_dispatch>()) return method_binding(d->cls.get(), d->name_id, i);
        }
        return arg_binding::opaque;
    }

    // `recv.m(args)` resolved by lookup alone (no getter runs): a field's callable, else the class's method; `super.m` the
    // receiver's parent's method; any other receiver is a host or builtin callee
    inline arg_binding member_call_binding(const script_value& receiver, uint64_t member_id, bool super_call, size_t i) {
        const script_value& recv = receiver.deref();
        const size_t ri = recv.raw_storage_index();
        if (ri != script_value::TYPEID_OBJECT && ri != script_value::TYPEID_SHARED_PTR) return arg_binding::opaque;
        const auto holder = recv.get_object_holder();
        if (!holder || !holder->is_class_instance_wrapper || !holder->data) return arg_binding::opaque;
        auto* inst = static_cast<class_instance*>(holder->data.get());
        const class_definition* cd = inst->get_class_definition();
        if (!cd) return arg_binding::opaque;
        if (super_call) {
            const auto parent = cd->get_parent();
            return parent ? callee_binding(parent->get_method(member_id, false), i) : arg_binding::opaque;
        }
        if (const script_value* field = inst->find_field_value(member_id)) return callee_binding(*field, i);
        return callee_binding(cd->get_method(member_id, false), i);
    }

    // what the binding would make of the argument now, in place: a copy parameter's value is dereferenced, its heavy payload
    // copied and a host-bound primitive decoded; a handle parameter's and an opaque callee's is dereferenced
    inline void capture_argument(script_value& v, uint8_t kind, arg_binding b) {
        if (b == arg_binding::reference) return;
        if (v.raw_storage_index() == script_value::TYPEID_REFERENCE) {
            script_value d = v.deref();
            v = std::move(d);
        } else if (kind != transient_read_marker::arg_capture_full) {
            return;
        }
        if (b != arg_binding::copy) return;
        const size_t ri = v.raw_storage_index();
        if (ri == script_value::TYPEID_CPP_BOUND) { v = v.detached_for_store(); return; }
        if (ri != script_value::TYPEID_OBJECT && ri != script_value::TYPEID_ARRAY && ri != script_value::TYPEID_MAP) return;
        if (v.get_type_info() && v.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) return;
        try { v = v.clone(); } catch (const std::exception&) {}   // a value that refuses the copy (nocopy) raises at the binding, as it always has
    }

    inline bool capture_candidate(const script_value& v, uint8_t kind) {
        const size_t ri = v.raw_storage_index();
        if (ri == script_value::TYPEID_REFERENCE) return true;
        return kind == transient_read_marker::arg_capture_full &&
               (ri == script_value::TYPEID_OBJECT || ri == script_value::TYPEID_ARRAY || ri == script_value::TYPEID_MAP || ri == script_value::TYPEID_CPP_BOUND);
    }

    // argument i of a stamped list, just evaluated: classified (binding() resolves the callee) only when its value may still share storage
    template<typename Binding>
    inline void capture_call_argument(const std::vector<uint8_t>& captures, size_t i, script_value& v, Binding&& binding) {
        if (i >= captures.size() || captures[i] == 0 || !capture_candidate(v, captures[i])) return;
        capture_argument(v, captures[i], binding());
    }

    // an initializer list's argument binds only when its constructor's body runs, after the chain's field initializers: when
    // those may run code, every read of storage is taken, else the stamp's
    template<typename Binding>
    inline void capture_initializer_argument(const constructor_initializer& ci, size_t i, bool fields_run_code, script_value& v, Binding&& binding) {
        const uint8_t kind = fields_run_code ? transient_read_marker::arg_capture_kind(ci.arguments[i].get()) : i < ci.arg_captures.size() ? ci.arg_captures[i] : 0;
        if (kind == 0 || !capture_candidate(v, kind)) return;
        capture_argument(v, kind, binding());
    }

    // the construction runs code ahead of the body (an initializer list, a non-literal field initializer)
    inline bool construction_runs_code(const script_class_definition& cls, const function_decl& ctor) {
        return !ctor.initializers.empty() || !cls.chain_initializers_all_literal();
    }

    // a parameter's conversion that may run user code (a converting constructor, an object's to_int/to_float, a class element
    // conversion): the arguments after it were taken before the binding began, so they are captured ahead of it
    inline bool binding_may_run_code(const script_value& arg, const type_info* t) {
        if (!t || t->base_type == script_value_type::jai_any_type) return false;
        const script_value& d = arg.deref();
        const size_t ri = d.raw_storage_index();
        const bool object = ri == script_value::TYPEID_OBJECT || ri == script_value::TYPEID_SHARED_PTR;
        switch (t->base_type) {
            case script_value_type::jai_object_type: return !(object && d.get_type_info().get() == t);
            case script_value_type::jai_int_type:
            case script_value_type::jai_float_type:
            case script_value_type::jai_bool_type:
            case script_value_type::jai_char_type:
            case script_value_type::jai_string_type: return object;
            case script_value_type::jai_array_type: { const auto et = t->element_type(); return et && et->base_type == script_value_type::jai_object_type; }
            case script_value_type::jai_map_type: { const auto vt = t->value_type(); return vt && vt->base_type == script_value_type::jai_object_type; }
            default: return false;
        }
    }

    template<typename At>
    inline bool later_capture_candidate(size_t from, size_t argc, At&& at) {
        for (size_t j = from; j < argc; ++j) {
            if (capture_candidate(at(j), transient_read_marker::arg_capture_full)) return true;
        }
        return false;
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_ARG_CAPTURE_HPP__
