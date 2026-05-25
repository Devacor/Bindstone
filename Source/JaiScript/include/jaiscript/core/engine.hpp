#pragma once

#ifndef __JAISCRIPT_CORE_ENGINE_HPP__
#define __JAISCRIPT_CORE_ENGINE_HPP__

#include "types.hpp"
#include "value.hpp"
#include "function_binder.hpp"
#include "binding_traits.hpp"
#include "conversion_registry.hpp"
#include "conversion_registry_impl.hpp"
// conversion_registry_templates.hpp moved to after engine class definition
#include "bound_array.hpp"
#include "bound_map.hpp"
#include <jaiscript/serialization/serialization_metadata.hpp>
#include <memory>
#include <optional>
#include <unordered_set>
#include <typeindex>
#include <iosfwd>

namespace jai {

    // Class constants namespace (shared with dynamic_binder.hpp)
    namespace class_constants {
        inline const std::string CPP_OBJECT_FIELD = "_cpp_object";
    }

    // Forward declarations
    class class_definition;
    class class_registry;
    class execution_backend;
    class string_symbolizer;

    namespace serialization {
        class serialization_registry;
        // Note: serialization metadata structs (property_metadata, class_metadata, etc.)
        // are defined in serialization_metadata.hpp which is included above
    }
    
    // Backend type enumeration
    enum class backend_type {
        interpreter,    // Default tree-walk interpreter
        jvm,           // Bytecode virtual machine
        auto_select    // Automatic selection based on heuristics
    };

    // Extended type info for overload resolution - stores base type + optional C++ type for objects
    struct param_type_info {
        script_value_type base_type;
        std::type_index cpp_type{typeid(void)};  // For object types, used to lookup class def

        param_type_info() : base_type(script_value_type::jai_null_type) {}
        param_type_info(script_value_type t) : base_type(t), cpp_type(typeid(void)) {}
        param_type_info(script_value_type t, std::type_index idx) : base_type(t), cpp_type(idx) {}

        bool operator==(const param_type_info& other) const {
            return base_type == other.base_type && cpp_type == other.cpp_type;
        }
    };

    class engine : public std::enable_shared_from_this<engine> {
    private:
        engine();
        
    public:
        ~engine();
        
        // Factory method to ensure engines are always created as shared_ptr
        static std::shared_ptr<engine> make() {
            auto eng = std::shared_ptr<engine>(new engine());
            eng->initialize_engine_reference();
            return eng;
        }

        // Helper methods for getting engine references
        // Use these when you need to pass engine references to systems that need weak_ptr or shared_ptr
        std::weak_ptr<engine> get_weak_engine() {
            return weak_from_this();
        }

        std::shared_ptr<engine> get_shared_engine() {
            return shared_from_this();
        }

        // Non-copyable, moveable
        engine(const engine&) = delete;
        engine& operator=(const engine&) = delete;
        engine(engine&&) noexcept;
        engine& operator=(engine&&) noexcept;
        
        
        // Primary API - execute methods
        virtual script_value execute(const std::string& scriptContent);
        script_value execute(const std::string& scriptContent, const instance_variables& instanceVars);
        
        script_value execute_file(const std::string& scriptPath);
        script_value execute_file(const std::string& scriptPath, const instance_variables& instanceVars);
        
        // Deprecated - use execute methods instead
        [[deprecated("Use execute() instead")]]
        script_value eval(const std::string& scriptContent);
        
        [[deprecated("Use execute_file() instead")]]
        script_value fileEval(const std::string& scriptPath);
        
        // Global registration
        void add_global(const std::string& name, script_value value, bool is_serializable = true);
        
        // Convenience overload for add_global that automatically wraps values
        template<typename T>
        void add_global(const std::string& name, const T& value, bool is_serializable = true) {
            add_global(name, make_value(value), is_serializable);
        }
        
        // Add a global reference that binds to a C++ variable
        // Supports both primitive types AND custom objects registered via dynamic_binder
        template<typename T>
        void add_global_ref(const std::string& name, T& value, bool is_serializable = false) {
            // For primitive types, use cpp_bound for direct binding
            if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T> ||
                          std::is_same_v<T, bool> || std::is_same_v<T, std::string> ||
                          std::is_same_v<T, char>) {
                auto ref = script_value::make_cpp_bound(&value, this);
                add_global(name, std::move(ref), is_serializable);
            } else {
                // For custom types, create a non-owning shared_ptr using aliasing constructor
                // This allows script to access the object without taking ownership
                // WARNING: Caller must ensure 'value' outlives the script engine!
                auto lifetime_tracker = std::make_shared<bool>(true);
                std::shared_ptr<T> non_owning_ptr(lifetime_tracker, &value);
                add_global(name, make_object(non_owning_ptr), is_serializable);
            }
        }
        
