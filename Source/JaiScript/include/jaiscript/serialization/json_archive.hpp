#pragma once

#include "archive.hpp"
#include <stack>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace jai {
namespace serialization {

// JSON archive writer (CRTP, no virtual dispatch)
class json_archive_writer : public archive_writer_impl<json_archive_writer> {
public:
    json_archive_writer(int indent = 2) : archive_writer_impl<json_archive_writer>(), indent_(indent), json_depth_(0) {}
    json_archive_writer(int indent, engine* eng) : archive_writer_impl<json_archive_writer>(eng), indent_(indent), json_depth_(0) {}
    explicit json_archive_writer(engine* eng) : archive_writer_impl<json_archive_writer>(eng), indent_(2), json_depth_(0) {}

    // JSON format properties (compile-time constants, no virtual dispatch)
    static constexpr bool needs_property_keys = false;
    static constexpr bool is_text_format = true;

    // Basic type serialization - non-virtual
    void write_int8(int8_t value) { write_json_value(value); }
    void write_int16(int16_t value) { write_json_value(value); }
    void write_int32(int32_t value) { write_json_value(value); }
    void write_int64(int64_t value) { write_json_value(value); }
    void write_uint8(uint8_t value) { write_json_value(value); }
    void write_uint16(uint16_t value) { write_json_value(value); }
    void write_uint32(uint32_t value) { write_json_value(value); }
    void write_uint64(uint64_t value) { write_json_value(value); }
    void write_float32(float value) { write_json_value(value); }
    void write_float64(double value) { write_json_value(value); }
    void write_bool(bool value) { write_json_value(value); }

    void write_null() { write_json_value("null"); }

    void write_string(const std::string& value) {
        write_json_value('"' + escape_json_string_local(value) + '"');
    }

    void write_binary(const void* data, size_t size) {
        // Encode binary as base64 or hex string
        std::ostringstream oss;
        oss << '"';
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
        }
        oss << '"';
        write_json_value(oss.str());
    }

