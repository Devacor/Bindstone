#pragma once

#ifndef __JAISCRIPT_DETAIL_TYPED_SLOT_HPP__
#define __JAISCRIPT_DETAIL_TYPED_SLOT_HPP__

// The script half of the typed-slot conversion table (class_definition.hpp
// convert_for_typed_slot_core owns the rows and the verdicts): a TRUE one-parameter
// constructor whose declared parameter takes the source DIRECTLY (no chaining, no to_X()
// on the way in), to_int/to_float/to_char/to_string, and to_bool(). Both backends and the
// field store run detail::convert_for_typed_slot verbatim; each site maps the verdict onto
// its own texts (docs/static_checking.md "Rule table"). A user conversion that fails
// propagates its own error.

#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/container_enforce.hpp>
#include <jaiscript/detail/return_conv.hpp>
#include <string>
#include <utility>
#include <vector>

namespace jai::detail {

    // Source spelling for rejection texts: detail::slot_source_type_name (class_definition.hpp,
    // next to the slot texts both wrappers raise through).

    // Target spelling for the parameter/return text: the interned canonical name, with
    // shared_ptr<T> spelled in full (its canonical name is the class alone).
    inline uint64_t slot_target_symbol(const type_info& target, string_symbolizer& symbols) {
        if (target.base_type == script_value_type::jai_shared_ptr_type) {
            return symbols.intern("shared_ptr<" + target.type_name + ">");
        }
        return target.id;
    }