        // Object creation through registered class system
        template<typename T, typename... Args>
        script_value make_object(Args&&... args) {
            auto data = std::make_shared<T>(std::forward<Args>(args)...);
            // Delegate to the shared_ptr overload which handles wrapping properly
            return make_object(data);
        }
        
        // Object creation from existing shared_ptr
        // NOTE: This method currently doesn't support property access on C++ objects
        // For C++ objects with properties, create them via script constructor instead
        template<typename T>
        script_value make_object(std::shared_ptr<T> data) {
            std::string type_name = get_registered_name<T>();

            // Try to get the class definition to wrap the object properly
            try {
                auto class_def = get_class_definition_by_type(std::type_index(typeid(T)));
                if (class_def) {
                    // Create a class_instance to wrap the object (same as constructors do)
                    auto instance = class_def->create_instance();

                    // Store the C++ object in the special field (same pattern as dynamic_binder)
                    // Intern the constant field name to ID
                    uint64_t cpp_object_field_id = symbolize(class_constants::CPP_OBJECT_FIELD);
                    instance->set_field(cpp_object_field_id,
                        script_value::make_cpp_object(type_name, class_def->get_type_id(), std::static_pointer_cast<void>(data), this));

                    // Return the class_instance wrapped in a script_value
                    return script_value::make_object(type_name, instance, this);
                }
            } catch (...) {
                // Class not registered, fall back to raw object storage
            }

            // Fallback: store as raw C++ object (properties won't work)
            auto type_id = get_symbolizer()->intern(type_name);
            return script_value::make_cpp_object(type_name, type_id, std::static_pointer_cast<void>(data), this);
        }

        // Convenient script_value creation methods
        // Usage: engine->make_value(42) instead of script_value(42, engine)
        // For primitive types (int, float, bool, char, string, string literals) - direct construction
        template<typename T>
        requires (std::is_arithmetic_v<std::remove_cvref_t<T>> ||
                  std::is_same_v<std::remove_cvref_t<T>, script_string> ||
                  std::is_same_v<std::remove_cvref_t<T>, std::string> ||
                  std::is_convertible_v<T, const char*> ||  // Handles const char*, const char[N], etc.
                  std::is_same_v<std::remove_cvref_t<T>, std::monostate>)
        script_value make_value(T&& value) {
            return script_value(std::forward<T>(value), this);
        }

        // For non-primitive lvalue references - use convert_reference_with_registry (defined in engine_impl.hpp)
        template<typename T>
        requires (!std::is_arithmetic_v<std::remove_cvref_t<T>> &&
                  !std::is_same_v<std::remove_cvref_t<T>, script_string> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
                  !std::is_convertible_v<T, const char*> &&  // Exclude string literals and char pointers
                  !std::is_same_v<std::remove_cvref_t<T>, std::monostate> &&
                  std::is_lvalue_reference_v<T>)
        script_value make_value(T&& value);  // Implemented in engine_impl.hpp

        // For non-primitive rvalue references (class types passed by value)
        // Uses convert_custom_type_with_registry to handle the conversion
        template<typename T>
        requires (!std::is_arithmetic_v<std::remove_cvref_t<T>> &&
                  !std::is_same_v<std::remove_cvref_t<T>, script_string> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
                  !std::is_convertible_v<T, const char*> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::monostate> &&
                  !std::is_pointer_v<std::remove_cvref_t<T>> &&
                  std::is_rvalue_reference_v<T&&> &&
                  !std::is_lvalue_reference_v<T>)
        script_value make_value(T&& value);  // Implemented in engine_impl.hpp

        // For shared_ptr types - convert to script object
        template<typename T>
        script_value make_value(const std::shared_ptr<T>& value) {
            if (!value) {
                return script_value(std::monostate{}, this);
            }
            return make_object(value);
        }

        template<typename T>
        script_value make_value(std::shared_ptr<T>&& value) {
            if (!value) {
                return script_value(std::monostate{}, this);
            }
            return make_object(std::move(value));
        }

