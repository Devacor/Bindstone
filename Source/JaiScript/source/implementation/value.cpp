#include <jaiscript/jaiscript.hpp>
#include <sstream>

namespace jai {

// Constructor implementations
script_value::script_value(script_int i, engine* eng) : type_info_(nullptr), engine_(eng), storage_(i) {
    if (eng) {
        type_info_ = eng->get_type_info_int();
    }
}

script_value::script_value(script_float f, engine* eng) : type_info_(nullptr), engine_(eng), storage_(f) {
    if (eng) {
        type_info_ = eng->get_type_info_float();
    }
}

script_value::script_value(const script_string& s, engine* eng) : type_info_(nullptr), engine_(eng), storage_(make_strong<script_string>(s)) {
    if (eng) {
        type_info_ = eng->get_type_info_string();
    }
}

script_value::script_value(script_string&& s, engine* eng) : type_info_(nullptr), engine_(eng), storage_(make_strong<script_string>(std::move(s))) {
    if (eng) {
        type_info_ = eng->get_type_info_string();
    }
}

script_value::script_value(const char* s, engine* eng) : type_info_(nullptr), engine_(eng), storage_(make_strong<script_string>(s)) {
    if (eng) {
        type_info_ = eng->get_type_info_string();
    }
}

script_value::script_value(script_char c, engine* eng) : type_info_(nullptr), engine_(eng), storage_(c) {
    if (eng) {
        type_info_ = eng->get_type_info_char();
    }
}

script_value::script_value(script_bool b, engine* eng) : type_info_(nullptr), engine_(eng), storage_(b) {
    if (eng) {
        type_info_ = eng->get_type_info_bool();
    }
}

// Factory methods (DEPRECATED - use engine-aware versions instead)
// These are kept for backwards compatibility but should not be used in new code
script_value script_value::make_array(type_info_ptr element_type) {
    // WARNING: This creates a type_info without interning - use make_array(element_type, engine) instead
    script_value v;
    // We cannot use engine interning here as we don't have an engine reference
    // This method is deprecated and should be replaced with the engine-aware version
    throw runtime_error("make_array without engine parameter is deprecated - use make_array(element_type, engine) instead");
}

script_value script_value::make_map(type_info_ptr keyType, type_info_ptr valueType) {
    // WARNING: This creates a type_info without interning - use make_map(keyType, valueType, engine) instead
    script_value v;
    // We cannot use engine interning here as we don't have an engine reference
    // This method is deprecated and should be replaced with the engine-aware version
    throw runtime_error("make_map without engine parameter is deprecated - use make_map(keyType, valueType, engine) instead");
}


script_value script_value::make_reference(script_value* target, const std::shared_ptr<environment>& env) {
    if (!target) {
        throw runtime_error("Cannot create reference to null");
    }
    // Get engine reference from the target value
    auto eng = target->get_engine();
    if (!eng) {
        throw runtime_error("Cannot create reference: target has no valid engine reference");
    }
    script_value v(std::monostate{}, eng);  // Use engine reference from target
    v.type_info_ = eng->get_type_info_reference(target->get_type_info());
    auto ref = make_strong<reference_holder>();
    ref->target = target;
    ref->sourceEnv = env;  // Store weak reference to environment
    v.storage_ = ref;
    return v;
}

// Engine-aware factory method implementations
script_value script_value::make_array(type_info_ptr element_type, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create array with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_array(element_type);

    // Create vector with small default capacity to avoid first few reallocations
    auto vec = make_strong<std::vector<script_value>>();
    vec->reserve(16);  // Reserve space for 16 elements (common small array size)
    v.storage_ = vec;
    return v;
}

script_value script_value::make_map(type_info_ptr keyType, type_info_ptr valueType, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create map with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_map(keyType, valueType);
    v.storage_ = make_strong<std::map<script_value, script_value>>();
    return v;
}

script_value script_value::make_object(const std::string& type_name, std::shared_ptr<void> data, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create object with null engine pointer");
    }

