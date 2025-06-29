#include "../../include/jaiscript/core/engine.hpp"
#include "../../include/jaiscript/core/class_builder.hpp"
#include "../../include/jaiscript/core/value.hpp"
#include "../../include/jaiscript/core/execution_backend.hpp"
#include "../../include/jaiscript/detail/lexer.hpp"
#include "../../include/jaiscript/detail/parser.hpp"
#include "../../include/jaiscript/detail/interpreter.hpp"
#include "../../include/jaiscript/detail/interpreter_backend.hpp"
#include "../../include/jaiscript/jvm/vm_backend.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdlib>

namespace jai {

struct engine::implementation {
    // Type conversion registry
    struct TypeConversionRegistry {
        struct Conversion {
            script_value_type from;
            script_value_type to;
            int cost; // Conversion cost for overload resolution
            std::function<script_value(const script_value&)> converter;
        };
        
        std::vector<Conversion> conversions;
        
        void register_conversion(script_value_type from, script_value_type to, int cost, 
                               std::function<script_value(const script_value&)> converter) {
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
        
        bool can_convert(script_value_type from, script_value_type to) const {
            if (from == to) return true;
            
            return std::any_of(conversions.begin(), conversions.end(),
                [from, to](const Conversion& c) { 
                    return c.from == from && c.to == to; 
                });
        }
        
        int get_conversion_cost(script_value_type from, script_value_type to) const {
            if (from == to) return 0;
            
            auto it = std::find_if(conversions.begin(), conversions.end(),
                [from, to](const Conversion& c) { 
                    return c.from == from && c.to == to; 
                });
            
            return (it != conversions.end()) ? it->cost : 1000;
        }
        
        script_value convert(const script_value& value, script_value_type targetType) const {
            if (value.type() == targetType) return value;
            
            auto it = std::find_if(conversions.begin(), conversions.end(),
                [&](const Conversion& c) { 
                    return c.from == value.type() && c.to == targetType; 
                });
            
            if (it != conversions.end()) {
                return it->converter(value);
            }
            
            throw runtime_error("No conversion available from " + 
                             std::to_string(static_cast<int>(value.type())) + 
                             " to " + std::to_string(static_cast<int>(targetType)));
        }
    };
    
    // Global type conversion registry
    TypeConversionRegistry typeConversions;
    
    // Polymorphic type registry for deep copying
    struct polymorphic_type_registry {
        struct type_copier {
            std::type_index type_id;
            std::function<std::shared_ptr<void>(const void*)> copy_func;
        };
        
        // Maps base type -> all registered derived type copiers
        std::unordered_map<std::type_index, std::vector<type_copier>> base_to_derived_;
        
        // Register a derived type with its copy function
        void register_type(std::type_index derived_type, std::type_index base_type,
                          std::function<std::shared_ptr<void>(const void*)> copier) {
            type_copier tc{derived_type, copier};
            base_to_derived_[base_type].push_back(tc);
            // Also register under its own type for direct copying
            base_to_derived_[derived_type].push_back(tc);
        }
        
        // Copy a polymorphic object, maintaining its actual derived type
        std::shared_ptr<void> copy_polymorphic(const void* obj, std::type_index stored_type) const {
            // For polymorphic types, we need the actual runtime type
            // Since we registered the copier with the exact type, we can just use stored_type
            // The polymorphic behavior is handled by registering derived types with their base
            
            // Find copier for this type
            auto it = base_to_derived_.find(stored_type);
            if (it != base_to_derived_.end() && !it->second.empty()) {
                // Use the first copier (there should only be one per type)
                return it->second[0].copy_func(obj);
            }
            
            // No copier found
            return nullptr;
        }
        
        bool has_copier(std::type_index type) const {
            return base_to_derived_.find(type) != base_to_derived_.end();
        }
    };
    
    polymorphic_type_registry polymorphic_copiers;
    
    // Structure to hold overloaded functions with type information
    struct OverloadSet {
        struct Overload {
            size_t argCount;
            script_value function;
            std::vector<script_value_type> paramTypes; // Type signature for type-based matching
            std::function<bool(const std::vector<script_value>&)> typeMatcher; // Custom type matcher
            