    // Object/array structure - non-virtual
    // No-argument version for inline objects (no type metadata)
    void begin_object() {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        json_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);
    }

    void begin_object(const std::string& type_name, uint32_t version) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        json_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);

        // Write _type_ and _version_ metadata
        write_property_name("_type_");
        write_string(type_name);
        if (version > 1) {
            write_property_name("_version_");
            write_uint32(version);
        }
    }

    void end_object() {
        json_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << '}';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void begin_array(size_t size) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '[';
        json_depth_++;
        container_stack_.push(ContainerType::Array);
        first_in_container_.push(true);
    }

    void end_array() {
        json_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << ']';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void write_property_name(const std::string& name) {
        if (!first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '"' << escape_json_string_local(name) << "\": ";
        first_in_container_.top() = false;
        // Context is managed by base class serialize() which pushes PropertyValue after this
    }

    // Map serialization - JSON uses native object format
    void begin_map(size_t size) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        json_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);
    }

    void end_map() {
        json_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << '}';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void write_map_key(const std::string& key) {
        // Same as write_property_name
        if (!first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '"' << escape_json_string_local(key) << "\": ";
        first_in_container_.top() = false;
        // Context is managed by base class which pushes MapValue after this
    }

    void write_value(const script_value& value) {
        // Track depth - throws on overflow (hard failure, no partial data)
        depth_guard guard(current_depth_);

        // Handle shared_ptr types with proper tracking, delegate others to stdlib
        switch (value.type()) {
            
            case script_value_type::jai_weak_ptr_type: {
                // Serialize weak_ptr by tracking the shared_ptr it references
                auto weak = value.get_weak_ptr();
                auto shared = weak.lock();  // Try to lock to get shared_ptr<object_holder>

                if (!shared) {
                    // Expired or null weak_ptr
                    oss_ << "{\"$weak_ptr_id\": 0}";
                } else {
                    // Track the shared_ptr and write its ID
                    auto [id, is_new] = track_shared_ptr(shared.get());
                    oss_ << "{\"$weak_ptr_id\": " << id;

                    // If this is the first time seeing this object, serialize it inline
                    if (is_new) {
                        oss_ << ", \"$weak_ptr_data\": ";
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

                    oss_ << "}";
                }
                break;
            }
            
            case script_value_type::jai_array_type: {
                const auto& arr = value.as_array();
                oss_ << '[';
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) oss_ << ',';
                    write_value(arr[i]);  // Recursive call handles shared_ptrs
                }
                oss_ << ']';
                break;
            }
            
            case script_value_type::jai_map_type: {
                const auto& map = value.as_map();
                oss_ << '{';
                bool first = true;
                for (const auto& [k, v] : map) {
                    if (!first) oss_ << ',';
                    first = false;
                    
                    if (k.is_string()) {
                        oss_ << '"' << escape_json_string_local(k.as_string()) << '"';
                    } else {
                        oss_ << '"' << escape_json_string_local(k.to_string()) << '"';
                    }
                    
                    oss_ << ':';
                    write_value(v);  // Recursive call handles shared_ptrs
                }
                oss_ << '}';
                break;
            }
            
            case script_value_type::jai_object_type: {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";

                auto eng = engine_ref_;
                if (!eng) {
                    oss_ << "null";
                    break;
                }

                // Try to serialize as class_instance
                try {
                    auto instance = value.as<std::shared_ptr<class_instance>>();
                    if (instance) {
                        auto class_def = instance->get_class_definition();

                        // For script classes, type_info->type_name may be "any" - use the actual class name
                        if (class_def) {
                            type_name = class_def->get_name();
                        }

                        oss_ << '{';
                        bool first = true;

                        // Add _type_ field
                        oss_ << "\"_type_\":\"" << escape_json_string_local(type_name) << "\"";
                        first = false;

                        // Check if this type is registered in the serialization registry
                        const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);

                        if (metadata && !metadata->properties.empty() && class_def) {
                            // C++ bound class with registered properties - use getters
                            for (const auto& prop_name : class_def->get_property_names()) {
                                uint64_t getter_id = eng->symbolize("_get_" + prop_name);
                                auto getter = class_def->get_method(getter_id);
                                if (getter.type() == script_value_type::jai_function_type) {
                                    try {
                                        std::vector<script_value> args = {value};
                                        auto result = getter.as_function()(args);
                                        if (result) {
                                            if (!first) oss_ << ',';
                                            first = false;
                                            oss_ << "\"" << escape_json_string_local(prop_name) << "\":";
                                            write_value(std::move(result.value()));
                                        }
                                    } catch (...) {
                                        // Skip properties that fail to get
                                    }
                                }
                            }
                        } else {
                            // Script-defined class or unregistered - serialize fields directly
                            const auto& fields = instance->get_fields();
                            for (const auto& [field_id, field_val] : fields) {
                                // Skip the __cpp_object__ field - it's not serializable
                                std::string field_name = std::string(eng->get_symbolizer()->get_string(field_id));
                                if (field_name == "__cpp_object__") continue;

                                if (!first) oss_ << ',';
                                first = false;
                                oss_ << "\"" << escape_json_string_local(field_name) << "\":";
                                write_value(field_val);
                            }
                        }

                        oss_ << '}';
                        break;
                    }
                } catch (...) {
                    // Not a class_instance - fall through to default
                }

                // Fallback for other object types
                oss_ << to_json_impl_fallback(value, 0, 0);
                break;
            }

            default: {
                // Use the existing to_json implementation for other types
                oss_ << to_json_impl_fallback(value, 0, 0);  // No indentation, we handle it ourselves
                break;
            }
        }
    }
    
    // Get the resulting JSON string
    std::string str() const { return oss_.str(); }
    
