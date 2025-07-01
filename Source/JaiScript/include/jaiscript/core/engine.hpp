#pragma once

#include "types.hpp"
#include "value.hpp"
#include "function_binder.hpp"
#include <memory>
#include <optional>
#include <unordered_set>
#include <typeindex>

namespace jai {

    // Forward declarations
    class class_definition;
    class execution_backend;
    
    // Backend type enumeration
    enum class backend_type {
        interpreter,    // Default tree-walk interpreter
        jvm,           // Bytecode virtual machine
        auto_select    // Automatic selection based on heuristics
    };

    class engine {
    public:
        engine();
        ~engine();
        
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
            using traits = detail::function_traits<std::decay_t<Func>>;
            using return_type = typename traits::return_type;
            constexpr size_t arity = traits::arity;
            
            auto binder = make_functionBinder(std::forward<Func>(func));
            script_function boundFunc = binder.bind();
            
            // Wrap the function to handle type conversion for return values
            script_function wrappedFunc = wrapFunctionForTypeConversion<return_type>(std::move(boundFunc));
            
            // Extract parameter types for type-based overloading
            std::vector<script_value_type> paramTypes = extractParameterTypes<Func>();
            
            // Auto-detect numeric operator overrides and set flag
            if (arity == 2 && isNumericOperator(name)) {
                if (hasNumericOperands(paramTypes)) {
                    setHasCustomNumericOps(true);
                }
            }
            
            // Check if this should be registered as an overload
            if (has_function(name)) {
                // Automatically convert to overloaded function with type info
                add_overloaded_functionWithTypes(name, arity, wrappedFunc, paramTypes);
            } else {
                // Register normally but store arity info for future overloading
                add_functionWithArityAndTypes(name, wrappedFunc, arity, paramTypes);
            }
        }
        
    private:
        // Helper methods for numeric operator detection
        bool isNumericOperator(const std::string& name) const {
            return name == "+" || name == "-" || name == "*" || name == "/" || name == "%" ||
                   name == "<" || name == ">" || name == "<=" || name == ">=" || 
                   name == "==" || name == "!=" || name == "<=>";
        }
        
        bool hasNumericOperands(const std::vector<script_value_type>& paramTypes) const {
            if (paramTypes.size() != 2) return false;
            
            auto isNumericType = [](script_value_type t) {
                return t == script_value_type::jai_int_type || 
                       t == script_value_type::jai_float_type || 
                       t == script_value_type::jai_char_type || 
                       t == script_value_type::jai_bool_type;
            };
            
            return isNumericType(paramTypes[0]) && isNumericType(paramTypes[1]);
        }
        
        void setHasCustomNumericOps(bool value);
        
    public:
        // Overloaded function registration
        void add_overloaded_function(const std::string& name, size_t argCount, script_function func);
        void add_overloaded_functionWithTypes(const std::string& name, size_t argCount, script_function func, const std::vector<script_value_type>& paramTypes);
        void add_functionWithArityAndTypes(const std::string& name, script_function func, size_t arity, const std::vector<script_value_type>& paramTypes);
        
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
        
        // Type registration (to be implemented)
        template<typename T>
        class TypeRegistrar {
        public:
            TypeRegistrar(engine& engine, const std::string& name);
            TypeRegistrar& constructor();
            template<typename... Args>
            TypeRegistrar& constructor();
            template<typename Method>
            TypeRegistrar& method(const std::string& name, Method m);
            template<typename Property>
            TypeRegistrar& property(const std::string& name, Property p);
        };
        
        template<typename T>
        TypeRegistrar<T> registerType(const std::string& name);
        
        // Variable access
        script_value get_variable(const std::string& name) const;
        bool has_variable(const std::string& name) const;
        bool has_function(const std::string& name) const;
        bool is_type_name(const std::string& name) const;
        
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
        script_value convert_to_value(const std::string& typeIdName, const void* obj) const;
        
        // Backend configuration
        void set_backend(backend_type type);
        void set_backend(std::unique_ptr<execution_backend> backend);
        backend_type get_backend_type() const;
        std::string get_backend_name() const;
        
        // Register type converters for custom types
        template<typename T>
        void register_type_converter(const std::string& type_name) {
            // Register a converter that creates a script_value from this type
            auto toValue = [type_name](const T& obj) -> script_value {
                auto sharedObj = std::make_shared<T>(obj);
                return script_value::make_cpp_object(type_name, std::static_pointer_cast<void>(sharedObj));
            };
            
            // Store this converter (implementation will handle the storage)
            register_type_converterImpl(typeid(T).name(), 
                [toValue](const void* obj) -> script_value {
                    return toValue(*static_cast<const T*>(obj));
                });
        }
        
        // Get class definition by type name
        std::shared_ptr<class_definition> get_class_definition_by_type(const std::type_index& type) const;
        
        // Get class definition by type index
        std::shared_ptr<class_definition> get_class_definition_by_type(std::type_index type) const;
        
        // Register polymorphic copier for derived types
        template<typename Derived>
        void register_polymorphic_copier(std::type_index derived_type, 
                                       std::type_index base_type,
                                       std::function<std::shared_ptr<void>(const void*)> copier) {
            register_polymorphic_copier_impl(derived_type, base_type, std::move(copier));
        }
        
    private:
        struct implementation;
        std::unique_ptr<implementation> impl;
        
        void add_class_impl(const std::string& name, std::shared_ptr<class_definition> classDef);
        void register_type_name_impl(const std::string& typeIdName, const std::string& friendlyName);
        void register_type_converterImpl(const std::string& typeIdName, std::function<script_value(const void*)> converter);
        void register_polymorphic_copier_impl(std::type_index derived_type, 
                                             std::type_index base_type,
                                             std::function<std::shared_ptr<void>(const void*)> copier);
        void register_class_by_type(std::type_index type, std::shared_ptr<class_definition> classDef);
        
        // Allow class_builder to access implementation details
        template<typename T> friend class class_builder;
        
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
            } else {
                // For unknown types, return Object (could be improved)
                return script_value_type::jai_object_type;
            }
        }
    };
    
    
} // namespace jai