            Overload(size_t count, const script_value& func, const std::vector<script_value_type>& types = {})
                : argCount(count), function(func), paramTypes(types) {}
        };
        
        std::vector<Overload> overloads;
        
        // Reference to the conversion registry
        const TypeConversionRegistry* conversions;
        
        OverloadSet() : conversions(nullptr) {}
        
        void setConversionRegistry(const TypeConversionRegistry* registry) {
            conversions = registry;
        }
        
        void addOverload(size_t argCount, const script_value& func, const std::vector<script_value_type>& paramTypes = {}) {
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
        
        script_value findBestMatch(const std::vector<script_value>& args) const {
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
                    int cost = conversions ? conversions->get_conversion_cost(args[i].type(), overload.paramTypes[i])
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
                return script_value(); // No viable candidates
            }
            
            auto best = std::min_element(viableCandidates.begin(), viableCandidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.totalCost < b.totalCost; });
            
            // Check for ambiguity (multiple candidates with same cost)
            int bestCost = best->totalCost;
            int numBest = std::count_if(viableCandidates.begin(), viableCandidates.end(),
                [bestCost](const Candidate& c) { return c.totalCost == bestCost; });
            
            if (numBest > 1 && bestCost < 100) { // Don't report ambiguity for untyped functions
                // Could throw ambiguity error here, but for now just pick first
                // throw runtime_error("Ambiguous function call");
            }
            
            return best->overload->function;
        }
    };
    
    // Track which globals should NOT be serialized (most are serializable by default)
    std::unordered_set<std::string> nonSerializableGlobals;
    
    // Overloaded functions (support multiple functions with same name)
    std::unordered_map<std::string, OverloadSet> overloadedFunctions;
    
    // Registered classes
    std::unordered_map<std::string, std::shared_ptr<class_definition>> classes;
    
    // Type index to class definition mapping for efficient lookups
    std::unordered_map<std::type_index, std::shared_ptr<class_definition>> classesByType;
    
    // Function arity info for proper overloading
    std::unordered_map<std::string, size_t> functionArities;
    
    // Shared string symbolizer for consistent variable name mapping
    string_symbolizer stringSymbolizer;
    
    // Type name registry for custom classes (maps typeid name to user-friendly name)
    std::unordered_map<std::string, std::string> typeNameRegistry;
    
    // Type converter registry (maps typeid name to converter function)
    std::unordered_map<std::string, std::function<script_value(const void*)>> typeConverters;
    
    // Template type registry for parsing (stores base template names like "Point", "MyMap")
    std::unordered_set<std::string> registeredTemplateTypes;
    
    // Global environment shared with interpreter
    std::shared_ptr<environment> globalEnvironment;
    
    execution_backend_ptr backend;
    backend_type current_backend_type = backend_type::interpreter; // Default to interpreter
    
    implementation();
    ~implementation();
    
    // Update interpreter when an overloaded function changes
    void updateOverloadedFunction(const std::string& name);
    
