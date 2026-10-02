#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/container_enforce.hpp>

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
	inline checked_result<script_value> map_merge(const script_value& a_left, const script_value& a_right) {
		if (!a_left.is_map() || !a_right.is_map()) {
			return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "merge() requires two map arguments");
		}
		auto* engine = a_left.get_engine();
		const auto tag = a_left.get_type_info();
		const auto keyType = tag ? tag->key_type() : nullptr, valueType = tag ? tag->value_type() : nullptr;
		script_value result = script_value::make_map(keyType, valueType, engine);
		using Entry = std::pair<script_value, script_value>;
		std::vector<Entry> entries;
		entries.reserve(a_left.unchecked_as_map().size() + a_right.unchecked_as_map().size());
		// Decide the output before copying: an overwritten input value is only read.
		for (const auto& [key,value] : a_left.unchecked_as_map()) {
			if (a_right.unchecked_as_map().find(key) == a_right.unchecked_as_map().end()) { entries.emplace_back(key,value); }
		}
		for (const auto& [key,value] : a_right.unchecked_as_map()) { entries.emplace_back(key,value); }
		for (const auto& [key,value] : entries) {
			if (keyType && keyType->base_type != script_value_type::jai_any_type && keyType->base_type != script_value_type::jai_null_type && key.type() != keyType->base_type) {
				return checked_result<script_value>(make_error_code(runtime_error_code::map_key_type_mismatch), "Cannot use '{0}' as a key of type '{1}'", engine->symbolize(detail::container_value_type_name(key)), engine->symbolize(detail::type_info_display_name(keyType)));
			}
			script_value output = value.clone_element();
			if (valueType && valueType->base_type != script_value_type::jai_any_type && valueType->base_type != script_value_type::jai_null_type) {
				if (!detail::container_element_compatible(output, valueType, engine)) {
					return checked_result<script_value>(make_error_code(runtime_error_code::map_value_type_mismatch), "Cannot store '{0}' in map value of type '{1}'", engine->symbolize(detail::container_value_type_name(output)), engine->symbolize(detail::type_info_display_name(valueType)));
				}
				auto converted = detail::convert_container_element(engine, output, valueType);
				if (!converted) { return converted.error_value(); }
				output = std::move(converted.value());
			}
			(*result.unchecked_get_map_storage())[key.clone_element()] = std::move(output);
		}
		return result;
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


    }

} // namespace stdlib
} // namespace jai
