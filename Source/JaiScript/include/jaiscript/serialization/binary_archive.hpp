#pragma once

#include "archive.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace jai {
namespace serialization {

// Binary archive writer - produces portable binary format
class binary_archive_writer : public archive_writer {
public:
    binary_archive_writer() : archive_writer() {}
    explicit binary_archive_writer(std::vector<uint8_t>& buffer) : archive_writer(), buffer_(&buffer) {
        buffer_->clear();
    }
    explicit binary_archive_writer(engine* eng) : archive_writer(eng) {}
    binary_archive_writer(std::vector<uint8_t>& buffer, engine* eng) : archive_writer(eng), buffer_(&buffer) {
        buffer_->clear();
    }

    // Binary format REQUIRES explicit property keys array (no named fields)
    bool needs_property_keys() const override { return true; }

    // Get the serialized data
    const std::vector<uint8_t>& data() const { return owned_buffer_.empty() ? *buffer_ : owned_buffer_; }
    std::vector<uint8_t>& data() { return owned_buffer_.empty() ? *buffer_ : owned_buffer_; }

    // Basic type serialization (little-endian for portability)
    void write_int8(int8_t value) override {
        write_raw(&value, sizeof(value));
    }
    
    void write_int16(int16_t value) override {
        write_little_endian(value);
    }
    
    void write_int32(int32_t value) override {
        write_little_endian(value);
    }
    
    void write_int64(int64_t value) override {
        write_little_endian(value);
    }
    
    void write_uint8(uint8_t value) override {
        write_raw(&value, sizeof(value));
    }
    
    void write_uint16(uint16_t value) override {
        write_little_endian(value);
    }
    
    void write_uint32(uint32_t value) override {
        write_little_endian(value);
    }
    
    void write_uint64(uint64_t value) override {
        write_little_endian(value);
    }
    
    void write_float32(float value) override {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        write_uint32(bits);
    }
    
    void write_float64(double value) override {
        static_assert(sizeof(double) == 8, "double must be 64-bit");
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        write_uint64(bits);
    }
    
    void write_bool(bool value) override {
        write_uint8(value ? 1 : 0);
    }
    
    void write_string(const std::string& value) override {
        write_uint32(static_cast<uint32_t>(value.size()));
        if (!value.empty()) {
            write_raw(value.data(), value.size());
        }
    }
    
    void write_binary(const void* data, size_t size) override {
        write_uint32(static_cast<uint32_t>(size));
        if (size > 0) {
            write_raw(data, size);
        }
    }
    
    // Object/array structure
    void begin_object(const std::string& type_name, uint32_t version) override {
        write_uint8(0x01); // Object marker
        write_string(type_name);
        write_uint32(version);
        set_version(version);
    }
    
    void end_object() override {
        write_uint8(0x02); // End object marker
    }
    
    void begin_array(size_t size) override {
        write_uint8(0x03); // Array marker
        write_uint32(static_cast<uint32_t>(size));
    }
    
    void end_array() override {
        write_uint8(0x04); // End array marker
    }
    
    void write_property_name(const std::string& name) override {
        // In binary format, property names are optional (structure is implicit)
        // We write them for debugging but they're not required for deserialization
        write_string(name);
    }

    // Map serialization - Binary uses array of key-value pairs
    void begin_map(size_t size) override {
        write_uint8(0x05); // Map marker
        write_uint32(static_cast<uint32_t>(size));
    }

    void end_map() override {
        write_uint8(0x06); // End map marker
    }

    void write_map_key(const std::string& key) override {
        write_string(key);
    }

