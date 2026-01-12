#pragma once

#ifndef __JAISCRIPT_CORE_STATIC_BINDER_IMPL_HPP__
#define __JAISCRIPT_CORE_STATIC_BINDER_IMPL_HPP__

#include <jaiscript/core/static_binder.hpp>
#include <jaiscript/core/template_binder.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/properties/property_serialization.hpp>

namespace jai {

// ============================================================================
// static_property<T, P> serialization implementation
// ============================================================================

template<typename T, typename P>
template<typename Archive>
void static_property<T, P>::save(Archive& ar, const T& obj) const {
    ar.write_property_name(name);

    // Use the same dispatch logic as property_serialization
    const P& value = obj.*member;

    if constexpr (property_serialization::is_direct_serializable_v<P>) {
        property_serialization::write_primitive_templated(ar, value);
    }
    else if constexpr (property_serialization::is_std_vector_v<P>) {
        // Use archive's array methods directly for vectors
        ar.begin_array(value.size());
        for (const auto& elem : value) {
            ar(elem);  // Let archive dispatch element serialization
        }
        ar.end_array();
    }
    else if constexpr (property_serialization::is_std_map_v<P>) {
        // Use archive's map methods directly for maps
        ar.begin_map(value.size());
        for (const auto& [k, v] : value) {
            ar.write_map_key(k);
            ar(v);  // Let archive dispatch value serialization
        }
        ar.end_map();
    }
    else if constexpr (has_static_type_v<P>) {
        // Nested static type - recurse (now templated)
        jai_static_type<P>::save(ar, value);
    }
    else if constexpr (has_any_template_binder_v<P>) {
        // Template type with JAI_TEMPLATE_BINDER - use accessor
        template_binder_accessor<P>::save(ar, value);
    }
    else {
        // Fallback: let archive dispatch via operator()
        ar(value);
    }
}

template<typename T, typename P>
template<typename Archive>
void static_property<T, P>::load(Archive& ar, T& obj) const {
    // Note: property name already consumed by caller or we read it here
    // For now, assume we're reading values in order

    P& value = obj.*member;

    if constexpr (property_serialization::is_direct_serializable_v<P>) {
        property_serialization::read_primitive_templated(ar, value);
    }
    else if constexpr (property_serialization::is_std_vector_v<P>) {
        // Use archive's array methods directly for vectors
        using elem_t = typename P::value_type;
        size_t count = ar.begin_array();
        value.clear();
        value.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            elem_t elem{};
            ar(elem);  // Let archive dispatch element deserialization
            value.push_back(std::move(elem));
        }
        ar.end_array();
    }
    else if constexpr (property_serialization::is_std_map_v<P>) {
        // Use archive's map methods directly for maps
        using key_t = typename P::key_type;
        using val_t = typename P::mapped_type;
        size_t count = ar.begin_map();
        value.clear();
        for (size_t i = 0; i < count; ++i) {
            std::string key_str;
            if (!ar.read_map_key(key_str)) break;
            key_t key{};
            if constexpr (std::is_same_v<key_t, std::string>) {
                key = key_str;
            } else if constexpr (std::is_integral_v<key_t>) {
                key = static_cast<key_t>(std::stoll(key_str));
            }
            val_t val{};
            ar(val);  // Let archive dispatch value deserialization
            value[key] = std::move(val);
        }
        ar.end_map();
    }
    else if constexpr (has_static_type_v<P>) {
        // Nested static type - recurse (now templated)
        jai_static_type<P>::load(ar, value);
    }
    else if constexpr (has_any_template_binder_v<P>) {
        // Template type with JAI_TEMPLATE_BINDER - use accessor
        template_binder_accessor<P>::load(ar, value);
    }
    else {
        // Fallback: let archive dispatch via operator()
        ar(value);
    }
}

// ============================================================================
// static_binder<T>::bind_to implementation
// ============================================================================
// This creates a runtime class_definition from the static info
// Requires dynamic_binder (formerly dynamic_binder)

} // namespace jai

// Include dynamic_binder for bind_to implementation
// This is done after the namespace close to avoid circular includes
#include <jaiscript/core/dynamic_binder.hpp>

namespace jai {

template<typename T, typename Props, typename Methods>
void static_binder<T, Props, Methods>::bind_to(engine& eng, const char* type_name) const {
    // Idempotent: check if type is already registered
    if (eng.is_type_registered(type_name)) {
        return;  // Already bound, nothing to do
    }

    // Create dynamic binder from static info
    dynamic_binder<T> builder(eng, type_name);

    // Bind all properties
    std::apply([&](const auto&... props) {
        (builder.property(props.name, props.member), ...);
    }, properties);

    // Bind all methods
    std::apply([&](const auto&... meths) {
        (builder.method(meths.name, meths.method), ...);
    }, methods);

    builder.build();
}

// ============================================================================
// engine::bind_static_type<T>() implementation
// ============================================================================

template<typename T>
void engine::bind_static_type() {
    if constexpr (has_static_type_v<T>) {
        // Idempotent: check if already registered before binding
        // This makes calling bind_static_type multiple times safe
        if (is_type_registered(jai_static_type<T>::name)) {
            return;  // Already bound, nothing to do
        }
        // Use the static type info to bind to this engine
        jai_static_type<T>::bind_to(*this);
    }
    else if constexpr (has_any_template_binder_v<T>) {
        // Template type with JAI_TEMPLATE_BINDER - use accessor for dynamic binding
        auto type_name = template_binder_accessor<T>::type_name();
        if (is_type_registered(type_name)) {
            return;  // Already bound, nothing to do
        }
        template_binder_accessor<T>::bind_to(*this);

        // Auto-bind complementary integer variants (int32 ↔ int64, uint32 ↔ uint64)
        // This ensures Point<int> and Point<int32> are both available when either is bound
        integer_variant_binder<T>::bind(*this);
    }
    else {
        // Type not registered with JAI_STATIC_BINDER or JAI_TEMPLATE_BINDER
        static_assert(has_static_type_v<T> || has_any_template_binder_v<T>,
            "Type is not registered with JAI_STATIC_BINDER or JAI_TEMPLATE_BINDER. "
            "Use JAI_STATIC_BINDER(Type, \"Name\", .property(...)) or "
            "JAI_TEMPLATE_BINDER(Template, \"Name\", ...) to register.");
    }
}

} // namespace jai

#endif // __JAISCRIPT_CORE_STATIC_BINDER_IMPL_HPP__
