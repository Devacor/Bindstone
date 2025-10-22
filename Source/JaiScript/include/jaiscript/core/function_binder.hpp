#pragma once

#include "types.hpp"
#include "value.hpp"
#include "conversion_registry.hpp"
#include "conversion_registry_impl.hpp"
#include "bound_array.hpp"
#include "bound_map.hpp"
#include "parameter_storage.hpp"
#include <functional>
#include <tuple>

// Forward declarations to avoid circular dependencies
namespace jai {
    class engine;
    
    // Helper function to convert custom types using engine's conversion registry
    // Implementation in engine_impl.hpp
    template<typename T>
    script_value convert_custom_type_with_registry(const T& t, engine* eng);
}
#include <type_traits>
#include <typeinfo>

namespace jai {

// Forward declarations
class class_instance;
class engine;

    // Type traits to help with function binding
    namespace detail {
        
        // Helper to convert C++ types to/from value
        template<typename T>
        struct value_converter {
            static T from(const script_value& v, engine* eng) {
                // Handle containers explicitly to avoid infinite recursion
                if constexpr (is_specialization_v<T, std::vector>) {
                    using element_type = typename T::value_type;
                    return conversions::convert_script_array_to_vector<element_type>(v, eng);
                } else if constexpr (is_specialization_v<T, std::map>) {
                    using key_type = typename T::key_type;
                    using value_type = typename T::mapped_type;
                    return conversions::convert_script_map_to_stdmap<key_type, value_type>(v, eng);
                } else if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string>) {
                    // For custom classes, try to extract directly from shared_ptr to avoid extra copies
                    if (v.is_object()) {
                        try {
                            // Try to get as shared_ptr and dereference - this creates only one copy
                            auto ptr = v.as<std::shared_ptr<T>>();
                            return *ptr;  // Single copy here
                        } catch (const std::exception&) {
                            // Fall back to conversion registry if direct extraction fails
                            return v.as<T>();
                        }
                    } else {
                        // Not an object, use standard conversion
                        return v.as<T>();
                    }
                } else {
                    // For basic types, use standard conversion
                    return v.as<T>();
                }
            }
            
