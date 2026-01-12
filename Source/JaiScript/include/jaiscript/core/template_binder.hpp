#pragma once

#ifndef __JAISCRIPT_CORE_TEMPLATE_BINDER_HPP__
#define __JAISCRIPT_CORE_TEMPLATE_BINDER_HPP__

#include <jaiscript/core/static_binder.hpp>
#include <string>
#include <typeinfo>

namespace jai {

// ============================================================================
// type_name_helper<T> - Clean Type Names for Script Binding
// ============================================================================
// Provides human-readable type names for template parameters.
// Specialize for custom types to get clean names in the script layer.

template<typename T>
struct type_name_helper {
    static std::string name() {
        // Fallback to compiler's mangled name (not ideal but works)
        return typeid(T).name();
    }
};

// Specializations for explicit integer types (avoids duplicates with platform-dependent int)
template<> struct type_name_helper<int8_t> {
    static std::string name() { return "int8"; }
};
template<> struct type_name_helper<int16_t> {
    static std::string name() { return "int16"; }
};
template<> struct type_name_helper<int32_t> {
    static std::string name() { return "int32"; }
};
template<> struct type_name_helper<int64_t> {
    static std::string name() { return "int"; }  // JaiScript's native int is 64-bit
};
template<> struct type_name_helper<uint8_t> {
    static std::string name() { return "uint8"; }
};
template<> struct type_name_helper<uint16_t> {
    static std::string name() { return "uint16"; }
};
template<> struct type_name_helper<uint32_t> {
    static std::string name() { return "uint32"; }
};
template<> struct type_name_helper<uint64_t> {
    static std::string name() { return "uint"; }  // JaiScript's native uint is 64-bit
};
template<> struct type_name_helper<float> {
    static std::string name() { return "float"; }
};
template<> struct type_name_helper<double> {
    static std::string name() { return "double"; }
};
template<> struct type_name_helper<bool> {
    static std::string name() { return "bool"; }
};
template<> struct type_name_helper<char> {
    static std::string name() { return "char"; }
};
template<> struct type_name_helper<std::string> {
    static std::string name() { return "string"; }
};

// ============================================================================
// static_type_registry - Global Type Registration Tracking
// ============================================================================
// Tracks which types have been registered to avoid duplicate registrations.
// This is used by auto_bind_template to ensure thread-safe, one-time registration.

class static_type_registry {
public:
    // Check if a type is already registered (by type_index)
    template<typename T>
    static bool is_registered() {
        return get_registration_flag<T>();
    }