    // Intern the type name for fast comparison
    uint64_t type_id = eng->get_symbolizer()->intern(type_name);

    // Call the optimized version with type_id
    return make_object(type_name, type_id, data, eng);
}

// Optimized version with cached type_id
script_value script_value::make_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng, bool is_cpp_class) {
    if (!eng) {
        throw runtime_error("Cannot create object with null engine pointer");
    }
    // CRITICAL: Get persistent type_info from class registry (fast O(1) lookup by type_id)
    // NEVER use type_info::make_object() - it creates a TEMPORARY that gets freed (0xDDDDDDDD)

    auto class_def = eng->get_class_definition(type_id);
    if (!class_def) {
        throw runtime_error("Cannot create object of unregistered class '" + type_name +
            "'. Classes must be registered with engine.register_class() before instantiation.");
    }

    script_value v(std::monostate{}, eng);
    v.type_info_ = class_def->get_type_info();

    auto obj = make_strong<object_holder>();
    obj->type_name = type_name;
    obj->type_id = type_id;  // Set the cached type_id for fast comparison
    obj->data = data;
    obj->is_class_instance_wrapper = is_cpp_class;  // True when data is a class_instance object (both C++ and script classes), false for raw data
    v.storage_ = obj;
    return v;
}

// Factory method for raw C++ objects - requires type_id to avoid re-interning
script_value script_value::make_cpp_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create cpp_object with null engine pointer");
    }

    // CRITICAL: Get persistent type_info from class registry (fast O(1) lookup by type_id)

    auto class_def = eng->get_class_definition(type_id);
    if (!class_def) {
        throw runtime_error("Cannot create cpp_object of unregistered class '" + type_name +
            "'. Classes must be registered with engine.register_class() before instantiation.");
    }

    script_value v(std::monostate{}, eng);
    v.type_info_ = class_def->get_type_info();

    auto obj = make_strong<object_holder>();
    obj->type_name = type_name;
    obj->type_id = type_id;  // Use the provided type_id directly (no re-interning)
    obj->data = data;
    obj->is_class_instance_wrapper = false;  // make_cpp_object is for raw C++ objects
    v.storage_ = obj;
    return v;
}

script_value script_value::make_empty_weak_ptr(type_info_ptr weak_ptr_type, engine* eng) {
    script_value v(std::monostate{}, eng);
    if (weak_ptr_type) {
        v.type_info_ = weak_ptr_type;
    } else if (eng) {
        v.type_info_ = eng->get_type_info_weak_ptr(nullptr);
    }
    v.storage_ = jai::weaker_ptr<object_holder>();
    return v;
}

script_value script_value::make_invalid(engine* eng) {
    script_value val(std::monostate{}, eng);  // Start with null
    val.storage_ = invalid_tag{};  // Change to invalid
    if (eng) {
        val.type_info_ = eng->get_type_info_invalid();  // Set proper type info
    }
    return val;
}

checked_result<script_value> script_value::make_weak_ptr(const script_value& value, engine* eng) {
    if (!eng) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::unsupported_operation),
            "Cannot create weak_ptr with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_weak_ptr(value.get_type_info());

    // Only accept shared_ptr types for weak_ptr creation
    // Regular objects have value semantics and get cloned when passed as parameters,
    // so creating a weak_ptr from them doesn't work correctly
    if (value.type() == script_value_type::jai_shared_ptr_type) {
        // Get the strong_ptr<object_holder> from the value
        auto holder = value.get_object_holder();
        if (!holder) {
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unsupported_operation),
                "Failed to get object_holder from script_value");
        }

        // Create a weak_ptr from the strong_ptr
        jai::weaker_ptr<object_holder> weak = holder;

        // Store the weak_ptr directly in the variant
        v.storage_ = weak;
    } else if (value.type() == script_value_type::jai_object_type) {
        // This is a regular object with value semantics
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "Cannot create weak_ptr from a value-semantic object. Use shared_ptr<T> to enable reference semantics: auto obj = shared_ptr<T>(...); auto weak = weak_ptr<T>(obj);");
    } else {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "weak_ptr can only be created from shared_ptr<T>");
    }

    return checked_result<script_value>(v);
}

