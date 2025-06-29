#pragma once

#include "types.hpp"
#include "value.hpp"
#include <functional>
#include <tuple>
#include <type_traits>
#include <typeinfo>

namespace jai {

// Forward declarations
class class_instance;

    // Type traits to help with function binding
    namespace detail {
        
        // Helper to convert C++ types to/from value
        template<typename T>
        struct value_converter {
            static T from(const script_value& v) {
                return v.as<T>();
            }
            
            static script_value to(const T& t) {
                // Check if this is a custom class type (excluding standard containers and string)
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, create raw C++ object (not class_instance)
                    auto sharedObj = std::make_shared<T>(t);
                    return script_value::make_cpp_object(typeid(T).name(), std::static_pointer_cast<void>(sharedObj));
                } else {
                    // For basic types and standard containers, use the script_value constructor
                    return script_value(t);
                }
            }
        };
        
        // Specialization for int (convert to script_int/int64_t)
        template<>
        struct value_converter<int> {
            static int from(const script_value& v) {
                return v.as<int>();
            }
            
            static script_value to(const int& t) {
                return script_value(static_cast<script_int>(t));
            }
        };
        
        // Specialization for float (convert to script_float/double)
        template<>
        struct value_converter<float> {
            static float from(const script_value& v) {
                return v.as<float>();
            }
            
            static script_value to(const float& t) {
                return script_value(static_cast<script_float>(t));
            }
        };
        
        // Specialization for double - handle int-to-double conversion
        template<>
        struct value_converter<double> {
            static double from(const script_value& v) {
                // Use the implicit conversion operator we just added
                return static_cast<double>(v);
            }
            
            static script_value to(const double& t) {
                return script_value(t);  // script_float is double
            }
        };
        
        // Specialization for script_value itself (no conversion needed!)
        template<>
        struct value_converter<script_value> {
            static const script_value& from(const script_value& v) {
                return v;
            }
            
            static script_value to(const script_value& v) {
                return v;  // Return as-is, no wrapping!
            }
        };
        
        // Specialization for std::shared_ptr<T> - auto-unwrap to T
        template<typename T>
        struct value_converter<std::shared_ptr<T>> {
            static std::shared_ptr<T> from(const script_value& v) {
                return v.as<std::shared_ptr<T>>();
            }
            
            static script_value to(const std::shared_ptr<T>& ptr) {
                if (!ptr) {
                    return script_value(); // Return null for nullptr
                }
                
                // Auto-unwrap: dereference the shared_ptr and convert the underlying object
                // This delegates to the base value_converter<T> which may have custom handling
                return value_converter<T>::to(*ptr);
            }
        };
        
        // Specializations for reference types
        template<typename T>
        struct value_converter<T&> {
            static T& from(const script_value& v) {
                // For std::string, return reference directly (const_cast for non-const ref)
                if constexpr (std::is_same_v<T, std::string>) {
                    return const_cast<T&>(v.as_string());
                }
                // For std::vector<script_value> arrays, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::vector<script_value>>) {
                    return const_cast<T&>(v.as_array());
                }
                // For std::map<script_value, script_value> maps, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::map<script_value, script_value>>) {
                    return const_cast<T&>(v.as_map());
                }
                // For std::shared_ptr<user_type>&, we need special handling
                else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                    // This is problematic - we can't return a reference to a temporary shared_ptr
                    // The shared_ptr is extracted from script_value, but we need a persistent reference
                    // We'd need to store it somewhere persistent (thread_local)
                    thread_local T temp = v.as<T>();
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
                    // For basic types, still need to make a copy and use thread_local
                    thread_local T temp = v.as<T>();
                    return temp;
                }
            }
            
            static script_value to(T& t) {
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, create raw C++ object (not class_instance)
                    auto sharedObj = std::make_shared<T>(t);
                    return script_value::make_cpp_object(typeid(T).name(), sharedObj);
                } else {
                    // For basic types and standard containers, return by value
                    return script_value(t);
                }
            }
        };
        
        // Specialization for const references
        template<typename T>
        struct value_converter<const T&> {
            static const T& from(const script_value& v) {
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
                // For custom classes, extract shared_ptr and return reference to avoid copy
                else if constexpr (std::is_class_v<T> && 
                                  !std::is_same_v<T, std::string> &&
                                  !is_specialization_v<T, std::vector> &&
                                  !is_specialization_v<T, std::map>) {
                    auto ptr = v.as<std::shared_ptr<T>>();
                    return *ptr;
                } else {
                    // For basic types (int, float, bool, etc.), still need to make a copy and use thread_local
                    thread_local T temp = v.as<T>();
                    return temp;
                }
            }
            
            static script_value to(const T& t) {
                // Check if this is a custom class type (excluding standard containers and string)
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, create raw C++ object (not class_instance)
                    auto sharedObj = std::make_shared<T>(t);
                    return script_value::make_cpp_object(typeid(T).name(), std::static_pointer_cast<void>(sharedObj));
                } else {
                    // For basic types and standard containers, use the script_value constructor
                    return script_value(t);
                }
            }
        };
        
        // Specialization for void
        template<>
        struct value_converter<void> {
            static script_value to() {
                return script_value(); // null/void
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
        std::tuple<Args...> values_to_tuple(const std::vector<script_value>& values, std::index_sequence<Is...>) {
            return std::make_tuple(value_converter<Args>::from(values[Is])...);
        }
        
        // Helper function to call a function with converted arguments
        template<typename Func, typename ArgsTuple, size_t... Is>
        auto call_with_converted_args(Func&& func, const std::vector<script_value>& args, std::index_sequence<Is...>) {
            using traits = function_traits<std::decay_t<Func>>;
            using return_type = typename traits::return_type;
            
            if constexpr (std::is_void_v<return_type>) {
                // Void function
                func(value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
                return value_converter<void>::to();
            } else {
                // Non-void function
                auto result = func(value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
                return value_converter<return_type>::to(result);
            }
        }
        
    } // namespace detail
    
    // Main function binder
    template<typename Func>
    class FunctionBinder {
    public:
        explicit FunctionBinder(Func f) : func_(std::forward<Func>(f)) {}
        
        script_function bind() {
            // IMPORTANT: capture func_ by value, not this!
            // The FunctionBinder is temporary and will be destroyed after add_function returns
            return [func = func_](const std::vector<script_value>& args) -> script_value {
                using traits = detail::function_traits<Func>;
                using return_type = typename traits::return_type;
                using args_tuple = typename traits::argument_types;
                
                // Check argument count
                if (args.size() != traits::arity) {
                    throw runtime_error("Function expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the function with unpacked arguments
                return FunctionBinder::call_impl_static<return_type, args_tuple>(func, args, std::make_index_sequence<traits::arity>{});
            };
        }
        
    private:
        Func func_;
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static script_value call_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>) {
            if constexpr (std::is_void_v<R>) {
                // Void function
                call_void_impl_static<ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{});
                return script_value(); // Return null for void
            } else {
                // Non-void function
                auto result = call_non_void_impl_static<R, ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{});
                return detail::value_converter<R>::to(result);
            }
        }
        
        template<typename ArgsTuple, typename F, size_t... Is>
        static void call_void_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>) {
            func(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
        }
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static R call_non_void_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>) {
            return func(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
        }
    };
    
    // Helper function to create a binder
    template<typename Func>
    FunctionBinder<Func> make_functionBinder(Func&& f) {
        return FunctionBinder<Func>(std::forward<Func>(f));
    }
    
} // namespace jai