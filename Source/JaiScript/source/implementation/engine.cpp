#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/runtime_errors.hpp>
// JVM backend is already included in jaiscript.hpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <filesystem>

namespace jai {

// Forward declaration for VM backend stub
std::unique_ptr<execution_backend> create_vm_backend();

namespace jvm {
    // Temporary stub until VM is refactored
    inline std::unique_ptr<execution_backend> create_vm_backend(string_symbolizer*, std::shared_ptr<environment>) {
        return ::jai::create_vm_backend();
    }
}

struct engine::implementation {
    // Unified conversion registry (replaces both type_conversions and custom_conversions)
    std::shared_ptr<conversions::conversion_registry> conversions;

    // Current parameter storage for function calls (replaces thread_local)
    detail::parameter_storage* current_parameter_storage = nullptr;

    // Serialization registry for class metadata (non-static to ensure test isolation)
    serialization::serialization_registry serialization_registry;
    
    // Class registry for both C++ and script classes (replaces global singleton)
    class_registry class_registry_;
    
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
        std::shared_ptr<const conversions::conversion_registry> conversions;
        
        OverloadSet() : conversions() {}
        
        void setConversionRegistry(std::shared_ptr<const conversions::conversion_registry> registry) {
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
                    int cost = conversions ? conversions->get_builtin_conversion_cost(args[i].type(), overload.paramTypes[i])
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
                throw runtime_error("No viable candidates for function call");
            }
            
            auto best = std::min_element(viableCandidates.begin(), viableCandidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.totalCost < b.totalCost; });
            
            // Check for ambiguity (multiple candidates with same cost)
            int bestCost = best->totalCost;
            auto numBest = std::count_if(viableCandidates.begin(), viableCandidates.end(),
                [bestCost](const Candidate& c) { return c.totalCost == bestCost; });
            
            if (numBest > 1 && bestCost < 100) { // Don't report ambiguity for untyped functions
                // Could throw ambiguity error here, but for now just pick first
                // throw runtime_error("Ambiguous function call");
            }
            
