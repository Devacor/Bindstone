#pragma once

#ifndef JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_ARCHIVE_HPP_INCLUDED

#include <jaiscript/serialization/serialization_metadata.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/engine.hpp>
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace jai {

// Serialization-specific exception for better error categorization
class serialization_error : public runtime_error {
public:
    explicit serialization_error(const std::string& message) : runtime_error(message) {}
    explicit serialization_error(const char* message) : runtime_error(message) {}
    
    // Enhanced constructor with context information
    serialization_error(const std::string& message, const std::string& property_name, const std::string& class_name = "")
        : runtime_error(build_message(message, property_name, class_name)) {}

private:
    static std::string build_message(const std::string& message, const std::string& property_name, const std::string& class_name) {
        std::string result = "Serialization error: " + message;
        if (!property_name.empty()) {
            result += " (property: " + property_name;
            if (!class_name.empty()) {
                result += " in class: " + class_name;
            }
            result += ")";
        }
        return result;
    }
};

}

namespace jai {
namespace serialization {

// Forward declarations
class binary_archive_writer;
class binary_archive_reader;
class json_archive_writer;
class json_archive_reader;

// Archive writer interface
class archive_writer {
protected:
    // Engine reference for creating script_values during serialization
    std::weak_ptr<engine> engine_ref_;

    // Shared pointer tracking to prevent duplicate serialization
    std::unordered_map<const void*, uint32_t> shared_ptr_ids_;
    uint32_t next_shared_id_ = 1;  // 0 reserved for null

    // Check if a shared_ptr has been serialized before
    // Returns: pair<id, is_new> where is_new indicates if this is the first time
    std::pair<uint32_t, bool> track_shared_ptr(const void* raw_ptr) {
        if (raw_ptr == nullptr) {
            return {0, false};  // null pointer
        }

        auto it = shared_ptr_ids_.find(raw_ptr);
        if (it != shared_ptr_ids_.end()) {
            return {it->second, false};  // Already seen
        }

        // New shared_ptr
        uint32_t id = next_shared_id_++;
        shared_ptr_ids_[raw_ptr] = id;
        return {id, true};
    }

public:
    // Constructor that accepts engine reference
    explicit archive_writer(std::weak_ptr<engine> eng = {}) : engine_ref_(eng) {}

    virtual ~archive_writer() = default;

    // Set engine reference
    void set_engine(std::weak_ptr<engine> eng) {
        engine_ref_ = eng;
    }

    // Get engine reference
    std::weak_ptr<engine> get_engine() const {
        return engine_ref_;
    }

    // Does this archive format require explicit property keys array?
    // Binary: true (no named fields), JSON: false (has object keys)
    virtual bool needs_property_keys() const = 0;

    // Basic type serialization
    virtual void write_int8(int8_t value) = 0;
    virtual void write_int16(int16_t value) = 0;
    virtual void write_int32(int32_t value) = 0;
    virtual void write_int64(int64_t value) = 0;
    virtual void write_uint8(uint8_t value) = 0;
    virtual void write_uint16(uint16_t value) = 0;
    virtual void write_uint32(uint32_t value) = 0;
    virtual void write_uint64(uint64_t value) = 0;
    virtual void write_float32(float value) = 0;
    virtual void write_float64(double value) = 0;
    virtual void write_bool(bool value) = 0;
    virtual void write_string(const std::string& value) = 0;
    virtual void write_binary(const void* data, size_t size) = 0;
    
    // Object/array structure
    virtual void begin_object(const std::string& type_name, uint32_t version) = 0;
    virtual void end_object() = 0;
    virtual void begin_array(size_t size) = 0;
    virtual void end_array() = 0;
    virtual void write_property_name(const std::string& name) = 0;

    // Map serialization (format-specific: JSON uses object, binary uses array)
    virtual void begin_map(size_t size) = 0;
    virtual void end_map() = 0;
    virtual void write_map_key(const std::string& key) = 0;

    // Generic value writing (determines type at runtime)
    virtual void write_value(const script_value& value) = 0;
    
    // Conditional serialization state
    void set_property_enabled(const std::string& property_name, bool enabled) {
        property_enabled_[property_name] = enabled;
    }
    
    bool is_property_enabled(const std::string& property_name) const {
        auto it = property_enabled_.find(property_name);
        return it != property_enabled_.end() ? it->second : true;
    }
    
    // Version being serialized
    void set_version(uint32_t version) { version_ = version; }
    uint32_t get_version() const { return version_; }
    
