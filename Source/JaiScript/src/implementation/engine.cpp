#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/core/class_builder.hpp"
#include "../../include/jaiscript/core/value.hpp"
#include "../../include/jaiscript/detail/lexer.hpp"
#include "../../include/jaiscript/detail/parser.hpp"
#include "../../include/jaiscript/detail/interpreter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>

namespace JaiScript {

struct Engine::Implementation {
    // Type conversion registry
    struct TypeConversionRegistry {
        struct Conversion {
            ValueType from;
            ValueType to;
            int cost; // Conversion cost for overload resolution
            std::function<Value(const Value&)> converter;
        };
        
        std::vector<Conversion> conversions;
        
        void registerConversion(ValueType from, ValueType to, int cost, 
                               std::function<Value(const Value&)> converter) {
            // Remove existing conversion if any
            auto it = std::find_if(conversions.begin(), conversions.end(),
                [from, to](const Conversion& c) { 
                    return c.from == from && c.to == to; 
                });
            
            if (it != conversions.end()) {
                *it = {from, to, cost, converter};
            } else {
                conversions.push_back({from, to, cost, converter});
            }
        }
        
        bool canConvert(ValueType from, ValueType to) const {
            if (from == to) return true;
            
            return std::any_of(conversions.begin(), conversions.end(),
                [from, to](const Conversion& c) { 
                    return c.from == from && c.to == to; 
                });
        }
        
        int getConversionCost(ValueType from, ValueType to) const {
            if (from == to) return 0;
            
            auto it = std::find_if(conversions.begin(), conversions.end(),
                [from, to](const Conversion& c) { 
                    return c.from == from && c.to == to; 
                });
            
            return (it != conversions.end()) ? it->cost : 1000;
        }
        
        Value convert(const Value& value, ValueType targetType) const {
            if (value.type() == targetType) return value;
            
            auto it = std::find_if(conversions.begin(), conversions.end(),
                [&](const Conversion& c) { 
                    return c.from == value.type() && c.to == targetType; 
                });
            
            if (it != conversions.end()) {
                return it->converter(value);
            }
            
            throw RuntimeError("No conversion available from " + 
                             std::to_string(static_cast<int>(value.type())) + 
                             " to " + std::to_string(static_cast<int>(targetType)));
        }
    };
    
    // Global type conversion registry
    TypeConversionRegistry typeConversions;
    
    // Structure to hold overloaded functions with type information
    struct OverloadSet {
        struct Overload {
            size_t argCount;
            Value function;
            std::vector<ValueType> paramTypes; // Type signature for type-based matching
            std::function<bool(const std::vector<Value>&)> typeMatcher; // Custom type matcher
            
            Overload(size_t count, const Value& func, const std::vector<ValueType>& types = {})
                : argCount(count), function(func), paramTypes(types) {}
        };
        
        std::vector<Overload> overloads;
        
        // Reference to the conversion registry
        const TypeConversionRegistry* conversions;
        
        OverloadSet() : conversions(nullptr) {}
        
        void setConversionRegistry(const TypeConversionRegistry* registry) {
            conversions = registry;
        }
        
        void addOverload(size_t argCount, const Value& func, const std::vector<ValueType>& paramTypes = {}) {
            // If we have type info, check for exact type match first
            if (!paramTypes.empty()) {
                auto it = std::find_if(overloads.begin(), overloads.end(),
                    [&](const Overload& o) { 
                        return o.argCount == argCount && o.paramTypes == paramTypes; 
                    });
                if (it != overloads.end()) {
                    it->function = func;
                    return;
                }
            } else {
                // No type info - remove any existing overload with same arg count and no type info
                auto it = std::find_if(overloads.begin(), overloads.end(),
                    [argCount](const Overload& o) { 
                        return o.argCount == argCount && o.paramTypes.empty(); 
                    });
                if (it != overloads.end()) {
                    it->function = func;
                    return;
                }
            }
            
            overloads.emplace_back(argCount, func, paramTypes);
        }
        
