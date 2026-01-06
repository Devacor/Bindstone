#pragma once

#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/properties/property_owner.hpp>
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace jai {
namespace serialization {

// Forward declarations
class writer;
class reader;

// Builder-style serialization writer
// Usage:
//   With engine (automatic type name and version lookup):
//     writer w(engine);
//     w(obj);  // Automatically looks up type and version
//     auto data = w.save();
//
//   Without engine (manual specification required):
//     writer w;
//     w(obj, "TypeName", 1);
//     auto data = w.save();
//
//   Multiple objects:
//     writer w(engine);
//     w(obj1);
//     w(obj2);
//     auto data = w.save();  // Automatically wraps in array
class writer {
public:
    // Constructor with optional engine for automatic type lookup
    explicit writer(engine* eng = nullptr, bool use_binary = false, int indent = 2)
        : engine_ref_(eng)
        , use_binary_(use_binary)
        , indent_(indent)
    {
    }

    // Overload for shared_ptr convenience
    explicit writer(std::shared_ptr<engine> eng, bool use_binary = false, int indent = 2)
        : writer(eng.get(), use_binary, indent)
    {
    }

    // Overload for engine reference
    explicit writer(engine& eng, bool use_binary = false, int indent = 2)
        : writer(&eng, use_binary, indent)
    {
    }

    // Accumulate an object to serialize (with automatic type/version lookup)
    // Works with any class registered via dynamic_binder
    template<typename T>
    writer& operator()(const T& obj) {
        // Try to look up metadata from engine
        std::string type_name;
        uint32_t version = 1;

        if (engine_ref_) {
            auto eng = engine_ref_;
            // Get the type name registered with dynamic_binder
            auto type_id = type_info::id<T>();
            auto registered_name = eng->get_type_name(type_id);

            if (!registered_name.empty()) {
                type_name = registered_name;

                // Look up version from serialization registry
                const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);
                if (metadata) {
                    version = metadata->current_version;
                }
            }
        }

        // Fall back to C++ type name if not registered
        if (type_name.empty()) {
            type_name = typeid(T).name();
        }

        return (*this)(obj, type_name, version);
    }

    // Accumulate an object with explicit type name and version
    // Works with any class registered via dynamic_binder
    template<typename T>
    writer& operator()(const T& obj, const std::string& type_name, uint32_t version = 1) {
        // Serialize immediately to avoid lifetime issues
        std::string serialized;

        if (use_binary_) {
            binary_archive_writer ar;
            ar.begin_object(type_name, version);
            serialize_object(obj, ar);
            ar.end_object();
            serialized = ar.str();
        } else {
            json_archive_writer ar(indent_);
            ar.begin_object(type_name, version);
            serialize_object(obj, ar);
            ar.end_object();
            serialized = ar.str();
        }

        serialized_objects_.push_back(std::move(serialized));
        return *this;
    }

private:
    // Serialize any object registered with dynamic_binder
    // Uses dynamic_binder metadata as the single source of truth
    template<typename T>
    void serialize_object(const T& obj, archive_writer& ar) {
        if (!engine_ref_) {
            // Fallback for property_owner without engine
            if constexpr (std::is_base_of_v<property_owner, T>) {
                obj.property_mgr.save(ar);
                return;
            } else {
                throw serialization_error("Engine required to serialize dynamic_binder objects");
            }
        }

        auto eng = engine_ref_;

        // Get the class metadata from dynamic_binder registry
        auto type_id = type_info::id<T>();
        auto type_name = eng->get_type_name(type_id);

        if (type_name.empty()) {
            // Not registered with dynamic_binder - fall back to property_mgr if available
            if constexpr (std::is_base_of_v<property_owner, T>) {
                obj.property_mgr.save(ar);
                return;
            } else {
                throw serialization_error("Type not registered with dynamic_binder");
            }
        }

        const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);
        if (!metadata || metadata->properties.empty()) {
            // No metadata - fall back to property_mgr if available
            if constexpr (std::is_base_of_v<property_owner, T>) {
                obj.property_mgr.save(ar);
                return;
            } else {
                throw serialization_error("No serialization metadata found for type: " + type_name);
            }
        }

        // Get the class definition
        auto class_def = eng->get_class(type_name);
        if (!class_def) {
            throw serialization_error("Class definition not found for type: " + type_name);
        }

        // Wrap the C++ object as a script_value for getter calls
        auto wrapped = script_value::make_shared(std::make_shared<T>(obj), eng);

        // Serialize each property using dynamic_binder registered getters
        for (const auto& prop_meta : metadata->properties) {
            // Skip read-only properties (computed values, no setter)
            if (prop_meta.read_only) {
                continue;
            }

            // Get the getter method (_get_propertyname)
            auto getter = class_def->get_method("_get_" + prop_meta.name);
            if (getter.type() == script_value_type::jai_function_type) {
                // Call getter with wrapped object
                std::vector<script_value> args = {wrapped};
                auto result = getter.as_function()(args);

                if (result) {
                    // Write property name and value
                    ar.write_property_name(prop_meta.name);
                    ar.write_value(result.value());
                }
            }
        }
    }

public:

    // Save and return the serialized data
    // Automatically wraps multiple objects in an array
    std::string save() {
        if (serialized_objects_.empty()) {
            return use_binary_ ? std::string() : "null";
        }

        // Single object: return directly
        if (serialized_objects_.size() == 1) {
            return serialized_objects_[0];
        }

        // Multiple objects: need to wrap in array
        // For JSON, we need to manually construct the array
        // For binary, we need to reconstruct from individual objects
        if (use_binary_) {
            // Binary format is more complex - for now just return the first object
            // Full implementation would need to deserialize and re-serialize
            throw serialization_error("Multiple object binary serialization not yet fully implemented. Use single-object mode or JSON format.");
        } else {
            // JSON: manually construct array
            std::string result = "[";
            for (size_t i = 0; i < serialized_objects_.size(); ++i) {
                if (i > 0) result += ",";
                if (indent_ > 0) result += "\n";
                result += serialized_objects_[i];
            }
            if (indent_ > 0) result += "\n";
            result += "]";
            return result;
        }
    }

private:
    engine* engine_ref_;
    bool use_binary_;
    int indent_;
    std::vector<std::string> serialized_objects_;
};

