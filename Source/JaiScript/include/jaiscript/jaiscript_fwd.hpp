#pragma once

#ifndef __JAISCRIPT_JAISCRIPT_FWD_HPP__
#define __JAISCRIPT_JAISCRIPT_FWD_HPP__

// Forced inlining for hot-path leaf functions the compiler's inline budget abandons in
// the giant interpreter/vm TUs (each use is WPR-justified — cite the row). GCC/Clang's
// always_inline is the same hard demand as MSVC's __forceinline; plain `inline` is the
// portable floor for anything else.
#if defined(_MSC_VER)
#define JAI_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define JAI_FORCEINLINE inline __attribute__((always_inline))
#else
#define JAI_FORCEINLINE inline
#endif

namespace jai {
    
    class engine;
    class script_value;
    class type_info;
    class wrapped_function;
    class class_definition;
    class class_instance;
    template<typename T> class dynamic_binder;
    class variable_scope;
    class parse_error;
    class runtime_error;
    class serialization_error;
    
    struct source_location;
    
    enum class script_value_type {
        jai_null_type,
        jai_any_type,            // Dynamic type (var keyword) - any value allowed
        jai_int_type,
        jai_float_type,
        jai_string_type,
        jai_char_type,
        jai_bool_type,
        jai_array_type,          // Array<T>
        jai_map_type,            // Map<K,V>
        jai_object_type,         // Object<T> - typed object
        jai_function_type,       // Function<ReturnType(Args...)>
        jai_reference_type,      // T& - Reference to another value
        jai_weak_ptr_type,       // weak_ptr<T>
        jai_shared_ptr_type,     // shared_ptr<T>
        jai_cpp_bound_type,      // C++ bound value
        jai_invalid_type         // Invalid/non-existent value (used as sentinel)
    };
    
} // namespace jai

#endif // __JAISCRIPT_JAISCRIPT_FWD_HPP__