            return best->overload->function;
        }
    };
    
    // Track which globals should NOT be serialized (most are serializable by default)
    // Uses string_view pointing into symbolizer storage for efficiency
    std::unordered_set<std::string_view, sv_hash, sv_equal> nonSerializableGlobals;
    
    // Overloaded functions (support multiple functions with same name)
    std::unordered_map<std::string, OverloadSet> overloadedFunctions;
    
    // Registered classes
    std::unordered_map<std::string, std::shared_ptr<class_definition>> classes;

    // Type ID to class definition mapping for fast lookups (type_id is interned string)
    std::unordered_map<uint64_t, std::shared_ptr<class_definition>> classesByTypeId;

    // Type index to class definition mapping for efficient lookups
    std::unordered_map<std::type_index, std::shared_ptr<class_definition>> classesByType;
    
    // Function arity info for proper overloading
    std::unordered_map<std::string, size_t> functionArities;
    
    // Shared string symbolizer for consistent variable name mapping
    string_symbolizer string_symbolizer_;

    // Cached symbol IDs for common type names (initialized in constructor)
    uint64_t class_definition_type_id_;

    // Cached symbol IDs for operator overloading (initialized in initialize_engine_reference)
    uint64_t op_plus_id_ = 0;
    uint64_t op_minus_id_ = 0;
    uint64_t op_star_id_ = 0;
    uint64_t op_slash_id_ = 0;
    uint64_t op_percent_id_ = 0;
    uint64_t op_less_id_ = 0;
    uint64_t op_less_equal_id_ = 0;
    uint64_t op_greater_id_ = 0;
    uint64_t op_greater_equal_id_ = 0;
    uint64_t op_equal_equal_id_ = 0;
    uint64_t op_bang_equal_id_ = 0;
    uint64_t op_spaceship_id_ = 0;
    uint64_t op_ampersand_id_ = 0;
    uint64_t op_pipe_id_ = 0;
    uint64_t op_caret_id_ = 0;
    uint64_t op_left_shift_id_ = 0;
    uint64_t op_right_shift_id_ = 0;

    // Type name registry for custom classes (maps typeid name to user-friendly name)
    std::unordered_map<std::string, std::string> typeNameRegistry;

    // Composite key for type interning - avoids string construction on lookup
    struct type_key {
        script_value_type base_type;
        uint64_t param1_id = 0;  // For array<T>, map<K,_>, weak_ptr<T>, reference<T>, etc.
        uint64_t param2_id = 0;  // For map<_,V>

        bool operator==(const type_key& other) const {
            return base_type == other.base_type &&
                   param1_id == other.param1_id &&
                   param2_id == other.param2_id;
        }
    };

    struct type_key_hash {
        size_t operator()(const type_key& k) const {
            size_t h = std::hash<int>{}(static_cast<int>(k.base_type));
            h ^= k.param1_id + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= k.param2_id + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // Type info storage - engine owns all type_info objects (raw pointers are safe because map doesn't invalidate on insert)
    std::unordered_map<type_key, type_info, type_key_hash> type_infos_;

    // Secondary index for O(1) lookup by type ID (stores pointers into type_infos_)
    std::unordered_map<uint64_t, type_info*> type_id_index_;

    // Commonly used type_info pointers for fast access
    type_info* type_info_int_ = nullptr;
    type_info* type_info_float_ = nullptr;
    type_info* type_info_string_ = nullptr;
    type_info* type_info_bool_ = nullptr;
    type_info* type_info_char_ = nullptr;
    type_info* type_info_void_ = nullptr;
    type_info* type_info_invalid_ = nullptr;

    
    // Template type registry for parsing (stores base template names like "Point", "MyMap")
    std::unordered_set<std::string> registeredTemplateTypes;
    
    // Global environment shared with interpreter
    std::shared_ptr<environment> global_environment_;
    
    execution_backend_ptr backend;
    backend_type current_backend_type = backend_type::interpreter; // Default to interpreter

    // Include/Import support
    std::vector<std::string> include_paths;
    engine::import_behavior import_behavior = engine::import_behavior::file_timestamp; // Default
    
    struct import_record {
        std::string resolved_path;
        std::filesystem::file_time_type last_modified;
    };
    std::unordered_map<std::string, import_record> import_cache;
    
    implementation();
    ~implementation();
    
    // Update interpreter when an overloaded function changes
    void updateOverloadedFunction(const std::string& name, engine* engine_ptr);
    
    // Helper to ensure overload set has conversion registry
    OverloadSet& getOrCreateOverloadSet(const std::string& name) {
        auto& overloadSet = overloadedFunctions[name];
        if (!overloadSet.conversions) {
            overloadSet.setConversionRegistry(conversions);
        }
        return overloadSet;
    }
};

engine::implementation::implementation()
    : conversions(std::make_shared<conversions::conversion_registry>()),
      class_definition_type_id_(string_symbolizer_.intern("class_definition")) {
    global_environment_ = std::make_shared<environment>(&string_symbolizer_);

    // Use unique_ptr with custom deleter for automatic cleanup
    char* backend_env_raw = nullptr;
    size_t len = 0;
    _dupenv_s(&backend_env_raw, &len, "JAISCRIPT_BACKEND");
    std::unique_ptr<char, decltype(&free)> backend_env(backend_env_raw, &free);

    if (backend_env) {
        std::string backend_str(backend_env.get());
        if (backend_str == "jvm" || backend_str == "vm") {
            current_backend_type = backend_type::jvm;
            backend = jvm::create_vm_backend(&string_symbolizer_, global_environment_);
        } else if (backend_str == "auto") {
            current_backend_type = backend_type::auto_select;
            backend = std::make_unique<interpreter_backend>(&string_symbolizer_, global_environment_);
        } else {
            current_backend_type = backend_type::interpreter;
            backend = std::make_unique<interpreter_backend>(&string_symbolizer_, global_environment_);
        }
    } else {
        current_backend_type = backend_type::interpreter;
        backend = std::make_unique<interpreter_backend>(&string_symbolizer_, global_environment_);
    }
    
    
    // Set up class lookup callback
    backend->set_class_lookup_callback([this](const std::string& name) -> std::shared_ptr<class_definition> {
        auto it = classes.find(name);
        if (it != classes.end()) {
            return it->second;
        }
        return nullptr;
    });
    
    // Initialize commonly used type_info objects for fast access
    // These are interned immediately so basic types are always available
    type_info int_info = type_info::make_int(string_symbolizer_);
    type_info_int_ = &type_infos_.emplace(type_key{script_value_type::jai_int_type, 0, 0}, int_info).first->second;
    type_id_index_[type_info_int_->id] = type_info_int_;

    type_info float_info = type_info::make_float(string_symbolizer_);
    type_info_float_ = &type_infos_.emplace(type_key{script_value_type::jai_float_type, 0, 0}, float_info).first->second;
    type_id_index_[type_info_float_->id] = type_info_float_;

    type_info string_info = type_info::make_string(string_symbolizer_);
    type_info_string_ = &type_infos_.emplace(type_key{script_value_type::jai_string_type, 0, 0}, string_info).first->second;
    type_id_index_[type_info_string_->id] = type_info_string_;

    type_info bool_info = type_info::make_bool(string_symbolizer_);
    type_info_bool_ = &type_infos_.emplace(type_key{script_value_type::jai_bool_type, 0, 0}, bool_info).first->second;
    type_id_index_[type_info_bool_->id] = type_info_bool_;

    type_info char_info = type_info::make_char(string_symbolizer_);
    type_info_char_ = &type_infos_.emplace(type_key{script_value_type::jai_char_type, 0, 0}, char_info).first->second;
    type_id_index_[type_info_char_->id] = type_info_char_;

    type_info void_info = type_info::make_void(string_symbolizer_);
    type_info_void_ = &type_infos_.emplace(type_key{script_value_type::jai_null_type, 0, 0}, void_info).first->second;
    type_id_index_[type_info_void_->id] = type_info_void_;

    type_info invalid_info = type_info::make_invalid(string_symbolizer_);
    type_info_invalid_ = &type_infos_.emplace(type_key{script_value_type::jai_invalid_type, 0, 0}, invalid_info).first->second;
    type_id_index_[type_info_invalid_->id] = type_info_invalid_;

    // Register standard C++ implicit conversions
    // NOTE: These conversions can't use engine reference yet because the engine isn't fully constructed
    // They will be updated to use proper engine references in initialize_engine_reference()
    // For now, we'll leave them empty and register them properly after engine construction
}

engine::implementation::~implementation() = default;

void engine::implementation::updateOverloadedFunction(const std::string& name, engine* engine_ptr) {
    // Create a dispatch function that selects the right overload
    // Make a copy of the name to ensure it survives the lambda lifetime
    std::string functionName = name;
    script_function dispatcher = [this, functionName](const std::vector<script_value>& args) -> checked_result<script_value> {
        auto it = overloadedFunctions.find(functionName);
        if (it == overloadedFunctions.end()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
                "Overloaded function '" + functionName + "' not found");
        }

        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                "No matching overload found for function '" + functionName + "' with " + std::to_string(args.size()) + " arguments");
        }

        const script_function& func = bestMatch.as_function();
        return func(args);
    };
    
    // Update in global environment
    script_value dispatcherValue = script_value::make_function(dispatcher, engine_ptr->weak_from_this());
    global_environment_->define(name, dispatcherValue);
}

engine::engine() : impl(std::make_unique<implementation>()) {
    // Set up the subscript resolver for custom [] operators
    impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> checked_result<script_value> {
        auto it = impl->overloadedFunctions.find("[]");
        if (it == impl->overloadedFunctions.end()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                "No custom subscript operator registered");
        }

        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                "No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
        }

        const script_function& func = bestMatch.as_function();
        return func(args);
    });
    
    // Custom extractor will be set up in the conversion registry in initialize_engine_reference()
    
    // TODO: Remove static converter setup - now using engine-bound conversions
    
    // Built-in functions will be added in initialize_engine_reference() after engine is fully constructed
    
    // Standard container conversions will be added in initialize_engine_reference()
    // when we have proper engine reference
    
    // Custom conversions that create script_value will be added in initialize_engine_reference()
    
    // Built-in functions that create script_value will be added in initialize_engine_reference()
}

engine::~engine() = default;

engine::engine(engine&&) noexcept = default;
engine& engine::operator=(engine&&) noexcept = default;