    // Mark a type as registered
    template<typename T>
    static void mark_registered() {
        get_registration_flag<T>() = true;
    }

private:
    // Per-type static flag - thread-safe initialization guaranteed by C++11
    template<typename T>
    static bool& get_registration_flag() {
        static bool registered = false;
        return registered;
    }
};

// ============================================================================
// template_binder_traits - Forward declaration for auto_bind_template
// ============================================================================
// Specialized by JAI_TEMPLATE_BINDER to provide type_name() and make_binder()

// Single type parameter
template<template<typename> class Template, typename T>
struct template_binder_traits;

// Two type parameters
template<template<typename, typename> class Template, typename T1, typename T2>
struct template_binder_traits_2;

// Three type parameters
template<template<typename, typename, typename> class Template, typename T1, typename T2, typename T3>
struct template_binder_traits_3;

// ============================================================================
// has_template_binder<T> - Auto-Detection for Template Binder Traits
// ============================================================================
// Detects if a type T is a template instantiation (e.g., Point<int>) that has
// template_binder_traits defined via JAI_TEMPLATE_BINDER.
// This enables automatic serialization/binding without explicit JAI_BIND_TEMPLATE.

// Primary template - not a template instantiation or no traits available
template<typename T, typename = void>
struct has_template_binder : std::false_type {};

// Specialization for single-parameter template instantiations
template<template<typename> class Template, typename Arg>
struct has_template_binder<Template<Arg>,
    std::void_t<decltype(template_binder_traits<Template, Arg>::type_name())>
> : std::true_type {};

template<typename T>
inline constexpr bool has_template_binder_v = has_template_binder<T>::value;

// Two-parameter version
template<typename T, typename = void>
struct has_template_binder_2 : std::false_type {};

template<template<typename, typename> class Template, typename Arg1, typename Arg2>
struct has_template_binder_2<Template<Arg1, Arg2>,
    std::void_t<decltype(template_binder_traits_2<Template, Arg1, Arg2>::type_name())>
> : std::true_type {};

template<typename T>
inline constexpr bool has_template_binder_2_v = has_template_binder_2<T>::value;

// Combined check - true if either 1-param or 2-param traits exist
template<typename T>
inline constexpr bool has_any_template_binder_v =
    has_template_binder_v<T> || has_template_binder_2_v<T>;

// ============================================================================
// template_binder_accessor<T> - Access Traits from Instantiated Type
// ============================================================================
// Given Point<int>, provides access to template_binder_traits<Point, int>.
// Used by serialization and binding to work with template types dynamically.

// Primary template - undefined (SFINAE will prevent use for non-template types)
template<typename T, typename = void>
struct template_binder_accessor;

// Single type parameter accessor
template<template<typename> class Template, typename Arg>
struct template_binder_accessor<Template<Arg>,
    std::enable_if_t<has_template_binder_v<Template<Arg>>>
> {
    using traits = template_binder_traits<Template, Arg>;

    static std::string type_name() { return traits::type_name(); }
    static auto make_binder() { return traits::make_binder(); }

    template<typename Archive>
    static void save(Archive& ar, const Template<Arg>& obj) {
        make_binder().save_all(ar, obj);
    }

    template<typename Archive>
    static void load(Archive& ar, Template<Arg>& obj) {
        make_binder().load_all(ar, obj);
    }

    static void bind_to(engine& eng) {
        auto binder = make_binder();
        auto name = type_name();
        binder.bind_to(eng, name.c_str());
    }
};

// Two type parameter accessor
template<template<typename, typename> class Template, typename Arg1, typename Arg2>
struct template_binder_accessor<Template<Arg1, Arg2>,
    std::enable_if_t<has_template_binder_2_v<Template<Arg1, Arg2>>>
> {
    using traits = template_binder_traits_2<Template, Arg1, Arg2>;

    static std::string type_name() { return traits::type_name(); }
    static auto make_binder() { return traits::make_binder(); }

    template<typename Archive>
    static void save(Archive& ar, const Template<Arg1, Arg2>& obj) {
        make_binder().save_all(ar, obj);
    }

    template<typename Archive>
    static void load(Archive& ar, Template<Arg1, Arg2>& obj) {
        make_binder().load_all(ar, obj);
    }

    static void bind_to(engine& eng) {
        auto binder = make_binder();
        auto name = type_name();
        binder.bind_to(eng, name.c_str());
    }
};

// ============================================================================
// integer_variant_binder - Auto-bind int32/int64 variants together
// ============================================================================
// When binding a template with an integer type parameter, automatically
// binds both 32-bit and 64-bit variants so users get:
//   - Point<int>    (64-bit, JaiScript's native int)
//   - Point<int32>  (32-bit, explicit)
//
// This is called from engine::bind_static_type() after binding the primary type.

// Forward declaration (implemented after engine is available)
template<typename T>
struct integer_variant_binder {
    static void bind(engine&) {} // Default: no-op for non-templates
};

// Specialization for single-param template instantiations
template<template<typename> class Template, typename Arg>
struct integer_variant_binder<Template<Arg>> {
    static void bind(engine& eng) {
        // int32_t / int → also bind int64_t
        if constexpr (std::is_same_v<Arg, int32_t>) {
            if constexpr (has_template_binder_v<Template<int64_t>>) {
                auto name = template_binder_accessor<Template<int64_t>>::type_name();
                if (!eng.is_type_registered(name)) {
                    template_binder_accessor<Template<int64_t>>::bind_to(eng);
                }
            }
        }
        // int64_t → also bind int32_t
        else if constexpr (std::is_same_v<Arg, int64_t>) {
            if constexpr (has_template_binder_v<Template<int32_t>>) {
                auto name = template_binder_accessor<Template<int32_t>>::type_name();
                if (!eng.is_type_registered(name)) {
                    template_binder_accessor<Template<int32_t>>::bind_to(eng);
                }
            }
        }
        // uint32_t → also bind uint64_t
        else if constexpr (std::is_same_v<Arg, uint32_t>) {
            if constexpr (has_template_binder_v<Template<uint64_t>>) {
                auto name = template_binder_accessor<Template<uint64_t>>::type_name();
                if (!eng.is_type_registered(name)) {
                    template_binder_accessor<Template<uint64_t>>::bind_to(eng);
                }
            }
        }
        // uint64_t → also bind uint32_t
        else if constexpr (std::is_same_v<Arg, uint64_t>) {
            if constexpr (has_template_binder_v<Template<uint32_t>>) {
                auto name = template_binder_accessor<Template<uint32_t>>::type_name();
                if (!eng.is_type_registered(name)) {
                    template_binder_accessor<Template<uint32_t>>::bind_to(eng);
                }
            }
        }
    }
};

// ============================================================================
// auto_bind_template - CRTP Helper for Automatic Registration
// ============================================================================
// Inherit from this to automatically register template instantiations.
// Registration happens when the type is first ODR-used (before main()).
//
// Usage:
//   template<typename T>
//   class Point : public jai::property_owner<Point<T>>,
//                 public jai::auto_bind_template<Point, T> {
//       JAI_PROPERTY((T), x, T{});
//       JAI_PROPERTY((T), y, T{});
//   };

// Single type parameter version
template<template<typename> class Template, typename T>
struct auto_bind_template {
private:
    // Static initialization triggers registration once per instantiation
    struct registrar {
        registrar() {
            if (!static_type_registry::is_registered<Template<T>>()) {
                static_type_registry::mark_registered<Template<T>>();
                // Registration happens via jai_static_type<Template<T>>
                // which is created by JAI_BIND_TEMPLATE or auto-triggered here
            }
        }
    };