        Value findBestMatch(const std::vector<Value>& args) const {
            size_t argCount = args.size();
            
            // Use C++-style overload resolution: find all viable candidates, then pick the best
            struct Candidate {
                const Overload* overload;
                int totalCost;
            };
            std::vector<Candidate> viableCandidates;
            
            // Phase 1: Find all viable candidates
            for (const auto& overload : overloads) {
                if (overload.argCount != argCount && overload.argCount != 0) {
                    continue; // Wrong number of arguments
                }
                
                if (overload.argCount == 0) {
                    // Wildcard function - always viable but with high cost
                    viableCandidates.push_back({&overload, 999});
                    continue;
                }
                
                if (overload.paramTypes.empty()) {
                    // No type info - viable with medium cost
                    viableCandidates.push_back({&overload, 100});
                    continue;
                }
                
                // Calculate conversion cost for typed overload
                int totalCost = 0;
                bool viable = true;
                for (size_t i = 0; i < argCount && viable; ++i) {
                    int cost = conversions ? conversions->getConversionCost(args[i].type(), overload.paramTypes[i])
                                          : (args[i].type() == overload.paramTypes[i] ? 0 : 1000);
                    if (cost >= 1000) {
                        viable = false; // No valid conversion
                    } else {
                        totalCost += cost;
                    }
                }
                
                if (viable) {
                    viableCandidates.push_back({&overload, totalCost});
                }
            }
            
            // Phase 2: Pick the best viable candidate (lowest cost)
            if (viableCandidates.empty()) {
                return Value(); // No viable candidates
            }
            
            auto best = std::min_element(viableCandidates.begin(), viableCandidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.totalCost < b.totalCost; });
            
            // Check for ambiguity (multiple candidates with same cost)
            int bestCost = best->totalCost;
            int numBest = std::count_if(viableCandidates.begin(), viableCandidates.end(),
                [bestCost](const Candidate& c) { return c.totalCost == bestCost; });
            
            if (numBest > 1 && bestCost < 100) { // Don't report ambiguity for untyped functions
                // Could throw ambiguity error here, but for now just pick first
                // throw RuntimeError("Ambiguous function call");
            }
            
            return best->overload->function;
        }
    };
    
    // C++ registered globals (persistent, never reset, injected into each interpreter)
    std::unordered_map<std::string, Value> cppGlobals;
    
    // Overloaded functions (support multiple functions with same name)
    std::unordered_map<std::string, OverloadSet> overloadedFunctions;
    
    // Script-declared variables (transferable between interpreter sessions)
    // Use unordered_map for performance - convert to ordered during serialization
    std::unordered_map<std::string, Value> scriptGlobals;
    
    // Registered classes
    std::unordered_map<std::string, std::shared_ptr<ClassDefinition>> classes;
    
    // Function arity info for proper overloading
    std::unordered_map<std::string, size_t> functionArities;
    
    // Shared string symbolizer for consistent variable name mapping
    StringSymbolizer stringSymbolizer;
    
    // Type name registry for custom classes (maps typeid name to user-friendly name)
    std::unordered_map<std::string, std::string> typeNameRegistry;
    
    // Type converter registry (maps typeid name to converter function)
    std::unordered_map<std::string, std::function<Value(const void*)>> typeConverters;
    
    std::unique_ptr<Interpreter> interpreter;
    
    Implementation();
    ~Implementation();
    
    // Helper to ensure overload set has conversion registry
    OverloadSet& getOrCreateOverloadSet(const std::string& name) {
        auto& overloadSet = overloadedFunctions[name];
        if (!overloadSet.conversions) {
            overloadSet.setConversionRegistry(&typeConversions);
        }
        return overloadSet;
    }
};