void engine::initialize_engine_reference() {
    // Pass the engine reference to the backend
    impl->backend->set_engine_reference(weak_from_this());
    
    // Pass the engine reference to the conversion registry
    impl->conversions->set_engine(weak_from_this());

    // Initialize cached operator symbol IDs for fast operator overload lookup
    impl->op_plus_id_ = impl->string_symbolizer_.intern("+");
    impl->op_minus_id_ = impl->string_symbolizer_.intern("-");
    impl->op_star_id_ = impl->string_symbolizer_.intern("*");
    impl->op_slash_id_ = impl->string_symbolizer_.intern("/");
    impl->op_percent_id_ = impl->string_symbolizer_.intern("%");
    impl->op_less_id_ = impl->string_symbolizer_.intern("<");
    impl->op_less_equal_id_ = impl->string_symbolizer_.intern("<=");
    impl->op_greater_id_ = impl->string_symbolizer_.intern(">");
    impl->op_greater_equal_id_ = impl->string_symbolizer_.intern(">=");
    impl->op_equal_equal_id_ = impl->string_symbolizer_.intern("==");
    impl->op_bang_equal_id_ = impl->string_symbolizer_.intern("!=");
    impl->op_spaceship_id_ = impl->string_symbolizer_.intern("<=>");
    impl->op_ampersand_id_ = impl->string_symbolizer_.intern("&");
    impl->op_pipe_id_ = impl->string_symbolizer_.intern("|");
    impl->op_caret_id_ = impl->string_symbolizer_.intern("^");
    impl->op_left_shift_id_ = impl->string_symbolizer_.intern("<<");
    impl->op_right_shift_id_ = impl->string_symbolizer_.intern(">>");

    // Now that we have a proper engine reference, add conversions and functions that create script_value
    auto engine_weak = weak_from_this();
    
    // Set up custom extractor for class_instance objects in the conversion registry
    impl->conversions->set_custom_extractor([this](const std::string& type_name, std::shared_ptr<void> obj) -> std::shared_ptr<void> {
        // Check if this is a class that was registered with class_builder
        // class_builder creates objects with type_name matching the class name
        auto classIt = impl->classes.find(type_name);
        if (classIt != impl->classes.end()) {
            // This is a registered class, obj should be a class_instance
            auto instance = std::static_pointer_cast<class_instance>(obj);

            // Get the C++ object from the special field
            uint64_t cpp_object_field_id = symbolize(class_constants::CPP_OBJECT_FIELD);
            script_value cppObjValue = instance->get_field(cpp_object_field_id);
            if (!cppObjValue.is_null() && cppObjValue.type() == script_value_type::jai_object_type) {
                // Direct access to the object_holder to avoid recursive extraction
                auto objHolder = cppObjValue.get_object_holder();
                return objHolder->data;
            }
        }
        return nullptr;
    });
    
    // Register standard container conversions for common types
    conversions::conversion_manager conv_mgr(impl->conversions, this);
    
    // Vector conversions for common types
    conv_mgr.add_vector_conversion<int>();           // Platform int (usually int32_t)
    conv_mgr.add_vector_conversion<int32_t>();       // Explicit 32-bit
    conv_mgr.add_vector_conversion<int64_t>();       // Explicit 64-bit (same as script_int)
    conv_mgr.add_vector_conversion<float>();         // 32-bit float
    conv_mgr.add_vector_conversion<double>();        // 64-bit double (same as script_float)
    conv_mgr.add_vector_conversion<char>();          // Single character
    conv_mgr.add_vector_conversion<std::string>();   // Strings
    conv_mgr.add_vector_conversion<bool>();          // Booleans
    
    // Map conversions for common key/value combinations
    // String keys (most common)
    conv_mgr.add_map_conversion<std::string, int>();
    conv_mgr.add_map_conversion<std::string, int32_t>();
    conv_mgr.add_map_conversion<std::string, int64_t>();
    conv_mgr.add_map_conversion<std::string, double>();
    conv_mgr.add_map_conversion<std::string, float>();
    conv_mgr.add_map_conversion<std::string, std::string>();
    conv_mgr.add_map_conversion<std::string, bool>();
    
    // Integer keys
    conv_mgr.add_map_conversion<int, int>();
    conv_mgr.add_map_conversion<int, int32_t>();
    conv_mgr.add_map_conversion<int, int64_t>();
    conv_mgr.add_map_conversion<int, double>();
    conv_mgr.add_map_conversion<int, float>();
    conv_mgr.add_map_conversion<int, std::string>();
    conv_mgr.add_map_conversion<int, bool>();
    
    conv_mgr.add_map_conversion<int32_t, int>();
    conv_mgr.add_map_conversion<int32_t, int32_t>();
    conv_mgr.add_map_conversion<int32_t, int64_t>();
    conv_mgr.add_map_conversion<int32_t, double>();
    conv_mgr.add_map_conversion<int32_t, std::string>();
    
    conv_mgr.add_map_conversion<int64_t, int>();
    conv_mgr.add_map_conversion<int64_t, int32_t>();
    conv_mgr.add_map_conversion<int64_t, int64_t>();
    conv_mgr.add_map_conversion<int64_t, double>();
    conv_mgr.add_map_conversion<int64_t, std::string>();
    
    // Double keys
    conv_mgr.add_map_conversion<double, double>();
    conv_mgr.add_map_conversion<double, int>();
    conv_mgr.add_map_conversion<double, std::string>();
    
    // Nested vector conversions
    conv_mgr.add_vector_conversion<std::vector<int>>();
    conv_mgr.add_vector_conversion<std::vector<double>>();
    conv_mgr.add_vector_conversion<std::vector<std::string>>();
    
    // Map of vectors
    conv_mgr.add_map_conversion<std::string, std::vector<int>>();
    conv_mgr.add_map_conversion<std::string, std::vector<double>>();
    conv_mgr.add_map_conversion<std::string, std::vector<std::string>>();
    
    // Bound array conversions for zero-copy performance
    conv_mgr.add_bound_array_conversion<int>();
    conv_mgr.add_bound_array_conversion<int32_t>();
    conv_mgr.add_bound_array_conversion<int64_t>();
    conv_mgr.add_bound_array_conversion<float>();
    conv_mgr.add_bound_array_conversion<double>();
    conv_mgr.add_bound_array_conversion<char>();
    conv_mgr.add_bound_array_conversion<std::string>();
    conv_mgr.add_bound_array_conversion<bool>();
    
    // Bound map conversions
    conv_mgr.add_bound_map_conversion<std::string, int>();
    conv_mgr.add_bound_map_conversion<std::string, int64_t>();
    conv_mgr.add_bound_map_conversion<std::string, double>();
    conv_mgr.add_bound_map_conversion<std::string, float>();
    conv_mgr.add_bound_map_conversion<std::string, std::string>();
    conv_mgr.add_bound_map_conversion<std::string, bool>();
    
    // Register custom conversions for numeric types
    // int32_t conversion with bounds checking
    conv_mgr.add_custom_conversion<int32_t>(
        [](const script_value& v) -> int32_t {
            script_int val = v.as_int();
            if (val > std::numeric_limits<int32_t>::max() || val < std::numeric_limits<int32_t>::min()) {
                throw std::overflow_error("Value " + std::to_string(val) + " is out of range for int32_t");
            }
            return static_cast<int32_t>(val);
        },
        [engine_weak](const int32_t& val) -> script_value {
            return script_value(static_cast<script_int>(val), engine_weak);
        }
    );
    
    // int64_t is the same as script_int, so direct conversion
    conv_mgr.add_custom_conversion<int64_t>(
        [](const script_value& v) -> int64_t {
            return v.as_int();
        },
        [engine_weak](const int64_t& val) -> script_value {
            return script_value(val, engine_weak);
        }
    );
    
    // float conversion with potential precision loss
    conv_mgr.add_custom_conversion<float>(
        [](const script_value& v) -> float {
            return static_cast<float>(v.as_float());
        },
        [engine_weak](const float& val) -> script_value {
            return script_value(static_cast<script_float>(val), engine_weak);
        }
    );
    
    // Register standard C++ implicit conversions with proper engine references
    // Promotions (lossless) - cost 1
    impl->conversions->register_builtin_conversion(script_value_type::jai_bool_type, script_value_type::jai_int_type, 1,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_int>(v.as_bool() ? 1 : 0), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_char_type, script_value_type::jai_int_type, 1,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_int>(v.as_char()), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_int_type, script_value_type::jai_float_type, 1,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_float>(v.as_int()), engine_weak); });
    
    // Standard conversions (may lose precision) - cost 2
    impl->conversions->register_builtin_conversion(script_value_type::jai_float_type, script_value_type::jai_int_type, 2,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_int>(v.as_float()), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_int_type, script_value_type::jai_char_type, 2,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_char>(v.as_int()), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_int_type, script_value_type::jai_bool_type, 2,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_bool>(v.as_int() != 0), engine_weak); });
    
    // Other numeric conversions - cost 3
    impl->conversions->register_builtin_conversion(script_value_type::jai_float_type, script_value_type::jai_bool_type, 3,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_bool>(v.as_float() != 0.0), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_bool_type, script_value_type::jai_float_type, 3,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_float>(v.as_bool() ? 1.0 : 0.0), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_char_type, script_value_type::jai_float_type, 3,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_float>(v.as_char()), engine_weak); });
    
    impl->conversions->register_builtin_conversion(script_value_type::jai_float_type, script_value_type::jai_char_type, 3,
        [engine_weak](const script_value& v) { return script_value(static_cast<script_char>(static_cast<script_int>(v.as_float())), engine_weak); });
    
    // Register standard container conversions now that we have engine reference
    add_standard_conversions();

    // BOOTSTRAP: Manually register class_definition to avoid chicken-and-egg problem
    // We can't use class_builder because it would try to create a script_value wrapping
    // a class_definition, which requires class_definition to already be registered!
    {
        auto class_def = std::make_shared<class_definition>("class_definition",
            impl->class_definition_type_id_, class_definition::cpp_class, engine_weak);
        // Manually set the persistent type_info using engine's interning
        class_def->set_type_info(get_type_info_object("class_definition"));
        // Register in all the maps
        impl->classes["class_definition"] = class_def;
        impl->classesByTypeId[impl->class_definition_type_id_] = class_def;
        auto reg_result = impl->class_registry_.register_cpp_class(class_def);
        if (!reg_result) {
            throw runtime_error("Failed to register class_definition metaclass during bootstrap");
        }
    }

    // Register int64_t to int conversion with bounds checking
    conversions::conversion_manager conv_manager(impl->conversions, this);
    conv_manager.add_custom_conversion<int>(
        [](const script_value& v) -> int {
            script_int val = v.as_int();  // Use as_int() instead of as<int64_t>()
            if (val > std::numeric_limits<int>::max() || val < std::numeric_limits<int>::min()) {
                throw std::overflow_error("Value " + std::to_string(val) + " is out of range for int");
            }
            return static_cast<int>(val);
        },
        [engine_weak](const int& val) -> script_value {
            return script_value(static_cast<script_int>(val), engine_weak);
        }
    );
    
    // Add built-in functions
    add_function("print", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
        for (const auto& arg : args) {
            std::cout << arg.to_string();
        }
        std::cout << std::endl;
        return script_value(std::monostate{}, engine_weak);
    });
    
    // Note: weak_ptr and shared_ptr are now handled as type constructors in the interpreter
    // They are keywords and follow C++ semantics:
    // - weak_ptr<T> var;           // Creates empty weak_ptr
    // - weak_ptr<T> var = obj;     // Creates weak_ptr from object
    // - weak_ptr<T>(obj)           // Constructor syntax
    // - shared_ptr<T> var;         // Creates empty shared_ptr  
    // - shared_ptr<T>(obj)         // Constructor syntax
    
    // weak_ptr methods are now registered as instance methods on weak_ptr objects
    // See interpreter::weak_ptr_methods_ for lock() and expired() implementations

    // expired() is now a method on weak_ptr objects - see interpreter::weak_ptr_methods_
}


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
                impl->backend = jvm::create_vm_backend(&impl->string_symbolizer_, impl->global_environment_);
                impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                                          impl->overloadedFunctions.count("-") > 0 ||
                                                          impl->overloadedFunctions.count("*") > 0 ||
                                                          impl->overloadedFunctions.count("/") > 0);
                impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> checked_result<script_value> {
                    auto it = impl->overloadedFunctions.find("[]");
                    if (it == impl->overloadedFunctions.end()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                            "No custom subscript operator registered");
                    }

                    script_value bestMatch = it->second.findBestMatch(args);
                    if (bestMatch.is_null()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                            "No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
                    }

                    const script_function& func = bestMatch.as_function();
                    return func(args);
                });
                
                impl->backend->set_class_lookup_callback([this](const std::string& name) -> std::shared_ptr<class_definition> {
                    auto it = impl->classes.find(name);
                    if (it != impl->classes.end()) {
                        return it->second;
                    }
                    return nullptr;
                });
            } else if (selected_type == backend_type::interpreter && !dynamic_cast<interpreter_backend*>(impl->backend.get())) {
                impl->backend = std::make_unique<interpreter_backend>(&impl->string_symbolizer_, impl->global_environment_);
                impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                                          impl->overloadedFunctions.count("-") > 0 ||
                                                          impl->overloadedFunctions.count("*") > 0 ||
                                                          impl->overloadedFunctions.count("/") > 0);
                impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> checked_result<script_value> {
                    auto it = impl->overloadedFunctions.find("[]");
                    if (it == impl->overloadedFunctions.end()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                            "No custom subscript operator registered");
                    }

                    script_value bestMatch = it->second.findBestMatch(args);
                    if (bestMatch.is_null()) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                            "No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
                    }

                    const script_function& func = bestMatch.as_function();
                    return func(args);
                });
                
                impl->backend->set_class_lookup_callback([this](const std::string& name) -> std::shared_ptr<class_definition> {
                    auto it = impl->classes.find(name);
                    if (it != impl->classes.end()) {
                        return it->second;
                    }
                    return nullptr;
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
        parser parser(tokens, &impl->string_symbolizer_, this, impl->registeredTemplateTypes);
        auto parse_result = parser.parse();

        // Convert checked_result to exception at API boundary
        if (!parse_result) {
            throw parse_error("Parse error: " + parse_result.error().message());
        }

        script_value result = impl->backend->execute(parse_result.value());

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

    } catch (const script_exception&) {
        // Script exceptions bubble up to C++
        impl->backend->prepare_for_execution();
        throw;
    } catch (const parse_error&) {
        // Parse errors should propagate as-is (don't wrap compilation errors)
        impl->backend->prepare_for_execution();
        throw;
    } catch (const std::runtime_error& e) {
        // Wrap runtime C++ exceptions as script exceptions for consistency
        impl->backend->prepare_for_execution();
        throw script_exception(std::string("C++ exception: ") + e.what());
    } catch (const std::exception& e) {
        // Wrap other C++ exceptions with their actual message
        impl->backend->prepare_for_execution();
        throw script_exception(std::string("C++ exception: ") + e.what());
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
    // Intern the name and get a stable string_view for tracking
    uint64_t id = impl->string_symbolizer_.intern(name);
    impl->global_environment_->define(id, std::move(value));

    // Track if this global should NOT be serialized (using string_view from symbolizer)
    std::string_view name_view = impl->string_symbolizer_.get_string(id);
    if (!is_serializable) {
        impl->nonSerializableGlobals.insert(name_view);
    } else {
        // In case it was previously marked as non-serializable
        impl->nonSerializableGlobals.erase(name_view);
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
    auto existing_result = impl->global_environment_->get(name);
    bool hasExistingFunction = existing_result && existing_result.value().is_function();

    auto overloadIt = impl->overloadedFunctions.find(name);

    if (hasExistingFunction) {
        // Move existing function to overloaded set
        if (overloadIt == impl->overloadedFunctions.end()) {
            // Check if we have arity info for the existing function
            auto arityIt = impl->functionArities.find(name);
            size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;

            impl->getOrCreateOverloadSet(name).setConversionRegistry(impl->conversions);
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, std::move(existing_result.value()));
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
        
        // Now add the new function as an overload
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func, weak_from_this()));
        impl->updateOverloadedFunction(name, this);
    } else if (overloadIt != impl->overloadedFunctions.end()) {
        // Already have overloaded functions, just add this one
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func, weak_from_this()));
        impl->updateOverloadedFunction(name, this);
    } else {
        // No existing function, just add normally
        // Store arity info for future use
        script_value funcValue = script_value::make_function(func, weak_from_this());
        impl->global_environment_->define(name, funcValue);
        impl->functionArities[name] = arity;
    }
}

