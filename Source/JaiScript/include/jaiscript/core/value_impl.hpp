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
        using encoding = cpp_bound_encoding<std::remove_const_t<T>>;   // ONE encoding with the binder's member accessors
        constexpr uint8_t sizeAndSign = encoding::size_and_sign;

        if (eng) {
            if constexpr (encoding::semantic_index != TYPEID_NULL) {
                val.type_info_ = cpp_bound_type_info(eng, encoding::semantic_index);
                val.storage_ = make_strong<cpp_bound_holder>(boundPtr, sizeAndSign, encoding::semantic_index);
            } else if constexpr (std::is_class_v<T>) {
                if (std::shared_ptr<detail::first_type_t<class_definition, T>> bound_def = eng->get_class_definition_by_type(std::type_index(typeid(T)))) {
                    // This enables non-owning reference semantics for T& returns
                    auto holder = acquire_object_holder(eng);
                    holder->type_name = bound_def->interned_name();   // interned once per class
                    holder->type_id = bound_def->interned_name_id();
                    holder->data = nullptr;  // Non-owning! bound_ptr holds the pointer
                    holder->is_class_instance_wrapper = false;
                    holder->bound_ptr = boundPtr;

                    val.type_info_ = eng->get_type_info_object(holder->type_id);
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