        // For raw pointer types - create non-owning bound reference
        // This handles signals that emit raw pointers like void(T*, int)
        template<typename T>
        requires (!std::is_same_v<T, char> && !std::is_same_v<T, const char>)  // Exclude char* (handled as strings)
        script_value make_value(T* value) {
            if (!value) {
                return script_value(std::monostate{}, this);
            }
            return script_value::make_cpp_bound(value, this);
        }

        // Create null/void value
        script_value make_null() {
            return script_value(std::monostate{}, this);
        }

        // Create array value
        script_value make_array() {
            return script_value::make_array(nullptr, this);
        }

        // Create map value
        script_value make_map() {
            return script_value::make_map(nullptr, nullptr, this);
        }

        // Create empty weak_ptr value with specified type
        script_value make_empty_weak_ptr(type_info_ptr weak_ptr_type) {
            return script_value::make_empty_weak_ptr(weak_ptr_type, this);
        }
        
        // === FUNCTION REGISTRATION ===
        
        // Variadic functions - handle any number of arguments at runtime
        // Use this for functions like print(), max(), or functions with optional parameters
        // Example: engine.add_variadic_function("print", [](const std::vector<script_value>& args) { ... });
        void add_variadic_function(const std::string& name, script_function func);
        void add_functionWithArity(const std::string& name, script_function func, size_t arity);
        
        // Typed function registration with automatic overloading
        // Use this for functions with specific C++ signatures
        // Example: engine.add_function("add", [](int a, int b) -> int { return a + b; });
        template<typename Func>
        void add_function(const std::string& name, Func&& func) {
            // Check for rejected container references
            binding_traits::check_binding_validity<Func>(name);
            
            using traits = detail::function_traits<std::decay_t<Func>>;
            using return_type = typename traits::return_type;
            constexpr size_t arity = traits::arity;
            
            auto binder = make_functionBinder(std::forward<Func>(func), this);
            script_function boundFunc = binder.bind();
            
            // Wrap the function to handle type conversion for return values
            script_function wrappedFunc = wrapFunctionForTypeConversion<return_type>(std::move(boundFunc));
            
            // Extract parameter types for type-based overloading (with C++ type info for objects)
            std::vector<param_type_info> paramTypes = extract_parameter_types_with_info<Func>();

            // Auto-detect numeric operator overrides and set flag
            if (arity == 2 && isNumericOperator(name)) {
                if (hasNumericOperands(paramTypes)) {
                    setHasCustomNumericOps(true);
                }
            }

            // Check if this should be registered as an overload
            if (has_function(name)) {
                // Automatically convert to overloaded function with type info
                add_overloaded_function_with_full_types(name, arity, wrappedFunc, paramTypes);
            } else {
                // Register normally but store arity info for future overloading
                add_function_with_arity_and_full_types(name, wrappedFunc, arity, paramTypes);
            }
        }
        
    private:
        // Helper methods for numeric operator detection
        bool isNumericOperator(const std::string& name) const {
            return name == "+" || name == "-" || name == "*" || name == "/" || name == "%" ||
                   name == "<" || name == ">" || name == "<=" || name == ">=" || 
                   name == "==" || name == "!=" || name == "<=>";
        }
        
        bool hasNumericOperands(const std::vector<param_type_info>& paramTypes) const {
            if (paramTypes.size() != 2) return false;

            auto isNumericType = [](script_value_type t) {
                return t == script_value_type::jai_int_type ||
                       t == script_value_type::jai_float_type ||
                       t == script_value_type::jai_char_type ||
                       t == script_value_type::jai_bool_type;
            };
            
            return isNumericType(paramTypes[0].base_type) && isNumericType(paramTypes[1].base_type);
        }
        
        void setHasCustomNumericOps(bool value);
        
    public:
        // Overloaded function registration
        void add_overloaded_function(const std::string& name, size_t argCount, script_function func);
        void add_overloaded_functionWithTypes(const std::string& name, size_t argCount, script_function func, const std::vector<script_value_type>& paramTypes);
        void add_overloaded_function_with_full_types(const std::string& name, size_t argCount, script_function func, const std::vector<param_type_info>& paramTypes);
        void add_functionWithArityAndTypes(const std::string& name, script_function func, size_t arity, const std::vector<script_value_type>& paramTypes);
        void add_function_with_arity_and_full_types(const std::string& name, script_function func, size_t arity, const std::vector<param_type_info>& paramTypes);
        