void engine::add_overloaded_function(const std::string& name, size_t argCount, script_function func) {
    // Check if we need to move an existing function from global_environment_
    auto existing_result = impl->global_environment_->get(name);
    bool hasExistingFunction = existing_result && existing_result.value().is_function();

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
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, std::move(existing_result.value()));
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
    }
    
    // Now add the new overload
    impl->getOrCreateOverloadSet(name).addOverload(argCount, script_value::make_function(func, weak_from_this()));
    impl->updateOverloadedFunction(name, this);
}

void engine::add_overloaded_functionWithTypes(const std::string& name, size_t argCount, script_function func, const std::vector<script_value_type>& paramTypes) {
    // Check if we need to move an existing function from global_environment_
    auto existing_result = impl->global_environment_->get(name);
    bool hasExistingFunction = existing_result && existing_result.value().is_function();

    if (hasExistingFunction) {
        // Move existing function to overloaded set first
        // Check if we have arity info for the existing function
        auto arityIt = impl->functionArities.find(name);
        size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;

        impl->getOrCreateOverloadSet(name).addOverload(existingArity, std::move(existing_result.value()));
        if (arityIt != impl->functionArities.end()) {
            impl->functionArities.erase(arityIt);
        }
    }
    
    // Now add the new overload with type information
    impl->getOrCreateOverloadSet(name).addOverload(argCount, script_value::make_function(func, weak_from_this()), paramTypes);
    impl->updateOverloadedFunction(name, this);
}

