#pragma once

#include "types.hpp"
#include "value.hpp"
#include "function_binder.hpp"
#include <memory>
#include <optional>

namespace JaiScript {

    // Forward declarations
    class ClassDefinition;

    class Engine {
    public:
        Engine();
        ~Engine();
        
        // Non-copyable, moveable
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        Engine(Engine&&) noexcept;
        Engine& operator=(Engine&&) noexcept;
        
        // File-based execution
        template<typename ReturnType = void>
        std::optional<ReturnType> fileEval(
            const std::string& identifier,
            const std::string& scriptPath,
            const LocalVariables& localVars = {}
        );
        
        // String-based execution
        template<typename ReturnType = void>
        std::optional<ReturnType> eval(
            const std::string& identifier,
            const std::string& scriptContent,
            const LocalVariables& localVars = {}
        );
        
        // Simplified overloads for common case
        Value eval(const std::string& scriptContent);
        Value fileEval(const std::string& scriptPath);
        
        // Preferred method names
        Value execute(const std::string& scriptContent);
        Value executeFile(const std::string& scriptPath);
        
        // Global registration (non-serializable)
        void addGlobal(const std::string& name, Value value);
        
        // === FUNCTION REGISTRATION ===
        
        // Variadic functions - handle any number of arguments at runtime
        // Use this for functions like print(), max(), or functions with optional parameters
        // Example: engine.addVariadicFunction("print", [](const std::vector<Value>& args) { ... });
        void addVariadicFunction(const std::string& name, ScriptFunction func);
        void addFunctionWithArity(const std::string& name, ScriptFunction func, size_t arity);
        
        // Typed function registration with automatic overloading
        // Use this for functions with specific C++ signatures
        // Example: engine.addFunction("add", [](int a, int b) -> int { return a + b; });
        template<typename Func>
        void addFunction(const std::string& name, Func&& func) {
            using traits = detail::function_traits<std::decay_t<Func>>;
            constexpr size_t arity = traits::arity;
            
            auto binder = makeFunctionBinder(std::forward<Func>(func));
            ScriptFunction boundFunc = binder.bind();
            
            // Extract parameter types for type-based overloading
            std::vector<ValueType> paramTypes = extractParameterTypes<Func>();
            
            // Check if this should be registered as an overload
            if (hasFunction(name)) {
                // Automatically convert to overloaded function with type info
                addOverloadedFunctionWithTypes(name, arity, boundFunc, paramTypes);
            } else {
                // Register normally but store arity info for future overloading
                addFunctionWithArityAndTypes(name, boundFunc, arity, paramTypes);
            }
        }
        
        // Overloaded function registration
        void addOverloadedFunction(const std::string& name, size_t argCount, ScriptFunction func);
        void addOverloadedFunctionWithTypes(const std::string& name, size_t argCount, ScriptFunction func, const std::vector<ValueType>& paramTypes);
        void addFunctionWithArityAndTypes(const std::string& name, ScriptFunction func, size_t arity, const std::vector<ValueType>& paramTypes);
        
        // Class registration
        // Register a class with its type information
        template<typename T>
        void addClass(const std::string& name, std::shared_ptr<ClassDefinition> classDef) {
            addClassImpl(name, classDef);
            registerTypeNameImpl(typeid(T).name(), name);
        }
        
        // Type conversion registration
        // Register a conversion between JaiScript types with a cost for overload resolution
        // Lower costs are preferred (0 = exact match, 1 = promotion, 2 = standard conversion, etc.)
        void registerTypeConversion(ValueType from, ValueType to, int cost, 
                                   std::function<Value(const Value&)> converter);
        
        // Template version for C++ type safety using std::is_convertible
        template<typename From, typename To>
        void registerTypeConversion(int cost = -1) {
            static_assert(std::is_convertible_v<From, To>, 
                         "Types must be convertible according to C++ rules");
            
            ValueType fromType = mapCppTypeToValueType<From>();
            ValueType toType = mapCppTypeToValueType<To>();
            
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
            
            registerTypeConversion(fromType, toType, cost, 
                [](const Value& v) { 
                    return Value(static_cast<To>(v.as<From>())); 
                });
        }
        