    // Force instantiation of registrar
    static inline registrar _registrar{};

    // Force ODR-use
    static constexpr const registrar* _force_registration = &_registrar;
};

// Two type parameters version
template<template<typename, typename> class Template, typename T1, typename T2>
struct auto_bind_template_2 {
private:
    struct registrar {
        registrar() {
            if (!static_type_registry::is_registered<Template<T1, T2>>()) {
                static_type_registry::mark_registered<Template<T1, T2>>();
            }
        }
    };
    static inline registrar _registrar{};
    static constexpr const registrar* _force_registration = &_registrar;
};

// ============================================================================
// JAI_TEMPLATE_BINDER - Define Template Binding Once
// ============================================================================
// Creates a template struct that knows how to bind any instantiation.
// Use JAI_BIND_TEMPLATE to instantiate for specific type parameters.
//
// Simple version (3 params) - TemplateClass used as binder name:
//   JAI_TEMPLATE_BINDER(Point, "Point",
//       .property("x", &Point<T>::x)
//       .property("y", &Point<T>::y)
//   );
//
// Named version (4 params) - for namespaced types:
//   JAI_TEMPLATE_BINDER_NAMED(MyPoint, MyNamespace::Point, "Point",
//       .property("x", &MyNamespace::Point<T>::x)
//   );

// Implementation macro - takes all 4 params explicitly
#define JAI_TEMPLATE_BINDER_IMPL(BinderName, TemplateClass, BaseName, ...) \
    template<typename T> \
    struct jai_template_binder_##BinderName { \
        static constexpr const char* base_name = BaseName; \
        \
        static std::string type_name() { \
            return std::string(BaseName) + "<" + \
                   jai::type_name_helper<T>::name() + ">"; \
        } \
        \
        static auto make_binder() { \
            return jai::static_binder<TemplateClass<T>>{} \
                __VA_ARGS__; \
        } \
    }; \
    \
    /* Specialize template_binder_traits for auto_bind_template support */ \
    template<typename T> \
    struct jai::template_binder_traits<TemplateClass, T> { \
        static std::string type_name() { \
            return jai_template_binder_##BinderName<T>::type_name(); \
        } \
        static auto make_binder() { \
            return jai_template_binder_##BinderName<T>::make_binder(); \
        } \
    }

// Simple version: JAI_TEMPLATE_BINDER(TemplateClass, BaseName, ...)
// Uses TemplateClass as the binder name (must be simple identifier, no namespaces)
#define JAI_TEMPLATE_BINDER(TemplateClass, BaseName, ...) \
    JAI_TEMPLATE_BINDER_IMPL(TemplateClass, TemplateClass, BaseName, __VA_ARGS__)

// Named version: JAI_TEMPLATE_BINDER_NAMED(BinderName, TemplateClass, BaseName, ...)
// For namespaced types where TemplateClass contains ::
#define JAI_TEMPLATE_BINDER_NAMED(BinderName, TemplateClass, BaseName, ...) \
    JAI_TEMPLATE_BINDER_IMPL(BinderName, TemplateClass, BaseName, __VA_ARGS__)