script_value script_value::make_reference(script_value* target, const std::shared_ptr<environment>& env, engine* eng) {
    script_value v = make_reference(target, env);
    v.engine_ = eng;
    return v;
}

script_value script_value::make_reference(script_value* target, const std::shared_ptr<environment>& env, engine* eng, type_info_ptr container_element_type) {
    if (!target) {
        throw runtime_error("Cannot create reference to null");
    }
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_reference(target->get_type_info());
    auto ref = make_strong<reference_holder>();
    ref->target = target;
    ref->sourceEnv = env;
    ref->container_element_type = container_element_type;  // Store the container's element type constraint
    v.storage_ = ref;
    return v;
}

script_value script_value::make_function(const script_function& func, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create function with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_function(eng->get_type_info_void(), {}); // TODO: Proper type info
    v.storage_ = make_strong<script_function>(func);
    return v;
}

// Copy constructor (shallow copy for reference semantics)
script_value::script_value(const script_value& other)
    : type_info_(other.type_info_),
      engine_(other.engine_),
      storage_(other.storage_),
      cpp_bound_ptr_(other.cpp_bound_ptr_) {
    // Simple shallow copy - shares storage with the original, including engine pointer
    // cpp_bound_ptr_ is also copied so copies of bound values remain bound
    // NOTE: Raw engine* is much faster to copy than weak_ptr (no atomic ops)
}

// Copy assignment operator (shallow copy for C++ internals)
// NOTE: This is intentionally a shallow copy for performance in C++ code.
// The interpreter handles cloning based on JaiScript value/reference semantics.
script_value& script_value::operator=(const script_value& other) {
    if (this != &other) {
        type_info_ = other.type_info_;
        engine_ = other.engine_;
        storage_ = other.storage_;
        cpp_bound_ptr_ = other.cpp_bound_ptr_;
    }
    return *this;
}