        // Class registration
        // Register a class with its type information
        template<typename T>
        void add_class(const std::string& name, std::shared_ptr<class_definition> classDef) {
            add_class_impl(name, classDef);
            register_type_name_impl(typeid(T).name(), name);
            register_class_by_type(std::type_index(typeid(T)), classDef);
        }
        
        // Template type registration for parsing
        // Registers base template names (e.g., "Point" from "Point<int>") 
        // so the parser knows which identifiers can be followed by template syntax
        void register_template_type(const std::string& baseTemplateName);
        
        // Get all registered template types for parser
        std::unordered_set<std::string> get_registered_template_types() const;
        
        // Get class definition by name
        std::shared_ptr<class_definition> get_class_definition(const std::string& name) const;

        // Get class definition by type_id (faster than string lookup)
        std::shared_ptr<class_definition> get_class_definition(uint64_t type_id) const;

        // Check if a type is registered (by name)
        bool is_type_registered(const std::string& name) const {
            return get_class_definition(name) != nullptr;
        }

        // Get registered script name for a C++ type
        template<typename T>
        std::string get_registered_name() const;

        // Check if type T has been registered via dynamic_binder
        template<typename T>
        bool has_registered_class() const {
            return get_class_definition_by_type(std::type_index(typeid(T))) != nullptr;
        }

        // ============================================================================
        // Static type binding - bind compile-time type info to this engine instance
        // ============================================================================
        // For types registered with JAI_STATIC_BINDER, this creates the runtime
        // class_definition and registers it with this engine.
        //
        // Usage:
        //   eng->bind_static_type<MV::Point<int>>();
        //   eng->bind_static_types<MV::Point<int>, MV::Color, MV::Size<int>>();

        template<typename T>
        void bind_static_type();  // Implemented in static_binder_impl.hpp

        template<typename... Ts>
        void bind_static_types() {
            (bind_static_type<Ts>(), ...);
        }

        // Enhanced Conversion System
        // Get the conversion manager for this engine
        conversions::conversion_manager get_conversion_manager();
        
        // Register standard vector and map conversions
        void add_standard_conversions();
        
        // Register vector conversion for a specific type
        template<typename T>
        void add_vector_conversion() {
            auto manager = get_conversion_manager();
            manager.add_vector_conversion<T>();
        }
        
        // Register map conversion for specific key/value types
        template<typename K, typename V>
        void add_map_conversion() {
            auto manager = get_conversion_manager();
            manager.add_map_conversion<K, V>();
        }
        
        // Register bound_array conversion for a specific type
        template<typename T>
        void add_bound_array_conversion() {
            auto manager = get_conversion_manager();
            manager.add_bound_array_conversion<T>();
        }
        
        // Register bound_map conversion for specific key/value types
        template<typename K, typename V>
        void add_bound_map_conversion() {
            auto manager = get_conversion_manager();
            manager.add_bound_map_conversion<K, V>();
        }
        
        // Register custom conversion for any type
        template<typename T>
        void add_custom_conversion(
            std::function<T(const script_value&)> from_func,
            std::function<script_value(const T&)> to_func
        );
        
        // Type conversion registration
        // Register a conversion between JaiScript types with a cost for overload resolution
        // Lower costs are preferred (0 = exact match, 1 = promotion, 2 = standard conversion, etc.)
        void register_type_conversion(script_value_type from, script_value_type to, int cost, 
                                   std::function<script_value(const script_value&)> converter);
        