Engine::Implementation::Implementation() {
    interpreter = std::make_unique<Interpreter>();
    
    // Register standard C++ implicit conversions
    // Promotions (lossless) - cost 1
    typeConversions.registerConversion(ValueType::Bool, ValueType::Int, 1,
        [](const Value& v) { return Value(static_cast<Int>(v.asBool() ? 1 : 0)); });
    
    typeConversions.registerConversion(ValueType::Char, ValueType::Int, 1,
        [](const Value& v) { return Value(static_cast<Int>(v.asChar())); });
    
    typeConversions.registerConversion(ValueType::Int, ValueType::Float, 1,
        [](const Value& v) { return Value(static_cast<Float>(v.asInt())); });
    
    // Standard conversions (may lose precision) - cost 2
    typeConversions.registerConversion(ValueType::Float, ValueType::Int, 2,
        [](const Value& v) { return Value(static_cast<Int>(v.asFloat())); });
    
    typeConversions.registerConversion(ValueType::Int, ValueType::Char, 2,
        [](const Value& v) { return Value(static_cast<Char>(v.asInt())); });
    
    typeConversions.registerConversion(ValueType::Int, ValueType::Bool, 2,
        [](const Value& v) { return Value(static_cast<Bool>(v.asInt() != 0)); });
    
    // Other numeric conversions - cost 3
    typeConversions.registerConversion(ValueType::Float, ValueType::Bool, 3,
        [](const Value& v) { return Value(static_cast<Bool>(v.asFloat() != 0.0)); });
    
    typeConversions.registerConversion(ValueType::Bool, ValueType::Float, 3,
        [](const Value& v) { return Value(static_cast<Float>(v.asBool() ? 1.0 : 0.0)); });
    
    typeConversions.registerConversion(ValueType::Char, ValueType::Float, 3,
        [](const Value& v) { return Value(static_cast<Float>(v.asChar())); });
    
    typeConversions.registerConversion(ValueType::Float, ValueType::Char, 3,
        [](const Value& v) { return Value(static_cast<Char>(static_cast<Int>(v.asFloat()))); });
}

Engine::Implementation::~Implementation() = default;

Engine::Engine() : impl(std::make_unique<Implementation>()) {
    // Set up custom extractor for ClassInstance objects
    Value::setCustomExtractor([this](const std::string& typeName, std::shared_ptr<void> obj) -> std::shared_ptr<void> {
        // Check if this is a class that was registered with ClassBuilder
        // ClassBuilder creates objects with typeName matching the class name
        auto classIt = impl->classes.find(typeName);
        if (classIt != impl->classes.end()) {
            // This is a registered class, so obj should be a ClassInstance
            // We need to static_cast since we can't dynamic_cast from void*
            auto instance = std::static_pointer_cast<ClassInstance>(obj);
            
            // Get the C++ object from the special field
            Value cppObjValue = instance->getField("__cpp_object");
            if (!cppObjValue.isNull() && cppObjValue.type() == ValueType::Object) {
                try {
                    // Extract as void* shared_ptr
                    auto voidPtr = cppObjValue.as<std::shared_ptr<void>>();
                    return voidPtr;
                } catch (...) {
                    // Failed to extract
                }
            }
        }
        return nullptr;
    });
    
    // Add built-in functions
    addFunction("print", [](const std::vector<Value>& args) {
        for (const auto& arg : args) {
            std::cout << arg.toString();
        }
        std::cout << std::endl;
        return Value();
    });
}

