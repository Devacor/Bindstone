#pragma once

#ifndef __JAISCRIPT_CORE_dynamic_binder_HPP__
#define __JAISCRIPT_CORE_dynamic_binder_HPP__
#define JAISCRIPT_dynamic_binder_HPP_INCLUDED

#include "engine.hpp"
#include "value.hpp"
#include "types.hpp"
#include "function_binder.hpp"
#include "parameter_storage.hpp"
#include "conversion_registry.hpp"
#include "bound_array.hpp"
#include "bound_map.hpp"
#include "bound_cpp_vector.hpp"
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/properties/property_schema.hpp>
#include <jaiscript/properties/observable_property.hpp>
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/core/property_type_converter.hpp>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <unordered_set>
#include <typeindex>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <type_traits>
#include <iostream>
#include <typeindex>

// class_instance, class_definition, access_level, delegation_type
// are defined in class_definition.hpp (extracted for faster compilation)
#include "class_definition.hpp"

// Forward declarations for script class support
namespace jai {
    class function_decl;
    class ConstructorDecl;
    class MethodDecl;
    class FieldDecl;
    class block_stmt;
    class expression;
    struct parameter;  // Defined in ast.hpp
    using expression_ptr = std::shared_ptr<expression>;

    struct script_defined_function;  // Defined in interpreter.hpp
}

namespace jai {

// Forward declarations
template<typename T> class signal_emitter;
template<typename T> class signal;
template<typename T> class observable_property;

// ============================================================================
// observable_property_ref<T> - Script-side reference to an observable property
// ============================================================================
//
// This wrapper class is used to expose observable_property<T> to scripts.
// It provides:
//   - get() - returns the current value
//   - on_change(callback) - connects a callback for value changes
//   - Transparent wrapper behavior - forwards operations to the underlying value
//
// Usage in scripts:
//   player.score           // Returns observable_property_ref<int>
//   player.score + 5       // Transparent wrapper forwards + to int (returns 5)
//   player.score.on_change([&](auto old, auto new) { ... })  // Connects callback
//
template<typename T>
class observable_property_ref {
public:
    observable_property_ref(observable_property<T>* prop, property_manager* mgr)
        : prop_(prop), mgr_(mgr) {}

    // Get the current value
    T get() const { return prop_ ? prop_->get() : T{}; }

    // Get pointer to the observable_property (for on_change registration)
    observable_property<T>* property() const { return prop_; }

    // Get the property_manager (for receiver tracking)
    property_manager* manager() const { return mgr_; }

private:
    observable_property<T>* prop_;
    property_manager* mgr_;
};
// class_instance and class_definition are defined in class_definition.hpp (included above)

// Helper function to extract base template name from full type name
// Examples: "Point<int>" -> "Point", "MyMap<std::string, int>" -> "MyMap", "Button" -> "Button"
inline std::string extract_base_template_name(const std::string& fullTypeName) {
    size_t anglePos = fullTypeName.find('<');
    if (anglePos != std::string::npos) {
        return fullTypeName.substr(0, anglePos);
    }
    return fullTypeName; // No template, return as-is
}

// Type trait to detect if a type has a constructor that takes engine*
template<typename T>
concept has_engine_constructor = requires(engine* eng) {
    T(eng);
};

// Detail namespace for factory registration helpers
namespace dynamic_binder_detail {
    using namespace serialization;

    // Helper to create wrapped script_value from C++ object
    template<typename T>
    script_value wrap_cpp_object(std::shared_ptr<T> cpp_obj, const std::string& class_name, engine* engine_ptr) {
        // Get engine for wrapping
        auto eng = engine_ptr;
        if (!eng) {
            throw serialization_error("Engine expired during deserialization");
        }

        // Get class definition
        auto class_def = eng->get_class_definition(class_name);
        if (!class_def) {
            throw serialization_error("Class definition not found: " + class_name);
        }

        // Create a class_instance to hold it
        auto instance = class_def->create_instance();

        // Store the C++ object in the class_instance
        uint64_t cpp_object_field_id = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
        instance->set_field(cpp_object_field_id,
            script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, eng));

        // Return wrapped object
        return script_value::make_object(class_name, instance, eng);
    }

    // Archive-only factory registration (no context needed)
    // Uses any_archive_reader for type-erased callback storage
    template<typename T, typename FactoryFunc>
    std::function<script_value(serialization::any_archive_reader&, uint32_t)>
    make_archive_only_factory(FactoryFunc&& factory, std::string class_name, engine* engine_ptr) {
        return [factory = std::forward<FactoryFunc>(factory), class_name = std::move(class_name), engine_ptr]
               (serialization::any_archive_reader& archive, uint32_t version) -> script_value {
            auto cpp_obj = factory(archive);
            return wrap_cpp_object<T>(cpp_obj, class_name, engine_ptr);
        };
    }

    // Forward declarations for context-based deserialization factories
    // These functions require archive_reader to be fully defined before instantiation.
    // Implementations are provided in dynamic_binder_serialization.hpp which should be
    // included explicitly by code that uses these context-based factories.
    template<typename T, typename ContextType, typename FactoryFunc>
    std::function<script_value(serialization::any_archive_reader&, uint32_t)>
    make_context_only_factory(FactoryFunc&& factory, std::string class_name, engine* engine_ptr);

    template<typename T, typename ContextType, typename FactoryFunc>
    std::function<script_value(serialization::any_archive_reader&, uint32_t)>
    make_context_archive_factory(FactoryFunc&& factory, std::string class_name, engine* engine_ptr);
}

// Tag for opting out of registration-time type dependency validation
// Use this with .property() to acknowledge circular dependencies
// Example: .property("child", &Parent::child, skip_type_check)
struct skip_type_check_t {};
inline constexpr skip_type_check_t skip_type_check{};

namespace dynamic_binder_validation {
    // Helper to detect if a type is a standard container
    template<typename T>
    struct is_std_container : std::false_type {};

    template<typename... Args>
    struct is_std_container<std::vector<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::map<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::unordered_map<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::set<Args...>> : std::true_type {};

    template<typename... Args>
    struct is_std_container<std::unordered_set<Args...>> : std::true_type {};

    // Helper to detect if a type is a signal type (jai::signal_emitter, jai::signal)
    // Signal types are internal callback mechanisms and don't need registration validation
    template<typename T>
    struct is_signal_type : std::false_type {};

    template<typename Sig>
    struct is_signal_type<signal_emitter<Sig>> : std::true_type {};

    template<typename Sig>
    struct is_signal_type<signal<Sig>> : std::true_type {};

    // Helper to unwrap smart pointers to get the inner type
    // For std::shared_ptr<T>, std::weak_ptr<T>, std::unique_ptr<T> -> extracts T
    // For other types -> returns the type as-is
    template<typename T>
    struct unwrap_smart_pointer {
        using type = T;
    };

    template<typename U>
    struct unwrap_smart_pointer<std::shared_ptr<U>> {
        using type = U;
    };

    template<typename U>
    struct unwrap_smart_pointer<std::weak_ptr<U>> {
        using type = U;
    };

    template<typename U>
    struct unwrap_smart_pointer<std::unique_ptr<U>> {
        using type = U;
    };

    template<typename T>
    using unwrap_smart_pointer_t = typename unwrap_smart_pointer<T>::type;

    // Helper to get the validation type (unwraps smart pointers and removes cv/ref)
    // For std::shared_ptr<CustomType> -> extracts CustomType
    // For int& -> extracts int
    template<typename T>
    struct get_validation_type {
        using bare_type = std::remove_cv_t<std::remove_reference_t<T>>;
        using type = unwrap_smart_pointer_t<bare_type>;
    };

    template<typename T>
    using get_validation_type_t = typename get_validation_type<T>::type;

    // Helper to determine if a type needs registration validation
    // Returns true for types that should be registered with dynamic_binder
    // Returns false for primitives, std::string, STL containers, signal types, and script_value
    // IMPORTANT: Smart pointers are unwrapped - we validate the inner type
    template<typename T>
    constexpr bool needs_registration_check() {
        using inner_type = get_validation_type_t<T>;  // Unwrap smart pointers

        return !std::is_fundamental_v<inner_type> &&
               !std::is_same_v<inner_type, std::string> &&
               !is_std_container<inner_type>::value &&
               !is_signal_type<inner_type>::value &&
               !std::is_same_v<inner_type, script_value> &&
               std::is_class_v<inner_type>;
    }

}

// ============================================================================
// detail namespace - Implementation helpers (already inside namespace jai)
// ============================================================================

namespace detail {
    // ============================================================================
    // extract_cpp_object_ptr<T> - Extract C++ object from class_instance or cpp_bound
    // ============================================================================
    //
    // This helper extracts a raw T* pointer from a script_value that represents
    // a C++ object. Handles two cases:
    //   1. class_instance wrapper: extracts via get_field(cpp_object_field_id)
    //   2. cpp_bound value: extracts directly from cpp_bound_ptr_
    //
    // Used by dynamic_binder method handlers to support method chaining where
    // methods return T& (creating cpp_bound values) that are then used as 'this'
    // for subsequent method calls.
    //
    template<typename T>
    T* extract_cpp_object_ptr(const script_value& val) {
        // Case 1: Non-owning cpp_bound reference (from T& return)
        if (val.is_cpp_bound()) {
            T* ptr = val.get_cpp_bound_as<T>();
            if (ptr) return ptr;
            // If cpp_bound but wrong type, fall through to try class_instance path
        }

        // Case 2: class_instance wrapper (normal constructed object)
        auto instance = val.as<std::shared_ptr<class_instance>>();
        auto cpp_obj_value = instance->get_field(instance->get_cpp_object_field_id());

        // The cpp_object field could also be cpp_bound or a shared_ptr
        if (cpp_obj_value.is_cpp_bound()) {
            return cpp_obj_value.get_cpp_bound_as<T>();
        }

        auto cpp_obj = cpp_obj_value.as<std::shared_ptr<T>>();
        return cpp_obj.get();
    }
} // namespace detail