            static script_value to(const T& t, engine* eng) {
                // Handle standard containers explicitly since script_value doesn't have constructors for them
                if constexpr (is_specialization_v<T, std::vector>) {
                    // For vectors, use the conversion utility with engine for registry access
                    using element_type = typename T::value_type;
                    return conversions::convert_vector_to_script_array<element_type>(t, eng);
                } else if constexpr (is_specialization_v<T, std::map>) {
                    // For maps, use the conversion utility
                    using key_type = typename T::key_type;
                    using value_type = typename T::mapped_type;
                    return conversions::convert_stdmap_to_script_map<key_type, value_type>(t, eng);
                } else if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string>) {
                    // For user classes, use the conversion registry if available
                    // Implementation moved to engine_impl.hpp to avoid circular dependencies
                    if (!eng) {
                        throw runtime_error("Engine reference required for custom type conversion");
                    }
                    return convert_custom_type_with_registry<T>(t, eng);
                } else {
                    // For basic types, use the script_value constructor with engine
                    if (!eng) {
                        throw runtime_error("Engine reference required for script_value creation");
                    }
                    return script_value(t, get_engine_weak_ptr(eng));
                }
            }
        };
        
        // Specialization for int (convert to script_int/int64_t)
        template<>
        struct value_converter<int> {
            static int from(const script_value& v, engine* eng) {
                return v.as<int>();
            }
            
            static script_value to(const int& t, engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(static_cast<script_int>(t), get_engine_weak_ptr(eng));
            }
        };
        
        // Specialization for float (convert to script_float/double)
        template<>
        struct value_converter<float> {
            static float from(const script_value& v, engine* eng) {
                return v.as<float>();
            }
            
            static script_value to(const float& t, engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(static_cast<script_float>(t), get_engine_weak_ptr(eng));
            }
        };
        
        // Specialization for double - handle int-to-double conversion
        template<>
        struct value_converter<double> {
            static double from(const script_value& v, engine* eng) {
                // Use the implicit conversion operator we just added
                return static_cast<double>(v);
            }
            
            static script_value to(const double& t, engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(t, get_engine_weak_ptr(eng));  // script_float is double
            }
        };
        
        // Specialization for script_value itself (no conversion needed!)
        template<>
        struct value_converter<script_value> {
            static const script_value& from(const script_value& v, engine* eng) {
                return v;
            }
            
            static script_value to(const script_value& v, engine* eng) {
                return v;  // Return as-is, no wrapping!
            }
        };
        
        // Specialization for std::shared_ptr<T> - auto-unwrap to T
        template<typename T>
        struct value_converter<std::shared_ptr<T>> {
            static std::shared_ptr<T> from(const script_value& v, engine* eng) {
                return v.as<std::shared_ptr<T>>();
            }
            
            static script_value to(const std::shared_ptr<T>& ptr, engine* eng) {
                if (!ptr) {
                    if (!eng) {
                        throw runtime_error("Engine reference required for null script_value creation");
                    }
                    return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // Return null for nullptr
                }
                
                // Auto-unwrap: dereference the shared_ptr and convert the underlying object
                // This delegates to the base value_converter<T> which may have custom handling
                return value_converter<T>::to(*ptr, eng);
            }
        };
        
        // Specializations for reference types - ZERO-COPY for built-in types
        template<typename T>
        struct value_converter<T&> {
            static T& from(const script_value& v, engine* eng) {
                // Get the actual target value (dereferences if v is a reference)
                script_value& target = const_cast<script_value&>(v).deref();
                
                // ZERO-COPY: Direct mutable access to stored values for built-in types
                if constexpr (std::is_same_v<T, script_int> || std::is_same_v<T, int64_t>) {
                    // Direct reference to stored int - ZERO COPY
                    if (target.type() != script_value_type::jai_int_type) {
                        throw runtime_error("Expected int for int64_t& parameter");
                    }
                    return std::get<script_int>(target.storage_);
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    // Direct reference to stored string - ZERO COPY
                    if (target.type() != script_value_type::jai_string_type) {
                        throw runtime_error("Expected string for string& parameter");
                    }
                    return std::get<script_string>(target.storage_);
                }
                else if constexpr (std::is_same_v<T, std::vector<script_value>>) {
                    // Direct reference to stored array - ZERO COPY
                    if (target.type() != script_value_type::jai_array_type) {
                        throw runtime_error("Expected array for vector<script_value>& parameter");
                    }
                    return *std::get<std::shared_ptr<std::vector<script_value>>>(target.storage_);
                }
                else if constexpr (std::is_same_v<T, std::map<script_value, script_value>>) {
                    // Direct reference to stored map - ZERO COPY
                    if (target.type() != script_value_type::jai_map_type) {
                        throw runtime_error("Expected map for map<script_value,script_value>& parameter");
                    }
                    return *std::get<std::shared_ptr<std::map<script_value, script_value>>>(target.storage_);
                }
                else {
                    // Fall back to the as<T&>() method for other types (including int conversions)
                    return target.as<T&>();
                }
            }
            
            static script_value to(T& t, engine* eng) {
                // Delegate to the const T& version with the engine parameter
                return value_converter<const T&>::to(t, eng);
            }
        };
        
        // Specialization for const references
        template<typename T>
        struct value_converter<const T&> {
            static const T& from(const script_value& v, engine* eng) {
                // For std::string, return reference directly
                if constexpr (std::is_same_v<T, std::string>) {
                    return v.as_string();
                }
                // For std::vector<script_value> arrays, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::vector<script_value>>) {
                    return v.as_array();
                }
                // For std::map<script_value, script_value> maps, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::map<script_value, script_value>>) {
                    return v.as_map();
                }
                // For std::vector<T> containers, use conversion utility
                else if constexpr (is_specialization_v<T, std::vector>) {
                    auto* storage = detail::parameter_storage::current();
                    if (!storage) {
                        throw runtime_error("No parameter storage available for const reference conversion");
                    }
                    T& temp = storage->allocate<T>();
                    using element_type = typename T::value_type;
                    temp = conversions::convert_script_array_to_vector<element_type>(v, eng);
                    return temp;
                }
                // For std::map<K,V> containers, use conversion utility
                else if constexpr (is_specialization_v<T, std::map>) {
                    auto* storage = detail::parameter_storage::current();
                    if (!storage) {
                        throw runtime_error("No parameter storage available for const reference conversion");
                    }
                    T& temp = storage->allocate<T>();
                    using key_type = typename T::key_type;
                    using value_type = typename T::mapped_type;
                    temp = conversions::convert_script_map_to_stdmap<key_type, value_type>(v, eng);
                    return temp;
                }
                // For custom classes, extract shared_ptr and return reference to avoid copy
                else if constexpr (std::is_class_v<T> && 
                                  !std::is_same_v<T, std::string> &&
                                  !is_specialization_v<T, std::vector> &&
                                  !is_specialization_v<T, std::map>) {
                    auto ptr = v.as<std::shared_ptr<T>>();
                    return *ptr;
                } else {
                    // For basic types (int, float, bool, etc.), need to make a copy
                    auto* storage = detail::parameter_storage::current();
                    if (!storage) {
                        throw runtime_error("No parameter storage available for const reference conversion");
                    }
                    T& temp = storage->allocate<T>();
                    temp = v.as<T>();
                    return temp;
                }
            }
            
            static script_value to(const T& t, engine* eng) {
                // Check if this is a custom class type (excluding standard containers and string)
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, use the conversion registry if available
                    // Implementation moved to engine_impl.hpp to avoid circular dependencies
                    if (!eng) {
                        throw runtime_error("Engine reference required for custom type conversion");
                    }
                    return convert_custom_type_with_registry<T>(t, eng);
                } else {
                    // For basic types and standard containers, use the script_value constructor with engine
                    if (!eng) {
                        throw runtime_error("Engine reference required for script_value creation");
                    }
                    return script_value(t, get_engine_weak_ptr(eng));
                }
            }
        };
        
        // Specialization for void
        template<>
        struct value_converter<void> {
            static script_value to(engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                // Create null/void value with engine reference
                return script_value(std::monostate{}, get_engine_weak_ptr(eng));
            }
        };
        
        // Specialization for bound_array<T> - zero-copy wrapper
        template<typename T>
        struct value_converter<bound_array<T>> {
            static bound_array<T> from(const script_value& v, engine* eng) {
                if (!v.is_array()) {
                    throw runtime_error("Cannot convert non-array to bound_array<T>");
                }
                // Create by value - will deep copy
                return bound_array<T>(v);
            }
            
            static script_value to(const bound_array<T>& arr, engine* eng) {
                // If the array owns its data, return the owned value
                if (arr.is_owned()) {
                    return arr.as_script_value();
                }
                // Otherwise, we need to create a new script_value with the array data
                // This will copy the array elements
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(arr.to_vector(), get_engine_weak_ptr(eng));
            }
        };
        
        // Note: bound_array<T>&, const bound_array<T>& are handled by create_argument() 
        // in the function call path, not through value_converter. They create stack-based
        // objects for zero-copy semantics without needing thread_local storage.
        
        // Specialization for bound_map<K,V> - zero-copy wrapper
        template<typename K, typename V>
        struct value_converter<bound_map<K, V>> {
            static bound_map<K, V> from(const script_value& v, engine* eng) {
                if (!v.is_map()) {
                    throw runtime_error("Cannot convert non-map to bound_map<K,V>");
                }
                // Create by value - will deep copy
                return bound_map<K, V>(v);
            }
            
            static script_value to(const bound_map<K, V>& map, engine* eng) {
                // If the map owns its data, return the owned value
                if (map.is_owned()) {
                    return map.as_script_value();
                }
                // Otherwise, we need to create a new script_value with the map data
                return script_value(map.to_map());
            }
        };
        
        // Note: bound_map<K,V>& and const bound_map<K,V>& are handled directly
        // in the function binder's call_with_bound_maps functions to ensure
        // proper stack-based lifetime management without thread_local storage.
        
        // Specialization for std::map<K,V> return values - converts to script map
        template<typename K, typename V>
        struct value_converter<std::map<K, V>> {
            static std::map<K, V> from(const script_value& v, engine* eng) {
                if (!v.is_map()) {
                    throw runtime_error("Cannot convert non-map to std::map<K,V>");
                }
                // Convert script map to std::map
                std::map<K, V> result;
                auto& script_map = v.as_map();
                for (const auto& [key, value] : script_map) {
                    K converted_key = value_converter<K>::from(key, eng);
                    V converted_value = value_converter<V>::from(value, eng);
                    result[converted_key] = converted_value;
                }
                return result;
            }
            
            static script_value to(const std::map<K, V>& stdmap, engine* eng) {
                // Use the engine-aware conversion function
                return conversions::convert_stdmap_to_script_map(stdmap, eng);
            }
        };
        
        // Helper to extract function traits
        template<typename T>
        struct function_traits;
        
        // Function pointer
        template<typename R, typename... Args>
        struct function_traits<R(*)(Args...)> {
            using return_type = R;
            using argument_types = std::tuple<Args...>;
            static constexpr size_t arity = sizeof...(Args);
        };
        
        // member function pointer
        template<typename C, typename R, typename... Args>
        struct function_traits<R(C::*)(Args...)> {
            using return_type = R;
            using class_type = C;
            using argument_types = std::tuple<Args...>;
            static constexpr size_t arity = sizeof...(Args);
        };
        
        // Const member function pointer
        template<typename C, typename R, typename... Args>
        struct function_traits<R(C::*)(Args...) const> {
            using return_type = R;
            using class_type = C;
            using argument_types = std::tuple<Args...>;
            static constexpr size_t arity = sizeof...(Args);
        };
        
        // Function types (not pointers)
        template<typename R, typename... Args>
        struct function_traits<R(Args...)> {
            using return_type = R;
            using argument_types = std::tuple<Args...>;
            static constexpr size_t arity = sizeof...(Args);
        };
        
        // Functors and lambdas
        template<typename T>
        struct function_traits : function_traits<decltype(&T::operator())> {};
        
        // Reference types - strip reference and forward to base type
        template<typename T>
        struct function_traits<T&> : function_traits<T> {};
        
        template<typename T>
        struct function_traits<T&&> : function_traits<T> {};
        
        
        // Helper to unpack arguments and call function
        template<typename Func, typename Tuple, size_t... Is>
        auto call_with_tuple(Func&& f, const Tuple& t, std::index_sequence<Is...>) {
            return f(std::get<Is>(t)...);
        }
        
        // Convert vector of Values to tuple of C++ types
        template<typename... Args, size_t... Is>
        std::tuple<Args...> values_to_tuple(const std::vector<script_value>& values, std::index_sequence<Is...>, engine* eng) {
            return std::make_tuple(value_converter<Args>::from(values[Is], eng)...);
        }
        
        // Helper function to call a function with converted arguments
        template<typename Func, typename ArgsTuple, size_t... Is>
        auto call_with_converted_args(Func&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            using traits = function_traits<std::decay_t<Func>>;
            using return_type = typename traits::return_type;
            
            if constexpr (std::is_void_v<return_type>) {
                // Void function
                func(value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
                return value_converter<void>::to(eng);
            } else {
                // Non-void function
                auto result = func(value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
                return value_converter<return_type>::to(result, eng);
            }
        }
        
    } // namespace detail
    
    // Helper to detect if function has reference parameters
    template<typename Tuple, size_t... Is>
    constexpr bool tuple_has_references_impl(std::index_sequence<Is...>) {
        return (std::is_reference_v<std::tuple_element_t<Is, Tuple>> || ...);
    }
    
    template<typename Tuple>
    constexpr bool tuple_has_references() {
        return tuple_has_references_impl<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }

    // Main function binder
    template<typename Func>
    class FunctionBinder {
    public:
        explicit FunctionBinder(Func f, engine* eng) : func_(std::forward<Func>(f)), engine_(eng) {}
        
        script_function bind() {
            using traits = detail::function_traits<Func>;
            using args_tuple = typename traits::argument_types;
            
            // IMPORTANT: capture func_ by value and engine pointer, not this!
            // The FunctionBinder is temporary and will be destroyed after add_function returns
            
            // Check if function has reference parameters
            if constexpr (tuple_has_references<args_tuple>()) {
                // Use reference-aware calling for functions with reference parameters
                return [func = func_, eng = engine_](const std::vector<script_value>& args) -> script_value {
                    using traits = detail::function_traits<Func>;
                    using return_type = typename traits::return_type;
                    using args_tuple = typename traits::argument_types;
                    
                    // Check argument count
                    if (args.size() != traits::arity) {
                        throw runtime_error("Function expects " + std::to_string(traits::arity) + 
                                         " arguments, got " + std::to_string(args.size()));
                    }
                    
                    // Call with reference support
                    return FunctionBinder::call_with_reference_support<return_type, args_tuple>(func, args, std::make_index_sequence<traits::arity>{}, eng);
                };
            } else {
                // Use standard calling for functions without reference parameters
                return [func = func_, eng = engine_](const std::vector<script_value>& args) -> script_value {
                    using traits = detail::function_traits<Func>;
                    using return_type = typename traits::return_type;
                    using args_tuple = typename traits::argument_types;
                    
                    // Check argument count
                    if (args.size() != traits::arity) {
                        throw runtime_error("Function expects " + std::to_string(traits::arity) + 
                                         " arguments, got " + std::to_string(args.size()));
                    }
                    
                    // Call the function with unpacked arguments
                    return FunctionBinder::call_impl_static<return_type, args_tuple>(func, args, std::make_index_sequence<traits::arity>{}, eng);
                };
            }
        }
        
    private:
        Func func_;
        engine* engine_;
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static script_value call_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            if constexpr (std::is_void_v<R>) {
                // Void function
                call_void_impl_static<ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
                return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // Return null for void
            } else {
                // Non-void function
                auto result = call_non_void_impl_static<R, ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
                return detail::value_converter<R>::to(result, eng);
            }
        }
        
        template<typename ArgsTuple, typename F, size_t... Is>
        static void call_void_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            // Create parameter storage on stack
            detail::parameter_storage storage;
            detail::parameter_storage::scope_guard guard(&storage);
            
            // Call function with conversions using the storage
            func(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
        }
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static R call_non_void_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            // Create parameter storage on stack
            detail::parameter_storage storage;
            detail::parameter_storage::scope_guard guard(&storage);
            
            // Call function with conversions using the storage
            return func(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
        }
        
        // Reference-aware calling methods
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static script_value call_with_reference_support(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            if constexpr (std::is_void_v<R>) {
                // Void function with reference support
                call_with_reference_support_void<ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
                return script_value(std::monostate{}, get_engine_weak_ptr(eng)); // Return null for void
            } else {
                // Non-void function with reference support
                auto result = call_with_reference_support_non_void<R, ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
                return detail::value_converter<R>::to(result, eng);
            }
        }
        
        template<typename ArgsTuple, typename F, size_t... Is>
        static void call_with_reference_support_void(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            // Create bound_map objects on the stack for zero-copy semantics
            // This approach supports recursion naturally
            call_with_bound_maps_void<ArgsTuple, F, Is...>(std::forward<F>(func), args, eng);
        }
        
        // Helper to create bound_map objects on stack and call void function
        template<typename ArgsTuple, typename F, size_t... Is>
        static void call_with_bound_maps_void(F&& func, const std::vector<script_value>& args, engine* eng) {
            // Create tuple of arguments, handling bound_map types specially
            auto arg_tuple = std::make_tuple(create_argument<std::tuple_element_t<Is, ArgsTuple>>(args[Is], eng)...);
            
            // Call function with unpacked arguments
            std::apply(std::forward<F>(func), arg_tuple);
        }
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static R call_with_reference_support_non_void(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            // Create bound_map objects on the stack for zero-copy semantics
            // This approach supports recursion naturally
            return call_with_bound_maps<R, ArgsTuple, F, Is...>(std::forward<F>(func), args, eng);
        }
        
        // Helper trait to detect bound_array types
        template<typename T>
        struct is_bound_array_ref : std::false_type {};
        
        template<typename T>
        struct is_bound_array_ref<bound_array<T>&> : std::true_type {
            using value_type = T;
        };
        
        template<typename T>
        struct is_bound_array_ref<const bound_array<T>&> : std::true_type {
            using value_type = T;
        };
        
        // Helper to check if a type is bound_array (not a reference)
        template<typename T>
        struct is_bound_array_type : std::false_type {};
        
        template<typename T>
        struct is_bound_array_type<bound_array<T>> : std::true_type {};
        
        // Helper to check if a type is bound_map (not a reference)
        template<typename T>
        struct is_bound_map_type : std::false_type {};
        
        template<typename K, typename V>
        struct is_bound_map_type<bound_map<K, V>> : std::true_type {};
        
        // Helper trait to detect bound_map types
        template<typename T>
        struct is_bound_map_ref : std::false_type {};
        
        template<typename K, typename V>
        struct is_bound_map_ref<bound_map<K, V>&> : std::true_type {
            using key_type = K;
            using value_type = V;
        };
        
        template<typename K, typename V>
        struct is_bound_map_ref<const bound_map<K, V>&> : std::true_type {
            using key_type = K;
            using value_type = V;
        };
        
        // Helper to create bound_map objects on stack and call function
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static R call_with_bound_maps(F&& func, const std::vector<script_value>& args, engine* eng) {
            // Create tuple of arguments, handling bound_map types specially
            auto arg_tuple = std::make_tuple(create_argument<std::tuple_element_t<Is, ArgsTuple>>(args[Is], eng)...);
            
            // Call function with unpacked arguments
            return std::apply(std::forward<F>(func), arg_tuple);
        }
        
        // Create individual arguments, handling bound_array and bound_map types specially
        template<typename T>
        static auto create_argument(const script_value& arg, engine* eng) {
            if constexpr (is_bound_array_ref<T>::value) {
                using V = typename is_bound_array_ref<T>::value_type;
                
                if (!arg.is_array()) {
                    throw runtime_error("Cannot convert non-array to bound_array<T>&");
                }
                // Create bound_array object on stack - zero-copy reference to script_array
                return bound_array<V>(const_cast<script_value&>(arg).as_array(), get_engine_weak_ptr(eng));
            } else if constexpr (is_bound_map_ref<T>::value) {
                using K = typename is_bound_map_ref<T>::key_type;
                using V = typename is_bound_map_ref<T>::value_type;
                
                if (!arg.is_map()) {
                    throw runtime_error("Cannot convert non-map to bound_map<K,V>&");
                }
                // Create bound_map object on stack - zero-copy reference to script_map
                return bound_map<K, V>(const_cast<script_value&>(arg).as_map(), get_engine_weak_ptr(eng));
            } else if constexpr (std::is_reference_v<T>) {
                // For reference parameters, work with the dereferenced value
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;
                
                // Get the actual value (deref handles references automatically)
                const script_value& actual_arg = arg.deref();
                
                // For custom classes, get shared_ptr and return reference to the object
                // Exclude string, bound_array, bound_map, and built-in container types which have special handling
                if constexpr (std::is_class_v<base_type> && 
                            !std::is_same_v<base_type, std::string> &&
                            !is_bound_array_type<base_type>::value &&
                            !is_bound_map_type<base_type>::value &&
                            !is_specialization_v<base_type, std::vector> &&
                            !is_specialization_v<base_type, std::map>) {
                    auto ptr = actual_arg.as<std::shared_ptr<base_type>>();
                    return std::ref(*ptr);
                } else {
                    // For built-in types and special types, use the non-const as<T&>() method
                    return const_cast<script_value&>(actual_arg).as<T>();
                }
            } else {
                // For value parameters, use normal conversion which creates a copy
                return detail::value_converter<T>::from(arg, eng);
            }
        }
        
        // Process individual arguments
        template<typename T>
        static T process_argument(const script_value& arg, size_t index, engine* eng) {
            return detail::value_converter<T>::from(arg, eng);
        }
    };
    
    // Helper function to create a binder
    template<typename Func>
    FunctionBinder<Func> make_functionBinder(Func&& f, engine* eng) {
        return FunctionBinder<Func>(std::forward<Func>(f), eng);
    }
    
} // namespace jai