// Two type parameters - implementation
#define JAI_TEMPLATE_BINDER_2_IMPL(BinderName, TemplateClass, BaseName, ...) \
    template<typename T1, typename T2> \
    struct jai_template_binder_##BinderName { \
        static constexpr const char* base_name = BaseName; \
        \
        static std::string type_name() { \
            return std::string(BaseName) + "<" + \
                   jai::type_name_helper<T1>::name() + ", " + \
                   jai::type_name_helper<T2>::name() + ">"; \
        } \
        \
        static auto make_binder() { \
            return jai::static_binder<TemplateClass<T1, T2>>{} \
                __VA_ARGS__; \
        } \
    }; \
    \
    template<typename T1, typename T2> \
    struct jai::template_binder_traits_2<TemplateClass, T1, T2> { \
        static std::string type_name() { \
            return jai_template_binder_##BinderName<T1, T2>::type_name(); \
        } \
        static auto make_binder() { \
            return jai_template_binder_##BinderName<T1, T2>::make_binder(); \
        } \
    }

// Two type parameters - simple version
#define JAI_TEMPLATE_BINDER_2(TemplateClass, BaseName, ...) \
    JAI_TEMPLATE_BINDER_2_IMPL(TemplateClass, TemplateClass, BaseName, __VA_ARGS__)

// Two type parameters - named version
#define JAI_TEMPLATE_BINDER_2_NAMED(BinderName, TemplateClass, BaseName, ...) \
    JAI_TEMPLATE_BINDER_2_IMPL(BinderName, TemplateClass, BaseName, __VA_ARGS__)

// ============================================================================
// JAI_BIND_TEMPLATE - Instantiate Template Binding for Specific Types
// ============================================================================
// Creates a jai_static_type specialization for the specific instantiation.
// This enables the type to be used with bind_static_type<T>().
//
// Simple version (2 params) - TemplateClass matches binder name:
//   JAI_BIND_TEMPLATE(Point, int);      // Creates Point<int> binding
//   JAI_BIND_TEMPLATE(Point, float);    // Creates Point<float> binding
//
// Named version (3 params) - for namespaced types:
//   JAI_BIND_TEMPLATE_NAMED(MyPoint, NS::Point, int);   // Creates NS::Point<int> binding

// Implementation macro - takes all 3 params explicitly
#define JAI_BIND_TEMPLATE_IMPL(BinderName, TemplateClass, TypeParam) \
    template<> struct jai::jai_static_type<TemplateClass<TypeParam>> { \
        static constexpr bool registered = true; \
        static inline const std::string name = \
            jai_template_binder_##BinderName<TypeParam>::type_name(); \
        static inline const auto binder = \
            jai_template_binder_##BinderName<TypeParam>::make_binder(); \
        \
        template<typename Archive> \
        static void save(Archive& ar, \
                         const TemplateClass<TypeParam>& obj) { \
            binder.save_all(ar, obj); \
        } \
        template<typename Archive> \
        static void load(Archive& ar, \
                         TemplateClass<TypeParam>& obj) { \
            binder.load_all(ar, obj); \
        } \
        static void bind_to(jai::engine& eng) { \
            binder.bind_to(eng, name.c_str()); \
        } \
    private: \
        /* Registration marker - uses address for unique identifier */ \
        static inline const bool _registered = []() { \
            jai::static_type_registry::mark_registered<TemplateClass<TypeParam>>(); \
            return true; \
        }(); \
    }

// Simple version: JAI_BIND_TEMPLATE(TemplateClass, TypeParam)
// Uses TemplateClass as the binder name (must match JAI_TEMPLATE_BINDER)
#define JAI_BIND_TEMPLATE(TemplateClass, TypeParam) \
    JAI_BIND_TEMPLATE_IMPL(TemplateClass, TemplateClass, TypeParam)

// Named version: JAI_BIND_TEMPLATE_NAMED(BinderName, TemplateClass, TypeParam)
// For namespaced types where TemplateClass contains ::
#define JAI_BIND_TEMPLATE_NAMED(BinderName, TemplateClass, TypeParam) \
    JAI_BIND_TEMPLATE_IMPL(BinderName, TemplateClass, TypeParam)

