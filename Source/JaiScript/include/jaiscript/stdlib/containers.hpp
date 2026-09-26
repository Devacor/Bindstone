#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>

namespace jai {
namespace stdlib {

    // Internal pair class for range-based for loop iteration
    struct script_pair {
        script_value first;
        script_value second;

        // Engine-aware constructor - will be used as default by dynamic_binder
        explicit script_pair(engine* eng)
            : first(std::monostate{}, eng),
              second(std::monostate{}, eng) {}

        // Constructor with values - script_values should already have engine references
        script_pair(const script_value& f, const script_value& s)
            : first(f), second(s) {}

        // Special constructor that preserves references
        script_pair(const script_value& f, script_value&& s)
            : first(f), second(std::move(s)) {}

    };

    // Merge two maps, with the second map's values overriding the first's on key collision
    inline script_value map_merge(const script_value& map1, const script_value& map2) {
        if (!map1.is_map() || !map2.is_map()) {
            throw runtime_error("merge() requires two map arguments");
        }

        // Create a new map with all entries from map1
        script_value result = map1.clone();
        auto& resultMap = const_cast<script_map&>(result.as_map());
        const auto& map2_data = map2.as_map();

        // Merge map2 into the result, overriding values for duplicate keys
        for (const auto& [key, value] : map2_data) {
            resultMap[key.clone()] = value.clone();
        }

        return result;
    }

    // Concatenate multiple arrays or multiple strings
    inline script_value concatenate_impl(const std::vector<script_value>& args) {
        if (args.empty()) {
            throw runtime_error("concatenate() requires at least one argument");
        }

        // Determine type from first argument
        bool is_string_concat = args[0].is_text();
        bool is_array_concat = args[0].is_array();

        if (!is_string_concat && !is_array_concat) {
            throw runtime_error("concatenate() requires arrays or strings");
        }

        // Verify all arguments are the same type
        for (size_t i = 1; i < args.size(); ++i) {
            if (is_string_concat && !args[i].is_text()) {
                throw runtime_error("concatenate() requires all arguments to be strings");
            }
            if (is_array_concat && !args[i].is_array()) {
                throw runtime_error("concatenate() requires all arguments to be arrays");
            }
        }

        // Handle string concatenation
        if (is_string_concat) {
            std::string result;
            bool utf8 = false;
            for (const auto& arg : args) {
                result += arg.deref().unchecked_text();
                utf8 = utf8 || arg.is_utf8_string();
            }
            return utf8 ? script_value::make_utf8(std::move(result), args[0].get_engine()) : script_value(result, args[0].get_engine());
        }

        // Handle array concatenation
        if (is_array_concat) {
            engine* eng = args[0].get_engine();
            size_t total_size = 0;
            for (const auto& arg : args) { total_size += arg.unchecked_array_node()->size(); }
            const script_array* first = args[0].unchecked_array_node();
            if (first->is_typed()) {
                // A typed first argument keeps its raw form when every appended element is
                // numeric; anything else lands in a fresh dynamic array (no tag lie)
                bool numeric_only = true;
                for (size_t i = 1; i < args.size() && numeric_only; ++i) {
                    const script_array* node = args[i].unchecked_array_node();
                    if (node->is_typed()) { continue; }
                    for (const script_value& elem : node->values()) {
                        const size_t idx = elem.deref().raw_storage_index();
                        if (idx != script_value::TYPEID_INT && idx != script_value::TYPEID_FLOAT) { numeric_only = false; break; }
                    }
                }
                if (numeric_only) {
                    script_value result = args[0].clone();
                    script_array* out = const_cast<script_array*>(result.unchecked_array_node());
                    out->reserve(total_size);
                    for (size_t i = 1; i < args.size(); ++i) {
                        args[i].unchecked_array_node()->for_each(eng, [&](const script_value& elem) { out->push(elem.deref()); });
                    }
                    return result;
                }
                script_value result = script_value::make_array(nullptr, eng);
                auto& resultArray = const_cast<std::vector<script_value>&>(result.as_array());
                resultArray.reserve(total_size);
                for (const auto& arg : args) {
                    arg.unchecked_array_node()->for_each(eng, [&](const script_value& elem) { resultArray.push_back(elem.clone()); });
                }
                return result;
            }
            script_value result = args[0].clone();
            auto& resultArray = const_cast<std::vector<script_value>&>(result.as_array());
            resultArray.reserve(total_size);
            for (size_t i = 1; i < args.size(); ++i) {
                args[i].unchecked_array_node()->for_each(eng, [&](const script_value& elem) { resultArray.push_back(elem.clone()); });
            }
            return result;
        }

        throw runtime_error("concatenate() requires arrays or strings");
    }

    inline void register_container_types(engine& eng) {
        // Register the pair type for map iteration
        dynamic_binder<script_pair>(eng, "pair")
            .constructor<>()  // Will automatically use engine-aware constructor
            .constructor<script_value, script_value>()
            .property("first", &script_pair::first)
            .property("second", &script_pair::second)
            .build();

        // Register container utility functions using variadic approach
        eng.add_variadic_function("merge", [](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 2) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::argument_count_mismatch),
                    "merge() expects exactly 2 arguments, got {0}",
                    static_cast<uint64_t>(args.size())
                );
            }
            return map_merge(args[0], args[1]);
        });

        eng.add_variadic_function("concatenate", [](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::argument_count_mismatch),
                    "concatenate() expects at least 1 argument, got {0}",
                    static_cast<uint64_t>(args.size())
                );
            }
            return concatenate_impl(args);
        });

        eng.add_variadic_function("append", [](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::argument_count_mismatch),
                    "append() expects at least 1 argument, got {0}",
                    static_cast<uint64_t>(args.size())
                );
            }
            return concatenate_impl(args);
        });
    }

} // namespace stdlib
} // namespace jai