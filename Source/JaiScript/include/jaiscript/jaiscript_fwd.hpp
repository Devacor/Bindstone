#pragma once

namespace jai {
    
    class engine;
    class script_value;
    class type_info;
    class wrapped_function;
    class class_definition;
    class class_instance;
    template<typename T> class class_builder;
    class variable_scope;
    class parse_error;
    class runtime_error;
    class serialization_error;
    
    struct source_location;
    
    enum class value_type {
        jai_null_type,
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
        jai_shared_ptr_type,     // shared_ptr<T>
        jai_weak_ptr_type        // weak_ptr<T>
    };
    
} // namespace jai