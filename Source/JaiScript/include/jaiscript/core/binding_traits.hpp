#pragma once

#include <type_traits>
#include <vector>
#include <map>
#include <memory>
#include <tuple>

namespace jai {

// Forward declarations
class script_value;
template<typename T> class bound_array;
template<typename K, typename V> class bound_map;

namespace binding_traits {

// ============ Container Reference Detection ============

// Detect const std::vector references (but exclude script_value vectors which are native). A non-const one writes the
// callee's final value back into the script lvalue (core/function_binder.hpp host_writes_back).
template<typename T>
struct is_std_vector_ref : std::false_type {};

template<typename T>
struct is_std_vector_ref<const std::vector<T>&> : std::bool_constant<!std::is_same_v<T, script_value>> {};

// Detect const std::map references (but exclude script_value maps which are native)
template<typename T>
struct is_std_map_ref : std::false_type {};

template<typename K, typename V>
struct is_std_map_ref<const std::map<K, V>&> : std::bool_constant<
    !(std::is_same_v<K, script_value> && std::is_same_v<V, script_value>)> {};

// Helper to check if type is a container reference we want to reject
template<typename T>
struct is_rejected_container_ref : std::disjunction<
    is_std_vector_ref<T>,
    is_std_map_ref<T>
> {};

// Check if any parameter in a parameter pack contains rejected types
template<typename... Args>
struct has_rejected_container_ref;

template<>
struct has_rejected_container_ref<> : std::false_type {};

template<typename First, typename... Rest>
struct has_rejected_container_ref<First, Rest...> 
    : std::disjunction<
        is_rejected_container_ref<First>,
        has_rejected_container_ref<Rest...>
    > {};

// Extract parameter types from function
template<typename T>
struct function_param_checker : function_param_checker<decltype(&T::operator())> {
    // For lambdas and functors, delegate to operator() overload
};

template<typename R, typename... Args>
struct function_param_checker<R(*)(Args...)> {
    static constexpr bool has_rejected_refs = has_rejected_container_ref<Args...>::value;
};

template<typename R, typename C, typename... Args>
struct function_param_checker<R(C::*)(Args...)> {
    static constexpr bool has_rejected_refs = has_rejected_container_ref<Args...>::value;
};

template<typename R, typename C, typename... Args>
struct function_param_checker<R(C::*)(Args...) const> {
    static constexpr bool has_rejected_refs = has_rejected_container_ref<Args...>::value;
};

// ============ Binding Policy ============

#ifdef JAI_ALLOW_MISLEADING_REFERENCE_BINDING

// Permissive mode - allow with warnings
template<typename Func>
inline void check_binding_validity(const std::string& name) {
    if constexpr (function_param_checker<std::decay_t<Func>>::has_rejected_refs) {
        // Could log warning here in the future
        // For now, the warning will be in the engine implementation
    }
}

#else

// Strict mode (default) - reject at compile time
template<typename Func>
inline void check_binding_validity(const std::string& name) {
    static_assert(!function_param_checker<std::decay_t<Func>>::has_rejected_refs,
        "\n"
        "╔════════════════════════════════════════════════════════════════╗\n"
        "║                  JaiScript Binding Error                       ║\n"
        "╠════════════════════════════════════════════════════════════════╣\n"
        "║ Cannot bind functions with const container reference params:   ║\n"
        "║   • const std::vector<T>&                                      ║\n"
        "║   • const std::map<K,V>&                                       ║\n"
        "║                                                                ║\n"
        "║ Why? These bind a converted copy, not the script container!   ║\n"
        "║                                                                ║\n"
        "║ Solutions:                                                     ║\n"
        "║ 1. Use bound_array<T> or bound_map<K,V> for zero-copy       ║\n"
        "║ 2. Use std::vector<T> (by value) if copying is acceptable     ║\n"
        "║ 3. std::vector<T>& writes the callee's result back            ║\n"
        "║ 4. #define JAI_ALLOW_MISLEADING_REFERENCE_BINDING before      ║\n"
        "║    including JaiScript headers to allow                        ║\n"
        "╚════════════════════════════════════════════════════════════════╝\n"
    );
}

#endif

} // namespace binding_traits

namespace detail {
    template<typename T> struct held_by_unique_ptr : std::false_type {};
    template<typename E, typename D> struct held_by_unique_ptr<std::unique_ptr<E, D>> : std::true_type {};

    // A copy assignment the binder can instantiate without risk: a trivial one, a string's, or a standard container's whose
    // elements have one. Any other class's is unknowable from outside it (an implicitly declared assignment that copies a
    // container of a move-only type, anywhere among its members, reports assignable and fails to compile once used).
    template<typename T>
    consteval bool copy_assignment_proven() {
        if constexpr (held_by_unique_ptr<T>::value) {
            return false;
        } else if constexpr (std::is_trivially_copy_assignable_v<T>) {
            return true;
        } else if constexpr (requires { typename T::traits_type; typename T::value_type; typename T::allocator_type; }) {
            return std::is_trivially_copy_assignable_v<typename T::value_type>;
        } else if constexpr (requires { typename T::key_type; typename T::mapped_type; typename T::allocator_type; }) {
            return copy_assignment_proven<typename T::key_type>() && copy_assignment_proven<typename T::mapped_type>();
        } else if constexpr (requires { typename T::key_type; typename T::allocator_type; }) {
            return copy_assignment_proven<typename T::key_type>();
        } else if constexpr (requires { typename T::value_type; typename T::allocator_type; }) {
            return copy_assignment_proven<typename T::value_type>();
        } else if constexpr (requires { typename T::value_type; std::tuple_size<T>::value; }) {
            return copy_assignment_proven<typename T::value_type>();
        } else {
            return false;
        }
    }
}

// Whether an assignment through a reference to a bound object of class T copy-assigns it; otherwise it raises Cannot assign to
// a reference to 'T': it has no copy assignment script can use. Trivially assignable classes, strings and standard containers
// of such answer on their own. Any other copy-assignable class says so, because C++ cannot tell from outside a class whether its
// assignment compiles: template<> struct jai::copy_assignment<Monster> : std::true_type {};
template<typename T>
struct copy_assignment : std::bool_constant<detail::copy_assignment_proven<T>()> {};

} // namespace jai