#pragma once

#ifndef __JAISCRIPT_JAISCRIPT_FWD_HPP__
#define __JAISCRIPT_JAISCRIPT_FWD_HPP__

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