void engine::add_functionWithArityAndTypes(const std::string& name, script_function func, size_t arity, const std::vector<script_value_type>& paramTypes) {
    // Check if we have an existing function with this name
    auto existing_result = impl->global_environment_->get(name);
    bool hasExistingFunction = existing_result && existing_result.value().is_function();

    auto overloadIt = impl->overloadedFunctions.find(name);

    if (hasExistingFunction) {
        // Move existing function to overloaded set
        if (overloadIt == impl->overloadedFunctions.end()) {
            // Check if we have arity info for the existing function
            auto arityIt = impl->functionArities.find(name);
            size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;

            impl->getOrCreateOverloadSet(name).setConversionRegistry(impl->conversions);
            impl->getOrCreateOverloadSet(name).addOverload(existingArity, std::move(existing_result.value()));
            if (arityIt != impl->functionArities.end()) {
                impl->functionArities.erase(arityIt);
            }
        }
        
        // Now add the new function as an overload with type info
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func, weak_from_this()), paramTypes);
        impl->updateOverloadedFunction(name, this);
    } else if (overloadIt != impl->overloadedFunctions.end()) {
        // Already have overloaded functions, just add this one with type info
        impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func, weak_from_this()), paramTypes);
        impl->updateOverloadedFunction(name, this);
    } else {
        // No existing function, check if we have type info
        if (!paramTypes.empty()) {
            // Have type info, create overload set immediately
            impl->getOrCreateOverloadSet(name).addOverload(arity, script_value::make_function(func, weak_from_this()), paramTypes);
            impl->updateOverloadedFunction(name, this);
        } else {
            // No type info, add normally
            script_value funcValue = script_value::make_function(func, weak_from_this());
            impl->global_environment_->define(name, funcValue);
            impl->functionArities[name] = arity;
        }
    }
}