    // Helper to ensure overload set has conversion registry
    OverloadSet& getOrCreateOverloadSet(const std::string& name) {
        auto& overloadSet = overloadedFunctions[name];
        if (!overloadSet.conversions) {
            overloadSet.setConversionRegistry(&typeConversions);
        }
        return overloadSet;
    }
};

engine::implementation::implementation() {
    globalEnvironment = std::make_shared<environment>(&stringSymbolizer);
    
    const char* backend_env = std::getenv("JAISCRIPT_BACKEND");
    if (backend_env) {
        std::string backend_str(backend_env);
        if (backend_str == "jvm" || backend_str == "vm") {
            current_backend_type = backend_type::jvm;
            backend = jvm::create_vm_backend(&stringSymbolizer, globalEnvironment);
        } else if (backend_str == "auto") {
            current_backend_type = backend_type::auto_select;
            backend = std::make_unique<interpreter_backend>(&stringSymbolizer, globalEnvironment);
        } else {
            current_backend_type = backend_type::interpreter;
            backend = std::make_unique<interpreter_backend>(&stringSymbolizer, globalEnvironment);
        }
    } else {
        current_backend_type = backend_type::interpreter;
        backend = std::make_unique<interpreter_backend>(&stringSymbolizer, globalEnvironment);
    }
    
    backend->set_type_converters(&typeConverters);
    
    // Register standard C++ implicit conversions
    // Promotions (lossless) - cost 1
    typeConversions.register_conversion(script_value_type::jai_bool_type, script_value_type::jai_int_type, 1,
        [](const script_value& v) { return script_value(static_cast<script_int>(v.as_bool() ? 1 : 0)); });
    
    typeConversions.register_conversion(script_value_type::jai_char_type, script_value_type::jai_int_type, 1,
        [](const script_value& v) { return script_value(static_cast<script_int>(v.as_char())); });
    
    typeConversions.register_conversion(script_value_type::jai_int_type, script_value_type::jai_float_type, 1,
        [](const script_value& v) { return script_value(static_cast<script_float>(v.as_int())); });
    
    // Standard conversions (may lose precision) - cost 2
    typeConversions.register_conversion(script_value_type::jai_float_type, script_value_type::jai_int_type, 2,
        [](const script_value& v) { return script_value(static_cast<script_int>(v.as_float())); });
    
    typeConversions.register_conversion(script_value_type::jai_int_type, script_value_type::jai_char_type, 2,
        [](const script_value& v) { return script_value(static_cast<script_char>(v.as_int())); });
    
    typeConversions.register_conversion(script_value_type::jai_int_type, script_value_type::jai_bool_type, 2,
        [](const script_value& v) { return script_value(static_cast<script_bool>(v.as_int() != 0)); });
    
    // Other numeric conversions - cost 3
    typeConversions.register_conversion(script_value_type::jai_float_type, script_value_type::jai_bool_type, 3,
        [](const script_value& v) { return script_value(static_cast<script_bool>(v.as_float() != 0.0)); });
    
    typeConversions.register_conversion(script_value_type::jai_bool_type, script_value_type::jai_float_type, 3,
        [](const script_value& v) { return script_value(static_cast<script_float>(v.as_bool() ? 1.0 : 0.0)); });
    
    typeConversions.register_conversion(script_value_type::jai_char_type, script_value_type::jai_float_type, 3,
        [](const script_value& v) { return script_value(static_cast<script_float>(v.as_char())); });
    
    typeConversions.register_conversion(script_value_type::jai_float_type, script_value_type::jai_char_type, 3,
        [](const script_value& v) { return script_value(static_cast<script_char>(static_cast<script_int>(v.as_float()))); });
}

engine::implementation::~implementation() = default;

void engine::implementation::updateOverloadedFunction(const std::string& name) {
    // Create a dispatch function that selects the right overload
    // Make a copy of the name to ensure it survives the lambda lifetime
    std::string functionName = name;
    script_function dispatcher = [this, functionName](const std::vector<script_value>& args) -> script_value {
        auto it = overloadedFunctions.find(functionName);
        if (it == overloadedFunctions.end()) {
            throw runtime_error("Overloaded function '" + functionName + "' not found");
        }
        
        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            throw runtime_error("No matching overload found for function '" + functionName + "' with " + std::to_string(args.size()) + " arguments");
        }
        
        const script_function& func = bestMatch.as_function();
        return func(args);
    };
    
    // Update in global environment
    script_value dispatcherValue = script_value::make_function(dispatcher);
    globalEnvironment->define(name, dispatcherValue);
}

