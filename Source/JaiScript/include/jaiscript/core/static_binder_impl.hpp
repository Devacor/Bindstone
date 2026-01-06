#pragma once

#ifndef __JAISCRIPT_CORE_STATIC_BINDER_IMPL_HPP__
#define __JAISCRIPT_CORE_STATIC_BINDER_IMPL_HPP__

#include <jaiscript/core/static_binder.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/properties/property_serialization.hpp>

namespace jai {

// ============================================================================
// static_property<T, P> serialization implementation
// ============================================================================

template<typename T, typename P>
void static_property<T, P>::save(serialization::archive_writer& ar, const T& obj) const {
    ar.write_property_name(name);

    // Use the same dispatch logic as property_serialization
    const P& value = obj.*member;

    if constexpr (property_serialization::is_direct_serializable_v<P>) {
        property_serialization::write_primitive(ar, value);
    }
    else if constexpr (property_serialization::is_std_vector_v<P>) {
        property_serialization::write_container(ar, value);
    }
    else if constexpr (property_serialization::is_std_map_v<P>) {
        property_serialization::write_map(ar, value);
    }
    else if constexpr (has_static_type_v<P>) {
        // Nested static type - recurse
        jai_static_type<P>::save(ar, value);
    }
    else if constexpr (property_serialization::has_any_save_v<P>) {
        property_serialization::dispatch_save(ar, value);
    }
    else {
        static_assert(sizeof(P) == 0,
            "Property type has no serialization support. "
            "Provide save/load functions or use JAI_STATIC_BINDER.");
    }
}

template<typename T, typename P>
void static_property<T, P>::load(serialization::archive_reader& ar, T& obj) const {
    // Note: property name already consumed by caller or we read it here
    // For now, assume we're reading values in order

    P& value = obj.*member;

    if constexpr (property_serialization::is_direct_serializable_v<P>) {
        property_serialization::read_primitive(ar, value);
    }
    else if constexpr (property_serialization::is_std_vector_v<P>) {
        property_serialization::read_vector(ar, value);
    }
    else if constexpr (property_serialization::is_std_map_v<P>) {
        property_serialization::read_map(ar, value);
    }
    else if constexpr (has_static_type_v<P>) {
        // Nested static type - recurse
        jai_static_type<P>::load(ar, value);
    }
    else if constexpr (property_serialization::has_any_load_v<P>) {
        property_serialization::dispatch_load(ar, value);
    }
    else {
        static_assert(sizeof(P) == 0,
            "Property type has no deserialization support. "
            "Provide save/load functions or use JAI_STATIC_BINDER.");
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
        // Use the static type info to bind to this engine
        jai_static_type<T>::bind_to(*this);
    } else {
        // Type not registered with JAI_STATIC_BINDER - this is a compile-time error
        static_assert(has_static_type_v<T>,
            "Type is not registered with JAI_STATIC_BINDER. "
            "Use JAI_STATIC_BINDER(Type, \"Name\", .property(...)) to register.");
    }
}

} // namespace jai

#endif // __JAISCRIPT_CORE_STATIC_BINDER_IMPL_HPP__