// Builder-style deserialization reader
// Usage:
//   reader r(engine_ref, data);
//   r(obj1);
//   r(obj2);
class reader {
public:
    // Constructor takes engine and serialized data
    reader(engine* eng, const std::string& data, bool is_binary = false)
        : engine_(eng)
        , is_binary_(is_binary)
    {
        if (is_binary_) {
            binary_reader_ = std::make_unique<binary_archive_reader>(data, engine_);
        } else {
            json_reader_ = std::make_unique<json_archive_reader>(data, engine_);
        }
    }

    // Overload for shared_ptr convenience
    reader(std::shared_ptr<engine> engine_ptr, const std::string& data, bool is_binary = false)
        : reader(engine_ptr.get(), data, is_binary)
    {
    }

    // Overload for engine reference
    reader(engine& eng, const std::string& data, bool is_binary = false)
        : reader(&eng, data, is_binary)
    {
    }

    // Read into a property_owner object
    template<typename T>
    std::enable_if_t<std::is_base_of_v<property_owner, T>, reader&>
    operator()(T& obj) {
        std::string type_name;
        uint32_t version;

        if (is_binary_) {
            binary_reader_->begin_object(type_name, version);
            obj.property_mgr.load(*binary_reader_);
            obj.post_deserialize(*binary_reader_);
            binary_reader_->end_object();
        } else {
            json_reader_->begin_object(type_name, version);
            obj.property_mgr.load(*json_reader_);
            obj.post_deserialize(*json_reader_);
            json_reader_->end_object();
        }

        return *this;
    }

    // Read a script_value
    reader& operator()(script_value& value) {
        if (is_binary_) {
            value = binary_reader_->read_value();
        } else {
            value = json_reader_->read_value();
        }
        return *this;
    }

    // Get the underlying archive reader for advanced use cases
    archive_reader& get_archive() {
        return is_binary_ ? static_cast<archive_reader&>(*binary_reader_)
                          : static_cast<archive_reader&>(*json_reader_);
    }

    // Set user context for custom deserialization factories
    template<typename T>
    void set_user_context(T* context) {
        if (is_binary_) {
            binary_reader_->set_user_context(context);
        } else {
            json_reader_->set_user_context(context);
        }
    }

private:
    engine* engine_;
    bool is_binary_;
    std::unique_ptr<json_archive_reader> json_reader_;
    std::unique_ptr<binary_archive_reader> binary_reader_;
};

// Convenience functions for single-object serialization

// With automatic type/version lookup from engine
// Works with any class registered via dynamic_binder
template<typename T>
std::string to_json(engine& eng, const T& obj, int indent = 2) {
    writer w(eng, false, indent);
    w(obj);  // Automatic lookup
    return w.save();
}

template<typename T>
std::string to_json(std::shared_ptr<engine> eng, const T& obj, int indent = 2) {
    return to_json(*eng, obj, indent);
}

template<typename T>
std::string to_binary(engine& eng, const T& obj) {
    writer w(eng, true);
    w(obj);  // Automatic lookup
    return w.save();
}

