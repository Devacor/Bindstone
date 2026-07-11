#pragma once

#ifndef __JAISCRIPT_CORE_ENGINE_IMPL_HPP__
#define __JAISCRIPT_CORE_ENGINE_IMPL_HPP__

#include "engine.hpp"
#include "conversion_registry.hpp"
#include "class_definition.hpp"

namespace jai {

namespace detail {
	// Sig decomposition for convert_script_function (declared in function_binder.hpp)
	template<typename Sig> struct script_function_wrapper;

	template<typename R, typename... Args>
	struct script_function_wrapper<R(Args...)> {
		static std::function<R(Args...)> wrap(script_value fn) {
			return [fn = std::move(fn)](Args... args) -> R {
				engine* eng = fn.get_engine();
				if (!eng || !fn.is_function()) {
					if constexpr (!std::is_void_v<R>) { return R{}; } else { return; }
				}
				engine::external_call_guard scope(eng);
				// No error-value channel through std::function and throwing into game
				// callsites (frame hooks, signal emits) is unsafe - report and default.
				try {
					std::vector<script_value> values;
					values.reserve(sizeof...(Args));
					(values.push_back(eng->make_value(args)), ...);
					auto result = fn.as_function()(values);
					if (!result) {
						eng->report_script_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
						if constexpr (!std::is_void_v<R>) { return R{}; } else { return; }
					}
					if constexpr (!std::is_void_v<R>) {
						return result.value().template as<R>();
					}
				} catch (const std::exception& e) {
					eng->report_script_error(e.what());
					if constexpr (!std::is_void_v<R>) { return R{}; } else { return; }
				}
			};
		}
	};
}

template<typename Sig>
std::function<Sig> convert_script_function(const script_value& fn) {
	return detail::script_function_wrapper<Sig>::wrap(fn);
}

template<typename T>
std::string engine::get_registered_name() const {
    auto class_def = get_class_definition_by_type(std::type_index(typeid(T)));
    if (class_def) {
        return class_def->get_name();
    }
    return typeid(T).name();
}

template<typename T>
script_value engine::make_object(std::shared_ptr<T> data) {
    std::string type_name = get_registered_name<T>();

    try {
        auto class_def = get_class_definition_by_type(std::type_index(typeid(T)));
        if (class_def) {
            auto instance = class_def->create_instance();

            uint64_t cpp_object_field_id = symbolize(class_constants::CPP_OBJECT_FIELD);
            instance->set_field(cpp_object_field_id,
                script_value::make_cpp_object(type_name, class_def->get_type_id(), std::static_pointer_cast<void>(data), this));

            return script_value::make_object(type_name, instance, this);
        }
    } catch (...) {
        // Class not registered, fall back to raw object storage
    }

    // Fallback: store as raw C++ object (properties won't work)
    auto type_id = get_symbolizer()->intern(type_name);
    return script_value::make_cpp_object(type_name, type_id, std::static_pointer_cast<void>(data), this);
}

template<typename T>
script_value convert_custom_type_with_registry(const T& t, engine* eng)
    requires std::is_copy_constructible_v<std::remove_const_t<T>>
{
    using NonConstT = std::remove_const_t<T>;
    auto registry = eng->get_conversion_registry();
    if (registry && registry->template has_conversion<NonConstT>()) {
        return registry->template convert_to_script<NonConstT>(t);
    }

    auto sharedObj = std::make_shared<NonConstT>(t);
    if (!eng) {
        throw runtime_error("Engine reference required for custom type conversion");
    }
    return eng->make_object(sharedObj);
}

template<typename T>
script_value convert_custom_type_with_registry(const T& t, engine* eng)
    requires (!std::is_copy_constructible_v<std::remove_const_t<T>>)
{
    using NonConstT = std::remove_const_t<T>;
    auto registry = eng->get_conversion_registry();
    if (registry && registry->template has_conversion<NonConstT>()) {
        return registry->template convert_to_script<NonConstT>(t);
    }

    throw runtime_error("Cannot convert non-copyable type by value. "
                       "Register the type with dynamic_binder or return shared_ptr<T>.");
}

template<typename T>
std::optional<script_value> try_convert_shared_ptr_with_registry(const std::shared_ptr<T>& ptr, engine* eng) {
    if (eng && eng->template has_registered_class<T>()) {
        return eng->make_object(ptr);
    }
    return std::nullopt;
}

template<typename T>
script_value convert_reference_with_registry(T& t, engine* eng) {
    using NonConstT = std::remove_const_t<T>;

    if (eng && eng->has_registered_class<NonConstT>()) {
        return script_value::make_cpp_bound(&t, eng);
    }

    if constexpr (std::is_copy_constructible_v<NonConstT>) {
        return convert_custom_type_with_registry<T>(t, eng);
    } else {
        throw runtime_error("Cannot convert non-copyable type by reference. "
                           "Register the type with dynamic_binder first.");
    }
}

template<typename T>
requires (!std::is_arithmetic_v<std::remove_cvref_t<T>> &&
          !std::is_same_v<std::remove_cvref_t<T>, script_string> &&
          !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
          !std::is_convertible_v<T, const char*> &&
          !std::is_same_v<std::remove_cvref_t<T>, std::monostate> &&
          std::is_lvalue_reference_v<T>)
script_value engine::make_value(T&& value) {
    return convert_reference_with_registry(value, this);
}

template<typename T>
requires (!std::is_arithmetic_v<std::remove_cvref_t<T>> &&
          !std::is_same_v<std::remove_cvref_t<T>, script_string> &&
          !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
          !std::is_convertible_v<T, const char*> &&
          !std::is_same_v<std::remove_cvref_t<T>, std::monostate> &&
          !std::is_pointer_v<std::remove_cvref_t<T>> &&
          std::is_rvalue_reference_v<T&&> &&
          !std::is_lvalue_reference_v<T>)
script_value engine::make_value(T&& value) {
    return convert_custom_type_with_registry(value, this);
}

template<typename T>
script_value conversions::convert_vector_to_script_array(const std::vector<T>& vec, engine* eng) {
    type_info_ptr element_type;
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t> ||
                   std::is_same_v<T, float> || std::is_same_v<T, double> ||
                   std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                   std::is_same_v<T, std::string> || std::is_same_v<T, script_value>) {
        element_type = nullptr;
    } else {
        element_type = eng->get_type_info_for_cpp_type<T>();
    }

