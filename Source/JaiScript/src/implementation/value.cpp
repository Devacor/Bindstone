#include <jaiscript/jaiscript.hpp>
#include <sstream>

namespace jai {

// Factory methods
script_value script_value::make_array(type_info_ptr element_type) {
    script_value v;
    v.type_info_ = type_info::make_array(element_type);
    v.storage_ = std::make_shared<std::vector<script_value>>();
    return v;
}

script_value script_value::make_map(type_info_ptr keyType, type_info_ptr valueType) {
    script_value v;
    v.type_info_ = type_info::make_map(keyType, valueType);
    v.storage_ = std::make_shared<std::map<script_value, script_value>>();
    return v;
}

script_value script_value::make_weak_ptr(const script_value& value) {
    script_value v;
    v.type_info_ = type_info::make_weak_ptr(value.get_type_info());
    
    if (value.type() == script_value_type::jai_object_type) {
        // Store a reference to the original value in a special holder
        // This allows weak_ptr to work with objects
        auto holder = std::make_shared<object_holder>();
        holder->type_name = "weak_ptr_holder";
        holder->data = std::make_shared<script_value>(value);
        holder->is_cpp_class_instance = false;
        
        // Store as object type with weak_ptr type info
        v.storage_ = holder;
    } else {
        throw runtime_error("weak_ptr can only be created from objects");
    }
    
    return v;
}

script_value script_value::make_reference(script_value* target, const std::shared_ptr<environment>& env) {
    if (!target) {
        throw runtime_error("Cannot create reference to null");
    }
    script_value v;
    v.type_info_ = type_info::make_reference(target->get_type_info());
    auto ref = std::make_shared<reference_holder>();
    ref->target = target;
    ref->sourceEnv = env;  // Store weak reference to environment
    v.storage_ = ref;
    return v;
}

script_value script_value::make_object(const std::string& type_name, std::shared_ptr<void> data) {
    script_value v;
    v.type_info_ = type_info::make_object(type_name);
    auto obj = std::make_shared<object_holder>();
    obj->type_name = type_name;
    obj->data = data;
    obj->is_cpp_class_instance = true;  // make_object is for class_instance wrapper objects
    v.storage_ = obj;
    return v;
}

script_value script_value::make_cpp_object(const std::string& type_name, std::shared_ptr<void> data) {
    script_value v;
    v.type_info_ = type_info::make_object(type_name);
    auto obj = std::make_shared<object_holder>();
    obj->type_name = type_name;
    obj->data = data;
    obj->is_cpp_class_instance = false;  // make_cpp_object is for raw C++ objects
    v.storage_ = obj;
    return v;
}

script_value script_value::make_function(const script_function& func) {
    script_value v;
    v.type_info_ = type_info::make_function(type_info::make_void(), {}); // TODO: Proper type info
    v.storage_ = func;
    return v;
}

// Engine-aware factory method implementations
script_value script_value::make_array(type_info_ptr element_type, std::weak_ptr<engine> eng) {
    script_value v = make_array(element_type);
    v.engine_ref_ = eng;
    return v;
}

script_value script_value::make_map(type_info_ptr keyType, type_info_ptr valueType, std::weak_ptr<engine> eng) {
    script_value v = make_map(keyType, valueType);
    v.engine_ref_ = eng;
    return v;
}

script_value script_value::make_object(const std::string& type_name, std::shared_ptr<void> data, std::weak_ptr<engine> eng) {
    script_value v = make_object(type_name, data);
    v.engine_ref_ = eng;
    return v;
}

script_value script_value::make_cpp_object(const std::string& type_name, std::shared_ptr<void> data, std::weak_ptr<engine> eng) {
    script_value v = make_cpp_object(type_name, data);
    v.engine_ref_ = eng;
    return v;
}

script_value script_value::make_weak_ptr(const script_value& value, std::weak_ptr<engine> eng) {
    script_value v = make_weak_ptr(value);
    v.engine_ref_ = eng;
    return v;
}

script_value script_value::make_reference(script_value* target, const std::shared_ptr<environment>& env, std::weak_ptr<engine> eng) {
    script_value v = make_reference(target, env);
    v.engine_ref_ = eng;
    return v;
}

script_value script_value::make_function(const script_function& func, std::weak_ptr<engine> eng) {
    script_value v = make_function(func);
    v.engine_ref_ = eng;
    return v;
}

// Copy constructor (shallow copy for reference semantics)
script_value::script_value(const script_value& other) : type_info_(other.type_info_), engine_ref_(other.engine_ref_), storage_(other.storage_) {
    // Simple shallow copy - shares storage with the original, including engine reference
}

// Copy assignment operator (shallow copy for reference semantics)
script_value& script_value::operator=(const script_value& other) {
    if (this != &other) {
        type_info_ = other.type_info_;
        engine_ref_ = other.engine_ref_;
        storage_ = other.storage_;
    }
    return *this;
}

// Explicit deep copy method
script_value script_value::clone() const {
    if (engine_ref_.expired()) {
        throw runtime_error("Cannot clone script_value: missing engine reference");
    }
    script_value result(std::monostate{}, engine_ref_);  // Preserve engine reference!
    result.type_info_ = type_info_;
    
    // Handle deep copying for different types
    switch (type()) {
        case script_value_type::jai_array_type: {
            // Deep copy the array - each element is also cloned
            auto& other_array = *std::get<std::shared_ptr<std::vector<script_value>>>(storage_);
            auto new_array = std::make_shared<std::vector<script_value>>();
            new_array->reserve(other_array.size());
            for (const auto& elem : other_array) {
                new_array->push_back(elem.clone());
            }
            result.storage_ = new_array;
            break;
        }
        case script_value_type::jai_map_type: {
            // Deep copy the map - keys and values are cloned
            auto& other_map = *std::get<std::shared_ptr<std::map<script_value, script_value>>>(storage_);
            auto new_map = std::make_shared<std::map<script_value, script_value>>();
            for (const auto& [key, value] : other_map) {
                new_map->emplace(key.clone(), value.clone());
            }
            result.storage_ = new_map;
            break;
        }
        case script_value_type::jai_object_type: {
            // Deep copy for objects
            auto obj_holder = std::get<std::shared_ptr<object_holder>>(storage_);
            
            // Check if this is a class_instance that supports deep copy
            if (obj_holder->is_cpp_class_instance) {
                // This is a class_instance, safe to static_cast
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Use class_instance's deep_copy method
                auto new_instance = instance->deep_copy();
                
                // Create new object_holder
                auto new_holder = std::make_shared<object_holder>();
                new_holder->type_name = obj_holder->type_name;
                new_holder->data = new_instance;
                new_holder->is_cpp_class_instance = true;
                result.storage_ = new_holder;
            } else {
                // For raw C++ objects, use shallow copy for now
                // TODO: Access polymorphic type registry through engine for deep copy
                result.storage_ = storage_;
            }
            break;
        }
        case script_value_type::jai_reference_type: {
            // When cloning a reference, we want to clone the referenced value,
            // not create another reference to the same value
            return deref().clone();
        }
        default:
            // For primitive types and functions, shallow copy is fine
            result.storage_ = storage_;
            break;
    }
    
    return result;
}

const script_function& script_value::as_function() const {
    const script_value& val = deref();
    if (val.type() != script_value_type::jai_function_type) {
        throw runtime_error("script_value is not a function");
    }
    return std::get<script_function>(val.storage_);
}

std::string script_value::to_string() const {
    // Special handling for references to show what they point to
    if (type() == script_value_type::jai_reference_type) {
        return deref().to_string();
    }
    
    switch (type()) {
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
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<std::shared_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Don't recursively deref - just return the target
        // This prevents infinite recursion and matches C++ reference semantics
        return *refHolder->target;
    }
    return *this;
}

script_value& script_value::deref() {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<std::shared_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Don't recursively deref - just return the target
        // This prevents infinite recursion and matches C++ reference semantics
        return *refHolder->target;
    }
    return *this;
}