engine::engine() : impl(std::make_unique<implementation>()) {
    // Set up the subscript resolver for custom [] operators
    impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> script_value {
        auto it = impl->overloadedFunctions.find("[]");
        if (it == impl->overloadedFunctions.end()) {
            throw runtime_error("No custom subscript operator registered");
        }
        
        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            throw runtime_error("No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
        }
        
        const script_function& func = bestMatch.as_function();
        return func(args);
    });
    
    // Set up custom extractor for class_instance objects
    script_value::set_custom_extractor([this](const std::string& type_name, std::shared_ptr<void> obj) -> std::shared_ptr<void> {
        // Check if this is a class that was registered with class_builder
        // class_builder creates objects with type_name matching the class name
        auto classIt = impl->classes.find(type_name);
        if (classIt != impl->classes.end()) {
            // This is a registered class, obj should be a class_instance
            auto instance = std::static_pointer_cast<class_instance>(obj);
            
            // Get the C++ object from the special field
            script_value cppObjValue = instance->get_field("_cpp_object");
            if (!cppObjValue.is_null() && cppObjValue.type() == script_value_type::jai_object_type) {
                // Direct access to the object_holder to avoid recursive extraction
                auto objHolder = std::get<std::shared_ptr<script_value::object_holder>>(cppObjValue.storage_);
                return objHolder->data;
            }
        }
        return nullptr;
    });
    
    // Add built-in functions
    add_function("print", [](const std::vector<script_value>& args) {
        for (const auto& arg : args) {
            std::cout << arg.to_string();
        }
        std::cout << std::endl;
        return script_value();
    });
}

engine::~engine() = default;

engine::engine(engine&&) noexcept = default;
engine& engine::operator=(engine&&) noexcept = default;


script_value engine::execute(const std::string& scriptContent) {
    return execute(scriptContent, instance_variables{});
}

script_value engine::execute(const std::string& scriptContent, const instance_variables& instanceVars) {
    try {
        // Auto-select backend based on script length if in auto mode
        if (impl->current_backend_type == backend_type::auto_select) {
            const size_t SCRIPT_LENGTH_THRESHOLD = 1000; // Characters
            backend_type selected_type = (scriptContent.length() > SCRIPT_LENGTH_THRESHOLD) 
                                       ? backend_type::jvm 
                                       : backend_type::interpreter;
            
            // Switch backend if needed
            if (selected_type == backend_type::jvm && dynamic_cast<interpreter_backend*>(impl->backend.get())) {
                impl->backend = jvm::create_vm_backend(&impl->stringSymbolizer, impl->globalEnvironment);
                impl->backend->set_type_converters(&impl->typeConverters);
                impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                                          impl->overloadedFunctions.count("-") > 0 ||
                                                          impl->overloadedFunctions.count("*") > 0 ||
                                                          impl->overloadedFunctions.count("/") > 0);
                impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> script_value {
                    auto it = impl->overloadedFunctions.find("[]");
                    if (it == impl->overloadedFunctions.end()) {
                        throw runtime_error("No custom subscript operator registered");
                    }
                    
                    script_value bestMatch = it->second.findBestMatch(args);
                    if (bestMatch.is_null()) {
                        throw runtime_error("No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
                    }
                    
                    const script_function& func = bestMatch.as_function();
                    return func(args);
                });
            } else if (selected_type == backend_type::interpreter && !dynamic_cast<interpreter_backend*>(impl->backend.get())) {
                impl->backend = std::make_unique<interpreter_backend>(&impl->stringSymbolizer, impl->globalEnvironment);
                impl->backend->set_type_converters(&impl->typeConverters);
                impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                                          impl->overloadedFunctions.count("-") > 0 ||
                                                          impl->overloadedFunctions.count("*") > 0 ||
                                                          impl->overloadedFunctions.count("/") > 0);
                impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> script_value {
                    auto it = impl->overloadedFunctions.find("[]");
                    if (it == impl->overloadedFunctions.end()) {
                        throw runtime_error("No custom subscript operator registered");
                    }
                    
                    script_value bestMatch = it->second.findBestMatch(args);
                    if (bestMatch.is_null()) {
                        throw runtime_error("No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
                    }
                    
                    const script_function& func = bestMatch.as_function();
                    return func(args);
                });
            }
        }
        
        // Prepare backend for new execution
        impl->backend->prepare_for_execution();
        
        // Push scope for instance variables if any
        bool hasInstanceVars = !instanceVars.empty();
        if (hasInstanceVars) {
            impl->backend->push_scope();
            for (const auto& [name, value] : instanceVars) {
                impl->backend->define_variable(name, value);
            }
        }
        
        // Parse and execute
        lexer lexer(scriptContent, impl->registeredTemplateTypes);
        auto tokens = lexer.tokenize();
        parser parser(tokens, impl->registeredTemplateTypes);
        auto declarations = parser.parse();
        
        script_value result = impl->backend->execute(declarations);
        
        // Check for unhandled script exception
        if (impl->backend->is_unwinding()) {
            const auto& exception = impl->backend->get_current_exception();
            
            // Pop instance scope before throwing
            if (hasInstanceVars) {
                impl->backend->pop_scope();
            }
            
            throw exception;
        }
        
        // No need to sync globals - they're already in the shared environment!
        
        // Pop instance scope if we pushed one
        if (hasInstanceVars) {
            impl->backend->pop_scope();
        }
        
        return result;
        
    } catch (const script_exception& e) {
        // Script exceptions bubble up to C++
        impl->backend->prepare_for_execution();
        throw;
    } catch (const std::runtime_error& e) {
        // Wrap C++ exceptions as script exceptions for consistency
        impl->backend->prepare_for_execution();
        throw script_exception(std::string("C++ exception: ") + e.what());
    } catch (const std::exception& e) {
        // Wrap other C++ exceptions with a generic message
        impl->backend->prepare_for_execution();
        throw script_exception("Unbound exception type caught in JaiScript.");
    }
}

