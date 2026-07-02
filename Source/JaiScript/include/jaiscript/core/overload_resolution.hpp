#pragma once

#ifndef __JAISCRIPT_CORE_OVERLOAD_RESOLUTION_HPP__
#define __JAISCRIPT_CORE_OVERLOAD_RESOLUTION_HPP__

#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/detail/ast.hpp>
#include <climits>
#include <memory>
#include <vector>

namespace jai {

// Shared script-overload scorer used by instance-method and static-method dispatch.
// Priority: 1) all typed + exact match, 2) all typed + conversions (fewest first)
//           3) var/any parameters (fallback)
// Returns nullptr when no overload is viable for these arguments.
inline std::shared_ptr<function_decl> pick_best_overload(
    const std::vector<std::shared_ptr<function_decl>>& overloads,
    const std::vector<script_value>& args) {
    std::shared_ptr<function_decl> best_ast;
    int best_score = INT_MAX;  // Lower is better: 0=exact, 1-N=conversions, 1000=has_var
    size_t best_conversion_count = SIZE_MAX;

    for (const auto& overload_ast : overloads) {
        if (overload_ast->parameters.size() != args.size()) {
            continue;
        }

        bool all_typed = true;       // All parameters have explicit types
        bool all_exact = true;       // All typed parameters match exactly
        bool all_convertible = true; // All typed parameters can be converted
        size_t conversion_count = 0; // Number of conversions needed
        bool has_var = false;        // Has any var/any parameters

        for (size_t i = 0; i < args.size(); ++i) {
            const auto& param = overload_ast->parameters[i];
            // var keyword creates type_info with type_name="any" and base_type=jai_any_type
            bool has_explicit_type = param.type &&
                                    !param.type->type_name.empty() &&
                                    param.type->type_name != "any" &&
                                    param.type->base_type != script_value_type::jai_any_type;
            if (has_explicit_type) {
                auto arg_type = args[i].type();

                if (arg_type == script_value_type::jai_object_type ||
                    arg_type == script_value_type::jai_shared_ptr_type) {
                    // For objects and shared_ptr, check class name with inheritance support
                    auto instance = const_cast<script_value&>(args[i]).get_class_instance();
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
                                all_exact = false;
                                all_convertible = false;
                            }
                        } else {
                            // Can't get class def, fall back to name comparison
                            if (instance->get_class_name() != param.type->type_name) {
                                all_exact = false;
                                all_convertible = false;
                            }
                        }
                    } else {
                        all_exact = false;
                        all_convertible = false;
                    }
                } else {
                    // For primitives, check base type
                    if (arg_type != param.type->base_type) {
                        all_exact = false;
                        bool is_numeric_conversion =
                            (arg_type == script_value_type::jai_int_type &&
                             param.type->base_type == script_value_type::jai_float_type) ||
                            (arg_type == script_value_type::jai_float_type &&
                             param.type->base_type == script_value_type::jai_int_type);
                        if (is_numeric_conversion) {
                            conversion_count++;
                        } else {
                            all_convertible = false;
                        }
                    }
                }
            } else {
                // Parameter has no type (var/auto) - accepts anything
                all_typed = false;
                has_var = true;
            }
        }

        int score;
        if (!all_convertible && !has_var) {
            continue;
        } else if (all_typed && all_exact) {
            score = 0;
        } else if (all_typed && all_convertible) {
            score = static_cast<int>(1 + conversion_count);
        } else if (has_var) {
            score = 1000;
        } else {
            continue;
        }

        if (score < best_score || (score == best_score && conversion_count < best_conversion_count)) {
            best_ast = overload_ast;
            best_score = score;
            best_conversion_count = conversion_count;
        }
    }

    return best_ast;
}

// Cache-aware instance-method overload resolution over method_overloads_.
// Cache rules preserved exactly: entries keyed (name_id, arity); read only when
// there is more than one overload; written only when more than one overload exists
// AND exactly one matches this arity (same-arity overloads depend on argument types).
inline checked_result<std::shared_ptr<function_decl>> class_definition::resolve_method_overload(
    uint64_t name_id, const std::vector<script_value>& args) const {
    auto it = method_overloads_.find(name_id);
    if (it == method_overloads_.end() || it->second.empty()) {
        return checked_result<std::shared_ptr<function_decl>>(
            make_error_code(runtime_error_code::member_not_found), "No method overloads found", name_id);
    }

    const auto& overloads = it->second;

    if (overloads.size() > 1) {
        overload_cache_key cache_key{name_id, args.size()};
        auto cache_it = overload_resolution_cache_.find(cache_key);
        if (cache_it != overload_resolution_cache_.end()) {
            return cache_it->second;
        }
    }

    auto best_ast = pick_best_overload(overloads, args);
    if (!best_ast) {
        return checked_result<std::shared_ptr<function_decl>>(
            make_error_code(runtime_error_code::argument_count_mismatch), "No matching overload found", name_id);
    }

    if (overloads.size() > 1) {
        size_t arity_match_count = 0;
        for (const auto& o : overloads) {
            if (o->parameters.size() == args.size()) {
                ++arity_match_count;
            }
        }
        if (arity_match_count == 1) {
            overload_cache_key cache_key{name_id, args.size()};
            overload_resolution_cache_[cache_key] = best_ast;
        }
    }

    return best_ast;
}

} // namespace jai

#endif // __JAISCRIPT_CORE_OVERLOAD_RESOLUTION_HPP__
