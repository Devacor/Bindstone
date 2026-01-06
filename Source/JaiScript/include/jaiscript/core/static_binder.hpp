#pragma once

#ifndef __JAISCRIPT_CORE_STATIC_BINDER_HPP__
#define __JAISCRIPT_CORE_STATIC_BINDER_HPP__

#include <tuple>
#include <type_traits>

// Forward declarations to avoid circular dependencies
namespace jai {
    class engine;
    namespace serialization {
        class archive_writer;
        class archive_reader;
    }
}

namespace jai {

// ============================================================================
// static_binder<T> - Compile-Time Type Registration
// ============================================================================
//
// Provides compile-time type information for serialization and engine binding.
// Unlike dynamic_binder (runtime), static_binder enables:
//   - Zero-overhead serialization dispatch (no runtime lookups)
//   - Compile-time property iteration
//   - Single definition, multiple engine instances
//
// Usage:
//   JAI_STATIC_BINDER(MyType, "MyType",
//       .property("x", &MyType::x)
//       .property("y", &MyType::y)
//       .method("length", &MyType::length)
//   );
//
// ============================================================================

// Compile-time property descriptor
template<typename T, typename P>
struct static_property {
    const char* name;
    P T::* member;

    constexpr static_property(const char* n, P T::* m) : name(n), member(m) {}

    // Direct serialization - called at compile-time dispatch
    void save(serialization::archive_writer& ar, const T& obj) const;
    void load(serialization::archive_reader& ar, T& obj) const;
};

// Compile-time method descriptor
template<typename T, typename MethodPtr>
struct static_method {
    const char* name;
    MethodPtr method;

    constexpr static_method(const char* n, MethodPtr m) : name(n), method(m) {}
};

// The static binder - accumulates properties/methods at compile time
// Each .property() or .method() call returns a new type with the info appended
template<typename T, typename Props = std::tuple<>, typename Methods = std::tuple<>>
struct static_binder {
    Props properties{};
    Methods methods{};

    constexpr static_binder() = default;
    constexpr static_binder(Props p, Methods m) : properties(p), methods(m) {}

    // Add a property (member pointer)
    template<typename P>
    constexpr auto property(const char* name, P T::* member) const {
        auto new_prop = static_property<T, P>{name, member};
        auto new_props = std::tuple_cat(properties, std::make_tuple(new_prop));
        return static_binder<T, decltype(new_props), Methods>{new_props, methods};
    }

    // Add a method
    template<typename MethodPtr>
    constexpr auto method(const char* name, MethodPtr m) const {
        auto new_method = static_method<T, MethodPtr>{name, m};
        auto new_methods = std::tuple_cat(methods, std::make_tuple(new_method));
        return static_binder<T, Props, decltype(new_methods)>{properties, new_methods};
    }

    // Get property count (compile-time)
    static constexpr size_t property_count() {
        return std::tuple_size_v<Props>;
    }

    // Get method count (compile-time)
    static constexpr size_t method_count() {
        return std::tuple_size_v<Methods>;
    }

    // Save all properties
    void save_all(serialization::archive_writer& ar, const T& obj) const {
        std::apply([&](const auto&... props) {
            (props.save(ar, obj), ...);
        }, properties);
    }

    // Load all properties
    void load_all(serialization::archive_reader& ar, T& obj) const {
        std::apply([&](const auto&... props) {
            (props.load(ar, obj), ...);
        }, properties);
    }

    // Bind to engine - creates runtime class_definition
    // Implementation in static_binder_impl.hpp (needs full engine definition)
    void bind_to(engine& eng, const char* type_name) const;
};

// ============================================================================
// jai_static_type<T> - Type trait for static type info
// ============================================================================

// Primary template - type not registered
template<typename T, typename = void>
struct jai_static_type {
    static constexpr bool registered = false;
};

// Detection trait
template<typename T>
inline constexpr bool has_static_type_v = jai_static_type<T>::registered;

// ============================================================================
// JAI_STATIC_BINDER macro - Define static type info
// ============================================================================
//
// Usage:
//   JAI_STATIC_BINDER(MV::Point<int>, "Point<int>",
//       .property("x", &MV::Point<int>::x)
//       .property("y", &MV::Point<int>::y)
//   );
//
// This creates a specialization of jai_static_type<T> with:
//   - name: The script-visible type name
//   - binder: The static_binder instance with all properties/methods
//   - save(): Serialize the type
//   - load(): Deserialize the type
//   - bind_to(): Bind to an engine instance

#define JAI_STATIC_BINDER(Type, Name, ...) \
    template<> struct jai::jai_static_type<Type> { \
        static constexpr bool registered = true; \
        static constexpr const char* name = Name; \
        static constexpr auto binder = jai::static_binder<Type>{} __VA_ARGS__; \
        \
        static void save(jai::serialization::archive_writer& ar, const Type& obj) { \
            binder.save_all(ar, obj); \
        } \
        static void load(jai::serialization::archive_reader& ar, Type& obj) { \
            binder.load_all(ar, obj); \
        } \
        static void bind_to(jai::engine& eng) { \
            binder.bind_to(eng, name); \
        } \
    }

// Convenience macro when type name matches C++ type (no namespaces)
#define JAI_STATIC_BINDER_AUTO(Type, ...) \
    JAI_STATIC_BINDER(Type, #Type, __VA_ARGS__)

} // namespace jai

#endif // __JAISCRIPT_CORE_STATIC_BINDER_HPP__
