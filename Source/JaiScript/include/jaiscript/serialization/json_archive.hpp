#pragma once

#include "archive.hpp"
#include <stack>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace jai {
namespace serialization {

class json_archive_writer : public archive_writer {
public:
    json_archive_writer(int indent = 2) : archive_writer(), indent_(indent), current_depth_(0) {}
    json_archive_writer(int indent, std::weak_ptr<engine> eng) : archive_writer(eng), indent_(indent), current_depth_(0) {}
    explicit json_archive_writer(std::weak_ptr<engine> eng) : archive_writer(eng), indent_(2), current_depth_(0) {}

    // JSON doesn't need explicit property keys array (object keys are self-describing)
    bool needs_property_keys() const override { return false; }

    // Basic type serialization
    void write_int8(int8_t value) override { write_json_value(value); }
    void write_int16(int16_t value) override { write_json_value(value); }
    void write_int32(int32_t value) override { write_json_value(value); }
    void write_int64(int64_t value) override { write_json_value(value); }
    void write_uint8(uint8_t value) override { write_json_value(value); }
    void write_uint16(uint16_t value) override { write_json_value(value); }
    void write_uint32(uint32_t value) override { write_json_value(value); }
    void write_uint64(uint64_t value) override { write_json_value(value); }
    void write_float32(float value) override { write_json_value(value); }
    void write_float64(double value) override { write_json_value(value); }
    void write_bool(bool value) override { write_json_value(value); }
    
    void write_string(const std::string& value) override {
        write_json_value('"' + escape_json_string_local(value) + '"');
    }
    
