#pragma once

#include "types.hpp"
#include "value.hpp"
#include "conversion_registry.hpp"
#include "conversion_registry_impl.hpp"
#include "bound_array.hpp"
#include "bound_map.hpp"
#include "parameter_storage.hpp"
#include "runtime_errors.hpp"
#include <functional>
#include <optional>
#include <tuple>

// Forward declarations to avoid circular dependencies
namespace jai {
    class engine;

    // Helper function to convert custom types using engine's conversion registry
    // Implementation in engine_impl.hpp
    template<typename T>
    script_value convert_custom_type_with_registry(const T& t, engine* eng);

    // Helper function to convert C++ reference to script_value
    // For registered types, creates non-owning reference via make_cpp_bound
    // For unregistered types, falls back to copy semantics
    // Implementation in engine_impl.hpp
    template<typename T>
    script_value convert_reference_with_registry(T& t, engine* eng);

    // For registered classes, wraps a shared_ptr as an engine object (shared ownership -
    // identity and mutation preserved); returns nullopt for unregistered types.
    // Implementation in engine_impl.hpp
    template<typename T>
    std::optional<script_value> try_convert_shared_ptr_with_registry(const std::shared_ptr<T>& ptr, engine* eng);

    // engine is incomplete here; the dependent E defers the symbolize lookup to the
    // instantiation point (where engine.hpp is complete) for conforming compilers.
    template<typename E>
    uint64_t symbolize_text(E* eng, const char* text) {
        return eng ? eng->symbolize(text) : 0;
    }

