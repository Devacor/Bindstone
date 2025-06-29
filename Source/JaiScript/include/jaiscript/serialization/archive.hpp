#pragma once

#include "../core/value.hpp"
#include "../core/type_info.hpp"
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
    virtual ~archive_writer() = default;
    
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
            case value_type::jai_int_type:
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
                
            case value_type::jai_float_type:
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
    // Shared pointer tracking for deserialization
    std::unordered_map<uint32_t, script_value> id_to_shared_ptr_;  // ID -> reconstructed shared_ptr
    
    // Register a reconstructed shared_ptr
    void register_shared_ptr(uint32_t id, const script_value& ptr) {
        if (id != 0) {  // Don't register null
            id_to_shared_ptr_[id] = ptr;
        }
    }
    
    // Get previously deserialized shared_ptr by ID
    script_value get_shared_ptr(uint32_t id) const {
        if (id == 0) return script_value();  // null
        
        auto it = id_to_shared_ptr_.find(id);
        if (it != id_to_shared_ptr_.end()) {
            return it->second;
        }
        throw serialization_error("Invalid shared_ptr ID: " + std::to_string(id));
    }
    
public:
    virtual ~archive_reader() = default;
    
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
    
    // Check if property exists in current object
    virtual bool has_property(const std::string& name) = 0;
    
    // Generic value reading (determines type from stream)
    virtual script_value read_value() = 0;
    
    // Skip a property of known type (for deleted properties)
    template<typename T>
    void skip_property() {
        skip_property_impl(type_info::make<T>());
    }
    
    // Version being deserialized
    uint32_t get_version() const { return version_; }
    
    // Type-aware value reading
    script_value read_value_typed(const type_info_ptr& type_info) {
        if (!type_info) {
            // Fallback to runtime type detection
            return read_value();
        }
        
        // Use type info to read with proper size
        switch (type_info->base_type) {
            case value_type::jai_int_type:
                if (type_info->native_size == 1) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int8() : read_uint8()));
                } else if (type_info->native_size == 2) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int16() : read_uint16()));
                } else if (type_info->native_size == 4) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int32() : read_uint32()));
                } else if (type_info->native_size == 8 && !type_info->is_signed) {
                    return script_value(static_cast<script_int>(read_uint64()));
                } else {
                    return script_value(read_int64());
                }
                
            case value_type::jai_float_type:
                if (type_info->native_size == 4) {
                    return script_value(static_cast<script_float>(read_float32()));
                } else {
                    return script_value(read_float64());
                }
                
            default:
                return read_value();
        }
    }

protected:
    virtual void skip_property_impl(type_info_ptr type) = 0;
    uint32_t version_ = 0;
};

// Serialization metadata for properties
struct property_metadata {
    std::string name;
    type_info_ptr type;
    uint32_t version_added = 1;
    uint32_t version_removed = UINT32_MAX;
    bool is_deleted = false;
    
    // Network flags
    enum network_flags : uint32_t {
        none = 0,
        reliable = 1 << 0,
        unreliable = 1 << 1,
        immediate = 1 << 2,
        compress_position = 1 << 3,
        throttle_100ms = 1 << 4,
        throttle_500ms = 1 << 5
    };
    uint32_t network_flags = network_flags::none;
    
    bool is_active_in_version(uint32_t version) const {
        return version >= version_added && version < version_removed && !is_deleted;
    }
};

// Class serialization metadata
struct class_metadata {
    std::string class_name;
    uint32_t current_version = 1;
    std::vector<property_metadata> properties;
    std::map<uint32_t, std::vector<std::string>> version_property_lists;
    
    // Custom serialization functions
    std::function<void(archive_writer&, const script_value&, uint32_t)> custom_save;
    std::function<script_value(archive_reader&, uint32_t)> custom_construct;
    
    std::vector<std::string> get_properties_for_version(uint32_t version) const {
        auto it = version_property_lists.find(version);
        if (it != version_property_lists.end()) {
            return it->second;
        }
        
        // Default: all properties active in this version, in registration order
        std::vector<std::string> result;
        for (const auto& prop : properties) {
            if (prop.is_active_in_version(version)) {
                result.push_back(prop.name);
            }
        }
        return result;
    }
};

// Global registry for class serialization metadata
class serialization_registry {
public:
    static serialization_registry& instance() {
        static serialization_registry instance;
        return instance;
    }
    
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

// Helper functions for version tagging
struct version_added {
    uint32_t version;
    explicit version_added(uint32_t v) : version(v) {}
};

struct version_removed {
    uint32_t version;
    explicit version_removed(uint32_t v) : version(v) {}
};

struct network_flags {
    uint32_t flags;
    explicit network_flags(uint32_t f) : flags(f) {}
};

// Convenience functions
inline version_added added_v(uint32_t version) { return version_added(version); }
inline version_removed removed_v(uint32_t version) { return version_removed(version); }

} // namespace serialization
} // namespace jai