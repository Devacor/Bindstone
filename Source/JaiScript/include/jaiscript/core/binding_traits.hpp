#pragma once

#include <type_traits>
#include <vector>
#include <map>
#include <tuple>

namespace jai {

// Forward declarations
class script_value;
template<typename T> class bound_array;
template<typename K, typename V> class bound_map;

namespace binding_traits {

// ============ Container Reference Detection ============

// Detect std::vector references (but exclude script_value vectors which are native)
template<typename T>
struct is_std_vector_ref : std::false_type {};

template<typename T>
struct is_std_vector_ref<std::vector<T>&> : std::bool_constant<!std::is_same_v<T, script_value>> {};

template<typename T>
struct is_std_vector_ref<const std::vector<T>&> : std::bool_constant<!std::is_same_v<T, script_value>> {};

// Detect std::map references (but exclude script_value maps which are native)
template<typename T>
struct is_std_map_ref : std::false_type {};

template<typename K, typename V>
struct is_std_map_ref<std::map<K, V>&> : std::bool_constant<
    !(std::is_same_v<K, script_value> && std::is_same_v<V, script_value>)> {};

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
struct should_reject_binding : std::false_type {};

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
struct should_reject_binding : std::bool_constant<
    function_param_checker<std::decay_t<Func>>::has_rejected_refs
> {};

template<typename Func>
inline void check_binding_validity(const std::string& name) {
    static_assert(!function_param_checker<std::decay_t<Func>>::has_rejected_refs,
        "\n"
        "╔════════════════════════════════════════════════════════════════╗\n"
        "║                  JaiScript Binding Error                       ║\n"
        "╠════════════════════════════════════════════════════════════════╣\n"
        "║ Cannot bind functions with container reference parameters:     ║\n"
        "║   • const std::vector<T>&                                      ║\n"
        "║   • std::vector<T>&                                            ║\n"
        "║   • const std::map<K,V>&                                       ║\n"
        "║   • std::map<K,V>&                                             ║\n"
        "║                                                                ║\n"
        "║ Why? These create misleading copies, not true references!     ║\n"
        "║                                                                ║\n"
        "║ Solutions:                                                     ║\n"
        "║ 1. Use bound_array<T> or bound_map<K,V> for zero-copy       ║\n"
        "║ 2. Use std::vector<T> (by value) if copying is acceptable     ║\n"
        "║ 3. #define JAI_ALLOW_MISLEADING_REFERENCE_BINDING before      ║\n"
        "║    including JaiScript headers to allow (but beware:          ║\n"
        "║    mutations won't affect the original!)                       ║\n"
        "╚════════════════════════════════════════════════════════════════╝\n"
    );
}

#endif

// ============ Type Name Extraction for Better Error Messages ============

template<typename T>
struct type_name {
    static constexpr const char* value = "unknown";
};

template<typename T>
struct type_name<std::vector<T>&> {
    static constexpr const char* value = "std::vector<T>&";
};

template<typename T>
struct type_name<const std::vector<T>&> {
    static constexpr const char* value = "const std::vector<T>&";
};

template<typename K, typename V>
struct type_name<std::map<K, V>&> {
    static constexpr const char* value = "std::map<K,V>&";
};

template<typename K, typename V>
struct type_name<const std::map<K, V>&> {
    static constexpr const char* value = "const std::map<K,V>&";
};

} // namespace binding_traits
} // namespace jai