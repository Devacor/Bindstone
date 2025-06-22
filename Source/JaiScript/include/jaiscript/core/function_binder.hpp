#pragma once

#include "types.hpp"
#include "value.hpp"
#include <functional>
#include <tuple>
#include <type_traits>
#include <typeinfo>

namespace JaiScript {

// Forward declarations
class ClassInstance;

    // Type traits to help with function binding
    namespace detail {
        
        // Helper to convert C++ types to/from Value
        template<typename T>
        struct ValueConverter {
            static T from(const Value& v) {
                return v.as<T>();
            }
            
            static Value to(const T& t) {
                // Check if this is a custom class type (excluding standard containers and string)
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, create an object value
                    auto sharedObj = std::make_shared<T>(t);
                    return Value::makeObject(typeid(T).name(), std::static_pointer_cast<void>(sharedObj));
                } else {
                    // For basic types and standard containers, use the Value constructor
                    return Value(t);
                }
            }
        };
        
        // Specialization for int (convert to Int/int64_t)
        template<>
        struct ValueConverter<int> {
            static int from(const Value& v) {
                return v.as<int>();
            }
            
            static Value to(const int& t) {
                return Value(static_cast<Int>(t));
            }
        };
        
        // Specialization for float (convert to Float/double)
        template<>
        struct ValueConverter<float> {
            static float from(const Value& v) {
                return v.as<float>();
            }
            
            static Value to(const float& t) {
                return Value(static_cast<Float>(t));
            }
        };
        
        // Specializations for reference types
        template<typename T>
        struct ValueConverter<T&> {
            static T& from(const Value& v) {
                // For std::string, return reference directly (const_cast for non-const ref)
                if constexpr (std::is_same_v<T, std::string>) {
                    return const_cast<T&>(v.asString());
                }
                // For std::vector<Value> arrays, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::vector<Value>>) {
                    return const_cast<T&>(v.asArray());
                }
                // For std::map<Value, Value> maps, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::map<Value, Value>>) {
                    return const_cast<T&>(v.asMap());
                }
                // For std::shared_ptr<UserType>&, we need special handling
                else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                    // This is problematic - we can't return a reference to a temporary shared_ptr
                    // The shared_ptr is extracted from Value, but we need a persistent reference
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
            
            static Value to(T& t) {
                // For reference returns to arbitrary user types, try to return the object as wrapped instance
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, create an object value
                    auto sharedObj = std::make_shared<T>(t);
                    return Value::makeObject(typeid(T).name(), sharedObj);
                } else {
                    // For basic types and standard containers, return by value
                    return Value(t);
                }
            }
        };
        
        // Specialization for const references
        template<typename T>
        struct ValueConverter<const T&> {
            static const T& from(const Value& v) {
                // For std::string, return reference directly
                if constexpr (std::is_same_v<T, std::string>) {
                    return v.asString();
                }
                // For std::vector<Value> arrays, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::vector<Value>>) {
                    return v.asArray();
                }
                // For std::map<Value, Value> maps, return reference to avoid copy
                else if constexpr (std::is_same_v<T, std::map<Value, Value>>) {
                    return v.asMap();
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
            
            static Value to(const T& t) {
                // Check if this is a custom class type (excluding standard containers and string)
                if constexpr (std::is_class_v<T> && 
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    // For user classes, create an object value
                    auto sharedObj = std::make_shared<T>(t);
                    return Value::makeObject(typeid(T).name(), std::static_pointer_cast<void>(sharedObj));
                } else {
                    // For basic types and standard containers, use the Value constructor
                    return Value(t);
                }
            }
        };
        
        // Specialization for void
        template<>
        struct ValueConverter<void> {
            static Value to() {
                return Value(); // null/void
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
        
        // Member function pointer
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
        std::tuple<Args...> values_to_tuple(const std::vector<Value>& values, std::index_sequence<Is...>) {
            return std::make_tuple(ValueConverter<Args>::from(values[Is])...);
        }
        
    } // namespace detail
    
    // Main function binder
    template<typename Func>
    class FunctionBinder {
    public:
        explicit FunctionBinder(Func f) : func_(std::forward<Func>(f)) {}
        
        ScriptFunction bind() {
            // IMPORTANT: Capture func_ by value, not this!
            // The FunctionBinder is temporary and will be destroyed after addFunction returns
            return [func = func_](const std::vector<Value>& args) -> Value {
                using traits = detail::function_traits<Func>;
                using return_type = typename traits::return_type;
                using args_tuple = typename traits::argument_types;
                
                // Check argument count
                if (args.size() != traits::arity) {
                    throw RuntimeError("Function expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the function with unpacked arguments
                return FunctionBinder::call_impl_static<return_type, args_tuple>(func, args, std::make_index_sequence<traits::arity>{});
            };
        }
        
    private:
        Func func_;
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static Value call_impl_static(F&& func, const std::vector<Value>& args, std::index_sequence<Is...>) {
            if constexpr (std::is_void_v<R>) {
                // Void function
                call_void_impl_static<ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{});
                return Value(); // Return null for void
            } else {
                // Non-void function
                auto result = call_non_void_impl_static<R, ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{});
                return detail::ValueConverter<R>::to(result);
            }
        }
        
        template<typename ArgsTuple, typename F, size_t... Is>
        static void call_void_impl_static(F&& func, const std::vector<Value>& args, std::index_sequence<Is...>) {
            func(detail::ValueConverter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
        }
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static R call_non_void_impl_static(F&& func, const std::vector<Value>& args, std::index_sequence<Is...>) {
            return func(detail::ValueConverter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is])...);
        }
    };
    
    // Helper function to create a binder
    template<typename Func>
    FunctionBinder<Func> makeFunctionBinder(Func&& f) {
        return FunctionBinder<Func>(std::forward<Func>(f));
    }
    
} // namespace JaiScript