    auto script_array = script_value::make_array(element_type, eng);
    auto& arr = const_cast<std::vector<script_value>&>(script_array.as_array());

    for (const auto& item : vec) {
        if constexpr (std::is_same_v<T, script_value>) {
            arr.push_back(item);
        } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t> ||
                           std::is_same_v<T, float> || std::is_same_v<T, double> ||
                           std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                           std::is_same_v<T, std::string>) {
            arr.push_back(script_value(item, eng));
        } else {
            if (eng) {
                auto registry = eng->get_conversion_registry();
                if (registry && registry->template has_conversion<T>()) {
                    arr.push_back(registry->template convert_to_script<T>(item));
                    continue;
                }
            }
            auto sharedObj = std::make_shared<T>(item);
            arr.push_back(eng->make_object(sharedObj));
        }
    }

    return script_array;
}

template<typename K, typename V>
script_value conversions::convert_stdmap_to_script_map(const std::map<K, V>& stdmap, engine* eng) {
    type_info_ptr key_type;
    type_info_ptr value_type;

    if constexpr (std::is_same_v<K, int> || std::is_same_v<K, int64_t> ||
                   std::is_same_v<K, float> || std::is_same_v<K, double> ||
                   std::is_same_v<K, bool> || std::is_same_v<K, char> ||
                   std::is_same_v<K, std::string> || std::is_same_v<K, script_value>) {
        key_type = nullptr;
    } else {
        key_type = eng->get_type_info_for_cpp_type<K>();
    }

    if constexpr (std::is_same_v<V, int> || std::is_same_v<V, int64_t> ||
                   std::is_same_v<V, float> || std::is_same_v<V, double> ||
                   std::is_same_v<V, bool> || std::is_same_v<V, char> ||
                   std::is_same_v<V, std::string> || std::is_same_v<V, script_value>) {
        value_type = nullptr;
    } else {
        value_type = eng->get_type_info_for_cpp_type<V>();
    }

    auto map_value = script_value::make_map(key_type, value_type, eng);
    auto& map = const_cast<script_map&>(map_value.as_map());

    for (const auto& [key, value] : stdmap) {
        script_value converted_key = script_value(std::monostate{}, eng);
        script_value converted_value = script_value(std::monostate{}, eng);

        if constexpr (std::is_same_v<K, script_value>) {
            converted_key = key;
        } else if constexpr (std::is_same_v<K, int> || std::is_same_v<K, int64_t> ||
                           std::is_same_v<K, float> || std::is_same_v<K, double> ||
                           std::is_same_v<K, bool> || std::is_same_v<K, char> ||
                           std::is_same_v<K, std::string>) {
            converted_key = script_value(key, eng);
        } else {
            if (eng) {
                auto registry = eng->get_conversion_registry();
                if (registry && registry->template has_conversion<K>()) {
                    converted_key = registry->template convert_to_script<K>(key);
                } else {
                    auto sharedObj = std::make_shared<K>(key);
                    converted_key = eng->make_object(sharedObj);
                }
            } else {
                auto sharedObj = std::make_shared<K>(key);
                converted_key = eng->make_object(sharedObj);
            }
        }

        if constexpr (std::is_same_v<V, script_value>) {
            converted_value = value;
        } else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, int64_t> ||
                           std::is_same_v<V, float> || std::is_same_v<V, double> ||
                           std::is_same_v<V, bool> || std::is_same_v<V, char> ||
                           std::is_same_v<V, std::string>) {
            converted_value = script_value(value, eng);
        } else {
            if (eng) {
                auto registry = eng->get_conversion_registry();
                if (registry && registry->template has_conversion<V>()) {
                    converted_value = registry->template convert_to_script<V>(value);
                } else {
                    auto sharedObj = std::make_shared<V>(value);
                    converted_value = eng->make_object(sharedObj);
                }
            } else {
                auto sharedObj = std::make_shared<V>(value);
                converted_value = eng->make_object(sharedObj);
            }
        }

        map.insert_or_assign(converted_key, converted_value);
    }

    return map_value;
}

template<typename T>
void engine::add_custom_conversion(
    std::function<T(const script_value&)> from_func,
    std::function<script_value(const T&)> to_func
) {
    conversions::conversion_manager conv_manager = get_conversion_manager();
    conv_manager.add_custom_conversion<T>(from_func, to_func);
}

inline detail::parameter_storage::scope_guard::scope_guard(engine* eng, parameter_storage* storage)
    : engine_(eng), previous_(eng ? eng->get_current_parameter_storage() : nullptr) {
    if (eng) {
        eng->set_current_parameter_storage(storage);
    }
}

inline detail::parameter_storage::scope_guard::~scope_guard() {
    if (engine_) {
        engine_->set_current_parameter_storage(previous_);
    }
}

} // namespace jai

#endif // __JAISCRIPT_CORE_ENGINE_IMPL_HPP__