// Explicit deep copy method
script_value script_value::clone() const {
    if (!engine_) {
        throw runtime_error("Cannot clone script_value: missing engine pointer");
    }

    // Check if this is a shared_ptr type - perform deep copy using registered copy constructor
    // shared_ptr<T> is a TYPE MARKER that indicates reference semantics for normal operations,
    // but clone() should perform a deep copy to create an independent instance
    if (type_info_ && type_info_->base_type == script_value_type::jai_shared_ptr_type) {
        // Get the object_holder from storage
        auto obj_holder = std::get<strong_ptr<object_holder>>(storage_);
        if (!obj_holder) {
            throw runtime_error("Cannot clone shared_ptr: null object_holder");
        }

        // Check if this is a class_instance (script class) that supports deep copy
        if (obj_holder->is_class_instance_wrapper) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
            auto new_instance = instance->deep_copy();

            auto new_holder = make_strong<object_holder>();
            new_holder->type_name = obj_holder->type_name;
            new_holder->type_id = obj_holder->type_id;
            new_holder->data = new_instance;
            new_holder->is_class_instance_wrapper = true;

            script_value result(std::monostate{}, engine_);
            result.type_info_ = type_info_;
            result.storage_ = new_holder;
            return result;
        }

        // For C++ objects wrapped in shared_ptr, use the registered copy function
        auto class_def = engine_->get_class_definition(obj_holder->type_id);
        if (class_def && class_def->has_copy_function()) {
            auto new_cpp_obj = class_def->copy_object(obj_holder->data.get());
            if (new_cpp_obj) {
                auto new_holder = make_strong<object_holder>();
                new_holder->type_name = obj_holder->type_name;
                new_holder->type_id = obj_holder->type_id;
                new_holder->data = new_cpp_obj;
                new_holder->is_class_instance_wrapper = false;

                script_value result(std::monostate{}, engine_);
                result.type_info_ = type_info_;
                result.storage_ = new_holder;
                return result;
            }
        }

        // No copy function registered - type is non-copyable
        throw runtime_error(
            "Cannot clone shared_ptr<" + obj_holder->type_name + ">: type is non-copyable. "
            "Register a copy constructor with class_builder<" + obj_holder->type_name + ">::copy_constructor() "
            "to enable deep copying.");
    }

    script_value result(std::monostate{}, engine_);  // Preserve engine pointer!
    result.type_info_ = type_info_;
    result.cpp_bound_ptr_ = cpp_bound_ptr_;  // Preserve C++ binding

    // Handle deep copying for different types
    // Use current_type() to check what's actually stored, not the declared type
    switch (current_type()) {
        case script_value_type::jai_array_type: {
            // Deep copy the array - each element is also cloned
            auto& other_array = *std::get<strong_ptr<std::vector<script_value>>>(storage_);
            auto new_array = make_strong<std::vector<script_value>>();
            new_array->reserve(other_array.size());
            for (const auto& elem : other_array) {
                new_array->push_back(elem.clone());
            }
            result.storage_ = new_array;
            break;
        }
        case script_value_type::jai_map_type: {
            // Deep copy the map - keys and values are cloned
            auto& other_map = *std::get<strong_ptr<std::map<script_value, script_value>>>(storage_);
            auto new_map = make_strong<std::map<script_value, script_value>>();
            for (const auto& [key, value] : other_map) {
                new_map->emplace(key.clone(), value.clone());
            }
            result.storage_ = new_map;
            break;
        }
        case script_value_type::jai_object_type: {
            // Regular objects have VALUE semantics by default (deep copy)
            // Only shared_ptr<T> has reference semantics (handled by early return above)
            auto obj_holder = std::get<strong_ptr<object_holder>>(storage_);

            // Check if this is a class_instance that supports deep copy
            if (obj_holder->is_class_instance_wrapper) {
                // This is a class_instance, safe to static_cast
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Use class_instance's deep_copy method
                auto new_instance = instance->deep_copy();

                // Create new object_holder
                auto new_holder = make_strong<object_holder>();
                new_holder->type_name = obj_holder->type_name;
                new_holder->type_id = obj_holder->type_id;  // Preserve the cached type_id
                new_holder->data = new_instance;
                new_holder->is_class_instance_wrapper = true;
                result.storage_ = new_holder;
            } else {
                // FIX #2: Raw C++ objects need explicit copy support
                // Try to get copy function from class definition
                bool copied = false;
                if (engine_) {
                    auto class_def = engine_->get_class_definition(obj_holder->type_id);
                    if (class_def && class_def->has_copy_function()) {
                        auto new_cpp_obj = class_def->copy_object(obj_holder->data.get());
                        if (new_cpp_obj) {
                            auto new_holder = make_strong<object_holder>();
                            new_holder->type_name = obj_holder->type_name;
                            new_holder->type_id = obj_holder->type_id;
                            new_holder->data = new_cpp_obj;
                            new_holder->is_class_instance_wrapper = false;
                            result.storage_ = new_holder;
                            copied = true;
                        }
                    }
                }

                if (!copied) {
                    // No copy function registered - throw error instead of silent aliasing
                    throw runtime_error(
                        "Cannot deep copy C++ object of type '" + obj_holder->type_name +
                        "'. Register a copy constructor with class_builder<T>::copy_constructor() "
                        "or use shared_ptr<" + obj_holder->type_name + "> for reference semantics.");
                }
            }
            break;
        }
        case script_value_type::jai_reference_type: {
            // When cloning a reference, we want to clone the referenced value,
            // not create another reference to the same value
            return deref().clone();
        }
        case script_value_type::jai_weak_ptr_type: {
            // For weak_ptr, shallow copy the weak_ptr itself (copy the weak reference)
            result.storage_ = storage_;
            break;
        }
        case script_value_type::jai_shared_ptr_type: {
            // For shared_ptr, shallow copy the shared_ptr itself (copy the shared reference)
            result.storage_ = storage_;
            break;
        }
        default:
            // For cpp_bound values, we need to read the actual value and create an independent copy
            if (is_cpp_bound()) {
                // Read the actual value from the C++ variable and create a new independent value
                // This breaks the binding - the clone is no longer bound to the C++ variable
                if (is_int()) {
                    result.storage_ = as_int();
                    result.cpp_bound_ptr_ = nullptr;  // Not bound
                } else if (is_float()) {
                    result.storage_ = as_float();
                    result.cpp_bound_ptr_ = nullptr;
                } else if (is_bool()) {
                    result.storage_ = as_bool();
                    result.cpp_bound_ptr_ = nullptr;
                } else if (is_char()) {
                    result.storage_ = as_char();
                    result.cpp_bound_ptr_ = nullptr;
                } else if (is_string()) {
                    result.storage_ = make_strong<script_string>(as_string());
                    result.cpp_bound_ptr_ = nullptr;
                } else {
                    // For other cpp_bound types, fall back to shallow copy
                    result.storage_ = storage_;
                }
            } else {
                // For primitive types and functions, shallow copy is fine
                result.storage_ = storage_;
            }
            break;
    }

    return result;
}