    void write_binary(const void* data, size_t size) override {
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
    
    // Object/array structure
    void begin_object(const std::string& type_name, uint32_t version) override {
        // Only add comma if we're in an array (not object, since write_property_name handles that)
        if (in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        current_depth_++;
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
    
    void end_object() override {
        current_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << '}';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }
    
    void begin_array(size_t size) override {
        // Only add comma if we're in an array (not object, since write_property_name handles that)
        if (in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '[';
        current_depth_++;
        container_stack_.push(ContainerType::Array);
        first_in_container_.push(true);
    }
    
    void end_array() override {
        current_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << ']';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }
    
    void write_property_name(const std::string& name) override {
        if (!first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '"' << escape_json_string_local(name) << "\": ";
        first_in_container_.top() = false;
    }

    // Map serialization - JSON uses native object format
    void begin_map(size_t size) override {
        // Same as begin_array but we'll write it as an object
        if (in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '{';
        current_depth_++;
        container_stack_.push(ContainerType::Object);
        first_in_container_.push(true);
    }

    void end_map() override {
        current_depth_--;
        container_stack_.pop();
        first_in_container_.pop();
        write_newline();
        write_indent();
        oss_ << '}';
        if (in_container()) {
            first_in_container_.top() = false;
        }
    }

    void write_map_key(const std::string& key) override {
        // Same as write_property_name
        if (!first_in_container_.top()) {
            oss_ << ',';
        }
        write_newline();
        write_indent();
        oss_ << '"' << escape_json_string_local(key) << "\": ";
        first_in_container_.top() = false;
    }

    void write_value(const script_value& value) override {
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
            
            default: {
                // Runtime validation: Check if object types are registered
                if (value.type() == script_value_type::jai_object_type) {
                    auto type_info = value.get_type_info();
                    std::string type_name = type_info ? type_info->type_name : "unknown";

                    auto eng = engine_ref_.lock();
                    if (eng) {
                        const auto* metadata = eng->get_serialization_registry().get_class_metadata(type_name);
                        if (!metadata || metadata->properties.empty()) {
                            throw serialization_error(
                                "Cannot serialize unregistered type '" + type_name + "'. " +
                                "Register the type with class_builder before serialization, or ensure it has registered properties."
                            );
                        }
                    }
                }

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
        for (char c : str) {
            switch (c) {
                case '"':  oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (c >= 0x20 && c <= 0x7E) {
                        oss << c;
                    } else {
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') 
                            << static_cast<int>(static_cast<unsigned char>(c));
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
    int current_depth_;
    std::ostringstream oss_;
    std::stack<ContainerType> container_stack_;
    std::stack<bool> first_in_container_;
    
    bool in_container() const { return !container_stack_.empty(); }
    
    void write_indent() {
        if (indent_ > 0) {
            oss_ << std::string(current_depth_ * indent_, ' ');
        }
    }
    
    void write_newline() {
        if (indent_ > 0) {
            oss_ << '\n';
        }
    }
    
    template<typename T>
    void write_json_value(T value) {
        if (in_container() && container_stack_.top() == ContainerType::Array && !first_in_container_.top()) {
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

class json_archive_reader : public archive_reader {
public:
    // Engine is REQUIRED for JSON reading since we need to create script_values
    json_archive_reader(const std::string& json_string, std::weak_ptr<engine> eng)
        : archive_reader(eng), json_(json_string), pos_(0) {
        if (eng.expired()) {
            throw serialization_error("json_archive_reader requires a valid engine reference");
        }
        root_value_ = parse_json();
        current_value_ = &root_value_;
        path_stack_.push_back("");
    }

    virtual ~json_archive_reader() = default;

    // JSON doesn't need explicit property keys array (object keys are self-describing)
    bool needs_property_keys() const override { return false; }

    // Basic type deserialization
    int8_t read_int8() override { return static_cast<int8_t>(read_value().as<script_int>()); }
    int16_t read_int16() override { return static_cast<int16_t>(read_value().as<script_int>()); }
    int32_t read_int32() override { return static_cast<int32_t>(read_value().as<script_int>()); }
    int64_t read_int64() override { return read_value().as<script_int>(); }
    uint8_t read_uint8() override { return static_cast<uint8_t>(read_value().as<script_int>()); }
    uint16_t read_uint16() override { return static_cast<uint16_t>(read_value().as<script_int>()); }
    uint32_t read_uint32() override { return static_cast<uint32_t>(read_value().as<script_int>()); }
    uint64_t read_uint64() override { return static_cast<uint64_t>(read_value().as<script_int>()); }
    float read_float32() override { return static_cast<float>(read_value().as<script_float>()); }
    double read_float64() override { return read_value().as<script_float>(); }
    bool read_bool() override { return read_value().as<script_bool>(); }
    std::string read_string() override { return read_value().as<script_string>(); }
    
    std::vector<uint8_t> read_binary(size_t size) override {
        std::string hex_str = read_value().as<script_string>();
        std::vector<uint8_t> result;
        for (size_t i = 0; i < hex_str.length(); i += 2) {
            std::string byte_str = hex_str.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
            result.push_back(byte);
        }
        return result;
    }
    
    // Object/array structure
    bool begin_object(std::string& type_name, uint32_t& version) override {
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
        auto eng = engine_ref_.lock();
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }
        auto eng_weak = eng->weak_from_this();
        
        // Read _type_ field
        auto type_it = map.find(script_value("_type_", eng_weak));
        if (type_it != map.end() && type_it->second.is_string()) {
            type_name = type_it->second.as_string();
        } else {
            type_name = "";
        }
        
        // Read _version_ field (default to 1)
        auto version_it = map.find(script_value("_version_", eng_weak));
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
    
    void end_object() override {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }
    
    size_t begin_array() override {
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
    
    void end_array() override {
        if (!array_stack_.empty()) {
            array_stack_.pop();
        }
    }
    
    bool read_property_name(std::string& name) override {
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
    size_t begin_map() override {
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

    void end_map() override {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }

    bool read_map_key(std::string& key) override {
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

    bool has_property(const std::string& name) override {
        if (object_stack_.empty()) {
            return false;
        }
        
        // Get engine reference for creating script_values
        auto eng = engine_ref_.lock();
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }
        auto eng_weak = eng->weak_from_this();
        
        const auto& obj_state = object_stack_.top();
        return obj_state.map.find(script_value(name, eng_weak)) != obj_state.map.end();
    }
    
    script_value read_value() override {
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
            auto eng = engine_ref_.lock();
            if (!eng) {
                throw serialization_error("Engine reference expired during JSON deserialization");
            }
            auto eng_weak = eng->weak_from_this();
            
            // Check for weak_ptr serialization format: {"$weak_ptr_id": id, "$weak_ptr_data": <optional>}
            auto weak_id_it = map.find(script_value("$weak_ptr_id", eng_weak));
            if (weak_id_it != map.end() && weak_id_it->second.type() == script_value_type::jai_int_type) {
                uint32_t id = static_cast<uint32_t>(weak_id_it->second.as<script_int>());

                if (id == 0) {
                    // Null or expired weak_ptr
                    script_value v(std::monostate{}, eng_weak);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = std::weak_ptr<script_value::object_holder>();
                    return v;
                }

                // Check if we've already reconstructed this object
                script_value existing_obj = get_shared_ptr(id);

                if (!existing_obj.is_invalid()) {
                    // Object was already reconstructed, get its object_holder and create weak_ptr
                    auto obj_holder = existing_obj.get_object_holder();
                    if (obj_holder) {
                        script_value weak_val(std::monostate{}, eng_weak);
                        weak_val.set_type_info(eng->get_type_info_weak_ptr(existing_obj.get_type_info()));
                        weak_val.storage_ = std::weak_ptr<script_value::object_holder>(obj_holder);
                        return weak_val;
                    }
                }

                // First time seeing this ID - check for inline data
                auto weak_data_it = map.find(script_value("$weak_ptr_data", eng_weak));
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
                    script_value weak_val(std::monostate{}, eng_weak);
                    weak_val.set_type_info(eng->get_type_info_weak_ptr(obj_val.get_type_info()));
                    weak_val.storage_ = std::weak_ptr<script_value::object_holder>(obj_holder);
                    return weak_val;
                } else {
                    // No data found - return null weak_ptr (object was never serialized)
                    script_value v(std::monostate{}, eng_weak);
                    v.set_type_info(eng->get_type_info_weak_ptr(nullptr));
                    v.storage_ = std::weak_ptr<script_value::object_holder>();
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
        auto eng = engine_ref_.lock();
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }
        auto eng_weak = eng->weak_from_this();
        
        char c = json_[pos_];
        
        if (c == '"') {
            return script_value(parse_string(), eng_weak);
        } else if (c == '{') {
            return parse_object(eng_weak);
        } else if (c == '[') {
            return parse_array(eng_weak);
        } else if (c == 't' || c == 'f') {
            return parse_bool(eng_weak);
        } else if (c == 'n') {
            return parse_null(eng_weak);
        } else if (c == '-' || std::isdigit(c)) {
            return parse_number(eng_weak);
        } else {
            throw serialization_error("Unexpected character in JSON: " + std::string(1, c));
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
                        int codepoint = std::stoi(hex, nullptr, 16);
                        if (codepoint < 128) {
                            result += static_cast<char>(codepoint);
                        } else {
                            result += '?'; // Placeholder
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
    
    script_value parse_object(std::weak_ptr<engine> eng_weak) {
        expect('{');

        auto eng = eng_weak.lock();
        if (!eng) {
            throw serialization_error("Engine reference expired during JSON deserialization");
        }

        script_value map_val = script_value::make_map(eng->get_type_info_string(), nullptr, eng_weak);
        auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());
        
        if (peek() == '}') {
            advance();
            return map_val;
        }
        
        while (true) {
            std::string key = parse_string();
            expect(':');
            script_value value = parse_value();
            
            map[script_value(key, eng_weak)] = value;
            
            char c = peek();
            if (c == '}') {
                advance();
                break;
            } else if (c == ',') {
                advance();
            } else {
                throw serialization_error("Expected ',' or '}' in JSON object");
            }
        }
        
        return map_val;
    }
    
    script_value parse_array(std::weak_ptr<engine> eng_weak) {
        expect('[');
        
        script_value array_val = script_value::make_array(nullptr, eng_weak);
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
                throw serialization_error("Expected ',' or ']' in JSON array");
            }
        }
        
        return array_val;
    }
    
    script_value parse_bool(std::weak_ptr<engine> eng_weak) {
        if (json_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return script_value(true, eng_weak);
        } else if (json_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return script_value(false, eng_weak);
        } else {
            throw serialization_error("Invalid boolean value");
        }
    }
    
    script_value parse_null(std::weak_ptr<engine> eng_weak) {
        if (json_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return script_value(std::monostate{}, eng_weak);
        } else {
            throw serialization_error("Invalid null value");
        }
    }
    
    script_value parse_number(std::weak_ptr<engine> eng_weak) {
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
            return script_value(std::stod(num_str), eng_weak);
        } else {
            return script_value(static_cast<script_int>(std::stoll(num_str)), eng_weak);
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

} // namespace serialization
} // namespace jai