    inline checked_result<slot_conversion> convert_typed_slot_script_rows_impl(slot_conversion pending, const script_value& value_in,
                                                                                const type_info_ptr& target_type, engine* eng) {
        using verdict = slot_conversion::verdict;
        const script_value& v = value_in.is_reference() ? value_in.deref() : value_in;
        const auto target = target_type->base_type;
        switch (pending.v) {
            case verdict::needs_to_bool: {
                // KEEP BYTE-PARALLEL with the backends' object_to_bool_via_method (class to_bool memo)
                auto view = resolve_slot_class(v, eng);
                bool truthy = true;
                if (view.def && !((view.def->is_script_class() || !view.def->extended_ever()) && !view.def->may_define_to_bool())) {
                    script_value method = view.def->get_method(eng->get_symbolizer()->intern("to_bool"), false);
                    if (!method.is_null() && !method.is_invalid() && method.is_function()) {
                        script_value bound = make_bound_method(v, method);
                        std::vector<script_value> no_args;
                        auto result = bound.as_function()(no_args);
                        if (!result) {
                            return result.error_value();   // to_bool()'s runtime error is the conversion's, like to_int()'s below
                        }
                        if (result.value().is_bool()) {
                            truthy = result.value().unchecked_as_bool();
                        }
                    }
                }
                return slot_conversion::converted_to(script_value(truthy, eng));
            }
            case verdict::needs_to_x: {
                const char* name = target == script_value_type::jai_int_type ? "to_int"
                                 : target == script_value_type::jai_float_type ? "to_float"
                                 : target == script_value_type::jai_char_type ? "to_char" : "to_string";
                auto view = resolve_slot_class(v, eng);
                script_value method = view.def ? view.def->get_method(eng->get_symbolizer()->intern(name), false)
                                               : script_value::make_invalid(eng);
                if (method.is_invalid() || method.is_null() || !method.is_function()) {
                    if (target == script_value_type::jai_string_type) {
                        return slot_conversion::converted_to(script_value(v.to_string(), eng));
                    }
                    if (target == script_value_type::jai_utf8_string_type) {
                        return slot_conversion::converted_to(script_value::make_utf8(v.to_string(), eng));
                    }
                    return slot_conversion::of(verdict::reject);
                }
                script_value bound = make_bound_method(v, method);
                std::vector<script_value> no_args;
                auto result = bound.as_function()(no_args);
                if (!result) {
                    return result.error_value();
                }
                if (auto* backend = eng->get_execution_backend(); backend && backend->is_unwinding()) {
                    // the method threw: the in-flight exception wins, the payload is never inspected
                    return slot_conversion::converted_to(std::move(result.value()));
                }
                // the result converts once more through the primitive rows, never through script again
                script_value produced = result.value().is_reference() ? result.value().deref() : std::move(result.value());
				auto converted = convert_for_typed_slot_core(produced, target_type, eng);
				if (!converted) { return converted.error_value(); }
				slot_conversion again = std::move(converted.value());
                if (again.v == verdict::identity || again.v == verdict::identity_exact) {
                    return slot_conversion::converted_to(std::move(produced));
                }
                if (again.v == verdict::converted) {
                    return again;
                }
                return slot_conversion::of(verdict::reject);
            }
            case verdict::needs_ctor: {
                const std::string& target_class_name = target_type->type_name;
                const size_t ri = v.raw_storage_index();
                const script_value_type src = v.current_type();
                const bool object_src = ri == script_value::TYPEID_OBJECT ||
                                        (ri == script_value::TYPEID_CPP_BOUND && src == script_value_type::jai_null_type);
                const verdict no = object_src ? verdict::reject_class : verdict::reject;
                auto conv_class_def = eng ? eng->get_class_definition(target_class_name) : nullptr;
                // Ruling: a defaulted-param ctor is NOT a converting ctor - implicit conversion needs a
                // TRUE 1-param ctor (a delegating overload opts in); host classes: the dispatcher decides
                if (conv_class_def && !has_true_single_param_conversion_ctor(conv_class_def)) {
                    return slot_conversion::of(no);
                }
                if (auto script_class = std::dynamic_pointer_cast<script_class_definition>(conv_class_def)) {
                    // exactly one user conversion: the ctor's declared parameter must take the source itself
                    std::string source_name;
                    class_definition* source_def = nullptr;
                    if (object_src) {
                        auto view = resolve_slot_class(v, eng);
                        if (view.def) {
                            source_name = view.def->get_name();
                            source_def = view.def;
                        }
                    }
                    bool matching = false;
                    for (const auto& ctor_ast : script_class->get_constructor_asts()) {
                        if (ctor_ast->parameters.size() != 1) continue;
                        const auto& param = ctor_ast->parameters[0];
                        if (!param.type || param.type->type_name.empty()) {
                            matching = true;
                            break;
                        }
                        if (object_src) {
                            if (param.type->type_name == source_name ||
                                (source_def && source_def->is_subtype_of(param.type->type_name))) {
                                matching = true;
                                break;
                            }
                        } else if (param.type->base_type == src ||
                                   (src == script_value_type::jai_int_type && param.type->base_type == script_value_type::jai_float_type) ||
                                   (src == script_value_type::jai_float_type && param.type->base_type == script_value_type::jai_int_type)) {
                            matching = true;
                            break;
                        }
                    }
                    if (!matching) {
                        return slot_conversion::of(no);
                    }
                }
                auto global = eng ? eng->get_global_environment() : nullptr;
                if (!global) {
                    return slot_conversion::of(no);
                }
                auto ctor_result = global->get(target_class_name);
                if (!ctor_result || !ctor_result.value().is_function()) {
                    return slot_conversion::of(no);
                }
                std::vector<script_value> ctor_args;
                ctor_args.push_back(value_in);
                auto result = ctor_result.value().as_function()(ctor_args);
                if (!result) {
                    if (result.error() == make_error_code(runtime_error_code::no_constructor_found)) {
                        return slot_conversion::of(no);
                    }
                    return result.error_value();
                }
                return slot_conversion::converted_to(std::move(result.value()));
            }
            default:
                return pending;
        }
    }

    // The full table: the script-free core, then the script rows where the core asked.
    inline checked_result<slot_conversion> convert_for_typed_slot(const script_value& value, const type_info_ptr& target_type, engine* eng) {
		auto result = convert_for_typed_slot_core(value, target_type, eng);
		if (!result) { return result.error_value(); }
		slot_conversion out = std::move(result.value());
        if (out.needs_script()) {
            return convert_typed_slot_script_rows_impl(std::move(out), value, target_type, eng);
        }
        return out;
    }

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_TYPED_SLOT_HPP__
