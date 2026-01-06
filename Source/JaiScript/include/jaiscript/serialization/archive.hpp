#pragma once

#ifndef JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_ARCHIVE_HPP_INCLUDED

// ============================================================
// Configurable limits - define these before including to override
// ============================================================

// Maximum serialization depth (default: 1000)
// This is high enough for complex nested structures but prevents stack overflow
// from maliciously crafted data or circular references
#ifndef JAI_MAX_SERIALIZATION_DEPTH
#define JAI_MAX_SERIALIZATION_DEPTH 1000
#endif

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

// Base tag type for all JaiScript archives (used for SFINAE disambiguation from cereal)
struct archive_base {};

// Archive writer interface
class archive_writer : public archive_base {
protected:
    // Engine reference for creating script_values during serialization
    engine* engine_ref_;

    // Shared pointer tracking to prevent duplicate serialization
    std::unordered_map<const void*, uint32_t> shared_ptr_ids_;
    uint32_t next_shared_id_ = 1;  // 0 reserved for null

    // Depth tracking for recursion limit
    int current_depth_ = 0;

    // RAII guard for depth tracking - throws on depth exceeded
    struct depth_guard {
        int& depth;
        bool acquired = false;

        depth_guard(int& d) : depth(d) {
            if (depth >= JAI_MAX_SERIALIZATION_DEPTH) {
                throw serialization_error(
                    "Maximum serialization depth (" + std::to_string(JAI_MAX_SERIALIZATION_DEPTH) +
                    ") exceeded - possible circular reference or deeply nested structure"
                );
            }
            ++depth;
            acquired = true;
        }

        ~depth_guard() {
            if (acquired) --depth;
        }

        // Non-copyable
        depth_guard(const depth_guard&) = delete;
        depth_guard& operator=(const depth_guard&) = delete;
    };

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
    // Constructor that accepts engine pointer
    explicit archive_writer(engine* eng = nullptr) : engine_ref_(eng) {}

    virtual ~archive_writer() = default;

    // Set engine reference
    void set_engine(engine* eng) {
        engine_ref_ = eng;
    }

    // Get engine reference
    engine* get_engine() const {
        return engine_ref_;
    }

    // ============================================================================
    // Shared pointer ID tracking (for de-duplication)
    // ============================================================================
    // Call get_or_assign_shared_id to track a shared_ptr. Returns {id, is_new}.
    // - If is_new is true, serialize the full object
    // - If is_new is false, just write the ID (object was already serialized)
    // - ID 0 means null pointer
    std::pair<uint32_t, bool> get_or_assign_shared_id(const void* raw_ptr) {
        return track_shared_ptr(raw_ptr);
    }