script_value engine::execute_file(const std::string& scriptPath) {
    return execute_file(scriptPath, instance_variables{});
}

script_value engine::execute_file(const std::string& scriptPath, const instance_variables& instanceVars) {
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        throw runtime_error("Failed to open script file: " + scriptPath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return execute(buffer.str(), instanceVars);
}

void engine::add_global(const std::string& name, script_value value, bool is_serializable) {
    impl->globalEnvironment->define(name, std::move(value));
    
    // Track if this global should NOT be serialized
    if (!is_serializable) {
        impl->nonSerializableGlobals.insert(name);
    } else {
        // In case it was previously marked as non-serializable
        impl->nonSerializableGlobals.erase(name);
    }
}

void engine::add_variadic_function(const std::string& name, script_function func) {
    // Variadic function registration - handles any number of arguments
    // Register with arity 0 to indicate it's a wildcard function
    if (has_function(name)) {
        // Add as overload with arity 0 (wildcard - accepts any number of args)
        add_overloaded_function(name, 0, func);
    } else {
        // Register with arity 0 to mark it as variadic
        add_functionWithArity(name, func, 0);
    }
}

void engine::add_functionWithArity(const std::string& name, script_function func, size_t arity) {
    // Check if we have an existing function with this name
    bool hasExistingFunction = false;
    script_value existing;
    try {
        existing = impl->globalEnvironment->get(name);
        hasExistingFunction = existing.is_function();
    } catch (...) {
        // Variable doesn't exist, which is fine
    }
    
    auto overloadIt = impl->overloadedFunctions.find(name);
    
    if (hasExistingFunction) {
        // Move existing function to overloaded set
        if (overloadIt == impl->overloadedFunctions.end()) {
            // Check if we have arity info for the existing function
            auto arityIt = impl->functionArities.find(name);
            size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
            
            impl->getOrCreateOverloadSet(name).setConversionRegistry(&impl->typeConversions);
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, existing);
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
        
        // Now add the new function as an overload
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func));
        impl->updateOverloadedFunction(name);
    } else if (overloadIt != impl->overloadedFunctions.end()) {
        // Already have overloaded functions, just add this one
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func));
        impl->updateOverloadedFunction(name);
    } else {
        // No existing function, just add normally
        // Store arity info for future use
        script_value funcValue = script_value::make_function(func);
        impl->globalEnvironment->define(name, funcValue);
        impl->functionArities[name] = arity;
    }
}