// Two type parameters - implementation
#define JAI_BIND_TEMPLATE_2_IMPL(BinderName, TemplateClass, T1, T2) \
    template<> struct jai::jai_static_type<TemplateClass<T1, T2>> { \
        static constexpr bool registered = true; \
        static inline const std::string name = \
            jai_template_binder_##BinderName<T1, T2>::type_name(); \
        static inline const auto binder = \
            jai_template_binder_##BinderName<T1, T2>::make_binder(); \
        \
        template<typename Archive> \
        static void save(Archive& ar, \
                         const TemplateClass<T1, T2>& obj) { \
            binder.save_all(ar, obj); \
        } \
        template<typename Archive> \
        static void load(Archive& ar, \
                         TemplateClass<T1, T2>& obj) { \
            binder.load_all(ar, obj); \
        } \
        static void bind_to(jai::engine& eng) { \
            binder.bind_to(eng, name.c_str()); \
        } \
    private: \
        /* Registration marker - uses address for unique identifier */ \
        static inline const bool _registered = []() { \
            jai::static_type_registry::mark_registered<TemplateClass<T1, T2>>(); \
            return true; \
        }(); \
    }

// Two type parameters - simple version
#define JAI_BIND_TEMPLATE_2(TemplateClass, T1, T2) \
    JAI_BIND_TEMPLATE_2_IMPL(TemplateClass, TemplateClass, T1, T2)

// Two type parameters - named version
#define JAI_BIND_TEMPLATE_2_NAMED(BinderName, TemplateClass, T1, T2) \
    JAI_BIND_TEMPLATE_2_IMPL(BinderName, TemplateClass, T1, T2)

// ============================================================================
// JAI_BIND_TEMPLATE_VARIANTS - Batch Registration Helper
// ============================================================================
// Register multiple type variants at once.
//
// Usage:
//   JAI_BIND_TEMPLATE_VARIANTS_3(Point, int, float, double);

// Note: Full variadic macro support would require more complex preprocessor magic.
// For now, provide explicit overloads for common cases.

#define JAI_BIND_TEMPLATE_VARIANTS_1(T, P1) \
    JAI_BIND_TEMPLATE(T, P1)

#define JAI_BIND_TEMPLATE_VARIANTS_2(T, P1, P2) \
    JAI_BIND_TEMPLATE(T, P1); \
    JAI_BIND_TEMPLATE(T, P2)

#define JAI_BIND_TEMPLATE_VARIANTS_3(T, P1, P2, P3) \
    JAI_BIND_TEMPLATE(T, P1); \
    JAI_BIND_TEMPLATE(T, P2); \
    JAI_BIND_TEMPLATE(T, P3)

#define JAI_BIND_TEMPLATE_VARIANTS_4(T, P1, P2, P3, P4) \
    JAI_BIND_TEMPLATE(T, P1); \
    JAI_BIND_TEMPLATE(T, P2); \
    JAI_BIND_TEMPLATE(T, P3); \
    JAI_BIND_TEMPLATE(T, P4)

#define JAI_BIND_TEMPLATE_VARIANTS_5(T, P1, P2, P3, P4, P5) \
    JAI_BIND_TEMPLATE(T, P1); \
    JAI_BIND_TEMPLATE(T, P2); \
    JAI_BIND_TEMPLATE(T, P3); \
    JAI_BIND_TEMPLATE(T, P4); \
    JAI_BIND_TEMPLATE(T, P5)

// Named variants for namespaced types
#define JAI_BIND_TEMPLATE_VARIANTS_NAMED_1(B, T, P1) \
    JAI_BIND_TEMPLATE_NAMED(B, T, P1)

#define JAI_BIND_TEMPLATE_VARIANTS_NAMED_2(B, T, P1, P2) \
    JAI_BIND_TEMPLATE_NAMED(B, T, P1); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P2)

#define JAI_BIND_TEMPLATE_VARIANTS_NAMED_3(B, T, P1, P2, P3) \
    JAI_BIND_TEMPLATE_NAMED(B, T, P1); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P2); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P3)

#define JAI_BIND_TEMPLATE_VARIANTS_NAMED_4(B, T, P1, P2, P3, P4) \
    JAI_BIND_TEMPLATE_NAMED(B, T, P1); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P2); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P3); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P4)

#define JAI_BIND_TEMPLATE_VARIANTS_NAMED_5(B, T, P1, P2, P3, P4, P5) \
    JAI_BIND_TEMPLATE_NAMED(B, T, P1); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P2); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P3); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P4); \
    JAI_BIND_TEMPLATE_NAMED(B, T, P5)

} // namespace jai

#endif // __JAISCRIPT_CORE_TEMPLATE_BINDER_HPP__