    // Check if a pointer has already been assigned an ID without assigning a new one
    // Returns 0 if not found (pointer not yet serialized)
    uint32_t lookup_shared_id(const void* raw_ptr) const {
        if (raw_ptr == nullptr) return 0;
        auto it = shared_ptr_ids_.find(raw_ptr);
        return it != shared_ptr_ids_.end() ? it->second : 0;
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
    
    // Version being serialized
    void set_version(uint32_t version) { version_ = version; }
    uint32_t get_version() const { return version_; }

    // ============================================================================
    // Public API: serialize("name", value) - always use named properties
    // ============================================================================
    // Usage in your type's save/load functions:
    //   void save(archive_writer& ar) const {
    //       ar.serialize("x", x);
    //       ar.serialize("y", y);
    //   }

    // Serialize primitives (named)
    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_same_v<T, std::string>>
    serialize(const char* name, const T& value) {
        write_property_name(name);
        write_primitive(value);
    }

    // Serialize custom types (named) - calls T::save(archive) or save(archive, T) via ADL
    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>>
    serialize(const char* name, const T& value) {
        write_property_name(name);
        write_custom(value);
    }

    // operator() as alias for serialize() - enables ar("name", value) syntax
    template<typename T>
    void operator()(const char* name, const T& value) {
        serialize(name, value);
    }

protected:
    std::map<std::string, bool> property_enabled_;
    uint32_t version_ = 0;

private:
    // Helper: write primitive value (type dispatch)
    template<typename T>
    void write_primitive(const T& value) {
        if constexpr (std::is_same_v<T, int8_t>) write_int8(value);
        else if constexpr (std::is_same_v<T, int16_t>) write_int16(value);
        else if constexpr (std::is_same_v<T, int32_t>) write_int32(value);
        else if constexpr (std::is_same_v<T, int64_t>) write_int64(value);
        else if constexpr (std::is_same_v<T, uint8_t>) write_uint8(value);
        else if constexpr (std::is_same_v<T, uint16_t>) write_uint16(value);
        else if constexpr (std::is_same_v<T, uint32_t>) write_uint32(value);
        else if constexpr (std::is_same_v<T, uint64_t>) write_uint64(value);
        else if constexpr (std::is_same_v<T, float>) write_float32(value);
        else if constexpr (std::is_same_v<T, double>) write_float64(value);
        else if constexpr (std::is_same_v<T, bool>) write_bool(value);
        else if constexpr (std::is_same_v<T, std::string>) write_string(value);
        else if constexpr (std::is_integral_v<T>) write_int64(static_cast<int64_t>(value));
        else if constexpr (std::is_floating_point_v<T>) write_float64(static_cast<double>(value));
    }

    // Helper: write custom type (ADL dispatch)
    template<typename T>
    void write_custom(const T& value) {
        // Try member save first, then ADL free save
        if constexpr (requires { const_cast<T&>(value).save(*this); }) {
            const_cast<T&>(value).save(*this);
        } else {
            // ADL lookup for free function save(archive_writer&, const T&)
            save(*this, value);
        }
    }
};

// Archive reader interface
class archive_reader : public archive_base {
protected:
    // Engine reference for creating script_values
    engine* engine_ref_;

    // Shared pointer tracking for deserialization (script_value based)
    std::unordered_map<uint32_t, script_value> id_to_shared_ptr_;  // ID -> reconstructed shared_ptr

    // C++ shared_ptr tracking for property serialization (type-erased)
    // Used by property_serialization for weak_ptr reconstruction
    std::unordered_map<uint32_t, std::shared_ptr<void>> cpp_shared_ptrs_;

    // User context storage (for dependency injection during deserialization)
    std::map<std::type_index, void*> user_contexts_;

    // Pre-read properties for factory-based deserialization
    // In binary format, all property names come first, then all values
    // This map stores pre-read properties so factories can access them
    std::map<std::string, script_value> preread_properties_;
    bool has_preread_properties_ = false;

    // Depth tracking for recursion limit
    int current_depth_ = 0;

    // RAII guard for depth tracking - throws on depth exceeded (hard failure, no partial data)
    struct depth_guard {
        int& depth;
        bool acquired = false;

        depth_guard(int& d) : depth(d) {
            if (depth >= JAI_MAX_SERIALIZATION_DEPTH) {
                throw serialization_error(
                    "Maximum deserialization depth (" + std::to_string(JAI_MAX_SERIALIZATION_DEPTH) +
                    ") exceeded - possible malicious data or deeply nested structure"
                );
            }
            ++depth;
            acquired = true;
        }

        ~depth_guard() {
            if (acquired) --depth;
        }

        // Non-copyable
        depth_guard(const depth_guard&) = delete;
        depth_guard& operator=(const depth_guard&) = delete;
    };

    // Register a reconstructed shared_ptr
    void register_shared_ptr(uint32_t id, const script_value& ptr) {
        if (id != 0) {  // Don't register null
            id_to_shared_ptr_.insert_or_assign(id, ptr);
        }
    }