        // Type registration (to be implemented)
        template<typename T>
        class TypeRegistrar {
        public:
            TypeRegistrar(Engine& engine, const std::string& name);
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
        Value getVariable(const std::string& name) const;
        bool hasVariable(const std::string& name) const;
        bool hasFunction(const std::string& name) const;
        
        // State management (hooks for external serialization)
        struct State {
            std::map<std::string, Value> globals;  // Use ordered map for deterministic serialization
            // More state to be added
        };
        
        State getState() const;
        void setState(const State& state);
        
        // Hot-reload support
        bool canHotReload(const std::string& scriptPath) const;
        bool hotReload(const std::string& scriptPath);
        
        // Get registered type name from typeid
        std::string getRegisteredTypeName(const std::string& typeIdName) const;
        
        // Convert a registered type to Value
        Value convertToValue(const std::string& typeIdName, const void* obj) const;
        
        // Register type converters for custom types
        template<typename T>
        void registerTypeConverter(const std::string& typeName) {
            // Register a converter that creates a Value from this type
            auto toValue = [typeName](const T& obj) -> Value {
                auto sharedObj = std::make_shared<T>(obj);
                return Value::makeObject(typeName, std::static_pointer_cast<void>(sharedObj));
            };
            
            // Store this converter (implementation will handle the storage)
            registerTypeConverterImpl(typeid(T).name(), 
                [toValue](const void* obj) -> Value {
                    return toValue(*static_cast<const T*>(obj));
                });
        }
        
    private:
        struct Implementation;
        std::unique_ptr<Implementation> impl;
        
        void addClassImpl(const std::string& name, std::shared_ptr<ClassDefinition> classDef);
        void registerTypeNameImpl(const std::string& typeIdName, const std::string& friendlyName);
        void registerTypeConverterImpl(const std::string& typeIdName, std::function<Value(const void*)> converter);
        
        // Helper to extract parameter types from a function signature
        template<typename Func>
        std::vector<ValueType> extractParameterTypes() {
            using traits = detail::function_traits<std::decay_t<Func>>;
            return extractParameterTypesImpl<typename traits::argument_types>(std::make_index_sequence<traits::arity>{});
        }
        
        template<typename ArgsTuple, size_t... Is>
        std::vector<ValueType> extractParameterTypesImpl(std::index_sequence<Is...>) {
            return {mapCppTypeToValueType<std::tuple_element_t<Is, ArgsTuple>>()...};
        }
        
        // Map C++ types to JaiScript ValueType
        template<typename T>
        static constexpr ValueType mapCppTypeToValueType() {
            using DecayT = std::decay_t<T>;
            
            if constexpr (std::is_same_v<DecayT, int> || std::is_same_v<DecayT, int64_t> || 
                          std::is_same_v<DecayT, Int>) {
                return ValueType::Int;
            } else if constexpr (std::is_same_v<DecayT, float> || std::is_same_v<DecayT, double> || 
                                 std::is_same_v<DecayT, Float>) {
                return ValueType::Float;
            } else if constexpr (std::is_same_v<DecayT, bool> || std::is_same_v<DecayT, Bool>) {
                return ValueType::Bool;
            } else if constexpr (std::is_same_v<DecayT, char> || std::is_same_v<DecayT, Char>) {
                return ValueType::Char;
            } else if constexpr (std::is_same_v<DecayT, std::string> || std::is_same_v<DecayT, String>) {
                return ValueType::String;
            } else {
                // For unknown types, return Object (could be improved)
                return ValueType::Object;
            }
        }
    };
    
    // Template implementation (would go in detail file or inline)
    template<typename ReturnType>
    std::optional<ReturnType> Engine::eval(
        const std::string& identifier,
        const std::string& scriptContent,
        const LocalVariables& localVars
    ) {
        Value result = eval(scriptContent);
        if constexpr (std::is_same_v<ReturnType, void>) {
            return std::nullopt;
        } else {
            try {
                return result.as<ReturnType>();
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    
} // namespace JaiScript