string_symbolizer* engine::get_symbolizer() {
    return &impl->string_symbolizer_;
}

uint64_t engine::symbolize(const std::string& str) {
    return impl->string_symbolizer_.intern(str);
}

void engine::add_class_impl(const std::string& name, std::shared_ptr<class_definition> classDef) {
    impl->classes[name] = classDef;
    impl->classesByTypeId[classDef->get_type_id()] = classDef;  // Fast lookup by interned type_id
    // Also register with the unified class_registry for both C++ and script classes
    auto reg_result = impl->class_registry_.register_cpp_class(classDef);
    if (!reg_result) {
        throw runtime_error("Failed to register C++ class: " + name);
    }

    // Store the class definition in the global environment with __class_ prefix
    // This allows static member access (ClassName::static_field) to work for C++ classes
    // Use optimized make_object with cached type_id for fast type checking
    impl->global_environment_->define("__class_" + name, script_value::make_object("class_definition", impl->class_definition_type_id_, classDef, weak_from_this()));
}

std::shared_ptr<class_definition> engine::get_class_definition(const std::string& name) const {
    // First check C++ classes
    auto it = impl->classes.find(name);
    if (it != impl->classes.end()) {
        return it->second;
    }

    // Also check script classes (script_class_definition inherits from class_definition)
    auto script_class = impl->class_registry_.find_script_class(name);
    if (script_class) {
        return std::static_pointer_cast<class_definition>(script_class);
    }

    return nullptr;
}

std::shared_ptr<class_definition> engine::get_class_definition(uint64_t type_id) const {
    // First check C++ classes by type_id
    auto it = impl->classesByTypeId.find(type_id);
    if (it != impl->classesByTypeId.end()) {
        return it->second;
    }

    // For script classes, we need to check each one since they're not indexed by type_id
    // This is slower but script classes should eventually be added to classesByTypeId as well
    // TODO: Index script classes by type_id for O(1) lookup
    for (const auto& [name, script_class] : impl->class_registry_.script_classes_) {
        if (impl->string_symbolizer_.intern(name) == type_id) {
            return std::static_pointer_cast<class_definition>(script_class);
        }
    }

    return nullptr;
}

void engine::register_type_conversion(script_value_type from, script_value_type to, int cost, 
                                  std::function<script_value(const script_value&)> converter) {
    impl->conversions->register_builtin_conversion(from, to, cost, converter);
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
        script_function dispatcher = [this, functionName](const std::vector<script_value>& args) -> checked_result<script_value> {
            auto it = impl->overloadedFunctions.find(functionName);
            if (it == impl->overloadedFunctions.end()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
                    "Overloaded function '" + functionName + "' not found");
            }

            script_value bestMatch = it->second.findBestMatch(args);
            if (bestMatch.is_null()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                    "No matching overload found for function '" + functionName + "' with " + std::to_string(args.size()) + " arguments");
            }

            // Call the selected overload
            const script_function& func = bestMatch.as_function();
            return func(args);
        };
        
        return script_value::make_function(dispatcher, std::const_pointer_cast<engine>(shared_from_this()));
    }
    
    throw runtime_error("Variable '" + name + "' not found");
}

bool engine::has_variable(const std::string& name) const {
    // Delegate to backend to ensure consistency
    return impl->backend->has_variable(name);
}

bool engine::has_function(const std::string& name) const {
    // Check overloaded functions first (fast hashmap lookup)
    if (impl->overloadedFunctions.find(name) != impl->overloadedFunctions.end()) {
        return true;
    }

    // Check if there's a function in global environment
    // Use contains() first to avoid string allocation on "not found" path
    if (!impl->global_environment_->contains(name)) {
        return false;
    }

    // Name exists - get it and check if it's a function
    auto val_result = impl->global_environment_->get(name);
    return val_result && val_result.value().is_function();
}

bool engine::is_type_name(const std::string& name) const {
    // Check if it's a registered class
    return impl->classes.find(name) != impl->classes.end();
}

std::shared_ptr<environment> engine::get_global_environment() const {
    return impl->global_environment_;
}

engine::state engine::get_state() const {
    // Build ordered map of serializable globals for deterministic serialization
    std::map<std::string, script_value> orderedGlobals;
    
    // Get all variables from the global environment
    auto allVars = impl->global_environment_->get_all_variables();
    
    // Filter out non-serializable ones
    for (const auto& [name, value] : allVars) {
        if (impl->nonSerializableGlobals.find(name) == impl->nonSerializableGlobals.end()) {
            orderedGlobals[std::string{name}] = value;
        }
    }
    
    return state{orderedGlobals};
}

void engine::set_state(const state& state) {
    // Get all current variables
    auto currentVars = impl->global_environment_->get_all_variables();
    
    // Create a new environment with only non-serializable globals
    auto newEnv = std::make_shared<environment>(&impl->string_symbolizer_);
    
    // Copy over non-serializable globals
    for (const auto& [name, value] : currentVars) {
        if (impl->nonSerializableGlobals.find(name) != impl->nonSerializableGlobals.end()) {
            newEnv->define(std::string{name}, value);
        }
    }
    
    // Add the new state globals  
    for (const auto& [name, value] : state.globals) {
        newEnv->define(name, value);
    }
    
    // Replace the global environment
    impl->global_environment_ = newEnv;
    
    // Update the backend to use the new environment
    impl->backend = std::make_unique<interpreter_backend>(&impl->string_symbolizer_, impl->global_environment_);
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

void engine::register_type_converter_impl(const std::type_info& type, std::function<script_value(const void*)> converter) {
    // Register with the conversion registry using type_id
    auto registry = impl->conversions;
    if (registry) {
        registry->register_cpp_type_converter(conversions::type_id::of_type(type), converter);
    }
}

script_value engine::convert_to_value(const std::type_info& type, const void* obj) const {
    auto registry = impl->conversions;
    if (registry) {
        return registry->convert_cpp_type_from_void(conversions::type_id::of_type(type), obj);
    }
    throw runtime_error("No converter registered for type: " + std::string(type.name()));
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
            impl->backend = jvm::create_vm_backend(&impl->string_symbolizer_, impl->global_environment_);
            break;
        case backend_type::interpreter:
            impl->backend = std::make_unique<interpreter_backend>(&impl->string_symbolizer_, impl->global_environment_);
            break;
        case backend_type::auto_select:
            impl->backend = std::make_unique<interpreter_backend>(&impl->string_symbolizer_, impl->global_environment_);
            break;
    }
    
    impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                              impl->overloadedFunctions.count("-") > 0 ||
                                              impl->overloadedFunctions.count("*") > 0 ||
                                              impl->overloadedFunctions.count("/") > 0);

    impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> checked_result<script_value> {
        auto it = impl->overloadedFunctions.find("[]");
        if (it == impl->overloadedFunctions.end()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                "No custom subscript operator registered");
        }

        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                "No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
        }

        const script_function& func = bestMatch.as_function();
        return func(args);
    });
}