    // Get previously deserialized shared_ptr by ID
    script_value get_shared_ptr(uint32_t id) const {
        if (id == 0) {
            if (engine_ref_) {
                return script_value(std::monostate{}, engine_ref_);
            }
            throw serialization_error("Engine pointer is null during deserialization");
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
    explicit archive_reader(engine* eng) : engine_ref_(eng) {}

    virtual ~archive_reader() = default;
    
    // Set engine reference (for readers that can't pass it in constructor)
    void set_engine(engine* eng) {
        engine_ref_ = eng;
    }

    // Get engine reference
    engine* get_engine() const {
        return engine_ref_;
    }

    // ============================================================================
    // Shared pointer ID tracking (for de-duplication and weak_ptr reconstruction)
    // ============================================================================
    // Register a newly deserialized shared_ptr with its ID
    // The shared_ptr is stored type-erased so weak_ptr can be reconstructed later
    template<typename T>
    void register_deserialized_shared(uint32_t id, const std::shared_ptr<T>& ptr) {
        if (id != 0 && ptr) {
            cpp_shared_ptrs_[id] = std::static_pointer_cast<void>(ptr);
        }
    }

    // Get a previously deserialized shared_ptr by ID
    // Returns empty shared_ptr if ID not found or was null
    template<typename T>
    std::shared_ptr<T> get_deserialized_shared(uint32_t id) const {
        if (id == 0) return nullptr;
        auto it = cpp_shared_ptrs_.find(id);
        if (it != cpp_shared_ptrs_.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

    // Check if we have a shared_ptr for this ID
    bool has_deserialized_shared(uint32_t id) const {
        return id != 0 && cpp_shared_ptrs_.find(id) != cpp_shared_ptrs_.end();
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

    // Version being deserialized
    uint32_t get_version() const { return version_; }

    // Read a specific property by name and convert to C++ type
    // Used for factory deserialization when properties need to be read before construction
    template<typename T>
    T read_property(const std::string& property_name) {
        // Check if we have pre-read properties (binary format)
        if (has_preread_properties_) {
            auto it = preread_properties_.find(property_name);
            if (it == preread_properties_.end()) {
                throw serialization_error("Property '" + property_name + "' not found in pre-read properties");
            }
            return convert_script_value<T>(it->second);
        } else {
            // Fall back to reading from stream (JSON format)
            std::string name;
            if (!read_property_name(name) || name != property_name) {
                throw serialization_error("Expected property '" + property_name + "' but found '" + name + "'");
            }
            return convert_script_value<T>(read_value());
        }
    }

    // ============================================================================
    // Protected: Internal helpers for derived archive classes
    // ============================================================================
protected:
    // Pre-read properties support (used by binary_archive_reader for factory deserialization)
    void set_preread_properties(std::map<std::string, script_value> props) {
        preread_properties_ = std::move(props);
        has_preread_properties_ = true;
    }

    void clear_preread_properties() {
        preread_properties_.clear();
        has_preread_properties_ = false;
    }

    const script_value* get_preread_property(const std::string& name) const {
        auto it = preread_properties_.find(name);
        return it != preread_properties_.end() ? &it->second : nullptr;
    }

    bool has_preread_properties() const { return has_preread_properties_; }

    // Type-aware value reading (used internally)
    script_value read_value_typed(const type_info_ptr& type_info) {
        if (!type_info) {
            return read_value();
        }

        if (!engine_ref_) {
            throw serialization_error("Engine pointer is null during deserialization");
        }

        switch (type_info->base_type) {
            case script_value_type::jai_int_type:
                if (type_info->native_size == 1) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int8() : read_uint8()), engine_ref_);
                } else if (type_info->native_size == 2) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int16() : read_uint16()), engine_ref_);
                } else if (type_info->native_size == 4) {
                    return script_value(static_cast<script_int>(type_info->is_signed ? read_int32() : read_uint32()), engine_ref_);
                } else if (type_info->native_size == 8 && !type_info->is_signed) {
                    return script_value(static_cast<script_int>(read_uint64()), engine_ref_);
                } else {
                    return script_value(read_int64(), engine_ref_);
                }

            case script_value_type::jai_float_type:
                if (type_info->native_size == 4) {
                    return script_value(static_cast<script_float>(read_float32()), engine_ref_);
                } else {
                    return script_value(read_float64(), engine_ref_);
                }

            default:
                return read_value();
        }
    }

private:
    // Helper: convert script_value to C++ type
    template<typename T>
    static T convert_script_value(const script_value& value) {
        if constexpr (std::is_same_v<T, std::string>) {
            return value.as<std::string>();
        } else if constexpr (std::is_same_v<T, bool>) {
            return value.as<bool>();
        } else if constexpr (std::is_integral_v<T>) {
            return static_cast<T>(value.as<script_int>());
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(value.as<script_float>());
        } else {
            return value.as<T>();
        }
    }

public:
    // ============================================================================
    // Public API: serialize("name", value) - always use named properties
    // ============================================================================
    // Usage in your type's save/load functions:
    //   void load(archive_reader& ar) {
    //       ar.serialize("x", x);
    //       ar.serialize("y", y);
    //   }

