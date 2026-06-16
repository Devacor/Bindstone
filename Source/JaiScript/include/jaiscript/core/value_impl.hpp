#pragma once

// Include this AFTER both value.hpp and engine.hpp are included

#ifndef __JAISCRIPT_CORE_VALUE_IMPL_HPP__
#define __JAISCRIPT_CORE_VALUE_IMPL_HPP__

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai {

    template<typename T>
    requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
    script_value::script_value(T i, engine* eng) : type_info_(nullptr), engine_(eng), storage_(script_int(i)) {
        if (eng) {
            type_info_ = eng->get_type_info_int();
        }
    }

    template<typename T>
    requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
    script_value::script_value(T f, engine* eng) : type_info_(nullptr), engine_(eng), storage_(script_float(f)) {
        if (eng) {
            type_info_ = eng->get_type_info_float();
        }
    }

    template<typename T>
    script_value script_value::make_cpp_bound(T* target, engine* eng) {
        script_value val(std::monostate{}, eng);

        if (eng) {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                val.type_info_ = eng->get_type_info_int();
                val.storage_ = script_int{0};
            } else if constexpr (std::is_floating_point_v<T>) {
                val.type_info_ = eng->get_type_info_float();
                val.storage_ = script_float{0.0};
            } else if constexpr (std::is_same_v<T, bool>) {
                val.type_info_ = eng->get_type_info_bool();
                val.storage_ = script_bool{false};
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, script_string>) {
                val.type_info_ = eng->get_type_info_string();
                val.storage_ = make_strong<script_string>();
            } else if constexpr (std::is_same_v<T, char>) {
                val.type_info_ = eng->get_type_info_char();
                val.storage_ = script_char{'\0'};
            } else if constexpr (std::is_class_v<T>) {
                if (eng->template has_registered_class<T>()) {
                    // This enables non-owning reference semantics for T& returns
                    auto holder = make_strong<object_holder>();
                    holder->type_name = eng->template get_registered_name<T>();
                    holder->type_id = eng->get_symbolizer()->intern(holder->type_name);
                    holder->data = nullptr;  // Non-owning! cpp_bound_ptr_ holds the pointer
                    holder->is_class_instance_wrapper = false;

                    val.type_info_ = eng->get_type_info_object(holder->type_name);
                    val.storage_ = std::move(holder);
                } else {
                    val.type_info_ = eng->get_type_info_object(typeid(T).name());
                    val.storage_ = std::monostate{};
                }
            } else {
                val.type_info_ = eng->get_type_info_object(typeid(T).name());
                val.storage_ = std::monostate{};
            }
        }

        // Use const_cast since cpp_bound_ptr_ is type-erased storage
        // Constness is handled at the call site via make_value overloads
        val.cpp_bound_ptr_ = const_cast<void*>(static_cast<const void*>(target));
        val.cpp_bound_type_size_ = static_cast<uint8_t>(sizeof(T));
        if constexpr (std::is_unsigned_v<T> && !std::is_same_v<T, bool>)
            val.cpp_bound_type_size_ |= 0x80;
        return val;
    }

} // namespace jai

#endif // __JAISCRIPT_CORE_VALUE_IMPL_HPP__