void engine::set_backend(std::unique_ptr<execution_backend> backend) {
    impl->backend = std::move(backend);
    impl->current_backend_type = backend_type::auto_select;
    
    impl->backend->set_has_custom_numeric_ops(impl->overloadedFunctions.count("+") > 0 || 
                                              impl->overloadedFunctions.count("-") > 0 ||
                                              impl->overloadedFunctions.count("*") > 0 ||
                                              impl->overloadedFunctions.count("/") > 0);

    impl->backend->set_subscript_resolver([this](const std::vector<script_value>& args) -> checked_result<script_value> {
        auto it = impl->overloadedFunctions.find("[]");
        if (it == impl->overloadedFunctions.end()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation),
                "No custom subscript operator registered");
        }

        script_value bestMatch = it->second.findBestMatch(args);
        if (bestMatch.is_null()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                "No matching overload found for function '[]' with " + std::to_string(args.size()) + " arguments");
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

// Removed duplicate - already defined at line 748

void engine::register_polymorphic_copier_impl(std::type_index derived_type, 
                                             std::type_index base_type,
                                             std::function<std::shared_ptr<void>(const void*)> copier) {
    impl->polymorphic_copiers.register_type(derived_type, base_type, std::move(copier));
}

void engine::register_class_by_type(std::type_index type, std::shared_ptr<class_definition> classDef) {
    impl->classesByType[type] = classDef;
}

std::shared_ptr<class_definition> engine::get_class_definition_by_type(const std::type_index& type) const {
    auto it = impl->classesByType.find(type);
    if (it != impl->classesByType.end()) {
        return it->second;
    }
    return nullptr;
}

void engine::add_standard_conversions() {
    conversions::register_all_standard_conversions(impl->conversions);
}

std::shared_ptr<conversions::conversion_registry> engine::get_conversion_registry() const {
    return impl->conversions;
}

// === TYPE INFO INTERNING METHODS ===

type_info* engine::get_type_info_int() {
    return impl->type_info_int_;
}

type_info* engine::get_type_info_float() {
    return impl->type_info_float_;
}

type_info* engine::get_type_info_string() {
    return impl->type_info_string_;
}

type_info* engine::get_type_info_bool() {
    return impl->type_info_bool_;
}

type_info* engine::get_type_info_char() {
    return impl->type_info_char_;
}

type_info* engine::get_type_info_void() {
    return impl->type_info_void_;
}

type_info* engine::get_type_info_invalid() {
    return impl->type_info_invalid_;
}

type_info* engine::get_type_info_array(type_info* element_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_array_type, element_type ? element_type->id : 0, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    type_info temp = type_info::make_array(impl->string_symbolizer_, element_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_map(type_info* key_type, type_info* value_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_map_type,
                              key_type ? key_type->id : 0,
                              value_type ? value_type->id : 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    type_info temp = type_info::make_map(impl->string_symbolizer_, key_type, value_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_object(const std::string& class_name) {
    // Use composite key for fast lookup
    uint64_t class_name_id = impl->string_symbolizer_.intern(class_name);
    implementation::type_key key{script_value_type::jai_object_type, class_name_id, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    type_info temp = type_info::make_object(impl->string_symbolizer_, class_name);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_object(uint64_t type_id) {
    // Use composite key for fast lookup
    implementation::type_key key{script_value_type::jai_object_type, type_id, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // If not found, we need to look up the class name from the type_id
    // This should only happen if the type_id was created but the type_info wasn't interned yet
    std::string_view class_name = impl->string_symbolizer_.get_string(type_id);
    if (class_name.empty()) {
        return nullptr; // Invalid type_id
    }

    // Cache miss - construct full type_info and insert
    type_info temp = type_info::make_object(impl->string_symbolizer_, class_name);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_weak_ptr(type_info* pointee_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_weak_ptr_type, pointee_type ? pointee_type->id : 0, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    type_info temp = type_info::make_weak_ptr(impl->string_symbolizer_, pointee_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_reference(type_info* referenced_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_reference_type, referenced_type ? referenced_type->id : 0, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    type_info temp = type_info::make_reference(impl->string_symbolizer_, referenced_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_function(type_info* return_type, const std::vector<type_info*>& arg_types) {
    // Convert raw pointers to type_info_ptr
    std::vector<type_info_ptr> arg_type_ptrs;
    arg_type_ptrs.reserve(arg_types.size());
    for (auto* arg : arg_types) {
        arg_type_ptrs.push_back(type_info_ptr(arg));
    }

    // Create type_info and use generic get_type_info which handles composite keys
    type_info temp = type_info::make_function(impl->string_symbolizer_, return_type, arg_type_ptrs);
    return get_type_info(temp);
}

type_info* engine::get_type_info(const type_info& temp) {
    // Build composite key from temp's fields
    uint64_t param1_id = 0, param2_id = 0;

    switch (temp.base_type) {
        case script_value_type::jai_array_type:
        case script_value_type::jai_weak_ptr_type:
        case script_value_type::jai_reference_type:
            // Single type parameter
            param1_id = temp.type_params.empty() ? 0 : (temp.type_params[0] ? temp.type_params[0]->id : 0);
            break;
        case script_value_type::jai_map_type:
            // Two type parameters
            param1_id = temp.type_params.empty() ? 0 : (temp.type_params[0] ? temp.type_params[0]->id : 0);
            param2_id = temp.type_params.size() < 2 ? 0 : (temp.type_params[1] ? temp.type_params[1]->id : 0);
            break;
        case script_value_type::jai_object_type:
        case script_value_type::jai_function_type:
            // Object and function types: use temp.id which encodes full signature
            param1_id = temp.id;
            break;
        default:
            // Primitive types have no parameters
            break;
    }

    implementation::type_key key{temp.base_type, param1_id, param2_id};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Insert the new type_info and return pointer to it
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_by_id(uint64_t type_id) {
    // O(1) lookup via secondary index
    auto it = impl->type_id_index_.find(type_id);
    if (it != impl->type_id_index_.end()) {
        return it->second;
    }
    return nullptr;
}

type_info* engine::get_type_info_by_name(const std::string& canonical_name) {
    // Intern the name to get the ID, then do ID-based lookup
    uint64_t type_id = impl->string_symbolizer_.intern(canonical_name);
    return get_type_info_by_id(type_id);
}

void engine::set_conversion_registry(std::shared_ptr<conversions::conversion_registry> registry) {
    impl->conversions = registry;
}

serialization::serialization_registry& engine::get_serialization_registry() {
    return impl->serialization_registry;
}

class_registry& engine::get_class_registry() {
    return impl->class_registry_;
}

script_value engine::try_create_reference(size_t arg_index, const script_value& fallback) {
    // Check if current backend is an interpreter
    auto* interpreter_backend_ptr = dynamic_cast<interpreter_backend*>(impl->backend.get());
    if (!interpreter_backend_ptr) {
        return fallback; // Not using interpreter backend, use fallback
    }
    
    // Get the interpreter instance from the backend
    interpreter* current_interpreter = interpreter_backend_ptr->get_interpreter();
    if (!current_interpreter) {
        return fallback; // No interpreter available, use fallback
    }
    
    // Access the current argument metadata
    const auto& metadata = current_interpreter->get_current_arg_metadata();
    if (arg_index >= metadata.size()) {
        return fallback; // Index out of bounds, use fallback
    }
    
    auto [symbol_id, env] = metadata[arg_index];
    if (symbol_id == UINT64_MAX || !env) {
        return fallback; // No valid metadata for this argument, use fallback
    }
    
    // Try to get pointer to the original variable
    script_value* target = env->get_value_ptr(symbol_id);
    if (!target) {
        return fallback; // Variable not found, use fallback
    }
    
    // Create reference wrapper to original variable
    return script_value::make_reference(target, env);
}

conversions::conversion_manager engine::get_conversion_manager() {
    return conversions::conversion_manager(impl->conversions, this);
}

// Include/Import path management
void engine::include_paths(const std::vector<std::string>& paths) {
    impl->include_paths = paths;
}

void engine::add_include_path(const std::string& path) {
    impl->include_paths.push_back(path);
}

void engine::clear_include_paths() {
    impl->include_paths.clear();
}

std::vector<std::string> engine::get_include_paths() const {
    return impl->include_paths;
}

// Import behavior configuration
void engine::set_import_behavior(import_behavior behavior) {
    impl->import_behavior = behavior;
}

engine::import_behavior engine::get_import_behavior() const {
    return impl->import_behavior;
}

// Parameter storage for function calls (replaces thread_local)
detail::parameter_storage* engine::get_current_parameter_storage() const {
    return impl->current_parameter_storage;
}

void engine::set_current_parameter_storage(detail::parameter_storage* storage) {
    impl->current_parameter_storage = storage;
}

// Non-member accessor for parameter_storage - breaks circular dependency
namespace detail {
    parameter_storage* get_engine_parameter_storage(engine* eng) {
        return eng ? eng->get_current_parameter_storage() : nullptr;
    }
}

// Import management
void engine::reset_imports() {
    impl->import_cache.clear();
}

void engine::reset_import(const std::string& path) {
    impl->import_cache.erase(path);
}

bool engine::is_imported(const std::string& path) const {
    return impl->import_cache.find(path) != impl->import_cache.end();
}

std::vector<std::string> engine::get_imported_files() const {
    std::vector<std::string> result;
    result.reserve(impl->import_cache.size());
    for (const auto& [path, record] : impl->import_cache) {
        result.push_back(path);
    }
    return result;
}

script_value engine::execute_import(const std::string& resolved_path) {
    // Check import behavior
    bool should_import = false;
    
    switch (impl->import_behavior) {
        case import_behavior::always:
            should_import = true;
            break;
            
        case import_behavior::once: {
            auto it = impl->import_cache.find(resolved_path);
            should_import = (it == impl->import_cache.end());
            break;
        }
        
        case import_behavior::file_timestamp: {
            auto it = impl->import_cache.find(resolved_path);
            if (it == impl->import_cache.end()) {
                should_import = true;
            } else {
                // Check if file has been modified
                try {
                    auto current_time = std::filesystem::last_write_time(resolved_path);
                    should_import = (current_time != it->second.last_modified);
                    if (should_import) {
                        // Update the timestamp for next check
                        it->second.last_modified = current_time;
                    }
                } catch (...) {
                    // If we can't get the file time, import it
                    should_import = true;
                }
            }
            break;
        }
    }
    
    if (should_import) {
        // Read and execute the file
        std::ifstream file(resolved_path);
        if (!file.is_open()) {
            throw runtime_error("Failed to open import file: " + resolved_path);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        auto result = execute(content);
        
        // Update import cache
        implementation::import_record record;
        record.resolved_path = resolved_path;
        try {
            record.last_modified = std::filesystem::last_write_time(resolved_path);
        } catch (...) {
            // Use epoch time if we can't get file time
            record.last_modified = std::filesystem::file_time_type::min();
        }
        impl->import_cache[resolved_path] = record;
        
        return result;
    } else {
        // File already imported and doesn't need re-import
        return make_null();
    }
}


} // namespace jai

// Helper function for conversion_registry_impl.hpp to avoid circular dependency
namespace jai {
std::weak_ptr<engine> get_engine_weak_ptr(engine* eng) {
    if (!eng) {
        throw runtime_error("Engine reference required for script_value creation");
    }
    return eng->weak_from_this();
}

std::shared_ptr<conversions::conversion_registry> get_engine_conversion_registry(engine* eng) {
    return eng ? eng->get_conversion_registry() : nullptr;
}
} // namespace jai