// ============================================================================
// Bind Mode for auto_build()
// ============================================================================

enum class bind_mode {
    all,        // Base classes + properties + auto-detected methods/constructors
    properties, // Base classes + properties only (no auto methods/constructors)
    hierarchy   // Base classes only (no properties or auto methods)
};

// ============================================================================
// Concepts for auto-detection (used by auto_build)
// ============================================================================

namespace auto_bind_concepts {

// Detect if T has to_string() method
template<typename T>
concept has_to_string = requires(const T& t) {
    { t.to_string() } -> std::convertible_to<std::string>;
};

// Detect if T has size() method
template<typename T>
concept has_size = requires(const T& t) {
    { t.size() } -> std::convertible_to<size_t>;
};

// Detect if T has empty() method
template<typename T>
concept has_empty = requires(const T& t) {
    { t.empty() } -> std::convertible_to<bool>;
};

// Detect if T has operator==
template<typename T>
concept has_equality = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
};

// Detect if T uses property_owner CRTP (has _jai_owner_type)
template<typename T>
concept is_property_owner = requires {
    typename T::_jai_owner_type;
};

// Detect if T has base types defined
template<typename T>
concept has_base_types = requires {
    typename T::_jai_base_types;
};

// Forward declaration for jai_auto_bind detection
template<typename U> class dynamic_binder;

// Detect if T has static jai_auto_bind(dynamic_binder<T>&) method
// This allows classes to register their own private members during auto_bind()
template<typename T>
concept has_jai_auto_bind = requires(dynamic_binder<T>& builder) {
    { T::jai_auto_bind(builder) } -> std::same_as<void>;
};

} // namespace auto_bind_concepts

// Builder pattern for registering C++ classes to JaiScript
template<typename T>
class dynamic_binder {
public:
    dynamic_binder(engine& engine, const std::string& class_name)
        : engine_(engine), class_name_(class_name) {
        // Extract base template name if this is a templated type
        std::string baseTemplateName = extract_base_template_name(class_name);

        // Register the base template name if it contains template syntax
        if (baseTemplateName != class_name) {
            engine.register_template_type(baseTemplateName);
        }

        // Intern the class name for fast type comparisons
        uint64_t type_id = engine.get_symbolizer()->intern(class_name);

        class_def_ = std::make_shared<class_definition>(class_name, type_id, &engine_);
        
        // Initialize serialization metadata
        serialization_metadata_.class_name = class_name;
        serialization_metadata_.current_version = 1;
    }
    
    // Constructor that accepts a shared_ptr<engine>
    dynamic_binder(std::shared_ptr<engine>& engine_ptr, const std::string& class_name)
        : dynamic_binder(*engine_ptr, class_name) {
    }

    // Destructor - automatically call build() if not already called
    // This ensures the class is registered even if the user forgets to call build()
    ~dynamic_binder() {
        if (!built_) {
            try { build(); } catch (...) {}
        }
    }

    // Disable copy (would cause double-build issues)
    dynamic_binder(const dynamic_binder&) = delete;
    dynamic_binder& operator=(const dynamic_binder&) = delete;

    // Enable move
    dynamic_binder(dynamic_binder&& other) noexcept
        : engine_(other.engine_)
        , class_name_(std::move(other.class_name_))
        , class_def_(std::move(other.class_def_))
        , serialization_metadata_(std::move(other.serialization_metadata_))
        , has_base_class_(other.has_base_class_)
        , base_type_index_(other.base_type_index_)
        , has_explicit_constructor_(other.has_explicit_constructor_)
        , built_(other.built_)
    {
        // Mark the moved-from object as built to prevent double-registration
        other.built_ = true;
    }