const script_function& script_value::as_function() const {
    const script_value& val = deref();
    if (val.current_type() != script_value_type::jai_function_type) {
        throw runtime_error("script_value is not a function");
    }
    return *std::get<strong_ptr<script_function>>(val.storage_);
}

script_value script_value::try_unwrap_transparent_wrapper() const {
    // Only objects can be transparent wrappers
    if (!is_object() || !engine_) {
        return *this;  // Return self unchanged
    }

    auto holder = get_object_holder();
    if (!holder) {
        return *this;
    }

    // Look up the class definition
    auto class_def = engine_->get_class_definition(holder->type_id);
    if (!class_def || !class_def->is_transparent_wrapper()) {
        return *this;  // Not a transparent wrapper
    }

    // Unwrap the value
    script_value mutable_self = *this;
    script_value unwrapped = class_def->unwrap(mutable_self);

    // If unwrap succeeded (returned non-null), return the unwrapped value
    if (!unwrapped.is_null()) {
        return unwrapped;
    }

    // Unwrap failed, return self
    return *this;
}

std::string script_value::to_string() const {
    // Special handling for references to show what they point to
    if (current_type() == script_value_type::jai_reference_type) {
        return deref().to_string();
    }

    // Use current_type() to switch on what's actually stored
    switch (current_type()) {
        case script_value_type::jai_null_type:
            return "null";
        case script_value_type::jai_int_type:
            return std::to_string(as_int());
        case script_value_type::jai_float_type:
            return std::to_string(as_float());
        case script_value_type::jai_string_type:
            return as_string();
        case script_value_type::jai_char_type:
            return std::string(1, as_char());
        case script_value_type::jai_bool_type:
            return as_bool() ? "true" : "false";
        case script_value_type::jai_array_type:
            return "[array]";
        case script_value_type::jai_map_type:
            return "[map]";
        case script_value_type::jai_object_type:
            return "[object]";
        case script_value_type::jai_function_type:
            return "[function]";
        default:
            return "[unknown]";
    }
}

const script_value& script_value::deref() const {
    // Use current_type() not defined_type() - references may have type_info with different base_type
    if (current_type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Recursively deref to handle chained references
        return refHolder->target->deref();
    }
    // For C++ references, we don't deref here - they need special handling
    // The interpreter will handle them specially
    return *this;
}