private:
    enum class ContainerType { Object, Array };
    
    // Local helper functions to avoid circular includes
    std::string escape_json_string_local(const std::string& str) {
        std::ostringstream oss;
        for (size_t i = 0; i < str.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            switch (c) {
                case '"':  oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (c >= 0x80) {
                        // UTF-8 multi-byte: pass through unescaped (valid JSON per RFC 8259)
                        oss << str[i];
                    } else if (c >= 0x20) {
                        oss << str[i];
                    } else {
                        // Control characters below 0x20 (other than those handled above)
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(c);
                    }
            }
        }
        return oss.str();
    }
    
    std::string to_json_impl_fallback(const script_value& val, int indent, int current_depth) {
        // Simple fallback for non-shared_ptr types
        std::ostringstream oss;
        
        switch (val.type()) {
            case script_value_type::jai_null_type:
                oss << "null";
                break;
                
            case script_value_type::jai_bool_type:
                oss << (val.as_bool() ? "true" : "false");
                break;
                
            case script_value_type::jai_int_type:
                oss << val.as_int();
                break;
                
            case script_value_type::jai_float_type: {
                double d = val.as_float();
                if (std::isfinite(d)) {
                    oss << std::setprecision(15) << d;
                    std::string str = oss.str();
                    if (str.find('.') == std::string::npos && str.find('e') == std::string::npos) {
                        oss << ".0";
                    }
                } else {
                    oss << "null";
                }
                break;
            }
                
            case script_value_type::jai_string_type:
                oss << '"' << escape_json_string_local(val.as_string()) << '"';
                break;
                
            case script_value_type::jai_char_type:
                oss << '"' << escape_json_string_local(std::string(1, val.as_char())) << '"';
                break;
                
            case script_value_type::jai_array_type: {
                const auto& arr = val.as_array();
                oss << '[';
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) oss << ',';
                    oss << to_json_impl_fallback(arr[i], indent, current_depth + 1);
                }
                oss << ']';
                break;
            }
            
            case script_value_type::jai_map_type: {
                const auto& map = val.as_map();
                oss << '{';
                bool first = true;
                for (const auto& [k, v] : map) {
                    if (!first) oss << ',';
                    first = false;
                    
                    if (k.is_string()) {
                        oss << '"' << escape_json_string_local(k.as_string()) << '"';
                    } else {
                        oss << '"' << escape_json_string_local(k.to_string()) << '"';
                    }
                    
                    oss << ':';
                    oss << to_json_impl_fallback(v, indent, current_depth + 1);
                }
                oss << '}';
                break;
            }
            
            default:
                oss << "null";
                break;
        }
        
        return oss.str();
    }
    
    int indent_;
    int json_depth_;  // JSON nesting depth (separate from serialization depth tracking in base)
    std::ostringstream oss_;
    std::stack<ContainerType> container_stack_;
    std::stack<bool> first_in_container_;
    // Context tracking is now handled by base class (SerializationContext)

    bool in_container() const { return !container_stack_.empty(); }

    void write_indent() {
        if (indent_ > 0) {
            oss_ << std::string(json_depth_ * indent_, ' ');
        }
    }
    
    void write_newline() {
        if (indent_ > 0) {
            oss_ << '\n';
        }
    }
    
    template<typename T>
    void write_json_value(T value) {
        // Check base class context: PropertyValue/MapValue context means we're after a property name
        auto ctx = this->current_context();
        bool after_property = (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue);

        if (!after_property && in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            // Only add comma in array context for non-first elements
            oss_ << ',';
            write_newline();
            write_indent();
        }

        if constexpr (std::is_same_v<T, bool>) {
            oss_ << (value ? "true" : "false");
        } else if constexpr (std::is_floating_point_v<T>) {
            // Handle special float values
            if (std::isnan(value)) {
                oss_ << "null";
            } else if (std::isinf(value)) {
                oss_ << (value > 0 ? "1e999" : "-1e999");
            } else {
                oss_ << value;
            }
        } else {
            oss_ << value;
        }

        if (in_container() && container_stack_.top() == ContainerType::Array) {
            first_in_container_.top() = false;
        }
    }
};

// JSON archive reader (CRTP, no virtual dispatch)
class json_archive_reader : public archive_reader_impl<json_archive_reader> {
public:
    // Engine is REQUIRED for JSON reading since we need to create script_values
    json_archive_reader(const std::string& json_string, engine* eng)
        : archive_reader_impl<json_archive_reader>(eng), json_(json_string), pos_(0) {
        if (!eng) {
            throw serialization_error("json_archive_reader requires a valid engine reference");
        }
        root_value_ = parse_json();
        current_value_ = &root_value_;
        path_stack_.push_back("");
    }

    ~json_archive_reader() = default;

    // JSON format properties (compile-time constants, no virtual dispatch)
    static constexpr bool needs_property_keys = false;
    static constexpr bool is_text_format = true;

