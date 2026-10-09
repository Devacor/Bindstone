#pragma once

#ifndef __JAISCRIPT_DETAIL_FUNCTION_OVERLOADS_HPP__
#define __JAISCRIPT_DETAIL_FUNCTION_OVERLOADS_HPP__

// Program-scope and namespace function overloads (Dev ruling 2026-10-02): declarations of one name coexist as in C++, a
// declaration of the same signature replaces, and a unit run again replaces or drops only what its earlier run declared. A call
// takes the one scorer's best (core/overload_resolution.hpp); a tie on every key raises. Shared verbatim by both backends.

#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/core/overload_resolution.hpp>

namespace jai::detail {

    // One signature: the same parameter count and, at each position, the same declared type (an untyped slot is var) and the same
    // reference flag (C++: T& and T are two signatures). Defaults are not part of it.
    inline bool same_signature(const std::vector<parameter>& a, const std::vector<parameter>& b) {
        if (a.size() != b.size()) { return false; }
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].is_reference != b[i].is_reference) { return false; }
            const bool untyped_a = !a[i].type || a[i].type->base_type == script_value_type::jai_any_type;
            const bool untyped_b = !b[i].type || b[i].type->base_type == script_value_type::jai_any_type;
            if (untyped_a != untyped_b) { return false; }
            if (!untyped_a && a[i].type.get() != b[i].type.get() && a[i].type->canonical_name() != b[i].type->canonical_name()) { return false; }
        }
        return true;
    }

    // An earlier run of the unit that is declaring now: its declarations the new run does not repeat are dropped
    inline bool earlier_run(unit_stamp declared, unit_stamp now) { return declared.id == now.id && declared.seq != now.seq; }

    inline const script_callable* script_callable_of(const script_value& v, engine* eng) {
        if (v.raw_storage_index() != script_value::TYPEID_FUNCTION) { return nullptr; }
        const auto* thunk = v.unchecked_as_function().target<script_callable_thunk>();
        return thunk && thunk->eng == eng ? &thunk->payload : nullptr;
    }

    // The value a function declaration defines its name to in `env`: the function itself, or the overload set it joins with the
    // declarations of other signatures `env` already holds under the name (a variable, a lambda or a host callable is replaced)
    inline script_value declare_function_overload(environment& env, uint64_t name_id, const std::shared_ptr<script_defined_function>& fn,
                                                  script_value value, engine* eng) {
        const script_value* existing = env.get_own_value_ptr(name_id);
        const script_callable* held = existing ? script_callable_of(*existing, eng) : nullptr;
        if (!held) { return value; }
        auto set = std::make_shared<function_overload_set>();
        set->name_id = name_id;
        auto keep = [&](const std::shared_ptr<script_defined_function>& member, const script_value& member_value) {
            if (member == fn || earlier_run(member->unit, fn->unit) || same_signature(member->parameters(), fn->parameters())) { return; }
            set->members.push_back(member);
            set->values.push_back(member_value);
        };
        if (held->kind == script_callable::kind_type::function && held->fn && held->fn->declared) {
            keep(held->fn, *existing);
        } else if (held->kind == script_callable::kind_type::overload_set && held->overloads) {
            for (size_t i = 0; i < held->overloads->members.size(); ++i) { keep(held->overloads->members[i], held->overloads->values[i]); }
        }
        if (set->members.empty()) { return value; }
        set->members.push_back(fn);
        set->values.push_back(std::move(value));
        script_callable payload;
        payload.kind = script_callable::kind_type::overload_set;
        payload.overloads = std::move(set);
        return script_value::make_function(script_function(script_callable_thunk{eng, std::move(payload)}), eng);
    }

    // The member a call binds: the scorer's best, else the one member the arity admits (its parameters convert or raise their own
    // text); a tie raises, and no viable member is the overload miss
    inline checked_result<const script_value*> select_overload_member(const function_overload_set& set, const script_value* args, size_t argc, engine* eng) {
        const auto params_of = [](const std::shared_ptr<script_defined_function>& f) -> const std::vector<parameter>& { return f->parameters(); };
        const auto verdict = score_overloads(set.members, params_of, args, argc);
        if (verdict.rival) [[unlikely]] {
            return ambiguous_call(set.name_id, (*verdict.best)->parameters(), (*verdict.rival)->parameters(), args, argc, eng);
        }
        if (verdict.best) { return &set.values[static_cast<size_t>(verdict.best - set.members.data())]; }
        const script_value* sole = nullptr;
        for (size_t i = 0; i < set.members.size(); ++i) {
            if (!arity_accepts(set.members[i]->parameters(), argc)) { continue; }
            if (sole) { sole = nullptr; break; }
            sole = &set.values[i];
        }
        if (sole) { return sole; }
        return overload_miss(make_error_code(runtime_error_code::argument_count_mismatch), set.name_id, args, argc, eng, false);
    }

    // The binding modes the members admitting argc arguments give argument i (detail::arg_binding bits)
    inline uint8_t overload_set_binding(const function_overload_set& set, size_t i, size_t argc) {
        uint8_t result = 0;
        for (const auto& m : set.members) {
            if (argc != SIZE_MAX && !arity_accepts(m->parameters(), argc)) { continue; }
            const auto& ps = m->parameters();
            if (i >= ps.size()) { result |= 1; continue; }
            const parameter& p = ps[i];
            if (p.is_reference) { result |= 4; }
            else if (p.type && (p.type->base_type == script_value_type::jai_shared_ptr_type || p.type->base_type == script_value_type::jai_weak_ptr_type)) { result |= 2; }
            else { result |= 1; }
        }
        return result;
    }

    // A namespace function declaration (both backends' namespace declaration): an earlier run of the declaring unit drops what it
    // declared; false when a declaration of the same signature stays (the caller raises unless `override`), which is erased first
    // when the new one overrides
    inline bool admit_namespace_function(std::vector<std::shared_ptr<function_decl>>& overloads, const function_decl& fn) {
        std::erase_if(overloads, [&](const std::shared_ptr<function_decl>& o) { return earlier_run(o->unit, fn.unit); });
        for (auto it = overloads.begin(); it != overloads.end(); ++it) {
            if (!same_signature((*it)->parameters, fn.parameters)) { continue; }
            if (!fn.is_override) { return false; }
            overloads.erase(it);
            break;
        }
        return true;
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_FUNCTION_OVERLOADS_HPP__
