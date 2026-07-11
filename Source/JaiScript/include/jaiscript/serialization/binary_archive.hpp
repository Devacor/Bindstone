#pragma once

#include "archive_impl.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace jai {
namespace serialization {

class binary_archive_writer : public archive_writer_impl<binary_archive_writer> {
public:
    binary_archive_writer() : archive_writer_impl<binary_archive_writer>() {}
    explicit binary_archive_writer(std::vector<uint8_t>& buffer) : archive_writer_impl<binary_archive_writer>(), buffer_(&buffer) {
        buffer_->clear();
    }
    explicit binary_archive_writer(engine* eng) : archive_writer_impl<binary_archive_writer>(eng) {}
    binary_archive_writer(std::vector<uint8_t>& buffer, engine* eng) : archive_writer_impl<binary_archive_writer>(eng), buffer_(&buffer) {
        buffer_->clear();
    }

    static constexpr bool needs_property_keys = true;
    static constexpr bool is_text_format = false;

    // Get the serialized data
    const std::vector<uint8_t>& data() const { return owned_buffer_.empty() ? *buffer_ : owned_buffer_; }
    std::vector<uint8_t>& data() { return owned_buffer_.empty() ? *buffer_ : owned_buffer_; }

    void write_int8(int8_t value) {
        write_raw(&value, sizeof(value));
    }

    void write_int16(int16_t value) {
        write_little_endian(value);
    }

    void write_int32(int32_t value) {
        write_little_endian(value);
    }

    void write_int64(int64_t value) {
        write_little_endian(value);
    }

    void write_uint8(uint8_t value) {
        write_raw(&value, sizeof(value));
    }

    void write_uint16(uint16_t value) {
        write_little_endian(value);
    }

    void write_uint32(uint32_t value) {
        write_little_endian(value);
    }

    void write_uint64(uint64_t value) {
        write_little_endian(value);
    }

    void write_float32(float value) {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        write_uint32(bits);
    }

    void write_float64(double value) {
        static_assert(sizeof(double) == 8, "double must be 64-bit");
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        write_uint64(bits);
    }

    void write_bool(bool value) {
        write_uint8(value ? 1 : 0);
    }

    void write_string(const std::string& value) {
        write_uint32(static_cast<uint32_t>(value.size()));
        if (!value.empty()) {
            write_raw(value.data(), value.size());
        }
    }

    void write_null() {
        write_uint8(0x00); // Null marker
    }

    void write_binary(const void* data, size_t size) {
        write_uint32(static_cast<uint32_t>(size));
        if (size > 0) {
            write_raw(data, size);
        }
    }
    
    // Buffered mode: collect properties during begin_object..end_object, then flush with sizes
    // Buffered mode: collect properties during begin_object..end_object, then flush with sizes
    void begin_object() {
        write_uint8(0x01); // Object marker
        write_string("");  // Empty type name
        write_uint32(0);   // Version 0
        object_write_stack_.emplace_back();
    }

    void begin_object(const std::string& type_name, uint32_t version) {
        write_uint8(0x01); // Object marker
        write_string(type_name);
        write_uint32(version);
        set_version(version);

        object_write_stack_.emplace_back();
    }

    void end_object() {
        if (!object_write_stack_.empty()) {
            // Save and pop state first so writes go to parent's buffer (or main buffer)
            auto state = std::move(object_write_stack_.back());
            object_write_stack_.pop_back();

            write_uint32(static_cast<uint32_t>(state.properties.size()));

            for (const auto& prop : state.properties) {
                write_string(prop.name);
            }

            for (const auto& prop : state.properties) {
                write_uint32(static_cast<uint32_t>(prop.data.size()));
            }

            for (const auto& prop : state.properties) {
                if (!prop.data.empty()) {
                    write_raw(prop.data.data(), prop.data.size());
                }
            }
        }

        write_uint8(0x02); // End object marker
    }

    void write_property_name(const std::string& name) {
        if (!object_write_stack_.empty()) {
            object_write_stack_.back().properties.push_back({name, {}});
            object_write_stack_.back().current_property_buffer = &object_write_stack_.back().properties.back().data;
        } else {
            write_string(name);
        }
    }