    void write_value(const script_value& value) override {
        // Track depth - throws on overflow (hard failure, no partial data)
        depth_guard guard(current_depth_);

        // Write type tag first
        write_uint8(static_cast<uint8_t>(value.type()));

        switch (value.type()) {
            case script_value_type::jai_null_type:
                // Nothing to write
                break;
                
            case script_value_type::jai_bool_type:
                write_bool(value.as<script_bool>());
                break;
                
            case script_value_type::jai_int_type:
                write_int64(value.as<script_int>());
                break;
                
            case script_value_type::jai_float_type:
                write_float64(value.as<script_float>());
                break;
                
            case script_value_type::jai_string_type:
                write_string(value.as<script_string>());
                break;
                
            case script_value_type::jai_char_type:
                write_uint8(static_cast<uint8_t>(value.as<script_char>()));
                break;
                
            case script_value_type::jai_array_type: {
                const auto& arr = value.as_array();
                write_uint32(static_cast<uint32_t>(arr.size()));
                for (const auto& elem : arr) {
                    write_value(elem);  // Recursive serialization
                }
                break;
            }
            
            case script_value_type::jai_map_type: {
                const auto& map = value.as_map();
                write_uint32(static_cast<uint32_t>(map.size()));
                for (const auto& [k, v] : map) {
                    write_value(k);   // Serialize key
                    write_value(v);   // Serialize value
                }
                break;
            }
            
            case script_value_type::jai_weak_ptr_type: {
                // Serialize weak_ptr by tracking the shared_ptr it references
                auto weak = value.get_weak_ptr();
                auto shared = weak.lock();  // Try to lock to get shared_ptr<object_holder>

                if (!shared) {
                    // Expired or null weak_ptr
                    write_uint32(0);
                } else {
                    // Track the shared_ptr and write its ID
                    auto [id, is_new] = track_shared_ptr(shared.get());
                    write_uint32(id);

                    // If this is the first time seeing this shared object, serialize the data
                    // This makes serialization order-independent: weak_ptr can come before or after shared_ptr
                    if (is_new) {
                        // Create a script_value from the object_holder using factory method
                        script_value shared_val = script_value::make_object(
                            shared->type_name,
                            shared->type_id,
                            shared->data,
                            engine_ref_,
                            shared->is_class_instance_wrapper
                        );
                        write_value(shared_val);
                    }
                }
                break;
            }
            
            case script_value_type::jai_object_type: {
                // Self-describing binary format: type name + version + property names array + values in order
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";

                // For script classes, type_info->type_name may be "any" - use the actual class name
                try {
                    auto instance = value.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();
                        if (class_def) {
                            type_name = class_def->get_name();
                        }
                    }
                } catch (...) {
                    // Not a class_instance, keep original type_name
                }

                // Write type name first
                write_string(type_name);

                // Look up version from serialization metadata
                uint32_t version = 1;  // Default version
                if (auto eng = engine_ref_) {
                    const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);
                    if (metadata) {
                        version = metadata->current_version;
                    } else {
                        // Runtime validation: Ensure type is registered
                        throw serialization_error(
                            "Cannot serialize unregistered type '" + type_name + "'. " +
                            "Register the type with dynamic_binder before serialization."
                        );
                    }
                }
                write_uint32(version);

                // Collect properties and values
                std::vector<std::string> property_names;
                std::vector<script_value> property_values;
                
                // Try to serialize as class_instance
                try {
                    auto instance = value.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();
                        if (class_def) {
                            auto eng = class_def->get_engine();
                            if (eng) {
                                for (const auto& prop_name : class_def->get_property_names()) {
                                    uint64_t getter_id = eng->symbolize("_get_" + prop_name);
                                    auto getter = class_def->get_method(getter_id);
                                    if (getter.type() == script_value_type::jai_function_type) {
                                        try {
                                            std::vector<script_value> args = {value};
                                            auto result = getter.as_function()(args);
                                            if (result) {
                                                property_names.push_back(prop_name);
                                                property_values.push_back(std::move(result.value()));
                                            }
                                            // Skip properties that fail (result has error)
                                        } catch (...) {
                                            // Skip properties that fail
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {
                    // Not a class_instance, empty object
                }
                
                // Write property count
                write_uint32(static_cast<uint32_t>(property_names.size()));
                
                // Write property names array
                for (const auto& prop_name : property_names) {
                    write_string(prop_name);
                }
                
                // Write property values in same order
                for (const auto& prop_value : property_values) {
                    write_value(prop_value);
                }
                break;
            }
            
            default:
                throw serialization_error("Unsupported type for binary serialization: " + std::to_string(static_cast<int>(value.type())));
        }
    }

private:
    std::vector<uint8_t>* buffer_ = nullptr;
    std::vector<uint8_t> owned_buffer_;
    
    std::vector<uint8_t>& get_buffer() {
        if (buffer_) return *buffer_;
        return owned_buffer_;
    }
    
    void write_raw(const void* data, size_t size) {
        auto& buf = get_buffer();
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), bytes, bytes + size);
    }
    
    template<typename T>
    void write_little_endian(T value) {
        static_assert(std::is_integral_v<T>, "Only for integral types");
        
        uint8_t bytes[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = static_cast<uint8_t>(value >> (i * 8));
        }
        write_raw(bytes, sizeof(T));
    }
};

// Binary archive reader
class binary_archive_reader : public archive_reader {
public:
    // Engine is REQUIRED for binary reading since we need to create script_values
    explicit binary_archive_reader(const std::vector<uint8_t>& data, engine* eng)
        : archive_reader(eng), data_(data), pos_(0) {
        if (!eng) {
            throw serialization_error("binary_archive_reader requires a valid engine reference");
        }
    }

    explicit binary_archive_reader(const void* data, size_t size, engine* eng)
        : archive_reader(eng), data_(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size), pos_(0) {
        if (!eng) {
            throw serialization_error("binary_archive_reader requires a valid engine reference");
        }
    }

    virtual ~binary_archive_reader() = default;

    // Binary format REQUIRES explicit property keys array (no named fields)
    bool needs_property_keys() const override { return true; }

    // Basic type deserialization
    int8_t read_int8() override {
        int8_t value;
        read_raw(&value, sizeof(value));
        return value;
    }
    
    int16_t read_int16() override {
        return read_little_endian<int16_t>();
    }
    
    int32_t read_int32() override {
        return read_little_endian<int32_t>();
    }
    
    int64_t read_int64() override {
        return read_little_endian<int64_t>();
    }
    
    uint8_t read_uint8() override {
        uint8_t value;
        read_raw(&value, sizeof(value));
        return value;
    }
    
    uint16_t read_uint16() override {
        return read_little_endian<uint16_t>();
    }
    
    uint32_t read_uint32() override {
        return read_little_endian<uint32_t>();
    }
    
    uint64_t read_uint64() override {
        return read_little_endian<uint64_t>();
    }
    
    float read_float32() override {
        uint32_t bits = read_uint32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    
    double read_float64() override {
        uint64_t bits = read_uint64();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    
    bool read_bool() override {
        return read_uint8() != 0;
    }
    
    std::string read_string() override {
        uint32_t size = read_uint32();
        if (size == 0) return "";
        
        std::string result(size, '\0');
        read_raw(result.data(), size);
        return result;
    }
    
    std::vector<uint8_t> read_binary(size_t expected_size) override {
        uint32_t size = read_uint32();
        std::vector<uint8_t> result(size);
        if (size > 0) {
            read_raw(result.data(), size);
        }
        return result;
    }
    
    // Object/array structure
    bool begin_object(std::string& type_name, uint32_t& version) override {
        if (pos_ >= data_.size()) return false;
        
        uint8_t marker = read_uint8();
        if (marker != 0x01) {
            throw runtime_error("Expected object marker, got " + std::to_string(marker));
        }
        
        type_name = read_string();
        version = read_uint32();
        version_ = version;
        return true;
    }
    
    void end_object() override {
        uint8_t marker = read_uint8();
        if (marker != 0x02) {
            throw runtime_error("Expected end object marker, got " + std::to_string(marker));
        }
    }
    
    size_t begin_array() override {
        uint8_t marker = read_uint8();
        if (marker != 0x03) {
            throw runtime_error("Expected array marker, got " + std::to_string(marker));
        }
        return read_uint32();
    }
    
    void end_array() override {
        uint8_t marker = read_uint8();
        if (marker != 0x04) {
            throw runtime_error("Expected end array marker, got " + std::to_string(marker));
        }
    }
    
    bool read_property_name(std::string& name) override {
        if (pos_ >= data_.size()) return false;
        name = read_string();
        return true;
    }

    // Map deserialization - Binary reads from array of key-value pairs
    size_t begin_map() override {
        if (pos_ >= data_.size()) return 0;

        uint8_t marker = read_uint8();
        if (marker != 0x05) {
            throw serialization_error("Expected map marker (0x05)");
        }

        uint32_t size = read_uint32();
        return size;
    }

    void end_map() override {
        uint8_t marker = read_uint8();
        if (marker != 0x06) {
            throw serialization_error("Expected end map marker (0x06)");
        }
    }

    bool read_map_key(std::string& key) override {
        if (pos_ >= data_.size()) return false;
        key = read_string();
        return true;
    }

    bool has_property(const std::string& name) override {
        // In binary format, properties are stored in order
        // This is a simplified implementation
        return true;
    }

    script_value read_value() override {
        // Track depth - throws on overflow (hard failure, no partial data)
        depth_guard guard(current_depth_);

        // Get engine reference once at the start
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during deserialization");
        }

        // Read type tag first
        uint8_t type_tag = read_uint8();
        script_value_type vtype = static_cast<script_value_type>(type_tag);

        switch (vtype) {
            case script_value_type::jai_null_type:
                return script_value(std::monostate{}, eng);

            case script_value_type::jai_bool_type:
                return script_value(read_bool(), eng);

            case script_value_type::jai_int_type:
                return script_value(read_int64(), eng);

            case script_value_type::jai_float_type:
                return script_value(read_float64(), eng);

            case script_value_type::jai_string_type:
                return script_value(read_string(), eng);

            case script_value_type::jai_char_type:
                return script_value(static_cast<script_char>(read_uint8()), eng);

            case script_value_type::jai_array_type: {
                uint32_t size = read_uint32();
                script_value array_val = script_value::make_array(nullptr, eng);
                auto& arr = const_cast<std::vector<script_value>&>(array_val.as_array());

                for (uint32_t i = 0; i < size; ++i) {
                    arr.push_back(read_value());  // Recursive deserialization
                }
                return array_val;
            }

            case script_value_type::jai_map_type: {
                uint32_t size = read_uint32();
                script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
                auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());

                for (uint32_t i = 0; i < size; ++i) {
                    script_value key = read_value();
                    script_value value = read_value();
                    map.insert_or_assign(key, value);
                }
                return map_val;
            }

            case script_value_type::jai_weak_ptr_type: {
                // Read the shared_ptr ID this weak_ptr references
                uint32_t id = read_uint32();

                if (id == 0) {
                    // Null or expired weak_ptr - create empty weak_ptr
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }

                // Check if we've already reconstructed this object
                script_value existing_obj = get_shared_ptr(id);

                if (!existing_obj.is_invalid()) {
                    // Object was already reconstructed, get its object_holder and create weak_ptr
                    auto obj_holder = existing_obj.get_object_holder();
                    if (obj_holder) {
                        script_value weak_val(std::monostate{}, eng);
                        weak_val.set_type_info(eng->get_type_info_weak_ptr(existing_obj.get_type_info()));
                        weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                        return weak_val;
                    }
                }

                // First time seeing this ID - need to deserialize the object
                script_value obj_val = read_value();  // Recursively read the object (already properly wrapped)

                // Register the object for future references
                register_shared_ptr(id, obj_val);

                // Get the object_holder from the deserialized object
                auto obj_holder = obj_val.get_object_holder();
                if (!obj_holder) {
                    // Object doesn't have an object_holder (shouldn't happen for valid objects)
                    throw std::runtime_error("Deserialized weak_ptr object has no object_holder");
                }

                // Create weak_ptr to the object_holder
                script_value weak_val(std::monostate{}, eng);
                weak_val.set_type_info(eng->get_type_info_weak_ptr(obj_val.get_type_info()));
                weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                return weak_val;
            }
            
            case script_value_type::jai_object_type: {
                // Read self-describing object: type name + version + property names array + values
                std::string type_name = read_string();
                uint32_t version = read_uint32();
                uint32_t property_count = read_uint32();

                // Read ALL property names first (binary format stores them separately)
                std::vector<std::string> property_names;
                property_names.reserve(property_count);
                for (uint32_t i = 0; i < property_count; ++i) {
                    property_names.push_back(read_string());
                }

                // Read ALL property values
                std::vector<script_value> property_values;
                property_values.reserve(property_count);
                for (uint32_t i = 0; i < property_count; ++i) {
                    property_values.push_back(read_value());
                }

                // Build a map of pre-read properties for factory access
                std::map<std::string, script_value> preread_props;
                for (uint32_t i = 0; i < property_count; ++i) {
                    preread_props[property_names[i]] = property_values[i];
                }

                // Check if there's a custom factory for this type
                // eng is already set at the start of read_value()
                const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);
                if (metadata && metadata->custom_construct) {
                    // Set pre-read properties so factory can access them via read_property
                    set_preread_properties(preread_props);

                    // Use custom factory for construction with proper version
                    script_value constructed_obj = metadata->custom_construct(*this, version);

                    // Clear pre-read properties
                    clear_preread_properties();

                    // Now hydrate the object's properties (all properties already read above)
                    auto instance = constructed_obj.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();
                        if (class_def) {
                            // Set each property from pre-read values
                            auto class_eng = class_def->get_engine();
                            if (class_eng) {
                                for (uint32_t i = 0; i < property_count; ++i) {
                                    const auto& prop_name = property_names[i];
                                    const auto& prop_value = property_values[i];

                                    // Try to find setter
                                    uint64_t setter_id = class_eng->symbolize("_set_" + prop_name);
                                    auto setter = class_def->get_method(setter_id);
                                    if (setter.type() == script_value_type::jai_function_type) {
                                        try {
                                            std::vector<script_value> args = {constructed_obj, prop_value};
                                            (void)setter.as_function()(args);  // Explicitly discard return value
                                        } catch (...) {
                                            // Skip properties that fail to set
                                        }
                                    }
                                }

                                // Call post_deserialize hook if it exists
                                // Pass both the object and the version number for migration logic
                                uint64_t post_deserialize_id = class_eng->symbolize("post_deserialize");
                                auto post_deserialize = class_def->get_method(post_deserialize_id, false);  // Don't throw if not found
                                if (post_deserialize.type() == script_value_type::jai_function_type) {
                                    try {
                                        std::vector<script_value> args = {
                                            constructed_obj,
                                            script_value(static_cast<script_int>(version), class_eng)
                                        };
                                        (void)post_deserialize.as_function()(args);
                                    } catch (...) {
                                        // Hook failed, but continue
                                    }
                                }
                            }
                        }
                    }

                    return constructed_obj;
                }

                // No custom factory - use default deserialization path
                // Convert to map for reconstruction by from_binary
                script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
                auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());

                // Add type information
                map.insert_or_assign(script_value("_type_", eng), script_value(type_name, eng));

                // Add properties in the order they were serialized
                for (uint32_t i = 0; i < property_count; ++i) {
                    map.insert_or_assign(script_value(property_names[i], eng), property_values[i]);
                }

                return map_val;  // Return as map for reconstruction by from_binary
            }
            
            default:
                throw serialization_error("Unsupported type tag in binary deserialization: " + 
                                        std::to_string(type_tag));
        }
    }

private:
    std::vector<uint8_t> data_;
    size_t pos_;
    
    void read_raw(void* dest, size_t size) {
        if (pos_ + size > data_.size()) {
            throw runtime_error("Binary archive read past end of data");
        }
        std::memcpy(dest, data_.data() + pos_, size);
        pos_ += size;
    }
    
    template<typename T>
    T read_little_endian() {
        static_assert(std::is_integral_v<T>, "Only for integral types");
        
        uint8_t bytes[sizeof(T)];
        read_raw(bytes, sizeof(T));
        
        T value = 0;
        for (size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<T>(bytes[i]) << (i * 8);
        }
        return value;
    }
};

} // namespace serialization
} // namespace jai