void engine::add_overloaded_function(const std::string& name, size_t argCount, script_function func) {
    // Check if we need to move an existing function from globalEnvironment
    bool hasExistingFunction = false;
    script_value existing;
    try {
        existing = impl->globalEnvironment->get(name);
        hasExistingFunction = existing.is_function();
    } catch (...) {
        // Variable doesn't exist, which is fine
    }
    
    if (hasExistingFunction) {
        // Move existing function to overloaded set first
        // Check if we have arity info for the existing function
        auto arityIt = impl->functionArities.find(name);
        size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
        
        // Check if this is already an overloaded function by seeing if it's in the overloadedFunctions map
        // This prevents trying to add a dispatcher function to the overload set, which would cause
        // a segfault due to recursive function copying issues
        auto overloadIt = impl->overloadedFunctions.find(name);
        if (overloadIt == impl->overloadedFunctions.end()) {
            // First time creating overload set for this function
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, existing);
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
    }
    
    // Now add the new overload
    impl->getOrCreateOverloadSet(name).addOverload(argCount, script_value::make_function(func));
    impl->updateOverloadedFunction(name);
}

void engine::add_overloaded_functionWithTypes(const std::string& name, size_t argCount, script_function func, const std::vector<script_value_type>& paramTypes) {
    // Check if we need to move an existing function from globalEnvironment
    bool hasExistingFunction = false;
    script_value existing;
    try {
        existing = impl->globalEnvironment->get(name);
        hasExistingFunction = existing.is_function();
    } catch (...) {
        // Variable doesn't exist, which is fine
    }
    
    if (hasExistingFunction) {
        // Move existing function to overloaded set first
        // Check if we have arity info for the existing function
        auto arityIt = impl->functionArities.find(name);
        size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
        
        impl->getOrCreateOverloadSet(name).addOverload(existingArity, existing);
        if (arityIt != impl->functionArities.end()) {
            impl->functionArities.erase(arityIt);
        }
    }
    
    // Now add the new overload with type information
    impl->getOrCreateOverloadSet(name).addOverload(argCount, script_value::make_function(func), paramTypes);
    impl->updateOverloadedFunction(name);
}

void engine::add_functionWithArityAndTypes(const std::string& name, script_function func, size_t arity, const std::vector<script_value_type>& paramTypes) {
    // Check if we have an existing function with this name
    bool hasExistingFunction = false;
    script_value existing;
    try {
        existing = impl->globalEnvironment->get(name);
        hasExistingFunction = existing.is_function();
    } catch (...) {
        // Variable doesn't exist, which is fine
    }
    
    auto overloadIt = impl->overloadedFunctions.find(name);
    
    if (hasExistingFunction) {
        // Move existing function to overloaded set
        if (overloadIt == impl->overloadedFunctions.end()) {
            // Check if we have arity info for the existing function
            auto arityIt = impl->functionArities.find(name);
            size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
            
            impl->getOrCreateOverloadSet(name).setConversionRegistry(&impl->typeConversions);
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, existing);
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
        
        // Now add the new function as an overload with type info
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func), paramTypes);
        impl->updateOverloadedFunction(name);
    } else if (overloadIt != impl->overloadedFunctions.end()) {
        // Already have overloaded functions, just add this one with type info
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func), paramTypes);
        impl->updateOverloadedFunction(name);
    } else {
        // No existing function, check if we have type info
        if (!paramTypes.empty()) {
            // Have type info, create overload set immediately
            impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func), paramTypes);
            impl->updateOverloadedFunction(name);
        } else {
            // No type info, add normally
            script_value funcValue = script_value::make_function(func);
            impl->globalEnvironment->define(name, funcValue);
            impl->functionArities[name] = arity;
        }
    }
}

void engine::add_class_impl(const std::string& name, std::shared_ptr<class_definition> classDef) {
    impl->classes[name] = classDef;
}

void engine::register_type_conversion(script_value_type from, script_value_type to, int cost, 
                                  std::function<script_value(const script_value&)> converter) {
    impl->typeConversions.register_conversion(from, to, cost, converter);
}