    void begin_array(size_t size) {
        write_uint8(0x03); // Array marker
        write_uint32(static_cast<uint32_t>(size));
    }

    void end_array() {
        write_uint8(0x04); // End array marker
    }

    // Map serialization - Binary uses array of key-value pairs
    void begin_map(size_t size) {
        write_uint8(0x05); // Map marker
        write_uint32(static_cast<uint32_t>(size));
    }

    void end_map() {
        write_uint8(0x06); // End map marker
    }

    void write_map_key(const std::string& key) {
        write_string(key);
    }

    void write_value(const script_value& value) {
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

    struct buffered_property { std::string name; std::vector<uint8_t> data; };
    struct object_write_state {
        std::vector<buffered_property> properties;
        std::vector<uint8_t>* current_property_buffer = nullptr;
    };
    std::vector<object_write_state> object_write_stack_;

    std::vector<uint8_t>& get_buffer() {
        if (buffer_) return *buffer_;
        return owned_buffer_;
    }

    void write_raw(const void* data, size_t size) {
        if (!object_write_stack_.empty() && object_write_stack_.back().current_property_buffer) {
            auto* prop_buf = object_write_stack_.back().current_property_buffer;
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            prop_buf->insert(prop_buf->end(), bytes, bytes + size);
        } else {
            auto& buf = get_buffer();
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            buf.insert(buf.end(), bytes, bytes + size);
        }
    }

    template<typename T>
    void write_little_endian(T value) {
        static_assert(std::is_integral_v<T>, "Only for integral types");
        std::make_unsigned_t<T> uval;
        std::memcpy(&uval, &value, sizeof(T));
        uint8_t bytes[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = static_cast<uint8_t>(uval >> (i * 8));
        }
        write_raw(bytes, sizeof(T));
    }
};

class binary_archive_reader : public archive_reader_impl<binary_archive_reader> {
public:
    // Engine is REQUIRED for binary reading since we need to create script_values
    explicit binary_archive_reader(const std::vector<uint8_t>& data, engine* eng)
        : archive_reader_impl<binary_archive_reader>(eng), data_(data), pos_(0) {
        if (!eng) {
            throw serialization_error("binary_archive_reader requires a valid engine reference");
        }
    }

    explicit binary_archive_reader(const void* data, size_t size, engine* eng)
        : archive_reader_impl<binary_archive_reader>(eng), data_(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size), pos_(0) {
        if (!eng) {
            throw serialization_error("binary_archive_reader requires a valid engine reference");
        }
    }

    ~binary_archive_reader() = default;

private:
    // Object state for pre-read property access with offset-based seeking
    struct property_location { size_t offset; uint32_t size; };
    struct object_state {
        std::map<std::string, size_t> property_name_to_index;       // Name -> index (for out-of-order access)
        std::vector<std::string> property_order;                     // Property names in order
        std::vector<property_location> property_locations;           // Offsets/sizes in same order (O(1) index access)
        size_t next_sequential_index = 0;
        size_t end_object_pos = 0;
    };
    std::vector<object_state> object_stack_;
    const script_value* current_property_value_ = nullptr;  // For pre-read script_values (jai_object_type)
    uint32_t seek_size_ = 0;    // Size of value to read (for validation)

public:
    static constexpr bool needs_property_keys = true;
    static constexpr bool is_text_format = false;

    // Basic type deserialization - check pre-read property first, then fall back to stream
    int8_t read_int8() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<int8_t>(val.as<script_int>());
        }
        int8_t value;
        read_raw(&value, sizeof(value));
        return value;
    }