    // Basic type deserialization - non-virtual
    int8_t read_int8() { return static_cast<int8_t>(read_value().as<script_int>()); }
    int16_t read_int16() { return static_cast<int16_t>(read_value().as<script_int>()); }
    int32_t read_int32() { return static_cast<int32_t>(read_value().as<script_int>()); }
    int64_t read_int64() { return read_value().as<script_int>(); }
    uint8_t read_uint8() { return static_cast<uint8_t>(read_value().as<script_int>()); }
    uint16_t read_uint16() { return static_cast<uint16_t>(read_value().as<script_int>()); }
    uint32_t read_uint32() { return static_cast<uint32_t>(read_value().as<script_int>()); }
    uint64_t read_uint64() { return static_cast<uint64_t>(read_value().as<script_int>()); }
    float read_float32() { return static_cast<float>(read_value().as<script_float>()); }
    double read_float64() { return read_value().as<script_float>(); }
    bool read_bool() { return read_value().as<script_bool>(); }
    std::string read_string() { return read_value().as<script_string>(); }

    bool peek_null() {
        const script_value* value_to_check = nullptr;
        if (current_property_value_) {
            value_to_check = current_property_value_;
        } else if (!array_stack_.empty()) {
            auto& state = array_stack_.top();
            if (state.index < state.array.size()) {
                value_to_check = &state.array[state.index];
            }
        }
        return value_to_check && value_to_check->is_null();
    }

    void read_null() {
        auto val = read_value();
        if (!val.is_null()) {
            throw serialization_error("Expected null value");
        }
    }

    std::vector<uint8_t> read_binary(size_t size) {
        std::string hex_str = read_value().as<script_string>();
        std::vector<uint8_t> result;
        for (size_t i = 0; i < hex_str.length(); i += 2) {
            std::string byte_str = hex_str.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
            result.push_back(byte);
        }
        return result;
    }

    // Object/array structure - non-virtual
    // No-argument version for inline objects (discard type metadata)
    bool begin_object() {
        std::string type_name;
        uint32_t version = 0;
        return begin_object(type_name, version);
    }

    bool begin_object(std::string& type_name, uint32_t& version) {
        // Determine which value to check
        const script_value* value_to_check = nullptr;

        if (current_property_value_) {
            // Use value from read_property_name()
            value_to_check = current_property_value_;
        } else if (!array_stack_.empty()) {
            // We're inside an array - read the next element
            auto& arr_state = array_stack_.top();
            if (arr_state.index < arr_state.array.size()) {
                temp_value_ = arr_state.array[arr_state.index++];
                value_to_check = &temp_value_;
            } else {
                return false;
            }
        } else {
            value_to_check = current_value_;
        }

        if (!value_to_check || !value_to_check->is_map()) {
            return false;
        }

        const auto& map = value_to_check->as_map();

        // Get engine reference for creating script_values
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        // Read _type_ field
        auto type_it = map.find(script_value("_type_", eng));
        if (type_it != map.end() && type_it->second.is_string()) {
            type_name = type_it->second.as_string();
        } else {
            type_name = "";
        }

        // Read _version_ field (default to 1)
        auto version_it = map.find(script_value("_version_", eng));
        if (version_it != map.end() && version_it->second.is_int()) {
            version = static_cast<uint32_t>(version_it->second.as_int());
        } else {
            version = 1;
        }
        
        version_ = version;
        
        // Set up iteration state
        object_stack_.push(ObjectState{map, map.begin()});

        // Clear current_property_value_ after using it (consumed)
        current_property_value_ = nullptr;

        return true;
    }

    void end_object() {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }

    size_t begin_array() {
        // Determine which value to check
        const script_value* value_to_check = nullptr;

        if (current_property_value_) {
            // Use value from read_property_name()
            value_to_check = current_property_value_;
        } else if (!array_stack_.empty()) {
            // We're inside an array - read the next element
            auto& arr_state = array_stack_.top();
            if (arr_state.index < arr_state.array.size()) {
                temp_value_ = arr_state.array[arr_state.index++];
                value_to_check = &temp_value_;
            } else {
                return 0;
            }
        } else {
            value_to_check = current_value_;
        }

        if (!value_to_check || !value_to_check->is_array()) {
            return 0;
        }

        const auto& arr = value_to_check->as_array();
        array_stack_.push(ArrayState{arr, 0});

        // Clear current_property_value_ after using it (consumed)
        current_property_value_ = nullptr;

        return arr.size();
    }