script_value engine::get_variable(const std::string& name) const {
    // Use backend to properly handle references
    try {
        return impl->backend->get_variable(name);
    } catch (...) {
        // Not in global environment, check overloaded functions
    }
    
    // Check overloaded functions
    auto overloadIt = impl->overloadedFunctions.find(name);
    if (overloadIt != impl->overloadedFunctions.end()) {
        // Create a dispatch function that selects the right overload
        // Make a copy of the name to ensure it survives the lambda lifetime
        std::string functionName = name;
        script_function dispatcher = [this, functionName](const std::vector<script_value>& args) -> script_value {
            auto it = impl->overloadedFunctions.find(functionName);
            if (it == impl->overloadedFunctions.end()) {
                throw runtime_error("Overloaded function '" + functionName + "' not found");
            }
            
            script_value bestMatch = it->second.findBestMatch(args);
            if (bestMatch.is_null()) {
                throw runtime_error("No matching overload found for function '" + functionName + "' with " + std::to_string(args.size()) + " arguments");
            }
            
            // Call the selected overload
            const script_function& func = bestMatch.as_function();
            return func(args);
        };
        
        return script_value::make_function(dispatcher);
    }
    
    throw runtime_error("Variable '" + name + "' not found");
}

bool engine::has_variable(const std::string& name) const {
    // Delegate to backend to ensure consistency
    return impl->backend->has_variable(name);
}

bool engine::has_function(const std::string& name) const {
    // Check if there's a function in global environment
    try {
        script_value val = impl->globalEnvironment->get(name);
        if (val.is_function()) {
            return true;
        }
    } catch (...) {
        // Not found
    }
    
    // Check overloaded functions
    return impl->overloadedFunctions.find(name) != impl->overloadedFunctions.end();
}

bool engine::is_type_name(const std::string& name) const {
    // Check if it's a registered class
    return impl->classes.find(name) != impl->classes.end();
}

engine::state engine::get_state() const {
    // Build ordered map of serializable globals for deterministic serialization
    std::map<std::string, script_value> orderedGlobals;
    
    // Get all variables from the global environment
    auto allVars = impl->globalEnvironment->get_all_variables();
    
    // Filter out non-serializable ones
    for (const auto& [name, value] : allVars) {
        if (impl->nonSerializableGlobals.find(name) == impl->nonSerializableGlobals.end()) {
            orderedGlobals[name] = value;
        }
    }
    
    return state{orderedGlobals};
}

void engine::set_state(const state& state) {
    // Get all current variables
    auto currentVars = impl->globalEnvironment->get_all_variables();
    
    // Create a new environment with only non-serializable globals
    auto newEnv = std::make_shared<environment>(&impl->stringSymbolizer);
    
    // Copy over non-serializable globals
    for (const auto& [name, value] : currentVars) {
        if (impl->nonSerializableGlobals.find(name) != impl->nonSerializableGlobals.end()) {
            newEnv->define(name, value);
        }
    }
    
    // Add the new state globals  
    for (const auto& [name, value] : state.globals) {
        newEnv->define(name, value);
    }
    
    // Replace the global environment
    impl->globalEnvironment = newEnv;
    
    // Update the backend to use the new environment
    impl->backend = std::make_unique<interpreter_backend>(&impl->stringSymbolizer, impl->globalEnvironment);
}

bool engine::can_hot_reload(const std::string& scriptPath) const {
    // TODO: Implement file timestamp checking
    return false;
}

bool engine::hot_reload(const std::string& scriptPath) {
    // TODO: Implement hot reload with state preservation
    return false;
}

void engine::register_type_name_impl(const std::string& typeIdName, const std::string& friendlyName) {
    impl->typeNameRegistry[typeIdName] = friendlyName;
}

std::string engine::get_registered_type_name(const std::string& typeIdName) const {
    auto it = impl->typeNameRegistry.find(typeIdName);
    if (it != impl->typeNameRegistry.end()) {
        return it->second;
    }
    // If not registered, try simple demangling for common cases
    std::string type_name = typeIdName;
    size_t pos = 0;
    while (pos < type_name.length() && std::isdigit(type_name[pos])) {
        pos++;
    }
    if (pos > 0 && pos < type_name.length()) {
        type_name = type_name.substr(pos);
    }
    return type_name;
}

void engine::register_type_converterImpl(const std::string& typeIdName, std::function<script_value(const void*)> converter) {
    impl->typeConverters[typeIdName] = converter;
}