    int16_t read_int16() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<int16_t>(val.as<script_int>());
        }
        return read_little_endian<int16_t>();
    }

    int32_t read_int32() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<int32_t>(val.as<script_int>());
        }
        return read_little_endian<int32_t>();
    }

    int64_t read_int64() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return val.is_float() ? static_cast<int64_t>(val.as<script_float>()) : val.as<script_int>();
        }
        return read_little_endian<int64_t>();
    }

    uint8_t read_uint8() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<uint8_t>(val.as<script_int>());
        }
        uint8_t value;
        read_raw(&value, sizeof(value));
        return value;
    }

    uint16_t read_uint16() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<uint16_t>(val.as<script_int>());
        }
        return read_little_endian<uint16_t>();
    }

    uint32_t read_uint32() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<uint32_t>(val.as<script_int>());
        }
        return read_little_endian<uint32_t>();
    }

    uint64_t read_uint64() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return static_cast<uint64_t>(val.as<script_int>());
        }
        return read_little_endian<uint64_t>();
    }

    float read_float32() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return val.is_int() ? static_cast<float>(val.as<script_int>()) : static_cast<float>(val.as<script_float>());
        }
        uint32_t bits = read_little_endian<uint32_t>();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double read_float64() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return val.is_int() ? static_cast<double>(val.as<script_int>()) : val.as<script_float>();
        }
        uint64_t bits = read_little_endian<uint64_t>();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool read_bool() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return val.as<script_bool>();
        }
        return read_uint8_raw() != 0;
    }

    std::string read_string() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return val.as<script_string>();
        }
        return read_string_raw();
    }

    bool peek_null() {
        if (pos_ >= data_.size()) return false;
        return data_[pos_] == 0x00;  // Null marker
    }

    void read_null() {
        uint8_t marker = read_uint8_raw();
        if (marker != 0x00) {
            throw serialization_error("Expected null marker (0x00), got " + std::to_string(marker));
        }
    }

    std::vector<uint8_t> read_binary(size_t /*expected_size*/) {
        uint32_t size = read_little_endian<uint32_t>();
        // Validate against remaining data BEFORE allocating: a hostile/corrupt size
        // (e.g. 0xFFFFFFFF) would otherwise allocate ~4 GB before read_raw fails.
        if (size > remaining_bytes()) {
            throw serialization_error("Binary archive blob size exceeds remaining data");
        }
        std::vector<uint8_t> result(size);
        if (size > 0) {
            read_raw(result.data(), size);
        }
        return result;
    }

    // Wire format must agree with the writer's end_object: count, names[], sizes[], values[]
    bool begin_object() {
        std::string type_name;
        uint32_t version = 0;
        return begin_object(type_name, version);
    }

    bool begin_object(std::string& type_name, uint32_t& version) {
        if (pos_ >= data_.size()) return false;

        uint8_t marker = read_uint8_raw();
        if (marker != 0x01) {
            throw runtime_error("Expected object marker, got " + std::to_string(marker));
        }

        type_name = read_string_raw();
        version = read_little_endian<uint32_t>();
        version_ = version;

        uint32_t prop_count = read_little_endian<uint32_t>();
        // Each property contributes at minimum a 4-byte name-length prefix and a 4-byte
        // size entry, so a valid prop_count can't exceed remaining/8. Reject a corrupt or
        // hostile count before the reserve() calls below (reserve(~4 billion) would OOM).
        if (prop_count > remaining_bytes() / 8) {
            throw serialization_error("Binary archive object property count exceeds remaining data");
        }

        // Pre-read all properties for out-of-order access
        object_stack_.emplace_back();
        auto& state = object_stack_.back();

        state.property_order.reserve(prop_count);
        for (uint32_t i = 0; i < prop_count; ++i) {
            state.property_order.push_back(read_string_raw());
        }

        std::vector<uint32_t> sizes;
        sizes.reserve(prop_count);
        for (uint32_t i = 0; i < prop_count; ++i) {
            sizes.push_back(read_little_endian<uint32_t>());
        }

        size_t values_base = pos_;
        size_t offset = 0;
        state.property_locations.reserve(prop_count);
        for (uint32_t i = 0; i < prop_count; ++i) {
            state.property_locations.push_back({values_base + offset, sizes[i]});
            state.property_name_to_index[state.property_order[i]] = i;
            offset += sizes[i];
        }

        state.end_object_pos = values_base + offset;
        return true;
    }

    void end_object() {
        if (!object_stack_.empty()) {
            pos_ = object_stack_.back().end_object_pos;
            object_stack_.pop_back();
        }
        uint8_t marker = read_uint8_raw();
        if (marker != 0x02) {
            throw runtime_error("Expected end object marker, got " + std::to_string(marker));
        }
        current_property_value_ = nullptr;
    }

    size_t begin_array() {
        uint8_t marker = read_uint8_raw();
        if (marker != 0x03) {
            throw runtime_error("Expected array marker, got " + std::to_string(marker));
        }
        uint32_t size = read_little_endian<uint32_t>();
        // A length prefix can't exceed the bytes left (each element costs >=1 byte); reject an
        // oversized count instead of reserving/looping into a multi-GB allocation from a tiny
        // malicious blob (mirrors read_binary). B5.
        if (size > remaining_bytes()) {
            throw serialization_error("Array size exceeds remaining data");
        }
        return size;
    }

    void end_array() {
        uint8_t marker = read_uint8_raw();
        if (marker != 0x04) {
            throw runtime_error("Expected end array marker, got " + std::to_string(marker));
        }
    }

    bool read_property_name(std::string& name) {
        if (pos_ >= data_.size()) return false;
        name = read_string();
        return true;
    }

    // Map deserialization - Binary reads from array of key-value pairs
    size_t begin_map() {
        if (pos_ >= data_.size()) return 0;

        uint8_t marker = read_uint8_raw();
        if (marker != 0x05) {
            throw serialization_error("Expected map marker (0x05)");
        }

        uint32_t size = read_little_endian<uint32_t>();
        // See begin_array: a map of `size` entries needs at least `size` more bytes.
        if (size > remaining_bytes()) {
            throw serialization_error("Map size exceeds remaining data");
        }
        return size;
    }

    void end_map() {
        uint8_t marker = read_uint8_raw();
        if (marker != 0x06) {
            throw serialization_error("Expected end map marker (0x06)");
        }
    }

    bool read_map_key(std::string& key) {
        if (pos_ >= data_.size()) return false;
        key = read_string();
        return true;
    }

    void clear_property_value() {}
    bool has_current_property_value() const { return false; }
    bool in_array() const { return false; }

    bool has_property(const std::string& name) {
        if (!object_stack_.empty()) {
            return object_stack_.back().property_name_to_index.find(name) != object_stack_.back().property_name_to_index.end();
        }
        return false;
    }

    size_t get_object_property_count() const {
        if (object_stack_.empty()) return 0;
        return object_stack_.back().property_order.size();
    }

    const std::vector<std::string>& get_object_property_names() const {
        if (object_stack_.empty()) {
            static const std::vector<std::string> empty;
            return empty;
        }
        return object_stack_.back().property_order;
    }

    bool seek_property_by_index(size_t index) {
        current_property_value_ = nullptr;
        seek_size_ = 0;

        if (object_stack_.empty()) return false;
        auto& state = object_stack_.back();

        if (index >= state.property_locations.size()) return false;

        const auto& loc = state.property_locations[index];
        pos_ = loc.offset;
        seek_size_ = loc.size;
        state.next_sequential_index = index + 1;
        return true;
    }

    bool seek_property_by_index(size_t index, const std::string& expected_name) {
        current_property_value_ = nullptr;
        seek_size_ = 0;

        if (object_stack_.empty()) return false;
        auto& state = object_stack_.back();

        if (index >= state.property_locations.size()) return false;

        if (state.property_order[index] != expected_name) return false;

        const auto& loc = state.property_locations[index];
        pos_ = loc.offset;
        seek_size_ = loc.size;
        state.next_sequential_index = index + 1;
        return true;
    }

    bool seek_property(const std::string& name) {
        current_property_value_ = nullptr;
        seek_size_ = 0;

        if (object_stack_.empty()) {
            throw serialization_error("Binary seek_property called outside object context for '" + name + "'");
        }

        auto& state = object_stack_.back();

        if (state.next_sequential_index < state.property_order.size()) {
            const std::string& next_prop = state.property_order[state.next_sequential_index];
            if (next_prop == name) {
                const auto& loc = state.property_locations[state.next_sequential_index];
                pos_ = loc.offset;
                seek_size_ = loc.size;
                state.next_sequential_index++;
                return true;
            }
        }

        auto it = state.property_name_to_index.find(name);
        if (it == state.property_name_to_index.end()) {
            // Property not found - this is OK for versioning (new property in newer code)
            return false;
        }

        const auto& loc = state.property_locations[it->second];
        pos_ = loc.offset;
        seek_size_ = loc.size;
        return true;
    }

    script_value read_value() {
        if (current_property_value_) {
            auto val = *current_property_value_;
            current_property_value_ = nullptr;
            return val;
        }

        return read_value_internal();
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

        std::make_unsigned_t<T> value = 0;
        for (size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<std::make_unsigned_t<T>>(bytes[i]) << (i * 8);
        }
        T result;
        std::memcpy(&result, &value, sizeof(T));
        return result;
    }

    // Raw read methods that always read from stream (for pre-reading phase)
    uint8_t read_uint8_raw() {
        uint8_t value;
        read_raw(&value, sizeof(value));
        return value;
    }

    size_t remaining_bytes() const { return data_.size() > pos_ ? data_.size() - pos_ : 0; }

    std::string read_string_raw() {
        uint32_t size = read_little_endian<uint32_t>();
        if (size == 0) return "";

        if (size > remaining_bytes()) {
            throw runtime_error("Binary archive string size exceeds remaining data");
        }
        std::string result(size, '\0');
        read_raw(result.data(), size);
        return result;
    }

    script_value read_value_internal() {
        // Track depth - throws on overflow (hard failure, no partial data)
        depth_guard guard(current_depth_);

        // Get engine reference once at the start
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during deserialization");
        }

        // Read type tag first
        uint8_t type_tag = read_uint8_raw();
        script_value_type vtype = static_cast<script_value_type>(type_tag);

        switch (vtype) {
            case script_value_type::jai_null_type:
                return script_value(std::monostate{}, eng);

            case script_value_type::jai_bool_type:
                return script_value(read_uint8_raw() != 0, eng);

            case script_value_type::jai_int_type:
                return script_value(read_little_endian<int64_t>(), eng);

            case script_value_type::jai_float_type: {
                uint64_t bits = read_little_endian<uint64_t>();
                double value;
                std::memcpy(&value, &bits, sizeof(value));
                return script_value(value, eng);
            }

            case script_value_type::jai_string_type:
                return script_value(read_string_raw(), eng);

            case script_value_type::jai_char_type:
                return script_value(static_cast<script_char>(read_uint8_raw()), eng);

            case script_value_type::jai_array_type: {
                uint32_t size = read_little_endian<uint32_t>();
                if (size > remaining_bytes()) {
                    throw runtime_error("Binary archive array size exceeds remaining data");
                }
                script_value array_val = script_value::make_array(nullptr, eng);
                auto& arr = const_cast<std::vector<script_value>&>(array_val.as_array());

                for (uint32_t i = 0; i < size; ++i) {
                    arr.push_back(read_value_internal());
                }
                return array_val;
            }

            case script_value_type::jai_map_type: {
                uint32_t size = read_little_endian<uint32_t>();
                if (size > remaining_bytes()) {
                    throw runtime_error("Binary archive map size exceeds remaining data");
                }
                script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
                auto& map = const_cast<script_map&>(map_val.as_map());

                for (uint32_t i = 0; i < size; ++i) {
                    script_value key = read_value_internal();
                    script_value value = read_value_internal();
                    map.insert_or_assign(key, value);
                }
                return map_val;
            }

            case script_value_type::jai_weak_ptr_type: {
                uint32_t id = read_little_endian<uint32_t>();

                if (id == 0) {
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }

                script_value existing_obj = get_shared_ptr(id);
                if (!existing_obj.is_invalid()) {
                    auto obj_holder = existing_obj.get_object_holder();
                    if (obj_holder) {
                        script_value weak_val(std::monostate{}, eng);
                        weak_val.set_type_info(eng->get_type_info_weak_ptr(existing_obj.get_type_info()));
                        weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                        return weak_val;
                    }
                }

                script_value obj_val = read_value_internal();
                register_shared_ptr(id, obj_val);

                auto obj_holder = obj_val.get_object_holder();
                if (!obj_holder) {
                    throw std::runtime_error("Deserialized weak_ptr object has no object_holder");
                }

                script_value weak_val(std::monostate{}, eng);
                weak_val.set_type_info(eng->get_type_info_weak_ptr(obj_val.get_type_info()));
                weak_val.storage_ = jai::weaker_ptr<script_value::object_holder>(obj_holder);
                return weak_val;
            }

            case script_value_type::jai_object_type: {
                std::string type_name = read_string_raw();
                uint32_t version = read_little_endian<uint32_t>();
                uint32_t property_count = read_little_endian<uint32_t>();
                // Each property is at minimum a 4-byte name length + a 1-byte value tag,
                // so property_count can't exceed remaining/5. Guard the reserve()s below
                // against a corrupt/hostile count (reserve(~4 billion) would OOM).
                if (property_count > remaining_bytes() / 5) {
                    throw serialization_error("Binary archive object property count exceeds remaining data");
                }

                std::vector<std::string> property_names;
                property_names.reserve(property_count);
                for (uint32_t i = 0; i < property_count; ++i) {
                    property_names.push_back(read_string_raw());
                }

                std::vector<script_value> property_values;
                property_values.reserve(property_count);
                for (uint32_t i = 0; i < property_count; ++i) {
                    property_values.push_back(read_value_internal());
                }

                std::map<std::string, script_value> preread_props;
                for (uint32_t i = 0; i < property_count; ++i) {
                    preread_props[property_names[i]] = property_values[i];
                }

                const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);
                if (metadata && metadata->custom_construct) {
                    set_preread_properties(preread_props);
                    any_archive_reader any_ar(*this);
                    script_value constructed_obj = metadata->custom_construct(any_ar, version);
                    clear_preread_properties();

                    auto instance = constructed_obj.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();
                        if (class_def) {
                            auto class_eng = class_def->get_engine();
                            if (class_eng) {
                                for (uint32_t i = 0; i < property_count; ++i) {
                                    const auto& prop_name = property_names[i];
                                    const auto& prop_value = property_values[i];

                                    uint64_t setter_id = class_eng->symbolize("_set_" + prop_name);
                                    auto setter = class_def->get_method(setter_id);
                                    if (setter.type() == script_value_type::jai_function_type) {
                                        try {
                                            std::vector<script_value> args = {constructed_obj, prop_value};
                                            (void)setter.as_function()(args);
                                        } catch (...) {
                                        }
                                    }
                                }

                                uint64_t post_load_id = class_eng->symbolize("post_load");
                                auto post_load = class_def->get_method(post_load_id, false);
                                if (post_load.type() == script_value_type::jai_function_type) {
                                    try {
                                        std::vector<script_value> args = {
                                            constructed_obj,
                                            script_value(static_cast<script_int>(version), class_eng)
                                        };
                                        (void)post_load.as_function()(args);
                                    } catch (...) {
                                    }
                                }
                            }
                        }
                    }

                    return constructed_obj;
                }

                script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
                auto& map = const_cast<script_map&>(map_val.as_map());

                map.insert_or_assign(script_value("_type_", eng), script_value(type_name, eng));

                for (uint32_t i = 0; i < property_count; ++i) {
                    map.insert_or_assign(script_value(property_names[i], eng), property_values[i]);
                }

                return map_val;
            }

            default:
                throw serialization_error("Unsupported type tag in binary deserialization: " +
                                        std::to_string(type_tag));
        }
    }
};

template<> struct writer_archive_id_trait<binary_archive_writer> {
    static constexpr writer_archive_id value = writer_archive_id::binary;
};
template<> struct reader_archive_id_trait<binary_archive_reader> {
    static constexpr reader_archive_id value = reader_archive_id::binary;
};

} // namespace serialization
} // namespace jai