    // Serialize primitives (named)
    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_same_v<T, std::string>>
    serialize(const char* name, T& value) {
        // Consume the property name that writer wrote (symmetric with archive_writer)
        std::string read_name;
        read_property_name(read_name);
        (void)name;  // Could verify read_name == name if desired
        read_primitive(value);
    }

    // Serialize custom types (named) - calls T::load(archive) or load(archive, T) via ADL
    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>>
    serialize(const char* name, T& value) {
        // Consume the property name that writer wrote (symmetric with archive_writer)
        std::string read_name;
        read_property_name(read_name);
        (void)name;  // Could verify read_name == name if desired
        read_custom(value);
    }

    // operator() as alias for serialize() - enables ar("name", value) syntax
    template<typename T>
    void operator()(const char* name, T& value) {
        serialize(name, value);
    }

protected:
    uint32_t version_ = 0;

private:
    // Helper: read primitive value (type dispatch)
    template<typename T>
    void read_primitive(T& value) {
        if constexpr (std::is_same_v<T, int8_t>) value = read_int8();
        else if constexpr (std::is_same_v<T, int16_t>) value = read_int16();
        else if constexpr (std::is_same_v<T, int32_t>) value = read_int32();
        else if constexpr (std::is_same_v<T, int64_t>) value = read_int64();
        else if constexpr (std::is_same_v<T, uint8_t>) value = read_uint8();
        else if constexpr (std::is_same_v<T, uint16_t>) value = read_uint16();
        else if constexpr (std::is_same_v<T, uint32_t>) value = read_uint32();
        else if constexpr (std::is_same_v<T, uint64_t>) value = read_uint64();
        else if constexpr (std::is_same_v<T, float>) value = read_float32();
        else if constexpr (std::is_same_v<T, double>) value = read_float64();
        else if constexpr (std::is_same_v<T, bool>) value = read_bool();
        else if constexpr (std::is_same_v<T, std::string>) value = read_string();
        else if constexpr (std::is_integral_v<T>) value = static_cast<T>(read_int64());
        else if constexpr (std::is_floating_point_v<T>) value = static_cast<T>(read_float64());
    }

    // Helper: read custom type (ADL dispatch)
    template<typename T>
    void read_custom(T& value) {
        // Try member load first, then ADL free load
        if constexpr (requires { value.load(*this); }) {
            value.load(*this);
        } else {
            // ADL lookup for free function load(archive_reader&, T&)
            load(*this, value);
        }
    }
};

// Registry for class serialization metadata - now tied to engine instance
class serialization_registry {
public:
    serialization_registry() = default;

    void register_class(const std::string& class_name, const class_metadata& metadata) {
        classes_[class_name] = metadata;
    }

    // Register class with type_index for runtime lookup by C++ type
    void register_class(const std::string& class_name, std::type_index type_idx, const class_metadata& metadata) {
        classes_[class_name] = metadata;
        type_to_class_name_[type_idx] = class_name;
    }

    const class_metadata* get_class_metadata(const std::string& class_name) const {
        auto it = classes_.find(class_name);
        return it != classes_.end() ? &it->second : nullptr;
    }

    // Get metadata by C++ type_index (for automatic serialization of dynamic_binder-registered types)
    const class_metadata* get_class_metadata_by_type(std::type_index type_idx) const {
        auto it = type_to_class_name_.find(type_idx);
        if (it != type_to_class_name_.end()) {
            return get_class_metadata(it->second);
        }
        return nullptr;
    }

    // Get class name by type_index
    const std::string* get_class_name_by_type(std::type_index type_idx) const {
        auto it = type_to_class_name_.find(type_idx);
        return it != type_to_class_name_.end() ? &it->second : nullptr;
    }

    bool has_class(const std::string& class_name) const {
        return classes_.find(class_name) != classes_.end();
    }

    bool has_class_by_type(std::type_index type_idx) const {
        return type_to_class_name_.find(type_idx) != type_to_class_name_.end();
    }

private:
    std::map<std::string, class_metadata> classes_;
    std::map<std::type_index, std::string> type_to_class_name_;
};

} // namespace serialization
} // namespace jai
#endif // JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