script_value engine::convert_to_value(const std::string& typeIdName, const void* obj) const {
    auto it = impl->typeConverters.find(typeIdName);
    if (it != impl->typeConverters.end()) {
        return it->second(obj);
    }
    throw runtime_error("No converter registered for type: " + typeIdName);
}

void engine::set_has_custom_numeric_operators(bool value) {
    // Explicitly set whether custom numeric operators are in use
    // This allows users to opt-in to custom operator support when needed
    // By default, the fast path is enabled (no custom operators)
    impl->backend->set_has_custom_numeric_ops(value);
}

void engine::register_template_type(const std::string& baseTemplateName) {
    impl->registeredTemplateTypes.insert(baseTemplateName);
}

std::unordered_set<std::string> engine::get_registered_template_types() const {
    return impl->registeredTemplateTypes;
}

void engine::set_backend(backend_type type) {
    if (type == impl->current_backend_type) {
        return;
    }
    
    impl->current_backend_type = type;
    
    switch (type) {
        case backend_type::jvm:
            impl->backend = jvm::create_vm_backend(&impl->stringSymbolizer, impl->globalEnvironment);
            break;
        case backend_type::interpreter:
            impl->backend = std::make_unique<interpreter_backend>(&impl->stringSymbolizer, impl->globalEnvironment);
            break;
        case backend_type::auto_select:
            impl->backend = std::make_unique<interpreter_backend>(&impl->stringSymbolizer, impl->globalEnvironment);
            break;
    }
    
    impl->backend->set_type_converters(&impl->typeConverters);
    impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                              impl->overloadedFunctions.count("-") > 0 ||
                                              impl->overloadedFunctions.count("*") > 0 ||
                                              impl->overloadedFunctions.count("/") > 0);
    
    impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> script_value {
        auto it = impl->overloadedFunctions.find("[]");
        if (it == impl->overloadedFunctions.end()) {
            throw runtime_error("No custom subscript operator registered");
        }
        
        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            throw runtime_error("No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
        }
        
        const script_function& func = bestMatch.as_function();
        return func(args);
    });
}

void engine::set_backend(std::unique_ptr<execution_backend> backend) {
    impl->backend = std::move(backend);
    impl->current_backend_type = backend_type::auto_select;
    
    impl->backend->set_type_converters(&impl->typeConverters);
    impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                              impl->overloadedFunctions.count("-") > 0 ||
                                              impl->overloadedFunctions.count("*") > 0 ||
                                              impl->overloadedFunctions.count("/") > 0);
    
    impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> script_value {
        auto it = impl->overloadedFunctions.find("[]");
        if (it == impl->overloadedFunctions.end()) {
            throw runtime_error("No custom subscript operator registered");
        }
        
        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            throw runtime_error("No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
        }
        
        const script_function& func = bestMatch.as_function();
        return func(args);
    });
}

backend_type engine::get_backend_type() const {
    return impl->current_backend_type;
}

std::string engine::get_backend_name() const {
    return impl->backend->get_backend_name();
}

void engine::setHasCustomNumericOps(bool value) {
    // Set the flag on the backend (which might be interpreter or VM)
    impl->backend->set_has_custom_numeric_ops(value);
}

std::shared_ptr<class_definition> engine::get_class_definition(const std::string& type_name) const {
    auto it = impl->classes.find(type_name);
    if (it != impl->classes.end()) {
        return it->second;
    }
    return nullptr;
}

void engine::register_polymorphic_copier_impl(std::type_index derived_type, 
                                             std::type_index base_type,
                                             std::function<std::shared_ptr<void>(const void*)> copier) {
    impl->polymorphic_copiers.register_type(derived_type, base_type, std::move(copier));
}

void engine::register_class_by_type(std::type_index type, std::shared_ptr<class_definition> classDef) {
    impl->classesByType[type] = classDef;
}

std::shared_ptr<class_definition> engine::get_class_definition_by_type(std::type_index type) const {
    auto it = impl->classesByType.find(type);
    if (it != impl->classesByType.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace jai