    // Type-aware value writing
    void write_value_typed(const script_value& value, const type_info_ptr& type_info) {
        if (!type_info) {
            // Fallback to runtime type
            write_value(value);
            return;
        }
        
        // Use type info to write with proper size
        switch (type_info->base_type) {
            case script_value_type::jai_int_type:
                if (type_info->native_size == 1) {
                    write_int8(static_cast<int8_t>(value.as<script_int>()));
                } else if (type_info->native_size == 2) {
                    write_int16(static_cast<int16_t>(value.as<script_int>()));
                } else if (type_info->native_size == 4) {
                    write_int32(static_cast<int32_t>(value.as<script_int>()));
                } else {
                    write_int64(value.as<script_int>());
                }
                break;
                
            case script_value_type::jai_float_type:
                if (type_info->native_size == 4) {
                    write_float32(static_cast<float>(value.as<script_float>()));
                } else {
                    write_float64(value.as<script_float>());
                }
                break;
                
            default:
                write_value(value);
                break;
        }
    }

protected:
    std::map<std::string, bool> property_enabled_;
    uint32_t version_ = 0;
};

// Archive reader interface
class archive_reader {
protected:
    // Engine reference for creating script_values
    std::weak_ptr<engine> engine_ref_;

    // Shared pointer tracking for deserialization
    std::unordered_map<uint32_t, script_value> id_to_shared_ptr_;  // ID -> reconstructed shared_ptr

    // User context storage (for dependency injection during deserialization)
    std::map<std::type_index, void*> user_contexts_;

    // Pre-read properties for factory-based deserialization
    // In binary format, all property names come first, then all values
    // This map stores pre-read properties so factories can access them
    std::map<std::string, script_value> preread_properties_;
    bool has_preread_properties_ = false;
    
    // Register a reconstructed shared_ptr
    void register_shared_ptr(uint32_t id, const script_value& ptr) {
        if (id != 0) {  // Don't register null
            id_to_shared_ptr_.insert_or_assign(id, ptr);
        }
    }
    
    // Get previously deserialized shared_ptr by ID
    script_value get_shared_ptr(uint32_t id) const {
        if (id == 0) {
            if (auto eng = engine_ref_.lock()) {
                return script_value(std::monostate{}, eng->weak_from_this());
            }
            throw serialization_error("Engine reference expired during deserialization");
        }
        
        auto it = id_to_shared_ptr_.find(id);
        if (it != id_to_shared_ptr_.end()) {
            return it->second;
        }
        throw serialization_error("Invalid shared_ptr ID: " + std::to_string(id));
    }
    
public:
    // Constructor that accepts engine reference
    // Engine is REQUIRED for readers since they create script_values during deserialization
    explicit archive_reader(std::weak_ptr<engine> eng) : engine_ref_(eng) {}

    virtual ~archive_reader() = default;
    
    // Set engine reference (for readers that can't pass it in constructor)
    void set_engine(std::weak_ptr<engine> eng) {
        engine_ref_ = eng;
    }

    // Get engine reference
    std::weak_ptr<engine> get_engine() const {
        return engine_ref_;
    }

    // Does this archive format require explicit property keys array?
    // Binary: true (no named fields), JSON: false (has object keys)
    virtual bool needs_property_keys() const = 0;

    // User context support (similar to Cereal's get_user_data)
    template<typename ContextType>
    ContextType* get_user_context() const {
        auto it = user_contexts_.find(std::type_index(typeid(ContextType)));
        if (it != user_contexts_.end()) {
            return static_cast<ContextType*>(it->second);
        }
        return nullptr;
    }

    template<typename ContextType>
    void set_user_context(ContextType* context) {
        user_contexts_[std::type_index(typeid(ContextType))] = context;
    }

    // Basic type deserialization
    virtual int8_t read_int8() = 0;
    virtual int16_t read_int16() = 0;
    virtual int32_t read_int32() = 0;
    virtual int64_t read_int64() = 0;
    virtual uint8_t read_uint8() = 0;
    virtual uint16_t read_uint16() = 0;
    virtual uint32_t read_uint32() = 0;
    virtual uint64_t read_uint64() = 0;
    virtual float read_float32() = 0;
    virtual double read_float64() = 0;
    virtual bool read_bool() = 0;
    virtual std::string read_string() = 0;
    virtual std::vector<uint8_t> read_binary(size_t size) = 0;
    
    // Object/array structure
    virtual bool begin_object(std::string& type_name, uint32_t& version) = 0;
    virtual void end_object() = 0;
    virtual size_t begin_array() = 0;
    virtual void end_array() = 0;
    virtual bool read_property_name(std::string& name) = 0;

