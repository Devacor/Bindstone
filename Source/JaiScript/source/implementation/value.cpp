#include <jaiscript/jaiscript.hpp>
#include <sstream>

namespace jai {

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


script_value script_value::make_reference(script_value* target, const std::shared_ptr<environment>& env) {
    if (!target) {
        throw runtime_error("Cannot create reference to null");
    }
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

script_value script_value::make_array(type_info_ptr element_type, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create array with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_array(element_type);

    // Create vector with small default capacity to avoid first few reallocations
    auto vec = make_strong<std::vector<script_value>>();
    vec->reserve(8);
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

    uint64_t type_id = eng->get_symbolizer()->intern(type_name);
    return make_object(type_name, type_id, data, eng);
}

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

script_value script_value::make_coroutine_handle(uint64_t type_id, std::shared_ptr<void> handle, engine* eng) {
    script_value v(std::monostate{}, eng);
    auto obj = make_strong<object_holder>();
    obj->type_name = "coroutine_handle";
    obj->type_id = type_id;
    obj->data = std::move(handle);
    obj->is_class_instance_wrapper = false;
    v.storage_ = obj;
    return v;
}

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
        auto holder = value.get_object_holder();
        if (!holder) {
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unsupported_operation),
                "Failed to get object_holder from script_value");
        }

        jai::weaker_ptr<object_holder> weak = holder;
        v.storage_ = weak;
    } else if (value.type() == script_value_type::jai_object_type) {
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

script_value script_value::make_element_reference(const strong_ptr<std::vector<script_value>>& container, size_t index,
                                                  const std::shared_ptr<environment>& env, engine* eng, type_info_ptr element_type) {
    if (!container || index >= container->size()) {
        throw runtime_error("Cannot create reference to out-of-range array element");
    }
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_reference((*container)[index].get_type_info());
    auto ref = make_strong<reference_holder>();
    ref->sourceEnv = env;
    ref->container_element_type = element_type;
    ref->container = container;          // owns the vector (keeps it alive) + enables re-resolve
    ref->container_index = index;
    // target is the element address AT CREATION time — valid for callers that resolve
    // and use the reference immediately (the assignment paths). deref()/assign_through()
    // ignore it in favor of container+index, which is recomputed each time and bounds-
    // checked, so a reference STORED across a reallocation (the #41 case) stays safe.
    ref->target = &(*container)[index];
    v.storage_ = ref;
    return v;
}

script_value script_value::make_field_reference(const std::shared_ptr<class_instance>& owner, uint64_t field_id,
                                                engine* eng, type_info_ptr field_type) {
    if (!owner) {
        throw runtime_error("Cannot create reference to null");
    }
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value* field_value = owner->find_field_value(field_id);
    if (!field_value) {
        throw runtime_error("Reference to a removed field");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_reference(field_value->get_type_info());
    auto ref = make_strong<reference_holder>();
    ref->owner_instance = owner;          // pins the instance for the reference's lifetime
    ref->field_id = field_id;
    ref->container_element_type = field_type;
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

script_value::script_value(const script_value& other)
    : type_info_(other.type_info_),
      engine_(other.engine_),
      storage_(other.storage_) {
    // The variant copy bumps the (immutable) binding box refcount, so copies of bound values remain aliases
    // NOTE: Raw engine* is much faster to copy than weak_ptr (no atomic ops)
}

// NOTE: Intentionally shallow; the interpreter handles cloning based on JaiScript value/reference semantics.
script_value& script_value::operator=(const script_value& other) {
    if (this != &other) {
        type_info_ = other.type_info_;
        engine_ = other.engine_;
        storage_ = other.storage_;
    }
    return *this;
}

script_value script_value::clone() const {
    if (!engine_) {
        throw runtime_error("Cannot clone script_value: missing engine pointer");
    }

    // shared_ptr<T> is a TYPE MARKER that indicates reference semantics for normal operations,
    // but clone() should perform a deep copy to create an independent instance
    if (type_info_ && type_info_->base_type == script_value_type::jai_shared_ptr_type) {
        auto obj_holder = std::get<strong_ptr<object_holder>>(storage_);
        if (!obj_holder) {
            throw runtime_error("Cannot clone shared_ptr: null object_holder");
        }

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

        throw runtime_error(
            "Cannot clone shared_ptr<" + obj_holder->type_name + ">: type is non-copyable. "
            "Register a copy constructor with dynamic_binder<" + obj_holder->type_name + ">::copy_constructor() "
            "to enable deep copying.");
    }

    script_value result(std::monostate{}, engine_);  // Preserve engine pointer!
    result.type_info_ = type_info_;

    // Use current_type() to check what's actually stored, not the declared type
    switch (current_type()) {
        case script_value_type::jai_array_type: {
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

            if (obj_holder->is_class_instance_wrapper) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                auto new_instance = instance->deep_copy();

                auto new_holder = make_strong<object_holder>();
                new_holder->type_name = obj_holder->type_name;
                new_holder->type_id = obj_holder->type_id;  // Preserve the cached type_id
                new_holder->data = new_instance;
                new_holder->is_class_instance_wrapper = true;
                result.storage_ = new_holder;
            } else {
                bool copied = false;
                if (engine_) {
                    auto class_def = engine_->get_class_definition(obj_holder->type_id);
                    if (class_def && class_def->has_copy_function()) {
                        // cpp_bound references own nothing (data null) - the live object is
                        // bound_ptr. Deep copy from it and drop the binding so the
                        // clone is an independent owned object, not an alias.
                        const void* copy_src = obj_holder->data ? obj_holder->data.get() : obj_holder->bound_ptr;
                        auto new_cpp_obj = copy_src ? class_def->copy_object(copy_src) : nullptr;
                        if (new_cpp_obj) {
                            auto new_holder = make_strong<object_holder>();
                            new_holder->type_name = obj_holder->type_name;
                            new_holder->type_id = obj_holder->type_id;
                            new_holder->data = new_cpp_obj;
                            new_holder->is_class_instance_wrapper = false;
                            result.storage_ = new_holder;  // fresh holder: bound_ptr stays null (clone detaches)
                            copied = true;
                        }
                    }
                }

                if (!copied) {
                    throw runtime_error(
                        "Cannot deep copy C++ object of type '" + obj_holder->type_name +
                        "'. Register a copy constructor with dynamic_binder<T>::copy_constructor() "
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
                // Assigning fresh storage inherently detaches the clone from the C++ variable
                if (is_int()) {
                    result.storage_ = as_int();
                } else if (is_float()) {
                    result.storage_ = as_float();
                } else if (is_bool()) {
                    result.storage_ = as_bool();
                } else if (is_char()) {
                    result.storage_ = as_char();
                } else if (is_string()) {
                    result.storage_ = make_strong<script_string>(as_string());
                } else {
                    // For other cpp_bound types, fall back to shallow copy (opaque bounds stay bound)
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

    auto class_def = engine_->get_class_definition(holder->type_id);
    if (!class_def || !class_def->is_transparent_wrapper()) {
        return *this;
    }

    script_value mutable_self = *this;
    script_value unwrapped = class_def->unwrap(mutable_self);
    if (!unwrapped.is_null()) {
        return unwrapped;
    }
    return *this;
}

std::string script_value::to_string() const {
    // Special handling for references to show what they point to
    if (current_type() == script_value_type::jai_reference_type) {
        return deref().to_string();
    }

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
        // Bind a reference to the held strong_ptr (do NOT copy it): copying bumps and
        // then drops the (non-atomic) refcount on every deref, which is pure overhead
        // on this hot path. We only read target/sourceEnv here.
        const auto& refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference");
        }
        if (refHolder->container) {
            // Vector-element reference: recompute the element address from container+index
            // each time so it survives reallocation (push), and bounds-check so a shrink
            // (pop/erase/clear) throws instead of reading freed memory (#41).
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Reference to a removed array element");
            }
            return (*refHolder->container)[refHolder->container_index].deref();
        }
        if (refHolder->owner_instance) {
            // Field reference: re-resolve by id (no lazy default insert, no sourceEnv
            // check - the pinned instance IS the lifetime)
            const script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            return field_value->deref();
        }
        if (!refHolder->target) {
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
        // See const overload: bind, don't copy, the strong_ptr (avoids refcount churn).
        auto& refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference");
        }
        if (refHolder->container) {
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Reference to a removed array element");
            }
            return (*refHolder->container)[refHolder->container_index].deref();
        }
        if (refHolder->owner_instance) {
            script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            return field_value->deref();
        }
        if (!refHolder->target) {
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
        if (!refHolder) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->container) {
            // Vector-element reference: resolve fresh + bounds-check before writing,
            // so a write-through after a reallocation/shrink can't corrupt the heap (#41).
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Assignment to a removed array element");
            }
            (*refHolder->container)[refHolder->container_index] = value;
            return;
        }
        if (refHolder->owner_instance) {
            script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            *field_value = value;
            return;
        }
        if (!refHolder->target) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        *refHolder->target = value;
    } else if (is_cpp_bound()) {
        void* boundPtr = bound_target_ptr();
        const uint8_t sizeAndSign = bound_size_and_sign();
        switch (type()) {
            case script_value_type::jai_int_type: {
                auto v = value.as<script_int>();
                const uint8_t size = sizeAndSign & 0x7F;
                if (sizeAndSign & 0x80) {
                    switch (size) {
                        case 1: *static_cast<uint8_t*>(boundPtr) = static_cast<uint8_t>(v); break;
                        case 2: *static_cast<uint16_t*>(boundPtr) = static_cast<uint16_t>(v); break;
                        case 4: *static_cast<uint32_t*>(boundPtr) = static_cast<uint32_t>(v); break;
                        default: *static_cast<uint64_t*>(boundPtr) = static_cast<uint64_t>(v); break;
                    }
                } else {
                    switch (size) {
                        case 1: *static_cast<int8_t*>(boundPtr) = static_cast<int8_t>(v); break;
                        case 2: *static_cast<int16_t*>(boundPtr) = static_cast<int16_t>(v); break;
                        case 4: *static_cast<int32_t*>(boundPtr) = static_cast<int32_t>(v); break;
                        default: *static_cast<script_int*>(boundPtr) = v; break;
                    }
                }
                break;
            }
            case script_value_type::jai_float_type:
                if ((sizeAndSign & 0x7F) == sizeof(float))
                    *static_cast<float*>(boundPtr) = static_cast<float>(value.as<script_float>());
                else
                    *static_cast<script_float*>(boundPtr) = value.as<script_float>();
                break;
            case script_value_type::jai_string_type:
                *static_cast<std::string*>(boundPtr) = value.as<script_string>();
                break;
            case script_value_type::jai_bool_type:
                *static_cast<bool*>(boundPtr) = value.as<script_bool>();
                break;
            case script_value_type::jai_char_type:
                *static_cast<char*>(boundPtr) = value.as<script_char>();
                break;
            default:
                // For complex types, we'll need to use the conversion registry
                throw runtime_error("assign_through not yet implemented for this cpp_bound type");
        }
    } else {
        *this = value;
    }
}

void script_value::assign_through(script_value&& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->container) {
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Assignment to a removed array element");
            }
            (*refHolder->container)[refHolder->container_index] = std::move(value);
            return;
        }
        if (refHolder->owner_instance) {
            script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            *field_value = std::move(value);
            return;
        }
        if (!refHolder->target) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->sourceEnv.expired()) {
            throw runtime_error("Reference target environment has been destroyed");
        }
        *refHolder->target = std::move(value);
    } else if (is_cpp_bound()) {
        // can't move into a C++ variable — fall back to copy
        assign_through(value);
    } else {
        *this = std::move(value);
    }
}

