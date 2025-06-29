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
    binary_archive_writer() = default;
    explicit binary_archive_writer(std::vector<uint8_t>& buffer) : buffer_(&buffer) {
        buffer_->clear();
    }
    
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
    
    void write_value(const script_value& value) override {
        // Write type tag first
        write_uint8(static_cast<uint8_t>(value.type()));
        
        switch (value.type()) {
            case value_type::jai_null_type:
                // Nothing to write
                break;
                
            case value_type::jai_bool_type:
                write_bool(value.as<script_bool>());
                break;
                
            case value_type::jai_int_type:
                write_int64(value.as<script_int>());
                break;
                
            case value_type::jai_float_type:
                write_float64(value.as<script_float>());
                break;
                
            case value_type::jai_string_type:
                write_string(value.as<script_string>());
                break;
                
            case value_type::jai_char_type:
                write_uint8(static_cast<uint8_t>(value.as<script_char>()));
                break;
                
            case value_type::jai_array_type: {
                const auto& arr = value.as_array();
                write_uint32(static_cast<uint32_t>(arr.size()));
                for (const auto& elem : arr) {
                    write_value(elem);  // Recursive serialization
                }
                break;
            }
            
            case value_type::jai_map_type: {
                const auto& map = value.as_map();
                write_uint32(static_cast<uint32_t>(map.size()));
                for (const auto& [k, v] : map) {
                    write_value(k);   // Serialize key
                    write_value(v);   // Serialize value
                }
                break;
            }
            
            case value_type::jai_shared_ptr_type: {
                // Handle shared_ptr serialization
                auto shared_val = std::get<std::shared_ptr<script_value>>(value.storage_);
                const void* raw_ptr = shared_val.get();
                
                auto [id, is_new] = track_shared_ptr(raw_ptr);
                
                // Write the ID (with MSB set if new)
                if (is_new && id != 0) {
                    write_uint32(id | 0x80000000);  // Set MSB for new pointers
                    // Serialize the pointed-to value
                    if (shared_val) {
                        write_value(*shared_val);
                    }
                } else {
                    write_uint32(id);  // Just the ID for already-seen or null
                }
                break;
            }
            
            case value_type::jai_weak_ptr_type: {
                // Handle weak_ptr - promote to shared_ptr first
                auto weak_val = std::get<std::weak_ptr<script_value>>(value.storage_);
                if (auto shared_val = weak_val.lock()) {
                    // Treat as shared_ptr
                    const void* raw_ptr = shared_val.get();
                    auto [id, is_new] = track_shared_ptr(raw_ptr);
                    
                    write_uint32(id | (is_new && id != 0 ? 0x80000000 : 0));
                    if (is_new && id != 0) {
                        write_value(*shared_val);
                    }
                } else {
                    // Expired weak_ptr
                    write_uint32(0);  // null
                }
                break;
            }
            
            case value_type::jai_object_type: {
                // Self-describing binary format: property names array + values in order
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                
                // Write type name first
                write_string(type_name);
                
                // Collect properties and values
                std::vector<std::string> property_names;
                std::vector<script_value> property_values;
                
                // Try to serialize as class_instance
                try {
                    auto instance = value.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();
                        if (class_def) {
                            for (const auto& prop_name : class_def->get_property_names()) {
                                auto getter = class_def->get_method("_get_" + prop_name);
                                if (getter.type() == value_type::jai_function_type) {
                                    try {
                                        std::vector<script_value> args = {value};
                                        script_value prop_value = getter.as_function()(args);
                                        property_names.push_back(prop_name);
                                        property_values.push_back(prop_value);
                                    } catch (...) {
                                        // Skip properties that fail
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
    explicit binary_archive_reader(const std::vector<uint8_t>& data) 
        : data_(data), pos_(0) {}
    
    explicit binary_archive_reader(const void* data, size_t size)
        : data_(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size), pos_(0) {}
    
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
    
    bool has_property(const std::string& name) override {
        // In binary format, properties are stored in order
        // This is a simplified implementation
        return true;
    }
    
    script_value read_value() override {
        // Read type tag first
        uint8_t type_tag = read_uint8();
        value_type vtype = static_cast<value_type>(type_tag);
        
        switch (vtype) {
            case value_type::jai_null_type:
                return script_value();
                
            case value_type::jai_bool_type:
                return script_value(read_bool());
                
            case value_type::jai_int_type:
                return script_value(read_int64());
                
            case value_type::jai_float_type:
                return script_value(read_float64());
                
            case value_type::jai_string_type:
                return script_value(read_string());
                
            case value_type::jai_char_type:
                return script_value(static_cast<script_char>(read_uint8()));
                
            case value_type::jai_array_type: {
                uint32_t size = read_uint32();
                script_value array_val = script_value::make_array(nullptr);
                auto& arr = const_cast<std::vector<script_value>&>(array_val.as_array());
                
                for (uint32_t i = 0; i < size; ++i) {
                    arr.push_back(read_value());  // Recursive deserialization
                }
                return array_val;
            }
            
            case value_type::jai_map_type: {
                uint32_t size = read_uint32();
                script_value map_val = script_value::make_map(type_info::make_string(), nullptr);
                auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());
                
                for (uint32_t i = 0; i < size; ++i) {
                    script_value key = read_value();
                    script_value value = read_value();
                    map[key] = value;
                }
                return map_val;
            }
            
            case value_type::jai_shared_ptr_type: {
                uint32_t id = read_uint32();
                
                if (id == 0) {
                    // Null shared_ptr
                    return script_value::make_shared_ptr(script_value());
                }
                
                bool is_new = (id & 0x80000000) != 0;
                id &= 0x7FFFFFFF;  // Clear MSB to get actual ID
                
                if (is_new) {
                    // New shared_ptr - deserialize the value
                    script_value pointed_value = read_value();
                    script_value shared_ptr_val = script_value::make_shared_ptr(pointed_value);
                    register_shared_ptr(id, shared_ptr_val);
                    return shared_ptr_val;
                } else {
                    // Previously seen shared_ptr
                    return get_shared_ptr(id);
                }
            }
            
            case value_type::jai_weak_ptr_type: {
                uint32_t id = read_uint32();
                
                if (id == 0) {
                    // Null/expired weak_ptr
                    return script_value::make_weak_ptr(script_value());
                }
                
                bool is_new = (id & 0x80000000) != 0;
                id &= 0x7FFFFFFF;  // Clear MSB
                
                if (is_new) {
                    // Should not happen - weak_ptrs are serialized as shared_ptrs
                    throw serialization_error("Unexpected new weak_ptr in deserialization");
                } else {
                    // Convert previously seen shared_ptr to weak_ptr
                    script_value shared_val = get_shared_ptr(id);
                    return script_value::make_weak_ptr(shared_val);
                }
            }
            
            case value_type::jai_object_type: {
                // Read self-describing object: type name + property names array + values
                std::string type_name = read_string();
                uint32_t property_count = read_uint32();
                
                // Read property names in order
                std::vector<std::string> property_names;
                property_names.reserve(property_count);
                for (uint32_t i = 0; i < property_count; ++i) {
                    property_names.push_back(read_string());
                }
                
                // Read property values in same order
                std::vector<script_value> property_values;
                property_values.reserve(property_count);
                for (uint32_t i = 0; i < property_count; ++i) {
                    property_values.push_back(read_value());
                }
                
                // Convert to map for reconstruction by from_binary
                script_value map_val = script_value::make_map(type_info::make_string(), nullptr);
                auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());
                
                // Add type information
                map[script_value("_type_")] = script_value(type_name);
                
                // Add properties in the order they were serialized
                for (uint32_t i = 0; i < property_count; ++i) {
                    map[script_value(property_names[i])] = property_values[i];
                }
                
                return map_val;  // Return as map for reconstruction by from_binary
            }
            
            default:
                throw serialization_error("Unsupported type tag in binary deserialization: " + 
                                        std::to_string(type_tag));
        }
    }

protected:
    void skip_property_impl(type_info_ptr type) override {
        // Skip based on type - this needs to match the write format
        if (type->type_name == "int8" || type->type_name == "uint8" || type->type_name == "bool") {
            pos_ += 1;
        } else if (type->type_name == "int16" || type->type_name == "uint16") {
            pos_ += 2;
        } else if (type->type_name == "int32" || type->type_name == "uint32" || type->type_name == "float") {
            pos_ += 4;
        } else if (type->type_name == "int64" || type->type_name == "uint64" || type->type_name == "double") {
            pos_ += 8;
        } else if (type->type_name == "string") {
            uint32_t size = read_uint32();
            pos_ += size;
        } else {
            throw runtime_error("Cannot skip unknown type: " + type->type_name);
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