    // Wraps a script FUNCTION value as a std::function<Sig> so script lambdas can be
    // assigned into C++ callback members/parameters. Implementation in engine_impl.hpp
    // (argument conversion needs the complete engine).
    template<typename Sig>
    std::function<Sig> convert_script_function(const script_value& fn);
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
        // Note: This generic template only works for copyable types.
        // Non-copyable types should be handled via shared_ptr<T> specialization or T& references.
        template<typename T>
        struct value_converter {
            static T from(const script_value& v, engine* eng)
                requires std::is_copy_constructible_v<T> || is_specialization_v<T, std::vector> || is_specialization_v<T, std::map>
            {
                // Handle containers explicitly to avoid infinite recursion
                if constexpr (is_specialization_v<T, std::vector>) {
                    using element_type = typename T::value_type;
                    return conversions::convert_script_array_to_vector<element_type>(v, eng);
                } else if constexpr (is_specialization_v<T, std::map>) {
                    using key_type = typename T::key_type;
                    using value_type = typename T::mapped_type;
                    return conversions::convert_script_map_to_stdmap<key_type, value_type>(v, eng);
                } else if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string>) {
                    if (v.is_object()) {
                        try {
                            auto ptr = v.as<std::shared_ptr<T>>();
                            return *ptr;
                        } catch (const std::exception&) {
                            return v.as<T>();
                        }
                    } else {
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
                } else if constexpr (std::is_enum_v<T>) {
                    if (!eng) {
                        throw runtime_error("Engine reference required for script_value creation");
                    }
                    return script_value(static_cast<script_int>(t), eng);
                } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T> ||
                                     std::is_same_v<T, bool> || std::is_same_v<T, std::string>) {
                    // For basic types, use the script_value constructor with engine
                    if (!eng) {
                        throw runtime_error("Engine reference required for script_value creation");
                    }
                    return script_value(t, eng);
                } else {
                    static_assert(std::is_integral_v<T> || std::is_floating_point_v<T> ||
                                  std::is_same_v<T, bool> || std::is_same_v<T, std::string> ||
                                  std::is_class_v<T> || std::is_enum_v<T>,
                        "value_converter<T>::to() does not support this type. "
                        "Register a custom converter or use a supported type.");
                    throw runtime_error("Unsupported type for script_value conversion");
                }
            }
        };
        
        // Specialization for std::function: script lambdas convert into C++ callbacks
        // (the spawn/update hook pattern). Reading a callback back yields null —
        // C++ callables are opaque to scripts.
        template<typename Sig>
        struct value_converter<std::function<Sig>> {
            static checked_result<std::function<Sig>> checked_from(const script_value& v, engine* eng) {
                return from(v, eng);
            }

            static std::function<Sig> from(const script_value& v, engine* /*eng*/) {
                if (!v.is_function()) {
                    return {};
                }
                return convert_script_function<Sig>(v);
            }

            static script_value to(const std::function<Sig>& /*v*/, engine* eng) {
                return script_value(std::monostate{}, eng);
            }
        };

        // Specialization for int (convert to script_int/int64_t)
        template<>
        struct value_converter<int> {
            // Non-throwing version - returns checked_result for error propagation
            static checked_result<int> checked_from(const script_value& v, engine* eng) {
                return v.checked_as<int>();
            }

            // Throwing version - for backward compatibility with existing bindings
            static int from(const script_value& v, engine* eng) {
                auto result = checked_from(v, eng);
                if (!result) {
                    throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
                }
                return result.value();
            }

            static script_value to(const int& t, engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(static_cast<script_int>(t), eng);
            }
        };

        // Specialization for float (convert to script_float/double)
        template<>
        struct value_converter<float> {
            // Non-throwing version
            static checked_result<float> checked_from(const script_value& v, engine* eng) {
                return v.checked_as<float>();
            }

            static float from(const script_value& v, engine* eng) {
                auto result = checked_from(v, eng);
                if (!result) {
                    throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
                }
                return result.value();
            }

            static script_value to(const float& t, engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(static_cast<script_float>(t), eng);
            }
        };

        // Specialization for double - handle int-to-double conversion
        template<>
        struct value_converter<double> {
            // Non-throwing version
            static checked_result<double> checked_from(const script_value& v, engine* eng) {
                return v.checked_as<double>();
            }

            static double from(const script_value& v, engine* eng) {
                auto result = checked_from(v, eng);
                if (!result) {
                    throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
                }
                return result.value();
            }

            static script_value to(const double& t, engine* eng) {
                if (!eng) {
                    throw runtime_error("Engine reference required for script_value creation");
                }
                return script_value(t, eng);  // script_float is double
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

        // Specialization for const script_value& - pass through directly
        template<>
        struct value_converter<const script_value&> {
            static const script_value& from(const script_value& v, engine* eng) {
                return v;
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
                    return script_value(std::monostate{}, eng); // Return null for nullptr
                }

                // Registered classes share ownership (identity + mutation preserved; also the
                // only lossless path for non-copyables like Node/Interface).
                if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string>) {
                    if (auto made = try_convert_shared_ptr_with_registry(ptr, eng)) {
                        return std::move(*made);
                    }
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
                
                // ZERO-COPY: Direct mutable access to stored values using safe accessors
                if constexpr (std::is_same_v<T, script_int> || std::is_same_v<T, int64_t>) {
                    // Direct reference to stored int - ZERO COPY
                    return target.as_int_ref();
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    // Direct reference to stored string - ZERO COPY
                    return target.as_string_ref();
                }
                else if constexpr (std::is_same_v<T, std::vector<script_value>>) {
                    // Direct reference to stored array - ZERO COPY
                    return target.as_array();
                }
                else if constexpr (std::is_same_v<T, std::map<script_value, script_value>>) {
                    // Direct reference to stored map - ZERO COPY
                    return target.as_map();
                }
                else {
                    // Fall back to the as<T&>() method for other types (including int conversions)
                    return target.as<T&>();
                }
            }
            
            static script_value to(T& t, engine* eng) {
                // avoids copying non-copyable types and preserves reference semantics
                if constexpr (std::is_class_v<T> &&
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                    return convert_reference_with_registry<T>(t, eng);
                }
                // For basic types, delegate to const T& version
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
                    auto* storage = detail::get_engine_parameter_storage(eng);
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
                    auto* storage = detail::get_engine_parameter_storage(eng);
                    if (!storage) {
                        throw runtime_error("No parameter storage available for const reference conversion");
                    }
                    T& temp = storage->allocate<T>();
                    using key_type = typename T::key_type;
                    using value_type = typename T::mapped_type;
                    temp = conversions::convert_script_map_to_stdmap<key_type, value_type>(v, eng);
                    return temp;
                }
                // For custom classes, alias the live object (handles class_instance
                // wrappers, cpp_bound references, and raw holders via as<T&>)
                else if constexpr (std::is_class_v<T> &&
                                  !std::is_same_v<T, std::string> &&
                                  !is_specialization_v<T, std::vector> &&
                                  !is_specialization_v<T, std::map>) {
                    return const_cast<script_value&>(v).deref().as<T&>();
                } else {
                    // For basic types (int, float, bool, etc.), need to make a copy
                    auto* storage = detail::get_engine_parameter_storage(eng);
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
                    if constexpr (std::is_copy_constructible_v<T>) {
                        return convert_custom_type_with_registry<T>(t, eng);
                    } else {
                        throw runtime_error("Cannot convert non-copyable type by const reference. "
                                           "Use T& or shared_ptr<T> instead.");
                    }
                } else {
                    // For basic types and standard containers, use the script_value constructor with engine
                    if (!eng) {
                        throw runtime_error("Engine reference required for script_value creation");
                    }
                    return script_value(t, eng);
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
                return script_value(std::monostate{}, eng);
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
                return script_value(arr.to_vector(), eng);
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
                return [func = func_, eng = engine_](const std::vector<script_value>& args) -> checked_result<script_value> {
                    using traits = detail::function_traits<Func>;
                    using return_type = typename traits::return_type;
                    using args_tuple = typename traits::argument_types;

                    // Check argument count
                    if (args.size() != traits::arity) {
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::argument_count_mismatch),
                            "Function expects {0} arguments, got {1}",
                            static_cast<uint64_t>(traits::arity), static_cast<uint64_t>(args.size())
                        );
                    }

                    // Call with reference support, catching any C++ exceptions
                    try {
                        return FunctionBinder::call_with_reference_support<return_type, args_tuple>(func, args, std::make_index_sequence<traits::arity>{}, eng);
                    } catch (const std::exception& e) {
                        uint64_t msg_id = symbolize_text(eng, e.what());
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::cpp_exception),
                            "{0}",
                            msg_id
                        );
                    }
                };
            } else {
                // Use standard calling for functions without reference parameters
                return [func = func_, eng = engine_](const std::vector<script_value>& args) -> checked_result<script_value> {
                    using traits = detail::function_traits<Func>;
                    using return_type = typename traits::return_type;
                    using args_tuple = typename traits::argument_types;

                    // Check argument count
                    if (args.size() != traits::arity) {
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::argument_count_mismatch),
                            "Function expects {0} arguments, got {1}",
                            static_cast<uint64_t>(traits::arity), static_cast<uint64_t>(args.size())
                        );
                    }

                    // Call the function with unpacked arguments, catching any C++ exceptions
                    try {
                        return FunctionBinder::call_impl_static<return_type, args_tuple>(func, args, std::make_index_sequence<traits::arity>{}, eng);
                    } catch (const std::exception& e) {
                        uint64_t msg_id = symbolize_text(eng, e.what());
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::cpp_exception),
                            "{0}",
                            msg_id
                        );
                    }
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
                return script_value(std::monostate{}, eng); // Return null for void
            } else {
                // Non-void function - use decltype(auto) to preserve reference types
                decltype(auto) result = call_non_void_impl_static<R, ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
                return detail::value_converter<R>::to(result, eng);
            }
        }

        template<typename ArgsTuple, typename F, size_t... Is>
        static void call_void_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            // Create parameter storage on stack
            detail::parameter_storage storage;
            detail::parameter_storage::scope_guard guard(eng, &storage);

            // Call function with conversions using the storage
            func(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
        }
        
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static R call_non_void_impl_static(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            // Create parameter storage on stack
            detail::parameter_storage storage;
            detail::parameter_storage::scope_guard guard(eng, &storage);

            // Call function with conversions using the storage
            return func(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
        }
        
        // Reference-aware calling methods
        template<typename R, typename ArgsTuple, typename F, size_t... Is>
        static script_value call_with_reference_support(F&& func, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
            if constexpr (std::is_void_v<R>) {
                // Void function with reference support
                call_with_reference_support_void<ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
                return script_value(std::monostate{}, eng); // Return null for void
            } else {
                // Non-void function with reference support - use decltype(auto) to preserve reference types
                decltype(auto) result = call_with_reference_support_non_void<R, ArgsTuple>(std::forward<F>(func), args, std::index_sequence<Is...>{}, eng);
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
        
        // Non-const T& argument carrier: either a live lvalue (cpp-bound write-through
        // target) or a private copy for plain args - converts to T& at the apply site.
        template<typename T>
        struct ref_or_copy_argument {
            T* live = nullptr;
            T owned{};
            operator T&() { return live ? *live : owned; }
        };

        // Create individual arguments, handling bound_array and bound_map types specially
        template<typename T>
        static auto create_argument(const script_value& arg, engine* eng) {
            if constexpr (is_bound_array_ref<T>::value) {
                using V = typename is_bound_array_ref<T>::value_type;
                
                if (!arg.is_array()) {
                    throw runtime_error("Cannot convert non-array to bound_array<T>&");
                }
                // Create bound_array object on stack - zero-copy reference to script_array
                return bound_array<V>(const_cast<script_value&>(arg).as_array(), eng);
            } else if constexpr (is_bound_map_ref<T>::value) {
                using K = typename is_bound_map_ref<T>::key_type;
                using V = typename is_bound_map_ref<T>::value_type;
                
                if (!arg.is_map()) {
                    throw runtime_error("Cannot convert non-map to bound_map<K,V>&");
                }
                // Create bound_map object on stack - zero-copy reference to script_map
                return bound_map<K, V>(const_cast<script_value&>(arg).as_map(), eng);
            } else if constexpr (std::is_reference_v<T>) {
                // For reference parameters, work with the dereferenced value
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;
                
                // Get the actual value (deref handles references automatically)
                const script_value& actual_arg = arg.deref();
                
                // For custom classes, get shared_ptr and return reference to the object
                // Exclude string, script_value, bound_array, bound_map, and built-in container types which have special handling
                if constexpr (std::is_class_v<base_type> &&
                            !std::is_same_v<base_type, std::string> &&
                            !std::is_same_v<base_type, script_value> &&
                            !is_bound_array_type<base_type>::value &&
                            !is_bound_map_type<base_type>::value &&
                            !is_specialization_v<base_type, std::vector> &&
                            !is_specialization_v<base_type, std::map>) {
                    auto ptr = actual_arg.as<std::shared_ptr<base_type>>();
                    return std::ref(*ptr);
                } else if constexpr (std::is_same_v<base_type, script_value>) {
                    // For script_value references, return directly - no conversion needed
                    return actual_arg;
                } else if constexpr (!std::is_const_v<std::remove_reference_t<T>> && std::is_lvalue_reference_v<T>) {
                    // Survives make_tuple's decay so the callee sees a real lvalue. Bound targets
                    // alias the LIVE C++ variable (S9/12.4 write-through); plain values get a
                    // private copy - the 12.4 aliasing delta stays confined to cpp-bound args.
                    ref_or_copy_argument<base_type> holder;
                    script_value& target = const_cast<script_value&>(actual_arg);
                    if (target.is_cpp_bound()) {
                        holder.live = &target.as<T>();
                    } else {
                        holder.owned = target.as<T>();
                    }
                    return holder;
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