bool script_value::operator==(const script_value& other) const {
    const script_value& lhs = deref();
    const script_value& rhs = other.deref();

    if (lhs.type() != rhs.type()) {
        return false;
    }

    switch (lhs.type()) {
        case script_value_type::jai_null_type:
            return true;
        case script_value_type::jai_int_type:
            return lhs.unchecked_as_int() == rhs.unchecked_as_int();
        case script_value_type::jai_float_type:
            return lhs.unchecked_as_float() == rhs.unchecked_as_float();
        case script_value_type::jai_string_type:
            return lhs.unchecked_as_string() == rhs.unchecked_as_string();
        case script_value_type::jai_char_type:
            return lhs.unchecked_as_char() == rhs.unchecked_as_char();
        case script_value_type::jai_bool_type:
            return lhs.unchecked_as_bool() == rhs.unchecked_as_bool();
        default:
            return false;
    }
}

std::strong_ordering script_value::operator<=>(const script_value& other) const {
    if (auto cmp = type() <=> other.type(); cmp != 0) {
        return cmp;
    }
    switch (type()) {
        case script_value_type::jai_null_type:
            return std::strong_ordering::equal; // All nulls are equal
        case script_value_type::jai_int_type:
            return unchecked_as_int() <=> other.unchecked_as_int();
        case script_value_type::jai_float_type:
            // Use a TOTAL order over doubles (IEEE-754 totalOrder). The previous
            // code mapped every NaN comparison to `less`, which made NaN < NaN
            // true — an irreflexivity violation that is undefined behavior when a
            // float (or NaN) is used as a std::map key. std::strong_order makes
            // all NaNs compare equal to each other and orders them deterministically.
            return std::strong_order(unchecked_as_float(), other.unchecked_as_float());
        case script_value_type::jai_string_type:
            return unchecked_as_string() <=> other.unchecked_as_string();
        case script_value_type::jai_char_type:
            return unchecked_as_char() <=> other.unchecked_as_char();
        case script_value_type::jai_bool_type:
            return unchecked_as_bool() <=> other.unchecked_as_bool();
        default: {
            // Complex types (array/map/object/function/shared_ptr/reference): order
            // by the address of the HELD object, which is a STABLE identity for a
            // given value. The previous code compared &storage_ — the address of the
            // operand's own member — which is a transient property of the comparison,
            // not of the value, so the ordering of a logical key changed between
            // operations and violated std::map's strict-weak-ordering invariant
            // (corrupting maps with complex-typed keys). Use std::less for a
            // guaranteed total order over pointers.
            auto identity = [](const script_value& v) -> const void* {
                switch (v.raw_storage_index()) {
                    case TYPEID_ARRAY:      return std::get_if<TYPEID_ARRAY>(&v.storage_)->get();
                    case TYPEID_MAP:        return std::get_if<TYPEID_MAP>(&v.storage_)->get();
                    case TYPEID_OBJECT:     return std::get_if<TYPEID_OBJECT>(&v.storage_)->get();
                    case TYPEID_FUNCTION:   return std::get_if<TYPEID_FUNCTION>(&v.storage_)->get();
                    case TYPEID_SHARED_PTR: return std::get_if<TYPEID_SHARED_PTR>(&v.storage_)->get();  // holder identity; runtime-unreachable (alt 11 never constructed)
                    case TYPEID_REFERENCE:  return std::get_if<TYPEID_REFERENCE>(&v.storage_)->get();
                    default:                return nullptr;  // invalid/weak_ptr: treated as one equivalence class
                }
            };
            const void* lp = identity(*this);
            const void* rp = identity(other);
            std::less<const void*> ptr_less;
            if (ptr_less(lp, rp)) return std::strong_ordering::less;
            if (ptr_less(rp, lp)) return std::strong_ordering::greater;
            return std::strong_ordering::equal;
        }
    }
}


} // namespace jai