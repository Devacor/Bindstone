#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

namespace jai {
namespace stdlib {

    // Internal pair class for range-based for loop iteration
    struct script_pair {
        script_value first;
        script_value second;
        
        // Engine-aware constructor - will be used as default by class_builder
        explicit script_pair(std::weak_ptr<engine> eng) 
            : first(std::monostate{}, eng), 
              second(std::monostate{}, eng) {}
        
        // Constructor with values - script_values should already have engine references
        script_pair(const script_value& f, const script_value& s) 
            : first(f), second(s) {}
            
        // Special constructor that preserves references
        script_pair(const script_value& f, script_value&& s) 
            : first(f), second(std::move(s)) {}
            
        // Factory method for creating a pair with a reference to the second value
        static script_pair make_with_reference(const script_value& key, 
                                             script_value* value_ptr,
                                             const std::shared_ptr<environment>& env,
                                             std::weak_ptr<engine> eng) {
            return script_pair(key.clone(), 
                              script_value::make_reference(value_ptr, env, eng));
        }
    };

    // Merge two maps, with the second map's values overriding the first's on key collision
    inline script_value map_merge(const script_value& map1, const script_value& map2) {
        if (!map1.is_map() || !map2.is_map()) {
            throw runtime_error("merge() requires two map arguments");
        }

        // Create a new map with all entries from map1
        script_value result = map1.clone();
        auto& resultMap = const_cast<std::map<script_value, script_value>&>(result.as_map());
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
        bool is_string_concat = args[0].is_string();
        bool is_array_concat = args[0].is_array();

        if (!is_string_concat && !is_array_concat) {
            throw runtime_error("concatenate() requires arrays or strings");
        }

        // Verify all arguments are the same type
        for (size_t i = 1; i < args.size(); ++i) {
            if (is_string_concat && !args[i].is_string()) {
                throw runtime_error("concatenate() requires all arguments to be strings");
            }
            if (is_array_concat && !args[i].is_array()) {
                throw runtime_error("concatenate() requires all arguments to be arrays");
            }
        }

        // Handle string concatenation
        if (is_string_concat) {
            std::string result;
            for (const auto& arg : args) {
                result += arg.as<std::string>();
            }
            return script_value(result, args[0].get_engine_ref());
        }

        // Handle array concatenation
        if (is_array_concat) {
            script_value result = args[0].clone();
            auto& resultArray = const_cast<std::vector<script_value>&>(result.as_array());

            // Calculate total size needed
            size_t total_size = resultArray.size();
            for (size_t i = 1; i < args.size(); ++i) {
                total_size += args[i].as_array().size();
            }
            resultArray.reserve(total_size);

            // Append all arrays
            for (size_t i = 1; i < args.size(); ++i) {
                const auto& arr = args[i].as_array();
                for (const auto& elem : arr) {
                    resultArray.push_back(elem.clone());
                }
            }

            return result;
        }

        throw runtime_error("concatenate() requires arrays or strings");
    }

    inline void register_container_types(engine& eng) {
        auto engine_weak = eng.weak_from_this();

        // Register the pair type for map iteration
        class_builder<script_pair>(eng, "pair")
            .constructor<>()  // Will automatically use engine-aware constructor
            .constructor<script_value, script_value>()
            .property("first", &script_pair::first)
            .property("second", &script_pair::second)
            .build();

        // Register container utility functions using variadic approach
        eng.add_variadic_function("merge", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 2) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::argument_count_mismatch),
                    "merge() expects exactly 2 arguments, got " + std::to_string(args.size())
                );
            }
            return map_merge(args[0], args[1]);
        });

        eng.add_variadic_function("concatenate", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::argument_count_mismatch),
                    "concatenate() expects at least 1 argument"
                );
            }
            return concatenate_impl(args);
        });

        eng.add_variadic_function("append", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() < 1) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::argument_count_mismatch),
                    "append() expects at least 1 argument"
                );
            }
            return concatenate_impl(args);
        });
    }

} // namespace stdlib
} // namespace jai