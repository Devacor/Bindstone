#pragma once

// Implementation file for script_value template methods
// Include this AFTER both value.hpp and engine.hpp are included

#ifndef __JAISCRIPT_CORE_VALUE_IMPL_HPP__
#define __JAISCRIPT_CORE_VALUE_IMPL_HPP__

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai {

    // Engine-aware template constructors for integral types
    template<typename T>
    requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
    script_value::script_value(T i, std::weak_ptr<engine> eng) : type_info_(nullptr), engine_ref_(eng), storage_(script_int(i)) {
        if (auto e = eng.lock()) {
            type_info_ = e->get_type_info_int();
        }
    }

    // Engine-aware template constructors for floating point types
    template<typename T>
    requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
    script_value::script_value(T f, std::weak_ptr<engine> eng) : type_info_(nullptr), engine_ref_(eng), storage_(script_float(f)) {
        if (auto e = eng.lock()) {
            type_info_ = e->get_type_info_float();
        }
    }

    // Factory method for C++ bound values
    template<typename T>
    script_value script_value::make_cpp_bound(T* target, std::weak_ptr<engine> eng) {
        script_value val(std::monostate{}, eng);

        if (auto e = eng.lock()) {
            // Map C++ types to script types
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                // Integer types (int, long, int64_t, etc.) -> script_int
                val.type_info_ = e->get_type_info_int();
                val.storage_ = script_int{0};
            } else if constexpr (std::is_floating_point_v<T>) {
                // Floating point types (float, double) -> script_float
                val.type_info_ = e->get_type_info_float();
                val.storage_ = script_float{0.0};
            } else if constexpr (std::is_same_v<T, bool>) {
                // bool -> script_bool
                val.type_info_ = e->get_type_info_bool();
                val.storage_ = script_bool{false};
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, script_string>) {
                // string types -> script_string (wrapped in shared_ptr)
                val.type_info_ = e->get_type_info_string();
                val.storage_ = std::make_shared<script_string>();
            } else if constexpr (std::is_same_v<T, char>) {
                // char -> script_char
                val.type_info_ = e->get_type_info_char();
                val.storage_ = script_char{'\0'};
            } else {
                // For complex types, use object type
                val.type_info_ = e->get_type_info_object(typeid(T).name());
                val.storage_ = std::monostate{};
            }
        }

        val.cpp_bound_ptr_ = static_cast<void*>(target);
        return val;
    }

} // namespace jai

#endif // __JAISCRIPT_CORE_VALUE_IMPL_HPP__
