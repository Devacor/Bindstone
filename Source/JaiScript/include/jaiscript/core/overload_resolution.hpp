#pragma once

#ifndef __JAISCRIPT_CORE_OVERLOAD_RESOLUTION_HPP__
#define __JAISCRIPT_CORE_OVERLOAD_RESOLUTION_HPP__

#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/detail/ast.hpp>
#include <climits>
#include <memory>
#include <optional>
#include <vector>

namespace jai {

// Trailing-default arity window (the parser enforces defaults are trailing): a callable
// with N params of which K have defaults accepts N-K..N arguments. The binding layer on
// BOTH backends already evaluates missing defaults; these gates decide VIABILITY only -
// used uniformly by method/ctor/delegation/namespace resolution so every callable kind
// honors defaults the way free functions always have.
inline size_t required_parameter_count(const std::vector<parameter>& params) {
    size_t required = 0;
    for (const auto& p : params) {
        if (!p.default_value) { ++required; } else { break; }
    }
    return required;
}

inline bool arity_accepts(const std::vector<parameter>& params, size_t argc) {
    return argc >= required_parameter_count(params) && argc <= params.size();
}

// A unary operator the class chain declares (Dev ruling 2026-09-30): a script overload of the name that takes no argument
// (the class or a base), or a host class's method of that name (its binding decides the arity)
inline bool declares_unary_operator(const class_definition* def, uint64_t name_id) {
    bool script_declared = false;
    for (const class_definition* c = def; c; c = c->get_parent().get()) {
        const auto& overloads = c->script_method_overloads();
        if (auto it = overloads.find(name_id); it != overloads.end()) {
            script_declared = true;
            for (const auto& d : it->second) { if (arity_accepts(d->parameters, 0)) { return true; } }
        }
    }
    return !script_declared && def && def->get_method(name_id, false).is_function();
}

// A typed parameter takes the argument with no mismatch (the scorer's rows: the class or a subclass, the same kind, int<->float)
inline bool parameter_admits(const parameter& p, const script_value& raw) {
    if (!p.type || p.type->type_name.empty() || p.type->type_name == "any" || p.type->base_type == script_value_type::jai_any_type) { return true; }
    const script_value& arg = raw.deref();
    const auto kind = arg.type();
    if (kind == script_value_type::jai_object_type || kind == script_value_type::jai_shared_ptr_type) {
        auto instance = const_cast<script_value&>(arg).get_class_instance();
        auto def = instance ? instance->get_class_definition() : nullptr;
        return def && (def->get_name() == p.type->type_name || def->is_subtype_of(p.type->type_name));
    }
    return kind == p.type->base_type ||
           (kind == script_value_type::jai_int_type && p.type->base_type == script_value_type::jai_float_type) ||
           (kind == script_value_type::jai_float_type && p.type->base_type == script_value_type::jai_int_type);
}

// The chain's binary operator `name_id` is a candidate for `arg`: the nearest script declaration has a one-argument overload
// that takes it (C++: a candidate the argument cannot reach is not viable), or a host class's method of that name
inline bool binary_operator_accepts(const class_definition* def, uint64_t name_id, const script_value& arg) {
    for (const class_definition* c = def; c; c = c->get_parent().get()) {
        const auto& overloads = c->script_method_overloads();
        if (auto it = overloads.find(name_id); it != overloads.end()) {
            for (const auto& d : it->second) {
                if (arity_accepts(d->parameters, 1) && !d->parameters.empty() && parameter_admits(d->parameters[0], arg)) { return true; }
            }
            return false;
        }
    }
    return def && def->get_method(name_id, false).is_function();
}

// A reversed comparison (Dev ruling 2026-10-01): a < b answered by b's <=> is (b <=> a) > 0, and so on
inline token_type reversed_comparison(token_type op) {
    switch (op) {
        case token_type::less: return token_type::greater;
        case token_type::less_equal: return token_type::greater_equal;
        case token_type::greater: return token_type::less;
        default: return token_type::less_equal;
    }
}

// 0 <=> r for a reversed a <=> b answered by b's <=>, in the builtin <=> order (signed zeros equivalent, NaN last); nullopt
// for a result that is no number
inline std::optional<script_int> reversed_spaceship_sign(const script_value& result) {
    const script_value& r = result.deref();
    if (r.is_int()) { const script_int v = r.as_int(); return v < 0 ? 1 : v > 0 ? -1 : 0; }
    if (r.is_float()) { const double v = r.as_float(); return v != v ? -1 : v < 0 ? 1 : v > 0 ? -1 : 0; }
    return std::nullopt;
}

// C++20 comparison rewriting (Dev ruling 2026-10-01): with no operator< in the chain, a < b is (a <=> b) < 0, and so on for
// <= > >=; the left operand's operator<=> result compares with zero (a NaN compares false), anything but a number is no answer
inline std::optional<bool> spaceship_rewritten(token_type op, const script_value& result) {
    const script_value& r = result.deref();
    double cmp = 0.0;
    if (r.is_int()) { cmp = static_cast<double>(r.as_int()); }
    else if (r.is_float()) { cmp = r.as_float(); }
    else { return std::nullopt; }
    switch (op) {
        case token_type::less: return cmp < 0;
        case token_type::less_equal: return cmp <= 0;
        case token_type::greater: return cmp > 0;
        case token_type::greater_equal: return cmp >= 0;
        default: return std::nullopt;
    }
}

inline bool object_declares_method(const script_value& arg, std::string_view name) {
    auto instance = const_cast<script_value&>(arg).get_class_instance();
    auto def = instance ? instance->get_class_definition() : nullptr;
    engine* eng = def ? def->get_engine() : nullptr;
    if (!eng) { return false; }
    const auto id = eng->get_symbolizer()->find(name);
    return id && def->defines_method(*id);
}

// A one-parameter script constructor of the parameter's class takes the argument (the table's converting row, no chaining;
// defined beside script_class_definition)
inline bool converting_constructor_takes(const script_value& arg, const type_info& target);

// The assignment table's rows past the exact type that a by-value parameter takes (docs/static_checking.md), as overload costs:
// int<->float and bool to a number 1 (a conversion, ahead of a var parameter); a to_int/to_float/to_char method and the string
// and bool rows 2 and a converting constructor 3 (k_loose_conversion and up: after a var parameter, which takes the value as it
// is); 0 = not viable. Among several overloads a string never makes a bool parameter viable (Dev ruling 2026-10-02).
inline constexpr size_t k_loose_conversion = 2;
inline size_t table_conversion_cost(const script_value& arg, const parameter& param, bool in_set) {
    using t = script_value_type;
    const t to = param.type->base_type;
    const t from = arg.type();
    const bool number_to = to == t::jai_int_type || to == t::jai_float_type;
    if (number_to && (from == t::jai_int_type || from == t::jai_float_type)) { return 1; }
    if (param.is_reference) { return 0; }
    switch (to) {
        case t::jai_int_type:
        case t::jai_float_type:
            if (from == t::jai_bool_type) { return 1; }
            return from == t::jai_object_type && object_declares_method(arg, to == t::jai_int_type ? "to_int" : "to_float") ? 2 : 0;
        case t::jai_char_type:
            return from == t::jai_object_type && object_declares_method(arg, "to_char") ? 2 : 0;
        case t::jai_bool_type:
            return in_set && (from == t::jai_string_type || from == t::jai_utf8_string_type) ? 0 : 2;
        case t::jai_string_type:
        case t::jai_utf8_string_type:
            return 2;
        case t::jai_object_type:
            return converting_constructor_takes(arg, *param.type) ? 3 : 0;
        default:
            return 0;
    }
}

// Shared script-overload scorer used by method, static-method and constructor dispatch.
// Priority: 1) all typed + exact match, 2) all typed + conversions (fewest first)
//           3) var/any parameters (fallback; a typed parameter the argument does not
//           match ranks the candidate below every one it matches, then fewer var
//           parameters win: typed exact > conversion > var)
// Same-score tie: the candidate using FEWER defaults wins (an exact-arity overload
// beats one that must fill trailing defaults), then fewest conversions.
// Returns nullptr when no overload is viable for these arguments.
// Span core (call-arg windows: the vm resolves against the caller's stack slice
// without collecting a vector); the vector overload below forwards.
inline const std::shared_ptr<function_decl>* pick_best_overload_ref(
    const std::vector<std::shared_ptr<function_decl>>& overloads,
    const script_value* args, size_t argc) {
    const std::shared_ptr<function_decl>* best_ast = nullptr;
    int best_score = INT_MAX;  // Lower is better: 0=exact, 1-N=conversions, 1000=has_var
    size_t best_mismatches = SIZE_MAX;
    size_t best_var_count = SIZE_MAX;
    size_t best_defaults_used = SIZE_MAX;
    size_t best_conversion_count = SIZE_MAX;

    for (const auto& overload_ast : overloads) {
        if (!arity_accepts(overload_ast->parameters, argc)) {
            continue;
        }
        const size_t defaults_used = overload_ast->parameters.size() - argc;

        bool all_typed = true;       // All parameters have explicit types
        bool all_exact = true;       // All typed parameters match exactly
        bool all_convertible = true; // All typed parameters can be converted
        size_t conversion_count = 0; // Number of conversions needed
        bool has_var = false;        // Has any var/any parameters
        size_t var_count = 0;        // var/any parameters
        size_t mismatches = 0;       // typed parameters the argument matches neither exactly nor by conversion
        bool loose = false;          // a conversion that ranks after a var parameter

        for (size_t i = 0; i < argc; ++i) {
            const auto& param = overload_ast->parameters[i];
            // Lvalue call args (arr[i], obj.field) arrive as references: match their targets
            const script_value& arg = args[i].deref();
            // var keyword creates type_info with type_name="any" and base_type=jai_any_type
            bool has_explicit_type = param.type &&
                                    !param.type->type_name.empty() &&
                                    param.type->type_name != "any" &&
                                    param.type->base_type != script_value_type::jai_any_type;
            if (has_explicit_type) {
                auto arg_type = arg.type();
                auto convert_or_mismatch = [&]() {
                    all_exact = false;
                    if (const size_t cost = table_conversion_cost(arg, param, overloads.size() > 1)) {
                        conversion_count += cost;
                        loose = loose || cost >= k_loose_conversion;
                    } else {
                        all_convertible = false;
                        ++mismatches;
                    }
                };

                if (arg_type == script_value_type::jai_object_type ||
                    arg_type == script_value_type::jai_shared_ptr_type) {
                    // For objects and shared_ptr, check class name with inheritance support
                    auto instance = const_cast<script_value&>(arg).get_class_instance();
                    if (instance) {
                        auto arg_class_def = instance->get_class_definition();
                        if (arg_class_def) {
                            if (arg_class_def->get_name() == param.type->type_name) {
                                if (arg_type == script_value_type::jai_shared_ptr_type &&
                                    param.type->base_type == script_value_type::jai_object_type) {
                                    // shared_ptr<T> -> T requires unwrapping, count as conversion
                                    all_exact = false;
                                    conversion_count++;
                                }
                            } else if (arg_class_def->is_subtype_of(param.type->type_name)) {
                                // Derived type - allow but rank as an implicit conversion
                                all_exact = false;
                                conversion_count++;
                            } else {
                                convert_or_mismatch();
                            }
                        } else {
                            // Can't get class def, fall back to name comparison
                            if (instance->get_class_name() != param.type->type_name) {
                                convert_or_mismatch();
                            }
                        }
                    } else {
                        convert_or_mismatch();
                    }
                } else if (arg_type != param.type->base_type) {
                    convert_or_mismatch();
                }
            } else {
                // Parameter has no type (var/auto) - accepts anything
                all_typed = false;
                has_var = true;
                ++var_count;
            }
        }

        int score;
        if (!all_convertible && !has_var) {
            continue;
        } else if (all_typed && all_exact) {
            score = 0;
        } else if (loose) {
            score = static_cast<int>(2000 + conversion_count);
        } else if (all_typed && all_convertible) {
            score = static_cast<int>(1 + conversion_count);
        } else if (has_var) {
            score = 1000;
        } else {
            continue;
        }

        bool better;
        if (mismatches != best_mismatches) {
            better = mismatches < best_mismatches;
        } else if (score != best_score) {
            better = score < best_score;
        } else if (var_count != best_var_count) {
            better = var_count < best_var_count;
        } else if (defaults_used != best_defaults_used) {
            better = defaults_used < best_defaults_used;
        } else {
            better = conversion_count < best_conversion_count;
        }
        if (better) {
            best_ast = &overload_ast;
            best_score = score;
            best_mismatches = mismatches;
            best_var_count = var_count;
            best_defaults_used = defaults_used;
            best_conversion_count = conversion_count;
        }
    }

    return best_ast;
}

inline std::shared_ptr<function_decl> pick_best_overload(
    const std::vector<std::shared_ptr<function_decl>>& overloads,
    const script_value* args, size_t argc) {
    const auto* best = pick_best_overload_ref(overloads, args, argc);
    return best ? *best : nullptr;
}

inline std::shared_ptr<function_decl> pick_best_overload(
    const std::vector<std::shared_ptr<function_decl>>& overloads,
    const std::vector<script_value>& args) {
    return pick_best_overload(overloads, args.data(), args.size());
}

// Constructor and method selection when the scorer finds no viable overload: the one overload the arity admits binds (its parameter
// converts the argument its slot's way or raises its own text); with several, none does and the caller raises the overload miss
inline const std::shared_ptr<function_decl>* sole_arity_candidate(const std::vector<std::shared_ptr<function_decl>>& overloads,
                                                                  const script_value* args, size_t argc) {
    const std::shared_ptr<function_decl>* sole = nullptr;
    for (const auto& o : overloads) {
        if (!arity_accepts(o->parameters, argc)) { continue; }
        if (sole) { return nullptr; }
        sole = &o;
    }
    return sole;
}

inline std::shared_ptr<function_decl> sole_arity_candidate(const std::vector<std::shared_ptr<function_decl>>& overloads,
                                                           const std::vector<script_value>& args) {
    const auto* sole = sole_arity_candidate(overloads, args.data(), args.size());
    return sole ? *sole : nullptr;
}

// Cache-aware instance-method overload resolution over method_overloads_.
// Cache rules preserved exactly: entries keyed (name_id, arity); read only when
// there is more than one overload; written only when more than one overload exists
// AND exactly one matches this arity (same-arity overloads depend on argument types).
// Span core; the vector overload forwards (call-arg windows resolve on the stack slice).
inline const std::shared_ptr<function_decl>* class_definition::resolve_method_overload_core(
    uint64_t name_id, const script_value* args, size_t argc, std::error_code& ec) const {
    auto it = method_overloads_.find(name_id);
    if (it == method_overloads_.end() || it->second.empty()) {
        ec = make_error_code(runtime_error_code::member_not_found);
        return nullptr;
    }

    const auto& overloads = it->second;

    // one declaration binds by arity and converts like a free function's parameters (the one table)
    if (overloads.size() == 1) {
        if (arity_accepts(overloads[0]->parameters, argc)) { return &overloads[0]; }
        ec = make_error_code(runtime_error_code::argument_count_mismatch);
        return nullptr;
    }

    {
        overload_cache_key cache_key{name_id, argc};
        auto cache_it = overload_resolution_cache_.find(cache_key);
        if (cache_it != overload_resolution_cache_.end()) {
            return &cache_it->second;
        }
    }

    const auto* best_ast = pick_best_overload_ref(overloads, args, argc);
    if (!best_ast) { best_ast = sole_arity_candidate(overloads, args, argc); }
    if (!best_ast) {
        ec = make_error_code(runtime_error_code::argument_count_mismatch);
        return nullptr;
    }

    {
        size_t arity_match_count = 0;
        for (const auto& o : overloads) {
            if (arity_accepts(o->parameters, argc)) {
                ++arity_match_count;
            }
        }
        if (arity_match_count == 1) {
            overload_cache_key cache_key{name_id, argc};
            return &overload_resolution_cache_.insert_or_assign(cache_key, *best_ast).first->second;
        }
    }

    return best_ast;
}

inline checked_result<const std::shared_ptr<function_decl>*> class_definition::resolve_method_overload_ref(
    uint64_t name_id, const script_value* args, size_t argc) const {
    std::error_code ec;
    const auto* best = resolve_method_overload_core(name_id, args, argc, ec);
    if (best) {
        return best;
    }
    if (ec == make_error_code(runtime_error_code::member_not_found)) {
        return checked_result<const std::shared_ptr<function_decl>*>(ec, "No overload of '{0}' exists", name_id);
    }
    return method_overload_miss(ec, name_id, args, argc);
}

inline error_propagator class_definition::method_overload_miss(std::error_code ec, uint64_t name_id, const script_value* args, size_t argc) const {
    auto it = method_overloads_.find(name_id);
    if (it != method_overloads_.end() && it->second.size() == 1 && !arity_accepts(it->second.front()->parameters, argc)) {
        return detail::method_arity_miss(name_id, it->second.front()->parameters.size(), argc, get_engine());
    }
    return detail::overload_miss(ec, name_id, args, argc, get_engine());
}

inline checked_result<std::shared_ptr<function_decl>> class_definition::resolve_method_overload(
    uint64_t name_id, const script_value* args, size_t argc) const {
    std::error_code ec;
    const auto* best = resolve_method_overload_core(name_id, args, argc, ec);
    if (best) {
        return *best;
    }
    if (ec == make_error_code(runtime_error_code::member_not_found)) {
        return checked_result<std::shared_ptr<function_decl>>(ec, "No overload of '{0}' exists", name_id);
    }
    return method_overload_miss(ec, name_id, args, argc);
}

inline checked_result<std::shared_ptr<function_decl>> class_definition::resolve_method_overload(
    uint64_t name_id, const std::vector<script_value>& args) const {
    return resolve_method_overload(name_id, args.data(), args.size());
}

} // namespace jai

#endif // __JAISCRIPT_CORE_OVERLOAD_RESOLUTION_HPP__