template<typename T>
std::string to_binary(std::shared_ptr<engine> eng, const T& obj) {
    return to_binary(*eng, obj);
}

// Convenience overloads for property_owner (gets engine from object)
// Only works with property_owner classes that have been bound to an engine
template<typename T>
std::enable_if_t<std::is_base_of_v<property_owner, T>, std::string>
to_json(const T& obj, int indent = 2) {
    auto eng_weak = obj.get_engine();
    auto eng = eng_weak.lock();
    if (!eng) {
        throw serialization_error("property_owner object must be bound to an engine for serialization. Use to_json(engine, obj) instead.");
    }
    return to_json(*eng, obj, indent);
}

template<typename T>
std::enable_if_t<std::is_base_of_v<property_owner, T>, std::string>
to_binary(const T& obj) {
    auto eng_weak = obj.get_engine();
    auto eng = eng_weak.lock();
    if (!eng) {
        throw serialization_error("property_owner object must be bound to an engine for serialization. Use to_binary(engine, obj) instead.");
    }
    return to_binary(*eng, obj);
}

// Deserialization

// With engine (recommended)
template<typename T>
void from_json(engine& eng, T& obj, const std::string& json) {
    reader r(&eng, json, false);
    r(obj);
}

template<typename T>
void from_json(std::shared_ptr<engine> eng, T& obj, const std::string& json) {
    from_json(*eng, obj, json);
}

template<typename T>
void from_binary(engine& eng, T& obj, const std::string& data) {
    reader r(&eng, data, true);
    r(obj);
}

template<typename T>
void from_binary(std::shared_ptr<engine> eng, T& obj, const std::string& data) {
    from_binary(*eng, obj, data);
}

// Convenience overloads for property_owner (gets engine from object)
template<typename T>
std::enable_if_t<std::is_base_of_v<property_owner, T>>
from_json(T& obj, const std::string& json) {
    auto eng_weak = obj.get_engine();
    auto eng = eng_weak.lock();
    if (!eng) {
        throw serialization_error("property_owner object must be bound to an engine for deserialization. Use from_json(engine, obj, json) instead.");
    }
    reader r(eng.get(), json, false);
    r(obj);
}

template<typename T>
std::enable_if_t<std::is_base_of_v<property_owner, T>>
from_binary(T& obj, const std::string& data) {
    auto eng_weak = obj.get_engine();
    auto eng = eng_weak.lock();
    if (!eng) {
        throw serialization_error("property_owner object must be bound to an engine for deserialization. Use from_binary(engine, obj, data) instead.");
    }
    reader r(eng.get(), data, true);
    r(obj);
}

// Context object support for non-default constructors with external dependencies
// Similar to Cereal's UserDataAdapter pattern
// The context object can be retrieved in custom deserialization factories via archive_reader::get_user_context<T>()

template<typename T, typename Context>
void from_json(engine& eng, T& obj, const std::string& json, Context& context) {
    reader r(&eng, json, false);
    r.set_user_context(&context);
    r(obj);
}

template<typename T, typename Context>
void from_json(std::shared_ptr<engine> eng, T& obj, const std::string& json, Context& context) {
    from_json(*eng, obj, json, context);
}

template<typename T, typename Context>
void from_binary(engine& eng, T& obj, const std::string& data, Context& context) {
    reader r(&eng, data, true);
    r.set_user_context(&context);
    r(obj);
}

template<typename T, typename Context>
void from_binary(std::shared_ptr<engine> eng, T& obj, const std::string& data, Context& context) {
    from_binary(*eng, obj, data, context);
}

// Context object support for property_owner convenience overloads
template<typename T, typename Context>
std::enable_if_t<std::is_base_of_v<property_owner, T>>
from_json(T& obj, const std::string& json, Context& context) {
    auto eng_weak = obj.get_engine();
    auto eng = eng_weak.lock();
    if (!eng) {
        throw serialization_error("property_owner object must be bound to an engine for deserialization. Use from_json(engine, obj, json, context) instead.");
    }
    reader r(eng.get(), json, false);
    r.set_user_context(&context);
    r(obj);
}

template<typename T, typename Context>
std::enable_if_t<std::is_base_of_v<property_owner, T>>
from_binary(T& obj, const std::string& data, Context& context) {
    auto eng_weak = obj.get_engine();
    auto eng = eng_weak.lock();
    if (!eng) {
        throw serialization_error("property_owner object must be bound to an engine for deserialization. Use from_binary(engine, obj, data, context) instead.");
    }
    reader r(eng.get(), data, true);
    r.set_user_context(&context);
    r(obj);
}

} // namespace serialization
} // namespace jai