    // Add constructor
    template<typename... Args>
    dynamic_binder& constructor() {
        has_explicit_constructor_ = true;  // Track that user explicitly registered a constructor

        // Register the constructor as an overloaded function
        if constexpr (sizeof...(Args) == 0) {
            // Zero-argument constructor
            engine* engine_ptr = &engine_;
            engine_.add_overloaded_function(class_name_, 0, [class_def = class_def_, class_name = class_name_, engine_ptr](const std::vector<script_value>& args) -> script_value {
                try {
                    // Create the C++ object
                    std::shared_ptr<T> cpp_obj;

                    // Check if T has an engine constructor and use it if available
                    if constexpr (has_engine_constructor<T>) {
                        // Use the engine-aware constructor
                        cpp_obj = std::make_shared<T>(engine_ptr);
                    } else {
                        // Use the default constructor
                        cpp_obj = std::make_shared<T>();
                    }

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, engine_ptr));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ptr);
                } catch (const std::exception& e) {
                    std::cerr << "Error in zero-arg constructor: " << e.what() << std::endl;
                    throw;
                }
            });
        } else {
            // Multi-argument constructor
            engine* engine_ptr = &engine_;

            // Helper to avoid template parameter pack in lambda (MSVC workaround)
            using constructor_helper = std::shared_ptr<T>(*)(const std::vector<script_value>&, engine*);
            constructor_helper helper = [](const std::vector<script_value>& args, engine* eng) -> std::shared_ptr<T> {
                return dynamic_binder<T>::template createObjectImpl<Args...>(args, std::index_sequence_for<Args...>{}, eng);
            };

            engine_.add_overloaded_function(class_name_, sizeof...(Args), [class_def = class_def_, class_name = class_name_, engine_ptr, helper](const std::vector<script_value>& args) -> script_value {
                try {
                    // Extract arguments using index-based unpacking via helper
                    auto cpp_obj = helper(args, engine_ptr);

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, engine_ptr));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ptr);
                } catch (const std::exception&) {
                    throw;
                }
            });
        }
        
        return *this;
    }
    
    // Add method binding - member function pointer version
    template<typename R, typename... Args>
    dynamic_binder& method(const std::string& name, R(T::*method)(Args...)) {
        auto method_func = [method, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }
            
            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw runtime_error("Method expects " + std::to_string(sizeof...(Args)) +
                                 " arguments, got " + std::to_string(args.size() - 1));
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            // Call the method with unpacked arguments
            if (auto eng = engine_ptr) {
                return dynamic_binder<T>::callMethodImpl(cpp_obj, method, args, std::index_sequence_for<Args...>{}, eng);
            }
            throw runtime_error("Engine no longer exists");
        };

        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func, sizeof...(Args));

        return *this;
    }

    // Add const method binding
    template<typename R, typename... Args>
    dynamic_binder& method(const std::string& name, R(T::*method)(Args...) const) {
        auto method_func = [method, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Method called without 'this' object");
            }

            // Validate argument count (first arg is 'this', so we need sizeof...(Args) + 1 total)
            if (args.size() != sizeof...(Args) + 1) {
                throw runtime_error("Method expects " + std::to_string(sizeof...(Args)) +
                                 " arguments, got " + std::to_string(args.size() - 1));
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            // Call the method with unpacked arguments
            if (engine_ptr) {
                return dynamic_binder<T>::callConstMethodImpl(cpp_obj, method, args, std::index_sequence_for<Args...>{}, engine_ptr);
            }
            throw runtime_error("Engine no longer exists");
        };

        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        class_def_->add_method(name, method_func, sizeof...(Args));

        return *this;
    }

    // Add lambda/callable method binding
    // Supports: .method("setText", [](Button& self, const std::string& text) { self.setText(text); })
    // Note: First parameter can be a reference to self for accessing the object
    template<typename Callable>
    dynamic_binder& method(const std::string& name, Callable&& callable) {
        // Detect accidental use of member data pointers - use property() instead
        static_assert(!std::is_member_object_pointer_v<std::decay_t<Callable>>,
            "Member data pointers cannot be passed to method(). Use property() instead.");

        // Use function_traits to determine the signature
        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;
        
        // Check if the first parameter is a reference to T (the self parameter)
        // Support both T& and const T& for lambda methods
        constexpr bool has_self_param = traits::arity > 0 &&
            (std::is_same_v<std::tuple_element_t<0, args_tuple>, T&> ||
             std::is_same_v<std::tuple_element_t<0, args_tuple>, const T&>);
        
        auto method_func = [callable = std::forward<Callable>(callable), has_self_param, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (has_self_param) {
                // Lambda expects T& as first parameter, we need to extract it from args[0]
                // args[0] is the class_instance, remaining args are the actual parameters
                if (args.empty()) {
                    throw runtime_error("Method called without 'this' object");
                }
                
                // Expected argument count is arity - 1 (excluding self) + 1 (for 'this')
                if (args.size() != traits::arity) {
                    throw runtime_error("Method expects " + std::to_string(traits::arity - 1) +
                                     " arguments, got " + std::to_string(args.size() - 1));
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                // Call the lambda with the C++ object as first argument and remaining args
                if (auto eng = engine_ptr) {
                    return callLambdaWithSelf<typename traits::return_type, args_tuple>(
                        callable, cpp_obj, args, std::make_index_sequence<traits::arity>{}, eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                // Regular lambda without self parameter
                if (args.size() != traits::arity) {
                    throw runtime_error("Method expects " + std::to_string(traits::arity) + 
                                     " arguments, got " + std::to_string(args.size()));
                }
                
                // Call the lambda with unpacked arguments
                if (auto eng = engine_ptr) {
                    return callCallableImpl<typename traits::return_type, args_tuple>(callable, args, std::make_index_sequence<traits::arity>{}, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };

        // Add method to the class definition (for object.method() calls)
        // Methods are stored per-class and accessed through the object instance
        // Arity from script perspective: if has_self_param, arity is traits::arity - 1, else traits::arity
        constexpr size_t script_arity = has_self_param ? traits::arity - 1 : traits::arity;
        class_def_->add_method(name, method_func, script_arity);

        return *this;
    }
    
    // Add static method binding - for regular function pointers
    template<typename R, typename... Args>
    dynamic_binder& static_method(const std::string& name, R(*func)(Args...)) {
        auto static_method_func = [func, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            // Validate argument count (no 'this' for static methods)
            if (args.size() != sizeof...(Args)) {
                throw runtime_error("Static method expects " + std::to_string(sizeof...(Args)) + 
                                 " arguments, got " + std::to_string(args.size()));
            }
            
            // Call the static function directly
            if constexpr (std::is_void_v<R>) {
                callStaticFunctionImpl(func, args, std::make_index_sequence<sizeof...(Args)>{});
                
                if (auto eng = engine_ptr) {
                    return script_value::make_null(eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                auto result = callStaticFunctionImpl(func, args, std::make_index_sequence<sizeof...(Args)>{});
                
                if (auto eng = engine_ptr) {
                    return script_value(result, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };
        
        // Add static method to the class definition with arity
        class_def_->add_static_method(name, static_method_func, sizeof...(Args));

        return *this;
    }
    
    // Add static method binding - for lambdas and callables
    template<typename Callable>
    dynamic_binder& static_method(const std::string& name, Callable&& callable) {
        // Detect accidental use of member pointers
        static_assert(!std::is_member_pointer_v<std::decay_t<Callable>>,
            "Member pointers cannot be passed to static_method(). Use method() or property() instead.");

        using traits = detail::function_traits<std::decay_t<Callable>>;
        
        auto static_method_func = [callable = std::forward<Callable>(callable), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            // Validate argument count
            if (args.size() != traits::arity) {
                throw runtime_error("Static method expects " + std::to_string(traits::arity) + 
                                 " arguments, got " + std::to_string(args.size()));
            }
            
            // Call the callable directly (no 'this' parameter)
            using return_type = typename traits::return_type;
            if constexpr (std::is_void_v<return_type>) {
                callStaticCallableImpl(callable, args, std::make_index_sequence<traits::arity>{});
                
                if (auto eng = engine_ptr) {
                    return script_value::make_null(eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                auto result = callStaticCallableImpl(callable, args, std::make_index_sequence<traits::arity>{});
                
                if (auto eng = engine_ptr) {
                    return script_value(result, eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        };

        // Add static method to the class definition with arity
        class_def_->add_static_method(name, static_method_func, traits::arity);

        return *this;
    }

    // Add custom serialization constructor for non-default constructible types
    template<typename constructor_func>
    dynamic_binder& serialize_construct(constructor_func&& constructor) {
        // Store the custom constructor in the class definition
        class_def_->add_method("_serialize_construct", [constructor = std::forward<constructor_func>(constructor), class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.size() != 1) {
                throw runtime_error("Serialization constructor expects exactly one argument (the serialized data)");
            }
            
            // Call the custom constructor with the serialized data
            T instance = constructor(args[0]);
            
            // Create a class_instance to hold it
            auto class_instance = class_def->create_instance();
            class_instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name,
                class_def->get_type_id(), std::make_shared<T>(std::move(instance)), engine_ptr));

            if (auto eng = engine_ptr) {
                return script_value::make_object(class_name, class_instance, eng);
            }
            throw runtime_error("Engine no longer exists");
        });
        
        // Also register serialization metadata
        auto& metadata = serialization_metadata_;
        metadata.custom_construct = [constructor = std::forward<constructor_func>(constructor)](serialization::any_archive_reader& ar, uint32_t version) -> script_value {
            // Convert archive data to script_value for the constructor
            // This is a simplified implementation - real version would need proper conversion
            script_value data = script_value(); // TODO: Convert archive to script_value
            T instance = constructor(data, version);

            // TODO: Wrap in class_instance and return as script_value
            return script_value();
        };
        
        return *this;
    }
    
    // Set class version
    dynamic_binder& version(uint32_t v) {
        serialization_metadata_.current_version = v;
        return *this;
    }

    // Add property/field binding (with automatic registration validation)
    template<typename P>
    dynamic_binder& property(const std::string& name, P T::*member) {
        return property_impl<P>(name, member, false);
    }

    // Add property/field binding (opt-out of registration validation)
    // Use this to explicitly acknowledge circular dependencies
    template<typename P>
    dynamic_binder& property(const std::string& name, P T::*member, skip_type_check_t) {
        return property_impl<P>(name, member, true);
    }

private:
    // Internal implementation for property registration with validation
    template<typename P>
    dynamic_binder& property_impl(const std::string& name, P T::*member, bool skip_validation) {
        // Validate that property type is registered (unless explicitly skipped)
        if constexpr (dynamic_binder_validation::needs_registration_check<P>()) {
            if (!skip_validation) {
                // Extract the validation type (unwraps smart pointers)
                using validation_type_t = typename dynamic_binder_validation::get_validation_type<P>::type;

                // Check if this type has been registered with dynamic_binder
                // We use std::type_index to identify types, just like the rest of the codebase
                auto prop_type_index = std::type_index(typeid(validation_type_t));
                auto registered_class = engine_.get_class_definition_by_type(prop_type_index);

                if (!registered_class) {
                    throw std::runtime_error(
                        "Property '" + name + "' of class '" + class_name_ + "' uses unregistered type '" +
                        typeid(P).name() + "'. " +
                        "You must register this type with dynamic_binder before registering '" + class_name_ + "', " +
                        "or use skip_type_check to explicitly acknowledge circular dependencies:\n" +
                        "  .property(\"" + name + "\", &" + class_name_ + "::" + name + ", jai::skip_type_check)"
                    );
                }
            }
        }

        // Register the property as a special field that knows how to access the C++ member
        // We'll store a lambda that can get/set the value
        class_def_->add_field(name, script_value(std::monostate{}, &engine_)); // Register field name

        // Register serialization metadata
        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.type = engine_.get_type_info_for_cpp_type<P>();
        serialization_metadata_.properties.push_back(prop_meta);
        
        // Add a special method that handles property access
        // The interpreter's visitMemberExpr will need to check for these
        class_def_->add_method("_get_" + name, [member, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            if (auto eng = engine_ptr) {
                // Special handling for std::vector<T> - wrap in bound_cpp_vector for zero-copy access
                if constexpr (is_specialization_v<P, std::vector>) {
                    using element_type = typename P::value_type;
                    // Create bound_cpp_vector wrapper that references the C++ vector directly
                    auto wrapper = std::make_shared<bound_cpp_vector<element_type>>(
                        cpp_obj->*member, eng);
                    return eng->make_object(wrapper);
                }
                else {
                    return detail::value_converter<P>::to(cpp_obj->*member, eng);
                }
            }
            throw runtime_error("Engine no longer exists");
        });

        // Only create setter for copy-assignable types
        if constexpr (std::is_copy_assignable_v<P>) {
            class_def_->add_method("_set_" + name, [member, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                if (args.size() < 2) {
                    throw runtime_error("Property setter requires 'this' and value");
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                // Special case: if P is script_value, don't convert
                if constexpr (std::is_same_v<P, script_value>) {
                    // deref() returns *this if not a reference, so this handles both cases
                    (cpp_obj->*member).deref() = args[1].clone();
                } else {
                    cpp_obj->*member = args[1].as<P>();
                }
                return script_value(std::monostate{}, engine_ptr); // null
            });

            // Register field_id -> setter_id mapping for fast runtime lookup
            uint64_t field_id = engine_.symbolize(name);
            uint64_t setter_id = engine_.symbolize("_set_" + name);
            class_def_->register_property_setter(field_id, setter_id);
        }

        // Register bound_cpp_vector<T> if this property is a std::vector
        if constexpr (is_specialization_v<P, std::vector>) {
            using element_type = typename P::value_type;
            std::string wrapper_type_name = std::string("bound_cpp_vector<") + typeid(element_type).name() + ">";

            // Check if already registered
            auto existing = engine_.get_class_definition_by_type(std::type_index(typeid(bound_cpp_vector<element_type>)));
            if (!existing) {
                // Register bound_cpp_vector<element_type> with array-like methods
                dynamic_binder<bound_cpp_vector<element_type>>(engine_, wrapper_type_name)
                    .method("size", &bound_cpp_vector<element_type>::size)
                    .method("empty", &bound_cpp_vector<element_type>::empty)
                    .method("clear", &bound_cpp_vector<element_type>::clear)
                    .method("push_back", static_cast<void(bound_cpp_vector<element_type>::*)(const element_type&)>(&bound_cpp_vector<element_type>::push_back))
                    .method("push", static_cast<void(bound_cpp_vector<element_type>::*)(const element_type&)>(&bound_cpp_vector<element_type>::push_back)) // Alias
                    .method("pop_back", &bound_cpp_vector<element_type>::pop_back)
                    .method("pop", &bound_cpp_vector<element_type>::pop_back) // Alias
                    .method("front", static_cast<element_type&(bound_cpp_vector<element_type>::*)()>(&bound_cpp_vector<element_type>::front))
                    .method("back", static_cast<element_type&(bound_cpp_vector<element_type>::*)()>(&bound_cpp_vector<element_type>::back))
                    .method("at", static_cast<element_type&(bound_cpp_vector<element_type>::*)(size_t)>(&bound_cpp_vector<element_type>::at))
                    .method("[]", static_cast<element_type&(bound_cpp_vector<element_type>::*)(size_t)>(&bound_cpp_vector<element_type>::operator[]))
                    .build();
            }
        }

        return *this;
    }

public:
    // Add property with getter/setter (supports both lambdas and member function pointers)
    template<typename Getter, typename Setter>
    dynamic_binder& property(const std::string& name, Getter&& getter, Setter&& setter) {
        // Register the property as a field
        class_def_->add_field(name, script_value(std::monostate{}, &engine_)); // Register field name

        // Register serialization metadata
        constexpr bool is_read_only = std::is_null_pointer_v<std::decay_t<Setter>> || std::is_same_v<std::decay_t<Setter>, std::nullptr_t>;

        serialization::property_metadata prop_meta;
        prop_meta.name = name;
        prop_meta.read_only = is_read_only;

        // Try to deduce type from getter return type
        if constexpr (std::is_member_function_pointer_v<std::decay_t<Getter>>) {
            using getter_traits = detail::function_traits<std::decay_t<Getter>>;
            using return_type = typename getter_traits::return_type;
            prop_meta.type = engine_.get_type_info_for_cpp_type<std::decay_t<return_type>>();
        } else {
            // For lambdas, we can't easily deduce the type at compile time
            // Leave type as nullptr - it will be determined at runtime if needed
            prop_meta.type = nullptr;
        }

        serialization_metadata_.properties.push_back(prop_meta);

        // Add getter method
        class_def_->add_method("_get_" + name, [getter = std::forward<Getter>(getter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (args.empty()) {
                throw runtime_error("Property getter called without 'this' object");
            }

            // Extract the C++ object from the first argument (this)
            // Handles both class_instance wrappers and cpp_bound values (for method chaining)
            T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

            // Check if getter is a member function pointer
            if constexpr (std::is_member_function_pointer_v<std::decay_t<Getter>>) {
                // Call member function pointer: (obj->*getter)()
                if (auto eng = engine_ptr) {
                    return detail::value_converter<decltype((cpp_obj->*getter)())>::to((cpp_obj->*getter)(), eng);
                }
                throw runtime_error("Engine no longer exists");
            } else {
                // Call lambda: getter(*cpp_obj)
                if (auto eng = engine_ptr) {
                    return detail::value_converter<decltype(getter(*cpp_obj))>::to(getter(*cpp_obj), eng);
                }
                throw runtime_error("Engine no longer exists");
            }
        });

        // Add setter method - use different implementations based on setter type
        if constexpr (std::is_null_pointer_v<std::decay_t<Setter>> || std::is_same_v<std::decay_t<Setter>, std::nullptr_t>) {
            // Readonly property - add a no-op setter
            class_def_->add_method("_set_" + name, [engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                // Read-only property, do nothing
                return script_value(std::monostate{}, engine_ptr);
            });
        } else if constexpr (std::is_member_function_pointer_v<std::decay_t<Setter>>) {
            // Member function pointer setter
            class_def_->add_method("_set_" + name, [setter = std::forward<Setter>(setter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                if (args.size() < 2) {
                    throw runtime_error("Property setter requires 'this' and value");
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                using setter_traits = detail::function_traits<std::decay_t<Setter>>;
                using value_type = std::tuple_element_t<0, typename setter_traits::argument_types>;
                auto value = args[1].as<value_type>();
                (cpp_obj->*setter)(value);
                return script_value(std::monostate{}, engine_ptr);
            });
        } else {
            // Lambda setter
            class_def_->add_method("_set_" + name, [setter = std::forward<Setter>(setter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
                if (args.size() < 2) {
                    throw runtime_error("Property setter requires 'this' and value");
                }

                // Extract the C++ object from the first argument (this)
                // Handles both class_instance wrappers and cpp_bound values (for method chaining)
                T* cpp_obj = detail::extract_cpp_object_ptr<T>(args[0]);

                using setter_traits = detail::function_traits<std::decay_t<Setter>>;
                using value_type = std::tuple_element_t<1, typename setter_traits::argument_types>;
                auto value = args[1].as<value_type>();
                setter(*cpp_obj, value);
                return script_value(std::monostate{}, engine_ptr);
            });
        }

        // Register field_id -> setter_id mapping for fast runtime lookup
        uint64_t field_id = engine_.symbolize(name);
        uint64_t setter_id = engine_.symbolize("_set_" + name);
        class_def_->register_property_setter(field_id, setter_id);

        return *this;
    }
    
    // Add static property binding - for simple variable access
    template<typename P>
    dynamic_binder& static_property(const std::string& name, P* static_var) {
        // Convert name to ID
        uint64_t name_id = engine_.symbolize(name);

        // Add static field with getter for the variable
        class_def_->add_static_field(name_id, script_value(*static_var, &engine_));

        // Add getter method for read access
        std::string getter_name = "_get_" + name;
        uint64_t getter_id = engine_.symbolize(getter_name);
        class_def_->add_static_method(getter_id, [static_var, engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (auto eng = engine_ptr) {
                return script_value(*static_var, eng);
            }
            throw runtime_error("Engine no longer exists");
        });

        return *this;
    }
    
    // Add static property with lambda getter/setter
    template<typename Getter, typename Setter>
    dynamic_binder& static_property(const std::string& name, Getter&& getter, Setter&& setter) {
        // Add getter method
        std::string getter_name = "_get_" + name;
        uint64_t getter_id = engine_.symbolize(getter_name);
        class_def_->add_static_method(getter_id, [getter = std::forward<Getter>(getter), engine_ptr = &engine_](const std::vector<script_value>& args) -> script_value {
            if (auto eng = engine_ptr) {
                return detail::value_converter<decltype(getter())>::to(getter(), eng);
            }
            throw runtime_error("Engine no longer exists");
        });

        // Note: setter parameter kept for API compatibility but not registered as setX()
        // Static property assignment uses set_static_field() directly in the interpreter
        (void)setter;  // Suppress unused warning

        return *this;
    }
    
    // Add base class - establishes inheritance relationship (appends, doesn't clear)
    // This enables:
    // 1. Method inheritance (derived can call base methods)
    // 2. Polymorphic copy support
    // Can be chained: .base_class<A>().base_class<B>() for multiple inheritance
    template<typename Base>
    dynamic_binder& base_class() {
        static_assert(std::is_base_of_v<Base, T>,
                      "Specified type is not a base class of this class");

        // Set up inheritance relationship - use type_index lookup instead of typeid name
        auto base_def = engine_.get_class_definition_by_type(std::type_index(typeid(Base)));
        if (base_def) {
            // Use add_parent to append (validates for diamond inheritance)
            // add_parent is idempotent (returns true if already registered)
            // but returns false for true diamond inheritance
            if (!class_def_->add_parent(base_def)) {
                throw std::runtime_error("Diamond inheritance detected: class '" + class_name_ +
                    "' would have multiple paths to the same base class");
            }
        }

        // Store base type info for polymorphic copy registration
        // For multiple inheritance, this stores the last base (first base is used for primary polymorphism)
        if (!has_base_class_) {
            base_type_index_ = std::type_index(typeid(Base));
        }
        has_base_class_ = true;

        return *this;
    }
    
    // Add explicit type conversion support - general purpose
    template<typename From, typename To>
    dynamic_binder& add_type_conversion(std::function<To(const From&)> converter) {
        // Register the conversion with the engine
        // This would need to be implemented in the engine's type system
        // Usage: .add_type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>([](const auto& item) { return item.self(); })
        return *this;
    }

private:

public:
    // Register a deserialization factory for non-default constructors
    // Supports three signatures automatically detected via template metaprogramming:
    // 1. Archive-only: [](serialization::archive_reader& archive) -> std::shared_ptr<T>
    // 2. Context-only: [](ContextType* ctx) -> std::shared_ptr<T>
    // 3. Context + archive: [](ContextType* ctx, serialization::archive_reader& archive) -> std::shared_ptr<T>
    template<typename ContextType = void, typename FactoryFunc>
    dynamic_binder& deserialization_factory(FactoryFunc&& factory) {
        using namespace serialization;

        // Detect factory signature using function traits
        using factory_traits = detail::function_traits<std::decay_t<FactoryFunc>>;
        constexpr size_t arg_count = factory_traits::arity;

        // Dispatch to appropriate helper based on signature
        if constexpr (arg_count == 1) {
            // Check if single arg is serialization::any_archive_reader& or context pointer
            using arg0_type = std::tuple_element_t<0, typename factory_traits::argument_types>;

            // Check if the decayed type (without reference/pointer) is any_archive_reader
            if constexpr (std::is_same_v<std::decay_t<arg0_type>, serialization::any_archive_reader>) {
                // Archive-only factory: [](serialization::any_archive_reader& archive) -> std::shared_ptr<T>
                serialization_metadata_.custom_construct =
                    dynamic_binder_detail::make_archive_only_factory<T>(
                        std::forward<FactoryFunc>(factory), class_name_, &engine_);
            } else if constexpr (!std::is_void_v<ContextType>) {
                // Context-only factory: [](ContextType* ctx) -> std::shared_ptr<T>
                serialization_metadata_.custom_construct =
                    dynamic_binder_detail::make_context_only_factory<T, ContextType>(
                        std::forward<FactoryFunc>(factory), class_name_, &engine_);
            } else {
                // Context-only factory requires ContextType to be specified
                static_assert(!std::is_void_v<ContextType>,
                    "Context-only factory requires a non-void ContextType template parameter. "
                    "Use deserialization_factory<YourContextType>([](YourContextType* ctx) { ... })");
            }
        } else if constexpr (arg_count == 2) {
            if constexpr (!std::is_void_v<ContextType>) {
                // Context + archive factory: [](ContextType* ctx, serialization::any_archive_reader& archive) -> std::shared_ptr<T>
                serialization_metadata_.custom_construct =
                    dynamic_binder_detail::make_context_archive_factory<T, ContextType>(
                        std::forward<FactoryFunc>(factory), class_name_, &engine_);
            } else {
                static_assert(!std::is_void_v<ContextType>,
                    "Context+archive factory requires a non-void ContextType template parameter. "
                    "Use deserialization_factory<YourContextType>([](YourContextType* ctx, serialization::any_archive_reader& ar) { ... })");
            }
        } else {
            static_assert(arg_count <= 2, "Deserialization factory must take 1 or 2 arguments");
        }

        return *this;
    }

    // Register post-deserialization hook for migration and data transformation
    // This is a convenience method that registers a "post_deserialize" method
    // that will be automatically called after properties are loaded from archives
    //
    // The hook receives the version number that was serialized, which can be used
    // for migration logic. You can use either signature:
    //
    // Examples:
    //   // Without version parameter (simple computed values)
    //   .post_deserialize_hook([](MyClass& self) {
    //       self.computed_field = self.width * self.height;
    //   })
    //
    //   // With version parameter (for migration)
    //   .post_deserialize_hook([](MyClass& self, int version) {
    //       if (version < 2) {
    //           // Migrate from v1 to v2
    //           self.new_field = self.compute_from_old_fields();
    //       }
    //   })
    template<typename Callable>
    dynamic_binder& post_deserialize_hook(Callable&& callable) {
        // Register as a regular method named "post_deserialize"
        return method("post_deserialize", std::forward<Callable>(callable));
    }

    // ============================================================================
    // transparent_wrapper() - Mark this type as a transparent wrapper
    // ============================================================================
    //
    // When an operation (method call, operator, etc.) is not found on the wrapper,
    // the interpreter will call the unwrap function and retry the operation on
    // the underlying value.
    //
    // This enables patterns like:
    //   player.score + 5        // score is observable_property<int>, forwards + to int
    //   player.score.on_change  // on_change is on the wrapper itself
    //   player.cat.meow()       // cat is observable_property<Cat>, forwards meow() to Cat
    //
    // The unwrap function receives a reference to the wrapper object and returns
    // a script_value containing the underlying value.
    //
    // Usage:
    //   dynamic_binder<observable_property<int>>(eng, "observable_int")
    //       .transparent_wrapper([](observable_property<int>& self) { return self.get(); })
    //       .method("on_change", ...)
    //       .build();
    //
    template<typename UnwrapFn>
    dynamic_binder& transparent_wrapper(UnwrapFn&& unwrap_fn) {
        engine* eng = &engine_;

        // Create the unwrap function that works with script_value
        auto wrapped_fn = [unwrap_fn = std::forward<UnwrapFn>(unwrap_fn), eng](script_value& wrapper_sv, engine* e) -> script_value {
            // Extract the C++ object from the script_value
            if (!wrapper_sv.is_object()) {
                return script_value(std::monostate{}, e);
            }

            auto holder = wrapper_sv.get_object_holder();
            if (!holder) {
                return script_value(std::monostate{}, e);
            }

            // Get the typed pointer - check if it's a class_instance wrapper
            T* cpp_ptr = nullptr;
            if (holder->is_class_instance_wrapper) {
                auto instance = std::static_pointer_cast<class_instance>(holder->data);
                if (instance) {
                    cpp_ptr = instance->get_cpp_object_as<T>().get();
                }
            } else {
                auto typed_ptr = std::static_pointer_cast<T>(holder->data);
                cpp_ptr = typed_ptr.get();
            }

            if (!cpp_ptr) {
                return script_value(std::monostate{}, e);
            }

            // Call the unwrap function and convert result to script_value
            auto underlying_value = unwrap_fn(*cpp_ptr);
            return script_value_from_cpp(underlying_value, e);
        };

        class_def_->set_unwrap_function(std::move(wrapped_fn));
        return *this;
    }

    // ============================================================================
    // auto_bind() - Apply auto-detection of base classes and common methods
    // ============================================================================
    //
    // Usage:
    //   dynamic_binder<Player>(eng, "Player")
    //       .auto_bind()                       // Applies base classes + common methods
    //       .method("custom", &Player::custom); // Add more custom bindings
    //
    //   dynamic_binder<Player>(eng, "Player")
    //       .auto_bind(bind_mode::hierarchy);  // Just base classes, no auto methods
    //
    dynamic_binder& auto_bind(bind_mode mode = bind_mode::all) {
        // Apply base classes from _jai_base_types if present
        if constexpr (auto_bind_concepts::has_base_types<T>) {
            apply_base_classes_from_tuple(std::type_identity<typename T::_jai_base_types>{});
        }

        // Bind properties from type_registry if T is a property_owner
        if constexpr (auto_bind_concepts::is_property_owner<T>) {
            bind_properties_from_schema();
        }

        // Apply auto-detected methods if mode is 'all'
        if (mode == bind_mode::all) {
            if constexpr (auto_bind_concepts::has_to_string<T>) {
                method("to_string", &T::to_string);
            }
            if constexpr (auto_bind_concepts::has_size<T>) {
                method("size", &T::size);
            }
            if constexpr (auto_bind_concepts::has_empty<T>) {
                method("empty", &T::empty);
            }
            if constexpr (auto_bind_concepts::has_equality<T>) {
                method("==", [](const T& a, const T& b) -> bool { return a == b; });
                method("!=", [](const T& a, const T& b) -> bool { return !(a == b); });
            }
        }

        // Call T::jai_auto_bind(*this) if it exists - allows classes to register private members
        if constexpr (auto_bind_concepts::has_jai_auto_bind<T>) {
            T::jai_auto_bind(*this);
        }

        return *this;
    }

private:
    // Bind properties from type_registry schema for property_owner classes
    // This is called when T is complete, so we can directly access T's property_mgr
    void bind_properties_from_schema() {
        // Get the schema for this type (includes inherited properties)
        auto all_props = type_registry::instance().all_properties<T>();

        for (const auto* prop_meta : all_props) {
            if (!prop_meta) continue;

            const std::string prop_name = prop_meta->name;
            const std::type_index value_type = prop_meta->value_type_id;
            const bool is_observable = prop_meta->is_observable;

            // Dispatch to the appropriate template based on value type
            // We use a type-switch here because we have runtime type_index
            if (!try_bind_property_typed<int>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<float>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<double>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<bool>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<std::string>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int64_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint64_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int32_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint32_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int16_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint16_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<int8_t>(prop_name, value_type, is_observable) &&
                !try_bind_property_typed<uint8_t>(prop_name, value_type, is_observable)) {
                // Try the global type converter registry for custom types
                try_bind_property_from_registry(prop_name, value_type);
            }
        }
    }

    // Try to bind a property if the value type matches ValueT
    // Returns true if bound, false if type doesn't match
    // For observable properties, also binds an observation method
    template<typename ValueT>
    bool try_bind_property_typed(const std::string& prop_name, std::type_index value_type, bool is_observable) {
        if (value_type != std::type_index(typeid(ValueT))) {
            return false;
        }

        engine* eng = &engine_;

        // For observable properties, register the wrapper type and use a different getter
        if (is_observable) {
            // Ensure the observable_property_ref<ValueT> type is registered
            ensure_observable_ref_type_registered<ValueT>();

            // Create getter that returns the wrapper for observable properties
            // This enables player.score.on_change(callback) syntax
            auto getter = [prop_name, eng](T& self) -> script_value {
                auto* base_prop = self.property_mgr.get(prop_name);
                if (!base_prop) {
                    return script_value(std::monostate{}, eng);
                }
                auto* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
                if (!obs_prop) {
                    return script_value(std::monostate{}, eng);
                }
                // Create an observable_property_ref wrapper and return it as a script object
                auto ref = std::make_shared<observable_property_ref<ValueT>>(obs_prop, &self.property_mgr);
                return eng->make_object(ref);
            };

            // Create setter that assigns through the property to trigger the signal
            auto setter = [prop_name](T& self, ValueT val) {
                jai::property_base* base_prop = self.property_mgr.get(prop_name);
                if (!base_prop) return;
                jai::observable_property<ValueT>* obs_prop = dynamic_cast<jai::observable_property<ValueT>*>(base_prop);
                if (obs_prop) {
                    *obs_prop = std::move(val);
                }
            };

            property(prop_name, std::move(getter), std::move(setter));

            // Also bind the legacy on_<prop>_change method for backwards compatibility
            bind_observable_property<ValueT>(prop_name);
        } else {
            // Regular (non-observable) properties just return the value directly
            auto getter = [prop_name, eng](T& self) -> script_value {
                if (auto* prop = self.property_mgr.template get<ValueT>(prop_name)) {
                    return script_value_from_cpp(prop->get(), eng);
                }
                return script_value(std::monostate{}, eng);
            };

            auto setter = [prop_name](T& self, ValueT val) {
                jai::property_base* base_prop = self.property_mgr.get(prop_name);
                if (!base_prop) return;
                jai::property<ValueT>* regular_prop = dynamic_cast<jai::property<ValueT>*>(base_prop);
                if (regular_prop) {
                    regular_prop->get() = std::move(val);
                }
            };

            property(prop_name, std::move(getter), std::move(setter));
        }

        return true;
    }

    // Bind observation method for an observable property
    // Allows scripts to connect callbacks that fire when the property changes
    // Supports both:
    //   - obj.on_score_change(callback)   (legacy API)
    //   - obj.score.on_change(callback)   (new API via transparent wrapper)
    template<typename ValueT>
    void bind_observable_property(const std::string& prop_name) {
        engine* eng = &engine_;
        std::string observe_method_name = "on_" + prop_name + "_change";

        // Method signature: on_<prop>_change(callback) where callback(old_value, new_value)
        // The connection persists for the lifetime of the owner object
        method(observe_method_name, [prop_name, eng](T& self, const script_value& callback) -> script_value {
            if (!callback.is_function()) {
                throw runtime_error("on_" + prop_name + "_change requires a function argument");
            }

            // Get the property_base by name and cast to observable_property
            auto* base_prop = self.property_mgr.get(prop_name);
            if (!base_prop) {
                throw runtime_error("Property '" + prop_name + "' not found");
            }

            // Dynamic cast to observable_property to access the on_change signal
            auto* obs_prop = dynamic_cast<observable_property<ValueT>*>(base_prop);
            if (!obs_prop) {
                throw runtime_error("Property '" + prop_name + "' is not observable");
            }

            // Capture the script function
            const script_function& script_func = callback.as_function();

            // Create a C++ callback that invokes the script function
            // The observable_property's on_change signal passes (old_value, new_value)
            auto cpp_callback = [script_func, eng](const ValueT& old_val, const ValueT& new_val) {
                // Convert C++ values to script_values
                script_value old_sv = script_value_from_cpp(old_val, eng);
                script_value new_sv = script_value_from_cpp(new_val, eng);

                // Call the script function
                auto result = script_func({old_sv, new_sv});
                // Ignore result (void callback)
            };

            // Connect to the signal and get the receiver
            auto recv = obs_prop->on_change.connect(std::move(cpp_callback));

            // Track the receiver in property_manager so it lives as long as the owner
            self.property_mgr.template track_receiver<void(const ValueT&, const ValueT&)>(recv);

            return script_value(std::monostate{}, eng); // Return null (disconnect not yet supported)
        });
    }

    // Register the observable_property_ref<ValueT> wrapper type if not already registered.
    // This wrapper enables the player.score.on_change(callback) syntax by:
    //   1. Having an on_change method for callback registration
    //   2. Being a transparent wrapper that forwards operations to the underlying value
    template<typename ValueT>
    void ensure_observable_ref_type_registered() {
        using RefType = observable_property_ref<ValueT>;

        // Generate type name based on ValueT
        std::string type_name = "observable_property_ref<" + std::string(typeid(ValueT).name()) + ">";

        // Check if already registered (engine tracks registered types)
        if (engine_.is_type_registered(type_name)) {
            return;
        }

        // Register the wrapper type
        dynamic_binder<RefType>(engine_, type_name)
            // Transparent wrapper - unwraps to the value type for arithmetic, etc.
            .transparent_wrapper([](RefType& self) -> ValueT {
                return self.get();
            })
            // on_change method - connects a callback to the property's signal
            .method("on_change", [](RefType& self, const script_value& callback) -> script_value {
                if (!callback.is_function()) {
                    throw runtime_error("on_change requires a function argument");
                }

                auto* obs_prop = self.property();
                auto* prop_mgr = self.manager();
                if (!obs_prop || !prop_mgr) {
                    throw runtime_error("Invalid observable property reference");
                }

                engine* eng = callback.get_engine();
                const script_function& script_func = callback.as_function();

                // Create callback that invokes the script function
                auto cpp_callback = [script_func, eng](const ValueT& old_val, const ValueT& new_val) {
                    script_value old_sv = script_value_from_cpp(old_val, eng);
                    script_value new_sv = script_value_from_cpp(new_val, eng);
                    auto result = script_func({old_sv, new_sv});
                };

                // Connect and track the receiver
                auto recv = obs_prop->on_change.connect(std::move(cpp_callback));
                prop_mgr->template track_receiver<void(const ValueT&, const ValueT&)>(recv);

                return script_value(std::monostate{}, eng);
            })
            .build();
    }

    // Try to bind a property using the global type converter registry
    // This handles custom types like MV::Point<int, int> that have been registered
    // via property_type_converter_registrar
    //
    // TODO: Implement fully once property_manager has type-erased value access
    // For now, complex property types need to be bound manually
    void try_bind_property_from_registry(const std::string& /*prop_name*/, std::type_index value_type) {
        // Check if converter exists (for future use)
        if (!property_type_converter_registry::instance().has_converter(value_type)) {
            return;  // No converter registered - skip silently
        }
        // Complex type binding not yet implemented
        // Custom types need to be bound manually for now
    }

    // Helper to convert C++ value to script_value
    // For primitive types, uses direct construction for efficiency.
    // For custom types (classes registered via jai::registrar), delegates to value_converter
    // which uses the engine's type conversion registry.
    template<typename ValueT>
    static script_value script_value_from_cpp(const ValueT& value, engine* eng) {
        if constexpr (std::is_same_v<ValueT, int> || std::is_same_v<ValueT, int32_t>) {
            return script_value(static_cast<script_int>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, int64_t>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<ValueT, uint64_t> || std::is_same_v<ValueT, uint32_t> ||
                            std::is_same_v<ValueT, uint16_t> || std::is_same_v<ValueT, uint8_t> ||
                            std::is_same_v<ValueT, int16_t> || std::is_same_v<ValueT, int8_t>) {
            return script_value(static_cast<script_int>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, float>) {
            return script_value(static_cast<script_float>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, double>) {
            return script_value(static_cast<script_float>(value), eng);
        } else if constexpr (std::is_same_v<ValueT, bool>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<ValueT, std::string>) {
            return script_value(value, eng);
        } else {
            // For custom types, use value_converter which handles registered classes
            // This works for any type registered via jai::registrar
            return detail::value_converter<ValueT>::to(value, eng);
        }
    }

    // Helper to convert script_value to C++ type
    // For primitive types, uses direct accessor methods.
    // For custom types, delegates to value_converter which handles registered classes.
    template<typename ValueT>
    static ValueT script_value_to_cpp(const script_value& val, engine* eng = nullptr) {
        if constexpr (std::is_same_v<ValueT, int> || std::is_same_v<ValueT, int32_t>) {
            return static_cast<ValueT>(val.as_int());
        } else if constexpr (std::is_same_v<ValueT, int64_t>) {
            return val.as_int();
        } else if constexpr (std::is_same_v<ValueT, uint64_t> || std::is_same_v<ValueT, uint32_t> ||
                            std::is_same_v<ValueT, uint16_t> || std::is_same_v<ValueT, uint8_t> ||
                            std::is_same_v<ValueT, int16_t> || std::is_same_v<ValueT, int8_t>) {
            return static_cast<ValueT>(val.as_int());
        } else if constexpr (std::is_same_v<ValueT, float>) {
            return static_cast<float>(val.as_float());
        } else if constexpr (std::is_same_v<ValueT, double>) {
            return val.as_float();
        } else if constexpr (std::is_same_v<ValueT, bool>) {
            return val.as_bool();
        } else if constexpr (std::is_same_v<ValueT, std::string>) {
            return val.as_string();
        } else {
            // For custom types, use value_converter which handles registered classes
            return detail::value_converter<ValueT>::from(val, eng);
        }
    }

public:

    // Finalize registration
    void build() {
        // Prevent double-build
        if (built_) return;
        built_ = true;

        // Auto-register default constructor if:
        // 1. User didn't explicitly register any constructor
        // 2. Type is default constructible
        // 3. Type is not abstract
        if constexpr (!std::is_abstract_v<T> && std::is_default_constructible_v<T>) {
            if (!has_explicit_constructor_) {
                // Auto-register default constructor
                engine* engine_ptr = &engine_;
                engine_.add_overloaded_function(class_name_, 0, [class_def = class_def_, class_name = class_name_, engine_ptr](const std::vector<script_value>& args) -> script_value {
                    // Create the C++ object
                    std::shared_ptr<T> cpp_obj;

                    // Check if T has an engine constructor and use it if available
                    if constexpr (has_engine_constructor<T>) {
                        cpp_obj = std::make_shared<T>(engine_ptr);
                    } else {
                        cpp_obj = std::make_shared<T>();
                    }

                    // Create a class_instance to hold it
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the class_instance as a special field
                    instance->set_field(instance->get_cpp_object_field_id(), script_value::make_cpp_object(class_name, class_def->get_type_id(), cpp_obj, engine_ptr));

                    // Return the class_instance wrapped in a value
                    return script_value::make_object(class_name, instance, engine_ptr);
                });
            }
        }

        // Register automatic copy function for copyable types
        if constexpr (std::is_copy_constructible_v<T>) {
            class_def_->set_copy_function([](const void* src) -> std::shared_ptr<void> {
                const T* typed_src = static_cast<const T*>(src);
                return std::make_shared<T>(*typed_src);
            });
            
            // If this is a polymorphic type with a base class, register polymorphic copier
            if constexpr (std::is_polymorphic_v<T>) {
                if (has_base_class_) {
                    engine_.register_polymorphic_copier<T>(
                        std::type_index(typeid(T)), 
                        base_type_index_,
                        [](const void* obj) -> std::shared_ptr<void> {
                            const T* typed = static_cast<const T*>(obj);
                            return std::make_shared<T>(*typed);
                        }
                    );
                }
            }
        }
        
        engine_.add_class<T>(class_name_, class_def_);

        // Register serialization metadata with the engine's registry (with type_index for runtime lookup)
        engine_.get_serialization_registry().register_class(class_name_, std::type_index(typeid(T)), serialization_metadata_);
        
        
        // Register custom conversions for shared_ptr<T> to handle class_instance wrapping
        if constexpr (!std::is_abstract_v<T>) {
            engine_.add_custom_conversion<std::shared_ptr<T>>(
                // From script_value to shared_ptr<T>
                [class_name = class_name_](const script_value& v) -> std::shared_ptr<T> {
                    if (v.is_object()) {
                        // Check if this is a raw C++ object or a class_instance wrapper
                        auto obj_holder = v.get_object_holder();
                        if (obj_holder) {
                            if (obj_holder->is_class_instance_wrapper) {
                                // This is a class_instance wrapper - extract the class_instance then get the C++ object
                                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                                return instance->get_cpp_object_as<T>();
                            } else {
                                // This is a raw C++ object - extract it directly
                                return std::static_pointer_cast<T>(obj_holder->data);
                            }
                        }
                    }
                    throw runtime_error("Cannot convert script_value to shared_ptr<" + class_name + ">");
                },
                // From shared_ptr<T> to script_value
                [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const std::shared_ptr<T>& obj) -> script_value {
                    if (!obj) {
                        return script_value(std::monostate{}, engine_ptr);
                    }
                    
                    // Create a class_instance to wrap the object
                    auto instance = class_def->create_instance();
                    instance->set_field(instance->get_cpp_object_field_id(),
                        script_value::make_cpp_object(class_name, class_def->get_type_id(), std::static_pointer_cast<void>(obj), engine_ptr));
                    
                    // Return wrapped in script_value
                    return script_value::make_object(class_name, std::static_pointer_cast<void>(instance), engine_ptr);
                }
            );
        }
        
        // Register converters for copyable types only (used for value conversions requiring copy)
        // Non-copyable types like property_owner classes must use shared_ptr conversions instead
        if constexpr (!std::is_abstract_v<T> && std::is_copy_constructible_v<T>) {
            // Register a C++ type converter that creates class_instance objects
            engine_.register_type_converter_impl(typeid(T),
                [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const void* obj) -> script_value {
                    // Create a class_instance using the class definition
                    // This properly initializes all fields with their defaults
                    auto instance = class_def->create_instance();

                    // Create a shared_ptr to the C++ object (by copying)
                    auto cpp_obj = std::make_shared<T>(*static_cast<const T*>(obj));

                    // Store the C++ object in the class_instance
                    if (auto eng = engine_ptr) {
                        uint64_t cpp_object_field_id = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
                        instance->set_field(cpp_object_field_id, script_value::make_cpp_object(class_name,
                            class_def->get_type_id(), std::static_pointer_cast<void>(cpp_obj), engine_ptr));
                    }

                    // Return the class_instance wrapped in a value
                    if (auto eng = engine_ptr) {
                        return script_value::make_object(class_name,
                            std::static_pointer_cast<void>(instance), eng);
                    }
                    throw runtime_error("Engine no longer exists");
                });

            // Also register with conversion registry for container conversions
            auto custom_conv = engine_.get_conversion_registry();
            if (custom_conv && !custom_conv->template has_conversion<T>()) {
                custom_conv->template register_conversion<T>(
                    // From script_value to T
                    [](const script_value& v) -> T {
                        // Direct access to avoid template recursion issues
                        if (v.type() != script_value_type::jai_object_type) {
                            throw runtime_error("Cannot convert non-object to " + std::string(typeid(T).name()));
                        }

                        // Get object holder using public method
                        auto objHolder = v.get_object_holder();
                        if (!objHolder) {
                            throw runtime_error("Cannot convert: script_value is not an object type");
                        }

                        if (!objHolder->is_class_instance_wrapper) {
                            throw runtime_error("Object is not a class_instance wrapper");
                        }

                        // Cast to class_instance
                        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                        if (!instance) {
                            throw runtime_error("Failed to cast to class_instance");
                        }

                        // Get the C++ object field directly - don't use has_field since it's an internal field
                        auto cpp_obj_value = instance->get_field(instance->get_cpp_object_field_id());
                        if (cpp_obj_value.is_null()) {
                            throw runtime_error("C++ object field not found in class_instance");
                        }

                        // Extract the C++ object directly to avoid recursion
                        if (cpp_obj_value.type() != script_value_type::jai_object_type) {
                            throw runtime_error("C++ object field is not an object");
                        }

                        auto cpp_objHolder = cpp_obj_value.get_object_holder();
                        if (!cpp_objHolder) {
                            throw runtime_error("Field value is not an object type");
                        }
                        auto cpp_obj = std::static_pointer_cast<T>(cpp_objHolder->data);

                        if (!cpp_obj) {
                            throw runtime_error("Failed to cast C++ object to expected type");
                        }

                        return *cpp_obj;
                    },
                    // From T to script_value
                    [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const T& obj) -> script_value {
                        // Create a class_instance using the class definition
                        auto instance = class_def->create_instance();

                        // Create a shared_ptr to the C++ object (by copying)
                        auto cpp_obj = std::make_shared<T>(obj);

                        // Store the C++ object in the class_instance
                        instance->set_field(instance->get_cpp_object_field_id(),
                            script_value::make_cpp_object(class_name,
                                class_def->get_type_id(), std::static_pointer_cast<void>(cpp_obj), engine_ptr));

                        // Return the class_instance wrapped in a value
                        if (auto eng = engine_ptr) {
                            return script_value::make_object(class_name,
                                std::static_pointer_cast<void>(instance), eng);
                        }
                        throw runtime_error("Engine no longer exists");
                    }
                );
            }
        }

        // Register shared_ptr<T> conversions for all non-abstract types (doesn't require copy)
        if constexpr (!std::is_abstract_v<T>) {
            auto custom_conv = engine_.get_conversion_registry();

            // Register std::shared_ptr<T> conversion
            if (!custom_conv->template has_conversion<std::shared_ptr<T>>()) {
                custom_conv->template register_conversion<std::shared_ptr<T>>(
                    // From script_value to std::shared_ptr<T>
                    [class_name = class_name_](const script_value& v) -> std::shared_ptr<T> {
                        // Direct extraction logic to avoid circular dependency
                        if (v.type() != script_value_type::jai_object_type) {
                            throw runtime_error("Cannot convert non-object to shared_ptr<" + class_name + ">");
                        }
                        
                        // Get object holder using public method
                        auto objHolder = v.get_object_holder();
                        if (!objHolder) {
                            throw runtime_error("Cannot convert: script_value is not an object type");
                        }
                        
                        // Check if it's a raw C++ object (not wrapped in class_instance)
                        if (!objHolder->is_class_instance_wrapper) {
                            // Direct C++ object - just cast and return
                            return std::static_pointer_cast<T>(objHolder->data);
                        }
                        
                        // It's a class_instance wrapper - extract the C++ object
                        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                        if (!instance) {
                            throw runtime_error("Failed to cast to class_instance");
                        }
                        
                        // Get the C++ object field
                        auto cpp_obj_value = instance->get_field(instance->get_cpp_object_field_id());
                        if (cpp_obj_value.is_null()) {
                            throw runtime_error("C++ object field not found in class_instance");
                        }
                        
                        // Extract the C++ object
                        if (cpp_obj_value.type() != script_value_type::jai_object_type) {
                            throw runtime_error("C++ object field is not an object");
                        }
                        
                        auto cpp_objHolder = cpp_obj_value.get_object_holder();
                        if (!cpp_objHolder) {
                            throw runtime_error("Field value is not an object type");
                        }
                        return std::static_pointer_cast<T>(cpp_objHolder->data);
                    },
                    // From std::shared_ptr<T> to script_value
                    [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const std::shared_ptr<T>& obj) -> script_value {
                        if (!obj) {
                            return script_value(std::monostate{}, engine_ptr); // null
                        }
                        
                        // Create a class_instance using the class definition
                        auto instance = class_def->create_instance();
                        
                        // Store the shared_ptr directly (no copy needed)
                        instance->set_field(instance->get_cpp_object_field_id(),
                            script_value::make_cpp_object(class_name,
                                class_def->get_type_id(), std::static_pointer_cast<void>(obj), engine_ptr));
                        
                        // Return the class_instance wrapped in a value
                        if (auto eng = engine_ptr) {
                            return script_value::make_object(class_name, 
                                std::static_pointer_cast<void>(instance), eng);
                        }
                        throw runtime_error("Engine no longer exists");
                    }
                );
            }
        }
        
        // Automatically register container conversions for this type
        // Do this AFTER registering the base type conversion
        register_container_conversions();
    }
    
private:
    // Helper to apply base classes for each type in a tuple
    // Collects all base definitions and sets them at once for efficiency
    // (single diamond check, single cache invalidation)
    // Uses std::type_identity to avoid requiring default constructors on base classes
    template<typename... Bases>
    void apply_base_classes_from_tuple(std::type_identity<std::tuple<Bases...>>) {
        if constexpr (sizeof...(Bases) == 0) {
            return;  // No bases to register
        } else if constexpr (sizeof...(Bases) == 1) {
            // Single inheritance - use base_class<> directly
            (base_class<Bases>(), ...);
        } else {
            // Multiple inheritance - collect all definitions and use set_parents()
            // This is more efficient: single diamond check instead of N checks
            std::vector<std::shared_ptr<class_definition>> parent_defs;
            parent_defs.reserve(sizeof...(Bases));

            // Collect base definitions (fold expression with static_assert validation)
            (collect_base_definition<Bases>(parent_defs), ...);

            // Set all parents at once
            if (!parent_defs.empty()) {
                if (!class_def_->set_parents(parent_defs)) {
                    throw std::runtime_error("Diamond inheritance detected: class '" + class_name_ +
                        "' would have multiple paths to the same base class");
                }
                has_base_class_ = true;
                // Store first base type index for polymorphic copy
                base_type_index_ = std::type_index(typeid(std::tuple_element_t<0, std::tuple<Bases...>>));
            }
        }
    }

    // Helper to collect a single base class definition
    template<typename Base>
    void collect_base_definition(std::vector<std::shared_ptr<class_definition>>& parent_defs) {
        static_assert(std::is_base_of_v<Base, T>,
                      "Specified type is not a base class of this class");
        auto base_def = engine_.get_class_definition_by_type(std::type_index(typeid(Base)));
        if (base_def) {
            parent_defs.push_back(base_def);
        }
    }

    engine& engine_;
    std::string class_name_;
    std::shared_ptr<class_definition> class_def_;
    serialization::class_metadata serialization_metadata_;
    bool has_base_class_ = false;
    std::type_index base_type_index_ = std::type_index(typeid(void));
    bool has_explicit_constructor_ = false;  // Track if user registered any constructor
    bool built_ = false;  // Track if build() was called
    
    // Automatically register container conversions for type T
    void register_container_conversions() {
        if constexpr (!std::is_abstract_v<T>) {
            // Get the conversion manager for high-level API
            auto conv_mgr = engine_.get_conversion_manager();
            
            // Register base type conversion
            if constexpr (std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>) {
                engine_.add_custom_conversion<T>(
                    // From script_value to T
                    [class_name = class_name_](const script_value& v) -> T {
                        if (v.is_object()) {
                            auto instance = v.as<std::shared_ptr<class_instance>>();
                            if (instance) {
                                return *instance->get_cpp_object_as<T>();
                            }
                        }
                        throw runtime_error("Cannot convert script_value to " + class_name);
                    },
                    // From T to script_value (create a class_instance)
                    [class_def = class_def_, class_name = class_name_, engine_ptr = &engine_](const T& obj) -> script_value {
                        auto instance = class_def->create_instance();
                        auto cpp_obj = std::make_shared<T>(obj);
                        instance->set_field(instance->get_cpp_object_field_id(),
                            script_value::make_cpp_object(class_name,
                                class_def->get_type_id(), std::static_pointer_cast<void>(cpp_obj), engine_ptr));
                        return script_value::make_object(class_name, instance, engine_ptr);
                    }
                );
            }
            
            // Register vector conversions
            // Only register value-type vector if T is copyable
            if constexpr (std::is_copy_constructible_v<T>) {
                conv_mgr.add_vector_conversion<T>();
            }
            conv_mgr.add_vector_conversion<std::shared_ptr<T>>();

            // Register common map conversions with this type as value
            // Only register T map conversions if T is copyable and default constructible (required for std::map)
            if constexpr (std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>) {
                conv_mgr.add_map_conversion<std::string, T>();
                conv_mgr.add_map_conversion<int, T>();
                conv_mgr.add_map_conversion<int64_t, T>();
            }
            // shared_ptr<T> is always default constructible
            conv_mgr.add_map_conversion<std::string, std::shared_ptr<T>>();
            conv_mgr.add_map_conversion<int, std::shared_ptr<T>>();
            conv_mgr.add_map_conversion<int64_t, std::shared_ptr<T>>();
            
            // Register bound array conversions for zero-copy performance
            conv_mgr.add_bound_array_conversion<T>();
            conv_mgr.add_bound_array_conversion<std::shared_ptr<T>>();
            
            // Register bound map conversions
            // Only register for default constructible types
            if constexpr (std::is_default_constructible_v<T>) {
                conv_mgr.add_bound_map_conversion<std::string, T>();
                conv_mgr.add_bound_map_conversion<int, T>();
            }
            // shared_ptr<T> is always default constructible
            conv_mgr.add_bound_map_conversion<std::string, std::shared_ptr<T>>();
            conv_mgr.add_bound_map_conversion<int, std::shared_ptr<T>>();
            
            // Register common map conversions with this type as key (if it has operator<)
            if constexpr (std::is_default_constructible_v<T> && requires(const T& a, const T& b) { a < b; }) {
                conv_mgr.add_map_conversion<T, std::string>();
                conv_mgr.add_map_conversion<T, int>();
                conv_mgr.add_map_conversion<T, int64_t>();
                conv_mgr.add_map_conversion<T, double>();
                conv_mgr.add_map_conversion<T, bool>();
            }
        }
    }
    
    // Helper method for creating objects with arguments
    template<typename... Args, size_t... Is>
    static std::shared_ptr<T> createObjectImpl(const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        return std::make_shared<T>(detail::value_converter<Args>::from(args[Is], eng)...);
    }
    
    // Helper method for calling member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callMethodImpl(T* obj, R(T::*method)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling const member functions
    template<typename R, typename... Args, size_t... Is>
    static script_value callConstMethodImpl(const T* obj, R(T::*method)(Args...) const, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            R result = (obj->*method)(detail::value_converter<Args>::from(args[Is + 1], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling static functions
    template<typename R, typename... Args, size_t... Is>
    static R callStaticFunctionImpl(R(*func)(Args...), const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // Create parameter storage on stack - note: no engine, so storage won't be set
        // This is fine for basic types but will fail for containers
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(nullptr, &storage);

        return func(detail::value_converter<Args>::from(args[Is], nullptr)...);
    }
    
    // Helper method for calling static callables/lambdas
    template<typename Callable, size_t... Is>
    static auto callStaticCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>) {
        // Create parameter storage on stack - note: no engine, so storage won't be set
        // This is fine for basic types but will fail for containers
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(nullptr, &storage);

        using traits = detail::function_traits<std::decay_t<Callable>>;
        using args_tuple = typename traits::argument_types;

        return callable(detail::value_converter<std::tuple_element_t<Is, args_tuple>>::from(args[Is], nullptr)...);
    }
    
    // Helper method for calling lambdas/callables
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callCallableImpl(Callable&& callable, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            R result = callable(detail::value_converter<std::tuple_element_t<Is, ArgsTuple>>::from(args[Is], eng)...);
            return detail::value_converter<R>::to(result, eng);
        }
    }
    
    // Helper method for calling lambdas with self parameter
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callLambdaWithSelf(Callable&& callable, T* self, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // We need to call the lambda with:
        // - self as the first argument
        // - remaining args starting from args[1] mapped to tuple indices 1, 2, 3...
        return callLambdaWithSelfImpl<R, ArgsTuple, Callable>(
            std::forward<Callable>(callable), self, args, 
            std::make_index_sequence<sizeof...(Is) - 1>{}, eng
        );
    }
    
    // implementation helper that correctly maps arguments
    template<typename R, typename ArgsTuple, typename Callable, size_t... Is>
    static script_value callLambdaWithSelfImpl(Callable&& callable, T* self, const std::vector<script_value>& args, std::index_sequence<Is...>, engine* eng) {
        // Create parameter storage on stack
        detail::parameter_storage storage;
        detail::parameter_storage::scope_guard guard(eng, &storage);

        if constexpr (std::is_void_v<R>) {
            // Call with self as first argument, then args[1], args[2], etc.
            callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
            return script_value(std::monostate{}, eng); // null for void
        } else {
            // For return type R&, we need special handling for method chaining
            if constexpr (std::is_reference_v<R> && std::is_same_v<std::remove_reference_t<R>, T>) {
                // Method returns T&, so we should return the original 'this' script_value for chaining
                callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
                return args[0]; // Return the original 'this' for chaining
            } else {
                R result = callable(*self, detail::value_converter<std::tuple_element_t<Is + 1, ArgsTuple>>::from(args[Is + 1], eng)...);
                return detail::value_converter<R>::to(result, eng);
            }
        }
    }
    
};


// Context-based factory implementations use type erasure (see make_context_extractor above)
// to avoid MSVC template instantiation issues with incomplete archive_reader type.
// The make_context_extractor function is instantiated at call sites where archive_reader is fully defined.

} // namespace jai

// Include script class after class_definition is complete
#include "script_class.hpp"

#endif // __JAISCRIPT_CORE_dynamic_binder_HPP__