Engine::~Engine() = default;

Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Value Engine::eval(const std::string& scriptContent) {
    try {
        // Create a new interpreter for this execution using shared string symbolizer
        Interpreter interpreter(&impl->stringSymbolizer);
        
        // Add both C++ globals and script globals to the interpreter
        std::unordered_map<std::string, Value> allGlobals = impl->cppGlobals;
        allGlobals.insert(impl->scriptGlobals.begin(), impl->scriptGlobals.end());
        
        // Add overloaded functions
        for (const auto& [name, overloadSet] : impl->overloadedFunctions) {
            // Create a dispatch function that selects the right overload
            // Capture by reference to the impl member, not the loop variable
            ScriptFunction dispatcher = [this, name](const std::vector<Value>& args) -> Value {
                auto it = impl->overloadedFunctions.find(name);
                if (it == impl->overloadedFunctions.end()) {
                    throw RuntimeError("Overloaded function '" + name + "' not found");
                }
                
                Value bestMatch = it->second.findBestMatch(args);
                if (bestMatch.isNull()) {
                    throw RuntimeError("No matching overload found for function '" + name + "' with " + std::to_string(args.size()) + " arguments");
                }
                
                // Call the selected overload
                const ScriptFunction& func = bestMatch.asFunction();
                return func(args);
            };
            
            allGlobals[name] = Value::makeFunction(dispatcher);
        }
        interpreter.addGlobals(allGlobals);
        
        // Tokenize the script
        Lexer lexer(scriptContent);
        auto tokens = lexer.tokenize();
        
        // Parse the tokens into an AST
        Parser parser(tokens);
        auto declarations = parser.parse();
        
        // Execute the AST
        Value result = interpreter.execute(declarations);
        
        // Update script globals from interpreter back to engine
        // Only script-declared variables should be transferred back
        auto variables = interpreter.getAllVariables();
        // Don't clear old script variables - we want to preserve them
        // Only update/add variables that were defined or modified
        for (const auto& [name, value] : variables) {
            // Only transfer variables that aren't C++ globals
            if (impl->cppGlobals.find(name) == impl->cppGlobals.end()) {
                impl->scriptGlobals[name] = value;
            }
        }
        
        return result;
        
    } catch (const std::exception& e) {
        throw RuntimeError("Execution failed: " + std::string(e.what()));
    }
}