void script_value::assign_through(const script_value& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<std::shared_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Assign to the referenced value
        *refHolder->target = value;
    } else {
        // Not a reference, direct assignment
        *this = value;
    }
}

void script_value::assign_through(script_value&& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<std::shared_ptr<reference_holder>>(storage_);
        if (!refHolder || !refHolder->target) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        // Move assign to the referenced value
        *refHolder->target = std::move(value);
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
            return as_int() == other.as_int();
        case script_value_type::jai_float_type:
            return as_float() == other.as_float();
        case script_value_type::jai_string_type:
            return as_string() == other.as_string();
        case script_value_type::jai_char_type:
            return as_char() == other.as_char();
        case script_value_type::jai_bool_type:
            return as_bool() == other.as_bool();
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
            return as_int() <=> other.as_int();
        case script_value_type::jai_float_type:
            // script_float comparison returns partial_ordering, convert to strong
            if (auto cmp = as_float() <=> other.as_float(); cmp < 0)
                return std::strong_ordering::less;
            else if (cmp > 0)
                return std::strong_ordering::greater;
            else
                return std::strong_ordering::equal;
        case script_value_type::jai_string_type:
            return as_string() <=> other.as_string();
        case script_value_type::jai_char_type:
            return as_char() <=> other.as_char();
        case script_value_type::jai_bool_type:
            return as_bool() <=> other.as_bool();
        default:
            // For complex types, compare by address for now
            return &storage_ <=> &other.storage_;
    }
}


} // namespace jai