    // Map deserialization (format-specific: JSON uses object, binary uses array)
    virtual size_t begin_map() = 0;
    virtual void end_map() = 0;
    virtual bool read_map_key(std::string& key) = 0;

    // Check if property exists in current object
    virtual bool has_property(const std::string& name) = 0;

    // Generic value reading (determines type from stream)
    virtual script_value read_value() = 0;

    // Set pre-read properties (called by binary_archive_reader before factory invocation)
    void set_preread_properties(std::map<std::string, script_value> props) {
        preread_properties_ = std::move(props);
        has_preread_properties_ = true;
    }

    // Clear pre-read properties (called after factory completes)
    void clear_preread_properties() {
        preread_properties_.clear();
        has_preread_properties_ = false;
    }

    // Get a pre-read property (returns nullptr if not found)
    const script_value* get_preread_property(const std::string& name) const {
        auto it = preread_properties_.find(name);
        return it != preread_properties_.end() ? &it->second : nullptr;
    }

    // Check if we have pre-read properties available
    bool has_preread_properties() const { return has_preread_properties_; }

    // Read a specific property by name and convert to C++ type
    // Used for property pre-reading before construction
    // When pre-read properties are available (binary format), reads from the pre-read map
    // Otherwise (JSON format), reads from the stream directly
    template<typename T>
    T read_property(const std::string& property_name) {
        // Helper lambda to convert script_value to T
        auto convert_value = [](const script_value& value) -> T {
            if constexpr (std::is_same_v<T, std::string>) {
                return value.as<std::string>();
            } else if constexpr (std::is_integral_v<T>) {
                return static_cast<T>(value.as<script_int>());
            } else if constexpr (std::is_floating_point_v<T>) {
                return static_cast<T>(value.as<script_float>());
            } else if constexpr (std::is_same_v<T, bool>) {
                return value.as<bool>();
            } else {
                // For complex types, assume they can be extracted directly
                // This will work for shared_ptr<T> if value holds an object
                return value.as<T>();
            }
        };

        // Check if we have pre-read properties (binary format)
        if (has_preread_properties_) {
            auto it = preread_properties_.find(property_name);
            if (it == preread_properties_.end()) {
                throw serialization_error("Property '" + property_name + "' not found in pre-read properties");
            }
            return convert_value(it->second);
        } else {
            // Fall back to reading from stream (JSON format)
            std::string name;
            if (!read_property_name(name) || name != property_name) {
                throw serialization_error("Expected property '" + property_name + "' but found '" + name + "'");
            }
            return convert_value(read_value());
        }
    }

    // Version being deserialized
    uint32_t get_version() const { return version_; }
    
    // Type-aware value reading
    script_value read_value_typed(const type_info_ptr& type_info) {
        if (!type_info) {
            // Fallback to runtime type detection
            return read_value();
        }
        
        // Get engine reference for creating script_values
        auto eng = engine_ref_.lock();
        if (!eng) {
            throw serialization_error("Engine reference expired during deserialization");
        }
        
        // Use type info to read with proper size
        switch (type_info->base_type) {
            case script_value_type::jai_int_type:
                if (type_info->native_size == 1) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int8() : read_uint8()), eng->weak_from_this());
                } else if (type_info->native_size == 2) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int16() : read_uint16()), eng->weak_from_this());
                } else if (type_info->native_size == 4) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int32() : read_uint32()), eng->weak_from_this());
                } else if (type_info->native_size == 8 && !type_info->is_signed) {
                    return script_value(static_cast<script_int>(read_uint64()), eng->weak_from_this());
                } else {
                    return script_value(read_int64(), eng->weak_from_this());
                }
                
            case script_value_type::jai_float_type:
                if (type_info->native_size == 4) {
                    return script_value(static_cast<script_float>(read_float32()), eng->weak_from_this());
                } else {
                    return script_value(read_float64(), eng->weak_from_this());
                }
                
            default:
                return read_value();
        }
    }

protected:
    uint32_t version_ = 0;
};

// Registry for class serialization metadata - now tied to engine instance
class serialization_registry {
public:
    serialization_registry() = default;

    void register_class(const std::string& class_name, const class_metadata& metadata) {
        classes_[class_name] = metadata;
    }

    const class_metadata* get_class_metadata(const std::string& class_name) const {
        auto it = classes_.find(class_name);
        return it != classes_.end() ? &it->second : nullptr;
    }

    bool has_class(const std::string& class_name) const {
        return classes_.find(class_name) != classes_.end();
    }

private:
    std::map<std::string, class_metadata> classes_;
};

} // namespace serialization
} // namespace jai
#endif // JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
