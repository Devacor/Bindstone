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

        // Use const_cast since the binding box is type-erased storage
        // Constness is handled at the call site via make_value overloads
        void* boundPtr = const_cast<void*>(static_cast<const void*>(target));
        uint8_t sizeAndSign = static_cast<uint8_t>(sizeof(T));
        if constexpr (std::is_unsigned_v<T> && !std::is_same_v<T, bool>)
            sizeAndSign |= 0x80;

        if (eng) {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                val.type_info_ = eng->get_type_info_int();
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_INT));
            } else if constexpr (std::is_floating_point_v<T>) {
                val.type_info_ = eng->get_type_info_float();
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_FLOAT));
            } else if constexpr (std::is_same_v<T, bool>) {
                val.type_info_ = eng->get_type_info_bool();
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_BOOL));
            } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, script_string>) {
                val.type_info_ = eng->get_type_info_string();
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_STRING));
            } else if constexpr (std::is_same_v<T, char>) {
                val.type_info_ = eng->get_type_info_char();
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_CHAR));
            } else if constexpr (std::is_class_v<T>) {
                if (eng->template has_registered_class<T>()) {
                    // This enables non-owning reference semantics for T& returns
                    auto holder = make_strong<object_holder>();
                    holder->type_name = eng->template get_registered_name<T>();
                    holder->type_id = eng->get_symbolizer()->intern(holder->type_name);
                    holder->data = nullptr;  // Non-owning! bound_ptr holds the pointer
                    holder->is_class_instance_wrapper = false;
                    holder->bound_ptr = boundPtr;

                    val.type_info_ = eng->get_type_info_object(holder->type_name);
                    val.storage_ = std::move(holder);
                } else {
                    val.type_info_ = eng->get_type_info_object(typeid(T).name());
                    val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_NULL));
                }
            } else {
                val.type_info_ = eng->get_type_info_object(typeid(T).name());
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_NULL));
            }
        } else {
            // No engine: keep the binding observable (is_cpp_bound), semantics opaque
            val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, static_cast<uint8_t>(TYPEID_NULL));
        }
        return val;
    }

} // namespace jai

#endif // __JAISCRIPT_CORE_VALUE_IMPL_HPP__