    void end_array() {
        if (!array_stack_.empty()) {
            array_stack_.pop();
        }
    }

    bool read_property_name(std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }

        auto& obj_state = object_stack_.top();

        // Skip metadata fields
        while (obj_state.current != obj_state.map.end()) {
            const std::string& prop_name = obj_state.current->first.as_string();
            if (prop_name != "_type_" && prop_name != "_version_") {
                name = prop_name;
                current_property_value_ = &obj_state.current->second;
                ++obj_state.current;
                return true;
            }
            ++obj_state.current;
        }

        return false;
    }

    // Map deserialization - JSON reads from native object format
    size_t begin_map() {
        // Use same logic as begin_object but without type/version reading
        const script_value* value_to_check = nullptr;

        if (current_property_value_) {
            value_to_check = current_property_value_;
        } else if (!array_stack_.empty()) {
            auto& arr_state = array_stack_.top();
            if (arr_state.index < arr_state.array.size()) {
                temp_value_ = arr_state.array[arr_state.index++];
                value_to_check = &temp_value_;
            } else {
                return 0;
            }
        } else {
            value_to_check = current_value_;
        }

        if (!value_to_check || !value_to_check->is_map()) {
            return 0;
        }

        const auto& map = value_to_check->as_map();
        object_stack_.push(ObjectState{map, map.begin()});
        current_property_value_ = nullptr;

        return map.size();
    }

    void end_map() {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }

    bool read_map_key(std::string& key) {
        // Same as read_property_name but without filtering metadata
        if (object_stack_.empty()) {
            return false;
        }

        auto& obj_state = object_stack_.top();

        if (obj_state.current != obj_state.map.end()) {
            key = obj_state.current->first.as_string();
            current_property_value_ = &obj_state.current->second;
            ++obj_state.current;
            return true;
        }

        return false;
    }

    bool has_property(const std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }

        // Get engine reference for creating script_values
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        const auto& obj_state = object_stack_.top();
        return obj_state.map.find(script_value(name, eng)) != obj_state.map.end();
    }

    // Seek to a specific property by name
    // Fast path: if properties are in sorted order, sequential access works
    // Slow path: fall back to map lookup for out-of-order access
    bool seek_property(const std::string& name) {
        if (object_stack_.empty()) {
            return false;
        }

        // Get engine reference for creating script_values
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        auto& obj_state = object_stack_.top();

        // Fast path: check if current iterator matches (skip metadata)
        while (obj_state.current != obj_state.map.end()) {
            const std::string& prop_name = obj_state.current->first.as_string();
            if (prop_name == "_type_" || prop_name == "_version_") {
                ++obj_state.current;
                continue;
            }
            // Check if this is the property we want
            if (prop_name == name) {
                current_property_value_ = &obj_state.current->second;
                ++obj_state.current;
                return true;
            }
            // Not a match - properties are out of order, use slow path
            break;
        }

        // Slow path: lookup by name in map
        auto it = obj_state.map.find(script_value(name, eng));
        if (it == obj_state.map.end()) {
            return false;
        }

        current_property_value_ = &it->second;
        return true;
    }

    script_value read_value() {
        // Track depth - throws on overflow (hard failure, no partial data)
        depth_guard guard(current_depth_);

        script_value result;

        if (current_property_value_) {
            result = *current_property_value_;
            current_property_value_ = nullptr;
        } else if (!array_stack_.empty()) {
            auto& arr_state = array_stack_.top();
            if (arr_state.index < arr_state.array.size()) {
                result = arr_state.array[arr_state.index++];
            } else {
                result = *current_value_;
            }
        } else {
            result = *current_value_;
        }
        
        // Check for shared_ptr reconstruction
        if (result.is_map()) {
            const auto& map = result.as_map();

            // Get engine reference for creating script_values
            auto eng = engine_ref_;
            if (!eng) {
                throw serialization_error("Engine reference expired during JSON deserialization");
            }

            // Check for weak_ptr serialization format: {"$weak_ptr_id": id, "$weak_ptr_data": <optional>}
            auto weak_id_it = map.find(script_value("$weak_ptr_id", eng));
            if (weak_id_it != map.end() && weak_id_it->second.type() == script_value_type::jai_int_type) {
                uint32_t id = static_cast<uint32_t>(weak_id_it->second.as<script_int>());

                if (id == 0) {
                    // Null or expired weak_ptr
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

                // First time seeing this ID - check for inline data
                auto weak_data_it = map.find(script_value("$weak_ptr_data", eng));
                if (weak_data_it != map.end()) {
                    script_value obj_val = weak_data_it->second;  // Already properly wrapped

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
                } else {
                    // No data found - return null weak_ptr (object was never serialized)
                    script_value v(std::monostate{}, eng);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = jai::weaker_ptr<script_value::object_holder>();
                    return v;
                }
            }
        }
        
        return result;
    }

private:
    std::string json_;
    size_t pos_;
    script_value root_value_;
    const script_value* current_value_;
    const script_value* current_property_value_ = nullptr;
    script_value temp_value_;  // Temporary storage for array elements
    std::vector<std::string> path_stack_;

    // JSON parsing methods
    script_value parse_json() {
        pos_ = 0;
        return parse_value();
    }
    
    script_value parse_value() {
        skip_whitespace();

        if (pos_ >= json_.length()) {
            throw serialization_error("Unexpected end of JSON");
        }

        // Get engine reference for creating script_values
        auto eng = engine_ref_;
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        char c = json_[pos_];

        if (c == '"') {
            return script_value(parse_string(), eng);
        } else if (c == '{') {
            return parse_object(eng);
        } else if (c == '[') {
            return parse_array(eng);
        } else if (c == 't' || c == 'f') {
            return parse_bool(eng);
        } else if (c == 'n') {
            return parse_null(eng);
        } else if (c == '-' || std::isdigit(c)) {
            return parse_number(eng);
        } else {
            // Build context for error message
            size_t start = (pos_ > 20) ? pos_ - 20 : 0;
            size_t len = std::min(size_t(40), json_.length() - start);
            std::string context = json_.substr(start, len);
            throw serialization_error("Unexpected character '" + std::string(1, c) +
                "' in JSON at position " + std::to_string(pos_) +
                ", near: ..." + context + "...");
        }
    }
    
    void skip_whitespace() {
        while (pos_ < json_.length() && std::isspace(json_[pos_])) {
            pos_++;
        }
    }
    
    char peek() {
        skip_whitespace();
        return pos_ < json_.length() ? json_[pos_] : '\0';
    }
    
    char advance() {
        skip_whitespace();
        return pos_ < json_.length() ? json_[pos_++] : '\0';
    }
    
    void expect(char c) {
        if (advance() != c) {
            throw serialization_error("Expected '" + std::string(1, c) + "' in JSON");
        }
    }
    
    std::string parse_string() {
        expect('"');
        std::string result;
        
        while (pos_ < json_.length() && json_[pos_] != '"') {
            if (json_[pos_] == '\\') {
                pos_++;
                if (pos_ >= json_.length()) {
                    throw serialization_error("Unexpected end of JSON string");
                }
                switch (json_[pos_]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 >= json_.length()) {
                            throw serialization_error("Invalid unicode escape");
                        }
                        std::string hex = json_.substr(pos_ + 1, 4);
                        pos_ += 4;
                        uint32_t codepoint = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
                        // Handle UTF-16 surrogate pairs
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            // High surrogate - look for low surrogate \uXXXX
                            if (pos_ + 2 < json_.length() && json_[pos_ + 1] == '\\' && json_[pos_ + 2] == 'u') {
                                if (pos_ + 6 < json_.length()) {
                                    std::string low_hex = json_.substr(pos_ + 3, 4);
                                    uint32_t low = static_cast<uint32_t>(std::stoul(low_hex, nullptr, 16));
                                    if (low >= 0xDC00 && low <= 0xDFFF) {
                                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                        pos_ += 6; // Skip the \uXXXX low surrogate
                                    }
                                }
                            }
                        }
                        // Encode as UTF-8
                        if (codepoint < 0x80) {
                            result += static_cast<char>(codepoint);
                        } else if (codepoint < 0x800) {
                            result += static_cast<char>(0xC0 | (codepoint >> 6));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else if (codepoint < 0x10000) {
                            result += static_cast<char>(0xE0 | (codepoint >> 12));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xF0 | (codepoint >> 18));
                            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default:
                        throw serialization_error("Invalid escape sequence");
                }
                pos_++;
            } else {
                result += json_[pos_++];
            }
        }
        
        expect('"');
        return result;
    }
    
    script_value parse_object(engine* eng) {
        expect('{');

        script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
        auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());

        if (peek() == '}') {
            advance();
            return map_val;
        }

        while (true) {
            std::string key = parse_string();
            expect(':');
            script_value value = parse_value();

            map[script_value(key, eng)] = value;

            char c = peek();
            if (c == '}') {
                advance();
                break;
            } else if (c == ',') {
                advance();
            } else {
                size_t start = (pos_ > 20) ? pos_ - 20 : 0;
                size_t len = std::min(size_t(40), json_.length() - start);
                std::string context = json_.substr(start, len);
                throw serialization_error("Expected ',' or '}' in JSON object at position " +
                    std::to_string(pos_) + ", got '" + std::string(1, c) + "', near: ..." + context + "...");
            }
        }

        return map_val;
    }

    script_value parse_array(engine* eng) {
        expect('[');

        script_value array_val = script_value::make_array(nullptr, eng);
        auto& arr = const_cast<std::vector<script_value>&>(array_val.as_array());

        if (peek() == ']') {
            advance();
            return array_val;
        }

        while (true) {
            arr.push_back(parse_value());

            char c = peek();
            if (c == ']') {
                advance();
                break;
            } else if (c == ',') {
                advance();
            } else {
                size_t start = (pos_ > 20) ? pos_ - 20 : 0;
                size_t len = std::min(size_t(40), json_.length() - start);
                std::string context = json_.substr(start, len);
                throw serialization_error("Expected ',' or ']' in JSON array at position " +
                    std::to_string(pos_) + ", got '" + std::string(1, c) + "', near: ..." + context + "...");
            }
        }

        return array_val;
    }

    script_value parse_bool(engine* eng) {
        if (json_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return script_value(true, eng);
        } else if (json_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return script_value(false, eng);
        } else {
            throw serialization_error("Invalid boolean value");
        }
    }

    script_value parse_null(engine* eng) {
        if (json_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return script_value(std::monostate{}, eng);
        } else {
            throw serialization_error("Invalid null value");
        }
    }

    script_value parse_number(engine* eng) {
        size_t start = pos_;
        bool has_decimal = false;
        bool has_exponent = false;

        if (json_[pos_] == '-') pos_++;

        while (pos_ < json_.length() &&
               (std::isdigit(json_[pos_]) || json_[pos_] == '.' ||
                json_[pos_] == 'e' || json_[pos_] == 'E' ||
                json_[pos_] == '+' || json_[pos_] == '-')) {
            if (json_[pos_] == '.') has_decimal = true;
            if (json_[pos_] == 'e' || json_[pos_] == 'E') has_exponent = true;
            pos_++;
        }

        std::string num_str = json_.substr(start, pos_ - start);

        if (has_decimal || has_exponent) {
            return script_value(std::stod(num_str), eng);
        } else {
            return script_value(static_cast<script_int>(std::stoll(num_str)), eng);
        }
    }
    
    struct ObjectState {
        const std::map<script_value, script_value>& map;
        std::map<script_value, script_value>::const_iterator current;
    };
    
    struct ArrayState {
        const std::vector<script_value>& array;
        size_t index;
    };
    
    std::stack<ObjectState> object_stack_;
    std::stack<ArrayState> array_stack_;
};

// Archive ID trait specializations for dispatch pattern
template<> struct writer_archive_id_trait<json_archive_writer> {
    static constexpr writer_archive_id value = writer_archive_id::json;
};
template<> struct reader_archive_id_trait<json_archive_reader> {
    static constexpr reader_archive_id value = reader_archive_id::json;
};

} // namespace serialization
} // namespace jai