script_value& script_value::deref() {
    // Use current_type() not defined_type() - references may have type_info with different base_type
    if (current_type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Recursively deref to handle chained references
        return refHolder->target->deref();
    }
    return *this;
}

void script_value::assign_through(const script_value& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Assign to the referenced value
        *refHolder->target = value;
    } else if (cpp_bound_ptr_ != nullptr) {
        // For C++ bound values, assign directly to the C++ variable
        // TODO: Store type metadata with cpp_bound values for proper casting
        // For now, we handle common C++ types (int, float, etc.)
        switch (type()) {
            case script_value_type::jai_int_type:
                *static_cast<int*>(cpp_bound_ptr_) = static_cast<int>(value.as<script_int>());
                break;
            case script_value_type::jai_float_type:
                *static_cast<double*>(cpp_bound_ptr_) = value.as<script_float>();
                break;
            case script_value_type::jai_string_type:
                *static_cast<std::string*>(cpp_bound_ptr_) = value.as<script_string>();
                break;
            case script_value_type::jai_bool_type:
                *static_cast<bool*>(cpp_bound_ptr_) = value.as<script_bool>();
                break;
            case script_value_type::jai_char_type:
                *static_cast<char*>(cpp_bound_ptr_) = value.as<script_char>();
                break;
            default:
                // For complex types, we'll need to use the conversion registry
                throw runtime_error("assign_through not yet implemented for this cpp_bound type");
        }
    } else {
        // Not a reference, direct assignment
        *this = value;
    }
}

void script_value::assign_through(script_value&& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Move assign to the referenced value
        *refHolder->target = std::move(value);
    } else if (cpp_bound_ptr_ != nullptr) {
        // For C++ bound values, we can't really move into the C++ variable
        // Just do a regular assignment
        assign_through(value);
    } else {
        // Not a reference, direct move assignment
        *this = std::move(value);
    }
}

bool script_value::operator==(const script_value& other) const {
    if (type() != other.type()) {
        return false;
    }
    
    switch (type()) {
        case script_value_type::jai_null_type:
            return true;
        case script_value_type::jai_int_type:
            return unchecked_as_int() == other.unchecked_as_int();
        case script_value_type::jai_float_type:
            return unchecked_as_float() == other.unchecked_as_float();
        case script_value_type::jai_string_type:
            return unchecked_as_string() == other.unchecked_as_string();
        case script_value_type::jai_char_type:
            return unchecked_as_char() == other.unchecked_as_char();
        case script_value_type::jai_bool_type:
            return unchecked_as_bool() == other.unchecked_as_bool();
        default:
            // TODO: Implement for complex types
            return false;
    }
}

std::strong_ordering script_value::operator<=>(const script_value& other) const {
    // First compare types
    if (auto cmp = type() <=> other.type(); cmp != 0) {
        return cmp;
    }

    // Then compare values for same types
    switch (type()) {
        case script_value_type::jai_null_type:
            return std::strong_ordering::equal; // All nulls are equal
        case script_value_type::jai_int_type:
            return unchecked_as_int() <=> other.unchecked_as_int();
        case script_value_type::jai_float_type:
            // script_float comparison returns partial_ordering, convert to strong
            if (auto cmp = unchecked_as_float() <=> other.unchecked_as_float(); cmp < 0)
                return std::strong_ordering::less;
            else if (cmp > 0)
                return std::strong_ordering::greater;
            else
                return std::strong_ordering::equal;
        case script_value_type::jai_string_type:
            return unchecked_as_string() <=> other.unchecked_as_string();
        case script_value_type::jai_char_type:
            return unchecked_as_char() <=> other.unchecked_as_char();
        case script_value_type::jai_bool_type:
            return unchecked_as_bool() <=> other.unchecked_as_bool();
        default:
            // For complex types, compare by address for now
            return &storage_ <=> &other.storage_;
    }
}


} // namespace jai