Value Engine::fileEval(const std::string& scriptPath) {
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        throw RuntimeError("Failed to open script file: " + scriptPath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return eval(buffer.str());
}

Value Engine::execute(const std::string& scriptContent) {
    return eval(scriptContent);
}

Value Engine::executeFile(const std::string& scriptPath) {
    return fileEval(scriptPath);
}

void Engine::addGlobal(const std::string& name, Value value) {
    impl->cppGlobals[name] = std::move(value);
}

void Engine::addVariadicFunction(const std::string& name, ScriptFunction func) {
    // Variadic function registration - handles any number of arguments
    // Register with arity 0 to indicate it's a wildcard function
    if (hasFunction(name)) {
        // Add as overload with arity 0 (wildcard - accepts any number of args)
        addOverloadedFunction(name, 0, func);
    } else {
        // Register with arity 0 to mark it as variadic
        addFunctionWithArity(name, func, 0);
    }
}

void Engine::addFunctionWithArity(const std::string& name, ScriptFunction func, size_t arity) {
    auto existingIt = impl->cppGlobals.find(name);
    auto overloadIt = impl->overloadedFunctions.find(name);
    
    if (existingIt != impl->cppGlobals.end() && existingIt->second.isFunction()) {
        // Move existing function to overloaded set
        if (overloadIt == impl->overloadedFunctions.end()) {
            // Check if we have arity info for the existing function
            auto arityIt = impl->functionArities.find(name);
            size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
            
            impl->getOrCreateOverloadSet(name).setConversionRegistry(&impl->typeConversions);
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, existingIt->second);
            impl->cppGlobals.erase(existingIt);
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
        
        // Now add the new function as an overload
        impl->getOrCreateOverloadSet(name).addOverload(arity, Value::makeFunction(func));
    } else if (overloadIt != impl->overloadedFunctions.end()) {
        // Already have overloaded functions, just add this one
        impl->getOrCreateOverloadSet(name).addOverload(arity, Value::makeFunction(func));
    } else {
        // No existing function, just add normally
        // Store arity info for future use
        impl->cppGlobals[name] = Value::makeFunction(func);
        impl->functionArities[name] = arity;
    }
}

void Engine::addOverloadedFunction(const std::string& name, size_t argCount, ScriptFunction func) {
    // Check if we need to move an existing function from cppGlobals
    auto existingIt = impl->cppGlobals.find(name);
    if (existingIt != impl->cppGlobals.end() && existingIt->second.isFunction()) {
        // Move existing function to overloaded set first
        // Check if we have arity info for the existing function
        auto arityIt = impl->functionArities.find(name);
        size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
        
        impl->getOrCreateOverloadSet(name).addOverload(existingArity, existingIt->second);
        impl->cppGlobals.erase(existingIt);
        if (arityIt != impl->functionArities.end()) {
            impl->functionArities.erase(arityIt);
        }
    }
    
    // Now add the new overload
    impl->getOrCreateOverloadSet(name).addOverload(argCount, Value::makeFunction(func));
}

void Engine::addOverloadedFunctionWithTypes(const std::string& name, size_t argCount, ScriptFunction func, const std::vector<ValueType>& paramTypes) {
    // Check if we need to move an existing function from cppGlobals
    auto existingIt = impl->cppGlobals.find(name);
    if (existingIt != impl->cppGlobals.end() && existingIt->second.isFunction()) {
        // Move existing function to overloaded set first
        // Check if we have arity info for the existing function
        auto arityIt = impl->functionArities.find(name);
        size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
        
        impl->getOrCreateOverloadSet(name).addOverload(existingArity, existingIt->second);
        impl->cppGlobals.erase(existingIt);
        if (arityIt != impl->functionArities.end()) {
            impl->functionArities.erase(arityIt);
        }
    }
    
    // Now add the new overload with type information
    impl->getOrCreateOverloadSet(name).addOverload(argCount, Value::makeFunction(func), paramTypes);
}

void Engine::addFunctionWithArityAndTypes(const std::string& name, ScriptFunction func, size_t arity, const std::vector<ValueType>& paramTypes) {
    auto existingIt = impl->cppGlobals.find(name);
    auto overloadIt = impl->overloadedFunctions.find(name);
    
    if (existingIt != impl->cppGlobals.end() && existingIt->second.isFunction()) {
        // Move existing function to overloaded set
        if (overloadIt == impl->overloadedFunctions.end()) {
            // Check if we have arity info for the existing function
            auto arityIt = impl->functionArities.find(name);
            size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
            
            impl->getOrCreateOverloadSet(name).setConversionRegistry(&impl->typeConversions);
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, existingIt->second);
            impl->cppGlobals.erase(existingIt);
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
        
        // Now add the new function as an overload with type info
        impl->getOrCreateOverloadSet(name).addOverload(arity, Value::makeFunction(func), paramTypes);
    } else if (overloadIt != impl->overloadedFunctions.end()) {
        // Already have overloaded functions, just add this one with type info
        impl->getOrCreateOverloadSet(name).addOverload(arity, Value::makeFunction(func), paramTypes);
    } else {
        // No existing function, check if we have type info
        if (!paramTypes.empty()) {
            // Have type info, create overload set immediately
            impl->getOrCreateOverloadSet(name).addOverload(arity, Value::makeFunction(func), paramTypes);
        } else {
            // No type info, add normally
            impl->cppGlobals[name] = Value::makeFunction(func);
            impl->functionArities[name] = arity;
        }
    }
}

void Engine::addClassImpl(const std::string& name, std::shared_ptr<ClassDefinition> classDef) {
    impl->classes[name] = classDef;
}

void Engine::registerTypeConversion(ValueType from, ValueType to, int cost, 
                                  std::function<Value(const Value&)> converter) {
    impl->typeConversions.registerConversion(from, to, cost, converter);
}

Value Engine::getVariable(const std::string& name) const {
    // Check script globals first
    auto scriptIt = impl->scriptGlobals.find(name);
    if (scriptIt != impl->scriptGlobals.end()) {
        return scriptIt->second;
    }
    
    // Then check C++ globals
    auto cppIt = impl->cppGlobals.find(name);
    if (cppIt != impl->cppGlobals.end()) {
        return cppIt->second;
    }
    
    // Check overloaded functions
    auto overloadIt = impl->overloadedFunctions.find(name);
    if (overloadIt != impl->overloadedFunctions.end()) {
        // Create a dispatch function that selects the right overload
        ScriptFunction dispatcher = [this, name](const std::vector<Value>& args) -> Value {
            auto it = impl->overloadedFunctions.find(name);
            if (it == impl->overloadedFunctions.end()) {
                throw RuntimeError("Overloaded function '" + name + "' not found");
            }
            
            Value bestMatch = it->second.findBestMatch(args);
            if (bestMatch.isNull()) {
                throw RuntimeError("No matching overload found for function '" + name + "' with " + std::to_string(args.size()) + " arguments");
            }
            
            // Call the selected overload
            const ScriptFunction& func = bestMatch.asFunction();
            return func(args);
        };
        
        return Value::makeFunction(dispatcher);
    }
    
    throw RuntimeError("Variable '" + name + "' not found");
}

bool Engine::hasVariable(const std::string& name) const {
    return impl->scriptGlobals.find(name) != impl->scriptGlobals.end() ||
           impl->cppGlobals.find(name) != impl->cppGlobals.end() ||
           impl->overloadedFunctions.find(name) != impl->overloadedFunctions.end();
}

bool Engine::hasFunction(const std::string& name) const {
    // Check if there's a function in cppGlobals
    auto cppIt = impl->cppGlobals.find(name);
    if (cppIt != impl->cppGlobals.end() && cppIt->second.isFunction()) {
        return true;
    }
    
    // Check overloaded functions
    return impl->overloadedFunctions.find(name) != impl->overloadedFunctions.end();
}

Engine::State Engine::getState() const {
    // Convert to ordered map for deterministic serialization
    std::map<std::string, Value> orderedGlobals(impl->scriptGlobals.begin(), impl->scriptGlobals.end());
    return State{orderedGlobals}; // Only return script globals for serialization
}

void Engine::setState(const State& state) {
    // Convert from ordered map back to unordered_map for performance
    impl->scriptGlobals.clear();
    impl->scriptGlobals.insert(state.globals.begin(), state.globals.end());
}

bool Engine::canHotReload(const std::string& scriptPath) const {
    // TODO: Implement file timestamp checking
    return false;
}

bool Engine::hotReload(const std::string& scriptPath) {
    // TODO: Implement hot reload with state preservation
    return false;
}

void Engine::registerTypeNameImpl(const std::string& typeIdName, const std::string& friendlyName) {
    impl->typeNameRegistry[typeIdName] = friendlyName;
}

std::string Engine::getRegisteredTypeName(const std::string& typeIdName) const {
    auto it = impl->typeNameRegistry.find(typeIdName);
    if (it != impl->typeNameRegistry.end()) {
        return it->second;
    }
    // If not registered, try simple demangling for common cases
    std::string typeName = typeIdName;
    size_t pos = 0;
    while (pos < typeName.length() && std::isdigit(typeName[pos])) {
        pos++;
    }
    if (pos > 0 && pos < typeName.length()) {
        typeName = typeName.substr(pos);
    }
    return typeName;
}

void Engine::registerTypeConverterImpl(const std::string& typeIdName, std::function<Value(const void*)> converter) {
    impl->typeConverters[typeIdName] = converter;
}

Value Engine::convertToValue(const std::string& typeIdName, const void* obj) const {
    auto it = impl->typeConverters.find(typeIdName);
    if (it != impl->typeConverters.end()) {
        return it->second(obj);
    }
    throw RuntimeError("No converter registered for type: " + typeIdName);
}

} // namespace JaiScript