        // Template version for C++ type safety using std::is_convertible
        template<typename From, typename To>
        void register_type_conversion(int cost = -1) {
            static_assert(std::is_convertible_v<From, To>, 
                         "Types must be convertible according to C++ rules");
            
            script_value_type fromType = mapCppTypeToValueType<From>();
            script_value_type toType = mapCppTypeToValueType<To>();
            
            // Auto-determine cost if not specified
            if (cost < 0) {
                if constexpr (std::is_same_v<From, To>) {
                    cost = 0; // Exact match
                } else if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>) {
                    cost = 1; // Promotion
                } else if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>) {
                    cost = 2; // Standard conversion (may lose precision)
                } else {
                    cost = 3; // User-defined conversion
                }
            }
            
            register_type_conversion(fromType, toType, cost, 
                [](const script_value& v) { 
                    return script_value(static_cast<To>(v.as<From>())); 
                });
        }

        // Variable access
        script_value get_variable(const std::string& name) const;
        bool has_variable(const std::string& name) const;
        bool has_function(const std::string& name) const;
        bool is_type_name(const std::string& name) const;
        
        // Get the global environment (for internal use)
        std::shared_ptr<environment> get_global_environment() const;
        
        // state management (hooks for external serialization)
        struct state {
            std::map<std::string, script_value> globals;  // Use ordered map for deterministic serialization
            // More state to be added
        };
        
        state get_state() const;
        void set_state(const state& state);
        
        // Hot-reload support
        bool can_hot_reload(const std::string& scriptPath) const;
        
        // Performance optimization
        void set_has_custom_numeric_operators(bool value);
        bool hot_reload(const std::string& scriptPath);
        
        // Get registered type name from typeid
        std::string get_registered_type_name(const std::string& typeIdName) const;
        
        // Convert a registered type to value
        script_value convert_to_value(const std::type_info& type, const void* obj) const;
        
        // Backend configuration
        void set_backend(backend_type type);
        void set_backend(std::unique_ptr<execution_backend> backend);
        backend_type get_backend_type() const;
        std::string get_backend_name() const;
        execution_backend* get_execution_backend() const;

        // Get string symbolizer for interning identifiers
        string_symbolizer* get_symbolizer();

        // Intern a string and return its unique ID (const-safe)
        uint64_t symbolize(std::string_view str) const;

        // Format a checked_result error to a human-readable string
        // Resolves symbol IDs to their string representations
        template<typename T>
        std::string format_error(const checked_result<T>& result) {
            return jai::format_error(result, *get_symbolizer());
        }

        // Register C++ type converters for custom types
        template<typename T>
        void register_type_converter(const std::string& type_name) {
            // Register a converter that creates a script_value from this type
            engine* eng = this;
            auto type_id = get_symbolizer()->intern(type_name);
            auto toValue = [type_name, type_id, eng](const T& obj) -> script_value {
                auto sharedObj = std::make_shared<T>(obj);
                return script_value::make_cpp_object(type_name, type_id, std::static_pointer_cast<void>(sharedObj), eng);
            };
            
            // Store this converter (implementation will handle the storage)
            register_type_converter_impl(typeid(T), 
                [toValue](const void* obj) -> script_value {
                    return toValue(*static_cast<const T*>(obj));
                });
        }
        
        // Get class definition by type index
        std::shared_ptr<class_definition> get_class_definition_by_type(const std::type_index& type) const;
        
        // Get the conversion registry - shareable between engines
        std::shared_ptr<conversions::conversion_registry> get_conversion_registry() const;
        
        // Set a shared conversion registry (for sharing between engine instances)
        void set_conversion_registry(std::shared_ptr<conversions::conversion_registry> registry);
        
        // Get the serialization registry for this engine
        serialization::serialization_registry& get_serialization_registry();
        
        // Get the class registry for this engine
        class_registry& get_class_registry();

        // === TYPE INFO INTERNING ===
        // Get or create type_info for basic types (fast O(1) access for common types)
        type_info* get_type_info_int();
        type_info* get_type_info_float();
        type_info* get_type_info_string();
        type_info* get_type_info_bool();
        type_info* get_type_info_char();
        type_info* get_type_info_void();
        type_info* get_type_info_invalid();

        // Get or create type_info for arrays/maps (interned by type_id)
        type_info* get_type_info_array(type_info* element_type);
        type_info* get_type_info_map(type_info* key_type, type_info* value_type);
        type_info* get_type_info_weak_ptr(type_info* pointee_type);
        type_info* get_type_info_shared_ptr(type_info* pointee_type);
        type_info* get_type_info_reference(type_info* referenced_type);
        type_info* get_type_info_function(type_info* return_type, const std::vector<type_info*>& arg_types);

        // Get or create type_info for object types (uses class_definition's type_info)
        type_info* get_type_info_object(const std::string& class_name);
        type_info* get_type_info_object(uint64_t type_id);

        // Generic helper to intern any type_info
        type_info* get_type_info(const type_info& temp);

        // Direct lookup by interned ID (fastest - O(1) with no string operations)
        type_info* get_type_info_by_id(uint64_t type_id);

        // Lookup by canonical name (interns the name first, then does ID lookup)
        type_info* get_type_info_by_name(const std::string& canonical_name);

        // Template method to get type_info for any C++ type
        template<typename T>
        type_info* get_type_info_for_cpp_type() {
            if (!get_symbolizer()) return nullptr;
            type_info temp = type_info::make<T>(*get_symbolizer());
            return get_type_info(temp);
        }

        // Reference support for function parameters
        script_value try_create_reference(size_t arg_index, const script_value& fallback);
        
        // Register polymorphic copier for derived types
        template<typename Derived>
        void register_polymorphic_copier(std::type_index derived_type, 
                                       std::type_index base_type,
                                       std::function<std::shared_ptr<void>(const void*)> copier) {
            register_polymorphic_copier_impl(derived_type, base_type, std::move(copier));
        }
        
        // Output stream redirection
        // Set a custom output stream for print() and related functions
        // Pass nullptr to reset to std::cout
        void set_output_stream(std::shared_ptr<std::ostream> stream);

        // Get the current output stream (defaults to std::cout)
        std::ostream& get_output_stream();

        // Include/Import path management
        void include_paths(const std::vector<std::string>& paths);
        void add_include_path(const std::string& path);
        void clear_include_paths();
        std::vector<std::string> get_include_paths() const;
        
        // Import behavior configuration
        enum class import_behavior {
            file_timestamp, // Re-import if file modified (DEFAULT)
            once,           // Import only once, ignore file changes
            always         // Treat import as include
        };
        
        void set_import_behavior(import_behavior behavior);
        import_behavior get_import_behavior() const;
        
        // Import management
        void reset_imports();
        void reset_import(const std::string& path);
        bool is_imported(const std::string& path) const;
        std::vector<std::string> get_imported_files() const;
        
        // Execute a file with import tracking
        script_value execute_import(const std::string& resolved_path);

        // Parameter storage for function calls (replaces thread_local)
        detail::parameter_storage* get_current_parameter_storage() const;
        void set_current_parameter_storage(detail::parameter_storage* storage);

        // Escaped value registry - tracks script_values that have left the interpreter
        // When engine dies, we null out their engine_ pointers to prevent dangling access
        void register_escaped(script_value* val);
        void unregister_escaped(script_value* val);

    private:
        struct implementation;
        std::unique_ptr<implementation> impl;
        
        void initialize_engine_reference();
        void add_class_impl(const std::string& name, std::shared_ptr<class_definition> classDef);
        void register_type_name_impl(const std::string& typeIdName, const std::string& friendlyName);
        void register_type_converter_impl(const std::type_info& type, std::function<script_value(const void*)> converter);
        void register_polymorphic_copier_impl(std::type_index derived_type, 
                                             std::type_index base_type,
                                             std::function<std::shared_ptr<void>(const void*)> copier);
        void register_class_by_type(std::type_index type, std::shared_ptr<class_definition> classDef);
        
        // Allow dynamic_binder to access implementation details
        template<typename T> friend class dynamic_binder;
        // Allow function_binder to access conversion registry
        template<typename T> friend struct detail::value_converter;
        // Allow interpreter to access implementation for include/import
        friend class interpreter;
        
        // Helper to wrap functions with type conversion
        template<typename ReturnType>
        script_function wrapFunctionForTypeConversion(script_function func) {
            // For user-defined types, type conversion is handled by function binder
            // We just return the function as-is since value_converter in function_binder
            // already handles the conversion properly
            return func;
        }
        
        // Helper to extract parameter types from a function signature
        template<typename Func>
        std::vector<script_value_type> extractParameterTypes() {
            using traits = detail::function_traits<std::decay_t<Func>>;
            return extractParameterTypesImpl<typename traits::argument_types>(std::make_index_sequence<traits::arity>{});
        }
        
        template<typename ArgsTuple, size_t... Is>
        std::vector<script_value_type> extractParameterTypesImpl(std::index_sequence<Is...>) {
            return {mapCppTypeToValueType<std::tuple_element_t<Is, ArgsTuple>>()...};
        }
        
        // Map C++ types to JaiScript ValueType
        template<typename T>
        static constexpr script_value_type mapCppTypeToValueType() {
            using decay_t = std::decay_t<T>;
            
            if constexpr (std::is_same_v<decay_t, int> || std::is_same_v<decay_t, int64_t> || 
                          std::is_same_v<decay_t, script_int>) {
                return script_value_type::jai_int_type;
            } else if constexpr (std::is_same_v<decay_t, float> || std::is_same_v<decay_t, double> || 
                                 std::is_same_v<decay_t, script_float>) {
                return script_value_type::jai_float_type;
            } else if constexpr (std::is_same_v<decay_t, bool> || std::is_same_v<decay_t, script_bool>) {
                return script_value_type::jai_bool_type;
            } else if constexpr (std::is_same_v<decay_t, char> || std::is_same_v<decay_t, script_char>) {
                return script_value_type::jai_char_type;
            } else if constexpr (std::is_same_v<decay_t, std::string> || std::is_same_v<decay_t, script_string>) {
                return script_value_type::jai_string_type;
            } else if constexpr (is_specialization_v<decay_t, std::vector>) {
                return script_value_type::jai_array_type;
            } else if constexpr (is_specialization_v<decay_t, bound_array>) {
                // bound_array is a zero-copy wrapper for arrays
                return script_value_type::jai_array_type;
            } else if constexpr (is_specialization_v<decay_t, std::map>) {
                return script_value_type::jai_map_type;
            } else if constexpr (is_specialization_v<decay_t, bound_map>) {
                // bound_map is a zero-copy wrapper for maps
                return script_value_type::jai_map_type;
            } else {
                // For unknown types, return Object (could be improved)
                return script_value_type::jai_object_type;
            }
        }

        // Extract parameter types with full type info (including C++ type_index for objects)
        template<typename Func>
        std::vector<param_type_info> extract_parameter_types_with_info() {
            using traits = detail::function_traits<std::decay_t<Func>>;
            return extract_parameter_types_with_info_impl<typename traits::argument_types>(std::make_index_sequence<traits::arity>{});
        }

        template<typename ArgsTuple, size_t... Is>
        std::vector<param_type_info> extract_parameter_types_with_info_impl(std::index_sequence<Is...>) {
            return {map_cpp_type_to_param_info<std::tuple_element_t<Is, ArgsTuple>>()...};
        }

        // Map C++ types to param_type_info (with type_index for object types)
        template<typename T>
        static param_type_info map_cpp_type_to_param_info() {
            using decay_t = std::decay_t<T>;

            if constexpr (std::is_same_v<decay_t, int> || std::is_same_v<decay_t, int64_t> ||
                          std::is_same_v<decay_t, script_int>) {
                return param_type_info(script_value_type::jai_int_type);
            } else if constexpr (std::is_same_v<decay_t, float> || std::is_same_v<decay_t, double> ||
                                 std::is_same_v<decay_t, script_float>) {
                return param_type_info(script_value_type::jai_float_type);
            } else if constexpr (std::is_same_v<decay_t, bool> || std::is_same_v<decay_t, script_bool>) {
                return param_type_info(script_value_type::jai_bool_type);
            } else if constexpr (std::is_same_v<decay_t, char> || std::is_same_v<decay_t, script_char>) {
                return param_type_info(script_value_type::jai_char_type);
            } else if constexpr (std::is_same_v<decay_t, std::string> || std::is_same_v<decay_t, script_string>) {
                return param_type_info(script_value_type::jai_string_type);
            } else if constexpr (is_specialization_v<decay_t, std::vector>) {
                return param_type_info(script_value_type::jai_array_type);
            } else if constexpr (is_specialization_v<decay_t, bound_array>) {
                return param_type_info(script_value_type::jai_array_type);
            } else if constexpr (is_specialization_v<decay_t, std::map>) {
                return param_type_info(script_value_type::jai_map_type);
            } else if constexpr (is_specialization_v<decay_t, bound_map>) {
                return param_type_info(script_value_type::jai_map_type);
            } else if constexpr (is_specialization_v<decay_t, std::shared_ptr>) {
                // For shared_ptr<T>, extract T and store its type_index
                using element_type = typename decay_t::element_type;
                return param_type_info(script_value_type::jai_shared_ptr_type, std::type_index(typeid(element_type)));
            } else {
                // For unknown/object types, store the C++ type_index for class lookup
                return param_type_info(script_value_type::jai_object_type, std::type_index(typeid(decay_t)));
            }
        }
    };

} // namespace jai

#include "engine_impl.hpp"
#include "conversion_registry_templates.hpp"
#include "value_impl.hpp"

#endif // __JAISCRIPT_CORE_ENGINE_HPP__