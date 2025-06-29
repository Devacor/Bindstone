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
    json_archive_writer(int indent = 2) : indent_(indent), current_depth_(0) {}
    
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
        if (in_container() && !first_in_container_.top()) {
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
        if (in_container() && !first_in_container_.top()) {
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
    
    void write_value(const script_value& value) override {
        // Handle shared_ptr types with proper tracking, delegate others to stdlib
        switch (value.type()) {
            case script_value_type::jai_shared_ptr_type: {
                // Use base class shared_ptr tracking
                auto shared_val = std::get<std::shared_ptr<script_value>>(value.storage_);
                const void* raw_ptr = shared_val.get();
                
                auto [id, is_new] = track_shared_ptr(raw_ptr);
                
                if (id == 0) {
                    // Null shared_ptr
                    oss_ << "null";
                } else if (is_new && shared_val) {
                    // First time: serialize with ID annotation
                    oss_ << '{';
                    current_depth_++;
                    
                    write_newline();
                    write_indent();
                    oss_ << "\"_shared_ptr_id\": " << id << ',';
                    
                    write_newline();
                    write_indent();
                    oss_ << "\"_shared_ptr_value\": ";
                    write_value(*shared_val);  // Recursive call
                    
                    current_depth_--;
                    write_newline();
                    write_indent();
                    oss_ << '}';
                } else {
                    // Subsequent times: just the reference
                    oss_ << "{\"_shared_ptr_ref\": " << id << "}";
                }
                break;
            }
            
            case script_value_type::jai_weak_ptr_type: {
                // Handle weak_ptr
                auto weak_val = std::get<std::weak_ptr<script_value>>(value.storage_);
                if (auto shared_val = weak_val.lock()) {
                    const void* raw_ptr = shared_val.get();
                    auto [id, is_new] = track_shared_ptr(raw_ptr);
                    
                    if (id == 0) {
                        oss_ << "null";
                    } else {
                        // Weak ptrs are always references
                        oss_ << "{\"_weak_ptr_ref\": " << id << "}";
                    }
                } else {
                    // Expired weak_ptr
                    oss_ << "null";
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
            
            default:
                // Use the existing to_json implementation for other types
                oss_ << to_json_impl_fallback(value, 0, 0);  // No indentation, we handle it ourselves
                break;
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
    json_archive_reader(const std::string& json_string) : json_(json_string), pos_(0) {
        root_value_ = parse_json();
        current_value_ = &root_value_;
        path_stack_.push_back("");
    }
    
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
        if (!current_value_->is_map()) {
            return false;
        }
        
        const auto& map = current_value_->as_map();
        
        // Read _type_ field
        auto type_it = map.find(script_value("_type_"));
        if (type_it != map.end() && type_it->second.is_string()) {
            type_name = type_it->second.as_string();
        } else {
            type_name = "";
        }
        
        // Read _version_ field (default to 1)
        auto version_it = map.find(script_value("_version_"));
        if (version_it != map.end() && version_it->second.is_int()) {
            version = static_cast<uint32_t>(version_it->second.as_int());
        } else {
            version = 1;
        }
        
        version_ = version;
        
        // Set up iteration state
        object_stack_.push(ObjectState{map, map.begin()});
        
        return true;
    }
    
    void end_object() override {
        if (!object_stack_.empty()) {
            object_stack_.pop();
        }
    }
    
    size_t begin_array() override {
        if (!current_value_->is_array()) {
            return 0;
        }
        
        const auto& arr = current_value_->as_array();
        array_stack_.push(ArrayState{arr, 0});
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
    
    bool has_property(const std::string& name) override {
        if (object_stack_.empty()) {
            return false;
        }
        
        const auto& obj_state = object_stack_.top();
        return obj_state.map.find(script_value(name)) != obj_state.map.end();
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
            
            // Check for shared_ptr_id (first occurrence)
            auto id_it = map.find(script_value("_shared_ptr_id"));
            if (id_it != map.end() && id_it->second.type() == script_value_type::jai_int_type) {
                uint32_t id = static_cast<uint32_t>(id_it->second.as<script_int>());
                auto value_it = map.find(script_value("_shared_ptr_value"));
                if (value_it != map.end()) {
                    script_value shared_ptr_val = script_value::make_shared_ptr(value_it->second);
                    register_shared_ptr(id, shared_ptr_val);
                    return shared_ptr_val;
                }
            }
            
            // Check for shared_ptr_ref (subsequent occurrences)
            auto ref_it = map.find(script_value("_shared_ptr_ref"));
            if (ref_it != map.end() && ref_it->second.type() == script_value_type::jai_int_type) {
                uint32_t id = static_cast<uint32_t>(ref_it->second.as<script_int>());
                return get_shared_ptr(id);
            }
            
            // Check for weak_ptr_ref
            auto weak_ref_it = map.find(script_value("_weak_ptr_ref"));
            if (weak_ref_it != map.end() && weak_ref_it->second.type() == script_value_type::jai_int_type) {
                uint32_t id = static_cast<uint32_t>(weak_ref_it->second.as<script_int>());
                script_value shared_val = get_shared_ptr(id);
                return script_value::make_weak_ptr(shared_val);
            }
        }
        
        return result;
    }

protected:
    void skip_property_impl(type_info_ptr type) override {
        // For JSON, we just read and discard the value
        read_value();
    }

private:
    std::string json_;
    size_t pos_;
    script_value root_value_;
    const script_value* current_value_;
    const script_value* current_property_value_ = nullptr;
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
        
        char c = json_[pos_];
        
        if (c == '"') {
            return script_value(parse_string());
        } else if (c == '{') {
            return parse_object();
        } else if (c == '[') {
            return parse_array();
        } else if (c == 't' || c == 'f') {
            return parse_bool();
        } else if (c == 'n') {
            return parse_null();
        } else if (c == '-' || std::isdigit(c)) {
            return parse_number();
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
    
    script_value parse_object() {
        expect('{');
        
        script_value map_val = script_value::make_map(type_info::make_string(), nullptr);
        auto& map = const_cast<std::map<script_value, script_value>&>(map_val.as_map());
        
        if (peek() == '}') {
            advance();
            return map_val;
        }
        
        while (true) {
            std::string key = parse_string();
            expect(':');
            script_value value = parse_value();
            
            map[script_value(key)] = value;
            
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
    
    script_value parse_array() {
        expect('[');
        
        script_value array_val = script_value::make_array(nullptr);
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
    
    script_value parse_bool() {
        if (json_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return script_value(true);
        } else if (json_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return script_value(false);
        } else {
            throw serialization_error("Invalid boolean value");
        }
    }
    
    script_value parse_null() {
        if (json_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return script_value();
        } else {
            throw serialization_error("Invalid null value");
        }
    }
    
    script_value parse_number() {
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
            return script_value(std::stod(num_str));
        } else {
            return script_value(static_cast<script_int>(std::stoll(num_str)));
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